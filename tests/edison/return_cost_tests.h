#pragma once
#include "harness.h"

inline void return_cost_negation(certification::Assets& assets, int options, int mode, bool self, bool trace) {
  using namespace certification;
  const bool drain=mode==1, active=mode!=2, disabled=mode==0 || (drain && !self);
  const uint32_t negator=drain?skill_drain:chalice, returned=self?swap_frog:jerry_beans;
  TestDuel d(assets,0,0,trace,options);
  d.add({swap_frog,0,0,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({jerry_beans,0,0,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
  d.add({dupe,0,0,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
  d.add({negator,0,0,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  bool setup=!active,setup_requested=false,setup_target=false,paid=false,requested=false;
  bool cost_choice=false,cost=false,negated=false,finished=false,extra_requested=false;
  bool announced=false,confirmed=false,resolving=false,solved=false;
  int normals=0;uint32_t summoning=0;
  auto choose_normal=[&](const Event& e,uint32_t want) {
    int index=-1;for(size_t i=0;i<e.idle[0].size();++i) if(e.idle[0][i].code==want) index=int(i);
    require(index>=0,"Required Normal Summon unavailable");summoning=want;d.respond(index<<16);
  };
  auto final_state=[&] {
    require(finished && cost && cost_choice && negated==disabled && paid==(active && drain) &&
            normals==(disabled?1:2),"Incomplete return-cost checkpoints");
    require(d.lp(0)==(drain?7000:8000) && d.lp(1)==8000 &&
            d.count(0,LOCATION_MZONE)==(disabled?1:2) && d.count(0,LOCATION_HAND)==(disabled?2:1),
            "Wrong final field/hand/LP state");
    require(d.code(0,LOCATION_MZONE,self?1:0)==(self?jerry_beans:swap_frog),"Wrong retained monster");
    std::vector<uint32_t> hand,want{returned};
    if(disabled) want.push_back(dupe);
    for(int i=0;i<d.count(0,LOCATION_HAND);++i) hand.push_back(d.code(0,LOCATION_HAND,i));
    std::sort(hand.begin(),hand.end());std::sort(want.begin(),want.end());
    require(hand==want,"Return cost must remain in hand even if effect is negated");
    if(!disabled) require(d.code(0,LOCATION_MZONE,2)==dupe,"Extra Normal Summon not completed");
    require(d.count(0,LOCATION_GRAVE)==int(mode==0) && d.count(0,LOCATION_SZONE)==int(mode!=0),
            "Wrong negator destination");
    if(mode==0) require(d.code(0,LOCATION_GRAVE,0)==chalice,"Chalice missing");
    else require(d.code(0,LOCATION_SZONE,0)==negator &&
                 (drain?bool(d.position(0,LOCATION_SZONE,0)&POS_FACEUP):
                        bool(d.position(0,LOCATION_SZONE,0)&POS_FACEDOWN)),"Negator state changed");
    for(int p=0;p<2;++p) {
      require(d.count(p,LOCATION_DECK)==0 && d.count(p,LOCATION_EXTRA)==0 &&
              d.count(p,LOCATION_REMOVED)==0,"Unexpected other zones");
      if(p==1) for(int loc:{LOCATION_HAND,LOCATION_GRAVE,LOCATION_MZONE,LOCATION_SZONE})
        require(d.count(p,loc)==0,"Unexpected opponent cards");
    }
    std::cout<<"Observed: Swap returned "<<(self?"itself":"another monster")<<" as cost; "
             <<(disabled?"effect negated, no additional Normal Summon":"extra Frog Normal Summon completed")
             <<"; return cost retained.\n";
  };
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0 && d.phase()==PHASE_MAIN1,"Unexpected idle");
      if(normals==0) {
        require(!summoning,"Normal Summon incomplete");choose_normal(e,jerry_beans);
      } else if(!setup || !requested) {
        require(!setup?!setup_requested:!finished,"Returned to idle before chain completed");
        uint32_t want=setup?swap_frog:negator;
        if(setup) require(d.query_value(0,LOCATION_MZONE,0,QUERY_ATTACK)==(mode==0?1400u:1000u),
                          "Negator must apply before activation");
        int index=-1;for(size_t i=0;i<e.idle[5].size();++i)
          if(e.idle[5][i].code==want && e.idle[5][i].location==(setup?LOCATION_MZONE:LOCATION_SZONE)) index=int(i);
        require(index>=0,"Required activation missing");
        if(setup) requested=true;else setup_requested=true;d.respond((index<<16)|5);
      } else {
        require(finished,"Returned to idle before Swap resolved");
        if(disabled) {
          require(e.idle[0].empty(),"Negated Swap must not grant an additional Normal Summon");
          final_state();return true;
        }
        if(!extra_requested) {
          require(e.idle[0].size()==1 && e.idle[0][0].code==dupe,
                  "Only the eligible Frog may use the extra summon; ordinary summon already spent");
          extra_requested=true;choose_normal(e,dupe);
        } else {final_state();return true;}
      }
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(!e.forced,"Unexpected forced choice");
      for(const auto& c:e.cards) require(e.player==0 && (c.code==negator || c.code==swap_frog),
                                         "Unexpected response");d.respond(-1);
    } else if(e.kind==MSG_SELECT_PLACE) {
      require(summoning && e.player==0 && e.count==1,"Unexpected Normal Summon placement");
      int zone=summoning==jerry_beans?1:2;
      require(!(e.mask&(1u<<zone)),"Required zone unavailable");
      d.respond({0,LOCATION_MZONE,static_cast<byte>(zone)});
    } else if(e.kind==MSG_SUMMONING) {
      require(summoning && e.cards.at(0).code==summoning && e.cards[0].controller==0,"Unexpected Normal Summon");
    } else if(e.kind==MSG_SUMMONED) {
      require(summoning && normals==(summoning==jerry_beans?0:1),"Unexpected summon success");++normals;summoning=0;
    } else if(e.kind==MSG_SELECT_YESNO) {
      require(extra_requested && summoning==dupe && !disabled,"Unexpected yes/no prompt");d.respond(1);
    } else if(e.kind==MSG_SELECT_OPTION) {
      require(extra_requested && summoning==dupe && e.player==0 && e.options.size()==1 &&
              e.options[0]==swap_frog*16,"Unexpected extra-summon option");d.respond(0);
    } else if(e.kind==MSG_CHAINING) {
      require(!announced && e.player==0 && e.chain==1 &&
              e.cards.at(0).code==(setup?swap_frog:negator),"Unexpected activation");announced=true;
    } else if(e.kind==MSG_SELECT_CARD) {
      require(announced && !confirmed && e.player==0 && e.minimum==1 && e.maximum==1,"Unexpected card choice");
      uint32_t want;
      if(!setup) {require(!drain && !setup_target,"Unexpected setup target");setup_target=true;want=swap_frog;}
      else {require(!cost_choice && !cost,"Repeated return cost");cost_choice=true;want=returned;}
      int index=-1;for(size_t i=0;i<e.cards.size();++i)
        if(e.cards[i].code==want && e.cards[i].controller==0 && e.cards[i].location==LOCATION_MZONE) index=int(i);
      require(index>=0,"Required target or return-cost monster absent");d.respond({1,static_cast<byte>(index)});
    } else if(e.kind==MSG_PAY_LPCOST) {
      require(!setup && drain && !paid && e.player==0 && e.mask==1000,"Unexpected LP cost");paid=true;
    } else if(e.kind==MSG_MOVE) {
      if(e.to.location==LOCATION_HAND) {
        require(setup && announced && !confirmed && cost_choice && !cost && e.from.code==returned &&
                e.from.location==LOCATION_MZONE && (e.reason&REASON_COST) && !(e.reason&REASON_EFFECT),
                "Monster must return for cost before confirmation");cost=true;
      } else if(e.to.location==LOCATION_MZONE) {
        require(summoning && e.from.code==summoning && e.from.location==LOCATION_HAND,"Wrong Normal Summon movement");
      } else require(mode==0 && e.from.code==chalice && e.from.location==LOCATION_SZONE &&
                     e.to.location==LOCATION_GRAVE,"Unexpected card movement");
    } else if(e.kind==MSG_CHAINED) {
      require(announced && !confirmed && e.chain==1 && (setup?cost:drain?paid:setup_target),
              "Activation cost/target not complete");confirmed=true;
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(confirmed && !resolving && !solved && e.chain==1,"Wrong resolution");resolving=true;
    } else if(e.kind==MSG_CHAIN_DISABLED) {
      require(setup && disabled && resolving && !negated && e.chain==1,"Unexpected effect negation");negated=true;
    } else if(e.kind==MSG_CHAIN_SOLVED) {
      require(resolving && !solved && e.chain==1,"Wrong solved link");resolving=false;solved=true;
    } else if(e.kind==MSG_CHAIN_END) {
      require(solved && !resolving,"Incomplete chain");
      if(!setup) {setup=true;announced=confirmed=solved=false;}
      else finished=true;
    } else if(e.kind==MSG_SELECT_EFFECTYN || e.kind==MSG_DRAW || e.kind==MSG_SPSUMMONING ||
              e.kind==MSG_CHAIN_NEGATED || e.kind==MSG_MISSED_EFFECT)
      throw std::runtime_error("Unexpected action in return-as-cost fixture");
    return false;
  });
}
