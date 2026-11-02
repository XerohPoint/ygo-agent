#pragma once
#include "harness.h"
inline void set_chain_window(certification::Assets& assets,int options,bool maus,bool trace) {
  using namespace certification;
  const bool historical=(options>>16)==1;
  const uint32_t actor=maus?mausoleum:offering,monster=maus?blue_eyes:jerry_beans;
  TestDuel d(assets,0,0,trace,options);
  d.add({actor,0,0,LOCATION_SZONE,uint8_t(maus?5:0),POS_FACEUP_ATTACK});
  d.add({monster,0,0,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
  d.add({dupe,0,0,LOCATION_HAND,1,POS_FACEDOWN_DEFENSE});
  d.add({judgment,1,1,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  d.add({torrential,1,1,LOCATION_SZONE,1,POS_FACEDOWN_DEFENSE});
  bool started=false,confirmed=false,resolving=false,ended=false,set=false;
  int paid=0;
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      if(!started) {require(e.idle[5].size()==1&&e.idle[5][0].code==actor,"Missing Set effect");started=true;d.respond(5);}
      else {
        require(ended&&set&&paid==(maus?2000:500)&&e.idle[0].empty()==maus&&
                d.code(0,LOCATION_MZONE,0)==monster&&d.position(0,LOCATION_MZONE,0)==POS_FACEDOWN_DEFENSE&&
                d.count(0,LOCATION_HAND)==1&&d.code(0,LOCATION_HAND,0)==dupe&&
                d.lp(0)==8000-paid&&d.lp(1)==8000&&d.count(1,LOCATION_SZONE)==2,"Wrong Set outcome/normal allowance");
        std::cout<<"Observed: "<<(maus?"Mausoleum consumed normal allowance":"Offering retained normal allowance")
                 <<"; Set "<<(historical?"inside resolution":"after chain")<<"; no Judgment/Torrential window.\n";return true;
      }
    } else if(e.kind==MSG_SELECT_CHAIN) {
      for(auto c:e.cards) require(c.code==offering,"Set must not offer summon-response traps");require(!e.forced,"Forced Set response");d.respond(-1);
    } else if(e.kind==MSG_CHAINING) require(e.chain==1&&e.cards.at(0).code==actor,"Wrong Set chain");
    else if(e.kind==MSG_CHAINED) confirmed=true;
    else if(e.kind==MSG_CHAIN_SOLVING) resolving=true;
    else if(e.kind==MSG_CHAIN_SOLVED) resolving=false;
    else if(e.kind==MSG_CHAIN_END) ended=true;
    else if(e.kind==MSG_PAY_LPCOST) {require(!confirmed&&e.player==0&&e.mask==uint32_t(maus?2000:500),"Wrong Set cost");paid+=e.mask;}
    else if(e.kind==MSG_ANNOUNCE_NUMBER) {require(maus&&e.options==std::vector<uint32_t>{2000},"Wrong payment choice");d.respond(0);}
    else if(e.kind==MSG_SELECT_CARD) {
      require(resolving,"Select Set monster at resolution");int pick=-1;
      for(size_t i=0;i<e.cards.size();++i) if(e.cards[i].code==monster) pick=i;
      require(pick>=0,"Missing Set monster");d.respond({1,uint8_t(pick)});
    } else if(e.kind==MSG_SELECT_POSITION) {require(resolving&&(e.mask&POS_FACEDOWN_DEFENSE),"Wrong Set choice");d.respond(POS_FACEDOWN_DEFENSE);}
    else if(e.kind==MSG_SELECT_PLACE) d.respond({0,LOCATION_MZONE,0});
    else if(e.kind==MSG_SET) {require(resolving==historical&&ended==!historical&&e.cards.at(0).code==monster,"Wrong Set timing");set=true;}
    else if(e.kind==MSG_SUMMONING||e.kind==MSG_SUMMONED||e.kind==MSG_SELECT_YESNO||e.kind==MSG_SELECT_EFFECTYN)
      throw std::runtime_error("Unexpected Set event");
    return false;
  });
}
