#!/usr/bin/env python3
"""Production regression: a failing card script must fail the guarded worker closed."""
import argparse
from pathlib import Path
import tempfile

from agent_match import PROFILE, ROOT, run_worker

# Both are in the blackwing fixture and have no historical override, so the file is what runs.
HEAVY_STORM = 19613556
BOOK_OF_MOON = 14087893
SCRIPTS = (ROOT / "scripts/script").resolve()

def overlay(root, code=None, plant=None):
    """A script directory holding only what the code list loads, with one file replaced."""
    target = root / "script"
    target.mkdir(parents=True)
    for name in ("constant.lua", "utility.lua", "procedure.lua"):
        (target / name).symlink_to(SCRIPTS / name)
    for line in PROFILE.read_text().splitlines():
        card, has_script = line.split()
        if int(has_script):
            (target / f"c{card}.lua").symlink_to(SCRIPTS / f"c{card}.lua")
    if plant:
        path = target / f"c{code}.lua"
        text = path.read_text()
        path.unlink()
        path.write_text(plant(text))
    return root

def replace_once(text, old, new):
    assert text.count(old) == 1, f"Plant anchor must occur once: {old}"
    return text.replace(old, new)

def load_error(text):
    return replace_once(text, f"function c{HEAVY_STORM}.initial_effect(c)\n",
                        f'function c{HEAVY_STORM}.initial_effect(c)\n\terror("planted load failure")\n')

def syntax_error(text):
    return text + "\nfunction (\n"

def play_error(text):
    return replace_once(text, f"function c{BOOK_OF_MOON}.target(e,tp,eg,ep,ev,re,r,rp,chk,chkc)\n",
                        f'function c{BOOK_OF_MOON}.target(e,tp,eg,ep,ev,re,r,rp,chk,chkc)\n\terror("planted play failure")\n')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fail-fast", action="store_true")
    args = parser.parse_args()
    passed = failed = 0
    cases = [
        ("A", "CLEAN OVERLAY CONTROL", None, None, None),
        ("B", "LUA RUNTIME ERROR AT CARD LOAD", HEAVY_STORM, load_error, "planted load failure"),
        ("C", "LUA SYNTAX ERROR AT CARD LOAD", HEAVY_STORM, syntax_error, f"c{HEAVY_STORM}.lua"),
        ("D", "LUA RUNTIME ERROR DURING PLAY", BOOK_OF_MOON, play_error, "planted play failure"),
    ]
    with tempfile.TemporaryDirectory(prefix="edison-diagnostics-") as tmp:
        for label, description, code, plant, marker in cases:
            print(f"\nEDISON RULE TEST 084{label} - {description}")
            print("Expected: a clean overlay completes; each planted failure reaches stderr and fails the worker.")
            root = Path(tmp) / label
            scripts = overlay(root / "scripts", code, plant)
            output = root / "worker"
            try:
                try:
                    result = run_worker(20840424, 2, 1, "blackwing", "frog_monarch", output,
                                        script_root=scripts)
                    outcome = "completed"
                except RuntimeError as error:
                    assert "Native diagnostic detected" in str(error), f"Unexpected failure: {error}"
                    result, outcome = None, "rejected"
                stderr = (output / "stderr-1.txt").read_text()
                if plant is None:
                    assert outcome == "completed" and len(result["duels"]) == 2, outcome
                    assert "[ygopro-core]" not in stderr, stderr[:400]
                    print("Observed: clean overlay duels completed with no core diagnostic.")
                else:
                    assert outcome == "rejected", "Worker accepted a duel with a failing script"
                    assert "[ygopro-core] Error:" in stderr and marker in stderr, stderr[:400]
                    print("Observed: core diagnostic reached stderr and run_worker raised.")
                print("RESULT: PASS")
                passed += 1
            except (AssertionError, OSError, ValueError) as error:
                print(f"RESULT: FAIL — {error}")
                failed += 1
                if args.fail_fast:
                    break
    print(f"\nSUMMARY: {passed} PASS, {failed} FAIL")
    return int(failed != 0)

if __name__ == "__main__":
    raise SystemExit(main())
