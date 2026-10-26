// Actual frozen-v40 fight_enemies caller with reachable positions and native effects.
#include <cambc.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <exception>
#include "../../_micro-freecenter/src/combat.h"
#include "../../_micro-freecenter/src/config.h"
#include "../../_micro-freecenter/src/knowledge.h"
#include "../../_micro-freecenter/src/world.h"
namespace mb {
bool fuel_ok(std::uint64_t need) {return cambc::fuel_remaining()>need+cfg::fuel_reserve;}
bool log_allowed() {static int count=0;return ++count<=100;}
}
namespace {
using namespace cambc;
inline constexpr int probe_active_side=0;
int side(){return map::spawn_region(team::ally).is_set({1,1})?0:1;}
bool active(){return side()==probe_active_side;}
vec2 frame(vec2 p){return probe_active_side?vec2{map::width()-1-p.x,map::height()-1-p.y}:p;}
void draft_turn(){
    const std::array party{champion_draft::ryth,champion_draft::heart_of_ruin,champion_draft::marcella,champion_draft::tavi,champion_draft::vesper_vale};
    const std::array passive{champion_draft::vendetta,champion_draft::varg,champion_draft::ren,champion_draft::willow,champion_draft::prism};
    const auto& picks=active()?party:passive;const auto chosen=draft::current_draft(team::ally);
    for(auto p:picks)if(std::find(chosen.begin(),chosen.end(),p)==chosen.end()){draft::pick(p);return;}
}
void turn(){
    auto& me=self();if(!me.is_alive())return;
    static bool init=false;if(!init){mb::World::get();mb::Knowledge::get();mb::kit();init=true;}
    const auto type=me.champion_name();vec2 dest{};bool moving=true;
    if(active()){
        if(type==champion_type::ryth)dest=frame({15,12});
        else if(type==champion_type::heart_of_ruin)dest=frame({3,3});
        else if(type==champion_type::marcella)dest=frame({3,4});
        else if(type==champion_type::tavi)dest=frame({4,3});
        else dest=frame({4,4});
    }
    else if(type==champion_type::vendetta)dest=frame({17,9});
    else if(type==champion_type::varg)dest=frame({16,9});
    else if(type==champion_type::ren)dest=frame({16,11});else moving=false;
    const int r=int(round_number());
    if(r<40){if(moving&&me.position()!=dest&&me.can_move()&&!vision::has_champion(dest))me.move_towards(dest);return;}
    if(r!=40||!active()||type!=champion_type::ryth)return;
    if(me.position()!=dest){std::printf("FC setup refusal\n");return;}
    mb::Ctx ctx;ctx.observe();mb::TurnActions acts;
    std::printf("FC before r%d x%d y%d hp%d enemies%zu fuel%llu\n",r,ctx.pos.x,ctx.pos.y,int(me.health()),ctx.enemies.size(),static_cast<unsigned long long>(fuel_remaining()));
    const bool acted=mb::fight_enemies(ctx,acts,false,nullptr);
    std::printf("FC after r%d hp%d acted%d moved%d cast%d fuel%llu\n",r,int(me.health()),acted,acts.moved,acts.cast,static_cast<unsigned long long>(fuel_remaining()));
}
}
int main(){while(true){try{if(cambc::is_draft())draft_turn();else turn();}catch(const std::exception& e){std::printf("freecenter probe error: %s\n",e.what());}cambc::end_turn();}}
