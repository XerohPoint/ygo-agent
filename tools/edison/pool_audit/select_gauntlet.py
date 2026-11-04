#!/usr/bin/env python3
"""Rank Format Library top decks per archetype. Fetches only event summaries, at a low rate."""
import json, sys, time, urllib.request
from collections import defaultdict
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import *

FL = OUT / "web/formatlibrary"
EVENTS = ["EWCQ14", "BEAK12", "IMNCS05", "EWCQ13", "BEAK11", "EWCQ12", "EWC2025", "DDEV09"]

def fetch_event(abbr):
    dest = FL / "events" / f"{abbr}.json"
    if not dest.exists():
        req = urllib.request.Request(f"https://formatlibrary.com/api/events/{abbr}", headers={"User-Agent": "edison-pool-audit/1.0"})
        with urllib.request.urlopen(req, timeout=40) as resp:
            dest.write_text(resp.read().decode())
        time.sleep(1.0)
    return json.loads(dest.read_text())

def main():
    gallery = json.loads((FL / "events_gallery.json").read_text())
    meta = {e["abbreviation"]: e for e in gallery["events"]}
    print("event names mentioning Time Wizard / Konami:",
          [e["abbreviation"] for e in gallery["events"] if "time wizard" in e["name"].lower() or "konami" in e["name"].lower()])
    print("communities:", sorted({e["communityName"] or "" for e in gallery["events"]}))
    rows = defaultdict(list)
    for abbr in EVENTS:
        event = fetch_event(abbr)
        for top in event["topDecks"]:
            rows[top["deckTypeName"]].append({"id": top["id"], "event": abbr, "placement": top["placement"],
                "builder": top["builderName"], "size": meta[abbr]["size"], "date": meta[abbr]["endDate"][:10]})
    for arch in sorted(rows, key=lambda a: -len(rows[a])):
        ordered = sorted(rows[arch], key=lambda r: (r["placement"], -r["size"], r["date"]))
        print(arch, len(rows[arch]), [(r["id"], r["event"], r["placement"]) for r in ordered[:4]])
    (FL / "top_deck_index.json").write_text(json.dumps(rows, indent=0))

if __name__ == "__main__":
    main()
