#pragma once
// Rules-conformance harness. This is deliberately NOT part of edison_rules:
//   - it lives in a subdirectory, so tests/edison/*.cpp does not link it and
//     tests/edison/run.py does not fingerprint it;
//   - it may load any card, because a conformance item may name any card;
//   - fixtures assert the written Edison specification, so a fixture can fail
//     because the simulator is wrong. Those failures are recorded in
//     known_failures.json and never reach the 544-case certification gate.
//
// A failure has one of two meanings and they are kept apart on purpose:
//   [RULE]  the fixture ran and the simulator disagrees with the specification.
//   [SETUP] the fixture itself did not reach its checkpoint. This is a defect in
//           the fixture or an unexplained engine event, never evidence about a rule.
#include <deque>
#include <set>
#include <sstream>
#include "../harness.h"

namespace conformance {
using namespace certification;

struct RuleMismatch : std::runtime_error {
  using std::runtime_error::runtime_error;
};
// The specification says otherwise. Use only once the fixture reached the point
// being measured.
inline void expect_rule(bool condition, const std::string& why) {
  if (!condition) throw RuleMismatch(why);
}
// Reachability of the fixture itself.
inline void setup(bool condition, const std::string& why) {
  if (!condition) throw std::runtime_error(why);
}

inline constexpr int rule1 = (1 & 0xff) << 16;
inline constexpr int rule5 = (5 & 0xff) << 16;

// Loads any card lazily, through the same override and suffix functions that
// production's read_card_script uses, so Rule 1 sees the same script bytes.
class ConfAssets : public Assets {
  SQLite::Database db;
  std::map<std::string, std::vector<byte>> cache;
  std::set<std::string> loaded;

 public:
  ConfAssets(const std::string& database, const std::string& script_dir)
      : Assets(database, script_dir), db(database, SQLite::OPEN_READONLY) {
    active = this;
    set_card_reader(read_any_card);
    set_script_reader(read_any_script);
  }
  static ConfAssets* self() { return static_cast<ConfAssets*>(active); }
  const std::set<std::string>& loaded_scripts() const { return loaded; }

  static uint32 read_any_card(uint32 code, card_data* out) {
    if (code == 0) { *out = {}; return 0; }
    auto* a = self();
    auto it = a->cards.find(code);
    if (it != a->cards.end()) { *out = it->second; return 0; }
    SQLite::Statement q(a->db, "SELECT * FROM datas WHERE id=?");
    q.bind(1, static_cast<int>(code));
    if (!q.executeStep()) {
      *out = {};
      a->error = "Card missing from database: " + std::to_string(code);
      return 0;
    }
    card_data c{};
    c.code = code;
    c.alias = q.getColumn("alias");
    c.set_setcode(q.getColumn("setcode").getInt64());
    c.type = q.getColumn("type");
    auto level = q.getColumn("level").getUInt();
    c.level = level & 0xff;
    c.lscale = (level >> 24) & 0xff;
    c.rscale = (level >> 16) & 0xff;
    c.attack = q.getColumn("atk");
    c.defense = q.getColumn("def");
    c.race = q.getColumn("race");
    c.attribute = q.getColumn("attribute");
    a->cards.emplace(code, c);
    *out = c;
    return 0;
  }

