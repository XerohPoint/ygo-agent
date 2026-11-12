#!/usr/bin/env python3
"""M3 Stage B: networks with an unknown-identity row, on lists with cards that no training list plays.

docs/edison-m3b-protocol.md is the plan, and this script is its exact form.

  soak        every candidate list through actor.py's own audit on the certified module; random and rule-based play only
  select      the held-out builds from the corpus, into stage-b-lists.json
  selftest    the rules, the result checks, the pilot's choice and the matrix's shape, on made-up data with known answers
  livecheck   a few cells on training lists only, with every held-out deck loaded; no network plays a corpus list
  freeze      records every input in m3b-manifest.json, to be committed alone on top of the commit it names
  pilot-train three short runs, one per dropout setting; refuses unless HEAD is the manifest's commit
  pilot       plays the pilot's networks on training lists and picks the setting by the protocol's rule
  train       the three round 2560 runs on the picked setting
  seal        records the pick and the trained checkpoints in m3b-checkpoints.json, to be committed alone on the manifest
  play        the cells, journaled as they finish; refuses unless HEAD is the seal's commit
  analyze     the verdict: exit 0 on a pass, 1 on a fail

Every pilot-train, pilot, train, seal and play appends a line to attempts.jsonl in the build directory, and the seal
carries the log, so a pilot that was run more than once shows in git.
"""
import os
os.environ["EDISON_CODE_LIST"] = "v2"  # before decks is imported: every process this starts loads code list v2

import argparse  # noqa: E402
from collections import Counter  # noqa: E402
import gzip  # noqa: E402
import hashlib  # noqa: E402
import json  # noqa: E402
from pathlib import Path  # noqa: E402
import sqlite3  # noqa: E402
import subprocess  # noqa: E402
import sys  # noqa: E402
import time  # noqa: E402

import numpy as np  # noqa: E402

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path[:0] = [str(ROOT / "tools/edison/learner"), str(HERE)]
import decks as deck_registry  # noqa: E402
import stage_a as A  # noqa: E402  (the journal, its repair and the task runner; nothing of Stage A's design)
from stage_a import distance, git, ratio, read_journal, run_tasks, sha, text_sha  # noqa: E402

CORPUS = ROOT / "assets/edison/corpus"
LISTS = HERE / "stage-b-lists.json"
SOAK = HERE / "stage-b-soak.json"
MANIFEST = HERE / "m3b-manifest.json"
SEAL = HERE / "m3b-checkpoints.json"
# Results outlive a session's worktree, so they sit beside M1's checkpoints. Nothing here is read as an input
# except through a hash in the manifest or the seal.
BUILD = Path(os.environ.get("EDISON_STAGE_B_BUILD", "/home/mada/ygo-agent/build/edison-stage-b-2026-11-12"))
DECKS = BUILD / "decks"
EXTRA = BUILD / "extra-decks.json"
PILOT = BUILD / "pilot"
RUNS = BUILD / "runs"
ATTEMPTS = BUILD / "attempts.jsonl"
V1_RUNS = Path("/home/mada/ygo-agent/build/edison-retrain-2026-11-04/runs")
PYTHON = "/home/mada/ygo-agent/.venv/bin/python"
GPU_PYTHON = "/home/mada/ygo-agent/.venv-gpu/bin/python"
NATIVE = ROOT / "ygoenv/ygoenv/ygopro/ygopro_ygoenv.cpython-310-x86_64-linux-gnu.so"
NATIVE_SHA = "d1f1f701e571cdaecc4270283bc0ae8e87533e0021ddf4ee84929dae81359983"  # certified, hardening r5
CODES_SHA = "071a484d92d6e960a70a57d7d764bdff6f54930d4fa2c57702b02cc7383e848a"
IDENTITY_SHA = "800fb1dab816fc098a86c4781ac190946ae2a9b2305b935f0560008cf1e15839"
V1_PARAMS_SHA = {"1702": "cb4cb1ad86aa5f6e33a339809e421f726225c9c13b70eaed5394f38cec6798cd",
                 "1703": "03132b3f63c97f6aeec7b54c544ce45e450080f235389ac05075950f33f28ce0",
                 "1704": "ddbdcee21ccceb9a60485aa785888b08c8c735ba79b46550ff18f83a9412a3a8"}
OPTIMIZERS = tuple(V1_PARAMS_SHA)
SEATS = (0, 1)
DOSES = ("2", "5", "10", "all")
GATED_DOSE = "5"
# Arms. pilot, face and mirror are Stage A's. base is the pilot arm played by the M1 networks given code list v2's
# table with no retraining. mask<d> is the pilot arm with d of the piloted list's identities hidden for the whole game.
HELD_ARMS = ("pilot", "face", "mirror", "base")
KNOWN_ARMS = HELD_ARMS + tuple(f"mask{d}" for d in DOSES)
COLUMNS = [(arm, k) for arm in ("pilot", "face") for k in OPTIMIZERS] + [("mirror", "heuristic")] + \
          [(arm, k) for arm in ("base",) + tuple(f"mask{d}" for d in DOSES) for k in OPTIMIZERS]
COL = {c: n for n, c in enumerate(COLUMNS)}
MIRROR = COL["mirror", "heuristic"]


def cols(arm):
    return [COL[arm, k] for k in OPTIMIZERS]


HELD_SEED, HELD_COUNT, HELD_END = 90000000, 4, 90099999      # one block of 4 per (held-out build, training list)
KNOWN_SEED, KNOWN_COUNT, KNOWN_END = 90200000, 32, 90219999  # one block of 32 per (training list, training list)
SOAK_SEED, SOAK_END = 89400000, 89499999
PILOT_SEED, PILOT_COUNT, PILOT_END = 89300000, 8, 89309999
LIVE_SEED = 89390000
TRAIN_SEED = 81000000  # M1's schedule of pairs and deals, so the runs differ from M1's only in what they are given
HEAVY = 6      # a build with this many Main Deck copies that read the unknown row is in the heavy set
ACTS = {1: "Set", 3: "Special Summon", 4: "Normal Summon", 5: "Set a monster", 6: "attack", 8: "activate"}  # ygopro.h ActionAct
BAR, LOW_BAR, SEAT_BAR, GAP_BAR, VOID_BAR, MIN_HEAVY = 0.55, 0.50, 0.50, -0.10, 0.01, 10
# Readings, reported beside the verdict and not part of it.
KNOWN_BAR, MASK_BAR, HELP_BAR = -0.05, -0.10, 0.03
RESAMPLES, BOOT_SEED = 10000, 90810001
REPLAY_EVERY = 127  # odd, so replays fall on both counted seats
CHUNK_LISTS = 8
# The dropout pilot. Settings are (probability a game is left alone, top of the per-game hiding rate).
SETTINGS = {"S0": (1.0, 0.0), "S1": (0.5, 0.5), "S2": (0.25, 1.0)}
PILOT_OPTIMIZER, PILOT_ROUNDS = 1705, 640
PILOT_DOSES = ("0", "5", "all")
ADMIT, TIE = 0.04, 0.02
TRAIN = {"arm": "gauntlet", "rounds": 2560, "games_per_cell": 8, "seed_base": TRAIN_SEED, "deck_set": "gauntlet", "cells_per_round": 12,
         "opponents": "league", "snapshot_every": 4, "learning_rate": 0.001, "minibatch": 256, "epochs": 4, "clip": 0.2, "entropy": 0.01,
         "value_coef": 0.5, "grad_norm": 0.5, "kl_stop": 0.03, "gae_lambda": 0.95, "row_grouping": "1", "id_dim": 32, "channels": 64,
         "layers": 2, "heads": 4, "critic_hidden": 128}
DEPENDS = ("tools/edison/heldout/stage_b.py", "tools/edison/heldout/stage_a.py", "tools/edison/heldout/stage-b-lists.json",
           "tools/edison/heldout/stage-b-soak.json", "docs/edison-m3b-protocol.md", "tools/edison/learner/actor.py",
           "tools/edison/learner/train.py", "tools/edison/learner/decks.py", "tools/edison/learner/obs.py", "tools/edison/learner/net.py",
           "tools/edison/learner/codes_v1.txt", "tools/edison/learner/codes_v2.txt", "tools/edison/learner/identity_v2.json",
           "ygoai/edison_policy.py", "assets/edison/legality.json", "assets/edison/corpus/manifest.json")
JOB_FIELDS = ("block", "arm", "policy", "list", "opponent", "deck0", "deck1", "seat", "seed", "games", "players", "hidden")
# Every other seed block this work or its reviews used, so no test or pilot seed repeats one.
OTHER_BLOCKS = {"identity checks": (89200000, 89299999), "reviews": (89500000, 89599999), "rehearsal": (89600000, 89699999),
                "search check": (90300000, 90399999), "corpus soak and Stage A": (88000000, 88999999), "M1 and M2": (82000000, 87999999)}


def out_dir():
    """Results sit under the seal they were played on, so cells played on other checkpoints can never be read as these."""
    return BUILD / f"stage-b-{sha(SEAL)[:16]}"


def attempt(what, **detail):
    BUILD.mkdir(parents=True, exist_ok=True)
    with ATTEMPTS.open("a") as log:
        log.write(json.dumps({"what": what, "time": time.strftime("%Y-%m-%dT%H:%M:%S"), "head": git("rev-parse", "HEAD"), **detail}) + "\n")


# ---------------------------------------------------------------- selection

def identity():
    codes_sha, codes = deck_registry.load_codes()
    identity_sha, row_of = deck_registry.load_identity()
    if codes_sha != CODES_SHA or identity_sha != IDENTITY_SHA:
        raise SystemExit("code list v2 or its identity map changed")
    return {code: cid for cid, code in codes.items()}, row_of


def build_key(zones):
    """What a duel plays: Main and Extra as multisets of passcodes. The Side never enters a single duel."""
    return tuple(sorted(zones["main"])), tuple(sorted(zones["extra"]))


def unknown_band(main_copies):
    return "Extra Deck only" if main_copies == 0 else "1-2" if main_copies <= 2 else "3-5" if main_copies <= 5 else "6-10" if main_copies <= 10 else "11+"


BANDS = ("Extra Deck only", "1-2", "3-5", "6-10", "11+")


def soak_failures():
    """Lists with a void cell in the soak. It must have run on these inputs and finished."""
    soak = json.loads(SOAK.read_text())
    if soak["native"] != NATIVE_SHA or soak["codes_sha256"] != CODES_SHA or soak["finished"] != soak["planned"]:
        raise SystemExit("stage-b-soak.json is not a finished soak on the certified module and code list v2")
    return soak


