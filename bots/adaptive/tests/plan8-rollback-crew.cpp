#include "../rollback-crew/src/policy.h"
#include "../rollback-crew/src/draft.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>
using namespace p8;
using namespace p8::rollback_crew;
namespace {
int checks=0;
void check(bool ok,const char* text) { ++checks; if (!ok) { std::fprintf(stderr,"FAIL: %s\n",text); std::exit(1); } }
template<class T> p6::Fact<T> fact(T v) { return {v,p6::Source::synthetic_fixture}; }
Route path(std::initializer_list<p6::Point> points) { Route r; r.verified=true; r.count=int(points.size()); int i=0; for (auto p:points) r.tiles[i++]=p; return r; }
Plan incumbent_plan(int owner,bool generalist=false) {
    const int y=owner==1?0:4;
    Plan p; p.organization=generalist?Organization::G:Organization::U; p.collector=owner; p.crew[0]=owner; p.crew_count=1;
    p.group=generalist?12:owner==1?7:8; p.altar=generalist?6:owner==1?4:5;
    p.ingress=path({{0,y},{1,y},{2,y}});
    p.capture=generalist?path({{2,0},{3,1}}):path({{2,y},{3,y}});
    p.bank=generalist?path({{3,1},{4,1}}):path({{3,y},{4,y}});
    p.renewal=generalist?path({{4,1},{3,1},{2,1},{1,1},{0,0}}):path({{4,y},{4,y+1},{3,y+1},{2,y+1},{1,y+1},{0,y}});
    p.forecast_rounds=18; p.capture_window=5; p.remaining_rounds=100; p.gem_bearing=p.complete_verified=p.rally_legal=true; return p;
}
Plan replacement_plan(int owner) {
    Plan p; p.organization=Organization::J; p.group=9; p.collector=3; p.altar=7; p.crew={1,2,3,-1,-1,-1}; p.crew_count=3;
    p.pads[0]={2,2}; p.pads[1]={3,3}; p.pads[2]={4,4};
    p.ingress=owner==1?path({{0,0},{0,1},{1,2},{2,2}}):path({{0,4},{1,3},{2,3},{3,3}});
    p.capture=owner==1?path({{2,2},{3,2}}):path({{3,3},{4,2}});
    p.bank=owner==1?path({{3,2},{3,1},{4,1},{5,1}}):path({{4,2},{5,2},{5,1}});
    p.renewal=owner==1?path({{5,1},{4,1},{3,1},{2,1},{1,0},{0,0}}):path({{5,1},{4,1},{3,1},{2,1},{1,2},{0,3},{0,4}});
    p.forecast_rounds=20; p.capture_window=5; p.remaining_rounds=100; p.gem_bearing=p.complete_verified=p.rally_legal=true; return p;
}
void event(p6::ActorSchedule& a,p6::EventKind kind,int id,int tick,int pi=-1) {
    p6::Event e; e.kind=kind; e.event_id=id; e.tick=tick; e.production_index=pi; e.gold_cost=fact<p6::Gems>(0);
    e.waypoint_id=kind==p6::EventKind::renewal?90+a.actor_id:-1; e.source=p6::Source::synthetic_fixture; a.events[a.event_count++]=e;
}
void income(p6::Schedule& s,int actor,int group,int amount,int payout,int bank) {
    const int pi=s.production_count++; auto& p=s.productions[pi]; p.event_id=1000+group; p.tick=payout;
    p.gems=fact<p6::Gems>(amount); p.gold=fact<p6::Gems>(0); p.source=p6::Source::synthetic_fixture; p.holder_count=1; p.holders[0]=actor;
    event(s.actors[actor-1],p6::EventKind::production,p.event_id,payout,pi); event(s.actors[actor-1],p6::EventKind::tribute,2000+group,bank);
}
// Declared synthetic continuation: independent named sources7/8 each12 and
// later distinct13/14 each12; proposer source11 yields10. Replacement source9
// has3original20HP monsters and18gems: actual holders receive6 each. A nearby
// secondary10 has120HP and30gems shared10 each. Charged bank queues put all
// three later replacement banks before39; old secondary banks are40/42 and
// source11 bank45. These are conditional future route/timing assumptions, not
// measured native throughput, and never supply a runtime completion receipt.
p6::Schedule schedule(const p6::Frame& f,Organization organization,int target) {
    p6::Schedule s; s.actor_count=3; s.target_id=target;
    for (int i=0;i<3;++i) {
        auto& a=s.actors[i]; a.actor_id=i+1; const bool own=i+1==f.clock.self; const auto* seen=f.visible(i+1);
        a.initial_bag={seen?seen->bag:0,own?p6::Source::self_observed:p6::Source::visible_subject};
        a.initial_gold={own?f.own.gold:0,own?p6::Source::self_observed:p6::Source::conservative};
    }
    if (organization==Organization::J) {
        for (int cycle=0;cycle<2;++cycle) {
            const int pi=s.production_count++; auto& p=s.productions[pi]; p.event_id=1009+cycle; p.tick=cycle?31:11;
            p.gems=fact<p6::Gems>(cycle?30:18); p.gold=fact<p6::Gems>(0); p.source=p6::Source::synthetic_fixture;
            p.holder_count=3; p.holders={1,2,3,-1,-1,-1};
            for (int i=0;i<3;++i) { event(s.actors[i],p6::EventKind::production,p.event_id,p.tick,pi);
                event(s.actors[i],p6::EventKind::tribute,2009+cycle,cycle?35+i:15+2*i); }
        }
        for (int i=0;i<3;++i) s.actors[i].occupied_turns=fact<p6::Gems>(44);
    } else {
        income(s,1,target==12?12:7,target==12?8:12,12,15); income(s,1,13,12,37,40);
        income(s,2,8,12,12,15); income(s,2,14,12,39,42); income(s,3,11,10,42,45);
        for (int i=0;i<3;++i) s.actors[i].occupied_turns=fact<p6::Gems>(i==2?48:46);
    }
    for (int i=0;i<3;++i) event(s.actors[i],p6::EventKind::renewal,3000+i,int(s.actors[i].occupied_turns.value));
    return s;
}
p6::CycleCertificate cert(const p6::Frame& f,int actor,int group) {
    p6::CycleCertificate c; c.clock=f.clock; c.actor=actor; c.group=group; const auto* a=f.visible(actor);
    if (a) c.clear=c.hold=c.bank=c.recovery={a->health,a->max_health,10,0,0,0,true};
    c.route_known=c.bank_executable=c.renewal_reachable=c.solo=true; return c;
}
struct Fixture {
    Inputs in{};
    int owner;
    EvidenceLedger ledger;
    Crew crew{};
    ProbeBudget probes{};
    LocalFrame local{};
    Work work{};
    CycleTrace trace{};
    ContextDeclaration component{},joint{};
    Plan selected{};
    Arm arm=Arm::active;
    bool recording=false;
    bool damage_trial=false;
    int started=-1;
    explicit Fixture(int actor=1):owner(actor),ledger(actor) {
        auto& f=in.frame; f.clock={0,owner,0}; f.own.gold=60;
        for (int id=1;id<=3;++id) {
            p6::Actor a; a.id=id; a.pick_ordinal=id-1; a.team=p6::Team::ally;
            a.type=id==1?p6::Champion::marcella:id==2?p6::Champion::varg:p6::Champion::tavi;
            a.health=a.max_health=100; a.strength=20; a.alive=a.can_move=a.can_attack=true;
            a.position=id==1?p6::Point{2,2}:id==2?p6::Point{3,3}:p6::Point{4,4};
            if (id==owner) f.own.actor=a; else f.actors[f.actor_count++]={a,f.clock,id,p6::Knowledge::exact};
        }
        f.own.actor.position=owner==1?p6::Point{0,0}:p6::Point{0,4}; f.own.at_spawn=true;
        p6::Group g; g.id=owner==1?7:8; g.lifecycle=p6::Lifecycle::living;
        g.original_spawn_visible=g.all_spawns_visible=g.uncontested_now=true; f.group={g,f.clock,g.id,p6::Knowledge::exact};
        in.settings={2000,1000,5}; in.settings_identity={17,p6::Source::queried_setting};
        in.draft.champions={p6::Champion::marcella,p6::Champion::varg,p6::Champion::tavi,p6::Champion::other,p6::Champion::other};
        in.draft.actors={1,2,3,-1,-1}; in.draft.picks=in.picks=3; in.target_group=9; in.independent=0; in.offer_count=3;
        in.own_state={{100,100,20,10,1,0,99},f.clock,owner,p6::Knowledge::exact}; in.threat_regime={0,f.clock,owner,p6::Knowledge::exact};
        in.offers[0].plan=incumbent_plan(owner); in.offers[1].plan=replacement_plan(owner); in.offers[2].plan=incumbent_plan(owner,true);
        // This focused fallback source belongs to actor1's district; actor2
        // retains an ordinary independent menu without that unsupported G.
        if (owner==2) in.offer_count=2;
        component.key.settings_id=17; component.key.actor_id=owner; component.key.champion=int(f.own.actor.type); component.key.role=0;
        component.key.source_group=owner==1?7:8; component.key.route_template=owner-1; component.key.endpoint=owner==1?4:5;
        component.key.threat_regime=0; component.key.participant_count=1; component.key.participants[0]=owner;
        component.key.route_hash=plan_route_hash(in.offers[0].plan); component.key.build={99,100,20,10,1};
        component.envelope={{75,100},{100,100},{20,20},{10,10},{1,1},{0,100},0};
        for (int h=0;h<3;++h) component.hypotheses[h].offset={2*h,2*h+2};
        joint=component; joint.kind=EpisodeKind::full_organization; joint.require_supported_work=true;
        joint.envelope.health.lower=50; // Planned J damage; incumbent entry stays75-100%.
        joint.key.role=3; joint.key.source_group=9; joint.key.route_template=2; joint.key.endpoint=7;
        joint.key.participants={1,2,3,-1,-1,-1}; joint.key.participant_count=3; joint.key.route_hash=plan_route_hash(in.offers[1].plan);
        in.component_slot=ledger.predeclare(component); in.component_entry=in.offers[0].entry=component.key; in.offers[0].context_slot=in.component_slot;
        in.supported_slot=ledger.predeclare(joint); in.supported_entry=in.offers[1].entry=joint.key; in.offers[1].context_slot=in.supported_slot;
        tick(0);
    }
    p6::Actor& actor(int id) { if (id==owner) return in.frame.own.actor; for (int i=0;i<in.frame.actor_count;++i) if (in.frame.actors[i].subject==id) return in.frame.actors[i].value; std::abort(); }
    void tick(int round) {
        auto& f=in.frame; f.clock={round,owner,0}; f.group.clock=f.clock;
        for (int i=0;i<f.actor_count;++i) f.actors[i].clock=f.clock;
        in.own_state.clock=in.threat_regime.clock=f.clock; in.own_state.value.bag=f.own.actor.bag;
        in.own_state.value.health=f.own.actor.health; in.own_state.value.max_health=f.own.actor.max_health; in.own_state.value.strength=f.own.actor.strength;
        in.windows.valid=true; in.windows.source=p6::Source::synthetic_fixture; in.windows.deadlines={2000-round,78,39}; in.windows.windows={120,78,39};
        in.windows.sampled_round=round; in.windows.sampled_actor_id=owner; in.visible_proposer={9,f.clock,3,p6::Knowledge::exact};
        for (int k=0;k<in.offer_count;++k) {
            auto& o=in.offers[k]; o.schedule=schedule(f,o.plan.organization,o.plan.group); o.safety_count=3;
            for (int id=1;id<=3;++id) o.safety[id-1]=cert(f,id,common::participant(o.plan,id)?o.plan.group:11);
        }
        local.frame=f; local.current_round=round; local.safety_count=3; local.milestones={};
        const auto& p=work.active?work.plan:in.offers[0].plan;
        for (int id=1;id<=3;++id) local.safety[id-1]=cert(f,id,p.group);
        local.legal={{},f.clock,owner,p6::Knowledge::exact}; auto& legal=local.legal.value;
        legal.safe_now=legal.attack_legal=legal.altar_legal=legal.renewal_productive=legal.recall_safe=true;
        for (Leg leg:{Leg::ingress,Leg::capture,Leg::bank,Leg::renewal,Leg::release}) {
            auto& a=legal.routes[int(leg)]; const auto& r=route_for(p,leg); a.assumptions_hold=route_valid(r); a.next_move_legal=true;
            for (int i=0;i<r.count;++i) if (r.tiles[i]==f.own.actor.position) { a.next_tile=r.tiles[std::min(i+1,r.count-1)]; break; }
        }
    }
    void source(int group,p6::Lifecycle lifecycle=p6::Lifecycle::living) { in.frame.group.value.id=group; in.frame.group.subject=group; in.frame.group.value.lifecycle=lifecycle; }
    void begin(int round,int expected,Arm chosen=Arm::active) {
        work={}; arm=chosen; started=round; selected=in.offers[expected].plan;
        auto& f=in.frame; f.own.actor.position=selected.ingress.tiles[0]; f.own.actor.bag=0; f.own.actor.channel=p6::Channel::none;
        f.own.actor.spawn_group=-1; f.own.at_spawn=true; source(selected.group);
        for (int id=1;id<=3;++id) if (id!=owner) { auto& a=actor(id); a.action=p6::WitnessAction::none; a.spawn_group=-1; a.bag=0; a.position=in.offers[1].plan.pads[crew_index(in.offers[1].plan,id)]; }
        tick(round); for (int id=1;id<=3;++id) local.safety[id-1]=cert(f,id,selected.group);
        const auto d=crew.decide(in,ledger,probes,arm);
        if (d.option!=expected) std::fprintf(stderr,"choice actor=%d round=%d want=%d got=%d phase=%d attempts=%u rejected=%d/%d/%d\n",owner,round,expected,d.option,int(crew.phase()),crew.attempts(),int(d.rejected[0]),int(d.rejected[1]),int(d.rejected[2]));
        check(d.option==expected,"actual private gates choose demonstrated U replacement J or fresh G");
        const auto admitted=crew.commit(in,local,d,ledger,probes,arm,work);
        check(admitted.started&&bool(admitted.episode)==d.record_episode,"episode and lifetime attempt are committed before first actual action");
        recording=d.record_episode; if (recording) check(trace.begin(ledger,work),"member-local U/J trace binds actual own Work generation");
    }
    Command step(int offset,p6::Point position,bool payout=false,bool bank=false,bool disappear=false) {
        auto& f=in.frame; f.own.actor.position=position; f.own.at_spawn=position==selected.ingress.tiles[0];
        f.own.actor.spawn_group=position==selected.capture.tiles[1]?selected.group:-1;
        if (selected.organization==Organization::J) {
            for (int id=1;id<=3;++id) if (id!=owner) {
                auto& peer=actor(id); peer.action=offset==3?p6::WitnessAction::useful_attack:p6::WitnessAction::none; peer.action_group=9;
                if (offset>=5&&offset<=11) { peer.position=id==1?p6::Point{3,2}:id==2?p6::Point{4,2}:p6::Point{4,3}; peer.spawn_group=9; }
                if (offset>=12) { peer.position=id==2?p6::Point{5,2}:p6::Point{5,3}; peer.spawn_group=-1; }
                if (offset>=16) peer.position=id==2?p6::Point{position.x,position.y+1}:p6::Point{position.x+1,position.y+1};
                if (payout) peer.bag=6;
            }
        }
        const int amount=selected.organization==Organization::J?6:selected.organization==Organization::G?8:12;
        if (selected.organization==Organization::J&&damage_trial&&offset==12) f.own.actor.health=70;
        if (payout) { f.own.actor.bag=amount; source(selected.group,p6::Lifecycle::respawning); }
        if (bank) { f.own.actor.bag=0; f.own.actor.channel=p6::Channel::none; }
        tick(started+offset);
        if (disappear) for (int i=0;i<f.actor_count;++i) if (f.actors[i].subject==3) { f.actors[i].knowledge=p6::Knowledge::unknown; local.frame=f; }
        if (payout||bank) local.milestones={{selected.group,payout,bank,payout?amount:0,bank?amount:0},f.clock,owner,p6::Knowledge::exact};
        const auto c=crew.advance(in,local,ledger,arm,work);
        check(work.confirm(c,{c.clock,c.clock,c.token,c.action,true,true}),"retained physical route command consumes actual own action receipt");
        if (recording) trace.observe(local,work,{f.own.at_spawn,f.clock,owner,p6::Knowledge::exact});
        if (selected.organization==Organization::J&&damage_trial&&offset==12&&ledger.active()) {
            Observation unavailable; unavailable.round=f.clock.round; unavailable.kind=ObservationKind::unavailable;
            unavailable.premises={joint.key,in.own_state.value,all_premises&~membership_known};
            const auto closed=ledger.resolve(ledger.active()->serial,unavailable);
            check(closed.accepted&&!closed.full_positive,"actual lost member closes one unknown trial while safe own bag remains");
            crew.observe_result(closed,work);
        }
        if (disappear) for (int i=0;i<f.actor_count;++i) f.actors[i].knowledge=p6::Knowledge::exact;
        return c;
    }
    void independent(int round,int option=0) {
        begin(round,option); const auto p=selected; const int amount=p.organization==Organization::G?8:12;
        step(0,p.ingress.tiles[0]); step(1,p.ingress.tiles[1]);
        for (int a=0;a<3;++a) check(step(2+a,p.ingress.tiles[2]).action==PhysicalAction::attack,"incumbent has actual three-hit first income");
        source(p.group,p6::Lifecycle::reward_available); step(5,p.capture.tiles[0]);
        for (int r=6;r<=11;++r) step(r,p.capture.tiles[1]);
        step(12,p.capture.tiles[1],true); check(step(13,p.bank.tiles[1]).action==PhysicalAction::tribute,"incumbent actual own bag reaches its own altar duty");
        const int channel=(amount+9)/10;
        for (int r=14;r<13+channel;++r) { in.frame.own.actor.channel=p6::Channel::tribute; in.frame.own.actor.channel_valid=true; step(r,p.bank.tiles[1]); }
        const int bank=13+channel; step(bank,p.bank.tiles[1],false,true);
        for (int i=1;i<p.renewal.count;++i) step(bank+i,p.renewal.tiles[i]);
        check(work.cycle_completed&&work.bank_seen&&work.renewal_seen,"restored or incumbent route actually captures banks and renews productive work");
        if (recording) { const auto result=trace.finish(ledger,in.frame.clock.round,{component.key,in.own_state.value,all_premises});
            check(result.accepted&&result.full_positive,"only complete own unchanged incumbent episodes support saved fallback"); crew.observe_result(result,work); }
    }
    void qualify() {
        check(!crew.remember(in,ledger),"unproved U cannot be saved as demonstrated incumbent");
        independent(0); independent(64);
        check(ledger.component_qualified(in.component_slot,component.key,84),"actual two nonoverlapping U jobs demonstrate own exact incumbent");
        check(crew.remember(in,ledger),"save one exact own source route role and proof after actual qualification");
        check(!crew.remember(in,ledger),"saved incumbent cannot become a mutable policy library");
    }
    void replacement(int round,bool fail,bool expected_damage=false) {
        damage_trial=expected_damage;
        begin(round,1); const auto p=selected;
        check(crew.attempts()==1&&crew.phase()==Phase::trial,"literal one lifetime replacement episode is spent before movement");
        for (int i=0;i<p.ingress.count-1;++i) step(i,p.ingress.tiles[i]);
        check(step(3,p.ingress.tiles[3]).action==PhysicalAction::attack,"each member personally performs real replacement clear work");
        source(9,p6::Lifecycle::reward_available); step(4,p.capture.tiles[0]);
        for (int r=5;r<=10;++r) step(r,p.capture.tiles[1]);
        step(11,p.capture.tiles[1],true);
        for (int i=1;i<p.bank.count;++i) {
            const auto c=step(11+i,p.bank.tiles[i],false,false,fail&&i==1);
            if (fail&&i==1) check(crew.attempts()==1&&in.frame.own.actor.bag==6&&c.action==PhysicalAction::move,
                "lost cooperation retains actual personal payout and follows safe bank route");
        }
        const int tribute=11+p.bank.count-1; // Last bank arrival starts tribute.
        check(work.tribute_started,"actual replacement own tribute starts despite a retired assembly");
        const int bank=tribute+1; step(bank,p.bank.tiles[p.bank.count-1],false,true);
        for (int i=1;i<p.renewal.count;++i) step(bank+i,p.renewal.tiles[i]);
        check(work.bank_seen&&work.renewal_seen&&in.frame.own.actor.bag==0,"replacement exit actually converts retained own share before restoration");
        if (recording) { const auto result=trace.finish(ledger,in.frame.clock.round,{joint.key,in.own_state.value,all_premises});
            if (!fail) check(result.accepted&&result.full_positive,"one successful member trace records own share and witnessed J roles");
            else check(!result.full_positive,"missing personally visible cooperation cannot become full replacement proof");
            crew.observe_result(result,work); }
        check(crew.retired()&&crew.phase()==Phase::restore&&crew.attempts()==1,"failed or capped successful episode retires replacement without replenishment");
        check(!ledger.organization_qualified(in.supported_slot,joint.key,in.frame.clock.round),"one literal replacement trial never claims full two-cycle J qualification");
    }
};
__attribute__((noinline)) void failed_replacement_restores_actual_incumbents() {
    auto first=std::make_unique<Fixture>(1); auto second=std::make_unique<Fixture>(2);
    first->qualify(); second->qualify();
    const auto old1=plan_route_hash(first->crew.saved().plan),old2=plan_route_hash(second->crew.saved().plan);
    check(old1!=old2&&first->crew.saved().plan.group!=second->crew.saved().plan.group,"two real incumbents have distinct geography and ownership");
    check(common::evaluate(first->in,first->in.offers[0].schedule).min_margin==24&&common::evaluate(first->in,first->in.offers[1].schedule).min_margin==48,
        "replacement opportunity is ranked by charged fixed-payload future schedules rather than a rollback bonus");
    first->replacement(100,true); second->replacement(100,true);
    first->independent(124); second->independent(124);
    check(plan_route_hash(first->work.plan)==old1&&plan_route_hash(second->work.plan)==old2&&first->work.plan.group==7&&second->work.plan.group==8,
        "actual failed replacement restores each stored source route and independent collector duty");
    check(first->work.bank_seen&&second->work.bank_seen&&first->work.renewal_seen&&second->work.renewal_seen,"both restored incumbents produce convert and return with their own bags");
    // The just-completed source is respawning, so it is not immediately a new
    // legal old-source job. Reobserve it living after its actual respawn gap.
    first->source(7); first->tick(164);
    const auto active=first->crew.decide(first->in,first->ledger,first->probes,Arm::active);
    const auto control=first->crew.choose(first->in,first->ledger,first->probes,Arm::control);
    if (active.option!=0||control.option!=1) std::fprintf(stderr,"repair comparison active=%d control=%d Jreject=%d minU=%lld minJ=%lld\n",
        active.option,control.option,int(control.rejected[1]),static_cast<long long>(common::evaluate(first->in,first->in.offers[0].schedule).min_margin),
        static_cast<long long>(common::evaluate(first->in,first->in.offers[1].schedule).min_margin));
    check(active.option==0&&control.option==1&&!control.record_episode,"control may repair currently feasible superior J while active retains actual saved organization");
    check(first->crew.attempts()==1&&second->crew.attempts()==1,"actual epochs and safe restoration do not reset private trial lifetime caps");
}
__attribute__((noinline)) void missing_source_uses_fresh_generalist() {
    auto f=std::make_unique<Fixture>(); f->qualify(); f->replacement(100,true);
    f->source(9,p6::Lifecycle::respawning); f->tick(123);
    check(f->crew.decide(f->in,f->ledger,f->probes,Arm::active).option<0,"unseen old source cannot be restored from saved geometry alone");
    f->source(12); f->tick(124);
    const auto fresh=f->crew.decide(f->in,f->ledger,f->probes,Arm::active);
    check(fresh.option==2&&fresh.organization==Organization::G&&!fresh.record_episode,"currently verified alternative permits fresh G without inventing incumbent proof");
    f->independent(124,2);
    check(f->work.plan.group==12&&f->work.bank_lower==8&&f->work.cycle_completed,"fresh G actually produces and banks its own distinct fixed reward");
}
__attribute__((noinline)) void one_positive_and_expiry_do_not_expand_cap() {
    auto success=std::make_unique<Fixture>(); success->qualify(); success->replacement(100,false);
    check(success->ledger.state(success->in.supported_slot)->positive_completions==1,"one actual successful J trial remains consistency evidence only");
    success->source(7); success->tick(124);
    check(success->crew.decide(success->in,success->ledger,success->probes,Arm::active).option==0,"successful literal one-trial limit returns to verified incumbent");
    auto expired=std::make_unique<Fixture>(); expired->qualify(); expired->begin(100,1);
    expired->tick(200); const auto c=expired->crew.advance(expired->in,expired->local,expired->ledger,Arm::active,expired->work);
    check(c.action!=PhysicalAction::move&&expired->crew.retired()&&expired->crew.attempts()==1&&!expired->ledger.active(),"replacement deadline stops unexecuted stale trial and spends its permanent cap");
    expired->source(7); expired->tick(201);
    check(expired->crew.decide(expired->in,expired->ledger,expired->probes,Arm::active).option==0,"expiry restores only currently reverified still-applicable old circuit");
    expired->in.offers[1].plan.renewal.blocker_assumptions=77; expired->tick(202);
    check(expired->crew.decide(expired->in,expired->ledger,expired->probes,Arm::active).option==0,"changed replacement route cannot launder a second lifetime trial");
}
__attribute__((noinline)) void retired_exit_damage_preserves_future_entry_check() {
    auto f=std::make_unique<Fixture>(); f->qualify(); const auto proof=f->ledger.state(f->in.component_slot)->latest_positive;
    f->replacement(100,true,true);
    check(f->in.frame.own.actor.health==70&&f->ledger.state(f->in.component_slot)->latest_positive==proof&&
        !f->ledger.state(f->in.component_slot)->material_premise_changed,
        "expected damaged J exit does not observe a new incumbent entry while safely banking and returning");
    check(f->component.envelope.health.lower==75&&f->crew.saved().entry.hp_band==HealthBand::three_quarters_to_full,
        "saved incumbent envelope and entry band remain exact rather than broadened");
    // Own spawn arrival was observed; this next owner start is the actual
    // recovery observation after its intervening spawn-heal turn end.
    f->in.frame.own.actor.health=100; f->in.frame.own.spawn_recovery_observed=true; f->source(7); f->tick(121);
    check(f->crew.decide(f->in,f->ledger,f->probes,Arm::active).option==0&&
        f->ledger.component_qualified(f->in.component_slot,f->component.key,121),
        "actual restored HP band and fresh source permit exact incumbent entry after completed J exit");
    f->independent(124);
    check(f->work.plan.group==7&&f->work.cycle_completed,"recovered owner actually restores saved U production bank and renewal");
}
} // namespace
int main() { failed_replacement_restores_actual_incumbents(); missing_source_uses_fresh_generalist(); one_positive_and_expiry_do_not_expand_cap(); retired_exit_damage_preserves_future_entry_check(); std::printf("plan8 rollback crew: %d checks passed\n",checks); }
