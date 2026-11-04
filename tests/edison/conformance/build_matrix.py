#!/usr/bin/env python3
"""Build the rules-conformance matrix from fixture results.

Status comes from the run, never from this file's opinion: an item with conformance
fixtures takes its status from results.json, and any failing fixture makes it FAIL.
An item resting only on an existing certification fixture is PASS only if the supplied
baseline report says the certification suite passed.

Inputs:  build/edison-conformance/results/results.json, a certification report.json,
         build/edison-rules-conformance-2026-11-04/metagame-rulings-extract.json
Outputs: conformance-matrix.json and conformance-matrix.md in that report directory.
"""
import argparse
import collections
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
REPORT = ROOT / "tests/edison/reports/rules-conformance-2026-11-04"
RULES = "https://www.edisonformat.com/edison-rule-differences.html"
ERRATA = "https://www.edisonformat.com/rulings/relevant-errata"
PRIORITY = "https://www.edisonformat.com/priority.html"
SUMMONS = "https://www.edisonformat.com/summons.html"
TIMING = "https://www.edisonformat.com/missing-the-timing.html"
BATTLE = "https://www.edisonformat.com/battle-phase.html"
RULINGS_INDEX = "https://www.edisonformat.com/rulings"
PAGE_URL = {
    "rulings_individual-rulings-a-c.txt": RULINGS_INDEX + "/individual-rulings-a-c",
    "rulings_individual-rulings-d-e.txt": RULINGS_INDEX + "/individual-rulings-d-e",
    "rulings_individual-rulings-f-h.txt": RULINGS_INDEX + "/individual-rulings-f-h",
    "rulings_individual-rulings-i-k.txt": RULINGS_INDEX + "/individual-rulings-i-k",
    "rulings_individual-rulings-l-o.txt": RULINGS_INDEX + "/individual-rulings-l-o",
    "rulings_individual-card-rulings-p-r.txt": RULINGS_INDEX + "/individual-card-rulings-p-r",
    "rulings_individual-card-rulings-s-t.txt": RULINGS_INDEX + "/individual-card-rulings-s-t",
    "rulings_rules-u-z.txt": RULINGS_INDEX + "/rules-u-z",
}


def item(id_, category, url, expected, new=(), existing=(), status=None, reason="", note="", strength=""):
    return dict(id=id_, category=category, source=url, expected=expected, new_fixtures=list(new),
                existing_fixtures=list(existing), forced_status=status, reason=reason, note=note,
                strength=strength)


