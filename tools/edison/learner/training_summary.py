#!/usr/bin/env python3
"""Summarize all training runs, including development runs, from their rounds.jsonl files."""
import argparse
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from curves import curve  # noqa: E402

ROOT = HERE.parents[2]
BASE = ROOT / "build/edison-learner-2026-11-04"


def summarize(path):
    rows = [json.loads(line) for line in (path / "rounds.jsonl").read_text().splitlines()]
    run = json.loads((path / "run.json").read_text())
    config = run["config"]
    marks = {}
    for mark in (32, 80, 160, 320):
        part = rows[:mark]
        if len(part) >= mark:
            marks[mark * config["games_per_cell"] * config["cells_per_round"]] = {
                "wall_seconds": round(sum(r["wall_seconds"] for r in part)),
                "collect_seconds": round(sum(r["collect_seconds"] for r in part)),
                "update_seconds": round(sum(r["update_seconds"] for r in part)),
                "updates": sum(r["updates"] for r in part),
                "decisions": sum(r["decisions"] for r in part),
                "learner_decisions": sum(r["learner_decisions"] for r in part)}
    return {"config": {k: config[k] for k in ("arm", "optimizer_seed", "rounds", "seed_base", "deck_set", "opponents", "learning_rate",
                                             "gae_lambda", "channels", "id_dim", "layers")},
            "workers": run["workers"], "learner_cpus": run["learner_cpus"], "rounds_done": len(rows), "duels": rows[-1]["duels"],
            "void_games": sum(r["void_games"] for r in rows), "marks": marks, "curve": curve(path, block=32 if len(rows) > 64 else 8),
            "final_params_sha256": rows[-1]["params_sha256"], "config_hash": run["config_hash"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = {"pilot": {}, "development": {}}
    for kind, folder in (("pilot", BASE / "pilot"), ("development", BASE / "dev")):
        for path in sorted(folder.iterdir()):
            if path.is_dir() and (path / "rounds.jsonl").exists():
                report[kind][path.name] = summarize(path)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=1) + "\n")
    for kind, runs in report.items():
        for name, run in runs.items():
            print(kind, name, run["duels"], "void", run["void_games"], {k: (v["wall_seconds"], v["updates"]) for k, v in run["marks"].items()})


if __name__ == "__main__":
    main()
