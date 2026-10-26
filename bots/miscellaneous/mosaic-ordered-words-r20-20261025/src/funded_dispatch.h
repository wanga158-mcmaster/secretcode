#pragma once
// Input-local funding and authority, not a movement planner or a game budget.
// Every cost is a caller-supplied upper bound for the COMPLETE SDK operation,
// including fresh validation, actual dispatch/failure closure and its required
// continuation. The adapter must measure those bounds on the real controller.
#include <cstdint>
#include <limits>

namespace mosaic::funded_dispatch {
using Fuel=std::uint64_t;
using Revision=std::uint64_t;
inline constexpr Fuel fuel_limit=std::numeric_limits<Fuel>::max()/4;

enum class Owner {work,approach,returning,bank,recover,capture,observe};
// maintain_timer requires a fresh finite engine-progress witness (positive
// channel/capture/recovery countdown), never mere presence at a nominal goal.
enum class Action {none,basic,cast,move,channel,maintain_timer};
enum class Knowledge {unknown,absent,executable};
enum class Kind {work,physical,probe_work,prepare,refresh,blocked};
enum class Reason {
    ok,invalid_cost,stale_frame,work_incumbent,physical_incumbent,
    bounded_probe,finite_preparation,failure_refresh,unfunded,missing_owner,
    exposed_preparation,unresolved_nominal_goal
};

struct Candidate {
    Action action{Action::none};
    bool ready{},continuation_admitted{},owner_compatible{true};
    Revision frame{},observation{};
    // Includes the complete actual operation AND its mandatory continuation.
    // With unknown work, a fresh continuation_admitted quote may protect one
    // possible work tail while ready remains false; it is not action authority.
    Fuel cost{};
};
struct Preparation {
    // hold_admitted covers ALL remaining preparation waits plus the real
    // dispatch horizon. A finite cursor alone cannot justify exposed waiting.
    bool pending{},hold_admitted{};
    unsigned remaining{};
    Fuel step_cost{};
};
struct Input {
    Revision frame{},observation{};
    Fuel fuel{};
    Owner owner{Owner::work};
    Knowledge work{Knowledge::unknown};
    Candidate work_action{},physical{};
    // physical.cost remains a protected caller bound even while its dated
    // nomination still needs a finite preparation stage and fresh validation.
    Fuel work_probe_cost{},refresh_cost{};
    Preparation preparation{};
    bool refresh_required{},nominal_goal{};
};
struct Decision {
    Kind kind{Kind::blocked};
    Reason reason{Reason::missing_owner};
    Fuel protected_tail{},optional_allowance{};
    Action action{Action::none};
};

inline bool valid_cost(Fuel value){return value>0&&value<=fuel_limit;}
inline bool fits(Fuel fuel,Fuel operation,Fuel tail=0){
    return fuel<=fuel_limit&&operation<=fuel_limit&&tail<=fuel_limit&&
           operation<fuel&&tail<fuel-operation;
}
inline bool current_continuation(const Candidate& candidate,const Input& input){
    return candidate.continuation_admitted&&candidate.owner_compatible&&
        candidate.action!=Action::none&&candidate.frame==input.frame&&
        candidate.observation==input.observation&&valid_cost(candidate.cost);
}
inline bool current(const Candidate& candidate,const Input& input){
    return candidate.ready&&current_continuation(candidate,input);
}
inline Decision selected(Kind kind,Reason reason,Fuel fuel,Fuel cost,Action action=Action::none){
    return {kind,reason,cost,fuel-cost,action};
}

inline Decision decide(const Input& input){
    if(input.fuel>fuel_limit||input.work_probe_cost>fuel_limit||
       input.refresh_cost>fuel_limit||input.preparation.step_cost>fuel_limit||
       input.physical.cost>fuel_limit||input.work_action.cost>fuel_limit)
        return {Kind::blocked,Reason::invalid_cost};
    const bool physical=current(input.physical,input);
    const bool work=input.work==Knowledge::executable&&current(input.work_action,input);
    const Fuel tail=input.physical.cost;

    // An actual false revokes old mask/target authority. A refreshed observation
    // must close that failure before either old command can be attempted again.
    if(input.refresh_required){
        if(valid_cost(input.refresh_cost)&&valid_cost(tail)&&
           fits(input.fuel,input.refresh_cost,tail))
            return selected(Kind::refresh,Reason::failure_refresh,input.fuel,
                            input.refresh_cost+tail);
        return {Kind::blocked,Reason::unfunded,tail};
    }
    // A complete executable work incumbent wins BEFORE optional improvement.
    // This includes a ranged Basic that spends M when its actual continuation
    // is already admitted; moving first would destroy that useful option.
    if(work&&fits(input.fuel,input.work_action.cost))
        return selected(Kind::work,Reason::work_incumbent,input.fuel,
                        input.work_action.cost,input.work_action.action);

    Fuel next=tail;
    const bool preparing=input.preparation.pending&&input.preparation.remaining>0;
    if(!physical&&preparing){
        if(!input.preparation.hold_admitted)
            return {Kind::blocked,Reason::exposed_preparation,tail};
        if(!valid_cost(input.preparation.step_cost)||!valid_cost(tail)||
           !fits(fuel_limit,input.preparation.step_cost,tail))
            return {Kind::blocked,Reason::invalid_cost,tail};
        next+=input.preparation.step_cost;
    }
    if(!physical&&!preparing){
        // A nominal spell perch can have no movement command and no cached
        // Basic while actual cast work is still unknown. A fresh HOLD quote
        // can fund one bounded probe plus a possible work tail independently
        // of navigation. Neither the perch nor the probe earns action/progress:
        // after an absent/unknown result the caller must consume this probe
        // opportunity and actually refine its finite observation/service owner.
        if(input.work==Knowledge::unknown&&current_continuation(input.work_action,input)&&
           valid_cost(input.work_probe_cost)&&
           fits(input.fuel,input.work_probe_cost,input.work_action.cost))
            return selected(Kind::probe_work,Reason::bounded_probe,input.fuel,
                            input.work_probe_cost+input.work_action.cost);
        return {Kind::blocked,input.nominal_goal?Reason::unresolved_nominal_goal:
                 work?Reason::unfunded:Reason::missing_owner,tail};
    }
    const bool unresolved=input.work==Knowledge::unknown||
        (input.work==Knowledge::executable&&!work);
    if(unresolved&&valid_cost(input.work_probe_cost)&&valid_cost(next)&&
       fits(input.fuel,input.work_probe_cost,next))
        return selected(Kind::probe_work,Reason::bounded_probe,input.fuel,
                        input.work_probe_cost+next);

    if(physical&&fits(input.fuel,tail))
        return selected(Kind::physical,Reason::physical_incumbent,input.fuel,
                        tail,input.physical.action);
    if(preparing&&input.preparation.hold_admitted&&valid_cost(next)&&fits(input.fuel,next))
        return selected(Kind::prepare,Reason::finite_preparation,input.fuel,next);
    return {Kind::blocked,input.nominal_goal?Reason::unresolved_nominal_goal:
             valid_cost(tail)?Reason::unfunded:Reason::missing_owner,tail};
}

// Optional computation is admitted only against a REAL retained incumbent's
// complete tail, from its CURRENT remaining fuel. Zero-cost/unknown menus do
// not get an unbounded loop; false returns do not spend an imaginary credit.
inline bool optional_fits(Fuel current_fuel,Fuel declared_cost,Fuel protected_tail){
    return valid_cost(declared_cost)&&valid_cost(protected_tail)&&
           fits(current_fuel,declared_cost,protected_tail);
}

// Finite preparation is actual completed new work, not observation-nonce churn.
// Intent revisions name semantic service/dependency changes; using the owner
// round as the intent revision would invalidate the finite-progress argument.
class Cursor {
public:
    static constexpr unsigned capacity=32;
    bool request(Revision intent,unsigned count){
        if(!count||count>capacity)return false;
        if(active_&&intent==intent_)return count==count_;
        active_=true;intent_=intent;count_=count;done_=0;validated_=false;return true;
    }
    bool enter(Revision frame){
        if(seen_frame_&&frame<frame_)return false;
        if(!seen_frame_||frame!=frame_)validated_=false;
        seen_frame_=true;frame_=frame;return true;
    }
    bool advance(Revision intent,Revision frame,unsigned stage,bool completed){
        if(!active_||!seen_frame_||intent!=intent_||frame!=frame_||
           stage!=done_||done_==count_||!completed)return false;
        ++done_;validated_=false;return true;
    }
    bool validate(Revision intent,Revision frame,Revision observation,bool actual_ready){
        // Even a failed same-frame validation revokes its older authority.
        // It must not leave a prior ready=true token reusable after observing
        // an occupied endpoint or otherwise invalid current action.
        validated_=false;
        if(!active_||intent!=intent_||frame!=frame_||done_!=count_||!actual_ready)return false;
        validated_=true;observation_=observation;return true;
    }
    bool authority(Revision frame,Revision observation)const{
        return validated_&&frame==frame_&&observation==observation_;
    }
    bool authority(Revision intent,Revision frame,Revision observation)const{
        return intent==intent_&&authority(frame,observation);
    }
    unsigned completed()const{return done_;}
    unsigned remaining()const{return active_?count_-done_:0;}
    Revision intent()const{return intent_;}
private:
    Revision intent_{},frame_{},observation_{};
    unsigned count_{},done_{};
    bool active_{},seen_frame_{},validated_{};
};

// Stable candidate IDs cover a bounded menu within one actual body/effect
// state. A false result preserves MAC but requires fresh authority; repeatedly
// renaming the same destination is not an admissible failure closure.
class FailureFence {
public:
    static constexpr unsigned capacity=32;
    bool enter(Revision frame,Revision effect,Revision observation){
        if(active_&&(frame<frame_||(frame==frame_&&effect<effect_)))return false;
        if(!active_||frame!=frame_||effect!=effect_){failed_=0;refresh_=false;}
        else if(observation<observation_)return false;
        active_=true;frame_=frame;effect_=effect;observation_=observation;return true;
    }
    bool refused(unsigned candidate){
        if(!active_||candidate>=capacity||refresh_||(failed_&(std::uint32_t(1)<<candidate)))return false;
        failed_|=std::uint32_t(1)<<candidate;refresh_=true;return true;
    }
    bool refreshed(Revision frame,Revision effect,Revision observation){
        if(!active_||frame!=frame_||effect!=effect_||observation<=observation_)return false;
        observation_=observation;refresh_=false;return true;
    }
    bool permits(Revision frame,Revision effect,Revision observation,unsigned candidate)const{
        return active_&&!refresh_&&candidate<capacity&&frame==frame_&&effect==effect_&&
            observation==observation_&&!(failed_&(std::uint32_t(1)<<candidate));
    }
    bool refresh_required()const{return refresh_;}
private:
    Revision frame_{},effect_{},observation_{};
    std::uint32_t failed_{};
    bool active_{},refresh_{};
};
}
