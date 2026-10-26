#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
#define P11_MOVE_FUEL_GUARD 1
#include "../common/src/move_fuel.h"

// The production movement and successful-farm tail are included verbatim.
// This deterministic SDK backend forces fuel boundaries, not engine costs.
namespace cambc {
struct vec2 { int x=0,y=0;bool operator==(const vec2&)const=default; };
inline int fuel=100000, current_round=514, field_cost=1000, blocked_cost=1000;
inline int blocked_queries=0, field_queries=0, fresh_queries=0, attack_queries=0;
inline int post_move_fuel=-1, hidden_failures=0, failure_cost=1000, refresh_cost=1000;
inline bool stale_after_move=false,stale_after_field=false;
int fuel_remaining(){return fuel;}
unsigned round_number(){return unsigned(current_round);}
int distance(vec2 a,vec2 b){return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
struct bitboard {
    std::vector<vec2> values;bool inverted=false;
    bool is_set(vec2 p)const {
        const bool found=std::find(values.begin(),values.end(),p)!=values.end();
        return inverted?!found:found;
    }
    bool is_empty()const{return values.empty()&&!inverted;}
    void set(vec2 p,bool yes=true){
        values.erase(std::remove(values.begin(),values.end(),p),values.end());
        if(yes!=inverted)values.push_back(p);
    }
    bitboard& operator|=(const bitboard&b){assert(!inverted&&!b.inverted);for(auto p:b.values)set(p);return *this;}
    bitboard& operator&=(const bitboard&b){
        assert(!inverted);values.erase(std::remove_if(values.begin(),values.end(),[&](auto p){return !b.is_set(p);}),values.end());return *this;
    }
    bitboard operator~()const{auto b=*this;b.inverted=!b.inverted;return b;}
    friend bitboard operator|(bitboard a,const bitboard&b){return a|=b;}
    bitboard reachable_tiles(vec2 p,int radius)const{
        bitboard result;
        for(int x=p.x-radius;x<=p.x+radius;++x)for(int y=p.y-radius;y<=p.y+radius;++y)
            if(!is_set({x,y}))result.set({x,y});
        return result;
    }
    auto positions()const{return values;}
};
struct distance_field {
    static constexpr int unreachable=99999;bitboard goals;
    void compute_to(const bitboard&,const bitboard&g,const bitboard&,bitboard&,int maximum){
        assert(maximum==256);++field_queries;fuel-=field_cost;goals=g;
        if(stale_after_field)++current_round;
    }
    int get(vec2 p)const{int best=unreachable;for(auto goal:goals.values)best=std::min(best,distance(p,goal));return best;}
};
struct Monster {
    vec2 tile{95,82};vec2 spawn_position()const{return tile;}
    bool is_alive()const{return true;}vec2 position()const{return tile;}
};
struct Champion {
    vec2 tile{94,83};bool ready=true;int attempts=0,moves=0;
    vec2 position()const{return tile;}bool can_move()const{return ready;}
    bool move_absolute(vec2 p){
        ++attempts;assert(distance(tile,p)==1);
        if(hidden_failures-->0){fuel-=failure_cost;return false;}
        tile=p;ready=false;++moves;if(post_move_fuel>=0)fuel=post_move_fuel;
        if(stale_after_move)++current_round;
        return true;
    }
    bitboard attack_tiles()const{++attack_queries;bitboard b;b.set({95,82});return b;}
};
inline Champion champion;
Champion& self(){return champion;}
namespace vision {auto camps(){++fresh_queries;fuel-=refresh_cost;return std::vector<Monster>{{}};}}
}

namespace {
using cambc::bitboard;
bitboard around(cambc::vec2 p,int radius){
    bitboard b;for(int x=p.x-radius;x<=p.x+radius;++x)for(int y=p.y-radius;y<=p.y+radius;++y)b.set({x,y});return b;
}
struct EmptyPolicy {};
template<class Policy=EmptyPolicy>struct Caller {
    struct Entry {bitboard spawns;int incoming=0,unavailable_until=0;} entry;
    int fights=0;
    bitboard blocked(){++cambc::blocked_queries;cambc::fuel-=cambc::blocked_cost;return {};}
    void publish_commitment(int,int){assert(false);}
    void fight(const cambc::Monster&,bool camp,bool incoming){assert(camp&&!incoming);++fights;}
#include "../common/src/move_to.inc"
    void farm_tail(){
        auto&me=cambc::self();const int round=cambc::current_round,selected=0;
        bitboard goals;goals.set({96,81});entry.spawns.set({95,82});
#include "../common/src/farm_move.inc"
    }
};
void reset(){
    cambc::champion={};cambc::fuel=100000;cambc::current_round=514;
    cambc::field_cost=cambc::blocked_cost=1000;
    cambc::blocked_queries=cambc::field_queries=cambc::fresh_queries=cambc::attack_queries=0;
    cambc::post_move_fuel=-1;cambc::hidden_failures=0;cambc::failure_cost=cambc::refresh_cost=1000;
    cambc::stale_after_move=cambc::stale_after_field=false;
}
template<class Work> void yields(Work work){bool yielded=false;try{work();}catch(const p11::movement::Yield&){yielded=true;}assert(yielded);}
}
int main(){
    reset();Caller normal;normal.farm_tail();
    assert(cambc::champion.moves==1&&cambc::fresh_queries==1&&normal.fights==1);

    // Actual production move succeeds. Nothing queries fresh vision, attacks,
    // or marks this camp unavailable after the low-fuel yield.
    reset();cambc::post_move_fuel=29999;Caller low;
    yields([&]{low.farm_tail();});
    assert(cambc::champion.moves==1&&cambc::champion.tile==cambc::vec2(95,82));
    assert(cambc::fresh_queries==0&&cambc::attack_queries==0&&low.fights==0&&low.entry.unavailable_until==0);
    // A subsequent owner turn creates a fresh caller frame, rather than
    // continuing the pre-yield frame or repeating the already-completed move.
    ++cambc::current_round;cambc::fuel=100000;cambc::champion.ready=true;
    cambc::post_move_fuel=-1;low.farm_tail();assert(cambc::champion.moves==2&&cambc::fresh_queries==1);

    reset();cambc::stale_after_move=true;Caller stale;
    yields([&]{stale.farm_tail();});
    assert(cambc::champion.moves==1&&cambc::fresh_queries==0&&stale.fights==0);

    reset();cambc::fuel=39999;Caller start;
    yields([&]{start.farm_tail();});
    assert(cambc::blocked_queries==0&&cambc::field_queries==0&&cambc::champion.attempts==0);
    reset();cambc::fuel=40000;Caller start_boundary;start_boundary.farm_tail();
    assert(cambc::champion.moves==1&&start_boundary.fights==1);

    reset();cambc::field_cost=77001;Caller field;
    yields([&]{field.farm_tail();});
    assert(cambc::blocked_queries==1&&cambc::field_queries==1&&cambc::champion.attempts==0);
    reset();cambc::field_cost=77000;Caller retry_boundary;
    yields([&]{retry_boundary.farm_tail();});
    assert(cambc::blocked_queries==2&&cambc::champion.moves==1&&cambc::fresh_queries==0);

    reset();cambc::blocked_cost=70001;Caller setup;
    yields([&]{setup.farm_tail();});
    assert(cambc::blocked_queries==1&&cambc::field_queries==0&&cambc::champion.attempts==0);
    assert(setup.entry.unavailable_until==0);

    reset();cambc::stale_after_field=true;Caller epoch;
    yields([&]{epoch.farm_tail();});
    assert(cambc::field_queries==1&&cambc::champion.attempts==0&&epoch.entry.unavailable_until==0);

    reset();cambc::hidden_failures=3;cambc::failure_cost=76001;Caller retry;
    yields([&]{retry.farm_tail();});
    assert(cambc::blocked_queries==2&&cambc::champion.attempts==1&&cambc::fresh_queries==0);
    assert(retry.entry.unavailable_until==0);

    reset();cambc::hidden_failures=3;Caller bounded;
    bounded.farm_tail();
    assert(cambc::blocked_queries==4&&cambc::champion.attempts==3&&cambc::fresh_queries==0);
    assert(bounded.entry.unavailable_until==518);

    reset();cambc::post_move_fuel=30000;cambc::refresh_cost=10001;Caller target;
    yields([&]{target.farm_tail();});
    assert(cambc::fresh_queries==1&&cambc::attack_queries==0&&target.fights==0);
    reset();cambc::post_move_fuel=30000;cambc::refresh_cost=10000;Caller target_boundary;
    target_boundary.farm_tail();assert(target_boundary.fights==1&&cambc::attack_queries==1);
    std::puts("production movement guard boundaries passed");
}
