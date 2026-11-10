#!/usr/bin/env python3
"""Build the M3 deck corpus from cached Format Library responses. Makes no web request.

For each distinct list (Main, Extra and Side compared as multisets) it writes one .ydk under
assets/edison/corpus/ydk/ and one manifest row. Card lines are copied from the source's
"ydk" field unedited and in source order. Only the source's header line is replaced,
because it can hold a player name. No player name, handle or user id is written.

Legality runs each file alone through the unmodified production init_module, with the
child program validate_gauntlet.py uses. Coverage follows tools/edison/learner/decks.py.
"""
import argparse, hashlib, json, re, subprocess, sys
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "pool_audit"))
sys.path.insert(0, str(HERE.parent / "learner"))
from corpus_common import *  # noqa: E402
import common as audit_common  # noqa: E402
from validate_gauntlet import CHILD  # noqa: E402  (the same child program, unchanged)
import decks as learner_decks  # noqa: E402

MODULE = ROOT / "ygoenv/ygoenv/ygopro/ygopro_ygoenv.cpython-310-x86_64-linux-gnu.so"
CERTIFIED = "b8fcd2bcf7774ae4edf8a4a800c2c55c006f62cb5cc1ef925fe316365a1ddf6f"


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def plain(name):
    return re.sub(r"[^0-9a-z]", "", (name or "").casefold())


def collect(gallery, names):
    """Return (appearances, exclusions, empty events) from the raw cache.

    The source's card arrays use artwork ids that can differ from the ydk passcode (Foolish
    Burial 81439174 against 81439173), so the ydk is cross-checked against them by card name.
    """
    appearances, exclusions, empty, seen = [], [], [], set()
    failures = RAW / "fetch_failures.json"
    failed = {int(f["id"]): f["error"] for f in (load_json(failures) if failures.exists() else []) if f["kind"] == "deck"}
    events = sorted(gallery["events"], key=lambda e: ((e.get("endDate") or ""), e["abbreviation"]))
    for meta in events:
        abbr = meta["abbreviation"]
        base = {"event": abbr, "event_name": (meta.get("name") or "").strip(), "event_date": (meta.get("endDate") or "")[:10],
                "event_size": meta.get("size"), "community": meta.get("communityName")}
        reason = excluded_event(meta)
        if reason:
            exclusions.append({"level": "event", **base, "reason": reason})
            continue
        path = RAW / "events" / f"{abbr}.json"
        if not path.exists():
            exclusions.append({"level": "event", **base, "reason": "event response not fetched"})
            continue
        event = load_json(path)
        tops = event.get("topDecks") or []
        if not tops:
            empty.append(base)
        for top in tops:
            deck_id = int(top["id"])
            row = {"level": "deck", "deck_id": deck_id, **base}
            if deck_id in seen:
                exclusions.append({**row, "reason": "deck already listed under another event"})
                continue
            seen.add(deck_id)
            dpath = RAW / "decks" / f"{deck_id}.json"
            if not dpath.exists():
                error = failed.get(deck_id, "not fetched")
                exclusions.append({**row, "reason": f"deck response not fetched ({error}, on two attempts)"})
                continue
            deck = load_json(dpath)
            if (deck.get("formatName") or "") != "Edison":
                exclusions.append({**row, "reason": f"deck format is {deck.get('formatName')}"})
                continue
            zones = parse_ydk_text(deck.get("ydk") or "")
            if not zones["main"]:
                exclusions.append({**row, "reason": "source ydk has no Main Deck cards"})
                continue
            arrays = {z: Counter(plain(c.get("name")) for c in deck.get(z) or []) for z in ("main", "extra", "side")}
            placement = deck.get("placement", top.get("placement"))
            # Card and section lines exactly as the source has them (leading zeros kept), trimmed of surrounding spaces.
            card_lines = [l.strip() for l in (deck.get("ydk") or "").splitlines()
                          if re.fullmatch(r"\d+|#main|#extra|!side", l.strip())]
            appearances.append({**base, "deck_id": deck_id, "zones": zones, "card_lines": card_lines,
                                "placement": int(placement) if str(placement).isdigit() else placement,
                                "archetype_source": deck.get("deckTypeName") or top.get("deckTypeName"),
                                "category_source": deck.get("category"),
                                "ruleset": ruleset(meta),
                                "event_on_deck": deck.get("eventAbbreviation"),
                                "ydk_matches_card_arrays": all(Counter(plain(names.get(c)) for c in zones[z]) == arrays[z] for z in zones),
                                "api_url": f"https://formatlibrary.com/api/decks/{deck_id}",
                                "source_url": f"https://formatlibrary.com/decks/{deck_id}"})
    return appearances, exclusions, empty


