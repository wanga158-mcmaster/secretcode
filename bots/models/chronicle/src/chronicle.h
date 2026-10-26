#pragma once

#include "../../common/pure.h"

// Model 09: Chronicle card (bots/models/README.md; archived MODELS-09-16.md). This ledger
// accepts actor-local evidence; it performs no SDK query and grants no stock.
namespace plan4::chronicle {
inline constexpr int max_members = 32;

struct MemberObservation {
    int member_id = -1;
    Point spawn{};
    float health = 0;
    Provenance provenance{};
};
struct GroupMembers {
    int group_id = -1, member_count = 0;
    const MemberObservation* samples = nullptr;
    int sample_count = 0;
    bool all_members_visible = false;
};
struct CaptureReceipt {
    int group_id = -1, round = -1, respawn_rounds = -1;
    Provenance provenance{};
    bool personally_witnessed = false;
};
struct History {
    bool present = false;
    Lifecycle last_lifecycle = Lifecycle::unknown;
    Provenance lifecycle_provenance{};
    int health_count = 0, declared_members = 0;
    bool exact_health_vector = false, member_overflow = false;
    bool occupancy_known = false, occupied_spawn = false, contested = false;
    int capture_started_round = -1;
    int capture_receipt_round = -1, earliest_ready_round = -1;
    Provenance readiness_provenance{};
};

class State {
    // Trivial backing storage leaves unused member/record capacity untouched.
    // State is persistent actor-owned heap storage, not a per-forecast local.
    struct Member {
        int id, x, y, round;
        float health;
        Source source;
        std::uint64_t epoch;
    };
    struct Record {
        int id, public_respawn, lifecycle_round;
        Lifecycle lifecycle;
        Source lifecycle_source;
        std::uint64_t layout_epoch, lifecycle_epoch, occupancy_epoch;
        int health_count, declared_members;
        std::uint64_t vector_epoch;
        bool member_overflow, occupied_spawn, contested;
        int capture_started, receipt_round, earliest_ready;
        Source receipt_source;
        std::array<Member, max_members> members;
    };
    static_assert(std::is_trivially_default_constructible<Record>::value);
    std::array<Record, max_groups> records_;
    std::array<unsigned short, 512> index_{}; // slot + 1; IDs need not be dense
    int count_ = 0, actor_ = -1, round_ = -1;
    std::uint64_t epoch_ = 0;
    bool enabled_ = true, truncated_ = false;

