#!/usr/bin/env python3
"""Test 086: at a Deck prompt each option shows the card the engine offered, and nothing more.

Prompts that cannot show a Deck card safely void instead: see production_deck_void_tests.py.

Each case plays seeded duels through production init_module and env.step until it reaches its
prompt. The expected identities come from the prompt's own card codes, through the test-only
differential in prompt_audit.h. These fail on the build before docs/edison-deck-labels.md, where
an own Deck option was labelled with the card at real Deck position k.
"""
import argparse
from collections import Counter
import json
import os
from pathlib import Path
import random
import sqlite3
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.append(str(ROOT))
sys.path.insert(0, str(ROOT / "ygoenv"))
sys.path.insert(0, str(ROOT / "tools/edison/pool_audit"))
from production_prompt_tests import DB, MARKERS, code_list, code_of  # noqa: E402
SEED = 80810000
MAX_OPTIONS = 256
MAX_DUELS = 40
DECK_LOCATION, MZONE_LOCATION = 1, 3  # Column 2 of a card row.
POS_FACE_UP = 0x5
ACT_SET, ACT_SPSUMMON, ACT_SUMMON, ACT_ACTIVATE = 1, 3, 4, 8
CARD_PROMPT = 3  # Column 3 of an action row, in _msgs order.
FROGS = [("Substitoad", 3), ("Swap Frog", 3), ("Dupe Frog", 3), ("Treeborn Frog", 2)]
SEARCH_FROGS = [("Substitoad", 3), ("Dupe Frog", 3), ("Treeborn Frog", 3)]  # Without Swap Frog, whose filter is wider.
EQUIPS = [("Arsenal Robber", 3), ("Axe of Despair", 3), ("Malevolent Nuzzler", 3)]

# label: (title, expectation, cards and copies, cards the driver plays first, evidence)
CASES = {
    "A": ("SUBSTITOAD, MIXED DECK", "every option is the Frog the engine offered, and history records the pick", SEARCH_FROGS, ["Substitoad"], "deck_only"),
    "B": ("SWAP FROG, DECK OR FIELD", "masked Deck options and real field options both show their card and zone", FROGS, ["Swap Frog"], "deck_and_field"),
    "C": ("DUPLICATES OF ONE CODE", "copies of a code take distinct rows, in identity order", FROGS, ["Substitoad"], "duplicates"),
    "D": ("A NON-CANDIDATE STAYS HIDDEN", "only candidates are visible; every other Deck row is blank", FROGS, ["Substitoad"], "non_candidate"),
    "E": ("TWO PICKS FROM THE DECK", "the first pick stays flagged on a row showing it", [("Thunder Dragon", 3)], ["Thunder Dragon"], "second_pick"),
    "F": ("UNSELECT LIST FROM THE DECK", "both lists of the prompt map to distinct rows showing their cards", [("Advanced Ritual Art", 1), ("Demise, King of Armageddon", 3)], ["Advanced Ritual Art", "Demise, King of Armageddon"], "unselect"),
    "G": ("A LONE CANDIDATE IN HISTORY", "an option answered with no decision records the true card", [("Substitoad", 2), ("Dupe Frog", 1)], ["Substitoad"], "lone"),
    "H": ("THE OPPONENT CHOOSES FROM THEIR OWN DECK", "the selecting seat sees its own candidates", EQUIPS, ["Arsenal Robber"], "other_seat"),
    "K": ("A COPY ALREADY REVEALED", "binding uses the revealed copy, so no further copy is shown; a planted script with Dark Tinker's reveal", [("Upstart Goblin", 3), ("Dupe Frog", 3)], ["Upstart Goblin"], "revealed_copy"),
    "L": ("A FACE-UP COPY IN THE DECK", "a face-up candidate binds to a face-up copy; a planted script with ReverseInDeck", [("Upstart Goblin", 3), ("Dupe Frog", 3)], ["Upstart Goblin"], "face_up_copy"),
    "N": ("A REVEALED FACE-UP COPY AND TWO FACE-DOWN CANDIDATES", "all three copies show, and that is not extra; a planted script", [("Upstart Goblin", 3), ("Dupe Frog", 3)], ["Upstart Goblin"], "reveal_and_candidates"),
}
UPSTART = 70368879
DUPE = 46239604


