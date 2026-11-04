// Native per-card script load probe. Links the pinned ygopro-core and the production
// script_loader.h, installs a message handler (production does not), and creates one fresh
// duel per card. Covers Forbidden cards, which production deck validation refuses.
// Usage: script_probe <cards.cdb> <code-list> <legal-ids> > probe.jsonl   (cwd: scripts/)
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <SQLiteCpp/SQLiteCpp.h>
#include "ygopro-core/common.h"
#include "ygopro-core/card_data.h"
#include "ygopro-core/ocgapi.h"
#include "ygopro/script_loader.h"

static std::map<uint32_t, card_data> cards;
static std::map<std::string, std::pair<byte*, int>> scripts;
static std::vector<std::string> diagnostics, script_requests, missing_scripts;
static std::set<uint32_t> card_requests, card_misses;

static uint32 read_card_cb(uint32 code, card_data* out) {
  if (code == 0) { *out = {}; return 0; }
  card_requests.insert(code);
  auto it = cards.find(code);
  if (it == cards.end()) { *out = {}; card_misses.insert(code); return 0; }
  *out = it->second;
  return 0;
}
static byte* read_script_cb(const char* name, int* len) {
  std::string path(name);
  auto it = scripts.find(path);
  if (it == scripts.end()) {
    int size = 0;
    byte* buffer = ygopro::read_card_script(path, &size);
    it = scripts.emplace(path, std::make_pair(buffer, size)).first;
  }
  script_requests.push_back(path);
  if (!it->second.first) missing_scripts.push_back(path);
  *len = it->second.second;
  return it->second.first;
}
static uint32 message_cb(intptr_t duel, uint32 type) {
  char text[1024] = {};
  get_log_message(duel, text);
  diagnostics.push_back(std::to_string(type) + ": " + text);
  return 0;
}
static std::string esc(const std::string& s) {
  std::string out;
  for (char ch : s) {
    if (ch == '"' || ch == '\\') { out += '\\'; out += ch; }
    else if (static_cast<unsigned char>(ch) < 0x20) out += ' ';
    else out += ch;
  }
  return out;
}
int main(int argc, char** argv) {
  if (argc != 4) { std::cerr << "usage\n"; return 2; }
  SQLite::Database db(argv[1], SQLite::OPEN_READONLY);
  std::ifstream list(argv[2]);
  std::string line;
  while (std::getline(list, line)) {
    std::istringstream iss(line);
    uint32_t code; int has;
    if (!(iss >> code >> has)) continue;
    SQLite::Statement q(db, "SELECT * FROM datas WHERE id=?");
    q.bind(1, static_cast<int64_t>(code));
    if (!q.executeStep()) continue;
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
  set_card_reader(read_card_cb);
  set_script_reader(read_script_cb);
  set_message_handler(message_cb);
  std::ifstream legal(argv[3]);
  uint32_t code;
  const uint32 extra_types = TYPE_FUSION | TYPE_SYNCHRO | TYPE_XYZ | TYPE_LINK;
  while (legal >> code) {
    diagnostics.clear(); script_requests.clear(); missing_scripts.clear();
    card_requests.clear(); card_misses.clear();
    intptr_t duel = create_duel(1);
    set_player_info(duel, 0, 8000, 5, 1);
    set_player_info(duel, 1, 8000, 5, 1);
    size_t preload = script_requests.size();
    const card_data& data = cards[code];
    uint8 location = (data.type & extra_types) ? LOCATION_EXTRA : LOCATION_DECK;
    for (int i = 0; i < 3; ++i) new_card(duel, code, 0, 0, location, 0, POS_FACEDOWN_DEFENSE);
    start_duel(duel, (1 & 0xff) << 16);
    process(duel);
    end_duel(duel);
    std::cout << "{\"code\":" << code << ",\"diagnostics\":[";
    for (size_t i = 0; i < diagnostics.size(); ++i) std::cout << (i ? "," : "") << '"' << esc(diagnostics[i]) << '"';
    std::cout << "],\"scripts\":[";
    bool first = true;
    for (size_t i = preload; i < script_requests.size(); ++i) {
      std::cout << (first ? "" : ",") << '"' << esc(script_requests[i]) << '"'; first = false;
    }
    std::cout << "],\"missing\":[";
    for (size_t i = 0; i < missing_scripts.size(); ++i) std::cout << (i ? "," : "") << '"' << esc(missing_scripts[i]) << '"';
    std::cout << "],\"card_misses\":[";
    first = true;
    for (auto miss : card_misses) { std::cout << (first ? "" : ",") << miss; first = false; }
    std::cout << "]}\n";
  }
  return 0;
}
