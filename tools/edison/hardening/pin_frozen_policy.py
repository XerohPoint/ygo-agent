#!/usr/bin/env python3
"""Regenerate the seeded baseline that test 082D compares frozen policies against.

082D exists to catch an unintended change in how a frozen policy plays. A rule fix that offers a
new legal action changes those games on purpose, so its baseline must be replaced by a deliberate
step, with first_divergence.py showing that the first difference in each game is that rule change.
"""
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tests/edison"))
from agent_match import run_worker
from production_context_tests import FROZEN_BASELINE, FROZEN_MODEL
import tempfile

def main():
    report = {"purpose": "Seeded frozen-policy games for test 082D, regenerated after the rule fixes in docs/edison-hardening.md. "
                         "Replaces the comparison against tests/edison/reports/agents-2026-11-02.",
              "seed": 20110424, "deck0": "blackwing", "deck1": "frog_monarch", "policies": {}}
    with tempfile.TemporaryDirectory(prefix="edison-pin-") as tmp:
        for policy in ("heuristic", "learned"):
            result = run_worker(20110424, 4, 1, "blackwing", "frog_monarch", Path(tmp) / policy, policy0=policy, policy1="random",
                                model=FROZEN_MODEL if policy == "learned" else None)
            report["policies"][policy] = [{key: duel[key] for key in ("seed", "steps", "winner", "win_reason", "interactions")}
                                          for duel in result["duels"]]
    FROZEN_BASELINE.parent.mkdir(parents=True, exist_ok=True)
    FROZEN_BASELINE.write_text(json.dumps(report, indent=1, sort_keys=True) + "\n")
    print(FROZEN_BASELINE)

if __name__ == "__main__":
    main()
