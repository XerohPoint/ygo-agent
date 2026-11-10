#!/usr/bin/env python3
"""Crash soak of the production-legal corpus lists, seed block 88000000-88999999.

Pool-audit style: random play (uniform), the audit's eager rule, and the frozen rule-based
Heuristic, on Main and Extra (--stage main) or with the whole Side moved in (--stage sidein).
No network, learner or search runs, and no win rate is computed.

Lists come from assets/edison/corpus/manifest.json, sorted by list id. Each list plays the
next --neighbours lists in that cycle. Each pair plays --per-pair duels: repeat r swaps seats
when r is odd and uses policy (uniform, eager, heuristic)[(r // 2) % 3]. Every duel uses the
certified production module through tools/edison/corpus/soak_worker.py and the audit's full
code list. Each batch is a separate process, so a native abort costs one duel, which is
recorded with its last decisions; the rest are re-run.
"""
import argparse, json, os, random, re, shutil, subprocess, sys, time
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "pool_audit"))
from corpus_common import *  # noqa: E402
import common as audit_common  # noqa: E402
import gen_decks  # noqa: E402

MARKERS = ("timeout", "Script not found", "Card not found", "Spec not found",
           "cannot find id", "MSG_RETRY", "Error", "stack traceback")  # as tools/edison/hardening/soak.py
BENIGN = ("Gym has been unmaintained", "Please upgrade to Gymnasium",
          "Users of this version of Gym", "See the migration guide")
POLICIES = ("uniform", "eager", "heuristic")


def side_in(deck, pool, rng):
    """The audit's side_in, except a Side card whose zone is empty stays in the Side."""
    main, extra, side = list(deck["main"]), list(deck["extra"]), []
    for code in deck["side"]:
        is_extra = pool.by_id[code]["rows"][0]["type"] & (audit_common.TYPE_FUSION | audit_common.TYPE_SYNCHRO)
        zone = extra if is_extra else main
        if not zone:
            side.append(code); continue
        side.append(zone.pop(rng.randrange(len(zone))))
        zone.append(code)
    return {"main": main, "extra": extra, "side": side}


