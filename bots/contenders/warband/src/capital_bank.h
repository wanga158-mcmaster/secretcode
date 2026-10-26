#pragma once
#include <cmath>
#include <cstdint>

// One owner-tick tribute refinement. All geometry, duration and response facts
// are native-producer premises, never inferred from bag size or elapsed time.
// The monitor preserves one receipt across at most two actual start attempts.
namespace wb::capital::bank {
struct Point { int x{}, y{}; };
inline constexpr bool operator==(Point a, Point b) { return a.x==b.x&&a.y==b.y; }
enum class Channel { Unknown, None, Submit, Other };
struct Observation {
    int owner{-1}, round{-1}; Point position{};
    float hp{}; std::uint64_t bag{}, team_score{};
    Channel channel{Channel::Unknown};
    unsigned restrictions{};
    bool observed{}, fresh{}, alive{}, stunned{}, submit_allowed{};
};
struct Context {
    int owner{-1}, round{-1}; Point altar{};
    unsigned queried_ticks{}, foe_opportunities_upper{};
    bool fresh{}, altar_known{}, adjacent{}, altar_unlocked{};
    bool foes_covered{}, npc_covered{}, active_effects_covered{};
    // Includes NPCs and active effects even when foe_opportunities_upper==0.
    bool survives_horizon{}, no_stun_horizon{}, full_reply_horizon{};
};
enum class Status { Admitted, NeedModel, Unsafe, Ineligible, RetryBlocked };
struct Plan { Status status{Status::Ineligible}; Observation before{}; Context context{}; };
enum class State { Open, Active, Blocked, Completed };
enum class Outcome {
    NoChange, NewReceipt, StartRejected, AwaitOwnerStart, Banked, Interrupted,
    Dead, Stunned, Locked, Unresolved, Expired, NeedObservation, WrongOwner,
    Stale, RetryBlocked
};
struct Result {
    Outcome outcome{Outcome::NoChange};
    std::uint64_t credited_prior_bag{}, unattributed_score_delta{};
    bool bag_preserved{};
};
enum class Change { None, ActualStep, RelevantOwnWard, ObservedUnlock, ActualSupport };
namespace detail {
inline bool valid(const Observation& o) {
    return o.observed&&o.fresh&&o.owner>=0&&o.round>=0&&o.round<1000000000&&std::isfinite(o.hp)&&o.hp>=0
        &&static_cast<unsigned>(o.channel)<=static_cast<unsigned>(Channel::Other)
        &&(!o.alive||(o.hp>0&&o.position.x>=0&&o.position.y>=0));
}
inline bool current_context(const Observation& o,const Context& c) {
    return c.fresh&&c.owner==o.owner&&c.round==o.round;
}
inline bool same(const Observation& a,const Observation& b) {
    return a.owner==b.owner&&a.round==b.round&&a.position==b.position&&a.hp==b.hp
        &&a.bag==b.bag&&a.team_score==b.team_score&&a.channel==b.channel
        &&a.restrictions==b.restrictions&&a.observed==b.observed&&a.fresh==b.fresh
        &&a.alive==b.alive&&a.stunned==b.stunned&&a.submit_allowed==b.submit_allowed;
}
inline bool same(const Context& a,const Context& b) {
    return a.owner==b.owner&&a.round==b.round&&a.altar==b.altar
        &&a.queried_ticks==b.queried_ticks&&a.foe_opportunities_upper==b.foe_opportunities_upper
        &&a.fresh==b.fresh&&a.altar_known==b.altar_known&&a.adjacent==b.adjacent
        &&a.altar_unlocked==b.altar_unlocked&&a.foes_covered==b.foes_covered
        &&a.npc_covered==b.npc_covered&&a.active_effects_covered==b.active_effects_covered
        &&a.survives_horizon==b.survives_horizon&&a.no_stun_horizon==b.no_stun_horizon
        &&a.full_reply_horizon==b.full_reply_horizon;
}
}
inline Plan admit(const Observation& o,const Context& c) {
    Plan p;p.before=o;p.context=c;
    if(!detail::valid(o)||!detail::current_context(o,c)||!c.altar_known) {
        p.status=Status::NeedModel;return p;
    }
    if(!o.alive||o.stunned||o.restrictions||!o.submit_allowed||!o.bag
        ||o.channel!=Channel::None||!c.adjacent||!c.altar_unlocked||c.queried_ticks!=1) {
        p.status=Status::Ineligible;return p;
    }
    if(!c.foes_covered||!c.npc_covered||!c.active_effects_covered
        ||(c.foe_opportunities_upper&&!c.full_reply_horizon)) {
        p.status=Status::NeedModel;return p;
    }
    p.status=c.survives_horizon&&c.no_stun_horizon?Status::Admitted:Status::Unsafe;
    return p;
}

class Monitor {
    State state_{State::Open};
    Observation latest_{},started_{},failed_{};
    Context context_{},failed_context_{};
    int owner_{-1},last_round_{-1},empty_boundary_{-1};
    unsigned attempts_{};
    bool seen_{},retry_armed_{};
    Result result(Outcome why,const Observation& o) const {
        return {why,0,0,o.bag>=started_.bag};
    }
    Result block(Outcome why,const Observation& o,const Context& c) {
        state_=State::Blocked;failed_=o;failed_context_=c;retry_armed_=false;
        if(o.alive&&o.bag==0&&o.channel==Channel::None)empty_boundary_=o.round;
        return result(why,o);
    }
    bool current(const Observation& o,const Context& c) const {
        return seen_&&detail::valid(o)&&detail::current_context(o,c)
            &&detail::same(o,latest_)&&detail::same(c,context_);
    }
public:
    State state() const { return state_; }
    unsigned attempts() const { return attempts_; }
    // Observe once per fresh own input, and after accepted/rejected commands.
    // Completion is checked only at the immediately following own input. A
    // skipped observation forfeits attribution, rather than inventing a bank.
    Result observe(const Observation& o,const Context& c) {
        if(!detail::valid(o)||!detail::current_context(o,c))return result(Outcome::NeedObservation,o);
        if(owner_>=0&&o.owner!=owner_)return result(Outcome::WrongOwner,o);
        if(o.round<last_round_)return result(Outcome::Stale,o);
        owner_=o.owner;last_round_=o.round;latest_=o;context_=c;seen_=true;
        if(state_!=State::Active) {
            if((state_==State::Completed||state_==State::Blocked)&&empty_boundary_>=0
                &&o.alive&&o.bag>0&&o.round>empty_boundary_&&o.channel==Channel::None) {
                state_=State::Open;attempts_=0;retry_armed_=false;empty_boundary_=-1;
                return result(Outcome::NewReceipt,o);
            }
            if(o.alive&&o.bag==0&&o.channel==Channel::None)empty_boundary_=o.round;
            return result(Outcome::NoChange,o);
        }
        if(!o.alive)return block(Outcome::Dead,o,c);
        if(o.stunned)return block(Outcome::Stunned,o,c);
        if(!c.altar_known||o.channel==Channel::Unknown)return result(Outcome::NeedObservation,o);
        if(!(c.altar==started_context().altar)||!c.altar_unlocked)return block(Outcome::Locked,o,c);
        if(!(o.position==started_.position))return block(Outcome::Interrupted,o,c);
        if(o.round==started_.round) {
            // Damage does not cancel tribute. A capture may increase this bag;
            // it does not cancel or renew the one-tick continuation.
            if(o.channel==Channel::Submit&&o.bag>=started_.bag)return result(Outcome::AwaitOwnerStart,o);
            return block(Outcome::Interrupted,o,c);
        }
        if(o.round!=started_.round+1)return block(Outcome::Expired,o,c);
        if(o.channel!=Channel::None)return block(Outcome::Unresolved,o,c);
        if(o.bag!=0)return block(Outcome::Interrupted,o,c);
        if(o.team_score<started_.team_score||o.team_score-started_.team_score<started_.bag)
            return block(Outcome::Unresolved,o,c);
        state_=State::Completed;empty_boundary_=o.round;
        // Only the pre-start own receipt is attributed. Extra capture/ally
        // score is retained as an unattributed difference, never own credit.
        return {Outcome::Banked,started_.bag,o.team_score-started_.team_score-started_.bag,true};
    }
    Plan plan(const Observation& o,const Context& c) const {
        auto p=admit(o,c);
        if(!current(o,c)){p.status=Status::NeedModel;return p;}
        if(attempts_>=2||state_==State::Active||state_==State::Completed
            ||(state_==State::Blocked&&!retry_armed_))p.status=Status::RetryBlocked;
        return p;
    }
    // Call only after the native attempt; accepted is the actual wrapper
    // result, followed by a fresh observed channel effect in the SAME input.
    Result accepted_start(const Plan& p,bool accepted,const Observation& after,const Context& c) {
        if(p.status!=Status::Admitted||!current(p.before,p.context)
            ||plan(p.before,p.context).status!=Status::Admitted)return result(Outcome::RetryBlocked,after);
        ++attempts_;retry_armed_=false;empty_boundary_=-1;started_=p.before;start_context_=p.context;
        if(!detail::valid(after)||!detail::current_context(after,c)
            ||after.owner!=p.before.owner||after.round!=p.before.round)
            return block(Outcome::NeedObservation,p.before,p.context);
        latest_=after;context_=c;last_round_=after.round;
        if(!accepted||!after.alive||after.stunned||!(after.position==p.before.position)
            ||after.channel!=Channel::Submit||after.bag<p.before.bag
            ||!c.altar_known||!(c.altar==p.context.altar)||!c.altar_unlocked)
            return block(Outcome::StartRejected,after,c);
        state_=State::Active;return result(Outcome::AwaitOwnerStart,after);
    }
    // Producer-attested actual changes, not a clock tick or empty vision.
    // A change permits at most the second attempt; it never resets attempts.
    bool changed_context(const Observation& o,const Context& c,Change change) {
        if(!current(o,c)||state_!=State::Blocked||attempts_>=2||!o.alive
            ||o.channel!=Channel::None||change==Change::None)return false;
        switch(change) {
        case Change::ActualStep:if(o.position==failed_.position)return false;break;
        case Change::ObservedUnlock:
            if(failed_context_.altar_unlocked||!c.altar_known||!c.altar_unlocked)return false;break;
        case Change::RelevantOwnWard:case Change::ActualSupport:break;
        default:return false;
        }
        retry_armed_=true;return true;
    }
private:
    Context start_context_{};
    const Context& started_context() const {return start_context_;}
};
} // namespace wb::capital::bank
