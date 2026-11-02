#pragma once
#include "harness.h"
#include "ygoenv/ygopro/deck_validation.h"

// Metadata-only tests do not preload scripts or create a duel.
struct DeckCatalog {
  SQLite::Database db;
  explicit DeckCatalog(const certification::Assets& assets):db(assets.database_path,SQLite::OPEN_READONLY) {}
  ygopro::deck_rules::Card operator()(uint32_t code) {
    SQLite::Statement q(db,"SELECT id,alias,type,ot FROM datas WHERE id=?");
    q.bind(1,static_cast<int64_t>(code));
    if(!q.executeStep()) return {};
    return {q.getColumn(0).getUInt(),q.getColumn(1).getUInt(),q.getColumn(2).getUInt(),q.getColumn(3).getUInt()};
  }
};

inline void deck_copy_case(certification::Assets& assets, int rule, int variant) {
  using namespace certification;
  using namespace ygopro::deck_rules;
  DeckCatalog catalog(assets);
  Deck deck;
  Error expected=Error::None;
  uint32_t expected_name=0;
  int expected_limit=0;
  const bool historical=rule==1;
  switch(variant) {
    case 0: deck[0]={sangan};break;
    case 1: deck[0]={sangan};deck[2]={sangan};
      if(historical) {expected=Error::CopyLimit;expected_name=sangan;expected_limit=1;}break;
    case 2: deck[0]={lonefire};deck[2]={lonefire};break;
    case 3: deck[0]={lonefire,lonefire};deck[2]={lonefire};
      if(historical) {expected=Error::CopyLimit;expected_name=lonefire;expected_limit=2;}break;
    case 4: deck[0]={55144522};
      if(historical) {expected=Error::CopyLimit;expected_name=55144522;expected_limit=0;}break;
    case 5: deck[1]={goyo};deck[2]={goyo};
      if(historical) {expected=Error::CopyLimit;expected_name=goyo;expected_limit=1;}break;
    case 6: deck[0]={blue_eyes,blue_eyes};deck[2]={blue_eyes};break;
    case 7: deck[0]={blue_eyes,blue_eyes};deck[2]={blue_eyes,89631140};
      expected=Error::CopyLimit;expected_name=blue_eyes;expected_limit=3;break;
    case 8: deck[0]={22702055,295517};deck[2]={22702055};break; // Umi + Legendary Ocean
    case 9: deck[0]={22702055,295517};deck[2]={22702055,295517};
      expected=Error::CopyLimit;expected_name=22702055;expected_limit=3;break;
    case 10: deck[0]={skill_drain,skill_drain};break;
    case 11: deck[0]={skill_drain,skill_drain};deck[2]={skill_drain};
      if(historical) {expected=Error::CopyLimit;expected_name=skill_drain;expected_limit=2;}break;
    default: throw std::runtime_error("Invalid copy fixture");
  }
  auto result=validate_copies(deck,rule,[&](uint32_t code){return catalog(code);});
  require(result.error==expected,"Wrong historical/Rule 5 copy-limit result");
  if(expected!=Error::None) require(result.code==expected_name && result.limit==expected_limit,
                                    "Wrong combined-name copy-limit diagnostic");
  std::cout<<"Observed: "<<(result?"copies accepted":describe(result))<<".\n";
}
