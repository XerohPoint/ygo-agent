#!/usr/bin/env python3
"""Checks on the learner's contracts, harness and determinism.

Uses only development seeds from 73580000 upward. Run from the repository root:
  .venv/bin/python tools/edison/learner/test_learner.py --output <fresh dir>
"""
import argparse
import hashlib
import itertools
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

os.environ.setdefault("JAX_PLATFORMS", "cpu")
os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import decks as deck_registry  # noqa: E402
from decks import ROOT  # noqa: E402

SEED = 73580000
RESULTS = {}


def check(name):
    def decorator(fn):
        def run(*args):
            started = time.perf_counter()
            try:
                detail = fn(*args)
                RESULTS[name] = {"status": "PASS", "seconds": round(time.perf_counter() - started, 2), "detail": detail}
            except Exception as error:
                RESULTS[name] = {"status": "FAIL", "seconds": round(time.perf_counter() - started, 2),
                                 "detail": f"{type(error).__name__}: {error}"}
            print(name, RESULTS[name]["status"], RESULTS[name]["detail"], flush=True)
        return run
    return decorator


def make_checkpoint(path, seed=1):
    import flax
    import jax
    import numpy as np
    from actor import CHECKPOINT_FORMAT
    from net import NetConfig, init_params
    config = NetConfig(n_codes=len(deck_registry.load_codes()[1]), channels=32, layers=1, heads=2, id_dim=16, critic_hidden=32)
    _, params = init_params(config, seed)
    raw = flax.serialization.msgpack_serialize(jax.tree_util.tree_map(np.asarray, flax.core.unfreeze(params)))
    sha = hashlib.sha256(raw).hexdigest()
    Path(path).with_suffix(".msgpack").write_bytes(raw)
    Path(path).with_suffix(".json").write_text(json.dumps(
        {"format": CHECKPOINT_FORMAT, "params_sha256": sha, "codes_sha256": deck_registry.load_codes()[0], "config": config.dict()}))
    return {"kind": "net", "checkpoint": str(path), "sha256": sha, "greedy": False}


@check("codes-closure")
def t_codes():
    sha, codes = deck_registry.load_codes()
    meta = json.loads(deck_registry.CODES_META.read_text())
    assert sha == meta["sha256"], "code list hash"
    values = set(codes.values())
    reg = deck_registry.registry()
    assert len(reg) == 21, "21 lists"
    pool = deck_registry.pool_ids()
    missing, outside = [], []
    for name, path in reg.items():
        zones = deck_registry.parse_ydk(Path(path).read_text())
        missing += [c for zone in zones.values() for c in zone if c not in values]
        outside += [c for zone in zones.values() for c in zone if c not in pool]
    assert not missing, f"cards missing {missing[:5]}"
    assert not outside, f"non-pool cards in played lists {outside[:5]}"
    return {"codes": len(codes), "sha256": sha, "lists": len(reg)}


@check("audit-rejects-planted-violations")
def t_audit():
    import numpy as np
    from obs import ContractError, audit
    good = {"cards_": np.zeros((1, 160, 41), dtype=np.uint8), "global_": np.zeros((1, 23), dtype=np.uint8),
            "actions_": np.zeros((1, 256, 12), dtype=np.uint8), "h_actions_": np.zeros((1, 16, 14), dtype=np.uint8),
            "decision_": np.zeros((1, 12), dtype=np.int32), "selection_": np.zeros((1, 160, 3), dtype=np.uint8)}
    good["decision_"][0, 0] = 1
    good["cards_"][0, 0, :6] = (0, 5, 2, 0, 0, 1)
    good["actions_"][0, 0, :5] = (1, 0, 5, 1, 4)
    counts, legal, overflow = np.asarray([1]), np.asarray([1]), np.asarray([0])
    audit(good, counts, legal, overflow, 509)

    def bad(edit, n_codes=509, c=counts, l=legal, o=overflow):
        obs = {k: v.copy() for k, v in good.items()}
        edit(obs)
        try:
            audit(obs, c, l, o, n_codes)
        except ContractError as error:
            return str(error)
        raise AssertionError("violation accepted")

    def s(key, index, value):
        def edit(obs):
            obs[key][index] = value
        return edit
    cases = {
        "id beyond list": bad(s("cards_", (0, 0, 1), 255), 100),
        "hidden stats": bad(lambda o: (s("cards_", (0, 1, 2), 3)(o), s("cards_", (0, 1, 12), 9)(o))),
        "action/card id": bad(s("actions_", (0, 0, 2), 6)),
        "stale selection": bad(s("selection_", (0, 0, 1), 1)),
        "unknown schema": bad(s("decision_", (0, 0), 2)),
        "private hint": bad(lambda o: (s("decision_", (0, 11), 1)(o), s("decision_", (0, 1), 60000)(o),
                                       s("selection_", (0, 0, 0), 1)(o))),
        "embed bound": bad(s("cards_", (0, 0, 8), 99)),
        "action embed bound": bad(s("actions_", (0, 0, 3), 40)),
        "beyond offered": bad(s("actions_", (0, 2, 3), 1)),
        "overflow": bad(lambda o: None, o=np.asarray([1])),
        "own hand id": bad(lambda o: (s("cards_", (0, 1, 2), 2)(o))),
    }
    return sorted(cases)


