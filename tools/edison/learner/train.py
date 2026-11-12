#!/usr/bin/env python3
"""League self-play PPO for the Edison network.

Rounds are the unit of work: play a fixed schedule of cells, update, checkpoint. Every
cell is a pure function of its seeds and checkpoints, so a resumed run matches an
uninterrupted one, and the rollout and parameter hashes in rounds.jsonl prove it.

Device placement: actors always run on CPU in subprocesses, where the native env lives.
The learner runs wherever --learner-device says. Nothing else needs to change for GPU.
"""
import argparse
from dataclasses import asdict, dataclass, field
import hashlib
import itertools
import json
import os
from pathlib import Path
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import decks as deck_registry  # noqa: E402
from decks import ROOT, digest  # noqa: E402

FORMAT = "edison_league_run_v1"
TRAIN_BLOCK = (73000000, 73099999)
GAUNTLET_BLOCK = (73100000, 73399999)
DEV_BLOCK = (73500000, 73899999)
SOURCE_FILES = ("actor.py", "net.py", "obs.py", "train.py", "decks.py", deck_registry.CODES.name) + \
    (("identity_v2.json",) if deck_registry.CODE_LIST == "v2" else ())


@dataclass(frozen=True)
class TrainConfig:
    arm: str = "league"
    optimizer_seed: int = 1702
    rounds: int = 32
    games_per_cell: int = 8
    seed_base: int = TRAIN_BLOCK[0]
    deck_set: str = "fixtures"          # fixtures | gauntlet
    cells_per_round: int = 12
    opponents: str = "league"           # league | baseline_alternating | heuristic
    snapshot_every: int = 4
    learning_rate: float = 3e-4
    minibatch: int = 256
    epochs: int = 4
    clip: float = 0.2
    entropy: float = 0.01
    value_coef: float = 0.5
    grad_norm: float = 0.5
    kl_stop: float = 0.03
    gae_lambda: float = 0.95
    row_grouping: bool = True
    id_dim: int = 48
    channels: int = 96
    layers: int = 2
    heads: int = 4
    critic_hidden: int = 128
    # Identity dropout, code list v2 only. A game is left alone with probability id_dropout_clean; otherwise
    # each known card is hidden behind the unknown row for that whole game, at one rate drawn below id_dropout_max.
    id_dropout_clean: float = 1.0
    id_dropout_max: float = 0.0

    def net_config(self, n_codes):
        from net import NetConfig
        return NetConfig(n_codes=n_codes, id_dim=self.id_dim, channels=self.channels, layers=self.layers,
                         heads=self.heads, critic_hidden=self.critic_hidden, unknown_row=deck_registry.CODE_LIST == "v2")

    def identity(self):
        if self.id_dropout_max <= 0 or self.id_dropout_clean >= 1:
            return None
        if deck_registry.CODE_LIST != "v2":
            raise ValueError("Identity dropout needs code list v2")
        return {"dropout": {"clean": self.id_dropout_clean, "max_rate": self.id_dropout_max}}

    def hash(self):
        return hashlib.sha256(json.dumps(asdict(self), sort_keys=True).encode()).hexdigest()


def fixture_cells():
    return [(a, b, seat) for (a, b), seat in itertools.product(itertools.permutations(deck_registry.FIXTURE_NAMES, 2), (0, 1))]


def schedule(config, round_index, league):
    """All cells of one round. Pure function of config, round number and league contents."""
    import numpy as np
    games = config.games_per_cell
    per_round = config.cells_per_round * games
    round_seed = config.seed_base + round_index * per_round
    if round_seed + per_round - 1 > config.seed_base + 299999:
        raise ValueError("Schedule leaves its seed block")
    if config.deck_set == "fixtures":
        base = fixture_cells()
        if config.cells_per_round != len(base):
            raise ValueError("Fixture rounds have exactly 12 cells")
        cells = base
    else:
        # Same pairs across optimizer seeds, and neither optimizer nor results influence them.
        rng = np.random.default_rng([config.seed_base, round_index, 0x6A75])
        names = sorted(deck_registry.registry())
        cells = []
        for _ in range(config.cells_per_round):
            a, b = rng.choice(len(names), 2, replace=False)
            cells.append((names[a], names[b], int(rng.integers(2))))
    rows = []
    for index, (deck0, deck1, seat) in enumerate(cells):
        if config.opponents == "baseline_alternating":
            kind = "random" if round_index % 2 == 0 else "heuristic"
        elif config.opponents == "heuristic":
            kind = "heuristic"
        else:
            # Rotating thirds: fixed balance across decks and seats within any 3 rounds.
            kind = ("heuristic", "past", "self")[(index + round_index) % 3]
        row = {"round": round_index, "cell": index, "deck0": deck0, "deck1": deck1, "seat": seat, "opponent": kind,
               "seed": round_seed + index * games, "games": games}
        if kind == "past":
            rng = np.random.default_rng([config.optimizer_seed, round_index, index, 0x70])
            row["past"] = league[int(rng.integers(len(league)))]
        rows.append(row)
    return rows


