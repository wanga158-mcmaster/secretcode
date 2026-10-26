#pragma once
#include <cambc.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>
#include "profile.h"
#include "policy.h"
#include "reservoir-policy.h"
#include "telemetry.h"

#ifndef RESERVOIR_SPAWN_VISIBILITY
#define RESERVOIR_SPAWN_VISIBILITY 0
#endif
#ifndef RESERVOIR_CHOICE_SNAPSHOT
#define RESERVOIR_CHOICE_SNAPSHOT 0
#endif
#ifndef RESERVOIR_WORK_DIAGNOSTICS
#define RESERVOIR_WORK_DIAGNOSTICS 0
#endif
#ifndef RESERVOIR_WORK_PREP
#define RESERVOIR_WORK_PREP 0
#endif
#ifndef RESERVOIR_TAVI_CAPTURE_ACCESS
#define RESERVOIR_TAVI_CAPTURE_ACCESS 0
#endif
#ifndef RESERVOIR_SELECTION_DIAGNOSTICS
#define RESERVOIR_SELECTION_DIAGNOSTICS 0
#endif

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
#if RESERVOIR_TAVI_CAPTURE_ACCESS
    std::optional<Field> tavi_capture_field_;
#endif
#if RESERVOIR_WORK_PREP
    std::optional<bitboard> work_obstacles_;
    vec2 work_obstacle_position_{};
    unsigned work_obstacle_round_ = 0;
#endif
    unsigned previous_bag_ = 0, previous_score_ = 0;
    bool previous_alive_ = true, previous_submit_ = false;
#if RESERVOIR_SELECTION_DIAGNOSTICS
    struct SelectionDiagnostic {
        bool active = false, capture_setup = false, cached = false, certify = false;
        unsigned round = 0, fields_before = 0, fields_after = 0;
        std::uint64_t farm_fuel = 0, init_fuel = 0, first_fence = 0, last_fence = 0;
        std::size_t prefix = 0, priced = 0, rejected = 0;
        int selected = -1;
        void emit(unsigned end_fields) const {
            if (!active) return;
            std::printf("reservoir selection r=%u farm=%llu init=%llu first=%llu last=%llu initFields=%u/%u cached=%u certify=%u prefix=%zu priced=%zu reject=%zu selected=%d endFields=%u\n",
                round, static_cast<unsigned long long>(farm_fuel), static_cast<unsigned long long>(init_fuel),
                static_cast<unsigned long long>(first_fence), static_cast<unsigned long long>(last_fence),
                fields_before, fields_after, unsigned(cached), unsigned(certify), prefix, priced, rejected, selected, end_fields);
        }
    } selection_diagnostic_;
