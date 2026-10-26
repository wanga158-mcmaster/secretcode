#pragma once
#include "repairman.h"
#include "../../common/model-runtime.h"

namespace plan4::repairman::runtime {
enum class Unsupported { none, stale, actor, kit, allowance, layout, health, lifecycle, escape, safety, budget,
                         executor_and_continuation_missing };
// Supplemental legal observations required at the SDK action boundary. The
// existing generic frames do not carry these facts. No field may be filled by
// inferring an unseen target, enemy absence, inventory or future reobservation.
struct AttackFrame {
    plan4::runtime::ObservationClock clock{};
    int group_id = -1, target_member_id = -1;
    unsigned lifecycle_version = 0;
    bool lifecycle_version_supported = false;
    bool can_attack_now = false, ordinary_basic_only = false, stationary = true;
    bool ability_timing_changed = false, emergency = false, new_visible_rival = false;
    float basic_damage = 0, incoming_upper_bound = 0;
    std::uint64_t kit_version = 0;
    std::uint32_t stat_epoch = 0;
    Provenance action_quote{}, combat_quote{};
    int legal_recall_ticks = -1;
    Provenance recall_quote{};
    plan4::runtime::PublicLayoutFrame layout{};
    plan4::runtime::MemberFrame members{};
    safe_segment::EscapeScene safety{};
};
struct PreparedAttack {
    AttackContext actor{};
    safe_segment::EscapeBuild escape{};
    SegmentQuote segment{};
    safe_segment::RestoreQuote restoration{};
    GroupObservation group{};
    float basic_damage = 0;
    std::uint64_t kit_version = 0;
    std::uint32_t stat_epoch = 0;
    int group_index = -1;
    Unsupported unsupported = Unsupported::stale;
    bool supported = false, context_supported = false;
};
inline bool quoted_now(Provenance p, int round) {
    return p.observed_round == round && p.source == Source::own_state &&
        (p.status == Status::exact_now || p.status == Status::derived);
}
inline bool prepare_attack_into(const Information& info, const plan4::runtime::ActorFrame& actors,
                                const plan4::runtime::CombatKit& kit, const AttackFrame& frame,
                                PreparedAttack& out, bool context_only = false, int graph_quota = 64) {
    out.supported = out.context_supported = false; out.unsupported = Unsupported::stale; out.group_index = -1;
    out.actor = {}; out.group = {}; out.basic_damage = 0; out.kit_version = 0; out.stat_epoch = 0; out.segment = {}; out.restoration = {};
    out.escape.proof.supported = out.escape.proof.based_on_observed_threats = out.escape.proof.endpoint_safe = false;
    out.escape.path_count = out.escape.vertices = 0; out.escape.failure = safe_segment::BuildFailure::context;
    const auto budget = [&] { return !info.work_budget.can_refine || info.work_budget.can_refine(info.work_budget.context); };
    const auto fresh = [&](Provenance p) { return safe_segment::current_exact(p,info.round,Source::personal_vision); };
    const auto clock = [&](plan4::runtime::ObservationClock c) { return c.round == info.round && c.own.id == info.own.id; };
    const auto reject = [&](Unsupported why) { out.unsupported = why; return false; };
    if (!budget()) return reject(Unsupported::budget);
    if (!clock(actors.clock) || !clock(kit.clock) || !clock(frame.clock) || !clock(frame.layout.clock)) return false;
    if (!actors.own_observed || actors.truncated || !info.own.alive || info.own.channel != Channel::none ||
        actors.own.id != info.own.id || !actors.own.alive || actors.own.position != info.own.position ||
        actors.own.health != info.own.health || actors.own.maximum_health != info.own.maximum_health ||
        actors.own.bag != info.own.bag || !safe_segment::current_exact(actors.own.provenance,info.round,Source::own_state))
        return reject(Unsupported::actor);
    if (!kit.exact || kit.version != frame.kit_version || kit.stat_epoch != frame.stat_epoch || kit.champion != actors.own.champion || kit.attack_range < 1 ||
        kit.attack_range > 3 || kit.attack_range != actors.own.attack_range || kit.strength != actors.own.strength ||
        !std::isfinite(kit.strength) || kit.strength <= 0 || !quoted_now(frame.combat_quote,info.round) ||
        !std::isfinite(frame.basic_damage) || frame.basic_damage <= 0 || !valid_loss(frame.incoming_upper_bound))
        return reject(Unsupported::kit);
    if ((!context_only && !frame.can_attack_now) || !frame.ordinary_basic_only || !frame.stationary || frame.ability_timing_changed ||
        frame.emergency || frame.new_visible_rival || !quoted_now(frame.action_quote,info.round)) return reject(Unsupported::allowance);
    if (!frame.lifecycle_version_supported) return reject(Unsupported::lifecycle);
    if (info.public_layout_truncated || frame.layout.truncated || frame.layout.group_count < 1 || frame.layout.group_count > 8 ||
        frame.layout.member_count < 1 || frame.layout.member_count > 256 || !frame.layout.groups || !frame.layout.members ||
        frame.members.truncated || frame.members.count < 1 || frame.members.count > 256 || !frame.members.samples ||
        info.group_count < 0 || info.group_count > max_groups) return reject(Unsupported::layout);
    for (int g = 0; g < info.group_count; ++g) {
        if ((g % 8) == 0 && !budget()) return reject(Unsupported::budget);
        if (info.groups[g].id == frame.group_id) { if (out.group_index >= 0) return reject(Unsupported::layout); out.group_index = g; }
    }
    if (out.group_index < 0) return reject(Unsupported::layout);
    const auto& group = info.groups[out.group_index];
    if (!group.visible || !group.all_spawns_visible || !group.occupancy_known || group.contested ||
        group.lifecycle != Lifecycle::living || !fresh(group.provenance) || !fresh(group.health_provenance) ||
        !valid_loss(group.health) || group.health <= 0 || !valid_loss(group.incoming_damage)) return reject(Unsupported::health);
    const plan4::runtime::PublicGroup* composition = nullptr;
    for (int g = 0; g < frame.layout.group_count; ++g) if (frame.layout.groups[g].group_id == frame.group_id) {
        if (composition) return reject(Unsupported::layout);
        composition = &frame.layout.groups[g];
    }
    if (!composition || !composition->complete || composition->count < 1 || composition->count > 32 || composition->first < 0 ||
        composition->first + composition->count > frame.layout.member_count) return reject(Unsupported::layout);
    float health = 0, maximum = 0; int alive = 0, target_range = -1;
    const plan4::runtime::VisibleMember* target = nullptr;
    for (int m = 0; m < composition->count; ++m) {
        if (!budget()) return reject(Unsupported::budget);
        const auto& expected = frame.layout.members[composition->first + m];
        if (expected.group_id != group.id || expected.member_id < 0 || expected.ordinal != m ||
            expected.declared_members != composition->count || expected.attack_range < 0 ||
            !std::isfinite(expected.maximum_health) || expected.maximum_health <= 0)
            return reject(Unsupported::layout);
        for (int p = 0; p < m; ++p) if (frame.layout.members[composition->first + p].member_id == expected.member_id ||
            frame.layout.members[composition->first + p].spawn == expected.spawn) return reject(Unsupported::layout);
        const plan4::runtime::VisibleMember* observed = nullptr;
        for (int v = 0; v < frame.members.count; ++v) {
            if ((v % 8) == 0 && !budget()) return reject(Unsupported::budget);
            const auto& candidate = frame.members.samples[v];
            if (candidate.member_id == expected.member_id && candidate.group_id == group.id) {
                if (observed) return reject(Unsupported::health);
                observed = &candidate;
            }
        }
        if (!observed || !fresh(observed->provenance) || observed->spawn != expected.spawn || observed->ordinal != m ||
            observed->declared_members != composition->count || !valid_loss(observed->health) || observed->health > expected.maximum_health)
            return reject(Unsupported::health);
        health += observed->health; maximum += expected.maximum_health;
        if (observed->health > 0) {
            ++alive;
            if (observed->member_id == frame.target_member_id) { target = observed; target_range = expected.attack_range; }
        }
    }
    if (alive != 1 || !target || health != group.health || maximum != group.full_health ||
        (!context_only && std::max(std::abs(target->spawn.x - info.own.position.x),std::abs(target->spawn.y - info.own.position.y)) > kit.attack_range))
        return reject(Unsupported::health);
    if (frame.safety.own_spawn != info.own_spawn) return reject(Unsupported::escape);
    if (frame.safety.threat_count < 0 || frame.safety.threat_count > safe_segment::max_stationary_threats)
        return reject(Unsupported::escape);
    bool target_threat = false;
    for (int t = 0; t < frame.safety.threat_count; ++t) if (frame.safety.threats[t].id == frame.target_member_id) {
        const auto& threat = frame.safety.threats[t]; target_threat = true;
        if (!threat.alive || threat.position != target->spawn || threat.attack_range != target_range ||
            threat.incoming_upper_bound < group.incoming_damage) return reject(Unsupported::escape);
    }
    if (group.incoming_damage > 0 && !target_threat) return reject(Unsupported::escape);
    auto& actor = out.actor; actor = {};
    actor.observed_round = info.round; actor.actor_id = info.own.id; actor.target_id = frame.target_member_id;
    actor.position = info.own.position; actor.health = info.own.health; actor.maximum_health = info.own.maximum_health;
    actor.predicted_incoming = frame.incoming_upper_bound; actor.already_attackable = frame.can_attack_now;
    actor.legal_recall_ticks = frame.legal_recall_ticks; actor.recall_quote = frame.recall_quote; actor.work_budget = info.work_budget;
    out.group = {group.id,info.round,frame.lifecycle_version,Lifecycle::living,group.health,group.health_provenance,true,true,false,false,true};
    out.basic_damage = frame.basic_damage; out.kit_version = kit.version; out.stat_epoch = kit.stat_epoch;
    out.context_supported = true;
    if (context_only) { out.unsupported = Unsupported::none; return true; }
    struct GraphBudget { WorkBudget outer; const safe_segment::EscapeBuild* result; int quota; };
    GraphBudget graph{info.work_budget,&out.escape,std::max(0,std::min(64,graph_quota))};
    actor.work_budget={[](const void*raw){const auto&g=*static_cast<const GraphBudget*>(raw);
        return (!g.outer.can_refine||g.outer.can_refine(g.outer.context))&&g.result->vertices<g.quota;},&graph};
    safe_segment::build_escape_into(actor,info.settings,frame.safety,out.escape);
    actor.work_budget=info.work_budget;
    if (out.escape.failure != safe_segment::BuildFailure::none)
        return reject(out.escape.failure == safe_segment::BuildFailure::budget ? Unsupported::budget : Unsupported::escape);
    out.segment = safe_segment_quote(actor,out.escape.proof,info.settings);
    if (out.segment.failure != Failure::none) return reject(out.segment.failure == Failure::budget ? Unsupported::budget : Unsupported::safety);
    out.restoration = safe_segment::quote_restore(actor,out.segment,out.escape,info.settings,info.round);
    if (!out.restoration.supported) return reject(Unsupported::safety);
    out.group = {group.id,info.round,frame.lifecycle_version,Lifecycle::living,group.health,group.health_provenance,true,true,false,false,true};
    out.basic_damage = frame.basic_damage; out.kit_version = kit.version; out.stat_epoch = kit.stat_epoch;
    out.unsupported = Unsupported::none; out.supported = true; return true;
}

// Preparation is shared by both arms. It deliberately does not rank a private
// supported subset of the fixed menu, nor execute an SDK action. Before a main
// can use this model, the integrator must (1) provide AttackFrame at each actual
// action, (2) execute the same <=4-attack/escape/recall/heal safety in BOTH arms,
// (3) reobserve before return attacks, (4) connect partial remaining-health stock
// plus exact phase/output endpoints to the full fixed macro continuation. A
// generic duration-only hook or aggregate-clear checkpoint cannot do (4).
class Preparation {
    bool enabled_ = true;
public:
    explicit Preparation(bool enabled = true) : enabled_(enabled) {}
    bool prepare_into(const Information& info, const plan4::runtime::ActorFrame& actors,
                      const plan4::runtime::CombatKit& kit, const AttackFrame& frame, PreparedAttack& out) const {
        return prepare_attack_into(info,actors,kit,frame,out);
    }
    CycleForecast forecast_cycle(const CycleInput& input) const { return predict_two_excursions(input,enabled_); }
    static constexpr bool complete_runtime_available = false;
    static constexpr Unsupported runtime_blocker = Unsupported::executor_and_continuation_missing;
};
} // namespace plan4::repairman::runtime
#include "excursion-process.h"
