#!/usr/bin/env python3
"""The suite's verdict. run_all.sh returns this program's exit code.

Every other script reports. This one judges: any nonzero mismatch, diagnostic or invariant counter fails, every planted
control must be caught, every planted failure must void as planned, and digests must agree across processes and heaps. A
missing result fails too, so a step that died cannot pass by saying nothing. Gaps that are known and accepted are listed in
KNOWN below with a limit, so they fail if they grow.

Usage: check_results.py [DIR]    DIR defaults to build/forward_model.
"""
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
# Residuals documented in docs/edison-forward-model.md. Each is a fraction of its denominator that must not be exceeded.
KNOWN = {
    "r4_exact_order": 0.002,     # pairs equal in hidden slots and canonical chain, but not in exact option order
    "r4_copy_order": 0.001,      # R4 pairs whose true Decks loaded copies in another order: a random pick over a group reads load order
    "r4w_exact_order": 0.002,
    "r4_refusal_bit": 0.005,     # pairs where one world dealt and the other refused
    "r4w_refusal_bit": 0.005,
    "handtrap_refusal_bit": 0.002,  # compared viewer decisions where only one world refused
    "handtrap_exact_order": 0.002,
}
LEAK_CONTROLS = ("leak-hand", "leak-seed", "leak-order")
ROLLBACK_CONTROLS = ("rollback-history", "rollback-cursor", "rollback-reveal", "rollback-bind", "rollback-rng")
VOID_EXPECTED = {"lua": "lua_error", "prompt": "environment_throw", "hang": "hang", "stack": "lua_error", "segv": "fatal_signal", "abort": "fatal_signal",
                 "marker": "native_diagnostic", "flush": "native_diagnostic", "printthrow": "environment_throw", "outsidearena": "hang"}
failures = []


def fail(message):
    failures.append(message)


def require(condition, message):
    if not condition:
        fail(message)


def lines(directory, name):
    path = directory / name
    if not path.exists():
        fail(f"{name} is missing")
        return []
    out = []
    for number, line in enumerate(path.read_text().splitlines(), 1):
        if not line.strip():
            continue
        try:
            out.append(json.loads(line))
        except ValueError:
            fail(f"{name}:{number} is not JSON")
    return out


def zero(record, keys, where):
    for key in keys:
        require(key in record, f"{where}: {key} is missing")
        if record.get(key, 0):
            fail(f"{where}: {key} = {record[key]}")


