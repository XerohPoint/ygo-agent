#!/usr/bin/env python3
"""Fetch every Edison event and its published top decks from the Format Library JSON API.

Only https://formatlibrary.com/api/ is read. Requests are at least 2 seconds apart and
every response is cached under build/edison-corpus/raw/, so nothing is fetched twice.
Raw responses contain player names, so they stay in that untracked directory.

--seed-cache copies responses the pool audit already fetched (same routes) into the raw
cache first, and records which files came from there, so those are not fetched again.
Events excluded by ruleset are never fetched; the gallery entry is enough to record them.
"""
import argparse, json, shutil, sys, time, urllib.error, urllib.request
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from corpus_common import RAW, excluded_event

API = "https://formatlibrary.com/api"
AGENT = "edison-research-client"
SPACING = 2.0
_last = [0.0]


def log(record):
    with (RAW / "fetch_log.jsonl").open("a") as handle:
        handle.write(json.dumps(record) + "\n")


def get(route, dest):
    """Return the cached response, or fetch it once and cache it unmodified."""
    if dest.exists():
        return json.loads(dest.read_text())
    wait = _last[0] + SPACING - time.time()
    if wait > 0:
        time.sleep(wait)
    request = urllib.request.Request(f"{API}/{route}", headers={"User-Agent": AGENT})
    stamp = datetime.now(timezone.utc).isoformat()
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            raw = response.read().decode()
            status = response.status
    finally:
        _last[0] = time.time()
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(raw)
    log({"route": route, "file": str(dest.relative_to(RAW)), "status": status, "utc": stamp, "bytes": len(raw)})
    return json.loads(raw)


def seed(source):
    """Copy audit-cache responses for the same routes, without overwriting."""
    copied = []
    pairs = [(source / "events_gallery.json", RAW / "events_gallery.json")]
    pairs += [(p, RAW / "events" / p.name) for p in sorted((source / "events").glob("*.json"))]
    pairs += [(p, RAW / "decks" / p.name) for p in sorted((source / "decks").glob("*.json"))]
    for src, dest in pairs:
        if src.exists() and not dest.exists():
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dest)
            copied.append({"file": str(dest.relative_to(RAW)),
                           "fetched_utc": datetime.fromtimestamp(src.stat().st_mtime, timezone.utc).isoformat()})
    previous = json.loads((RAW / "seeded.json").read_text()) if (RAW / "seeded.json").exists() else []
    (RAW / "seeded.json").write_text(json.dumps(previous + copied, indent=0))
    print("seeded", len(copied), "responses from", source, flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seed-cache", type=Path, help="The pool audit's web/formatlibrary cache directory")
    args = parser.parse_args()
    RAW.mkdir(parents=True, exist_ok=True)
    if args.seed_cache:
        seed(args.seed_cache)
    gallery = get("events/gallery/edison", RAW / "events_gallery.json")
    events = sorted(gallery["events"], key=lambda e: (e.get("endDate") or "", e["abbreviation"]))
    failures, decks = [], 0
    for index, meta in enumerate(events):
        abbr = meta["abbreviation"]
        if excluded_event(meta):
            continue
        try:
            event = get(f"events/{abbr}", RAW / "events" / f"{abbr}.json")
        except (urllib.error.URLError, ValueError, TimeoutError) as error:
            failures.append({"kind": "event", "id": abbr, "error": str(error)[:200]})
            continue
        for top in event.get("topDecks") or []:
            try:
                get(f"decks/{top['id']}", RAW / "decks" / f"{top['id']}.json")
                decks += 1
            except (urllib.error.URLError, ValueError, TimeoutError) as error:
                failures.append({"kind": "deck", "id": top["id"], "event": abbr, "error": str(error)[:200]})
        print(f"{index + 1}/{len(events)} {abbr} decks so far {decks} failures {len(failures)}", flush=True)
    (RAW / "fetch_failures.json").write_text(json.dumps(failures, indent=1))
    print("events", len(events), "decks", decks, "failures", len(failures), flush=True)


if __name__ == "__main__":
    main()