def policy_specs(row, current):
    me = {"kind": "net", "checkpoint": current["stem"], "sha256": current["sha256"], "greedy": False}
    kind = row["opponent"]
    if kind == "self":
        return [me, me], [True, True]
    if kind == "past":
        other = {"kind": "net", "checkpoint": row["past"]["stem"], "sha256": row["past"]["sha256"], "greedy": False}
    else:
        other = {"kind": kind}
    return ([me, other], [True, False]) if row["seat"] == 0 else ([other, me], [False, True])


def save_checkpoint(stem, params, config, net_config, extra):
    import flax
    import jax
    import numpy as np
    from actor import CHECKPOINT_FORMAT, checkpoint_paths
    params_path, meta_path = checkpoint_paths(stem)
    raw = flax.serialization.msgpack_serialize(jax.tree_util.tree_map(np.asarray, jax.device_get(params)))
    sha = hashlib.sha256(raw).hexdigest()
    params_path.write_bytes(raw)
    meta = {"format": CHECKPOINT_FORMAT, "params_sha256": sha, "codes_sha256": deck_registry.load_codes()[0],
            **({"identity_sha256": deck_registry.load_identity()[0]} if deck_registry.CODE_LIST == "v2" else {}),
            "config": net_config.dict(), "train_config": asdict(config),
            "privacy": "public observation only: oppo_info=False, no info:deck, no centralized critic", **extra}
    meta_path.write_text(json.dumps(meta, indent=1) + "\n")
    return sha


def provenance():
    import jax
    import numpy as np
    def git(*args):
        return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    native = sorted((ROOT / "ygoenv/ygoenv/ygopro").glob("ygopro_ygoenv*.so"))
    lock = json.loads((ROOT / "tests/edison/agent_assets.lock.json").read_text())
    for relative, expected in lock.items():
        if digest(ROOT / relative) != expected:
            raise ValueError(f"Asset changed: {relative}")
    return {"format": FORMAT, "git_commit": git("rev-parse", "HEAD"),
            "learner_sources": {name: digest(HERE / name) for name in SOURCE_FILES},
            "native_binaries": {p.name: digest(p) for p in native},
            "edison_policy": digest(ROOT / "ygoai/edison_policy.py"),
            "assets_lock": digest(ROOT / "tests/edison/agent_assets.lock.json"),
            "codes_sha256": deck_registry.load_codes()[0], "code_list": deck_registry.CODE_LIST,
            "jax": jax.__version__, "numpy": np.__version__}


def compute_gae(rollouts, gae_lambda):
    """Per-seat GAE on the learner's own decisions. Gamma 1; the only reward is terminal."""
    import numpy as np
    n = len(rollouts["chosen"])
    returns = np.zeros(n, dtype=np.float64)
    advantages = np.zeros(n, dtype=np.float64)
    keys = rollouts["trajectory"]
    # Decisions are stored in play order inside a cell, so each trajectory is ascending.
    order = np.argsort(keys, kind="stable")
    start = 0
    weights = np.zeros(n, dtype=np.float64)
    while start < n:
        stop = start
        key = keys[order[start]]
        while stop < n and keys[order[stop]] == key:
            stop += 1
        chain = order[start:stop]
        values = rollouts["value"][chain]
        outcome = rollouts["outcome"][chain[-1]]
        adv = np.zeros(len(chain))
        next_adv, next_value = 0.0, outcome
        for i in range(len(chain) - 1, -1, -1):
            delta = next_value - values[i]
            next_adv = delta + gae_lambda * next_adv
            adv[i] = next_adv
            next_value = values[i]
        advantages[chain] = adv
        returns[chain] = adv + values
        weights[chain] = 1.0 / len(chain)
        start = stop
    return advantages, returns, weights