def check_verify(directory):
    records = [r for r in lines(directory, "results.jsonl") if "verify" in r]
    require(len(records) >= 7, f"results.jsonl: {len(records)} verify runs, expected 7")
    compared = [r for r in records if r.get("observations_compared")]
    require(len(compared) >= 1, "results.jsonl: no run compared observations with production")
    for index, r in enumerate(records):
        where = f"verify[{index}:{r.get('verify')}]"
        zero(r, ("games_with_a_difference", "seat_differences", "option_count_differences", "result_differences", "cards_", "global_", "decision_", "selection_", "actions_",
                 "h_actions_", "stamp_vs_writestate_differences", "diagnostics"), where)
        require(r.get("lists") == 21, f"{where}: {r.get('lists')} lists")
        require(r.get("games", 0) >= 672, f"{where}: {r.get('games')} games")
    full = ("glibc-deep", "glibc-deep2", "perturb-deep", "arena-deep", "encode-deep")
    digests = {}
    for name in full:
        path = directory / f"chains-{name}.sorted"
        if not path.exists() or not path.stat().st_size:
            fail(f"{path.name} is missing or empty")
            continue
        digests[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    if len(set(digests.values())) > 1:
        fail(f"full digests differ across processes and heaps: {digests}")
    columns = {}
    for name in ("glibc-deep", "glibc-nodeep", "arena-nodeep"):
        path = directory / f"chains-{name}.sorted"
        if not path.exists():
            fail(f"{path.name} is missing")
            continue
        rows = [line.split() for line in path.read_text().splitlines()]
        columns[name] = hashlib.sha256(repr([(r[0], r[1], r[2], r[5], r[6]) for r in rows]).encode()).hexdigest()
    if len(set(columns.values())) > 1:
        fail(f"decision digests differ with hashing or allocator: {columns}")


def check_clean_correct(r, where):
    zero(r, ("truncated_by_cap", "position_failures", "replay_repeat_bad", "replay_perturbed_heap_bad", "fork_bad", "arena_bad", "arena_decision_bad", "arena_deep_bad",
             "arena_obs_bad", "arena_reverse_bad", "arena_scrub_bad", "arena_bytes_bad", "arena_bytes_differing", "env_state_bad", "arena_return_bad", "diagnostics"), where)
    require(r.get("lists") == 21, f"{where}: {r.get('lists')} lists")
    require(r.get("arena_bytes_compared", 0) == r.get("options", -1), f"{where}: bytes compared {r.get('arena_bytes_compared')} of {r.get('options')} options")
    require(r.get("branch_points", 0) >= 400, f"{where}: {r.get('branch_points')} branch points")


def check_redeal(r, where):
    require(r.get("lists_covered") == 21, f"{where}: {r.get('lists_covered')} lists")
    zero(r, ("crashed_positions",), where)
    r1, r2, r3, r4, r4w, r5 = (r.get(k, {}) for k in ("r1", "r2", "r3", "r4", "r4w", "r5"))
    zero(r1, ("invariant_bad", "diag", "exceptions", "repeat_bad"), where + " r1")
    require(r1.get("done") == r1.get("playouts") and r1.get("playouts"), f"{where} r1: {r1.get('done')} of {r1.get('playouts')} done")
    zero(r2, ("invariant_bad", "diag", "exceptions", "repeat_bad", "root_obs_changed", "hard_rule_violations", "true_world_rule_violations", "slot_state_changed"), where + " r2")
    require(r2.get("done") == r2.get("playouts") and r2.get("playouts"), f"{where} r2: {r2.get('done')} of {r2.get('playouts')} done")
    zero(r3, ("exceptions", "compare_exceptions"), where + " r3")
    require(r3.get("layout_same"), f"{where} r3: no samples")
    for key in ("board_match", "canon_match_cardid", "winner_match"):
        require(r3.get(key) == r3.get("layout_same"), f"{where} r3: {key} {r3.get(key)} of {r3.get('layout_same')}")
    for name, section, exact, bit in (("r4", r4, "r4_exact_order", "r4_refusal_bit"), ("r4w", r4w, "r4w_exact_order", "r4w_refusal_bit")):
        zero(section, ("failed",), f"{where} {name}")
        dealt = section.get("pairs", 0) - section.get("refused_both", 0) - section.get("refused_one_world_only", 0)
        require(dealt > 0, f"{where} {name}: no pairs dealt")
        for key in ("slots_equal", "root_obs_equal"):
            require(section.get(key) == dealt, f"{where} {name}: {key} {section.get(key)} of {dealt}")
        if name == "r4w":
            require(section.get("canon_equal") == dealt, f"{where} {name}: canon_equal {section.get('canon_equal')} of {dealt}")
        else:
            require(section.get("layout_same_pairs", 0) > 0, f"{where} r4: no pair with one load order")
            for key in ("layout_same_slots", "layout_same_canon"):
                require(section.get(key) == section.get("layout_same_pairs"), f"{where} r4: {key} {section.get(key)} of {section.get('layout_same_pairs')}")
            if section.get("canon_equal", 0) < dealt * (1 - KNOWN["r4_copy_order"]):
                fail(f"{where} r4: canonical chain {section.get('canon_equal')} of {dealt}, past the known limit")
        if dealt and section.get("raw_equal", 0) < dealt * (1 - KNOWN[exact]):
            fail(f"{where} {name}: exact order {section.get('raw_equal')} of {dealt}, past the known limit")
        if section.get("refused_one_world_only", 0) > section.get("pairs", 0) * KNOWN[bit]:
            fail(f"{where} {name}: {section.get('refused_one_world_only')} one-sided refusals, past the known limit")
    require(r4.get("control_chain_equal", 1e9) < r4.get("control_pairs", 0) * 0.1, f"{where} r4: its no-deal control is not different enough, {r4.get('control_chain_equal')} of {r4.get('control_pairs')}")
    zero(r4w, ("hard_rule_violations",), where + " r4w")
    require(r4w.get("private_pairs", 0) > 0, f"{where} r4w: no pair differed in a private prompt")
    zero(r5, ("invariant_bad", "diag", "exceptions"), where + " r5")
    require(r5.get("done") == r5.get("playouts") and r5.get("playouts"), f"{where} r5: {r5.get('done')} of {r5.get('playouts')} done")


def check_correct(directory):
    records = lines(directory, "correct.jsonl")
    kinds = {}
    for r in records:
        for key in ("correct", "statics_changed", "redeal", "uniform", "rules", "set_fixture", "handtrap", "construct", "set_rule_facts"):
            if key in r:
                kinds.setdefault(key, []).append(r)
    for key in ("correct", "statics_changed", "redeal", "uniform", "rules", "set_fixture", "handtrap", "set_rule_facts"):
        require(key in kinds, f"correct.jsonl: no {key} result")
    for r in kinds.get("correct", []):
        check_clean_correct(r, "correct")
    for r in kinds.get("statics_changed", []):
        require(r.get("statics_changed"), "statics: nothing changed, so the audit saw nothing")
        require(not r.get("not_allowed"), f"statics: {r.get('not_allowed')}")
    for r in kinds.get("redeal", []):
        check_redeal(r, "redeal")
    for r in kinds.get("uniform", []):
        for slot in ("hand", "constrained_slot"):
            section = r.get(slot, {})
            require(section.get("positions", 0) > 0, f"uniform {slot}: no positions")
            require(0.9 <= section.get("ratio", 0) <= 1.1, f"uniform {slot}: chi-square ratio {section.get('ratio')}")
            require(section.get("over_three_sigma", 1e9) <= max(2, section.get("positions", 0) * 0.05), f"uniform {slot}: {section.get('over_three_sigma')} positions past 3 sigma")
    rules = kinds.get("rules", [])
    require({r.get("policy") for r in rules} == {"recorded", "random"}, "rules: need both recorded and random play")
    for r in rules:
        zero(r, ("true_world_rejected",), f"rules {r.get('policy')}")
    for r in kinds.get("set_fixture", []):
        zero(r, ("above_level_four", "hard_rule_violations", "refused"), "set fixture")
        require(r.get("ok", 0) > 0, "set fixture: no deals")
    for r in kinds.get("handtrap", []):
        zero(r, ("slot_differences", "canonical_differences", "skipped"), "hand traps")
        compared = r.get("viewer_decisions_compared", 0)
        require(compared > 0, "hand traps: nothing compared")
        if r.get("refusal_differences", 0) > compared * KNOWN["handtrap_refusal_bit"]:
            fail(f"hand traps: {r.get('refusal_differences')} one-sided refusals of {compared}, past the known limit")
        if r.get("exact_differences", 0) > compared * KNOWN["handtrap_exact_order"]:
            fail(f"hand traps: {r.get('exact_differences')} exact differences of {compared}, past the known limit")
        gorz = r.get("gorz", {})
        zero(gorz, ("refusal_differences",), "hand traps, Gorz")
        require(gorz.get("extra_private_prompts", 0) > 0, "hand traps: no Gorz prompt was reached, so this tested nothing")
    for r in kinds.get("set_rule_facts", []):
        require(not r.get("unexpected") and not r.get("field_or_ritual"), f"set rules: {r.get('unexpected')} {r.get('field_or_ritual')}")


def check_controls(directory):
    records = lines(directory, "controls.jsonl")
    by_mutant = {}
    for r in records:
        by_mutant.setdefault(r.get("mutant"), []).append(r)
    for name in LEAK_CONTROLS:
        found = by_mutant.get(name, [])
        require(found, f"control {name}: no result")
        for r in found:
            r4 = r.get("r4", {})
            dealt = r4.get("pairs", 0) - r4.get("refused_both", 0) - r4.get("refused_one_world_only", 0)
            require(dealt > 0 and r4.get("slots_equal", dealt) < dealt, f"control {name}: not caught by slot equality, {r4.get('slots_equal')} of {dealt}")
    for name in ROLLBACK_CONTROLS:
        found = by_mutant.get(name, [])
        require(found, f"control {name}: no result")
        for r in found:
            require(r.get("arena_bad", 0) > 0, f"control {name}: not caught, arena_bad {r.get('arena_bad')}")
    found = by_mutant.get("relax-set", [])
    require(found, "control relax-set: no result")
    for r in found:
        require(r.get("above_level_four", 0) > 0 and r.get("hard_rule_violations", 0) > 0, f"control relax-set: not caught: {r}")
    found = by_mutant.get("stay-attack-state", [])
    require(found, "control stay-attack-state: no result")
    for r in found:
        require(r.get("r2", {}).get("slot_state_changed", 0) > 0, f"control stay-attack-state: not caught: {r.get('r2', {}).get('slot_state_changed')}")
    found = by_mutant.get("refuse-relate", [])
    require(found, "control refuse-relate: no result")
    for r in found:
        require(r.get("refusal_differences", 0) > 0, f"control refuse-relate: Gorz not caught: {r.get('refusal_differences')}")


def check_void(directory):
    records = lines(directory, "void.jsonl")
    seen = set()
    for r in records:
        name = r.get("void_test")
        where = f"void {name} ({r.get('mutant')})"
        if r.get("mutant") != "none":
            seen.add("control")
            require(r.get("clean_digest_differences", 0) > 0, f"{where}: the missed rollback was not caught")
            continue
        seen.add(name)
        zero(r, ("capped", "truncated", "clean_status_differences", "clean_digest_differences", "void_reproduce_differences"), where)
        require(r.get("true_game_finished_after_search") == 1, f"{where}: the true game did not finish correctly")
        require(r.get("finished", 0) > 0, f"{where}: no world finished")
        expected = VOID_EXPECTED.get(name)
        voids = r.get("voids", {})
        require(voids.get(expected, 0) > 0, f"{where}: planted failure did not void as {expected}: {voids}")
        require(sum(voids.values()) == voids.get(expected, 0), f"{where}: voided another way: {voids}")
    for name in list(VOID_EXPECTED) + ["control"]:
        require(name in seen, f"void: no result for {name}")
    # A marker in a failed world must not reach stdout later.
    for path in directory.glob("void.out"):
        text = path.read_text(errors="replace")
        for line in text.splitlines():
            if line.startswith("Spec not found"):
                fail("void.out: a marker from a failed world reached stdout")


def check_text(directory):
    api = directory / "api.out"
    if not api.exists():
        fail("api.out is missing")
    else:
        text = api.read_text()
        require("0 failures" in text, "api: contract checks did not report 0 failures")
        require("FAIL" not in text, "api: a check failed")
        require(text.count("does not compile") == 8, f"api: {text.count('does not compile')} compile refusals, expected 8")
    refusal = directory / "build_refusal.out"
    if not refusal.exists():
        fail("build_refusal.out is missing")
    else:
        text = refusal.read_text()
        require(text.count("pass:") == 6 and "FAIL" not in text, "build refusal: not 6 of 6 refused")
    records = lines(directory, "throughput.jsonl")
    nodes = [r for r in records if r.get("node")]
    require({r.get("workers") for r in nodes} == {1, 2, 4}, "throughput: need 1, 2 and 4 workers")
    for r in nodes:
        zero(r, ("node_void", "playout_void"), f"throughput {r.get('workers')}")
        require(r.get("nodes", 0) > 0, f"throughput {r.get('workers')}: no nodes")


def main():
    directory = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/forward_model"
    check_verify(directory)
    check_correct(directory)
    check_controls(directory)
    check_void(directory)
    check_text(directory)
    for message in failures:
        print("FAIL:", message)
    print(json.dumps({"checked": str(directory), "failures": len(failures)}))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
