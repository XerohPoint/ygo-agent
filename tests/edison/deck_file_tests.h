#pragma once
#include "harness.h"
#include "ygoenv/ygopro/deck_file.h"
#include <sstream>

inline void deck_file_case(int variant) {
  using namespace certification;
  std::string newline=variant==1?"\r\n":"\n";
  std::string input=variant==3?"\xef\xbb\xbf":"";
  input+="#created by certification"+newline+"#main"+newline;
  if(variant==2) input+="\n  \n#comment mentioning extra and side\n";
  for(int i=0;i<40;++i) input+=std::to_string(blue_eyes)+newline;
  input+="#extra"+newline+std::to_string(goyo)+newline+"!side"+newline+std::to_string(sangan)+newline;
  if(variant==4) input+="-1\n";
  if(variant==5) input+="4294967296\n";
  if(variant==6) input+="26202165garbage\n";
  if(variant==7) input+="#extra\n";
  if(variant==8) input+="!side\n";
  if(variant==9) input+="!unknown\n";
  if(variant==10) input+="#main\n";
  if(variant==11) input+="0\n";
  bool failed=false;
  ygopro::deck_rules::Deck deck;
  try {std::istringstream stream(input);deck=ygopro::deck_rules::read_stream(stream);}
  catch(const std::exception&) {failed=true;}
  require(failed==(variant>=4),"Production YDK parser accepted/rejected the wrong input");
  if(!failed) require(deck[0]==std::vector<uint32_t>(40,blue_eyes) &&
                      deck[1]==std::vector<uint32_t>{goyo} && deck[2]==std::vector<uint32_t>{sangan},
                      "YDK parser silently lost or reassigned cards");
  std::cout<<"Observed: "<<(failed?"malformed YDK rejected":"exact Main/Extra/Side contents retained")<<".\n";
}
