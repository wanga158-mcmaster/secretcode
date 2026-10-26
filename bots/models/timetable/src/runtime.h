#pragma once
#include "timetable.h"
#include "../../common/model-runtime.h"

namespace plan4::runtime {
struct TimetableOrder {
    bool current_observations = false, full_order = false;
    bool allied_started_current = false, next_priorities_assumed = false;
};

// Dynamic order is inferred only from personally current bodies. Roster vector
// order and native IDs never stand in for certified draft order.
inline TimetableOrder timetable_order(const ActorFrame& frame,
        std::array<timetable::ActorSample,9>& samples,int& count) {
    count=0;TimetableOrder result;
    const int round=frame.clock.round,own=frame.clock.own.id;
    if(round<0||own<0||!frame.roster_complete||frame.truncated||!frame.own_observed
        ||frame.allied_count<1||frame.allied_count>5||frame.enemy_count<1||frame.enemy_count>5
        ||frame.count<0||frame.count>9)return result;
    auto roster_side=[&](int id){
        for(int n=0;n<frame.allied_count;++n)if(frame.allied_roster[n]==id)return 1;
        for(int n=0;n<frame.enemy_count;++n)if(frame.enemy_roster[n]==id)return 2;
        return 0;
    };
    int own_count=0;
    for(int side=0;side<2;++side){const auto& ids=side?frame.enemy_roster:frame.allied_roster;const int size=side?frame.enemy_count:frame.allied_count;
        for(int n=0;n<size;++n){if(ids[n]<0)return result;own_count+=!side&&ids[n]==own;
            for(int p=0;p<n;++p)if(ids[p]==ids[n])return result;
            if(side)for(int p=0;p<frame.allied_count;++p)if(ids[n]==frame.allied_roster[p])return result;}}
    if(own_count!=1)return result;
    auto fresh=[&](const VisibleActor& actor,bool is_own){return actor.id>=0&&actor.alive&&actor.position.x>=0&&actor.position.y>=0
        &&std::isfinite(actor.health)&&std::isfinite(actor.maximum_health)&&actor.health>0&&actor.health<=actor.maximum_health
        &&actor.provenance.observed_round==round&&actor.provenance.status==Status::exact_now
        &&actor.provenance.source==(is_own?Source::own_state:Source::personal_vision);};
    if(frame.own.id!=own||!frame.own.ally||!fresh(frame.own,true)||frame.own.completed_turn)return result;
    for(int n=0;n<frame.count;++n){const auto& actor=frame.samples[n];const int side=roster_side(actor.id);
        if(actor.id==own||!side||actor.ally!=(side==1)||!fresh(actor,false)){count=0;return result;}
        for(int p=0;p<n;++p)if(frame.samples[p].id==actor.id){count=0;return result;}
        samples[count++]={actor.id,actor.position,actor.provenance,{},true};
    }
    result.current_observations=true;
    if(frame.allied_count!=frame.enemy_count||frame.count!=frame.allied_count+frame.enemy_count-1)return result;
    auto body=[&](int id)->const VisibleActor*{if(frame.own.id==id)return&frame.own;
        for(int n=0;n<frame.count;++n)if(frame.samples[n].id==id)return&frame.samples[n];
        return nullptr;};
    std::array<const VisibleActor*,5> ally{},enemy{};
    const int size=frame.allied_count;
    for(int n=0;n<size;++n){ally[n]=body(frame.allied_roster[n]);enemy[n]=body(frame.enemy_roster[n]);if(!ally[n]||!enemy[n])return result;}
    if(!frame.draft_order_known){
        int allies_done=0,enemies_done=0;
        for(int n=0;n<size;++n){allies_done+=ally[n]->completed_turn;enemies_done+=enemy[n]->completed_turn;
            for(int p=0;p<size;++p)if((ally[n]->current_priority>ally[p]->current_priority&&!ally[n]->completed_turn&&ally[p]->completed_turn)
                ||(enemy[n]->current_priority>enemy[p]->current_priority&&!enemy[n]->completed_turn&&enemy[p]->completed_turn))return result;
            if(ally[n]->id!=own&&ally[n]->current_priority!=frame.own.current_priority
                &&ally[n]->completed_turn!=(ally[n]->current_priority>frame.own.current_priority))return result;
        }
        if(enemies_done!=allies_done&&enemies_done!=allies_done+1)return result;
        const bool enemy_started=enemies_done==allies_done+1;result.allied_started_current=!enemy_started;
        int own_next=0;std::array<bool,9> before{};
        for(int n=0;n<count;++n){const auto* actor=body(samples[n].id);if(!actor->ally)continue;
            if(actor->next_priority!=frame.own.next_priority)before[n]=actor->next_priority>frame.own.next_priority;
            else if(actor->current_priority==actor->next_priority&&frame.own.current_priority==frame.own.next_priority)
                before[n]=actor->completed_turn;
            else return result; // A newly formed tie has unknown draft ordering.
            own_next+=before[n];
        }
        const int enemy_cutoff=own_next+(!enemy_started?1:0);
        for(int n=0;n<count;++n){const auto* actor=body(samples[n].id);if(actor->ally)continue;
            int first=0,last=-1;
            for(int p=0;p<size;++p){first+=enemy[p]->next_priority>actor->next_priority;last+=enemy[p]->next_priority>=actor->next_priority;}
            if(last<enemy_cutoff)before[n]=true;
            else if(first>=enemy_cutoff)before[n]=false;
            else return result; // A next-priority tie straddles the owner cutoff.
        }
        for(int n=0;n<count;++n)samples[n].order={true,body(samples[n].id)->completed_turn,before[n]};
        result.full_order=result.next_priorities_assumed=true;return result;
    }
    for(int n=0;n<size;++n){if(ally[n]->draft_ordinal<0||ally[n]->draft_ordinal>=size||enemy[n]->draft_ordinal<0||enemy[n]->draft_ordinal>=size)return result;
        for(int p=0;p<n;++p)if(ally[p]->draft_ordinal==ally[n]->draft_ordinal||enemy[p]->draft_ordinal==enemy[n]->draft_ordinal)return result;}
    auto current=[](const VisibleActor* a,const VisibleActor* b){return a->current_priority!=b->current_priority
        ?a->current_priority>b->current_priority:a->draft_ordinal<b->draft_ordinal;};
    auto next=[](const VisibleActor* a,const VisibleActor* b){return a->next_priority!=b->next_priority
        ?a->next_priority>b->next_priority:a->draft_ordinal<b->draft_ordinal;};
    std::sort(ally.begin(),ally.begin()+size,current);std::sort(enemy.begin(),enemy.begin()+size,current);
    int rank=0;while(rank<size&&ally[rank]->id!=own)++rank;if(rank==size)return result;
    const bool enemy_started=enemy[rank]->completed_turn;
    for(int n=0;n<size;++n)if(ally[n]->completed_turn!=(n<rank)||enemy[n]->completed_turn!=(n<rank||(n==rank&&enemy_started)))return result;
    result.allied_started_current=!enemy_started;
    std::sort(ally.begin(),ally.begin()+size,next);std::sort(enemy.begin(),enemy.begin()+size,next);
    int own_next=0;while(own_next<size&&ally[own_next]->id!=own)++own_next;
    // Teams alternate, so the enemy starts next exactly when allies start now.
    const bool enemy_starts_next=!enemy_started;
    for(int n=0;n<count;++n){const auto* actor=body(samples[n].id);int actor_rank=0;
        const auto& order=actor->ally?ally:enemy;while(actor_rank<size&&order[actor_rank]->id!=actor->id)++actor_rank;
        samples[n].order={true,actor->completed_turn,actor_rank<own_next||(!actor->ally&&actor_rank==own_next&&enemy_starts_next)};
    }
    result.full_order=result.next_priorities_assumed=true;return result;
}

class Timetable : public M0 {
public:
    enum class EvidenceReason { none, no_batch, actors, current_only, route, partial_route, budget, no_travel };
private:
    struct Cache {Point target{};int nominal=-1;timetable::Quote quote{};};
    struct Path {int extra=0;bool first_travel=true;};
    timetable::State tracker_;
    ActorFrame actors_{};TimetableOrder order_{};
    SpatialFrame spatial_{},movement_{};
    std::array<Cache,max_jobs> cache_{};int cache_count_=0;
    timetable::WaitLedger waits_{};
    Point selected_target_{};int selected_actor_=-1,selected_round_=-1;
    std::uint64_t selected_epoch_=0;
    bool selected_=false;
    bool enabled_=true,batch_seen_=false,batch_budget_=false;
    int supported_quotes_=0,unknown_quotes_=0,route_calls_=0;
    EvidenceReason quote_reason_=EvidenceReason::no_batch;
    std::uint64_t evidence_revision_=0;
    void reset_batch_evidence(){batch_seen_=batch_budget_=false;supported_quotes_=unknown_quotes_=route_calls_=0;quote_reason_=EvidenceReason::no_batch;}
    int unknown_motion()const{
        int count=0;for(int n=0;n<tracker_.count();++n){const auto& track=*tracker_.track(n);const int age=actors_.clock.round-track.previous_round;
            count+=!order_.full_order||!track.current.order.known||!track.has_previous||age<1||age>2;}
        return count;
    }
    static timetable::Tile tile(Point p,const void* context){const auto& topology=*static_cast<const PublicTopology*>(context);
        if(!topology.complete||!topology.walkable)return timetable::Tile::unknown;
        if(p.x<0||p.y<0||p.x>=topology.width||p.y>=topology.height)return timetable::Tile::blocked;
        return topology.walkable(p,topology.context)?timetable::Tile::walkable:timetable::Tile::blocked;
    }
    static bool first_target(const Information& info,const Job& job,Point& target){
        if(job.kind==JobKind::farm&&job.group>=0&&job.group<info.group_count&&job.group<max_groups){target=info.groups[job.group].position;return true;}
        if(job.kind==JobKind::bank&&job.altar>=0&&job.altar<info.altar_count&&job.altar<max_altars){target=info.altars[job.altar].position;return true;}
        return false;
    }
    bool current_frame(const SpatialFrame& frame,int round,int actor,Point origin)const{
        return frame.clock.round==round&&frame.clock.own.id==actor&&actors_.clock.round==round&&actors_.clock.own.id==actor
            &&order_.current_observations&&actors_.own.position==origin&&frame.topology.complete&&frame.topology.walkable
            &&frame.topology.width>0&&frame.topology.height>0&&frame.route_prefix;
    }
    timetable::Quote quote(const SpatialFrame& frame,int round,int actor,Point origin,Point target,int nominal)const{
        if(nominal<=0||!current_frame(frame,round,actor,origin))return{};
        std::array<Point,timetable::max_route_steps> steps;
        const int expected=std::min(nominal,timetable::max_route_steps);
        const int count=frame.route_prefix(origin,target,steps.data(),expected,frame.route_context);
        if(count!=expected)return{};
        return tracker_.quote({round,round,origin,steps.data(),count,true,true},{tile,&frame.topology});
    }
    timetable::Quote cached_quote(const Information& info,Point target,int nominal){
        for(int n=0;n<cache_count_;++n)if(cache_[n].target==target&&cache_[n].nominal==nominal)return cache_[n].quote;
        const auto result=quote(spatial_,info.round,info.own.id,info.own.position,target,nominal);
        ++route_calls_;
        if(cache_count_<max_jobs)cache_[cache_count_++]={target,nominal,result};
        return result;
    }
    static int phase(EventKind kind,int,int nominal,int,int now,const Information& info,const void*,void* context){
        auto& path=*static_cast<Path*>(context);
        if(kind!=EventKind::travel||!path.first_travel)return nominal;
        path.first_travel=false;
        return now==info.round&&nominal>0?nominal+path.extra:nominal;
    }
public:
    explicit Timetable(bool enabled=true):tracker_(enabled),enabled_(enabled){}
    const char* name()const{return"timetable";}
    void observe_actors(const ActorFrame& frame){
        cache_count_=0;reset_batch_evidence();
        std::array<timetable::ActorSample,9> samples;int count=0;
        if(frame.clock.round==actors_.clock.round&&frame.clock.own.id==actors_.clock.own.id){
            order_={};tracker_.observe(frame.clock.round,frame.clock.own.id,frame.own.position,nullptr,0,true);return;}
        actors_=frame;order_=timetable_order(frame,samples,count);
        tracker_.observe(frame.clock.round,frame.clock.own.id,frame.own.position,samples.data(),order_.current_observations?count:0,!order_.current_observations);
    }
    void observe_spatial(const SpatialFrame& frame){spatial_=frame;cache_count_=0;reset_batch_evidence();}
    void observe_movement(const SpatialFrame& frame){movement_=frame;}
    void predict_into(const Information& info,const Job& job,const Menu& menu,Forecast& out){
        if(!batch_seen_){batch_seen_=true;++evidence_revision_;quote_reason_=EvidenceReason::no_travel;}
        Point target;Path path;
        if(first_target(info,job,target)&&info.own.alive&&info.own.channel==Channel::none){
            const int nominal=distance(info,info.own.position,target);
            const auto q=cached_quote(info,target,nominal);if(q.supported)path.extra=q.wait_phases;
            if(nominal!=0){
                if(q.supported)++supported_quotes_;
                else{++unknown_quotes_;quote_reason_=!order_.current_observations?EvidenceReason::actors:EvidenceReason::route;}
            }
        }
        ForecastHooks hooks;hooks.path=&path;hooks.phase_duration=phase;plan4::predict_into(info,job,menu,out,hooks);
        batch_budget_|=out.budget_exhausted;
    }
    void evaluate_into(const Information& info,const Menu& menu,Evaluation& out){
        out.menu=menu;out.chosen=-1;out.evaluated_count=0;out.budget_exhausted=false;cache_count_=0;
        batch_seen_=true;batch_budget_=false;supported_quotes_=unknown_quotes_=route_calls_=0;quote_reason_=EvidenceReason::no_travel;++evidence_revision_;
        if(menu.count<0||menu.count>max_jobs){out.budget_exhausted=batch_budget_=true;return;}
        for(int n=0;n<menu.count;++n){if(info.work_budget.can_refine&&!info.work_budget.can_refine(info.work_budget.context)){out.budget_exhausted=true;break;}
            predict_into(info,menu.jobs[n],menu,out.forecasts[n]);if(out.forecasts[n].budget_exhausted){out.budget_exhausted=true;break;}++out.evaluated_count;}
        batch_budget_|=out.budget_exhausted;
    }
    void selected(const Information& info,const Job& job,std::uint64_t epoch,bool productive){
        selected_=productive&&info.own.alive&&info.own.channel==Channel::none&&first_target(info,job,selected_target_);
        selected_actor_=info.own.id;selected_round_=info.round;selected_epoch_=epoch;
        if(!waits_.initialized||waits_.decision!=epoch)waits_={epoch,0,true};
    }
    void selection_canceled(){selected_=false;}
    bool wait_for_movement(const ObservationClock& clock,Point origin,const Point* goals,int count){
        if(!selected_||selected_actor_!=clock.own.id||clock.round<selected_round_||count<1||count>32||!goals||!order_.full_order
            ||!current_frame(movement_,clock.round,clock.own.id,origin)||waits_.used>=timetable::max_waits)return false;
        bool goal=false;for(int n=0;n<count;++n)goal|=goals[n]==selected_target_;if(!goal)return false;
        const int nominal=movement_.travel.distance?movement_.travel.distance(origin,selected_target_,movement_.travel.context):-1;
        const auto q=quote(movement_,clock.round,clock.own.id,origin,selected_target_,nominal);
        if(!q.supported||q.unknown_motion)return false;
        return timetable::bounded_wait(q,waits_,selected_epoch_,true).wait_one_owner_phase;
    }
    TimetableOrder order_evidence()const{return order_;}
    const timetable::State& tracker()const{return tracker_;}
    int waits_used()const{return waits_.used;}
    ModelEvidence model_evidence()const{
        // paths counts valid-route occupancy-law quotes; unknown counts tracked
        // bodies lacking supported future motion. Current-only is a declared
        // law in both arms, not an effect or a claim of extrapolation accuracy.
        const int motion_unknown=unknown_motion();
        const auto reason=!batch_seen_?EvidenceReason::no_batch:batch_budget_?EvidenceReason::budget:
            !order_.current_observations?EvidenceReason::actors:unknown_quotes_?(supported_quotes_?EvidenceReason::partial_route:quote_reason_):
            !supported_quotes_?EvidenceReason::no_travel:motion_unknown?EvidenceReason::current_only:EvidenceReason::none;
        return{true,batch_seen_&&!batch_budget_&&order_.current_observations&&supported_quotes_>0,enabled_,int(reason),
            tracker_.count(),motion_unknown,supported_quotes_,route_calls_,evidence_revision_};
    }
};
} // namespace plan4::runtime
