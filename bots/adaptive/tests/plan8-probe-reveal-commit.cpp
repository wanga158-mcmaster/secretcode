#include "../probe-reveal-commit/src/policy.h"
#include "../probe-reveal-commit/src/draft.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <initializer_list>
using namespace p8;using namespace p8::probe_reveal_commit;
namespace {
int checks=0;void check(bool value,const char* message){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
template<class T> p6::Fact<T> fact(T value){return {value,p6::Source::synthetic_fixture};}
Route path(std::initializer_list<p6::Point> points){Route r;r.verified=true;for(auto p:points)r.tiles[r.count++]=p;return r;}
__attribute__((noinline)) Plan circuit(int actor,int group,bool h=false){
    Plan p;p.organization=h?Organization::H:group==6?Organization::G:Organization::U;p.group=group;p.collector=h?1:actor;p.crew[0]=p.collector;p.crew_count=1;
    if(h){p.crew[1]=3;p.crew_count=2;}const auto start=actor==1?p6::Point{2,0}:actor==2?p6::Point{0,2}:p6::Point{2,2};
    p.pads[0]={4,1};p.pads[1]={4,2};
    if(group==7){p.ingress=path({start});p.capture=path({start,{3,0}});p.bank=path({{3,0},{4,0}});p.renewal=path({{4,0},{4,1},{3,1},{2,0}});p.altar=50;p.forecast_rounds=13;}
    else if(group==10){p.ingress=h&&actor==3?path({start,{3,2},{4,2}}):path({start,{3,1},{4,1}});
        p.capture=h&&actor==3?path({{4,2}}):path({{4,1},{4,0}});p.bank=h&&actor==3?path({{4,2},{5,2},{6,2}}):path({{4,0},{5,1},{6,1},{6,2}});
        p.renewal=h&&actor==3?path({{6,2},{5,3},{4,3}}):path({{6,2},{5,2},{4,2},{3,1},{2,0}});
        if(h&&actor==3)p.release=path({{4,2},{4,3}});p.altar=63;p.forecast_rounds=h?(actor==3?40:20):21;}
    else if(group==6){p.ingress=path({start,{1,1},{1,2}});p.capture=path({{1,2},{2,2}});p.bank=path({{2,2},{2,3}});
        p.renewal=path({{2,3},{3,3},{3,2},{2,1},{2,0}});p.altar=24;p.forecast_rounds=18;}
    else if(group==8){p.ingress=path({start,{1,2},{1,3}});p.capture=path({{1,3},{1,4}});p.bank=path({{1,4},{0,3}});p.renewal=path({{0,3},{0,2}});p.altar=4;p.forecast_rounds=15;}
    else {p.ingress=path({start,{3,2},{4,3}});p.capture=path({{4,3},{5,3}});p.bank=path({{5,3},{6,4}});p.renewal=path({{6,4},{5,4},{4,3}});p.altar=63;p.forecast_rounds=17;}
    p.remaining_rounds=110;p.capture_window=5;p.gem_bearing=p.complete_verified=p.rally_legal=true;return p;
}
p6::CycleCertificate safety(const p6::Frame& f,int actor,int group,double incoming=600){p6::CycleCertificate c;c.clock=f.clock;c.actor=actor;c.group=group;
    if(const auto* a=f.visible(actor))c.clear=c.hold=c.bank=c.recovery={a->health,a->max_health,incoming,0,0,0,true};
    c.route_known=c.bank_executable=c.renewal_reachable=c.solo=true;return c;}
void event(p6::ActorSchedule& a,p6::EventKind kind,int id,int tick,int production=-1){p6::Event e;e.kind=kind;e.event_id=id;e.tick=tick;e.production_index=production;
    e.source=p6::Source::synthetic_fixture;e.gold_cost=fact<p6::Gems>(0);e.waypoint_id=kind==p6::EventKind::renewal?100+a.actor_id:-1;a.events[a.event_count++]=e;}
void production(p6::Schedule& s,p6::ActorSchedule& a,int id,int tick,p6::Gems gems){const int index=s.production_count++;auto& p=s.productions[index];p.event_id=id;p.tick=tick;p.gems=fact(gems);p.gold=fact<p6::Gems>(0);
    p.holders[0]=a.actor_id;p.holder_count=1;p.source=p6::Source::synthetic_fixture;event(a,p6::EventKind::production,id,tick,index);}
__attribute__((noinline)) p6::Schedule schedule(const p6::Frame& f,int option,int branch=-1){
    p6::Schedule s;s.actor_count=3;s.target_id=option==0?7:option==2?6:10;
    for(int id=1;id<=3;++id){auto& a=s.actors[id-1];a.actor_id=id;const bool own=id==f.clock.self;const auto* seen=f.visible(id);
        a.initial_bag={seen?seen->bag:0,own?p6::Source::self_observed:p6::Source::visible_subject};a.initial_gold={own?f.own.gold:0,own?p6::Source::self_observed:p6::Source::conservative};
        if(id==1){if(option==0){production(s,a,1007,8,2);event(a,p6::EventKind::tribute,2007,10);
                const int b=branch<0?2:branch;const int group=b==2?6:10,bank=b==0?29:b==1?30:27,end=b==0?33:b==1?34:31;
                production(s,a,1000+group,b==0?24:25,group==6?4:20);event(a,p6::EventKind::tribute,2000+group,bank);event(a,p6::EventKind::renewal,3001,end);a.occupied_turns=fact<p6::Gems>(end);}
            else{const bool h=option==3;const int group=option==2?6:10,bank=option==2?14:h?16:17,end=option==2?18:h?20:21;
                production(s,a,1000+group,option==2?12:h?11:12,group==6?4:20);event(a,p6::EventKind::tribute,2000+group,bank);event(a,p6::EventKind::renewal,3001,end);a.occupied_turns=fact<p6::Gems>(end);}}
        else if(id==2){if(a.initial_bag.value>0)event(a,p6::EventKind::tribute,2080,4);production(s,a,1008,20,10);event(a,p6::EventKind::tribute,2008,22);event(a,p6::EventKind::renewal,3002,25);a.occupied_turns=fact<p6::Gems>(25);}
        else{const bool h=option==3||(option==0&&branch==0);const int bank=h?(option==0?35:22):14;production(s,a,1009,bank-2,10);event(a,p6::EventKind::tribute,2009,bank);event(a,p6::EventKind::renewal,3003,bank+3);a.occupied_turns=fact<p6::Gems>(bank+3);}}
    return s;
}
enum class Rule{approach,stay,unobservable};
struct Reactive {
    Rule rule=Rule::approach;p6::Point position{10,3};int remaining=0,last_round=-1;bool visible=true,intervened=false;
    void action(const Command& c,const ActionReceipt& r){if(!r.attempted||!r.success)return;
        if(c.action==PhysicalAction::attack&&c.group==7){intervened=true;remaining=3;}
        else if(intervened&&c.action==PhysicalAction::tribute&&c.group==10)remaining=3;}
    void step(int round){if(last_round<0){last_round=round;return;}if(round<=last_round)return;last_round=round;
        if(remaining>0){if(rule==Rule::approach){if(position.x>7)--position.x;else if(position.x==7)position={6,2};else position={5,3};}
            else if(rule==Rule::unobservable)position={11,3};--remaining;}
        else if(position.x<10){++position.x;if(position.y==2&&position.x>=7)position.y=3;}
        else if(position.x>10)--position.x;
        visible=position!=p6::Point{11,3};}
};
// Exact static eight-neighbour distances on this small declared fixture map.
// Altars block movement. A bush at11,3 hides the departing rival; it does not
// change static movement distance. There are no walls or other obstructions.
int distance(p6::Point from,p6::Point target){std::array<int,128> d;d.fill(-1);std::array<p6::Point,128> queue{};int head=0,tail=0;
    auto blocked=[](p6::Point p){return p==p6::Point{6,3}||p==p6::Point{5,0}||p==p6::Point{2,4}||p==p6::Point{0,4};};
    d[target.y*16+target.x]=0;queue[tail++]=target;while(head<tail){const auto p=queue[head++];for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){p6::Point n{p.x+x,p.y+y};
            if((!x&&!y)||n.x<0||n.x>=16||n.y<0||n.y>=8||blocked(n)||d[n.y*16+n.x]>=0)continue;
            d[n.y*16+n.x]=d[p.y*16+p.x]+1;queue[tail++]=n;}}
    return d[from.y*16+from.x];}
ActionReceipt receipt(const Command& c){return {c.clock,c.clock,c.token,c.action,true,true};}
struct Fixture {
    Inputs in{};Protocol protocol;EvidenceLedger ledger;Execution execution{};LocalFrame local{};Reactive rival{};int self=1,window=12;
    __attribute__((noinline)) explicit Fixture(int actor=1,Rule rule=Rule::approach):protocol(actor),ledger(actor),self(actor){rival.rule=rule;
        in.frame.clock={0,self,0};in.settings={2000,1000,5};in.settings_identity={17,p6::Source::queried_setting};in.frame.own.gold=30;
        in.draft.champions={p6::Champion::tavi,p6::Champion::marcella,p6::Champion::varg,p6::Champion::other,p6::Champion::other};in.draft.actors={1,2,3,-1,-1};in.draft.picks=in.picks=3;
        for(int id=1;id<=3;++id){p6::Actor a;a.id=id;a.pick_ordinal=id-1;a.type=in.draft.champions[id-1];a.team=p6::Team::ally;a.health=a.max_health=2000;a.strength=id==3?40:20;
            a.attack_range=id==1?3:1;a.ranged=id==1;a.alive=a.can_move=a.can_attack=true;a.position=id==1?p6::Point{2,0}:id==2?p6::Point{0,2}:p6::Point{2,2};a.bag=id==2?17:0;
            if(id==self)in.frame.own.actor=a;else in.frame.actors[in.frame.actor_count++]={a,in.frame.clock,id,p6::Knowledge::exact};}
        p6::Actor enemy;enemy.id=9;enemy.type=p6::Champion::ren;enemy.team=p6::Team::enemy;enemy.health=enemy.max_health=1000;enemy.strength=20;enemy.attack_range=1;enemy.alive=enemy.can_move=true;enemy.position=rival.position;
        in.frame.actors[in.frame.actor_count++]={enemy,in.frame.clock,9,p6::Knowledge::exact};in.rival=9;
        in.own_state={{2000,2000,in.frame.own.actor.strength,30,1,in.frame.own.actor.bag,99},in.frame.clock,self,p6::Knowledge::exact};in.threat_regime={0,in.frame.clock,self,p6::Knowledge::exact};
        auto& layout=in.layout;layout.intervention=circuit(1,7);layout.supported=circuit(1,10,true);layout.helper=circuit(3,10,true);layout.independent=circuit(1,10);layout.generalist=circuit(1,6);
        layout.site={100,63,16,8,{6,3},{5,3},{{{5,3},{6,2}}},2,{77,p6::Source::public_map}};
        auto declaration=full_declaration();check(protocol.declare(layout,declaration,in.draft,ledger),"fixed actor0/2 roles and actual routes are declared before any intervention");
        in.supported_slot=protocol.slot;if(self==1)in.supported_entry=declaration.key;in.target_group=10;in.intervention=0;in.independent=1;in.offer_count=4;
        configure();tick(0,7,p6::Lifecycle::living);
    }
    ContextDeclaration full_declaration(){ContextDeclaration d;auto& k=d.key;k.settings_id=17;k.route_hash=plan_route_hash(in.layout.supported);k.return_route_hash=plan_return_hash(in.layout.supported);
        k.actor_id=1;k.champion=int(p6::Champion::tavi);k.role=1;k.source_group=10;k.route_template=0;k.endpoint=63;k.threat_regime=0;k.participants={1,3,-1,-1,-1,-1};k.participant_count=2;k.build={99,2000,20,30,1};
        d.kind=EpisodeKind::full_organization;d.require_supported_work=true;d.envelope={{700,2000},{2000,2000},{20,20},{30,30},{1,1},{0,20},0};for(int h=0;h<3;++h)d.hypotheses[h].offset={2*h,2*h+2};return d;}
    __attribute__((noinline)) void configure(){
        in.offers[0].plan=self==1?in.layout.intervention:circuit(self,self==2?8:9);
        in.offers[1].plan=self==1?in.layout.independent:in.offers[0].plan;in.offers[2].plan=self==1?in.layout.generalist:in.offers[0].plan;
        in.offers[3].plan=self==1?in.layout.supported:in.layout.helper;
        if(self==1){in.offers[3].context_slot=protocol.slot;in.offers[3].entry=protocol.organization.key;}
        for(int o=0;o<4;++o){in.offers[o].schedule=schedule(in.frame,o);in.offers[o].schedule.target_id=in.offers[o].plan.group;}
        auto& f=in.forecast;f.lowload_event=1007;f.lowload_gems=fact<p6::Gems>(2);f.lowload_gold=fact<p6::Gems>(0);f.endpoint_distinguishes=true;
        for(int b=0;b<3;++b){f.branches[b]=schedule(in.frame,0,b);f.lowload_end[b]=f.continuation_start[b]=13;f.continuation_event[b]=b==2?1006:1010;f.continuation_gems[b]=fact<p6::Gems>(b==2?4:20);f.continuation_gold[b]=fact<p6::Gems>(0);}
        auto& own=in.own_income.value;own=in.offers[1];own.schedule={};own.schedule.target_id=own.plan.group;own.schedule.actor_count=1;
        const auto& represented=in.offers[1].schedule.actors[self-1];own.schedule.actors[0]=represented;
        for(int e=0;e<own.schedule.actors[0].event_count;++e){auto& event=own.schedule.actors[0].events[e];if(event.kind==p6::EventKind::production){const auto p=in.offers[1].schedule.productions[event.production_index];event.production_index=own.schedule.production_count;own.schedule.productions[own.schedule.production_count++]=p;}}
    }
    __attribute__((noinline)) void observe_plan(const Plan& plan){local.frame=in.frame;local.current_round=in.frame.clock.round;local.milestones={};local.safety_count=plan.crew_count;
        for(int i=0;i<plan.crew_count;++i)local.safety[i]=safety(in.frame,plan.crew[i],plan.group);
        local.legal={{},in.frame.clock,self,p6::Knowledge::exact};auto& legal=local.legal.value;legal.safe_now=legal.attack_legal=legal.altar_legal=legal.recall_safe=legal.renewal_productive=true;
        for(Leg leg:{Leg::ingress,Leg::capture,Leg::bank,Leg::renewal,Leg::release}){const auto& route=route_for(plan,leg);auto& a=legal.routes[int(leg)];a.assumptions_hold=route_valid(route);a.next_move_legal=true;
            for(int i=0;i<route.count;++i)if(route.tiles[i]==in.frame.own.actor.position){a.next_tile=route.tiles[std::min(i+1,route.count-1)];break;}}}
    __attribute__((noinline)) void tick(int round,int group,p6::Lifecycle lifecycle){rival.step(round);auto& f=in.frame;f.clock.round=round;
        const bool enemy_visible=rival.visible&&std::max(std::abs(rival.position.x-f.own.actor.position.x),std::abs(rival.position.y-f.own.actor.position.y))<=8;
        for(int a=0;a<f.actor_count;++a){auto& seen=f.actors[a];seen.clock=f.clock;if(seen.subject==9){seen.value.position=rival.position;seen.knowledge=enemy_visible?p6::Knowledge::exact:p6::Knowledge::unknown;}}
        f.own.at_spawn=f.own.actor.position==(self==1?p6::Point{2,0}:self==2?p6::Point{0,2}:p6::Point{2,2});
        p6::Group camp;camp.id=group;camp.lifecycle=lifecycle;camp.original_spawn_visible=camp.all_spawns_visible=camp.uncontested_now=true;f.group={camp,f.clock,group,p6::Knowledge::exact};
        in.own_state.clock=in.threat_regime.clock=f.clock;in.own_state.value.bag=f.own.actor.bag;
        in.windows.valid=true;in.windows.source=p6::Source::synthetic_fixture;in.windows.sampled_round=round;in.windows.sampled_actor_id=self;in.windows.windows=in.windows.deadlines={window,window,window};
        for(int o=0;o<4;++o){in.offers[o].safety_count=3;for(int id=1;id<=3;++id)in.offers[o].safety[id-1]=safety(f,id,common::participant(in.offers[o].plan,id)?in.offers[o].plan.group:id==2?8:9);}
        for(int b=0;b<3;++b){in.forecast.safety_count[b]=3;for(int id=1;id<=3;++id)in.forecast.safety[b][id-1]=safety(f,id,id==1?(b==2?6:10):id==2?8:9,800);}
        in.distance={{100,77,rival.position,{5,3},{distance(rival.position,{5,3}),p6::Source::public_map},true,true},f.clock,9,enemy_visible?p6::Knowledge::exact:p6::Knowledge::unknown};
        in.collector_proposal={10,f.clock,1,p6::Knowledge::exact};in.own_income.clock=f.clock;in.own_income.subject=self;in.own_income.knowledge=p6::Knowledge::exact;
        in.own_income.value.safety_count=1;in.own_income.value.safety[0]=safety(f,self,in.own_income.value.plan.group);
        observe_plan(execution.work.active?execution.work.plan:in.offers[0].plan);
    }
    __attribute__((noinline)) void ready(int round,bool continue_work=false){in.frame.own.actor.position=self==1?p6::Point{2,0}:self==2?p6::Point{0,2}:p6::Point{2,2};
        in.frame.own.actor.bag=0;in.frame.own.actor.channel=p6::Channel::none;in.frame.own.actor.spawn_group=-1;
        for(int a=0;a<in.frame.actor_count;++a)if(in.frame.actors[a].subject<=3){auto& actor=in.frame.actors[a].value;actor.position=actor.id==2?p6::Point{0,2}:p6::Point{4,2};actor.bag=actor.id==2?17:0;actor.channel=p6::Channel::none;actor.spawn_group=-1;actor.action=p6::WitnessAction::none;}
        configure();if(continue_work){in.offers[0].plan.complete_verified=false;window=16;}tick(round,continue_work?10:7,p6::Lifecycle::living);}
    __attribute__((noinline)) void run(int start,int expected,Arm arm=Arm::active,bool continuation=true){ready(start,continuation);
        const auto selected=decide(in,protocol,ledger,arm);check(selected.option==expected||(self!=1&&selected.organization==Organization::U),"current response chooses its declared distinct complete physical organization");
        const auto frozen=in.offers[expected].plan;observe_plan(frozen);check(commit(in,local,selected,protocol,ledger,arm,execution).started,"current visible actual membership starts the retained physical Work");
        const int clear=frozen.group==7?1:frozen.organization==Organization::H?4:5;
        int capture=-1,payout=-1,bank=-1;
        for(int t=0;t<95&&execution.work.active;++t){auto& own=in.frame.own.actor;own.spawn_group=own.position==frozen.capture.tiles[frozen.capture.count-1]&&t>=clear?frozen.group:-1;
            if(capture>=0&&t==capture+6){own.bag=frozen.group==7?2:frozen.group==10?20:frozen.group==6?4:10;payout=t;}
            if(bank>=0&&t==bank+(frozen.group==10?2:1)){own.bag=0;own.channel=p6::Channel::none;}
            tick(start+t,frozen.group,t<clear?p6::Lifecycle::living:payout<0?p6::Lifecycle::reward_available:p6::Lifecycle::respawning);
            if(frozen.organization==Organization::H)for(int a=0;a<in.frame.actor_count;++a)if(in.frame.actors[a].subject==3){auto& helper=in.frame.actors[a].value;helper.action=t==2?p6::WitnessAction::useful_attack:p6::WitnessAction::none;
                helper.action_group=frozen.group;helper.position=t<=clear?frozen.pads[1]:p6::Point{4,3};helper.spawn_group=-1;local.frame=in.frame;}
            if(payout==t)local.milestones={{frozen.group,true,false,own.bag,0},in.frame.clock,self,p6::Knowledge::exact};
            if(bank>=0&&t==bank+(frozen.group==10?2:1))local.milestones={{frozen.group,false,true,0,frozen.group==7?2:frozen.group==10?20:frozen.group==6?4:10},in.frame.clock,self,p6::Knowledge::exact};
            const auto command=advance(in,local,protocol,ledger,arm,execution);
            if(command.token){const auto actual=receipt(command);check(execution.confirm(in,protocol,command,actual),"current command and its actual receipt are consumed once");rival.action(command,actual);}
            observe(in,local,protocol,ledger,execution);
            if(command.action==PhysicalAction::move){own.position=command.target;if(command.target==frozen.capture.tiles[frozen.capture.count-1]&&capture<0)capture=t+1;}
            if(command.action==PhysicalAction::tribute&&bank<0){bank=t;own.channel=p6::Channel::tribute;own.channel_valid=true;}}
        check(execution.work.payout_seen&&execution.work.bank_seen&&execution.work.renewal_seen&&!execution.work.active,"branch owns its actual production, conversion and named productive renewal");
    }
    __attribute__((noinline)) void protect_bag(){
        tick(0,8,p6::Lifecycle::respawning);auto& existing=in.existing_inventory.value;existing.plan=circuit(2,8);
        existing.plan.ingress=existing.plan.capture=path({{0,2}});existing.plan.bank=path({{0,2},{0,3}});existing.plan.renewal=path({{0,3},{1,3},{1,2},{0,2}});existing.plan.forecast_rounds=14;
        existing.schedule.target_id=8;existing.schedule.actor_count=1;auto& own=existing.schedule.actors[0];own.actor_id=2;own.initial_bag={17,p6::Source::self_observed};own.initial_gold={30,p6::Source::self_observed};
        own.occupied_turns=fact<p6::Gems>(14);event(own,p6::EventKind::tribute,4002,4);event(own,p6::EventKind::renewal,4003,7);
        existing.safety_count=1;existing.safety[0]=safety(in.frame,2,8);in.existing_inventory.clock=in.frame.clock;in.existing_inventory.subject=2;in.existing_inventory.knowledge=p6::Knowledge::exact;
        const auto selected=decide(in,protocol,ledger,Arm::active);check(selected.secure_inventory&&selected.option<0,"independent ordinal1 never risks its real seventeen-gem bag for the intervention or H");
        const auto frozen=existing.plan;observe_plan(frozen);check(commit(in,local,selected,protocol,ledger,Arm::active,execution).started,"protected owned bag starts its own actual bank route");int bank=-1;
        for(int t=0;t<45&&execution.work.active;++t){auto& actor=in.frame.own.actor;if(bank>=0&&t==bank+2){actor.bag=0;actor.channel=p6::Channel::none;}
            tick(t,8,p6::Lifecycle::respawning);if(bank>=0&&t==bank+2)local.milestones={{8,false,true,0,17},in.frame.clock,2,p6::Knowledge::exact};
            const auto command=advance(in,local,protocol,ledger,Arm::active,execution);if(command.token)check(execution.confirm(in,protocol,command,receipt(command)),"protected producer consumes its own movement/channel receipt");
            observe(in,local,protocol,ledger,execution);if(command.action==PhysicalAction::move)actor.position=command.target;
            if(command.action==PhysicalAction::tribute&&bank<0){bank=t;actor.channel=p6::Channel::tribute;actor.channel_valid=true;}}
        check(execution.work.bank_lower==17&&execution.work.renewal_seen&&!execution.work.active&&!protocol.issued&&ledger.size()==0,
            "seventeen protected gems actually convert and renew without becoming a probe or another actor's proof");
    }
};
__attribute__((noinline)) void reactive_branches(){
    for(Rule rule:{Rule::approach,Rule::stay,Rule::unobservable}){auto f=std::make_unique<Fixture>(1,rule);
        const auto before=decide(f->in,f->protocol,f->ledger,Arm::active);check(before.intervention&&!before.deliberate_probe,"unchanged productive comparator selects ordinary low-load work without a diagnostic detour");
        check(!f->protocol.responses.window.active&&f->protocol.action_token==0,"response window cannot exist before the actual productive intervention");
        f->run(0,0,Arm::active,false);check(f->rival.intervened&&f->protocol.action_token&&f->protocol.intervention_complete&&f->protocol.budget.used==0,
            "confirmed empty-bag attack precedes actual three-observation response and completed low-load income without fake probe charges");
        const auto expected=rule==Rule::approach?Response::approach:rule==Rule::stay?Response::stay:Response::unknown;
        check(f->protocol.initial==expected,"legal local reactive movement distinguishes approach, continuous nonapproach and lost observation");
        const int option=rule==Rule::approach?3:rule==Rule::stay?1:2;f->run(16,option);
        check(f->execution.work.plan.organization==(rule==Rule::approach?Organization::H:rule==Rule::stay?Organization::U:Organization::G),"observed branch changes physical membership or geography rather than only a classifier");
        check(!common::participant(f->execution.work.plan,2),"the independent producer and its bag never become a dependent H member");
        if(rule==Rule::approach){check(!full_current(f->in,f->protocol,f->ledger),"one actual H after response cannot inherit full organization qualification");f->run(38,3);
            check(full_current(f->in,f->protocol,f->ledger)&&f->protocol.responses.matching()>=2,"two actual H cycles and later passive ordinary tribute encounters establish separate full and repeated-response gates");
            auto one_response=std::make_unique<Fixture>(*f);one_response->ready(60,true);one_response->protocol.responses.clear_permission();
            check(full_current(one_response->in,one_response->protocol,one_response->ledger)&&decide(one_response->in,one_response->protocol,one_response->ledger,Arm::active).organization!=Organization::H,
                "full organization qualification alone cannot replace the separate matching-response permission for repeated H");
            f->ready(60,true);check(!decide(f->in,f->protocol,f->ledger,Arm::active).intervention,"ordinary intervention identity remains once-only even with unspent deliberate slots");}}
    auto control=std::make_unique<Fixture>();control->run(0,3,Arm::control,false);
    check(!control->rival.intervened&&!control->protocol.issued&&control->protocol.responses.encounters()==0,"control takes the same currently safe valuable deployment directly without eliciting action or manufactured response evidence");
    auto independent=std::make_unique<Fixture>(2);independent->protect_bag();independent->run(8,0,Arm::active,false);
    check(independent->execution.work.plan.group==8&&independent->execution.work.bank_lower==10&&!independent->protocol.issued,
        "independent ordinal1 continues actual own production, bank and renewal alongside the intervention timeline");
    auto helper=std::make_unique<Fixture>(3);helper->run(0,0,Arm::active,false);
    check(helper->execution.work.plan.group==9&&helper->execution.work.bank_lower==10&&helper->ledger.size()==0,"helper core has a legal own first-income path independent of another actor's response proof");
}
__attribute__((noinline)) void honest_paid_and_identity_boundaries(){
    auto f=std::make_unique<Fixture>();f->window=20;f->tick(0,7,p6::Lifecycle::living);const auto paid=decide(f->in,f->protocol,f->ledger,Arm::active);
    check(!paid.intervention&&paid.option!=0&&f->protocol.budget.used==0,"a lower-ranked low-load intervention whose minimum branch loses is honestly unavailable and spends nothing");
    check(f->protocol.budget.spend(0)&&f->protocol.budget.spend(1),"retained earlier deliberate charges exhaust the persistent two-per-match cap");
    f->tick(128,7,p6::Lifecycle::living);check(!f->protocol.budget.available(2)&&!decide(f->in,f->protocol,f->ledger,Arm::active).intervention,"epoch changes cannot replenish prior deliberate probe charges");
    auto raw=std::make_unique<Fixture>();check(raw->protocol.responses.begin(raw->in.frame,raw->in.layout.site,9,raw->in.distance),"raw passive observer can independently observe a baseline");
    for(int t=1;t<=3;++t){raw->rival.position.x=10-t;raw->tick(t,7,p6::Lifecycle::living);raw->protocol.responses.observe(raw->in.frame,raw->in.distance);}
    check(!collector_allowed(raw->in,raw->protocol,raw->ledger,raw->in.offers[3]),"a raw15 APPROACH window without a preceding bound intervention cannot authorize16 deployment");
    auto altered=std::make_unique<Fixture>();altered->in.forecast.branches[2].actors[1].events[0].tick=5;
    check(!decide(altered->in,altered->protocol,altered->ledger,Arm::active).intervention,"probe branches cannot silently delay the independent actor's protected existing bag");
    check(champion_pool.size()==8,"draft honors the wider role-specific supported pool");
}
__attribute__((noinline)) void diagnostic_physical_cap(){
    auto f=std::make_unique<Fixture>();const auto frozen=f->in.layout.intervention;f->observe_plan(frozen);
    // Exercise the physical cap after an earlier admission, independently of
    // the deliberately losing paid packet above. This is no paid gain claim.
    check(f->protocol.budget.spend(0)&&f->execution.work.start(frozen,f->local),"previously admitted diagnostic has a retained charge and actual empty-bag Work");
    f->protocol.bind(f->execution.work,true);
    for(int t=0;t<8;++t){f->tick(t,7,p6::Lifecycle::living);f->local.legal.value.attack_legal=false;
        const auto command=advance(f->in,f->local,f->protocol,f->ledger,Arm::active,f->execution);
        check(command.action==PhysicalAction::hold&&f->execution.confirm(f->in,f->protocol,command,receipt(command)),"delayed productive readiness consumes an actual bounded diagnostic hold");
        observe(f->in,f->local,f->protocol,f->ledger,f->execution);
        const auto duplicate=advance(f->in,f->local,f->protocol,f->ledger,Arm::active,f->execution);
        observe(f->in,f->local,f->protocol,f->ledger,f->execution);
        check(!duplicate.token&&f->protocol.marked_turns==t+1,"duplicate snapshot neither issues a second action nor double-charges its marked turn");}
    f->tick(8,7,p6::Lifecycle::living);const auto ninth=advance(f->in,f->local,f->protocol,f->ledger,Arm::active,f->execution);
    check(ninth.action!=PhysicalAction::attack&&ninth.action!=PhysicalAction::move&&f->protocol.marked_turns==8&&f->protocol.intervention_failed,
        "ninth eliciting attack is stopped before dispatch when readiness returns after eight actual marked turns");
    if(ninth.token)check(f->execution.confirm(f->in,f->protocol,ninth,receipt(ninth)),"safe fallback receipt remains valid after closing the diagnostic segment");
    observe(f->in,f->local,f->protocol,f->ledger,f->execution);
    check(!f->protocol.action_token&&!f->protocol.responses.window.active&&f->protocol.initial==Response::unknown&&f->protocol.budget.used==1,
        "capped actual work retains its spent slot and cannot manufacture intervention or response permission");
}
__attribute__((noinline)) void unknown_channel_and_expiry(){
    auto f=std::make_unique<Fixture>();f->run(0,0,Arm::active,false);f->rival.rule=Rule::unobservable;f->run(16,3);
    check(f->protocol.responses.rule()==Response::unknown&&f->execution.work.bank_lower==20&&f->execution.work.renewal_seen&&!full_current(f->in,f->protocol,f->ledger),
        "lost passive observation stops new dependent evidence while actual safe tribute, owned twenty gems and renewal finish");
    f->ready(40,true);check(decide(f->in,f->protocol,f->ledger,Arm::active).organization==Organization::G,
        "unknown rival response changes the next actual proposal to the declared generalist geography");
    f->in.offers[2].plan.complete_verified=false;f->in.own_income.knowledge=p6::Knowledge::unknown;
    check(decide(f->in,f->protocol,f->ledger,Arm::active).option<0,"an unavailable named generalist fallback cannot silently revive an unrelated independent proposal");
    auto expiring=std::make_unique<Fixture>();expiring->run(0,0,Arm::active,false);expiring->ready(50,true);
    check(decide(expiring->in,expiring->protocol,expiring->ledger,Arm::active).organization!=Organization::H,
        "an initial response cannot fund a new H whose worst surviving duration crosses its original strict sixty-four-round expiry");
}
}
int main(){reactive_branches();honest_paid_and_identity_boundaries();diagnostic_physical_cap();unknown_channel_and_expiry();std::printf("Probe Reveal Commit: %d checks passed\n",checks);return 0;}
