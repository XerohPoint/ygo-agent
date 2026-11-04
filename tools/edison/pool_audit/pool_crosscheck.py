#!/usr/bin/env python3
"""Compare legality.json to a dated TCG release listing (one YGOPRODeck bulk request).

Reports possible false exclusions and false inclusions. Does not change the manifest.
"""
import json, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import *

def main():
    data = json.loads((OUT / "web/ygoprodeck/tcg_until_20100424.json").read_text())["data"]
    entries, by_id, manifest = load_pool()
    db = connect()
    known = {r[0] for r in db.execute("select id from datas")}
    rows = []
    for card in data:
        ids = [img["id"] for img in card.get("card_images", [])] + [card["id"]]
        date = card["misc_info"][0].get("tcg_date")
        formats = card["misc_info"][0].get("formats", [])
        if not any(i in by_id for i in ids) and "Token" not in card["type"]:
            rows.append({"id": card["id"], "name": card["name"], "tcg_date": date, "type": card["type"],
                         "in_db": any(i in known for i in ids), "formats": formats,
                         "sets": sorted({s["set_code"].split("-")[0] for s in card.get("card_sets", [])})[:6]})
    rows.sort(key=lambda r: r["tcg_date"] or "")
    listed = {i for card in data for i in [img["id"] for img in card.get("card_images", [])] + [card["id"]]}
    extra = [{"code": e["base"], "name": e["name"]} for e in entries if not any(i in listed for i in e["ids"])]
    out = {"tcg_listing_cards": len(data), "manifest_entries": len(entries),
           "released_not_in_manifest": rows, "in_manifest_not_in_listing": extra}
    (OUT / "pool_crosscheck.json").write_text(json.dumps(out, indent=1))
    print("listing", len(data), "released-not-in-manifest", len(rows), "manifest-not-in-listing", len(extra))
    for r in rows[:60]: print(r["id"], r["name"], r["tcg_date"], r["in_db"], r["sets"][:3])
    print("sample manifest-only", extra[:40])

if __name__ == "__main__":
    main()
