#pragma once
#include "harness.h"

inline void historical_goyo_material(certification::Assets& assets, int options,
                                     bool historical, bool earth, bool trace) {
  using namespace certification;
  const uint32_t tuner=earth?psychic_commander:gale;
  const bool allowed=historical || earth;
  TestDuel d(assets,0,0,trace,options);
  d.add({tuner,0,0,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({jerry_beans,0,0,LOCATION_MZONE,1,POS_FACEUP_ATTACK});
  d.add({goyo,0,0,LOCATION_EXTRA,0,POS_FACEDOWN_DEFENSE});
  bool requested=false,tuner_selected=false,sum_selected=false,announced=false,summoned=false;
  std::vector<uint32_t> materials;
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0 && d.phase()==PHASE_MAIN1,"Unexpected idle");
      int index=-1;for(size_t i=0;i<e.idle[1].size();++i) if(e.idle[1][i].code==goyo) index=int(i);
      if(!requested && allowed) {
        require(index>=0,"Goyo must accept the historical/control tuner");
        requested=true;d.respond((index<<16)|1);
      } else {
        require(requested==allowed && summoned==allowed && announced==allowed &&
                tuner_selected==allowed && sum_selected==allowed,"Incomplete Synchro procedure");
        if(allowed) {
          std::sort(materials.begin(),materials.end());std::vector<uint32_t> want{tuner,jerry_beans};
          std::sort(want.begin(),want.end());require(materials==want,"Wrong Synchro materials");
          require(d.count(0,LOCATION_MZONE)==1 && d.code(0,LOCATION_MZONE,0)==goyo &&
                  d.position(0,LOCATION_MZONE,0)==POS_FACEUP_ATTACK &&
                  d.query_value(0,LOCATION_MZONE,0,QUERY_ATTACK)==2800 &&
                  d.count(0,LOCATION_EXTRA)==0 && d.count(0,LOCATION_GRAVE)==2,"Wrong summoned Goyo state");
          std::vector<uint32_t> grave{d.code(0,LOCATION_GRAVE,0),d.code(0,LOCATION_GRAVE,1)};
          std::sort(grave.begin(),grave.end());require(grave==want,"Material identities lost");
        } else require(index<0 && materials.empty() && d.count(0,LOCATION_MZONE)==2 &&
                       d.code(0,LOCATION_MZONE,0)==tuner && d.code(0,LOCATION_MZONE,1)==jerry_beans &&
                       d.count(0,LOCATION_GRAVE)==0 && d.count(0,LOCATION_EXTRA)==1 &&
                       d.code(0,LOCATION_EXTRA,0)==goyo,"Rule 5 must retain ineligible materials and Goyo");
        for(int p=0;p<2;++p) {
          require(d.lp(p)==8000,"Unexpected LP change");
          for(int loc:{LOCATION_HAND,LOCATION_DECK,LOCATION_SZONE,LOCATION_REMOVED})
            require(d.count(p,loc)==0,"Unexpected extra zones");
          if(p==1) for(int loc:{LOCATION_MZONE,LOCATION_EXTRA,LOCATION_GRAVE})
            require(d.count(p,loc)==0,"Unexpected opponent card");
        }
        std::cout<<"Observed: "<<(earth?"EARTH":"WIND")<<" Level 3 Tuner + Level 3 non-Tuner; "
                 <<(allowed?"Goyo Synchro Summon completed":"Rule 5 excludes the summon")<<".\n";
        return true;
      }
    } else if(e.kind==MSG_SELECT_CARD) {
      require(requested && !tuner_selected && e.player==0 && e.minimum==1 && e.maximum==1 &&
              e.cards.size()==1 && e.cards[0].code==tuner &&
              e.cards[0].location==LOCATION_MZONE,"Wrong tuner selection");
      tuner_selected=true;d.respond({1,0});
    } else if(e.kind==MSG_SELECT_SUM) {
      require(requested && tuner_selected && !sum_selected && e.player==0 && e.sum_mode==0 &&
              e.mask==6 && e.minimum<=1 && e.maximum>=1 && e.mandatory.size()==1 &&
              e.mandatory[0].code==tuner && e.mandatory[0].controller==0 &&
              e.mandatory[0].location==LOCATION_MZONE && e.mandatory[0].sum_value==3 &&
              e.cards.size()==1 && e.cards[0].code==jerry_beans && e.cards[0].controller==0 &&
              e.cards[0].location==LOCATION_MZONE && e.cards[0].sum_value==3,
              "Expected Level 3 tuner plus Level 3 non-tuner sum");
      sum_selected=true;d.respond({2,0,0});
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(e.cards.empty() && !e.forced,"Unexpected response");d.respond(-1);
    } else if(e.kind==MSG_MOVE) {
      if(e.from.code==goyo) {
        require(requested && materials.size()==2 && e.from.location==LOCATION_EXTRA &&
                e.to.location==LOCATION_MZONE && e.to.controller==0,"Wrong Goyo summon movement");
      } else {
        require(requested && tuner_selected && (e.from.code==tuner || e.from.code==jerry_beans) &&
                e.from.location==LOCATION_MZONE && e.to.location==LOCATION_GRAVE &&
                (e.reason&(REASON_MATERIAL|REASON_SYNCHRO))==(REASON_MATERIAL|REASON_SYNCHRO),
                "Wrong Synchro material movement");materials.push_back(e.from.code);
      }
    } else if(e.kind==MSG_SELECT_PLACE) {
      require(requested && materials.size()==2 && e.player==0 && e.count==1 && !(e.mask&1),
              "Unexpected Goyo placement");d.respond({0,LOCATION_MZONE,0});
    } else if(e.kind==MSG_SELECT_POSITION) {
      require(requested && e.cards.at(0).code==goyo && (e.mask&POS_FACEUP_ATTACK),
              "Unexpected Goyo position");d.respond(POS_FACEUP_ATTACK);
    } else if(e.kind==MSG_SPSUMMONING) {
      require(requested && !announced && materials.size()==2 && e.cards.at(0).code==goyo,
              "Wrong summon announcement");announced=true;
    } else if(e.kind==MSG_SPSUMMONED) {
      require(announced && !summoned,"Unexpected summon success");summoned=true;
    } else if(e.kind==MSG_CHAINING || e.kind==MSG_DRAW || e.kind==MSG_SELECT_EFFECTYN ||
              e.kind==MSG_SELECT_YESNO || e.kind==MSG_PAY_LPCOST)
      throw std::runtime_error("Unexpected action in Goyo summon procedure");
    return false;
  });
}
