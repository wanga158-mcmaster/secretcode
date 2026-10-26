#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
#include "../common/src/events.h"

#ifdef NDEBUG
#error The post-cast caller fixture requires assertions enabled.
#endif

namespace test {
struct Live { bool visible=true,alive=true;float hp=1350,incoming=20;int x=5,y=4; };
inline int round=1,fuel=60000,attacks=0,last=-1,queries=0;
inline bool attack_ready=true,move_ready=false,throw_attack=false;
inline std::vector<std::string> calls;
inline std::function<void(const std::string&)> mutate;
inline void query(const std::string& name) {++queries;calls.push_back(name);if(mutate)mutate(name);}
}
namespace cambc {
struct vec2 {int x,y;};
enum class champion_type {tavi,other};
enum class handle_camp_t : std::uint32_t {};
struct camp {
    int id;vec2 spawn;test::Live* live;
    operator handle_camp_t() const {return static_cast<handle_camp_t>(id);}
    vec2 spawn_position() const {return spawn;}
    bool is_visible() const {test::query("visible"+std::to_string(id));return live->visible;}
    bool is_alive() const {test::query("alive"+std::to_string(id));return live->alive;}
    float health() const {test::query("hp"+std::to_string(id));return live->hp;}
    float strength() const {test::query("incoming"+std::to_string(id));return live->incoming;}
    vec2 position() const {test::query("position"+std::to_string(id));return {live->x,live->y};}
};
namespace map {inline bool is_in_bounds(vec2 p){return p.x>=0&&p.y>=0&&p.x<24&&p.y<14;}}
struct bitboard {
    bool range=false;std::vector<vec2> spawns;
    bool is_set(vec2 p) const {
        assert(map::is_in_bounds(p));
        if(range)return std::max(std::abs(p.x-2),std::abs(p.y-2))<=3;
        return std::any_of(spawns.begin(),spawns.end(),[&](auto s){return s.x==p.x&&s.y==p.y;});
    }
};
struct Champion {
    champion_type type=champion_type::tavi;float damage=45;
    champion_type champion_name()const{test::query("type");return type;}
    bool can_attack()const{test::query("can_attack");return test::attack_ready;}
    bool can_move()const{test::query("can_move");return test::move_ready;}
    float strength()const{test::query("strength");return damage;}
    bitboard attack_tiles()const{test::query("range");return {true,{}};}
    void attack(const camp& target)const{
        test::query("attack"+std::to_string(target.id));
        if(test::throw_attack)throw std::runtime_error("supplied host attack failure");
        ++test::attacks;test::last=target.id;target.live->hp-=damage;test::attack_ready=false;
    }
};
inline Champion champion;
inline Champion& self(){return champion;}
inline int round_number(){test::query("round");return test::round;}
inline int fuel_remaining(){test::query("fuel");return test::fuel;}
}
using camp=cambc::camp;
struct Group {
    struct Members {std::vector<camp> values;std::span<const camp> camps()const{return values;}} group;
    cambc::bitboard spawns;
};
struct Enabled {static constexpr bool triage_postcast_basic=true;};
struct Absent {};
struct Disabled {static constexpr bool triage_postcast_basic=false;};
template<class Policy> struct Harness {
    unsigned monster_shards_=0;
#include "../common/src/postcast_basic.inc"
};

namespace {
int checks=0,cases=0;
void check(bool value){++checks;assert(value);}
struct Fixture {
    test::Live gold{false,false,0,0,-1,-1},wolf{},third{};
    camp dead{103,{5,3},&gold};
    Group entry;
    Harness<Enabled> caller;
    Fixture(){
        ++cases;test::round=1;test::fuel=60000;test::attacks=0;test::last=-1;test::queries=0;
        test::attack_ready=true;test::move_ready=false;test::throw_attack=false;test::calls.clear();test::mutate={};
        cambc::champion={};
        entry.group.values={dead,{907,{5,4},&wolf}};entry.spawns.spawns={{5,3},{5,4}};
    }
    void run(){caller.postcast_basic(entry,dead,1);}
    void refused(){run();check(test::attacks==0);}
};
}