def select(write=True):
    ids, row_of = identity()
    unknown = int(row_of[-1])
    registry = deck_registry.registry()
    known = {name: Counter(z["main"] + z["extra"]) for name, z in ((n, deck_registry.parse_ydk(Path(p).read_text())) for n, p in registry.items())}
    soak = soak_failures()
    failed = set(soak["void_lists"])
    data = {row[0]: (row[1], row[2]) for row in sqlite3.connect(deck_registry.DB).execute("select id, alias, type from datas")}
    card = lambda code: data[code][0] if deck_registry.is_artwork(code, data[code][0]) else code  # artworks are one card
    manifest = json.loads((CORPUS / "manifest.json").read_text())
    candidates, excluded, soaked = {}, Counter(), set(soak["lists"])
    for row in sorted(manifest["lists"], key=lambda r: int(r["list_id"][2:])):
        zones = deck_registry.parse_ydk((CORPUS / row["ydk"]).read_text())
        me = Counter(zones["main"] + zones["extra"])
        if row["production_legal"] and any(code not in ids for code in me):
            raise SystemExit(f"{row['list_id']}: a legal list holds a card outside code list v2")
        is_unknown = lambda code: code in ids and int(row_of[ids[code]]) == unknown
        cards = sorted(Counter(card(code) for code in me.elements() if is_unknown(code)).items())
        twin = min(sorted(known), key=lambda n: distance(me, known[n]))
        key = build_key(zones)
        if not row["production_legal"]:
            excluded["not production legal"] += 1
        elif row["in_training"]:
            excluded["a training list"] += 1
        elif not cards:
            excluded["every Main and Extra card is known, which is Stage A's question"] += 1
        elif row["list_id"] not in soaked:
            raise SystemExit(f"{row['list_id']} was not in the soak")
        elif row["list_id"] in failed:
            excluded["a void cell in the soak"] += 1
        elif key in candidates:
            excluded["the same Main and Extra as a lower-numbered held-out list"] += 1
            candidates[key]["variants"].append(row["list_id"])
            candidates[key]["archetypes"] = sorted(set(candidates[key]["archetypes"]) | {row["archetype"]})
        else:
            copies, main_copies = sum(n for _, n in cards), sum(1 for code in zones["main"] if is_unknown(code))
            candidates[key] = {"name": f"h_{row['list_id']}", "list_id": row["list_id"], "variants": [row["list_id"]], "source": row["ydk"],
                               "source_sha256": sha(CORPUS / row["ydk"]), "archetype": row["archetype"], "archetypes": [row["archetype"]],
                               "twin": twin, "distance": distance(me, known[twin]), "unknown_copies": copies, "unknown_main_copies": main_copies,
                               "unknown_distinct": len(cards), "unknown_spell_trap_copies": sum(n for code, n in cards if not data[code][1] & 1),
                               "unknown_cards": [[code, n] for code, n in cards], "band": unknown_band(main_copies), "heavy": main_copies >= HEAVY,
                               "main": len(key[0]), "extra": len(key[1])}
    chosen = sorted(candidates.values(), key=lambda r: r["name"])
    if HELD_SEED + len(chosen) * len(known) * HELD_COUNT - 1 > HELD_END:
        raise SystemExit("the held-out seed block is too small for this many builds")
    out = {"rule": "production legal, not a training list, at least one Main or Extra card that reads the unknown row of identity_v2.json, "
                   "no void cell in the soak, and one entry per distinct Main and Extra. Lists play as published: no card is removed",
           "corpus_manifest_sha256": sha(CORPUS / "manifest.json"), "code_list_v2_sha256": CODES_SHA, "identity_sha256": IDENTITY_SHA,
           "soak_sha256": sha(SOAK), "training_lists": sorted(known), "heavy_copies": HEAVY, "corpus_lists": len(manifest["lists"]),
           "excluded": dict(sorted(excluded.items())), "held_out_lists": sum(len(r["variants"]) for r in chosen), "held_out": len(chosen),
           "heavy": sum(r["heavy"] for r in chosen), "by_band": {band: sum(r["band"] == band for r in chosen) for band in BANDS},
           "unknown_cards": len({code for r in chosen for code, _ in r["unknown_cards"]}),
           "by_archetype": dict(Counter(r["archetype"] for r in chosen).most_common()), "by_twin": dict(Counter(r["twin"] for r in chosen).most_common()),
           "lists": chosen}
    if write:
        LISTS.write_text(json.dumps(out, indent=1) + "\n")
    return out


def write_decks(lists=None):
    """The env's copies: Main and Extra as published, with no Side, which a single duel never reads."""
    lists = json.loads(LISTS.read_text())["lists"] if lists is None else lists
    DECKS.mkdir(parents=True, exist_ok=True)
    extra = {}
    for row in lists:
        if sha(CORPUS / row["source"]) != row["source_sha256"]:
            raise SystemExit(f"{row['source']} changed since selection")
        zones = deck_registry.parse_ydk((CORPUS / row["source"]).read_text())
        path = DECKS / f"{row['name']}.ydk"
        deck_registry.write_ydk(path, {"main": zones["main"], "extra": zones["extra"], "side": []})
        extra[row["name"]] = str(path)
    EXTRA.write_text(json.dumps(extra, indent=1) + "\n")
    return extra


# ---------------------------------------------------------------- the matrix

def identity_rows(training):
    """The identity rows of each training list's Main and Extra: what a mask arm draws from."""
    ids, row_of = identity()
    registry = deck_registry.registry()
    out = {}
    for name in training:
        zones = deck_registry.parse_ydk(Path(registry[name]).read_text())
        rows = sorted({int(row_of[ids[code]]) for code in zones["main"] + zones["extra"]})
        if rows[-1] == int(row_of[-1]) or rows[0] == 0:
            raise SystemExit(f"{name}: a training list holds a card with no identity row")
        out[name] = rows
    return out


def hidden_ids(rows, dose, seed, games):
    """One list of hidden identity rows per game. A pure function of the game's seed, the dose and the list's rows."""
    out = []
    for game in range(games):
        # One order of the list's rows per game, and a dose takes its first rows: a larger dose hides all a smaller one does.
        order = np.random.default_rng([seed + game, 0xD05E]).permutation(rows)
        out.append(sorted(int(r) for r in (order if dose == "all" else order[:int(dose)])))
    return out


def cells(held, training, rows):
    """Every cell in a fixed order. A cell's seat is the seat whose result counts: the network's, or in the mirror the list's.

    pilot    a Stage B network plays the list against the heuristic on a training list
    face     a Stage B network plays the training list against the heuristic on the list
    mirror   the heuristic plays both, which measures the list itself
    base     an M1 network, given code list v2's table without retraining, plays the list as in pilot
    mask<d>  as pilot, on training lists only, with d of the list's identities hidden for the whole game
    All arms share each cell's decks and seeds, so they differ only in who plays and what is hidden.
    A job names its players by label. The seal binds a label to a checkpoint."""
    out = []
    for block, names, base, count, arms in (("known", training, KNOWN_SEED, KNOWN_COUNT, KNOWN_ARMS), ("held", held, HELD_SEED, HELD_COUNT, HELD_ARMS)):
        for i, x in enumerate(names):
            for j, o in enumerate(training):
                seed = base + (len(training) * i + j) * count
                for arm in arms:
                    for k in (OPTIMIZERS if arm != "mirror" else ("heuristic",)):
                        player = "heuristic" if arm == "mirror" else f"v1:{k}" if arm == "base" else f"v2:{k}"
                        hidden = hidden_ids(rows[x], arm[4:], seed, count) if arm.startswith("mask") else None
                        for seat in SEATS:
                            ours, theirs = (o, x) if arm == "face" else (x, o)
                            d0, d1 = (ours, theirs) if seat == 0 else (theirs, ours)
                            out.append({"block": block, "arm": arm, "policy": k, "list": x, "opponent": o, "deck0": d0, "deck1": d1, "seat": seat,
                                        "seed": seed, "games": count, "players": [player, "heuristic"] if seat == 0 else ["heuristic", player],
                                        "hidden": hidden})
    return out


def chunks(jobs):
    """Known lists one at a time, then held-out builds eight at a time."""
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
    return json.dumps({k: job[k] for k in JOB_FIELDS}, sort_keys=True)


def part_sha(part):
    return text_sha("\n".join(job_text(j) for j in part))


def matrix():
    lists = json.loads(LISTS.read_text())
    held = [r["name"] for r in lists["lists"]]
    training = lists["training_lists"]
    return lists, held, training, cells(held, training, identity_rows(training))


def worker_job(job, specs, **extra):
    """What actor.py runs: the labels become checkpoints, and hidden rows become the game's identity spec."""
    out = {"deck0": job["deck0"], "deck1": job["deck1"], "seed": job["seed"], "games": job["games"],
           "policies": [specs[label] for label in job["players"]], **extra}
    if job["hidden"] is not None:
        out["identity"] = {"unknown_ids": job["hidden"]}
    return out


def net_spec(stem, expected, v1=False):
    meta = json.loads(Path(str(stem) + ".json").read_text())
    if meta["params_sha256"] != expected:
        raise SystemExit(f"{stem} is not the expected checkpoint")
    spec = {"kind": "net", "checkpoint": str(stem), "sha256": expected, "greedy": True, "repeat_guard": True}
    return {**spec, "v1_into_v2": True} if v1 else spec


def specs_from_seal(seal):
    specs = {"heuristic": {"kind": "heuristic"}}
    for k in OPTIMIZERS:
        specs[f"v1:{k}"] = net_spec(V1_RUNS / f"gauntlet-{k}/checkpoint-2560", V1_PARAMS_SHA[k], v1=True)
        specs[f"v2:{k}"] = net_spec(RUNS / f"main-{k}/checkpoint-{TRAIN['rounds']:04d}", seal["checkpoints"][k]["params_sha256"])
    return specs


# ---------------------------------------------------------------- journal and result checks

def assemble(n, part, indices, results, unresolved):
    rows = []
    for job, i in zip(part, indices):
        r = results[f"first:{i}"]
        first, again = unresolved.get(f"first:{i}", 0), unresolved.get(f"replay:{i}", 0)
        rows.append({"cell": i, "job_sha256": text_sha(job_text(job)), **{k: job[k] for k in JOB_FIELDS if k != "hidden"}, "status": r.get("status"),
                     "duels": r.get("duels"), "reason": r.get("reason"), "counters": r.get("counters"), "hidden_rows": r.get("hidden_rows"),
                     "unresolved_first": first, "unresolved_replay": again, "unresolved_starts": first + again})
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
        if row.get("cell") != i or row.get("job_sha256") != text_sha(job_text(job)) or any(row.get(k) != job[k] for k in JOB_FIELDS if k != "hidden"):
            problems.append(f"cell {i}: another job's row")
            continue
        if row["status"] == "PASS":
            duels = row.get("duels") or []
            if [d.get("seed") for d in duels] != list(range(job["seed"], job["seed"] + job["games"])):
                problems.append(f"cell {i}: duels don't cover its seeds")
            if any(d.get("winner") not in (0, 1, 2) for d in duels):
                problems.append(f"cell {i}: a winner outside 0, 1, 2")
            # What the worker says it hid must be what the job asked it to hide.
            if row.get("hidden_rows") != job["hidden"]:
                problems.append(f"cell {i}: the hidden identities are not the job's")
        elif row["status"] != "VOID":
            problems.append(f"cell {i}: status {row['status']}")
        counts = [row.get(k) for k in ("unresolved_first", "unresolved_replay", "unresolved_starts")]
        if not all(isinstance(c, int) and c >= 0 for c in counts) or counts[0] + counts[1] != counts[2]:
            problems.append(f"cell {i}: interrupted attempts not recorded")
    replays = record.get("replays", [])
    if [r.get("cell") for r in replays] != replayed(indices):
        problems.append("replays are not the scheduled cells")
    for r in replays:
        if r.get("cell") in by_cell and r.get("unresolved_starts") != by_cell[r["cell"]].get("unresolved_replay"):
            problems.append(f"cell {r.get('cell')}: replay interruptions disagree")
    for i in set(indices) - set(replayed(indices)):
        if by_cell.get(i, {}).get("unresolved_replay"):
            problems.append(f"cell {i}: a replay interruption on a cell with no replay")
    mismatches = sum(1 for r in replays if r.get("cell") in by_cell and (r.get("status") != by_cell[r["cell"]].get("status") or r.get("duels") != by_cell[r["cell"]].get("duels")))
    return problems, mismatches


def check_chunk(n, record, part, indices, journal):
    """validate(), and the chunk must be exactly what its journal holds, so a stored row can't drop an interrupted attempt or a failure."""
    problems, mismatches = validate(record, part, indices)
    if not problems and (not journal.exists() or sha(journal) != record.get("journal_sha256")
                         or {k: v for k, v in record.items() if k != "journal_sha256"} != assemble(n, part, indices, *read_journal(journal))):
        problems.append("the chunk is not what its journal holds")
    return problems, mismatches


