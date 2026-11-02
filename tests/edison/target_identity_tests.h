#pragma once
#include "harness.h"

// Chalice CL1 targets Blue-Eyes; Enemy Controller CL2 tributes it;
// Call CL3 can return the same card before Chalice resolves.
inline void target_identity(certification::Assets& assets, int options, int mode, bool trace) {
  using namespace certification;
  const bool leave=mode!=0, revive=mode==2;
  TestDuel d(assets,0,0,trace,options);
  d.add({blue_eyes,0,0,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  d.add({jerry_beans,1,1,LOCATION_MZONE,0,POS_FACEUP_ATTACK});
  for(auto pair:{std::pair<uint32_t,uint8_t>{chalice,0},{enemy_controller,1},{call_haunted,2}})
    d.add({pair.first,0,0,LOCATION_SZONE,pair.second,POS_FACEDOWN_DEFENSE});
  std::vector<uint32_t> links{chalice};
  if(leave) links.push_back(enemy_controller);
  if(revive) links.push_back(call_haunted);
  bool requested=false,enemy_chosen=false,call_chosen=false,option=false,cost_selected=false,cost=false;
  bool targets[3]={},returned=false,revived=false,controlled=false,finished=false;
  int announced=0,confirmed=0,resolving=0,solved=0;
  auto check_original=[&](uint32_t atk) {
    require(d.code(0,LOCATION_MZONE,0)==blue_eyes &&
            d.query_value(0,LOCATION_MZONE,0,QUERY_ATTACK)==atk,"Original target has unexpected identity/ATK");
  };
  d.start(options);
  d.run([&](const Event& e) {
    if(e.kind==MSG_SELECT_IDLECMD) {
      require(e.player==0 && d.phase()==PHASE_MAIN1,"Unexpected idle");
      if(requested) {
        require(finished && targets[0] && targets[1]==leave && targets[2]==revive &&
                cost==leave && option==leave && cost_selected==leave &&
                returned==revive && revived==revive && controlled==leave,"Missing target identity checkpoint");
        if(!leave || revive) check_original(leave?3000:3400);
        require(d.count(0,LOCATION_MZONE)==(leave?1+int(revive):1) &&
                d.count(1,LOCATION_MZONE)==int(!leave),"Wrong final controller counts");
        require(d.code(leave?0:1,LOCATION_MZONE,leave?1:0)==jerry_beans,"Enemy Controller took wrong monster");
        require(d.count(0,LOCATION_GRAVE)==(leave?(revive?2:3):1),"Wrong final Graveyard count");
        std::vector<uint32_t> grave,want{chalice};
        if(leave) want.push_back(enemy_controller);
        if(leave && !revive) want.push_back(blue_eyes);
        for(int i=0;i<d.count(0,LOCATION_GRAVE);++i) grave.push_back(d.code(0,LOCATION_GRAVE,i));
        std::sort(grave.begin(),grave.end());std::sort(want.begin(),want.end());
        require(grave==want,"Wrong Graveyard identities");
        require(d.count(0,LOCATION_SZONE)==(leave?1:2) &&
                d.code(0,LOCATION_SZONE,2)==call_haunted &&
                (revive?bool(d.position(0,LOCATION_SZONE,2)&POS_FACEUP):
                        bool(d.position(0,LOCATION_SZONE,2)&POS_FACEDOWN)),"Wrong retained Call state");
        for(int p=0;p<2;++p) {
          require(d.lp(p)==8000 && d.count(p,LOCATION_HAND)==0 && d.count(p,LOCATION_DECK)==0 &&
                  d.count(p,LOCATION_REMOVED)==0 && d.count(p,LOCATION_EXTRA)==0,"Unexpected final state");
          if(p==1) require(d.count(p,LOCATION_GRAVE)==0 && d.count(p,LOCATION_SZONE)==0,"Unexpected opposing zones");
        }
        std::cout<<"Observed: "<<(!leave?"target stayed; Chalice applied +400 ATK":
                    revive?"same card returned before CL1; Chalice did not affect the new instance":
                           "target stayed in Graveyard; Chalice did not apply")<<".\n";
        return true;
      }
      int index=-1;for(size_t i=0;i<e.idle[5].size();++i) if(e.idle[5][i].code==chalice) index=int(i);
      require(index>=0,"Chalice unavailable");requested=true;d.respond((index<<16)|5);
    } else if(e.kind==MSG_SELECT_CHAIN) {
      require(!e.forced,"Unexpected forced choice");int index=-1;
      for(size_t i=0;i<e.cards.size();++i) {
        auto code=e.cards[i].code;
        require(e.player==0 && (code==enemy_controller || code==call_haunted || (!requested && code==chalice)),"Unexpected response");
        if(leave && confirmed==1 && code==enemy_controller && !enemy_chosen) {index=int(i);enemy_chosen=true;}
        if(revive && confirmed==2 && code==call_haunted && !call_chosen) {index=int(i);call_chosen=true;}
      }
      d.respond(index);
    } else if(e.kind==MSG_CHAINING) {
      require(announced==confirmed && announced<int(links.size()) && e.chain==announced+1 &&
              e.player==0 && e.cards.at(0).code==links[announced],"Wrong chain order");++announced;
    } else if(e.kind==MSG_SELECT_OPTION) {
      require(announced==2 && confirmed==1 && leave && !option && e.player==0 &&
              e.options==std::vector<uint32_t>{enemy_controller*16,enemy_controller*16+1},
              "Expected Enemy Controller's two modes");option=true;d.respond(1);
    } else if(e.kind==MSG_SELECT_CARD) {
      require(e.player==0 && e.minimum==1 && e.maximum==1 && !resolving,"Unexpected target/cost selection");
      uint32_t want=blue_eyes;int controller=0,location=LOCATION_MZONE;
      if(announced==2) {
        require(option,"Enemy Controller mode not chosen");
        if(!cost) {require(!cost_selected,"Repeated tribute selection");cost_selected=true;}
        else {require(!targets[1],"Repeated control target");targets[1]=true;want=jerry_beans;controller=1;}
      } else {
        require((announced==1 || announced==3) && !targets[announced-1],"Repeated target");
        targets[announced-1]=true;if(announced==3) {require(cost,"Revival requires paid tribute");location=LOCATION_GRAVE;}
      }
      int index=-1;
      for(size_t i=0;i<e.cards.size();++i)
        if(e.cards[i].code==want && e.cards[i].controller==controller && e.cards[i].location==location) index=int(i);
      require(index>=0,"Required target/cost unavailable");d.respond({1,static_cast<byte>(index)});
    } else if(e.kind==MSG_MOVE) {
      if(e.from.code==blue_eyes && e.to.location==LOCATION_GRAVE) {
        require(announced==2 && confirmed==1 && cost_selected && !cost && e.from.location==LOCATION_MZONE &&
                (e.reason&(REASON_COST|REASON_RELEASE))==(REASON_COST|REASON_RELEASE),"Expected tribute cost");cost=true;
      } else if(e.from.code==blue_eyes && e.to.location==LOCATION_MZONE) {
        require(revive && resolving==3 && cost && !returned && e.from.location==LOCATION_GRAVE,
                "Unexpected return");returned=true;
      } else if(e.from.code==jerry_beans) {
        require(leave && resolving==2 && !controlled && e.from.controller==1 && e.to.controller==0 &&
                e.from.location==LOCATION_MZONE && e.to.location==LOCATION_MZONE,"Wrong control change");controlled=true;
      } else require((e.from.code==chalice || (leave && e.from.code==enemy_controller)) &&
                     e.from.location==LOCATION_SZONE && e.to.location==LOCATION_GRAVE,
                     "Unexpected card movement");
    } else if(e.kind==MSG_SELECT_PLACE) {
      require(e.player==0 && e.count==1,"Unexpected placement");
      int zone=resolving==3?0:1;
      require((resolving==3 && revive) || (resolving==2 && leave),"Placement outside return/control resolution");
      require(!(e.mask&(1u<<zone)),"Required field zone unavailable");
      d.respond({0,LOCATION_MZONE,static_cast<byte>(zone)});
    } else if(e.kind==MSG_SELECT_POSITION) {
      require(revive && resolving==3 && e.cards.at(0).code==blue_eyes &&
              (e.mask&POS_FACEUP_ATTACK),"Unexpected revival position");d.respond(POS_FACEUP_ATTACK);
    } else if(e.kind==MSG_SPSUMMONING) {
      require(revive && returned && resolving==3 && e.cards.at(0).code==blue_eyes,"Unexpected revival");
    } else if(e.kind==MSG_SPSUMMONED) {
      require(revive && returned && resolving==3 && !revived,"Unexpected revival success");revived=true;
    } else if(e.kind==MSG_CHAINED) {
      require(announced==confirmed+1 && e.chain==confirmed+1 && targets[confirmed],
              "Incomplete confirmed link");++confirmed;
    } else if(e.kind==MSG_CHAIN_SOLVING) {
      require(!resolving && confirmed==int(links.size()) && e.chain==confirmed-solved,"Wrong resolution order");
      resolving=e.chain;
      if(resolving==1 && revive) {require(returned && revived,"Target must return before Chalice");check_original(3000);}
    } else if(e.kind==MSG_CHAIN_SOLVED) {
      require(resolving==e.chain,"Wrong solved link");
      if(resolving==1 && (!leave || revive)) check_original(leave?3000:3400);
      resolving=0;++solved;
    } else if(e.kind==MSG_CHAIN_END) {
      require(!finished && !resolving && solved==int(links.size()),"Incomplete chain");finished=true;
    } else if(e.kind==MSG_SELECT_EFFECTYN || e.kind==MSG_SELECT_YESNO || e.kind==MSG_DRAW ||
              e.kind==MSG_PAY_LPCOST || e.kind==MSG_CHAIN_NEGATED || e.kind==MSG_CHAIN_DISABLED)
      throw std::runtime_error("Unexpected event in target identity fixture");
    return false;
  });
}
