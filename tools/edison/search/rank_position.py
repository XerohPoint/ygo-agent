#!/usr/bin/env python3
"""Rank the options of a recorded position, with estimated win rates.

A recorded position is a game's start (two lists and a seed) and the options played so far, by index.
Give them directly, or name a game written by edison_search and a decision in it:

  rank_position.py --deck0 blackwing --deck1 frog_monarch --seed 86100000 --moves 0,3,1
  rank_position.py --game build/edison-search/final/g12.json --decision 40

The estimate is the average rollout result under the search's deals, as a win rate for the acting seat. The deals assume the
opponent's list is known and both seats play on as the network does, so it is an estimate of those games, not of perfect play.
A draw counts as half a win. "± ?" marks an option with one rollout, which has no error estimate.
"""
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import search_common as sc  # noqa: E402

MSGS = ("idle", "chain", "select card", "tribute", "position", "effect yes/no", "yes/no", "battle", "select/unselect", "option", "place", "select sum", "place on field",
        "attribute", "number", "announce card", "race")
ACTS = ("", "Set", "Change position", "Special Summon", "Normal Summon", "Set monster", "Attack", "Direct attack", "Activate", "Cancel")
PHASES = ("", "Go to Battle Phase", "Go to Main Phase 2", "End turn")
POSITIONS = ("", "face-up attack", "face-down attack", "attack", "face-up defense", "face-up", "face-down defense", "face-down", "defense")


def describe(row_hex, names):
    row = bytes.fromhex(row_hex)
    card = names.get(row[1] * 256 + row[2], "")
    msg = MSGS[row[3] - 1] if 0 < row[3] <= len(MSGS) else f"prompt {row[3]}"
    act = ACTS[row[4]] if row[4] < len(ACTS) else f"act {row[4]}"
    phase = PHASES[row[7]] if row[7] < len(PHASES) else ""
    if row[3] in (5,):
        return f"{card}: {POSITIONS[row[8]] if row[8] < len(POSITIONS) else row[8]}".strip(": ")
    if row[5]:
        return "Finish selecting"
    if phase:
        return phase
    if row[3] in (7, 6, 10) and row[4] == 0:
        return f"{msg} {row[6]}" + (f": {card}" if card else "")
    if row[3] == 2 and not card and not act:
        return "Pass"
    if row[3] in (15, 17):
        return f"{msg} {row[9] + 256 * row[10]}"
    if row[3] in (11, 13):
        return f"{msg} zone {row[10]}"
    if row[3] == 14:
        return f"attribute {row[11]}"
    return " ".join(x for x in (act, card or msg) if x)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--deck0")
    parser.add_argument("--deck1")
    parser.add_argument("--seed", type=int)
    parser.add_argument("--moves", default="")
    parser.add_argument("--game", type=Path, help="A game file written by edison_search")
    parser.add_argument("--decision", type=int, help="Decision number in --game")
    parser.add_argument("--net", choices=sorted(sc.PARAMS_SHA), help="Default: the net the acting seat played in --game, or n1702")
    parser.add_argument("--budget", type=int, default=1024, help="Rollouts in total, shared evenly by the options")
    parser.add_argument("--depth", type=int, default=16)
    parser.add_argument("--search-seed", type=int, default=1)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    sc.write_registry()
    if args.game:
        game = json.loads(args.game.read_text())
        jobs = {line.split("\t")[0]: line.split("\t") for line in args.game.parent.with_suffix(".tsv").read_text().splitlines()}
        job = jobs[game["id"]]
        args.deck0, args.deck1, args.seed = job[1], job[2], int(job[3])
        moves = game["moves"][:args.decision]
        if not args.net:
            # As edison_search: search_seat -1 plays net in seat 0, otherwise net is the searching seat's and opponent_net the other's.
            seat = json.loads(sc.rank(args.deck0, args.deck1, args.seed, moves, net=job[5], budget=2, depth=0))["seat"]
            first = int(job[4]) if int(job[4]) >= 0 else 0
            args.net = job[5] if seat == first else (job[13] if len(job) > 13 else job[5])
    else:
        if not (args.deck0 and args.deck1 and args.seed is not None):
            parser.error("give --deck0 --deck1 --seed, or --game --decision")
        moves = [int(x) for x in args.moves.split(",") if x]
    args.net = args.net or "n1702"
    out = json.loads(sc.rank(args.deck0, args.deck1, args.seed, moves, net=args.net, budget=args.budget, depth=args.depth, search_seed=args.search_seed))
    if out["status"] == "OVER":
        print(json.dumps(out) if args.json else f"The game is over at this position. Winner: seat {out['winner']}.")
        return 0
    names = {cid: meta["name"] for cid, meta in sc.deck_registry.card_metadata().items()}
    options = out["options"]
    compared = [o for o in options if o["deals"]]
    for o in options:
        o["action"] = describe(o["row"], names)
        o["win_rate"] = (o["mean"] + 1) / 2 if o["deals"] else None
        o["margin"] = 1.96 * o["error"] / 2 if o["deals"] > 1 else None  # one rollout cannot estimate its own error
    out["ranked"] = sorted(options, key=lambda o: -(o["win_rate"] if o["win_rate"] is not None else -1))
    if args.json:
        print(json.dumps(out, indent=1))
        return 0
    print(f"{args.deck0} vs {args.deck1}, seed {args.seed}, decision {out['decision']}: seat {out['seat']} to act. Network's own estimate {100 * (out['network_value'] + 1) / 2:.1f}%.")
    if not compared:
        print(f"No sampled worlds: {out['deals_refused']} deals refused, or one option only. Network's choice: option {out['greedy']}.")
    for rank, o in enumerate(out["ranked"], 1):
        if o["win_rate"] is None:
            win = "   not compared"
        elif o["margin"] is None:
            win = f"{100 * o['win_rate']:5.1f}% ± ?  "
        else:
            win = f"{100 * o['win_rate']:5.1f}% ±{100 * o['margin']:4.1f}"
        marks = ("  search" if o["option"] == out["chosen"] else "") + ("  network" if o["option"] == out["greedy"] else "")
        print(f"{rank:3d}. {win}  [{o['option']:2d}] {o['action']}{marks}")
    print(f"{out['rollouts']} rollouts, depth {args.depth}, {out['deals_ok']} deals, {out['deals_refused']} refused, {out['worlds_void']} voided. Assumes the opponent's list is known.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
