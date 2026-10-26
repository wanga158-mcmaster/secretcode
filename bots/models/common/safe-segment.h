#pragma once

#include "pure.h"
#include <array>
#include <cmath>

namespace plan4::safe_segment {
inline constexpr const char* version = "plan4-safe-segment/1";
inline constexpr int max_escape_phases = 32;
enum class Failure { none, unsupported, not_attackable, changed_segment, emergency,
                     unsafe_segment, unsafe_escape, unsafe_recall, stale_observation,
                     new_lifecycle, rival, unfinished, horizon, budget };
struct Exposure {
    float incoming = 0;
    bool known = false, ordinary_action = false, legal = false;
};
struct EscapeProof {
    int observed_round = -1, escape_count = 0, recall_ticks = -1, recall_phase_count = 0;
    std::array<Exposure, max_escape_phases> escape{};
    std::array<Exposure, max_horizon> recall{};
    bool supported = false, based_on_observed_threats = false, endpoint_safe = false;
};
struct AttackContext {
    int observed_round = -1, actor_id = -1, target_id = -1;
    float health = 0, maximum_health = 0, predicted_incoming = 0;
    Point position{};
    bool alive = true, already_attackable = false, stationary = true;
    bool target_changed = false, ability_timing_changed = false;
    bool emergency = false, new_visible_rival = false, escape_premise_lost = false;
    // An actual kit-adjusted recall quote may replace the queried base duration.
    // No optional ability is assumed by this adapter.
    int legal_recall_ticks = -1;
    Provenance recall_quote{};
    WorkBudget work_budget{};
};
struct SegmentQuote {
    int attacks = 0, escape_phases = 0, recall_ticks = 0, elapsed_rounds = 0;
    float boundary = 0, health_after_attacks = 0, health_after_extra_exposure = 0;
    float health_after_escape = 0, health_at_recall_arrival = 0;
    Status status = Status::predicted;
    Failure failure = Failure::unsupported;
};
inline bool valid_loss(float loss) { return std::isfinite(loss) && loss >= 0; }
inline bool work_available(const AttackContext& context) {
    return !context.work_budget.can_refine || context.work_budget.can_refine(context.work_budget.context);
}
inline float retreat_boundary(const AttackContext& context) {
    return std::max(context.maximum_health * 0.35f, context.predicted_incoming * 2.0f + 60.0f);
}
inline int recall_ticks(const AttackContext& context, const Settings& settings) {
    if (context.legal_recall_ticks < 0) return std::max(1, settings.recall_rounds);
    const auto& proof = context.recall_quote;
    if (context.legal_recall_ticks < 1 || proof.observed_round != context.observed_round ||
        proof.source != Source::own_state || (proof.status != Status::exact_now && proof.status != Status::derived)) return -1;
    return context.legal_recall_ticks;
}
inline Failure validate_escape(const AttackContext& context, const EscapeProof& proof, const Settings& settings) {
    if (!work_available(context)) return Failure::budget;
    if (!proof.supported || !proof.based_on_observed_threats || !proof.endpoint_safe ||
        proof.observed_round != context.observed_round || proof.escape_count < 0 ||
        proof.escape_count > max_escape_phases || context.escape_premise_lost) return Failure::unsupported;
    if (context.predicted_incoming > 0 && proof.escape_count == 0) return Failure::unsupported;
    const int ticks = recall_ticks(context, settings);
    if (ticks < 1 || ticks > max_horizon || proof.recall_ticks != ticks || proof.recall_phase_count != ticks)
        return Failure::unsafe_recall;
    for (int n = 0; n < proof.escape_count; ++n) {
        if (!work_available(context)) return Failure::budget;
        if (!proof.escape[n].known || !proof.escape[n].ordinary_action || !proof.escape[n].legal ||
            !valid_loss(proof.escape[n].incoming)) return Failure::unsupported;
    }
    for (int n = 0; n < ticks; ++n) {
        if (!work_available(context)) return Failure::budget;
        if (!proof.recall[n].known || !proof.recall[n].legal || !valid_loss(proof.recall[n].incoming)) return Failure::unsupported;
        if (proof.recall[n].incoming > 0) return Failure::unsafe_recall; // Damage interrupts recall.
    }
    return Failure::none;
}
inline SegmentQuote quote_segment(const AttackContext& context, const EscapeProof& proof,
                                  const Settings& settings, int attacks, bool apply_boundary = true) {
    SegmentQuote result;
    result.boundary = retreat_boundary(context);
    if (context.observed_round < 0 || context.actor_id < 0 || context.target_id < 0 ||
        !context.alive || !std::isfinite(context.health) || !std::isfinite(context.maximum_health) ||
        context.health <= 0 || context.maximum_health <= 0 || !valid_loss(context.predicted_incoming)) return result;
    if (context.emergency || context.new_visible_rival) { result.failure = Failure::emergency; return result; }
    if (!context.already_attackable) { result.failure = Failure::not_attackable; return result; }
    if (!context.stationary || context.target_changed || context.ability_timing_changed) {
        result.failure = Failure::changed_segment; return result;
    }
    if (attacks < 0 || attacks > 4) return result;
    result.failure = validate_escape(context, proof, settings);
    if (result.failure != Failure::none) return result;
    result.health_after_attacks = context.health - attacks * context.predicted_incoming;
    result.health_after_extra_exposure = context.health - (attacks + 1) * context.predicted_incoming;
    if (apply_boundary && result.health_after_extra_exposure <= result.boundary) {
        result.failure = Failure::unsafe_segment; return result;
    }
    float health = result.health_after_extra_exposure;
    for (int n = 0; n < proof.escape_count; ++n) {
        health -= proof.escape[n].incoming;
        if (health <= 0) { result.failure = Failure::unsafe_escape; return result; }
    }
    result.attacks = attacks; result.escape_phases = proof.escape_count;
    result.recall_ticks = recall_ticks(context, settings);
    result.elapsed_rounds = attacks + 1 + proof.escape_count + result.recall_ticks;
    result.health_after_escape = result.health_at_recall_arrival = health;
    result.failure = Failure::none;
    return result;
}
// Active and disabled arms call this same utility, with no optional-ability
// admission gate. SDK execution must preserve these quoted phase boundaries.
inline SegmentQuote safe_segment_quote(const AttackContext& context, const EscapeProof& proof, const Settings& settings) {
    SegmentQuote last;
    for (int attacks = 4; attacks >= 1; --attacks) {
        auto quote = quote_segment(context, proof, settings, attacks);
        if (quote.failure == Failure::none) return quote;
        if (quote.failure != Failure::unsafe_segment && quote.failure != Failure::unsafe_escape) return quote;
        last = quote;
    }
    return last;
}
struct SegmentToken {
    int actor_id = -1, target_id = -1, admitted_round = -1, remaining_attacks = 0;
    Point position{};
    bool active = false;
    float maximum_health = 0, admitted_incoming = 0, escape_loss = 0;
};
inline SegmentToken begin_segment(const AttackContext& context, const SegmentQuote& quote) {
    return {context.actor_id, context.target_id, context.observed_round, quote.attacks, context.position,
            quote.failure == Failure::none && quote.attacks > 0, context.maximum_health, context.predicted_incoming,
            std::max(0.0f,quote.health_after_extra_exposure - quote.health_at_recall_arrival)};
}
inline bool segment_still_valid(const SegmentToken& token, const AttackContext& context) {
    if (token.remaining_attacks < 1 || token.remaining_attacks > 4) return false;
    const float remaining_health = context.health - (token.remaining_attacks + 1) * context.predicted_incoming;
    return context.alive && std::isfinite(context.health) && context.health > 0 &&
        std::isfinite(context.maximum_health) && context.maximum_health > 0 &&
        valid_loss(context.predicted_incoming) && context.predicted_incoming <= token.admitted_incoming &&
        context.maximum_health == token.maximum_health && valid_loss(token.escape_loss) &&
        remaining_health > retreat_boundary(context) && remaining_health > token.escape_loss &&
        token.active && token.remaining_attacks > 0 && token.remaining_attacks <= 4 && token.actor_id == context.actor_id &&
        token.target_id == context.target_id && token.position == context.position && context.stationary &&
        !context.target_changed && !context.ability_timing_changed && !context.emergency &&
        !context.new_visible_rival && !context.escape_premise_lost && context.already_attackable &&
        context.observed_round >= token.admitted_round && std::int64_t(context.observed_round) <= std::int64_t(token.admitted_round) + 4;
}
inline void consumed_attack(SegmentToken& token) {
    if (token.active && token.remaining_attacks > 0 && --token.remaining_attacks == 0) token.active = false;
}

inline constexpr int max_escape_vertices = 64, max_escape_depth = 8, max_stationary_threats = 64;
struct PackedOccupancy {
    int width = 0, height = 0;
    const std::uint8_t* visible = nullptr;
    const std::uint8_t* occupied = nullptr;
    std::size_t bytes = 0;
    bool complete = false;
    bool contains(Point p) const { return p.x >= 0 && p.y >= 0 && p.x < width && p.y < height; }
    bool valid() const {
        const auto tiles = std::int64_t(width) * height;
        return complete && width > 0 && height > 0 && tiles <= 16384 && visible && occupied &&
            bytes >= std::size_t((tiles + 7) / 8);
    }
    bool known_free(Point p, Point own) const {
        if (!contains(p)) return false;
        const unsigned key = unsigned(p.y * width + p.x), bit = 1u << (key & 7);
        return (visible[key >> 3] & bit) && (p == own || !(occupied[key >> 3] & bit));
    }
    bool known(Point p) const {
        if (!contains(p)) return false;
        const unsigned key = unsigned(p.y * width + p.x);
        return visible[key >> 3] & (1u << (key & 7));
    }
};
struct StationaryThreat {
    int id = -1, attack_range = -1;
    Point position{};
    float incoming_upper_bound = 0;
    Provenance provenance{};
    bool stationary = false, alive = false;
};
struct EnemyExclusion {
    Provenance provenance{};
    // Complete enumeration/exclusion of personally visible enemies. This is
    // explicitly conditional on current observations, not unseen future actors.
    bool visible_set_complete = false, visible_threats_excluded = false;
};
struct EscapeScene {
    int observed_round = -1, actor_id = -1;
    bool topology_complete = false;
    bool (*walkable)(Point, const void*) = nullptr;
    const void* topology_context = nullptr;
    PackedOccupancy occupancy{};
    std::array<StationaryThreat, max_stationary_threats> threats{};
    int threat_count = 0;
    // The adapter must join the public composition against fresh observations
    // for every stationary threat which could reach the depth-eight envelope.
    bool relevant_threats_complete = false;
    EnemyExclusion enemies{};
    Point own_spawn{};
    Provenance spawn_quote{};
    bool physical_spawn_known = false, recall_arrival_supported = false;
};
enum class BuildFailure { none, context, topology, occupancy, threats, enemies, recall, spawn, exhausted, budget };
struct EscapeBuild {
    EscapeProof proof{};
    std::array<Point, max_escape_depth> path{};
    Point endpoint{}, recall_destination{};
    int path_count = 0, vertices = 0;
    BuildFailure failure = BuildFailure::context;
};
inline bool current_exact(Provenance p, int round, Source source) {
    return p.observed_round == round && p.source == source && p.status == Status::exact_now;
}
inline void build_escape_into(const AttackContext& actor, const Settings& settings,
                              const EscapeScene& scene, EscapeBuild& out) {
    out.proof.supported = out.proof.based_on_observed_threats = out.proof.endpoint_safe = false;
    out.proof.escape_count = out.proof.recall_phase_count = out.path_count = out.vertices = 0;
    out.failure = BuildFailure::context;
    if (!work_available(actor)) { out.failure = BuildFailure::budget; return; }
    if (actor.observed_round < 0 || actor.observed_round != scene.observed_round || actor.actor_id != scene.actor_id ||
        !actor.alive || !valid_loss(actor.health) || actor.health <= 0 || !valid_loss(actor.predicted_incoming)) return;
    if (!scene.topology_complete || !scene.walkable) { out.failure = BuildFailure::topology; return; }
    if (!scene.occupancy.valid() || !scene.occupancy.known_free(actor.position, actor.position) ||
        !scene.walkable(actor.position, scene.topology_context)) { out.failure = BuildFailure::occupancy; return; }
    if (!scene.relevant_threats_complete || scene.threat_count < 0 || scene.threat_count > max_stationary_threats) {
        out.failure = BuildFailure::threats; return;
    }
    if (!current_exact(scene.enemies.provenance, actor.observed_round, Source::personal_vision) ||
        !scene.enemies.visible_set_complete || !scene.enemies.visible_threats_excluded) { out.failure = BuildFailure::enemies; return; }
    const int ticks = recall_ticks(actor, settings);
    if (ticks < 1 || ticks > max_horizon) { out.failure = BuildFailure::recall; return; }
    if (!scene.physical_spawn_known || !scene.recall_arrival_supported ||
        scene.spawn_quote.observed_round != actor.observed_round || scene.spawn_quote.source != Source::own_state ||
        (scene.spawn_quote.status != Status::exact_now && scene.spawn_quote.status != Status::derived) ||
        !scene.occupancy.contains(scene.own_spawn) || !scene.walkable(scene.own_spawn, scene.topology_context)) {
        out.failure = BuildFailure::spawn; return;
    }
    for (int t = 0; t < scene.threat_count; ++t) {
        if (!work_available(actor)) { out.failure = BuildFailure::budget; return; }
        const auto& threat = scene.threats[t];
        if (threat.id < 0 || threat.attack_range < 0 || threat.attack_range > max_horizon || !threat.stationary ||
            !current_exact(threat.provenance, actor.observed_round, Source::personal_vision) ||
            !valid_loss(threat.incoming_upper_bound) || !scene.occupancy.known(threat.position)) {
            out.failure = BuildFailure::threats; return;
        }
        for (int p = 0; p < t; ++p) if (scene.threats[p].id == threat.id) { out.failure = BuildFailure::threats; return; }
    }
    auto incoming = [&](Point p) {
        float total = 0;
        for (int t = 0; t < scene.threat_count; ++t) {
            if ((t % 8) == 0 && !work_available(actor)) { out.failure = BuildFailure::budget; return -1.0f; }
            const auto& threat = scene.threats[t];
            if (threat.alive && std::max(std::abs(threat.position.x - p.x), std::abs(threat.position.y - p.y)) <= threat.attack_range)
                total += threat.incoming_upper_bound;
        }
        return std::isfinite(total) ? total : -1.0f;
    };
    struct Node { Point p; int parent, depth; float incoming; };
    std::array<Node, max_escape_vertices> queue;
    const float start_incoming = incoming(actor.position);
    if (start_incoming < 0) { if (out.failure != BuildFailure::budget) out.failure = BuildFailure::threats; return; }
    if (actor.predicted_incoming < start_incoming) { out.failure = BuildFailure::threats; return; }
    queue[0] = {actor.position, -1, 0, start_incoming};
    int count = 1, found = -1;
    for (int head = 0; head < count && out.vertices < max_escape_vertices; ++head) {
        if (!work_available(actor)) { out.failure = BuildFailure::budget; return; }
        ++out.vertices; const auto node = queue[head];
        if (node.incoming == 0 && (head != 0 || actor.predicted_incoming == 0)) { found = head; break; }
        if (node.depth == max_escape_depth) continue;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            if (!work_available(actor)) { out.failure = BuildFailure::budget; return; }
            if (dx == 0 && dy == 0) continue;
            Point p{node.p.x + dx, node.p.y + dy};
            if (!scene.occupancy.known_free(p, actor.position) || !scene.walkable(p, scene.topology_context)) continue;
            bool seen = false;
            for (int n = 0; n < count; ++n) seen |= queue[n].p == p;
            if (seen || count == max_escape_vertices) continue;
            const float loss = incoming(p);
            if (loss < 0) { if (out.failure != BuildFailure::budget) out.failure = BuildFailure::threats; return; }
            queue[count++] = {p, head, node.depth + 1, loss};
        }
    }
    if (found < 0) { out.failure = BuildFailure::exhausted; return; }
    const auto endpoint = queue[found];
    out.path_count = endpoint.depth; out.endpoint = endpoint.p; out.recall_destination = scene.own_spawn;
    for (int n = endpoint.depth - 1, at = found; n >= 0; --n, at = queue[at].parent) {
        out.path[n] = queue[at].p;
        // The k+1 reserve covers the camp phase before the first move. Each
        // route edge additionally charges the next camp phase at its new tile.
        out.proof.escape[n] = {queue[at].incoming, true, true, true};
    }
    for (int t = 0; t < ticks; ++t) out.proof.recall[t] = {0, true, false, true};
    out.proof.observed_round = actor.observed_round; out.proof.escape_count = out.path_count;
    out.proof.recall_ticks = out.proof.recall_phase_count = ticks;
    out.proof.supported = out.proof.based_on_observed_threats = out.proof.endpoint_safe = true;
    out.failure = BuildFailure::none;
}
struct RestoreQuote {
    Point destination{};
    int arrival_round = -1, completion_round = -1, healing_turn_ends = 0;
    float arrival_health = 0, final_health = 0;
    bool supported = false;
};
inline RestoreQuote quote_restore(const AttackContext& actor, const SegmentQuote& segment,
                                  const EscapeBuild& escape, const Settings& settings, int start_round) {
    RestoreQuote out;
    if (escape.failure != BuildFailure::none || !escape.proof.supported || segment.failure != Failure::none ||
        start_round < actor.observed_round || start_round > settings.max_rounds || segment.elapsed_rounds < 0 ||
        segment.elapsed_rounds > settings.max_rounds - start_round || !valid_loss(segment.health_at_recall_arrival) ||
        segment.health_at_recall_arrival <= 0 || !valid_loss(actor.maximum_health) || actor.maximum_health <= 0 ||
        segment.health_at_recall_arrival > actor.maximum_health) return out;
    const double healing = double(actor.maximum_health) * settings.spawn_heal_fraction;
    if (!std::isfinite(healing) || healing <= 0) return out;
    const double heals = std::ceil((actor.maximum_health - double(segment.health_at_recall_arrival)) / healing);
    if (heals > max_horizon) return out;
    out.destination = escape.recall_destination; out.arrival_round = start_round + segment.elapsed_rounds;
    // Completion is the following owner-start after the final needed turn-end
    // heal; an arrival never grants a free earlier ordinary action at full HP.
    out.healing_turn_ends = int(heals);
    if (out.healing_turn_ends > settings.max_rounds - out.arrival_round) return out;
    out.completion_round = out.arrival_round + out.healing_turn_ends;
    out.arrival_health = segment.health_at_recall_arrival; out.final_health = actor.maximum_health; out.supported = true;
    return out;
}

} // namespace plan4::safe_segment
