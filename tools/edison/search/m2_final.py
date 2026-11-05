#!/usr/bin/env python3
"""M2's final test. docs/edison-m2-protocol.md is the plan, and this script is its exact form.

  selftest   the decision rules on made-up results, where the answer is known
  freeze     records what the run depends on in m2-manifest.json, to be committed before any game
  play       the search and plain games, which refuses to start unless everything still matches the manifest
  analyze    the verdict: exit 0 on a pass, 1 on a fail
"""
import argparse
import hashlib
import json
from pathlib import Path
import random
import subprocess
import sys

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import search_common as sc  # noqa: E402
import search_study as study  # noqa: E402
import test_search as checks  # noqa: E402

BASE_SEED = 87000000
NETS = ("n1702", "n1703", "n1704")
SEATS = (0, 1)
SETTING = {"budget": 64, "depth": 48, "candidates": 4, "z": 1.0}
BLOCKS = 168
RESAMPLES = 20000
MAX_EXCLUDED = 10  # of 1,008 comparisons
RANK_GAMES = 64
RANK_OPTIONS = {"budget": 256, "depth": 48}
SEARCH_DIR = sc.OUT / "m2-final-search"
PLAIN_DIR = sc.OUT / "m2-final-plain"
RESULT = sc.OUT / "m2-final-result.json"
MANIFEST = Path(__file__).resolve().parent / "m2-manifest.json"
GATED = ("missing_games", "worker_deaths", "worker_failures", "attempt_logs_missing", "attempt_logs_other_inputs", "inputs_differ", "root_refusals", "net_errors", "option_mismatch", "step_overruns", "job_mismatches", "name_mismatches", "build_mismatches")


def blocks(names):
    pairs = sc.neighbour_pairs(names)
    if len(pairs) != BLOCKS:
        raise SystemExit(f"expected {BLOCKS} ordered pairs, found {len(pairs)}")
    return pairs


def search_jobs(names):
    lines = []
    for b, (deck0, deck1) in enumerate(blocks(names)):
        for net in NETS:
            for seat in SEATS:
                lines.append(sc.job_line(f"g{b}_{net}_{seat}", deck0, deck1, BASE_SEED + b, seat, net, SETTING["budget"], SETTING["depth"], SETTING["candidates"], SETTING["z"], 1, 3000 + len(lines), 0))
    return lines


def plain_jobs(names):
    return [sc.job_line(f"p{b}_{net}", deck0, deck1, BASE_SEED + b, -1, net) for b, (deck0, deck1) in enumerate(blocks(names)) for net in NETS]


def comparisons(search, plain):
    """(block, net, seat, search score, plain score). A score is None for a void game."""
    out = []
    for b in range(BLOCKS):
        for net in NETS:
            base = plain.get(f"p{b}_{net}")
            for seat in SEATS:
                game = search.get(f"g{b}_{net}_{seat}")
                out.append((b, net, seat, study.score(game, seat) if game else None, study.score(base, seat) if base else None))
    return out


def interval(rows, value, rng):
    """Every block counts once, whatever survives in it, and whole blocks are resampled, so a seed's six games stay together."""
    per_block = {}
    for row in rows:
        v = value(row)
        if v is not None:
            per_block.setdefault(row[0], []).append(v)
    if not per_block:
        return {"blocks": 0, "mean": None, "low": None, "high": None}
    means = np.array([np.mean(v) for _, v in sorted(per_block.items())])
    drawn = rng.integers(0, len(means), size=(RESAMPLES, len(means)))
    samples = means[drawn].mean(axis=1)
    return {"blocks": len(means), "mean": float(means.mean()), "low": float(np.percentile(samples, 2.5)), "high": float(np.percentile(samples, 97.5))}


