#include "../underwritten-frontier/src/policy.h"
#include "../underwritten-frontier/src/draft.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>
using namespace p8;
using namespace p8::underwritten_frontier;
namespace {
int checks=0;
void check(bool value,const char* message) { ++checks; if (!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); } }
template<class T> p6::Fact<T> fact(T value) { return {value,p6::Source::synthetic_fixture}; }
Route path(std::initializer_list<p6::Point> tiles) {
    Route out; out.verified=true; for (auto tile:tiles) out.tiles[out.count++]=tile; return out;
}
__attribute__((noinline)) Plan circuit(int actor,int group=7,int y=0,Organization organization=Organization::U) {
    Plan plan; plan.organization=organization; plan.group=group; plan.collector=actor; plan.altar=4+y;
    plan.crew[0]=actor; plan.crew_count=1;
    plan.ingress=path({{0,y},{1,y},{2,y}}); plan.capture=path({{2,y},{3,y}}); plan.bank=path({{3,y},{4,y}});
    plan.renewal=path({{4,y},{4,y+1},{3,y+1},{2,y+1},{1,y+1},{0,y}});
    plan.forecast_rounds=18; plan.remaining_rounds=100; plan.capture_window=5;
    plan.gem_bearing=plan.complete_verified=plan.rally_legal=true;
    plan.pads[0]={2,0}; plan.pads[1]={2,1}; plan.pads[2]={2,2};
    if (organization==Organization::H) { plan.crew={3,2,-1,-1,-1,-1}; plan.crew_count=2; }
    if (organization==Organization::J) { plan.crew={3,2,1,-1,-1,-1}; plan.crew_count=3; }
    return plan;
}
void event(p6::ActorSchedule& actor,p6::EventKind kind,int id,int tick,int production=-1) {
    p6::Event e; e.kind=kind; e.event_id=id; e.tick=tick; e.production_index=production;
    e.gold_cost=fact<p6::Gems>(0); e.source=p6::Source::synthetic_fixture;
    e.waypoint_id=kind==p6::EventKind::renewal?90+actor.actor_id:-1; actor.events[actor.event_count++]=e;
}
__attribute__((noinline)) p6::Schedule single(const p6::Frame& frame,int actor,bool venture=false,bool h=false) {
    p6::Schedule out; out.actor_count=1; out.target_id=actor==1?3:actor==2?8:actor==4?9:actor==5?13:7;
    auto& a=out.actors[0]; a.actor_id=actor; const auto* seen=frame.visible(actor); const bool own=actor==frame.clock.self;
    a.initial_bag={seen?seen->bag:0,own?p6::Source::self_observed:p6::Source::visible_subject};
    a.initial_gold={own?frame.own.gold:0,own?p6::Source::self_observed:p6::Source::conservative};
    const bool late=actor==2 && h; const int finish=actor==3?(h?44:45):late?48:18;
    a.occupied_turns=fact<p6::Gems>(finish);
    const int cycles=actor==3 && venture?2:1;
    for (int cycle=0;cycle<cycles;++cycle) {
        const int source=actor==1?3:actor==2?8:actor==4?9:actor==5?13:cycle?11:7;
        auto& p=out.productions[out.production_count++]; p.event_id=1000+source;
        p.tick=cycle?(h?36:37):late?42:h?11:12;
        p.gems=fact<p6::Gems>(actor==3?12:10); p.gold=fact<p6::Gems>(0);
        p.holders[0]=actor; p.holder_count=1; p.source=p6::Source::synthetic_fixture;
        event(a,p6::EventKind::production,p.event_id,p.tick,cycle);
        event(a,p6::EventKind::tribute,2000+source,cycle?(h?39:40):late?45:h?14:15);
    }
    event(a,p6::EventKind::renewal,3000+actor,finish); return out;
}
void append(p6::Schedule& whole,const p6::Schedule& source) {
    auto actor=source.actors[0]; const int offset=whole.production_count;
    for (int e=0;e<actor.event_count;++e) if (actor.events[e].production_index>=0) actor.events[e].production_index+=offset;
    whole.actors[whole.actor_count++]=actor;
    for (int p=0;p<source.production_count;++p) whole.productions[whole.production_count++]=source.productions[p];
}
__attribute__((noinline)) p6::Schedule aggregate(const p6::Frame& frame,bool full=false,bool h=false,bool j=false) {
    p6::Schedule out; out.target_id=7;
    if (full) append(out,single(frame,1));
    append(out,single(frame,2,true,h)); append(out,single(frame,3,true,h));
    if (!j) return out;
    out.production_count=2;
    for (int p=0;p<2;++p) {
        auto& production=out.productions[p]; production.event_id=700+p; production.tick=10+12*p;
        // Same primary7 pays12 in U/H/J. Distinct secondary10 pays36 and its
        // represented clear requires the full current visible coalition.
        production.gems=fact<p6::Gems>(p?36:12); production.gold=fact<p6::Gems>(0);
        production.holder_count=3; production.holders={1,2,3,0,0,0}; production.source=p6::Source::synthetic_fixture;
    }
    for (int a=0;a<3;++a) {
        auto& actor=out.actors[a]; actor.event_count=0; actor.occupied_turns=fact<p6::Gems>(35+4*a);
        for (int p=0;p<2;++p) { event(actor,p6::EventKind::production,700+p,10+12*p,p); event(actor,p6::EventKind::tribute,1700+p,13+12*p+4*a); }
        event(actor,p6::EventKind::renewal,3000+a,35+4*a);
    }
    return out;
}
p6::CycleCertificate safety(const p6::Frame& frame,int actor,int group) {
    p6::CycleCertificate safe; safe.clock=frame.clock; safe.actor=actor; safe.group=group;
    const auto* seen=frame.visible(actor);
    if (seen) safe.clear=safe.hold=safe.bank=safe.recovery={seen->health,seen->max_health,10,0,0,0,true};
    safe.route_known=safe.bank_executable=safe.renewal_reachable=safe.solo=true; return safe;
}
ActionReceipt receipt(const Command& c) { return {c.clock,c.clock,c.token,c.action,true,true}; }
struct Fixture {
    Inputs in{}; EvidenceLedger ledger; ProbeBudget probes{}; Work work{}; LocalFrame local{}; CycleTrace trace{};
    ContextDeclaration own{},supported{}; int self=3,y=0,source=7;
    __attribute__((noinline)) explicit Fixture(int actor=3):ledger(actor),self(actor),y(actor==1?6:0),source(actor==1?3:actor==4?9:actor==5?13:7) {
        in.frame.clock={0,self,0}; in.frame.own.gold=15;
        in.draft.champions={p6::Champion::marcella,p6::Champion::varg,p6::Champion::prism,p6::Champion::willow,p6::Champion::tavi};
        in.draft.actors={1,2,3,actor>=4?4:-1,actor>=5?5:-1}; in.draft.picks=in.picks=std::max(3,actor);
        for (int id=1;id<=std::max(3,actor);++id) {
            p6::Actor actor; actor.id=id; actor.pick_ordinal=id-1; actor.type=in.draft.champions[id-1]; actor.team=p6::Team::ally;
            actor.health=actor.max_health=100; actor.strength=20; actor.alive=actor.can_attack=actor.can_move=true;
            actor.ranged=actor.type==p6::Champion::prism; actor.attack_range=actor.ranged?3:1;
            actor.position=id==self?p6::Point{0,y}:id==1?p6::Point{0,6}:id==2?p6::Point{2,1}:id==3?p6::Point{2,0}:p6::Point{2,id-2};
            if (id==self) in.frame.own.actor=actor; else in.frame.actors[in.frame.actor_count++]={actor,in.frame.clock,id,p6::Knowledge::exact};
        }
        in.settings={2000,1000,5}; in.settings_identity={17,p6::Source::queried_setting};
        in.own_state={{100,100,20,80,1,0,99},in.frame.clock,self,p6::Knowledge::exact}; in.threat_regime={0,in.frame.clock,self,p6::Knowledge::exact};
        p6::Group g; g.id=source; g.original_spawn_visible=g.all_spawns_visible=g.uncontested_now=true; g.lifecycle=p6::Lifecycle::living;
        in.frame.group={g,in.frame.clock,source,p6::Knowledge::exact}; in.target_group=7;
        in.independent=0; in.venture_alternative=1; in.control_alternative=2; in.offer_count=5;
        in.offers[0].plan=circuit(self,source,y); in.offers[1].plan=in.offers[0].plan; in.offers[2].plan=in.offers[0].plan;
        in.offers[3].plan=circuit(3,7,0,Organization::H); in.offers[4].plan=circuit(3,7,0,Organization::J);
        if (self==1) {
            in.offers[4].plan.ingress=path({{0,6},{0,5},{0,4},{0,3},{0,2},{1,2},{2,2}});
            in.offers[4].plan.capture=path({{2,2},{3,2}}); in.offers[4].plan.bank=path({{3,2},{4,1},{4,0}}); in.offers[4].plan.forecast_rounds=30;
        }
        own.key.settings_id=17; own.key.route_hash=plan_route_hash(in.offers[0].plan); own.key.return_route_hash=plan_return_hash(in.offers[0].plan);
        own.key.actor_id=self; own.key.champion=int(in.frame.own.actor.type); own.key.role=self-1;
        own.key.source_group=source; own.key.route_template=0; own.key.endpoint=4+y; own.key.threat_regime=0;
        own.key.participants[0]=self; own.key.participant_count=1; own.key.build={99,100,20,80,1};
        own.envelope={{20,100},{100,100},{20,20},{80,92},{1,1},{0,12},0};
        for (int h=0;h<3;++h) own.hypotheses[h].offset={2*h,2*h+2};
        if (self==3) {
            supported=own; supported.kind=EpisodeKind::full_organization; supported.require_supported_work=true;
            supported.key.role=3; supported.key.participants={2,3,-1,-1,-1,-1}; supported.key.participant_count=2;
            in.component_slot=ledger.predeclare(own); in.supported_slot=ledger.predeclare(supported);
            in.component_entry=in.offers[0].entry=own.key; in.supported_entry=in.offers[3].entry=supported.key;
            in.offers[0].context_slot=in.component_slot; in.offers[3].context_slot=in.supported_slot;
            check(in.component_slot>=0 && in.supported_slot>=0,"venture collector owns separate private component and H declarations");
        }
        schedules(); tick(0);
    }
    __attribute__((noinline)) void schedules() {
        in.offers[0].schedule=single(in.frame,self,self==3); in.offers[0].schedule.target_id=source;
        in.offers[1].schedule=aggregate(in.frame); in.offers[2].schedule=aggregate(in.frame,true);
        in.offers[1].schedule.target_id=in.offers[2].schedule.target_id=source;
        in.offers[3].schedule=aggregate(in.frame,false,true); in.offers[4].schedule=aggregate(in.frame,true,false,true);
    }
    __attribute__((noinline)) void observe_plan(const Plan& plan) {
        local.frame=in.frame; local.current_round=in.frame.clock.round; local.milestones={}; local.safety_count=plan.crew_count;
        for (int i=0;i<plan.crew_count;++i) local.safety[i]=safety(in.frame,plan.crew[i],plan.group);
        local.legal={{},in.frame.clock,self,p6::Knowledge::exact}; auto& legal=local.legal.value;
        legal.safe_now=legal.attack_legal=legal.altar_legal=legal.recall_safe=legal.renewal_productive=true;
        for (Leg leg:{Leg::ingress,Leg::capture,Leg::bank,Leg::renewal,Leg::release}) {
            const auto& route=route_for(plan,leg); auto& access=legal.routes[int(leg)]; access.assumptions_hold=route_valid(route); access.next_move_legal=true;
            for (int i=0;i<route.count;++i) if (route.tiles[i]==in.frame.own.actor.position) { access.next_tile=route.tiles[std::min(i+1,route.count-1)]; break; }
        }
    }
    __attribute__((noinline)) void tick(int round) {
        auto& f=in.frame; f.clock.round=round; f.group.clock=f.clock;
        for (int a=0;a<f.actor_count;++a) f.actors[a].clock=f.clock;
        in.own_state.clock=in.threat_regime.clock=f.clock; auto& state=in.own_state.value;
        state.health=f.own.actor.health; state.max_health=f.own.actor.max_health; state.strength=f.own.actor.strength; state.bag=f.own.actor.bag;
        in.windows.valid=true; in.windows.source=p6::Source::synthetic_fixture; in.windows.windows=in.windows.deadlines={39,39,39}; in.windows.sampled_actor_id=self; in.windows.sampled_round=round;
        for (int o=0;o<in.offer_count;++o) {
            auto& offer=in.offers[o]; offer.safety_count=offer.schedule.actor_count;
            for (int a=0;a<offer.schedule.actor_count;++a) {
                const int id=offer.schedule.actors[a].actor_id;
                offer.safety[a]=safety(f,id,participant(offer.plan,id)?offer.plan.group:id==1?3:id==2?8:7);
            }
        }
        in.loaded_return={{},f.clock,self,p6::Knowledge::exact}; auto& loaded=in.loaded_return.value;
        loaded.context=supported.key; loaded.focus_per_gem={1,p6::Source::queried_setting};
        for (int i=0;i<3;++i) loaded.points[i]={i==1?12:0,i==1?92.:80.,{f.own.actor.health,f.own.actor.max_health,double(i*10),0,0,0,true}};
        observe_plan(work.active?work.plan:in.offers[0].plan);
    }
    __attribute__((noinline)) void reset(int round) {
        in.frame.own.actor.position={0,y}; in.frame.own.actor.bag=0; in.frame.own.actor.spawn_group=-1; in.frame.own.actor.channel=p6::Channel::none;
        in.own_state.value.focus=80; in.frame.group.value.lifecycle=p6::Lifecycle::living;
        for (int a=0;a<in.frame.actor_count;++a) {
            auto& actor=in.frame.actors[a].value; actor.action=p6::WitnessAction::none; actor.spawn_group=-1;
            actor.position=actor.id==1?p6::Point{0,6}:actor.id==2?p6::Point{2,1}:actor.id==3?p6::Point{2,0}:p6::Point{2,actor.id-2};
        }
        work={}; schedules(); tick(round);
    }
    __attribute__((noinline)) void cycle(int start,bool h=false,bool break_focus=false) {
        reset(start);
        // Ordinary first income remains executable when specialist loaded
        // capability is unknown. Verified H needs no preliminary U history.
        if (!h && self==3) in.loaded_return.knowledge=p6::Knowledge::unknown;
        auto decision=decide(in,ledger,probes,Arm::active);
        check(decision.option==(h?3:0),"actor selects productive own U or currently verified H commissioning");
        observe_plan(in.offers[decision.option].plan);
        const auto admitted=commit(in,local,decision,ledger,probes,Arm::active,work);
        check(admitted.started,"selected current physical route starts");
        const bool tracked=admitted.episode!=0;
        if (tracked) check(trace.begin(ledger,work),"own ledger issue binds a new physical trace before work");
        auto step=[&](int offset,p6::Point position,bool payout=false,bool bank=false) {
            in.frame.own.actor.position=position; in.frame.own.actor.spawn_group=position==p6::Point{3,y}?source:-1;
            if (payout) { in.frame.own.actor.bag=self==3?12:10; in.own_state.value.focus=self==3?(break_focus?93:92):80; }
            if (bank) { in.frame.own.actor.bag=0; in.frame.own.actor.channel=p6::Channel::none; in.own_state.value.focus=80; }
            tick(start+offset);
            if (payout || bank) local.milestones={{source,payout,bank,payout?(self==3?12:10):0,bank?(self==3?12:10):0},in.frame.clock,self,p6::Knowledge::exact};
            auto command=advance(in,local,ledger,Arm::active,work);
            if (command.action==PhysicalAction::move || command.action==PhysicalAction::attack || command.action==PhysicalAction::tribute)
                check(work.confirm(command,receipt(command)),"actual own move attack or tribute consumes a same-clock receipt");
            if (tracked) trace.observe(local,work,{}); return command;
        };
        step(0,{0,y}); step(1,{1,y});
        if (h) for (int a=0;a<in.frame.actor_count;++a) if (in.frame.actors[a].value.id==2) { in.frame.actors[a].value.action=p6::WitnessAction::useful_attack; in.frame.actors[a].value.action_group=7; }
        check(step(2,{2,y}).action==PhysicalAction::attack,"independent or supported cycle performs actual production"); step(3,{2,y});
        const int shift=h?-1:0; if (!h) step(4,{2,y});
        in.frame.group.value.lifecycle=p6::Lifecycle::reward_available; step(5+shift,{2,y});
        for (int round=6+shift;round<=11+shift;++round) {
            if (h) for (int a=0;a<in.frame.actor_count;++a) if (in.frame.actors[a].value.id==2) {
                auto& helper=in.frame.actors[a].value; helper.action=p6::WitnessAction::none; helper.spawn_group=round<8+shift?7:-1;
                helper.position=round<8+shift?p6::Point{3,1}:p6::Point{4,1};
            }
            check(step(round,{3,y}).action==PhysicalAction::hold,"full capture window remains an observed obligation");
        }
        step(12+shift,{3,y},true); check(work.payout_seen,"actual own payout retains source ownership");
        check(step(13+shift,{4,y}).action==PhysicalAction::tribute,"actual existing bag enters its own safe whole-bag channel");
        in.frame.own.actor.channel=p6::Channel::tribute; in.frame.own.actor.channel_valid=true;
        check(step(14+shift,{4,y}).action==PhysicalAction::preserve_channel,"valid channel persists through stop or Focus change");
        step(15+shift,{4,y},false,true);
        const std::array<p6::Point,5> returns{{{4,y+1},{3,y+1},{2,y+1},{1,y+1},{0,y}}};
        for (int i=0;i<5;++i) step(16+shift+i,returns[i]);
        check(work.bank_seen && work.renewal_seen && !work.active,"actual bank ends at named productive renewal");
        if (tracked) {
            const auto result=trace.finish(ledger,in.frame.clock.round,{h?supported.key:own.key,in.own_state.value,all_premises});
            check(result.full_positive==!break_focus,"unexpected own loaded Focus cannot qualify a venture cycle");
        }
    }
};
__attribute__((noinline)) void private_gate_and_membership() {
    auto fixture=std::make_unique<Fixture>();
    check(decide(fixture->in,fixture->ledger,fixture->probes,Arm::active).organization==Organization::H &&
          !component(fixture->in,fixture->ledger) && !organization(fixture->in,fixture->ledger),
          "current safe pair may commission H immediately without U history or initial H status");
    fixture->cycle(0); check(!component(fixture->in,fixture->ledger),"one own cycle cannot qualify a return component");
    fixture->cycle(40); check(component(fixture->in,fixture->ledger),"two actual own matched cycles qualify only own component");
    fixture->reset(80);
    const auto active=decide(fixture->in,fixture->ledger,fixture->probes,Arm::active);
    const auto control=decide(fixture->in,fixture->ledger,fixture->probes,Arm::control);
    check(active.organization==Organization::H && control.organization==Organization::J,"same legal input changes insured H pair versus recruiting full J crew");
    check(!participant(fixture->in.offers[active.option].plan,1) && participant(fixture->in.offers[control.option].plan,1),"membership ring-fence has actual actor responsibility rather than a score allowance");
    check(!organization(fixture->in,fixture->ledger),"component proof never becomes full new H membership proof");
    fixture->cycle(80,true); check(!organization(fixture->in,fixture->ledger),"first actual supported cycle remains unqualified");
    fixture->cycle(120,true); check(organization(fixture->in,fixture->ledger),"two actual H cycles qualify their own membership and loaded return");
    check(fixture->probes.used==0,"ordinary complete cycles do not spend or replenish diagnostic slots");
    auto initial=std::make_unique<Fixture>();
    initial->in.component_slot=-1; initial->in.component_entry={};
    initial->cycle(0,true);
    check(!organization(initial->in,initial->ledger),"first initial H completion cannot manufacture the second full completion");
    initial->cycle(40,true);
    check(organization(initial->in,initial->ledger) && initial->ledger.state(0)->positive_completions==0,
          "two actual initial H cycles qualify only H with no component input or borrowed U completion");
}
__attribute__((noinline)) void insurer_survives_venture_failure() {
    auto insurer=std::make_unique<Fixture>(1), venture=std::make_unique<Fixture>();
    venture->reset(80);
    auto d=decide(venture->in,venture->ledger,venture->probes,Arm::active); venture->observe_plan(venture->in.offers[d.option].plan);
    check(commit(venture->in,venture->local,d,venture->ledger,venture->probes,Arm::active,venture->work).started,"venture begins before its local observed failure");
    venture->tick(81); venture->local.legal.value.safe_now=false; advance(venture->in,venture->local,venture->ledger,Arm::active,venture->work);
    check(!venture->work.active && venture->ledger.suspended(venture->in.supported_slot,1),"actual venture safety failure stops only its own proposal");
    const auto failures=venture->ledger.lifetime_failure_count(); insurer->cycle(80);
    check(insurer->work.payout_lower==10 && insurer->work.bank_lower==10 && insurer->work.renewal_seen,"insured actor actually earns banks and renews through the venture's failure window");
    check(venture->ledger.lifetime_failure_count()==failures && !insurer->ledger.active(),"insurer work cannot repair or mutate another private venture ledger");
}
__attribute__((noinline)) void unknown_focus_and_insurer_inputs() {
    auto fixture=std::make_unique<Fixture>(); fixture->reset(80);
    fixture->in.loaded_return.knowledge=p6::Knowledge::unknown;
    check(decide(fixture->in,fixture->ledger,fixture->probes,Arm::active).organization==Organization::U,"unknown own loaded return never blocks first-income U or licenses Prism H");
    fixture->tick(80); fixture->in.loaded_return.value.points[2].focus=92;
    check(decide(fixture->in,fixture->ledger,fixture->probes,Arm::active).organization==Organization::U,"loaded Focus cannot survive whole-bag tribute as fictitious permanent capital");
    fixture->tick(80); fixture->in.loaded_return.value.points[1].cumulative.incoming_total=99;
    check(decide(fixture->in,fixture->ledger,fixture->probes,Arm::active).organization==Organization::U,"own loaded return must survive the full conservative exposure");
    fixture->tick(80); fixture->in.loaded_return.value.points[1].bag=1; fixture->in.loaded_return.value.points[1].focus=81;
    check(decide(fixture->in,fixture->ledger,fixture->probes,Arm::active).organization==Organization::U,"a one-gem Focus sample cannot license the frozen twelve-gem return envelope");
    fixture->tick(80);
    for (int a=0;a<fixture->in.frame.actor_count;++a) if (fixture->in.frame.actors[a].value.id==1) fixture->in.frame.actors[a].knowledge=p6::Knowledge::unknown;
    fixture->tick(80);
    check(decide(fixture->in,fixture->ledger,fixture->probes,Arm::active).organization==Organization::H,"active pair does not need current insurer visibility work or private resources");
    auto& h=fixture->in.offers[3]; h.safety[h.safety_count++]={}; h.safety[h.safety_count-1].actor=1;
    check(decide(fixture->in,fixture->ledger,fixture->probes,Arm::active).organization==Organization::U,"insurer certificate cannot silently become a venture input");
    fixture->tick(80); event(h.schedule.actors[0],p6::EventKind::purchase,999,0); h.schedule.actors[0].events[h.schedule.actors[0].event_count-1].gold_cost=fact<p6::Gems>(1000);
    check(decide(fixture->in,fixture->ledger,fixture->probes,Arm::active).organization==Organization::U,"future or another actor's gold cannot fund unsupported venture purchases");
}
__attribute__((noinline)) void focus_change_banks_before_retreat() {
    auto fixture=std::make_unique<Fixture>();
    fixture->cycle(80,true,true);
    check(!organization(fixture->in,fixture->ledger) && fixture->ledger.suspended(fixture->in.supported_slot,1),"own bag/Focus regime change stops future H after actual bag conversion");
    fixture->reset(120); check(decide(fixture->in,fixture->ledger,fixture->probes,Arm::active).organization==Organization::G,"failed unqualified venture returns to productive G without recruiting insurer");
}
__attribute__((noinline)) void existing_bag_and_profile() {
    auto fixture=std::make_unique<Fixture>(); auto& in=fixture->in;
    in.frame.own.actor.position={3,0}; in.frame.own.actor.bag=6; in.own_state.value.focus=86; fixture->tick(0);
    auto d=decide(in,fixture->ledger,fixture->probes,Arm::active);
    check(d.option<0 && !d.secure_inventory,"existing bag forbids new productive or supported admission without an own bank route");
    auto& bank=in.existing_inventory.value; bank.plan=circuit(3); bank.plan.ingress=bank.plan.capture=path({{3,0}});
    bank.schedule={}; bank.schedule.target_id=7; bank.schedule.actor_count=1; auto& actor=bank.schedule.actors[0]; actor.actor_id=3;
    actor.initial_bag={6,p6::Source::self_observed}; actor.initial_gold={15,p6::Source::self_observed}; actor.occupied_turns=fact<p6::Gems>(10);
    event(actor,p6::EventKind::tribute,800,3); event(actor,p6::EventKind::renewal,801,10);
    bank.safety_count=1; bank.safety[0]=safety(in.frame,3,7); in.existing_inventory.clock=in.frame.clock; in.existing_inventory.subject=3; in.existing_inventory.knowledge=p6::Knowledge::exact;
    d=decide(in,fixture->ledger,fixture->probes,Arm::active); fixture->observe_plan(bank.plan);
    const auto admitted=commit(in,fixture->local,d,fixture->ledger,fixture->probes,Arm::active,fixture->work);
    check(d.secure_inventory && admitted.started && !admitted.episode && fixture->work.stage==PhysicalStage::bank,"actual existing inventory uses a physical bank-only job without fabricated production proof");
    auto command=advance(in,fixture->local,fixture->ledger,Arm::active,fixture->work);
    check(command.action==PhysicalAction::move && command.target==p6::Point{4,0},"existing bag physically leaves for its actual altar before any new venture");
    check(fixture->work.confirm(command,receipt(command)),"bank-only movement has a real own action receipt");
    in.frame.own.actor.position={4,0}; fixture->tick(1);
    command=advance(in,fixture->local,fixture->ledger,Arm::active,fixture->work);
    check(command.action==PhysicalAction::tribute && fixture->work.confirm(command,receipt(command)),"preexisting whole bag actually begins tribute");
    in.frame.own.actor.bag=0; in.own_state.value.focus=80; fixture->tick(2);
    fixture->local.milestones={{7,false,true,0,6},in.frame.clock,3,p6::Knowledge::exact};
    command=advance(in,fixture->local,fixture->ledger,Arm::active,fixture->work);
    check(fixture->work.bank_seen && fixture->work.confirm(command,receipt(command)),"actual preexisting bag conversion precedes renewed movement");
    const std::array<p6::Point,5> returns{{{4,1},{3,1},{2,1},{1,1},{0,0}}};
    for (int i=0;i<5;++i) {
        in.frame.own.actor.position=returns[i]; fixture->tick(3+i); command=advance(in,fixture->local,fixture->ledger,Arm::active,fixture->work);
        if (command.action==PhysicalAction::move) check(fixture->work.confirm(command,receipt(command)),"bank-only productive return follows actual retained tiles");
    }
    check(fixture->work.renewal_seen && !fixture->work.payout_seen && !fixture->work.cycle_completed &&
          !fixture->ledger.active() && fixture->ledger.state(in.component_slot)->positive_completions==0,
          "bank-only renewal cannot fabricate a new production episode or qualify the component");
    check(champion_pool.size()==8 && insurer_actor(p6::Actor{.id=1,.pick_ordinal=0,.type=p6::Champion::marcella,.team=p6::Team::ally},in.draft),"draft roles use exact selected identity across the broader supported pool");
}
__attribute__((noinline)) void extension_duties() {
    auto helper=std::make_unique<Fixture>(4); auto& in=helper->in;
    append(in.offers[1].schedule,single(in.frame,4));
    append(in.offers[3].schedule,single(in.frame,4));
    auto& support=in.offers[3]; auto& plan=support.plan;
    plan.crew={3,2,4,-1,-1,-1}; plan.crew_count=3; plan.forecast_rounds=26;
    plan.ingress=path({{0,0},{0,1},{1,2},{2,2}}); plan.capture=path({{2,2}});
    plan.bank=path({{2,2},{3,2},{4,1},{4,0}}); plan.release=path({{2,2},{3,2}});
    auto& displaced=support.schedule.actors[2]; displaced.occupied_turns=fact<p6::Gems>(48);
    for (int e=0;e<displaced.event_count;++e) displaced.events[e].tick+=30;
    support.schedule.productions[support.schedule.production_count-1].tick+=30;
    helper->tick(0); in.windows.windows=in.windows.deadlines={14,14,14};
    in.visible_proposer={7,in.frame.clock,3,p6::Knowledge::exact};
    auto decision=decide(in,helper->ledger,helper->probes,Arm::active);
    check(decision.organization==Organization::H && !decision.record_episode,
          "fourth pick visibly assists the venture using current capability without a partner's private proof");
    helper->observe_plan(plan);
    check(commit(in,helper->local,decision,helper->ledger,helper->probes,Arm::active,helper->work).started &&
          advance(in,helper->local,helper->ledger,Arm::active,helper->work).action==PhysicalAction::move,
          "fourth pick starts its own retained physical ingress to the visible venture");
    in.visible_proposer.knowledge=p6::Knowledge::unknown;
    check(decide(in,helper->ledger,helper->probes,Arm::active).organization==Organization::U,
          "unknown proposal cannot recruit a helper by borrowing private evidence");
    in.visible_proposer.knowledge=p6::Knowledge::exact;
    for (int a=0;a<in.frame.actor_count;++a) if (in.frame.actors[a].value.id==2) in.frame.actors[a].knowledge=p6::Knowledge::unknown;
    check(decide(in,helper->ledger,helper->probes,Arm::active).organization==Organization::U,
          "missing actual venture member cancels dependent helper admission while own U remains available");
    auto fifth=std::make_unique<Fixture>(5);
    fifth->cycle(0);
    check(fifth->work.plan.organization==Organization::U && fifth->work.bank_seen && fifth->work.renewal_seen,
          "fifth pick physically produces converts and renews independently without specialist qualification");
}
}
int main() {
    private_gate_and_membership(); insurer_survives_venture_failure(); unknown_focus_and_insurer_inputs();
    focus_change_banks_before_retreat(); existing_bag_and_profile(); extension_duties();
    std::printf("Underwritten Frontier: %d checks passed\n",checks); return 0;
}
