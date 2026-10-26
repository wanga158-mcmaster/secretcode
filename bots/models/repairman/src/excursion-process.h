#pragma once
// Included after runtime.h's SDK-free preparation types.
namespace plan4::repairman::runtime {
struct RouteStep {Point position{};float incoming=0;bool supported=false;};
struct RoutePlan {std::array<RouteStep,max_horizon>steps{};int count=0;bool supported=false;};
struct CyclePlan {
    plan4::runtime::ObservationClock clock{};
    int group_id=-1,target_member_id=-1;
    unsigned lifecycle_version=0;
    std::uint64_t kit_version=0;
    std::uint32_t stat_epoch=0;
    Provenance quote{};
    // These certify only a prediction under the current legal scene. Neither
    // installs a hypothetical future exact observation or authorizes attacks.
    RoutePlan returning{},capture_entry{};
    bool supported=false,solo_capture_supported=false,capture_hold_zero_exposure=false,executor_enabled=false;
};
enum class TapeKind {attack,extra_camp_phase,escape,recall,heal,return_move,clear,capture};
struct TapeEvent {TapeKind kind=TapeKind::attack;int start=0,end=0;Point position{};float health=0,remaining=0;};
struct Path {
    Forecast prefix,macro;
    SimulationState checkpoint,next;
    std::array<TapeEvent,max_events>tape{};
    int count=0,excursions=0,attacks=0,work=0;
    float remaining=0;
    bool returned=false,requires_reobservation=false,captured=false,restored=false,supported=false;
    Failure failure=Failure::unsupported;
    Path() noexcept {}
};
// A private first-FARM prefix. The common seam completes the same pair/altar
// and the unchanged fixed continuation. Other stocks are never edited here.
inline bool prefix_into(const Information&info,const Menu&menu,const Job&job,const PreparedAttack&prepared,
                        const CyclePlan&plan,bool enabled,Path&path){
    path.count=path.excursions=path.attacks=path.work=0;path.remaining=prepared.group.health;
    path.returned=path.requires_reobservation=path.captured=path.restored=path.supported=false;path.failure=Failure::unsupported;
    const auto budget=[&]{if(!can_refine(info)){path.failure=Failure::budget;return false;}return true;};
    if(!budget())return false;
    if(!prepared.supported||job.kind!=JobKind::farm||job.group!=prepared.group_index||!plan.supported||!plan.executor_enabled
        ||!plan.solo_capture_supported||!plan.capture_hold_zero_exposure||plan.clock.round!=info.round||plan.clock.own.id!=info.own.id||!quoted_now(plan.quote,info.round)
        ||plan.group_id!=prepared.group.group_id||plan.target_member_id!=prepared.actor.target_id
        ||plan.lifecycle_version!=prepared.group.lifecycle_version||plan.kit_version!=prepared.kit_version||plan.stat_epoch!=prepared.stat_epoch
        ||job.group<0||job.group>=info.group_count)return false;
    const auto legal_route=[&](const RoutePlan&r,Point from,Point target){
        if(!r.supported||r.count<0||r.count>max_horizon)return false;
        for(int n=0;n<r.count;++n){if(!budget())return false;const auto&s=r.steps[n];
            if(!s.supported||!valid_loss(s.incoming)||std::max(std::abs(s.position.x-from.x),std::abs(s.position.y-from.y))!=1)return false;
            from=s.position;
        }
        return from==target;
    };
    // Same source/quote support gate in both arms, including the route whose
    // scheduling coupling control disables. Unknown exposures are never zero.
    if(!legal_route(plan.returning,info.own_spawn,prepared.actor.position)
        ||!legal_route(plan.capture_entry,prepared.actor.position,info.groups[job.group].position))return false;
    if(!initialize_state_into(info,menu,path.prefix,path.checkpoint))return false;
    auto&f=path.prefix;auto&state=path.checkpoint;
    f.macros_started=1;f.first_conversion_round=state.deadline+1;f.truncated=false;
    Phase phase=Phase::owner_action;
    const auto snapshot=[&]{f.final_round=state.now;f.final_bag=state.bag;f.final_gold=state.gold;f.final_health=state.health;};
    const auto emit=[&](TapeKind kind,EventKind event,int duration,Point position,float health,Phase end_phase=Phase::owner_action){
        if(!budget())return false;
        if(duration<0)return false;
        if(duration>state.deadline-state.now){
            // Only zero-damage recall/capture waits may span a cutoff here.
            // Attacks, escape/return moves and healing are emitted individually.
            if(health!=state.health)return false;
            const int start=state.now;
            if(state.now<state.deadline&&path.count<max_events&&f.event_count<max_events){
                path.tape[path.count++]={kind,start,state.deadline,state.position,state.health,path.remaining};
                f.events[f.event_count++]={EventKind::wait,{start,phase},{state.deadline,Phase::owner_action},info.groups[job.group].id,
                    state.bag,state.bag,0,state.gold,state.health,Status::predicted};}
            path.failure=Failure::horizon;state.now=state.deadline;state.stopped=true;
            f.truncated=true;f.abort_reason=AbortReason::horizon;
            f.terminal_unknown=state.deadline==info.settings.max_rounds;snapshot();return false;
        }
        if(path.count>=max_events||f.event_count>=max_events||!std::isfinite(health)||health<=0||health>info.own.maximum_health)return false;
        const int start=state.now;const unsigned before=state.bag;
        state.now+=duration;state.position=position;state.health=health;
        path.tape[path.count++]={kind,start,state.now,position,health,path.remaining};
        f.events[f.event_count++]={event,{start,phase},{state.now,end_phase},info.groups[job.group].id,
            before,before,0,state.gold,health,Status::predicted};phase=end_phase;++path.work;snapshot();return true;
    };
    const auto route=[&](const RoutePlan&route,Point target,TapeKind kind){
        if(!route.supported||route.count<0||route.count>max_horizon)return false;
        for(int n=0;n<route.count;++n){if(!budget())return false;const auto&step=route.steps[n];
            if(!step.supported||!valid_loss(step.incoming)||std::max(std::abs(step.position.x-state.position.x),std::abs(step.position.y-state.position.y))!=1)return false;
            if(!emit(kind,EventKind::travel,1,step.position,state.health-step.incoming))return false;
        }
        return state.position==target;
    };
    for(int excursion=0;excursion<2;++excursion){++path.excursions;path.restored=false;int excursion_attacks=0;
        while(path.remaining>0){if(!budget())return false;auto actor=prepared.actor;actor.health=state.health;
            const auto safe=safe_segment_quote(actor,prepared.escape.proof,info.settings);
            if(safe.failure!=Failure::none){if(safe.failure==Failure::budget){path.failure=Failure::budget;return false;}break;}
            const int needed=std::max(1,int(std::min(4.0,std::ceil(double(path.remaining)/prepared.basic_damage))));
            const int attacks=std::min(safe.attacks,needed);
            if(attacks<1)return false;
            for(int n=0;n<attacks;++n){
                if(state.now>=state.deadline){path.failure=Failure::horizon;state.stopped=true;f.truncated=true;f.abort_reason=AbortReason::horizon;snapshot();return false;}
                const float remaining=std::max(0.0f,path.remaining-prepared.basic_damage);
                if(!emit(TapeKind::attack,EventKind::wait,1,state.position,state.health-actor.predicted_incoming))return false;
                path.remaining=remaining;path.tape[path.count-1].remaining=remaining;++path.attacks;++excursion_attacks;
            }
        }
        if(path.remaining<=0){
            if(!emit(TapeKind::clear,EventKind::clear,0,state.position,state.health))return false;
            if(!route(plan.capture_entry,info.groups[job.group].position,TapeKind::clear))return false;
            if(!emit(TapeKind::capture,EventKind::capture_start,1,state.position,state.health,Phase::capture))return false;
            if(!emit(TapeKind::capture,EventKind::capture,info.settings.capture_rounds,state.position,state.health,Phase::capture))return false;
            const auto&source=info.groups[job.group];
            const unsigned gems=allocated_reward(source.gems,source.eligible_allied_occupants,source.own_is_lowest_occupant);
            const unsigned gold=allocated_reward(source.gold,source.eligible_allied_occupants,source.own_is_lowest_occupant);
            if(state.bag>std::numeric_limits<unsigned>::max()-gems||state.gold>std::numeric_limits<unsigned>::max()-gold)return false;
            state.bag+=gems;state.gold+=gold;auto&event=f.events[f.event_count-1];event.bag_after=state.bag;event.gold_after=state.gold;
            if(source.respawn_rounds>std::numeric_limits<int>::max()-state.now)return false;
            state.groups[job.group]={Lifecycle::captured,state.now+std::max(0,source.respawn_rounds),true,true};
            path.captured=path.supported=true;path.failure=Failure::none;snapshot();return true;
        }
        if(!excursion_attacks)return false;
        auto actor=prepared.actor;actor.health=state.health;const auto escape=quote_segment(actor,prepared.escape.proof,info.settings,0,false);
        if(escape.failure!=Failure::none){path.failure=escape.failure;return false;}
        if(!emit(TapeKind::extra_camp_phase,EventKind::wait,1,state.position,state.health-actor.predicted_incoming))return false;
        for(int n=0;n<prepared.escape.path_count;++n){if(!budget())return false;
            if(!emit(TapeKind::escape,EventKind::escape,1,prepared.escape.path[n],state.health-prepared.escape.proof.escape[n].incoming))return false;}
        if(!emit(TapeKind::recall,EventKind::recall_arrival,escape.recall_ticks,info.own_spawn,state.health,Phase::owner_start))return false;
        const double recovery=double(info.own.maximum_health)*info.settings.spawn_heal_fraction;
        if(!std::isfinite(recovery)||recovery<=0)return false;
        const double heals=std::ceil((double(info.own.maximum_health)-state.health)/recovery);
        if(!std::isfinite(heals)||heals<0||heals>max_horizon)return false;
        for(int n=0;n<int(heals);++n){
            if(state.now>=state.deadline){path.failure=Failure::horizon;state.stopped=true;f.truncated=true;f.abort_reason=AbortReason::horizon;snapshot();return false;}
            if(!emit(TapeKind::heal,EventKind::heal,1,info.own_spawn,std::min(info.own.maximum_health,state.health+float(recovery))))return false;
        }
        path.restored=true;
        if(!enabled||excursion==1){path.supported=true;path.failure=Failure::unfinished;snapshot();return true;}
        if(!route(plan.returning,prepared.actor.position,TapeKind::return_move))return false;
        path.returned=path.requires_reobservation=true;
    }
    return false;
}
inline bool predict_into(const Information&info,const Menu&menu,const Job&job,const PreparedAttack&prepared,
                         const CyclePlan&plan,bool enabled,Path&path,Forecast&out,const ForecastHooks&hooks={}){
    if(!prefix_into(info,menu,job,prepared,plan,enabled,path)){
        if(path.failure==Failure::horizon){out=path.prefix;path.supported=true;return true;}
        return false;
    }
    if(path.captured){
        if(!complete_farm_after_first_into(info,job,menu,path.prefix,path.checkpoint,path.macro,path.next,hooks))return false;
        return continue_from_into(info,menu,path.macro,path.next,out,hooks);
    }
    // Control and exhausted two-excursion paths end at the physical restored
    // spawn. The immutable original source supplies ordinary full-clear work
    // in subsequent macros: an independence approximation, no HP reset claim.
    return continue_from_into(info,menu,path.prefix,path.checkpoint,out,hooks);
}
} // namespace plan4::repairman::runtime
