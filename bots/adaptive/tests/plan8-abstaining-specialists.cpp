#include "../abstaining-specialists/src/policy.h"
#include "../abstaining-specialists/src/draft.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <initializer_list>
using namespace p8;using namespace p8::abstaining_specialists;
namespace {
int checks=0;
void check(bool value,const char* message){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
template<class T> p6::Fact<T> fact(T value){return {value,p6::Source::synthetic_fixture};}
Route path(std::initializer_list<p6::Point> points){Route r;r.verified=true;for(auto p:points)r.tiles[r.count++]=p;return r;}
__attribute__((noinline)) Plan circuit(int actor,int group,bool h=false){
    Plan p;p.organization=h?Organization::H:Organization::G;p.group=group;p.collector=h?1:actor;p.crew[0]=p.collector;p.crew_count=1;
    if(h){p.crew[1]=2;p.crew_count=2;}
    const auto start=actor==1?p6::Point{2,0}:actor==2?p6::Point{2,1}:p6::Point{2,2};
    p.pads[0]=group==8?p6::Point{4,2}:p6::Point{2,0};p.pads[1]=group==8?p6::Point{4,1}:p6::Point{2,1};
    if(group==7){p.ingress=path({start});p.capture=h&&actor!=1?path({start}):path({start,{3,0}});
        p.bank=actor==2?path({start,{3,1},{4,0}}):path({{3,0},{4,0}});p.renewal=path({{4,0},{4,1},{3,1},{2,0}});p.altar=4;p.forecast_rounds=h?14:16;}
    else if(group==8){p.ingress=actor==2?path({start,{3,1},{4,1}}):path({start,{3,1},{4,2}});
        p.capture=actor==2?path({{4,1}}):path({{4,2},{5,2}});p.bank=actor==2?path({{4,1},{5,1},{6,2}}):path({{5,2},{6,2}});
        p.renewal=path({{6,2},{6,3},{5,3},{4,3},{3,2},{2,1},{2,0}});p.altar=26;p.forecast_rounds=h?19:21;}
    else if(group==6){p.ingress=path({start,{3,0},{4,0},{5,1}});p.capture=path({{5,1},{6,1}});p.bank=path({{6,1},{7,1}});
        p.renewal=path({{7,1},{7,2},{6,2},{5,2},{4,1},{3,0},{2,0}});p.altar=17;p.forecast_rounds=24;}
    else if(group==10){p.ingress=path({start,{1,3},{1,4}});p.capture=path({{1,4},{1,5}});p.bank=path({{1,5},{2,5}});
        p.renewal=path({{2,5},{3,5},{3,4},{2,3},{2,2}});p.altar=52;p.forecast_rounds=20;}
    else {p.ingress=path({start,{3,2},{4,3}});p.capture=path({{4,3},{5,3}});p.bank=path({{5,3},{6,3}});
        p.renewal=path({{6,3},{6,4},{5,4},{4,4},{3,3},{2,2},{2,1}});p.altar=36;p.forecast_rounds=22;}
    if(h&&actor==2){p.release=group==8?path({{4,1},{4,2},{4,3}}):path({start,{3,2},{4,3}});p.forecast_rounds=40;}
    p.remaining_rounds=110;p.capture_window=5;p.gem_bearing=p.complete_verified=p.rally_legal=true;return p;
}
p6::CycleCertificate safety(const p6::Frame& f,int actor,int group){p6::CycleCertificate c;c.clock=f.clock;c.actor=actor;c.group=group;
    if(const auto* a=f.visible(actor))c.clear=c.hold=c.bank=c.recovery={a->health,a->max_health,10,0,0,0,true};
    c.route_known=c.bank_executable=c.renewal_reachable=c.solo=true;return c;}
void event(p6::ActorSchedule& a,p6::EventKind kind,int id,int tick,int production=-1,p6::Gems cost=0){p6::Event e;e.kind=kind;e.event_id=id;e.tick=tick;e.production_index=production;
    e.gold_cost=fact(cost);e.source=p6::Source::synthetic_fixture;e.waypoint_id=kind==p6::EventKind::renewal?100+a.actor_id:-1;a.events[a.event_count++]=e;}
__attribute__((noinline)) p6::Schedule schedule(const p6::Frame& f,int own_group,bool h=false,bool purchase=false,int own_delay=0){
    p6::Schedule s;s.target_id=own_group;s.actor_count=s.production_count=3;
    for(int id=1;id<=3;++id){auto& a=s.actors[id-1];a.actor_id=id;const bool self=id==f.clock.self;
        a.initial_bag={f.visible(id)?f.visible(id)->bag:0,self?p6::Source::self_observed:p6::Source::visible_subject};
        a.initial_gold={self?f.own.gold:0,self?p6::Source::self_observed:p6::Source::conservative};
        const int group=id==1?own_group:id==2?9:10;
        // Named fixed sources: 7 pays12, 8 pays14, 6 pays8. Source6's
        // 45 health needs three20-damage attacks or two22.5-damage attacks.
        // The purchase itself does not consume the ordinary move/attack.
        const int bank=id==1?(own_group==7?(h?11:13):own_group==8?(h?13:15):(f.visible(1)->strength>20?14:15))+own_delay:id==2?(h?21:14):26;
        auto& p=s.productions[id-1];p.event_id=1000+group;p.tick=bank-(group==7||group==8?3:2);p.holders[0]=id;p.holder_count=1;
        p.gems=fact<p6::Gems>(group==7?12:group==8?14:group==6?8:10);p.gold=fact<p6::Gems>(0);p.source=p6::Source::synthetic_fixture;
        if(purchase&&id==1)event(a,p6::EventKind::purchase,5000,0,-1,30);
        event(a,p6::EventKind::production,p.event_id,p.tick,id-1);event(a,p6::EventKind::tribute,2000+group,bank);event(a,p6::EventKind::renewal,3000+id,bank+6);
        a.occupied_turns=fact<p6::Gems>(bank+12);}
    return s;
}
ActionReceipt receipt(const Command& c){return {c.clock,c.clock,c.token,c.action,true,true};}
struct Fixture {
    Inputs in{};Specialist state;EvidenceLedger ledger;Execution execution{};LocalFrame local{};
    int self=1;Inventory inventory{};
    __attribute__((noinline)) explicit Fixture(int actor=1):state(actor),ledger(actor),self(actor){
        // The ordinary G damage bundle is already actually owned at entry;
        // neither carry trial needs a specialist-only purchase for first income.
        inventory.count[damage_item]=1;
        in.frame.clock={0,self,0};in.frame.own.gold=90;in.settings={2000,1000,5};in.settings_identity={17,p6::Source::queried_setting};
        in.draft.champions={p6::Champion::prism,p6::Champion::varg,p6::Champion::marcella,p6::Champion::other,p6::Champion::other};
        in.draft.actors={1,2,3,-1,-1};in.draft.picks=in.picks=3;
        for(int id=1;id<=3;++id){p6::Actor a;a.id=id;a.pick_ordinal=id-1;a.type=in.draft.champions[id-1];a.team=p6::Team::ally;
            a.health=a.max_health=100;a.strength=id==2?40:20;a.attack_range=id==1?3:1;a.ranged=id==1;a.alive=a.can_move=a.can_attack=true;
            a.position=id==1?p6::Point{2,0}:id==2?p6::Point{2,1}:p6::Point{2,2};
            if(id==self)in.frame.own.actor=a;else in.frame.actors[in.frame.actor_count++]={a,in.frame.clock,id,p6::Knowledge::exact};}
        in.own_state={{100,100,20,30,2.5,0,inventory.hash()},in.frame.clock,self,p6::Knowledge::exact};in.threat_regime={0,in.frame.clock,self,p6::Knowledge::exact};
        in.target_group=7;in.offer_count=self==1?4:3;in.independent=0;
        if(self==1){auto h=std::make_unique<std::array<Configuration,2>>();auto g=std::make_unique<std::array<Configuration,2>>();
            for(int i=0;i<2;++i){auto& c=(*h)[i];c.plan=circuit(1,i?8:7,true);c.inventory=inventory;if(i)c.inventory.count[focus_item]=1;
                auto d=declaration(c.plan,c.inventory,i?38:30,20);c.context=ledger.predeclare(d);c.entry=d.key;check(c.context>=0,"actual Prism has separate predeclared cheap carry and purchased H contexts");}
            for(int i=0;i<2;++i){auto& c=(*g)[i];c.plan=circuit(1,6);c.inventory=inventory;++c.inventory.count[damage_item];if(i)c.inventory.count[focus_item]=1;
                c.entry=declaration(c.plan,c.inventory,i?38:30,22.5).key;}
            check(state.declare(*h,*g,ledger),"specialization freezes two H builds and generalist additions without inventing Focus resale");}
        configure(7);tick(0,7,p6::Lifecycle::living);
    }
    __attribute__((noinline)) ContextDeclaration declaration(const Plan& p,const Inventory& items,double focus,double strength){ContextDeclaration d;auto& k=d.key;
        k.settings_id=17;k.route_hash=plan_route_hash(p);k.return_route_hash=plan_return_hash(p);k.actor_id=self;k.champion=int(in.draft.champions[self-1]);k.role=p.organization==Organization::H?1:0;
        k.source_group=p.group;k.route_template=0;k.endpoint=p.altar;k.threat_regime=0;k.participants=p.crew;k.participant_count=p.crew_count;
        std::sort(k.participants.begin(),k.participants.begin()+k.participant_count);k.build={items.hash(),100,strength,focus,2.5};d.kind=EpisodeKind::full_organization;d.require_supported_work=true;
        const double largest=p.group==8?14:12;d.envelope={{75,100},{100,100},{strength,strength},{focus,focus+largest},{2.5,2.5},{0,largest},0};
        for(int h=0;h<3;++h)d.hypotheses[h].offset={2*h,2*h+2};return d;}
    __attribute__((noinline)) void configure(int general_group){
        in.offers[0].plan=circuit(self,self==1?general_group:self==2?9:10);in.offers[0].context_slot=-1;in.offers[0].entry={};
        for(int o=1;o<3;++o){in.offers[o].plan=circuit(self,o==1?7:8,true);if(self==1){in.offers[o].context_slot=state.supported[o-1].context;in.offers[o].entry=state.supported[o-1].entry;}}
        if(self==1)in.offers[3].plan=circuit(1,8);
        for(int o=0;o<in.offer_count;++o){in.offers[o].schedule=schedule(in.frame,self==1?in.offers[o].plan.group:7,o==1||o==2);in.offers[o].schedule.target_id=in.offers[o].plan.group;}
        auto& own=in.private_income.value;own=in.offers[0];own.schedule={};own.schedule.target_id=own.plan.group;own.schedule.actor_count=own.schedule.production_count=1;
        own.schedule.actors[0]=in.offers[0].schedule.actors[self-1];own.schedule.productions[0]=in.offers[0].schedule.productions[self-1];
        for(int e=0;e<own.schedule.actors[0].event_count;++e)if(own.schedule.actors[0].events[e].production_index>=0)own.schedule.actors[0].events[e].production_index=0;
    }
    __attribute__((noinline)) void observe_plan(const Plan& plan){local.frame=in.frame;local.current_round=in.frame.clock.round;local.milestones={};local.safety_count=plan.crew_count;
        for(int i=0;i<plan.crew_count;++i)local.safety[i]=safety(in.frame,plan.crew[i],plan.group);
        local.legal={{},in.frame.clock,self,p6::Knowledge::exact};auto& legal=local.legal.value;legal.safe_now=legal.attack_legal=legal.altar_legal=legal.renewal_productive=legal.recall_safe=true;
        for(Leg leg:{Leg::ingress,Leg::capture,Leg::bank,Leg::renewal,Leg::release}){const auto& route=route_for(plan,leg);auto& a=legal.routes[int(leg)];a.assumptions_hold=route_valid(route);a.next_move_legal=true;
            for(int i=0;i<route.count;++i)if(route.tiles[i]==in.frame.own.actor.position){a.next_tile=route.tiles[std::min(i+1,route.count-1)];break;}}}
    __attribute__((noinline)) void tick(int round,int group,p6::Lifecycle lifecycle){auto& f=in.frame;f.clock.round=round;for(int a=0;a<f.actor_count;++a)f.actors[a].clock=f.clock;
        f.own.at_spawn=f.own.actor.position==(self==1?p6::Point{2,0}:self==2?p6::Point{2,1}:p6::Point{2,2});
        p6::Group camp;camp.id=group;camp.lifecycle=lifecycle;camp.original_spawn_visible=camp.all_spawns_visible=camp.uncontested_now=true;f.group={camp,f.clock,group,p6::Knowledge::exact};
        in.own_state.clock=in.threat_regime.clock=f.clock;in.own_state.value.bag=f.own.actor.bag;in.own_state.value.focus=30+8*inventory.count[focus_item]+(self==1?double(f.own.actor.bag):0);
        in.own_state.value.strength=f.own.actor.strength;in.own_state.value.equipment_hash=inventory.hash();
        in.windows.valid=true;in.windows.source=p6::Source::synthetic_fixture;in.windows.sampled_actor_id=self;in.windows.sampled_round=round;
        in.windows.windows=in.windows.deadlines={group==7?12:14,group==7?12:14,group==7?12:14};
        for(int o=0;o<in.offer_count;++o){auto& offer=in.offers[o];offer.safety_count=3;for(int id=1;id<=3;++id)offer.safety[id-1]=safety(f,id,common::participant(offer.plan,id)?offer.plan.group:id==2?9:10);}
        if(self==1)for(int i=0;i<2;++i){const auto& c=state.supported[i];auto& obs=in.loaded[i];obs={{},f.clock,self,p6::Knowledge::exact};obs.value.context=c.entry;obs.value.focus_per_gem={1,p6::Source::queried_setting};
            for(int j=0;j<3;++j){auto& p=obs.value.points[j];p.bag=j==1?(i?14:12):0;p.focus=c.entry.build.focus+double(p.bag);p.cumulative={75,100,10.0*j,0,0,0,true};}}
        if(self==1)for(int i=0;i<2;++i){const auto& plan=i==1?in.offers[3].plan:execution.work.active&&execution.work.plan.organization==Organization::G?execution.work.plan:in.offers[0].plan;
            in.generalist_bound[i]={declaration(plan,inventory,30+8*inventory.count[focus_item],f.own.actor.strength),f.clock,self,p6::Knowledge::exact};
            auto& model=in.generalist_loaded[i];model={{},f.clock,self,p6::Knowledge::exact};model.value.context=in.generalist_bound[i].value.key;model.value.focus_per_gem={1,p6::Source::queried_setting};
            for(int j=0;j<3;++j){auto& p=model.value.points[j];p.bag=j==1?(plan.group==8?14:12):0;p.focus=model.value.context.build.focus+double(p.bag);p.cumulative={75,100,10.0*j,0,0,0,true};}}
        in.return_attribution_available={true,f.clock,self,p6::Knowledge::exact};in.collector_proposal={group,f.clock,1,p6::Knowledge::exact};
        in.private_income.clock=f.clock;in.private_income.subject=self;in.private_income.knowledge=p6::Knowledge::exact;in.private_income.value.safety_count=1;in.private_income.value.safety[0]=safety(f,self,in.private_income.value.plan.group);
        in.market.inventory={inventory,f.clock,self,p6::Knowledge::exact};in.market.spawn_shop_legal={f.own.at_spawn,f.clock,self,p6::Knowledge::exact};in.market.sell_legal={false,f.clock,self,p6::Knowledge::exact};
        for(int item:{damage_item,focus_item}){auto& q=in.market.items[item];q.price={30,p6::Source::queried_setting};q.purchasable={true,p6::Source::queried_setting};q.buy_legal={f.own.at_spawn,f.clock,self,p6::Knowledge::exact};}
        if(self==1){in.purchases[0].changed=schedule(f,8,true,true);in.purchases[0].unchanged=schedule(f,6);
            for(int i=1;i<3;++i){in.purchases[i].changed=schedule(f,6,false,true,-1);in.purchases[i].unchanged=schedule(f,6);}}
        observe_plan(execution.work.active?execution.work.plan:in.offers[0].plan);
    }
    __attribute__((noinline)) void ready(int round,int general_group=7){in.frame.own.actor.position=self==1?p6::Point{2,0}:self==2?p6::Point{2,1}:p6::Point{2,2};
        in.frame.own.actor.bag=0;in.frame.own.actor.channel=p6::Channel::none;in.frame.own.actor.spawn_group=-1;in.frame.own.at_spawn=true;
        for(int a=0;a<in.frame.actor_count;++a){auto& actor=in.frame.actors[a].value;actor.position=actor.id==1?p6::Point{2,0}:actor.id==2?p6::Point{2,1}:p6::Point{2,2};actor.bag=0;actor.channel=p6::Channel::none;actor.spawn_group=-1;actor.action=p6::WitnessAction::none;}
        configure(general_group);tick(round,general_group,p6::Lifecycle::living);}
    __attribute__((noinline)) void cycle(int start,int expected,Arm arm=Arm::active,bool missing_return=false,int clear_delay=0){
        const int general_group=self==1&&(state.abstaining||state.specialist_bought)?6:7;ready(start,general_group);
        if(expected==2)for(int a=0;a<in.frame.actor_count;++a)if(in.frame.actors[a].value.id==2)in.frame.actors[a].value.position={4,1};
        if(expected==0&&state.abstaining)in.collector_proposal.knowledge=p6::Knowledge::unknown;
        if(self==1&&expected==2){in.supported_slot=state.supported[1].context;in.supported_entry=state.supported[1].entry;}else if(self==1){in.supported_slot=state.supported[0].context;in.supported_entry=state.supported[0].entry;}
        tick(start,in.offers[expected].plan.group,p6::Lifecycle::living);
        if(expected==0){if(self==1&&!state.trial_seen)for(auto& model:in.loaded)model.knowledge=p6::Knowledge::unknown;
            if(self!=1)in.collector_proposal.knowledge=p6::Knowledge::unknown;}
        const auto d=decide(in,state,ledger,arm);check(d.option==expected||(expected==0&&d.private_income),"actual ordinary, trial, specialized or abstaining physical duty is selected");
        const auto frozen=d.private_income?in.private_income.value.plan:in.offers[d.option].plan;observe_plan(frozen);check(commit(in,local,d,state,ledger,arm,execution).started,"retained current circuit starts with actual feasible membership");
        const int clear=clear_delay?clear_delay:expected>0?(frozen.group==8?3:1):frozen.group==7?3:5;
        int capture_round=-1,payout_round=-1,bank_round=-1;bool applied=false;
        for(int t=0;t<105&&execution.work.active;++t){auto& own=in.frame.own.actor;own.spawn_group=own.position==frozen.capture.tiles[frozen.capture.count-1]&&t>=clear?frozen.group:-1;
            if(capture_round>=0&&t==capture_round+6){own.bag=frozen.group==7?12:frozen.group==8?14:frozen.group==6?8:10;payout_round=t;}
            if(bank_round>=0&&t==bank_round+(frozen.group==7||frozen.group==8?2:1)){own.bag=0;own.channel=p6::Channel::none;}
            tick(start+t,frozen.group,t<clear?p6::Lifecycle::living:payout_round<0?p6::Lifecycle::reward_available:p6::Lifecycle::respawning);
            if(expected>0)for(int a=0;a<in.frame.actor_count;++a)if(in.frame.actors[a].value.id==2){auto& helper=in.frame.actors[a].value;helper.action=t<clear?p6::WitnessAction::useful_attack:p6::WitnessAction::none;
                helper.action_group=frozen.group;helper.position=t<=clear?frozen.pads[1]:p6::Point{4,3};helper.spawn_group=-1;local.frame=in.frame;}
            if(payout_round==t)local.milestones={{frozen.group,true,false,own.bag,0},in.frame.clock,self,p6::Knowledge::exact};
            if(bank_round>=0&&t==bank_round+(frozen.group==7||frozen.group==8?2:1))local.milestones={{frozen.group,false,true,0,frozen.group==7?12:frozen.group==8?14:frozen.group==6?8:10},in.frame.clock,self,p6::Knowledge::exact};
            if(missing_return&&own.channel==p6::Channel::tribute){in.return_attribution_available.knowledge=p6::Knowledge::unknown;applied=true;}
            const auto command=advance(in,local,state,ledger,arm,execution);
            // The pinned SDK dispatcher also settles hold/preserve/inspect
            // commands. Its final no-action hold must not leave Work.pending.
            if(command.token)check(execution.confirm(command,receipt(command)),"own physical or settled no-action command has a current successful consumed receipt");
            observe(in,local,ledger,execution);
            if(command.action==PhysicalAction::move){own.position=command.target;if(command.target==frozen.capture.tiles[frozen.capture.count-1]&&capture_round<0)capture_round=t+1;}
            if(command.action==PhysicalAction::tribute&&bank_round<0){bank_round=t;own.channel=p6::Channel::tribute;own.channel_valid=true;}
        }
        check(execution.work.bank_seen&&execution.work.renewal_seen&&!execution.work.active,"actual owned payout reaches bank and named productive renewal");
        check(in.own_state.value.focus==30+8*inventory.count[focus_item],"actual tribute removes only carried-gem Focus and retains purchased Focus");
        if(missing_return)check(applied&&state.abstaining,"unresolved own return attribution withdraws future specialization while valid channel finishes");
    }
    __attribute__((noinline)) void buy(int round,Arm arm,Purchase expected){
        ready(round,state.abstaining?6:7);tick(round,8,p6::Lifecycle::living);refresh(in,state,ledger);
        check(purchase_choice(in,state,ledger,arm)==expected&&begin_purchase(in,local,state,ledger,arm,execution.work),"current own gold funds exactly the evidence-selected actual purchase direction");
        const auto command=purchase_step(in,local,state,execution.work);check(command.valid&&command.kind==reversible_workshop::TransactionKind::buy,"Prism issues a real buy and never an invented equipment sale");
        const auto stale=TransactionReceipt{command.clock,command.clock,command.token,command.kind,command.item,true,true};
        check(!confirm_purchase(command,stale,in,state),"success without a newer actual inventory/gold/stat observation does not confirm purchase");
        inventory=command.after;in.frame.own.gold-=command.amount;in.frame.own.actor.strength=17.5+2.5*inventory.count[damage_item];++in.frame.clock.revision;
        tick(round,8,p6::Lifecycle::living);const TransactionReceipt receipt{command.clock,in.frame.clock,command.token,command.kind,command.item,true,true};
        check(confirm_purchase(command,receipt,in,state)&&state.rebuild.completed,"actual newer queried inventory, own cash and exact target stats confirm the purchase");
    }
    __attribute__((noinline)) void loaded_bank(int round){
        ready(round);in.frame.own.actor.bag=5;tick(round,9,p6::Lifecycle::respawning);
        auto& offer=in.existing_inventory.value;offer=in.offers[0];offer.plan.ingress=offer.plan.capture=path({{2,1}});
        offer.plan.bank=path({{2,1},{3,2},{4,3},{5,3},{6,3}});offer.plan.forecast_rounds=24;offer.schedule=schedule(in.frame,7);offer.schedule.target_id=9;
        const auto own_schedule=offer.schedule.actors[self-1];offer.schedule={};offer.schedule.target_id=9;offer.schedule.actor_count=1;
        auto& retained=offer.schedule.actors[0];retained.actor_id=self;retained.initial_bag=own_schedule.initial_bag;retained.initial_gold=own_schedule.initial_gold;
        retained.occupied_turns=fact<p6::Gems>(24);event(retained,p6::EventKind::tribute,4001,7);event(retained,p6::EventKind::renewal,4002,15);
        offer.safety_count=1;offer.safety[0]=safety(in.frame,self,9);
        for(int a=0;a<in.frame.actor_count;++a)if(in.frame.actors[a].value.id==1)in.frame.actors[a].knowledge=p6::Knowledge::unknown;
        in.existing_inventory.clock=in.frame.clock;in.existing_inventory.subject=self;in.existing_inventory.knowledge=p6::Knowledge::exact;
        const auto decision=decide(in,state,ledger,Arm::active);check(decision.secure_inventory&&decision.option<0,"loaded former helper secures its own existing bag before any new specialization adoption");
        const auto frozen=offer.plan;observe_plan(frozen);check(commit(in,local,decision,state,ledger,Arm::active,execution).started,"existing helper inventory starts its own retained bank route");
        int bank=-1;
        for(int t=0;t<65&&execution.work.active;++t){auto& own=in.frame.own.actor;if(bank>=0&&t==bank+1){own.bag=0;own.channel=p6::Channel::none;}
            tick(round+t,9,p6::Lifecycle::respawning);in.collector_proposal.knowledge=p6::Knowledge::unknown;
            if(bank>=0&&t==bank+1)local.milestones={{9,false,true,0,5},in.frame.clock,self,p6::Knowledge::exact};
            const auto command=advance(in,local,state,ledger,Arm::active,execution);
            if(command.token)check(execution.confirm(command,receipt(command)),"helper's own bag or settled no-action command has its own actual receipt");
            observe(in,local,ledger,execution);if(command.action==PhysicalAction::move)own.position=command.target;
            if(command.action==PhysicalAction::tribute&&bank<0){bank=t;own.channel=p6::Channel::tribute;own.channel_valid=true;}}
        check(execution.work.bank_lower==5&&execution.work.renewal_seen&&!execution.work.active&&ledger.size()==0,
            "former helper's five real gems bank and renew independently without becoming Prism proof or transferred income");
    }
};
__attribute__((noinline)) void first_income_and_private_gate(){
    auto f=std::make_unique<Fixture>();f->in.loaded[0].knowledge=p6::Knowledge::unknown;
    check(choose(f->in,f->state,f->ledger,Arm::active).organization==Organization::G,"first income is independent G when loaded carry evidence is unavailable");
    f->in.loaded[0].knowledge=p6::Knowledge::exact;f->in.loaded[0].value.points[1].bag=1;
    check(choose(f->in,f->state,f->ledger,Arm::active).organization==Organization::G,"a safe tiny bag cannot stand in for the full twelve-gem frozen regime");
    f->cycle(0,0);f->cycle(40,1);f->ready(80);f->tick(80,8,p6::Lifecycle::living);
    check(!prepare_specialist(f->in,f->local,f->state,f->ledger,f->execution.work),"one actual carry trial cannot license specialist purchases");
    f->cycle(80,1);check(qualified(f->in,f->ledger,f->state.supported[0]),"two own actual loaded H trials qualify only their exact original build and membership");
    f->ready(120);f->tick(120,8,p6::Lifecycle::living);check(prepare_specialist(f->in,f->local,f->state,f->ledger,f->execution.work),"current full carry proof freezes one exact funded future-build permission");
    const int proof=f->state.permit.proof_round;f->ready(proof+qualification_rounds);f->tick(proof+qualification_rounds,8,p6::Lifecycle::living);
    check(purchase_choice(f->in,f->state,f->ledger,Arm::control)==Purchase::none,"frozen permission expires at its original strict 256-round boundary without a timer reset");
    auto other=std::make_unique<Fixture>();other->in.loaded[0].subject=2;
    check(choose(other->in,other->state,other->ledger,Arm::active).organization==Organization::G,"a partner's observation cannot supply Prism's private loaded-return premise");
    auto peer=std::make_unique<Fixture>(2);const auto failures=peer->ledger.lifetime_failure_count();
    check(!prepare_specialist(other->in,other->local,other->state,peer->ledger,other->execution.work)&&peer->ledger.lifetime_failure_count()==failures,
        "a foreign private ledger cannot fund or mutate a Prism specialization permission");
    other->in.frame.own.actor.type=p6::Champion::ren;other->in.draft.champions[0]=p6::Champion::ren;
    check(choose(other->in,other->state,other->ledger,Arm::active).organization==Organization::G,"broader draft fallback keeps first income but cannot borrow Prism's bag passive for a different champion");
    auto third=std::make_unique<Fixture>(3);third->cycle(0,0);
    check(third->execution.work.plan.group==10&&third->execution.work.bank_lower==10,"third core actor has an independent legal first-income conversion and renewal");
}
__attribute__((noinline)) void empty_model_changes_whole_specialization(){
    auto active=std::make_unique<Fixture>();active->cycle(0,1);active->cycle(40,1);active->ready(80);active->tick(80,8,p6::Lifecycle::living);
    check(prepare_specialist(active->in,active->local,active->state,active->ledger,active->execution.work),"future specialist purchase is frozen while exact own full proof is current");
    active->cycle(100,1,Arm::active,false,11);check(active->ledger.state(active->state.supported[0].context)->model_set_failed,"an actual slower applicable carry cycle empties the named duration family");
    auto control=std::make_unique<Fixture>(*active);active->ready(150,6);active->tick(150,8,p6::Lifecycle::living);refresh(active->in,active->state,active->ledger);
    control->ready(150,7);control->tick(150,8,p6::Lifecycle::living);refresh(control->in,control->state,control->ledger);
    check(active->state.abstaining&&control->state.abstaining&&purchase_choice(active->in,active->state,active->ledger,Arm::active)==Purchase::generalist&&
        purchase_choice(control->in,control->state,control->ledger,Arm::control)==Purchase::specialist,"identical failed models change actual active purchases while control retains its still-current pre-frozen specialist purchase");
    active->buy(150,Arm::active,Purchase::generalist);control->buy(150,Arm::control,Purchase::specialist);
    check(active->inventory.count[damage_item]==2&&active->inventory.count[focus_item]==0&&control->inventory.count[focus_item]==1,"actual purchase receipts produce different carried specialization inventories");
    active->cycle(180,0);control->cycle(180,2,Arm::control);
    check(active->execution.work.plan.group==6&&control->execution.work.plan.group==8&&active->execution.work.bank_lower==8&&control->execution.work.bank_lower==14,
        "abstention changes geography and collector duty through actual complete owned conversions rather than an evaluation penalty");
    auto helper=std::make_unique<Fixture>(2);helper->loaded_bank(140);helper->state.abstaining=true;helper->cycle(180,0);
    check(helper->execution.work.plan.group==9&&helper->execution.work.bank_lower==10&&helper->ledger.size()==0,"former helper continues its own actual independent income without borrowing Prism's evidence");
}
__attribute__((noinline)) void new_build_and_safe_loaded_boundary(){
    auto f=std::make_unique<Fixture>();f->cycle(0,1);f->cycle(40,1);f->ready(80);f->tick(80,8,p6::Lifecycle::living);
    check(prepare_specialist(f->in,f->local,f->state,f->ledger,f->execution.work),"own two-cycle full proof precedes the one purchased specialization");f->buy(80,Arm::active,Purchase::specialist);
    check(!qualified(f->in,f->ledger,f->state.supported[1]),"purchased build starts with zero full H qualification");
    f->cycle(100,2);check(!qualified(f->in,f->ledger,f->state.supported[1]),"one new-build H cannot inherit two old-build completions");
    f->cycle(140,2);check(qualified(f->in,f->ledger,f->state.supported[1]),"two actual new-build H cycles establish their separate complete qualification");
    f->cycle(180,2,Arm::active,true);check(f->execution.work.bank_lower==14&&f->execution.work.renewal_seen,"existing loaded bag and valid tribute finish before special-role withdrawal");
    f->buy(220,Arm::active,Purchase::retaining_generalist);check(f->inventory.count[focus_item]==1&&f->inventory.count[damage_item]==2,"Prism's generalist addition retains its real purchased Focus rather than selling or resetting it");
    f->cycle(240,0);check(f->in.own_state.value.focus==38&&f->execution.work.plan.group==6,"generalist bank and renewal retain equipment Focus while ending carried-bag Focus");
}
}
int main(){first_income_and_private_gate();empty_model_changes_whole_specialization();new_build_and_safe_loaded_boundary();
    std::printf("Abstaining Specialists: %d checks passed\n",checks);return 0;}