def decide(rows, integrity, ranker):
    """The decision rules of the protocol, in order. Returns the numbers and the rules broken."""
    rng = np.random.default_rng(BASE_SEED)
    kept = [r for r in rows if r[3] is not None and r[4] is not None]
    difference = lambda r: r[3] - r[4] if r[3] is not None and r[4] is not None else None
    points = lambda r: r[3] if r[3] is not None and r[4] is not None else None
    excluded = len(rows) - len(kept)
    numbers = {"comparisons": len(rows), "excluded": excluded, "overall": {"difference": interval(rows, difference, rng), "score": interval(rows, points, rng)}}
    for net in NETS:
        subset = [r for r in rows if r[1] == net]
        numbers[net] = {"difference": interval(subset, difference, rng), "score": interval(subset, points, rng)}
    for seat in SEATS:
        subset = [r for r in rows if r[2] == seat]
        numbers[f"seat_{seat}"] = {"difference": interval(subset, difference, rng), "score": interval(subset, points, rng)}
    low = numbers["overall"]["difference"]["low"]
    worst = interval(rows, lambda r: difference(r) if difference(r) is not None else -1.0, rng)["low"]
    numbers["overall_worst_case_low"] = worst
    numbers["integrity"] = integrity
    numbers["ranker"] = ranker
    broken = []
    if low is None or low <= 0:
        broken.append("1: the overall difference's lower bound is not above 0")
    for net in NETS:
        d = numbers[net]["difference"]["low"]
        if d is None or d <= 0:
            broken.append(f"2: {net}'s difference has a lower bound not above 0")
    for seat in SEATS:
        d = numbers[f"seat_{seat}"]["difference"]["low"]
        if d is None or d <= 0:
            broken.append(f"3: seat {seat}'s difference has a lower bound not above 0")
    if excluded > MAX_EXCLUDED:
        broken.append(f"4: {excluded} comparisons excluded, above {MAX_EXCLUDED}")
    if worst is None or worst <= 0:
        broken.append("5: counting every excluded comparison as the worst result, the lower bound is not above 0")
    for key in GATED:
        if integrity[key]:
            broken.append(f"6: {key} is {integrity[key]}")
    broken += [f"7: {b}" for b in ranker["broken"]]
    return numbers, broken


def selftest():
    rng = random.Random(1)
    clean = {key: 0 for key in GATED}
    good_ranker = {"broken": []}

    def rows(search_rate, seat1=None, net_rate=None, void=0):
        out = []
        for b in range(BLOCKS):
            for net in NETS:
                plain = float(rng.random() < 0.5)
                for seat in SEATS:
                    rate = search_rate
                    if seat == 1 and seat1 is not None:
                        rate = seat1
                    if net_rate and net in net_rate:
                        rate = net_rate[net]
                    won = float(rng.random() < rate)
                    out.append((b, net, seat, won, plain if seat == 0 else 1 - plain))
        for i in range(void):
            out[i] = out[i][:3] + (None,) + out[i][4:]
        return out
    cases = [("search much stronger", rows(0.8), clean, good_ranker, None),
             ("no better than plain", rows(0.5), clean, good_ranker, "1:"),
             ("one net no better", rows(0.8, net_rate={"n1703": 0.5}), clean, good_ranker, "2:"),
             ("one seat no better", rows(0.8, seat1=0.5), clean, good_ranker, "3:"),
             ("too many voids", rows(0.8, void=11), clean, good_ranker, "4:"),
             ("a worker died", rows(0.8), {**clean, "worker_deaths": 1}, good_ranker, "6:"),
             ("a failure a resume hid", rows(0.8), {**clean, "worker_failures": 1}, good_ranker, "6:"),
             ("ranker failed", rows(0.8), clean, {"broken": ["x"]}, "7:")]
    failed = 0
    for name, data, integrity, ranker, expected in cases:
        _, broken = decide(data, integrity, ranker)
        ok = not broken if expected is None else any(b.startswith(expected) for b in broken)
        print(f"{'pass' if ok else 'FAIL'}: {name}: {broken}")
        failed += not ok
    # Reading both seats of one plain game makes its mean exactly a half, which is what makes a difference and a score carry the same information.
    _, broken = decide([(b, net, seat, None, None) for b in range(BLOCKS) for net in NETS for seat in SEATS], clean, good_ranker)
    print(f"{'pass' if broken else 'FAIL'}: every game void: {broken[:2]}")
    failed += not broken
    # Few enough exclusions for rule 4, yet they would decide a bare margin. Each excluded comparison counts as -1 in its own block.
    thin = [(b, net, seat, 0.505, 0.5) for b in range(BLOCKS) for net in NETS for seat in SEATS]
    thin[:10] = [r[:3] + (None, r[4]) for r in thin[:10]]
    _, broken = decide(thin, clean, good_ranker)
    ok = any(b.startswith("5:") for b in broken) and not any(b.startswith(("1:", "4:")) for b in broken)
    print(f"{'pass' if ok else 'FAIL'}: exclusions would decide a bare margin: {broken}")
    failed += not ok
    failed += selftest_records()
    return 1 if failed else 0


