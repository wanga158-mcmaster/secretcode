#include "../assay-guild/src/policy.h"
#include "../assay-guild/src/draft.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>
using namespace p8;
using namespace p8::assay_guild;
namespace {
int checks=0;
void check(bool value,const char* message){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
template<class T> p6::Fact<T> fact(T value){return {value,p6::Source::synthetic_fixture};}
Route path(std::initializer_list<p6::Point> points){Route r;r.verified=true;for(auto p:points)r.tiles[r.count++]=p;return r;}
Route stay(p6::Point point){return path({point});}
__attribute__((noinline)) Plan ordinary_plan(int actor,bool initial=false) {
    Plan p;p.organization=Organization::U;p.collector=actor;p.crew[0]=actor;p.crew_count=1;
    p.group=actor==1?7:actor==2?8:9;p.altar=actor==2?6:4;
    if(actor==1){p.ingress=initial?path({{0,0},{1,0},{2,0}}):stay({2,0});p.capture=path({{2,0},{3,0}});
        p.bank=path({{3,0},{4,0}});p.renewal=path({{4,0},{4,1},{3,1},{2,1},{2,0}});}
    else if(actor==2){p.ingress=path({{2,1},{2,2},{3,2},{4,2}});p.capture=path({{4,2},{5,2}});
        p.bank=path({{5,2},{6,2}});p.renewal=path({{6,2},{5,2},{4,2},{3,2},{2,1}});}
    else{p.ingress=stay({2,4});p.capture=path({{2,4},{3,4}});p.bank=path({{3,4},{4,4}});p.renewal=path({{4,4},{4,5},{3,5},{2,4}});}
    p.forecast_rounds=90;p.remaining_rounds=110;p.capture_window=5;p.gem_bearing=p.complete_verified=p.rally_legal=true;return p;
}
__attribute__((noinline)) Plan role_plan(int actor,Configuration configuration) {
    const int collector=configuration==Configuration::joint_clear?1:2;
    Plan p;p.organization=Organization::H;p.group=7;p.collector=collector;p.altar=4;
    p.crew={collector,collector==1?2:1,-1,-1,-1,-1};p.crew_count=2;
    p.pads[0]=collector==1?p6::Point{2,0}:p6::Point{2,1};p.pads[1]=collector==1?p6::Point{2,1}:p6::Point{2,0};
    const auto at=actor==1?p6::Point{2,0}:p6::Point{2,1};p.ingress=stay(at);
    p.capture=actor==collector?path({at,{3,0}}):stay(at);
    p.bank=actor==collector?path({{3,0},{4,0}}):path({at,{3,0},{4,0}});
    if(actor==collector)p.renewal=actor==1?path({{4,0},{4,1},{3,1},{2,1},{2,0}}):path({{4,0},{4,1},{3,1},{2,1}});
    else{p.renewal=path({{4,0},{4,1},{4,2}});p.release=actor==1?path({{2,0},{2,1},{2,2},{3,2},{4,2}}):path({{2,1},{2,2},{3,2},{4,2}});}
    p.forecast_rounds=90;p.remaining_rounds=110;p.capture_window=5;p.gem_bearing=p.complete_verified=p.rally_legal=true;return p;
}
__attribute__((noinline)) Plan continuation_plan(int actor) {
    auto p=ordinary_plan(2);p.collector=actor;p.crew[0]=actor;p.ingress=stay({4,2});
    p.renewal=path({{6,2},{6,3},{5,3},{4,3},{4,2}});return p;
}
p6::CycleCertificate safety(const p6::Frame& frame,int actor,int group){
    p6::CycleCertificate c;c.clock=frame.clock;c.actor=actor;c.group=group;
    if(const auto* seen=frame.visible(actor))c.clear=c.hold=c.bank=c.recovery={seen->health,seen->max_health,20,0,0,0,true};
    c.route_known=c.bank_executable=c.renewal_reachable=c.solo=true;return c;
}
void event(p6::ActorSchedule& actor,p6::EventKind kind,int id,int tick,int production=-1){
    p6::Event e;e.kind=kind;e.event_id=id;e.tick=tick;e.production_index=production;e.gold_cost=fact<p6::Gems>(0);
    e.source=p6::Source::synthetic_fixture;e.waypoint_id=kind==p6::EventKind::renewal?100+actor.actor_id:-1;
    actor.events[actor.event_count++]=e;
}
__attribute__((noinline)) p6::Schedule schedule(const p6::Frame& frame,bool slow,Configuration config,int target) {
    p6::Schedule s;s.target_id=target;s.actor_count=3;s.production_count=3;
    const int solo_clear=(slow?14:2)+(frame.visible(1)->position==p6::Point{2,0}?0:2);
    const int joint_clear=slow?13:2;
    for(int actor=1;actor<=3;++actor){
        auto& a=s.actors[actor-1];a.actor_id=actor;const bool own=actor==frame.clock.self;
        a.initial_bag={frame.visible(actor)->bag,own?p6::Source::self_observed:p6::Source::visible_subject};
        a.initial_gold={own?frame.own.gold:0,own?p6::Source::self_observed:p6::Source::conservative};
        int source=actor==1?7:actor==2?8:9,payout=actor==1?solo_clear+7:actor==2?30:7;
        int bank=payout+3,renew=bank+4;
        if(config==Configuration::joint_clear && actor==1){payout=joint_clear+7;bank=joint_clear+10;renew=joint_clear+14;}
        if(config==Configuration::joint_clear && actor==2){payout=joint_clear+31;bank=joint_clear+34;renew=joint_clear+38;}
        if(config==Configuration::serial && actor==1){source=8;const int c=slow?14:2;payout=c+15;bank=c+18;renew=c+22;}
        if(config==Configuration::serial && actor==2){source=7;const int c=slow?14:2;payout=c+7;bank=c+10;renew=c+13;}
        auto& production=s.productions[actor-1];production.event_id=1000+source;production.tick=payout;
        production.gems=fact<p6::Gems>(source==7?12:10);production.gold=fact<p6::Gems>(0);
        production.holders[0]=actor;production.holder_count=1;production.source=p6::Source::synthetic_fixture;
        event(a,p6::EventKind::production,production.event_id,payout,actor-1);event(a,p6::EventKind::tribute,2000+source,bank);
        event(a,p6::EventKind::renewal,3000+actor,renew);a.occupied_turns=fact<p6::Gems>(renew);
    }
    return s;
}
ActionReceipt receipt(const Command& c){return {c.clock,c.clock,c.token,c.action,true,true};}
struct Fixture {
    Inputs in{};Guild guild;EvidenceLedger ledger;Execution execution{};LocalFrame local{};
    int self=1;bool slow=false;std::array<int,2> slots{-1,-1};
    __attribute__((noinline)) Fixture(int actor=1,bool long_clear=false):guild(actor),ledger(actor),self(actor),slow(long_clear){
        in.frame.clock={0,self,0};in.frame.own.gold=15;
        in.draft.champions={p6::Champion::tavi,p6::Champion::varg,p6::Champion::marcella,p6::Champion::other,p6::Champion::other};
        in.draft.actors={1,2,3,-1,-1};in.draft.picks=in.picks=3;
        for(int id=1;id<=3;++id){p6::Actor a;a.id=id;a.pick_ordinal=id-1;a.type=in.draft.champions[id-1];a.team=p6::Team::ally;
            a.position=id==1?p6::Point{0,0}:id==2?p6::Point{2,1}:p6::Point{2,4};
            a.health=a.max_health=1000;a.strength=id==2?2:20;a.attack_range=id==1?3:1;a.ranged=id==1;
            a.alive=a.can_move=a.can_attack=true;
            if(id==self)in.frame.own.actor=a;else in.frame.actors[in.frame.actor_count++]={a,in.frame.clock,id,p6::Knowledge::exact};}
        in.settings={2000,1000,5};in.settings_identity={17,p6::Source::queried_setting};
        in.own_state={{1000,1000,in.frame.own.actor.strength,30,1,0,99},in.frame.clock,self,p6::Knowledge::exact};
        in.threat_regime={0,in.frame.clock,self,p6::Knowledge::exact};in.target_group=7;in.independent=0;in.offer_count=3;
        in.offers[0].plan=ordinary_plan(self,self==1);
        in.assay.ordinary=ordinary_plan(1,true);in.assay.fixed=declaration(in.assay.ordinary,1,7,0);
        in.assay.nominal_clear=fact(14);in.assay.nominal_logistics=fact(7);
        if(self>=3)in.offer_count=1;
        for(int r=0;self<3&&r<2;++r){auto& candidate=in.roles[r];candidate.offer=r+1;auto& role=candidate.role;
            role.configuration=r==0?Configuration::joint_clear:Configuration::serial;
            role.primary=role_plan(self,role.configuration);if(!collecting(role,self))role.next=continuation_plan(self);
            const int income=collecting(role,self)?7:8;role.context=declaration(role.primary,self,income,r+1);
            role.context.kind=EpisodeKind::full_organization;role.context.require_supported_work=true;
            role.context.key.participants={1,2,-1,-1,-1,-1};role.context.key.participant_count=2;
            role.context.key.route_hash=role_hash(role,self);role.context.key.return_route_hash=0;
            const int c=r==0?(slow?13:2):(slow?14:2);
            role.nominal_duration=collecting(role,self)?c+(self==1?14:13):c+(self==1?22:38);
            slots[r]=ledger.predeclare(role.context);check(slots[r]>=0,"both exact changed-role contexts declared before assay");
            in.offers[r+1].plan=role.primary;in.offers[r+1].entry=role.context.key;in.offers[r+1].context_slot=slots[r];}
        schedules();tick(0,in.offers[0].plan.group,p6::Lifecycle::living);
    }
    ContextDeclaration declaration(const Plan& plan,int owner,int group,int role){
        ContextDeclaration d;auto& k=d.key;k.settings_id=17;k.actor_id=owner;k.champion=int(in.draft.champions[owner-1]);
        k.source_group=group;k.route_template=0;k.endpoint=plan.altar;k.role=role;k.threat_regime=0;
        k.route_hash=plan_route_hash(plan);k.return_route_hash=plan_return_hash(plan);k.participants[0]=owner;k.participant_count=1;
        k.build={99,1000,owner==2?2.:20.,30,1};
        d.envelope={{750,1000},{1000,1000},{k.build.strength,k.build.strength},{30,30},{1,1},{0,12},0};
        for(int h=0;h<3;++h)d.hypotheses[h].offset={2*h,2*h+2};return d;
    }
    __attribute__((noinline)) void schedules(){
        for(int o=0;o<3;++o)in.offers[o].schedule=schedule(in.frame,slow,o==0?Configuration::independent:o==1?Configuration::joint_clear:Configuration::serial,in.offers[o].plan.group);
        auto& own=in.private_income.value;own.plan=in.offers[0].plan;own.schedule={};own.schedule.actor_count=1;own.schedule.production_count=1;
        own.schedule.target_id=own.plan.group;own.schedule.actors[0]=in.offers[0].schedule.actors[self-1];
        own.schedule.productions[0]=in.offers[0].schedule.productions[self-1];
        for(int e=0;e<own.schedule.actors[0].event_count;++e)if(own.schedule.actors[0].events[e].production_index>=0)own.schedule.actors[0].events[e].production_index=0;
    }
    __attribute__((noinline)) void tick(int round,int group,p6::Lifecycle lifecycle){
        auto& frame=in.frame;frame.clock.round=round;
        for(int a=0;a<frame.actor_count;++a)frame.actors[a].clock=frame.clock;
        p6::Group g;g.id=group;g.lifecycle=lifecycle;g.original_spawn_visible=g.all_spawns_visible=g.uncontested_now=true;
        frame.group={g,frame.clock,group,p6::Knowledge::exact};
        in.own_state.clock=in.threat_regime.clock=frame.clock;in.own_state.value.bag=frame.own.actor.bag;
        in.own_state.value.health=frame.own.actor.health;
        in.windows.valid=true;in.windows.source=p6::Source::synthetic_fixture;in.windows.sampled_actor_id=self;in.windows.sampled_round=round;
        in.windows.windows=in.windows.deadlines={slow?23:24,slow?23:24,slow?23:24};
        for(int o=0;o<in.offer_count;++o){auto& offer=in.offers[o];offer.safety_count=3;
            for(int a=0;a<3;++a){const int id=a+1;offer.safety[a]=safety(frame,id,common::participant(offer.plan,id)?offer.plan.group:id==1?7:id==2?8:9);}}
        for(auto& role:in.roles)role.role.next_safety=safety(frame,self,8);
        in.private_income.clock=frame.clock;in.private_income.subject=self;in.private_income.knowledge=p6::Knowledge::exact;
        in.private_income.value.safety_count=1;in.private_income.value.safety[0]=safety(frame,self,in.private_income.value.plan.group);
        in.proposal={{Configuration::serial,7},frame.clock,1,p6::Knowledge::exact};
        observe_plan(execution.work.active||execution.awaiting_next?execution.selected_plan():in.offers[0].plan);
    }
    __attribute__((noinline)) void observe_plan(const Plan& plan){
        local.frame=in.frame;local.current_round=in.frame.clock.round;local.milestones={};local.safety_count=plan.crew_count;
        for(int i=0;i<plan.crew_count;++i)local.safety[i]=safety(in.frame,plan.crew[i],plan.group);
        local.legal={{},in.frame.clock,self,p6::Knowledge::exact};auto& legal=local.legal.value;
        legal.safe_now=legal.attack_legal=legal.altar_legal=legal.recall_safe=legal.renewal_productive=true;
        for(Leg leg:{Leg::ingress,Leg::capture,Leg::bank,Leg::renewal,Leg::release}){const auto& route=route_for(plan,leg);auto& access=legal.routes[int(leg)];
            access.assumptions_hold=route_valid(route);access.next_move_legal=true;
            for(int i=0;i<route.count;++i)if(route.tiles[i]==in.frame.own.actor.position){access.next_tile=route.tiles[std::min(i+1,route.count-1)];break;}}
    }
    __attribute__((noinline)) void ordinary_assay(bool missing_bank=false){
        auto decision=choose(in,guild,ledger,Arm::active);check(decision.option==0 && decision.assay,"ordinary unchanged comparator selects exact fixed assay without a detour");
        observe_plan(in.offers[0].plan);check(commit(in,local,decision,guild,ledger,Arm::active,execution).started,"ordinary assay starts physical U");
        const int clear=slow?16:4;
        for(int t=0;t<=clear+14;++t){auto& own=in.frame.own.actor;
            const bool payout=t==clear+7,bank=t==clear+10;
            if(payout)own.bag=12;if(bank){own.bag=0;own.channel=p6::Channel::none;}
            own.spawn_group=own.position==p6::Point{3,0}?7:-1;
            tick(t,7,t<clear?p6::Lifecycle::living:t<clear+7?p6::Lifecycle::reward_available:p6::Lifecycle::respawning);
            if(payout || (bank&&!missing_bank))local.milestones={{7,payout,bank,12,12},in.frame.clock,self,p6::Knowledge::exact};
            const auto command=advance(in,local,guild,ledger,execution);
            if(command.action==PhysicalAction::attack || command.action==PhysicalAction::move || command.action==PhysicalAction::tribute)
                check(execution.confirm(command,receipt(command)),"assay consumes actual own action receipts");
            observe(in,local,guild,ledger,execution);
            if(command.action==PhysicalAction::move)own.position=command.target;
            if(command.action==PhysicalAction::tribute){own.channel=p6::Channel::tribute;own.channel_valid=true;}
        }
        check(!execution.work.active,"assay completes its actual named productive return");
        if(!missing_bank)check(guild.assay.resolved && guild.assay.clear==(slow?14:2) && guild.assay.logistics==7,
            "measured C starts at actual first attack and L spans own payout bank and named renewal");
    }
    __attribute__((noinline)) void ready(int round,Configuration proposal){
        in.frame.own.actor.position=self==1?p6::Point{2,0}:p6::Point{2,1};in.frame.own.actor.bag=0;in.frame.own.actor.channel=p6::Channel::none;
        for(int a=0;a<in.frame.actor_count;++a){auto& actor=in.frame.actors[a].value;
            actor.position=actor.id==1?p6::Point{2,0}:actor.id==2?p6::Point{2,1}:p6::Point{2,4};actor.bag=0;actor.action=p6::WitnessAction::none;actor.spawn_group=-1;}
        in.offers[0].plan=ordinary_plan(self);schedules();tick(round,7,p6::Lifecycle::living);
        in.proposal.value.configuration=proposal;
    }
};
struct PairResult{bool overlap=false;std::array<bool,2> positive{};std::array<int,2> banks{-1,-1};};
__attribute__((noinline)) PairResult role_cycle(Fixture& first,Fixture& second,int start,Configuration config,bool missing_secondary=false){
    PairResult result;first.ready(start,config);second.ready(start,config);
    for(auto* fixture:{&first,&second}){auto decision=choose(fixture->in,fixture->guild,fixture->ledger,Arm::active);
        check(decision.role>=0 && decision.configuration==config,"each actual actor admits only its selected current role configuration");
        fixture->observe_plan(fixture->in.offers[decision.option].plan);
        check(commit(fixture->in,fixture->local,decision,fixture->guild,fixture->ledger,Arm::active,fixture->execution).started,
            "changed configuration starts as its own unqualified physical campaign");}
    std::array<p6::Actor,2> world{first.in.frame.own.actor,second.in.frame.own.actor};
    const int collector=config==Configuration::joint_clear?0:1,helper=1-collector;
    const int c=config==Configuration::joint_clear?(first.slow?13:2):(first.slow?14:2);
    const int secondary_clear=c+(config==Configuration::joint_clear?24:8),finish=secondary_clear+14;
    for(int t=0;t<=finish;++t){std::array<Command,2> commands{};
        for(int actor=0;actor<2;++actor){auto& f=actor==0?first:second;
            const bool next=f.execution.next||f.execution.awaiting_next;const int group=next?8:7;
            const int clear=next?secondary_clear:c;const bool payout=actor==collector?t==c+7:t==secondary_clear+7;
            const bool bank=actor==collector?t==c+10:t==secondary_clear+10;
            if(payout)world[actor].bag=group==7?12:10;if(bank){world[actor].bag=0;world[actor].channel=p6::Channel::none;}
            world[actor].spawn_group=world[actor].position==p6::Point{3,0}?7:world[actor].position==p6::Point{5,2}?8:-1;
            f.in.frame.own.actor=world[actor];
            for(int a=0;a<f.in.frame.actor_count;++a)if(f.in.frame.actors[a].value.id==world[1-actor].id)f.in.frame.actors[a].value=world[1-actor];
            f.tick(start+t,group,t<clear?p6::Lifecycle::living:t<clear+7?p6::Lifecycle::reward_available:p6::Lifecycle::respawning);
            f.in.proposal.value.configuration=config;
            if(payout || bank){const bool attributed=!(missing_secondary && actor==helper && payout);
                if(attributed)f.local.milestones={{group,payout,bank,payout?(group==7?12:10):0,bank?(group==7?12:10):0},f.in.frame.clock,f.self,p6::Knowledge::exact};}
            if(!f.execution.work.active&&!f.execution.awaiting_next)continue;
            auto command=advance(f.in,f.local,f.guild,f.ledger,f.execution);commands[actor]=command;
            if(config==Configuration::serial&&actor==collector)check(command.action!=PhysicalAction::attack,"serial collector actually reserves attack labor for the clearer");
            if(command.action==PhysicalAction::move || command.action==PhysicalAction::attack || command.action==PhysicalAction::tribute)
                check(f.execution.confirm(command,receipt(command)),"changed-role commands have actual same-owner-turn receipts");
            const auto resolution=observe(f.in,f.local,f.guild,f.ledger,f.execution);
            world[actor].action=command.action==PhysicalAction::attack?p6::WitnessAction::useful_attack:p6::WitnessAction::none;
            world[actor].action_group=group;
            if(command.action==PhysicalAction::move)world[actor].position=command.target;
            world[actor].spawn_group=world[actor].position==p6::Point{3,0}?7:world[actor].position==p6::Point{5,2}?8:-1;
            if(command.action==PhysicalAction::tribute){world[actor].channel=p6::Channel::tribute;world[actor].channel_valid=true;}
            if(resolution.accepted)result.positive[actor]=resolution.full_positive;
            if(f.execution.trace.bank>=0)result.banks[actor]=f.execution.trace.bank-start;
        }
        if(config==Configuration::serial && commands[helper].action==PhysicalAction::attack &&
            first.execution.next && world[collector].bag>0)result.overlap=true;
    }
    return result;
}
__attribute__((noinline)) void measured_serial_and_full_proof(){
    auto clearer=std::make_unique<Fixture>(),collector=std::make_unique<Fixture>(2);
    clearer->ordinary_assay();check(clearer->guild.assay.desired(Arm::active)==Configuration::serial &&
        clearer->guild.assay.desired(Arm::control)==Configuration::joint_clear,"same accurately recorded assay overrides nominal stages only in active");
    clearer->ready(80,Configuration::serial);
    const auto active=choose(clearer->in,clearer->guild,clearer->ledger,Arm::active),control=choose(clearer->in,clearer->guild,clearer->ledger,Arm::control);
    check(active.configuration==Configuration::serial && control.option==0,"serial work beats current alternatives while nominal joint work fails the unchanged comparator");
    check(clearer->ledger.state(clearer->slots[1])->positive_completions==0,"one assay is no completed changed membership proof");
    const auto first=role_cycle(*clearer,*collector,80,Configuration::serial);
    check(first.positive[0]&&first.positive[1]&&first.overlap,"serial clearer produces at the second source during the actual collector's loaded bank trip");
    check(clearer->execution.trace.frozen.context.key.source_group==8 && clearer->execution.trace.income_attack>=88 && first.banks[0]==20 && first.banks[1]==12 &&
        clearer->execution.trace.payout_lower==10 && collector->execution.trace.payout_lower==12,
        "secondary payout is owned at source8 while source7 remains with the actual collector");
    check(!qualified(clearer->in,clearer->guild,clearer->ledger)&&!qualified(collector->in,collector->guild,collector->ledger),"first role episode cannot qualify either actor or borrow partner proof");
    const auto second=role_cycle(*clearer,*collector,160,Configuration::serial);
    check(second.positive[0]&&second.positive[1]&&qualified(clearer->in,clearer->guild,clearer->ledger)&&qualified(collector->in,collector->guild,collector->ledger),
        "two actual matched complete role cycles independently qualify both exact memberships");
    clearer->ready(240,Configuration::serial);clearer->in.frame.own.actor.strength=21;clearer->in.own_state.value.strength=21;
    decide(clearer->in,clearer->guild,clearer->ledger,Arm::active);
    clearer->in.frame.own.actor.strength=20;clearer->in.own_state.value.strength=20;
    check(decide(clearer->in,clearer->guild,clearer->ledger,Arm::active).role<0&&!qualified(clearer->in,clearer->guild,clearer->ledger),
        "restoring an old build cannot resurrect materially expired exact-role qualification");
    clearer->guild.assay.clear=100;clearer->guild.assay.logistics=0;
    check(choose(clearer->in,clearer->guild,clearer->ledger,Arm::active).option==0,"one changed configuration cannot turn into a fresh joint campaign after a new epoch");
}
__attribute__((noinline)) void joint_release_and_unknown(){
    auto owner=std::make_unique<Fixture>(1,true),helper=std::make_unique<Fixture>(2,true);owner->ordinary_assay();
    check(owner->guild.assay.desired(Arm::active)==Configuration::joint_clear,"long measured clearance selects actual joint labor");
    const auto result=role_cycle(*owner,*helper,80,Configuration::joint_clear);
    check(result.positive[0]&&result.positive[1]&&owner->execution.trace.other_attack&&helper->execution.trace.released,
        "joint actors both attack then the helper actually releases into its own complete U production");
    auto unknown=std::make_unique<Fixture>();unknown->ordinary_assay(true);
    check(!unknown->guild.assay.resolved&&unknown->guild.assay.desired(Arm::active)==Configuration::independent,"unattributed bank loss never becomes an assay measurement");
    check(division(6,9)==Configuration::independent && division(9,5)==Configuration::joint_clear && division(5,9)==Configuration::serial,
        "four owner-turn threshold has explicit tie and equality boundaries");
    auto bad=std::make_unique<Fixture>(),good=std::make_unique<Fixture>(2);bad->ordinary_assay();
    const auto incomplete=role_cycle(*bad,*good,80,Configuration::serial,true);
    check(!incomplete.positive[0]&&incomplete.positive[1]&&!qualified(bad->in,bad->guild,bad->ledger),
        "another actor's actual successful bank cannot replace the clearer's missing own secondary payout receipt");
}
__attribute__((noinline)) void safety_identity_and_ordinary_selection(){
    auto fixture=std::make_unique<Fixture>();fixture->ordinary_assay();fixture->ready(80,Configuration::serial);
    const auto original=fixture->in.roles[1].role.next.group;fixture->in.roles[1].role.next.group=9;
    check(choose(fixture->in,fixture->guild,fixture->ledger,Arm::active).option==0,"changed secondary identity cannot borrow a composite route or primary source proof");
    fixture->in.roles[1].role.next.group=original;
    fixture->in.roles[1].role.next_safety.solo=false;
    check(choose(fixture->in,fixture->guild,fixture->ledger,Arm::active).option==0,"serial continuation needs its own complete solo safety bound");
    fixture->in.roles[1].role.next_safety.solo=true;
    fixture->in.offers[2].safety[0].solo=false;
    check(choose(fixture->in,fixture->guild,fixture->ledger,Arm::active).option==0,"serial primary clearance cannot depend on a nonattacking collector's labor or heal");
    fixture->in.offers[2].safety[0].solo=true;
    for(int a=0;a<fixture->in.frame.actor_count;++a)if(fixture->in.frame.actors[a].value.id==2)fixture->in.frame.actors[a].knowledge=p6::Knowledge::unknown;
    check(choose(fixture->in,fixture->guild,fixture->ledger,Arm::active).role<0,"missing current collector cannot license serial handoff from private assay evidence");
    auto producer=std::make_unique<Fixture>(3);
    for(int a=0;a<producer->in.frame.actor_count;++a)producer->in.frame.actors[a].knowledge=p6::Knowledge::unknown;
    const auto independent=choose(producer->in,producer->guild,producer->ledger,Arm::active);
    check(independent.private_income&&independent.organization==Organization::U,"third actor keeps its own U duties when the assay and partner are unseen");
    producer->observe_plan(producer->in.private_income.value.plan);
    check(commit(producer->in,producer->local,independent,producer->guild,producer->ledger,Arm::active,producer->execution).started,
        "third actor starts its own retained route without another actor's assay");
    const auto production=advance(producer->in,producer->local,producer->guild,producer->ledger,producer->execution);
    check(production.action==PhysicalAction::attack&&producer->execution.confirm(production,receipt(production))&&producer->execution.work.productive_action_seen,
        "third actor confirms actual productive work without a partner's private ledger");
    auto unavailable=std::make_unique<Fixture>();unavailable->in.assay.ordinary.bank=path({{3,0},{3,1},{4,0}});
    auto d=choose(unavailable->in,unavailable->guild,unavailable->ledger,Arm::active);
    check(d.option==0&&!d.assay,"assay naming cannot divert the unchanged selected productive route");
    unavailable->observe_plan(unavailable->in.offers[0].plan);
    check(commit(unavailable->in,unavailable->local,d,unavailable->guild,unavailable->ledger,Arm::active,unavailable->execution).started&&
        unavailable->guild.assay.attempted&&!unavailable->guild.assay.resolved,"unavailable assay preserves ordinary physical income and cannot retry later");
    check(champion_pool.size()==8,"draft retains the broader eight-champion role-ranked pool");
}
}
int main(){measured_serial_and_full_proof();joint_release_and_unknown();safety_identity_and_ordinary_selection();
    std::printf("Assay Guild: %d checks passed\n",checks);return 0;}
