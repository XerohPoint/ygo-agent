#!/usr/bin/env python3
"""Find pool and gauntlet scripts with evidence of post-2010 functional change.

Signals: (a) scripts-repo commits with 'errata' in the message touching the file, (b) a
hard once-per-turn SetCountLimit(n,code) in the script, (c) "You can only" in cards.cdb
text. These only nominate cards; each nominated gauntlet card still needs reading.
"""
import json, re, subprocess, sys
from collections import defaultdict
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import *

def main():
    entries, by_id, _ = load_pool()
    gauntlet = {r["base"]: r for r in json.loads((OUT / "gauntlet_cards.json").read_text())}
    log = subprocess.run(["git", "-C", str(SCRIPTS), "log", "--format=@%h|%ad|%s", "--date=short", "-i",
                          "--grep=errata", "--grep=erratum", "--name-only"], capture_output=True, text=True).stdout
    commits = defaultdict(list)
    current = None
    for line in log.splitlines():
        if line.startswith("@"): current = line[1:].split("|", 2)
        elif line.startswith("c") and line.endswith(".lua"):
            m = re.match(r"c(\d+)\.lua", line)
            if m: commits[int(m.group(1))].append(current)
    rows = []
    for e in entries:
        path = SCRIPTS / f"c{e['base']}.lua"
        text = path.read_text(errors="replace") if path.exists() else ""
        hard = bool(re.search(r"SetCountLimit\(\s*\d+\s*,\s*(\d{5,}|c\d+|id|EFFECT_COUNT_CODE)", text))
        desc = e["rows"][0]["desc"] or ""
        only = bool(re.search(r"You can only|can only (use|activate)", desc))
        c = commits.get(e["base"], [])
        if hard or only or c:
            rows.append({"code": e["base"], "name": e["name"], "hard_opt_script": hard, "only_text": only,
                         "errata_commits": c, "gauntlet_decks": gauntlet.get(e["base"], {}).get("decks", 0)})
    (OUT / "errata_signals.json").write_text(json.dumps(rows, indent=0))
    print("pool signals", len(rows), "hard", sum(r["hard_opt_script"] for r in rows), "only", sum(r["only_text"] for r in rows), "commit", sum(bool(r["errata_commits"]) for r in rows))
    for r in sorted(rows, key=lambda r: -r["gauntlet_decks"]):
        if r["gauntlet_decks"]:
            print(r["gauntlet_decks"], r["code"], r["name"], "HARD" if r["hard_opt_script"] else "", "ONLY" if r["only_text"] else "", [(x[0], x[1], x[2][:40]) for x in r["errata_commits"]])

if __name__ == "__main__":
    main()
