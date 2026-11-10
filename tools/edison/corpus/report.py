#!/usr/bin/env python3
"""Write tests/edison/reports/corpus-2026-11-09/summary.json from the manifest and soak outputs.

Counts are of distinct lists unless a field says appearances. Nothing here plays a duel.
"""
import argparse, json, re, sys
from collections import Counter, defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from corpus_common import *  # noqa: E402


GOTTOMS = {52352005}


def band(d):
    return "0" if d == 0 else ("1-9" if d <= 9 else "10+")


def first_date(row):
    return min(a["event_date"] for a in row["appearances"])


def crash_cards(crash, list_cards):
    """Cards the crash points at: codes in the error text, then the last card chosen."""
    text_codes = [int(c) for c in re.findall(r"\b(\d{5,9})\b", crash.get("text") or "")]
    last = [s["c"] for s in reversed(crash.get("last") or []) if s.get("c")]
    activated = [s["c"] for s in reversed(crash.get("last") or []) if s.get("c") and s.get("a") == 8]
    involved = set().union(*(list_cards.get(n, set()) for n in crash["lists"]))
    return {"text": [c for c in text_codes if c in involved or not involved],
            "last_chosen": last[:1], "last_activated": activated[:1]}


def soak_section(directory, list_cards, names, zone_cards):
    summary = load_json(directory / "summary.json")
    # Cards a player chose in an action row (summon, set, activate, attack...). Not a rules check.
    chosen = load_json(directory / "chosen.json")
    picked = {int(k.split(":")[0]) for k in chosen}
    activated = {int(k.split(":")[0]) for k in chosen if k.endswith(":activate")}
    playable = zone_cards["main_extra" if summary["stage"] == "main" else "all"]
    never = sorted(playable - picked)
    crashes = load_json(directory / "crashes.json")
    diagnostics = load_json(directory / "diagnostics.json")
    non_ok = load_json(directory / "non_ok.json")
    classes = defaultdict(lambda: {"duels": 0, "cards": Counter(), "lists": Counter(), "policies": Counter(), "seeds": []})
    for crash in crashes:
        row = classes[crash["signature"]]
        row["duels"] += 1; row["policies"][crash["policy"]] += 1
        row["seeds"].append(crash["seed"])
        for name in crash["lists"]: row["lists"][name] += 1
        cards = crash_cards(crash, list_cards)
        for code in set(cards["text"] + cards["last_chosen"] + cards["last_activated"]):
            row["cards"][code] += 1
    lines = Counter(re.sub(r"[0-9]{5,}", "N", d["line"]) for d in diagnostics)
    return {"seeds": [summary["first_seed"], summary["last_seed"]], "lists": summary["lists"],
            "planned": summary["planned"], "finished": summary["finished"], "decisions": summary["decisions"],
            "statuses": summary["statuses"], "by_policy": summary["by_policy"], "crashes": summary["crashes"],
            "diagnostic_lines": summary["diagnostic_lines"],
            "diagnostic_lines_outside_crashed_duels": summary.get("diagnostic_lines_outside_crashed_duels"),
            "diagnostic_classes": dict(lines.most_common()),
            "contract_violations": summary["contract_violations"], "max_options": summary["max_options"],
            "max_repeat": summary["max_repeat"], "elapsed_seconds": summary["elapsed_seconds"],
            "non_ok_duels": non_ok,
            "card_choice_coverage": {"distinct_cards_in_played_zones": len(playable), "chosen_in_an_action": len(playable & picked),
                                     "activated": len(playable & activated),
                                     "never_chosen": [{"code": c, "name": names.get(c, "?")} for c in never]},
            "crash_classes": [{"signature": sig, "duels": row["duels"], "policies": dict(row["policies"]),
                               "seeds": sorted(row["seeds"])[:20],
                               "cards": [{"code": c, "name": names.get(c, "?"), "duels": n} for c, n in row["cards"].most_common(10)],
                               "lists": dict(row["lists"].most_common(10))}
                              for sig, row in sorted(classes.items(), key=lambda kv: -kv[1]["duels"])]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--soak", type=Path, nargs="*", default=[], help="Soak output directories, in report order")
    parser.add_argument("--controls", type=Path, nargs="*", default=[], help="Pilot and negative-control soak directories")
    parser.add_argument("--probes", type=Path, nargs="*", default=[], help="probe_targets.py output directories")
    parser.add_argument("--extra", type=Path, help="JSON merged into the summary (notes)")
    args = parser.parse_args()
    manifest = load_json(DEST / "manifest.json")
    rows = manifest["lists"]
    names = {}
    list_cards, zone_cards = {}, {"main_extra": set(), "all": set()}
    for row in rows:
        zones = parse_ydk_text((DEST / row["ydk"]).read_text())
        list_cards[row["list_id"]] = {c for z in zones.values() for c in z}
        if row["production_legal"]:
            zone_cards["main_extra"] |= set(zones["main"]) | set(zones["extra"])
            zone_cards["all"] |= list_cards[row["list_id"]]
        for item in row["outside_v1"] + row["untrained_main_extra"] + row["outside_pool"]:
            names[item["code"]] = item["name"]
    import sqlite3
    db = sqlite3.connect(ROOT / "assets/locale/en/cards.cdb")
    names.update({r[0]: r[1] for r in db.execute("select id,name from texts")})

    apps = [a for r in rows for a in r["appearances"]]
    by_arch = defaultdict(Counter)
    for row in rows:
        c = by_arch[row["archetype"]]
        c["lists"] += 1; c["appearances"] += len(row["appearances"])
        c["production_legal"] += row["production_legal"]
        c["inside_v1_main_extra"] += row["inside_v1_main_extra"]
        c["in_training"] += row["in_training"]
        held = row["inside_v1_main_extra"] and not row["in_training"]
        c["held_out_candidates_inside_v1"] += held
        c["held_out_inside_v1_no_untrained"] += held and not row["untrained_main_extra"]
    held_rows = [r for r in rows if r["inside_v1_main_extra"] and not r["in_training"]]
    no_untrained = [r for r in rows if r["inside_v1_main_extra"] and not r["untrained_main_extra"]]
    outside_any, outside_me, blockers = Counter(), Counter(), Counter()
    for row in rows:
        codes = {i["code"] for i in row["outside_v1"]}
        me_codes = {i["code"] for i in row["outside_v1"] if i["zone"] in ("main", "extra")}
        outside_any.update(codes); outside_me.update(me_codes)
        blocking = me_codes - GOTTOMS  # The Gottoms rule removes it, so it blocks nothing.
        if row["production_legal"] and len(blocking) == 1:
            blockers.update(blocking)
    reasons = Counter(re.sub(r"[0-9]{5,}", "N", (r["legality_reason"] or "").split(", file:")[0]) for r in rows if not r["production_legal"])
    exclusions = manifest["exclusions"]
    summary = {
        "date": "2026-11-09", "base_commit": "6752ce9", "inputs": manifest["inputs"], "retrieved": manifest["retrieved"],
        "privacy": manifest["privacy"],
        "sources": {"events_in_gallery": manifest["events_in_gallery"],
                    "events_excluded": sum(e["level"] == "event" for e in exclusions),
                    "events_used": len({a["event"] for a in apps}),
                    "events_without_published_decks": manifest["events_without_decks"],
                    "deck_exclusions": sum(e["level"] == "deck" for e in exclusions),
                    "appearances": len(apps), "distinct_lists": len(rows),
                    "lists_with_more_than_one_appearance": sum(len(r["appearances"]) > 1 for r in rows),
                    "lists_with_conflicting_labels": sum(bool(r["archetype_labels_all"]) for r in rows),
                    "ydk_card_array_mismatches": manifest.get("ydk_card_array_mismatches"),
                    "deck_event_mismatches": manifest.get("deck_event_mismatches")},
        "exclusions": {"by_reason": dict(Counter(e["reason"] for e in exclusions)), "rows": exclusions},
        "counts_by_archetype": {k: dict(v) for k, v in sorted(by_arch.items(), key=lambda kv: (-kv[1]["lists"], kv[0]))},
        "lists_by_first_event_year": dict(sorted(Counter(first_date(r)[:4] for r in rows).items())),
        "appearances_by_event_year": dict(sorted(Counter(a["event_date"][:4] for a in apps).items())),
        "held_out_candidates_by_first_event_year": dict(sorted(Counter(first_date(r)[:4] for r in held_rows).items())),
        "appearances_by_ruleset": dict(Counter(a["ruleset"] for a in apps)),
        "legality": {"production_legal": sum(r["production_legal"] for r in rows),
                     "production_illegal": sum(not r["production_legal"] for r in rows),
                     "failure_reasons": dict(reasons.most_common()),
                     "failures": [{"list_id": r["list_id"], "archetype": r["archetype"], "reason": r["legality_reason"],
                                   "outside_pool": r["outside_pool"], "over_limit": r["over_limit"], "sizes": r["sizes"]}
                                  for r in rows if not r["production_legal"]],
                     "outside_pool_cards_by_lists": [{"code": c, "name": names.get(c, "?"), "lists": n} for c, n in Counter(
                         c for r in rows for c in {x["code"] for x in r["outside_pool"]}).most_common()]},
        "code_list_v1": {"inside_v1_main_extra": sum(r["inside_v1_main_extra"] for r in rows),
                         "inside_v1_strict": sum(r["inside_v1_strict"] for r in rows),
                         "inside_v1_main_extra_and_side": sum(r["inside_v1_main_extra"] and r["inside_v1_side"] for r in rows),
                         "inside_only_after_gottoms_rule": sum(r["inside_v1_main_extra"] and bool(r["gottoms_rule_removed"]) for r in rows),
                         "held_out_candidates_inside_v1": len(held_rows),
                         "top_cards_outside_v1": [{"code": c, "name": names.get(c, "?"), "lists_any_zone": n,
                                                   "lists_main_extra": outside_me.get(c, 0),
                                                   **({"removed_by_gottoms_rule": True} if c in GOTTOMS else {})}
                                                  for c, n in outside_any.most_common(40)],
                         "sole_main_extra_blocker_of_legal_lists": [{"code": c, "name": names.get(c, "?"), "lists": n}
                                                                    for c, n in blockers.most_common(20)]},
        "untrained": {
            "inside_v1_no_untrained_main_extra": len(no_untrained),
            "inside_v1_no_untrained_not_in_training": sum(not r["in_training"] for r in no_untrained),
            "by_archetype": {k: dict(v) for k, v in sorted(
                ((a, Counter(band(r["nearest_training"]["distance"]) for r in no_untrained if r["archetype"] == a))
                 for a in {r["archetype"] for r in no_untrained}), key=lambda kv: (-sum(kv[1].values()), kv[0]))},
            "by_distance_band": dict(Counter(band(r["nearest_training"]["distance"]) for r in no_untrained)),
            "by_distance_band_not_in_training": dict(Counter(band(r["nearest_training"]["distance"]) for r in no_untrained if not r["in_training"])),
            "held_out_inside_v1_by_untrained_count": dict(sorted(Counter(min(len(r["untrained_main_extra"]), 5) for r in held_rows).items())),
            "top_untrained_cards_in_held_out_inside_v1": [{"code": c, "name": names.get(c, "?"), "lists": n} for c, n in Counter(
                i["code"] for r in held_rows for i in r["untrained_main_extra"]).most_common(25)]},
        "soak": [{"output": str(d.relative_to(ROOT)) if d.is_relative_to(ROOT) else str(d), **soak_section(d, list_cards, names, zone_cards)} for d in args.soak],
    }
    summary["controls"] = []
    for d in args.controls:
        s = load_json(d / "summary.json")
        summary["controls"].append({"name": d.name, "seeds": [s["first_seed"], s["last_seed"]], "stage": s["stage"], "lists": s["lists"],
                                    "planned": s["planned"], "finished": s["finished"], "statuses": s["statuses"],
                                    "crashes": s["crashes"], "crash_signatures": s["crash_signatures"],
                                    "diagnostic_lines": s["diagnostic_lines"], "elapsed_seconds": s["elapsed_seconds"]})
    summary["probes"] = [{"name": d.name, **load_json(d / "summary.json")} for d in args.probes]
    if args.extra:
        summary.update(load_json(args.extra))
    REPORT.mkdir(parents=True, exist_ok=True)
    (REPORT / "summary.json").write_text(json.dumps(summary, indent=1) + "\n")
    print(json.dumps({k: v for k, v in summary.items() if k in ("sources", "code_list_v1", "untrained")}, indent=1)[:6000])


if __name__ == "__main__":
    main()
