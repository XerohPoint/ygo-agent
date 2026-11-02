#pragma once
#include "harness.h"

// Caius banishes Survivor face-up in Main Phase; only a previously face-up
// Survivor schedules its mandatory return during the End Phase.
inline void delayed_return(certification::Assets& assets,int options,bool faceup,bool trace) {
  using namespace certification;
  TestDuel d(assets,0,0,trace,options);
  d.add({blue_eyes,0,0,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({survivor,0,0,LOCATION_MZONE,1,uint8_t(faceup?POS_FACEUP_ATTACK:POS_FACEDOWN_DEFENSE)});
  d.add({caius,0,0,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
  bool requested=false,tribute_selected=false,tributed=false,normal=false,target=false,banished=false;
  bool setup=false,ending=false,return_chosen=false,returned=false,damage=false;
  int turns=0,announced=0,confirmed=0,resolving=0,solved=0;
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_NEW_TURN) {
      if(++turns==2) {
        require(e.player==1 && ending && setup && damage && banished && returned==faceup &&
                return_chosen==faceup && d.lp(0)==8000 && d.lp(1)==7000,"Missing delayed-return checkpoints");
        require(d.count(0,LOCATION_HAND)==0 && d.count(0,LOCATION_GRAVE)==1 &&
                d.code(0,LOCATION_GRAVE,0)==blue_eyes && d.count(0,LOCATION_MZONE)==1+int(faceup) &&
                d.code(0,LOCATION_MZONE,0)==caius && d.count(0,LOCATION_REMOVED)==int(!faceup),
                "Wrong delayed-return zones");
        require(d.code(0,faceup?LOCATION_MZONE:LOCATION_REMOVED,faceup?1:0)==survivor &&
                d.position(0,faceup?LOCATION_MZONE:LOCATION_REMOVED,faceup?1:0)==(faceup?POS_FACEUP_ATTACK:POS_FACEUP),
                "Wrong Survivor destination/position");
        for(int p=0;p<2;++p) {
          for(int loc:{LOCATION_DECK,LOCATION_SZONE,LOCATION_EXTRA}) require(d.count(p,loc)==0,"Unexpected extra cards");
          if(p==1) for(int loc:{LOCATION_HAND,LOCATION_GRAVE,LOCATION_MZONE,LOCATION_REMOVED})
            require(d.count(p,loc)==0,"Unexpected opponent card");
        }
        std::cout<<"Observed: Survivor was "<<(faceup?"face-up; returned only during End Phase":"face-down; no delayed return")<<".\n";
        return true;
      }
      require(turns==1 && e.player==0,"Unexpected turn");
    } else if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0 && d.phase()==PHASE_MAIN1 && !ending,"Unexpected idle");
      if(!requested) {
        int index=-1;for(size_t i=0;i<e.idle[0].size();++i) if(e.idle[0][i].code==caius) index=int(i);
        require(index>=0,"Caius summon unavailable");requested=true;d.respond(index<<16);
      } else {
        require(setup && banished && !returned && d.count(0,LOCATION_REMOVED)==1 && e.to_end,
                "Survivor returned before End Phase");ending=true;d.respond(7);
      }
    } else if(e.kind==MSG_SELECT_TRIBUTE) {
      require(requested && !tribute_selected && e.player==0 && e.minimum==1 && e.maximum==1,"Wrong tribute prompt");
      int index=-1;for(size_t i=0;i<e.cards.size();++i) if(e.cards[i].code==blue_eyes) index=int(i);
      require(index>=0,"Required tribute absent");tribute_selected=true;d.respond({1,static_cast<byte>(index)});
    } else if(e.kind==MSG_SELECT_PLACE) {
      const int zone=setup?1:0;
      require(e.player==0 && e.count==1 && !(e.mask&(1u<<zone)) &&
              (setup?ending && resolving==1:requested && tributed),"Unexpected placement");
      d.respond({0,LOCATION_MZONE,static_cast<byte>(zone)});
    } else if(e.kind==MSG_SELECT_POSITION) {
      require(e.cards.at(0).code==(setup?survivor:caius) && (e.mask&POS_FACEUP_ATTACK),"Wrong summon position");
      d.respond(POS_FACEUP_ATTACK);
    } else if(e.kind==MSG_SUMMONED) {
      require(!setup && requested && tributed && !normal,"Wrong Normal Summon");normal=true;
    } else if(e.kind==MSG_SELECT_CHAIN) {
      if(e.cards.empty()) {require(!e.forced,"Empty forced choice");d.respond(-1);}
      else {
        require(setup && ending && faceup && d.phase()==PHASE_END && !return_chosen &&
                e.player==0 && e.cards.size()==1 && e.cards[0].code==survivor,"Delayed return offered outside End Phase");
        return_chosen=true;d.respond(0);
      }
    } else if(e.kind==MSG_CHAINING) {
      require(!announced && e.chain==1 && e.player==0 && e.cards.at(0).code==(setup?survivor:caius) &&
              (setup?return_chosen && ending && e.cards[0].location==LOCATION_REMOVED:normal),"Wrong activation");
      announced=1;
    } else if(e.kind==MSG_SELECT_CARD) {
      require(!setup && announced==1 && !confirmed && !target && e.player==0 &&
              e.minimum==1 && e.maximum==1,"Wrong Caius target prompt");
      int index=-1;for(size_t i=0;i<e.cards.size();++i)
        if(e.cards[i].code==survivor && e.cards[i].controller==0 && e.cards[i].location==LOCATION_MZONE) index=int(i);
      require(index>=0,"Survivor target unavailable");target=true;d.respond({1,static_cast<byte>(index)});
    } else if(e.kind==MSG_MOVE) {
      if(e.from.code==blue_eyes) {
        require(!setup && tribute_selected && !tributed && e.from.location==LOCATION_MZONE &&
                e.to.location==LOCATION_GRAVE && (e.reason&REASON_RELEASE),"Wrong tribute");tributed=true;
      } else if(e.from.code==caius) require(!setup && e.from.location==LOCATION_HAND && e.to.location==LOCATION_MZONE,"Wrong Caius movement");
      else if(!setup) {
        require(e.from.code==survivor && resolving==1 && target && !banished &&
                e.from.location==LOCATION_MZONE && e.to.location==LOCATION_REMOVED &&
                e.to.position==POS_FACEUP && (e.reason&REASON_EFFECT),"Wrong banishment");banished=true;
      } else require(faceup && ending && resolving==1 && e.from.code==survivor &&
                     e.from.location==LOCATION_REMOVED && e.to.location==LOCATION_MZONE,"Wrong return movement");
    } else if(e.kind==MSG_DAMAGE) {
      require(!setup && resolving==1 && banished && !damage && e.player==1 && e.mask==1000,"Wrong Caius damage");damage=true;
    } else if(e.kind==MSG_SPSUMMONING) {
      require(setup && ending && resolving==1 && e.cards.at(0).code==survivor,"Unexpected Special Summon");
    } else if(e.kind==MSG_SPSUMMONED) {
      require(setup && faceup && ending && resolving==1 && !returned,"Unexpected return success");returned=true;
    } else if(e.kind==MSG_CHAINED) {
      require(announced==1 && !confirmed && e.chain==1 && (setup || target),"Incomplete link");confirmed=1;
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(confirmed && !resolving && !solved && e.chain==1,"Wrong resolution");resolving=1;
    } else if(e.kind==MSG_CHAIN_SOLVED) {
      require(resolving==e.chain,"Wrong solved link");resolving=0;solved=1;
    } else if(e.kind==MSG_CHAIN_END) {
      require(solved && !resolving,"Incomplete chain");
      if(!setup) {require(banished && damage,"Setup not resolved");setup=true;announced=confirmed=solved=0;}
      else require(returned,"Delayed return failed");
    } else if(e.kind==MSG_SELECT_EFFECTYN || e.kind==MSG_SELECT_YESNO || e.kind==MSG_DRAW ||
              e.kind==MSG_PAY_LPCOST || e.kind==MSG_CHAIN_NEGATED || e.kind==MSG_CHAIN_DISABLED)
      throw std::runtime_error("Unexpected delayed-return event");
    return false;
  });
}
