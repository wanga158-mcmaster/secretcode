#include <cambc.h>
#include <array>
#include <algorithm>
#include <iostream>
#include <limits>
#include <optional>
#include <variant>
#include <vector>
#include <cstdio>
#include <string>
#include <type_traits>
#include <cmath>
#include "policy.h"
#include "profile.h"

#ifndef CONVEYOR_REFRESH_INVENTORY_ONCE
#define CONVEYOR_REFRESH_INVENTORY_ONCE 0
#endif

namespace {
using cambc::vec2;
using cambc::team;
using cambc::champion;
using cambc::camp;
using cambc::bitboard;
using cambc::item_type;

int distance(vec2 a, vec2 b) { return static_cast<int>(a.chebyshev_distance(b)); }
bitboard around(vec2 p, int radius) {
    bitboard result;
    result.draw_chebyshev(p, radius);
    return result;
}

void draft() {
    constexpr std::array order{cambc::champion_draft::tavi, cambc::champion_draft::varg,
        cambc::champion_draft::vendetta, cambc::champion_draft::vesper_vale,
        cambc::champion_draft::marcella};
    const auto selected = cambc::draft::current_draft(team::ally);
    for (const auto choice : order)
        if (std::find(selected.begin(), selected.end(), choice) == selected.end()) {
            cambc::draft::pick(choice);
            return;
        }
}

struct Group {
    cambc::map::camp_group group;
    bitboard spawns;
    vec2 anchor;
    float health = 0, incoming = 0;
    bool monolith = false;
    unsigned unavailable_until = 0;
    unsigned key = 0;
};

class Bot {
    std::vector<Group> groups_;
    std::vector<int> group_at_tile_;
    std::vector<vec2> altars_;
    bitboard terrain_;
    bitboard original_spawns_;
    bool initialized_ = false;
    bool recovering_ = false;
    bool partial_clear_ = false;
    bool has_non_monolith_gems_ = false;
    int target_ = -1;
    unsigned monster_shards_ = 0, jungle_shards_ = 0;
    unsigned identity_ = 0;
    unsigned fields_this_turn_ = 0, navigation_failures_ = 0, purchases_ = 0;
    unsigned target_changes_ = 0, blocked_retries_ = 0, planning_guards_ = 0;
    unsigned fuel_low_water_ = std::numeric_limits<unsigned>::max();
    unsigned bank_turns_ = 0, threshold_only_turns_ = 0, journeys_ = 0;
    unsigned seq_ = 0, details_ = 0, summaries_ = 0, dropped_ = 0;
    unsigned last_turn_ = 0, turn_gaps_ = 0, summary_bucket_ = 0;
    std::string actor_;
    enum class State { farm, bank_requested, channel, recover };
    State state_ = State::farm;
    conveyor::BankReason last_reason_ = conveyor::BankReason::none;
    bool tribute_active_ = false;
    unsigned tribute_bag_ = 0, tribute_round_ = 0, tribute_score_ = 0;
    unsigned tribute_starts_ = 0, tribute_completions_ = 0, tribute_interruptions_ = 0;
    int held_group_ = -1, overlap_group_ = -1;
    vec2 overlap_tile_{};
    bool overlap_tile_fixed_ = false;
    unsigned held_entry_round_ = 0, held_entry_gems_ = 0;
    float held_entry_health_ = 0;
    float overlap_damage_ = 0;
    unsigned overlap_basics_ = 0, overlap_casts_ = 0, overlap_moves_ = 0, overlap_turns_ = 0;
    unsigned probe_start_ = 0, probe_entry_round_ = 0, probe_a_done_round_ = 0;
    int probe_group_ = -1, probe_other_ = -1;
    bool probe_selected_ = false, probe_triggered_ = false, probe_finished_ = false;
    bool probe_a_done_ = false, probe_b_holding_ = false, probe_witness_ = false, probe_action_seen_ = false;
    float probe_b_initial_health_ = 0;
    vec2 probe_stage_{};
    float camp_multiplier_ = 1, incoming_multiplier_ = 1;

    static unsigned tile(vec2 p) { return p.y * cambc::map::width() + p.x; }
    static std::string boolean(bool value) { return value ? "true" : "false"; }
    int nearest_altar(vec2 p) const {
        int nearest = 10000;
        for (auto altar : altars_) nearest = std::min(nearest, distance(p, altar));
        return nearest;
    }
    void event(const char* kind, const char* subtype, const std::string& data = "", bool essential = false) {
        if (cambc::fuel_remaining() < 8000) { ++dropped_; return; }
        const bool summary = std::string(kind) == "summary", start = std::string(kind) == "start";
        // Setup/detail events cannot consume the eight reserved probe witnesses.
        const unsigned cap = profile::probe_mode && !essential ? 24 : 32;
        if ((!start && !summary && details_ >= cap) || (summary && summaries_ >= 62)) { ++dropped_; return; }
        char line[513];
        const int length = std::snprintf(line, sizeof(line),
            "BC26T{\"s\":1,\"v\":\"%s\",\"c\":\"%s\",\"a\":\"%s\",\"r\":%u,\"q\":%u,\"k\":\"%s\",\"d\":{\"subtype\":\"%s\"%s%s}}\n",
            profile::version_id, profile::core_id, actor_.c_str(), cambc::round_number(), seq_+1,
            kind, subtype, data.empty() ? "" : ",", data.c_str());
        if (length <= 0 || length > 512) { ++dropped_; return; }
        ++seq_; if (summary) ++summaries_; else if (!start) ++details_;
        std::cout.write(line, length);
    }
    void probe_stop(const char* kind, const char* reason) {
        event(kind, "conveyor_probe", "\"reason\":\"" + std::string(reason) + "\"", true);
        probe_finished_ = true;
    }
    void telemetry_tick() {
        const unsigned round = cambc::round_number();
        if (last_turn_ && round > last_turn_ + 1) turn_gaps_ += round-last_turn_-1;
        last_turn_ = round;
        auto& me = cambc::self();
        if (tribute_active_ && (!me.is_alive() || !me.is_submitting_gems())) {
            const unsigned score = cambc::global_submitted_gems(team::ally);
            const bool complete = me.is_alive() && me.gem_count() == 0 && score >= tribute_score_ + tribute_bag_;
            if (complete) ++tribute_completions_; else ++tribute_interruptions_;
            event("observation", "tribute_result", "\"starting_bag\":" + std::to_string(tribute_bag_)
                + ",\"remaining_bag\":" + std::to_string(me.gem_count()) + ",\"start_round\":" + std::to_string(tribute_round_)
                + ",\"team_score_delta\":" + std::to_string(score-tribute_score_) + ",\"complete\":" + boolean(complete), true);
            tribute_active_ = false; state_ = State::farm;

        }
        if (round/32 > summary_bucket_) {
            summary_bucket_ = round/32;
            event("summary", "counters", "\"lastObservedRound\":" + std::to_string(round)
                + ",\"droppedEvents\":" + std::to_string(dropped_) + ",\"turn_gaps\":" + std::to_string(turn_gaps_)
                + ",\"overlap_turns\":" + std::to_string(overlap_turns_) + ",\"overlap_basics\":" + std::to_string(overlap_basics_)
                + ",\"overlap_casts\":" + std::to_string(overlap_casts_) + ",\"overlap_moves\":" + std::to_string(overlap_moves_)
                + ",\"useful_damage\":" + std::to_string(overlap_damage_) + ",\"planning_guards\":" + std::to_string(planning_guards_)
                + ",\"fuel_low_water\":" + std::to_string(fuel_low_water_));
        }
    }

