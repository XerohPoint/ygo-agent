#!/usr/bin/env python3
"""M3 Stage A: the round 2560 networks on held-out builds of cards they trained on.

docs/edison-m3-protocol.md is the plan, and this script is its exact form.

  select     the held-out set from the corpus, into stage-a-lists.json, and the decks the env loads
  selftest   the decision rules on made-up cells with known answers, and the shape of the matrix
  livecheck  a few cells on training lists only, with every held-out deck loaded; plays no corpus list
  freeze     records every input in m3a-manifest.json, to be committed before any game
  play       the cells, in chunks that resume; refuses to start unless the inputs match the manifest
  analyze    the verdict: exit 0 on a pass, 1 on a fail
  search     the reported search check, run after analyze
"""
import argparse
from collections import Counter
import gzip
import hashlib
import json
import math
import os
from pathlib import Path
import random
import subprocess
import sys
import time

import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tools/edison/learner"))
import decks as deck_registry  # noqa: E402

CORPUS = ROOT / "assets/edison/corpus"
CORPUS_SUMMARY = ROOT / "tests/edison/reports/corpus-2026-11-09/summary.json"
LISTS = HERE / "stage-a-lists.json"
MANIFEST = HERE / "m3a-manifest.json"
BUILD = ROOT / "build/edison-heldout"
OUT = BUILD / "stage-a"
DECKS = BUILD / "decks"
EXTRA = BUILD / "extra-decks.json"
RESULT = BUILD / "stage-a-result.json"
RUNS = ROOT / "build/edison-retrain-2026-11-04/runs"
NATIVE = ROOT / "ygoenv/ygoenv/ygopro/ygopro_ygoenv.cpython-310-x86_64-linux-gnu.so"
NATIVE_SHA = "b8fcd2bcf7774ae4edf8a4a800c2c55c006f62cb5cc1ef925fe316365a1ddf6f"
CODES_SHA = "53fcea4df9298ee7c3e0eb548c533048932eb870f090eb456a2ecac031fcbfab"
PARAMS_SHA = {"1702": "cb4cb1ad86aa5f6e33a339809e421f726225c9c13b70eaed5394f38cec6798cd",
              "1703": "03132b3f63c97f6aeec7b54c544ce45e450080f235389ac05075950f33f28ce0",
              "1704": "ddbdcee21ccceb9a60485aa785888b08c8c735ba79b46550ff18f83a9412a3a8"}
OPTIMIZERS = tuple(PARAMS_SHA)
ARMS = ("pilot", "face", "mirror")
SEATS = (0, 1)

HELD_SEED, HELD_COUNT = 88130000, 4      # one block of 4 per (held-out list, training list)
KNOWN_SEED, KNOWN_COUNT = 88180000, 32   # one block of 32 per (training list, training list)
HELD_END, KNOWN_END = 88179999, 88199999
LIVE_SEED = 88410000
FAR = 20
BAR, LOW_BAR, SEAT_BAR, GAP_BAR, VOID_BAR, MIN_FAR = 0.55, 0.50, 0.50, -0.10, 0.01, 10
RESAMPLES, BOOT_SEED = 10000, 88810001
REPLAY_EVERY = 128
CHUNK_LISTS = 8
SEARCH_SEED, SEARCH_DRAW, SEARCH_LISTS, SEARCH_OPPONENTS = 88800000, 88810002, 60, 4
SEARCH_SETTING = (64, 48, 4, 1.0)
SEARCH_BIN = ROOT / "build/forward_model/edison_search"
SEARCH_NETS = ROOT / "build/edison-search/nets"
SEARCH_BIN_SHA = "167be8aa8f9a4767789c3e81dc719051dd41c32afc3678d9cf6172d307a5548f"  # M2's frozen binary
DEPENDS = ("tools/edison/heldout/stage_a.py", "tools/edison/learner/actor.py", "tools/edison/learner/evaluate.py",
           "tools/edison/learner/decks.py", "tools/edison/learner/obs.py", "tools/edison/learner/net.py",
           "tools/edison/learner/codes_v1.txt", "ygoai/edison_policy.py", "assets/edison/legality.json",
           "assets/edison/corpus/manifest.json", "tools/edison/heldout/stage-a-lists.json")


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def text_sha(text):
    return hashlib.sha256(text.encode()).hexdigest()


# ---------------------------------------------------------------- selection

def main_extra(zones):
    """Main and Extra after the Gottoms rule, as a multiset of passcodes."""
    return Counter(c for c in zones["main"] + zones["extra"] if c not in deck_registry.OUTSIDE_V1)


def distance(a, b):
    """Cards counted with multiplicity: the sum over passcodes of the difference in copies. One swap counts 2."""
    return sum(abs(a[c] - b[c]) for c in set(a) | set(b))


def band(d):
    return "1-9" if d < 10 else ("10-19" if d < FAR else "20+")


def soak_crashes():
    """Lists that crashed in the soak's Main and Extra stage. The stage reported none, and this refuses a summary it can't read."""
    summary = json.loads(CORPUS_SUMMARY.read_text())
    stages = [s for s in summary["soak"] if s["output"].endswith("/main")]
    if len(stages) != 1 or stages[0]["finished"] != stages[0]["planned"]:
        raise SystemExit("the corpus summary has no complete Main and Extra soak stage")
    if stages[0]["crashes"]:
        raise SystemExit("the Main and Extra soak stage crashed: name the lists before selecting")
    return set()


