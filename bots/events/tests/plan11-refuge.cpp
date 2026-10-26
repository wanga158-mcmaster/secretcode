#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>
#include <tuple>
#include <type_traits>
#include <vector>
#include "../refuge/src/policy.h"

#ifdef NDEBUG
#error This oracle requires assertions enabled.
#endif

using p11_refuge::Threat;
using p11_refuge::Tile;
using Policy=p11_refuge::Policy;

namespace {
bool same_tile(const Tile&a,const Tile&b) {return a.x==b.x&&a.y==b.y;}

// Reproduce the former camp clearance heuristic only for its counterexample.
double old_clearance(const Tile&tile,std::span<const Threat> threats) {
    double score=0;
    for(const auto&threat:threats)
        score+=std::min(std::max(std::abs(tile.x-threat.x),std::abs(tile.y-threat.y)),6)*threat.damage;
    return score;
}

// Independent next-phase simulation for exactly represented quarter values.
// Membership uses the attack square's axis intervals, not the chooser's
// Chebyshev calculation. HP loses every covering hit, including after death.
int phase_oracle(int hp_quarters,std::span<const Tile> tiles,std::span<const Threat> threats) {
    int answer=-1;
    std::tuple<bool,int,int,int,int,int> winner{};
    for(std::size_t i=0;i<tiles.size();++i) {
        int remaining=hp_quarters;
        for(const auto&threat:threats) {
            const auto left=std::int64_t(threat.x)-threat.range,right=std::int64_t(threat.x)+threat.range;
            const auto bottom=std::int64_t(threat.y)-threat.range,top=std::int64_t(threat.y)+threat.range;
            if(tiles[i].x>=left&&tiles[i].x<=right&&tiles[i].y>=bottom&&tiles[i].y<=top)
                remaining-=int(threat.damage*4);
        }
        const auto key=std::tuple{remaining<=0,hp_quarters-remaining,tiles[i].escape_steps,
                                  tiles[i].spawn_steps,tiles[i].x,tiles[i].y};
        if(answer<0||key<winner){answer=int(i);winner=key;}
    }
    return answer;
}

std::uint32_t seed=0x6f756765;
std::uint32_t random_word(){seed=seed*1664525u+1013904223u;return seed;}
int draw(int count){return int(random_word()%std::uint32_t(count));}
} // namespace

