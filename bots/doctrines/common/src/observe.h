#pragma once
// Pure offline observations and conservative estimates. No SDK queries or handles.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string_view>

namespace p5 {
enum class Knowledge { unknown, current, stale };
template<class T> struct Known {
    T value{};
    Knowledge knowledge = Knowledge::unknown;
    int round = -1;
    bool current(int now) const { return knowledge == Knowledge::current && round == now; }
};
struct ActorObservation {
    int id = -1;
    bool self = false, visible = false, own_team = false;
    Known<int> bag{}, gold{};
};
inline ActorObservation private_view(ActorObservation actor, int now) {
    // Callers cannot preserve a leaked ally/enemy treasury in the policy input.
    if (!actor.self) actor.gold = {};
    if (!actor.visible || !actor.bag.current(now)) actor.bag = {};
    if (actor.self && !actor.gold.current(now)) actor.gold = {};
    return actor;
}
struct RouteLeg {
    int destination = -1, distance = -1;
    bool terrain_and_diagonals_checked = false;
    bool safety_checked = false;
    int turns() const {
        return destination >= 0 && distance >= 0 && distance <= 256 && terrain_and_diagonals_checked
            ? distance + 2 : -1;
    }
};
struct EconomicSettings {
    int capture_rounds = -1, tribute_gems_per_tick = -1, tribute_min_ticks = -1;
    int round_limit = -1, winning_gems = -1, marcella_gold_per_gem = -1;
    bool valid() const {
        return capture_rounds >= 0 && tribute_gems_per_tick > 0 && tribute_min_ticks > 0 &&
            round_limit > 0 && winning_gems > 0 && marcella_gold_per_gem >= 0;
    }
    int channel_ticks(int starting_bag) const {
        if (!valid() || starting_bag <= 0) return -1;
        return std::max(tribute_min_ticks, starting_bag / tribute_gems_per_tick +
            (starting_bag % tribute_gems_per_tick != 0));
    }
};
struct CombatActor {
    int id = -1;
    bool self = false, visible = false, ranged = false;
    double health = 0, max_health = 0, strength = 0;
    double basic_modifier = 1, direct_or_reset_damage = 0;
    bool spell_known_ready = false, legal_first_cast = false, reserved_cast = false;
    // A uniquely assigned ready heal may protect one named recipient only.
    int heal_recipient = -1;
    double heal_amount = 0;
    bool heal_legal_ready = false;
    double shield = 0;
    int shield_expiry_tick = 0;
    double visible_enemy_next_phase = 0;
    bool enemy_cast_bound_known = true;
};
struct CombatMonster {
    int id = -1;
    double public_health = 0, observed_health = 0, effective_strength = 0;
    bool visible = false;
};
struct CombatInput {
    std::array<CombatActor, 6> actors{};
    std::size_t actor_count = 0;
    std::array<CombatMonster, 8> monsters{};
    std::size_t monster_count = 0;
    int hold_ticks = 0;
};
enum class EstimateState { feasible, unsafe, unknown, invalid };
struct CombatForecast {
    EstimateState state = EstimateState::invalid;
    int clear_ticks = 0;
    double worst_camp_damage = 0;
    std::array<double, 6> remaining_health{};
    const char* reason = "invalid-combat-input";
};
inline CombatForecast forecast_combat(const CombatInput& input) {
    CombatForecast out;
    if (!input.actor_count || input.actor_count > 6 || !input.monster_count ||
        input.monster_count > 8 || input.hold_ticks < 0) return out;
    double rate = 0;
    for (std::size_t i = 0; i < input.actor_count; ++i) {
        const auto& actor = input.actors[i];
        if ((!actor.self && !actor.visible) || !actor.enemy_cast_bound_known) {
            out.state = EstimateState::unknown; out.reason = "absent-support-or-unknown-threat"; return out;
        }
        if (actor.id < 0 || actor.health <= 0 || actor.max_health < actor.health ||
            actor.strength < 0 || actor.basic_modifier < 0 || actor.shield < 0 ||
            actor.visible_enemy_next_phase < 0 || actor.heal_amount < 0 || actor.direct_or_reset_damage < 0)
            return out;
        for (std::size_t j = 0; j < i; ++j) if (input.actors[j].id == actor.id) return out;
        const bool healing = actor.heal_legal_ready && actor.heal_recipient >= 0;
        const double spell = actor.spell_known_ready && actor.legal_first_cast &&
            !actor.reserved_cast && !healing ? actor.direct_or_reset_damage / 8 : 0;
        rate += actor.strength * actor.basic_modifier * (actor.ranged ? 0.5 : 1) + spell;
    }
    auto targets = input.monsters;
    for (std::size_t i = 0; i < input.monster_count; ++i) {
        const auto& monster = targets[i];
        if (monster.id < 0 || monster.public_health <= 0 || monster.effective_strength < 0 ||
            (monster.visible && (monster.observed_health <= 0 || monster.observed_health > monster.public_health)))
            return out;
        for (std::size_t j = 0; j < i; ++j) if (targets[j].id == monster.id) return out;
    }
    const auto hp = [](const CombatMonster& m) { return m.visible ? m.observed_health : m.public_health; };
    std::sort(targets.begin(), targets.begin() + input.monster_count, [&](const auto& a, const auto& b) {
        return hp(a) == hp(b) ? a.id < b.id : hp(a) < hp(b);
    });
    for (std::size_t i = 0; i < input.monster_count; ++i) {
        const double turns = std::ceil(hp(targets[i]) / std::max(1.0, rate));
        if (!std::isfinite(turns) || turns > 100000 || out.clear_ticks > 100000 - turns) return out;
        double incoming = 0;
        for (std::size_t j = i; j < input.monster_count; ++j) incoming += targets[j].effective_strength;
        out.clear_ticks += static_cast<int>(turns);
        out.worst_camp_damage += turns * incoming;
        if (i + 1 < input.monster_count) {
            out.clear_ticks += 2;
            out.worst_camp_damage += 2 * (incoming - targets[i].effective_strength);
        }
    }
    for (std::size_t i = 0; i < input.actor_count; ++i) {
        const auto& actor = input.actors[i];
        // Expiring shields receive credit only when they cover the complete exposure.
        const double shield = actor.shield_expiry_tick >= out.clear_ticks + input.hold_ticks ? actor.shield : 0;
        double healed = 0;
        for (std::size_t j = 0; j < input.actor_count; ++j) {
            const auto& healer = input.actors[j];
            if (healer.id != actor.id && healer.heal_legal_ready && healer.heal_recipient == actor.id)
                healed += healer.heal_amount;
        }
        // Only pre-existing missing health is credited: no future rescue from death.
        healed = std::min(healed, actor.max_health - actor.health);
        const double threat = actor.visible_enemy_next_phase;
        out.remaining_health[i] = actor.health + healed + shield - out.worst_camp_damage -
            threat * (out.clear_ticks + input.hold_ticks);
        if (out.remaining_health[i] <= 0 || out.remaining_health[i] < std::max(0.2 * actor.max_health, 2 * threat)) {
            out.state = EstimateState::unsafe; out.reason = "individual-worst-exposure"; return out;
        }
    }
    out.state = EstimateState::feasible; out.reason = "conservative-visible-party"; return out;
}
struct CycleCandidate {
    CombatInput combat{};
    RouteLeg to_group{}, to_altar{}, return_to_work{};
    int starting_bag = 0, reward_gems = 0;
};
struct CycleForecast {
    EstimateState state = EstimateState::invalid;
    CombatForecast combat{};
    int capture_tick = -1, bank_tick = -1, renewal_tick = -1;
    const char* reason = "invalid-cycle-input";
};
inline CycleForecast forecast_cycle(CycleCandidate candidate, const EconomicSettings& settings) {
    CycleForecast out;
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
    out.combat = forecast_combat(candidate.combat);
    out.state = out.combat.state; out.reason = out.combat.reason;
    if (out.state != EstimateState::feasible) return out;
    out.capture_tick = travel + out.combat.clear_ticks + settings.capture_rounds + 1;
    out.bank_tick = out.capture_tick + conversion + settings.channel_ticks(candidate.starting_bag + candidate.reward_gems);
    out.renewal_tick = out.bank_tick + renewal;
    return out;
}
// Draft aliases mirror the staged 3.0.2 SDK catalogue, not a preferred pick order.
inline constexpr std::array<std::string_view, 12> draft_aliases{
    "ryth", "vesper_vale", "vendetta", "cassi_and_issac", "stellar", "prism",
    "marcella", "willow", "ren", "varg", "tavi", "heart_of_ruin"
};
inline int draft_actor_count(std::string_view alias) {
    for (const auto candidate : draft_aliases)
        if (candidate == alias) return alias == "cassi_and_issac" ? 2 : 1;
    return 0;
}
// The adapter supplies public starting traits. These are role heuristics, not
// future equipment, income, guaranteed clears, or a globally optimal draft.
struct DraftTraits {
    std::string_view alias{};
    bool known = false;
    int actors = 0;
    double productivity = 0, health = 0, shielding = 0, healing = 0, mobility = 0;
};
struct DraftWeights {
    double productivity = 0, survival = 0, healing = 0, mobility = 0, coverage = 0;
};
struct DraftContext {
    int team_size = 0, pick_index = -1;
    std::array<std::string_view, 5> own_picks{};
    std::size_t own_count = 0;
    std::array<DraftTraits, 12> pool{};
    std::size_t pool_count = 0;
    // Publicly supported independent work cells; zero confers no body credit.
    int independent_cells = -1;
};
struct DraftRequest {
    std::string_view required_anchor{};
    std::array<DraftWeights, 5> roles{};
};
struct DraftDecision {
    bool feasible = false;
    std::string_view alias{};
    double score = 0;
    const char* reason = "invalid-draft-context";
};
inline DraftDecision choose_draft(const DraftContext& context, const DraftRequest& request) {
    DraftDecision out;
    if (context.team_size < 3 || context.team_size > 5 || context.pick_index < 0 ||
        context.pick_index >= context.team_size || context.own_count > context.own_picks.size() ||
        context.own_count != static_cast<std::size_t>(context.pick_index) ||
        context.pool_count > context.pool.size() || context.independent_cells < 0) return out;
    int own_actors = 0;
    for (std::size_t i = 0; i < context.own_count; ++i) {
        const int actors = draft_actor_count(context.own_picks[i]);
        if (!actors) return out;
        for (std::size_t j = 0; j < i; ++j)
            if (context.own_picks[i] == context.own_picks[j]) return out;
        own_actors += actors;
    }
    const auto nonnegative = [](double value) { return std::isfinite(value) && value >= 0; };
    out.reason = "invalid-draft-request";
    if (!request.required_anchor.empty() && !draft_actor_count(request.required_anchor)) return out;
    for (const auto& weights : request.roles)
        if (!nonnegative(weights.productivity) || !nonnegative(weights.survival) ||
            !nonnegative(weights.healing) || !nonnegative(weights.mobility) ||
            !nonnegative(weights.coverage)) return out;
    out.reason = "invalid-draft-pool";
    for (std::size_t i = 0; i < context.pool_count; ++i) {
        const auto& candidate = context.pool[i];
        const int actors = draft_actor_count(candidate.alias);
        if (!actors || candidate.actors != actors || !nonnegative(candidate.productivity) ||
            !nonnegative(candidate.health) || !nonnegative(candidate.shielding) ||
            !nonnegative(candidate.healing) || !nonnegative(candidate.mobility)) return out;
        for (std::size_t j = 0; j < i; ++j)
            if (context.pool[j].alias == candidate.alias) return out;
    }
    const auto already_picked = [&](std::string_view alias) {
        for (std::size_t i = 0; i < context.own_count; ++i)
            if (context.own_picks[i] == alias) return true;
        return false;
    };
    const bool needs_anchor = !request.required_anchor.empty() && !already_picked(request.required_anchor);
    const auto& weights = request.roles[context.pick_index];
    const int free_cells = std::max(0, context.independent_cells - own_actors);
    out.reason = needs_anchor ? "unknown-required-anchor" : "no-known-legal-candidate";
    for (std::size_t i = 0; i < context.pool_count; ++i) {
        const auto& candidate = context.pool[i];
        if (!candidate.known || already_picked(candidate.alias) ||
            (needs_anchor && candidate.alias != request.required_anchor)) continue;
        const double survival = weights.survival == 0 ? 0 :
            weights.survival * candidate.health + weights.survival * candidate.shielding;
        const double score = weights.productivity * candidate.productivity + survival +
            weights.healing * candidate.healing + weights.mobility * candidate.mobility +
            weights.coverage * std::min(candidate.actors, free_cells);
        if (!std::isfinite(score)) return {false, {}, 0, "unrepresentable-draft-score"};
        if (!out.feasible || score > out.score || (score == out.score && candidate.alias < out.alias))
            out = {true, candidate.alias, score, needs_anchor ? "required-anchor" : "weighted-public-traits"};
    }
    return out;
}
} // namespace p5
