#pragma once
#include <cmath>

// Failed-recall history, not a hidden-threat safety proof. The SDK producer
// supplies actual accepted commands and fresh observations for ONE body. Empty
// vision, elapsed rounds, changed counters, and lower HP never rearm a failure.
// Blocking recall leaves route/observe/combat operators to the caller.
namespace wb::capital::channel {
struct Position { int x{}, y{}; };
inline constexpr bool operator==(Position a, Position b) { return a.x == b.x && a.y == b.y; }
inline constexpr bool operator!=(Position a, Position b) { return !(a == b); }
enum class Channel { Unknown, None, Recall, Other };
enum class State { Open, Active, Blocked };
enum class Outcome {
    NoChange, Pending, AwaitEffect, ArrivedAtSpawn, SpawnRecovery, Interrupted,
    DamageInterruption, StunInterruption, Dead, NeedObservation, Stale, WrongOwner,
    UntrackedChannel
};
enum class Change { None, ActualStep, RelevantOwnWard, ObservedThreatResolution };
struct Observation {
    int owner{-1}, round{-1};
    bool observed{}, alive{};
    Position position{};
    float hp{}, shield{};
    Channel channel{Channel::Unknown};
    bool at_spawn{}, stunned{};
};

class Monitor {
    State state_{State::Open};
    Observation latest_{}, started_{};
    Position failed_position_{};
    int owner_{-1}, last_round_{-1}, start_round_{-1}, cancel_round_{-1};
    bool seen_{}, effect_seen_{};

    static bool valid(const Observation& o) {
        return o.observed && o.owner >= 0 && o.round >= 0
            && std::isfinite(o.hp) && o.hp >= 0
            && std::isfinite(o.shield) && o.shield >= 0
            && (!o.alive || (o.hp > 0 && o.position.x >= 0 && o.position.y >= 0));
    }
    bool current(const Observation& o) const {
        return valid(o) && seen_ && o.owner == owner_ && o.round == last_round_
            && o.position == latest_.position && o.alive == latest_.alive
            && o.hp == latest_.hp && o.shield == latest_.shield
            && o.channel == latest_.channel && o.at_spawn == latest_.at_spawn
            && o.stunned == latest_.stunned;
    }
    Outcome block(const Observation& o, Outcome why) {
        state_ = State::Blocked; failed_position_ = o.position; return why;
    }
public:
    State state() const { return state_; }
    // No input-generation reset is allowed to erase this actor's history.
    Outcome observe(const Observation& o) {
        if (!valid(o)) return Outcome::NeedObservation;
        if (owner_ >= 0 && o.owner != owner_) return Outcome::WrongOwner;
        if (o.round < last_round_) return Outcome::Stale;
        owner_ = o.owner; last_round_ = o.round; latest_ = o; seen_ = true;
        if (!o.alive) return block(o, Outcome::Dead);
        if (o.channel == Channel::Unknown) return Outcome::NeedObservation;
        if (o.at_spawn && o.channel == Channel::None) {
            const auto before = state_; state_ = State::Open;
            // This is observed arrival/recovery presence, never a full-HP claim.
            if (before == State::Active) return Outcome::ArrivedAtSpawn;
            if (before == State::Blocked) return Outcome::SpawnRecovery;
            return Outcome::NoChange;
        }
        if (state_ == State::Active) {
            if (o.channel == Channel::Recall) { effect_seen_ = true; return Outcome::Pending; }
            // A pre-command packet can be re-read before an accepted start's
            // effect. It is not an interruption merely because its round is equal.
            // Shield loss may include expiry: this is a resource-loss witness,
            // not attribution of the channel interruption to a particular hit.
            const bool damage = o.hp < started_.hp || o.shield < started_.shield;
            if (!effect_seen_ && o.round == start_round_ && o.channel == Channel::None && !o.stunned && !damage)
                return Outcome::AwaitEffect;
            return block(o, o.stunned ? Outcome::StunInterruption
                : damage ? Outcome::DamageInterruption : Outcome::Interrupted);
        }
        if (o.channel == Channel::Recall) return block(o, Outcome::UntrackedChannel);
        return Outcome::NoChange;
    }
    bool can_start(const Observation& o, bool fresh_priced_quiet) const {
        return current(o) && state_ == State::Open && fresh_priced_quiet && o.alive
            && !o.at_spawn && !o.stunned && o.channel == Channel::None
            && start_round_ != o.round;
    }
    // Invoke ONLY after the native recall wrapper actually accepts the start.
    // The packet is its pre-command packet; accepted is not yet channel effect.
    bool accepted_start(const Observation& o, bool fresh_priced_quiet) {
        if (!can_start(o, fresh_priced_quiet)) return false;
        started_ = o; start_round_ = o.round; effect_seen_ = false; state_ = State::Active; return true;
    }
    // Invoke ONLY after actual native cancellation. Cancelling does not itself
    // justify another start: it poisons the same endpoint until a real change.
    bool accepted_cancel(const Observation& o) {
        if (!current(o) || state_ != State::Active || !o.alive
            || o.channel != Channel::Recall || cancel_round_ == o.round) return false;
        cancel_round_ = o.round; block(o, Outcome::Interrupted); return true;
    }
    // These are producer-attested EVENTS, not permissions inferred from a
    // timestamp or a ward cooldown. OwnWard must actually reveal information
    // relevant to the failed premise; threat resolution must actually resolve
    // the remembered source. Empty vision alone is neither kind of event.
    bool changed_context(const Observation& o, Change event) {
        if (!current(o) || state_ != State::Blocked || !o.alive
            || o.channel != Channel::None || event == Change::None) return false;
        if (event != Change::ActualStep && event != Change::RelevantOwnWard
            && event != Change::ObservedThreatResolution) return false;
        if (event == Change::ActualStep && o.position == failed_position_) return false;
        state_ = State::Open; return true;
    }
};
} // namespace wb::capital::channel
