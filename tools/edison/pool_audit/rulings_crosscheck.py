#!/usr/bin/env python3
"""Nominate rulings-versus-script differences for gauntlet cards using coarse text signals.

Compares ruling statements (trigger kind, targeting, timing, once per turn) with script
effect types. A nomination is a lead for reading, not a demonstrated bug.
"""
import json, re, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import *

def main():
    index = json.loads((OUT / "rulings_index.json").read_text())
    cards = json.loads((OUT / "gauntlet_cards.json").read_text())
    rows = []
    for card in cards:
        section = index.get(card["name"], {}).get("section")
        path = SCRIPTS / f"c{card['base']}.lua"
        if not section or not path.exists() or card["in_errata_35"]: continue
        text = path.read_text(errors="replace")
        leads = []
        trig_f, trig_o = "TRIGGER_F" in text, "TRIGGER_O" in text
        says_m = re.search(r"mandatory (\"[^\"]*\" )?Trigger", section)
        says_o = re.search(r"optional (\"[^\"]*\" )?Trigger", section)
        if says_m and not trig_f and trig_o: leads.append("ruling says mandatory trigger; script only TRIGGER_O")
        if says_o and not trig_o and trig_f: leads.append("ruling says optional trigger; script only TRIGGER_F")
        if re.search(r"Ignition Effect", section) and "IGNITION" not in text and "QUICK" in text: leads.append("ruling says Ignition; script Quick")
        if re.search(r"Quick Effect", section) and "QUICK" not in text and "IGNITION" in text and "EFFECT_TYPE_ACTIVATE" not in text: leads.append("ruling says Quick; script Ignition")
        targets = re.search(r"(This|The) ([^.]{0,30})?(effect )?targets", section)
        no_target = re.search(r"does not target|does NOT target|not a targeting", section)
        has_target_flag = "EFFECT_FLAG_CARD_TARGET" in text
        if targets and not no_target and not has_target_flag: leads.append("ruling says targets; script has no CARD_TARGET flag")
        if no_target and not targets and has_target_flag: leads.append("ruling says no target; script has CARD_TARGET flag")
        if re.search(r"cannot miss the timing", section, re.I) and "TRIGGER_O" in text and "EFFECT_FLAG_DELAY" not in text: leads.append("ruling says cannot miss timing; optional trigger without DELAY")
        if re.search(r"can miss the timing", section, re.I) and "EFFECT_FLAG_DELAY" in text: leads.append("ruling says can miss timing; script has DELAY")
        if re.search(r"Once per turn", section) and "SetCountLimit" not in text and "FLIP" not in text: leads.append("ruling says once per turn; no SetCountLimit")
        if not re.search(r"Once per turn|once per turn", section) and "SetCountLimit" in text: leads.append("script has SetCountLimit; ruling text does not mention once per turn")
        if re.search(r"SetCountLimit\(\s*\d+\s*,\s*(\d{5,}|c\d+|id)", text): leads.append("script has hard once-per-turn")
        if leads:
            rows.append({"name": card["name"], "code": card["base"], "decks": card["decks"], "leads": leads, "url": index[card["name"]]["url"]})
    (OUT / "rulings_leads.json").write_text(json.dumps(rows, indent=1))
    for r in rows:
        print(r["decks"], r["name"], r["code"], r["leads"])
    print(len(rows))

if __name__ == "__main__":
    main()
