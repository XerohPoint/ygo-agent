#pragma once
#include "harness.h"

// Applied field negation must not follow Sangan's new Graveyard trigger.
// Both negators also exercise Exiled Force leaving as its activation cost.
inline void graveyard_negation(certification::Assets& assets, int options, int mode, bool trace) {
  using namespace certification;
  const bool chalice_on=mode==0 || mode==2, drain_on=mode==1 || mode==2;
  TestDuel d(assets,0,0,trace,options);
  d.add({sangan,0,0,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({exiled_force,0,0,LOCATION_MZONE,1,POS_FACEUP_ATTACK});
  d.add({chalice,0,0,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  d.add({skill_drain,0,0,LOCATION_SZONE,1,POS_FACEDOWN_DEFENSE});
  d.add({dupe,0,0,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});
  std::vector<uint32_t> sequence;
  if(chalice_on) sequence.push_back(chalice);
  if(drain_on) sequence.push_back(skill_drain);
  sequence.push_back(exiled_force);sequence.push_back(sangan);
  size_t completed=0;
  bool requested=false,announced=false,confirmed=false,resolving=false,solved=false,target=false;
  bool cost=false,paid=false,destroyed=false,searched=false,added=false,checked_field=false;
  auto actor=[&] {require(completed<sequence.size(),"Extra effect");return sequence[completed];};
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0 && d.phase()==PHASE_MAIN1,"Unexpected idle");
      if(completed==sequence.size()) {
        require(cost && paid==drain_on && destroyed && searched && added && checked_field,
                "Graveyard negation checkpoints missing");
        require(d.lp(0)==(drain_on?7000:8000) && d.lp(1)==8000 &&
                d.count(0,LOCATION_MZONE)==0 && d.count(0,LOCATION_DECK)==0 &&
                d.count(0,LOCATION_HAND)==1 && d.code(0,LOCATION_HAND,0)==dupe,
                "Sangan must search despite prior field negation");
        require(d.count(0,LOCATION_GRAVE)==2+int(chalice_on) &&
                d.code(0,LOCATION_GRAVE,int(chalice_on))==exiled_force &&
                d.code(0,LOCATION_GRAVE,1+int(chalice_on))==sangan,"Wrong Graveyard identities");
        if(chalice_on) require(d.code(0,LOCATION_GRAVE,0)==chalice,"Chalice missing");
        require(d.count(0,LOCATION_SZONE)==2-int(chalice_on) &&
                d.code(0,LOCATION_SZONE,1)==skill_drain &&
                (drain_on?bool(d.position(0,LOCATION_SZONE,1)&POS_FACEUP):
                          bool(d.position(0,LOCATION_SZONE,1)&POS_FACEDOWN)),"Wrong retained Skill Drain state");
        if(!chalice_on) require(d.code(0,LOCATION_SZONE,0)==chalice &&
                                (d.position(0,LOCATION_SZONE,0)&POS_FACEDOWN),"Unused Chalice changed");
        for(int p=0;p<2;++p) {
          require(d.count(p,LOCATION_REMOVED)==0 && d.count(p,LOCATION_EXTRA)==0,"Unexpected extra zones");
          if(p==1) for(int loc:{LOCATION_MZONE,LOCATION_SZONE,LOCATION_HAND,LOCATION_DECK,LOCATION_GRAVE})
            require(d.count(p,loc)==0,"Unexpected opponent card");
        }
        std::cout<<"Observed: prior "<<(mode==0?"Chalice":mode==1?"Skill Drain":mode==2?"Chalice and Skill Drain":"unused negators")
                 <<"; Exiled paid cost and destroyed Sangan; new Graveyard search resolved.\n";
        return true;
      }
      require(actor()!=sangan && !requested,"Returned to idle before mandatory search");
      if(actor()==exiled_force) {
        require(d.query_value(0,LOCATION_MZONE,0,QUERY_ATTACK)==(chalice_on?1400u:1000u),
                "Chalice must apply before Sangan leaves");
        if(drain_on) require(paid && (d.position(0,LOCATION_SZONE,1)&POS_FACEUP),"Skill Drain setup incomplete");
        checked_field=true;
      }
      int index=-1;for(size_t i=0;i<e.idle[5].size();++i) if(e.idle[5][i].code==actor()) index=int(i);
      require(index>=0,"Required activation unavailable");requested=true;d.respond((index<<16)|5);
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(!e.forced,"Unexpected forced choice");
      for(const auto& c:e.cards) require(e.player==0 && (c.code==chalice || c.code==skill_drain),
                                         "Unexpected response");d.respond(-1);
    } else if(e.kind==MSG_CHAINING) {
      require(!announced && e.chain==1 && e.player==0 && e.cards.at(0).code==actor() &&
              (actor()==sangan?destroyed:requested),"Wrong activation");
      if(actor()==sangan) require(e.cards[0].location==LOCATION_GRAVE,"Sangan must activate in Graveyard");
      announced=true;
    } else if(e.kind==MSG_PAY_LPCOST) {
      require(actor()==skill_drain && announced && !confirmed && !paid && e.player==0 && e.mask==1000,
              "Wrong Skill Drain cost");paid=true;
    } else if(e.kind==MSG_SELECT_CARD) {
      require(announced && e.player==0 && e.minimum==1 && e.maximum==1,"Unexpected selection");
      uint32_t want;int loc;
      if(actor()==sangan) {
        require(resolving && !searched,"Unexpected search selection");searched=true;want=dupe;loc=LOCATION_DECK;
      } else {
        require(!confirmed && !target && (actor()==chalice || (actor()==exiled_force && cost)),
                "Unexpected target choice");target=true;want=sangan;loc=LOCATION_MZONE;
      }
      int index=-1;for(size_t i=0;i<e.cards.size();++i)
        if(e.cards[i].code==want && e.cards[i].controller==0 && e.cards[i].location==loc) index=int(i);
      require(index>=0,"Required target/search missing");d.respond({1,static_cast<byte>(index)});
    } else if(e.kind==MSG_MOVE) {
      if(e.from.code==exiled_force) {
        require(actor()==exiled_force && announced && !confirmed && !cost &&
                e.from.location==LOCATION_MZONE && e.to.location==LOCATION_GRAVE &&
                (e.reason&(REASON_COST|REASON_RELEASE))==(REASON_COST|REASON_RELEASE),"Wrong Exiled cost");cost=true;
      } else if(e.from.code==sangan) {
        require(actor()==exiled_force && resolving && target && !destroyed &&
                e.from.location==LOCATION_MZONE && e.to.location==LOCATION_GRAVE &&
                (e.reason&(REASON_EFFECT|REASON_DESTROY))==(REASON_EFFECT|REASON_DESTROY),
                "Wrong Sangan destruction");destroyed=true;
      } else if(e.from.code==dupe) {
        require(actor()==sangan && resolving && searched && !added &&
                e.from.location==LOCATION_DECK && e.to.location==LOCATION_HAND,"Wrong search outcome");added=true;
      } else require(chalice_on && e.from.code==chalice && e.from.location==LOCATION_SZONE &&
                     e.to.location==LOCATION_GRAVE,"Unexpected movement");
    } else if(e.kind==MSG_CHAINED) {
      require(announced && !confirmed && e.chain==1 &&
              (actor()==sangan || actor()==skill_drain || target),"Incomplete confirmed link");confirmed=true;
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(confirmed && !resolving && !solved && e.chain==1,"Unexpected resolution");resolving=true;
    } else if(e.kind==MSG_CHAIN_SOLVED) {
      require(resolving && !solved && e.chain==1,"Wrong solved link");resolving=false;solved=true;
    } else if(e.kind==MSG_CHAIN_END) {
      require(solved && !resolving,"Incomplete chain");++completed;requested=announced=confirmed=solved=target=false;
    } else if(e.kind==MSG_SELECT_EFFECTYN || e.kind==MSG_SELECT_YESNO || e.kind==MSG_DRAW ||
              e.kind==MSG_CHAIN_NEGATED || e.kind==MSG_CHAIN_DISABLED || e.kind==MSG_SELECT_PLACE)
      throw std::runtime_error("Prior field negation must not disable this Graveyard trigger or paid-cost Exiled effect");
    return false;
  });
}
