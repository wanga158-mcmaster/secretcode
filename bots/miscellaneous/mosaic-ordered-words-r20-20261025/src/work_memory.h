#pragma once
// Persistent NOMINATION knowledge only. Dead is a remembered observation, not a
// safety/occupancy/retaliation bound. A clock deadline permits another actual
// observation; it never asserts respawn or reconstructs positive work.
namespace mosaic::work_memory {
enum class Knowledge { initial_unknown, alive, dead };
enum class Nomination { Work, DeferredDead, ProbeDue };

class Member {
public:
    bool observe_alive(int epoch) {
        if (!current_event(epoch) || (knowledge_ == Knowledge::dead && epoch == latest_epoch_)) return false;
        knowledge_ = Knowledge::alive; latest_epoch_ = epoch; probe_after_ = -1;
        return true;
    }
    bool observe_dead(int epoch, int probe_after) {
        if (!current_event(epoch)) return false;
        // Even a missing/past nomination deadline retains the actual Dead fact.
        // Such a deadline asks for observation now, rather than inventing work.
        const int deadline = probe_after < epoch ? epoch : probe_after;
        if (knowledge_ == Knowledge::dead && epoch == latest_epoch_) {
            if (deadline < probe_after_) probe_after_ = deadline;
        } else probe_after_ = deadline;
        knowledge_ = Knowledge::dead; latest_epoch_ = epoch;
        return true;
    }
    // Caller supplies an actually observed NEW group generation, never a
    // predicted timer expiry. Epoch-only ordering cannot distinguish two life
    // observations within one input, so Dead conservatively wins a same-epoch
    // conflict. Repeated generation notifications cannot erase that Dead fact.
    bool new_generation(int epoch) {
        if (!current_event(epoch) || epoch <= generation_epoch_ ||
            (knowledge_ == Knowledge::dead && epoch == latest_epoch_)) return false;
        knowledge_ = Knowledge::initial_unknown; latest_epoch_ = generation_epoch_ = epoch; probe_after_ = -1;
        return true;
    }
    Nomination nomination(int epoch) const {
        if (knowledge_ != Knowledge::dead) return Nomination::Work;
        return epoch >= probe_after_ ? Nomination::ProbeDue : Nomination::DeferredDead;
    }
    bool dead() const { return knowledge_ == Knowledge::dead; }
    bool due(int epoch) const { return dead() && nomination(epoch) == Nomination::ProbeDue; }
    Knowledge knowledge() const { return knowledge_; }
    int observed_epoch() const { return latest_epoch_; }
    int probe_after() const { return probe_after_; }
private:
    bool current_event(int epoch) const { return epoch >= 0 && epoch >= latest_epoch_; }
    Knowledge knowledge_{Knowledge::initial_unknown};
    int latest_epoch_{-1}, generation_epoch_{-1}, probe_after_{-1};
};
} // namespace mosaic::work_memory