def load_results(jobs):
    rows, integrity = [], Counter(missing_chunks=0, invalid_chunks=0, replay_mismatches=0)
    out = out_dir()
    for n, indices in enumerate(chunks(jobs)):
        path = out / f"chunk-{n:03d}.json.gz"
        if not path.exists():
            integrity["missing_chunks"] += 1
            continue
        with gzip.open(path, "rt") as f:
            record = json.load(f)
        problems, mismatches = check_chunk(n, record, [jobs[i] for i in indices], indices, out / f"chunk-{n:03d}.jsonl")
        if problems:
            integrity["invalid_chunks"] += 1
            continue
        integrity["replay_mismatches"] += mismatches
        rows += record["rows"]
    return rows, dict(integrity)


# ---------------------------------------------------------------- the rules

def arrays(rows, held, training):
    """Wins, games, void games and wins in cells with an interrupted earlier attempt, by block, list, (opponent, seat) unit and column."""
    out = {}
    for block, names in (("known", training), ("held", held)):
        out[block] = {key: np.zeros((len(names), 2 * len(training), len(COLUMNS))) for key in ("wins", "games", "void", "suspect")}
    place = {"known": {x: i for i, x in enumerate(training)}, "held": {x: i for i, x in enumerate(held)}}
    opponent = {o: j for j, o in enumerate(training)}
    for row in rows:
        a = out[row["block"]]
        at = (place[row["block"]][row["list"]], 2 * opponent[row["opponent"]] + row["seat"], COL[row["arm"], row["policy"]])
        if row["status"] != "PASS":
            a["void"][at] += row["games"]
            continue
        wins = sum(d["winner"] == row["seat"] for d in row["duels"])  # a draw is not a win
        a["wins"][at] += wins
        a["games"][at] += len(row["duels"])
        if row.get("unresolved_starts"):
            a["suspect"][at] += wins
    return out


def gap_of(w, g, wk, gk, twins):
    """Per list: the mean over optimizers of (pilot - mirror) on the list minus the same on its twin. w, g are (lists, columns)."""
    p = cols("pilot")
    edge = ratio(w[:, p], g[:, p]) - ratio(w[:, MIRROR], g[:, MIRROR])[:, None]
    known_edge = ratio(wk[:, p], gk[:, p]) - ratio(wk[:, MIRROR], gk[:, MIRROR])[:, None]
    return (edge - known_edge[twins]).mean(axis=1)


def pooled(w, g, arm):
    """The mean over optimizers of an arm's pooled rate. w, g are (lists, columns) or (columns,)."""
    w, g = (w.sum(axis=0), g.sum(axis=0)) if w.ndim == 2 else (w, g)
    return float(np.mean(ratio(w[cols(arm)], g[cols(arm)])))


def known_stats(wk, gk):
    """On the training lists: Stage B's networks against M1's, and what hiding identities costs. Each is a difference of
    means over optimizers, on the same decks and seeds."""
    out = {"known_minus_base": pooled(wk, gk, "pilot") - pooled(wk, gk, "base")}
    for d in DOSES:
        out[f"mask{d}_minus_pilot"] = pooled(wk, gk, f"mask{d}") - pooled(wk, gk, "pilot")
    return out


def bootstrap(data, subset, twins, rng, resamples):
    """Resample lists, then each drawn list's 42 (opponent, seat) cells, and each training list's cells once per draw,
    shared by every list whose twin it is. All columns of a cell share its seeds, so they move together.
    Returns, for each draw: the pooled pilot and face rates by optimizer, the mean gap, the pilot minus base difference on
    the drawn lists, and the known block's differences; and how many drawn values were undefined."""
    held, known = data["held"], data["known"]
    W, G = held["wins"][subset], held["games"][subset]
    WK, GK = known["wins"], known["games"]
    tw = twins[subset]
    n, units = W.shape[0], W.shape[1]
    six = cols("pilot") + cols("face")
    rates, gaps, helps, undefined = np.empty((resamples, 6)), np.empty(resamples), np.empty(resamples), 0
    known_draws = {key: np.empty(resamples) for key in known_stats(WK.sum(axis=1), GK.sum(axis=1))}
    k_rows = np.arange(WK.shape[0])[:, None]
    for r in range(resamples):
        lists = rng.integers(0, n, n)
        cells_ = rng.integers(0, units, (n, units))
        w, g = W[lists[:, None], cells_].sum(axis=1), G[lists[:, None], cells_].sum(axis=1)
        known_cells = rng.integers(0, units, (WK.shape[0], units))
        wk, gk = WK[k_rows, known_cells].sum(axis=1), GK[k_rows, known_cells].sum(axis=1)
        drawn = ratio(w[:, six].sum(axis=0), g[:, six].sum(axis=0))
        gap = gap_of(w, g, wk, gk, tw[lists])
        # A drawn build whose cells, or whose twin's, hold no game for a rate the gap needs counts at the lowest possible gap.
        undefined += int(np.isnan(gap).sum()) + int(np.isnan(drawn).sum())
        rates[r] = np.where(np.isnan(drawn), 0.0, drawn)
        gaps[r] = np.where(np.isnan(gap), -2.0, gap).mean()
        helps[r] = pooled(w, g, "pilot") - pooled(w, g, "base")
        for key, value in known_stats(wk, gk).items():
            known_draws[key][r] = value
    return rates, gaps, helps, known_draws, undefined


def interval(point, draws):
    return {"mean": float(point), "low": float(np.nanpercentile(draws, 2.5)), "high": float(np.nanpercentile(draws, 97.5))}


def decide(data, lists, training, integrity, resamples=RESAMPLES):
    """The protocol's rules in order. Returns the numbers and the rules broken."""
    rng = np.random.default_rng(BOOT_SEED)
    names = [r["name"] for r in lists]
    twins = np.array([training.index(r["twin"]) for r in lists])
    heavy = np.array([i for i, r in enumerate(lists) if r["heavy"]], dtype=int)
    everything = np.arange(len(lists))
    gate_heavy = len(heavy) >= MIN_HEAVY
    held, known = data["held"], data["known"]
    numbers, broken = {"lists": len(names), "heavy_lists": int(len(heavy)), "heavy_gated": gate_heavy}, []
    known_draws = None
    for label, subset, gated in (("all", everything, True), ("heavy", heavy, gate_heavy)):
        if not len(subset):
            numbers[label] = {"lists": 0, "note": "no builds"}
            if gated:
                broken.append(f"B1 on {label}: there are no builds")
            continue
        W, G, V, S = (held[key][subset].sum(axis=(0, 1)) for key in ("wins", "games", "void", "suspect"))
        shape = (len(subset), len(training), 2, len(COLUMNS))
        seat_w = held["wins"][subset].reshape(shape).sum(axis=(0, 1))
        seat_g = held["games"][subset].reshape(shape).sum(axis=(0, 1))
        rates, gaps, helps, draws, undefined = bootstrap(data, subset, twins, rng, resamples)
        known_draws = known_draws or draws  # the known block's draws from the first, all-builds pass
        per = {"lists": int(len(subset)), "undefined_draws": undefined}
        for arm, rule, at in (("pilot", "B1", 0), ("face", "B2", 3)):
            by = {k: {"games": int(G[c]), "rate": float(ratio(W[c], G[c])), "low": float(np.nanpercentile(rates[:, at + n], 2.5))}
                  for n, (k, c) in enumerate(zip(OPTIMIZERS, cols(arm)))}
            mean = float(np.mean([v["rate"] for v in by.values()]))
            worst = float(np.mean([ratio(W[c] - S[c], G[c] + V[c]) for c in cols(arm)]))
            seats = {s: float(ratio(seat_w[s, cols(arm)].sum(), seat_g[s, cols(arm)].sum())) for s in SEATS}
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
                broken.append(f"B4 {arm} on {label}: counting void and interrupted games as losses, the mean is below {BAR:.0%}")
        w, g = held["wins"][subset].sum(axis=1), held["games"][subset].sum(axis=1)
        wk, gk = known["wins"].sum(axis=1), known["games"].sum(axis=1)
        point = gap_of(w, g, wk, gk, twins[subset])
        p = cols("pilot")
        raw = (ratio(w[:, p], g[:, p]) - ratio(wk[:, p], gk[:, p])[twins[subset]]).mean(axis=1)
        per["gap"] = interval(np.nanmean(point), gaps)
        per["raw_gap"] = {"mean": float(np.nanmean(raw))}
        if gated and not per["gap"]["low"] > GAP_BAR:
            broken.append(f"B3 gap on {label}: the lower bound is not above {GAP_BAR * 100:.0f} points")
        # Reported: what retraining with the unknown row bought over M1's networks, on these builds.
        per["base"] = {"mean": pooled(w, g, "base"), "by_optimizer": {k: float(ratio(W[c], G[c])) for k, c in zip(OPTIMIZERS, cols("base"))}}
        per["pilot_minus_base"] = interval(pooled(w, g, "pilot") - pooled(w, g, "base"), helps)
        numbers[label] = per
    # The training lists: Stage B's networks against M1's, and what hiding identities costs.
    wk, gk = known["wins"].sum(axis=1), known["games"].sum(axis=1)
    point = known_stats(wk, gk)
    block = {"pilot": pooled(wk, gk, "pilot"), "face": pooled(wk, gk, "face"), "base": pooled(wk, gk, "base"),
             "mirror": float(ratio(wk[:, MIRROR].sum(), gk[:, MIRROR].sum())),
             "by_optimizer": {arm: {k: float(ratio(wk[:, c].sum(), gk[:, c].sum())) for k, c in zip(OPTIMIZERS, cols(arm))}
                              for arm in ("pilot", "face", "base") + tuple(f"mask{d}" for d in DOSES)}}
    for key, value in point.items():
        block[key] = interval(value, known_draws[key]) if known_draws else {"mean": value, "low": float("nan"), "high": float("nan")}
    numbers["known"] = block
    # Readings: fixed here before any game, reported beside the verdict, and not part of it. Each fails closed.
    helped = lambda per: per.get("pilot_minus_base", {}).get("mean", float("nan")) >= HELP_BAR and per["pilot_minus_base"]["low"] > 0
    numbers["readings"] = {
        "retraining_on_all": "HELPED" if helped(numbers["all"]) else "NOT_SHOWN",
        "retraining_on_heavy": "HELPED" if helped(numbers["heavy"]) else "NOT_SHOWN",
        "known_cards": "KEPT" if block["known_minus_base"]["mean"] >= KNOWN_BAR else "LOST",
        "hidden_identities": "SMALL_COST" if block[f"mask{GATED_DOSE}_minus_pilot"]["low"] > MASK_BAR else "LARGE_COST"}
    voids = {}
    for name, arms in (("known", KNOWN_ARMS), ("held", HELD_ARMS)):
        for c, (arm, k) in enumerate(COLUMNS):
            if arm not in arms:
                continue
            void, games = float(data[name]["void"][:, :, c].sum()), float(data[name]["games"][:, :, c].sum())
            share = void / (void + games) if void + games else 1.0
            voids[f"{name}/{arm}/{k}"] = {"void": int(void), "games": int(games), "share": share}
            if share > VOID_BAR:
                broken.append(f"B4: {name}/{arm}/{k} has {share:.2%} void, above {VOID_BAR:.0%}")
    numbers["voids"] = voids
    # Coverage: every list has completed games in every arm of its block, for every optimizer and seat.
    empty = []
    for name, arms in (("known", KNOWN_ARMS), ("held", HELD_ARMS)):
        per_seat = data[name]["games"].reshape(data[name]["games"].shape[0], len(training), 2, len(COLUMNS)).sum(axis=1)
        for i, s, c in zip(*np.nonzero(per_seat == 0)):
            if COLUMNS[c][0] in arms:
                empty.append(f"{name}/{i}/seat {s}/{COLUMNS[c][0]}/{COLUMNS[c][1]}")
    numbers["integrity"] = {**integrity, "empty_list_arms": len(empty)}
    if empty:
        broken.append(f"B4: {len(empty)} list, arm, optimizer and seat combinations have no completed game, such as {empty[0]}")
    for key, value in integrity.items():
        if value:
            broken.append(f"B4: {key} is {value}")
    return numbers, broken