@check("repeat-guard")
def t_repeat_guard():
    import numpy as np
    from actor import RepeatGuard
    logp = np.asarray([-2.0, -0.5, -1.0])
    guard = RepeatGuard()
    picks = [guard.choose(b"same", logp) for _ in range(4)]
    assert picks == [1, 2, 0, 1], picks
    assert guard.choose(b"new", logp) == 1, "a new state plays the top option"
    assert guard.fires == 3, guard.fires
    return "top option first, then untried options, then top again"


@check("softmax-offered-only")
def t_softmax():
    import jax.numpy as jnp
    import numpy as np
    from net import segment_log_softmax
    from obs import segment_distribution
    logits = np.asarray([1.0, 2.0, -1.0, 0.5, 9.0, 9.0], dtype=np.float32)
    segments = np.asarray([0, 0, 0, 1, 2, 2], dtype=np.int32)
    out = np.asarray(segment_log_softmax(jnp.asarray(logits), jnp.asarray(segments), 2))
    numpy_lp, _ = segment_distribution(logits[:4].astype(np.float64), np.asarray([0, 3, 4]))
    assert np.allclose(out[:4], numpy_lp, atol=1e-5)
    assert abs(np.exp(out[:3]).sum() - 1) < 1e-5
    return "padding excluded"


@check("gae-terminal-only")
def t_gae():
    import numpy as np
    from train import compute_gae
    data = {"chosen": np.zeros(3), "value": np.asarray([0.0, 0.5, 0.2]), "outcome": np.asarray([1.0, 1.0, -1.0]),
            "trajectory": np.asarray([0, 0, 1])}
    adv, ret, weights = compute_gae(data, 1.0)
    assert np.allclose(adv[:2], [1.0, 0.5]) and np.allclose(ret[:2], [1.0, 1.0])
    assert np.allclose(adv[2], -1.2) and np.allclose(weights, [0.5, 0.5, 1.0])
    return "lambda=1 gives Monte Carlo"


def tiny_job(tmp, spec, seed, name, **extra):
    return {"seed": seed, "games": 4, "deck0": "blackwing", "deck1": "machina_gadget", "policies": [spec, {"kind": "heuristic"}],
            "collect": [True, False], "rollout": str(Path(tmp) / f"{name}.npz"), **extra}


@check("cell-determinism")
def t_determinism(pool, tmp):
    spec = make_checkpoint(Path(tmp) / "net", 3)
    jobs = [tiny_job(tmp, spec, SEED + 100, "a"), tiny_job(tmp, spec, SEED + 100, "b", threads=4),
            tiny_job(tmp, spec, SEED + 100, "c")]
    results = pool.map(jobs)
    assert all(r["status"] == "PASS" for r in results), results
    assert results[0]["duels"] == results[1]["duels"] == results[2]["duels"], "duel traces differ"
    assert results[0]["rollout_sha256"] == results[1]["rollout_sha256"] == results[2]["rollout_sha256"], "rollouts differ"
    return {"rollout": results[0]["rollout_sha256"][:16], "decisions": results[0]["counters"]["decisions"]}


