#!/usr/bin/env python3
"""M3 Stage A: the round 2560 networks on held-out builds of cards they trained on.

docs/edison-m3-protocol.md is the plan, and this script is its exact form.

  select     the held-out builds from the corpus, into stage-a-lists.json, and the decks the env loads
  selftest   the rules, the result checks and the journal on made-up data with known answers, and the matrix's shape
  livecheck  a few cells on training lists only, with every held-out deck loaded; plays no corpus list
  freeze     records every input in m3a-manifest.json, to be committed alone on top of the commit it names
  play       the cells, journaled as they finish; refuses to start unless HEAD is the manifest's commit
  analyze    the verdict: exit 0 on a pass, 1 on a fail
  search     the reported search check, after analyze
"""
import argparse
from collections import Counter
import gzip
import hashlib
import json
import os
from pathlib import Path
import random
import subprocess
import sys
import threading
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
SEARCH_OUT = BUILD / "search-check"
SEARCH_RESULT = BUILD / "search-check-result.json"
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
# Columns of the result arrays: pilot by optimizer, face by optimizer, then the mirror.
COLUMNS = [("pilot", k) for k in OPTIMIZERS] + [("face", k) for k in OPTIMIZERS] + [("mirror", "heuristic")]
MIRROR = len(COLUMNS) - 1

HELD_SEED, HELD_COUNT = 88130000, 4      # one block of 4 per (held-out build, training list)
KNOWN_SEED, KNOWN_COUNT = 88180000, 32   # one block of 32 per (training list, training list)
HELD_END, KNOWN_END = 88179999, 88199999
LIVE_SEED = 88410000
FAR = 20
BAR, LOW_BAR, SEAT_BAR, GAP_BAR, VOID_BAR, MIN_FAR = 0.55, 0.50, 0.50, -0.10, 0.01, 10
RESAMPLES, BOOT_SEED = 10000, 88810001
REPLAY_EVERY = 127  # odd, so replays fall on both counted seats
CHUNK_LISTS = 8
SEARCH_SEED, SEARCH_DRAW, SEARCH_LISTS, SEARCH_OPPONENTS = 88800000, 88810002, 60, 4
SEARCH_SETTING = (64, 48, 4, 1.0)
SEARCH_BIN = ROOT / "build/forward_model/edison_search"
SEARCH_NETS = ROOT / "build/edison-search/nets"
SEARCH_BIN_SHA = "167be8aa8f9a4767789c3e81dc719051dd41c32afc3678d9cf6172d307a5548f"  # M2's frozen binary
DEPENDS = ("tools/edison/heldout/stage_a.py", "tools/edison/heldout/stage-a-lists.json", "docs/edison-m3-protocol.md",
           "tools/edison/learner/actor.py", "tools/edison/learner/evaluate.py", "tools/edison/learner/decks.py",
           "tools/edison/learner/obs.py", "tools/edison/learner/net.py", "tools/edison/learner/codes_v1.txt",
           "ygoai/edison_policy.py", "assets/edison/legality.json", "assets/edison/corpus/manifest.json",
           "tests/edison/reports/corpus-2026-11-09/summary.json", "tools/edison/search/search_common.py",
           "tools/edison/search/search_study.py")
JOB_FIELDS = ("block", "arm", "policy", "list", "opponent", "deck0", "deck1", "seat", "seed", "games")


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def text_sha(text):
    return hashlib.sha256(text.encode()).hexdigest()


# ---------------------------------------------------------------- selection

def main_extra(zones):
    """Main and Extra after the Gottoms rule, as a multiset of passcodes."""
    return Counter(c for c in zones["main"] + zones["extra"] if c not in deck_registry.OUTSIDE_V1)