def legality(path):
    """Production verdict for one file, exactly as validate_gauntlet.py obtains it."""
    code = CHILD % (str(ROOT), str(ROOT / "ygoenv"), str(audit_common.DB), str(audit_common.FULL_CODES))
    proc = subprocess.run([sys.executable, "-c", code, str(path)], cwd=ROOT / "scripts", capture_output=True, text=True)
    ok = "PRODUCTION_LEGAL" in proc.stdout
    lines = [l for l in proc.stderr.splitlines() if l.strip()]
    reason = "" if ok else (lines[-1][:300] if lines else f"exit {proc.returncode}")
    return ok, reason.replace(str(ROOT) + "/", "")


def write_ydk(path, list_id, rep, copies):
    lines = [f"#corpus {list_id}", f"#source {rep['source_url']}",
             f"#event {rep['event']} {rep['event_date']} placement {rep['placement']} of {rep['event_size']}",
             f"#archetype {rep['archetype_source']}", f"#ruleset {rep['ruleset']}",
             f"#appearances {copies} (all listed in manifest.json)", *rep["card_lines"]]
    if parse_ydk_text("\n".join(lines)) != rep["zones"]:
        raise ValueError(f"Copied card lines do not parse back to the source's zones: {list_id}")
    path.write_text("\n".join(lines) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workers", type=int, default=8)
    args = parser.parse_args()
    if sha(MODULE) != CERTIFIED:
        raise SystemExit("Production module is not the certified build")
    gallery = load_json(RAW / "events_gallery.json")
    db = audit_common.connect()
    names = {r[0]: r[1] for r in db.execute("select id,name from texts")}
    appearances, exclusions, empty = collect(gallery, names)

    # Training lists: the 18 gauntlet ids, and the 21 lists registry() builds.
    gauntlet = load_json(GAUNTLET / "manifest.json")["decks"]
    training_ids = {int(d["deck_id"]): d["name"] for d in gauntlet}
    training_sources = {multiset_key(parse_ydk_text((GAUNTLET / f"{d['name']}.ydk").read_text())): f"g_{d['name']}" for d in gauntlet}
    for name, path in learner_decks.FIXTURES.items():
        training_sources.setdefault(multiset_key(parse_ydk_text(Path(path).read_text())), name)
    registry = {name: parse_ydk_text(Path(path).read_text()) for name, path in sorted(learner_decks.registry().items())}
    v1_sha, v1 = learner_decks.load_codes()
    v1_codes = set(v1.values())
    in_registry = {c for zones in registry.values() for zone in zones.values() for c in zone}
    trained = {c for zones in registry.values() for c in main_extra(zones)} | (v1_codes - in_registry)
    registry_me = {name: main_extra(zones) for name, zones in registry.items()}
    pool = learner_decks.pool_ids()
    entries, by_id, _ = audit_common.load_pool()
    limit_of = {e["copy_key"]: e["limit"] for e in entries}
    card = lambda c: {"code": c, "name": names.get(c, "?")}

    groups = defaultdict(list)
    for app in appearances:
        groups[multiset_key(app["zones"])].append(app)
    YDK_DIR.mkdir(parents=True, exist_ok=True)
    for old in YDK_DIR.glob("*.ydk"):
        old.unlink()
    rows = []
    for key, apps in groups.items():
        apps.sort(key=lambda a: (a["deck_id"] not in training_ids, a["deck_id"]))
        rep = apps[0]
        list_id = f"fl{rep['deck_id']}"
        write_ydk(YDK_DIR / f"{list_id}.ydk", list_id, rep, len(apps))
        zones = rep["zones"]
        reasons = [f"gauntlet deck id {a['deck_id']} ({training_ids[a['deck_id']]})" for a in apps if a["deck_id"] in training_ids]
        if key in training_sources:
            reasons.append(f"identical to training list {training_sources[key]}")
        labels = sorted({a["archetype_source"] or "" for a in apps})
        rows.append({"list_id": list_id, "ydk": f"ydk/{list_id}.ydk", "zones": zones, "key": key,
                     "archetype_source": rep["archetype_source"], "archetype": normalize_label(rep["archetype_source"]),
                     "archetype_labels_all": labels if len(labels) > 1 else None,
                     "sizes": {z: len(v) for z, v in zones.items()},
                     "in_training": bool(reasons), "in_training_reason": reasons or None,
                     "appearances": [{k: a[k] for k in ("deck_id", "event", "event_name", "event_date", "event_size",
                                                        "community", "placement", "archetype_source", "category_source",
                                                        "ruleset", "ydk_matches_card_arrays", "source_url", "api_url")}
                                     for a in apps]})
    rows.sort(key=lambda r: int(r["list_id"][2:]))

    with ThreadPoolExecutor(args.workers) as ex:
        verdicts = list(ex.map(lambda r: legality(YDK_DIR / f"{r['list_id']}.ydk"), rows))
    for row, (ok, reason) in zip(rows, verdicts):
        zones = row.pop("zones"); row.pop("key")
        row["production_legal"], row["legality_reason"] = ok, reason or None
        # Card-level detail from the Python mirror; production's verdict above is the authority.
        counts, over = Counter(), []
        for zone in zones.values():
            for c in zone:
                entry = by_id.get(c)
                if entry: counts[entry["copy_key"]] += 1
        for key_code, n in sorted(counts.items()):
            if n > limit_of[key_code]:
                over.append({**card(key_code), "copies": n, "limit": limit_of[key_code]})
        not_pool = Counter((c, z) for z, codes in zones.items() for c in codes if c not in pool)
        row["outside_pool"] = [{**card(c), "zone": z, "copies": n} for (c, z), n in sorted(not_pool.items())]
        row["over_limit"] = over
        outside = Counter((c, z) for z, codes in zones.items() for c in codes if c not in v1_codes)
        row["outside_v1"] = [{**card(c), "zone": z, "copies": n} for (c, z), n in sorted(outside.items())]
        # decks.py rule: drop cards outside the pool and XX-Saber Gottoms, then play the rest.
        played = {z: [c for c in codes if c in pool and c not in learner_decks.OUTSIDE_V1] for z, codes in zones.items()}
        removed = Counter(c for z in zones for c in zones[z] if c in learner_decks.OUTSIDE_V1)
        row["gottoms_rule_removed"] = [{**card(c), "copies": n} for c, n in sorted(removed.items())]
        me_inside = all(c in v1_codes for z in ("main", "extra") for c in played[z])
        row["inside_v1_main_extra"] = bool(ok and me_inside and len(played["main"]) == len(zones["main"]))
        row["inside_v1_side"] = all(c in v1_codes for c in played["side"])
        row["inside_v1_strict"] = bool(ok) and not outside
        me = main_extra(played)
        untrained = Counter({c: n for c, n in me.items() if c not in trained})
        row["untrained_main_extra"] = [{**card(c), "copies": n} for c, n in sorted(untrained.items())]
        best = min(registry_me, key=lambda name: (distance(me, registry_me[name]), name))
        row["nearest_training"] = {"name": best, "distance": distance(me, registry_me[best])}
    manifest = {
        "purpose": "M3 corpus of published Edison tournament lists for a held-out split. Placements, not a strength ranking.",
        "source": "https://formatlibrary.com/api/ (events/gallery/edison, events/<abbr>, decks/<id>) only",
        "retrieved": {"first_utc": None, "last_utc": None},
        "privacy": "No player names, handles, Discord ids or user ids. Raw responses stay in build/edison-corpus/raw/.",
        "definitions": {
            "distinct list": "Main, Extra and Side identical as multisets of passcodes; every source appearance is kept",
            "list_id": "fl + the lowest training deck id, else the lowest deck id among the appearances; the .ydk uses that appearance's card order",
            "archetype": "the source's deckTypeName, casefolded, '&' to 'and', punctuation dropped, words joined by '_'; nothing merged by meaning",
            "production_legal": "the file alone passes the unmodified production init_module with the audit's full code list (validate_gauntlet.py path)",
            "outside_v1": "cards in any zone absent from tools/edison/learner/codes_v1.txt",
            "gottoms_rule": "decks.py registry(): drop cards outside the legal pool and XX-Saber Gottoms (OUTSIDE_V1), play the rest",
            "inside_v1_main_extra": "production legal, and after the Gottoms rule every Main and Extra card is in code list v1; what today's networks can load. The env stores only Main and Extra",
            "inside_v1_side": "after the Gottoms rule every Side card is in v1; matters only for siding",
            "inside_v1_strict": "production legal and no card in any zone outside v1, with nothing removed",
            "untrained_main_extra": "after the Gottoms rule, Main and Extra cards found in no Main or Extra of the 21 registry() training lists; v1 tokens and closure cards (v1 codes in no zone of those lists) count as trained, a card seen only in a training Side does not",
            "nearest_training": "registry() training list with the fewest Main+Extra card differences after the Gottoms rule, counted with multiplicity as the sum of per-passcode count differences; ties go to the lowest sorted name",
            "in_training": "an appearance has one of the 18 gauntlet deck ids, or the list is identical to a gauntlet source or fixture"},
        "inputs": {"production_module_sha256": sha(MODULE), "full_code_list_sha256": sha(audit_common.FULL_CODES),
                   "code_list_v1_sha256": v1_sha, "legality_sha256": sha(audit_common.LEGALITY),
                   "training_lists": sorted(registry)},
        "events_in_gallery": len(gallery["events"]),
        "events_without_decks": [e["event"] for e in empty],
        "deck_event_mismatches": sorted(a["deck_id"] for a in appearances if a["event_on_deck"] != a["event"]),
        "ydk_card_array_mismatches": sorted(a["deck_id"] for a in appearances if not a["ydk_matches_card_arrays"]),
        "lists": rows, "exclusions": exclusions}
    log = RAW / "fetch_log.jsonl"
    stamps = [json.loads(l)["utc"] for l in log.read_text().splitlines()] if log.exists() else []
    seeded = load_json(RAW / "seeded.json") if (RAW / "seeded.json").exists() else []
    stamps += [s["fetched_utc"] for s in seeded]
    manifest["retrieved"] = {"first_utc": min(stamps) if stamps else None, "last_utc": max(stamps) if stamps else None,
                             "responses_from_pool_audit_cache": len(seeded), "responses_fetched_now": len(stamps) - len(seeded)}
    DEST.mkdir(parents=True, exist_ok=True)
    (DEST / "manifest.json").write_text(json.dumps(manifest, indent=1) + "\n")
    print(len(appearances), "appearances", len(rows), "distinct lists", sum(r["production_legal"] for r in rows), "legal",
          sum(r["inside_v1_main_extra"] for r in rows), "inside v1", len(exclusions), "exclusions")


if __name__ == "__main__":
    main()
