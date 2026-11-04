#!/usr/bin/env python3
"""CPU speed measurements for the learner, on real rollout data.

Modes: collect-sample, env, infer, update, upstream. Thread counts are explicit. Absolute
numbers are for a machine shared with other workstreams; report load average beside them.
"""
import argparse
import json
import os
from pathlib import Path
import sys
import time

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import decks as deck_registry  # noqa: E402
from decks import ROOT  # noqa: E402

BATCHES = (1, 4, 16, 64, 256)


def physical_cpus(count):
    """One logical CPU per physical core, up to count, inside the current affinity.

    Workers are not pinned. This keeps a learner off SMT siblings and limits its thread pool.
    """
    available, seen, chosen = sorted(os.sched_getaffinity(0)), set(), []
    for cpu in available:
        path = Path(f"/sys/devices/system/cpu/cpu{cpu}/topology/core_id")
        core = path.read_text().strip() if path.exists() else cpu
        if core not in seen:
            seen.add(core)
            chosen.append(cpu)
    for cpu in available:
        if cpu not in chosen:
            chosen.append(cpu)
    return chosen[:count]


def load_sample(path):
    import numpy as np
    from train import load_rollouts
    return load_rollouts(sorted(Path(path).glob("*.npz")))


def packed_subset(data, selection):
    import numpy as np
    sizes = data["sizes"][selection]
    starts = data["offsets"][selection]
    total = int(sizes.sum())
    index = np.repeat(starts - np.r_[0, np.cumsum(sizes)[:-1]], sizes) + np.arange(total)
    width = int(data["n_rows"][selection].max())
    return {"cards": data["cards"][selection, :width], "n_rows": data["n_rows"][selection],
            "actions": data["actions"][index], "offsets": np.r_[0, np.cumsum(sizes)].astype(np.int64),
            "hist": data["hist"][selection], "decision": data["decision"][selection], "global": data["global"][selection]}


def bench_infer(args):
    import jax
    import numpy as np
    from net import EdisonNet, NetConfig, param_count
    from obs import pad_batch
    data = load_sample(args.sample)
    rows = []
    for spec in args.nets:
        channels, layers, heads, id_dim = map(int, spec.split("x"))
        config = NetConfig(n_codes=len(deck_registry.load_codes()[1]), channels=channels, layers=layers, heads=heads, id_dim=id_dim)
        model = EdisonNet(config)
        rng = np.random.default_rng(1)
        batch0 = pad_batch(packed_subset(data, np.arange(4)))
        params = model.init(jax.random.PRNGKey(1), batch0)["params"]
        apply_full = jax.jit(lambda p, b: model.apply({"params": p}, b))
        apply_value = jax.jit(lambda p, b: model.apply({"params": p}, b, value_only=True))
        for batch_size in BATCHES:
            batches = [pad_batch(packed_subset(data, rng.integers(0, len(data["chosen"]), batch_size))) for _ in range(40)]
            groups = {}
            for b in batches:
                groups.setdefault((b["cards"].shape, b["actions"].shape), []).append(b)
            t0 = time.perf_counter()
            for group in groups.values():
                jax.block_until_ready(apply_full(params, group[0]))
                jax.block_until_ready(apply_value(params, group[0]))
            compile_seconds = time.perf_counter() - t0
            full, value = [], []
            for repeat in range(3):
                for b in batches:
                    t0 = time.perf_counter()
                    jax.block_until_ready(apply_full(params, b))
                    full.append(time.perf_counter() - t0)
                    t0 = time.perf_counter()
                    jax.block_until_ready(apply_value(params, b))
                    value.append(time.perf_counter() - t0)
            row = {"net": spec, "params": param_count(params), "batch": batch_size,
                   "shapes": sorted({f"{s[0][1]}r/{s[1][0]}a" for s in groups}),
                   "full_ms_p50": 1000 * float(np.median(full)), "full_ms_p90": 1000 * float(np.percentile(full, 90)),
                   "value_ms_p50": 1000 * float(np.median(value)), "value_ms_p90": 1000 * float(np.percentile(value, 90)),
                   "per_decision_us_full": 1e6 * float(np.median(full)) / batch_size,
                   "leaf_evals_per_second": batch_size / float(np.median(value)),
                   "compile_seconds": compile_seconds}
            print(json.dumps(row), flush=True)
            rows.append(row)
    return rows