def build_key(zones):
    """What a duel plays: Main and Extra after the Gottoms rule, as multisets. The Side never enters a single duel."""
    keep = lambda zone: tuple(sorted(c for c in zones[zone] if c not in deck_registry.OUTSIDE_V1))
    return keep("main"), keep("extra")


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
    known_keys = {build_key(z) for z in training.values()}
    crashed = soak_crashes()
    manifest = json.loads((CORPUS / "manifest.json").read_text())
    candidates, excluded = {}, Counter()
    for row in sorted(manifest["lists"], key=lambda r: int(r["list_id"][2:])):
        zones = deck_registry.parse_ydk((CORPUS / row["ydk"]).read_text())
        me = main_extra(zones)
        inside = all(c in v1 for c in me)
        untrained = sorted((c, n) for c, n in me.items() if c not in trained)
        twin = min(sorted(known), key=lambda n: distance(me, known[n]))
        d = distance(me, known[twin])
        # The corpus field means legal and inside v1. Legality itself is checked again when every worker loads each deck.
        if (row["production_legal"] and inside) != row["inside_v1_main_extra"] or untrained != sorted((u["code"], u["copies"]) for u in row["untrained_main_extra"]) \
                or (twin, d) != (row["nearest_training"]["name"], row["nearest_training"]["distance"]):
            raise SystemExit(f"{row['list_id']}: the corpus manifest disagrees with this count")
        key = build_key(zones)
        if not row["production_legal"]:
            excluded["not production legal"] += 1
        elif row["in_training"]:
            excluded["a training list"] += 1
        elif not inside:
            excluded["a Main or Extra card outside code list v1"] += 1
        elif untrained:
            excluded["a Main or Extra card that no training list plays"] += 1
        elif d == 0 or key in known_keys:
            excluded["a training list's exact Main and Extra"] += 1
        elif row["list_id"] in crashed:
            excluded["crashed in the soak's Main and Extra stage"] += 1
        elif key in candidates:
            excluded["the same Main and Extra as a lower-numbered held-out list"] += 1
            candidates[key]["variants"].append(row["list_id"])
            candidates[key]["archetypes"] = sorted(set(candidates[key]["archetypes"]) | {row["archetype"]})
        else:
            candidates[key] = {"name": f"h_{row['list_id']}", "list_id": row["list_id"], "variants": [row["list_id"]], "source": row["ydk"],
                               "source_sha256": sha(CORPUS / row["ydk"]), "archetype": row["archetype"], "archetypes": [row["archetype"]],
                               "twin": twin, "distance": d, "band": band(d), "far": d >= FAR,
                               "gottoms_removed": sum(zones["extra"].count(c) + zones["main"].count(c) for c in deck_registry.OUTSIDE_V1),
                               "main": len(key[0]), "extra": len(key[1])}
    chosen = sorted(candidates.values(), key=lambda r: r["name"])
    if HELD_SEED + len(chosen) * len(known) * HELD_COUNT - 1 > HELD_END:
        raise SystemExit("the held-out seed block is too small for this many builds")
    out = {"rule": "production legal, not a training list, every Main and Extra card in code list v1 after the Gottoms rule and played by some training list's Main or Extra, "
                   "Main and Extra not identical to a training list's, no crash in the soak's Main and Extra stage, and one entry per distinct Main and Extra",
           "corpus_manifest_sha256": sha(CORPUS / "manifest.json"), "code_list_v1_sha256": codes_sha, "training_lists": sorted(known),
           "far_distance": FAR, "corpus_lists": len(manifest["lists"]), "excluded": dict(sorted(excluded.items())),
           "held_out_lists": sum(len(r["variants"]) for r in chosen), "held_out": len(chosen), "far": sum(r["far"] for r in chosen),
           "by_band": dict(sorted(Counter(r["band"] for r in chosen).items())),
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


def chunks(jobs):
    """Known lists one at a time, then held-out builds eight at a time: about 9,400 games each."""
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


def replayed(indices):
    return [i for i in indices if i % REPLAY_EVERY == 0]


def job_text(job):
    return json.dumps({k: job[k] for k in sorted(job) if k != "policies"} | {"policies": [{k: p[k] for k in sorted(p) if k != "checkpoint"} for p in job["policies"]]}, sort_keys=True)


def part_sha(part):
    return text_sha("\n".join(job_text(j) for j in part))


def matrix():
    lists = json.loads(LISTS.read_text())
    held = [r["name"] for r in lists["lists"]]
    training = lists["training_lists"]
    return lists, held, training, cells(held, training, net_specs())


def worker_job(job, **extra):
    return {**{k: v for k, v in job.items() if k not in ("block", "arm", "policy", "list", "opponent")}, **extra}


# ---------------------------------------------------------------- journal and result checks

def read_journal(path):
    """Results by key, and keys started with no result. Only the last line may be cut short, by an interrupted write."""
    results, started = {}, Counter()
    if not path.exists():
        return results, started
    lines = path.read_text().split("\n")
    for n, line in enumerate(lines):
        if not line:
            continue
        try:
            entry = json.loads(line)
        except ValueError:
            if n == len(lines) - 1 or (n == len(lines) - 2 and not lines[-1]):
                break
            raise SystemExit(f"{path} is damaged at line {n + 1}")
        if entry["event"] == "start":
            started[entry["key"]] += 1
        elif entry["event"] == "result" and entry["key"] not in results:
            results[entry["key"]] = entry["result"]
    unresolved = Counter({k: n - (k in results) for k, n in started.items() if n - (k in results) > 0})
    return results, unresolved


def run_tasks(pool, tasks, journal_path, done):
    """Run the (key, job) tasks with no result in done, journaling each start and each result as it happens.
    A result written once is never replaced, so a failure survives any later attempt."""
    queue = [t for t in tasks if t[0] not in done]
    lock = threading.Lock()
    with journal_path.open("a") as journal:
        def write(entry):
            journal.write(json.dumps(entry) + "\n")
            journal.flush()

        def loop(worker):
            while True:
                with lock:
                    if not queue:
                        return
                    key, job = queue.pop(0)
                    write({"event": "start", "key": key})
                result = worker.run(job, pool.cell_timeout)
                with lock:
                    write({"event": "result", "key": key, "result": result})
                    done[key] = result
        threads = [threading.Thread(target=loop, args=(w,)) for w in pool.workers]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()


def assemble(n, part, indices, results, unresolved):
    rows = []
    for job, i in zip(part, indices):
        r = results[f"first:{i}"]
        rows.append({"cell": i, "job_sha256": text_sha(job_text(job)), **{k: job[k] for k in JOB_FIELDS}, "status": r.get("status"),
                     "duels": r.get("duels"), "reason": r.get("reason"), "counters": r.get("counters"), "unresolved_starts": unresolved.get(f"first:{i}", 0)})
    replays = []
    for i in replayed(indices):
        r = results[f"replay:{i}"]
        replays.append({"cell": i, "status": r.get("status"), "duels": r.get("duels"), "unresolved_starts": unresolved.get(f"replay:{i}", 0)})
    return {"chunk": n, "jobs_sha256": part_sha(part), "rows": rows, "replays": replays}


def validate(record, part, indices):
    """Every problem with a stored chunk, checked against the jobs it should hold. Replays are compared again here."""
    problems = []
    if record.get("jobs_sha256") != part_sha(part):
        problems.append("job digest")
    rows = record.get("rows", [])
    if len(rows) != len(part):
        return problems + [f"{len(rows)} rows for {len(part)} jobs"], 0
    by_cell = {}
    for job, i, row in zip(part, indices, rows):
        by_cell[i] = row
        if row.get("cell") != i or row.get("job_sha256") != text_sha(job_text(job)) or any(row.get(k) != job[k] for k in JOB_FIELDS):
            problems.append(f"cell {i}: another job's row")
            continue
        if row["status"] == "PASS":
            duels = row.get("duels") or []
            if [d.get("seed") for d in duels] != list(range(job["seed"], job["seed"] + job["games"])):
                problems.append(f"cell {i}: duels don't cover its seeds")
            if any(d.get("winner") not in (0, 1, 2) for d in duels):
                problems.append(f"cell {i}: a winner outside 0, 1, 2")
        elif row["status"] != "VOID":
            problems.append(f"cell {i}: status {row['status']}")
    replays = record.get("replays", [])
    if [r.get("cell") for r in replays] != replayed(indices):
        problems.append("replays are not the scheduled cells")
    mismatches = sum(1 for r in replays if r.get("cell") in by_cell and (r.get("status") != by_cell[r["cell"]].get("status") or r.get("duels") != by_cell[r["cell"]].get("duels")))
    return problems, mismatches


def load_results(jobs):
    rows, integrity = [], Counter(missing_chunks=0, invalid_chunks=0, replay_mismatches=0)
    for n, indices in enumerate(chunks(jobs)):
        path = OUT / f"chunk-{n:03d}.json.gz"
        if not path.exists():
            integrity["missing_chunks"] += 1
            continue
        with gzip.open(path, "rt") as f:
            record = json.load(f)
        problems, mismatches = validate(record, [jobs[i] for i in indices], indices)
        if problems:
            integrity["invalid_chunks"] += 1
            continue
        integrity["replay_mismatches"] += mismatches
        rows += record["rows"]
    return rows, dict(integrity)


# ---------------------------------------------------------------- the rules

def arrays(rows, held, training):
    """Wins, games, void games and wins in cells with an interrupted earlier attempt, by block, list, (opponent, seat) unit and column."""
    columns = {c: n for n, c in enumerate(COLUMNS)}
    out = {}
    for block, names in (("known", training), ("held", held)):
        out[block] = {key: np.zeros((len(names), 2 * len(training), len(COLUMNS))) for key in ("wins", "games", "void", "suspect")}
    place = {"known": {x: i for i, x in enumerate(training)}, "held": {x: i for i, x in enumerate(held)}}
    opponent = {o: j for j, o in enumerate(training)}
    for row in rows:
        a = out[row["block"]]
        at = (place[row["block"]][row["list"]], 2 * opponent[row["opponent"]] + row["seat"], columns[row["arm"], row["policy"]])
        if row["status"] != "PASS":
            a["void"][at] += row["games"]
            continue
        wins = sum(d["winner"] == row["seat"] for d in row["duels"])  # a draw is not a win
        a["wins"][at] += wins
        a["games"][at] += len(row["duels"])
        if row.get("unresolved_starts"):
            a["suspect"][at] += wins
    return out


def ratio(w, g):
    with np.errstate(invalid="ignore", divide="ignore"):
        return np.where(g > 0, w / np.maximum(g, 1), np.nan)


def gap_of(w, g, wk, gk, twins):
    """Per list: the mean over optimizers of (pilot - mirror) on the list minus the same on its twin. w, g are (lists, columns)."""
    edge = ratio(w[:, :3], g[:, :3]) - ratio(w[:, MIRROR], g[:, MIRROR])[:, None]
    known_edge = ratio(wk[:, :3], gk[:, :3]) - ratio(wk[:, MIRROR], gk[:, MIRROR])[:, None]
    return (edge - known_edge[twins]).mean(axis=1)


def bootstrap(data, subset, twins, rng, resamples):
    """Resample lists, then each drawn list's 42 (opponent, seat) cells, and each training list's cells once per draw,
    shared by every list whose twin it is. All columns of a cell share its seeds, so they move together.
    Returns the pooled pilot and face rates by optimizer, and the mean gap, for each draw."""
    held, known = data["held"], data["known"]
    W, G = held["wins"][subset], held["games"][subset]
    WK, GK = known["wins"], known["games"]
    tw = twins[subset]
    n, units = W.shape[0], W.shape[1]
    rates, gaps = np.empty((resamples, 6)), np.empty(resamples)
    k_rows = np.arange(WK.shape[0])[:, None]
    for r in range(resamples):
        lists = rng.integers(0, n, n)
        cells_ = rng.integers(0, units, (n, units))
        w, g = W[lists[:, None], cells_].sum(axis=1), G[lists[:, None], cells_].sum(axis=1)
        known_cells = rng.integers(0, units, (WK.shape[0], units))
        wk, gk = WK[k_rows, known_cells].sum(axis=1), GK[k_rows, known_cells].sum(axis=1)
        rates[r] = ratio(w[:, :6].sum(axis=0), g[:, :6].sum(axis=0))
        gaps[r] = np.nanmean(gap_of(w, g, wk, gk, tw[lists]))
    return rates, gaps


def decide(data, lists, training, integrity, resamples=RESAMPLES):
    """The protocol's rules in order. Returns the numbers and the rules broken."""
    rng = np.random.default_rng(BOOT_SEED)
    names = [r["name"] for r in lists]
    twins = np.array([training.index(r["twin"]) for r in lists])
    far = np.array([i for i, r in enumerate(lists) if r["far"]], dtype=int)
    everything = np.arange(len(lists))
    gate_far = len(far) >= MIN_FAR
    held, known = data["held"], data["known"]
    numbers, broken = {"lists": len(names), "far_lists": int(len(far)), "far_gated": gate_far}, []
    for label, subset, gated in (("all", everything, True), ("far", far, gate_far)):
        W, G, V, S = (held[key][subset].sum(axis=(0, 1)) for key in ("wins", "games", "void", "suspect"))
        seat_w = held["wins"][subset].reshape(len(subset), -1, 2, len(COLUMNS)).sum(axis=(0, 1))
        seat_g = held["games"][subset].reshape(len(subset), -1, 2, len(COLUMNS)).sum(axis=(0, 1))
        rates, gaps = bootstrap(data, subset, twins, rng, resamples) if len(subset) else (np.full((1, 6), np.nan), np.full(1, np.nan))
        per = {"lists": int(len(subset))}
        for arm, rule, cols in (("pilot", "A1", range(0, 3)), ("face", "A2", range(3, 6))):
            by = {k: {"games": int(G[c]), "rate": float(ratio(W[c], G[c])), "low": float(np.nanpercentile(rates[:, c], 2.5))} for k, c in zip(OPTIMIZERS, cols)}
            mean = float(np.mean([v["rate"] for v in by.values()]))
            worst = float(np.mean([ratio(W[c] - S[c], G[c] + V[c]) for c in cols]))
            seats = {s: float(ratio(seat_w[s, list(cols)].sum(), seat_g[s, list(cols)].sum())) for s in SEATS}
            per[arm] = {"mean": mean, "worst_case_mean": worst, "by_optimizer": by, "by_seat": seats}
            if not gated:
                continue
            if not mean >= BAR:
                broken.append(f"{rule} {arm} on {label}: the mean is below {BAR:.0%}")
            for k, v in by.items():
                if not v["low"] > LOW_BAR:
                    broken.append(f"{rule} {arm} on {label}: optimizer {k}'s lower bound is not above {LOW_BAR:.0%}")
            for s, r in seats.items():
                if not r >= SEAT_BAR:
                    broken.append(f"{rule} {arm} on {label}: seat {s} is below {SEAT_BAR:.0%}")
            if not worst >= BAR:
                broken.append(f"A4 {arm} on {label}: counting void and interrupted games as losses, the mean is below {BAR:.0%}")
        point = gap_of(held["wins"][subset].sum(axis=1), held["games"][subset].sum(axis=1), known["wins"].sum(axis=1), known["games"].sum(axis=1), twins[subset]) if len(subset) else np.array([np.nan])
        raw = (ratio(held["wins"][subset].sum(axis=1)[:, :3], held["games"][subset].sum(axis=1)[:, :3])
               - ratio(known["wins"].sum(axis=1)[:, :3], known["games"].sum(axis=1)[:, :3])[twins[subset]]).mean(axis=1) if len(subset) else np.array([np.nan])
        per["gap"] = {"mean": float(np.nanmean(point)), "low": float(np.nanpercentile(gaps, 2.5)), "high": float(np.nanpercentile(gaps, 97.5))}
        per["raw_gap"] = {"mean": float(np.nanmean(raw))}
        if gated and not per["gap"]["low"] > GAP_BAR:
            broken.append(f"A3 gap on {label}: the lower bound is not above {GAP_BAR * 100:.0f} points")
        numbers[label] = per
    voids = {}
    for block in ("known", "held"):
        for c, (arm, k) in enumerate(COLUMNS):
            void, games = float(data[block]["void"][:, :, c].sum()), float(data[block]["games"][:, :, c].sum())
            share = void / (void + games) if void + games else 1.0
            voids[f"{block}/{arm}/{k}"] = {"void": int(void), "games": int(games), "share": share}
            if share > VOID_BAR:
                broken.append(f"A4: {block}/{arm}/{k} has {share:.2%} void, above {VOID_BAR:.0%}")
    numbers["voids"] = voids
    # Coverage: every list has completed games in every arm, optimizer and seat.
    empty = []
    for block in ("known", "held"):
        per_seat = data[block]["games"].reshape(data[block]["games"].shape[0], -1, 2, len(COLUMNS)).sum(axis=1)
        for i, s, c in zip(*np.nonzero(per_seat == 0)):
            empty.append(f"{block}/{i}/seat {s}/{COLUMNS[c][0]}/{COLUMNS[c][1]}")
    numbers["integrity"] = {**integrity, "empty_list_arms": len(empty)}
    if empty:
        broken.append(f"A4: {len(empty)} list, arm, optimizer and seat combinations have no completed game, such as {empty[0]}")
    for key, value in integrity.items():
        if value:
            broken.append(f"A4: {key} is {value}")
    return numbers, broken


def reported(data, lists, training):
    """Read, not gated. One build's 168 games per optimizer don't support a claim about that build."""
    held, known = data["held"], data["known"]
    w, g = held["wins"].sum(axis=1), held["games"].sum(axis=1)
    wk, gk = known["wins"].sum(axis=1), known["games"].sum(axis=1)
    twins = np.array([training.index(r["twin"]) for r in lists])
    gap = gap_of(w, g, wk, gk, twins)
    raw = (ratio(w[:, :3], g[:, :3]) - ratio(wk[:, :3], gk[:, :3])[twins]).mean(axis=1)
    pilot = ratio(w[:, :3].sum(axis=1), g[:, :3].sum(axis=1))
    face = ratio(w[:, 3:6].sum(axis=1), g[:, 3:6].sum(axis=1))
    mirror = ratio(w[:, MIRROR], g[:, MIRROR])
    out = {"per_list": {r["name"]: {"archetype": r["archetype"], "variants": len(r["variants"]), "twin": r["twin"], "distance": r["distance"],
                                    "pilot": float(pilot[i]), "face": float(face[i]), "mirror": float(mirror[i]), "gap": float(gap[i]), "raw_gap": float(raw[i])}
                        for i, r in enumerate(lists)}}

    def summary(index):
        index = np.asarray(index, dtype=int)
        return {"lists": int(len(index)), "pilot": float(ratio(w[index, :3].sum(), g[index, :3].sum())), "face": float(ratio(w[index, 3:6].sum(), g[index, 3:6].sum())),
                "gap": float(np.nanmean(gap[index])), "raw_gap": float(np.nanmean(raw[index]))} if len(index) else {"lists": 0}
    out["by_band"] = {b: summary([i for i, r in enumerate(lists) if r["band"] == b]) for b in ("1-9", "10-19", "20+")}
    archetypes = sorted({r["archetype"] for r in lists})
    out["by_archetype"] = {a: summary([i for i, r in enumerate(lists) if r["archetype"] == a]) for a in archetypes}
    out["pilot_each_archetype_equal"] = float(np.mean([out["by_archetype"][a]["pilot"] for a in archetypes]))
    out["gap_spread"] = {"p10": float(np.nanpercentile(gap, 10)), "p50": float(np.nanpercentile(gap, 50)), "p90": float(np.nanpercentile(gap, 90))}
    out["raw_gap_spread"] = {"p10": float(np.nanpercentile(raw, 10)), "p50": float(np.nanpercentile(raw, 50)), "p90": float(np.nanpercentile(raw, 90))}
    out["known_lists"] = {t: {"pilot": float(ratio(wk[i, :3].sum(), gk[i, :3].sum())), "face": float(ratio(wk[i, 3:6].sum(), gk[i, 3:6].sum())),
                              "mirror": float(ratio(wk[i, MIRROR], gk[i, MIRROR]))} for i, t in enumerate(training)}
    for block in ("known", "held"):
        a = data[block]
        out[f"{block}_pilot"] = {k: float(ratio(a["wins"][:, :, c].sum(), a["games"][:, :, c].sum())) for c, k in enumerate(OPTIMIZERS)}
        seat0 = a["wins"].reshape(a["wins"].shape[0], -1, 2, len(COLUMNS))[:, :, 0, MIRROR].sum()
        seat0_games = a["games"].reshape(a["games"].shape[0], -1, 2, len(COLUMNS))[:, :, 0, MIRROR].sum()
        out[f"{block}_mirror_seat0"] = float(ratio(seat0, seat0_games))
    return out


# ---------------------------------------------------------------- selftest

FAKE = {k: {"kind": "net", "sha256": k, "greedy": True, "repeat_guard": True} for k in OPTIMIZERS}


def made_up(held_rows, training, rates, void=None, seed=1):
    """Rows in the real format, from the real job builder, with wins drawn at the given rates.
    rates(block, arm, policy, list, seat) -> the chance that the counted seat wins."""
    rng = np.random.default_rng(seed)
    rows = []
    for i, job in enumerate(cells([r["name"] for r in held_rows], training, FAKE)):
        base = {"cell": i, "job_sha256": text_sha(job_text(job)), **{k: job[k] for k in JOB_FIELDS}, "unresolved_starts": 0}
        if void and void(job):
            rows.append({**base, "status": "VOID", "reason": "made up", "duels": None})
            continue
        p = rates(job["block"], job["arm"], job["policy"], job["list"], job["seat"])
        wins = rng.random(job["games"]) < p
        rows.append({**base, "status": "PASS", "duels": [{"seed": job["seed"] + e, "winner": job["seat"] if w else 1 - job["seat"]} for e, w in enumerate(wins)]})
    return rows


def selftest():
    training = [f"t{i:02d}" for i in range(21)]
    held = [{"name": f"h{i:03d}", "twin": training[i % 21], "far": i % 4 == 0, "archetype": f"a{i % 5}", "band": "20+" if i % 4 == 0 else "1-9",
             "distance": 20 if i % 4 == 0 else 5, "variants": ["x"]} for i in range(120)]
    names = [r["name"] for r in held]
    clean = {"missing_chunks": 0, "invalid_chunks": 0, "replay_mismatches": 0}
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
             ("only far builds fall behind", make(far_pilot=0.58), None, clean, held, "A3 gap on far"),
             ("a weaker build, for everyone", make(pilot=0.62, mirror=0.37, known=0.8, known_mirror=0.55), None, clean, held, None),
             ("too many voids in one arm", make(), lambda j: j["arm"] == "face" and j["policy"] == "1702" and j["seed"] % 50 < 2 and j["block"] == "held", clean, held, "A4: held/face/1702"),
             ("one build has no face game", make(), lambda j: j["arm"] == "face" and j["list"] == "h000", clean, held, "A4: "),
             ("a replay mismatch", make(), None, {**clean, "replay_mismatches": 1}, held, "A4: replay_mismatches"),
             ("a chunk missing", make(), None, {**clean, "missing_chunks": 3}, held, "A4: missing_chunks"),
             ("a chunk that fails its checks", make(), None, {**clean, "invalid_chunks": 1}, held, "A4: invalid_chunks"),
             ("far set too small to gate", make(far_pilot=0.3, rows=small), None, clean, small, None)]
    failed = 0
    for name, rates, void, integrity, rows_in, expected in cases:
        data = arrays(made_up(rows_in, training, rates, void), [r["name"] for r in rows_in], training)
        numbers, broken = decide(data, rows_in, training, integrity, resamples=1000)
        ok = not broken if expected is None else any(b.startswith(expected) for b in broken)
        if name == "one build has no face game":
            ok = ok and any("no completed game" in b for b in broken) and not any("void, above" in b for b in broken)
        print(f"{'pass' if ok else 'FAIL'}: {name}: {broken[:3]}")
        failed += not ok
    # Voids under the 1% bar that would decide a bare margin: the worst case fails while A1 and the void rule pass.
    data = arrays(made_up(held, training, make(pilot=0.555, known=0.6)), names, training)
    data["held"]["void"][:, :, :3] = data["held"]["games"][:, :, :3] * 0.0101
    numbers, broken = decide(data, held, training, clean, resamples=1000)
    ok = any(b.startswith("A4 pilot on all: counting void") for b in broken) and not any(b.startswith(("A1", "A4: held")) for b in broken)
    print(f"{'pass' if ok else 'FAIL'}: voids decide a bare margin: mean {numbers['all']['pilot']['mean']:.4f}, worst {numbers['all']['pilot']['worst_case_mean']:.4f}, {broken[:2]}")
    failed += not ok
    data = arrays(made_up(held, training, make(pilot=0.62, mirror=0.37, known=0.8, known_mirror=0.55)), names, training)
    numbers, _ = decide(data, held, training, clean, resamples=1000)
    ok = numbers["all"]["raw_gap"]["mean"] < -0.15 and abs(numbers["all"]["gap"]["mean"]) < 0.03
    print(f"{'pass' if ok else 'FAIL'}: the mirror takes out a build's strength: raw {numbers['all']['raw_gap']['mean']:.3f}, gap {numbers['all']['gap']['mean']:.3f}")
    failed += not ok
    # The shared twin's noise widens the gap's interval: a held-out block with no noise still has one.
    exact = make(pilot=0.75, known=0.8)
    data = arrays(made_up(held, training, exact), names, training)
    for a in ("wins", "games"):
        data["held"][a] = data["held"][a] * 0 + (np.round(0.75 * 1000) if a == "wins" else 1000)
    numbers, _ = decide(data, held, training, clean, resamples=1000)
    ok = numbers["all"]["gap"]["high"] - numbers["all"]["gap"]["low"] > 0.005
    print(f"{'pass' if ok else 'FAIL'}: the interval carries the twins' noise: {numbers['all']['gap']['low']:.4f} to {numbers['all']['gap']['high']:.4f}")
    failed += not ok
    rows = [{"block": "held", "arm": "pilot", "policy": "1702", "list": "h000", "opponent": "t00", "seat": 1, "games": 3, "status": "PASS",
             "duels": [{"winner": 1}, {"winner": 2}, {"winner": 0}], "unresolved_starts": 1},
            {"block": "held", "arm": "pilot", "policy": "1702", "list": "h000", "opponent": "t00", "seat": 1, "games": 4, "status": "VOID"}]
    a = arrays(rows, names, training)["held"]
    got = [int(a[key][0, 1, 0]) for key in ("wins", "games", "void", "suspect")]
    ok = got == [1, 3, 4, 1]
    print(f"{'pass' if ok else 'FAIL'}: draws, voids and interrupted cells: {got}")
    failed += not ok
    failed += selftest_records(held, training)
    failed += selftest_journal()
    failed += selftest_matrix(held, training)
    return 1 if failed else 0