  static byte* read_any_script(const char* name, int* len) {
    *len = 0;
    auto* a = self();
    auto file = std::filesystem::path(name).filename().string();
    auto key = std::to_string(a->script_rule) + ":" + file;
    auto found = a->cache.find(key);
    if (found == a->cache.end()) {
      std::vector<byte> data;
      auto override_text = ygopro::historical_card_script(file, a->script_rule);
      if (!override_text.empty()) {
        data.assign(override_text.begin(), override_text.end());
      } else {
        std::ifstream in(a->scripts / file, std::ios::binary);
        data.assign(std::istreambuf_iterator<char>(in), {});
      }
      if (!data.empty()) {
        auto suffix = ygopro::historical_script_suffix(file, a->script_rule);
        data.insert(data.end(), suffix.begin(), suffix.end());
      }
      found = a->cache.emplace(key, std::move(data)).first;
    }
    // Normal Monsters and Tokens have no script; the core probes and continues.
    if (found->second.empty()) return nullptr;
    a->loaded.insert(file);
    *len = static_cast<int>(found->second.size());
    return found->second.data();
  }
};

// Decodes the messages the certification Reader rejects, and delegates the rest
// to it so both decoders agree on every message they share.
struct XReader : Reader {
  Event next() {
    require(offset < data.size(), "Truncated core message");
    const int kind = data[offset];
    auto simple = [&](auto body) {
      Event e;
      e.kind = take(1);
      body(e);
      return e;
    };
    switch (kind) {
      case MSG_WIN:
        return simple([&](Event& e) { e.player = take(1); e.count = take(1); });
      case MSG_RECOVER:
      case MSG_LPUPDATE:
        return simple([&](Event& e) { e.player = take(1); e.mask = take(4); });
      case MSG_TOSS_COIN:
      case MSG_TOSS_DICE:
        return simple([&](Event& e) {
          e.player = take(1); e.count = take(1);
          for (int i = 0; i < e.count; ++i) e.options.push_back(take(1));
        });
      case MSG_SWAP:
        return simple([&](Event& e) { skip(16); });
      case MSG_RANDOM_SELECTED:
        return simple([&](Event& e) { e.player = take(1); e.count = take(1); skip(4 * e.count); });
      case MSG_ADD_COUNTER:
      case MSG_REMOVE_COUNTER:
        return simple([&](Event& e) { skip(7); });
      case MSG_FIELD_DISABLED:
        return simple([&](Event& e) { e.mask = take(4); });
      case MSG_DECK_TOP:
        return simple([&](Event& e) { skip(6); });
      case MSG_ATTACK_DISABLED:
      case MSG_REVERSE_DECK:
        return simple([&](Event& e) {});
      case MSG_SWAP_GRAVE_DECK:
        return simple([&](Event& e) { e.player = take(1); });
      case MSG_PLAYER_HINT:
        return simple([&](Event& e) { skip(6); });
      case MSG_CANCEL_TARGET:
        return simple([&](Event& e) { skip(8); });
      case MSG_SHUFFLE_SET_CARD:
        return simple([&](Event& e) { skip(1); int n = take(1); skip(8 * n); });
      case MSG_CONFIRM_DECKTOP:
      case MSG_CONFIRM_EXTRATOP:
        return simple([&](Event& e) {
          e.player = take(1); e.count = take(1);
          for (int i = 0; i < e.count; ++i) e.cards.push_back(card(false));
        });
      case MSG_SELECT_DISFIELD:
        return simple([&](Event& e) { e.player = take(1); e.count = take(1); e.mask = take(4); });
      case MSG_ANNOUNCE_RACE:
      case MSG_ANNOUNCE_ATTRIB:
        return simple([&](Event& e) { e.player = take(1); e.count = take(1); e.mask = take(4); });
      case MSG_ANNOUNCE_CARD:
        return simple([&](Event& e) {
          e.player = take(1); e.count = take(1);
          for (int i = 0; i < e.count; ++i) e.options.push_back(take(4));
        });
      case MSG_SORT_CARD:
        return simple([&](Event& e) {
          e.player = take(1); e.count = take(1);
          for (int i = 0; i < e.count; ++i) e.cards.push_back(card(false));
        });
      case MSG_SELECT_COUNTER:
        return simple([&](Event& e) {
          e.player = take(1); skip(2); e.minimum = take(2); e.count = take(1);
          for (int i = 0; i < e.count; ++i) { e.cards.push_back(card(false)); skip(2); }
        });
      default:
        return event();
    }
  }
};

struct Pick {
  uint32_t code;
  int controller = -1;
  int location = 0;
};
struct ChainLink {
  int chain;
  uint32_t code;
  int player;
  int phase;
  int location;
};
struct Offer {
  int player, phase, depth;
  uint32_t code;
  bool forced;
};

// One idle or battle prompt's worth of intent.
struct Action {
  enum Kind { Summon, SpSummon, Reposition, Set, SetST, Activate, ToBattle, ToEnd, ToMain2, Attack, AttackDirect, Check, EndBattle } kind;
  uint32_t code = 0;
  uint32_t target = 0;
  int effect_index = 0;
  std::function<void(const Event&)> check;
};

// Drives a duel with permissive defaults: decline optional things and take the
// first legal answer to anything mandatory. A fixture states only the decisions it
// cares about, and asserts afterwards from what the engine actually logged.
class Play {
 public:
  ConfAssets& assets;
  const int options;
  TestDuel d;
  const bool trace;

