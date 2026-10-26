#include "../common-cause-firewall/src/purchases.h"
#include "../common-cause-firewall/src/draft.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>
using namespace p8;
using namespace p8::common_cause_firewall;
namespace {
int checks=0;
void check(bool ok,const char* message) { ++checks; if (!ok) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); } }
template<class T> p6::Fact<T> fact(T value) { return {value,p6::Source::synthetic_fixture}; }
Route path(std::initializer_list<p6::Point> points) { Route r; r.verified=true; r.count=int(points.size()); int i=0; for (auto p:points) r.tiles[i++]=p; return r; }
Plan circuit(int actor,bool supported=false,bool joint=false) {
    const bool field=actor==1&&!joint;
    Plan p; p.organization=joint?Organization::J:supported?Organization::H:Organization::U;
    p.group=field?7:8; p.collector=joint?1:actor; p.altar=field?4:5;
    p.crew[0]=actor; p.crew_count=1;
    p.ingress=field?path({{0,0},{1,0},{2,0}}):path({{0,2},{1,2},{2,2}});
    p.capture=field?path({{2,0},{3,0}}):path({{2,2},{3,2}});
    p.bank=field?path({{3,0},{4,0}}):path({{3,2},{4,2}});
    p.renewal=field?path({{4,0},{4,1},{3,1},{2,1},{1,1},{0,0}}):path({{4,2},{4,3},{3,3},{2,3},{1,3},{0,2}});
    p.pads[0]=field?p6::Point{2,0}:p6::Point{2,2}; p.pads[1]={2,3};
    if (supported) { p.crew={2,3,-1,-1,-1,-1}; p.crew_count=2; }
    if (joint) { p.crew={1,2,3,-1,-1,-1}; p.crew_count=3; p.pads[1]={2,1}; p.pads[2]={3,1}; }
    p.forecast_rounds=field||supported||joint?18:19; p.capture_window=5; p.remaining_rounds=100;
    p.gem_bearing=p.complete_verified=p.rally_legal=true; return p;
}
void event(p6::ActorSchedule& a,p6::EventKind kind,int id,int tick,int pi=-1) {
    p6::Event e; e.kind=kind; e.event_id=id; e.tick=tick; e.production_index=pi; e.gold_cost=fact<p6::Gems>(0);
    e.waypoint_id=kind==p6::EventKind::renewal?90+a.actor_id:-1; e.source=p6::Source::synthetic_fixture; a.events[a.event_count++]=e;
}
void production(p6::Schedule& s,int actor,int group,int amount,int payout,int bank) {
    auto& a=s.actors[actor-1]; const int pi=s.production_count++; auto& p=s.productions[pi];
    p.event_id=1000+group; p.tick=payout; p.gems=fact<p6::Gems>(amount); p.gold=fact<p6::Gems>(0);
    p.source=p6::Source::synthetic_fixture; p.holder_count=1; p.holders[0]=actor;
    event(a,p6::EventKind::production,p.event_id,payout,pi); event(a,p6::EventKind::tribute,2000+group,bank);
}
// Conditional public fixture assumptions, not native throughput evidence:
// primaries7/8 both60HP and12gems. Secondary9/10 both120HP and24gems;
// source10 has the longer independent approach. A helper's source11 yields10.
// Current solo8 splits60HP over2targets, so four20-damage hits are needed;
// H/J each exercise actual own+visible helper attacks. Future source9 can be
// cleared by the represented crew before the39-turn stress deadline. All
// displaced production, later banks and return/queue time remain in the lists.
p6::Schedule schedule(const p6::Frame& f,Organization organization,int target) {
    p6::Schedule s; s.actor_count=3; s.target_id=target;
    for (int i=0;i<3;++i) {
        auto& a=s.actors[i]; a.actor_id=i+1; const bool own=i+1==f.clock.self; const auto* seen=f.visible(i+1);
        a.initial_bag={seen?seen->bag:0,own?p6::Source::self_observed:p6::Source::visible_subject};
        a.initial_gold={own?f.own.gold:0,own?p6::Source::self_observed:p6::Source::conservative};
    }
    if (organization==Organization::J) {
        production(s,1,8,12,11,14); production(s,1,9,24,31,35);
        for (auto& a:s.actors) if (a.actor_id>=1&&a.actor_id<=3) a.occupied_turns=fact<p6::Gems>(37);
    } else {
        const bool h=organization==Organization::H;
        const int connector=f.clock.self==1&&f.own.actor.position==p6::Point{0,2}?2:0;
        production(s,1,7,12,12+connector,15+connector); production(s,1,10,24,40+connector,44+connector);
        production(s,2,8,12,h?11:13,h?14:16); production(s,2,9,24,h?32:36,h?36:40);
        production(s,3,11,10,h?46:42,h?49:45);
        s.actors[0].occupied_turns=fact<p6::Gems>(48+connector);
        s.actors[1].occupied_turns=fact<p6::Gems>(h?38:44); s.actors[2].occupied_turns=fact<p6::Gems>(h?52:48);
    }
    for (int i=0;i<3;++i) event(s.actors[i],p6::EventKind::renewal,3000+i,int(s.actors[i].occupied_turns.value));
    return s;
}
p6::CycleCertificate cert(const p6::Frame& f,int id,int group) {
    p6::CycleCertificate c; c.clock=f.clock; c.actor=id; c.group=group; const auto* a=f.visible(id);
    if (a) c.clear=c.hold=c.bank=c.recovery={a->health,a->max_health,10,0,0,0,true};
    c.route_known=c.bank_executable=c.renewal_reachable=c.solo=true; return c;
}
struct Fixture {
    Inputs in{};
    int owner=1;
    EvidenceLedger ledger;
    ProbeBudget probes{};
    LocalFrame local{};
    Work work{};
    CycleTrace trace{};
    ContextDeclaration component{},supported{};
    Arm arm=Arm::active;
    Plan selected{};
    int start_round=-1;
    bool recording=false;
    explicit Fixture(int actor=1):owner(actor),ledger(actor) {
        auto& f=in.frame; f.clock={0,owner,0}; f.own.gold=60;
        for (int id=1;id<=3;++id) {
            p6::Actor a; a.id=id; a.pick_ordinal=id-1; a.team=p6::Team::ally;
            a.type=id==1?p6::Champion::tavi:id==2?p6::Champion::varg:p6::Champion::marcella;
            a.health=a.max_health=100; a.strength=20; a.alive=a.can_move=a.can_attack=true;
            a.position=id==1?p6::Point{0,0}:id==2?p6::Point{2,2}:p6::Point{2,3};
            if (id==owner) f.own.actor=a; else f.actors[f.actor_count++]={a,f.clock,id,p6::Knowledge::exact};
        }
        f.own.actor.position=owner==1?p6::Point{0,0}:p6::Point{0,2}; f.own.at_spawn=owner!=1;
        p6::Group g; g.id=owner==1?7:8; g.lifecycle=p6::Lifecycle::living;
        g.original_spawn_visible=g.all_spawns_visible=g.uncontested_now=true; f.group={g,f.clock,g.id,p6::Knowledge::exact};
        in.settings={2000,1000,5}; in.settings_identity={17,p6::Source::queried_setting};
        in.draft.champions={p6::Champion::tavi,p6::Champion::varg,p6::Champion::marcella,p6::Champion::other,p6::Champion::other};
        in.draft.actors={1,2,3,-1,-1}; in.draft.picks=in.picks=3;
        in.layout.field=circuit(1); in.layout.spawn_component=circuit(2); in.layout.spawn_supported=circuit(2,true); in.layout.uniform=circuit(1,false,true);
        in.layout.spawn_shop={0,2}; in.layout.field_work=in.layout.spawn_work={60,p6::Source::public_map};
        in.layout.field_reward=in.layout.spawn_reward={12,p6::Source::public_map};
        in.market.inventory={{},f.clock,owner,p6::Knowledge::exact};
        in.own_state={{100,100,20,10,1,0,in.market.inventory.value.hash()},f.clock,owner,p6::Knowledge::exact};
        in.threat_regime={0,f.clock,owner,p6::Knowledge::exact};
        in.target_group=owner==1?7:8; in.offer_count=3; in.independent=0;
        in.offers[0].plan=owner==1?in.layout.field:in.layout.spawn_component;
        in.offers[1].plan=in.layout.spawn_supported; in.offers[2].plan=in.layout.uniform;
        component.key.settings_id=17; component.key.actor_id=owner; component.key.champion=int(f.own.actor.type); component.key.role=0;
        component.key.source_group=in.target_group; component.key.route_template=owner-1; component.key.endpoint=owner==1?4:5;
        component.key.threat_regime=0; component.key.participant_count=1; component.key.participants[0]=owner;
        component.key.route_hash=plan_route_hash(in.offers[0].plan); component.key.return_route_hash=plan_return_hash(in.offers[0].plan);
        component.key.build={in.market.inventory.value.hash(),100,20,10,1};
        component.envelope={{75,100},{100,100},{20,20},{10,10},{1,1},{0,100},0}; component.require_spawn_shop=owner==2;
        for (int h=0;h<3;++h) component.hypotheses[h].offset={2*h,2*h+2};
        in.component_slot=ledger.predeclare(component); in.component_entry=in.offers[0].entry=component.key; in.offers[0].context_slot=in.component_slot;
        if (owner==2) {
            supported=component; supported.kind=EpisodeKind::full_organization; supported.require_supported_work=true;
            supported.key.role=1; supported.key.participant_count=2; supported.key.participants[1]=3;
            supported.key.route_hash=plan_route_hash(in.offers[1].plan);
            in.supported_slot=ledger.predeclare(supported); in.supported_entry=in.offers[1].entry=supported.key; in.offers[1].context_slot=in.supported_slot;
        }
        for (auto& q:in.market.items) { q.price={30,p6::Source::queried_setting}; q.purchasable={true,p6::Source::queried_setting}; }
        check(in.layout.valid(in.draft),"public comparable districts have physically distinct field and spawn dependencies");
        tick(0);
    }
    p6::Actor& actor(int id) { if (id==owner) return in.frame.own.actor; for (int i=0;i<in.frame.actor_count;++i) if (in.frame.actors[i].subject==id) return in.frame.actors[i].value; std::abort(); }
    void tick(int round,int revision=0) {
        auto& f=in.frame; f.clock={round,owner,revision}; f.group.clock=f.clock;
        for (int i=0;i<f.actor_count;++i) f.actors[i].clock=f.clock;
        auto& n=in.own_state.value; n.health=f.own.actor.health; n.max_health=f.own.actor.max_health; n.strength=f.own.actor.strength; n.bag=f.own.actor.bag; n.equipment_hash=in.market.inventory.value.hash();
        in.own_state.clock=in.threat_regime.clock=in.market.inventory.clock=f.clock;
        in.market.spawn_shop_legal={f.own.at_spawn,f.clock,owner,p6::Knowledge::exact};
        in.field_shop_legal={f.own.actor.type==p6::Champion::tavi,f.clock,owner,p6::Knowledge::exact};
        for (auto& q:in.market.items) q.buy_legal={q.price.value<=f.own.gold&&(f.own.at_spawn||owner==1),f.clock,owner,p6::Knowledge::exact};
        in.windows.valid=true; in.windows.source=p6::Source::synthetic_fixture; in.windows.deadlines={2000-round,78,39}; in.windows.windows={120,78,39};
        in.windows.sampled_round=round; in.windows.sampled_actor_id=owner;
        for (int k=0;k<3;++k) {
            auto& o=in.offers[k]; o.schedule=schedule(f,o.plan.organization,o.plan.group); o.safety_count=3;
            for (int id=1;id<=3;++id) o.safety[id-1]=cert(f,id,common::participant(o.plan,id)?o.plan.group:11);
        }
        local.frame=f; local.current_round=round; local.safety_count=3; local.milestones={};
        const auto& p=work.active?work.plan:in.offers[0].plan;
        for (int id=1;id<=3;++id) local.safety[id-1]=cert(f,id,p.group);
        local.legal={{},f.clock,owner,p6::Knowledge::exact}; auto& legal=local.legal.value;
        legal.safe_now=legal.attack_legal=legal.altar_legal=legal.renewal_productive=legal.recall_safe=true;
        for (Leg leg:{Leg::ingress,Leg::capture,Leg::bank,Leg::renewal,Leg::release}) {
            auto& access=legal.routes[int(leg)]; const auto& r=route_for(p,leg); access.assumptions_hold=route_valid(r); access.next_move_legal=true;
            for (int i=0;i<r.count;++i) if (r.tiles[i]==f.own.actor.position) { access.next_tile=r.tiles[std::min(i+1,r.count-1)]; break; }
        }
    }
    void begin(int round,Arm chosen=Arm::active) {
        work={}; arm=chosen; start_round=round;
        const bool h=owner==2&&own_component(in,ledger)&&chosen==Arm::active;
        selected=chosen==Arm::control?in.layout.uniform:owner==1?in.layout.field:h?in.layout.spawn_supported:in.layout.spawn_component;
        auto& f=in.frame; f.own.actor.position=selected.ingress.tiles[0]; f.own.actor.spawn_group=-1; f.own.actor.bag=0; f.own.actor.channel=p6::Channel::none; f.own.at_spawn=f.own.actor.position==in.layout.spawn_shop;
        f.group.value.id=selected.group; f.group.subject=selected.group; f.group.value.lifecycle=p6::Lifecycle::living;
        f.group.value.approach=selected.capture.tiles[0]; f.group.value.spawn=selected.capture.tiles[1];
        for (int id=1;id<=3;++id) if (id!=owner) { auto& a=actor(id); a.action=p6::WitnessAction::none; a.spawn_group=-1;
            if (chosen==Arm::control) a.position=selected.pads[crew_index(selected,id)]; else a.position=id==1?p6::Point{2,0}:id==2?p6::Point{2,2}:p6::Point{2,3}; }
        tick(round); for (int id=1;id<=3;++id) local.safety[id-1]=cert(f,id,selected.group);
        const auto d=decide(in,ledger,probes,arm); const int expected=chosen==Arm::control?2:h?1:0;
        if (d.option!=expected) std::fprintf(stderr,"begin actor=%d round=%d arm=%d option=%d expect=%d rejects=%d/%d/%d\n",owner,round,int(arm),d.option,expected,int(d.rejected[0]),int(d.rejected[1]),int(d.rejected[2]));
        check(d.option==expected,"same unchanged comparator chooses card-admissible U H or superior J");
        const auto admitted=commit(in,local,d,ledger,probes,arm,work);
        check(admitted.started&&bool(admitted.episode)==d.record_episode,"own episode is issued before actual work only in its private evidence context");
        recording=d.record_episode; if (recording) check(trace.begin(ledger,work),"new Work binds private own receipt trace");
    }
    Command step(int offset,p6::Point position,bool payout=false,bool bank=false,bool blocked=false) {
        auto& f=in.frame; f.own.actor.position=position; f.own.at_spawn=position==in.layout.spawn_shop;
        f.own.actor.spawn_group=position==selected.capture.tiles[1]?selected.group:-1;
        if (selected.organization==Organization::H) {
            auto& helper=actor(3); helper.action=offset==2||offset==3?p6::WitnessAction::useful_attack:p6::WitnessAction::none; helper.action_group=8;
            if (offset>=5&&offset<7) { helper.position={3,3}; helper.spawn_group=8; }
            if (offset>=7) { helper.position={4,3}; helper.spawn_group=-1; }
        }
        if (selected.organization==Organization::J) for (int id=2;id<=3;++id) {
            actor(id).action=offset==2?p6::WitnessAction::useful_attack:p6::WitnessAction::none; actor(id).action_group=8;
        }
        if (payout) { f.own.actor.bag=12; f.group.value.lifecycle=p6::Lifecycle::respawning; }
        if (bank) { f.own.actor.bag=0; f.own.actor.channel=p6::Channel::none; }
        tick(start_round+offset);
        if (payout||bank) local.milestones={{selected.group,payout,bank,payout?12:0,bank?12:0},f.clock,owner,p6::Knowledge::exact};
        if (blocked) { local.legal.value.routes[int(Leg::renewal)].assumptions_hold=false; in.market.spawn_shop_legal.value=false; }
        const auto c=advance(in,local,ledger,arm,work);
        check(work.confirm(c,{c.clock,c.clock,c.token,c.action,true,true}),"issued physical token receives an actual same-turn action receipt");
        if (recording) trace.observe(local,work,in.market.spawn_shop_legal); return c;
    }
    int cycle(int round,Arm chosen=Arm::active,bool block_return=false) {
        begin(round,chosen); const auto p=selected;
        const int attacks=p.organization==Organization::U?(owner==2?4:3):2;
        const int shift=attacks-3;
        check(step(0,p.ingress.tiles[0]).action==PhysicalAction::move,"actual ingress begins on selected route"); step(1,p.ingress.tiles[1]);
        for (int a=0;a<attacks;++a) check(step(2+a,p.ingress.tiles[2]).action==PhysicalAction::attack,"actual own and witnessed helper attacks clear public fixed payload source");
        in.frame.group.value.lifecycle=p6::Lifecycle::reward_available; step(5+shift,p.capture.tiles[0]);
        for (int r=6+shift;r<=11+shift;++r) check(step(r,p.capture.tiles[1]).action==PhysicalAction::hold,"capture awaits actual personal reward");
        step(12+shift,p.capture.tiles[1],true); check(step(13+shift,p.bank.tiles[1]).action==PhysicalAction::tribute,"actual bag owner starts tribute");
        in.frame.own.actor.channel=p6::Channel::tribute; in.frame.own.actor.channel_valid=true;
        check(step(14+shift,p.bank.tiles[1]).action==PhysicalAction::preserve_channel,"safe tribute is preserved through policy changes");
        step(15+shift,p.bank.tiles[1],false,true);
        for (int i=1;i<p.renewal.count;++i) {
            const bool stop=block_return&&i==p.renewal.count-2;
            const auto c=step(15+shift+i,p.renewal.tiles[i],false,false,stop);
            if (stop) {
                check(c.action!=PhysicalAction::move&&work.failure==PhysicalFailure::blocked_route&&!work.renewal_seen&&!work.cycle_completed,
                    "personally blocked spawn return censors this actual dependency before productive renewal");
                if (recording) { const auto result=trace.finish(ledger,in.frame.clock.round,{supported.key,in.own_state.value,all_premises}); check(!result.full_positive,"blocked compartment cannot count incomplete return as qualification"); }
                return in.frame.clock.round;
            }
        }
        check(work.cycle_completed&&work.bank_seen&&work.renewal_seen&&!work.active,"actual capture own bank and productive return complete");
        if (recording) { const auto result=trace.finish(ledger,in.frame.clock.round,{p.organization==Organization::H?supported.key:component.key,in.own_state.value,all_premises});
            check(result.accepted&&result.full_positive,"private full own endpoint receipts support only this actor context"); }
        return in.frame.clock.round;
    }
    void qualify() { cycle(0); cycle(64); check(own_component(in,ledger),"two unchanged personal circuits qualify the actual own component"); }
};
__attribute__((noinline)) void structural_failure_survives_elsewhere() {
    auto field=std::make_unique<Fixture>(1); auto spawn=std::make_unique<Fixture>(2); auto joint=std::make_unique<Fixture>(1);
    field->qualify(); spawn->qualify();
    check(!spawn->ledger.organization_qualified(spawn->in.supported_slot,spawn->supported.key,85),"new spawn H membership inherits no full qualification");
    field->tick(100); const auto field_before=*field->ledger.state(field->in.component_slot);
    const auto field_score=common::evaluate(field->in,field->in.offers[0].schedule);
    const auto joint_score=common::evaluate(field->in,field->in.offers[2].schedule);
    check(field_score.min_margin==24&&joint_score.min_margin==36&&p6::compare(joint_score,field_score)>0,
        "conditional J future source9 timing wins common score with fixed payloads and all occupied work charged");
    const auto blocked=spawn->cycle(100,Arm::active,true);
    check(spawn->ledger.lifetime_failure_count()>0&&!spawn->work.cycle_completed,"spawn compartment retains its own actual failed return");
    field->cycle(100);
    const auto* untouched=field->ledger.state(field->in.component_slot);
    check(untouched->attempts==field_before.attempts&&untouched->positive_completions==field_before.positive_completions&&own_component(field->in,field->ledger),
        "another actor failed return neither retires private field proof nor recruits its labor");
    check(field->work.bank_seen&&field->work.renewal_seen&&field->in.frame.clock.round>blocked&&field->in.frame.own.actor.bag==0,
        "separate field U actually converts its unchanged source and renews after the witnessed spawn dependency failure");
    joint->cycle(100,Arm::control,true);
    check(joint->work.bank_seen&&!joint->work.renewal_seen&&!joint->work.cycle_completed,
        "same spawn-return failure interrupts the uniform J actual full cycle");
    check(!visits(field->selected,field->in.layout.spawn_shop)&&!common::participant(spawn->selected,1),
        "surviving circuit shares neither failed spawn tile nor Tavi helper dependency");
}
__attribute__((noinline)) void typed_field_purchase_is_local() {
    auto f=std::make_unique<Fixture>(1); f->qualify(); Shop shop;
    check(!f->in.frame.own.at_spawn&&own_component(f->in,f->ledger),"qualified field circuit ends away from spawn");
    auto p=std::make_unique<PurchasePlan>(); p->item=reversible_workshop::damage_item;
    p->without_purchase=f->in.offers[0].schedule; p->with_purchase=p->without_purchase;
    auto& own=p->with_purchase.actors[0]; --own.occupied_turns.value;
    for (int e=0;e<own.event_count;++e) if (own.events[e].kind==p6::EventKind::renewal||own.events[e].event_id==1010||own.events[e].event_id==2010) --own.events[e].tick;
    for (int i=0;i<p->with_purchase.production_count;++i) if (p->with_purchase.productions[i].event_id==1010) --p->with_purchase.productions[i].tick;
    event(own,p6::EventKind::purchase,4000,0); own.events[own.event_count-1].gold_cost=fact<p6::Gems>(30);
    f->in.market.spawn_shop_legal.value=false;
    const auto command=shop.propose(f->in,f->local,f->ledger,f->work,*p);
    check(command.valid&&!command.at_spawn,"actual Tavi can fund typed field purchase despite unavailable spawn shop");
    const auto health=f->in.frame.own.actor.health; const auto gold=f->in.frame.own.gold;
    f->in.market.inventory.value=command.after; f->in.frame.own.gold-=30; f->in.frame.own.actor.strength=24;
    f->tick(command.clock.round,command.clock.revision+1);
    const PurchaseReceipt receipt{command.clock,f->in.frame.clock,command.token,command.item,true,true};
    check(shop.confirm(command,receipt,f->in,f->ledger)&&f->in.frame.own.actor.health==health&&f->in.frame.own.gold==gold-30,
        "actual own item debit and queried new strength confirm without field healing or score credit");
    check(!shop.confirm(command,receipt,f->in,f->ledger),"duplicate field purchase receipt cannot spend or qualify again");
    check(!own_component(f->in,f->ledger),"material own purchase expires old exact build proof rather than transferring it");
}
__attribute__((noinline)) void dependency_and_shop_guards() {
    auto f=std::make_unique<Fixture>(1); auto invalid=std::make_unique<Layout>(f->in.layout);
    invalid->spawn_shop={3,0}; check(!invalid->valid(f->in.draft),"shared critical return tile cannot masquerade as a firewall");
    invalid->spawn_shop=f->in.layout.spawn_shop; invalid->spawn_supported.crew[1]=1;
    check(!invalid->valid(f->in.draft),"spawn H cannot require field actor as helper");
    auto s=std::make_unique<Fixture>(2); s->qualify(); s->cycle(100); s->cycle(140);
    check(own_organization(s->in,s->ledger),"spawn H needs its own two complete observed supported cycles");
    Shop shop; auto p=std::make_unique<PurchasePlan>(); p->item=reversible_workshop::damage_item;
    p->without_purchase=s->in.offers[0].schedule; p->with_purchase=p->without_purchase;
    auto& own=p->with_purchase.actors[1]; --own.occupied_turns.value;
    for (int e=0;e<own.event_count;++e) if (own.events[e].kind==p6::EventKind::renewal) --own.events[e].tick;
    event(own,p6::EventKind::purchase,4002,0); own.events[own.event_count-1].gold_cost=fact<p6::Gems>(30);
    s->in.market.spawn_shop_legal.value=false;
    check(!shop.propose(s->in,s->local,s->ledger,s->work,*p).valid,"own blocked spawn shop cannot be replaced by Tavi field capability");
    s->in.market.spawn_shop_legal.value=true; s->in.frame.own.gold=29; s->tick(161);
    check(!shop.propose(s->in,s->local,s->ledger,s->work,*p).valid,"future gems or another compartment gold cannot fund present purchase");
    check(s->ledger.state(s->in.supported_slot)->positive_completions==2&&f->ledger.size()==1,
        "private evidence and financial permissions remain actor scoped");
}
} // namespace
int main() { structural_failure_survives_elsewhere(); typed_field_purchase_is_local(); dependency_and_shop_guards(); std::printf("plan8 common cause firewall: %d checks passed\n",checks); }