def selftest_records(held, training):
    """What validate accepts, on records made from the real job builder."""
    jobs = cells([r["name"] for r in held[:16]], training, FAKE)
    indices = chunks(jobs)[21]
    part = [jobs[i] for i in indices]
    rows = {r["cell"]: r for r in made_up(held[:16], training, lambda *a: 0.6) if r["cell"] in set(indices)}
    results = {f"first:{i}": {"status": "PASS", "duels": rows[i]["duels"], "counters": {}} for i in indices}
    results |= {f"replay:{i}": dict(results[f"first:{i}"]) for i in replayed(indices)}
    good = assemble(21, part, indices, results, Counter())
    failed = 0

    def expect(name, record, problem=True, mismatch=0):
        nonlocal failed
        problems, mismatches = validate(record, part, indices)
        ok = bool(problems) == problem and (problem or mismatches == mismatch)
        print(f"{'pass' if ok else 'FAIL'}: {name}: {problems[:2]} {mismatches}")
        failed += not ok
    expect("a well-formed chunk", good, problem=False)
    swap = json.loads(json.dumps(good))
    swap["rows"][0], swap["rows"][1] = swap["rows"][1], swap["rows"][0]
    expect("two rows swapped", swap)
    other = json.loads(json.dumps(good))
    other["rows"][3]["list"] = "h015"
    expect("a row naming another list", other)
    wrong = json.loads(json.dumps(good))
    wrong["rows"][5]["duels"][0]["seed"] -= 1
    expect("a wrong duel seed", wrong)
    short = json.loads(json.dumps(good))
    short["rows"][7]["duels"] = short["rows"][7]["duels"][:-1]
    expect("a short PASS row", short)
    policy = json.loads(json.dumps(good))
    policy["rows"][2]["job_sha256"] = text_sha(job_text({**part[2], "policies": part[2]["policies"][::-1]}))
    expect("a row from another policy pairing", policy)
    missing = json.loads(json.dumps(good))
    missing["replays"] = []
    expect("replays missing", missing, problem=bool(replayed(indices)))
    if replayed(indices):
        doubled = json.loads(json.dumps(good))
        doubled["replays"] = doubled["replays"] + doubled["replays"][:1]
        expect("a replay twice", doubled)
        differ = json.loads(json.dumps(good))
        differ["replays"][0]["duels"] = differ["replays"][0]["duels"][::-1]
        expect("a replay that differs is counted", differ, problem=False, mismatch=1)
    else:
        print("FAIL: the test chunk has no scheduled replay")
        failed += 1
    header = json.loads(json.dumps(good))
    header["jobs_sha256"] = "0" * 64
    expect("a wrong job digest", header)
    return failed