def plant(target, operation, code=UPSTART):
    """A Spell whose effect is this Lua, in place of the script of a Spell with no historical override."""
    return f"""
function c{code}.initial_effect(c)
	local e1=Effect.CreateEffect(c)
	e1:SetType(EFFECT_TYPE_ACTIVATE)
	e1:SetCode(EVENT_FREE_CHAIN)
	e1:SetTarget(function(e,tp,eg,ep,ev,re,r,rp,chk)
		if chk==0 then return {target} end
	end)
	e1:SetOperation(function(e,tp,eg,ep,ev,re,r,rp)
		{operation}
	end)
	c:RegisterEffect(e1)
end
"""


# Dark Tinker's reveal, ConfirmCards on one own Deck card with no move after it, then a Deck prompt.
REVEALED_COPY = plant(f"Duel.GetMatchingGroupCount(Card.IsCode,tp,LOCATION_DECK,0,nil,{DUPE})>=3",
                      f"local g=Duel.GetMatchingGroup(Card.IsCode,tp,LOCATION_DECK,0,nil,{DUPE}) "
                      "local tc=g:GetMaxGroup(Card.GetSequence):GetFirst() "
                      "Duel.ConfirmCards(tp,tc) "
                      f"local s=Duel.SelectMatchingCard(tp,function(c) return c:IsCode({DUPE}) and c~=tc end,tp,LOCATION_DECK,0,1,1,nil) "
                      "Duel.SendtoGrave(s,REASON_EFFECT)")
# Two of three copies face-up, and only those offered. The lower copies are the ones a blind binding takes.
FACE_UP_COPY = plant(f"Duel.GetMatchingGroupCount(Card.IsCode,tp,LOCATION_DECK,0,nil,{DUPE})>=3",
                     f"local g=Duel.GetMatchingGroup(Card.IsCode,tp,LOCATION_DECK,0,nil,{DUPE}) "
                     "local a=g:GetMaxGroup(Card.GetSequence):GetFirst() g:RemoveCard(a) a:ReverseInDeck() "
                     "local b=g:GetMaxGroup(Card.GetSequence):GetFirst() b:ReverseInDeck() "
                     f"local s=Duel.SelectMatchingCard(tp,function(c) return c:IsCode({DUPE}) and c:IsPosition(POS_FACEUP) end,tp,LOCATION_DECK,0,1,1,nil) "
                     "Duel.SendtoGrave(s,REASON_EFFECT)")
# Dark Tinker's reveal of a face-up copy, with the other two copies offered. Nothing here is extra.
REVEAL_AND_CANDIDATES = plant(f"Duel.GetMatchingGroupCount(Card.IsCode,tp,LOCATION_DECK,0,nil,{DUPE})>=3",
                              f"local g=Duel.GetMatchingGroup(Card.IsCode,tp,LOCATION_DECK,0,nil,{DUPE}) "
                              "local tc=g:GetMaxGroup(Card.GetSequence):GetFirst() tc:ReverseInDeck() "
                              "Duel.ConfirmCards(tp,tc) "
                              f"local s=Duel.SelectMatchingCard(tp,function(c) return c:IsCode({DUPE}) and c~=tc end,tp,LOCATION_DECK,0,1,1,nil) "
                              "Duel.SendtoGrave(s,REASON_EFFECT)")
PLANTS = {"K": REVEALED_COPY, "L": FACE_UP_COPY, "N": REVEAL_AND_CANDIDATES}


def write_deck(db, path, cards):
    manifest = json.loads((ROOT / "assets/edison/legality.json").read_text())
    pool = {code for card in manifest["cards"] for code in card["accepted_ids"]}
    limited = {card["code"]: card["limit"] for card in manifest["restrictions"]}
    main = []
    for name, copies in cards:
        code = code_of(db, name)
        main += [code] * min(copies, limited.get(code, 3))
    filler = [row[0] for row in db.execute(
        "SELECT id FROM datas WHERE type=17 AND alias=0 AND level<=4 AND atk>=1000 ORDER BY id")
        if row[0] in pool and row[0] not in limited]
    main += filler[:40 - len(main)]
    assert len(main) == 40
    path.write_text("#main\n" + "\n".join(map(str, main)) + "\n#extra\n!side\n")


def overlay_scripts(root, script, code=UPSTART):
    """A copy of the script directory, as links, with one card's file planted."""
    target = root / "script"
    target.mkdir(parents=True)
    source = (ROOT / "scripts/script").resolve()
    for path in source.iterdir():
        if path.suffix == ".lua" and path.name != f"c{code}.lua":
            (target / path.name).symlink_to(path)
    (target / f"c{code}.lua").write_text(script)
    return root