def load_rollouts(paths):
    import numpy as np
    parts = []
    for index, path in enumerate(paths):
        with np.load(path) as data:
            part = {key: data[key] for key in data.files}
        # Every game and seat in every cell is one trajectory.
        part["trajectory"] = (index * 1000000 + part["game"].astype(np.int64) * 2 + part["seat"].astype(np.int64))
        parts.append(part)
    width = max(p["cards"].shape[1] for p in parts)
    total = sum(len(p["chosen"]) for p in parts)
    cards = np.zeros((total, width, 45), dtype=np.uint8)
    at = 0
    for part in parts:
        n = len(part["chosen"])
        cards[at:at + n, :part["cards"].shape[1]] = part["cards"]
        at += n
    merged = {"cards": cards}
    for key in parts[0]:
        if key != "cards":
            merged[key] = np.concatenate([p[key] for p in parts])
    merged["offsets"] = np.r_[0, np.cumsum(merged["sizes"])].astype(np.int64)
    return merged


def make_minibatch(data, selection, batch_size):
    import numpy as np
    from obs import ACTION_BUCKETS, ROW_BUCKETS, bucket
    n = len(selection)
    n_rows = data["n_rows"][selection]
    msize = bucket(int(n_rows.max()), ROW_BUCKETS)
    cards = np.zeros((batch_size, msize, 45), dtype=np.uint8)
    take = min(msize, data["cards"].shape[1])
    cards[:n, :take] = data["cards"][selection, :take]
    row_mask = np.zeros((batch_size, msize), dtype=bool)
    row_mask[:n] = np.arange(msize)[None, :] < n_rows[:, None]
    sizes = data["sizes"][selection]
    total = int(sizes.sum())
    asize = bucket(total, ACTION_BUCKETS)
    starts = data["offsets"][selection]
    flat_index = np.repeat(starts - np.r_[0, np.cumsum(sizes)[:-1]], sizes) + np.arange(total)
    actions = np.zeros((asize, 12), dtype=np.uint8)
    actions[:total] = data["actions"][flat_index]
    segments = np.full(asize, batch_size, dtype=np.int32)
    segments[:total] = np.repeat(np.arange(n), sizes)
    chosen_flat = np.zeros(batch_size, dtype=np.int32)
    chosen_flat[:n] = np.r_[0, np.cumsum(sizes)[:-1]] + data["chosen"][selection]
    out = {"cards": cards, "row_mask": row_mask, "actions": actions, "segments": segments, "chosen_flat": chosen_flat}
    for key, dtype, shape in (("hist", np.uint8, (16, 14)), ("decision", np.int32, (12,)), ("global", np.uint8, (23,))):
        arr = np.zeros((batch_size, *shape), dtype=dtype)
        arr[:n] = data[key][selection]
        out[key] = arr
    for key in ("logp", "advantage", "return", "weight"):
        arr = np.zeros(batch_size, dtype=np.float32)
        arr[:n] = data[key][selection]
        out[key] = arr
    return out


def order_epoch(n_rows, minibatch, rng, grouping):
    """Index arrays for one epoch.

    Attention cost grows with the square of the widest row count in a minibatch, and one
    long game forces the whole minibatch wide. With grouping, decisions are shuffled inside
    row-count classes, and the resulting minibatches are then shuffled together.
    """
    import numpy as np
    from obs import ROW_BUCKETS
    n = len(n_rows)
    if not grouping:
        order = rng.permutation(n)
        return [order[start:start + minibatch] for start in range(0, n, minibatch)]
    classes = np.searchsorted(np.asarray(ROW_BUCKETS), n_rows)
    batches = []
    leftovers = []
    for value in range(len(ROW_BUCKETS)):
        members = rng.permutation(np.nonzero(classes == value)[0])
        whole = len(members) // minibatch * minibatch
        batches += [members[start:start + minibatch] for start in range(0, whole, minibatch)]
        leftovers.append(members[whole:])
    tail = np.concatenate(leftovers)
    batches += [tail[start:start + minibatch] for start in range(0, len(tail), minibatch)]
    return [batches[i] for i in rng.permutation(len(batches))]


