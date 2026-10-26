#include "../common/src/competition.h"
#include <cstdlib>
#include <iostream>

using namespace p9::live;
namespace c=p9::live::competition;
namespace {
int checks=0;
void check(bool value,const char* name) {
    ++checks;
    if(!value) {std::cerr<<"FAIL competition: "<<name<<'\n';std::exit(1);}
}
World world() {
    World w;w.round=100;w.max_rounds=2000;w.win_gems=1000;
    w.capture_turns=6;w.tribute_multiplier=.1;w.safe_now=true;
    w.self.id=10;w.self.position={0,0};w.self.visible=w.self.alive=w.self.ally=true;
    w.self.hp=w.self.max_hp=1000;w.self.strength=100;
    return w;
}
Group group(int id,Gems gems,int travel=4,int bank=4,Point position={20,20}) {
    Group g;g.id=id;g.position=g.approach=position;
    g.observed=g.route_known=g.safe_solo=g.living=true;
    g.gems=gems;g.gold=40;g.travel=travel;g.to_bank=bank;
    g.hp=100;g.clear_turns=2;return g;
}
void source_selection() {
    auto w=world();w.group_count=3;
    w.groups[0]=group(1,10);w.groups[1]=group(2,11);w.groups[2]=group(3,80);
    check(c::gem_source(w)==3,"third feasible candidate can beat the first two");
    std::swap(w.groups[0],w.groups[2]);
    check(c::gem_source(w)==3,"source result does not depend on array order");
    w.groups[0]=group(9,0,1);w.groups[1]=group(4,30,2);w.groups[2]=group(2,0,5);
    check(c::gem_source(w)==4,"gem selection excludes a nearer gold-only camp");
    check(c::gold_source(w)==9,"gold fallback excludes the positive-gem camp");
    w.groups[0].to_bank=-1;
    check(c::gold_source(w)==9,"gold income does not require an altar route");

    w=world();w.group_count=2;
    w.groups[0]=group(7,20);w.groups[1]=group(3,20);
    check(c::gem_source(w)==3,"exact value ties use deterministic group ID");
    w.groups[0]=group(7,100,4,4,{2,2});w.groups[1]=group(3,80,4,4,{20,20});
    w.ally_count=1;w.allies[0]=w.self;w.allies[0].id=11;w.allies[0].position={2,3};
    check(c::gem_source(w)==3,"nearby ally congestion can favor an empty camp");
    w.allies[0].visible=false;
    check(c::gem_source(w)==7,"an unseen ally cannot manufacture congestion");

    w=world();w.group_count=1;w.groups[0]=group(1,200,80,80);
    check(c::gem_source(w)==1,"real remaining rounds allow a trip beyond the old 120 horizon");
    w.round=1900;
    check(c::gem_source(w)==-1,"a source that cannot bank before game end is rejected");
    w=world();w.group_count=1;w.groups[0]=group(1,1500,0,0);
    check(c::tribute_time(w,1500)==150,"tribute duration is not clipped to the old horizon");
    w.round=1843;
    check(c::gem_source(w)==-1,"long tribute duration counts in deadline feasibility");
    w.round=100;w.groups[0].observed=false;
    check(c::gem_source(w)==-1,"hidden camps are exploration candidates, never priced as known safe production");
    w.groups[0].observed=true;w.groups[0].respawning=true;
    check(c::gem_source(w)==-1,"a respawning source is not an available reward");
    w.groups[0].respawning=false;w.groups[0].incoming=1000;
    check(c::gem_source(w)==-1,"safety is rechecked against the current actor");
    w.groups[0]=group(1,0,4);w.groups[0].gold=0;
    check(c::gold_source(w)==-1,"zero-value camps do not become gold fallback");

    w=world();w.group_count=2;
    auto blocked=group(7,80,1,1);
    blocked.living=false;blocked.cleared=true;blocked.hp=0;blocked.clear_turns=0;
    blocked.contested=true;blocked.incoming=40;blocked.camp_incoming=0;
    w.groups[0]=blocked;w.groups[1]=group(3,10,6);
    check(!c::available<true>(w,w.groups[0]),"enemy-contested cleared work is not ordinary production for the opt-in");
    check(c::available(w,w.groups[0])&&c::available<false>(w,w.groups[0]),
        "default availability preserves legacy contested capture admission");
    check(c::service_turns(w,w.groups[0])>=0&&c::service_turns<true>(w,w.groups[0])<0,
        "service pricing propagates only the explicit contest opt-in");
    check(c::gem_source<true>(w)==3,"a living gem source beats a zero-work contested capture for the opt-in");
    check(c::gem_source(w)==7,"default gem selection retains the legacy cleared contested source");
    w.groups[0].cleared=false;w.groups[0].living=true;w.groups[0].hp=100;w.groups[0].clear_turns=3;
    check(c::available<true>(w,w.groups[0])&&c::gem_source<true>(w)==7,"safe living contested work retains ordinary admission");
    w.groups[0]=blocked;w.groups[0].contested=false;
    w.ally_count=1;w.allies[0]=w.self;w.allies[0].id=11;w.allies[0].position={20,21};
    check(c::nearby_allies(w,w.groups[0])==1&&c::available<true>(w,w.groups[0])&&c::gem_source<true>(w)==7,
        "an uncontested cleared source remains available with allied congestion");
    w.ally_count=0;w.groups[0]=blocked;w.groups[0].gems=0;w.groups[0].gold=400;
    w.groups[1]=group(3,0,6);
    check(c::gold_source<true>(w)==3,"living gold work beats a zero-work contested capture for the opt-in");
    check(c::gold_source(w)==7,"default gold selection retains the legacy cleared contested source");
    w.groups[0].contested=false;
    check(c::gold_source<true>(w)==7,"uncontested cleared gold retains its ordinary value");
    w.group_count=1;w.groups[0]=blocked;w.self.bag=10;
    check(c::bank_now<true>(w,10),"only blocked capture work cannot postpone converting a carried bag for the opt-in");
    check(!c::bank_now(w,10),"default banking retains the legacy nearby contested capture deferral");
    w.groups[0].contested=false;
    check(!c::bank_now<true>(w,10),"a nearby uncontested capture can still precede a small bank trip");
}
void banking() {
    auto w=world();w.group_count=1;w.groups[0]=group(1,20,4);
    check(!c::bank_now(w,10),"an empty bag never requests tribute");
    w.self.bag=10;
    check(!c::bank_now(w,10),"a healthy small bag can add a nearby safe gem reward");
    check(c::bank_now(w,3),"a nearby altar banks the current bag");
    w.self.bag=24;
    check(c::bank_now(w,10),"the bag threshold limits exposed carried gems");
    w.self.bag=5;w.score=995;
    check(c::bank_now(w,10),"an immediate winning bag banks before further harvesting");
    w.score=0;w.self.bag=10;w.round=1980;
    check(c::bank_now(w,10),"deadline reserve includes travel and tribute");
    w.round=100;w.self.hp=400;
    check(c::bank_now(w,10),"low health prioritizes carried value");
    w.self.hp=1000;w.safe_now=false;
    check(c::bank_now(w,10),"observed immediate danger prioritizes carried value");
    w.safe_now=true;w.groups[0].travel=11;
    check(c::bank_now(w,10),"a distant gem camp does not defer a bank trip");
    w.groups[0].travel=4;w.groups[0].safe_solo=false;
    check(c::bank_now(w,10),"an unsafe nearby camp cannot defer banking");
    w.groups[0].safe_solo=true;w.groups[0].gems=0;
    check(c::bank_now(w,10),"gold-only work cannot defer banking a gem bag");
}
void exploration() {
    c::Exploration e;e.start(7,10,30);
    check(e.target==7&&e.deadline==48,"exploration budgets route length plus eight turns");
    for(int round=11;round<=22;++round)
        check(e.continue_route(7,round,40-round),"a progressing route continues beyond eight turns");
    check(e.lastProgress==22&&e.lastDistance==18,"progress retains the best actual route distance");
    check(e.continue_route(7,23,18),"one stalled turn is tolerated");
    check(e.continue_route(7,24,19),"a short detour does not count as progress");
    check(!e.continue_route(7,25,18)&&e.target==-1,"three turns without a new best distance abort the route");
    e.start(8,0,1);
    check(e.deadline==12,"short exploration receives a twelve-turn minimum");
    check(!e.continue_route(9,1,0),"a mismatched target does not continue old exploration");
    e.start(8,0,200);
    check(e.deadline==120,"long exploration has an absolute 120-turn budget");
    check(!e.continue_route(8,120,1),"absolute deadline wins even when distance improves");
    e.start(8,10,-1);
    check(!e.continue_route(8,11,0),"unknown routes are not admitted as exploration progress");
}
void cleared_geometry_and_items() {
    std::array<Point,9> spawns{};std::array<int,9> exits{};
    int index=0;
    for(int y=1;y<=3;++y)for(int x=1;x<=3;++x) {
        spawns[index]={x,y};exits[index]=(x==2&&y==2)?-1:1;++index;
    }
    check(c::cleared_group_bank_distance(spawns,exits)==2,"interior 3x3 spawn exits through another cleared member");
    exits.fill(-1);
    check(c::cleared_group_bank_distance(spawns,exits)==-1,"a completely sealed group has no invented bank route");
    const std::array<Point,2> disconnected{Point{0,0},Point{5,5}};
    const std::array<int,2> one_exit{2,-1};
    check(c::cleared_group_bank_distance(disconnected,one_exit)==-1,"an unknown disconnected member invalidates worst-spawn travel");
    const std::array<Point,2> diagonal{Point{0,0},Point{1,1}};
    check(c::cleared_group_bank_distance(diagonal,one_exit)==3,"cleared internal paths use legal eight-direction steps");
    check(c::cleared_group_bank_distance(std::span<const Point>{},std::span<const int>{})==-1,"empty geometry remains unknown");
    check(c::cleared_group_bank_distance(spawns,std::span<const int>{one_exit})==-1,"mismatched exit metadata is rejected");
#if P9_NAV_BOUNDED_PREPARE || P9_NAV_PRICING_RESUME
    c::ClearedBankWork work;
    std::uint32_t seed=0x91bcedu;
    const auto next=[&](){seed=1664525u*seed+1013904223u;return seed;};
    for(int trial=0;trial<320;++trial) {
        std::vector<Point> geometry;std::vector<int> boundary;
        const int count=1+trial%40;
        for(int i=0;i<count;++i) {
            geometry.push_back({i%8+(trial%3==0?i/8*12:0),i/8});
            boundary.push_back(next()%4==0?static_cast<int>(next()%120):-1);
        }
        work.begin(geometry,boundary);int steps=0;
        while(!work.step()) {
            check(work.result()==-1,"incomplete static pricing publishes no invented distance");
            if(++steps>count){check(false,"bounded Dijkstra terminates after every member settles");break;}
        }
        check(work.result()==c::cleared_group_bank_distance(geometry,boundary),"resumed pricing retains every connected and sealed member exactly");
    }
    work.begin({},{});check(work.step()&&work.result()==-1,"empty resumed pricing stays unknown");
    work.begin(spawns,one_exit);check(work.step()&&work.result()==-1,"mismatched resumed metadata stays unknown");
    work.begin(diagonal,one_exit);while(!work.step()){}
    check(work.result()==3,"a reused pricing state retains diagonal connectivity without old costs");
#endif
    check(c::total_shards(4,1,5)==9,"loose shards and one automatic emblem retain nine total shards");
    check(c::total_shards(0,1,5)==5,"combining a fifth shard preserves the completed five");
}
void released_bank_routes() {
    check(c::released_bank_distance(7,2,true)==7,"an existing direct bank route wins over released-spawn repricing");
    check(c::released_bank_distance(-1,5,true)==5,"a released spawn can use its known cleared-group exit");
    check(c::released_bank_distance(-1,5,false)==-1,"an uncleared spawn does not acquire a route through living camps");
    check(c::released_bank_distance(-1,-1,true)==-1,"release alone cannot invent an unknown exit");
    auto w=world();w.self.bag=30;w.altar_count=1;
    w.altars[0].id=4;w.altars[0].travel=-1;w.altars[0].route_known=false;
    check(c::nearest_bank(w)==-1,"an occupied static-blocked spawn initially has no bank route");
    w.altars[0].travel=c::released_bank_distance(-1,5,true);
    w.altars[0].route_known=w.altars[0].travel>=0;
    check(c::bank_now(w,w.altars[0].travel)&&c::nearest_bank(w)==4,
        "a loaded captured spawn exposes a usable effective altar route to the bank selector");
    w.altars[0].travel=c::released_bank_distance(-1,5,false);w.altars[0].route_known=false;
    check(c::nearest_bank(w)==-1,"uncleared effective travel remains unavailable to selection");
    w.altars[0].travel=c::released_bank_distance(-1,-1,true);
    check(c::nearest_bank(w)==-1,"unknown cleared exit remains unavailable to selection");
    w.altars[0].travel=5;w.altars[0].route_known=true;w.altars[0].enemy_lock=true;
    w.altar_count=2;w.altars[1].id=7;w.altars[1].travel=12;w.altars[1].route_known=true;
    check(c::nearest_bank(w)==7,"an unlocked bank is preferred over a nearer enemy lock");
    check(c::nearest_bank(w,4)==4,"a reachable requested bank preserves the explicit destination");
    w.altars[0].enemy_lock=false;
    check(c::nearest_bank(w,99)==4,"an unavailable requested bank falls back to the nearest usable altar");
}
void competitive_movement_and_bootstrap() {
    auto w=world();w.group_count=2;
    w.groups[0]=group(1,0,2,0,{2,2});w.groups[0].gold=100;
    w.groups[1]=group(2,0,6,0,{20,20});w.groups[1].gold=180;
    check(c::gold_source(w)==2,"a farther gold camp can offer higher actual income per capture cycle");
    w.groups[1]=group(2,0,2,0,{20,20});w.groups[1].gold=80;
    check(c::gold_source(w)==1,"an empty higher-throughput gold camp wins without crowding");
    w.ally_count=1;w.allies[0]=w.self;w.allies[0].id=11;w.allies[0].position={2,3};
    check(c::gold_source(w)==2,"gold bootstrap spreads away from a crowded capture");
    w.allies[0].alive=false;
    check(c::gold_source(w)==1,"a dead ally does not create a gold crowd penalty");
    w.groups[0].gold=w.groups[1].gold=80;
    check(c::gold_source(w)==1,"equal gold throughput has a deterministic group-ID tie");

    check(!c::safe_progress(0,100),"movement from safety into enemy damage is blocked");
    check(c::safe_progress(100,100),"equal exposure remains legal while escaping a wide threat ring");
    check(c::safe_progress(100,50),"movement that lowers incoming damage is allowed");
    check(c::safe_progress(0,0),"safe ordinary route progress is allowed");
    check(!c::safe_progress(100,std::numeric_limits<double>::quiet_NaN()),"unknown projected damage does not license movement");
    check(!c::retreat_progress(100,100,20,21),"equal exposure cannot retreat farther from home");
    check(c::retreat_progress(100,100,20,19),"equal exposure can retreat strictly closer to home");
    check(c::retreat_progress(100,100,-1,19),"a known home route improves an unknown route at equal exposure");
    check(!c::retreat_progress(100,100,20,-1),"equal exposure cannot replace a known home route with an unknown one");
    check(c::retreat_progress(100,50,20,21),"lower incoming damage permits a step temporarily farther from home");
    check(!c::retreat_progress(100,110,20,19),"a shorter home route does not justify increased incoming damage");
    check(!c::retreat_progress(100,100,-1,-1),"two unknown home distances cannot manufacture strict retreat progress");
    check(!c::retreat_progress(100,std::numeric_limits<double>::quiet_NaN(),20,19),"nonfinite retreat damage is rejected");
    check(!c::retreat_progress(-1,0,20,19),"negative current retreat damage is invalid");
    check(!c::retreat_progress(100,-1,20,19),"negative next retreat damage is invalid");
    check(c::prospect_priority(.05,5)<c::prospect_priority(2.,8),
        "retained low safety priority prevents an unsafe jackpot beating a feasible starter source");
    check(c::prospect_priority(2.,5)>c::prospect_priority(2.,15),"equally safe prospects prefer shorter current travel");
    check(c::prospect_priority(-1.,5)==0,"invalid negative source priority is not an attractive prospect");
}
void recent_damage_and_camp_modifiers() {
    c::DamageMemory memory;
    memory.observe(10,100,true);
    check(!memory.recently_hurt(10),"first live HP sample does not invent earlier damage");
    memory.observe(11,90,true);
    check(memory.recently_hurt(11),"a contiguous owner-turn HP drop records damage");
    memory.observe(12,90,true);
    check(memory.recently_hurt(12),"one quiet turn preserves the damage signal");
    memory.observe(13,100,true);
    check(memory.recently_hurt(13),"healing does not erase recent damage");
    memory.observe(14,100,true);memory.observe(15,100,true);
    check(memory.recently_hurt(15),"four quiet owner turns retain the retreat signal");
    memory.observe(16,100,true);
    check(!memory.recently_hurt(16),"damage expires after the four quiet turns");
    memory.observe(17,95,true);
    check(memory.recently_hurt(17),"new damage refreshes an expired signal");
    memory.observe(19,80,true);
    check(!memory.recently_hurt(19),"a missed owner turn invalidates HP-loss continuity");
    memory.observe(20,70,true);
    check(memory.recently_hurt(20),"continuous samples after a gap can establish new damage");
    memory.observe(21,0,false);
    check(!memory.recently_hurt(21),"death clears the damage signal");
    memory.observe(22,100,true);
    check(!memory.recently_hurt(22),"respawn health is a fresh baseline");
    memory.observe(23,std::numeric_limits<double>::quiet_NaN(),true);memory.observe(24,50,true);
    check(!memory.recently_hurt(24),"a nonfinite sample invalidates continuity");
    memory.observe(24,20,true);
    check(!memory.recently_hurt(24),"same-turn samples do not invent owner-boundary damage");
    memory.observe(25,40,true);
    check(memory.recently_hurt(25),"same-turn refresh does not overwrite the original HP baseline");
    check(!memory.recently_hurt(24),"damage is not reported before its observed round");

    c::DamageMemory farming;
    farming.observe(10,1000,true);farming.observe(11,920,true,80);
    check(!farming.recently_hurt(11),"expected safe camp damage does not turn a final kill into hidden-fire retreat");
    farming.observe(12,920,true);
    check(!farming.recently_hurt(12),"the quiet cleared-camp capture turn preserves a normal payout hold");
    farming.observe(13,800,true,80);
    check(farming.recently_hurt(13),"loss beyond expected camp damage records an additional hidden burst");
    farming.observe(14,720,true,80);
    check(farming.recently_hurt(14),"later expected damage does not erase an existing unexpected-hit signal");
    c::DamageMemory tolerance;
    tolerance.observe(1,100,true);tolerance.observe(2,89.5,true,10);
    check(!tolerance.recently_hurt(2),"half a health point of tolerance does not create a hidden hit");
    tolerance.observe(3,78.9,true,10);
    check(tolerance.recently_hurt(3),"damage above the expected loss and tolerance records a hit");
    c::DamageMemory invalid_expected;
    invalid_expected.observe(1,100,true);invalid_expected.observe(2,90,true,std::numeric_limits<double>::quiet_NaN());
    check(invalid_expected.recently_hurt(2),"nonfinite expected damage cannot hide a real health drop");

    auto w=world();auto g=group(1,20);g.hp=900;g.incoming=g.camp_incoming=80;
    check(!safe_for(g,w.self),"unmodified long camp clear fails the survival reserve");
    w.self.camp_attack_multiplier=2;
    check(safe_for(g,w.self),"camp attack bonus shortens the supported damage exposure");
    w.self.camp_attack_multiplier=1;w.self.camp_defense_multiplier=.5;
    check(safe_for(g,w.self),"camp defense bonus reduces actual camp incoming damage");
    g.incoming=120;g.camp_incoming=80;
    check(!safe_for(g,w.self),"camp defense does not discount the additional enemy champion burst");
    g.incoming=100;g.camp_incoming=0;w.self.camp_defense_multiplier=.01;
    check(!safe_for(g,w.self),"pure enemy damage retains full strength despite camp defense");
    g.cleared=true;g.incoming=600;g.camp_incoming=0;
    check(!safe_for(g,w.self),"cleared capture safety also preserves full enemy damage");
}
void partial_clear_boundaries() {
    auto w=world();auto g=group(1,20);g.hp=900;g.incoming=g.camp_incoming=80;
    w.self.hp=750;g.safe_solo=false;
    check(!safe_for(g,w.self)&&c::partial_clear_step(w,g,false,4),"partial admission prices one attack instead of inventing a full clear");
    w.self.hp=749;
    check(!c::partial_clear_step(w,g,false,4)&&c::partial_clear_step(w,g,true,4),"new admission requires75percent health while an actual commitment retains its exit budget");
    w.self.hp=560;
    check(c::partial_clear_step(w,g,true,4),"exact attack-plus-exit damage boundary is admitted");
    w.self.hp=559;w.ally_count=1;w.allies[0]=w.self;w.allies[0].focus=10000;
    check(!c::partial_clear_step(w,g,true,4),"an allied healer cannot fund missing personal escape health");
    w.self.hp=750;
    check(!c::partial_clear_step(w,g,true,-1)&&!c::partial_clear_step(w,g,true,100),"unknown or unaffordable escape length rejects the attack");
    g.contested=true;check(!c::partial_clear_step(w,g,true,4),"visible contest blocks partial attacks");g.contested=false;
    w.extra_opposition=true;check(c::partial_clear_step(w,g,true,4),"additional distant visibility does not invent local camp contest");w.extra_opposition=false;
    g.observed=false;check(!c::partial_clear_step(w,g,true,4),"unobserved source health cannot fund partial work");g.observed=true;
    g.respawning=true;check(!c::partial_clear_step(w,g,true,4),"respawning sources cannot be attacked");g.respawning=false;
    g.living=false;g.cleared=true;check(!c::partial_clear_step(w,g,true,4),"a cleared source uses capture handling instead");g.living=true;g.cleared=false;
    g.gems=0;check(!c::partial_clear_step(w,g,true,4),"gold-only loops do not receive partial-clear permission");g.gems=20;
    g.route_known=false;check(!c::partial_clear_step(w,g,true,4),"missing route blocks partial admission");g.route_known=true;
    w.self.hp=340;w.self.camp_defense_multiplier=.5;
    check(c::partial_clear_step(w,g,true,4),"camp mitigation preserves the exact30percent residual health floor");
    --w.self.hp;check(!c::partial_clear_step(w,g,true,4),"one point below the personal health floor rejects continuation");
    w.self.hp=750;g.camp_incoming=-1;w.self.camp_defense_multiplier=1;
    check(c::partial_clear_step(w,g,false,4),"unknown camp split conservatively charges the whole incoming estimate");
    g.camp_incoming=std::numeric_limits<double>::quiet_NaN();
    check(!c::partial_clear_step(w,g,true,4),"nonfinite damage cannot create an affordable attack");
}
}
int main() {
    source_selection();banking();exploration();cleared_geometry_and_items();released_bank_routes();competitive_movement_and_bootstrap();recent_damage_and_camp_modifiers();partial_clear_boundaries();
    std::cout<<"{\"suite\":\"plan9-competition\",\"passed\":true,\"checks\":"<<checks
             <<",\"engineLaunches\":0}\n";
}
