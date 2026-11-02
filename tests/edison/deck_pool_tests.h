#pragma once
#include "deck_copy_tests.h"

inline ygopro::deck_rules::Deck legal_deck_base(DeckCatalog& catalog) {
  using namespace ygopro::deck_rules;
  Deck deck;
  SQLite::Statement q(catalog.db,"SELECT id FROM datas WHERE type=17 AND alias=0 AND (ot&2)!=0 ORDER BY id");
  while(q.executeStep() && deck[0].size()<40) {
    uint32_t code=q.getColumn(0).getUInt();
    if(in_edison_pool(code) && copy_limit(code,1)==3) deck[0].push_back(code);
  }
  certification::require(deck[0].size()==40,"Missing legal base deck identities");
  return deck;
}
inline void deck_pool_case(certification::Assets& assets, int rule, int variant) {
  using namespace certification;
  using namespace ygopro::deck_rules;
  struct Case { uint32_t code; int zone; Error edison,modern; };
  const std::array<Case,18> cases{{
    {blue_eyes,0,Error::None,Error::None},{89631140,0,Error::None,Error::None},
    {81480460,0,Error::None,Error::None},{10000000,0,Error::None,Error::None},
    {47297616,0,Error::None,Error::None},{295517,0,Error::None,Error::None},
    {89258906,0,Error::CardPool,Error::None},{97268402,0,Error::CardPool,Error::None},
    {23434538,2,Error::CardPool,Error::None},{2819435,0,Error::CardPool,Error::None},
    {52687916,1,Error::CardPool,Error::None},{22061412,1,Error::CardPool,Error::None},
    {2956282,1,Error::CardPool,Error::None},{73915052,0,Error::NonDeckCard,Error::NonDeckCard},
    {0,0,Error::UnknownCard,Error::UnknownCard},{111000561,0,Error::UnknownCard,Error::UnknownCard},
    {55144522,0,Error::CopyLimit,Error::None},{5405695,0,Error::NonDeckCard,Error::NonDeckCard}
  }};
  DeckCatalog catalog(assets);auto deck=legal_deck_base(catalog);
  const auto c=cases.at(variant);
  if(c.zone==0) deck[0][0]=c.code;else deck[c.zone].push_back(c.code);
  auto result=validate(deck,rule,[&](uint32_t code){return catalog(code);});
  require(result.error==(rule==1?c.edison:c.modern),"Wrong historical card-pool result for "+std::to_string(c.code)+": "+describe(result));
  if(!result) require(result.code==c.code,"Wrong rejected card identity");
  std::cout<<"Observed: code "<<c.code<<" in zone "<<c.zone<<": "<<(result?"accepted":describe(result))<<".\n";
}
inline void extra_deck_case(certification::Assets& assets, int rule, int variant) {
  using namespace certification;
  using namespace ygopro::deck_rules;
  DeckCatalog catalog(assets);auto deck=legal_deck_base(catalog);
  Error expected=Error::None;
  switch(variant) {
    case 0: deck[1]={23995346,goyo};break; // Fusion + Synchro
    case 1: deck[1]={blue_eyes};expected=Error::ExtraType;break;
    case 2: deck[1]={5405694};expected=Error::ExtraType;break; // Ritual
    case 3: deck[1]={brain};expected=Error::ExtraType;break;
    case 4: deck[1]={skill_drain};expected=Error::ExtraType;break;
    case 5: deck[1]={84013237};expected=rule==1?Error::ExtraType:Error::None;break;
    case 6: deck[1]={98978921};expected=rule==1?Error::ExtraType:Error::None;break;
    case 7: deck[1]={16178681};expected=Error::ExtraType;break; // Pendulum starts in Main
    case 8: deck[0][0]=goyo;expected=Error::MainType;break;
    case 9: deck[0][0]=23995346;expected=Error::MainType;break;
    case 10: deck[0][0]=84013237;expected=Error::MainType;break;
    case 11: deck[0][0]=98978921;expected=Error::MainType;break;
    case 12: deck[2]={23995346,goyo,5405694};break; // All can be side-decked
    case 13: deck[1]={73915052};expected=Error::NonDeckCard;break;
    case 14: deck[0][0]=16178681;expected=rule==1?Error::CardPool:Error::None;break;
    case 15: deck[1]={goyo};deck[2]={goyo};expected=rule==1?Error::CopyLimit:Error::None;break;
    default: throw std::runtime_error("Invalid Extra Deck case");
  }
  auto result=validate(deck,rule,[&](uint32_t code){return catalog(code);});
  require(result.error==expected,"Wrong Extra Deck legality result: "+describe(result));
  std::cout<<"Observed: "<<(result?"deck zones accepted":describe(result))<<".\n";
}
