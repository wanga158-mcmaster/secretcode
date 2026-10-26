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
#include "policy.h"
#include "profile.h"

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
    std::vector<camp> ordered_members{};
};

class Bot {
    std::vector<Group> groups_;
    std::vector<int> group_at_tile_;
    std::vector<vec2> altars_;
    bitboard terrain_;
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
    banking::BankReason last_reason_ = banking::BankReason::none;
    bool tribute_active_ = false;
    unsigned tribute_bag_ = 0, tribute_round_ = 0, tribute_score_ = 0;
    unsigned tribute_starts_ = 0, tribute_completions_ = 0, tribute_interruptions_ = 0;
    struct Sequence {
        unsigned sequence = 0, basics = 0, casts = 0;
        unsigned first = 0, second = 0;
        bool first_killed = false, second_killed = false, useful = false;
        int before_first = -1, before_second = -1, after_kill = -1, before_cast = -1;
        unsigned useful_kill_round = 0;
    } trace_;
    struct Refresh { unsigned round; int cooldown; };
    std::optional<Refresh> pending_refresh_, consumed_refresh_;
    unsigned kills_ = 0, cooling_refreshes_ = 0, ready_refreshes_ = 0, useful_refreshes_ = 0;
    unsigned domino_opportunities_ = 0, domino_inactive_ = 0, candidate_scans_ = 0;
    unsigned probe_start_ = 0, probe_trigger_round_ = 0, probe_phase_ = 0;
    unsigned probe_preparation_hits_ = 0;
    int probe_group_ = -1;
    bool probe_released_ = false, probe_finished_ = false, probe_selected_ = false;
    vec2 probe_stage_{};
    unsigned probe_lethal_ = 0, probe_survivor_ = 0, probe_removed_ = 0;
    float probe_damage_ = 0;
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
        event(kind, "domino_probe", "\"reason\":\"" + std::string(reason) + "\"", true);
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
                + ",\"kills\":" + std::to_string(kills_) + ",\"cooling_refreshes\":" + std::to_string(cooling_refreshes_)
                + ",\"ready_refreshes\":" + std::to_string(ready_refreshes_) + ",\"useful_refreshes\":" + std::to_string(useful_refreshes_)
                + ",\"opportunities\":" + std::to_string(domino_opportunities_) + ",\"inactive\":" + std::to_string(domino_inactive_)
                + ",\"planning_guards\":" + std::to_string(planning_guards_) + ",\"fuel_low_water\":" + std::to_string(fuel_low_water_));
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
            entry.ordered_members.assign(group.camps().begin(), group.camps().end());
            std::sort(entry.ordered_members.begin(), entry.ordered_members.end(), [](const camp& a, const camp& b) {
                return tile(a.spawn_position()) < tile(b.spawn_position());
            });
            has_non_monolith_gems_ |= !entry.monolith && entry.group.gem_value() > 0;
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

    unsigned item_progress(item_type shard) const {
        const auto inventory = cambc::self().item_count();
        const auto emblem = shard.result();
        // Only this declared item stage is bought; existing emblems represent
        // their consumed shards. Do not infer a passive from stage counters.
        return emblem && inventory[*emblem] ? shard.combine_count() : inventory[shard];
    }
    void refresh_equipment() {
        const auto inventory = cambc::self().item_count();
        monster_shards_ = item_progress(item_type::monster_shard_of_strength);
        jungle_shards_ = item_progress(item_type::jungle_shard_of_health);
        camp_multiplier_ = cambc::self().base_stats().camp_damage_multiplier;
        incoming_multiplier_ = 1;
        if (inventory[item_type::monster_emblem_of_strength])
            camp_multiplier_ *= 1 + item_type::monster_emblem_of_strength.info().passive_magnitude;
        if (inventory[item_type::jungle_emblem_of_health])
            incoming_multiplier_ *= 1 - item_type::jungle_emblem_of_health.info().passive_magnitude;
    }
    void shop() {
        auto& me = cambc::self();
        refresh_equipment();
        if (profile::probe_mode) return; // Fixed no-equipment diagnostic, identical in both arms.
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
                if ((ability.is_damage() || ability.is_cc()) && ability.is_ready()
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
                if (ability.is_shield() && ability.is_ready() && ability.can_target_self()) { acted |= ability.use(); break; }
        if (me.can_attack()) {
            if constexpr (std::is_same_v<Target, camp>) recorded_basic(target);
            else me.attack(target);
            acted = true;
        }
        if (!valid()) return acted;
        // Includes ranged champions that walked this turn: a reset explicitly
        // restores their attack allowance before the extra attack.
        if (!me.can_attack())
            for (const auto& ability : abilities)
                if (ability.is_attack_reset() && ability.is_ready() && ability.can_target_self()) {
                    const int before = ability.cooldown();
                    if (ability.use()) {
                        if constexpr (std::is_same_v<Target, camp>) recorded_reset(before);
                        else if (me.champion_name() == cambc::champion_type::vendetta) {
                            // An enemy-defense reset also consumes the slot;
                            // never later attribute it to a stale camp refresh.
                            pending_refresh_.reset(); consumed_refresh_.reset();
                        }
                        if (me.can_attack() && valid()) {
                            if constexpr (std::is_same_v<Target, camp>) recorded_basic(target);
                            else me.attack(target);
                        }
                    }
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
        const unsigned threshold = 24;
        const auto reason = altars_.empty() ? banking::BankReason::none : banking::bank_reason(
            me.gem_count(), cambc::round_number(), cambc::constants::max_rounds, nearest, threshold);
        const bool requested = reason != banking::BankReason::none;
        if (!requested) { state_ = State::farm; last_reason_ = reason; return false; }
        ++bank_turns_;
        if (reason == banking::BankReason::threshold && nearest > 4
            && !banking::late_round(cambc::round_number(), cambc::constants::max_rounds)) ++threshold_only_turns_;
        if (state_ != State::bank_requested || last_reason_ != reason) {
            ++journeys_;
            event("activation", "bank", "\"bag\":" + std::to_string(me.gem_count())
                + ",\"threshold\":" + std::to_string(threshold) + ",\"nearest\":" + std::to_string(nearest)
                + ",\"reason\":\"" + banking::reason_name(reason) + "\"", profile::probe_mode);
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
                        if (profile::probe_mode) probe_stop("not_exercised", "enemy_lock_prevents_conversion_fixture");
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
                if (!target || score < weakest || (score == weakest && tile(monster.spawn_position()) < tile(target->spawn_position()))) {
                    weakest = score; target = monster;
                }
            }
        const bool domino_actor = me.champion_name() == cambc::champion_type::vendetta && profile::core_enabled;
        if (domino_actor && domino_fight(selected)) return;
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
                    if (!domino_actor || !domino_fight(selected)) fight(monster, true, entry.incoming > 0);
                    break;
                }
        } else if (me.can_move()) { entry.unavailable_until = round + 4; ++navigation_failures_; }
    }

    struct Candidates {
        std::optional<camp> ordinary, lethal, nonlethal;
        float ordinary_score = std::numeric_limits<float>::max();
        float lethal_score = std::numeric_limits<float>::max();
        float nonlethal_score = std::numeric_limits<float>::max();
        bool guarded = false;
    };
    bool legal_basic(const camp& member) const {
        auto& me = cambc::self();
        return member.is_visible() && member.is_alive()
            && me.attack_tiles().is_set(member.position());
    }
    bool confirmed_dead(const camp& member) const {
        // A fresh state query on its personally visible original tile confirms
        // death. Disappearance from vision is never treated as a kill.
        return cambc::vision::visible_tiles().is_set(member.spawn_position()) && member.is_dead();
    }
    Candidates domino_candidates(int group) {
        Candidates result;
        ++candidate_scans_;
        auto& me = cambc::self();
        const float damage = me.strength()*camp_multiplier_;
        unsigned inspected = 0;
        for (const auto& member : groups_[group].ordered_members) {
            if (inspected++ >= 32) break;
            if (cambc::fuel_remaining() < 16000) { result.guarded = true; ++planning_guards_; break; }
            if (!legal_basic(member)) continue;
            const float health = member.health();
            const float score = health + distance(me.position(), member.position())*(partial_clear_ ? 1000.0f : 100.0f);
            const auto keep = [&](std::optional<camp>& best, float& best_score) {
                if (!best || domino::precedes(score, tile(member.spawn_position()), best_score, tile(best->spawn_position()))) {
                    best = member; best_score = score;
                }
            };
            keep(result.ordinary, result.ordinary_score);
            if (domino::lethal(health, damage)) keep(result.lethal, result.lethal_score);
            else keep(result.nonlethal, result.nonlethal_score);
        }
        return result;
    }
    void recorded_reset(int cooldown_before) {
        if (cambc::self().champion_name() != cambc::champion_type::vendetta) return;
        ++trace_.casts;
        trace_.before_cast = cooldown_before;
        if (pending_refresh_ && domino::refresh_still_useful(pending_refresh_->round, pending_refresh_->cooldown, cambc::round_number()))
            consumed_refresh_ = pending_refresh_;
        else consumed_refresh_.reset();
        pending_refresh_.reset();
    }
    void recorded_basic(const camp& member) {
        auto& me = cambc::self();
        if (me.champion_name() != cambc::champion_type::vendetta) { me.attack(member); return; }
        const auto reset = me.abilities()[0];
        const int before = reset.cooldown();
        const unsigned member_tile = tile(member.spawn_position());
        me.attack(member); // SDK attack returns void; a completed call is success.
        const bool killed = confirmed_dead(member);
        const int after = reset.cooldown();
        ++trace_.basics;
        if (trace_.basics == 1) { trace_.first = member_tile; trace_.first_killed = killed; trace_.before_first = before; }
        else { trace_.second = member_tile; trace_.second_killed = killed; trace_.before_second = before; }
        if (killed) {
            ++kills_; trace_.after_kill = after;
            if (before > 0 && after == 0) {
                ++cooling_refreshes_;
                pending_refresh_ = Refresh{cambc::round_number(), before};
            } else if (before == 0 && after == 0) ++ready_refreshes_;
        }
        if (consumed_refresh_) {
            ++useful_refreshes_; trace_.useful = true; trace_.useful_kill_round = consumed_refresh_->round;
            event("observation", "domino_refresh_use", "\"kill_round\":" + std::to_string(consumed_refresh_->round)
                + ",\"productive_round\":" + std::to_string(cambc::round_number()) + ",\"member\":" + std::to_string(member_tile)
                + ",\"replaced_cooldown\":" + std::to_string(consumed_refresh_->cooldown), profile::probe_mode && probe_released_);
            consumed_refresh_.reset();
        }
    }
    void log_sequence() {
        event("action_result", "domino_sequence", "\"case\":" + std::to_string(trace_.sequence)
            + ",\"member0\":" + std::to_string(trace_.first) + ",\"member1\":" + std::to_string(trace_.second)
            + ",\"basics\":" + std::to_string(trace_.basics) + ",\"casts\":" + std::to_string(trace_.casts)
            + ",\"before_first\":" + std::to_string(trace_.before_first) + ",\"before_cast\":" + std::to_string(trace_.before_cast)
            + ",\"before_second\":" + std::to_string(trace_.before_second) + ",\"after_kill\":" + std::to_string(trace_.after_kill)
            + ",\"can_cast_after\":" + boolean(cambc::self().can_cast()), profile::probe_mode && probe_released_);
    }
    bool domino_fight(int group) {
        auto& me = cambc::self();
        if (cambc::fuel_remaining() < 16000) { ++planning_guards_; return true; }
        auto candidates = domino_candidates(group);
        if (candidates.guarded) return true;
        if (!me.can_attack() || !candidates.ordinary) { ++domino_inactive_; return false; }
        const auto reset = me.abilities()[0];
        trace_.sequence = domino::sequence(me.can_attack(), reset.cooldown(), reset.is_ready(),
            candidates.lethal.has_value(), candidates.nonlethal.has_value());
        ++domino_opportunities_;
        auto first = trace_.sequence == 1 ? candidates.lethal : trace_.sequence == 2 ? candidates.nonlethal : candidates.ordinary;
        // Case2's finisher is the distinct member that was already lethal.
        // The first hit may make its own target newly lethal and weaker; do
        // not let a post-hit minimum steal the promised original finisher.
        const auto planned_finisher = candidates.lethal;
        // Refresh immediately before the action; never soften a newly lethal
        // member merely to retain a stale case-2 order.
        if (!first || !legal_basic(*first) || !me.can_attack()) return true;
        if (trace_.sequence == 2 && domino::lethal(first->health(), me.strength()*camp_multiplier_)) {
            candidates = domino_candidates(group);
            if (candidates.guarded || !candidates.ordinary) return true;
            trace_.sequence = 3; first = candidates.ordinary;
        }
        recorded_basic(*first);
        if (cambc::fuel_remaining() < 16000) { ++planning_guards_; log_sequence(); return true; }
        // Case 1 only spends the reset after the expected kill is actually
        // confirmed. No target, allowance or reset state is reused from above.
        if (trace_.sequence == 1 && !trace_.first_killed) { log_sequence(); return true; }
        candidates = domino_candidates(group);
        if (candidates.guarded) { log_sequence(); return true; }
        auto second = trace_.sequence == 2 ? planned_finisher : candidates.ordinary;
        const int before_cast = reset.cooldown();
        if (second && legal_basic(*second)
            && (trace_.sequence != 2 || domino::lethal(second->health(), me.strength()*camp_multiplier_))
            && reset.is_attack_reset() && reset.can_target_self()
            && reset.is_ready() && me.can_cast() && trace_.casts == 0 && trace_.basics < 2) {
            if (reset.use()) {
                recorded_reset(before_cast);
                if (cambc::fuel_remaining() < 16000) { ++planning_guards_; log_sequence(); return true; }
                candidates = domino_candidates(group);
                if (!candidates.guarded) {
                    second = trace_.sequence == 2 ? planned_finisher : candidates.ordinary;
                    if (second && legal_basic(*second) && me.can_attack() && trace_.basics < 2
                        && (trace_.sequence != 2 || domino::lethal(second->health(), me.strength()*camp_multiplier_)))
                        recorded_basic(*second);
                }
            }
        }
        log_sequence();
        return true;
    }

    bool mirrored_probe_home() const {
        if (!profile::probe_mirror_with_home) return false;
        const auto own = cambc::map::spawn_region(team::ally).positions();
        const auto other = cambc::map::spawn_region(team::enemy).positions();
        return !own.empty() && !other.empty() && tile(own.front()) > tile(other.front());
    }
    unsigned probe_fixture_tile(unsigned index) const {
        if (!mirrored_probe_home()) return index;
        bitboard point;
        point.set({static_cast<int>(index % cambc::map::width()), static_cast<int>(index / cambc::map::width())});
        return tile(point.reflect(cambc::map::symmetry()).positions().front());
    }
    std::optional<camp> probe_member(unsigned index) const {
        for (const auto& member : groups_[probe_group_].ordered_members)
            if (tile(member.spawn_position()) == index) return member;
        return std::nullopt;
    }
    bool select_probe_fixture() {
        if (cambc::map::width() != 50 || cambc::map::height() != 50) {
            probe_stop("not_exercised", "declared_rows_dimensions_missing"); return false;
        }
        probe_lethal_ = probe_fixture_tile(profile::probe_member_key);
        probe_survivor_ = probe_fixture_tile(profile::probe_survivor_key);
        probe_removed_ = probe_fixture_tile(profile::probe_remove_key);
        const unsigned stage = probe_fixture_tile(profile::probe_tile_index);
        probe_stage_ = {static_cast<int>(stage % cambc::map::width()), static_cast<int>(stage / cambc::map::width())};
        for (unsigned i = 0; i < groups_.size(); ++i)
            if (groups_[i].spawns.is_set({static_cast<int>(probe_lethal_%cambc::map::width()), static_cast<int>(probe_lethal_/cambc::map::width())})) {
                probe_group_ = static_cast<int>(i); break;
            }
        if (probe_group_ < 0 || groups_[probe_group_].ordered_members.size() != 3
            || !probe_member(probe_survivor_) || !probe_member(probe_removed_) || terrain_.is_set(probe_stage_)) {
            probe_stop("not_exercised", "declared_three_cluster_fixture_missing"); return false;
        }
        for (const auto& member : groups_[probe_group_].ordered_members)
            if (!member.is_gold_cluster() || distance(probe_stage_, member.spawn_position()) > 1) {
                probe_stop("not_exercised", "fixture_not_harmless_shared_melee_geometry"); return false;
            }
        probe_selected_ = true; target_ = probe_group_; partial_clear_ = false;
        event("observation", "domino_fixture", "\"scenario\":\"" + std::string(profile::scenario_id)
            + "\",\"group\":\"g_" + std::to_string(groups_[probe_group_].key) + "\",\"stage\":" + std::to_string(stage)
            + ",\"lethal_member\":" + std::to_string(probe_lethal_) + ",\"survivor\":" + std::to_string(probe_survivor_)
            + ",\"remove\":" + std::to_string(probe_removed_), true);
        return true;
    }
    bool probe_tick(const std::vector<camp>&, const std::vector<champion>& enemies) {
        auto& me = cambc::self();
        if (!probe_selected_ && !select_probe_fixture()) return true;
        if (cambc::fuel_remaining() < 45000) { ++planning_guards_; return true; }
        for (const auto& enemy : enemies) if (distance(me.position(), enemy.position()) <= 8) {
            probe_stop("not_exercised", "enemy_present_in_diagnostic"); return true;
        }
        if (me.position() != probe_stage_) {
            if (probe_released_) { probe_stop("mechanism_failed", "measurement_position_changed"); return true; }
            move_to(around(probe_stage_, 0)); return true;
        }
        if (!(groups_[probe_group_].spawns & ~cambc::vision::visible_tiles()).is_empty()) {
            probe_stop("not_exercised", "whole_group_not_personally_visible"); return true;
        }
        const auto lethal = *probe_member(probe_lethal_);
        const auto survivor = *probe_member(probe_survivor_);
        const auto removed = *probe_member(probe_removed_);
        const float damage = me.strength()*camp_multiplier_;
        const auto reset = me.abilities()[0];
        if (!probe_released_) {
            // Basic-only preparation, with no resets or shopping in either arm.
            // Removing the third member leaves exactly the pair the contract
            // requires. The harmless group cannot damage the measurement actor.
            if (removed.is_alive()) {
                if (me.can_attack() && legal_basic(removed)) {
                    recorded_basic(removed); ++probe_preparation_hits_;
                }
                return true;
            }
            if (!lethal.is_alive() || !survivor.is_alive()) {
                probe_stop("not_exercised", "pair_died_during_preparation"); return true;
            }
            if (!domino::lethal(lethal.health(), damage)) {
                if (me.can_attack() && legal_basic(lethal)) {
                    recorded_basic(lethal); ++probe_preparation_hits_;
                }
                return true;
            }
            if (!me.can_attack()) return true;
            if (!legal_basic(lethal) || !legal_basic(survivor) || !reset.is_ready() || !me.can_cast()
                || !reset.is_attack_reset() || !reset.can_target_self() || reset.cooldown() != 0
                || survivor.health() <= 2*damage+domino::epsilon || damage <= 0
                || me.health() != me.max_health() || camp_multiplier_ != me.base_stats().camp_damage_multiplier) {
                probe_stop("not_exercised", "prepared_pair_readiness_or_later_hit_predicate_missing"); return true;
            }
            for (const auto item : std::array{item_type::monster_shard_of_strength, item_type::monster_emblem_of_strength,
                item_type::jungle_shard_of_health, item_type::jungle_emblem_of_health, item_type::power_shard_of_strength,
                item_type::power_emblem_of_strength})
                if (me.item_count()[item]) { probe_stop("not_exercised", "fixed_empty_equipment_changed"); return true; }
            probe_released_ = true; probe_trigger_round_ = cambc::round_number(); probe_damage_ = damage;
            pending_refresh_.reset(); consumed_refresh_.reset(); trace_ = {};
            event("opportunity", "domino_prepared_pair", "\"group\":\"g_" + std::to_string(groups_[probe_group_].key)
                + "\",\"lethal\":" + std::to_string(probe_lethal_) + ",\"H_lethal\":" + std::to_string(lethal.health())
                + ",\"survivor\":" + std::to_string(probe_survivor_) + ",\"H_survivor\":" + std::to_string(survivor.health())
                + ",\"basic_damage\":" + std::to_string(damage) + ",\"setup_basics\":" + std::to_string(probe_preparation_hits_)
                + ",\"cooldown\":0,\"basic_unused\":true,\"empty_equipment\":true", true);
            if (profile::core_enabled) domino_fight(probe_group_);
            else { fight(lethal, false, false); log_sequence(); }
            const bool enabled_ok = trace_.sequence == 2 && trace_.first == probe_survivor_ && trace_.second == probe_lethal_
                && trace_.basics == 2 && trace_.casts == 1 && !trace_.first_killed && trace_.second_killed
                && trace_.before_second > 0 && trace_.after_kill == 0 && !me.can_cast() && reset.cooldown() == 0;
            const bool disabled_ok = trace_.first == probe_lethal_ && trace_.basics == 1 && trace_.casts == 0
                && trace_.first_killed && trace_.before_first == 0 && me.can_cast() && reset.cooldown() == 0;
            if (!(profile::core_enabled ? enabled_ok : disabled_ok))
                probe_stop("mechanism_failed", "prepared_turn_order_or_refresh_mismatch");
            else { probe_phase_ = 1; }
            return true;
        }
        if (probe_phase_ == 1) {
            if (cambc::round_number() != probe_trigger_round_+1) {
                probe_stop("not_exercised", "next_owner_turn_missing"); return true;
            }
            if (!legal_basic(survivor) || !me.can_attack() || !me.can_cast() || !reset.is_ready()
                || reset.cooldown() != 0 || damage != probe_damage_) {
                probe_stop("mechanism_failed", "refreshed_slot_or_living_followup_target_missing"); return true;
            }
            if (profile::core_enabled) domino_fight(probe_group_);
            else { fight(survivor, false, false); log_sequence(); }
            const bool common = trace_.basics == 2 && trace_.casts == 1 && trace_.first == probe_survivor_
                && trace_.second == probe_survivor_ && !me.can_cast();
            const bool causal = trace_.useful && trace_.useful_kill_round == probe_trigger_round_;
            if (!common || (profile::core_enabled && !causal) || (!profile::core_enabled && causal))
                probe_stop("mechanism_failed", "productive_later_reset_or_control_contrast_mismatch");
            else probe_stop("mechanism_success", profile::core_enabled ? "cooling_kill_refresh_produced_next_turn_extra_basic"
                : "ordinary_lethal_first_then_uncredited_natural_ready_reset");
            return true;
        }
        return true;
    }

