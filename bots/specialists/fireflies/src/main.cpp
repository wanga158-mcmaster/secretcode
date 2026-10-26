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
#if FIREFLIES_BOUNDED_STARTUP
    // Public, immutable metadata may be retained across setup turns. Never
    // carry a live observation or an intended gameplay action over this yield.
    std::vector<cambc::map::camp_group> startup_groups_;
    unsigned startup_phase_ = 0;
#endif
#if FIREFLIES_CONNECTED_CATALOG
    bitboard connected_tiles_;
    std::vector<unsigned char> connected_groups_;
    bool connected_ready_ = false;
#endif
#if FIREFLIES_CAPTURE_DEFENSE
    fireflies::CaptureHold capture_hold_;
#endif
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
    fireflies::BankReason last_reason_ = fireflies::BankReason::none;
    bool tribute_active_ = false;
    unsigned tribute_bag_ = 0, tribute_round_ = 0, tribute_score_ = 0;
    unsigned tribute_starts_ = 0, tribute_completions_ = 0, tribute_interruptions_ = 0;
    unsigned probe_start_ = 0, probe_bag_ = 0;
    int probe_group_ = -1;
    bool probe_released_ = false, probe_finished_ = false;
    bool probe_decision_seen_ = false, probe_requested_ = false, probe_journey_seen_ = false;
    bool probe_holding_ = false, probe_capture_seen_ = false;
    vec2 probe_stage_{};
    bool probe_stage_selected_ = false;
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
        event(kind, "fireflies_probe", "\"reason\":\"" + std::string(reason) + "\"", true);
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
            if (profile::probe_mode && probe_released_ && !probe_finished_) {
                if (complete && probe_decision_seen_ && probe_requested_ && probe_journey_seen_
                    && probe_capture_seen_ && tribute_bag_ == probe_bag_ && score-tribute_score_ == probe_bag_)
                    probe_stop("mechanism_success", "threshold_journey_and_captured_bag_tributed");
                else probe_stop("mechanism_failed", "tribute_interrupted_or_conversion_not_attributed");
            }
        }
        if (round/32 > summary_bucket_) {
            summary_bucket_ = round/32;
            event("summary", "counters", "\"lastObservedRound\":" + std::to_string(round)
                + ",\"droppedEvents\":" + std::to_string(dropped_) + ",\"turn_gaps\":" + std::to_string(turn_gaps_)
                + ",\"bank_turns\":" + std::to_string(bank_turns_) + ",\"threshold_only_requests\":" + std::to_string(threshold_only_turns_)
                + ",\"journeys\":" + std::to_string(journeys_) + ",\"tribute_starts\":" + std::to_string(tribute_starts_)
                + ",\"tribute_completions\":" + std::to_string(tribute_completions_) + ",\"tribute_interruptions\":" + std::to_string(tribute_interruptions_)
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

#if FIREFLIES_BOUNDED_STARTUP
    void initialize_bounded() {
        if (startup_phase_ == 0) {
            terrain_ = cambc::map::walls() | cambc::map::holes() | cambc::map::altars()
                | cambc::map::spawn_region(team::enemy);
            altars_ = cambc::map::altars().positions();
            group_at_tile_.assign(cambc::map::width() * cambc::map::height(), -1);
            startup_groups_ = cambc::map::camp_groups();
            startup_phase_ = 1;
        }
        if (startup_phase_ == 1) {
            // The current pool has at most 30 members in one group. Reserve
            // 60k before starting that atomic group and limit the batch even
            // when the engine grants a larger first-turn allowance.
            unsigned completed = 0;
            while (groups_.size() < startup_groups_.size() && completed < 64
                   && cambc::fuel_remaining() >= 60000) {
                const auto& group = startup_groups_[groups_.size()];
                Group entry{group, group.spawn_tiles(), group.camps().front().spawn_position()};
                for (const auto& monster : group) {
                    if (tile(monster.spawn_position()) < tile(entry.anchor)) entry.anchor = monster.spawn_position();
                    const auto stats = monster.base_stats();
                    entry.health += stats.base_health;
                    entry.incoming += stats.base_strength;
                    entry.monolith |= monster.is_monolith();
                }
                has_non_monolith_gems_ |= !entry.monolith && entry.group.gem_value() > 0;
                groups_.push_back(std::move(entry));
                ++completed;
            }
            if (groups_.size() != startup_groups_.size()) return;
            startup_phase_ = 2;
            if (cambc::fuel_remaining() < 60000) return;
        }
        std::sort(groups_.begin(), groups_.end(), [](const Group& a, const Group& b) { return a.anchor.y == b.anchor.y ? a.anchor.x < b.anchor.x : a.anchor.y < b.anchor.y; });
        for (unsigned i = 0; i < groups_.size(); ++i) {
            groups_[i].key = tile(groups_[i].anchor);
            for (const auto& member : groups_[i].group) group_at_tile_[tile(member.spawn_position())] = i;
        }
        std::sort(altars_.begin(), altars_.end(), [](vec2 a, vec2 b) { return tile(a) < tile(b); });
        identity_ = static_cast<unsigned>(cambc::self().champion_name());
        actor_ = cambc::to_string(cambc::self().champion_name());
        startup_groups_.clear();
        event("start", "profile", "\"enabled\":" + boolean(profile::core_enabled) + ",\"probe_mode\":" + boolean(profile::probe_mode), true);
        initialized_ = true;
    }
#endif

#if FIREFLIES_CONNECTED_CATALOG
    void initialize_connected_catalog() {
        // Terrain alone is immutable. Camps and champions are deliberately
        // absent: the executor still refreshes those obstacles before moving.
        if (cambc::fuel_remaining() < 60000) return;
        bitboard roots;
        roots.set(cambc::self().position());
        cambc::distance_field field;
        field.compute(terrain_, roots, connected_tiles_);
        connected_groups_.assign(groups_.size(), 0);
        for (unsigned i = 0; i < groups_.size(); ++i)
            connected_groups_[i] = fireflies::any_member_reachable(groups_[i].group, [this](const camp& member) {
                return connected_tiles_.is_set(member.spawn_position());
            });
        connected_ready_ = true;
    }
#endif

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
        // One native field, stopped at our tile, with a distance limit of 256.
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
        const unsigned threshold = profile::core_enabled ? profile::bank_threshold : 24;
        const auto reason = altars_.empty() ? fireflies::BankReason::none : fireflies::bank_reason(
            me.gem_count(), cambc::round_number(), cambc::constants::max_rounds, nearest, threshold);
        const bool requested = reason != fireflies::BankReason::none;
        if (profile::probe_mode && probe_released_ && !probe_decision_seen_) {
            probe_decision_seen_ = true; probe_requested_ = requested;
            event("observation", "fireflies_first_free", "\"bag\":" + std::to_string(me.gem_count())
                + ",\"threshold\":" + std::to_string(threshold) + ",\"nearest\":" + std::to_string(nearest)
                + ",\"requested\":" + boolean(requested) + ",\"reason\":\"" + fireflies::reason_name(reason) + "\"", true);
            if (requested != profile::core_enabled) {
                probe_stop("mechanism_failed", "first_free_threshold_decision_mismatch"); return true;
            }
            if (!profile::core_enabled) {
                // Still return false so this very turn executes the ordinary
                // inherited post-bank policy. The next turn parks the actor.
                probe_stop("mechanism_success", "disabled_first_free_did_not_request_bank");
            }
        }
        if (!requested) { state_ = State::farm; last_reason_ = reason; return false; }
        ++bank_turns_;
        if (reason == fireflies::BankReason::threshold && nearest > 4
            && !fireflies::late_round(cambc::round_number(), cambc::constants::max_rounds)) ++threshold_only_turns_;
        if (state_ != State::bank_requested || last_reason_ != reason) {
            ++journeys_;
            event("activation", "fireflies_bank", "\"bag\":" + std::to_string(me.gem_count())
                + ",\"threshold\":" + std::to_string(threshold) + ",\"nearest\":" + std::to_string(nearest)
                + ",\"reason\":\"" + fireflies::reason_name(reason) + "\"", profile::probe_mode);
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
        const auto origin = me.position();
        const bool moved = move_to(goals);
        if (profile::probe_mode && moved && !probe_journey_seen_) {
            probe_journey_seen_ = true;
            event("action_result", "fireflies_journey", "\"from\":" + std::to_string(tile(origin))
                + ",\"to\":" + std::to_string(tile(me.position())), true);
        }
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
#if FIREFLIES_CONNECTED_CATALOG
            // Skip statically disconnected work before spending live-query
            // fuel. The original ranking, index bias and tie order stay intact.
            if (!connected_groups_[i]) continue;
#endif
            // Reserve fuel for movement/combat and yielding. Exhaustion resumes
            // midway through an old decision next round, so stop voluntarily.
            if (cambc::fuel_remaining() < 45000) { ++planning_guards_; break; }
            auto& entry = groups_[i];
#if FIREFLIES_CAPTURE_DEFENSE
            if (entry.monolith || !fireflies::capture_group_available(round, entry.unavailable_until)) continue;
#else
            if (entry.monolith || entry.unavailable_until > round) continue;
#endif
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
        const auto tile_count = cambc::map::width()*cambc::map::height();
        if ((profile::probe_group_key >= 0 && static_cast<unsigned>(profile::probe_group_key) >= tile_count)
            || (profile::probe_tile_index >= 0 && static_cast<unsigned>(profile::probe_tile_index) >= tile_count)) {
            probe_stop("not_exercised", "fixture_key_out_of_bounds"); return false;
        }
        const int selected_key = profile::probe_group_key >= 0 ? static_cast<int>(probe_fixture_tile(profile::probe_group_key)) : -1;
        unsigned best_canonical_key = std::numeric_limits<unsigned>::max();
        for (unsigned i = 0; i < groups_.size(); ++i) {
            const auto& entry = groups_[i];
            if (entry.monolith || entry.group.gem_value() < 12 || entry.group.gem_value() > 23
                || entry.group.camps().size() != 1 || !entry.group.camps().front().is_wolf()) continue;
            if (selected_key >= 0 && entry.key != static_cast<unsigned>(selected_key)) continue;
            const unsigned canonical = probe_fixture_tile(entry.key);
            if (canonical < best_canonical_key) { best_canonical_key = canonical; probe_group_ = i; }
        }
        if (probe_group_ < 0) { probe_stop("not_exercised", "declared_solo_wolf_missing"); return false; }
        const auto& entry = groups_[probe_group_];
        probe_stage_ = entry.anchor;
        if (profile::probe_tile_index >= 0) {
            const auto index = probe_fixture_tile(profile::probe_tile_index);
            if (index >= cambc::map::width()*cambc::map::height()) { probe_stop("not_exercised", "fixture_tile_out_of_bounds"); return false; }
            probe_stage_ = {static_cast<int>(index % cambc::map::width()), static_cast<int>(index / cambc::map::width())};
        }
        if (terrain_.is_set(probe_stage_) || nearest_altar(probe_stage_) <= 4) {
            probe_stop("not_exercised", "fixture_not_outside_all_altar_radius_four"); return false;
        }
        probe_stage_selected_ = true; target_ = probe_group_;
        event("observation", "fireflies_fixture", "\"group\":\"g_" + std::to_string(entry.key)
            + "\",\"member\":" + std::to_string(tile(entry.group.camps().front().spawn_position()))
            + ",\"stage_tile\":" + std::to_string(tile(probe_stage_)) + ",\"reward\":" + std::to_string(entry.group.gem_value())
            + ",\"mirrored_home\":" + boolean(mirrored_probe_home()), true);
        return true;
    }
    bool probe_altar_route() {
        if (altars_.empty() || cambc::fuel_remaining() < 45000 || fields_this_turn_ >= 2) return false;
        // Conservative public route: never walk through another camp's original
        // spawn even if its live state is hidden. Dynamic collisions remain SDK
        // checked during the normal bank journey.
        auto walls = blocked();
        for (const auto& group : groups_) walls |= group.spawns;
        walls &= ~groups_[probe_group_].spawns;
        walls |= terrain_ | cambc::vision::champion_tiles() | cambc::vision::teleport_tiles();
        walls.set(cambc::self().position(), false);
        bitboard goals;
        for (auto altar : altars_) goals |= around(altar, cambc::constants::submit_gems_range);
        goals &= ~walls;
        if (goals.is_empty()) return false;
        ++fields_this_turn_;
        cambc::distance_field field; bitboard visited;
        field.compute_to(walls, goals, around(cambc::self().position(), 0), visited, 256);
        return field.get(cambc::self().position()) != cambc::distance_field::unreachable;
    }
    // Only compiled probe profiles enter this opening. Both arms share the
    // exact gear, route, target and combat. The only released contrast is bank12.
    bool probe_tick(const std::vector<camp>& camps, const std::vector<champion>& enemies) {
        auto& me = cambc::self();
        if (cambc::fuel_remaining() < 45000) return true;
        if (!probe_stage_selected_ && !select_probe_fixture()) return true;
        auto& entry = groups_[probe_group_];
        const auto visible = cambc::vision::visible_tiles();
        const bool group_seen = !(entry.spawns & visible).is_empty();
        std::optional<cambc::map::camp_group_state> observed_state;
        if (group_seen) observed_state = entry.group.state();
        if (!probe_capture_seen_ && me.gem_count()) {
            if (!probe_holding_ || !observed_state || !std::holds_alternative<cambc::map::camp_group_respawning>(*observed_state)
                || me.gem_count() != entry.group.gem_value()) {
                probe_stop("not_exercised", "captured_bag_not_attributed_to_declared_hold"); return true;
            }
            probe_capture_seen_ = true; probe_bag_ = me.gem_count();
            event("action_result", "fireflies_capture", "\"group\":\"g_" + std::to_string(entry.key)
                + "\",\"bag\":" + std::to_string(probe_bag_) + ",\"capture_finished\":true", true);
        }
        if (probe_capture_seen_) {
            if (me.gem_count() < 12 || me.gem_count() > 23) { probe_stop("not_exercised", "bag_outside_discriminating_interval"); return true; }
            if (me.health() < me.max_health()*0.6f) { recovering_ = true; recover(camps, enemies); return true; }
            if (me.position() != probe_stage_) { move_to(around(probe_stage_, 0)); return true; }
            if (fireflies::late_round(cambc::round_number(), cambc::constants::max_rounds)) {
                probe_stop("not_exercised", "late_game_override_active"); return true;
            }
            if (nearest_altar(me.position()) <= 4) { probe_stop("not_exercised", "nearby_altar_override_active"); return true; }
            // A finished capture is witnessed above; never release while a
            // reward remains available on our occupied region.
            if (observed_state && entry.spawns.is_set(me.position())
                && std::holds_alternative<cambc::map::camp_group_reward_available>(*observed_state)) return true;
            for (const auto& member : camps) if (member.is_alive() && member.is_awake() && member.strength() > 0
                && distance(me.position(), member.position()) <= member.attack_range()) {
                probe_stop("not_exercised", "active_camp_threat_at_trigger"); return true;
            }
            if (!enemies.empty()) {
                for (const auto& enemy : enemies) if (distance(me.position(), enemy.position()) <= 8) {
                    probe_stop("not_exercised", "enemy_present_at_trigger"); return true;
                }
            }
            if (!me.can_move() || cambc::fuel_remaining() < 45000 || fields_this_turn_ >= 2) return true;
            if (!probe_altar_route()) { probe_stop("not_exercised", "no_legal_public_altar_route"); return true; }
            probe_released_ = true; state_ = State::farm;
            event("opportunity", "fireflies_threshold_only", "\"group\":\"g_" + std::to_string(entry.key)
                + "\",\"bag\":" + std::to_string(me.gem_count()) + ",\"nearest\":" + std::to_string(nearest_altar(me.position()))
                + ",\"tile\":" + std::to_string(tile(me.position()))
                + ",\"healthy\":true,\"channel_free\":true,\"capture_finished\":true,\"public_route\":true", true);
            return false; // Execute the ordinary bank hook on this free turn.
        }
        target_ = probe_group_; partial_clear_ = false;
        if (observed_state && std::holds_alternative<cambc::map::camp_group_reward_available>(*observed_state)) {
            if (entry.spawns.is_set(me.position())) {
                if (!probe_holding_) event("observation", "fireflies_hold", "\"group\":\"g_" + std::to_string(entry.key) + "\"", true);
                probe_holding_ = true; return true;
            }
            move_to(entry.spawns); return true;
        }
        if (observed_state && std::holds_alternative<cambc::map::camp_group_respawning>(*observed_state)) {
            probe_stop("not_exercised", "declared_camp_reward_already_claimed"); return true;
        }
        std::optional<camp> target;
        for (const auto& member : camps) if (entry.spawns.is_set(member.spawn_position()) && member.is_alive()) { target = member; break; }
        if (target && fight(*target, false, true)) return true;
        const auto goals = target ? around(target->position(), me.attack_range()) : around(entry.anchor, 1);
        if (move_to(goals)) {
            for (const auto& member : cambc::vision::camps())
                if (entry.spawns.is_set(member.spawn_position()) && member.is_alive()
                    && me.attack_tiles().is_set(member.position())) { fight(member, true, true); break; }
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
        fields_this_turn_ = 0;
#if FIREFLIES_BOUNDED_STARTUP
        if (!initialized_) { initialize_bounded(); return; }
#else
        if (!initialized_) initialize();
#endif
#if FIREFLIES_CONNECTED_CATALOG
        // A real displacement (including a legal dash/teleport) may move the
        // actor into another static component. Rebuild from its current live
        // position; never reuse a home-component assumption after that move.
        if (me.is_alive() && (!connected_ready_ || !connected_tiles_.is_set(me.position()))) {
            initialize_connected_catalog();
            return;
        }
#endif
#if FIREFLIES_CAPTURE_DEFENSE
        if (capture_hold_.group() >= 0 && me.is_alive()
            && group_at_tile_[tile(me.position())] != capture_hold_.group()) capture_hold_.reset();
#endif
        telemetry_tick();
        if (profile::probe_mode) {
            if (me.champion_name() != cambc::champion_type::tavi || probe_finished_) return;
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
#if FIREFLIES_CAPTURE_DEFENSE
            capture_hold_.reset();
#endif
            shop(); recovering_ = false; target_ = -1; state_ = State::farm; last_reason_ = fireflies::BankReason::none;
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
        if (profile::probe_mode && !probe_released_ && probe_tick(camps, enemies)) return;
        // Cleared camp ownership takes precedence over banking old gems.
        const int occupied_group = group_at_tile_[tile(me.position())];
        if (occupied_group >= 0) {
            auto& entry = groups_[occupied_group];
            if (entry.spawns.is_set(me.position()) && !(entry.spawns & cambc::vision::visible_tiles()).is_empty()
                && std::holds_alternative<cambc::map::camp_group_reward_available>(entry.group.state())) {
#if FIREFLIES_CAPTURE_DEFENSE
                const unsigned round = cambc::round_number();
                if (fireflies::capture_group_available(round, entry.unavailable_until)) {
                    const auto defense = fireflies::defend_capture(enemies, [&entry](const champion& enemy) {
                        return enemy.is_visible() && entry.spawns.is_set(enemy.position());
                    }, [this](const champion& enemy) {
                        const float before = enemy.health();
                        const bool acted = fight(enemy, false, true);
                        const bool progressed = acted && (!enemy.is_visible() || enemy.health() < before);
                        return fireflies::CaptureDefenseResult{acted, progressed};
                    });
                    const unsigned capture_ticks = static_cast<unsigned>(std::max(1, cambc::constants::capture_time));
                    if (capture_hold_.keep(occupied_group, round, capture_ticks, defense.progressed, entry.unavailable_until)) {
                        target_ = occupied_group;
                        return;
                    }
                    // Do not append a new movement plan after an action. The
                    // next turn observes the temporary group suppression too.
                    if (defense.acted) return;
                }
#else
                target_ = occupied_group; return;
#endif
            }
        }
#if FIREFLIES_CAPTURE_DEFENSE
        capture_hold_.reset();
#endif
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