def select(write=True):
    registry = deck_registry.registry()
    training = {name: deck_registry.parse_ydk(Path(p).read_text()) for name, p in registry.items()}
    codes_sha, codes = deck_registry.load_codes()
    if codes_sha != CODES_SHA:
        raise SystemExit("code list v1 changed")
    v1 = set(codes.values())
    seen = {c for z in training.values() for zone in z.values() for c in zone}
    trained = {c for z in training.values() for c in main_extra(z)} | (v1 - seen)
    known = {name: main_extra(z) for name, z in training.items()}
    crashed = soak_crashes()
    manifest = json.loads((CORPUS / "manifest.json").read_text())
    chosen, excluded = [], Counter()
    for row in manifest["lists"]:
        zones = deck_registry.parse_ydk((CORPUS / row["ydk"]).read_text())
        me = main_extra(zones)
        inside = all(c in v1 for c in me)
        untrained = sorted(c for c in me if c not in trained)
        twin = min(sorted(known), key=lambda n: distance(me, known[n]))
        d = distance(me, known[twin])
        if (inside != row["inside_v1_main_extra"] or untrained != sorted(u["code"] for u in row["untrained_main_extra"])
                or (twin, d) != (row["nearest_training"]["name"], row["nearest_training"]["distance"])):
            raise SystemExit(f"{row['list_id']}: the corpus manifest disagrees with this count")
        if not row["production_legal"]:
            excluded["not production legal"] += 1
        elif row["in_training"]:
            excluded["a training list"] += 1
        elif not inside:
            excluded["a Main or Extra card outside code list v1"] += 1
        elif untrained:
            excluded["a Main or Extra card that no training list plays"] += 1
        elif d == 0:
            excluded["a training list's exact Main and Extra"] += 1
        elif row["list_id"] in crashed:
            excluded["crashed in the soak's Main and Extra stage"] += 1
        else:
            chosen.append({"name": f"h_{row['list_id']}", "list_id": row["list_id"], "source": row["ydk"], "source_sha256": sha(CORPUS / row["ydk"]),
                           "archetype": row["archetype"], "twin": twin, "distance": d, "band": band(d), "far": d >= FAR,
                           "gottoms_removed": sum(zones["extra"].count(c) + zones["main"].count(c) for c in deck_registry.OUTSIDE_V1),
                           "main": sum(1 for c in zones["main"] if c not in deck_registry.OUTSIDE_V1),
                           "extra": sum(1 for c in zones["extra"] if c not in deck_registry.OUTSIDE_V1)})
    chosen.sort(key=lambda r: r["name"])
    if HELD_SEED + len(chosen) * len(known) * HELD_COUNT - 1 > HELD_END:
        raise SystemExit("the held-out seed block is too small for this many lists")
    out = {"rule": "production legal, not a training list, every Main and Extra card in code list v1 after the Gottoms rule and played by some training list's Main or Extra, "
                   "Main and Extra not identical to a training list's, and no crash in the soak's Main and Extra stage",
           "corpus_manifest_sha256": sha(CORPUS / "manifest.json"), "code_list_v1_sha256": codes_sha, "training_lists": sorted(known),
           "far_distance": FAR, "corpus_lists": len(manifest["lists"]), "excluded": dict(sorted(excluded.items())),
           "held_out": len(chosen), "far": sum(r["far"] for r in chosen), "by_band": dict(sorted(Counter(r["band"] for r in chosen).items())),
           "by_archetype": dict(Counter(r["archetype"] for r in chosen).most_common()), "by_twin": dict(Counter(r["twin"] for r in chosen).most_common()),
           "lists": chosen}
    if write:
        LISTS.write_text(json.dumps(out, indent=1) + "\n")
    return out


def write_decks():
    """The env's copies: Main and Extra after the Gottoms rule, with no Side, which a single duel never reads."""
    lists = json.loads(LISTS.read_text())["lists"]
    DECKS.mkdir(parents=True, exist_ok=True)
    extra = {}
    for row in lists:
        if sha(CORPUS / row["source"]) != row["source_sha256"]:
            raise SystemExit(f"{row['source']} changed since selection")
        zones = deck_registry.parse_ydk((CORPUS / row["source"]).read_text())
        kept = {"main": [c for c in zones["main"] if c not in deck_registry.OUTSIDE_V1],
                "extra": [c for c in zones["extra"] if c not in deck_registry.OUTSIDE_V1], "side": []}
        path = DECKS / f"{row['name']}.ydk"
        deck_registry.write_ydk(path, kept)
        extra[row["name"]] = str(path)
    EXTRA.write_text(json.dumps(extra, indent=1) + "\n")
    return extra


# ---------------------------------------------------------------- the matrix

def net_specs():
    import evaluate
    specs = {}
    for k in OPTIMIZERS:
        spec = evaluate.policy(f"net:{RUNS}/gauntlet-{k}/checkpoint-2560", True, True)
        if spec["sha256"] != PARAMS_SHA[k]:
            raise SystemExit(f"checkpoint {k} is not the M1 checkpoint")
        specs[k] = spec
    return specs


def cells(held, training, nets):
    """Every cell in a fixed order. A cell's seat is the seat whose result counts: the network's, or in the mirror the list's.

    pilot   the network plays the list against the heuristic on a training list
    face    the network plays the training list against the heuristic on the list
    mirror  the heuristic plays both, which measures the list itself
    The three arms share each cell's decks and seeds, so they differ only in who plays."""
    heuristic = {"kind": "heuristic"}
    out = []
    for block, names, base, count in (("known", training, KNOWN_SEED, KNOWN_COUNT), ("held", held, HELD_SEED, HELD_COUNT)):
        for i, x in enumerate(names):
            for j, o in enumerate(training):
                seed = base + (len(training) * i + j) * count
                for arm in ARMS:
                    for k in (OPTIMIZERS if arm != "mirror" else ("heuristic",)):
                        player = nets[k] if arm != "mirror" else heuristic
                        for seat in SEATS:
                            ours = o if arm == "face" else x
                            theirs = x if arm == "face" else o
                            d0, d1 = (ours, theirs) if seat == 0 else (theirs, ours)
                            out.append({"block": block, "arm": arm, "policy": k, "list": x, "opponent": o, "deck0": d0, "deck1": d1, "seat": seat,
                                        "seed": seed, "games": count, "policies": [player, heuristic] if seat == 0 else [heuristic, player]})
    return out