def bench_update(args):
    import jax
    import numpy as np
    from net import EdisonNet, NetConfig
    from train import TrainConfig, compute_gae, make_minibatch, make_optimizer, make_update, order_epoch
    data = load_sample(args.sample)
    advantages, returns, weights = compute_gae(data, 0.95)
    data.update(advantage=(advantages - advantages.mean()) / advantages.std(), weight=weights)
    data["return"] = returns
    rows = []
    for spec in args.nets:
        channels, layers, heads, id_dim = map(int, spec.split("x"))
        for minibatch in args.minibatches:
            train = TrainConfig(channels=channels, layers=layers, heads=heads, id_dim=id_dim, minibatch=minibatch)
            config = NetConfig(n_codes=len(deck_registry.load_codes()[1]), channels=channels, layers=layers, heads=heads, id_dim=id_dim)
            model = EdisonNet(config)
            optimizer = make_optimizer(train)
            step = make_update(model, optimizer, train)
            rng = np.random.default_rng(1)
            order = order_epoch(data["n_rows"], minibatch, rng, train.row_grouping)[:args.updates]
            sample = [make_minibatch(data, sel, minibatch) for sel in order]
            params = model.init(jax.random.PRNGKey(1), {k: v for k, v in sample[0].items()})["params"]
            opt_state = optimizer.init(params)
            t0 = time.perf_counter()
            seen = set()
            for mb in sample:
                key = (mb["cards"].shape, mb["actions"].shape)
                if key not in seen:
                    seen.add(key)
                    jax.block_until_ready(step(params, opt_state, mb)[2])
            compile_seconds = time.perf_counter() - t0
            t0 = time.perf_counter()
            for mb in sample:
                params, opt_state, stats = step(params, opt_state, mb)
            jax.block_until_ready(stats)
            train_seconds = time.perf_counter() - t0
            t0 = time.perf_counter()
            for sel in order:
                make_minibatch(data, sel, minibatch)
            pack_seconds = time.perf_counter() - t0
            row = {"net": spec, "minibatch": minibatch, "updates": len(sample), "updates_per_second": len(sample) / train_seconds,
                   "assembly_ms_per_update": 1000 * pack_seconds / len(sample), "compile_seconds": compile_seconds,
                   "shapes": sorted(f"{s[0][1]}r/{s[1][0]}a" for s in seen), "cpus": len(os.sched_getaffinity(0)),
                   "row_grouping": train.row_grouping}
            print(json.dumps(row), flush=True)
            rows.append(row)
    return rows