int main(){
    {Fixture f;f.run();check(test::attacks==1&&test::last==907);check(f.wolf.hp==1305);check(f.gold.hp==0);
     check(std::find(test::calls.begin(),test::calls.end(),"visible103")==test::calls.end());
     check(std::find(test::calls.begin(),test::calls.end(),"position103")==test::calls.end());}
    {Fixture f;test::fuel=35000;f.run();check(test::attacks==1);}
    {Fixture f;test::fuel=34999;f.refused();check(test::calls==std::vector<std::string>({"round","fuel"}));}
    {Fixture f;test::round=2;f.refused();check(test::calls==std::vector<std::string>({"round"}));}
    {Fixture f;test::attack_ready=false;f.refused();}
    {Fixture f;test::move_ready=true;f.refused();}
    {Fixture f;cambc::champion.type=cambc::champion_type::other;f.refused();}
    {Fixture f;f.gold.alive=true;f.refused();}
    {Fixture f;f.dead.id=777;f.refused();}
    {Fixture f;f.entry.group.values.clear();f.refused();}
    {Fixture f;f.entry.group.values.push_back(f.entry.group.values.back());f.refused();}
    {Fixture f;for(int i=0;i<63;++i)f.entry.group.values.push_back({1000+i,{5,4},&f.wolf});f.refused();}
    {Fixture f;f.entry.spawns.spawns={{5,3}};f.refused();}
    {Fixture f;f.entry.group.values[1].spawn={-1,4};f.refused();}
    {Fixture f;f.wolf.visible=false;f.refused();check(std::find(test::calls.begin(),test::calls.end(),"hp907")==test::calls.end());}
    {Fixture f;f.wolf.alive=false;f.refused();}
    {Fixture f;f.wolf.hp=0;f.refused();}
    {Fixture f;f.wolf.hp=std::numeric_limits<float>::quiet_NaN();f.refused();}
    {Fixture f;f.wolf.x=-1;f.refused();}
    {Fixture f;f.wolf.x=24;f.refused();}
    {Fixture f;f.wolf.x=10;f.refused();}
    {Fixture f;f.wolf.incoming=std::numeric_limits<float>::infinity();f.refused();}
    {Fixture f;cambc::champion.damage=0;f.refused();}
    {Fixture f;test::mutate=[](const auto& name){if(name=="hp907")test::round=2;};f.refused();}
    {Fixture f;test::mutate=[](const auto& name){if(name=="range")test::fuel=34999;};f.refused();}
    {Fixture f;int visits=0;test::mutate=[&](const auto& name){if(name=="visible907"&&++visits==2)f.wolf.visible=false;};f.refused();}
    {Fixture f;int visits=0;test::mutate=[&](const auto& name){if(name=="position907"&&++visits==2)f.wolf.x=20;};f.refused();}
    {Fixture f;test::mutate=[](const auto& name){if(name=="incoming907")test::attack_ready=false;};f.refused();}
    {Fixture f;test::mutate=[](const auto& name){if(name=="incoming907")test::move_ready=true;};f.refused();}
    {Fixture f;int visits=0;test::mutate=[&](const auto& name){if(name=="range"&&++visits==2)test::round=2;};f.refused();}
    {Fixture f;int visits=0;test::mutate=[&](const auto& name){if(name=="range"&&++visits==2)test::fuel=34999;};f.refused();}
    {Fixture f;f.third=f.wolf;f.entry.group.values.push_back({29,{4,4},&f.third});f.entry.spawns.spawns.push_back({4,4});f.run();check(test::attacks==1&&test::last==907);}
    {Fixture f;f.third=f.wolf;f.third.hp=45;f.entry.group.values.push_back({29,{4,4},&f.third});f.entry.spawns.spawns.push_back({4,4});f.run();check(test::attacks==1&&test::last==29);}
    {Fixture f;test::throw_attack=true;bool caught=false;try{f.run();}catch(const std::runtime_error&){caught=true;}check(caught&&test::attacks==0);}
    {Fixture f;Harness<Absent> absent;absent.postcast_basic(f.entry,f.dead,1);check(test::queries==0&&test::attacks==0);}
    {Fixture f;Harness<Disabled> disabled;disabled.postcast_basic(f.entry,f.dead,1);check(test::queries==0&&test::attacks==0);}
    std::printf("postcast-basic cases=%d checks=%d actual-helper=1 supplied-sdk=1\n",cases,checks);
}
