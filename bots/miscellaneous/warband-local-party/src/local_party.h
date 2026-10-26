#pragma once
// Included once after brain.cpp. A personally observed co-farm becomes an explicit
// fixed-position two-body duty. Slots name existing work, never invented arrivals.
#include "party_model.h"
#include "party_record.h"
#include "party_cleanup.h"

namespace mb {
namespace {

struct LocalPartyState {
    int group = -1, peer = -1, started = -1;
    int saved_priority = 0;
    bool priority_owned = false, owns_turn = false;
    int trace_count = 0;
    int release_group = -1, release_until = -1;
    party::LocalRecord record;
};

LocalPartyState& local_party_state() {
    if constexpr (cfg::control_actual_party) { static LocalPartyState state; return state; }
    else __builtin_unreachable();
}

party::LocalRecord local_party_read() {
    if constexpr (!cfg::control_actual_party) return {};
    else {
        const auto size = cambc::shared_memory_buffer::size();
        if (Knowledge::get().allies.size() > 5 || size < Knowledge::get().allies.size() + 3) return {};
        std::array<std::uint8_t, 3> bytes{};
        cambc::shared_memory.read(size - 3, std::span<std::uint8_t>{bytes});
        return party::LocalRecord::decode(bytes);
    }
}

void local_party_write(party::LocalRecord record) {
    if constexpr (cfg::control_actual_party) {
        const auto size = cambc::shared_memory_buffer::size();
        if (Knowledge::get().allies.size() > 5 || size < Knowledge::get().allies.size() + 3) return;
        const auto bytes = record.encode();
        cambc::shared_memory.write(size - 3, std::span<const std::uint8_t>{bytes});
    }
}

struct LocalPartyActor {
    int index = -1;
    vec2 pos{};
    float hp{}, max_hp{}, strength{}, mitigation{1};
    int range{};
    cambc::champion_type type{};
    cambc::champion_state state{};
};

bool local_party_actor(const Ctx& ctx, int index, LocalPartyActor& out) {
    const auto& kn = Knowledge::get();
    if (index < 0 || index >= static_cast<int>(kn.allies.size())) return false;
    const auto& actor = kn.allies[static_cast<std::size_t>(index)];
    if (!actor.is_alive() || !actor.is_visible()) return false;
    const auto pos = actor.position();
    if (!ctx.visible.is_set(pos)) return false;
    const auto state = actor.state();
    if (state.is_dead() || state.contains(cambc::champion_state::recalling)
        || state.contains(cambc::champion_state::submitting_gems)
        || state.contains(cambc::champion_state::teleporting) || actor.buffs().has_stun()) return false;
    out = {index, pos, actor.health(), actor.max_health(), actor.strength(), 1,
        actor.attack_range(), actor.champion_name(), state};
    if (index == kn.self_index) out.mitigation = ctx.camp_damage_taken_mult;
    else {
        // Emblem families do not stack; use actual equipment, not the caller's growth proxy.
        const auto counts = actor.item_count();
        bool jungle = false, durability = false;
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(cambc::item_type::count); ++i) {
            const cambc::item_type item{i};
            if (counts[item] == 0) continue;
            const auto passive = item.passive();
            if (!passive) continue;
            if ((*passive == cambc::item_passive::jungle && !jungle)
                || (*passive == cambc::item_passive::durability && !durability)) {
                (*passive == cambc::item_passive::jungle ? jungle : durability) = true;
                out.mitigation *= 1 - item.passive_magnitude();
            }
        }
    }
    return out.hp > 0 && out.range == 1 && out.type != cambc::champion_type::varg
        && (state.contains(cambc::champion_state::can_attack)
            || state.contains(cambc::champion_state::completed_turn));
}

bool local_party_no_other_fire(const Ctx& ctx, int group, vec2 a, vec2 b) {
    if (!ctx.enemies.empty() || ctx.n_unseen != 0 || ctx.hit_unseen) return false;
    const auto& world = World::get();
    for (std::size_t g = 0; g < world.groups.size(); ++g) {
        if (static_cast<int>(g) == group) continue;
        const auto& info = world.groups[g];
        if (info.harmless) continue;
        for (std::size_t m = 0; m < info.tiles.size(); ++m) {
            const auto& stats = world.camp_stats[info.types[m]];
            if (stats.strength <= 0 || (cheb(info.tiles[m], a) > stats.range
                && cheb(info.tiles[m], b) > stats.range)) continue;
            // An unseen possible source cannot be assigned zero damage.
            if (!ctx.visible.is_set(info.tiles[m])) return false;
        }
    }
    for (const auto& camp : ctx.camps)
        if (camp.group != group && camp.awake && camp.strength > 0
            && (cheb(camp.pos, a) <= camp.range || cheb(camp.pos, b) <= camp.range)) return false;
    return true;
}

} // namespace

