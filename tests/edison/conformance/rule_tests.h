#pragma once
// Fixtures for the 13 Edison rule differences, in the order the source lists them.
// Source for every test here unless noted:
// https://www.edisonformat.com/edison-rule-differences.html
//
// Numbering: 2xx = rule differences, 3xx = errata, 4xx = per-card rulings. Numbers do
// not collide with the certification suite, which ends at 083.
#include "conformance_harness.h"
#include "cards.h"

namespace conformance {
using namespace card_codes;

inline void observed(const std::string& text) { std::cout << "Observed: " << text << '\n'; }

struct Registry {
  std::function<void(const std::string&, const std::string&, const std::string&, bool, const std::function<void()>&)> test;
  // Each fixture runs under Edison, then under Rule 5 as a comparison. Rule 5 records a
  // difference rather than failing on it: it is not the target ruleset, so only a fixture
  // that cannot run fails there.
  void both(const std::string& n, const std::string& title, const std::string& exp, std::function<void(int, bool)> body) {
    test(n, title, exp, false, [=] { body(rule1, true); });
    test(n, title + " (Rule 5 comparison)", "Recorded for comparison with Edison; only a fixture that cannot run fails here.", true, [=] {
      try { body(rule5, false); observed("Rule 5 meets the same expectation."); }
      catch (const RuleMismatch& e) { observed(std::string("Rule 5 differs: ") + e.what()); }
    });
  }
};
inline std::string yn(bool v) { return v ? "yes" : "no"; }

// ---------------------------------------------------------------- Rule 2: Field Spells
inline void field_chain_destroyed(ConfAssets& a, int rule, bool historical, bool trace) {
  Play p(a, rule, trace);
  p.field(yami, 1);
  p.hand(forest, 0);
  p.backrow(dust_tornado, 1);
  p.activate(forest);
  p.chain(dust_tornado).pick(forest);
  p.start();
  setup(p.activated(dust_tornado), "Dust Tornado did not chain");
  observed("Yami on field " + yn(p.zone_has(1, LOCATION_SZONE, yami)) + ", Forest in GY " + yn(p.zone_has(0, LOCATION_GRAVE, forest)));
  setup(p.zone_has(0, LOCATION_GRAVE, forest), "Forest was not destroyed");
  expect_rule(p.zone_has(1, LOCATION_SZONE, yami), "Rule 2: replacement must not happen when the new Field Spell is destroyed in the chain");
}
inline void field_set_village(ConfAssets& a, int rule, bool historical, bool trace) {
  Play p(a, rule, trace);
  p.field(secret_village, 1);
  p.monster(dark_magician, 1);
  p.hand(umi, 0);
  p.setst(umi);
  bool activation_offered = true;
  p.check([&](const Event& e) { activation_offered = Play::listed(e.idle[5], umi); });
  p.start();
  observed("Village kept " + yn(p.zone_has(1, LOCATION_SZONE, secret_village)) + ", Set Umi activation offered " + yn(activation_offered));
  expect_rule(p.zone_has(1, LOCATION_SZONE, secret_village), "Rule 2: Setting a Field Spell must not destroy the opponent's active one");
  expect_rule(!activation_offered, "Rule 2 example: Village must still prevent activating the Set Umi");
}
inline void field_over_geartown(ConfAssets& a, int rule, bool historical, bool trace, bool set, bool facedown) {
  // Positive control: destroyed by an effect, Geartown's trigger must be available. Without
  // this, "not offered" below could only mean the fixture's trigger condition is broken.
  {
    Play c(a, rule, trace);
    c.add(geartown, 0, LOCATION_SZONE, 5, facedown ? POS_FACEDOWN_ATTACK : POS_FACEUP_ATTACK);
    c.deck(ancient_gear, 0);
    c.hand(mst, 0);
    c.activate(mst).pick(geartown);
    c.start();
    setup(c.zone_has(0, LOCATION_GRAVE, geartown), "Control: Geartown was not destroyed");
    observed("Control, destroyed by MST: Geartown trigger offered " + yn(c.offered(geartown, 0)));
    setup(c.offered(geartown, 0), "Control: Geartown's trigger must be available when destroyed by an effect");
  }
  Play p(a, rule, trace);
  p.add(geartown, 0, LOCATION_SZONE, 5, facedown ? POS_FACEDOWN_ATTACK : POS_FACEUP_ATTACK);
  p.hand(mausoleum, 0);
  p.deck(ancient_gear, 0);
  if (set) p.setst(mausoleum); else p.activate(mausoleum);
  p.accept(geartown);
  p.pick(ancient_gear);
  p.start();
  setup(p.zone_has(0, LOCATION_GRAVE, geartown), "Geartown was not replaced");
  const bool triggered = p.offered(geartown, 0);
  observed("Geartown trigger offered " + yn(triggered) + ";" + p.chain_text());
  if (!historical) return;  // Rule 5 sends it without destroying it; recorded, not asserted.
  if (set) expect_rule(triggered, "Rule 2: Setting over Geartown destroys it by game mechanic with no Chain, so it can trigger");
  else expect_rule(!triggered, "Rule 2: Activating over Geartown starts a Chain, so its trigger cannot activate");
}

// ---------------------------------------------------------------- Rule 3: Union
inline void union_substitute(ConfAssets& a, int rule, bool trace, uint32_t host, uint32_t unit, bool expect_substitute, bool historical) {
  Play p(a, rule, trace);
  p.monster(host, 0, 0);
  p.monster(unit, 0, 1);
  p.hand(smashing_ground, 1);
  p.activate(unit).pick(host);
  p.end();
  p.activate(smashing_ground).pick(host);
  p.check([](const Event&) {});
  p.start();
  setup(p.activated(smashing_ground), "Smashing Ground did not resolve");
  const bool host_alive = p.zone_has(0, LOCATION_MZONE, host);
  observed("Equipped monster survived " + yn(host_alive) + ", Union in GY " + yn(p.zone_has(0, LOCATION_GRAVE, unit)));
  if (!historical) return;
  expect_rule(host_alive == expect_substitute, expect_substitute
      ? "Rule 3: a listed Union must be destroyed in place of the equipped monster"
      : "Rule 3: this Union is not listed as replacing effect destruction, so the equipped monster must be destroyed");
}

// ---------------------------------------------------------------- Rule 5: Trap Monsters
// Turn 2 activation by P1, so it is a real Trap Monster when P0 acts on turn 3. There is
// no response window before the first idle prompt of turn 1.
inline void p1_activates_on_turn_2(Play& p, uint32_t code) {
  p.end();
  p.activate(code);
  p.end();
}
// Activates Set Apophis on P0's first Main Phase; returns once it is a monster.
inline void field_full_blocks_trap_monster(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  for (int i = 0; i < 5; ++i) p.monster(jerry_beans, 0, i);
  p.backrow(apophis, 0);
  bool offered = true;
  p.check([&](const Event& e) { offered = Play::listed(e.idle[5], apophis); });
  p.start();
  observed("Apophis activation offered with no Monster Zone: " + yn(offered));
  expect_rule(!offered, "Rule 5: cannot activate a Trap Monster with no empty Monster Zone");
}
inline void trap_monster_blocks_zone(ConfAssets& a, int rule, bool historical, bool trace) {
  Play p(a, rule, trace);
  p.backrow(apophis, 0);
  p.hand(mirror_force, 0);
  p.activate(apophis);
  p.setst(mirror_force);
  p.start();
  setup(p.zone_has(0, LOCATION_MZONE, apophis), "Apophis did not become a monster");
  setup(!p.place_masks.empty(), "No placement prompt observed");
  const bool blocked = (p.place_masks.back() >> 8) & 1;
  observed("S/T zone 0 unavailable after Apophis left it: " + yn(blocked));
  if (historical) expect_rule(blocked, "Rule 5: a Trap Monster keeps its S/T Zone unusable");
}
inline void control_needs_both_zones(ConfAssets& a, int rule, bool trace) {
  // Enemy Controller also has a position option, so being offered proves nothing here:
  // the fixture must attempt control. With one open S/T Zone the attempt succeeds.
  for (int occupied : {4, 5}) {
    Play p(a, rule, trace);
    p.backrow(apophis, 1);
    p.monster(jerry_beans, 0, 0);
    for (int i = 0; i < occupied; ++i) p.backrow(i == 0 ? enemy_controller : mirror_force, 0, i);
    p1_activates_on_turn_2(p, apophis);
    p.activate(enemy_controller);
    p.pick(jerry_beans).pick(apophis);
    bool control_option = false;
    p.hook = [&](const Event& e) {
      if (e.kind != MSG_SELECT_OPTION) return false;
      for (size_t i = 0; i < e.options.size(); ++i)
        if ((e.options[i] & 0xf) == 1) { control_option = true; p.d.respond(int(i)); return true; }
      return false;
    };
    p.start();
    setup(p.activated(apophis) && p.activated(enemy_controller), "Fixture did not reach the attempt");
    const bool taken = p.zone_has(0, LOCATION_MZONE, apophis);
    observed(std::to_string(occupied) + " S/T Zones occupied: control option offered " + yn(control_option) + ", Apophis taken " + yn(taken));
    if (occupied == 4) { setup(taken, "Control must succeed with a free S/T Zone"); continue; }
    expect_rule(!taken, "Rule 5: gaining control of a Trap Monster needs an open S/T Zone too");
  }
}
inline void jinzo_reverts(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.backrow(apophis, 1);
  p.hand(jinzo, 0);
  p.monster(jerry_beans, 0, 0);
  // P1 activates Apophis in response to entering Main Phase on its own turn; then P0 summons.
  p.end();
  p.activate(apophis);
  p.end();
  p.yesno_answers = {1};
  p.pick(jerry_beans);
  p.summon(jinzo);
  p.start();
  setup(p.zone_has(0, LOCATION_MZONE, jinzo), "Jinzo was not summoned");
  observed("Apophis in Monster Zone " + yn(p.zone_has(1, LOCATION_MZONE, apophis)) + ", in S/T Zone " + yn(p.zone_has(1, LOCATION_SZONE, apophis)));
  expect_rule(!p.zone_has(1, LOCATION_MZONE, apophis) && p.zone_has(1, LOCATION_SZONE, apophis),
              "Rule 5: a negated Trap Monster reverts to a Trap Card in the S/T Zone immediately");
}
inline void attack_set_jinzo(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.set_monster(jinzo, 0);
  p.backrow(apophis, 1);
  p.end();
  p.activate(apophis);
  p.battle();
  p.attack(apophis, jinzo);
  p.check([](const Event&) {});
  p.end_battle();
  p.start();
  setup(p.moved(jinzo, LOCATION_MZONE) || p.zone_has(0, LOCATION_MZONE, jinzo) || p.zone_has(0, LOCATION_GRAVE, jinzo), "Jinzo fixture lost");
  observed("Battles calculated " + std::to_string(p.battles.size()) + ", Apophis in S/T " + yn(p.zone_has(1, LOCATION_SZONE, apophis)));
  expect_rule(p.battles.empty(), "Rule 5: when a flipped Jinzo reverts the attacking Trap Monster, Damage Calculation is not applied");
}
inline void stolen_returns_to_owner(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.backrow(apophis, 1);
  p.hand(snatch_steal, 0);
  p.hand(jinzo, 0);
  p.monster(jerry_beans, 0, 1);
  p1_activates_on_turn_2(p, apophis);
  p.activate(snatch_steal).pick(apophis);
  p.yesno_answers = {1};
  p.summon(jinzo);
  p.pick(jerry_beans);
  p.start();
  setup(p.activated(apophis), "Apophis was not activated");
  setup(p.activated(snatch_steal), "Snatch Steal was not activated");
  setup(p.zone_has(0, LOCATION_MZONE, jinzo), "Jinzo was not summoned");
  observed("Apophis P1 S/T " + yn(p.zone_has(1, LOCATION_SZONE, apophis)) + ", P0 S/T " + yn(p.zone_has(0, LOCATION_SZONE, apophis)) +
           ", P0 monster " + yn(p.zone_has(0, LOCATION_MZONE, apophis)));
  expect_rule(p.zone_has(1, LOCATION_SZONE, apophis), "Rule 5: a stolen Trap Monster returns to its owner's S/T Zone when Jinzo is Summoned");
}
inline void book_on_trap_monster(ConfAssets& a, int rule, bool trace, bool stolen) {
  Play p(a, rule, trace);
  if (stolen) {
    p.backrow(apophis, 1);
    p.hand(snatch_steal, 0);
    p1_activates_on_turn_2(p, apophis);
    p.activate(snatch_steal).pick(apophis);
  } else {
    p.backrow(apophis, 0);
    p.activate(apophis);
  }
  p.hand(book_moon, 0);
  bool target_offered = false;
  p.check([&](const Event& e) { target_offered = Play::listed(e.idle[5], book_moon); });
  p.activate(book_moon).pick(apophis);
  p.start();
  setup(p.activated(apophis), "Apophis was not activated");
  observed("Apophis P0: S/T " + yn(p.zone_has(0, LOCATION_SZONE, apophis)) + ", monster " + yn(p.zone_has(0, LOCATION_MZONE, apophis)) +
           ". P1: S/T " + yn(p.zone_has(1, LOCATION_SZONE, apophis)) + ", monster " + yn(p.zone_has(1, LOCATION_MZONE, apophis)) + ", GY " + yn(p.zone_has(1, LOCATION_GRAVE, apophis)));
  expect_rule(target_offered, "Rule 5: Book of Moon can flip a Trap Monster");
  expect_rule(!p.zone_has(0, LOCATION_MZONE, apophis) && p.zone_has(0, LOCATION_SZONE, apophis),
              "Rule 5: a Trap Monster flipped face-down is Set in its controller's S/T Zone");
}
inline void trap_monster_protection(ConfAssets& a, int rule, bool trace, uint32_t attacker_card, uint32_t protector, bool expected) {
  // One protector at a time: a second Set Trap would itself be a Heavy Storm target and
  // would make Fake Trap legal for a reason unrelated to the Trap Monster.
  Play p(a, rule, trace);
  p.backrow(apophis, 1, 0);
  p.backrow(protector, 1, 1);
  if (attacker_card == raigeki_break) { p.backrow(raigeki_break, 0, 0); p.hand(jerry_beans, 0); }
  else if (attacker_card == lightning_vortex) { p.hand(lightning_vortex, 0); p.hand(jerry_beans, 0); }
  else p.hand(attacker_card, 0);
  p1_activates_on_turn_2(p, apophis);
  p.activate(attacker_card);
  p.pick(apophis);
  p.start();
  setup(p.activated(apophis), "Apophis was not activated");
  setup(p.link_index(attacker_card) > p.link_index(apophis), "Removal must follow Apophis's activation");
  const bool on = p.offered(protector, 1);
  observed("Protector offered " + yn(on));
  expect_rule(on == expected, std::string("Rule 5: ") + (protector == fake_trap ? "Fake Trap" : "My Body as a Shield") + " should " + (expected ? "" : "not ") + "be available");
}
inline void caius_zoma(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.backrow(zoma, 1);
  p.monster(jerry_beans, 0, 0);
  p.hand(caius, 0);
  p1_activates_on_turn_2(p, zoma);
  p.yesno_answers = {1};
  p.summon(caius);
  p.pick(jerry_beans).pick(zoma);
  p.start();
  setup(p.activated(zoma), "Zoma was not activated");
  setup(p.activated(caius), "Caius did not activate");
  setup(p.zone_has(1, LOCATION_REMOVED, zoma), "Zoma was not banished");
  observed("Damage events " + std::to_string(p.damage.size()) + ", P1 LP " + std::to_string(p.d.lp(1)));
  expect_rule(p.damage.empty(), "Rule 5: Zoma is removed as a Trap Card, so Caius inflicts no damage");
}
inline void michizure_trap_monster(ConfAssets& a, int rule, bool trace) {
  {
    Play c(a, rule, trace);
    c.monster(bazoo, 0);
    c.backrow(michizure, 0, 1);
    c.monster(blue_eyes, 1);
    c.hand(smashing_ground, 1);
    c.end();
    c.activate(smashing_ground);
    c.start();
    observed("Control, ordinary monster destroyed: Michizure offered " + yn(c.offered(michizure, 0)));
    setup(c.offered(michizure, 0), "Control: Michizure must be available for an ordinary monster");
  }
  Play p(a, rule, trace);
  p.backrow(apophis, 0, 0);
  p.backrow(michizure, 0, 1);
  p.monster(blue_eyes, 1);
  p.hand(smashing_ground, 1);
  p.activate(apophis);
  p.end();
  p.activate(smashing_ground);
  p.start();
  setup(p.activated(apophis), "Apophis was not activated");
  setup(p.zone_has(0, LOCATION_GRAVE, apophis), "Apophis was not destroyed");
  observed("Michizure offered " + yn(p.offered(michizure, 0)));
  expect_rule(!p.offered(michizure, 0), "Rule 5: a Trap Monster sent to the GY does not allow Michizure");
}
inline void shura_trap_monster(ConfAssets& a, int rule, bool trace) {
  {
    Play c(a, rule, trace);
    c.monster(shura, 0);
    c.monster(jerry_beans, 1);
    c.deck(gale, 0);
    c.end();
    c.end();
    c.battle();
    c.attack(shura, jerry_beans);
    c.check([](const Event&) {});
    c.start();
    observed("Control, ordinary monster destroyed: Shura offered " + yn(c.offered(shura, 0)));
    setup(c.offered(shura, 0), "Control: Shura must trigger on an ordinary monster");
  }
  Play p(a, rule, trace);
  p.monster(shura, 0);
  p.backrow(apophis, 1);
  p.deck(gale, 0);
  p1_activates_on_turn_2(p, apophis);
  p.battle();
  p.attack(shura, apophis);
  p.check([](const Event&) {});
  p.start();
  setup(p.activated(apophis), "Apophis was not activated");
  setup(p.battles.size() == 1, "Battle did not occur");
  setup(p.zone_has(1, LOCATION_GRAVE, apophis), "Apophis was not destroyed");
  observed("Shura offered " + yn(p.offered(shura, 0)));
  expect_rule(!p.offered(shura, 0), "Rule 5: a Trap Monster destroyed by battle does not trigger Shura");
}
inline void penguin_major_riot(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.set_monster(penguin_soldier, 0);
  p.backrow(apophis, 1, 0);
  p.backrow(major_riot, 1, 1);
  p1_activates_on_turn_2(p, apophis);
  p.accept(penguin_soldier);
  p.yesno_answers = {1};
  p.reposition(penguin_soldier);
  p.pick(apophis);
  p.start();
  setup(p.activated(apophis), "Apophis was not activated");
  setup(p.activated(penguin_soldier), "Penguin Soldier did not activate");
  setup(p.zone_has(1, LOCATION_HAND, apophis), "Penguin did not return Apophis");
  observed("Major Riot offered " + yn(p.offered(major_riot, 1)));
  expect_rule(!p.offered(major_riot, 1), "Rule 5: a Trap Monster returns to hand as a Trap Card, so Major Riot cannot be activated");
}
inline void matter_transporter(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.backrow(apophis, 0, 0);
  p.backrow(imt, 0, 1);
  p.activate(apophis);
  p.activate(imt).pick(apophis);
  p.end();
  p.stop_at_turn = 2;
  p.start();
  setup(p.activated(imt), "Transporter did not activate");
  const bool in_gy = p.zone_has(0, LOCATION_GRAVE, apophis);
  observed("Apophis after return: GY " + yn(in_gy) + ", monster " + yn(p.zone_has(0, LOCATION_MZONE, apophis)) + ", S/T " + yn(p.zone_has(0, LOCATION_SZONE, apophis)));
  expect_rule(in_gy, "Rule 5: a temporarily removed Trap Monster returns as a Trap Card and is destroyed immediately");
}

inline void time_machine_trap_monster(ConfAssets& a, int rule, bool trace) {
  {
    Play c(a, rule, trace);
    c.monster(shura, 0);
    c.monster(jerry_beans, 1, 0);
    c.backrow(time_machine, 1, 1);
    c.end();
    c.end();
    c.battle();
    c.attack(shura, jerry_beans);
    c.check([](const Event&) {});
    c.start();
    observed("Control, ordinary monster destroyed: Time Machine offered " + yn(c.offered(time_machine, 1)));
    setup(c.offered(time_machine, 1), "Control: Time Machine must be available for an ordinary monster");
  }
  Play p(a, rule, trace);
  p.monster(shura, 0);
  p.backrow(apophis, 1, 0);
  p.backrow(time_machine, 1, 1);
  p1_activates_on_turn_2(p, apophis);
  p.battle();
  p.attack(shura, apophis);
  p.check([](const Event&) {});
  p.start();
  setup(p.activated(apophis), "Apophis was not activated");
  setup(p.zone_has(1, LOCATION_GRAVE, apophis), "Apophis was not destroyed by battle");
  observed("Time Machine offered " + yn(p.offered(time_machine, 1)));
  expect_rule(!p.offered(time_machine, 1), "Rule 5: a Trap Monster sent to the GY cannot be revived with Time Machine");
}
inline void doomed_trap_monster(ConfAssets& a, int rule, bool trace) {
  // Return of the Doomed has no target at activation: it retrieves at the End Phase, so the
  // measurement is what reaches the hand. An ordinary monster is the positive control.
  for (bool trap_monster : {false, true}) {
    Play p(a, rule, trace);
    const uint32_t victim = trap_monster ? apophis : bazoo;
    if (trap_monster) { p.backrow(apophis, 0, 0); p.activate(apophis); } else p.monster(bazoo, 0, 0);
    p.hand(return_doomed, 0);
    p.hand(jerry_beans, 0);
    p.monster(blue_eyes, 1);
    p.end();
    p.end();
    p.battle();
    p.attack(victim, blue_eyes);
    p.main2();
    p.activate(return_doomed);
    p.pick(jerry_beans);
    p.end();
    p.yesno_answers = {1};
    p.stop_at_turn = 4;
    p.start();
    setup(p.activated(return_doomed), "Return of the Doomed was not activated");
    setup(p.moved(victim, LOCATION_GRAVE, LOCATION_MZONE), "Monster was not destroyed by battle");
    const bool returned = p.zone_has(0, LOCATION_HAND, victim);
    observed(std::string(trap_monster ? "Apophis" : "Control, Bazoo") + " returned to hand: " + yn(returned));
    if (!trap_monster) setup(returned, "Control: Return of the Doomed must retrieve an ordinary monster");
    else expect_rule(!returned, "Rule 5: a Trap Monster in the GY cannot be retrieved with Return of the Doomed");
  }
}
inline void dimensionhole_trap_monster(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.backrow(apophis, 0, 0);
  p.hand(dimensionhole, 0);
  p.activate(apophis);
  p.activate(dimensionhole).pick(apophis);
  p.end();
  p.end();
  p.check([](const Event&) {});
  p.start();
  setup(p.activated(dimensionhole), "Dimensionhole did not activate");
  setup(p.turn >= 3, "Did not reach the next turn");
  observed("Apophis after the Standby Phase: GY " + yn(p.zone_has(0, LOCATION_GRAVE, apophis)) + ", monster " + yn(p.zone_has(0, LOCATION_MZONE, apophis)) + ", S/T " + yn(p.zone_has(0, LOCATION_SZONE, apophis)));
  expect_rule(p.zone_has(0, LOCATION_GRAVE, apophis), "Rule 5: a temporarily removed Trap Monster returns as a Trap Card and is destroyed immediately");
}

// ---------------------------------------------------------------- Rule 6: Ignition priority
inline void malicious_priority(ConfAssets& a, int rule, bool historical, bool trace) {
  Play p(a, rule, trace);
  p.hand(grepher, 0);
  p.hand(malicious, 0);
  p.deck(malicious, 0);
  p.hand(dd_crow, 1);
  p.pick(malicious);
  p.spsummon(grepher);
  p.chain(malicious);
  p.pick(malicious, 0, LOCATION_DECK);
  bool idle_offers = false;
  p.check([&](const Event& e) { idle_offers = Play::listed(e.idle[5], malicious); });
  p.start();
  setup(p.zone_has(0, LOCATION_MZONE, grepher), "Grepher was not summoned");
  const int m = p.link_index(malicious);
  observed("Malicious offered in Summon window " + yn(p.offered(malicious, 0)) + ", Crow offered " + yn(p.offered(dd_crow, 1)) + ", usable from idle afterwards " + yn(idle_offers) + ";" + p.chain_text());
  setup(m == 0 || idle_offers, "Malicious must be activatable at all in this fixture");
  if (!historical) return;
  expect_rule(m == 0, "Rule 6 Ex.1: Malicious's Graveyard Ignition Effect can be Chain Link 1 in the Summon window, before D.D. Crow");
}
inline void chaos_priority(ConfAssets& a, int rule, bool historical, bool trace) {
  Play p(a, rule, trace);
  p.hand(chaos_sorcerer, 0);
  p.grave(blue_eyes, 0);
  p.grave(sangan, 0);
  p.monster(goyo, 1);
  p.backrow(bottomless, 1);
  p.spsummon(chaos_sorcerer);
  p.chain(chaos_sorcerer);
  p.chain(bottomless);
  p.pick(goyo);
  p.start();
  setup(p.zone_has(0, LOCATION_REMOVED, blue_eyes), "Chaos Sorcerer was not summoned");
  observed(p.chain_text());
  if (!historical) return;
  expect_rule(p.link_index(chaos_sorcerer) == 0, "Rule 6 Ex.2: Chaos Sorcerer's Ignition Effect can be Chain Link 1 before Bottomless");
}
inline void demise_priority(ConfAssets& a, int rule, bool historical, bool trace) {
  Play p(a, rule, trace);
  p.hand(demise, 0);
  p.hand(end_of_world, 0);
  p.monster(blue_eyes, 0, 0);
  p.backrow(torrential, 1);
  p.activate(end_of_world);
  p.chain(demise);
  p.pick(demise).pick(blue_eyes);
  p.start();
  setup(p.zone_has(0, LOCATION_MZONE, demise) || p.zone_has(0, LOCATION_GRAVE, demise), "Demise was not Ritual Summoned");
  observed("Demise offered " + yn(p.offered(demise, 0)) + ", Torrential offered " + yn(p.offered(torrential, 1)) + ";" + p.chain_text());
  if (!historical) return;
  expect_rule(p.offered(demise, 0), "Rule 6 Ex.3: Demise's Ignition Effect is available in the window after its Ritual Summon");
}
inline void garden_strips_priority(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.field(black_garden, 1);
  p.hand(dark_armed, 0);
  for (int i = 0; i < 3; ++i) p.grave(sangan, 0);
  p.spsummon(dark_armed);
  p.start();
  setup(p.activated(black_garden), "Black Garden did not trigger");
  observed("DAD offered during the trigger Chain " + yn(p.offered(dark_armed, 0)) + ";" + p.chain_text());
  expect_rule(p.links.at(0).code == black_garden, "Rule 6: Black Garden's trigger starts the Chain");
  expect_rule(!p.offered(dark_armed, 0), "Rule 6: no Ignition Effect when the Summon starts a Chain");
}
// Owner ruling, November 4, 2026: declining the optional trigger leaves Ignition Effect Priority. The
// Swap Frog and Alien Dog rulings agree. This overrides the Armageddon Knight example in Rule 6 (Ex.3).
inline void knight_trigger_priority(ConfAssets& a, int rule, bool trace, bool accept_trigger) {
  Play p(a, rule, trace);
  p.hand(armageddon_knight, 0);
  p.monster(bazoo, 0, 1);
  p.grave(blue_eyes, 0);
  p.deck(sangan, 0);
  p.backrow(torrential, 1);
  if (accept_trigger) p.accept(armageddon_knight);
  p.summon(armageddon_knight);
  p.stop = [&](const Event& e) { return e.kind == MSG_SELECT_IDLECMD && !p.idle_prompts.empty(); };
  p.start();
  setup(p.offered(torrential, 1), "Torrential was not offered in the Summon window");
  observed("Bazoo Ignition offered " + yn(p.offered(bazoo, 0)) + ";" + p.chain_text());
  if (accept_trigger) expect_rule(!p.offered(bazoo, 0), "Rule 6 Ex.3 (no priority): a triggered Summon gives the turn player no Ignition Effect as Chain Link 1");
  else expect_rule(p.offered(bazoo, 0), "Owner ruling, with the Swap Frog and Alien Dog rulings: after declining the trigger, Ignition Effects are still offered with priority");
}
inline void sangan_material_strips_priority(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.monster(junk_synchron, 0, 0);
  p.monster(sangan, 0, 1);
  p.extra(brionac, 0);
  p.hand(jerry_beans, 0);
  p.deck(dupe, 0);
  p.spsummon(brionac);
  p.pick(junk_synchron);
  p.pick(dupe);
  p.start();
  setup(p.zone_has(0, LOCATION_MZONE, brionac), "Brionac was not Synchro Summoned");
  observed("Brionac offered " + yn(p.offered(brionac, 0)) + ";" + p.chain_text());
  expect_rule(p.activated(sangan), "Rule 6 Ex.2 (no priority): Sangan's trigger starts a Chain after the Synchro Summon");
  expect_rule(!p.offered(brionac, 0), "Rule 6 Ex.2 (no priority): Brionac's Ignition Effect is unavailable when Sangan starts the Chain");
}
// "A chain has finished resolving": the turn player gets fast effects only, and if they
// pass, priority reaches the opponent before any Ignition Effect.
inline void after_chain_priority(ConfAssets& a, int rule, bool historical, bool trace) {
  Play p(a, rule, trace);
  p.hand(armageddon_knight, 0);
  p.deck(malicious, 0);
  p.deck(malicious, 0);
  p.hand(dd_crow, 1);
  p.accept(armageddon_knight);
  p.pick(malicious);
  p.summon(armageddon_knight);
  size_t chain_end_at = 0;
  bool crow_after_chain = false;
  p.hook = [&](const Event& e) {
    if (e.kind == MSG_CHAIN_END) chain_end_at = p.log.size();
    if (e.kind == MSG_SELECT_CHAIN && chain_end_at && e.player == 1 && Play::listed(e.cards, dd_crow)) crow_after_chain = true;
    return false;
  };
  bool malicious_idle = false;
  p.check([&](const Event& e) { malicious_idle = Play::listed(e.idle[5], malicious); });
  p.start();
  setup(p.activated(armageddon_knight), "Armageddon Knight did not activate");
  setup(p.zone_has(0, LOCATION_GRAVE, malicious), "Malicious was not milled");
  observed("Crow offered after the Chain and before the next idle " + yn(crow_after_chain) + "; idle offers Malicious " + yn(malicious_idle));
  if (!historical) return;
  expect_rule(crow_after_chain, "Priority page: after a Chain, the opponent gets priority before the turn player can activate an Ignition Effect");
}
inline void level_eater_window(ConfAssets& a, int rule, bool historical, bool trace) {
  Play p(a, rule, trace);
  p.hand(cyber_dragon, 0);
  p.monster(blue_eyes, 1, 0);
  p.grave(level_eater, 0);
  p.spsummon(cyber_dragon);
  p.chain(level_eater);
  p.pick(cyber_dragon);
  bool idle_offers = false;
  p.check([&](const Event& e) { idle_offers = Play::listed(e.idle[5], level_eater); });
  p.start();
  setup(p.moved(cyber_dragon, LOCATION_MZONE, LOCATION_HAND), "Cyber Dragon was not summoned");
  observed("Level Eater offered in Summon window " + yn(p.offered(level_eater, 0)) + ", usable from idle afterwards " + yn(idle_offers) + ";" + p.chain_text());
  setup(p.link_index(level_eater) == 0 || idle_offers, "Level Eater must be activatable at all in this fixture");
  if (historical) expect_rule(p.link_index(level_eater) == 0, "Summons page Ex.2: Level Eater's Graveyard Ignition Effect can be Chain Link 1");
}
inline void rai_oh_negates(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.hand(chaos_sorcerer, 0);
  p.grave(blue_eyes, 0);
  p.grave(sangan, 0);
  p.monster(rai_oh, 1);
  p.chain(rai_oh);
  p.spsummon(chaos_sorcerer);
  p.start();
  observed("Rai-Oh activated " + yn(p.activated(rai_oh)) + ", Chaos Sorcerer in GY " + yn(p.zone_has(0, LOCATION_GRAVE, chaos_sorcerer)));
  expect_rule(p.activated(rai_oh) && p.zone_has(0, LOCATION_GRAVE, chaos_sorcerer), "Summons page Ex.1: Rai-Oh negates the inherent Special Summon");
}
inline void oppression_dust_tornado(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.faceup_st(oppression, 1);
  p.monster(junk_synchron, 0, 0);
  p.monster(cyber_dragon, 0, 1);
  p.extra(stardust, 0);
  p.backrow(dust_tornado, 0);
  p.spsummon(stardust);
  p.pick(junk_synchron);
  p.chain(oppression);
  p.chain_when(dust_tornado, [&p] { return p.activated(oppression); });
  p.pick(oppression);
  p.start();
  observed("Stardust on field " + yn(p.zone_has(0, LOCATION_MZONE, stardust)) + ";" + p.chain_text());
  setup(p.activated(oppression), "Oppression was not activated");
  setup(p.activated(dust_tornado), "Dust Tornado did not chain");
  expect_rule(p.zone_has(0, LOCATION_MZONE, stardust), "Summons page Ex.3: destroying Oppression in the Chain means the Summon is not negated");
}

// Priority page: on entering a phase the turn player acts first.
inline void new_phase_priority(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.grave(malicious, 0);
  p.deck(malicious, 0);
  p.hand(dd_crow, 1);
  bool crow_in_main_before_idle = false, malicious_idle = false;
  p.hook = [&](const Event& e) {
    if (e.kind == MSG_SELECT_CHAIN && p.d.phase() == PHASE_MAIN1 && p.idle_prompts.empty() && Play::listed(e.cards, dd_crow)) crow_in_main_before_idle = true;
    return false;
  };
  p.check([&](const Event& e) { malicious_idle = Play::listed(e.idle[5], malicious); });
  p.start();
  setup(malicious_idle, "Malicious must be available at the first idle prompt");
  observed("Crow offered in Main Phase 1 before the turn player acted: " + yn(crow_in_main_before_idle));
  expect_rule(!crow_in_main_before_idle, "Priority page: the turn player may make the first action on entering a new phase");
}

// ---------------------------------------------------------------- Rule 7: SEGOC
inline void soul_exchange_segoc(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.monster(sangan, 1, 0);
  p.monster(blue_eyes, 1, 1);
  p.hand(soul_exchange, 0);
  p.hand(caius, 0);
  p.deck(dupe, 1);
  p.activate(soul_exchange).pick(sangan);
  p.yesno_answers = {1};
  p.summon(caius);
  p.pick(sangan).pick(blue_eyes);
  p.stop = [&](const Event& e) { return e.kind == MSG_CHAINED && e.chain == 2 && p.link_index(caius) >= 0 && p.link_index(sangan) >= 0; };
  p.start();
  setup(p.activated(caius) && p.activated(sangan), "Both triggers must activate");
  observed(p.chain_text());
  expect_rule(p.link_index(caius) < p.link_index(sangan), "Rule 7 Ex.2: turn player's mandatory Caius (Step 1) is CL1, non-turn player's mandatory Sangan (Step 2) is CL2");
}
inline void earlier_optional_later_mandatory(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.monster(bell, 1, 0, POS_FACEUP_DEFENSE);
  p.hand(dealings, 0);
  p.hand(broww, 0);
  p.add(blue_eyes, 0, LOCATION_DECK, SEQ_DECKTOP, POS_FACEDOWN_DEFENSE);
  p.add(watapon, 0, LOCATION_DECK, SEQ_DECKTOP, POS_FACEDOWN_DEFENSE);
  p.add(blue_eyes, 1, LOCATION_DECK, SEQ_DECKTOP, POS_FACEDOWN_DEFENSE);
  p.activate(dealings);
  p.pick(broww, 0).accept(watapon).chain(watapon);
  p.start();
  setup(p.activated(watapon) && p.activated(broww), "Both triggers must activate");
  observed(p.chain_text());
  expect_rule(p.link_index(broww) < p.link_index(watapon), "Rule 7: the chronology exception only applies within one step, so mandatory Broww (Step 1) precedes optional Watapon (Step 3)");
}

// Inside one SEGOC step: Dealings draws Watapon, then discards Parl, both optional for one player. The
// earlier Watapon is offered alone and goes first. Declining it is final and leaves the later Parl.
inline void same_step_chronology(ConfAssets& a, int rule, bool trace, bool public_hand, bool opponent, bool take_watapon, bool take_parl) {
  Play p(a, rule, trace);
  const int op = opponent ? 1 : 0;
  if (public_hand) p.monster(bell, 1, 0, POS_FACEUP_DEFENSE);
  p.hand(dealings, 0);
  p.hand(opponent ? broww : parl, 0);
  if (opponent) p.hand(parl, 1);
  p.grave(broww, op);  // Parl's target
  for (int i = 0; i < 3; ++i) { p.deck(blue_eyes, 0); p.deck(blue_eyes, 1); }
  p.add(watapon, op, LOCATION_DECK, SEQ_DECKTOP, POS_FACEDOWN_DEFENSE);
  if (take_watapon) p.accept(watapon).chain(watapon);
  if (take_parl) p.accept(parl).chain(parl);
  p.activate(dealings);
  if (opponent) p.pick(broww, 0, LOCATION_HAND);
  p.pick(parl, op, LOCATION_HAND);
  if (take_parl) p.pick(broww, op, LOCATION_GRAVE);
  std::vector<std::vector<uint32_t>> prompts;
  p.hook = [&](const Event& e) {
    if (e.kind != MSG_SELECT_CHAIN && e.kind != MSG_SELECT_EFFECTYN) return false;
    if (!Play::listed(e.cards, watapon) && !Play::listed(e.cards, parl)) return false;
    std::vector<uint32_t> codes;
    for (const auto& c : e.cards) codes.push_back(c.code);
    prompts.push_back(codes);
    return false;
  };
  p.start();
  std::vector<uint32_t> order;
  for (size_t i = 1; i < p.links.size(); ++i) order.push_back(p.links[i].code);
  setup(p.moved(parl, LOCATION_GRAVE, LOCATION_HAND) && p.zone_has(op, LOCATION_HAND, watapon) == !take_watapon, "Dealings must draw Watapon and discard Parl");
  observed("Trigger Chain:" + p.chain_text() + "; prompts offering Watapon or Parl: " + std::to_string(prompts.size()));
  std::vector<std::vector<uint32_t>> want_prompts{{watapon}, {parl}};
  std::vector<uint32_t> want;
  if (opponent) want.push_back(broww);
  if (take_watapon) want.push_back(watapon);
  if (take_parl) want.push_back(parl);
  expect_rule(prompts == want_prompts, "Rule 7: within one step the earlier Watapon is offered alone and once, then Parl");
  expect_rule(order == want, "Rule 7: within one step the earlier event goes on the Chain first; declining ends only that batch");
}
// The other half of the same model: triggers from one simultaneous batch stay the player's to order.
inline void same_batch_keeps_choice(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.hand(bazoo, 0);
  p.monster(sangan, 0, 0);
  p.monster(sangan, 0, 1);
  p.backrow(torrential, 0);
  for (int i = 0; i < 2; ++i) p.deck(dupe, 0);
  p.summon(bazoo);
  p.chain(torrential);
  int forced_choice = 0;
  p.hook = [&](const Event& e) {
    if (e.kind == MSG_SELECT_CHAIN && e.forced && e.cards.size() == 2 && e.cards[0].code == sangan && e.cards[1].code == sangan) ++forced_choice;
    return false;
  };
  p.stop = [&](const Event& e) { return e.kind == MSG_CHAINED && e.chain == 2 && p.links.back().code == sangan; };
  p.start();
  setup(p.activated(torrential), "Torrential was not activated");
  observed("Forced prompts listing both Sangans: " + std::to_string(forced_choice) + ";" + p.chain_text());
  expect_rule(forced_choice == 1, "Rule 7: triggers from one simultaneous batch are ordered by their controller");
}

// ---------------------------------------------------------------- Rule 8: Damage Step
inline void counter_at_damage_start(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.monster(blue_eyes, 0, 0);
  p.backrow(chalice, 0, 0);
  p.monster(bazoo, 1, 0);
  p.backrow(solemn, 1, 0);
  p.end();
  p.end();
  p.battle();
  auto in_damage_step = [&p] { return p.d.phase() == PHASE_DAMAGE; };
  p.chain_when(chalice, in_damage_step);
  p.chain_when(solemn, in_damage_step);
  p.pick(blue_eyes);
  p.attack(blue_eyes, bazoo);
  p.check([](const Event&) {});
  p.start();
  setup(p.activated(chalice), "Chalice was not activated in the Damage Step");
  observed(p.chain_text());
  expect_rule(p.activated(solemn), "Rule 8: Counter Traps may be activated at any point in the Damage Step");
}
inline void doomcaliber_in_damage_step(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.monster(blue_eyes, 0);
  p.monster(doomcaliber, 0, 1);
  p.monster(tomato, 1);
  p.deck(sangan, 1);
  p.accept(tomato);
  p.pick(sangan);
  p.end();  // P0 has no first-turn Battle Phase
  p.end();
  p.battle();
  p.attack(blue_eyes, tomato);
  p.check([](const Event&) {});
  p.start();
  setup(p.activated(tomato), "Tomato did not activate");
  observed("Doomcaliber activated " + yn(p.activated(doomcaliber)) + ";" + p.chain_text());
  expect_rule(p.activated(doomcaliber), "Rule 8: Doomcaliber's mandatory effect can be activated at any point in the Damage Step");
}
inline void veiler_in_damage_step(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.monster(blue_eyes, 0);
  p.monster(bazoo, 1);
  p.hand(effect_veiler, 1);
  p.hand(honest, 0);
  p.hand(sangan, 0);
  p.chain_when(honest, [&p] { return p.d.phase() == PHASE_DAMAGE; });
  p.end();
  p.end();
  p.summon(sangan);  // Effect Veiler only works in the opponent's Main Phase: this is its control window.
  p.battle();
  p.attack(blue_eyes, bazoo);
  p.check([](const Event&) {});
  p.start();
  setup(p.activated(honest), "Honest did not activate in the Damage Step");
  bool in_damage = false, elsewhere = false;
  for (const auto& o : p.offers) {
    if (o.code != effect_veiler) continue;
    if (o.phase == PHASE_DAMAGE || o.phase == PHASE_DAMAGE_CAL) in_damage = true; else elsewhere = true;
  }
  observed("Effect Veiler offered in Main Phase " + yn(elsewhere) + ", in the Damage Step " + yn(in_damage));
  setup(elsewhere, "Control: Effect Veiler must be available in the opponent's Main Phase");
  expect_rule(!in_damage, "Rule 8: monster effects that only negate effects cannot be activated in the Damage Step");
}
inline void marked_monster_cannot_be_returned(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.set_monster(penguin_soldier, 0, 0);
  p.monster(blue_eyes, 1, 0);
  p.accept(penguin_soldier);
  p.yesno_answers = {1};
  bool self_listed = false, prompted = false;
  p.hook = [&](const Event& e) {
    if (e.kind == MSG_SELECT_CARD && p.activated(penguin_soldier)) {
      prompted = true;
      if (Play::listed(e.cards, penguin_soldier)) self_listed = true;
    }
    return false;
  };
  p.end();
  p.battle();
  p.attack(blue_eyes, penguin_soldier);
  p.check([](const Event&) {});
  p.start();
  setup(p.activated(penguin_soldier), "Penguin Soldier did not activate");
  setup(prompted, "Penguin Soldier offered no return choice");
  observed("Penguin Soldier, marked destroyed by battle, offered as its own return target: " + yn(self_listed));
  expect_rule(!self_listed, "Battle Phase page: monsters marked destroyed by battle cannot be returned to the hand");
}
inline void gadget_after_rat(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.monster(blue_eyes, 1);
  p.monster(giant_rat, 0);
  p.deck(green_gadget, 0);
  p.deck(red_gadget, 0);
  p.accept(giant_rat).accept(green_gadget);
  p.pick(green_gadget).pick(red_gadget);
  p.end();
  p.battle();
  p.attack(blue_eyes, giant_rat);
  p.check([](const Event&) {});
  p.start();
  setup(p.activated(giant_rat), "Giant Rat did not activate");
  observed("Gadget offered " + yn(p.offered(green_gadget, 0)) + ";" + p.chain_text());
  expect_rule(p.offered(green_gadget, 0), "Rule 8: a Green Gadget Special Summoned by Giant Rat can activate in the Damage Step");
}
inline void ryko_mills_peten(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.monster(blue_eyes, 1);
  p.set_monster(ryko, 0);
  for (int i = 0; i < 3; ++i) p.add(peten, 0, LOCATION_DECK, SEQ_DECKTOP, POS_FACEDOWN_DEFENSE);
  p.add(peten, 0, LOCATION_DECK, SEQ_DECKTOP, POS_FACEDOWN_DEFENSE);
  p.accept(peten).accept(ryko);
  p.end();
  p.battle();
  p.attack(blue_eyes, ryko);
  p.check([](const Event&) {});
  p.start();
  setup(p.activated(ryko), "Ryko did not activate");
  observed("Peten offered " + yn(p.offered(peten, 0)) + ";" + p.chain_text());
  expect_rule(p.offered(peten, 0), "Rule 8 Substep 6: Ryko milling Peten as CL1 builds another manual Chain for Peten");
}
inline void gorz_damage_step(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.monster(blue_eyes, 1);
  p.hand(gorz, 0);
  p.accept(gorz).chain(gorz);
  p.end();
  p.battle();
  p.attack(blue_eyes);
  p.check([](const Event&) {});
  p.start();
  observed("Gorz activated " + yn(p.activated(gorz)) + ", P0 monsters " + std::to_string(p.d.count(0, LOCATION_MZONE)) + ";" + p.chain_text());
  expect_rule(p.activated(gorz) && p.zone_has(0, LOCATION_MZONE, gorz) && p.d.count(0, LOCATION_MZONE) == 2,
              "Rule 8 Substep 5: Gorz activates on Battle Damage and its Token effect follows in another manual Chain");
}
inline void mirror_force_replay(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.monster(blue_eyes, 1);
  p.monster(jerry_beans, 0, 0);
  p.monster(lonefire, 0, 1);
  p.backrow(compulsory, 0, 0);
  p.backrow(mirror_force, 0, 1);
  p.chain_when(compulsory, [&p] { return p.seen(MSG_ATTACK); });
  p.pick(jerry_beans);
  p.yesno_answers = {1};
  p.end();
  p.battle();
  p.attack(blue_eyes, jerry_beans);
  p.check([](const Event&) {});
  size_t solved_at = 0;
  bool mirror_after_replay = false;
  p.hook = [&](const Event& e) {
    if (e.kind == MSG_CHAIN_END && p.activated(compulsory) && !solved_at) solved_at = p.log.size();
    if (solved_at && e.kind == MSG_SELECT_CHAIN && Play::listed(e.cards, mirror_force)) mirror_after_replay = true;
    return false;
  };
  p.start();
  setup(p.activated(compulsory), "Compulsory did not chain to the attack");
  int attacks = 0;
  for (const auto& e : p.log) if (e.kind == MSG_ATTACK) ++attacks;
  observed("Attack messages " + std::to_string(attacks) + ", Mirror Force offered after the replay " + yn(mirror_after_replay));
  expect_rule(!mirror_after_replay, "Battle Phase page: after a replay Mirror Force cannot be played");
}

// ---------------------------------------------------------------- Rule 9: Trigger location
inline void dandylion_reincarnation(ConfAssets& a, int rule, bool historical, bool trace) {
  Play p(a, rule, trace);
  p.hand(monster_reincarnation, 0);
  p.hand(dandylion, 0);
  p.grave(jerry_beans, 0);
  p.accept(dandylion).chain(dandylion);
  p.activate(monster_reincarnation);
  p.pick(dandylion).pick(dandylion);
  p.start();
  setup(p.activated(monster_reincarnation), "Monster Reincarnation did not activate");
  setup(p.zone_has(0, LOCATION_HAND, dandylion), "Dandylion did not return to hand");
  observed("Dandylion activated from hand " + yn(p.activated(dandylion)) + ", tokens " + std::to_string(p.d.count(0, LOCATION_MZONE)));
  if (historical) expect_rule(p.activated(dandylion) && p.d.count(0, LOCATION_MZONE) == 2,
      "Rule 9: Dandylion's trigger activates from outside its trigger location, even after returning to hand");
}
inline void necroface_future_visions(ConfAssets& a, int rule, bool historical, bool trace, bool visions_first) {
  Play p(a, rule, trace);
  p.field(future_visions, 0);
  p.hand(necroface, 0);
  for (int i = 0; i < 6; ++i) { p.deck(blue_eyes, 0); p.deck(blue_eyes, 1); }
  p.add(jerry_beans, 0, LOCATION_REMOVED, 0, POS_FACEUP_ATTACK);
  p.chain(visions_first ? future_visions : necroface);
  p.accept(future_visions).accept(necroface);
  p.summon(necroface);
  p.start();
  setup(p.activated(necroface), "Necroface did not activate");
  int necroface_activations = 0;
  for (const auto& l : p.links) if (l.code == necroface) ++necroface_activations;
  observed("Necroface activations " + std::to_string(necroface_activations) + ", P0 banished " + std::to_string(p.d.count(0, LOCATION_REMOVED)) + ";" + p.chain_text());
  if (historical) expect_rule(necroface_activations == 2,
      "Rule 9: Necroface's banish effect starts a new Chain after Future Visions removed it, even though it was returned to the Deck");
}
inline void aslla_returned(ConfAssets& a, int rule, bool trace) {
  {
    Play c(a, rule, trace);
    c.monster(aslla, 1);
    c.field(yami, 1);
    c.hand(mst, 0);
    c.hand(dark_hole, 0);
    c.activate(dark_hole);
    c.start();
    observed("Control, destroyed: Aslla activated " + yn(c.activated(aslla)));
    setup(c.activated(aslla), "Control: Aslla must activate when destroyed");
  }
  Play p(a, rule, trace);
  p.monster(aslla, 1);
  p.field(yami, 1);
  p.monster(jerry_beans, 1, 1);
  p.backrow(phoenix_wing, 0);
  p.hand(jerry_beans, 0);
  p.activate(phoenix_wing);
  p.pick(jerry_beans, 0).pick(aslla);
  p.start();
  setup(p.activated(phoenix_wing), "Phoenix Wing Wind Blast did not activate");
  setup(p.zone_has(1, LOCATION_DECK, aslla), "Aslla Piscu was not returned to the Deck");
  observed("Aslla activated " + yn(p.activated(aslla)));
  expect_rule(!p.activated(aslla), "Rule 9: Aslla Piscu returned to the Deck does not activate");
}
inline void zero_returned(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.monster(absolute_zero, 1);
  p.monster(blue_eyes, 0, 0);
  p.backrow(phoenix_wing, 0);
  p.hand(jerry_beans, 0);
  p.activate(phoenix_wing);
  p.pick(jerry_beans, 0).pick(absolute_zero);
  p.start();
  setup(p.activated(phoenix_wing), "Phoenix Wing Wind Blast did not activate");
  setup(p.zone_has(1, LOCATION_EXTRA, absolute_zero), "Absolute Zero was not returned to the Extra Deck");
  observed("Absolute Zero activated " + yn(p.activated(absolute_zero)) + ", Blue-Eyes destroyed " + yn(p.zone_has(0, LOCATION_GRAVE, blue_eyes)));
  expect_rule(p.activated(absolute_zero) && p.zone_has(0, LOCATION_GRAVE, blue_eyes), "Rule 9: Absolute Zero returned to the Extra Deck does activate");
}

// ---------------------------------------------------------------- Rule 10: LP costs
inline void exact_lp_activation(ConfAssets& a, int rule, bool trace, uint32_t card_code, int cost, bool setcard, bool historical) {
  // A control one LP above the cost proves the card was otherwise activatable here.
  for (int extra : {1, 0}) {
    Play p(a, rule, trace, 0, 0, {cost + extra, 8000});
    if (card_code == brain) p.monster(bazoo, 1);
    if (card_code == premature_burial) p.grave(bazoo, 0);
    if (setcard) p.backrow(card_code, 0); else p.hand(card_code, 0);
    bool offered = false;
    p.check([&](const Event& e) { offered = Play::listed(e.idle[5], card_code); });
    p.start();
    observed("Activation offered at " + std::to_string(cost + extra) + " LP: " + yn(offered));
    if (extra) setup(offered, "Card must be activatable with LP above its cost");
    else if (historical) expect_rule(!offered, "Rule 10: you cannot pay a cost that would reduce your LP to 0");
  }
}
inline void maintenance_exact_lp(ConfAssets& a, int rule, bool trace, uint32_t card_code, int cost, bool historical) {
  Play p(a, rule, trace, 0, 0, {cost, 8000});
  p.faceup_st(card_code, 0);
  p.yesno_answers = {1};
  p.end();
  p.stop_at_turn = 3;
  p.end();
  p.start();
  const bool destroyed = p.zone_has(0, LOCATION_GRAVE, card_code);
  observed("Winner " + std::to_string(p.winner) + ", LP " + std::to_string(p.d.lp(0)) + ", self-destructed " + yn(destroyed) + ", LP costs " + std::to_string(p.costs.size()));
  if (historical) {
    expect_rule(p.winner == -1 && p.d.lp(0) == cost, "Rule 10: maintenance cannot reduce LP to 0");
    expect_rule(destroyed, "Rule 10: when maintenance cannot be paid the card self-destructs");
  }
}

// ---------------------------------------------------------------- Rule 11: End-of-turn discard
inline void end_discard_mandatory(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  for (int i = 0; i < 6; ++i) p.hand(jerry_beans, 0);
  p.hand(white_stone, 0);
  p.hand(super_rejuvenation, 0);
  p.deck(blue_eyes, 0);
  p.backrow(wrath, 1);
  p.hand(jerry_beans, 1);
  p.pick(white_stone, 0);
  p.end();
  p.stop_at_turn = 2;
  p.start();
  setup(p.moved(white_stone, LOCATION_GRAVE, LOCATION_HAND), "White Stone was not discarded");
  setup(p.activated(white_stone), "White Stone did not activate");
  // Super Rejuvenation is legal in the End Phase before the discard, so only an offer inside the
  // trigger's Chain counts. P1 declines Wrath, because nothing below a Counter Trap could chain to it.
  bool wrath_in_chain = false, rejuvenation_in_chain = false;
  for (const auto& o : p.offers) {
    if (o.depth > 0 && o.code == wrath) wrath_in_chain = true;
    if (o.depth > 0 && o.code == super_rejuvenation) rejuvenation_in_chain = true;
  }
  observed("In the trigger's Chain: Super Rejuvenation offered " + yn(rejuvenation_in_chain) + ", Divine Wrath offered " + yn(wrath_in_chain) + ";" + p.chain_text());
  expect_rule(wrath_in_chain, "Rule 11 Ex.1: Divine Wrath can negate the mandatory trigger");
  expect_rule(!rejuvenation_in_chain, "Rule 11 Ex.1: Super Rejuvenation cannot chain to the end-of-turn discard trigger");
}
inline void end_discard_optional(ConfAssets& a, int rule, bool historical, bool trace) {
  Play p(a, rule, trace);
  for (int i = 0; i < 7; ++i) p.hand(jerry_beans, 0);
  p.hand(peten, 0);
  p.deck(peten, 0);
  p.accept(peten);
  p.pick(peten, 0);
  p.end();
  p.stop_at_turn = 2;
  p.start();
  setup(p.moved(peten, LOCATION_GRAVE, LOCATION_HAND), "Peten was not discarded");
  observed("Peten offered " + yn(p.offered(peten, 0)));
  if (historical) expect_rule(!p.offered(peten, 0), "Rule 11 Ex.2: the discarded monster's optional trigger cannot activate");
}
inline void end_discard_doomcaliber(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  for (int i = 0; i < 6; ++i) p.hand(jerry_beans, 0);
  p.hand(white_stone, 0);
  p.hand(jerry_beans, 0);
  p.deck(blue_eyes, 0);
  p.monster(doomcaliber, 1);
  p.pick(white_stone, 0);
  p.end();
  p.stop_at_turn = 2;
  p.start();
  setup(p.activated(white_stone), "White Stone did not activate");
  observed(p.chain_text());
  expect_rule(p.activated(doomcaliber), "Rule 11 Ex.1: a face-up Doomcaliber tributes itself and adds a Chain Link");
}

inline void end_discard_no_response(ConfAssets& a, int rule, bool historical, bool trace) {
  Play p(a, rule, trace);
  for (int i = 0; i < 7; ++i) p.hand(jerry_beans, 0);
  p.backrow(mirror_force, 0, 0);
  p.backrow(mst, 1, 0);
  p.pick(jerry_beans, 0);
  p.end();
  size_t discard_at = 0;
  bool response_after_discard = false;
  p.hook = [&](const Event& e) {
    if (e.kind == MSG_MOVE && e.from.location == LOCATION_HAND && e.to.location == LOCATION_GRAVE) discard_at = p.log.size();
    if (discard_at && e.kind == MSG_SELECT_CHAIN && p.turn == 1 && Play::listed(e.cards, mst)) response_after_discard = true;
    return false;
  };
  p.stop_at_turn = 2;
  p.start();
  setup(discard_at != 0, "No hand-size discard occurred");
  setup(p.offered(mst, 1), "MST must be available earlier in this fixture");
  observed("MST offered after the discard, same turn: " + yn(response_after_discard));
  if (historical) expect_rule(!response_after_discard, "Rule 11: players cannot respond to the end-of-turn discard");
}

// ---------------------------------------------------------------- Rule 12: Infinite loops
inline void pole_position_voluntary(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.monster(gemini_elf, 1, 0);
  p.field(luminous_spark, 1);
  p.faceup_st(pole_position, 1);
  p.hand(x_head, 0);
  bool offered = false;
  p.check([&](const Event& e) { offered = Play::listed(e.idle[0], x_head); });
  p.start();
  observed("X-Head Cannon Normal Summon offered " + yn(offered));
  expect_rule(!offered, "Rule 12 Ex.1: a voluntary Summon that would create an infinite loop is illegal");
}
// Rule 12 Example 4, by the listed route: the opponent draws at the start of the turn.
inline void pole_position_involuntary(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace, 0, 1);
  p.faceup_st(pole_position, 0, 0);
  p.monster(opticlops, 0, 0);
  for (int i = 0; i < 4; ++i) p.deck(jerry_beans, 0);
  p.monster(muka_muka, 1, 0);
  p.hand(axe_despair, 1);
  for (int i = 0; i < 4; ++i) p.deck(jerry_beans, 1);
  p.end();                       // turn 1, P0
  p.activate(axe_despair).pick(muka_muka);
  p.set(jerry_beans);            // empties the hand: Muka Muka 600 + 1000 = 1600
  p.end();                       // turn 2, P1
  p.end();                       // turn 3, P0
  p.check([](const Event&) {});  // turn 4, P1 has drawn
  p.start();
  setup(p.turn >= 4, "Did not reach turn 4");
  const bool destroyed = p.zone_has(0, LOCATION_GRAVE, pole_position);
  observed("Pole Position destroyed by game mechanics " + yn(destroyed) + ", on field " + yn(p.zone_has(0, LOCATION_SZONE, pole_position)));
  expect_rule(destroyed, "Rule 12 Ex.4: an involuntary infinite loop destroys Pole Position by game mechanics");
}
// Rule 12 Example 1 taken anyway: does the engine survive the illegal state?
inline void pole_position_forced_loop(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.monster(gemini_elf, 1, 0);
  p.field(luminous_spark, 1);
  p.faceup_st(pole_position, 1);
  p.hand(x_head, 0);
  p.summon(x_head);
  p.start();
  observed("Engine reached a checkpoint. X-Head on field " + yn(p.zone_has(0, LOCATION_MZONE, x_head)) + ", in GY " + yn(p.zone_has(0, LOCATION_GRAVE, x_head)) +
           ", Pole Position on field " + yn(p.zone_has(1, LOCATION_SZONE, pole_position)) + ", in GY " + yn(p.zone_has(1, LOCATION_GRAVE, pole_position)));
}