def selftest_records():
    """What analyze accepts as results, on records made up from the real job lines."""
    names = [f"list{i}" for i in range(21)]
    lines = {line.split("\t")[0]: line for line in search_jobs(names) + plain_jobs(names)}
    search, plain = {}, {}
    for key, line in lines.items():
        record = {"id": key, "job": line.rstrip("\n").replace("\t", " "), "binary": "good", "status": "OK", "winner": 0, "search_seat": int(line.split("\t")[4])}
        (search if key[0] == "g" else plain)[key] = record
    logs = {"logs_missing": 0, "logs_other_inputs": 0, "worker_failures": 0, "interrupted": 0, "inputs_differ": 0}
    run = lambda s, p: record_problems(s, p, names, "good", logs)
    failed = 0

    def expect(name, result, key):
        nonlocal failed
        ok = bool(result[key]) if key else not any(result[k] for k in GATED if k in result)
        print(f"{'pass' if ok else 'FAIL'}: {name}: {key} {result.get(key)}")
        failed += not ok
    expect("well-formed records", run(search, plain), None)
    expect("another build", run({k: {**g, "binary": "wrong"} for k, g in search.items()}, plain), "build_mismatches")
    expect("no build named", run({k: {x: v for x, v in g.items() if x != "binary"} for k, g in search.items()}, plain), "build_mismatches")
    swapped = dict(search)
    for b in range(BLOCKS):
        for net in NETS:
            swapped[f"g{b}_{net}_0"], swapped[f"g{b}_{net}_1"] = search[f"g{b}_{net}_1"], search[f"g{b}_{net}_0"]
    expect("files swapped between seats", run(swapped, plain), "name_mismatches")
    expect("a job changed", run({**search, "g0_n1702_0": {**search["g0_n1702_0"], "job": "other"}}, plain), "job_mismatches")
    shadow = {k: {**g, "binary": "wrong"} for k, g in search.items()}
    expect("wrong-build results hidden by good copies in the other directory", run(shadow, {**plain, **search}), "build_mismatches")
    expect("search results copied into the plain directory", run(search, {**plain, **search}), "name_mismatches")
    expect("a game missing", run({k: g for k, g in search.items() if k != "g0_n1702_0"}, plain), "missing_games")
    died = {"id": "g0_n1702_0", "job": search["g0_n1702_0"]["job"], "binary": "good", "status": "VOID", "reason": "worker ended with status 9"}
    expect("a worker died", run({**search, "g0_n1702_0": died}, plain), "worker_deaths")
    # The native parent's void record has no seat. Reporting must not crash on it.
    try:
        study.calibration([died])
        study.splits([dict(died, _job=[""] * 14)])
        print("pass: a worker's void record can be reported")
    except KeyError as missing:
        print(f"FAIL: a worker's void record crashes the report: {missing}")
        failed += 1
    few = {k: ({**g, "winner": 2} if i >= RANK_GAMES - 1 else g) for i, (k, g) in enumerate(search.items())}
    ok = len(eligible_games(few, names)) < RANK_GAMES
    print(f"{'pass' if ok else 'FAIL'}: fewer decisive games than the ranker needs are seen")
    failed += not ok
    return failed


def current_inputs(names):
    return {**sc.inputs(sc.BIN, sc.SCRIPTS), "binary_id": subprocess.run([str(sc.BIN), "id"], check=True, capture_output=True, text=True).stdout.strip(), "jobs": hashlib.sha256("".join(search_jobs(names) + plain_jobs(names)).encode()).hexdigest()}


def git(*args):
    return subprocess.run(["git", "-C", str(sc.ROOT), *args], check=True, capture_output=True, text=True).stdout.strip()


def freeze(args):
    names = sc.write_registry()
    if git("status", "--porcelain", "--untracked-files=no"):
        raise SystemExit("commit everything first: the manifest names one commit")
    files = {str(p.relative_to(sc.ROOT)): sc.file_sha(p) for p in sorted(sc.HERE.glob("*.py"))}
    MANIFEST.write_text(json.dumps({"commit": git("rev-parse", "HEAD"), "inputs": current_inputs(names), "scripts": files, "setting": SETTING, "base_seed": BASE_SEED, "resamples": RESAMPLES}, indent=1) + "\n")
    print(f"wrote {MANIFEST}. Commit it, and only it, before play")