void Brain::restore_party_priority() {
    if constexpr (cfg::control_actual_party) {
        auto& state = local_party_state();
        if (!state.priority_owned || !cambc::self().is_alive()) return;
        if (cambc::self().priority() != state.saved_priority)
            cambc::self().set_priority(state.saved_priority);
        state.priority_owned = false;
    }
}

void Brain::clear_actual_party(bool shared_release) {
    if constexpr (cfg::control_actual_party) {
        auto& state = local_party_state();
        const auto released = state.record;
        // Local intent is invalid even if either independent engine cleanup call fails.
        // Keep a failed priority obligation for a later live owner turn.
        state.group = -1;
        state.peer = -1;
        state.started = -1;
        state.record = {};
        party::finish_release([&] {
            if (shared_release && released.same_duty(local_party_read())) local_party_write({});
        }, [&] { restore_party_priority(); });
    }
}

void Brain::begin_actual_party_turn() {
    if constexpr (cfg::control_actual_party) {
        local_party_state().owns_turn = false;
        // A dead body cannot set a priority. Its obligation survives until the first
        // live owner turn; its shared commitment is still released at turn end.
        restore_party_priority();
    }
}

void Brain::finish_actual_party_turn() {
    if constexpr (cfg::control_actual_party)
        if (!local_party_state().owns_turn) clear_actual_party();
}

bool Brain::party_released(const Ctx& ctx, int group) const {
    if constexpr (!cfg::control_actual_party) return false;
    else {
        const auto& state = local_party_state();
        return state.release_group == group && ctx.round <= state.release_until;
    }
}

