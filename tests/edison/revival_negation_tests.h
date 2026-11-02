#pragma once
#include "harness.h"

// Debris -> Lonefire: self tribute, other tribute, or other tribute + Book.
// Call -> Lonefire is the unnegated revival control.
inline void revival_negation(certification::Assets& assets, int options, int mode, bool trace) {
  using namespace certification;
  const bool call=mode==3, self=mode==0 || call, book=mode==2, disabled=mode<2;
  TestDuel d(assets,0,0,trace,options);
  d.add({lonefire,0,0,LOCATION_GRAVE,0,POS_FACEUP_ATTACK});
  d.add({jerry_beans,0,0,LOCATION_MZONE,1,POS_FACEUP_ATTACK});
  d.add({jerry_beans,0,0,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});
  d.add({book_moon,0,0,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  if(call) d.add({call_haunted,0,0,LOCATION_SZONE,1,POS_FACEDOWN_DEFENSE});
  else d.add({debris_dragon,0,0,LOCATION_HAND,0,POS_FACEDOWN_DEFENSE});
  bool setup=false,setup_requested=false,normal_summoned=false,optional=false,revival_target=false,revived=false;
  bool requested=false,cost_selected=false,cost=false,book_chosen=false,book_target=false,facedown=false;
  bool recruit_selected=false,recruited=false,negated=false,finished=false;
  int announced=0,confirmed=0,resolving=0,solved=0;
  auto links=[&] {return setup?1+int(book):1;};
  auto code=[&](int link) {return !setup?(call?call_haunted:debris_dragon):link==1?lonefire:book_moon;};
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0 && d.phase()==PHASE_MAIN1,"Unexpected idle");
      if(!setup) {
        require(!setup_requested,"Returned to idle before revival");
        const auto& list=e.idle[call?5:0];int index=-1;
        for(size_t i=0;i<list.size();++i) if(list[i].code==(call?call_haunted:debris_dragon)) index=int(i);
        require(index>=0,"Revival setup unavailable");setup_requested=true;d.respond((index<<16)|(call?5:0));
      } else if(!requested) {
        require(revived && d.code(0,LOCATION_MZONE,2)==lonefire &&
                d.query_value(0,LOCATION_MZONE,2,QUERY_ATTACK)==500,"Lonefire revival incomplete");
        int index=-1;for(size_t i=0;i<e.idle[5].size();++i) if(e.idle[5][i].code==lonefire) index=int(i);
        require(index>=0,"Negation must permit ignition activation");requested=true;d.respond((index<<16)|5);
      } else {
        require(finished && cost && cost_selected && negated==disabled && facedown==book &&
                recruit_selected==!disabled && recruited==!disabled && normal_summoned==!call,
                "Missing revival-negation checkpoints");
        require(d.count(0,LOCATION_DECK)==int(disabled) &&
                d.count(0,LOCATION_MZONE)==int(!call)+1+int(!disabled) &&
                d.count(0,LOCATION_GRAVE)==1+int(book) && d.count(0,LOCATION_HAND)==0,
                "Wrong final own zones");
        require(d.code(0,LOCATION_GRAVE,0)==(self?lonefire:jerry_beans),"Tribute cost not retained");
        if(book) require(d.code(0,LOCATION_GRAVE,1)==book_moon,"Book not in Graveyard");
        if(!call) require(d.code(0,LOCATION_MZONE,0)==debris_dragon,"Debris missing");
        require(d.code(0,LOCATION_MZONE,self?1:2)==(self?jerry_beans:lonefire),"Wrong retained monster");
        if(!self) require(d.position(0,LOCATION_MZONE,2)==(book?POS_FACEDOWN_DEFENSE:POS_FACEUP_ATTACK),
                          "Wrong Lonefire reset position");
        if(disabled) require(d.code(0,LOCATION_DECK,0)==jerry_beans,"Negated effect removed recruit");
        else require(d.code(0,LOCATION_MZONE,3)==jerry_beans &&
                     d.position(0,LOCATION_MZONE,3)==POS_FACEUP_ATTACK,"Wrong recruited Plant");
        require(d.count(0,LOCATION_SZONE)==1+int(call)-int(book),"Wrong S/T count");
        if(!book) require(d.code(0,LOCATION_SZONE,0)==book_moon &&
                          (d.position(0,LOCATION_SZONE,0)&POS_FACEDOWN),"Unused Book changed");
        if(call) require(d.code(0,LOCATION_SZONE,1)==call_haunted &&
                         (d.position(0,LOCATION_SZONE,1)&POS_FACEUP),"Call must remain after tribute");
        for(int p=0;p<2;++p) {
          require(d.lp(p)==8000 && d.count(p,LOCATION_REMOVED)==0 && d.count(p,LOCATION_EXTRA)==0,"Unexpected state");
          if(p==1) for(int loc:{LOCATION_HAND,LOCATION_DECK,LOCATION_MZONE,LOCATION_SZONE,LOCATION_GRAVE})
            require(d.count(p,loc)==0,"Unexpected opponent cards");
        }
        std::cout<<"Observed: "<<(call?"Call revival":book?"Debris revival then Book reset":"Debris revival")
                 <<"; Lonefire tributed "<<(self?"itself":"another Plant")<<"; "
                 <<(disabled?"effect negated, cost retained":"effect recruited normally")<<".\n";
        return true;
      }
    } else if(e.kind==MSG_SUMMONED) {
      require(!call && !setup && setup_requested && !normal_summoned,"Unexpected Normal Summon");normal_summoned=true;
    } else if(e.kind==MSG_SELECT_EFFECTYN) {
      require(!call && !setup && normal_summoned && !optional && e.cards.at(0).code==debris_dragon,
              "Unexpected optional revival");optional=true;d.respond(1);
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(!e.forced,"Unexpected forced choice");int pick=-1;
      for(size_t i=0;i<e.cards.size();++i) {
        const auto c=e.cards[i].code;
        if(c==debris_dragon) {
          require(!call && !setup && normal_summoned && !optional,"Repeated revival offer");optional=true;pick=int(i);
        } else if(c==book_moon) {
          if(setup && requested && confirmed==1 && book && !book_chosen) {book_chosen=true;pick=int(i);}
        } else if(c==lonefire) {
          require(setup && revived && !requested && e.player==0,
                  "Unexpected Lonefire ignition-priority offer");
          // Decline this legal Edison response and activate from idle instead.
        } else require(call && !setup_requested && c==call_haunted,"Unexpected chain candidate");
      }
      d.respond(pick);
    } else if(e.kind==MSG_CHAINING) {
      require(announced==confirmed && announced<links() && e.chain==announced+1 &&
              e.player==0 && e.cards.at(0).code==code(e.chain),"Wrong chain formation");++announced;
    } else if(e.kind==MSG_SELECT_CARD) {
      require(e.player==0 && e.minimum==1 && e.maximum==1,"Unexpected selection");
      uint32_t want;int loc;
      if(!setup) {
        require(announced==1 && !confirmed && !revival_target,"Repeated revival target");revival_target=true;
        want=lonefire;loc=LOCATION_GRAVE;
      } else if(resolving) {
        require(resolving==1 && !disabled && !recruit_selected,"Unexpected recruitment");recruit_selected=true;
        want=jerry_beans;loc=LOCATION_DECK;
      } else if(announced==2) {
        require(book && book_chosen && !book_target,"Unexpected Book target");book_target=true;
        want=lonefire;loc=LOCATION_MZONE;
      } else {
        require(announced==1 && !confirmed && !cost_selected,"Repeated cost");cost_selected=true;
        want=self?lonefire:jerry_beans;loc=LOCATION_MZONE;
      }
      int index=-1;for(size_t i=0;i<e.cards.size();++i)
        if(e.cards[i].code==want && e.cards[i].controller==0 && e.cards[i].location==loc) index=int(i);
      require(index>=0,"Required revival/cost/target/recruit missing");d.respond({1,static_cast<byte>(index)});
    } else if(e.kind==MSG_SELECT_PLACE) {
      int zone=!setup?resolving?2:0:3;
      require(e.player==0 && e.count==1 && !(e.mask&(1u<<zone)),"Unexpected placement");d.respond({0,LOCATION_MZONE,static_cast<byte>(zone)});
    } else if(e.kind==MSG_SELECT_POSITION) {
      require(setup && resolving==1 && !disabled && e.cards.at(0).code==jerry_beans &&
              (e.mask&POS_FACEUP_ATTACK),"Unexpected recruit position");d.respond(POS_FACEUP_ATTACK);
    } else if(e.kind==MSG_MOVE) {
      if(e.to.location==LOCATION_GRAVE && e.from.code!=(uint32_t)book_moon) {
        require(setup && announced==1 && !confirmed && cost_selected && !cost &&
                e.from.code==(self?lonefire:jerry_beans) && e.from.location==LOCATION_MZONE &&
                (e.reason&(REASON_COST|REASON_RELEASE))==(REASON_COST|REASON_RELEASE),"Wrong tribute cost");cost=true;
      } else if(e.from.code==book_moon) {
        require(book && setup && e.from.location==LOCATION_SZONE && e.to.location==LOCATION_GRAVE,
                "Wrong Book movement");
      } else if(e.from.code==lonefire) {
        require(!setup && resolving==1 && revival_target && e.from.location==LOCATION_GRAVE &&
                e.to.location==LOCATION_MZONE,"Wrong revival movement");
      } else if(e.from.code==debris_dragon) {
        require(!setup && !call && setup_requested && e.from.location==LOCATION_HAND &&
                e.to.location==LOCATION_MZONE,"Wrong Debris summon movement");
      } else require(setup && !disabled && resolving==1 && recruit_selected && e.from.code==jerry_beans &&
                     e.from.location==LOCATION_DECK && e.to.location==LOCATION_MZONE,"Wrong recruit movement");
    } else if(e.kind==MSG_POS_CHANGE && e.from.code==lonefire) {
      require(book && setup && resolving==2 && book_target && !facedown &&
              e.to.position==POS_FACEDOWN_DEFENSE,"Book must reset Lonefire before CL1");facedown=true;
    } else if(e.kind==MSG_SPSUMMONING) {
      require(resolving==1 && e.cards.at(0).code==(!setup?lonefire:jerry_beans),"Unexpected Special Summon");
    } else if(e.kind==MSG_SPSUMMONED) {
      require(resolving==1,"Unexpected summon success");
      if(!setup) {require(!revived,"Repeated revival");revived=true;}
      else {require(!disabled && recruit_selected && !recruited,"Unexpected recruit");recruited=true;}
    } else if(e.kind==MSG_CHAINED) {
      require(announced==confirmed+1 && e.chain==announced &&
              (!setup?revival_target:e.chain==1?cost:book_target),"Incomplete activation");++confirmed;
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(confirmed==links() && !resolving && e.chain==confirmed-solved,"Wrong resolution order");resolving=e.chain;
    } else if(e.kind==MSG_CHAIN_DISABLED) {
      require(setup && disabled && resolving==1 && !negated,"Unexpected negation");negated=true;
    } else if(e.kind==MSG_CHAIN_SOLVED) {require(resolving==e.chain,"Wrong solved link");resolving=0;++solved;}
    else if(e.kind==MSG_CHAIN_END) {
      require(!resolving && solved==links(),"Incomplete chain");
      if(!setup) {require(revived,"Revival missing");setup=true;announced=confirmed=solved=0;}
      else finished=true;
    } else if(e.kind==MSG_CHAIN_NEGATED || e.kind==MSG_DRAW || e.kind==MSG_PAY_LPCOST ||
              e.kind==MSG_SELECT_YESNO || e.kind==MSG_MISSED_EFFECT)
      throw std::runtime_error("Unexpected revival-negation event");
    return false;
  });
}
