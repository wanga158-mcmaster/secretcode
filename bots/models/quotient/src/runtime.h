#pragma once
#include "quotient.h"
#include "../../common/model-runtime.h"
#include <tuple>

namespace plan4::runtime {
class Quotient : public M0 {
    static constexpr int layout_limit = 8, sample_limit = layout_limit * quotient::member_limit;
public:
    struct Evidence {
        bool layout_valid = false, layout_truncated = false, members_truncated = false;
        bool combat_current = false, snapshot_current = false, source_mismatch = false;
        int public_groups = 0, exact_members = 0, unknown_members = 0, malformed_groups = 0;
    };
private:
    quotient::Quotient model_;
    CombatKit combat_{};
    ObservationClock layout_clock_{};
    std::array<PublicGroup, layout_limit> public_groups_{};
    std::array<PublicMember, sample_limit> public_members_{};
    std::array<VisibleMember, sample_limit> observations_{};
    std::array<int, sample_limit> observation_order_{};
    std::array<std::array<quotient::Member, quotient::member_limit>, layout_limit> members_{};
    std::array<quotient::Composition, max_groups> compositions_{};
    std::array<int, layout_limit> mapped_{};
    std::array<quotient::Endpoint, max_jobs> endpoints_{};
    std::unique_ptr<Information> source_ = std::make_unique<Information>();
    std::unique_ptr<Evaluation> batch_ = std::make_unique<Evaluation>();
    Menu batch_menu_{};
    quotient::Statistics statistics_{};
    Evidence evidence_{};
    int public_group_count_ = 0, public_member_count_ = 0, observed_count_ = 0, mapped_count_ = 0;
    std::uint64_t kit_version_ = 0, batches_ = 0;
    std::uint32_t stat_epoch_ = 0;
    bool has_kit_ = false, has_snapshot_ = false, layout_valid_ = false, members_valid_ = false, batch_ready_ = false, enabled_ = true, diagnostics_ready_ = false;