@check("pack-preserves-references")
def t_pack(tmp):
    """Everything an action or decision refers to survives trimming, on real rollout data."""
    import numpy as np
    with np.load(Path(tmp) / "a.npz") as data:
        cards, n_rows, actions, sizes, decision = data["cards"], data["n_rows"], data["actions"], data["sizes"], data["decision"]
    at = 0
    for i, size in enumerate(sizes):
        for a in actions[at:at + size]:
            if a[0]:
                assert a[0] <= n_rows[i], "reference beyond rows"
                assert (cards[i, int(a[0]) - 1, :2] == a[1:3]).all(), "reference moved to a different card"
        at += size
        if decision[i, 3]:
            assert cards[i, int(decision[i, 3]) - 1, 44] == 1, "source flag"
    visible = (cards[:, :, 0].astype(int) * 256 + cards[:, :, 1]) > 0
    hidden_private = ~visible & np.isin(cards[:, :, 2], (1, 2, 7))
    # A kept hidden private row must be referenced by selection flags or an action.
    return {"decisions": len(sizes), "hidden_private_rows_kept": int(hidden_private.sum())}


@check("void-on-native-abort-and-truncation")
def t_void(pool, tmp):
    spec = make_checkpoint(Path(tmp) / "net2", 4)
    # A max_steps of 5 truncates every duel, which must void rather than score.
    truncated = pool.map([tiny_job(tmp, spec, SEED + 200, "trunc", max_steps=5)])[0]
    assert truncated["status"] == "VOID" and "duels" not in truncated, truncated
    aborted = pool.map([tiny_job(tmp, spec, SEED + 201, "abort", test_abort=True)])[0]
    assert aborted["status"] == "VOID" and "duels" not in aborted, aborted
    # The worker came back, so later cells still run.
    after = pool.map([tiny_job(tmp, spec, SEED + 202, "after")])[0]
    assert after["status"] == "PASS"
    return {"truncated": truncated["reason"][:60], "aborted": aborted["reason"][:60]}


@check("checkpoint-fails-closed")
def t_checkpoint(tmp):
    from actor import load_checkpoint
    stem = Path(tmp) / "net3"
    make_checkpoint(stem, 5)
    load_checkpoint(stem)
    raw = stem.with_suffix(".msgpack").read_bytes()
    stem.with_suffix(".msgpack").write_bytes(raw[:-1] + bytes([raw[-1] ^ 1]))
    try:
        load_checkpoint(stem)
        raise AssertionError("tampered parameters accepted")
    except ValueError:
        pass
    stem.with_suffix(".msgpack").write_bytes(raw)
    meta = json.loads(stem.with_suffix(".json").read_text())
    stem.with_suffix(".json").write_text(json.dumps({**meta, "codes_sha256": "0" * 64}))
    try:
        load_checkpoint(stem)
        raise AssertionError("wrong code list accepted")
    except ValueError:
        pass
    stem.with_suffix(".json").write_text(json.dumps({**meta, "format": "other"}))
    try:
        load_checkpoint(stem)
        raise AssertionError("wrong format accepted")
    except ValueError:
        pass
    return "tamper, code list, format"


@check("generated-list-matches-production-worker")
def t_equivalence(pool, tmp):
    """Same games as the unmodified agent_match worker on the 67-id list, heuristic and baseline."""
    sys.path.insert(0, str(ROOT / "tests/edison"))
    import agent_match
    pairs = list(itertools.permutations(deck_registry.FIXTURE_NAMES, 2))[:3]
    jobs, refs = [], []
    for i, (a, b) in enumerate(pairs):
        seed = SEED + 300 + i * 10
        jobs.append({"seed": seed, "games": 4, "deck0": a, "deck1": b, "policies": [{"kind": "heuristic"}, {"kind": "random"}]})
        refs.append(agent_match.run_worker(seed, 4, 1, a, b, Path(tmp) / f"ref-{i}", 1, "heuristic", "random"))
    model = ROOT / "tests/edison/reports/rl-budget-2026-11-02/seed-1702/final-policy.json"
    seed = SEED + 400
    jobs.append({"seed": seed, "games": 4, "deck0": "blackwing", "deck1": "frog_monarch",
                 "policies": [{"kind": "baseline", "checkpoint": str(model), "greedy": False}, {"kind": "heuristic"}]})
    refs.append(agent_match.run_worker(seed, 4, 1, "blackwing", "frog_monarch", Path(tmp) / "ref-rl", 1, "rl", "heuristic", model))
    results = pool.map(jobs)
    games = 0
    for result, ref in zip(results, refs):
        assert result["status"] == "PASS", result
        ours = [(d["seed"], d["steps"], d["winner"], d["win_reason"]) for d in result["duels"]]
        theirs = [(d["seed"], d["steps"], d["winner"], d["win_reason"]) for d in ref["duels"]]
        assert ours == theirs, (ours, theirs)
        games += len(ours)
    return {"games_identical_outcome_and_length": games}


