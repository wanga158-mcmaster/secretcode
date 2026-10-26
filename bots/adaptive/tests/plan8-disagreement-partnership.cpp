#include "../disagreement-partnership/src/policy.h"
#include "../disagreement-partnership/src/draft.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>
using namespace p8;
using namespace p8::disagreement_partnership;
namespace {
int checks=0;
void check(bool ok,const char* text) { ++checks; if (!ok) { std::fprintf(stderr,"FAIL: %s\n",text); std::exit(1); } }
template<class T> p6::Fact<T> fact(T x) { return {x,p6::Source::synthetic_fixture}; }
Route path(std::initializer_list<p6::Point> points) { Route r; r.verified=true; r.count=int(points.size()); int i=0; for (auto p:points) r.tiles[i++]=p; return r; }
Plan route(int owner=3,bool h=false) {
    Plan p; p.organization=h?Organization::H:Organization::U; p.collector=owner; p.group=owner==3?8:owner==1?7:6; p.altar=owner==3?5:4;
    p.crew[0]=owner; p.crew_count=1; const int y=owner==1?0:owner==2?2:4;
    p.ingress=owner==3?path({{0,4},{1,3},{2,2}}):path({{0,y},{1,y},{2,y}});
    p.capture=owner==3?path({{2,2},{3,2}}):path({{2,y},{3,y}});
    p.bank=owner==3?path({{3,2},{4,3},{4,4}}):path({{3,y},{4,y}});
    p.renewal=owner==3?path({{4,4},{4,5},{3,5},{2,5},{1,5},{0,4}}):path({{4,y},{4,y+1},{3,y+1},{2,y+1},{1,y+1},{0,y}});
    if (h) { p.crew={1,3,-1,-1,-1,-1}; p.crew_count=2; p.pads[0]={2,3}; p.pads[1]={2,2}; }
    p.forecast_rounds=18; p.remaining_rounds=100; p.capture_window=5; p.gem_bearing=p.complete_verified=p.rally_legal=true; return p;
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
// Conditional synthetic production/geometry assumptions, not native outcomes:
// investigator sources8/18 each have60HP and12gems; producer7/17 and converter
// 6/16 each yield10. Actual first-source U needs3 own20-damage attacks; H needs
// 2 own plus1 actual visible helper attack. Under the common route model H
// banks its second12 at39 and U at40. Under the declared +2 bank-route model,
// each collector tribute gains2 turns and its second job starts2 turns later.
// H's displaced helper still earns both10-gem jobs, later at18/45, charged52
// occupied turns. The converter stays independent. No payload differs by model.
void schedule(p6::Schedule& s,const p6::Frame& f,Organization organization,int target,int delay=0) {
    s={}; s.actor_count=3; s.target_id=target; const bool h=organization==Organization::H;
    for (int id=1;id<=3;++id) {
        auto& a=s.actors[id-1]; a.actor_id=id; const auto* seen=f.visible(id); const bool own=id==f.clock.self;
        a.initial_bag={seen?seen->bag:0,own?p6::Source::self_observed:p6::Source::visible_subject};
        a.initial_gold={own?f.own.gold:0,own?p6::Source::self_observed:p6::Source::conservative};
    }
    income(s,1,7,10,h?14:12,h?18:14); income(s,1,17,10,h?42:32,h?45:35);
    income(s,2,6,10,12,14); income(s,2,16,10,33,36);
    income(s,3,8,12,h?11:12,(h?15:16)+delay); income(s,3,18,12,(h?36:37)+delay,(h?39:40)+2*delay);
    s.actors[0].occupied_turns=fact<p6::Gems>(h?52:40); s.actors[1].occupied_turns=fact<p6::Gems>(41);
    s.actors[2].occupied_turns=fact<p6::Gems>((h?44:45)+2*delay);
    for (int i=0;i<3;++i) event(s.actors[i],p6::EventKind::renewal,3000+i,int(s.actors[i].occupied_turns.value));
}
p6::CycleCertificate certificate(const p6::Frame& f,int actor,int group) {
    p6::CycleCertificate c; c.clock=f.clock; c.actor=actor; c.group=group; const auto* a=f.visible(actor);
    if (a) c.clear=c.hold=c.bank=c.recovery={a->health,a->max_health,10,0,0,0,true};
    c.route_known=c.bank_executable=c.renewal_reachable=c.solo=true; return c;
}
struct Fixture {
    Inputs in{}; int owner=3; EvidenceLedger ledger; Partnership partnership{}; ProbeBudget probes{};
    LocalFrame local{}; Work work{}; CycleTrace trace{}; Spec spec{}; Plan selected{};
    Arm arm=Arm::active; int started=-1,stress_window=39; bool paid_counter_primitive=false;
    explicit Fixture(int actor=3,bool overlap=false,bool paid=false):owner(actor),ledger(actor) {
        auto& f=in.frame; f.clock={0,owner,0}; f.own.gold=60;
        in.draft.champions={p6::Champion::vesper_vale,p6::Champion::willow,p6::Champion::ren,p6::Champion::tavi,p6::Champion::other};
        in.draft.actors={1,2,3,4,-1}; in.draft.picks=in.picks=4;
        for (int id=1;id<=4;++id) {
            p6::Actor a; a.id=id; a.pick_ordinal=id-1; a.type=in.draft.champions[id-1]; a.team=p6::Team::ally;
            a.health=a.max_health=100; a.strength=20; a.alive=a.can_move=a.can_attack=true; a.ready_attack_group=8;
            a.position=id==1?p6::Point{2,3}:id==2?p6::Point{0,2}:id==3?p6::Point{0,4}:p6::Point{4,3};
            if (id==owner) f.own.actor=a; else f.actors[f.actor_count++]={a,f.clock,id,p6::Knowledge::exact};
        }
        f.own.actor.position=route(owner).ingress.tiles[0]; f.own.at_spawn=true;
        p6::Group g; g.id=8; g.lifecycle=p6::Lifecycle::living; g.original_spawn_visible=g.all_spawns_visible=g.uncontested_now=true;
        f.group={g,f.clock,8,p6::Knowledge::exact}; in.settings={2000,1000,5}; in.settings_identity={17,p6::Source::queried_setting};
        in.own_state={{100,100,20,10,1,0,99},f.clock,owner,p6::Knowledge::exact}; in.threat_regime={0,f.clock,owner,p6::Knowledge::exact};
        in.target_group=8; in.independent=0; in.offer_count=2; in.offers[0].plan=route(owner); in.offers[1].plan=route(3,true);
        spec.assay=route(3); spec.supported=route(3,true); spec.nominal_stage=2;
        auto& c=spec.route; c.key.settings_id=17; c.key.actor_id=3; c.key.champion=int(p6::Champion::ren); c.key.role=0;
        c.key.source_group=8; c.key.route_template=0; c.key.endpoint=5; c.key.threat_regime=0;
        c.key.participants[0]=3; c.key.participant_count=1; c.key.build={99,100,20,10,1};
        c.key.route_hash=plan_route_hash(spec.assay); c.key.return_route_hash=plan_return_hash(spec.assay);
        c.envelope={{75,100},{100,100},{20,20},{10,10},{1,1},{0,100},0};
        if (overlap) { spec.models[0].offset={0,2}; spec.models[1].offset={2,4}; }
        spec.paid_bank_turn_bound=paid?spec.nominal_stage+spec.models[1].offset.upper+1:0;
        c.hypothesis_count=2; c.hypotheses[0]=spec.models[0]; c.hypotheses[1]=spec.models[1];
        spec.organization=c; spec.organization.kind=EpisodeKind::full_organization; spec.organization.require_supported_work=true;
        spec.organization.key.role=1; spec.organization.key.participants={1,3,-1,-1,-1,-1}; spec.organization.key.participant_count=2;
        spec.organization.key.route_hash=plan_route_hash(spec.supported); spec.organization.hypothesis_count=3;
        for (int i=0;i<3;++i) spec.organization.hypotheses[i].offset={2*i,2*i+2};
        if (owner==3) check(partnership.declare(spec,in.draft,ledger),"two explicit route models and one exact outer H context freeze before actual work");
        in.offers[1].context_slot=partnership.slot(); in.offers[1].entry=spec.organization.key;
        tick(0);
    }
    p6::Actor& actor(int id) { if (owner==id) return in.frame.own.actor; for (int i=0;i<in.frame.actor_count;++i) if (in.frame.actors[i].subject==id) return in.frame.actors[i].value; std::abort(); }
    void source(int group,p6::Lifecycle lifecycle=p6::Lifecycle::living) { in.frame.group.subject=group; in.frame.group.value.id=group; in.frame.group.value.lifecycle=lifecycle; }
    void tick(int round) {
        auto& f=in.frame; f.clock={round,owner,0}; f.group.clock=f.clock;
        for (int i=0;i<f.actor_count;++i) f.actors[i].clock=f.clock;
        in.own_state.clock=in.threat_regime.clock=f.clock; in.own_state.value.health=f.own.actor.health;
        in.own_state.value.strength=f.own.actor.strength; in.own_state.value.bag=f.own.actor.bag;
        in.windows.valid=true; in.windows.source=p6::Source::synthetic_fixture; in.windows.deadlines={2000-round,78,stress_window};
        in.windows.windows={120,78,stress_window}; in.windows.sampled_round=round; in.windows.sampled_actor_id=owner;
        in.visible_proposer={8,f.clock,3,p6::Knowledge::exact};
        for (int m=0;m<2;++m) {
            schedule(in.models[m].independent,f,Organization::U,8,2*m); schedule(in.models[m].supported,f,Organization::H,8,2*m);
        }
        for (int i=0;i<in.offer_count;++i) {
            auto& o=in.offers[i]; schedule(o.schedule,f,o.plan.organization,o.plan.group); o.safety_count=3;
            for (int id=1;id<=3;++id) o.safety[id-1]=certificate(f,id,common::participant(o.plan,id)?o.plan.group:route(id).group);
        }
        local.frame=f; local.current_round=round; local.safety_count=3; local.milestones={};
        const auto& p=work.active?work.plan:in.offers[0].plan;
        for (int id=1;id<=3;++id) local.safety[id-1]=certificate(f,id,p.group);
        local.legal={{},f.clock,owner,p6::Knowledge::exact}; auto& l=local.legal.value;
        l.safe_now=l.attack_legal=l.altar_legal=l.recall_safe=l.renewal_productive=true;
        for (Leg leg:{Leg::ingress,Leg::capture,Leg::bank,Leg::renewal,Leg::release}) {
            auto& access=l.routes[int(leg)]; const auto& r=route_for(p,leg); access.assumptions_hold=route_valid(r); access.next_move_legal=true;
            for (int j=0;j<r.count;++j) if (r.tiles[j]==f.own.actor.position) { access.next_tile=r.tiles[std::min(j+1,r.count-1)]; break; }
        }
    }
    void ready(int round,int option,Arm chosen=Arm::active) {
        work={}; arm=chosen; started=round; selected=in.offers[option].plan;
        auto& f=in.frame; f.own.actor.position=selected.ingress.tiles[0]; f.own.actor.bag=0; f.own.actor.spawn_group=-1;
        f.own.actor.channel=p6::Channel::none; f.own.at_spawn=true; source(selected.group);
        for (int id=1;id<=3;++id) if (id!=owner) { auto& a=actor(id); a.action=p6::WitnessAction::none; a.spawn_group=-1;
            if (common::participant(selected,id)) a.position=selected.pads[crew_index(selected,id)]; }
        tick(round); for (int id=1;id<=3;++id) local.safety[id-1]=certificate(f,id,selected.group);
    }
    void begin(int round,int option,Arm chosen=Arm::active) {
        ready(round,option,chosen);
        if (paid_counter_primitive) {
            // Isolated actual-action accounting primitive: this does not claim
            // that the economic paid-probe selector admitted this fixture.
            check(probes.spend(round/common::project_rounds),"isolated marked-leg accounting spends one persistent lifetime slot before action");
            check(partnership.assay.issue(spec,in,true)&&work.start(selected,local)&&partnership.assay.attach(work),
                "predeclared whole bank leg binds a fresh actual Work before any movement"); return;
        }
        const auto d=decide(in,partnership,ledger,probes,arm);
        if (d.option!=option) std::fprintf(stderr,"choice t=%d owner=%d arm=%d want=%d got=%d rejection=%d/%d resolved=%d consistent=%d/%d\n",round,owner,int(arm),option,d.option,int(d.rejected[0]),int(d.rejected[1]),partnership.assay.resolved,partnership.assay.consistent[0],partnership.assay.consistent[1]);
        check(d.option==option,"same complete comparator selects only current model-admissible productive organization");
        const auto admitted=commit(in,local,d,partnership,ledger,probes,arm,work,trace);
        check(admitted.started&&bool(admitted.episode)==d.record_episode,"actual owned Work starts after bounded stage or full episode authority is committed");
        if (owner==3&&round==0&&matching_models(in,partnership)&&disagreement(in))
            check(admitted.stage_issued&&partnership.assay.attempted&&!d.deliberate_probe&&probes.used==0,"ordinary best productive stage is recorded once without inventing a diagnostic charge");
    }
    Command step(int offset,p6::Point position,bool payout=false,bool bank=false,bool queue=false,bool unknown=false) {
        auto& f=in.frame; f.own.actor.position=position; f.own.at_spawn=position==selected.ingress.tiles[0];
        f.own.actor.spawn_group=position==selected.capture.tiles[selected.capture.count-1]?selected.group:-1;
        if (selected.organization==Organization::H) { auto& helper=actor(1); helper.action=offset==2?p6::WitnessAction::useful_attack:p6::WitnessAction::none;
            helper.action_group=8; helper.spawn_group=-1; if (offset>=5) helper.position={3,3}; }
        const int amount=owner==3?12:10;
        if (payout) { f.own.actor.bag=amount; source(selected.group,p6::Lifecycle::respawning); }
        if (bank) { f.own.actor.bag=0; f.own.actor.channel=p6::Channel::none; }
        tick(started+offset);
        if (payout||bank) local.milestones={{selected.group,payout,bank,payout?amount:0,bank?amount:0},f.clock,owner,p6::Knowledge::exact};
        if (queue) { actor(4).position=selected.bank.tiles[1]; local.frame=f; auto& access=local.legal.value.routes[int(Leg::bank)]; access.next_move_legal=false; access.friendly_queue=true; }
        else if (owner==3) { actor(4).position={4,2}; local.frame=f; }
        if (unknown) local.fuel_resumed=true;
        const auto c=advance(in,local,partnership,ledger,arm,work);
        check(work.confirm(c,{c.clock,c.clock,c.token,c.action,true,true}),"actual legal command gets one fresh own action receipt");
        observe(in,local,c,partnership,ledger,probes,work,trace); return c;
    }
    int cycle(int round,int option,int wait=0,Arm chosen=Arm::active,bool lose=false) {
        begin(round,option,chosen); const auto p=selected; const int attacks=p.organization==Organization::H?2:3;
        check(step(0,p.ingress.tiles[0]).action==PhysicalAction::move,"actual retained ingress starts first income"); step(1,p.ingress.tiles[1]);
        for (int a=0;a<attacks;++a) check(step(2+a,p.ingress.tiles[2]).action==PhysicalAction::attack,"productive own source clear uses actual actions on fixed60HP payload");
        int t=2+attacks; source(p.group,p6::Lifecycle::reward_available); step(t++,p.capture.tiles[0]);
        for (int i=0;i<6;++i) check(step(t++,p.capture.tiles[1]).action==PhysicalAction::hold,"actual holder waits through capture without team-score inference");
        check(step(t++,p.capture.tiles[1],true,false,wait>0).action==(wait?PhysicalAction::hold:PhysicalAction::move),"own payout begins the named measured bank route and retains the actual bag");
        for (int q=1;q<wait;++q) check(step(t++,p.bank.tiles[0],false,false,true).action==PhysicalAction::hold,"personally visible friendly queue explains actual productive route waiting");
        if (wait) step(t++,p.bank.tiles[0]);
        for (int i=1;i<p.bank.count;++i) {
            const auto c=step(t++,p.bank.tiles[i],false,false,false,lose&&i==1);
            if (lose&&i==1) check(partnership.assay.unavailable&&!partnership.assay.resolved,"fuel/observation gap leaves the stage unknown rather than zero or a matching model");
            if (i==p.bank.count-1) check(c.action==PhysicalAction::tribute,"actual arrival at declared endpoint starts a real own tribute");
        }
        if (owner==3) { fchannel(); check(step(t++,p.bank.tiles[p.bank.count-1]).action==PhysicalAction::preserve_channel,"safe own tribute remains protected while model result is pending"); }
        step(t++,p.bank.tiles[p.bank.count-1],false,true);
        for (int i=1;i<p.renewal.count;++i) step(t++,p.renewal.tiles[i]);
        check(!work.active&&work.bank_seen&&work.renewal_seen&&(lose?!work.cycle_completed:work.cycle_completed),
            "own physical payout still banks and renews after unavailable evidence without a fabricated full cycle"); return in.frame.clock.round;
    }
    void fchannel() { in.frame.own.actor.channel=p6::Channel::tribute; in.frame.own.actor.channel_valid=true; }
};
__attribute__((noinline)) void productive_disagreement_then_supported_work() {
    auto f=std::make_unique<Fixture>(); auto producer=std::make_unique<Fixture>(1); auto converter=std::make_unique<Fixture>(2);
    check(matching_models(f->in,f->partnership)&&disagreement(f->in)&&preferred(f->in,0)==int(Organization::H)&&preferred(f->in,1)==int(Organization::U),"two explicit same-payload route models imply different complete physical organizations");
    check(common::evaluate(f->in,f->in.models[0].independent).min_margin==52&&common::evaluate(f->in,f->in.models[0].supported).min_margin==54&&
        common::evaluate(f->in,f->in.models[1].supported).min_margin==42,"charged displaced helper work and queued late banks produce consequential ex ante disagreement");
    const auto active=decide(f->in,f->partnership,f->ledger,f->probes,Arm::active),control=decide(f->in,f->partnership,f->ledger,f->probes,Arm::control);
    check(active.option==0&&control.option==1,"identical legal input keeps active independent while nominal control commits current feasible H immediately");
    producer->in.offer_count=1; converter->in.offer_count=1; producer->cycle(0,0); converter->cycle(0,0);
    check(producer->work.bank_seen&&converter->work.bank_seen&&producer->work.renewal_seen&&converter->work.renewal_seen,"both initial independent roles actually earn convert and renew during investigator's bounded stage");
    f->cycle(0,0); const auto& assay=f->partnership.assay;
    check(assay.resolved&&assay.observed.lower==2&&assay.consistent[0]&&!assay.consistent[1]&&assay.completed_round==21,"continuous exact own payout-to-bank stage discriminates only after actual own bank and return");
    check(f->ledger.state(f->partnership.slot())->positive_completions==0,"one route endpoint grants no borrowed full organization proof");
    f->cycle(64,1); check(f->ledger.state(f->partnership.slot())->positive_completions==1&&!f->ledger.organization_qualified(f->partnership.slot(),f->spec.organization.key,f->in.frame.clock.round),"first changed-membership physical supported cycle remains unqualified");
    f->cycle(108,1); check(f->ledger.organization_qualified(f->partnership.slot(),f->spec.organization.key,f->in.frame.clock.round),"two fresh supported attacks actual helper withdrawal own12 bank and return qualify only actual H");
    f->ready(400,0); check(decide(f->in,f->partnership,f->ledger,f->probes,Arm::active).option==0&&!f->partnership.assay.active&&f->partnership.assay.attempted,"elapsed epochs and expired authority never issue another assay or transfer old proof");
}
__attribute__((noinline)) void slow_overlap_and_unknown_retain_independence() {
    auto slow=std::make_unique<Fixture>(); slow->cycle(0,0,2);
    check(slow->partnership.assay.resolved&&slow->partnership.assay.observed.lower==4&&!slow->partnership.assay.consistent[0]&&slow->partnership.assay.consistent[1],"actual visible queue selects delayed route predictor without averaging models");
    slow->ready(64,0); check(decide(slow->in,slow->partnership,slow->ledger,slow->probes,Arm::active).option==0&&
        decide(slow->in,slow->partnership,slow->ledger,slow->probes,Arm::control).option==1,"resolved slower route keeps active U while control retains nominal H with identical accurate observed result");
    auto control=std::make_unique<Fixture>(); control->cycle(0,1,2,Arm::control);
    check(control->partnership.assay.resolved&&control->partnership.assay.observed.lower==4&&organization(control->in,control->partnership,Arm::control)==Organization::H,
        "separate control physically observes the same delayed bank stage while applying its original nominal organization rule");
    auto overlap=std::make_unique<Fixture>(3,true); overlap->cycle(0,0,2); overlap->ready(64,0);
    check(overlap->partnership.assay.consistent[0]&&overlap->partnership.assay.consistent[1]&&decide(overlap->in,overlap->partnership,overlap->ledger,overlap->probes,Arm::active).option==0,"an endpoint overlapping both declared intervals retains both explanations and independent work");
    auto unknown=std::make_unique<Fixture>(); unknown->cycle(0,0,0,Arm::active,true); unknown->ready(64,0);
    check(decide(unknown->in,unknown->partnership,unknown->ledger,unknown->probes,Arm::active).option==0&&unknown->partnership.assay.attempted,"unknown first observation consumes the one stage without licensing H or retrying from an empty sample");
    auto empty=std::make_unique<Fixture>(); empty->cycle(0,0,4);
    check(empty->partnership.assay.model_set_failed&&!empty->partnership.assay.resolved&&!empty->partnership.assay.consistent[0]&&!empty->partnership.assay.consistent[1],"actual out-of-family route duration retains empty-model failure instead of choosing the least wrong explanation");
}
__attribute__((noinline)) void current_safety_adoption_and_probe_boundaries() {
    auto f=std::make_unique<Fixture>(); f->cycle(0,0); f->ready(64,1);
    f->in.offers[1].safety[0].bank_executable=false;
    check(decide(f->in,f->partnership,f->ledger,f->probes,Arm::active).option==0,"model resolution never substitutes for helper's current individual survival and bank feasibility");
    f->ready(65,1); f->in.frame.own.gold=0; f->tick(65); f->in.offers[1].capital_cost=1;
    check(decide(f->in,f->partnership,f->ledger,f->probes,Arm::active).option==0,"unknown or future partner gold cannot finance a currently unfunded supported package");
    f->in.offers[1].capital_cost=0; f->in.own_state.value.equipment_hash=100; f->tick(66);
    check(decide(f->in,f->partnership,f->ledger,f->probes,Arm::active).option==0,"changed actual entry gear invalidates the old route evidence instead of inheriting H authority");
    auto no_disagreement=std::make_unique<Fixture>();
    no_disagreement->stress_window=78; no_disagreement->tick(0);
    check(matching_models(no_disagreement->in,no_disagreement->partnership)&&!disagreement(no_disagreement->in),"valid explicit models that imply the same organization supply no consequential diagnostic opportunity");
    no_disagreement->cycle(0,0); check(!no_disagreement->partnership.assay.attempted,"agreeing models keep first income ordinary and do not manufacture a probe");
    auto false_reward=std::make_unique<Fixture>(); ++false_reward->in.models[1].supported.productions[0].gems.value;
    check(!matching_models(false_reward->in,false_reward->partnership),"different invented rewards cannot masquerade as consequential route-duration disagreement");
    auto paid=std::make_unique<Fixture>(3,false,true); paid->in.offers[2]=paid->in.offers[0]; paid->in.offers[2].plan.group=9; paid->in.offers[2].schedule.target_id=9;
    paid->in.offers[2].safety[2].group=9;
    paid->in.offer_count=3; paid->in.independent=2; paid->in.offers[0].diagnostic_segment=true; paid->in.offers[0].marked_extra_turns=5;
    paid->in.probe.offer=0; paid->in.probe.endpoint_distinguishes=true;
    for (int i=0;i<3;++i) { paid->in.probe.branches[i]=paid->in.offers[0].schedule; paid->in.probe.continuation_plans[i]=i==1?paid->spec.supported:paid->spec.assay;
        paid->in.probe.safety[i]=paid->in.offers[0].safety; paid->in.probe.safety_count[i]=3; }
    paid->in.probe.safety[1][0].group=8;
    paid->in.probe.branches[2].actors[0].occupied_turns.value+=20;
    check(!useful_probe(paid->in,paid->partnership,paid->ledger,paid->probes,0,2),"even a distinguishable endpoint cannot justify a probe whose complete ambiguous branch loses to productive U");
    check(paid->probes.spend(0)&&paid->probes.spend(1),"persistent diagnostic lifetime budget is explicitly exhausted");
    const auto decision=decide(paid->in,paid->partnership,paid->ledger,paid->probes,Arm::active);
    check(decision.option==2&&!decision.deliberate_probe&&!paid->partnership.assay.attempted,"marked stage has no free execution after spent lifetime probe slots");
    auto expiring=std::make_unique<Fixture>(); expiring->cycle(0,0); expiring->ready(260,1);
    check(decide(expiring->in,expiring->partnership,expiring->ledger,expiring->probes,Arm::active).option==0,
        "model evidence which expires before the worst declared H completion cannot commission new dependent work");
}
__attribute__((noinline)) void marked_whole_bank_leg_counts_actual_moves() {
    auto fast=std::make_unique<Fixture>(3,false,true); fast->paid_counter_primitive=true; fast->cycle(0,0);
    check(fast->partnership.assay.resolved&&fast->partnership.assay.marked_turns==3&&fast->probes.used==1,
        "marked leg counts payout movement intermediate movement and bank arrival even with zero queue holds");
    auto queued=std::make_unique<Fixture>(3,false,true); queued->paid_counter_primitive=true; queued->cycle(0,0,2);
    check(queued->partnership.assay.resolved&&queued->partnership.assay.marked_turns==5&&queued->probes.used==1&&!queued->probes.overrun,
        "whole paid leg counts two actual queue holds plus both movements and inclusive arrival at the declared bound");
    auto capped=std::make_unique<Fixture>(3,false,true); capped->paid_counter_primitive=true; capped->cycle(0,0,4);
    check(capped->partnership.assay.unavailable&&!capped->partnership.assay.resolved&&capped->partnership.assay.marked_turns==5&&
        capped->probes.used==1&&capped->work.bank_seen&&capped->work.renewal_seen&&capped->in.frame.own.actor.bag==0,
        "bound exhaustion stops experimental evidence while actual retained bag safely banks and renews without another charge");
    capped->ready(128,0); check(capped->partnership.assay.attempted&&capped->probes.used==1&&!capped->probes.available(0),
        "later epoch never resets the spent slot or once-only stage after paid bound exhaustion");
    auto disabled=std::make_unique<Fixture>(); disabled->ready(0,0);
    check(!disabled->partnership.assay.issue(disabled->spec,disabled->in,true),"default zero bound makes paid stages explicitly unavailable");
}
__attribute__((noinline)) void helper_personally_adopts_after_independent_income() {
    auto f=std::make_unique<Fixture>(1); f->in.offer_count=1; f->cycle(0,0);
    check(f->work.bank_seen&&f->work.renewal_seen&&f->ledger.size()==0,"future helper first completes its own independent income without reading any investigator evidence");
    Plan p=route(3,true); p.ingress=path({{2,3}}); p.capture=path({{2,3},{3,2}});
    p.release=path({{3,2},{3,3}}); p.bank=path({{3,2},{3,3},{4,3},{4,4}});
    p.renewal=path({{4,4},{3,4},{2,4},{1,4},{0,3},{0,2},{0,1},{0,0}}); p.forecast_rounds=24;
    f->in.offers[1].plan=p; f->in.offer_count=2; f->ready(64,1); f->in.frame.own.at_spawn=false; f->tick(64);
    for (int id=1;id<=3;++id) f->local.safety[id-1]=certificate(f->in.frame,id,8);
    f->in.visible_proposer.knowledge=p6::Knowledge::unknown;
    check(decide(f->in,f->partnership,f->ledger,f->probes,Arm::active).option==0,"unseen invitation cannot silently recruit an independent actor");
    f->in.visible_proposer.knowledge=p6::Knowledge::exact; const auto d=decide(f->in,f->partnership,f->ledger,f->probes,Arm::active);
    check(d.option==1&&!d.record_episode&&!f->partnership.declared(),"visible currently safe investigator recruits using only helper's local state and no shared private proof");
    const auto admission=commit(f->in,f->local,d,f->partnership,f->ledger,f->probes,Arm::active,f->work,f->trace);
    check(admission.started&&!admission.episode,"personally accepted role changes actual Work without transferring an evidence receipt");
    auto step=[&](int offset,p6::Point position) {
        f->in.frame.own.actor.position=position; f->in.frame.own.actor.spawn_group=position==p6::Point{3,2}?8:-1;
        f->tick(64+offset); const auto command=advance(f->in,f->local,f->partnership,f->ledger,Arm::active,f->work);
        check(f->work.confirm(command,{command.clock,command.clock,command.token,command.action,true,true}),"new helper actually performs its accepted retained duty"); return command;
    };
    check(step(0,{2,3}).action==PhysicalAction::attack,"new member contributes actual productive clear work");
    f->source(8,p6::Lifecycle::reward_available); step(1,{2,3}); f->actor(3).position={3,2}; f->actor(3).spawn_group=8;
    check(step(2,{3,2}).action==PhysicalAction::move,"helper leaves future reward ownership only after personally checking actual safe holder"); step(3,{3,3});
    check(f->work.helper_released&&f->work.renewal_seen&&!f->work.payout_seen&&!f->work.active&&f->in.frame.own.actor.bag==0,
        "actual released helper reaches its verified independent productive waypoint while collector keeps future capture");
}
}
int main() {
    productive_disagreement_then_supported_work(); slow_overlap_and_unknown_retain_independence(); current_safety_adoption_and_probe_boundaries(); marked_whole_bank_leg_counts_actual_moves(); helper_personally_adopts_after_independent_income();
    std::printf("plan8 disagreement partnership: %d checks passed\n",checks); return 0;
}