def usage(rows, lists):
    """How often the counted seat's player chose an option on a known card, and on a card that reads the unknown row, among
    the decisions that offered one, by the option's act. A network that treats unknown cards as blanks shows here.
    In a mask arm the unknown cards are the hidden ones. Reported only."""
    heavy = {r["name"] for r in lists if r["heavy"]}
    total = {}
    for row in rows:
        if row["status"] != "PASS" or "usage" not in (row.get("counters") or {}):
            continue
        counted = np.asarray(row["counters"]["usage"])[row["seat"]]
        wide = row["counters"]["wide_decisions"][row["seat"]]
        for key in [(row["block"], row["arm"])] + ([("held heavy", row["arm"])] if row["block"] == "held" and row["list"] in heavy else []):
            entry = total.setdefault(key, [np.zeros((2, 10, 2), dtype=np.int64), 0])
            entry[0] += counted
            entry[1] += wide
    out = {}
    for (block, arm), (counts, wide) in sorted(total.items()):
        out[f"{block}/{arm}"] = {"decisions_only_the_wider_tables_hold": int(wide), **{
            name: {kind: {"offered": int(counts[k, act, 0]), "chosen": int(counts[k, act, 1]),
                          "rate": float(counts[k, act, 1] / counts[k, act, 0]) if counts[k, act, 0] else None}
                   for k, kind in enumerate(("known", "unknown"))} for act, name in ACTS.items()}}
    return out


def reported(data, lists, training):
    """Read, not gated. One build's 168 games per optimizer don't support a claim about that build."""
    held, known = data["held"], data["known"]
    w, g = held["wins"].sum(axis=1), held["games"].sum(axis=1)
    wk, gk = known["wins"].sum(axis=1), known["games"].sum(axis=1)
    twins = np.array([training.index(r["twin"]) for r in lists])
    p, f, b = cols("pilot"), cols("face"), cols("base")
    gap = gap_of(w, g, wk, gk, twins)
    raw = (ratio(w[:, p], g[:, p]) - ratio(wk[:, p], gk[:, p])[twins]).mean(axis=1)
    pilot, face, base = (ratio(w[:, c].sum(axis=1), g[:, c].sum(axis=1)) for c in (p, f, b))
    mirror = ratio(w[:, MIRROR], g[:, MIRROR])
    out = {"per_list": {r["name"]: {"archetype": r["archetype"], "variants": len(r["variants"]), "twin": r["twin"], "distance": r["distance"],
                                    "unknown_copies": r["unknown_copies"], "unknown_distinct": r["unknown_distinct"],
                                    "pilot": float(pilot[i]), "face": float(face[i]), "mirror": float(mirror[i]), "base": float(base[i]),
                                    "gap": float(gap[i]), "raw_gap": float(raw[i])} for i, r in enumerate(lists)}}

    def summary(index):
        index = np.asarray(index, dtype=int)
        if not len(index):
            return {"lists": 0}
        return {"lists": int(len(index)), "pilot": float(ratio(w[index][:, p].sum(), g[index][:, p].sum())), "face": float(ratio(w[index][:, f].sum(), g[index][:, f].sum())),
                "base": float(ratio(w[index][:, b].sum(), g[index][:, b].sum())), "mirror": float(ratio(w[index, MIRROR].sum(), g[index, MIRROR].sum())),
                "gap": float(np.nanmean(gap[index])), "raw_gap": float(np.nanmean(raw[index]))}
    pick = lambda test: [i for i, r in enumerate(lists) if test(r)]
    out["by_unknown_main_copies"] = {band: summary(pick(lambda r: r["band"] == band)) for band in BANDS}
    out["by_distance"] = {band: summary(pick(lambda r: A.band(r["distance"]) == band)) for band in ("1-9", "10-19", "20+")}
    kinds = {"monsters only": lambda r: r["unknown_spell_trap_copies"] == 0, "spells and traps only": lambda r: r["unknown_spell_trap_copies"] == r["unknown_copies"]}
    out["by_unknown_kind"] = {name: summary(pick(test)) for name, test in kinds.items()}
    out["by_unknown_kind"]["both"] = summary(pick(lambda r: 0 < r["unknown_spell_trap_copies"] < r["unknown_copies"]))
    archetypes = sorted({r["archetype"] for r in lists})
    out["by_archetype"] = {a: summary(pick(lambda r: r["archetype"] == a)) for a in archetypes}
    out["pilot_each_archetype_equal"] = float(np.mean([out["by_archetype"][a]["pilot"] for a in archetypes]))
    by_card = {}
    for i, r in enumerate(lists):
        for code, _ in r["unknown_cards"]:
            by_card.setdefault(code, []).append(i)
    out["by_unknown_card"] = {str(code): summary(index) for code, index in sorted(by_card.items()) if len(index) >= 10}
    out["gap_spread"] = {q: float(np.nanpercentile(gap, v)) for q, v in (("p10", 10), ("p50", 50), ("p90", 90))}
    out["raw_gap_spread"] = {q: float(np.nanpercentile(raw, v)) for q, v in (("p10", 10), ("p50", 50), ("p90", 90))}
    out["known_lists"] = {t: {"pilot": float(ratio(wk[i, p].sum(), gk[i, p].sum())), "face": float(ratio(wk[i, f].sum(), gk[i, f].sum())),
                              "base": float(ratio(wk[i, b].sum(), gk[i, b].sum())), "mirror": float(ratio(wk[i, MIRROR], gk[i, MIRROR])),
                              **{f"mask{d}": float(ratio(wk[i, cols(f'mask{d}')].sum(), gk[i, cols(f'mask{d}')].sum())) for d in DOSES}}
                          for i, t in enumerate(training)}
    for block in ("known", "held"):
        a = data[block]
        seat0 = a["wins"].reshape(a["wins"].shape[0], -1, 2, len(COLUMNS))[:, :, 0, MIRROR].sum()
        seat0_games = a["games"].reshape(a["games"].shape[0], -1, 2, len(COLUMNS))[:, :, 0, MIRROR].sum()
        out[f"{block}_mirror_list_in_seat0"] = float(ratio(seat0, seat0_games))  # the list's rate when it sits in seat 0, not seat 0's rate
    return out


# ---------------------------------------------------------------- the dropout pilot

def pilot_pairs(training):
    """Every training list pilots against the next four in a fixed cycle, in both seats."""
    return [(training[i], training[(i + step) % len(training)]) for i in range(len(training)) for step in range(1, 5)]


def pilot_cells(training, rows):
    out = []
    for n, (x, o) in enumerate(pilot_pairs(training)):
        seed = PILOT_SEED + n * PILOT_COUNT
        if seed + PILOT_COUNT - 1 > PILOT_END:
            raise SystemExit("the pilot leaves its seed block")
        for setting in SETTINGS:
            for dose in PILOT_DOSES:
                hidden = None if dose == "0" else hidden_ids(rows[x], dose, seed, PILOT_COUNT)
                for seat in SEATS:
                    d0, d1 = (x, o) if seat == 0 else (o, x)
                    out.append({"setting": setting, "dose": dose, "list": x, "opponent": o, "deck0": d0, "deck1": d1, "seat": seat, "seed": seed,
                                "games": PILOT_COUNT, "players": [f"pilot:{setting}", "heuristic"] if seat == 0 else ["heuristic", f"pilot:{setting}"],
                                "hidden": hidden})
    return out


def choose(rates):
    """The pilot's rule. rates[setting][dose] is a win rate against the heuristic on the training lists.

    A dropout setting is admissible if, with nothing hidden, it is at most ADMIT below the run without dropout.
    Among admissible settings the higher mean of the dose 5 and dose all rates wins; within TIE, the milder S1 does.
    With no admissible setting there is no pick, and no training under this protocol."""
    admissible = [s for s in ("S1", "S2") if rates[s]["0"] >= rates["S0"]["0"] - ADMIT]
    score = {s: (rates[s]["5"] + rates[s]["all"]) / 2 for s in SETTINGS}
    if not admissible:
        pick = None
    elif admissible == ["S1", "S2"]:
        pick = "S2" if score["S2"] > score["S1"] + TIE else "S1"
    else:
        pick = admissible[0]
    return {"pick": pick, "admissible": admissible, "hidden_score": score,
            "dropout_helps_when_hidden": {s: score[s] - score["S0"] for s in ("S1", "S2")}}


def train_command(output, optimizer, rounds, setting):
    clean, top = SETTINGS[setting]
    config = {**TRAIN, "optimizer_seed": optimizer, "rounds": rounds, "id_dropout_clean": clean, "id_dropout_max": top}
    command = [GPU_PYTHON, str(ROOT / "tools/edison/learner/train.py"), "--output", str(output), "--workers", "5", "--learner-device", "gpu",
               "--worker-python", PYTHON]
    for key, value in config.items():
        command += ["--" + key.replace("_", "-"), str(value)]
    return command


def run_training(runs):
    """Start the runs side by side and wait. A run that exists is resumed, and train.py refuses a resume on other inputs."""
    RUNS.mkdir(parents=True, exist_ok=True)
    started = []
    for name, command in runs:
        output = RUNS / name
        if (output / "state.json").exists():
            command = command + ["--resume"]
        log = (RUNS / f"{name}.log").open("ab")
        started.append((name, subprocess.Popen(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, env={**os.environ, "EDISON_CODE_LIST": "v2"})))
    codes = {name: process.wait() for name, process in started}
    print(json.dumps(codes))
    return 1 if any(codes.values()) else 0


def run_state(name, optimizer, rounds, setting, manifest):
    """A finished run, and proof that it is this manifest's: trained at its commit, on its files, with exactly its settings."""
    state = json.loads((RUNS / name / "state.json").read_text())
    run = json.loads((RUNS / name / "run.json").read_text())
    if state["round"] != rounds:
        raise SystemExit(f"{name} stopped at round {state['round']}")
    clean, top = SETTINGS[setting]
    want = {**TRAIN, "row_grouping": True, "optimizer_seed": optimizer, "rounds": rounds, "id_dropout_clean": clean, "id_dropout_max": top}
    files, prov = manifest["inputs"]["files"], run["provenance"]
    problems = [key for key in sorted(set(want) | set(run["config"])) if run["config"].get(key) != want.get(key)]
    problems += [name_ for name_, digest in prov["learner_sources"].items() if files.get(f"tools/edison/learner/{name_}") != digest]
    if prov["git_commit"] != git("rev-parse", "HEAD") or prov["code_list"] != "v2" or prov["codes_sha256"] != CODES_SHA \
            or prov["edison_policy"] != files["ygoai/edison_policy.py"] or prov["native_binaries"].get(NATIVE.name) != manifest["inputs"]["native"] \
            or [prov["jax"], prov["numpy"]] != manifest["inputs"]["versions"]["learner"][1:3]:
        problems.append("provenance")
    if run["workers"] != 5 or run["learner_device"] != "gpu":
        problems.append("workers or device")
    if sha(str(state["current"]["stem"]) + ".msgpack") != state["current"]["sha256"]:
        problems.append("checkpoint")
    if problems:
        raise SystemExit(f"{name} is not this manifest's run: {problems}")
    return state, run


