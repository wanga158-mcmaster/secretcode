#pragma once
#include <cambc.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>
#include "profile.h"
#include "policy.h"
#include "mission-policy.h"
#include "telemetry.h"

namespace {
using cambc::vec2;
using cambc::team;
using cambc::champion;
using cambc::camp;
using cambc::bitboard;
using cambc::item_type;
using Field = cambc::distance_field;
constexpr int unreachable = Field::unreachable;
constexpr float epsilon = 0.0001f;
int distance(vec2 a, vec2 b) { return static_cast<int>(a.chebyshev_distance(b)); }
int tile_index(vec2 p) { return p.y * cambc::map::width() + p.x; }
vec2 tile_position(int n) { return {n % static_cast<int>(cambc::map::width()), n / static_cast<int>(cambc::map::width())}; }
bitboard around(vec2 p, int radius) { bitboard result; result.draw_chebyshev(p, radius); return result; }
int min_distance(const Field& field, const bitboard& goals) {
    int best = unreachable;
    for (const auto p : goals.positions()) best = std::min(best, static_cast<int>(field.get(p)));
    return best;
}
int channel_duration(unsigned gems) {
    return std::max(cambc::constants::submit_gem_minimum_duration,
        cambc::constants::submit_gem_base_duration + static_cast<int>(std::ceil(gems * cambc::constants::submit_gem_duration_multiplier)));
}
std::string integer(const char* name, long long value) { return "\"" + std::string(name) + "\":" + std::to_string(value); }
std::string number(const char* name, float value) { return "\"" + std::string(name) + "\":" + std::to_string(std::isfinite(value) ? value : 0.0f); }
std::string group_field(int id) { return "\"group\":\"g_" + std::to_string(id) + "\""; }
bitboard public_terrain() { return cambc::map::walls() | cambc::map::holes() | cambc::map::altars() | cambc::map::spawn_region(team::enemy); }

struct Group {
    cambc::map::camp_group group;
    bitboard spawns;
    vec2 anchor;
    int id = 0, canonical_non_monolith = -1;
    float health = 0, incoming = 0;
    bool monolith = false;
    unsigned unavailable_until = 0;
};
std::vector<Group> public_groups() {
    std::vector<Group> result;
    for (auto group : cambc::map::camp_groups()) {
        Group g{group, group.spawn_tiles(), group.camps().front().spawn_position()};
        g.id = std::numeric_limits<int>::max();
        for (const auto& monster : group) {
            const auto p = monster.spawn_position();
            if (tile_index(p) < g.id) { g.id = tile_index(p); g.anchor = p; }
            const auto stats = monster.base_stats();
            g.health += stats.base_health; g.incoming += stats.base_strength;
            g.monolith |= monster.is_monolith();
        }
        result.push_back(std::move(g));
    }
    std::sort(result.begin(), result.end(), [](const Group& a, const Group& b) { return a.id < b.id; });
    int next = 0;
    for (auto& g : result) if (!g.monolith) g.canonical_non_monolith = next++;
    return result;
}

#include "draft-plan.h"

struct Choice { int index; float score, health, incoming; bool reward, affordable; };

class Bot {
    std::vector<Group> groups_;
    std::vector<vec2> altars_;
    bitboard terrain_;
    bool initialized_ = false, recovering_ = false, partial_clear_ = false, has_non_monolith_gems_ = false;
    int target_ = -1;
    unsigned monster_shards_ = 0, jungle_shards_ = 0, identity_ = 0;
    bool monster_passive_ = false, jungle_passive_ = false;
    unsigned turn_ = 0, fields_ = 0;
    Telemetry telemetry_;
    std::optional<Field> origin_field_;
    vec2 origin_field_position_{};
    unsigned previous_bag_ = 0, previous_score_ = 0;
    bool previous_alive_ = true, previous_submit_ = false;