class FakeWorker:
    def __init__(self, calls, die_after=None):
        self.calls, self.die_after = calls, die_after

    def run(self, job, timeout):
        self.calls.append(job["seed"])
        if self.die_after is not None and len(self.calls) > self.die_after:
            raise KeyboardInterrupt
        return {"status": "VOID" if job["seed"] == 3 else "PASS", "duels": [], "seed": job["seed"]}


def selftest_journal():
    import tempfile
    failed = 0

    def check(name, ok, detail=""):
        nonlocal failed
        print(f"{'pass' if ok else 'FAIL'}: {name} {detail}")
        failed += not ok
    with tempfile.TemporaryDirectory() as tmp:
        journal = Path(tmp) / "j.jsonl"
        tasks = [(f"first:{i}", {"seed": i}) for i in range(8)]
        calls = []
        pool = type("P", (), {"workers": [FakeWorker(calls, die_after=4)], "cell_timeout": 1})()
        done = {}
        try:
            run_tasks(pool, tasks, journal, done)
        except KeyboardInterrupt:
            pass
        first, unresolved = read_journal(journal)
        check("an interrupted run keeps what finished, and the failure", set(first) == {f"first:{i}" for i in range(4)} and first["first:3"]["status"] == "VOID")
        check("the cell running at the interruption is unresolved", dict(unresolved) == {"first:4": 1}, str(dict(unresolved)))
        calls.clear()
        later = type("P", (), {"workers": [FakeWorker(calls)], "cell_timeout": 1})()
        done = dict(first)
        run_tasks(later, tasks, journal, done)
        check("a resume runs only cells with no result", calls == [4, 5, 6, 7], str(calls))
        again, unresolved = read_journal(journal)
        check("a resume never replaces a stored result, and remembers the interrupted attempt",
              again["first:3"]["status"] == "VOID" and len(again) == 8 and dict(unresolved) == {"first:4": 1}, str(dict(unresolved)))
        with journal.open("a") as f:
            f.write('{"event": "result", "key": "first:9", "res')
        cut, _ = read_journal(journal)
        check("a cut last line is ignored", len(cut) == 8)
        lines = journal.read_text().split("\n")
        lines[2] = "garbage"
        journal.write_text("\n".join(lines))
        try:
            read_journal(journal)
            check("a damaged middle line stops the run", False)
        except SystemExit:
            check("a damaged middle line stops the run", True)
    return failed


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