// ---------------------------------------------------------------- Rule 13: 0 ATK
inline void zero_battle(ConfAssets& a, int rule, bool trace, bool defender_attack, bool historical) {
  Play p(a, rule, trace);
  p.monster(battle_fader, 1, 0);
  p.add(battle_fader, 0, LOCATION_MZONE, 0, defender_attack ? POS_FACEUP_ATTACK : POS_FACEUP_DEFENSE);
  p.end();
  p.battle();
  p.attack(battle_fader, battle_fader);
  p.check([](const Event&) {});
  p.start();
  setup(p.battles.size() == 1, "Battle did not occur");
  const bool attacker_dead = p.zone_has(1, LOCATION_GRAVE, battle_fader), defender_dead = p.zone_has(0, LOCATION_GRAVE, battle_fader);
  observed("Attacker destroyed " + yn(attacker_dead) + ", defender destroyed " + yn(defender_dead));
  if (!historical) return;
  if (defender_attack) expect_rule(attacker_dead && defender_dead, "Rule 13: two 0 ATK Attack Position monsters destroy each other");
  else expect_rule(!attacker_dead && !defender_dead, "Rule 13: a 0 ATK attacker does not destroy a 0 DEF defender");
}

inline void register_rule_tests(Registry& r, ConfAssets& a, bool t) {
  auto both = [&r](const std::string& n, const std::string& title, const std::string& exp, std::function<void(int, bool)> body) { r.both(n, title, exp, std::move(body)); };
  both("201", "FIELD SPELL DESTROYED IN CHAIN", "Forest is destroyed in the Chain, so Yami stays.", [&a, t](int o, bool h) { field_chain_destroyed(a, o, h, t); });
  both("202", "SET FIELD SPELL UNDER SECRET VILLAGE", "Village stays; Set Umi cannot be activated.", [&a, t](int o, bool h) { field_set_village(a, o, h, t); });
  both("203A", "SET MAUSOLEUM OVER FACE-UP GEARTOWN", "Geartown destroyed with no Chain; its trigger is available.", [&a, t](int o, bool h) { field_over_geartown(a, o, h, t, true, false); });
  both("203B", "SET MAUSOLEUM OVER FACE-DOWN GEARTOWN", "Same as 203A when Geartown is face-down.", [&a, t](int o, bool h) { field_over_geartown(a, o, h, t, true, true); });
  both("204A", "ACTIVATE MAUSOLEUM OVER FACE-UP GEARTOWN", "A Chain starts, so Geartown's trigger cannot activate.", [&a, t](int o, bool h) { field_over_geartown(a, o, h, t, false, false); });
  both("204B", "ACTIVATE MAUSOLEUM OVER FACE-DOWN GEARTOWN", "Same as 204A when Geartown is face-down.", [&a, t](int o, bool h) { field_over_geartown(a, o, h, t, false, true); });
  both("205", "LISTED UNION REPLACES EFFECT DESTRUCTION", "Gearframe is destroyed instead of its equipped Cyber Dragon.", [&a, t](int o, bool h) { union_substitute(a, o, t, cyber_dragon, gearframe, true, h); });
  both("206", "UNLISTED UNION DOES NOT REPLACE EFFECT DESTRUCTION", "Y-Dragon Head is not listed, so X-Head Cannon is destroyed.", [&a, t](int o, bool h) { union_substitute(a, o, t, x_head, y_dragon, false, h); });

  both("211", "TRAP MONSTER NEEDS A MONSTER ZONE", "Apophis cannot be activated with no empty Monster Zone.", [&a, t](int o, bool) { field_full_blocks_trap_monster(a, o, t); });
  both("212", "TRAP MONSTER BLOCKS ITS S/T ZONE", "The S/T Zone Apophis left stays unusable.", [&a, t](int o, bool h) { trap_monster_blocks_zone(a, o, h, t); });
  both("213", "CONTROL OF TRAP MONSTER NEEDS BOTH ZONES", "Enemy Controller cannot take Apophis with no open S/T Zone.", [&a, t](int o, bool) { control_needs_both_zones(a, o, t); });
  both("214", "JINZO REVERTS TRAP MONSTER", "Apophis returns to the S/T Zone immediately.", [&a, t](int o, bool) { jinzo_reverts(a, o, t); });
  both("215", "TRAP MONSTER ATTACKS SET JINZO", "Apophis reverts on the flip; no Damage Calculation.", [&a, t](int o, bool) { attack_set_jinzo(a, o, t); });
  both("216", "STOLEN TRAP MONSTER MEETS JINZO", "Apophis returns to its owner's S/T Zone.", [&a, t](int o, bool) { stolen_returns_to_owner(a, o, t); });
  both("217", "BOOK OF MOON ON TRAP MONSTER", "Apophis becomes Set in its controller's S/T Zone.", [&a, t](int o, bool) { book_on_trap_monster(a, o, t, false); });
  both("218", "BOOK OF MOON ON STOLEN TRAP MONSTER", "Apophis goes to the controller's S/T Zone.", [&a, t](int o, bool) { book_on_trap_monster(a, o, t, true); });
  both("219A", "HEAVY STORM ON TRAP MONSTER, FAKE TRAP", "Fake Trap is available.", [&a, t](int o, bool) { trap_monster_protection(a, o, t, heavy_storm, fake_trap, true); });
  both("219B", "HEAVY STORM ON TRAP MONSTER, MY BODY", "My Body is not available.", [&a, t](int o, bool) { trap_monster_protection(a, o, t, heavy_storm, my_body, false); });
  both("219C", "LIGHTNING VORTEX ON TRAP MONSTER, FAKE TRAP", "Fake Trap is not available.", [&a, t](int o, bool) { trap_monster_protection(a, o, t, lightning_vortex, fake_trap, false); });
  both("219D", "LIGHTNING VORTEX ON TRAP MONSTER, MY BODY", "My Body is available.", [&a, t](int o, bool) { trap_monster_protection(a, o, t, lightning_vortex, my_body, true); });
  both("219E", "RAIGEKI BREAK ON TRAP MONSTER, FAKE TRAP", "Fake Trap is available.", [&a, t](int o, bool) { trap_monster_protection(a, o, t, raigeki_break, fake_trap, true); });
  both("219F", "RAIGEKI BREAK ON TRAP MONSTER, MY BODY", "My Body is available.", [&a, t](int o, bool) { trap_monster_protection(a, o, t, raigeki_break, my_body, true); });
  both("220", "CAIUS REMOVES ZOMA", "Removed as a Trap Card, so no damage.", [&a, t](int o, bool) { caius_zoma(a, o, t); });
  both("221", "MICHIZURE AND TRAP MONSTER", "Michizure cannot be activated.", [&a, t](int o, bool) { michizure_trap_monster(a, o, t); });
  both("222", "SHURA AND TRAP MONSTER", "Shura does not trigger.", [&a, t](int o, bool) { shura_trap_monster(a, o, t); });
  both("223", "PENGUIN SOLDIER AND MAJOR RIOT", "Major Riot cannot be activated.", [&a, t](int o, bool) { penguin_major_riot(a, o, t); });
  both("224", "MATTER TRANSPORTER ON TRAP MONSTER", "Returns as a Trap Card and is destroyed.", [&a, t](int o, bool) { matter_transporter(a, o, t); });

  both("225", "TIME MACHINE AND TRAP MONSTER", "Time Machine cannot be activated.", [&a, t](int o, bool) { time_machine_trap_monster(a, o, t); });
  both("226", "RETURN OF THE DOOMED AND TRAP MONSTER", "No legal target.", [&a, t](int o, bool) { doomed_trap_monster(a, o, t); });
  both("227", "DIMENSIONHOLE ON TRAP MONSTER", "Returns as a Trap Card and is destroyed.", [&a, t](int o, bool) { dimensionhole_trap_monster(a, o, t); });

  both("231", "MALICIOUS GY IGNITION PRIORITY", "Malicious can be CL1 before D.D. Crow.", [&a, t](int o, bool h) { malicious_priority(a, o, h, t); });
  both("232", "CHAOS SORCERER IGNITION PRIORITY", "Chaos Sorcerer can be CL1 before Bottomless.", [&a, t](int o, bool h) { chaos_priority(a, o, h, t); });
  both("233", "DEMISE PRIORITY AFTER RITUAL", "Demise's effect is available before Torrential.", [&a, t](int o, bool h) { demise_priority(a, o, h, t); });
  both("234", "BLACK GARDEN REMOVES PRIORITY", "Garden is CL1; DAD's Ignition is not offered.", [&a, t](int o, bool) { garden_strips_priority(a, o, t); });
  both("235A", "DECLINED TRIGGER KEEPS IGNITION PRIORITY", "Bazoo's Ignition is offered after the trigger is declined.", [&a, t](int o, bool) { knight_trigger_priority(a, o, t, false); });
  both("235B", "ACCEPTED TRIGGER REMOVES PRIORITY", "Bazoo's Ignition is not offered as the trigger Chain builds.", [&a, t](int o, bool) { knight_trigger_priority(a, o, t, true); });
  both("236", "SANGAN MATERIAL REMOVES PRIORITY", "Sangan starts a Chain; Brionac is not offered.", [&a, t](int o, bool) { sangan_material_strips_priority(a, o, t); });
  both("237", "PRIORITY AFTER A CHAIN RESOLVES", "D.D. Crow is offered before any Ignition Effect.", [&a, t](int o, bool h) { after_chain_priority(a, o, h, t); });
  both("238", "LEVEL EATER SUMMON WINDOW", "Level Eater's GY Ignition can be CL1.", [&a, t](int o, bool h) { level_eater_window(a, o, h, t); });
  both("239", "RAI-OH NEGATES INHERENT SUMMON", "Chaos Sorcerer's Summon is negated.", [&a, t](int o, bool) { rai_oh_negates(a, o, t); });
  both("240", "OPPRESSION DESTROYED IN CHAIN", "The Synchro Summon is not negated.", [&a, t](int o, bool) { oppression_dust_tornado(a, o, t); });

  both("244", "PRIORITY ON ENTERING MAIN PHASE", "D.D. Crow is not offered before the turn player acts.", [&a, t](int o, bool) { new_phase_priority(a, o, t); });
  both("241", "SOUL EXCHANGE SANGAN AND CAIUS", "Caius CL1, opponent's Sangan CL2.", [&a, t](int o, bool) { soul_exchange_segoc(a, o, t); });
  both("242", "EARLIER OPTIONAL VERSUS LATER MANDATORY", "Mandatory Broww CL1, optional Watapon CL2.", [&a, t](int o, bool) { earlier_optional_later_mandatory(a, o, t); });
  both("243A", "SAME STEP CHRONOLOGY, ACCEPT BOTH", "Earlier Watapon offered alone, CL1; later Parl CL2.", [&a, t](int o, bool) { same_step_chronology(a, o, t, true, false, true, true); });
  both("243B", "SAME STEP, DECLINE THE EARLIER", "Watapon is not offered again; later Parl is still offered, CL1.", [&a, t](int o, bool) { same_step_chronology(a, o, t, true, false, false, true); });
  both("243C", "SAME STEP, DECLINE THE LATER", "Watapon CL1; Parl declined.", [&a, t](int o, bool) { same_step_chronology(a, o, t, true, false, true, false); });
  both("243D", "SAME STEP FOR THE NON-TURN PLAYER", "Turn player's mandatory Broww CL1 (Step 1); P1's earlier Watapon CL2 and later Parl CL3 (Step 4).", [&a, t](int o, bool) { same_step_chronology(a, o, t, true, true, true, true); });
  both("243E", "SAME STEP WITH A PRIVATE HAND TRIGGER", "Private Watapon offered alone, CL1; later Parl CL2.", [&a, t](int o, bool) { same_step_chronology(a, o, t, false, false, true, true); });
  both("243F", "PRIVATE HAND TRIGGER DECLINED", "Private Watapon is not offered again; later Parl is still offered, CL1.", [&a, t](int o, bool) { same_step_chronology(a, o, t, false, false, false, true); });
  both("243G", "SAME BATCH KEEPS THE CHOICE", "Two Sangans destroyed together are both offered in one forced prompt.", [&a, t](int o, bool) { same_batch_keeps_choice(a, o, t); });

  both("251", "COUNTER TRAP AT START OF DAMAGE STEP", "Divine Wrath is available when Chalice activates.", [&a, t](int o, bool) { counter_at_damage_start(a, o, t); });
  both("252", "DOOMCALIBER IN DAMAGE STEP", "Doomcaliber activates on Tomato's effect.", [&a, t](int o, bool) { doomcaliber_in_damage_step(a, o, t); });
  both("253", "GADGET FROM GIANT RAT", "Green Gadget's trigger is available.", [&a, t](int o, bool) { gadget_after_rat(a, o, t); });
  both("254", "RYKO MILLS PETEN", "Peten activates in another manual Chain.", [&a, t](int o, bool) { ryko_mills_peten(a, o, t); });
  both("255", "GORZ ON BATTLE DAMAGE", "Gorz activates.", [&a, t](int o, bool) { gorz_damage_step(a, o, t); });
  both("256", "ATTACK REPLAY DOES NOT REOPEN DECLARATION", "No second declaration Chain.", [&a, t](int o, bool) { mirror_force_replay(a, o, t); });

  both("257", "EFFECT VEILER IN THE DAMAGE STEP", "Veiler is not offered.", [&a, t](int o, bool) { veiler_in_damage_step(a, o, t); });
  both("258", "MONSTER MARKED DESTROYED CANNOT BE RETURNED", "Penguin cannot return itself.", [&a, t](int o, bool) { marked_monster_cannot_be_returned(a, o, t); });

  both("261", "DANDYLION RETURNED BY REINCARNATION", "Dandylion activates from the hand.", [&a, t](int o, bool h) { dandylion_reincarnation(a, o, h, t); });
  both("262A", "NECROFACE WITH FUTURE VISIONS CL1", "Necroface activates a second time in a new Chain.", [&a, t](int o, bool h) { necroface_future_visions(a, o, h, t, true); });
  both("262B", "NECROFACE CL1 WITH FUTURE VISIONS", "Necroface activates a second time from the Deck.", [&a, t](int o, bool h) { necroface_future_visions(a, o, h, t, false); });
  both("263", "ASLLA PISCU RETURNED TO DECK", "Aslla does not activate.", [&a, t](int o, bool) { aslla_returned(a, o, t); });
  both("264", "ABSOLUTE ZERO RETURNED TO EXTRA", "Absolute Zero activates.", [&a, t](int o, bool) { zero_returned(a, o, t); });

  both("271A", "BRAIN CONTROL AT EXACTLY 800 LP", "Not offered.", [&a, t](int o, bool h) { exact_lp_activation(a, o, t, brain, 800, false, h); });
  both("271B", "SKILL DRAIN AT EXACTLY 1000 LP", "Not offered.", [&a, t](int o, bool h) { exact_lp_activation(a, o, t, skill_drain, 1000, true, h); });
  both("271C", "PREMATURE BURIAL AT EXACTLY 800 LP", "Not offered.", [&a, t](int o, bool h) { exact_lp_activation(a, o, t, premature_burial, 800, false, h); });
  both("272", "MIRROR WALL MAINTENANCE AT EXACTLY 2000 LP", "Cannot pay; self-destructs.", [&a, t](int o, bool h) { maintenance_exact_lp(a, o, t, mirror_wall, 2000, h); });
  both("273", "DEGENERATE CIRCUIT AT EXACTLY 500 LP", "Cannot pay; self-destructs.", [&a, t](int o, bool h) { maintenance_exact_lp(a, o, t, degenerate_circuit, 500, h); });
  both("274", "MESSENGER OF PEACE AT EXACTLY 100 LP", "Cannot pay; self-destructs.", [&a, t](int o, bool h) { maintenance_exact_lp(a, o, t, messenger, 100, h); });

  both("281", "END DISCARD MANDATORY TRIGGER", "Wrath offered, Super Rejuvenation not.", [&a, t](int o, bool) { end_discard_mandatory(a, o, t); });
  both("282", "END DISCARD OPTIONAL TRIGGER", "Peten cannot activate.", [&a, t](int o, bool h) { end_discard_optional(a, o, h, t); });
  both("283", "END DISCARD DOOMCALIBER", "Doomcaliber adds a Chain Link.", [&a, t](int o, bool) { end_discard_doomcaliber(a, o, t); });

  both("284", "END DISCARD ALLOWS NO RESPONSE", "No response window follows the discard.", [&a, t](int o, bool h) { end_discard_no_response(a, o, h, t); });

  both("291", "VOLUNTARY LOOP SUMMON", "X-Head Cannon cannot be Summoned.", [&a, t](int o, bool) { pole_position_voluntary(a, o, t); });
  both("292", "INVOLUNTARY LOOP DESTROYS POLE POSITION", "Muka Muka's draw loops; Pole Position is destroyed by game mechanics.", [&a, t](int o, bool) { pole_position_involuntary(a, o, t); });
  both("293", "ILLEGAL LOOP SUMMON TAKEN ANYWAY", "Engine must not hang; records what the state does.", [&a, t](int o, bool) { pole_position_forced_loop(a, o, t); });

  both("296", "TWO 0 ATK ATTACK POSITION MONSTERS", "Both destroyed.", [&a, t](int o, bool h) { zero_battle(a, o, t, true, h); });
  both("297", "0 ATK ATTACKS 0 DEF", "Neither destroyed.", [&a, t](int o, bool h) { zero_battle(a, o, t, false, h); });
}
}  // namespace conformance
