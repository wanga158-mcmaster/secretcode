#pragma once
#include "profile.h"
#include "../../common/src/accounting.h"
#include "../../common/src/execute.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace p5::endowment {
inline constexpr int actor_cap = 3;
enum class Job { unavailable, preserve, recover, inspect, bootstrap, bank_rebuild, retain_rebuild, independent, winning_bank };
enum class StepKind { produce, support, bank, purchase, renew };
struct Focus {
    double permanent = -1, current = -1, per_gem = -1;
    std::array<double, 2> modifiers{}; // queried primary-target damage scaling only
    int observation_round = -1;
    int valid_until_round = -1;
    bool base_is_permanent = false;
};
struct Purchase {
    int family = -1, item = -1, shards = 0, cost = 0;
    double strength_gain = 0, health_gain = 0, focus_gain = 0, actual_spawn_purchase_heal = 0;
    bool legal_on_spawn = false;
};
struct Group {
    int id = -1, gems = 0, gold = 0;
    bool live_observed = false, reward_or_alive = false;
    std::array<CombatMonster, 8> monsters{};
    std::size_t monster_count = 0;
    RouteLeg from_current{}, from_altar{}, from_spawn{}, to_altar{}, return_to_work{};
    // The first represented cast needs its own current/checked target geometry.
    std::array<execution::Spell, 2> prism_casts{};
};
struct ActorInput {
    ActorObservation resources{};
    CombatActor stats{};
    Group independent{}; // complete unchanged helper alternative, one group
    RouteLeg to_bootstrap{}, after_bootstrap{};
    bool tribute_income_eligible = false;
};
struct Observation {
    int round = 0, team_score = 0, actor_count = 1, prism_index = 0;
    std::array<ActorInput, actor_cap> actors{};
    Focus focus{};
    std::array<Group, 8> groups{};
    std::size_t group_count = 0;
    int bootstrap_group = -1, rebuild_group = -1;
    std::array<Purchase, 3> purchases{};
    std::size_t purchase_count = 0;
    RouteLeg current_to_altar{}, altar_to_spawn{}, final_return{};
    int spawn_rest_ticks = 1;
    double queried_spawn_heal_per_tick = 0;
    EconomicSettings settings{};
    FinishWindows windows{};
    execution::Observation execution{};
    bool bank_legal = false, safe_bank_reachable = false;
    bool safe_active_capture = false, needs_recovery = false;
    bool bootstrap_partner_visible_usable = false;
    bool concentrate_bootstrap = true;
    bool final_return_productive = false;
};
struct Step {
    StepKind kind = StepKind::renew;
    int group = -1, expected_gems = 0, expected_gold = 0, cast_slot = -1, cast_target = -1;
    int purchase_item = -1, purchase_family = -1, purchase_cost = 0, waypoint = -1;
    double expected_focus = -1, expected_permanent_focus = -1;
    double expected_health = 0, expected_strength = 0, expected_max_health = 0, expected_basic_modifier = 0;
    bool coalition = false;
    bool withdraw_when_safe = false;
};
struct ActorPlan {
    std::array<Step, 8> steps{};
    int count = 0, actor = -1, initial_bag = 0, initial_gold = 0;
    int occupied = 0;
    double final_health = 0, final_shield = 0;
};
struct Decision {
    bool feasible = false, retention_crosses_threshold = false;
    Job job = Job::unavailable;
    int group = -1, self_index = -1, prism_index = -1, observation_round = -1;
    std::array<ActorPlan, actor_cap> actors{};
    AccountingSchedule schedule{};
    AccountingResult rank{};
    const char* first_action = "yield";
    const char* abort_condition = "no-complete-safe-project";
};
inline int self_index(const Observation& o) {
    int self = -1;
    if (o.actor_count < 1 || o.actor_count > actor_cap) return -1;
    for (int i = 0; i < o.actor_count; ++i) if (o.actors[i].resources.self) {
        if (self >= 0) return -1; self = i;
    }
    return self;
}
inline int turns(const RouteLeg& leg) { return leg.safety_checked ? leg.turns() : -1; }
inline const Group* find_group(const Observation& o, int id) {
    for (std::size_t i = 0; i < std::min(o.group_count, o.groups.size()); ++i)
        if (o.groups[i].id == id) return &o.groups[i];
    return nullptr;
}
inline bool safe(const CombatActor& a) {
    return std::isfinite(a.health) && std::isfinite(a.max_health) && a.health > 0 && a.max_health >= a.health &&
        a.health >= std::max(.20 * a.max_health, 2 * a.visible_enemy_next_phase);
}
inline bool expose(CombatActor& a, int ticks, double damage) {
    if (ticks < 0 || ticks > 100000 || !std::isfinite(damage) || damage < 0) return false;
    const double shield = a.shield_expiry_tick >= ticks ? a.shield : 0;
    const double absorbed = std::min(shield, damage);
    a.health -= damage - absorbed; a.shield = shield - absorbed;
    a.shield_expiry_tick = std::max(0, a.shield_expiry_tick - ticks);
    if (!a.shield_expiry_tick) a.shield = 0;
    return safe(a);
}
inline bool travel(CombatActor& a, int ticks) { return expose(a, ticks, a.visible_enemy_next_phase * ticks); }
inline bool valid_input(const Observation& o) {
    const int self = self_index(o);
    if (self < 0 || o.round < 0 || o.execution.owner_turn != o.round || !o.settings.valid() || !o.windows.valid ||
        o.prism_index < 0 || o.prism_index >= o.actor_count || o.group_count > 8 || o.purchase_count > 3 ||
        o.settings.capture_rounds > 100000 || o.team_score < 0 || o.focus.observation_round != o.round ||
        !std::isfinite(o.focus.permanent) || o.focus.permanent < 0 || !std::isfinite(o.focus.current) ||
        !std::isfinite(o.focus.per_gem) || o.focus.per_gem < 0 || !o.focus.base_is_permanent ||
        o.focus.valid_until_round < o.round) return false;
    for (double modifier : o.focus.modifiers) if (!std::isfinite(modifier) || modifier < 0) return false;
    for (int i = 0; i < o.actor_count; ++i) {
        const auto r = private_view(o.actors[i].resources, o.round); const auto& a = o.actors[i].stats;
        if (r.id < 0 || !r.visible || !r.own_team || !r.bag.current(o.round) || r.bag.value < 0 ||
            (r.self && (!r.gold.current(o.round) || r.gold.value < 0)) || a.id != r.id || a.self != r.self ||
            !a.visible || !a.enemy_cast_bound_known || !safe(a) || !std::isfinite(a.strength) || a.strength < 0 ||
            !std::isfinite(a.basic_modifier) || a.basic_modifier < 0 || !std::isfinite(a.shield) || a.shield < 0 ||
            a.shield_expiry_tick < 0 || !std::isfinite(a.visible_enemy_next_phase) || a.visible_enemy_next_phase < 0) return false;
        for (int j = 0; j < i; ++j) if (r.id == o.actors[j].resources.id) return false;
    }
    if (!o.actors[o.prism_index].stats.ranged ||
        std::abs(o.focus.current - (o.focus.permanent + o.focus.per_gem * o.actors[o.prism_index].resources.bag.value)) > .0001)
        return false;
    for (std::size_t i = 0; i < o.group_count; ++i) {
        if (o.groups[i].id < 0) return false;
        for (std::size_t j = 0; j < i; ++j) if (o.groups[i].id == o.groups[j].id) return false;
    }
    return true;
}
inline bool valid_purchase(const Purchase& p) {
    return p.family >= 0 && p.family < 3 && p.item >= 0 && p.shards == 1 && p.cost > 0 && p.legal_on_spawn &&
        std::isfinite(p.strength_gain) && p.strength_gain >= 0 && std::isfinite(p.health_gain) && p.health_gain >= 0 &&
        std::isfinite(p.focus_gain) && p.focus_gain >= 0 && std::isfinite(p.actual_spawn_purchase_heal) &&
        p.actual_spawn_purchase_heal >= 0 && p.actual_spawn_purchase_heal <= p.health_gain;
}
inline AccountingSchedule initial_schedule(const Observation& o) {
    AccountingSchedule s; s.actor_count = o.actor_count; s.doctrine_id = 2;
    s.starting_team_score = o.team_score; s.win_score = o.settings.winning_gems;
    s.tribute_gems_per_tick = o.settings.tribute_gems_per_tick; s.tribute_min_ticks = o.settings.tribute_min_ticks;
    for (int i = 0; i < o.actor_count; ++i) {
        const auto r = private_view(o.actors[i].resources, o.round);
        s.actors[i] = {r.id, r.self, true, r.bag.current(o.round), r.bag.value, r.self ? r.gold.value : 0,
            o.actors[i].tribute_income_eligible ? o.settings.marcella_gold_per_gem : 0, 0};
    }
    return s;
}
inline void event(AccountingSchedule& s, AccountingEventKind kind, int tick, int actor, int gems = 0,
                  int gold = 0, int group = -1, unsigned participants = 0, unsigned holders = 0, int waypoint = -1) {
    auto& e = s.events[s.event_count]; e.id = s.event_count++; e.kind = kind; e.tick = tick; e.actor = actor;
    e.gems = gems; e.gold = gold; e.group = group; e.participants = participants; e.holders = holders; e.waypoint = waypoint;
}
inline void step(ActorPlan& p, Step action) { p.steps[p.count++] = action; }
inline int selected_cast(const Observation& o, const Group& g, bool available) {
    if (!available || !o.actors[o.prism_index].resources.self) return -1;
    int selected = -1;
    for (int slot = 0; slot < 2; ++slot) {
        const auto& cast = g.prism_casts[slot];
        if (!o.execution.spells[slot].ready || !execution::spell_legal(o.execution, cast)) continue;
        bool actual_member = false;
        for (std::size_t i = 0; i < std::min(g.monster_count, g.monsters.size()); ++i)
            if (g.monsters[i].id == cast.target) actual_member = true;
        if (!actual_member || cast.dash || (selected >= 0 && o.focus.modifiers[slot] <= o.focus.modifiers[selected])) continue;
        selected = slot;
    }
    return selected;
}
inline bool combat_once(const Observation& o, const Group& g, std::array<CombatActor, actor_cap>& party,
                        int count, int caster, int bag, double permanent, int slot, int& clear) {
    if (g.id < 0 || !g.live_observed || !g.reward_or_alive || g.gems <= 0 || g.gold < 0 ||
        !g.monster_count || g.monster_count > 8) return false;
    for (std::size_t i = 0; i < g.monster_count; ++i)
        if (!std::isfinite(g.monsters[i].public_health) || !std::isfinite(g.monsters[i].observed_health) ||
            !std::isfinite(g.monsters[i].effective_strength)) return false;
    if (count < 1 || count > actor_cap || slot < -1 || slot > 1) return false;
    CombatInput c; int event_ticks = 0;
    for (std::size_t m = 0; m < g.monster_count; ++m) {
        const auto& v = g.monsters[m];
        if (v.id < 0 || v.public_health <= 0 || v.effective_strength < 0 ||
            (v.visible && (v.observed_health < 0 || v.observed_health > v.public_health))) return false;
        for (std::size_t n = 0; n < m; ++n) if (v.id == g.monsters[n].id) return false;
        if (!v.visible || v.observed_health > 0) c.monsters[c.monster_count++] = v;
    }
    for (int i = 0; i < count; ++i) {
        const auto& a = party[i];
        if (!safe(a) || a.id < 0 || !a.visible || !a.enemy_cast_bound_known || !std::isfinite(a.strength) || a.strength < 0 ||
            !std::isfinite(a.basic_modifier) || a.basic_modifier < 0 || !std::isfinite(a.shield) || a.shield < 0 ||
            a.shield_expiry_tick < 0 || !std::isfinite(a.visible_enemy_next_phase) || a.visible_enemy_next_phase < 0) return false;
        for (int n = 0; n < i; ++n) if (a.id == party[n].id) return false;
    }
    if (!c.monster_count) { clear = 0; return true; }
    if (slot >= 0) {
        if (caster < 0 || caster >= count) return false;
        const int target_id = g.prism_casts[slot].target;
        const double damage = (permanent + bag * o.focus.per_gem) * o.focus.modifiers[slot];
        if (!std::isfinite(damage) || damage < 0) return false;
        double incoming = 0; bool found = false;
        for (std::size_t m = 0; m < c.monster_count; ++m) { incoming += c.monsters[m].effective_strength; found |= c.monsters[m].id == target_id; }
        if (!found) return false;
        // One actual primary event, no concurrent basic and no damage/8 reuse.
        for (int i = 0; i < count; ++i) if (!expose(party[i], 1, incoming + party[i].visible_enemy_next_phase)) return false;
        event_ticks = 1;
        for (std::size_t m = 0; m < c.monster_count; ++m) if (c.monsters[m].id == target_id) {
            auto& target = c.monsters[m]; const double hp = target.visible ? target.observed_health : target.public_health;
            if (damage >= hp) {
                const double remaining = incoming - target.effective_strength;
                for (std::size_t n = m + 1; n < c.monster_count; ++n) c.monsters[n - 1] = c.monsters[n];
                --c.monster_count;
                if (c.monster_count) {
                    for (int i = 0; i < count; ++i) if (!expose(party[i], 2, 2 * (remaining + party[i].visible_enemy_next_phase))) return false;
                    event_ticks += 2;
                }
            } else { target.observed_health = hp - damage; target.visible = true; }
            break;
        }
        if (!c.monster_count) { clear = event_ticks; return true; }
    }
    for (int i = 0; i < count; ++i) {
        auto a = party[i]; a.heal_legal_ready = a.reserved_cast = a.spell_known_ready = a.legal_first_cast = false; a.direct_or_reset_damage = 0;
        c.actors[c.actor_count++] = a;
    }
    const auto result = forecast_combat(c);
    if (result.state != EstimateState::feasible) return false;
    clear = event_ticks + result.clear_ticks;
    for (int i = 0; i < count; ++i) if (!expose(party[i], result.clear_ticks,
        result.worst_camp_damage + party[i].visible_enemy_next_phase * result.clear_ticks)) return false;
    return true;
}
inline bool fight(const Observation& o, const Group& g, CombatActor& actor, int bag,
                  double permanent, int slot, int& clear) {
    std::array<CombatActor, actor_cap> party{}; party[0] = actor;
    if (!combat_once(o, g, party, 1, 0, bag, permanent, slot, clear) || !travel(party[0], o.settings.capture_rounds + 1)) return false;
    actor = party[0]; return true;
}
inline Decision initial_decision(const Observation& o, Job job) {
    Decision d; d.job = job; d.self_index = self_index(o); d.prism_index = o.prism_index;
    d.observation_round = o.round; d.schedule = initial_schedule(o);
    for (int i = 0; i < o.actor_count; ++i) {
        auto& p = d.actors[i]; p.actor = o.actors[i].resources.id;
        p.initial_bag = o.actors[i].resources.bag.value;
        p.initial_gold = o.actors[i].resources.self ? o.actors[i].resources.gold.value : 0;
    }
    return d;
}
inline bool produce(const Observation& o, const Group& g, const RouteLeg& access, int index,
                    CombatActor& actor, double permanent, int slot, int& bag, int& gold, int& tick, Decision& d) {
    const int distance = turns(access); int clear = 0;
    if (distance < 0 || access.destination != g.id || !travel(actor, distance) ||
        !fight(o, g, actor, index == o.prism_index ? bag : 0, permanent, slot, clear)) return false;
    Step s; s.kind = StepKind::produce; s.group = g.id; s.expected_gems = g.gems; s.expected_gold = g.gold;
    s.cast_slot = slot; s.cast_target = slot >= 0 ? g.prism_casts[slot].target : -1;
    s.expected_permanent_focus = permanent;
    s.expected_focus = index == o.prism_index ? permanent + bag * o.focus.per_gem : -1;
    step(d.actors[index], s);
    tick += distance + clear + o.settings.capture_rounds + 1;
    event(d.schedule, AccountingEventKind::capture, tick, index, g.gems, g.gold, g.id, 1u << index, 1u << index);
    bag += g.gems; gold += g.gold;
    return true;
}
inline bool bank(const Observation& o, const RouteLeg& route, int index, CombatActor& actor,
                 double permanent, int& bag, int& tick, Decision& d) {
    const int distance = turns(route), channel = o.settings.channel_ticks(bag);
    if (!o.bank_legal || distance < 0 || channel < 1 || !travel(actor, distance + channel)) return false;
    tick += distance;
    event(d.schedule, AccountingEventKind::tribute_start, tick, index, -1);
    tick += channel; event(d.schedule, AccountingEventKind::tribute_complete, tick, index, -1);
    Step s; s.kind = StepKind::bank; s.expected_gems = bag; s.waypoint = route.destination;
    s.expected_permanent_focus = permanent; s.expected_focus = index == o.prism_index ? permanent : -1;
    step(d.actors[index], s); bag = 0;
    return true;
}
inline bool renew(const Observation&, const RouteLeg& route, int index, CombatActor& actor,
                  int& tick, Decision& d) {
    const int distance = turns(route);
    if (distance < 0 || !travel(actor, distance)) return false;
    tick += distance; event(d.schedule, AccountingEventKind::return_to_work, tick, index, 0, 0, -1, 0, 0, route.destination);
    Step s; s.kind = StepKind::renew; s.waypoint = route.destination; step(d.actors[index], s);
    d.schedule.actors[index].occupied_turns = tick;
    d.actors[index].occupied = tick; d.actors[index].final_health = actor.health; d.actors[index].final_shield = actor.shield;
    return true;
}
inline bool purchase(const Observation& o, const Purchase* p, int index, CombatActor& actor,
                     double& permanent, int& gold, int& tick, Decision& d) {
    if (!p) return true;
    const int distance = turns(o.altar_to_spawn);
    if (!valid_purchase(*p) || gold < p->cost || distance < 0 || o.spawn_rest_ticks < 1 || o.spawn_rest_ticks > 32 ||
        !std::isfinite(o.queried_spawn_heal_per_tick) || o.queried_spawn_heal_per_tick < 0 ||
        !travel(actor, distance + o.spawn_rest_ticks)) return false;
    tick += distance + o.spawn_rest_ticks;
    actor.health = std::min(actor.max_health, actor.health + o.queried_spawn_heal_per_tick * o.spawn_rest_ticks);
    actor.strength += p->strength_gain; actor.max_health += p->health_gain; permanent += p->focus_gain;
    actor.health = std::min(actor.max_health, actor.health + p->actual_spawn_purchase_heal);
    if (o.queried_spawn_heal_per_tick > 0 || p->health_gain > 0 || p->actual_spawn_purchase_heal > 0)
        actor.basic_modifier = std::min(1.0, actor.basic_modifier);
    if (!safe(actor)) return false;
    event(d.schedule, AccountingEventKind::purchase, tick, index, 0, p->cost); gold -= p->cost;
    Step s; s.kind = StepKind::purchase; s.purchase_item = p->item; s.purchase_family = p->family;
    s.purchase_cost = p->cost; s.waypoint = o.altar_to_spawn.destination;
    s.expected_permanent_focus = permanent; s.expected_focus = index == o.prism_index ? permanent : -1;
    s.expected_health = actor.health; s.expected_strength = actor.strength;
    s.expected_max_health = actor.max_health; s.expected_basic_modifier = actor.basic_modifier;
    step(d.actors[index], s);
    return true;
}
struct HelperCache { Decision physical{}; bool valid = false; };
inline HelperCache helper_alternatives(const Observation& o) {
    HelperCache cache; cache.physical = initial_decision(o, Job::independent);
    for (int i = 0; i < o.actor_count; ++i) if (i != o.prism_index) {
        auto actor = o.actors[i].stats;
        int bag = o.actors[i].resources.bag.value;
        int gold = o.actors[i].resources.self ? o.actors[i].resources.gold.value : 0, tick = 0;
        const auto& g = o.actors[i].independent;
        if (!produce(o, g, g.from_current, i, actor, 0, -1, bag, gold, tick, cache.physical) ||
            !bank(o, g.to_altar, i, actor, 0, bag, tick, cache.physical) ||
            !renew(o, g.return_to_work, i, actor, tick, cache.physical)) return cache;
    }
    cache.valid = true; return cache;
}
inline void merge_helpers(const Observation& o, const HelperCache& cache, Decision& d) {
    for (int i = 0; i < o.actor_count; ++i) if (i != o.prism_index) {
        d.actors[i] = cache.physical.actors[i];
        d.schedule.actors[i].occupied_turns = cache.physical.schedule.actors[i].occupied_turns;
    }
    for (int i = 0; i < cache.physical.schedule.event_count; ++i) {
        auto e = cache.physical.schedule.events[i]; e.id = d.schedule.event_count;
        d.schedule.events[d.schedule.event_count++] = e;
    }
}
inline Decision project(const Observation& o, const Group& growth, bool retain,
                        const HelperCache& helpers, const Purchase* investment = nullptr) {
    Decision d;
    if (!valid_input(o) || !helpers.valid || !o.final_return_productive) return d;
    d = initial_decision(o, retain ? Job::retain_rebuild : Job::bank_rebuild); d.group = growth.id;
    auto fail = [&](const char* reason) { d.abort_condition = reason; return d; };
    const int index = o.prism_index;
    const auto* rebuild = find_group(o, o.rebuild_group);
    if (!rebuild || growth.id < 0 || (retain && growth.id == rebuild->id)) return fail("missing-or-repeated-rebuild-group");
    auto actor = o.actors[index].stats;
    int bag = o.actors[index].resources.bag.value;
    int gold = o.actors[index].resources.self ? o.actors[index].resources.gold.value : 0, tick = 0;
    double permanent = o.focus.permanent;
    if (bag <= 0 || (!retain && !o.safe_bank_reachable)) return fail("positive-inventory-and-safe-liquidation-required");
    if (retain) {
        const int slot = selected_cast(o, growth, true), distance = turns(growth.from_current);
        if (slot < 0 || distance < 0) return fail("no-current-ready-legal-capacity-cast");
        auto without_inventory = actor;
        int clear = 0;
        if (!travel(without_inventory, distance)) return fail("unsafe-retention-travel");
        // Same target, same kit, same ready cast and injury. Only temporary
        // inventory Focus is removed. A faster but still safe target is not
        // the required Endowment access-threshold witness.
        if (fight(o, growth, without_inventory, 0, permanent, slot, clear))
            return fail("inventory-does-not-cross-target-safety-threshold");
        if (!produce(o, growth, growth.from_current, index, actor, permanent, slot, bag, gold, tick, d) ||
            !bank(o, growth.to_altar, index, actor, permanent, bag, tick, d))
            return fail("unsafe-retained-growth-or-liquidation");
        d.retention_crosses_threshold = true;
    } else if (!bank(o, o.current_to_altar, index, actor, permanent, bag, tick, d))
        return fail("unsafe-immediate-bank");
    if (!purchase(o, investment, index, actor, permanent, gold, tick, d)) return fail("unfunded-or-illegal-spawn-purchase");
    const auto& next = retain ? *rebuild : growth;
    const RouteLeg& access = investment ? next.from_spawn : next.from_altar;
    // Bank-first has not spent its known ready slot. Retention's rebuilding
    // follows a consumed growth cast and receives no second readiness credit.
    const int next_slot = retain ? -1 : selected_cast(o, next, true);
    if (!produce(o, next, access, index, actor, permanent, next_slot, bag, gold, tick, d) ||
        !bank(o, next.to_altar, index, actor, permanent, bag, tick, d) ||
        !renew(o, o.final_return, index, actor, tick, d)) return fail("unsafe-postbank-rebuild-or-return");
    merge_helpers(o, helpers, d);
    if (tick > o.focus.valid_until_round - o.round) return fail("focus-stat-certificate-expires-before-renewal");
    d.rank = forecast_schedule(d.schedule, o.windows); d.feasible = d.rank.status == AccountingStatus::feasible;
    d.first_action = retain ? "clear-named-growth-with-retained-focus" : "bank-whole-bag-before-rebuilding";
    d.abort_condition = d.feasible ? "danger-inventory-focus-cast-payout-or-route-premise-failed" : d.rank.reason;
    return d;
}
inline bool better(const Decision& a, const Decision& b) {
    return a.feasible && (!b.feasible || (same_actors(a.rank, b.rank) && a.rank.windows == b.rank.windows &&
                                         compare_results(a.rank, b.rank) > 0));
}
inline Decision bootstrap(const Observation& o, const HelperCache& helpers) {
    Decision d;
    if (!valid_input(o) || !helpers.valid || !o.final_return_productive) return d;
    const auto* group = find_group(o, o.bootstrap_group);
    if (!group || o.actors[o.prism_index].resources.bag.value != 0) return d;
    d = initial_decision(o, Job::bootstrap); d.group = group->id;
    const int index = o.prism_index;
    auto actor = o.actors[index].stats;
    int bag = 0, gold = d.actors[index].initial_gold, tick = 0;
    if (produce(o, *group, group->from_current, index, actor, o.focus.permanent,
                selected_cast(o, *group, true), bag, gold, tick, d) &&
        bank(o, group->to_altar, index, actor, o.focus.permanent, bag, tick, d) &&
        renew(o, o.final_return, index, actor, tick, d)) {
        merge_helpers(o, helpers, d);
        if (tick > o.focus.valid_until_round - o.round) return {};
        d.rank = forecast_schedule(d.schedule, o.windows); d.feasible = d.rank.status == AccountingStatus::feasible;
        d.first_action = "bootstrap-safe-source-reobserve-actual-payout";
        d.abort_condition = "danger-or-payout-premise-failed";
        return d;
    }
    if (o.actor_count < 2 || !o.bootstrap_partner_visible_usable) return {};
    d = initial_decision(o, Job::bootstrap); d.group = group->id;
    std::array<CombatActor, actor_cap> party{};
    std::array<int, actor_cap> arrival{};
    int start = 0;
    for (int i = 0; i < o.actor_count; ++i) {
        if (o.actors[i].resources.bag.value != 0) return {};
        const auto& leg = i == index ? group->from_current : o.actors[i].to_bootstrap;
        arrival[i] = turns(leg); party[i] = o.actors[i].stats;
        if (arrival[i] < 0 || leg.destination != group->id || !travel(party[i], arrival[i])) return {};
        start = std::max(start, arrival[i]);
    }
    const int hold_ticks = o.settings.capture_rounds + 1;
    const int slot = selected_cast(o, *group, true);
    for (int i = 0; i < o.actor_count; ++i) {
        if (start - arrival[i] > 8 || !travel(party[i], start - arrival[i])) return {};
    }
    int clear_ticks = 0;
    if (!combat_once(o, *group, party, o.actor_count, index, o.actors[index].resources.bag.value,
        o.focus.permanent, slot, clear_ticks)) return {};
    const unsigned all = (1u << o.actor_count) - 1u;
    const unsigned holders = o.concentrate_bootstrap ? 1u << index : all;
    const int holder_count = o.concentrate_bootstrap ? 1 : o.actor_count;
    int lowest = index;
    for (int i = 0; i < o.actor_count; ++i) if ((holders & (1u << i)) &&
        o.actors[i].resources.id < o.actors[lowest].resources.id) lowest = i;
    const int payout_tick = start + clear_ticks + hold_ticks;
    event(d.schedule, AccountingEventKind::capture, payout_tick, index, group->gems, group->gold, group->id, all, holders);
    for (int i = 0; i < o.actor_count; ++i) {
        const bool holds = holders & (1u << i);
        if (holds && !travel(party[i], hold_ticks)) return {};
        int own_bag = holds ? group->gems / holder_count + (i == lowest ? group->gems % holder_count : 0) : 0;
        int own_gold = d.actors[i].initial_gold + (holds ? group->gold / holder_count + (i == lowest ? group->gold % holder_count : 0) : 0);
        Step first; first.kind = i == index ? StepKind::produce : StepKind::support;
        first.group = group->id; first.expected_gems = own_bag; first.expected_gold = own_gold - d.actors[i].initial_gold;
        first.cast_slot = i == index ? slot : -1;
        first.cast_target = i == index && slot >= 0 ? group->prism_casts[slot].target : -1;
        first.coalition = true;
        first.withdraw_when_safe = i != index && o.concentrate_bootstrap;
        first.expected_focus = i == index ? o.focus.current : -1; first.expected_permanent_focus = o.focus.permanent;
        step(d.actors[i], first);
        int own_tick = start + clear_ticks + (holds ? hold_ticks : 0);
        if (i == index) {
            if (!bank(o, group->to_altar, i, party[i], o.focus.permanent, own_bag, own_tick, d) ||
                !renew(o, o.final_return, i, party[i], own_tick, d)) return {};
        } else {
            const auto& independent = o.actors[i].independent;
            if (!produce(o, independent, o.actors[i].after_bootstrap, i, party[i], 0, -1,
                         own_bag, own_gold, own_tick, d) ||
                !bank(o, independent.to_altar, i, party[i], 0, own_bag, own_tick, d) ||
                !renew(o, independent.return_to_work, i, party[i], own_tick, d)) return {};
        }
    }
    d.rank = forecast_schedule(d.schedule, o.windows); d.feasible = d.rank.status == AccountingStatus::feasible;
    if (d.actors[index].occupied > o.focus.valid_until_round - o.round) return {};
    d.first_action = "visible-common-clearance-safe-holder-before-withdrawal";
    d.abort_condition = "partner-holder-safety-payout-or-independent-work-premise-failed";
    return d;
}
inline Decision helper_fallback(const Observation& o, int own) {
    Decision d;
    if (own < 0 || own >= o.actor_count || own == o.prism_index || !o.settings.valid() || !o.windows.valid) return d;
    const auto r = private_view(o.actors[own].resources, o.round); auto actor = o.actors[own].stats;
    if (!r.self || !r.visible || !r.own_team || !r.bag.current(o.round) || !r.gold.current(o.round) ||
        r.bag.value < 0 || r.gold.value < 0 || !safe(actor) || actor.id != r.id) return d;
    auto temporary = initial_decision(o, Job::independent);
    int bag = r.bag.value, gold = r.gold.value, tick = 0;
    const auto& group = o.actors[own].independent;
    if (!produce(o, group, group.from_current, own, actor, 0, -1, bag, gold, tick, temporary) ||
        !bank(o, group.to_altar, own, actor, 0, bag, tick, temporary) ||
        !renew(o, group.return_to_work, own, actor, tick, temporary)) return d;
    d.job = Job::independent; d.self_index = 0; d.prism_index = -1; d.observation_round = o.round;
    d.actors[0] = temporary.actors[own]; d.schedule = temporary.schedule; d.schedule.actor_count = 1;
    d.schedule.actors[0] = temporary.schedule.actors[own]; d.schedule.event_count = 0;
    for (int i = 0; i < temporary.schedule.event_count; ++i) {
        auto e = temporary.schedule.events[i]; e.id = d.schedule.event_count; e.actor = 0;
        if (e.kind == AccountingEventKind::capture) e.participants = e.holders = 1;
        d.schedule.events[d.schedule.event_count++] = e;
    }
    d.rank = forecast_schedule(d.schedule, o.windows); d.feasible = d.rank.status == AccountingStatus::feasible;
    d.first_action = "resume-own-independent-circuit";
    d.abort_condition = "own-source-route-or-safety-premise-failed";
    return d;
}
inline Decision choose_job(const Observation& o, Profile profile = {}) {
    Decision best;
    if (o.execution.channel != execution::Channel::none && o.execution.channel_valid && o.execution.channel_safe) {
        best.job = Job::preserve; best.first_action = "preserve-valid-channel"; return best;
    }
    if (o.safe_active_capture && o.execution.safe && o.execution.on_group_spawn) {
        best.job = Job::preserve; best.first_action = "preserve-safe-capture"; return best;
    }
    if (o.needs_recovery || !o.execution.safe || o.execution.needs_recovery) {
        best.job = Job::recover; best.first_action = "flee-recall-and-observe-recovery"; return best;
    }
    const int own = self_index(o);
    if (own >= 0 && o.settings.valid() && o.execution.owner_turn == o.round &&
        o.execution.legal_winning_bank && o.execution.adjacent_altar && o.execution.altar_id >= 0 &&
        o.execution.fuel >= o.execution.action_reserve) {
        const auto r = private_view(o.actors[own].resources, o.round);
        const int ticks = o.settings.channel_ticks(r.bag.value);
        if (r.self && r.visible && r.own_team && r.bag.current(o.round) && r.gold.current(o.round) &&
            r.bag.value > 0 && r.gold.value >= 0 && r.bag.value >= o.settings.winning_gems - o.team_score &&
            ticks <= o.execution.remaining_rounds) {
            best.job = Job::winning_bank; best.self_index = 0; best.prism_index = -1; best.observation_round = o.round;
            best.schedule.actor_count = 1; best.schedule.doctrine_id = 2; best.schedule.starting_team_score = o.team_score;
            best.schedule.win_score = o.settings.winning_gems; best.schedule.tribute_gems_per_tick = o.settings.tribute_gems_per_tick;
            best.schedule.tribute_min_ticks = o.settings.tribute_min_ticks;
            best.schedule.actors[0] = {r.id, true, true, true, r.bag.value, r.gold.value,
                o.actors[own].tribute_income_eligible ? o.settings.marcella_gold_per_gem : 0, ticks};
            best.schedule.terminal_win = best.schedule.immediate_legal_winning_bank = true;
            best.schedule.terminal_win_tick = ticks;
            event(best.schedule, AccountingEventKind::tribute_start, 0, 0, -1);
            event(best.schedule, AccountingEventKind::tribute_complete, ticks, 0, -1);
            auto& p = best.actors[0]; p.actor = r.id; p.initial_bag = r.bag.value; p.initial_gold = r.gold.value;
            Step s; s.kind = StepKind::bank; s.expected_gems = r.bag.value; s.waypoint = o.execution.altar_id; step(p, s);
            best.rank = forecast_schedule(best.schedule, o.windows); best.feasible = best.rank.status == AccountingStatus::feasible;
            best.first_action = "start-legal-whole-bag-winning-bank";
            if (best.feasible) return best;
        }
    }
    if (!valid_input(o)) return own >= 0 && own != o.prism_index ? helper_fallback(o, own) : best;
    if (o.execution.fuel < o.execution.selection_reserve) return best;
    const auto helpers = helper_alternatives(o);
    if (!helpers.valid) { best.abort_condition = "helper-independent-alternative-unavailable"; return best; }
    if (o.actors[o.prism_index].resources.bag.value == 0) {
        auto solo = o; solo.bootstrap_partner_visible_usable = false;
        for (std::size_t g = 0; g < o.group_count; ++g) {
            solo.bootstrap_group = o.groups[g].id;
            const auto candidate = bootstrap(solo, helpers);
            if (candidate.feasible && (!best.feasible || candidate.actors[o.prism_index].occupied < best.actors[o.prism_index].occupied ||
                (candidate.actors[o.prism_index].occupied == best.actors[o.prism_index].occupied && candidate.group < best.group))) best = candidate;
        }
        if (!best.feasible) best = bootstrap(o, helpers);
        if (!best.feasible && own != o.prism_index) return helper_fallback(o, own);
        return best;
    }
    for (std::size_t g = 0; g < o.group_count; ++g) {
        const auto banked = project(o, o.groups[g], false, helpers);
        if (better(banked, best)) best = banked;
        const bool retention_allowed = profile.arm == Arm::active || !o.safe_bank_reachable;
        const auto retained = retention_allowed ? project(o, o.groups[g], true, helpers) : Decision{};
        if (better(retained, best)) best = retained;
        for (std::size_t p = 0; p < o.purchase_count; ++p) {
            const auto bought = project(o, o.groups[g], false, helpers, &o.purchases[p]);
            if (better(bought, banked) && better(bought, best)) best = bought;
            if (retention_allowed) {
                const auto funded = project(o, o.groups[g], true, helpers, &o.purchases[p]);
                if (better(funded, retained) && better(funded, best)) best = funded;
            }
        }
    }
    return !best.feasible && own != o.prism_index ? helper_fallback(o, own) : best;
}