  std::deque<Action> actions;
  std::deque<Pick> picks;
  std::deque<uint32_t> chain_wanted;
  // Activate a card only when the predicate holds, for fixtures where the first legal
  // window is not the window being measured.
  std::vector<std::pair<uint32_t, std::function<bool()>>> chain_timed;
  std::set<uint32_t> accept_optional;
  std::deque<int> yesno_answers;
  std::deque<int> option_answers;
  std::function<bool(const Event&)> stop;
  std::function<bool(const Event&)> hook;  // true when it answered the prompt itself
  int stop_at_turn = 0;

  std::vector<Event> log;
  std::vector<ChainLink> links;
  std::vector<Offer> offers;
  std::vector<Event> moves, damage, costs, draws, battles;
  std::vector<int> negated, disabled, solved;
  std::vector<Event> idle_prompts;
  std::vector<uint32_t> place_masks;
  int winner = -1, win_reason = -1;
  int chain_depth = 0;
  int turn = 0;

  Play(ConfAssets& a, int options_, bool trace_, int opening = 0, int draw = 0,
       std::array<int, 2> lp = {8000, 8000})
      : assets(a), options(options_), d(a, opening, draw, trace_, options_), trace(trace_) {
    for (int p = 0; p < 2; ++p) set_player_info(d.handle(), p, lp[p], opening, draw);
  }

  duel& core() const { return *reinterpret_cast<duel*>(d.handle()); }
  void add(uint32_t code, int player, int location, int sequence = 0, int position = POS_FACEUP_ATTACK, int owner = -1) {
    d.add({code, uint8_t(owner < 0 ? player : owner), uint8_t(player), uint8_t(location), uint8_t(sequence), uint8_t(position)});
  }
  void hand(uint32_t code, int p) { add(code, p, LOCATION_HAND, 0, POS_FACEDOWN_DEFENSE); }
  void deck(uint32_t code, int p) { add(code, p, LOCATION_DECK, 0, POS_FACEDOWN_DEFENSE); }
  void extra(uint32_t code, int p) { add(code, p, LOCATION_EXTRA, 0, POS_FACEDOWN_DEFENSE); }
  void grave(uint32_t code, int p) { add(code, p, LOCATION_GRAVE, 0, POS_FACEUP_ATTACK); }
  void monster(uint32_t code, int p, int seq = 0, int pos = POS_FACEUP_ATTACK) { add(code, p, LOCATION_MZONE, seq, pos); }
  void set_monster(uint32_t code, int p, int seq = 0) { add(code, p, LOCATION_MZONE, seq, POS_FACEDOWN_DEFENSE); }
  void backrow(uint32_t code, int p, int seq = 0) { add(code, p, LOCATION_SZONE, seq, POS_FACEDOWN_ATTACK); }
  void faceup_st(uint32_t code, int p, int seq = 0) { add(code, p, LOCATION_SZONE, seq, POS_FACEUP_ATTACK); }
  void field(uint32_t code, int p) { add(code, p, LOCATION_SZONE, 5, POS_FACEUP_ATTACK); }