    void initialize() {
        terrain_ = cambc::map::walls() | cambc::map::holes() | cambc::map::altars()
            | cambc::map::spawn_region(team::enemy);
        altars_ = cambc::map::altars().positions();
        group_at_tile_.assign(cambc::map::width() * cambc::map::height(), -1);
        for (auto group : cambc::map::camp_groups()) {
            Group entry{group, group.spawn_tiles(), group.camps().front().spawn_position()};
            for (const auto& monster : group) {
                if (tile(monster.spawn_position()) < tile(entry.anchor)) entry.anchor = monster.spawn_position();
                const auto p = monster.spawn_position();
                group_at_tile_[p.y * cambc::map::width() + p.x] = static_cast<int>(groups_.size());
                // All metadata here is public; no hidden live-state queries.
                const auto stats = monster.base_stats();
                entry.health += stats.base_health;
                entry.incoming += stats.base_strength;
                entry.monolith |= monster.is_monolith();
            }
            has_non_monolith_gems_ |= !entry.monolith && entry.group.gem_value() > 0;
            original_spawns_ |= entry.spawns;
            groups_.push_back(std::move(entry));
        }
        std::sort(groups_.begin(), groups_.end(), [](const Group& a, const Group& b) { return a.anchor.y == b.anchor.y ? a.anchor.x < b.anchor.x : a.anchor.y < b.anchor.y; });
        for (unsigned i = 0; i < groups_.size(); ++i) {
            groups_[i].key = tile(groups_[i].anchor);
            for (const auto& member : groups_[i].group) group_at_tile_[tile(member.spawn_position())] = i;
        }
        std::sort(altars_.begin(), altars_.end(), [](vec2 a, vec2 b) { return tile(a) < tile(b); });
        identity_ = static_cast<unsigned>(cambc::self().champion_name());
        actor_ = cambc::to_string(cambc::self().champion_name());
        event("start", "profile", "\"enabled\":" + boolean(profile::core_enabled) + ",\"probe_mode\":" + boolean(profile::probe_mode), true);
        initialized_ = true;
    }

#if CONVEYOR_REFRESH_INVENTORY_ONCE
    unsigned item_progress(item_type shard, const cambc::enum_map<item_type>& inventory) const {
        const auto emblem = shard.result();
        // Only this declared item stage is bought; existing emblems represent
        // their consumed shards. Do not infer a passive from stage counters.
        return emblem && inventory[*emblem] ? shard.combine_count() : inventory[shard];
    }
#else
    unsigned item_progress(item_type shard) const {
        const auto inventory = cambc::self().item_count();
        const auto emblem = shard.result();
        // Only this declared item stage is bought; existing emblems represent
        // their consumed shards. Do not infer a passive from stage counters.
        return emblem && inventory[*emblem] ? shard.combine_count() : inventory[shard];
    }
#endif
    void refresh_equipment() {
        const auto inventory = cambc::self().item_count();
#if CONVEYOR_REFRESH_INVENTORY_ONCE
        monster_shards_ = item_progress(item_type::monster_shard_of_strength, inventory);
        jungle_shards_ = item_progress(item_type::jungle_shard_of_health, inventory);
#else
        monster_shards_ = item_progress(item_type::monster_shard_of_strength);
        jungle_shards_ = item_progress(item_type::jungle_shard_of_health);
#endif
        camp_multiplier_ = cambc::self().base_stats().camp_damage_multiplier;
        incoming_multiplier_ = 1;
        for (auto item : {item_type::monster_emblem_of_strength, item_type::monster_emblem_of_focus, item_type::monster_emblem_of_health})
            if (inventory[item]) { camp_multiplier_ *= 1 + item.info().passive_magnitude; break; }
        for (auto item : {item_type::jungle_emblem_of_strength, item_type::jungle_emblem_of_focus, item_type::jungle_emblem_of_health})
            if (inventory[item]) { incoming_multiplier_ *= 1 - item.info().passive_magnitude; break; }
        for (auto item : {item_type::durability_emblem_of_strength, item_type::durability_emblem_of_focus, item_type::durability_emblem_of_health})
            if (inventory[item]) { incoming_multiplier_ *= 1 - item.info().passive_magnitude; break; }

    }
    void shop() {
        auto& me = cambc::self();
        refresh_equipment();
        // A bulk buy completes one stage at a time; Tavi may do this in the field.
        for (int stage = 0; stage < 3; ++stage) {
            const bool first = monster_shards_ < item_type::monster_shard_of_strength.combine_count();
            const bool second = jungle_shards_ < item_type::jungle_shard_of_health.combine_count();
            auto item = item_type::power_shard_of_strength;
            unsigned limit = 100;
            if (first) {
                item = item_type::monster_shard_of_strength;
                limit = item.combine_count() - monster_shards_;
            } else if (second) {
                item = item_type::jungle_shard_of_health;
                limit = item.combine_count() - jungle_shards_;
            }
            const unsigned count = std::min(limit, me.gold_count() / item.cost());
            if (!count || !me.can_buy(item, count)) { refresh_equipment(); return; }
            me.buy(item, count);
            ++purchases_;
            if (first) monster_shards_ += count;
            else if (second) jungle_shards_ += count;
        }
        refresh_equipment();
    }

    bitboard blocked() const {
        auto result = terrain_ | cambc::vision::champion_tiles() | cambc::vision::camp_tiles()
            | cambc::vision::teleport_tiles();
        result.set(cambc::self().position(), false);
        return result;
    }

    bool move_to(bitboard goals, const bitboard* danger = nullptr) {
        auto& me = cambc::self();
        if (!me.can_move() || goals.is_set(me.position())) return false;
        auto walls = blocked();
        if (danger) walls |= *danger;
        walls.set(me.position(), false);
        goals &= ~walls;
        if (goals.is_empty() || cambc::fuel_remaining() < 30000 || fields_this_turn_ >= 2) return false;
        ++fields_this_turn_;
        // One native field, stopped at our tile, at most 256 expansion steps.
        // Never build fields inside candidate-scoring loops.
        cambc::distance_field field;
        bitboard visited;
        field.compute_to(walls, goals, around(me.position(), 0), visited, 256);
        if (field.get(me.position()) == cambc::distance_field::unreachable) return false;
        // Retry alternatives after a hidden occupant is revealed. The engine
        // preserves the movement allowance on an unsuccessful hidden collision.
        for (int retry = 0; retry < 3; ++retry) {
            const auto fresh_walls = blocked() | walls;
            auto best = me.position();
            auto best_distance = field.get(best);
            for (const auto p : fresh_walls.reachable_tiles(me.position(), 1).positions()) {
                const auto d = field.get(p);
                if (d < best_distance || (d == best_distance && d < field.get(me.position()) && tile(p) < tile(best))) { best = p; best_distance = d; }
            }
            if (best == me.position()) return false;
            if (me.move_absolute(best)) return true;
            ++blocked_retries_;
        }
        return false;
    }

