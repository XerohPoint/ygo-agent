#!/usr/bin/env python3
"""Count the Substitoad thinning line and Deck-option label accuracy in logs written by line_probe.py.

Deck picks are read from the summoned card at the position prompt and tributes from the Graveyard, so label accuracy can be checked against them.
Field Dupe Frog reports as Des Frog in some views, so the end board accepts either.
"""
import argparse
import collections
import gzip
import json
from pathlib import Path
import sqlite3
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import decks  # noqa: E402

META = decks.card_metadata()
NAMES = {i: v["name"] for i, v in META.items()}
IDS = {v["name"]: i for i, v in META.items()}
SUB, TREE, SWAP, DUPE, DES = (IDS[n] for n in ("Substitoad", "Treeborn Frog", "Swap Frog", "Dupe Frog", "Des Frog"))
FROG_NAMES = ("Substitoad", "Treeborn Frog", "Swap Frog", "Dupe Frog", "Des Frog", "Flip Flop Frog", "Unifrog")
FROGS = {IDS[n] for n in FROG_NAMES}
PLACE, POSITION, SELECT, IDLE, CHAIN, EFFECT_YN, TRIBUTE, ACTIVATE = 11, 5, 3, 1, 2, 6, 4, 8


def frogs_in_list(deck, passcodes):
    zones = decks.parse_ydk(Path(decks.registry()[deck]).read_text())
    main = zones["main"] if isinstance(zones, dict) else zones[0]
    return sum(passcodes.get(int(code)) in FROG_NAMES for code in main)


def turns_of(game):
    out, current = [], None
    for entry in game["log"]:
        if entry["tp"] == 1:
            if current is None or current["n"] != entry["turn"]:
                current = {"n": entry["turn"], "entries": [], "after": None}
                out.append(current)
            current["entries"].append(entry)
        elif current is not None and current["after"] is None:
            current["after"] = entry
    return out


def is_activation(option):
    return option[0] in (IDLE, CHAIN) and option[1] == ACTIVATE and option[2] == SUB


def gained(before, after):
    return collections.Counter(after.get("5", [])) - collections.Counter(before.get("5", []))


