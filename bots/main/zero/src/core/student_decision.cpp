#include "student_decision.h"
#include "student_context_cache.h"
#include "net.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace zero::student {
namespace {

struct Pick { int index{-1}; float logp{}; };

Pick pick(std::span<const float> values, const DecisionOptions& options,
          std::uint32_t mask = ~0u, bool direct = false)
{
    Pick result;
    for (int i = 0; i < static_cast<int>(values.size()); ++i) {
        if (direct && !(mask & (1u << i))) continue;
        if (result.index < 0 || values[i] > values[result.index]) result.index = i;
    }
    if (result.index < 0 || (options.temperature <= 0.0f && !options.log_probability)) return result;
    const float maximum = values[result.index];
    const float temperature = options.temperature > 0.0f ? options.temperature : 1.0f;
    float total = 0.0f;
    for (int i = 0; i < static_cast<int>(values.size()); ++i)
        if (!direct || (mask & (1u << i))) total += std::exp((values[i] - maximum) / temperature);
    if (options.temperature > 0.0f) {
        *options.rng = zero::net::mix(*options.rng);
        const float draw = static_cast<float>(*options.rng >> 40) / static_cast<float>(1ull << 24) * total;
        float cumulative = 0.0f;
        int last = result.index;
        for (int i = 0; i < static_cast<int>(values.size()); ++i) {
            if (direct && !(mask & (1u << i))) continue;
            last = i;
            cumulative += std::exp((values[i] - maximum) / temperature);
            if (draw < cumulative) { result.index = i; break; }
            result.index = last;
        }
    }
    result.logp = (values[result.index] - maximum) / temperature - std::log(total);
    return result;
}

bool finite(std::span<const float> values)
{
    for (float value : values) if (!std::isfinite(value)) return false;
    return true;
}

bool matches(const Cand& row, const TeacherSource& source)
{
    auto fields = source.match_fields;
    // These source scalar values are output labels, never pointer identity or input facts.
    if (source.type == a_priority) fields &= ~match_target;
    if (source.type == a_buy) fields &= ~match_count;
    return (!(fields & match_kind) || row.kind == source.command.kind)
        && (!(fields & match_target) || row.target == source.command.target)
        && (!(fields & match_count) || row.count == source.command.count)
        && (!(fields & match_tile) || (row.at.x == source.command.at.x && row.at.y == source.command.at.y));
}

DecisionStatus build_error(ActionBuildStatus status)
{
    return status == ActionBuildStatus::unsupported ? DecisionStatus::unsupported : DecisionStatus::unavailable;
}

std::uint64_t allowance(std::uint64_t bound)
{
    // Avoid overflow even for an invalid host-provided diagnostic bound.
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    return bound > maximum / 5u ? maximum : bound * 5u / 4u;
}

}  // namespace

void FuelBounds::measured(ActionType type, std::uint64_t fuel)
{
    if (type >= a_count) return;
    argument[type] = std::max(argument[type], fuel);
    known_mask |= action_bit(type);
}

