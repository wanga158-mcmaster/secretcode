#pragma once
#include "observe.h"
#include "crew_forecast.h"
#include <exception>
#include <memory>
#include <utility>

namespace p6 {
enum class Stage { produce_or_approach, observe_admission, commit, execute,
                   verify, release_or_repair };
enum class Phase { bootstrap, travel, assemble, clear, hold, allocate, bank, recover };
enum class Action { yield, inspect, move, attack, cast, hold, tribute, recall,
                    preserve_channel, recover_on_spawn };
struct ProjectID {
    int group = -1, epoch = -1;
    bool operator==(const ProjectID&) const = default;
};
struct ActorState {
    struct EpochBudget { int group = -1, epoch = -1, repairs = 0; bool retired = false; };
    std::array<EpochBudget, 8> epoch_budgets{};
    ProjectID project{};
    Stage stage = Stage::produce_or_approach;
    Phase phase = Phase::bootstrap;
    Failure failure = Failure::none;
    int role = -1, owner_turn = 0, phase_started = 0, phase_limit = 8;
    int waits = 0, repairs = 0, hold_extensions = 0, failed_inspections = 0;
    int retired_group = -1, retired_epoch = -1;
    int last_round = -1;
    std::int64_t payout_bag_baseline = 0, payout_gold_baseline = 0;
    bool active = false, payout_seen = false, bank_seen = false;
    bool empty_liquidation = false; // Actual zero-bag payout awaiting physical renewal; no bank receipt.
    bool previous_observed = false;
    std::int64_t previous_bag = 0, previous_gold = 0, previous_score = 0;
    Channel previous_channel = Channel::none;
    bool tribute_started = false;
    int tribute_started_round = -1;
    std::int64_t tribute_started_bag = 0, tribute_started_score = 0;
    EpochBudget* budget(int group, int epoch) {
        for (auto& entry : epoch_budgets)
            if (entry.group == group && entry.epoch == epoch) return &entry;
        return nullptr;
    }
    // Ledger eligibility is also queried for a possible replacement before
    // committing it. Ownership and conversion locks remain caller decisions.
    bool epoch_eligible(int group, int round) const {
        for (const auto& entry : epoch_budgets)
            if (entry.group == group && entry.epoch == round / 64) return !entry.retired;
        return true;
    }
    bool may_admit(int group, int round) const {
        return !active && epoch_eligible(group, round);
    }
    bool admit(int group, int round, int forecast, int remaining_rounds,
               std::int64_t bag, std::int64_t gold) {
        if (round < 0 || group < 0 || forecast < 0 || remaining_rounds <= 0 ||
            !may_admit(group, round)) return false;
        project = {group, round / 64};
        auto* history = budget(group, project.epoch);
        if (!history) {
            for (auto& entry : epoch_budgets) if (entry.group < 0 || entry.epoch < project.epoch) {
                entry = {group, project.epoch, 0, false}; history = &entry; break;
            }
        }
        if (!history) return false; // Draft cap exceeded: unavailable, not forgotten history.
        active = true; waits = hold_extensions = 0; repairs = history->repairs;
        payout_seen = bank_seen = empty_liquidation = false;
        failure = Failure::none;
        tribute_started = false;
        payout_bag_baseline = bag; payout_gold_baseline = gold;
        phase = Phase::travel; stage = Stage::commit; phase_started = owner_turn;
        const auto bound = std::max<std::int64_t>(8, 2LL * forecast + 4);
        phase_limit = static_cast<int>(std::min<std::int64_t>(remaining_rounds, bound));
        return true;
    }
    void enter(Phase next, Stage reason) {
        phase = next; stage = reason; phase_started = owner_turn;
    }
    void retire(Failure why) {
        if (auto* history = budget(project.group, project.epoch)) history->retired = true;
        else for (auto& entry : epoch_budgets) if (entry.group < 0 || entry.epoch < project.epoch) {
            entry = {project.group, project.epoch, repairs, true}; break;
        }
        retired_group = project.group; retired_epoch = project.epoch;
        active = false; failure = why; waits = 0; empty_liquidation = false;
        stage = Stage::release_or_repair; phase = Phase::bootstrap;
    }
    bool repair() {
        if (!active || repairs != 0) return false;
        auto* history = budget(project.group, project.epoch);
        if (!history || history->repairs != 0) return false;
        ++history->repairs;
        ++repairs; waits = 0; stage = Stage::release_or_repair;
        return true;
    }
    bool wait_once() { return waits < 8 ? (++waits, true) : false; }
};
// Call only with an observed successful self action, after the legal executor
// returns. A proposal cannot supply this receipt. Needed for one-tick channels
// which begin and complete entirely between owner turn-start observations.
inline bool confirm_tribute_started(ActorState& state, const Frame& observed,
                                   const Observation<Action>& successful_action) {
    if (!observed.valid() || !state.active || state.phase != Phase::bank ||
        !successful_action.current(observed.clock, observed.clock.self) ||
        successful_action.value != Action::tribute || observed.own.actor.bag <= 0 ||
        state.last_round != observed.clock.round) return false;
    state.tribute_started = true; state.tribute_started_round = observed.clock.round;
    state.tribute_started_bag = observed.own.actor.bag;
    state.tribute_started_score = observed.own_team_score;
    return true;
}
struct StepProposal {
    Action action = Action::yield;
    Stage stage = Stage::produce_or_approach;
    Phase phase = Phase::bootstrap;
    Failure reason = Failure::none;
    int group = -1;
};
// Input observations describe this actor's actual local event, not a replay event.
struct Completion {
    ViewClock clock{};
    int group = -1;
    bool own_payout = false, own_bank = false;
    bool attribution_unambiguous = false;
};
struct ReferenceInput {
    Frame frame{};
    CycleCertificate solo{};
    Completion completion{};
    int assigned_group = -1, forecast_rounds = 0, remaining_rounds = 0;
    int capture_window = 0;
    bool at_approach = false, at_bank = false, at_renewal = false;
    bool altar_legal = false, recall_safe = false, safe_now = false;
    bool fresh_target_attack_legal = false;
    bool inspection_failed = false;
    // This owned quote is revalidated below; a non-solo certificate by itself
    // never supplies crew authority. Its lifetime must cover reference_step.
    const forecast::EvaluatedCrewQuote* crew = nullptr;
};
inline bool observed_payout(const ActorState& state, const ReferenceInput& in) {
    const auto& f = in.frame;
    return f.valid() && state.active && state.phase == Phase::hold &&
        in.completion.clock == f.clock && in.completion.attribution_unambiguous &&
        in.completion.group == state.project.group && in.completion.own_payout &&
        state.previous_observed &&
        (f.own.actor.bag > state.previous_bag || f.own.gold > state.previous_gold);
}
inline bool observed_bank(const ActorState& state, const ReferenceInput& in) {
    const auto& f = in.frame;
    return f.valid() && state.active && state.phase == Phase::bank &&
        in.completion.clock == f.clock && in.completion.attribution_unambiguous &&
        in.completion.group == state.project.group && in.completion.own_bank &&
        state.tribute_started && f.clock.round > state.tribute_started_round &&
        f.own.actor.bag == 0 && f.own_team_score >= state.tribute_started_score &&
        f.own_team_score - state.tribute_started_score >= state.tribute_started_bag;
}
// Verified resource receipts survive a safety diversion or a fuel-limited
// action turn. Ingestion changes ownership state; it authorizes no action.
inline void record_completion(ActorState& state, const ReferenceInput& in) {
    if (observed_payout(state, in)) {
        state.payout_seen = true;
        if (in.frame.own.actor.bag == 0) {
            state.empty_liquidation = true; state.tribute_started = false;
            state.enter(Phase::travel, Stage::release_or_repair);
        } else state.enter(Phase::allocate, Stage::verify);
    }
    if (observed_bank(state, in)) {
        state.bank_seen = true; state.enter(Phase::travel, Stage::release_or_repair);
        state.tribute_started = false;
    }
}
struct CrewBinding {
    int group = -1, self_index = -1, duration = 0, remaining = 0, capture_window = 0;
};
// Crew authority retains the exact evaluated personal view and all three
// individual continuations. No actor's unsafe solo result is a prerequisite,
// and no aggregate kit or public boolean can replace this owned proof.
inline std::optional<CrewBinding> current_crew_forecast(
    const Frame& f, const forecast::EvaluatedCrewQuote& evaluated, int target) {
    if (!f.valid() || target < 0 || !evaluated.matches_frame(f)) return std::nullopt;
    const auto& request = evaluated.request();
    const auto& quote = evaluated.result();
    const auto* actual = f.live_group(target);
    if (quote.status != Status::feasible || quote.issue != forecast::Issue::none ||
        quote.abort != Failure::none || !actual || !actual->all_spawns_visible ||
        !actual->uncontested_now || (actual->lifecycle != Lifecycle::living &&
        actual->lifecycle != Lifecycle::reward_available) ||
        !request.ready_basics.current(f.clock, target) || request.ready_basics.value != 7 ||
        quote.schedule.target_id != target || quote.schedule.actor_count != 3 ||
        quote.schedule.production_count != 1 || quote.clear_turns < 0 ||
        quote.capture_tick < quote.clear_turns) return std::nullopt;
    const auto settings = request.routes[0].timing.settings.value;
    const auto remaining = static_cast<std::int64_t>(settings.max_rounds) - f.clock.round;
    const auto capture_window = static_cast<std::int64_t>(settings.capture_rounds) + 1;
    if (!p6::valid(settings) || remaining <= 0 ||
        capture_window > std::numeric_limits<int>::max() || quote.capture_tick > remaining)
        return std::nullopt;
    const auto& production = quote.schedule.productions[0];
    if (production.holder_count != 3 || production.tick != quote.capture_tick)
        return std::nullopt;
    CrewBinding binding{target, -1, 0, static_cast<int>(remaining), static_cast<int>(capture_window)};
    for (int index = 0; index < 3; ++index) {
        const int id = request.actors[index];
        for (int previous = 0; previous < index; ++previous)
            if (request.actors[previous] == id) return std::nullopt;
        const auto* actor = f.visible(id);
        const auto& route = request.routes[index];
        const auto& certificate = quote.certificates[index];
        const auto& scheduled = quote.schedule.actors[index];
        ViewClock subject_clock = f.clock; subject_clock.self = id;
        const auto route_settings = route.timing.settings.value;
        if (!actor || actor->team != Team::ally || !actor->alive || actor->health <= 0 ||
            actor->channel != Channel::none || actor->pick_ordinal != index ||
            route.shape != forecast::Shape::produce_bank || route.project_group != target ||
            route.group_count != 1 || route.leg_count != 4 ||
            route.timing.settings.source != Source::queried_setting ||
            route_settings.max_rounds != settings.max_rounds ||
            route_settings.win_gems != settings.win_gems ||
            route_settings.capture_rounds != settings.capture_rounds ||
            !route.kit.current(subject_clock, id) ||
            !route.groups[0].observed.current(subject_clock, target) ||
            route.groups[0].observed.value.id != target || certificate.solo ||
            !certificate.valid(f, id, target) || scheduled.actor_id != id ||
            production.holders[index] != id || quote.durations[index] <= 0 ||
            quote.durations[index] > remaining || quote.durations[index] < quote.capture_tick ||
            scheduled.event_count < 1 || scheduled.event_count > max_actor_events)
            return std::nullopt;
        const auto& proposed = route.groups[0].observed.value;
        // Each actor has its own current attack pad and capture pad. These are
        // request-owned waypoints, not replacements for current source state.
        if (proposed.lifecycle != actual->lifecycle ||
            proposed.original_spawn_visible != actual->original_spawn_visible ||
            proposed.all_spawns_visible != actual->all_spawns_visible ||
            proposed.uncontested_now != actual->uncontested_now ||
            proposed.approach != actor->position || route.legs[0].from != actor->position)
            return std::nullopt;
        const bool self = id == f.clock.self;
        if (scheduled.initial_bag.value != actor->bag ||
            scheduled.initial_bag.source != (self ? Source::self_observed : Source::visible_subject) ||
            scheduled.initial_gold.value != (self ? f.own.gold : 0) ||
            scheduled.initial_gold.source != (self ? Source::self_observed : Source::conservative))
            return std::nullopt;
        for (int event = 0; event < scheduled.event_count; ++event)
            if (scheduled.events[event].tick < 0 || scheduled.events[event].tick > quote.durations[index])
                return std::nullopt;
        if (self) { binding.self_index = index; binding.duration = quote.durations[index]; }
    }
    if (binding.self_index < 0) return std::nullopt;
    return binding;
}
inline bool crew_authorized(const ReferenceInput& in, int target) {
    if (!in.crew || in.assigned_group != target) return false;
    const auto binding = current_crew_forecast(in.frame, *in.crew, target);
    return binding && in.forecast_rounds == binding->duration &&
        in.remaining_rounds == binding->remaining && in.capture_window == binding->capture_window;
}
inline bool bind_crew_forecast_into(const ActorState& state, ReferenceInput& observed,
                                     const forecast::EvaluatedCrewQuote& evaluated) {
    const auto binding = current_crew_forecast(observed.frame, evaluated, observed.assigned_group);
    if (!binding || (state.active && state.project.group != binding->group &&
        !((state.bank_seen || state.empty_liquidation) && observed.at_renewal))) return false;
    if (state.active && (state.payout_seen || state.bank_seen || state.empty_liquidation ||
        state.tribute_started || state.phase == Phase::allocate || state.phase == Phase::bank ||
        state.phase == Phase::recover)) return false;
    // Commit only after every actor, current source and ownership check passes.
    observed.solo = {};
    observed.crew = &evaluated;
    observed.forecast_rounds = binding->duration;
    observed.remaining_rounds = binding->remaining;
    observed.capture_window = binding->capture_window;
    return true;
}
// A forecast may supply duration and conservative safety. It cannot replace
// local completion, channel, position or action-legality observations.
// Rejection leaves the incumbent untouched; callers keep its safe stage before
// considering another option. Other shapes remain quotes for policy selection.
inline bool bind_cycle_forecast_into(
    const ActorState& state, ReferenceInput& observed,
    const forecast::EvaluatedQuote& evaluated) {
    const auto& f = observed.frame;
    // The quote owns the exact inputs from which it privately computed this
    // result. Reuse requires the same complete live frame; current group facts
    // below are still checked against the request before binding any action.
    if(!evaluated.matches_frame(f))return false;
    const auto& request=evaluated.request();const auto& quote=evaluated.result();
    if (!f.valid() || quote.status != Status::feasible ||
        !request.timing.settings.known() || !p6::valid(request.timing.settings.value) ||
        !quote.certificate.solo ||
        !quote.certificate.valid(f, f.clock.self, quote.certificate.group) ||
        quote.schedule.actor_count != 1 || quote.phase_count < 1 || quote.phase_count > 16)
        return false;
    const auto& actor = quote.schedule.actors[0];
    if (actor.actor_id != f.clock.self || actor.initial_bag.source != Source::self_observed ||
        actor.initial_bag.value != f.own.actor.bag ||
        actor.initial_gold.source != Source::self_observed || actor.initial_gold.value != f.own.gold)
        return false;
    const int group = quote.certificate.group;
    if (state.active && state.project.group != group &&
        !((state.bank_seen || state.empty_liquidation) && observed.at_renewal)) return false;
    if (request.shape == forecast::Shape::produce_bank) {
        if (request.group_count != 1 || !request.groups[0].observed.current(f.clock, group))
            return false;
        const auto* actual = f.live_group(group);
        const auto& proposed = request.groups[0].observed.value;
        if (!actual || actual->lifecycle != proposed.lifecycle ||
            actual->approach != proposed.approach || actual->spawn != proposed.spawn ||
            actual->retreat != proposed.retreat ||
            actual->all_spawns_visible != proposed.all_spawns_visible ||
            actual->uncontested_now != proposed.uncontested_now) return false;
    } else if (request.shape == forecast::Shape::bank_only) {
        if (request.group_count != 0 || request.project_group != group || !state.active ||
            !(state.phase == Phase::bank ||
              (state.phase == Phase::allocate && state.payout_seen) || observed_payout(state, observed)))
            return false;
    } else return false;
    const auto settings = request.timing.settings.value;
    const auto remaining = static_cast<std::int64_t>(settings.max_rounds) - f.clock.round;
    const auto capture_window = static_cast<std::int64_t>(settings.capture_rounds) + 1;
    if (remaining <= 0 || capture_window > std::numeric_limits<int>::max()) return false;
    int duration = 0;
    for (int i = 0; i < quote.phase_count; ++i) {
        const auto& phase = quote.phases[i];
        if (phase.actor != f.clock.self || phase.start < 0 || phase.finish < phase.start ||
            phase.finish > remaining) return false;
        duration = std::max(duration, phase.finish);
    }
    // Commit only after every original ownership/current-view/admission check.
    observed.solo = quote.certificate;
    observed.crew = nullptr;
    observed.assigned_group = group;
    observed.forecast_rounds = duration;
    observed.remaining_rounds = static_cast<int>(remaining);
    observed.capture_window = static_cast<int>(capture_window);
    return true;
}
inline std::optional<ReferenceInput> bind_cycle_forecast(
    const ActorState& state,const ReferenceInput& observed,const forecast::EvaluatedQuote& evaluated) {
    ReferenceInput bound=observed;
    if(!bind_cycle_forecast_into(state,bound,evaluated))return std::nullopt;
    return bound;
}
inline std::optional<ReferenceInput> bind_cycle_forecast(
    const ActorState& state, const ReferenceInput& observed,
    const forecast::Request& request) {
    auto evaluated=std::make_unique<forecast::EvaluatedQuote>();
    evaluated->evaluate(observed.frame,request);
    return bind_cycle_forecast(state,observed,*evaluated);
}
inline StepProposal fallback(const ReferenceInput& in) {
    if (!in.frame.own.actor.alive || in.frame.own.at_spawn)
        return {Action::recover_on_spawn, Stage::release_or_repair, Phase::recover};
    if (in.safe_now && in.frame.own.actor.bag > 0)
        return {in.at_bank && in.altar_legal ? Action::tribute : Action::move,
                Stage::release_or_repair, Phase::bank, Failure::none, -1};
    if (in.recall_safe)
        return {Action::recall, Stage::release_or_repair, Phase::recover};
    return {Action::move, Stage::release_or_repair, Phase::recover, Failure::unsafe};
}
// One call per owner turn. Completing a phase never invents an action result.
inline StepProposal reference_step(ActorState& state, const ReferenceInput& in) {
    const auto& f = in.frame;
    if (!f.valid() || state.last_round == f.clock.round) return {};
    state.last_round = f.clock.round; ++state.owner_turn;
    struct Remember {
        ActorState& s; const Frame& f;
        ~Remember() {
            s.previous_observed = true; s.previous_bag = f.own.actor.bag;
            s.previous_gold = f.own.gold; s.previous_score = f.own_team_score;
            s.previous_channel = f.own.actor.channel;
        }
    } remember{state, f};
    // Channels precede appointment/epoch checks. No time expiry cancels a channel.
    if (f.own.actor.channel != Channel::none && f.own.actor.channel_valid && in.safe_now)
        return {Action::preserve_channel, Stage::execute, state.phase};
    if (!f.own.actor.alive || !in.safe_now) {
        if (state.active) state.retire(Failure::unsafe);
        return fallback(in);
    }
    record_completion(state, in);
    if (state.bank_seen || state.empty_liquidation) {
        if (!in.at_renewal) return {Action::move, Stage::release_or_repair, Phase::travel};
        state.active = false; state.bank_seen = state.payout_seen = state.empty_liquidation = false;
        state.enter(Phase::bootstrap, Stage::produce_or_approach);
    }
    const int target = state.active ? state.project.group : in.assigned_group;
    if (target < 0) return fallback(in);
    const auto* group = f.live_group(target);
    const bool certified = (in.solo.solo && in.solo.valid(f, f.clock.self, target)) ||
        crew_authorized(in, target);
    if (!state.active) {
        if (!state.may_admit(target, f.clock.round)) return fallback(in);
        if (!group) {
            if (in.inspection_failed && ++state.failed_inspections >= 2) {
                state.project = {target, f.clock.round / 64};
                state.retire(Failure::not_observed); return fallback(in);
            }
            return {Action::inspect, Stage::produce_or_approach, Phase::bootstrap,
                    Failure::not_observed, target};
        }
        state.failed_inspections = 0;
        if (!certified) return fallback(in);
        if (!state.admit(target, f.clock.round, in.forecast_rounds, in.remaining_rounds,
                         f.own.actor.bag, f.own.gold)) return fallback(in);
    }
    if (!certified) { state.retire(Failure::unsafe); return fallback(in); }
    if (state.owner_turn - state.phase_started >= state.phase_limit) {
        if (state.phase == Phase::hold && state.hold_extensions == 0) {
            // One fresh safe window; it is an estimate, never capture completion.
            if (in.capture_window <= 0) {
                state.retire(Failure::expired); return fallback(in);
            }
            ++state.hold_extensions; state.phase_started = state.owner_turn;
            state.phase_limit = std::min(in.remaining_rounds, in.capture_window);
        } else { state.retire(Failure::expired); return fallback(in); }
    }
    if (state.phase == Phase::allocate) {
        state.enter(Phase::bank, Stage::release_or_repair);
        return {Action::move, Stage::release_or_repair, Phase::bank, Failure::none, target};
    }
    if (state.phase == Phase::bank)
        return {in.at_bank && in.altar_legal ? Action::tribute : Action::move,
                Stage::execute, Phase::bank, Failure::none, target};
    if (!group) return {Action::inspect, Stage::observe_admission, state.phase,
                        Failure::not_observed, target};
    if (group->lifecycle == Lifecycle::respawning) {
        state.retire(Failure::observed_unavailable); return fallback(in);
    }
    if (group->lifecycle == Lifecycle::reward_available) {
        if (f.own.actor.spawn_group != target)
            return {Action::move, Stage::commit, Phase::hold, Failure::none, target};
        if (state.phase != Phase::hold) state.enter(Phase::hold, Stage::execute);
        // All-spawn visibility is not assumed. Even uncontested_now is no countdown.
        return {Action::hold, Stage::execute, Phase::hold, Failure::none, target};
    }
    if (!in.at_approach) return {Action::move, Stage::commit, Phase::travel,
                                Failure::none, target};
    if (group->lifecycle == Lifecycle::living) {
        if (state.phase != Phase::clear) state.enter(Phase::clear, Stage::execute);
        return {in.fresh_target_attack_legal ? Action::attack : Action::inspect,
                Stage::execute, Phase::clear, Failure::none, target};
    }
    return {Action::inspect, Stage::observe_admission, state.phase,
            Failure::not_observed, target};
}

// This is a legality filter for a future reconciled adapter, not that adapter.
struct ActionGuard {
    ViewClock clock{};
    bool available = false, visible = false, range_mask = false;
    bool target_category = false, known_occupancy_legal = false;
    bool path_legal = false, landing_legal = false, safe = false;
};
enum class Outcome { success, hidden_collision, unexpected_error };
struct TurnBudget {
    ViewClock clock{};
    int fuel = 0, moves = 0;
    bool moved = false, attacked = false, cast = false, fault = false;
    bool select_allowed() const { return !fault && fuel >= 45000; }
    bool allows(Action a, const ActionGuard& g, bool ranged = false) const {
        if (fault || !(clock == g.clock) || !g.safe || !g.available) return false;
        if (a == Action::move)
            return fuel >= 30000 && moves < 3 && !moved && !(ranged && attacked) &&
                   g.known_occupancy_legal && g.path_legal && g.landing_legal;
        if (a == Action::attack)
            return fuel >= 16000 && !attacked && !(ranged && moved) &&
                   g.visible && g.range_mask && g.target_category;
        if (a == Action::cast)
            return fuel >= 16000 && !cast && g.visible && g.range_mask &&
                   g.target_category && g.path_legal && g.landing_legal &&
                   g.known_occupancy_legal;
        return false;
    }
    void result(Action a, Outcome result) {
        if (a == Action::move) ++moves;
        if (result == Outcome::unexpected_error) fault = true;
        if (result == Outcome::success) {
            if (a == Action::move) moved = true;
            if (a == Action::attack) attacked = true;
            if (a == Action::cast) cast = true;
        }
        // Success, kill, hidden collision and faults all invalidate old handles.
        ++clock.revision;
    }
};
template<class Work, class Log, class Yield>
void turn_boundary(Work&& work, Log&& log, Yield&& yield) {
    try { std::forward<Work>(work)(); }
    catch (const std::exception& e) { std::forward<Log>(log)(e.what()); }
    catch (...) { std::forward<Log>(log)("unexpected non-standard engine error"); }
    std::forward<Yield>(yield)();
}
} // namespace p6
