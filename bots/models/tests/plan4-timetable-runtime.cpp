#include "../timetable/src/runtime.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
using namespace plan4;
using namespace plan4::runtime;
namespace {
int checks=0;
void check(bool value,const char* message){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
struct Map {
    Point blocked{-1,-1};bool route_known=true,distance_known=true,malformed=false;
    mutable int calls=0,max_capacity=0;
};
bool walk(Point p,const void* context){const auto& map=*static_cast<const Map*>(context);return p.x>=0&&p.y>=0&&p.x<40&&p.y<40&&p!=map.blocked;}
int travel(Point a,Point b,const void* context){return static_cast<const Map*>(context)->distance_known?std::max(std::abs(a.x-b.x),std::abs(a.y-b.y)):-1;}
int prefix(Point a,Point b,Point* out,int capacity,const void* context){const auto& map=*static_cast<const Map*>(context);++map.calls;map.max_capacity=std::max(map.max_capacity,capacity);
    if(!map.route_known)return-1;
    int count=0;
    while(a!=b&&count<capacity){a.x+=(b.x>a.x)-(b.x<a.x);a.y+=(b.y>a.y)-(b.y<a.y);out[count++]=a;}
    if(map.malformed&&count)out[0]=b;
    return count;
}
struct Fixture {
    std::unique_ptr<Information> info=std::make_unique<Information>();
    ActorFrame actors{};Map map;Menu menu{};
    Fixture(){
        info->own.id=90;info->own.position={0,0};info->own.health=info->own.maximum_health=1000;info->own.bag=10;
        info->travel={travel,&map};info->own.recall_safe=true;
        info->altar_count=1;info->altars[0].id=0;info->altars[0].position={2,2};info->altars[0].lock_known=true;
        info->group_count=1;auto& group=info->groups[0];group.id=10;group.position={2,2};group.lifecycle=Lifecycle::cleared;
        group.visible=group.all_spawns_visible=group.reward_available=group.occupancy_known=true;group.clear_rounds=group.full_clear_rounds=0;
        group.gems=20;group.gold=5;group.respawn_rounds=200;
        actors.clock.own.id=90;actors.allied_count=actors.enemy_count=3;actors.allied_roster={90,91,92};actors.enemy_roster={1,2,3};
        actors.roster_complete=actors.own_observed=actors.draft_order_known=true;actors.own.id=90;actors.own.ally=actors.own.alive=true;
        actors.own.health=actors.own.maximum_health=1000;actors.own.draft_ordinal=0;actors.own.position={0,0};actors.count=5;
        for(int n=0;n<5;++n){auto& actor=actors.samples[n];actor=actors.own;actor.id=n<2?91+n:n-1;actor.ally=n<2;
            actor.draft_ordinal=n<2?n+1:n-2;actor.position={30+n,30};}
        // Current enemies start, then own rank0. At next-round own rank1,
        // enemy rank0 will act before us, yielding one predicted rival move.
        actors.samples[2].completed_turn=true;actors.samples[2].position={1,2};
        actors.own.next_priority=-1;actors.samples[1].next_priority=-2;
        menu.count=menu.single_count=1;menu.jobs[0]={JobKind::bank,-1,-1,0};
    }
    void round(int r){info->round=r;info->own.position=actors.own.position;actors.clock.round=r;
        actors.own.provenance={r,Source::own_state,Status::exact_now};
        for(int n=0;n<actors.count;++n)actors.samples[n].provenance={r,Source::personal_vision,Status::exact_now};
        info->groups[0].provenance=info->groups[0].health_provenance={r,Source::personal_vision,Status::exact_now};}
    SpatialFrame spatial(bool planning=true){SpatialFrame out;out.clock=actors.clock;out.topology={40,40,true,walk,&map};out.travel={travel,&map};
        out.route_prefix=prefix;out.route_context=&map;if(planning){out.information=info.get();out.menu=&menu;}return out;}
    void feed(Timetable& runtime,int r,bool planning=true){round(r);runtime.observe_actors(actors);if(planning)runtime.observe_spatial(spatial());else runtime.observe_movement(spatial(false));}
    void history(Timetable& runtime){actors.samples[2].position={0,2};feed(runtime,9);actors.samples[2].position={1,2};feed(runtime,10);}
};
void order_inference(){
    auto f=std::make_unique<Fixture>();f->round(10);std::array<timetable::ActorSample,9> samples;int count=0;
    auto order=timetable_order(f->actors,samples,count);
    check(order.current_observations&&order.full_order&&!order.allied_started_current&&order.next_priorities_assumed,"all fresh bodies support current team-start inference and explicit next-priority assumption");
    check(count==5&&samples[2].order.known&&samples[2].order.already_acted_current_round&&samples[2].order.before_own_next_round,"enemy rank0 flags preserve one intervening turn before changed own next rank");
    check(!samples[3].order.before_own_next_round,"enemy equal next rank acts after own when allies start next");
    f->actors.samples[2].completed_turn=false;order=timetable_order(f->actors,samples,count);
    check(order.full_order&&order.allied_started_current&&samples[3].order.before_own_next_round,"equal rank opponent next acts first after public starting-team flip");
    f->actors.samples[4].completed_turn=true;order=timetable_order(f->actors,samples,count);
    check(order.current_observations&&!order.full_order&&!samples[0].order.known,"impossible completed-turn suffix cannot grant trusted order");
    f->actors.samples[4].completed_turn=false;f->actors.count=4;order=timetable_order(f->actors,samples,count);
    check(order.current_observations&&!order.full_order,"missing actual roster body leaves dynamic order unknown");
    f->actors.count=5;f->actors.enemy_count=2;f->actors.count=4;order=timetable_order(f->actors,samples,count);
    check(order.current_observations&&!order.full_order,"unequal roster sizes never use equal-rank team-start inference");
    f=std::make_unique<Fixture>();f->round(10);f->actors.samples[0].draft_ordinal=0;
    order=timetable_order(f->actors,samples,count);check(!order.full_order,"duplicate team draft ordinal rejects order");
    f=std::make_unique<Fixture>();f->round(10);f->actors.samples[2].current_priority=-1;
    order=timetable_order(f->actors,samples,count);check(!order.full_order,"priority sorting checks actual completed pattern instead of draft index");
}
void uncertified_draft_order(){
    auto f=std::make_unique<Fixture>();f->actors.draft_order_known=false;f->actors.own.draft_ordinal=-1;
    for(auto& actor:f->actors.samples)actor.draft_ordinal=-1;
    f->actors.samples[2].next_priority=3;f->actors.samples[3].next_priority=2;f->actors.samples[4].next_priority=1;
    f->round(10);std::array<timetable::ActorSample,9> samples;int count=0;
    auto order=timetable_order(f->actors,samples,count);
    check(order.full_order&&!order.allied_started_current&&samples[2].order.before_own_next_round&&!samples[3].order.before_own_next_round,"fresh completed counts plus distinct priorities infer legal order without draft ordinals");
    f->actors.samples[3].next_priority=3;order=timetable_order(f->actors,samples,count);
    check(order.current_observations&&!order.full_order&&!samples[2].order.known,"enemy next-priority tie interval straddling owner cutoff remains unknown");
    f->actors.own.next_priority=0;f->actors.samples[0].next_priority=0;f->actors.samples[1].next_priority=0;
    for(int n=2;n<5;++n)f->actors.samples[n].next_priority=0;
    order=timetable_order(f->actors,samples,count);
    check(order.full_order&&!samples[2].order.before_own_next_round,"unchanged own-team ties retain witnessed order and whole enemy interval lies after next cutoff");
    f->actors.samples[0].current_priority=1;f->actors.samples[0].completed_turn=true;order=timetable_order(f->actors,samples,count);
    check(!order.full_order,"newly formed own-relative priority tie cannot borrow unknown draft position");
    f=std::make_unique<Fixture>();f->actors.draft_order_known=false;f->round(10);f->actors.samples[3].completed_turn=true;f->actors.samples[4].completed_turn=true;
    order=timetable_order(f->actors,samples,count);check(!order.full_order,"completed-team count difference greater than one rejects starting-team inference");
    f=std::make_unique<Fixture>();f->actors.draft_order_known=false;f->actors.own.draft_ordinal=-1;
    for(auto& actor:f->actors.samples)actor.draft_ordinal=-1;
    f->actors.samples[2].next_priority=3;f->actors.samples[3].next_priority=2;f->actors.samples[4].next_priority=1;
    Timetable runtime;f->history(runtime);auto forecast=std::make_unique<Forecast>();runtime.predict_into(*f->info,f->menu.jobs[0],f->menu,*forecast);
    check(runtime.order_evidence().full_order&&forecast->events[0].end.round==13,"live-shaped uncertified roster still supports predictor when interval proof resolves every order relation");
}
void uncertified_order_oracle(){
    auto f=std::make_unique<Fixture>();f->actors.draft_order_known=false;int supported=0,ambiguous=0;
    for(int enemy_starts=0;enemy_starts<2;++enemy_starts)for(int own_rank=0;own_rank<3;++own_rank)for(int code=0;code<64;++code){
        f->actors.own.draft_ordinal=own_rank;f->actors.own.current_priority=0;f->actors.own.next_priority=(code&1)?1:0;
        for(int n=0;n<5;++n){auto& actor=f->actors.samples[n];actor.current_priority=0;actor.next_priority=(code&(1<<(n+1)))?1:0;
            actor.draft_ordinal=n<2?(n+1==own_rank?0:n+1):n-2;
            actor.completed_turn=actor.ally?actor.draft_ordinal<own_rank:actor.draft_ordinal<own_rank+enemy_starts;}
        f->round(10);std::array<timetable::ActorSample,9> samples;int count=0;const auto order=timetable_order(f->actors,samples,count);
        check(order.current_observations&&order.allied_started_current==!enemy_starts,"unknown-draft completed-count inference agrees with explicit legal alternating schedule");
        if(!order.full_order){++ambiguous;continue;}++supported;
        int own_next_rank=0;
        for(int n=0;n<2;++n){const auto& actor=f->actors.samples[n];own_next_rank+=actor.next_priority>f->actors.own.next_priority
            ||(actor.next_priority==f->actors.own.next_priority&&actor.draft_ordinal<f->actors.own.draft_ordinal);}
        for(int n=0;n<count;++n){const auto& actor=f->actors.samples[n];bool before=false;
            if(actor.ally)before=actor.next_priority>f->actors.own.next_priority||(actor.next_priority==f->actors.own.next_priority&&actor.draft_ordinal<f->actors.own.draft_ordinal);
            else {int rank=0;for(int p=2;p<5;++p){const auto& other=f->actors.samples[p];rank+=other.next_priority>actor.next_priority
                    ||(other.next_priority==actor.next_priority&&other.draft_ordinal<actor.draft_ordinal);}
                before=rank<own_next_rank||(rank==own_next_rank&&!enemy_starts);}
            check(samples[n].order.known&&samples[n].order.before_own_next_round==before&&samples[n].order.already_acted_current_round==actor.completed_turn,
                "every supported uncertified relation agrees with independent explicit-draft schedule oracle");
        }
    }
    check(supported>0&&ambiguous>0,"interval oracle exercises both supported and honestly ambiguous future schedules");
}
void runtime_forecasts(){
    auto f=std::make_unique<Fixture>();Timetable active,control(false);f->history(active);f->actors.samples[2].position={0,2};f->feed(control,9);f->actors.samples[2].position={1,2};f->feed(control,10);
    check(!active.model_evidence().supported&&active.model_evidence().reason==int(Timetable::EvidenceReason::no_batch),"fresh observations alone do not claim a completed predictive batch");
    check(active.order_evidence().full_order&&active.tracker().count()==4,"runtime retains nearest four legal bodies with trusted inferred order");
    auto a=std::make_unique<Forecast>(),b=std::make_unique<Forecast>();active.predict_into(*f->info,f->menu.jobs[0],f->menu,*a);control.predict_into(*f->info,f->menu.jobs[0],f->menu,*b);
    check(a->events[0].kind==EventKind::travel&&a->events[0].end.round==13&&b->events[0].end.round==12,"same cached route gets one active future-vertex delay and no control delay");
    check(a->our_secured==b->our_secured&&a->our_secured==10,"occupancy predictor cannot create extra bank reward");
    const auto ae=active.model_evidence(),ce=control.model_evidence();
    check(ae.reported&&ae.supported&&ae.coupling_enabled&&ae.paths==1&&!ae.unknown&&ae.work==1&&ae.revision>0,
        "current known-order first-route quote reports support and bounded cache work without claiming an effect");
    check(ce.supported&&!ce.coupling_enabled&&ce.paths==ae.paths&&ce.samples==ae.samples,
        "control reports same law support and actor coverage with coupling disabled");
    check(f->map.max_capacity<=8,"runtime never requests more than eight cached steps");
    f->menu.count=2;f->menu.jobs[1]=f->menu.jobs[0];auto evaluation=std::make_unique<Evaluation>();const int before=f->map.calls;
    evaluate_model(*f->info,f->menu,active,*evaluation);
    check(evaluation->evaluated_count==2&&evaluation->menu.count==2&&f->map.calls-before==1,"batch shares duplicate first-route quote and preserves frozen menu");
    f->map.route_known=false;active.observe_spatial(f->spatial());active.predict_into(*f->info,f->menu.jobs[0],f->menu,*a);
    check(a->events[0].end.round==12,"unknown cached route uses ordinary forecast without private search");
    check(!active.model_evidence().supported&&active.model_evidence().reason==int(Timetable::EvidenceReason::route),
        "route fallback evidence cannot retain successful support from an earlier quote");
    f->map.route_known=true;f->map.distance_known=false;active.observe_spatial(f->spatial());active.predict_into(*f->info,f->menu.jobs[0],f->menu,*a);
    check(a->travel_unknown,"unknown terrain distance is never replaced by geometric distance");
    f->map.distance_known=true;f->map.malformed=true;active.observe_spatial(f->spatial());active.predict_into(*f->info,f->menu.jobs[0],f->menu,*a);
    check(a->events[0].end.round==12,"nonadjacent route prefix cannot create conflict delay");
    f->map.malformed=false;auto stale=f->spatial();stale.clock.round=9;active.observe_spatial(stale);active.predict_into(*f->info,f->menu.jobs[0],f->menu,*a);
    check(a->events[0].end.round==12,"stale spatial route clock cannot affect current forecast");
    f->info->groups[0].position=f->info->own.position;f->info->settings.capture_rounds=0;
    Job farm{JobKind::farm,0,-1,0};active.observe_spatial(f->spatial());active.predict_into(*f->info,farm,f->menu,*a);
    bool delayed_bank_leg=false;for(int n=0;n<a->event_count;++n)if(a->events[n].kind==EventKind::travel&&a->events[n].start.round==10&&a->events[n].end.round-a->events[n].start.round>2)delayed_bank_leg=true;
    check(!delayed_bank_leg,"zero-length first travel consumes hook and never recharges occupancy on same-round later bank leg");
}
void fresh_waits_and_fallback(){
    auto f=std::make_unique<Fixture>();Timetable active;f->history(active);active.selected(*f->info,f->menu.jobs[0],77,true);active.observe_movement(f->spatial(false));
    const Point target=f->info->altars[0].position;
    check(active.wait_for_movement(f->actors.clock,{0,0},&target,1)&&active.waits_used()==1,"selected first travel may consume one fresh bounded predicted wait");
    check(active.wait_for_movement(f->actors.clock,{0,0},&target,1)&&active.waits_used()==2,"second accepted wait is recorded against same decision");
    check(!active.wait_for_movement(f->actors.clock,{0,0},&target,1)&&active.waits_used()==2,"third request returns to fresh ordinary route");
    active.selected(*f->info,f->menu.jobs[0],78,true);const Point other{3,3};
    check(!active.wait_for_movement(f->actors.clock,{0,0},&other,1)&&active.waits_used()==0,"later-leg or changed executor goals cannot reuse selected first-leg conflict");
    check(!active.wait_for_movement(f->actors.clock,{1,1},&target,1),"post-observation changed own origin rejects stale actor positions");
    auto stale_clock=f->actors.clock;stale_clock.round=11;check(!active.wait_for_movement(stale_clock,{0,0},&target,1),"old actor frame cannot wait on a later movement clock");
    active.selection_canceled();check(!active.wait_for_movement(f->actors.clock,{0,0},&target,1),"canceled selection cannot retain optional movement wait");
    active.selected(*f->info,f->menu.jobs[0],79,false);check(!active.wait_for_movement(f->actors.clock,{0,0},&target,1),"unproductive fallback never installs a wait decision");
    f->actors.samples[2].position={1,1};f->round(11);active.observe_actors(f->actors);active.selected(*f->info,f->menu.jobs[0],80,true);
    check(!active.wait_for_movement(f->actors.clock,{0,0},&target,1),"fresh actor frame cannot reuse last-round movement geometry clock");
    active.observe_movement(f->spatial(false));
    check(active.wait_for_movement(f->actors.clock,{0,0},&target,1),"fresh movement geometry and current actor evidence support a wait without new planning snapshot");
    // Active and control use identical immediate occupancy/rank/two-wait guards.
    auto g=std::make_unique<Fixture>();Timetable a,b(false);g->actors.samples[2].position={1,1};g->feed(a,9);g->feed(b,9);g->feed(a,10);g->feed(b,10);
    a.selected(*g->info,g->menu.jobs[0],1,true);b.selected(*g->info,g->menu.jobs[0],1,true);a.observe_movement(g->spatial(false));b.observe_movement(g->spatial(false));
    check(a.wait_for_movement(g->actors.clock,{0,0},&target,1)&&b.wait_for_movement(g->actors.clock,{0,0},&target,1),"current occupied first step uses same bounded wait in active and control");
    g->actors.count=4;g->feed(a,11,false);a.selected(*g->info,g->menu.jobs[0],2,true);
    check(!a.wait_for_movement(g->actors.clock,{0,0},&target,1),"missing roster body prevents actual wait even when current blockers are visible");
}
void current_only_evidence(){
    auto f=std::make_unique<Fixture>();Timetable active,control(false);f->actors.draft_order_known=false;
    f->history(active);f->actors.samples[2].position={0,2};f->feed(control,9);f->actors.samples[2].position={1,2};f->feed(control,10);
    auto a=std::make_unique<Forecast>(),b=std::make_unique<Forecast>();active.predict_into(*f->info,f->menu.jobs[0],f->menu,*a);control.predict_into(*f->info,f->menu.jobs[0],f->menu,*b);
    const auto ae=active.model_evidence(),ce=control.model_evidence();
    check(!active.order_evidence().full_order&&ae.supported&&ce.supported&&ae.unknown==4&&ce.unknown==4
        &&ae.reason==int(Timetable::EvidenceReason::current_only)&&ce.reason==int(Timetable::EvidenceReason::current_only),
        "valid current-occupancy law remains supported while unknown-order future motion is explicitly distinguished in both arms");
    check(a->events[0].end.round==b->events[0].end.round,"current-only support does not claim an active prediction or action difference");
}
void invalidation_and_permutation(){
    auto f=std::make_unique<Fixture>();Timetable runtime;f->history(runtime);
    f->feed(runtime,11);f->actors.samples[2].provenance.source=Source::model;runtime.observe_actors(f->actors);
    check(runtime.tracker().count()==0&&!runtime.order_evidence().current_observations,"duplicate same-turn actor observation invalidates history");
    f->round(12);f->actors.samples[0].provenance.observed_round=11;runtime.observe_actors(f->actors);
    check(runtime.tracker().count()==0,"stale body source cannot enter legal motion tracker");
    f->round(13);f->actors.samples[0].alive=false;runtime.observe_actors(f->actors);
    check(!runtime.order_evidence().full_order,"dead or unavailable roster body does not infer turn-order parity");f->actors.samples[0].alive=true;
    f->actors.truncated=true;f->feed(runtime,14);check(runtime.tracker().count()==0,"budget-truncated actor prefix cannot upgrade exact observation coverage");f->actors.truncated=false;
    f->actors.samples[4].id=f->actors.samples[3].id;f->feed(runtime,15);check(runtime.tracker().count()==0,"duplicate visible native identity invalidates frame");
    f=std::make_unique<Fixture>();Timetable normal,permuted;f->history(normal);
    std::reverse(f->actors.allied_roster.begin(),f->actors.allied_roster.begin()+3);std::reverse(f->actors.enemy_roster.begin(),f->actors.enemy_roster.begin()+3);
    std::reverse(f->actors.samples.begin(),f->actors.samples.begin()+5);f->actors.samples[2].position={0,2};f->feed(permuted,9);f->actors.samples[2].position={1,2};f->feed(permuted,10);
    check(permuted.order_evidence().full_order,"public roster/sample permutation does not alter turn-order inference");
    for(int n=0;n<normal.tracker().count();++n)check(normal.tracker().track(n)->current.id==permuted.tracker().track(n)->current.id,"nearest-body selection remains distance then actual native ID");
    f->actors.enemy_roster[2]=f->actors.allied_roster[0];f->feed(permuted,11);check(permuted.tracker().count()==0,"cross-team duplicate public identity rejects complete order");
}
}
int main(){order_inference();uncertified_draft_order();uncertified_order_oracle();runtime_forecasts();fresh_waits_and_fallback();current_only_evidence();invalidation_and_permutation();
    std::printf("timetable-runtime checks=%d legal-witness-and-fuel-gates=pending\n",checks);}
