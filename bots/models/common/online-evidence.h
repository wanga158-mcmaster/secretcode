#pragma once
#include "evidence.h"
#include <cambc.h>
#include <vector>

#ifndef PLAN4_PRODUCER_ACTOR_PACKETS
#define PLAN4_PRODUCER_ACTOR_PACKETS 0
#endif

namespace plan4::runtime {
// A bounded, actor-local supplement to the planning snapshot. Public handles
// come from the executor's existing layout cache; live queries use this actor's
// current vision only. Truncation never proves absent consumers or hidden HP.
class OnlineEvidence {
    static constexpr int group_limit = 8, member_limit = 64;
    std::array<GroupBoundary, group_limit> previous_end_;
    std::array<int, group_limit> previous_indices_;
    int previous_count_ = 0;
    std::array<Group, group_limit> groups_;
    std::array<int, group_limit> indices_;
    int group_count_ = 0;
    std::array<VisibleMember, member_limit> members_;
    int member_count_ = 0;
    std::array<CaptureEvidence, group_limit> captures_;
    int capture_count_ = 0;
    ProducerFrame producers_;
    int round_ = -1, actor_ = -1;
    unsigned floor_ = 0;
    bool truncated_ = false, alive_ = false;

    bool budget() {
        if (cambc::fuel_remaining() >= floor_ + 1200) return true;
        truncated_ = true; return false;
    }
    static Point point(cambc::vec2 p) { return {p.x, p.y}; }
    static int tile_group(cambc::vec2 p, const std::vector<int>& lookup, int width) {
        const int index = p.y * width + p.x;
        return index >= 0 && index < int(lookup.size()) ? lookup[index] : -1;
    }
    template<class Entries>
    int query_group(int index, const Entries& entries, const cambc::bitboard& vision,
                    const std::vector<cambc::vec2>& allies, const std::vector<cambc::vec2>& enemies,
                    bool with_members) {
        for (int n = 0; n < group_count_; ++n) if (indices_[n] == index) return n;
        if (index < 0 || index >= int(entries.size())) return -1;
        if (group_count_ >= group_limit) { truncated_ = true; return -1; }
        const auto& sdk = entries[index].group;
        if (sdk.camps().size() > 32) { truncated_ = true; return -1; }
        bool any = false, all = true;
        for (const auto& member : sdk) {
            if (!budget()) return -1;
            const bool seen = vision.is_set(member.spawn_position());
            any |= seen; all &= seen;
        }
        if (!any || !budget()) return -1;
        const auto state = sdk.state();
        const int slot = group_count_++;
        indices_[slot] = index;
        auto& observed = groups_[slot]; observed = {};
        observed.id = int(static_cast<cambc::handle_camp_group_t>(sdk));
        observed.position = point(entries[index].anchor);
        observed.respawn_rounds = sdk.max_respawn_timer();
        observed.visible = true; observed.all_spawns_visible = all;
        observed.occupancy_known = all;
        observed.provenance = {round_, Source::personal_vision, Status::exact_now};
        observed.lifecycle = std::holds_alternative<cambc::map::camp_group_alive>(state) ? Lifecycle::living
            : std::holds_alternative<cambc::map::camp_group_reward_available>(state) ? Lifecycle::cleared : Lifecycle::respawning;
        if (const auto* respawn = std::get_if<cambc::map::camp_group_respawning>(&state))
            observed.respawn_remaining = respawn->timer;
        observed.reward_available = observed.lifecycle == Lifecycle::cleared;
        observed.gems = sdk.gem_value(); observed.gold = sdk.gold_value();
        // Store local occupancy in both directions. Producer classification
        // below tests opponents relative to the producer's own side.
        for (const auto& member : sdk) {
            if (!budget()) { observed.occupancy_known = false; break; }
            const auto p = member.spawn_position();
            observed.occupied_spawn |= std::find(allies.begin(), allies.end(), p) != allies.end()
                || std::find(enemies.begin(), enemies.end(), p) != enemies.end();
            observed.contested |= std::find(enemies.begin(), enemies.end(), p) != enemies.end();
        }
        if (with_members && (observed.lifecycle == Lifecycle::living || observed.lifecycle == Lifecycle::cleared)) {
            int ordinal = 0;
            for (const auto& member : sdk) {
                if (!budget()) break;
                if (!vision.is_set(member.spawn_position())) { ++ordinal; continue; }
                if (member_count_ == member_limit) { truncated_ = true; break; }
                if (!budget()) break;
                const bool dead = member.state().is_dead();
                if (!dead && !budget()) break;
                const float hp = dead ? 0 : member.health();
                members_[member_count_++] = {observed.id, int(static_cast<cambc::handle_camp_t>(member)),
                    ordinal++, int(sdk.camps().size()), point(member.spawn_position()), hp,
                    {round_, Source::personal_vision, Status::exact_now}};
            }
        }
        return slot;
    }
    GroupBoundary boundary(int slot) const {
        const auto& g = groups_[slot];
        return {actor_, round_, g.id, g.respawn_rounds, g.lifecycle, g.provenance, alive_, g.visible};
    }
public:
    template<class Entries>
    void start(const Entries& entries, const std::vector<int>& lookup, int width, int target) {
        group_count_ = member_count_ = capture_count_ = 0; truncated_ = false;
        producers_ = {};
        const unsigned before = unsigned(cambc::fuel_remaining());
        floor_ = std::max(60000u, before > 9000 ? before - 9000 : 60000u);
        round_ = int(cambc::round_number());
        auto& me = cambc::self(); actor_ = int(static_cast<cambc::handle_champion_t>(me));
        producers_.clock.round = round_; producers_.clock.own.id = actor_;
        if (!budget()) { producers_.truncated = true; previous_count_ = 0; return; }
        alive_ = me.is_alive();
        if (!alive_ || !budget()) { producers_.truncated = truncated_; previous_count_ = 0; return; }
        const auto own = me.position();
        if (!budget()) { producers_.truncated = true; previous_count_ = 0; return; }
        const auto vision = cambc::vision::visible_tiles();
        if (!budget()) { producers_.truncated = true; previous_count_ = 0; return; }
        const auto allies = cambc::vision::champion_tiles(cambc::team::ally).positions();
        if (!budget()) { producers_.truncated = true; previous_count_ = 0; return; }
        const auto enemies = cambc::vision::champion_tiles(cambc::team::enemy).positions();
        for (int n = 0; n < previous_count_ && budget(); ++n) {
            const int slot = query_group(previous_indices_[n], entries, vision, allies, enemies, true);
            if (slot < 0) continue;
            const auto event = capture_between_owner_turns(previous_end_[n], boundary(slot));
            if (event.supported) captures_[capture_count_++] = event;
        }
        query_group(tile_group(own, lookup, width), entries, vision, allies, enemies, true);
        query_group(target, entries, vision, allies, enemies, true);
        // Spawn holders first, then other visible actors. A bounded prefix is
        // explicitly incomplete even if no visible actor was omitted.
        for (int pass = 0; pass < 2; ++pass) for (int side = 0; side < 2; ++side) {
            const auto& positions = side ? enemies : allies;
            const auto& opposition = side ? allies : enemies;
            for (const auto p : positions) {
                if (p == own) continue;
                const int index = tile_group(p, lookup, width);
                if ((index >= 0) != (pass == 0)) continue;
                if (producers_.count == 4 || !budget()) { truncated_ = true; continue; }
#if PLAN4_PRODUCER_ACTOR_PACKETS
                // Same current tile handle and visibility packet as the SDK
                // wrappers below, without constructing unused ability views.
                std::uint32_t other_handle{};
                cambc::detail::check(cambc::detail::sdk_champion_at(p.x, p.y, &other_handle));
                if (!budget()) continue;
                cambc::detail::champion_info packet{};
                const auto code = cambc::detail::sdk_champion(other_handle, &packet);
                if (code == 97) { truncated_ = true; continue; } // SDK is_visible(): api_not_in_vision.
                cambc::detail::check(code);
                if (packet.x < 0 || packet.y < 0) { truncated_ = true; continue; }
                const int other_id = int(other_handle);
#else
                const auto other = cambc::vision::champion_at(p);
                if (!budget()) continue;
                if (!other.is_visible()) { truncated_ = true; continue; }
                const int other_id = int(static_cast<cambc::handle_champion_t>(other));
#endif
                auto& sample = producers_.samples[producers_.count++];
                sample.actor_id = other_id;
                sample.position = point(p); sample.personally_visible = true;
                sample.provenance = {round_, Source::personal_vision, Status::exact_now};
                const int slot = query_group(index, entries, vision, allies, enemies, true);
                if (slot < 0) { sample.ambiguous = index >= 0; continue; }
                const auto& g = groups_[slot]; sample.group_id = g.id;
                bool opposed = false;
                bool opposition_complete = true;
                for (const auto& member : entries[index].group) {
                    if (!budget()) { opposition_complete = false; break; }
                    opposed |= std::find(opposition.begin(), opposition.end(), member.spawn_position()) != opposition.end();
                }
                sample.producing = g.lifecycle == Lifecycle::cleared;
                sample.ambiguous = sample.producing && (!g.all_spawns_visible || opposed || !opposition_complete);
                if (sample.producing && !sample.ambiguous) sample.evidence = ProductionEvidence::visible_cleared_spawn;
            }
        }
        if (int(cambc::round_number()) != round_) {
            group_count_ = member_count_ = capture_count_ = producers_.count = 0; truncated_ = true;
        }
        producers_.truncated = truncated_;
    }
    template<class Entries>
    void finish(const Entries& entries) {
        previous_count_ = 0;
        const int expected_round = round_;
        if (!alive_ || int(cambc::round_number()) != expected_round) return;
        const unsigned before = unsigned(cambc::fuel_remaining());
        floor_ = std::max(30000u, before > 5000 ? before - 5000 : 30000u);
        if (!budget() || !cambc::self().is_alive() || !budget()) return;
        const auto vision = cambc::vision::visible_tiles();
        // Requery only the bounded prefix observed at start. A skipped END
        // cannot leave an older boundary eligible for a future capture proof.
        for (int n = 0; n < group_count_ && budget(); ++n) {
            const auto& sdk = entries[indices_[n]].group;
            bool seen = false;
            for (const auto& member : sdk) {
                if (!budget()) break;
                if (vision.is_set(member.spawn_position())) { seen = true; break; }
            }
            if (!seen || !budget()) continue;
            const auto state = sdk.state();
            auto observed = boundary(n);
            observed.lifecycle = std::holds_alternative<cambc::map::camp_group_alive>(state) ? Lifecycle::living
                : std::holds_alternative<cambc::map::camp_group_reward_available>(state) ? Lifecycle::cleared : Lifecycle::respawning;
            if (observed.lifecycle != Lifecycle::cleared) continue;
            previous_indices_[previous_count_] = indices_[n];
            previous_end_[previous_count_++] = observed;
        }
        if (int(cambc::round_number()) != expected_round) previous_count_ = 0;
    }
    ProducerFrame producers() const { return producers_; }
    LifecycleFrame lifecycles() const { return {producers_.clock, groups_.data(), group_count_, truncated_}; }
    MemberFrame members() const { return {members_.data(), member_count_, truncated_}; }
    int capture_count() const { return capture_count_; }
    const CaptureEvidence& capture(int index) const { return captures_[index]; }
};
} // namespace plan4::runtime
