#!/usr/bin/env python3
"""Run the native-vs-JAX differential, the controls and the loader checks. Writes summary.json.

Steps are cached by their output files, so a rerun does only what is missing. Expects
collect_decisions.py shards in <out>/data and build.sh faults to have run.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "learner"))
from dump_format import RECORD  # noqa: E402
import export_net  # noqa: E402

ROOT = HERE.parents[2]
OUT = ROOT / "build/edison-native-net"
DATA = OUT / "data"
BIN = OUT / "bin"
RUNS = Path("/home/mada/ygo-agent/build/edison-retrain-2026-11-04/runs")
SEEDS = (1702, 1703, 1704)
THREADS = 4
os.environ.setdefault("JAX_PLATFORMS", "cpu")
ENV = {**os.environ, "JAX_PLATFORMS": "cpu", "OPENBLAS_NUM_THREADS": "1", "OMP_NUM_THREADS": "1", "TF_CPP_MIN_LOG_LEVEL": "2",
       "XLA_FLAGS": "--xla_cpu_multi_thread_eigen=false intra_op_parallelism_threads=1"}

# Gates, set from the correct build's measured rounding (about twice its worst case across the
# checkpoints and the extremes), so they were calibrated after the fact. The means are the sensitive ones: a small systematic
# fault moves a mean long before a max. Logits reach the thousands, so their gates are scaled by
# max(|logit|, 1); noise_floor.py shows the large absolute errors are rounding JAX shares.
GATES = {"value_error.max": 2e-5, "value_error.p99.9": 8e-6, "value_error.mean": 4e-7,
         "logit_error.max": 2.5e-3, "logit_error_scaled.max": 3e-4, "logit_error_scaled.p99.9": 5e-5, "logit_error_scaled.mean": 1.5e-6,
         "state_error.mean": 3.5e-6, "row_error.mean": 2.6e-7}
COUNT_GATES = ("verdict_mismatch", "hist_mismatch", "pack_size_mismatch", "pack_cards_mismatch", "pack_actions_mismatch",
               "pack_decision_mismatch", "pack_global_mismatch", "eval_fail", "nonfinite", "value_only_vs_full_bit_mismatch")
BYTE_GATES = ("hist_mismatch", "pack_size_mismatch", "pack_cards_mismatch", "pack_actions_mismatch", "pack_decision_mismatch", "pack_global_mismatch")


def run(command, **kwargs):
    return subprocess.run([str(c) for c in command], check=True, env=ENV, **kwargs)


def stem(seed):
    return RUNS / f"gauntlet-{seed}" / "checkpoint-2560"


def meta(seed):
    return json.loads(stem(seed).with_suffix(".json").read_text())


def python(*args):
    return [sys.executable, *args]


def parallel(jobs, workers=THREADS):
    with ThreadPoolExecutor(workers) as pool:
        return list(pool.map(lambda f: f(), jobs))


def concatenate():
    target = DATA / "decisions.bin"
    if target.exists():
        return target
    with target.open("wb") as out:
        for shard in sorted(DATA.glob("decisions-*.bin")):
            with shard.open("rb") as f:
                shutil.copyfileobj(f, out)
    return target


def dataset_summary(path):
    r = np.memmap(path, dtype=RECORD, mode="r")
    reports = [json.loads(p.read_text()) for p in sorted(DATA.glob("decisions-*.json"))]
    lists = reports[0]["lists"]
    per_list = {name: [0, 0] for name in lists}
    deck = np.where(r["to_play"] == 0, r["deck0"], r["deck1"])
    for seat in (0, 1):
        ids, counts = np.unique(deck[r["to_play"] == seat], return_counts=True)
        for i, c in zip(ids, counts):
            per_list[lists[int(i)]][seat] = int(c)
    ident = (r["cards"][:, :, 0].astype(np.int32) << 8) + r["cards"][:, :, 1]
    loc = r["cards"][:, :, 2]
    keep = (loc > 0) & ~((ident == 0) & ((loc == 1) | (loc == 2) | (loc == 7))) | r["selection"].any(axis=2)
    rows = keep.sum(axis=1)  # Lower bound on kept rows; net_diff reports the true maximum.
    options = r["counts"]
    def pct(values):
        return {q: int(np.percentile(values, float(q))) for q in ("50", "90", "99", "99.9", "100")}
    cells = [c for rep in reports for c in rep["cells"]]
    voids = [v for rep in reports for v in rep["void"]]
    seeds = sorted(c["seed"] for c in cells) + sorted(v["seed"] for v in voids)
    return {"decisions": len(r), "games": int(sum(len(c["winners"]) for c in cells)), "cells": len(cells), "void_cells": voids,
            "seed_min": min(seeds), "seed_max": max(seeds) + reports[0]["games_per_cell"] - 1,
            "native_sha256": reports[0]["native_sha256"], "per_list_decisions_seat0_seat1": per_list,
            "lists_with_both_seats": sum(1 for v in per_list.values() if v[0] and v[1]), "rows": pct(rows), "options": pct(options),
            "full_history_decisions": int((r["hist"][:, 15, 3] != 0).sum()), "empty_history_decisions": int((r["hist"][:, 0, 3] == 0).sum()),
            "past_16_own_decisions": past_ring(r), "normalization_changed_decisions": normalization_changes(r), "turn_percentiles": pct(r["global"][:, 4]),
            "active_selection_decisions": int((r["decision"][:, 11] != 0).sum()),
            "unknown_stat_rows": int(((r["cards"][:, :, 12].astype(int) * 256 + r["cards"][:, :, 13] > 60000) & (loc > 0)).sum())}


def past_ring(r):
    """Decisions at or past a seat's 17th in its game, when its history ring has probably wrapped."""
    seen, total = {}, 0
    for game, seat in zip(r["game"].tolist(), r["to_play"].tolist()):
        seen[game, seat] = seen.get((game, seat), 0) + 1
        total += seen[game, seat] > 16
    return total