def check_frozen(names):
    """The manifest must be committed, alone, directly on the commit it names, with nothing else changed."""
    manifest = json.loads(MANIFEST.read_text())
    path = str(MANIFEST.relative_to(sc.ROOT))
    if git("status", "--porcelain", "--untracked-files=no") or git("status", "--porcelain", "--", path):
        raise SystemExit("uncommitted changes, or an uncommitted manifest")
    if git("rev-parse", "HEAD^") != manifest["commit"]:
        raise SystemExit(f"HEAD must be the manifest's commit, directly on {manifest['commit']}")
    if git("diff", "--name-only", manifest["commit"], "HEAD").split() != [path]:
        raise SystemExit("the commit on top of the manifest's must change the manifest and nothing else")
    if current_inputs(names) != manifest["inputs"]:
        raise SystemExit("the binary, nets, scripts, database, lists, codes or jobs differ from the manifest")
    return manifest


def play(args):
    names = sc.write_registry()
    check_frozen(names)
    sc.play(plain_jobs(names), PLAIN_DIR, args.workers)
    sc.play(search_jobs(names), SEARCH_DIR, args.workers)


def attempt_logs(manifest, names):
    """What each directory's log says about every attempt on it, against the frozen inputs and that directory's jobs. A start with no end is a run that was cut off."""
    out = {"logs_missing": 0, "logs_other_inputs": 0, "worker_failures": 0, "interrupted": 0, "nonzero_exits": 0, "inputs_differ": 0}
    frozen = {k: v for k, v in manifest["inputs"].items() if k not in ("jobs", "binary_id")}
    for directory, lines in ((SEARCH_DIR, search_jobs(names)), (PLAIN_DIR, plain_jobs(names))):
        identity = sc.attempt_identity(frozen, lines)
        log, meta = directory.with_suffix(".attempts"), directory.with_suffix(".inputs")
        out["inputs_differ"] += not meta.exists() or json.loads(meta.read_text()) != frozen
        if not log.exists():
            out["logs_missing"] += 1
            continue
        rows = [json.loads(line) for line in log.read_text().splitlines()]
        out["logs_missing"] += not rows
        out["logs_other_inputs"] += sum(row["inputs"] != identity for row in rows)
        ends = [row for row in rows if row["event"] == "end"]
        out["interrupted"] += sum(row["event"] == "start" for row in rows) - len(ends)
        out["nonzero_exits"] += sum(row["returncode"] != 0 for row in ends)
        out["worker_failures"] += sum(row["counts"]["worker_failures"] for row in ends if row["counts"])
    return out


def record_problems(search, plain, names, binary_id, logs):
    """Every result must sit under its own name, hold its own job, and come from the frozen build. Checked before anything is scored."""
    expected = [(search, {line.split("\t")[0]: line.rstrip("\n").replace("\t", " ") for line in search_jobs(names)}), (plain, {line.split("\t")[0]: line.rstrip("\n").replace("\t", " ") for line in plain_jobs(names)})]
    listed = list(search.values()) + list(plain.values())
    counters = {key: sum(g.get(key, 0) for g in listed) for key in ("net_errors", "option_mismatch", "step_overruns")}
    return {"missing_games": sum(k not in games for games, jobs in expected for k in jobs),
            "name_mismatches": sum(g.get("id") != k or k not in jobs for games, jobs in expected for k, g in games.items()),
            "job_mismatches": sum(g.get("job", "").strip() != jobs.get(k, "").strip() for games, jobs in expected for k, g in games.items()),
            "build_mismatches": sum(g.get("binary") != binary_id for g in listed),
            "worker_deaths": sum("worker ended" in g.get("reason", "") for g in listed),
            "root_refusals": sum("network refused" in g.get("reason", "") for g in listed),
            "attempt_logs_missing": logs["logs_missing"],
            "attempt_logs_other_inputs": logs["logs_other_inputs"],
            "worker_failures": logs["worker_failures"],
            "inputs_differ": logs["inputs_differ"],
            "interrupted_attempts": logs["interrupted"],
            "nonzero_exits": logs.get("nonzero_exits", 0),
            "void_games": sum(g.get("status") != "OK" for g in listed),
            "void_reasons": sorted({g.get("reason", "")[:100] for g in listed if g.get("status") != "OK"}),
            **counters}