NT = "NOT-TESTABLE-YET"
ITEMS = [
    # --- Rule differences
    item("R1.1", "rule", RULES, "The starting player draws in the first Draw Phase, so starts with 6 cards.", existing=["001"]),
    item("R2.1", "rule", RULES, "Activating a Field Spell over the opponent's active one destroys it at resolution.", existing=["003A"]),
    item("R2.2", "rule", RULES, "If the new Field Spell is destroyed in the Chain, the old one is not destroyed.", new=["201"]),
    item("R2.3", "rule", RULES, "Setting a Field Spell does not destroy the opponent's; Village still stops activating it.", new=["202"], note="Only the no-Spellcaster half was tested. See ambiguity A4."),
    item("R2.4", "rule", RULES, "Setting over your own Geartown destroys it with no Chain, so it can trigger.", new=["203A", "203B"]),
    item("R2.5", "rule", RULES, "Activating over your own Geartown starts a Chain, so it cannot trigger.", new=["204A", "204B"]),
    item("R3.1", "rule", RULES, "A monster can be equipped with only 1 Union Monster at a time.", existing=["062"]),
    item("R3.2", "rule", RULES, "A listed Union (Gearframe) is destroyed instead of the equipped monster.", new=["205"]),
    item("R3.3", "rule", RULES, "Unlisted Unions do not have the effect-destruction replacement.", new=["206"], note="See ambiguity A3: Y-Dragon Head's battle-only replacement is not asserted."),
    item("R4.1", "rule", RULES, "A phase-dependent mandatory trigger activates again when its activation is negated by LADD.", existing=["052"], new=["302", "303"]),
    item("R4.2", "rule", RULES, "It does not activate again when its effect is negated by Skill Drain.", existing=["052"]),
    item("R5.1", "rule", RULES, "Cannot activate a Trap Monster with no empty Monster Zone.", new=["211"]),
    item("R5.2", "rule", RULES, "A Trap Monster keeps 1 S/T Zone unusable.", new=["212"]),
    item("R5.3", "rule", RULES, "Gaining control of a Trap Monster needs an open Monster Zone and S/T Zone.", new=["213"]),
    item("R5.4", "rule", RULES, "A Trap Monster negated by Jinzo reverts to the S/T Zone immediately.", new=["214"]),
    item("R5.5", "rule", RULES, "A Trap Monster attacking a Set Jinzo reverts on the flip; no Damage Calculation.", new=["215"]),
    item("R5.6", "rule", RULES, "A stolen Trap Monster returns to its owner's S/T Zone when Jinzo is Summoned.", new=["216"]),
    item("R5.7", "rule", RULES, "Book of Moon Sets a Trap Monster in its corresponding S/T Zone.", new=["217"]),
    item("R5.8", "rule", RULES, "A stolen Trap Monster flipped face-down goes to the controller's S/T Zone.", new=["218"]),
    item("R5.9", "rule", RULES, "Heavy Storm on a Trap Monster: Fake Trap can protect it, My Body cannot.", new=["219A", "219B"]),
    item("R5.10", "rule", RULES, "Lightning Vortex on a Trap Monster: My Body can protect it, Fake Trap cannot.", new=["219C", "219D"], strength="219C is weak: Fake Trap is never offered for a Trap Monster, as 219A shows."),
    item("R5.11", "rule", RULES, "Raigeki Break on a Trap Monster: both can protect it.", new=["219E", "219F"]),
    item("R5.12", "rule", RULES, "A Trap Monster removed from the field is removed as a Trap Card; Caius inflicts no damage on Zoma.", new=["220"]),
    item("R5.13", "rule", RULES, "A Trap Monster in the GY cannot be revived with Time Machine.", new=["225"]),
    item("R5.14", "rule", RULES, "A Trap Monster in the GY cannot be retrieved with Return of the Doomed.", new=["226"]),
    item("R5.15", "rule", RULES, "A Trap Monster in the GY does not trigger Shura.", new=["222"]),
    item("R5.16", "rule", RULES, "A Trap Monster in the GY does not allow Michizure.", new=["221"]),
    item("R5.17", "rule", RULES, "A Trap Monster returned by Penguin Soldier returns as a Trap Card; no Major Riot.", new=["223"]),
    item("R5.18", "rule", RULES, "Temporarily removed by Matter Transporter: returns as a Trap Card and is destroyed immediately.", new=["224"]),
    item("R5.19", "rule", RULES, "Temporarily removed by Dimensionhole: same, returning at the next Standby Phase.", new=["227"]),
    item("R5.20", "rule", RULES, "While Dimensionhole holds it, the Monster Zone is unusable but the S/T Zone it occupied is usable.", status="UNTESTED", reason="Low priority: Trap Monsters are in 0 of 75 sampled decks."),
    item("R6.1", "rule", RULES, "Ignition priority for a monster on the field: Chaos Sorcerer before Bottomless.", new=["232"], existing=["002"]),
    item("R6.2", "rule", RULES, "Ignition priority for a Graveyard effect: Malicious before D.D. Crow.", new=["231", "238", "460A", "460B"]),
    item("R6.3", "rule", RULES, "Ignition priority after a Ritual Summon: Demise before Torrential.", new=["233"]),
    item("R6.4", "rule", RULES, "No ignition priority when Black Garden's trigger starts a Chain.", new=["234"]),
    item("R6.5", "rule", RULES, "No ignition priority when Sangan used as Synchro Material starts a Chain.", new=["236"]),
    item("R6.6", "rule", RULES, "After a triggered Summon the turn player has no Ignition Effect as Chain Link 1, trigger accepted.", new=["235B"]),
    item("R6.7", "rule", RULES, "After a triggered Summon the turn player has no Ignition Effect as Chain Link 1, trigger declined.", new=["235A"]),
    item("R7.1", "rule", RULES, "Sangan Tributed for Caius: Sangan CL1, Caius CL2.", existing=["004B"]),
    item("R7.2", "rule", RULES, "Opponent's Sangan Tributed for Caius: Caius CL1 (Step 1), Sangan CL2 (Step 2).", new=["241"], note="Contradicts existing fixture 004C. See ambiguity A1."),
    item("R7.3", "rule", RULES, "The chronology exception applies only within one step: later mandatory before earlier optional.", new=["242"], note="Contradicts existing fixtures 005, 006, 017, 018, 033. See ambiguity A1."),
    item("R8.1", "rule", RULES, "Counter Traps can be activated at any point in the Damage Step.", new=["251"], existing=["009", "010"]),
    item("R8.2", "rule", RULES, "Monster effects that negate activations can activate at any point in the Damage Step.", new=["306A", "306B"], existing=["009B", "010C"]),
    item("R8.3", "rule", RULES, "Monster effects that only negate effects cannot activate in the Damage Step.", new=["257"], strength="Weak: Effect Veiler is also limited to the opponent's Main Phase by its own text, so this cannot separate the two reasons."),
    item("R8.4", "rule", RULES, "My Body cannot activate in the Damage Step.", existing=["068B"]),
    item("R8.5", "rule", RULES, "Royal Oppression cannot activate in the Damage Step.", new=["418"]),
    item("R8.6", "rule", RULES, "Mandatory effects such as Doomcaliber can activate at any point in the Damage Step.", new=["252"]),
    item("R8.7", "rule", RULES, "Effects can activate if the card moves or acts: Green Gadget from Giant Rat.", new=["253"]),
    item("R8.8", "rule", RULES, "Fast effects that modify ATK/DEF apply; Spell/Trap modifiers cannot activate in or after Damage Calculation.", existing=["008", "009", "010"]),
    item("R8.9", "rule", RULES, "Flip Effects activate in Substep 6, not on the flip.", existing=["007A"]),
    item("R8.10", "rule", RULES, "Substep 5: effects on Battle Damage activate; a further manual Chain forms for Gorz's Token.", new=["255"]),
    item("R8.11", "rule", RULES, "Substep 6: Ryko milling Peten builds another manual Chain.", new=["254"]),
    item("R8.12", "rule", RULES, "Substep 7: Mystic Tomato into another monster builds another manual Chain.", status="UNTESTED", reason="The Green Gadget fixture 253 covers the same mechanism; the exact Armageddon Knight pairing was not run."),
    item("R8.13", "rule", RULES, "Self-destruct continuous effects (Gozen Match, Rivalry of Warlords) apply in Substep 5.", status="UNTESTED", reason="Not reached within this pass."),
    item("R8.14", "rule", BATTLE, "Monsters marked destroyed by battle cannot be returned to the hand.", new=["258"]),
    item("R8.15", "rule", BATTLE, "Flashbang makes battle-damage and end-of-Battle-Phase effects miss timing.", status="UNTESTED", reason="Not reached within this pass."),
    item("R8.16", "rule", BATTLE, "A replay does not reopen the attack declaration; Mirror Force cannot be played.", new=["256"]),
    item("R9.1", "rule", RULES, "Dandylion discarded for Monster Reincarnation and returned still activates from the hand.", new=["261"]),
    item("R9.2", "rule", RULES, "Necroface CL1 with Future Visions: the banish effect still activates in a new Chain, from the Deck.", new=["262B"]),
    item("R9.3", "rule", "https://www.edisonformat.com/home/rules-update-trigger-effect-location-mid-chain-triggers", "Future Visions CL1 with Necroface: Necroface activates in a new Chain from banished.", new=["262A"]),
    item("R9.4", "rule", RULES, "Aslla Piscu returned to the Deck does not activate.", new=["263"]),
    item("R9.5", "rule", RULES, "Absolute Zero returned to the Extra Deck does activate.", new=["264"]),
    item("R9.6", "rule", RULES, "A trigger recognized before leaving its location still activates: Sangan banished by D.D. Crow.", existing=["025B", "043B"]),
    item("R10.1", "rule", RULES, "You cannot pay an activation cost that would reduce your LP to 0.", new=["271A", "271B", "271C"]),
    item("R10.2", "rule", RULES, "Mirror Wall at LP <= 2000: maintenance cannot be paid; it self-destructs.", new=["272"]),
    item("R10.3", "rule", RULES, "Degenerate Circuit at LP <= 500: maintenance cannot be paid; it self-destructs.", new=["273"]),
    item("R10.4", "rule", RULES, "Same for other LP maintenance (Messenger of Peace).", new=["274"], note="Messenger is an application of the stated general rule, not a listed example."),
    item("R11.1", "rule", RULES, "No response window follows the end-of-turn discard.", new=["284"]),
    item("R11.2", "rule", RULES, "A mandatory trigger from the discard: only negating Spell Speed 2 effects and Spell Speed 3 effects can chain. Super Rejuvenation cannot.", new=["281"]),
    item("R11.3", "rule", RULES, "A face-up Doomcaliber adds a Chain Link to that Chain.", new=["283"]),
    item("R11.4", "rule", RULES, "Optional triggers of the discarded card cannot activate.", new=["282"]),
    item("R12.1", "rule", RULES, "A voluntary action that would create an infinite loop is illegal (Example 1: X-Head Cannon).", new=["291"]),
    item("R12.2", "rule", RULES, "Example 2: Ring of Destruction cannot target if resolving it would loop.", status="UNTESTED", reason="Same missing mechanism as R12.1, which already fails."),
    item("R12.3", "rule", RULES, "Example 3: Pole Position cannot be chained if resolution would loop.", status="UNTESTED", reason="Same missing mechanism as R12.1, which already fails."),
    item("R12.4", "rule", RULES, "An involuntary loop destroys Pole Position by game mechanics (Example 4).", new=["292"]),
    item("R12.5", "rule", RULES, "Engine robustness: taking the illegal Summon anyway does not hang.", new=["293"], note="Not a stated rule. Measured because a hang would break training."),
    item("R13.1", "rule", RULES, "Two 0 ATK Attack Position monsters destroy each other.", new=["296"]),
    item("R13.2", "rule", RULES, "A 0 ATK attacker does not destroy a 0 DEF Defense Position monster.", new=["297"]),
    item("P1", "rule", PRIORITY, "After a Chain resolves, the opponent gets priority before the turn player can use an Ignition Effect.", new=["237"]),
    item("P2", "rule", PRIORITY, "On entering a new phase the turn player acts first.", new=["244"]),
    item("P3", "rule", SUMMONS, "Rai-Oh negates an inherent Special Summon.", new=["239"]),
    item("P4", "rule", SUMMONS, "Destroying Oppression in the Chain means the Summon is not negated.", new=["240"]),
    item("P5", "rule", TIMING, "A Summon as Chain Link 2+ is not the last thing to happen, so no Summon response follows it.", new=["423"]),
    item("P6", "rule", TIMING, "A When-you-can trigger misses timing if met while paying a cost or as material.", existing=["021"], strength="Dupe Frog as a Tribute only."),
    item("P7", "rule", TIMING, "An If-you-can trigger used as material activates after the Summon (Arcane Apprentice).", status="UNTESTED", reason="Not reached within this pass."),
    # --- Errata
    item("E01.a", "errata", ERRATA, "Armory Arm: the Colossal Fighter OTK works.", new=["309"]),
    item("E01.b", "errata", ERRATA, "Armory Arm: damage applies even if the destroyed monster leaves the GY.", existing=["061"]),
    item("E02", "errata", ERRATA, "Black Garden activates if a monster is Special Summoned face-down.", existing=["063B"]),
    item("E03.a", "errata", ERRATA, "Brain Control has no modern target restrictions.", existing=["034"], strength="Tested with a Synchro target only."),
    item("E03.b", "errata", ERRATA, "Brain Control: target flipped face-down before resolution means no control change.", existing=["041"]),
    item("E03.c", "errata", ERRATA, "Brain Control: flipped face-down later, control still returns in the End Phase.", existing=["041"]),
    item("E04", "errata", ERRATA, "Brionac: discard is a cost, targets either side, no once-per-turn.", existing=["046"]),
    item("E05", "errata", ERRATA, "Catapult Turtle has no once-per-turn.", existing=["045"]),
    item("E06", "errata", ERRATA, "Cyber Phoenix must be face-up before being selected as an attack target.", existing=["060A", "060B"]),
    item("E07.a", "errata", ERRATA, "D.D. Survivor returns in the End Phase after being removed while face-up.", existing=["044"]),
    item("E07.b", "errata", ERRATA, "D.D. Survivor: the attack-target bullets.", status=NT, reason="The bullets appear copied from Cyber Phoenix and Jade Knight; D.D. Survivor has no battle trigger for them to describe. See ambiguity A6."),
    item("E08", "errata", ERRATA, "Dark End Dragon still loses 500 ATK/DEF if the target leaves first.", existing=["059B"]),
    item("E09", "errata", ERRATA, "Darkness Approaches: face-down Attack Position, no attacks, Flip Summon to Attack.", existing=["049"]),
    item("E10", "errata", ERRATA, "Destiny End Dragoon's trigger can activate more than once per turn.", existing=["065"], strength="Only shown after a negated first activation."),
    item("E11", "errata", ERRATA, "Prisma reveals and sends at resolution, not as a cost; nothing is sent if it is not face-up.", existing=["053"]),
    item("E12", "errata", ERRATA, "Fortune Lady Light's second trigger activates when leaving the field face-down.", existing=["057B"]),
    item("E13.a", "errata", ERRATA, "Future Fusion sends materials at resolution.", existing=["058"]),
    item("E13.b", "errata", ERRATA, "Future Fusion cannot be activated if you cannot Special Summon.", existing=["058B"]),
    item("E14", "errata", ERRATA, "Goyo Guardian accepts non-EARTH Tuners.", existing=["035B"]),
    item("E15", "errata", ERRATA, "Jade Knight must be face-up before being selected as an attack target.", existing=["060C", "060D"]),
    item("E16", "errata", ERRATA, "LADD resolves sequentially: destroy all your cards, then revive.", new=["301"], existing=["073"]),
    item("E17", "errata", ERRATA, "Light End Dragon still loses 500 ATK/DEF if the opposing monster leaves first.", existing=["059D"]),
    item("E18.a", "errata", ERRATA, "Lightsworn phase trigger activates again after LADD: Lumina.", existing=["052"]),
    item("E18.b", "errata", ERRATA, "Applies to Judgment Dragon too.", new=["302"]),
    item("E19", "errata", ERRATA, "Machina Gearframe: only 1 Union per monster.", existing=["062"]),
    item("E20", "errata", ERRATA, "Mark of the Rose: both triggers start Chains; Cold Wave stops the second.", existing=["064"]),
    item("E21.a", "errata", ERRATA, "Mausoleum uses your Normal Summon or Set for the turn.", new=["307"], existing=["071B"], note="Existing 069B's check is vacuous because its hand is empty; 307 replaces it for the Summon path."),
    item("E21.b", "errata", ERRATA, "Mausoleum: LP is a cost, Summon at resolution, Solemn cannot negate, Torrential can respond.", existing=["069B", "071"]),
    item("E22.a", "errata", ERRATA, "My Body can chain to a face-up Royal Oppression effect.", existing=["067"], note="Positive case is Oppression threatening an on-field monster. See ambiguity A5."),
    item("E22.b", "errata", ERRATA, "My Body cannot activate in the Damage Step.", existing=["068B"]),
    item("E22.c", "errata", RULINGS_INDEX + "/individual-rulings-l-o", "My Body cannot negate Heavy Storm that would destroy a monster treated as an Equip Spell.", new=["308"]),
    item("E23.a", "errata", ERRATA, "Necrovalley negates targeting effects; Treeborn is unaffected.", existing=["047"]),
    item("E23.b", "errata", ERRATA, "Necrovalley does not negate Rekindling or REDMD.", new=["304A", "304B"]),
    item("E23.c", "errata", ERRATA, "Graveyard Types and Attributes may change under Necrovalley if the effect does not target.", status="UNTESTED", reason="Low priority: DNA Transplant-style interaction, not seen in sampled decks."),
    item("E24", "errata", ERRATA, "Quickdraw Synchron performs a Summon; sends at resolution, not as a cost.", existing=["051"]),
    item("E25", "errata", ERRATA, "REDMD: no once-per-name limits; Ignition does not target.", existing=["056"]),
    item("E26", "errata", ERRATA, "Rescue Cat does not negate Summoned effects; no once-per-name.", existing=["054"]),
    item("E27", "errata", ERRATA, "Ryko: optional target; destroys then mills sequentially.", existing=["050"]),
    item("E28", "errata", ERRATA, "Sangan: the searched monster's effects can be activated.", existing=["019A"]),
    item("E29", "errata", ERRATA, "Soul Exchange: Tributing the target is optional, not forced at the earliest chance.", existing=["066"]),
    item("E30", "errata", ERRATA, "Strike Ninja: multiple copies can use the effect in one turn.", existing=["055"]),
    item("E31.a", "errata", ERRATA, "Spirit trigger activates again after LADD: Susa Soldier.", existing=["052"]),
    item("E31.b", "errata", ERRATA, "Applies to all Spirit Monsters.", new=["303"], strength="Asura Priest as one further sample, not all Spirits."),
    item("E32.a", "errata", ERRATA, "Swap Frog: each copy once.", new=["305"], existing=["072"], note="Existing 072 and 076 cannot detect a same-copy repeat because each copy returns itself."),
    item("E32.b", "errata", ERRATA, "Swap Frog: only 1 additional Normal Summon.", existing=["072", "076"]),
    item("E33.a", "errata", ERRATA, "Treeborn has no once-per-turn.", existing=["048"]),
    item("E33.b", "errata", ERRATA, "Treeborn cannot activate if a monster is treated as a Spell in your S/T Zone.", existing=["074"]),
    item("E34", "errata", ERRATA, "Ultimate Offering: Summon at resolution; Solemn cannot negate; Torrential can respond if CL1.", existing=["069", "071"], note="The Solemn ruling list conflicts with itself here. See ambiguity A2."),
    item("E35", "errata", ERRATA, "Urgent Tuning: Summon at resolution; Solemn negates only the activation.", existing=["070"]),
]