bool Brain::handle_actual_party(Ctx& ctx, TurnActions& acts) {
    if constexpr (!cfg::control_actual_party) return false;
    else {
        auto& state = local_party_state();
        auto& kn = Knowledge::get();
        auto& world = World::get();
        // The final three bytes are also the inherited optional SOS/sighting channel.
        // These independent protocols may not be enabled in the same executable.
        if constexpr (cfg::share_sightings || cfg::sos_radius > 0) return false;
        if (kn.allies.size() > 5 || cambc::shared_memory_buffer::size() < kn.allies.size() + 3) return false;
        const auto trace = [&](const char* phase, int group, int peer, float a = 0, float b = 0) {
            if constexpr (cfg::control_party_trace) {
                if (state.trace_count < 24 && fuel_ok(3000)) {
                    ++state.trace_count;
                    MB_LOG("localparty r%d a%d g%d peer%d %s %.1f %.1f\n",
                        ctx.round, kn.self_index, group, peer, phase, a, b);
                }
            }
        };
        if (state.group >= 0 && (ctx.round - state.started > 64 || target_ != state.group)) {
            trace("release-duty", state.group, state.peer);
            clear_actual_party();
        }
        if (state.group >= 0 && (!state.record.same_duty(local_party_read())
            || !state.record.fresh(ctx.round))) clear_actual_party(false);
        if (state.group >= 0) {
            const auto& belief = kn.groups[static_cast<std::size_t>(state.group)];
            if (belief.seen_round == ctx.round && belief.status == GroupStatus::respawning) {
                trace("release-observed", state.group, state.peer);
                clear_actual_party();
                return false;
            }
            if (belief.seen_round == ctx.round && belief.status == GroupStatus::reward
                && local_party_no_other_fire(ctx, state.group, ctx.pos, ctx.pos)) {
                const int group = state.group, peer = state.peer;
                restore_party_priority();
                if (kn.self_index > peer) {
                    // One actual capturer earns the whole reward. A helper leaves on OBSERVED
                    // reward availability, never a communicated intent or predicted kill.
                    capturing_ = false;
                    leave_camp_tiles(ctx, acts);
                    if (world.group_at(ctx.pos) >= 0) {
                        why_ = "party-vacate";
                        state.owns_turn = true;
                        return true;
                    }
                    trace("helper-release", group, peer, ctx.hp, ctx.gems);
                    state.release_group = group;
                    state.release_until = ctx.round + cambc::constants::capture_time + 8;
                    banned_until_[static_cast<std::size_t>(group)] = ctx.round + cambc::constants::capture_time + 8;
                    target_ = -1;
                    capturing_ = false;
                    engaged_ = false;
                    publish(-1, static_cast<int>(SlotStatus::idle));
                    clear_actual_party(false);
                    state.owns_turn = true;
                    why_ = "party-release";
                    return true;
                }
                const auto& helper_actor = kn.allies[static_cast<std::size_t>(peer)];
                if (helper_actor.is_alive() && !helper_actor.is_visible()) {
                    // Unknown live occupancy is never evidence of a sole capturer.
                    capturing_ = false;
                    leave_camp_tiles(ctx, acts);
                    why_ = "party-await-helper";
                    state.owns_turn = true;
                    return true;
                }
                if (helper_actor.is_alive() && helper_actor.is_visible()) {
                    const auto peer_pos = helper_actor.position();
                    for (const auto tile : world.groups[static_cast<std::size_t>(group)].tiles)
                        if (tile == peer_pos) {
                            capturing_ = false;
                            leave_camp_tiles(ctx, acts);
                            why_ = "party-await-vacancy";
                            state.owns_turn = true;
                            return true;
                        }
                }
                trace("capture-hold", group, peer, ctx.hp, ctx.gems);
                target_ = group;
                engaged_ = false;
                why_ = "party-capture";
                if (!go_capture(ctx, acts, group)) {
                    banned_until_[static_cast<std::size_t>(group)] = ctx.round + 10;
                    target_ = -1;
                    clear_actual_party();
                }
                state.owns_turn = true;
                return true;
            }
        }
        const int group = target_;
        if (group < 0 || kit().ranged || kit().type == cambc::champion_type::varg
            || !ctx.state.contains(cambc::champion_state::can_attack) || !fuel_ok(18000)) return false;
        const auto& belief = kn.groups[static_cast<std::size_t>(group)];
        if (belief.seen_round != ctx.round || belief.status != GroupStatus::alive) return false;
        const auto& info = world.groups[static_cast<std::size_t>(group)];
        if (info.tiles.size() > 16) return false;
        for (const auto tile : info.tiles) if (!ctx.visible.is_set(tile)) return false;
        const CampView* elk = nullptr;
        int harmful = 0;
        for (const auto& camp : ctx.camps) if (camp.group == group && camp.hp > 0 && camp.strength > 0) {
            if (++harmful != 1 || !camp.camp->is_elk()) return false;
            elk = &camp;
        }
        if (!elk && state.group == group
            && local_party_no_other_fire(ctx, group, ctx.pos, ctx.pos)) {
            // The harmful wave is over; harmless members still need actual work before
            // any capture exists. Keep the named capturer through this real suffix.
            restore_party_priority();
            why_ = "party-cleanup";
            const bool acted = fight_camps(ctx, acts, group, true);
            if (acted) { state.owns_turn = true; publish(group, static_cast<int>(SlotStatus::engaging)); }
            return acted;
        }
        if (!elk || cheb(ctx.pos, elk->pos) != 1) return false;
        // Both exact bodies already named this same work and personally arrived. No
        // unassigned ally, estimated route, headcount or hypothetical joiner creates a duty.
        const auto& slots = kn.slots();
        const auto own = slots[static_cast<std::size_t>(kn.self_index)];
        if (own.group != group || own.status != SlotStatus::engaging) return false;
        LocalPartyActor self, peer;
        if (!local_party_actor(ctx, kn.self_index, self)) return false;
        int peers = 0;
        for (std::size_t i = 0; i < slots.size(); ++i) {
            if (static_cast<int>(i) == kn.self_index || slots[i].group != group
                || slots[i].status != SlotStatus::engaging) continue;
            LocalPartyActor candidate;
            if (!local_party_actor(ctx, static_cast<int>(i), candidate)
                || cheb(candidate.pos, elk->pos) != 1) continue;
            peer = candidate;
            ++peers;
        }
        if (peers != 1 || std::abs(self.mitigation - peer.mitigation) > 0.0001f
            || !local_party_no_other_fire(ctx, group, self.pos, peer.pos)) return false;
        // A third real adjacent body, even on another duty or with no ready attack,
        // would leave the two-body targeting/action scope. Published slots are not a census.
        for (const auto& ally : ctx.allies)
            if (ally.index != peer.index && cheb(ally.pos, elk->pos) == 1) return false;
        const LocalPartyActor& leader = self.index < peer.index ? self : peer;
        const LocalPartyActor& helper = self.index < peer.index ? peer : self;
        const auto bound = party::rotate_remaining_wave(leader.hp, helper.hp, elk->strength,
            leader.mitigation, leader.strength, helper.strength, elk->hp);
        if (!bound.valid || !bound.safe) return false;
        auto shared = local_party_read();
        party::LocalRecord proposed{group, leader.index, helper.index, ctx.round, 0};
        if (shared.fresh(ctx.round)) {
            if (shared.group != group || shared.leader != leader.index || shared.helper != helper.index) return false;
            proposed = shared;
        }
        const bool accepted_before = proposed.acknowledged(self.index);
        if (!accepted_before) {
            // Before both bodies accept, own only one conservative stationary action turn.
            // This actor can take the next volley and still pay its two-volley escape;
            // ordinary low-health recall is not suppressed by an unaccepted invitation.
            if (self.hp <= cfg::recall_health_frac * self.max_hp
                || self.hp <= 3 * elk->strength * self.mitigation) return false;
            proposed.acknowledge(self.index);
            local_party_write(proposed);
            trace(proposed.ack == 3 ? "accept" : "offer", group, peer.index);
        }
        const bool pair_accepted = proposed.ack == 3;
        if (!pair_accepted && (self.hp <= cfg::recall_health_frac * self.max_hp
            || self.hp <= 3 * elk->strength * self.mitigation)) return false;
        state.record = proposed;
        const bool leader_tanks = !(helper.hp > leader.hp + 0.1f);
        const int self_priority = ((self.index == leader.index) == leader_tanks) ? 100 : 0;
        auto& me = cambc::self();
        state.saved_priority = me.priority();
        state.priority_owned = true;
        if (me.priority() != self_priority) me.set_priority(self_priority);
        // Both ACKs bind this controller's stationary continuation, subject to fresh
        // observations each owner turn. Self-only priority writes are transiently unequal
        // after a tank switch; the model bounds EITHER first future tank. No immediate
        // reorder is credited, and every preemption erases this exact shared duty.
        if (state.group != group || state.peer != peer.index) {
            state.group = group;
            state.peer = peer.index;
            state.started = proposed.epoch;
            trace(pair_accepted ? "execute" : "solo-entry", group, peer.index, bound.hp[0], bound.hp[1]);
        }
        target_need_ = 2;
        engaged_ = true;
        staged_since_ = -1;
        chosen_round_ = ctx.round;
        publish(group, static_cast<int>(SlotStatus::engaging));
        why_ = "party-wave";
        const bool acted = fight_camps_stationary(ctx, acts, group);
        if (acts.attacked || acts.cast) last_hit_round_ = ctx.round;
        if (!acted) {
            clear_actual_party();
            return false;
        }
        // The casts are extra applied work, never forecast credit. Recheck live HP and the
        // still-living camp after dispatch before choosing the following round's tank.
        ctx.refresh_self();
        if (elk->hp > 0 && cambc::self().is_alive()) {
            const float hp_self = cambc::self().health();
            const float hp_peer = kn.allies[static_cast<std::size_t>(peer.index)].health();
            const bool self_tanks = hp_self > hp_peer + 0.1f
                || (std::abs(hp_self - hp_peer) <= 0.1f && self.index < peer.index);
            const int wanted = self_tanks ? 100 : 0;
            if (me.priority() != wanted) me.set_priority(wanted);
        } else restore_party_priority();
        state.owns_turn = true;
        return true;
    }
}

} // namespace mb
