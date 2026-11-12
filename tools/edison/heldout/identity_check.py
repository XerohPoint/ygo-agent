#!/usr/bin/env python3
"""Does every card identity the network reads come from a row that training covers?

Code list v2 adds one unknown row and an id -> row map. This check compares what reaches the
network with a second computation of that map, written here from the deck files, the database
and the scripts. It does not call decks.build_identity or read identity_v2.json.

  static     the map in identity_v2.json equals the one computed here, for every id
  collect    plays lists with untrained cards, random and rule-based play only, and records every observation
  inputs     on those observations: after pack and the map, every identity in card rows, offered actions
             and own history is the row computed here, nothing else changed, and no identity reads a
             row that no training list covers. --mutant plants a fault that must be caught
  actor      actor.py itself, on training lists: rollouts with identity dropout and with forced unknown
             ids equal the plain rollouts read through each game's map
  native     edison_net.h against the Python network on the same observations, and its planted faults
  all        everything, the mutants, and the same input check on code list v1, where it must fail:
             v1 has no unknown row, so a Side-only card reads a row nothing trained

No network plays a list here except in `actor`, which uses training lists only. Seeds 89200000-89299999.
"""
import argparse
import collections
import hashlib
import json
import os
from pathlib import Path
import re
import sqlite3
import subprocess
import sys

import numpy as np

HERE = Path(__file__).resolve().parent
LEARNER, SEARCH = HERE.parent / "learner", HERE.parent / "search"
sys.path[:0] = [str(LEARNER), str(SEARCH)]
import decks as deck_registry  # noqa: E402
from decks import ROOT  # noqa: E402

V2 = deck_registry.CODE_LIST == "v2"
SEED_BASE, SEED_END = 89200000, 89299999
CORPUS = ROOT / "assets/edison/corpus"
MODULE_SHA = "d1f1f701e571cdaecc4270283bc0ae8e87533e0021ddf4ee84929dae81359983"
V1_CHECKPOINT = ROOT / "build/edison-retrain-2026-11-04/runs/gauntlet-1702/checkpoint-2560"
BIN = ROOT / "build/edison-native-net/bin"
TOKEN, ARTWORK = 0x4000, 20
# Set from the correct build's rounding in tools/edison/search/run_differential.py.
GATES = {"value_error.max": 2e-5, "value_error.p99.9": 8e-6, "value_error.mean": 4e-7,
         "logit_error.max": 2.5e-3, "logit_error_scaled.max": 3e-4, "logit_error_scaled.p99.9": 5e-5,
         "logit_error_scaled.mean": 1.5e-6, "state_error.mean": 3.5e-6, "row_error.mean": 2.6e-7}
COUNT_GATES = ("verdict_mismatch", "hist_mismatch", "pack_size_mismatch", "pack_cards_mismatch", "pack_actions_mismatch",
               "pack_decision_mismatch", "pack_global_mismatch", "eval_fail", "nonfinite", "value_only_vs_full_bit_mismatch")
MUTANTS = ("no_map", "skip_rows", "skip_actions", "skip_history", "known_to_unknown", "unknown_keeps_own_row")
NATIVE_FAULTS = {16: "card rows", 17: "offered actions", 18: "own history"}
SITES = (("rows", "cards", 0), ("actions", "actions", 1), ("history", "hist", 1))


def main_extra(path):
    """Main and Extra codes of a .ydk, read here rather than through decks.parse_ydk."""
    codes, zone = [], None
    for line in Path(path).read_text().splitlines():
        line = line.strip()
        if line in ("#main", "#extra", "!side"):
            zone = line
        elif line.isdigit() and zone in ("#main", "#extra"):
            codes.append(int(line))
    return codes