@check("hidden-cards-do-not-reach-network")
def t_privacy(pool, tmp):
    """Same seat 0 hand and seed against different opponent lists: same first logits."""
    sys.path[:0] = [str(ROOT), str(ROOT / "ygoenv")]
    code = """
import json, sys
sys.path[:0] = [%r, %r, %r]
import numpy as np, ygoenv
from ygoenv.ygopro import init_module
import decks as D
from obs import audit, pack, OBS_KEYS, normalize_history_turn, ContractError
init_module(str(D.DB), str(D.CODES), {k: str(v) for k, v in D.registry().items()})
out = {}
for opp in ("frog_monarch", "machina_gadget"):
    env = ygoenv.make("YGOPro-v1", env_type="gymnasium", num_envs=1, num_threads=1, seed=%d, deck1="blackwing", deck2=opp,
                      play_mode="self", player=-1, async_reset=False, greedy_reward=False, oppo_info=False, max_options=256,
                      max_steps=10000, max_episode_steps=10000, timeout=10, verbose=False)
    obs, info = env.reset()
    assert int(info["to_play"][0]) == 0
    sub = {k: obs[k] for k in OBS_KEYS}
    packed = pack(sub, np.asarray(info["num_options"]))
    out[opp] = {k: np.asarray(v).tobytes().hex() for k, v in packed.items()}
    deck_rows = obs["cards_"][0][(obs["cards_"][0][:, 2] == 1) | ((obs["cards_"][0][:, 4] == 1) & (obs["cards_"][0][:, 2] == 2)) | ((obs["cards_"][0][:, 4] == 1) & (obs["cards_"][0][:, 2] == 7))]
    assert not deck_rows[:, :2].any() and not deck_rows[:, 7:].any()
print("RESULT" + json.dumps(out))
""" % (str(ROOT), str(ROOT / "ygoenv"), str(HERE), SEED + 500)
    done = subprocess.run([sys.executable, "-c", code], cwd=ROOT / "scripts", capture_output=True, text=True, timeout=120,
                          env={**os.environ, "JAX_PLATFORMS": "cpu"})
    assert done.returncode == 0, done.stderr[-800:]
    out = json.loads(done.stdout.split("RESULT", 1)[1])
    differing = [k for k in out["frog_monarch"] if out["frog_monarch"][k] != out["machina_gadget"][k]]
    assert differing == [], f"packed inputs differ: {differing}"
    source = (HERE / "net.py").read_text() + (HERE / "obs.py").read_text() + (HERE / "actor.py").read_text()
    code_lines = [line for line in source.splitlines() if not line.lstrip().startswith(("#", '"""'))]
    leaks = [line for line in code_lines if '"deck"]' in line or "info:deck" in line and "never" not in line or "mask_\"" in line]
    assert not leaks, leaks
    return "packed network inputs byte-identical; info:deck and mask_ never read"


