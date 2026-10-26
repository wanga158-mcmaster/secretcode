#pragma once
#include "observe.h"
#include <memory>

// Pure old-bag alternatives. These are forecasts of this one actor's resources,
// never bank receipts, joint income, or permission to interrupt a legal channel.
namespace p6::runtime_economy {
struct Selection {
    forecast::Result production{}, bank_first{}, bank_only{};
    ScoreEvaluation production_rank{}, bank_first_rank{}, bank_rank{};
    bool bank_preferred = false;
};
inline bool distance_valid(Point from, Point to, int distance) {
    const auto dx = std::abs(std::int64_t(from.x) - to.x);
    const auto dy = std::abs(std::int64_t(from.y) - to.y);
    return distance >= 0 && distance <= 256 && distance >= std::max(dx, dy) &&
        ((from == to) == (distance == 0));
}
inline bool route_leg(const forecast::Leg& leg, Point from, Point to) {
    return leg.from == from && leg.to == to &&
        (leg.distance.source == Source::public_map || leg.distance.source == Source::queried_setting) &&
        distance_valid(from, to, leg.distance.value);
}
inline ScoreEvaluation evaluate(const forecast::Result& result, const Frame& frame,
                                const ScenarioWindows& windows, Settings settings) {
    if (result.status != Status::feasible) return {};
    return evaluate_schedule(result.schedule, windows, settings,
        {frame.clock.round, frame.clock.self}, frame.own_team_score);
}
inline void choose_into(Selection& destination, const Frame& frame, const forecast::Request& base_produce_bank,
    const ScenarioWindows& windows, Settings settings,
    int self_to_bank_distance, int bank_to_approach_distance) {
    // Reuse bounded schedule payload; only current counts and validity gates
    // are reset. Each builder initializes the records it actually appends.
    auto reset = [](forecast::Result& result) {
        result.status = Status::unresolved; result.issue = forecast::Issue::unknown_input;
        result.abort = Failure::none;
        result.schedule.actor_count = result.schedule.production_count = 0;
        result.schedule.target_id = -1;
        result.schedule.immediate_win = {}; result.schedule.terminal_tiebreak = {};
        result.schedule.actors[0].event_count = 0;
        result.certificate = {}; result.phase_count = 0; result.first = {};
        result.recovery_health_unmodeled = false; result.purchases_deferred = true;
    };
    reset(destination.production); reset(destination.bank_first); reset(destination.bank_only);
    destination.production_rank = {}; destination.bank_first_rank = {}; destination.bank_rank = {};
    destination.bank_preferred = false;
    auto* output = &destination;
    const auto& base = base_produce_bank;
    const auto& queried = base.timing.settings;
    const auto& group = base.groups[0].observed.value;
    if (!frame.valid() || !p6::valid(settings) ||
        queried.source != Source::queried_setting ||
        queried.value.max_rounds != settings.max_rounds ||
        queried.value.win_gems != settings.win_gems ||
        queried.value.capture_rounds != settings.capture_rounds ||
        base.shape != forecast::Shape::produce_bank || base.group_count != 1 ||
        base.leg_count != 4 || base.recovery.enabled || base.recovery.requires_health_restore ||
        group.id < 0 ||
        !route_leg(base.legs[0], frame.own.actor.position, group.approach) ||
        !route_leg(base.legs[1], group.approach, group.spawn) ||
        !route_leg(base.legs[2], group.spawn, base.bank_position) ||
        !route_leg(base.legs[3], base.bank_position, base.renewal_position)) {
        output->production.status = output->bank_first.status = output->bank_only.status = Status::invalid;
        output->production.issue = output->bank_first.issue = output->bank_only.issue = forecast::Issue::malformed;
        return;
    }
    forecast::build_into(frame, base, output->production);
    output->production_rank = evaluate(output->production, frame, windows, settings);
    if (frame.own.actor.bag <= 0) return;
    if (!distance_valid(frame.own.actor.position, base.bank_position, self_to_bank_distance))
        return; // No guessed route to the altar.
    auto request = std::make_unique<forecast::Request>(base);
    request->shape = forecast::Shape::bank_only;
    request->project_group = group.id;
    request->group_count = 0; request->leg_count = 2;
    request->legs[0] = {frame.own.actor.position, base.bank_position,
                        {self_to_bank_distance, Source::public_map}};
    request->legs[1] = base.legs[3];
    forecast::build_into(frame, *request, output->bank_only);
    output->bank_rank = evaluate(output->bank_only, frame, windows, settings);
    if (distance_valid(base.bank_position, group.approach, bank_to_approach_distance)) {
        *request = base;
        request->shape = forecast::Shape::bank_then_produce_bank;
        request->leg_count = 5;
        request->legs[0] = {frame.own.actor.position, base.bank_position,
                            {self_to_bank_distance, Source::public_map}};
        request->legs[1] = {base.bank_position, group.approach,
                            {bank_to_approach_distance, Source::public_map}};
        for (int i = 1; i < 4; ++i) request->legs[i + 1] = base.legs[i];
        forecast::build_into(frame, *request, output->bank_first);
        output->bank_first_rank = evaluate(output->bank_first, frame, windows, settings);
    }
    const auto& best_bank = compare(output->bank_first_rank, output->bank_rank) >= 0 ?
        output->bank_first_rank : output->bank_rank;
    output->bank_preferred = best_bank.eligible() && compare(best_bank, output->production_rank) > 0;
    return;
}
inline Selection choose(const Frame& frame, const forecast::Request& base_produce_bank,
    const ScenarioWindows& windows, Settings settings,
    int self_to_bank_distance, int bank_to_approach_distance) {
    auto output = std::make_unique<Selection>();
    choose_into(*output, frame, base_produce_bank, windows, settings,
                self_to_bank_distance, bank_to_approach_distance);
    return std::move(*output);
}
} // namespace p6::runtime_economy
