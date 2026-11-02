#pragma once
#include <algorithm>
#include <array>
#include <map>
#include <vector>
#include "ygopro-core/common.h"
#include "edison_legality_data.h"
#include <cstddef>
#include <cstdint>
#include <string>

namespace ygopro::deck_rules {
enum class Error { None, MainSize, ExtraSize, SideSize, UnknownCard, NonDeckCard, MainType, ExtraType, CardPool, CopyLimit };
struct Result {
  Error error=Error::None;
  uint32_t code=0;
  int limit=0;
  explicit operator bool() const { return error==Error::None; }
};
inline Result validate_sizes(size_t main, size_t extra, size_t side) {
  if(main<40 || main>60) return {Error::MainSize,0,main<40?40:60};
  if(extra>15) return {Error::ExtraSize,0,15};
  if(side>15) return {Error::SideSize,0,15};
  return {};
}
inline std::string describe(Result result) {
  switch(result.error) {
    case Error::MainSize: return "Main Deck must contain 40 to 60 cards";
    case Error::ExtraSize: return "Extra Deck must contain 0 to 15 cards";
    case Error::SideSize: return "Side Deck must contain 0 to 15 cards";
    case Error::UnknownCard: return "Unknown card "+std::to_string(result.code);
    case Error::NonDeckCard: return "Card cannot be placed in a TCG deck: "+std::to_string(result.code);
    case Error::MainType: return "Extra Deck monster in Main Deck: "+std::to_string(result.code);
    case Error::ExtraType: return "Invalid Extra Deck card: "+std::to_string(result.code);
    case Error::CardPool: return "Outside Edison card pool: "+std::to_string(result.code);
    case Error::CopyLimit: return "Combined Main/Extra/Side copies exceed "+std::to_string(result.limit)+" for "+std::to_string(result.code);
    default: return {};
  }
}

using Deck = std::array<std::vector<uint32_t>,3>;
struct Card { uint32_t code=0, alias=0, type=0, ot=0; };
inline bool in_edison_pool(uint32_t code) {
  return std::binary_search(data::pool.begin(),data::pool.end(),code);
}
inline int copy_limit(uint32_t name, int rule) {
  if(rule!=1) return 3; // Rule number does not select a modern tournament banlist.
  auto it=std::lower_bound(data::limits.begin(),data::limits.end(),name,
                          [](const auto& item,uint32_t code){return item.first<code;});
  return it!=data::limits.end() && it->first==name?it->second:3;
}
template<class Lookup>
Result validate_copies(const Deck& deck, int rule, Lookup lookup) {
  std::map<uint32_t,int> counts;
  for(const auto& zone:deck) for(uint32_t code:zone) {
    Card card=lookup(code);
    if(!code || card.code!=code) return {Error::UnknownCard,code};
    // Permanent name aliases (including alternate arts) share the copy limit.
    const uint32_t name=card.alias?card.alias:card.code;
    const int limit=copy_limit(name,rule);
    if(++counts[name]>limit) return {Error::CopyLimit,name,limit};
  }
  return {};
}
template<class Lookup>
Result validate(const Deck& deck, int rule, Lookup lookup) {
  auto sizes=validate_sizes(deck[0].size(),deck[1].size(),deck[2].size());
  if(!sizes) return sizes;
  for(size_t zone=0;zone<deck.size();++zone) for(uint32_t code:deck[zone]) {
    const Card card=lookup(code);
    if(!code || card.code!=code) return {Error::UnknownCard,code};
    if(!(card.ot&2) || (card.type&TYPE_TOKEN)) return {Error::NonDeckCard,code};
    const bool extra=card.type&(TYPE_FUSION|TYPE_SYNCHRO|TYPE_XYZ|TYPE_LINK);
    if(zone==0 && extra) return {Error::MainType,code};
    if(zone==1 && (!extra || (rule==1 && !(card.type&(TYPE_FUSION|TYPE_SYNCHRO)))))
      return {Error::ExtraType,code};
    if(rule==1 && !in_edison_pool(code)) return {Error::CardPool,code};
  }
  return validate_copies(deck,rule,lookup);
}
} // namespace ygopro::deck_rules
