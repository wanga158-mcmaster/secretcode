#include "../bulkheads/src/runtime.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
using namespace plan4;
namespace rt=plan4::runtime;
namespace binding=rt::bulkheads_binding;
namespace b=plan4::bulkheads;
namespace {
int checks=0;
void check(bool value,const char* message){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
Provenance seen(int round){return {round,Source::personal_vision,Status::exact_now};}
bool same(const Forecast& a,const Forecast& z) {
    if(a.event_count!=z.event_count||a.macros_started!=z.macros_started||a.final_round!=z.final_round||
        a.first_conversion_round!=z.first_conversion_round||a.our_secured!=z.our_secured||a.enemy_secured!=z.enemy_secured||
        a.final_bag!=z.final_bag||a.final_gold!=z.final_gold||a.final_health!=z.final_health||a.terminal_z!=z.terminal_z||
        a.terminal_unknown!=z.terminal_unknown||a.stock_unknown!=z.stock_unknown||a.travel_unknown!=z.travel_unknown||
        a.lock_unknown!=z.lock_unknown||a.truncated!=z.truncated||a.completed_conversion!=z.completed_conversion||
        a.no_conversion!=z.no_conversion||a.budget_exhausted!=z.budget_exhausted||a.abort_reason!=z.abort_reason)return false;
    for(int n=0;n<a.event_count;++n){const auto& x=a.events[n];const auto& y=z.events[n];
        if(x.kind!=y.kind||x.start.round!=y.start.round||x.start.phase!=y.start.phase||x.end.round!=y.end.round||
            x.end.phase!=y.end.phase||x.group_id!=y.group_id||x.bag_before!=y.bag_before||x.bag_after!=y.bag_after||
            x.secured!=y.secured||x.gold_after!=y.gold_after||x.health_after!=y.health_after||x.status!=y.status)return false;}
    return true;
}
struct Context {
    bool route=true,safe=true,complete=true,zero_exposure=true,no_heal=true,no_channel=true,order_known=true;
    bool deny=false,deny_in_route=false,deny_in_wait=false,deny_in_order=false,late_route_failure=false;
    int calls=0,waits=0,geometry=77,connector=10,blocker=7,point=2;
    static bool walkable(Point p,const void*){return p.y==0&&p.x>=0&&p.x<16;}
    static int distance(Point a,Point z,const void*){return walkable(a,nullptr)&&walkable(z,nullptr)?std::abs(a.x-z.x):-1;}
    static bool budget(const void* p){return !static_cast<const Context*>(p)->deny;}
    static bool dependencies(const PhaseEntry& e,const Information&,const rt::SpatialFrame&,binding::Dependencies& out,const void* p){
        auto& c=*const_cast<Context*>(static_cast<const Context*>(p));++c.calls;
        if(c.deny_in_route)c.deny=true;
        if(!c.route||(c.late_route_failure&&c.calls>=2)||!walkable(e.from,nullptr)||!walkable(e.to,nullptr)||
            e.nominal!=std::abs(e.from.x-e.to.x))return false;
        out.geometry_identity=c.geometry;out.complete=c.complete;
        if((e.from.x<c.point&&e.to.x>c.point)||(e.from.x>c.point&&e.to.x<c.point))
            out.connectors[out.count++]={c.connector,{c.point,0}};
        return true;
    }
    static bool safe_wait(const PhaseEntry& e,int rounds,const Information& i,binding::SafeWait& out,const void* p){
        auto& c=*const_cast<Context*>(static_cast<const Context*>(p));++c.waits;
        if(c.deny_in_wait)c.deny=true;
        out={i.round,i.own.id,rounds,e.from,std::uint64_t(c.geometry),true,c.zero_exposure,c.no_heal,c.no_channel};
        return c.safe;
    }
    static bool order_supported(int first,int last,const Information& i,const rt::ActorFrame& actors,const void* p){
        auto& c=*const_cast<Context*>(static_cast<const Context*>(p));if(c.deny_in_order)c.deny=true;
        return c.order_known&&first>=i.round&&last>=first&&
            actors.clock.round==i.round&&actors.clock.own.id==i.own.id;
    }
};
struct Fixture {
    std::unique_ptr<Information> info=std::make_unique<Information>();
    Menu menu{};Context context{};binding::Profile profile{};rt::ActorFrame actors{};binding::CauseFrame causes{};rt::SpatialFrame spatial{};
    Fixture(){
        auto& i=*info;i.round=1000;i.own.id=0;i.own.position={0,0};i.own_spawn={0,0};i.own.health=i.own.maximum_health=1000;
        i.travel={Context::distance,&context};i.group_count=1;i.altar_count=1;i.settings.max_rounds=1400;
        i.altars[0].id=50;i.altars[0].position={0,0};i.altars[0].altar_position={0,1};i.altars[0].lock_known=true;
        auto& g=i.groups[0];g.id=20;g.position={4,0};g.full_health=100;g.health=0;g.clear_rounds=0;g.full_clear_rounds=1;
        g.gems=24;g.gold=5;g.respawn_rounds=1;g.visible=g.all_spawns_visible=g.occupancy_known=g.reward_available=true;
        g.lifecycle=Lifecycle::cleared;g.provenance=g.health_provenance=seen(i.round);
        menu.count=menu.single_count=1;menu.jobs[0].kind=JobKind::farm;menu.jobs[0].group=0;menu.jobs[0].altar=0;
        b::State table;
        for(int n=0;n<8;++n){const int start=10+n*100,duration=24+n*4,id=n+1;
            b::Onset o;o.episode_id=id;o.connector_id=100+id;o.blocker_id=200+id;o.round=start;
            o.current=seen(start);o.preceding=seen(start-1);o.preceding_clear=true;o.policy={42,true,true};b::observe_onset(table,o);
            for(int r=start+1;r<=start+duration;++r)b::observe(table,{id,r,true,r<start+duration,false,seen(r),o.connector_id,o.blocker_id});}
        check(b::freeze_fit(table),"synthetic first-eight reference table validates");
        profile.fit=table.fit;profile.policy={42,true,true};profile.table_identity=123;profile.geometry_identity=77;profile.production_frozen=true;
        profile.dependencies=Context::dependencies;profile.safe_wait=Context::safe_wait;profile.order_supported=Context::order_supported;profile.context=&context;
        update_frames(true);
    }
    void update_frames(bool onset=false){auto& i=*info;
        for(int g=0;g<i.group_count;++g)if(i.groups[g].visible)i.groups[g].provenance=i.groups[g].health_provenance=seen(i.round);
        actors.clock={i.round,{i.own.id}};actors.own_observed=true;actors.count=1;
        actors.own.id=i.own.id;actors.own.position=i.own.position;actors.own.health=i.own.health;actors.own.maximum_health=i.own.maximum_health;
        actors.own.bag=i.own.bag;actors.own.alive=actors.own.ally=true;actors.own.provenance={i.round,Source::own_state,Status::exact_now};
        actors.samples[0].id=context.blocker;actors.samples[0].position={context.point,0};actors.samples[0].alive=true;
        actors.samples[0].provenance=seen(i.round);
        causes.clock=actors.clock;causes.count=1;auto& s=causes.samples[0];s.episode_id=100;s.connector_id=context.connector;
        s.blocker_id=context.blocker;s.position={context.point,0};s.provenance=seen(i.round);s.blocked=s.personally_visible=true;
        s.onset=onset;s.preceding_clear=onset;s.preceding=seen(i.round-1);s.policy={42,true,true};
        spatial.clock=actors.clock;spatial.information=info.get();spatial.menu=&menu;
        spatial.topology={16,1,true,Context::walkable,&context};spatial.travel=i.travel;spatial.current_complete=true;
    }
    void feed(rt::Bulkheads& model,bool configure=true){if(configure)check(model.configure(profile),"explicit production profile accepted");
        model.observe_actors(actors);model.observe_snapshot(*info);model.observe_spatial(spatial);check(model.observe_causes(causes),"legal current cause frame accepted");}
};
std::unique_ptr<Forecast> prediction(rt::Bulkheads& model,Fixture& f,int n=0){auto out=std::make_unique<Forecast>();model.predict_into(*f.info,f.menu.jobs[n],f.menu,*out);return out;}
void exact_fallback(rt::Bulkheads& model,Fixture& f,const char* message){auto out=prediction(model,f),base=std::make_unique<Forecast>();
    plan4::predict_into(*f.info,f.menu.jobs[0],f.menu,*base);check(same(*out,*base),message);check(!model.model_evidence().supported,"fallback is not reported as supported law");}
void positive_conditional_repeat(){
    Fixture f;rt::Bulkheads active,control(false);f.feed(active);f.feed(control);
    auto a=prediction(active,f),c=prediction(control,f);
    check(a->completed_conversion&&c->completed_conversion,"both laws complete actual represented conversions");
    check(a->first_conversion_round<c->first_conversion_round,"absolute common cause avoids repeated control delay");
    int aw=0,cw=0;for(int n=0;n<a->event_count;++n)aw+=a->events[n].kind==EventKind::wait;
    for(int n=0;n<c->event_count;++n)cw+=c->events[n].kind==EventKind::wait;
    check(aw<cw&&f.context.calls>=4,"same current cause is queried on multiple chronological travel legs");
    check(a->final_health==1000&&c->final_health==1000,"added waits do not manufacture camp exposure or healing");
    check(active.model_evidence().supported&&active.model_evidence().coupling_enabled&&active.model_evidence().samples==8,"active diagnostics report conditional fitted law only");
    check(control.model_evidence().supported&&!control.model_evidence().coupling_enabled,"control retains fitted law with coupling disabled");
    auto again=prediction(active,f);check(same(*a,*again),"candidate path clocks reset independently");
    std::printf("conditional repeat first conversion active=%d control=%d; wait events=%d/%d\n",a->first_conversion_round,c->first_conversion_round,aw,cw);
}
void profiles_and_default_mains(){
    Fixture f;
    for(bool enabled:{true,false}){rt::Bulkheads empty(enabled);exact_fallback(empty,f,"ordinary play without calibrated profile is exact M0");}
    for(int mutation=0;mutation<12;++mutation){auto p=f.profile;
        if(mutation==0)p.production_frozen=false;
        if(mutation==1)p.fit.started=7;
        if(mutation==2)p.policy.release_independent_of_own_actions=false;
        if(mutation==3)p.fit.episodes[0].censor=b::Censor::sight_lost;
        if(mutation==4)p.fit.episodes[0].complete=false;
        if(mutation==5)p.fit.episodes[0].policy=43;
        if(mutation==6)p.fit.durations[0]++;
        if(mutation==7)p.fit.episodes[1].id=p.fit.episodes[0].id;
        if(mutation==8)p.table_identity=0;
        if(mutation==9)p.dependencies=nullptr;
        if(mutation==10)p.safe_wait=nullptr;
        if(mutation==11)p.order_supported=nullptr;
        for(bool enabled:{true,false}){rt::Bulkheads model(enabled);check(!model.configure(p),"invalid or incomplete production cohort fails closed");exact_fallback(model,f,"invalid fit never alters either arm");}}
}
void missing_sources_and_frames(){
    for(int mutation=0;mutation<10;++mutation)for(bool enabled:{true,false}){Fixture f;rt::Bulkheads model(enabled);f.feed(model);
        if(mutation==0){++f.info->round;}
        if(mutation==1){auto stale=f.actors;--stale.clock.round;model.observe_actors(stale);}
        if(mutation==2){auto stale=f.spatial;--stale.clock.round;model.observe_spatial(stale);}
        if(mutation==3){f.info->own.health=999;}
        if(mutation==4){f.info->own.id=2;}
        if(mutation==5){f.info->public_layout_truncated=true;}
        if(mutation==6){auto unknown=f.spatial;unknown.topology.complete=false;model.observe_spatial(unknown);}
        if(mutation==7){auto bad=f.causes;bad.truncated=true;check(!model.observe_causes(bad),"truncated frame invalidates current causes");}
        if(mutation==8){check(!model.observe_causes(f.causes),"duplicate same-round frame cannot keep exact support");}
        if(mutation==9){auto malformed=f.actors;malformed.count=10;model.observe_actors(malformed);}
        exact_fallback(model,f,"missing/stale/changed source restores whole-candidate M0");}
}
void route_generation_and_safe_wait(){
    for(int mutation=0;mutation<11;++mutation)for(bool enabled:{true,false}){Fixture f;rt::Bulkheads model(enabled);f.feed(model);
        if(mutation==0)f.context.route=false;
        if(mutation==1)f.context.complete=false;
        if(mutation==2)f.context.geometry=78;
        if(mutation==3)f.context.connector=11;
        if(mutation==4)f.context.point=3;
        if(mutation==5)f.context.safe=false;
        if(mutation==6)f.context.zero_exposure=false;
        if(mutation==7)f.context.no_heal=false;
        if(mutation==8)f.context.no_channel=false;
        if(mutation==9)f.context.late_route_failure=true;
        if(mutation==10)f.context.order_known=false;
        exact_fallback(model,f,"unknown route/epoch/unsafe or later unsupported leg resets full forecast");}
    Fixture f;rt::Bulkheads model;f.causes.samples[0].policy.release_independent_of_own_actions=false;f.feed(model);
    exact_fallback(model,f,"new runtime episode cannot borrow frozen law without its own exogenous policy receipt");
    Fixture healing;healing.info->own.health=999;healing.info->own.on_spawn=true;healing.update_frames(true);
    rt::Bulkheads heal;healing.feed(heal);exact_fallback(heal,healing,"positive stationary delay on injured spawn cannot omit real healing");
    Fixture capturing;capturing.info->group_count=2;capturing.info->groups[1]=capturing.info->groups[0];
    capturing.info->groups[1].id=21;capturing.info->groups[0].position={0,0};capturing.menu.jobs[0].group=1;
    capturing.info->in_progress_group_id=20;capturing.info->groups[0].capture_started_round=999;capturing.update_frames(true);
    rt::Bulkheads capture;capturing.feed(capture);exact_fallback(capture,capturing,"positive initial delay cannot invent or omit ongoing capture completion");
}
void continuous_frames_and_new_generation(){
    Fixture f;rt::Bulkheads model;f.feed(model);
    ++f.info->round;f.update_frames();f.feed(model,false);check(prediction(model,f)->completed_conversion,"continuous witnessed persistence preserves supported age");
    ++f.info->round;f.update_frames();f.causes.count=0;f.feed(model,false);exact_fallback(model,f,"omitted connector evidence cannot stand for release");
    ++f.info->round;f.update_frames();f.feed(model,false);exact_fallback(model,f,"later sight after gap cannot repair unknown onset");
    ++f.info->round;f.update_frames(true);f.causes.samples[0].episode_id=101;f.causes.samples[0].blocker_id=8;
    f.actors.samples[0].id=8;f.feed(model,false);check(model.model_evidence().revision>0,"new occupant source has a distinct reconciliation revision");
    check(prediction(model,f)->completed_conversion&&model.model_evidence().supported,"new generation requires fresh witnessed onset and current body");
    ++f.info->round;f.update_frames();f.causes.samples[0].episode_id=101;f.causes.samples[0].blocker_id=8;
    f.actors.samples[0].id=9;model.observe_actors(f.actors);model.observe_snapshot(*f.info);model.observe_spatial(f.spatial);
    check(!model.observe_causes(f.causes),"different current occupant cannot continue prior episode");
    exact_fallback(model,f,"new occupant without a supported current epoch restores M0");
}
void rejected_observations(){
    for(int mutation=0;mutation<8;++mutation){Fixture f;rt::Bulkheads model;f.feed(model);++f.info->round;f.update_frames();
        if(mutation==0)f.causes.count=-1;
        if(mutation==1)f.causes.count=9;
        if(mutation==2)f.causes.samples[0].provenance=seen(f.info->round-1);
        if(mutation==3)f.causes.samples[0].action_conditioned=true;
        if(mutation==4)f.causes.samples[0].personally_visible=false;
        if(mutation==5)f.actors.samples[0].id=9;
        if(mutation==6)f.causes.samples[0].blocker_id=8;
        if(mutation==7){f.causes.count=2;f.causes.samples[1]=f.causes.samples[0];}
        model.observe_actors(f.actors);model.observe_snapshot(*f.info);model.observe_spatial(f.spatial);
        check(!model.observe_causes(f.causes),"illegal/missing/duplicate/body-mismatched cause frame rejected");exact_fallback(model,f,"rejected observation cannot preserve old current support");}
    Fixture f;rt::Bulkheads changed;f.feed(changed);f.actors.samples[0].position={3,0};changed.observe_actors(f.actors);
    exact_fallback(changed,f,"new same-clock actor observation cannot reuse old connector occupancy");
    Fixture contradiction;rt::Bulkheads clear;check(clear.configure(contradiction.profile),"clear contradiction uses valid immutable profile");
    contradiction.causes.samples[0].blocked=false;clear.observe_actors(contradiction.actors);
    clear.observe_snapshot(*contradiction.info);clear.observe_spatial(contradiction.spatial);
    check(!clear.observe_causes(contradiction.causes),"visible current blocker contradicts connector-clear sample");
    exact_fallback(clear,contradiction,"contradictory connector-clear claim cannot erase a live block");
}
void nonfarm_pairs_batch_and_no_dependency(){
    Fixture f;rt::Bulkheads model;f.info->group_count=2;f.info->groups[1]=f.info->groups[0];f.info->groups[1].id=21;f.info->groups[1].position={6,0};
    f.menu.count=5;f.menu.single_count=2;f.menu.jobs[1]=f.menu.jobs[0];f.menu.jobs[1].group=1;f.menu.jobs[1].menu_rank=1;
    f.menu.jobs[2]=f.menu.jobs[0];f.menu.jobs[2].second_group=1;f.menu.jobs[2].menu_rank=2;
    f.menu.jobs[3].kind=JobKind::bank;f.menu.jobs[3].altar=0;f.menu.jobs[3].menu_rank=3;
    f.menu.jobs[4].kind=JobKind::hold;f.menu.jobs[4].menu_rank=4;
    f.info->own.bag=11;f.update_frames(true);f.feed(model);
    auto eval=std::make_unique<Evaluation>();rt::evaluate_model(*f.info,f.menu,model,*eval);
    check(eval->evaluated_count==5&&eval->menu.count==5&&!eval->budget_exhausted,"same fixed menu includes pair BANK and HOLD laws");
    check(eval->forecasts[2].completed_conversion&&eval->forecasts[2].our_secured>=59,"two ordered acquisitions convert actual combined bag once");
    check(eval->chosen==choose(*f.info,f.menu,eval->forecasts),"shared selector ranks all completed rows");
    f.info->own.health=500;f.info->own.on_spawn=true;f.menu.jobs[0].kind=JobKind::renew;f.menu.single_count=0;f.update_frames(true);
    rt::Bulkheads renew;f.feed(renew);auto healed=prediction(renew,f);check(healed->final_health==1000,"RENEW retains ordinary common healing without private wait insertion");
    Fixture clear;clear.causes.samples[0].blocked=false;clear.causes.samples[0].onset=false;clear.actors.count=0;
    rt::Bulkheads free;clear.feed(free);auto unblocked=prediction(free,clear),base=std::make_unique<Forecast>();plan4::predict_into(*clear.info,clear.menu.jobs[0],clear.menu,*base);
    check(same(*unblocked,*base)&&free.model_evidence().supported,"current exact connector-clear evidence adds no delay");
    Fixture no_route;no_route.context.point=12;no_route.update_frames(true);rt::Bulkheads absent;no_route.feed(absent);
    auto no_delay=prediction(absent,no_route);plan4::predict_into(*no_route.info,no_route.menu.jobs[0],no_route.menu,*base);
    check(same(*no_delay,*base)&&absent.model_evidence().supported,"complete public no-dependency route remains a supported zero-effect law");
}
void budget_reserve(){
    for(int where=0;where<4;++where)for(bool enabled:{true,false}){Fixture f;rt::Bulkheads model(enabled);f.info->work_budget={Context::budget,&f.context};f.feed(model);
        if(where==0)f.context.deny=true;
        if(where==1)f.context.deny_in_route=true;
        if(where==2)f.context.deny_in_wait=true;
        if(where==3)f.context.deny_in_order=true;
        auto out=prediction(model,f),base=std::make_unique<Forecast>();plan4::predict_into(*f.info,f.menu.jobs[0],f.menu,*base);
        check(out->budget_exhausted&&same(*out,*base),"denied budget publishes only the same denied-budget M0 prefix");
        check(!model.model_evidence().supported&&model.model_evidence().reason==int(binding::Reason::budget),"budget failure is never mislabeled unavailable quote");
        check(where!=0||(f.context.calls==0&&f.context.waits==0),"no route or wait callbacks after initial denial");
        check(where!=1||f.context.waits==0,"route callback denial prevents safe-wait callback");}
}
}
int main(){positive_conditional_repeat();profiles_and_default_mains();missing_sources_and_frames();route_generation_and_safe_wait();
    continuous_frames_and_new_generation();rejected_observations();nonfarm_pairs_batch_and_no_dependency();budget_reserve();
    std::printf("plan4-bulkheads-runtime: %d checks passed (synthetic conditional premises; no engine witnesses)\n",checks);
}