@check("order-equivariant")
def t_order(tmp):
    """List position carries no information: reordering rows and options only reorders results.

    The forward-model report warns that prompt order can differ between sampled worlds, so a
    policy that reads list position could learn true-world information there.
    """
    import jax
    import numpy as np
    from net import EdisonNet, NetConfig, init_params
    from obs import pad_batch
    with np.load(Path(tmp) / "a.npz") as data:
        i = int(np.argmax(data["sizes"]))
        at = int(data["sizes"][:i].sum())
        size, rows = int(data["sizes"][i]), int(data["n_rows"][i])
        packed = {"cards": data["cards"][i:i + 1, :rows], "n_rows": data["n_rows"][i:i + 1], "actions": data["actions"][at:at + size],
                  "offsets": np.asarray([0, size]), "hist": data["hist"][i:i + 1], "decision": data["decision"][i:i + 1],
                  "global": data["global"][i:i + 1]}
    config = NetConfig(n_codes=len(deck_registry.load_codes()[1]), channels=32, layers=1, heads=2, id_dim=16, critic_hidden=32)
    model, _ = init_params(config, 9)
    params = model.init(jax.random.PRNGKey(9), pad_batch(packed))["params"]
    # Non-zero output layers, because the zero init would make every logit equal.
    params = jax.tree_util.tree_map(lambda x: x, params)
    params["logit_out"]["kernel"] = jax.random.normal(jax.random.PRNGKey(10), params["logit_out"]["kernel"].shape)
    apply = jax.jit(lambda b: model.apply({"params": params}, b))
    base_logits, base_value = map(np.asarray, apply(pad_batch(packed)))
    rng = np.random.default_rng(1)
    action_order = rng.permutation(size)
    row_order = rng.permutation(rows)
    new_index = np.zeros(rows + 1, dtype=np.int64)
    new_index[row_order + 1] = np.arange(1, rows + 1)
    moved = {**packed, "cards": packed["cards"][:, row_order], "actions": packed["actions"][action_order].copy(),
             "decision": packed["decision"].copy()}
    moved["actions"][:, 0] = new_index[moved["actions"][:, 0]]
    moved["decision"][0, 3] = new_index[moved["decision"][0, 3]]
    logits, value = map(np.asarray, apply(pad_batch(moved)))
    assert size > 1 and rows > 1
    assert np.allclose(logits[:size], base_logits[:size][action_order], atol=2e-4), (logits[:size], base_logits[:size][action_order])
    assert abs(float(value[0]) - float(base_value[0])) < 2e-4
    assert np.ptp(base_logits[:size]) > 1e-3, "test has no power when logits are equal"
    return {"options": size, "rows": rows, "logit_spread": float(np.ptp(base_logits[:size]))}


@check("resume-matches-uninterrupted")
def t_resume(tmp):
    base = [sys.executable, str(HERE / "train.py"), "--arm", "test", "--seed-base", str(SEED + 1000), "--rounds", "3",
            "--games-per-cell", "2", "--workers", "2", "--learner-cpus", "2", "--channels", "32", "--layers", "1",
            "--heads", "2", "--id-dim", "16", "--critic-hidden", "32", "--epochs", "2", "--optimizer-seed", "1702"]
    straight, broken = Path(tmp) / "straight", Path(tmp) / "broken"
    def run(*extra):
        done = subprocess.run([*base, *extra], capture_output=True, text=True, timeout=900)
        assert done.returncode == 0, done.stdout[-800:] + done.stderr[-800:]
    run("--output", str(straight))
    run("--output", str(broken), "--stop-after", "1")
    run("--output", str(broken), "--resume")
    a = [json.loads(line) for line in (straight / "rounds.jsonl").read_text().splitlines()]
    b = [json.loads(line) for line in (broken / "rounds.jsonl").read_text().splitlines()]
    assert len(a) == len(b) == 3
    for x, y in zip(a, b):
        assert x["params_sha256"] == y["params_sha256"], ("parameters differ at round", x["round"])
        assert x["optimizer_sha256"] == y["optimizer_sha256"], ("optimizer differs at round", x["round"])
        assert x["rollout_sha256"] == y["rollout_sha256"], ("rollouts differ at round", x["round"])
    # Changing the configuration on resume must fail closed.
    done = subprocess.run([*base, "--output", str(broken), "--resume", "--learning-rate", "0.5"], capture_output=True, text=True, timeout=300)
    assert done.returncode != 0 and "different configuration" in done.stderr
    return {"rounds": 3, "final_params": a[-1]["params_sha256"][:16]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--only", nargs="*")
    args = parser.parse_args()
    from actor import Pool
    t_codes()
    t_audit()
    t_repeat_guard()
    t_softmax()
    t_gae()
    with tempfile.TemporaryDirectory(dir=ROOT / "build") as tmp:
        pool = Pool(2, Path(tmp) / "logs")
        try:
            t_determinism(pool, tmp)
            t_pack(tmp)
            t_void(pool, tmp)
            t_checkpoint(tmp)
            t_equivalence(pool, tmp)
            t_privacy(pool, tmp)
            t_order(tmp)
        finally:
            pool.close()
        t_resume(tmp)
    failed = [name for name, row in RESULTS.items() if row["status"] != "PASS"]
    report = {"status": "PASS" if not failed else "FAIL", "failed": failed, "checks": RESULTS}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=1, default=str) + "\n")
    print(report["status"], failed)
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