int main() {
    static_assert(Policy::bounded_scan&&Policy::window_size==3&&Policy::trace_scan&&Policy::event_escape);
    static_assert(std::is_same_v<Policy::EscapeThreat,Threat>&&std::is_same_v<Policy::EscapeTile,Tile>);
    static_assert(p11_refuge::max_tiles==9&&p11_refuge::max_threats==128);

    // Both legal moves reach the production chooser. A farther, weaker
    // threat outside its attack square lures the old weighted-distance sum
    // into a lethal next camp phase. Distance hints cannot overturn survival.
    std::array<Tile,2> lure{{{-1,0,9,9},{1,1,0,0}}};
    std::array<Threat,2> lure_threats{{{2,3,2,10},{-5,0,1,6}}};
    assert(old_clearance(lure[1],lure_threats)>old_clearance(lure[0],lure_threats));
    assert(Policy::choose_escape(10,lure,lure_threats)==0);

    // The same failure also occurs with current Wolf/Golem rule values.
    std::array<Tile,2> official{{{10,10,8,8},{11,10,0,0}}};
    std::array<Threat,2> official_threats{{{14,10,3,20},{5,10,4,120}}};
    assert(old_clearance(official[1],official_threats)>old_clearance(official[0],official_threats));
    assert(Policy::choose_escape(20,official,official_threats)==0);

    // Every tile is lethal: choose the least next-phase loss, retaining
    // overkill rather than capping every lethal alternative at current HP.
    std::array<Tile,3> unsafe{{{0,0,0,0},{1,0,9,9},{2,0,0,0}}};
    std::array<Threat,3> point_hits{{{0,0,0,60},{1,0,0,20},{2,0,0,40}}};
    assert(Policy::choose_escape(20,unsafe,point_hits)==1);
    assert(Policy::choose_escape(20.25f,unsafe,point_hits)==1);

    // Waiting at origin is admissible, including when movement would hurt.
    std::array<Tile,3> wait{{{0,0,0,8},{1,0,0,0},{-1,0,0,0}}};
    std::array<Threat,2> sides{{{1,0,0,30},{-1,0,0,30}}};
    assert(Policy::choose_escape(20,wait,sides)==0);
    assert(Policy::choose_escape(20,std::span<const Tile>{wait.data(),1},sides)==0);

    // Zero damage permits route ties: safe-region distance precedes spawn,
    // followed by coordinate order independent of the supplied tile order.
    std::array<Tile,4> ties{{{1,0,3,0},{0,1,2,9},{-1,0,2,4},{0,-1,2,4}}};
    assert(Policy::choose_escape(1,ties,{})==2);
    std::reverse(ties.begin(),ties.end());
    assert(Policy::choose_escape(1,ties,{})==1);
    std::array<Tile,2> y_tie{{{2,1,0,0},{2,-1,0,0}}};
    assert(Policy::choose_escape(1,y_tie,{})==1);
    std::array<Tile,2> duplicate{{{0,0,0,0},{0,0,0,0}}};
    assert(Policy::choose_escape(1,duplicate,{})==0);

    // Attack squares include diagonal boundary tiles, but not range+1.
    std::array<Tile,3> boundary{{{2,2,0,0},{3,2,0,0},{-2,-2,0,0}}};
    std::array<Threat,1> square{{{0,0,2,25}}};
    assert(Policy::choose_escape(25,boundary,square)==1);
    std::array<Tile,2> extremes{{{std::numeric_limits<int>::min(),0,0,0},
                                {std::numeric_limits<int>::max(),0,1,1}}};
    std::array<Threat,1> far{{{std::numeric_limits<int>::max(),0,0,1}}};
    assert(Policy::choose_escape(1,extremes,far)==0);

    // All documented bounds are real production limits. No unsupported
    // oversized or malformed model silently becomes a safety decision.
    std::array<Tile,9> nine{};
    for(int i=0;i<9;++i)nine[i]={i%3-1,i/3-1,0,0};
    std::array<Threat,128> crowded{};
    for(auto&threat:crowded)threat={0,0,2,.25f};
    assert(Policy::choose_escape(32,nine,crowded)==0);
    assert(Policy::choose_escape(32.25f,nine,crowded)==0);
    assert(Policy::choose_escape(1,{},crowded)==-1);
    std::array<Tile,10> too_many_tiles{};
    std::array<Threat,129> too_many_threats{};
    assert(Policy::choose_escape(1,too_many_tiles,{})==-1);
    assert(Policy::choose_escape(1,nine,too_many_threats)==-1);
    for(float hp:{0.f,-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
        assert(Policy::choose_escape(hp,nine,{})==-1);
    for(float damage:{-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        square[0].damage=damage;assert(Policy::choose_escape(1,nine,square)==-1);
    }
    square[0]={0,0,-1,0};assert(Policy::choose_escape(1,nine,square)==-1);
    square[0]={0,0,0,0};assert(Policy::choose_escape(1,nine,square)==0);
    nine[0].escape_steps=-1;assert(Policy::choose_escape(1,nine,{})==-1);
    nine[0].escape_steps=0;nine[0].spawn_steps=-1;assert(Policy::choose_escape(1,nine,{})==-1);
    nine[0].spawn_steps=65535;assert(Policy::choose_escape(1,nine,{})==3);

    // Canonical addition remains stable under even widely separated float
    // threat values. The model intentionally sums loss in double precision.
    std::array<Threat,4> scales{{{0,0,9,1e30f},{0,0,9,1.f},{0,0,9,1e10f},{0,0,9,1e20f}}};
    auto scale_choice=Policy::choose_escape(1,nine,scales);
    std::reverse(scales.begin(),scales.end());
    assert(Policy::choose_escape(1,nine,scales)==scale_choice);

    // 4,096 independent phase simulations and their reversed-input checks
    // stay below the requested 10,000-case ceiling. No game is launched.
    constexpr int cases=4096;
    for(int case_id=0;case_id<cases;++case_id) {
        std::vector<Tile> tiles;
        const int tile_count=1+draw(9),threat_count=draw(6),hp_quarters=1+draw(120);
        for(int i=0;i<tile_count;++i)tiles.push_back({i%3-1,i/3-1,draw(8),draw(13)});
        std::vector<Threat> threats;
        for(int i=0;i<threat_count;++i)threats.push_back({draw(11)-5,draw(11)-5,draw(6),draw(81)/4.f});
        const int expected=phase_oracle(hp_quarters,tiles,threats);
        const int actual=Policy::choose_escape(hp_quarters/4.f,tiles,threats);
        assert(actual==expected);
        const auto selected=tiles[actual];
        std::reverse(tiles.begin(),tiles.end());std::reverse(threats.begin(),threats.end());
        const int reversed=Policy::choose_escape(hp_quarters/4.f,tiles,threats);
        assert(reversed==phase_oracle(hp_quarters,tiles,threats));
        assert(same_tile(selected,tiles[reversed]));
    }

    // Financial objective, banking and retreat remain inherited from Tempo.
    p11::State state;state.hp=600;
    std::array<p11::Candidate,2> menu{};
    menu[0].id=0;menu[0].gold=30;menu[0].clear=20;
    menu[1].id=1;menu[1].gold=100;menu[1].clear=2;
    assert(Policy::score(state,menu[0])==p11_tempo::Policy::score(state,menu[0]));
    assert(Policy::screen(state,menu[1])==p11_tempo::Policy::score(state,menu[1]));
    assert(p11::choose<Policy>(state,menu)==1);
    assert(Policy::bank(state)==p11_tempo::Policy::bank(state));
    assert(Policy::retreat(100,600,20)==p11_tempo::Policy::retreat(100,600,20));
    std::printf("refuge: %d independent next-phase cases, %d input-order checks\n",cases,cases);
}
