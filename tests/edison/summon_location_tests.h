#pragma once
#include "harness.h"

inline void summon_trigger_location(certification::Assets& assets, int options,
                                    bool historical, bool destroy_call, bool trace) {
  using namespace certification;
  const bool draw=historical || !destroy_call;
  TestDuel d(assets,0,0,trace,options);
  d.add({sacred_crane,1,1,LOCATION_GRAVE,0,POS_FACEUP_ATTACK});
  d.add({blue_eyes,1,1,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});
  d.add({call_haunted,1,1,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  d.add({book_moon,1,1,LOCATION_SZONE,1,POS_FACEDOWN_DEFENSE});
  d.add({mst,0,0,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  bool requested=false,call_chosen=false,mst_target=false,call_target=false,revived=false;
  bool removed_call=false,removed_crane=false,removed_book=false,setup=false,trigger_done=false,drawn=false;
  int announced=0,confirmed=0,resolving=0,solved=0;
  auto links=[&] {return setup?1:2;};
  auto actor=[&](int link) {return setup?sacred_crane:link==1?mst:call_haunted;};
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0 && d.phase()==PHASE_MAIN1,"Unexpected idle");
      if(!requested) {
        int index=-1;for(size_t i=0;i<e.idle[5].size();++i) if(e.idle[5][i].code==mst) index=int(i);
        require(index>=0,"MST unavailable");requested=true;d.respond((index<<16)|5);
      } else {
        require(setup && revived && mst_target && call_target && trigger_done==draw && drawn==draw &&
                removed_call==destroy_call && removed_crane==destroy_call && removed_book==!destroy_call,
                "Incomplete summon-trigger-location checkpoints");
        require(d.count(0,LOCATION_GRAVE)==1 && d.code(0,LOCATION_GRAVE,0)==mst &&
                d.count(1,LOCATION_MZONE)==int(!destroy_call) && d.count(1,LOCATION_SZONE)==1 &&
                d.count(1,LOCATION_GRAVE)==(destroy_call?2:1) &&
                d.count(1,LOCATION_HAND)==int(draw) && d.count(1,LOCATION_DECK)==int(!draw),"Wrong final zones");
        require(d.code(1,draw?LOCATION_HAND:LOCATION_DECK,0)==blue_eyes,"Wrong draw/filler identity");
        if(destroy_call) {
          std::vector<uint32_t> grave{d.code(1,LOCATION_GRAVE,0),d.code(1,LOCATION_GRAVE,1)},want{call_haunted,sacred_crane};
          std::sort(grave.begin(),grave.end());std::sort(want.begin(),want.end());require(grave==want,"Wrong destroyed identities");
          require(d.code(1,LOCATION_SZONE,1)==book_moon &&
                  (d.position(1,LOCATION_SZONE,1)&POS_FACEDOWN),"Unused Book changed");
        } else require(d.code(1,LOCATION_GRAVE,0)==book_moon &&
                       d.code(1,LOCATION_MZONE,0)==sacred_crane &&
                       d.position(1,LOCATION_MZONE,0)==POS_FACEUP_ATTACK &&
                       d.code(1,LOCATION_SZONE,0)==call_haunted &&
                       (d.position(1,LOCATION_SZONE,0)&POS_FACEUP),"Wrong surviving Crane/Call");
        for(int p=0;p<2;++p) {
          require(d.lp(p)==8000 && d.count(p,LOCATION_EXTRA)==0 && d.count(p,LOCATION_REMOVED)==0,"Unexpected extra state");
          if(p==0) for(int loc:{LOCATION_HAND,LOCATION_DECK,LOCATION_MZONE,LOCATION_SZONE})
            require(d.count(p,loc)==0,"Unexpected own card");
        }
        std::cout<<"Observed: Crane "<<(destroy_call?"left the field before its trigger window":"stayed on field")
                 <<"; "<<(draw?"mandatory draw resolved on a new chain":"Rule 5 suppressed the trigger")<<".\n";
        return true;
      }
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(!e.forced,"Unexpected forced choice");int index=-1;
      for(size_t i=0;i<e.cards.size();++i) {
        const auto code=e.cards[i].code;
        require((e.player==0 && code==mst && !requested) ||
                (e.player==1 && (code==call_haunted || code==book_moon)),"Unexpected response");
        if(!setup && confirmed==1 && code==call_haunted && !call_chosen) {call_chosen=true;index=int(i);}
      }
      d.respond(index);
    } else if(e.kind==MSG_CHAINING) {
      require(announced==confirmed && announced<links() && e.chain==announced+1 &&
              e.cards.at(0).code==actor(e.chain) && e.player==(setup || e.chain==2?1:0),"Wrong activation order");
      if(setup) require(draw && revived && e.cards[0].location==(destroy_call?LOCATION_GRAVE:LOCATION_MZONE),
                        "Wrong Crane activation location");
      ++announced;
    } else if(e.kind==MSG_SELECT_CARD) {
      require(!setup && !resolving && e.minimum==1 && e.maximum==1,"Unexpected target choice");
      uint32_t want;int location;
      if(announced==1) {require(e.player==0 && !mst_target,"Repeated MST target");mst_target=true;want=destroy_call?call_haunted:book_moon;location=LOCATION_SZONE;}
      else {require(announced==2 && e.player==1 && !call_target,"Wrong Call target");call_target=true;want=sacred_crane;location=LOCATION_GRAVE;}
      int index=-1;for(size_t i=0;i<e.cards.size();++i)
        if(e.cards[i].code==want && e.cards[i].controller==1 && e.cards[i].location==location) index=int(i);
      require(index>=0,"Required target absent");d.respond({1,static_cast<byte>(index)});
    } else if(e.kind==MSG_CHAINED) {
      require(announced==confirmed+1 && e.chain==announced &&
              (setup || (e.chain==1?mst_target:call_target)),"Incomplete link");++confirmed;
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(confirmed==links() && !resolving && e.chain==confirmed-solved,"Wrong resolution order");resolving=e.chain;
    } else if(e.kind==MSG_SELECT_PLACE) {
      require(!setup && resolving==2 && e.player==1 && e.count==1 && !(e.mask&1),"Wrong revival placement");
      d.respond({1,LOCATION_MZONE,0});
    } else if(e.kind==MSG_SPSUMMONING) {
      require(!setup && resolving==2 && e.cards.at(0).code==sacred_crane,"Wrong revival");
    } else if(e.kind==MSG_SPSUMMONED) {
      require(!setup && resolving==2 && !revived,"Wrong revival success");revived=true;
    } else if(e.kind==MSG_MOVE) {
      if(e.from.code==sacred_crane && e.to.location==LOCATION_MZONE) {
        require(!setup && resolving==2 && e.from.location==LOCATION_GRAVE,"Wrong Crane revival movement");
      } else if(e.from.code==mst) require(e.from.location==LOCATION_SZONE && e.to.location==LOCATION_GRAVE,"Wrong MST movement");
      else {
        require(!setup && resolving==1 && e.to.location==LOCATION_GRAVE &&
                (e.reason&(REASON_DESTROY|REASON_EFFECT))==(REASON_DESTROY|REASON_EFFECT),"Destruction outside MST resolution");
        if(e.from.code==call_haunted) {require(destroy_call && !removed_call,"Unexpected Call destruction");removed_call=true;}
        else if(e.from.code==sacred_crane) {require(destroy_call && removed_call && revived && !removed_crane,"Unexpected Crane destruction");removed_crane=true;}
        else {require(!destroy_call && e.from.code==book_moon && !removed_book,"Unexpected Book destruction");removed_book=true;}
      }
    } else if(e.kind==MSG_DRAW) {
      require(setup && draw && resolving==1 && !drawn && e.player==1 && e.count==1 &&
              (e.cards.at(0).code&0x7fffffffu)==blue_eyes,"Wrong Crane draw");drawn=true;
    } else if(e.kind==MSG_CHAIN_SOLVED) {require(resolving==e.chain,"Wrong solved link");resolving=0;++solved;}
    else if(e.kind==MSG_CHAIN_END) {
      require(solved==links() && !resolving,"Incomplete chain");
      if(!setup) {setup=true;announced=confirmed=solved=0;}
      else {require(draw && drawn && !trigger_done,"Unexpected trigger completion");trigger_done=true;}
    } else if(e.kind==MSG_SELECT_EFFECTYN || e.kind==MSG_SELECT_YESNO || e.kind==MSG_PAY_LPCOST ||
              e.kind==MSG_CHAIN_DISABLED || e.kind==MSG_CHAIN_NEGATED || e.kind==MSG_MISSED_EFFECT)
      throw std::runtime_error("Unexpected summon-trigger event");
    return false;
  });
}