def chunks(jobs, training):
    """Known lists one at a time, then held-out lists eight at a time: about 9,400 games each."""
    groups, order = {}, []
    for index, job in enumerate(jobs):
        key = (job["block"], job["list"])
        if key not in groups:
            groups[key] = []
            order.append(key)
        groups[key].append(index)
    out, current, size = [], [], 0
    for block, name in order:
        current += groups[block, name]
        size += 1
        if block == "known" or size == CHUNK_LISTS:
            out.append(current)
            current, size = [], 0
    if current:
        out.append(current)
    return out


def job_text(job):
    return json.dumps({k: job[k] for k in sorted(job) if k != "policies"} | {"policies": [{k: p[k] for k in sorted(p) if k != "checkpoint"} for p in job["policies"]]}, sort_keys=True)


def matrix():
    lists = json.loads(LISTS.read_text())
    held = [r["name"] for r in lists["lists"]]
    training = lists["training_lists"]
    return lists, held, training, cells(held, training, net_specs())


# ---------------------------------------------------------------- the rules

def wilson_low(wins, games, z=1.96):
    if not games:
        return 0.0
    p = wins / games
    return (p + z * z / (2 * games) - z * math.sqrt(p * (1 - p) / games + z * z / (4 * games * games))) / (1 + z * z / games)


def tally(rows):
    """(block, arm, policy, list, seat) -> [wins, games, void games, draws]. Draws are not wins."""
    table = {}
    for row in rows:
        t = table.setdefault((row["block"], row["arm"], row["policy"], row["list"], row["seat"]), [0, 0, 0, 0])
        if row["status"] != "PASS":
            t[2] += row["games"]
            continue
        for duel in row["duels"]:
            t[0] += duel["winner"] == row["seat"]
            t[1] += 1
            t[3] += duel["winner"] == 2
    return table


def pooled(table, block, arm, policies, names, seats=SEATS):
    wins = games = 0
    for k in policies:
        for x in names:
            for s in seats:
                t = table.get((block, arm, k, x, s))
                if t:
                    wins += t[0]
                    games += t[1]
    return wins, games


def rate(table, block, arm, policies, names, seats=SEATS):
    wins, games = pooled(table, block, arm, policies, names, seats)
    return wins / games if games else None


def arm_rule(table, arm, names, label):
    """A1 and A2: the mean of the optimizers' rates, each optimizer's lower bound, and both seats pooled over optimizers."""
    by = {}
    for k in OPTIMIZERS:
        wins, games = pooled(table, "held", arm, (k,), names)
        by[k] = {"games": games, "rate": wins / games if games else None, "low": wilson_low(wins, games)}
    seats = {s: rate(table, "held", arm, OPTIMIZERS, names, (s,)) for s in SEATS}
    rates = [v["rate"] for v in by.values()]
    mean = sum(rates) / len(rates) if all(r is not None for r in rates) else None
    broken = []
    if mean is None or mean < BAR:
        broken.append(f"{label}: the mean is below {BAR:.0%}")
    for k, v in by.items():
        if v["low"] <= LOW_BAR:
            broken.append(f"{label}: optimizer {k}'s lower bound is not above {LOW_BAR:.0%}")
    for s, r in seats.items():
        if r is None or r < SEAT_BAR:
            broken.append(f"{label}: seat {s} pooled is below {SEAT_BAR:.0%}")
    return {"lists": len(names), "mean": mean, "by_optimizer": by, "by_seat": seats}, broken


def gaps(table, lists):
    """Per held-out list: the network's edge over the heuristic on the list, minus its edge on the list's twin, averaged over optimizers.
    The heuristic's own result on each list takes out how strong the list is, as far as the heuristic can tell."""
    out, raw, missing = {}, {}, []
    for row in lists:
        x, t = row["name"], row["twin"]
        mirror, known_mirror = rate(table, "held", "mirror", ("heuristic",), (x,)), rate(table, "known", "mirror", ("heuristic",), (t,))
        values, plain = [], []
        for k in OPTIMIZERS:
            pilot, known = rate(table, "held", "pilot", (k,), (x,)), rate(table, "known", "pilot", (k,), (t,))
            if None in (pilot, known, mirror, known_mirror):
                break
            values.append((pilot - mirror) - (known - known_mirror))
            plain.append(pilot - known)
        if len(values) != len(OPTIMIZERS):
            missing.append(x)
            continue
        out[x] = sum(values) / len(values)
        raw[x] = sum(plain) / len(plain)
    return out, raw, missing


def bootstrap(values, rng):
    v = np.asarray(values, dtype=float)
    if not len(v):
        return {"lists": 0, "mean": None, "low": None, "high": None}
    samples = v[rng.integers(0, len(v), size=(RESAMPLES, len(v)))].mean(axis=1)
    return {"lists": len(v), "mean": float(v.mean()), "low": float(np.percentile(samples, 2.5)), "high": float(np.percentile(samples, 97.5))}


