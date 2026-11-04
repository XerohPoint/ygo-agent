#pragma once
// Test-only differential between what the engine's prompt offered and what the policy was shown.
//
// Off unless the environment variable EDISON_PROMPT_AUDIT names a directory. When on it only
// reads: prompt bytes the environment already parsed, and the observation it already wrote. No
// observation, response or random stream depends on it, which tests/edison/prompt_audit.py checks
// by comparing whole observation traces with the audit on and off.
//
// One JSON line per duel goes to <dir>/audit-<pid>.jsonl. EDISON_PROMPT_AUDIT_TRACE=1 also writes
// one line per decision, which is what the fixtures in production_deck_label_tests.py read.
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <map>
#include <string>
#include <unistd.h>
#include <vector>

namespace ygopro {

struct PromptAuditOption {
  std::string spec;
  uint32_t code;
  int controller, location, sequence;
  int position;  // -1 when the message does not carry it.
};

class PromptAudit {
public:
  enum Field {
    Steps,          // decisions showing at least one option of this kind
    Options,        // options checked
    Hidden,         // face-down or private opposing options, exempt from identity checks
    LabelMismatch,  // the option's card id is not the prompt's code
    RowMismatch,    // the option's row does not show the prompt's code, or there is no row
    ZoneMismatch,   // the option's row is in a different zone or belongs to the other player
    SharedRow,      // two options of a card-selection prompt point at one row
    AutoOptions,    // lone options answered with no decision, as recorded in history
    AutoMismatch,   // such an option recorded a card id other than the prompt's code
    UnknownCode,    // the prompt's code is outside the code list
    PrivateShown,   // a private opposing option shows an identity with no recorded reveal
    NFields
  };

  static const char* directory() {
    static const char* const value = std::getenv("EDISON_PROMPT_AUDIT");
    return value && *value ? value : nullptr;
  }
  static bool on() { return directory() != nullptr; }
  static bool tracing() {
    static const bool value = [] {
      const char* text = std::getenv("EDISON_PROMPT_AUDIT_TRACE");
      return text && *text == '1';
    }();
    return value;
  }

  std::vector<PromptAuditOption> options;
  std::map<std::string, std::array<uint64_t, NFields>> counts;
  uint64_t decisions = 0, card_decisions = 0;
  // Own-Deck rows shown in a prompt beyond its candidates and explicit reveals.
  uint64_t extra_visible_rows = 0, prompts_with_extra_rows = 0;
  // Pairs of own-Deck candidates at the first decision of a prompt, against true Deck position.
  uint64_t order_pairs_distinct = 0, order_top_first_distinct = 0;
  uint64_t order_pairs_same = 0, order_top_first_same = 0;
  std::vector<std::string> mismatches;
  uint64_t mismatch_total = 0;

  const PromptAuditOption* find(const std::string& spec) const {
    for (const auto& option : options)
      if (option.spec == spec) return &option;
    return nullptr;
  }

  void bump(const std::string& key, Field field, uint64_t by = 1) {
    counts[key][field] += by;
  }

  void record_mismatch(std::string line) {
    if (++mismatch_total <= kMaxMismatchLines) mismatches.push_back(std::move(line));
  }

  void write_line(const std::string& line) const {
    const int fd = file();
    if (fd < 0) return;
    const std::string text = line + "\n";
    if (::write(fd, text.data(), text.size()) < 0) std::perror("prompt audit");
  }

  bool has_data() const { return decisions || !counts.empty(); }

  // Reports and empties the counts, so a later duel on this environment starts clean.
  void flush(uint64_t seed, const std::string& deck0, const std::string& deck1) {
    if (!has_data()) return;
    std::string line = "{\"type\":\"duel\",\"seed\":" + std::to_string(seed) + ",\"deck0\":\"" + deck0 +
                       "\",\"deck1\":\"" + deck1 + "\",\"decisions\":" + std::to_string(decisions) +
                       ",\"card_decisions\":" + std::to_string(card_decisions) +
                       ",\"extra_visible_rows\":" + std::to_string(extra_visible_rows) +
                       ",\"prompts_with_extra_rows\":" + std::to_string(prompts_with_extra_rows) +
                       ",\"order_pairs_distinct\":" + std::to_string(order_pairs_distinct) +
                       ",\"order_top_first_distinct\":" + std::to_string(order_top_first_distinct) +
                       ",\"order_pairs_same\":" + std::to_string(order_pairs_same) +
                       ",\"order_top_first_same\":" + std::to_string(order_top_first_same) +
                       ",\"mismatch_total\":" + std::to_string(mismatch_total) + ",\"counts\":{";
    bool first = true;
    for (const auto& [key, row] : counts) {
      if (!first) line += ",";
      first = false;
      line += "\"" + key + "\":[";
      for (int i = 0; i < NFields; ++i) line += (i ? "," : "") + std::to_string(row[i]);
      line += "]";
    }
    line += "},\"mismatches\":[";
    for (size_t i = 0; i < mismatches.size(); ++i) line += (i ? "," : "") + mismatches[i];
    line += "]}";
    write_line(line);
    counts.clear();
    mismatches.clear();
    decisions = card_decisions = extra_visible_rows = prompts_with_extra_rows = 0;
    order_pairs_distinct = order_top_first_distinct = order_pairs_same = order_top_first_same = 0;
    mismatch_total = 0;
  }

private:
  static constexpr uint64_t kMaxMismatchLines = 16;

  static int file() {
    static const int fd = [] {
      const std::string path = std::string(directory()) + "/audit-" + std::to_string(::getpid()) + ".jsonl";
      return ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    }();
    return fd;
  }
};

}  // namespace ygopro