def independent_rows():
    """(rows, codes): the row each id of the active code list should read, or -1 for an id that no
    training list covers, and the code of each id.

    Known: a card some training list plays in its Main or Extra, every artwork of it, a token that a
    known card's script names, and every token with that token's name. All artworks read the lowest id
    among those a training list plays, and all tokens of one name read the one a script names.
    """
    codes = [0] + [int(line.split()[0]) for line in deck_registry.CODES.read_text().splitlines()]
    ids = {code: cid for cid, code in enumerate(codes) if cid}
    db = sqlite3.connect(deck_registry.DB)
    data = {row[0]: (row[1], row[2]) for row in db.execute("select id, alias, type from datas")}

    def base(code):
        alias = data[code][0]
        return alias if alias and abs(alias - code) < ARTWORK else code

    played = set()
    for path in deck_registry.registry().values():
        played.update(main_extra(path))
    lowest = {}
    for code in played:
        if base(code) not in lowest or ids[code] < ids[lowest[base(code)]]:
            lowest[base(code)] = code
    rows = np.full(len(codes), -1, dtype=np.int64)
    rows[0] = 0
    for cid in range(1, len(codes)):
        if base(codes[cid]) in lowest:
            rows[cid] = ids[lowest[base(codes[cid])]]
    for cid in np.nonzero(rows > 0)[0]:
        script = deck_registry.SCRIPTS / f"c{base(codes[cid])}.lua"
        if not script.exists():
            continue
        for number in re.findall(r"\b(\d{5,9})\b", script.read_text(errors="replace")):
            other = ids.get(int(number))
            if other and rows[other] < 0 and data[int(number)][1] & TOKEN:
                rows[other] = other
    name = dict(db.execute("select id, name from texts"))
    scripted = {}
    for cid in range(1, len(codes)):
        if rows[cid] == cid and data[codes[cid]][1] & TOKEN:
            scripted.setdefault(name[codes[cid]], cid)
    for cid in range(1, len(codes)):
        if rows[cid] < 0 and data[codes[cid]][1] & TOKEN and name[codes[cid]] in scripted:
            rows[cid] = scripted[name[codes[cid]]]
    return rows, codes


def expected_table():
    """(table, allowed, codes): what the network should read for each raw id, and every row it may read.
    v2 sends an uncovered id to the unknown row. v1 has no map, so an uncovered id reads its own row."""
    rows, codes = independent_rows()
    if V2:
        unknown = len(codes)
        return np.r_[np.where(rows < 0, unknown, rows), unknown], set(rows[rows > 0].tolist()) | {0, unknown}, codes
    # On v1 every known id trained its own row, artworks and same-name tokens included.
    return np.arange(len(codes)), set(np.nonzero(rows >= 0)[0].tolist()), codes


def static(args=None):
    if not V2:
        raise SystemExit("the identity map belongs to code list v2")
    table, _, codes = expected_table()
    sha, row_of = deck_registry.load_identity()
    different = np.nonzero(table != row_of)[0] if len(table) == len(row_of) else np.arange(1)
    report = {"identity_sha256": sha, "ids": len(codes) - 1, "unknown_row": int(row_of[-1]),
              "known_ids": int(((row_of != 0) & (row_of != row_of[-1])).sum()),
              "rows": len(set(row_of.tolist()) - {0, int(row_of[-1])}), "different": different.tolist()[:20]}
    print(json.dumps(report))
    if different.size:
        raise SystemExit("identity_v2.json disagrees with this computation")
    return report


