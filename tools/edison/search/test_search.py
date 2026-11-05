#!/usr/bin/env python3
"""The search's own checks. Seeds 86000000-86009999. Each stage prints one JSON line and writes it to build/edison-search/tests.

  production  the network in both seats through the Referee equals the certified module under JAX, every observation byte
  ignored     running the search and playing the network's choice anyway changes nothing in the game
  determinism the same jobs on other worker counts give the same games
  privacy     a search on two natural duels with one public history answers the same; planted leaks must show
  refusal     an assumed list that cannot be true gives no worlds, and the network's choice stands
  voids       failures planted in card scripts void sampled worlds only
  semantics   estimates must point the right way: a broken leaf or terminal sign fails it
  key         deal seeds come from the seat's own decisions: the duel's whole count fails it
  provenance  an output directory refuses games from other jobs
  ranker      the ranking command's network choice is the move the acting seat played, with the right net

A stage fails if its result breaks its rule, and the program's exit code is the verdict. Every stage plays into a fresh directory.
"""
import argparse
import json
import math
from multiprocessing.pool import ThreadPool
import os
from pathlib import Path
import random
import shutil
import subprocess
import sys
import zlib

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import search_common as sc  # noqa: E402

TESTS = sc.OUT / "tests"
SEED = 86000000
SEMANTIC_GAP = 0.25  # a position estimated high must end in a win this much more often than one estimated low
SEMANTIC_MIN = {"mid": 200, "sure": 20}  # decisions each side of a band needs before its result means anything
CLOSING_HIGH = 0.8  # a move that ended the game, for the seat that won, should be estimated at least this high
CLOSING_SHARE = 0.7  # in at least this share of such positions
CLOSING_TOP = 0.7  # and to rank within CLOSING_TIE of the best option in at least this share, so numbers on the wrong option fail
CLOSING_TIE = 0.05


FAILURES = []


def report(name, result, broken=()):
    """broken holds the stage's rules that its result breaks."""
    TESTS.mkdir(parents=True, exist_ok=True)
    result = {**result, "broken": list(broken)}
    (TESTS / f"{name}.json").write_text(json.dumps(result, indent=1) + "\n")
    print(json.dumps({"stage": name, "verdict": "FAIL" if broken else "pass", **{k: v for k, v in result.items() if not isinstance(v, (list, dict)) or len(str(v)) < 300}}), flush=True)
    FAILURES.extend(f"{name}: {b}" for b in broken)
    return result


def play(lines, out, workers, **options):
    return sc.play(lines, TESTS / out, workers, fresh=True, **options)


def plain_jobs(names, games, seed, net="n1702"):
    pairs = sc.neighbour_pairs(names)
    return [sc.job_line(f"g{i}", *pairs[i % len(pairs)], seed + i, -1, net) for i in range(games)]


