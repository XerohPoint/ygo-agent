#!/usr/bin/env python3
"""Evaluate, score and final-test the retraining study on the fixed Deck labels, with the long run's own tools."""
import argparse
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import final_test  # noqa: E402
import gauntlet_eval  # noqa: E402
import gauntlet_report  # noqa: E402

ROOT = HERE.parents[2]
BASE = ROOT / "build/edison-retrain-2026-11-04"
EVIDENCE = ROOT / "tests/edison/reports/retrain-2026-11-04"
NATIVE = "b8fcd2bcf7774ae4"
CHECKPOINTS = (160, 320, 640, 1280, 2560)
TOP = 2560


def configure():
    gauntlet_eval.BASE, gauntlet_eval.EVIDENCE = BASE, EVIDENCE
    gauntlet_eval.CHECKPOINTS, gauntlet_eval.TOP, gauntlet_eval.NATIVE = CHECKPOINTS, TOP, NATIVE
    gauntlet_eval.FIXTURE_SEED, gauntlet_eval.GAUNTLET_SEED = 82000000, 82010000
    gauntlet_report.BASE, gauntlet_report.EVAL, gauntlet_report.EVIDENCE = BASE, BASE / "eval", EVIDENCE
    gauntlet_report.CHECKPOINTS, gauntlet_report.TOP, gauntlet_report.MIDDLE, gauntlet_report.START = CHECKPOINTS, TOP, 1280, 160
    final_test.BASE, final_test.RUNS, final_test.EVIDENCE = BASE / "final", BASE / "runs", EVIDENCE / "final"
    final_test.FIXTURE_SEED, final_test.GAUNTLET_SEED = 83000000, 83010000
    final_test.NATIVE, final_test.CHECKPOINT = NATIVE, f"checkpoint-{TOP:04d}"


def forward(module, arguments):
    sys.argv = [module.__file__, *arguments]
    return module.main()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("eval", "report", "final", "final-report"))
    parser.add_argument("--workers", type=int, default=8)
    args = parser.parse_args()
    configure()
    summary = EVIDENCE / "summary.json"
    if args.command == "eval":
        return forward(gauntlet_eval, ["--workers", str(args.workers)])
    if args.command == "report":
        return forward(gauntlet_report, ["--output", str(summary)])
    if args.command == "final-report":
        return forward(final_test, ["report"])
    verdict = json.loads(summary.read_text())["conclusions"]["G3"]["verdict"]
    if verdict != "CANDIDATE":
        print(f"G3 is {verdict}: the final test stays unplayed, as the protocol says.")
        return 0
    return forward(final_test, ["run", "--workers", str(args.workers)])


if __name__ == "__main__":
    raise SystemExit(main())
