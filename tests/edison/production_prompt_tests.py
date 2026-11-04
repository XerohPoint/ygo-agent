#!/usr/bin/env python3
"""Production regression for prompts that used to abort the process: one case per class.

Each case plays seeded duels from a small legal deck built around one card, through production
init_module and env.step, until the row or card the class depends on has been seen. Every
decision also passes the worker's observation contract and is scored by the heuristic, context
and saved learner policies, which must return a legal row. A worker that prints a native
diagnostic, aborts, or never reaches its evidence fails the case.
"""
import argparse
import json
from pathlib import Path
import random
import re
import sqlite3
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.append(str(ROOT))
sys.path.insert(0, str(ROOT / "ygoenv"))
sys.path.insert(0, str(ROOT / "tools/edison/pool_audit"))
DB = ROOT / "assets/locale/en/cards.cdb"
MARKERS = ("timeout", "Script not found", "Card not found", "Spec not found",
           "cannot find id", "MSG_RETRY", "Error", "stack traceback")
SEED = 74900000
MAX_OPTIONS = 256
MAX_DUELS = 24
# Column 3 of an action row, in _msgs order.
OPTION, TRIBUTE, YESNO, DISFIELD, ATTRIB, NUMBER, CARD, RACE = 10, 4, 7, 13, 14, 15, 16, 17
ACT_SET, ACT_SPSUMMON, ACT_SUMMON, ACT_MSET, ACT_ACTIVATE = 1, 3, 4, 5, 8

# name: (cards at three copies, evidence, seats that use the deck)
CASES = {
    "A": ("TOSS COIN", ["Time Wizard"], "activate"),
    "B": ("TOSS DICE, SIDE DECK CARD", ["Snipe Hunter"], "activate"),
    "C": ("SPIRIT RETURN, SIDE DECK CARD", ["Dark Dust Spirit"], "effect:1104"),
    "D": ("SPIRIT RETURN, ERRATA CARD", ["Susa Soldier"], "effect:1104"),
    "E": ("FIELD DISABLED AND TWO ZONES", ["Ground Collapse"], "disfield2"),
    "F": ("DECLARE A CARD BY NAME", ["Mind Crush"], "declare"),
    "G": ("DECLARE A MONSTER", ["Question"], "declare"),
    "H": ("CALL A COIN", ["Time Wizard"], "effect:60,61"),
    "I": ("DECLARE A TYPE", ["DNA Surgery"], "race"),
    "J": ("DECLARE A CARD KIND", ["Reversal Quiz"], "effect:70,71,72"),
    "K": ("NUMBER OVER 12, ERRATA CARD", ["Mausoleum of the Emperor", "Blue-Eyes White Dragon"], "number:1000,2000"),
    "L": ("DOUBLE TRIBUTE", ["Kaiser Sea Horse", "Blue-Eyes White Dragon"], "double_tribute"),
    "M": ("DECK TOP AND REVERSED DECK", ["Convulsion of Nature"], "activate"),
    "N": ("TWO ATTRIBUTES", ["DNA Checkup"], "attrib2"),
    "O": ("CANCEL TARGET", ["Seal of Wickedness"], "activate"),
    "P": ("YES OR NO DESCRIPTIONS", ["Magic Drain", "Dark Hole", "Pot of Avarice", "Mystical Space Typhoon"], "effect:1213,1214"),
    "Q": ("SAME DECLARATION WITH ANOTHER DECK", ["Mind Crush", "Dark Hole"], "declare"),
}

def code_of(db, name):
    row = db.execute("select d.id from datas d join texts t using(id) where t.name=? and d.alias=0", (name,)).fetchone()
    assert row, f"Unknown card {name}"
    return row[0]

