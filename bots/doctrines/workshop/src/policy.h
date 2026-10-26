#pragma once
#include "profile.h"
#include "../../common/src/accounting.h"
#include "../../common/src/execute.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace p5::workshop {
enum class Job { unavailable, inspect, recover, preserve, bank_now, two_groups };
enum class Timing { none, now, after_first_capture };
struct Purchase {
    int family = -1, item = -1, shards = 0, cost = 0;
    double strength_gain = 0, health_gain = 0, spell_damage_gain = 0;
    bool legal_in_field = false, equipment = true;
};
struct Group {
    int id = -1, gems = 0, gold = 0;
    bool live_observed = false, reward_or_alive = false;
    CombatInput combat{};
};
struct RouteProject {
    Group first{}, successor{};
    RouteLeg to_first{}, between{}, to_altar{}, return_to_work{};
};
struct Observation {
    int round = 0, team_score = 0;
    ActorObservation self{};
    CombatActor stats{};
    EconomicSettings settings{};
    FinishWindows windows{};
    std::array<RouteProject, 8> routes{};
    std::size_t count = 0;
    std::array<Purchase, 3> purchases{};
    std::size_t purchase_count = 0;
    RouteLeg current_to_altar{}, bank_return{};
    bool bank_legal = false, safe_channel = false, safe_active_capture = false;
    bool needs_recovery = false;
    int fuel_remaining = 100000;
};
struct PrivateState {
    bool committed = false;
    int first_group = -1, successor_group = -1;
    Timing timing = Timing::none;
    int purchase_family = -1;
};
struct ForecastTerms {
    int travel = 0, first_clear = 0, first_capture = 0, between = 0;
    int second_clear = 0, second_capture = 0, conversion_travel = 0;
    int channel = 0, recovery_shop = 0, return_to_work = 0;
    int first_payout_tick = -1, purchase_tick = -1, second_payout_tick = -1;
    int bank_tick = -1, productive_tick = -1;
    double first_injury = 0, second_injury = 0, final_health = 0, final_shield = 0;
    bool successor_spell_credited = false;
};
struct Decision {
    bool feasible = false;
    Job job = Job::unavailable;
    Timing timing = Timing::none;
    int first_group = -1, successor_group = -1, purchase_family = -1;
    int purchase_item = -1, purchase_cost = 0;
    int expected_first_gems = 0, expected_first_gold = 0;
    int expected_second_gems = 0, expected_second_gold = 0;
    int initial_bag = 0, initial_gold = 0, observation_round = -1;
    AccountingSchedule schedule{};
    AccountingResult rank{};
    ForecastTerms forecast{};
    const char* first_action = "yield";
    const char* abort_condition = "no-certified-opportunity";
};
inline bool valid_purchase(const Purchase& p) {
    // A menu bundle is one legal shard in this offline increment. The adapter
    // must requery price/stats/affordability before each subsequent decision.
    return p.family >= 0 && p.family < 3 && p.item >= 0 && p.shards == 1 && p.cost > 0 &&
        p.legal_in_field && p.equipment && std::isfinite(p.strength_gain) && p.strength_gain >= 0 &&
        std::isfinite(p.health_gain) && p.health_gain >= 0 &&
        std::isfinite(p.spell_damage_gain) && p.spell_damage_gain >= 0;
}
inline bool valid_actor(const CombatActor& a, int id) {
    return a.id == id && a.self && a.visible && a.enemy_cast_bound_known &&
        std::isfinite(a.health) && std::isfinite(a.max_health) && a.health > 0 && a.max_health >= a.health &&
        std::isfinite(a.strength) && a.strength >= 0 && std::isfinite(a.basic_modifier) && a.basic_modifier >= 0 &&
        std::isfinite(a.direct_or_reset_damage) && a.direct_or_reset_damage >= 0 &&
        std::isfinite(a.shield) && a.shield >= 0 && a.shield_expiry_tick >= 0 &&
        std::isfinite(a.visible_enemy_next_phase) && a.visible_enemy_next_phase >= 0;
}
inline int leg_turns(const RouteLeg& r) {
    return r.safety_checked ? r.turns() : -1;
}
inline bool reserve_safe(const CombatActor& a) {
    return a.health > 0 && a.health >= std::max(0.20 * a.max_health, 2 * a.visible_enemy_next_phase);
}
// Health and shielding remain distinct stocks across the entire chronology.
// An expiry inside a segment receives no credit, a conservative lower bound.
inline bool exposure(CombatActor& a, int ticks, double damage) {
    if (ticks < 0 || ticks > 100000 || !std::isfinite(damage) || damage < 0) return false;
    const double protection = a.shield_expiry_tick >= ticks ? a.shield : 0;
    const double absorbed = std::min(protection, damage);
    a.health -= damage - absorbed;
    a.shield = protection - absorbed;
    a.shield_expiry_tick = std::max(0, a.shield_expiry_tick - ticks);
    if (!a.shield_expiry_tick) a.shield = 0;
    return reserve_safe(a);
}
inline bool travel(CombatActor& a, int ticks) {
    return exposure(a, ticks, a.visible_enemy_next_phase * ticks);
}
inline void equip(CombatActor& a, const Purchase& p) {
    a.strength += p.strength_gain;
    a.max_health += p.health_gain; // Field Health NEVER increases current health.
    a.direct_or_reset_damage += p.spell_damage_gain;
}
inline CombatForecast group_forecast(const Group& group, CombatActor& actor, int capture_ticks) {
    CombatForecast out;
    if (group.id < 0 || group.gems < 0 || group.gold < 0 || !group.live_observed ||
        !group.reward_or_alive || group.combat.actor_count != 1 ||
        group.combat.actors[0].id != actor.id || !group.combat.actors[0].self ||
        group.combat.monster_count < 1 || group.combat.monster_count > 8) return out;
    auto combat = group.combat;
    for (std::size_t i = 0; i < combat.monster_count; ++i) {
        const auto& m = combat.monsters[i];
        if (!std::isfinite(m.public_health) || !std::isfinite(m.observed_health) ||
            !std::isfinite(m.effective_strength)) return out;
    }
    combat.actors[0] = actor;
    combat.hold_ticks = capture_ticks;
    out = forecast_combat(combat);
    if (out.state != EstimateState::feasible) return out;
    const int exposure_ticks = out.clear_ticks + capture_ticks;
    const double damage = out.worst_camp_damage + actor.visible_enemy_next_phase * exposure_ticks;
    if (!exposure(actor, exposure_ticks, damage)) {
        out.state = EstimateState::unsafe; out.reason = "cumulative-health-or-shield-reserve";
    }
    return out;
}
inline AccountingSchedule initial_schedule(const Observation& input, int target) {
    const auto own = private_view(input.self, input.round);
    AccountingSchedule s;
    s.actor_count = 1; s.target_id = target; s.doctrine_id = 4;
    s.starting_team_score = input.team_score; s.win_score = input.settings.winning_gems;
    s.tribute_gems_per_tick = input.settings.tribute_gems_per_tick;
    s.tribute_min_ticks = input.settings.tribute_min_ticks;
    s.actors[0] = {own.id, true, true, own.bag.current(input.round), own.bag.value, own.gold.value, 0, 0};
    return s;
}
inline void event(AccountingSchedule& s, AccountingEventKind kind, int tick, int gems = 0,
                  int gold = 0, int group = -1, int waypoint = -1) {
    auto& e = s.events[s.event_count];
    e.id = s.event_count++; e.kind = kind; e.tick = tick; e.actor = 0;
    e.gems = gems; e.gold = gold; e.group = group; e.waypoint = waypoint;
    if (kind == AccountingEventKind::capture) e.participants = e.holders = 1;
}
inline Decision project(const Observation& input, const RouteProject& route,
                        Timing timing = Timing::none, const Purchase* purchase = nullptr) {
    Decision out;
    auto fail = [&](const char* why) { out.abort_condition = why; return out; };
    const auto own = private_view(input.self, input.round);
    if (!input.settings.valid() || !input.windows.valid || !own.self || !own.visible ||
        !own.bag.current(input.round) || !own.gold.current(input.round) || own.bag.value < 0 ||
        own.gold.value < 0 || !valid_actor(input.stats, own.id) || input.team_score < 0)
        return fail("invalid-current-self-or-settings");
    if (route.first.id == route.successor.id) return fail("duplicate-production-group");
    if (timing != Timing::none && (!purchase || !valid_purchase(*purchase)))
        return fail("invalid-field-purchase");
    const int to_first = leg_turns(route.to_first), between = leg_turns(route.between);
    const int to_altar = leg_turns(route.to_altar), back = leg_turns(route.return_to_work);
    if (to_first < 0 || between < 0 || to_altar < 0 || back < 0 || !input.bank_legal ||
        route.to_first.destination != route.first.id || route.between.destination != route.successor.id)
        return fail("uncertified-route-bank-or-return");
    if (input.settings.capture_rounds > 100000) return fail("invalid-capture-window");
    const int capture = input.settings.capture_rounds + 1;
    out.job = Job::two_groups; out.timing = timing;
    out.first_group = route.first.id; out.successor_group = route.successor.id;
    out.initial_bag = own.bag.value; out.initial_gold = own.gold.value; out.observation_round = input.round;
    out.expected_first_gems = route.first.gems; out.expected_first_gold = route.first.gold;
    out.expected_second_gems = route.successor.gems; out.expected_second_gold = route.successor.gold;
    out.schedule = initial_schedule(input, route.first.id);
    CombatActor actor = input.stats;
    auto& f = out.forecast;
    if (timing != Timing::none) {
        out.purchase_family = purchase->family; out.purchase_item = purchase->item; out.purchase_cost = purchase->cost;
    }
    if (timing == Timing::now) {
        if (own.gold.value < purchase->cost) return fail("buy-now-needs-actual-own-gold");
        equip(actor, *purchase); f.purchase_tick = 0;
        event(out.schedule, AccountingEventKind::purchase, 0, 0, purchase->cost);
    }
    if (!travel(actor, to_first)) return fail("unsafe-first-travel");
    f.travel = to_first; f.first_capture = capture;
    const auto first = group_forecast(route.first, actor, capture);
    if (first.state != EstimateState::feasible) return fail("unsafe-or-unknown-first-group");
    f.first_clear = first.clear_ticks; f.first_injury = input.stats.health - actor.health;
    f.first_payout_tick = to_first + f.first_clear + capture;
    event(out.schedule, AccountingEventKind::capture, f.first_payout_tick,
          route.first.gems, route.first.gold, route.first.id);
    if (timing == Timing::after_first_capture) {
        if (static_cast<AccountingStock>(own.gold.value) + route.first.gold < purchase->cost)
            return fail("first-payout-cannot-fund-purchase");
        equip(actor, *purchase); f.purchase_tick = f.first_payout_tick;
        event(out.schedule, AccountingEventKind::purchase, f.purchase_tick, 0, purchase->cost);
    }
    if (!travel(actor, between)) return fail("unsafe-successor-travel");
    // No second readiness observation is available at planning time. A ready
    // first cast cannot be spent again on the represented successor for free.
    actor.spell_known_ready = actor.legal_first_cast = actor.heal_legal_ready = false;
    f.between = between; f.second_capture = capture;
    const double second_start_health = actor.health;
    const auto second = group_forecast(route.successor, actor, capture);
    if (second.state != EstimateState::feasible) return fail("unsafe-or-unknown-successor");
    f.second_clear = second.clear_ticks; f.second_injury = second_start_health - actor.health;
    f.second_payout_tick = f.first_payout_tick + between + f.second_clear + capture;
    event(out.schedule, AccountingEventKind::capture, f.second_payout_tick,
          route.successor.gems, route.successor.gold, route.successor.id);
    const auto whole_bag = static_cast<AccountingStock>(own.bag.value) + route.first.gems + route.successor.gems;
    if (whole_bag <= 0 || whole_bag > 1000000) return fail("no-bounded-whole-bag-liquidation");
    f.conversion_travel = to_altar; f.channel = input.settings.channel_ticks(static_cast<int>(whole_bag));
    f.return_to_work = back;
    if (!travel(actor, to_altar + f.channel + back)) return fail("unsafe-bank-or-renewal");
    const int channel_start = f.second_payout_tick + to_altar;
    f.bank_tick = channel_start + f.channel; f.productive_tick = f.bank_tick + back;
    f.final_health = actor.health; f.final_shield = actor.shield;
    event(out.schedule, AccountingEventKind::tribute_start, channel_start, -1);
    event(out.schedule, AccountingEventKind::tribute_complete, f.bank_tick, -1);
    event(out.schedule, AccountingEventKind::return_to_work, f.productive_tick, 0, 0, -1,
          route.return_to_work.destination);
    out.schedule.actors[0].occupied_turns = f.productive_tick;
    out.rank = forecast_schedule(out.schedule, input.windows);
    if (out.rank.status != AccountingStatus::feasible) return fail(out.rank.reason);
    out.feasible = true;
    out.first_action = timing == Timing::now ? "buy-one-field-shard" : "travel-to-first-group-and-reobserve";
    out.abort_condition = "danger-route-payout-price-or-purchase-premise-failed";
    return out;
}
inline Decision bank_only(const Observation& input) {
    Decision out;
    const auto own = private_view(input.self, input.round);
    const int to = input.current_to_altar.distance == 0 ? 0 : leg_turns(input.current_to_altar);
    const int back = leg_turns(input.bank_return);
    if (!input.settings.valid() || !own.bag.current(input.round) || !own.gold.current(input.round) ||
        !own.self || !own.visible || own.bag.value <= 0 || !input.bank_legal || to < 0 ||
        !input.current_to_altar.safety_checked || !input.current_to_altar.terrain_and_diagonals_checked ||
        !valid_actor(input.stats, own.id)) return out;
    const int ticks = input.settings.channel_ticks(own.bag.value);
    const int complete = to + ticks;
    const bool winning = own.bag.value >= input.settings.winning_gems - input.team_score &&
        complete <= input.windows.official_remaining;
    if (!winning && back < 0) return out;
    auto actor = input.stats;
    if (!travel(actor, complete + (winning ? 0 : back))) return out;
    out.schedule = initial_schedule(input, input.bank_return.destination);
    event(out.schedule, AccountingEventKind::tribute_start, to, -1);
    event(out.schedule, AccountingEventKind::tribute_complete, complete, -1);
    out.schedule.actors[0].occupied_turns = complete + (winning ? 0 : back);
    if (winning) {
        out.schedule.terminal_win = true; out.schedule.terminal_win_tick = complete;
        out.schedule.immediate_legal_winning_bank = input.current_to_altar.distance == 0;
    } else event(out.schedule, AccountingEventKind::return_to_work, complete + back, 0, 0, -1,
                 input.bank_return.destination);
    out.rank = forecast_schedule(out.schedule, input.windows);
    out.feasible = out.rank.status == AccountingStatus::feasible;
    out.job = Job::bank_now; out.initial_bag = own.bag.value; out.initial_gold = own.gold.value;
    out.observation_round = input.round; out.first_action = "travel-to-altar-or-tribute";
    out.abort_condition = "unsafe-bank-route-or-channel";
    return out;
}
inline bool better(const Decision& candidate, const Decision& old) {
    return candidate.feasible && (!old.feasible ||
        (same_actors(candidate.rank, old.rank) && candidate.rank.windows == old.rank.windows &&
         compare_results(candidate.rank, old.rank) > 0));
}
inline Decision choose_purchase(const Observation& input, const RouteProject& route) {
    const Decision baseline = project(input, route);
    Decision best = baseline;
    for (std::size_t p = 0; p < std::min(input.purchase_count, input.purchases.size()); ++p) {
        for (Timing timing : {Timing::now, Timing::after_first_capture}) {
            const auto candidate = project(input, route, timing, &input.purchases[p]);
            if (better(candidate, baseline) && better(candidate, best)) best = candidate;
        }
    }
    return best;
}
inline Decision choose_job(const Observation& input, const PrivateState& state, Profile profile = {}) {
    Decision special;
    if (input.safe_channel || input.safe_active_capture) {
        special.job = Job::preserve; special.first_action = "preserve-safe-channel-or-capture";
        special.abort_condition = "safety-or-stage-premise-failed"; return special;
    }
    if (input.needs_recovery) {
        special.job = Job::recover; special.first_action = "flee-then-recall-and-observe-spawn-recovery";
        special.abort_condition = "unsafe-recall"; return special;
    }
    if (input.count > input.routes.size() || input.purchase_count > input.purchases.size()) {
        special.abort_condition = "candidate-or-menu-cap-exceeded"; return special;
    }
    Decision best = bank_only(input);
    if (best.feasible && best.rank.immediate_win && input.fuel_remaining >= 16000) return best;
    if (input.fuel_remaining < 45000) {
        special.abort_condition = "selection-fuel-reserve"; return special;
    }
    Decision incumbent;
    // Commitment is common to both arms, including the exact saved timing and
    // family. Reoptimizing a saved route must not silently change its purchase.
    if (state.committed) {
        for (std::size_t i = 0; i < input.count; ++i) {
            const auto& route = input.routes[i];
            if (route.first.id != state.first_group || route.successor.id != state.successor_group) continue;
            if (state.timing == Timing::none) incumbent = project(input, route);
            else for (std::size_t j = 0; j < input.purchase_count; ++j)
                if (input.purchases[j].family == state.purchase_family)
                    incumbent = project(input, route, state.timing, &input.purchases[j]);
        }
    }
    if (profile.arm == Arm::route_first_control) {
        Decision route_best;
        std::size_t selected = input.count;
        for (std::size_t i = 0; i < input.count; ++i) {
            const auto route = project(input, input.routes[i]);
            if (better(route, route_best)) { route_best = route; selected = i; }
        }
        // Freeze both groups BEFORE considering the identical purchase menu.
        if (selected < input.count) {
            const auto bought = choose_purchase(input, input.routes[selected]);
            if (better(bought, best)) best = bought;
        }
    } else {
        for (std::size_t i = 0; i < input.count; ++i) {
            const auto candidate = choose_purchase(input, input.routes[i]);
            if (better(candidate, best)) best = candidate;
        }
    }
    if (incumbent.feasible && best.feasible && !best.rank.immediate_win &&
        *std::min_element(best.rank.M.begin(), best.rank.M.end()) <
        *std::min_element(incumbent.rank.M.begin(), incumbent.rank.M.end()) + 1) return incumbent;
    if (best.feasible) return best;
    for (std::size_t i = 0; i < input.count; ++i) {
        const auto& r = input.routes[i];
        if (!r.first.live_observed && leg_turns(r.to_first) >= 0) {
            special.job = Job::inspect; special.first_group = r.first.id;
            special.first_action = "inspect-assigned-first-group";
            special.abort_condition = "two-failed-observations-or-danger"; return special;
        }
    }
    return best;
}

