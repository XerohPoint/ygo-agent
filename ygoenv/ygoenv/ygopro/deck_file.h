#pragma once
#include <charconv>
#include <istream>
#include <stdexcept>
#include <string_view>
#include "deck_validation.h"

namespace ygopro::deck_rules {
inline Deck read_stream(std::istream& input) {
  Deck deck;
  std::string line;
  size_t line_number=0, section=0;
  bool main_marker=false;
  auto invalid=[&](const std::string& why) {
    throw std::runtime_error("Invalid YDK at line "+std::to_string(line_number)+": "+why);
  };
  while(std::getline(input,line)) {
    ++line_number;
    if(line_number==1 && line.compare(0,3,"\xef\xbb\xbf")==0) line.erase(0,3);
    const auto first=line.find_first_not_of(" \t\r");
    if(first==std::string::npos) continue;
    const auto last=line.find_last_not_of(" \t\r");
    std::string_view text(line.data()+first,last-first+1);
    if(text=="#main") {
      if(main_marker || section!=0 || !deck[0].empty()) invalid("misplaced #main");
      main_marker=true;continue;
    }
    if(text=="#extra") {
      if(section!=0) invalid("misplaced #extra");
      section=1;continue;
    }
    if(text=="!side") {
      if(section==2) invalid("repeated !side");
      section=2;continue;
    }
    if(text.front()=='#') continue;
    if(text.front()=='!') invalid("unknown section marker");
    uint32_t code=0;
    auto parsed=std::from_chars(text.data(),text.data()+text.size(),code);
    if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || !code)
      invalid("expected a nonzero uint32 card passcode");
    deck[section].push_back(code);
  }
  if(input.bad()) invalid("input read failed");
  auto sizes=validate_sizes(deck[0].size(),deck[1].size(),deck[2].size());
  if(!sizes) throw std::runtime_error(describe(sizes));
  return deck;
}
} // namespace ygopro::deck_rules
