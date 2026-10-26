#include "../finite-counterprotocol/src/policy.h"
#include "../finite-counterprotocol/src/draft.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>
using namespace p8;
using namespace p8::finite_counterprotocol;
namespace {
int checks=0;
void check(bool ok,const char* text) { ++checks; if (!ok) { std::fprintf(stderr,"FAIL: %s\n",text); std::exit(1); } }
template<class T> p6::Fact<T> fact(T value) { return {value,p6::Source::synthetic_fixture}; }
Route path(std::initializer_list<p6::Point> points) { Route r; r.verified=true; r.count=int(points.size()); int i=0; for (auto p:points) r.tiles[i++]=p; return r; }
Plan solo(int owner=1,bool after=false,bool long_bank=false) {
    Plan p; p.organization=Organization::U; p.collector=owner; p.crew[0]=owner; p.crew_count=1;
    const int y=owner==1?0:owner==2?2:6;
    p.group=owner==1?7:owner==2?(after?9:10):11; p.altar=owner==1?4:owner==2?(after?6:4):8;
    p.ingress=owner==1?path({{0,0},{1,1},{2,1}}):after?path({{4,2},{5,3},{6,3}}):path({{0,y},{1,y},{2,y}});
    p.capture=owner==1?path({{2,1},{3,0}}):after?path({{6,3},{7,3}}):path({{2,y},{3,y}});
    p.bank=owner==1?(long_bank?path({{3,0},{4,0},{5,0},{5,1}}):path({{3,0},{3,1},{4,1}})):
        after?path({{7,3},{8,3}}):path({{3,y},{4,y}});
    p.renewal=owner==1?(long_bank?path({{5,1},{4,1},{3,1},{2,1},{1,1},{0,0}}):path({{4,1},{4,0},{3,0},{2,0},{1,0},{0,0}})):
        after?path({{8,3},{7,3},{6,3},{5,3},{4,3},{4,2}}):path({{4,y},{4,y+1},{3,y+1},{2,y+1},{1,y+1},{0,y}});
    p.forecast_rounds=18; p.remaining_rounds=100; p.capture_window=5; p.gem_bearing=p.complete_verified=p.rally_legal=true; return p;
}
Layout layout() {
    Layout l; l.short_solo=solo(); l.collector=l.short_solo; l.collector.organization=Organization::H;
    l.collector.ingress=path({{0,0},{1,0},{2,0}}); l.collector.capture=path({{2,0},{3,0}});
    l.collector.crew={1,2,-1,-1,-1,-1}; l.collector.crew_count=2; l.collector.pads[0]={2,0}; l.collector.pads[1]={2,1};
    l.helper=l.collector; l.helper.ingress=path({{0,2},{1,2},{2,1}}); l.helper.capture=path({{2,1},{3,1}});
    // The public source has distinct adjacent capture tiles30/31. Both members
    // can occupy it, then the helper exits31->42 before the collector's payout.
    l.helper.release=path({{3,1},{4,2}}); l.helper.bank=l.helper.release; l.helper.renewal=path({{4,2}}); l.helper.forecast_rounds=30;
    l.helper_after=solo(2,true); l.escort_pad={4,2}; l.escort_wait_bound=12;
    l.site.opportunity=4; l.site.altar=4; l.site.width=l.site.height=20; l.site.altar_tile={5,2}; l.site.target={5,1};
    l.site.own_approaches={p6::Point{5,1},p6::Point{4,1}}; l.site.approach_count=2; l.site.map_identity={27,p6::Source::public_map}; return l;
}
void event(p6::ActorSchedule& a,p6::EventKind kind,int id,int tick,int production=-1) {
    p6::Event e; e.kind=kind; e.event_id=id; e.tick=tick; e.production_index=production; e.gold_cost=fact<p6::Gems>(0);
    e.waypoint_id=kind==p6::EventKind::renewal?90+a.actor_id:-1; e.source=p6::Source::synthetic_fixture; a.events[a.event_count++]=e;
}
void income(p6::Schedule& s,int actor,int source,int amount,int payout,int bank) {
    const int pi=s.production_count++; auto& p=s.productions[pi]; p.event_id=1000+source; p.tick=payout; p.gems=fact<p6::Gems>(amount);
    p.gold=fact<p6::Gems>(0); p.source=p6::Source::synthetic_fixture; p.holder_count=1; p.holders[0]=actor;
    event(s.actors[actor-1],p6::EventKind::production,p.event_id,payout,pi); event(s.actors[actor-1],p6::EventKind::tribute,2000+source,bank);
}
// Predeclared synthetic timing assumptions, not measured protection evidence:
// sources7/8 each60HP and12gems; sources9/10 and11/12 each10gems. The best short
// solo path is ALSO offered and has the same bank/return geometry as H. Actual
// H saves only one own clearance turn (2own20 hits+1helper20 vs3own20). Its
// secondary bank39 beats best solo40, enough to repay the helper's lost10.
// Escort gives NO timing, survival or deterrence credit. It costs the actual
// helper through bank15, followed by own jobs bank30/50 and renew35/55. Equal
// critical clearance is a separate negative timing scenario: the convoy loses.
void schedule(p6::Schedule& s,const p6::Frame& f,const Plan& p,bool equal_clear=false,bool long_path=false) {
    s={}; s.actor_count=3; s.target_id=p.group; const bool h=p.organization==Organization::H;
    for (int id=1;id<=3;++id) {
        auto& a=s.actors[id-1]; const auto* seen=f.visible(id); const bool own=id==f.clock.self; a.actor_id=id;
        a.initial_bag={seen?seen->bag:0,own?p6::Source::self_observed:p6::Source::visible_subject};
        a.initial_gold={own?f.own.gold:0,own?p6::Source::self_observed:p6::Source::conservative};
    }
    const int gain=h&&!equal_clear?1:0,extra=long_path?1:0;
    income(s,1,7,12,12-gain,16-gain+extra); income(s,1,8,12,37-gain,40-gain+extra);
    const bool after=p.organization==Organization::U&&p.collector==2&&p.group==9;
    income(s,2,h||after?9:10,10,h?28:12,h?30:14); income(s,2,h||after?10:9,10,h?48:32,h?50:35);
    income(s,3,11,10,12,14); income(s,3,12,10,33,36);
    s.actors[0].occupied_turns=fact<p6::Gems>(45-gain+extra); s.actors[1].occupied_turns=fact<p6::Gems>(h?55:40); s.actors[2].occupied_turns=fact<p6::Gems>(41);
    for (int i=0;i<3;++i) event(s.actors[i],p6::EventKind::renewal,3000+i,int(s.actors[i].occupied_turns.value));
}
p6::CycleCertificate certificate(const p6::Frame& f,int id,int group) {
    p6::CycleCertificate c; c.clock=f.clock; c.actor=id; c.group=group; const auto* a=f.visible(id);
    // Each exposure is independently conservative. No shielding/tank-aggro
    // reduction is supplied by convoy presence or the observed response label.
    if (a) c.clear=c.hold=c.bank=c.recovery={a->health,a->max_health,10,0,0,0,true};
    c.route_known=c.bank_executable=c.renewal_reachable=c.solo=true; return c;
}
enum class Pattern { approach,stay,unknown_path,lost_vision,changed_identity,none };
struct Fixture {
    Inputs in{}; int owner=1; EvidenceLedger ledger; Protocol protocol{}; ProbeBudget probes{}; LocalFrame local{};
    Work work{}; ConvoyTrace trace{}; ContextDeclaration full{}; Plan selected{};
    Arm arm=Arm::active; Pattern pattern=Pattern::approach; int started=-1,baseline_offset=-1; bool equal_clear=false;
    explicit Fixture(int actor=1):owner(actor),ledger(actor) {
        auto& f=in.frame; f.clock={0,owner,0}; f.own.gold=60; in.layout=layout();
        in.draft.champions={p6::Champion::vendetta,p6::Champion::willow,p6::Champion::vesper_vale,p6::Champion::other,p6::Champion::other};
        in.draft.actors={1,2,3,-1,-1}; in.draft.picks=in.picks=3;
        for (int id=1;id<=3;++id) {
            p6::Actor a; a.id=id; a.pick_ordinal=id-1; a.type=in.draft.champions[id-1]; a.team=p6::Team::ally; a.health=a.max_health=100;
            a.strength=20; a.alive=a.can_move=a.can_attack=true; a.ready_attack_group=7; a.position=id==1?p6::Point{0,0}:id==2?p6::Point{2,1}:p6::Point{0,6};
            if (id==owner) f.own.actor=a; else f.actors[f.actor_count++]={a,f.clock,id,p6::Knowledge::exact};
        }
        p6::Actor enemy; enemy.id=91; enemy.team=p6::Team::enemy; enemy.type=p6::Champion::varg; enemy.health=enemy.max_health=100;
        enemy.strength=0; enemy.alive=enemy.can_move=true; enemy.position={9,1}; f.actors[f.actor_count++]={enemy,f.clock,91,p6::Knowledge::exact};
        f.own.actor.position=solo(owner).ingress.tiles[0]; f.own.at_spawn=true;
        p6::Group g; g.id=solo(owner).group; g.lifecycle=p6::Lifecycle::living; g.original_spawn_visible=g.all_spawns_visible=g.uncontested_now=true;
        f.group={g,f.clock,g.id,p6::Knowledge::exact}; in.settings={2000,1000,5}; in.settings_identity={17,p6::Source::queried_setting};
        in.own_state={{100,100,20,10,1,0,99},f.clock,owner,p6::Knowledge::exact}; in.threat_regime={0,f.clock,owner,p6::Knowledge::exact};
        in.target_group=7; in.independent=0; in.offer_count=owner==3?1:3; in.rival=91;
        in.offers[0].plan=solo(owner); in.offers[1].plan=owner==1?in.layout.collector:in.layout.helper;
        in.offers[2].plan=owner==1?solo(1,false,true):in.layout.helper_after;
        full.kind=EpisodeKind::full_organization; full.require_supported_work=true; full.key.settings_id=17; full.key.actor_id=1;
        full.key.champion=int(p6::Champion::vendetta); full.key.role=1; full.key.source_group=7; full.key.route_template=0; full.key.endpoint=4; full.key.threat_regime=0;
        full.key.participants={1,2,-1,-1,-1,-1}; full.key.participant_count=2; full.key.build={99,100,20,10,1};
        full.key.route_hash=plan_route_hash(in.layout.collector); full.key.return_route_hash=plan_return_hash(in.layout.collector);
        full.envelope={{75,100},{100,100},{20,20},{10,10},{1,1},{0,100},0};
        for (int i=0;i<3;++i) full.hypotheses[i].offset={2*i,2*i+2};
        if (owner==1) check(protocol.declare(in.layout,full,in.draft,ledger),"collector predeclares exact response site and actual convoy build membership and retained routes");
        in.offers[1].context_slot=protocol.slot(); in.offers[1].entry=full.key; tick(0);
    }
    p6::Actor& actor(int id) { if (id==owner) return in.frame.own.actor; for (int i=0;i<in.frame.actor_count;++i) if (in.frame.actors[i].value.id==id) return in.frame.actors[i].value; std::abort(); }
    void source(int group,p6::Lifecycle lifecycle=p6::Lifecycle::living) { in.frame.group.subject=group; in.frame.group.value.id=group; in.frame.group.value.lifecycle=lifecycle; }
    void tick(int round) {
        auto& f=in.frame; f.clock={round,owner,0}; f.group.clock=f.clock;
        for (int i=0;i<f.actor_count;++i) { f.actors[i].clock=f.clock; f.actors[i].subject=f.actors[i].value.id; }
        in.own_state.clock=in.threat_regime.clock=f.clock; in.own_state.value.health=f.own.actor.health; in.own_state.value.bag=f.own.actor.bag;
        in.windows.valid=true; in.windows.source=p6::Source::synthetic_fixture; in.windows.deadlines={2000-round,78,39}; in.windows.windows={120,78,39};
        in.windows.sampled_round=round; in.windows.sampled_actor_id=owner; in.visible_proposer={7,f.clock,1,p6::Knowledge::exact};
        for (int i=0;i<in.offer_count;++i) { auto& o=in.offers[i]; schedule(o.schedule,f,o.plan,equal_clear,owner==1&&i==2); o.safety_count=3;
            for (int id=1;id<=3;++id) o.safety[id-1]=certificate(f,id,common::participant(o.plan,id)?o.plan.group:solo(id).group); }
        auto& enemy=f.actors[f.actor_count-1];
        StaticDistance d; d.opportunity=in.layout.site.opportunity; d.map_identity=in.layout.site.map_identity.value;
        d.from=enemy.value.position; d.target=in.layout.site.target; d.distance={std::max(std::abs(d.from.x-d.target.x),std::abs(d.from.y-d.target.y)),p6::Source::public_map};
        d.shortest_path_known=d.unobstructed=true; in.distance={d,f.clock,enemy.value.id,p6::Knowledge::exact};
        local.frame=f; local.current_round=round; local.safety_count=3; local.milestones={};
        const auto& p=work.active?work.plan:in.offers[0].plan; for (int id=1;id<=3;++id) local.safety[id-1]=certificate(f,id,p.group);
        local.legal={{},f.clock,owner,p6::Knowledge::exact}; auto& l=local.legal.value;
        l.safe_now=l.attack_legal=l.altar_legal=l.recall_safe=l.renewal_productive=true;
        for (Leg leg:{Leg::ingress,Leg::capture,Leg::bank,Leg::renewal,Leg::release}) {
            auto& access=l.routes[int(leg)]; const auto& r=route_for(p,leg); access.assumptions_hold=route_valid(r); access.next_move_legal=true;
            for (int i=0;i<r.count;++i) if (r.tiles[i]==f.own.actor.position) { access.next_tile=r.tiles[std::min(i+1,r.count-1)]; break; }
        }
    }
    void ready(int round,int option,Arm chosen=Arm::active) {
        work={}; selected=in.offers[option].plan; started=round; arm=chosen;
        auto& f=in.frame; f.own.actor.position=selected.ingress.tiles[0]; f.own.actor.bag=0; f.own.actor.spawn_group=-1; f.own.actor.channel=p6::Channel::none;
        f.own.at_spawn=f.own.actor.position==solo(owner).ingress.tiles[0]; source(selected.group);
        for (int id=1;id<=3;++id) if (id!=owner) { auto& a=actor(id); a.action=p6::WitnessAction::none; a.spawn_group=-1;
            a.position=common::participant(selected,id)?selected.pads[crew_index(selected,id)]:solo(id).ingress.tiles[0]; }
        auto& enemy=in.frame.actors[in.frame.actor_count-1]; enemy.value.id=91; enemy.value.position={9,1}; enemy.knowledge=p6::Knowledge::exact; in.rival=91;
        baseline_offset=owner==1?(selected.organization==Organization::H?13:option==2?15:14):-1;
        tick(round); for (int id=1;id<=3;++id) local.safety[id-1]=certificate(f,id,selected.group);
    }
    void begin(int round,int option,Arm chosen=Arm::active) {
        ready(round,option,chosen); const auto d=decide(in,protocol,ledger,probes,arm);
        if (d.option!=option) std::fprintf(stderr,"choice owner=%d t=%d want=%d got=%d matches=%d rule=%d reject=%d/%d/%d helper=%d\n",owner,round,option,d.option,protocol.responses.matching(),int(protocol.responses.rule()),int(d.rejected[0]),int(d.rejected[1]),int(d.rejected[2]),int(protocol.helper.phase));
        check(d.option==option,"ordinary complete schedule chooses only current response-authorized actual role");
        const auto admission=commit(in,local,d,protocol,ledger,probes,arm,work,trace);
        check(admission.started&&bool(admission.episode)==d.record_episode,"convoy full episode is issued before actual movement without diagnostic expenditure");
    }
    Command step(int offset,p6::Point position,bool payout=false,bool bank=false,bool missing_escort=false) {
        auto& f=in.frame; f.own.actor.position=position; f.own.at_spawn=position==solo(owner).ingress.tiles[0];
        f.own.actor.spawn_group=position==selected.capture.tiles[selected.capture.count-1]?selected.group:-1;
        if (owner==1&&selected.organization==Organization::H) { auto& escort=actor(2); escort.action=offset==2?p6::WitnessAction::useful_attack:p6::WitnessAction::none;
            escort.action_group=7; escort.spawn_group=offset==4?7:-1;
            escort.position=offset<4?in.layout.collector.pads[1]:offset==4?p6::Point{3,1}:
                missing_escort&&offset>=6?p6::Point{5,3}:in.layout.escort_pad; }
        const int amount=owner==1?12:10;
        if (payout) { f.own.actor.bag=amount; source(selected.group,p6::Lifecycle::respawning); }
        if (bank) { f.own.actor.bag=0; f.own.actor.channel=p6::Channel::none; }
        auto& enemy=f.actors[f.actor_count-1]; const int relative=offset-baseline_offset;
        const bool responding=baseline_offset>=0&&relative>=0&&relative<=3;
        const int movement=pattern==Pattern::approach&&relative>=0&&relative<=6?(relative<=3?relative:6-relative):0;
        enemy.value.position={9-movement,1};
        if (responding&&pattern==Pattern::changed_identity&&relative>=1) { enemy.value.id=92; in.rival=92; }
        enemy.knowledge=pattern==Pattern::none||(responding&&pattern==Pattern::lost_vision&&relative==1)?p6::Knowledge::unknown:p6::Knowledge::exact;
        tick(started+offset);
        if (responding&&pattern==Pattern::unknown_path&&relative==1) in.distance.value.shortest_path_known=false;
        if (enemy.knowledge!=p6::Knowledge::exact) in.distance.knowledge=p6::Knowledge::unknown;
        if (payout||bank) local.milestones={{selected.group,payout,bank,payout?amount:0,bank?amount:0},f.clock,owner,p6::Knowledge::exact};
        const auto c=advance(in,local,protocol,ledger,arm,work);
        check(work.confirm(c,{c.clock,c.clock,c.token,c.action,true,true}),"actual one-turn retained command receives its own fresh action receipt");
        observe(in,local,protocol,ledger,work,trace); return c;
    }
    int cycle(int round,int option,Arm chosen=Arm::active,bool missing_escort=false) {
        begin(round,option,chosen); const auto p=selected; const int attacks=p.organization==Organization::H?2:3;
        check(step(0,p.ingress.tiles[0]).action==PhysicalAction::move,"actual first income starts on the retained current route"); step(1,p.ingress.tiles[1]);
        for (int a=0;a<attacks;++a) check(step(2+a,p.ingress.tiles[2]).action==PhysicalAction::attack,"actual own and visible helper clearance uses fixed public60HP source");
        int t=2+attacks; source(p.group,p6::Lifecycle::reward_available); step(t++,p.capture.tiles[0]);
        for (int i=0;i<6;++i) step(t++,p.capture.tiles[1]); step(t++,p.capture.tiles[1],true);
        for (int i=1;i<p.bank.count;++i) { const auto c=step(t++,p.bank.tiles[i],false,false,missing_escort); if (i==p.bank.count-1) check(c.action==PhysicalAction::tribute,"actual bag owner begins conversion with current escort rather than imagined deterrence"); }
        if (owner==1) { in.frame.own.actor.channel=p6::Channel::tribute; in.frame.own.actor.channel_valid=true;
            check(step(t++,p.bank.tiles[p.bank.count-1],false,false,missing_escort).action==PhysicalAction::preserve_channel,"response expiry or contradiction preserves actual safe own tribute"); }
        step(t++,p.bank.tiles[p.bank.count-1],false,true,missing_escort);
        for (int i=1;i<p.renewal.count;++i) step(t++,p.renewal.tiles[i]);
        check(!work.active&&work.cycle_completed&&work.bank_seen&&work.renewal_seen&&in.frame.own.actor.bag==0,"actual own capture bank and productive return complete without altering another actor's bag"); return in.frame.clock.round;
    }
    void two_approaches(Arm chosen=Arm::active) { pattern=Pattern::approach; cycle(0,0,chosen); cycle(40,0,chosen);
        check(protocol.responses.encounters()==2&&protocol.responses.matching()==2&&protocol.responses.approach_current(owner,in.frame.clock.round)&&probes.used==0,
            "two exact passive productive encounters create fresh own response authority without a deliberate probe"); }
};
__attribute__((noinline)) void exact_finite_response_samples() {
    auto f=std::make_unique<Fixture>(); auto window=std::make_unique<ResponseWindow>(); const auto site=f->in.layout.site;
    f->in.frame.own.actor.position={4,1}; f->tick(0); check(window->begin(f->in.frame,site,91,f->in.distance),"exact visible baseline starts one bounded response window");
    for (int i=1;i<=3;++i) { f->actor(91).position={9-i,1}; f->tick(i); window->observe(f->in.frame,f->in.distance); }
    check(window->finished&&window->record.response==Response::approach&&window->record.next_observations==3&&window->record.distances[0]==4&&window->record.distances[3]==1,
        "APPROACH is actual timed static-distance reduction over baseline plus exactly three own observations");
    auto staying=std::make_unique<ResponseWindow>(); f->actor(91).position={9,1}; f->tick(10); staying->begin(f->in.frame,site,91,f->in.distance);
    for (int i=1;i<=3;++i) { f->tick(10+i); staying->observe(f->in.frame,f->in.distance); }
    check(staying->record.response==Response::stay,"STAY requires all three personally visible known paths with no decrease");
    auto returns=std::make_unique<ResponseWindow>(); f->tick(20); returns->begin(f->in.frame,site,91,f->in.distance);
    for (int i=1;i<=3;++i) { f->actor(91).position={i==1?10:9,1}; f->tick(20+i); returns->observe(f->in.frame,f->in.distance); }
    check(returns->record.response==Response::approach,"consecutive reduction is a movement label even when rival merely returns to baseline distance");
    for (int failure=0;failure<8;++failure) {
        auto bad=std::make_unique<ResponseWindow>(); f->actor(91).alive=true; f->actor(91).health=100; f->actor(91).position={9,1}; f->tick(40+10*failure);
        check(bad->begin(f->in.frame,site,91,f->in.distance),"negative sample starts with an actual valid baseline"); f->tick(41+10*failure);
        if (failure==0) f->in.distance.knowledge=p6::Knowledge::unknown;
        if (failure==1) f->in.distance.value.unobstructed=false;
        if (failure==2) f->in.distance.value.shortest_path_known=false;
        if (failure==3) f->in.distance.subject=92;
        if (failure==4) { f->actor(91).alive=false; f->actor(91).health=0; }
        if (failure==5) f->tick(42+10*failure);
        if (failure==6) ++f->in.distance.value.map_identity;
        if (failure==7) f->in.distance.value.distance.source=p6::Source::conservative;
        bad->observe(f->in.frame,f->in.distance);
        check(bad->finished&&bad->record.response==Response::unknown&&bad->record.distances[1]==-1,"visibility path obstruction identity death timing or map uncertainty is UNKNOWN and never a zero-distance/STAY sample");
    }
    auto book=std::make_unique<ResponseBook>();
    for (int pass=0;pass<2;++pass) {
        auto named=site; if (pass) named.target=named.own_approaches[1];
        f->actor(91).alive=true; f->actor(91).health=100; f->actor(91).position={9,1}; f->tick(140+40*pass);
        f->in.distance.value.target=named.target; f->in.distance.value.distance={9-named.target.x,p6::Source::public_map};
        check(book->begin(f->in.frame,named,91,f->in.distance),"each changed-target sample has its own actual valid baseline");
        for (int i=1;i<=3;++i) { f->actor(91).position={9-i,1}; f->tick(140+40*pass+i); f->in.distance.value.target=named.target;
            f->in.distance.value.distance={9-i-named.target.x,p6::Source::public_map}; book->observe(f->in.frame,f->in.distance); }
    }
    check(book->encounters()==2&&book->matching()==1&&!book->approach_current(1,183),"same rival and map with a different static-distance target cannot combine into matching response authority");
}
__attribute__((noinline)) void passive_history_licenses_real_convoy() {
    auto f=std::make_unique<Fixture>(); f->two_approaches(); f->ready(88,1);
    const auto a=decide(f->in,f->protocol,f->ledger,f->probes,Arm::active),b=decide(f->in,f->protocol,f->ledger,f->probes,Arm::control);
    check(a.option==1&&b.option==0,"same legal response history licenses visible active convoy while control keeps best short solo route");
    check(common::evaluate(f->in,f->in.offers[0].schedule).min_margin==52&&common::evaluate(f->in,f->in.offers[1].schedule).min_margin==54&&
        p6::compare(common::evaluate(f->in,f->in.offers[0].schedule),common::evaluate(f->in,f->in.offers[2].schedule))>0,
        "best short-U is included and only actual clearance overlap repays charged escort displacement with unique fixed rewards");
    f->cycle(88,1); check(f->trace.coverage_observations>=5&&f->ledger.state(f->protocol.slot())->positive_completions==1&&
        !f->ledger.organization_qualified(f->protocol.slot(),f->full.key,f->in.frame.clock.round),"one actual continuously visible convoy conversion is not borrowed full qualification");
    f->cycle(132,1); check(f->ledger.organization_qualified(f->protocol.slot(),f->full.key,f->in.frame.clock.round)&&f->ledger.state(f->protocol.slot())->campaign_episodes==2,
        "two separate same-build actual escorted payouts conversions and returns qualify only the real convoy");
    auto survivor=std::make_unique<Fixture>(3); survivor->cycle(88,0); check(survivor->ledger.size()==0&&survivor->work.bank_seen&&survivor->work.renewal_seen,
        "third role actually preserves independent production conversion and renewal without financing convoy labor");
    f->ready(180,1); f->equal_clear=true; f->tick(180);
    check(decide(f->in,f->protocol,f->ledger,f->probes,Arm::active).option==0,"equal critical-clearance scenario rejects costly convoy because short-U has no missing escort benefit");
}
__attribute__((noinline)) void unknown_contradiction_and_expiry_stop_new_work() {
    auto f=std::make_unique<Fixture>(); f->two_approaches(Arm::control); f->pattern=Pattern::stay; f->cycle(80,0,Arm::control); f->ready(120,0);
    check(f->protocol.responses.rule()==Response::stay&&f->protocol.responses.matching()==1&&decide(f->in,f->protocol,f->ledger,f->probes,Arm::active).option==0,
        "contradictory ordinary STAY resets matching authority while retaining accurate control observations");
    auto lost=std::make_unique<Fixture>(); lost->two_approaches(); lost->pattern=Pattern::unknown_path; lost->cycle(88,1);
    check(lost->work.bank_seen&&lost->work.renewal_seen&&lost->in.frame.own.actor.bag==0,"unknown response during live tribute preserves the actual owned conversion and productive return");
    lost->ready(132,0);
    check(lost->protocol.responses.unknowns()==1&&!lost->protocol.responses.approach_current(1,132)&&decide(lost->in,lost->protocol,lost->ledger,lost->probes,Arm::active).option==0,
        "unknown required route premise suspends new convoy rather than converting loss of knowledge into STAY");
    lost->pattern=Pattern::approach; lost->cycle(132,0); lost->cycle(172,0); lost->cycle(216,1);
    check(lost->protocol.responses.unknowns()==1&&lost->ledger.state(lost->protocol.slot())->campaign_episodes==2&&
        lost->ledger.organization_qualified(lost->protocol.slot(),lost->full.key,lost->in.frame.clock.round)&&lost->probes.used==0,
        "two fresh ordinary encounters reopen admission without erasing UNKNOWN history or replenishing the two real organization starts");
    auto expired=std::make_unique<Fixture>(); expired->two_approaches(); const auto records=expired->protocol.responses.encounters(); expired->ready(121,0);
    check(!expired->protocol.responses.approach_current(1,121)&&decide(expired->in,expired->protocol,expired->ledger,expired->probes,Arm::active).option==0&&expired->protocol.responses.encounters()==records,
        "64-round expiry stops new convoy without manufacturing an encounter or resetting history");
    auto live_expiry=std::make_unique<Fixture>(); live_expiry->two_approaches(); live_expiry->cycle(108,1);
    check(live_expiry->work.stop_requested&&live_expiry->work.bank_seen&&live_expiry->work.renewal_seen&&
        live_expiry->protocol.responses.matching()==1&&live_expiry->in.frame.own.actor.bag==0,
        "response expiry at own bank approach stops further commitment while actual safe tribute and renewal finish; a lone fresh encounter is insufficient");
    auto staying=std::make_unique<Fixture>(); staying->pattern=Pattern::stay; staying->cycle(0,0); staying->cycle(40,0); staying->ready(88,0);
    check(staying->protocol.responses.matching()==2&&staying->protocol.responses.rule()==Response::stay&&decide(staying->in,staying->protocol,staying->ledger,staying->probes,Arm::active).option==0,"two actual STAY encounters retain U");
    auto absent=std::make_unique<Fixture>(); absent->pattern=Pattern::none; absent->cycle(0,0); absent->ready(40,0);
    check(absent->protocol.responses.encounters()==0&&decide(absent->in,absent->protocol,absent->ledger,absent->probes,Arm::active).option==0,"no observed same-rival encounter remains opportunity-blocked with actual first income intact");
    auto missing=std::make_unique<Fixture>(); missing->two_approaches(); missing->cycle(88,1,Arm::active,true);
    check(missing->ledger.state(missing->protocol.slot())->positive_completions==0&&missing->ledger.lifetime_failure_count()>0&&missing->work.bank_seen&&missing->work.renewal_seen,
        "clearance support without actual escort through bank cannot qualify convoy even while owner's retained bag safely converts");
}
__attribute__((noinline)) void actual_helper_escort_then_own_income() {
    auto f=std::make_unique<Fixture>(2); f->in.offer_count=1; f->cycle(0,0); f->in.offer_count=3;
    f->ready(88,1); f->in.visible_proposer.knowledge=p6::Knowledge::unknown;
    check(decide(f->in,f->protocol,f->ledger,f->probes,Arm::active).option!=1,"helper cannot read leader response history or adopt an unseen proposal");
    auto loaded=std::make_unique<Fixture>(2); loaded->ready(88,1); loaded->in.frame.own.actor.bag=10; loaded->tick(88);
    check(decide(loaded->in,loaded->protocol,loaded->ledger,loaded->probes,Arm::active).option!=1&&loaded->in.frame.own.actor.bag==10,"existing owned bag must be banked before accepting this empty-entry convoy role");
    f->begin(88,1); auto& collector=f->actor(1);
    f->step(0,{0,2}); f->step(1,{1,2}); check(f->step(2,{2,1}).action==PhysicalAction::attack,"escort first performs actual useful camp work");
    f->source(7,p6::Lifecycle::reward_available); collector.position={2,0}; f->step(3,{2,1}); collector.position={3,0}; collector.spawn_group=7;
    f->step(4,{3,1}); f->step(5,{4,2});
    for (int t=6;t<=15;++t) {
        collector.position=t<12?p6::Point{3,0}:t==12?p6::Point{3,1}:p6::Point{4,1}; collector.spawn_group=t<12?7:-1;
        collector.bag=t>=11&&t<15?12:0; collector.channel=t==13||t==14?p6::Channel::tribute:p6::Channel::none; collector.channel_valid=true;
        const auto command=f->step(t,{4,2});
        if (t<15) check(f->work.active&&command.action==PhysicalAction::inspect&&f->protocol.helper.phase==HelperPhase::escort,"helper actually remains at visible escort pad through capture bank approach and live tribute instead of leaving to U");
    }
    check(!f->work.active&&f->protocol.helper.phase==HelperPhase::own_income&&f->protocol.helper.saw_positive_bag&&f->protocol.helper.escorted==1,
        "only actual visible emptied collector inventory ends bounded escort without claiming a peer tribute receipt");
    f->cycle(104,2); check(f->protocol.helper.own_incomes==1&&f->protocol.helper.phase==HelperPhase::independent&&f->work.payout_seen&&f->work.bank_seen&&f->work.renewal_seen&&f->ledger.size()==0,
        "released helper actually executes retained own10-gem productive route banks and renews independently after convoy");
}
}
int main() {
    exact_finite_response_samples(); passive_history_licenses_real_convoy(); unknown_contradiction_and_expiry_stop_new_work(); actual_helper_escort_then_own_income();
    std::printf("plan8 finite counterprotocol: %d checks passed\n",checks); return 0;
}
