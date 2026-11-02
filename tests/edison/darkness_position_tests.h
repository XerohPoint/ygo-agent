#pragma once
#include "harness.h"

inline void darkness_position(certification::Assets& assets,int options,bool historical,bool attack,bool trace) {
  using namespace certification;
  TestDuel d(assets,0,0,trace,options);
  d.add({blue_eyes,1,1,LOCATION_MZONE,0,uint8_t(attack?POS_FACEUP_ATTACK:POS_FACEUP_DEFENSE)});
  d.add({darkness_approaches,1,1,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  for(int i=0;i<2;++i) d.add({jerry_beans,1,1,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
  int stage=0,costs=0,links=0;bool resolving=false,target=false,changed=false,flip=false;
  const int down=historical&&attack?POS_FACEDOWN_ATTACK:POS_FACEDOWN_DEFENSE;
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      if(e.player==0) {require(stage==0,"Unexpected later turn");d.respond(7);}
      else if(stage==0) {
        require(d.phase()==PHASE_MAIN1,"Expected Main Phase");
        int pick=-1;for(size_t i=0;i<e.idle[5].size();++i) if(e.idle[5][i].code==darkness_approaches) pick=i;
        require(pick>=0,"Darkness Approaches unavailable");stage=1;d.respond((pick<<16)|5);
      } else if(stage==2) {
        require(changed && costs==2 && d.position(1,LOCATION_MZONE,0)==down,
                "Historical Darkness Approaches must preserve Attack/Defense position");
        require(e.to_battle,"Battle Phase unavailable");stage=3;d.respond(6);
      } else if(stage==4) {
        require(d.phase()==PHASE_MAIN2 && e.idle[2].size()==1 && e.idle[2][0].code==blue_eyes,
                "Face-down monster must permit Flip Summon");stage=5;d.respond(2);
      } else {
        require(stage==5 && flip && d.position(1,LOCATION_MZONE,0)==POS_FACEUP_ATTACK,"Flip Summon failed");
        require(d.count(1,LOCATION_GRAVE)==3 && d.count(1,LOCATION_HAND)==0 &&
                d.count(1,LOCATION_SZONE)==0 && d.count(1,LOCATION_MZONE)==1,"Wrong final zones");
        require(d.lp(0)==8000 && d.lp(1)==8000,"Unexpected damage/cost");
        std::cout<<"Observed: face-down position "<<down<<"; no attack; Flip Summon succeeds.\n";return true;
      }
    } else if(e.kind==MSG_SELECT_BATTLECMD) {
      require(stage==3 && e.player==1 && e.idle[0].empty(),"Face-down monster offered an attack");
      stage=4;d.respond(2);
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(e.cards.empty() && !e.forced,"Unexpected response");d.respond(-1);
    } else if(e.kind==MSG_SELECT_POSITION) {
      require(stage==5 && historical && attack && e.player==1 && e.cards.at(0).code==blue_eyes &&
              e.mask==(POS_FACEUP_ATTACK|POS_FACEDOWN_DEFENSE),"Wrong deprecated-position choice");
      d.respond(POS_FACEUP_ATTACK);
    } else if(e.kind==MSG_CHAINING) {
      require(stage==1 && !links && e.chain==1 && e.cards.at(0).code==darkness_approaches,"Wrong activation");links=1;
    } else if(e.kind==MSG_SELECT_CARD) {
      require(stage==1 && links==1 && !resolving && e.player==1,"Wrong selection window");
      if(!costs) {
        require(e.minimum==2 && e.maximum==2 && e.cards.size()==2,"Wrong discard cost");
        for(auto& c:e.cards) require(c.code==jerry_beans && c.location==LOCATION_HAND,"Wrong cost candidate");
        d.respond({2,0,1});
      } else {
        require(costs==2 && !target && e.minimum==1 && e.maximum==1 &&
                e.cards.size()==1 && e.cards[0].code==blue_eyes,"Wrong target");target=true;d.respond({1,0});
      }
    } else if(e.kind==MSG_MOVE) {
      if(e.from.code==jerry_beans) {
        require(stage==1 && !resolving && costs<2 && e.from.location==LOCATION_HAND &&
                e.to.location==LOCATION_GRAVE && (e.reason&(REASON_COST|REASON_DISCARD))==(REASON_COST|REASON_DISCARD),
                "Discard must be paid as cost");++costs;
      } else require(e.from.code==darkness_approaches && e.to.location==LOCATION_GRAVE,"Unexpected movement");
    } else if(e.kind==MSG_CHAINED) require(target && costs==2 && e.chain==1,"Incomplete activation");
    else if(e.kind==MSG_CHAIN_SOLVING) {require(target && e.chain==1 && !resolving,"Wrong resolution");resolving=true;}
    else if(e.kind==MSG_POS_CHANGE && e.from.code==blue_eyes) {
      require(resolving && !changed && e.to.position==down,"Wrong historical face-down position");changed=true;
    } else if(e.kind==MSG_CHAIN_SOLVED) {require(resolving && changed && e.chain==1,"Position change missing");resolving=false;}
    else if(e.kind==MSG_CHAIN_END) {require(changed && !resolving && stage==1,"Incomplete chain");stage=2;}
    else if(e.kind==MSG_FLIPSUMMONING) require(stage==5 && e.cards.at(0).code==blue_eyes,"Wrong Flip Summon");
    else if(e.kind==MSG_FLIPSUMMONED) {require(stage==5 && !flip,"Duplicate Flip Summon");flip=true;}
    else if(e.kind==MSG_ATTACK || e.kind==MSG_DAMAGE || e.kind==MSG_CHAIN_NEGATED || e.kind==MSG_CHAIN_DISABLED ||
            e.kind==MSG_SELECT_EFFECTYN || e.kind==MSG_SELECT_YESNO) throw std::runtime_error("Unexpected position test event");
    return false;
  });
}
