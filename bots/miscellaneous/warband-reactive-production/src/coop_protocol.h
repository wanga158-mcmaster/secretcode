#pragma once
// Communication for one limited optional party family, not a job/value/safety
// oracle. The caller supplies its OWN actual consent and finite operator bound.
// Every damaging action still needs fresh SDK legality and per-body safety checks.
#include <cstdint>
#include <optional>

namespace wb::protocol {

inline constexpr int supported_round_limit = 2000;
inline constexpr int actors = 4;
inline constexpr int jobs = 31;

struct Record {
    int round{-1};
    int job{}; // 0 empty; 1..31 are a fixed map dictionary, not arbitrary IDs.
    bool valid{false};
};

inline constexpr std::optional<std::uint16_t> encode(int round, int job) {
    if (round < 0 || round > supported_round_limit || job < 0 || job > jobs) return {};
    return static_cast<std::uint16_t>((round << 5) | job);
}

inline constexpr Record decode(std::uint16_t word) {
    const int round = word >> 5;
    return {round, word & 31, round <= supported_round_limit};
}

inline constexpr bool fresh(Record record, int round) {
    return record.valid && record.round >= 0 && record.round <= supported_round_limit
        && record.job >= 0 && record.job <= jobs && record.round <= round
        && round - record.round <= 1;
}

inline constexpr bool assigned_job(int slot, int job) {
    // Fixed named pairs (0,1)/(2,3); odd/even dictionary codes have one party
    // proposer. This does not claim exclusive ownership of ordinary camp work.
    return slot >= 0 && slot < actors && job > 0 && job <= jobs
        && (job - 1) % 2 == slot / 2;
}

enum class Stage {
    reset, idle, offer_wait, acceptance_wait, confirmation_wait,
    post_ack_wait, leader_receipt_wait, active
};

enum class Disposition {
    disabled, invalid_input, independent, resetting,
    offered, accepted, confirmed, acknowledged, assembling,
    leader_ready, support_ready, working, released, expired, cancelled
};

struct OwnerWrite {
    int slot{};
    std::uint16_t word{};
    constexpr int byte_offset() const { return 2 * slot; }
};

struct Decision {
    std::optional<OwnerWrite> write{};
    Disposition disposition{Disposition::independent};
};

struct Input {
    int round{};
    int round_limit{supported_round_limit};
    Record peer{}; // Latest decoded read of the fixed peer's slot on this input.
    int proposed_job{}; // Leader only, and only in idle. Solo work is always empty.
    bool local_consent{false}; // For THIS same fixed operator/generation, never inferred.
    int operator_deadline{-1}; // Own admitted absolute bound; required to accept.
    // Available means eligible for reservation (not channeling/recalling/etc.),
    // not a claim that every attack/move allowance is currently unused.
    bool own_alive{false}, own_available{false};
    bool peer_visible{false}, peer_alive{false}, peer_available{false};
    bool own_arrived{false}, peer_arrived{false};
    bool safety{false}; // Fresh whole-group/per-body prefix checks, not a word.
    bool group_awake{false}; // Actual observation, not assumed from elapsed time.
    bool release{false}; // Caller retains actual capture/delivery obligations.
};

class Machine {
    int slot_;
    Stage stage_{Stage::reset};
    int last_round_{-1}, reset_round_{-1}, zero_seen_{-1}, barrier_ack_{-1};
    int epoch_{-1}, job_{}, deadline_{-1}, arrival_witness_{-1};