    // A single visible target is attacked; refresh live validity after every
    // hit because kills and Varg splash can invalidate an earlier camp view.
    template<class Target>
    bool fight(const Target& target, bool walked, bool dangerous) {
        auto& me = cambc::self();
        if (cambc::fuel_remaining() < 16000) { ++planning_guards_; return false; }
        auto valid = [&] { return target.is_visible() && target.health() > 0; };
        if (!valid()) return false;
        bool acted = false;
        auto abilities = me.abilities();
        const auto category_ok = [](const cambc::ability& ability) {
            if constexpr (std::is_same_v<Target, camp>) return ability.can_target_camps();
            else return ability.can_target_enemies();
        };
        if (!walked) {
            // Tavi/Marcella burst from range before choosing to walk.
            for (const auto& ability : abilities)
                if ((ability.is_damage() || ability.is_cc()) && me.can_cast() && ability.is_ready()
                    && category_ok(ability)
                    && ability.valid_tiles().is_set(target.position())) {
                    acted |= ability.use(target);
                    break;
                }
        }
        if (!valid()) return acted;
        const bool in_range = me.attack_tiles().is_set(target.position());
        if (!in_range) return acted;
        if (dangerous)
            for (const auto& ability : abilities)
                if (ability.is_shield() && me.can_cast() && ability.is_ready() && ability.can_target_self()) { acted |= ability.use(); break; }
        if (me.can_attack()) { me.attack(target); acted = true; }
        if (!valid()) return acted;
        // Includes ranged champions that walked this turn: a reset explicitly
        // restores their attack allowance before the extra attack.
        if (!me.can_attack())
            for (const auto& ability : abilities)
                if (ability.is_attack_reset() && me.can_cast() && ability.is_ready() && ability.can_target_self()) {
                    if (ability.use() && me.can_attack() && valid()) me.attack(target);
                    acted = true;
                    break;
                }
        return acted;
    }

    bool recover(const std::vector<camp>& camps, const std::vector<champion>& enemies) {
        auto& me = cambc::self();
        const auto origin = me.position();
        bool safe = true;
        bitboard danger;
        for (const auto& monster : camps)
            if (monster.is_alive() && monster.is_awake() && monster.strength() > 0) {
                danger |= around(monster.position(), monster.attack_range());
                if (distance(origin, monster.position()) <= monster.attack_range() + 1) safe = false;
            }
        for (const auto& enemy : enemies) {
            danger |= around(enemy.position(), enemy.attack_range() + 1);
            if (distance(origin, enemy.position()) <= 8) safe = false;
        }
        if (safe) { me.recall(); return true; }
        // First get out of attack range. Maximise clearance locally if danger
        // surrounds us; a path whose starting tile is threatened may be blocked.
        auto legal = blocked().reachable_tiles(origin, 1);
        auto best = origin;
        float best_score = -1e30f;
        for (const auto p : legal.positions()) {
            float score = 0;
            for (const auto& monster : camps)
                if (monster.is_alive() && monster.is_awake() && monster.strength() > 0)
                    score += std::min(distance(p, monster.position()), 6) * monster.strength();
            for (const auto& enemy : enemies) score += std::min(distance(p, enemy.position()), 9) * 150;
            if (score > best_score) { best = p; best_score = score; }
        }
        if (best != origin && me.can_move() && me.move_absolute(best)) return true;
        move_to(cambc::map::spawn_region(team::ally), &danger);
        return true;
    }

    bool bank() {
        auto& me = cambc::self();
        const int nearest = nearest_altar(me.position());
        constexpr unsigned threshold = 24;
        const auto reason = altars_.empty() ? conveyor::BankReason::none : conveyor::bank_reason(
            me.gem_count(), cambc::round_number(), cambc::constants::max_rounds, nearest, threshold);
        const bool requested = reason != conveyor::BankReason::none;
        if (!requested) { state_ = State::farm; last_reason_ = reason; return false; }
        ++bank_turns_;
        if (reason == conveyor::BankReason::threshold && nearest > 4
            && !conveyor::late_round(cambc::round_number(), cambc::constants::max_rounds)) ++threshold_only_turns_;
        if (state_ != State::bank_requested || last_reason_ != reason) {
            ++journeys_;
            event("activation", "bank_requested", "\"bag\":" + std::to_string(me.gem_count())
                + ",\"threshold\":" + std::to_string(threshold) + ",\"nearest\":" + std::to_string(nearest)
                + ",\"reason\":\"" + conveyor::reason_name(reason) + "\"", profile::probe_mode);
        }
        state_ = State::bank_requested; last_reason_ = reason;
        bitboard goals;
        for (const auto altar : altars_) {
            if (distance(me.position(), altar) <= cambc::constants::submit_gems_range) {
                // Opposing locks take three ordinary attacks. Friendly locks
                // allow tributing. Only use a fresh visible lock observation.
                for (const auto& lock : cambc::vision::altar_locks(team::enemy))
                    if (lock.position() == altar) {
                        if (me.can_attack() && me.attack_tiles().is_set(lock.position())) me.attack(lock);
                        return true;
                    }
                tribute_bag_ = me.gem_count(); tribute_round_ = cambc::round_number();
                tribute_score_ = cambc::global_submitted_gems(team::ally);
                me.submit_gems(altar);
                tribute_active_ = me.is_submitting_gems();
                if (tribute_active_) {
                    state_ = State::channel; ++tribute_starts_;
                    event("action_result", "tribute_start", "\"bag\":" + std::to_string(tribute_bag_)
                        + ",\"altar\":" + std::to_string(tile(altar))
                        + ",\"remaining_ticks\":" + std::to_string(me.remaining_channel_time()), true);
                }
                return true;
            }
            goals |= around(altar, cambc::constants::submit_gems_range);
        }
        const bool moved = move_to(goals);
        if (!moved) ++navigation_failures_;
        return moved;
    }