    bool field(Field& output, const bitboard& roots, const bitboard& walls, bool complete = true) {
        if (fields_ >= 2 || cambc::fuel_remaining() < 30000 || roots.is_empty()) return false;
        ++fields_;
        bitboard visited;
        // A native expansion is a distance layer, bounded to 256 by the contracts.
        const auto expansions = complete ? output.compute_with_expansion_count(walls, roots & ~walls, visited, 256)
            : output.compute_to_with_expansion_count(walls, roots & ~walls, around(cambc::self().position(), 0), visited, 256);
        return expansions < 256;
    }
    bool ensure_origin() {
        const auto p = cambc::self().position();
        if (origin_field_ && origin_field_position_ == p) return true;
        Field value;
        if (!field(value, around(p, 0), terrain_)) return false;
        origin_field_ = std::move(value); origin_field_position_ = p;
        return true;
    }
    bool seen(const Group& g) const { return !(g.spawns & cambc::vision::visible_tiles()).is_empty(); }
    bool reward(const Group& g) const { return seen(g) && std::holds_alternative<cambc::map::camp_group_reward_available>(g.group.state()); }
    int find_group(int id) const { for (std::size_t i = 0; i < groups_.size(); ++i) if (groups_[i].id == id) return static_cast<int>(i); return -1; }
    bool is_actor(cambc::champion_type name) const { return cambc::self().champion_name() == name; }
    int nearest_altar() const {
        int result = unreachable;
        for (const auto p : altars_) result = std::min(result, distance(cambc::self().position(), p));
        return result;
    }
    bool winning_bag() const {
        const auto score = cambc::global_submitted_gems(team::ally);
        return score < cambc::constants::winning_gems && cambc::self().gem_count() >= cambc::constants::winning_gems - score;
    }
    bool base_bank_request(int nearest) const {
        const auto late = cambc::constants::max_rounds > 100 ? cambc::constants::max_rounds - 100 : 0;
        return cambc::self().gem_count() && (cambc::self().gem_count() >= 24 || nearest <= 4 || cambc::round_number() >= late || winning_bag());
    }
    bitboard danger(const std::vector<camp>& camps, const std::vector<champion>& enemies, bool enemy_step = true) const {
        bitboard result;
        for (const auto& m : camps) if (m.is_visible() && m.is_alive() && m.is_awake() && m.strength() > 0)
            result |= around(m.position(), m.attack_range());
        for (const auto& e : enemies) result |= around(e.position(), e.attack_range() + (enemy_step ? 1 : 0));
        return result;
    }
    bool enemy_near(vec2 p, const std::vector<champion>& enemies, int radius = 8) const {
        for (const auto& e : enemies) if (distance(p, e.position()) <= radius) return true;
        return false;
    }
    void initialize() {
        terrain_ = public_terrain(); altars_ = cambc::map::altars().positions();
        std::sort(altars_.begin(), altars_.end(), [](vec2 a, vec2 b) { return tile_index(a) < tile_index(b); });
        groups_ = public_groups();
        for (const auto& g : groups_) has_non_monolith_gems_ |= !g.monolith && g.group.gem_value() > 0;
        identity_ = static_cast<unsigned>(cambc::self().champion_name());
        initialized_ = true;
    }
    void shop() {
        auto& me = cambc::self();
        core_shop();
        for (int stage = 0; stage < 3; ++stage) {
            const auto build = fundamentals::build_stage(monster_shards_, jungle_shards_);
            auto item = item_type::power_shard_of_strength;
            unsigned limit = 100;
            if (build == fundamentals::BuildStage::monster_strength) { item = item_type::monster_shard_of_strength; limit = 5 - monster_shards_; }
            else if (build == fundamentals::BuildStage::jungle_health) { item = item_type::jungle_shard_of_health; limit = 5 - jungle_shards_; }
            const unsigned count = std::min(limit, me.gold_count() / item.cost());
            if (!count || !me.can_buy(item, count)) break;
            me.buy(item, count);
            if (build == fundamentals::BuildStage::monster_strength) monster_shards_ += count;
            if (build == fundamentals::BuildStage::jungle_health) jungle_shards_ += count;
        }
        const auto items = me.item_count();
        monster_passive_ = items[item_type::monster_emblem_of_strength] > 0;
        jungle_passive_ = items[item_type::jungle_emblem_of_health] > 0;
    }
    bitboard blocked() const {
        auto result = terrain_ | cambc::vision::champion_tiles() | cambc::vision::camp_tiles() | cambc::vision::teleport_tiles();
        result |= core_obstacles();
        result.set(cambc::self().position(), false); return result;
    }
    bitboard legal_walk() const {
        return blocked().reachable_tiles(cambc::self().position(), static_cast<std::uint16_t>(cambc::self().base_stats().base_move_speed));
    }
    bool ordinary_move(bitboard goals, const bitboard* avoid = nullptr) {
        auto& me = cambc::self();
        if (!me.can_move() || goals.is_set(me.position())) return false;
        auto walls = blocked(); if (avoid) walls |= *avoid;
        walls.set(me.position(), false); goals &= ~walls;
        Field route;
        if (!field(route, goals, walls, false) || route.get(me.position()) == unreachable) return false;
        for (int retry = 0; retry < 3; ++retry) {
            const auto legal = legal_walk() & ~walls & ~blocked();
            auto best = me.position(); auto best_d = route.get(best);
            for (const auto p : legal.positions()) {
                const auto d = route.get(p);
                if (d < best_d || (d == best_d && d < route.get(me.position()) && tile_index(p) < tile_index(best))) { best = p; best_d = d; }
            }
            if (best == me.position()) return false;
            if (me.move_absolute(best)) return true;
        }
        return false;
    }
    bool move_to(bitboard goals, const bitboard* avoid = nullptr) {
        if (core_move(goals, avoid)) return true;
        return ordinary_move(goals, avoid);
    }
    template<class Target> bool fight(const Target& target, bool walked, bool dangerous) {
        auto& me = cambc::self();
        if (cambc::fuel_remaining() < 16000) return false;
        auto valid = [&] { return target.is_visible() && target.health() > 0; };
        if (!valid()) return false;
        bool acted = false;
        const auto abilities = me.abilities();
        if (!walked) for (const auto& ability : abilities) {
            const bool category = [&] { if constexpr (std::is_same_v<Target, camp>) return ability.can_target_camps(); else return ability.can_target_enemies(); }();
            if ((ability.is_damage() || ability.is_cc()) && me.can_cast() && category && ability.is_ready() && ability.valid_tiles().is_set(target.position())) {
                acted = ability.use(target);
                break;
            }
        }
        if (!valid() || !me.attack_tiles().is_set(target.position())) return acted;
        if (dangerous) for (const auto& ability : abilities)
            if (ability.is_shield() && me.can_cast() && ability.can_target_self() && ability.is_ready()) { acted |= ability.use(); break; }
        if (me.can_attack()) { me.attack(target); acted = true; }
        if (!valid()) return acted;
        if (!me.can_attack()) for (const auto& ability : abilities)
            if (ability.is_attack_reset() && me.can_cast() && ability.can_target_self() && ability.is_ready()) {
                if (ability.use() && me.can_attack() && valid()) me.attack(target);
                acted = true; break;
            }
        return acted;
    }
    bool recover(const std::vector<camp>& camps, const std::vector<champion>& enemies) {
        auto& me = cambc::self(); const auto origin = me.position();
        bool safe = !enemy_near(origin, enemies); const auto avoid = danger(camps, enemies);
        for (const auto& m : camps) if (m.is_alive() && m.is_awake() && m.strength() > 0 && distance(origin, m.position()) <= m.attack_range() + 1) safe = false;
        if (safe) { me.recall(); return true; }
        auto best = origin; float best_score = -1e30f;
        for (const auto p : legal_walk().positions()) {
            float score = 0;
            for (const auto& m : camps) if (m.is_alive() && m.is_awake() && m.strength() > 0) score += std::min(distance(p, m.position()), 6) * m.strength();
            for (const auto& e : enemies) score += std::min(distance(p, e.position()), 9) * 150;
            if (score > best_score || (score == best_score && tile_index(p) < tile_index(best))) { best = p; best_score = score; }
        }
        if (best != origin && me.can_move() && legal_walk().is_set(best) && me.move_absolute(best)) return true;
        ordinary_move(cambc::map::spawn_region(team::ally), &avoid); return true;
    }
    bool bank() {
        auto& me = cambc::self();
        if (!me.gem_count() || altars_.empty() || !core_bank_request(nearest_altar())) return false;
        bitboard goals;
        for (const auto altar : altars_) {
            if (distance(me.position(), altar) <= cambc::constants::submit_gems_range) {
                for (const auto& lock : cambc::vision::altar_locks(team::enemy)) if (lock.position() == altar) {
                    if (me.can_attack() && me.attack_tiles().is_set(altar)) me.attack(lock);
                    return true;
                }
                me.submit_gems(altar);
                previous_submit_ = true;
                telemetry_.event("action_result", "tribute_start", integer("bag", me.gem_count()), true); return true;
            }
            goals |= around(altar, cambc::constants::submit_gems_range);
        }
        return move_to(goals);
    }
    std::vector<Choice> choices(const std::vector<camp>& camps, const std::vector<champion>& allies) {
        auto& me = cambc::self(); const auto visible = cambc::vision::visible_tiles();
        const auto round = cambc::round_number();
        const float dps = me.strength() * 1.125f + me.focus() * 0.12f;
        std::vector<Choice> result;
        for (std::size_t i = 0; i < groups_.size(); ++i) {
            if (cambc::fuel_remaining() < 45000) break;
            auto& g = groups_[i]; if (g.monolith || g.unavailable_until > round) continue;
            if (has_non_monolith_gems_ && monster_shards_ >= 5 && jungle_shards_ >= 5 && !g.group.gem_value()) continue;
            const bool visible_group = !(g.spawns & visible).is_empty(); bool available = false;
            if (visible_group) {
                const auto state = g.group.state();
                if (const auto* r = std::get_if<cambc::map::camp_group_respawning>(&state)) { g.unavailable_until = round + std::max(1, r->timer); continue; }
                available = std::holds_alternative<cambc::map::camp_group_reward_available>(state);
            }
            float health = g.health, incoming = g.incoming;
            if (visible_group && (g.spawns & ~visible).is_empty()) {
                health = incoming = 0;
                for (const auto& m : camps) if (g.spawns.is_set(m.spawn_position()) && m.is_alive()) { health += m.health(); incoming += m.strength(); }
            }
            incoming *= jungle_passive_ ? 0.7f : 1.0f;
            const float damage = dps * (monster_passive_ ? 1.1f : 1.0f);
            const float loss = incoming * (health / std::max(1.0f, damage) + 2);
            if (!core_group_allowed(static_cast<int>(i), loss)) continue;
            const bool affordable = available || fundamentals::affordable_camp(me.health(), me.max_health(), health, incoming, damage);
            float travel = static_cast<float>(distance(me.position(), g.anchor));
            for (const auto& ally : allies) if (ally != me && distance(ally.position(), g.anchor) <= 3) travel += 12;
            travel += static_cast<float>((i * 3 + identity_ * 11) % 7);
            float score = (g.group.gem_value() * 2.0f + g.group.gold_value() * 0.4f) / (travel + (available ? 0 : health / std::max(1.0f, dps)) + 10);
            if (target_ == static_cast<int>(i)) score *= 1.2f;
            result.push_back({static_cast<int>(i), score, health, incoming, available, affordable});
        }
        std::sort(result.begin(), result.end(), [&](const Choice& a, const Choice& b) {
            return std::abs(a.score - b.score) > epsilon ? a.score > b.score : groups_[a.index].id < groups_[b.index].id;
        });
        return result;
    }
    bool farm_group(int index, const std::vector<camp>& camps, bool outside = false) {
        if (index < 0 || index >= static_cast<int>(groups_.size())) return false;
        auto& me = cambc::self(); auto& g = groups_[index]; target_ = index;
        if (reward(g)) {
            if (g.spawns.is_set(me.position())) return true;
            move_to(g.spawns); return true;
        }
        std::optional<camp> target; float weakest = std::numeric_limits<float>::max();
        for (const auto& m : camps) if (g.spawns.is_set(m.spawn_position()) && m.is_alive()) {
            const float value = m.health() + distance(me.position(), m.position()) * (partial_clear_ ? 1000.0f : 100.0f);
            if (value < weakest - epsilon || (std::abs(value - weakest) <= epsilon && target && tile_index(m.spawn_position()) < tile_index(target->spawn_position()))) { weakest = value; target = m; }
        }
        if (target && fight(*target, false, g.incoming > 0)) return true;
        bitboard goals;
        if (target) goals = around(target->position(), me.attack_range());
        else for (const auto& m : g.group) goals |= around(m.spawn_position(), 1);
        if (outside) goals &= ~g.spawns;
        if (move_to(goals)) {
            for (const auto& m : cambc::vision::camps()) if (g.spawns.is_set(m.spawn_position()) && m.is_alive() && me.attack_tiles().is_set(m.position())) { fight(m, true, g.incoming > 0); break; }
        } else if (me.can_move()) g.unavailable_until = cambc::round_number() + 4;
        return true;
    }
    void farm(const std::vector<camp>& camps, const std::vector<champion>& allies) {
        auto candidates = choices(camps, allies);
        if (core_fork(candidates)) candidates = choices(cambc::vision::camps(), cambc::vision::champions(team::ally));
        int selected = -1;
        for (const auto& c : candidates) if (c.affordable) { selected = c.index; break; }
        partial_clear_ = false;
        if (selected < 0 && core_partial_allowed()) for (const auto& c : candidates)
            if (cambc::self().health() >= cambc::self().max_health() * 0.75f || c.index == target_) { selected = c.index; partial_clear_ = true; break; }
        target_ = selected;
        if (selected < 0) { if (!cambc::map::spawn_region(team::ally).is_set(cambc::self().position())) recovering_ = true; return; }
        core_selected(selected);
        farm_group(selected, cambc::vision::camps());
    }

