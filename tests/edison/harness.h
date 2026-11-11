#pragma once

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <random>
#include <stdexcept>
#include <vector>
#include <SQLiteCpp/SQLiteCpp.h>
#include "ygopro-core/card.h"
#include "ygopro-core/card_data.h"
#include "ygopro-core/duel.h"
#include "ygopro-core/effect.h"
#include "ygopro-core/field.h"
#include "ygopro-core/ocgapi.h"
#include "ygoenv/ygopro/duel_config.h"
#include "ygoenv/ygopro/historical_scripts.h"

namespace certification {
constexpr uint32_t bazoo = 40133511;
constexpr uint32_t bottomless = 29401950;
constexpr uint32_t blue_eyes = 89631139;
constexpr uint32_t yami = 59197169;
constexpr uint32_t forest = 87430998;
constexpr uint32_t sangan = 26202165;
constexpr uint32_t caius = 9748752;
constexpr uint32_t torrential = 53582587;
constexpr uint32_t dupe = 46239604;
constexpr uint32_t treeborn = 12538374;
constexpr uint32_t brain = 87910978;
constexpr uint32_t dealings = 74117290;
constexpr uint32_t broww = 79126789;
constexpr uint32_t watapon = 87774234;
constexpr uint32_t bell = 20228463;
constexpr uint32_t jar = 83968380;
constexpr uint32_t dekoichi = 87621407;
constexpr uint32_t tomato = 83011277;
constexpr uint32_t rush = 70046172;
constexpr uint32_t walls = 44209392;
constexpr uint32_t lily = 79575620;
constexpr uint32_t wrath = 49010598;
constexpr uint32_t herald = 17266660;
constexpr uint32_t chalice = 25789292;
constexpr uint32_t skill_drain = 82732705;
constexpr uint32_t mst = 5318639;
constexpr uint32_t book_moon = 14087893;
constexpr uint32_t compulsory = 94192409;
constexpr uint32_t lonefire = 48686504;
constexpr uint32_t exiled_force = 74131780;
constexpr uint32_t jerry_beans = 23635815;
constexpr uint32_t call_haunted = 97077563;
constexpr uint32_t enemy_controller = 98045062;
constexpr uint32_t dd_crow = 24508238;
constexpr uint32_t swap_frog = 9126351;
constexpr uint32_t cyber_valley = 3657444;
constexpr uint32_t honest = 37742478;
constexpr uint32_t kalut = 85215458;
constexpr uint32_t bora = 49003716;
constexpr uint32_t debris_dragon = 14943837;
constexpr uint32_t goyo = 7391448;
constexpr uint32_t gale = 2009101;
constexpr uint32_t psychic_commander = 21454943;
constexpr uint32_t sacred_crane = 30914564;
constexpr uint32_t survivor = 48092532;
constexpr uint32_t catapult_turtle = 95727991;
constexpr uint32_t brionac = 50321796;
constexpr uint32_t necrovalley = 47355498;
constexpr uint32_t darkness_approaches = 80168720;
constexpr uint32_t ryko = 21502796;
constexpr uint32_t quickdraw = 20932152;
constexpr uint32_t lumina = 95503687;
constexpr uint32_t susa = 40473581;
constexpr uint32_t light_dark_dragon = 47297616;
constexpr uint32_t prisma = 89312388;
constexpr uint32_t blue_eyes_ultimate = 23995346;
constexpr uint32_t rescue_cat = 14878871;
constexpr uint32_t sea_koala = 87685879;
constexpr uint32_t strike_ninja = 41006930;
constexpr uint32_t redmd = 88264978;
constexpr uint32_t fortune_light = 34471458;
constexpr uint32_t future_fusion = 77565204;
constexpr uint32_t vanity_fiend = 47084486;
constexpr uint32_t dark_end = 88643579;
constexpr uint32_t light_end = 25132288;
constexpr uint32_t cyber_phoenix = 3370104;
constexpr uint32_t jade_knight = 44364207;
constexpr uint32_t armory_arm = 29071332;
constexpr uint32_t colossal = 23693634;
constexpr uint32_t gearframe = 42940404;
constexpr uint32_t black_garden = 71645242;
constexpr uint32_t shallow_grave = 43434803;
constexpr uint32_t rose_token = 71645243;
constexpr uint32_t mark_rose = 45247637;
constexpr uint32_t cold_wave = 60682203;
constexpr uint32_t polymerization = 24094653;
constexpr uint32_t destiny_dragoon = 76263644;
constexpr uint32_t plasma = 83965310;
constexpr uint32_t dogma = 17132130;
constexpr uint32_t soul_exchange = 68005187;
constexpr uint32_t cyber_dragon = 70095154;
constexpr uint32_t my_body = 69279219;
constexpr uint32_t oppression = 93016201;
constexpr uint32_t man_eater = 54652250;
constexpr uint32_t judgment = 41420027;
constexpr uint32_t mausoleum = 80921533;
constexpr uint32_t offering = 80604091;
constexpr uint32_t urgent_tuning = 94634433;
constexpr uint32_t dark_panther = 43237273;
constexpr uint32_t creature_swap = 31036355;
constexpr uint32_t gold_sarcophagus = 75500286;
constexpr uint32_t seed = 20100424;

inline void require(bool condition, const std::string& why) {
  if (!condition) throw std::runtime_error(why);
}

// This executable owns the core's process-global callbacks. It must not be
// embedded in the training process. Callback failures are surfaced immediately.
struct Assets {
  std::filesystem::path scripts;
  std::string database_path;
  int script_rule = 0;
  std::map<uint32_t, card_data> cards;
  std::map<std::string, std::vector<byte>> buffers;
  std::string error;
  inline static Assets* active = nullptr;