    void farm(const std::vector<camp>& camps, const std::vector<champion>& allies) {
        auto& me = cambc::self();
        const auto visible = cambc::vision::visible_tiles();
        const unsigned round = cambc::round_number();
        int selected = -1;
        float best_score = -1;
        int partial_target = -1;
        float partial_score = -1;
        bool selected_reward = false;
        const float strength = me.strength();
        const float dps = strength * 1.125f + me.focus() * 0.12f;
        const auto origin = me.position();
        const float health = me.health(), maximum = me.max_health();
        std::vector<float> observed_health(groups_.size(), 0), observed_strength(groups_.size(), 0);
        for (const auto& monster : camps) {
            const auto p = monster.spawn_position();
            const int group = group_at_tile_[p.y * cambc::map::width() + p.x];
            if (group >= 0 && monster.is_alive()) {
                observed_health[group] += monster.health();
                observed_strength[group] += monster.strength();
            }
        }
        std::vector<vec2> ally_positions;
        for (const auto& ally : allies) if (ally != me) ally_positions.push_back(ally.position());
        for (std::size_t i = 0; i < groups_.size(); ++i) {
            // Reserve fuel for movement/combat and yielding. Exhaustion resumes
            // midway through an old decision next round, so stop voluntarily.
            if (cambc::fuel_remaining() < 45000) { ++planning_guards_; break; }
            auto& entry = groups_[i];
            if (entry.monolith || entry.unavailable_until > round) continue;
            // Gold establishes the first two emblems. Once equipped, pursue
            // score rather than farming safe gold clusters indefinitely. Maps
            // whose only gems are in monolith groups retain the gold tiebreak.
            if (has_non_monolith_gems_ && monster_shards_ >= item_type::monster_shard_of_strength.combine_count()
                && jungle_shards_ >= item_type::jungle_shard_of_health.combine_count()
                && entry.group.gem_value() == 0) continue;
            const bool seen = !(entry.spawns & visible).is_empty();
            bool reward = false;
            if (seen) {
                const auto state = entry.group.state();
                if (const auto* respawn = std::get_if<cambc::map::camp_group_respawning>(&state)) {
                    entry.unavailable_until = round + std::max(1, respawn->timer);
                    continue;
                }
                reward = std::holds_alternative<cambc::map::camp_group_reward_available>(state);
                // Hold a cleared camp through all five capture phases. Do not
                // start the altar journey merely because we carry older gems.
                if (reward && entry.spawns.is_set(origin)) { target_ = static_cast<int>(i); return; }
            }
            float remaining_health = entry.health;
            float remaining_incoming = entry.incoming;
            // Once all spawn tiles are visible, use remaining monsters rather
            // than repeatedly budgeting a full-health fight after every hit.
            if (seen && (entry.spawns & ~visible).is_empty()) {
                remaining_health = observed_health[i];
                remaining_incoming = observed_strength[i];
            }
            const float incoming = remaining_incoming * incoming_multiplier_;
            const bool affordable = reward || fundamentals::affordable_camp(health, maximum,
                remaining_health, incoming, dps * camp_multiplier_);
            float travel = static_cast<float>(distance(origin, entry.anchor));
            // Visible allies already farming/capturing discourage splitting
            // resources; a small deterministic bias spreads the opening.
            for (const auto p : ally_positions)
                if (distance(p, entry.anchor) <= 3) travel += 12;
            travel += static_cast<float>((i * 3 + identity_ * 11) % 7);
            const float value = entry.group.gem_value() * 2.0f + entry.group.gold_value() * 0.4f;
            float score = value / (travel + (reward ? 0 : remaining_health / std::max(1.0f, dps)) + 10);
            if (target_ == static_cast<int>(i)) score *= 1.2f;
            if (!affordable) {
                // Damage persists while camps sleep. If there is no affordable
                // full clear, make progress in several attack/recall trips.
                // Stay on a chosen attempt until the reactive retreat policy
                // fires, instead of abandoning it after the first damage tick.
                if ((health >= maximum * 0.75f || target_ == static_cast<int>(i)) && score > partial_score) {
                    partial_target = static_cast<int>(i);
                    partial_score = score;
                }
                continue;
            }
            if (score > best_score) { best_score = score; selected = static_cast<int>(i); selected_reward = reward; }
        }
        partial_clear_ = selected < 0 && partial_target >= 0;
        if (partial_clear_) selected = partial_target;
        if (target_ != selected) ++target_changes_;
        target_ = selected;
        if (selected < 0) {
            if (!cambc::map::spawn_region(team::ally).is_set(me.position())) recovering_ = true;
            return;
        }
        auto& entry = groups_[selected];
        if (selected_reward) {
            if (!move_to(entry.spawns)) entry.unavailable_until = round + 3;
            return;
        }
        std::optional<camp> target;
        float weakest = std::numeric_limits<float>::max();
        for (const auto& monster : camps)
            if (entry.spawns.is_set(monster.spawn_position()) && monster.is_alive()) {
                const auto score = monster.health() + distance(me.position(), monster.position())
                    * (partial_clear_ ? 1000.0f : 100.0f);
                if (score < weakest) { weakest = score; target = monster; }
            }
        if (target && fight(*target, false, entry.incoming > 0)) return;
        bitboard goals;
        if (target) goals = around(target->position(), me.attack_range());
        else {
            for (const auto& monster : entry.group) goals |= around(monster.spawn_position(), 1);
        }
        if (move_to(goals)) {
            // Movement can reveal the group behind a bush or wall.
            for (const auto& monster : cambc::vision::camps())
                if (entry.spawns.is_set(monster.spawn_position()) && monster.is_alive()
                    && me.attack_tiles().is_set(monster.position())) {
                    fight(monster, true, entry.incoming > 0);
                    break;
                }
        } else if (me.can_move()) { entry.unavailable_until = round + 4; ++navigation_failures_; }
    }

