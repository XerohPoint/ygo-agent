#!/usr/bin/env python3
"""Play one cell of duels with audited public observations and return results.

A cell is one vector env: fixed decks, consecutive seeds, one policy per seat. It is a pure
function of (seeds, decks, policy checkpoints). A worker is a long-lived subprocess, so a
native abort kills the worker and voids its cell without touching the learner. A voided
cell contributes no games at all: incomplete games are never wins or losses.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import select
import signal
import subprocess
import sys
import tempfile
import threading
import time
import traceback

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import decks as deck_registry  # noqa: E402
from decks import ROOT  # noqa: E402

MAX_STEPS = 10000
MAX_OPTIONS = 256
BAD_MARKERS = ("timeout", "Script not found", "Card not found", "Spec not found", "Spec2index", "cannot find id",
               "MSG_RETRY", "Error", "stack traceback", "terminate called")
CHECKPOINT_FORMAT = "edison_net_v1"


def checkpoint_paths(stem):
    stem = Path(stem)
    return stem.with_suffix(".msgpack"), stem.with_suffix(".json")


def load_checkpoint(stem, expected_sha=None):
    """Read a frozen net. Fails closed on format, code list or hash mismatch."""
    import flax
    from net import NetConfig
    params_path, meta_path = checkpoint_paths(stem)
    meta = json.loads(meta_path.read_text())
    if meta.get("format") != CHECKPOINT_FORMAT:
        raise ValueError("Unknown checkpoint format")
    raw = params_path.read_bytes()
    sha = hashlib.sha256(raw).hexdigest()
    if sha != meta["params_sha256"] or (expected_sha and sha != expected_sha):
        raise ValueError("Checkpoint hash mismatch")
    codes_sha, _ = deck_registry.load_codes()
    if meta["codes_sha256"] != codes_sha:
        raise ValueError("Checkpoint was trained on a different code list")
    return NetConfig(**meta["config"]), flax.serialization.msgpack_restore(raw), meta, sha


class NetPolicy:
    """Batched inference for one network shape. Parameters are passed per call."""

    shared = {}

    def __init__(self, stem, sha):
        import jax
        from net import EdisonNet
        config, params, self.meta, self.sha = load_checkpoint(stem, sha)
        self.params = jax.tree_util.tree_map(lambda x: jax.numpy.asarray(x), params)
        if config not in NetPolicy.shared:
            model = EdisonNet(config)
            NetPolicy.shared[config] = jax.jit(lambda p, b: model.apply({"params": p}, b))
        self.apply = NetPolicy.shared[config]

    def infer(self, packed):
        import numpy as np
        from obs import pad_batch
        batch = packed["cards"].shape[0]
        logits, values = self.apply(self.params, pad_batch(packed))
        n = int(packed["offsets"][-1])
        return np.asarray(logits)[:n].astype(np.float64), np.asarray(values)[:batch].astype(np.float64)


class BaselinePolicy:
    """The frozen NumPy learner, one decision at a time, on the generated code list."""

    def __init__(self, path, greedy, metadata, seed):
        from ygoai.edison_rl import RLModel, ContextEncoder
        self.model = RLModel.load(path)
        self.encoder = ContextEncoder(metadata, seed)
        self.greedy = greedy
        import numpy as np
        self.rng = np.random.default_rng(seed ^ 0x50504F)

    def choose(self, cards, glob, actions, decision, selection):
        import numpy as np
        from ygoai.edison_rl import distribution
        x = self.model.mask_features(self.encoder.encode(cards, glob, actions, decision, selection))
        logits, _ = self.model.forward(x)
        probs, _, _ = distribution(logits, np.array([0, len(x)]))
        choice = int(np.argmax(probs)) if self.greedy else int(self.rng.choice(len(x), p=probs))
        self.encoder.observe(cards, actions[choice])
        return choice


class RepeatGuard:
    """Steer greedy play off a public state it has already acted on.

    A free effect that resolves with no change, such as one negated by Destiny HERO - Plasma,
    would otherwise repeat until the decision limit. The key comes only from the acting player's
    observation. When every option has been tried, argmax repeats and the limit voids the cell.
    """

    def __init__(self):
        self.tried = {}
        self.fires = 0

    def choose(self, key, logp):
        import numpy as np
        tried = self.tried.setdefault(key, set())
        order = [int(i) for i in np.argsort(-logp, kind="stable")]
        choice = next((i for i in order if i not in tried), order[0])
        self.fires += bool(tried)
        tried.add(choice)
        return choice


def spec_key(spec):
    return json.dumps(spec, sort_keys=True)


class Runtime:
    def __init__(self):
        import numpy as np
        import ygoenv
        from ygoenv.ygopro import init_module
        self.np, self.ygoenv = np, ygoenv
        reg = deck_registry.registry()
        # Held-out lists for evaluation only, as {name: path}. They never enter the code list or training.
        extra = os.environ.get("EDISON_EXTRA_DECKS")
        if extra:
            added = json.loads(Path(extra).read_text())
            if set(added) & set(reg):
                raise ValueError("An extra deck reuses a registry name")
            reg = {**reg, **added}
        init_module(str(deck_registry.DB), str(deck_registry.CODES), {k: str(v) for k, v in reg.items()})
        self.metadata = deck_registry.card_metadata()
        self.n_codes = len(self.metadata)
        self.nets = {}

    def net(self, spec):
        key = spec["sha256"]
        if key not in self.nets:
            if len(self.nets) > 40:
                self.nets.clear()
            self.nets[key] = NetPolicy(spec["checkpoint"], spec["sha256"])
        return self.nets[key]

    def run_cell(self, job):
        np = self.np
        from obs import audit, pack, normalize_history_turn, segment_distribution, OBS_KEYS, ContractError
        from ygoai.edison_policy import Heuristic
        seed, games, specs = job["seed"], job["games"], job["policies"]
        if job.get("test_abort"):  # Used by test_learner.py to prove that an abort voids the cell.
            os.abort()
        max_steps = job.get("max_steps", MAX_STEPS)
        collect = job.get("collect", [False, False])
        env = self.ygoenv.make("YGOPro-v1", env_type="gymnasium", num_envs=games, num_threads=job.get("threads", 1),
                               seed=seed, deck1=job["deck0"], deck2=job["deck1"], play_mode="self", player=-1,
                               async_reset=False, greedy_reward=False, oppo_info=False, max_options=MAX_OPTIONS,
                               max_steps=max_steps, max_episode_steps=max_steps, timeout=10, verbose=False)
        # Per-seat agents for Python policies, seeded as agent_match.py seeds them.
        heuristics, baselines, nets = {}, {}, {}
        for seat, spec in enumerate(specs):
            if spec["kind"] == "heuristic":
                heuristics[seat] = [Heuristic(self.metadata, seed + eid * 2 + seat) for eid in range(games)]
            elif spec["kind"] == "baseline":
                baselines[seat] = [BaselinePolicy(spec["checkpoint"], spec["greedy"], self.metadata, seed + eid * 2 + seat)
                                   for eid in range(games)]
            elif spec["kind"] == "net":
                nets[seat] = self.net(spec)
            elif spec["kind"] != "random":
                raise ValueError("Unknown policy kind")
        randoms = [random.Random((seed + eid) ^ 0xED150) for eid in range(games)]
        samplers = [[np.random.default_rng([seed + eid, seat, 0x5050]) for seat in (0, 1)] for eid in range(games)]
        guards = {(eid, seat): RepeatGuard() for eid in range(games) for seat, spec in enumerate(specs) if spec.get("repeat_guard")}
        traces = [hashlib.sha256() for _ in range(games)]
        steps = [0] * games
        results = {}
        stored = []
        counters = {"net_seconds": 0.0, "net_calls": 0, "net_decisions": 0, "env_seconds": 0.0, "max_options": 0,
                    "audit_seconds": 0.0, "python_policy_seconds": 0.0, "heuristic_decisions": 0, "decisions": 0}
        started = time.perf_counter()
        obs, info = env.reset()
        while len(results) < games:
            ids = np.asarray(info["env_id"], dtype=np.int32)
            order = np.argsort(ids)
            ids = ids[order]
            obs_b = {key: obs[key][order] for key in OBS_KEYS}
            counts = np.asarray(info["num_options"])[order]
            legal = np.asarray(info["legal_options"])[order]
            overflow = np.asarray(info["action_overflow"])[order]
            seats = np.asarray(info["to_play"])[order]
            t0 = time.perf_counter()
            audit(obs_b, counts, legal, overflow, self.n_codes)
            counters["audit_seconds"] += time.perf_counter() - t0
            obs_b["h_actions_"] = normalize_history_turn(obs_b["h_actions_"], obs_b["global_"])
            counters["max_options"] = max(counters["max_options"], int(counts.max()))
            actions = np.zeros(len(ids), dtype=np.int64)
            # Group net rows by parameters, so self-play seats share one batch.
            groups = {}
            for seat, net in nets.items():
                groups.setdefault(net.sha, []).append(seat)
            for sha, group_seats in groups.items():
                rows = np.nonzero(np.isin(seats, group_seats))[0]
                if not rows.size:
                    continue
                sub = {key: obs_b[key][rows] for key in OBS_KEYS}
                packed = pack(sub, counts[rows])
                t0 = time.perf_counter()
                logits, values = nets[group_seats[0]].infer(packed)
                counters["net_seconds"] += time.perf_counter() - t0
                counters["net_calls"] += 1
                counters["net_decisions"] += rows.size
                logp, _ = segment_distribution(logits, packed["offsets"])
                offsets = packed["offsets"]
                chosen = np.zeros(rows.size, dtype=np.int64)
                for i, row in enumerate(rows):
                    eid, seat = int(ids[row]), int(seats[row])
                    segment = logp[offsets[i]:offsets[i + 1]]
                    if specs[seat]["greedy"] and specs[seat].get("repeat_guard"):
                        key = hashlib.sha256(b"".join(obs_b[name][row].tobytes() for name in
                                                      ("cards_", "selection_", "global_", "decision_"))
                                             + obs_b["actions_"][row, :int(counts[row])].tobytes()).digest()
                        chosen[i] = guards[eid, seat].choose(key, segment)
                    elif specs[seat]["greedy"]:
                        chosen[i] = int(np.argmax(segment))
                    else:
                        chosen[i] = min(int(np.searchsorted(np.cumsum(np.exp(segment)), samplers[eid][seat].random())),
                                        len(segment) - 1)
                actions[rows] = chosen
                collect_mask = np.asarray([collect[int(seats[row])] for row in rows])
                if collect_mask.any():
                    picked = np.nonzero(collect_mask)[0]
                    sizes = np.diff(offsets)
                    keep_flat = np.repeat(collect_mask, sizes)
                    stored.append({
                        "cards": packed["cards"][picked], "n_rows": packed["n_rows"][picked],
                        "actions": packed["actions"][keep_flat], "sizes": sizes[picked],
                        "hist": packed["hist"][picked], "decision": packed["decision"][picked],
                        "global": packed["global"][picked], "chosen": chosen[picked],
                        "logp": logp[offsets[picked] + chosen[picked]], "value": values[picked],
                        "game": ids[rows[picked]], "seat": seats[rows[picked]]})
            t0 = time.perf_counter()
            for row, (eid, seat) in enumerate(zip(ids.tolist(), seats.tolist())):
                kind = specs[seat]["kind"]
                if kind == "net":
                    continue
                count = int(counts[row])
                if kind == "random":
                    actions[row] = randoms[eid].randrange(count)
                elif kind == "heuristic":
                    counters["heuristic_decisions"] += 1
                    actions[row] = heuristics[seat][eid].choose(obs_b["cards_"][row], obs_b["global_"][row],
                                                               obs_b["actions_"][row, :count])[0]
                else:
                    actions[row] = baselines[seat][eid].choose(obs_b["cards_"][row], obs_b["global_"][row],
                                                               obs_b["actions_"][row, :count],
                                                               obs_b["decision_"][row], obs_b["selection_"][row])
            counters["python_policy_seconds"] += time.perf_counter() - t0
            for row, eid in enumerate(ids.tolist()):
                trace = traces[eid]
                count = int(counts[row])
                trace.update(obs_b["cards_"][row].tobytes())
                trace.update(obs_b["selection_"][row].tobytes())
                trace.update(obs_b["global_"][row].tobytes())
                trace.update(obs_b["actions_"][row, :count].tobytes())
                trace.update(obs_b["decision_"][row].tobytes())
                trace.update(int(actions[row]).to_bytes(2, "little"))
                steps[eid] += 1
                if steps[eid] >= MAX_STEPS:
                    raise ContractError("decision limit; not a completed duel")
            counters["decisions"] += len(ids)
            t0 = time.perf_counter()
            obs, rewards, terminated, truncated, info = env.step(actions, ids)
            counters["env_seconds"] += time.perf_counter() - t0
            if np.any(truncated):
                raise ContractError("environment truncation is not a natural result")
            keep = []
            for row, eid in enumerate(np.asarray(info["env_id"]).tolist()):
                if terminated[row]:
                    winner = int(info["winner"][row])
                    if winner not in (0, 1, 2):
                        raise ContractError("terminal without a winner")
                    results[eid] = {"env": eid, "seed": seed + eid, "steps": steps[eid], "winner": winner,
                                    "win_reason": int(info["win_reason"][row]), "trace": traces[eid].hexdigest()}
                else:
                    if float(rewards[row]) != 0:
                        raise ContractError("reward before terminal")
                    keep.append(row)
            if keep:
                obs = {key: obs[key][keep] for key in OBS_KEYS}
                info = {key: np.asarray(info[key])[keep] for key in
                        ("env_id", "num_options", "legal_options", "action_overflow", "to_play")}
        elapsed = time.perf_counter() - started
        counters["guard_fires"] = sum(guard.fires for guard in guards.values())
        report = {"status": "PASS", "duels": [results[i] for i in sorted(results)], "elapsed": elapsed,
                  "counters": counters, "learner_decisions": sum(len(s["chosen"]) for s in stored)}
        if stored:
            width = max(s["cards"].shape[1] for s in stored)
            total = sum(len(s["chosen"]) for s in stored)
            cards = np.zeros((total, width, 45), dtype=np.uint8)
            at = 0
            for s in stored:
                n = len(s["chosen"])
                cards[at:at + n, :s["cards"].shape[1]] = s["cards"]
                at += n
            out = {"cards": cards}
            for key in ("n_rows", "actions", "sizes", "hist", "decision", "global", "chosen", "logp", "value", "game", "seat"):
                out[key] = np.concatenate([s[key] for s in stored])
            winners = np.asarray([results[int(g)]["winner"] for g in out["game"]])
            out["outcome"] = np.where(winners == 2, 0.0, np.where(winners == out["seat"], 1.0, -1.0))
            path = Path(job["rollout"])
            sha = hashlib.sha256()
            for key in sorted(out):
                sha.update(key.encode())
                sha.update(np.ascontiguousarray(out[key]).tobytes())
            np.savez(path, **out)
            report["rollout_sha256"] = sha.hexdigest()
        return report


def configure_child_environment():
    env = dict(os.environ)
    env.update({"JAX_PLATFORMS": "cpu", "OPENBLAS_NUM_THREADS": "1", "OMP_NUM_THREADS": "1",
                "XLA_FLAGS": "--xla_cpu_multi_thread_eigen=false intra_op_parallelism_threads=1",
                "TF_CPP_MIN_LOG_LEVEL": "2", "PYTHONUNBUFFERED": "1"})
    return env


def serve(reply_fd):
    sys.path.insert(0, str(ROOT))
    sys.path.insert(0, str(ROOT / "ygoenv"))
    runtime = Runtime()
    with os.fdopen(reply_fd, "w", buffering=1) as reply:
        reply.write(json.dumps({"ready": True, "pid": os.getpid()}) + "\n")
        for line in sys.stdin:
            job = json.loads(line)
            try:
                result = runtime.run_cell(job)
            except Exception as error:  # Any contract failure voids the cell.
                result = {"status": "VOID", "reason": f"{type(error).__name__}: {error}",
                          "traceback": traceback.format_exc()[-1500:]}
            sys.stdout.flush()
            reply.write(json.dumps(result) + "\n")


class Worker:
    """Parent-side handle. Output goes to a log, which is scanned per job for native diagnostics.

    python: interpreter for this worker. A GPU learner can live in its own environment while
    actors keep the CPU environment that holds the certified native module.
    """

    def __init__(self, log_dir, index, start_timeout=120, python=None, cwd=None):
        self.log_dir, self.index, self.start_timeout = Path(log_dir), index, start_timeout
        self.python = python or sys.executable
        self.cwd = cwd or ROOT / "scripts"  # Tests point this at a directory of planted scripts.
        self.start()

    def start(self):
        self.log_dir.mkdir(parents=True, exist_ok=True)
        self.log_path = self.log_dir / f"worker-{self.index}.log"
        self.log = self.log_path.open("ab")
        reply_r, reply_w = os.pipe()
        self.process = subprocess.Popen(
            [self.python, str(Path(__file__).resolve()), "--serve", str(reply_w)],
            cwd=self.cwd, stdin=subprocess.PIPE, stdout=self.log, stderr=subprocess.STDOUT, text=True,
            env=configure_child_environment(), pass_fds=(reply_w,))
        os.close(reply_w)
        self.reply = os.fdopen(reply_r, "r", buffering=1)
        ready = self._read(self.start_timeout)
        if ready is None or not ready.get("ready"):
            raise RuntimeError(f"Worker {self.index} did not start; see {self.log_path}")

    def _read(self, timeout):
        deadline = time.monotonic() + timeout
        fd = self.reply.fileno()
        # A buffered line may already be available, so readline runs after select.
        while True:
            left = deadline - time.monotonic()
            if left <= 0:
                return None
            ready, _, _ = select.select([fd], [], [], min(left, 5))
            if ready:
                line = self.reply.readline()
                return json.loads(line) if line else None
            if self.process.poll() is not None:
                ready, _, _ = select.select([fd], [], [], 0)
                if not ready:
                    return None

    def run(self, job, timeout):
        before = self.log_path.stat().st_size
        try:
            self.process.stdin.write(json.dumps(job) + "\n")
            self.process.stdin.flush()
        except BrokenPipeError:
            self.restart()
            return {"status": "VOID", "reason": "worker pipe closed before the job"}
        result = self._read(timeout)
        if result is None:
            # The reply pipe can close just before a dying process is reaped.
            try:
                code = self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                code = None
            self.restart()
            return {"status": "VOID", "reason": f"worker died or timed out (exit {code})"}
        self.log.flush()
        with self.log_path.open("rb") as log:
            log.seek(before)
            text = log.read().decode(errors="replace")
        if result.get("status") == "PASS" and any(m.lower() in text.lower() for m in BAD_MARKERS):
            result = {"status": "VOID", "reason": "native diagnostic in output", "log_tail": text[-800:]}
        return result

    def restart(self):
        self.stop()
        self.start()

    def stop(self):
        try:
            self.process.kill()
            self.process.wait(timeout=10)
        except Exception:
            pass
        try:
            self.reply.close()
            self.log.close()
        except Exception:
            pass


class Pool:
    """A fixed set of workers. Results do not depend on which worker ran a cell."""

    def __init__(self, size, log_dir, cell_timeout=900, python=None):
        self.workers = [Worker(log_dir, i, python=python) for i in range(size)]
        self.cell_timeout = cell_timeout

    def map(self, jobs):
        results = [None] * len(jobs)
        lock = threading.Lock()
        queue = list(enumerate(jobs))

        def loop(worker):
            while True:
                with lock:
                    if not queue:
                        return
                    index, job = queue.pop(0)
                results[index] = worker.run(job, self.cell_timeout)

        threads = [threading.Thread(target=loop, args=(w,)) for w in self.workers]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        return results

    def close(self):
        for worker in self.workers:
            worker.stop()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serve", type=int, required=True)
    args = parser.parse_args()
    signal.signal(signal.SIGINT, signal.SIG_IGN)
    serve(args.serve)


if __name__ == "__main__":
    main()
