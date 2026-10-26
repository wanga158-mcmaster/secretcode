#include "../timetable/src/timetable.h"
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace plan4;
namespace t = plan4::timetable;
namespace {
int checks = 0;
void check(bool value,const char* message) {
    ++checks; if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}
}
struct Map { Point blocked{-1,-1}, unknown{-1,-1}; };
t::Tile tile(Point p,const void* context) {
    const auto& map=*static_cast<const Map*>(context);
    if(p.x<0||p.y<0||p.x>=20||p.y>=20||p==map.blocked)return t::Tile::blocked;
    return p==map.unknown?t::Tile::unknown:t::Tile::walkable;
}
t::ActorSample actor(int id,Point p,int round,t::TurnOrder order={true,true,true}) {
    return {id,p,{round,Source::personal_vision,Status::exact_now},order,true};
}
void observe_pair(t::State& state,Point previous,Point current,t::TurnOrder order={true,true,true},
                  int prior_round=9,int own=9,Point own_position={0,0}) {
    auto before=actor(1,previous,prior_round,order);state.observe(prior_round,own,own_position,&before,1);
    auto now=actor(1,current,10,order);state.observe(10,own,own_position,&now,1);
}
t::RouteWindow route(const Point* steps,int count,Point origin={0,0},int first_round=10) {
    return {10,first_round,origin,steps,count,true,true};
}
void moving_vertex_and_negative() {
    Map map;const t::Topology topology{tile,&map};
    t::State active,control(false);observe_pair(active,{0,2},{1,2});observe_pair(control,{0,2},{1,2});
    const std::array<Point,2> steps{{{1,1},{2,2}}};
    const auto positive=active.quote(route(steps.data(),2),topology);
    const auto disabled=control.quote(route(steps.data(),2),topology);
    check(positive.supported&&positive.wait_phases==1&&disabled.wait_phases==0,
          "fresh action-time occupancy creates active/control delay difference on identical route");
    check(positive.conflict_count==1&&positive.conflicts[0].arrival_offset==1
        &&positive.conflicts[0].other_position==Point{2,2},"conflict trace records predicted vertex and arrival");
    check(active.project(0,0,topology).position==Point{1,2}
        &&active.project(0,0,topology).status==Status::exact_now,
          "first current-turn arrival always uses actual current occupancy");
    check(active.project(0,1,topology).status==Status::predicted,"future positions remain predictions");
    const auto staggered=active.quote(route(steps.data(),2,{0,0},13),topology);
    check(staggered.supported&&staggered.wait_phases==0&&staggered.outside_horizon,
          "traffic staggered three rounds never borrows future occluded positions");
    t::State stale;observe_pair(stale,{0,2},{1,2},{true,true,true},7);
    const auto old=stale.quote(route(steps.data(),2),topology);
    check(old.wait_phases==0&&old.unknown_motion,"previous observation older than two rounds falls back to current occupancy");
    auto hidden=actor(1,{1,2},11);hidden.personally_visible=false;active.observe(11,9,{0,0},&hidden,1);
    check(active.count()==0,"losing current personal sight removes actor from prediction");
    auto fresh=actor(1,{2,2},12);active.observe(12,9,{0,0},&fresh,1);
    check(!active.track(0)->has_previous,"reappearing actor cannot resurrect occluded motion history");
    t::State low_rank;observe_pair(low_rank,{0,2},{1,2},{true,true,true},9,0);
    check(low_rank.quote(route(steps.data(),2),topology).wait_phases==0,
          "stable public lower actor rank resolves predicted contention without extra wait");
}
void sequential_order_and_stops() {
    Map map;const t::Topology topology{tile,&map};
    const std::array<Point,2> swap_steps{{{1,0},{2,0}}};
    t::State before,after,control(false);
    observe_pair(before,{3,0},{2,0},{true,true,true});
    observe_pair(after,{3,0},{2,0},{true,true,false});
    observe_pair(control,{3,0},{2,0},{true,true,true});
    const auto vacated=before.quote(route(swap_steps.data(),2),topology);
    const auto occupied=after.quote(route(swap_steps.data(),2),topology);
    check(vacated.wait_phases==0,"sequential edge swap into already vacated tile is not simultaneous-MAPF collision");
    check(occupied.wait_phases==1&&occupied.conflicts[0].edge_swap_risky,
          "swap destination still occupied before later other turn is risky");
    check(control.quote(route(swap_steps.data(),2),topology).wait_phases==1,
          "disabled arm keeps identical current-occupancy fallback safeguard");
    t::State twice;observe_pair(twice,{0,2},{1,2},{true,false,true},9,9,{1,0});
    const std::array<Point,2> steps{{{2,1},{3,2}}};
    const auto projected=twice.project(0,1,topology);
    check(projected.position==Point{3,2}&&projected.moves_before_arrival==2,
          "alternating sequential order may give rival two turns before next owner arrival");
    check(twice.quote(route(steps.data(),2,{1,0}),topology).wait_phases==1,"two-turn rival projection reaches second arrival conflict");
    map.blocked={2,2};
    const auto stopped=twice.project(0,1,topology);
    check(stopped.position==Point{1,2}&&stopped.stopped_at_public_obstacle,
          "projection stops before public illegal destination");
    check(twice.quote(route(steps.data(),2,{1,0}),topology).wait_phases==0,
          "stopped trajectory never passes through public obstacle to invent conflict");
    map.blocked={-1,-1};map.unknown={2,2};
    const auto unknown=twice.project(0,1,topology);
    check(unknown.position==Point{1,2}&&unknown.unsupported&&!unknown.extrapolated,
          "unknown topology falls back to current occupancy instead of invented traversal");
    t::State unordered;observe_pair(unordered,{0,2},{1,2},{false,false,true});
    check(unordered.project(0,1,topology).unsupported,"unknown turn order cannot authorize sequential trajectory assumption");
    map.unknown={-1,-1};
    for(int already=0;already<2;++already)for(int next=0;next<2;++next){
        t::State ordered;observe_pair(ordered,{0,2},{1,2},{true,bool(already),bool(next)});
        const auto p=ordered.project(0,1,topology);
        const int moves=(!already)+next;
        check(p.moves_before_arrival==moves&&p.position==Point{1+moves,2},
              "all public sequential-order combinations preserve exact intervening move count");
    }
    t::State unit;
    observe_pair(unit,{0,0},{5,5},{true,true,true},8);
    check(unit.project(0,1,topology).position==Point{6,6},
          "two-round displacement uses signed unit coordinates rather than extrapolating an unobserved dash");
    const auto missing=before.quote(route(swap_steps.data(),2),{});
    check(!missing.supported,"missing public route topology uses M0 fallback");
}
void selection_and_invalidation() {
    t::State state;
    std::array<t::ActorSample,6> samples{{actor(5,{7,5},10),actor(4,{6,6},10),actor(3,{4,5},10),
        actor(2,{5,4},10),actor(1,{5,6},10),actor(0,{0,0},10)}};
    state.observe(10,99,{5,5},samples.data(),int(samples.size()));
    check(state.count()==4,"tracker is capped at four current visible actors");
    for(int n=0;n<4;++n)check(state.track(n)->current.id==n+1,"selection uses current Chebyshev distance then public ID");
    std::reverse(samples.begin(),samples.end());t::State permuted;
    permuted.observe(10,99,{5,5},samples.data(),int(samples.size()));
    for(int n=0;n<4;++n)check(state.track(n)->current.id==permuted.track(n)->current.id,"input permutation cannot change current nearest-actor selection");
    t::State repeated;observe_pair(repeated,{0,2},{1,2});
    auto same_round=actor(1,{2,2},10);repeated.observe(10,9,{0,0},&same_round,1);
    check(repeated.track(0)->previous_round==9&&repeated.track(0)->previous==Point{0,2},
          "same-round refresh retains older witness instead of manufacturing zero-age velocity");
    auto model=actor(1,{3,2},11);model.provenance.source=Source::model;
    repeated.observe(11,9,{0,0},&model,1);
    check(repeated.count()==0,"predicted positions cannot enter personally observed motion history");
    std::array<t::ActorSample,2> duplicate{{actor(1,{1,1},12),actor(1,{2,2},12)}};
    check(!repeated.observe(12,9,{0,0},duplicate.data(),2)&&!repeated.count(),"conflicting duplicate actor identity invalidates current prefix");
    check(!repeated.observe(13,9,{0,0},nullptr,1),"null nonempty actor prefix rejected");
    check(!repeated.observe(13,9,{0,0},duplicate.data(),t::max_input_actors+1),"over-capacity prefix rejected before out-of-bounds read");
    observe_pair(repeated,{0,2},{1,2});same_round=actor(1,{2,2},11);
    repeated.observe(11,8,{0,0},&same_round,1);
    check(!repeated.track(0)->has_previous,"actor identity change discards another actor's motion history");
}
void bounded_actual_waits() {
    Map map;const t::Topology topology{tile,&map};t::State active,control(false);
    observe_pair(active,{1,0},{1,0});observe_pair(control,{1,0},{1,0});
    const Point step{1,0};const auto input=route(&step,1);
    const auto quote=active.quote(input,topology);
    check(quote.wait_phases==2&&quote.fallback_after_two&&control.quote(input,topology).wait_phases==2,
          "stationary current blocker receives identical bounded waits in both arms");
    t::WaitLedger ledger;
    for(int n=0;n<2;++n)check(t::bounded_wait(quote,ledger,77,true).wait_one_owner_phase,"safe actual wait consumes one owner phase");
    const auto third=t::bounded_wait(quote,ledger,77,true);
    check(!third.wait_one_owner_phase&&third.use_fresh_ordinary_route&&ledger.used==2,
          "same decision cannot wait forever and returns to freshly checked ordinary route");
    check(t::bounded_wait(quote,ledger,78,true).wait_one_owner_phase&&ledger.used==1,"new common decision key resets bounded wait counter");
    const auto unsafe=t::bounded_wait(quote,ledger,78,false);
    check(!unsafe.wait_one_owner_phase&&unsafe.use_fresh_ordinary_route&&ledger.used==1,
          "safety or channel premise blocks optional wait without consuming a phase");
    t::State low_rank;observe_pair(low_rank,{1,0},{1,0},{true,true,true},9,0);
    check(low_rank.quote(input,topology).wait_phases>=1,"rank never permits an immediately occupied move");
    auto bad=input;bad.count=9;check(!active.quote(bad,topology).supported,"more than eight cached route steps rejected safely");
    bad=input;bad.ordinary_movement=false;check(!active.quote(bad,topology).supported,"unsupported movement/channel premise uses fallback");
    bad=input;bad.origin={0,1};check(!active.quote(bad,topology).supported,"stale own route origin cannot be treated as current physical state");
}
}
int main(){moving_vertex_and_negative();sequential_order_and_stops();selection_and_invalidation();bounded_actual_waits();
    std::printf("timetable checks=%d synthetic-only legal-witness=pending runtime-order-route-binding=pending\n",checks);}