#endif

    bool field(Field& output, const bitboard& roots, const bitboard& walls, bool complete = true) {
#if RESERVOIR_SELECTION_DIAGNOSTICS
        // Reuse the existing admission query, including its short circuit.
        const auto remaining = fields_ >= 2 ? 0 : cambc::fuel_remaining();
        if (selection_diagnostic_.active && selection_diagnostic_.capture_setup)
            selection_diagnostic_.init_fuel = remaining;
        if (fields_ >= 2 || remaining < 30000 || roots.is_empty()) return false;
#else
        if (fields_ >= 2 || cambc::fuel_remaining() < 30000 || roots.is_empty()) return false;
#endif
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
#if RESERVOIR_TAVI_CAPTURE_ACCESS
    bool tavi_ordinary_capture_context() const {
        auto& me = cambc::self();
        if (!is_actor(cambc::champion_type::tavi)) return false;
        for (const auto& ability : me.abilities()) if (ability.is_dash()) return false;
        return me.consumables()[cambc::consumable_type::tp_crystal].count() == 0;
    }
    bool ensure_tavi_capture_field() {
        const auto p = cambc::self().position();
        // Static walking access is reusable within this component. A later
        // relocation outside it requires a new bounded native field.
        if (tavi_capture_field_ && tavi_capture_field_->get(p) != unreachable) return true;
        tavi_capture_field_.reset();
        Field value;
        if (!field(value, around(p, 0), terrain_)) return false;
        tavi_capture_field_ = std::move(value);
        return true;
    }
    bool tavi_capture_access(const Group& g) const {
        // Completing an ordinary camp job needs an actual capture member,
        // not merely a ranged attack tile. This is not a dynamic safety proof.
        for (const auto& member : g.group)
            if (tavi_capture_field_->get(member.spawn_position()) != unreachable) return true;
        return false;
    }
#endif
#if RESERVOIR_SPAWN_VISIBILITY
    bool any_spawn_visible(const Group& g, const bitboard& visible) const {
        // Spawn membership and vision are the same sets as the mask expression.
        // Avoid scanning every map word for each immutable physical group.
        for (const auto& member : g.group) if (visible.is_set(member.spawn_position())) return true;
        return false;
    }
    bool all_spawns_visible(const Group& g, const bitboard& visible) const {
        for (const auto& member : g.group) if (!visible.is_set(member.spawn_position())) return false;
        return true;
    }
    bool seen(const Group& g) const { return any_spawn_visible(g, cambc::vision::visible_tiles()); }
#else
    bool seen(const Group& g) const { return !(g.spawns & cambc::vision::visible_tiles()).is_empty(); }
#endif
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
        result.set(cambc::self().position(), false); return result;
    }
    bitboard legal_walk() const {
        return blocked().reachable_tiles(cambc::self().position(), static_cast<std::uint16_t>(cambc::self().base_stats().base_move_speed));
    }
    bool ordinary_move(bitboard goals, const bitboard* avoid = nullptr) {
        auto& me = cambc::self();
        if (!me.can_move() || goals.is_set(me.position())) return false;
#if RESERVOIR_WORK_PREP
        auto walls = [&] {
            if (work_obstacles_ && work_obstacle_round_ == cambc::round_number() && work_obstacle_position_ == me.position()) {
                auto prepared = std::move(*work_obstacles_); work_obstacles_.reset(); return prepared;
            }
            work_obstacles_.reset(); return blocked();
        }();
#else
        auto walls = blocked();
#endif
        if (avoid) walls |= *avoid;
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
            if ((ability.is_damage() || ability.is_cc()) && category && ability.is_ready() && ability.valid_tiles().is_set(target.position())) {
                acted = ability.use(target);
                break;
            }
        }
        if (!valid() || !me.attack_tiles().is_set(target.position())) return acted;
        if (dangerous) for (const auto& ability : abilities)
            if (ability.is_shield() && ability.can_target_self() && ability.is_ready()) { acted |= ability.use(); break; }
        if (me.can_attack()) { me.attack(target); acted = true; }
        if (!valid()) return acted;
        if (!me.can_attack()) for (const auto& ability : abilities)
            if (ability.is_attack_reset() && ability.can_target_self() && ability.is_ready()) {
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
#if RESERVOIR_SELECTION_DIAGNOSTICS
        if (selection_diagnostic_.active) {
            selection_diagnostic_.fields_before = fields_;
#if RESERVOIR_TAVI_CAPTURE_ACCESS
            selection_diagnostic_.cached = bool(tavi_capture_field_);
            selection_diagnostic_.capture_setup = true;
#endif
        }
#endif
#if RESERVOIR_TAVI_CAPTURE_ACCESS
        // Only Tavi's ordinary job admission changes. Other actors and any
        // later mobility-consumable state retain the existing selection path.
        // An incomplete field supplies no negative applicability conclusion.
        const bool certify_capture = tavi_ordinary_capture_context() && ensure_tavi_capture_field();
#endif
#if RESERVOIR_SELECTION_DIAGNOSTICS
        if (selection_diagnostic_.active) {
            selection_diagnostic_.capture_setup = false;
            selection_diagnostic_.fields_after = fields_;
#if RESERVOIR_TAVI_CAPTURE_ACCESS
            selection_diagnostic_.certify = certify_capture;
#endif
        }
#endif
#if RESERVOIR_CHOICE_SNAPSHOT
        // This ranking issues no action. Reuse its own current personal reads;
        // no observations survive into another decision or owner turn.
        const auto own_position = me.position();
        const float own_health = me.health(), own_maximum = me.max_health();
        std::vector<vec2> ally_positions;
        for (const auto& ally : allies) if (ally != me) ally_positions.push_back(ally.position());
#endif
        std::vector<Choice> result;
#if RESERVOIR_SELECTION_DIAGNOSTICS
        std::size_t i = 0;
        std::uint64_t ranking_fuel = 0;
        for (; i < groups_.size(); ++i) {
            // The decision still uses its one existing query at each fence.
            ranking_fuel = cambc::fuel_remaining();
            if (i == 0 && selection_diagnostic_.active) selection_diagnostic_.first_fence = ranking_fuel;
#if RESERVOIR_WORK_PREP
            if (ranking_fuel < 50000) break;
#else
            if (ranking_fuel < 45000) break;
#endif
#else
        for (std::size_t i = 0; i < groups_.size(); ++i) {
#if RESERVOIR_WORK_PREP
            // Retain the unchanged 30k field floor plus the observed source,
            // goal and epoch-check tail. Optional ranking already used a
            // bounded prefix; every retained group is still fully evaluated.
            if (cambc::fuel_remaining() < 50000) break;
#else
            if (cambc::fuel_remaining() < 45000) break;
#endif
#endif
            auto& g = groups_[i]; if (g.monolith || g.unavailable_until > round) continue;
#if RESERVOIR_TAVI_CAPTURE_ACCESS
            if (certify_capture && !tavi_capture_access(g)) {
#if RESERVOIR_SELECTION_DIAGNOSTICS
                if (selection_diagnostic_.active) ++selection_diagnostic_.rejected;
#endif
                continue;
            }
#endif
            if (has_non_monolith_gems_ && monster_shards_ >= 5 && jungle_shards_ >= 5 && !g.group.gem_value()) continue;
#if RESERVOIR_SPAWN_VISIBILITY
            const bool visible_group = any_spawn_visible(g, visible); bool available = false;
#else
            const bool visible_group = !(g.spawns & visible).is_empty(); bool available = false;
#endif
            if (visible_group) {
                const auto state = g.group.state();
                if (const auto* r = std::get_if<cambc::map::camp_group_respawning>(&state)) { g.unavailable_until = round + std::max(1, r->timer); continue; }
                available = std::holds_alternative<cambc::map::camp_group_reward_available>(state);
            }
            float health = g.health, incoming = g.incoming;
#if RESERVOIR_SPAWN_VISIBILITY
            if (visible_group && all_spawns_visible(g, visible)) {
#else
            if (visible_group && (g.spawns & ~visible).is_empty()) {
#endif
                health = incoming = 0;
                for (const auto& m : camps) if (g.spawns.is_set(m.spawn_position()) && m.is_alive()) { health += m.health(); incoming += m.strength(); }
            }
            incoming *= jungle_passive_ ? 0.7f : 1.0f;
            const float damage = dps * (monster_passive_ ? 1.1f : 1.0f);
            const float loss = incoming * (health / std::max(1.0f, damage) + 2);
            if (!core_group_allowed(static_cast<int>(i), loss)) continue;
#if RESERVOIR_CHOICE_SNAPSHOT
            const bool affordable = available || fundamentals::affordable_camp(own_health, own_maximum, health, incoming, damage);
            float travel = static_cast<float>(distance(own_position, g.anchor));
            for (const auto position : ally_positions) if (distance(position, g.anchor) <= 3) travel += 12;
#else
            const bool affordable = available || fundamentals::affordable_camp(me.health(), me.max_health(), health, incoming, damage);
            float travel = static_cast<float>(distance(me.position(), g.anchor));
            for (const auto& ally : allies) if (ally != me && distance(ally.position(), g.anchor) <= 3) travel += 12;
#endif
            travel += static_cast<float>((i * 3 + identity_ * 11) % 7);
            float score = (g.group.gem_value() * 2.0f + g.group.gold_value() * 0.4f) / (travel + (available ? 0 : health / std::max(1.0f, dps)) + 10);
            if (target_ == static_cast<int>(i)) score *= 1.2f;
            result.push_back({static_cast<int>(i), score, health, incoming, available, affordable});
        }
        std::sort(result.begin(), result.end(), [&](const Choice& a, const Choice& b) {
            return std::abs(a.score - b.score) > epsilon ? a.score > b.score : groups_[a.index].id < groups_[b.index].id;
        });
#if RESERVOIR_SELECTION_DIAGNOSTICS
        if (selection_diagnostic_.active) {
            selection_diagnostic_.prefix = i;
            selection_diagnostic_.priced = result.size();
            selection_diagnostic_.last_fence = ranking_fuel;
        }
#endif
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
#if RESERVOIR_WORK_DIAGNOSTICS
        const auto route_fuel = cambc::fuel_remaining();
        const auto route_fields = fields_;
        const bool moved = move_to(goals);
        if (!moved && identity_ == static_cast<unsigned>(cambc::champion_type::tavi)
            && (cambc::round_number()==12 || cambc::round_number()==16 || cambc::round_number()==20)
            && cambc::fuel_remaining()>10000) {
            const auto p=me.position();
            const auto t=target?target->position():g.anchor;
            const bool attack=target&&me.attack_tiles().is_set(t);
            std::printf("reservoir executor round=%u group=%d position=%d,%d target=%d,%d visible=%d attack=%d inGoals=%d fuel=%llu fields=%u/%u left=%llu\n",
                cambc::round_number(),g.id,p.x,p.y,t.x,t.y,int(bool(target)),int(attack),int(goals.is_set(p)),
                static_cast<unsigned long long>(route_fuel),route_fields,fields_,static_cast<unsigned long long>(cambc::fuel_remaining()));
        }
        if (moved) {
#else
        if (move_to(goals)) {
#endif
            for (const auto& m : cambc::vision::camps()) if (g.spawns.is_set(m.spawn_position()) && m.is_alive() && me.attack_tiles().is_set(m.position())) { fight(m, true, g.incoming > 0); break; }
        } else if (me.can_move()) g.unavailable_until = cambc::round_number() + 4;
        return true;
    }
    void farm(const std::vector<camp>& camps, const std::vector<champion>& allies) {
#if RESERVOIR_WORK_PREP
        // Pay the mandatory route observation before optional ranking. The
        // ranking is action-free; consume this exact mask once, and reject
        // reuse after a fork, movement or owner-round change.
        work_obstacles_.reset();
#if RESERVOIR_SELECTION_DIAGNOSTICS
        const auto work_fuel = cambc::fuel_remaining();
        if (selection_diagnostic_.active) selection_diagnostic_.farm_fuel = work_fuel;
        if (work_fuel >= 45000) {
#else
        if (cambc::fuel_remaining() >= 45000) {
#endif
            work_obstacle_position_ = cambc::self().position();
            work_obstacle_round_ = cambc::round_number();
            work_obstacles_ = blocked();
        }
        auto candidates = choices(camps, allies);
        if (core_fork(candidates)) {
            work_obstacles_.reset();
            candidates = choices(cambc::vision::camps(), cambc::vision::champions(team::ally));
        }
#else
        auto candidates = choices(camps, allies);
        if (core_fork(candidates)) candidates = choices(cambc::vision::camps(), cambc::vision::champions(team::ally));
#endif
        int selected = -1;
        for (const auto& c : candidates) if (c.affordable) { selected = c.index; break; }
        partial_clear_ = false;
        if (selected < 0 && core_partial_allowed()) for (const auto& c : candidates)
            if (cambc::self().health() >= cambc::self().max_health() * 0.75f || c.index == target_) { selected = c.index; partial_clear_ = true; break; }
        target_ = selected;
        if (selected < 0) { if (!cambc::map::spawn_region(team::ally).is_set(cambc::self().position())) recovering_ = true; return; }
        core_selected(selected);
        farm_group(selected, cambc::vision::camps());
#if RESERVOIR_SELECTION_DIAGNOSTICS
        if (selection_diagnostic_.active) selection_diagnostic_.selected = groups_[selected].id;
#endif
    }

    // Each source closure supplies only its own state machine and these narrow hooks.
#include "core.h"

public:
    void turn() {
#if RESERVOIR_WORK_PREP
        work_obstacles_.reset();
#endif
        fields_ = 0; ++turn_; origin_field_.reset();
        auto& me = cambc::self(); if (!initialized_) initialize();
#if RESERVOIR_SELECTION_DIAGNOSTICS
        selection_diagnostic_ = {};
        if (identity_ == static_cast<unsigned>(cambc::champion_type::tavi)) {
            selection_diagnostic_.round = cambc::round_number();
            selection_diagnostic_.active = selection_diagnostic_.round >= 1 && selection_diagnostic_.round <= 3;
        }
        // Emit only after the original turn path has completed all its actions.
        struct DeferredSelectionDiagnostic {
            Bot& bot;
            ~DeferredSelectionDiagnostic() { bot.selection_diagnostic_.emit(bot.fields_); }
        } deferred_selection_diagnostic{*this};
#endif
        telemetry_.context(core_summary()); telemetry_.tick();
        const auto score = cambc::global_submitted_gems(team::ally);
        if (previous_alive_ && !me.is_alive()) telemetry_.event("observation", "own_death", integer("previousBag", previous_bag_), true);
        if (previous_submit_ && !me.is_submitting_gems() && me.gem_count() == 0 && score > previous_score_)
            telemetry_.event("observation", "tribute_complete", integer("scoreDelta", score - previous_score_) + "," + integer("previousBag", previous_bag_), true);
        core_observe();
        previous_bag_ = me.gem_count(); previous_score_ = score; previous_alive_ = me.is_alive(); previous_submit_ = me.is_submitting_gems();
        if (!me.is_alive()) { shop(); recovering_ = false; target_ = -1; return; }
        if (me.is_recalling() || me.is_submitting_gems() || me.is_teleporting()) return;
        if (!me.can_move() && !me.can_attack() && !me.can_cast()) return;
        shop();
        const bool spawn = cambc::map::spawn_region(team::ally).is_set(me.position());
        if (spawn) { recovering_ = false; if (me.health() < me.max_health()) return; }
        const auto camps = cambc::vision::camps(); const auto enemies = cambc::vision::champions(team::enemy);
        if (cambc::fuel_remaining() < 30000) return;
        float incoming = 0;
        for (const auto& m : camps) if (m.is_alive() && m.is_awake() && distance(me.position(), m.position()) <= m.attack_range()) incoming += m.strength();
        for (const auto& e : enemies) if (distance(me.position(), e.position()) <= e.attack_range() + 1) incoming += e.strength();
        if (!spawn && (fundamentals::should_retreat(me.health(), me.max_health(), incoming * (partial_clear_ ? 2.0f : 1.0f)) || core_retreat(enemies))) recovering_ = true;
        if (recovering_) { core_recovery(); recover(camps, enemies); return; }
        if (winning_bag() && bank()) return;
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