    bool leader() const { return slot_ % 2 == 0; }
    Decision emit(int round, int job, Disposition disposition) const {
        const auto word = encode(round,job);
        if (!word || slot_ < 0 || slot_ >= actors) return {{},disposition};
        return {OwnerWrite{slot_,*word},disposition};
    }
    Decision hold(Disposition disposition) const { return {{},disposition}; }
    Decision reset(int round, Disposition disposition) {
        stage_=Stage::reset; reset_round_=round; zero_seen_=-1; barrier_ack_=-1;
        epoch_=-1; job_=0; deadline_=-1; arrival_witness_=-1;
        return emit(round,0,disposition);
    }
    bool physical_peer(const Input& input) const {
        return input.peer_visible && input.peer_alive && input.peer_available;
    }
    bool same_fresh(const Input& input) const {
        return fresh(input.peer,input.round) && input.peer.job==job_;
    }
    bool accepts_bound(const Input& input, int epoch) const {
        return input.operator_deadline >= epoch+6
            && input.operator_deadline <= input.round_limit;
    }
    void accept(int epoch, int job, int deadline) {
        epoch_=epoch;job_=job;deadline_=deadline;arrival_witness_=-1;
    }
    Decision active(const Input& input) {
        if (!same_fresh(input)) return reset(input.round,Disposition::cancelled);
        if (!input.own_arrived || !input.peer_arrived || !input.safety) {
            arrival_witness_=-1;
            return emit(input.round,job_,Disposition::assembling);
        }
        // A fresh approach/arrival word cannot act as a post-arrival owner read.
        // Conforming actors hold their assigned station until work or withdrawal.
        if (arrival_witness_<0) arrival_witness_=input.round;
        if (input.peer.round<=arrival_witness_)
            return emit(input.round,job_,Disposition::assembling);
        if (input.group_awake) return emit(input.round,job_,Disposition::working);
        return emit(input.round,job_,leader() ? Disposition::leader_ready
                                            : Disposition::support_ready);
    }

public:
    explicit Machine(int owner_slot) : slot_(owner_slot) {}
    int owner_slot() const { return slot_; }
    int peer_slot() const { return slot_>=0 && slot_<actors ? slot_^1 : -1; }
    Stage stage() const { return stage_; }
    int job() const { return job_; }
    int epoch() const { return epoch_; }
    int deadline() const { return deadline_; }

