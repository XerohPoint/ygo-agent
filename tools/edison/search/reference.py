#!/usr/bin/env python3
"""Compute what net_diff.cpp must reproduce, with the actual Python functions.

expect: for each record, audit, normalize_history_turn and pack from obs.py, in actor.py's order.
net: for each record and one checkpoint, EdisonNet through pad_batch, jitted as NetPolicy does, for
the full path and value_only. Every Nth record also stores the Encoder's state and rows.
"""
import argparse
import json
from pathlib import Path
import sys

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "learner"))
sys.path.insert(0, str(HERE))
import decks as deck_registry  # noqa: E402
from dump_format import RECORD, python_pipeline, write_expect  # noqa: E402

N_CODES = len(deck_registry.load_codes()[1])
# Code list v2: the network reads identities through this map, after pack. The expect file holds pack's
# output before the map, which is what the native pack must reproduce; the native pass maps at lookup.
ROW_OF = deck_registry.load_identity()[1] if deck_registry.CODE_LIST == "v2" else None


def expect(args):
    records = np.memmap(args.decisions, dtype=RECORD, mode="r")
    tally = {}
    with args.output.open("wb") as out:
        for record in records:
            verdict, packed = python_pipeline(record, N_CODES)
            tally[verdict] = tally.get(verdict, 0) + 1
            write_expect(out, verdict, packed)
    print(json.dumps({"records": len(records), "verdicts": {str(k): v for k, v in sorted(tally.items())}}))


def apply_functions(config):
    """The jitted full, value-only and captured paths. Full matches NetPolicy's lambda exactly."""
    import jax
    from net import EdisonNet, Encoder
    model = EdisonNet(config)
    full = jax.jit(lambda p, b: model.apply({"params": p}, b))
    value_only = jax.jit(lambda p, b: model.apply({"params": p}, b, value_only=True))

    def capture_encoder(p, b):
        _, state = model.apply({"params": p}, b, capture_intermediates=lambda module, name: isinstance(module, Encoder),
                               mutable=["intermediates"])
        return state["intermediates"]["Encoder_0"]["__call__"][0]
    return full, value_only, jax.jit(capture_encoder)


def net(args):
    import jax
    from actor import load_checkpoint
    from obs import pad_batch, remap_ids, segment_distribution
    config, params, meta, sha = load_checkpoint(args.checkpoint)
    params = jax.tree_util.tree_map(lambda x: jax.numpy.asarray(x), params)
    full, value_only, capture = apply_functions(config)
    records = np.memmap(args.decisions, dtype=RECORD, mode="r")
    if args.limit:
        records = records[:args.limit]
    written = 0
    with args.output.open("wb") as out:
        for index, record in enumerate(records):
            verdict, packed = python_pipeline(record, config.n_codes)
            if verdict:
                raise ValueError(f"Record {index} fails the Python pipeline with rule {verdict}")
            if ROW_OF is not None:
                remap_ids(packed, ROW_OF[None])
            batch = pad_batch(packed)
            logits, value = full(params, batch)
            n = int(packed["offsets"][-1])
            logits32 = np.asarray(logits, dtype=np.float32)[:n]
            value32 = np.asarray(value, dtype=np.float32)[0]
            only32 = np.asarray(value_only(params, batch), dtype=np.float32)[0]
            # actor.py converts to float64 and takes argmax of segment_distribution.
            logp, _ = segment_distribution(logits32.astype(np.float64), packed["offsets"])
            best = int(np.argmax(logp))
            header = np.array([n, best, 1 if index % args.rows_every == 0 else 0], dtype="<i4")
            header.tofile(out)
            np.array([value32, only32], dtype="<f4").tofile(out)
            logits32.astype("<f4").tofile(out)
            if header[2]:
                state, rows = capture(params, batch)
                n_rows = int(packed["n_rows"][0])
                np.asarray(state, dtype="<f4")[0].tofile(out)
                np.asarray(rows, dtype="<f4")[0, :n_rows + 1].tofile(out)
            written += 1
            if written % 5000 == 0:
                print(written, flush=True)
    print(json.dumps({"records": written, "params_sha256": sha, "rows_every": args.rows_every}))


def latency(args):
    """The JAX side of the benchmark: jitted, batch 1, on the same shuffled mix, for comparison."""
    import time
    import jax
    from actor import load_checkpoint
    from obs import pad_batch
    config, params, _, _ = load_checkpoint(args.checkpoint)
    params = jax.tree_util.tree_map(lambda x: jax.numpy.asarray(x), params)
    full, value_only, _ = apply_functions(config)
    records = np.memmap(args.decisions, dtype=RECORD, mode="r")
    order = np.random.default_rng(20261105).permutation(len(records))[:args.samples]
    batches = []
    for index in order:
        _, packed = python_pipeline(records[int(index)], config.n_codes)
        batches.append(pad_batch(packed))
    # Compile every shape first, so the timings below hold no compilation.
    for batch in batches:
        jax.block_until_ready(value_only(params, batch))
        jax.block_until_ready(full(params, batch))
    report = {}
    for name, fn in (("value_only", value_only), ("full", full)):
        times = []
        for batch in batches:
            t0 = time.perf_counter()
            jax.block_until_ready(fn(params, batch))
            times.append((time.perf_counter() - t0) * 1e6)
        report[name] = {q: round(float(np.percentile(times, float(q))), 1) for q in ("50", "90", "99")}
    print(json.dumps({"samples": len(batches), "jax_inline_us": report}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="mode", required=True)
    c = sub.add_parser("latency")
    c.add_argument("--decisions", type=Path, required=True)
    c.add_argument("--checkpoint", required=True)
    c.add_argument("--samples", type=int, default=3000)
    c.set_defaults(run=latency)
    a = sub.add_parser("expect")
    a.add_argument("--decisions", type=Path, required=True)
    a.add_argument("--output", type=Path, required=True)
    a.set_defaults(run=expect)
    b = sub.add_parser("net")
    b.add_argument("--decisions", type=Path, required=True)
    b.add_argument("--checkpoint", required=True)
    b.add_argument("--output", type=Path, required=True)
    b.add_argument("--rows-every", type=int, default=8)
    b.add_argument("--limit", type=int, default=0)
    b.set_defaults(run=net)
    args = parser.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
