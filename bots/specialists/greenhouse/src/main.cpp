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
    unsigned first_shards_ = 0, jungle_shards_ = 0;
    unsigned identity_ = 0;
    unsigned fields_this_turn_ = 0, navigation_failures_ = 0, purchases_ = 0;
    unsigned target_changes_ = 0, blocked_retries_ = 0, planning_guards_ = 0;
    unsigned fuel_low_water_ = std::numeric_limits<unsigned>::max();
    unsigned seq_ = 0, details_ = 0, summaries_ = 0, dropped_ = 0;
    unsigned last_turn_ = 0, turn_gaps_ = 0, summary_bucket_ = 0;
    unsigned first_complete_round_ = 0, second_complete_round_ = 0;
    int last_stage_ = -1;
    bool treasure_owned_ = false, monster_owned_ = false, jungle_owned_ = false;
    std::string actor_;
    unsigned tribute_starts_ = 0, tribute_completions_ = 0, tribute_interruptions_ = 0;
    bool tribute_active_ = false;
    unsigned tribute_bag_ = 0, tribute_round_ = 0, tribute_score_ = 0;
    unsigned probe_start_ = 0, probe_phase_ = 0, probe_upkeep_ = 0, probe_quiet_samples_ = 0;
    unsigned probe_previous_gold_ = 0, probe_previous_round_ = 0;
    unsigned probe_capture_round_ = 0;
    int reward_group_ = -1;
    unsigned reward_gold_ = 0, reward_round_ = 0, reward_upkeep_ = 0;
    bool reward_upkeep_known_ = false;
    int probe_group_ = -1;
    bool probe_finished_ = false, probe_holding_ = false, probe_reward_seen_ = false;
    bool probe_fixture_selected_ = false;
    std::array<int, 5> funding_groups_{-1, -1, -1, -1, -1};
    unsigned funding_index_ = 0, funding_previous_gold_ = 0, funding_previous_round_ = 0;
    unsigned funding_basics_ = 0;
    bool funding_holding_ = false, funding_complete_ = false;
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
        event(kind, "greenhouse_probe", "\"reason\":\"" + std::string(reason) + "\"", true);
        probe_finished_ = true;
    }
    std::string flags() const {
        return "\"treasure_owned\":" + boolean(treasure_owned_)
            + ",\"monster_owned\":" + boolean(monster_owned_)
            + ",\"jungle_owned\":" + boolean(jungle_owned_);
    }
    void telemetry_tick() {
        const unsigned round = cambc::round_number();
        if (last_turn_ && round > last_turn_ + 1) turn_gaps_ += round-last_turn_-1;
        last_turn_ = round;
        auto& me = cambc::self();
        if (!profile::probe_mode && reward_group_ >= 0) {
            auto& entry = groups_[reward_group_];
            if (me.is_alive() && entry.spawns.is_set(me.position()) && round == reward_round_+1
                && !(entry.spawns & cambc::vision::visible_tiles()).is_empty()) {
                const auto state = entry.group.state();
                const int gain = static_cast<int>(me.gold_count())-static_cast<int>(reward_gold_);
                if (std::holds_alternative<cambc::map::camp_group_reward_available>(state) && gain >= 0) {
                    reward_upkeep_ = static_cast<unsigned>(gain); reward_upkeep_known_ = true;
                } else if (std::holds_alternative<cambc::map::camp_group_respawning>(state)) {
                    event("observation", "greenhouse_reward", "\"group\":\"g_" + std::to_string(entry.key)
                        + "\",\"base_gold\":" + std::to_string(entry.group.gold_value())
                        + ",\"gold_before\":" + std::to_string(reward_gold_) + ",\"gold_after\":" + std::to_string(me.gold_count())
                        + ",\"observed_gain\":" + std::to_string(gain)
                        + ",\"passive_income_window\":" + std::to_string(reward_upkeep_)
                        + ",\"passive_observed\":" + boolean(reward_upkeep_known_) + "," + flags());
                }
            }
            reward_group_ = -1;
        }
        if (tribute_active_ && (!me.is_alive() || !me.is_submitting_gems())) {
            const unsigned score = cambc::global_submitted_gems(team::ally);
            const bool complete = me.is_alive() && me.gem_count() == 0 && score >= tribute_score_ + tribute_bag_;
            if (complete) ++tribute_completions_; else ++tribute_interruptions_;
            event("observation", "tribute_result", "\"starting_bag\":" + std::to_string(tribute_bag_)
                + ",\"remaining_bag\":" + std::to_string(me.gem_count()) + ",\"start_round\":" + std::to_string(tribute_round_)
                + ",\"team_score_delta\":" + std::to_string(score-tribute_score_) + ",\"complete\":" + boolean(complete));
            tribute_active_ = false;
        }
        if (round/32 > summary_bucket_) {
            summary_bucket_ = round/32;
            event("summary", "counters", "\"lastObservedRound\":" + std::to_string(round)
                + ",\"droppedEvents\":" + std::to_string(dropped_) + ",\"turn_gaps\":" + std::to_string(turn_gaps_)
                + ",\"purchases\":" + std::to_string(purchases_) + ",\"navigation_failures\":" + std::to_string(navigation_failures_)
                + ",\"target_changes\":" + std::to_string(target_changes_) + ",\"blocked_retries\":" + std::to_string(blocked_retries_)
                + ",\"planning_guards\":" + std::to_string(planning_guards_) + ",\"fuel_low_water\":" + std::to_string(fuel_low_water_)
                + ",\"first_complete_round\":" + std::to_string(first_complete_round_)
                + ",\"second_complete_round\":" + std::to_string(second_complete_round_));
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

    item_type first_item() const {
        return profile::core_enabled ? item_type::treasure_shard_of_strength : item_type::monster_shard_of_strength;
    }
    unsigned item_progress(item_type shard, const cambc::enum_map<item_type>& inventory) const {
        const auto emblem = shard.result();
        return emblem && inventory[*emblem] ? shard.combine_count() : inventory[shard];
    }
    greenhouse::Stage build_stage() const {
        return greenhouse::stage(first_shards_, first_item().combine_count(),
            jungle_shards_, item_type::jungle_shard_of_health.combine_count());
    }
    void refresh_equipment() {
        const auto inventory = cambc::self().item_count();
        const bool previous_treasure = treasure_owned_;
        first_shards_ = item_progress(first_item(), inventory);
        jungle_shards_ = item_progress(item_type::jungle_shard_of_health, inventory);
        treasure_owned_ = inventory[item_type::treasure_emblem_of_strength]
            || inventory[item_type::treasure_emblem_of_focus] || inventory[item_type::treasure_emblem_of_health];
        if (previous_treasure != treasure_owned_) reward_upkeep_known_ = false;
        monster_owned_ = inventory[item_type::monster_emblem_of_strength]
            || inventory[item_type::monster_emblem_of_focus] || inventory[item_type::monster_emblem_of_health];
        jungle_owned_ = inventory[item_type::jungle_emblem_of_strength]
            || inventory[item_type::jungle_emblem_of_focus] || inventory[item_type::jungle_emblem_of_health];
        camp_multiplier_ = cambc::self().base_stats().camp_damage_multiplier;
        incoming_multiplier_ = 1;
        if (monster_owned_) camp_multiplier_ *= 1 + item_type::monster_emblem_of_strength.passive_magnitude();
        if (jungle_owned_) incoming_multiplier_ *= 1 - item_type::jungle_emblem_of_health.passive_magnitude();
        const auto round = cambc::round_number();
        if (first_shards_ >= first_item().combine_count() && !first_complete_round_) first_complete_round_ = round;
        if (jungle_shards_ >= item_type::jungle_shard_of_health.combine_count() && !second_complete_round_) second_complete_round_ = round;
        const int stage = static_cast<int>(build_stage());
        if (stage != last_stage_) {
            last_stage_ = stage;
            event("observation", "greenhouse_stage", "\"stage\":" + std::to_string(stage)
                + ",\"first_shards\":" + std::to_string(first_shards_) + ",\"jungle_shards\":" + std::to_string(jungle_shards_)
                + "," + flags() + ",\"gem_focus\":" + boolean(stage == 2)
                + ",\"camp_multiplier\":" + std::to_string(camp_multiplier_), profile::probe_mode);
        }
    }
    void shop() {
        auto& me = cambc::self();
        refresh_equipment();
        // Both diagnostic arms complete the same equipment-free gold circuit,
        // return to spawn, and then freeze purchases through the measured reward.
        if (profile::probe_mode) {
            if (probe_phase_ == 1 || probe_phase_ == 2) return;
            if (probe_phase_ == 0) {
                if (!funding_complete_ || !me.is_alive()
                    || !cambc::map::spawn_region(team::ally).is_set(me.position())) return;
                const unsigned remaining = first_item().combine_count()-first_shards_;
                if (me.gold_count()/first_item().cost() < remaining) {
                    probe_stop("not_exercised", "legal_funding_below_first_emblem_cost"); return;
                }
            }
        }
        for (int visit_stage = 0; visit_stage < 3; ++visit_stage) {
            // A previous bulk purchase can consume most of this turn through
            // inventory refresh and telemetry. Reserve half the normal budget
            // before starting another stage; defer it to the next legal visit.
            if (!profile::probe_mode && cambc::fuel_remaining() < 50000) {
                ++planning_guards_; break;
            }
            const auto build = build_stage();
            auto item = item_type::power_shard_of_strength;
            unsigned limit = 100;
            if (build == greenhouse::Stage::first) {
                item = first_item(); limit = item.combine_count()-first_shards_;
            } else if (build == greenhouse::Stage::jungle) {
                item = item_type::jungle_shard_of_health; limit = item.combine_count()-jungle_shards_;
            }
            const unsigned count = std::min(limit, me.gold_count()/item.cost());
            if (!count || !me.can_buy(item, count)) break;
            const unsigned before = me.gold_count();
            const float strength_before = me.strength();
            me.buy(item, count); ++purchases_;
            refresh_equipment();
            event("action_result", "greenhouse_purchase", "\"item\":\"" + cambc::to_string(item)
                + "\",\"count\":" + std::to_string(count) + ",\"gold_before\":" + std::to_string(before)
                + ",\"gold_after\":" + std::to_string(me.gold_count())
                + ",\"strength_gain\":" + std::to_string(me.strength()-strength_before) + "," + flags());
            if (profile::probe_mode && probe_phase_ == 0) {
                if (build_stage() != greenhouse::Stage::jungle || treasure_owned_ != profile::core_enabled
                    || monster_owned_ == profile::core_enabled) {
                    probe_stop("mechanism_failed", "first_emblem_inventory_mismatch"); return;
                }
                event("opportunity", "greenhouse_first_emblem", "\"strength_before\":" + std::to_string(strength_before)
                    + ",\"strength_after\":" + std::to_string(me.strength())
                    + ",\"count\":" + std::to_string(count) + "," + flags()
                    + ",\"camp_multiplier\":" + std::to_string(camp_multiplier_), true);
                probe_phase_ = 1; probe_previous_gold_ = me.gold_count(); probe_previous_round_ = cambc::round_number();
                return;
            }
            if (profile::probe_mode && probe_phase_ == 3) {
                event("action_result", "greenhouse_later_purchase", "\"rounds_after_capture\":"
                    + std::to_string(cambc::round_number()-probe_capture_round_)
                    + ",\"count\":" + std::to_string(count)
                    + ",\"scoring_payback_established\":false", true);
                probe_stop("mechanism_success", "captured_gold_bonus_and_later_legal_purchase");
                return;
            }
        }
    }

    bitboard blocked() const {
        auto result = terrain_ | cambc::vision::champion_tiles() | cambc::vision::camp_tiles()
            | cambc::vision::teleport_tiles();
        if (profile::probe_mode)
            for (unsigned i = 0; i < groups_.size(); ++i)
                if (static_cast<int>(i) != (probe_phase_ == 0 && funding_index_ < funding_groups_.size()
                        ? funding_groups_[funding_index_] : probe_group_)) result |= groups_[i].spawns;
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
        if (cambc::fuel_remaining() < 30000) { ++planning_guards_; return false; }
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
        if (altars_.empty() || !greenhouse::bank(me.gem_count(), cambc::round_number(),
            cambc::constants::max_rounds, nearest_altar(me.position()))) return false;
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
                    ++tribute_starts_;
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

    void hold_reward(unsigned group) {
        target_ = static_cast<int>(group);
        // Snapshot after this turn's shopping. On the next owner turn the
        // unspent delta is upkeep alone, or upkeep plus a newly captured reward.
        // This is own gold; shared captures are resolved by the native replay.
        if (reward_group_ != static_cast<int>(group) && reward_round_+1 != cambc::round_number())
            reward_upkeep_known_ = false;
        reward_group_ = static_cast<int>(group);
        reward_gold_ = cambc::self().gold_count(); reward_round_ = cambc::round_number();
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
            if (has_non_monolith_gems_ && first_shards_ >= first_item().combine_count()
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
                if (reward && entry.spawns.is_set(origin)) { hold_reward(i); return; }
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
    bool select_probe_fixture() {
        if (profile::probe_group_key < 0 || static_cast<unsigned>(profile::probe_group_key) >= cambc::map::width()*cambc::map::height()) {
            probe_stop("not_exercised", "fixture_key_out_of_bounds"); return false;
        }
        const auto selected_key = probe_fixture_tile(profile::probe_group_key);
        for (unsigned i = 0; i < groups_.size(); ++i) {
            const auto& entry = groups_[i];
            if (entry.key != selected_key || entry.monolith || entry.group.gold_value() == 0
                || entry.group.camps().size() != 1 || !entry.group.camps().front().is_gold_cluster()) continue;
            probe_group_ = static_cast<int>(i); break;
        }
        if (probe_group_ < 0) { probe_stop("not_exercised", "declared_safe_gold_group_missing"); return false; }
        for (unsigned n = 0; n < funding_groups_.size(); ++n) {
            const auto wanted = probe_fixture_tile(profile::probe_funding_group_keys[n]);
            for (unsigned i = 0; i < groups_.size(); ++i) {
                const auto& funding = groups_[i];
                if (funding.key == wanted && funding.key != selected_key && !funding.monolith
                    && funding.group.camps().size() == 1 && funding.group.camps().front().is_gold_cluster()
                    && funding.incoming <= 0.0001f && funding.group.gem_value() == 0)
                    funding_groups_[n] = static_cast<int>(i);
            }
            if (funding_groups_[n] < 0) { probe_stop("not_exercised", "declared_legal_funding_group_missing"); return false; }
        }
        probe_fixture_selected_ = true;
        const auto& entry = groups_[probe_group_];
        event("observation", "greenhouse_fixture", "\"group\":\"g_" + std::to_string(entry.key)
            + "\",\"member\":" + std::to_string(tile(entry.group.camps().front().spawn_position()))
            + ",\"base_gold\":" + std::to_string(entry.group.gold_value())
            + ",\"mirrored_home\":" + boolean(mirrored_probe_home()), true);
        return true;
    }
    bool equipment_empty() const {
        const auto inventory = cambc::self().item_count();
        for (unsigned i = 0; i < static_cast<unsigned>(item_type::count); ++i)
            if (inventory[item_type{static_cast<item_type::enum_type>(i)}]) return false;
        return true;
    }
    bool fund_probe(const std::vector<camp>& camps, const std::vector<champion>& enemies) {
        auto& me = cambc::self();
        const unsigned round = cambc::round_number();
        if (!equipment_empty()) { probe_stop("not_exercised", "funding_equipment_not_empty"); return true; }
        if (funding_complete_) {
            if (cambc::map::spawn_region(team::ally).is_set(me.position())) return true;
            // All five declared groups have paid before this identical safe
            // recall. No gold or bags are injected, transferred or refunded.
            for (const auto& enemy : enemies) if (distance(me.position(), enemy.position()) <= enemy.attack_range()+1) {
                probe_stop("not_exercised", "enemy_blocks_funding_return"); return true;
            }
            for (const auto& member : camps) if (member.is_alive() && member.is_awake() && member.strength() > 0
                && distance(me.position(), member.position()) <= member.attack_range()+1) {
                probe_stop("not_exercised", "active_camp_blocks_funding_return"); return true;
            }
            me.recall(); return true;
        }
        if (funding_index_ >= funding_groups_.size()) {
            probe_stop("not_exercised", "funding_index_out_of_range"); return true;
        }
        auto& entry = groups_[funding_groups_[funding_index_]];
        if (!(entry.spawns & cambc::vision::visible_tiles()).is_empty()) {
            const auto state = entry.group.state();
            if (std::holds_alternative<cambc::map::camp_group_respawning>(state)) {
                const int gain = static_cast<int>(me.gold_count())-static_cast<int>(funding_previous_gold_);
                const unsigned expected = entry.group.gold_value()+cambc::constants::passive_gold;
                if (!funding_holding_ || !entry.spawns.is_set(me.position()) || round != funding_previous_round_+1
                    || gain != static_cast<int>(expected)) {
                    probe_stop("not_exercised", "funding_reward_not_attributed_to_solo_hold"); return true;
                }
                event("observation", "greenhouse_legal_funding", "\"group\":\"g_" + std::to_string(entry.key)
                    + "\",\"captured_gold\":" + std::to_string(entry.group.gold_value())
                    + ",\"current_gold\":" + std::to_string(me.gold_count())
                    + ",\"basic_count\":" + std::to_string(funding_basics_)
                    + ",\"equipment_empty\":true", true);
                ++funding_index_; funding_holding_ = false; funding_basics_ = 0;
                if (funding_index_ == funding_groups_.size()) {
                    const auto monster = item_type::monster_shard_of_strength;
                    const auto treasure = item_type::treasure_shard_of_strength;
                    const unsigned required = std::max(monster.cost()*monster.combine_count(), treasure.cost()*treasure.combine_count());
                    if (me.gold_count() < required) { probe_stop("not_exercised", "legal_funding_below_both_emblem_costs"); return true; }
                    funding_complete_ = true;
                }
                return true;
            }
            if (std::holds_alternative<cambc::map::camp_group_reward_available>(state)) {
                if (!entry.spawns.is_set(me.position())) { move_to(entry.spawns); return true; }
                for (const auto& ally : cambc::vision::champions(team::ally))
                    if (ally != me && entry.spawns.is_set(ally.position())) {
                        probe_stop("not_exercised", "funding_capture_not_solo"); return true;
                    }
                // The frozen parking opponent never acts outside spawn. A
                // distant visible parked actor cannot contest this capture;
                // retain a conservative observed basic-threat margin.
                for (const auto& enemy : enemies)
                    if (entry.spawns.is_set(enemy.position()) || distance(me.position(), enemy.position()) <= enemy.attack_range()+1) {
                        probe_stop("not_exercised", "enemy_threatens_funding_capture"); return true;
                    }
                funding_holding_ = true; funding_previous_gold_ = me.gold_count(); funding_previous_round_ = round;
                return true;
            }
        }
        for (const auto& member : camps) if (entry.spawns.is_set(member.spawn_position()) && member.is_alive()) {
            if (member.strength() > 0.0001f) { probe_stop("not_exercised", "funding_camp_not_harmless"); return true; }
            if (me.can_attack() && me.attack_tiles().is_set(member.position())) {
                const float damage = me.strength()*me.base_stats().camp_damage_multiplier;
                if (damage <= 0 || funding_basics_ >= static_cast<unsigned>(std::ceil(entry.health/damage))) {
                    probe_stop("not_exercised", "funding_basic_action_bound_exceeded"); return true;
                }
                me.attack(member); ++funding_basics_; return true;
            }
            break;
        }
        // One field per turn toward this single declared candidate. No
        // per-candidate fields, and no unrelated living spawn is a route tile.
        move_to(around(entry.anchor, 1)); return true;
    }
    // Both diagnostic arms legally wait, buy, walk, basic-attack and solo-capture.
    // No shopping or other gold-generating action occurs in the reward window.
    bool probe_tick(const std::vector<camp>& camps, const std::vector<champion>& enemies) {
        auto& me = cambc::self();
        const unsigned round = cambc::round_number();
        if (cambc::fuel_remaining() < 45000) return true;
        if (!probe_fixture_selected_ && !select_probe_fixture()) return true;
        if (probe_phase_ == 0) return fund_probe(camps, enemies);
        if (probe_phase_ == 1) {
            if (!cambc::map::spawn_region(team::ally).is_set(me.position())) {
                probe_stop("not_exercised", "passive_calibration_not_at_spawn"); return true;
            }
            if (round == probe_previous_round_) return true;
            if (round != probe_previous_round_+1 || me.gold_count() < probe_previous_gold_) {
                probe_stop("not_exercised", "passive_window_not_consecutive"); return true;
            }
            const unsigned gain = me.gold_count()-probe_previous_gold_;
            event("observation", "greenhouse_passive_window", "\"from_round\":" + std::to_string(probe_previous_round_)
                + ",\"to_round\":" + std::to_string(round) + ",\"gold_before\":" + std::to_string(probe_previous_gold_)
                + ",\"gold_after\":" + std::to_string(me.gold_count()) + ",\"observed_upkeep\":" + std::to_string(gain)
                + ",\"queried_base_upkeep\":" + std::to_string(cambc::constants::passive_gold) + "," + flags(), true);
            if (probe_quiet_samples_ && gain != probe_upkeep_) {
                probe_stop("not_exercised", "passive_income_not_stable"); return true;
            }
            probe_upkeep_ = gain; ++probe_quiet_samples_;
            probe_previous_gold_ = me.gold_count(); probe_previous_round_ = round;
            if (probe_quiet_samples_ >= 2) probe_phase_ = 2;
            return true;
        }
        if (probe_phase_ == 3) return true; // ordinary legal shop is called before this adapter
        auto& entry = groups_[probe_group_];
        if (!(entry.spawns & cambc::vision::visible_tiles()).is_empty()) {
            const auto state = entry.group.state();
            if (std::holds_alternative<cambc::map::camp_group_respawning>(state)) {
                if (!probe_holding_ || !entry.spawns.is_set(me.position()) || round != probe_previous_round_+1) {
                    probe_stop("not_exercised", "reward_not_attributed_to_consecutive_solo_hold"); return true;
                }
                const int gain = static_cast<int>(me.gold_count())-static_cast<int>(probe_previous_gold_);
                const int reward = greenhouse::reward_only(gain, round-probe_previous_round_, probe_upkeep_);
                const float multiplier = 1 + (treasure_owned_ ? item_type::treasure_emblem_of_strength.passive_magnitude() : 0);
                const int expected = static_cast<int>(entry.group.gold_value()*multiplier+0.0001f);
                event("observation", "greenhouse_reward", "\"group\":\"g_" + std::to_string(entry.key)
                    + "\",\"base_gold\":" + std::to_string(entry.group.gold_value())
                    + ",\"gold_before\":" + std::to_string(probe_previous_gold_) + ",\"gold_after\":" + std::to_string(me.gold_count())
                    + ",\"observed_gain\":" + std::to_string(gain) + ",\"passive_income_window\":" + std::to_string(probe_upkeep_)
                    + ",\"reward_only\":" + std::to_string(reward) + ",\"expected_reward\":" + std::to_string(expected)
                    + "," + flags(), true);
                if (reward != expected || treasure_owned_ != profile::core_enabled || monster_owned_ == profile::core_enabled) {
                    probe_stop("mechanism_failed", "captured_gold_or_actual_passive_mismatch"); return true;
                }
                probe_reward_seen_ = true; probe_capture_round_ = round; probe_phase_ = 3;
                event("mechanism_success", "greenhouse_capture_bonus", "\"extra_captured_gold\":"
                    + std::to_string(reward-static_cast<int>(entry.group.gold_value()))
                    + ",\"native_reward_verification_required\":true", true);
                return true;
            }
            if (std::holds_alternative<cambc::map::camp_group_reward_available>(state)) {
                if (entry.spawns.is_set(me.position())) {
                    for (const auto& ally : cambc::vision::champions(team::ally))
                        if (ally != me && entry.spawns.is_set(ally.position())) {
                            probe_stop("not_exercised", "capture_not_solo"); return true;
                        }
                    if (!enemies.empty()) { probe_stop("not_exercised", "enemy_present_during_capture"); return true; }
                    if (!probe_holding_) event("opportunity", "greenhouse_reward_hold", "\"group\":\"g_" + std::to_string(entry.key)
                        + "\",\"strength\":" + std::to_string(me.strength()) + "," + flags(), true);
                    probe_holding_ = true; probe_previous_gold_ = me.gold_count(); probe_previous_round_ = round;
                    return true;
                }
                move_to(entry.spawns); return true;
            }
        }
        target_ = probe_group_; partial_clear_ = false;
        std::optional<camp> target;
        for (const auto& member : camps) if (entry.spawns.is_set(member.spawn_position()) && member.is_alive()) { target = member; break; }
        if (target && me.can_attack() && me.attack_tiles().is_set(target->position())) { me.attack(*target); return true; }
        const auto goals = target ? around(target->position(), me.attack_range()) : around(entry.anchor, 1);
        move_to(goals);
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
        if (profile::probe_mode) {
            if (me.champion_name() != cambc::champion_type::tavi || probe_finished_) return;
            if (!probe_start_) probe_start_ = cambc::round_number();
            if (!probe_reward_seen_ && cambc::round_number()-probe_start_ >= profile::max_setup_rounds) {
                probe_stop("not_exercised", "legal_setup_timeout"); return;
            }
            if (probe_reward_seen_ && cambc::round_number() >= profile::probe_round_limit) {
                probe_stop("observation", "payback_not_observed"); return;
            }
        }
        if (!me.is_alive()) {
            shop(); recovering_ = false; target_ = -1;
            if (profile::probe_mode) probe_stop("not_exercised", "measurement_actor_died_during_setup");
            return;
        }
        if (me.is_recalling() || me.is_submitting_gems() || me.is_teleporting()) return;
        if (!me.can_move() && !me.can_attack() && !me.can_cast()) return;
        shop();
        if (profile::probe_mode && probe_finished_) return;
        const bool spawn = cambc::map::spawn_region(team::ally).is_set(me.position());
        if (spawn) {
            recovering_ = false;
            if (me.health() < me.max_health()) return;
        }
        // Do not spend the shopping reserve materializing visible unit lists.
        // These lists are read once here and reused by recovery/farming below.
        if (!profile::probe_mode && cambc::fuel_remaining() < 45000) {
            ++planning_guards_; return;
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
        if (!spawn && fundamentals::should_retreat(me.health(), me.max_health(),
            incoming * (partial_clear_ ? 2.0f : 1.0f))) recovering_ = true;
        if (recovering_) { recover(camps, enemies); return; }
        if (profile::probe_mode) { probe_tick(camps, enemies); return; }
        const int occupied_group = group_at_tile_[tile(me.position())];
        if (occupied_group >= 0) {
            auto& entry = groups_[occupied_group];
            if (entry.spawns.is_set(me.position()) && !(entry.spawns & cambc::vision::visible_tiles()).is_empty()
                && std::holds_alternative<cambc::map::camp_group_reward_available>(entry.group.state())) {
                hold_reward(static_cast<unsigned>(occupied_group)); return;
            }
        }
        if (bank()) return;
        for (const auto& enemy : enemies)
            if (distance(me.position(), enemy.position()) <= 1 && fight(enemy, false, true)) return;
        if (!spawn && me.champion_name() != cambc::champion_type::tavi && me.gold_count() >= 300) {
            recovering_ = true; recover(camps, enemies); return;
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