public:
    void turn() {
        struct FuelSample {
            unsigned& low_water;
            ~FuelSample() { low_water = std::min(low_water, static_cast<unsigned>(cambc::fuel_remaining())); }
        } fuel_sample{fuel_low_water_};
        auto& me = cambc::self();
        fields_this_turn_ = 0; trace_ = {}; consumed_refresh_.reset();
        if (!initialized_) initialize();
        telemetry_tick();
        if (profile::probe_mode) {
            if (me.champion_name() != cambc::champion_type::vendetta || probe_finished_) return;
            if (!probe_start_) probe_start_ = cambc::round_number();
            if (!probe_released_ && cambc::round_number()-probe_start_ >= profile::max_setup_rounds) {
                probe_stop("not_exercised", "legal_setup_timeout"); return;
            }
            if (probe_released_ && cambc::round_number() >= profile::probe_round_limit) {
                probe_stop("not_exercised", "followup_witness_timeout"); return;
            }
        }
        // Dead champions retain shopping turns, but have no usable position.
        if (!me.is_alive()) {
            pending_refresh_.reset(); consumed_refresh_.reset();
            shop(); recovering_ = false; target_ = -1; state_ = State::farm; last_reason_ = banking::BankReason::none;
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
        if (recovering_) { state_ = State::recover; recover(camps, enemies); return; }
        if (profile::probe_mode && probe_tick(camps, enemies)) return;
        // Cleared camp ownership takes precedence over banking old gems.
        const int occupied_group = group_at_tile_[tile(me.position())];
        if (occupied_group >= 0) {
            auto& entry = groups_[occupied_group];
            if (entry.spawns.is_set(me.position()) && !(entry.spawns & cambc::vision::visible_tiles()).is_empty()
                && std::holds_alternative<cambc::map::camp_group_reward_available>(entry.group.state())) {
                target_ = occupied_group; return;
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