def analyze(group, frog_total):
    c = collections.Counter()
    tributed, summoned, swapped, most = collections.Counter(), collections.Counter(), collections.Counter(), collections.Counter()
    examples = []
    for game in group:
        c["games"] += 1
        c["wins"] += game["won"]
        reached = used = False
        highest = 0
        for turn in turns_of(game):
            entries = turn["entries"]
            mine = [e for e in entries if e["mine"]]
            count = 0
            fed_treeborn = sent_toad = False
            pulled = []
            for at, entry in enumerate(entries):
                if not entry["mine"]:
                    continue
                option = entry["options"][entry["chosen"]]
                if any(is_activation(o) for o in entry["options"]):
                    reached = True
                    c["decisions_with_activation"] += 1
                if is_activation(option):
                    count += 1
                    used = True
                    later = entries[at + 1:at + 10]
                    # The cost reaches the Graveyard after any tribute prompt, so take the first change.
                    gain = next((g for g in (gained(entry["public"], step["public"]) for step in later) if g), {})
                    for code in gain:
                        tributed[NAMES.get(code, "?")] += 1
                        fed_treeborn |= code == TREE
                    own = [e for e in later if e["mine"]]
                    for i, step in enumerate(own):
                        first = step["options"][0][0]
                        if first == POSITION:
                            pulled.append(NAMES.get(step["options"][0][2], "?"))
                            summoned[pulled[-1]] += 1
                            # The cost is paid before the Deck prompt, so a prompt with nothing new in the Graveyard chose the tribute.
                            # With one candidate the Deck pick is automatic and no prompt counts.
                            prompt = next((p for p in reversed(own[:i]) if p["options"][0][0] == SELECT and gained(entry["public"], p["public"])), None)
                            c["summons_without_deck_prompt"] += prompt is None
                            if prompt:
                                labels = [o[2] for o in prompt["options"]]
                                c["deck_prompts"] += 1
                                c["label_right"] += labels[prompt["chosen"]] == step["options"][0][2]
                                c["chance_x1000"] += 1000 // len(labels)
                                c["options_shown"] += len(labels)
                                c["options_frog_label"] += sum(x in FROGS for x in labels)
                            break
                        if first == IDLE:
                            break
                if option[0] in (CHAIN, EFFECT_YN) and option[1] == ACTIVATE and option[2] == SWAP:
                    c["swap_taken"] += 1
                    for later in entries[at + 1:at + 4]:
                        gain = gained(entry["public"], later["public"])
                        if gain:
                            for code in gain:
                                swapped[NAMES.get(code, "?")] += 1
                                sent_toad |= code == SUB
                            break
                c["treeborn_revive_offered"] += option[0] in (CHAIN, EFFECT_YN) and any(o[1] == ACTIVATE and o[2] == TREE for o in entry["options"])
                c["treeborn_revived"] += option[0] in (CHAIN, EFFECT_YN) and option[1] == ACTIVATE and option[2] == TREE
                c["treeborn_tributed_for_summon"] += option[0] == TRIBUTE and option[2] == TREE
            highest = max(highest, count)
            after = turn["after"]
            field = after["public"].get("3", []) if after else []
            grave = after["public"].get("5", []) if after else []
            board = SUB in field and (DUPE in field or DES in field)
            seen = 0
            if mine:
                for zone in ("2", "3", "4", "5", "6"):
                    seen += sum(code in FROGS for code in mine[-1]["public"].get(zone, []))
            c["own_turns"] += 1
            c["turns_1plus"] += count >= 1
            c["turns_2plus"] += count >= 2
            c["turns_3plus"] += count >= 3
            c["turns_5plus"] += count >= 5
            c["end_board"] += board
            c["end_board_after_2plus"] += board and count >= 2
            c["treeborn_fed"] += fed_treeborn
            c["swap_sent_toad_turns"] += sent_toad
            c["deck_thinned_turns"] += count >= 1 and frog_total - seen <= 1
            full = count >= 3 and fed_treeborn and sent_toad and board
            c["full_line"] += full
            if count >= 3 and board and len(examples) < 3:
                examples.append({"seed": game["seed"], "turn": turn["n"], "activations": count, "summoned": pulled, "field": [NAMES.get(x, "?") for x in field],
                                 "frogs_in_gy": sum(x in FROGS for x in grave), "full_line": bool(full)})
        c["games_reached"] += reached
        c["games_used"] += used
        most[highest] += 1
    return {"counts": dict(c), "tributed": tributed.most_common(8), "summoned": summoned.most_common(8), "swap_sent": swapped.most_common(8), "most_in_one_turn": sorted(most.items()), "examples": examples}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--logs", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    db = sqlite3.connect(decks.DB)
    passcodes = dict(db.execute("select id,name from texts"))
    groups = collections.defaultdict(list)
    with gzip.open(args.logs, "rt") as f:
        for line in f:
            row = json.loads(line)
            groups[row["label"], row["deck"]].extend(row["games"])
    report = {f"{label}/{deck}": analyze(games, frogs_in_list(deck, passcodes)) for (label, deck), games in sorted(groups.items())}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=1) + "\n")
    for key, row in report.items():
        c = row["counts"]
        print(f"\n{key}: {c['games']} games, won {100 * c['wins'] / c['games']:.1f}%; reached {c['games_reached']}, used {c['games_used']}")
        print(f" turns 1+ {c['turns_1plus']} 2+ {c['turns_2plus']} 3+ {c['turns_3plus']} 5+ {c['turns_5plus']} of {c['own_turns']}; most {row['most_in_one_turn']}")
        print(f" tributed {row['tributed'][:5]}; summoned {row['summoned'][:5]}")
        print(f" swap {c.get('swap_taken', 0)} sent {row['swap_sent'][:5]}; Treeborn fed {c['treeborn_fed']} revived {c.get('treeborn_revived', 0)}/{c.get('treeborn_revive_offered', 0)} tribute {c.get('treeborn_tributed_for_summon', 0)}")
        print(f" board {c['end_board']} (after 2+ {c['end_board_after_2plus']}); thinned {c['deck_thinned_turns']}; full {c['full_line']}; labels right {c.get('label_right', 0)}/{c.get('deck_prompts', 0)}, chance {c.get('chance_x1000', 0) / 1000:.1f}; Frog labels {c.get('options_frog_label', 0)}/{c.get('options_shown', 0)}")
        for example in row["examples"]:
            print("  e.g.", example)


if __name__ == "__main__":
    main()