  Assets(const std::string& database, const std::string& script_dir) : scripts(script_dir), database_path(database) {
    SQLite::Database db(database, SQLite::OPEN_READONLY);
    for (auto code : {bazoo, bottomless, blue_eyes, yami, forest, sangan, caius, torrential, dupe, treeborn, brain, dealings, broww, watapon, bell, jar, dekoichi, tomato, rush, walls, lily, wrath, herald, chalice, skill_drain, mst, book_moon, compulsory, lonefire, exiled_force, jerry_beans, call_haunted, enemy_controller, dd_crow, swap_frog, cyber_valley, honest, kalut, bora, debris_dragon, goyo, gale, psychic_commander, sacred_crane, survivor, catapult_turtle, brionac, necrovalley, darkness_approaches, ryko, quickdraw, lumina, susa, light_dark_dragon, prisma, blue_eyes_ultimate, rescue_cat, sea_koala, strike_ninja, redmd, fortune_light, future_fusion, vanity_fiend, dark_end, light_end, cyber_phoenix, jade_knight, armory_arm, colossal, gearframe, black_garden, shallow_grave, rose_token, mark_rose, cold_wave, polymerization, destiny_dragoon, plasma, dogma, soul_exchange, cyber_dragon, my_body, oppression, man_eater, judgment, mausoleum, offering, urgent_tuning, dark_panther, creature_swap, gold_sarcophagus}) {
      SQLite::Statement q(db, "SELECT * FROM datas WHERE id=?");
      q.bind(1, static_cast<int>(code));
      require(q.executeStep(), "Missing fixture card " + std::to_string(code));
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
      cards.emplace(code, c);
    }
    require(cards.at(bazoo).attack == 1600 && cards.at(bazoo).level == 4,
            "Bazoo fixture must be a 1600 ATK level 4 monster");
    for (auto code : {yami, forest})
      require(cards.at(code).type == (TYPE_SPELL | TYPE_FIELD), "Expected a Field Spell fixture");
    require(cards.at(jerry_beans).type == (TYPE_MONSTER | TYPE_NORMAL) &&
            cards.at(jerry_beans).race == RACE_PLANT, "Expected a normal Plant recruit");
    active = this;
    set_card_reader(read_card);
    set_script_reader(read_script);
    set_message_handler(log_message);
  }

  static uint32 read_card(uint32 code, card_data* out) {
    // duel() creates an internal temporary card with code 0.
    if (code == 0) { *out = {}; return 0; }
    auto it = active->cards.find(code);
    if (it == active->cards.end()) {
      *out = {};
      active->error = "Unexpected card requested: " + std::to_string(code);
    } else *out = it->second;
    return 0;
  }