enum class ProjectStage { purchase_now, first_production, deferred_purchase, second_production,
                          output_bank, renewal, complete, aborted };
struct ProjectFacts {
    int observation_round = -1, own_actor = -1, own_gold = -1, own_bag = -1;
    std::uint64_t own_capture_receipt = 0, own_purchase_receipt = 0, own_bank_receipt = 0;
    bool own_purchase_confirmed = false;
    int purchased_family = -1, purchased_item = -1, purchased_cost = -1, purchased_shards = 0;
    bool at_named_productive_waypoint = false;
    int productive_waypoint = -1;
};
struct Project {
    Decision selected{};
    execution::State execution_state{};
    ProjectStage stage = ProjectStage::aborted;
    std::uint64_t capture_baseline = 0, purchase_baseline = 0, bank_baseline = 0;
    bool purchase_intent = false, bank_intent = false, initialized = false;
    int purchase_intent_turn = -1, purchase_before_gold = -1, bank_intent_turn = -1;
    int capture_bag_baseline = -1, capture_gold_baseline = -1;
};
inline Project start_project(const Decision& selected, const ProjectFacts& facts) {
    Project p;
    p.selected = selected;
    if (!selected.feasible || selected.rank.status != AccountingStatus::feasible ||
        facts.own_actor != selected.schedule.actors[0].id || facts.observation_round != selected.observation_round ||
        facts.own_gold != selected.initial_gold || facts.own_bag != selected.initial_bag) return p;
    p.capture_baseline = facts.own_capture_receipt; p.purchase_baseline = facts.own_purchase_receipt;
    p.bank_baseline = facts.own_bank_receipt;
    p.capture_bag_baseline = facts.own_bag; p.capture_gold_baseline = facts.own_gold;
    if (selected.job == Job::two_groups)
        p.stage = selected.timing == Timing::now ? ProjectStage::purchase_now : ProjectStage::first_production;
    else if (selected.job == Job::bank_now) p.stage = ProjectStage::output_bank;
    return p;
}
inline execution::Decision execute_job(execution::Observation o, const ProjectFacts& facts, Project& p) {
    namespace e = execution;
    using Stage = ProjectStage;
    auto abort = [&](e::Reason reason) {
        p.stage = Stage::aborted; return e::emit(p.execution_state, e::ActionKind::yield, reason);
    };
    if (p.stage == Stage::complete || p.stage == Stage::aborted)
        return e::emit(p.execution_state, e::ActionKind::yield, e::Reason::illegal);
    if (facts.observation_round != o.owner_turn || facts.own_actor != p.selected.schedule.actors[0].id ||
        facts.own_gold < 0 || facts.own_bag < 0) return abort(e::Reason::illegal);
    e::consume_feedback(o, p.execution_state);
    auto enter = [&](Stage stage, e::Phase phase, int target) {
        p.stage = stage; e::enter(p.execution_state, phase, o); p.execution_state.target = target;
    };
    if (!p.initialized) {
        p.initialized = true;
        const bool shop = p.stage == Stage::purchase_now;
        e::enter(p.execution_state, shop ? e::Phase::shop :
            p.stage == Stage::output_bank ? e::Phase::bank : e::Phase::bootstrap, o);
        p.execution_state.target = p.selected.first_group;
    }
    // A valid running channel always keeps its common executor priority.
    if (o.channel != e::Channel::none) return e::decision(o, p.execution_state);
    if (!o.alive || !o.safe || o.needs_recovery || o.remaining_rounds <= 0)
        return abort(e::Reason::unsafe);
    const bool producing = p.stage == Stage::first_production || p.stage == Stage::second_production;
    if (producing && o.own_payout_observed) {
        const bool first = p.stage == Stage::first_production;
        const int target = first ? p.selected.first_group : p.selected.successor_group;
        const int gems = first ? p.selected.expected_first_gems : p.selected.expected_second_gems;
        const int gold = first ? p.selected.expected_first_gold : p.selected.expected_second_gold;
        if ((p.execution_state.phase != e::Phase::clear && p.execution_state.phase != e::Phase::hold) ||
            o.own_payout_group != target || o.payout_receipt <= p.capture_baseline ||
            facts.own_capture_receipt != o.payout_receipt || o.own_gems_received != gems ||
            o.own_gold_received != gold || (gems == 0 && gold == 0) ||
            facts.own_bag != static_cast<AccountingStock>(p.capture_bag_baseline) + gems ||
            facts.own_gold != static_cast<AccountingStock>(p.capture_gold_baseline) + gold)
            return abort(e::Reason::illegal);
        p.capture_baseline = o.payout_receipt;
        p.capture_bag_baseline = facts.own_bag; p.capture_gold_baseline = facts.own_gold;
        p.execution_state.consumed_payout = o.payout_receipt;
        o.own_payout_observed = false;
        if (first) {
            p.purchase_baseline = facts.own_purchase_receipt;
            p.purchase_intent = false;
            enter(p.selected.timing == Timing::after_first_capture ? Stage::deferred_purchase : Stage::second_production,
                  p.selected.timing == Timing::after_first_capture ? e::Phase::shop : e::Phase::bootstrap,
                  p.selected.successor_group);
        } else {
            p.bank_baseline = facts.own_bank_receipt; p.bank_intent = false;
            enter(Stage::output_bank, e::Phase::allocate, target);
        }
    }
    if (p.stage == Stage::purchase_now || p.stage == Stage::deferred_purchase) {
        // Anything observed before this stage has issued a buy is a baseline,
        // never a confirmation of an intent that does not yet exist.
        if (!p.purchase_intent) p.purchase_baseline = facts.own_purchase_receipt;
        const bool confirmed = p.purchase_intent && o.owner_turn >= p.purchase_intent_turn &&
            facts.own_purchase_confirmed && facts.own_purchase_receipt > p.purchase_baseline &&
            facts.purchased_family == p.selected.purchase_family && facts.purchased_item == p.selected.purchase_item &&
            facts.purchased_cost == p.selected.purchase_cost && facts.purchased_shards == 1 &&
            facts.own_gold == p.purchase_before_gold - p.selected.purchase_cost;
        if (confirmed) {
            const bool before_first = p.stage == Stage::purchase_now;
            p.purchase_baseline = facts.own_purchase_receipt; p.purchase_intent = false;
            p.capture_gold_baseline = facts.own_gold;
            enter(before_first ? Stage::first_production : Stage::second_production,
                  e::Phase::bootstrap, before_first ? p.selected.first_group : p.selected.successor_group);
        } else {
            if (p.purchase_intent && (facts.own_purchase_receipt > p.purchase_baseline ||
                facts.own_gold != p.purchase_before_gold)) return abort(e::Reason::illegal);
            if (facts.own_gold < p.selected.purchase_cost) return abort(e::Reason::illegal);
        }
    }
    if (p.stage == Stage::output_bank && p.bank_intent &&
        o.owner_turn > p.bank_intent_turn && facts.own_bank_receipt > p.bank_baseline && facts.own_bag == 0) {
        p.bank_baseline = facts.own_bank_receipt; p.bank_intent = false;
        if (p.selected.schedule.terminal_win) {
            p.stage = Stage::complete; return e::emit(p.execution_state, e::ActionKind::yield);
        }
        enter(Stage::renewal, e::Phase::renew, -1);
    }
    if (p.stage == Stage::output_bank && !p.bank_intent)
        p.bank_baseline = facts.own_bank_receipt;
    if (p.stage == Stage::renewal) {
        if (o.owner_turn - p.execution_state.phase_start >= p.execution_state.phase_budget)
            return abort(e::Reason::timeout);
        int waypoint = -1;
        for (int i = 0; i < p.selected.schedule.event_count; ++i)
            if (p.selected.schedule.events[i].kind == AccountingEventKind::return_to_work)
                waypoint = p.selected.schedule.events[i].waypoint;
        if (facts.own_bag != 0) return abort(e::Reason::illegal);
        if (facts.at_named_productive_waypoint && facts.productive_waypoint == waypoint) {
            p.stage = Stage::complete; return e::emit(p.execution_state, e::ActionKind::yield);
        }
        return e::move_intent(o, p.execution_state);
    }
    if (p.stage == Stage::output_bank && facts.own_bag == 0) return abort(e::Reason::illegal);
    const bool shopping = p.stage == Stage::purchase_now || p.stage == Stage::deferred_purchase;
    o.purchase_requested = shopping; o.purchase_id = p.selected.purchase_item;
    o.purchase_affordable = facts.own_gold >= p.selected.purchase_cost;
    o.purchase_is_equipment = true;
    o.bank_requested = p.stage == Stage::output_bank; o.own_bag = facts.own_bag;
    const int group = p.stage == Stage::first_production ? p.selected.first_group : p.selected.successor_group;
    if (p.stage == Stage::first_production || p.stage == Stage::second_production) {
        for (std::size_t i = 0; i < std::min(o.group_count, o.groups.size()); ++i)
            o.groups[i].assigned = o.groups[i].id == group;
    }
    const auto result = e::decision(o, p.execution_state);
    if (result.action.kind == e::ActionKind::buy) {
        p.purchase_intent = true; p.purchase_intent_turn = o.owner_turn;
        p.purchase_before_gold = facts.own_gold;
    }
    if (result.action.kind == e::ActionKind::start_tribute) {
        p.bank_intent = true; p.bank_intent_turn = o.owner_turn;
    }
    if (result.reason == e::Reason::timeout || result.reason == e::Reason::capture_unresolved ||
        result.reason == e::Reason::no_opportunity || result.reason == e::Reason::assembly_timeout)
        p.stage = Stage::aborted;
    return result;
}
} // namespace p5::workshop
