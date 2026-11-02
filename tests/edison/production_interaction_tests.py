#!/usr/bin/env python3
"""Production regression for the Dupe Frog preload failure and worker scheduling."""
import argparse
import tempfile
from pathlib import Path
from smoke import run_worker

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fail-fast", action="store_true")
    args = parser.parse_args()
    passed = failed = 0
    cases = [
        ("A", "DUPE FROG RESOURCE DEPENDENCY", 1),
        ("B", "PARALLEL SEEDED PRODUCTION", 4),
    ]
    with tempfile.TemporaryDirectory(prefix="edison-production-") as tmp:
        root = Path(tmp)
        for label, description, count in cases:
            print(f"\nEDISON RULE TEST 077{label} - {description}")
            print("Expected: complete natural duels, Dupe Frog summon, deterministic thread-count control.")
            try:
                one = run_worker(20100424, count, 1, "blackwing", "frog_monarch", root/label/"one")
                many = run_worker(20100424, count, count, "blackwing", "frog_monarch", root/label/"many")
                assert one["duels"] == many["duels"], "Scheduling changed seeded decisions/results"
                assert len(one["duels"]) == count
                assert one["duels"][0]["interactions"].get("46239604:1:4", 0) > 0, "Dupe summon not reached"
                assert one["duels"][0]["steps"] > 99, "Original missing-metadata checkpoint not passed"
                print(f"Observed: {count} complete duel(s); Dupe summon passed; identical full decision hashes.")
                print("RESULT: PASS")
                passed += 1
            except (AssertionError, RuntimeError, OSError) as error:
                print(f"RESULT: FAIL — {error}")
                failed += 1
                if args.fail_fast:
                    break
    print(f"\nSUMMARY: {passed} PASS, {failed} FAIL")
    return int(failed != 0)

if __name__ == "__main__":
    raise SystemExit(main())
