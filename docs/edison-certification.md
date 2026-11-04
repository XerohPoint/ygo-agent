# Deterministic Edison rules certification

This suite tests draw, priority, Field Spell, trigger, and battle rules against the installed legacy
YGOPro core. It does **not** certify the simulator as fully Edison-correct.
Normal self-play still uses `reset() -> new_duel() -> load_deck()`; fixtures run in
a separate executable with no Python environment pool, agents, or training code.

The full suite now passes **519 cases, 0 failures**, with two identical runs
against the final build. Tests 001–076 provide 517 rules/deck checks; Test 077 adds
two production regressions from complete seeded play. See
[the production baseline](edison-baseline.md) for the committed report, legal
fixed decks, 64 distinct seeded matchup duels, replay checks, and measured rates.

Sections below retain earlier focused-run checkpoints as historical evidence.
Those earlier counts and build fingerprints are superseded by the full report
for the current checkpoint.

## Run

From the repository root:

```bash
source .venv/bin/activate
xmake f -y
xmake b ygopro_ygoenv
xmake b edison_rules
python tests/edison/run.py
```

To run only one certification test and all of its Rule 5/comparison controls:

```bash
python tests/edison/run.py --test 014
```

The native equivalent is `edison_rules DATABASE SCRIPT_DIRECTORY --test=014`.
Test numbers must have three digits. Unmatched test numbers fail. Focused runs
default to `build/edison-certification/test-014/` (substitute the selected
number), preserving previous full-suite evidence; `--output` overrides this.
Reports include `scope` and `test_id` so selected results are distinguishable
from a full-suite run.

Add `--fail-fast` to stop at the first failed case and skip replay of a
failed run. This is used for the current add-test, fix-failure, and continue workflow:

```bash
python tests/edison/run.py --test 017 --fail-fast
```

The runner requires only Python's standard library. It verifies the database and
88 upstream Lua files against `tests/edison/assets.lock.json`, runs the native executable
twice in fresh processes, and compares every protocol event and response. Each
process has a 30-second timeout; the native driver also bounds process calls.
Missing/changed assets, Lua diagnostics, unknown/truncated messages, rejected
responses (`MSG_RETRY`), and assertion failures produce a nonzero exit status.

Default outputs (ignored by Git):

- `build/edison-certification/report.json`: result, seed, version pins, command,
  asset/source/executable SHA-256 hashes, exit codes, and trace hashes.
- `build/edison-certification/run-1.txt` and `run-2.txt`: complete event/response
  traces, observed checkpoints, and individual PASS/FAIL results.

Use `--binary`, `--database`, `--scripts`, or `--output` for other build/asset
locations. Rebuild after changing native sources. The core pin comes from the
XMake recipe, not a runtime version API; the executable hash identifies the
actual statically linked artifact. The installed package inspected for this
implementation is `ygopro-core 0.0.2`, with Lua `v5.5.1`.

## Fixtures and assertions

`tests/edison/harness.h` provides `Placement{code, owner, controller, location,
sequence, position}`. The core's `playerid` means controller; ownership is a
separate field. Placements use `new_card` before `start_duel`, with insertion
count checks. Sequence follows the pinned core's zone insertion semantics.
Field setup does not raise a Summon event: Test 002 actually chooses a Normal
Summon through the engine's idle-action prompt.

The fixtures are deliberately small synthetic states, **not legal deck lists**.
Deck size and copy limits are outside their scope. Only forty-three card identities are
loaded from the existing database; the rest of its modern card pool is unused.
Upstream scripts remain pinned; the shared historical override adapts Sangan, Honest, Kalut,
Brain Control, and Goyo Guardian for Rule 1. The pinned core carries the
Early Trigger patch described below. Seed `20100424` is expanded through
`std::mt19937` exactly as in `YGOProEnvImpl::YGO_CreateDuel`.

### Test 001 — first-turn draw

Both players receive twelve identical Blue-Eyes White Dragon deck fillers
(`89631139`). Shared production settings provide 8000 LP, five opening cards,
one draw per turn, and rule 1. Assertions check both opening-draw messages, both
five-card hand zones before the first turn, P0 entering the Draw Phase and drawing
exactly one card, then hand counts 6/5 and unchanged LP at Main Phase 1.
The rule-5 control requires zero first-turn draws and hand counts 5/5.

### Test 002 — ignition priority

Opening-hand and automatic draw counts are zero for this exact-state fixture:

| Zone | Card |
| --- | --- |
| P0 hand | Bazoo the Soul-Eater (`40133511`) |
| P0 Graveyard | Blue-Eyes White Dragon (`89631139`) |
| P1 S/T zone 0 | Bottomless Trap Hole (`29401950`), face-down |

The preplaced trap represents a card Set on a previous turn. Bazoo is Normal
Summoned to P0 monster zone 0 in face-up Attack Position. Its 1600 ATK makes
Bottomless legal. The driver requires this sequence:

1. Successful Summon event, with Bazoo actually on the field.
2. P0 offered Bazoo's ignition effect in the Summon-response window, before
   any opponent Bottomless offer or return to the idle-action prompt.
3. Select the Graveyard monster as cost; verify it was banished.
4. Core confirms Bazoo as CL1, then offers P1 Bottomless, which becomes CL2.
5. Resolution order 2, 1; Bazoo banished and Bottomless in P1's Graveyard.

`MSG_CHAINING` announces an activation **before** the core runs its Lua cost;
`MSG_CHAINED` confirms it after cost/target processing. The cost assertion runs
at confirmation and before the opponent's response, not at announcement.

Under rule 5, the same fixture requires no Bazoo ignition offer, Bottomless CL1,
and the cost monster remaining in the Graveyard. This proves that the test
distinguishes historical priority from ordinary Main Phase ignition activation.

Historical basis (checked November 1, 2026):