    static bool legal_source(Source source) {
        return source == Source::personal_vision || source == Source::own_occupancy;
    }
    static bool exact(const Provenance& p, int round) {
        return p.status == Status::exact_now && p.observed_round == round && legal_source(p.source);
    }
    static unsigned bucket(int id) {
        std::uint32_t value = std::uint32_t(id);
        value ^= value >> 16; value *= 0x7feb352dU;
        return (value ^ (value >> 15)) & 511U;
    }
    int find(int id) const {
        if (id < 0) return -1;
        unsigned slot = bucket(id);
        for (unsigned probes = 0; probes < index_.size(); ++probes, slot = (slot + 1) & 511U) {
            if (!index_[slot]) return -1;
            const int position = int(index_[slot]) - 1;
            if (records_[position].id == id) return position;
        }
        return -1;
    }
    Record* retain(int id) {
        const int found = find(id);
        if (found >= 0) return &records_[found];
        if (id < 0 || count_ >= max_groups) { truncated_ = true; return nullptr; }
        unsigned slot = bucket(id);
        while (index_[slot]) slot = (slot + 1) & 511U;
        index_[slot] = static_cast<unsigned short>(++count_);
        auto& r = records_[count_ - 1];
        r.id = id; r.public_respawn = -1; r.lifecycle_round = -1;
        r.lifecycle = Lifecycle::unknown; r.lifecycle_source = Source::model;
        r.layout_epoch = r.lifecycle_epoch = r.occupancy_epoch = r.vector_epoch = 0;
        r.health_count = r.declared_members = 0; r.member_overflow = false;
        r.occupied_spawn = r.contested = false;
        r.capture_started = r.receipt_round = r.earliest_ready = -1;
        r.receipt_source = Source::model;
        return &r;
    }
    void discard() { count_ = 0; index_.fill(0); }
    bool begin_frame(int round, int actor_id, bool truncated) {
        ++epoch_;
        truncated_ = truncated;
        if (round < 0 || actor_id < 0) {
            discard(); actor_ = round_ = -1; truncated_ = true; return false;
        }
        if (actor_ != actor_id || round < round_ || !enabled_) discard();
        actor_ = actor_id; round_ = round;
        return true;
    }
    bool valid_groups(const Group* groups, int count) {
        if (count < 0 || count > max_groups || (count && !groups)) {
            truncated_ = true; return false;
        }
        for (int g = 0; g < count; ++g) if (groups[g].id < 0) { truncated_ = true; return false; }
        return true;
    }
    void reconcile_group(const Group& group) {
        // A partial frame supplies current legal observations only. An omitted,
        // hidden or stale row cannot refresh even public-layout fields here.
        if (!group.visible || !exact(group.provenance, round_) || group.lifecycle == Lifecycle::unknown) return;
        auto* r = retain(group.id); if (!r) return;
        const int previous_respawn = r->public_respawn;
        r->layout_epoch = epoch_; r->public_respawn = group.respawn_rounds;
        // Walking away can take several still-visible turns. A compatible
        // observation of the same waiting cycle preserves its lower bound;
        // once that bound has elapsed, another waiting state could instead
        // be a new cycle and cannot inherit the old capture receipt.
        const bool same_waiting = r->lifecycle == group.lifecycle &&
            (group.lifecycle == Lifecycle::captured || group.lifecycle == Lifecycle::respawning);
        const bool compatible_countdown = group.respawn_remaining < 0 ||
            std::int64_t(round_) + group.respawn_remaining >= r->earliest_ready;
        const bool retain_receipt = r->receipt_round >= 0 && round_ < r->earliest_ready
            && same_waiting && compatible_countdown && previous_respawn == group.respawn_rounds;
        if (r->lifecycle != group.lifecycle) {
            r->health_count = 0; r->vector_epoch = 0;
        }
        r->lifecycle = group.lifecycle; r->lifecycle_round = round_;
        r->lifecycle_source = group.provenance.source; r->lifecycle_epoch = epoch_;
        if (!retain_receipt) r->receipt_round = r->earliest_ready = -1;
        if (group.all_spawns_visible && group.occupancy_known) {
            r->occupancy_epoch = epoch_; r->occupied_spawn = group.occupied_spawn;
            r->contested = group.contested; r->capture_started = group.capture_started_round;
        }
    }
    bool budget(const Information& information) {
        if (!information.work_budget.can_refine || information.work_budget.can_refine(information.work_budget.context)) return true;
        truncated_ = true; return false;
    }
    static bool valid_member(const MemberObservation& sample, int round) {
        return sample.member_id >= 0 && std::isfinite(sample.health) && sample.health >= 0
            && exact(sample.provenance, round);
    }
    void members(const Information& information, const GroupMembers& observation) {
        const int found = find(observation.group_id);
        if (found < 0) return;
        auto& r = records_[found];
        if (r.layout_epoch != epoch_ || r.lifecycle_epoch != epoch_
            || observation.member_count < 0 || observation.sample_count < 0
            || (observation.sample_count && !observation.samples)) return;
        r.declared_members = observation.member_count;
        r.member_overflow = observation.member_count > max_members || observation.sample_count > max_members;
        const int samples = std::min(observation.sample_count, max_members);
        bool complete = observation.all_members_visible && !r.member_overflow
            && observation.sample_count == observation.member_count;
        for (int n = 0; n < samples && complete; ++n) {
            complete = valid_member(observation.samples[n], information.round);
            for (int prior = 0; prior < n && complete; ++prior)
                complete = observation.samples[prior].member_id != observation.samples[n].member_id;
        }
        if (complete) { r.health_count = 0; r.vector_epoch = 0; }
        for (int n = 0; n < samples; ++n) {
            if (!budget(information)) return;
            const auto& sample = observation.samples[n];
            if (!valid_member(sample, information.round)) continue;
            int member = 0;
            while (member < r.health_count && r.members[member].id != sample.member_id) ++member;
            if (member >= max_members) { r.member_overflow = true; continue; }
            if (member == r.health_count) ++r.health_count;
            r.members[member] = {sample.member_id, sample.spawn.x, sample.spawn.y,
                sample.provenance.observed_round, sample.health, sample.provenance.source, epoch_};
        }
        // Stable public spawn order; IDs break only a duplicate-position tie.
        std::sort(r.members.begin(), r.members.begin() + r.health_count, [](const Member& a, const Member& b) {
            return a.x != b.x ? a.x < b.x : a.y != b.y ? a.y < b.y : a.id < b.id;
        });
        if (complete) r.vector_epoch = epoch_;
    }
    static int hook_wait(int group, int now, int ordinary_service, int readiness_wait,
                         bool initially_ready, const Information& information, const void* model, void*) {
        return static_cast<const State*>(model)->supply_wait(group, now, ordinary_service,
            readiness_wait, initially_ready, information);
    }

public:
    explicit State(bool enabled = true) : enabled_(enabled) {}
    bool enabled() const { return enabled_; }
    void enabled(bool value) { if (value != enabled_) { enabled_ = value; discard(); } }
    bool truncated() const { return truncated_; }
    int retained_groups() const { return count_; }