def pilot_train():
    check_frozen("manifest")
    if (PILOT / "pilot-result.json").exists():
        raise SystemExit("the pilot has a result already")
    attempt("pilot-train")
    return run_training([(f"pilot-{s}", train_command(RUNS / f"pilot-{s}", PILOT_OPTIMIZER, PILOT_ROUNDS, s)) for s in SETTINGS])


def pilot(workers):
    from actor import Pool
    manifest = check_frozen("manifest")
    if (PILOT / "pilot-result.json").exists():
        raise SystemExit("the pilot has a result already")
    training = sorted(deck_registry.registry())
    jobs = pilot_cells(training, identity_rows(training))
    if text_sha("\n".join(json.dumps(j, sort_keys=True) for j in jobs)) != manifest["pilot_jobs"]:
        raise SystemExit("the pilot's jobs differ from the manifest")
    specs = {"heuristic": {"kind": "heuristic"}}
    record = {}
    for s in SETTINGS:
        state, run = run_state(f"pilot-{s}", PILOT_OPTIMIZER, PILOT_ROUNDS, s, manifest)
        specs[f"pilot:{s}"] = net_spec(state["current"]["stem"], state["current"]["sha256"])
        record[s] = {"params_sha256": state["current"]["sha256"], "duels": state["duels"], "void_games": state["void_games"]}
    PILOT.mkdir(parents=True, exist_ok=True)
    # The journal is named after the networks it played, so results from other networks are never read as these.
    played = text_sha(json.dumps([manifest["pilot_jobs"], {s: record[s]["params_sha256"] for s in SETTINGS}], sort_keys=True))
    journal = PILOT / f"pilot-{played[:16]}.jsonl"
    attempt("pilot", networks={s: record[s]["params_sha256"] for s in SETTINGS}, journal=journal.name)
    done, _ = read_journal(journal)
    pool = Pool(workers, PILOT / "logs", cell_timeout=1800)
    try:
        run_tasks(pool, [(f"first:{i}", worker_job(j, specs)) for i, j in enumerate(jobs)], journal, done)
    finally:
        pool.close()
    results, unresolved = read_journal(journal)
    wins, games, void = Counter(), Counter(), Counter()
    for i, job in enumerate(jobs):
        r, key = results[f"first:{i}"], (job["setting"], job["dose"])
        if r.get("status") != "PASS" or [d["seed"] for d in r["duels"]] != list(range(job["seed"], job["seed"] + job["games"])) \
                or r.get("hidden_rows") != job["hidden"]:
            void[key] += job["games"]
            continue
        wins[key] += sum(d["winner"] == job["seat"] for d in r["duels"])
        games[key] += len(r["duels"])
    rates = {s: {d: wins[s, d] / games[s, d] if games[s, d] else float("nan") for d in PILOT_DOSES} for s in SETTINGS}
    # A cell is a function of its seeds and networks, so one that was interrupted and run again counts. The count is reported.
    valid = all(games[s, d] and void[s, d] <= VOID_BAR * (void[s, d] + games[s, d]) for s in SETTINGS for d in PILOT_DOSES)
    out = {"manifest_sha256": sha(MANIFEST), "jobs_sha256": text_sha("\n".join(json.dumps(j, sort_keys=True) for j in jobs)),
           "journal": journal.name, "journal_sha256": sha(journal), "other_journals": sorted(p.name for p in PILOT.glob("pilot-*.jsonl") if p != journal),
           "runs": record, "rates": rates, "games": {f"{s}/{d}": games[s, d] for s in SETTINGS for d in PILOT_DOSES},
           "void_games": {f"{s}/{d}": void[s, d] for s in SETTINGS for d in PILOT_DOSES if void[s, d]}, "interrupted_cells": sum(unresolved.values()),
           "valid": valid, **(choose(rates) if valid else {"pick": None})}
    (PILOT / "pilot-result.json").write_text(json.dumps(out, indent=1) + "\n")
    print(json.dumps({k: out[k] for k in ("rates", "valid", "pick")}, indent=1))
    return 0 if out["pick"] else 1


def picked():
    result = json.loads((PILOT / "pilot-result.json").read_text())
    if result["manifest_sha256"] != sha(MANIFEST) or not result["pick"]:
        raise SystemExit("no pilot pick on this manifest")
    return result


def train():
    check_frozen("manifest")
    setting = picked()["pick"]
    attempt("train", setting=setting)
    return run_training([(f"main-{k}", train_command(RUNS / f"main-{k}", int(k), TRAIN["rounds"], setting)) for k in OPTIMIZERS])


def seal():
    """Bind the pilot's pick and the trained networks to the manifest. Nothing is chosen here: the round is fixed and all three count."""
    manifest = check_frozen("manifest")
    if SEAL.exists() or any(BUILD.glob("stage-b-*")):
        raise SystemExit("a seal or results exist already")
    result = picked()
    head = git("rev-parse", "HEAD")
    checkpoints = {}
    for k in OPTIMIZERS:
        state, run = run_state(f"main-{k}", int(k), TRAIN["rounds"], result["pick"], manifest)
        stem = RUNS / f"main-{k}/checkpoint-{TRAIN['rounds']:04d}"
        if str(stem) != state["current"]["stem"]:
            raise SystemExit(f"main-{k}'s last checkpoint is not round {TRAIN['rounds']}")
        checkpoints[k] = {"params_sha256": state["current"]["sha256"], "duels": state["duels"], "void_games": state["void_games"],
                          "config_hash": run["config_hash"], "rounds_log_sha256": sha(RUNS / f"main-{k}/rounds.jsonl")}
    attempt("seal", setting=result["pick"])
    SEAL.write_text(json.dumps({"manifest_sha256": sha(MANIFEST), "manifest_commit": head, "pilot": result, "setting": result["pick"],
                                "checkpoints": checkpoints, "attempts": [json.loads(line) for line in ATTEMPTS.read_text().splitlines()]},
                               indent=1) + "\n")
    print(json.dumps({"setting": result["pick"], "checkpoints": {k: v["params_sha256"][:16] for k, v in checkpoints.items()}}, indent=1))


# ---------------------------------------------------------------- selftest

def made_up(held_rows, training, rates, void=None, seed=1):
    """Rows in the real format, from the real job builder, with wins drawn at the given rates.
    rates(block, arm, policy, list, seat) -> the chance that the counted seat wins."""
    rng = np.random.default_rng(seed)
    rows = []
    fake_rows = {t: list(range(1, 31)) for t in training}
    for i, job in enumerate(cells([r["name"] for r in held_rows], training, fake_rows)):
        base = {"cell": i, "job_sha256": text_sha(job_text(job)), **{k: job[k] for k in JOB_FIELDS if k != "hidden"}, "hidden_rows": job["hidden"],
                "unresolved_first": 0, "unresolved_replay": 0, "unresolved_starts": 0}
        if void and void(job):
            rows.append({**base, "status": "VOID", "reason": "made up", "duels": None})
            continue
        p = rates(job["block"], job["arm"], job["policy"], job["list"], job["seat"])
        wins = rng.random(job["games"]) < p
        rows.append({**base, "status": "PASS", "duels": [{"seed": job["seed"] + e, "winner": job["seat"] if w else 1 - job["seat"]} for e, w in enumerate(wins)]})
    return rows