def normalization_changes(r):
    from obs import normalize_history_turn
    return sum(int((normalize_history_turn(r["hist"][i:i + 4096], r["global"][i:i + 4096]) != r["hist"][i:i + 4096]).any(axis=(1, 2)).sum())
               for i in range(0, len(r), 4096))


def export_all():
    for seed in SEEDS:
        target = OUT / f"gauntlet-{seed}.ednet"
        if not target.exists():
            run(python(HERE / "export_net.py", stem(seed), target, "--expect-sha", meta(seed)["params_sha256"]), stdout=subprocess.DEVNULL)


def references(name):
    """Expect and per-checkpoint reference files for a decisions file."""
    decisions = DATA / f"{name}.bin"
    jobs = []
    expect = DATA / f"{name}.expect"
    if not expect.exists():
        jobs.append(lambda: run(python(HERE / "reference.py", "expect", "--decisions", decisions, "--output", expect), stdout=subprocess.DEVNULL))
    for seed in SEEDS:
        ref = DATA / f"{name}-{seed}.ref"
        if not ref.exists():
            jobs.append(lambda ref=ref, seed=seed: run(python(HERE / "reference.py", "net", "--decisions", decisions, "--checkpoint", stem(seed), "--output", ref), stdout=subprocess.DEVNULL))
    parallel(jobs)


def diff(binary, seed, name, output, reference=True, net=None, limit=0, config=None, cached=True, dump=None):
    if cached and output.exists():
        return json.loads(output.read_text())
    m = meta(seed)
    command = [binary, "--net", net or OUT / f"gauntlet-{seed}.ednet", "--params-sha", m["params_sha256"], "--codes-sha", m["codes_sha256"],
               "--decisions", DATA / f"{name}.bin", "--expect", DATA / f"{name}.expect", "--json", output]
    if reference:
        command += ["--reference", DATA / f"{name}-{seed}.ref"]
    if limit:
        command += ["--limit", limit]
    if dump:
        command += ["--dump", dump]
    if config:
        command += ["--config", config]
    run(command)
    return json.loads(output.read_text())


def get(report, dotted):
    key, _, stat = dotted.partition(".")
    return report[key][stat]


def gate_failures(report, forward=True):
    failed = {}
    for key in COUNT_GATES:
        if report[key]:
            failed[key] = report[key]
    if forward:
        for key, limit in GATES.items():
            if get(report, key) > limit:
                failed[key] = get(report, key)
    return failed


def near_tie_check(report):
    """Every greedy mismatch must be a tie within the measured error for that decision."""
    bad = [m for m in report["greedy_mismatch"] if m["python_gap"] > 2 * m["worst_logit_error"]]
    return {"mismatches": report["greedy_mismatch_count"], "not_near_ties": len(bad), "examples": report["greedy_mismatch"][:10]}