# Metagame per-card rulings. Key: (card, text fragment of the ruling) -> fixtures.
RULING_MAP = [
    ("Stardust Dragon", "even in the Damage Step", ["401"], []),
    ("Stardust Dragon", "Deck Devastation Virus", ["402A"], []),
    ("Stardust Dragon", "would destroy a card on the field", ["403B"], []),
    ("Stardust Dragon", "would negate a Contact Fusion", ["403A"], []),
    ("Stardust Dragon", "only activates", ["404A", "404B"], []),
    ("Black Rose Dragon", "Flip Effect will activate", ["405"], []),
    ("Brionac, Dragon of the Ice Barrier", "multiple times per turn", [], ["046"]),
    ("Brionac, Dragon of the Ice Barrier", "is a cost", [], ["046"]),
    ("Brionac, Dragon of the Ice Barrier", "Macro Cosmos", ["406"], []),
    ("Ally of Justice Catastor", "will not activate when attacking a face-down", ["407A", "407B"], []),
    ("Ally of Justice Catastor", "multiple Attributes", ["407C"], []),
    ("Goyo Guardian", "do not need to be EARTH", [], ["035B"]),
    ("Goyo Guardian", "equal ATK", ["408"], []),
    ("Goyo Guardian", "Flip Effect (ex:", ["409"], []),
    ("Colossal Fighter", "same \"Colossal Fighter\"", ["309"], []),
    ("Torrential Tribute", "cannot be activated during the Damage Step", ["412"], []),
    ("Brain Control", "must remain face-up", [], ["041"]),
    ("Brain Control", "resolves without effect", [], ["041"]),
    ("Brain Control", "still returned during the End Phase", [], ["041"]),
    ("Brain Control", "all occupied", ["414"], []),
    ("Gorz the Emissary of Darkness", "chain the effect of \"Tragoedia\"", ["417"], []),
    ("Gorz the Emissary of Darkness", "while your monster battled", ["416"], []),
    ("Gorz the Emissary of Darkness", "removes from play your only card", ["415"], []),
    ("Gorz the Emissary of Darkness", "cannot be negated by \"Royal Oppression\"", ["418"], []),
    ("Chimeratech Fortress Dragon", "face-down Fusion Material", ["420"], []),
    ("Chimeratech Fortress Dragon", "\"Skill Drain\" is active", ["421"], []),
    ("Dimensional Prison", "\"Book of Moon\" is used on an attacking monster", ["422"], []),
    ("Bottomless Trap Hole", "Chain Link 2+", ["424"], []),
    ("Bottomless Trap Hole", "\"Monster Reborn\" and chains", ["425"], []),
    ("Dark Armed Dragon", "from the Graveyard (by \"Call of the Haunted\")", ["426"], []),
    ("Dark Armed Dragon", "multiple times", ["427"], []),
    ("Armory Arm", "VS. \"Colossal Fighter\"", ["309"], []),
    ("Armory Arm", "even if it leaves the Graveyard", [], ["061"]),
    ("Solemn Judgment", "rounded up", ["431"], []),
    ("Solemn Judgment", "DO NOT negate the Normal Summon from \"Ultimate Offering\"", [], ["069"]),
    ("Solemn Judgment", "DO NOT negate the Synchro Summon after", [], ["070"]),
    ("Dust Tornado", "did not destroy a Spell or Trap Card", ["432B"], []),
    ("Dust Tornado", "it is not negated", ["432A"], []),
    ("Dark End Dragon", "cannot lose 500", ["433"], []),
    ("Dark End Dragon", "leaves the field before", [], ["059B"]),
    ("Royal Oppression", "can't be activated in the Damage Step", ["418"], []),
    ("Royal Oppression", "Debris Dragon", ["435"], []),
    ("Necro Gardna", "automatically negates", ["437"], []),
    ("Necro Gardna", "all negate your opponent's next attack", ["438"], []),
    ("Card Trooper", "destroyed while face-down", ["439A"], []),
    ("Card Trooper", "under your opponent's control", ["439B"], []),
    ("Plaguespreader Zombie", "does not target", ["441C"], []),
    ("Plaguespreader Zombie", "\"Caius the Shadow Monarch\" will not inflict", ["441A"], []),
    ("Plaguespreader Zombie", "flipped face-down", ["441B"], []),
    ("Caius the Shadow Monarch", "Battle Fader", ["447"], []),
    ("Caius the Shadow Monarch", "Token Monsters, Trap Monster", ["220"], []),
    ("Caius the Shadow Monarch", "Bottomless Trap Hole", ["446"], []),
    ("Ryko, Lightsworn Hunter", "always the last thing", ["448"], []),
    ("Ryko, Lightsworn Hunter", "Example: Player A Flip Summons", ["448"], []),
    ("Ryko, Lightsworn Hunter", "resolves sequentially", [], ["050"]),
    ("Pulling the Rug", "Ignition Effect Priority", ["449A", "449B"], []),
    ("Blackwing - Gale the Whirlwind", "Example 2", ["411"], []),
    ("Blackwing - Sirocco the Dawn", "Main Phase 2", ["450"], []),
    ("Blackwing - Sirocco the Dawn", "Monsters Summoned after", ["451"], []),
    ("Blackwing - Sirocco the Dawn", "is negated, then your other monsters may attack", ["452"], []),
    ("Blackwing - Vayu the Emblem of Honor", "Example: Player A activates", ["455"], []),
    ("Blackwing - Vayu the Emblem of Honor", "not a cost", ["455"], []),
    ("Blackwing - Vayu the Emblem of Honor", "cannot be Special Summoned from", ["456"], []),
    ("Book of Moon", "once per turn", ["459"], []),
    ("Deck Devastation Virus", "You CAN activate this card if at least one face-up monster", ["402B"], []),
    ("Deck Devastation Virus", "You CANNOT activate this card if your monster(s)", ["402C"], []),
]
SIGNALS = ("cannot", "can't", "will not", "won't", " can ", "does not", "do not", "not ", "even if", " if ", "must ", "only ", "cost", "instead", "still ")
FAIL_LABELS = {"PASS", "FAIL", "UNTESTED", NT}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results", type=Path, default=ROOT / "build/edison-conformance/results/results.json")
    parser.add_argument("--certification", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=REPORT)
    parser.add_argument("--extract", type=Path, default=ROOT / "build/edison-rules-conformance-2026-11-04/metagame-rulings-extract.json")
    args = parser.parse_args()
    data = json.loads(args.results.read_text())
    cases = {c["case"]: c for c in data["cases"] if c["kind"] == "rule"}
    controls = {c["case"]: c for c in data["cases"] if c["kind"] == "control"}
    certified = json.loads(args.certification.read_text())["status"] == "PASS"
    extract = json.loads(args.extract.read_text())

    items = [dict(i) for i in ITEMS]
    used = {}
    for card, entry in extract.items():
        url = PAGE_URL.get(entry["page"], RULINGS_INDEX)
        for n, line in enumerate(entry["lines"], 1):
            mapped = [m for m in RULING_MAP if m[0] == card and m[1] in line]
            for m in mapped:
                used[(m[0], m[1])] = True
            new = sorted({x for m in mapped for x in m[2]})
            old = sorted({x for m in mapped for x in m[3]})
            claim = any(s in f" {line.lower()} " for s in SIGNALS)
            forced, reason = None, ""
            if not (new or old):
                forced = "UNTESTED" if claim else NT
                reason = "Not reached within this pass." if claim else "Keyword heuristic, not reviewed line by line: reads as description with no distinguishing behaviour to assert."
            items.append(item(f"C-{card.split(',')[0].replace(' ', '')}-{n}", "card-ruling", url, line, new=new, existing=old,
                              status=forced, reason=reason, note=f"{entry['decks']} of 75 sampled decks"))
    unmapped = [m[:2] for m in RULING_MAP if (m[0], m[1]) not in used]
    if unmapped:
        raise SystemExit(f"Ruling map entries matched nothing: {unmapped}")

    referenced = set()
    for it in items:
        for key in it["new_fixtures"]:
            referenced.add(key)
            if key not in cases:
                raise SystemExit(f"{it['id']} names a fixture with no result: {key}")
        statuses = [cases[k]["status"] for k in it["new_fixtures"]]
        if it["forced_status"]:
            it["status"] = it["forced_status"]
        elif any(s != "PASS" for s in statuses):
            it["status"] = "FAIL"
        elif statuses:
            it["status"] = "PASS"
        elif it["existing_fixtures"]:
            it["status"] = "PASS" if certified else "UNTESTED"
        else:
            raise SystemExit(f"{it['id']} has no fixture and no forced status")
        it["evidence"] = []
        for k in it["new_fixtures"]:
            c = cases[k]
            r5 = controls.get(k, {})
            same = any(o.startswith("Rule 5 meets") for o in r5.get("observed", []))
            it["evidence"].append({
                "fixture": k, "result": c["status"], "observed": c["observed"], "message": c.get("message") if c["status"] != "PASS" else None,
                "rule5": "meets the same expectation" if same else "differs",
            })
        if it["existing_fixtures"]:
            it["evidence"].append({"fixture": ",".join(it["existing_fixtures"]), "result": "certification", "verified_by": "assertions read by audit; see report"})
        del it["forced_status"]
        assert it["status"] in FAIL_LABELS
    orphan = sorted(set(cases) - referenced)
    if orphan:
        raise SystemExit(f"Fixtures not referenced by any matrix item: {orphan}")

    totals = collections.Counter((i["category"], i["status"]) for i in items)
    categories = ["rule", "errata", "card-ruling"]
    summary = {
        c: {s: totals[(c, s)] for s in ("PASS", "FAIL", "UNTESTED", NT)} for c in categories
    }
    summary["fixtures"] = {"rule_cases": data["summary"]["rule_cases"], "pass": data["summary"]["rule_pass"], "fail": data["summary"]["rule_fail"]}
    output = {
        "generated_from": {"results": str(args.results.relative_to(ROOT)) if args.results.is_absolute() and ROOT in args.results.parents else str(args.results),
                           "certification_status": "PASS" if certified else "FAIL"},
        "target_ruleset": "Traditional Edison as written at edisonformat.com; not the 2025 Time Wizard variant.",
        "status_meaning": {"PASS": "A test proves it.", "FAIL": "A test proves a mismatch.", "UNTESTED": "No test.", NT: "Cannot be tested as stated; see reason."},
        "summary": summary,
        "items": items,
    }
    (args.output / "conformance-matrix.json").write_text(json.dumps(output, indent=1) + "\n")

    lines = ["# Rules-conformance matrix", "", "Generated by `tests/edison/conformance/build_matrix.py`. See `docs/edison-rules-conformance.md`.", "",
             "| Category | PASS | FAIL | UNTESTED | NOT-TESTABLE-YET |", "| --- | ---: | ---: | ---: | ---: |"]
    for c in categories:
        s = summary[c]
        lines.append(f"| {c} | {s['PASS']} | {s['FAIL']} | {s['UNTESTED']} | {s[NT]} |")
    lines.append("")
    for c in categories:
        lines += [f"## {c}", "", "| ID | Status | Expected | New fixtures | Existing fixtures | Notes |", "| --- | --- | --- | --- | --- | --- |"]
        for i in items:
            if i["category"] != c:
                continue
            notes = " ".join(x for x in (i["reason"], i["note"], i["strength"]) if x)
            text = i["expected"].replace("|", "\\|")
            lines.append(f"| {i['id']} | {i['status']} | {text} | {', '.join(i['new_fixtures'])} | {', '.join(i['existing_fixtures'])} | {notes.replace('|', chr(92) + '|')} |")
        lines.append("")
    (args.output / "conformance-matrix.md").write_text("\n".join(lines))
    print(json.dumps(summary, indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
