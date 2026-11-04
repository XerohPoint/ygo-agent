#!/usr/bin/env python3
"""List every gauntlet card with deck frequency and interface and historical status."""
import json, sys
from collections import Counter, defaultdict
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import *
import build_gauntlet as bg

ERRATA_35 = ["Armory Arm","Black Garden","Brain Control","Brionac, Dragon of the Ice Barrier","Catapult Turtle","Cyber Phoenix",
 "D.D. Survivor","Dark End Dragon","Darkness Approaches","Destiny End Dragoon","Elemental HERO Prisma","Fortune Lady Light","Future Fusion",
 "Goyo Guardian","Jade Knight","Light and Darkness Dragon","Light End Dragon","Lumina, Lightsworn Summoner","Machina Gearframe","Mark of the Rose",
 "Mausoleum of the Emperor","My Body as a Shield","Necrovalley","Quickdraw Synchron","Red-Eyes Darkness Metal Dragon","Rescue Cat",
 "Ryko, Lightsworn Hunter","Sangan","Soul Exchange","Strike Ninja","Susa Soldier","Swap Frog","Treeborn Frog","Ultimate Offering","Urgent Tuning"]

def load_decks():
    manifest = json.loads((bg.DEST / "manifest.json").read_text())
    decks = {}
    for row in manifest["decks"]:
        path = bg.DEST / f"{row['name']}.ydk"
        if path.exists(): decks[row["name"]] = bg.parse_ydk(path.read_text())
    return manifest, decks

def main():
    manifest, decks = load_decks()
    entries, by_id, _ = load_pool()
    db = connect()
    overrides = historical_overrides()
    errata = {n.lower() for n in ERRATA_35}
    static = json.loads((OUT / "static_features.json").read_text())
    use, zones = defaultdict(set), defaultdict(Counter)
    for name, deck in decks.items():
        for zone, codes in deck.items():
            for c in codes:
                use[c].add(name); zones[c][zone] += 1
    rows = []
    for code, who in use.items():
        entry = by_id.get(code)
        row = db.execute("select name from texts where id=?", (code,)).fetchone()
        base = entry["base"] if entry else code
        rows.append({"code": code, "base": base, "name": row[0] if row else "?", "decks": len(who), "deck_names": sorted(who), "zones": dict(zones[code]),
                     "in_pool": bool(entry), "historical_override": base in overrides,
                     "in_errata_35": (row[0].lower() if row else "") in errata,
                     "features": static.get(str(base), {}).get("features", [])})
    rows.sort(key=lambda r: (-r["decks"], r["name"]))
    (OUT / "gauntlet_cards.json").write_text(json.dumps(rows, indent=0))
    for r in rows:
        print(r["decks"], r["name"], r["base"], "H" if r["historical_override"] else "", "E35" if r["in_errata_35"] else "", ",".join(r["features"]), "side-only" if set(r["zones"]) == {"side"} else "")
    print(len(rows))

if __name__ == "__main__":
    main()
