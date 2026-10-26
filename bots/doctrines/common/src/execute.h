#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

// Pure offline intents. No SDK calls, native timing claim, or replay event API.
// Every legality/safety flag is a fresh actor-local fact supplied by the caller.
namespace p5::execution {

enum class Phase { bootstrap, travel, assemble, clear, hold, allocate, bank, recover, shop, renew };
enum class ActionKind { yield, inspect, refresh, move, attack, cast, hold, withdraw,
                        start_tribute, preserve_channel, cancel_channel, start_recall, buy, vacate };
enum class Channel { none, tribute, recall, teleport };
enum class Reason { ordinary, unknown_target, no_opportunity, low_fuel, unsafe,
                    timeout, assembly_timeout, capture_unresolved, payout, illegal,
                    hidden_block, arrival_recovery, unexpected_error };

struct Group {
    int id = -1;
    int distance = 0;
    bool assigned = false, reachable = false, solo_safe = false, unknown = true;
    bool coalition_required = false;
    bool gem_bearing = true;
};
struct Move {
    int destination = -1;
    bool in_mask = false, path_legal = false, landing_legal = false, safe = false;
    bool known_occupied = false;
};
struct Spell {
    int target = -1;
    bool ready = false, in_range_mask = false, visible = false, category_legal = false;
    bool useful = false, movement_exclusive = false, dash = false;
    bool path_legal = false, landing_legal = false, known_occupied = false;
    bool shield_needed = false;
};
struct Observation {
    int owner_turn = 0, remaining_rounds = 0, forecast_turns = 0;
    int fuel = 0, selection_reserve = 45000, navigation_reserve = 30000, action_reserve = 16000;
    int capture_duration = 0; // configured countdown; initial capture phase is additional
    int group_id = -1;
    bool alive = true, safe = false, can_act = false, at_target = false;
    bool at_spawn = false, spawn_recovery_observed = false, needs_recovery = false;
    bool in_threat_range = false, can_recall = false;
    Channel channel = Channel::none;
    bool channel_valid = false, channel_safe = false, channel_cancellable = false;
    bool on_group_spawn = false, group_cleared = false, group_live_known = false;
    bool group_respawning = false, enemy_on_region = false, occupancy_reset_observed = false;
    bool helper = false, holder_visible_on_spawn = false;
    bool remaining_coalition_safety_fresh = false, remaining_coalition_safe = false;
    bool coalition_visible = false, coalition_safety_fresh = false, coalition_safe = false;
    // Own payout must be attributable self-resource evidence, never lifecycle alone.
    bool own_payout_observed = false;
    int own_payout_group = -1, own_gems_received = 0, own_gold_received = 0;
    std::uint64_t payout_receipt = 0;
    bool bank_requested = false, legal_winning_bank = false, adjacent_altar = false;
    bool hostile_lock = false, lock_visible = false, lock_attack_legal = false;
    int altar_id = -1, own_bag = 0, tribute_ticks_quote = 0;
    bool purchase_requested = false, purchase_purchasable = false, purchase_affordable = false;
    bool field_equipment_allowed = false, purchase_is_equipment = true;
    int purchase_id = -1;
    bool can_move = false, can_attack = false, can_cast = false, ranged = false;
    bool moved_this_turn = false, attack_reset_after_move = false, successful_cast_this_turn = false;
    bool target_visible = false, target_category_legal = false, target_in_range = false;
    int attack_target = -1;
    std::array<Spell, 2> spells{};
    std::array<Group, 8> groups{};
    std::size_t group_count = 0;
    std::array<Move, 4> moves{}; // initial step plus at most three alternatives
    std::size_t move_count = 0;
    std::uint64_t observation_epoch = 0, feedback_sequence = 0;
    ActionKind feedback_kind = ActionKind::yield;
    bool feedback_success = false;
    int feedback_target = -1;
};
struct State {
    Phase phase = Phase::bootstrap, travel_next = Phase::clear;
    int target = -1, phase_start = 0, phase_budget = 8;
    int hold_start = -1, retry_start = -1, retry_budget = 0, recovery_arrival = -1;
    bool capture_retry = false;
    std::uint64_t consumed_payout = 0, intent_sequence = 0, consumed_feedback = 0;
    int movement_turn = -1, failed_moves = 0;
    std::array<int, 4> blocked_moves{{-1, -1, -1, -1}};
    std::uint64_t blocked_epoch = 0;
    int inspected_target = -1, inspection_failures = 0, inspection_turn = -1;
    std::array<int, 8> unavailable{{-1, -1, -1, -1, -1, -1, -1, -1}};
    std::size_t unavailable_count = 0;
};
struct Action {
    ActionKind kind = ActionKind::yield;
    int target = -1, slot = -1, starting_bag = 0, channel_ticks = 0;
    bool whole_bag = false;
    std::uint64_t sequence = 0;
};
struct Decision {
    Action action{};
    Phase phase = Phase::bootstrap;
    Reason reason = Reason::ordinary;
    bool log_error = false;
};

inline int capture_window(int configured_countdown) {
    return configured_countdown >= 0 && configured_countdown < 1000000
        ? configured_countdown + 1 : 0;
}
inline void enter(State& s, Phase phase, const Observation& o) {
    s.phase = phase;
    s.phase_start = o.owner_turn;
    const int estimate = std::clamp(o.forecast_turns, 0, 1000000);
    s.phase_budget = std::min(std::max(0, o.remaining_rounds), std::max(8, 2 * estimate + 4));
    if (phase == Phase::hold) {
        s.hold_start = o.owner_turn;
        s.capture_retry = false;
        s.retry_start = -1;
        s.phase_budget = std::min(std::max(0, o.remaining_rounds),
            std::max(s.phase_budget, capture_window(o.capture_duration)));
    }
}
inline Decision emit(State& s, ActionKind kind, Reason reason = Reason::ordinary,
                     int target = -1, int slot = -1) {
    Decision d;
    d.phase = s.phase;
    d.reason = reason;
    d.action.kind = kind;
    d.action.target = target;
    d.action.slot = slot;
    d.action.sequence = ++s.intent_sequence;
    return d;
}
inline bool known_unavailable(const State& s, int id) {
    for (std::size_t i = 0; i < s.unavailable_count; ++i)
        if (s.unavailable[i] == id) return true;
    return false;
}
inline void exclude_target(State& s) {
    if (s.target >= 0 && !known_unavailable(s, s.target) && s.unavailable_count < s.unavailable.size())
        s.unavailable[s.unavailable_count++] = s.target;
}
inline bool attack_legal(const Observation& o) {
    return o.can_act && o.can_attack && o.target_visible && o.target_category_legal &&
           o.target_in_range && (!o.ranged || !o.moved_this_turn || o.attack_reset_after_move);
}
inline bool spell_legal(const Observation& o, const Spell& p) {
    return o.can_act && o.can_cast && !o.successful_cast_this_turn && p.ready && p.in_range_mask &&
        p.visible && p.category_legal && p.useful && (!p.movement_exclusive || !o.moved_this_turn) &&
        (!p.dash || (p.path_legal && p.landing_legal && !p.known_occupied));
}
inline Decision move_intent(const Observation& o, State& s, ActionKind kind = ActionKind::move) {
    if (o.fuel < o.navigation_reserve) return emit(s, ActionKind::yield, Reason::low_fuel);
    if (s.failed_moves && o.observation_epoch <= s.blocked_epoch)
        return emit(s, ActionKind::refresh, Reason::hidden_block);
    if (!o.can_act || !o.can_move || s.failed_moves >= 4)
        return emit(s, ActionKind::yield, s.failed_moves >= 4 ? Reason::hidden_block : Reason::illegal);
    for (std::size_t i = 0; i < std::min(o.move_count, o.moves.size()); ++i) {
        const auto& m = o.moves[i];
        bool tried = false;
        for (int j = 0; j < s.failed_moves; ++j) if (s.blocked_moves[j] == m.destination) tried = true;
        if (!tried && m.in_mask && m.path_legal && m.landing_legal && m.safe && !m.known_occupied)
            return emit(s, kind, Reason::ordinary, m.destination);
    }
    return emit(s, ActionKind::yield, Reason::illegal);
}
inline void consume_feedback(const Observation& o, State& s) {
    if (s.movement_turn != o.owner_turn) {
        s.movement_turn = o.owner_turn;
        s.failed_moves = 0;
        s.blocked_moves.fill(-1);
    }
    if (!o.feedback_sequence || o.feedback_sequence <= s.consumed_feedback ||
        o.feedback_sequence > s.intent_sequence) return;
    s.consumed_feedback = o.feedback_sequence;
    if (o.feedback_success && o.feedback_kind == ActionKind::withdraw) {
        enter(s, Phase::renew, o);
    }
    if (!o.feedback_success && (o.feedback_kind == ActionKind::move ||
        o.feedback_kind == ActionKind::withdraw || o.feedback_kind == ActionKind::vacate)) {
        if (s.failed_moves < 4) s.blocked_moves[s.failed_moves++] = o.feedback_target;
        s.blocked_epoch = o.observation_epoch;
    }
}

inline Decision decision(const Observation& o, State& s) {
    consume_feedback(o, s);
    const bool own_payout = o.own_payout_observed && o.own_payout_group == s.target &&
        o.payout_receipt > s.consumed_payout && (o.own_gems_received > 0 || o.own_gold_received > 0);
    if (own_payout && (s.phase == Phase::hold || s.phase == Phase::clear)) {
        s.consumed_payout = o.payout_receipt;
        enter(s, Phase::allocate, o);
    }
    // Channels precede changes to inventory, phase deadlines and planner fuel.
    if (o.channel != Channel::none) {
        if (o.channel_valid && o.channel_safe) return emit(s, ActionKind::preserve_channel);
        if (o.channel_cancellable) {
            enter(s, Phase::recover, o);
            return emit(s, ActionKind::cancel_channel, Reason::unsafe);
        }
        return emit(s, ActionKind::yield, Reason::illegal);
    }
    if (o.remaining_rounds <= 0) return emit(s, ActionKind::yield, Reason::timeout);
    if (o.fuel < o.action_reserve) {
        if (s.phase == Phase::hold && o.safe && o.on_group_spawn)
            return emit(s, ActionKind::hold, Reason::low_fuel);
        return emit(s, ActionKind::yield, Reason::low_fuel);
    }
    if (!o.alive && o.purchase_requested) enter(s, Phase::shop, o);
    else if (!o.alive) return emit(s, ActionKind::yield);
    if (o.alive && (!o.safe || o.needs_recovery) && s.phase != Phase::recover) enter(s, Phase::recover, o);
    // At most eight transitions; there is no recursive planner or busy wait.
    for (int transition = 0; transition < 8; ++transition) {
        if (s.phase != Phase::bootstrap && s.phase != Phase::assemble &&
            s.phase != Phase::hold && s.phase != Phase::recover &&
            o.owner_turn - s.phase_start >= s.phase_budget) {
            exclude_target(s);
            enter(s, Phase::bootstrap, o);
            return emit(s, ActionKind::yield, Reason::timeout);
        }
        switch (s.phase) {
        case Phase::bootstrap: {
            if (o.fuel < o.selection_reserve) return emit(s, ActionKind::yield, Reason::low_fuel);
            const Group* chosen = nullptr;
            const Group* unknown = nullptr;
            for (std::size_t i = 0; i < std::min(o.group_count, o.groups.size()); ++i) {
                const auto& g = o.groups[i];
                if (g.id < 0 || !g.assigned || !g.reachable || !g.gem_bearing || known_unavailable(s, g.id)) continue;
                const bool usable = !g.unknown && (g.solo_safe || (g.coalition_required &&
                    o.coalition_visible && o.coalition_safety_fresh && o.coalition_safe));
                auto nearer = [&](const Group* old) { return !old || g.distance < old->distance ||
                    (g.distance == old->distance && g.id < old->id); };
                if (usable && nearer(chosen)) chosen = &g;
                if (g.unknown && nearer(unknown)) unknown = &g;
            }
            if (!chosen && unknown) {
                s.target = unknown->id;
                if (s.inspected_target != s.target) {
                    s.inspected_target = s.target; s.inspection_failures = 0; s.inspection_turn = -1;
                }
                if (o.at_target && o.group_id == s.target && s.inspection_turn != o.owner_turn) {
                    s.inspection_turn = o.owner_turn;
                    if (++s.inspection_failures >= 2) { exclude_target(s); continue; }
                }
                if (o.at_target && o.group_id == s.target)
                    return emit(s, ActionKind::inspect, Reason::unknown_target, s.target);
                return move_intent(o, s);
            }
            if (!chosen) return emit(s, ActionKind::yield, Reason::no_opportunity);
            s.target = chosen->id;
            s.travel_next = chosen->coalition_required ? Phase::assemble : Phase::clear;
            enter(s, Phase::travel, o);
            continue;
        }
        case Phase::travel:
            if (!o.at_target || o.group_id != s.target) return move_intent(o, s);
            enter(s, s.travel_next, o);
            continue;
        case Phase::assemble:
            if (o.owner_turn - s.phase_start >= 8) {
                exclude_target(s); enter(s, Phase::bootstrap, o);
                return emit(s, ActionKind::yield, Reason::assembly_timeout);
            }
            if (o.coalition_visible && o.coalition_safety_fresh && o.coalition_safe) {
                enter(s, Phase::clear, o); continue;
            }
            return emit(s, ActionKind::yield);
        case Phase::clear:
            if (!o.group_live_known || o.group_id != s.target)
                return emit(s, ActionKind::inspect, Reason::unknown_target, s.target);
            if (o.group_respawning) { // Lifecycle is not proof of our ownership.
                exclude_target(s); enter(s, Phase::bootstrap, o);
                return emit(s, ActionKind::yield, Reason::capture_unresolved);
            }
            if (o.group_cleared) { enter(s, Phase::hold, o); continue; }
            // Survival-required shield precedes offensive use of the one cast.
            for (int slot = 0; slot < 2; ++slot)
                if (o.spells[slot].shield_needed && spell_legal(o, o.spells[slot]))
                    return emit(s, ActionKind::cast, Reason::ordinary, o.spells[slot].target, slot);
            for (int slot = 0; slot < 2; ++slot)
                if (spell_legal(o, o.spells[slot]))
                    return emit(s, ActionKind::cast, Reason::ordinary, o.spells[slot].target, slot);
            if (attack_legal(o)) return emit(s, ActionKind::attack, Reason::ordinary, o.attack_target);
            return move_intent(o, s);
        case Phase::hold: {
            if (o.group_id != s.target) return emit(s, ActionKind::inspect, Reason::unknown_target, s.target);
            if (o.enemy_on_region || o.occupancy_reset_observed || !o.on_group_spawn)
                s.hold_start = o.on_group_spawn && !o.enemy_on_region ? o.owner_turn : -1;
            if (o.owner_turn - s.phase_start >= s.phase_budget && !s.capture_retry) {
                s.capture_retry = true; s.retry_start = o.owner_turn; s.hold_start = o.owner_turn;
                s.retry_budget = std::min(o.remaining_rounds, capture_window(o.capture_duration));
            }
            if (s.capture_retry && o.owner_turn - s.retry_start >= s.retry_budget) {
                exclude_target(s); enter(s, Phase::bootstrap, o);
                return emit(s, ActionKind::yield, Reason::capture_unresolved);
            }
            if (o.helper && o.holder_visible_on_spawn && o.remaining_coalition_safety_fresh &&
                o.remaining_coalition_safe) return move_intent(o, s, ActionKind::withdraw);
            // No exact capture countdown exists here. Payout, not elapsed time, advances ALLOCATE.
            if (o.on_group_spawn) return emit(s, ActionKind::hold);
            return move_intent(o, s);
        }
        case Phase::allocate:
            if (o.on_group_spawn) return move_intent(o, s, ActionKind::vacate);
            enter(s, o.bank_requested || o.legal_winning_bank ? Phase::bank :
                (o.purchase_requested ? Phase::shop : Phase::renew), o);
            continue;
        case Phase::bank:
            if (!o.adjacent_altar) return move_intent(o, s);
            if (o.hostile_lock) {
                if (o.lock_visible && o.lock_attack_legal && o.can_act && o.can_attack &&
                    (!o.ranged || !o.moved_this_turn || o.attack_reset_after_move))
                    return emit(s, ActionKind::attack, Reason::ordinary, o.altar_id);
                return emit(s, ActionKind::yield, Reason::illegal);
            }
            if (o.own_bag <= 0) { enter(s, o.purchase_requested ? Phase::shop : Phase::renew, o); continue; }
            if (!o.can_act || o.tribute_ticks_quote <= 0) return emit(s, ActionKind::yield, Reason::illegal);
            {
                auto d = emit(s, ActionKind::start_tribute, Reason::ordinary, o.altar_id);
                d.action.whole_bag = true; d.action.starting_bag = o.own_bag;
                d.action.channel_ticks = o.tribute_ticks_quote; return d;
            }
        case Phase::recover:
            if (o.at_spawn) {
                if (s.recovery_arrival < 0) s.recovery_arrival = o.owner_turn;
                if (o.owner_turn <= s.recovery_arrival)
                    return emit(s, ActionKind::yield, Reason::arrival_recovery);
                if (!o.spawn_recovery_observed || o.needs_recovery) {
                    if (o.owner_turn - s.phase_start >= s.phase_budget) {
                        enter(s, Phase::bootstrap, o);
                        return emit(s, ActionKind::yield, Reason::timeout);
                    }
                    return emit(s, ActionKind::yield, Reason::arrival_recovery);
                }
                s.recovery_arrival = -1;
                enter(s, o.purchase_requested ? Phase::shop : Phase::renew, o); continue;
            }
            s.recovery_arrival = -1;
            if (o.owner_turn - s.phase_start >= s.phase_budget) {
                enter(s, Phase::bootstrap, o);
                return emit(s, ActionKind::yield, Reason::timeout);
            }
            if (o.in_threat_range) return move_intent(o, s);
            if (o.can_act && o.can_recall) return emit(s, ActionKind::start_recall);
            return emit(s, ActionKind::yield, Reason::unsafe);
        case Phase::shop:
            if (!o.purchase_requested) { enter(s, Phase::renew, o); continue; }
            if (!o.at_spawn && o.alive && !(o.field_equipment_allowed && o.purchase_is_equipment))
                return move_intent(o, s);
            if (o.purchase_purchasable && o.purchase_affordable && o.purchase_id >= 0)
                return emit(s, ActionKind::buy, Reason::ordinary, o.purchase_id);
            enter(s, Phase::renew, o); continue;
        case Phase::renew:
            s.target = -1; enter(s, Phase::bootstrap, o); continue;
        }
    }
    return emit(s, ActionKind::yield, Reason::no_opportunity);
}

// A native adapter must log log_error and yield the turn when this catches.
// A no-exception offline build cannot establish engine exception handling.
#if defined(__cpp_exceptions)
inline constexpr bool boundary_catches_exceptions = true;
#else
inline constexpr bool boundary_catches_exceptions = false;
#endif
template<class Function>
Decision turn_boundary(Function&& function) noexcept {
#if defined(__cpp_exceptions)
    try { return function(); }
    catch (...) {
        Decision d;
        d.reason = Reason::unexpected_error;
        d.log_error = true;
        return d;
    }
#else
    return function();
#endif
}
} // namespace p5::execution
