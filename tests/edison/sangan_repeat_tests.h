#pragma once
#include "harness.h"

// One physical Sangan: destroy, search, revive with Call, then destroy Call.
// This checks reset/re-entry as well as the modern shared once-per-name limit.
inline void sangan_repeat(certification::Assets& assets, int options, bool historical, bool trace) {
  using namespace certification;
  TestDuel d(assets,0,0,trace,options);
  d.add({exiled_force,0,0,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({sangan,0,0,LOCATION_MZONE,1,POS_FACEUP_ATTACK});
  d.add({call_haunted,0,0,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  d.add({mst,0,0,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
  for(int i=0;i<2;++i) d.add({dupe,0,0,LOCATION_DECK,SEQ_DECKTOP,POS_FACEDOWN_DEFENSE});
  std::vector<uint32_t> expected{exiled_force,sangan,call_haunted,mst};
  if(historical) expected.push_back(sangan);
  size_t completed=0;
  bool requested=false,announced=false,confirmed=false,resolving=false,solved=false,target=false;
  bool cost=false,reviving=false,revived=false;
  int destroyed=0,searches=0,selected=0;
  auto actor=[&] { require(completed<expected.size(),"Extra activation after expected chains");return expected[completed]; };
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_NEW_TURN) require(e.player==0 && completed==0,"Unexpected new turn");
    else if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0 && d.phase()==PHASE_MAIN1,"Expected P0 Main Phase");
      if(completed==expected.size()) {
        require(destroyed==2 && cost && revived && searches==(historical?2:1) &&
                selected==searches,"Incomplete Sangan repeat checkpoints");
        require(d.count(0,LOCATION_HAND)==searches && d.count(0,LOCATION_DECK)==2-searches &&
                d.count(0,LOCATION_MZONE)==0 && d.count(0,LOCATION_SZONE)==0 &&
                d.count(0,LOCATION_GRAVE)==4,"Unexpected final zones");
        for(int i=0;i<searches;++i) require(d.code(0,LOCATION_HAND,i)==dupe,"Wrong searched card");
        if(!historical) require(d.code(0,LOCATION_DECK,0)==dupe,"Second search target must stay in Deck");
        std::vector<uint32_t> grave;
        for(int i=0;i<4;++i) grave.push_back(d.code(0,LOCATION_GRAVE,i));
        std::vector<uint32_t> want{exiled_force,sangan,call_haunted,mst};
        std::sort(grave.begin(),grave.end());std::sort(want.begin(),want.end());
        require(grave==want,"Wrong final Graveyard identities");
        for(int p=0;p<2;++p) {
          require(d.lp(p)==8000 && d.count(p,LOCATION_REMOVED)==0 && d.count(p,LOCATION_EXTRA)==0,
                  "Unexpected LP/banished/Extra Deck state");
          if(p==1) for(int loc:{LOCATION_HAND,LOCATION_DECK,LOCATION_MZONE,LOCATION_SZONE,LOCATION_GRAVE})
            require(d.count(p,loc)==0,"Unexpected opposing card");
        }
        std::cout << "Observed: same Sangan destroyed twice after revival; " << searches
                  << " search chain(s) resolved in the same Main Phase.\n";
        return true;
      }
      require(!requested && (completed==0 || completed==2 || completed==3),
              "Returned to idle before mandatory Sangan trigger");
      int index=-1;
      for(size_t i=0;i<e.idle[5].size();++i)
        if(e.idle[5][i].code==actor()) index=int(i);
      require(index>=0,"Required setup activation unavailable");requested=true;d.respond((index<<16)|5);
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(!e.forced,"Unexpected forced choice");
      for(const auto& c:e.cards)
        require(e.player==0 && (c.code==call_haunted || c.code==mst),"Unexpected response effect");
      d.respond(-1);
    } else if(e.kind==MSG_CHAINING) {
      require(!announced && e.chain==1 && e.player==0 && e.cards.at(0).code==actor(),
              "Wrong trigger/activation count or identity");
      if(actor()==sangan) require(destroyed==searches+1,"Sangan triggered without a new destruction");
      else require(requested,"Unexpected unrequested activation");
      announced=true;
    } else if(e.kind==MSG_SELECT_CARD) {
      require(announced && e.player==0 && e.minimum==1 && e.maximum==1,"Unexpected selection");
      uint32_t want;int loc;
      if(actor()==sangan) {
        require(resolving && selected==searches,"Unexpected Sangan search selection");
        want=dupe;loc=LOCATION_DECK;++selected;
      } else {
        require(!confirmed && !target,"Repeated/late target choice");
        want=actor()==mst?call_haunted:sangan;
        loc=actor()==exiled_force?LOCATION_MZONE:actor()==call_haunted?LOCATION_GRAVE:LOCATION_SZONE;
        if(actor()==exiled_force) require(cost,"Exiled must pay cost before targeting");
        target=true;
      }
      int index=-1;
      for(size_t i=0;i<e.cards.size();++i)
        if(e.cards[i].code==want && e.cards[i].controller==0 && e.cards[i].location==loc) index=int(i);
      require(index>=0,"Required target/search missing");d.respond({1,static_cast<byte>(index)});
    } else if(e.kind==MSG_MOVE) {
      if(e.from.code==exiled_force) {
        require(completed==0 && announced && !confirmed && !cost &&
                e.from.location==LOCATION_MZONE && e.to.location==LOCATION_GRAVE &&
                (e.reason&(REASON_COST|REASON_RELEASE))==(REASON_COST|REASON_RELEASE),"Wrong Exiled cost");
        cost=true;
      } else if(e.from.code==sangan && e.to.location==LOCATION_GRAVE) {
        require(e.from.location==LOCATION_MZONE && (e.reason&(REASON_EFFECT|REASON_DESTROY))==
                (REASON_EFFECT|REASON_DESTROY) && !(e.reason&REASON_COST) &&
                (destroyed==0?completed==0 && resolving:destroyed==1 && completed==3 && revived),
                "Sangan must be destroyed before each trigger");++destroyed;
      } else if(e.from.code==sangan && e.to.location==LOCATION_MZONE) {
        require(completed==2 && resolving && target && destroyed==1 && searches==1 &&
                e.from.location==LOCATION_GRAVE && !revived,"Unexpected Sangan revival movement");
      } else if(e.from.code==dupe) {
        require(actor()==sangan && resolving && selected==searches+1 &&
                e.from.location==LOCATION_DECK && e.to.location==LOCATION_HAND,"Unexpected search movement");
        ++searches;
      } else if(e.from.code==call_haunted) {
        require(completed==3 && resolving && target && e.from.location==LOCATION_SZONE &&
                e.to.location==LOCATION_GRAVE && (e.reason&REASON_DESTROY),"Call must be destroyed by MST");
      } else {
        require(e.from.code==mst && completed==3 &&
                ((e.from.location==LOCATION_HAND && e.to.location==LOCATION_SZONE) ||
                 (e.from.location==LOCATION_SZONE && e.to.location==LOCATION_GRAVE)),
                "Unexpected movement");
      }
    } else if(e.kind==MSG_SELECT_PLACE) {
      require(e.player==0 && e.count==1,"Unexpected place prompt");
      if(completed==2) {
        require(resolving && target && !(e.mask&1u),"Expected Sangan revival zone");
        d.respond({0,LOCATION_MZONE,0});
      } else {
        require(completed==3 && requested && !(e.mask&(1u<<9)),"Expected MST S/T zone");
        d.respond({0,LOCATION_SZONE,1});
      }
    } else if(e.kind==MSG_SELECT_POSITION) {
      require(completed==2 && resolving && e.player==0 && e.cards.at(0).code==sangan &&
              (e.mask&POS_FACEUP_ATTACK),"Unexpected revival position");d.respond(POS_FACEUP_ATTACK);
    } else if(e.kind==MSG_SPSUMMONING) {
      require(completed==2 && resolving && !reviving && e.cards.at(0).code==sangan &&
              e.cards[0].controller==0 && e.cards[0].sequence==0,"Unexpected revival");reviving=true;
    } else if(e.kind==MSG_SPSUMMONED) {
      require(reviving && !revived && completed==2 && resolving,"Unexpected revival success");revived=true;
    } else if(e.kind==MSG_CHAINED) {
      require(announced && !confirmed && e.chain==1 && (actor()==sangan || target),
              "Incomplete confirmed link");confirmed=true;
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(confirmed && !resolving && !solved && e.chain==1,"Unexpected resolution");resolving=true;
    } else if(e.kind==MSG_CHAIN_SOLVED) {
      require(resolving && !solved && e.chain==1,"Unexpected solved link");resolving=false;solved=true;
    } else if(e.kind==MSG_CHAIN_END) {
      require(solved && !resolving,"Incomplete chain");
      ++completed;requested=announced=confirmed=solved=target=false;
    } else if(e.kind==MSG_SELECT_EFFECTYN || e.kind==MSG_SELECT_YESNO || e.kind==MSG_SELECT_TRIBUTE ||
              e.kind==MSG_SUMMONING || e.kind==MSG_DRAW || e.kind==MSG_PAY_LPCOST ||
              e.kind==MSG_CHAIN_DISABLED || e.kind==MSG_CHAIN_NEGATED)
      throw std::runtime_error("Unexpected action in repeated Sangan fixture");
    return false;
  });
}
