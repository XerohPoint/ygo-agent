#!/usr/bin/env python3
"""Build observation sets beyond real play, for the audit and shape checks.

mutated: real observations changed one way per contract rule, each confirmed to trigger the
rule it targets in Python's own audit, plus random single-field mutations. Verdicts are compared
with net_diff exactly, rule and column alike.
extremes: valid-looking observations at the shape limits real play did not reach: 160 kept rows,
256 options, both, and no rows. These also go through the network.
"""
import argparse
import json
from pathlib import Path
import sys

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from dump_format import RECORD, python_pipeline  # noqa: E402

N_CODES = 509
ACTIVE = lambda r: r["decision"][11] != 0  # noqa: E731
INACTIVE = lambda r: r["decision"][11] == 0  # noqa: E731


def used_refs(r):
    return np.nonzero(r["actions"][: r["counts"], 0] > 0)[0]


def first_where(mask):
    found = np.nonzero(mask)[0]
    return int(found[0]) if found.size else None


# name: (expected code, precondition, mutator). The mutator returns False if it does not apply.
def rule_table():
    table = {}

    def rule(name, code, pre=lambda r: True):
        def wrap(fn):
            table[name] = (code, pre, fn)
            return fn
        return wrap

    @rule("counts zero", 1)
    def _(r): r["counts"] = 0; r["legal"] = 0
    @rule("counts negative", 1)
    def _(r): r["counts"] = -3; r["legal"] = -3
    @rule("counts over limit", 1)
    def _(r): r["counts"] = 257; r["legal"] = 257
    @rule("overflow flag", 1)
    def _(r): r["overflow"] = 1
    @rule("counts differ from legal", 1)
    def _(r): r["legal"] = r["counts"] + 1

    @rule("visible id past list", 2)
    def _(r):
        row = first_where(r["cards"][:, 2] > 0)
        r["cards"][row, 0] = 2  # 512 is past the 509 codes
        r["cards"][row, 1] = 0

    @rule("own hand hidden", 3, lambda r: np.any((r["cards"][:, 2] == 2) & (r["cards"][:, 4] == 0)))
    def _(r):
        row = first_where((r["cards"][:, 2] == 2) & (r["cards"][:, 4] == 0))
        r["cards"][row, :2] = 0

    @rule("hidden row with statistics", 4, lambda r: np.any((r["cards"][:, 0] == 0) & (r["cards"][:, 1] == 0) & (r["cards"][:, 2] > 0) & ~((r["cards"][:, 2] == 2) & (r["cards"][:, 4] == 0))))
    def _(r):
        row = first_where((r["cards"][:, 0] == 0) & (r["cards"][:, 1] == 0) & (r["cards"][:, 2] > 0) & ~((r["cards"][:, 2] == 2) & (r["cards"][:, 4] == 0)))
        r["cards"][row, 12] = 1

    card_bounds = {2: 8, 3: 79, 4: 1, 5: 8, 6: 1, 7: 7, 8: 26, 9: 13, 10: 15, 11: 1}
    for col, bound in card_bounds.items():
        def make(col=col, bound=bound):
            def fn(r):
                r["cards"][first_where(r["cards"][:, 2] > 0), col] = bound + 1
            return fn
        table[f"card column {col}"] = (100 + col, lambda r: True, make())

    action_bounds = {3: 16, 4: 9, 5: 1, 7: 3, 8: 8, 9: 13, 10: 31, 11: 9}
    for col, bound in action_bounds.items():
        def make(col=col, bound=bound):
            def fn(r):
                r["actions"][0, col] = bound + 1
            return fn
        table[f"action column {col}"] = (200 + col, lambda r: True, make())

    @rule("data past offered", 7, lambda r: r["counts"] < 256)
    def _(r): r["actions"][r["counts"], 4] = 1
    @rule("action id past list", 8)
    def _(r): r["actions"][0, 0] = 0; r["actions"][0, 1] = 2; r["actions"][0, 2] = 0
    @rule("action row past last", 9)
    def _(r): r["actions"][0, 0] = 200

    @rule("action names another card", 10, lambda r: used_refs(r).size > 0)
    def _(r): r["actions"][used_refs(r)[0], 2] ^= 1

    @rule("schema", 11)
    def _(r): r["decision"][0] = 2
    @rule("selected counter", 12)
    def _(r): r["decision"][6] += 1
    @rule("confirmed counter", 12)
    def _(r): r["decision"][7] += 1
    @rule("selection flag value", 13, lambda r: np.any(r["cards"][:, 2] > 0))
    def _(r): r["selection"][first_where(r["cards"][:, 2] > 0), 0] = 2
    @rule("confirmed without selected", 13, lambda r: np.any(r["cards"][:, 2] > 0))
    def _(r):
        r["selection"][first_where(r["cards"][:, 2] > 0), 2] = 1
        r["decision"][7] += 1
    @rule("selection on padding", 14, lambda r: np.any(r["cards"][:, 2] == 0))
    def _(r):
        r["selection"][first_where(r["cards"][:, 2] == 0), 0] = 1
    @rule("stale decision field", 15, INACTIVE)
    def _(r): r["decision"][4] = 1
    @rule("stale selection", 15, INACTIVE)
    def _(r): r["selection"][first_where(r["cards"][:, 2] > 0), 0] = 1

    @rule("hint below range", 16, ACTIVE)
    def _(r): r["decision"][1] = 499
    @rule("hint above range", 16, ACTIVE)
    def _(r): r["decision"][1] = 576
    @rule("stage negative", 17, ACTIVE)
    def _(r): r["decision"][2] = -1
    @rule("stage high", 17, ACTIVE)
    def _(r): r["decision"][2] = 4
    @rule("min negative", 18, ACTIVE)
    def _(r): r["decision"][4] = -1
    @rule("max below min", 18, ACTIVE)
    def _(r): r["decision"][5] = r["decision"][4] - 1
    @rule("source past last", 19, ACTIVE)
    def _(r): r["decision"][3] = 161
    @rule("source on hidden row", 20, ACTIVE)
    def _(r):
        hidden = first_where((r["cards"][:, 0] == 0) & (r["cards"][:, 1] == 0))
        r["decision"][3] = hidden + 1

    def offered_mismatch(r):
        rows = r["actions"][: r["counts"], 0]
        rows = set((rows[rows > 0] - 1).tolist())
        valid = [i for i in range(160) if r["cards"][i, 2] > 0 and i not in rows and not r["selection"][i].any()]
        return valid
    @rule("candidate flag missing", 21, lambda r: ACTIVE(r) and used_refs(r).size > 0)
    def _(r):
        target = r["actions"][used_refs(r)[0], 0] - 1
        r["selection"][target, 0] = 0
    @rule("candidate flag extra", 21, lambda r: ACTIVE(r) and offered_mismatch(r))
    def _(r): r["selection"][offered_mismatch(r)[0], 0] = 1
    @rule("finish flag", 22, ACTIVE)
    def _(r): r["decision"][8] ^= 1
    @rule("cancel flag", 22, ACTIVE)
    def _(r): r["decision"][9] ^= 1

    for col, bound in {4: 16, 5: 10, 6: 1, 7: 1}.items():
        def make(col=col, bound=bound):
            def fn(r):
                r["global"][col] = bound + 1
            return fn
        table[f"global column {col}"] = (300 + col, lambda r: True, make())
    for col, bound in {3: 16, 4: 9, 5: 1, 7: 3, 8: 8, 9: 13, 10: 31, 11: 9, 13: 10}.items():
        def make(col=col, bound=bound):
            def fn(r):
                r["hist"][15, col] = bound + 1
            return fn
        table[f"history column {col}"] = (400 + col, lambda r: True, make())
    @rule("history id past list", 25)
    def _(r): r["hist"][15, 1] = 2; r["hist"][15, 2] = 0
    @rule("history turn offset after wrap", 26, lambda r: r["hist"][0, 3] != 0)
    def _(r): r["hist"][0, 12] = 17
    @rule("history turn offset on empty row", 26, lambda r: r["hist"][0, 3] == 0)
    def _(r): r["hist"][0, 12] = 17
    return table


