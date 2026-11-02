#pragma once
#include "harness.h"

inline void prisma_resolution(certification::Assets& assets,int options,bool historical,bool set_before,bool trace) {
  using namespace certification;
  TestDuel d(assets,0,0,trace,options);
  d.add({prisma,0,0,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({blue_eyes,0,0,LOCATION_DECK,0,POS_FACEDOWN_DEFENSE});
  d.add({blue_eyes_ultimate,0,0,LOCATION_EXTRA,0,POS_FACEDOWN_DEFENSE});
  if(set_before) d.add({book_moon,1,1,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  int announced=0,confirmed=0,resolving=0,solved=0,selections=0;
  bool requested=false,book=false,book_target=false,flipped=false,sent=false,revealed=false,ended=false;
  const bool sends=!historical||!set_before;
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0,"Wrong idle player");
      if(!requested) {
        require(e.idle[5].size()==1 && e.idle[5][0].code==prisma,"Prisma unavailable");requested=true;d.respond(5);
      } else {
        require(ended && sent==sends && revealed==sends && selections==(sends?2:0) &&
                flipped==set_before && book_target==set_before,"Incomplete Prisma effect");
        require(d.count(0,LOCATION_DECK)==int(!sends) && d.count(0,LOCATION_GRAVE)==int(sends) &&
                d.count(0,LOCATION_EXTRA)==1 && d.count(0,LOCATION_MZONE)==1 &&
                d.query_value(0,LOCATION_MZONE,0,QUERY_ALIAS)==(set_before?prisma:blue_eyes) &&
                d.position(0,LOCATION_MZONE,0)==(set_before?POS_FACEDOWN_DEFENSE:POS_FACEUP_ATTACK),
                "Wrong Prisma name/material state");
        require(d.lp(0)==8000 && d.lp(1)==8000 && d.count(1,LOCATION_GRAVE)==int(set_before),"Wrong final state");
        std::cout<<"Observed: "<<(historical?"resolution":"cost")<<" reveal/send; "
                 <<(sends?"material sent":"face-down Prisma sends nothing")<<".\n";return true;
      }
    } else if(e.kind==MSG_SELECT_CHAIN) {
      if(e.cards.empty()) {require(!e.forced,"Empty forced choice");d.respond(-1);}
      else {
        require(set_before && !book && e.player==1 && e.cards.size()==1 &&
                e.cards[0].code==book_moon,"Unexpected response");
        if(!requested) d.respond(-1);
        else {require(confirmed==1,"Wrong Book window");book=true;d.respond(0);}
      }
    } else if(e.kind==MSG_CHAINING) {
      require(e.chain==announced+1 && announced==confirmed &&
              e.cards.at(0).code==(announced?book_moon:prisma),"Wrong chain");++announced;
    } else if(e.kind==MSG_SELECT_CARD) {
      require(e.minimum==1 && e.maximum==1 && e.cards.size()==1,"Wrong selection count");
      if(e.player==1) {
        require(book && announced==2 && confirmed==1 && !book_target && e.cards[0].code==prisma,"Wrong Book target");
        book_target=true;
      } else {
        require(sends && selections<2 && (historical?resolving==1:confirmed==0) &&
                e.cards[0].code==(selections?blue_eyes:blue_eyes_ultimate) &&
                e.cards[0].location==(selections?LOCATION_DECK:LOCATION_EXTRA),
                "Prisma reveal/send must occur during Edison resolution");++selections;
      }
      d.respond({1,0});
    } else if(e.kind==MSG_CONFIRM_CARDS) {
      require(sends && selections==1 && !revealed && e.cards.size()==1 &&
              e.cards[0].code==blue_eyes_ultimate && (historical?resolving==1:!confirmed),
              "Wrong Fusion reveal timing");revealed=true;
    } else if(e.kind==MSG_MOVE) {
      if(e.from.code==blue_eyes) {
        require(revealed && selections==2 && !sent && e.from.location==LOCATION_DECK && e.to.location==LOCATION_GRAVE &&
                (historical?(resolving==1 && (e.reason&REASON_EFFECT) && !(e.reason&REASON_COST)):
                 (!confirmed && (e.reason&REASON_COST))),"Wrong Prisma material-send reason/timing");sent=true;
      } else require(set_before && e.from.code==book_moon && e.to.location==LOCATION_GRAVE,"Unexpected movement");
    } else if(e.kind==MSG_POS_CHANGE && e.from.code==prisma) {
      require(set_before && resolving==2 && !flipped && e.to.position==POS_FACEDOWN_DEFENSE,"Wrong Book resolution");flipped=true;
    } else if(e.kind==MSG_CHAINED) {
      require(confirmed+1==announced && e.chain==announced &&
              (e.chain==1?(sent==!historical):book_target),"Incomplete link");++confirmed;
    } else if(e.kind==MSG_CHAIN_SOLVING) {require(!resolving && e.chain==confirmed-solved,"Wrong resolution order");resolving=e.chain;}
    else if(e.kind==MSG_CHAIN_SOLVED) {require(resolving==e.chain,"Wrong solved link");resolving=0;++solved;}
    else if(e.kind==MSG_CHAIN_END) {require(!resolving && solved==confirmed,"Incomplete chain");ended=true;}
    else if(e.kind==MSG_CHAIN_NEGATED || e.kind==MSG_CHAIN_DISABLED || e.kind==MSG_SELECT_YESNO ||
            e.kind==MSG_SELECT_EFFECTYN || e.kind==MSG_DRAW) throw std::runtime_error("Unexpected Prisma event");
    return false;
  });
}