  Play& summon(uint32_t c) { actions.push_back({Action::Summon, c}); return *this; }
  Play& spsummon(uint32_t c) { actions.push_back({Action::SpSummon, c}); return *this; }
  Play& reposition(uint32_t c) { actions.push_back({Action::Reposition, c}); return *this; }
  Play& set(uint32_t c) { actions.push_back({Action::Set, c}); return *this; }
  Play& setst(uint32_t c) { actions.push_back({Action::SetST, c}); return *this; }
  Play& activate(uint32_t c, int effect_index = 0) {
    Action a{Action::Activate, c}; a.effect_index = effect_index; actions.push_back(a); return *this;
  }
  Play& battle() { actions.push_back({Action::ToBattle}); return *this; }
  Play& end() { actions.push_back({Action::ToEnd}); return *this; }
  Play& main2() { actions.push_back({Action::ToMain2}); return *this; }
  Play& end_battle() { actions.push_back({Action::EndBattle}); return *this; }
  Play& attack(uint32_t c, uint32_t target = 0) {
    Action a{Action::Attack, c}; a.target = target; actions.push_back(a); return *this;
  }
  Play& check(std::function<void(const Event&)> f) {
    Action a{Action::Check}; a.check = std::move(f); actions.push_back(std::move(a)); return *this;
  }
  Play& pick(uint32_t code, int controller = -1, int location = 0) { picks.push_back({code, controller, location}); return *this; }
  Play& chain(uint32_t code) { chain_wanted.push_back(code); return *this; }
  Play& chain_when(uint32_t code, std::function<bool()> when) { chain_timed.emplace_back(code, std::move(when)); return *this; }
  bool seen(int kind) const {
    return std::any_of(log.begin(), log.end(), [&](const Event& e) { return e.kind == kind; });
  }
  Play& accept(uint32_t code) { accept_optional.insert(code); return *this; }

  static bool listed(const std::vector<CardRef>& cards, uint32_t code) {
    return std::any_of(cards.begin(), cards.end(), [&](const CardRef& c) { return c.code == code; });
  }
  static int index_of(const std::vector<CardRef>& cards, uint32_t code, int nth = 0) {
    for (size_t i = 0; i < cards.size(); ++i)
      if (cards[i].code == code && nth-- == 0) return int(i);
    return -1;
  }
  bool offered(uint32_t code, int player = -1) const {
    return std::any_of(offers.begin(), offers.end(), [&](const Offer& o) {
      return o.code == code && (player < 0 || o.player == player);
    });
  }
  bool activated(uint32_t code) const {
    return std::any_of(links.begin(), links.end(), [&](const ChainLink& l) { return l.code == code; });
  }
  int link_index(uint32_t code) const {
    for (size_t i = 0; i < links.size(); ++i) if (links[i].code == code) return int(i);
    return -1;
  }
  bool moved(uint32_t code, int to_location, int from_location = 0) const {
    return std::any_of(moves.begin(), moves.end(), [&](const Event& m) {
      return m.from.code == code && m.to.location == to_location &&
             (!from_location || m.from.location == from_location);
    });
  }
  // Move at location: zone contents are read from the engine, not inferred.
  int zone_count(int player, int location) const { return d.count(player, location); }
  bool zone_has(int player, int location, uint32_t code) const {
    for (card* c : zone(player, location)) if (c && c->data.code == code) return true;
    return false;
  }
  std::vector<card*> zone(int player, int location) const {
    auto& p = core().game_field->player[player];
    std::vector<card*> out;
    auto add_all = [&](const auto& list) { for (card* c : list) if (c) out.push_back(c); };
    if (location == LOCATION_MZONE) add_all(p.list_mzone);
    if (location == LOCATION_SZONE) add_all(p.list_szone);
    if (location == LOCATION_HAND) add_all(p.list_hand);
    if (location == LOCATION_GRAVE) add_all(p.list_grave);
    if (location == LOCATION_REMOVED) add_all(p.list_remove);
    if (location == LOCATION_DECK) add_all(p.list_main);
    if (location == LOCATION_EXTRA) add_all(p.list_extra);
    return out;
  }
  std::string chain_text() const {
    std::ostringstream s;
    for (const auto& l : links) s << " CL" << l.chain << "=P" << l.player << ":" << l.code;
    return s.str();
  }

