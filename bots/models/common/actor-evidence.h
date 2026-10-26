#pragma once
#include "model-runtime.h"
#include <cambc.h>
#include <vector>

#ifndef PLAN4_ACTOR_PACKET_BLOCK
#define PLAN4_ACTOR_PACKET_BLOCK 0
#endif

namespace plan4::runtime {
// Optional actor collector. The same bounded work runs in both model arms.
// It never reads hidden bags, positions, priorities or combat statistics.
class ActorEvidence {
    std::vector<cambc::champion> allies_, enemies_;
    bool configured_ = false, roster_supported_ = false;
    bool allies_loaded_ = false, enemies_loaded_ = false;
    ActorFrame frame_{};
    int round_ = -1;
    unsigned floor_ = 0;
    unsigned collection_start_fuel_ = 0;
    int stop_reason_ = 0;
    std::int64_t stop_fuel_ = -1;
    void stop(int reason, std::int64_t fuel = -1) {
        frame_.truncated = true;
        if (!stop_reason_) { stop_reason_ = reason; stop_fuel_ = fuel; }
    }
    bool budget() {
        // Preserve the original fuel-first short circuit and SDK query count.
        const auto fuel = cambc::fuel_remaining();
        if (fuel < floor_ + 1200) { stop(1, std::int64_t(fuel)); return false; }
        if (int(cambc::round_number()) != round_) { stop(2, std::int64_t(fuel)); return false; }
        return true;
    }
    bool read(const cambc::champion& actor, bool own, bool ally, VisibleActor& out) {
        if (!budget()) return false;
        // The pinned SDK's getters each fetch this same visible packet. Keep
        // one observation local to this read; never reuse it across turns.
        const auto handle = std::uint32_t(static_cast<cambc::handle_champion_t>(actor));
        cambc::detail::champion_info packet{};
        if (!own) {
            const auto code = cambc::detail::sdk_champion(handle, &packet);
            if (code == 97) return false; // Same api_not_in_vision rule as SDK is_visible().
            cambc::detail::check(code);
            if (packet.x < 0 || packet.y < 0) return false;
        }
        VisibleActor value;
        value.id = int(static_cast<cambc::handle_champion_t>(actor));
        // SDK roster enumeration is not a documented draft-order certificate.
        value.draft_ordinal = -1; value.ally = ally;
        value.provenance = {round_, own ? Source::own_state : Source::personal_vision, Status::exact_now};
        if (!budget()) return false;
        value.alive = actor.is_alive();
        if (!value.alive) return false; // A dead/hidden actor has no live body observation.
        if (!budget()) return false;
        if (own) cambc::detail::check(cambc::detail::sdk_champion(handle, &packet));
#if PLAN4_ACTOR_PACKET_BLOCK
        // One bounded local decode of the current visible packet. These scalar
        // copies perform no SDK queries or allocation. Keep the same 1200-fuel
        // admission margin and check again before the next live import; no
        // packet or body field survives this invocation.
        value.position = {packet.x, packet.y};
        value.champion = packet.type;
        value.health = packet.health;
        value.maximum_health = packet.max_health;
        value.strength = packet.strength;
        value.attack_range = packet.attack_range;
        value.completed_turn = cambc::champion_state::from_bits_retain(packet.state)
            .contains(cambc::champion_state::completed_turn);
        if (!budget()) return false;
#else
        value.position = {packet.x, packet.y};
        if (!budget()) return false;
        value.champion = int(actor.champion_name());
        if (!budget()) return false;
        value.health = packet.health;
        if (!budget()) return false;
        value.maximum_health = packet.max_health;
        if (!budget()) return false;
        value.strength = packet.strength;
        if (!budget()) return false;
        value.attack_range = packet.attack_range;
        if (!budget()) return false;
#endif
        value.bag = actor.gem_count();
        if (!budget()) return false;
        value.current_priority = actor.prev_priority();
        if (!budget()) return false;
        value.next_priority = actor.priority();
        if (!budget()) return false;
#if !PLAN4_ACTOR_PACKET_BLOCK
        value.completed_turn = cambc::champion_state::from_bits_retain(packet.state)
            .contains(cambc::champion_state::completed_turn);
        if (!budget()) return false;
#endif
        out = value; return true;
    }
public:
    void start() {
        stop_reason_ = 0; stop_fuel_ = -1;
        frame_ = {}; round_ = int(cambc::round_number());
        frame_.clock.round = round_;
        const unsigned before = unsigned(cambc::fuel_remaining());
        collection_start_fuel_ = before;
        floor_ = std::max(70000u, before > 6000 ? before - 6000 : 70000u);
        if (!budget()) return;
        if (!configured_) {
            // Draft identities are immutable. Keep each completed SDK load even
            // when its post-call guard stops this frame, so setup can progress.
            if (!allies_loaded_) {
                allies_ = cambc::champions(cambc::team::ally);
                allies_loaded_ = true;
                if (!budget()) return;
            }
            if (!enemies_loaded_) {
                enemies_ = cambc::champions(cambc::team::enemy);
                enemies_loaded_ = true;
                if (!budget()) return;
            }
            roster_supported_ = !allies_.empty() && allies_.size() <= 5 && !enemies_.empty() && enemies_.size() <= 5;
            configured_ = true;
        }
        if (!roster_supported_) { stop(3); return; }
        const auto& own = cambc::self();
        const int own_id = int(static_cast<cambc::handle_champion_t>(own));
        frame_.clock.own.id = own_id;
        frame_.allied_count = int(allies_.size()); frame_.enemy_count = int(enemies_.size());
        int own_ordinal = -1;
        for (int n = 0; n < frame_.allied_count; ++n) {
            frame_.allied_roster[n] = int(static_cast<cambc::handle_champion_t>(allies_[n]));
            if (frame_.allied_roster[n] == own_id) own_ordinal = n;
        }
        for (int n = 0; n < frame_.enemy_count; ++n)
            frame_.enemy_roster[n] = int(static_cast<cambc::handle_champion_t>(enemies_[n]));
        frame_.roster_complete = own_ordinal >= 0;
        for (int a = 0; a < frame_.allied_count; ++a) {
            for (int b = a + 1; b < frame_.allied_count; ++b)
                if (frame_.allied_roster[a] == frame_.allied_roster[b]) frame_.roster_complete = false;
            for (int b = 0; b < frame_.enemy_count; ++b)
                if (frame_.allied_roster[a] == frame_.enemy_roster[b]) frame_.roster_complete = false;
        }
        for (int a = 0; a < frame_.enemy_count; ++a) for (int b = a + 1; b < frame_.enemy_count; ++b)
            if (frame_.enemy_roster[a] == frame_.enemy_roster[b]) frame_.roster_complete = false;
        if (!frame_.roster_complete) { stop(4); return; }
        frame_.own_observed = read(own, true, true, frame_.own);
        if (!frame_.own_observed) return;
        for (int side = 0; side < 2; ++side) {
            const auto& roster = side ? enemies_ : allies_;
            for (int n = 0; n < int(roster.size()); ++n) {
                if (int(static_cast<cambc::handle_champion_t>(roster[n])) == own_id) continue;
                if (!budget()) break;
                VisibleActor value;
                if (read(roster[n], false, side == 0, value)) frame_.samples[frame_.count++] = value;
            }
        }
        (void)budget(); // Includes the last invisible-actor visibility call.
        if (int(cambc::round_number()) != round_) {
            frame_.count = 0; frame_.own_observed = frame_.roster_complete = false; stop(2);
        }
    }
    const ActorFrame& frame() const { return frame_; }
    unsigned collection_start_fuel() const { return collection_start_fuel_; }
    unsigned minimum_fuel() const { return floor_ + 1200; }
    int stop_reason() const { return stop_reason_; }
    std::int64_t stop_fuel() const { return stop_fuel_; }
};
} // namespace plan4::runtime