  static byte* read_script(const char* name, int* len) {
    *len = 0;
    auto file = std::filesystem::path(name).filename().string();
    // The core probes c0 and normal monsters; these intentionally have no script.
    if (file == "c0.lua" || file == "c89631139.lua" || file == "c23635815.lua" || file == "c71645243.lua") return nullptr;
    const std::array<std::string, 91> allowed = {
        "constant.lua", "utility.lua", "procedure.lua", "c40133511.lua", "c29401950.lua",
        "c59197169.lua", "c87430998.lua", "c26202165.lua", "c9748752.lua", "c53582587.lua", "c46239604.lua", "c12538374.lua", "c87910978.lua", "c74117290.lua", "c79126789.lua", "c87774234.lua", "c20228463.lua", "c83968380.lua", "c87621407.lua", "c83011277.lua", "c70046172.lua", "c44209392.lua", "c79575620.lua", "c49010598.lua", "c17266660.lua", "c25789292.lua", "c82732705.lua", "c5318639.lua", "c14087893.lua", "c94192409.lua", "c48686504.lua", "c74131780.lua", "c97077563.lua", "c98045062.lua", "c24508238.lua", "c9126351.lua", "c3657444.lua", "c37742478.lua", "c85215458.lua", "c49003716.lua", "c14943837.lua", "c7391448.lua", "c2009101.lua", "c21454943.lua", "c30914564.lua", "c48092532.lua", "c95727991.lua", "c50321796.lua", "c47355498.lua", "c80168720.lua", "c21502796.lua", "c20932152.lua", "c95503687.lua", "c40473581.lua", "c47297616.lua", "c89312388.lua", "c23995346.lua", "c14878871.lua", "c87685879.lua", "c41006930.lua", "c88264978.lua", "c34471458.lua", "c77565204.lua", "c47084486.lua", "c88643579.lua", "c25132288.lua", "c3370104.lua", "c44364207.lua", "c29071332.lua", "c23693634.lua", "c42940404.lua", "c71645242.lua", "c43434803.lua", "c45247637.lua", "c60682203.lua", "c24094653.lua", "c76263644.lua", "c83965310.lua", "c17132130.lua", "c68005187.lua", "c70095154.lua", "c69279219.lua", "c93016201.lua", "c54652250.lua", "c41420027.lua", "c80921533.lua", "c80604091.lua", "c94634433.lua", "c43237273.lua", "c31036355.lua", "c75500286.lua"};
    if (std::find(allowed.begin(), allowed.end(), file) == allowed.end()) {
      active->error = "Unpinned script requested: " + file;
      return nullptr;
    }
    auto override = ygopro::historical_card_script(file, active->script_rule);
    auto& data = active->buffers[std::to_string(active->script_rule) + ":" + file];
    if (data.empty()) {
      if (!override.empty()) data.assign(override.begin(), override.end());
      else {
        std::ifstream in(active->scripts / file, std::ios::binary);
        data.assign(std::istreambuf_iterator<char>(in), {});
      }
      if (!data.empty()) {
        auto suffix=ygopro::historical_script_suffix(file,active->script_rule);
        data.insert(data.end(),suffix.begin(),suffix.end());
      }
    }
    if (data.empty()) {
      active->error = "Missing/empty script: " + file;
      return nullptr;
    }
    *len = static_cast<int>(data.size());
    return data.data();
  }

  static uint32 log_message(intptr_t handle, uint32) {
    char message[256]{};
    get_log_message(handle, message);
    active->error = std::string("Core/Lua diagnostic: ") + message;
    return 0;
  }