    // Readiness dispositions are conditional candidates, never SDK commands or
    // proof of the supplied predicates. Only leader_ready can nominate first
    // wake; support_ready must wait for an actual awake observation/recheck.
    // Exactly one call per increasing owner round. Input reads and the emitted
    // write must remain on that same actual input; caller discards on a clock
    // change/write failure and resets. No API can address the peer's slot.
    Decision step(const Input& input) {
        if (slot_<0 || slot_>=actors || input.round_limit<1
            || input.round_limit>supported_round_limit || input.round<0
            || input.round>input.round_limit) {
            return reset(input.round,Disposition::disabled);
        }
        if (input.round<=last_round_) return hold(Disposition::invalid_input);
        last_round_=input.round;
        if (input.release) return reset(input.round,Disposition::released);
        if (!input.own_alive || !input.own_available)
            return reset(input.round,Disposition::cancelled);

        if (stage_==Stage::reset) {
            if (reset_round_<0) return reset(input.round,Disposition::resetting);
            if (fresh(input.peer,input.round) && input.peer.job==0) {
                if (leader()) {
                    if (input.peer.round>reset_round_) {
                        barrier_ack_=input.peer.round;stage_=Stage::idle;
                    }
                } else {
                    if (zero_seen_<0) zero_seen_=input.peer.round;
                    if (input.round>zero_seen_) {
                        barrier_ack_=input.round;stage_=Stage::idle;
                    }
                }
            } else if (!leader()) zero_seen_=-1;
            return emit(input.round,0,Disposition::resetting);
        }

        if (stage_==Stage::idle) {
            if (!physical_peer(input)) return reset(input.round,Disposition::cancelled);
            if (leader()) {
                if (!fresh(input.peer,input.round) || input.peer.job!=0)
                    return reset(input.round,Disposition::cancelled);
                if (input.round<=barrier_ack_ || !input.local_consent
                    || !assigned_job(slot_,input.proposed_job)
                    || !accepts_bound(input,input.round))
                    return emit(input.round,0,Disposition::independent);
                accept(input.round,input.proposed_job,input.operator_deadline);
                stage_=Stage::offer_wait;
                return emit(input.round,job_,Disposition::offered);
            }
            if (!fresh(input.peer,input.round))
                return reset(input.round,Disposition::cancelled);
            if (input.peer.job==0) return emit(input.round,0,Disposition::independent);
            const int proposed_epoch=input.peer.round;
            if (!assigned_job(slot_,input.peer.job) || proposed_epoch<=barrier_ack_)
                return reset(input.round,Disposition::cancelled);
            if (input.round==proposed_epoch)
                return emit(input.round,0,Disposition::independent);
            if (input.round!=proposed_epoch+1 || !input.local_consent
                || !accepts_bound(input,proposed_epoch))
                return reset(input.round,Disposition::cancelled);
            accept(proposed_epoch,input.peer.job,input.operator_deadline);
            stage_=Stage::acceptance_wait;
            return emit(input.round,job_,Disposition::accepted);
        }

        if (!input.local_consent || !physical_peer(input))
            return reset(input.round,Disposition::cancelled);
        // A caller can shrink its accepted bound, never extend this epoch for free.
        if (input.operator_deadline>=0 && input.operator_deadline<deadline_)
            deadline_=input.operator_deadline;
        if (input.round>deadline_) return reset(input.round,Disposition::expired);

        if (stage_==Stage::offer_wait) {
            if (input.round<epoch_+2) return hold(Disposition::offered);
            if (input.round!=epoch_+2 || !same_fresh(input)
                || input.peer.round!=epoch_+1)
                return reset(input.round,Disposition::cancelled);
            stage_=Stage::confirmation_wait;
            return emit(input.round,job_,Disposition::confirmed);
        }
        if (stage_==Stage::acceptance_wait) {
            if (input.round<epoch_+3) return hold(Disposition::accepted);
            if (input.round!=epoch_+3 || !same_fresh(input)
                || input.peer.round!=epoch_+2)
                return reset(input.round,Disposition::cancelled);
            stage_=Stage::post_ack_wait;
            return emit(input.round,job_,Disposition::acknowledged);
        }
        if (stage_==Stage::confirmation_wait) {
            if (input.round<epoch_+4) return hold(Disposition::confirmed);
            if (input.round!=epoch_+4 || !same_fresh(input)
                || input.peer.round<epoch_+3)
                return reset(input.round,Disposition::cancelled);
            stage_=Stage::leader_receipt_wait;
            return emit(input.round,job_,Disposition::acknowledged);
        }
        if (stage_==Stage::leader_receipt_wait) {
            if (input.round>epoch_+6) return reset(input.round,Disposition::cancelled);
            if (same_fresh(input) && input.peer.round>=epoch_+4) {
                stage_=Stage::active;
                return active(input);
            }
            // The follower may have acted before our p+4 receipt and frozen its
            // p+3 ACK. Renew receipt once, never treating that ACK as readiness.
            if (input.round==epoch_+5 && input.peer.valid
                && input.peer.job==job_ && input.peer.round==epoch_+3)
                return emit(input.round,job_,Disposition::acknowledged);
            return reset(input.round,Disposition::cancelled);
        }
        if (stage_==Stage::post_ack_wait) {
            if (input.round>epoch_+5) return reset(input.round,Disposition::cancelled);
            if (same_fresh(input) && input.peer.round>=epoch_+4) {
                stage_=Stage::active;
                return active(input);
            }
            // At p+4 the leader may not yet have its turn. Keep the ACK frozen;
            // the known p+2 confirmation is not fresh consent or readiness.
            if (input.round==epoch_+4 && input.peer.valid
                && input.peer.job==job_ && input.peer.round==epoch_+2)
                return hold(Disposition::acknowledged);
            return reset(input.round,Disposition::cancelled);
        }
        return active(input);
    }
};

} // namespace wb::protocol