def code_list(path):
    """Legal pool plus what its scripts can request; see tools/edison/pool_audit/build_codelist.py."""
    db = sqlite3.connect(DB)
    rows = {r[0]: r for r in db.execute("select id,alias,type from datas")}
    manifest = json.loads((ROOT / "assets/edison/legality.json").read_text())
    pool = {code for card in manifest["cards"] for code in card["accepted_ids"]}
    closure = set(pool) | {code for code, row in rows.items() if row[2] & 0x4000}
    for code in pool:
        alias = rows[code][1]
        if alias:
            closure.add(alias)
        script = ROOT / "scripts/script" / f"c{alias if alias and abs(alias - code) < 20 else code}.lua"
        if script.exists():
            closure |= {int(n) for n in re.findall(r"\b(\d{5,9})\b", script.read_text(errors="replace"))
                        if int(n) in rows}
    lines = [f"{code} {0 if rows[code][2] & 0x10 and not rows[code][2] & 0x1000000 else 1}" for code in sorted(closure)]
    path.write_text("\n".join(lines) + "\n")

def write_deck(db, path, cards):
    manifest = json.loads((ROOT / "assets/edison/legality.json").read_text())
    pool = {code for card in manifest["cards"] for code in card["accepted_ids"]}
    limited = {card["code"]: card["limit"] for card in manifest["restrictions"]}
    main = []
    for name in cards:
        code = code_of(db, name)
        main += [code] * limited.get(code, 3)
    filler = [row[0] for row in db.execute(
        "SELECT id FROM datas WHERE type=17 AND alias=0 AND level<=4 AND atk>=1000 ORDER BY id")
        if row[0] in pool and row[0] not in limited]
    main += filler[:40 - len(main)]
    assert len(main) == 40
    path.write_text("#main\n" + "\n".join(map(str, main)) + "\n#extra\n!side\n")

def system_ids():
    """Prompt description to effect-column value, read from the header it is defined in."""
    text = (ROOT / "ygoenv/ygoenv/ygopro/ygopro.h").read_text()
    first = text[text.index("system_strings = {"):]
    first = first[:first.index("};")]
    second = text[text.index("added_system_strings = {"):]
    second = second[:second.index("};")]
    old = sorted({int(x) for x in re.findall(r"\{(\d+),\s*\"", first)})
    new = sorted({int(x) for x in re.findall(r"\{(\d+),\s*\"", second)})
    ids = {desc: 16 + i for i, desc in enumerate(old)}
    ids.update({desc: 16 + len(old) + i for i, desc in enumerate(new)})
    return ids