def trim(report):
    return {k: v for k, v in report.items() if k not in ("greedy_mismatch", "verdict_examples")}


def control_rows(seed):
    faults = {1: "wrong GELU variant: exact erf", 2: "wrong LayerNorm epsilon: 1e-5", 3: "shuffled row order, references remapped",
              4: "shuffled row order, references not remapped", 5: "off-by-one action row reference in pack",
              6: "off-by-one row reference in forward only", 7: "pointer 0 reads the context token, not no_row",
              8: "thermometer unknown bit dropped", 9: "hint index misses its +1", 10: "action pool sum, not mean",
              11: "history turn normalization skipped", 12: "is-source flag not set", 13: "selected rows not kept",
              14: "context token in the state pool", 15: "audit skips finish and cancel flags"}
    rows = {}
    (OUT / "controls").mkdir(exist_ok=True)

    def one(fault):
        report = diff(BIN / f"net_diff_fault{fault}", seed, "decisions", OUT / "controls" / f"fault{fault}.json")
        mutated = diff(BIN / f"net_diff_fault{fault}", seed, "mutated", OUT / "controls" / f"fault{fault}-mutated.json", reference=False)
        failed = gate_failures(report)
        mutated_failed = gate_failures(mutated, forward=False)
        return fault, {"fault": faults[fault], "caught": bool(failed) or bool(mutated_failed),
                       "failed_gates": {k: float(v) for k, v in failed.items()},
                       "byte_gates_failed": sorted(k for k in failed if k in BYTE_GATES),
                       "value_max": report["value_error"]["max"], "value_mean": report["value_error"]["mean"],
                       "logit_max": report["logit_error"]["max"], "logit_mean": report["logit_error"]["mean"],
                       "greedy_mismatches": report["greedy_mismatch_count"],
                       "mutated_failed_gates": {k: float(v) for k, v in mutated_failed.items()}}
    for fault, row in parallel([lambda f=f: one(f) for f in faults]):
        rows[fault] = row
    return rows


def rewrite_net(seed, change, output):
    """The checkpoint's arrays through `change`, then serialized with valid hashes and the claimed sha."""
    from actor import load_checkpoint
    from net import BIN_POINTS, BIN_WIDTHS
    config, params, m, sha = load_checkpoint(stem(seed))
    arrays = [("const/bin_points", BIN_POINTS.astype(np.float32).copy()), ("const/bin_widths", BIN_WIDTHS.astype(np.float32).copy())]
    arrays += [(name, np.array(value, dtype=np.float32)) for name, value in export_net.flatten(params)]
    arrays = change(arrays)
    output.write_bytes(export_net.serialize(arrays, config.dict(), sha, m["codes_sha256"]))
    return [name for name, _ in arrays]


def perturbed_weights(seed):
    """Single small weight changes, and then every array in turn on a subset."""
    target_dir = OUT / "controls" / "weights"
    target_dir.mkdir(parents=True, exist_ok=True)
    singles = [("value_out/kernel", (0, 0), 1e-3), ("logit_hidden/kernel", (10, 10), 1e-3), ("logit_hidden/kernel", (10, 10), 1e-2),
               ("logit_hidden/kernel", (10, 10), 1e-1), ("Encoder_0/Block_1/Attention_0/Dense_0/kernel", (3, 5), 1e-3),
               ("Encoder_0/Dense_1/kernel", (40, 7), 1e-3), ("Encoder_0/LayerNorm_0/scale", (12,), 1e-3), ("Encoder_0/no_row", (5,), 1e-3)]
    results = {}

    def single(name, at, delta):
        path = target_dir / (name.replace("/", "_") + f"-{delta}.ednet")

        def change(arrays):
            for key, value in arrays:
                if key == name:
                    value[at] += delta
            return arrays
        rewrite_net(seed, change, path)
        report = diff(BIN / "net_diff", seed, "decisions", path.with_suffix(".json"), net=path)
        return f"{name} {at} +{delta}", {"at": at, "delta": delta, "failed_gates": {k: float(v) for k, v in gate_failures(report).items()},
                      "value_max": report["value_error"]["max"], "logit_max": report["logit_error"]["max"]}
    for name, row in parallel([lambda a=a: single(*a) for a in singles]):
        results[name] = row

    names = rewrite_net(seed, lambda a: a, target_dir / "same.ednet")
    good = diff(BIN / "net_diff", seed, "decisions", target_dir / "same.json", net=target_dir / "same.ednet", limit=4000)
    sweep = {"baseline_failed_gates": {k: float(v) for k, v in gate_failures(good).items()}}
    if good["pack_cards_mismatch"]:
        raise RuntimeError("Rewritten baseline differs")
    undetected = []

    def every(name, noise=2e-3):
        path = target_dir / (name.replace("/", "_") + f".sweep-{noise}.ednet")

        def change(arrays):
            # Per element: a uniform shift is invisible through a LayerNorm's mean subtraction.
            rng = np.random.default_rng(20261105)
            for key, value in arrays:
                if key == name:
                    value += rng.normal(0, noise, value.shape).astype(np.float32)
            return arrays
        rewrite_net(seed, change, path)
        report = diff(BIN / "net_diff", seed, "decisions", path.with_suffix(".json"), net=path, limit=4000, cached=True)
        path.unlink()
        return name, gate_failures(report)
    rows = parallel([lambda n=n: every(n) for n in names])
    sweep["arrays"] = len(rows)
    for name, failed in rows:
        if not failed:
            undetected.append(name)
    sweep["undetected"] = undetected
    # The bin tables hold values in the hundreds, so the same noise is relatively much smaller.
    sweep["undetected_at_noise_1"] = [name for name, failed in parallel([lambda n=n: every(n, 1.0) for n in undetected]) if not failed]
    return {"single": results, "every_array_noise_2e-3": sweep}


