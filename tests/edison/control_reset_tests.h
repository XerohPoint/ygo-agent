#pragma once
#include "harness.h"

inline void control_face_down(certification::Assets& assets, int options, int mode, bool trace) {
  using namespace certification;
  const bool book=mode!=0, before=mode==1, take=mode!=1;
  TestDuel d(assets,0,0,trace,options);
  d.add({blue_eyes,1,1,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({brain,0,0,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  d.add({book_moon,0,0,LOCATION_SZONE,1,POS_FACEDOWN_DEFENSE});
  int stage=0,announced=0,confirmed=0,resolving=0,solved=0,turns=0;
  bool requested=false,book_chosen=false,paid=false,brain_target=false,book_target=false;
  bool facedown=false,controlled=false,returned=false,ending=false,return_selected=false;
  auto links=[&] {return before?2:1;};
  auto actor=[&](int link) {return stage==0 && link==1?brain:book_moon;};
  auto check_state=[&](bool final) {
    const int controller=take && !final?0:1;
    require(d.lp(0)==7200 && d.lp(1)==8000 && d.count(controller,LOCATION_MZONE)==1 &&
            d.count(1-controller,LOCATION_MZONE)==0 && d.code(controller,LOCATION_MZONE,0)==blue_eyes &&
            d.position(controller,LOCATION_MZONE,0)==(book?POS_FACEDOWN_DEFENSE:POS_FACEUP_ATTACK),
            "Wrong control/position state");
    require(d.count(0,LOCATION_GRAVE)==1+int(book) && d.count(0,LOCATION_SZONE)==int(!book),
            "Wrong resolved spell zones");
    if(!book) require(d.code(0,LOCATION_SZONE,1)==book_moon &&
                      (d.position(0,LOCATION_SZONE,1)&POS_FACEDOWN),"Unused Book changed");
    std::vector<uint32_t> grave;
    for(int i=0;i<d.count(0,LOCATION_GRAVE);++i) grave.push_back(d.code(0,LOCATION_GRAVE,i));
    std::sort(grave.begin(),grave.end());std::vector<uint32_t> want{brain};
    if(book) want.push_back(book_moon);std::sort(want.begin(),want.end());
    require(grave==want,"Wrong Graveyard identities");
    for(int p=0;p<2;++p) {
      for(int loc:{LOCATION_HAND,LOCATION_DECK,LOCATION_EXTRA,LOCATION_REMOVED})
        require(d.count(p,loc)==0,"Unexpected extra zones");
      if(p==1) require(d.count(p,LOCATION_GRAVE)==0 && d.count(p,LOCATION_SZONE)==0,"Unexpected opponent cards");
    }
  };
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_NEW_TURN) {
      ++turns;
      if(turns==2) {
        require(e.player==1 && ending && returned==take && controlled==take && facedown==book &&
                paid && brain_target && book_target==book && return_selected==(take && !book),"Incomplete control return checkpoints");
        check_state(true);
        std::cout<<"Observed: "<<(before?"Book before resolution prevented control change":
                    book?"Book after resolution preserved End Phase return":"face-up control returned in End Phase")<<".\n";
        return true;
      }
      require(turns==1 && e.player==0,"Unexpected turn");
    } else if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0 && d.phase()==PHASE_MAIN1 && !ending,"Unexpected idle");
      if(stage==(mode==2?2:1)) {
        require(controlled==take && facedown==book,"Control/Book did not resolve");
        check_state(false);require(e.to_end,"End Phase unavailable");ending=true;d.respond(7);
      } else {
        require(!requested,"Repeated activation");
        uint32_t want=stage==0?brain:book_moon;int index=-1;
        for(size_t i=0;i<e.idle[5].size();++i) if(e.idle[5][i].code==want) index=int(i);
        require(index>=0,"Required spell unavailable");requested=true;d.respond((index<<16)|5);
      }
    } else if(e.kind==MSG_SELECT_CHAIN) {
      int index=-1;
      for(size_t i=0;i<e.cards.size();++i) {
        require(e.player==0,"Unexpected response player");
        if(e.cards[i].code==blue_eyes) {
          require(ending && take && d.phase()==PHASE_END && !return_selected,
                  "Unexpected control-return choice");
          return_selected=true;index=int(i);
        } else {
          require(e.cards[i].code==book_moon,"Unexpected response");
          if(before && stage==0 && confirmed==1 && !book_chosen) {book_chosen=true;index=int(i);}
        }
      }
      require(!e.forced || index>=0,"Unanswered mandatory End Phase choice");
      d.respond(index);
    } else if(e.kind==MSG_CHAINING) {
      require(requested && announced==confirmed && announced<links() &&
              e.chain==announced+1 && e.cards.at(0).code==actor(e.chain) && e.player==0,
              "Wrong chain formation");++announced;
    } else if(e.kind==MSG_PAY_LPCOST) {
      require(stage==0 && announced==1 && !confirmed && !paid && e.player==0 && e.mask==800,
              "Wrong Brain cost");paid=true;
    } else if(e.kind==MSG_SELECT_CARD) {
      require(announced && e.player==0 && e.minimum==1 && e.maximum==1 &&
              e.cards.size()==1 && e.cards[0].code==blue_eyes && e.cards[0].location==LOCATION_MZONE &&
              e.cards[0].controller==(stage==0?1:0),"Wrong control/Book target");
      bool& selected=actor(announced)==brain?brain_target:book_target;
      require(!selected,"Repeated target");selected=true;d.respond({1,0});
    } else if(e.kind==MSG_CHAINED) {
      require(announced==confirmed+1 && e.chain==announced &&
              (actor(e.chain)==brain?paid && brain_target:book_target),"Incomplete activation");++confirmed;
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(confirmed==links() && !resolving && e.chain==confirmed-solved,"Wrong resolution order");resolving=e.chain;
    } else if(e.kind==MSG_POS_CHANGE && e.from.code==blue_eyes) {
      require(book && resolving && actor(resolving)==book_moon && !facedown &&
              e.from.code==blue_eyes && e.to.position==POS_FACEDOWN_DEFENSE,"Wrong Book resolution");facedown=true;
    } else if(e.kind==MSG_MOVE) {
      if(e.from.code==blue_eyes) {
        require(take && e.from.location==LOCATION_MZONE && e.to.location==LOCATION_MZONE,
                "Unexpected monster departure");
        if(!ending) {
          require(stage==0 && resolving==1 && !controlled && e.from.controller==1 && e.to.controller==0,
                  "Wrong initial control change");controlled=true;
        } else {
          require(controlled && !returned && d.phase()==PHASE_END &&
                  e.from.controller==0 && e.to.controller==1,"Wrong End Phase return");returned=true;
        }
      } else require((e.from.code==brain || book && e.from.code==book_moon) &&
                     e.from.location==LOCATION_SZONE && e.to.location==LOCATION_GRAVE,"Unexpected movement");
    } else if(e.kind==MSG_SELECT_PLACE) {
      const int player=ending?1:0;
      require(take && (ending || stage==0 && resolving==1) && e.player==player &&
              e.count==1 && !(e.mask&1),"Wrong control placement");
      d.respond({static_cast<byte>(player),LOCATION_MZONE,0});
    } else if(e.kind==MSG_CHAIN_SOLVED) {
      require(resolving==e.chain,"Wrong solved link");resolving=0;++solved;
    } else if(e.kind==MSG_CHAIN_END) {
      require(solved==links() && !resolving,"Incomplete chain");++stage;
      requested=false;announced=confirmed=solved=0;
    } else if(e.kind==MSG_SELECT_EFFECTYN || e.kind==MSG_SELECT_YESNO || e.kind==MSG_DRAW ||
              e.kind==MSG_CHAIN_DISABLED || e.kind==MSG_CHAIN_NEGATED || e.kind==MSG_SPSUMMONED)
      throw std::runtime_error("Unexpected control-reset event");
    return false;
  });
}