def eligible_games(search, names):
    return [line for line in search_jobs(names) if search.get(line.split("\t")[0], {}).get("status") == "OK" and search[line.split("\t")[0]].get("winner") in (0, 1)]


def ranker_gate(search, names):
    """Positions from games chosen by a fixed draw over the finished, decisive ones. Scores aren't consulted beyond that, and the rules are test_search.py's estimates stage's."""
    done = eligible_games(search, names)
    if len(done) < RANK_GAMES:
        return {"broken": [f"only {len(done)} decisive games, and the ranker needs {RANK_GAMES}"]}
    rng = random.Random(BASE_SEED)
    chosen = sorted(rng.sample(range(len(done)), RANK_GAMES))
    closing, middle = checks.game_positions([done[i] for i in chosen], search, rng)
    first_closing = checks.rank_positions(closing, 4, **RANK_OPTIONS)
    first_middle = checks.rank_positions(middle, 4, **RANK_OPTIONS)
    followed = checks.Followed(4, **RANK_OPTIONS)
    result = checks.judge_estimates(closing, first_closing, middle, first_middle, followed)
    result.update(checks.alone(closing + middle, first_closing + first_middle, 4, **RANK_OPTIONS))
    again = checks.rank_positions(closing, 4, **RANK_OPTIONS) + checks.rank_positions(middle, 4, **RANK_OPTIONS)
    followed_again = checks.Followed(4, **RANK_OPTIONS)
    followed_again.values(list(followed.tasks.values()))
    result["repeats_exactly"] = json.dumps(again) == json.dumps(first_closing + first_middle) and followed_again.known == followed.known
    result["positions"] = [len(closing), len(middle)]
    result["broken"] = checks.estimate_rules(result) + ["a ranking did not repeat exactly"] * (not result["repeats_exactly"]) + ["a ranking lost worlds to a failure"] * bool(result["ranker_failures"])
    return result


def analyze(args):
    RESULT.unlink(missing_ok=True)
    names = sc.write_registry()
    manifest = check_frozen(names)
    search, plain = sc.load(SEARCH_DIR), sc.load(PLAIN_DIR)
    integrity = record_problems(search, plain, names, manifest["inputs"]["binary_id"], attempt_logs(manifest, names))
    if any(integrity[key] for key in ("missing_games", "name_mismatches", "job_mismatches", "build_mismatches")):
        ranker = {"broken": ["not run: the results are not this run's"]}
    else:
        ranker = ranker_gate(search, names)
    numbers, broken = decide(comparisons(search, plain), integrity, ranker)
    listed = list(search.values())
    jobs = {line.split("\t")[0]: line.rstrip("\n").split("\t") for line in search_jobs(names)}
    with_job = [dict(g, _job=jobs.get(g["id"], [""] * 14)) for g in listed]
    reported = {key: sum(g.get(key, 0) for g in listed) for key in ("searched", "departed", "fell_back", "deals_ok", "deals_refused", "rollouts", "worlds_void", "truncated", "rollout_guard_fires")}
    reported["cpu_per_game"] = round(sum(g.get("total_cpu", 0) for g in listed) / max(1, len(listed)), 1)
    reported["first_voids"] = sorted({g.get("first_void", "")[:120] for g in listed if g.get("first_void")})
    result = {"verdict": "FAIL" if broken else "PASS", "broken": broken, **numbers, "reported": {**reported, **study.splits(with_job), "calibration": study.calibration(with_job)}}
    RESULT.write_text(json.dumps(result, indent=1) + "\n")
    print(json.dumps({k: result[k] for k in ("verdict", "broken", "comparisons", "excluded", "overall", "overall_worst_case_low")}, indent=1))
    return 1 if broken else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=("selftest", "freeze", "play", "analyze"))
    parser.add_argument("--workers", type=int, default=8)
    args = parser.parse_args()
    if args.command == "selftest":
        return selftest()
    return {"freeze": freeze, "play": play, "analyze": analyze}[args.command](args) or 0


if __name__ == "__main__":
    raise SystemExit(main())
