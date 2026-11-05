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
import math
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
# Residuals documented in docs/edison-forward-model.md. A number here is not what allows an exception. Each exception must
# first be verified as its stated cause, so an unrelated failure fails however small it is. The limits below then bound
# how much of a verified cause is acceptable, as low as the data allows.
KNOWN = {
    "r4_load_order_pairs": 0.002,   # verified load-order pairs, a fraction of dealt pairs
    "r4w_load_order_pairs": 0.002,
    "handtrap_refusal_kinds": ("pending chain list",),  # what a one-sided refusal in the hand-trap pairs may be
    "handtrap_refusals": 2,         # the most seen on any corpus. Kind is checked; that a hidden trigger caused it is not
}
# Coverage the suite must reach, so a test cannot pass by testing less.
FLOOR = {"positions": 2000, "r4_pairs": 1500, "r4w_samples": 3000, "r4w_accept": 1000, "r4w_private_accept": 300, "distribution_pairs": 100, "handtrap_fixtures": 100, "handtrap_decisions": 2000}
PAIR_COUNTERS = ("slots_equal", "canon_equal", "raw_equal", "winner_equal", "root_obs_equal", "next8_viewer_obs_equal", "viewer_obs_equal", "all_obs_equal")
HOMOGENEOUS_P = 1e-3     # natural variants must not be told apart: six tests, so a false alarm is about 1 run in 170
PLANT_P = 1e-6           # a planted first-card pick must be seen
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


def chi2_sf(x, k):
    """The chance a chi-square with k degrees of freedom is at least x: the regularized upper incomplete gamma."""
    if x <= 0:
        return 1.0
    a, z = k / 2, x / 2
    log_front = a * math.log(z) - z - math.lgamma(a)
    if z < a + 1:
        total = term = 1 / a
        n = a
        for _ in range(100000):
            n += 1
            term *= z / n
            total += term
            if term < total * 1e-15:
                break
        return max(0.0, 1 - total * math.exp(log_front))
    b = z + 1 - a
    c = 1e300
    d = 1 / b
    h = d
    for i in range(1, 100000):
        an = -i * (i - a)
        b += 2
        d = an * d + b
        d = 1e-300 if abs(d) < 1e-300 else d
        c = b + an / c
        c = 1e-300 if abs(c) < 1e-300 else c
        d = 1 / d
        delta = d * c
        h *= delta
        if abs(delta - 1) < 1e-15:
            break
    return min(1.0, h * math.exp(log_front))


def homogeneity(rows):
    """Chi-square across rows. A column too thin for the test is pooled with the others like it."""
    if not rows or not rows[0]:
        return 0.0, 0, 1.0
    n_rows = len(rows)
    columns = [[row[k] for row in rows] for k in range(len(rows[0]))]
    keep, pool = [], [0] * n_rows
    for column in columns:
        if sum(column) < 5 * n_rows:
            pool = [a + b for a, b in zip(pool, column)]
        else:
            keep.append(column)
    if sum(pool) >= 5 * n_rows:
        keep.append(pool)
    if len(keep) < 2:
        return 0.0, 0, 1.0
    row_totals = [sum(column[i] for column in keep) for i in range(n_rows)]
    total = sum(row_totals)
    chi = 0.0
    for column in keep:
        column_total = sum(column)
        for i in range(n_rows):
            expected = row_totals[i] * column_total / total
            if expected:
                chi += (column[i] - expected) ** 2 / expected
    dof = (n_rows - 1) * (len(keep) - 1)
    return chi, dof, chi2_sf(chi, dof)


