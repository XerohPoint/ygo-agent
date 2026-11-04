#!/usr/bin/env python3
"""Per-deck gauntlet evidence: source, legality, coverage, dependency and crash results."""
import csv, gzip, json, re, sys
from collections import Counter
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import *
import build_gauntlet as bg

REPORT = ROOT / "tests/edison/reports/pool-audit-2026-11-04"
RISK = {25862681: "Ancient Fairy Dragon", 16226786: "Night Assailant", 5556499: "Machina Fortress"}

def main():
    manifest = json.loads((bg.DEST / "manifest.json").read_text())
    validation = {r["name"]: r for r in json.loads((OUT / "gauntlet_validation.json").read_text())}
    share = {a["archetype"]: a["appearance_share"] for a in json.loads((ROOT / "tests/edison/reports/metagame-replays-2026-11-04/summary.json").read_text())["archetypes"]}
    with gzip.open(REPORT / "per-card.csv.gz", "rt") as handle:
        table = {int(r["code"]): r for r in csv.DictReader(handle)}
    _, by_id, _ = load_pool()
    state = json.loads((OUT / "soak/state.json").read_text())
    confirmed = json.loads((REPORT / "confirmed-crash-cards.json").read_text())["cards"]
    rows = []
    for deck in manifest["decks"]:
        zones = bg.parse_ydk((bg.DEST / f"{deck['name']}.ydk").read_text())
        codes = {c: z for z, cs in zones.items() for c in cs}
        info = {"name": deck["name"], "archetype": deck["archetype"], "share": share.get(deck["archetype"], 0),
                "url": deck["source_url"], "event": deck["event"], "date": deck["event_date"], "placement": deck["placement"],
                "size": deck["event_size"], "community": deck["community"], "ruleset": deck["ruleset"],
                "legal": validation[deck["name"]]["legal"], "reason": validation[deck["name"]]["reason"],
                "sizes": deck["sizes"], "off_manifest": [], "e35": [], "e35_no_override": [], "other_risk": [],
                "interface": [], "weak": []}
        seen = set()
        exercised = total = 0
        for code, zone in codes.items():
            entry = by_id.get(code)
            if entry is None:
                info["off_manifest"].append(code); continue
            base = entry["base"]
            if base in seen: continue
            seen.add(base)
            row = table[base]
            total += 1
            exercised += row["status"] in ("activated", "summoned")
            if row["status"] not in ("activated", "summoned"): info["weak"].append(f"{row['name']} ({row['status']})")
            if row["in_errata_35"] == "1":
                info["e35"].append(row["name"])
                if row["historical_override"] == "0": info["e35_no_override"].append(row["name"])
            if base in RISK: info["other_risk"].append(RISK[base])
            if str(base) in confirmed:
                info["interface"].append(f"{row['name']} [{zone}]")
        info["coverage"] = [exercised, total]
        for variant in ("main", "sidein"):
            result = state.get("gauntlet", {}).get(variant, {}).get(deck["name"], {})
            info[variant] = {"ok": result.get("ok", 0), "crash": sum(result.get("crash", {}).values()),
                             "other": result.get("other", {}), "signatures": result.get("crash", {})}
        rows.append(info)
    legal = {r["archetype"] for r in rows if r["legal"]}
    covered = sum(share.get(a, 0) for a in {r["archetype"] for r in rows})
    out = {"decks": rows, "replay_share_all_archetypes": round(covered, 4),
           "replay_share_legal_lists_only": round(sum(share.get(a, 0) for a in legal), 4)}
    (REPORT / "gauntlet.json").write_text(json.dumps(out, indent=1))
    print("share", out["replay_share_all_archetypes"], out["replay_share_legal_lists_only"])
    for r in rows:
        print(f"{r['name']:18} {r['share']*100:5.2f}% {r['event']:8} {r['date']} #{r['placement']}/{r['size']} legal={r['legal']} cov={r['coverage']} main ok/crash={r['main']['ok']}/{r['main']['crash']} sidein={r['sidein']['ok']}/{r['sidein']['crash']}")
        print("   e35 no override:", r["e35_no_override"], "| interface:", r["interface"], "| weak:", r["weak"][:6])

if __name__ == "__main__":
    main()