def load_checks(seed):
    target = OUT / "controls" / "load"
    target.mkdir(parents=True, exist_ok=True)
    good = OUT / f"gauntlet-{seed}.ednet"
    data = good.read_bytes()
    m = meta(seed)
    params, codes = m["params_sha256"], m["codes_sha256"]
    config = "509,32,64,2,4,128"
    cases = {}

    def probe(path, p=params, cc=codes, g=config):
        result = subprocess.run([str(BIN / "net_load_test"), str(path), p, cc, g], capture_output=True, text=True)
        return result.returncode, result.stdout.strip()

    def write(name, blob):
        path = target / name
        path.write_bytes(blob)
        return path
    header_bytes = struct.unpack_from("<I", data, 12)[0]
    cases["good file"] = (0, probe(good))
    cases["truncated"] = (1, probe(write("trunc", data[:-4])))
    cases["extended"] = (1, probe(write("ext", data + b"\0" * 64)))
    cases["payload byte flip"] = (1, probe(write("flip", data[:header_bytes + 100] + bytes([data[header_bytes + 100] ^ 1]) + data[header_bytes + 101:])))
    cases["header byte flip"] = (1, probe(write("hflip", data[:300] + bytes([data[300] ^ 1]) + data[301:])))
    cases["bad magic"] = (1, probe(write("magic", b"XXXXXXXX" + data[8:])))
    cases["bad version"] = (1, probe(write("ver", data[:8] + struct.pack("<I", 2) + data[12:])))
    cases["wrong params sha expected"] = (1, probe(good, p="0" * 64))
    cases["wrong codes sha expected"] = (1, probe(good, cc="0" * 64))
    cases["empty params sha expected"] = (1, probe(good, p=""))
    cases["wrong config expected"] = (1, probe(good, g="509,32,96,2,4,128"))
    cases["empty file"] = (1, probe(write("empty", b"")))
    cases["missing file"] = (1, probe(target / "does-not-exist"))

    def renamed(arrays):
        return [("Encoder_0/nope" if name == "Encoder_0/no_row" else name, value) for name, value in arrays]

    def reshaped(arrays):
        return [(name, value.reshape(-1, 1) if name == "Encoder_0/no_row" else value) for name, value in arrays]

    def missing(arrays):
        return [(name, value) for name, value in arrays if name != "Encoder_0/no_row"]

    def extra(arrays):
        return arrays + [("extra", np.zeros(4, dtype=np.float32))]

    def duplicate(arrays):
        return [("Encoder_0/stage/embedding" if name == "Encoder_0/turn/embedding" else name, value) for name, value in arrays]

    for name, change in (("renamed array", renamed), ("reshaped array", reshaped), ("missing array", missing), ("extra array", extra), ("duplicate array", duplicate)):
        path = target / name.replace(" ", "_")
        rewrite_net(seed, change, path)
        cases[name] = (1, probe(path))
    results = {name: {"expected_exit": want, "exit": got[0], "output": got[1], "pass": want == got[0]} for name, (want, got) in cases.items()}
    return results


