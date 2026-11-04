#!/usr/bin/env python3
"""Separate cards that cause an interface crash from cards that were merely in a crashing duel.

Decks carry other cards, so one crash beside a target proves little. A card counts when its
script has the matching feature and it crashed or was attributed, or when at least a third
of its dedicated duels crashed with at least 2 crashes.
"""
import collections, gzip, json, re, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import *

REP = ROOT / "tests/edison/reports/pool-audit-2026-11-04"
FEATURE = [("toss_coin", {"toss_coin", "announce_coin"}), ("toss_dice", {"toss_dice"}), ("id: 1104", {"spirit_return"}),
           ("id: 60", {"announce_coin", "toss_coin"}), ("id: 70", {"announce_type"}), ("opcodes", {"announce_card"}),
           ("Card not found: 1", {"announce_card"}), ("announce_race", {"announce_race"}), ("field_disabled", {"field_disabled"}),
           ("deck_top", {"deck_top"}), ("announce number", {"announce_number"}), ("cancel_target", {"cancel_target"}),
           ("reverse_deck", {"reverse_deck"}), ("Announce attrib", {"announce_attribute_multi"}), ("weight not", {"tribute_weight"})]

def feature_match(signature, row):
    features = set(row["features"])
    literal = re.search(r"cannot find id: (\d+)", signature)
    if literal and int(literal.group(1)) in row["system_strings"]: return True
    return any(key in signature and features & wanted for key, wanted in FEATURE)

def main():
    state = json.loads((OUT / "soak/state.json").read_text())
    _, by, _ = load_pool()
    static = json.loads((OUT / "static_features.json").read_text())
    attributed = collections.defaultdict(collections.Counter)
    for failure in state["failures"]:
        for code in failure.get("attributed") or []:
            if code in by: attributed[by[code]["base"]][failure["signature"]] += 1
    confirmed = {}
    for base, sigs in attributed.items():
        for signature, count in sigs.items():
            if feature_match(signature, static[str(base)]) and count >= 2:
                confirmed.setdefault(base, {"signature": signature, "evidence": "attributed+feature", "count": count})
    for key, row in state["confirm"].items():
        base = int(key)
        crashes = sum(row["crash"].values())
        if not crashes: continue
        top = max(row["crash"], key=row["crash"].get)
        total = crashes + row["ok"]
        if feature_match(top, static[key]) or (crashes >= 2 and crashes / total >= 1 / 3):
            confirmed[base] = {"signature": top, "evidence": "dedicated duels", "crashed": crashes, "of": total}
    classes = collections.defaultdict(list)
    for base, row in confirmed.items():
        classes[re.sub(r"\s*exit-6: what\(\):\s*", "", row["signature"])].append(by[base]["name"])
    out = {"cards": {str(k): {"name": by[k]["name"], **v} for k, v in confirmed.items()},
           "classes": {k: sorted(v) for k, v in sorted(classes.items(), key=lambda x: -len(x[1]))}}
    (REP / "confirmed-crash-cards.json").write_text(json.dumps(out, indent=1))
    print(len(confirmed))
    merged = collections.Counter()
    for k, v in out["classes"].items():
        merged[re.sub(r", length \d+, dp 1|\d+ not implemented for announce number", "", k)] += len(v)
        print(len(v), k, v[:60])
    print(merged)
    gauntlet = json.loads((OUT / "gauntlet_cards.json").read_text())
    print("gauntlet confirmed:", [(r["name"], r["deck_names"], r["zones"]) for r in gauntlet if r["base"] in confirmed])

if __name__ == "__main__":
    main()
