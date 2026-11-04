#!/usr/bin/env python3
"""Test 088: a card prompt that cannot show a Deck card safely voids its cell, through the real actor.

An opposing Deck card's option index or row gives away its real position, and Tribute and Sum carry no
position to bind an own Deck card by. No pool card is known to produce these prompts, so the environment
throws, the worker aborts, and tools/edison/learner/actor.py returns VOID with no duels. Each case plants a
script on Heavy Storm, which is in both fixture lists and has no historical override, and plays random games
through a real actor worker. A clean overlay must complete. MSG_SELECT_TRIBUTE has no case: the core builds
its candidates from monsters, so no script can put a Deck card there.
"""
import argparse
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/edison/learner"))
from actor import Worker  # noqa: E402
from production_deck_label_tests import overlay_scripts, plant  # noqa: E402

HEAVY_STORM = 19613556
SEED = 80410000
MARKER = "Deck card in unsupported prompt"
OPPOSING = "Duel.GetFieldGroupCount(tp,0,LOCATION_DECK)>=2"
OPPOSING_GROUP = "local g=Duel.GetFieldGroup(tp,0,LOCATION_DECK) "
OPPOSING_MONSTERS = "local g=Duel.GetMatchingGroup(Card.IsType,tp,0,LOCATION_DECK,nil,TYPE_MONSTER) "
OWN_MONSTERS = "local g=Duel.GetMatchingGroup(Card.IsType,tp,LOCATION_DECK,0,nil,TYPE_MONSTER) "
SUM = "local lv=g:GetFirst():GetLevel() local s=g:SelectWithSumEqual(tp,Card.GetLevel,lv,1,1) "
# As the review's probe: an effect on opposing Deck cards, offered to this player when the event arrives.
CHAIN = (OPPOSING_GROUP + """local gg=Group.CreateGroup()
		for i=1,2 do
			local tc=g:GetFirst() g:RemoveCard(tc) gg:AddCard(tc)
			local e2=Effect.CreateEffect(tc)
			e2:SetDescription(aux.Stringid(46239604,0))
			e2:SetType(EFFECT_TYPE_FIELD+EFFECT_TYPE_TRIGGER_O)
			e2:SetProperty(EFFECT_FLAG_EVENT_PLAYER+EFFECT_FLAG_SET_AVAILABLE)
			e2:SetRange(LOCATION_DECK)
			e2:SetCode(EVENT_CUSTOM+19613556)
			e2:SetCountLimit(1)
			e2:SetTarget(function(e,tp,eg,ep,ev,re,r,rp,chk) if chk==0 then return true end end)
			e2:SetOperation(function(e,tp,eg,ep,ev,re,r,rp) end)
			tc:RegisterEffect(e2)
		end
		Duel.RaiseEvent(gg,EVENT_CUSTOM+19613556,e,REASON_EFFECT,tp,tp,0)""")

# label: (title, prompt message, condition, operation)
CASES = {
    "A": ("OPPOSING DECK IN A CARD PROMPT", "select_card", OPPOSING, "Duel.SelectMatchingCard(tp,aux.TRUE,tp,0,LOCATION_DECK,1,1,nil)"),
    "B": ("OPPOSING DECK, ONE CARD REVEALED", "select_card", OPPOSING,
          OPPOSING_GROUP + "Duel.ConfirmCards(tp,g:GetFirst()) Duel.SelectMatchingCard(tp,aux.TRUE,tp,0,LOCATION_DECK,1,1,nil)"),
    "C": ("OWN AND OPPOSING DECK IN ONE PROMPT", "select_card", OPPOSING, "Duel.SelectMatchingCard(tp,aux.TRUE,tp,LOCATION_DECK,LOCATION_DECK,2,2,nil)"),
    "D": ("OPPOSING DECK IN AN UNSELECT PROMPT", "select_unselect_card", OPPOSING, OPPOSING_GROUP + "g:SelectUnselect(Group.CreateGroup(),tp,false,false,1,1)"),
    "E": ("OPPOSING DECK IN A SUM PROMPT", "select_sum", OPPOSING, OPPOSING_MONSTERS + SUM),
    "F": ("OWN DECK IN A SUM PROMPT", "select_sum", "Duel.GetFieldGroupCount(tp,LOCATION_DECK,0)>=2", OWN_MONSTERS + SUM),
    "G": ("OPPOSING DECK IN A CHAIN PROMPT", "select_chain", OPPOSING, CHAIN),
    "H": ("OPPOSING DECK IN AN EFFECT YES-NO PROMPT", "select_effectyn", OPPOSING, OPPOSING_GROUP + "Duel.SelectEffectYesNo(tp,g:GetFirst())"),
}


def job(seed):
    return {"seed": seed, "games": 32, "deck0": "frog_monarch", "deck1": "blackwing",
            "policies": [{"kind": "random"}, {"kind": "random"}]}


def run(directory, script, seed):
    """One cell on a fresh actor worker, whose scripts come from this directory."""
    root = overlay_scripts(directory / "overlay", script, HEAVY_STORM) if script else ROOT / "scripts"
    worker = Worker(directory / "logs", 0, cwd=root)
    try:
        before = worker.log_path.stat().st_size
        result = worker.run(job(seed), 120)
        with worker.log_path.open("rb") as log:
            log.seek(before)
            text = log.read().decode(errors="replace")
    finally:
        worker.stop()
    return result, text


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--fail-fast", action="store_true")
    args = parser.parse_args()
    passed = failed = 0
    with tempfile.TemporaryDirectory(prefix="edison-deck-void-") as tmp:
        tmp = Path(tmp)
        print("\nEDISON RULE TEST 088Z - CLEAN CONTROL")
        print("Expected: the same cell with unplanted scripts completes, so a void below comes from the plant.")
        (tmp / "clean").mkdir()
        try:
            result, _ = run(tmp / "clean", None, SEED)
            assert result["status"] == "PASS" and len(result["duels"]) == 32, f"The clean cell did not complete: {str(result)[:300]}"
            print("Observed: 32 duels completed.")
            print("RESULT: PASS")
            passed += 1
        except (AssertionError, OSError, ValueError, RuntimeError) as error:
            print(f"RESULT: FAIL — {error}")
            failed += 1
        for index, (label, (title, message, condition, operation)) in enumerate(CASES.items()):
            print(f"\nEDISON RULE TEST 088{label} - {title}")
            print("Expected: the worker aborts on this prompt, the cell is VOID, and no duel is counted.")
            directory = tmp / label
            directory.mkdir()
            try:
                result, text = run(directory, plant(condition, operation, HEAVY_STORM), SEED)
                assert result.get("status") == "VOID", f"The cell was not voided: {result.get('status')}"
                assert "duels" not in result, "A voided cell still reports duels"
                assert result["reason"] == "worker died or timed out (exit -6)", f"Not the abort expected: {result['reason']}"
                assert f"{MARKER}: {message}" in text, f"The abort is not this prompt's: {text[-300:]}"
                print(f"Observed: {result['reason']}; the log names {message}.")
                print("RESULT: PASS")
                passed += 1
            except (AssertionError, OSError, ValueError, RuntimeError) as error:
                print(f"RESULT: FAIL — {error}")
                failed += 1
                if args.fail_fast:
                    break
    print(f"\nSUMMARY: {passed} PASS, {failed} FAIL")
    return int(failed != 0)


if __name__ == "__main__":
    raise SystemExit(main())