def decide(table, lists, integrity):
    """The protocol's rules in order. Returns the numbers and the rules broken."""
    rng = np.random.default_rng(BOOT_SEED)
    names = [r["name"] for r in lists]
    far = [r["name"] for r in lists if r["far"]]
    gate_far = len(far) >= MIN_FAR
    numbers, broken = {"lists": len(names), "far_lists": len(far), "far_gated": gate_far}, []
    for arm, rule in (("pilot", "A1"), ("face", "A2")):
        numbers[arm] = {}
        for label, subset, gated in (("all", names, True), ("far", far, gate_far)):
            result, why = arm_rule(table, arm, subset, f"{rule} {arm} on {label}")
            numbers[arm][label] = result
            broken += why if gated else []
    gap, raw, missing = gaps(table, lists)
    numbers["gap"] = {"all": bootstrap([gap[x] for x in names if x in gap], rng), "far": bootstrap([gap[x] for x in far if x in gap], rng)}
    numbers["raw_gap"] = {"all": bootstrap([raw[x] for x in names if x in raw], rng), "far": bootstrap([raw[x] for x in far if x in raw], rng)}
    for label, gated in (("all", True), ("far", gate_far)):
        low = numbers["gap"][label]["low"]
        if gated and (low is None or low <= GAP_BAR):
            broken.append(f"A3 gap on {label}: the lower bound is not above {GAP_BAR * 100:.0f} points")
    voids = {}
    for block in ("known", "held"):
        for arm in ARMS:
            for k in (OPTIMIZERS if arm != "mirror" else ("heuristic",)):
                void = sum(t[2] for key, t in table.items() if key[:3] == (block, arm, k))
                games = sum(t[1] for key, t in table.items() if key[:3] == (block, arm, k))
                share = void / (void + games) if void + games else 1.0
                voids[f"{block}/{arm}/{k}"] = {"void": void, "games": games, "share": share}
                if share > VOID_BAR:
                    broken.append(f"A4: {block}/{arm}/{k} has {share:.2%} void, above {VOID_BAR:.0%}")
    numbers["voids"] = voids
    numbers["integrity"] = {**integrity, "lists_without_a_gap": missing}
    for key, value in integrity.items():
        if value:
            broken.append(f"A4: {key} is {value}")
    if missing:
        broken.append(f"A4: {len(missing)} lists have no game in some arm")
    return numbers, broken


def reported(table, lists, gap, raw):
    """Read, not gated. One list's 168 games per optimizer don't support a claim about that list."""
    out = {"per_list": {}, "by_band": {}, "by_archetype": {}, "known_lists": {}}
    rng = np.random.default_rng(BOOT_SEED + 1)
    for row in lists:
        x = row["name"]
        out["per_list"][x] = {"archetype": row["archetype"], "twin": row["twin"], "distance": row["distance"],
                              "pilot": rate(table, "held", "pilot", OPTIMIZERS, (x,)), "face": rate(table, "held", "face", OPTIMIZERS, (x,)),
                              "mirror": rate(table, "held", "mirror", ("heuristic",), (x,)), "gap": gap.get(x), "raw_gap": raw.get(x)}
    for b in ("1-9", "10-19", "20+"):
        subset = [r["name"] for r in lists if r["band"] == b]
        out["by_band"][b] = {"pilot": rate(table, "held", "pilot", OPTIMIZERS, subset), "gap": bootstrap([gap[x] for x in subset if x in gap], rng)}
    archetypes = sorted({r["archetype"] for r in lists})
    for a in archetypes:
        subset = [r["name"] for r in lists if r["archetype"] == a]
        out["by_archetype"][a] = {"lists": len(subset), "pilot": rate(table, "held", "pilot", OPTIMIZERS, subset),
                                  "face": rate(table, "held", "face", OPTIMIZERS, subset), "gap": float(np.mean([gap[x] for x in subset if x in gap])) if subset else None}
    pilots = [out["by_archetype"][a]["pilot"] for a in archetypes if out["by_archetype"][a]["pilot"] is not None]
    out["pilot_each_archetype_equal"] = float(np.mean(pilots)) if pilots else None
    training = sorted({key[3] for key in table if key[0] == "known"})
    for t in training:
        out["known_lists"][t] = {"pilot": rate(table, "known", "pilot", OPTIMIZERS, (t,)), "face": rate(table, "known", "face", OPTIMIZERS, (t,)),
                                 "mirror": rate(table, "known", "mirror", ("heuristic",), (t,))}
    for block in ("known", "held"):
        names = sorted({key[3] for key in table if key[0] == block})
        out[f"{block}_pilot"] = {k: rate(table, block, "pilot", (k,), names) for k in OPTIMIZERS}
        out[f"{block}_mirror_seat0"] = rate(table, block, "mirror", ("heuristic",), names, (0,))
    out["draws"] = sum(t[3] for t in table.values())
    return out


# ---------------------------------------------------------------- selftest

def made_up(held_rows, training, rates, void=None, seed=1):
    """Rows in the real format, from the real job builder, with wins drawn at the given rates.
    rates(block, arm, policy, list, seat) -> the chance that the counted seat wins."""
    fake = {k: {"kind": "net", "sha256": k, "greedy": True, "repeat_guard": True} for k in OPTIMIZERS}
    rng = np.random.default_rng(seed)
    rows = []
    for job in cells([r["name"] for r in held_rows], training, fake):
        p = rates(job["block"], job["arm"], job["policy"], job["list"], job["seat"])
        if void and void(job):
            rows.append({**job, "status": "VOID", "reason": "made up"})
            continue
        wins = rng.random(job["games"]) < p
        rows.append({**job, "status": "PASS", "duels": [{"winner": job["seat"] if w else 1 - job["seat"]} for w in wins]})
    return rows


