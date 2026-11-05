#!/usr/bin/env python3
"""A checker that cannot fail proves nothing. Break real results, one fault at a time, and require a failure.

Run after a complete suite: it works on copies of build/forward_model's results and needs the checker to pass on the
unbroken copy first.
"""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
SOURCE = HERE.parents[2] / "build/forward_model"
FILES = ["results.jsonl", "correct.jsonl", "controls.jsonl", "void.jsonl", "api.out", "build_refusal.out", "throughput.jsonl", "void.out"]
CHAINS = ["glibc-deep", "glibc-deep2", "perturb-deep", "arena-deep", "encode-deep", "glibc-nodeep", "arena-nodeep"]


def edit_json(directory, name, match, change):
    path = directory / name
    out = []
    done = False
    for line in path.read_text().splitlines():
        record = json.loads(line)
        if not done and match(record):
            change(record)
            done = True
        out.append(json.dumps(record))
    if not done:
        raise SystemExit(f"nothing in {name} to break")
    path.write_text("\n".join(out) + "\n")


def setter(path, value):
    def change(record):
        at = record
        for key in path[:-1]:
            at = at[key]
        at[path[-1]] = value
    return change


def is_redeal(r):
    return "redeal" in r and r.get("mutant") is None


FAULTS = {
    "a production field differs": lambda d: edit_json(d, "results.jsonl", lambda r: "verify" in r, setter(["h_actions_"], 1)),
    "a diagnostic in verify": lambda d: edit_json(d, "results.jsonl", lambda r: "verify" in r, setter(["diagnostics"], 1)),
    "digests differ across heaps": lambda d: (d / "chains-perturb-deep.sorted").write_text((d / "chains-perturb-deep.sorted").read_text().replace("0 ", "1 ", 1)),
    "a chain file is missing": lambda d: (d / "chains-encode-deep.sorted").unlink(),
    "a rollback mismatch": lambda d: edit_json(d, "correct.jsonl", lambda r: "correct" in r, setter(["arena_deep_bad"], 1)),
    "a differing arena byte": lambda d: edit_json(d, "correct.jsonl", lambda r: "correct" in r, setter(["arena_bytes_differing"], 3)),
    "an invariant failure": lambda d: edit_json(d, "correct.jsonl", is_redeal, setter(["r2", "invariant_bad"], 1)),
    "a hard rule broken by a deal": lambda d: edit_json(d, "correct.jsonl", is_redeal, setter(["r2", "hard_rule_violations"], 1)),
    "a true world rejected": lambda d: edit_json(d, "correct.jsonl", lambda r: "rules" in r, setter(["true_world_rejected"], 1)),
    "R4 slots differ": lambda d: edit_json(d, "correct.jsonl", is_redeal, lambda r: r["r4"].__setitem__("slots_equal", r["r4"]["slots_equal"] - 1)),
    "R4 canonical chains differ with one load order": lambda d: edit_json(d, "correct.jsonl", is_redeal, lambda r: r["r4"].__setitem__("layout_same_canon", r["r4"]["layout_same_canon"] - 1)),
    "R4 canonical chains past the limit": lambda d: edit_json(d, "correct.jsonl", is_redeal, lambda r: r["r4"].__setitem__("canon_equal", r["r4"]["canon_equal"] - 50)),
    "a slot's state changed by a deal": lambda d: edit_json(d, "correct.jsonl", is_redeal, setter(["r2", "slot_state_changed"], 1)),
    "a planted slot state not caught": lambda d: edit_json(d, "controls.jsonl", lambda r: r.get("mutant") == "stay-attack-state", setter(["r2", "slot_state_changed"], 0)),
    "R4W canonical chains differ": lambda d: edit_json(d, "correct.jsonl", is_redeal, lambda r: r["r4w"].__setitem__("canon_equal", r["r4w"]["canon_equal"] - 1)),
    "a Gorz refusal difference": lambda d: edit_json(d, "correct.jsonl", lambda r: "handtrap" in r, setter(["gorz", "refusal_differences"], 1)),
    "too many one-sided refusals": lambda d: edit_json(d, "correct.jsonl", lambda r: "handtrap" in r, setter(["refusal_differences"], 100)),
    "the Set fixture broken": lambda d: edit_json(d, "correct.jsonl", lambda r: "set_fixture" in r, setter(["above_level_four"], 1)),
    "a planted leak not caught": lambda d: edit_json(d, "controls.jsonl", lambda r: r.get("mutant") == "leak-hand", lambda r: r["r4"].__setitem__("slots_equal", r["r4"]["pairs"] - r["r4"]["refused_both"] - r["r4"]["refused_one_world_only"])),
    "a planted rollback not caught": lambda d: edit_json(d, "controls.jsonl", lambda r: r.get("mutant") == "rollback-reveal", setter(["arena_bad"], 0)),
    "a planted Set rule not caught": lambda d: edit_json(d, "controls.jsonl", lambda r: r.get("mutant") == "relax-set", setter(["hard_rule_violations"], 0)),
    "a control missing": lambda d: edit_json(d, "controls.jsonl", lambda r: r.get("mutant") == "rollback-bind", setter(["mutant"], "something-else")),
    "a void counted as a finish by cap": lambda d: edit_json(d, "void.jsonl", lambda r: r.get("void_test") == "lua", setter(["capped"], 3)),
    "a planted failure that did not void": lambda d: edit_json(d, "void.jsonl", lambda r: r.get("void_test") == "hang", setter(["voids", "hang"], 0)),
    "a void differing from a clean run": lambda d: edit_json(d, "void.jsonl", lambda r: r.get("void_test") == "segv", setter(["clean_digest_differences"], 2)),
    "a marker reaching stdout": lambda d: (d / "void.out").write_text((d / "void.out").read_text() + "Spec not found: planted\n"),
    "a missing result file": lambda d: (d / "correct.jsonl").unlink(),
    "a failed API check": lambda d: (d / "api.out").write_text((d / "api.out").read_text() + "FAIL: planted\n"),
}


def run_checker(directory):
    return subprocess.run([sys.executable, str(HERE / "check_results.py"), str(directory)], capture_output=True, text=True)


def fresh_copy(root):
    directory = Path(tempfile.mkdtemp(dir=root))
    for name in FILES:
        shutil.copy(SOURCE / name, directory / name)
    for name in CHAINS:
        shutil.copy(SOURCE / f"chains-{name}.sorted", directory / f"chains-{name}.sorted")
    return directory


def main():
    bad = 0
    with tempfile.TemporaryDirectory() as root:
        clean = run_checker(fresh_copy(root))
        print(("pass" if clean.returncode == 0 else "FAIL") + ": the checker accepts the real results")
        if clean.returncode:
            print(clean.stdout[-2000:])
            return 1
        for name, fault in FAULTS.items():
            directory = fresh_copy(root)
            fault(directory)
            result = run_checker(directory)
            caught = result.returncode != 0 and "FAIL:" in result.stdout
            print(("pass" if caught else "FAIL") + f": {name}: " + (result.stdout.splitlines()[0][:110] if caught else "not caught"))
            bad += not caught
    print(f"{len(FAULTS)} faults, {bad} not caught")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
