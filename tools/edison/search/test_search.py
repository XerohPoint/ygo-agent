#!/usr/bin/env python3
"""The search's own checks. Seeds 86000000-86009999. Each stage prints one JSON line and writes it to build/edison-search/tests.

  production  the network in both seats through the Referee equals the certified module under JAX, every observation byte
  ignored     running the search and playing the network's choice anyway changes nothing in the game
  determinism the same jobs on other worker counts give the same games
  privacy     a search on two natural duels with one public history answers the same; planted leaks must show
  refusal     an assumed list that cannot be true gives no worlds, and the network's choice stands
  voids       failures planted in card scripts void sampled worlds only
"""
import argparse
import json
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


def report(name, result):
    TESTS.mkdir(parents=True, exist_ok=True)
    (TESTS / f"{name}.json").write_text(json.dumps(result, indent=1) + "\n")
    print(json.dumps({"stage": name, **{k: v for k, v in result.items() if not isinstance(v, (list, dict)) or len(str(v)) < 300}}), flush=True)
    return result


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
    native_games = sc.play(lines, TESTS / "production", args.workers)
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
    return report("production", {"games": games, "decisions": decisions, "games_differing": len(diffs), "diffs": diffs[:12]})


def stage_ignored(args, names):
    games = args.games
    pairs = sc.neighbour_pairs(names)
    plain = sc.play([sc.job_line(f"g{i}", *pairs[i % len(pairs)], SEED + 1000 + i, -1, "n1703") for i in range(games)], TESTS / "ignored-plain", args.workers)
    ran = sc.play([sc.job_line(f"g{i}", *pairs[i % len(pairs)], SEED + 1000 + i, i % 2, "n1703", 24, 8, 4, 1.0, 1, 7, 1) for i in range(games)], TESTS / "ignored-search", args.workers)
    keys = ("status", "winner", "win_reason", "decisions", "crc", "moves")
    bad = [k for k in plain if any(plain[k].get(f) != ran[k].get(f) for f in keys)]
    return report("ignored", {"games": games, "decisions": sum(v.get("decisions", 0) for v in plain.values()), "searches_run": sum(v.get("searched", 0) for v in ran.values()),
                              "games_differing": len(bad), "first": bad[:8]})


def stage_determinism(args, names):
    games = min(args.games, 64)
    pairs = sc.neighbour_pairs(names)
    lines = [sc.job_line(f"g{i}", *pairs[i % len(pairs)], SEED + 2000 + i, i % 2, "n1704", 24, 8, 4, 1.0, 1, 11, 0) for i in range(games)]
    one = sc.play(lines, TESTS / "det-one", 1)
    many = sc.play(lines, TESTS / "det-many", args.workers)
    strip = lambda d: {k: v for k, v in d.items() if k not in ("search_cpu", "total_cpu")}
    bad = [k for k in one if strip(one[k]) != strip(many[k])]
    return report("determinism", {"games": games, "searches": sum(v.get("searched", 0) for v in one.values()), "games_differing": len(bad), "first": bad[:8]})


def positions(names, games, seed):
    played = sc.play(plain_jobs(names, games, seed, "n1702"), TESTS / "position-games", 3)
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
    return report("privacy", summary)


def stage_refusal(args, names):
    pos = positions(names, 16, SEED + 3000)[:32]
    total = bad = compared = 0
    for deck0, deck1, seed, moves in pos:
        wrong = next(n for n in names if n not in (deck0, deck1))
        d = json.loads(sc.rank(deck0, deck1, seed, moves, assume_opponent=wrong, budget=16, depth=2))
        total += 1
        compared += d["deals_refused"] > 0
        bad += not (d["deals_ok"] == 0 and d["rollouts"] == 0 and d["chosen"] == d["greedy"])
    return report("refusal", {"positions": total, "positions_with_refused_deals": compared, "searched_on_a_wrong_list": bad})


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
        played = sc.play(lines, out, args.workers, cwd=cwd, timeout=1800)
        ok = [d for d in played.values() if d["status"] == "OK"]
        summary[name] = {"games": games, "finished": len(ok), "worlds_void": sum(d.get("worlds_void", 0) for d in played.values()), "worker_deaths": sum("worker ended" in d.get("reason", "") for d in played.values()),
                         "true_duel_voids": sum("true duel" in d.get("reason", "") for d in played.values()), "reasons": sorted({d.get("reason", "")[:80] for d in played.values() if d["status"] != "OK"})}
    return report("voids", summary)


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


if __name__ == "__main__":
    main()