    // Each source closure supplies only its own state machine and these narrow hooks.
#include "core.h"

public:
    void turn() {
        fields_ = 0; ++turn_; origin_field_.reset();
        auto& me = cambc::self();
        const bool cold_metadata_turn = !initialized_;
        if (cold_metadata_turn) initialize();
        telemetry_.tick();
        if constexpr (!profile::probe_mode) {
            // Public group materialisation and first telemetry already consume
            // most of the doubled first-turn quantum on Swirl. Start the one
            // mission on a fresh owner turn; never suspend mid prediction.
            if (cold_metadata_turn) return;
            // Keep the initial plan's formatted record separate from the first
            // mission observations and navigation too. The failed v1 resumed
            // that old record on its following turn, then used another83k.
            if (!opening_loaded_) { load_opening(); return; }
        }
        const auto score = cambc::global_submitted_gems(team::ally);
        if (previous_alive_ && !me.is_alive()) telemetry_.event("observation", "own_death", integer("previousBag", previous_bag_), true);
        if (previous_submit_ && !me.is_submitting_gems() && me.gem_count() == 0 && score > previous_score_)
            telemetry_.event("observation", "tribute_complete", integer("scoreDelta", score - previous_score_) + "," + integer("previousBag", previous_bag_), true);
        core_observe();
        previous_bag_ = me.gem_count(); previous_score_ = score; previous_alive_ = me.is_alive(); previous_submit_ = me.is_submitting_gems();
        if constexpr (!profile::probe_mode) if (cambc::fuel_remaining() < 45000) return;
        if (!me.is_alive()) { shop(); recovering_ = false; target_ = -1; return; }
        if (me.is_recalling() || me.is_submitting_gems() || me.is_teleporting()) return;
        if (!me.can_move() && !me.can_attack() && !me.can_cast()) return;
        shop();
        const bool spawn = cambc::map::spawn_region(team::ally).is_set(me.position());
        if (spawn) { recovering_ = false; if (me.health() < me.max_health()) return; }
        // Mission observations/predictions and shopping must not spend the
        // reserve on materialising personal-vision vectors in the same turn.
        if constexpr (!profile::probe_mode) if (cambc::fuel_remaining() < 45000) return;
        const auto camps = cambc::vision::camps(); const auto enemies = cambc::vision::champions(team::enemy);
        if (cambc::fuel_remaining() < 30000) return;
        float incoming = 0;
        for (const auto& m : camps) if (m.is_alive() && m.is_awake() && distance(me.position(), m.position()) <= m.attack_range()) incoming += m.strength();
        for (const auto& e : enemies) if (distance(me.position(), e.position()) <= e.attack_range() + 1) incoming += e.strength();
        if (!spawn && (fundamentals::should_retreat(me.health(), me.max_health(), incoming * (partial_clear_ ? 2.0f : 1.0f)) || core_retreat(enemies))) recovering_ = true;
        if (recovering_) { core_recovery(); recover(camps, enemies); return; }
        for (std::size_t i = 0; i < groups_.size(); ++i) if (groups_[i].spawns.is_set(me.position()) && reward(groups_[i]) && !core_departing(static_cast<int>(i))) {
            target_ = static_cast<int>(i); return;
        }
        if (bank()) return;
        for (const auto& e : enemies) if (distance(me.position(), e.position()) <= 1 && !e.is_submitting_gems() && fight(e, false, true)) return;
        const auto allies = cambc::vision::champions(team::ally);
        if (core_turn(camps, enemies, allies)) return;
        if (!spawn && !is_actor(cambc::champion_type::tavi) && me.gold_count() >= 300) { recovering_ = true; core_recovery(); recover(camps, enemies); return; }
        farm(camps, allies);
    }
};
} // namespace
