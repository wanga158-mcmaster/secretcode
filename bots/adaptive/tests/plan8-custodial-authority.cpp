#include "../custodial-authority/src/policy.h"
#include "../custodial-authority/src/draft.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>
using namespace p8;using namespace p8::custodial_authority;
namespace {
int checks=0;
void check(bool value,const char* message){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
template<class T> p6::Fact<T> fact(T value){return {value,p6::Source::synthetic_fixture};}
Route path(std::initializer_list<p6::Point> points){Route r;r.verified=true;for(auto p:points)r.tiles[r.count++]=p;return r;}
__attribute__((noinline)) Plan circuit(int actor,int group=7,bool supported=false,int collector=2){
    Plan p;p.organization=supported?Organization::H:Organization::U;p.group=group;p.collector=supported?collector:actor;
    p.crew[0]=p.collector;p.crew_count=1;if(supported){p.crew[1]=3;p.crew_count=2;}
    p.pads[0]=collector==1?p6::Point{2,0}:p6::Point{2,1};p.pads[1]={2,2};
    const auto start=actor==1?p6::Point{2,0}:actor==2?p6::Point{2,1}:p6::Point{2,2};
    if(group==8){p.ingress=actor==1?path({{2,0},{2,1},{2,2},{3,2},{4,2}}):actor==2?path({start,{2,2},{3,2},{4,2}}):path({start,{3,2},{4,2}});p.capture=path({{4,2},{5,2}});
        p.bank=path({{5,2},{6,2}});p.renewal=path({{6,2},{6,3},{5,3},{4,3},{4,2}});p.altar=26;}
    else if(group==9){p.ingress=path({start,{3,3},{4,4}});p.capture=path({{4,4},{5,4}});
        p.bank=path({{5,4},{6,4}});p.renewal=path({{6,4},{6,5},{5,5},{4,4}});p.altar=46;}
    else {p.ingress=path({start});p.capture=supported&&actor!=collector?path({start}):path({start,{3,0}});
        if(actor==1){p.bank=supported&&actor!=collector?path({start,{3,0},{4,0}}):path({{3,0},{4,0}});
            p.renewal=path({{4,0},{4,1},{3,1},{2,0}});p.altar=4;}
        else {p.bank=supported&&actor!=collector?path({start,{3,1},{4,1},{5,0},{6,0}}):path({{3,0},{4,1},{5,0},{6,0}});
            p.renewal=path({{6,0},{5,1},{4,1},{3,1},{2,1}});p.altar=6;}
        if(supported&&actor!=collector)p.release=path({start,{3,3},{4,4}});}
    p.forecast_rounds=group==8?20:group==9?17:supported&&actor!=collector?40:actor==1?(supported?14:15):(supported?17:18);
    p.remaining_rounds=110;p.capture_window=5;p.gem_bearing=p.complete_verified=p.rally_legal=true;return p;
}
p6::CycleCertificate safety(const p6::Frame& frame,int actor,int group){p6::CycleCertificate c;c.clock=frame.clock;c.actor=actor;c.group=group;
    if(const auto* a=frame.visible(actor))c.clear=c.hold=c.bank=c.recovery={a->health,a->max_health,10,0,0,0,true};
    c.route_known=c.bank_executable=c.renewal_reachable=c.solo=true;return c;}
void event(p6::ActorSchedule& a,p6::EventKind kind,int id,int tick,int production=-1){p6::Event e;e.kind=kind;e.event_id=id;e.tick=tick;e.production_index=production;
    e.gold_cost=fact<p6::Gems>(0);e.source=p6::Source::synthetic_fixture;e.waypoint_id=kind==p6::EventKind::renewal?100+a.actor_id:-1;a.events[a.event_count++]=e;}
__attribute__((noinline)) p6::Schedule schedule(const p6::Frame& frame,int target,bool original,bool h){
    p6::Schedule s;s.target_id=target;s.actor_count=s.production_count=3;
    for(int id=1;id<=3;++id){auto& a=s.actors[id-1];a.actor_id=id;const bool own=id==frame.clock.self;
        a.initial_bag={frame.visible(id)?frame.visible(id)->bag:0,own?p6::Source::self_observed:p6::Source::visible_subject};
        a.initial_gold={own?frame.own.gold:0,own?p6::Source::self_observed:p6::Source::conservative};
        const int collector=original?1:2,group=id==collector?7:id==3?9:8;
        int bank=group==7?(original?12:14):group==8?(id==1?16:15):30;
        if(h&&group==7)--bank;if(h&&group==9)bank+=8;
        auto& p=s.productions[id-1];p.event_id=1000+group;p.tick=bank-3;p.gems=fact<p6::Gems>(group==7?12:10);p.gold=fact<p6::Gems>(0);
        p.holders[0]=id;p.holder_count=1;p.source=p6::Source::synthetic_fixture;
        event(a,p6::EventKind::production,p.event_id,p.tick,id-1);event(a,p6::EventKind::tribute,2000+group,bank);
        event(a,p6::EventKind::renewal,3000+id,bank+4);a.occupied_turns=fact<p6::Gems>(bank+4);}
    return s;
}
ActionReceipt receipt(const Command& c){return {c.clock,c.clock,c.token,c.action,true,true};}
struct Fixture {
    Inputs in{};Authority authority;EvidenceLedger ledger;Execution execution{};LocalFrame local{};
    int self=1;bool original=true;
    __attribute__((noinline)) explicit Fixture(int actor):authority(actor),ledger(actor),self(actor){
        in.frame.clock={0,self,0};in.frame.own.gold=15;in.settings={2000,1000,5};in.settings_identity={17,p6::Source::queried_setting};
        in.draft.champions={p6::Champion::marcella,p6::Champion::varg,p6::Champion::tavi,p6::Champion::other,p6::Champion::other};
        in.draft.actors={1,2,3,-1,-1};in.draft.picks=in.picks=3;
        for(int id=1;id<=3;++id){p6::Actor a;a.id=id;a.pick_ordinal=id-1;a.type=in.draft.champions[id-1];a.team=p6::Team::ally;
            a.health=a.max_health=100;a.strength=20;a.attack_range=id==3?3:1;a.ranged=id==3;a.alive=a.can_move=a.can_attack=true;
            a.position=id==1?p6::Point{2,0}:id==2?p6::Point{2,1}:p6::Point{2,2};
            if(id==self)in.frame.own.actor=a;else in.frame.actors[in.frame.actor_count++]={a,in.frame.clock,id,p6::Knowledge::exact};}
        in.own_state={{100,100,20,30,1,0,99},in.frame.clock,self,p6::Knowledge::exact};in.threat_regime={0,in.frame.clock,self,p6::Knowledge::exact};
        auto& meeting=in.meeting;meeting.original=circuit(1,7,true,1);meeting.successor=circuit(2,7,true,2);meeting.former_u=circuit(1,8);
        meeting.helper=3;meeting.original_pad={2,0};meeting.successor_pad={2,1};meeting.helper_pad={2,2};
        check(spec_valid(meeting,in.draft),"handoff freezes actual role IDs, both bank routes and former U geometry");
        in.target_group=7;in.independent=0;in.offer_count=3;
        if(self==1){in.offers[1].plan=circuit(1,7,true,1);in.offers[2].plan=circuit(1,7,true,2);}
        else if(self==2){in.offers[1].plan=circuit(2,7,true,1);in.offers[2].plan=circuit(2,7,true,2);}
        else {in.offers[1].plan=circuit(3,7,true,1);in.offers[2].plan=circuit(3,7,true,2);}
        if(self==1){auto full=declaration(in.offers[1].plan,true);in.supported_slot=ledger.predeclare(full);in.supported_entry=full.key;
            in.offers[1].context_slot=in.supported_slot;in.offers[1].entry=full.key;check(in.supported_slot>=0,"old collector owns its own original H declaration");}
        if(self==2){const auto solo=circuit(2);auto component=declaration(solo,false);auto full=declaration(in.offers[2].plan,true);
            in.component_slot=ledger.predeclare(component);in.component_entry=component.key;in.supported_slot=ledger.predeclare(full);in.supported_entry=full.key;
            in.offers[2].context_slot=in.supported_slot;in.offers[2].entry=full.key;check(in.component_slot>=0&&in.supported_slot>=0,"successor owns separate relevant U and new H contexts");}
        configure(self==1);tick(0,7,p6::Lifecycle::living);
    }
    ContextDeclaration declaration(const Plan& p,bool h){ContextDeclaration d;auto& k=d.key;k.settings_id=17;k.route_hash=plan_route_hash(p);k.return_route_hash=plan_return_hash(p);
        k.actor_id=self;k.champion=int(in.draft.champions[self-1]);k.role=h?1:0;k.source_group=p.group;k.route_template=0;k.endpoint=p.altar;k.threat_regime=0;
        k.participants=p.crew;k.participant_count=p.crew_count;std::sort(k.participants.begin(),k.participants.begin()+k.participant_count);k.build={99,100,20,30,1};
        d.kind=h?EpisodeKind::full_organization:EpisodeKind::return_component;d.require_supported_work=h;
        d.envelope={{20,100},{100,100},{20,20},{30,30},{1,1},{0,12},0};
        for(int j=0;j<3;++j)d.hypotheses[j].offset={2*j,2*j+2};return d;}
    __attribute__((noinline)) void configure(bool old){original=old;in.offers[0].plan=circuit(self,self==3?9:self==(old?1:2)?7:8);
        if(self==2&&in.offers[0].plan.group==7){in.offers[0].context_slot=in.component_slot;in.offers[0].entry=in.component_entry;}
        else {in.offers[0].context_slot=-1;in.offers[0].entry={};}
        for(int o=0;o<3;++o)in.offers[o].schedule=schedule(in.frame,in.offers[o].plan.group,old,o>0);
        auto& own=in.private_income.value;own=in.offers[0];own.schedule={};own.schedule.target_id=own.plan.group;own.schedule.actor_count=own.schedule.production_count=1;
        own.schedule.actors[0]=in.offers[0].schedule.actors[self-1];own.schedule.productions[0]=in.offers[0].schedule.productions[self-1];
        for(int e=0;e<own.schedule.actors[0].event_count;++e)if(own.schedule.actors[0].events[e].production_index>=0)own.schedule.actors[0].events[e].production_index=0;}
    __attribute__((noinline)) void observe_plan(const Plan& p){local.frame=in.frame;local.current_round=in.frame.clock.round;local.milestones={};local.safety_count=p.crew_count;
        for(int i=0;i<p.crew_count;++i)local.safety[i]=safety(in.frame,p.crew[i],p.group);
        local.legal={{},in.frame.clock,self,p6::Knowledge::exact};auto& legal=local.legal.value;
        legal.safe_now=legal.attack_legal=legal.altar_legal=legal.recall_safe=legal.renewal_productive=true;
        for(Leg leg:{Leg::ingress,Leg::capture,Leg::bank,Leg::renewal,Leg::release}){const auto& r=route_for(p,leg);auto& a=legal.routes[int(leg)];a.assumptions_hold=route_valid(r);a.next_move_legal=true;
            for(int i=0;i<r.count;++i)if(r.tiles[i]==in.frame.own.actor.position){a.next_tile=r.tiles[std::min(i+1,r.count-1)];break;}}}
    __attribute__((noinline)) void tick(int round,int group,p6::Lifecycle lifecycle){auto& f=in.frame;f.clock.round=round;for(int a=0;a<f.actor_count;++a)f.actors[a].clock=f.clock;
        p6::Group g;g.id=group;g.lifecycle=lifecycle;g.original_spawn_visible=g.all_spawns_visible=g.uncontested_now=true;f.group={g,f.clock,group,p6::Knowledge::exact};
        in.own_state.clock=in.threat_regime.clock=f.clock;in.own_state.value.bag=f.own.actor.bag;
        in.windows.valid=true;in.windows.source=p6::Source::synthetic_fixture;in.windows.sampled_actor_id=self;in.windows.sampled_round=round;
        in.windows.windows=in.windows.deadlines={original?11:13,original?11:13,original?11:13};
        for(int o=0;o<3;++o){auto& offer=in.offers[o];offer.safety_count=3;for(int id=1;id<=3;++id)
            offer.safety[id-1]=safety(f,id,common::participant(offer.plan,id)?offer.plan.group:id==3?9:id==(original?1:2)?7:8);}
        in.private_income.clock=f.clock;in.private_income.subject=self;in.private_income.knowledge=p6::Knowledge::exact;
        in.private_income.value.safety_count=1;in.private_income.value.safety[0]=safety(f,self,in.private_income.value.plan.group);
        in.own_access={{7,plan_return_hash(in.meeting.original),{true,p6::Source::self_observed},{true,p6::Source::self_observed},{false,p6::Source::self_observed},{}},f.clock,self,p6::Knowledge::exact};
        const Signal signal{meeting_hash(in.meeting),1,2,7};in.original_offer={signal,f.clock,1,p6::Knowledge::exact};in.successor_offer={signal,f.clock,2,p6::Knowledge::exact};
        in.meeting_safety={safety(f,1,8),safety(f,2,7),safety(f,3,7)};
        in.collector_proposal={7,f.clock,original?1:2,p6::Knowledge::exact};
        observe_plan(execution.work.active?execution.work.plan:in.offers[0].plan);
    }
    void loss(){in.own_access.value.return_executable.value=false;in.own_access.value.blocker_visible.value=true;in.own_access.value.blocked_tile={4,0};refresh(in,authority,ledger);}
    __attribute__((noinline)) void ready(int round,bool old){
        in.frame.own.actor.position=self==1?p6::Point{2,0}:self==2?p6::Point{2,1}:p6::Point{2,2};in.frame.own.actor.bag=0;in.frame.own.actor.channel=p6::Channel::none;in.frame.own.actor.spawn_group=-1;
        for(int a=0;a<in.frame.actor_count;++a){auto& actor=in.frame.actors[a].value;actor.position=actor.id==1?p6::Point{2,0}:actor.id==2?p6::Point{2,1}:p6::Point{2,2};actor.bag=0;actor.channel=p6::Channel::none;actor.spawn_group=-1;actor.action=p6::WitnessAction::none;}
        configure(old);tick(round,7,p6::Lifecycle::living);
    }
    __attribute__((noinline)) void cycle(int start,bool h=false,bool lost_channel=false,Arm arm=Arm::active){
        const bool old=self==1&&!authority.transferred;ready(start,old);if(self==1&&authority.lost&&arm==Arm::active)loss();
        auto d=decide(in,authority,ledger,arm);check(d.option==(h?(old?1:2):0),"actor selects its own ordinary or authorized actual collector duty");
        const auto& p=in.offers[d.option].plan;observe_plan(p);const auto admitted=commit(in,local,d,authority,ledger,arm,execution);
        check(admitted.started,"current retained production circuit starts");
        const auto frozen=p;const int clear=frozen.group==8?6:h?1:2;
        int capture_round=-1,payout_round=-1,bank_round=-1;bool applied=false;
        for(int t=0;t<90&&execution.work.active;++t){auto& own=in.frame.own.actor;
            own.spawn_group=own.position==frozen.capture.tiles[frozen.capture.count-1]&&t>=clear?frozen.group:-1;
            if(capture_round>=0&&t==capture_round+6){own.bag=frozen.group==7?12:10;payout_round=t;}
            if(bank_round>=0&&t==bank_round+2){own.bag=0;own.channel=p6::Channel::none;}
            tick(start+t,frozen.group,t<clear?p6::Lifecycle::living:payout_round<0?p6::Lifecycle::reward_available:p6::Lifecycle::respawning);
            if(h)for(int a=0;a<in.frame.actor_count;++a)if(in.frame.actors[a].value.id==3){auto& helper=in.frame.actors[a].value;
                helper.action=t<clear?p6::WitnessAction::useful_attack:p6::WitnessAction::none;helper.action_group=7;
                helper.position=t<=clear?p6::Point{2,2}:p6::Point{3,3};helper.spawn_group=-1;local.frame=in.frame;}
            if(payout_round==t)local.milestones={{frozen.group,true,false,own.bag,0},in.frame.clock,self,p6::Knowledge::exact};
            if(bank_round>=0&&t==bank_round+2)local.milestones={{frozen.group,false,true,0,frozen.group==7?12:10},in.frame.clock,self,p6::Knowledge::exact};
            if(lost_channel&&own.channel==p6::Channel::tribute){
                in.own_access.value.region_visible.value=false;refresh(in,authority,ledger);applied=true;
                check(!commit_meeting(in,authority,ledger,Arm::active,execution.work),"active tribute cannot release custody or an existing owned bag");}
            const auto command=advance(in,local,authority,ledger,execution);
            if(command.action==PhysicalAction::attack||command.action==PhysicalAction::move||command.action==PhysicalAction::tribute)
                check(execution.confirm(command,receipt(command)),"physical own action has a current consumed receipt");
            observe(in,local,ledger,execution);
            if(command.action==PhysicalAction::move){own.position=command.target;
                if(command.target==frozen.capture.tiles[frozen.capture.count-1]&&capture_round<0)capture_round=t+1;}
            if(command.action==PhysicalAction::tribute&&bank_round<0){bank_round=t;own.channel=p6::Channel::tribute;own.channel_valid=true;}
        }
        check(execution.work.bank_seen&&execution.work.renewal_seen&&!execution.work.active,"actor owns its actual bank endpoint and named productive renewal");
        if(lost_channel)check(applied&&authority.lost&&execution.work.bank_lower==12,"personally lost premise stops future custody while its current own bag still converts");
    }
};
__attribute__((noinline)) void real_transfer_and_role_proof(){
    auto old=std::make_unique<Fixture>(1),next=std::make_unique<Fixture>(2);
    next->cycle(0);check(!own_component(next->in,next->ledger),"one own cycle cannot qualify a successor's production and return");
    next->cycle(40);check(own_component(next->in,next->ledger),"two own exact cycles qualify the successor's own alternate return");
    old->cycle(80,true);old->cycle(120,true);check(current_organization(old->in,old->ledger),"original custodian has its own two full historical H cycles");
    old->ready(200,false);old->loss();next->ready(200,false);
    check(old->authority.lost&&decide(old->in,old->authority,old->ledger,Arm::active).organization==Organization::U,"real blocked old return stops new old-collector proposals");
    check(may_offer(next->in,next->authority,next->ledger,next->execution.work),"successor proposes only from its personal current component proof");
    check(!commit_meeting(next->in,next->authority,next->ledger,Arm::control,next->execution.work),"control keeps fixed original authority under identical safety");
    check(commit_meeting(old->in,old->authority,old->ledger,Arm::active,old->execution.work)&&
        commit_meeting(next->in,next->authority,next->ledger,Arm::active,next->execution.work),"both personally observe the same current safe physical meeting");
    check(old->in.frame.own.actor.bag==0&&next->in.frame.own.actor.bag==0&&!current_organization(next->in,next->ledger),"meeting transfers future duties without bags or full H qualification");
    const auto active=decide(next->in,next->authority,next->ledger,Arm::active),control=decide(next->in,next->authority,next->ledger,Arm::control);
    check(active.organization==Organization::H&&next->in.offers[active.option].plan.collector==2&&control.organization==Organization::U,"same feasible inputs change actual next region and collector responsibility");
    old->cycle(200);next->cycle(200,true);
    check(old->execution.work.plan.group==8&&old->execution.work.payout_lower==10&&next->execution.work.plan.group==7&&next->execution.work.payout_lower==12,
        "former custodian actually produces elsewhere while successor owns the next fixed-source payout");
    check(!current_organization(next->in,next->ledger),"old full proof and meeting cannot replace the second new membership cycle");
    next->cycle(240,true);check(current_organization(next->in,next->ledger),"two actual successor H cycles qualify their own new membership and role");
    check(old->ledger.state(old->in.supported_slot)->positive_completions==2&&old->ledger.state(old->in.supported_slot)->material_premise_changed,
        "retired original proof is retained and marked stale rather than reassigned");
}
__attribute__((noinline)) void missing_meeting_and_private_limits(){
    auto old=std::make_unique<Fixture>(1),next=std::make_unique<Fixture>(2);old->ready(0,true);refresh(old->in,old->authority,old->ledger);
    old->tick(64,7,p6::Lifecycle::living);old->in.own_access.clock.round=0;refresh(old->in,old->authority,old->ledger);
    check(!old->authority.lost&&!commit_meeting(old->in,old->authority,old->ledger,Arm::active,old->execution.work),"freshness timeout never becomes a real custody-loss meeting permission");
    next->cycle(0);next->ready(40,false);
    check(!may_offer(next->in,next->authority,next->ledger,next->execution.work)&&!commit_meeting(next->in,next->authority,next->ledger,Arm::active,next->execution.work),
        "visible meeting cannot borrow original or peer qualification after only one own cycle");
    next->cycle(40);next->ready(80,false);
    for(int a=0;a<next->in.frame.actor_count;++a)if(next->in.frame.actors[a].value.id==1)next->in.frame.actors[a].knowledge=p6::Knowledge::unknown;
    check(!commit_meeting(next->in,next->authority,next->ledger,Arm::active,next->execution.work),"remote old holder cannot transfer future custody");
    const auto fallback=decide(next->in,next->authority,next->ledger,Arm::active);
    check(fallback.private_income&&fallback.organization==Organization::U,"missing meeting preserves own safe productive fallback");
    next->ready(80,false);next->in.original_offer.clock.round=79;
    check(!commit_meeting(next->in,next->authority,next->ledger,Arm::active,next->execution.work),"stale willingness is not a current actual meeting");
    next->ready(80,false);const auto failures=old->ledger.lifetime_failure_count();
    check(!commit_meeting(next->in,next->authority,old->ledger,Arm::active,next->execution.work)&&old->ledger.lifetime_failure_count()==failures,
        "another actor's private ledger is neither qualification nor mutable input to this transfer");
}
__attribute__((noinline)) void held_bag_and_identity(){
    auto old=std::make_unique<Fixture>(1);old->cycle(0,true,true);
    auto off_route=std::make_unique<Fixture>(1);refresh(off_route->in,off_route->authority,off_route->ledger);
    off_route->in.own_access.value.return_executable.value=false;off_route->in.own_access.value.blocker_visible.value=true;off_route->in.own_access.value.blocked_tile={9,9};
    refresh(off_route->in,off_route->authority,off_route->ledger);
    check(!off_route->authority.lost,"off-route obstacle cannot retire the frozen current return premise");
    auto next=std::make_unique<Fixture>(2);next->cycle(0);next->cycle(40);next->ready(80,false);
    next->in.meeting.successor.bank=path({{3,0},{4,1},{5,1},{6,0}});
    check(!commit_meeting(next->in,next->authority,next->ledger,Arm::active,next->execution.work),"changed meeting route cannot reuse old visible signal or role context");
    const auto changed=meeting_hash(next->in.meeting);next->in.original_offer.value.meeting=changed;next->in.successor_offer.value.meeting=changed;
    check(own_component(next->in,next->ledger)&&!relevant_component(next->in,next->ledger)&&
        !may_offer(next->in,next->authority,next->ledger,next->execution.work)&&
        !commit_meeting(next->in,next->authority,next->ledger,Arm::active,next->execution.work),
        "fresh willingness cannot turn completed old-return cycles into relevant proof for a changed future route");
    check(champion_pool.size()==8,"draft uses the broader supported pool with role-specific tie preferences");
}
__attribute__((noinline)) void restored_access_control(){
    auto old=std::make_unique<Fixture>(1);old->cycle(0,true);old->cycle(40,true);old->ready(80,true);old->loss();
    const auto attempts=old->ledger.state(old->in.supported_slot)->attempts;
    old->ready(120,true);refresh(old->in,old->authority,old->ledger);
    const auto active=decide(old->in,old->authority,old->ledger,Arm::active),control=decide(old->in,old->authority,old->ledger,Arm::control);
    check(old->authority.lost&&active.organization==Organization::U&&control.organization==Organization::H&&!control.record_episode,
        "restored access lets fixed control resume its actual old duty while active sticky loss still stops original custody");
    old->cycle(120,true,false,Arm::control);
    const auto* retired=old->ledger.state(old->in.supported_slot);
    check(old->execution.work.payout_lower==12&&old->execution.work.bank_lower==12&&old->execution.work.renewal_seen,
        "restored-access control physically captures and converts its own next payout before productive renewal");
    check(retired->positive_completions==2&&retired->attempts==attempts&&retired->material_premise_changed&&!current_organization(old->in,old->ledger),
        "control continuation leaves old proof stale and retained without recording a new qualified episode");
}
}
int main(){real_transfer_and_role_proof();missing_meeting_and_private_limits();held_bag_and_identity();restored_access_control();
    std::printf("Custodial Authority: %d checks passed\n",checks);return 0;}
