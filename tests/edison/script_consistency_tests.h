#pragma once
#include "harness.h"
#include "ygoenv/ygopro/script_loader.h"
inline void script_consistency(certification::Assets& assets,int rule) {
  using namespace certification;
  const std::vector<uint32_t> changed{swap_frog,my_body,destiny_dragoon,mark_rose,black_garden,armory_arm,
    jade_knight,cyber_phoenix,future_fusion,fortune_light,redmd,strike_ninja,rescue_cat,prisma,quickdraw,
    ryko,darkness_approaches,treeborn,necrovalley,brionac,catapult_turtle,goyo,brain,honest,kalut,sangan,caius};
  std::vector<std::string> files{"constant.lua","utility.lua","procedure.lua"};
  for(auto [code,data]:assets.cards)
    if(code!=blue_eyes&&code!=jerry_beans&&code!=rose_token) files.push_back("c"+std::to_string(code)+".lua");
  require(files.size()==88&&changed.size()==27,"Update reviewed script inventory for new fixtures/overrides");
  assets.script_rule=rule;
  int overrides=0,suffixes=0;
  for(auto file:files) {
    const auto path=assets.scripts/file;
    std::ifstream in(path,std::ios::binary);require(bool(in),"Missing pinned script");
    std::string original((std::istreambuf_iterator<char>(in)),{});
    auto replacement=ygopro::historical_card_script(file,rule);
    auto suffix=ygopro::historical_script_suffix(file,rule);
    bool changed_card=false;
    for(auto code:changed) if(file=="c"+std::to_string(code)+".lua") changed_card=true;
    require(!replacement.empty()==(rule==1&&changed_card),"Unreviewed or missing historical override");
    require(!suffix.empty()==(rule==1&&file=="utility.lua"),"Wrong helper suffix selection");
    for(auto prefix:{"nested/","C:\\scripts\\"}) {
      require(ygopro::historical_card_script(std::string(prefix)+file,rule)==replacement,"Card basename normalization mismatch");
      require(ygopro::historical_script_suffix(std::string(prefix)+file,rule)==suffix,"Helper basename normalization mismatch");
    }
    std::string expected=replacement.empty()?original:std::string(replacement);expected+=suffix;
    int prod_len=-1,cert_len=-1;
    std::unique_ptr<uint8_t[]> production(ygopro::read_card_script(path.string(),&prod_len,rule));
    auto certification=Assets::read_script(path.string().c_str(),&cert_len);
    assets.check();
    require(production&&certification&&prod_len==int(expected.size())&&cert_len==prod_len&&
            std::string(reinterpret_cast<char*>(production.get()),prod_len)==expected&&
            std::string(reinterpret_cast<char*>(certification),cert_len)==expected,"Production/certification script bytes differ");
    if(rule==1) {
      int default_len=-1;
      std::unique_ptr<uint8_t[]> defaults(ygopro::read_card_script(path.string(),&default_len));
      require(default_len==prod_len&&std::equal(defaults.get(),defaults.get()+default_len,production.get()),"Production default must select Edison");
    }
    overrides+=!replacement.empty();suffixes+=!suffix.empty();
  }
  int missing_len=-1;
  std::unique_ptr<uint8_t[]> missing(ygopro::read_card_script((assets.scripts/"__missing_certification__.lua").string(),&missing_len,rule));
  require(!missing&&missing_len==0,"Missing script must fail closed");
  require(ygopro::historical_card_script("unknown.lua",rule).empty(),"Unknown card must not use Sangan fallback");
  for(int other:{0,2,3,4,5}) {
    require(ygopro::historical_card_script("c26202165.lua",other).empty()&&
            ygopro::historical_script_suffix("utility.lua",other).empty(),"Historical changes leaked outside Rule 1");
  }
  std::cout<<"Observed: "<<files.size()<<" pinned scripts match both loaders; "<<overrides
           <<" card overrides, "<<suffixes<<" helper suffixes; rule "<<rule<<".\n";
}
