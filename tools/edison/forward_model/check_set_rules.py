#!/usr/bin/env python3
"""Hold the facts the deal's Set rules rest on, for the lists in tools/edison/learner/decks.py.

The deal's rules for face-down cards are simple because these lists are simple: nothing changes a card's Level in
hand, nothing lowers a Tribute count, one card has a Set procedure, there are no Field Spells or Ritual Monsters, and
the only effects that place a monster face-down reveal it or act on a face-up card. If a list gains a card that
breaks one of these, the rules in fm_deal.h may reject true worlds. This exits nonzero until they are revisited.
"""
import json
import re
import sqlite3
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tools/edison/learner"))
import decks  # noqa: E402

SCRIPTS = Path("/home/mada/ygopro-scripts")
# The script text that matters, and the cards allowed to have it.
ALLOWED = {
    "EFFECT_SET_PROC": {75498415},           # Blackwing - Sirocco the Dawn: note_public tracks its condition
    "EFFECT_LIMIT_SET_PROC": set(),
    "EFFECT_DECREASE_TRIBUTE_SET": set(),
    "EFFECT_DECREASE_TRIBUTE": set(),
    "EFFECT_DOUBLE_TRIBUTE": set(),
    "EFFECT_TRIPLE_TRIBUTE": set(),
    "EFFECT_EXTRA_SET_COUNT": set(),
    "EFFECT_UPDATE_LEVEL": set(),            # Level in hand. Two below change it on the field, which is public.
    "EFFECT_CHANGE_LEVEL": {98777036, 66457407},  # Tragoedia, Copy Plant: field only
    "EFFECT_MONSTER_SSET": set(),
    "EFFECT_CANNOT_SSET": set(),
    "EFFECT_CANNOT_MSET": {52105192, 44887817},   # Hidden Armory, Miracle Fertilizer: "cannot this turn", a public fact
    # Anything placing a card face-down. Book of Moon and Flip Flop Frog turn a face-up card, which stays tracked.
    # Mega Hamster reveals what it Summons. Ultimate Offering is an ordinary Normal Set.
    "POS_FACEDOWN": {14087893, 5220687, 81278754, 80604091},
    "Duel.MSet": {80604091},
    "Duel.SSet": {60082869},                 # Dust Tornado: a Spell or Trap from the hand
}
TYPE_FIELD, TYPE_RITUAL, TYPE_MONSTER = 0x80000, 0x80, 0x1


def main():
    db = sqlite3.connect(decks.DB)
    found, unexpected, codes = {}, {}, set()
    for path in decks.registry().values():
        zones = decks.parse_ydk(Path(path).read_text())
        codes.update(zones["main"], zones["extra"])
    field_or_ritual = []
    for code in sorted(codes):
        alias, kind = db.execute("select alias,type from datas where id=?", (code,)).fetchone()
        if kind & TYPE_FIELD or (kind & TYPE_RITUAL and kind & TYPE_MONSTER):
            field_or_ritual.append(code)
        script = SCRIPTS / f"c{code}.lua"
        if not script.exists() and alias:
            script = SCRIPTS / f"c{alias}.lua"
        text = script.read_text(errors="replace") if script.exists() else ""
        for key, allowed in ALLOWED.items():
            if re.search(re.escape(key), text):
                found.setdefault(key, []).append(code)
                if code not in allowed:
                    unexpected.setdefault(key, []).append(code)
    report = {"set_rule_facts": True, "codes": len(codes), "found": found, "unexpected": unexpected, "field_or_ritual": field_or_ritual}
    print(json.dumps(report))
    return 1 if unexpected or field_or_ritual else 0


if __name__ == "__main__":
    sys.exit(main())
