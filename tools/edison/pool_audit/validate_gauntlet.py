#!/usr/bin/env python3
"""Validate each candidate alone through the unmodified production module's init_module.

Runs one subprocess per deck, because init_module raises on the first illegal deck and the
production .so holds static state. The Python mirror explains card-level detail only.
"""
import json, subprocess, sys
from collections import Counter
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import *

DEST = ROOT / "assets/edison/gauntlet_candidates"
CHILD = """
import sys
sys.path.insert(0, %r); sys.path.insert(0, %r)
import ygoenv
from ygoenv.ygopro import init_module
init_module(%r, %r, {"candidate": sys.argv[1]})
print("PRODUCTION_LEGAL")
"""

def main():
    manifest = json.loads((DEST / "manifest.json").read_text())
    entries, by_id, _ = load_pool()
    db = connect()
    results = []
    for deck in manifest["decks"]:
        path = DEST / f"{deck['name']}.ydk"
        if not path.exists():
            results.append({"name": deck["name"], "legal": False, "reason": "no file"})
            continue
        code = CHILD % (str(ROOT), str(ROOT / "ygoenv"), str(DB), str(FULL_CODES))
        proc = subprocess.run([sys.executable, "-c", code, str(path)], cwd=ROOT / "scripts", capture_output=True, text=True)
        ok = "PRODUCTION_LEGAL" in proc.stdout
        reason = "" if ok else [l for l in proc.stderr.splitlines() if l.strip()][-1][:300]
        zones, zone = {"main": [], "extra": [], "side": []}, None
        for line in path.read_text().splitlines():
            if line == "#main": zone = "main"
            elif line == "#extra": zone = "extra"
            elif line == "!side": zone = "side"
            elif line.isdigit(): zones[zone].append(int(line))
        problems = []
        counts = Counter()
        for zname, codes in zones.items():
            for c in codes:
                entry = by_id.get(c)
                if entry is None:
                    row = db.execute("select t.name from texts t where t.id=?", (c,)).fetchone()
                    problems.append(f"{c} {row[0] if row else '?'} not in pool ({zname})")
                else:
                    counts[entry["copy_key"]] += 1
        for key, n in counts.items():
            entry = next(e for e in entries if e["copy_key"] == key)
            if n > entry["limit"]: problems.append(f"{entry['name']} x{n} over limit {entry['limit']}")
        results.append({"name": deck["name"], "legal": ok, "reason": reason, "problems": problems,
                        "main": len(zones["main"]), "extra": len(zones["extra"]), "side": len(zones["side"])})
        print(deck["name"], "LEGAL" if ok else f"ILLEGAL {reason}", len(zones["main"]), len(zones["extra"]), len(zones["side"]), problems)
    (OUT / "gauntlet_validation.json").write_text(json.dumps(results, indent=1))

if __name__ == "__main__":
    main()
