#include "../circuitbreaker/src/policy.h"
#include "../circuitbreaker/src/draft.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>
using namespace p8;
namespace cb=p8::circuitbreaker;
namespace {
int checks=0;
void check(bool yes,const char* message){++checks;if(!yes){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
template<class T>p6::Fact<T> fact(T value){return {value,p6::Source::synthetic_fixture};}
Route path(std::initializer_list<p6::Point> points){Route r;r.verified=true;for(auto p:points)r.tiles[r.count++]=p;return r;}
__attribute__((noinline)) Plan plan(int self=1,bool joint=false,bool changed=false) {
    const int y=self-1;Plan p;p.organization=joint?Organization::J:Organization::U;p.group=joint?7:9;
    p.collector=joint?1:self;p.altar=9;p.crew[0]=self;p.crew_count=1;
    p.ingress=path({{0,0},{1,0},{2,y}});p.capture=path({{2,y},{3,y}});
    p.bank=path({{3,y},{4,y},{5,y},{6,y},{7,y},{8,y}});p.renewal=path({{8,y},{9,y},{10,y}});
    if(joint) {
        p.crew={1,2,3,-1,-1,-1};p.crew_count=3;p.pads[0]={2,0};p.pads[1]={2,1};p.pads[2]={2,2};
        // y=2 needs a connected retained route as well.
        p.ingress=y==2?path({{0,0},{1,1},{2,2}}):path({{0,0},{1,0},{2,y}});
        if(changed)p.ingress=y==2?path({{0,0},{0,1},{1,2},{2,2}}):path({{0,0},{1,1},{2,y}});
        p.rally_legal=true;
    }
    if(!joint&&y==2)p.ingress=path({{0,0},{1,1},{2,2}});
    if(!joint) {
        // A distinct source, separated from J's x=3 source by empty x=4.
        p.ingress=path({{0,0},{1,std::min(1,y)},{2,y},{3,y},{4,y}});
        p.capture=path({{4,y},{5,y}});p.bank=path({{5,y},{6,y},{7,y},{8,y},{9,y},{10,y}});
        p.renewal=path({{10,y},{11,y},{12,y}});
    }
    p.forecast_rounds=20;p.remaining_rounds=500;p.capture_window=5;p.gem_bearing=p.complete_verified=true;return p;
}
void event(p6::ActorSchedule& a,p6::EventKind kind,int id,int tick,int production=-1) {
    a.events[a.event_count++]={kind,id,tick,production,fact<p6::Gems>(0),
        kind==p6::EventKind::renewal?90+a.actor_id:-1,p6::Source::synthetic_fixture};
}
__attribute__((noinline)) p6::Schedule schedule(const p6::Frame& f,bool joint,int reward=90) {
    p6::Schedule s;s.target_id=joint?7:9;s.actor_count=3;
    for(int id=1;id<=3;++id) {
        auto& a=s.actors[id-1];const auto* seen=f.visible(id);a.actor_id=id;
        a.initial_bag={seen?seen->bag:0,id==f.clock.self?p6::Source::self_observed:p6::Source::visible_subject};
        a.initial_gold={id==f.clock.self?f.own.gold:0,id==f.clock.self?p6::Source::self_observed:p6::Source::conservative};
        a.occupied_turns=fact<p6::Gems>(20);
        const int index=joint?0:s.production_count++;
        if(!joint||id==1) {
            auto& p=s.productions[index];p.event_id=100+id;p.tick=joint?10:12;
            p.gems=fact<p6::Gems>(joint?reward:10);p.gold=fact<p6::Gems>(0);p.source=p6::Source::synthetic_fixture;
            p.holders[0]=id;p.holder_count=joint?3:1;if(joint)p.holders={1,2,3};
        }
        event(a,p6::EventKind::production,s.productions[index].event_id,joint?10:12,index);
        event(a,p6::EventKind::tribute,200+id,18);event(a,p6::EventKind::renewal,300+id,20);
    }
    if(joint)s.production_count=1;
    return s;
}
p6::CycleCertificate safe(const p6::Frame& f,int id,int group=7) {
    p6::CycleCertificate c;c.clock=f.clock;c.actor=id;c.group=group;const auto* a=f.visible(id);
    if(a)c.clear=c.hold=c.bank=c.recovery={a->health,a->max_health,10,0,0,0,true};
    c.route_known=c.bank_executable=c.renewal_reachable=c.solo=true;return c;
}
__attribute__((noinline)) ContextDeclaration declaration(const Plan& p,int self,p6::Champion champion) {
    ContextDeclaration d;auto& k=d.key;k.settings_id=17;k.route_hash=plan_route_hash(p);k.return_route_hash=plan_return_hash(p);
    k.actor_id=self;k.champion=int(champion);k.role=p.organization==Organization::J?1:0;
    k.source_group=p.group;k.route_template=0;k.endpoint=9;k.threat_regime=0;k.build={10,100,20,10,1};
    k.participants[0]=self;k.participant_count=1;
    d.envelope={{std::nextafter(0.,1.),100},{100,100},{20,20},{10,10},{1,1},{0,100},0};
    d.require_spawn_shop=true;
    // A priori bounded four-turn scheduling uncertainty. These overlapping
    // predictions allow two genuine ordinary contradictions without forcing
    // the entire family empty after the first nominal mismatch.
    for(int i=0;i<3;++i)d.hypotheses[i].offset={i*2,4+i*2};
    if(p.organization==Organization::J) {
        k.participants={1,2,3,-1,-1,-1};k.participant_count=3;d.kind=EpisodeKind::full_organization;
        d.require_supported_work=true;
    }
    return d;
}
struct Fixture {
    std::unique_ptr<cb::Inputs> in=std::make_unique<cb::Inputs>();
    std::unique_ptr<cb::Circuit> circuit=std::make_unique<cb::Circuit>();
    std::unique_ptr<EvidenceLedger> ledger;
    LocalFrame local{};Work work{};cb::JTrace trace{};
    int receipt_serial=0;bool old_available=true;int joint_reward=90;
    explicit Fixture(int self=1):ledger(std::make_unique<EvidenceLedger>(self)) {
        auto& f=in->frame;f.clock={0,self,0};f.own.gold=20;f.own.at_spawn=true;
        in->draft.picks=in->picks=3;in->draft.actors={1,2,3,-1,-1};
        in->draft.champions={p6::Champion::varg,p6::Champion::willow,p6::Champion::marcella,p6::Champion::other,p6::Champion::other};
        for(int id=1;id<=3;++id) {
            p6::Actor a;a.id=id;a.pick_ordinal=id-1;a.type=in->draft.champions[id-1];a.team=p6::Team::ally;
            a.alive=a.can_move=a.can_attack=true;a.health=a.max_health=100;a.strength=20;
            a.position=id==self?p6::Point{0,0}:p6::Point{2,id-1};
            if(id==self)f.own.actor=a;else f.actors[f.actor_count++]={a,f.clock,id,p6::Knowledge::exact};
        }
        p6::Group g;g.id=7;g.original_spawn_visible=g.all_spawns_visible=g.uncontested_now=true;g.lifecycle=p6::Lifecycle::living;
        f.group={g,f.clock,7,p6::Knowledge::exact};in->settings={2000,1000,5};in->settings_identity={17,p6::Source::queried_setting};
        in->target_group=7;in->own_state={{100,100,20,10,1,0,10},f.clock,self,p6::Knowledge::exact};
        const auto p=std::make_unique<cb::Package>();p->bootstrap=plan(self);
        p->ordinary=declaration(p->bootstrap,self,in->draft.champions[self-1]);
        for(int i=0;i<2;++i) {
            auto& s=p->systems[i];s.plan=plan(self,true,i==1);
            s.declaration=declaration(s.plan,self,in->draft.champions[self-1]);
            for(int j=0;j<3;++j)s.members[j]={j+1,in->draft.champions[j],{10,100,20,10,1},s.declaration.envelope};
        }
        check(circuit->declare(*p,in->draft,*ledger),"three bounded contexts declare before any episode");tick(0);
    }
    p6::Actor& actor(int id) {
        if(id==in->frame.clock.self)return in->frame.own.actor;
        for(int i=0;i<in->frame.actor_count;++i)if(in->frame.actors[i].subject==id)return in->frame.actors[i].value;
        std::abort();
    }
    void tick(int round) {
        auto& f=in->frame;f.clock.round=round;f.group.clock=f.clock;
        for(int i=0;i<f.actor_count;++i)f.actors[i].clock=f.clock;
        const int self=f.clock.self;const auto& own=f.own.actor;
        in->own_state.clock=f.clock;in->own_state.value.health=own.health;in->own_state.value.max_health=own.max_health;
        in->own_state.value.strength=own.strength;in->own_state.value.bag=own.bag;
        for(int id=1;id<=3;++id) {
            const auto& a=actor(id);in->member_states[id-1]={{a.health,a.max_health,a.strength,10,1,a.bag,10},f.clock,id,p6::Knowledge::exact};
        }
        in->threat_regime={0,f.clock,self,p6::Knowledge::exact};in->spawn_shop_legal={f.own.at_spawn,f.clock,self,p6::Knowledge::exact};
        in->visible_proposer={7,f.clock,1,p6::Knowledge::exact};
        in->windows.deadlines=in->windows.windows={120,120,120};in->windows.sampled_actor_id=self;in->windows.sampled_round=round;
        in->windows.source=p6::Source::private_history;in->windows.valid=true;in->independent=0;in->offer_count=3;
        for(int i=0;i<3;++i) {
            auto& o=in->offers[i];o={};o.plan=i?circuit->package.systems[i-1].plan:circuit->package.bootstrap;
            if(i==1&&!old_available)o.plan.complete_verified=false;
            o.context_slot=i?circuit->systems[i-1].slot:circuit->ordinary;
            o.entry=i?circuit->package.systems[i-1].declaration.key:circuit->package.ordinary.key;
            o.schedule=schedule(f,i!=0,joint_reward);o.safety_count=3;
            for(int id=1;id<=3;++id)o.safety[id-1]=safe(f,id,i?7:id==self?9:9+id);
        }
        cb::bind(*in,*circuit);physical(work.active?work.plan:circuit->package.bootstrap);
    }
    void physical(const Plan& p) {
        const auto& f=in->frame;local.frame=f;local.current_round=f.clock.round;local.safety_count=3;
        for(int id=1;id<=3;++id)local.safety[id-1]=safe(f,id,p.group);
        local.legal={{},f.clock,f.clock.self,p6::Knowledge::exact};auto& l=local.legal.value;
        l.safe_now=l.attack_legal=l.altar_legal=l.recall_safe=l.renewal_productive=true;
        for(Leg leg:{Leg::ingress,Leg::capture,Leg::bank,Leg::renewal}) {
            auto& a=l.routes[int(leg)];const auto& r=route_for(p,leg);a.assumptions_hold=a.next_move_legal=true;
            a.next_tile=r.tiles[std::min(1,r.count-1)];
            for(int i=0;i<r.count;++i)if(r.tiles[i]==f.own.actor.position)a.next_tile=r.tiles[std::min(i+1,r.count-1)];
        }
    }
    std::uint32_t issue(int which,int start) {
        tick(start);const int slot=which<0?circuit->ordinary:circuit->systems[which].slot;
        const auto* s=ledger->state(slot);check(s!=nullptr,"predeclared evidence slot exists");
        if(which>=0&&!s->campaign_started)check(ledger->begin_initial_campaign(slot,start,
            {{s->declaration.key,in->own_state.value,all_premises},true,true,true}),"visible funded safe initial commissioning grants no proof");
        EpisodeRequest r;r.context=slot;r.issue_round=start;r.deadline=start+100;r.action=which<0?0:3;r.nominal_duration=20;
        r.premises={s->declaration.key,in->own_state.value,all_premises};const auto serial=ledger->issue(r);
        check(serial!=0,"each oracle cycle issues once before its own work");return serial;
    }
    Resolution complete(int which,int start,Interval duration={22,24},bool supported=true,bool bank=true) {
        const auto serial=issue(which,start);Observation o;o.round=start+duration.upper;o.kind=ObservationKind::completed;
        o.premises=ledger->active()->premises;o.duration={duration,duration.lower==duration.upper?
            DurationSource::personally_exact:DurationSource::legally_bracketed};
        auto& r=o.receipts;r.source=ReceiptSource::own_observation;
        r.production=r.payout_owned=r.conversion=r.renewed=r.attribution_excludes_other_bag_loss=true;
        r.conversion=bank;r.payout_event=r.conversion_event=r.renewal_event=++receipt_serial;
        r.production_round=start+1;r.payout_round=start+5;r.conversion_round=start+10;
        r.spawn_visited=r.spawn_shop_legal=true;r.spawn_round=start+15;r.renewal_round=o.round;
        r.supported_roles_witnessed=supported;r.positive_reward_lower=10;
        const auto result=ledger->resolve(serial,o);check(result.accepted,"own terminal oracle resolves exactly once");
        trace.next_receipt=std::max(trace.next_receipt,receipt_serial);
        circuit->synchronize(*ledger);tick(o.round);return result;
    }
    void bootstrap() {check(complete(-1,1).full_positive,"own productive U payout, bank, spawn and renewal bootstrap J");tick(26);}
    void qualified() {
        bootstrap();check(complete(0,26).full_positive,"first actual matched full J is supported");
        check(!circuit->fresh_full(*ledger,0,50),"one complete J never qualifies full mechanism");
        check(complete(0,51).full_positive,"second distinct matched full J supports full mechanism");tick(76);
        check(circuit->fresh_full(*ledger,0,76),"two actual complete J cycles qualify only current exact subsystem");
    }
    void change(int round) {
        old_available=false;tick(round);cb::ContextChange changed;
        changed.previous=circuit->package.systems[0].declaration.key;changed.current=circuit->package.systems[1].declaration.key;
        changed.kind=cb::ChangeKind::route;changed.inspected_route=changed.current.route_hash;
        in->actual_change={changed,in->frame.clock,in->frame.clock.self,p6::Knowledge::exact};
    }
};
void selection_and_campaigns() {
    for(int self=1;self<=3;++self) {
        auto f=std::make_unique<Fixture>(self);if(self!=1){f->actor(1).position={2,0};f->tick(0);}
        const auto initial=cb::decide(*f->in,*f->circuit,*f->ledger,cb::Arm::active);
        check(initial.option==0&&initial.record_episode,"every core actor has a verified own first-income U path");
        check(cb::commit(*f->in,f->local,initial,*f->circuit,*f->ledger,cb::Arm::active,f->work,f->trace).started,
            "bootstrap is actual physical own Work, not plan metadata");
        const auto move=cb::advance(*f->in,f->local,*f->circuit,*f->ledger,cb::Arm::active,f->work);
        check(move.action==PhysicalAction::move,"bounded bootstrap dispatch walks the retained route");
    }
    auto f=std::make_unique<Fixture>();f->bootstrap();const auto d=cb::decide(*f->in,*f->circuit,*f->ledger,cb::Arm::active);
    check(d.option==1&&d.record_episode&&!d.deliberate_probe,"best ordinary shared circuit receives initial J trial");
    f->physical(f->circuit->package.systems[0].plan);
    const auto admission=cb::commit(*f->in,f->local,d,*f->circuit,*f->ledger,cb::Arm::active,f->work,f->trace);
    check(admission.started&&admission.episode&&f->trace.active&&f->circuit->systems[0].trials==1,
        "initial J issues and spends one lifetime trial before departure");
    check(!f->ledger->organization_qualified(f->circuit->systems[0].slot,f->in->offers[1].entry,26),"commissioning starts unqualified");
    auto stale=d;stale.clock.round=25;
    check(!cb::commit(*f->in,f->local,stale,*f->circuit,*f->ledger,cb::Arm::active,f->work,f->trace).started,
        "stale or already active proposal cannot duplicate physical work or trial charge");
    auto low=std::make_unique<Fixture>();low->bootstrap();low->joint_reward=9;low->tick(26);
    check(cb::decide(*low->in,*low->circuit,*low->ledger,cb::Arm::active).option==0,
        "initial commissioning never adds information bonus to an inferior J schedule");
    low->joint_reward=90;low->tick(27);low->in->offers[1].marked_extra_turns=1;
    check(cb::decide(*low->in,*low->circuit,*low->ledger,cb::Arm::active).option==0,
        "an unrepresented diagnostic segment cannot masquerade as ordinary commissioning");
    check(low->circuit->probes.used==0,"rejected diagnostics do not invent or replenish probe slots");
    low->tick(28);low->in->offers[1].safety[1].clear.incoming_total=1000;
    low->in->offers[2].safety[1].clear.incoming_total=1000;
    check(cb::decide(*low->in,*low->circuit,*low->ledger,cb::Arm::control).option==0,
        "control obeys identical individual safety for every J member");
}
void retirement_and_private_lag() {
    auto f=std::make_unique<Fixture>();f->qualified();
    check(f->complete(0,76,{26,26}).contradiction,"first nominal contradiction retains a distinct admissible explanation");
    check(!f->circuit->systems[0].retired,"one non-safety mismatch does not retire a qualified subsystem");
    check(f->complete(0,103,{24,26}).full_positive,"a bracket straddling nominal boundary is a noncontradictory eligible cycle");
    check(f->complete(0,130,{26,26}).contradiction,"second nominal contradiction remains mechanically supported");
    check(f->circuit->systems[0].retired&&f->circuit->systems[0].reason==cb::Retirement::contradictions,
        "two of last three eligible contradictions retire the subsystem across project epochs");
    f->tick(157);const auto active=cb::decide(*f->in,*f->circuit,*f->ledger,cb::Arm::active);
    const auto control=cb::decide(*f->in,*f->circuit,*f->ledger,cb::Arm::control);
    check(active.option==0&&!active.record_episode&&control.option==1&&!control.record_episode,
        "active returns to productive U while accurate control repairs the same feasible J");
    f->tick(256);check(cb::decide(*f->in,*f->circuit,*f->ledger,cb::Arm::active).option==0,
        "new epoch does not erase latched subsystem retirement");
    f->in->frame.own.actor.alive=false;f->tick(257);
    cb::advance(*f->in,f->local,*f->circuit,*f->ledger,cb::Arm::active,f->work);
    f->in->frame.own.actor.alive=true;f->tick(300);
    check(f->circuit->systems[0].retired&&f->circuit->systems[0].trials==2,
        "death and return retain retirement and spent commissioning starts");
    auto remote=std::make_unique<Fixture>(2);remote->qualified();remote->actor(1).position={2,0};remote->tick(157);
    check(cb::decide(*remote->in,*remote->circuit,*remote->ledger,cb::Arm::active).option==1,
        "another actual member has its own still-current proof and does not instantly learn retirement");
    remote->actor(1).position={0,0};remote->tick(158);
    check(cb::decide(*remote->in,*remote->circuit,*remote->ledger,cb::Arm::active).option==0,
        "remote lag ends only when that actor personally sees the named proposer leave the rally");
    check(!remote->circuit->systems[0].retired,"observed missing willingness is not a copied peer failure ledger");
}
void failure_and_freshness() {
    for(bool safety_failure:{false,true}) {
        auto f=std::make_unique<Fixture>();f->bootstrap();const auto serial=f->issue(0,26);
        Observation o;o.round=27;o.premises=f->ledger->active()->premises;
        o.kind=safety_failure?ObservationKind::safety_failure:ObservationKind::completed;
        o.duration={{99,99},DurationSource::personally_exact};const auto result=f->ledger->resolve(serial,o);
        f->circuit->synchronize(*f->ledger);
        check(result.accepted&&f->circuit->systems[0].retired,"one observed safety or empty-model failure retires immediately");
        check(f->circuit->systems[0].reason==(safety_failure?cb::Retirement::safety:cb::Retirement::empty_model),
            "retirement preserves the applicable distinct safety/model failure reason");
    }
    auto unknown=std::make_unique<Fixture>();unknown->qualified();const auto serial=unknown->issue(0,76);
    Observation unavailable;unavailable.round=77;unavailable.kind=ObservationKind::unavailable;
    unavailable.premises=unknown->ledger->active()->premises;unavailable.premises.personally_observed&=~route_known;
    const auto r=unknown->ledger->resolve(serial,unavailable);unknown->circuit->synchronize(*unknown->ledger);
    check(!r.eligible&&!r.contradiction&&!unknown->circuit->systems[0].retired,
        "unknown route and unavailable outcome are neither zero duration nor an eligible contradiction");
    auto f=std::make_unique<Fixture>();f->qualified();const int latest=f->ledger->state(f->circuit->systems[0].slot)->latest_positive;
    check(f->circuit->fresh_full(*f->ledger,0,latest+255)&&!f->circuit->fresh_full(*f->ledger,0,latest+256),
        "matched full J status expires exactly at the strict 256-round boundary");
    f->tick(latest+250);
    check(cb::decide(*f->in,*f->circuit,*f->ledger,cb::Arm::active).option==0,
        "a proposed whole cycle cannot promise a status that expires before worst completion");
    f->tick(latest+256);check(cb::decide(*f->in,*f->circuit,*f->ledger,cb::Arm::control).option==1,
        "control may still execute a physically feasible ordinary repair without invented qualification");
    auto peer=std::make_unique<Fixture>();peer->qualified();peer->tick(77);peer->in->member_states[1].value.equipment_hash=99;
    check(cb::decide(*peer->in,*peer->circuit,*peer->ledger,cb::Arm::active).option==0&&peer->circuit->systems[0].material_changed,
        "actual visible peer rebuild invalidates privately stored full J premises");
    peer->tick(78);check(cb::decide(*peer->in,*peer->circuit,*peer->ledger,cb::Arm::active).option==0,
        "returning to former gear cannot resurrect already invalidated subsystem proof");
    auto unseen=std::make_unique<Fixture>();unseen->qualified();unseen->tick(77);
    unseen->in->member_states[1].knowledge=p6::Knowledge::unknown;
    check(cb::decide(*unseen->in,*unseen->circuit,*unseen->ledger,cb::Arm::active).option==0&&
        !unseen->circuit->systems[0].retired&&!unseen->circuit->systems[0].material_changed,
        "missing personally observed peer stats remove J without inventing a material change or contradiction");
}
void requalification() {
    auto f=std::make_unique<Fixture>();f->qualified();f->circuit->systems[0].retire(cb::Retirement::safety,76);f->change(77);
    f->circuit->probes.spend(0);f->circuit->probes.spend(1);
    f->in->actual_change.value.inspected_route=0;
    check(!cb::authorize_requalification(*f->in,f->local,f->work,*f->circuit,*f->ledger,cb::Arm::active),
        "a hypothetical new route, epoch or label cannot restore assembly rights");
    f->change(77);f->in->frame.own.actor.bag=2;f->tick(77);
    f->in->actual_change.clock=f->in->frame.clock;
    check(!cb::authorize_requalification(*f->in,f->local,f->work,*f->circuit,*f->ledger,cb::Arm::active),
        "requalification cannot put an existing loaded bag at risk");
    f->in->frame.own.actor.bag=0;f->change(78);
    check(cb::authorize_requalification(*f->in,f->local,f->work,*f->circuit,*f->ledger,cb::Arm::active),
        "one actual inspected changed route opens one empty-bag ordinary requalification campaign");
    check(f->circuit->selected==1&&f->circuit->requalification_used&&f->circuit->systems[0].retired&&
        !f->circuit->fresh_full(*f->ledger,1,78)&&f->circuit->probes.used==2,
        "new context starts unqualified while old retirement, trials and lifetime probe charges remain");
    check(f->complete(1,79).full_positive&&!f->circuit->fresh_full(*f->ledger,1,103),
        "first real changed-context cycle grants no full readmission");
    check(f->complete(1,104).full_positive&&f->circuit->fresh_full(*f->ledger,1,128),
        "two fresh distinct full changed-context cycles permit readmission");
    check(f->circuit->systems[0].trials==2&&f->circuit->systems[1].trials==2,
        "each commissioning campaign is bounded at two starts without replenishing total trials");
    f->change(129);check(!cb::authorize_requalification(*f->in,f->local,f->work,*f->circuit,*f->ledger,cb::Arm::active),
        "a second named real change cannot acquire another requalification campaign");
    auto failed=std::make_unique<Fixture>();failed->bootstrap();failed->complete(0,26,{22,24},false);
    failed->tick(51);check(cb::decide(*failed->in,*failed->circuit,*failed->ledger,cb::Arm::active).option==0,
        "failed initial commissioning remains spent and needs a real context change");
    check(cb::decide(*failed->in,*failed->circuit,*failed->ledger,cb::Arm::control).option==1,
        "control continues identical feasible J repairs after ordinary commissioning failure");
}
// Synthetic personally observed actions/positions model a full owner timeline.
// Thirty actual carried gems require three tribute ticks, including its start.
// J shares its fixed 90-gem source once; each independent circuit produces10.
// Those are distinct sites, rather than a reward bonus for changing template.
__attribute__((noinline)) Resolution physical_cycle(Fixture& f,int start,bool joint=true,
        bool peer_attack=true,bool own_bank=true,int retire_at=-1,bool early_departure=false,cb::Arm arm=cb::Arm::active) {
    auto& in=*f.in;const int self=in.frame.clock.self;
    check(!f.work.active&&!f.ledger->active(),"a new physical job starts only after the preceding local job and episode close");
    // Keep the persistent Work generation/serial as well as private evidence.
    // Starting a distinct inactive job rebases only its owner-step clock; a
    // gap inside the active job must still invalidate its physical witness.
    for(int id=1;id<=3;++id) {
        auto& a=f.actor(id);a.position=id==self?p6::Point{0,0}:p6::Point{2,id-1};
        a.spawn_group=-1;a.action=p6::WitnessAction::none;a.channel=p6::Channel::none;a.channel_valid=false;a.bag=0;
    }
    in.frame.group.value.lifecycle=p6::Lifecycle::living;in.frame.own.at_spawn=true;f.tick(start);
    const auto selected=cb::decide(in,*f.circuit,*f.ledger,arm);
    check(selected.option==(joint?1:0),"physical oracle selects the intended actual U/J organization");
    f.physical(joint?f.circuit->package.systems[0].plan:f.circuit->package.bootstrap);
    const auto admission=cb::commit(in,f.local,selected,*f.circuit,*f.ledger,arm,f.work,f.trace);
    check(admission.started&&(!joint||admission.episode),"physical oracle binds its own newly started Work before actions");
    Resolution terminal;
    const int extra_ingress=joint?0:2,payout=10+extra_ingress,tribute=15+extra_ingress;
    const int bank_end=18,finish=20;
    for(int t=0;t<=finish;++t) {
        auto& a=in.frame.own.actor;const auto& p=f.work.plan;
        if(t<2+extra_ingress)a.position=p.ingress.tiles[t];
        else if(t<=4+extra_ingress)a.position=p.ingress.tiles[p.ingress.count-1];
        else if(t<=payout)a.position=p.capture.tiles[p.capture.count-1];
        else if(t<=tribute)a.position=p.bank.tiles[t-payout];
        else if(t<=bank_end)a.position=p.bank.tiles[p.bank.count-1];
        else a.position=p.renewal.tiles[t-bank_end];
        a.spawn_group=t>=5+extra_ingress&&t<=payout?p.group:-1;a.bag=t>=payout&&t<bank_end?(joint?30:10):0;
        a.channel=t>tribute&&t<bank_end?p6::Channel::tribute:p6::Channel::none;a.channel_valid=t>tribute&&t<bank_end;
        in.frame.own.at_spawn=t==bank_end;in.frame.group.subject=in.frame.group.value.id=p.group;
        in.frame.group.value.lifecycle=t<4+extra_ingress?p6::Lifecycle::living:t<payout?p6::Lifecycle::reward_available:p6::Lifecycle::respawning;
        for(int id=1;id<=3;++id)if(id!=self) {
            auto& peer=f.actor(id);peer.action=peer_attack&&(t==2||t==3)?p6::WitnessAction::useful_attack:p6::WitnessAction::none;
            peer.action_group=p.group;peer.spawn_group=t>=5&&t<=10?p.group:-1;
            peer.position=t<5?p6::Point{2,id-1}:p6::Point{a.position.x,id-1};
            peer.bag=t>=10&&t<18?30:0;peer.channel=a.channel;peer.channel_valid=a.channel_valid;
            if(early_departure&&id!=(self==1?3:1)&&t==9){peer.spawn_group=-1;peer.position={4,id-1};}
        }
        f.tick(start+t);f.physical(p);f.local.milestones={};
        if(t==payout)f.local.milestones={{p.group,true,false,10,0},in.frame.clock,self,p6::Knowledge::exact};
        if(t==bank_end)f.local.milestones={{p.group,false,own_bank,0,10},in.frame.clock,self,p6::Knowledge::exact};
        if(t==retire_at)f.circuit->systems[0].retire(cb::Retirement::safety,start+t);
        const auto command=cb::advance(in,f.local,*f.circuit,*f.ledger,arm,f.work);
        if(command.action==PhysicalAction::move||command.action==PhysicalAction::attack||command.action==PhysicalAction::tribute) {
            const ActionReceipt receipt{command.clock,command.clock,command.token,command.action,true,true};
            check(f.work.confirm(command,receipt),"actual same-clock action receipt confirms only this Work command");
        }
        if(t==5+extra_ingress) {
            if(command.action!=PhysicalAction::hold)std::fprintf(stderr,
                "physical source hold: self=%d J=%d t=%d action=%d stage=%d failure=%d active=%d stop=%d own=(%d,%d) spawn=%d group=%d lifecycle=%d cursor=%d\n",
                self,int(joint),t,int(command.action),int(f.work.stage),int(f.work.failure),int(f.work.active),int(f.work.stop_requested),
                a.position.x,a.position.y,a.spawn_group,p.group,int(in.frame.group.value.lifecycle),f.work.cursor[int(Leg::capture)]);
            check(command.action==PhysicalAction::hold,"safe own source capture remains an actual hold after retirement");
        }
        if(t>tribute&&t<bank_end)check(command.action==PhysicalAction::preserve_channel,
            "a safe actual tribute channel takes precedence over local subsystem retirement");
        const auto result=cb::observe_cycle(in,f.local,*f.circuit,*f.ledger,f.work,f.trace);
        if(result.accepted)terminal=result;
    }
    return terminal;
}
void own_physical_traces() {
    auto control=std::make_unique<Fixture>();physical_cycle(*control,1,false,true,true,-1,false,cb::Arm::control);
    check(control->circuit->bootstrapped&&control->ledger->state(control->circuit->ordinary)->attempts==0,
        "control establishes actual U first income without fabricating a qualification episode");
    control->tick(22);check(cb::decide(*control->in,*control->circuit,*control->ledger,cb::Arm::control).option==1,
        "control then permits actual same-menu J commissioning after its own physical bootstrap");
    for(int self:{1,2,3}) {
        auto f=std::make_unique<Fixture>(self);f->bootstrap();
        const auto result=physical_cycle(*f,26);
        check(result.full_positive&&f->work.cycle_completed&&f->work.bank_seen&&f->work.renewal_seen,
            "each J role establishes its OWN actual payout, conversion, shop and productive return");
        check(f->trace.owner==self&&f->trace.plan.collector==1&&f->trace.payout_lower==10,
            "nonleader member keeps its actual collector identity and own positive lower bound");
        check(!f->circuit->fresh_full(*f->ledger,0,46),"one physical J cycle never borrows bootstrap as a second full success");
    }
    auto no_contribution=std::make_unique<Fixture>();no_contribution->bootstrap();
    check(!physical_cycle(*no_contribution,26,true,false).full_positive,
        "visible capable partners without witnessed actual contribution cannot qualify J");
    auto public_score=std::make_unique<Fixture>();public_score->bootstrap();public_score->in->frame.own_team_score=999;
    check(!physical_cycle(*public_score,26,true,true,false).full_positive,
        "public team score and an emptied own bag cannot replace an attributed own bank endpoint");
    auto departed=std::make_unique<Fixture>();departed->bootstrap();
    check(!physical_cycle(*departed,26,true,true,true,-1,true).full_positive,
        "a member leaving capture before own payout cannot become a persistent J completion");
    auto stopped=std::make_unique<Fixture>();stopped->bootstrap();
    const auto completed=physical_cycle(*stopped,26,true,true,true,5);
    check(completed.full_positive&&stopped->circuit->systems[0].retired&&stopped->work.bank_seen&&stopped->work.renewal_seen,
        "retirement during a safe capture preserves its real bag, bank channel and productive renewal without restoring J");
    stopped->tick(47);check(cb::decide(*stopped->in,*stopped->circuit,*stopped->ledger,cb::Arm::active).option==0,
        "finishing the safe former J job cannot undo latched retirement");
    // Actually recall from J's productive endpoint to spawn; there is no free
    // repositioning between the retired job and the new independent route.
    auto recall=cb::advance(*stopped->in,stopped->local,*stopped->circuit,*stopped->ledger,cb::Arm::active,stopped->work);
    check(recall.action==PhysicalAction::recall&&stopped->work.confirm(recall,
        {recall.clock,recall.clock,recall.token,recall.action,true,true}),"retired owner starts a real safe recall from completed work");
    for(int r=48;r<52;++r) {
        stopped->in->frame.own.actor.channel=p6::Channel::recall;stopped->in->frame.own.actor.channel_valid=true;stopped->tick(r);
        check(cb::advance(*stopped->in,stopped->local,*stopped->circuit,*stopped->ledger,cb::Arm::active,stopped->work).action==PhysicalAction::preserve_channel,
            "safe recall consumes its actual remaining ticks before an independent departure");
    }
    stopped->in->frame.own.actor.channel=p6::Channel::none;stopped->in->frame.own.actor.channel_valid=false;
    stopped->in->frame.own.actor.position={0,0};stopped->in->frame.own.at_spawn=true;stopped->tick(52);
    physical_cycle(*stopped,53,false);
    check(stopped->circuit->independent_renewals==1,"retired actor completes actual subsequent independent income, conversion and renewal");
    cb::observe_cycle(*stopped->in,stopped->local,*stopped->circuit,*stopped->ledger,stopped->work,stopped->trace);
    check(stopped->circuit->independent_renewals==1,"reobserving one physical independent completion cannot create another episode");
    auto unsafe=std::make_unique<Fixture>();unsafe->bootstrap();const auto selected=cb::decide(*unsafe->in,*unsafe->circuit,*unsafe->ledger,cb::Arm::active);
    unsafe->physical(unsafe->circuit->package.systems[0].plan);
    check(cb::commit(*unsafe->in,unsafe->local,selected,*unsafe->circuit,*unsafe->ledger,cb::Arm::active,unsafe->work,unsafe->trace).started,
        "safety oracle starts one actual dependent episode");
    unsafe->tick(27);unsafe->local.legal.value.safe_now=false;
    cb::advance(*unsafe->in,unsafe->local,*unsafe->circuit,*unsafe->ledger,cb::Arm::active,unsafe->work);
    check(unsafe->circuit->systems[0].retired&&unsafe->circuit->systems[0].reason==cb::Retirement::safety,
        "actual turn-boundary unsafe observation retires immediately and spends the issued campaign");
}
void diversified_draft() {
    static std::array<cb::DraftCandidate,8> candidates;
    for(int i=0;i<8;++i) {
        auto& c=candidates[i];c.champion=cb::champion_pool[i];c.first_income=plan();
        c.public_geometry_verified=c.ability_schedule_legal=true;
        c.starting_health=c.starting_max_health={100,p6::Source::queried_setting};
        for(auto& e:c.cumulative_survival)e={100,100,10,0,0,0,true};
        auto& s=c.complete_schedule;s.actor_count=s.production_count=1;s.target_id=9;
        auto& a=s.actors[0];a.actor_id=1;a.initial_bag=fact<p6::Gems>(0);a.initial_gold=fact<p6::Gems>(0);
        a.occupied_turns=fact<p6::Gems>(20);event(a,p6::EventKind::production,1,12,0);
        event(a,p6::EventKind::tribute,2,18);event(a,p6::EventKind::renewal,3,20);
        auto& p=s.productions[0];p.event_id=1;p.tick=12;p.gems=fact<p6::Gems>(10+i);p.gold=fact<p6::Gems>(0);
        p.holders[0]=1;p.holder_count=1;p.source=p6::Source::synthetic_fixture;
    }
    cb::DraftInputs in;in.prospective_actor=1;in.settings={2000,1000,5};in.settings_identity={17,p6::Source::queried_setting};
    in.windows.deadlines=in.windows.windows={120,120,120};in.windows.sampled_actor_id=1;in.windows.sampled_round=0;
    in.windows.source=p6::Source::public_map;in.windows.valid=true;
    const auto best=cb::select_draft(in,candidates);
    check(best.champion==p6::Champion::ren&&best.role==common::DraftRole::collector,
        "eight-champion complete first-income pool can select a stronger nonprescribed leader");
    for(auto& c:candidates)c.complete_schedule.productions[0].gems=fact<p6::Gems>(10);
    check(cb::select_draft(in,candidates).champion==p6::Champion::varg,
        "Varg leader preference applies only after identical complete-cycle ranks");
    in.ordinal=1;in.own_picks[0]=p6::Champion::willow;
    check(cb::select_draft(in,candidates).champion!=p6::Champion::willow,"safe support preference cannot duplicate an actual own pick");
    for(auto& c:candidates)c.cumulative_survival[3].incoming_total=1000;
    check(cb::select_draft(in,candidates).candidate<0,"unsupported first-income survival cannot be replaced by roster metadata");
}
} // namespace
int main() {
    selection_and_campaigns();retirement_and_private_lag();failure_and_freshness();requalification();own_physical_traces();diversified_draft();
    std::printf("plan8 circuitbreaker: %d checks passed; pure private policy/physical oracles only\n",checks);return 0;
}
