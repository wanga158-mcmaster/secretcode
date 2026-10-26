// Actual OnlineObserver, with a deliberately limited SDK wrapper-seam spy.
// No cache or production behavior is substituted. Forced transitions are
// robustness experiments, not measurements of native suspension or fuel.
#define PLAN4_OBSERVER_STATE_PROBE
#include "../common/observer.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <tuple>
using namespace plan4;
namespace cm = cambc::mock;

auto key(const Provenance& a) { return std::tie(a.observed_round,a.source,a.status); }
auto key(const Actor& a) { return std::tie(a.id,a.position,a.health,a.maximum_health,a.bag,a.gold,
    a.channel,a.channel_ticks,a.alive,a.on_spawn,a.recall_safe,a.escape_rounds,a.escape_incoming); }
auto key(const Settings& a) { return std::tie(a.max_rounds,a.winning_gems,a.capture_rounds,a.recall_rounds,
    a.tribute_multiplier,a.tribute_base,a.tribute_minimum,a.spawn_heal_fraction); }
auto key(const Group& a) { return std::tie(a.id,a.position,a.full_health,a.health,a.clear_rounds,a.full_clear_rounds,
    a.incoming_damage,a.full_incoming_damage,a.gems,a.gold,a.respawn_rounds,a.lifecycle,
    a.visible,a.all_spawns_visible,a.reward_available,a.contested,a.occupied_spawn,a.occupancy_known,
    a.capture_started_round,a.respawn_remaining,a.eligible_allied_occupants,a.own_is_lowest_occupant,
    a.clear_damage_per_round); }
auto key(const Altar& a) { return std::tie(a.id,a.position,a.altar_position,a.accessible,a.hostile_lock,
    a.lock_known,a.remove_lock_rounds); }
auto key(const Information& a) { return std::tie(a.round,a.own_spawn,a.our_score,a.enemy_score,
    a.group_count,a.altar_count,a.travel.distance,a.travel.context,a.travel.route_prefix,a.travel.symmetric_distance,
    a.work_budget.can_refine,a.work_budget.context,a.in_progress_group_id,a.retained_group_id,
    a.retained_second_group_id,a.retained_altar_id,a.observation_truncated,a.public_layout_truncated); }
auto key(const ObserverDiagnostics& a) { return std::tie(a.stop_stage,a.stop_group,a.stop_member,a.groups,a.visited,
    a.collection_fuel,a.stop_floor,a.stop_fuel,a.stop_margin); }
auto key(const Job& a) { return std::tie(a.kind,a.group,a.second_group,a.altar,a.menu_rank,a.nominal_travel,a.nominal_completion); }
auto key(const Event& a) { return std::tie(a.kind,a.start.round,a.start.phase,a.end.round,a.end.phase,a.group_id,
    a.bag_before,a.bag_after,a.secured,a.gold_after,a.health_after,a.status); }
auto key(const Forecast& a) { return std::tie(a.event_count,a.macros_started,a.final_round,a.first_conversion_round,
    a.our_secured,a.enemy_secured,a.final_bag,a.final_gold,a.final_health,a.terminal_z,a.terminal_unknown,
    a.stock_unknown,a.travel_unknown,a.lock_unknown,a.truncated,a.completed_conversion,a.no_conversion,
    a.budget_exhausted,a.abort_reason,a.objective.paths,a.objective.terminal_z,a.objective.our_secured,
    a.objective.enemy_secured,a.objective.first_conversion_round); }
void equal(const Menu& a,const Menu& b) {
    assert(std::tie(a.count,a.single_count,a.truncated)==std::tie(b.count,b.single_count,b.truncated));
    for(int n=0;n<a.count;++n)assert(key(a.jobs[n])==key(b.jobs[n]));
}
void equal(const Information& a,const Information& b) {
    assert(key(a)==key(b));assert(key(a.own)==key(b.own));assert(key(a.settings)==key(b.settings));
    for(int n=0;n<a.group_count;++n) {
        assert(key(a.groups[n])==key(b.groups[n]));
        assert(key(a.groups[n].provenance)==key(b.groups[n].provenance));
        assert(key(a.groups[n].health_provenance)==key(b.groups[n].health_provenance));
    }
    for(int n=0;n<a.altar_count;++n)assert(key(a.altars[n])==key(b.altars[n]));
}
void equal(const Evaluation& a,const Evaluation& b) {
    equal(a.menu,b.menu);
    assert(std::tie(a.chosen,a.evaluated_count,a.budget_exhausted)==std::tie(b.chosen,b.evaluated_count,b.budget_exhausted));
    for(int n=0;n<a.evaluated_count;++n) {
        const auto& x=a.forecasts[n];const auto& y=b.forecasts[n];assert(key(x)==key(y));
        for(int e=0;e<x.event_count;++e)assert(key(x.events[e])==key(y.events[e]));
    }
}
int travel(Point a,Point b,const void*) { return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y)); }
enum class Case { live, dead_partial, hidden, round_only, state_only, round_state, actor,
    throw_first, throw_second, packet_error, floor1999, floor2000 };
