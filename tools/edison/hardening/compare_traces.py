#!/usr/bin/env python3
"""Compare certification and conformance output case by case, between two builds.

The first run's trace of each case must match unless a change meant to alter it, and this reports
every case that differs. Rule 5 and Edison cases are counted apart: a change gated on Edison, or one
that only alters what a policy observes, must leave every Rule 5 trace alone.
"""
import argparse
import json
from pathlib import Path
import re

HEADING = re.compile(r"^(EDISON RULE TEST \S+|CONTROL \S+|RULE 5 \S+|RULE5 \S+)")


def cases(text):
    found, name, body = {}, None, []
    for line in text.splitlines():
        match = HEADING.match(line)
        if match:
            if name:
                found[name] = "\n".join(body).rstrip()
            name, body = match.group(1), [line]
        elif name:
            body.append(line)
    if name:
        found[name] = "\n".join(body).rstrip()
    return found


def certification(old_dir, new_dir):
    old, new = cases((old_dir / "run-1.txt").read_text()), cases((new_dir / "run-1.txt").read_text())
    report = {"cases_old": len(old), "cases_new": len(new), "only_new": sorted(set(new) - set(old)), "only_old": sorted(set(old) - set(new))}
    shared = sorted(set(old) & set(new))
    rule5 = [name for name in shared if name.startswith(("CONTROL", "RULE"))]
    edison = [name for name in shared if name.startswith("EDISON")]
    report["rule5_cases"] = len(rule5)
    report["rule5_trace_differs"] = [name for name in rule5 if old[name] != new[name]]
    report["edison_cases"] = len(edison)
    report["edison_trace_differs"] = [name for name in edison if old[name] != new[name]]
    return report


def conformance(old_dir, new_dir):
    report = {"tests_old": 0, "tests_new": 0, "test_files_differ": [], "only_old": [], "only_new": []}
    old = {p.name: p.read_text() for p in old_dir.glob("test-*.txt")}
    new = {p.name: p.read_text() for p in new_dir.glob("test-*.txt")}
    report["tests_old"], report["tests_new"] = len(old), len(new)
    report["only_old"], report["only_new"] = sorted(set(old) - set(new)), sorted(set(new) - set(old))
    report["test_files_differ"] = sorted(name for name in set(old) & set(new) if old[name] != new[name])
    for name in ("results.json",):
        report[f"{name}_identical"] = (old_dir / name).read_text() == (new_dir / name).read_text()
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--certification", nargs=2, type=Path, metavar=("OLD", "NEW"), required=True)
    parser.add_argument("--conformance", nargs=2, type=Path, metavar=("OLD", "NEW"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = {"certification": certification(*args.certification), "conformance": conformance(*args.conformance)}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=1) + "\n")
    print(json.dumps(report, indent=1))
    # Rule 5 and Edison traces of earlier cases must match.
    certification_report, conformance_report = report["certification"], report["conformance"]
    problems = certification_report["rule5_trace_differs"] + certification_report["edison_trace_differs"] + certification_report["only_old"]
    problems += conformance_report["test_files_differ"] + conformance_report["only_old"]
    if not conformance_report["results.json_identical"]:
        problems.append("conformance results.json")
    if not certification_report["cases_old"] or not conformance_report["tests_old"]:
        problems.append("nothing compared")
    print(("FAILED: " + ", ".join(problems)) if problems else "PASSED")
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