    std::string group_key(int i) const { return "g_" + std::to_string(groups_[i].key); }
    void end_overlap(const char* reason) {
        if (overlap_group_ >= 0)
            event("observation", "conveyor_end", "\"A\":\"" + group_key(held_group_) + "\",\"B\":\"" + group_key(overlap_group_)
                + "\",\"reason\":\"" + reason + "\"", profile::probe_mode);
        overlap_group_ = -1; overlap_tile_fixed_ = false;
    }
    void begin_hold(int group) {
        if (held_group_ == group) return;
        end_overlap("new_hold"); held_group_ = group;
        held_entry_round_ = cambc::round_number(); held_entry_gems_ = cambc::self().gem_count();
        held_entry_health_ = cambc::self().health();
        if (profile::probe_mode && group == probe_group_ && probe_entry_round_) held_entry_round_ = probe_entry_round_;
        event("observation", "conveyor_hold", "\"A\":\"" + group_key(group) + "\",\"tile\":" + std::to_string(tile(cambc::self().position()))
            + ",\"entry_round\":" + std::to_string(held_entry_round_), profile::probe_mode);
    }
    // Revalidate before any elective action, including death/channel/low-fuel returns.
    void audit_held_capture() {
        if (held_group_ < 0) return;
        auto& me = cambc::self();
        if (!me.is_alive()) {
            if (profile::probe_mode && probe_triggered_ && !probe_a_done_) probe_stop("mechanism_failed", "death_during_measured_capture");
            end_overlap("death"); held_group_ = -1; return;
        }
        const auto& a = groups_[held_group_];
        if (!a.spawns.is_set(me.position())) {
            if (profile::probe_mode && probe_triggered_ && !probe_a_done_)
                probe_stop("mechanism_failed", "own_displacement_interrupted_A");
            end_overlap("displacement"); held_group_ = -1; return;
        }
        if ((a.spawns & cambc::vision::visible_tiles()).is_empty()) { end_overlap("vision"); held_group_ = -1; return; }
        const auto state = a.group.state();
        if (std::holds_alternative<cambc::map::camp_group_reward_available>(state)) return;
        const bool complete = std::holds_alternative<cambc::map::camp_group_respawning>(state);
        event("observation", "conveyor_capture", "\"A\":\"" + group_key(held_group_) + "\",\"entry_round\":" + std::to_string(held_entry_round_)
            + ",\"complete_round\":" + std::to_string(cambc::round_number()) + ",\"continuous_occupancy\":true,\"complete\":" + boolean(complete)
            + ",\"bag_delta\":" + std::to_string(static_cast<int>(me.gem_count())-static_cast<int>(held_entry_gems_))
            + ",\"net_health_spent\":" + std::to_string(held_entry_health_-me.health()), profile::probe_mode);
        if (profile::probe_mode && held_group_ == probe_group_ && probe_triggered_) {
            const bool reward = complete && me.gem_count() == held_entry_gems_ + a.group.gem_value();
            float remaining = 0; bool known = (groups_[probe_other_].spawns & ~cambc::vision::visible_tiles()).is_empty();
            if (known) for (const auto& member : cambc::vision::camps())
                if (groups_[probe_other_].spawns.is_set(member.spawn_position()) && member.is_alive()) remaining += member.health();
            const bool contrast = known && (profile::core_enabled ? remaining < probe_b_initial_health_ - conveyor::epsilon
                : std::abs(remaining-probe_b_initial_health_) <= conveyor::epsilon);
            probe_a_done_ = reward; probe_a_done_round_ = cambc::round_number();
            if (reward && contrast) {
                probe_witness_ = true;
                event("mechanism_success", "conveyor_probe", "\"reason\":\"" + std::string(profile::core_enabled ? "B_damaged_during_continuous_A_capture" : "control_waited_through_A")
                    + "\",\"entry_round\":" + std::to_string(held_entry_round_) + ",\"complete_round\":" + std::to_string(probe_a_done_round_)
                    + ",\"B_initial\":" + std::to_string(probe_b_initial_health_) + ",\"B_remaining\":" + std::to_string(remaining), true);
            } else probe_stop(known ? "mechanism_failed" : "not_exercised", known ? "A_reward_or_B_damage_contrast_missing" : "B_visibility_lost_at_completion");
        }
        end_overlap(complete ? "completion" : "lifecycle"); held_group_ = -1;
    }
    struct SeenCamp { camp handle; vec2 position; int group; float health, strength; int range; bool awake; };
    std::vector<SeenCamp> camp_snapshot(const std::vector<camp>& camps) const {
        std::vector<SeenCamp> seen;
        for (const auto& member : camps) {
            if (cambc::fuel_remaining()<30000) return {}; // Never use a truncated fight observation.
            if (member.is_alive()) seen.push_back({member, member.position(), group_at_tile_[tile(member.spawn_position())], member.health(),
                member.strength(), member.attack_range(), member.is_awake()});
        }
        return seen;
    }
    float incoming_at(int other, vec2 p, const std::vector<SeenCamp>& seen) const {
        float incoming = 0;
        // Sleeping members of B will wake together. Already awake outside
        // camps also count; a stun supplies no hypothetical future protection.
        for (const auto& member : seen) if ((member.group == other || member.awake)
            && distance(p, member.position) <= member.range) incoming += member.strength * incoming_multiplier_;
        return incoming;
    }
    bool affordable_overlap(float health, float incoming) const {
        auto& me = cambc::self();
        const float dps = (me.strength()*1.125f + me.focus()*0.12f)*camp_multiplier_;
        return !fundamentals::should_retreat(me.health(), me.max_health(), incoming)
            && fundamentals::affordable_camp(me.health(), me.max_health(), health, incoming, dps);
    }
    bool stage_eligible(const Group& entry) const {
        return !entry.monolith && !(has_non_monolith_gems_ && monster_shards_ >= item_type::monster_shard_of_strength.combine_count()
            && jungle_shards_ >= item_type::jungle_shard_of_health.combine_count() && entry.group.gem_value() == 0);
    }
    float damage_range(const cambc::ability& ability) const {
        if (!ability.can_target_camps()) return 0;
        if (ability.is_damage()) return ability.get<cambc::ability_damage>().range;
        if (ability.is_cc() && ability.get<cambc::ability_cc>().modifier > conveyor::epsilon)
            return ability.get<cambc::ability_cc>().range;
        return 0;
    }
    bool isolated_basic(int other, vec2 target) const {
        if (held_group_ < 0 || cambc::self().champion_name() != cambc::champion_type::varg) return true;
        // Public original spawns conservatively exclude splash into even an
        // unseen third group. Dead A and selected B are the only exemptions.
        const auto footprint = around(target,static_cast<int>(cambc::constants::passive_abilities::varg_splash_radius));
        return (footprint & original_spawns_ & ~groups_[other].spawns & ~groups_[held_group_].spawns).is_empty();
    }
    std::optional<camp> ordered_member(int other, bool actionable, bool walked = false) const {
        auto& me = cambc::self(); std::optional<camp> best; float best_score = std::numeric_limits<float>::max(); unsigned best_tile = 0;
        for (const auto& member : cambc::vision::camps()) {
            if (!groups_[other].spawns.is_set(member.spawn_position()) || !member.is_alive()) continue;
            bool legal = me.can_attack() && me.attack_tiles().is_set(member.position()) && isolated_basic(other,member.position());
            if (!legal && !walked && me.can_cast()) for (const auto& ability : me.abilities())
                if (damage_range(ability) > 0 && ability.is_ready() && ability.valid_tiles().is_set(member.position())) { legal = true; break; }
            if (actionable && !legal) continue;
            const float score = member.health() + distance(me.position(), member.position())*100.0f;
            const unsigned index = tile(member.spawn_position());
            if (score < best_score || (score == best_score && (!best || index < best_tile))) { best = member; best_score = score; best_tile = index; }
        }
        return best;
    }
    bool overlap_fight(int other, bool walked, float incoming) {
        auto target = ordered_member(other, true, walked); if (!target || cambc::fuel_remaining() < 16000) return false;
        auto& me = cambc::self();
        float before = 0; for (const auto& member : cambc::vision::camps())
            if (groups_[other].spawns.is_set(member.spawn_position()) && member.is_alive()) before += member.health();
        const unsigned basics_before = overlap_basics_, casts_before = overlap_casts_;
        // This is the ordinary bounded damage/shield/reset sequence with fresh
        // validity checks; no displacement-capable ability is admitted.
        auto valid = [&] { return target->is_visible() && target->health() > 0; };
        if (!walked && me.can_cast()) for (const auto& ability : me.abilities())
            if (damage_range(ability) > 0 && ability.is_ready() && valid() && ability.valid_tiles().is_set(target->position())) {
                if (ability.use(*target)) ++overlap_casts_; break;
            }
        if (valid() && me.attack_tiles().is_set(target->position())) {
            if (incoming > 0 && me.can_cast()) for (const auto& ability : me.abilities())
                if (ability.is_shield() && ability.can_target_self() && ability.is_ready()) { if (ability.use()) ++overlap_casts_; break; }
            if (me.can_attack() && valid() && me.attack_tiles().is_set(target->position()) && isolated_basic(other,target->position())) { me.attack(*target); ++overlap_basics_; }
            if (valid() && !me.can_attack() && me.can_cast() && isolated_basic(other,target->position())) for (const auto& ability : me.abilities())
                if (ability.is_attack_reset() && ability.can_target_self() && ability.is_ready()) {
                    if (ability.use()) {
                        ++overlap_casts_;
                        if (me.can_attack() && valid() && me.attack_tiles().is_set(target->position())) { me.attack(*target); ++overlap_basics_; }
                    }
                    break;
                }
        }
        float after = 0;
        for (const auto& member : cambc::vision::camps())
            if (groups_[other].spawns.is_set(member.spawn_position()) && member.is_alive()) after += member.health();
        const float useful = std::max(0.0f, before-after); overlap_damage_ += useful;
        const bool acted = overlap_basics_ != basics_before || overlap_casts_ != casts_before;
        if (acted) {
            ++overlap_turns_;
            // First useful probe hit is essential; repeated hits are aggregated.
            if (overlap_turns_ == 1 || (profile::probe_mode && useful > 0 && !probe_action_seen_)) {
                event("action_result", "conveyor_action", "\"A\":\"" + group_key(held_group_) + "\",\"B\":\"" + group_key(other)
                    + "\",\"tile\":" + std::to_string(tile(me.position())) + ",\"useful_damage\":" + std::to_string(useful)
                    + ",\"basics\":" + std::to_string(overlap_basics_-basics_before) + ",\"casts\":" + std::to_string(overlap_casts_-casts_before)
                    + ",\"stayed_on_A\":" + boolean(groups_[held_group_].spawns.is_set(me.position())), profile::probe_mode);
                if (profile::probe_mode && useful > 0) probe_action_seen_ = true;
            }
        }
        return acted;
    }
    void hold_and_work(int held, const std::vector<camp>& camps, const std::vector<champion>& enemies) {
        auto& me = cambc::self();
        if(overlap_tile_fixed_ && me.position()!=overlap_tile_) { end_overlap("displacement"); return; }
        for (const auto& enemy : enemies) if (distance(me.position(), enemy.position()) <= 8) {
            end_overlap("enemy");
            if (profile::probe_mode && probe_triggered_ && !probe_a_done_) probe_stop("not_exercised", "enemy_intervened_during_capture");
            return;
        }
        if (cambc::fuel_remaining() < 45000) { ++planning_guards_; return; }
        const auto visible = cambc::vision::visible_tiles(); const auto seen = camp_snapshot(camps);
        if (cambc::fuel_remaining() < 45000) { ++planning_guards_; return; }
        auto local_mask = blocked().reachable_tiles(me.position(), 1) & groups_[held].spawns;
        local_mask.set(me.position());
        const auto local_tiles = local_mask.positions(); // Exactly self plus at most eight adjacent A tiles.
        std::vector<float> health(groups_.size(), 0);
        std::vector<bool> stationary(groups_.size(),false);
        std::vector<std::array<bool,9>> reachable(groups_.size());
        std::vector<std::array<float,9>> sleeping_incoming(groups_.size());
        std::array<float,9> awake_incoming{};
        std::array<bool,9> enemy_near{};
        unsigned origin_index=0;
        for(unsigned j=0;j<local_tiles.size();++j) {
            if(local_tiles[j]==me.position()) origin_index=j;
            for(const auto& enemy:enemies) if(distance(local_tiles[j],enemy.position())<=8) { enemy_near[j]=true; break; }
        }
        const bool can_attack = me.can_attack(); const int attack_range = me.attack_range();
        const float own_health=me.health(), own_maximum=me.max_health();
        const float dps=me.strength()*1.125f+me.focus()*0.12f;
        float spell_range = 0, ready_spell_range = 0;
        for (const auto& ability : me.abilities()) {
            const float range=damage_range(ability); spell_range=std::max(spell_range,range);
            if(ability.is_ready()) ready_spell_range=std::max(ready_spell_range,range);
        }
        for (const auto& member : seen) {
            if(cambc::fuel_remaining()<30000) { ++planning_guards_; return; }
            const bool basic_safe=member.group>=0 && isolated_basic(member.group,member.position);
            if(member.group>=0) {
                health[member.group]+=member.health;
                const int d=distance(me.position(),member.position);
                stationary[member.group]=stationary[member.group] || (can_attack && d<=attack_range && basic_safe) || (ready_spell_range>0 && d<=ready_spell_range);
            }
            for(unsigned j=0;j<local_tiles.size();++j) {
                const int d=distance(local_tiles[j],member.position);
                if(d<=member.range) {
                    if(member.awake) awake_incoming[j]+=member.strength*incoming_multiplier_;
                    else if(member.group>=0) sleeping_incoming[member.group][j]+=member.strength*incoming_multiplier_;
                }
                if(member.group>=0 && ((basic_safe && d<=attack_range) || (spell_range>0 && d<=spell_range))) reachable[member.group][j]=true;
            }
        }
        auto tile_incoming=[&](int i,unsigned j) { return awake_incoming[j]+sleeping_incoming[i][j]; };
        auto safe=[&](int i,unsigned j) { const float incoming=tile_incoming(i,j);
            return !fundamentals::should_retreat(own_health,own_maximum,incoming)
                && fundamentals::affordable_camp(own_health,own_maximum,health[i],incoming,dps*camp_multiplier_); };
        auto eligible = [&](int i) { return i != held && stage_eligible(groups_[i]) && health[i] > 0
            && (groups_[i].spawns & ~visible).is_empty()
            && [&] { for(unsigned j=0;j<local_tiles.size();++j) if(reachable[i][j] && !enemy_near[j] && safe(i,j)) return true; return false; }(); };
        if (overlap_group_ >= 0 && (!eligible(overlap_group_) || !safe(overlap_group_,origin_index))) {
            const int other=overlap_group_;
            const char* reason=!(groups_[other].spawns & ~visible).is_empty() ? "vision"
                : health[other]<=0 ? "no_living_B" : !stage_eligible(groups_[other]) ? "eligibility" : "safety";
            end_overlap(reason); return;
        }
        if (overlap_group_ < 0) {
            int selected = -1; float best_score = -1; bool best_stationary = false;
            for (unsigned i = 0; i < groups_.size(); ++i) {
                if (cambc::fuel_remaining() < 45000) { ++planning_guards_; return; }
                if (!eligible(i)) continue;
                const float value = groups_[i].group.gem_value()*2.0f + groups_[i].group.gold_value()*0.4f;
                const float score = value/(health[i]/std::max(1.0f,dps)+10);
                const bool stationary_safe=stationary[i] && safe(i,origin_index);
                if (selected < 0 || (stationary_safe && !best_stationary)
                    || (stationary_safe == best_stationary && score > best_score+conveyor::epsilon)) {
                    selected = i; best_score = score; best_stationary = stationary_safe;
                } // Numeric group order breaks epsilon-equivalent ties.
            }
            if (selected < 0) return;
            overlap_group_ = selected;
            event("activation", "conveyor_begin", "\"A\":\"" + group_key(held) + "\",\"B\":\"" + group_key(selected)
                + "\",\"holding_tile\":" + std::to_string(tile(me.position())) + ",\"all_visible\":true,\"predicted_incoming\":"
                + std::to_string(incoming_at(selected, me.position(), seen)), profile::probe_mode);
        }
        const int other = overlap_group_;
        if (safe(other,origin_index) && overlap_fight(other, false,tile_incoming(other,origin_index))) return;
        // Retain a selected tile until B invalidates or A resolves. Never
        // oscillate between equivalent legal tiles, even on cooldown turns.
        if (overlap_tile_fixed_ || !me.can_move()) return;
        vec2 best = me.position(); bool found = false; float best_damage = -1, best_incoming = 0; int best_distance = 0;
        const float basic_damage = me.strength()*camp_multiplier_;
        const bool splash = me.champion_name() == cambc::champion_type::varg;
        for (unsigned j=0;j<local_tiles.size();++j) {
            const auto p=local_tiles[j];
            if (cambc::fuel_remaining()<30000) { ++planning_guards_; return; }
            if(enemy_near[j] || !safe(other,j)) continue;
            const float incoming=tile_incoming(other,j);
            float damage = 0; bool reachable = false; int near = 10000;
            const SeenCamp* basic_target = nullptr; float basic_target_score = std::numeric_limits<float>::max();
            for (const auto& member : seen) if (member.group == other) {
                const int d = distance(p,member.position); near = std::min(near,d);
                if (d <= attack_range && isolated_basic(other,member.position)) {
                    reachable = true; const float score=member.health+d*100.0f;
                    if (!basic_target || score < basic_target_score || (score == basic_target_score && tile(member.position)<tile(basic_target->position))) {
                        basic_target=&member; basic_target_score=score;
                    }
                }
                if (spell_range>0 && d<=spell_range) reachable=true;
            }
            if (basic_target) for (const auto& member : seen) if (member.group==other
                && (&member==basic_target || (splash && distance(member.position,basic_target->position)<=cambc::constants::passive_abilities::varg_splash_radius)))
                damage+=std::min(member.health,basic_damage);
            if (!reachable) continue;
            if (!found || damage > best_damage+conveyor::epsilon || (std::abs(damage-best_damage)<=conveyor::epsilon && (incoming < best_incoming-conveyor::epsilon
                || (std::abs(incoming-best_incoming)<=conveyor::epsilon && (near < best_distance || (near == best_distance && tile(p) < tile(best))))))) {
                found=true; best=p; best_damage=damage; best_incoming=incoming; best_distance=near;
            }
        }
        if (!found) return;
        overlap_tile_ = best;
        if (best == me.position()) { overlap_tile_fixed_ = true; return; }
        // A hidden occupant can reject the only selected move without spending
        // movement; refresh next turn instead of adding an unbounded planner.
        if (me.can_move() && !blocked().is_set(best) && me.move_absolute(best)) {
            ++overlap_moves_; overlap_tile_fixed_ = true;
            if (!groups_[held].spawns.is_set(me.position())) { end_overlap("displacement"); return; }
            const auto fresh = cambc::vision::visible_tiles();
            for(const auto& enemy:cambc::vision::champions(team::enemy)) if(distance(me.position(),enemy.position())<=8) { end_overlap("enemy_after_move"); return; }
            if (!(groups_[other].spawns & ~fresh).is_empty()) { end_overlap("vision_after_move"); return; }
            if(cambc::fuel_remaining()<30000) return;
            const auto fresh_seen=camp_snapshot(cambc::vision::camps());
            if(cambc::fuel_remaining()<30000) return;
            const float fresh_incoming=incoming_at(other,me.position(),fresh_seen);
            if(!affordable_overlap(health[other],fresh_incoming)) { end_overlap("safety_after_move"); return; }
            overlap_fight(other,true,fresh_incoming);
        } else ++blocked_retries_;
    }