def selftest():
    training = [f"t{i:02d}" for i in range(21)]
    held = [{"name": f"h{i:03d}", "twin": training[i % 21], "heavy": i % 4 == 0, "archetype": f"a{i % 5}", "band": "6-10" if i % 4 == 0 else "1-2",
             "distance": 20 if i % 4 == 0 else 5, "variants": ["x"], "unknown_copies": 7 if i % 4 == 0 else 1, "unknown_distinct": 3 if i % 4 == 0 else 1,
             "unknown_spell_trap_copies": i % 2, "unknown_cards": [[100 + i % 7, 1]]} for i in range(120)]
    names = [r["name"] for r in held]
    clean = {"missing_chunks": 0, "invalid_chunks": 0, "replay_mismatches": 0}
    small = [dict(r, heavy=r["name"] in ("h000", "h004")) for r in held]

    def make(pilot=0.75, face=0.75, mirror=0.5, known=0.8, known_mirror=0.5, heavy_pilot=None, seat1=None, opt=None, rows=held, base=0.7,
             known_base=0.8, mask=None):
        heavy = {r["name"] for r in rows if r["heavy"]}
        mask = mask or {"2": 0.78, "5": 0.75, "10": 0.72, "all": 0.6}

        def rates(block, arm, k, x, s):
            if arm == "mirror":
                return known_mirror if block == "known" else mirror
            if arm == "base":
                return known_base if block == "known" else base
            if arm.startswith("mask"):
                return mask[arm[4:]]
            if block == "known":
                return known if arm == "pilot" else 0.8
            p = pilot if arm == "pilot" else face
            if arm == "pilot" and heavy_pilot is not None and x in heavy:
                p = heavy_pilot
            if s == 1 and seat1 is not None and arm == "pilot":
                p = seat1
            if opt and k == opt[0] and arm == "pilot":
                p = opt[1]
            return p
        return rates
    cases = [("strong everywhere", make(), None, clean, held, None),
             ("pilot mean below the bar", make(pilot=0.53, known=0.53, known_base=0.53, mask={"2": 0.53, "5": 0.52, "10": 0.5, "all": 0.45}), None, clean, held, "B1 pilot on all: the mean"),
             ("one optimizer weak", make(opt=("1703", 0.49)), None, clean, held, "B1 pilot on all: optimizer 1703"),
             ("one seat weak", make(seat1=0.45), None, clean, held, "B1 pilot on all: seat 1"),
             ("face weak", make(face=0.5), None, clean, held, "B2 face on all"),
             ("held-out far below known", make(pilot=0.60, known=0.85, known_base=0.85, mask={"2": 0.84, "5": 0.82, "10": 0.8, "all": 0.7}), None, clean, held, "B3 gap on all"),
             ("only heavy builds fall behind", make(heavy_pilot=0.58), None, clean, held, "B3 gap on heavy"),
             ("a weaker build, for everyone", make(pilot=0.62, mirror=0.37, known=0.8, known_mirror=0.55), None, clean, held, None),
             ("an empty mask cell on a training list", make(), lambda j: j["arm"] == "mask5" and j["list"] == "t03" and j["seat"] == 0 and j["policy"] == "1702", clean, held, "B4: "),
             ("too many voids in one arm", make(), lambda j: j["arm"] == "face" and j["policy"] == "1702" and j["seed"] % 50 < 2 and j["block"] == "held", clean, held, "B4: held/face/1702"),
             ("too many voids in a mask arm", make(), lambda j: j["arm"] == "mask10" and j["policy"] == "1704" and j["seed"] % 400 < 32, clean, held, "B4: known/mask10/1704"),
             ("one build has no face game", make(), lambda j: j["arm"] == "face" and j["list"] == "h000", clean, held, "B4: "),
             ("a replay mismatch", make(), None, {**clean, "replay_mismatches": 1}, held, "B4: replay_mismatches"),
             ("a chunk missing", make(), None, {**clean, "missing_chunks": 3}, held, "B4: missing_chunks"),
             ("a chunk that fails its checks", make(), None, {**clean, "invalid_chunks": 1}, held, "B4: invalid_chunks"),
             ("heavy set too small to gate", make(heavy_pilot=0.3, rows=small), None, clean, small, None)]
    failed = 0

    def check(name, ok, detail=""):
        nonlocal failed
        print(f"{'pass' if ok else 'FAIL'}: {name} {detail}")
        failed += not ok
    for name, rates, void, integrity, rows_in, expected in cases:
        data = arrays(made_up(rows_in, training, rates, void), [r["name"] for r in rows_in], training)
        numbers, broken = decide(data, rows_in, training, integrity, resamples=600)
        ok = not broken if expected is None else any(b.startswith(expected) for b in broken)
        if name == "one build has no face game":
            ok = ok and any("no completed game" in b for b in broken) and not any("void, above" in b for b in broken)
        if name == "an empty mask cell on a training list":
            ok = ok and any("known/3/seat 0/mask5/1702" in b for b in broken)
        if name == "strong everywhere":
            ok = ok and numbers["readings"] == {"retraining_on_all": "HELPED", "retraining_on_heavy": "HELPED", "known_cards": "KEPT", "hidden_identities": "SMALL_COST"}
        check(name, ok, str(broken[:3]))
    # The readings. None changes the verdict, and each turns on its own number.
    def readings(**kw):
        numbers, broken = decide(arrays(made_up(held, training, make(**kw)), names, training), held, training, clean, resamples=600)
        return numbers["readings"], broken, numbers
    got, broken, _ = readings(known=0.72, known_base=0.80, pilot=0.72, mask={"2": 0.71, "5": 0.70, "10": 0.69, "all": 0.6})
    check("reading: known-card strength lost", got["known_cards"] == "LOST" and got["hidden_identities"] == "SMALL_COST" and not broken, str(got))
    got, broken, _ = readings(mask={"2": 0.79, "5": 0.78, "10": 0.60, "all": 0.4})
    check("reading: the hidden-identity cost is read at dose 5, not at 10", got["hidden_identities"] == "SMALL_COST" and not broken, str(got))
    got, broken, _ = readings(mask={"2": 0.79, "5": 0.66, "10": 0.78, "all": 0.7})
    check("reading: hiding five identities costs too much", got["hidden_identities"] == "LARGE_COST" and got["known_cards"] == "KEPT" and not broken, str(got))
    got, _, numbers = readings(mask={"2": 0.79, "5": 0.7015, "10": 0.69, "all": 0.6})
    cost = numbers["known"]["mask5_minus_pilot"]
    check("reading: the hidden-identity cost is read at its lower bound, not its mean", cost["low"] < MASK_BAR < cost["mean"] and got["hidden_identities"] == "LARGE_COST", str(cost))
    got, _, _ = readings(pilot=0.75, base=0.70, heavy_pilot=0.70)
    check("reading: retraining is read separately on the heavy set", got["retraining_on_all"] == "HELPED" and got["retraining_on_heavy"] == "NOT_SHOWN", str(got))
    # The reported reading of the baseline: retraining helped only when the difference is large and its interval excludes zero.
    numbers, _ = decide(arrays(made_up(held, training, make(pilot=0.75, base=0.70)), names, training), held, training, clean, resamples=600)
    same, _ = decide(arrays(made_up(held, training, make(pilot=0.75, base=0.745)), names, training), held, training, clean, resamples=600)
    check("the baseline reading", numbers["readings"]["retraining_on_all"] == "HELPED" and same["readings"]["retraining_on_all"] == "NOT_SHOWN",
          f"{numbers['all']['pilot_minus_base']} {same['all']['pilot_minus_base']}")
    check("the known block's differences", abs(numbers["known"]["mask5_minus_pilot"]["mean"] + 0.05) < 0.01 and abs(numbers["known"]["known_minus_base"]["mean"]) < 0.01
          and numbers["known"]["mask5_minus_pilot"]["low"] < numbers["known"]["mask5_minus_pilot"]["mean"] < numbers["known"]["mask5_minus_pilot"]["high"],
          str(numbers["known"]["mask5_minus_pilot"]))
    # Voids under the 1% bar that would decide a bare margin: the worst case fails while B1 and the void rule pass.
    data = arrays(made_up(held, training, make(pilot=0.555, known=0.6, known_base=0.6, mask={"2": 0.6, "5": 0.59, "10": 0.58, "all": 0.5})), names, training)
    data["held"]["void"][:, :, cols("pilot")] = data["held"]["games"][:, :, cols("pilot")] * 0.0101
    numbers, broken = decide(data, held, training, clean, resamples=600)
    check("voids decide a bare margin", any(b.startswith("B4 pilot on all: counting void") for b in broken) and not any(b.startswith(("B1", "B4: held")) for b in broken),
          f"mean {numbers['all']['pilot']['mean']:.4f}, worst {numbers['all']['pilot']['worst_case_mean']:.4f}")
    data = arrays(made_up(held, training, make(pilot=0.62, mirror=0.37, known=0.8, known_mirror=0.55)), names, training)
    numbers, _ = decide(data, held, training, clean, resamples=600)
    check("the mirror takes out a build's strength", numbers["all"]["raw_gap"]["mean"] < -0.15 and abs(numbers["all"]["gap"]["mean"]) < 0.03,
          f"raw {numbers['all']['raw_gap']['mean']:.3f}, gap {numbers['all']['gap']['mean']:.3f}")
    rows = [{"block": "held", "arm": "pilot", "policy": "1702", "list": "h000", "opponent": "t00", "seat": 1, "games": 3, "status": "PASS",
             "duels": [{"winner": 1}, {"winner": 2}, {"winner": 0}], "unresolved_starts": 1},
            {"block": "held", "arm": "pilot", "policy": "1702", "list": "h000", "opponent": "t00", "seat": 1, "games": 4, "status": "VOID"}]
    a = arrays(rows, names, training)["held"]
    got = [int(a[key][0, 1, COL["pilot", "1702"]]) for key in ("wins", "games", "void", "suspect")]
    check("draws, voids and interrupted cells", got == [1, 3, 4, 1], str(got))
    out = reported(arrays(made_up(held, training, make()), names, training), held, training)
    check("the report's cuts cover every build once", sum(v["lists"] for v in out["by_unknown_main_copies"].values()) == len(held)
          and sum(v["lists"] for v in out["by_unknown_kind"].values()) == len(held) and sum(v["lists"] for v in out["by_archetype"].values()) == len(held))
    # The pilot's rule.
    r = lambda s0, s1, s2: {"S0": dict(zip(PILOT_DOSES, s0)), "S1": dict(zip(PILOT_DOSES, s1)), "S2": dict(zip(PILOT_DOSES, s2))}
    for name, rates, want in (("the milder setting on a tie", r((.67, .5, .3), (.66, .6, .5), (.66, .61, .5)), "S1"),
                              ("the stronger setting when it is clearly better hidden", r((.67, .5, .3), (.66, .58, .45), (.65, .63, .55)), "S2"),
                              ("a setting that costs too much is out", r((.67, .5, .3), (.66, .58, .45), (.62, .64, .58)), "S1"),
                              ("only the stronger is admissible", r((.67, .5, .3), (.60, .58, .45), (.65, .6, .5)), "S2"),
                              ("no setting is admissible", r((.67, .5, .3), (.60, .58, .45), (.61, .6, .5)), None)):
        check(f"pilot: {name}", choose(rates)["pick"] == want, str(choose(rates)["pick"]))
    # Usage: the counted seat only, and heavy builds again under their own key.
    grid = np.zeros((2, 2, 10, 2), dtype=int)
    grid[1, 1, 8], grid[1, 0, 8], grid[0, 1, 8] = (10, 2), (10, 7), (99, 99)
    cell = lambda name, seat: {"status": "PASS", "block": "held", "arm": "pilot", "list": name, "seat": seat, "counters": {"usage": grid.tolist(), "wide_decisions": [5, 1]}}
    got = usage([cell("h000", 1), cell("h001", 1), {"status": "VOID", "block": "held", "arm": "pilot", "list": "h000", "seat": 1}], held)
    check("usage counts the counted seat, and heavy builds on their own", got["held/pilot"]["activate"]["unknown"] == {"offered": 20, "chosen": 4, "rate": 0.2}
          and got["held heavy/pilot"]["activate"]["known"] == {"offered": 10, "chosen": 7, "rate": 0.7} and got["held/pilot"]["decisions_only_the_wider_tables_hold"] == 2
          and got["held/pilot"]["Set"]["unknown"]["rate"] is None, str(got["held/pilot"]["activate"]))
    # Labels become the right checkpoints, and hidden rows become the worker's identity spec.
    import tempfile
    global RUNS
    real_runs = RUNS
    with tempfile.TemporaryDirectory() as tmp:
        RUNS = Path(tmp)
        seal_ = {"checkpoints": {}}
        for k in OPTIMIZERS:
            (RUNS / f"main-{k}").mkdir()
            (RUNS / f"main-{k}/checkpoint-{TRAIN['rounds']:04d}.json").write_text(json.dumps({"params_sha256": f"new{k}"}))
            seal_["checkpoints"][k] = {"params_sha256": f"new{k}"}
        try:
            specs = specs_from_seal(seal_)
            ok = all(specs[f"v2:{k}"]["sha256"] == f"new{k}" and "v1_into_v2" not in specs[f"v2:{k}"] and f"main-{k}/" in specs[f"v2:{k}"]["checkpoint"]
                     and specs[f"v1:{k}"] == {"kind": "net", "checkpoint": str(V1_RUNS / f"gauntlet-{k}/checkpoint-2560"), "sha256": V1_PARAMS_SHA[k],
                                              "greedy": True, "repeat_guard": True, "v1_into_v2": True} for k in OPTIMIZERS) and specs["heuristic"] == {"kind": "heuristic"}
            seal_["checkpoints"]["1703"]["params_sha256"] = "other"
            try:
                specs_from_seal(seal_)
                ok = False
            except SystemExit:
                pass
        finally:
            RUNS = real_runs
    check("labels bind to the sealed Stage B networks and to M1's baseline, and a wrong hash refuses", ok)
    job = {"deck0": "a", "deck1": "b", "seed": 5, "games": 2, "players": ["heuristic", "v1:1702"], "hidden": [[3], [4, 5]]}
    plain = worker_job({**job, "hidden": None}, specs, threads=4)
    check("a worker job carries its players, its hidden rows and nothing else",
          worker_job(job, specs) == {"deck0": "a", "deck1": "b", "seed": 5, "games": 2, "policies": [specs["heuristic"], specs["v1:1702"]], "identity": {"unknown_ids": [[3], [4, 5]]}}
          and "identity" not in plain and plain["threads"] == 4)
    failed += selftest_records(held, training)
    failed += A.selftest_journal()
    failed += selftest_matrix(held, training)
    return 1 if failed else 0


