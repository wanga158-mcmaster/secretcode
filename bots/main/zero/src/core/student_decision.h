#pragma once
// One production compact-policy decision, shared by the live caller and saved-replay producer.
// The caller owns observation/history updates, Memory commit, SDK dispatch, refresh and yielding.
#include <cstdint>
#include <span>
#include "student_net.h"

namespace zero::student {

class CampContextCache;

enum class DecisionStatus : std::uint8_t {
    command, learned_end, rule_end, fuel_end, unavailable, unsupported,
    missing_teacher, invalid_scalar, nonfinite, invalid_context
};
enum class TraceStage : std::uint8_t { command, argument, scalar };
enum PayloadField : std::uint32_t {
    match_kind = 1u, match_target = 2u, match_count = 4u, match_tile = 8u,
    match_all = match_kind | match_target | match_count | match_tile
};

struct TeacherSource {
    ActionType type{a_end};
    Cand command{};
    std::uint32_t match_fields{match_all};
};

// Rows/state/scores are borrowed only for this callback. Scalar label is the signed decoded source
// value (priority or BUY count), whereas a pointer label is the row index. Source values never enter
// state/phi. All callbacks run after choice/admission; their measured cost is separate and still part
// of actual caller fuel. They cannot retroactively change masks or the selected executable command.
// decision_status is provisional until decide returns: trace/destruction may consume the reserve.
// A sink must reconcile every staged frame with the final returned Decision before accepting it.
struct DecisionTrace {
    TraceStage stage{};
    Head head{h_action};
    ActionType command{a_end};
    const float* state{};  // exactly 48 float32 values
    std::span<const CampContext> context{};  // complete static groups, present for cfg8
    std::span<const Cand> rows{};  // complete ordered raw rows, no fixed cap; empty for direct root
    std::span<const float> scores{};  // 15 for direct root, otherwise one per row
    std::uint32_t rule_mask{}, admitted_mask{}, used_mask{};
    std::int32_t label{-1}, natural_label{-1};
    float predicted_scalar{};
    bool forced{};
    ActionBuildStatus build_status{ActionBuildStatus::ok};
    DecisionStatus decision_status{DecisionStatus::invalid_context};
};
using TraceHook = void (*)(void* user, const DecisionTrace&);

struct FuelBounds {
    std::uint64_t argument[a_count]{};
    std::uint32_t known_mask{};  // ordinary execution excludes families without measured bounds
    void measured(ActionType type, std::uint64_t fuel);
};

struct DecisionOptions {
    std::uint64_t (*remaining_fuel)(){};
    std::uint64_t* rng{};  // required only at temperature > 0; T0 does not advance RNG
    float temperature{};
    bool log_probability{};
    bool camp_attention{};  // cfg8 opt-in; base feature2 execution remains unchanged when false
    CampContextCache* camp_context_cache{};  // cfg9 engineering cache; immutable map prepared by caller
#if defined(ZERO_STUDENT_PROBE)
    // Test-only computation variants must preserve complete argument rows and their feature ABI.
    ActionBuild (*argument_builder)(const Context&, ActionType, std::vector<Cand>&){};
    std::uint32_t (*rule_builder)(const Context&, std::uint32_t){};
#endif
    std::uint32_t allowed_mask{kAllActions};
    std::uint64_t cleanup_reserve{8000};
    std::uint64_t choice_reserve{};
    // Explicit diagnostics only, normally in a higher interval. The returned used_mask reveals this
    // bypass; it must never be counted as an ordinary admission/full-caller success.
    bool diagnostic_bypass_admission{};
    TraceHook trace{};
    void* trace_user{};
};

struct DecisionFuel {
    std::uint64_t state{}, encode{}, rule_mask{}, controller{}, argument{}, trace{}, total{};
#if defined(ZERO_STUDENT_PROBE)
    // Counted diagnostic instrumentation only; absent from normal production builds.
    std::uint64_t context_build{}, context_attention{}, context_destroy{};
    std::uint32_t context_entities{};
    std::uint64_t argument_build{}, argument_project{}, argument_scan{}, argument_route{}, argument_quantity{}, argument_destroy{};
#endif
};
struct Decision {
    DecisionStatus status{DecisionStatus::invalid_context};
    ActionType type{a_end};
    Cand command{};
    ActionBuildStatus build_status{ActionBuildStatus::ok};
    std::uint32_t rule_mask{}, admitted_mask{}, used_mask{};
    std::int32_t natural_command{a_end}, argument_index{-1};
    std::int32_t admitted_greedy{a_end};
    std::int32_t predicted_priority{}, predicted_quantity{};
    float command_logp{}, argument_logp{};
    bool forced{}, diagnostic_bypass{};
    DecisionFuel fuel{};
};

// weights excludes config[0..15]. The Context must remain immutable through this call. The returned
// Cand is a value copy; no pointer to temporary rows escapes. No SDK action or Memory mutation occurs.
Decision decide(const Context& context, const float* weights, const DecisionOptions& options,
                FuelBounds& bounds, const TeacherSource* teacher = nullptr);

}  // namespace zero::student