    // A bounded current lifecycle prefix, including a valid empty frame, is
    // sufficient on non-planning turns. Nothing fills the omitted groups.
    bool observe_lifecycle_frame(int round, int actor_id, const Group* groups, int count, bool truncated = false) {
        if (!begin_frame(round, actor_id, truncated) || !valid_groups(groups, count)) return false;
        for (int g = 0; g < count; ++g) reconcile_group(groups[g]);
        return true;
    }
    void observe(const Information& information, const GroupMembers* health = nullptr, int health_count = 0) {
        if (!begin_frame(information.round, information.own.id, information.observation_truncated)
            || !valid_groups(information.groups.data(), information.group_count)) return;
        for (int g = 0; g < information.group_count; ++g) {
            if (!budget(information)) return;
            reconcile_group(information.groups[g]);
        }
        for (int n = 0; health && n < std::max(0, std::min(health_count, max_groups)); ++n) {
            if (!budget(information)) return;
            members(information, health[n]);
        }
    }
    // A receipt must identify an actually witnessed capture in the current
    // actor-local frame and use the public duration already in that frame.
    bool legal_action_receipt(const CaptureReceipt& receipt) {
        if (!enabled_ || !receipt.personally_witnessed || receipt.round != round_
            || receipt.respawn_rounds < 0 || !exact(receipt.provenance, round_)) return false;
        const int found = find(receipt.group_id);
        if (found < 0) return false;
        auto& r = records_[found];
        if (r.layout_epoch != epoch_ || r.lifecycle_epoch != epoch_
            || (r.lifecycle != Lifecycle::captured && r.lifecycle != Lifecycle::respawning)
            || r.public_respawn != receipt.respawn_rounds) return false;
        r.receipt_round = receipt.round; r.receipt_source = receipt.provenance.source;
        const auto bound = std::int64_t(receipt.round) + receipt.respawn_rounds;
        r.earliest_ready = int(std::min(bound, std::int64_t(std::numeric_limits<int>::max())));
        return true;
    }
    History history(int group_id) const {
        History out;
        const int found = find(group_id); if (found < 0) return out;
        const auto& r = records_[found]; out.present = true;
        out.last_lifecycle = r.lifecycle;
        out.lifecycle_provenance = {r.lifecycle_round, r.lifecycle_source,
            r.lifecycle_round < 0 ? Status::unknown : r.lifecycle_epoch == epoch_ ? Status::exact_now : Status::derived};
        out.health_count = r.health_count; out.declared_members = r.declared_members;
        out.exact_health_vector = r.vector_epoch == epoch_ && !r.member_overflow;
        out.member_overflow = r.member_overflow;
        out.occupancy_known = r.occupancy_epoch == epoch_;
        if (out.occupancy_known) {
            out.occupied_spawn = r.occupied_spawn; out.contested = r.contested;
            out.capture_started_round = r.capture_started;
        }
        out.capture_receipt_round = r.receipt_round; out.earliest_ready_round = r.earliest_ready;
        out.readiness_provenance = {r.receipt_round, r.receipt_source,
            r.earliest_ready >= 0 ? Status::derived : Status::unknown};
        return out;
    }
    bool member_health(int group_id, int ordinal, MemberObservation& out) const {
        const int found = find(group_id);
        if (found < 0 || ordinal < 0 || ordinal >= records_[found].health_count) return false;
        const auto& sample = records_[found].members[ordinal];
        out = {sample.id, {sample.x, sample.y}, sample.health,
            {sample.round, sample.source, sample.epoch == epoch_ ? Status::exact_now : Status::derived}};
        return true;
    }
    int supply_wait(int group, int now, int ordinary_service, int readiness_wait,
                    bool initially_ready, const Information& information) const {
        (void)ordinary_service; (void)readiness_wait;
        if (!enabled_ || initially_ready || information.own.id != actor_
            || group < 0 || group >= std::min(max_groups, information.group_count)) return 0;
        const Group& current = information.groups[group];
        if (current.visible && exact(current.provenance, information.round)
            && current.lifecycle != Lifecycle::unknown) return 0;
        const int found = find(current.id);
        if (found < 0 || records_[found].receipt_round > information.round) return 0;
        return int(std::min(std::int64_t(std::numeric_limits<int>::max()),
            std::max(std::int64_t(0), std::int64_t(records_[found].earliest_ready) - now)));
    }
    ForecastHooks hooks() const {
        ForecastHooks out; out.model = this; out.supply_wait = hook_wait;
        return out;
    }
    void predict_into(const Information& information, const Job& job, const Menu& menu, Forecast& out) const {
        plan4::predict_into(information, job, menu, out, hooks());
    }
};
} // namespace plan4::chronicle
