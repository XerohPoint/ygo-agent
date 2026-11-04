#pragma once
// 3xx: gaps left by the existing fixtures in https://www.edisonformat.com/rulings/relevant-errata
// 4xx: per-card rulings from https://www.edisonformat.com/rulings subpages, for cards chosen by
//      how many Format Library Edison decks run them.
#include "rule_tests.h"

namespace conformance {
inline const auto noop = [](const Event&) {};
inline std::string atk_text(Play& p, int player, int seq) { return std::to_string(p.d.query_value(player, LOCATION_MZONE, seq, QUERY_ATTACK)); }

// ---------------------------------------------------------------- Errata gaps
inline void ladd_sequential(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.monster(light_dark_dragon, 0, 0);
  p.monster(jerry_beans, 0, 1);
  p.backrow(mirror_force, 0, 0);
  p.grave(sangan, 0);
  p.monster(blue_eyes, 1);
  p.pick(sangan);
  p.end();
  p.battle();
  p.attack(blue_eyes, light_dark_dragon);
  p.check(noop);
  p.start();
  setup(p.activated(light_dark_dragon), "LADD's trigger did not activate");
  int jerry_at = -1, mirror_at = -1, revive_at = -1;
  for (size_t i = 0; i < p.moves.size(); ++i) {
    const auto& m = p.moves[i];
    if (m.from.code == jerry_beans && m.to.location == LOCATION_GRAVE) jerry_at = int(i);
    if (m.from.code == mirror_force && m.to.location == LOCATION_GRAVE) mirror_at = int(i);
    if (m.from.code == sangan && m.to.location == LOCATION_MZONE) revive_at = int(i);
  }
  observed("Move order: other monster " + std::to_string(jerry_at) + ", S/T " + std::to_string(mirror_at) + ", revival " + std::to_string(revive_at));
  expect_rule(jerry_at >= 0 && mirror_at >= 0, "Errata: LADD destroys ALL cards you control");
  expect_rule(revive_at > jerry_at && revive_at > mirror_at, "Errata: LADD resolves sequentially; revival comes after destruction");
}
inline void phase_retry(ConfAssets& a, int rule, bool historical, bool trace, uint32_t actor, bool spirit, int mills) {
  Play p(a, rule, trace);
  if (spirit) p.hand(actor, 0); else p.monster(actor, 0);
  p.monster(light_dark_dragon, 1);
  for (int i = 0; i < mills; ++i) p.deck(blue_eyes, 0);
  if (spirit) p.summon(actor);
  p.end();
  p.stop_at_turn = 2;
  p.start();
  int activations = 0;
  for (const auto& l : p.links) if (l.code == actor) ++activations;
  int milled = 0;
  for (const auto& m : p.moves) if (m.from.location == LOCATION_DECK && m.to.location == LOCATION_GRAVE) ++milled;
  observed("Activations " + std::to_string(activations) + ", milled " + std::to_string(milled) + ", returned " + yn(p.zone_has(0, LOCATION_HAND, actor)));
  if (!historical) return;
  expect_rule(activations >= 2, "Errata: a phase-dependent mandatory trigger activates again after activation negation");
  if (spirit) expect_rule(p.zone_has(0, LOCATION_HAND, actor), "Errata: the Spirit keeps activating until it returns");
  else expect_rule(milled == mills, "Errata: the Lightsworn keeps activating until it mills");
}
inline void necrovalley_no_target(ConfAssets& a, int rule, bool trace, uint32_t spell, uint32_t revived, bool redmd) {
  Play p(a, rule, trace);
  p.field(necrovalley, 0);
  p.grave(revived, 0);
  if (redmd) p.monster(spell, 0, 1); else p.hand(spell, 0);
  p.activate(spell);
  p.pick(revived);
  p.start();
  setup(p.activated(spell), "Effect was not activated");
  observed("Revived monster on field " + yn(p.zone_has(0, LOCATION_MZONE, revived)) + "; negated " + std::to_string(p.disabled.size()));
  expect_rule(p.zone_has(0, LOCATION_MZONE, revived), "Errata: Necrovalley only negates effects that target");
}
inline void swap_same_copy(ConfAssets& a, int rule, bool historical, bool trace) {
  Play p(a, rule, trace);
  p.monster(swap_frog, 0, 0);
  p.monster(dupe, 0, 1);
  p.monster(jerry_beans, 0, 2);
  p.activate(swap_frog);
  p.pick(dupe);
  bool offered_again = true;
  p.check([&](const Event& e) { offered_again = Play::listed(e.idle[5], swap_frog); });
  p.start();
  setup(p.activated(swap_frog), "Swap Frog did not activate");
  setup(p.zone_has(0, LOCATION_HAND, dupe), "Dupe Frog was not returned");
  observed("Same Swap Frog offered again with another monster available: " + yn(offered_again));
  if (historical) expect_rule(!offered_again, "Errata: each Swap Frog can use its effect once per turn");
}
inline void herald_before_calculation(ConfAssets& a, int rule, bool trace, bool catastor_case) {
  Play p(a, rule, trace);
  auto in_damage = [&p] { return p.d.phase() == PHASE_DAMAGE; };
  p.hand(herald, 1);
  p.hand(watapon, 1);
  if (catastor_case) {
    p.monster(catastor, 0);
    p.monster(jerry_beans, 1);
    p.end();
    p.end();
    p.battle();
    p.attack(catastor, jerry_beans);
    p.chain_when(herald, [&p] { return p.activated(catastor); });
  } else {
    p.monster(blue_eyes, 0);
    p.hand(honest, 0);
    p.monster(bazoo, 1);
    p.end();
    p.end();
    p.battle();
    p.attack(blue_eyes, bazoo);
    p.chain_when(honest, in_damage);
    p.chain_when(herald, [&p] { return p.activated(honest); });
  }
  p.check(noop);
  p.pick(herald, 1).pick(watapon, 1);
  p.start();
  const uint32_t actor = catastor_case ? catastor : honest;
  setup(p.activated(actor), "Monster effect did not activate in the Damage Step");
  observed("Actor phase " + std::to_string(p.links.at(p.link_index(actor)).phase) + ", Herald offered " + yn(p.offered(herald, 1)) + ";" + p.chain_text());
  expect_rule(p.activated(herald), "Rule 8: monster effects that negate activations can activate at any point in the Damage Step");
}
inline void mausoleum_uses_normal(ConfAssets& a, int rule, bool historical, bool trace) {
  Play p(a, rule, trace);
  p.field(mausoleum, 0);
  p.hand(caius, 0);
  p.hand(bazoo, 0);
  bool normal_still_offered = true;
  p.activate(mausoleum);
  p.pick(caius);
  p.check([&](const Event& e) { normal_still_offered = Play::listed(e.idle[0], bazoo) || Play::listed(e.idle[3], bazoo); });
  p.start();
  setup(p.zone_has(0, LOCATION_MZONE, caius), "Caius was not Summoned through Mausoleum");
  observed("LP " + std::to_string(p.d.lp(0)) + ", another Normal Summon or Set offered " + yn(normal_still_offered));
  if (historical) expect_rule(!normal_still_offered, "Errata: Mausoleum uses your Normal Summon / Set for the turn");
}
inline void storm_equipped_union(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.monster(cyber_dragon, 1, 0);
  p.monster(gearframe, 1, 1);
  p.backrow(my_body, 1, 2);
  p.hand(heavy_storm, 0);
  p.end();
  p.activate(gearframe).pick(cyber_dragon);
  p.end();
  p.activate(heavy_storm);
  p.start();
  setup(p.activated(heavy_storm), "Heavy Storm did not activate");
  setup(p.link_index(heavy_storm) > p.link_index(gearframe), "Gearframe must be equipped first");
  observed("My Body offered " + yn(p.offered(my_body, 1)));
  expect_rule(!p.offered(my_body, 1), "My Body ruling: cannot negate Heavy Storm that would destroy a monster treated as an Equip Spell");
}
inline void armory_otk(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.monster(armory_arm, 0, 0);
  p.monster(colossal, 0, 1);
  p.monster(archfiend_soldier, 1, 0);
  p.accept(colossal);
  for (int i = 0; i < 3; ++i) p.chain(colossal);
  p.pick(archfiend_soldier);
  p.end();
  p.end();
  p.activate(armory_arm);
  p.battle();
  int attacks = 0;
  bool no_attacker = false;
  p.hook = [&](const Event& e) {
    if (e.kind != MSG_SELECT_BATTLECMD) return false;
    p.actions.clear();
    const int i = Play::index_of(e.idle[0], colossal);
    if (i < 0) { no_attacker = true; return false; }
    ++attacks;
    p.pick(archfiend_soldier);
    p.d.respond((i << 16) | 1);
    return true;
  };
  p.stop = [&](const Event& e) { return e.kind == MSG_SELECT_BATTLECMD && Play::index_of(e.idle[0], colossal) < 0; };
  p.start();
  observed("Attacks " + std::to_string(attacks) + ", winner " + std::to_string(p.winner) + ", P1 LP " + std::to_string(p.d.lp(1)) + ", P0 LP " + std::to_string(p.d.lp(0)) +
           ", Colossal on field " + yn(p.zone_has(0, LOCATION_MZONE, colossal)) + ", Armory on field " + yn(p.zone_has(0, LOCATION_SZONE, armory_arm)) + ";" + p.chain_text());
  setup(p.activated(armory_arm), "Armory Arm was not activated");
  setup(attacks >= 1, "Colossal never attacked");
  expect_rule(p.winner == 0, "Errata: the Colossal Fighter + Armory Arm OTK works (three 2800 burns)");
}

// ---------------------------------------------------------------- Per-card rulings
inline void stardust_in_damage_step(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.monster(stardust, 0);
  p.set_monster(ryko, 1);
  for (int i = 0; i < 3; ++i) p.deck(blue_eyes, 1);
  p.yesno_answers = {1};
  p.pick(stardust, 0);
  p.end();
  p.end();
  p.battle();
  p.attack(stardust, ryko);
  p.check(noop);
  p.start();
  setup(p.activated(ryko), "Ryko did not activate");
  observed("Stardust offered " + yn(p.offered(stardust, 0)));
  expect_rule(p.offered(stardust, 0), "Stardust ruling: negates activations that destroy, even in the Damage Step");
}
inline void ddv_response(ConfAssets& a, int rule, bool trace, uint32_t responder, bool responder_is_monster, bool face_up, bool expected) {
  Play p(a, rule, trace);
  if (responder_is_monster) p.monster(responder, 0, 2); else p.backrow(responder, 0, 2);
  p.add(sangan, 0, LOCATION_MZONE, 0, face_up ? POS_FACEUP_ATTACK : POS_FACEDOWN_DEFENSE);
  p.monster(dark_armed, 1, 0);
  p.backrow(ddv, 1);
  p.end();
  p.activate(ddv);
  p.pick(dark_armed);
  p.start();
  setup(p.activated(ddv), "Deck Devastation Virus did not activate");
  observed("Responder offered " + yn(p.offered(responder, 0)));
  expect_rule(p.offered(responder, 0) == expected, expected ? "Ruling: this card CAN respond to Deck Devastation Virus" : "Ruling: this card CANNOT respond when only face-down monsters would be destroyed");
}
inline void stardust_oppression(ConfAssets& a, int rule, bool trace, bool pending_summon) {
  Play p(a, rule, trace);
  p.monster(stardust, 0, 0);
  p.faceup_st(oppression, 1);
  if (pending_summon) {
    p.hand(chaos_sorcerer, 0);
    p.grave(blue_eyes, 0);
    p.grave(sangan, 0);
    p.spsummon(chaos_sorcerer);
  } else {
    p.backrow(call_haunted, 0);
    p.grave(bazoo, 0);
    p.activate(call_haunted).pick(bazoo);
  }
  p.chain(oppression);
  p.start();
  setup(p.activated(oppression), "Oppression's effect did not activate");
  observed("Stardust offered " + yn(p.offered(stardust, 0)) + ";" + p.chain_text());
  expect_rule(p.offered(stardust, 0) == !pending_summon, pending_summon
      ? "Stardust ruling: cannot negate Oppression when it would negate a Special Summon"
      : "Stardust ruling: can negate Oppression when it would destroy a card on the field");
}
inline void stardust_revival_needs_resolution(ConfAssets& a, int rule, bool trace, bool negated) {
  Play p(a, rule, trace);
  p.monster(stardust, 0);
  p.hand(dark_hole, 1);
  if (negated) { p.backrow(wrath, 1); p.hand(jerry_beans, 1); }
  p.monster(bazoo, 1);
  p.chain(stardust);
  if (negated) p.chain_when(wrath, [&p] { return p.activated(stardust); });
  p.accept(stardust);
  p.end();
  p.activate(dark_hole);
  p.end();
  p.stop_at_turn = 3;
  p.start();
  setup(p.activated(stardust), "Stardust did not negate");
  int activations = 0;
  for (const auto& l : p.links) if (l.code == stardust) ++activations;
  observed("Stardust activations " + std::to_string(activations) + ", on field " + yn(p.zone_has(0, LOCATION_MZONE, stardust)) + ";" + p.chain_text());
  if (negated) expect_rule(activations == 1 && !p.zone_has(0, LOCATION_MZONE, stardust), "Stardust ruling: revival only if (1) resolved");
  else expect_rule(p.zone_has(0, LOCATION_MZONE, stardust), "Stardust ruling: revival after (1) resolved");
}
inline void rose_flip(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.monster(black_rose, 0);
  p.grave(jerry_beans, 0);
  p.set_monster(ryko, 1);
  for (int i = 0; i < 3; ++i) p.deck(blue_eyes, 1);
  p.activate(black_rose, 0);
  p.pick(jerry_beans).pick(ryko);
  p.start();
  setup(p.zone_has(0, LOCATION_REMOVED, jerry_beans), "Black Rose cost was not paid");
  observed("Ryko activated " + yn(p.activated(ryko)) + ", Ryko ATK " + (p.zone_has(1, LOCATION_MZONE, ryko) ? atk_text(p, 1, 0) : "n/a"));
  expect_rule(p.activated(ryko), "Black Rose ruling: a face-down target's Flip Effect activates after this resolves");
  expect_rule(p.zone_has(1, LOCATION_MZONE, ryko) && p.d.query_value(1, LOCATION_MZONE, 0, QUERY_ATTACK) == 0, "Black Rose ruling: target ATK becomes 0");
}
inline void brionac_macro(ConfAssets& a, int rule, bool trace) {
  for (bool macro : {false, true}) {
    Play p(a, rule, trace);
    p.monster(brionac, 0);
    p.hand(jerry_beans, 0);
    p.monster(bazoo, 1);
    if (macro) p.faceup_st(macro_cosmos, 1);
    bool offered = false;
    p.check([&](const Event& e) { offered = Play::listed(e.idle[5], brionac); });
    p.start();
    observed("Macro Cosmos " + yn(macro) + ": Brionac offered " + yn(offered));
    if (!macro) setup(offered, "Brionac must be available without Macro Cosmos");
    else expect_rule(!offered, "Brionac ruling: cannot be activated while Macro Cosmos is active");
  }
}
inline void catastor_case(ConfAssets& a, int rule, bool trace, int mode) {
  Play p(a, rule, trace);
  p.monster(catastor, 0);
  uint32_t target = jerry_beans;
  if (mode == 0) p.set_monster(jerry_beans, 1);
  if (mode == 1) p.monster(jerry_beans, 1);
  if (mode == 2) { p.monster(light_dark_dragon, 1); target = light_dark_dragon; }
  p.end();
  p.end();
  p.battle();
  p.attack(catastor, target);
  p.check(noop);
  p.start();
  observed("Catastor activated " + yn(p.activated(catastor)) + ";" + p.chain_text());
  if (mode == 0) expect_rule(!p.activated(catastor), "Catastor ruling: does not activate when attacking a face-down monster");
  else expect_rule(p.activated(catastor), mode == 1 ? "Catastor: activates against a face-up non-DARK monster" : "Catastor ruling: activates against a monster with multiple Attributes such as LADD");
}
inline void goyo_equal_atk(ConfAssets& a, int rule, bool trace) {
  {
    Play c(a, rule, trace);
    c.monster(goyo, 0);
    c.monster(jerry_beans, 1);
    c.accept(goyo);
    c.end();
    c.end();
    c.battle();
    c.attack(goyo, jerry_beans);
    c.check(noop);
    c.start();
    observed("Control, ordinary destruction: Goyo offered " + yn(c.offered(goyo, 0)));
    setup(c.offered(goyo, 0), "Control: Goyo must trigger on an ordinary battle destruction");
  }
  Play p(a, rule, trace);
  p.monster(goyo, 0);
  p.monster(colossal, 1);
  p.accept(goyo);
  p.end();
  p.end();
  p.battle();
  p.attack(goyo, colossal);
  p.check(noop);
  p.start();
  setup(p.zone_has(0, LOCATION_GRAVE, goyo) && p.zone_has(1, LOCATION_GRAVE, colossal), "Both must be destroyed");
  observed("Goyo offered " + yn(p.offered(goyo, 0)));
  expect_rule(!p.offered(goyo, 0), "Goyo ruling: does not activate when it battled equal ATK and both were destroyed");
}
inline void goyo_ryko(ConfAssets& a, int rule, bool trace) {
  {
    Play c(a, rule, trace);
    c.monster(goyo, 0);
    c.monster(jerry_beans, 1);
    c.accept(goyo);
    c.end();
    c.end();
    c.battle();
    c.attack(goyo, jerry_beans);
    c.check(noop);
    c.start();
    observed("Control, ordinary destruction: Goyo offered " + yn(c.offered(goyo, 0)));
    setup(c.offered(goyo, 0), "Control: Goyo must trigger on an ordinary battle destruction");
  }
  Play p(a, rule, trace);
  p.monster(goyo, 0);
  p.set_monster(ryko, 1);
  for (int i = 0; i < 3; ++i) p.deck(blue_eyes, 1);
  p.yesno_answers = {1};
  p.pick(goyo, 0);
  p.accept(goyo);
  p.end();
  p.end();
  p.battle();
  p.attack(goyo, ryko);
  p.check(noop);
  p.start();
  setup(p.activated(ryko), "Ryko did not activate");
  setup(p.zone_has(0, LOCATION_GRAVE, goyo), "Ryko did not destroy Goyo");
  observed("Goyo offered " + yn(p.offered(goyo, 0)));
  expect_rule(!p.offered(goyo, 0), "Goyo ruling: does not activate if removed from the field by a Flip Effect");
}
inline void gale_locks_atk(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.monster(gale, 0, 0);
  p.backrow(mst, 0, 0);
  p.monster(blue_eyes, 1, 0);
  p.hand(axe_despair, 1);
  p.end();
  p.activate(axe_despair).pick(blue_eyes);
  p.end();
  p.activate(gale).pick(blue_eyes);
  p.activate(mst).pick(axe_despair);
  p.start();
  setup(p.zone_has(1, LOCATION_GRAVE, axe_despair), "Axe was not destroyed");
  observed("Blue-Eyes ATK " + atk_text(p, 1, 0));
  expect_rule(p.d.query_value(1, LOCATION_MZONE, 0, QUERY_ATTACK) == 2000, "Gale ruling Ex.2: Blue-Eyes with Axe halves to 2000 and stays there after Axe is destroyed");
}
inline void torrential_in_damage(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.monster(blue_eyes, 0);
  p.backrow(torrential, 0);
  p.monster(tomato, 1);
  p.deck(sangan, 1);
  p.accept(tomato).pick(sangan);
  p.end();
  p.end();
  p.battle();
  p.attack(blue_eyes, tomato);
  p.check(noop);
  p.start();
  setup(p.zone_has(1, LOCATION_MZONE, sangan), "Tomato did not recruit");
  bool in_damage = false;
  for (const auto& o : p.offers) if (o.code == torrential && o.phase == PHASE_DAMAGE) in_damage = true;
  observed("Torrential offered in the Damage Step " + yn(in_damage));
  expect_rule(!in_damage, "Torrential ruling: cannot be activated during the Damage Step");
}
inline void brain_full_zones(ConfAssets& a, int rule, bool trace) {
  for (int count : {4, 5}) {
    Play p(a, rule, trace);
    for (int i = 0; i < count; ++i) p.monster(jerry_beans, 0, i);
    p.monster(bazoo, 1);
    p.hand(brain, 0);
    bool offered = false;
    p.check([&](const Event& e) { offered = Play::listed(e.idle[5], brain); });
    p.start();
    observed(std::to_string(count) + " monsters: Brain Control offered " + yn(offered));
    if (count == 4) setup(offered, "Brain Control must be available with a free Monster Zone");
    else expect_rule(!offered, "Brain Control ruling: cannot be activated if your Monster Card Zones are all occupied");
  }
}
inline void gorz_after_caius(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.monster(jerry_beans, 0, 0);
  p.hand(caius, 0);
  p.monster(sangan, 1, 0);
  p.hand(gorz, 1);
  p.chain(gorz).accept(gorz);
  p.yesno_answers = {1};
  p.summon(caius);
  p.pick(jerry_beans).pick(sangan);
  p.start();
  setup(p.zone_has(1, LOCATION_REMOVED, sangan), "Sangan was not banished");
  setup(!p.damage.empty(), "Caius inflicted no damage");
  observed("Gorz offered " + yn(p.offered(gorz, 1)));
  expect_rule(p.offered(gorz, 1), "Gorz ruling: can activate after Caius banishes your only card and inflicts 1000");
}
inline void gorz_not_direct(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.monster(jerry_beans, 0);
  p.hand(gorz, 0);
  p.monster(blue_eyes, 1);
  p.end();
  p.battle();
  p.attack(blue_eyes, jerry_beans);
  p.check(noop);
  p.start();
  setup(!p.damage.empty(), "No battle damage");
  observed("Gorz offered " + yn(p.offered(gorz, 0)));
  expect_rule(!p.offered(gorz, 0), "Gorz ruling: cannot activate if battle damage was dealt while your monster battled");
}
inline void gorz_tragoedia(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.hand(gorz, 0);
  p.hand(tragoedia, 0);
  p.monster(blue_eyes, 1);
  p.chain(gorz).accept(gorz);
  p.end();
  p.battle();
  p.attack(blue_eyes);
  p.check(noop);
  p.start();
  setup(p.activated(gorz), "Gorz did not activate");
  bool chained = false;
  for (const auto& o : p.offers) if (o.code == tragoedia && o.depth >= 1) chained = true;
  observed("Tragoedia offered as a response to Gorz " + yn(chained) + ";" + p.chain_text());
  expect_rule(!chained, "Gorz ruling: you cannot activate Gorz's effect and chain the effect of Tragoedia");
}
inline void gorz_token_window(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.hand(gorz, 0);
  p.monster(blue_eyes, 1);
  p.faceup_st(oppression, 1, 0);
  p.backrow(torrential, 1, 1);
  p.chain(gorz).accept(gorz);
  p.end();
  p.battle();
  p.attack(blue_eyes);
  p.check(noop);
  p.start();
  setup(p.zone_has(0, LOCATION_MZONE, gorz), "Gorz was not Summoned");
  bool oppression_ds = false, torrential_ds = false;
  for (const auto& o : p.offers) {
    if (o.phase != PHASE_DAMAGE && o.phase != PHASE_DAMAGE_CAL) continue;
    if (o.code == oppression) oppression_ds = true;
    if (o.code == torrential) torrential_ds = true;
  }
  observed("In the Damage Step: Oppression offered " + yn(oppression_ds) + ", Torrential offered " + yn(torrential_ds) + ", P0 monsters " + std::to_string(p.d.count(0, LOCATION_MZONE)));
  expect_rule(!oppression_ds, "Gorz ruling: (2A) cannot be negated by Royal Oppression");
  expect_rule(!torrential_ds, "Gorz ruling: Torrential Tribute cannot activate");
}
inline void chimeratech_facedown(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.set_monster(cyber_dragon, 0, 0);
  p.set_monster(cyber_dragon, 0, 1);
  p.extra(chimeratech, 0);
  bool offered = false;
  p.check([&](const Event& e) { offered = Play::listed(e.idle[1], chimeratech); });
  p.start();
  observed("Offered with face-down materials: " + yn(offered));
  expect_rule(offered, "Chimeratech ruling: can be Special Summoned by sending face-down materials you control");
}
inline void chimeratech_drain(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.monster(cyber_dragon, 0, 0);
  p.monster(cyber_dragon, 0, 1);
  p.extra(chimeratech, 0);
  p.faceup_st(skill_drain, 1);
  p.backrow(mst, 0);
  p.spsummon(chimeratech);
  p.check(noop);
  p.activate(mst).pick(skill_drain);
  p.start();
  setup(p.zone_has(0, LOCATION_MZONE, chimeratech), "Chimeratech was not Summoned");
  setup(p.zone_has(1, LOCATION_GRAVE, skill_drain), "Skill Drain was not destroyed");
  auto zones = p.zone(0, LOCATION_MZONE);
  int seq = zones.at(0)->current.sequence;
  observed("ATK after Skill Drain left: " + atk_text(p, 0, seq));
  expect_rule(p.d.query_value(0, LOCATION_MZONE, seq, QUERY_ATTACK) == 0, "Chimeratech ruling: Skill Drain makes its ATK 0, even if Skill Drain is later destroyed");
}
inline void prison_book(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.monster(blue_eyes, 1);
  p.monster(jerry_beans, 0);
  p.backrow(dimensional_prison, 0, 0);
  p.backrow(book_moon, 0, 1);
  p.chain_when(dimensional_prison, [&p] { return p.seen(MSG_ATTACK); });
  p.chain_when(book_moon, [&p] { return p.activated(dimensional_prison); });
  p.pick(blue_eyes);
  p.end();
  p.battle();
  p.attack(blue_eyes, jerry_beans);
  p.check(noop);
  p.start();
  setup(p.activated(dimensional_prison) && p.activated(book_moon), "Chain did not form");
  observed("Blue-Eyes banished " + yn(p.zone_has(1, LOCATION_REMOVED, blue_eyes)) + ", on field " + yn(p.zone_has(1, LOCATION_MZONE, blue_eyes)));
  expect_rule(p.zone_has(1, LOCATION_MZONE, blue_eyes), "Dimensional Prison ruling: Book of Moon chained means the monster is not removed");
}
inline void bottomless_chain_summon(ConfAssets& a, int rule, bool trace, bool first_link_summons) {
  Play p(a, rule, trace);
  p.backrow(bottomless, 1);
  p.backrow(call_haunted, 0, 0);
  p.grave(bazoo, 0);
  if (first_link_summons) { p.hand(monster_reborn, 0); p.grave(blue_eyes, 0); p.activate(monster_reborn).pick(blue_eyes); }
  else { p.hand(rota, 0); p.deck(exiled_force, 0); p.activate(rota).pick(exiled_force); }
  p.chain(call_haunted).pick(bazoo);
  p.chain(bottomless);
  p.start();
  setup(p.activated(call_haunted), "Call of the Haunted did not chain");
  setup(p.zone_has(0, LOCATION_MZONE, bazoo) || p.zone_has(0, LOCATION_REMOVED, bazoo), "Bazoo was not revived");
  observed("Bottomless offered " + yn(p.offered(bottomless, 1)) + ", Bazoo banished " + yn(p.zone_has(0, LOCATION_REMOVED, bazoo)) + ", Blue-Eyes banished " + yn(p.zone_has(0, LOCATION_REMOVED, blue_eyes)));
  if (first_link_summons) {
    expect_rule(p.activated(bottomless), "Bottomless ruling: can activate after the Chain, against the monster Summoned in Chain Link 1");
    expect_rule(p.zone_has(0, LOCATION_REMOVED, blue_eyes) && !p.zone_has(0, LOCATION_REMOVED, bazoo), "Bottomless ruling: destroys the monster Summoned in Chain Link 1 only");
  } else {
    expect_rule(!p.offered(bottomless, 1), "Bottomless ruling: cannot activate if a monster is Summoned as Chain Link 2+");
  }
}
// A Special Summon as Chain Link 2 with a further link resolving after it: the same
// question as 424 for Torrential Tribute. Test 069 covers only a Normal Summon.
inline void torrential_after_cl2_special(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.backrow(torrential, 1);
  p.backrow(call_haunted, 0, 0);
  p.grave(bazoo, 0);
  p.hand(rota, 0);
  p.deck(exiled_force, 0);
  p.activate(rota).pick(exiled_force);
  p.chain(call_haunted).pick(bazoo);
  p.start();
  setup(p.activated(call_haunted), "Call of the Haunted did not chain");
  setup(p.zone_has(0, LOCATION_MZONE, bazoo), "Bazoo was not revived");
  bool after_chain = false;
  for (const auto& o : p.offers) if (o.code == torrential && o.depth == 0) after_chain = true;
  observed("Torrential offered at Chain Link 1 after the Chain: " + yn(after_chain));
  expect_rule(!after_chain, "Missing the Timing page: a Summon as Chain Link 2+ is not the last thing to happen, so no Summon response window opens");
}
inline void dad_call(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.hand(dark_armed, 0);
  for (int i = 0; i < 3; ++i) p.grave(sangan, 0);
  p.hand(dark_hole, 0);
  p.backrow(call_haunted, 0);
  p.spsummon(dark_armed);
  p.check(noop);
  p.activate(dark_hole);
  bool dad_listed = true;
  p.check(noop);
  p.activate(call_haunted);
  p.hook = [&](const Event& e) {
    if (e.kind == MSG_SELECT_CARD && p.activated(dark_hole) && !p.activated(call_haunted)) dad_listed = false;
    if (e.kind == MSG_SELECT_CARD) { if (Play::listed(e.cards, dark_armed)) dad_listed = true; else dad_listed = false; }
    return false;
  };
  p.check(noop);
  p.start();
  setup(p.zone_has(0, LOCATION_GRAVE, dark_armed) || p.activated(call_haunted), "Dark Armed was not destroyed");
  observed("Dark Armed offered as a Call target " + yn(dad_listed));
  expect_rule(!dad_listed, "Dark Armed ruling: cannot be Special Summoned from the Graveyard by Call, even if properly Summoned beforehand");
}
inline void dad_repeat(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.monster(dark_armed, 0);
  for (int i = 0; i < 2; ++i) p.grave(sangan, 0);
  p.backrow(mst, 1, 0);
  p.backrow(wrath, 1, 1);
  p.activate(dark_armed).pick(sangan).pick(mst);
  p.activate(dark_armed).pick(sangan).pick(wrath);
  p.start();
  int count = 0;
  for (const auto& l : p.links) if (l.code == dark_armed) ++count;
  observed("Activations " + std::to_string(count));
  expect_rule(count == 2, "Dark Armed ruling: its Ignition Effect can be activated multiple times in a single turn");
}
inline void solemn_rounding(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace, 0, 0, {8000, 7001});
  p.hand(bazoo, 0);
  p.backrow(solemn, 1);
  p.chain(solemn);
  p.summon(bazoo);
  p.start();
  setup(p.activated(solemn), "Solemn did not activate");
  observed("P1 LP after paying half of 7001: " + std::to_string(p.d.lp(1)));
  expect_rule(p.d.lp(1) == 3501, "Solemn ruling: paying half; a fraction leaves your Life Points rounded up");
}
inline void dust_tornado_set(ConfAssets& a, int rule, bool trace, bool target_survives_as_continuous) {
  Play p(a, rule, trace);
  p.backrow(dust_tornado, 0, 0);
  p.hand(mirror_force, 0);
  p.yesno_answers = {1};
  if (target_survives_as_continuous) {
    p.backrow(skill_drain, 1, 0);
    p.chain_when(skill_drain, [&p] { return p.activated(dust_tornado); });
    p.pick(skill_drain);
  } else {
    p.backrow(mst, 1, 0);
    p.backrow(jar, 1, 1);
    p.chain_when(mst, [&p] { return p.activated(dust_tornado); });
    p.pick(jar).pick(jar);
  }
  p.activate(dust_tornado);
  p.start();
  setup(p.activated(dust_tornado), "Dust Tornado did not activate");
  const bool set_done = p.zone_has(0, LOCATION_SZONE, mirror_force);
  observed("Mirror Force Set " + yn(set_done) + ";" + p.chain_text());
  if (target_survives_as_continuous) {
    setup(p.zone_has(1, LOCATION_GRAVE, skill_drain), "Skill Drain was not destroyed");
    expect_rule(set_done, "Dust Tornado ruling: if the target was activated it is still destroyed, so you may Set");
  } else {
    setup(p.activated(mst), "MST did not chain");
    expect_rule(!set_done, "Dust Tornado ruling: if the opponent destroys the target in response, you cannot Set");
  }
}
inline void dark_end_book(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.monster(dark_end, 0);
  p.monster(blue_eyes, 1);
  p.backrow(book_moon, 1);
  p.activate(dark_end).pick(blue_eyes);
  p.chain_when(book_moon, [&p] { return p.activated(dark_end); });
  p.pick(dark_end);
  p.start();
  setup(p.activated(book_moon), "Book of Moon did not chain");
  observed("Blue-Eyes in GY " + yn(p.zone_has(1, LOCATION_GRAVE, blue_eyes)));
  expect_rule(!p.zone_has(1, LOCATION_GRAVE, blue_eyes), "Dark End ruling: if it cannot lose 500 ATK and DEF because it was flipped face-down, the target is not sent");
}
inline void oppression_debris(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.hand(debris_dragon, 0);
  p.grave(lonefire, 0);
  p.faceup_st(oppression, 1);
  p.accept(debris_dragon);
  p.chain(debris_dragon);
  p.chain_when(oppression, [&p] { return p.activated(debris_dragon); });
  p.pick(lonefire);
  p.summon(debris_dragon);
  p.start();
  setup(p.activated(debris_dragon), "Debris Dragon did not activate");
  observed("Oppression offered " + yn(p.offered(oppression, 1)) + ", Lonefire on field " + yn(p.zone_has(0, LOCATION_MZONE, lonefire)));
  expect_rule(p.activated(oppression), "Oppression ruling: can respond to a card/effect that would perform a Special Summon such as Debris Dragon");
}
inline void gardna_next_attack(ConfAssets& a, int rule, bool trace, int copies) {
  Play p(a, rule, trace);
  for (int i = 0; i < copies; ++i) p.grave(necro_gardna, 0);
  p.monster(blue_eyes, 1, 0);
  p.monster(bazoo, 1, 1);
  auto before_attack = [&p] { return p.d.phase() == PHASE_BATTLE_START; };
  for (int i = 0; i < copies; ++i) p.chain_when(necro_gardna, before_attack);
  p.end();
  p.battle();
  p.attack(blue_eyes);
  if (copies == 2) p.attack(bazoo);
  p.check(noop);
  p.start();
  int count = 0;
  for (const auto& l : p.links) if (l.code == necro_gardna) ++count;
  setup(count == copies, "Necro Gardna was not activated " + std::to_string(copies) + " times");
  observed("Gardna activations " + std::to_string(count) + ", damage events " + std::to_string(p.damage.size()) + ", P0 LP " + std::to_string(p.d.lp(0)));
  if (copies == 1) expect_rule(p.damage.empty(), "Necro Gardna ruling: activated before the attack, it negates the next attack");
  else expect_rule(p.damage.size() == 1 && p.d.lp(0) == 8000 - 1600, "Necro Gardna ruling: multiple copies all negate only the next attack");
}
inline void trooper_destroyed(ConfAssets& a, int rule, bool trace, bool face_down, bool stolen) {
  Play p(a, rule, trace);
  if (face_down) p.set_monster(card_trooper, 0); else p.monster(card_trooper, 0);
  p.deck(blue_eyes, 0);
  p.hand(dark_hole, 1);
  p.end();
  if (stolen) { p.hand(brain, 1); p.activate(brain).pick(card_trooper); }
  p.activate(dark_hole);
  p.start();
  setup(p.zone_has(0, LOCATION_GRAVE, card_trooper), "Card Trooper was not destroyed");
  observed("Card Trooper activated " + yn(p.activated(card_trooper)));
  expect_rule(p.activated(card_trooper) == !stolen, stolen ? "Card Trooper ruling: does not activate if destroyed while under the opponent's control" : "Card Trooper ruling: activates if destroyed while face-down");
}
inline void plague_case(ConfAssets& a, int rule, bool trace, int mode) {
  Play p(a, rule, trace);
  p.grave(plaguespreader, 0);
  p.hand(jerry_beans, 0);
  if (mode == 2) p.field(necrovalley, 0);
  p.activate(plaguespreader);
  p.pick(jerry_beans);
  if (mode == 0) {
    p.hand(caius, 1);
    p.monster(bazoo, 1);
    p.end();
    p.yesno_answers = {1};
    p.summon(caius);
    p.pick(bazoo).pick(plaguespreader);
  } else if (mode == 1) {
    p.hand(book_moon, 0);
    p.hand(dark_hole, 0);
    p.activate(book_moon).pick(plaguespreader);
    p.activate(dark_hole);
  }
  p.start();
  setup(p.activated(plaguespreader), "Plaguespreader did not activate");
  if (mode == 0) {
    setup(p.zone_has(0, LOCATION_REMOVED, plaguespreader), "Plaguespreader was not banished");
    observed("Damage events " + std::to_string(p.damage.size()));
    expect_rule(p.damage.empty(), "Plaguespreader ruling: Caius inflicts no damage since its Condition banishes it");
  } else if (mode == 1) {
    observed("Plaguespreader in GY " + yn(p.zone_has(0, LOCATION_GRAVE, plaguespreader)) + ", banished " + yn(p.zone_has(0, LOCATION_REMOVED, plaguespreader)));
    expect_rule(p.zone_has(0, LOCATION_GRAVE, plaguespreader), "Plaguespreader ruling: the banish effect disappears if it is flipped face-down");
  } else {
    observed("Plaguespreader on field " + yn(p.zone_has(0, LOCATION_MZONE, plaguespreader)));
    expect_rule(p.zone_has(0, LOCATION_MZONE, plaguespreader), "Plaguespreader ruling: does not target, so it is not negated by Necrovalley");
  }
}
inline void caius_bottomless(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.monster(jerry_beans, 0, 0);
  p.hand(caius, 0);
  p.monster(blue_eyes, 1, 0);
  p.backrow(bottomless, 1, 0);
  p.chain_when(bottomless, [&p] { return p.activated(caius); });
  p.yesno_answers = {1};
  p.summon(caius);
  p.pick(jerry_beans).pick(blue_eyes);
  p.start();
  setup(p.activated(caius), "Caius did not activate");
  observed("Bottomless offered " + yn(p.offered(bottomless, 1)) + ", Caius banished " + yn(p.zone_has(0, LOCATION_REMOVED, caius)) + ", target banished " + yn(p.zone_has(1, LOCATION_REMOVED, blue_eyes)));
  expect_rule(p.activated(bottomless), "Caius ruling: Bottomless is placed on the Chain after the Monarch's effect");
  expect_rule(p.zone_has(1, LOCATION_REMOVED, blue_eyes), "Caius ruling: destroying the Monarch with Bottomless does not negate its effect");
}
inline void fader_caius(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.monster(blue_eyes, 0, 0);
  p.hand(caius, 0);
  p.hand(battle_fader, 1);
  p.chain(battle_fader).accept(battle_fader);
  p.end();
  p.end();
  p.battle();
  p.attack(blue_eyes);
  // Battle Fader ends the Battle Phase, so the next prompt is Main Phase 2.
  p.yesno_answers = {1};
  p.summon(caius);
  p.pick(blue_eyes).pick(battle_fader);
  p.start();
  setup(p.activated(battle_fader), "Battle Fader did not activate");
  setup(p.zone_has(1, LOCATION_REMOVED, battle_fader), "Battle Fader was not banished");
  observed("Damage events " + std::to_string(p.damage.size()));
  expect_rule(p.damage.empty(), "Caius ruling: no damage when a DARK monster is banished by a Condition, such as Battle Fader");
}
inline void ryko_miss_timing(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.set_monster(ryko, 0);
  p.monster(dupe, 1);
  p.deck(treeborn, 1);
  for (int i = 0; i < 3; ++i) p.add(peten, 0, LOCATION_DECK, SEQ_DECKTOP, POS_FACEDOWN_DEFENSE);
  p.add(peten, 0, LOCATION_DECK, SEQ_DECKTOP, POS_FACEDOWN_DEFENSE);
  p.yesno_answers = {1};
  p.pick(dupe, 1);
  p.reposition(ryko);
  p.start();
  setup(p.activated(ryko), "Ryko did not activate");
  setup(p.zone_has(1, LOCATION_GRAVE, dupe), "Dupe Frog was not destroyed");
  observed("Dupe offered " + yn(p.offered(dupe, 1)) + ", Peten offered " + yn(p.offered(peten, 0)));
  expect_rule(!p.offered(dupe, 1), "Ryko ruling: milling is always last, so a When-you-can target misses the timing");
  expect_rule(p.offered(peten, 0), "Ryko ruling: a milled Peten may activate");
}
inline void rug_ignition(ConfAssets& a, int rule, bool historical, bool trace, bool use_ignition) {
  // The Rug negates only an effect that activates on a Normal Summon, so Armageddon Knight
  // is the positive control proving the Set card is usable in this fixture.
  Play p(a, rule, trace);
  const uint32_t actor = use_ignition ? bazoo : armageddon_knight;
  p.hand(actor, 0);
  p.grave(blue_eyes, 0);
  p.deck(sangan, 0);
  p.backrow(pulling_rug, 1);
  p.chain(actor).accept(actor);
  p.pick(use_ignition ? blue_eyes : sangan);
  p.summon(actor);
  p.start();
  setup(p.moved(actor, LOCATION_MZONE, LOCATION_HAND), "Monster was not summoned");
  if (historical || !use_ignition) setup(p.activated(actor), "Monster effect did not activate");
  observed("Pulling the Rug offered " + yn(p.offered(pulling_rug, 1)) + ";" + p.chain_text());
  if (!historical) return;
  if (use_ignition) expect_rule(!p.offered(pulling_rug, 1), "Pulling the Rug ruling: cannot be activated in response to an Ignition Effect activated via Ignition Effect Priority");
  else expect_rule(p.offered(pulling_rug, 1), "Pulling the Rug: can respond to an effect that activates on a Normal Summon");
}
inline void sirocco_case(ConfAssets& a, int rule, bool trace, int mode) {
  Play p(a, rule, trace);
  p.monster(sirocco, 0, 0);
  p.monster(bora_spear, 0, 1);
  p.monster(blue_eyes, 1, 0);
  if (mode == 3) { p.backrow(wrath, 1); p.hand(jerry_beans, 1); p.chain_when(wrath, [&p] { return p.activated(sirocco); }); }
  p.end();
  p.end();
  std::set<uint32_t> attackers;
  bool mp2_offered = true;
  if (mode == 0) {
    p.battle();
    p.main2();
    p.check([&](const Event& e) { mp2_offered = Play::listed(e.idle[5], sirocco); });
  } else {
    p.activate(sirocco).pick(sirocco, 0);
    p.battle();
    p.check([&](const Event& e) { for (const auto& c : e.idle[0]) attackers.insert(c.code); });
  }
  p.start();
  if (mode == 0) {
    observed("Sirocco offered in Main Phase 2: " + yn(mp2_offered));
    expect_rule(!mp2_offered, "Sirocco ruling: cannot be activated during your Main Phase 2");
    return;
  }
  setup(p.activated(sirocco), "Sirocco did not activate");
  observed("Bora can attack " + yn(attackers.count(bora_spear) > 0) + ", Sirocco can attack " + yn(attackers.count(sirocco) > 0));
  if (mode == 1) expect_rule(!attackers.count(bora_spear), "Sirocco ruling: other monsters cannot attack this turn");
  else expect_rule(attackers.count(bora_spear) > 0, "Sirocco ruling: if the activation is negated, your other monsters may attack");
}
inline void vayu_crow(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.grave(vayu, 0);
  p.grave(sirocco, 0);
  p.extra(armed_wing, 0);
  p.hand(dd_crow, 1);
  p.activate(vayu);
  p.pick(sirocco);
  p.chain_when(dd_crow, [&p] { return p.activated(vayu); });
  p.pick(sirocco);
  p.start();
  setup(p.activated(vayu), "Vayu did not activate");
  setup(p.activated(dd_crow), "D.D. Crow did not chain");
  setup(p.zone_has(0, LOCATION_REMOVED, sirocco), "Crow did not banish the target");
  observed("Vayu in GY " + yn(p.zone_has(0, LOCATION_GRAVE, vayu)) + ", Armed Wing on field " + yn(p.zone_has(0, LOCATION_MZONE, armed_wing)));
  expect_rule(p.zone_has(0, LOCATION_GRAVE, vayu), "Vayu ruling: Vayu is not removed if its target was banished first");
  expect_rule(!p.zone_has(0, LOCATION_MZONE, armed_wing), "Vayu ruling: no Synchro Monster is Summoned");
}
inline void vayu_no_revival(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.grave(vayu, 0);
  p.grave(sirocco, 0);
  p.extra(armed_wing, 0);
  p.hand(dark_hole, 0);
  p.backrow(call_haunted, 0);
  p.activate(vayu);
  p.pick(sirocco);
  p.activate(dark_hole);
  bool call_offered = true;
  p.check([&](const Event& e) { call_offered = Play::listed(e.idle[5], call_haunted); });
  p.start();
  setup(p.activated(vayu), "Vayu did not activate");
  setup(p.zone_has(0, LOCATION_GRAVE, armed_wing), "Armed Wing was not destroyed");
  observed("Call of the Haunted offered with only Armed Wing as a monster in the GY: " + yn(call_offered));
  expect_rule(!call_offered, "Vayu ruling: the monster was not properly Synchro Summoned, so it cannot be Special Summoned from the Graveyard");
}
// D2 measured on the metagame's Graveyard Ignition Effects rather than the source's example.
inline void gy_ignition_window(ConfAssets& a, int rule, bool historical, bool trace, bool use_vayu) {
  Play p(a, rule, trace);
  const uint32_t actor = use_vayu ? vayu : plaguespreader;
  p.hand(bazoo, 0);
  p.grave(actor, 0);
  if (use_vayu) { p.grave(sirocco, 0); p.extra(armed_wing, 0); } else p.hand(jerry_beans, 0);
  p.hand(dd_crow, 1);
  p.chain(actor);
  p.pick(use_vayu ? sirocco : jerry_beans);
  p.summon(bazoo);
  bool idle_offers = false;
  p.check([&](const Event& e) { idle_offers = Play::listed(e.idle[5], actor); });
  p.start();
  setup(p.moved(bazoo, LOCATION_MZONE, LOCATION_HAND), "Bazoo was not summoned");
  observed("Offered in the Summon window " + yn(p.offered(actor, 0)) + ", usable from idle afterwards " + yn(idle_offers) + ";" + p.chain_text());
  setup(p.link_index(actor) == 0 || idle_offers, "Effect must be activatable at all in this fixture");
  if (historical) expect_rule(p.link_index(actor) == 0, "Rule 6: the turn player can activate this Graveyard Ignition Effect as Chain Link 1 in the Summon window");
}
inline void book_resets_once_per_turn(ConfAssets& a, int rule, bool trace) {
  Play p(a, rule, trace);
  p.monster(dark_end, 0);
  p.monster(blue_eyes, 1, 0);
  p.monster(bazoo, 1, 1);
  p.hand(book_moon, 0);
  p.activate(dark_end).pick(blue_eyes);
  p.activate(book_moon).pick(dark_end);
  p.reposition(dark_end);
  bool offered = false;
  p.check([&](const Event& e) { offered = Play::listed(e.idle[5], dark_end); });
  p.start();
  setup(p.activated(book_moon), "Book of Moon did not activate");
  observed("Dark End effect offered again after Book and Flip Summon: " + yn(offered));
  expect_rule(offered, "Book of Moon ruling: a monster flipped face-down forgets if it used its once-per-turn effect");
}

