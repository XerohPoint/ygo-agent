#pragma once
#include "harness.h"

inline void future_fusion_timing(certification::Assets& assets,int options,bool historical,bool trace) {
  using namespace certification;
  TestDuel d(assets,0,0,trace,options);
  d.add({future_fusion,0,0,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  d.add({mst,0,0,LOCATION_SZONE,1,POS_FACEDOWN_DEFENSE});
  d.add({blue_eyes_ultimate,0,0,LOCATION_EXTRA,0,POS_FACEDOWN_DEFENSE});
  for(int i=0;i<3;++i) d.add({blue_eyes,0,0,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});
  int turns=0,chains=0,announced=0,confirmed=0,resolving=0,solved=0,sent=0,fusion_choices=0,material_choices=0;
  bool activated=false,initial_checked=false,first_checked=false,summoned=false,cleanup=false,destroyed=false;
  const int fusion_chains=historical?2:3;
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_NEW_TURN) {require(e.player==turns%2 && turns<5,"Unexpected turn");++turns;}
    else if(e.kind==MSG_SELECT_IDLECMD) {
      if(e.player==1) {require(turns==2 || turns==4,"Wrong opposing turn");d.respond(7);}
      else if(!activated) {
        require(turns==1,"Wrong activation turn");
        int pick=-1;for(size_t i=0;i<e.idle[5].size();++i) if(e.idle[5][i].code==future_fusion) pick=i;
        require(pick>=0,"Future Fusion unavailable");activated=true;d.respond((pick<<16)|5);
      } else if(turns==1) {
        require(chains==1 && sent==(historical?3:0) && !summoned && !initial_checked,
                "Edison Future Fusion must send materials on initial resolution");
        initial_checked=true;d.respond(7);
      } else if(turns==3) {
        require(initial_checked && sent==3 && !summoned && !first_checked && chains==(historical?1:2),
                "First own Standby must not summon yet");first_checked=true;d.respond(7);
      } else if(!cleanup) {
        require(turns==5 && first_checked && sent==3 && summoned && chains==fusion_chains &&
                d.code(0,LOCATION_MZONE,0)==blue_eyes_ultimate,"Second own Standby Fusion Summon missing");
        int pick=-1;for(size_t i=0;i<e.idle[5].size();++i) if(e.idle[5][i].code==mst) pick=i;
        require(pick>=0,"MST cleanup unavailable");cleanup=true;d.respond((pick<<16)|5);
      } else {
        require(destroyed && chains==fusion_chains+1 && fusion_choices==2 && material_choices==3 &&
                d.count(0,LOCATION_MZONE)==0 && d.count(0,LOCATION_EXTRA)==0 &&
                d.count(0,LOCATION_SZONE)==0 && d.count(0,LOCATION_GRAVE)==6 &&
                d.count(0,LOCATION_DECK)==0 && d.lp(0)==8000 && d.lp(1)==8000,"Wrong Future Fusion cleanup");
        std::cout<<"Observed: materials sent on turn "<<(historical?1:3)
                 <<"; Fusion Summon on turn 5; removing Future Fusion destroys the Fusion.\n";return true;
      }
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(!e.forced || (e.player==0 && d.phase()==PHASE_STANDBY),"Wrong forced choice");
      if(e.forced) {
        int pick=-1;
        for(size_t i=0;i<e.cards.size();++i) {
          require(e.cards[i].code==future_fusion || e.cards[i].code==mst,"Wrong phase candidate");
          if(e.cards[i].code==future_fusion) pick=i;
        }
        require(pick>=0,"Missing phase trigger");d.respond(pick);
      } else {
        for(auto& c:e.cards) require(c.code==mst && e.player==0,"Unexpected response");d.respond(-1);
      }
    } else if(e.kind==MSG_CHAINING) {
      require(!announced && e.chain==1 && e.cards.at(0).code==(cleanup?mst:future_fusion),"Wrong activation");
      if(chains>0&&!cleanup) require(d.phase()==PHASE_STANDBY,"Wrong delayed trigger phase");
      announced=1;
    } else if(e.kind==MSG_SELECT_CARD || e.kind==MSG_SELECT_UNSELECT_CARD) {
      require(e.player==0,"Wrong selection player");
      if(cleanup) {
        require(!confirmed && e.cards.size()==1 && e.cards[0].code==future_fusion,"Wrong MST target");d.respond({1,0});
      } else {
        require(resolving==1 && !e.cards.empty(),"Fusion choices must occur at resolution");
        if(e.cards[0].code==blue_eyes_ultimate) {
          require(e.cards.size()==1 && e.minimum==1 && e.maximum==1,"Wrong Fusion choice");++fusion_choices;d.respond({1,0});
        } else {
          require(turns==(historical?1:3) && material_choices<3,"Wrong material window");
          for(auto& c:e.cards) require(c.code==blue_eyes && c.location==LOCATION_DECK,"Wrong material");
          if(e.kind==MSG_SELECT_UNSELECT_CARD) {
            if(e.finishable) d.respond(-1);
            else {++material_choices;d.respond({1,0});}
          } else {
            require(e.minimum==e.maximum && e.minimum>=1 && e.minimum<=3,"Wrong material count");
            material_choices+=e.minimum;
            if(e.minimum==1) d.respond({1,0});
            else if(e.minimum==2) d.respond({2,0,1});
            else d.respond({3,0,1,2});
          }
        }
      }
    } else if(e.kind==MSG_SHUFFLE_EXTRA) {
      require(resolving==1 && sent==3 && e.player==0 && e.count==1 &&
              e.cards.at(0).code==blue_eyes_ultimate,"Wrong Extra Deck shuffle");
    } else if(e.kind==MSG_MOVE) {
      if(e.from.code==blue_eyes) {
        require(resolving==1 && turns==(historical?1:3) && sent<3 && e.from.location==LOCATION_DECK &&
                e.to.location==LOCATION_GRAVE && (e.reason&REASON_EFFECT) && !(e.reason&REASON_COST),
                "Wrong material send timing/reason");++sent;
      } else if(e.from.code==blue_eyes_ultimate) {
        if(e.from.location==LOCATION_EXTRA) require(turns==5 && resolving==1 && !cleanup && e.to.location==LOCATION_MZONE,"Wrong delayed summon");
        else {require(cleanup && e.from.location==LOCATION_MZONE && e.to.location==LOCATION_GRAVE &&
                      (e.reason&REASON_DESTROY),"Wrong linked destruction");destroyed=true;}
      } else require(cleanup && (e.from.code==future_fusion || e.from.code==mst) &&
                     e.to.location==LOCATION_GRAVE,"Unexpected movement");
    } else if(e.kind==MSG_SELECT_PLACE) {
      require(turns==5 && resolving==1 && !cleanup && e.player==0 && !(e.mask&1),"Wrong Fusion placement");d.respond({0,LOCATION_MZONE,0});
    } else if(e.kind==MSG_SELECT_POSITION) {
      require(turns==5 && resolving==1 && e.cards.at(0).code==blue_eyes_ultimate && (e.mask&POS_FACEUP_ATTACK),"Wrong Fusion position");
      d.respond(POS_FACEUP_ATTACK);
    } else if(e.kind==MSG_SPSUMMONED) {require(turns==5 && resolving==1 && !summoned,"Wrong summon success");summoned=true;}
    else if(e.kind==MSG_CHAINED) {require(announced==1 && !confirmed && e.chain==1,"Wrong confirmation");confirmed=1;}
    else if(e.kind==MSG_CHAIN_SOLVING) {require(confirmed==1 && !resolving && e.chain==1,"Wrong resolution");resolving=1;}
    else if(e.kind==MSG_CHAIN_SOLVED) {require(resolving==1 && e.chain==1,"Wrong solved link");resolving=0;solved=1;}
    else if(e.kind==MSG_CHAIN_END) {require(solved==1 && !resolving,"Incomplete chain");++chains;announced=confirmed=solved=0;}
    else if(e.kind==MSG_CHAIN_NEGATED || e.kind==MSG_CHAIN_DISABLED || e.kind==MSG_SELECT_EFFECTYN ||
            e.kind==MSG_SELECT_YESNO || e.kind==MSG_DRAW) throw std::runtime_error("Unexpected Future Fusion event");
    return false;
  });
}
