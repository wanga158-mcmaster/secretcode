#include "../requalification-reserve/src/policy.h"
#include "../requalification-reserve/src/draft.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>
using namespace p8;
using namespace p8::requalification_reserve;
namespace {
int checks=0;
void check(bool ok,const char* message) { ++checks; if (!ok) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); } }
template<class T> p6::Fact<T> fact(T value) { return {value,p6::Source::synthetic_fixture}; }
Route path(std::initializer_list<p6::Point> points) { Route r; r.verified=true; r.count=int(points.size()); int i=0; for (auto p:points) r.tiles[i++]=p; return r; }
Plan route(int owner=1,bool supported=false,bool replacement=false) {
    const int y=owner==1?0:owner;
    Plan p; p.organization=supported?Organization::H:Organization::U; p.group=owner==1?(supported?8:7):10+owner;
    p.collector=owner; p.altar=replacement?5:4; p.crew[0]=owner; p.crew_count=1;
    p.ingress=path({{0,y},{1,y},{2,y}}); p.capture=path({{2,y},{3,y}});
    p.bank=replacement?path({{3,y},{4,y+1}}):path({{3,y},{4,y}});
    p.renewal=replacement?path({{4,y+1},{3,y+2},{2,y+2},{1,y+2},{0,y+1},{0,y}}):path({{4,y},{4,y+1},{3,y+1},{2,y+1},{1,y+1},{0,y}});
    p.pads[0]={2,y}; p.pads[1]={2,y+1};
    if (supported) { p.crew[1]=4; p.crew_count=2; }
    p.forecast_rounds=18; p.remaining_rounds=100; p.capture_window=5;
    p.gem_bearing=p.complete_verified=p.rally_legal=true; return p;
}
void event(p6::ActorSchedule& a,p6::EventKind kind,int id,int tick,int production=-1) {
    p6::Event e; e.kind=kind; e.event_id=id; e.tick=tick; e.production_index=production; e.gold_cost=fact<p6::Gems>(0);
    e.waypoint_id=kind==p6::EventKind::renewal?90+a.actor_id:-1; e.source=p6::Source::synthetic_fixture; a.events[a.event_count++]=e;
}
void income(p6::Schedule& s,int actor,int group,int amount,int payout,int bank) {
    const int pi=s.production_count++; auto& p=s.productions[pi]; p.event_id=1000+group; p.tick=payout;
    p.gems=fact<p6::Gems>(amount); p.gold=fact<p6::Gems>(0); p.source=p6::Source::synthetic_fixture;
    p.holder_count=1; p.holders[0]=actor; event(s.actors[actor-1],p6::EventKind::production,p.event_id,payout,pi);
    event(s.actors[actor-1],p6::EventKind::tribute,2000+group,bank);
}
// Synthetic, predeclared future work assumptions, not native measurements:
// Safe independent source7 and coupled source8 each have96HP and12gems;
// distinct secondaries9/10 each have192HP and12gems. Solo strength44 needs3
// then5 hits; strength48 needs2 then4. Actual primary H uses2 own hits plus
// one visible44-strength helper hit. Conservative approach/queue timing puts
// the secondary H bank at39, and U at41 before or40 after the upgrade. A displaced
// helper's separate10-gem job is retained at45 with its full return at48.
// Other actors retain separate10-gem production, bank and renewal. A queried
// +4 own damage shard crosses the declared two-hit solo fallback boundary;
// incoming40/phase is a conservative feasibility bound, not an observed hit.
p6::Schedule schedule(const p6::Frame& f,const Plan& p) {
    p6::Schedule s; s.actor_count=5; s.target_id=p.group;
    for (int i=0;i<5;++i) {
        auto& a=s.actors[i]; a.actor_id=i+1; const bool own=i+1==f.clock.self; const auto* seen=f.visible(i+1);
        a.initial_bag={seen?seen->bag:0,own?p6::Source::self_observed:p6::Source::visible_subject};
        a.initial_gold={own?f.own.gold:0,own?p6::Source::self_observed:p6::Source::conservative};
    }
    const bool h=p.organization==Organization::H; const int helper=h?p.crew[1]:-1;
    const auto* collector=f.visible(1); const bool upgraded=collector&&collector->strength>=48;
    income(s,1,h?8:7,12,h||upgraded?11:12,h||upgraded?14:15);
    income(s,1,h?10:9,12,h?36:upgraded?37:38,h?39:upgraded?40:41);
    s.actors[0].occupied_turns=fact<p6::Gems>(h?44:upgraded?44:46);
    for (int id=2;id<=5;++id) {
        if (id==helper&&s.actors[id-1].initial_bag.value>0) event(s.actors[id-1],p6::EventKind::tribute,4000+id,10);
        income(s,id,10+id,10,id==helper?42:11,id==helper?45:14);
        s.actors[id-1].occupied_turns=fact<p6::Gems>(id==helper?48:18);
    }
    for (int i=0;i<5;++i) event(s.actors[i],p6::EventKind::renewal,3000+i,int(s.actors[i].occupied_turns.value));
    return s;
}
p6::CycleCertificate certificate(const p6::Frame& f,int actor,int group) {
    p6::CycleCertificate c; c.clock=f.clock; c.actor=actor; c.group=group; const auto* a=f.visible(actor);
    if (a) c.clear=c.hold=c.bank=c.recovery={a->health,a->max_health,10,0,0,0,true};
    c.route_known=c.bank_executable=c.renewal_reachable=c.solo=true; return c;
}
struct Fixture {
    Inputs in{}; int owner=1; EvidenceLedger ledger; Reserve reserve{}; ProbeBudget probes{};
    LocalFrame local{}; Work work{}; CycleTrace trace{}; Package package{}; Plan selected{};
    int started=-1; bool recording=false; Arm arm=Arm::active;
    explicit Fixture(ChangeKind kind=ChangeKind::visible_membership,int actor=1):owner(actor),ledger(actor) {
        auto& f=in.frame; f.clock={0,owner,0}; f.own.gold=60;
        in.draft.champions={p6::Champion::varg,p6::Champion::tavi,p6::Champion::marcella,p6::Champion::willow,p6::Champion::prism};
        in.draft.actors={1,2,3,4,5}; in.draft.picks=in.picks=5;
        for (int id=1;id<=5;++id) {
            p6::Actor a; a.id=id; a.pick_ordinal=id-1; a.team=p6::Team::ally; a.type=in.draft.champions[id-1];
            a.health=a.max_health=100; a.strength=44; a.alive=a.can_move=a.can_attack=true;
            a.position=id==1?p6::Point{0,0}:id==4||id==5?p6::Point{2,1}:p6::Point{0,id}; a.ready_attack_group=8;
            if (id==owner) f.own.actor=a; else f.actors[f.actor_count++]={a,f.clock,id,p6::Knowledge::exact};
        }
        f.own.actor.position={0,owner==1?0:owner}; f.own.at_spawn=true;
        p6::Group g; g.id=8; g.lifecycle=p6::Lifecycle::living; g.original_spawn_visible=g.all_spawns_visible=g.uncontested_now=true;
        f.group={g,f.clock,8,p6::Knowledge::exact}; in.settings={2000,1000,5}; in.settings_identity={17,p6::Source::queried_setting};
        in.market.inventory={{},f.clock,owner,p6::Knowledge::exact};
        in.own_state={{100,100,44,10,4,0,in.market.inventory.value.hash()},f.clock,owner,p6::Knowledge::exact};
        in.threat_regime={0,f.clock,owner,p6::Knowledge::exact}; in.target_group=8; in.independent=0; in.offer_count=3;
        in.offers[0].plan=route(owner); package.change=kind;
        package.systems[0].plan=route(1,true); package.systems[1].plan=route(1,true,kind==ChangeKind::observed_route);
        if (kind==ChangeKind::visible_membership) package.systems[1].plan.crew[1]=5;
        auto& old=package.systems[0].context; old.kind=EpisodeKind::full_organization; old.require_supported_work=true;
        old.key.settings_id=17; old.key.actor_id=1; old.key.champion=int(p6::Champion::varg); old.key.source_group=8;
        old.key.role=1; old.key.endpoint=4; old.key.route_template=0; old.key.threat_regime=0;
        old.key.participant_count=2; old.key.participants={1,4,-1,-1,-1,-1};
        old.key.build={package.old_inventory.hash(),100,44,10,4}; old.key.route_hash=plan_route_hash(package.systems[0].plan);
        old.key.return_route_hash=plan_return_hash(package.systems[0].plan);
        old.envelope={{75,100},{100,100},{44,44},{10,10},{4,4},{0,100},0};
        for (int h=0;h<3;++h) old.hypotheses[h].offset={2*h,2*h+2};
        package.systems[1].context=old; auto& next=package.systems[1].context;
        next.key.route_hash=plan_route_hash(package.systems[1].plan); next.key.return_route_hash=plan_return_hash(package.systems[1].plan);
        if (kind==ChangeKind::visible_membership) next.key.participants[1]=5;
        if (kind==ChangeKind::observed_route) { next.key.endpoint=5; next.key.route_template=1; }
        if (kind==ChangeKind::own_gear) {
            package.purchase_item=reversible_workshop::damage_item; package.new_inventory.count[package.purchase_item]=1;
            package.strength_boundary=48; package.solo_work={96,p6::Source::public_map}; package.incoming_per_phase=fact(40.0);
            package.reserve=fact(10.0); package.maximum_solo_rounds=fact(2);
            next.key.build.equipment_hash=package.new_inventory.hash(); next.key.build.strength=48; next.envelope.strength={48,48};
        }
        if (owner==1) check(reserve.declare(package,in.draft,ledger),"two exact old and replacement H keys freeze before the first action");
        in.offers[1].plan=package.systems[0].plan; in.offers[2].plan=package.systems[1].plan;
        for (int i=0;i<2;++i) { in.offers[i+1].entry=package.systems[i].context.key; in.offers[i+1].context_slot=reserve.slot(i); }
        for (auto& q:in.market.items) { q.price={30,p6::Source::queried_setting}; q.purchasable={true,p6::Source::queried_setting}; }
        tick(0);
    }
    p6::Actor& actor(int id) { if (id==owner) return in.frame.own.actor; for (int i=0;i<in.frame.actor_count;++i) if (in.frame.actors[i].subject==id) return in.frame.actors[i].value; std::abort(); }
    void source(int group,p6::Lifecycle lifecycle=p6::Lifecycle::living) { in.frame.group.subject=group; in.frame.group.value.id=group; in.frame.group.value.lifecycle=lifecycle; }
    void tick(int round,int revision=0) {
        auto& f=in.frame; f.clock={round,owner,revision}; f.group.clock=f.clock;
        for (int i=0;i<f.actor_count;++i) f.actors[i].clock=f.clock;
        auto& n=in.own_state.value; n.health=f.own.actor.health; n.max_health=f.own.actor.max_health; n.strength=f.own.actor.strength;
        n.bag=f.own.actor.bag; n.equipment_hash=in.market.inventory.value.hash();
        in.own_state.clock=in.threat_regime.clock=in.market.inventory.clock=f.clock;
        in.market.spawn_shop_legal={f.own.at_spawn,f.clock,owner,p6::Knowledge::exact};
        for (auto& q:in.market.items) q.buy_legal={f.own.at_spawn&&q.price.value<=f.own.gold,f.clock,owner,p6::Knowledge::exact};
        in.windows.valid=true; in.windows.source=p6::Source::synthetic_fixture; in.windows.deadlines={2000-round,78,39};
        in.windows.windows={120,78,39}; in.windows.sampled_round=round; in.windows.sampled_actor_id=owner;
        in.visible_proposer={8,f.clock,1,p6::Knowledge::exact};
        for (int k=0;k<in.offer_count;++k) {
            auto& o=in.offers[k]; o.schedule=schedule(f,o.plan); o.safety_count=5;
            for (int id=1;id<=5;++id) o.safety[id-1]=certificate(f,id,common::participant(o.plan,id)?o.plan.group:10+id);
        }
        local.frame=f; local.current_round=round; local.milestones={}; local.safety_count=5;
        const auto& p=work.active?work.plan:in.offers[0].plan;
        for (int id=1;id<=5;++id) local.safety[id-1]=certificate(f,id,p.group);
        local.legal={{},f.clock,owner,p6::Knowledge::exact}; auto& l=local.legal.value;
        l.safe_now=l.attack_legal=l.altar_legal=l.recall_safe=l.renewal_productive=true;
        for (Leg leg:{Leg::ingress,Leg::capture,Leg::bank,Leg::renewal,Leg::release}) {
            auto& a=l.routes[int(leg)]; const auto& r=route_for(p,leg); a.assumptions_hold=route_valid(r); a.next_move_legal=true;
            for (int i=0;i<r.count;++i) if (r.tiles[i]==f.own.actor.position) { a.next_tile=r.tiles[std::min(i+1,r.count-1)]; break; }
        }
    }
    void ready(int round,int option) {
        work={}; selected=in.offers[option].plan; started=round;
        auto& f=in.frame; f.own.actor.position=selected.ingress.tiles[0]; f.own.actor.bag=0; f.own.actor.spawn_group=-1;
        f.own.actor.channel=p6::Channel::none; f.own.at_spawn=true; source(selected.group);
        for (int id=1;id<=5;++id) if (id!=owner) { auto& a=actor(id); a.action=p6::WitnessAction::none; a.bag=0; a.spawn_group=-1;
            if (common::participant(selected,id)) a.position=selected.pads[crew_index(selected,id)]; }
        tick(round); for (int id=1;id<=5;++id) local.safety[id-1]=certificate(f,id,selected.group);
    }
    void begin(int round,int option) {
        ready(round,option); const auto d=decide(in,reserve,ledger,probes,arm);
        if (d.option!=option) std::fprintf(stderr,"choice owner=%d t=%d want=%d got=%d retired=%d used=%d selected=%d rejects=%d/%d/%d\n",owner,round,option,d.option,reserve.retired(),reserve.used(),reserve.selected(),int(d.rejected[0]),int(d.rejected[1]),int(d.rejected[2]));
        check(d.option==option,"current evidence selects only the permitted ordinary productive organization");
        const auto admitted=commit(in,local,d,reserve,ledger,probes,arm,work,trace);
        check(admitted.started&&bool(admitted.episode)==d.record_episode,"fresh actual Work starts only after its own episode is issued"); recording=d.record_episode;
    }
    Command step(int offset,p6::Point position,bool payout=false,bool bank=false,bool blocked=false) {
        auto& f=in.frame; f.own.actor.position=position; f.own.at_spawn=position==selected.ingress.tiles[0];
        f.own.actor.spawn_group=position==selected.capture.tiles[selected.capture.count-1]?selected.group:-1;
        if (selected.organization==Organization::H) {
            auto& helper=actor(selected.crew[1]); helper.action=offset==2?p6::WitnessAction::useful_attack:p6::WitnessAction::none;
            helper.action_group=selected.group; helper.spawn_group=-1;
            if (offset>=5) helper.position={4,2};
        }
        const int amount=owner==1?12:10;
        if (payout) { f.own.actor.bag=amount; source(selected.group,p6::Lifecycle::respawning); }
        if (bank) { f.own.actor.bag=0; f.own.actor.channel=p6::Channel::none; }
        tick(started+offset);
        if (payout||bank) local.milestones={{selected.group,payout,bank,payout?amount:0,bank?amount:0},f.clock,owner,p6::Knowledge::exact};
        if (blocked) local.legal.value.routes[int(Leg::renewal)].assumptions_hold=false;
        const auto c=advance(in,local,reserve,ledger,arm,work);
        check(work.confirm(c,{c.clock,c.clock,c.token,c.action,true,true}),"actual action receipt confirms retained command once");
        observe_cycle(in,local,reserve,ledger,work,trace); return c;
    }
    int cycle(int round,int option,bool block=false,bool inspect=false) {
        begin(round,option); const auto p=selected;
        if (inspect) check(reserve.begin_route_observation(work),"route change begins on a newly started real independent Work");
        check(step(0,p.ingress.tiles[0]).action==PhysicalAction::move,"actual changed or incumbent route starts at its first retained tile"); step(1,p.ingress.tiles[1]);
        const int attacks=p.organization==Organization::H||in.frame.own.actor.strength>=48?2:3;
        for (int a=0;a<attacks;++a) check(step(2+a,p.ingress.tiles[2]).action==PhysicalAction::attack,"actual productive attack work clears the same fixed public payload");
        int t=2+attacks; source(p.group,p6::Lifecycle::reward_available); step(t++,p.capture.tiles[0]);
        for (int hold=0;hold<6;++hold) check(step(t++,p.capture.tiles[1]).action==PhysicalAction::hold,"actual own holder waits for personal capture payout");
        step(t++,p.capture.tiles[1],true);
        for (int i=1;i<p.bank.count;++i) {
            const auto c=step(t++,p.bank.tiles[i]);
            if (i==p.bank.count-1) check(c.action==PhysicalAction::tribute,"actual owner begins own retained bank channel");
        }
        if (owner==1) { in.frame.own.actor.channel=p6::Channel::tribute; in.frame.own.actor.channel_valid=true;
            check(step(t++,p.bank.tiles[p.bank.count-1]).action==PhysicalAction::preserve_channel,"safe ongoing own tribute survives policy changes"); }
        step(t++,p.bank.tiles[p.bank.count-1],false,true);
        for (int i=1;i<p.renewal.count;++i) {
            const bool stop=block&&i==p.renewal.count-2; const auto c=step(t++,p.renewal.tiles[i],false,false,stop);
            if (stop) { check(c.action!=PhysicalAction::move&&!work.cycle_completed&&work.failure==PhysicalFailure::blocked_route,
                "personally blocked actual return stops the coupled episode without fabricated renewal");
                check(reserve.retired()&&ledger.lifetime_failure_count()>0,"actual unsuccessful endpoint retires old context and retains failure"); return in.frame.clock.round; }
        }
        check(!work.active&&work.cycle_completed&&work.bank_seen&&work.renewal_seen,"actual own payout bank and productive return complete");
        return in.frame.clock.round;
    }
    int fail_old() { const int end=cycle(0,1,true); check(ledger.state(reserve.slot(0))->campaign_episodes==1&&ledger.state(reserve.slot(0))->positive_completions==0,
        "first feasible H is permitted without Canary U gate and its real failure remains spent"); return end; }
    void at_shop(int round) { work={}; in.frame.own.actor.position={0,0}; in.frame.own.actor.bag=0; in.frame.own.actor.channel=p6::Channel::none; in.frame.own.at_spawn=true; source(8); tick(round); }
};
__attribute__((noinline)) void membership_reopens_only_once() {
    auto f=std::make_unique<Fixture>();
    const auto u=common::evaluate(f->in,f->in.offers[0].schedule); const auto h=common::evaluate(f->in,f->in.offers[1].schedule);
    check(u.min_margin==52&&h.min_margin==54&&p6::compare(h,u)>0,"honest fixed-payload complete schedules favor current H by one-turn second bank after charged helper work");
    check(decide(f->in,f->reserve,f->ledger,f->probes,Arm::active).option==1,"initial feasible H needs no unrelated two-U prerequisite");
    f->fail_old(); const auto failures=f->ledger.lifetime_failure_count(); const auto old=*f->ledger.state(f->reserve.slot(0));
    f->ready(64,2);
    check(decide(f->in,f->reserve,f->ledger,f->probes,Arm::active).option==0,"unwitnessed new member cannot reopen the failed route");
    check(f->reserve.witness_membership(f->in),"different named visible helper currently ready at its own rally pad is an actual local membership change");
    auto& helper=f->actor(5); helper.ready_attack_group=-1; f->tick(65);
    for (int k=1;k<3;++k) f->in.offers[k].safety[4].route_known=false;
    check(!f->reserve.authorize(f->in,f->local,f->work,f->ledger,Arm::active),"a real change cannot override current unsafe new helper work");
    helper.ready_attack_group=8; f->ready(66,2);
    check(!f->reserve.authorize(f->in,f->local,f->work,f->ledger,Arm::control),"control permanently retires the original H despite an actual later change");
    check(f->reserve.authorize(f->in,f->local,f->work,f->ledger,Arm::active),"one actual changed feasible membership licenses one new context campaign");
    const auto* fresh=f->ledger.state(f->reserve.slot(1));
    check(fresh->positive_completions==0&&!fresh->campaign_started&&!f->ledger.organization_qualified(f->reserve.slot(1),f->package.systems[1].context.key,66),"new H inherits no old organization qualification");
    f->cycle(66,2); fresh=f->ledger.state(f->reserve.slot(1));
    check(fresh->positive_completions==1&&!f->ledger.organization_qualified(f->reserve.slot(1),f->package.systems[1].context.key,f->in.frame.clock.round),"one actual new member clear bank and return is only one fresh positive");
    f->cycle(110,2); fresh=f->ledger.state(f->reserve.slot(1));
    check(fresh->campaign_episodes==2&&fresh->positive_completions==2&&f->ledger.organization_qualified(f->reserve.slot(1),f->package.systems[1].context.key,f->in.frame.clock.round),"two own matched physical H episodes qualify only the fresh context");
    check(f->ledger.state(f->reserve.slot(0))->attempts==old.attempts&&f->ledger.state(f->reserve.slot(0))->positive_completions==old.positive_completions&&
        f->ledger.lifetime_failure_count()==failures,"new campaign retains old spent attempt and actual failure rather than rewriting history");
    f->ready(400,2); check(!f->reserve.witness_membership(f->in)&&!f->reserve.authorize(f->in,f->local,f->work,f->ledger,Arm::active),"time and another observation cannot replenish used requalification authority");
    check(decide(f->in,f->reserve,f->ledger,f->probes,Arm::active).option==0,"expired two-cycle proof cannot restart an exhausted campaign on a later epoch");
}
__attribute__((noinline)) void elapsed_time_is_not_change() {
    auto f=std::make_unique<Fixture>(); f->fail_old(); const auto failures=f->ledger.lifetime_failure_count();
    for (int round:{64,128,512,1024}) {
        f->ready(round,2); f->reserve.synchronize(f->ledger,round);
        check(!f->reserve.changed()&&!f->reserve.used()&&!f->reserve.authorize(f->in,f->local,f->work,f->ledger,Arm::active),"elapsed timer and new epoch cannot supply a missing causal change");
        check(decide(f->in,f->reserve,f->ledger,f->probes,Arm::active).option==0,"retired H keeps income in current independent U indefinitely");
    }
    check(f->ledger.lifetime_failure_count()==failures&&f->ledger.state(f->reserve.slot(0))->campaign_episodes==1,"waiting neither clears old failure nor replenishes attempts");
    auto untouched=std::make_unique<Fixture>(); untouched->ready(1024,1); untouched->reserve.synchronize(untouched->ledger,1024);
    check(!untouched->reserve.retired()&&!untouched->reserve.witness_membership(untouched->in),"time cannot retire an unattempted route to create a requalification opportunity");
    auto control=std::make_unique<Fixture>(); control->arm=Arm::control; control->fail_old(); control->ready(64,2);
    check(control->reserve.witness_membership(control->in)&&!control->reserve.authorize(control->in,control->local,control->work,control->ledger,Arm::control)&&
        decide(control->in,control->reserve,control->ledger,control->probes,Arm::control).option==0,"separate actual control failure permanently retires coupled H while current independent income remains available");
}
__attribute__((noinline)) void actual_route_inspection() {
    auto f=std::make_unique<Fixture>(ChangeKind::observed_route); f->fail_old();
    f->ready(64,2); check(!f->reserve.authorize(f->in,f->local,f->work,f->ledger,Arm::active),"merely supplying a different route hash is not a personally observed replacement route");
    f->in.offers[0].plan=f->package.systems[1].plan; auto& u=f->in.offers[0].plan; u.organization=Organization::U; u.crew={1,-1,-1,-1,-1,-1}; u.crew_count=1;
    f->in.offer_count=1; f->cycle(64,0,false,true);
    check(f->reserve.changed()&&!f->reserve.used(),"actual productive traversal personally observes all declared new ingress capture bank and renewal legs");
    check(!f->reserve.begin_route_observation(f->work),"completed Work cannot be retimestamped into another route change observation");
    f->in.offer_count=3; f->ready(110,2);
    check(f->reserve.authorize(f->in,f->local,f->work,f->ledger,Arm::active),"one physically observed replacement route licenses its exact new unqualified H");
    f->cycle(110,2); check(f->ledger.state(f->reserve.slot(1))->positive_completions==1,"inspection's actual independent income never substitutes for a fresh supported cycle");
    f->cycle(154,2); check(f->ledger.organization_qualified(f->reserve.slot(1),f->package.systems[1].context.key,f->in.frame.clock.round),"new route requires its own two physical supported returns");
}
__attribute__((noinline)) void own_gear_is_a_typed_feasibility_crossing() {
    auto f=std::make_unique<Fixture>(ChangeKind::own_gear); f->fail_old(); f->at_shop(64);
    const auto old=*f->ledger.state(f->reserve.slot(0));
    auto plan=std::make_unique<GearPlan>(); plan->without_purchase=f->in.offers[0].schedule; plan->with_purchase=plan->without_purchase;
    auto& bought=plan->with_purchase.actors[0];
    for (int e=0;e<bought.event_count;++e) bought.events[e].tick-=bought.events[e].kind==p6::EventKind::renewal?2:1;
    for (int p=0;p<plan->with_purchase.production_count;++p) if (plan->with_purchase.productions[p].holders[0]==1) --plan->with_purchase.productions[p].tick;
    bought.occupied_turns.value-=2; event(bought,p6::EventKind::purchase,9000,0); bought.events[bought.event_count-1].gold_cost={30,p6::Source::queried_setting};
    f->in.frame.own.gold=29; f->tick(64);
    check(!f->reserve.propose_gear(f->in,f->local,f->work,f->ledger,*plan).valid,"future source reward cannot finance a present unaffordable feasibility purchase");
    f->in.frame.own.gold=60; f->tick(64); f->in.market.items[0].price.source=p6::Source::synthetic_fixture;
    check(!f->reserve.propose_gear(f->in,f->local,f->work,f->ledger,*plan).valid,"unqueried item price cannot authorize actual gear spending");
    f->in.market.items[0].price.source=p6::Source::queried_setting;
    f->in.frame.own.actor.bag=12; f->tick(64);
    check(!f->reserve.propose_gear(f->in,f->local,f->work,f->ledger,*plan).valid,"own unbanked bag forbids the switch before safe conversion");
    f->in.frame.own.actor.bag=0; f->tick(64);
    auto no_gain=std::make_unique<GearPlan>(*plan); no_gain->with_purchase=no_gain->without_purchase;
    auto& unchanged=no_gain->with_purchase.actors[0]; event(unchanged,p6::EventKind::purchase,9000,0); unchanged.events[unchanged.event_count-1].gold_cost={30,p6::Source::queried_setting};
    check(!f->reserve.propose_gear(f->in,f->local,f->work,f->ledger,*no_gain).valid,"a named feasibility purchase must still beat a complete unchanged production alternative after its real expense");
    const auto command=f->reserve.propose_gear(f->in,f->local,f->work,f->ledger,*plan);
    check(command.valid&&command.cost==30&&!f->reserve.changed(),"one named affordable real item command is issued without inventing its successful endpoint");
    GearReceipt receipt{command.clock,command.clock,command.token,command.item,true,true};
    check(!f->reserve.confirm_gear(command,receipt,f->in),"a successful proposal with unchanged inventory and cash is not an actual purchase");
    f->in.market.inventory.value=f->package.new_inventory; f->in.frame.own.gold=30; f->in.frame.own.actor.strength=48; f->tick(64,1);
    receipt.observed_clock=f->in.frame.clock;
    auto bad=receipt; bad.observed_clock.round=65;
    check(!f->reserve.confirm_gear(command,bad,f->in),"stale or mismatched transaction endpoint is rejected");
    check(f->reserve.confirm_gear(command,receipt,f->in)&&!f->reserve.confirm_gear(command,receipt,f->in),"actual full inventory cash debit and queried fractional-capable stats confirm exactly once");
    check(f->reserve.changed()&&!f->reserve.used()&&own_boundary_feasible(f->package,f->in.own_state.value),"real named damage crosses the declared own two-hit fallback boundary");
    for (int k=1;k<3;++k) f->in.offers[k].safety[0].bank_executable=false;
    check(!f->reserve.authorize(f->in,f->local,f->work,f->ledger,Arm::active),"crossed strength boundary does not override a currently blocked bank path");
    f->ready(65,2); check(f->reserve.authorize(f->in,f->local,f->work,f->ledger,Arm::active),"confirmed funded gear change opens one actual current feasible new-build H campaign");
    f->cycle(65,2); f->cycle(110,2);
    check(f->ledger.organization_qualified(f->reserve.slot(1),f->package.systems[1].context.key,f->in.frame.clock.round)&&
        f->ledger.state(f->reserve.slot(0))->attempts==old.attempts,"new build earns two fresh own physical supported cycles while old failure remains retained");
    auto invalid=std::make_unique<Package>(f->package); invalid->maximum_solo_rounds.value=3; invalid->incoming_per_phase.value=0;
    check(!package_valid(*invalid,f->in.draft),"a nominal gear threshold which changes no declared feasibility is not a new-context license");
}
__attribute__((noinline)) void insulated_producers_and_probe_limits() {
    for (int owner:{2,3}) {
        auto f=std::make_unique<Fixture>(ChangeKind::visible_membership,owner); f->in.offer_count=1; f->source(10+owner);
        f->cycle(0,0); check(f->ledger.size()==0&&f->work.bank_seen&&f->work.renewal_seen,"independent producer and converter actually earn bank and renew without financing a helper or reading collector evidence");
    }
    auto f=std::make_unique<Fixture>();
    auto bad=std::make_unique<Package>(f->package); bad->systems[0].plan.crew[1]=2; bad->systems[0].context.key.participants[1]=2;
    check(!package_valid(*bad,f->in.draft),"insulated producer cannot be drafted into the coupled helper route");
    bad->systems[0].plan.crew[1]=3; bad->systems[0].context.key.participants[1]=3;
    check(!package_valid(*bad,f->in.draft),"insulated conversion actor cannot be used to finance or crew H");
    for (int epoch=0;epoch<common::max_probes_match;++epoch) check(f->probes.spend(epoch),"shared lifetime diagnostic budget is explicitly spent without reset");
    f->in.offers[1].diagnostic_segment=true; f->in.offers[1].marked_extra_turns=1;
    const auto d=decide(f->in,f->reserve,f->ledger,f->probes,Arm::active);
    check(d.option==0&&d.rejected[1]==Rejection::probe_limit&&f->ledger.state(f->reserve.slot(0))->attempts==0,"marked H cannot execute freely through an ordinary trial when diagnostic budget is exhausted");
}
__attribute__((noinline)) void helper_adopts_without_private_proof() {
    auto f=std::make_unique<Fixture>(ChangeKind::visible_membership,5);
    Plan p=route(1,true); p.crew[1]=5; p.ingress=path({{0,5},{1,4},{2,3},{2,2},{2,1}});
    p.capture=path({{2,1},{3,0}}); p.bank=path({{3,0},{3,1},{4,1}}); p.release=path({{3,0},{3,1}});
    p.renewal=path({{4,1},{3,1},{2,1},{1,2},{0,3},{0,4},{0,5}}); p.forecast_rounds=26;
    f->in.offers[1].plan=p; f->in.offer_count=2; f->ready(64,1); f->in.frame.own.actor.bag=10;
    f->actor(1).position=p.pads[0]; f->tick(64); for (int id=1;id<=5;++id) f->local.safety[id-1]=certificate(f->in.frame,id,8);
    f->in.visible_proposer.knowledge=p6::Knowledge::unknown;
    check(decide(f->in,f->reserve,f->ledger,f->probes,Arm::active).option==0,"helper's own empty ledger cannot infer a proposal it does not personally see");
    f->in.visible_proposer.knowledge=p6::Knowledge::exact;
    const auto d=decide(f->in,f->reserve,f->ledger,f->probes,Arm::active);
    check(d.option==1&&!d.record_episode&&!f->reserve.declared()&&f->ledger.size()==0,"real current helper adopts visible collector without any collector receipt or peer ledger argument");
    const auto admitted=commit(f->in,f->local,d,f->reserve,f->ledger,f->probes,Arm::active,f->work,f->trace);
    check(admitted.started&&!admitted.episode,"loaded helper begins actual productive work while preserving existing private bag");
    auto step=[&](int offset,p6::Point position,bool bank=false) {
        f->in.frame.own.actor.position=position; f->in.frame.own.actor.spawn_group=position==p6::Point{3,0}?8:-1;
        f->in.frame.own.at_spawn=position==p.ingress.tiles[0];
        if (bank) f->in.frame.own.actor.bag=0;
        f->tick(64+offset); if (bank) f->local.milestones={{8,false,true,0,10},f->in.frame.clock,5,p6::Knowledge::exact};
        const auto command=advance(f->in,f->local,f->reserve,f->ledger,Arm::active,f->work);
        check(f->work.confirm(command,{command.clock,command.clock,command.token,command.action,true,true}),"helper's actual local action receipt confirms current retained duty"); return command;
    };
    for (int i=0;i<4;++i) check(step(i,p.ingress.tiles[i]).action==PhysicalAction::move,"helper actually traverses its own current ingress");
    check(step(4,p.ingress.tiles[4]).action==PhysicalAction::attack&&step(5,p.ingress.tiles[4]).action==PhysicalAction::attack,"new helper performs actual useful source work rather than lending presence");
    f->source(8,p6::Lifecycle::reward_available); step(6,p.capture.tiles[0]); f->actor(1).position={3,0}; f->actor(1).spawn_group=8;
    check(step(7,{3,0}).action==PhysicalAction::move&&f->in.frame.own.actor.bag==10,"helper physically withdraws before future capture while retaining all existing gems");
    check(step(8,{3,1}).action==PhysicalAction::move&&f->work.helper_released,"actual release joins the prefix of helper's own bank route");
    check(step(9,{4,1}).action==PhysicalAction::tribute,"helper actually converts its retained pre-existing bag at its own altar duty");
    step(10,{4,1},true);
    for (int i=1;i<p.renewal.count;++i) step(10+i,p.renewal.tiles[i]);
    check(f->work.bank_seen&&f->work.renewal_seen&&!f->work.payout_seen&&f->in.frame.own.actor.bag==0&&f->ledger.size()==0,
        "helper's own bank and productive return neither erase existing property nor claim the collector's later capture as own evidence");
}
}
int main() {
    membership_reopens_only_once(); elapsed_time_is_not_change(); actual_route_inspection(); own_gear_is_a_typed_feasibility_crossing(); insulated_producers_and_probe_limits(); helper_adopts_without_private_proof();
    std::printf("plan8 requalification reserve: %d checks passed\n",checks); return 0;
}