def selftest():
    training = [f"t{i:02d}" for i in range(21)]
    held = [{"name": f"h{i:03d}", "twin": training[i % 21], "far": i % 4 == 0, "archetype": f"a{i % 5}", "band": "20+" if i % 4 == 0 else "1-9", "distance": 20 if i % 4 == 0 else 5}
            for i in range(120)]
    clean = {"missing_chunks": 0, "replay_mismatches": 0, "job_mismatches": 0}
    small = [dict(r, far=r["name"] in ("h000", "h004")) for r in held]

    def make(pilot=0.75, face=0.75, mirror=0.5, known=0.8, known_mirror=0.5, far_pilot=None, seat1=None, opt=None, rows=held):
        far = {r["name"] for r in rows if r["far"]}

        def rates(block, arm, k, x, s):
            if arm == "mirror":
                return known_mirror if block == "known" else mirror
            if block == "known":
                return known if arm == "pilot" else 0.8
            p = pilot if arm == "pilot" else face
            if arm == "pilot" and far_pilot is not None and x in far:
                p = far_pilot
            if s == 1 and seat1 is not None and arm == "pilot":
                p = seat1
            if opt and k == opt[0] and arm == "pilot":
                p = opt[1]
            return p
        return rates
    cases = [("strong everywhere", make(), None, clean, held, None),
             ("pilot mean below the bar", make(pilot=0.53, known=0.53), None, clean, held, "A1 pilot on all: the mean"),
             ("one optimizer weak", make(opt=("1703", 0.49)), None, clean, held, "A1 pilot on all: optimizer 1703"),
             ("one seat weak", make(seat1=0.45), None, clean, held, "A1 pilot on all: seat 1"),
             ("face weak", make(face=0.5), None, clean, held, "A2 face on all"),
             ("held-out far below known", make(pilot=0.60, known=0.85), None, clean, held, "A3 gap on all"),
             ("only far lists fall behind", make(far_pilot=0.58), None, clean, held, "A3 gap on far"),
             ("a weaker list, for everyone", make(pilot=0.62, mirror=0.37, known=0.8, known_mirror=0.55), None, clean, held, None),
             ("too many voids in one arm", make(), lambda j: j["arm"] == "face" and j["policy"] == "1702" and j["seed"] % 50 < 2 and j["block"] == "held", clean, held, "A4: held/face/1702"),
             ("a replay mismatch", make(), None, {**clean, "replay_mismatches": 1}, held, "A4: replay_mismatches"),
             ("a chunk missing", make(), None, {**clean, "missing_chunks": 3}, held, "A4: missing_chunks"),
             ("far set too small to gate", make(far_pilot=0.3, rows=small), None, clean, small, None)]
    failed = 0
    for name, rates, void, integrity, rows_in, expected in cases:
        table = tally(made_up(rows_in, training, rates, void))
        numbers, broken = decide(table, rows_in, integrity)
        ok = not broken if expected is None else any(b.startswith(expected) for b in broken)
        print(f"{'pass' if ok else 'FAIL'}: {name}: {broken[:3]}")
        failed += not ok
    # The weaker-list case passes only because the mirror takes out the list's strength: the raw gap is far below the bar.
    table = tally(made_up(held, training, make(pilot=0.62, mirror=0.37, known=0.8, known_mirror=0.55)))
    numbers, _ = decide(table, held, clean)
    ok = numbers["raw_gap"]["all"]["mean"] < -0.15 and abs(numbers["gap"]["all"]["mean"]) < 0.03
    print(f"{'pass' if ok else 'FAIL'}: the mirror takes out a list's strength: raw {numbers['raw_gap']['all']['mean']:.3f}, gap {numbers['gap']['all']['mean']:.3f}")
    failed += not ok
    # A draw is not a win, and a void cell adds no game.
    rows = [{"block": "held", "arm": "pilot", "policy": "1702", "list": "h", "seat": 1, "games": 3, "status": "PASS", "duels": [{"winner": 1}, {"winner": 2}, {"winner": 0}]},
            {"block": "held", "arm": "pilot", "policy": "1702", "list": "h", "seat": 1, "games": 4, "status": "VOID"}]
    t = tally(rows)[("held", "pilot", "1702", "h", 1)]
    ok = t == [1, 3, 4, 1]
    print(f"{'pass' if ok else 'FAIL'}: draws and voids: {t}")
    failed += not ok
    failed += selftest_matrix(held, training)
    return 1 if failed else 0


def used_ranges():
    """Every env seed range the corpus soak recorded, so a test seed can't repeat one."""
    found = []

    def walk(v):
        if isinstance(v, list) and len(v) == 2 and all(isinstance(x, int) for x in v):
            found.append(tuple(v))
        elif isinstance(v, list):
            for x in v:
                walk(x)
        elif isinstance(v, dict):
            for x in v.values():
                walk(x)
    seeds = json.loads(CORPUS_SUMMARY.read_text())["seeds"]
    walk({k: v for k, v in seeds.items() if k != "block"})
    return found


