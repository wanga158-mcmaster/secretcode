#include "../commons/src/runtime.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
using namespace plan4;
namespace cb=plan4::commons_binding;
namespace cm=plan4::commons;
namespace rt=plan4::runtime;
namespace {
int checks=0;
void require(bool okay,const char*why){++checks;if(!okay){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
struct World {
    bool allowed=true,quote_supported=true,quote_safe=true,wrong_actor=false,order_supported=true,own_first=true,flip_order=true;
    int own_extra=0,enemy_extra=5,unknown_enemy_x=-1,deny_at_quote=-1,fail_group=-1;
    int own_distances=0,enemy_distances=0,quotes=0;
    std::uint64_t geometry=0;
};
int travel(Point a,Point b,const void*context){auto&w=*const_cast<World*>(static_cast<const World*>(context));++w.own_distances;
    return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
int enemy_travel(Point a,Point b,const void*context){auto&w=*const_cast<World*>(static_cast<const World*>(context));++w.enemy_distances;
    if(a.x==w.unknown_enemy_x||b.x==w.unknown_enemy_x)return-1;
    return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
bool budget(const void*context){return static_cast<const World*>(context)->allowed;}
bool order(int round,bool&own_first,const void*context){const auto&w=*static_cast<const World*>(context);own_first=w.own_first^bool(w.flip_order&&(round&1));return w.order_supported;}
bool walkable(Point p,const void*){return p.x>=0&&p.x<30&&p.y>=0&&p.y<30;}
rt::TileOccupancy occupancy(Point,const void*){return rt::TileOccupancy::free;}
bool quote(const cm::LegRequest&r,const Information&info,cb::LegalLegQuote&q,const void*context){
    auto&w=*const_cast<World*>(static_cast<const World*>(context));++w.quotes;if(w.quotes==w.deny_at_quote)w.allowed=false;
    if(!w.quote_supported||(r.group==w.fail_group&&r.kind==cm::Leg::acquire))return false;
    q={};q.clock={info.round,{info.own.id}};q.profile=99;q.geometry=w.geometry;q.actor_id=r.actor.id+(w.wrong_actor?1:0);
    q.enemy=r.enemy;q.physical_route=q.phase_schedule=true;q.safety_and_occupancy=w.quote_safe;
    auto&p=q.plan;p.supported=true;p.entry_round=r.start;p.entry_health=p.end_health=p.failure_health=r.actor.health;
    p.end_position=p.failure_position=r.actor.position;p.departure_supported=true;p.departure_round=r.start;
    if(r.kind==cm::Leg::acquire){const auto&g=info.groups[r.group];const int d=r.travel.distance(r.actor.position,g.position,r.travel.context);if(d<0)return false;
        p.entry_round+=d+(r.enemy?w.enemy_extra:w.own_extra)+(r.stock.regenerated?g.full_clear_rounds:0);
        p.completion_round=r.current_capture?std::max(r.start+1,g.capture_started_round+info.settings.capture_rounds):p.entry_round+1+info.settings.capture_rounds;
        p.completion_phase=Phase::capture;p.end_position=g.position;
        if(r.actor.position!=g.position){p.failure_position={g.position.x+(r.actor.position.x>g.position.x)-(r.actor.position.x<g.position.x),
            g.position.y+(r.actor.position.y>g.position.y)-(r.actor.position.y<g.position.y)};p.failure_supported=true;}
    }else if(r.kind==cm::Leg::bank){const Point target=r.existing_channel?r.actor.position:info.altars[r.altar].position;
        const int d=r.travel.distance(r.actor.position,target,r.travel.context);if(d<0)return false;p.entry_round+=d;p.end_position=target;
        p.completion_round=p.entry_round+(r.existing_channel?r.channel_ticks:tribute_ticks(r.actor.bag,info.settings));
        p.completion_phase=r.actor.bag?Phase::owner_start:Phase::owner_action;
    }else if(r.kind==cm::Leg::hold){p.completion_round=r.start+1;p.completion_phase=Phase::owner_action;}
    else{p.completion_phase=Phase::owner_action;if(r.actor.health==r.actor.maximum_health)p.completion_round=r.start+1;
        else{p.recall_safe=true;const bool spawn=r.actor.position==info.own_spawn||(r.start==info.round&&info.own.on_spawn);
            if(!spawn){p.entry_round+=r.existing_channel?r.channel_ticks:info.settings.recall_rounds;p.end_position=info.own_spawn;}
            p.completion_round=p.entry_round+int(std::ceil((r.actor.maximum_health-r.actor.health)/(r.actor.maximum_health*info.settings.spawn_heal_fraction)));
            p.end_health=r.actor.maximum_health;}}
    return true;
}
struct Fixture {
    std::unique_ptr<Information>info=std::make_unique<Information>();
    std::unique_ptr<rt::Commons>model=std::make_unique<rt::Commons>();
    std::unique_ptr<Evaluation>out=std::make_unique<Evaluation>(),baseline=std::make_unique<Evaluation>();
    Menu menu{};World world{};rt::ActorFrame actors{};rt::SpatialFrame spatial{};
    std::array<rt::PublicGroup,2>groups{};std::array<rt::PublicMember,2>members{};std::array<rt::VisibleMember,2>visible{};
    rt::PublicLayoutFrame layout{};cb::PublicProfile profile{};cb::IsolationFrame isolation{};cb::EntryFrame entry{};
    Fixture(){
        info->settings.max_rounds=16;info->settings.capture_rounds=1;info->own.id=7;info->own.position={0,0};info->own_spawn={0,0};
        info->own.health=info->own.maximum_health=100;info->own.alive=info->own.recall_safe=true;info->group_count=2;info->altar_count=1;
        info->travel={travel,&world};info->work_budget={budget,&world};
        for(int g=0;g<2;++g){auto&row=info->groups[g];row.id=101+g;row.position={2+2*g,0};row.full_health=100;row.health=0;
            row.gems=20;row.gold=5;row.respawn_rounds=100;row.full_clear_rounds=2;row.clear_rounds=0;row.lifecycle=Lifecycle::cleared;
            row.visible=row.all_spawns_visible=row.occupancy_known=row.reward_available=true;
            row.provenance=row.health_provenance={0,Source::personal_vision,Status::exact_now};
            groups[g]={row.id,g,1,true};members[g]={row.id,501+g,0,1,0,3,row.position,100};visible[g]={row.id,501+g,0,1,row.position,0,row.provenance};
            entry.stocks[g]={row.id,0,row.lifecycle,row.provenance};}
        auto&a=info->altars[0];a.id=90;a.position={0,1};a.altar_position={0,2};a.lock_known=true;
        menu.count=1;menu.jobs[0]={JobKind::farm,0,-1,0};
        actors.clock={0,{7}};actors.allied_roster[0]=7;actors.enemy_roster[0]=9;actors.allied_count=actors.enemy_count=1;
        actors.roster_complete=actors.own_observed=true;actors.own.id=7;actors.own.ally=actors.own.alive=true;actors.own.health=actors.own.maximum_health=100;
        actors.own.provenance={0,Source::own_state,Status::exact_now};actors.count=1;
        auto&v=actors.samples[0];v.id=9;v.position={6,0};v.health=v.maximum_health=100;v.alive=true;v.provenance={0,Source::personal_vision,Status::exact_now};
        spatial.clock=actors.clock;spatial.information=info.get();spatial.menu=&menu;spatial.topology={30,30,true,walkable,nullptr};
        spatial.travel=info->travel;spatial.occupancy=occupancy;spatial.current_complete=true;
        layout={actors.clock,groups.data(),2,members.data(),2,false};
        profile.identity=99;profile.rival_policy={99,true,true,true};profile.own_idle_policy={98,true,true,true};
        profile.allied_roster=actors.allied_roster;profile.enemy_roster=actors.enemy_roster;profile.allied_count=profile.enemy_count=1;
        profile.own_producer=7;profile.enemy_producer=9;profile.own_spawn=info->own_spawn;profile.enemy_spawn={8,0};
        profile.configured=profile.public_spawns_supported=profile.rival_orientation_supported=true;profile.enemy_travel={enemy_travel,&world};
        profile.order_at=order;profile.order_context=&world;profile.order_first_round=0;profile.order_last_round=200;profile.order_published=true;
        profile.own_rank=profile.enemy_rank=0;profile.fixed_rank_certificate=true;
        profile.quote_leg=quote;profile.quote_context=&world;
        isolation={actors.clock,99,true,true,true,true};entry.clock=actors.clock;entry.profile=99;entry.enemy_id=9;entry.cursor=1;entry.next_action_round=0;
        entry.provenance={0,Source::personal_vision,Status::derived};entry.history_complete=entry.idle_policy_boundary=true;entry.stock_count=2;
        reseal();
    }
    void reseal(){world.geometry=profile.geometry=cb::geometry(*info,layout);model->configure(profile);}
    void observe(){model->observe_actors(actors);model->observe_isolation(isolation);model->observe_entry(entry);model->observe_spatial(spatial);
        model->observe_layout(layout);model->observe_snapshot(*info,{visible.data(),2,false});}
    void run(){observe();rt::evaluate_model(*info,menu,*model,*out);}
    void singles(){menu.single_count=2;menu.count=6;menu.jobs[0]={JobKind::farm,0,-1,0};menu.jobs[1]={JobKind::farm,1,-1,0};
        menu.jobs[2]={JobKind::farm,0,1,0};menu.jobs[3]={JobKind::bank,-1,-1,0};menu.jobs[4]={JobKind::renew};menu.jobs[5]={JobKind::hold};}
};
bool equal(const Forecast&a,const Forecast&b){
    if(a.event_count!=b.event_count||a.macros_started!=b.macros_started||a.final_round!=b.final_round||a.first_conversion_round!=b.first_conversion_round
        ||a.our_secured!=b.our_secured||a.enemy_secured!=b.enemy_secured||a.final_bag!=b.final_bag||a.final_gold!=b.final_gold||a.final_health!=b.final_health
        ||a.terminal_z!=b.terminal_z||a.terminal_unknown!=b.terminal_unknown||a.stock_unknown!=b.stock_unknown||a.travel_unknown!=b.travel_unknown
        ||a.lock_unknown!=b.lock_unknown||a.truncated!=b.truncated||a.completed_conversion!=b.completed_conversion||a.no_conversion!=b.no_conversion
        ||a.budget_exhausted!=b.budget_exhausted||a.abort_reason!=b.abort_reason)return false;
    for(int n=0;n<a.event_count;++n){const auto&x=a.events[n];const auto&y=b.events[n];if(x.kind!=y.kind||x.group_id!=y.group_id||x.start.round!=y.start.round||x.start.phase!=y.start.phase
        ||x.end.round!=y.end.round||x.end.phase!=y.end.phase||x.bag_before!=y.bag_before||x.bag_after!=y.bag_after||x.secured!=y.secured||x.gold_after!=y.gold_after||x.health_after!=y.health_after||x.status!=y.status)return false;}
    return true;
}
void fallback(Fixture&f,const char*why){f.run();plan4::evaluate_into(*f.info,f.menu,*f.baseline);require(!f.model->used_commons(),why);
    require(f.out->evaluated_count==f.baseline->evaluated_count&&f.out->chosen==f.baseline->chosen,"whole fallback keeps completed common menu and selector");
    for(int n=0;n<f.out->evaluated_count;++n)require(equal(f.out->forecasts[n],f.baseline->forecasts[n]),"every fallback forecast exactly M0 including earlier refined prefixes");}
struct Receipts {std::array<rt::ModelPredictionEvent,8>events{};int count=0,limit=8;};
bool sink(const rt::ModelPredictionEvent&e,void*context){auto&r=*static_cast<Receipts*>(context);if(r.count>=r.limit)return false;r.events[r.count++]=e;return r.count<r.limit;}
bool core_quote(const cm::LegRequest&r,const Information&info,cm::LegPlan&p,const void*context){cb::LegalLegQuote q;if(!quote(r,info,q,context))return false;p=q.plan;return true;}
void parity(Fixture&f,bool enabled){
    auto perspective=std::make_unique<Information>(*f.info);perspective->travel=f.profile.enemy_travel;cm::Priority priority;
    require(cm::freeze_priority(*perspective,f.profile.enemy_spawn,true,f.profile.rival_policy,priority),"rival priority matches retained shared fixture rule");
    cm::Input input;input.info=f.info.get();input.menu=&f.menu;input.priority=&priority;input.sole_producers_verified=input.order_known=true;
    const auto&v=f.actors.samples[0];input.enemy.actor={v.id,v.position,v.health,v.maximum_health,v.bag,0};input.enemy.provenance=v.provenance;
    input.enemy.personally_visible=input.enemy.entry_phase_supported=true;input.enemy.cursor=f.entry.cursor;input.enemy.next_action_round=f.entry.next_action_round;
    input.enemy_travel=f.profile.enemy_travel;input.quote_leg=core_quote;input.quote_context=&f.world;input.order_at=order;input.order_context=&f.world;
    for(int n=0;n<f.menu.count;++n){input.first=f.menu.jobs[n];cm::Result r;cm::predict_into(input,enabled,r);const auto&p=f.out->forecasts[n];
        require(r.supported&&p.our_secured==r.our_secured&&p.enemy_secured==r.enemy_secured&&p.first_conversion_round==r.first_conversion_round,
            "full runtime output equals chronological core for each fixed candidate");
        require(p.final_bag==r.final_bag&&p.final_health==r.final_health&&p.final_gold==r.final_gold&&p.terminal_z==r.terminal_z&&p.macros_started==r.own_macros_started,
            "terminal health cash bag and cyclic count retained without posthoc ally output");}
}
void positive_and_control(){auto f=std::make_unique<Fixture>();f->run();require(f->model->used_commons()&&f->out->evaluated_count==1,"conditional complete shared-stock path binds");
    require(f->out->forecasts[0].our_secured==20&&f->out->forecasts[0].enemy_secured==0,"shared source changes completed rival conversion");
    require(f->out->forecasts[0].event_count==0,"economic tape does not fabricate intermediate HP/gold states");
    require(f->world.enemy_distances>0&&f->world.own_distances>0,"both terrain perspectives used explicitly");
    auto active=std::make_unique<Forecast>(f->out->forecasts[0]);f->model=std::make_unique<rt::Commons>(false);f->reseal();f->run();
    require(f->model->used_commons()&&f->out->forecasts[0].our_secured==active->our_secured&&f->out->forecasts[0].enemy_secured==20,"control changes only persistent shared-stock coupling");
    require(!f->model->model_evidence().coupling_enabled&&f->model->model_evidence().supported,"control reports supported exogenous schedule");
    require(!rt::Commons::live_profile_producer_available,"SDK mains advertise missing live profile producer honestly");}
void full_menu(){auto f=std::make_unique<Fixture>();f->singles();f->info->settings.max_rounds=45;f->world.enemy_extra=100;f->run();
    require(f->model->used_commons()&&f->out->evaluated_count==6,"same six candidates include ordered pair bank renew hold");
    require(f->out->forecasts[2].our_secured>=40&&f->out->forecasts[2].macros_started>1,"ordered pair and common cyclic continuation both execute");
    require(f->out->menu.count==6&&f->out->menu.single_count==2,"private adapter preserves fixed menu");
    for(int n=0;n<6;++n)require(f->out->forecasts[n].macros_started<=8,"every full path respects common macro cap");
    require(f->model->model_evidence().paths==6&&f->model->model_evidence().samples==1,"reported evidence counts completed paths and one bound rival");
    parity(*f,true);f->model=std::make_unique<rt::Commons>(false);f->reseal();f->run();require(f->model->used_commons(),"full control path uses same six candidates");parity(*f,false);}
void choice_and_reflection(){auto f=std::make_unique<Fixture>();f->singles();f->info->groups[1].gems=30;f->reseal();f->run();
    require(f->model->used_commons()&&f->out->chosen==0,"conditional scarce shared source makes first farm preferable");
    const auto active_ours=f->out->forecasts[0].our_secured,active_enemy=f->out->forecasts[0].enemy_secured;
    f->model=std::make_unique<rt::Commons>(false);f->reseal();f->run();require(f->model->used_commons()&&f->out->chosen==1,"control chooses larger disjoint harvest from completed team output");
    require(f->out->forecasts[1].our_secured==30&&f->out->forecasts[1].enemy_secured==20,"rank disagreement uses real predicted conversions, no denial bonus");
    auto reflect=[](Point p){return Point{20-p.x,p.y};};
    f->info->own.position=reflect(f->info->own.position);f->info->own_spawn=reflect(f->info->own_spawn);f->actors.own.position=f->info->own.position;
    f->actors.samples[0].position=reflect(f->actors.samples[0].position);f->profile.own_spawn=f->info->own_spawn;f->profile.enemy_spawn=reflect(f->profile.enemy_spawn);
    for(int g=0;g<2;++g){f->info->groups[g].position=reflect(f->info->groups[g].position);f->members[g].spawn=f->info->groups[g].position;f->visible[g].spawn=f->info->groups[g].position;}
    f->info->altars[0].position=reflect(f->info->altars[0].position);f->info->altars[0].altar_position=reflect(f->info->altars[0].altar_position);
    f->model=std::make_unique<rt::Commons>(true);f->reseal();f->run();require(f->model->used_commons()&&f->out->chosen==0
        &&f->out->forecasts[0].our_secured==active_ours&&f->out->forecasts[0].enemy_secured==active_enemy,"reflected public geometry preserves active rank and economic law");
    parity(*f,true);f->model=std::make_unique<rt::Commons>(false);f->reseal();f->run();require(f->model->used_commons()&&f->out->chosen==1,"reflected control keeps same named cut");parity(*f,false);
}
void missing_contracts(){
    {auto f=std::make_unique<Fixture>();f->model=std::make_unique<rt::Commons>();fallback(*f,"unknown ordinary opponent uses exact M0");require(!f->model->isolation_idle(f->actors.clock),"no-profile ordinary actors never isolated");}
    {auto f=std::make_unique<Fixture>();f->entry.history_complete=false;fallback(*f,"private cursor not inferred from profile identity");}
    {auto f=std::make_unique<Fixture>();f->actors.enemy_count=2;f->actors.enemy_roster[1]=8;f->profile.enemy_count=2;f->profile.enemy_roster=f->actors.enemy_roster;
        f->model->configure(f->profile);fallback(*f,"nonminimum rival ID cannot replace inspectable fixture producer role");}
    {auto f=std::make_unique<Fixture>();f->entry.idle_policy_boundary=false;fallback(*f,"ongoing rival macro not silently restarted");}
    {auto f=std::make_unique<Fixture>();f->entry.cursor=2;fallback(*f,"out of list cursor rejected");}
    {auto f=std::make_unique<Fixture>();f->isolation.enemy_idle_uninterrupted=false;fallback(*f,"rival sole-producer premise mandatory");}
    {auto f=std::make_unique<Fixture>();f->isolation.own_idle_uninterrupted=false;fallback(*f,"own sole-producer premise mandatory");}
    {auto f=std::make_unique<Fixture>();f->actors.samples[0].provenance.observed_round=-1;fallback(*f,"stale rival bag/body never exact");}
    {auto f=std::make_unique<Fixture>();f->entry.next_action_round=1;fallback(*f,"entry schedule must match actual completed-turn observation");}
    {auto f=std::make_unique<Fixture>();f->entry.provenance.source=Source::own_state;fallback(*f,"no hidden rival cursor API premise");}
    {auto f=std::make_unique<Fixture>();f->profile.enemy_travel=f->info->travel;f->model->configure(f->profile);fallback(*f,"own navigation cannot masquerade as enemy perspective");}
    {auto f=std::make_unique<Fixture>();f->world.unknown_enemy_x=8;fallback(*f,"unknown enemy spawn distances do not reuse own navigation");}
    {auto f=std::make_unique<Fixture>();f->profile.order_published=false;f->model->configure(f->profile);fallback(*f,"unknown future order not inferred from enumeration");}
    {auto f=std::make_unique<Fixture>();f->profile.fixed_rank_certificate=false;f->model->configure(f->profile);fallback(*f,"enumeration cannot certify draft or priority rank");}
    {auto f=std::make_unique<Fixture>();f->world.flip_order=false;fallback(*f,"published equal-rank law must obey alternating team start");}
    {auto f=std::make_unique<Fixture>();f->profile.order_last_round=15;f->model->configure(f->profile);fallback(*f,"published order must cover full horizon");}
    {auto f=std::make_unique<Fixture>();f->world.own_first=false;fallback(*f,"published current order cross-checks actual completed turns");}
    {auto f=std::make_unique<Fixture>();f->actors.samples[0].next_priority=1;fallback(*f,"unmodeled priority change fails closed");}
}
void source_and_stock(){
    {auto f=std::make_unique<Fixture>();f->layout.clock.round=1;fallback(*f,"stale composition not joined to fresh snapshot");}
    {auto f=std::make_unique<Fixture>();f->spatial.clock.own.id=8;fallback(*f,"spatial owner identity bound");}
    {auto f=std::make_unique<Fixture>();f->spatial.current_complete=false;fallback(*f,"unknown current occupancy not safe route evidence");}
    {auto f=std::make_unique<Fixture>();f->info->public_layout_truncated=true;fallback(*f,"omitted public layout cannot freeze priority");}
    {auto f=std::make_unique<Fixture>();f->info->groups[0].gems=1000;fallback(*f,"public reward drift invalidates profile geometry");}
    {auto f=std::make_unique<Fixture>();f->entry.stocks[0].lifecycle=Lifecycle::living;fallback(*f,"generation receipt must match current lifecycle");}
    {auto f=std::make_unique<Fixture>();f->entry.stocks[0].epoch=-1;fallback(*f,"unknown lifecycle generation remains unsupported");}
    {auto f=std::make_unique<Fixture>();f->info->groups[0].contested=true;fallback(*f,"visible contest cannot produce solo reward");}
    {auto f=std::make_unique<Fixture>();f->info->groups[0].all_spawns_visible=false;fallback(*f,"partial source occupancy not certified sole capture");}
    {auto f=std::make_unique<Fixture>();f->info->groups[0].visible=false;fallback(*f,"unknown first own source never assured ready");}
    {auto f=std::make_unique<Fixture>();f->info->groups[1].visible=false;f->run();require(f->model->used_commons()&&f->out->forecasts[0].stock_unknown,"unknown nonchosen source stays tagged and skipped");}
    {auto f=std::make_unique<Fixture>();auto&g=f->info->groups[0];g.lifecycle=Lifecycle::living;g.health=100;g.clear_rounds=2;g.reward_available=false;f->visible[0].health=100;
        f->entry.stocks[0].lifecycle=g.lifecycle;f->run();require(f->model->used_commons(),"fresh living stock does not require cleared reward flag");
        f->visible[0].health=99;fallback(*f,"living member HP must equal aggregate snapshot");}
    {auto f=std::make_unique<Fixture>();f->members[0].member_id=-1;fallback(*f,"unknown public member identity cannot support quotes");}
}
void quotes_and_whole_batch(){
    {auto f=std::make_unique<Fixture>();f->singles();f->world.fail_group=1;fallback(*f,"later candidate refusal discards all refined earlier prefixes");}
    {auto f=std::make_unique<Fixture>();f->world.quote_safe=false;fallback(*f,"unsupported safety or occupancy quote cannot rank jobs");}
    {auto f=std::make_unique<Fixture>();f->world.wrong_actor=true;fallback(*f,"legal quote actor identity required");}
    {auto f=std::make_unique<Fixture>();f->world.geometry=123;fallback(*f,"quote geometry bound independently of callback success");}
    {auto f=std::make_unique<Fixture>();f->world.deny_at_quote=1;f->run();require(!f->model->used_commons()&&f->out->budget_exhausted&&f->out->evaluated_count==0&&f->out->chosen==-1,"budget loss inside quote publishes no partial comparison");
        require(f->model->unsupported()==cb::Unsupported::budget,"budget denial classified distinctly");}
    {auto f=std::make_unique<Fixture>();f->run();require(f->model->used_commons(),"reused output initially refined");f->world.allowed=false;f->run();
        require(!f->model->used_commons()&&f->out->evaluated_count==0&&f->out->chosen==-1&&f->out->budget_exhausted,"exhausted reused output cannot select stale tail");}
}
void isolation_and_receipts(){auto f=std::make_unique<Fixture>();f->observe();require(!f->model->isolation_idle(f->actors.clock),"certified configured producer admitted");
    auto c=f->actors.clock;c.round=1;require(f->model->isolation_idle(c),"configured unknown roster frame idles every actor");
    f->actors.allied_count=2;f->actors.allied_roster[1]=11;f->profile.allied_count=2;f->profile.allied_roster=f->actors.allied_roster;f->reseal();
    f->actors.clock.own.id=11;f->observe();require(f->model->isolation_idle(f->actors.clock),"same declared nonproducer isolated in both arms");
    f->actors.clock.own.id=7;f->run();require(f->model->used_commons(),"public full roster includes fixed idle teammate");
    Receipts r;const auto job=f->out->menu.jobs[f->out->chosen];f->model->prediction_receipts(*f->info,job,sink,&r);require(r.count>0&&r.count<=8,"chosen physical economic receipt prefix bounded");
    unsigned own=0,enemy=0;for(int n=0;n<r.count;++n){const auto&e=r.events[n];require(e.actor_id==7||e.actor_id==9,"colored receipts preserve actual actor IDs");
        if(e.kind==int(cm::Kind::acquire)){require(e.group_id==101||e.group_id==102,"group receipts use physical IDs");(e.actor_id==7?own:enemy)+=unsigned(e.amount_milli/1000);}
        if(e.kind==int(cm::Kind::convert)){auto&bag=e.actor_id==7?own:enemy;require(bag>=unsigned(e.amount_milli/1000),"no conversion before corresponding inventory");bag-=unsigned(e.amount_milli/1000);require(e.phase==int(Phase::owner_start),"conversion receipts retain channel completion phase");}}
    Receipts capped;capped.limit=1;f->model->prediction_receipts(*f->info,job,sink,&capped);require(capped.count==1,"caller sink caps trace without recomputation");
    ++f->info->own.bag;Receipts stale;f->model->prediction_receipts(*f->info,job,sink,&stale);require(stale.count==0,"changed actual bag invalidates cached receipt source");
    --f->info->own.bag;auto wrong=job;wrong.kind=JobKind::hold;f->model->prediction_receipts(*f->info,wrong,sink,&stale);require(stale.count==0,"unchosen key cannot read another prefix");
    f->model->observe_entry(f->entry);f->model->prediction_receipts(*f->info,job,sink,&stale);require(stale.count==0,"new legal observations invalidate prior choice receipts");}
}
int main(){positive_and_control();full_menu();choice_and_reflection();missing_contracts();source_and_stock();quotes_and_whole_batch();isolation_and_receipts();std::printf("plan4 commons runtime: %d checks passed\n",checks);}