def make_update(model, optimizer, config):
    import jax
    import jax.numpy as jnp
    import optax
    from net import segment_log_softmax

    def loss_fn(params, mb):
        logits, values = model.apply({"params": params}, mb)
        size = values.shape[0]
        logp_all = segment_log_softmax(logits, mb["segments"], size)
        logp = logp_all[mb["chosen_flat"]]
        weight = mb["weight"] / jnp.maximum(mb["weight"].sum(), 1e-9)
        ratio = jnp.exp(logp - mb["logp"])
        clipped = jnp.clip(ratio, 1 - config.clip, 1 + config.clip)
        policy = -jnp.minimum(ratio * mb["advantage"], clipped * mb["advantage"])
        valid = mb["segments"] < size
        plogp = jnp.where(valid, jnp.exp(logp_all) * logp_all, 0.0)
        entropy = -jax.ops.segment_sum(plogp, jnp.minimum(mb["segments"], size - 1), num_segments=size)
        residual = values - mb["return"]
        total = jnp.sum(weight * (policy - config.entropy * entropy + config.value_coef * 0.5 * residual ** 2))
        stats = {"entropy": jnp.sum(weight * entropy), "approx_kl": jnp.sum(weight * (ratio - 1 - jnp.log(ratio))),
                 "clip_fraction": jnp.sum(weight * (jnp.abs(ratio - 1) > config.clip)),
                 "value_mse": jnp.sum(weight * residual ** 2), "policy_loss": jnp.sum(weight * policy),
                 "loss": total}
        return total, stats

    def step(params, opt_state, mb):
        (_, stats), grads = jax.value_and_grad(loss_fn, has_aux=True)(params, mb)
        stats["grad_norm"] = optax.global_norm(grads)
        updates, opt_state = optimizer.update(grads, opt_state, params)
        return optax.apply_updates(params, updates), opt_state, stats

    return jax.jit(step)


def make_optimizer(config):
    import optax
    return optax.chain(optax.clip_by_global_norm(config.grad_norm), optax.adam(config.learning_rate, eps=1e-8))


def update_round(step, params, opt_state, data, config, round_index, device):
    import jax
    import numpy as np
    advantages, returns, weights = compute_gae(data, config.gae_lambda)
    mean = np.average(advantages, weights=weights)
    std = np.sqrt(np.average((advantages - mean) ** 2, weights=weights))
    data["advantage"] = (advantages - mean) / max(std, 1e-8)
    data["return"] = returns
    data["weight"] = weights
    if not (np.isfinite(data["advantage"]).all() and np.isfinite(returns).all()):
        raise ValueError("Non-finite training targets")
    rng = np.random.default_rng([config.optimizer_seed, round_index, 0x55])
    history = []
    n = len(data["chosen"])
    for epoch in range(config.epochs):
        sums, batches, updates = {}, 0, 0
        for selection in order_epoch(data["n_rows"], config.minibatch, rng, config.row_grouping):
            mb = make_minibatch(data, selection, config.minibatch)
            params, opt_state, stats = step(params, opt_state, jax.device_put(mb, device))
            for key, value in stats.items():
                sums[key] = sums.get(key, 0.0) + value
            batches += 1
        means = {key: float(value) / batches for key, value in sums.items()}
        means.update(epoch=epoch + 1, updates=batches)
        if not all(np.isfinite(v) for v in means.values()):
            raise ValueError("Non-finite training statistic")
        history.append(means)
        if means["approx_kl"] > config.kl_stop:
            break
    return params, opt_state, history


def write_state(output, state):
    scratch = output / "state.json.tmp"
    scratch.write_text(json.dumps(state, indent=1) + "\n")
    os.replace(scratch, output / "state.json")


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--learner-device", default="cpu", choices=("cpu", "gpu"))
    parser.add_argument("--learner-cpus", type=int, default=8, help="Affinity size for the learner on CPU")
    parser.add_argument("--learner-cpu-offset", type=int, default=0, help="Skip this many physical CPUs, for parallel runs")
    parser.add_argument("--worker-python", help="Interpreter for actors, when the learner runs in a GPU environment")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--stop-after", type=int, help="Stop cleanly after this round count; a later --resume continues")
    parser.add_argument("--keep-rollouts", action="store_true")
    parser.add_argument("--max-void-fraction", type=float, default=0.01)
    for name, kind in (("arm", str), ("optimizer_seed", int), ("rounds", int), ("games_per_cell", int),
                       ("seed_base", int), ("deck_set", str), ("cells_per_round", int), ("opponents", str),
                       ("snapshot_every", int), ("learning_rate", float), ("minibatch", int), ("epochs", int),
                       ("clip", float), ("entropy", float), ("value_coef", float), ("grad_norm", float),
                       ("kl_stop", float), ("gae_lambda", float), ("row_grouping", lambda v: v == "1"), ("id_dim", int), ("channels", int), ("layers", int),
                       ("heads", int), ("critic_hidden", int), ("id_dropout_clean", float), ("id_dropout_max", float)):
        parser.add_argument("--" + name.replace("_", "-"), type=kind, dest=name, default=None)
    return parser.parse_args(argv)


