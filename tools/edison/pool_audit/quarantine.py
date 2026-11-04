#!/usr/bin/env python3
"""Write the quarantine list: cards with a predicted or attributed interface crash.

A card here is not excluded from totals. Quarantine only keeps it out of later coverage
decks so other cards get to play. The confirm stage still runs every quarantined card.
"""
import json, re, sys
from collections import Counter, defaultdict
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import *
import static_features

CRASH_FEATURES = {"toss_coin", "toss_dice", "announce_race", "announce_card", "announce_type", "announce_coin",
                  "field_disabled", "reverse_deck", "announce_attribute_multi", "rps", "spirit_return"}

def main():
    table, entries = static_features.features()
    _, by_id, _ = load_pool()
    state = json.loads((OUT / "soak/state.json").read_text())
    static = {base for base, row in table.items()
              if set(row["features"]) & CRASH_FEATURES or row["system_strings"]}
    number_cards = set()
    for base, row in table.items():
        if "announce_number" in row["features"]:
            text = (SCRIPTS / f"c{base}.lua").read_text(errors="replace")
            if re.search(r"AnnounceNumber\([^\n]*\b(1[3-9]|[2-9]\d|\d{3,})\b", text) or re.search(r"AnnounceNumber\(tp,\s*table", text):
                number_cards.add(base)
    attributed = defaultdict(Counter)
    for failure in state["failures"]:
        signature = failure.get("signature") or failure.get("kind", "")
        for code in failure.get("attributed") or []:
            if code in by_id:
                attributed[signature][by_id[code]["base"]] += 1
    runtime = set()
    for signature, cards in attributed.items():
        total = sum(cards.values())
        for base, count in cards.items():
            if count >= 3 and count >= 0.04 * total:
                runtime.add(base)
    all_q = sorted(static | number_cards | runtime)
    (OUT / "quarantine.json").write_text(json.dumps(all_q))
    print(f"static {len(static)} number {len(number_cards)} runtime {len(runtime)} total {len(all_q)}")

if __name__ == "__main__":
    main()