def check_counts(r, where):
    """Counters that must agree with each other, and coverage the suite must reach."""
    r2, r3, r4, r4w = (r.get(k, {}) for k in ("r2", "r3", "r4", "r4w"))
    require(r.get("positions", 0) >= FLOOR["positions"], f"{where}: {r.get('positions')} positions")
    status = r2.get("status", {})
    require(sum(status.values()) == r2.get("attempts", -1), f"{where} r2: statuses add to {sum(status.values())} of {r2.get('attempts')}")
    require(status.get("ok") == r2.get("ok") == r2.get("playouts"), f"{where} r2: ok {status.get('ok')}, {r2.get('ok')}, playouts {r2.get('playouts')}")
    require(r2.get("positions_refused_every_deal", 0) <= r.get("positions", 0), f"{where} r2: more positions refused than positions")
    rejected = sum(r3.get(k, 0) for k in ("reject_prompt", "reject_offered", "reject_public", "reject_obs", "reject_action", "reject_end", "exceptions"))
    require(r3.get("accept", 0) + rejected == r3.get("samples", -1), f"{where} r3: accepted {r3.get('accept')} and rejected {rejected} of {r3.get('samples')}")
    require(r3.get("layout_same", 0) <= r3.get("accept", 0), f"{where} r3: more same-layout worlds than accepted")
    require(r3.get("raw_match_cardid", 0) <= r3.get("layout_same", 0), f"{where} r3: exact matches above same-layout worlds")
    require(r4.get("pairs") == 2 * r3.get("accept", 0), f"{where} r4: {r4.get('pairs')} pairs from {r3.get('accept')} accepted worlds")
    require(r4.get("control_pairs") == r3.get("accept"), f"{where} r4: {r4.get('control_pairs')} control pairs from {r3.get('accept')} accepted worlds")
    require(r4.get("layout_same_pairs") == 2 * r3.get("layout_same", 0), f"{where} r4: {r4.get('layout_same_pairs')} same-layout pairs from {r3.get('layout_same')} worlds")
    require(r4.get("pairs", 0) >= FLOOR["r4_pairs"], f"{where} r4: {r4.get('pairs')} pairs")
    rejected = sum(r4w.get(k, 0) for k in ("reject_private", "reject_viewer", "reject_public", "reject_end"))
    require(r4w.get("accept", 0) + rejected + r4w.get("failed", 0) == r4w.get("samples", -1), f"{where} r4w: accepted {r4w.get('accept')} and rejected {rejected} of {r4w.get('samples')}")
    require(r4w.get("samples", 0) >= FLOOR["r4w_samples"], f"{where} r4w: {r4w.get('samples')} samples")
    require(r4w.get("accept", 0) >= FLOOR["r4w_accept"], f"{where} r4w: {r4w.get('accept')} accepted")
    require(r4w.get("accept_with_private_difference", 0) >= FLOOR["r4w_private_accept"], f"{where} r4w: {r4w.get('accept_with_private_difference')} accepted with a private difference")
    require(r4w.get("accept_with_private_difference", 1e9) <= r4w.get("accept", 0), f"{where} r4w: more private worlds than accepted")
    require(r4w.get("extra_prompts", 0) > 0 and r4w.get("skipped_prompts", 0) > 0, f"{where} r4w: extra {r4w.get('extra_prompts')}, skipped {r4w.get('skipped_prompts')} private prompts")
    require(r4w.get("pairs") == 2 * r4w.get("accept", 0), f"{where} r4w: {r4w.get('pairs')} pairs from {r4w.get('accept')} accepted")
    require(r4w.get("private_pairs") == 2 * r4w.get("accept_with_private_difference", 0), f"{where} r4w: {r4w.get('private_pairs')} private pairs from {r4w.get('accept_with_private_difference')} worlds")
    require(r4w.get("refused_both", 0) == r4w.get("refused_a", -1) == r4w.get("refused_b", -2), f"{where} r4w: refused {r4w.get('refused_a')}, {r4w.get('refused_b')}, both {r4w.get('refused_both')}")
    private_dealt = r4w.get("private_pairs", 0) - r4w.get("private_refused_one", 0)
    for key in ("private_slots", "private_canon", "private_raw"):
        require(r4w.get(key, 1e9) <= private_dealt, f"{where} r4w: {key} {r4w.get(key)} above {private_dealt} private pairs")
    require(r4w.get("private_slots", 0) > 0, f"{where} r4w: no private pair was compared")