enum class Stage { running, reobserve, complete, aborted };
struct ProjectFacts {
    int observation_round = -1, own_actor = -1, own_bag = -1, own_gold = -1;
    double own_focus = -1, own_permanent_focus = -1;
    CombatActor own_stats{}; // fresh observed executing actor, mandatory on purchase confirmation
    std::uint64_t capture_receipt = 0, purchase_receipt = 0, bank_receipt = 0;
    bool purchase_confirmed = false;
    int purchased_item = -1, purchased_family = -1, purchased_cost = -1, purchased_shards = 0;
    bool at_named_productive_waypoint = false;
    int productive_waypoint = -1;
    int holder_actor = -1, holder_group = -1, holder_observation_round = -1;
    bool holder_on_spawn = false;
};
struct Project {
    Decision selected{};
    execution::State executor{};
    Stage stage = Stage::aborted;
    int step_index = 0, bag_baseline = 0, gold_baseline = 0, intent_turn = -1, before_buy_gold = -1;
    int shop_arrival = -1, bank_ticks = 0;
    std::uint64_t capture_baseline = 0, purchase_baseline = 0, bank_baseline = 0, intent = 0;
    execution::ActionKind intent_kind = execution::ActionKind::yield;
    bool initialized = false, cast_done = false;
    std::uint64_t pending_cast = 0;
    int pending_cast_target = -1, pending_cast_turn = -1;
};
inline Project start_project(const Decision& selected, const ProjectFacts& facts) {
    Project p; p.selected = selected;
    if (!selected.feasible || selected.self_index < 0 || selected.self_index >= selected.schedule.actor_count ||
        selected.self_index >= actor_cap) return p;
    const auto& a = selected.actors[selected.self_index];
    if (facts.observation_round != selected.observation_round || facts.own_actor != a.actor ||
        facts.own_bag != a.initial_bag || facts.own_gold != a.initial_gold || a.count < 1 || a.count > 8) return p;
    p.stage = Stage::running; p.bag_baseline = facts.own_bag; p.gold_baseline = facts.own_gold;
    p.capture_baseline = facts.capture_receipt; p.purchase_baseline = facts.purchase_receipt; p.bank_baseline = facts.bank_receipt;
    return p;
}
inline execution::Decision execute_job(execution::Observation o, const ProjectFacts& f, Project& p) {
    namespace e = execution;
    auto stop = [&](Stage stage, e::Reason why) { p.stage = stage; return e::emit(p.executor, e::ActionKind::refresh, why); };
    if (p.stage != Stage::running || p.selected.self_index < 0 || p.selected.self_index >= p.selected.schedule.actor_count ||
        p.selected.self_index >= actor_cap) return e::emit(p.executor, e::ActionKind::yield, e::Reason::illegal);
    const auto& plan = p.selected.actors[p.selected.self_index];
    const bool prism = p.selected.self_index == p.selected.prism_index;
    if (f.observation_round != o.owner_turn || f.own_actor != plan.actor || f.own_bag < 0 || f.own_gold < 0 ||
        p.step_index < 0 || p.step_index >= plan.count || plan.count < 1 || plan.count > 8 ||
        (prism && (!std::isfinite(f.own_focus) || !std::isfinite(f.own_permanent_focus) ||
                   f.own_focus < 0 || f.own_permanent_focus < 0))) return stop(Stage::aborted, e::Reason::illegal);
    e::consume_feedback(o, p.executor);
    auto enter_step = [&]() {
        const auto& s = plan.steps[p.step_index];
        const auto phase = s.kind == StepKind::bank ? e::Phase::bank : s.kind == StepKind::purchase ? e::Phase::shop :
            s.kind == StepKind::renew ? e::Phase::renew : e::Phase::bootstrap;
        e::enter(p.executor, phase, o); p.executor.target = s.group;
        p.intent = 0; p.pending_cast = 0; p.cast_done = false; p.shop_arrival = -1;
        p.bag_baseline = f.own_bag; p.gold_baseline = f.own_gold;
        p.capture_baseline = f.capture_receipt; p.purchase_baseline = f.purchase_receipt; p.bank_baseline = f.bank_receipt;
    };
    if (!p.initialized) {
        if (f.own_bag != plan.initial_bag || f.own_gold != plan.initial_gold)
            return stop(Stage::reobserve, e::Reason::illegal);
        p.initialized = true; enter_step();
    }
    const auto previous = plan.steps[p.step_index];
    if ((previous.kind == StepKind::produce || previous.kind == StepKind::support) && o.own_payout_observed) {
        if ((p.executor.phase != e::Phase::clear && p.executor.phase != e::Phase::hold) ||
            o.own_payout_group != previous.group || o.payout_receipt <= p.capture_baseline ||
            f.capture_receipt != o.payout_receipt || o.own_gems_received + o.own_gold_received <= 0 ||
            f.own_bag != static_cast<AccountingStock>(p.bag_baseline) + o.own_gems_received ||
            f.own_gold != static_cast<AccountingStock>(p.gold_baseline) + o.own_gold_received)
            return stop(Stage::aborted, e::Reason::illegal);
        p.capture_baseline = o.payout_receipt; p.executor.consumed_payout = o.payout_receipt;
        if (p.selected.job == Job::bootstrap && prism) return stop(Stage::reobserve, e::Reason::payout);
        if (o.own_gems_received != previous.expected_gems || o.own_gold_received != previous.expected_gold)
            return stop(Stage::reobserve, e::Reason::payout);
        o.own_payout_observed = false;
        ++p.step_index; if (p.step_index >= plan.count) return stop(Stage::aborted, e::Reason::illegal); enter_step();
    }
    if (o.channel != e::Channel::none) return e::decision(o, p.executor);
    if (!o.alive || !o.safe || o.needs_recovery || o.remaining_rounds <= 0) return stop(Stage::aborted, e::Reason::unsafe);
    if (p.pending_cast) {
        if (o.feedback_sequence != p.pending_cast || o.feedback_kind != e::ActionKind::cast) {
            if (o.owner_turn > p.pending_cast_turn) return stop(Stage::reobserve, e::Reason::illegal);
            return e::emit(p.executor, e::ActionKind::refresh);
        }
        if (!o.feedback_success || o.feedback_target != p.pending_cast_target) return stop(Stage::reobserve, e::Reason::illegal);
        p.pending_cast = 0; p.cast_done = true;
    }
    if (previous.kind == StepKind::support && previous.withdraw_when_safe && p.intent &&
        p.intent_kind == e::ActionKind::withdraw && o.feedback_sequence == p.intent &&
        o.feedback_kind == e::ActionKind::withdraw && o.feedback_success) {
        ++p.step_index; if (p.step_index >= plan.count) return stop(Stage::aborted, e::Reason::illegal); enter_step();
    }
    auto current = plan.steps[p.step_index];
    if (current.cast_slot < -1 || current.cast_slot > 1 ||
        (current.cast_slot >= 0 && current.cast_target < 0)) return stop(Stage::aborted, e::Reason::illegal);
    if (current.kind == StepKind::bank && p.intent && p.intent_kind == e::ActionKind::start_tribute &&
        o.owner_turn >= p.intent_turn + std::max(1, p.bank_ticks) && f.bank_receipt > p.bank_baseline && f.own_bag == 0) {
        if (prism && (std::abs(f.own_focus - current.expected_permanent_focus) > .0001 ||
            std::abs(f.own_permanent_focus - current.expected_permanent_focus) > .0001))
            return stop(Stage::reobserve, e::Reason::illegal);
        ++p.step_index;
        if (p.step_index >= plan.count) { p.stage = Stage::complete; return e::emit(p.executor, e::ActionKind::yield); }
        enter_step(); current = plan.steps[p.step_index];
    }
    if (current.kind == StepKind::purchase) {
        if (o.owner_turn - p.executor.phase_start >= p.executor.phase_budget) return stop(Stage::aborted, e::Reason::timeout);
        const bool bought = p.intent && p.intent_kind == e::ActionKind::buy && f.purchase_confirmed &&
            o.owner_turn >= p.intent_turn && f.purchase_receipt > p.purchase_baseline && f.purchased_item == current.purchase_item &&
            f.purchased_family == current.purchase_family && f.purchased_cost == current.purchase_cost &&
            f.purchased_shards == 1 && f.own_gold == p.before_buy_gold - current.purchase_cost;
        if (bought) {
            if (!o.at_spawn || !o.spawn_recovery_observed || f.own_stats.id != f.own_actor || !f.own_stats.self ||
                !f.own_stats.visible || !safe(f.own_stats) || !std::isfinite(f.own_stats.basic_modifier) ||
                !std::isfinite(f.own_stats.strength) || f.own_stats.basic_modifier < 0 || f.own_stats.strength < 0 ||
                f.own_stats.health < current.expected_health || f.own_stats.strength < current.expected_strength ||
                f.own_stats.max_health < current.expected_max_health || f.own_stats.basic_modifier < current.expected_basic_modifier || (prism &&
                (std::abs(f.own_permanent_focus - current.expected_permanent_focus) > .0001 ||
                 std::abs(f.own_focus - current.expected_permanent_focus) > .0001))) return stop(Stage::reobserve, e::Reason::illegal);
            ++p.step_index; enter_step(); current = plan.steps[p.step_index];
        } else {
            if (f.own_gold < current.purchase_cost || (p.intent &&
                (f.purchase_receipt > p.purchase_baseline || f.own_gold != p.before_buy_gold))) return stop(Stage::aborted, e::Reason::illegal);
            if (!p.intent) p.purchase_baseline = f.purchase_receipt;
            if (o.at_spawn) {
                if (p.shop_arrival < 0) p.shop_arrival = o.owner_turn;
                if (o.owner_turn <= p.shop_arrival || !o.spawn_recovery_observed)
                    return e::emit(p.executor, e::ActionKind::yield, e::Reason::arrival_recovery);
            }
        }
    }
    if (current.kind == StepKind::renew) {
        if (f.own_bag != 0 || o.owner_turn - p.executor.phase_start >= p.executor.phase_budget)
            return stop(Stage::aborted, e::Reason::timeout);
        if (f.at_named_productive_waypoint && f.productive_waypoint == current.waypoint) {
            p.stage = Stage::complete; return e::emit(p.executor, e::ActionKind::yield);
        }
        return e::move_intent(o, p.executor);
    }
    if (current.kind == StepKind::bank && f.own_bag == 0) return stop(Stage::reobserve, e::Reason::illegal);
    if (current.kind == StepKind::bank && !p.intent) p.bank_baseline = f.bank_receipt;
    if ((current.kind == StepKind::produce || current.kind == StepKind::support) && prism &&
        (std::abs(f.own_permanent_focus - current.expected_permanent_focus) > .0001 ||
         std::abs(f.own_focus - current.expected_focus) > .0001 || f.own_bag != p.bag_baseline))
        return stop(Stage::reobserve, e::Reason::illegal);
    if (current.coalition && (p.executor.phase == e::Phase::clear || p.executor.phase == e::Phase::hold) &&
        (!o.coalition_visible || !o.coalition_safety_fresh || !o.coalition_safe)) return stop(Stage::aborted, e::Reason::unsafe);
    if (current.kind == StepKind::produce || current.kind == StepKind::support) {
        for (std::size_t i = 0; i < std::min(o.group_count, o.groups.size()); ++i) {
            o.groups[i].assigned = o.groups[i].id == current.group;
            if (o.groups[i].assigned) o.groups[i].coalition_required = current.coalition;
        }
        o.helper = current.kind == StepKind::support && current.withdraw_when_safe;
        if (o.helper) o.holder_visible_on_spawn = o.holder_visible_on_spawn && f.holder_on_spawn &&
            f.holder_observation_round == o.owner_turn && f.holder_group == current.group &&
            p.selected.prism_index >= 0 && p.selected.prism_index < p.selected.schedule.actor_count &&
            f.holder_actor == p.selected.actors[p.selected.prism_index].actor;
        for (int slot = 0; slot < 2; ++slot)
            if (slot != current.cast_slot || p.cast_done) o.spells[slot].useful = false;
        if (current.cast_slot >= 0 && !p.cast_done && (p.executor.phase == e::Phase::clear ||
            (o.at_target && o.group_id == current.group && o.group_live_known && !o.group_cleared)) &&
            (o.spells[current.cast_slot].target != current.cast_target ||
             !e::spell_legal(o, o.spells[current.cast_slot]))) return stop(Stage::reobserve, e::Reason::illegal);
    }
    o.bank_requested = current.kind == StepKind::bank; o.own_bag = f.own_bag;
    if (current.kind == StepKind::bank) {
        const int rate = p.selected.schedule.tribute_gems_per_tick;
        if (rate <= 0) return stop(Stage::aborted, e::Reason::illegal);
        o.tribute_ticks_quote = std::max(p.selected.schedule.tribute_min_ticks,
            f.own_bag / rate + (f.own_bag % rate != 0));
    }
    o.purchase_requested = current.kind == StepKind::purchase; o.purchase_id = current.purchase_item;
    o.field_equipment_allowed = false; o.purchase_is_equipment = true;
    o.purchase_affordable = f.own_gold >= current.purchase_cost;
    if (current.kind == StepKind::bank) o.adjacent_altar = o.adjacent_altar && o.altar_id == current.waypoint;
    const auto result = e::decision(o, p.executor);
    if (result.action.kind == e::ActionKind::cast && result.action.target != current.cast_target)
        return stop(Stage::reobserve, e::Reason::illegal);
    if (result.action.kind == e::ActionKind::start_tribute || result.action.kind == e::ActionKind::buy ||
        result.action.kind == e::ActionKind::withdraw) {
        p.intent = result.action.sequence; p.intent_kind = result.action.kind; p.intent_turn = o.owner_turn;
        p.before_buy_gold = f.own_gold;
        if (result.action.kind == e::ActionKind::start_tribute) p.bank_ticks = result.action.channel_ticks;
    }
    if (result.action.kind == e::ActionKind::cast) {
        p.pending_cast = result.action.sequence; p.pending_cast_target = result.action.target;
        p.pending_cast_turn = o.owner_turn;
    }
    if (result.reason == e::Reason::timeout || result.reason == e::Reason::assembly_timeout ||
        result.reason == e::Reason::capture_unresolved || result.reason == e::Reason::no_opportunity) p.stage = Stage::aborted;
    return result;
}
} // namespace p5::endowment
