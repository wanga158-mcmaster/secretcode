#pragma once
#include <cstdint>
#include "attempt-preaction.h"

namespace plan4::attempts {

enum class Kind { bank = 0, one_camp = 1 };
enum class Origin { planned = 0, bootstrap = 1, legacy = 2 };
enum class Outcome { completed = 0, aborted = 1, replaced = 2, death = 3, observation_gap = 4, unknown = 5 };
enum class EventType { begin = 0, end = 1 };

struct Identity {
    Kind kind = Kind::bank;
    Origin origin = Origin::planned;
    std::uint64_t job_token = 0;
    int group_id = -1, altar_id = -1;
};

inline bool operator==(const Identity& a, const Identity& b) {
    return a.kind == b.kind && a.origin == b.origin && a.job_token == b.job_token &&
        a.group_id == b.group_id && a.altar_id == b.altar_id;
}

// Only explicitly supplied own observations are retained. Unknown values are
// canonicalized to -1 (zero bits for unknown health); in particular neither
// health nor bag changes imply an
// alive state, a capture, a tribute, equipment or any economic conversion.
struct Observation {
    int actor = -1, round = -1;
    int x = -1, y = -1, bag = -1;
    // Raw IEEE f32 bits retain fractional SDK health exactly. The known flags
    // distinguish an actual zero from absent data; no integer truncation.
    std::uint32_t hp_bits = 0, max_hp_bits = 0;
    bool own = false, fresh = false;
    bool alive_known = false, alive = false;
    bool position_known = false, bag_known = false;
    bool hp_known = false, max_hp_known = false;
};

struct Event {
    std::uint64_t event_id = 0, attempt_id = 0, dropped_events = 0;
    EventType type = EventType::begin;
    Identity identity{};
    Observation start{}, end{};
    PreactionEvidence preaction{};
    bool has_end = false;
    Outcome outcome = Outcome::unknown;
    // A censor can have a fresh detection round without an endpoint sample.
    int terminal_round = -1;
};

// Actor-local, SDK-free bookkeeping. Calling begin records an invoked macro;
// calling finish(completed, ...) asserts an endpoint established by the caller.
// The recorder never derives successful conversion from a resource difference.
// No destructor or game-end hook manufactures a terminal event.
class Recorder {
public:
    static constexpr unsigned capacity = 8;

private:
    Event queue_[capacity]{};
    unsigned head_ = 0, size_ = 0;
    std::uint64_t next_event_ = 0, next_attempt_ = 0, dropped_ = 0;
    Identity identity_{};
    Observation start_{};
    PreactionEvidence preaction_{};
    int actor_ = -1, last_round_ = -1;
    bool active_ = false, has_identity_ = false;
    Outcome last_outcome_ = Outcome::unknown;