def selftest_matrix(held, training):
    fake = {k: {"kind": "net", "sha256": k, "greedy": True, "repeat_guard": True} for k in OPTIMIZERS}
    names = [r["name"] for r in held]
    jobs = cells(names, training, fake)
    failed = 0

    def check(name, ok, detail=""):
        nonlocal failed
        print(f"{'pass' if ok else 'FAIL'}: {name} {detail}")
        failed += not ok
    per = len(training) * len(SEATS) * (2 * len(OPTIMIZERS) + 1)
    check("cell count", len(jobs) == per * (len(names) + len(training)), str(len(jobs)))
    key = lambda j: (j["block"], j["list"], j["opponent"], j["deck0"], j["deck1"], j["seed"])
    pilot = {key(j) for j in jobs if j["arm"] == "pilot"}
    face = {key(j) for j in jobs if j["arm"] == "face"}
    mirror = {key(j) for j in jobs if j["arm"] == "mirror"}
    check("pilot and mirror share decks and seeds", pilot == mirror)
    check("face uses the same games with the other seat's deck", face == pilot)
    ok = all((j["deck0"] if j["seat"] == 0 else j["deck1"]) == (j["opponent"] if j["arm"] == "face" else j["list"]) for j in jobs)
    check("the counted seat holds the right deck", ok)
    ok = all(j["policies"][j["seat"]] == fake.get(j["policy"], {"kind": "heuristic"}) and j["policies"][1 - j["seat"]] == {"kind": "heuristic"} for j in jobs)
    check("the counted seat holds the right policy", ok)
    blocks = {}
    for j in jobs:
        blocks.setdefault((j["block"], j["list"], j["opponent"]), set()).add(j["seed"])
    check("one seed block per pair", all(len(s) == 1 for s in blocks.values()))
    spans = sorted((j["seed"], j["seed"] + j["games"] - 1) for j in jobs)
    distinct = sorted(set(spans))
    check("blocks don't overlap", all(a[1] < b[0] for a, b in zip(distinct, distinct[1:])))
    real = cells([f"x{i}" for i in range(595)], training, fake)
    check("595 lists still fit the held-out block", max(j["seed"] + j["games"] - 1 for j in real if j["block"] == "held") <= HELD_END)
    check("the known block fits", max(j["seed"] + j["games"] - 1 for j in real if j["block"] == "known") <= KNOWN_END)
    used = used_ranges()
    lo, hi = min(s[0] for s in spans), max(s[1] for s in spans)
    check("no seed the corpus soak used", all(hi < a or b < lo for a, b in used) and HELD_SEED > max(b for a, b in used if a < HELD_SEED), str(len(used)))
    pairs, search, plain = search_jobs([f"x{i}" for i in range(542)], training)
    groups = search_groups(pairs, training)
    covered = all(any(d in g and o in g for g in groups) for d, o in pairs)
    once = Counter(d for g in groups for d in g if d.startswith("x")) == Counter({d: 1 for d, _ in pairs})
    check("search runs hold 21 lists, each held-out list once, with its opponents", all(len(set(g)) == 21 for g in groups) and covered and once, str(len(groups)))
    check("search games pair with plain games", [j[1:5] for j in search] == [j[1:4] + [j[4]] for j in search] and [j[1:4] for j in search] == [j[1:4] for j in plain]
          and all(p[4] == -1 for p in plain) and len({j[3] for j in search}) == len(pairs))
    sizes = [len(c) for c in chunks(jobs, training)]
    check("chunks cover every cell once", sorted(i for c in chunks(jobs, training) for i in c) == list(range(len(jobs))), str(sizes[:3]))
    return failed


# ---------------------------------------------------------------- running

def inputs(jobs):
    extra = json.loads(EXTRA.read_text())
    registry = deck_registry.registry()
    lua = hashlib.sha256()
    for path in sorted((ROOT / "scripts/script").resolve().glob("*.lua")):
        lua.update(path.name.encode() + b"\0" + path.read_bytes())
    return {"native": sha(NATIVE), "database": sha(deck_registry.DB), "lua_scripts": lua.hexdigest(),
            "files": {p: sha(ROOT / p) for p in DEPENDS},
            "checkpoints": {k: sha(RUNS / f"gauntlet-{k}/checkpoint-2560.msgpack") for k in OPTIMIZERS},
            "decks": {name: sha(path) for name, path in sorted({**registry, **extra}.items())},
            "jobs": text_sha("\n".join(job_text(j) for j in jobs)), "cells": len(jobs), "games": sum(j["games"] for j in jobs)}


def freeze():
    if sha(NATIVE) != NATIVE_SHA:
        raise SystemExit("the native module is not the certified one")
    again = select(write=False)
    if json.loads(LISTS.read_text()) != json.loads(json.dumps(again)):
        raise SystemExit("stage-a-lists.json is not what select gives now")
    write_decks()
    _, _, _, jobs = matrix()
    _, held, training, _ = matrix()
    if sha(SEARCH_BIN) != SEARCH_BIN_SHA:
        raise SystemExit("the search binary is not M2's")
    manifest = {"inputs": inputs(jobs), "search": search_inputs(held, training), "constants": {"held_seed": HELD_SEED, "held_count": HELD_COUNT, "known_seed": KNOWN_SEED, "known_count": KNOWN_COUNT,
                                                       "far": FAR, "bar": BAR, "low_bar": LOW_BAR, "seat_bar": SEAT_BAR, "gap_bar": GAP_BAR, "void_bar": VOID_BAR,
                                                       "min_far": MIN_FAR, "resamples": RESAMPLES, "boot_seed": BOOT_SEED, "replay_every": REPLAY_EVERY}}
    MANIFEST.write_text(json.dumps(manifest, indent=1) + "\n")
    print(json.dumps({k: v for k, v in manifest["inputs"].items() if k not in ("decks", "files")}, indent=1))


def check_frozen(jobs):
    frozen = json.loads(MANIFEST.read_text())["inputs"]
    now = inputs(jobs)
    changed = [k for k in now if now[k] != frozen[k]]
    if changed:
        raise SystemExit(f"inputs differ from the manifest: {changed}")
    return now