def row_id(row):
    return int(row[0]) * 256 + int(row[1])


def check_decision(line, obs, count, id_to_code):
    """Compare the engine's prompt with the observation, and return what this decision shows."""
    cards = obs["cards_"][0]
    offered = obs["actions_"][0, :count]
    spec_rows = [a for a in offered if a[0]]
    entries = line["options"]
    assert len(spec_rows) == len(entries), f"Options differ: {len(spec_rows)} rows, {len(entries)} in the prompt"
    deck_options = []
    for entry, action in zip(entries, spec_rows):
        assert int(action[0]) == entry["row"] and row_id(action[1:3]) == entry["id"], f"Audit disagrees with actions_: {entry}"
        row = cards[entry["row"] - 1]
        own = entry["controller"] == line["player"]
        assert own or entry["spec_kind"] != "deck", f"An opposing Deck card reached an observation: {entry}"
        if entry["exempt"]:
            continue
        assert entry["id"] == entry["expected_id"], f"Option labelled {entry['id']}, engine offered {entry['expected_id']}: {entry}"
        assert row_id(row) == entry["expected_id"], f"Option's row shows {row_id(row)}, engine offered {entry['expected_id']}: {entry}"
        assert int(row[4]) == (0 if own else 1), f"Option's row belongs to the other player: {entry}"
        if entry["spec_kind"] == "deck":
            assert int(row[2]) == DECK_LOCATION, f"Deck option's row is not in the Deck: {entry}"
            deck_options.append(entry)
    rows = [entry["row"] for entry in deck_options]
    assert len(rows) == len(set(rows)), f"Options share a row: {rows}"
    # Copies pair with rows in one fixed way, so the pairing cannot say which copy sits higher.
    for code in {entry["code"] for entry in deck_options}:
        copies = [entry["row"] for entry in deck_options if entry["code"] == code]
        assert copies == sorted(copies), f"Copies of {code} pair with rows out of order: {copies}"
    own_deck = [(index + 1, row) for index, row in enumerate(cards) if int(row[2]) == DECK_LOCATION and int(row[4]) == 0]
    visible = [(index, row_id(row)) for index, row in own_deck if row_id(row)]
    # Cards the engine lists as selected or mandatory, compared with the prompt's codes and not with the flags.
    selected_entries = line["selected"]
    entry_rows = [entry["row"] for entry in selected_entries]
    assert sorted(entry_rows) == sorted(line["selected_rows"]), f"Selection flags {line['selected_rows']}, engine lists rows {entry_rows}"
    assert sorted(entry["row"] for entry in selected_entries if entry["mandatory"]) == sorted(line["mandatory_rows"]), f"Mandatory flags {line['mandatory_rows']} differ from the prompt"
    assert len(entry_rows) == len(set(entry_rows)), f"Selected cards share a row: {entry_rows}"
    assert not set(entry_rows) & {entry["row"] for entry in entries}, f"A selected card shares a row with an option: {entry_rows}"
    for entry in selected_entries:
        row = cards[entry["row"] - 1]
        if not entry["exempt"]:
            assert row_id(row) == entry["expected_id"], f"Selected row shows {row_id(row)}, engine lists {entry['expected_id']}: {entry}"
    selected = [entry["row"] for entry in selected_entries if entry["spec_kind"] == "deck" and int(cards[entry["row"] - 1, 4]) == 0]
    expected = sorted([entry["row"] for entry in deck_options] + selected)
    shown_rows = {index: cid for index, cid in visible}
    # Blank rows are face-down, so a row like them is the copy a face-up candidate must not take.
    blank_face = next((int(row[5]) for _, row in own_deck if not row_id(row)), None)
    if blank_face:
        for entry in deck_options:
            row_down = int(cards[entry["row"] - 1, 5]) == blank_face
            assert row_down == (not entry["position"] & POS_FACE_UP), f"A candidate binds to a copy with the other face state: {entry}"
    # A row beyond the candidates must be a reveal the candidates of that code and face state did not use.
    leftover = [index for index, _ in visible if index not in expected]
    def kind_of(row_number):
        return (id_to_code[row_id(cards[row_number - 1])], int(int(cards[row_number - 1, 5]) != blank_face))
    allowed = Counter(tuple(item) for item in line["revealed_own_deck"])
    bound = Counter(kind_of(row) for row in expected if row in shown_rows)
    for index in leftover:
        kind = kind_of(index)
        assert allowed[kind] > bound[kind], f"Deck row {index} is visible, and neither a candidate nor a reveal: {visible}, candidates {expected}"
        bound[kind] += 1
    assert not [row for row in expected if row not in shown_rows], f"A candidate's row is blank: {visible}, candidates {expected}"
    assert line["extra_rows"] == 0, f"{line['extra_rows']} Deck rows shown beyond the prompt"
    for index, row in own_deck:
        if not row_id(row):
            assert not row[7:].any(), f"Hidden Deck row {index} carries statistics"
            assert int(row[3]) == 0, f"Hidden Deck row {index} carries a position"
    # Identity order: visible rows lead the Deck and ascend by code, so a row says nothing of position.
    if visible:
        first = own_deck[0][0]
        assert [index for index, _ in visible] == list(range(first, first + len(visible))), "Visible Deck rows are not first"
        codes = [id_to_code[cid] for _, cid in visible]
        assert codes == sorted(codes), f"Visible Deck rows are not in identity order: {codes}"
    return {"deck_options": deck_options, "selected": selected, "kinds": {entry["spec_kind"] for entry in entries},
            "visible": [cid for _, cid in visible], "blank_face": blank_face}


