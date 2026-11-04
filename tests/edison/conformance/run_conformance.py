#!/usr/bin/env python3
"""Run written-specification conformance fixtures, separately from certification.

This is NOT the certification gate. tests/edison/run.py and its cases do not read
or run anything in this directory, and this runner does not change their meaning.

Each test number runs in its own process with a timeout, twice, so a hang or a crash
is a recorded result for that number rather than the end of the run. The exit status
compares results against known_failures.json:

  0  every result is expected: fixtures pass, or fail exactly as recorded there
  1  a new [RULE] failure, a recorded failure that now passes, a [SETUP] failure,
     a timeout or crash, or a replay that differs
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
RESULT = re.compile(r"^RESULT: (PASS|FAIL \[(RULE|SETUP)\])\s*(.*)$")


def parse(text):
    cases = []
    current = None
    for line in text.splitlines():
        if line.startswith("TRACE "):
            continue
        match = re.match(r"^(EDISON RULE TEST|CONTROL) (\d{3}[A-Z]?) - (.*)$", line)
        if match:
            current = {"case": match.group(2), "kind": "control" if match.group(1) == "CONTROL" else "rule",
                       "title": match.group(3), "observed": [], "expected": ""}
            cases.append(current)
        elif current is not None and line.startswith("Expected: "):
            current["expected"] = line[len("Expected: "):]
        elif current is not None and line.startswith("Observed: "):
            current["observed"].append(line[len("Observed: "):])
        else:
            result = RESULT.match(line)
            if result and current is not None:
                current["status"] = "PASS" if result.group(1) == "PASS" else "FAIL"
                current["failure_class"] = result.group(2)
                current["message"] = result.group(3)
    return cases


def execute(binary, database, scripts, number, timeout):
    command = [str(binary), str(database), str(scripts), "--trace", f"--test={number}"]
    try:
        done = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired as error:
        return {"outcome": "TIMEOUT", "text": (error.stdout or b"").decode(errors="replace") if isinstance(error.stdout, bytes) else (error.stdout or "")}
    outcome = "CRASH" if done.returncode < 0 else "RAN"
    return {"outcome": outcome, "returncode": done.returncode, "text": done.stdout + done.stderr}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/edison-conformance/linux/x86_64/release/edison_conformance")
    parser.add_argument("--database", type=Path, default=ROOT / "assets/locale/en/cards.cdb")
    parser.add_argument("--scripts", type=Path, default=ROOT / "scripts/script")
    parser.add_argument("--output", type=Path, default=ROOT / "build/edison-conformance/results")
    parser.add_argument("--test", help="Run only this three-digit number")
    parser.add_argument("--timeout", type=int, default=20)
    parser.add_argument("--known", type=Path, default=HERE / "known_failures.json")
    args = parser.parse_args()

    listed = subprocess.run([str(args.binary), str(args.database), str(args.scripts), "--list"],
                            cwd=ROOT, text=True, capture_output=True, check=True)
    numbers = sorted({line.split("\t")[0] for line in listed.stdout.splitlines() if line.strip()})
    if args.test:
        numbers = [n for n in numbers if n == args.test]
        if not numbers:
            parser.error(f"No fixture numbered {args.test}")
    args.output.mkdir(parents=True, exist_ok=True)
    known = json.loads(args.known.read_text()) if args.known.exists() else {"failures": {}}
    known_failures = known["failures"]

    cases, infrastructure, replay_differs = [], [], []
    for number in numbers:
        first = execute(args.binary, args.database, args.scripts, number, args.timeout)
        second = execute(args.binary, args.database, args.scripts, number, args.timeout)
        (args.output / f"test-{number}.txt").write_text(first["text"])
        if first["outcome"] != "RAN":
            infrastructure.append({"test": number, "outcome": first["outcome"], "tail": first["text"][-600:]})
        if first["text"] != second["text"]:
            replay_differs.append(number)
        parsed = parse(first["text"])
        for case in parsed:
            case.setdefault("status", "NO_RESULT")
            case["test"] = number
        cases += parsed

    unexpected = []
    for case in cases:
        if case["kind"] != "rule":
            continue
        recorded = case["case"] in known_failures
        if case["status"] == "FAIL" and case.get("failure_class") == "RULE" and not recorded:
            unexpected.append(f"NEW RULE FAILURE {case['case']}: {case.get('message')}")
        if case["status"] == "PASS" and recorded:
            unexpected.append(f"RECORDED FAILURE NOW PASSES {case['case']}: update known_failures.json")
    setup_failures = [c for c in cases if c.get("failure_class") == "SETUP"]
    no_result = [c for c in cases if c["status"] == "NO_RESULT"]
    rule_cases = [c for c in cases if c["kind"] == "rule"]
    summary = {
        "note": "Separate from certification. Passing or failing here does not change the gate.",
        "tests_run": len(numbers),
        "rule_cases": len(rule_cases),
        "rule_pass": sum(c["status"] == "PASS" for c in rule_cases),
        "rule_fail": sum(c.get("failure_class") == "RULE" for c in rule_cases),
        "setup_fail": len(setup_failures),
        "control_cases": sum(c["kind"] == "control" for c in cases),
        "control_failures": [c["case"] for c in cases if c["kind"] == "control" and c["status"] != "PASS"],
        "infrastructure": infrastructure,
        "replay_differs": replay_differs,
        "unexpected": unexpected,
        "no_result": [c["case"] for c in no_result],
    }
    (args.output / "results.json").write_text(json.dumps({"summary": summary, "cases": cases}, indent=1) + "\n")
    for case in cases:
        flag = case["status"] if case["status"] != "FAIL" else f"FAIL[{case['failure_class']}]"
        print(f"{flag:12} {'C' if case['kind'] == 'control' else ' '} {case['case']:5} {case['title']}")
        for line in case["observed"]:
            print(f"             {line}")
        if case["status"] == "FAIL":
            print(f"             >> {case.get('message')}")
    print(json.dumps(summary, indent=1))
    clean = not (unexpected or setup_failures or no_result or infrastructure or replay_differs)
    print("CONFORMANCE RUN: " + ("AS RECORDED" if clean else "NEEDS REVIEW"))
    return 0 if clean else 1


if __name__ == "__main__":
    sys.exit(main())