def main():
    args = parse_args()
    if args.learner_device == "cpu":
        os.environ["JAX_PLATFORMS"] = "cpu"
        from bench import physical_cpus
        os.sched_setaffinity(0, physical_cpus(args.learner_cpus + args.learner_cpu_offset)[args.learner_cpu_offset:])
    else:
        # Without the flag, repeated GPU runs differ from the first update. The desktop shares this card's memory.
        os.environ.setdefault("XLA_FLAGS", "--xla_gpu_exclude_nondeterministic_ops=true")
        os.environ.setdefault("XLA_PYTHON_CLIENT_PREALLOCATE", "false")
    os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")
    import flax
    import jax
    import numpy as np
    from actor import Pool, checkpoint_paths
    from net import EdisonNet, dummy_batch, param_count
    defaults = TrainConfig()
    config = TrainConfig(**{name: getattr(args, name) if getattr(args, name) is not None else getattr(defaults, name)
                            for name in defaults.__dataclass_fields__})
    output = args.output.resolve()
    state_path = output / "state.json"
    device = jax.devices(args.learner_device)[0]
    codes_sha, codes = deck_registry.load_codes()
    net_config = config.net_config(len(codes))
    model = EdisonNet(net_config)
    optimizer = make_optimizer(config)
    step = make_update(model, optimizer, config)
    prov = provenance()
    if args.resume:
        run = json.loads((output / "run.json").read_text())
        state = json.loads(state_path.read_text())
        if run["config"] != asdict(config):
            raise ValueError("Resume with a different configuration")
        for key, value in prov.items():
            if key != "git_commit" and run["provenance"][key] != value:
                raise ValueError(f"Resume with different provenance: {key}")
        stem = output / f"checkpoint-{state['round']:04d}"
        raw = checkpoint_paths(stem)[0].read_bytes()
        if hashlib.sha256(raw).hexdigest() != state["current"]["sha256"]:
            raise ValueError("Checkpoint does not match saved state")
        params = jax.device_put(flax.serialization.msgpack_restore(raw), device)
        opt_raw = (output / f"optimizer-{state['round']:04d}.msgpack").read_bytes()
        if hashlib.sha256(opt_raw).hexdigest() != state["optimizer_sha256"]:
            raise ValueError("Optimizer state does not match saved state")
        template = optimizer.init(params)
        opt_state = jax.device_put(flax.serialization.from_bytes(template, opt_raw), device)
    else:
        if output.exists() and any(output.iterdir()):
            raise ValueError("Refusing to overwrite: use a fresh --output or --resume")
        output.mkdir(parents=True, exist_ok=True)
        (output / "run.json").write_text(json.dumps(
            {"config": asdict(config), "config_hash": config.hash(), "provenance": prov, "workers": args.workers,
             "learner_device": args.learner_device, "learner_cpus": args.learner_cpus,
             "network_input": "cards_ global_ actions_ h_actions_ decision_ selection_ only"}, indent=1) + "\n")
        params = model.init(jax.random.PRNGKey(config.optimizer_seed), dummy_batch(net_config))["params"]
        params = jax.tree_util.tree_map(lambda x: x, flax.core.unfreeze(params))
        params = jax.device_put(params, device)
        opt_state = optimizer.init(params)
        sha = save_checkpoint(output / "checkpoint-0000", params, config, net_config, {"round": 0})
        current = {"stem": str(output / "checkpoint-0000"), "sha256": sha}
        state = {"round": 0, "current": current, "league": [current], "duels": 0, "void_games": 0,
                 "optimizer_sha256": None, "seconds": 0.0}
        print(f"parameters {param_count(params)}", flush=True)
    stop_at = min(config.rounds, args.stop_after or config.rounds)
    pool = None
    rollout_dir = output / "rollouts"
    rollout_dir.mkdir(exist_ok=True)
    try:
        with (output / "rounds.jsonl").open("a", buffering=1) as log:
            for round_index in range(state["round"], stop_at):
                if pool is None:
                    pool = Pool(args.workers, output / "logs", python=args.worker_python)
                started = time.perf_counter()
                rows = schedule(config, round_index, state["league"])
                jobs = []
                for row in rows:
                    policies, collect = policy_specs(row, state["current"])
                    jobs.append({"seed": row["seed"], "games": row["games"], "deck0": row["deck0"], "deck1": row["deck1"],
                                 "policies": policies, "collect": collect,
                                 **({"identity": config.identity()} if config.identity() else {}),
                                 "rollout": str(rollout_dir / f"round-{round_index:04d}-cell-{row['cell']:03d}.npz")})
                results = pool.map(jobs)
                collected = time.perf_counter()
                void = [(row, res) for row, res in zip(rows, results) if res["status"] != "PASS"]
                void_games = sum(row["games"] for row, _ in void)
                state["void_games"] += void_games
                state["duels"] += sum(row["games"] for row, res in zip(rows, results) if res["status"] == "PASS")
                if state["void_games"] > args.max_void_fraction * max(state["duels"] + state["void_games"], 96):
                    raise RuntimeError(f"Too many voided games: {state['void_games']}; last: {void[-1][1] if void else None}")
                passed = [(row, res, job) for row, res, job in zip(rows, results, jobs) if res["status"] == "PASS"]
                data = load_rollouts([job["rollout"] for _, res, job in passed if "rollout_sha256" in res])
                mb_start = time.perf_counter()
                params, opt_state, history = update_round(step, params, opt_state, data, config, round_index, device)
                updated = time.perf_counter()
                stem = output / f"checkpoint-{round_index + 1:04d}"
                sha = save_checkpoint(stem, params, config, net_config, {"round": round_index + 1, "duels": state["duels"]})
                opt_raw = flax.serialization.to_bytes(jax.device_get(opt_state))
                opt_path = output / f"optimizer-{round_index + 1:04d}.msgpack"
                opt_path.write_bytes(opt_raw)
                current = {"stem": str(stem), "sha256": sha}
                league = state["league"]
                if (round_index + 1) % config.snapshot_every == 0:
                    league = [*league, current]
                previous_round = state["round"]
                state.update(round=round_index + 1, current=current, league=league,
                             optimizer_sha256=hashlib.sha256(opt_raw).hexdigest(),
                             seconds=state["seconds"] + time.perf_counter() - started)
                write_state(output, state)
                # Only frozen league members and the last checkpoint must stay on disk.
                if previous_round > 0:
                    old = output / f"checkpoint-{previous_round:04d}"
                    if not any(member["stem"] == str(old) for member in league) and previous_round % 32 != 0:
                        for suffix in (".msgpack", ".json"):
                            old.with_suffix(suffix).unlink(missing_ok=True)
                    (output / f"optimizer-{previous_round:04d}.msgpack").unlink(missing_ok=True)
                wins = {}
                total_decisions = 0
                for row, res, _ in passed:
                    total_decisions += res["counters"]["decisions"]
                    if row["opponent"] == "self":
                        continue
                    bucket = wins.setdefault(row["opponent"], [0, 0, 0])
                    for duel in res["duels"]:
                        bucket[0] += duel["winner"] == row["seat"]
                        bucket[1] += duel["winner"] == 2
                        bucket[2] += 1
                record = {"round": round_index + 1, "duels": state["duels"], "void_games": void_games,
                          "void_reasons": [res.get("reason") for _, res in void],
                          "sampled_training_results": {k: {"wins": v[0], "draws": v[1], "games": v[2]} for k, v in wins.items()},
                          "learner_decisions": len(data["chosen"]), "decisions": total_decisions,
                          "rollout_sha256": [res.get("rollout_sha256") for _, res, _ in passed],
                          "params_sha256": sha, "optimizer_sha256": state["optimizer_sha256"],
                          "collect_seconds": collected - started, "update_seconds": updated - mb_start,
                          "wall_seconds": time.perf_counter() - started,
                          "updates": sum(h["updates"] for h in history), "history": history}
                log.write(json.dumps(record) + "\n")
                if not args.keep_rollouts:
                    for _, _, job in passed:
                        Path(job["rollout"]).unlink(missing_ok=True)
                last = history[-1]
                print(f"round {round_index + 1}/{config.rounds} duels {state['duels']} "
                      f"{ {k: f'{v[0]}/{v[2]}' for k, v in wins.items()} } decisions {total_decisions} "
                      f"collect {collected - started:.1f}s update {updated - mb_start:.1f}s ({record['updates']}u) "
                      f"H {last['entropy']:.3f} kl {last['approx_kl']:.4f} vmse {last['value_mse']:.3f} void {void_games}",
                      flush=True)
    finally:
        if pool:
            pool.close()


if __name__ == "__main__":
    main()