def stage_production(args, names):
    sys.path.insert(0, str(sc.ROOT))
    sys.path.insert(0, str(sc.ROOT / "ygoenv"))
    import hashlib
    import collect_decisions as cd
    import ygoenv
    from ygoenv.ygopro import init_module, ygopro_ygoenv
    from actor import NetPolicy, MAX_OPTIONS, MAX_STEPS
    native = hashlib.sha256(Path(ygopro_ygoenv.__file__).read_bytes()).hexdigest()
    if not native.startswith(cd.NATIVE_PREFIX):
        raise SystemExit(f"Native module is not certified: {native}")
    games = args.games
    lines = plain_jobs(names, games, SEED)
    native_games = play(lines, "production", args.workers)
    cwd = os.getcwd()
    os.chdir(sc.SCRIPTS)
    registry = sc.deck_registry.registry()
    init_module(str(sc.deck_registry.DB), str(sc.deck_registry.CODES), {k: str(v) for k, v in registry.items()})
    metadata = sc.deck_registry.card_metadata()
    stem = cd.CHECKPOINTS[1702]
    nets = {1702: NetPolicy(stem, json.loads(Path(stem + ".json").read_text())["params_sha256"])}

    def make_env(cell, count):
        return ygoenv.make("YGOPro-v1", env_type="gymnasium", num_envs=count, num_threads=1, seed=cell["seed"], deck1=cell["deck0"], deck2=cell["deck1"], play_mode="self",
                           player=-1, async_reset=False, greedy_reward=False, oppo_info=False, max_options=MAX_OPTIONS, max_steps=MAX_STEPS, max_episode_steps=MAX_STEPS,
                           timeout=10, verbose=False)
    diffs = []
    decisions = 0
    for line in lines:
        job_id, deck0, deck1, seed = line.split("\t")[:4]
        cell = {"index": 0, "deck0": deck0, "deck1": deck1, "seed": int(seed), "deck0_id": 0, "deck1_id": 0, "policies": ("net-greedy:1702", "net-greedy:1702")}
        buffers, finished = cd.play_cell(cell, 1, nets, metadata, len(metadata), make_env)
        crc = 0
        for rec in buffers[0]:
            r = rec[0]
            for field, size in (("cards", None), ("selection", None), ("actions", int(r["counts"])), ("hist", None), ("decision", None), ("global", None)):
                crc = zlib.crc32(r[field][:size].tobytes() if size is not None else r[field].tobytes(), crc)
        mine = native_games[job_id]
        decisions += len(buffers[0])
        if mine["crc"] != crc or mine["winner"] != finished[0] or mine["decisions"] != len(buffers[0]):
            diffs.append({"job": job_id, "deck0": deck0, "deck1": deck1, "native": [mine["crc"], mine["winner"], mine["decisions"]], "jax": [crc, finished[0], len(buffers[0])]})
    os.chdir(cwd)
    return report("production", {"games": games, "decisions": decisions, "games_differing": len(diffs), "diffs": diffs[:12]}, ["games differ"] * bool(diffs) + ["no decisions"] * (not decisions))


def stage_ignored(args, names):
    games = args.games
    pairs = sc.neighbour_pairs(names)
    plain = play([sc.job_line(f"g{i}", *pairs[i % len(pairs)], SEED + 1000 + i, -1, "n1703") for i in range(games)], "ignored-plain", args.workers)
    ran = play([sc.job_line(f"g{i}", *pairs[i % len(pairs)], SEED + 1000 + i, i % 2, "n1703", 24, 8, 4, 1.0, 1, 7, 1) for i in range(games)], "ignored-search", args.workers)
    keys = ("status", "winner", "win_reason", "decisions", "crc", "moves")
    bad = [k for k in plain if any(plain[k].get(f) != ran[k].get(f) for f in keys)]
    searches = sum(v.get("searched", 0) for v in ran.values())
    voided = sum(v["status"] != "OK" for v in ran.values())
    return report("ignored", {"games": games, "decisions": sum(v.get("decisions", 0) for v in plain.values()), "searches_run": searches, "games_differing": len(bad), "void": voided, "first": bad[:8]},
                  ["games differ"] * bool(bad) + ["no searches ran"] * (not searches) + ["games voided"] * bool(voided))


def stage_determinism(args, names):
    games = min(args.games, 64)
    pairs = sc.neighbour_pairs(names)
    lines = [sc.job_line(f"g{i}", *pairs[i % len(pairs)], SEED + 2000 + i, i % 2, "n1704", 24, 8, 4, 1.0, 1, 11, 0) for i in range(games)]
    one = play(lines, "det-one", 1)
    many = play(lines, "det-many", args.workers)
    strip = lambda d: {k: v for k, v in d.items() if k not in ("search_cpu", "total_cpu")}
    bad = [k for k in one if strip(one[k]) != strip(many[k])]
    searches = sum(v.get("searched", 0) for v in one.values())
    return report("determinism", {"games": games, "searches": searches, "games_differing": len(bad), "first": bad[:8]}, ["games differ"] * bool(bad) + ["no searches ran"] * (not searches))


def positions(names, games, seed):
    played = play(plain_jobs(names, games, seed, "n1702"), "position-games", 3)
    lines = (TESTS / "position-games.tsv").read_text().splitlines()
    rng = random.Random(seed)
    out = []
    for line in lines:
        job, deck0, deck1, game_seed = line.split("\t")[:4]
        d = played[job]
        if d["status"] != "OK":
            continue
        for stage in (0.1, 0.5, 0.9):
            index = int(stage * d["decisions"])
            out.append((deck0, deck1, int(game_seed), d["moves"][:index]))
    rng.shuffle(out)
    return out