def write_checkpoint(params, config, target):
    """A checkpoint in actor.py's format, then its export. Returns the params hash."""
    import flax
    import hashlib
    import decks
    raw = flax.serialization.msgpack_serialize(params)
    sha = hashlib.sha256(raw).hexdigest()
    target.with_suffix(".msgpack").write_bytes(raw)
    codes_sha, _ = decks.load_codes()
    target.with_suffix(".json").write_text(json.dumps({"format": "edison_net_v1", "params_sha256": sha, "codes_sha256": codes_sha, "config": config.dict()}))
    run(python(HERE / "export_net.py", target, target.with_suffix(".ednet")), stdout=subprocess.DEVNULL)
    return sha, codes_sha


def reference_and_diff(binary, target, sha, codes_sha, config_text, limit, output):
    ref = target.with_suffix(".ref")
    if not ref.exists():
        run(python(HERE / "reference.py", "net", "--decisions", DATA / "decisions.bin", "--checkpoint", target, "--output", ref, "--limit", limit), stdout=subprocess.DEVNULL)
    run([binary, "--net", target.with_suffix(".ednet"), "--params-sha", sha, "--codes-sha", codes_sha, "--decisions", DATA / "decisions.bin",
         "--expect", DATA / "decisions.expect", "--reference", ref, "--json", output, "--limit", limit, "--config", config_text])
    return json.loads(output.read_text())


def other_config():
    """A wider, deeper random net whose head size is not a multiple of the vector width."""
    import jax
    from net import NetConfig, init_params
    config = NetConfig(n_codes=509, id_dim=48, channels=96, layers=3, heads=4, critic_hidden=64)
    folder = OUT / "controls" / "other-config"
    folder.mkdir(parents=True, exist_ok=True)
    _, params = init_params(config, 85000123)
    # The heads start at zero, which would hide their wiring. Give them weights.
    rng = np.random.default_rng(20261105)
    params = jax.tree_util.tree_map(lambda x: np.asarray(x), jax.device_get(params))
    params["logit_out"]["kernel"] = rng.normal(0, 0.1, params["logit_out"]["kernel"].shape).astype(np.float32)
    params["value_out"]["kernel"] = rng.normal(0, 0.1, params["value_out"]["kernel"].shape).astype(np.float32)
    target = folder / "random"
    sha, codes_sha = write_checkpoint(params, config, target)
    report = reference_and_diff(BIN / "net_diff", target, sha, codes_sha, "509,48,96,3,4,64", 3000, folder / "diff.json")
    return trim(report)


SMALL_ACTIVATION_GATES = {"value_error.max": 2e-4, "row_error.mean": 1e-6}


def small_activation(seed):
    """The LayerNorm epsilon control. Real activations have variance far above 1e-5, so real
    weights cannot see the wrong epsilon. Here the real net has its row and token outputs shrunk
    1000 times, so the first LayerNorm's input variance is near epsilon, and JAX is rerun on it.
    """
    from actor import load_checkpoint
    folder = OUT / "controls" / "small-activation"
    folder.mkdir(parents=True, exist_ok=True)
    config, params, _, _ = load_checkpoint(stem(seed))
    params = {k: v for k, v in params.items()}
    params["Encoder_0"] = dict(params["Encoder_0"])
    for name in ("Dense_0", "Dense_3"):
        params["Encoder_0"][name] = {k: np.asarray(v, dtype=np.float32) * 1e-3 for k, v in params["Encoder_0"][name].items()}
    target = folder / "shrunk"
    sha, codes_sha = write_checkpoint(params, config, target)
    text = "509,32,64,2,4,128"
    rows = {}
    for label, binary in (("correct", "net_diff"), ("wrong_epsilon", "net_diff_fault2"), ("exact_gelu", "net_diff_fault1")):
        report = reference_and_diff(BIN / binary, target, sha, codes_sha, text, 4000, folder / f"{label}.json")
        rows[label] = {**{k: report[k] for k in ("records", "value_error", "logit_error", "logit_error_scaled", "state_error", "row_error", "greedy_mismatch_count")},
                       "failed_gates": {k: get(report, k) for k, limit in SMALL_ACTIVATION_GATES.items() if get(report, k) > limit}}
    return rows