    unsigned fixture_tile(unsigned index) const {
        if (!profile::probe_mirror_with_home) return index;
        const auto own = cambc::map::spawn_region(team::ally).positions(), enemy = cambc::map::spawn_region(team::enemy).positions();
        if (own.empty() || enemy.empty() || tile(own.front()) < tile(enemy.front())) return index;
        bitboard point; point.set({static_cast<int>(index%cambc::map::width()),static_cast<int>(index/cambc::map::width())});
        return tile(point.reflect(cambc::map::symmetry()).positions().front());
    }
    bool select_probe_fixture() {
        const unsigned a_tile = fixture_tile(profile::probe_group_key), b_tile = fixture_tile(profile::probe_other_group_key);
        for (unsigned i=0;i<groups_.size();++i) {
            if (groups_[i].spawns.is_set({static_cast<int>(a_tile%cambc::map::width()),static_cast<int>(a_tile/cambc::map::width())})) probe_group_=i;
            if (groups_[i].spawns.is_set({static_cast<int>(b_tile%cambc::map::width()),static_cast<int>(b_tile/cambc::map::width())})) probe_other_=i;
        }
        if (probe_group_<0 || probe_other_<0 || probe_group_==probe_other_) { probe_stop("not_exercised","declared_groups_missing"); return false; }
        const unsigned stage=fixture_tile(profile::probe_tile_index);
        probe_stage_={static_cast<int>(stage%cambc::map::width()),static_cast<int>(stage/cambc::map::width())};
        if (!groups_[probe_group_].spawns.is_set(probe_stage_) || terrain_.is_set(probe_stage_)) { probe_stop("not_exercised","holding_tile_invalid"); return false; }
        probe_selected_=true;
        event("observation","conveyor_fixture","\"A\":\""+group_key(probe_group_)+"\",\"B\":\""+group_key(probe_other_)
            +"\",\"holding_tile\":"+std::to_string(stage),true);
        return true;
    }
    bool probe_tick(const std::vector<camp>& camps, const std::vector<champion>& enemies) {
        if (cambc::fuel_remaining()<45000) return true;
        if (!probe_selected_ && !select_probe_fixture()) return true;
        auto& me=cambc::self();
        if (probe_a_done_) {
            // The competitive hook gives B no claim. This explicitly declared
            // diagnostic continuation only witnesses the second conversion.
            auto& b=groups_[probe_other_]; const auto visible=cambc::vision::visible_tiles();
            if (!(b.spawns&visible).is_empty()) {
                const auto state=b.group.state();
                if (std::holds_alternative<cambc::map::camp_group_reward_available>(state)) {
                    if (b.spawns.is_set(me.position())) { probe_b_holding_=true; return true; }
                    move_to(b.spawns); return true;
                }
                if (std::holds_alternative<cambc::map::camp_group_respawning>(state)) {
                    if (probe_b_holding_ && b.spawns.is_set(me.position())) {
                        event("observation","conveyor_B_capture","\"B\":\""+group_key(probe_other_)+"\",\"A_complete_round\":"
                            +std::to_string(probe_a_done_round_)+",\"B_complete_round\":"+std::to_string(cambc::round_number()),true);
                        probe_finished_=true;
                    } else probe_stop("not_exercised","B_reward_not_attributed");
                    return true;
                }
            }
            target_=probe_other_; auto target=ordered_member(probe_other_,false);
            if (target && fight(*target,false,false)) return true;
            move_to(target ? around(target->position(),me.attack_range()) : around(b.anchor,1)); return true;
        }
        auto& a=groups_[probe_group_]; const auto visible=cambc::vision::visible_tiles();
        if (!(a.spawns&visible).is_empty()) {
            const auto state=a.group.state();
            if (std::holds_alternative<cambc::map::camp_group_reward_available>(state)) {
                if (me.position()!=probe_stage_) {
                    if (move_to(around(probe_stage_,0)) && me.position()==probe_stage_) {
                        probe_entry_round_=cambc::round_number();
                        event("action_result","conveyor_entry","\"A\":\""+group_key(probe_group_)+"\",\"tile\":"+std::to_string(tile(me.position()))
                            +",\"entry_round\":"+std::to_string(probe_entry_round_),true);
                    }
                    return true;
                }
                if (!probe_triggered_) {
                    const auto& b=groups_[probe_other_];
                    if (!(b.spawns&~visible).is_empty()) { probe_stop("not_exercised","B_not_fully_visible"); return true; }
                    const auto bstate=b.group.state();
                    if (!std::holds_alternative<cambc::map::camp_group_alive>(bstate)) { probe_stop("not_exercised","B_not_alive"); return true; }
                    for (const auto& enemy:enemies) if(distance(me.position(),enemy.position())<=8) { probe_stop("not_exercised","enemy_at_trigger"); return true; }
                    for (const auto& member:camps) if(b.spawns.is_set(member.spawn_position()) && member.is_alive()) {
                        if(member.strength()>0) { probe_stop("not_exercised","B_not_harmless"); return true; }
                        probe_b_initial_health_+=member.health();
                    }
                    if (!ordered_member(probe_other_,true) || !stage_eligible(b) || probe_b_initial_health_<=0) {
                        probe_stop("not_exercised","B_not_legal_eligible_reachable"); return true;
                    }
                    probe_triggered_=true; begin_hold(probe_group_);
                    event("opportunity","conveyor_overlap","\"A\":\""+group_key(probe_group_)+"\",\"B\":\""+group_key(probe_other_)
                        +"\",\"tile\":"+std::to_string(tile(me.position()))+",\"B_health\":"+std::to_string(probe_b_initial_health_)
                        +",\"entry_round\":"+std::to_string(probe_entry_round_)+",\"gear_strength\":"+std::to_string(me.strength())
                        +",\"gear_focus\":"+std::to_string(me.focus())+",\"health\":"+std::to_string(me.health())+",\"max_health\":"+std::to_string(me.max_health()),true);
                }
                return false; // Release the identical occupied-capture hook.
            }
            if (std::holds_alternative<cambc::map::camp_group_respawning>(state)) { probe_stop("not_exercised","A_reward_claimed_before_entry"); return true; }
        }
        target_=probe_group_; partial_clear_=false;
        auto target=ordered_member(probe_group_,false);
        if(target && fight(*target,false,true)) return true;
        if(move_to(target ? around(target->position(),me.attack_range()) : around(a.anchor,1)))
            for(const auto& member:cambc::vision::camps()) if(a.spawns.is_set(member.spawn_position()) && member.is_alive()
                && me.attack_tiles().is_set(member.position())) { fight(member,true,true); break; }
        return true;
    }

public:
    void turn() {
        struct FuelSample {
            unsigned& low_water;
            ~FuelSample() { low_water = std::min(low_water, static_cast<unsigned>(cambc::fuel_remaining())); }
        } fuel_sample{fuel_low_water_};
        auto& me = cambc::self();
        fields_this_turn_ = 0;
        if (!initialized_) initialize();
        telemetry_tick();
        audit_held_capture();
        if (profile::probe_mode) {
            if (me.champion_name() != cambc::champion_type::tavi || probe_finished_) return;
            if (!probe_start_) probe_start_ = cambc::round_number();
            if (!probe_triggered_ && cambc::round_number()-probe_start_ >= profile::max_setup_rounds) {
                probe_stop("not_exercised", "legal_setup_timeout"); return;
            }
            if (cambc::round_number() >= profile::probe_round_limit) {
                probe_stop("not_exercised", probe_witness_ ? "b_conversion_censored" : "capture_witness_timeout"); return;
            }
        }
        // Dead champions retain shopping turns, but have no usable position.
        if (!me.is_alive()) {
            shop(); recovering_ = false; target_ = -1; state_ = State::farm; last_reason_ = conveyor::BankReason::none;
            if (profile::probe_mode) probe_stop("not_exercised", "measurement_actor_died_during_setup");
            return;
        }
        if (me.is_recalling() || me.is_submitting_gems() || me.is_teleporting()) {
            state_ = me.is_submitting_gems() ? State::channel : State::recover;
            return;
        }
        if (!me.can_move() && !me.can_attack() && !me.can_cast()) return;
        shop();
        const bool spawn = cambc::map::spawn_region(team::ally).is_set(me.position());
        if (spawn) {
            recovering_ = false;
            // Recovery happens at turn end; leaving on recall arrival prevents it.
            if (me.health() < me.max_health()) return;
        }
        const auto camps = cambc::vision::camps();
        const auto enemies = cambc::vision::champions(team::enemy);
        if (cambc::fuel_remaining() < 30000) { ++planning_guards_; return; }
        float incoming = 0;
        for (const auto& monster : camps)
            if (monster.is_alive() && monster.is_awake()
                && distance(me.position(), monster.position()) <= monster.attack_range()) incoming += monster.strength();
        for (const auto& enemy : enemies)
            if (distance(me.position(), enemy.position()) <= enemy.attack_range() + 1) incoming += enemy.strength();
        // Partial clears reserve roughly four incoming camp phases for escape;
        // ordinary safe fights retain the less conservative baseline threshold.
        if (!spawn && fundamentals::should_retreat(me.health(), me.max_health(),
            incoming * (partial_clear_ ? 2.0f : 1.0f))) recovering_ = true;
        if (recovering_) {
            end_overlap("recovery");
            if (profile::probe_mode && probe_triggered_ && !probe_a_done_) probe_stop("mechanism_failed", "recovery_during_measured_capture");
            state_ = State::recover; recover(camps, enemies); return;
        }
        if (profile::probe_mode && probe_tick(camps, enemies)) return;
        // Cleared camp ownership takes precedence over banking old gems.
        const int occupied_group = group_at_tile_[tile(me.position())];
        if (occupied_group >= 0) {
            auto& entry = groups_[occupied_group];
            if (entry.spawns.is_set(me.position()) && !(entry.spawns & cambc::vision::visible_tiles()).is_empty()
                && std::holds_alternative<cambc::map::camp_group_reward_available>(entry.group.state())) {
                target_ = occupied_group;
                begin_hold(occupied_group);
                if (profile::core_enabled) hold_and_work(occupied_group, camps, enemies);
                return;
            }
        }
        if (bank()) return;
        // Defend against an adjacent threat, without chasing opponents.
        for (const auto& enemy : enemies)
            if (distance(me.position(), enemy.position()) <= 1 && fight(enemy, false, true)) return;
        if (!spawn && me.champion_name() != cambc::champion_type::tavi && me.gold_count() >= 300) {
            recovering_ = true; state_ = State::recover;
            recover(camps, enemies);
            return;
        }
        farm(camps, cambc::vision::champions(team::ally));
    }
};
} // namespace

int main() {
    Bot bot;
    while (true) {
        try {
            if (cambc::is_draft()) draft();
            else bot.turn();
        } catch (const std::exception& error) {
            std::cout << "bot error: " << error.what() << '\n';
        } catch (...) {
            std::cout << "bot error: unknown exception\n";
        }
        cambc::end_turn();
    }
}