def plan(lists, stage, neighbours, per_pair, seed, pool):
    names = sorted(lists, key=lambda n: int(n[2:]))
    decks, duels = {}, []
    for index, a in enumerate(names):
        for step in range(1, neighbours + 1):
            b = names[(index + step) % len(names)]
            for repeat in range(per_pair):
                rng = random.Random(seed)
                first, second = (b, a) if repeat % 2 else (a, b)
                d0, d1 = lists[first], lists[second]
                if stage == "sidein":
                    d0, d1 = (side_in(d, pool, rng) if d["side"] else d for d in (d0, d1))
                n0, n1 = f"k{seed}_0", f"k{seed}_1"
                decks[n0], decks[n1] = d0, d1
                duels.append({"id": str(seed), "seed": seed, "deck0": n0, "deck1": n1,
                              "policy": POLICIES[(repeat // 2) % 3], "lists": [first, second]})
                seed += 1
    return duels, decks, seed


def raw_text(err, out, kind):
    what = [l for l in err.splitlines() if "what():" in l or "Traceback" in l or "Segmentation" in l]
    tail = [l for l in (err + "\n" + out).splitlines()
            if l.strip() and not l.startswith("@@") and not any(l.startswith(b) for b in BENIGN)]
    return (what[-1] if what else (tail[-1] if tail else kind)).strip()[:300]


def signature(err, out, kind):
    return f"{kind}: {re.sub(r'[0-9]{5,}', 'N', raw_text(err, out, kind))[:200]}"


def read_log(path):
    ended, began, last = {}, None, []
    if path.exists():
        for raw in path.read_text(errors="replace").splitlines():
            try: record = json.loads(raw)
            except ValueError: continue
            if "begin" in record: began, last = record["begin"], []
            elif "s" in record: last = (last + [record])[-8:]
            elif "end" in record: ended[record["end"]["id"]] = record["end"]
    return ended, began, last


def tie_lines(err, out):
    """Diagnostic and other output lines, each tied to the duel printing it (None if outside one)."""
    tied, current = [], None
    for line in (out + "\n" + err).splitlines():
        text = line.strip()
        if text.startswith("@@begin "): current = text.split()[1]; continue
        if text.startswith("@@end "): current = None; continue
        if text and not any(text.startswith(b) for b in BENIGN):
            tied.append((current, text[:240]))
    return tied


def run_batch(index, stage_dir, duels, decks, timeout, codes, script_root):
    bdir = stage_dir / f"b{index:05d}"
    bdir.mkdir(parents=True, exist_ok=True)
    paths = {}
    for name in {d for x in duels for d in (x["deck0"], x["deck1"])}:
        paths[name] = bdir / f"{name}.ydk"
        gen_decks.write_ydk(decks[name], paths[name])
    finished, crashes, lines, chosen = [], [], [], Counter()
    remaining, attempt = list(duels), 0
    while remaining:
        attempt += 1
        spec, log = bdir / f"spec{attempt}.json", bdir / f"log{attempt}.jsonl"
        spec.write_text(json.dumps({"decks": {n: str(p) for n, p in paths.items()}, "duels": remaining}))
        cmd = [sys.executable, str(HERE / "soak_worker.py"), "--spec", str(spec), "--log", str(log),
               "--codes", str(codes)]
        kind = None
        try:
            done = subprocess.run(cmd, cwd=script_root, capture_output=True, text=True, errors="replace", timeout=timeout)
            out, err, code = done.stdout, done.stderr, done.returncode
        except subprocess.TimeoutExpired as error:
            out = (error.stdout or b"").decode(errors="replace") if isinstance(error.stdout, bytes) else (error.stdout or "")
            err = (error.stderr or b"").decode(errors="replace") if isinstance(error.stderr, bytes) else (error.stderr or "")
            code, kind = None, "timeout"
        lines += tie_lines(err, out)
        ended, began, last = read_log(log)
        for record in ended.values():
            # Keep memory flat over 100,000+ duels: fold per-duel card counts into the batch.
            for card, act, n in record.pop("chosen"):  # not "code": that holds the exit status
                chosen[f"{card}:{act}"] += n
            record.pop("trace", None)
        finished += list(ended.values())
        unfinished = [d for d in remaining if d["id"] not in ended]
        if code == 0 and kind is None and not unfinished:
            break
        crash = next((d for d in remaining if d["id"] == began and d["id"] not in ended), None)
        if crash is None:
            if began in ended and unfinished:
                # The worker stopped by design after a non-ok duel, which is already recorded.
                remaining = unfinished
                continue
            for dead in unfinished:
                crashes.append({"duel": dead["id"], "seed": dead["seed"], "signature": signature(err, out, "startup"),
                                "text": raw_text(err, out, "startup"),
                                "last": [], "lists": dead["lists"], "policy": dead["policy"]})
            break
        crashes.append({"duel": crash["id"], "seed": crash["seed"], "last": last,
                        "signature": signature(err, out, kind or f"exit{code}"),
                        "text": raw_text(err, out, kind or f"exit{code}"),
                        "lists": crash["lists"], "policy": crash["policy"]})
        remaining = [d for d in unfinished if d["id"] != crash["id"]]
    if not crashes:
        shutil.rmtree(bdir, ignore_errors=True)
    return finished, crashes, lines, chosen


def load_lists(manifest, pool):
    lists = {}
    for row in manifest["lists"]:
        if row["production_legal"]:
            lists[row["list_id"]] = parse_ydk_text((DEST / row["ydk"]).read_text())
    return lists


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--stage", choices=["main", "sidein"], required=True)
    parser.add_argument("--seed", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--workers", type=int, default=12)
    parser.add_argument("--batch", type=int, default=20)
    parser.add_argument("--neighbours", type=int, default=4)
    parser.add_argument("--per-pair", type=int, default=6)
    parser.add_argument("--only", nargs="*", help="Restrict to these list ids (a probe)")
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument("--codes", type=Path, default=audit_common.FULL_CODES,
                        help="Code list; only a negative control should change it")
    parser.add_argument("--script-root", type=Path, default=ROOT / "scripts",
                        help="Directory holding script/; only a negative control should change it")
    args = parser.parse_args()
    args.output = args.output.resolve()
    if args.workers > 12:
        parser.error("At most 12 workers")
    pool = gen_decks.Pool()
    manifest = load_json(DEST / "manifest.json")
    lists = load_lists(manifest, pool)
    if args.only:
        lists = {k: v for k, v in lists.items() if k in set(args.only)}
    duels, decks, next_seed = plan(lists, args.stage, args.neighbours, args.per_pair, args.seed, pool)
    if not (SEED_FIRST <= args.seed and next_seed - 1 <= SEED_LAST):
        parser.error("Corpus seeds stay inside 88000000-88999999")
    args.output.mkdir(parents=True, exist_ok=True)
    batches = [duels[i:i + args.batch] for i in range(0, len(duels), args.batch)]
    started = time.time()
    finished, crashes, lines, chosen = [], [], [], Counter()
    with ThreadPoolExecutor(args.workers) as runner:
        futures = [runner.submit(run_batch, i, args.output, batch, decks, args.timeout, args.codes.resolve(),
                                 args.script_root.resolve()) for i, batch in enumerate(batches)]
        for n, future in enumerate(futures):
            done, crashed, output, picks = future.result()
            finished += done; crashes += crashed; lines += output; chosen.update(picks)
            if n % 200 == 0:
                print(f"batch {n}/{len(batches)} finished {len(finished)} crashes {len(crashes)} {time.time() - started:.0f}s", flush=True)
    by_id = {d["id"]: d for d in duels}
    per_list = defaultdict(lambda: {"duels": 0, "ok": 0, "other": Counter(), "crash": Counter(), "diagnostics": 0, "decisions": 0})
    for record in finished:
        for name in by_id[record["id"]]["lists"]:
            row = per_list[name]
            row["duels"] += 1; row["decisions"] += record["steps"]
            if record["status"] == "ok": row["ok"] += 1
            else: row["other"][record["status"]] += 1
    for crash in crashes:
        for name in crash["lists"]:
            per_list[name]["duels"] += 1; per_list[name]["crash"][crash["signature"]] += 1
    crashed = {c["duel"] for c in crashes}
    diagnostics = [{"duel": duel, "line": text, "lists": by_id[duel]["lists"] if duel in by_id else None,
                    "policy": by_id[duel]["policy"] if duel in by_id else None, "crashed_duel": duel in crashed}
                   for duel, text in lines if any(m.lower() in text.lower() for m in MARKERS)]
    for item in diagnostics:
        for name in item["lists"] or []:
            per_list[name]["diagnostics"] += 1
    other_lines = Counter(text for _, text in lines if not any(m.lower() in text.lower() for m in MARKERS))
    statuses = Counter(r["status"] for r in finished)
    violations, prompts = Counter(), Counter()
    for record in finished:
        violations.update(record.get("violations", {})); prompts.update(record.get("prompts", {}))
    summary = {"stage": args.stage, "first_seed": args.seed, "last_seed": next_seed - 1, "lists": len(lists),
               "neighbours": args.neighbours, "per_pair": args.per_pair, "workers": args.workers,
               "planned": len(duels), "finished": len(finished), "statuses": dict(statuses),
               "by_policy": dict(Counter(by_id[r["id"]]["policy"] for r in finished)),
               "crashes": len(crashes), "crash_signatures": dict(Counter(c["signature"] for c in crashes).most_common()),
               "diagnostic_lines": len(diagnostics),
               "diagnostic_lines_outside_crashed_duels": sum(not d["crashed_duel"] for d in diagnostics),
               "contract_violations": dict(violations),
               "decisions": sum(r["steps"] for r in finished),
               "max_options": max((r["max_options"] for r in finished), default=0),
               "max_repeat": max((r["max_repeat"] for r in finished), default=0),
               "prompt_rows_by_msg_id": dict(sorted(prompts.items(), key=lambda kv: int(kv[0]))),
               "other_output_lines": dict(other_lines.most_common(20)),
               "elapsed_seconds": round(time.time() - started, 1)}
    (args.output / "summary.json").write_text(json.dumps(summary, indent=1) + "\n")
    (args.output / "crashes.json").write_text(json.dumps(crashes, indent=0) + "\n")
    (args.output / "diagnostics.json").write_text(json.dumps(diagnostics, indent=0) + "\n")
    (args.output / "non_ok.json").write_text(json.dumps([{**{k: r[k] for k in ("id", "status", "steps", "max_repeat")},
        "lists": by_id[r["id"]]["lists"], "policy": by_id[r["id"]]["policy"]} for r in finished if r["status"] != "ok"], indent=0) + "\n")
    (args.output / "per_list.json").write_text(json.dumps({k: {**v, "other": dict(v["other"]), "crash": dict(v["crash"])}
                                                          for k, v in sorted(per_list.items())}, indent=0) + "\n")
    (args.output / "chosen.json").write_text(json.dumps(dict(chosen)) + "\n")
    print(json.dumps(summary, indent=1))
    clean = not crashes and not diagnostics and not violations and set(statuses) <= {"ok"} and len(finished) == len(duels)
    print("SOAK: " + ("CLEAN" if clean else "NEEDS REVIEW"))
    return 0 if clean else 1


if __name__ == "__main__":
    raise SystemExit(main())