  void start() { d.start(options); run(); }

 private:
  bool drive(const Event& e) {
    log.push_back(e);
    switch (e.kind) {
      case MSG_NEW_TURN: ++turn; break;
      case MSG_CHAINING:
        links.push_back({e.chain, e.cards.at(0).code, e.player, d.phase(), e.cards.at(0).location});
        chain_depth = e.chain;
        break;
      case MSG_CHAIN_NEGATED: negated.push_back(e.chain); break;
      case MSG_CHAIN_DISABLED: disabled.push_back(e.chain); break;
      case MSG_CHAIN_SOLVED: solved.push_back(e.chain); break;
      case MSG_CHAIN_END: chain_depth = 0; break;
      case MSG_MOVE: moves.push_back(e); break;
      case MSG_DAMAGE: damage.push_back(e); break;
      case MSG_PAY_LPCOST: costs.push_back(e); break;
      case MSG_DRAW: draws.push_back(e); break;
      case MSG_BATTLE: battles.push_back(e); break;
      case MSG_WIN: winner = e.player; win_reason = e.count; return true;
      default: break;
    }
    if (e.kind == MSG_SELECT_CHAIN)
      for (const auto& c : e.cards) offers.push_back({e.player, d.phase(), chain_depth, c.code, e.forced});
    if (e.kind == MSG_SELECT_EFFECTYN)
      for (const auto& c : e.cards) offers.push_back({e.player, d.phase(), chain_depth, c.code, false});
    if (stop && stop(e)) return true;
    if (stop_at_turn && turn >= stop_at_turn) return true;
    if (hook && hook(e)) return false;
    switch (e.kind) {
      case MSG_SELECT_IDLECMD: return idle(e);
      case MSG_SELECT_BATTLECMD: return battle_prompt(e);
      case MSG_SELECT_CHAIN: answer_chain(e); break;
      case MSG_SELECT_EFFECTYN: d.respond(accept_optional.count(e.cards.at(0).code) ? 1 : 0); break;
      case MSG_SELECT_YESNO: {
        int answer = 0;
        if (!yesno_answers.empty()) { answer = yesno_answers.front(); yesno_answers.pop_front(); }
        d.respond(answer);
        break;
      }
      case MSG_SELECT_OPTION: {
        int answer = 0;
        if (!option_answers.empty()) { answer = option_answers.front(); option_answers.pop_front(); }
        d.respond(answer);
        break;
      }
      case MSG_SELECT_CARD:
      case MSG_SELECT_TRIBUTE: answer_cards(e); break;
      case MSG_SELECT_UNSELECT_CARD: answer_unselect(e); break;
      case MSG_SELECT_SUM: answer_sum(e); break;
      case MSG_SELECT_PLACE:
      case MSG_SELECT_DISFIELD: answer_place(e); break;
      case MSG_SELECT_POSITION: {
        int want = POS_FACEUP_ATTACK;
        if (!(e.mask & want)) want = e.mask & -e.mask;
        d.respond(want);
        break;
      }
      case MSG_ANNOUNCE_NUMBER: d.respond(0); break;
      case MSG_ANNOUNCE_RACE:
      case MSG_ANNOUNCE_ATTRIB: d.respond(int(e.mask & -e.mask)); break;
      case MSG_SORT_CARD: d.respond(-1); break;
      case MSG_ANNOUNCE_CARD:
      case MSG_SELECT_COUNTER:
        throw std::runtime_error("Fixture needs a hook for prompt " + std::to_string(e.kind));
      default: break;
    }
    return false;
  }