def bench_obsproc(args):
    """Python cost around the network: audit and pack on full 160-row observations."""
    import numpy as np
    sys.path[:0] = [str(ROOT), str(ROOT / "ygoenv")]
    os.chdir(ROOT / "scripts")
    import ygoenv
    from ygoenv.ygopro import init_module
    from obs import OBS_KEYS, audit, normalize_history_turn, pack, pad_batch
    reg = deck_registry.registry()
    init_module(str(deck_registry.DB), str(deck_registry.CODES), {k: str(v) for k, v in reg.items()})
    rows = []
    for batch in (1, 4, 16, 64, 256):
        env = ygoenv.make("YGOPro-v1", env_type="gymnasium", num_envs=batch, num_threads=1, seed=73890100,
                          deck1="blackwing", deck2="frog_monarch", play_mode="self", player=-1, async_reset=False,
                          greedy_reward=False, oppo_info=False, max_options=256, max_steps=10000,
                          max_episode_steps=10000, timeout=10, verbose=False)
        rng = np.random.default_rng(1)
        obs, info = env.reset()
        audits, packs, n = [], [], 0
        while n < 400:
            counts = np.asarray(info["num_options"])
            live = np.asarray(info["legal_options"]) > 0  # Finished duels report no legal options.
            sub = {key: obs[key][live] for key in OBS_KEYS}
            if n > 40 and live.all():  # Skip the opening, where boards are nearly empty.
                t0 = time.perf_counter()
                audit(sub, counts, np.asarray(info["legal_options"]), np.asarray(info["action_overflow"]), 509)
                t1 = time.perf_counter()
                sub["h_actions_"] = normalize_history_turn(sub["h_actions_"], sub["global_"])
                pad_batch(pack(sub, counts))
                t2 = time.perf_counter()
                audits.append(t1 - t0)
                packs.append(t2 - t1)
            actions = (rng.random(len(counts)) * counts).astype(np.int64)
            obs, _, _, _, info = env.step(actions, np.asarray(info["env_id"]))
            n += 1
        row = {"batch": batch, "audit_us_p50": 1e6 * float(np.median(audits)), "pack_pad_us_p50": 1e6 * float(np.median(packs)),
               "per_decision_us": 1e6 * (float(np.median(audits)) + float(np.median(packs))) / batch}
        print(json.dumps(row), flush=True)
        rows.append(row)
    return rows


def bench_env(args):
    """Native env with uniform random actions, with no audit and no network."""
    import numpy as np
    import ygoenv
    from ygoenv.ygopro import init_module
    reg = deck_registry.registry()
    init_module(str(deck_registry.DB), str(deck_registry.CODES), {k: str(v) for k, v in reg.items()})
    rows = []
    for envs, threads in ((1, 1), (8, 1), (64, 1), (64, 4), (64, 8)):
        env = ygoenv.make("YGOPro-v1", env_type="gymnasium", num_envs=envs, num_threads=threads, seed=73890000,
                          deck1="blackwing", deck2="frog_monarch", play_mode="self", player=-1, async_reset=False,
                          greedy_reward=False, oppo_info=False, max_options=256, max_steps=10000,
                          max_episode_steps=10000, timeout=10, verbose=False)
        rng = np.random.default_rng(1)
        obs, info = env.reset()
        steps, t0 = 0, time.perf_counter()
        while time.perf_counter() - t0 < args.seconds:
            counts = np.asarray(info["num_options"])
            actions = (rng.random(len(counts)) * counts).astype(np.int64)
            obs, rewards, terminated, truncated, info = env.step(actions, np.asarray(info["env_id"]))
            steps += len(counts)
        elapsed = time.perf_counter() - t0
        row = {"envs": envs, "threads": threads, "decisions_per_second": steps / elapsed}
        print(json.dumps(row), flush=True)
        rows.append(row)
    return rows