- [Edison priority rules](https://www.edisonformat.com/priority.html): after a
  successful Summon without triggered effects, the turn player may start a chain
  with an ignition effect.
- [Edison card rulings A–C](https://www.edisonformat.com/rulings/individual-rulings-a-c):
  Bazoo banishes monsters as activation cost; Bottomless responds to an opponent's
  Summon of a monster with at least 1500 ATK.
- [Konami's Bazoo print history](https://www.db.yugioh-card.com/yugiohdb/card_search.action?cid=5250&ope=2&request_locale=en)
  confirms its pre-2010 TCG availability. The [March 2010 list](https://www.edisonformat.com/banlist.html)
  allows the single-copy interaction.

The Bazoo/Bottomless expectation is an application of those rules, not a claim
that the cited page explicitly lists this exact pair. The pinned Bazoo script
registers `EFFECT_TYPE_IGNITION`, with a Graveyard-monster banish cost; the pinned
Bottomless script registers `EVENT_SUMMON_SUCCESS` and checks face-up ATK >=1500.
Bazoo has no Summon-triggered effect to occupy CL1 first.

### Test 003 — single-active-Field-Spell replacement

Each case starts with one face-up Yami (`59197169`) in either player's Field
Zone and Forest (`87430998`) in P0's hand. Automatic draws are disabled and no
monsters are present. Both pinned scripts register a plain activation plus
continuous ATK/DEF modifiers, with no destruction effects or activation costs.
Their effects cannot themselves cause replacement in these fixtures.

P0 selects Forest's activation or Set action from the engine's idle prompt.
The four cases each have a rule-5 control:

| Case | Action | Edison expectation | Rule-5 expectation |
| --- | --- | --- | --- |
| 003A | Activate Forest while P1 controls Yami | Yami survives activation, then is destroyed during resolution | Both Field Spells remain face-up |
| 003B | Set Forest while P1 controls Yami | Yami remains face-up; Forest is face-down | Same |
| 003C | Activate Forest while P0 controls Yami | Yami is destroyed before Forest is placed/activated | Yami is sent to the Graveyard without destruction |
| 003D | Set Forest while P0 controls Yami | Yami is destroyed before Forest is placed | Yami is sent to the Graveyard without destruction |

The assertions inspect decoded `MSG_MOVE` source/destination locations and
`REASON_RULE`/`REASON_DESTROY` flags, plus activation and resolution events.
At activation confirmation, an opponent's original Field Spell must still be
present. At the next idle prompt, actual card identities, Field Zone occupancy,
face-up/face-down positions, hand and Graveyard counts, and unchanged LP must
match expectations. Set cases must not start a chain. Case 003B is deliberately
the same under both rules: setting in the other player's zone does not replace
the active Field Spell.

The legacy C API represents Field Zones as `LOCATION_SZONE`, sequence `5`.
`LOCATION_FZONE` is a wider Lua selector, not a valid uint8 C API location.
The core requests a destination for a Set even when only the Field Zone is
available; the driver validates the exact zone mask and responds explicitly.

Sources:

- [Edison single-Field-Spell rulings](https://edisonformat.net/rules/compendium/Single-Field-Spell)
  distinguish opponent replacement at resolution, own replacement, and setting.
- [Konami's Master Rule 3 changes](https://www.yugioh-card.com/japan/howto/masterrule3/)
  explain coexistence of opposing Field Spells and sending one's own previous
  Field Spell to the Graveyard without destruction when activating or setting
  another. These rules also apply to the rule-5 controls.
- Konami's print histories for
  [Yami](https://www.db.yugioh-card.com/yugiohdb/card_search.action?cid=4341&ope=2&request_locale=en)
  and [Forest](https://www.db.yugioh-card.com/yugiohdb/card_search.action?cid=4336&ope=2&request_locale=en)
  establish their pre-Edison availability.

Negated activations, destruction of the new Field Spell before it resolves,
and card-specific destruction triggers such as Geartown remain untested. The
suite certifies these four replacement scenarios, not every Field Spell ruling.

### Test 004 — SEGOC / Early Trigger ordering

**Corrected November 4, 2026.** The owner ruled that SEGOC steps come first and chronology applies only inside
one step. 004B is unchanged. Tests 004C, 005, 006, 017, 018 and 033 were changed, and the text below that
describes them records their earlier expectation where it says Edison offers the earlier trigger first. See
[the trigger-order report](edison-trigger-order.md).

The [Edison SEGOC / Early Trigger rules](https://edisonformat.net/rules/compendium/SEGOC-And-Early-Trigger)
require mandatory turn-player triggers, mandatory opponent triggers, optional
turn-player triggers, then optional opponent triggers for simultaneous events.
Earlier events take precedence: Tributing Sangan for Caius requires Sangan CL1
and Caius CL2, rather than a choice between them.

004A gives each player a Sangan and Dupe Frog, then Normal Summons Bazoo and
activates a previously Set Torrential Tribute. After it destroys all five
monsters, the test accepts both optional effects and requires the confirmed
chain P0 Sangan, P1 Sangan, P0 Dupe Frog, P1 Dupe Frog. A Treeborn Frog in each
Deck makes the optional search legal. The Rule 5 control requires the same
order: ordinary SEGOC is not the historical difference.

004B Normal Summons Caius by Tributing the sole P0 monster, Sangan. It checks
the Tribute and successful Summon before requiring Sangan as the only initial
mandatory trigger offered. Caius targets P1's Blue-Eyes. The Rule 5 comparison
requires both triggers offered and deliberately chooses Caius CL1, Sangan CL2.

**Corrected incompatibility:** the unpatched core offered both Caius and Sangan
under Rule 1. The repository patch now automatically processes Sangan first.
004B retains its original ordering assertions; it does not select Sangan from
an incorrectly unrestricted prompt. Rule 5 still offers both and accepts
Caius-first ordering.

004C first resolves Brain Control on an opponent-owned Sangan, then Tributes
it for Caius. It requires P1 Sangan CL1 before P0 Caius CL2 under Rule 1.
The Rule 5 control requires P0 Caius CL1 before P1 Sangan CL2. This guards
chronology overriding turn-player precedence across different event batches.
Brain Control pays 800 LP; the fixture checks actual control transfer before
Tributing. Its later card errata do not affect targeting Sangan in this case.

These tests stop at confirmation of the trigger chain, before resolving its
effects. The existing Sangan script contains later errata (once-per-turn limit
and a restriction on activating the searched card). Its mandatory field-to-GY
trigger suffices for this timing test, but historical Sangan resolution is
explicitly **not certified**. At the original Test 004 checkpoint, scripts were unchanged. Test 019 later added the shared historical Sangan override.

The six added scripts (Sangan, Caius, Torrential Tribute, Dupe Frog, Treeborn
Frog, Brain Control) are hashed from the same existing script checkout. The new
`tests/edison/trigger_tests.h` is included in the report's source hashes.
The runner now repeats failing suites too, recording both exit codes and trace
hashes while retaining a FAIL overall status.

### Test 005 — earlier optional versus later mandatory trigger

P0 activates Dark World Dealings (`74117290`) with Broww, Huntsman of Dark
World (`79126789`) in hand and Watapon (`87774234`) on top of the Deck.
A Blue-Eyes filler is beneath Watapon for Broww's later draw. P1 has one
Blue-Eyes in the Deck and a face-up Ceremonial Bell (`20228463`).

Dealings actually resolves: both players draw, then P0 discards Broww and P1
discards Blue-Eyes. The driver verifies both draw messages precede the discard
moves, which must carry effect/discard reasons and not cost. Thus Watapon's
optional trigger condition is met before Broww's mandatory trigger condition;
neither activates during the resolving setup chain.

Ceremonial Bell keeps hands public. The driver checks Watapon is still face-up
in hand at the optional decision. This isolates chronological ordering from
the pinned core's separate handling of nonpublic hand triggers; it does not
certify the same interaction without Bell. Watapon's pinned script uses an
optional `EVENT_TO_HAND` trigger with `EFFECT_FLAG_DELAY`, so it remains eligible
after Dealings' subsequent discard. Broww uses a mandatory `EVENT_TO_GRAVE`
trigger conditioned on discarding by effect.

| Case | Edison expectation | Rule 5 comparison |
| --- | --- | --- |
| 005A: accept | Confirm Broww CL1 (Step 1) first, then offer Watapon (Step 3) as CL2 | Confirm Broww CL1 first, then offer Watapon as CL2 |
| 005B: decline | Confirm Broww CL1 first, then offer and decline Watapon | Confirm Broww CL1 first, then offer and decline Watapon |

Both decline cases ultimately have only Broww CL1. The test therefore asserts
**when the optional choice occurs**, not just the final chain. It rejects
skipping or repeating that choice, activating a declined effect, unexpected
ordering choices, or resolving before all trigger decisions are complete.

All four cases continue through reverse chain resolution to the next idle
prompt. Broww must draw exactly one Blue-Eyes. Accepted Watapon must be Special
Summoned to P0 monster zone 0 in Attack Position; declined Watapon must remain
in hand. Final card identities, zone counts, empty Decks, Bell's presence, and
unchanged 8000/8000 LP are checked. No card, script, or effect is injected after
duel start.

The expected historical ordering is an application of the general
[Edison Early Trigger rule](https://edisonformat.net/rules/compendium/SEGOC-And-Early-Trigger),
not an explicit published ruling for this four-card fixture. Konami's card
texts and print histories establish the effects and pre-Edison availability:
[Dealings](https://www.db.yugioh-card.com/yugiohdb/card_search.action?cid=7002&ope=2&request_locale=en),
[Watapon](https://www.db.yugioh-card.com/yugiohdb/card_search.action?cid=6092&ope=2&request_locale=en),
[Broww](https://www.db.yugioh-card.com/yugiohdb/card_search.action?cid=6502&ope=2&request_locale=en),
and [Ceremonial Bell](https://www.db.yugioh-card.com/yugiohdb/card_search.action?cid=4928&ope=2&request_locale=en).

The four additional scripts are hash-pinned from the existing checkout.
`tests/edison/optional_trigger_tests.h` is included in the report's source
hashes. The harness now decodes hand confirmations, Special Summon events,
and battle-position prompts. No core patch or production code changes were
needed for Test 005.

### Test 006 — delayed triggers across resolving chain links

This reuses the Test 005 driver with a two-link setup: P0 activates Dark World
Dealings as CL1 and chains a previously Set Jar of Greed (`83968380`) as CL2.
The Deck is Watapon above two Blue-Eyes fillers. Jar resolves first and draws
Watapon. Dealings then draws a filler and discards Broww. Ceremonial Bell keeps
Watapon public throughout.

Assertions require both setup links to be confirmed, resolve in order 2 then 1,
and end before either monster's trigger can activate or be offered. The draw
and discard messages establish chronology independently of the eventual trigger
order. After setup ends:

- 006A accepts Watapon: both rules require Broww CL1, Watapon CL2.
- 006B declines Watapon: both rules confirm Broww first, then the decision. Both
  resolve only Broww.

All four cases verify the resulting trigger chain resolves, the draw occurs
exactly once, Watapon is summoned or retained as chosen, and all final hand,
Graveyard, field, Deck, and LP states match. The additional filler remains in
hand. Jar and Dealings must both be in the Graveyard.

This applies the same Early Trigger interpretation as Test 005 to events from
different resolving links. Watapon's delay flag preserves eligibility through
the later link; the test does not confuse a delayed trigger with an optional
“when” effect that misses timing. Hidden-hand rules, negated setup links, and
further chains generated during trigger resolution remain outside this fixture.

### Test 007 — Damage Step events and activation windows

The driver ends P0's first turn, enters P1's Battle Phase on turn 2, and attacks
through the engine's battle-action prompt with a preplaced Blue-Eyes. No direct
Damage Step calls or mid-duel fixture injections are used.

| Case | P0 defender | Required chronology and outcome |
| --- | --- | --- |
| 007A | Face-down Dekoichi (`87621407`) | Flip face-up, calculate battle result, activate and resolve its Flip draw, then send Dekoichi to the Graveyard by battle; no LP loss |
| 007B | Attack Position Mystic Tomato (`83011277`) | Calculate battle result, inflict 1600 damage, send Tomato to the Graveyard, offer and accept its optional trigger, recruit Sangan from the Deck in Attack Position |
| 007C | Attack Position Mystic Tomato | Same battle damage/destruction, then decline the recruiter; no chain or Special Summon; Sangan stays in the Deck |

Each case has a Rule 5 comparison with the **same** expected sequence. These
fundamental battle windows should be preserved by the Early Trigger patch.
Sangan is only a recruitment target here; its modern search errata do not
participate.

P0 also has a Set Jar of Greed. It must be offered at attack declaration, where
the driver passes, and must never be offered between Damage Step start/end
messages. At least one P0 response prompt inside that interval must occur
while its Deck is nonempty, so the negative control cannot pass merely because
drawing became impossible. The trap stays Set.

The decoder reads battle commands, attack locations, battle ATK/DEF and
destruction flags, battle damage, position changes, and Damage Step boundaries.
Tests verify chain announcement/confirmation/resolution, battle destruction
reasons (not effect destruction), final LP and zones, and return to the battle
prompt after the Damage Step. Dekoichi's Flip effect must not activate merely
when flipped, and Tomato must not activate before reaching the Graveyard.

Historical basis: [Konami's version 7.02 rulebook, printed page 34](https://www.edisonformat.com/uploads/1/3/4/1/134149181/yugioh5dsofficialrulebook.pdf)
places battle Flip effects after damage calculation and limits Damage Step
activations. The [Edison Damage Substeps](https://edisonformat.net/rules/compendium/Damage-Substeps)
place Flip resolution before sending battle-destroyed monsters to the Graveyard
and battle-destruction triggers afterward. [Konami's modern Damage Step guide](https://www.yugioh-card.com/eu/play/damage-step-rules/)
supports the same sequence for the comparison cases. These assertions cover
those shared windows, not equivalence of the entire historical and modern
Damage Steps.

The unchanged pinned scripts and pre-Edison print histories are recorded for
[Jar of Greed](https://www.db.yugioh-card.com/yugiohdb/card_search.action?cid=5210&ope=2),
[Dekoichi](https://www.db.yugioh-card.com/yugiohdb/card_search.action?cid=6201&ope=2&request_locale=en),
and [Mystic Tomato](https://www.db.yugioh-card.com/yugiohdb/card_search.action?cid=4930&ope=2&request_locale=en).
Their script hashes join the assets lock; `damage_step_tests.h` joins the
report's source hashes. No core or production changes were needed.

### Test 008 — ATK/DEF modification windows

These fixtures end P0's first turn and attack P0's face-down Dekoichi on P1's
turn. P0 has Set Rush Recklessly (`70046172`), Castle Walls (`44209392`),
and Forbidden Chalice (`25789292`). All three must actually be offered during
the Damage Step before calculation. They must never be offered during
calculation or afterward, even though a valid face-up target remains.

| Case | Action | Required result |
| --- | --- | --- |
| 008A | Activate Rush at Damage Step start, before the defender flips | Blue-Eyes gains 700 ATK; battle calculation records 3700 ATK |
| 008B | Wait for Dekoichi to flip, then activate Castle Walls before calculation | Battle calculation records Dekoichi's DEF as 1500 |
| 008C | Attack with Injection Fairy Lily (`79575620`) and activate its explicit calculation effect | Pay 2000 LP before confirmation; calculate with 3400 ATK; return to 400 ATK afterward |

008A/B include an unused second copy of the activated modifier so later
absence cannot be explained by consuming the only copy. 008C requires a P0
response prompt while the core is in `PHASE_DAMAGE_CAL`, with all three
ordinary modifiers excluded. The phase query is read-only and used at response
prompts to distinguish calculation from the earlier Damage Step windows;
the driver never changes phase or injects effects.

All cases require a later P0 response window with the modifiers still excluded,
normal Dekoichi Flip resolution, one Blue-Eyes draw, battle destruction, correct
LP and final zones, and return to the battle prompt. Each has a Rule 5 control
with the same expectations.

### Test 009 — Damage Step negation exceptions

P1 attacks face-down Dekoichi with Watapon. Its 200 ATK loses to 1000 DEF:
P1 takes 800 damage and Dekoichi survives damage calculation, making subsequent
effect destruction unambiguous. Dekoichi then activates its Flip effect as CL1.

| Case | P1 response after calculation | Required result |
| --- | --- | --- |
| 009A | Divine Wrath (`49010598`) as CL2 | Discard Blue-Eyes as cost, negate CL1's activation, destroy Dekoichi by effect, no draw |
| 009B | Herald of Orange Light (`17266660`) as CL2 | Send Herald and a separate Watapon from hand as cost, negate CL1's activation, destroy Dekoichi by effect, no draw |
| 009C | Decline the offered Divine Wrath | No cost or negation; Dekoichi survives and draws; Wrath remains Set |

All three have Rule 5 controls with the same outcomes. The negative modifier
controls from Test 008 remain present: Forbidden Chalice's effect negation
does not grant it an activation-negation exception after calculation. The
driver verifies it is available earlier, but never offered in the later window.

The tests distinguish `REASON_COST` from `REASON_EFFECT`, and Wrath's discard
from Herald's send-to-Graveyard cost. Both costs must finish before CL2 is
confirmed. They require `MSG_CHAIN_NEGATED` for CL1, reject effect-only
`MSG_CHAIN_DISABLED`, verify CL2 resolves before CL1 is skipped, and check
effect-destruction reasons, no draw, exact cost/negator identities, and final LP
and zones. In the pinned protocol an activation-negated link emits
`MSG_CHAIN_SOLVING` but no `MSG_CHAIN_SOLVED`; the fixture explicitly checks
this skip instead of counting it as a resolved effect.

The [Edison Damage Step exception rules](https://edisonformat.net/rules/compendium/Damage-Step)
and [substep timing](https://edisonformat.net/rules/compendium/Damage-Substeps)
supply the historical window expectations. The [Edison rule differences](https://www.edisonformat.com/edison-rule-differences.html)
explicitly exclude ordinary ATK/DEF-modifying Spells/Traps such as Chalice from
calculation and later substeps. [Konami's Damage Step guide](https://www.yugioh-card.com/eu/play/damage-step-rules/)
places ordinary modifiers before calculation and explicitly timed effects
during calculation. The pinned scripts provide the corresponding properties:
ordinary modifiers use `EFFECT_FLAG_DAMAGE_STEP` and `aux.dscon`; Lily uses
`EVENT_PRE_DAMAGE_CALCULATE`; Herald has both Damage Step/calculation flags,
and Divine Wrath is a Counter Trap. Expectations are applications of these
rules to the fixtures, not published rulings for each exact card combination.

Six additional scripts are hash-pinned from the existing checkout.
`damage_exception_tests.h` is included in the evidence source hashes. No
scripts, core patch, or production behavior were changed.

### Test 010 — activation negation during damage calculation

P1 attacks P0's face-up Attack Position Blue-Eyes with Injection Fairy Lily.
The driver activates Lily only when the core's read-only phase query reports
`PHASE_DAMAGE_CAL`. Lily pays 2000 LP before CL1 is confirmed. P0 must then
be offered the configured negator in that same calculation window, before
any battle-result message.

| Case | P0 response to Lily CL1 | Required outcome |
| --- | --- | --- |
| 010A | Accept Divine Wrath as CL2 | Discard Blue-Eyes from hand; negate Lily, destroy it by effect, no battle result or battle damage |
| 010B | Decline Divine Wrath | No negation cost; Lily reaches 3400 ATK, destroys the field Blue-Eyes by battle and deals 400 damage |
| 010C | Accept Herald of Orange Light as CL2 | Send Herald and Watapon from hand as cost; negate Lily, destroy it by effect, no battle result or battle damage |
| 010D | Decline Herald | Both Fairies remain in hand; Lily reaches 3400 ATK, destroys Blue-Eyes by battle and deals 400 damage |

Each case has a Rule 5 control with the same expected result. P1 finishes at
6000 LP in **every** case: negation must not refund Lily's activation cost.
P0 finishes at 8000 LP when accepting and 7600 LP when declining. Unnegated
Lily must return to 400 ATK after calculation.

P0 also has Set Rush Recklessly, Castle Walls, and Forbidden Chalice. They
must be offered earlier in the Damage Step but excluded during calculation,
including the response to Lily. Their valid face-up targets and unused Set
cards remain present, preventing the absence of a usable card from masquerading
as correct timing behavior.

The fixture verifies explicit acceptance/decline, cost identities and reasons,
cost payment before confirmation, CL2 resolution and activation-negated CL1's
protocol skip, effect versus battle destruction, final hand/field/Graveyard
identities, LP, and return to the battle prompt. Repeated attacks, attack replay
prompts, unexpected draws, additional chains, and any battle-result/damage
message after successful negation fail. All state setup precedes duel start;
the test reaches calculation through real turn, battle, and attack commands.

Historical expectations apply the
[Edison Damage Step exception rule](https://edisonformat.net/rules/compendium/Damage-Step):
Counter Traps and qualifying monster negation effects may respond during
calculation. The [Edison battle timing guide](https://www.edisonformat.com/battle-phase.html)
also lists Divine Wrath and Herald in the permitted classes. The existing
pinned scripts implement Lily's explicit calculation event and the respective
negation effects. The Rule 5 comparison certifies these shared exceptions,
not a historical difference or every possible calculation response.

`tests/edison/calculation_negation_tests.h` is included in the report's source
hashes. No additional cards/scripts, core changes, or production changes were
needed. Test 011 extends coverage to continuous negation; the historical
Honest/Kalut windows remain follow-up work.

### Test 011 — continuous negation

Each case starts with Skill Drain (`82732705`) Set in P0's S/T zone. When
used, the driver activates it through P0's Main Phase action prompt, pays
1000 LP, and verifies it resolves before ending the turn. P1 then conducts a
real battle on turn 2. Each of the five cases has a Rule 5 control with the
same expected behavior.

| Case | Setup and action | Required result |
| --- | --- | --- |
| 011A | Active Skill Drain; Lily attacks Blue-Eyes | Lily can activate and pay 2000 LP, but its effect resolves disabled; 400 ATK, 2600 battle damage to P1, Lily destroyed by battle |
| 011B | Leave Skill Drain Set; same attack | Skill Drain is offered at attack declaration but excluded in the Damage Step; Lily reaches 3400 ATK and destroys Blue-Eyes, dealing 400 damage |
| 011C | Activate Skill Drain, then resolve Mystical Space Typhoon (`5318639`) targeting it before battle | Drain is destroyed by effect; Lily's 3000 ATK gain resolves normally and deals 400 damage |
| 011D | Active Skill Drain; Blue-Eyes attacks face-down Dekoichi | Dekoichi flips and activates after calculation; its effect resolves disabled without drawing, then it is destroyed by battle |
| 011E | Active Skill Drain; Blue-Eyes attacks Mystic Tomato | Tomato takes its normal battle destruction route to the Graveyard, then activates there and successfully recruits Sangan in Attack Position |

The disabled effects must emit `MSG_CHAIN_DISABLED`, complete normal chain
resolution, and never emit `MSG_CHAIN_NEGATED`. Costs are paid before chain
confirmation and retained. The final LP pairs (P0/P1) are 7000/3400, 7600/6000,
6600/6000, 7000/8000, and 5400/8000 respectively. These distinguish negation
from an activation prohibition, a cost refund, or a canceled battle.

Assertions also check actual ATK at battle calculation, battle versus effect
destruction reasons, target selection for MST, exact final Graveyard identities,
hand/Deck/field counts, Drain's remaining face-up/Set state, and the return to
the battle prompt. The unused Deck filler in 011D must remain undrawn. The
recruited Sangan's own search effect is never used, avoiding its historical
errata issue.

[Konami's Skill Drain text and print history](https://www.db.yugioh-card.com/yugiohdb/card_search.action?cid=5740&ope=2&request_locale=en)
establish the activation cost, face-up-on-field scope, and permission to
activate effects under negation. The pinned script applies a continuous
`EFFECT_DISABLE` to monsters in the Monster Zones. The historical
[Damage Step exceptions](https://edisonformat.net/rules/compendium/Damage-Step)
do not permit activating Skill Drain itself in that window. These rules,
together with the pinned monster scripts, determine the fixture expectations.

Two new scripts are hash-pinned; `continuous_negation_tests.h` is included in
the evidence source hashes. No card scripts, core patch, or production behavior
were changed. Test 011 tests removal **before battle**. Test 012 extends
coverage to changes within the monster effect's chain, including the monster
leaving the field or becoming face-down before resolution. Interactions
between multiple negation sources and other continuous negators remain gaps.

### Test 012 — continuous negation at resolution

Dekoichi begins Set and is Flip Summoned through the Main Phase idle prompt.
Its mandatory Flip effect becomes CL1. Opening hands and automatic draws are
zero; the sole Deck card is Blue-Eyes, so any draw must come from that effect.
Each case has an identical Rule 5 comparison.

| Case | Response to Dekoichi CL1 | Expected resolution |
| --- | --- | --- |
| 012A | Decline the offered Book of Moon; Skill Drain already active | Dekoichi remains face-up; effect disabled, no draw |
| 012B | Book of Moon CL2 targets Dekoichi | Dekoichi becomes face-down, then draws while Skill Drain stays active |
| 012C | Compulsory Evacuation Device CL2 targets Dekoichi | Dekoichi returns to hand, then its already-activated field effect draws |
| 012D | Mystical Space Typhoon CL2 targets active Skill Drain | Skill Drain is destroyed, then face-up Dekoichi draws |
| 012E | Activate Set Skill Drain as CL2 | Pay 1000 LP, resolve Drain, then disable Dekoichi's effect without drawing |

In A–D, Skill Drain is actually activated and resolves in a separate setup
chain before the Flip Summon. In E, it remains Set until the response to CL1.
All five cases require exactly one 1000 LP payment and final LP of 7000/8000.
The response cards start Set in the fixture; no card or script is injected
after the duel begins.

Assertions require the real Flip Summon announcement/completion, exact
activation/confirmation order, CL2 before CL1 resolution, and the intermediate
position or location change before CL1 starts resolving. A successful draw
must occur during CL1 and identify the Deck filler. Negated cases require
`MSG_CHAIN_DISABLED` followed by normal `MSG_CHAIN_SOLVED`, never activation
negation. The final idle checkpoint verifies exact hands, Deck contents,
Monster Zones and positions, Spell/Trap Zones, Graveyards, and no banishment.
The declined Book of Moon in A must remain Set.

The expectations follow Skill Drain's face-up-on-field restriction in
[Konami's card text](https://www.db.yugioh-card.com/yugiohdb/card_search.action?cid=5740&ope=2&request_locale=en),
together with the pinned card scripts. Applying that restriction at resolution
predicts that B and C escape the ongoing negation; this is also described in the
[historical Skill Drain ruling collection](https://edisonrulings.org/cards/skill-drain).
D and E test that removing or establishing the continuous effect before CL1
changes the result. These are Main Phase cases, not additional permissions to
activate these response cards in the Damage Step.

Book of Moon (`14087893`) and Compulsory Evacuation Device (`94192409`) are
added to the database allowlist and script hash lock. The harness decodes
`MSG_FLIPSUMMONING` and `MSG_FLIPSUMMONED` using the pinned core's payload
layout. `resolution_negation_tests.h` is included in evidence source hashes.
No core patch, production code, or card script changes were needed.

This closes the within-chain removal and monster-position/location gaps
identified in Test 011. Test 013 adds departure by tribute as an activation
cost. Tests 014–016 extend the comparisons to persistent negation, face-down
resets, and self-tribute followed by revival before resolution. Multiple
continuous negators and other departure/return routes remain gaps.

### Test 013 — leaving the field as an activation cost

Three Main Phase ignition scenarios each run with Skill Drain active and left
Set, and each has an identical Rule 5 control: twelve cases total.

| Case | Skill Drain | Activation cost and expected outcome |
| --- | --- | --- |
| 013A | Active | Lonefire tributes itself, then recruits Jerry Beans Man |
| 013B | Active | Lonefire tributes the other Plant and stays face-up; effect disabled, no recruit |
| 013C | Active | Exiled Force tributes itself, then destroys the opposing Blue-Eyes |
| 013D | Set | Lonefire tributes itself and recruits normally |
| 013E | Set | Lonefire tributes the other Plant and recruits normally |
| 013F | Set | Exiled Force tributes itself and destroys Blue-Eyes normally |

Lonefire starts face-up alongside Jerry Beans Man, with one further Jerry
Beans Man in the Deck. Both field Plants must be offered as tribute choices.
Using a Normal Monster avoids any extra trigger from paying the cost.
Exiled Force starts opposite a face-up Blue-Eyes. All cards are placed before
duel start; opening hands and automatic draws are zero.

In A–C, Skill Drain is activated through the idle prompt, pays 1000 LP, and
fully resolves before the monster's ignition is selected. In D–F, its response
offer is explicitly declined and it remains Set. Final LP is 7000/8000 in the
active cases and 8000/8000 in the Set controls.

The fixture requires the monster effect's activation to be announced from
the Monster Zone. Its tribute must move to the Graveyard with
`REASON_COST | REASON_RELEASE`, without effect, battle, or destruction reasons,
before `MSG_CHAINED` and before any response to the confirmed link. Exiled's
target prompt must follow its tribute; target destruction must instead occur
during resolution with effect/destruction reasons. These are effects activated
on the field, not new Graveyard triggers.

For Lonefire, the retained monster, paid tribute, and Drain state are checked
at confirmation and again before resolution. Successful recruitment requires
the actual Deck selection, zone and position prompts, Special Summon movement,
and summon completion. The negated case requires `MSG_CHAIN_DISABLED` and
normal link completion, no recruitment, the Deck card unchanged, and the
other Plant still in the Graveyard. No case permits activation negation.
The final idle checkpoint verifies exact field positions, Deck and Graveyard
identities, empty hands/banished/Extra Deck zones, and retained tribute costs.

[Konami's Lonefire text](https://www.db.yugioh-card.com/yugiohdb/card_search.action?cid=7568&ope=2&request_locale=en)
and [Exiled Force text](https://www.db.yugioh-card.com/yugiohdb/card_search.action?cid=5323&ope=2&request_locale=en),
together with their pinned scripts, establish the tribute costs.
[Historical Lonefire rulings](https://edisonrulings.org/cards/lonefire-blossom)
explicitly describe its self-tribute interaction with Skill Drain. The
contrasting outcomes test Skill Drain's restriction to monsters remaining
face-up on the field when their effects resolve.

Lonefire Blossom (`48686504`), Exiled Force (`74131780`), and Jerry Beans Man
(`23635815`) are added to the database allowlist. The two effect scripts are
hash-pinned; Jerry Beans Man is verified as a Normal Plant and intentionally
has no script. `cost_departure_tests.h` is included in evidence source hashes.
No core, production, or card-script changes were needed.

This covers self-tribute departure and a matched other-tribute comparison.
Test 014 adds already-applied negation after self-tribute, and Test 016 adds
revival before the original effect resolves. Returning to hand/Deck or
banishing as costs, and other departure/return routes, remain gaps.

### Test 014 — applied negation after self-tribute

These cases contrast Forbidden Chalice's already-applied negation with
Skill Drain using the same two ignition effects as Test 013. Each case has an
identical Rule 5 control.

| Case | Setup | Expected result after self-tribute |
| --- | --- | --- |
| 014A | Resolve Chalice on Lonefire Blossom | Field effect resolves disabled; Plant stays in Deck |
| 014B | Resolve Chalice on Exiled Force | Field effect resolves disabled; opposing Blue-Eyes survives |
| 014C | Resolve Skill Drain, then activate Lonefire | Recruit Jerry Beans Man normally |
| 014D | Resolve Skill Drain, then activate Exiled Force | Destroy opposing Blue-Eyes normally |
| 014E | Leave Chalice Set, then activate Lonefire | Recruit normally |
| 014F | Leave Chalice Set, then activate Exiled Force | Destroy Blue-Eyes normally |

The applied negator resolves in a separate setup chain before the ignition
action is chosen. Chalice must target the face-up actor, finish resolving, and
go to the Graveyard. Before ignition activation, the actor must have the actual
400 ATK increase: Lonefire 900 or Exiled Force 1400. The other controls require
their unchanged ATK. Skill Drain's setup must pay 1000 LP; Chalice has no LP
cost. The unused-Chalice controls require that its activation was available
and that it remains Set.

The actor's ignition is announced from the Monster Zone. Self-tribute must
move it to the Graveyard as a release cost before link confirmation and before
responses. The empty Monster Zone and paid cost are checked again before
resolution. Applied Chalice must then produce effect negation and normal link
completion, with no recruitment or target destruction. Activation negation is
rejected. Successful controls require the actual Special Summon or destruction
events. Final assertions verify the exact field, Deck, Graveyard, LP, and
retained cost outcomes.

This tests negation of an effect **activated on the field**, even though its
monster has left. It does not mean that Chalice negates a new effect activated
in the Graveyard. The historical
[Forbidden Chalice rulings](https://www.edisonformat.com/rulings/individual-rulings-f-h)
state this distinction and explicitly cover Lonefire's self-tribute. The
[Raging Battle ruling document](https://ms.yugipedia.com/a/af/Card_Rulings_-_Raging_Battle_v1.2.pdf)
also describes the previously affected Exiled Force case. Applying Chalice in
advance is essential: these fixtures do not attempt to target a monster after
it has already left as cost.

All cards and scripts were already pinned; no asset, core, production, or
card-script changes were needed. The new fixture is
`persistent_negation_tests.h`, included in evidence source hashes.

Only Test 014 and its Rule 5 controls were run for this change:
**12 PASS, 0 FAIL**, twice with identical traces. The new `--test 014` runner
option selects these cases and records a selected-test scope in a separate
evidence directory. Invalid or unmatched selectors fail instead of reporting
an empty success. No full-suite or full forced-Rule-5 rerun was performed.

Test 015 adds Chalice face-down resets and Skill Drain reapplication.
Test 016 adds self-tribute followed by revival before the original effect
resolves. Remaining gaps include other persistent negators, departure by return
or banishment, new Graveyard effects after Chalice, and other return routes.

### Test 015 — face-down resets of negation

Five Dekoichi scenarios each have a Rule 5 control. Opening hands and automatic
draws are zero; one Blue-Eyes in the Deck makes the Flip effect's draw observable.
Chalice/Skill Drain and Book of Moon start Set and activate through normal
prompts. No state or script is injected after duel start.

| Case | Activation sequence | Expected result |
| --- | --- | --- |
| 015A | Dekoichi CL1, Book CL2, Chalice CL3 | Chalice resolves first, then Book resets it; face-down Dekoichi draws at CL1 |
| 015B | Dekoichi CL1, Chalice CL2; decline Book | Dekoichi stays face-up at 1800 ATK; effect disabled, no draw |
| 015C | Dekoichi CL1, Chalice CL2, Book CL3 | Book resolves first; Chalice cannot apply to the face-down target; Dekoichi draws |
| 015D | Resolve Chalice, resolve Book in a separate chain, then Flip Summon | Chalice stays cleared after returning face-up; 1400 ATK and a successful draw |
| 015E | Resolve Skill Drain, resolve Book, then Flip Summon | Skill Drain remains active and negates the face-up Flip effect; no draw |

A–C begin with Dekoichi Set and actually Flip Summon it before building the
response chain. A requires Chalice to finish resolving before Book turns the
monster face-down; C requires the opposite order. B retains the unused Set
Book as the negative comparison. The driver checks every activation,
confirmation, target, and reversed resolution order, then verifies the draw
or effect-disabled event and normal link completion. Activation negation is
rejected.

D–E begin with Dekoichi face-up. After the first setup chain, the idle
checkpoint requires 1800 ATK under Chalice or 1400 under Skill Drain. Book
then actually Sets Dekoichi, after which the engine must offer a legal Flip
Summon. All actions finish during the same first Main Phase: the driver
rejects progression to the End Phase, so expiration cannot explain Chalice's
reset. D's final 1400 ATK and draw distinguish a cleared effect from a merely
temporary suspension while face-down. E requires Skill Drain to remain face-up
and its 1000 LP cost to remain paid.

Final checks include exact monster position, hand/Deck identities, resolved
Spell cards in the Graveyard, remaining Spell/Trap states, empty opposing
zones, and LP. No new assets, scripts, core changes, or protocol changes were
needed; `face_down_reset_tests.h` is included in evidence source hashes.

The historical [Book of Moon rulings](https://www.edisonformat.com/rulings/individual-rulings-a-c)
describe resetting previously applied lingering effects. The pinned Chalice
script explicitly resets its ATK modification and negation on turning
face-down, including negation of the related pending chain.
[Konami's later FAQ 12314](https://www.db.yugioh-card.com/yugiohdb/faq_search.action?fid=12314&keyword=&ope=5&request_locale=ja&tag=-1)
confirms the Chalice-then-Book resolution distinction using Judgment Dragon;
it is supporting modern evidence, not a contemporaneous 2010 ruling.
The [historical Chalice rulings](https://www.edisonformat.com/rulings/individual-rulings-f-h)
also require the target to remain face-up when Chalice resolves, supporting C.

Only Test 015 and its controls were executed for this change:
**10 PASS, 0 FAIL**, twice with identical traces. Evidence is saved under
`build/edison-certification/test-015/`. Run with
`python tests/edison/run.py --test 015`.

This covers Chalice's reset and Skill Drain's reapplication. Test 016 adds
self-tribute and revival within a pending chain. Other negators, multiple
negation sources, and return routes outside that Graveyard revival remain gaps.

### Test 016 — leaving and returning before the original effect resolves

Lonefire Blossom and Exiled Force each activate as CL1 and Tribute themselves
as cost. Call of the Haunted is then activated as CL2, targeting that exact
monster in the Graveyard. CL2 must finish reviving it into its original Monster
Zone before the original CL1 begins resolving. Each case has a Rule 5 control.

| Case | Actor | Negation setup | Original CL1 after revival |
| --- | --- | --- | --- |
| 016A | Lonefire | Chalice already resolved on it | Disabled; Jerry Beans Man remains in Deck |
| 016B | Exiled Force | Chalice already resolved on it | Disabled; opposing Blue-Eyes survives |
| 016C | Lonefire | Skill Drain active | Resolves; recruits Jerry Beans Man |
| 016D | Exiled Force | Skill Drain active | Resolves; destroys opposing Blue-Eyes |
| 016E | Lonefire | Chalice offered but left Set | Resolves; recruits normally |
| 016F | Exiled Force | Chalice offered but left Set | Resolves; destroys normally |

The applied negator resolves before the ignition action is selected. Chalice's
400 ATK increase must be present at that point; Skill Drain must have paid
1000 LP. Only one copy of the actor exists in the fixture, so the Graveyard
target and returned monster cannot be a replacement copy. Call must not be
available before the actor leaves. All setup uses pre-start placement followed
by real activation, target, cost, and summon responses.

The departure must carry release/cost reasons and occur before CL1 confirmation.
The actor must be absent from the Monster Zone and present in the Graveyard
when Call is selected and confirmed. During CL2, the driver requires the actual
Graveyard-to-field Special Summon movement, placement in original sequence 0,
summon announcement/completion, and Call's ongoing card-target link. CL1 cannot
start before these events and CL2 completion. The returned monster must be
face-up at its base ATK, including in the Chalice cases.

This distinguishes the returned monster from its original pending effect.
Reviving the monster does not remove Chalice's negation of the already-activated
CL1. Skill Drain affects the returned monster, but that does not retroactively
negate the original field effect whose actor left. The test checks the original
effect only; it does not activate another effect of the revived monster.

[Call of the Haunted's official text](https://www.db.yugioh-card.com/yugiohdb/card_search.action?cid=4989&ope=2&request_locale=en)
and its pinned script establish the Graveyard target and Attack Position
revival. The negation expectations extend the self-tribute distinctions in
Tests 013–014 to revival during the same chain. As an implementation
cross-check, the pinned core's `processor.cpp` records chain-specific
`EFFECT_DISABLE_EFFECT` negation before paying costs; its resolution check
separately requires a surviving relation to the original chain when applying
the handler's current disabled status. `field::is_chain_disabled` checks the
stored chain-specific negation. These checks explain why the two negators
produce different results after the physical card returns.

Negated cases require `MSG_CHAIN_DISABLED` and normal solved-link completion,
never activation negation. Successful cases require the actual recruit or
destruction. Final checks verify the revived actor still on the field, Call
still face-up, base ATK, exact Graveyard and Deck contents, other field cards,
and LP. The Tribute was paid, then the card was revived by a separate effect;
this is not a refund of the cost.

Call of the Haunted (`97077563`) is added to the database allowlist and script
hash lock. The harness decodes `MSG_CARD_TARGET` as the pinned core's two
four-byte locations, and the fixture verifies the link from Call to the revived
actor. `leave_return_tests.h` is included in evidence source hashes. No
production, core, or card-script changes were needed.

Only Test 016 and its controls were executed:
**12 PASS, 0 FAIL**, twice with identical traces, via
`python tests/edison/run.py --test 016`. Evidence is in
`build/edison-certification/test-016/`. Earlier groups and the full suite were
not rerun.

Coverage is specifically self-tribute to the Graveyard followed by revival
before resolution. Departure by a resolving effect, return from banishment or
hand/Deck, target identity after a target leaves and returns, and subsequent
activations of the revived monster remain separate gaps.

### Test 017 — private-hand Early Trigger ordering (corrected)

This extends Tests 005–006 to an unrevealed Watapon by removing Ceremonial Bell.
Dealings draws Watapon before discarding Broww. The across-link variant draws
Watapon with Jar of Greed at CL2 before Dealings resolves at CL1. Each timing
variant accepts and declines Watapon and checks full resolution, identities,
zones, and LP. Expectations follow the established
[Early Trigger interpretation](https://edisonformat.net/rules/compendium/SEGOC-And-Early-Trigger).

The original run produced **6 PASS, 1 FAIL, 3 NOT RUN**: Edison announced
Broww CL1 before offering private Watapon. That evidence is preserved in
build/edison-certification/test-017-before-private-hand-fix/.

The correction extends the pinned-core patch in field::check_nonpublic_trigger.
Eligible Rule 1 hand triggers retain CHAIN_HAND_TRIGGER but stay in the
chronological optional-trigger queue instead of also entering new_ochain_h.
Existing trigger validity and hand self-summon checks still apply. Declining
removes the trigger without a second offer from the quick-effect queue.
Deck triggers, out-of-range handling, and Rule 5 keep the prior path.

After reinstalling the explicitly configured patched package and rebuilding
the certification executable and production ygopro_ygoenv module, the focused
run produced **10 PASS, 0 FAIL**, with two identical event/response traces:

| Cases | Scope | Result |
| --- | --- | --- |
| CONTROL 017A–D | Rule 5 private hand; within/across links; accept/decline | PASS |
| CONTROL 017E–F | Edison public hand; accept/decline | PASS |
| EDISON 017A–D | Edison private hand; within/across links; accept/decline | PASS |

Command: python tests/edison/run.py --test 017 --fail-fast.
Evidence: build/edison-certification/test-017/report.json.

An initial rebuild accidentally selected the unpatched XMake variant and
failed CONTROL 017E (4 PASS, 1 FAIL). Its trace is retained separately in
build/edison-certification/test-017-wrong-package-variant/. Explicit package
configuration and a clean XMake reconfiguration corrected the linkage before
the successful run. No full suite ran.

### Test 018 — cross-player Early Trigger ordering

cross_player_trigger_tests.h moves Watapon and Broww to opposing players.
P0 activates Dealings; both assignments of optional-trigger ownership are
tested, each with accept/decline and Rule 5 comparisons. Edison offers the
earlier optional decision before the opponent's later mandatory trigger.
Rule 5 places Broww first. Watapon stays private until activation.

The fixture checks draws before discards, effect/discard reasons, trigger
ownership, activation and reverse resolution order, no repeated declined
offer, final identities/zones, and unchanged LP. Broww draws one card for
P0's own Dealings, or two when P1 discards it due to P0's effect.

Command: python tests/edison/run.py --test 018 --fail-fast.
Result: **8 PASS, 0 FAIL**, two identical event/response traces.
Evidence: build/edison-certification/test-018/report.json.

### Test 019 — historical Sangan search restrictions (corrected)

The initial empty-buffer failure was corrected by one-shot confirmation-wait
handling. The subsequent run reached the historical-text assertion and found
that the modern Sangan script prevented the searched Lonefire from activating.
Those failures are preserved in test-019-before-confirmation-wait-fix and
test-019-before-sangan-fix under build/edison-certification/.

The shared historical_scripts.h override provides Sangan's Edison text:
no searched-name activation restriction and no modern once-per-name limit.
It is compiled into production and certification, leaving upstream script bytes
unchanged. Rule 5 loads the original pinned script. Fixtures select their
script rule before constructing or placing cards, maintain per-rule script
caches, and reject a different rule passed to start(). Production uses the
same shared override with duel_config::rules. The runner hashes the shared
header and production loader alongside its other source evidence.

The [Edison functional errata](https://www.edisonformat.com/rulings/relevant-errata)
and [Sangan rulings](https://edisonformat.net/card?name=Sangan) support the historical
behavior; the [modern official text](https://www.db.yugioh-card.com/yugiohdb/card_search.action?cid=4054&ope=2&request_locale=en)
supports the comparison.

Test 019 now produces **4 PASS, 0 FAIL**, with identical replay traces:
Rule 5 blocks the searched Lonefire, both rules allow an already-held Lonefire
when Sangan searches Dupe Frog instead, and Edison allows the searched Lonefire.
Allowed effects actually self-tribute and recruit Jerry Beans Man. Both native
production and certification consumers were rebuilt for the shared override.

### Tests 020–035 — iterative focused certification

Each group runs through the real core protocol with Rule 5 comparisons,
strict choices, costs, chain order, resolution, and final state assertions.
Only the new or affected group is run; passing groups replay in a second
fresh process. No full-suite result is inferred.

| Test | Coverage | Focused result |
| --- | --- | --- |
| 020 | Same Sangan destroyed, searched, revived, destroyed again: Edison searches twice; Rule 5 once | 2 PASS |
| 021 | Dupe misses timing when tributed for Caius; destruction as last action offers accept/decline search | 6 PASS |
| 022 | Dekoichi draws private Watapon during its trigger resolution; Watapon waits for a new chain, accept/decline | 4 PASS |
| 023 | Equal-ATK Sangan or Tomato destroyed simultaneously; turn-player-first triggers, all optional decision combinations | 10 PASS |
| 024 | Chalice target remains, leaves, or leaves and returns via Enemy Controller cost and Call before CL1 | 6 PASS |
| 025 | Enemy Controller tributes Sangan; Crow optionally banishes it before activation; Edison still searches, Rule 5 does not | 4 PASS |
| 026 | New Sangan Graveyard effect after Chalice, Skill Drain, both, or neither; paid-cost Exiled resolves | 8 PASS |
| 027 | Swap Frog returns itself or another monster for cost under Chalice, Drain, or neither; ordinary summon already spent; additional Frog summon verified | 12 PASS |
| 028 | Cyber Valley banishes itself as attack-trigger cost under Chalice, Drain, or neither; decline control takes battle damage | 8 PASS |
| 029 | Honest/Kalut before calculation, during calculation, or declined; Edison allows the historical calculation window | 12 PASS |
| 030 | Two Honest: accept/decline second copy in the same calculation chain; no second manual chain after resolution; Rule 5 early-window controls | 5 PASS |
| 031 | Debris-revived Lonefire tributes itself/another Plant; Book resets pending negation; Call supplies an unnegated revival control | 8 PASS |
| 032 | Remove/retain Skill Drain with/without independently applied Chalice; Lonefire tributes another Plant and recruits only when no negation remains | 8 PASS |
| 033 | Jar CL2 draws private Watapon; Dealings CL1 discards opposing Broww; both player assignments and accept/decline decisions | 8 PASS |
| 034 | Brain Control takes Blue-Eyes in both rules; Goyo is an eligible target only in Edison | 4 PASS |
| 035 | Actual Goyo Synchro Summon with Level 3 Tuner and Level 3 non-Tuner; EARTH accepted in both rules, WIND only in Edison | 4 PASS |

All listed passing runs have identical event/response replay traces.
At the Test 035 checkpoint, the suite had 249 cases, including 109 new cases in 020–035.
Together with the corrected 019, this continuation has 113 passing cases in
focused runs. This is not a full-suite run on the current source. Earlier
passing groups retain evidence for their own recorded source/binary hashes.

Rules failures were fixed through the shared production/certification script
loader, scoped to Rule 1. Rule 5 continues to load the pinned original scripts:

- Test 019: historical Sangan removes the searched-name activation restriction
  and once-per-name limit; Test 020 separately verifies repeated searches.
- Test 029 initially passed all six Rule 5 controls and the early Edison Honest
  case, then failed when Honest was not offered during calculation. Historical
  Honest and Kalut now register the calculation timing/property and permit that
  phase, while still rejecting activation after damage has been calculated.
- Test 034 initially passed both Rule 5 controls and Edison Blue-Eyes, then
  failed to offer Brain Control against Goyo. The historical filter removes
  only the Normal Summon/Set eligibility restriction.
- Test 035 initially passed both Rule 5 controls and Edison EARTH materials,
  then failed to offer Goyo with WIND Gale. Its historical procedure removes
  only the EARTH attribute filter. The test actually selects materials, verifies
  their Synchro-material movement, and completes the summon from the Extra Deck.

The production module was rebuilt after each shared-script correction.
The upstream scripts checkout, dependency pins, and existing Early Trigger
core patch were not changed in this continuation. Saved pre/post-fix Rule 5
control traces compare identically: three in 019, six in 029, two in 034,
and two in 035. This comparison reads saved evidence; it does not rerun tests.

Harness/fixture failures were corrected separately:

| Group | Correction | Preserved failure directory suffix |
| --- | --- | --- |
| 021 | Decode MSG_MISSED_EFFECT; assert Dupe's exact missed-timing notification | before-missed-effect-decoder |
| 024 | Decline legal pre-Main-Phase Chalice offer | before-response-fixture-fix |
| 031 | Decline legal Edison ignition-priority offer after revival, then activate Lonefire from idle | before-priority-fixture-fix |
| 033 | Find private Watapon by identity after hand shuffle, rather than assuming hand index zero | before-hand-index-fix |
| 034 | Include the already hash-pinned Goyo script in the script allowlist | before-script-allowlist-fix |
| 035 | Decode MSG_SELECT_SUM, including mandatory material and level values; explicitly select 3 + 3 | before-sum-decoder |

Rule-failure archives use suffixes before-sangan-fix (019),
before-historical-window-fix (029), before-brain-control-fix (034), and
before-goyo-fix (035). Each directory is under build/edison-certification/
with prefix test-NNN-. Failed runs stop immediately; only that group is rerun.

Test 024 also adds strict MSG_SELECT_OPTION decoding and validates Enemy
Controller's two mode descriptions. At the Test 035 checkpoint the fixture asset set had 43 card
identities and 44 hash-pinned upstream Lua files, including three shared helper
scripts. New assets are the minimum cards used by these fixtures; no full card
pool was imported.

Sources:
[missed timing](https://edisonformat.net/rules/compendium/Missing-the-Timing),
[trigger locations](https://edisonformat.net/rules/compendium/Trigger-Locations),
[Swap Frog's return cost](https://edisonformat.net/rules/compendium/Ignition-Priority),
[Cyber Valley](https://edisonrul.ing/cards/c/cyber-valley.html),
[historical battle substeps](https://www.edisonformat.com/battle-phase.html),
[Debris Dragon](https://edisonrulings.org/cards/debris-dragon),
[Book of Moon resets](https://edisonrul.ing/cards/b/book-of-moon.html),
[Brain Control and Goyo errata](https://www.edisonformat.com/rulings/relevant-errata),
and the pinned card scripts/core.

Evidence directories are build/edison-certification/test-NNN/:
each contains report.json and run-1.txt/run-2.txt for a passing group.
Commands used for each new group, and again after any failure correction:

```bash
xmake b edison_rules
python tests/edison/run.py --test NNN --fail-fast
```

NNN was each of 019 through 035; no unfiltered runner invocation was used.
Production rebuilds used xmake b ygopro_ygoenv.

At the Test 035 checkpoint, historical text, trigger locations, deck legality,
and control resets remained in the backlog. Tests 036–076 below close those
representative coverage items. This does not establish correctness for every
possible card combination.

### Reproducible core correction

`repo/packages/y/ygopro-core/early-trigger.patch` applies to upstream commit
`f96929650ff8685b82fd48670126eae406366734`. The package recipe runs
`git apply --check` before applying it, prior to the existing header fixes.
The project explicitly enables the `edison_early_trigger=true` package
configuration. XMake installs this as a separate package variant, preserving
the existing unpatched cache. No upstream commit or dependency version changed.

Events raised between instant-event flushes receive a shared chronological
group number. Single-card and group notifications for simultaneous actions
therefore share a group. In Rule 1, since `edison_hardening = "r2"`, each SEGOC step
offers only its player's oldest pending group, so steps come first and chronology
applies inside a step. Invalid or declined effects are still handled by the existing
checks. Rule 5 uses the existing selection path.

The change adds fields to native structures, so both the environment module and
certification executable must be rebuilt against the same patched headers and
library. The runner verifies the patch SHA-256 against the assets lock and
records it alongside the source pin, recipe hash, and executable hash.
When editing the patch in future, force-reinstall the package variant before
rebuilding consumers; do not rely on an old cached native library.

Use explicit package configuration when forcing a reinstall:

```bash
xmake require -f -y --shallow --extra='{configs={edison_early_trigger=true}}' 'ygopro-core 0.0.2'
xmake f -c -y
xmake b -r edison_rules
xmake b -r ygopro_ygoenv
```

Check verbose compiler/linker paths for the configured variant
`bedb42dd1c644c7482fd8520cd0c023e`; a bare `xmake require ygopro-core`
can select the unpatched variant. The corrected patch SHA-256 is
`5a6d18d7bdac6cb305cf414a7409b374c3ea7519c18a4cc41884686f05fede61`.

## Versions and limits

| Component | Source revision |
| --- | --- |
| YGOPro core 0.0.2 | `f96929650ff8685b82fd48670126eae406366734` |
| Existing mycard scripts | `8e7fde9bf1b5a3934cfb8c91f23fadb322106d3e` |
| Existing English database source in Makefile | `7b1874301fc1aa52bd60585589f771e372ff52cc` |

Core packaging retains the existing header compatibility patches. The assets
lock records the actual database and script bytes; it does not make the entire
modern database or script set historically correct. Review changes before
updating the lock. A hash mismatch was verified to fail before executing fixtures.

The last full-suite run, through Test 013, produced **84 PASS, 0 FAIL**,
exit 0, in two fresh processes with identical traces. Prior focused runs of
Tests 014, 015, and 016 produced 12, 10, and 12 passing cases respectively,
with no failures and identical replay traces. These remain earlier results.

Tests 017 and 018 retain prior focused passing evidence (10 and 8 cases).
The current continuation corrected Sangan and passed Test 019 plus Tests
020–035: 113 passing cases across focused runs, each replaying identically.
No full suite or earlier group was rerun. The suite had 249 cases at this checkpoint.
Earlier reports remain evidence for their recorded source and binary hashes.
The production module and certification target were rebuilt for the shared
historical script override; subsequent fixture changes rebuilt certification.

To verify regression detection:

```bash
build/linux/x86_64/release/edison_rules \
  assets/locale/en/cards.cdb scripts/script --rule=5
```

The last full probe, through Test 013, returned exit 1, 73 PASS, 11 FAIL.
This probe has not been rerun since; no current full-probe count is claimed.
The eleven established Edison-specific failures were 001, 002, 003A, 003C,
003D, 004B, 004C, 005A, 005B, 006A, and 006B. Direct native runs are debugging
tools; use the Python runner for asset checks and saved evidence.

Tests 001–016 have prior passing evidence. Tests 017–018 now cover private-hand
Early Trigger decisions, across-link events for one player, and opposing-player
within-link chronology. Test 019 now passes with the shared historical Sangan override; Test 020
checks its repeatability after revival.
This is not exhaustive Edison certification. Test 033 adds
cross-player across-link private-trigger chronology with both player assignments. Test 007 covers
battle-delayed Flip effects, battle-destruction recruiters, and exclusion of a
generic draw trap. Tests 021–023 now cover selected missed-timing, nested-trigger, and
simultaneous battle-trigger cases; unenumerated combinations remain outside this finite backlog.
Tests 008–009 now cover ordinary ATK/DEF modifiers before calculation, Lily's
explicit calculation exception, and Counter Trap/monster activation negation
after calculation. Test 010 covers accepting and declining those two negators
during calculation itself, including cancellation of the battle when Lily is
destroyed and retention of its paid LP cost. Test 011 covers Skill Drain's
activation permission, effect negation, pre-battle removal, and Graveyard
exclusion. Test 012 covers Skill Drain being activated or destroyed within
the monster effect's chain, plus the monster returning to hand or becoming
face-down before resolution. Test 013 covers self-tribute activation costs
and the contrasting case of tributing another monster while remaining face-up.
Test 014 covers already-applied Chalice continuing to negate those field
effects after self-tribute, with Skill Drain and unused-Chalice comparisons.
Test 015 covers Chalice resetting face-down, including a pending chain and
a same-turn Flip Summon, plus Skill Drain reapplication on returning face-up.
Test 016 covers self-tribute followed by revival before the original field
effect resolves, under Chalice, Skill Drain, and an unnegated comparison.
Tests 024–027 add target identity after returning, pre-activation trigger
location changes, Graveyard effects after combined field negation, and return
to hand as a cost. Tests 028–035 add banishment costs, Honest/Kalut calculation
timing, the single calculation-chain limit, Debris negation, overlapping
negation removal, opposing-player across-link triggers, and historical Brain
Control/Goyo behavior. The continuation below adds the remaining documented
historical-card audit, trigger-location example, and deck/card-pool/Extra Deck validation.

## Backlog continuation: focused Tests 036–052

This section records the first part of the backlog continuation. The completed
coverage matrix and final focused totals follow below. No full suite or earlier
passing group was rerun.

| Group | Cases passed | Coverage |
| --- | ---: | --- |
| 036 | 10 | Main 40–60, Extra/Side 0–15 size boundaries |
| 037 | 24 | March 2010 copy limits across all zones and permanent-name aliases |
| 038 | 36 | April card-pool membership, alternate art, tokens, unknown IDs, forbidden cards |
| 039 | 32 | Fusion/Synchro Extra Deck legality; Rule 5 Xyz/Link comparisons |
| 040 | 12 | YDK parsing: LF/CRLF/BOM, comments, malformed IDs and sections |
| 041 | 6 | Brain Control target set before/after resolution and End Phase return |
| 042 | 10 | Actual production module deck initialization, isolated processes |
| 043 | 4 | Sacred Crane leaves before its summon trigger's activation window |
| 044 | 4 | D.D. Survivor delayed End Phase return; face-down exclusion |
| 045 | 2 | Historical Catapult Turtle repeatability |
| 046 | 4 | Historical Brionac own-field targets and repeatability |
| 047 | 6 | Necrovalley targeted/non-targeted revival and Graveyard banish costs |
| 048 | 3 | Treeborn retries after activation negation, accepted/declined |
| 049 | 4 | Darkness Approaches face-down Attack Position, attack prohibition, Flip Summon |
| 050 | 4 | Ryko optional target at activation, destruction before milling |
| 051 | 4 | Quickdraw hand ignition, send at resolution, activation-negation control |
| 052 | 8 | Mandatory Lightsworn/Spirit phase retries; effect-negation exclusion |

These groups total **173 passing cases across focused runs**, each with two
identical event/response traces. Together with earlier registrations the suite
contained **422 cases** at the Test 052 checkpoint. Earlier passing reports describe their own recorded
source/binary hashes, not a regression run against today's build.

Production deck validation now rejects illegal Edison decks before registration.
The existing modern example decks are not legal Edison input. Names beginning
with an underscore are preload resources, not selectable decks; _tokens.ydk
remains usable as a resource. Test 042 imports the actual native module in a
fresh process per case, without loading training infrastructure.

The reviewed pool manifest is assets/edison/legality.json: 3,659 distinct card
identities, 3,759 explicit accepted passcodes, and 135 restrictions (45 forbidden,
70 limited, 20 semi-limited). The generated header is checked against both the
manifest and lock before every certification invocation. Permanent-name aliases
count together for copy limits but cannot grant pool eligibility to a later card.
Rule 5 comparison validation permits modern Extra Deck types and uses the
default three-copy ceiling; it does not implement a current tournament banlist.

Pool/list sources and review adjustments are recorded in the manifest:
[March 2010 official list](https://img.yugioh-card.com/en/downloads/alt_format/2010-03-01.pdf),
[Edison card pool](https://www.edisonformat.com/card-pool.html),
and its linked YGOPRODeck format data. Token/non-duel promo exclusions and the
Barrel Dragon database passcode mapping are explicit. The generator's --check
option verifies the generated artifact offline.

Historical corrections added here are Rule-1-only overrides for Catapult
Turtle, Brionac, Necrovalley, Treeborn, Darkness Approaches, Ryko, and Quickdraw.
Rule 5 uses the unchanged pinned upstream files. They are shared by certification
and production. Their basis is the
[functional-errata reference](https://www.edisonformat.com/rulings/relevant-errata).

The core patch additionally exposes Effect.RemoveCountLimit() so a historical
script can remove a phase limit after registration installs its implicit
default. The historical Treeborn and Destiny End Dragoon overrides call it. A Rule-1-only
activation-negation path restores a mandatory phase trigger's single-use count;
effect negation does not take that path. Test 052 validates both distinctions
with Lumina and Susa Soldier against Light and Darkness Dragon/Skill Drain.
The upstream core commit remains pinned.

Failed attempts are preserved in test-NNN-before-... evidence directories.
This includes rule failures and fixture/protocol corrections; they are not
reported as passing runs. Passing evidence remains under test-NNN/.
Commands are still xmake b edison_rules followed by
python tests/edison/run.py --test NNN --fail-fast. Core patch changes use the
explicit configured reinstall command above and rebuild both native consumers.

The historical-card audit listed at this checkpoint is completed by the groups
below. Some pinned scripts already matched the historical behavior and needed
only runtime coverage.

## Completed backlog: Tests 053–076

| Group | Cases passed | Coverage |
| --- | ---: | --- |
| 053 | 4 | Prisma reveals/sends material at resolution; negation and departure |
| 054 | 2 | Rescue Cat repeatability and recruited effects |
| 055 | 2 | Strike Ninja per-copy use |
| 056 | 4 | REDMD repeated procedures and non-targeting revival per copy |
| 057 | 4 | Fortune Lady Light face-up/face-down departure |
| 058 | 4 | Future Fusion initial materials, second Standby, linked destruction, summon lock |
| 059 | 8 | Dark/Light End ATK/DEF loss when opposing target leaves |
| 060 | 8 | Cyber Phoenix/Jade Knight face-up before attack selection |
| 061 | 4 | Armory damage after an actually Synchro Summoned Colossal revives |
| 062 | 2 | One Union per monster, same-turn limit, next-turn unequip |
| 063 | 4 | Black Garden face-down summons, face-up ATK/token controls |
| 064 | 4 | Mark of the Rose End/Standby trigger chains and Cold Wave |
| 065 | 2 | Properly Fusion Summoned Dragoon retries Standby revival after negation |
| 066 | 6 | Soul Exchange optional tribute choice for summon and activation cost |
| 067 | 2 | My Body versus face-up Oppression; pending inherent-summon exclusion |
| 068 | 4 | My Body Main Phase FLIP positive control and Damage Step exclusion |
| 069 | 8 | Judgment/Mausoleum/Offering, resolution summons, CL2 Torrential missed timing |
| 070 | 4 | Urgent Tuning activation negation versus Synchro summon-negation windows |
| 071 | 4 | Mausoleum/Offering Sets, normal allowance, no summon-response traps |
| 072 | 2 | Swap per-copy return effects; only one extra Normal Summon |
| 073 | 4 | LADD destruction before revival; Dupe missed timing and positive control |
| 074 | 4 | Actual Union equip blocks Treeborn; unequipped monster allows revival |
| 075 | 3 | All 88 script files: production/certification bytes and rule-isolated caches |
| 076 | 2 | Swap can return after extra summon is spent; no third Normal Summon |

These groups add **95** passing cases to the 173 in Tests 036–052.
Tests 019–035 contributed 113 focused passes, giving **381** for 019–076.
All include Rule 5 comparisons where rules differ, or equivalent-behavior
controls where both rules agree. Test 067 runs two separate duels within each
registered case. Test 075 checks 88 files per case.

### Corrections and shared production behavior

Historical scripts now have 26 card overrides and one utility.lua suffix for
Union restrictions. They are selected only for Rule 1; all other rule values
retain original pinned script bytes. Original script files remain untouched.
The small metadata fixture inventory is 88 cards, not the entire legal pool.

Additional Rule-1-only corrections cover Prisma, Rescue Cat, Strike Ninja,
REDMD, Fortune Lady Light, Future Fusion, Cyber Phoenix/Jade Knight, Armory Arm,
Mark of the Rose, Destiny End Dragoon, Black Garden, My Body as a Shield,
and Swap Frog. Goyo/Brain/Sangan and earlier battle/phase corrections remain.
The runtime groups above document what each correction proves.

Test 066 found that the core forced a Soul Exchange target into the mandatory
tribute set. Rule 1 now offers that target alongside ordinary legal tributes
for both a summon and an effect cost; Rule 5 retains the mandatory selection.

Tests 069–071 found that the Lua Summon, MSet, and SynchroSummon APIs deferred
the procedure until after the resolving link. Rule 1 now executes it within
the link, preventing an intervening summon-negation window. Rule 5 retains
deferred execution, including its CL1 summon-negation window. CL2 Offering
still misses Torrential timing after the lower link resolves.

Test 067 initially used an invalid positive expectation for Cyber Dragon's
pending inherent summon. That was corrected into a negative control in both
rules: the monster is not yet successfully on the field. The positive
historical case uses Oppression threatening an on-field REDMD that activated
its revival effect. My Body can negate that face-up Trap effect in Edison.

Swap Frog requires two historical changes: removing the shared activation
flag check, and permitting another copy's return effect after the extra summon
has been used. The engine's extra-summon cap remains intact. Test 076 verifies
the later activation still cannot grant a third Normal Summon.

Sources: [functional errata](https://www.edisonformat.com/rulings/relevant-errata)
and [Swap Frog rulings](https://edisonrul.ing/cards/s/swap-frog.html).
These references define the reviewed historical cases; they do not certify
unexamined card scripts.

The production read_card_script function is extracted into
ygoenv/ygoenv/ygopro/script_loader.h, retaining its buffer ownership contract
and defaulting to the shared Edison rule. Test 075 calls that exact reader
and independently compares the certification cache against expected pinned
or overridden bytes. It checks 1→5→1 switching, path basenames, helper suffixes,
missing files, and non-Edison rule exclusion. Behavioral fixtures continue
to use the real Lua/core runtime.

### Completed minimum coverage matrix

| Handoff requirement | Representative evidence |
| --- | --- |
| Starting LP, opening hand, first-turn draw | 001 |
| Historical ignition priority | 002 |
| Single active Field Spell/replacement | 003 |
| SEGOC and Early Trigger ordering | 004–006, 017–018, 033 |
| Historical trigger locations | 025, 043; delayed return 044 |
| Damage Step timing/exceptions | 007–010, 029–030, 068 |
| Summon responses | 002, 069–071 |
| Card-specific pre-errata behavior | 019–020, 034–035, 041, 044–074, 076 |
| March 2010 limits | 037 |
| April 2010 card pool | 038 |
| Extra Deck legality | 036, 039 |
| Historical script/errata consistency | 075, per-group asset lock checks |

The finite documented backlog has no remaining failing group. This coverage
does not prove every Edison card interaction, every legal deck, or all
historical rulings. The next validation milestone is an explicitly requested
full regression run against one final build, then legal fixed-deck simulation
smoke tests. Training and deck optimization remain outside this change.

### Final verification and evidence

For each new or failing group, the commands were:

    xmake b edison_rules
    python tests/edison/run.py --test NNN --fail-fast

NNN was selected individually; there was no unfiltered certification run.
Core changes used the configured pinned-package reinstall described above.
Final production verification used xmake b ygopro_ygoenv, which built and
copied the native module successfully.

The final offline audit is
build/edison-certification/backlog-audit.json. It verifies all saved 019–076
reports are PASS, both traces match their recorded SHA-256, all pinned assets
match the lock, all fixture headers are registered, and the installed
production module matches the freshly built artifact. The installed core
also passes a reverse-apply check against the complete recorded patch.

Rule 5 protocol traces are byte-identical across the relevant before/after
fixes for Tests 068 (2 controls), 069 (4), 072 (1), and 076 (1). Test 075
separately verifies all 88 Rule 5 script files remain byte-identical to upstream.

Source fingerprints now automatically include every fixture header. Reports
before this inventory fix retain their original recorded metadata; some later
headers were absent from those older fingerprints. Do not treat an older report
as proof against the final source tree. Likewise, Test 070's saved modern
Observed sentence used historical wording; its protocol trace and assertions
validate deferred modern timing, and the display text has since been corrected.

Final native binary SHA-256:
2f6aca0bd88fb3079bfd073129a8844c75d34f3bd8e895fb83500fb8d596e287

Final production module SHA-256:
54a16ac63c773d321a25ad1fb1a05f3d1d6fefac07449c4f0652717a77cc2a97

Core patch SHA-256:
5a6d18d7bdac6cb305cf414a7409b374c3ea7519c18a4cc41884686f05fede61

At the end of the focused backlog pass, work remained uncommitted. The subsequent
full-suite and production checkpoint is documented in edison-baseline.md.
Dependency versions, upstream source pins, and existing checkpoint tags remain
unchanged.


## Test 077 — complete production duels

Test 077 covers the observed Dupe Frog → Des Frog preload dependency and
thread-count repeatability using legal fixed decks through the real production
Gymnasium API. The original seed must actually select Dupe Frog's Normal Summon
and complete naturally beyond the old crash. Four seeds are also compared with
one versus four worker threads.

The runner fingerprints all Python/C++ fixture sources automatically and includes
077 beside Test 042 production ingestion. Current result:
**507 native + 10 ingestion + 2 production interaction = 519 PASS**.
Both full traces are identical. The full run and 162-execution playing baseline
are archived with the checkpoint; see edison-baseline.md.


## Observation and agent regression additions (078–080)

Test 078 checks production identities, private initial zones, action references
and overflow, both budget limits, zero-reward truncation, and reset. Test 079
checks heuristic decisions, actual Machina Gearframe search interactions,
parallel determinism and relative worker-output paths. Test 080 checks shared
visibility rules for hidden zones, searches, explicit reveals, facedown cards
and public overlays. The full suite now contains 527 passing cases.
See [edison-agents.md](edison-agents.md) for evaluation, stress, training and evidence.


## Decision-context additions (081–082)

Test 081 validates recipient-scoped hint lifecycle, private hint redaction,
Brionac cost/target stages, and resolving discards, with Edison and Rule 5
controls. Test 082 exercises context-sensitive policy choices, real production
selection/mask/privacy behavior, retained selections, mandatory materials,
serial/parallel replay and frozen-policy compatibility. The full suite now
contains 536 passing cases. Schema, limitations and evidence are documented in
[edison-decision-context.md](edison-decision-context.md).


## Context-aware PPO regression additions (083)

Test 083 validates PPO gradients and clipping, context feature/privacy contracts,
checkpoint round trips, legal stochastic sampling, learner-seat outcome
attribution, production replay, and unique/balanced/disjoint training/evaluation
seeds. The full suite now passes 540 cases. See
[edison-rl-pilot.md](edison-rl-pilot.md) for the bounded training run and held-out
benchmark results.


## PPO replication controls (083E)

Test 083E verifies optimizer-seed configuration, different seeded hidden weights
with the same initial uniform policy, correct run-level sample statistics, and
rejection of failed/incomplete replication sets. The full suite now passes
541 cases. See [edison-rl-replication.md](edison-rl-replication.md) for the fixed
three-seed study.

## PPO budget and evaluation isolation (083F)

Test 083F verifies the 3,072-duel schedule preserves the 768-duel prefix, balances
seats/opponents, separates new training, validation and final-test seeds, and
rejects a changed frozen checkpoint. The full suite now passes 542 cases.
See [edison-rl-budget.md](edison-rl-budget.md) for the controlled budget study.

## Decision-context ablation (083G–H)

Test 083G covers explicit context masking, gradient invariance, checkpoint mode
persistence and legacy full-context compatibility. Test 083H checks production
rollout masking, identical initial full/masked trajectories and thread-count replay
for both learner seats. The full suite now passes 544 cases. See
[edison-rl-ablation.md](edison-rl-ablation.md) for the paired study.

## Hardening (084–085)

Test 084 plants Lua errors through `run_worker()` and requires them to fail the worker closed, with a clean control.
Test 085 plays each previously crashing prompt class through production with the observation contract and all three
policies checked on every decision, and confirms all 18 gauntlet lists pass production legality. Test 082D now compares
against a baseline regenerated after the rule fixes, because those fixes change seeded games on purpose. The full suite
now passes 567 cases. See [edison-hardening.md](edison-hardening.md) for the changes, evidence and limits.

## Deck option labels (086–087)

Test 086 plays Deck prompts through production and compares each option with the card code in the engine's own prompt, through the
test-only differential in `prompt_audit.h`. It covers mixed Deck contents, Deck-or-field prompts, duplicates, non-candidates, two picks,
unselect lists, lone options, the other seat's selection, revealed and face-up copies, and blank opposing Deck pointers, with guards for
the verbose parser and for the audit commands' verdicts. Test 087 plays one seed with two hidden Deck orders and requires byte-identical
policy inputs throughout the first turn. Test 082D now compares against a baseline regenerated after the fix, because its interaction
counts record the labels shown. The full suite now passes 585 cases. See [edison-deck-labels.md](edison-deck-labels.md).
