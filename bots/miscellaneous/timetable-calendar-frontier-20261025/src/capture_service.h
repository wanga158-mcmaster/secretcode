#pragma once
#include <cstdint>

// Selected Capture observation/return ownership only. All movement remains a
// fresh backend command over complete all-NPC support and actual reverse Trail.
// This dispatcher is shared by the runtime and the composed actuator fixture.
namespace tt::capture_service {
struct Point { int x{-1},y{-1}; friend bool operator==(Point,Point)=default; };
enum class Life { Unknown,Alive,Reward,Respawning };
enum class Read { Fresh,Unavailable,Stale };
enum class Mode { Dormant,Approach,RewardPreparation,Returning };
enum class Reason { Refuted,Revalue,Bank };
enum class StepStatus { Applied,Spent,Refused,Denied,NeedCompute,Stale,NoSupport,SupportLost,NeedContact };
enum class Contact { Clear,Revalue,Stale };
enum class Outcome { LegacyCapture,Geometry,RewardGeometry,Handled,YieldDispatcher,NeedResolver,Refuted,Revalue,Bank,Contact };
enum class Flow { Legacy,Handled,Geometry,Dispatcher,Contact };
enum class Caller { Legacy,Stop,Geometry,FreshObservation };
inline Caller caller(Flow flow)
{
    return flow==Flow::Handled?Caller::Stop:flow==Flow::Geometry?Caller::Geometry:
        flow==Flow::Legacy?Caller::Legacy:Caller::FreshObservation;
}
struct GeometryCursor
{
    std::uint64_t stage{},phase{},group{},member{},target{},cell{};
    friend bool operator==(GeometryCursor,GeometryCursor)=default;
};
inline bool geometry_after(GeometryCursor a,GeometryCursor b)
{
    if(a.stage!=b.stage)return a.stage>b.stage;
    if(a.phase!=b.phase)return a.phase>b.phase;
    if(a.group!=b.group)return a.group>b.group;
    if(a.member!=b.member)return a.member>b.member;
    if(a.target!=b.target)return a.target>b.target;
    return a.cell>b.cell;
}
struct Step
{
    StepStatus status{StepStatus::NoSupport};
    Point after{}; int distance_after{-1}; bool quiet_after{};
};
struct Frame
{
    int epoch{-1},group{-1}; Point pos;
    bool alive{},channeling{},quiet{},enemy_known{},enemy{};
    Read read{Read::Unavailable}; Life life{Life::Unknown};
    bool safe_capture{},receipt{},route_ready{};
    bool needs_geometry{};GeometryCursor geometry{};
    int distance{-1}; std::uint64_t graph{},field{};
};
// Shared REAL adapter loop: a refused move may reveal contact. The fresh
// epoch/pose fence alone does not restore the old negative enemy observation.
// No second forward command (or later retreat) uses that invalidated support.
template<class Backend>
Step guarded_advance(const Frame& frame,Backend& backend)
{
    bool refused=false,denied=false;unsigned inspected=0;
    for(const auto destination:backend.candidates(frame))
    {
        if(inspected++>=8)return {StepStatus::NoSupport}; // eight ordinary directions
        if(!backend.headroom())return {StepStatus::NeedCompute};
        const auto step=backend.attempt(frame,destination);
        if(step.status==StepStatus::Refused)
        {
            const auto contact=backend.refresh_contact(frame);
            if(contact==Contact::Stale)return {StepStatus::Stale};
            if(contact!=Contact::Clear)return {StepStatus::NeedContact};
            refused=true;continue;
        }
        if(step.status==StepStatus::Denied){denied=true;continue;}
        if(step.status!=StepStatus::NoSupport)return step;
    }
    return {refused?StepStatus::Refused:denied?StepStatus::Denied:StepStatus::NoSupport};
}
struct State
{
    Mode mode{Mode::Dormant}; Reason reason{Reason::Refuted};
    int group{-1},last_distance{-1},last_defer_epoch{-1};
    Point expected;std::uint64_t graph{},field{};unsigned deferrals{};
    GeometryCursor geometry{};int preparation_epoch{-1},progress_epoch{-1};unsigned no_progress{};
    void reset() { *this=State{}; }
};
inline bool before_optional(const State& state,bool selected_capture,bool actual_quiet)
{return state.mode==Mode::Returning || (selected_capture && !actual_quiet);}
inline Outcome resolved(Reason reason)
{return reason==Reason::Bank?Outcome::Bank:reason==Reason::Revalue?Outcome::Revalue:Outcome::Refuted;}
inline void defer(State& state,int epoch)
{if(state.last_defer_epoch!=epoch){state.last_defer_epoch=epoch;++state.deferrals;}}

// Backend obligations:
// fresh(frame) fences the frame's actual epoch/origin after enemy observations.
// advance(frame) prices ONE edge + its complete actual reverse, two bounded
// landing observation inputs and one refusal, from fresh CURRENT HP. Accepted
// moves alone update actual Trail. No historical damage is added as future debt.
// retreat(frame) freshly prices/executes actual reverse support; changed forward
// target/geometry is not by itself permission to erase that spatial history.
template<class Backend>
Outcome dispatch(State& state,const Frame& frame,Backend& backend)
{
    if(!frame.alive){state.reset();return Outcome::YieldDispatcher;}
    if(frame.channeling)return Outcome::YieldDispatcher;
    if(frame.read==Read::Stale || !backend.fresh(frame))
    {
        // No command uses the withdrawn frame. A charged observation owner must
        // not repeat that expensive prefix forever: its next input cheaply
        // re-reads the body and prices actual return rather than source work.
        if(!frame.quiet && (state.mode==Mode::Approach || state.mode==Mode::RewardPreparation))
        {state.mode=Mode::Returning;state.reason=Reason::Refuted;}
        return Outcome::Handled;
    }
    if(state.mode==Mode::RewardPreparation && state.group!=frame.group)
    {state.mode=Mode::Returning;state.reason=Reason::Refuted;}
    if(state.mode!=Mode::Returning && state.group!=frame.group)state.reset();
    if(frame.group<0 && state.mode!=Mode::Returning)return Outcome::YieldDispatcher;
    if(state.group<0)state.group=frame.group;
    if(frame.receipt && (state.mode!=Mode::Returning || state.reason!=Reason::Bank))
    {state.reason=Reason::Bank;state.mode=Mode::Returning;state.deferrals=0;}
    if(state.mode!=Mode::Returning)
    {
        if(frame.read!=Read::Fresh)
        {
            if(frame.quiet)
            {
                defer(state,frame.epoch);
                if(state.deferrals<2)return Outcome::Handled;
                state.mode=Mode::Returning;state.reason=Reason::Refuted;
            }
            const bool funded=state.mode==Mode::Approach && state.graph==frame.graph && state.field==frame.field &&
                state.expected==frame.pos;
            if(funded){defer(state,frame.epoch);if(state.deferrals<2)return Outcome::Handled;}
            state.mode=Mode::Returning;state.reason=Reason::Refuted;
        }
        if(state.mode!=Mode::Returning && (frame.life==Life::Alive || frame.life==Life::Respawning))
        {
            state.reason=frame.life==Life::Alive?Reason::Revalue:Reason::Refuted;
            state.mode=Mode::Returning;
        }
        else if(state.mode!=Mode::Returning && frame.life==Life::Reward)
        {
            if(frame.safe_capture)
            {
                if(!frame.needs_geometry)return Outcome::LegacyCapture; // NO Step/Hold permission is created here
                if(!frame.enemy_known || frame.enemy)return Outcome::Contact;
                // Reward excludes selected sources ONLY for this fresh input.
                // This is distinct from immutable P0 and never admits Unknown.
                const bool retained=state.mode==Mode::RewardPreparation && state.group==frame.group &&
                    state.expected==frame.pos && state.field==frame.field;
                if(state.mode==Mode::RewardPreparation && !retained)
                {state.mode=Mode::Returning;state.reason=Reason::Refuted;}
                else if(!retained)
                {
                    state.mode=Mode::RewardPreparation;state.group=frame.group;state.expected=frame.pos;
                    state.field=frame.field;state.geometry=frame.geometry;state.progress_epoch=frame.epoch;state.no_progress=0;
                }
                else if(state.progress_epoch!=frame.epoch)
                {
                    if(state.geometry!=frame.geometry && !geometry_after(frame.geometry,state.geometry))
                    {state.mode=Mode::Returning;state.reason=Reason::Refuted;}
                    state.no_progress=state.geometry==frame.geometry?state.no_progress+1:0;
                    state.geometry=frame.geometry;state.progress_epoch=frame.epoch;
                }
                if(state.mode!=Mode::Returning && state.no_progress<2)
                {state.preparation_epoch=frame.epoch;return Outcome::RewardGeometry;}
                state.reason=Reason::Refuted;state.mode=Mode::Returning;
            }
            else {state.reason=Reason::Refuted;state.mode=Mode::Returning;}
        }
        else if(state.mode==Mode::RewardPreparation)
        {state.reason=Reason::Refuted;state.mode=Mode::Returning;} // Unknown withdraws Reward-only preparation
    }
    const auto return_input=[&]() {
        if(frame.quiet){const auto result=resolved(state.reason);state.reset();return result;}
        if(!frame.enemy_known || frame.enemy)return Outcome::Contact;
        if(!backend.fresh(frame))return Outcome::Handled;
        const auto step=backend.retreat(frame);
        if(step.status==StepStatus::NeedContact)return Outcome::Contact;
        if(step.status==StepStatus::Applied)
        {
            state.expected=step.after;state.deferrals=0;state.last_defer_epoch=-1;
            if(step.quiet_after){const auto result=resolved(state.reason);state.reset();return result;}
            return Outcome::Handled;
        }
        if(step.status==StepStatus::SupportLost)
        {state.expected=step.after;return Outcome::Handled;}
        if(step.status==StepStatus::Stale)return Outcome::Handled;
        if(step.status==StepStatus::Spent || step.status==StepStatus::Refused || step.status==StepStatus::NeedCompute)
        {defer(state,frame.epoch);return state.deferrals<2?Outcome::Handled:Outcome::NeedResolver;}
        return Outcome::NeedResolver;
    };
    if(state.mode==Mode::Returning)return return_input();
    if(!frame.enemy_known || frame.enemy)return Outcome::Contact;
    // Initial admission requires actual P0. Later charged continuation requires
    // the retained same-target/model/pose owner, not the P0 handoff predicate.
    const bool matching=state.mode==Mode::Approach && state.group==frame.group &&
        state.graph==frame.graph && state.field==frame.field && state.expected==frame.pos &&
        (state.last_distance<0 || !frame.route_ready || state.last_distance==frame.distance);
    if(!matching)
    {
        if(!frame.quiet)
        {state.reason=Reason::Refuted;state.mode=Mode::Returning;return return_input();}
        state.mode=Mode::Approach;state.group=frame.group;state.graph=frame.graph;state.field=frame.field;
        state.expected=frame.pos;state.last_distance=frame.distance;state.deferrals=0;state.last_defer_epoch=-1;
    }
    if(!frame.route_ready)
    {
        if(frame.quiet)return Outcome::Geometry;
        state.mode=Mode::Returning;state.reason=Reason::Refuted;return return_input();
    }
    if(frame.distance<=0)
    {
        defer(state,frame.epoch);
        if(state.deferrals<2)return Outcome::Handled;
        state.mode=Mode::Returning;state.reason=Reason::Refuted;return return_input();
    }
    if(!backend.fresh(frame))return Outcome::Handled;
    const auto step=backend.advance(frame);
    if(step.status==StepStatus::NeedContact)return Outcome::Contact;
    if(step.status==StepStatus::Applied)
    {
        state.expected=step.after;state.last_distance=step.distance_after;
        state.deferrals=0;state.last_defer_epoch=-1;
        if(step.distance_after<0 || step.distance_after>=frame.distance)
        {state.mode=Mode::Returning;state.reason=Reason::Refuted;}
        return Outcome::Handled;
    }
    if(step.status==StepStatus::SupportLost)
    {
        // An accepted edge already consumed this input. Lost bookkeeping is
        // resolved from its actual new pose next input, never by another
        // actuator in the old frame.
        state.expected=step.after;state.mode=Mode::Returning;state.reason=Reason::Refuted;
        return Outcome::Handled;
    }
    if(step.status==StepStatus::Spent || step.status==StepStatus::NeedCompute)
    {
        defer(state,frame.epoch);
        if(state.deferrals<2)return Outcome::Handled;
    }
    state.mode=Mode::Returning;state.reason=Reason::Refuted;
    // A stale epoch cannot immediately authorize a second command. Every later
    // return input re-reads its own body/HP/point guards against the actual Trail.
    if(step.status==StepStatus::Stale)return Outcome::Handled;
    return return_input();
}
} // namespace tt::capture_service
