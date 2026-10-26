#include "../common/src/offer_storage.h"
#include "../reversible-workshop/src/native/menu_storage.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>

// Pure storage fixtures, not native execution or evidence receipts.
using namespace p8;
using namespace p8::native;
namespace {
int checks=0;
void check(bool condition,const char* message) {
    ++checks;if(!condition){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}
}
template<class T> bool same(const p6::Fact<T>& a,const p6::Fact<T>& b) {
    return a.value==b.value&&a.source==b.source;
}
bool same(const p6::Event& a,const p6::Event& b) {
    return a.kind==b.kind&&a.event_id==b.event_id&&a.tick==b.tick&&
        a.production_index==b.production_index&&same(a.gold_cost,b.gold_cost)&&
        a.waypoint_id==b.waypoint_id&&a.source==b.source;
}
bool same(const Route& a,const Route& b) {
    if(a.count!=b.count||a.verified!=b.verified||a.blocker_assumptions!=b.blocker_assumptions||
        a.count<0||a.count>max_route_tiles)return false;
    for(int i=0;i<a.count;++i)if(!(a.tiles[i]==b.tiles[i]))return false;
    return true;
}
bool same(const Plan& a,const Plan& b) {
    if(a.organization!=b.organization||a.group!=b.group||a.collector!=b.collector||a.altar!=b.altar||
        a.crew_count!=b.crew_count||a.crew_count<0||a.crew_count>6||
        a.forecast_rounds!=b.forecast_rounds||a.remaining_rounds!=b.remaining_rounds||
        a.capture_window!=b.capture_window||a.gem_bearing!=b.gem_bearing||
        a.complete_verified!=b.complete_verified||a.rally_legal!=b.rally_legal)return false;
    for(int i=0;i<a.crew_count;++i)if(a.crew[i]!=b.crew[i]||!(a.pads[i]==b.pads[i]))return false;
    return same(a.ingress,b.ingress)&&same(a.capture,b.capture)&&same(a.bank,b.bank)&&
        same(a.renewal,b.renewal)&&same(a.release,b.release);
}
bool same(const p6::Schedule& a,const p6::Schedule& b) {
    if(a.actor_count!=b.actor_count||a.production_count!=b.production_count||a.target_id!=b.target_id||
        a.actor_count<0||a.actor_count>p6::max_actors||a.production_count<0||a.production_count>p6::max_productions||
        a.immediate_win.actor_id!=b.immediate_win.actor_id||
        a.immediate_win.tribute_event_id!=b.immediate_win.tribute_event_id||
        !same(a.immediate_win.legal_now,b.immediate_win.legal_now)||
        !same(a.terminal_tiebreak.earned_gold_delta,b.terminal_tiebreak.earned_gold_delta)||
        !same(a.terminal_tiebreak.kills_delta,b.terminal_tiebreak.kills_delta)||
        a.terminal_tiebreak.matched_opponent_continuation!=b.terminal_tiebreak.matched_opponent_continuation)return false;
    for(int i=0;i<a.actor_count;++i) {
        const auto& x=a.actors[i];const auto& y=b.actors[i];
        if(x.actor_id!=y.actor_id||!same(x.initial_bag,y.initial_bag)||!same(x.initial_gold,y.initial_gold)||
            !same(x.occupied_turns,y.occupied_turns)||x.event_count!=y.event_count||
            x.event_count<0||x.event_count>p6::max_actor_events)return false;
        for(int e=0;e<x.event_count;++e)if(!same(x.events[e],y.events[e]))return false;
    }
    for(int i=0;i<a.production_count;++i) {
        const auto& x=a.productions[i];const auto& y=b.productions[i];
        if(x.event_id!=y.event_id||x.tick!=y.tick||!same(x.gems,y.gems)||!same(x.gold,y.gold)||
            x.holder_count!=y.holder_count||x.source!=y.source||x.holder_count<0||x.holder_count>p6::max_actors)return false;
        for(int h=0;h<x.holder_count;++h)if(x.holders[h]!=y.holders[h])return false;
    }
    return true;
}
bool same_headers(const common::Offer& a,const common::Offer& b) {
    return same(a.plan,b.plan)&&same(a.schedule,b.schedule)&&a.safety_count==b.safety_count&&
        a.context_slot==b.context_slot&&a.entry==b.entry&&a.diagnostic_segment==b.diagnostic_segment&&
        a.marked_extra_turns==b.marked_extra_turns&&a.capital_cost==b.capital_cost;
}
struct Fixtures { common::Offer source{},out{},oracle{},fresh{}; };
using RouteMember=Route Plan::*;
constexpr std::array<RouteMember,5> route_members{&Plan::ingress,&Plan::capture,&Plan::bank,&Plan::renewal,&Plan::release};
void dirty(common::Offer& offer) {
    auto& p=offer.plan;
    p.organization=Organization::J;p.group=77;p.collector=51;p.altar=92;p.crew_count=6;
    for(int i=0;i<6;++i){p.crew[i]=51+i;p.pads[i]={81+i,91+i};}
    for(const auto member:route_members) {
        auto& r=p.*member;r.count=max_route_tiles;r.blocker_assumptions=98765;r.verified=true;
        for(int i=0;i<max_route_tiles;++i)r.tiles[i]={501+i,601+i};
    }
    p.forecast_rounds=119;p.remaining_rounds=901;p.capture_window=17;
    p.gem_bearing=p.complete_verified=p.rally_legal=true;
    auto& s=offer.schedule;s.actor_count=p6::max_actors;s.production_count=p6::max_productions;s.target_id=77;
    for(int i=0;i<p6::max_actors;++i) {
        auto& a=s.actors[i];a.actor_id=51+i;a.initial_bag={777,p6::Source::self_observed};
        a.initial_gold={778,p6::Source::visible_subject};a.occupied_turns={779,p6::Source::conservative};
        a.event_count=p6::max_actor_events;
        for(int e=0;e<p6::max_actor_events;++e)a.events[e]={p6::EventKind::purchase,800+i*10+e,80+e,e,
            {900+e,p6::Source::queried_setting},901+e,p6::Source::scheduled};
    }
    for(int i=0;i<p6::max_productions;++i) {
        auto& pdt=s.productions[i];pdt.event_id=1000+i;pdt.tick=80+i;
        pdt.gems={200+i,p6::Source::public_map};pdt.gold={300+i,p6::Source::queried_setting};
        pdt.holder_count=p6::max_actors;pdt.source=p6::Source::scheduled;
        for(int h=0;h<p6::max_actors;++h)pdt.holders[h]=51+h;
    }
    s.immediate_win={51,801,{true,p6::Source::self_observed}};
    s.terminal_tiebreak={{987,p6::Source::private_history},{98,p6::Source::public_score},true};
    offer.safety_count=p6::max_actors;offer.context_slot=7;
    for(int i=0;i<p6::max_actors;++i) {
        auto& c=offer.safety[i];c.clock={71,51,9};c.actor=51+i;c.group=77;
        c.route_known=c.bank_executable=c.renewal_reachable=c.solo=true;
        for(auto* exposure:{&c.clear,&c.hold,&c.bank,&c.recovery}) {
            exposure->health=750;exposure->max_health=1000;exposure->incoming_total=80;
            exposure->next_phase_incoming=10;exposure->own_timed_shield=20;
            exposure->assigned_timed_heal=30;exposure->observations_complete=true;
        }
    }
    auto& k=offer.entry;k.settings_id=41;k.route_hash=42;k.return_route_hash=43;
    k.actor_id=51;k.champion=5;k.role=6;k.source_group=77;k.route_template=8;k.endpoint=92;k.threat_regime=9;
    k.participant_count=6;k.participants={51,52,53,54,55,56};k.build={44,1000,10.5,12.25,.25};
    k.hp_band=HealthBand::half_to_three_quarters;
    offer.diagnostic_segment=true;offer.marked_extra_turns=8;offer.capital_cost=999;
}
void solo(common::Offer& offer) {
    auto& p=offer.plan;p.organization=Organization::U;p.group=7;p.collector=1;p.altar=42;
    p.crew_count=1;p.crew[0]=1;p.pads[0]={1,1};
    for(const auto member:route_members) {
        auto& r=p.*member;r.count=member==&Plan::release?0:1;
        r.verified=r.count!=0;r.blocker_assumptions=0;r.tiles[0]={1,1};
    }
    p.forecast_rounds=16;p.remaining_rounds=100;p.capture_window=5;
    p.gem_bearing=p.complete_verified=true;p.rally_legal=false;
    auto& s=offer.schedule;s.actor_count=1;s.production_count=1;s.target_id=7;
    auto& a=s.actors[0];a.actor_id=1;a.initial_bag={0,p6::Source::self_observed};
    a.initial_gold={0,p6::Source::self_observed};a.occupied_turns={4,p6::Source::scheduled};a.event_count=3;
    a.events[0]={p6::EventKind::production,10,2,0,{},-1,p6::Source::scheduled};
    a.events[1]={p6::EventKind::tribute,11,3,-1,{},-1,p6::Source::scheduled};
    a.events[2]={p6::EventKind::renewal,12,4,-1,{},7,p6::Source::scheduled};
    auto& pdt=s.productions[0];pdt.event_id=10;pdt.tick=2;pdt.gems={12,p6::Source::public_map};
    pdt.gold={4,p6::Source::public_map};pdt.holders[0]=1;pdt.holder_count=1;pdt.source=p6::Source::scheduled;
    s.immediate_win={};s.terminal_tiebreak={};
}
p6::ScoreEvaluation evaluate(const p6::Schedule& schedule) {
    p6::ScenarioWindows windows;windows.deadlines={1990,1990,1990};windows.windows={120,120,120};
    windows.sampled_round=10;windows.sampled_actor_id=1;windows.source=p6::Source::synthetic_fixture;windows.valid=true;
    return p6::evaluate_schedule(schedule,windows,{2000,1000,5},{10,1},0);
}
void compare_copy(Fixtures& f) {
    f.oracle=f.source; // Independent old whole-object assignment oracle.
    check(copy_plan_into(f.out.plan,f.source.plan),"bounded Plan payload copied");
    check(copy_schedule_into(f.out.schedule,f.source.schedule),"bounded Schedule payload copied");
    check(same(f.out.plan,f.oracle.plan),"Plan active payload matches whole-copy oracle");
    check(same(f.out.schedule,f.oracle.schedule),"Schedule active payload matches whole-copy oracle");
    check(same(f.source.plan,f.oracle.plan)&&same(f.source.schedule,f.oracle.schedule),"copy leaves source active payload unchanged");
}
[[gnu::noinline]] void reset_and_reuse() {
    auto f=std::make_unique<Fixtures>();dirty(f->out);
    const auto old_tile=f->out.plan.ingress.tiles.back();const auto old_event=f->out.schedule.actors[5].events[6];
    const auto old_holder=f->out.schedule.productions[11].holders[5];
    reset_offer(f->out);
    check(same_headers(f->out,f->fresh),"dirty offer reset has every fresh active scalar and optional header");
    check(f->out.entry==ContextKey{},"reset clears complete context including inactive participant sentinels");
    check(common::certificate(f->out,51)==nullptr,"zero safety_count hides old observations and certificates");
    check(!plan_valid(f->out.plan,1)&&!evaluate(f->out.schedule).eligible(),"reset cannot reuse old productive permission");
    check(f->out.plan.ingress.tiles.back()==old_tile&&same(f->out.schedule.actors[5].events[6],old_event)&&
        f->out.schedule.productions[11].holders[5]==old_holder,"inactive payload remains initialized and untouched");
    solo(f->source);compare_copy(*f);
    check(plan_valid(f->out.plan,1),"reused Plan retains a complete U route");
    const auto score=evaluate(f->out.schedule);
    check(score.eligible()&&score.min_margin==12,"reused complete U schedule retains feasible accounted income");
    check(f->out.plan.crew[5]==56&&f->out.schedule.actors[5].actor_id==56,
        "shorter live counts hide old crew and actor tails without whole-array copies");
    check(f->out.plan.ingress.tiles.back()==old_tile,"shorter route copy leaves inactive initialized tiles untouched");
    f->out.safety_count=1;f->out.safety[0].actor=1;f->out.context_slot=3;f->out.entry=f->oracle.entry;
    f->out.diagnostic_segment=true;f->out.marked_extra_turns=2;f->out.capital_cost=33;
    reset_offer(f->out);
    check(same_headers(f->out,f->fresh)&&!common::certificate(f->out,1),"feasible U to unavailable removes all live headers");
    f->source=f->fresh;compare_copy(*f);
    check(same_headers(f->out,f->fresh),"empty payload replacing unavailable offer stays exactly fresh");
    dirty(f->source);compare_copy(*f);
    check(f->out.plan.crew_count==6&&f->out.schedule.actor_count==6&&f->out.schedule.production_count==12,
        "empty to maximum different crew copies every active actor and production");
    check(f->out.schedule.immediate_win.legal_now.value&&f->out.schedule.terminal_tiebreak.matched_opponent_continuation,
        "copy preserves active immediate-win and terminal metadata");
    solo(f->source);compare_copy(*f);
    check(f->out.plan.release.count==0&&!f->out.plan.release.verified&&f->out.plan.release.blocker_assumptions==0,
        "optional release header resets on active to empty replacement");
    check(!f->out.schedule.immediate_win.legal_now.known()&&
        !f->out.schedule.terminal_tiebreak.matched_opponent_continuation,"shrinking schedule resets prior win and endpoint flags");
    f->source.schedule.actors[0].event_count=0;f->source.schedule.productions[0].holder_count=0;compare_copy(*f);
    check(!evaluate(f->out.schedule).eligible(),"empty active nested counts cannot leak old events or holders");
    check(f->out.schedule.actors[0].events[2].event_id==12,"inactive events are retained behind zero nested count");
    solo(f->source);compare_copy(*f);
    check(evaluate(f->out.schedule).eligible(),"zero to active nested payload restores only supplied complete U");
}
[[gnu::noinline]] void preserved_unknowns_and_validation() {
    auto f=std::make_unique<Fixtures>();dirty(f->out);solo(f->source);
    f->source.plan.capture.verified=false;f->source.plan.capture.blocker_assumptions=123;
    f->source.schedule.actors[0].initial_bag={88,p6::Source::unknown};
    f->source.schedule.actors[0].events[1].gold_cost={44,p6::Source::unknown};
    f->source.schedule.productions[0].gold={45,p6::Source::unknown};
    f->source.schedule.immediate_win={90,91,{true,p6::Source::unknown}};
    f->source.schedule.terminal_tiebreak={{92,p6::Source::unknown},{93,p6::Source::conservative},false};
    compare_copy(*f);
    check(!plan_valid(f->out.plan,1)&&!evaluate(f->out.schedule).eligible(),"storage does not certify unknown or unverified inputs");
    solo(f->source);f->source.plan.bank.count=2;f->source.plan.bank.tiles[1]={50,50};compare_copy(*f);
    check(!route_valid(f->out.plan.bank)&&!plan_valid(f->out.plan,1),"bounded malformed geometry is still rejected downstream");
    solo(f->source);f->source.schedule.production_count=0;compare_copy(*f);
    check(!evaluate(f->out.schedule).eligible(),"inactive stale production cannot satisfy an active production reference");
    solo(f->source);f->source.schedule.actors[0].events[0].production_index=11;compare_copy(*f);
    check(!evaluate(f->out.schedule).eligible(),"stored out-of-range event reference remains invalid downstream");
    solo(f->source);f->source.plan.release.verified=true;f->source.plan.release.blocker_assumptions=91;
    compare_copy(*f);
    check(f->out.plan.release.count==0&&f->out.plan.release.verified&&f->out.plan.release.blocker_assumptions==91,
        "empty optional route copies source header exactly instead of silently correcting it");
}
[[gnu::noinline]] void boundaries_and_alias() {
    auto f=std::make_unique<Fixtures>();dirty(f->source);dirty(f->out);compare_copy(*f);
    f->oracle=f->out;
    check(copy_plan_into(f->out.plan,f->out.plan)&&same(f->out.plan,f->oracle.plan),"maximum Plan self alias preserves active payload");
    check(copy_schedule_into(f->out.schedule,f->out.schedule)&&same(f->out.schedule,f->oracle.schedule),
        "maximum Schedule self alias preserves active payload");
    constexpr std::array<int,4> bad{-1,7,std::numeric_limits<int>::min(),std::numeric_limits<int>::max()};
    for(const int count:bad) {
        dirty(f->out);f->source.plan.crew_count=count;
        check(!copy_plan_into(f->out.plan,f->source.plan)&&same(f->out.plan,f->fresh.plan),
            "invalid crew bound resets destination Plan headers");
        check(f->source.plan.crew_count==count,"invalid nonalias source is unchanged");
    }
    for(const auto member:route_members)for(const int count:std::array<int,4>{-1,129,std::numeric_limits<int>::min(),std::numeric_limits<int>::max()}) {
        dirty(f->source);dirty(f->out);(f->source.plan.*member).count=count;
        check(!copy_plan_into(f->out.plan,f->source.plan)&&same(f->out.plan,f->fresh.plan),
            "each invalid route bound resets all Plan headers before copying");
    }
    for(const int count:bad) {
        dirty(f->source);dirty(f->out);f->source.schedule.actor_count=count;
        check(!copy_schedule_into(f->out.schedule,f->source.schedule)&&same(f->out.schedule,f->fresh.schedule),
            "invalid actor bound resets destination Schedule headers");
    }
    for(const int count:std::array<int,4>{-1,13,std::numeric_limits<int>::min(),std::numeric_limits<int>::max()}) {
        dirty(f->source);dirty(f->out);f->source.schedule.production_count=count;
        check(!copy_schedule_into(f->out.schedule,f->source.schedule)&&same(f->out.schedule,f->fresh.schedule),
            "invalid production bound resets destination Schedule headers");
    }
    for(int actor=0;actor<p6::max_actors;++actor)for(const int count:std::array<int,2>{-1,8}) {
        dirty(f->source);dirty(f->out);f->source.schedule.actors[actor].event_count=count;
        check(!copy_schedule_into(f->out.schedule,f->source.schedule)&&same(f->out.schedule,f->fresh.schedule),
            "every active invalid event count resets Schedule headers");
    }
    for(int production=0;production<p6::max_productions;++production)for(const int count:std::array<int,2>{-1,7}) {
        dirty(f->source);dirty(f->out);f->source.schedule.productions[production].holder_count=count;
        check(!copy_schedule_into(f->out.schedule,f->source.schedule)&&same(f->out.schedule,f->fresh.schedule),
            "every active invalid holder count resets Schedule headers");
    }
    dirty(f->out);f->out.plan.crew_count=7;
    check(!copy_plan_into(f->out.plan,f->out.plan)&&same(f->out.plan,f->fresh.plan),"invalid Plan self alias safely resets headers");
    dirty(f->out);f->out.schedule.actors[5].event_count=8;
    check(!copy_schedule_into(f->out.schedule,f->out.schedule)&&same(f->out.schedule,f->fresh.schedule),
        "invalid Schedule self alias safely resets headers");
    dirty(f->source);f->source.plan.crew_count=0;
    f->source.schedule.actor_count=0;f->source.schedule.production_count=0;
    f->source.schedule.actors[5].event_count=std::numeric_limits<int>::max();
    f->source.schedule.productions[11].holder_count=std::numeric_limits<int>::max();compare_copy(*f);
    check(!evaluate(f->out.schedule).eligible(),"inactive malformed nested headers are never read or promoted to permission");
}

namespace menu=p8::reversible_workshop::native;
bool same(const p6::Exposure& a,const p6::Exposure& b) {
    return a.health==b.health&&a.max_health==b.max_health&&a.incoming_total==b.incoming_total&&
        a.next_phase_incoming==b.next_phase_incoming&&a.own_timed_shield==b.own_timed_shield&&
        a.assigned_timed_heal==b.assigned_timed_heal&&a.observations_complete==b.observations_complete;
}
bool same(const p6::CycleCertificate& a,const p6::CycleCertificate& b) {
    return a.clock==b.clock&&a.actor==b.actor&&a.group==b.group&&same(a.clear,b.clear)&&
        same(a.hold,b.hold)&&same(a.bank,b.bank)&&same(a.recovery,b.recovery)&&
        a.route_known==b.route_known&&a.bank_executable==b.bank_executable&&
        a.renewal_reachable==b.renewal_reachable&&a.solo==b.solo;
}
bool same(const p6::forecast::PhaseTerm& a,const p6::forecast::PhaseTerm& b) {
    return a.kind==b.kind&&a.actor==b.actor&&a.group==b.group&&a.waypoint==b.waypoint&&
        a.event_id==b.event_id&&a.start==b.start&&a.finish==b.finish&&a.incoming==b.incoming&&
        a.shield_absorbed==b.shield_absorbed&&a.cumulative_incoming==b.cumulative_incoming&&
        a.health_after==b.health_after&&a.next_phase_incoming==b.next_phase_incoming&&a.source==b.source;
}
bool same_menu_plan(const Plan& a,const Plan& b) {
    // Workshop configuration identity compares complete arrays, including tails.
    return same(a,b)&&a.crew==b.crew&&a.pads==b.pads;
}
bool same_menu_baseline(const Solo& a,const Solo& b) {
    const auto& x=a.model;const auto& y=b.model;
    if(!same_menu_plan(a.plan,b.plan)||!same(x.schedule,y.schedule)||!same(x.certificate,y.certificate)||
        x.status!=y.status||x.issue!=y.issue||x.abort!=y.abort||x.phase_count!=y.phase_count||
        x.phase_count<0||x.phase_count>int(x.phases.size())||x.first.stage!=y.first.stage||
        x.first.actor!=y.first.actor||x.first.group!=y.first.group||x.first.waypoint!=y.first.waypoint||
        x.first.event_id!=y.first.event_id||x.recovery_health_unmodeled!=y.recovery_health_unmodeled||
        x.purchases_deferred!=y.purchases_deferred||a.site!=b.site||a.bank!=b.bank||
        a.ambient_incoming!=b.ambient_incoming||a.feasible!=b.feasible)return false;
    for(int i=0;i<x.phase_count;++i)if(!same(x.phases[i],y.phases[i]))return false;
    return true;
}
bool same_menu_offer(const common::Offer& a,const common::Offer& b) {
    if(!same_headers(a,b)||!same_menu_plan(a.plan,b.plan)||a.safety_count<0||a.safety_count>p6::max_actors)return false;
    for(int i=0;i<a.safety_count;++i)if(!same(a.safety[i],b.safety[i]))return false;
    return true;
}
// Local copy of the old append operation; no native/SDK headers enter this test.
bool old_append(p6::Schedule& to,const p6::Schedule& from,int delay,int prefix) {
    if(from.actor_count!=1||to.actor_count==p6::max_actors||
        to.production_count+from.production_count>p6::max_productions)return false;
    auto actor=from.actors[0];const int offset=to.production_count;
    if(!actor.occupied_turns.known())return false;
    actor.occupied_turns.value+=delay;
    for(int e=0;e<actor.event_count;++e) {
        actor.events[e].tick+=delay;actor.events[e].event_id+=prefix;
        if(actor.events[e].production_index>=0)actor.events[e].production_index+=offset;
    }
    for(int p=0;p<from.production_count;++p) {
        auto production=from.productions[p];production.tick+=delay;production.event_id+=prefix;
        to.productions[to.production_count++]=production;
    }
    to.actors[to.actor_count++]=actor;return true;
}
bool old_assemble(common::Offer& offer,const Solo& own,std::span<const Solo> baselines,const p6::Frame& frame) {
    offer={};offer.plan=own.plan;offer.schedule.target_id=own.plan.group;
    int prefix=0;bool self=false;
    for(const auto& baseline:baselines) {
        if(!baseline.feasible||offer.safety_count==p6::max_actors)return false;
        auto current=baseline;auto& actor=current.model.schedule.actors[0];
        if(actor.actor_id!=frame.clock.self) {
            actor.initial_bag.source=p6::Source::visible_subject;actor.initial_gold={0,p6::Source::conservative};
        }
        current.model.certificate.clock=frame.clock;
        self|=actor.actor_id==frame.clock.self;
        if(!old_append(offer.schedule,current.model.schedule,0,prefix))return false;
        offer.safety[offer.safety_count++]=current.model.certificate;prefix+=64;
    }
    return self;
}
void menu_solo(Solo& out,int actor) {
    out={};auto seed=std::make_unique<common::Offer>();solo(*seed);
    out.plan=seed->plan;out.plan.group=actor+7;out.plan.collector=actor;
    for(int i=0;i<6;++i){out.plan.crew[i]=actor+i*100;out.plan.pads[i]={actor+i,actor-i};}
    out.model.schedule=seed->schedule;auto& schedule=out.model.schedule;
    schedule.target_id=out.plan.group;auto& owner=schedule.actors[0];owner.actor_id=actor;
    owner.initial_bag={actor+3,p6::Source::self_observed};owner.initial_gold={actor+4,p6::Source::private_history};
    owner.occupied_turns={9,p6::Source::scheduled};owner.event_count=4;
    owner.events[0]={p6::EventKind::production,10,2,0,{},-1,p6::Source::scheduled};
    owner.events[1]={p6::EventKind::production,20,3,1,{},-1,p6::Source::scheduled};
    owner.events[2]={p6::EventKind::tribute,30,7,-1,{},-1,p6::Source::scheduled};
    owner.events[3]={p6::EventKind::renewal,40,9,-1,{},out.plan.group,p6::Source::scheduled};
    schedule.production_count=2;
    for(int i=0;i<2;++i) {
        auto& production=schedule.productions[i];production.event_id=10+i*10;production.tick=2+i;
        production.gems={actor+i,p6::Source::public_map};production.gold={actor+i+1,p6::Source::queried_setting};
        production.holders.fill(actor+500);production.holders[0]=actor;production.holder_count=1;
        production.source=p6::Source::scheduled;
    }
    schedule.immediate_win={actor,30,{true,p6::Source::self_observed}};
    schedule.terminal_tiebreak={{actor,p6::Source::private_history},{actor+1,p6::Source::public_score},true};
    auto& certificate=out.model.certificate;certificate.clock={1,actor,2};
    certificate.actor=actor;certificate.group=out.plan.group;
    certificate.route_known=certificate.bank_executable=certificate.renewal_reachable=certificate.solo=true;
    int phase=0;
    for(auto* exposure:{&certificate.clear,&certificate.hold,&certificate.bank,&certificate.recovery}) {
        exposure->health=1000;exposure->max_health=1200;exposure->incoming_total=10+phase;
        exposure->next_phase_incoming=2+phase;exposure->own_timed_shield=3+phase;
        exposure->assigned_timed_heal=4+phase;exposure->observations_complete=true;++phase;
    }
    out.model.status=p6::Status::feasible;out.model.issue=p6::forecast::Issue::none;
    out.model.abort=p6::Failure::missed_appointment;out.model.phase_count=3;
    for(int i=0;i<int(out.model.phases.size());++i) {
        auto& term=out.model.phases[i];term.kind=p6::forecast::PhaseKind::clear;
        term.actor=actor;term.group=out.plan.group;term.waypoint=actor+i;term.event_id=100+i;
        term.start=i;term.finish=i+1;term.incoming=1+i;term.shield_absorbed=2+i;
        term.cumulative_incoming=3+i;term.health_after=1000-i;term.next_phase_incoming=4+i;
        term.source=p6::Source::conservative;
    }
    out.model.first={p6::forecast::FirstStage::clear,actor,out.plan.group,77,10};
    out.model.recovery_health_unmodeled=true;out.model.purchases_deferred=false;
    out.site=actor+1;out.bank=actor+2;out.ambient_incoming=actor+.25;out.feasible=true;
    for(const auto member:route_members)(out.plan.*member).tiles.back()={actor+700,actor+800};
}
struct MenuFixtures {
    std::array<Solo,7> baselines{},before{};
    Solo own{},own_before{},copied{},copy_oracle{};
    common::Offer out{},oracle{};
    p6::Frame frame{};
};
void prepare_menu(MenuFixtures& f) {
    for(int i=0;i<7;++i)menu_solo(f.baselines[i],101+i);
    f.own=f.baselines[0];f.frame.clock={88,101,7};
}
void compare_menu(MenuFixtures& f,int count,bool expected) {
    dirty(f.out);dirty(f.oracle);f.before=f.baselines;f.own_before=f.own;
    const std::span<const Solo> baselines{f.baselines.data(),std::size_t(count)};
    const bool old=old_assemble(f.oracle,f.own,baselines,f.frame);
    const bool current=menu::assemble_independent_offer(f.out,f.own,baselines,f.frame,old_append);
    check(old==expected&&current==old,"menu success/failure agrees with old whole-copy assembly");
    check(same_menu_offer(f.out,f.oracle),"menu complete live fields and full crew/pads match old assembly");
    check(same_menu_baseline(f.own,f.own_before),"menu assembly does not mutate own source");
    for(int i=0;i<7;++i)check(same_menu_baseline(f.baselines[i],f.before[i]),"menu assembly does not mutate baseline sources");
}
[[gnu::noinline]] void menu_baseline_copy() {
    auto f=std::make_unique<MenuFixtures>();prepare_menu(*f);menu_solo(f->copied,201);
    auto& source=f->baselines[0];source.model.phase_count=int(source.model.phases.size());
    f->copy_oracle=source;menu::copy_menu_baseline(f->copied,source);
    check(same_menu_baseline(f->copied,f->copy_oracle),"menu baseline copies every live Solo and forecast field");
    source.model.phase_count=1;source.model.schedule.actors[0].event_count=0;
    source.model.schedule.production_count=0;source.plan.release.count=0;
    source.plan.release.verified=true;source.plan.release.blocker_assumptions=93;
    source.model.schedule.actors[0].initial_bag={88,p6::Source::unknown};
    f->copy_oracle=source;menu::copy_menu_baseline(f->copied,source);
    check(same_menu_baseline(f->copied,f->copy_oracle),"shrinking menu baseline retains exact source headers and unknowns");
    check(f->copied.model.phases.back().actor==101,"inactive menu forecast phases stay initialized behind phase_count");
    menu::copy_menu_baseline(f->copied,f->copied);
    check(same_menu_baseline(f->copied,f->copy_oracle),"menu baseline self alias retains live fields");
    source={};f->copy_oracle=source;menu::copy_menu_baseline(f->copied,source);
    check(same_menu_baseline(f->copied,f->copy_oracle),"empty menu baseline clears live headers and complete crew/pads");
    for(int invalid=0;invalid<4;++invalid) {
        menu_solo(source,101);menu_solo(f->copied,201);
        if(invalid==0)source.plan.crew_count=7;
        if(invalid==1)source.plan.bank.count=max_route_tiles+1;
        if(invalid==2)source.model.schedule.actors[0].event_count=p6::max_actor_events+1;
        if(invalid==3)source.model.phase_count=int(source.model.phases.size())+1;
        menu::copy_menu_baseline(f->copied,source);
        check(f->copied.plan.crew_count==source.plan.crew_count&&f->copied.plan.bank.count==source.plan.bank.count&&
            f->copied.model.schedule.actors[0].event_count==source.model.schedule.actors[0].event_count&&
            f->copied.model.phase_count==source.model.phase_count,"invalid menu bounds retain old assignment headers");
        check(f->copied.plan.bank.tiles.back()==source.plan.bank.tiles.back()&&
            same(f->copied.model.phases.back(),source.model.phases.back()),"invalid menu bounds fall back to old full payload assignment");
        source.plan.crew_count=f->copied.plan.crew_count=1;source.plan.bank.count=f->copied.plan.bank.count=1;
        source.model.schedule.actors[0].event_count=f->copied.model.schedule.actors[0].event_count=4;
        source.model.phase_count=f->copied.model.phase_count=3;
        check(same_menu_baseline(f->copied,source),"invalid menu fallback preserves all other live fields");
    }
}
[[gnu::noinline]] void menu_assembly_oracle() {
    auto f=std::make_unique<MenuFixtures>();
    for(int self=0;self<3;++self) {
        prepare_menu(*f);std::swap(f->baselines[0],f->baselines[self]);compare_menu(*f,3,true);
        for(int i=0;i<3;++i) {
            const auto& actor=f->out.schedule.actors[i];const bool own=actor.actor_id==f->frame.clock.self;
            check(actor.actor_id==f->baselines[i].model.schedule.actors[0].actor_id&&
                actor.events[0].event_id==10+i*64&&actor.events[1].production_index==i*2+1&&
                actor.events[2].production_index==-1,"menu preserves peer order, event prefixes and production offsets");
            check(actor.initial_bag.source==(own?p6::Source::self_observed:p6::Source::visible_subject)&&
                actor.initial_gold.value==(own?105:0)&&
                actor.initial_gold.source==(own?p6::Source::private_history:p6::Source::conservative),
                "menu normalization retains own facts and replaces only peer purse provenance");
            check(f->out.safety[i].clock==f->frame.clock,"menu certificates use exact current observer clock");
        }
    }
    prepare_menu(*f);compare_menu(*f,6,true);compare_menu(*f,1,true);compare_menu(*f,0,false);
    prepare_menu(*f);compare_menu(*f,7,false);
    check(f->out.schedule.actor_count==6&&f->out.safety_count==6,"seventh baseline rejection retains the first six appended actors");
    prepare_menu(*f);f->frame.clock.self=999;compare_menu(*f,3,false);
    check(f->out.schedule.actor_count==3,"missing self rejects after retaining all independently appended peers");
    prepare_menu(*f);f->baselines[1].feasible=false;compare_menu(*f,3,false);
    check(f->out.schedule.actor_count==1&&f->out.safety_count==1,"infeasible baseline leaves the original partial offer");
    for(int count:{0,2}) {
        prepare_menu(*f);f->baselines[1].model.schedule.actor_count=count;compare_menu(*f,3,false);
    }
    prepare_menu(*f);f->baselines[1].model.schedule.actors[0].occupied_turns.source=p6::Source::unknown;
    compare_menu(*f,3,false);
    prepare_menu(*f);f->baselines[2].model.schedule.production_count=p6::max_productions;compare_menu(*f,3,false);
    prepare_menu(*f);f->baselines[1].model.schedule.actors[0].initial_bag.source=p6::Source::unknown;
    f->baselines[1].model.schedule.actors[0].initial_gold.source=p6::Source::unknown;compare_menu(*f,3,true);
}
} // namespace
int main() {
    reset_and_reuse();preserved_unknowns_and_validation();boundaries_and_alias();
    menu_baseline_copy();menu_assembly_oracle();
    std::printf("plan8-offer-storage: %d checks PASS\n",checks);
}