def check_redeal(r, where, directory):
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
    check_counts(r, where)
    exceptions = lines(directory, "exceptions.jsonl")
    for name, section, limit in (("r4", r4, "r4_load_order_pairs"), ("r4w", r4w, "r4w_load_order_pairs")):
        zero(section, ("failed", "refused_one_world_only"), f"{where} {name}")
        dealt = section.get("pairs", 0) - section.get("refused_both", 0) - section.get("refused_one_world_only", 0)
        require(dealt > 0, f"{where} {name}: no pairs dealt")
        require(section.get("root_obs_equal") == dealt, f"{where} {name}: root_obs_equal {section.get('root_obs_equal')} of {dealt}")
        records = [x for x in exceptions if x.get("test") == name]
        # Every pair that failed any comparison has a record, and every counter agrees with the records.
        require(section.get("exceptions") == len(records), f"{where} {name}: {section.get('exceptions')} exceptions counted, {len(records)} recorded")
        require(section.get("exceptions_explained") == len(records), f"{where} {name}: only {section.get('exceptions_explained')} of {len(records)} exceptions explained")
        for key in PAIR_COUNTERS:
            expected = dealt - sum(1 for x in records if not x.get(key, 1))
            require(section.get(key) == expected, f"{where} {name}: {key} {section.get(key)}, but {expected} from {dealt} dealt and {len(records)} recorded exceptions")
        for later, earlier in (("all_obs_equal", "viewer_obs_equal"), ("viewer_obs_equal", "next8_viewer_obs_equal"), ("next8_viewer_obs_equal", "root_obs_equal")):
            require(section.get(later, 1e9) <= section.get(earlier, -1), f"{where} {name}: {later} {section.get(later)} above {earlier} {section.get(earlier)}")
        for x in records:
            at = f"{where} {name} exception game {x.get('game')} index {x.get('index')} deal {x.get('deal')}"
            require(x.get("slots_equal") == 1, f"{at}: hidden slots differ")
            require(x.get("cause") == "load_order" and x.get("numbering_differs") == 1 and x.get("repeats_untouched") == 1 and x.get("numbering_applied") == 1 and x.get("same_with_one_numbering") == 1, f"{at}: not verified as load order: {x.get('cause')}")
            require(not all(x.get(key, 0) for key in PAIR_COUNTERS if key != "root_obs_equal"), f"{at}: recorded, yet nothing differs")
        if len(records) > dealt * KNOWN[limit]:
            fail(f"{where} {name}: {len(records)} load-order pairs of {dealt}, past the known limit")
    require(r4.get("layout_same_pairs", 0) > 0, f"{where} r4: no pair with one load order")
    for key in ("layout_same_slots", "layout_same_canon", "layout_same_raw", "layout_same_obs"):
        require(r4.get(key) == r4.get("layout_same_pairs"), f"{where} r4: {key} {r4.get(key)} of {r4.get('layout_same_pairs')}")
    require(r4.get("control_chain_equal", 1e9) < r4.get("control_pairs", 0) * 0.1, f"{where} r4: its no-deal control is not different enough, {r4.get('control_chain_equal')} of {r4.get('control_pairs')}")
    # reject_viewer is not here. A rigged world that shows the viewer another byte is simply not a pair.
    zero(r4w, ("hard_rule_violations", "private_refused_one"), where + " r4w")
    # Over deal seeds, the two worlds of a pair should not be told apart.
    pairs, dof, chi = r4w.get("distribution_pairs", 0), r4w.get("distribution_dof", 0), r4w.get("distribution_chi_milli", 0) / 1000
    require(pairs >= FLOOR["distribution_pairs"], f"{where} r4w: {pairs} pairs in the distribution test")
    # A pair enters this test only with at least one degree of freedom, so fewer than that is a malformed result.
    require(dof >= pairs, f"{where} r4w: {dof} degrees of freedom for {pairs} pairs")
    if dof:
        p = chi2_sf(chi, dof)
        require(p >= HOMOGENEOUS_P, f"{where} r4w: the two worlds' playouts differ over deal seeds, chi-square {chi:.0f} on {dof}, p {p:.2g}")
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
        check_redeal(r, "redeal", directory)
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
        zero(r, ("slot_differences", "canonical_differences", "exact_differences", "skipped"), "hand traps")
        compared = r.get("viewer_decisions_compared", 0)
        require(compared >= FLOOR["handtrap_decisions"], f"hand traps: {compared} decisions compared")
        require(r.get("fixtures", 0) >= FLOOR["handtrap_fixtures"], f"hand traps: {r.get('fixtures')} fixtures")
        require(r.get("fixtures_with_extra_private_prompts", 1e9) <= r.get("fixtures", 0), "hand traps: more fixtures with prompts than fixtures")
        kinds = r.get("refusal_kinds", {})
        require(sum(kinds.values()) == r.get("refusal_differences", -1), f"hand traps: refusal kinds add to {sum(kinds.values())} of {r.get('refusal_differences')}")
        for kind in kinds:
            require(kind in KNOWN["handtrap_refusal_kinds"], f"hand traps: a one-sided refusal of kind {kind!r}")
        if r.get("refusal_differences", 0) > KNOWN["handtrap_refusals"]:
            fail(f"hand traps: {r.get('refusal_differences')} one-sided refusals, past the known limit")
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
            require(r4.get("exceptions_explained", 0) < r4.get("exceptions", 0), f"control {name}: load order explained every planted difference away, {r4.get('exceptions_explained')} of {r4.get('exceptions')}")
        if name == "leak-hand":
            for r in found:
                r4w = r.get("r4w", {})
                dof = r4w.get("distribution_dof", 0)
                p = chi2_sf(r4w.get("distribution_chi_milli", 0) / 1000, dof) if dof else 1.0
                require(p < HOMOGENEOUS_P, f"control {name}: the distribution test did not see it, p {p:.2g}")
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