def selftest_records(held, training):
    """What validate accepts, on records made from the real job builder."""
    fake_rows = {t: list(range(1, 31)) for t in training}
    jobs = cells([r["name"] for r in held[:16]], training, fake_rows)
    failed = 0
    for n in (3, 21):  # a known chunk, with mask arms, and a held-out chunk
        indices = chunks(jobs)[n]
        part = [jobs[i] for i in indices]
        rows = {r["cell"]: r for r in made_up(held[:16], training, lambda *a: 0.6) if r["cell"] in set(indices)}
        results = {f"first:{i}": {"status": "PASS", "duels": rows[i]["duels"], "counters": {}, **({"hidden_rows": jobs[i]["hidden"]} if jobs[i]["hidden"] else {})}
                   for i in indices}
        results |= {f"replay:{i}": dict(results[f"first:{i}"]) for i in replayed(indices)}
        good = assemble(n, part, indices, results, Counter())

        def expect(name, record, problem=True, mismatch=0):
            nonlocal failed
            problems, mismatches = validate(record, part, indices)
            ok = bool(problems) == problem and (problem or mismatches == mismatch)
            print(f"{'pass' if ok else 'FAIL'}: chunk {n}: {name}: {problems[:2]} {mismatches}")
            failed += not ok
        copy = lambda: json.loads(json.dumps(good))
        expect("a well-formed chunk", good, problem=False)
        swap = copy()
        swap["rows"][0], swap["rows"][1] = swap["rows"][1], swap["rows"][0]
        expect("two rows swapped", swap)
        wrong = copy()
        wrong["rows"][5]["duels"][0]["seed"] -= 1
        expect("a wrong duel seed", wrong)
        short = copy()
        short["rows"][7]["duels"] = short["rows"][7]["duels"][:-1]
        expect("a short PASS row", short)
        players = copy()
        players["rows"][2]["players"] = players["rows"][2]["players"][::-1]
        expect("a row with the players swapped", players)
        missing = copy()
        missing["replays"] = []
        expect("replays missing", missing, problem=bool(replayed(indices)))
        if replayed(indices):
            differ = copy()
            differ["replays"][0]["duels"] = differ["replays"][0]["duels"][::-1]
            expect("a replay that differs is counted", differ, problem=False, mismatch=1)
        else:
            print(f"FAIL: test chunk {n} has no scheduled replay")
            failed += 1
        dropped = copy()
        del dropped["rows"][4]["unresolved_first"]
        expect("a row without its interrupted attempts", dropped)
        masked = [i for i, row in enumerate(good["rows"]) if row["arm"].startswith("mask")]
        if n == 3:
            if not masked:
                print("FAIL: the known chunk has no mask arm")
                failed += 1
            else:
                other = copy()
                other["rows"][masked[0]]["hidden_rows"] = [[1, 2]] * other["rows"][masked[0]]["games"]
                expect("a mask cell that hid other identities", other)
                none = copy()
                none["rows"][masked[0]]["hidden_rows"] = None
                expect("a mask cell that hid nothing", none)
            hid = copy()
            plain = next(i for i, row in enumerate(good["rows"]) if row["arm"] == "pilot")
            hid["rows"][plain]["hidden_rows"] = [[1]] * hid["rows"][plain]["games"]
            expect("a pilot cell that hid an identity", hid)
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            journal = Path(tmp) / "c.jsonl"
            journal.write_text("".join(json.dumps({"event": e, "key": k, **({"result": v} if e == "result" else {})}) + "\n"
                                       for k, v in results.items() for e in ("start", "result")))
            record = {**assemble(n, part, indices, *read_journal(journal)), "journal_sha256": sha(journal)}
            problems, _ = check_chunk(n, record, part, indices, journal)
            edited = json.loads(json.dumps(record))
            edited["rows"][0]["status"], edited["rows"][0]["duels"] = "VOID", None
            problems2, _ = check_chunk(n, edited, part, indices, journal)
            gone, _ = check_chunk(n, record, part, indices, Path(tmp) / "none.jsonl")
            ok = not problems and bool(problems2) and bool(gone)
            print(f"{'pass' if ok else 'FAIL'}: chunk {n}: a chunk must match its journal: {problems[:1]} {problems2[:1]} {gone[:1]}")
            failed += not ok
    return failed


def selftest_matrix(held, training):
    names = [r["name"] for r in held]
    fake_rows = {t: list(range(1, 31)) for t in training}
    jobs = cells(names, training, fake_rows)
    failed = 0

    def check(name, ok, detail=""):
        nonlocal failed
        print(f"{'pass' if ok else 'FAIL'}: {name} {detail}")
        failed += not ok
    per_held = len(training) * 2 * (3 * len(OPTIMIZERS) + 1)
    per_known = len(training) * 2 * ((3 + len(DOSES)) * len(OPTIMIZERS) + 1)
    check("cell count", len(jobs) == per_held * len(names) + per_known * len(training), str(len(jobs)))
    check("every job's column exists, and only its block's arms", all((j["arm"], j["policy"]) in COL and j["arm"] in (KNOWN_ARMS if j["block"] == "known" else HELD_ARMS) for j in jobs))
    key = lambda j: (j["block"], j["list"], j["opponent"], j["seed"], j["games"])
    by_arm = {arm: {key(j) for j in jobs if j["arm"] == arm} for arm in KNOWN_ARMS}
    check("pilot, face, mirror and base share lists and seeds", by_arm["pilot"] == by_arm["face"] == by_arm["mirror"] == by_arm["base"])
    known_pilot = {k for k in by_arm["pilot"] if k[0] == "known"}
    check("mask arms replay the known block's pilot cells", all(by_arm[f"mask{d}"] == known_pilot for d in DOSES))
    ok = all((j["deck0"] if j["seat"] == 0 else j["deck1"]) == (j["opponent"] if j["arm"] == "face" else j["list"]) for j in jobs)
    check("the counted seat holds the right deck", ok)
    label = lambda j: "heuristic" if j["arm"] == "mirror" else f"v1:{j['policy']}" if j["arm"] == "base" else f"v2:{j['policy']}"
    check("the counted seat holds the right player", all(j["players"][j["seat"]] == label(j) and j["players"][1 - j["seat"]] == "heuristic" for j in jobs))
    check("only mask arms hide identities", all((j["hidden"] is not None) == j["arm"].startswith("mask") for j in jobs))
    masks = [j for j in jobs if j["arm"].startswith("mask")]
    sizes = {j["arm"]: {len(h) for h in j["hidden"]} for j in masks}
    check("a mask hides its dose", sizes == {"mask2": {2}, "mask5": {5}, "mask10": {10}, "maskall": {30}}, str(sizes))
    check("one hidden set per game, from the piloted list", all(len(j["hidden"]) == j["games"] and all(set(h) <= set(fake_rows[j["list"]]) for h in j["hidden"]) for j in masks))
    same = {}
    for j in masks:
        same.setdefault((j["arm"], j["list"], j["opponent"]), []).append(j["hidden"])
    check("a cell hides the same identities for every optimizer and seat", all(all(h == group[0] for h in group) for group in same.values()))
    check("games in a cell hide different identities", any(j["hidden"][0] != j["hidden"][1] for j in masks if j["arm"] == "mask5"))
    by_dose = {(j["arm"], j["list"], j["opponent"]): j["hidden"] for j in masks}
    check("a larger dose hides everything a smaller one does", all(set(a) <= set(b) for (arm, x, o), small in by_dose.items() if arm != "maskall"
                                                                 for big in [by_dose[{"mask2": "mask5", "mask5": "mask10", "mask10": "maskall"}[arm], x, o]]
                                                                 for a, b in zip(small, big)))
    spans = sorted({(j["seed"], j["seed"] + j["games"] - 1) for j in jobs})
    check("blocks don't overlap", all(a[1] < b[0] for a, b in zip(spans, spans[1:])))
    real = cells([f"x{i}" for i in range(1190)], training, fake_rows)
    check("1,190 builds still fit the held-out block", max(j["seed"] + j["games"] - 1 for j in real if j["block"] == "held") <= HELD_END)
    check("the known block fits", max(j["seed"] + j["games"] - 1 for j in real if j["block"] == "known") <= KNOWN_END)
    pilot_jobs = pilot_cells(training, fake_rows)
    blocks = [(HELD_SEED, HELD_END), (KNOWN_SEED, KNOWN_END), (SOAK_SEED, SOAK_END), (PILOT_SEED, PILOT_END), (LIVE_SEED, LIVE_SEED + 99),
              (TRAIN_SEED, TRAIN_SEED + 299999)] + list(OTHER_BLOCKS.values())
    check("the seed blocks are disjoint", all(a[1] < b[0] or b[1] < a[0] for i, a in enumerate(blocks) for b in blocks[i + 1:]))
    check("the pilot stays in its block", all(PILOT_SEED <= j["seed"] and j["seed"] + j["games"] - 1 <= PILOT_END for j in pilot_jobs), str(len(pilot_jobs)))
    check("the pilot plays every setting and dose on the same games", len(pilot_jobs) == 84 * 2 * len(SETTINGS) * len(PILOT_DOSES)
          and len({(j["deck0"], j["deck1"], j["seed"]) for j in pilot_jobs}) == 84 * 2)
    picks = [jobs[i] for i in replayed(range(len(jobs)))]
    cover = {(j["block"], j["arm"], j["seat"]) for j in picks}
    want = {(b, a, s) for b, arms in (("known", KNOWN_ARMS), ("held", HELD_ARMS)) for a in arms for s in SEATS}
    check("replays cover both blocks, every arm and both seats", cover == want, str(len(picks)))
    groups = chunks(jobs)
    check("chunks cover every cell once", sorted(i for c in groups for i in c) == list(range(len(jobs))), str([len(c) for c in groups[:2] + groups[-2:]]))
    return failed


# ---------------------------------------------------------------- provenance

VERSIONS = {}


def versions(python):
    """[Python, JAX, NumPy, Flax] of an interpreter: the actors' and the GPU learner's."""
    if python not in VERSIONS:
        code = "import sys, jax, numpy, flax; print(sys.version.split()[0], jax.__version__, numpy.__version__, flax.__version__)"
        VERSIONS[python] = subprocess.run([python, "-c", code], check=True, capture_output=True, text=True,
                                          env={**os.environ, "JAX_PLATFORMS": "cpu"}).stdout.split()
    return VERSIONS[python]


def inputs(jobs, pilot_jobs):
    extra = json.loads(EXTRA.read_text())
    registry = deck_registry.registry()
    lua = hashlib.sha256()
    for path in sorted((ROOT / "scripts/script").resolve().glob("*.lua")):
        lua.update(path.name.encode() + b"\0" + path.read_bytes())
    wrapper = hashlib.sha256()
    for path in sorted((ROOT / "ygoenv/ygoenv").rglob("*.py")) + sorted((ROOT / "ygoai").glob("*.py")):
        wrapper.update(str(path.relative_to(ROOT)).encode() + b"\0" + path.read_bytes())
    return {"native": sha(NATIVE), "database": sha(deck_registry.DB), "lua_scripts": lua.hexdigest(),
            "files": {p: sha(ROOT / p) for p in DEPENDS}, "python_packages": wrapper.hexdigest(), "build": str(BUILD),
            "versions": {"actors": versions(PYTHON), "learner": versions(GPU_PYTHON)},
            "v1_checkpoints": {k: [sha(V1_RUNS / f"gauntlet-{k}/checkpoint-2560.msgpack"), sha(V1_RUNS / f"gauntlet-{k}/checkpoint-2560.json")] for k in OPTIMIZERS},
            "decks": {name: sha(path) for name, path in sorted({**registry, **extra}.items())},
            "jobs": text_sha("\n".join(job_text(j) for j in jobs)), "cells": len(jobs), "games": sum(j["games"] for j in jobs),
            "pilot_jobs": text_sha("\n".join(json.dumps(j, sort_keys=True) for j in pilot_jobs)),
            "training": {name: train_command(Path(name), optimizer, rounds, setting)[4:] for name, optimizer, rounds, setting in
                         [(f"pilot-{s}", PILOT_OPTIMIZER, PILOT_ROUNDS, s) for s in SETTINGS] + [(f"main-{k}-{s}", int(k), TRAIN["rounds"], s) for k in OPTIMIZERS for s in ("S1", "S2")]}}


def freeze():
    if sha(NATIVE) != NATIVE_SHA:
        raise SystemExit("the native module is not the certified one")
    if git("status", "--porcelain", "--untracked-files=no"):
        raise SystemExit("commit everything first: the manifest names one commit")
    if any(BUILD.glob("stage-b-*")) or PILOT.exists() or RUNS.exists() or SEAL.exists() or ATTEMPTS.exists():
        raise SystemExit("runs or results exist already: a consumed protocol can't be frozen again")
    again = select(write=False)
    if json.loads(LISTS.read_text()) != json.loads(json.dumps(again)):
        raise SystemExit("stage-b-lists.json is not what select gives now")
    write_decks()
    _, held, training, jobs = matrix()
    now = inputs(jobs, pilot_cells(training, identity_rows(training)))
    constants = {name: globals()[name] for name in (
        "HELD_SEED", "HELD_COUNT", "KNOWN_SEED", "KNOWN_COUNT", "PILOT_SEED", "PILOT_COUNT", "TRAIN_SEED", "HEAVY", "BAR", "LOW_BAR", "SEAT_BAR",
        "GAP_BAR", "VOID_BAR", "MIN_HEAVY", "KNOWN_BAR", "MASK_BAR", "HELP_BAR", "GATED_DOSE", "RESAMPLES", "BOOT_SEED", "REPLAY_EVERY",
        "DOSES", "PILOT_DOSES",
        "SETTINGS", "PILOT_OPTIMIZER", "PILOT_ROUNDS", "ADMIT", "TIE", "TRAIN")}
    manifest = {"commit": git("rev-parse", "HEAD"), "inputs": now, "pilot_jobs": now["pilot_jobs"], "constants": constants}
    MANIFEST.write_text(json.dumps(manifest, indent=1) + "\n")
    print(json.dumps({k: v for k, v in now.items() if k not in ("decks", "files", "training")} | {"commit": manifest["commit"]}, indent=1))


