#pragma once
#include "profile.h"
#include "../../common/src/observe.h"
#include "../../common/src/accounting.h"
#include "../../common/src/execute.h"
#include <array>
#include <cstddef>
#include <optional>

namespace p5::dividend {
enum class Job { unavailable, inspect, recover, preserve, bank_now, produce_bank, bank_produce, finance_produce, cash_produce };
struct Purchase {
    int family = -1, shards = 0, cost = 0;
    double strength_gain = 0, health_gain = 0, actual_spawn_heal = 0, spell_damage_gain = 0;
    bool legal_at_spawn = false;
};
struct Opportunity {
    int group = -1, gems = 0, gold = 0;
    bool live_observed = false, reward_or_alive = false;
    CombatInput combat{};
    RouteLeg from_current{}, from_spawn{}, from_altar{}, to_altar{}, next_productive{};
    // Opt-in native returns: 0 is no purchase, family+1 is that exact shard.
    // Old offline inputs retain their single explicit next_productive route.
    bool variant_returns_known = false;
    std::array<RouteLeg, 4> variant_returns{};
};
// Optional synchronous native quote reuse. A provider may only replace the
// combat primitive; routes, financing, event accounting and ranking stay here.
// A missing quote invalidates the complete comparison, never just one option.
struct CombatQuoteProvider {
    void* context = nullptr;
    CombatForecast (*quote)(void*, const Opportunity&, const CombatInput&) = nullptr;
    bool complete = true;
};
struct Observation {
    int round = 0, team_score = 0, pick_ordinal = 0;
    ActorObservation self{};
    CombatActor stats{};
    EconomicSettings settings{};
    FinishWindows windows{};
    RouteLeg current_to_altar{}, altar_to_spawn{}, bank_return{}, current_to_spawn{};
    std::array<Opportunity, 8> opportunities{};
    std::size_t count = 0;
    std::array<Purchase, 3> purchases{};
    std::size_t purchase_count = 0;
    bool bank_legal = false, bank_route_safe = false, safe_channel = false, safe_hold = false;
    int fuel_remaining = 100000;
};
struct PrivateState {
    bool committed = false;
    int group = -1;
    Job job = Job::unavailable;
};
// Current solo finance is the largest path: two initial-bank events, one
// purchase, one capture, two output-bank events and one productive return.
// Coalition or additional-capture paths must extend this storage first.
using DividendSoloSchedule = AccountingScheduleStorage<1, 7>;
struct Decision {
    bool feasible = false;
    Job job = Job::unavailable;
    int group = -1, purchase_family = -1;
    DividendSoloSchedule schedule{};
    AccountingResult rank{};
    CycleForecast forecast{};
    const char* first_action = "yield";
    const char* abort_condition = "unavailable";
    double purchase_strength = 0, purchase_max_health = 0, purchase_basic_modifier = 0, purchase_min_health = 0;
};
inline DividendSoloSchedule initial_schedule(const Observation& input, int group) {
    DividendSoloSchedule out;
    const auto self = private_view(input.self, input.round);
    out.actor_count = 1; out.target_id = group; out.doctrine_id = 1;
    out.starting_team_score = input.team_score; out.win_score = input.settings.winning_gems;
    out.tribute_gems_per_tick = input.settings.tribute_gems_per_tick;
    out.tribute_min_ticks = input.settings.tribute_min_ticks;
    out.actors[0].id = self.id; out.actors[0].self = true; out.actors[0].visible = true;
    out.actors[0].bag_known = self.bag.current(input.round);
    out.actors[0].bag = self.bag.value; out.actors[0].gold = self.gold.value;
    // Only the fixed collector Marcella receives her own observed tribute income.
    out.actors[0].tribute_gold_per_gem = input.pick_ordinal == 0 ? input.settings.marcella_gold_per_gem : 0;
    return out;
}
inline void event(DividendSoloSchedule& schedule, AccountingEventKind kind, int tick,
                  int amount = 0, int gold = 0, int group = -1, int waypoint = -1) {
    if (schedule.event_count < 0 || schedule.event_count >= int(schedule.events.size())) {
        // Keep the entire schedule invalid after overflow; never publish a
        // truncated prefix or let a later append restore a valid count.
        schedule.event_count = std::max(schedule.event_count, int(schedule.events.size()) + 1);
        return;
    }
    auto& item = schedule.events[schedule.event_count];
    item.id = schedule.event_count++; item.tick = tick; item.kind = kind; item.actor = 0;
    item.gems = amount; item.gold = gold; item.group = group; item.waypoint = waypoint;
    if (kind == AccountingEventKind::capture) item.participants = item.holders = 1;
}
inline void bank(DividendSoloSchedule& schedule, const EconomicSettings& settings, int start, int bag) {
    event(schedule, AccountingEventKind::tribute_start, start, -1);
    event(schedule, AccountingEventKind::tribute_complete, start + settings.channel_ticks(bag), -1);
}
inline Decision bank_only(const Observation& input) {
    Decision out;
    const auto self = private_view(input.self, input.round);
    const int travel = input.current_to_altar.distance == 0 ? 0 : input.current_to_altar.turns();
    const int back = input.bank_return.turns();
    const int complete = travel + input.settings.channel_ticks(self.bag.value);
    const bool winning = input.team_score + self.bag.value >= input.settings.winning_gems &&
        complete <= input.windows.official_remaining;
    if (!input.bank_legal || !input.bank_route_safe || !self.bag.current(input.round) ||
        self.bag.value <= 0 || travel < 0 || !input.current_to_altar.safety_checked ||
        (!winning && (back < 0 || !input.bank_return.safety_checked))) return out;
    out.schedule = initial_schedule(input, input.bank_return.destination);
    bank(out.schedule, input.settings, travel, self.bag.value);
    if (winning) {
        out.schedule.terminal_win = true; out.schedule.terminal_win_tick = complete;
        out.schedule.immediate_legal_winning_bank = input.current_to_altar.distance == 0;
        out.schedule.actors[0].occupied_turns = complete;
    } else {
        event(out.schedule, AccountingEventKind::return_to_work, complete + back, 0, 0, -1, input.bank_return.destination);
        out.schedule.actors[0].occupied_turns = complete + back;
    }
    out.rank = forecast_schedule(out.schedule, input.windows);
    out.feasible = out.rank.status == AccountingStatus::feasible;
    out.job = Job::bank_now; out.first_action = "travel-to-altar-or-tribute";
    out.abort_condition = "unsafe-route-or-channel-invalid";
    return out;
}
inline bool valid_purchase(const Purchase& purchase) {
    // The first offline increment commissions one legal shard per decision.
    return purchase.family >= 0 && purchase.family < 3 && purchase.shards == 1 && purchase.cost > 0 &&
        purchase.legal_at_spawn && purchase.strength_gain >= 0 && purchase.health_gain >= 0 &&
        purchase.actual_spawn_heal >= 0 && purchase.actual_spawn_heal <= purchase.health_gain &&
        purchase.spell_damage_gain >= 0;
}
inline bool finance_eligible(const Observation& input, const Purchase& purchase) {
    return input.pick_ordinal == 0 && input.self.bag.value > 0 && valid_purchase(purchase) &&
        input.self.gold.value < purchase.cost &&
        AccountingStock(input.self.gold.value) + AccountingStock(input.self.bag.value) *
            input.settings.marcella_gold_per_gem >= purchase.cost;
}
inline bool cash_eligible(const Observation& input, const Purchase& purchase) {
    return input.pick_ordinal == 0 && valid_purchase(purchase) && input.self.gold.value >= purchase.cost;
}
inline CycleForecast quoted_cycle(CycleCandidate& candidate, const EconomicSettings& settings,
                                  const Opportunity& opportunity, CombatQuoteProvider* quotes) {
    if (!quotes) return forecast_cycle(candidate, settings);
    CycleForecast out;
    if (!quotes->complete || !quotes->quote) { quotes->complete = false; return out; }
    if (!settings.valid() || candidate.starting_bag < 0 || candidate.reward_gems <= 0) return out;
    const int travel = candidate.to_group.turns(), conversion = candidate.to_altar.turns();
    const int renewal = candidate.return_to_work.turns();
    if (travel < 0 || conversion < 0 || renewal < 0 || !candidate.to_group.safety_checked ||
        !candidate.to_altar.safety_checked || !candidate.return_to_work.safety_checked) {
        out.state = EstimateState::unknown; out.reason = "unwitnessed-route"; return out;
    }
    candidate.combat.hold_ticks = settings.capture_rounds + 1;
    for (std::size_t i = 0; i < candidate.combat.actor_count && i < 6; ++i)
        candidate.combat.actors[i].shield_expiry_tick -= travel;
    out.combat = quotes->quote(quotes->context, opportunity, candidate.combat);
    if (!quotes->complete) return {};
    out.state = out.combat.state; out.reason = out.combat.reason;
    if (out.state != EstimateState::feasible) return out;
    out.capture_tick = travel + out.combat.clear_ticks + settings.capture_rounds + 1;
    out.bank_tick = out.capture_tick + conversion + settings.channel_ticks(candidate.starting_bag + candidate.reward_gems);
    out.renewal_tick = out.bank_tick + renewal;
    return out;
}
inline Decision project(const Observation& input, const Opportunity& opportunity,
                        bool bank_first, const Purchase* purchase = nullptr,
                        CombatQuoteProvider* quotes = nullptr, bool cash_first = false) {
    Decision out;
    const auto self = private_view(input.self, input.round);
    if (!input.bank_legal || !input.bank_route_safe || !opportunity.live_observed || !opportunity.reward_or_alive || opportunity.group < 0 ||
        opportunity.gems <= 0 || opportunity.gold < 0 || !self.bag.current(input.round) ||
        !self.gold.current(input.round) || (purchase && ((!bank_first && !cash_first) || !valid_purchase(*purchase))) ||
        (cash_first && (!purchase || bank_first))) return out;
    // This increment admits solo schedules; a visible rendezvous is handled by
    // the common stage executor, never invented in an economic forecast.
    if (opportunity.combat.actor_count != 1) return out;
    if (purchase && !(cash_first ? cash_eligible(input, *purchase) : finance_eligible(input, *purchase)))
        return out; // Ordinary cash is distinct from tribute-created affordability.
    CycleCandidate candidate;
    candidate.combat = opportunity.combat;
    candidate.combat.actors[0] = input.stats;
    candidate.combat.actors[0].self = true;
    candidate.starting_bag = bank_first ? 0 : self.bag.value;
    candidate.reward_gems = opportunity.gems;
    const auto& productive_return = opportunity.variant_returns_known ?
        opportunity.variant_returns[purchase ? purchase->family + 1 : 0] : opportunity.next_productive;
    candidate.to_altar = opportunity.to_altar; candidate.return_to_work = productive_return;
    candidate.to_group = cash_first ? opportunity.from_spawn :
        bank_first ? (purchase ? opportunity.from_spawn : opportunity.from_altar) : opportunity.from_current;
    int prefix = 0;
    out.schedule = initial_schedule(input, opportunity.group);
    if (bank_first) {
        const int travel = input.current_to_altar.distance == 0 ? 0 : input.current_to_altar.turns();
        if (!input.bank_legal || !input.bank_route_safe || travel < 0 || self.bag.value <= 0 ||
            !input.current_to_altar.safety_checked) return out;
        bank(out.schedule, input.settings, travel, self.bag.value);
        prefix = travel + input.settings.channel_ticks(self.bag.value);
        if (purchase) {
            const int shopping = input.altar_to_spawn.turns();
            if (shopping < 0 || !input.altar_to_spawn.safety_checked) return out;
            prefix += shopping + 1;
        }
    }
    if (cash_first) {
        const int witnessed = input.current_to_spawn.turns();
        if (witnessed < 0 || !input.current_to_spawn.safety_checked) return out;
        const int travel = input.current_to_spawn.distance == 0 ? 0 : witnessed;
        prefix = travel + 1;
    }
    if (purchase) {
        event(out.schedule, AccountingEventKind::purchase, prefix, 0, purchase->cost);
        auto& stats = candidate.combat.actors[0];
        stats.strength += purchase->strength_gain; stats.max_health += purchase->health_gain;
        stats.health = std::min(stats.max_health, stats.health + purchase->actual_spawn_heal);
        stats.direct_or_reset_damage += purchase->spell_damage_gain;
        out.purchase_family = purchase->family;
        out.purchase_strength = stats.strength; out.purchase_max_health = stats.max_health;
        out.purchase_basic_modifier = stats.basic_modifier; out.purchase_min_health = stats.health;
    }
    candidate.combat.actors[0].shield_expiry_tick -= prefix;
    out.forecast = quoted_cycle(candidate, input.settings, opportunity, quotes);
    if (out.forecast.state != EstimateState::feasible) return out;
    event(out.schedule, AccountingEventKind::capture, prefix + out.forecast.capture_tick,
          opportunity.gems, opportunity.gold, opportunity.group);
    const int bag = candidate.starting_bag + opportunity.gems;
    bank(out.schedule, input.settings, prefix + out.forecast.bank_tick - input.settings.channel_ticks(bag), bag);
    const int endpoint = prefix + out.forecast.renewal_tick;
    event(out.schedule, AccountingEventKind::return_to_work, endpoint, 0, 0, -1, productive_return.destination);
    out.schedule.actors[0].occupied_turns = endpoint;
    out.rank = forecast_schedule(out.schedule, input.windows);
    out.feasible = out.rank.status == AccountingStatus::feasible;
    out.group = opportunity.group;
    out.job = cash_first ? Job::cash_produce : purchase ? Job::finance_produce : bank_first ? Job::bank_produce : Job::produce_bank;
    out.first_action = cash_first ? "travel-to-spawn-or-buy" : bank_first ? "travel-to-altar-or-tribute" : "travel-to-production";
    out.abort_condition = "reobserve-payout-affordability-route-readiness-and-survival";
    return out;
}
inline Decision purchase_from_baseline(const Observation& input, const Opportunity& opportunity,
                                      const Decision& no_purchase, CombatQuoteProvider* quotes) {
    if (quotes && !quotes->complete) return {};
    Decision best = no_purchase;
    for (std::size_t p = 0; p < input.purchase_count && p < 3; ++p) {
        if (!finance_eligible(input, input.purchases[p])) continue;
        const auto option = project(input, opportunity, true, &input.purchases[p], quotes);
        if (quotes && !quotes->complete) return {};
        // A purchase must repay for this same productive group; each kit owes
        // the complete safe return explicitly supplied for that exact variant.
        if (option.feasible && (!no_purchase.feasible || compare_results(option.rank, no_purchase.rank) > 0) &&
            (!best.feasible || compare_results(option.rank, best.rank) > 0)) best = option;
    }
    if (quotes && !quotes->complete) return {};
    return best;
}
inline Decision choose_purchase(const Observation& input, const Opportunity& opportunity,
                                CombatQuoteProvider* quotes = nullptr) {
    if (quotes && !quotes->complete) return {};
    if (input.self.bag.value <= 0) return {};
    const auto baseline = project(input, opportunity, true, nullptr, quotes);
    return purchase_from_baseline(input, opportunity, baseline, quotes);
}
inline Decision choose_job(const Observation& input, const PrivateState& state, Profile profile,
                           CombatQuoteProvider* quotes = nullptr) {
    Decision best;
    if (input.safe_channel || input.safe_hold || input.fuel_remaining < 16000) {
        best.feasible = input.safe_channel || input.safe_hold;
        best.job = Job::preserve; best.first_action = input.safe_channel ? "preserve-channel" : input.safe_hold ? "hold" : "yield";
        return best;
    }
    if (quotes && (!quotes->complete || !quotes->quote)) { quotes->complete = false; return best; }
    const auto self = private_view(input.self, input.round);
    if (!input.settings.valid() || !input.windows.valid || input.count > 8 || input.purchase_count > 3 ||
        !self.self || !self.bag.current(input.round) || !self.gold.current(input.round)) return best;
    if (self.bag.value > 0) best = bank_only(input);
    if (best.feasible && best.rank.immediate_win) return best;
    if (input.fuel_remaining < 45000) return best;
    std::optional<Decision> incumbent;
    int ordinary_group = -1;
    AccountingResult ordinary_rank;
    bool any_finance = false, any_cash = false;
    for (std::size_t p = 0; p < input.purchase_count; ++p) {
        any_finance |= finance_eligible(input, input.purchases[p]);
        any_cash |= cash_eligible(input, input.purchases[p]) && input.current_to_spawn.turns() >= 0 &&
            input.current_to_spawn.safety_checked;
    }
    const auto ordinary = [&](const Decision& candidate) {
        if (candidate.feasible && (ordinary_group < 0 || compare_results(candidate.rank, ordinary_rank) > 0)) {
            ordinary_group = candidate.group; ordinary_rank = candidate.rank;
        }
    };
    for (std::size_t i = 0; i < input.count; ++i) {
        // Duplicate public group IDs do not create duplicate candidates.
        bool duplicate = false;
        for (std::size_t j = 0; j < i; ++j) duplicate |= input.opportunities[j].group == input.opportunities[i].group;
        if (duplicate) continue;
        const auto& opportunity = input.opportunities[i];
        auto route = project(input, opportunity, false, nullptr, quotes);
        if (quotes && !quotes->complete) return {};
        ordinary(route);
        if (route.feasible && (!best.feasible || compare_results(route.rank, best.rank) > 0)) best = route;
        if (state.committed && state.group == opportunity.group && state.job == route.job) incumbent.emplace(route);
        // With no bag there is no legal initial tribute or finance. Do not
        // construct five rejected 64-event Decisions for that known condition.
        if (self.bag.value > 0) {
            auto bank_route = project(input, opportunity, true, nullptr, quotes);
            if (quotes && !quotes->complete) return {};
            ordinary(bank_route);
            if (bank_route.feasible && (!best.feasible || compare_results(bank_route.rank, best.rank) > 0)) best = bank_route;
            if (state.committed && state.group == opportunity.group && state.job == bank_route.job) incumbent.emplace(bank_route);
            if (profile.arm == Arm::active && any_finance) {
                auto financed = purchase_from_baseline(input, opportunity, bank_route, quotes);
                if (quotes && !quotes->complete) return {};
                if (state.committed && state.group == opportunity.group && state.job == financed.job) incumbent.emplace(financed);
                if (financed.feasible && (!best.feasible || compare_results(financed.rank, best.rank) > 0)) best = financed;
            }
        }
    }
    // Control selects the productive route before considering tribute finance.
    // It may invest for that frozen route, preserving the same menu and passive.
    if (profile.arm == Arm::route_first_control && best.feasible && best.group >= 0 && any_finance) {
        for (std::size_t i = 0; i < input.count; ++i) if (input.opportunities[i].group == best.group) {
            const auto financed = choose_purchase(input, input.opportunities[i], quotes);
            if (financed.feasible && compare_results(financed.rank, best.rank) > 0) best = financed;
            break;
        }
    }
    if (quotes && !quotes->complete) return {};
    // Common ordinary-capital fallback. Freeze the route from the same no-buy
    // comparison in both arms, independently of active's finance winner. If no
    // no-buy route works, both arms compare all cash-enabled complete routes.
    if (any_cash) for (std::size_t i = 0; i < input.count; ++i) {
        const auto& opportunity = input.opportunities[i];
        if (ordinary_group >= 0 && opportunity.group != ordinary_group) continue;
        bool duplicate = false;
        for (std::size_t j = 0; j < i; ++j) duplicate |= input.opportunities[j].group == opportunity.group;
        if (duplicate) continue;
        for (std::size_t p = 0; p < input.purchase_count; ++p) if (cash_eligible(input, input.purchases[p])) {
            auto funded = project(input, opportunity, false, &input.purchases[p], quotes, true);
            if (quotes && !quotes->complete) return {};
            if (state.committed && state.group == opportunity.group && state.job == funded.job) incumbent.emplace(funded);
            if (funded.feasible && (!best.feasible || compare_results(funded.rank, best.rank) > 0)) best = funded;
        }
    }
    if (incumbent && incumbent->feasible && best.feasible &&
        *std::min_element(best.rank.M.begin(), best.rank.M.end()) <
        *std::min_element(incumbent->rank.M.begin(), incumbent->rank.M.end()) + 1) return *incumbent;
    if (!best.feasible && input.count) {
        best.job = Job::inspect; best.first_action = "inspect-assigned-unknown-or-visible-rendezvous";
        best.abort_condition = "two-failed-inspections-or-unsafe-coalition";
    }
    return best;
}

// This private controller binds the economic choice to physical stage intents.
// Facts below must be reconstructed from fresh own observations/action results;
// forecast event timestamps never count as a completion signal.
enum class ProjectStage { initial_bank, purchase, production, output_bank, renewal, complete, aborted, purchase_first, purchase_inspection };
struct ProjectFacts {
    int observation_round = -1, own_gold = -1, own_bag = -1;
    std::uint64_t own_bank_receipt = 0, own_purchase_receipt = 0, own_capture_receipt = 0;
    bool own_purchase_confirmed = false;
    int purchased_family = -1, purchased_cost = 0, purchased_count = 0;
    CombatActor own_stats{};
    bool own_stats_known = false;
    bool at_named_productive_waypoint = false;
    int productive_waypoint = -1;
    bool purchase_source_reached = false, purchase_tail_safe = false, purchase_tail_checked = false;
    bool purchase_targets_unavailable = false;
};
struct Project {
    Decision selected{};
    execution::State execution_state{};
    ProjectStage stage = ProjectStage::aborted;
    std::uint64_t bank_baseline = 0, purchase_baseline = 0, payout_baseline = 0;
    bool bank_intent_outstanding = false;
    int bank_intent_round = -1;
    bool purchase_intent_outstanding = false;
    int purchase_intent_round = -1;
    int purchase_intent_gold = -1, purchase_intent_bag = -1;
};
inline Project start_project(const Decision& selected, const ProjectFacts& facts) {
    Project out;
    out.selected = selected;
    out.bank_baseline = facts.own_bank_receipt; out.purchase_baseline = facts.own_purchase_receipt;
    out.payout_baseline = facts.own_capture_receipt;
    if (selected.feasible && selected.rank.status == AccountingStatus::feasible &&
        (selected.job == Job::produce_bank || selected.job == Job::bank_now ||
         selected.job == Job::bank_produce || selected.job == Job::finance_produce || selected.job == Job::cash_produce)) {
        out.stage = selected.job == Job::cash_produce ? ProjectStage::purchase_first :
            selected.job == Job::produce_bank ? ProjectStage::production : ProjectStage::initial_bank;
        if (out.stage == ProjectStage::purchase_first) {
            // Bind the shop goal before any executor call can emit a buy.
            out.execution_state.phase = execution::Phase::shop;
            out.execution_state.phase_start = facts.observation_round;
            out.execution_state.phase_budget = std::max(8, 2 * std::clamp(selected.rank.occupied_turns, 0, 1000000) + 4);
        }
    }
    return out;
}
inline execution::Decision execute_job(execution::Observation observed, const ProjectFacts& facts, Project& project) {
    using Stage = ProjectStage;
    namespace e = execution;
    if (facts.observation_round != observed.owner_turn || facts.own_gold < 0 || facts.own_bag < 0 ||
        project.stage == Stage::aborted || project.stage == Stage::complete)
        return e::emit(project.execution_state, e::ActionKind::yield, e::Reason::illegal);
    e::consume_feedback(observed, project.execution_state);
    auto transition = [&](Stage next, e::Phase phase) {
        project.stage = next;
        e::enter(project.execution_state, phase, observed);
        project.execution_state.target = project.selected.group;
    };
    const bool completed_own_bank = project.bank_intent_outstanding &&
        observed.owner_turn > project.bank_intent_round && facts.own_bag == 0 &&
        facts.own_bank_receipt > project.bank_baseline;
    if (project.stage == Stage::initial_bank && completed_own_bank) {
        project.bank_baseline = facts.own_bank_receipt;
        project.bank_intent_outstanding = false;
        project.purchase_baseline = facts.own_purchase_receipt;
        project.purchase_intent_outstanding = false;
        transition(project.selected.job == Job::finance_produce ? Stage::purchase :
                   project.selected.job == Job::bank_produce ? Stage::production : Stage::renewal,
                   project.selected.job == Job::finance_produce ? e::Phase::shop : e::Phase::bootstrap);
    }
    if (project.stage == Stage::purchase || project.stage == Stage::purchase_first) {
        int cost = 0;
        for (int i = 0; i < project.selected.schedule.event_count; ++i)
            if (project.selected.schedule.events[i].kind == AccountingEventKind::purchase)
                cost = static_cast<int>(project.selected.schedule.events[i].gold);
        const auto& actual = facts.own_stats;
        const bool kit = facts.own_stats_known && actual.id == project.selected.schedule.actors[0].id && actual.self && actual.visible &&
            std::isfinite(actual.health) && std::isfinite(actual.max_health) && std::isfinite(actual.strength) &&
            std::isfinite(actual.basic_modifier) && actual.health > 0 && actual.health >= project.selected.purchase_min_health &&
            actual.max_health >= actual.health && actual.strength >= project.selected.purchase_strength &&
            actual.max_health >= project.selected.purchase_max_health && actual.basic_modifier >= project.selected.purchase_basic_modifier;
        const bool purchased = project.purchase_intent_outstanding && observed.owner_turn >= project.purchase_intent_round &&
            facts.own_purchase_confirmed && facts.own_purchase_receipt > project.purchase_baseline &&
            facts.purchased_family == project.selected.purchase_family && facts.purchased_cost == cost && facts.purchased_count == 1 &&
            facts.own_gold == project.purchase_intent_gold - cost && facts.own_bag == project.purchase_intent_bag && kit;
        if (purchased) {
            project.purchase_baseline = facts.own_purchase_receipt;
            project.purchase_intent_outstanding = false;
            transition(Stage::purchase_inspection, e::Phase::bootstrap);
            // The shop packet cannot authorize distant work, even if it also
            // claims to be at the source. Bind a new nonproduction goal first.
            return e::emit(project.execution_state, e::ActionKind::refresh);
        } else if (facts.own_gold < cost || (project.purchase_intent_outstanding && facts.own_purchase_receipt > project.purchase_baseline)) {
            project.stage = Stage::aborted;
            return e::emit(project.execution_state, e::ActionKind::yield, e::Reason::illegal);
        }
    }
    if (project.stage == Stage::purchase_inspection) {
        if (observed.channel != e::Channel::none) return e::decision(observed, project.execution_state);
        if (!observed.alive || !observed.safe || observed.needs_recovery || observed.remaining_rounds <= 0 ||
            observed.owner_turn - project.execution_state.phase_start >= project.execution_state.phase_budget) {
            project.stage = Stage::aborted;
            return e::emit(project.execution_state, e::ActionKind::yield, e::Reason::unsafe);
        }
        // A known changed source/successor invalidates this named fight-tail.
        // End only the project; the actual purchased kit and receipts remain.
        if (facts.purchase_targets_unavailable) {
            project.stage = Stage::aborted;
            return e::emit(project.execution_state, e::ActionKind::yield, e::Reason::no_opportunity);
        }
        if (observed.fuel < observed.action_reserve)
            return e::emit(project.execution_state, e::ActionKind::yield, e::Reason::low_fuel);
        if (observed.forecast_turns < 0)
            return e::emit(project.execution_state, e::ActionKind::yield, e::Reason::unknown_target);
        const bool at_source = observed.at_target && observed.group_id == project.selected.group;
        if (at_source || facts.purchase_source_reached) {
            // A failed proof while below the planning reserve is not evidence
            // that the actual bought kit has become unsafe.
            if (observed.fuel < observed.selection_reserve)
                return e::emit(project.execution_state, e::ActionKind::yield, e::Reason::low_fuel);
            if (!at_source || !facts.purchase_source_reached) {
                project.stage = Stage::aborted;
                return e::emit(project.execution_state, e::ActionKind::yield, e::Reason::unsafe);
            }
            // Unknown availability or an interrupted quote is not an unsafe
            // completed proof. Keep the original bounded inspection clock.
            if (!facts.purchase_tail_checked)
                return e::emit(project.execution_state, e::ActionKind::yield, e::Reason::unknown_target);
            if (!facts.purchase_tail_safe) {
                project.stage = Stage::aborted;
                return e::emit(project.execution_state, e::ActionKind::yield, e::Reason::unsafe);
            }
            transition(Stage::production, e::Phase::bootstrap);
            return e::emit(project.execution_state, e::ActionKind::refresh);
        }
        return e::move_intent(observed, project.execution_state);
    }
    if (project.stage == Stage::production &&
        (project.execution_state.phase == e::Phase::hold || project.execution_state.phase == e::Phase::clear) &&
        observed.own_payout_observed &&
        observed.own_payout_group == project.selected.group && observed.payout_receipt > project.payout_baseline &&
        (observed.own_gems_received > 0 || observed.own_gold_received > 0)) {
        project.payout_baseline = observed.payout_receipt;
        // Older banks cannot liquidate the just-observed payout.
        project.bank_baseline = facts.own_bank_receipt;
        project.bank_intent_outstanding = observed.channel == e::Channel::tribute;
        project.bank_intent_round = observed.owner_turn;
        transition(Stage::output_bank, e::Phase::allocate);
    }
    if (project.stage == Stage::output_bank && completed_own_bank &&
        facts.own_bank_receipt > project.bank_baseline) {
        project.bank_baseline = facts.own_bank_receipt;
        project.bank_intent_outstanding = false;
        transition(Stage::renewal, e::Phase::renew);
    }
    if (project.stage == Stage::renewal) {
        int waypoint = -1;
        for (int i = 0; i < project.selected.schedule.event_count; ++i)
            if (project.selected.schedule.events[i].kind == AccountingEventKind::return_to_work)
                waypoint = project.selected.schedule.events[i].waypoint;
        if (observed.channel != e::Channel::none) return e::decision(observed, project.execution_state);
        if (!observed.safe || observed.needs_recovery || observed.remaining_rounds <= 0 ||
            observed.owner_turn - project.execution_state.phase_start >= project.execution_state.phase_budget) {
            project.stage = Stage::aborted;
            return e::emit(project.execution_state, e::ActionKind::yield, e::Reason::unsafe);
        }
        if (facts.at_named_productive_waypoint && facts.productive_waypoint == waypoint && facts.own_bag == 0) {
            project.stage = Stage::complete;
            return e::emit(project.execution_state, e::ActionKind::yield);
        }
        return e::move_intent(observed, project.execution_state);
    }
    observed.bank_requested = project.stage == Stage::initial_bank || project.stage == Stage::output_bank;
    observed.purchase_requested = project.stage == Stage::purchase || project.stage == Stage::purchase_first;
    observed.purchase_id = project.selected.purchase_family;
    observed.own_bag = facts.own_bag;
    if (project.stage == Stage::initial_bank) {
        // A lost bag is not proof of a completed tribute.
        if (facts.own_bag == 0 && observed.channel == e::Channel::none) {
            project.stage = Stage::aborted;
            return e::emit(project.execution_state, e::ActionKind::yield, e::Reason::illegal);
        }
        if (project.execution_state.phase == e::Phase::bootstrap) e::enter(project.execution_state, e::Phase::bank, observed);
    }
    if (project.stage == Stage::output_bank && facts.own_bag == 0 && observed.channel == e::Channel::none) {
        project.stage = Stage::aborted;
        return e::emit(project.execution_state, e::ActionKind::yield, e::Reason::illegal);
    }
    if (project.stage == Stage::production)
        for (std::size_t i = 0; i < observed.group_count && i < observed.groups.size(); ++i)
            observed.groups[i].assigned = observed.groups[i].id == project.selected.group;
    const auto result = e::decision(observed, project.execution_state);
    if (result.action.kind == e::ActionKind::start_tribute) {
        project.bank_intent_outstanding = true;
        project.bank_intent_round = observed.owner_turn;
    }
    if (result.action.kind == e::ActionKind::buy) {
        project.purchase_intent_outstanding = true;
        project.purchase_intent_round = observed.owner_turn;
        project.purchase_intent_gold = facts.own_gold; project.purchase_intent_bag = facts.own_bag;
    }
    if (result.reason == e::Reason::timeout || result.reason == e::Reason::assembly_timeout ||
        result.reason == e::Reason::capture_unresolved) project.stage = Stage::aborted;
    return result;
}
} // namespace p5::dividend