    static bool same_clock(ObservationClock clock, const Information& info) {
        return clock.round == info.round && clock.own.id == info.own.id;
    }
    static bool same_provenance(Provenance a, Provenance b) {
        return std::tie(a.observed_round, a.source, a.status) == std::tie(b.observed_round, b.source, b.status);
    }
    static bool same_actor(const Actor& a, const Actor& b) {
        return std::tie(a.id, a.position.x, a.position.y, a.health, a.maximum_health, a.bag, a.gold, a.channel,
            a.channel_ticks, a.alive, a.on_spawn, a.recall_safe, a.escape_rounds, a.escape_incoming) ==
            std::tie(b.id, b.position.x, b.position.y, b.health, b.maximum_health, b.bag, b.gold, b.channel,
            b.channel_ticks, b.alive, b.on_spawn, b.recall_safe, b.escape_rounds, b.escape_incoming);
    }
    static bool same_settings(const Settings& a, const Settings& b) {
        return std::tie(a.max_rounds, a.winning_gems, a.capture_rounds, a.recall_rounds, a.tribute_multiplier,
            a.tribute_base, a.tribute_minimum, a.spawn_heal_fraction) == std::tie(b.max_rounds, b.winning_gems,
            b.capture_rounds, b.recall_rounds, b.tribute_multiplier, b.tribute_base, b.tribute_minimum, b.spawn_heal_fraction);
    }
    static bool same_group(const Group& a, const Group& b) {
        return std::tie(a.id, a.position.x, a.position.y, a.full_health, a.health, a.clear_rounds, a.full_clear_rounds,
            a.incoming_damage, a.full_incoming_damage, a.gems, a.gold, a.respawn_rounds, a.lifecycle, a.visible,
            a.all_spawns_visible, a.reward_available, a.contested, a.occupied_spawn, a.occupancy_known,
            a.capture_started_round, a.respawn_remaining, a.eligible_allied_occupants, a.own_is_lowest_occupant) ==
            std::tie(b.id, b.position.x, b.position.y, b.full_health, b.health, b.clear_rounds, b.full_clear_rounds,
            b.incoming_damage, b.full_incoming_damage, b.gems, b.gold, b.respawn_rounds, b.lifecycle, b.visible,
            b.all_spawns_visible, b.reward_available, b.contested, b.occupied_spawn, b.occupancy_known,
            b.capture_started_round, b.respawn_remaining, b.eligible_allied_occupants, b.own_is_lowest_occupant) &&
            same_provenance(a.provenance, b.provenance) && same_provenance(a.health_provenance, b.health_provenance);
    }
    static bool same_altar(const Altar& a, const Altar& b) {
        return std::tie(a.id, a.position.x, a.position.y, a.altar_position.x, a.altar_position.y, a.accessible,
            a.hostile_lock, a.lock_known, a.remove_lock_rounds) == std::tie(b.id, b.position.x, b.position.y,
            b.altar_position.x, b.altar_position.y, b.accessible, b.hostile_lock, b.lock_known, b.remove_lock_rounds);
    }
    bool same_source(const Information& info) const {
        const auto& old = *source_;
        if (!has_snapshot_ || info.group_count < 0 || info.group_count > max_groups || info.altar_count < 0 || info.altar_count > max_altars ||
            info.round != old.round || info.our_score != old.our_score || info.enemy_score != old.enemy_score ||
            info.group_count != old.group_count || info.altar_count != old.altar_count || !same_actor(info.own, old.own) ||
            !same_settings(info.settings, old.settings) || info.own_spawn != old.own_spawn ||
            info.in_progress_group_id != old.in_progress_group_id || info.retained_group_id != old.retained_group_id ||
            info.retained_second_group_id != old.retained_second_group_id || info.retained_altar_id != old.retained_altar_id ||
            info.observation_truncated != old.observation_truncated || info.public_layout_truncated != old.public_layout_truncated ||
            info.travel.distance != old.travel.distance || info.travel.context != old.travel.context ||
            info.work_budget.can_refine != old.work_budget.can_refine || info.work_budget.context != old.work_budget.context) return false;
        for (int g = 0; g < info.group_count; ++g) {
            if ((g % 16 == 0 && !quotient::budget(info)) || !same_group(info.groups[g], old.groups[g])) return false;
        }
        for (int a = 0; a < info.altar_count; ++a) if (!same_altar(info.altars[a], old.altars[a])) return false;
        return true;
    }
    static bool same_job(const Job& a, const Job& b) {
        return std::tie(a.kind, a.group, a.second_group, a.altar, a.menu_rank, a.nominal_travel, a.nominal_completion) ==
            std::tie(b.kind, b.group, b.second_group, b.altar, b.menu_rank, b.nominal_travel, b.nominal_completion);
    }
    static bool same_menu(const Menu& a, const Menu& b) {
        if (a.count < 0 || a.count > max_jobs || a.count != b.count || a.single_count != b.single_count || a.truncated != b.truncated) return false;
        for (int n = 0; n < a.count; ++n) if (!same_job(a.jobs[n], b.jobs[n])) return false;
        return true;
    }
    quotient::View view(const Information& info) {
        for (int n = 0; n < mapped_count_; ++n) compositions_[mapped_[n]] = {};
        mapped_count_ = 0;
        evidence_.public_groups = evidence_.exact_members = evidence_.unknown_members = evidence_.malformed_groups = 0;
        evidence_.source_mismatch = !same_source(info);
        evidence_.snapshot_current = !evidence_.source_mismatch;
        evidence_.combat_current = combat_.exact && has_kit_ && same_clock(combat_.clock, info) &&
            combat_.version == kit_version_ && combat_.stat_epoch == stat_epoch_;
        quotient::View result;
        if (evidence_.source_mismatch || !evidence_.combat_current || !layout_valid_ || !members_valid_ || !same_clock(layout_clock_, info)) return result;
        result.groups = compositions_.data(); result.count = info.group_count;
        result.kit = {combat_.champion, combat_.attack_range, combat_.strength, combat_.focus,
            combat_.monster_shards, combat_.jungle_shards, combat_.version};
        for (int p = 0; p < public_group_count_; ++p) {
            if (!quotient::budget(info)) return {};
            const auto& public_group = public_groups_[p];
            if (!public_group.complete) continue;
            int actual = -1;
            for (int g = 0; g < info.group_count; ++g) if (info.groups[g].id == public_group.group_id) {
                if (actual >= 0) { actual = -1; break; }
                actual = g;
            }
            if (actual < 0) continue;
            bool malformed = false;
            for (int m = 0; m < public_group.count; ++m) {
                const auto& public_member = public_members_[public_group.first + m]; auto& member = members_[p][m];
                member = {public_member.kind, public_member.spawn, public_member.maximum_health, 0, {}};
                int matches = 0;
                const auto found = std::lower_bound(observation_order_.begin(), observation_order_.begin() + observed_count_,
                    std::pair{public_group.group_id, public_member.member_id}, [&](int index, const auto& key) {
                        const auto& sample = observations_[index]; return std::pair{sample.group_id, sample.member_id} < key;
                    });
                for (auto item = found; item != observation_order_.begin() + observed_count_; ++item) {
                    const auto& sample = observations_[*item];
                    if (sample.group_id != public_group.group_id || sample.member_id != public_member.member_id) break;
                    if (!quotient::current_health(sample.provenance, info.round)) continue;
                    if (sample.ordinal != public_member.ordinal || sample.declared_members != public_group.count || sample.spawn != public_member.spawn ||
                        !std::isfinite(sample.health) || sample.health < 0 || sample.health > public_member.maximum_health || ++matches != 1) {
                        malformed = true; continue;
                    }
                    member.health = sample.health; member.health_provenance = sample.provenance;
                }
                if (matches == 1) ++evidence_.exact_members; else ++evidence_.unknown_members;
            }
            if (malformed) { ++evidence_.malformed_groups; continue; }
            compositions_[actual] = {members_[p].data(), public_group.count, true};
            mapped_[mapped_count_++] = actual; ++evidence_.public_groups;
        }
        return result;
    }
public:
    explicit Quotient(bool enabled = true) : model_(enabled), enabled_(enabled) {}
    const char* name() const { return "quotient"; }
    void observe_kit(std::uint64_t version, std::uint32_t epoch = 0) {
        diagnostics_ready_=false;
        if (!has_kit_ || kit_version_ != version || stat_epoch_ != epoch) batch_ready_ = false;
        has_kit_ = true; kit_version_ = version; stat_epoch_ = epoch;
    }
    void observe_combat(const CombatKit& combat) {
        diagnostics_ready_=false;
        batch_ready_ = false; combat_ = combat;
        combat_.exact &= combat.clock.round >= 0 && combat.clock.own.id >= 0 && combat.champion >= 0 && combat.attack_range >= 0 &&
            std::isfinite(combat.strength) && combat.strength >= 0 && std::isfinite(combat.focus) && combat.focus >= 0;
    }
    void observe_layout(const PublicLayoutFrame& frame) {
        diagnostics_ready_=false;
        batch_ready_ = false; layout_valid_ = false; public_group_count_ = public_member_count_ = 0;
        layout_clock_ = frame.clock; evidence_.layout_truncated = frame.truncated;
        if (frame.clock.round < 0 || frame.clock.own.id < 0 || frame.group_count < 0 || frame.group_count > layout_limit ||
            frame.member_count < 0 || frame.member_count > sample_limit || (frame.group_count && !frame.groups) || (frame.member_count && !frame.members)) return;
        std::array<int, sample_limit> identities;
        for (int m = 0; m < frame.member_count; ++m) identities[m] = frame.members[m].member_id;
        std::sort(identities.begin(), identities.begin() + frame.member_count);
        for (int m = 1; m < frame.member_count; ++m) if (identities[m] == identities[m - 1]) return;
        for (int p = 0; p < frame.group_count; ++p) {
            const auto& group = frame.groups[p];
            if (group.group_id < 0 || group.first < 0 || group.count < 0 || group.count > quotient::member_limit ||
                group.first > frame.member_count - group.count || (group.complete && !group.count)) return;
            for (int previous = 0; previous < p; ++previous) if (frame.groups[previous].group_id == group.group_id) return;
            std::array<bool, quotient::member_limit> ordinals{};
            std::array<Point, quotient::member_limit> spawns;
            for (int m = 0; m < group.count; ++m) {
                const auto& member = frame.members[group.first + m];
                if (member.group_id != group.group_id || member.member_id < 0 || member.ordinal < 0 || member.ordinal >= group.count ||
                    member.declared_members != group.count || member.kind < 0 || member.attack_range < 0 ||
                    !std::isfinite(member.maximum_health) || member.maximum_health <= 0) return;
                if (ordinals[member.ordinal]) return;
                ordinals[member.ordinal] = true; spawns[m] = member.spawn;
            }
            std::sort(spawns.begin(), spawns.begin() + group.count, point_less);
            for (int m = 1; m < group.count; ++m) if (spawns[m] == spawns[m - 1]) return;
        }
        public_group_count_ = frame.group_count; public_member_count_ = frame.member_count;
        if (public_group_count_) std::copy_n(frame.groups, public_group_count_, public_groups_.begin());
        if (public_member_count_) std::copy_n(frame.members, public_member_count_, public_members_.begin());
        layout_valid_ = true;
    }
    void observe_snapshot(const Information& info, MemberFrame frame = {}) {
        diagnostics_ready_=false;
        batch_ready_ = false; has_snapshot_ = true; *source_ = info;
        evidence_.members_truncated = frame.truncated;
        members_valid_ = frame.count >= 0 && frame.count <= sample_limit && (!frame.count || frame.samples);
        observed_count_ = members_valid_ ? frame.count : 0;
        if (observed_count_) std::copy_n(frame.samples, observed_count_, observations_.begin());
        for (int n = 0; n < observed_count_; ++n) observation_order_[n] = n;
        std::sort(observation_order_.begin(), observation_order_.begin() + observed_count_, [&](int a, int b) {
            return std::pair{observations_[a].group_id, observations_[a].member_id} < std::pair{observations_[b].group_id, observations_[b].member_id};
        });
    }
    void evaluate_into(const Information& info, const Menu& menu, Evaluation& out) {
        batch_ready_ = false; ++batches_;
        evidence_.layout_valid = layout_valid_ && same_clock(layout_clock_, info);
        const auto current = view(info);
        statistics_ = model_.evaluate_full(info, menu, current, out, endpoints_);
        diagnostics_ready_=true;
    }
    // Compatibility for the common per-candidate interface. Normal integration
    // uses evaluate_into once, retaining the SAME frozen menu and selector.
    void predict_into(const Information& info, const Job& job, const Menu& menu, Forecast& out) {
        if (!batch_ready_ || !same_menu(menu, batch_menu_) || !same_source(info)) {
            evaluate_into(info, menu, *batch_); batch_menu_ = menu; batch_ready_ = true;
        }
        for (int n = 0; n < menu.count && n < max_jobs; ++n) if (same_job(job, menu.jobs[n])) {
            if (n < batch_->evaluated_count) out = batch_->forecasts[n];
            else { out.reset(); out.budget_exhausted = true; out.truncated = true; }
            return;
        }
        // A job outside the frozen menu cannot borrow another row's cache.
        diagnostics_ready_=false;
        plan4::predict_into(info, job, menu, out);
    }
    const quotient::Statistics& statistics() const { return statistics_; }
    const Evidence& evidence() const { return evidence_; }
    std::uint64_t batches() const { return batches_; }
    ModelEvidence model_evidence() const {
        ModelEvidence out; out.reported=true;
        // 1 source; 2 kit/layout; 3 incomplete physical continuation; 4 budget;
        // 5 no applicable class (e.g. exact two-camp fallback). Public metadata
        // and a valid exact path do not by themselves claim class influence.
        out.reason=!diagnostics_ready_ || !evidence_.snapshot_current?1:!evidence_.combat_current || !evidence_.layout_valid?2:
            statistics_.budget_exhausted?4:statistics_.preparation_incomplete || statistics_.first_macro_only?3:
            !statistics_.classes || !statistics_.full_paths?5:0;
        out.supported=out.reason==0;
        out.coupling_enabled=out.supported && enabled_ && statistics_.rebased_paths>0;
        if (diagnostics_ready_) {
            out.samples=evidence_.exact_members;
            out.unknown=evidence_.unknown_members+evidence_.malformed_groups+statistics_.unsupported_features;
            out.paths=statistics_.rebased_paths;
            out.work=statistics_.forecasts_computed+statistics_.continuation_steps;
        }
        out.revision=batches_; return out;
    }
};
} // namespace plan4::runtime