  bool idle(const Event& e) {
    idle_prompts.push_back(e);
    while (!actions.empty() && actions.front().kind == Action::Check) {
      actions.front().check(e);
      actions.pop_front();
    }
    if (actions.empty()) return true;  // checkpoint: nothing further scripted
    Action a = actions.front();
    actions.pop_front();
    auto do_group = [&](int group) {
      int i = index_of(e.idle[group], a.code, a.effect_index);
      setup(i >= 0, "Idle action unavailable: group " + std::to_string(group) + " code " + std::to_string(a.code));
      d.respond((i << 16) | group);
    };
    switch (a.kind) {
      case Action::Summon: do_group(0); break;
      case Action::SpSummon: do_group(1); break;
      case Action::Reposition: do_group(2); break;
      case Action::Set: do_group(3); break;
      case Action::SetST: do_group(4); break;
      case Action::Activate: do_group(5); break;
      case Action::ToBattle: setup(e.to_battle, "Battle Phase unavailable"); d.respond(6); break;
      case Action::ToEnd: setup(e.to_end, "End Phase unavailable"); d.respond(7); break;
      default: throw std::runtime_error("Battle action queued at an idle prompt");
    }
    return false;
  }

  bool battle_prompt(const Event& e) {
    while (!actions.empty() && actions.front().kind == Action::Check) {
      actions.front().check(e);
      actions.pop_front();
    }
    if (actions.empty()) return true;
    Action a = actions.front();
    actions.pop_front();
    if (a.kind == Action::Attack) {
      int i = index_of(e.idle[0], a.code);
      setup(i >= 0, "Attacker unavailable: " + std::to_string(a.code));
      if (a.target) picks.push_front({a.target, -1, 0});
      d.respond((i << 16) | 1);
    } else if (a.kind == Action::Activate) {
      int i = index_of(e.idle[5], a.code, a.effect_index);
      setup(i >= 0, "Battle activation unavailable: " + std::to_string(a.code));
      d.respond((i << 16) | 0);
    } else if (a.kind == Action::EndBattle) {
      d.respond(e.to_end ? 3 : 2);
    } else if (a.kind == Action::ToMain2) {
      d.respond(2);
    } else {
      throw std::runtime_error("Idle action queued at a battle prompt");
    }
    return false;
  }

  void answer_chain(const Event& e) {
    for (auto it = chain_timed.begin(); it != chain_timed.end(); ++it) {
      int i = index_of(e.cards, it->first);
      if (i >= 0 && it->second()) { chain_timed.erase(it); d.respond(i); return; }
    }
    for (auto it = chain_wanted.begin(); it != chain_wanted.end(); ++it) {
      int i = index_of(e.cards, *it);
      if (i >= 0) { chain_wanted.erase(it); d.respond(i); return; }
    }
    d.respond(e.forced && !e.cards.empty() ? 0 : -1);
  }

  std::vector<int> choose(const std::vector<CardRef>& cards, int count) {
    std::vector<int> out;
    auto used = [&](int i) { return std::find(out.begin(), out.end(), i) != out.end(); };
    for (auto it = picks.begin(); it != picks.end() && int(out.size()) < count;) {
      int found = -1;
      for (size_t i = 0; i < cards.size(); ++i)
        if (!used(int(i)) && cards[i].code == it->code &&
            (it->controller < 0 || cards[i].controller == it->controller) &&
            (!it->location || cards[i].location == it->location)) { found = int(i); break; }
      if (found >= 0) { out.push_back(found); it = picks.erase(it); }
      else ++it;
    }
    for (size_t i = 0; i < cards.size() && int(out.size()) < count; ++i)
      if (!used(int(i))) out.push_back(int(i));
    return out;
  }

  void respond_list(const std::vector<int>& list) {
    std::array<byte, 64> buffer{};
    require(list.size() < buffer.size(), "Oversized selection");
    buffer[0] = byte(list.size());
    for (size_t i = 0; i < list.size(); ++i) buffer[i + 1] = byte(list[i]);
    if (trace) {
      std::cout << "TRACE response bytes";
      for (size_t i = 0; i <= list.size(); ++i) std::cout << ' ' << int(buffer[i]);
      std::cout << '\n';
    }
    set_responseb(d.handle(), buffer.data());
  }

  void answer_cards(const Event& e) {
    int count = std::max(e.minimum, 1);
    if (e.minimum == 0 && picks.empty()) count = 0;
    respond_list(choose(e.cards, count));
  }

