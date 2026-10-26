#include "../banded-cells/src/policy.h"
#include "../banded-cells/src/draft.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>

using namespace p8;
using namespace p8::banded_cells;
namespace {
int checks=0;
void check(bool value,const char* message) {
    ++checks; if (!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
template<class T> p6::Fact<T> fact(T value) { return {value,p6::Source::synthetic_fixture}; }
Route path(std::initializer_list<p6::Point> tiles) {
    Route route; route.verified=true; route.count=int(tiles.size());
    int i=0; for (auto tile:tiles) route.tiles[i++]=tile; return route;
}
__attribute__((noinline)) Plan circuit(int actor,int group,int y,Organization organization=Organization::U) {
    Plan plan; plan.organization=organization; plan.group=group; plan.collector=actor;
    plan.crew[0]=actor; plan.crew_count=1; plan.altar=4+y;
    plan.ingress=path({{0,y},{1,y},{2,y}}); plan.capture=path({{2,y},{3,y}});
    plan.bank=path({{3,y},{4,y}});
    plan.renewal=path({{4,y},{4,y+1},{3,y+1},{2,y+1},{1,y+1},{0,y}});
    plan.forecast_rounds=18; plan.remaining_rounds=100; plan.capture_window=5;
    plan.gem_bearing=plan.complete_verified=plan.rally_legal=true;
    plan.pads[0]={2,y}; plan.pads[1]={2,y+1}; plan.pads[2]={2,y+2};
    if (organization==Organization::H) { plan.crew[1]=3; plan.crew_count=2; }
    if (organization==Organization::J) {
        plan.collector=1; plan.crew={1,2,3,-1,-1,-1}; plan.crew_count=3;
        plan.bank=path({{3,y},{4,y+1},{4,y}});
    }
    return plan;
}
void event(p6::ActorSchedule& actor,p6::EventKind kind,int id,int tick,int production=-1) {
    p6::Event e; e.kind=kind; e.event_id=id; e.tick=tick; e.production_index=production;
    e.gold_cost=fact<p6::Gems>(0); e.source=p6::Source::synthetic_fixture;
    e.waypoint_id=kind==p6::EventKind::renewal?90+actor.actor_id:-1;
    actor.events[actor.event_count++]=e;
}
// Public primary sources7/8 and distinct secondary sources11/18 contain12
// each; source9 contains10. Neither cell consumes the other's assigned source.
// H preserves payloads and the observed one-turn first-clear lead; its helper's
// own bank is delayed. These continuations are synthetic, not native geography.
__attribute__((noinline)) p6::Schedule independent(const p6::Frame& frame,int collector=0,bool supported=false,int helper=3) {
    p6::Schedule schedule; schedule.target_id=collector==2?8:7;
    schedule.actor_count=3;
    for (int i=0;i<3;++i) {
        auto& actor=schedule.actors[i]; actor.actor_id=i+1;
        const auto* seen=frame.visible(i+1); const bool own=i+1==frame.clock.self;
        actor.initial_bag={seen?seen->bag:0,own?p6::Source::self_observed:p6::Source::visible_subject};
        actor.initial_gold={own?frame.own.gold:0,own?p6::Source::self_observed:p6::Source::conservative};
        const bool late=supported && i+1==helper;
        const int finish=i+1==collector?(supported?44:45):late?48:18;
        actor.occupied_turns=fact<p6::Gems>(finish);
        if (late && actor.initial_bag.value>0) event(actor,p6::EventKind::tribute,2100+i,15);
        const int cycles=i+1==collector?2:1;
        for (int cycle=0;cycle<cycles;++cycle) {
            const int ix=schedule.production_count++; auto& production=schedule.productions[ix];
            const int source=i==0?(cycle?11:7):i==1?(cycle?18:8):9;
            production.event_id=1000+source;
            production.tick=cycle?(supported?36:37):late?42:supported&&i+1==collector?11:12;
            production.gems=fact<p6::Gems>(i==2?10:12); production.gold=fact<p6::Gems>(0);
            production.source=p6::Source::synthetic_fixture; production.holders[0]=i+1; production.holder_count=1;
            event(actor,p6::EventKind::production,production.event_id,production.tick,ix);
            event(actor,p6::EventKind::tribute,2000+production.event_id,cycle?(supported?39:40):late?45:supported&&i+1==collector?14:15);
        }
        event(actor,p6::EventKind::renewal,3000+i,finish);
    }
    return schedule;
}
__attribute__((noinline)) p6::Schedule joint(const p6::Frame& frame) {
    auto schedule=independent(frame); schedule.production_count=2; schedule.target_id=7;
    // Shared primary7 still pays12. Named secondary10 pays36 and requires
    // the whole presently visible crew; U has no solo certificate for source10.
    for (int p=0;p<2;++p) {
        auto& production=schedule.productions[p]; production.event_id=700+p; production.tick=10+p*12;
        production.gems=fact<p6::Gems>(p?36:12); production.gold=fact<p6::Gems>(0);
        production.holder_count=3; production.holders={1,2,3,0,0,0}; production.source=p6::Source::synthetic_fixture;
    }
    for (int i=0;i<3;++i) {
        auto& actor=schedule.actors[i]; actor.event_count=0; actor.occupied_turns=fact<p6::Gems>(35+4*i);
        for (int p=0;p<2;++p) { event(actor,p6::EventKind::production,700+p,10+p*12,p); event(actor,p6::EventKind::tribute,1700+p,13+p*12+4*i); }
        event(actor,p6::EventKind::renewal,3000+i,35+4*i);
    }
    return schedule;
}
p6::CycleCertificate safety(const p6::Frame& frame,int id,int group) {
    p6::CycleCertificate safe; safe.clock=frame.clock; safe.actor=id; safe.group=group;
    const auto* actor=frame.visible(id);
    if (actor) safe.clear=safe.hold=safe.bank=safe.recovery={actor->health,actor->max_health,10,0,0,0,true};
    safe.route_known=safe.bank_executable=safe.renewal_reachable=safe.solo=true; return safe;
}
ActionReceipt receipt(const Command& command) { return {command.clock,command.clock,command.token,command.action,true,true}; }
struct Fixture {
    Inputs in{}; EvidenceLedger ledger; ProbeBudget probes{}; Work work{}; LocalFrame local{}; CycleTrace trace{};
    ContextDeclaration component{},supported{}; int self=1,y=0,group=7;
    __attribute__((noinline)) explicit Fixture(int actor=1):ledger(actor),self(actor),y(actor==2?6:0),group(actor==2?8:7) {
        in.frame.clock={0,self,0}; in.frame.own.gold=15;
        in.draft.champions={p6::Champion::tavi,p6::Champion::varg,p6::Champion::marcella,p6::Champion::other,p6::Champion::other};
        in.draft.actors={1,2,3,-1,-1}; in.draft.picks=in.picks=3;
        for (int id=1;id<=3;++id) {
            p6::Actor a; a.id=id; a.pick_ordinal=id-1; a.type=in.draft.champions[id-1]; a.team=p6::Team::ally;
            a.health=a.max_health=100; a.strength=20; a.alive=a.can_attack=a.can_move=true;
            a.position=id==self?p6::Point{0,y}:p6::Point{2,id==2?6:id==3?y+1:0};
            if (id==self) in.frame.own.actor=a; else in.frame.actors[in.frame.actor_count++]={a,in.frame.clock,id,p6::Knowledge::exact};
        }
        in.settings={2000,1000,5}; in.settings_identity={17,p6::Source::queried_setting}; in.target_group=group;
        p6::Group live; live.id=group; live.original_spawn_visible=live.all_spawns_visible=live.uncontested_now=true;
        live.lifecycle=p6::Lifecycle::living; in.frame.group={live,in.frame.clock,group,p6::Knowledge::exact};
        auto candidates=std::make_unique<std::array<CircuitCandidate,3>>();
        (*candidates)[0]={circuit(1,7,0),fact(2),fact(true)};
        (*candidates)[1]={circuit(2,7,0),fact(2),fact(true)};
        (*candidates)[2]={circuit(2,8,6),fact(2),fact(true)};
        Assignments::select_into(in.assignments,in.draft,*candidates);
        in.own_state={{100,100,20,10,1,0,99},in.frame.clock,self,p6::Knowledge::exact};
        in.threat_regime={0,in.frame.clock,self,p6::Knowledge::exact};
        in.offer_count=3; in.independent=0;
        in.offers[0].plan=circuit(self,group,y); in.offers[0].schedule=independent(in.frame,self);
        in.offers[1].plan=circuit(self,group,y,Organization::H); in.offers[1].schedule=independent(in.frame,self,true);
        in.offers[2].plan=circuit(1,7,0,Organization::J); in.offers[2].schedule=joint(in.frame);
        in.control_route=fact(joint_route_hash(in.offers[2].plan));
        component.key.settings_id=17; component.key.route_hash=plan_route_hash(in.offers[0].plan);
        component.key.actor_id=self; component.key.champion=int(in.frame.own.actor.type); component.key.role=self-1;
        component.key.source_group=group; component.key.route_template=self-1; component.key.endpoint=4+y; component.key.threat_regime=0;
        component.key.participants[0]=self; component.key.participant_count=1; component.key.build={99,100,20,10,1};
        component.envelope={{20,100},{100,100},{20,20},{10,10},{1,1},{0,100},0};
        for (int h=0;h<3;++h) component.hypotheses[h].offset={2*h,2*h+2};
        supported=component; supported.kind=EpisodeKind::full_organization; supported.require_supported_work=true;
        supported.key.role+=2; supported.key.participants[1]=3; supported.key.participant_count=2;
        if (self<=2) {
            in.component_slot=ledger.predeclare(component); in.supported_slot=ledger.predeclare(supported);
            in.component_entry=in.offers[0].entry=component.key; in.supported_entry=in.offers[1].entry=supported.key;
            in.offers[0].context_slot=in.component_slot; in.offers[1].context_slot=in.supported_slot;
            check(in.component_slot>=0 && in.supported_slot>=0,"cell owns distinct predeclared component and H slots");
        }
        tick(0);
    }
    __attribute__((noinline)) void tick(int round) {
        auto& frame=in.frame; frame.clock.round=round; frame.group.clock=frame.clock;
        for (int i=0;i<frame.actor_count;++i) frame.actors[i].clock=frame.clock;
        in.own_state.clock=in.threat_regime.clock=frame.clock;
        in.own_state.value.health=int(frame.own.actor.health); in.own_state.value.max_health=int(frame.own.actor.max_health);
        in.own_state.value.strength=int(frame.own.actor.strength); in.own_state.value.bag=int(frame.own.actor.bag);
        in.windows.valid=true; in.windows.source=p6::Source::synthetic_fixture; in.windows.windows=in.windows.deadlines={39,39,39};
        in.windows.sampled_actor_id=self; in.windows.sampled_round=round;
        for (int o=0;o<in.offer_count;++o) {
            auto& offer=in.offers[o]; offer.safety_count=3;
            for (int i=0;i<3;++i) offer.safety[i]=safety(frame,i+1,participant(offer.plan,i+1)?offer.plan.group:i==0?7:i==1?8:9);
        }
        observe_plan(work.active?work.plan:in.offers[0].plan);
    }
    __attribute__((noinline)) void observe_plan(const Plan& plan) {
        const auto& frame=in.frame;
        local.frame=frame; local.current_round=frame.clock.round; local.safety_count=3; local.milestones={};
        for (int i=0;i<3;++i) local.safety[i]=safety(frame,i+1,plan.group);
        local.legal={{},frame.clock,self,p6::Knowledge::exact}; auto& legal=local.legal.value;
        legal.safe_now=legal.attack_legal=legal.altar_legal=legal.recall_safe=legal.renewal_productive=true;
        for (Leg leg:{Leg::ingress,Leg::capture,Leg::bank,Leg::renewal,Leg::release}) {
            const auto& route=route_for(plan,leg); auto& access=legal.routes[int(leg)];
            access.assumptions_hold=route_valid(route); access.next_move_legal=true;
            for (int i=0;i<route.count;++i) if (route.tiles[i]==frame.own.actor.position) {
                access.next_tile=route.tiles[std::min(i+1,route.count-1)]; break;
            }
        }
    }
    __attribute__((noinline)) void reset(int start) {
        in.frame.own.actor.position={0,y}; in.frame.own.actor.bag=0; in.frame.own.actor.spawn_group=-1;
        in.frame.own.actor.channel=p6::Channel::none; in.frame.group.value.lifecycle=p6::Lifecycle::living;
        for (int i=0;i<in.frame.actor_count;++i) if (in.frame.actors[i].value.id==3) {
            in.frame.actors[i].value.position={2,y+1}; in.frame.actors[i].value.spawn_group=-1; in.frame.actors[i].value.action=p6::WitnessAction::none;
        }
        work={}; tick(start); in.offers[0].schedule=independent(in.frame,self); in.offers[1].schedule=independent(in.frame,self,true);
        in.offers[2].schedule=joint(in.frame);
    }
    __attribute__((noinline)) void cycle(int start,bool h=false) {
        reset(start); const auto decision=decide(in,ledger,probes,Arm::active);
        check(decision.option==(h?1:0) && !decision.deliberate_probe,"each cell uses its own ordinary U or earned H");
        const auto admission=commit(in,local,decision,ledger,probes,Arm::active,work);
        check(admission.started && admission.episode && trace.begin(ledger,work),"own issue precedes clean physical cycle and typed trace");
        auto step=[&](int offset,p6::Point position,bool payout=false,bool bank=false) {
            in.frame.own.actor.position=position; in.frame.own.actor.spawn_group=position==p6::Point{3,y}?group:-1;
            if (payout) in.frame.own.actor.bag=12;
            if (bank) { in.frame.own.actor.bag=0; in.frame.own.actor.channel=p6::Channel::none; }
            tick(start+offset);
            if (payout || bank) local.milestones={{group,payout,bank,payout?12:0,bank?12:0},in.frame.clock,self,p6::Knowledge::exact};
            auto command=advance(in,local,ledger,Arm::active,work);
            if (command.action==PhysicalAction::move || command.action==PhysicalAction::attack || command.action==PhysicalAction::tribute)
                check(work.confirm(command,receipt(command)),"same-clock successful own action has an actual receipt");
            trace.observe(local,work,{}); return command;
        };
        check(step(0,{0,y}).action==PhysicalAction::move,"cell starts its retained route"); step(1,{1,y});
        if (h) for (int i=0;i<in.frame.actor_count;++i) if (in.frame.actors[i].value.id==3) {
            in.frame.actors[i].value.action=p6::WitnessAction::useful_attack; in.frame.actors[i].value.action_group=group;
        }
        check(step(2,{2,y}).action==PhysicalAction::attack,"own productive attack is physically executed"); step(3,{2,y});
        const int shift=h?-1:0; if (!h) step(4,{2,y});
        in.frame.group.value.lifecycle=p6::Lifecycle::reward_available; step(5+shift,{2,y});
        for (int round=6+shift;round<=11+shift;++round) {
            if (h) for (int i=0;i<in.frame.actor_count;++i) if (in.frame.actors[i].value.id==3) {
                auto& helper=in.frame.actors[i].value; helper.action=p6::WitnessAction::none;
                helper.position=round<8+shift?p6::Point{3,y+1}:p6::Point{4,y+1}; helper.spawn_group=round<8+shift?group:-1;
            }
            check(step(round,{3,y}).action==PhysicalAction::hold,"capture waits through the full observed window");
        }
        check(step(12+shift,{3,y},true).action==PhysicalAction::move,"actual own twelve-gem payout preserves fixed source payload");
        check(step(13+shift,{4,y}).action==PhysicalAction::tribute,"own bag reaches an actual whole-bag tribute");
        in.frame.own.actor.channel=p6::Channel::tribute; in.frame.own.actor.channel_valid=true;
        check(step(14+shift,{4,y}).action==PhysicalAction::preserve_channel,"valid own channel survives physical production");
        step(15+shift,{4,y},false,true);
        const std::array<p6::Point,5> waypoints{{{4,y+1},{3,y+1},{2,y+1},{1,y+1},{0,y}}};
        for (int i=0;i<5;++i) step(16+shift+i,waypoints[i]);
        check(work.cycle_completed && work.bank_seen && work.renewal_seen,"actual payout bank and renewed work complete the cell");
        const auto result=trace.finish(ledger,in.frame.clock.round,{h?supported.key:component.key,in.own_state.value,all_premises});
        check(result.full_positive,"private typed own trace supplies one matched completed episode");
    }
};
__attribute__((noinline)) void assignment_boundaries() {
    auto fixture=std::make_unique<Fixture>(); auto& draft=fixture->in.draft;
    auto candidates=std::make_unique<std::array<CircuitCandidate,3>>();
    (*candidates)[0]={circuit(1,7,0),fact(2),fact(true)};
    (*candidates)[1]={circuit(2,7,0),fact(2),fact(true)};
    (*candidates)[2]={circuit(2,8,6),fact(2),fact(true)};
    auto assignment=std::make_unique<Assignments>(); Assignments::select_into(*assignment,draft,*candidates);
    check(assignment->valid(draft) && assignment->for_ordinal(0)->group==7 && assignment->for_ordinal(1)->group==8,
          "later draft cell resolves same nearest group by its next distinct complete source");
    std::swap((*candidates)[0],(*candidates)[2]); Assignments::select_into(*assignment,draft,*candidates);
    check(assignment->valid(draft) && assignment->for_ordinal(1)->group==8,"candidate enumeration order does not change deterministic allocation");
    (*candidates)[0].solo_first_income=fact(false); Assignments::select_into(fixture->in.assignments,draft,*candidates);
    check(!fixture->in.assignments.valid(draft) && decide(fixture->in,fixture->ledger,fixture->probes,Arm::active).reason==Rejection::opportunity_unavailable,
          "missing second certified complete circuit blocks the declared experiment");
    (*candidates)[0]={circuit(2,8,0),fact(3),fact(true)};
    check(!Assignments::select_into(*assignment,draft,*candidates),"different IDs on identical actual capture/return geometry are not distinct cells");
    (*candidates)[0]={circuit(2,8,6),fact(3),{true,p6::Source::public_score}};
    check(!Assignments::select_into(*assignment,draft,*candidates),"team score cannot certify another cell's first income");
    (*candidates)[0]={circuit(1,7,0),fact(2),fact(true)};
    (*candidates)[1]={circuit(1,8,6),fact(3),fact(true)};
    (*candidates)[2]={circuit(2,7,0),fact(2),fact(true)};
    Assignments::select_into(*assignment,draft,*candidates);
    check(assignment->valid(draft) && assignment->for_ordinal(0)->group==8 && assignment->for_ordinal(1)->group==7,
          "finite pair assignment preserves an available distinct pair when the second actor has only the shared nearest source");
}
__attribute__((noinline)) void active_control_physical() {
    auto active=std::make_unique<Fixture>(), control=std::make_unique<Fixture>();
    auto a=decide(active->in,active->ledger,active->probes,Arm::active);
    auto c=decide(control->in,control->ledger,control->probes,Arm::control);
    check(a.organization==Organization::U && c.organization==Organization::J,"same input changes separate circuits versus one visible core expedition");
    check(commit(active->in,active->local,a,active->ledger,active->probes,Arm::active,active->work).started,
          "active commits an actual independent physical route");
    check(commit(control->in,control->local,c,control->ledger,control->probes,Arm::control,control->work).started,
          "control commits an actual J physical route");
    check(active->work.plan.crew_count==1 && control->work.plan.crew_count==3 && !control->ledger.active(),
          "physical crew responsibility differs and control does not borrow private evidence");
    control->in.frame.actors[1].knowledge=p6::Knowledge::unknown; control->tick(1);
    control->local.frame=control->in.frame;
    auto command=advance(control->in,control->local,control->ledger,Arm::control,control->work);
    check(control->work.failure==PhysicalFailure::missing_member && !control->work.cycle_completed && command.action!=PhysicalAction::attack,
          "missing J member cannot remain promised damage or certify a joint cycle");
    auto wrong=std::make_unique<Fixture>(); wrong->in.control_route.value^=1;
    check(decide(wrong->in,wrong->ledger,wrong->probes,Arm::control).organization==Organization::U,
          "control cannot silently switch to another shared route");
    auto second=std::make_unique<Fixture>(2); auto& jp=second->in.offers[2].plan;
    jp.ingress=path({{0,6},{0,5},{0,4},{0,3},{0,2},{0,1},{1,1},{2,1}});
    jp.capture=path({{2,1},{3,1}}); jp.bank=path({{3,1},{4,1},{4,0}}); jp.forecast_rounds=30;
    second->tick(0);
    check(joint_route_hash(jp)==second->in.control_route.value,"distinct actor capture prefix joins the same actual J bank and return corridor");
    second->observe_plan(jp);
    const auto other_control=decide(second->in,second->ledger,second->probes,Arm::control);
    check(other_control.organization==Organization::J &&
          commit(second->in,second->local,other_control,second->ledger,second->probes,Arm::control,second->work).started,
          "the second core actor can physically enter the shared control campaign through its own legal assembly pad");
}
__attribute__((noinline)) void independent_failure_and_earned_second_cell() {
    auto failed=std::make_unique<Fixture>(1), survivor=std::make_unique<Fixture>(2);
    const auto initial=decide(failed->in,failed->ledger,failed->probes,Arm::active);
    check(commit(failed->in,failed->local,initial,failed->ledger,failed->probes,Arm::active,failed->work).started,
          "first cell starts its own proposal before a local failure");
    failed->tick(1); failed->local.legal.value.safe_now=false;
    advance(failed->in,failed->local,failed->ledger,Arm::active,failed->work);
    check(!failed->work.active && failed->ledger.campaign_exhausted(failed->in.component_slot),"observed local unsafe failure retires only that own campaign");
    const auto failures=failed->ledger.lifetime_failure_count();
    survivor->cycle(0); survivor->cycle(40);
    check(own_component(survivor->in,survivor->ledger) && !own_component(failed->in,failed->ledger),
          "unaffected second cell banks twice and qualifies only its own return component");
    check(failed->ledger.lifetime_failure_count()==failures && survivor->probes.used==0,
          "surviving actor work neither mutates remote private history nor resets probe budgets");
    survivor->cycle(80,true);
    check(!own_organization(survivor->in,survivor->ledger),"one actual ordinal-one H cycle does not qualify new membership");
    survivor->cycle(120,true);
    check(own_organization(survivor->in,survivor->ledger),"actual second cell collector earns H through two own supported cycles");
    failed->reset(160); const auto fallback=decide(failed->in,failed->ledger,failed->probes,Arm::active);
    check(fallback.organization==Organization::U && !fallback.record_episode,"suspended cell keeps productive U without retrying or summoning the other cell");
}
__attribute__((noinline)) void private_helper_bag_and_channel() {
    auto fixture=std::make_unique<Fixture>(3); auto& in=fixture->in;
    in.frame.own.actor.position={0,0}; in.frame.own.actor.bag=6;
    for (int i=0;i<in.frame.actor_count;++i) if (in.frame.actors[i].value.id==1) in.frame.actors[i].value.position={2,0};
    in.target_group=7; in.offer_count=2; in.independent=0;
    in.offers[0].plan=circuit(3,9,0); in.offers[0].schedule=independent(in.frame,1); in.offers[0].schedule.target_id=9;
    auto& plan=in.offers[1].plan; plan=circuit(1,7,0,Organization::H);
    plan.ingress=path({{0,0},{1,0},{2,1}}); plan.capture=path({{2,1},{3,0}});
    plan.bank=path({{3,0},{3,1},{4,1}}); plan.release=path({{3,0},{3,1}}); plan.renewal=path({{4,1},{4,2}}); plan.forecast_rounds=24;
    in.offers[1].schedule=independent(in.frame,1,true); fixture->tick(0);
    check(decide(in,fixture->ledger,fixture->probes,Arm::active).organization==Organization::U,"helper needs a visible named proposal without reading leader proof");
    in.proposals[0]={{7,in.frame.clock,1,p6::Knowledge::exact},fact(2)};
    const auto decision=decide(in,fixture->ledger,fixture->probes,Arm::active);
    check(decision.option==1 && !decision.record_episode,"helper accepts current feasible named collector without private proof transfer");
    check(!physical_admissible(plan,fixture->local),"independent source safety certificate cannot authorize a different supported source");
    fixture->observe_plan(plan);
    check(commit(in,fixture->local,decision,fixture->ledger,fixture->probes,Arm::active,fixture->work).started,"loaded helper starts its own retained release and bank route");
    auto step=[&](int round,p6::Point position) {
        in.frame.own.actor.position=position; in.frame.own.actor.spawn_group=position==p6::Point{3,0}?7:-1; fixture->tick(round);
        auto command=advance(in,fixture->local,fixture->ledger,Arm::active,fixture->work);
        if (command.action==PhysicalAction::move || command.action==PhysicalAction::attack || command.action==PhysicalAction::tribute)
            check(fixture->work.confirm(command,receipt(command)),"helper independently confirms actual own actions");
        return command;
    };
    step(0,{0,0}); step(1,{1,0}); check(step(2,{2,1}).action==PhysicalAction::attack,"helper physically contributes clearance");
    in.frame.group.value.lifecycle=p6::Lifecycle::reward_available; step(3,{2,1});
    check(step(4,{3,0}).action==PhysicalAction::hold && in.frame.own.actor.bag==6,"helper keeps old bag and holds until safe replacement is personally witnessed");
    for (int i=0;i<in.frame.actor_count;++i) if (in.frame.actors[i].value.id==1) {
        in.frame.actors[i].value.position={3,2}; in.frame.actors[i].value.spawn_group=7;
    }
    step(5,{3,0}); step(6,{3,1}); check(step(7,{4,1}).action==PhysicalAction::tribute,"released helper banks its own unchanged six-gem bag");
    in.frame.own.actor.channel=p6::Channel::tribute; in.frame.own.actor.channel_valid=true;
    for (int i=0;i<in.frame.actor_count;++i) if (in.frame.actors[i].value.id==1) in.frame.actors[i].knowledge=p6::Knowledge::unknown;
    check(step(8,{4,1}).action==PhysicalAction::preserve_channel,"lost proposer preserves a valid own tribute channel");
    in.frame.own.actor.channel=p6::Channel::none; in.frame.own.actor.bag=0; fixture->tick(9);
    fixture->local.milestones={{7,false,true,0,6},in.frame.clock,3,p6::Knowledge::exact};
    auto command=advance(in,fixture->local,fixture->ledger,Arm::active,fixture->work);
    check(command.action==PhysicalAction::move && fixture->work.confirm(command,receipt(command)),"actual own conversion still reaches independent renewed work");
    step(10,{4,2}); check(fixture->work.renewal_seen && !fixture->work.payout_seen && !fixture->ledger.active(),
          "helper neither claims collector payout nor imports an active leader episode");
}
__attribute__((noinline)) void profile_and_forbidden_diagnostics() {
    auto fixture=std::make_unique<Fixture>();
    check(champion_pool.size()==8 && draft_role(0)==DraftRole::collector && draft_role(1)==DraftRole::collector &&
          draft_role(2)==DraftRole::home_production && draft_role(4)==DraftRole::safe_helper,
          "broad per-design draft retains two cell leaders and independent producer/extension roles");
    fixture->in.offers[0].diagnostic_segment=true;
    check(decide(fixture->in,fixture->ledger,fixture->probes,Arm::active).option<0 && fixture->probes.used==0,
          "unrepresented diagnostic continuation is rejected without inventing future qualification or spending a probe");
}
__attribute__((noinline)) void optional_helpers_choose_nearest() {
    for (int helper_id:{4,5}) {
        auto fixture=std::make_unique<Fixture>(3); auto& in=fixture->in; auto& frame=in.frame;
        auto old=frame.own.actor; old.position={2,2}; frame.actors[frame.actor_count++]={old,frame.clock,3,p6::Knowledge::exact};
        in.draft.picks=in.picks=helper_id; in.draft.actors[3]=4; in.draft.champions[3]=p6::Champion::willow;
        if (helper_id==5) {
            in.draft.actors[4]=5; in.draft.champions[4]=p6::Champion::vesper_vale;
            auto fourth=old; fourth.id=4; fourth.pick_ordinal=3; fourth.type=p6::Champion::willow; fourth.position={1,2};
            frame.actors[frame.actor_count++]={fourth,frame.clock,4,p6::Knowledge::exact};
        }
        frame.own.actor.id=helper_id; frame.own.actor.pick_ordinal=helper_id-1;
        frame.own.actor.type=in.draft.champions[helper_id-1]; frame.own.actor.position={0,0};
        frame.clock.self=helper_id; fixture->self=helper_id; in.own_state.subject=in.threat_regime.subject=helper_id;
        auto candidates=std::make_unique<std::array<CircuitCandidate,2>>();
        (*candidates)[0]={circuit(1,7,0),fact(2),fact(true)}; (*candidates)[1]={circuit(2,8,6),fact(2),fact(true)};
        Assignments::select_into(in.assignments,in.draft,*candidates); in.offer_count=3;
        for (int i=0;i<frame.actor_count;++i) if (frame.actors[i].value.id==1) frame.actors[i].value.position={2,0};
        in.offers[0].plan=circuit(helper_id,12,0);
        auto& near=in.offers[1].plan; near=circuit(1,7,0,Organization::H); near.crew[1]=helper_id;
        near.ingress=path({{0,0},{1,0},{2,1}}); near.capture=path({{2,1},{3,0}});
        near.bank=path({{3,0},{3,1},{4,1}}); near.release=path({{3,0},{3,1}});
        near.renewal=path({{4,1},{4,2}}); near.forecast_rounds=24;
        auto& far=in.offers[2].plan; far=circuit(2,8,6,Organization::H); far.crew[1]=helper_id;
        far.ingress=path({{0,0},{0,1},{0,2},{0,3},{0,4},{0,5},{0,6},{1,6},{2,7}});
        far.capture=path({{2,7},{3,6}}); far.bank=path({{3,6},{3,7},{4,7}});
        far.release=path({{3,6},{3,7}}); far.renewal=path({{4,7},{4,8}}); far.forecast_rounds=32;
        auto with_helper=[&](int collector,bool h) {
            auto schedule=independent(frame,collector,h,helper_id); auto& actor=schedule.actors[schedule.actor_count++];
            actor.actor_id=helper_id; actor.initial_bag={0,p6::Source::self_observed}; actor.initial_gold={15,p6::Source::self_observed};
            actor.occupied_turns=fact<p6::Gems>(h?48:18);
            const int ix=schedule.production_count++; auto& production=schedule.productions[ix];
            production.event_id=1200+helper_id; production.tick=h?42:12; production.gems=fact<p6::Gems>(8); production.gold=fact<p6::Gems>(0);
            production.holders[0]=helper_id; production.holder_count=1; production.source=p6::Source::synthetic_fixture;
            event(actor,p6::EventKind::production,production.event_id,production.tick,ix);
            event(actor,p6::EventKind::tribute,2200+helper_id,h?45:15); event(actor,p6::EventKind::renewal,3200+helper_id,h?48:18);
            return schedule;
        };
        in.offers[0].schedule=with_helper(1,false); in.offers[0].schedule.target_id=12;
        in.offers[1].schedule=with_helper(1,true); in.offers[2].schedule=with_helper(2,true);
        // The farther pair's declared secondary13 is a distinct eighteen-gem
        // coalition opportunity, not an enlarged source8 payout. Its longer
        // first assembly is charged explicitly before the same late bank.
        for (auto& production:in.offers[2].schedule.productions)
            if (production.event_id==1008) production.tick+=6;
            else if (production.event_id==1018) { production.event_id=1313; production.gems=fact<p6::Gems>(18); }
        auto& far_collector=in.offers[2].schedule.actors[1];
        for (int e=0;e<far_collector.event_count;++e) {
            auto& action=far_collector.events[e];
            if (action.event_id==1008 || action.event_id==3008) action.tick+=6;
            if (action.event_id==1018) action.event_id=1313;
        }
        fixture->tick(0);
        for (int o=0;o<in.offer_count;++o) {
            in.offers[o].safety_count=4; in.offers[o].safety[3]=safety(frame,helper_id,in.offers[o].plan.group);
        }
        in.proposals[0]={{7,frame.clock,1,p6::Knowledge::exact},fact(2)};
        in.proposals[1]={{8,frame.clock,2,p6::Knowledge::exact},fact(8)};
        auto private_ledger=std::make_unique<EvidenceLedger>(helper_id);
        check(decide(in,*private_ledger,fixture->probes,Arm::active).option==1,
              "fourth and fifth actors choose nearest current safe visible proposal by actual retained route");
        in.proposals[0].named_group.knowledge=p6::Knowledge::unknown;
        check(decide(in,*private_ledger,fixture->probes,Arm::active).option==2,
              "unobserved nearer proposal cannot displace an actually visible other cell");
        in.proposals[1].named_group.knowledge=p6::Knowledge::unknown;
        check(decide(in,*private_ledger,fixture->probes,Arm::active).organization==Organization::U && private_ledger->size()==0,
              "optional helper without visible adoption retains U and an empty private ledger");
    }
}
}
int main() {
    {
        const auto assigned=circuit(1,7,0);
        auto entry=assigned;entry.ingress=path({{0,1},{1,0},{2,0}});
        check(assigned_circuit(assigned,entry),"public district assignment permits a fresh actual entry prefix");
        auto supported=assigned;supported.organization=Organization::H;
        supported.capture=path({{2,1},{3,0}});
        check(assigned_circuit(assigned,supported),"supported assembly prefix does not replace assigned bank and return");
        supported.bank.tiles[1].y+=1;
        check(!assigned_circuit(assigned,supported),"changed actual bank dependency is a different assigned circuit");
    }
    assignment_boundaries(); active_control_physical(); independent_failure_and_earned_second_cell();
    private_helper_bag_and_channel(); profile_and_forbidden_diagnostics(); optional_helpers_choose_nearest();
    std::printf("Banded Cells: %d checks passed\n",checks); return 0;
}
