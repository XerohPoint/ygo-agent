#!/usr/bin/env python3
"""Convert selected Format Library decks to .ydk with provenance, and write a manifest.

Card lists come verbatim from each deck's "ydk" field. Nothing is edited to pass legality:
a list that fails stays in the manifest with its failure, and its file is still written.
"""
import json, re, sys, time, urllib.request
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import *

FL = OUT / "web/formatlibrary"
DEST = ROOT / "assets/edison/gauntlet_candidates"
SELECTED = [
    ("blackwing", "Blackwing", 78809), ("vayu_turbo", "Vayu Turbo", 84297), ("hero_frog", "Hero Frog", 73078),
    ("gladiator_beast", "Gladiator Beast", 68372), ("frog_monarch", "Frog Monarch", 78922),
    ("dragon_turbo", "Dragon Turbo", 85607), ("lightsworn", "Lightsworn", 85060), ("frog_otk", "Frog OTK", 84951),
    ("diva_hero_beat", "Diva Hero Beat", 73097), ("gigavise", "Gigavise", 84319),
    ("machina_control", "Machina Control", 78816), ("diva_hero", "Diva Hero", 85032),
    ("zombie_teledad", "Zombie TeleDAD", 75594), ("diva_frog", "Diva Frog", 75612),
    ("hero_beat", "Hero Beat", 28337),
    ("lightsworn_fairy", "Lightsworn Fairy", 85583), ("quickdraw_plant", "Quickdraw Plant", 2981),
    ("vayu_turbo_ddev08", "Vayu Turbo", 60919),
]
RULESET = {"Konami": "2010 official OP/SJC, rules as judged in April 2010",
           None: "Community traditional Edison; not Konami Time Wizard modern mechanics"}

def fetch(deck_id):
    dest = FL / "decks" / f"{deck_id}.json"
    if not dest.exists():
        req = urllib.request.Request(f"https://formatlibrary.com/api/decks/{deck_id}", headers={"User-Agent": "edison-pool-audit/1.0"})
        with urllib.request.urlopen(req, timeout=40) as resp:
            dest.write_text(resp.read().decode())
        time.sleep(1.0)
    return json.loads(dest.read_text())

def parse_ydk(text):
    zones, zone = {"main": [], "extra": [], "side": []}, None
    for raw in text.splitlines():
        line = raw.strip()
        if line == "#main": zone = "main"
        elif line == "#extra": zone = "extra"
        elif line == "!side": zone = "side"
        elif re.fullmatch(r"\d+", line) and zone: zones[zone].append(int(line))
    return zones

def main():
    DEST.mkdir(parents=True, exist_ok=True)
    gallery = {e["abbreviation"]: e for e in json.loads((FL / "events_gallery.json").read_text())["events"]}
    manifest = []
    for name, archetype, deck_id in SELECTED:
        if not deck_id:
            manifest.append({"name": name, "archetype": archetype, "status": "no list selected"})
            continue
        deck = fetch(deck_id)
        zones = parse_ydk(deck["ydk"])
        event = gallery.get(deck.get("eventAbbreviation"), {})
        community = event.get("communityName")
        info = {"name": name, "archetype": archetype, "type_on_source": deck["deckTypeName"], "deck_id": deck_id,
                "source_url": f"https://formatlibrary.com/decks/{deck_id}",
                "api_url": f"https://formatlibrary.com/api/decks/{deck_id}",
                "event": deck.get("eventAbbreviation"), "event_name": event.get("name"),
                "event_url": f"https://formatlibrary.com/events/{deck.get('eventAbbreviation')}",
                "bracket_url": event.get("referenceUrl"),
                "event_date": (event.get("endDate") or deck.get("publishDate") or "")[:10],
                "event_size": event.get("size"), "community": community,
                "placement": deck.get("placement"),
                "ruleset": RULESET.get(community, RULESET[None]),
                "sizes": {k: len(v) for k, v in zones.items()}}
        lines = [f"#source {info['source_url']}", f"#event {info['event']} {info['event_date']} placement {info['placement']} of {info['event_size']}",
                 f"#archetype {archetype}", f"#ruleset {info['ruleset']}", "#main", *map(str, zones["main"]),
                 "#extra", *map(str, zones["extra"]), "!side", *map(str, zones["side"])]
        (DEST / f"{name}.ydk").write_text("\n".join(lines) + "\n")
        manifest.append(info)
    (DEST / "manifest.json").write_text(json.dumps({
        "purpose": "Sourced candidate gauntlet; published tournament placements, not a strength ranking.",
        "source": "https://formatlibrary.com/api/ (read from the deck-gallery site bundle)", "decks": manifest}, indent=1))
    for row in manifest:
        print(row["name"], row.get("sizes"), row.get("event"), row.get("placement"), row.get("event_size"))

if __name__ == "__main__":
    main()
