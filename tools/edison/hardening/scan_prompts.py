#!/usr/bin/env python3
"""List what the legal pool's scripts can ask the environment for, without playing a duel.

Prints the system-string ids that scripts and the core can use as a prompt description, and
which pool cards reach the prompt classes the environment handled poorly. Reads only.
"""
import json, re, sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "pool_audit"))
from common import SCRIPTS, load_pool, script_code

PATTERNS = {
    "SetDescription": r"SetDescription\(\s*(\d+)\s*\)",
    "SelectYesNo": r"SelectYesNo\([^,]+,\s*(\d+)\s*\)",
    "SelectEffectYesNo": r"SelectEffectYesNo\([^,]+,[^,]+,\s*(\d+)\s*\)",
    "SelectOption": None,
}
APIS = ("TossCoin", "TossDice", "AnnounceCoin", "AnnounceCard", "AnnounceRace", "AnnounceAttribute", "AnnounceType",
        "AnnounceNumber", "AnnounceLevel", "SelectDisableField", "RockPaperScissors", "SwapDeckAndGrave", "ReverseDeck",
        "SelectCounter", "RemoveOverlayCard", "SortDecktop", "ConfirmExtratop", "SelectReleaseGroup", "SelectFusionMaterial")

def main():
    entries, _, _ = load_pool()
    ids, api_users = defaultdict(set), defaultdict(set)
    texts = {}
    for entry in entries:
        path = SCRIPTS / f"c{entry['base']}.lua"
        if path.exists():
            texts[entry["name"]] = path.read_text(errors="replace")
    for name in ("utility.lua", "procedure.lua"):
        texts[name] = (SCRIPTS / name).read_text(errors="replace")
    for name, text in texts.items():
        for label, pattern in PATTERNS.items():
            if pattern:
                for match in re.findall(pattern, text):
                    ids[int(match)].add(name)
        for call in re.findall(r"SelectOption\(([^)]*)\)", text):
            for number in re.findall(r"(?:^|,)\s*(\d+)\s*(?=,|$)", call):
                ids[int(number)].add(name)
        for api in APIS:
            if re.search(rf"Duel\.{api}\b", text):
                api_users[api].add(name)
    print("system description ids used by pool scripts:")
    for key in sorted(ids):
        print(f"  {key}: {len(ids[key])} {sorted(ids[key])[:4]}")
    print(json.dumps({k: {"count": len(v), "cards": sorted(v)[:12]} for k, v in sorted(api_users.items())}, indent=0))

if __name__ == "__main__":
    main()