Decision decide(const Context& context, const float* weights, const DecisionOptions& options,
                FuelBounds& bounds, const TeacherSource* teacher)
{
    Decision result;
    if (!context.map || !context.obs || !context.mem || !weights || !options.remaining_fuel
        || !std::isfinite(options.temperature) || (options.temperature > 0.0f && !options.rng)) return result;
    const auto start = options.remaining_fuel();
    result.forced = teacher != nullptr;
    result.diagnostic_bypass = options.diagnostic_bypass_admission;
    bool argument_started = false;
    bool argument_completed = false;
    std::uint64_t before_cleanup{};
    std::vector<CampContext> camps;
    // Keep owning buffers inside this scope so total includes their destruction on every outcome.
    {
        float state[kState]{}, commands[a_count]{};
        net::Embedding embedding;
        std::vector<Cand> rows;
        std::vector<float> scores;
        Cand quantity{};
        float quantity_score{};
        Head head{h_aux};
        bool root_ready = false, rows_ready = false, quantity_ready = false;
        int natural_argument = -1, scalar_label = 0, natural_scalar = 0;
        const auto before_state = options.remaining_fuel();
        zero::student::state_vector(context, state);
        auto now = options.remaining_fuel();
        result.fuel.state = before_state - now;
        auto execute = [&] {
            if (!finite(state)) { result.status = DecisionStatus::nonfinite; return; }
            auto before = options.remaining_fuel();
            bool encoded = true;
            if (options.camp_attention) {
                if (options.camp_context_cache) camp_context_cached(context, *options.camp_context_cache, camps);
                else camp_context(context, camps);
#if defined(ZERO_STUDENT_PROBE)
                const auto before_attention = options.remaining_fuel();
                result.fuel.context_build = before - before_attention;
                result.fuel.context_entities = static_cast<std::uint32_t>(camps.size());
#endif
                encoded = net::embed_attention(weights, state, camps, embedding);
#if defined(ZERO_STUDENT_PROBE)
                result.fuel.context_attention = before_attention - options.remaining_fuel();
#endif
            } else net::embed(weights, state, embedding);
            now = options.remaining_fuel();
            result.fuel.encode = before - now;
            if (!encoded) { result.status = DecisionStatus::nonfinite; return; }
            before = now;
#if defined(ZERO_STUDENT_PROBE)
            result.rule_mask = (options.rule_builder ? options.rule_builder(context, kAllActions)
                                                    : rule_mask(context, kAllActions)) | action_bit(a_end);
#else
            result.rule_mask = rule_mask(context, kAllActions) | action_bit(a_end);
#endif
            now = options.remaining_fuel();
            result.fuel.rule_mask = before - now;
            before = now;
            net::command_logits(weights, embedding, commands);
            now = options.remaining_fuel();
            result.fuel.controller = before - now;
            root_ready = true;
            if (!finite(commands)) { result.status = DecisionStatus::nonfinite; return; }
            result.admitted_mask = action_bit(a_end);
            for (int i = 1; i < a_count; ++i) {
                const auto bit = 1u << i;
                if (!(result.rule_mask & options.allowed_mask & bounds.known_mask & bit)) continue;
                const auto required = allowance(bounds.argument[i]);
                if (required < now && now - required > options.cleanup_reserve + options.choice_reserve)
                    result.admitted_mask |= bit;
            }
            result.used_mask = options.diagnostic_bypass_admission
                ? (result.rule_mask & options.allowed_mask) | action_bit(a_end) : result.admitted_mask;
            // Report natural greedy independently; teacher forcing never consumes policy RNG.
            DecisionOptions greedy = options;
            greedy.temperature = 0.0f;
            greedy.log_probability = false;
            result.natural_command = pick(commands, greedy, result.rule_mask, true).index;
            result.admitted_greedy = pick(commands, greedy, result.used_mask, true).index;
            Pick selected;
            if (teacher) {
                if (teacher->type >= a_count || !(result.rule_mask & action_bit(teacher->type))) {
                    result.status = DecisionStatus::missing_teacher; return;
                }
                if (!(result.used_mask & action_bit(teacher->type))) {
                    result.status = DecisionStatus::fuel_end; return;
                }
                selected.index = teacher->type;
            } else selected = pick(commands, options, result.used_mask, true);
            if (selected.index < 0) { result.status = DecisionStatus::unavailable; return; }
            result.type = static_cast<ActionType>(selected.index);
            result.command_logp = selected.logp;
            if (result.type == a_end) {
                result.status = result.rule_mask == action_bit(a_end) ? DecisionStatus::rule_end
                    : !teacher && result.natural_command != a_end
                        && !(result.used_mask & action_bit(static_cast<ActionType>(result.natural_command)))
                    ? DecisionStatus::fuel_end : DecisionStatus::learned_end;
                return;
            }
            const auto argument_start = options.remaining_fuel();
            argument_started = true;
            auto arguments = [&] {
#if defined(ZERO_STUDENT_PROBE)
                auto profile_mark = options.remaining_fuel();
#endif
                ActionBuild build;
                if (result.type == a_replan) {
                    goal_candidates(context, rows);
                    build.total = static_cast<int>(rows.size());
                    head = h_goal;
                } else {
#if defined(ZERO_STUDENT_PROBE)
                    build = options.argument_builder ? options.argument_builder(context, result.type, rows)
                        : argument_candidates(context, result.type, rows);
#else
                    build = argument_candidates(context, result.type, rows);
#endif
                    head = argument_head(result.type);
                }
                result.build_status = build.status;
                rows_ready = true;
#if defined(ZERO_STUDENT_PROBE)
                result.fuel.argument_build = profile_mark - options.remaining_fuel();
                profile_mark = options.remaining_fuel();
#endif
                // Compact builders promise full lists. Truncation is a hard interface failure here.
                if (build.status != ActionBuildStatus::ok || rows.empty()) {
                    result.status = build_error(build.status); return;
                }
                net::Projection projection;
                if (!net::project(weights, embedding, head, projection)) {
                    result.status = DecisionStatus::unsupported; return;
                }
#if defined(ZERO_STUDENT_PROBE)
                result.fuel.argument_project = profile_mark - options.remaining_fuel();
                profile_mark = options.remaining_fuel();
#endif
                scores.reserve(rows.size());
                for (const auto& row : rows) scores.push_back(net::score(projection, row.phi));
#if defined(ZERO_STUDENT_PROBE)
                result.fuel.argument_scan = profile_mark - options.remaining_fuel();
                profile_mark = options.remaining_fuel();
#endif
                if (!finite(scores)) { result.status = DecisionStatus::nonfinite; return; }
                if (result.type == a_priority) {
                    if (rows.size() != 1) { result.status = DecisionStatus::unsupported; return; }
                    result.argument_index = natural_argument = 0;
                    result.command = rows[0];
                    result.predicted_priority = decode_priority(scores[0]);
                    natural_scalar = result.predicted_priority;
                    if (teacher && !matches(result.command, *teacher)) { result.status = DecisionStatus::missing_teacher; return; }
                    result.command.target = teacher ? teacher->command.target : result.predicted_priority;
                    scalar_label = result.command.target;
                } else {
                    natural_argument = pick(scores, greedy).index;
                    Pick argument;
                    if (teacher) {
                        for (int i = 0; i < static_cast<int>(rows.size()); ++i)
                            if (matches(rows[i], *teacher)) { argument.index = i; break; }
                        if (argument.index < 0) { result.status = DecisionStatus::missing_teacher; return; }
                    } else argument = pick(scores, options);
                    result.argument_index = argument.index;
                    result.argument_logp = argument.logp;
                    result.command = rows[argument.index];
                }
#if defined(ZERO_STUDENT_PROBE)
                result.fuel.argument_route = profile_mark - options.remaining_fuel();
                profile_mark = options.remaining_fuel();
#endif
                if (result.type == a_buy) {
                    quantity_candidate(context, result.command, quantity);
                    quantity_ready = true;
                    const auto maximum = max_affordable_quantity(context, result.command.target);
                    if (maximum <= 0) { result.status = DecisionStatus::invalid_scalar; return; }
                    net::Projection quantity_projection;
                    if (!net::project(weights, embedding, h_quantity, quantity_projection)) {
                        result.status = DecisionStatus::unsupported; return;
                    }
                    quantity_score = net::score(quantity_projection, quantity.phi);
                    if (!std::isfinite(quantity_score)) { result.status = DecisionStatus::nonfinite; return; }
                    result.predicted_quantity = decode_quantity(quantity_score, maximum);
                    const auto count = teacher ? teacher->command.count : result.predicted_quantity;
                    if (count <= 0 || count > maximum) { result.status = DecisionStatus::invalid_scalar; return; }
                    result.command.count = count;
                }
#if defined(ZERO_STUDENT_PROBE)
                result.fuel.argument_quantity = profile_mark - options.remaining_fuel();
#endif
                argument_completed = true;
                result.status = options.remaining_fuel() > options.cleanup_reserve + options.choice_reserve
                    ? DecisionStatus::command : DecisionStatus::fuel_end;
            };
            arguments();
            const auto after_argument = options.remaining_fuel();
            result.fuel.argument = argument_start - after_argument;
        };
        execute();
        if (options.trace) {
            const auto before_trace = options.remaining_fuel();
            DecisionTrace event;
            event.command = teacher ? teacher->type : result.type;
            event.state = state;
            event.context = camps;
            event.rule_mask = result.rule_mask;
            event.admitted_mask = result.admitted_mask;
            event.used_mask = result.used_mask;
            event.forced = teacher != nullptr;
            event.build_status = result.build_status;
            event.decision_status = result.status;
            if (root_ready) {
                event.stage = TraceStage::command;
                event.head = h_action;
                event.scores = commands;
                event.label = teacher ? teacher->type : result.type;
                event.natural_label = result.natural_command;
                options.trace(options.trace_user, event);
            }
            if (rows_ready) {
                event.stage = result.type == a_priority ? TraceStage::scalar : TraceStage::argument;
                event.head = head;
                event.rows = rows;
                event.scores = scores;
                event.label = result.type == a_priority ? scalar_label : result.argument_index;
                event.natural_label = result.type == a_priority ? natural_scalar : natural_argument;
                event.predicted_scalar = result.type == a_priority && !scores.empty() ? scores[0] : 0.0f;
                options.trace(options.trace_user, event);
            }
            if (quantity_ready) {
                event.stage = TraceStage::scalar;
                event.head = h_quantity;
                event.rows = std::span<const Cand>(&quantity, 1);
                event.scores = std::span<const float>(&quantity_score, 1);
                event.label = teacher ? teacher->command.count : result.command.count;
                event.natural_label = result.predicted_quantity;
                event.predicted_scalar = quantity_score;
                options.trace(options.trace_user, event);
            }
            result.fuel.trace = before_trace - options.remaining_fuel();
        }
        before_cleanup = options.remaining_fuel();
    }
    const auto after_cleanup = options.remaining_fuel();
    if (argument_started) result.fuel.argument += before_cleanup - after_cleanup;
#if defined(ZERO_STUDENT_PROBE)
    result.fuel.argument_destroy = before_cleanup - after_cleanup;
#endif
    // Context belongs to every root decision, independent of the selected argument family.
    // Release it after argument accounting so its destruction cannot lower the root bound.
    if (options.camp_attention) {
        std::vector<CampContext>{}.swap(camps);
#if defined(ZERO_STUDENT_PROBE)
        result.fuel.context_destroy = after_cleanup - options.remaining_fuel();
#endif
    }
    const auto after_context_cleanup = options.remaining_fuel();
    // Calibration includes complete selected buffers/destruction, excluding optional trace callbacks.
    // Caller separately meters Memory commit, primitive/refresh, and its own final reserve.
    if (argument_completed) bounds.measured(result.type, result.fuel.argument);
    if (result.status == DecisionStatus::command && after_context_cleanup <= options.cleanup_reserve + options.choice_reserve)
        result.status = DecisionStatus::fuel_end;
    result.fuel.total = start - options.remaining_fuel();
    return result;
}

}  // namespace zero::student
