#pragma once
#include "harness.h"
inline void future_fusion_locked(certification::Assets& assets,int options,bool historical,bool trace) {
  using namespace certification;
  TestDuel d(assets,0,0,trace,options);
  d.add({future_fusion,0,0,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  d.add({blue_eyes_ultimate,0,0,LOCATION_EXTRA,0,POS_FACEDOWN_DEFENSE});
  for(int i=0;i<3;++i) d.add({blue_eyes,0,0,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});
  d.add({vanity_fiend,1,1,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0 && d.phase()==PHASE_MAIN1,"Wrong idle");
      bool offered=false;for(auto& c:e.idle[5]) {require(c.code==future_fusion,"Wrong action");offered=true;}
      require(offered==!historical,"Historical Future Fusion cannot activate while Special Summoning is forbidden");
      require(d.count(0,LOCATION_DECK)==3 && d.count(0,LOCATION_GRAVE)==0 &&
              d.position(0,LOCATION_SZONE,0)==POS_FACEDOWN_DEFENSE,"Availability check changed state");
      std::cout<<"Observed: Future Fusion "<<(offered?"offered":"unavailable")<<" under Vanity's Fiend.\n";return true;
    } else if(e.kind==MSG_SELECT_CHAIN) {require(e.cards.empty() && !e.forced,"Unexpected chain");d.respond(-1);}
    else if(e.kind==MSG_CHAINING || e.kind==MSG_MOVE) throw std::runtime_error("Unexpected activation/movement");
    return false;
  });
}
