#pragma once
#include "harness.h"

// Removing one negator must not clear another independently applied effect.
inline void layered_negation(certification::Assets& assets, int options, bool chalice_on,
                             bool remove_drain, bool trace) {
  using namespace certification;
  TestDuel d(assets,0,0,trace,options);
  d.add({lonefire,0,0,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({jerry_beans,0,0,LOCATION_MZONE,1,POS_FACEUP_ATTACK});
  d.add({jerry_beans,0,0,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});
  d.add({chalice,0,0,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  d.add({skill_drain,0,0,LOCATION_SZONE,1,POS_FACEDOWN_DEFENSE});
  d.add({mst,0,0,LOCATION_SZONE,2,POS_FACEDOWN_DEFENSE});
  std::vector<uint32_t> sequence;
  if(chalice_on) sequence.push_back(chalice);
  sequence.push_back(skill_drain);
  if(remove_drain) sequence.push_back(mst);
  sequence.push_back(lonefire);
  const bool disabled=chalice_on || !remove_drain;
  size_t completed=0;
  bool requested=false,announced=false,confirmed=false,resolving=false,solved=false,selected=false;
  bool paid=false,drain_destroyed=false,cost=false,recruited=false,negated=false,recruit_selected=false;
  auto actor=[&] {require(completed<sequence.size(),"Extra effect");return sequence[completed];};
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0 && d.phase()==PHASE_MAIN1,"Unexpected idle");
      if(completed==sequence.size()) {
        require(paid && drain_destroyed==remove_drain && cost && negated==disabled &&
                recruited==!disabled && recruit_selected==!disabled,"Missing layered-negation checkpoints");
        require(d.lp(0)==7000 && d.lp(1)==8000 &&
                d.count(0,LOCATION_MZONE)==1+int(!disabled) &&
                d.code(0,LOCATION_MZONE,0)==lonefire &&
                d.query_value(0,LOCATION_MZONE,0,QUERY_ATTACK)==(chalice_on?900u:500u),
                "Wrong retained Lonefire state");
        require(d.count(0,LOCATION_DECK)==int(disabled),"Wrong recruit deck count");
        if(disabled) require(d.code(0,LOCATION_DECK,0)==jerry_beans,"Negated effect removed recruit");
        else require(d.code(0,LOCATION_MZONE,1)==jerry_beans &&
                     d.position(0,LOCATION_MZONE,1)==POS_FACEUP_ATTACK,"Wrong recruited Plant");
        require(d.count(0,LOCATION_GRAVE)==1+int(chalice_on)+2*int(remove_drain),"Wrong Graveyard count");
        int grave=0;
        if(chalice_on) require(d.code(0,LOCATION_GRAVE,grave++)==chalice,"Chalice missing");
        if(remove_drain) {
          require(d.code(0,LOCATION_GRAVE,grave++)==skill_drain,"Destroyed Drain missing");
          require(d.code(0,LOCATION_GRAVE,grave++)==mst,"MST missing");
        }
        require(d.code(0,LOCATION_GRAVE,grave)==jerry_beans,"Tribute cost not retained");
        require(d.count(0,LOCATION_SZONE)==3-int(chalice_on)-2*int(remove_drain),"Wrong S/T count");
        if(!chalice_on) require(d.code(0,LOCATION_SZONE,0)==chalice &&
                                (d.position(0,LOCATION_SZONE,0)&POS_FACEDOWN),"Unused Chalice changed");
        if(!remove_drain) {
          require(d.code(0,LOCATION_SZONE,1)==skill_drain &&
                  (d.position(0,LOCATION_SZONE,1)&POS_FACEUP),"Drain must remain active");
          require(d.code(0,LOCATION_SZONE,2)==mst &&
                  (d.position(0,LOCATION_SZONE,2)&POS_FACEDOWN),"Unused MST changed");
        }
        for(int p=0;p<2;++p) {
          require(d.count(p,LOCATION_HAND)==0 && d.count(p,LOCATION_REMOVED)==0 &&
                  d.count(p,LOCATION_EXTRA)==0,"Unexpected extra zones");
          if(p==1) for(int loc:{LOCATION_DECK,LOCATION_MZONE,LOCATION_SZONE,LOCATION_GRAVE})
            require(d.count(p,loc)==0,"Unexpected opponent card");
        }
        std::cout<<"Observed: Drain "<<(remove_drain?"removed":"active")<<", Chalice "
                 <<(chalice_on?"applied":"unused")<<"; Lonefire paid other-Plant cost; "
                 <<(disabled?"effect negated":"recruit succeeded")<<".\n";
        return true;
      }
      require(!requested,"Returned to idle before chain ended");
      if(actor()==lonefire) {
        require(paid && drain_destroyed==remove_drain,"Negator setup incomplete");
        require(d.query_value(0,LOCATION_MZONE,0,QUERY_ATTACK)==(chalice_on?900u:500u),
                "Chalice setup not observable");
      }
      int index=-1;for(size_t i=0;i<e.idle[5].size();++i) if(e.idle[5][i].code==actor()) index=int(i);
      require(index>=0,"Required activation unavailable");requested=true;d.respond((index<<16)|5);
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(!e.forced,"Unexpected forced choice");
      for(const auto& c:e.cards) require(e.player==0 && (c.code==chalice || c.code==skill_drain || c.code==mst),
                                         "Unexpected response");
      d.respond(-1);
    } else if(e.kind==MSG_CHAINING) {
      require(requested && !announced && e.chain==1 && e.player==0 && e.cards.at(0).code==actor(),
              "Wrong activation");announced=true;
    } else if(e.kind==MSG_PAY_LPCOST) {
      require(actor()==skill_drain && announced && !confirmed && !paid && e.player==0 && e.mask==1000,
              "Wrong Drain cost");paid=true;
    } else if(e.kind==MSG_SELECT_CARD) {
      require(announced && e.player==0 && e.minimum==1 && e.maximum==1,"Unexpected selection");
      uint32_t want;int loc;
      if(resolving) {
        require(actor()==lonefire && !disabled && !recruit_selected,"Unexpected recruit selection");
        recruit_selected=true;want=jerry_beans;loc=LOCATION_DECK;
      } else {
        require(!confirmed && !selected && actor()!=skill_drain,"Repeated target/cost");selected=true;
        want=actor()==chalice?lonefire:actor()==mst?skill_drain:jerry_beans;
        loc=actor()==mst?LOCATION_SZONE:LOCATION_MZONE;
      }
      int index=-1;for(size_t i=0;i<e.cards.size();++i)
        if(e.cards[i].code==want && e.cards[i].controller==0 && e.cards[i].location==loc) index=int(i);
      require(index>=0,"Required target/cost/recruit missing");d.respond({1,static_cast<byte>(index)});
    } else if(e.kind==MSG_MOVE) {
      if(e.from.code==jerry_beans && e.from.location==LOCATION_MZONE) {
        require(actor()==lonefire && announced && !confirmed && selected && !cost &&
                e.to.location==LOCATION_GRAVE &&
                (e.reason&(REASON_COST|REASON_RELEASE))==(REASON_COST|REASON_RELEASE),"Wrong tribute cost");cost=true;
      } else if(e.from.code==jerry_beans) {
        require(actor()==lonefire && resolving && !disabled && recruit_selected &&
                e.from.location==LOCATION_DECK && e.to.location==LOCATION_MZONE,"Wrong recruit movement");
      } else if(e.from.code==skill_drain) {
        require(actor()==mst && remove_drain && resolving && selected && !drain_destroyed &&
                e.from.location==LOCATION_SZONE && e.to.location==LOCATION_GRAVE &&
                (e.reason&(REASON_EFFECT|REASON_DESTROY))==(REASON_EFFECT|REASON_DESTROY),
                "Wrong Drain destruction");drain_destroyed=true;
      } else require((e.from.code==chalice && chalice_on || e.from.code==mst && remove_drain) &&
                     e.from.location==LOCATION_SZONE && e.to.location==LOCATION_GRAVE,"Unexpected movement");
    } else if(e.kind==MSG_SELECT_PLACE) {
      require(actor()==lonefire && resolving && !disabled && e.player==0 && e.count==1 && !(e.mask&2),
              "Unexpected recruit placement");d.respond({0,LOCATION_MZONE,1});
    } else if(e.kind==MSG_SELECT_POSITION) {
      require(actor()==lonefire && resolving && !disabled && e.cards.at(0).code==jerry_beans &&
              (e.mask&POS_FACEUP_ATTACK),"Unexpected recruit position");d.respond(POS_FACEUP_ATTACK);
    } else if(e.kind==MSG_SPSUMMONING) {
      require(actor()==lonefire && resolving && recruit_selected && e.cards.at(0).code==jerry_beans,
              "Unexpected Special Summon");
    } else if(e.kind==MSG_SPSUMMONED) {
      require(actor()==lonefire && resolving && !disabled && !recruited,"Unexpected summon success");recruited=true;
    } else if(e.kind==MSG_CHAINED) {
      require(announced && !confirmed && e.chain==1 && (actor()==skill_drain || selected) &&
              (actor()!=lonefire || cost),"Incomplete link");confirmed=true;
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(confirmed && !resolving && !solved && e.chain==1,"Unexpected resolution");resolving=true;
    } else if(e.kind==MSG_CHAIN_DISABLED) {
      require(actor()==lonefire && disabled && resolving && !negated,"Unexpected negation");negated=true;
    } else if(e.kind==MSG_CHAIN_SOLVED) {
      require(resolving && !solved && e.chain==1,"Wrong solved link");resolving=false;solved=true;
    } else if(e.kind==MSG_CHAIN_END) {
      require(solved && !resolving,"Incomplete chain");++completed;
      requested=announced=confirmed=solved=selected=false;
    } else if(e.kind==MSG_SELECT_EFFECTYN || e.kind==MSG_SELECT_YESNO || e.kind==MSG_DRAW ||
              e.kind==MSG_CHAIN_NEGATED || e.kind==MSG_MISSED_EFFECT)
      throw std::runtime_error("Unexpected layered-negation event");
    return false;
  });
}
