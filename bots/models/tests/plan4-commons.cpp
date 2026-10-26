#include "../commons/src/commons.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
using namespace plan4;
namespace cm=plan4::commons;
namespace {
int checks=0;
void require(bool okay,const char* why){++checks;if(!okay){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
struct World {
    int enemy_extra=5,own_extra=0,bank_extra=0,unknown_x=-1;
    bool departure=true,bad_bank=false,free_heal=false,early_renew=false,deny_quote=false,allowed=true,order=true,order_supported=true,quote_false=false,enemy_supported=true;
    mutable std::array<cm::LegRequest,64> requests{};mutable int count=0;
};
int terrain(Point a,Point b,const void* context){const auto& w=*static_cast<const World*>(context);return a.x==w.unknown_x||b.x==w.unknown_x?-1:std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
int enemy_terrain(Point a,Point b,const void*){return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
bool budget(const void* context){return static_cast<const World*>(context)->allowed;}
bool order(int,bool& own_first,const void* context){const auto& w=*static_cast<const World*>(context);own_first=w.order;return w.order_supported;}
bool quote(const cm::LegRequest& request,const Information& info,cm::LegPlan& plan,const void* context){
    auto& w=*const_cast<World*>(static_cast<const World*>(context));if(w.count<int(w.requests.size()))w.requests[w.count++]=request;
    if(w.deny_quote)w.allowed=false;
    if(w.quote_false||(request.enemy&&!w.enemy_supported))return false;
    plan={};plan.supported=true;plan.entry_round=request.start;plan.entry_health=plan.end_health=plan.failure_health=request.actor.health;
    plan.end_position=plan.failure_position=request.actor.position;plan.departure_supported=w.departure;plan.departure_round=request.start;
    if(request.kind==cm::Leg::acquire){
        const auto target=info.groups[request.group].position;const int d=request.travel.distance(request.actor.position,target,request.travel.context);if(d<0)return false;
        plan.entry_round+=d+(request.enemy?w.enemy_extra:w.own_extra)+(request.stock.regenerated?info.groups[request.group].full_clear_rounds:0);
        plan.completion_round=request.current_capture?std::max(request.start+1,info.groups[request.group].capture_started_round+info.settings.capture_rounds):plan.entry_round+1+info.settings.capture_rounds;
        plan.end_position=target;plan.completion_phase=Phase::capture;
        if(request.actor.position!=target){plan.failure_position={target.x+(request.actor.position.x>target.x)-(request.actor.position.x<target.x),
            target.y+(request.actor.position.y>target.y)-(request.actor.position.y<target.y)};plan.failure_supported=true;}
        if(w.free_heal)plan.end_health=request.actor.maximum_health;
    }else if(request.kind==cm::Leg::bank){
        const auto target=request.existing_channel?request.actor.position:info.altars[request.altar].position;
        plan.entry_round+=request.travel.distance(request.actor.position,target,request.travel.context);plan.end_position=target;
        plan.completion_round=plan.entry_round+(request.existing_channel?request.channel_ticks:tribute_ticks(request.actor.bag,info.settings))+(request.enemy?0:w.bank_extra);
        plan.completion_phase=request.actor.bag?Phase::owner_start:Phase::owner_action;if(w.bad_bank)--plan.completion_round;
    }else if(request.kind==cm::Leg::hold){plan.completion_round=request.start+1;plan.completion_phase=Phase::owner_action;}
    else{
        const bool full=request.actor.health>=request.actor.maximum_health;
        const bool on_spawn=request.actor.position==info.own_spawn||(request.start==info.round&&info.own.on_spawn);
        if(full)plan.completion_round=request.start+1;
        else{plan.recall_safe=true;
            if(!on_spawn){plan.entry_round+=request.existing_channel?request.channel_ticks:info.settings.recall_rounds;plan.end_position=info.own_spawn;}
            const float recovery=request.actor.maximum_health*info.settings.spawn_heal_fraction;
            plan.completion_round=plan.entry_round+int(std::ceil((request.actor.maximum_health-request.actor.health)/recovery));plan.end_health=request.actor.maximum_health;
            if(w.early_renew)plan.completion_round=plan.entry_round;}
        plan.completion_phase=Phase::owner_action;
    }
    return true;
}
struct Fixture {
    std::unique_ptr<Information> info=std::make_unique<Information>();Menu menu{};cm::Priority priority{};World world{};cm::Input input{};
    Fixture(){
        info->settings.max_rounds=16;info->settings.capture_rounds=1;info->own.id=7;info->own.position={0,0};info->own_spawn={0,0};
        info->own.health=info->own.maximum_health=100;info->own.alive=info->own.recall_safe=true;info->group_count=2;info->altar_count=1;info->travel={terrain,&world};
        for(int g=0;g<2;++g){auto& row=info->groups[g];row.id=101+g;row.position={2+2*g,0};row.gems=20;row.gold=5;row.respawn_rounds=100;
            row.full_health=100;row.full_clear_rounds=2;row.clear_rounds=0;
            row.lifecycle=Lifecycle::cleared;row.visible=row.all_spawns_visible=row.occupancy_known=row.reward_available=true;row.provenance={0,Source::personal_vision,Status::exact_now};}
        info->altars[0].id=90;info->altars[0].position={0,1};info->altars[0].altar_position={0,2};info->altars[0].lock_known=true;
        require(cm::freeze_priority(*info,{3,0},true,{99,true,true,true},priority),"freeze canonical public anchor priority");
        menu.count=1;menu.jobs[0]={JobKind::farm,0,-1,0};input.info=info.get();input.menu=&menu;input.first=menu.jobs[0];input.priority=&priority;
        input.sole_producers_verified=input.order_known=true;input.enemy_travel={terrain,&world};input.enemy.actor={9,{6,0},100,100,0};input.enemy.personally_visible=input.enemy.entry_phase_supported=true;
        input.enemy.provenance={0,Source::personal_vision,Status::exact_now};input.enemy.next_action_round=0;
        input.quote_leg=quote;input.quote_context=&world;input.order_at=order;input.order_context=&world;
    }
    void choose(Job job){input.first=job;menu.count=1;menu.single_count=0;menu.jobs[0]=job;}
    void singles(){menu.single_count=2;menu.count=6;menu.jobs[0]={JobKind::farm,0,-1,0};menu.jobs[1]={JobKind::farm,1,-1,0};
        menu.jobs[2]={JobKind::farm,0,1,0};menu.jobs[3]={JobKind::bank,-1,-1,0};menu.jobs[4]={JobKind::renew};menu.jobs[5]={JobKind::hold};}
};
int count(const cm::Result& r,cm::Kind kind,bool enemy){int n=0;for(int e=0;e<r.event_count;++e)n+=r.events[e].kind==kind&&r.events[e].enemy==enemy;return n;}
int at(const cm::Result& r,cm::Kind kind,bool enemy,int ordinal=0){for(int e=0;e<r.event_count;++e)if(r.events[e].kind==kind&&r.events[e].enemy==enemy&&ordinal--==0)return r.events[e].round;return-1;}
void conserved(const cm::Result& r,unsigned own_initial=0,unsigned enemy_initial=0){
    std::uint64_t own=own_initial,enemy=enemy_initial;
    for(int e=0;e<r.event_count;++e){const auto& event=r.events[e];if(event.kind==cm::Kind::acquire)(event.enemy?enemy:own)+=event.amount;
        if(e)require(event.round>=r.events[e-1].round,"chronological public event prefix");
        require(event.status==Status::predicted,"projection remains predicted");
        if(event.kind==cm::Kind::convert)require(event.phase==Phase::owner_start,"score only at completed owner-start conversion");}
    require(own==std::uint64_t(r.our_secured)+r.final_bag,"own colored inventory conserved");require(enemy==std::uint64_t(r.enemy_secured)+r.enemy_final_bag,"enemy colored inventory conserved");
    require(r.event_count<=cm::event_limit&&r.own_macros_started<=max_macros,"world-event and common own-macro caps enforced");
}
void scarce_and_replacement(){
    auto f=std::make_unique<Fixture>();cm::Result a,b;f->info->groups[1].visible=false;cm::predict_into(f->input,true,a);cm::predict_into(f->input,false,b);
    require(a.supported&&b.supported&&a.our_secured==20&&b.our_secured==20,"same own single job in both stock laws");
    require(a.enemy_secured==0&&b.enemy_secured==20&&a.enemy_displaced_attempts==1,"scarce shared stock changes actual completed enemy conversion");
    require(at(a,cm::Kind::missed_acquisition,true)==9&&a.enemy_final_position!=f->input.enemy.actor.position,"old generation misses at physical approach checkpoint");conserved(a);conserved(b);
    f->info->groups[1].visible=true;f->info->settings.max_rounds=30;cm::predict_into(f->input,true,a);cm::predict_into(f->input,false,b);
    require(a.supported&&b.supported&&a.enemy_secured>=20,"rival recomputes replacement from unchanged cyclic rule");conserved(a);conserved(b);
    f->info->settings.max_rounds=14;cm::predict_into(f->input,false,b);require(b.enemy_secured==0&&b.enemy_final_bag==20,"acquired bag is not score before cutoff");
}
void all_jobs_and_pairs(){
    auto f=std::make_unique<Fixture>();cm::Result r;f->world.enemy_extra=100;f->info->settings.max_rounds=30;f->choose({JobKind::farm,0,1,0});
    f->info->own.bag=7;f->info->own.gold=3;cm::predict_into(f->input,true,r);
    require(r.supported&&count(r,cm::Kind::acquire,false)==2&&count(r,cm::Kind::convert,false)==1&&r.our_secured==47,"ordered pair acquires twice then banks whole bag once");
    require(r.final_gold==13&&r.final_position==f->info->altars[0].position,"pair conserves gold and physical endpoint");
    bool actual_second=false;for(int n=0;n<f->world.count;++n){const auto& q=f->world.requests[n];actual_second|=!q.enemy&&q.kind==cm::Leg::acquire&&q.group==1&&q.actor.position==f->info->groups[0].position&&q.actor.bag==27;}
    require(actual_second,"second leg receives actual first-capture state");conserved(r,7);
    for(const Job first:std::array<Job,3>{{{JobKind::bank,-1,-1,0},{JobKind::renew},{JobKind::hold}}}){
        auto g=std::make_unique<Fixture>();g->world.enemy_extra=100;g->info->settings.max_rounds=40;g->singles();g->input.first=first;
        g->info->own.bag=first.kind==JobKind::bank?7:0;g->info->own.health=first.kind==JobKind::renew?40:100;cm::predict_into(g->input,true,r);
        require(r.supported&&r.own_macros_started>1,"nonfarm first job continues with common fixed single cursor");require(count(r,cm::Kind::acquire,false)>0,"nonfarm job permits productive continuation");conserved(r,g->info->own.bag);
        if(first.kind==JobKind::renew)require(r.final_health==100&&at(r,cm::Kind::finish,false)==3,"renew uses actual spawn healing duration");}
    auto g=std::make_unique<Fixture>();g->world.enemy_extra=100;g->singles();g->info->settings.max_rounds=60;
    for(int first=0;first<g->menu.count;++first){g->input.first=g->menu.jobs[first];cm::Result a,b;cm::predict_into(g->input,true,a);cm::predict_into(g->input,false,b);
        require(a.supported&&b.supported,"entire identical fixed menu uses Commons law");conserved(a);conserved(b);}
}
void release_and_cycles(){
    auto f=std::make_unique<Fixture>();f->world.enemy_extra=100;f->info->settings.capture_rounds=0;f->info->groups[0].respawn_rounds=0;
    f->info->own.position=f->info->groups[0].position;f->menu.single_count=1;f->info->settings.max_rounds=40;cm::Result a,b;
    cm::predict_into(f->input,true,a);f->world.departure=false;cm::predict_into(f->input,true,b);
    require(a.supported&&b.supported&&at(a,cm::Kind::acquire,false,1)<at(b,cm::Kind::acquire,false,1),"planned departure releases stock before conservative completion-only bound");
    require(a.future_stock_predicted&&a.event_count==8&&a.truncated,"renewable stock stays bounded by eight chronological events");conserved(a);conserved(b);
    f=std::make_unique<Fixture>();f->info->settings.capture_rounds=0;f->info->settings.max_rounds=14;f->info->own.position=f->info->groups[0].position;
    f->input.enemy.actor.position={3,0};f->world.enemy_extra=2;f->world.bank_extra=10;f->info->groups[0].respawn_rounds=0;f->info->groups[1].visible=false;
    cm::predict_into(f->input,true,a);require(a.supported&&count(a,cm::Kind::acquire,true)>0&&a.enemy_displaced_attempts==1,"stock may replenish after planned departure while owner bank leg is still in flight");conserved(a);
    f=std::make_unique<Fixture>();f->info->groups[0].lifecycle=Lifecycle::respawning;f->info->groups[0].respawn_remaining=4;f->info->groups[1].visible=false;
    cm::predict_into(f->input,true,a);require(a.supported&&at(a,cm::Kind::acquire,false)==10&&a.future_stock_predicted,"initial empty-spawn cooldown includes full respawn service before capture");
    f->info->groups[0].occupied_spawn=true;cm::predict_into(f->input,true,a);require(!a.supported&&a.unsupported==cm::Unsupported::stock,"occupied initial cooldown is unknown stock");
}
void channels_order_and_censoring(){
    auto f=std::make_unique<Fixture>();cm::Result r;f->world.enemy_extra=100;f->info->own.bag=7;f->info->own.channel=Channel::tribute;f->info->own.channel_ticks=2;
    f->info->own.position=f->info->altars[0].position;f->choose({JobKind::hold});cm::predict_into(f->input,true,r);require(r.supported&&r.our_secured==7&&r.first_conversion_round==2,"HOLD preserves existing tribute and whole current bag");conserved(r,7);
    f=std::make_unique<Fixture>();f->world.enemy_extra=100;f->info->own.health=40;f->info->own.position={0,3};f->info->own.channel=Channel::recall;f->info->own.channel_ticks=2;f->choose({JobKind::hold});
    cm::predict_into(f->input,true,r);require(r.supported&&r.final_position==f->info->own_spawn&&r.final_health==100&&at(r,cm::Kind::finish,false)==5,"HOLD preserves recall arrival then spawn recovery");
    f=std::make_unique<Fixture>();f->world.enemy_extra=100;f->info->own.position=f->info->groups[0].position;f->info->in_progress_group_id=f->info->groups[0].id;f->info->groups[0].capture_started_round=0;f->choose({JobKind::hold});
    cm::predict_into(f->input,true,r);require(r.supported&&r.final_bag==20&&r.our_secured==0,"HOLD finishes supported capture without invented bank");conserved(r);
    f=std::make_unique<Fixture>();f->info->our_score=f->info->enemy_score=990;f->info->own.bag=f->input.enemy.actor.bag=10;f->info->own.position=f->info->altars[0].position;
    f->info->altar_count=2;f->info->altars[1]=f->info->altars[0];f->info->altars[1].id=91;f->info->altars[1].position=f->input.enemy.actor.position={1,1};f->choose({JobKind::bank,-1,-1,0});
    cm::predict_into(f->input,true,r);require(r.supported&&r.terminal_z==1&&r.event_count==1&&r.enemy_secured==0,"actual owner order stops second same-round winning conversion");
    f->world.order=false;cm::predict_into(f->input,true,r);require(r.supported&&r.terminal_z==-1&&r.event_count==1&&r.our_secured==0,"explicit order may remain fixed across rounds for unequal ranks");
    f=std::make_unique<Fixture>();f->input.enemy.actor.position={4,0};f->world.enemy_extra=1;cm::predict_into(f->input,true,r);require(!r.supported&&r.unsupported==cm::Unsupported::contested&&r.our_secured==0,"overlapping holders censored before invented capture");
}
void unavailable_and_budget(){
    auto f=std::make_unique<Fixture>();cm::Result r;f->priority.policy.matches_rule=false;cm::predict_into(f->input,true,r);require(!r.supported&&r.unsupported==cm::Unsupported::policy,"unknown policy unsupported");f->priority.policy.matches_rule=true;
    f->input.sole_producers_verified=false;cm::predict_into(f->input,true,r);require(!r.supported&&r.unsupported==cm::Unsupported::stock,"additional unrepresented producers unsupported");f->input.sole_producers_verified=true;
    f->input.order_at=nullptr;cm::predict_into(f->input,true,r);require(!r.supported&&r.unsupported==cm::Unsupported::order,"missing full-horizon order quote unsupported");f->input.order_at=order;
    f->world.allowed=false;f->info->work_budget={budget,&f->world};cm::predict_into(f->input,true,r);require(!r.supported&&r.unsupported==cm::Unsupported::budget&&!r.event_count,"initial budget denial publishes no resources");
    f->world.allowed=true;f->world.deny_quote=true;cm::predict_into(f->input,true,r);require(!r.supported&&r.unsupported==cm::Unsupported::budget&&!r.event_count,"quote losing budget publishes no acquisition");
    f->world.allowed=true;f->world.quote_false=true;cm::predict_into(f->input,true,r);require(!r.supported&&r.unsupported==cm::Unsupported::budget&&!r.event_count,"denied budget takes precedence over a callback returning no quote");
    f=std::make_unique<Fixture>();f->world.enemy_supported=false;cm::predict_into(f->input,true,r);require(!r.supported&&r.unsupported==cm::Unsupported::quote&&!r.event_count,"missing enemy combat/service premise is all-or-fallback rather than invented output");
    f=std::make_unique<Fixture>();f->input.enemy.entry_phase_supported=false;cm::predict_into(f->input,true,r);require(!r.supported&&r.unsupported==cm::Unsupported::enemy,"missing producer entry phase cannot be inferred");
    f=std::make_unique<Fixture>();f->world.enemy_extra=100;f->info->own.health=50;f->world.free_heal=true;cm::predict_into(f->input,true,r);require(!r.supported&&r.unsupported==cm::Unsupported::quote,"acquisition cannot supply free field healing");
    f->world.free_heal=false;f->world.early_renew=true;f->choose({JobKind::renew});cm::predict_into(f->input,true,r);require(!r.supported&&r.unsupported==cm::Unsupported::quote,"renew cannot restore before healing phases");
    f=std::make_unique<Fixture>();f->world.bad_bank=true;cm::predict_into(f->input,true,r);require(!r.supported&&r.unsupported==cm::Unsupported::quote&&r.our_secured==0,"bank cannot convert before authoritative ticks");
    f=std::make_unique<Fixture>();f->info->groups[0].provenance.status=Status::derived;cm::predict_into(f->input,true,r);require(!r.supported&&r.unsupported==cm::Unsupported::stock,"derived lifecycle is not assured stock");
    f=std::make_unique<Fixture>();f->info->public_layout_truncated=true;cm::predict_into(f->input,true,r);require(!r.supported&&r.unsupported==cm::Unsupported::stock,"partial public alternatives cannot claim denied output");
    f=std::make_unique<Fixture>();f->world.unknown_x=2;cm::Priority rejected;require(!cm::freeze_priority(*f->info,{3,0},true,{99,true,true,true},rejected),"unknown public distance cannot freeze priority");
    f=std::make_unique<Fixture>();f->world.unknown_x=6;f->input.enemy_travel={enemy_terrain,nullptr};f->input.enemy.actor.bag=7;
    cm::predict_into(f->input,false,r);require(r.supported&&r.enemy_secured>=7,"rival choices and physical quotes use rival-oriented spawn walkability instead of owner field");
    f->input.enemy_travel={};cm::predict_into(f->input,true,r);require(!r.supported&&r.unsupported==cm::Unsupported::quote,"missing rival terrain cannot be filled from owner cache");
}
void fixed_selector_and_cutoff(){
    auto f=std::make_unique<Fixture>();f->singles();f->info->groups[1].gems=30;auto forecasts=std::make_unique<std::array<Forecast,max_jobs>>();
    for(int arm=0;arm<2;++arm){
        for(int n=0;n<f->menu.count;++n){cm::Result result;f->input.first=f->menu.jobs[n];cm::predict_into(f->input,arm==0,result);require(result.supported,"every candidate remains under complete Commons stock law");
            auto& forecast=(*forecasts)[n];forecast.reset();forecast.terminal_z=result.terminal_z;forecast.our_secured=result.our_secured;forecast.enemy_secured=result.enemy_secured;
            forecast.first_conversion_round=result.first_conversion_round;}
        require(choose(*f->info,f->menu,*forecasts)==(arm==0?0:1),"same common selector changes choice only through completed shared-stock margin");
    }
    f=std::make_unique<Fixture>();f->info->settings.max_rounds=0;f->info->own.bag=7;f->choose({JobKind::bank,-1,-1,0});cm::Result result;
    cm::predict_into(f->input,true,result);require(result.supported&&!result.event_count&&result.final_bag==7,"zero decision horizon cannot start or complete a new bank");
    f=std::make_unique<Fixture>();f->info->groups[0].respawn_rounds=std::numeric_limits<int>::max();f->world.enemy_extra=100;cm::predict_into(f->input,true,result);
    require(result.supported&&result.our_secured==20,"cooldown beyond horizon saturates safely without fabricating another cycle");
}
}
int main(){scarce_and_replacement();all_jobs_and_pairs();release_and_cycles();channels_order_and_censoring();unavailable_and_budget();fixed_selector_and_cutoff();
    std::printf("commons checks=%d synthetic-only legal-stock-policy-and-leg-quotes=pending\n",checks);}