def history_unfilled(records, rng, count):
    """Empty first row, later rows present, turn columns on both sides of the clip range."""
    out = []
    full = np.nonzero((records["hist"][:, 15, 3] != 0))[0]
    for index in rng.choice(full, count):
        r = records[int(index)].copy()
        r["hist"][0] = 0
        r["hist"][1:, 12] = rng.integers(0, 40, 15)
        out.append(r)
    return out


def row_lookup_cases(records, rng, count):
    """Rows kept by a rule no real observation here depends on: a selection flag on a hidden
    private row, and an offered action pointing at a padding row. Both pass the audit."""
    out = []
    active = np.nonzero(records["decision"][:, 11] != 0)[0]
    inactive = np.nonzero(records["decision"][:, 11] == 0)[0]
    for index in rng.choice(active, count):
        r = records[int(index)].copy()
        hidden = (r["cards"][:, 0] == 0) & (r["cards"][:, 1] == 0) & np.isin(r["cards"][:, 2], (1, 2, 7))
        row = first_where(hidden)
        if row is not None:
            r["selection"][row, 1] = 1
            r["decision"][6] += 1
            out.append(r)
    for index in rng.choice(inactive, count):
        r = records[int(index)].copy()
        row = first_where(r["cards"][:, 2] == 0)
        if row is not None:
            r["actions"][0, :3] = (row + 1, 0, 0)
            out.append(r)
    return out