def worker(label, codes, deck, so):
    if so:
        import importlib.util
        spec = importlib.util.spec_from_file_location("ygoenv.ygopro.ygopro_ygoenv", so)
        module = importlib.util.module_from_spec(spec)
        sys.modules["ygoenv.ygopro.ygopro_ygoenv"] = module
        spec.loader.exec_module(module)
    import numpy as np
    import ygoenv
    from ygoenv.ygopro import init_module
    from soak_worker import check_contracts
    name, expectation, cards, play_first, evidence = CASES[label]
    init_module(str(DB), str(codes), {"deck": str(deck)})
    id_to_code = {i + 1: int(line.split()[0]) for i, line in enumerate(codes.read_text().splitlines())}
    code_to_id = {code: cid for cid, code in id_to_code.items()}
    db = sqlite3.connect(DB)
    targets = {code_to_id[code_of(db, card)] for card in play_first}
    frog_ids = {cid for cid, code in id_to_code.items() if (db.execute("select setcode from datas where id=?", (code,)).fetchone() or (0,))[0] & 0xfff == 0x12}
    assert code_to_id[DUPE] in frog_ids
    trace = Path(os.environ["EDISON_PROMPT_AUDIT"]) / f"audit-{os.getpid()}.jsonl"
    position = 0
    seen, checked = set(), 0
    decisions = 0

    def new_lines():
        nonlocal position
        if not trace.exists():
            return []
        with trace.open() as stream:
            stream.seek(position)
            text = stream.read()
            position = stream.tell()
        return [json.loads(line) for line in text.splitlines() if line]

    for duel in range(MAX_DUELS):
        seed = SEED + (ord(label) - 65) * 1000 + duel
        env = ygoenv.make("YGOPro-v1", env_type="gymnasium", num_envs=1, num_threads=1, seed=seed, deck1="deck", deck2="deck",
                          play_mode="self", player=-1, async_reset=False, greedy_reward=False, oppo_info=False,
                          max_options=MAX_OPTIONS, max_steps=10000, max_episode_steps=10000, timeout=10, verbose=False)
        rng = random.Random(seed ^ 0xED150)
        obs, info = env.reset()
        picked, pending_lone = {}, {}
        activator = None
        while True:
            count = int(info["num_options"][0])
            assert 0 < count <= MAX_OPTIONS and not info["action_overflow"][0], "action overflow"
            problem = check_contracts(obs, 0, count, id_to_code)
            assert problem is None, f"Observation contract: {problem}"
            offered = obs["actions_"][0, :count]
            decisions += 1
            seat = int(info["to_play"][0])
            lines = new_lines()
            for record in lines:
                if record["type"] == "duel":
                    # Reached only after a reset, for the duel before this one.
                    raise AssertionError("Duel record before the end of a duel")
                if record["type"] == "lone" and record["msg"] == "select_card":
                    # A later lone card prompt becomes the newest, so only the last one can be checked.
                    pending_lone[record["player"]] = record["expected_id"] if record["spec_kind"] == "deck" else None
                    picked.pop(record["player"], None)
            decisions_shown = [record for record in lines if record["type"] == "decision"]
            shown = decisions_shown[-1] if decisions_shown else None
            if shown is not None:
                assert shown["player"] == int(info["to_play"][0]), "Audit line is not this decision"
                result = check_decision(shown, obs, count, id_to_code)
                checked += 1
                deck_options, kinds = result["deck_options"], result["kinds"]
                own_deck_cards = [row for row in obs["cards_"][0] if int(row[2]) == DECK_LOCATION and int(row[4]) == 0]
                if evidence == "deck_only" and len(deck_options) >= 2 and kinds == {"deck"}:
                    assert {entry["id"] for entry in deck_options} <= frog_ids, "A Substitoad option is not a Frog"
                    seen.add("deck prompt")
                if evidence == "deck_and_field" and deck_options and "mzone" in kinds:
                    seen.add("evidence")
                if evidence == "duplicates":
                    ids = [entry["id"] for entry in deck_options]
                    if len(ids) != len(set(ids)):
                        seen.add("evidence")
                if evidence == "non_candidate" and deck_options and len(own_deck_cards) > len(result["visible"]):
                    seen.add("evidence")
                if evidence == "second_pick" and deck_options and result["selected"]:
                    seen.add("evidence")
                if evidence == "unselect" and shown["msg"] == "select_unselect_card" and deck_options and result["selected"]:
                    seen.add("evidence")
                if evidence == "other_seat" and deck_options and activator == 1 - shown["player"]:
                    seen.add("evidence")
                if evidence == "revealed_copy" and deck_options and shown["revealed_own_deck"]:
                    copies = sum(cid == deck_options[0]["id"] for cid in result["visible"])
                    assert copies == len(deck_options), f"{copies} copies shown for {len(deck_options)} candidates and one reveal"
                    seen.add("evidence")
                if evidence == "face_up_copy" and deck_options and all(entry["position"] & POS_FACE_UP for entry in deck_options):
                    assert result["blank_face"], "No blank Deck row to compare with"
                    seen.add("evidence")
                if evidence == "reveal_and_candidates" and len(deck_options) == 2 and shown["revealed_own_deck"]:
                    copies = sum(cid == deck_options[0]["id"] for cid in result["visible"])
                    assert copies == 3, f"{copies} copies shown for 2 face-down candidates and a revealed face-up copy"
                    seen.add("evidence")
            seat = int(info["to_play"][0])
            history = obs["h_actions_"][0]
            card_rows = [index for index, row in enumerate(history) if int(row[3]) == CARD_PROMPT]
            if picked.get(seat):
                # The pick must be the newest card prompt in the history this seat sees next, with the
                # older entries moved along by exactly that much, so an older identical copy cannot pass.
                id_picked, before = picked.pop(seat)
                assert card_rows and row_id(history[card_rows[0], 1:3]) == id_picked, f"Newest card prompt in history is not the picked card {id_picked}"
                newest = card_rows[0]
                columns = [c for c in range(history.shape[1]) if c != 12]  # Column 12 is turns ago.
                older = history[newest + 1:][:, columns]
                assert (older == before[:len(older)][:, columns]).all(), "History's pick is not a new entry"
                seen.add("history")
            if pending_lone.get(seat):
                assert card_rows and row_id(history[card_rows[0], 1:3]) == pending_lone[seat], f"Newest card prompt in history is not the lone card {pending_lone[seat]}"
                seen.add("lone history")
            pending_lone.pop(seat, None)
            # Choose: the case's cards first, then anything that develops a board, else at random.
            ids = [row_id(a[1:3]) for a in offered]
            target_rows = [i for i, cid in enumerate(ids) if cid in targets and int(offered[i, 4]) in (ACT_ACTIVATE, ACT_SUMMON, ACT_SPSUMMON)]
            develop = [i for i in range(count) if int(offered[i, 4]) in (ACT_ACTIVATE, ACT_SUMMON, ACT_SPSUMMON)]
            set_rows = [i for i, cid in enumerate(ids) if cid in targets and int(offered[i, 4]) == ACT_SET]
            if target_rows:
                action = rng.choice(target_rows)
                activator = int(info["to_play"][0])
            elif set_rows:  # Arsenal Robber is a Trap.
                action = rng.choice(set_rows)
            elif develop and rng.random() < 0.8:
                action = rng.choice(develop)
            else:
                action = rng.randrange(count)
            if evidence == "deck_only" and shown is not None and shown["msg"] == "select_card":
                spec_rows = [i for i in range(count) if offered[i, 0]]
                if action in spec_rows and shown["options"][spec_rows.index(action)]["spec_kind"] == "deck":
                    picked[int(info["to_play"][0])] = (shown["options"][spec_rows.index(action)]["expected_id"], obs["h_actions_"][0].copy())
            obs, rewards, terminated, truncated, info = env.step(np.asarray([action]), np.asarray(info["env_id"]))
            assert not truncated[0], "Duel truncated"
            if terminated[0]:
                for record in new_lines():
                    if record["type"] == "duel":
                        assert record["mismatch_total"] == 0, f"Differential mismatches: {record['mismatches'][:4]}"
                        lone = record["counts"].get("select_card|deck|own")
                        # lone_options, then lone_mismatch, in prompt_audit.h's field order.
                        if evidence == "lone" and lone and lone[7] and "lone history" in seen:
                            seen.add("evidence")
                break
        if {"deck prompt", "history"} <= seen:
            seen.add("evidence")
        if "evidence" in seen:
            break
    print(json.dumps({"seen": sorted(seen), "duels": duel + 1, "decisions": decisions, "checked": checked}))
    sys.stdout.flush()
    os._exit(0)


