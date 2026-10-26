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
    unsigned bank_turns_ = 0, journeys_ = 0;
    unsigned order_switches_ = 0, order_decisions_ = 0, recovery_turns_ = 0;
    int last_order_tile_ = -1;
    unsigned seq_ = 0, details_ = 0, summaries_ = 0, dropped_ = 0;
    unsigned last_turn_ = 0, turn_gaps_ = 0, summary_bucket_ = 0;
    std::string actor_;
    enum class State { farm, bank_requested, channel, recover };
    State state_ = State::farm;
    bool tribute_active_ = false;
    unsigned tribute_bag_ = 0, tribute_round_ = 0, tribute_score_ = 0;
    unsigned tribute_starts_ = 0, tribute_completions_ = 0, tribute_interruptions_ = 0;
    unsigned probe_start_ = 0, probe_bag_ = 0;
    int probe_group_ = -1;
    bool probe_released_ = false, probe_finished_ = false;
    bool probe_holding_ = false, probe_prepared_ = false;
    bool probe_funded_ = false, probe_funding_holding_ = false;
    int probe_funding_group_ = -1;
    unsigned preparation_wolf_hits_ = 0, preparation_elk_hits_ = 0;
    unsigned window_round_ = 0, window_phases_ = 0, window_basics_ = 0, window_kills_ = 0;
    unsigned window_first_kill_ = 0, window_second_kill_ = 0, expected_first_kill_ = 0;
    float window_health_ = 0, window_last_health_ = 0, window_loss_ = 0;
    float window_predicted_active_ = 0, window_predicted_control_ = 0;
    bool window_confounded_ = false;
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
        event(kind, "integral_window", "\"reason\":\"" + std::string(reason) + "\"", true);
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
                + ",\"order_decisions\":" + std::to_string(order_decisions_)
                + ",\"order_switches\":" + std::to_string(order_switches_) + ",\"recovery_turns\":" + std::to_string(recovery_turns_)
                + ",\"purchases\":" + std::to_string(purchases_) + ",\"navigation_failures\":" + std::to_string(navigation_failures_)
                + ",\"target_changes\":" + std::to_string(target_changes_) + ",\"blocked_retries\":" + std::to_string(blocked_retries_)
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
        if (me.can_attack()) { me.attack(target); acted = true; }
        if (!valid()) return acted;
        // Includes ranged champions that walked this turn: a reset explicitly
        // restores their attack allowance before the extra attack.
        if (!me.can_attack())
            for (const auto& ability : abilities)
                if (ability.is_attack_reset() && ability.is_ready() && ability.can_target_self()) {
                    if (ability.use() && me.can_attack() && valid()) me.attack(target);
                    acted = true;
                    break;
                }
        return acted;
    }

    struct Ranked { camp member; integral::Candidate score; unsigned actionable_count; };
    std::optional<Ranked> ranked_member(int group, bool walked, bool only_actionable, bool geometry_only = false) {
        if (cambc::fuel_remaining() < 16000) { ++planning_guards_; return std::nullopt; }
        auto& me = cambc::self();
        const float damage = me.strength() * camp_multiplier_;
        const auto origin = me.position();
        std::optional<Ranked> best;
        float baseline_score = std::numeric_limits<float>::max();
        unsigned count = 0;
        bitboard action_tiles;
        if (geometry_only || me.can_attack()) action_tiles = me.attack_tiles();
        if (!geometry_only && !walked && me.can_cast() && !profile::probe_mode)
            for (const auto& ability : me.abilities())
                if ((ability.is_damage() || ability.is_cc()) && ability.is_ready() && ability.can_target_camps())
                    action_tiles |= ability.valid_tiles();
        // Scan only the selected group's public membership, and query live data
        // only after this member independently passes its visibility guard.
        for (const auto& member : groups_[group].group) {
            if (!member.is_visible() || !member.is_alive()) continue;
            const auto position = member.position();
            const bool can_act = action_tiles.is_set(position);
            if (can_act) ++count;
            if (only_actionable && !can_act) continue;
            const float hp = member.health();
            const float weight = distance(origin, position) <= member.attack_range() ? member.strength() : 0;
            const integral::Candidate candidate{hp, weight, integral::basic_work(hp, damage), tile(member.spawn_position())};
            const float weak = hp + distance(origin, position) * (partial_clear_ ? 1000.0f : 100.0f);
            const bool better = !best || (profile::core_enabled && only_actionable
                ? integral::precedes(candidate, best->score)
                : weak < baseline_score || (weak == baseline_score && candidate.tile < best->score.tile));
            if (better) { best = Ranked{member, candidate, 0}; baseline_score = weak; }
        }
        if (best) best->actionable_count = count;
        return best;
    }
    void record_order(int group, const Ranked& selected) {
        ++order_decisions_;
        if (last_order_tile_ == static_cast<int>(selected.score.tile)) return;
        if (last_order_tile_ >= 0) ++order_switches_;
        last_order_tile_ = static_cast<int>(selected.score.tile);
        event("prediction", "integral_order", "\"group\":\"g_" + std::to_string(groups_[group].key)
            + "\",\"member\":" + std::to_string(selected.score.tile) + ",\"H\":" + std::to_string(selected.score.health)
            + ",\"w\":" + std::to_string(selected.score.weight) + ",\"p\":" + std::to_string(selected.score.work)
            + ",\"ratio_numerator\":" + std::to_string(selected.score.weight)
            + ",\"ratio_denominator\":" + std::to_string(selected.score.work)
            + ",\"actionable_count\":" + std::to_string(selected.actionable_count));
    }
    bool camp_fight(int group, bool walked) {
        if (!profile::core_enabled) {
            const auto ordinary = ranked_member(group, walked, false);
            if (ordinary) record_order(group, *ordinary);
            return ordinary && fight(ordinary->member, walked, groups_[group].incoming > 0);
        }
        auto& me = cambc::self();
        bool acted = false;
        auto target = ranked_member(group, walked, true);
        if (!target) {
            // After ranged walking no ordinary attack is currently available.
            // Preserve the inherited reset opportunity on its approach target,
            // then rank again with the newly restored live allowance.
            const auto ordinary = ranked_member(group, walked, false);
            if (!ordinary || !me.attack_tiles().is_set(ordinary->member.position()) || !me.can_cast()) return false;
            for (const auto& ability : me.abilities())
                if (ability.is_attack_reset() && ability.is_ready() && ability.can_target_self() && ability.use()) {
                    target = ranked_member(group, walked, true);
                    if (target && me.can_attack() && me.attack_tiles().is_set(target->member.position())) {
                        record_order(group, *target); me.attack(target->member);
                        (void)ranked_member(group, walked, true);
                    }
                    return true;
                }
            return false;
        }
        record_order(group, *target);
        if (!walked && me.can_cast()) {
            for (const auto& ability : me.abilities())
                if ((ability.is_damage() || ability.is_cc()) && ability.is_ready() && ability.can_target_camps()
                    && ability.valid_tiles().is_set(target->member.position())) {
                    if (ability.use(target->member)) acted = true;
                    break;
                }
        }
        // Re-observe after a damaging or stunning cast, before shield/basic.
        target = ranked_member(group, walked, true);
        if (!target) return acted;
        if (!me.attack_tiles().is_set(target->member.position())) return acted;
        if (groups_[group].incoming > 0 && me.can_cast())
            for (const auto& ability : me.abilities())
                if (ability.is_shield() && ability.is_ready() && ability.can_target_self()) {
                    acted |= ability.use(); break;
                }
        target = ranked_member(group, walked, true);
        if (target && me.can_attack() && me.attack_tiles().is_set(target->member.position())) {
            record_order(group, *target); me.attack(target->member); acted = true;
        }
        // No stale post-hit target survives here: Varg can splash-kill or change
        // every member's remaining work. Geometry scan permits a legal reset
        // when the normal attack allowance has already been consumed.
        target = ranked_member(group, walked, true, true);
        if (target && !me.can_attack() && me.can_cast())
            for (const auto& ability : me.abilities())
                if (ability.is_attack_reset() && ability.is_ready() && ability.can_target_self()) {
                    if (ability.use()) {
                        acted = true;
                        target = ranked_member(group, walked, true);
                        if (target && me.can_attack() && me.attack_tiles().is_set(target->member.position())) {
                            record_order(group, *target); me.attack(target->member);
                            // Required fresh observation even after the final
                            // hit: no stale Varg splash state crosses actions.
                            (void)ranked_member(group, walked, true);
                        }
                    }
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
        if (altars_.empty() || !me.gem_count() || !(me.gem_count() >= 24
            || nearest_altar(me.position()) <= 4 || integral::late_round(cambc::round_number(), cambc::constants::max_rounds))) {
            state_ = State::farm; return false;
        }
        ++bank_turns_;
        if (state_ != State::bank_requested) ++journeys_;
        state_ = State::bank_requested;
        bitboard goals;
        for (const auto altar : altars_) {
            if (distance(me.position(), altar) <= cambc::constants::submit_gems_range) {
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
                if (score < weakest || (score == weakest && target && tile(monster.spawn_position()) < tile(target->spawn_position()))) {
                    weakest = score; target = monster;
                }
            }
        if (target && camp_fight(selected, false)) return;
        bitboard goals;
        if (target) goals = around(target->position(), me.attack_range());
        else {
            for (const auto& monster : entry.group) goals |= around(monster.spawn_position(), 1);
        }
        if (move_to(goals)) {
            // Fresh threat coverage/actionability after movement.
            camp_fight(selected, true);
        } else if (me.can_move()) { entry.unavailable_until = round + 4; ++navigation_failures_; }
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
#include "probe.h"

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
        if (profile::probe_mode) {
            if ((me.champion_name() != cambc::champion_type::tavi && me.champion_name() != cambc::champion_type::vendetta) || probe_finished_) return;
            if (!probe_start_) probe_start_ = cambc::round_number();
            if (!probe_released_ && cambc::round_number()-probe_start_ >= profile::max_setup_rounds) {
                probe_stop("not_exercised", "legal_setup_timeout"); return;
            }
            if (probe_released_ && cambc::round_number() >= profile::probe_round_limit) {
                probe_stop("not_exercised", "conversion_witness_timeout"); return;
            }
        }
        // Dead champions retain shopping turns, but have no usable position.
        if (!me.is_alive()) {
            if (!profile::probe_mode) shop(); recovering_ = false; target_ = -1; state_ = State::farm;
            if (profile::probe_mode) probe_stop(probe_released_ ? "mechanism_failed" : "not_exercised", "measurement_actor_died");
            return;
        }
        if (me.is_recalling() || me.is_submitting_gems() || me.is_teleporting()) {
            state_ = me.is_submitting_gems() ? State::channel : State::recover;
            return;
        }
        if (!me.can_move() && !me.can_attack() && !me.can_cast()) return;
        if (!profile::probe_mode) shop(); else refresh_equipment();
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
        if (recovering_) { ++recovery_turns_; state_ = State::recover;
            if (profile::probe_mode && probe_released_) { probe_stop("mechanism_failed", "measurement_forced_retreat"); return; }
            recover(camps, enemies); return; }
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