def pairing_ok(search, plain):
    """Each search game and its plain game: the same id but for the arm letter, decks, seed and net; plain has no search seat."""
    if len(search) != len(plain) or len({j[0] for j in search}) != len(search):
        return False
    return all(s[0][1:] == p[0][1:] and s[0][0] == "g" and p[0][0] == "p" and s[1:4] == p[1:4] and s[5] == p[5] and p[4] == -1 and s[4] in SEATS
               and (s[1] if s[4] == 0 else s[2]).startswith(("h_", "x")) for s, p in zip(search, plain))


def selftest_matrix(held, training):
    names = [r["name"] for r in held]
    jobs = cells(names, training, FAKE)
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
    ok = all(j["policies"][j["seat"]] == FAKE.get(j["policy"], {"kind": "heuristic"}) and j["policies"][1 - j["seat"]] == {"kind": "heuristic"} for j in jobs)
    check("the counted seat holds the right policy", ok)
    blocks = {}
    for j in jobs:
        blocks.setdefault((j["block"], j["list"], j["opponent"]), set()).add(j["seed"])
    check("one seed block per pair", all(len(s) == 1 for s in blocks.values()))
    spans = sorted((j["seed"], j["seed"] + j["games"] - 1) for j in jobs)
    distinct = sorted(set(spans))
    check("blocks don't overlap", all(a[1] < b[0] for a, b in zip(distinct, distinct[1:])))
    real = cells([f"x{i}" for i in range(595)], training, FAKE)
    check("595 builds still fit the held-out block", max(j["seed"] + j["games"] - 1 for j in real if j["block"] == "held") <= HELD_END)
    check("the known block fits", max(j["seed"] + j["games"] - 1 for j in real if j["block"] == "known") <= KNOWN_END)
    used = used_ranges()
    lo, hi = min(s[0] for s in spans), max(s[1] for s in spans)
    search_lo, search_hi = SEARCH_SEED, SEARCH_SEED + SEARCH_LISTS * SEARCH_OPPONENTS - 1
    check("no seed the corpus soak used", all((hi < a or b < lo) and (search_hi < a or b < search_lo) for a, b in used), str(len(used)))
    picks = [jobs[i] for i in replayed(range(len(jobs)))]
    cover = {(j["block"], j["arm"], j["seat"]) for j in picks}
    check("replays cover both blocks, every arm and both seats", cover == {(b, a, s) for b in ("known", "held") for a in ARMS for s in SEATS}, str(len(picks)))
    groups = chunks(jobs)
    check("chunks cover every cell once", sorted(i for c in groups for i in c) == list(range(len(jobs))), str([len(c) for c in groups[:3]]))
    pairs, search, plain = search_jobs([f"x{i}" for i in range(542)], training)
    sgroups = search_groups(pairs, training)
    covered = all(any(d in g and o in g for g in sgroups) for d, o in pairs)
    once = Counter(d for g in sgroups for d in g if d.startswith("x")) == Counter({d: 1 for d, _ in pairs})
    check("search runs hold 21 lists, each held-out build once, with its opponents", all(len(set(g)) == 21 for g in sgroups) and covered and once, str(len(sgroups)))
    check("search games pair with plain games", pairing_ok(search, plain))
    bent = [p[:5] + ["n1703" if p[5] != "n1703" else "n1702"] if i == 7 else p for i, p in enumerate(plain)]
    check("the pairing check fails when one plain game uses another net", not pairing_ok(search, bent))
    return failed