def worker(label, codes, deck):
    import numpy as np
    import ygoenv
    from ygoenv.ygopro import init_module
    from soak_worker import check_contracts
    name, cards, evidence = CASES[label]
    init_module(str(DB), str(codes), {"deck": str(deck)})
    id_to_code = {i + 1: int(line.split()[0]) for i, line in enumerate(codes.read_text().splitlines())}
    db = sqlite3.connect(DB)
    targets = {code_of(db, card) for card in cards}
    effects = system_ids()
    wanted = None
    if evidence.startswith("effect:"):
        wanted = {effects[int(x)] for x in evidence.split(":")[1].split(",")}
    numbers = {int(x) for x in evidence.split(":")[1].split(",")} if evidence.startswith("number:") else set()
    from ygoai.edison_policy import Heuristic, ContextHeuristic
    from ygoai.edison_rl import RLModel, RLAgent
    metadata = {}
    for cid, code in id_to_code.items():
        name, atk, defense, level = db.execute(
            "select name,atk,def,level from datas join texts using(id) where id=?", (code,)).fetchone()
        metadata[cid] = {"code": code, "name": name, "atk": atk, "def": defense, "level": level & 255}
    model = RLModel.load(ROOT / "tests/edison/reports/rl-budget-2026-11-02/seed-1702/final-policy.json")
    policies = [Heuristic(metadata, SEED), ContextHeuristic(metadata, SEED + 1), RLAgent(metadata, SEED + 2, model)]
    seen = set()
    finished = decisions = 0
    declare_pages = []
    for duel in range(MAX_DUELS):
        seed = SEED + (ord(label) - 65) * 100 + duel
        env = ygoenv.make("YGOPro-v1", env_type="gymnasium", num_envs=1, num_threads=1, seed=seed, deck1="deck", deck2="deck",
                          play_mode="self", player=-1, async_reset=False, greedy_reward=False, oppo_info=False,
                          max_options=MAX_OPTIONS, max_steps=10000, max_episode_steps=10000, timeout=10, verbose=False)
        rng = random.Random(seed ^ 0xED150)
        obs, info = env.reset()
        previous = None
        while True:
            count = int(info["num_options"][0])
            assert 0 < count <= MAX_OPTIONS and not info["action_overflow"][0], "action overflow"
            offered = obs["actions_"][0, :count]
            problem = check_contracts(obs, 0, count, id_to_code)
            assert problem is None, f"Observation contract: {problem}"
            msg = int(offered[0, 3])
            decisions += 1
            cards_row, global_row, context, selection = (obs[key][0] for key in ("cards_", "global_", "decision_", "selection_"))
            choices = (policies[0].choose(cards_row, global_row, offered)[0],
                       policies[1].choose(cards_row, global_row, offered, context, selection)[0],
                       policies[2].choose(cards_row, global_row, offered, context, selection)[0])
            assert all(0 <= int(choice) < count for choice in choices), f"Policy row out of range: {choices}"
            # A lone option is answered without a decision, so history shows those prompts.
            rows = [a for a in offered] + [a for a in obs["h_actions_"][0] if a[3]]
            if wanted and {int(a[6]) for a in rows} & wanted:
                seen.add("evidence")
            if {int(a[9]) + 256 * int(a[10]) for a in rows if int(a[3]) == NUMBER} & numbers:
                seen.add("evidence")
            if previous and previous[0] == TRIBUTE and msg != TRIBUTE and previous[2] == (2, 0) and previous[3] in targets:
                seen.add("evidence")  # One double Tribute satisfied a prompt for two Tributes.
            if msg == RACE:
                raced = {int(a[9]) for a in offered}
                assert raced and not raced & {24, 25, 26}, f"Declared Type not in this format: {sorted(raced)}"
                assert all(int(a[10]) == 0 for a in offered)
                seen.add("evidence")
            if msg == CARD:
                assert count <= 16, f"Declaration offered {count} rows"
                if all(int(a[9]) + int(a[10]) for a in offered):
                    declare_pages.append([int(a[1]) * 256 + int(a[2]) for a in offered])
                elif evidence == "declare" and declare_pages:
                    seen.add("evidence")
            if msg == ATTRIB and previous and previous[0] == ATTRIB:
                assert previous[1] not in {int(a[11]) for a in offered}, "Second Attribute repeats the first"
                seen.add("evidence")
            if msg == DISFIELD and previous and previous[0] == DISFIELD:
                assert previous[1] not in {int(a[10]) for a in offered}, "Second zone repeats the first"
                seen.add("evidence")
            # Choose: the case's card first, then anything that develops a board, else at random.
            codes_offered = [id_to_code.get(int(a[1]) * 256 + int(a[2]), 0) for a in offered]
            target_rows = [i for i, c in enumerate(codes_offered) if c in targets and int(offered[i, 4]) in
                           (ACT_ACTIVATE, ACT_SUMMON, ACT_SPSUMMON, ACT_SET, ACT_MSET)]
            develop = [i for i in range(count) if int(offered[i, 4]) in (ACT_ACTIVATE, ACT_SUMMON, ACT_SPSUMMON)]
            tribute_rows = [i for i, c in enumerate(codes_offered) if c in targets] if msg == TRIBUTE else []
            if tribute_rows:
                action = rng.choice(tribute_rows)
            elif target_rows:
                action = rng.choice(target_rows)
                if int(offered[action, 4]) == ACT_ACTIVATE and evidence == "activate":
                    seen.add("activated")
            elif develop and rng.random() < 0.8:
                action = rng.choice(develop)
            else:
                action = rng.randrange(count)
            previous = (msg, int(offered[action, 11]) if msg == ATTRIB else int(offered[action, 10]),
                        (int(obs["decision_"][0, 4]), int(obs["decision_"][0, 6])), codes_offered[action])
            obs, rewards, terminated, truncated, info = env.step(np.asarray([action]), np.asarray(info["env_id"]))
            assert not truncated[0], "Duel truncated"
            if terminated[0]:
                assert int(info["winner"][0]) in (0, 1, 2)
                finished += 1
                break
        if ("activated" if evidence == "activate" else "evidence") in seen:
            break
    print(json.dumps({"finished": finished, "decisions": decisions, "seen": sorted(seen), "pages": declare_pages[:1]}))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fail-fast", action="store_true")
    parser.add_argument("--worker")
    parser.add_argument("--codes", type=Path)
    parser.add_argument("--deck", type=Path)
    args = parser.parse_args()
    if args.worker == "legal":
        from ygoenv.ygopro import init_module
        lists = sorted((ROOT / "assets/edison/gauntlet_candidates").glob("*.ydk"))
        init_module(str(DB), str(args.codes), {path.stem: str(path) for path in lists})
        print(json.dumps({"accepted": [path.stem for path in lists]}))
        return 0
    if args.worker:
        worker(args.worker, args.codes, args.deck)
        return 0
    passed = failed = 0
    db = sqlite3.connect(DB)
    declare_views = []
    with tempfile.TemporaryDirectory(prefix="edison-prompts-") as tmp:
        tmp = Path(tmp)
        codes = tmp / "codes.txt"
        code_list(codes)
        for label, (name, cards, evidence) in CASES.items():
            print(f"\nEDISON RULE TEST 085{label} - {name}")
            print(f"Expected: {', '.join(cards)} completes through production with evidence '{evidence}' and no diagnostic.")
            deck = tmp / f"{label}.ydk"
            try:
                write_deck(db, deck, cards)
                done = subprocess.run([sys.executable, str(Path(__file__).resolve()), "--worker", label,
                                       "--codes", str(codes), "--deck", str(deck)],
                                      cwd=ROOT / "scripts", capture_output=True, text=True, timeout=25)
                output = done.stdout + done.stderr
                body = "\n".join(l for l in output.splitlines() if not l.startswith(("Gym has", "Please upgrade", "Users of", "See the migration")))
                assert done.returncode == 0, f"Worker exited {done.returncode}: {body[-600:]}"
                assert not any(m.lower() in body.lower() for m in MARKERS), f"Native diagnostic: {body[-600:]}"
                result = json.loads(done.stdout.strip().splitlines()[-1])
                expected = "activated" if evidence == "activate" else "evidence"
                assert expected in result["seen"], f"Never reached the evidence: {result}"
                if label in ("F", "Q"):
                    declare_views.append(result["pages"][0])
                print(f"Observed: {result['finished']} duel(s), {result['decisions']} decisions; evidence {result['seen']}.")
                print("RESULT: PASS")
                passed += 1
            except (AssertionError, RuntimeError, OSError, ValueError, subprocess.TimeoutExpired) as error:
                print(f"RESULT: FAIL — {error}")
                failed += 1
                if args.fail_fast:
                    break
        print("\nEDISON RULE TEST 085R - DECLARATION LIST IGNORES THE DUEL")
        print("Expected: an open declaration shows one fixed first page across different decks and seeds.")
        if len(declare_views) == 2 and declare_views[0] == declare_views[1]:
            print(f"Observed: both cases offered the same {len(declare_views[0])} rows.")
            print("RESULT: PASS")
            passed += 1
        else:
            print(f"RESULT: FAIL — first pages differ: {declare_views}")
            failed += 1
        print("\nEDISON RULE TEST 085S - ALL GAUNTLET LISTS PASS PRODUCTION LEGALITY")
        print("Expected: the 18 unedited lists load, including the XX-Saber Gottoms lists gigavise and machina_control.")
        done = subprocess.run([sys.executable, str(Path(__file__).resolve()), "--worker", "legal", "--codes", str(codes)],
                              cwd=ROOT / "scripts", capture_output=True, text=True, timeout=25)
        accepted = json.loads(done.stdout.strip().splitlines()[-1])["accepted"] if done.returncode == 0 else []
        if len(accepted) == 18 and {"gigavise", "machina_control"} <= set(accepted):
            print(f"Observed: {len(accepted)} lists accepted by init_module.")
            print("RESULT: PASS")
            passed += 1
        else:
            print(f"RESULT: FAIL — {done.stderr[-400:]}")
            failed += 1
    print(f"\nSUMMARY: {passed} PASS, {failed} FAIL")
    return int(failed != 0)

if __name__ == "__main__":
    raise SystemExit(main())
