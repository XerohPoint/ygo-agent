#pragma once
#include "harness.h"
#include "ygoenv/ygopro/deck_validation.h"

inline void deck_size_case(int variant) {
  using namespace certification;
  using namespace ygopro::deck_rules;
  struct Case { size_t main,extra,side; Error error; };
  const std::array<Case,10> cases{{
    {40,0,0,Error::None},{60,15,15,Error::None},{40,1,1,Error::None},
    {39,0,0,Error::MainSize},{61,0,0,Error::MainSize},
    {40,16,0,Error::ExtraSize},{40,0,16,Error::SideSize},
    {0,0,0,Error::MainSize},{40,15,0,Error::None},{40,0,15,Error::None}
  }};
  const auto c=cases.at(variant);
  auto result=validate_sizes(c.main,c.extra,c.side);
  require(result.error==c.error,"Production deck-size validation accepted/rejected the wrong boundary");
  std::cout<<"Observed: Main/Extra/Side "<<c.main<<"/"<<c.extra<<"/"<<c.side<<": "
           <<(result?"accepted":describe(result))<<".\n";
}