def bench_upstream(args):
    """Forward cost of the unmodified upstream RNNAgent on real Edison observations."""
    import jax
    import numpy as np
    sys.path.insert(0, str(ROOT))
    from ygoai.rl.jax.agent import RNNAgent
    from net import param_count
    data = load_sample(args.sample)
    n_codes = len(deck_registry.load_codes()[1])
    rows = []
    for name, kwargs in (("upstream-default", {}), ("upstream-id64", {"embedding_shape": (n_codes, 64)})):
        kw = {"num_layers": 2, "num_channels": 128, "rnn_channels": 512, "rnn_type": "lstm", "film": True, "noam": True,
              "embedding_shape": (n_codes, 1024), **kwargs}
        model = RNNAgent(**kw)
        rng = np.random.default_rng(1)
        for batch_size in BATCHES:
            selection = rng.integers(0, len(data["chosen"]), batch_size)
            # Rebuild the unpacked shapes upstream expects: 160 rows and max_options 256.
            cards = np.zeros((batch_size, 160, 41), dtype=np.uint8)
            take = min(data["cards"].shape[1], 160)
            cards[:, :take] = data["cards"][selection, :take, :41]
            actions = np.zeros((batch_size, 256, 12), dtype=np.uint8)
            for out, index in enumerate(selection):
                start, size = data["offsets"][index], data["sizes"][index]
                actions[out, :size] = data["actions"][start:start + size]
            obs = {"cards_": cards, "global_": data["global"][selection], "actions_": actions,
                   "h_actions_": data["hist"][selection]}
            obs = {key: jax.numpy.asarray(value) for key, value in obs.items()}
            rstate = model.init_rnn_state(batch_size)
            t0 = time.perf_counter()
            variables = model.init(jax.random.PRNGKey(1), obs, rstate)
            apply = jax.jit(lambda v, o, r: model.apply(v, o, r)[1:3])
            jax.block_until_ready(apply(variables, obs, rstate))
            compile_seconds = time.perf_counter() - t0
            times = []
            for _ in range(10):
                t0 = time.perf_counter()
                jax.block_until_ready(apply(variables, obs, rstate))
                times.append(time.perf_counter() - t0)
            logits, value = apply(variables, obs, rstate)
            row = {"net": name, "params": param_count(variables["params"]), "batch": batch_size,
                   "full_ms_p50": 1000 * float(np.median(times)), "per_decision_us": 1e6 * float(np.median(times)) / batch_size,
                   "compile_seconds": compile_seconds, "finite": bool(np.isfinite(np.asarray(value)).all())}
            print(json.dumps(row), flush=True)
            rows.append(row)
            if time.perf_counter() - t0 > 30:
                break
    # Failure mode the audit found: an out-of-range card id through Flax Embed.
    from flax import linen as nn
    probe = nn.Embed(4, 2)
    params = probe.init(jax.random.PRNGKey(0), np.zeros(1, dtype=np.int32))
    out = np.asarray(probe.apply(params, np.asarray([0, 7], dtype=np.int32)))
    rows.append({"out_of_range_embed_is_nan": bool(np.isnan(out[1]).all()), "in_range_is_nan": bool(np.isnan(out[0]).any())})
    print(json.dumps(rows[-1]), flush=True)
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("env", "infer", "update", "upstream", "obsproc"))
    parser.add_argument("--sample", type=Path)
    parser.add_argument("--nets", nargs="*", default=["96x2x4x48"], help="channels x layers x heads x id_dim")
    parser.add_argument("--minibatches", nargs="*", type=int, default=[256])
    parser.add_argument("--updates", type=int, default=40)
    parser.add_argument("--seconds", type=float, default=8)
    parser.add_argument("--cpus", type=int, default=1)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.output:
        args.output = args.output.resolve()  # The env modes change directory to scripts/.
    os.environ["JAX_PLATFORMS"] = "cpu"
    os.environ["TF_CPP_MIN_LOG_LEVEL"] = "2"
    os.environ["OPENBLAS_NUM_THREADS"] = "1"
    if args.cpus == 1:
        os.environ["XLA_FLAGS"] = "--xla_cpu_multi_thread_eigen=false intra_op_parallelism_threads=1"
    os.sched_setaffinity(0, physical_cpus(args.cpus))
    if args.mode == "env":
        sys.path[:0] = [str(ROOT), str(ROOT / "ygoenv")]
        os.chdir(ROOT / "scripts")
    rows = {"env": bench_env, "infer": bench_infer, "update": bench_update, "upstream": bench_upstream, "obsproc": bench_obsproc}[args.mode](args)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps({"mode": args.mode, "cpus": args.cpus, "load": os.getloadavg(), "rows": rows}, indent=1) + "\n")


if __name__ == "__main__":
    main()