Case current_case;
OnlineObserver* current_observer=nullptr;
void before_state(int id,int ordinal) {
    if(id!=100)return;
    if((current_case==Case::throw_first&&ordinal==1)||(current_case==Case::throw_second&&ordinal==2))
        throw std::runtime_error("state wrapper probe error");
    if(ordinal!=2)return;
    if(current_case==Case::round_only||current_case==Case::round_state)++cm::round;
    if(current_case==Case::state_only||current_case==Case::round_state)
        cm::camp_health_overrides.push_back({100,0});
    if(current_case==Case::actor)cm::self_id=1;
}
void after_state(int id,int ordinal,bool) {
    if(id!=100||ordinal!=1)return;
    if(current_case==Case::floor1999||current_case==Case::floor2000) {
        const auto floor=current_observer->diagnostics().collection_fuel-26000;
        cm::fuel=int(floor)+(current_case==Case::floor1999?1999:2000);
    }
}
struct Result {
    std::unique_ptr<Information> info=std::make_unique<Information>();
    std::unique_ptr<Evaluation> eval=std::make_unique<Evaluation>();
    Menu menu{};ObserverDiagnostics diagnostics{};
    std::vector<cm::StateRead> reads;
    int packet_reads=0,wrapper_reads=0,final_round=0,final_actor=0,unauthorized=0;
    bool ok=false,threw=false,evaluated=false;
};
Result run(Case kind,bool hooks) {
    cm::reset();cambc::map::mock_groups.clear();current_case=kind;
    cm::fuel=200000;cm::query_cost=1;cm::visible.fill(true);
    cm::actors={{0,{4,5},true,true,cambc::team::ally},{1,{30,30},true,true,cambc::team::ally}};
    cm::actor(0).bag=12;cm::actor(0).gold=37;
    cm::altar_positions={{1,1}};cm::allied_spawns={{0,0}};cm::states[0]=0;
    std::vector<cambc::camp> members;
    members.emplace_back(100,cambc::vec2{5,5},kind==Case::dead_partial?0.f:100.f);
    members.emplace_back(101,cambc::vec2{6,5},100.f);
    cambc::map::mock_groups.emplace_back(0,std::move(members));
    if(kind==Case::hidden)cm::visible[cm::tile({5,5})]=false;
    if(kind==Case::packet_error){cm::camp_raw_error_id=100;cm::camp_raw_error=97;cm::camp_raw_error_payload=true;}
    if(kind==Case::floor1999||kind==Case::floor2000)cm::query_cost=0;
    cm::before_state=hooks?before_state:nullptr;cm::after_state=hooks?after_state:nullptr;
    auto observer=std::make_unique<OnlineObserver>();current_observer=observer.get();Result out;
    try { out.ok=observer->observe_into(*out.info,{travel,nullptr,nullptr,true}); }
    catch(const std::runtime_error&) { out.threw=true; }
    current_observer=nullptr;out.diagnostics=observer->diagnostics();
    out.reads=cm::state_reads;out.packet_reads=cm::camp_raw_queries;out.wrapper_reads=cm::camp_state_queries;
    out.final_round=cm::round;out.final_actor=cm::self_id;out.unauthorized=cm::unauthorized;
    // Exercise unchanged menu, ranking and M0 on completed same-clock fixtures.
    // No suspended or exception result is offered to a caller here.
    if(out.ok&&!out.threw&&!out.info->observation_truncated&&out.info->round==cm::round&&cm::self_id==0) {
        out.menu=make_menu(*out.info);evaluate_into(*out.info,out.menu,*out.eval);out.evaluated=true;
    }
    cm::before_state=nullptr;cm::after_state=nullptr;return out;
}
int attempts(const Result& r,int id) { return int(std::count_if(r.reads.begin(),r.reads.end(),[&](auto x){return x.id==id;})); }
void print(const char* label,const Result& r) {
    std::printf("observer-boundary case=%s ok=%d threw=%d entryRound=%d finalRound=%d entryActor=%d finalActor=%d state100=%d state101=%d packet=%d health=%.0f exact=%d recall=%d truncated=%d evaluated=%d\n",
        label,r.ok,r.threw,r.info->round,r.final_round,r.info->own.id,r.final_actor,attempts(r,100),attempts(r,101),
        r.packet_reads,r.info->groups[0].health,r.info->groups[0].health_provenance.status==Status::exact_now,
        r.info->own.recall_safe,r.info->observation_truncated,r.evaluated);
}
void exact_group(const Result& r,float hp,bool recall) {
    assert(r.ok&&!r.threw&&!r.info->observation_truncated&&!r.info->public_layout_truncated);
    assert(r.info->group_count==1&&r.info->altar_count==1&&r.unauthorized==0);
    const auto& g=r.info->groups[0];assert(g.lifecycle==Lifecycle::living&&g.health==hp);
    assert(g.provenance.observed_round==10&&g.provenance.source==Source::personal_vision&&g.provenance.status==Status::exact_now);
    assert(key(g.health_provenance)==key(g.provenance));assert(r.info->own.recall_safe==recall);
}
int main() {
    // Normal no-op probe twins compare ALL represented Information fields,
    // diagnostics, complete menu, every evaluated forecast/event and choice.
    const Case bases[]={Case::live,Case::dead_partial,Case::hidden};
    const char* labels[]={"live","dead-partial","hidden"};
    for(int n=0;n<3;++n) {
        auto a=run(bases[n],false),b=run(bases[n],true);
        equal(*a.info,*b.info);assert(key(a.diagnostics)==key(b.diagnostics));
        assert(a.evaluated&&b.evaluated);equal(a.menu,b.menu);equal(*a.eval,*b.eval);
        assert(a.wrapper_reads==b.wrapper_reads&&a.packet_reads==b.packet_reads&&a.unauthorized==0&&b.unauthorized==0);
        if(n==0){exact_group(b,200,false);assert(attempts(b,100)==2&&attempts(b,101)==1&&b.packet_reads==2);}
        if(n==1){exact_group(b,100,true);assert(attempts(b,100)==2&&attempts(b,101)==1&&b.packet_reads==1);}
        if(n==2){assert(attempts(b,100)==0&&attempts(b,101)==1&&b.packet_reads==1);
            assert(!b.info->own.recall_safe&&b.info->groups[0].health==200);
            assert(b.info->groups[0].health_provenance.status==Status::unknown&&!b.info->groups[0].all_spawns_visible);}
        print(labels[n],b);
    }
    auto round=run(Case::round_only,true);exact_group(round,200,false);
    assert(round.final_round==11&&round.info->round==10&&!round.evaluated);print("round-only",round);
    auto state=run(Case::state_only,true);exact_group(state,200,true);
    assert(state.final_round==10&&state.reads.back().id==100&&state.reads.back().dead);print("state-only",state);
    auto mixed=run(Case::round_state,true);exact_group(mixed,200,true);
    assert(mixed.final_round==11&&mixed.info->round==10&&!mixed.evaluated);
    assert(mixed.reads.front().round==10&&!mixed.reads.front().dead);
    assert(mixed.reads.back().id==100&&mixed.reads.back().round==11&&mixed.reads.back().dead);
    print("round-state",mixed);
    // Separate synthetic robustness case: a real guest has a fixed SDK self.
    auto actor=run(Case::actor,true);exact_group(actor,200,false);
    assert(actor.final_actor==1&&actor.info->own.id==0&&!actor.evaluated);print("synthetic-actor",actor);
    for(auto kind:{Case::throw_first,Case::throw_second,Case::packet_error}) {
        auto r=run(kind,true);assert(r.threw&&!r.ok&&!r.evaluated&&r.unauthorized==0);
        if(kind==Case::throw_first)assert(attempts(r,100)==1&&r.packet_reads==0);
        if(kind==Case::throw_second)assert(attempts(r,100)==2&&r.packet_reads==2);
        if(kind==Case::packet_error)assert(attempts(r,100)==1&&r.packet_reads==1);
        print(kind==Case::throw_first?"throw-first":kind==Case::throw_second?"throw-second":"packet-error",r);
    }
    auto low=run(Case::floor1999,true),edge=run(Case::floor2000,true);
    assert(low.ok&&edge.ok&&!low.threw&&!edge.threw);
    assert(low.info->observation_truncated&&edge.info->observation_truncated);
    assert(!low.info->own.recall_safe&&!edge.info->own.recall_safe);
    assert(attempts(low,100)==1&&attempts(edge,100)==2);
    assert(!low.evaluated&&!edge.evaluated&&low.unauthorized==0&&edge.unauthorized==0);
    print("floor1999",low);print("floor2000",edge);
    std::puts("observer-boundary PASS: wrapper repetitions and forced-transition validity gap; no cache, native-cost, native-suspension or applied-action claim");
}