def pair_run(label, binary, per_cell, workers, budget, depth, mutant=None):
    log = TESTS / f"pairs-{label}.jsonl"
    command = [str(binary), "redeal", "--root", "..", "--per-cell", str(per_cell), "--deals", "2", "--cap", "1500", "--workers", str(workers), "--out", str(TESTS / f"pairs-{label}.out"),
               "--search-net", str(sc.OUT / "nets/gauntlet-1702.ednet"), "--search-params", sc.PARAMS_SHA["n1702"], "--search-codes", sc.CODES_SHA, "--search-budget", str(budget),
               "--search-depth", str(depth), "--search-log", str(log)]
    if mutant:
        command += ["--mutant", mutant]
    subprocess.run(command, cwd=sc.SCRIPTS, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    rows = [json.loads(line) for line in log.read_text().splitlines()]
    return {family: pair_counts([r for r in rows if r["family"] == code]) for family, code in (("r4", 0), ("r4w_same_timeline", 1), ("r4w_private_difference", 2))}


def pair_counts(rows):
    searched = [r for r in rows if r["worlds"][0] and r["worlds"][1]]
    return {"pairs": len(rows), "both_searched": len(searched), "one_refused": sum(bool(r["worlds"][0]) != bool(r["worlds"][1]) for r in rows),
            "exact_same": sum(r["exact"] for r in searched), "rounded_same": sum(r["close"] for r in searched), "chosen_same": sum(r["chosen"] for r in searched),
            "first_differences": [[r["deck0"], r["deck1"], r["game"], r["index"]] for r in searched if not r["exact"]][:8]}


def stage_privacy(args, names):
    """Two natural duels with one public history, built by the forward model's R4 and R4W rigs on its corpus (read, not replayed by policy)."""
    corpus = sc.ROOT / "build/forward_model/corpus"
    if not (corpus / "games.txt").exists():
        raise SystemExit("needs the forward model's corpus: collect_corpus.py")
    summary = {"clean": pair_run("clean", sc.ROOT / "build/forward_model/fm_bench", args.per_cell, args.workers, 48, 6)}
    for mutant in ("leak-hand", "leak-seed", "leak-order"):
        summary[mutant] = pair_run(mutant, sc.ROOT / "build/forward_model/fm_bench_mutant", 2, args.workers, 48, 6, mutant)
    broken = []
    for family, c in summary["clean"].items():
        if not c["both_searched"]:
            broken.append(f"clean {family}: no pairs")
        if c["exact_same"] != c["both_searched"]:
            broken.append(f"clean {family}: answers differ")
        if c["one_refused"] > 0.01 * c["pairs"]:
            broken.append(f"clean {family}: too many one-sided refusals")
    for mutant in ("leak-hand", "leak-seed", "leak-order"):
        for family, c in summary[mutant].items():
            if c["exact_same"] == c["both_searched"]:
                broken.append(f"{mutant} {family}: the planted leak went unseen")
    return report("privacy", summary, broken)


def stage_refusal(args, names):
    pos = positions(names, 16, SEED + 3000)[:32]
    total = bad = compared = 0
    for deck0, deck1, seed, moves in pos:
        wrong = next(n for n in names if n not in (deck0, deck1))
        d = json.loads(sc.rank(deck0, deck1, seed, moves, assume_opponent=wrong, budget=16, depth=2))
        total += 1
        compared += d["deals_refused"] > 0
        bad += not (d["deals_ok"] == 0 and d["rollouts"] == 0 and d["chosen"] == d["greedy"])
    return report("refusal", {"positions": total, "positions_with_refused_deals": compared, "searched_on_a_wrong_list": bad}, ["searched on a wrong list"] * bool(bad) + ["too few refused positions"] * (compared < total // 2))


PLANTS = {"lua": 'error("planted failure in a sampled world")', "prompt": "Duel.SelectMatchingCard(tp,aux.TRUE,tp,0,LOCATION_DECK,1,1,nil)", "hang": "while true do end"}
PLANT_CARD = 19613556  # Heavy Storm


def overlay(name, body):
    source = Path("/home/mada/ygopro-scripts")
    target = TESTS / f"overlay-{name}" / "script"
    if target.parent.exists():
        shutil.rmtree(target.parent)
    target.mkdir(parents=True)
    for item in source.iterdir():
        (target / item.name).symlink_to(item)
    (target / f"c{PLANT_CARD}.lua").unlink()
    text = (source / f"c{PLANT_CARD}.lua").read_text()
    (target / f"c{PLANT_CARD}.lua").write_text(text + f"""
local planted_initial_effect=c{PLANT_CARD}.initial_effect
function c{PLANT_CARD}.initial_effect(c)
	planted_initial_effect(c)
	local e=Effect.CreateEffect(c)
	e:SetType(EFFECT_TYPE_CONTINUOUS+EFFECT_TYPE_FIELD)
	e:SetCode(EVENT_SUMMON_SUCCESS)
	e:SetRange(LOCATION_HAND)
	e:SetOperation(function(e,tp,eg,ep,ev,re,r,rp) {body} end)
	c:RegisterEffect(e)
end
""")
    return target.parent


def stage_voids(args, names):
    summary = {}
    for name, body in PLANTS.items():
        games = 2 if name == "hang" else 16
        budget = 16 if name == "hang" else 32
        cwd = overlay(name, body)
        lines = [sc.job_line(f"g{i}", "g_diva_frog", "g_hero_beat", SEED + 4000 + i, 1, "n1702", budget, 12, 4, 1.0, 1, 3, 0) for i in range(games)]
        out = TESTS / f"voids-{name}"
        if out.exists():
            shutil.rmtree(out)
        played = sc.play(lines, out, args.workers, cwd=cwd, timeout=1800, fresh=True)
        ok = [d for d in played.values() if d["status"] == "OK"]
        summary[name] = {"games": games, "finished": len(ok), "worlds_void": sum(d.get("worlds_void", 0) for d in played.values()), "worker_deaths": sum("worker ended" in d.get("reason", "") for d in played.values()),
                         "true_duel_voids": sum("true duel" in d.get("reason", "") for d in played.values()), "reasons": sorted({d.get("reason", "")[:80] for d in played.values() if d["status"] != "OK"})}
    broken = []
    for name, c in summary.items():
        if c["worker_deaths"]:
            broken.append(f"{name}: workers died")
        if not c["worlds_void"]:
            broken.append(f"{name}: the plant voided no sampled world")
        if not c["finished"]:
            broken.append(f"{name}: no game finished")
    return report("voids", summary, broken)


def direction(played, low=0.6):
    """Won share of decisions by the sign of the network choice's estimate: all of them, and the near-certain ones, which are mostly finished rollouts."""
    table = {"mid": [[0, 0], [0, 0]], "sure": [[0, 0], [0, 0]]}
    for g in played.values():
        if g["status"] != "OK" or g["winner"] not in (0, 1):
            continue
        for row in g["log"]:
            if row[2] < 2:
                continue
            mean = row[6]
            band = "sure" if abs(mean) >= 0.97 else ("mid" if 0.3 <= abs(mean) <= low + 0.3 else None)
            if band:
                cell = table[band][mean > 0]
                cell[0] += g["winner"] == g["search_seat"]
                cell[1] += 1
    rate = lambda c: c[0] / max(1, c[1])
    return {band: {"won_when_low": round(rate(t[0]), 3), "won_when_high": round(rate(t[1]), 3), "decisions": [t[0][1], t[1][1]]} for band, t in table.items()}


def stage_semantics(args, names):
    """Estimates against how the games ended, in games where the network plays regardless. Rows carry the mean of the network's own option."""
    pairs = sc.neighbour_pairs(names)
    lines = [sc.job_line(f"g{i}", *pairs[i % len(pairs)], SEED + 5000 + i, i % 2, "n1702", 24, 8, 4, 1.0, 1, 13, 1) for i in range(args.games)]
    runs = {"clean": direction(play(lines, "semantics-clean", args.workers))}
    for mutant in ("flip-leaf", "flip-terminal"):
        runs[mutant] = direction(play(lines, f"semantics-{mutant}", args.workers, binary=sc.TEST_BIN, extra=("--mutant", mutant)))
    runs["no-rollouts"] = direction(play(lines, "semantics-no-rollouts", args.workers, binary=sc.TEST_BIN, extra=("--mutant", "no-rollouts")))
    broken = semantic_rules(runs)
    empty = semantic_rules({**runs, "flip-leaf": runs["no-rollouts"], "flip-terminal": runs["no-rollouts"]})
    broken += ["controls that produced no estimates were accepted"] * (not any("too few" in b for b in empty))
    return report("semantics", runs, broken)


def semantic_rules(runs):
    points = lambda d: d["won_when_high"] - d["won_when_low"]
    thin = lambda d, band: min(d[band]["decisions"]) < SEMANTIC_MIN[band]
    broken = []
    for band in ("mid", "sure"):
        if thin(runs["clean"], band):
            broken.append(f"clean {band}: too few decisions")
        elif points(runs["clean"][band]) < SEMANTIC_GAP:
            broken.append(f"clean {band}: estimates do not separate wins from losses")
    for mutant, band in (("flip-leaf", "mid"), ("flip-terminal", "sure")):
        if thin(runs[mutant], band):
            broken.append(f"{mutant}: too few decisions to show anything")
        elif points(runs[mutant][band]) > 0:
            broken.append(f"{mutant} went unseen")
    return broken


def stage_key(args, names):
    """A row's last column is the deal key. It must be exactly how many decisions that seat had made, counted from the game's own record of who acted."""
    pairs = sc.neighbour_pairs(names)
    lines = [sc.job_line(f"g{i}", *pairs[i % len(pairs)], SEED + 6000 + i, i % 2, "n1702", 12, 4, 4, 1.0, 1, 17, 1) for i in range(8)]

    def check(played):
        wrong = rows = behind = 0
        for g in played.values():
            for row in g["log"]:
                decision, key = row[0], row[-1]
                rows += 1
                wrong += key != sum(seat == g["search_seat"] for seat in g["seats"][:decision])
                behind += key < decision
        return {"rows": rows, "wrong": wrong, "behind_the_duel": behind}
    clean = check(play(lines, "key-clean", args.workers))
    planted = {name: check(play(lines, f"key-{name}", args.workers, binary=sc.TEST_BIN, extra=("--mutant", name))) for name in ("total-key", "half-key")}
    broken = ["no rows"] * (not clean["rows"]) + ["a key is not the seat's own count"] * bool(clean["wrong"]) + ["the seats never fell out of step, so this shows nothing"] * (not clean["behind_the_duel"])
    broken += [f"{name} went unseen" for name, result in planted.items() if not result["wrong"]]
    return report("key", {"clean": clean, **planted}, broken)


def stage_provenance(args, names):
    """A directory must not hand back games from other jobs or inputs, through the script or the binary."""
    pairs = sc.neighbour_pairs(names)
    first = [sc.job_line("g0", *pairs[0], SEED + 7000, -1, "n1702")]
    second = [sc.job_line("g0", *pairs[0], SEED + 7001, -1, "n1702")]
    play(first, "provenance", 1)
    refused_script = refused_binary = False
    try:
        sc.play(second, TESTS / "provenance", 1)
    except SystemExit:
        refused_script = True
    (TESTS / "provenance.tsv").write_text("".join(second))
    done = subprocess.run([str(sc.BIN), "play", "--root", str(sc.ROOT), "--registry", str(sc.REGISTRY), *sc.net_args(["n1702"]), "--codes-sha", sc.CODES_SHA, "--jobs", str(TESTS / "provenance.tsv"),
                           "--out", str(TESTS / "provenance"), "--workers", "1"], cwd=sc.SCRIPTS, capture_output=True, text=True)
    refused_binary = done.returncode != 0
    (TESTS / "provenance.tsv").write_text("".join(first))
    done = subprocess.run([str(sc.TEST_BIN), "play", "--root", str(sc.ROOT), "--registry", str(sc.REGISTRY), *sc.net_args(["n1702"]), "--codes-sha", sc.CODES_SHA, "--jobs", str(TESTS / "provenance.tsv"),
                           "--out", str(TESTS / "provenance"), "--workers", "1"], cwd=sc.SCRIPTS, capture_output=True, text=True)
    refused_build = done.returncode != 0
    refused = {"other jobs, script": refused_script, "other jobs, binary": refused_binary, "other build, binary": refused_build}
    for label, options in (("other build, script", {"binary": sc.TEST_BIN}), ("other Lua", {"cwd": overlay("provenance", "")}), ("other arguments", {"extra": ("--mutant", "x")})):
        try:
            sc.play(first, TESTS / "provenance", 1, **options)
            refused[label] = False
        except SystemExit:
            refused[label] = True
    return report("provenance", refused, [f"{label}: reused the games" for label, ok in refused.items() if not ok])


def stage_ranker(args, names):
    """In games between two nets, the ranker's network choice must be the move the acting seat played, so it loaded that seat's net at that decision."""
    pairs = sc.neighbour_pairs(names)
    lines = [sc.job_line(f"g{i}", *pairs[i * 7 % len(pairs)], SEED + 7100 + i, -1, "n1702", opponent_net="n1703") for i in range(4)]
    played = play(lines, "ranker", args.workers)
    rng = random.Random(SEED + 7100)
    script = str(sc.HERE / "rank_position.py")

    def rank(path, decision, *more):
        done = subprocess.run([sys.executable, script, "--game", str(path), "--decision", str(decision), "--budget", "2", "--depth", "0", "--json", *more], cwd=sc.SCRIPTS, capture_output=True, text=True, check=True)
        return json.loads(done.stdout)
    checked = right = wrong_net_right = seats = 0
    for key, game in played.items():
        path = TESTS / "ranker" / f"{key}.json"
        for decision in rng.sample(range(game["decisions"]), 12):
            d = rank(path, decision)
            if len(d["options"]) < 2:
                continue
            checked += 1
            seats += d["seat"]
            right += d["greedy"] == game["moves"][decision]
            wrong = rank(path, decision, "--net", "n1703" if d["seat"] == 0 else "n1702")
            wrong_net_right += wrong["greedy"] == game["moves"][decision]
    broken = ["too few decisions"] * (checked < 20) + ["one seat never checked"] * (seats in (0, checked))
    broken += ["the ranker's network choice is not the move played"] * (right != checked) + ["using the other net went unseen"] * (wrong_net_right == checked)
    return report("ranker", {"checked": checked, "seat_1": seats, "network_choice_is_the_move": right, "with_the_other_net": wrong_net_right}, broken)


def closing_decision(game):
    """The last real choice of the seat that won. None if the game has none."""
    for decision in range(game["decisions"] - 1, -1, -1):
        if game["counts"][decision] > 1 and game["seats"][decision] == game["winner"]:
            return decision
    return None


def judge_rank(position, out):
    """The numbers a ranking shows must follow from its samples. Returns the recorded move's estimate as a win rate, or None if it wasn't compared."""
    bad = 0
    for o in out["options"]:
        n = len(o["samples"])
        bad += o["deals"] != n
        if n:
            mean = sum(o["samples"]) / n
            error = math.sqrt(max(0.0, sum((v - mean) ** 2 for v in o["samples"]) / (n - 1)) / n) if n > 1 else 0.0
            bad += abs(o["mean"] - mean) > 1e-9 or abs(o["error"] - error) > 1e-9
    played = out["options"][position["move"]]
    return bad, (played["mean"] + 1) / 2 if played["deals"] else None


def rank_positions(positions, workers, binary=sc.BIN, **options):
    def one(position):
        return json.loads(sc.rank(*position["start"], position["moves"], net=position["net"], binary=binary, samples=1, **options))
    with ThreadPool(workers) as pool:
        return pool.map(one, positions)


def judge_estimates(closing, closing_out, middle, middle_out):
    """Against outcomes the ranker can't see: moves that won the game at once, and mid-game positions by who went on to win."""
    bad = high = top = compared = 0
    for position, out in zip(closing, closing_out):
        wrong, estimate = judge_rank(position, out)
        bad += wrong
        if estimate is not None:
            compared += 1
            high += estimate >= CLOSING_HIGH
            top += estimate >= max((o["mean"] + 1) / 2 for o in out["options"] if o["deals"]) - CLOSING_TIE
    won, lost = [], []
    for position, out in zip(middle, middle_out):
        wrong, estimate = judge_rank(position, out)
        bad += wrong
        if estimate is not None:
            (won if position["won"] else lost).append(estimate)
    gap = float(np.mean(won) - np.mean(lost)) if won and lost else 0.0
    return {"numbers_not_from_samples": bad, "closing": compared, "closing_high": high, "closing_top": top, "middle": [len(won), len(lost)], "middle_gap": round(gap, 3)}


def estimate_rules(result):
    broken = ["a shown number does not follow from its samples"] * bool(result["numbers_not_from_samples"])
    broken += ["too few closing positions"] * (result["closing"] < 20) + ["moves that won are not estimated high"] * (result["closing_high"] < CLOSING_SHARE * max(1, result["closing"]))
    broken += ["moves that won don't rank near the top"] * (result["closing_top"] < CLOSING_TOP * max(1, result["closing"]))
    broken += ["too few middle positions"] * (min(result["middle"]) < 10) + ["estimates do not separate later wins from losses"] * (result["middle_gap"] < SEMANTIC_GAP)
    return broken


def game_positions(jobs, played, rng, middle_per_game=2):
    closing, middle = [], []
    for job in jobs:
        fields = job.rstrip("\n").split("\t")
        game = played[fields[0]]
        if game["status"] != "OK" or game["winner"] not in (0, 1):
            continue
        make = lambda d: {"start": (fields[1], fields[2], int(fields[3])), "moves": game["moves"][:d], "move": game["moves"][d], "net": fields[5], "won": game["seats"][d] == game["winner"], "id": fields[0], "decision": d}
        d = closing_decision(game)
        if d is not None:
            closing.append(make(d))
        choices = [i for i in range(game["decisions"]) if game["counts"][i] > 1]
        middle += [make(d) for d in rng.sample(choices, min(middle_per_game, len(choices)))]
    return closing, middle


def stage_estimates(args, names):
    """The ranker's estimates, labels and errors, on positions with known outcomes. The planted rankers must all fail."""
    pairs = sc.neighbour_pairs(names)
    lines = [sc.job_line(f"g{i}", *pairs[i * 5 % len(pairs)], SEED + 7200 + i, -1, "n1702") for i in range(32)]
    played = play(lines, "estimates", args.workers)
    closing, middle = game_positions(lines, played, random.Random(SEED + 7200))
    options = {"budget": 64, "depth": 48}
    run = lambda binary=sc.BIN, **more: judge_estimates(closing, rank_positions(closing, args.workers, binary=binary, **options, **more), middle, rank_positions(middle, args.workers, binary=binary, **options, **more))
    clean = run()
    planted = {name: run(binary=sc.TEST_BIN, rank_mutant=name) for name in ("zero", "blank", "rotate")}
    broken = estimate_rules(clean)
    broken += [f"the planted {name} ranker passed" for name, result in planted.items() if not estimate_rules(result)]
    # Labels: at a game's first decision, every card an option names comes from the acting seat's own list.
    from rank_position import describe
    metadata = sc.deck_registry.card_metadata()
    names_by_id = {cid: meta["name"] for cid, meta in metadata.items()}
    lists = sc.deck_registry.registry()
    label_errors = labels = 0
    for job in lines:
        fields = job.split("\t")
        out = json.loads(sc.rank(fields[1], fields[2], int(fields[3]), [], net=fields[5], budget=2, depth=0))
        zones = sc.deck_registry.parse_ydk(Path(lists[fields[1 + out["seat"]]]).read_text())
        own = {metadata[cid]["name"] for cid, meta in metadata.items() if any(meta["code"] in codes for codes in zones.values())}
        for o in out["options"]:
            row = bytes.fromhex(o["row"])
            card = names_by_id.get(row[1] * 256 + row[2], "")
            labels += bool(card)
            label_errors += bool(card) and (card not in own or card not in describe(o["row"], names_by_id))
    broken += ["no labels named a card"] * (not labels) + ["an opening option names a card outside the acting seat's list"] * bool(label_errors)
    return report("estimates", {"clean": clean, **planted, "labels": labels, "label_errors": label_errors}, broken)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stages", nargs="+")
    parser.add_argument("--games", type=int, default=84)
    parser.add_argument("--per-cell", type=int, default=6)
    parser.add_argument("--workers", type=int, default=4)
    args = parser.parse_args()
    names = sc.write_registry()
    for stage in args.stages:
        globals()[f"stage_{stage}"](args, names)
    if FAILURES:
        print("FAILED:\n  " + "\n  ".join(FAILURES), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
