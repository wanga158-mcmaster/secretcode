// Native supplied-setup assay: call the complete frozen-v41 Brain at r1 and r90.
// Every setup change is a normal move/basic action; no stats, flags or private state injection.
#include <cambc.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <exception>
#include "../../_micro-shield/src/brain.h"
#include "../../_micro-shield/src/config.h"

namespace mb {
namespace { int log_lines=0; }
bool fuel_ok(std::uint64_t need) { return cambc::fuel_remaining()>need+cfg::fuel_reserve; }
bool log_allowed() { return ++log_lines<=400; }
void FuelTrace::report(int round,std::uint64_t threshold) const {
    const auto total=start>last?start-last:0;
    if(!suspended&&total<threshold)return;
    if(!log_allowed())return;
    std::printf("fuel r%d %s%s total %llu:",round,suspended?"SUSPENDED in ":"",suspended?suspended_in:"",static_cast<unsigned long long>(total));
    for(int i=0;i<n;++i)std::printf(" %s=%llu",names[i],static_cast<unsigned long long>(used[i]));
    std::printf("\n");
}
}

namespace {
using namespace cambc;
inline constexpr int probe_active_side=0;
int side(){return map::spawn_region(team::ally).is_set({1,1})?0:1;}
bool active(){return side()==probe_active_side;}
vec2 frame(vec2 p){return probe_active_side?vec2{map::width()-1-p.x,map::height()-1-p.y}:p;}
void draft_turn(){
    const std::array party{champion_draft::varg,champion_draft::marcella,champion_draft::vesper_vale};
    const std::array passive{champion_draft::ren,champion_draft::tavi,champion_draft::prism};
    const auto& picks=active()?party:passive;
    const auto chosen=draft::current_draft(team::ally);
    for(auto p:picks)if(std::find(chosen.begin(),chosen.end(),p)==chosen.end()){draft::pick(p);return;}
}
bool move_to(vec2 p){auto& me=self();return me.position()!=p&&me.can_move()?me.move_towards(p):false;}
float shield_of(const champion& c){const auto b=c.buffs();return b.has_shield()?b.get<buff_shield>().value:0.0f;}
void basic_at(vec2 p){
    if(!self().can_attack()||!vision::has_champion(p))throw std::runtime_error("script basic prerequisite refusal");
    self().attack(vision::champion_at(p));
}
void turn(){
    auto& me=self();if(!me.is_alive())return;
    const auto type=me.champion_name();const int r=static_cast<int>(round_number());
    static mb::Brain* brain=nullptr;
    if(active()&&type==champion_type::varg){
        if(!brain)brain=new mb::Brain;
        if(r==1){
            std::printf("SP init_before hp%d fuel%llu\n",int(me.health()),static_cast<unsigned long long>(fuel_remaining()));
            brain->turn();
            std::printf("SP init_after hp%d fuel%llu\n",int(me.health()),static_cast<unsigned long long>(fuel_remaining()));
            return;
        }
        if(r<30){move_to(frame({10,10}));return;}
        if(r>=30&&r<=51){basic_at(frame({11,10}));return;}
        if(r!=90)return;
        if(me.position()!=frame({10,10})||me.health()!=10.0f||!vision::has_champion(frame({11,10}))){
            std::printf("SP setup_refusal r%d hp%d p%d,%d\n",r,int(me.health()),me.position().x,me.position().y);return;
        }
        const auto enemy=vision::champion_at(frame({11,10}));
        if(enemy.champion_name()!=champion_type::ren||enemy.health()!=20.0f){
            std::printf("SP enemy_setup_refusal hp%d\n",int(enemy.health()));return;
        }
        const auto ability=me.abilities()[1];const auto payload=ability.get<ability_shield>();
        std::printf("SP before r%d hp%d sh%d str%d foc%d M%d A%d C%d cd%d,%d shield_query%d shield_target%d duration%d ren_hp%d ren_sh%d ren_foc%d ren_spell_against%d fuel%llu\n",
                    r,int(me.health()),int(shield_of(me)),int(me.strength()),int(me.focus()),me.can_move(),me.can_attack(),me.can_cast(),
                    me.abilities()[0].cooldown(),ability.cooldown(),int(ability.shield_amount()),int(ability.shield_amount(me)),payload.duration,
                    int(enemy.health()),int(shield_of(enemy)),int(enemy.focus()),int(enemy.abilities()[0].damage_amount(me)),static_cast<unsigned long long>(fuel_remaining()));
        brain->turn();
        if(me.is_alive())std::printf("SP after r%d alive1 hp%d sh%d M%d A%d C%d cd%d,%d fuel%llu\n",r,int(me.health()),int(shield_of(me)),me.can_move(),me.can_attack(),me.can_cast(),me.abilities()[0].cooldown(),me.abilities()[1].cooldown(),static_cast<unsigned long long>(fuel_remaining()));
        else std::printf("SP after r%d alive0 fuel%llu\n",r,static_cast<unsigned long long>(fuel_remaining()));
        return;
    }
    if(active())return;
    if(type==champion_type::ren){
        if(r<30){move_to(frame({11,10}));return;}
        if(r>=53&&r<=60)basic_at(frame({10,10}));
        return;
    }
    if(type==champion_type::tavi){
        if(r<30){move_to(frame({10,13}));return;}
        if(r>=62&&r<=67){basic_at(frame({10,10}));return;}
        if(r>=68&&r<=85)move_to(frame({22,22}));
    }
}
}
int main(){while(true){try{if(cambc::is_draft())draft_turn();else turn();}catch(const std::exception& e){std::printf("shield probe error: %s\n",e.what());}cambc::end_turn();}}