# ---------------------------------------------------------------- provenance

def git(*args):
    return subprocess.run(["git", "-C", str(ROOT), *args], check=True, capture_output=True, text=True).stdout.strip()


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
    if sha(SEARCH_BIN) != SEARCH_BIN_SHA:
        raise SystemExit("the search binary is not M2's")
    if git("status", "--porcelain", "--untracked-files=no"):
        raise SystemExit("commit everything first: the manifest names one commit")
    if OUT.exists() and any(OUT.iterdir()) or SEARCH_OUT.exists():
        raise SystemExit("results exist already: a consumed run can't be frozen again")
    again = select(write=False)
    if json.loads(LISTS.read_text()) != json.loads(json.dumps(again)):
        raise SystemExit("stage-a-lists.json is not what select gives now")
    write_decks()
    _, held, training, jobs = matrix()
    manifest = {"commit": git("rev-parse", "HEAD"), "inputs": inputs(jobs), "search": search_inputs(held, training),
                "constants": {"held_seed": HELD_SEED, "held_count": HELD_COUNT, "known_seed": KNOWN_SEED, "known_count": KNOWN_COUNT, "far": FAR, "bar": BAR,
                              "low_bar": LOW_BAR, "seat_bar": SEAT_BAR, "gap_bar": GAP_BAR, "void_bar": VOID_BAR, "min_far": MIN_FAR, "resamples": RESAMPLES,
                              "boot_seed": BOOT_SEED, "replay_every": REPLAY_EVERY}}
    MANIFEST.write_text(json.dumps(manifest, indent=1) + "\n")
    print(json.dumps({k: v for k, v in manifest["inputs"].items() if k not in ("decks", "files")} | {"commit": manifest["commit"]}, indent=1))


