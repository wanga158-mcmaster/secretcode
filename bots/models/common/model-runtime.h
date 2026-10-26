#pragma once
#include "pure.h"
#include "safe-segment-execution.h"
#include "alternate-route.h"

namespace plan4::runtime {
// Small legal-observation DTOs shared by every active/control adapter. They
// contain no engine handles, replay data, private teammate gold or future state.
struct ObservationClock { int round = -1; struct { int id = -1; } own; };
// Roster identities are public. Dynamic actor fields enter only through a
// fresh personal-visibility observation (or this actor's own state).
struct VisibleActor {
    int id = -1, draft_ordinal = -1, champion = -1, attack_range = -1;
    Point position{};
    float health = 0, maximum_health = 0, strength = 0;
    unsigned bag = 0;
    int current_priority = 0, next_priority = 0;
    Provenance provenance{};
    bool ally = false, alive = false, completed_turn = false;
};
struct ActorFrame {
    ObservationClock clock{};
    std::array<int, 5> allied_roster{}, enemy_roster{};
    int allied_count = 0, enemy_count = 0;
    VisibleActor own{};
    std::array<VisibleActor, 9> samples{};
    int count = 0;
    bool roster_complete = false, truncated = false, own_observed = false, draft_order_known = false;
};
struct PublicMember {
    int group_id = -1, member_id = -1, ordinal = -1, declared_members = 0, kind = -1, attack_range = -1;
    Point spawn{};
    float maximum_health = 0;
};
struct PublicGroup { int group_id = -1, first = 0, count = 0; bool complete = false; };
struct PublicLayoutFrame {
    ObservationClock clock{};
    const PublicGroup* groups = nullptr;
    int group_count = 0;
    const PublicMember* members = nullptr;
    int member_count = 0;
    bool truncated = false;
};
struct CombatKit {
    ObservationClock clock{};
    int champion = -1, attack_range = -1;
    float strength = 0, focus = 0;
    unsigned monster_shards = 0, jungle_shards = 0;
    std::uint64_t version = 0;
    std::uint32_t stat_epoch = 0;
    bool exact = false;
};
enum class TileOccupancy { unknown, free, occupied };
struct PublicTopology {
    int width = 0, height = 0;
    bool complete = false;
    bool (*walkable)(Point, const void*) = nullptr;
    const void* context = nullptr;
};
struct StaticSpatialFrame {
    ObservationClock clock{};
    PublicTopology topology{};
    WorkBudget work_budget{};
    int graph_quota = 0;
};
struct SpatialFrame {
    ObservationClock clock{};
    const Information* information = nullptr;
    const Menu* menu = nullptr; // Borrowed only during this planning call.
    PublicTopology topology{};
    TileOccupancy (*occupancy)(Point, const void*) = nullptr;
    const void* occupancy_context = nullptr;
    TravelLookup travel{};
    int (*route_prefix)(Point, Point, Point*, int, const void*) = nullptr;
    const void* route_context = nullptr;
    bool current_complete = false, own_recall_here = false;
    int graph_quota = 0;
    int reference_vertices_used = 0;
    unsigned optional_fuel_floor = 0;
};
struct ModelEvidence {
    bool reported = false, supported = false, coupling_enabled = false;
    int reason = 0, samples = 0, unknown = 0, paths = 0, work = 0;
    std::uint64_t revision = 0;
};
// Coverage is optional and reads only the model's cached diagnostic. Keeping it
// outside ModelEvidence preserves every other model's existing record schema.
template<class Model>
constexpr unsigned model_record_version() {
    if constexpr (requires(const Model& model) { model.truncation_mask(); }) return 3;
    else return 2;
}
template<class Row, class Model>
inline void append_model_coverage(Row& row, const Model& model) {
    if constexpr (requires { model.truncation_mask(); }) row.field("truncationMask", model.truncation_mask());
}
struct ModelPredictionEvent {
    int predicted_round = -1, phase = -1, kind = -1, actor_id = -1, group_id = -1;
    std::int64_t amount_milli = 0;
    unsigned bag_after = 0;
};
using PredictionEventSink = bool (*)(const ModelPredictionEvent&, void*);
enum class ProductionEvidence { unknown, visible_cleared_spawn, predicted_participation };
struct ProducerSample {
    int actor_id = -1, group_id = -1;
    Point position{};
    Provenance provenance{};
    bool personally_visible = false, producing = false, ambiguous = false;
    ProductionEvidence evidence = ProductionEvidence::unknown;
};
struct ProducerFrame {
    ObservationClock clock{};
    std::array<ProducerSample, 4> samples{};
    int count = 0;
    bool complete = false, truncated = false;
    std::array<bool, max_altars> full_patch_visibility{};
};
enum class CaptureOrigin { unknown, lifecycle_boundaries, own_action, isolated_own_stock };
struct CapturedGroup {
    int group_id = -1, round = -1, respawn_rounds = -1;
    Provenance provenance{};
    bool personally_witnessed = false;
    Status event_status = Status::derived;
    CaptureOrigin origin = CaptureOrigin::unknown;
    bool actor_attribution_known = false;
    int actor_id = -1;
    std::uint64_t stock_epoch = 0;
    unsigned bag_before = 0, bag_after = 0, received_gems = 0;
    Provenance own_provenance{};
};
struct StockAcquisition {
    int group_id = -1, capture_round = -1;
    std::uint64_t stock_epoch = 0;
    unsigned gems = 0;
    Provenance provenance{};
};
struct OwnStockFrame {
    ObservationClock clock{};
    std::uint64_t stock_epoch = 0;
    unsigned bag = 0;
    std::array<StockAcquisition,2> acquisitions{};
    int count = 0;
    bool known = false;
};
struct VisibleMember {
    int group_id = -1, member_id = -1, ordinal = -1, declared_members = 0;
    Point spawn{};
    float health = 0;
    Provenance provenance{};
};
struct MemberFrame {
    const VisibleMember* samples = nullptr;
    int count = 0;
    bool truncated = false;
};
struct LifecycleFrame {
    ObservationClock clock{};
    const Group* groups = nullptr;
    int count = 0;
    bool truncated = false;
};
enum class ProductionPhase : unsigned { travel, clear, capture, bank, restore };
enum class PhaseCensor { none, interrupted, lost_sight, ambiguous_endpoint, game_end, mode_change, safety, stat_change };
struct PhaseStart {
    ProductionPhase phase = ProductionPhase::travel;
    int actor = -1;
    std::uint64_t job_epoch = 0, phase_sequence = 0;
    int start_round = -1, nominal_duration = 0, mode = -1;
    std::uint32_t stat_epoch = 0;
    bool emergency = false;
    int group_id = -1;
};
struct PhaseReceipt {
    ProductionPhase phase = ProductionPhase::travel;
    int actor = -1;
    std::uint64_t job_epoch = 0, phase_sequence = 0;
    int start_round = -1, end_round = -1, nominal_duration = 0;
    int requested_mode = -1, executed_mode = -1;
    std::uint32_t stat_epoch = 0;
    bool completed = false, endpoint_observed = false, emergency = false;
    PhaseCensor censor = PhaseCensor::none;
    int legal_wait_rounds = 0, waiting_bound = 0;
    bool waiting_legal = true;
    int group_id = -1;
};

enum class ExecutionRepairAction { none, preserve, alternate_path, bank, renew, replan, safe_retreat };
struct ExecutionRepairFrame {
    ObservationClock clock{};
    const Information* information = nullptr;
    const Menu* fixed_menu = nullptr;
    Job selected_job{};
    std::uint64_t job_epoch = 0;
    int remaining_group = -1;
    bool same_epoch = false, protected_progress = false;
    ProductionPhase phase = ProductionPhase::travel;
    bool phase_known = false;
    // A complete personally observed ordinary route, supplied only by the
    // common movement repair adapter. Models cannot supply this proof.
    const detour::Route* alternate_route = nullptr;
    bool ordinary_goal_bound = false;
};
struct ExecutionRepair {
    bool supported = false;
    ExecutionRepairAction action = ExecutionRepairAction::none;
    int bank_menu_index = -1;
    std::uint64_t job_epoch = 0;
};
inline ExecutionRepair checked_execution_repair(const ExecutionRepairFrame& frame, ExecutionRepair proposal) {
    if (!frame.information || !frame.fixed_menu || !frame.same_epoch || !frame.job_epoch ||
        proposal.job_epoch != frame.job_epoch || !proposal.supported) return {};
    const auto& info = *frame.information;
    if (frame.clock.round != info.round || frame.clock.own.id != info.own.id || !info.own.alive ||
        frame.fixed_menu->count < 0 || frame.fixed_menu->count > max_jobs) return {};
    if (frame.protected_progress || info.own.channel != Channel::none || info.in_progress_group_id >= 0)
        return {true,ExecutionRepairAction::preserve,-1,frame.job_epoch};
    if (proposal.action == ExecutionRepairAction::bank) {
        if (!info.own.bag || proposal.bank_menu_index < 0 || proposal.bank_menu_index >= frame.fixed_menu->count) return {};
        const auto& bank = frame.fixed_menu->jobs[proposal.bank_menu_index];
        if (bank.kind != JobKind::bank || bank.altar < 0 || bank.altar >= info.altar_count ||
            bank.altar >= max_altars || !info.altars[bank.altar].accessible) return {};
    } else if (proposal.action == ExecutionRepairAction::alternate_path) {
        const auto* route=frame.alternate_route;
        if (!frame.ordinary_goal_bound || !route || !detour::valid(*route) || route->actor!=info.own.id ||
            route->round!=info.round || route->origin!=info.own.position ||
            !frame.phase_known || frame.phase!=ProductionPhase::travel) return {};
        if (frame.selected_job.kind==JobKind::farm) {
            if (frame.remaining_group<0 || frame.remaining_group>=info.group_count ||
                (frame.remaining_group!=frame.selected_job.group && frame.remaining_group!=frame.selected_job.second_group)) return {};
        } else if (frame.selected_job.kind!=JobKind::bank || frame.selected_job.altar<0 ||
            frame.selected_job.altar>=info.altar_count) return {};
    } else if (proposal.action != ExecutionRepairAction::none && proposal.action != ExecutionRepairAction::preserve &&
               proposal.action != ExecutionRepairAction::renew && proposal.action != ExecutionRepairAction::replan &&
               proposal.action != ExecutionRepairAction::safe_retreat) return {};
    return proposal;
}
template<class Model>
inline constexpr bool observes_execution_repairs = requires(Model& model, const ExecutionRepairFrame& frame) {
    model.execution_repair(frame);
};
template<class Model>
inline ExecutionRepair execution_repair(Model& model, const ExecutionRepairFrame& frame) {
    if constexpr (observes_execution_repairs<Model>) return checked_execution_repair(frame,model.execution_repair(frame));
    return {};
}

// An arm-independent request to consult the ordinary selector for an executing
// job. It carries current contract facts, never a model-selected action.
struct ActiveJobConsultation {
    bool supported=false;
    std::uint64_t epoch=0,sequence=0;
    int deadline=-1;
    unsigned completed=0;
    ProductionPhase phase=ProductionPhase::travel;
};
template<class Model>
inline ActiveJobConsultation active_job_consultation(Model& model,const ExecutionRepairFrame& frame) {
    if constexpr (requires {model.active_job_consultation(frame);}) {
        if (!frame.information || !frame.fixed_menu || !frame.same_epoch || !frame.job_epoch || !frame.phase_known ||
            frame.protected_progress || frame.clock.round!=frame.information->round ||
            frame.clock.own.id!=frame.information->own.id || !frame.information->own.alive ||
            frame.information->own.channel!=Channel::none || frame.information->in_progress_group_id>=0) return {};
        auto result=model.active_job_consultation(frame);
        if (!result.supported || !result.epoch || !result.sequence || result.deadline<frame.clock.round ||
            result.completed>3 || unsigned(frame.phase)>unsigned(ProductionPhase::restore)) return {};
        result.phase=frame.phase;
        return result;
    }
    return {};
}
template<class Model>
inline constexpr bool uses_alternate_routes = requires(Model& model,const ExecutionRepairFrame& frame) {
    model.alternate_route_result(frame,true,Point{});
};
template<class Model>
inline bool alternate_route_result(Model& model,const ExecutionRepairFrame& frame,bool moved,Point actual) {
    if constexpr (uses_alternate_routes<Model>) return model.alternate_route_result(frame,moved,actual);
    return false;
}
template<class Model>
inline constexpr bool uses_producer_isolation = requires(Model& model, const ObservationClock& clock) {
    model.isolation_idle(clock);
};
template<class Model>
inline bool isolation_idle(Model& model, const ObservationClock& clock) {
    if constexpr (uses_producer_isolation<Model>) return model.isolation_idle(clock);
    return false;
}

class M0 {
public:
    explicit M0(bool = true) {}
    const char* name() const { return "m0"; }
    void observe_kit(std::uint64_t, std::uint32_t = 0) {}
    void observe_snapshot(const Information&, MemberFrame = {}) {}
    void observe_producers(const ProducerFrame&) {}
    void observe_actors(const ActorFrame&) {}
    void observe_layout(const PublicLayoutFrame&) {}
    void observe_combat(const CombatKit&) {}
    void observe_spatial(const SpatialFrame&) {}
    void static_spatial(const StaticSpatialFrame&) {}
    void observe_movement(const SpatialFrame&) {}
    int spatial_vertices() const { return 0; }
    void selected(const Information&, const Job&, std::uint64_t, bool) {}
    void selection_canceled() {}
    bool wait_for_movement(const ObservationClock&, Point, const Point*, int) { return false; }
    ModelEvidence model_evidence() const { return {}; }
    void prediction_receipts(const Information&, const Job&, PredictionEventSink, void*) const {}
    void observe_lifecycles(const LifecycleFrame&) {}
    bool capture_receipt(const Information&, const CapturedGroup&) { return false; }
    bool phase_started(const PhaseStart&) { return false; }
    void phase_completed(const PhaseReceipt&) {}
    void predict_into(const Information& info, const Job& job, const Menu& menu, Forecast& out) {
        plan4::predict_into(info, job, menu, out);
    }
};

template<class Model>
inline constexpr bool observes_purchases = requires(Model& model, unsigned item, unsigned count) {
    model.before_purchase(item,count); model.after_purchase(item,count);
};
template<class Model>
inline constexpr bool uses_safe_segments = requires(Model& model,const safe_segment::ExecutionBody& body) {
    model.safe_segments_enabled();model.segment_proposal(body);
};
template<class Model>
inline bool safe_segments_enabled(Model& model) {
    if constexpr (uses_safe_segments<Model>) return model.safe_segments_enabled();
    return false;
}
template<class Model>
inline safe_segment::ExecutionProposal segment_proposal(Model& model,const safe_segment::ExecutionBody& body) {
    if constexpr (uses_safe_segments<Model>) return model.segment_proposal(body);
    return {};
}
template<class Model>
inline void segment_attack_completed(Model& model,const safe_segment::ExecutionBody& body,float actual_damage) {
    if constexpr (requires {model.segment_attack_completed(body,actual_damage);}) model.segment_attack_completed(body,actual_damage);
}
template<class Model>
inline void segment_departed(Model& model,const safe_segment::ExecutionBody& body) {
    if constexpr (requires {model.segment_departed(body);}) model.segment_departed(body);
}
template<class Model>
inline void before_purchase(Model& model, unsigned item, unsigned count) {
    if constexpr (observes_purchases<Model>) model.before_purchase(item,count);
}
template<class Model>
inline void after_purchase(Model& model, unsigned item, unsigned count) {
    if constexpr (observes_purchases<Model>) model.after_purchase(item,count);
}

// The common selector stays identical. A model owns and resets its path state
// within predict_into for every candidate, including Habitat's stock ledger.
template<class Model>
inline void evaluate_model(const Information& info, const Menu& menu, Model& model, Evaluation& out) {
    out.menu = menu; out.chosen = -1; out.evaluated_count = 0; out.budget_exhausted = false;
    if constexpr (requires { model.evaluate_into(info, menu, out); }) {
        if (info.work_budget.can_refine && !info.work_budget.can_refine(info.work_budget.context)) out.budget_exhausted = true;
        else model.evaluate_into(info, menu, out);
        // Batch refiners share the same candidates and selector. Only their
        // completed forecast prefix may cross this boundary.
        out.menu = menu; out.chosen = -1;
        if (out.evaluated_count < 0 || out.evaluated_count > menu.count) {
            out.evaluated_count = 0; out.budget_exhausted = true;
        }
        for (int n = 0; n < out.evaluated_count; ++n) if (out.forecasts[n].budget_exhausted) {
            out.evaluated_count = n; out.budget_exhausted = true; break;
        }
    } else {
        for (int n = 0; n < menu.count; ++n) {
            if (info.work_budget.can_refine && !info.work_budget.can_refine(info.work_budget.context)) {
                out.budget_exhausted = true; break;
            }
            model.predict_into(info, menu.jobs[n], menu, out.forecasts[n]);
            if (out.forecasts[n].budget_exhausted) { out.budget_exhausted = true; break; }
            ++out.evaluated_count;
        }
    }
    Menu completed = menu; completed.count = out.evaluated_count;
    out.chosen = choose(info, completed, out.forecasts);
}
} // namespace plan4::runtime