def choose_lists(count, out):
    """Legal lists outside training that hold a card no training list covers, heaviest first, then a spread.
    Code list v1 can only load the ones inside it, after the Gottoms rule."""
    rows, codes = independent_rows()
    ids = {code: cid for cid, code in enumerate(codes) if cid}
    found = []
    for row in json.loads((CORPUS / "manifest.json").read_text())["lists"]:
        if not row["production_legal"] or row["in_training"]:
            continue
        path = CORPUS / row["ydk"]
        if not V2:
            if not row["inside_v1_main_extra"]:
                continue
            zones = deck_registry.parse_ydk(path.read_text())
            if any(code in deck_registry.OUTSIDE_V1 for zone in zones.values() for code in zone):
                path = out / "decks" / f"{row['list_id']}.ydk"
                path.parent.mkdir(parents=True, exist_ok=True)
                deck_registry.write_ydk(path, {z: [c for c in cs if c not in deck_registry.OUTSIDE_V1] for z, cs in zones.items()})
        cards = main_extra(path)
        if any(code not in ids for code in cards):
            raise SystemExit(f"{row['list_id']} has a card outside the active code list")
        copies = sum(rows[ids[code]] < 0 for code in cards)
        if copies:
            found.append((int(copies), int(row["list_id"][2:]), row["list_id"], str(path)))
    found.sort(key=lambda r: (-r[0], r[1]))
    heavy, rest = found[:count // 2], found[count // 2:]
    step = max(1, len(rest) // max(1, count - len(heavy)))
    picked = heavy + rest[::step][:count - len(heavy)]
    return [{"name": f"h_{name}", "path": path, "untrained_copies": n} for n, _, name, path in picked], len(found)


def collect(args):
    os.environ.update({"JAX_PLATFORMS": "cpu", "OPENBLAS_NUM_THREADS": "1", "OMP_NUM_THREADS": "1"})
    from collect_decisions import play_cell
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    lists, available = choose_lists(args.lists, out)
    registry = deck_registry.registry()
    names = sorted(registry)
    sys.path[:0] = [str(ROOT), str(ROOT / "ygoenv")]
    os.chdir(ROOT / "scripts")
    import ygoenv
    from ygoenv.ygopro import init_module, ygopro_ygoenv
    native_sha = hashlib.sha256(Path(ygopro_ygoenv.__file__).read_bytes()).hexdigest()
    if native_sha != MODULE_SHA:
        raise SystemExit(f"native module is not the certified r5 build: {native_sha}")
    from actor import MAX_OPTIONS, MAX_STEPS
    init_module(str(deck_registry.DB), str(deck_registry.CODES),
                {**{k: str(v) for k, v in registry.items()}, **{row["name"]: row["path"] for row in lists}})
    metadata = deck_registry.card_metadata()
    rotation = (("random", "random"), ("heuristic", "random"), ("random", "heuristic"), ("heuristic", "heuristic"))
    cells = []
    for index, row in enumerate(lists):
        other = names[index % len(names)]
        deck0, deck1 = (row["name"], other) if index % 2 == 0 else (other, row["name"])
        cells.append({"deck0": deck0, "deck1": deck1})
    for index in range(args.training_cells):
        cells.append({"deck0": names[index % len(names)], "deck1": names[(index * 5 + 3) % len(names)]})
    for index, cell in enumerate(cells):
        cell.update(index=index, seed=args.seed + index * args.games, deck0_id=0, deck1_id=0, policies=rotation[index % 4])
        if not SEED_BASE <= cell["seed"] <= SEED_END - args.games:
            raise SystemExit("seed outside this tool's block")

    def make_env(cell, games):
        return ygoenv.make("YGOPro-v1", env_type="gymnasium", num_envs=games, num_threads=1, seed=cell["seed"],
                           deck1=cell["deck0"], deck2=cell["deck1"], play_mode="self", player=-1, async_reset=False,
                           greedy_reward=False, oppo_info=False, max_options=MAX_OPTIONS, max_steps=MAX_STEPS,
                           max_episode_steps=MAX_STEPS, timeout=10, verbose=False)

    report = {"code_list": deck_registry.CODE_LIST, "native_sha256": native_sha, "lists": lists, "lists_available": available,
              "seed_base": args.seed, "games_per_cell": args.games, "cells": [], "void": []}
    with (out / "decisions.bin").open("wb") as dump:
        for cell in cells:
            try:
                buffers, _ = play_cell(cell, args.games, {}, metadata, len(metadata), make_env)
            except Exception as error:  # A failed cell contributes no observations, and the run reports it.
                report["void"].append({"cell": cell["index"], "seed": cell["seed"], "decks": [cell["deck0"], cell["deck1"]],
                                       "reason": f"{type(error).__name__}: {error}"})
                continue
            count = 0
            for game in sorted(buffers):
                np.concatenate(buffers[game]).tofile(dump)
                count += len(buffers[game])
            report["cells"].append({"cell": cell["index"], "seed": cell["seed"], "decks": [cell["deck0"], cell["deck1"]],
                                    "policies": cell["policies"], "decisions": count})
    report["decisions"] = sum(c["decisions"] for c in report["cells"])
    (out / "collect.json").write_text(json.dumps(report, indent=1) + "\n")
    print(json.dumps({"code_list": report["code_list"], "decisions": report["decisions"], "lists_available": available,
                      "cells": len(report["cells"]), "void": len(report["void"])}))
    if report["void"]:
        raise SystemExit(f"void cells: {report['void'][:3]}")


def ids_of(array, column):
    return (array[..., column].astype(np.int64) << 8) + array[..., column + 1]


def without_ids(array, column):
    out = array.copy()
    out[..., column:column + 2] = 0
    return out


def inputs(args):
    """The Python path as actor.py runs it: audit, normalize, pack, then the game's map."""
    from dump_format import RECORD, python_pipeline
    from obs import game_identity_maps, remap_ids
    table, allowed, codes = expected_table()
    allowed = np.asarray(sorted(allowed))
    unknown = len(codes)
    names = {cid: meta["name"] for cid, meta in deck_registry.card_metadata().items()}
    row_of = deck_registry.load_identity()[1].copy() if V2 else None
    mutant = args.mutant
    if mutant and not V2:
        raise SystemExit("mutants are planted in the code list v2 path")
    records = np.memmap(args.data / "decisions.bin", dtype=RECORD, mode="r")
    planted = None
    if mutant in ("known_to_unknown", "unknown_keeps_own_row"):
        seen = collections.Counter()
        for record in records[::17]:
            seen.update(ids_of(record["cards"], 0)[record["cards"][:, 2] > 0].tolist())
        want_known = mutant == "known_to_unknown"
        planted = next(cid for cid, _ in seen.most_common() if cid and (table[cid] != unknown) == want_known)
        if want_known:
            row_of[row_of == row_of[planted]] = unknown
        else:
            row_of[planted] = planted
    stats, examples = collections.Counter(), collections.defaultdict(collections.Counter)
    for record in records:
        verdict, packed = python_pipeline(record, len(codes) - 1)
        if verdict:
            stats["rejected_by_audit"] += 1
            continue
        stats["decisions"] += 1
        before = {key: packed[key].copy() for key in ("cards", "actions", "hist", "decision", "global")}
        if V2 and mutant != "no_map":
            maps, _ = game_identity_maps(row_of, None, 0, 1)
            remap_ids(packed, maps[[0]])
            for name, key in (("skip_rows", "cards"), ("skip_actions", "actions"), ("skip_history", "hist")):
                if mutant == name:
                    packed[key][...] = before[key]
        for site, key, column in SITES:
            raw, read = ids_of(before[key], column).ravel(), ids_of(packed[key], column).ravel()
            present = raw > 0
            wrong = read != table[raw]
            uncovered = ~np.isin(read, allowed)
            stats[f"{site}.identities"] += int(present.sum())
            stats[f"{site}.read_unknown_row"] += int((read[present] == unknown).sum())
            stats[f"{site}.not_the_expected_row"] += int(wrong.sum())
            stats[f"{site}.reads_an_uncovered_row"] += int(uncovered.sum())
            for cid in raw[wrong | uncovered].tolist():
                examples[site][names.get(cid, str(cid))] += 1
            stats["other_bytes_changed"] += int((without_ids(before[key], column) != without_ids(packed[key], column)).sum())
        for key in ("decision", "global"):
            stats["other_bytes_changed"] += int((before[key] != packed[key]).sum())
    violations = stats["other_bytes_changed"] + sum(v for k, v in stats.items() if k.endswith(("not_the_expected_row", "reads_an_uncovered_row")))
    report = {"code_list": deck_registry.CODE_LIST, "mutant": mutant, "planted_id": names.get(planted), "records": len(records),
              "rejected_by_audit": 0, **dict(sorted(stats.items())), "violations": violations,
              "examples": {site: dict(c.most_common(8)) for site, c in examples.items()}}
    print(json.dumps(report))
    if args.json:
        args.json.write_text(json.dumps(report, indent=1) + "\n")
    return report


def write_test_checkpoint(stem, zero_identity=False):
    """A code list v2 checkpoint with trained weights and no training: a v1 network given v2's table.

    Rows past v1 get noise, as the never-trained rows of a trained v2 network hold, so a pass that reads
    one by mistake gives a different answer. For tests only: it is not the Stage B baseline.
    """
    import flax
    from actor import CHECKPOINT_FORMATS, checkpoint_paths, load_v1_into_v2
    config, params, meta, _ = load_v1_into_v2(V1_CHECKPOINT)
    table = np.array(params["card_id"])
    first_new = len(deck_registry.load_codes(deck_registry.CODES_V1)[1]) + 1
    table[first_new:-1] = np.random.default_rng(89200001).normal(0, 0.1, table[first_new:-1].shape)
    params["card_id"] = np.zeros_like(table) if zero_identity else table.astype(np.float32)
    raw = flax.serialization.msgpack_serialize(params)
    sha = hashlib.sha256(raw).hexdigest()
    params_path, meta_path = checkpoint_paths(stem)
    params_path.parent.mkdir(parents=True, exist_ok=True)
    params_path.write_bytes(raw)
    meta_path.write_text(json.dumps({"format": CHECKPOINT_FORMATS["v2"], "params_sha256": sha,
                                     "codes_sha256": deck_registry.load_codes()[0],
                                     "identity_sha256": deck_registry.load_identity()[0], "config": config.dict(),
                                     "source": {"v1_checkpoint": str(V1_CHECKPOINT), "v1_params_sha256": meta["params_sha256"]},
                                     "note": "test checkpoint from identity_check.py; never trained on code list v2"}, indent=1) + "\n")
    return sha, config


def actor(args):
    """actor.py's own wiring. The test network's identity table is all zero, so hiding a card cannot change a
    choice, and the same seeds give the same games with and without hidden identities. The rollouts, which
    hold what the network was given, can then be compared decision by decision."""
    if not V2:
        raise SystemExit("the actor check needs code list v2")
    from actor import Pool
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    sha, _ = write_test_checkpoint(out / "zero-identity", zero_identity=True)
    table, _, codes = expected_table()
    unknown = len(codes)
    names = sorted(deck_registry.registry())
    games = 4
    clean, max_rate = 0.25, 0.8
    spec = {"kind": "net", "checkpoint": str(out / "zero-identity"), "sha256": sha, "greedy": False}
    known_ids = np.nonzero((table != 0) & (table != unknown))[0][:-1]
    known_rows = np.unique(table[known_ids])
    rng = np.random.default_rng(89200002)
    jobs, plans = [], []
    for cell in range(args.cells):
        seed = args.seed + cell * games
        if not SEED_BASE <= seed <= SEED_END - games:
            raise SystemExit("seed outside this tool's block")
        base = {"seed": seed, "games": games, "deck0": names[(cell * 2) % 21], "deck1": names[(cell * 2 + 7) % 21],
                "policies": [spec, spec], "collect": [True, True]}
        # Known ids, artworks included, plus a few ids that are unknown already and must change nothing.
        forced = [sorted(set(rng.choice(known_ids, size=int(rng.integers(0, 120))).tolist())
                         | set(rng.integers(1, unknown, size=5).tolist())) for _ in range(games)]
        variants = {"plain": None, "dropout": {"dropout": {"clean": clean, "max_rate": max_rate}}, "forced": {"unknown_ids": forced}}
        for name, identity in variants.items():
            job = {**base, "rollout": str(out / f"cell-{cell}-{name}.npz")}
            if identity:
                job["identity"] = identity
            jobs.append(job)
        plans.append({"seed": seed, "forced": forced})
    pool = Pool(args.workers, out / "logs")
    try:
        results = pool.map(jobs)
    finally:
        pool.close()
    stats = collections.Counter()
    for cell, plan in enumerate(plans):
        res = {name: results[cell * 3 + i] for i, name in enumerate(("plain", "dropout", "forced"))}
        if any(r["status"] != "PASS" for r in res.values()):
            raise SystemExit(f"cell {cell} void: {[r.get('reason') for r in res.values()]}")
        data = {name: dict(np.load(out / f"cell-{cell}-{name}.npz")) for name in res}
        if "hidden_rows" in res["plain"]:
            raise SystemExit("a plain cell reported hidden rows")
        plain = data["plain"]
        stats["decisions"] += len(plain["chosen"])
        for name in ("dropout", "forced"):
            hidden = res[name]["hidden_rows"]
            if name == "forced":
                want = [sorted(set(table[ids].tolist()) - {unknown}) for ids in plan["forced"]]
            else:
                want = []
                for game in range(games):
                    draw = np.random.default_rng([plan["seed"] + game, 0x1DD0])
                    first, second = draw.random(2)
                    picks = draw.random(len(known_rows))
                    want.append(known_rows[picks < (0.0 if first < clean else second * max_rate)].tolist())
            if [sorted(h) for h in hidden] != [sorted(w) for w in want]:
                raise SystemExit(f"cell {cell} {name}: hidden rows differ from the rows computed here")
            stats[f"{name}.games_with_hidden_rows"] += sum(bool(h) for h in hidden)
            stats[f"{name}.games"] += games
            got = data[name]
            for key in ("chosen", "game", "seat", "sizes", "n_rows", "outcome"):
                if not np.array_equal(plain[key], got[key]):
                    raise SystemExit(f"cell {cell} {name}: games diverged from the plain run ({key})")
            maps = np.tile(table, (games, 1))
            for game in range(games):
                maps[game][np.isin(table, hidden[game])] = unknown
            per_decision = maps[plain["game"]]
            per_action = np.repeat(per_decision, plain["sizes"], axis=0)
            for site, key, column in SITES:
                lookup = per_action if site == "actions" else per_decision
                raw, read = ids_of(plain[key], column), ids_of(got[key], column)
                # The plain rollout already went through the constant map, which every game's map extends.
                want_ids = lookup[np.arange(len(lookup)), raw] if raw.ndim == 1 else lookup[np.arange(len(lookup))[:, None], raw]
                stats[f"{name}.{site}.identities"] += int((raw > 0).sum())
                stats[f"{name}.{site}.hidden"] += int(((want_ids == unknown) & (raw != unknown)).sum())
                stats[f"{name}.{site}.wrong"] += int((read != want_ids).sum())
                stats[f"{name}.other_bytes_changed"] += int((without_ids(plain[key], column) != without_ids(got[key], column)).sum())
    report = dict(sorted(stats.items()))
    report["violations"] = sum(v for k, v in stats.items() if k.endswith((".wrong", "other_bytes_changed")))
    report["power"] = all(stats[f"{name}.{site}.hidden"] > 0 for name in ("dropout", "forced") for site, _, _ in SITES)
    print(json.dumps(report))
    if args.json:
        args.json.write_text(json.dumps(report, indent=1) + "\n")
    if report["violations"] or not report["power"]:
        raise SystemExit("actor identity check failed")
    return report


def gate_failures(report):
    failed = {key: report[key] for key in COUNT_GATES if report[key]}
    for key, limit in GATES.items():
        name, _, stat = key.partition(".")
        if report[name][stat] > limit:
            failed[key] = report[name][stat]
    return failed


def native(args):
    if not V2:
        raise SystemExit("the native check needs code list v2")
    import export_net
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    env = {**os.environ, "JAX_PLATFORMS": "cpu", "OPENBLAS_NUM_THREADS": "1", "OMP_NUM_THREADS": "1", "TF_CPP_MIN_LOG_LEVEL": "2",
           "XLA_FLAGS": "--xla_cpu_multi_thread_eigen=false intra_op_parallelism_threads=1"}
    sha, config = write_test_checkpoint(out / "transplant")
    exported = export_net.export(out / "transplant", out / "transplant.ednet", sha)
    decisions = (args.data / "decisions.bin").resolve()

    def run(command):
        subprocess.run([str(c) for c in command], check=True, env=env, stdout=subprocess.DEVNULL)

    run([sys.executable, SEARCH / "reference.py", "expect", "--decisions", decisions, "--output", out / "decisions.expect"])
    run([sys.executable, SEARCH / "reference.py", "net", "--decisions", decisions, "--checkpoint", out / "transplant",
         "--output", out / "transplant.ref"])
    text = f"{config.n_codes},{config.id_dim},{config.channels},{config.layers},{config.heads},{config.critic_hidden},1"

    def diff(binary, name):
        target = out / f"{name}.json"
        run([BIN / binary, "--net", out / "transplant.ednet", "--params-sha", sha, "--codes-sha", exported["codes_sha256"],
             "--decisions", decisions, "--expect", out / "decisions.expect", "--reference", out / "transplant.ref",
             "--json", target, "--config", text])
        return json.loads(target.read_text())

    def row(report):
        return {"failed_gates": sorted(gate_failures(report)), "greedy_mismatches": report["greedy_mismatch_count"],
                "value_error_max": report["value_error"]["max"], "logit_error_scaled_max": report["logit_error_scaled"]["max"]}

    correct = diff("net_diff", "correct")
    report = {"net_file_sha256": exported["file_sha256"], "config": text, "records": correct["records"], "correct": row(correct),
              "faults": {site: row(diff(f"net_diff_fault{fault}", f"fault{fault}")) for fault, site in NATIVE_FAULTS.items()}, "load": {}}

    def probe(path, config_text):
        got = subprocess.run([str(BIN / "net_load_test"), str(path), sha, exported["codes_sha256"], config_text],
                             capture_output=True, text=True)
        return [got.returncode, (got.stdout + got.stderr).strip()[-120:]]

    # A version 2 file refuses a caller that expects no identity map, and a broken map refuses to load.
    report["load"]["correct"] = probe(out / "transplant.ednet", text)
    report["load"]["caller_expects_no_map"] = probe(out / "transplant.ednet", text[:-2])
    from actor import load_checkpoint
    from net import BIN_POINTS, BIN_WIDTHS
    _, params, _, _ = load_checkpoint(out / "transplant")
    row_of = deck_registry.load_identity()[1].astype(np.float32)
    known = int(np.nonzero((row_of != 0) & (row_of != row_of[-1]))[0][0])
    breaks = {"map_out_of_range": (7, len(row_of)), "map_moves_empty_id": (0, 3), "map_sends_card_to_empty": (7, 0),
              "map_row_does_not_read_itself": (known, 1 if row_of[1] != 1 else 3), "map_fraction": (9, 9.5),
              "map_moves_unknown_row": (len(row_of) - 1, known)}
    for name, (index, value) in breaks.items():
        broken = row_of.copy()
        broken[index] = value
        arrays = [("const/bin_points", BIN_POINTS.astype(np.float32)), ("const/bin_widths", BIN_WIDTHS.astype(np.float32)),
                  ("const/id_row", broken)] + list(export_net.flatten(params))
        (out / f"{name}.ednet").write_bytes(export_net.serialize(arrays, config.dict(), sha, exported["codes_sha256"],
                                                                 export_net.FORMAT_VERSION_IDENTITY))
        report["load"][name] = probe(out / f"{name}.ednet", text)
    failures = []
    if report["correct"]["failed_gates"]:
        failures.append("the correct build fails its gates")
    failures += [f"the fault at {site} passes the gates" for site, r in report["faults"].items() if not r["failed_gates"]]
    if report["load"]["correct"][0] != 0:
        failures.append("the correct file does not load")
    failures += [f"{name} loads" for name, (code, _) in report["load"].items() if name != "correct" and code == 0]
    report["failures"] = failures
    print(json.dumps(report))
    if args.json:
        args.json.write_text(json.dumps(report, indent=1) + "\n")
    if failures:
        raise SystemExit(f"native identity check failed: {failures}")
    return report


def run_all(args):
    out = args.output.resolve()
    if out.exists() and any(out.iterdir()):
        raise SystemExit("use a fresh --output")
    out.mkdir(parents=True)

    def call(code_list, *command):
        env = {**os.environ, "EDISON_CODE_LIST": code_list, "JAX_PLATFORMS": "cpu", "TF_CPP_MIN_LOG_LEVEL": "2"}
        done = subprocess.run([sys.executable, str(Path(__file__).resolve()), *map(str, command)], env=env, capture_output=True, text=True)
        if done.returncode:
            raise SystemExit(f"{command[0]} on code list {code_list} failed:\n{done.stdout[-2000:]}\n{done.stderr[-3000:]}")
        return done

    summary = {"static": json.loads(call("v2", "static").stdout.strip().splitlines()[-1])}
    for code_list, seed in (("v2", SEED_BASE), ("v1", SEED_BASE + 20000)):
        call(code_list, "collect", "--output", out / code_list, "--seed", seed, "--lists", args.lists, "--games", args.games)
        collected = json.loads((out / code_list / "collect.json").read_text())
        summary[f"collect_{code_list}"] = {k: collected[k] for k in ("decisions", "lists_available", "native_sha256", "void")}
        summary[f"collect_{code_list}"]["untrained_copies_per_list"] = [row["untrained_copies"] for row in collected["lists"]]
        call(code_list, "inputs", "--data", out / code_list, "--json", out / f"inputs-{code_list}.json")
        summary[f"inputs_{code_list}"] = json.loads((out / f"inputs-{code_list}.json").read_text())
    summary["mutants"] = {}
    for mutant in MUTANTS:
        call("v2", "inputs", "--data", out / "v2", "--mutant", mutant, "--json", out / f"inputs-v2-{mutant}.json")
        row = json.loads((out / f"inputs-v2-{mutant}.json").read_text())
        summary["mutants"][mutant] = {k: v for k, v in row.items()
                                      if k in ("violations", "other_bytes_changed", "planted_id") or k.endswith(("_row",))}
    call("v2", "actor", "--output", out / "actor", "--seed", SEED_BASE + 40000, "--json", out / "actor.json")
    summary["actor"] = json.loads((out / "actor.json").read_text())
    call("v2", "native", "--data", out / "v2", "--output", out / "native", "--json", out / "native.json")
    summary["native"] = json.loads((out / "native.json").read_text())
    v2, v1 = summary["inputs_v2"], summary["inputs_v1"]
    failures = []
    if v2["violations"] or v2["rejected_by_audit"]:
        failures.append("code list v2 inputs have violations")
    if not all(v2[f"{site}.read_unknown_row"] > 0 for site, _, _ in SITES):
        failures.append("the v2 observations never read the unknown row at some site, so the check has no power there")
    if not v1["violations"]:
        failures.append("the same check passes on code list v1, so it proves nothing")
    failures += [f"mutant {name} was not caught" for name, row in summary["mutants"].items() if not row["violations"]]
    summary["failures"] = failures
    summary["verdict"] = "PASS" if not failures else "FAIL"
    (out / "summary.json").write_text(json.dumps(summary, indent=1) + "\n")
    print(json.dumps({"verdict": summary["verdict"], "failures": failures}, indent=1))
    if failures:
        raise SystemExit(1)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="mode", required=True)
    sub.add_parser("static").set_defaults(run=static)
    c = sub.add_parser("collect")
    c.add_argument("--output", type=Path, required=True)
    c.add_argument("--seed", type=int, default=SEED_BASE)
    c.add_argument("--lists", type=int, default=32)
    c.add_argument("--games", type=int, default=2)
    c.add_argument("--training-cells", type=int, default=8)
    c.set_defaults(run=collect)
    i = sub.add_parser("inputs")
    i.add_argument("--data", type=Path, required=True)
    i.add_argument("--mutant", choices=MUTANTS)
    i.add_argument("--json", type=Path)
    i.set_defaults(run=inputs)
    a = sub.add_parser("actor")
    a.add_argument("--output", type=Path, required=True)
    a.add_argument("--seed", type=int, default=SEED_BASE + 40000)
    a.add_argument("--cells", type=int, default=6)
    a.add_argument("--workers", type=int, default=6)
    a.add_argument("--json", type=Path)
    a.set_defaults(run=actor)
    n = sub.add_parser("native")
    n.add_argument("--data", type=Path, required=True)
    n.add_argument("--output", type=Path, required=True)
    n.add_argument("--json", type=Path)
    n.set_defaults(run=native)
    e = sub.add_parser("all")
    e.add_argument("--output", type=Path, required=True)
    e.add_argument("--lists", type=int, default=32)
    e.add_argument("--games", type=int, default=2)
    e.set_defaults(run=run_all)
    args = parser.parse_args()
    args.run(args)


if __name__ == "__main__":
    main()
