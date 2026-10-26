#pragma once
#include "evidence.h"

namespace plan4::runtime {
// An adapter supplies only this actor's current state, personally observed
// camp state and the public alive/dead flags of the complete enemy roster.
struct OwnStockBoundary {
    int actor = -1, round = -1;
    Point position{};
    unsigned bag = 0;
    bool alive = false, channel_none = false, complete = false, on_unique_spawn = false;
    Provenance own_provenance{};
    GroupBoundary group{};
    int group_gems = -1, respawn_time = -1;
    std::array<int,16> enemy_ids{};
    int enemy_count = 0;
    bool enemies_complete = false, enemies_all_alive = false;
};

class OwnAcquisitionTracker {
    enum class Phase { none, start, end };
    Phase phase_ = Phase::none;
    OwnStockBoundary last_{};
    OwnStockFrame frame_{};
    CapturedGroup capture_{};
    std::uint64_t epoch_ = 0;
    int high_round_ = -1, started_round_ = -1, finished_round_ = -1;
    bool epoch_exhausted_ = false, captured_ = false;

    static bool fresh_own(const OwnStockBoundary& b) {
        return b.complete && b.alive && b.actor >= 0 && b.round >= 0 &&
            b.position.x >= 0 && b.position.y >= 0 &&
            b.own_provenance.observed_round == b.round &&
            b.own_provenance.source == Source::own_state &&
            b.own_provenance.status == Status::exact_now;
    }
    static bool same_own(const OwnStockBoundary& a, const OwnStockBoundary& b) {
        return fresh_own(a) && fresh_own(b) && a.actor == b.actor && a.round == b.round &&
            a.position == b.position && a.bag == b.bag && a.channel_none == b.channel_none;
    }
    static bool valid_enemies(const OwnStockBoundary& b) {
        if (!b.enemies_complete || !b.enemies_all_alive || b.enemy_count <= 0 ||
            b.enemy_count > int(b.enemy_ids.size())) return false;
        for (int n = 0; n < b.enemy_count; ++n) {
            if (b.enemy_ids[n] < 0 || b.enemy_ids[n] == b.actor ||
                (n && b.enemy_ids[n - 1] >= b.enemy_ids[n])) return false;
        }
        return true;
    }
    static bool isolated_capture(const OwnStockBoundary& a, const OwnStockBoundary& b) {
        if (!a.channel_none || !b.channel_none || !a.on_unique_spawn || !b.on_unique_spawn ||
            a.position != b.position || a.group.actor != a.actor || b.group.actor != b.actor ||
            a.group.round != a.round || b.group.round != b.round ||
            a.group_gems <= 0 || a.group_gems != b.group_gems ||
            a.respawn_time <= 2 || a.respawn_time != b.respawn_time ||
            b.bag <= a.bag || b.bag - a.bag > unsigned(b.group_gems) ||
            !valid_enemies(a) || !valid_enemies(b) || a.enemy_count != b.enemy_count) return false;
        for (int n = 0; n < a.enemy_count; ++n)
            if (a.enemy_ids[n] != b.enemy_ids[n]) return false;
        return capture_between_owner_turns(a.group, b.group).supported;
    }
    void new_lineage(const OwnStockBoundary& b, bool valid) {
        frame_ = {};
        frame_.clock.round = b.round >= 0 ? b.round : -1;
        frame_.clock.own.id = b.actor >= 0 ? b.actor : -1;
        if (epoch_exhausted_ || epoch_ == std::numeric_limits<std::uint64_t>::max()) {
            epoch_exhausted_ = true;
            return; // Never reuse an epoch after counter exhaustion.
        }
        frame_.stock_epoch = ++epoch_;
        frame_.known = valid;
        if (valid) frame_.bag = b.bag;
    }
    void refresh(const OwnStockBoundary& b) {
        frame_.clock.round = b.round;
        frame_.clock.own.id = b.actor;
        frame_.bag = b.bag;
        // A historical occurrence does not itself establish present stock.
        // These rows become current only after the caller checks continuity.
        for (int n = 0; n < frame_.count; ++n)
            frame_.acquisitions[n].provenance = b.own_provenance;
    }
    void acquire(const OwnStockBoundary& a, const OwnStockBoundary& b) {
        for (int n = 0; n < frame_.count; ++n) {
            const auto& row = frame_.acquisitions[n];
            if (row.group_id == b.group.group_id && row.capture_round == b.round &&
                row.stock_epoch == frame_.stock_epoch) return;
        }
        capture_ = capture_between_owner_turns(a.group, b.group).receipt;
        capture_.origin = CaptureOrigin::isolated_own_stock;
        capture_.actor_attribution_known = true;
        capture_.actor_id = b.actor;
        capture_.stock_epoch = frame_.stock_epoch;
        capture_.bag_before = a.bag;
        capture_.bag_after = b.bag;
        capture_.received_gems = b.bag - a.bag;
        capture_.own_provenance = b.own_provenance;
        captured_ = true;
        if (frame_.count == int(frame_.acquisitions.size())) {
            frame_.acquisitions[0] = frame_.acquisitions[1];
            --frame_.count;
        }
        frame_.acquisitions[frame_.count++] = {b.group.group_id, b.round,
            frame_.stock_epoch, capture_.received_gems, b.own_provenance};
    }
public:
    void start(const OwnStockBoundary& b) {
        captured_ = false;
        const bool fresh = fresh_own(b);
        // A repeated call cannot replay the receipt, renew its provenance, or
        // become a second acquisition. Identical current stock stays usable.
        if (phase_ == Phase::start && frame_.known && same_own(last_, b)) return;
        const bool valid = fresh && b.round >= high_round_ &&
            b.round > started_round_ && b.round > finished_round_;
        if (fresh) high_round_ = std::max(high_round_, b.round);
        if (valid) started_round_ = b.round;
        const bool continuous = phase_ == Phase::end && frame_.known && valid &&
            fresh_own(last_) && last_.actor == b.actor &&
            std::int64_t(last_.round) + 1 == b.round && b.bag >= last_.bag;
        if (continuous) {
            refresh(b);
            if (isolated_capture(last_, b)) acquire(last_, b);
        } else new_lineage(b, valid);
        last_ = b;
        phase_ = frame_.known ? Phase::start : Phase::none;
    }
    void finish(const OwnStockBoundary& b) {
        captured_ = false;
        const bool fresh = fresh_own(b);
        const bool valid = fresh && b.round >= high_round_;
        if (fresh) {
            high_round_ = std::max(high_round_, b.round);
            finished_round_ = std::max(finished_round_, b.round);
        }
        if (phase_ == Phase::end && frame_.known && same_own(last_, b)) return;
        const bool matched = phase_ == Phase::start && frame_.known && valid &&
            fresh_own(last_) && last_.actor == b.actor && last_.round == b.round;
        if (matched && b.bag >= last_.bag) refresh(b);
        else new_lineage(b, valid);
        last_ = b;
        // An unmatched END (including a resumed stale turn) cannot anchor a
        // later capture. A matched bag decrease starts a fresh, empty lineage.
        phase_ = matched && frame_.known ? Phase::end : Phase::none;
    }
    const OwnStockFrame& frame() const { return frame_; }
    const CapturedGroup* capture() const { return captured_ ? &capture_ : nullptr; }
};
} // namespace plan4::runtime