def check_frozen(jobs):
    """The manifest must be committed alone, directly on the commit it names, with nothing else changed, and every input must match it."""
    path = str(MANIFEST.relative_to(ROOT))
    manifest = json.loads(MANIFEST.read_text())
    if git("status", "--porcelain", "--untracked-files=no") or git("status", "--porcelain", "--", path):
        raise SystemExit("uncommitted changes, or an uncommitted manifest")
    if git("rev-parse", "HEAD^") != manifest["commit"]:
        raise SystemExit(f"HEAD must be the manifest's commit, directly on {manifest['commit']}")
    if git("diff", "--name-only", manifest["commit"], "HEAD").split() != [path]:
        raise SystemExit("the commit on top of the manifest's must change the manifest and nothing else")
    now = inputs(jobs)
    changed = [k for k in now if now[k] != manifest["inputs"][k]]
    if changed:
        raise SystemExit(f"inputs differ from the manifest: {changed}")
    return manifest


# ---------------------------------------------------------------- running

def play(workers):
    from actor import Pool
    write_decks()
    _, held, training, jobs = matrix()
    check_frozen(jobs)
    os.environ["EDISON_EXTRA_DECKS"] = str(EXTRA)
    OUT.mkdir(parents=True, exist_ok=True)
    groups = chunks(jobs)
    for n, indices in enumerate(groups):
        path = OUT / f"chunk-{n:03d}.json.gz"
        if path.exists():
            with gzip.open(path, "rt") as f:
                problems, mismatches = validate(json.load(f), [jobs[i] for i in indices], indices)
            if problems:
                raise SystemExit(f"{path} fails its checks: {problems[:3]}")
            if mismatches:
                raise SystemExit(f"{path} holds a replay mismatch: the run stops")
    pool = Pool(workers, OUT / "logs", cell_timeout=1800)
    try:
        for n, indices in enumerate(groups):
            path = OUT / f"chunk-{n:03d}.json.gz"
            if path.exists():
                continue
            journal = OUT / f"chunk-{n:03d}.jsonl"
            part = [jobs[i] for i in indices]
            started = time.perf_counter()
            done, _ = read_journal(journal)
            run_tasks(pool, [(f"first:{i}", worker_job(j)) for i, j in zip(indices, part)], journal, done)
            run_tasks(pool, [(f"replay:{i}", worker_job(jobs[i], threads=4)) for i in replayed(indices)], journal, done)
            results, unresolved = read_journal(journal)
            record = assemble(n, part, indices, results, unresolved)
            record["journal_sha256"] = sha(journal)
            problems, mismatches = validate(record, part, indices)
            if problems:
                raise SystemExit(f"chunk {n} fails its checks: {problems[:3]}")
            scratch = path.with_suffix(".tmp")
            with gzip.open(scratch, "wt") as f:
                json.dump(record, f)
            os.replace(scratch, path)
            rows = record["rows"]
            print(json.dumps({"chunk": n, "of": len(groups), "cells": len(rows), "void_games": sum(r["games"] for r in rows if r["status"] != "PASS"),
                              "unresolved": sum(unresolved.values()), "replays": len(record["replays"]), "mismatches": mismatches,
                              "seconds": round(time.perf_counter() - started)}), flush=True)
            if mismatches:
                raise SystemExit("replay mismatch: results are not reproducible")
    finally:
        pool.close()


