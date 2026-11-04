#!/usr/bin/env python3
"""Fetch Edison event results and top decks from the Format Library public JSON API.

Pages at formatlibrary.com render with JavaScript. The API routes below were read from
the site's main.js bundle. Responses are stored unmodified and treated only as data.
"""
import argparse, json, time, urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "build/edison-pool-audit-2026-11-04/web/formatlibrary"
API = "https://formatlibrary.com/api"

def get(path, dest):
    if dest.exists():
        return json.loads(dest.read_text())
    req = urllib.request.Request(f"{API}/{path}", headers={"User-Agent": "edison-pool-audit/1.0"})
    with urllib.request.urlopen(req, timeout=40) as resp:
        raw = resp.read().decode()
    dest.write_text(raw)
    time.sleep(0.4)
    return json.loads(raw)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--since", default="2025-11-01")
    parser.add_argument("--min-size", type=int, default=40)
    args = parser.parse_args()
    (OUT / "events").mkdir(parents=True, exist_ok=True)
    (OUT / "decks").mkdir(parents=True, exist_ok=True)
    gallery = get("events/gallery/edison", OUT / "events_gallery.json")
    events = [e for e in gallery["events"] if (e.get("endDate") or "") >= args.since
              and (e.get("size") or 0) >= args.min_size]
    failures = []
    rows = []
    for meta in sorted(events, key=lambda e: e["endDate"], reverse=True):
        abbr = meta["abbreviation"]
        try:
            event = get(f"events/{abbr}", OUT / "events" / f"{abbr}.json")
        except Exception as exc:
            failures.append(["event", abbr, str(exc)])
            continue
        for top in event.get("topDecks") or []:
            rows.append({"event": abbr, "event_name": meta["name"], "date": meta["endDate"][:10],
                         "size": meta["size"], "reference": meta.get("referenceUrl"), **top})
            try:
                get(f"decks/{top['id']}", OUT / "decks" / f"{top['id']}.json")
            except Exception as exc:
                failures.append(["deck", top["id"], str(exc)])
    (OUT / "top_decks.json").write_text(json.dumps(rows, indent=1))
    (OUT / "failures.json").write_text(json.dumps(failures, indent=1))
    print(len(events), "events", len(rows), "top decks", len(failures), "failures")

if __name__ == "__main__":
    main()