  void check() const { require(error.empty(), error); }
};

// playerid in ocgapi is the controller; owner can be different. In stacked
// zones sequence follows ocgapi insertion semantics, not a post-insertion move.
struct Placement {
  uint32_t code;
  uint8_t owner, controller, location, sequence, position;
};

struct CardRef {
  uint32_t code = 0;
  uint8_t controller = 0, location = 0, sequence = 0, position = 0;
  uint32_t description = 0;
  uint32_t sum_value = 0;
};

struct Event {
  int kind = 0, player = -1, phase = 0, count = 0, minimum = 0, maximum = 0;
  int chain = 0, sum_mode = 0;
  bool forced = false, to_end = false, to_battle = false, finishable = false, cancelable = false;
  std::array<uint32_t, 4> battle_stats{};
  std::array<bool, 2> battle_destroyed{};
  uint32_t mask = 0;
  uint32_t reason = 0;
  CardRef from, to;
  std::vector<CardRef> cards;
  std::vector<uint32_t> options;
  std::vector<CardRef> mandatory;
  std::vector<CardRef> selected;
  std::array<std::vector<CardRef>, 6> idle;
};

// Protocol integers are little endian. Never skip the remainder of a process
// buffer: multiple messages may be returned together, including a prompt.
struct Reader {
  const std::vector<byte>& data;
  size_t offset = 0;
  uint32_t take(size_t n) {
    require(n <= 4 && offset + n <= data.size(), "Truncated core message");
    uint32_t value = 0;
    for (size_t i = 0; i < n; ++i) value |= uint32_t(data[offset++]) << (8 * i);
    return value;
  }
  void skip(size_t n) {
    require(offset + n <= data.size(), "Truncated core message payload");
    offset += n;
  }
  CardRef card(bool position) {
    CardRef c;
    c.code = take(4);
    c.controller = take(1); c.location = take(1); c.sequence = take(1);
    if (position) c.position = take(1);
    return c;
  }
  Event event() {
    Event e;
    e.kind = take(1);
    switch (e.kind) {
      case MSG_NEW_TURN: e.player = take(1); break;
      case MSG_NEW_PHASE: e.phase = take(2); break;
      case MSG_DRAW:
        e.player = take(1); e.count = take(1);
        for (int i = 0; i < e.count; ++i) { CardRef c; c.code = take(4); e.cards.push_back(c); }
        break;
      case MSG_SELECT_BATTLECMD:
        e.player=take(1);
        for(int group : {5, 0}) {
          int n=take(1);
          for(int i=0;i<n;++i) {
            auto c=card(false);
            if(group==5) c.description=take(4);
            else c.position=take(1); // direct-attack availability, not position
            e.idle[group].push_back(c);
          }
        }
        take(1); e.to_end=take(1); break;
      case MSG_ATTACK:
        e.from.controller=take(1); e.from.location=take(1); e.from.sequence=take(1); e.from.position=take(1);
        e.to.controller=take(1); e.to.location=take(1); e.to.sequence=take(1); e.to.position=take(1);
        break;
      case MSG_BATTLE:
        for(int i=0;i<2;++i) {
          CardRef c; c.controller=take(1); c.location=take(1); c.sequence=take(1); c.position=take(1);
          e.cards.push_back(c);
          e.battle_stats[2*i]=take(4); e.battle_stats[2*i+1]=take(4);
          e.battle_destroyed[i]=take(1);
        }
        break;
      case MSG_DAMAGE: e.player=take(1); e.mask=take(4); break;
      case MSG_DAMAGE_STEP_START: case MSG_DAMAGE_STEP_END: break;
      case MSG_SELECT_IDLECMD:
        e.player = take(1);
        for (int group = 0; group < 6; ++group) {
          int n = take(1);
          for (int i = 0; i < n; ++i) {
            auto c = card(false);
            if (group == 5) c.description = take(4);
            e.idle[group].push_back(c);
          }
        }
        e.to_battle = take(1); e.to_end = take(1); take(1);
        break;
      case MSG_SELECT_CHAIN:
        e.player = take(1); e.count = take(1); take(1); e.forced = take(1); skip(8);
        for (int i = 0; i < e.count; ++i) {
          take(1); auto c = card(true); c.description = take(4); e.cards.push_back(c);
        }
        break;
      case MSG_ANNOUNCE_NUMBER:
        e.player=take(1); e.count=take(1);
        for(int i=0;i<e.count;++i) e.options.push_back(take(4));
        break;
      case MSG_SELECT_EFFECTYN:
        e.player = take(1); e.cards.push_back(card(true)); take(4); break;
      case MSG_SELECT_YESNO:
        e.player = take(1); e.mask = take(4); break;
      case MSG_SELECT_TRIBUTE:
      case MSG_SELECT_CARD:
        e.player = take(1); take(1); e.minimum = take(1); e.maximum = take(1); e.count = take(1);
        for (int i = 0; i < e.count; ++i) e.cards.push_back(card(true));
        break;
      case MSG_SELECT_SUM: {
        e.sum_mode=take(1); e.player=take(1); e.mask=take(4);
        e.minimum=take(1); e.maximum=take(1);
        int mandatory_count=take(1);
        for(int i=0;i<mandatory_count;++i) {
          auto c=card(false); c.sum_value=take(4); e.mandatory.push_back(c);
        }
        e.count=take(1);
        for(int i=0;i<e.count;++i) {
          auto c=card(false); c.sum_value=take(4); e.cards.push_back(c);
        }
        break;
      }
      case MSG_SELECT_UNSELECT_CARD: {
        e.player=take(1); e.finishable=take(1); e.cancelable=take(1);
        e.minimum=take(1); e.maximum=take(1); e.count=take(1);
        for(int i=0;i<e.count;++i) e.cards.push_back(card(true));
        int selected_count=take(1);
        for(int i=0;i<selected_count;++i) e.selected.push_back(card(true));
        break;
      }
      case MSG_SELECT_OPTION:
        e.player=take(1); e.count=take(1);
        for(int i=0;i<e.count;++i) e.options.push_back(take(4));
        break;
      case MSG_SELECT_POSITION:
        e.player=take(1); { CardRef c; c.code=take(4); e.cards.push_back(c); }
        e.mask=take(1); break;
      case MSG_CONFIRM_CARDS:
        e.player=take(1); e.count=take(1);
        for(int i=0;i<e.count;++i) e.cards.push_back(card(false));
        break;
      case MSG_SELECT_PLACE:
        e.player = take(1); e.count = take(1); e.mask = take(4); break;
      case MSG_FLIPSUMMONING: case MSG_SUMMONING: case MSG_SPSUMMONING: case MSG_SET: e.cards.push_back(card(true)); break;
      case MSG_CHAINING:
        e.cards.push_back(card(true)); e.player = take(1); skip(2); take(4); e.chain = take(1); break;
      case MSG_HINT: skip(6); break;
      case MSG_PAY_LPCOST: e.player=take(1); e.mask=take(4); break;
      case MSG_MOVE:
        e.from = card(true);
        e.to.code = e.from.code;
        e.to.controller = take(1); e.to.location = take(1);
        e.to.sequence = take(1); e.to.position = take(1);
        e.reason = take(4);
        break;
      case MSG_SWAP: e.from = card(true); e.to = card(true); break;
      case MSG_POS_CHANGE:
        e.from=card(true); e.to=e.from; e.to.position=take(1); break;
      case MSG_SHUFFLE_DECK: skip(1); break;
      case MSG_SHUFFLE_EXTRA:
        e.player=take(1); e.count=take(1);
        for(int i=0;i<e.count;++i) { CardRef c; c.code=take(4); e.cards.push_back(c); }
        break;
      case MSG_SHUFFLE_HAND: skip(1); { auto n = take(1); skip(4 * n); } break;
      case MSG_BECOME_TARGET: { auto n = take(1); skip(4 * n); } break;
      case MSG_EQUIP:
      case MSG_CARD_TARGET:
        e.from.controller=take(1); e.from.location=take(1); e.from.sequence=take(1); e.from.position=take(1);
        e.to.controller=take(1); e.to.location=take(1); e.to.sequence=take(1); e.to.position=take(1);
        break;
      case MSG_MISSED_EFFECT:
        e.from.controller=take(1); e.from.location=take(1);
        e.from.sequence=take(1); e.from.position=take(1);
        e.from.code=take(4);
        break;
      case MSG_CARD_HINT: skip(9); break;
      case MSG_CHAIN_NEGATED: case MSG_CHAIN_DISABLED:
      case MSG_CHAINED: case MSG_CHAIN_SOLVING: case MSG_CHAIN_SOLVED:
        e.chain = take(1); break;
      case MSG_FLIPSUMMONED: case MSG_SUMMONED: case MSG_SPSUMMONED: case MSG_CHAIN_END: break;
      case MSG_RETRY: throw std::runtime_error("Engine rejected scripted response (MSG_RETRY)");
      case MSG_WIN: throw std::runtime_error("Duel ended before certification checkpoint");
      default: throw std::runtime_error("Unsupported core message " + std::to_string(e.kind));
    }
    return e;
  }
};

class TestDuel {
  Assets& assets;
  std::unique_ptr<duel> core;
  bool started = false;
  bool trace;
  int script_options;
  static std::unique_ptr<duel> create_core(Assets& assets, int options) {
    assets.script_rule = (options >> 16) & 0xff;
    return std::make_unique<duel>();
  }
 public:
  TestDuel(Assets& assets_, int opening, int draws, bool trace_, int options)
      : assets(assets_), core(create_core(assets_, options)), trace(trace_), script_options(options) {
    // Match YGOProEnvImpl::YGO_CreateDuel, including its seed expansion.
    std::mt19937 generator(seed);
    core->random.reset(generator());
    assets.check();
    for (int p = 0; p < 2; ++p)
      set_player_info(handle(), p, ygopro::duel_config::starting_lp, opening, draws);
  }
  intptr_t handle() const { return reinterpret_cast<intptr_t>(core.get()); }
  int count(int p, int location) const { return query_field_count(handle(), p, location); }
  int phase() const { return core->game_field->infos.phase; }
  int lp(int p) const { return core->game_field->player[p].lp; }
  void add(const Placement& c) {
    require(!started, "Fixture placements are only allowed before start");
    require(c.owner <= 1 && c.controller <= 1, "Invalid fixture player");
    int before = count(c.controller, c.location);
    new_card(handle(), c.code, c.owner, c.controller, c.location, c.sequence, c.position);
    assets.check();
    require(count(c.controller, c.location) == before + 1, "Engine rejected fixture placement");
  }
  void start(int options) {
    require(!started, "Duel already started");
    require(options == script_options, "Script and engine rules must match");
    started = true;
    start_duel(handle(), options);
    assets.check();
  }
  void respond(int value) {
    if (trace) std::cout << "TRACE response int " << value << '\n';
    set_responsei(handle(), value);
  }
  void respond(std::initializer_list<byte> bytes) {
    std::array<byte, SIZE_RETURN_VALUE> buffer{}; // duel::set_responseb copies SIZE_RETURN_VALUE bytes
    require(bytes.size() <= buffer.size(), "Oversized response");
    std::copy(bytes.begin(), bytes.end(), buffer.begin());
    if (trace) {
      std::cout << "TRACE response bytes";
      for (auto b : bytes) std::cout << ' ' << int(b);
      std::cout << '\n';
    }
    set_responseb(handle(), buffer.data());
  }
  uint32_t query_value(int player, int location, int sequence, int flag) const {
    require(flag == QUERY_CODE || flag == QUERY_ALIAS || flag == QUERY_POSITION || flag == QUERY_ATTACK || flag == QUERY_DEFENSE, "Expected a scalar card query");
    std::array<byte, 64> buffer{};
    int n = query_card(handle(), player, location, sequence, flag, buffer.data(), 0);
    require(n == 12, "Missing card at certification checkpoint");
    return uint32_t(buffer[8]) | uint32_t(buffer[9]) << 8 |
           uint32_t(buffer[10]) << 16 | uint32_t(buffer[11]) << 24;
  }
  uint32_t code(int player, int location, int sequence) const {
    return query_value(player, location, sequence, QUERY_CODE);
  }
  uint8_t position(int player, int location, int sequence) const {
    return query_value(player, location, sequence, QUERY_POSITION) >> 24;
  }
  // The turn start recharges every effect in this set. Each member must be a live effect with a
  // count limit. Membership is checked first, so a deleted effect is reported, never read.
  void check_rechargeable() const {
    for (auto* peffect : core->game_field->effects.rechargeable) {
      require(core->effects.count(peffect) == 1, "Turn-reset set holds a deleted effect");
      require(peffect->is_flag(EFFECT_FLAG_COUNT_LIMIT), "Turn-reset set holds an effect without a count limit");
    }
  }
  bool effect_live(const effect* peffect) const { return core->effects.count(const_cast<effect*>(peffect)) == 1; }
  // A card keeps the effect that last moved or summoned it in four states. The turn start deletes reset
  // effects, so each pointer must be null or name a live effect. Membership is checked, never the effect.
  void check_reason_effects() const {
    static constexpr const char* names[] = {"current", "temp", "previous", "spsummon"};
    for (auto* pcard : core->cards) {
      const card_state* states[] = {&pcard->current, &pcard->temp, &pcard->previous, &pcard->spsummon};
      for (int i = 0; i < 4; ++i)
        if (states[i]->reason_effect && core->effects.count(states[i]->reason_effect) != 1)
          require(false, "A card keeps a deleted effect as its reason effect (card " + std::to_string(pcard->data.code) +
                             ", " + names[i] + " state)");
    }
  }
  bool effect_reset(const effect* peffect) const {
    return core->game_field->core.reseted_effects.count(const_cast<effect*>(peffect)) == 1;
  }
  // The one card with that code in that player's hand, GY or banishment.
  card* card_in(int player, int location, uint32_t code) const {
    const auto& side = core->game_field->player[player];
    require(location == LOCATION_HAND || location == LOCATION_GRAVE || location == LOCATION_REMOVED,
            "Expected the hand, GY or banishment");
    const auto& list = location == LOCATION_HAND ? side.list_hand : location == LOCATION_GRAVE ? side.list_grave : side.list_remove;
    card* found = nullptr;
    for (auto* pcard : list)
      if (pcard->data.code == code) {
        require(!found, "Expected one card with that code there");
        found = pcard;
      }
    require(found != nullptr, "Card not found there");
    return found;
  }
  bool effect_rechargeable(const effect* peffect) const {
    return core->game_field->effects.rechargeable.count(const_cast<effect*>(peffect)) == 1;
  }
  // Effects copied onto the card in a Monster Zone: CopyEffect gives each a nonzero copy_id.
  std::vector<effect*> copied_effects(int player, int sequence, uint32_t code) const {
    std::vector<effect*> out;
    card* pcard = core->game_field->player[player].list_mzone.at(sequence);
    require(pcard != nullptr, "No card in that Monster Zone");
    for (auto* peffect : core->effects)
      if (peffect->handler == pcard && peffect->copy_id != 0 && peffect->code == code) out.push_back(peffect);
    return out;
  }
  // The effects still attached to the card in that Monster Zone. A reset effect leaves the card's indexer at
  // once, though the core deletes it only at the next turn start.
  std::vector<effect*> card_effects(int player, int sequence) const {
    std::vector<effect*> out;
    card* pcard = core->game_field->player[player].list_mzone.at(sequence);
    require(pcard != nullptr, "No card in that Monster Zone");
    for (const auto& entry : pcard->indexer) out.push_back(entry.first);
    return out;
  }
  void run(const std::function<bool(const Event&)>& on_event) {
    bool confirmation_wait = false;
    for (int steps = 0; steps < 5000; ++steps) {
      auto result = process(handle());
      assets.check();
      check_reason_effects();
      // The pinned core exposes the buffer size; allocate before calling the
      // legacy get_message API, which takes no destination capacity.
      std::vector<byte> data(core->message_buffer.size());
      auto n = get_message(handle(), data.data());
      require(n == static_cast<int>(data.size()), "Message buffer size mismatch");
      if (data.empty()) {
        // Duel.ConfirmCards yields through PROCESSOR_WAIT without requesting
        // a response. Permit only its immediately following empty wait, once.
        require(confirmation_wait && (result & PROCESSOR_FLAG) == PROCESSOR_WAITING,
                "Engine stopped without a certification event");
        confirmation_wait = false;
        if (trace) std::cout << "TRACE confirmation wait flags " << (result & PROCESSOR_FLAG) << '\n';
        continue;
      }
      confirmation_wait = false;
      Reader reader{data};
      while (reader.offset < data.size()) {
        auto begin = reader.offset;
        auto event = reader.event();
        confirmation_wait = event.kind == MSG_CONFIRM_CARDS;
        if (trace) {
          std::cout << "TRACE message";
          for (size_t i = begin; i < reader.offset; ++i)
            std::cout << ' ' << std::hex << std::setw(2) << std::setfill('0') << int(data[i]);
          std::cout << std::dec << '\n';
        }
        if (on_event(event)) {
          require(reader.offset == data.size(), "Unconsumed events at checkpoint");
          return;
        }
      }
      // A wait returned with the confirmation message is already consumed.
      if ((result & PROCESSOR_FLAG) == PROCESSOR_WAITING) confirmation_wait = false;
      require((result & PROCESSOR_FLAG) != PROCESSOR_END, "Engine ended unexpectedly");
    }
    throw std::runtime_error("Certification exceeded 5000 process calls");
  }
};
}  // namespace certification