inline void register_ruling_tests(Registry& r, ConfAssets& a, bool t) {
  r.both("301", "LADD SEQUENTIAL ALL CARDS", "All your cards are destroyed, then the target is revived.", [&a, t](int o, bool) { ladd_sequential(a, o, t); });
  r.both("302", "JUDGMENT DRAGON PHASE RETRY", "Reactivates after LADD negation until it mills.", [&a, t](int o, bool h) { phase_retry(a, o, h, t, judgment_dragon, false, 4); });
  r.both("303", "ASURA PRIEST PHASE RETRY", "Reactivates after LADD negation until it returns.", [&a, t](int o, bool h) { phase_retry(a, o, h, t, asura_priest, true, 0); });
  r.both("304A", "NECROVALLEY AND REKINDLING", "Rekindling does not target, so it is not negated.", [&a, t](int o, bool) { necrovalley_no_target(a, o, t, rekindling, flamvell_firedog, false); });
  r.both("304B", "NECROVALLEY AND REDMD", "REDMD does not target, so it is not negated.", [&a, t](int o, bool) { necrovalley_no_target(a, o, t, redmd, blue_eyes, true); });
  r.both("305", "SWAP FROG SAME COPY", "The same Swap Frog cannot use its effect twice.", [&a, t](int o, bool h) { swap_same_copy(a, o, h, t); });
  r.both("306A", "HERALD AT START OF DAMAGE STEP", "Herald can respond to Catastor.", [&a, t](int o, bool) { herald_before_calculation(a, o, t, true); });
  r.both("306B", "HERALD BEFORE DAMAGE CALCULATION", "Herald can respond to Honest.", [&a, t](int o, bool) { herald_before_calculation(a, o, t, false); });
  r.both("307", "MAUSOLEUM USES THE NORMAL SUMMON", "No further Normal Summon or Set this turn.", [&a, t](int o, bool h) { mausoleum_uses_normal(a, o, h, t); });
  r.both("308", "HEAVY STORM ON EQUIPPED UNION", "My Body is not available.", [&a, t](int o, bool) { storm_equipped_union(a, o, t); });
  r.both("309", "COLOSSAL FIGHTER ARMORY ARM OTK", "Three attacks win the duel.", [&a, t](int o, bool) { armory_otk(a, o, t); });

  r.both("401", "STARDUST IN THE DAMAGE STEP", "Stardust can negate Ryko's Flip Effect.", [&a, t](int o, bool) { stardust_in_damage_step(a, o, t); });
  r.both("402A", "STARDUST VERSUS DECK DEVASTATION VIRUS", "Stardust can respond.", [&a, t](int o, bool) { ddv_response(a, o, t, stardust, true, true, true); });
  r.both("402B", "MY BODY VERSUS VIRUS, FACE-UP", "My Body can respond.", [&a, t](int o, bool) { ddv_response(a, o, t, my_body, false, true, true); });
  r.both("402C", "MY BODY VERSUS VIRUS, FACE-DOWN", "My Body cannot respond.", [&a, t](int o, bool) { ddv_response(a, o, t, my_body, false, false, false); });
  r.both("403A", "STARDUST VERSUS OPPRESSION ON A SUMMON", "Stardust cannot respond.", [&a, t](int o, bool) { stardust_oppression(a, o, t, true); });
  r.both("403B", "STARDUST VERSUS OPPRESSION ON CALL", "Stardust can respond.", [&a, t](int o, bool) { stardust_oppression(a, o, t, false); });
  r.both("404A", "STARDUST REVIVES AFTER RESOLVING", "Stardust returns during the End Phase.", [&a, t](int o, bool) { stardust_revival_needs_resolution(a, o, t, false); });
  r.both("404B", "STARDUST DOES NOT REVIVE IF NEGATED", "No revival when (1) was negated.", [&a, t](int o, bool) { stardust_revival_needs_resolution(a, o, t, true); });
  r.both("405", "BLACK ROSE ON A SET FLIP MONSTER", "Ryko flips, activates and has 0 ATK.", [&a, t](int o, bool) { rose_flip(a, o, t); });
  r.both("406", "BRIONAC UNDER MACRO COSMOS", "Brionac cannot be activated.", [&a, t](int o, bool) { brionac_macro(a, o, t); });
  r.both("407A", "CATASTOR ATTACKS FACE-DOWN", "Catastor does not activate.", [&a, t](int o, bool) { catastor_case(a, o, t, 0); });
  r.both("407B", "CATASTOR ATTACKS FACE-UP NON-DARK", "Catastor activates.", [&a, t](int o, bool) { catastor_case(a, o, t, 1); });
  r.both("407C", "CATASTOR ATTACKS LADD", "Catastor activates.", [&a, t](int o, bool) { catastor_case(a, o, t, 2); });
  r.both("408", "GOYO EQUAL ATK", "Goyo does not activate.", [&a, t](int o, bool) { goyo_equal_atk(a, o, t); });
  r.both("409", "GOYO REMOVED BY RYKO", "Goyo does not activate.", [&a, t](int o, bool) { goyo_ryko(a, o, t); });
  r.both("411", "GALE LOCKS HALVED ATK", "Stays 2000 after Axe is destroyed.", [&a, t](int o, bool) { gale_locks_atk(a, o, t); });
  r.both("412", "TORRENTIAL IN THE DAMAGE STEP", "Torrential is not offered.", [&a, t](int o, bool) { torrential_in_damage(a, o, t); });
  r.both("414", "BRAIN CONTROL WITH FULL ZONES", "Brain Control is not offered.", [&a, t](int o, bool) { brain_full_zones(a, o, t); });
  r.both("415", "GORZ AFTER CAIUS BURN", "Gorz is available.", [&a, t](int o, bool) { gorz_after_caius(a, o, t); });
  r.both("416", "GORZ WHEN A MONSTER BATTLED", "Gorz is not available.", [&a, t](int o, bool) { gorz_not_direct(a, o, t); });
  r.both("417", "GORZ THEN TRAGOEDIA", "Tragoedia cannot chain to Gorz.", [&a, t](int o, bool) { gorz_tragoedia(a, o, t); });
  r.both("418", "GORZ TOKEN WINDOW", "No Oppression or Torrential in the Damage Step.", [&a, t](int o, bool) { gorz_token_window(a, o, t); });
  r.both("420", "CHIMERATECH FACE-DOWN MATERIALS", "Chimeratech can be Summoned.", [&a, t](int o, bool) { chimeratech_facedown(a, o, t); });
  r.both("421", "CHIMERATECH UNDER SKILL DRAIN", "ATK stays 0 after Drain leaves.", [&a, t](int o, bool) { chimeratech_drain(a, o, t); });
  r.both("422", "DIMENSIONAL PRISON WITH BOOK OF MOON", "The monster is not removed.", [&a, t](int o, bool) { prison_book(a, o, t); });
  r.both("424", "BOTTOMLESS AFTER CL2 SUMMON ONLY", "Bottomless is not offered.", [&a, t](int o, bool) { bottomless_chain_summon(a, o, t, false); });
  r.both("423", "TORRENTIAL AFTER CL2 SPECIAL SUMMON", "Torrential is not offered.", [&a, t](int o, bool) { torrential_after_cl2_special(a, o, t); });
  r.both("425", "BOTTOMLESS AFTER CL1 AND CL2 SUMMONS", "Bottomless hits the CL1 monster only.", [&a, t](int o, bool) { bottomless_chain_summon(a, o, t, true); });
  r.both("426", "DARK ARMED AND CALL OF THE HAUNTED", "Dark Armed is not a legal target.", [&a, t](int o, bool) { dad_call(a, o, t); });
  r.both("427", "DARK ARMED REPEATED", "Two activations in a turn.", [&a, t](int o, bool) { dad_repeat(a, o, t); });
  r.both("431", "SOLEMN JUDGMENT ROUNDING", "7001 LP becomes 3501.", [&a, t](int o, bool) { solemn_rounding(a, o, t); });
  r.both("432A", "DUST TORNADO TARGET ACTIVATED", "Target destroyed; you may Set.", [&a, t](int o, bool) { dust_tornado_set(a, o, t, true); });
  r.both("432B", "DUST TORNADO TARGET DESTROYED FIRST", "You cannot Set.", [&a, t](int o, bool) { dust_tornado_set(a, o, t, false); });
  r.both("433", "DARK END FLIPPED IN RESPONSE", "Target is not sent to the Graveyard.", [&a, t](int o, bool) { dark_end_book(a, o, t); });
  r.both("435", "OPPRESSION VERSUS DEBRIS DRAGON", "Oppression can respond.", [&a, t](int o, bool) { oppression_debris(a, o, t); });
  r.both("437", "NECRO GARDNA NEXT ATTACK", "The next attack is negated.", [&a, t](int o, bool) { gardna_next_attack(a, o, t, 1); });
  r.both("438", "TWO NECRO GARDNAS", "Only the next attack is negated.", [&a, t](int o, bool) { gardna_next_attack(a, o, t, 2); });
  r.both("439A", "CARD TROOPER DESTROYED FACE-DOWN", "Trooper activates.", [&a, t](int o, bool) { trooper_destroyed(a, o, t, true, false); });
  r.both("439B", "CARD TROOPER UNDER OPPONENT'S CONTROL", "Trooper does not activate.", [&a, t](int o, bool) { trooper_destroyed(a, o, t, false, true); });
  r.both("441A", "CAIUS BANISHES PLAGUESPREADER", "No damage.", [&a, t](int o, bool) { plague_case(a, o, t, 0); });
  r.both("441B", "PLAGUESPREADER FLIPPED THEN DESTROYED", "Goes to the Graveyard.", [&a, t](int o, bool) { plague_case(a, o, t, 1); });
  r.both("441C", "PLAGUESPREADER UNDER NECROVALLEY", "Not negated.", [&a, t](int o, bool) { plague_case(a, o, t, 2); });
  r.both("446", "CAIUS WITH BOTTOMLESS CHAINED", "Bottomless chains; target still banished.", [&a, t](int o, bool) { caius_bottomless(a, o, t); });
  r.both("447", "CAIUS BANISHES BATTLE FADER", "No damage.", [&a, t](int o, bool) { fader_caius(a, o, t); });
  r.both("448", "RYKO MILLS LAST", "Destroyed Dupe misses timing; milled Peten can activate.", [&a, t](int o, bool) { ryko_miss_timing(a, o, t); });
  r.both("449A", "PULLING THE RUG AFTER IGNITION PRIORITY", "Pulling the Rug is not offered.", [&a, t](int o, bool h) { rug_ignition(a, o, h, t, true); });
  r.both("449B", "PULLING THE RUG ON A SUMMON TRIGGER", "Pulling the Rug is offered.", [&a, t](int o, bool h) { rug_ignition(a, o, h, t, false); });
  r.both("450", "SIROCCO IN MAIN PHASE 2", "Not offered.", [&a, t](int o, bool) { sirocco_case(a, o, t, 0); });
  r.both("451", "SIROCCO LOCKS OTHER ATTACKS", "Bora cannot attack.", [&a, t](int o, bool) { sirocco_case(a, o, t, 1); });
  r.both("452", "SIROCCO NEGATED", "Bora can attack.", [&a, t](int o, bool) { sirocco_case(a, o, t, 3); });
  r.both("455", "VAYU TARGET BANISHED BY CROW", "Vayu stays; no Summon.", [&a, t](int o, bool) { vayu_crow(a, o, t); });
  r.both("456", "VAYU SUMMON CANNOT BE REVIVED", "Call has no legal target.", [&a, t](int o, bool) { vayu_no_revival(a, o, t); });
  r.both("460A", "PLAGUESPREADER GY IGNITION PRIORITY", "Can be CL1 in the Summon window.", [&a, t](int o, bool h) { gy_ignition_window(a, o, h, t, false); });
  r.both("460B", "VAYU GY IGNITION PRIORITY", "Can be CL1 in the Summon window.", [&a, t](int o, bool h) { gy_ignition_window(a, o, h, t, true); });
  r.both("459", "BOOK OF MOON RESETS ONCE PER TURN", "Dark End's effect is offered again.", [&a, t](int o, bool) { book_resets_once_per_turn(a, o, t); });
}
}  // namespace conformance