def play(workers):
    from actor import Pool
    write_decks()
    _, held, training, jobs = matrix()
    check_frozen(jobs)
    os.environ["EDISON_EXTRA_DECKS"] = str(EXTRA)
    OUT.mkdir(parents=True, exist_ok=True)
    groups = chunks(jobs, training)
    pool = Pool(workers, OUT / "logs", cell_timeout=1800)
    try:
        for n, indices in enumerate(groups):
            path = OUT / f"chunk-{n:03d}.json.gz"
            part = [jobs[i] for i in indices]
            digest = text_sha("\n".join(job_text(j) for j in part))
            if path.exists():
                with gzip.open(path, "rt") as f:
                    if json.load(f)["jobs_sha256"] != digest:
                        raise SystemExit(f"{path} holds other jobs")
                continue
            started = time.perf_counter()
            results = pool.map([{k: v for k, v in j.items() if k not in ("block", "arm", "policy", "list", "opponent")} for j in part])
            rows = [{**{k: v for k, v in j.items() if k != "policies"}, **r} for j, r in zip(part, results)]
            picks = [p for p, i in enumerate(indices) if i % REPLAY_EVERY == 0]
            again = pool.map([{**{k: v for k, v in part[p].items() if k not in ("block", "arm", "policy", "list", "opponent")}, "threads": 4} for p in picks])
            replays = [{"cell": indices[p], "same": a.get("status") == rows[p]["status"] and a.get("duels") == rows[p].get("duels")} for p, a in zip(picks, again)]
            record = {"chunk": n, "jobs_sha256": digest, "rows": rows, "replays": replays, "elapsed": time.perf_counter() - started}
            scratch = path.with_suffix(".tmp")
            with gzip.open(scratch, "wt") as f:
                json.dump(record, f)
            os.replace(scratch, path)
            void = sum(r["games"] for r in rows if r["status"] != "PASS")
            print(json.dumps({"chunk": n, "of": len(groups), "cells": len(rows), "void_games": void, "replays": len(replays),
                              "mismatches": sum(not r["same"] for r in replays), "seconds": round(record["elapsed"])}), flush=True)
            if not all(r["same"] for r in replays):
                raise SystemExit("replay mismatch: results are not reproducible")
    finally:
        pool.close()


def load_results(jobs, training):
    rows, mismatches, missing, wrong = [], 0, 0, 0
    for n, indices in enumerate(chunks(jobs, training)):
        path = OUT / f"chunk-{n:03d}.json.gz"
        if not path.exists():
            missing += 1
            continue
        with gzip.open(path, "rt") as f:
            record = json.load(f)
        part = [jobs[i] for i in indices]
        if record["jobs_sha256"] != text_sha("\n".join(job_text(j) for j in part)) or len(record["rows"]) != len(part):
            wrong += 1
            continue
        rows += record["rows"]
        mismatches += sum(not r["same"] for r in record["replays"])
    return rows, {"missing_chunks": missing, "replay_mismatches": mismatches, "job_mismatches": wrong}


def analyze():
    lists, held, training, jobs = matrix()
    check_frozen(jobs)
    rows, integrity = load_results(jobs, training)
    table = tally(rows)
    numbers, broken = decide(table, lists["lists"], integrity)
    gap, raw, _ = gaps(table, lists["lists"])
    reasons = Counter(str(r.get("reason"))[:120] for r in rows if r["status"] != "PASS")
    passing = [r for r in rows if r["status"] == "PASS"]
    result = {"verdict": "STAGE_A_MET" if not broken else "NOT_MET", "broken": broken, "numbers": numbers, "reported": reported(table, lists["lists"], gap, raw),
              "void_reasons": dict(reasons), "decisions": sum(r["counters"]["decisions"] for r in passing),
              "guard_fires": sum(r["counters"].get("guard_fires", 0) for r in passing), "manifest_sha256": sha(MANIFEST)}
    RESULT.write_text(json.dumps(result, indent=1) + "\n")
    print(json.dumps({k: result[k] for k in ("verdict", "broken")}, indent=1))
    return 0 if not broken else 1


def livecheck(workers):
    """Training lists only, with every held-out deck loaded, so a deck the env refuses stops this before any test game."""
    from actor import Pool
    write_decks()
    os.environ["EDISON_EXTRA_DECKS"] = str(EXTRA)
    training = sorted(deck_registry.registry())
    nets = net_specs()
    heuristic = {"kind": "heuristic"}
    jobs = [{"deck0": training[0], "deck1": training[0], "seat": 0, "seed": LIVE_SEED, "games": 2, "policies": [nets["1702"], heuristic]},
            {"deck0": training[1], "deck1": training[2], "seat": 1, "seed": LIVE_SEED + 2, "games": 2, "policies": [heuristic, nets["1703"]]},
            {"deck0": training[3], "deck1": training[4], "seat": 0, "seed": LIVE_SEED + 4, "games": 2, "policies": [heuristic, heuristic]}]
    pool = Pool(workers, BUILD / "livecheck-logs", cell_timeout=600)
    try:
        first = pool.map(jobs)
        again = pool.map([{**j, "threads": 4} for j in jobs])
    finally:
        pool.close()
    ok = all(r["status"] == "PASS" for r in first) and all(a.get("duels") == r.get("duels") for a, r in zip(again, first))
    print(json.dumps({"loaded_held_out_decks": len(json.loads(EXTRA.read_text())), "statuses": [r["status"] for r in first],
                      "reasons": [r.get("reason") for r in first], "replays_same": ok}, indent=1))
    return 0 if ok else 1


# ---------------------------------------------------------------- reported search check

