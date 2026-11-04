#!/usr/bin/env python3
"""Rewrite the certification lock entries for files this workstream changed on purpose.

Run after changing the legality manifest, its generated header, or the hardening patch. It records
current hashes; review the diff, because run.py's hash checks are only as good as this step.
"""
import hashlib, json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
LEGALITY = "assets/edison/legality.json"
HEADER = "ygoenv/ygoenv/ygopro/edison_legality_data.h"

def digest(relative):
    return hashlib.sha256((ROOT / relative).read_bytes()).hexdigest()

def rewrite(path, change):
    data = json.loads(path.read_text())
    change(data)
    path.write_text(json.dumps(data, indent=2) + "\n")

def main():
    def certification(data):
        data["legality_sha256"][LEGALITY] = digest(LEGALITY)
        data["legality_sha256"][HEADER] = digest(HEADER)
        data["core_hardening_patch_sha256"] = digest("repo/packages/y/ygopro-core/edison-hardening.patch")
    rewrite(ROOT / "tests/edison/assets.lock.json", certification)
    for name in ("agent_assets.lock.json", "smoke_assets.lock.json"):
        def other(data):
            for key in (LEGALITY, HEADER):
                if key in data:
                    data[key] = digest(key)
        rewrite(ROOT / "tests/edison" / name, other)
    print("locks updated")

if __name__ == "__main__":
    main()