def check_frozen(stage):
    """stage "manifest": the manifest is committed alone, directly on the commit it names. The pilot and training run here.
    stage "sealed": the seal is committed alone, directly on the manifest's commit. The test plays and is analyzed here.
    In both, nothing else is changed and every input matches the manifest."""
    manifest = json.loads(MANIFEST.read_text())
    paths = [str(MANIFEST.relative_to(ROOT))] + ([str(SEAL.relative_to(ROOT))] if stage == "sealed" else [])
    if git("status", "--porcelain", "--untracked-files=no") or git("status", "--porcelain", "--", *paths):
        raise SystemExit("uncommitted changes, or an uncommitted manifest or seal")
    back = "HEAD^" if stage == "manifest" else "HEAD^^"
    if git("rev-parse", back) != manifest["commit"]:
        raise SystemExit(f"{back} must be the commit the manifest names, {manifest['commit']}")
    if sorted(git("diff", "--name-only", manifest["commit"], "HEAD").split()) != sorted(paths):
        raise SystemExit("the commits on top of the manifest's must change the manifest, then the seal, and nothing else")
    if stage == "sealed":
        if git("diff", "--name-only", "HEAD^", "HEAD").split() != [paths[1]]:
            raise SystemExit("the seal must be committed alone")
        seal_ = json.loads(SEAL.read_text())
        if seal_["manifest_sha256"] != sha(MANIFEST) or seal_["manifest_commit"] != git("rev-parse", "HEAD^"):
            raise SystemExit("the seal belongs to another manifest")
    write_decks()
    _, held, training, jobs = matrix()
    now = inputs(jobs, pilot_cells(training, identity_rows(training)))
    changed = [k for k in now if now[k] != manifest["inputs"][k]]
    if changed:
        raise SystemExit(f"inputs differ from the manifest: {changed}")
    return manifest


# ---------------------------------------------------------------- running

def soak(workers):
    """Every candidate list through the actor's own audit on the certified module. No network plays. A list plays 16 duels:
    in each seat, one cell of 4 with uniform random play on both seats, and one of 4 with the rule-based heuristic on both."""
    from actor import Pool
    if sha(NATIVE) != NATIVE_SHA:
        raise SystemExit("the native module is not the certified one")
    ids, row_of = identity()
    unknown = int(row_of[-1])
    registry = deck_registry.registry()
    training = sorted(registry)
    manifest = json.loads((CORPUS / "manifest.json").read_text())
    candidates = []
    for row in sorted(manifest["lists"], key=lambda r: int(r["list_id"][2:])):
        zones = deck_registry.parse_ydk((CORPUS / row["ydk"]).read_text())
        if row["production_legal"] and not row["in_training"] and any(row_of[ids[c]] == unknown for c in zones["main"] + zones["extra"]):
            candidates.append({"name": f"h_{row['list_id']}", "list_id": row["list_id"], "source": row["ydk"], "source_sha256": sha(CORPUS / row["ydk"])})
    out_dir = BUILD / "soak"
    if out_dir.exists():
        raise SystemExit("use a fresh soak directory")
    out_dir.mkdir(parents=True)
    write_decks(candidates)
    os.environ["EDISON_EXTRA_DECKS"] = str(EXTRA)
    jobs = []
    for n, row in enumerate(candidates):
        for part, kind in enumerate(("random", "heuristic")):
            for seat in SEATS:
                other = training[(n + part) % len(training)]
                d0, d1 = (row["name"], other) if seat == 0 else (other, row["name"])
                seed = SOAK_SEED + n * 16 + part * 8 + seat * 4
                jobs.append({"list": row["list_id"], "deck0": d0, "deck1": d1, "seed": seed, "games": 4, "policies": [{"kind": kind}, {"kind": kind}]})
    if jobs[-1]["seed"] + 3 > SOAK_END:
        raise SystemExit("the soak leaves its seed block")
    pool = Pool(workers, out_dir / "logs", cell_timeout=1800)
    started = time.perf_counter()
    try:
        results = pool.map([{k: v for k, v in j.items() if k != "list"} for j in jobs])
    finally:
        pool.close()
    void = [(j, r) for j, r in zip(jobs, results) if r.get("status") != "PASS"]
    passed = [r for r in results if r.get("status") == "PASS"]
    summary = {"what": "Every Stage B candidate list through actor.py's audit on the certified module: 16 duels each, uniform random and the rule-based "
                       "heuristic on both seats, against two training lists. No network played.",
               "native": sha(NATIVE), "codes_sha256": CODES_SHA, "identity_sha256": IDENTITY_SHA, "seeds": [SOAK_SEED, jobs[-1]["seed"] + 3],
               "lists": [c["list_id"] for c in candidates], "planned": len(jobs), "finished": len(results),
               "duels": sum(len(r["duels"]) for r in passed), "decisions": sum(r["counters"]["decisions"] for r in passed),
               "max_options": max(r["counters"]["max_options"] for r in passed), "longest_duel": max(d["steps"] for r in passed for d in r["duels"]),
               "lists_with_a_prompt_only_the_wider_tables_hold": len({j["list"] for j, r in zip(jobs, results)
                                                                      if r.get("status") == "PASS" and sum(r["counters"]["wide_decisions"])}),
               "void_cells": len(void), "void_lists": sorted({j["list"] for j, _ in void}),
               "void_reasons": dict(Counter(str(r.get("reason"))[:120] for _, r in void)), "seconds": round(time.perf_counter() - started)}
    SOAK.write_text(json.dumps(summary, indent=1) + "\n")
    print(json.dumps({k: v for k, v in summary.items() if k not in ("lists", "what")}, indent=1))
    return 0


def livecheck(workers):
    """Training lists only, with every held-out deck loaded, so a deck the env refuses stops this before any test game.
    The networks are M1's, given code list v2's table: Stage B's own do not exist yet."""
    from actor import Pool
    write_decks()
    os.environ["EDISON_EXTRA_DECKS"] = str(EXTRA)
    training = sorted(deck_registry.registry())
    rows = identity_rows(training)
    nets = {k: net_spec(V1_RUNS / f"gauntlet-{k}/checkpoint-2560", V1_PARAMS_SHA[k], v1=True) for k in OPTIMIZERS}
    heuristic = {"kind": "heuristic"}
    jobs = [{"deck0": training[0], "deck1": training[0], "seed": LIVE_SEED, "games": 2, "policies": [nets["1702"], heuristic]},
            {"deck0": training[1], "deck1": training[2], "seed": LIVE_SEED + 2, "games": 2, "policies": [heuristic, nets["1703"]],
             "identity": {"unknown_ids": hidden_ids(rows[training[2]], "5", LIVE_SEED + 2, 2)}},
            {"deck0": training[3], "deck1": training[4], "seed": LIVE_SEED + 4, "games": 2, "policies": [heuristic, heuristic]}]
    pool = Pool(workers, BUILD / "livecheck-logs", cell_timeout=900)
    try:
        first = pool.map(jobs)
        again = pool.map([{**j, "threads": 4} for j in jobs])
    finally:
        pool.close()
    ok = all(r["status"] == "PASS" for r in first) and all(a.get("duels") == r.get("duels") for a, r in zip(again, first)) \
        and first[1].get("hidden_rows") == jobs[1]["identity"]["unknown_ids"]
    print(json.dumps({"loaded_held_out_decks": len(json.loads(EXTRA.read_text())), "statuses": [r["status"] for r in first],
                      "reasons": [r.get("reason") for r in first], "replays_same_and_hidden_rows_reported": ok}, indent=1))
    return 0 if ok else 1


def play(workers):
    from actor import Pool
    check_frozen("sealed")
    _, held, training, jobs = matrix()
    specs = specs_from_seal(json.loads(SEAL.read_text()))
    os.environ["EDISON_EXTRA_DECKS"] = str(EXTRA)
    OUT = out_dir()
    OUT.mkdir(parents=True, exist_ok=True)
    attempt("play", seal=sha(SEAL))
    groups = chunks(jobs)
    for n, indices in enumerate(groups):
        path = OUT / f"chunk-{n:03d}.json.gz"
        if path.exists():
            with gzip.open(path, "rt") as f:
                problems, mismatches = check_chunk(n, json.load(f), [jobs[i] for i in indices], indices, OUT / f"chunk-{n:03d}.jsonl")
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
            run_tasks(pool, [(f"first:{i}", worker_job(j, specs)) for i, j in zip(indices, part)], journal, done)
            run_tasks(pool, [(f"replay:{i}", worker_job(jobs[i], specs, threads=4)) for i in replayed(indices)], journal, done)
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
    check_frozen("sealed")
    lists, held, training, jobs = matrix()
    rows, integrity = load_results(jobs)
    data = arrays(rows, held, training)
    numbers, broken = decide(data, lists["lists"], training, integrity)
    RESULT = out_dir() / "stage-b-result.json"
    reasons = Counter(str(r.get("reason"))[:120] for r in rows if r["status"] != "PASS")
    passing = [r for r in rows if r["status"] == "PASS"]
    net = [r for r in passing if r["arm"] != "mirror"]
    result = {"verdict": "STAGE_B_MET" if not broken else "NOT_MET", "broken": broken, "readings": numbers["readings"], "numbers": numbers,
              "reported": reported(data, lists["lists"], training), "usage": usage(rows, lists["lists"]),
              "void_reasons": dict(reasons), "unresolved_first": sum(r["unresolved_first"] for r in rows), "unresolved_replay": sum(r["unresolved_replay"] for r in rows),
              "draws": sum(d["winner"] == 2 for r in passing for d in r["duels"]), "decisions": sum(r["counters"]["decisions"] for r in passing),
              "net_decisions": sum(r["counters"]["net_decisions"] for r in net), "guard_fires": sum(r["counters"].get("guard_fires", 0) for r in net),
              "manifest_sha256": sha(MANIFEST), "seal_sha256": sha(SEAL)}
    RESULT.write_text(json.dumps(result, indent=1) + "\n")
    print(json.dumps({k: result[k] for k in ("verdict", "broken", "readings")}, indent=1))
    return 0 if not broken else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("select", "selftest", "freeze", "pilot-train", "train", "seal", "analyze"):
        commands.add_parser(name)
    for name in ("soak", "livecheck", "pilot", "play"):
        commands.add_parser(name).add_argument("--workers", type=int, default=14)
    args = parser.parse_args()
    if args.command == "select":
        out = select()
        print(json.dumps({k: v for k, v in out.items() if k != "lists"}, indent=1))
    elif args.command == "freeze":
        freeze()
    elif args.command == "seal":
        seal()
    elif args.command == "play":
        play(args.workers)
    else:
        run = {"selftest": selftest, "pilot-train": pilot_train, "train": train, "analyze": analyze}.get(args.command)
        sys.exit(run() if run else {"soak": soak, "livecheck": livecheck, "pilot": pilot}[args.command](args.workers))


if __name__ == "__main__":
    main()
