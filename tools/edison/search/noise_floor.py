#!/usr/bin/env python3
"""How far are native and JAX float32 outputs from the same network in float64?

A large native-vs-JAX gap could be a bug or rounding. If the native result is as close to a
float64 run of EdisonNet as JAX's own float32 run is, the gap is rounding. Checks the decisions
with the largest native-vs-JAX logit gap, and a random sample as a baseline.
The thermometer's bin tables stay float32 in this run, as net.py builds them.
"""
import argparse
import json
from pathlib import Path
import sys

import jax

jax.config.update("jax_enable_x64", True)
import jax.numpy as jnp  # noqa: E402
import numpy as np  # noqa: E402

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "learner"))
sys.path.insert(0, str(HERE))
from dump_format import RECORD, python_pipeline, read_reference  # noqa: E402


def native_outputs(path, counts):
    with open(path, "rb") as f:
        for n in counts:
            value = np.frombuffer(f.read(4), dtype="<f4")[0]
            yield value, np.frombuffer(f.read(4 * int(n)), dtype="<f4")


def main():
    from actor import load_checkpoint
    from net import EdisonNet
    from obs import pad_batch
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--decisions", type=Path, required=True)
    parser.add_argument("--checkpoint", required=True)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--worst", type=int, default=200)
    parser.add_argument("--random", type=int, default=2000)
    args = parser.parse_args()
    config, params, _, _ = load_checkpoint(args.checkpoint)
    if config.unknown_row:
        raise SystemExit("noise_floor.py applies the network directly and would skip code list v2's identity map")
    params = jax.tree_util.tree_map(lambda x: jnp.asarray(x, dtype=jnp.float64), params)
    model = EdisonNet(config)
    full = jax.jit(lambda p, b: model.apply({"params": p}, b))
    records = np.memmap(args.decisions, dtype=RECORD, mode="r")
    packed = [None] * len(records)
    counts = records["counts"]
    n_rows = np.zeros(len(records), dtype=np.int32)
    for i, record in enumerate(records):
        _, p = python_pipeline(record, config.n_codes)
        n_rows[i] = p["n_rows"][0]
        packed[i] = p
    jax32, native, gaps = [], [], np.zeros(len(records))
    for i, ((value, _, _, logits), (nvalue, nlogits)) in enumerate(zip(read_reference(args.reference, config.channels, n_rows), native_outputs(args.native, counts))):
        jax32.append((value, logits))
        native.append((nvalue, nlogits))
        gaps[i] = np.abs(logits - nlogits).max()
    worst = np.argsort(-gaps)[:args.worst]
    sample = np.random.default_rng(20261105).choice(len(records), args.random, replace=False)

    def errors(indices):
        out = {"jax_value": [], "native_value": [], "jax_logit": [], "native_logit": [], "native_vs_jax_logit": [], "logit_size": []}
        for i in indices:
            logits64, value64 = full(params, pad_batch(packed[int(i)]))
            n = int(counts[i])
            logits64 = np.asarray(logits64)[:n]
            value64 = float(np.asarray(value64)[0])
            out["jax_value"].append(abs(float(jax32[i][0]) - value64))
            out["native_value"].append(abs(float(native[i][0]) - value64))
            out["jax_logit"].append(float(np.abs(jax32[i][1] - logits64).max()))
            out["native_logit"].append(float(np.abs(native[i][1] - logits64).max()))
            out["native_vs_jax_logit"].append(float(gaps[i]))
            out["logit_size"].append(float(np.abs(logits64).max()))
        return out

    def describe(values):
        values = np.asarray(values)
        return {"mean": float(values.mean()), "p50": float(np.percentile(values, 50)), "p99": float(np.percentile(values, 99)), "max": float(values.max())}

    report = {}
    for name, indices in (("worst_decisions", worst), ("random_decisions", sample)):
        data = errors(indices)
        report[name] = {key: describe(value) for key, value in data.items()}
        native_better = np.mean(np.array(data["native_logit"]) <= np.array(data["jax_logit"]))
        report[name]["native_at_least_as_close_fraction"] = float(native_better)
    print(json.dumps(report, indent=1))


if __name__ == "__main__":
    main()
