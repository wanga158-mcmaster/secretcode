#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>

// Receipt/channel bookkeeping only. Prepared words and ModelExpected are NOT
// safety admission, native permission, value weights or authority to wait.
// The producer supplies fresh actual reads, queried durations and actual host
// results. No bag-to-duration formula or future observation is used here.
namespace wb::capital::tribute {
inline constexpr unsigned max_ticks=10000;
inline constexpr int max_round=1000000000;
struct Point {int x=0,y=0;};
inline bool operator==(Point a,Point b){return a.x==b.x&&a.y==b.y;}
enum class Channel {Unknown,None,Submit,Other};
enum class State {Empty,Tracking,Active,Blocked,Completed};
enum class Presence {Unknown,Held,Lost,Credited};
enum class Evidence {None,ModeledExpected,ConservationSupported,ObservedPayout};
enum class Action {None,Approach,Start};
enum class Change {None,ActualStep,ObservedUnlock};
enum class Outcome {
    NoChange,NewReceipt,Prepared,PendingWord,PendingChannel,Approached,MoveRefused,
    EndpointChanged,StartRejected,Interrupted,Dead,Stunned,Locked,Unknown,
    TimerMismatch,BagMismatch,ConservationPayout,ObservedPayout,Duplicate,
    NeedObservation,WrongOwner,Stale,WrongInput,Invalid,RetryBlocked,Rearmed,Conflict
};
struct Observation {
    int owner=-1,round=-1;std::uint64_t input=0,sample=0;
    Point position{};std::uint64_t bag=0,team_score=0;
    Channel channel=Channel::Unknown;unsigned remaining_ticks=0;
    bool observed=false,fresh=false,alive=false,stunned=false,remaining_known=false;
    bool submit_started=false,submit_cancelled=false,submit_permitted=false;
    // Actual post-attempt allowances are retained, never repaired by bookkeeping.
    bool can_move=false,can_attack=false,can_cast=false;
};
struct Context {
    int owner=-1,round=-1;std::uint64_t input=0,sample=0;Point altar{};
    unsigned queried_ticks=0;int planned_horizon=-1;
    bool fresh=false,altar_known=false,adjacent=false,unlocked=false;
    // Actual producer premises, not inferred from HP, elapsed time or a constant:
    // minimum round gap between two alive OWN-input observations that could
    // contain a death/respawn; and all possible bag decreases are tribute/death.
    unsigned minimum_alive_cycle_gap=0;
    bool minimum_gap_queried=false,bag_loss_model_closed=false;
};
struct Guard {
    std::uint64_t id=0,token=0;int owner=-1,round=-1;
    std::uint64_t input=0,sample=0;Action action=Action::None;Point destination{};
    unsigned queried_ticks=0;int planned_horizon=-1;bool fresh=false;
};
struct Plan {
    bool prepared=false;std::uint64_t serial=0,token=0;Action action=Action::None;
    Point destination{};Observation before{};Context context{};Guard guard{};
};
// Optional ACTUAL owner-attributed payout witness. Ordinary bag/team-score
// reads cannot manufacture this. A native/history producer must authenticate
// the event and its exact original start episode, including event identity.
struct PayoutEvent {
    bool observed=false;std::uint64_t id=0,token=0;int owner=-1,round=-1,start_round=-1;
    std::uint64_t input=0,start_input=0,amount=0;
};
struct Result {
    Outcome outcome=Outcome::NoChange;State state=State::Empty;
    Presence original_presence=Presence::Unknown;Evidence evidence=Evidence::None;
    std::uint64_t token=0,original_amount=0,credited_original=0;
    std::uint64_t own_payout_lower=0,observed_total=0,unattributed_score_delta=0;
    bool observed_total_known=false,new_evidence=false,attempted=false,host_accepted=false,actual_movement=false;
    int expected_round=-1;
};
namespace detail {
inline bool point(Point p){return p.x>=0&&p.y>=0&&p.x<=100&&p.y<=100;}
inline bool valid(const Observation& o) {
    return o.observed&&o.fresh&&o.owner>=0&&o.round>=0&&o.round<=max_round&&o.input&&o.sample
        &&static_cast<unsigned>(o.channel)<=static_cast<unsigned>(Channel::Other)
        &&o.remaining_ticks<=max_ticks&&(!o.alive||point(o.position));
}
inline bool same(const Observation& a,const Observation& b) {
    return a.owner==b.owner&&a.round==b.round&&a.input==b.input&&a.sample==b.sample&&a.position==b.position
        &&a.bag==b.bag&&a.team_score==b.team_score&&a.channel==b.channel&&a.remaining_ticks==b.remaining_ticks
        &&a.observed==b.observed&&a.fresh==b.fresh&&a.alive==b.alive&&a.stunned==b.stunned
        &&a.remaining_known==b.remaining_known&&a.submit_started==b.submit_started&&a.submit_cancelled==b.submit_cancelled
        &&a.submit_permitted==b.submit_permitted&&a.can_move==b.can_move&&a.can_attack==b.can_attack&&a.can_cast==b.can_cast;
}
inline bool current(const Context& c,const Observation& o) {
    return c.fresh&&c.owner==o.owner&&c.round==o.round&&c.input==o.input&&c.sample==o.sample
        &&c.minimum_alive_cycle_gap<=static_cast<unsigned>(max_round);
}
inline bool same(const Context& a,const Context& b) {
    return a.owner==b.owner&&a.round==b.round&&a.input==b.input&&a.sample==b.sample&&a.altar==b.altar
        &&a.queried_ticks==b.queried_ticks&&a.planned_horizon==b.planned_horizon&&a.fresh==b.fresh
        &&a.altar_known==b.altar_known&&a.adjacent==b.adjacent&&a.unlocked==b.unlocked
        &&a.minimum_alive_cycle_gap==b.minimum_alive_cycle_gap&&a.minimum_gap_queried==b.minimum_gap_queried
        &&a.bag_loss_model_closed==b.bag_loss_model_closed;
}
inline bool same(const Guard& a,const Guard& b) {
    return a.id==b.id&&a.token==b.token&&a.owner==b.owner&&a.round==b.round&&a.input==b.input&&a.sample==b.sample
        &&a.action==b.action&&a.destination==b.destination&&a.queried_ticks==b.queried_ticks
        &&a.planned_horizon==b.planned_horizon&&a.fresh==b.fresh;
}
inline bool same(const Plan& a,const Plan& b) {
    return a.prepared&&b.prepared&&a.serial==b.serial&&a.token==b.token&&a.action==b.action
        &&a.destination==b.destination&&same(a.before,b.before)&&same(a.context,b.context)&&same(a.guard,b.guard);
}
inline bool guard(const Guard& g,std::uint64_t token,Action action,Point destination,const Observation& o,const Context& c) {
    return g.fresh&&g.id&&g.token==token&&g.action==action&&g.destination==destination&&g.owner==o.owner
        &&g.round==o.round&&g.input==o.input&&g.sample==o.sample&&g.queried_ticks==c.queried_ticks
        &&g.planned_horizon==c.planned_horizon;
}
}
class Monitor {
    State state_=State::Empty;Presence presence_=Presence::Unknown;Evidence evidence_=Evidence::None;
    std::uint64_t token_=0,original_=0,serial_=0,attempts_=0,last_start_input_=0,last_event_=0;
    std::uint64_t held_lower_=0,score_at_start_=0,credited_=0,observed_total_=0,score_delta_=0;
    bool observed_total_known_=false,pending_=false,started_=false,continuous_=true,known_interruption_=false;
    Observation latest_{},start_{},failure_{};Context context_{},start_context_{},failure_context_{};
    Plan plan_{};unsigned ticks_=0;int due_=-1;
    Result result(Outcome why) const {
        Result r;r.outcome=why;r.state=state_;r.original_presence=presence_;r.evidence=evidence_;
        r.token=token_;r.original_amount=original_;r.own_payout_lower=observed_total_known_?observed_total_:(credited_?held_lower_:0);
        r.observed_total=observed_total_;r.observed_total_known=observed_total_known_;r.expected_round=due_;
        const auto attributed=observed_total_known_?observed_total_:credited_;
        r.unattributed_score_delta=score_delta_>=attributed?score_delta_-attributed:0;return r;
    }
    Outcome check(const Observation& o,const Context& c) const {
        if(!detail::valid(o)||!detail::current(c,o))return Outcome::NeedObservation;
        if(state_!=State::Empty&&o.owner!=latest_.owner)return Outcome::WrongOwner;
        if(state_==State::Empty)return Outcome::NoChange;
        if(o.round<latest_.round)return Outcome::Stale;
        if(o.round==latest_.round) {
            if(o.input!=latest_.input)return Outcome::WrongInput;
            if(o.sample<latest_.sample)return Outcome::Stale;
            if(o.sample==latest_.sample&&!detail::same(o,latest_))return Outcome::Conflict;
        } else if(o.input<=latest_.input)return Outcome::WrongInput;
        return Outcome::NoChange;
    }
    bool current(const Observation& o,const Context& c) const {
        return detail::valid(o)&&detail::current(c,o)&&detail::same(o,latest_)&&detail::same(c,context_);
    }
    bool continuity(const Observation& before,const Observation& after,const Context& c) const {
        if(!before.alive||!after.alive)return false;
        if(after.round==before.round)return after.input==before.input;
        return after.round==before.round+1&&c.minimum_gap_queried&&c.bag_loss_model_closed
            &&c.minimum_alive_cycle_gap>static_cast<unsigned>(after.round-before.round);
    }
    void retain(const Observation& o,const Context& c) {
        if(!o.alive){presence_=Presence::Lost;continuous_=false;}
        else if(presence_==Presence::Held&&!continuity(latest_,o,c)){presence_=Presence::Unknown;continuous_=false;}
        if(presence_==Presence::Held&&(o.bag<original_||o.bag<latest_.bag)){presence_=Presence::Unknown;continuous_=false;}
        latest_=o;context_=c;
    }
    Result block(Outcome why) {
        if(presence_==Presence::Held&&latest_.bag<original_){presence_=Presence::Unknown;continuous_=false;}
        if(started_&&(why==Outcome::Dead||why==Outcome::Stunned||why==Outcome::Locked
            ||why==Outcome::Interrupted||why==Outcome::TimerMismatch||why==Outcome::BagMismatch))known_interruption_=true;
        state_=State::Blocked;evidence_=Evidence::None;failure_=latest_;failure_context_=context_;pending_=false;
        return result(why);
    }
    bool confirmation(const Plan& p,const Observation& after,const Context& c) const {
        return pending_&&detail::same(p,plan_)&&detail::valid(after)&&detail::current(c,after)
            &&after.owner==p.before.owner&&after.round==p.before.round&&after.input==p.before.input
            &&after.sample>p.before.sample;
    }
    Result event(const PayoutEvent& e,const Observation& o) {
        if(!e.observed||!e.id||!started_||e.owner!=start_.owner||e.token!=token_||e.start_round!=start_.round
            ||e.start_input!=start_.input||e.round!=due_||e.round!=o.round||e.input!=o.input||!e.amount
            ||o.bag!=0||o.channel!=Channel::None||o.team_score<score_at_start_
            ||o.team_score-score_at_start_<e.amount)return result(Outcome::Invalid);
        if(last_event_==e.id)return result(e.amount==observed_total_?Outcome::Duplicate:Outcome::Conflict);
        if(known_interruption_)return result(Outcome::Conflict);
        if(e.amount<held_lower_||e.amount<credited_) {
            if(!credited_) {presence_=Presence::Unknown;continuous_=false;return block(Outcome::Conflict);}
            return result(Outcome::Conflict); // never downgrade permanent credit
        }
        if(observed_total_known_)return result(Outcome::Conflict);
        // An actual own payout does not recover the provenance of a token lost
        // through a death, observation gap, interruption or contradictory bag.
        const bool own=state_==State::Active&&presence_==Presence::Held&&continuous_&&o.alive&&!o.stunned
            &&!o.submit_started&&!o.submit_cancelled&&e.amount>=held_lower_;
        last_event_=e.id;observed_total_known_=true;observed_total_=e.amount;score_delta_=o.team_score-score_at_start_;
        if(!own&&presence_==Presence::Held){presence_=Presence::Unknown;continuous_=false;}
        evidence_=Evidence::ObservedPayout;state_=State::Completed;
        Result r=result(Outcome::ObservedPayout);r.new_evidence=true;
        if(own&&!credited_){credited_=original_;presence_=Presence::Credited;r=result(Outcome::ObservedPayout);r.new_evidence=true;r.credited_original=original_;}
        return r;
    }
public:
    State state() const{return state_;}
    Presence original_presence() const{return presence_;}
    const Observation& latest() const{return latest_;}
    unsigned queried_ticks_at_start() const{return ticks_;}
    int expected_round() const{return due_;}
    std::uint64_t attempts() const{return attempts_;}
    // One monitor owns ONE externally unique receipt token. New generations,
    // empty bags or repricing do not replace it. Token acquisition/uniqueness
    // across monitor instances remains the actual receipt producer's duty.
    Result begin(std::uint64_t token,const Observation& o,const Context& c) {
        if(state_!=State::Empty)return result(Outcome::RetryBlocked);
        if(!token||!detail::valid(o)||!detail::current(c,o)||!o.alive||!o.bag||o.channel!=Channel::None)
            return result(Outcome::Invalid);
        token_=token;original_=held_lower_=o.bag;latest_=o;context_=c;presence_=Presence::Held;state_=State::Tracking;
        return result(Outcome::NewReceipt);
    }
    Plan prepare(Action action,Point destination,const Observation& o,const Context& c,const Guard& guard) {
        Plan out;
        if(pending_||state_!=State::Tracking||presence_!=Presence::Held||!current(o,c)||!o.alive||o.stunned
            ||o.channel!=Channel::None||o.submit_started||o.submit_cancelled||!o.bag||!c.altar_known
            ||!detail::point(c.altar)||!detail::point(destination)||!c.queried_ticks||c.queried_ticks>max_ticks
            ||c.planned_horizon<o.round||c.planned_horizon>max_round
            ||static_cast<std::int64_t>(o.round)+c.queried_ticks>c.planned_horizon
            ||!detail::guard(guard,token_,action,destination,o,c))return out;
        if(action==Action::Approach) {if(destination==o.position)return out;}
        else if(action==Action::Start) {
            if(!c.adjacent||!c.unlocked||!o.submit_permitted||last_start_input_==o.input||!(destination==c.altar))return out;
        } else return out; // no empty-wait or implicit channel action
        out={true,++serial_,token_,action,destination,o,c,guard};plan_=out;pending_=true;return out;
    }
    Result confirm_move(const Plan& p,bool accepted,const Observation& after,const Context& c) {
        if(!confirmation(p,after,c)||p.action!=Action::Approach)return result(Outcome::Invalid);
        pending_=false;retain(after,c);Result r;
        if(!after.alive)r=block(Outcome::Dead);
        else if(presence_!=Presence::Held)r=block(Outcome::Unknown);
        else if(!accepted)r=block(Outcome::MoveRefused);
        else if(!(after.position==p.destination))r=block(Outcome::EndpointChanged);
        else if(after.stunned||after.channel!=Channel::None)r=block(Outcome::Interrupted);
        else r=result(Outcome::Approached);
        r.attempted=true;r.host_accepted=accepted;r.actual_movement=!(after.position==p.before.position)&&after.alive;return r;
    }
    Result confirm_start(const Plan& p,bool accepted,const Observation& after,const Context& c) {
        if(!confirmation(p,after,c)||p.action!=Action::Start)return result(Outcome::Invalid);
        pending_=false;if(attempts_<std::numeric_limits<std::uint64_t>::max())++attempts_;
        last_start_input_=p.before.input;retain(after,c);
        if(!accepted||!after.alive||after.stunned||presence_!=Presence::Held||!(after.position==p.before.position)
            ||after.channel!=Channel::Submit||!after.submit_started||after.submit_cancelled||!after.remaining_known
            ||after.remaining_ticks!=p.context.queried_ticks||!c.altar_known||!(c.altar==p.context.altar)||!c.unlocked) {
            auto r=block(after.alive?Outcome::StartRejected:Outcome::Dead);r.attempted=true;r.host_accepted=accepted;return r;
        }
        start_=p.before;start_context_=p.context;ticks_=p.context.queried_ticks;due_=p.before.round+static_cast<int>(ticks_);
        score_at_start_=p.before.team_score;held_lower_=std::max(held_lower_,after.bag);continuous_=started_=true;known_interruption_=false;
        state_=State::Active;evidence_=Evidence::ModeledExpected;
        auto r=result(Outcome::PendingChannel);r.attempted=true;r.host_accepted=true;return r;
    }
    Result observe(const Observation& o,const Context& c,const PayoutEvent* payout=nullptr) {
        const auto checked=check(o,c);if(checked!=Outcome::NoChange)return result(checked);
        if(state_==State::Empty)return result(Outcome::Invalid);
        if(state_==State::Completed) {
            latest_=o;context_=c;
            if(payout&&payout->observed)return event(*payout,o);
            return result(Outcome::NoChange); // later death cannot undo banked credit
        }
        if(pending_&&!detail::same(o,plan_.before)) {retain(o,c);return block(Outcome::Unknown);}
        const auto previous=latest_;const auto old_presence=presence_;
        const bool adjacent=continuity(previous,o,c);
        // A zero bag is expected AT payout; do not erase an owned token before
        // testing the channel's conservation/direct-event evidence.
        const bool due=state_==State::Active&&started_&&o.round==due_&&o.channel==Channel::None&&o.bag==0;
        if(due&&old_presence==Presence::Held&&adjacent&&o.alive){latest_=o;context_=c;}
        else retain(o,c);
        if(payout&&payout->observed) {
            if(old_presence==Presence::Held&&!adjacent){presence_=Presence::Unknown;continuous_=false;}
            return event(*payout,o);
        }
        if(state_==State::Completed)return result(Outcome::NoChange);
        if(!o.alive)return block(Outcome::Dead);
        if(state_==State::Blocked)return result(Outcome::NoChange);
        if(!adjacent||presence_!=Presence::Held)return block(Outcome::Unknown);
        if(o.team_score<previous.team_score)return block(Outcome::Unknown);
        if(state_==State::Tracking) {
            if(o.channel!=Channel::None)return block(Outcome::Interrupted);
            return result(Outcome::NoChange);
        }
        if(o.stunned)return block(Outcome::Stunned);
        if(!c.altar_known||!(c.altar==start_context_.altar)||!c.adjacent)return block(Outcome::Unknown);
        if(!c.unlocked)return block(Outcome::Locked);
        if(!(o.position==start_.position)||o.submit_cancelled)return block(Outcome::Interrupted);
        if(o.round>due_)return block(Outcome::Unknown);
        const unsigned elapsed=static_cast<unsigned>(o.round-start_.round);
        if(o.round<due_) {
            if(o.channel!=Channel::Submit)return block(Outcome::Interrupted);
            if(!o.remaining_known||o.remaining_ticks!=ticks_-elapsed)return block(Outcome::TimerMismatch);
            if(o.submit_started!=(o.round==start_.round))return block(Outcome::Interrupted);
            if(o.bag<held_lower_)return block(Outcome::BagMismatch);
            held_lower_=std::max(held_lower_,o.bag);return result(Outcome::PendingChannel);
        }
        if(o.channel!=Channel::None||o.bag!=0||o.submit_started||o.submit_cancelled)return block(Outcome::Interrupted);
        if(!continuous_||!c.minimum_gap_queried||!c.bag_loss_model_closed||c.minimum_alive_cycle_gap<=1
            ||o.team_score<score_at_start_||o.team_score-score_at_start_<held_lower_)return block(Outcome::Unknown);
        score_delta_=o.team_score-score_at_start_;credited_=original_;presence_=Presence::Credited;
        state_=State::Completed;evidence_=Evidence::ConservationSupported;
        auto r=result(Outcome::ConservationPayout);r.credited_original=original_;r.new_evidence=true;return r;
    }
    // A real, freshly observed change permits another proposal. It never
    // restores an unknown/lost token, refunds an attempt or starts a channel.
    bool rearm(Change change,const Observation& o,const Context& c) {
        if(state_!=State::Blocked||presence_!=Presence::Held||!current(o,c)||!o.alive||o.stunned
            ||o.channel!=Channel::None||pending_)return false;
        if(change==Change::ActualStep) {if(o.position==failure_.position)return false;}
        else if(change==Change::ObservedUnlock) {
            if(failure_context_.unlocked||!c.altar_known||!c.unlocked||!(c.altar==failure_context_.altar))return false;
        } else return false;
        state_=State::Tracking;return true;
    }
};
}
