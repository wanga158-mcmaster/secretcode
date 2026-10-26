#include "../repairman/src/ranking.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
using namespace plan4;
namespace rt=plan4::runtime;
namespace r=plan4::repairman;
namespace p=r::runtime;
namespace {
int checks=0;
void check(bool okay,const char*why){++checks;if(!okay){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
bool walkable(Point point,const void*){return point.x>=0&&point.x<16&&point.y>=0&&point.y<2;}
int travel(Point a,Point b,const void*){return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
struct Fixture {
    std::unique_ptr<Information>info=std::make_unique<Information>();
    std::unique_ptr<rt::Repairman>model=std::make_unique<rt::Repairman>();
    std::unique_ptr<Evaluation>out=std::make_unique<Evaluation>();
    std::unique_ptr<p::PreparedAttack>prepared=std::make_unique<p::PreparedAttack>();
    std::unique_ptr<p::Path>path=std::make_unique<p::Path>();
    rt::ActorFrame actors{};rt::CombatKit kit{};rt::SpatialFrame spatial{};p::AttackFrame frame{};p::CyclePlan cycle{};Menu menu{};
    std::array<rt::PublicGroup,1>groups{};std::array<rt::PublicMember,2>layout{};std::array<rt::VisibleMember,2>visible{};
    std::array<std::uint8_t,4>sight{},occupied{};
    Fixture(){
        info->round=10;info->settings.max_rounds=75;info->own.id=4;info->own.position={1,0};info->own_spawn={9,0};
        info->own.health=500;info->own.maximum_health=1000;info->own.recall_safe=true;info->travel={travel,nullptr};
        actors.clock=kit.clock=frame.clock={10,{4}};actors.own_observed=true;
        actors.own.id=4;actors.own.champion=11;actors.own.alive=actors.own.ally=true;actors.own.position=info->own.position;
        actors.own.health=500;actors.own.maximum_health=1000;actors.own.attack_range=3;actors.own.strength=10;
        actors.own.provenance={10,Source::own_state,Status::exact_now};
        kit.exact=true;kit.version=5;kit.stat_epoch=7;kit.champion=11;kit.strength=10;kit.attack_range=3;
        frame.group_id=20;frame.target_member_id=100;frame.lifecycle_version=3;frame.lifecycle_version_supported=true;
        frame.can_attack_now=frame.ordinary_basic_only=true;frame.basic_damage=10;frame.incoming_upper_bound=20;
        frame.kit_version=5;frame.stat_epoch=7;frame.action_quote=frame.combat_quote={10,Source::own_state,Status::derived};
        groups[0]={20,0,2,true};layout[0]={20,100,0,2,0,3,{0,0},200};layout[1]={20,101,1,2,0,3,{0,1},100};
        visible[0]={20,100,0,2,{0,0},100,{10,Source::personal_vision,Status::exact_now}};
        visible[1]={20,101,1,2,{0,1},0,{10,Source::personal_vision,Status::exact_now}};
        frame.layout={{10,{4}},groups.data(),1,layout.data(),2,false};frame.members={visible.data(),2,false};
        info->group_count=2;auto&g=info->groups[0];g.id=20;g.position={0,0};g.health=100;g.full_health=300;
        g.clear_rounds=10;g.full_clear_rounds=30;g.gems=20;g.gold=5;g.respawn_rounds=1000;
        g.incoming_damage=g.full_incoming_damage=20;g.lifecycle=Lifecycle::living;g.visible=g.all_spawns_visible=g.occupancy_known=true;
        g.provenance=g.health_provenance={10,Source::personal_vision,Status::exact_now};
        auto&h=info->groups[1];h.id=21;h.position={6,1};h.full_health=50;h.gems=10;h.gold=2;h.respawn_rounds=1000;
        h.clear_rounds=0;h.full_clear_rounds=5;h.lifecycle=Lifecycle::cleared;h.visible=h.reward_available=h.all_spawns_visible=h.occupancy_known=true;
        h.provenance=h.health_provenance=g.provenance;
        info->altar_count=1;info->altars[0].id=30;info->altars[0].position={1,1};info->altars[0].altar_position={1,2};info->altars[0].lock_known=true;
        sight.fill(255);occupied[0]=1;
        auto&s=frame.safety;s.observed_round=10;s.actor_id=4;s.topology_complete=true;s.walkable=walkable;
        s.occupancy={16,2,sight.data(),occupied.data(),sight.size(),true};s.relevant_threats_complete=true;s.threat_count=1;
        s.threats[0]={100,3,{0,0},20,{10,Source::personal_vision,Status::exact_now},true,true};
        s.enemies={{10,Source::personal_vision,Status::exact_now},true,true};s.own_spawn=info->own_spawn;s.physical_spawn_known=s.recall_arrival_supported=true;
        s.spawn_quote={10,Source::own_state,Status::derived};
        cycle.clock=actors.clock;cycle.group_id=20;cycle.target_member_id=100;cycle.lifecycle_version=3;cycle.kit_version=5;cycle.stat_epoch=7;
        cycle.quote={10,Source::own_state,Status::derived};cycle.supported=cycle.solo_capture_supported=cycle.capture_hold_zero_exposure=cycle.executor_enabled=true;
        cycle.returning.supported=true;cycle.returning.count=8;
        for(int n=0;n<8;++n){const int x=8-n;cycle.returning.steps[n]={{x,0},x<=3?20.0f:0,true};}
        cycle.capture_entry.supported=true;cycle.capture_entry.count=1;cycle.capture_entry.steps[0]={{0,0},0,true};
        menu.count=menu.single_count=1;menu.jobs[0]={JobKind::farm,0,-1,0};
        spatial.clock=actors.clock;spatial.information=info.get();spatial.menu=&menu;spatial.graph_quota=64;
    }
    void prepare(){check(p::prepare_attack_into(*info,actors,kit,frame,*prepared),"current preparation supports one exact attackable remaining member");}
    void run(){model->observe_actors(actors);model->observe_combat(kit);model->observe_spatial(spatial);model->observe_snapshot(*info,frame.members);model->observe_attack(frame);model->observe_cycle(cycle);
        rt::evaluate_model(*info,menu,*model,*out);}
    void fullmenu(){menu.single_count=2;menu.count=6;menu.jobs[0]={JobKind::farm,0,-1,0};menu.jobs[1]={JobKind::farm,1,-1,0};menu.jobs[2]={JobKind::farm,0,1,0};
        menu.jobs[3]={JobKind::bank,-1,-1,0};menu.jobs[4]={JobKind::renew};menu.jobs[5]={JobKind::hold};}
};
int events(const Forecast&f,EventKind kind){int count=0;for(int n=0;n<f.event_count;++n)count+=f.events[n].kind==kind;return count;}
void phase_tape(){auto f=std::make_unique<Fixture>();f->prepare();
    check(p::prefix_into(*f->info,f->menu,f->menu.jobs[0],*f->prepared,f->cycle,true,*f->path),"active two-excursion physical prefix completes");
    check(f->path->captured&&f->path->excursions==2&&f->path->returned&&f->path->requires_reobservation,"retained work schedules exactly one conditional return");
    check(f->path->attacks==10&&f->path->remaining==0&&f->path->prefix.our_secured==0&&f->path->prefix.final_bag==20,"full work and capture still provide no pre-bank score");
    check(f->path->checkpoint.position==f->info->groups[0].position&&f->path->checkpoint.groups[0].lifecycle==Lifecycle::captured,"captured physical checkpoint carries only real stock transition");
    check(f->path->checkpoint.groups[1].lifecycle==Lifecycle::cleared&&f->path->checkpoint.groups[1].ready_round==-1,"unrelated source stock unchanged");
    bool restored=false,returned=false;for(int n=0;n<f->path->count;++n){const auto&e=f->path->tape[n];
        check(e.health>0&&e.health<=1000&&e.end>=e.start,"every physical phase has bounded positive health and elapsed time");
        if(e.kind==p::TapeKind::heal){restored=e.health==1000;check(e.position==f->info->own_spawn&&e.end==e.start+1,"every healing owner turn follows actual recall to physical spawn");}
        if(e.kind==p::TapeKind::return_move){returned=true;check(restored,"return movement cannot precede completed first restoration");}}
    check(returned&&events(f->path->prefix,EventKind::recall_arrival)==1&&events(f->path->prefix,EventKind::capture)==1,"one recall and one capture prefix, no duplicated acquisition");
    auto a=std::make_unique<Forecast>(f->path->prefix);
    check(p::prefix_into(*f->info,f->menu,f->menu.jobs[0],*f->prepared,f->cycle,false,*f->path),"disabled same first excursion prefix supported");
    check(f->path->restored&&!f->path->returned&&!f->path->captured&&f->path->excursions==1&&f->path->remaining==40,"control ends at same first restored spawn and retains no private return schedule");
    check(f->path->prefix.final_bag==0&&f->path->prefix.our_secured==0&&f->path->checkpoint.position==f->info->own_spawn,"unfinished partial work yields no gem or score");
    int first_heal=-1;for(int n=0;n<a->event_count;++n){if(a->events[n].kind==EventKind::heal)first_heal=n;else if(first_heal>=0)break;}
    check(first_heal>=0&&f->path->prefix.event_count==first_heal+1,"both arms have identical first restoration event prefix length");
    for(int n=0;n<f->path->prefix.event_count;++n){const auto&x=a->events[n];const auto&y=f->path->prefix.events[n];
        check(x.kind==y.kind&&x.start.round==y.start.round&&x.end.round==y.end.round&&x.health_after==y.health_after,"same attack/escape/recall/heal timing and loss before named coupling");}
}
void full_path(){auto f=std::make_unique<Fixture>();f->run();check(f->model->used_repairman(),"conditional complete runtime binds");
    const auto active=f->out->forecasts[0].first_conversion_round;check(f->out->forecasts[0].our_secured==20&&f->out->forecasts[0].final_bag==0,"active first FARM banks exactly captured reward");
    check(f->model->path().macro.macros_started==1,"same first FARM macro includes both excursions");
    check(!f->model->return_attack_authorized(50),"prediction cannot install a hypothetical future observation");
    f->model=std::make_unique<rt::Repairman>(false);f->run();check(f->model->used_repairman()&&f->out->forecasts[0].macros_started>=2,"disabled restored endpoint enters ordinary unchanged continuation");
    check(f->out->forecasts[0].our_secured==20&&f->out->forecasts[0].first_conversion_round>active,"control next excursion uses original full-clear snapshot independence approximation");
    f->fullmenu();f->model=std::make_unique<rt::Repairman>();f->run();check(f->model->used_repairman()&&f->out->evaluated_count==6,"same full fixed menu includes singles pair bank renew hold");
    check(events(f->out->forecasts[2],EventKind::capture)>=2&&f->out->forecasts[2].our_secured==30,"ordered pair completes second ordinary camp then one whole-bag conversion");
    check(f->model->path().macro.macros_started==1&&events(f->out->forecasts[2],EventKind::bank)==1,"pair is not secretly counted as a second own macro");
    int held_target_captures=0;for(int n=0;n<f->out->forecasts[5].event_count;++n)held_target_captures+=f->out->forecasts[5].events[n].kind==EventKind::capture&&f->out->forecasts[5].events[n].group_id==20;
    check(!held_target_captures&&f->out->forecasts[5].our_secured<=10,"HOLD cannot evade shared strict full-clear admission and cheaply claim the unsafe target");
    for(int n=0;n<6;++n)check(f->out->forecasts[n].macros_started<=8,"unchanged common continuation cap applies to all jobs");
    check(f->model->model_evidence().reported&&f->model->model_evidence().supported&&f->model->model_evidence().paths==6,"model evidence reports actual complete batch");
}
void refusals(){
    {auto f=std::make_unique<Fixture>();f->cycle.executor_enabled=false;f->run();check(!f->model->used_repairman(),"missing actual common safety executor gates entire coupling");}
    {auto f=std::make_unique<Fixture>();f->spatial.graph_quota=1;f->run();check(!f->model->used_repairman()&&f->out->budget_exhausted&&f->model->spatial_vertices()<=1,"planning admission shares actual remaining optional graph quota");}
    {auto f=std::make_unique<Fixture>();f->cycle.returning.steps[0].supported=false;f->run();check(!f->model->used_repairman(),"unknown return exposure is not zero");}
    {auto f=std::make_unique<Fixture>();f->cycle.capture_hold_zero_exposure=false;f->run();check(!f->model->used_repairman(),"unquoted capture exposure not ignored");}
    {auto f=std::make_unique<Fixture>();f->cycle.lifecycle_version=4;f->run();check(!f->model->used_repairman(),"new lifecycle cannot inherit previous work stock");}
    {auto f=std::make_unique<Fixture>();f->cycle.kit_version=6;f->run();check(!f->model->used_repairman(),"stat quote identity changes invalidate planned work");}
    {auto f=std::make_unique<Fixture>();f->frame.new_visible_rival=true;f->run();check(!f->model->used_repairman(),"rival invalidates retained scheduling before any productive phase");}
    {auto f=std::make_unique<Fixture>();f->frame.emergency=true;f->run();check(!f->model->used_repairman(),"common emergency precedes work retention");}
    {auto f=std::make_unique<Fixture>();f->info->settings.max_rounds=20;f->run();check(!f->model->used_repairman()&&f->out->forecasts[0].our_secured==0&&f->out->forecasts[0].final_bag==0,"game end before a complete initial safe escape fails closed without partial reward");}
    {auto f=std::make_unique<Fixture>();f->info->settings.max_rounds=2000;f->cycle.returning.count=120;
        for(int n=0;n<112;++n)f->cycle.returning.steps[n]={{n%2?9:8,0},0,true};
        for(int n=0;n<8;++n)f->cycle.returning.steps[112+n]={{8-n,0},n>=5?20.0f:0,true};
        f->run();check(f->model->used_repairman()&&f->out->forecasts[0].truncated&&f->out->forecasts[0].our_secured==0&&f->out->forecasts[0].final_bag==0,"H128 cutoff during a supported return cannot receive later acquisition or conversion");}
    {auto f=std::make_unique<Fixture>();f->info->settings.max_rounds=2000;f->cycle.returning.count=107;
        for(int n=0;n<99;++n)f->cycle.returning.steps[n]={{n%3==0?8:9,n%3==1?1:0},0,true};
        for(int n=0;n<8;++n)f->cycle.returning.steps[99+n]={{8-n,0},n>=5?20.0f:0,true};
        f->run();check(f->model->used_repairman()&&f->model->path().remaining==20&&f->model->path().attacks==8&&f->out->forecasts[0].final_health==900,
            "four-attack quote at cutoff executes only the two owner actions before H, with exact work and HP prefix");}
    {auto f=std::make_unique<Fixture>();f->info->settings.max_rounds=2000;f->info->own.health=f->actors.own.health=700;
        f->info->groups[0].incoming_damage=f->info->groups[0].full_incoming_damage=f->frame.incoming_upper_bound=f->frame.safety.threats[0].incoming_upper_bound=100;
        f->cycle.returning.count=100;for(int n=0;n<92;++n)f->cycle.returning.steps[n]={{n%2?9:8,0},0,true};
        for(int n=0;n<8;++n)f->cycle.returning.steps[92+n]={{8-n,0},n>=5?100.0f:0,true};
        f->run();check(f->model->used_repairman()&&f->out->forecasts[0].final_health==600&&f->model->path().remaining==60&&f->out->forecasts[0].our_secured==0,
            "cutoff inside second restoration receives only the two completed spawn-heal turn ends");}
    {auto f=std::make_unique<Fixture>();f->run();safe_segment::ExecutionBody body;body.round=10;body.actor_id=4;body.group_id=20;body.target_id=100;body.position={1,0};
        body.health=500;body.maximum_health=1000;body.kit_version=5;body.stat_epoch=7;body.lifecycle_version=3;body.alive=body.can_attack=true;
        check(!f->model->work_record().valid,"completed hypothetical forecast never mutates actual execution work");
        check(f->model->segment_proposal(body).actor!=nullptr,"actual source proposal initializes work only after complete fresh member join");
        f->model->attack_processed(4,10);f->model->work_departed();check(!f->model->return_attack_authorized(20)&&f->model->work_record().predicted_remaining==60,"actual attack receipt leaves absent work explicitly predicted");
        auto observed=f->model->work_record();r::GroupObservation group{20,20,3,Lifecycle::living,55,{20,Source::personal_vision,Status::exact_now},true,true,false,false,true};
        check(f->model->reobserve_work(group,20)&&f->model->return_attack_authorized(20)&&f->model->work_record().return_prediction_error==-5,"only fresh current lifecycle observation authorizes return");
        (void)observed;group.lifecycle_version=4;group.round=group.provenance.observed_round=21;
        check(!f->model->reobserve_work(group,21)&&!f->model->return_attack_authorized(21)&&!f->model->work_record().valid,"later generation invalidates execution record");}
}
void executor_proposals(){auto f=std::make_unique<Fixture>();check(!f->model->safe_segments_enabled(),"unbound ordinary main does not enable a fictitious safe executor provider");f->run();
    safe_segment::ExecutionBody body;body.round=10;body.actor_id=4;body.group_id=20;body.target_id=100;body.position=f->info->own.position;
    body.health=500;body.maximum_health=1000;body.kit_version=5;body.stat_epoch=7;body.lifecycle_version=3;body.alive=body.can_attack=body.can_move=true;
    auto proposal=f->model->segment_proposal(body);check(f->model->safe_segments_enabled()&&proposal.actor&&proposal.scene&&proposal.settings&&proposal.lifecycle_supported,"conditional actual-body proposal borrows current legal scene with epochs");
    safe_segment::SegmentExecutor executor;check(executor.next(proposal,body,64).kind==safe_segment::ExecutionKind::attack,"common executor independently recomputes the same ordinary attack admission");
    f->model->segment_attack_completed(body,10);f->model->segment_departed(body);
    check(f->model->work_record().predicted_remaining==90&&f->model->work_record().absent,"acknowledged actual one-attack/departure hooks update predicted work once");
    f->model->segment_attack_completed(body,10);f->model->segment_proposal(body);
    check(f->model->work_record().predicted_remaining==90&&f->model->work_record().absent,"duplicate attack ack or repeated stale same-turn observation cannot duplicate work or undo departure");
    body.health=499;check(!f->model->segment_proposal(body).actor,"cached planning body cannot authorize a newly damaged actual body");body.health=500;
    body.kit_version=6;check(!f->model->segment_proposal(body).actor,"kit mutation requires new legal observation before execution");body.kit_version=5;
    body.round=11;check(!f->model->segment_proposal(body).actor,"new round cannot reuse stale target or safety frame");body.round=10;
    body.lifecycle_version=0;check(f->model->segment_proposal(body).lifecycle_version==3,"legal history epoch is explicitly handed off without querying a nonexistent SDK generation counter");body.lifecycle_version=3;
    f->model=std::make_unique<rt::Repairman>(false);f->run();proposal=f->model->segment_proposal(body);
    safe_segment::SegmentExecutor control;check(proposal.actor&&control.next(proposal,body,64).kind==safe_segment::ExecutionKind::attack,"disabled uses identical common admission instead of weaker safety");
}
}
int main(){phase_tape();full_path();refusals();executor_proposals();std::printf("plan4 repairman ranking: %d checks passed\n",checks);}