def search_jobs(held, training):
    """60 held-out lists, each against 4 training lists, drawn once. The search side plays the held-out list in both seats, the net rotating by pair."""
    rng = random.Random(SEARCH_DRAW)
    picked = rng.sample(sorted(held), SEARCH_LISTS)
    pairs = [(d, o) for d in picked for o in rng.sample(sorted(training), SEARCH_OPPONENTS)]
    nets = ("n1702", "n1703", "n1704")
    budget, depth, candidates, z = SEARCH_SETTING
    search, plain = [], []
    for p, (d, o) in enumerate(pairs):
        for seat in SEATS:
            d0, d1 = (d, o) if seat == 0 else (o, d)
            fields = [d0, d1, SEARCH_SEED + p, seat, nets[p % 3]]
            search.append([f"g{p}_{seat}", *fields, budget, depth, candidates, z, 1, 5000 + 2 * p + seat, 0])
            plain.append([f"p{p}_{seat}", d0, d1, SEARCH_SEED + p, -1, nets[p % 3]])
    return pairs, search, plain


def search_groups(pairs, training):
    """The search binary loads exactly 21 lists. Each run holds some held-out lists, in draw order, and the training lists they meet,
    filled to 21 from the other training lists in sorted order."""
    meets = {}
    for d, o in pairs:
        meets.setdefault(d, set()).add(o)
    groups, held_in, needed = [], [], set()
    for d in meets:
        trial = needed | meets[d]
        if held_in and len(held_in) + 1 + len(trial) > 21:
            groups.append((held_in, needed))
            held_in, trial = [], set(meets[d])
        held_in, needed = held_in + [d], trial
    groups.append((held_in, needed))
    out = []
    for held_in, needed in groups:
        fill = [t for t in sorted(training) if t not in needed][:21 - len(held_in) - len(needed)]
        out.append(held_in + sorted(needed) + fill)
    return out


def search_inputs(held, training):
    pairs, search, plain = search_jobs(held, training)
    groups = search_groups(pairs, training)
    return {"binary": sha(SEARCH_BIN), "nets": {k: sha(SEARCH_NETS / f"gauntlet-{k}.ednet") for k in OPTIMIZERS},
            "jobs": text_sha(json.dumps([search, plain])), "groups": groups}


def search_check(workers):
    sys.path.insert(0, str(ROOT / "tools/edison/search"))
    import search_common as sc
    import search_study as study
    lists, held, training, jobs = matrix()
    check_frozen(jobs)
    frozen = json.loads(MANIFEST.read_text())["search"]
    if search_inputs(held, training) != frozen:
        raise SystemExit("the search check's inputs differ from the manifest")
    if sc.BIN != SEARCH_BIN or sc.OUT / "nets" != SEARCH_NETS:
        raise SystemExit("search_common points at another binary or other nets")
    decks = {**deck_registry.registry(), **json.loads(EXTRA.read_text())}
    pairs, search, plain = search_jobs(held, training)
    games = {}
    for n, names in enumerate(frozen["groups"]):
        if len(set(names)) != 21:
            raise SystemExit("a search group does not hold 21 lists")
        sc.REGISTRY = BUILD / f"search-check/registry-{n:02d}.txt"
        sc.REGISTRY.parent.mkdir(parents=True, exist_ok=True)
        sc.REGISTRY.write_text("".join(f"{k} {Path(decks[k]).resolve()}\n" for k in sorted(names)))
        mine = [j for j in search + plain if pairs[int(j[0][1:].split("_")[0])][0] in names]
        games.update(sc.play([sc.job_line(*j) for j in mine], BUILD / f"search-check/group-{n:02d}", workers))
    rows = []
    for p in range(len(pairs)):
        for seat in SEATS:
            g, b = games.get(f"g{p}_{seat}"), games.get(f"p{p}_{seat}")
            rows.append((p, seat, study.score(g, seat) if g else None, study.score(b, seat) if b else None))
    kept = [r for r in rows if r[2] is not None and r[3] is not None]
    per_pair = {}
    for p, seat, a, b in kept:
        per_pair.setdefault(p, []).append(a - b)
    means = np.array([np.mean(v) for _, v in sorted(per_pair.items())])
    rng = np.random.default_rng(SEARCH_DRAW)
    samples = means[rng.integers(0, len(means), size=(RESAMPLES, len(means)))].mean(axis=1)
    searched = {k: v for k, v in games.items() if k.startswith("g")}
    far = {r["name"] for r in lists["lists"] if r["far"]}
    on_far = [r[2] - r[3] for r in kept if pairs[r[0]][0] in far]
    result = {"pairs": len(pairs), "comparisons": len(rows), "kept": len(kept), "search": float(np.mean([r[2] for r in kept])), "plain": float(np.mean([r[3] for r in kept])),
              "difference": float(means.mean()), "low": float(np.percentile(samples, 2.5)), "high": float(np.percentile(samples, 97.5)),
              "by_seat": {seat: float(np.mean([r[2] - r[3] for r in kept if r[1] == seat])) for seat in SEATS},
              "far_lists": float(np.mean(on_far)) if on_far else None,
              "summary": study.summarise(searched)}
    (BUILD / "search-check-result.json").write_text(json.dumps(result, indent=1) + "\n")
    print(json.dumps({k: v for k, v in result.items() if k != "summary"}, indent=1))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("select")
    commands.add_parser("selftest")
    for name in ("livecheck", "play", "search"):
        p = commands.add_parser(name)
        p.add_argument("--workers", type=int, default=14)
    commands.add_parser("freeze")
    commands.add_parser("analyze")
    args = parser.parse_args()
    if args.command == "select":
        out = select()
        print(json.dumps({k: v for k, v in out.items() if k != "lists"}, indent=1))
    elif args.command == "selftest":
        sys.exit(selftest())
    elif args.command == "livecheck":
        sys.exit(livecheck(args.workers))
    elif args.command == "freeze":
        freeze()
    elif args.command == "play":
        play(args.workers)
    elif args.command == "analyze":
        sys.exit(analyze())
    elif args.command == "search":
        search_check(args.workers)


if __name__ == "__main__":
    main()