def verbose_worker(codes, verbose, so):
    """Whole-observation digest of seeded duels through the verbose or the plain prompt parser."""
    if so:
        import importlib.util
        spec = importlib.util.spec_from_file_location("ygoenv.ygopro.ygopro_ygoenv", so)
        module = importlib.util.module_from_spec(spec)
        sys.modules["ygoenv.ygopro.ygopro_ygoenv"] = module
        spec.loader.exec_module(module)
    import hashlib
    import numpy as np
    import ygoenv
    from ygoenv.ygopro import init_module
    decks = {name: str(ROOT / "assets/edison/decks" / f"{name}.ydk") for name in ("frog_monarch", "blackwing")}
    init_module(str(DB), str(codes), decks)
    digest = hashlib.sha256()
    decisions = 0
    for duel in range(3):
        seed = SEED + 9000 + duel
        env = ygoenv.make("YGOPro-v1", env_type="gymnasium", num_envs=1, num_threads=1, seed=seed, deck1="frog_monarch", deck2="blackwing",
                          play_mode="self", player=-1, async_reset=False, greedy_reward=False, oppo_info=False,
                          max_options=MAX_OPTIONS, max_steps=10000, max_episode_steps=10000, timeout=10, verbose=verbose)
        rng = random.Random(seed)
        obs, info = env.reset()
        while True:
            for key in ("cards_", "global_", "actions_", "h_actions_", "decision_", "selection_"):
                digest.update(np.ascontiguousarray(obs[key]).tobytes())
            count = int(info["num_options"][0])
            decisions += 1
            develop = [i for i in range(count) if int(obs["actions_"][0, i, 4]) in (ACT_ACTIVATE, ACT_SUMMON, ACT_SPSUMMON)]
            action = rng.choice(develop) if develop and rng.random() < 0.8 else rng.randrange(count)
            obs, _, terminated, truncated, info = env.step(np.asarray([action]), np.asarray(info["env_id"]))
            assert not truncated[0], "Duel truncated"
            if terminated[0]:
                break
    print("\nRESULT" + json.dumps({"sha256": digest.hexdigest(), "decisions": decisions}))
    sys.stdout.flush()
    os._exit(0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fail-fast", action="store_true")
    parser.add_argument("--worker")
    parser.add_argument("--codes", type=Path)
    parser.add_argument("--deck", type=Path)
    parser.add_argument("--so", type=Path, help="Run a module file instead of the checkout's, for the negative control")
    args = parser.parse_args()
    if args.worker in ("verbose", "plain"):
        verbose_worker(args.codes, args.worker == "verbose", args.so)
        return 0
    if args.worker:
        worker(args.worker, args.codes, args.deck, args.so)
        return 0
    passed = failed = 0
    db = sqlite3.connect(DB)
    with tempfile.TemporaryDirectory(prefix="edison-deck-labels-") as tmp:
        tmp = Path(tmp)
        codes = tmp / "codes.txt"
        code_list(codes)
        for label, (name, expectation, cards, play_first, evidence) in CASES.items():
            print(f"\nEDISON RULE TEST 086{label} - {name}")
            print(f"Expected: {expectation}.")
            deck = tmp / f"{label}.ydk"
            audit = tmp / f"audit-{label}"
            audit.mkdir()
            cwd = ROOT / "scripts"
            try:
                write_deck(db, deck, cards)
                if label in PLANTS:
                    cwd = overlay_scripts(tmp / f"overlay-{label}", PLANTS[label])
                env = {**os.environ, "EDISON_PROMPT_AUDIT": str(audit), "EDISON_PROMPT_AUDIT_TRACE": "1"}
                command = [sys.executable, str(Path(__file__).resolve()), "--worker", label, "--codes", str(codes), "--deck", str(deck)]
                if args.so:
                    command += ["--so", str(args.so.resolve())]
                done = subprocess.run(command, cwd=cwd, env=env, capture_output=True, text=True, timeout=60)
                output = done.stdout + done.stderr
                body = "\n".join(l for l in output.splitlines() if not l.startswith(("Gym has", "Please upgrade", "Users of", "See the migration")))
                assert done.returncode == 0, f"Worker exited {done.returncode}: {body[-500:]}"
                assert not any(m.lower() in body.lower() for m in MARKERS), f"Native diagnostic: {body[-500:]}"
                result = json.loads(done.stdout.strip().splitlines()[-1])
                assert "evidence" in result["seen"], f"Never reached the prompt: {result}"
                print(f"Observed: reached in duel {result['duels']}; {result['checked']} card-listing decisions compared with the prompt.")
                print("RESULT: PASS")
                passed += 1
            except (AssertionError, RuntimeError, OSError, ValueError, subprocess.TimeoutExpired) as error:
                print(f"RESULT: FAIL — {error}")
                failed += 1
                if args.fail_fast:
                    break
        print("\nEDISON RULE TEST 086J - THE VERBOSE PARSER AGREES")
        print("Expected: seeded duels give byte-identical observations through the verbose and the plain prompt parser.")
        try:
            results = []
            for mode in ("verbose", "plain"):
                command = [sys.executable, str(Path(__file__).resolve()), "--worker", mode, "--codes", str(codes)]
                if args.so:
                    command += ["--so", str(args.so.resolve())]
                done = subprocess.run(command, cwd=ROOT / "scripts", capture_output=True, text=True, errors="replace", timeout=60)
                assert done.returncode == 0, f"{mode} worker exited {done.returncode}: {done.stderr[-400:]}"
                results.append(json.loads(done.stdout.rsplit("RESULT", 1)[1]))
            assert results[0] == results[1], f"Parsers differ: {results}"
            print(f"Observed: {results[0]['decisions']} decisions identical.")
            print("RESULT: PASS")
            passed += 1
        except (AssertionError, OSError, ValueError, subprocess.TimeoutExpired) as error:
            print(f"RESULT: FAIL — {error}")
            failed += 1
    print("\nEDISON RULE TEST 086M - THE AUDIT COMMANDS FAIL CLOSED")
    print("Expected: mismatches, differing traces and empty or thin coverage are all a failure, in exit code and verdict.")
    try:
        import prompt_audit
        tool = str(Path(__file__).with_name("prompt_audit.py"))
        with tempfile.TemporaryDirectory(prefix="edison-audit-verdict-") as tmp:
            tmp = Path(tmp)
            (tmp / "empty").mkdir()
            (tmp / "bad").mkdir()
            record = {"type": "duel", "seed": 1, "deck0": "a", "deck1": "b", "decisions": 5, "card_decisions": 5, "extra_visible_rows": 0,
                      "prompts_with_extra_rows": 0, "order_pairs_distinct": 0, "order_top_first_distinct": 0, "order_pairs_same": 0,
                      "order_top_first_same": 0, "mismatch_total": 1, "counts": {"select_card|deck|own": [1, 1, 0, 1, 1]},
                      "mismatches": [{"kind": "option", "key": "select_card|deck|own"}]}
            (tmp / "bad/audit-1.jsonl").write_text(json.dumps(record) + "\n")
            (tmp / "good").mkdir()
            (tmp / "good/audit-1.jsonl").write_text(json.dumps({**record, "mismatch_total": 0, "mismatches": [], "counts": {"select_card|deck|own": [1, 1]}}) + "\n")
            def exit_code(*arguments):
                return subprocess.run([sys.executable, tool, *arguments], capture_output=True, text=True, timeout=20).returncode
            assert exit_code("summarize", str(tmp / "empty")) == 1, "An empty audit passed"
            assert exit_code("summarize", str(tmp / "bad")) == 1, "An audit with a mismatch passed"
            assert exit_code("summarize", str(tmp / "good")) == 0, "A clean audit failed"
            assert exit_code("summarize", str(tmp / "good"), "--min-duels", "2") == 1, "An audit below its minimum passed"
            assert exit_code("compare-soak", str(tmp / "empty"), str(tmp / "empty")) == 1, "An empty soak comparison passed"
            # A counter that C++ can bump with no mismatch record is still a failure.
            for position, name in ((6, "shared row"), (12, "selected mismatch"), (13, "face mismatch")):
                row = [1, 1] + [0] * 12
                row[position] = 1
                (tmp / "counter").mkdir(exist_ok=True)
                (tmp / "counter/audit-1.jsonl").write_text(json.dumps({**record, "mismatch_total": 0, "mismatches": [], "counts": {"select_card|deck|own": row}}) + "\n")
                assert exit_code("summarize", str(tmp / "counter")) == 1, f"A {name} with no record passed"
            done = subprocess.run([sys.executable, str(ROOT / "tools/edison/hardening/soak.py"), "--stage", "uniform", "--count", "0", "--seed", "80400000",
                                   "--output", str(tmp / "empty-soak")], capture_output=True, text=True, timeout=25)
            assert done.returncode == 1 and "CLEAN" not in done.stdout, "A soak with no duels reported clean"
            # compare_traces.py: a new case that fails is a failure, and an empty comparison is too.
            for name in ("old", "new", "conformance"):
                (tmp / name).mkdir()
            (tmp / "conformance/results.json").write_text("{}")
            (tmp / "conformance/test-1.txt").write_text("same")
            (tmp / "old/run-1.txt").write_text("EDISON RULE TEST 001A - X\nRESULT: PASS\n")
            def compare(new_text):
                (tmp / "new/run-1.txt").write_text(new_text)
                return subprocess.run([sys.executable, str(ROOT / "tools/edison/hardening/compare_traces.py"), "--certification", str(tmp / "old"), str(tmp / "new"),
                                       "--conformance", str(tmp / "conformance"), str(tmp / "conformance"), "--output", str(tmp / "traces.json")],
                                      capture_output=True, text=True, timeout=20).returncode
            same = "EDISON RULE TEST 001A - X\nRESULT: PASS\n"
            assert compare(same) == 0, "Identical traces failed"
            assert compare(same + "EDISON RULE TEST 002A - Y\nRESULT: PASS\n") == 0, "A passing new case failed"
            assert compare(same + "EDISON RULE TEST 002A - Y\nRESULT: FAIL - bad\n") == 1, "A failing new case passed"
            assert compare("EDISON RULE TEST 001A - X\nRESULT: FAIL - bad\n") == 1, "A changed earlier trace passed"
        assert prompt_audit.verdict_no_effect({"pairings": [{"deck0": "a", "deck1": "b", "identical": False, "audit_saw_duels": 1, "audit_saw_decisions": 1}]}), "Differing traces passed"
        assert prompt_audit.verdict_no_effect({"pairings": []}), "No pairings passed"
        assert prompt_audit.verdict_fields({"only_expected_fields_changed": False, "pairings": [{"decisions": 1, "own_deck_option_decisions": 1}]}), "Unexpected field changes passed"
        assert prompt_audit.verdict_fields({"only_expected_fields_changed": True, "pairings": [{"decisions": 1, "own_deck_option_decisions": 0}]}), "A comparison reaching no Deck prompt passed"
        clean = prompt_audit.summarize([])
        clean.update(duels=2, decisions=9, by_kind={"select_card|deck|own": dict(zip(prompt_audit.FIELDS, [1, 1] + [0] * 12))})
        counts = {"deck_prompts": 3, "label_right": 3}
        assert not prompt_audit.verdict_labels(counts, clean, 2), f"A clean label check failed: {prompt_audit.verdict_labels(counts, clean, 2)}"
        assert prompt_audit.verdict_labels(counts, clean, 3), "A label check with incomplete audit coverage passed"
        assert prompt_audit.verdict_labels({"deck_prompts": 0, "label_right": 0}, clean, 2), "A label check with no prompts passed"
        assert prompt_audit.verdict_labels({"deck_prompts": 3, "label_right": 2}, clean, 2), "A wrong label passed"
        print("Observed: every bad report is rejected and the clean one is accepted.")
        print("RESULT: PASS")
        passed += 1
    except (AssertionError, OSError, ValueError, subprocess.TimeoutExpired) as error:
        print(f"RESULT: FAIL — {error}")
        failed += 1
    print(f"\nSUMMARY: {passed} PASS, {failed} FAIL")
    return int(failed != 0)


if __name__ == "__main__":
    raise SystemExit(main())