    static bool valid(const Identity& key) {
        return unsigned(key.kind) <= unsigned(Kind::one_camp) &&
            unsigned(key.origin) <= unsigned(Origin::legacy) &&
            key.group_id >= -1 && key.altar_id >= -1;
    }
    static bool valid(Outcome outcome) {
        return unsigned(outcome) <= unsigned(Outcome::unknown);
    }
    bool valid(const Observation& sample) const {
        return sample.own && sample.fresh && sample.actor >= 0 && sample.round >= 0 &&
            (actor_ < 0 || sample.actor == actor_) && sample.round >= last_round_;
    }
    static Observation clean(Observation sample) {
        if (!sample.alive_known) sample.alive = false;
        if (!sample.position_known || sample.x < 0 || sample.y < 0) {
            sample.position_known = false; sample.x = sample.y = -1;
        }
        if (!sample.bag_known || sample.bag < 0) { sample.bag_known = false; sample.bag = -1; }
        // Preserve all explicitly known SDK bits, including anomalous values;
        // the recorder imposes no numeric health rule or alive/dead inference.
        if (!sample.hp_known) sample.hp_bits = 0;
        if (!sample.max_hp_known) sample.max_hp_bits = 0;
        return sample;
    }
    void emit(Event event) {
        event.event_id = ++next_event_;
        event.dropped_events = dropped_;
        if (size_ == capacity) {
            ++dropped_;
            // Mark already pending evidence too: loss remains visible even if
            // no subsequent macro emits before the game stops.
            for (unsigned n = 0; n < size_; ++n)
                queue_[(head_ + n) % capacity].dropped_events = dropped_;
            return; // Preserve the order and identity of previously queued events.
        }
        queue_[(head_ + size_) % capacity] = event;
        ++size_;
    }
    void close(Outcome outcome, const Observation& sample, bool has_end) {
        Event event;
        event.type = EventType::end; event.attempt_id = next_attempt_;
        event.identity = identity_; event.start = start_;
        event.preaction = preaction_;
        event.has_end = has_end;
        if (has_end) event.end = clean(sample);
        event.outcome = outcome; event.terminal_round = sample.round;
        active_ = false; last_outcome_ = outcome;
        emit(event);
    }
    bool observe_sample(const Observation& sample, Outcome unknown_cause = Outcome::unknown) {
        if (!valid(sample)) return false;
        const bool gap = last_round_ >= 0 && sample.round - last_round_ > 1;
        actor_ = sample.actor; last_round_ = sample.round;
        if (active_) {
            if (gap) close(Outcome::observation_gap, sample, false);
            else if (sample.alive_known && !sample.alive) close(Outcome::death, sample, true);
            // Unknown alive cannot bridge a death/respawn interval. Retain the
            // invoked start, but censor its path before a later success claim.
            else if (!sample.alive_known || !start_.alive_known) close(unknown_cause, sample, false);
        }
        return true;
    }

public:
    bool active() const { return active_; }
    // Includes the most recent terminal attempt: retrying its identity must not
    // reopen it, recollect its pre-action state or upgrade an unknown snapshot.
    bool same_attempt(const Identity& key) const { return has_identity_ && key == identity_; }
    const Identity* active_identity() const { return active_ ? &identity_ : nullptr; }
    std::uint64_t active_attempt_id() const { return active_ ? next_attempt_ : 0; }
    std::uint64_t dropped_events() const { return dropped_; }
    unsigned pending() const { return size_; }
    const Event* front() const { return size_ ? &queue_[head_] : nullptr; }
    void pop() {
        if (!size_) return;
        head_ = (head_ + 1) % capacity; --size_;
    }

    // Same-round own samples are legal: pre-action and post-action samples can
    // differ. An older round, foreign actor or nonfresh sample cannot mutate
    // the clock or close an attempt. Unknown alive censors, never means dead.
    bool observe(const Observation& sample) {
        return observe_sample(sample);
    }

    // A start with unknown body/alive state is still an invoked macro and must
    // stay in the cohort. Only a known-dead dispatch is ineligible. A terminal
    // identity cannot reopen on the next tick; a new episode needs a new token.
    bool begin(const Identity& key, const Observation& sample, const PreactionEvidence& preaction = {}) {
        if (!valid(key) || !valid(sample)) return false;
        const bool same = same_attempt(key);
        (void)observe(sample);
        if (same || (sample.alive_known && !sample.alive)) return false;
        if (active_) close(Outcome::replaced, sample, sample.alive_known);
        identity_ = key; has_identity_ = active_ = true; start_ = clean(sample);
        preaction_ = freeze_preaction(preaction, sample.actor, sample.round);
        ++next_attempt_;
        Event event;
        event.type = EventType::begin; event.attempt_id = next_attempt_;
        event.identity = identity_; event.start = start_;
        event.preaction = preaction_;
        emit(event);
        return true;
    }

    // Death/gap observation takes precedence over a caller's success claim.
    // An unknown alive endpoint censors a requested completion as unknown.
    // Explicit abort/replacement causes remain usable without a body endpoint.
    bool finish(Outcome outcome, const Observation& sample) {
        if (!active_ || !valid(outcome) || !valid(sample)) return false;
        (void)observe_sample(sample, outcome == Outcome::aborted || outcome == Outcome::replaced ? outcome : Outcome::unknown);
        if (!active_) return last_outcome_ == outcome;
        if (outcome == Outcome::death) return false; // Requires observed death.
        const Outcome actual = outcome == Outcome::completed && !sample.alive_known ? Outcome::unknown : outcome;
        const bool has_end = sample.alive_known && actual != Outcome::observation_gap && actual != Outcome::unknown;
        close(actual, sample, has_end);
        return actual == outcome;
    }
};

} // namespace plan4::attempts
