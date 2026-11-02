#pragma once
#include "harness.h"

inline void black_garden_facedown(certification::Assets& assets,int options,bool historical,bool facedown,bool trace) {
  using namespace certification;
  const uint32_t spell=facedown?shallow_grave:call_haunted;
  const bool triggers=historical||!facedown;
  TestDuel d(assets,0,0,trace,options);
  d.add({black_garden,0,0,LOCATION_SZONE,5,POS_FACEUP_ATTACK});
  d.add({spell,0,0,LOCATION_SZONE,0,POS_FACEDOWN_DEFENSE});
  d.add({blue_eyes,0,0,LOCATION_GRAVE,0,POS_FACEUP_ATTACK});
  if(facedown) d.add({blue_eyes,1,1,LOCATION_GRAVE,0,POS_FACEUP_ATTACK});
  int chains=0,targets=0,revived=0,tokens=0,batches=0;bool requested=false,announced=false,confirmed=false,resolving=false,solved=false;
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0,"Wrong idle");
      if(!requested) {
        int pick=-1;for(size_t i=0;i<e.idle[5].size();++i) if(e.idle[5][i].code==spell) pick=i;
        require(pick>=0,"Revival spell unavailable");requested=true;d.respond((pick<<16)|5);
      } else {
        require(chains==1+int(triggers) && targets==(facedown?2:1) && revived==(facedown?2:1) &&
                tokens==(triggers?(facedown?2:1):0) && batches==1+int(triggers),"Black Garden must trigger on face-down Special Summons in Edison");
        require(d.code(0,LOCATION_MZONE,0)==blue_eyes &&
                d.position(0,LOCATION_MZONE,0)==(facedown?POS_FACEDOWN_DEFENSE:POS_FACEUP_ATTACK) &&
                d.query_value(0,LOCATION_MZONE,0,QUERY_ATTACK)==uint32_t(facedown?3000:1500),"Wrong summoned monster position/ATK");
        require(d.count(0,LOCATION_MZONE)==1+int(facedown&&triggers) &&
                d.count(1,LOCATION_MZONE)==int(facedown)+int(triggers) &&
                d.count(0,LOCATION_GRAVE)==int(facedown) && d.count(1,LOCATION_GRAVE)==0 &&
                d.lp(0)==8000 && d.lp(1)==8000,"Wrong Garden final zones");
        if(triggers) {
          require(d.code(1,LOCATION_MZONE,facedown?1:0)==rose_token &&
                  d.query_value(1,LOCATION_MZONE,facedown?1:0,QUERY_ATTACK)==800,"Wrong opposing Rose Token");
          if(facedown) require(d.code(0,LOCATION_MZONE,1)==rose_token && d.position(1,LOCATION_MZONE,0)==POS_FACEDOWN_DEFENSE,"Wrong second token/face-down monster");
        }
        std::cout<<"Observed: "<<(facedown?"two face-down summons":"face-up summon halved to 1500")
                 <<"; "<<tokens<<" Rose Token(s), no recursive Garden trigger.\n";return true;
      }
    } else if(e.kind==MSG_SELECT_CHAIN) {
      if(e.cards.empty()) {require(!e.forced,"Empty forced choice");d.respond(-1);}
      else {
        require(!requested && e.player==0 && !e.forced && e.cards.size()==1 && e.cards[0].code==call_haunted,
                "Unexpected response");d.respond(-1);
      }
    } else if(e.kind==MSG_CHAINING) {
      require(!announced && e.chain==1 && e.cards.at(0).code==(chains?black_garden:spell) &&
              (!chains || (triggers && revived==(facedown?2:1))),"Wrong Garden chain");announced=true;
    } else if(e.kind==MSG_SELECT_CARD) {
      require(!chains && announced && !confirmed && e.minimum==1 && e.maximum==1 && e.cards.size()==1 &&
              e.cards[0].code==blue_eyes && e.cards[0].location==LOCATION_GRAVE && e.player==targets,
              "Wrong revival target");++targets;d.respond({1,0});
    } else if(e.kind==MSG_MOVE) {
      if(e.from.code==blue_eyes) {
        require(!chains && resolving && e.from.location==LOCATION_GRAVE && e.to.location==LOCATION_MZONE,
                "Wrong revival movement");++revived;
      } else if(e.from.code==rose_token) require(chains==1 && resolving && e.to.location==LOCATION_MZONE,"Wrong token placement");
      else require(facedown && e.from.code==shallow_grave && e.to.location==LOCATION_GRAVE,"Unexpected movement");
    } else if(e.kind==MSG_SELECT_PLACE) {
      require(resolving && e.count==1,"Wrong summon placement");
      int slot=-1,relative=0;
      for(int side=0;side<2 && slot<0;++side) for(int i=0;i<5;++i)
        if(!(e.mask&(1u<<(16*side+i)))) {slot=i;relative=side;break;}
      require(slot>=0,"No free summon zone");
      d.respond({uint8_t(e.player^relative),LOCATION_MZONE,uint8_t(slot)});
    } else if(e.kind==MSG_SELECT_POSITION) {
      require(!facedown && !chains && resolving && e.cards.at(0).code==blue_eyes &&
              (e.mask&POS_FACEUP_ATTACK),"Wrong revival position");d.respond(POS_FACEUP_ATTACK);
    } else if(e.kind==MSG_SPSUMMONING && e.cards.at(0).code==rose_token) {
      require(chains==1 && resolving,"Wrong token summon");++tokens;
    } else if(e.kind==MSG_SPSUMMONED) {require(resolving,"Unexpected summon batch");++batches;}
    else if(e.kind==MSG_CHAINED) {require(announced && !confirmed && e.chain==1,"Wrong confirmation");confirmed=true;}
    else if(e.kind==MSG_CHAIN_SOLVING) {require(confirmed && !resolving && e.chain==1,"Wrong resolution");resolving=true;}
    else if(e.kind==MSG_CHAIN_SOLVED) {require(resolving && e.chain==1,"Wrong solved link");resolving=false;solved=true;}
    else if(e.kind==MSG_CHAIN_END) {require(solved && !resolving,"Incomplete chain");++chains;announced=confirmed=solved=false;}
    else if(e.kind==MSG_CHAIN_NEGATED || e.kind==MSG_CHAIN_DISABLED || e.kind==MSG_SELECT_EFFECTYN ||
            e.kind==MSG_SELECT_YESNO || e.kind==MSG_DRAW) throw std::runtime_error("Unexpected Garden event");
    return false;
  });
}
