#pragma once
#include "harness.h"
inline void summon_chain_window(certification::Assets& assets,int options,int mode,bool trace) {
  using namespace certification;
  // 0: ordinary summon; 1: Mausoleum; 2: Offering CL1; 3: Offering CL2.
  const bool historical=(options>>16)==1;
  const uint32_t monster=mode==1?blue_eyes:jerry_beans;
  TestDuel d(assets,0,0,trace,options);
  d.add({monster,0,0,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
  d.add({judgment,1,1,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  d.add({torrential,1,1,LOCATION_SZONE,1,POS_FACEDOWN_DEFENSE});
  if(mode==1) d.add({mausoleum,0,0,LOCATION_SZONE,5,POS_FACEUP_ATTACK});
  if(mode>=2) d.add({offering,0,0,LOCATION_SZONE,0,POS_FACEUP_ATTACK});
  if(mode==3) {
    d.add({mst,0,0,LOCATION_HAND,1,POS_FACEDOWN_DEFENSE});
    d.add({forest,1,1,LOCATION_SZONE,5,POS_FACEUP_ATTACK});
  }
  int chain=0,confirmed=0,resolving=0,ends=0,cost=0,judgment_offers=0,tt=0,summons=0;
  bool started=false,summoning=false;
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      if(!started) {
        started=true;int pick=-1;auto& list=e.idle[mode?5:0];
        const uint32_t actor=mode==1?mausoleum:mode==2?offering:mode==3?mst:monster;
        for(size_t i=0;i<list.size();++i) if(list[i].code==actor) pick=i;
        require(pick>=0,"Missing initial summon/effect");d.respond((pick<<16)|(mode?5:0));
      } else {
        require(summoning&&summons==int(mode!=0)&&ends==(mode==1||mode==2?2:1) &&
                tt==int(mode==1||mode==2) && judgment_offers>=(mode==0?1:0),"Incomplete summon windows");
        require(d.lp(0)==8000-(mode==1?2000:mode>=2?500:0) &&
                d.lp(1)==(mode==0?4000:8000) &&
                d.count(0,LOCATION_MZONE)==int(mode==3) &&
                d.count(1,LOCATION_SZONE)==(mode==0||mode==1||mode==2?1:2),"Wrong final summon/cost state");
        if(mode==1) require(e.idle[0].empty(),"Mausoleum must consume normal summon");
        std::cout<<"Observed: mode "<<mode<<"; successful summons "<<summons<<", Torrential chains "<<tt<<", LP cost "<<cost<<".\n";return true;
      }
    } else if(e.kind==MSG_SELECT_CHAIN) {
      int pick=-1;
      for(size_t i=0;i<e.cards.size();++i) {
        auto c=e.cards[i];
        if(c.code==judgment) {
          require((mode==0&&summoning&&!summons) || (mode==3&&confirmed==1&&!summoning) || (tt&&confirmed==1&&ends==1) || (!historical&&mode>0&&mode<3&&summoning&&!summons&&ends==1),"Judgment offered inside summon resolution");
          ++judgment_offers;if(mode==0) pick=i;
        } else if(c.code==offering) {
          if(mode==3&&confirmed==1&&!summoning) pick=i;
        } else if(c.code==torrential) {
          require((mode==1||mode==2)&&summons==1&&ends==1,"Torrential must see CL1 summon only");pick=i;
        } else require(c.code==mst,"Unexpected chain candidate");
      } require(!e.forced,"Unexpected forced chain");d.respond(pick);
    } else if(e.kind==MSG_CHAINING) {
      ++chain;
      uint32_t want=mode==0?judgment:ends?torrential:mode==1?mausoleum:mode==2?offering:e.chain==1?mst:offering;
      require(e.cards.at(0).code==want,"Wrong chain actor");
      if(want==torrential) ++tt;
    } else if(e.kind==MSG_CHAINED) confirmed=e.chain;
    else if(e.kind==MSG_CHAIN_SOLVING) resolving=e.chain;
    else if(e.kind==MSG_CHAIN_SOLVED) resolving=0;
    else if(e.kind==MSG_CHAIN_END) {++ends;confirmed=resolving=0;}
    else if(e.kind==MSG_PAY_LPCOST) {
      require(e.mask==uint32_t(mode==0?4000:mode==1?2000:500)&&(mode==0?summoning:!summoning),"Wrong summon-effect cost timing");cost+=e.mask;
    } else if(e.kind==MSG_ANNOUNCE_NUMBER) {
      require(mode==1&&!confirmed&&e.options==std::vector<uint32_t>{2000},"Wrong Mausoleum payment choice");d.respond(0);
    } else if(e.kind==MSG_SELECT_CARD) {
      const uint32_t want=resolving?monster:forest;int pick=-1;
      for(size_t i=0;i<e.cards.size();++i) if(e.cards[i].code==want) pick=i;
      require(pick>=0&&e.minimum==1&&e.maximum==1,"Wrong summon/target choice");d.respond({1,uint8_t(pick)});
    } else if(e.kind==MSG_SELECT_POSITION) d.respond(POS_FACEUP_ATTACK);
    else if(e.kind==MSG_SELECT_PLACE) {
      require(e.player==0,"Wrong placement player");
      if(mode==3&&!confirmed&&!summoning) d.respond({0,LOCATION_SZONE,1});
      else d.respond({0,LOCATION_MZONE,0});
    } else if(e.kind==MSG_SUMMONING) {
      require(!summoning&&e.cards.at(0).code==monster&&(mode==0||resolving==(historical?(mode==3?2:1):0)),"Summon must occur during effect resolution");summoning=true;
    } else if(e.kind==MSG_SUMMONED) {
      require(mode&&resolving==(historical?(mode==3?2:1):0),"Summon success outside resolution");++summons;
    } else if(e.kind==MSG_SELECT_EFFECTYN||e.kind==MSG_SELECT_YESNO||e.kind==MSG_CHAIN_NEGATED||e.kind==MSG_CHAIN_DISABLED)
      throw std::runtime_error("Unexpected summon-window event");
    return false;
  });
}
