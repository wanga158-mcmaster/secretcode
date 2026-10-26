#include "../staged-venture/src/policy.h"
#include "../staged-venture/src/draft.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>
using namespace p8;
namespace sv=p8::staged_venture;
namespace {
int checks=0;
void check(bool yes,const char* message){++checks;if(!yes){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
template<class T> p6::Fact<T> fact(T x){return {x,p6::Source::synthetic_fixture};}
Route path(std::initializer_list<p6::Point> tiles){Route p;p.verified=true;for(auto t:tiles)p.tiles[p.count++]=t;return p;}
Plan circuit(bool supported=false,int group=7) {
    Plan p;p.organization=supported?Organization::H:Organization::U;p.group=group;p.collector=2;p.altar=9;
    p.crew[0]=2;p.crew_count=1;
    p.ingress=path({{0,0},{1,0},{2,0}});p.capture=path({{2,0},{3,0}});
    p.bank=path({{3,0},{4,0}});p.renewal=path({{4,0},{5,0},{6,0}});
    if(supported){p.crew[1]=1;p.crew_count=2;p.ingress=path({{0,0},{1,1},{2,1}});
        p.capture=path({{2,1},{3,0}});p.pads[0]={2,1};p.pads[1]={2,2};p.rally_legal=true;}
    p.forecast_rounds=30;p.remaining_rounds=500;p.capture_window=5;p.gem_bearing=p.complete_verified=true;return p;
}
void event(p6::ActorSchedule& a,p6::EventKind kind,int id,int tick,int production=-1,p6::Gems cost=0) {
    a.events[a.event_count++]={kind,id,tick,production,fact(cost),kind==p6::EventKind::renewal?90+a.actor_id:-1,p6::Source::synthetic_fixture};
}
__attribute__((noinline)) p6::Schedule schedule(const p6::Frame& f,bool supported=false,bool purchase=false,int target=7) {
    p6::Schedule s;s.target_id=target;s.actor_count=3;
    for(int id=1;id<=3;++id) {
        auto& a=s.actors[id-1];const auto* seen=f.visible(id);a.actor_id=id;
        a.initial_bag={seen->bag,id==f.clock.self?p6::Source::self_observed:p6::Source::visible_subject};
        a.initial_gold={id==f.clock.self?f.own.gold:0,id==f.clock.self?p6::Source::self_observed:p6::Source::conservative};
        a.occupied_turns=fact<p6::Gems>(supported?35:30);
        if(id==2&&purchase)event(a,p6::EventKind::purchase,50,0,-1,30);
        const int index=s.production_count++;auto& p=s.productions[index];
        p.event_id=100+id;p.tick=8;p.gems=fact<p6::Gems>(id==2&&target==7?15:10);p.gold=fact<p6::Gems>(0);
        p.holders[0]=id;p.holder_count=1;p.source=p6::Source::synthetic_fixture;
        event(a,p6::EventKind::production,p.event_id,8,index);event(a,p6::EventKind::tribute,200+id,15);
        if(id==2&&supported) {
            const int second=s.production_count++;auto& continuation=s.productions[second];
            continuation.event_id=108;continuation.tick=22;continuation.gems=fact<p6::Gems>(40);continuation.gold=fact<p6::Gems>(0);
            continuation.holders[0]=2;continuation.holder_count=1;continuation.source=p6::Source::synthetic_fixture;
            event(a,p6::EventKind::production,108,22,second);event(a,p6::EventKind::tribute,208,27);
        }
        event(a,p6::EventKind::renewal,300+id,supported?35:30);
    }
    return s;
}
p6::CycleCertificate safe(const p6::Frame& f,int id,int group,double damage=10) {
    p6::CycleCertificate c;c.clock=f.clock;c.actor=id;c.group=group;const auto* a=f.visible(id);
    if(a)c.clear=c.hold=c.bank=c.recovery={a->health,a->max_health,damage,0,0,0,true};
    c.route_known=c.bank_executable=c.renewal_reachable=c.solo=true;return c;
}
struct Fixture {
    std::unique_ptr<sv::Inputs> in=std::make_unique<sv::Inputs>();
    std::unique_ptr<sv::Venture> venture=std::make_unique<sv::Venture>();
    std::unique_ptr<sv::PurchaseComparison> comparison=std::make_unique<sv::PurchaseComparison>();
    std::unique_ptr<EvidenceLedger> ledger=std::make_unique<EvidenceLedger>(2);
    LocalFrame local{};Work work{};sv::CycleTrace trace{};
    Fixture() {
        auto& f=in->frame;f.clock={0,2,0};f.own.gold=40;f.own.at_spawn=true;
        for(int id=1;id<=3;++id) {
            p6::Actor a;a.id=id;a.pick_ordinal=id-1;a.type=id==1?p6::Champion::varg:id==2?p6::Champion::tavi:p6::Champion::marcella;
            a.health=a.max_health=100;a.strength=20;a.team=p6::Team::ally;a.alive=a.can_move=a.can_attack=true;
            a.position=id==2?p6::Point{0,0}:id==1?p6::Point{2,2}:p6::Point{0,2};
            if(id==2)f.own.actor=a;else f.actors[f.actor_count++]={a,f.clock,id,p6::Knowledge::exact};
        }
        p6::Group g;g.id=7;g.original_spawn_visible=g.all_spawns_visible=g.uncontested_now=true;g.lifecycle=p6::Lifecycle::living;
        f.group={g,f.clock,7,p6::Knowledge::exact};
        in->draft.picks=in->picks=3;in->draft.actors={1,2,3,-1,-1};
        in->draft.champions={p6::Champion::varg,p6::Champion::tavi,p6::Champion::marcella,p6::Champion::other,p6::Champion::other};
        in->settings={2000,1000,5};in->settings_identity={17,p6::Source::queried_setting};in->target_group=7;
        in->own_state={{100,100,20,10,1,0,10},f.clock,2,p6::Knowledge::exact};
        sv::Package p;p.preliminary=circuit();p.supported=circuit(true);
        auto& a=p.ordinary;a.key.settings_id=17;a.key.route_hash=plan_route_hash(p.preliminary);
        a.key.return_route_hash=plan_return_hash(p.preliminary);a.key.actor_id=2;a.key.champion=int(p6::Champion::tavi);
        a.key.role=0;a.key.source_group=7;a.key.route_template=0;a.key.endpoint=9;a.key.threat_regime=0;
        a.key.participants[0]=2;a.key.participant_count=1;a.key.build={10,100,20,10,1};
        a.envelope={{std::nextafter(0.,1.),100},{100,100},{20,20},{10,10},{1,1},{0,100},0};a.require_spawn_shop=true;
        p.invested=a;p.invested.kind=EpisodeKind::full_organization;p.invested.key.role=1;
        p.invested.key.route_hash=plan_route_hash(p.supported);p.invested.key.participants={1,2,-1,-1,-1,-1};p.invested.key.participant_count=2;
        p.invested.key.build={11,120,20,10,1};p.invested.envelope.health.upper=120;p.invested.envelope.max_health={120,120};
        p.invested.require_supported_work=true;p.cost={30,p6::Source::queried_setting};p.health_gain={20,p6::Source::queried_setting};
        check(venture->declare(p,in->draft,*ledger),"bounded S-first venture declares ordinary and fresh invested contexts");
        tick(0);
    }
    void tick(int round) {
        auto& f=in->frame;f.clock.round=round;f.group.clock=f.clock;
        for(int i=0;i<f.actor_count;++i)f.actors[i].clock=f.clock;
        in->own_state.clock=f.clock;in->own_state.value.health=f.own.actor.health;in->own_state.value.max_health=f.own.actor.max_health;
        in->own_state.value.strength=f.own.actor.strength;in->own_state.value.bag=f.own.actor.bag;
        in->threat_regime={0,f.clock,f.clock.self,p6::Knowledge::exact};in->spawn_shop_legal={f.own.at_spawn,f.clock,f.clock.self,p6::Knowledge::exact};
        in->windows.deadlines=in->windows.windows={120,120,120};in->windows.sampled_actor_id=f.clock.self;
        in->windows.sampled_round=round;in->windows.source=p6::Source::private_history;in->windows.valid=true;
        in->independent=0;in->offer_count=3;
        for(int i=0;i<3;++i) {
            auto& o=in->offers[i];o={};o.plan=i==2?venture->package.supported:i==1?venture->package.preliminary:circuit(false,9);
            o.schedule=schedule(f,i==2,false,i?7:9);o.safety_count=3;
            for(int id=1;id<=3;++id)o.safety[id-1]=safe(f,id,crew_index(o.plan,id)>=0?o.plan.group:10,i==2&&id==2?90:10);
            o.context_slot=i==2?venture->organization:venture->component;o.entry=i==2?venture->package.invested.key:venture->package.ordinary.key;
        }
        sv::bind(*in,*venture);local.frame=f;local.current_round=round;local.safety_count=2;
        local.safety[0]=safe(f,2,7,venture->paid?90:10);local.safety[1]=safe(f,1,7);
        local.legal={{},f.clock,f.clock.self,p6::Knowledge::exact};
        auto& l=local.legal.value;l.safe_now=l.attack_legal=l.altar_legal=l.recall_safe=l.renewal_productive=true;
        const auto& p=venture->paid?venture->package.supported:venture->package.preliminary;
        for(Leg leg:{Leg::ingress,Leg::capture,Leg::bank,Leg::renewal}) {
            auto& access=l.routes[int(leg)];const auto& route=route_for(p,leg);access.assumptions_hold=true;access.next_move_legal=true;
            access.next_tile=route.tiles[std::min(1,route.count-1)];
        }
        comparison->funded=schedule(f,true,true);comparison->ordinary=schedule(f,false,false);
        comparison->item_legal={true,f.clock,f.clock.self,p6::Knowledge::exact};comparison->helper_safety=safe(f,1,7);
        for(auto& e:comparison->new_own_survival)e={120,120,90,0,0,0,true};
    }
    void qualify() {
        for(int n=0;n<2;++n) {
            const int start=1+n*31;EpisodeRequest r;r.context=venture->component;r.issue_round=start;r.deadline=start+100;
            r.nominal_duration=30;r.action=int(Organization::U);r.premises={venture->package.ordinary.key,in->own_state.value,all_premises};
            const auto serial=ledger->issue(r);check(serial!=0,"productive ordinary-gear component issues before action");
            Observation o;o.round=start+30;o.kind=ObservationKind::completed;o.premises=r.premises;
            o.duration={{30,30},DurationSource::personally_exact};auto& receipt=o.receipts;receipt.source=ReceiptSource::own_observation;
            receipt.production=receipt.payout_owned=receipt.conversion=receipt.renewed=receipt.attribution_excludes_other_bag_loss=true;
            receipt.payout_event=receipt.conversion_event=receipt.renewal_event=n+1;receipt.positive_reward_lower=15;
            receipt.production_round=start+2;receipt.payout_round=start+8;receipt.conversion_round=start+15;
            receipt.spawn_visited=receipt.spawn_shop_legal=true;receipt.spawn_round=start+20;receipt.renewal_round=start+30;
            check(ledger->resolve(serial,o).full_positive,"actual own payout, tribute, spawn shop and return qualify one complete preliminary cycle");
        }
        tick(63);
    }
    sv::PurchaseCommand purchase(sv::Arm arm) {
        return sv::propose_purchase(*in,local,work,*venture,*ledger,arm,*comparison);
    }
    bool confirm(const sv::PurchaseCommand& command,sv::Arm arm,bool debit=true) {
        ++in->frame.clock.revision;in->frame.own.gold-=debit?30:0;in->frame.own.actor.health=in->frame.own.actor.max_health=120;
        in->own_state.value.equipment_hash=11;tick(in->frame.clock.round);
        const bool confirmed=sv::confirm_purchase(*in,command,*venture,*ledger,arm,safe(in->frame,1,7));
        if(confirmed)tick(in->frame.clock.round);
        return confirmed;
    }
};
}
int main() {
    {
        auto f=std::make_unique<Fixture>();
        const auto d=sv::decide(*f->in,*f->venture,*f->ledger,sv::Arm::active);
        check(d.option==1&&d.record_episode&&!d.capital_allowed,"ordinary productive preliminary cycle precedes staged expenditure");
        check(!f->purchase(sv::Arm::active).valid,"zero component successes cannot authorize S-first expenditure");
        const auto admission=sv::commit(*f->in,f->local,d,*f->venture,*f->ledger,sv::Arm::active,f->work,f->trace);
        check(admission.started&&admission.episode&&f->trace.active,"component episode binds a fresh physical Work generation");
        const auto move=sv::advance(*f->in,f->local,*f->venture,*f->ledger,sv::Arm::active,f->work);
        check(move.action==PhysicalAction::move&&move.target==p6::Point{1,0},"preliminary production executes its actual retained route");
    }
    {
        auto f=std::make_unique<Fixture>();const auto purchase=f->purchase(sv::Arm::control);
        check(purchase.valid&&f->ledger->state(f->venture->component)->attempts==0,"control funds the identical present-feasible package before preliminary cycles");
        check(f->confirm(purchase,sv::Arm::control),"control confirms actual own purchase debit and new build");
        const auto d=sv::decide(*f->in,*f->venture,*f->ledger,sv::Arm::control);
        check(d.option==2&&!d.record_episode,"immediate control uses new physical H without claiming component or full qualification");
    }
    {
        auto f=std::make_unique<Fixture>();const auto d=sv::decide(*f->in,*f->venture,*f->ledger,sv::Arm::control);
        check(sv::commit(*f->in,f->local,d,*f->venture,*f->ledger,sv::Arm::control,f->work,f->trace).started&&
              !f->purchase(sv::Arm::control).valid,"even control must finish an active unrecorded physical job before buying at spawn");
    }
    {
        auto f=std::make_unique<Fixture>();f->qualify();
        check(f->venture->component_current(*f->in,*f->ledger),"two matched complete ordinary-gear cycles establish only the named return component");
        const auto purchase=f->purchase(sv::Arm::active);
        check(purchase.valid&&f->venture->reservation&&!f->venture->paid,"purchase is reserved before any gold or equipment action");
        check(!f->purchase(sv::Arm::active).valid,"one bounded package cannot issue duplicate purchase commands");
        check(f->confirm(purchase,sv::Arm::active),"same-round own debit, queried S growth and new equipment begin a fresh commissioning campaign");
        check(!f->ledger->component_qualified(f->venture->component,f->venture->package.ordinary.key,63)&&
              !f->ledger->organization_qualified(f->venture->organization,f->venture->package.invested.key,63),
              "ordinary build proof retires while the newly invested organization starts unqualified");
        const auto d=sv::decide(*f->in,*f->venture,*f->ledger,sv::Arm::active);
        check(d.option==2&&d.record_episode,"confirmed staged investment changes actual subsequent helper commitment to H");
        const auto admitted=sv::commit(*f->in,f->local,d,*f->venture,*f->ledger,sv::Arm::active,f->work,f->trace);
        check(admitted.started&&admitted.episode,"post-build H issues its own episode rather than importing the component successes");
        const auto move=sv::advance(*f->in,f->local,*f->venture,*f->ledger,sv::Arm::active,f->work);
        check(move.action==PhysicalAction::move&&move.target==p6::Point{1,1},"supported investment physically takes the different retained crew route");
        check(f->work.confirm(move,{move.clock,move.clock,move.token,move.action,true,true})&&!f->work.productive_action_seen,
              "a successful movement receipt is neither combat nor payout evidence");
    }
    {
        auto f=std::make_unique<Fixture>();f->qualify();f->in->frame.own.gold=20;f->tick(63);
        check(!f->purchase(sv::Arm::active).valid,"future source rewards cannot fund a package above present personal gold");
        f->in->frame.own.gold=40;f->tick(63);f->comparison->new_own_survival[3].incoming_total=120;
        check(!f->purchase(sv::Arm::active).valid,"preliminary route proof cannot license individually unsafe new combat");
        f->tick(63);f->comparison->funded=f->comparison->ordinary;
        check(!f->purchase(sv::Arm::active).valid,"capital without a better complete same-actor continuation is not free score");
    }
    {
        auto f=std::make_unique<Fixture>();f->qualify();const auto command=f->purchase(sv::Arm::active);
        check(!f->confirm(command,sv::Arm::active,false)&&f->venture->phase==sv::VenturePhase::retired,
              "new item metadata without an exact own gold debit retires the spent package attempt");
        check(f->ledger->state(f->venture->organization)->campaign_failed,"failed receipt cannot replenish the reserved commissioning campaign");
    }
    {
        auto f=std::make_unique<Fixture>();f->qualify();f->tick(318);
        check(!f->purchase(sv::Arm::active).valid,"ordinary proof expires exactly 256 rounds after its latest completion");
    }
    {
        auto f=std::make_unique<Fixture>();const auto command=f->purchase(sv::Arm::control);
        check(f->confirm(command,sv::Arm::control),"helper adoption fixture has an actually purchased collector build");
        auto& in=*f->in;const auto collector=in.frame.own.actor;const auto collector_state=in.own_state.value;
        in.frame.own.actor=in.frame.actors[0].value;in.frame.own.actor.position={0,0};in.frame.own.actor.bag=5;
        in.frame.own.gold=0;in.frame.clock.self=1;in.frame.actors[0].value=collector;
        in.frame.actors[0].value.position=f->venture->package.supported.pads[0];in.frame.actors[0].subject=2;
        in.own_state.value={100,100,20,10,1,5,20};in.own_state.subject=1;f->tick(1);
        in.visible_proposer={7,in.frame.clock,2,p6::Knowledge::exact};
        in.collector_state={collector_state,in.frame.clock,2,p6::Knowledge::exact};
        auto& independent=in.offers[0];independent.plan.collector=1;independent.plan.crew[0]=1;
        independent.safety[0]=safe(in.frame,1,9);
        auto& own=in.offers[2].plan;own.ingress=path({{0,0},{1,1},{2,2}});own.capture=path({{2,2}});
        own.bank=path({{2,2},{3,1},{4,0}});own.release=path({{2,2},{3,1}});
        auto helper=std::make_unique<sv::Venture>();helper->package=f->venture->package;
        auto private_ledger=std::make_unique<EvidenceLedger>(1);
        const auto decision=sv::decide(in,*helper,*private_ledger,sv::Arm::active);
        check(plan_return_hash(own)!=helper->package.invested.key.return_route_hash&&decision.option==2&&!decision.record_episode,
              "visible helper adopts the actual collector while retaining its distinct own bank and release route");
        check(sv::commit(in,f->local,decision,*helper,*private_ledger,sv::Arm::active,f->work,f->trace).started&&
              private_ledger->size()==0&&!private_ledger->active()&&in.frame.own.actor.bag==5,
              "loaded helper physically joins without transferring its existing bag or importing private collector proof");
        in.frame.actors[0].knowledge=p6::Knowledge::unknown;
        check(sv::decide(in,*helper,*private_ledger,sv::Arm::active).option!=2,"unseen collector removes helper admission");
    }
    {
        auto f=std::make_unique<Fixture>();auto wrong=std::make_unique<sv::Package>(f->venture->package);
        wrong->preliminary.renewal.tiles[1]={5,1};wrong->ordinary.key.route_hash=plan_route_hash(wrong->preliminary);
        wrong->ordinary.key.return_route_hash=plan_return_hash(wrong->preliminary);
        check(!sv::package_valid(*wrong,f->in->draft),"a cheap preliminary visit over a different decisive return cannot approve the venture");
    }
    check(sv::supported_champion(p6::Champion::ren)&&sv::supported_champion(p6::Champion::prism)&&
          !sv::supported_champion(p6::Champion::cassi),"role preferences retain the broader supported single-pick pool");
    std::printf("plan8-staged-venture: %d explicit checks passed; synthetic kernel, no native claim\n",checks);
}
