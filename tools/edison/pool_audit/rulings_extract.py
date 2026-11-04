#!/usr/bin/env python3
"""Index edisonformat.com rulings text by ALL-CAPS card heading for gauntlet cards."""
import json, re, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import *

PAGES = {"individual-rulings-a-c": "A-C", "individual-rulings-d-e": "D-E", "individual-rulings-f-h": "F-H",
         "individual-rulings-i-k": "I-K", "individual-rulings-l-o": "L-O", "individual-card-rulings-p-r": "P-R",
         "individual-card-rulings-s-t": "S-T", "rules-u-z": "U-Z"}

def main():
    cards = json.loads((OUT / "gauntlet_cards.json").read_text())
    text = {p: (OUT / f"web/rulings/{p}.txt").read_text() for p in PAGES}
    index = {}
    for card in cards:
        name = card["name"]
        upper = name.upper()
        for page, body in text.items():
            pos = body.find(upper)
            if pos < 0: continue
            # A section ends at the next line beginning with a run of capitals that is a new heading.
            match = re.search(r"\n\s*[A-Z][A-Z0-9 ,.'\"!&/:-]{6,}\s", body[pos + len(upper):])
            end = pos + len(upper) + (match.start() if match else 3000)
            index[name] = {"page": page, "url": f"https://www.edisonformat.com/rulings/{page}", "section": body[pos:end].strip()}
            break
    (OUT / "rulings_index.json").write_text(json.dumps(index, indent=0))
    print(len(cards), "cards;", len(index), "with a section;", "absent:", [c["name"] for c in cards if c["name"] not in index][:40])
    keywords = re.compile(r"errata|erratum|old(er)? text|original text|printed|reprint|wording|text of|new text|once per turn|OCG|re-?ruled", re.I)
    for name, row in index.items():
        hits = [s.strip() for s in re.split(r"(?<=[.\]])\s+", row["section"]) if keywords.search(s)]
        if hits:
            print("==", name, row["page"], len(row["section"]))
            for h in hits[:4]: print("   ", h[:210])

if __name__ == "__main__":
    main()