def mutated(records, rng, per_rule, fuzz):
    out, coverage = [], {}
    for name, (code, pre, mutate) in rule_table().items():
        hits = 0
        for index in rng.permutation(len(records)):
            if hits == per_rule:
                break
            r = records[index:index + 1].copy()[0]
            if not pre(r):
                continue
            mutate(r)
            verdict, _ = python_pipeline(r, N_CODES)
            if verdict != code:
                continue  # An earlier rule caught it on this observation. Try another.
            out.append(r.copy())
            hits += 1
        coverage[name] = {"code": code, "samples": hits}
        if hits == 0:
            raise ValueError(f"No real observation can trigger rule {name}")

    # Real play never has an empty first row with others present (see docs/edison-native-net.md),
    # so normalize_history_turn's rewrite only runs on these.
    for r in history_unfilled(records, rng, 400):
        out.append(r)
    out.extend(row_lookup_cases(records, rng, 200))

    # Single-field mutations with values chosen to sit on either side of a bound.
    fields = {"cards": 255, "selection": 3, "actions": 255, "hist": 255, "global": 255}
    for _ in range(fuzz):
        r = records[int(rng.integers(len(records)))].copy()
        which = rng.choice(("cards", "selection", "actions", "hist", "global", "decision"))
        if which == "decision":
            col = int(rng.integers(12))
            r["decision"][col] = int(rng.choice((-1, 0, 1, 2, 5, 161, 499, 500, 575, 576, int(r["decision"][col]) + 1)))
        else:
            arr = r[which]
            at = tuple(int(rng.integers(n)) for n in arr.shape)
            if which == "actions" and rng.random() < 0.8:  # Mostly inside the offered range.
                at = (int(rng.integers(max(int(r["counts"]), 1))), at[1])
            top = fields[which]
            arr[at] = int(rng.choice((0, 1, 2, 7, 8, 9, 10, 15, 16, 17, 26, 27, 79, 80, 160, 161, top)))
        out.append(r)
    return np.array(out, dtype=RECORD), coverage


def extremes(records, rng, count):
    """Each is built from a real observation, so column values stay plausible."""
    out = []
    picks = rng.permutation(len(records))
    for index in picks[:count]:
        base = records[int(index)]
        # All rows kept: copy visible rows into every slot, as a public zone could not hold.
        r = base.copy()
        visible = np.nonzero((r["cards"][:, 0].astype(int) << 8) + r["cards"][:, 1] > 0)[0]
        if visible.size == 0:
            continue
        r["cards"] = r["cards"][rng.choice(visible, 160)]
        r["actions"][:] = 0
        r["counts"] = 1
        r["legal"] = 1
        r["selection"][:] = 0
        r["decision"][1:] = 0
        out.append(r.copy())
        # All options: repeat real options, with reference fields left pointing at real rows.
        r = base.copy()
        n = int(r["counts"])
        r["actions"][:] = r["actions"][np.arange(256) % n]
        r["counts"] = 256
        r["legal"] = 256
        out.append(r.copy())
        # Both limits at once.
        r2 = r.copy()
        r2["cards"] = r2["cards"][rng.choice(visible, 160)]
        r2["actions"][:, 0] = rng.integers(0, 161, 256)
        r2["actions"][:, 1] = r2["cards"][np.maximum(r2["actions"][:, 0].astype(int) - 1, 0), 0]
        r2["actions"][:, 2] = r2["cards"][np.maximum(r2["actions"][:, 0].astype(int) - 1, 0), 1]
        r2["selection"][:] = 0
        r2["decision"][1:] = 0
        out.append(r2.copy())
        # No rows at all: nothing to attend to but the context token.
        r3 = base.copy()
        r3["cards"][:] = 0
        r3["selection"][:] = 0
        r3["actions"][:] = 0
        r3["actions"][0, 3] = 1
        r3["counts"] = 1
        r3["legal"] = 1
        r3["decision"][1:] = 0
        out.append(r3.copy())
    # Unfilled history that also goes through the network: turn columns stay in table range.
    for r in history_unfilled(records, rng, count):
        out.append(r)
    out.extend(row_lookup_cases(records, rng, count))
    return np.array(out, dtype=RECORD)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--decisions", type=Path, required=True)
    parser.add_argument("--mutated", type=Path, required=True)
    parser.add_argument("--extremes", type=Path, required=True)
    parser.add_argument("--coverage", type=Path, required=True)
    parser.add_argument("--per-rule", type=int, default=20)
    parser.add_argument("--fuzz", type=int, default=20000)
    parser.add_argument("--extreme-bases", type=int, default=100)
    parser.add_argument("--rng", type=int, default=20261105, help="Mutation choices only; no game runs here")
    args = parser.parse_args()
    records = np.memmap(args.decisions, dtype=RECORD, mode="r")
    rng = np.random.default_rng(args.rng)
    m, coverage = mutated(records, rng, args.per_rule, args.fuzz)
    m.tofile(args.mutated)
    e = extremes(records, rng, args.extreme_bases)
    # The extremes go through the network, so each must pass Python's whole pipeline.
    keep = [i for i, r in enumerate(e) if python_pipeline(r, N_CODES)[0] == 0]
    dropped = {}
    for i in set(range(len(e))) - set(keep):
        code = python_pipeline(e[i], N_CODES)[0]
        dropped[code] = dropped.get(code, 0) + 1
    e[keep].tofile(args.extremes)
    args.coverage.write_text(json.dumps({"rules": coverage, "mutated": len(m), "fuzz": args.fuzz, "extremes": len(keep),
                                         "extremes_dropped_by_rule": {str(k): v for k, v in dropped.items()}}, indent=1) + "\n")
    print(len(m), len(keep), dropped)


if __name__ == "__main__":
    main()