  void answer_unselect(const Event& e) {
    if (e.finishable && int(e.selected.size()) >= std::max(e.minimum, 1) && !e.cards.empty() &&
        (picks.empty() || !std::any_of(picks.begin(), picks.end(), [&](const Pick& p) {
           return listed(e.cards, p.code); }))) { d.respond(-1); return; }
    if (e.finishable && e.cards.empty()) { d.respond(-1); return; }
    auto list = choose(e.cards, 1);
    setup(!list.empty(), "No selectable card in unselect prompt");
    respond_list(list);
  }

  // Selects non-mandatory cards until the required level total is met.
  void answer_sum(const Event& e) {
    uint32_t total = 0;
    for (const auto& c : e.mandatory) total += c.sum_value & 0xffff;
    std::vector<int> out;
    for (size_t i = 0; i < e.cards.size() && total < e.mask; ++i) {
      total += e.cards[i].sum_value & 0xffff;
      out.push_back(int(i));
    }
    std::array<byte, 64> buffer{};
    buffer[0] = byte(out.size() + e.mandatory.size());
    size_t at = 1;
    for (size_t i = 0; i < e.mandatory.size(); ++i) buffer[at++] = byte(i);
    for (size_t i = 0; i < out.size(); ++i) buffer[at++] = byte(out[i]);
    set_responseb(d.handle(), buffer.data());
  }

  void answer_place(const Event& e) {
    place_masks.push_back(e.mask);
    // In the mask a set bit is an unavailable zone. Own zones occupy the low 16 bits.
    for (int seq = 0; seq < 5; ++seq)
      if (!(e.mask & (1u << seq))) { d.respond({byte(e.player), LOCATION_MZONE, byte(seq)}); return; }
    for (int seq = 0; seq < 8; ++seq)
      if (!(e.mask & (1u << (8 + seq)))) { d.respond({byte(e.player), LOCATION_SZONE, byte(seq)}); return; }
    for (int seq = 0; seq < 5; ++seq)
      if (!(e.mask & (1u << (16 + seq)))) { d.respond({byte(1 - e.player), LOCATION_MZONE, byte(seq)}); return; }
    for (int seq = 0; seq < 8; ++seq)
      if (!(e.mask & (1u << (24 + seq)))) { d.respond({byte(1 - e.player), LOCATION_SZONE, byte(seq)}); return; }
    throw std::runtime_error("No available zone in place prompt");
  }

  void run() {
    bool confirmation_wait = false;
    for (int steps = 0; steps < 5000; ++steps) {
      auto result = process(d.handle());
      assets.check();
      std::vector<byte> data(core().message_buffer.size());
      auto n = get_message(d.handle(), data.data());
      require(n == static_cast<int>(data.size()), "Message buffer size mismatch");
      if (data.empty()) {
        if (confirmation_wait && (result & PROCESSOR_FLAG) == PROCESSOR_WAITING) { confirmation_wait = false; continue; }
        require((result & PROCESSOR_FLAG) != PROCESSOR_END, "Engine ended without a result");
        continue;
      }
      confirmation_wait = false;
      XReader reader{{data}};
      while (reader.offset < data.size()) {
        auto begin = reader.offset;
        auto event = reader.next();
        confirmation_wait = event.kind == MSG_CONFIRM_CARDS;
        if (trace) {
          std::cout << "TRACE message";
          for (size_t i = begin; i < reader.offset; ++i)
            std::cout << ' ' << std::hex << std::setw(2) << std::setfill('0') << int(data[i]);
          std::cout << std::dec << '\n';
        }
        if (drive(event)) return;
      }
      if ((result & PROCESSOR_FLAG) == PROCESSOR_WAITING) confirmation_wait = false;
      require((result & PROCESSOR_FLAG) != PROCESSOR_END, "Engine ended unexpectedly");
    }
    throw std::runtime_error("Conformance fixture exceeded 5000 process calls");
  }
};
}  // namespace conformance