def check_loadorder(directory):
    """Over deal seeds, natural variants must not be told apart, and a pick that reads load order must be."""
    records = lines(directory, "loadorder.jsonl")
    def found(line, plant, canonical=0):
        out = [r for r in records if r.get("loadorder") == line and r.get("plant") == plant and r.get("canonical_numbering") == canonical]
        require(len(out) == 1, f"load order {line} {plant}: {len(out)} results")
        return out[0] if out else {}
    for line in ("thestalos_viewer_hand", "thestalos_opposing_hand"):
        for plant in ("none", "first", "min2"):
            r = found(line, plant)
            where = f"load order {line} {plant}"
            require(r.get("variants", 0) >= 16 and r.get("variants_one_public_history") == r.get("variants"), f"{where}: {r.get('variants_one_public_history')} of {r.get('variants')} variants share a public history")
            zero(r, ("failed",), where)
            require(not r.get("error"), f"{where}: {r.get('error')}")
            require(r.get("deals") == r.get("variants", 0) * r.get("seeds_per_variant", -1), f"{where}: {r.get('deals')} deals")
            require(r.get("refused", 0) + r.get("none", 0) + r.get("reached", 0) == r.get("deals", -1), f"{where}: outcomes add to {r.get('refused', 0) + r.get('none', 0) + r.get('reached', 0)} of {r.get('deals')}")
            require(r.get("reached", 0) >= 100 * r.get("variants", 0), f"{where}: only {r.get('reached')} worlds reached the pick")
            rows = r.get("by_sequence", [])
            require(len(rows) == r.get("variants_one_public_history"), f"{where}: {len(rows)} rows")
            require(sum(sum(row[2:]) for row in rows) == r.get("reached", -1), f"{where}: table holds {sum(sum(row[2:]) for row in rows)} of {r.get('reached')} picks")
            code_rows = r.get("by_code", [])
            require(sum(sum(row[2:]) for row in code_rows) == r.get("reached", -1), f"{where}: code table holds {sum(sum(row[2:]) for row in code_rows)} of {r.get('reached')} picks")
            if plant == "none":
                for name, table in (("place in hand", rows), ("card and Life Points", code_rows)):
                    chi, dof, p = homogeneity(table)
                    require(dof > 0, f"{where} {name}: nothing to test")
                    require(p >= HOMOGENEOUS_P, f"{where} {name}: variants differ, chi-square {chi:.1f} on {dof}, p {p:.2g}")
        natural = found(line, "none")
        first = found(line, "first")
        # The fact the contract is written around. If this stops being true, the doc is stale.
        require(natural.get("paired_different", 0) > 0, f"load order {line}: one seed always gave one result, so the doc is stale")
        chi, dof, p = homogeneity(first.get("by_sequence", []))
        require(p < PLANT_P, f"load order {line}: a first-card pick was not seen, p {p:.2g}")


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
    check_loadorder(directory)
    check_void(directory)
    check_text(directory)
    for message in failures:
        print("FAIL:", message)
    print(json.dumps({"checked": str(directory), "failures": len(failures)}))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
