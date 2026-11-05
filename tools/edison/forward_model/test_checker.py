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
FILES = ["results.jsonl", "correct.jsonl", "controls.jsonl", "void.jsonl", "api.out", "build_refusal.out", "throughput.jsonl", "void.out", "exceptions.jsonl", "loadorder.jsonl"]
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


def change_redeal(directory, change):
    edit_json(directory, "correct.jsonl", is_redeal, change)


def lower(section, key, by=1):
    def change(r):
        r[section][key] -= by
    return change


def first_exception(field, value):
    def fault(directory):
        path = directory / "exceptions.jsonl"
        rows = [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
        if not rows:
            raise SystemExit("no exception to break")
        rows[0][field] = value
        path.write_text("\n".join(json.dumps(r) for r in rows) + "\n")
    return fault


def forged_exception(directory):
    """One more failure, in the results and in the list, that is not load order."""
    rows = [json.loads(line) for line in (directory / "exceptions.jsonl").read_text().splitlines() if line.strip()]
    forged = {"test": "r4", "game": 1, "index": 1, "deal": 0, "slots_equal": 1, "canon_equal": 0, "raw_equal": 1, "winner_equal": 1, "next8_viewer_obs_equal": 1, "viewer_obs_equal": 1, "all_obs_equal": 1, "numbering_differs": 0,
              "numbering_applied": 1, "same_with_one_numbering": 0, "cause": "unexplained"}
    (directory / "exceptions.jsonl").write_text("\n".join(json.dumps(r) for r in rows + [forged]) + "\n")
    def change(r):
        r["r4"]["canon_equal"] -= 1
        r["r4"]["exceptions"] += 1
    change_redeal(directory, change)


def drop_exception(directory):
    path = directory / "exceptions.jsonl"
    rows = path.read_text().splitlines()
    if not rows:
        raise SystemExit("no exception to drop")
    path.write_text("\n".join(rows[1:]) + ("\n" if rows[1:] else ""))


def load_record(line, plant):
    return lambda r: r.get("loadorder") == line and r.get("plant") == plant and r.get("canonical_numbering") == 0


def bias_table(r):
    """A lean by load order, in the natural results."""
    rows = r["by_sequence"]
    half = len(rows) // 2
    for i, row in enumerate(rows):
        move = row[2] // 2 if i < half else 0
        row[2] -= move
        row[3] += move


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
    # Sol's mutations, from the second review.
    "Sol: every continuation observation fails": lambda d: change_redeal(d, lambda r: [s.__setitem__(k, 0) for s in [r["r4"], r["r4w"]] for k in ["all_obs_equal", "viewer_obs_equal", "next8_viewer_obs_equal"]]),
    "Sol: every sampled winner differs": lambda d: change_redeal(d, lambda r: [r[k].__setitem__("winner_equal", 0) for k in ["r4", "r4w"]]),
    "Sol: one more canonical failure, within the old allowance": lambda d: change_redeal(d, lower("r4", "canon_equal")),
    "Sol: R4W private coverage gone": lambda d: change_redeal(d, lambda r: [r["r4w"].__setitem__(k, 0) for k in ["accept_with_private_difference", "extra_prompts", "skipped_prompts"]]),
    "Sol: one observation failure": lambda d: change_redeal(d, lower("r4w", "all_obs_equal")),
    "Sol: one production cards_ mismatch": lambda d: edit_json(d, "results.jsonl", lambda r: "verify" in r, setter(["cards_"], 1)),
    # Each comparison counter, by one.
    "one more R4 observation failure": lambda d: change_redeal(d, lower("r4", "all_obs_equal")),
    "one more R4 viewer failure": lambda d: change_redeal(d, lower("r4", "viewer_obs_equal")),
    "one more R4W next-8 failure": lambda d: change_redeal(d, lower("r4w", "next8_viewer_obs_equal")),
    "one more R4 winner difference": lambda d: change_redeal(d, lower("r4", "winner_equal")),
    "one more R4W winner difference": lambda d: change_redeal(d, lower("r4w", "winner_equal")),
    "one more R4W exact failure": lambda d: change_redeal(d, lower("r4w", "raw_equal")),
    # Exceptions must be verified, one by one.
    "an unrelated failure listed as an exception": forged_exception,
    "an exception whose cause is not verified": first_exception("same_with_one_numbering", 0),
    "an exception with one numbering in both worlds": first_exception("numbering_differs", 0),
    "an exception with hidden slots differing": first_exception("slots_equal", 0),
    "an exception left out of the list": drop_exception,
    # Coverage and the relations between counters.
    "R4W samples that do not add up": lambda d: change_redeal(d, lower("r4w", "samples")),
    "R4W pairs that do not match accepted worlds": lambda d: change_redeal(d, lower("r4w", "accept", 2)),
    "R3 samples that do not add up": lambda d: change_redeal(d, lower("r3", "samples")),
    "R4 pairs that do not match accepted worlds": lambda d: change_redeal(d, lower("r4", "pairs", 2)),
    "too few positions": lambda d: change_redeal(d, setter(["positions"], 10)),
    "R2 statuses that do not add up": lambda d: change_redeal(d, setter(["r2", "status", "core_reference"], 0)),
    "hand-trap comparison gone": lambda d: edit_json(d, "correct.jsonl", lambda r: "handtrap" in r, setter(["viewer_decisions_compared"], 5)),
    "a hand-trap refusal of another kind": lambda d: edit_json(d, "correct.jsonl", lambda r: "handtrap" in r, lambda r: (r["refusal_kinds"].__setitem__("something else", 1), r.__setitem__("refusal_differences", r["refusal_differences"] + 1))),
    "a hand-trap exact difference": lambda d: edit_json(d, "correct.jsonl", lambda r: "handtrap" in r, setter(["exact_differences"], 1)),
    # The distribution tests.
    "worlds told apart over deal seeds": lambda d: change_redeal(d, lambda r: r["r4w"].__setitem__("distribution_chi_milli", r["r4w"]["distribution_dof"] * 5000)),
    "the distribution test ran on nothing": lambda d: change_redeal(d, setter(["r4w", "distribution_pairs"], 0)),
    "a planted leak the distribution test missed": lambda d: edit_json(d, "controls.jsonl", lambda r: r.get("mutant") == "leak-hand", setter(["r4w", "distribution_chi_milli"], 0)),
    "a planted leak explained away as load order": lambda d: edit_json(d, "controls.jsonl", lambda r: r.get("mutant") == "leak-hand", lambda r: r["r4"].__setitem__("exceptions_explained", r["r4"]["exceptions"])),
    "load-order variants that differ": lambda d: edit_json(d, "loadorder.jsonl", load_record("thestalos_viewer_hand", "none"), bias_table),
    "another line's variants that differ": lambda d: edit_json(d, "loadorder.jsonl", load_record("thestalos_opposing_hand", "none"), bias_table),
    "a first-card plant not seen": lambda d: edit_json(d, "loadorder.jsonl", load_record("thestalos_viewer_hand", "first"), lambda r: r.__setitem__("by_sequence", [r["by_sequence"][0]] * len(r["by_sequence"]))),
    "a load-order result missing": lambda d: edit_json(d, "loadorder.jsonl", load_record("power_tool_viewer_deck", "none"), setter(["loadorder"], "something-else")),
    "one seed giving one result, so the doc is stale": lambda d: edit_json(d, "loadorder.jsonl", load_record("thestalos_viewer_hand", "none"), setter(["paired_different"], 0)),
    "variants with different public histories": lambda d: edit_json(d, "loadorder.jsonl", load_record("thestalos_viewer_hand", "none"), setter(["variants_one_public_history"], 8)),
    "too few worlds reaching the pick": lambda d: edit_json(d, "loadorder.jsonl", load_record("thestalos_opposing_hand", "none"), lambda r: (r.__setitem__("none", r["none"] + r["reached"]), r.__setitem__("reached", 0))),
    "a slot's state changed by a deal": lambda d: edit_json(d, "correct.jsonl", is_redeal, setter(["r2", "slot_state_changed"], 1)),
    "a planted slot state not caught": lambda d: edit_json(d, "controls.jsonl", lambda r: r.get("mutant") == "stay-attack-state", setter(["r2", "slot_state_changed"], 0)),
    "R4W canonical chains differ": lambda d: edit_json(d, "correct.jsonl", is_redeal, lambda r: r["r4w"].__setitem__("canon_equal", r["r4w"]["canon_equal"] - 1)),
    "a Gorz refusal difference": lambda d: edit_json(d, "correct.jsonl", lambda r: "handtrap" in r, setter(["gorz", "refusal_differences"], 1)),
    "too many one-sided refusals": lambda d: edit_json(d, "correct.jsonl", lambda r: "handtrap" in r, lambda r: (r["refusal_kinds"].__setitem__("pending chain list", 100), r.__setitem__("refusal_differences", 100))),
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