def analyze():
    lists, held, training, jobs = matrix()
    check_frozen(jobs)
    rows, integrity = load_results(jobs)
    data = arrays(rows, held, training)
    numbers, broken = decide(data, lists["lists"], training, integrity)
    reasons = Counter(str(r.get("reason"))[:120] for r in rows if r["status"] != "PASS")
    passing = [r for r in rows if r["status"] == "PASS"]
    result = {"verdict": "STAGE_A_MET" if not broken else "NOT_MET", "broken": broken, "numbers": numbers, "reported": reported(data, lists["lists"], training),
              "void_reasons": dict(reasons), "unresolved_starts": sum(r.get("unresolved_starts", 0) for r in rows),
              "draws": sum(d["winner"] == 2 for r in passing for d in r["duels"]), "decisions": sum(r["counters"]["decisions"] for r in passing),
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
    """60 held-out builds, each against 4 training lists, drawn once. The search side plays the held-out build in both seats, the net rotating by pair."""
    rng = random.Random(SEARCH_DRAW)
    picked = rng.sample(sorted(held), SEARCH_LISTS)
    pairs = [(d, o) for d in picked for o in rng.sample(sorted(training), SEARCH_OPPONENTS)]
    nets = ("n1702", "n1703", "n1704")
    budget, depth, candidates, z = SEARCH_SETTING
    search, plain = [], []
    for p, (d, o) in enumerate(pairs):
        for seat in SEATS:
            d0, d1 = (d, o) if seat == 0 else (o, d)
            search.append([f"g{p}_{seat}", d0, d1, SEARCH_SEED + p, seat, nets[p % 3], budget, depth, candidates, z, 1, 5000 + 2 * p + seat, 0])
            plain.append([f"p{p}_{seat}", d0, d1, SEARCH_SEED + p, -1, nets[p % 3]])
    return pairs, search, plain


def search_groups(pairs, training):
    """The search binary loads exactly 21 lists. Each run holds some held-out builds, in draw order, and the training lists they meet,
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
    return {"binary": sha(SEARCH_BIN), "nets": {k: sha(SEARCH_NETS / f"gauntlet-{k}.ednet") for k in OPTIMIZERS},
            "jobs": text_sha(json.dumps([search, plain])), "groups": search_groups(pairs, training)}


def paired_summary(kept, clusters, rng, resamples):
    """Scores and their difference on one population of comparisons, with an interval that resamples held-out builds."""
    if not kept:
        return {"comparisons": 0, "builds": 0, "search": None, "plain": None, "difference": None, "low": None, "high": None, "note": "no complete pair"}
    by = {}
    for c in kept:
        by.setdefault(clusters[c[0]], []).append(c[2] - c[3])
    groups = list(by.values())
    draws = []
    for _ in range(resamples):
        pick = rng.integers(0, len(groups), len(groups))
        values = [v for i in pick for v in groups[i]]
        draws.append(np.mean(values))
    return {"comparisons": len(kept), "builds": len(groups), "search": float(np.mean([c[2] for c in kept])), "plain": float(np.mean([c[3] for c in kept])),
            "difference": float(np.mean([c[2] - c[3] for c in kept])), "low": float(np.percentile(draws, 2.5)), "high": float(np.percentile(draws, 97.5))}


def search_check(workers):
    sys.path.insert(0, str(ROOT / "tools/edison/search"))
    import search_common as sc
    import search_study as study
    lists, held, training, jobs = matrix()
    manifest = check_frozen(jobs)
    if not RESULT.exists() or json.loads(RESULT.read_text()).get("manifest_sha256") != sha(MANIFEST):
        raise SystemExit("run analyze on this manifest first: the search check comes after the verdict")
    if search_inputs(held, training) != manifest["search"]:
        raise SystemExit("the search check's inputs differ from the manifest")
    if sc.BIN != SEARCH_BIN or sc.OUT / "nets" != SEARCH_NETS:
        raise SystemExit("search_common points at another binary or other nets")
    decks = {**deck_registry.registry(), **json.loads(EXTRA.read_text())}
    pairs, search, plain = search_jobs(held, training)
    games = {}
    for n, names in enumerate(manifest["search"]["groups"]):
        if len(set(names)) != 21:
            raise SystemExit("a search group does not hold 21 lists")
        sc.REGISTRY = SEARCH_OUT / f"registry-{n:02d}.txt"
        sc.REGISTRY.parent.mkdir(parents=True, exist_ok=True)
        sc.REGISTRY.write_text("".join(f"{k} {Path(decks[k]).resolve()}\n" for k in sorted(names)))
        mine = [j for j in search + plain if pairs[int(j[0][1:].split("_")[0])][0] in names]
        games.update(sc.play([sc.job_line(*j) for j in mine], SEARCH_OUT / f"group-{n:02d}", workers))
    comparisons, missing, void = [], Counter(), Counter()
    reasons = Counter()
    for p in range(len(pairs)):
        for seat in SEATS:
            g, b = games.get(f"g{p}_{seat}"), games.get(f"p{p}_{seat}")
            for arm, game in (("search", g), ("plain", b)):
                if game is None:
                    missing[arm] += 1
                elif game.get("status") != "OK":
                    void[arm] += 1
                    reasons[f"{arm}: {str(game.get('reason') or game.get('status'))[:80]}"] += 1
            a = study.score(g, seat) if g else None
            q = study.score(b, seat) if b else None
            if a is not None and q is not None:
                comparisons.append((p, seat, a, q))
    clusters = [d for d, _ in pairs]
    far = {r["name"] for r in lists["lists"] if r["far"]}
    rng = np.random.default_rng(SEARCH_DRAW)
    result = {"pairs": len(pairs), "comparisons_planned": 2 * len(pairs), "missing": dict(missing), "void": dict(void), "void_reasons": dict(reasons),
              "all": paired_summary(comparisons, clusters, rng, RESAMPLES),
              "by_seat": {s: paired_summary([c for c in comparisons if c[1] == s], clusters, rng, RESAMPLES) for s in SEATS},
              "far": paired_summary([c for c in comparisons if clusters[c[0]] in far], clusters, rng, RESAMPLES),
              "search_games": study.summarise({k: v for k, v in games.items() if k.startswith("g")}),
              "note": "Reported only. The interval resamples held-out builds. The search is told the opposing list and deals hidden cards uniformly, as in M2."}
    SEARCH_RESULT.write_text(json.dumps(result, indent=1) + "\n")
    print(json.dumps({k: v for k, v in result.items() if k != "search_games"}, indent=1))


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