def noise_floors():
    def one(seed):
        output = OUT / f"noise-{seed}.json"
        native = DATA / f"native-{seed}.bin"
        if not output.exists():
            diff(BIN / "net_diff", seed, "decisions", OUT / f"correct-{seed}-dump.json", dump=native, cached=False)
            with output.open("w") as out:
                run(python(HERE / "noise_floor.py", "--decisions", DATA / "decisions.bin", "--checkpoint", stem(seed), "--reference", DATA / f"decisions-{seed}.ref", "--native", native), stdout=out, stderr=subprocess.DEVNULL)
        return seed, json.loads(output.read_text())
    return dict(parallel([lambda s=s: one(s) for s in SEEDS], 3))


def lower_isa(seed):
    """The same header on AVX2+FMA and on baseline x86-64, to exercise the narrower lane paths."""
    results = {}
    flags = {"avx2_fma": ["-march=x86-64-v3"], "sse2_no_fma": ["-march=x86-64"]}

    def one(name):
        binary = BIN / f"net_diff_{name}"
        run(["g++", "-std=c++17", "-O3", *flags[name], "-Wno-psabi", f"-I{HERE}", HERE / "net_diff.cpp", "-o", binary])
        report = diff(binary, seed, "decisions", OUT / "controls" / f"{name}.json", limit=8000)
        return name, {**trim(report), "failed_gates": {k: float(v) for k, v in gate_failures(report).items()}}
    for name, row in parallel([lambda n=n: one(n) for n in flags]):
        results[name] = row
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skip-controls", action="store_true")
    args = parser.parse_args()
    (OUT / "controls").mkdir(parents=True, exist_ok=True)
    decisions = concatenate()
    summary = {"gates": GATES, "dataset": dataset_summary(decisions)}
    export_all()
    if not (DATA / "mutated.bin").exists():
        run(python(HERE / "mutate_decisions.py", "--decisions", decisions, "--mutated", DATA / "mutated.bin", "--extremes", DATA / "extremes.bin", "--coverage", DATA / "mutation-coverage.json"))
    references("decisions")
    references("extremes")
    expect = DATA / "mutated.expect"
    if not expect.exists():
        run(python(HERE / "reference.py", "expect", "--decisions", DATA / "mutated.bin", "--output", expect), stdout=subprocess.DEVNULL)

    correct = {}
    for seed, report in zip(SEEDS, parallel([lambda s=s: diff(BIN / "net_diff", s, "decisions", OUT / f"correct-{s}.json", cached=False) for s in SEEDS], 3)):
        correct[seed] = {**trim(report), "failed_gates": {k: float(v) for k, v in gate_failures(report).items()}, "greedy": near_tie_check(report)}
    summary["correct"] = correct
    extremes = {}
    for seed in SEEDS:
        report = diff(BIN / "net_diff", seed, "extremes", OUT / f"extremes-{seed}.json", cached=False)
        extremes[seed] = {**trim(report), "failed_gates": {k: float(v) for k, v in gate_failures(report).items()}, "greedy": near_tie_check(report)}
    summary["extremes"] = extremes
    mutated = diff(BIN / "net_diff", SEEDS[0], "mutated", OUT / "correct-mutated.json", reference=False, cached=False)
    summary["mutated"] = {**trim(mutated), "examples": mutated["verdict_examples"], "coverage": json.loads((DATA / "mutation-coverage.json").read_text())}
    if not args.skip_controls:
        summary["controls"] = control_rows(SEEDS[0])
        summary["perturbed_weights"] = perturbed_weights(SEEDS[0])
        summary["load_checks"] = load_checks(SEEDS[0])
        summary["small_activation"] = small_activation(SEEDS[0])
        summary["controls"][2]["caught_on_shrunk_net"] = bool(summary["small_activation"]["wrong_epsilon"]["failed_gates"])
        summary["other_config"] = other_config()
        summary["noise_floor"] = noise_floors()
        summary["lower_isa"] = lower_isa(SEEDS[0])
    (OUT / "summary.json").write_text(json.dumps(summary, indent=1) + "\n")
    brief = {"dataset": {k: summary["dataset"][k] for k in ("decisions", "games", "cells", "rows", "options", "full_history_decisions", "past_16_own_decisions", "normalization_changed_decisions", "lists_with_both_seats")},
             "correct_failed": {s: c["failed_gates"] for s, c in correct.items()},
             "greedy": {s: {k: c["greedy"][k] for k in ("mismatches", "not_near_ties")} for s, c in correct.items()}}
    print(json.dumps(brief, indent=1))


if __name__ == "__main__":
    main()
