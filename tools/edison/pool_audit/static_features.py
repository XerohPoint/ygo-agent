#!/usr/bin/env python3
"""Flag pool scripts that use features the production env interface does not handle.

Patterns are derived from reading ygopro.h. These are predictions: a pattern means the
script can reach the feature, and soak results confirm or fail to confirm each card.
"""
import json, re, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import *

SRC = (ROOT / "ygoenv/ygoenv/ygopro/ygopro.h").read_text()
SYSTEM_TABLE = SRC[SRC.index("system_strings = {"):SRC.index("};", SRC.index("system_strings = {"))]
SYSTEM = {int(x) for x in re.findall(r"\{(\d+),\s*\"", SYSTEM_TABLE)}
ACTIVATED = re.compile(r"EFFECT_TYPE_(ACTIVATE|IGNITION|QUICK_O|QUICK_F|TRIGGER_O|TRIGGER_F|FLIP)")

CHECKS = {
    "toss_coin": re.compile(r"Duel\.TossCoin|EFFECT_TOSS_COIN"),
    "toss_dice": re.compile(r"Duel\.TossDice|EFFECT_TOSS_DICE"),
    "announce_race": re.compile(r"Duel\.AnnounceRace"),
    "announce_card": re.compile(r"Duel\.AnnounceCard"),
    "announce_type": re.compile(r"Duel\.AnnounceType"),
    "announce_coin": re.compile(r"Duel\.AnnounceCoin"),
    "spirit_return": re.compile(r"EnableSpiritReturn|SpiritReturnReg"),
    "rps": re.compile(r"Duel\.RockPaperScissors"),
    "field_disabled": re.compile(r"EFFECT_DISABLE_FIELD|SelectDisableField"),
    "reverse_deck": re.compile(r"EFFECT_REVERSE_DECK"),
    "deck_top": re.compile(r"Duel\.ConfirmDecktop|EFFECT_REVERSE_DECK"),
    "cancel_target": re.compile(r"CancelCardTarget|Duel\.ChangeTargetCard"),
    "sort_deck": re.compile(r"SortDecktop|SortDeckbottom"),
    "announce_number": re.compile(r"Duel\.AnnounceNumber|AnnounceLevel"),
    "announce_attribute_multi": re.compile(r"Duel\.AnnounceAttribute\([^,)]+,\s*([2-9]|ct|\w*[cC]ount)"),
    "tribute_weight": re.compile(r"EFFECT_TRIPLE_TRIBUTE|EFFECT_DOUBLE_TRIBUTE"),
}

def system_string_literals(text):
    found = set()
    for pat in (r"SelectOption\(([^)\n]*)\)", r"SelectYesNo\(([^)\n]*)\)", r"SetDescription\(([^)\n]*)\)",
                r"SelectEffectYesNo\(([^)\n]*)\)"):
        for args in re.findall(pat, text):
            parts = [p.strip() for p in args.split(",")]
            for part in parts:
                if re.fullmatch(r"\d+", part) and 60 <= int(part) < 2000:
                    if int(part) not in SYSTEM: found.add(int(part))
    return sorted(found)

def features():
    entries, by_id, _ = load_pool()
    table = {}
    for entry in entries:
        path = SCRIPTS / f"c{entry['base']}.lua"
        row = {"features": [], "activated_effect": False, "stub": False, "system_strings": []}
        if path.exists():
            text = path.read_text(errors="replace")
            for key, regex in CHECKS.items():
                if regex.search(text): row["features"].append(key)
            row["system_strings"] = system_string_literals(text)
            row["activated_effect"] = bool(ACTIVATED.search(text))
            row["stub"] = "CreateEffect" not in text and "aux." not in text and "Auxiliary." not in text
        table[entry["base"]] = row
    return table, entries

if __name__ == "__main__":
    table, entries = features()
    (OUT / "static_features.json").write_text(json.dumps({str(k): v for k, v in table.items()}))
    from collections import Counter
    counts = Counter(f for v in table.values() for f in v["features"])
    print(counts)
    print("system strings", Counter(s for v in table.values() for s in v["system_strings"]).most_common(30))
    print("stubs", [e["name"] for e in entries if table[e["base"]]["stub"] and e["rows"][0]["type"] & TYPE_NORMAL == 0][:40])
