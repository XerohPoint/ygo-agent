#!/usr/bin/env python3
"""Build the per-card table and summary evidence from the probe, static scan and soak state."""
import csv, gzip, json, re, sys
from collections import Counter, defaultdict
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import *
import static_features
from gauntlet_cards import ERRATA_35

REPORT = ROOT / "tests/edison/reports/pool-audit-2026-11-04"

def kind_of(t):
    if t & TYPE_SPELL: return "spell"
    if t & TYPE_TRAP: return "trap"
    if t & TYPE_FUSION: return "fusion"
    if t & TYPE_SYNCHRO: return "synchro"
    if t & TYPE_RITUAL: return "ritual"
    return "normal_monster" if t & TYPE_NORMAL else "effect_monster"

def main():
    REPORT.mkdir(parents=True, exist_ok=True)
    entries, by_id, manifest = load_pool()
    state = json.loads((OUT / "soak/state.json").read_text())
    probe = {}
    for line in (OUT / "script_probe.jsonl").read_text().splitlines():
        row = json.loads(line); probe[row["code"]] = row
    table, _ = static_features.features()
    overrides = historical_overrides()
    errata = {n.lower() for n in ERRATA_35}
    signals = {r["code"]: r for r in json.loads((OUT / "errata_signals.json").read_text())}
    gauntlet = {r["base"]: r for r in json.loads((OUT / "gauntlet_cards.json").read_text())}
    quarantine = set(json.loads((OUT / "quarantine.json").read_text()))
    attributed = defaultdict(Counter)
    for failure in state["failures"]:
        for code in failure.get("attributed") or []:
            if code in by_id:
                attributed[by_id[code]["base"]][failure.get("signature") or failure["kind"]] += 1
    tally = lambda key, entry: sum(state[key].get(str(c), 0) for c in entry["ids"])
    chosen = Counter()
    for key, n in state["chosen"].items():
        code, act = key.split(":")
        if int(code) in by_id: chosen[(by_id[int(code)]["base"], act)] += n
    rows, stats = [], Counter()
    lua_by_script = Counter()
    for text, n in state["lua"].items():
        m = re.search(r"c(\d+)\.lua", text)
        if m: lua_by_script[int(m.group(1))] += n
    for e in entries:
        row0 = next(r for r in e["rows"] if r["id"] == e["base"])
        t = row0["type"]
        needs = needs_script(t)
        base_script = (SCRIPTS / f"c{e['base']}.lua").exists()
        entry_script = (SCRIPTS / f"c{e['entry_code']}.lua").exists()
        diags = sum(len(probe[c]["diagnostics"]) for c in e["ids"] if c in probe)
        features = table[e["base"]]
        activatable = bool(needs and features["activated_effect"])
        in_deck, offered = tally("in_deck", e), tally("offered", e)
        chain, summon, sp, flip = tally("chain", e), tally("summon", e), tally("spsummon", e), tally("flip", e)
        conf = state["confirm"].get(str(e["base"]))
        if e["limit"] == 0: status = "forbidden_no_production_duel"
        elif chain: status = "activated"
        elif summon or sp or flip: status = "summoned_not_activated" if activatable else "summoned"
        elif offered: status = "offered_only"
        elif in_deck: status = "in_deck_only"
        else: status = "never_in_deck"
        rows.append({"code": e["base"], "accepted_ids": "|".join(map(str, e["ids"])), "name": e["name"], "kind": kind_of(t), "limit": e["limit"],
            "needs_script": int(needs), "script_at_manifest_code": int(entry_script), "script_after_alias": int(base_script),
            "probe_loaded_clean": int(diags == 0), "probe_diagnostics": diags, "soak_lua_diagnostics": lua_by_script.get(e["base"], 0),
            "has_activated_effect": int(activatable), "historical_override": int(e["base"] in overrides),
            "in_errata_35": int(e["name"].lower() in errata),
            "interface_features": "|".join(features["features"] + [f"sysstr{x}" for x in features["system_strings"]]),
            "quarantined": int(e["base"] in quarantine), "soak_decks": in_deck, "offered": offered, "chosen_activate": chosen[(e["base"], "activate")],
            "chains": chain, "normal_summons": summon, "special_summons": sp, "flip_summons": flip, "status": status,
            "crash_attributions": sum(attributed[e["base"]].values()),
            "confirm_ok": conf["ok"] if conf else "", "confirm_crash": sum(conf["crash"].values()) if conf else "",
            "confirm_signature": next(iter(sorted(conf["crash"], key=conf["crash"].get, reverse=True)), "") if conf else "",
            "gauntlet_decks": gauntlet.get(e["base"], {}).get("decks", 0),
            "errata_signal": "|".join(k for k, v in (("hard_opt", signals.get(e["base"], {}).get("hard_opt_script")),
                ("only_text", signals.get(e["base"], {}).get("only_text")), ("errata_commit", signals.get(e["base"], {}).get("errata_commits"))) if v)})
    REPORT_CSV = REPORT / "per-card.csv.gz"
    with gzip.open(REPORT_CSV, "wt", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader(); writer.writerows(rows)
    playable = [r for r in rows if r["limit"] > 0]
    pct = lambda a, b: round(100 * a / b, 2) if b else None
    summary = {
        "entries": len(rows), "accepted_ids": sum(len(r["accepted_ids"].split("|")) for r in rows),
        "forbidden": len(rows) - len(playable), "playable": len(playable),
        "needs_script": sum(r["needs_script"] for r in rows), "no_script_needed_normal": sum(not r["needs_script"] for r in rows),
        "file_at_manifest_code": sum(r["script_at_manifest_code"] for r in rows),
        "non_normal_missing_at_manifest_code": sum(r["needs_script"] and not r["script_at_manifest_code"] for r in rows),
        "non_normal_missing_after_alias": sum(r["needs_script"] and not r["script_after_alias"] for r in rows),
        "probe_entries_with_diagnostics": sum(r["probe_diagnostics"] > 0 for r in rows),
        "soak": {k: state[k] for k in ("statuses", "total_steps", "max_options", "violations", "stage_stats")},
        "soak_lua_distinct": len(state["lua"]),
        "status_counts": Counter(r["status"] for r in rows),
        "in_a_soak_deck": sum(r["soak_decks"] > 0 for r in playable),
        "offered": sum(r["offered"] > 0 for r in playable),
        "activated_among_activatable": [sum(r["chains"] > 0 for r in playable if r["has_activated_effect"]), sum(r["has_activated_effect"] for r in playable)],
        "normal_summoned_or_chain": sum(r["status"] in ("activated", "summoned", "summoned_not_activated") for r in playable),
    }
    summary["pct_activated_among_activatable"] = pct(*summary["activated_among_activatable"])
    summary["pct_exercised_of_playable"] = pct(summary["normal_summoned_or_chain"], len(playable))
    summary["pct_exercised_of_all"] = pct(summary["normal_summoned_or_chain"], len(rows))
    # Crash classes with their stage-level counts.
    classes, class_cards = Counter(), defaultdict(Counter)
    for failure in state["failures"]:
        sig = re.sub(r"exit-6: what\(\):\s*", "", failure.get("signature") or failure["kind"])
        classes[sig] += 1
        for code in failure.get("attributed") or []:
            if code in by_id: class_cards[sig][by_id[code]["name"]] += 1
    summary["crash_classes"] = [{"signature": s, "count": n, "cards": class_cards[s].most_common(12)} for s, n in classes.most_common()]
    summary["confirm"] = {by_id[int(k)]["name"]: v for k, v in state["confirm"].items()}
    summary["gauntlet"] = state.get("gauntlet", {})
    (REPORT / "summary.json").write_text(json.dumps(summary, indent=1))
    never = [{"code": r["code"], "name": r["name"], "kind": r["kind"], "status": r["status"], "offered": r["offered"], "chains": r["chains"]}
             for r in playable if r["status"] in ("never_in_deck", "in_deck_only", "offered_only", "summoned_not_activated")]
    with gzip.open(REPORT / "unexercised.json.gz", "wt") as handle:
        json.dump(never, handle)
    print(json.dumps({k: v for k, v in summary.items() if k not in ("crash_classes", "confirm", "gauntlet", "soak")}, indent=0))
    print(summary["soak"]["statuses"], summary["soak"]["total_steps"], summary["soak"]["max_options"], summary["soak"]["violations"])
    print(Counter(r["kind"] for r in never), Counter(r["status"] for r in never))

if __name__ == "__main__":
    main()
