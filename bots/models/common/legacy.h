#include <cambc.h>
#include <array>
#include <algorithm>
#include <iostream>
#include <limits>
#include <optional>
#include <variant>
#include <vector>
#include "legacy-policy.h"
#ifndef PLAN4_STARTUP_YIELD
#define PLAN4_STARTUP_YIELD 0
#endif
#if PLAN4_MACRO_MODE
#ifndef PLAN4_FRESH_DYNAMIC_GRID
#define PLAN4_FRESH_DYNAMIC_GRID 0
#endif
#if PLAN4_FRESH_DYNAMIC_GRID
#include "fresh-dynamic-grid.h"
#endif
#include <type_traits>
#include "observer.h"
#include "nav.h"
#include "movement.h"
#include "telemetry.h"
#include "phase-diagnostics.h"
#include "model-runtime.h"
#include "attempts.h"
#include "attempt-preaction-sdk.h"
#include "draft-sdk.h"
#include <bit>
#ifdef PLAN4_ONLINE_EVIDENCE
#include "online-evidence.h"
#include "phase-tracker.h"
#include "production-evidence.h"
#ifdef PLAN4_OWN_ACQUISITION
#include "own-acquisition-sdk.h"
#endif
#ifdef PLAN4_ACTOR_EVIDENCE
#include "actor-evidence.h"
#endif
#ifdef PLAN4_SPATIAL_EVIDENCE
#include "spatial-evidence.h"
#endif
#endif
#endif

namespace plan4_legacy {
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
#if PLAN4_MACRO_MODE
#ifdef PLAN4_DRAFT_PROFILE
    constexpr auto profile = PLAN4_DRAFT_PROFILE;
#else
    constexpr auto profile = plan4::drafting::Profile::reference;
#endif
    plan4::drafting::sdk::run(profile, [](auto selected_profile, auto requested, int completed, int team_size) {
        plan4::LogRow("draft", PLAN4_REFERENCE_VERSION)
            .field("profile", plan4::drafting::profile_name(selected_profile))
            .field("requested", static_cast<int>(requested)).field("completed", completed)
            .field("teamSize", team_size).emit();
    });
#else
    constexpr std::array order{cambc::champion_draft::tavi, cambc::champion_draft::varg,
        cambc::champion_draft::vendetta, cambc::champion_draft::vesper_vale,
        cambc::champion_draft::marcella};
    const auto selected = cambc::draft::current_draft(team::ally);
    for (const auto choice : order)
        if (std::find(selected.begin(), selected.end(), choice) == selected.end()) {
#if PLAN4_MACRO_MODE
            plan4::LogRow("draft").field("requested", static_cast<int>(choice)).field("completed", selected.size()).emit();
#endif
            cambc::draft::pick(choice);
            return;
        }
#endif
}

struct Group {
    cambc::map::camp_group group;
    bitboard spawns;
    vec2 anchor;
    float health = 0, incoming = 0;
    bool monolith = false;
    unsigned unavailable_until = 0;
};

class Bot {
#if PLAN4_STARTUP_YIELD
    bool startup_yielded_ = false;
#endif
#if PLAN4_MACRO_MODE
#ifdef PLAN4_SETUP_TYPE
    bool fixture_warming_ = false;
    int fixture_warmup_note_ = -1;
    bool fixture_preparing_ = false;
    int fixture_prepare_round_ = -1;
    int fixture_wait_reason_ = -1, fixture_wait_round_ = -32;
    bool (*fixture_commit_)(void*) = nullptr;
    void* fixture_commit_context_ = nullptr;
#endif
    // Isolated M0 additions compile away completely in extracted legacy mode.
#include "macro-methods.h"
#endif
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

    void initialize() {
#if PLAN4_FRESH_DYNAMIC_GRID
        ++fresh_grid_map_epoch_;
#endif
        terrain_ = cambc::map::walls() | cambc::map::holes() | cambc::map::altars()
            | cambc::map::spawn_region(team::enemy);
        altars_ = cambc::map::altars().positions();
        group_at_tile_.assign(cambc::map::width() * cambc::map::height(), -1);
        for (auto group : cambc::map::camp_groups()) {
            Group entry{group, group.spawn_tiles(), group.camps().front().spawn_position()};
            for (const auto& monster : group) {
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
        identity_ = static_cast<unsigned>(cambc::self().champion_name());
        initialized_ = true;
    }

    void shop() {
        auto& me = cambc::self();
        // A bulk buy completes one stage at a time; Tavi may do this in the field.
        for (int stage = 0; stage < 3; ++stage) {
            const auto build = fundamentals::build_stage(monster_shards_, jungle_shards_);
            auto item = item_type::power_shard_of_strength;
            unsigned limit = 100;
            if (build == fundamentals::BuildStage::monster_strength) {
                item = item_type::monster_shard_of_strength;
                limit = 5 - monster_shards_;
            } else if (build == fundamentals::BuildStage::jungle_health) {
                item = item_type::jungle_shard_of_health;
                limit = 5 - jungle_shards_;
            }
            const unsigned count = std::min(limit, me.gold_count() / item.cost());
            if (!count || !me.can_buy(item, count)) return;
#if PLAN4_MACRO_MODE
            unsigned purchase_round = 0;
            if constexpr (plan4::runtime::observes_purchases<decltype(runtime_model_)>) {
                purchase_round = cambc::round_number();
                plan4::runtime::before_purchase(runtime_model_,unsigned(item),count);
                if (cambc::round_number() != purchase_round || cambc::fuel_remaining() < 30000 || !me.can_buy(item,count)) return;
            }
#endif
#if PLAN4_MACRO_MODE
#ifdef PLAN4_ONLINE_EVIDENCE
            // The tick's same-round kit remains exact until a purchase. Invalidate
            // before the mutating SDK call so a failed/suspended buy cannot leave
            // that pre-purchase snapshot marked current.
            kit_observed_round_ = -1;
#endif
#endif
            me.buy(item, count);
            if (build == fundamentals::BuildStage::monster_strength) monster_shards_ += count;
            if (build == fundamentals::BuildStage::jungle_health) jungle_shards_ += count;
#if PLAN4_MACRO_MODE
            if constexpr (plan4::runtime::observes_purchases<decltype(runtime_model_)>) {
                if (cambc::round_number() != purchase_round || cambc::fuel_remaining() < 30000) return;
                plan4::runtime::after_purchase(runtime_model_,unsigned(item),count);
                if (cambc::round_number() != purchase_round || cambc::fuel_remaining() < 30000) return;
            }
#endif
        }
    }

    bitboard blocked() const {
        auto result = terrain_ | cambc::vision::champion_tiles() | cambc::vision::camp_tiles()
            | cambc::vision::teleport_tiles();
        result.set(cambc::self().position(), false);
        return result;
    }

    bool move_to(bitboard goals, const bitboard* danger = nullptr) {
#if PLAN4_MACRO_MODE
        return macro_move(goals, danger);
#else
        auto& me = cambc::self();
        if (!me.can_move() || goals.is_set(me.position())) return false;
        auto walls = blocked();
        if (danger) walls |= *danger;
        walls.set(me.position(), false);
        goals &= ~walls;
        if (goals.is_empty() || cambc::fuel_remaining() < 30000) return false;
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
                if (d < best_distance) { best = p; best_distance = d; }
            }
            if (best == me.position()) return false;
            if (me.move_absolute(best)) return true;
        }
        return false;
#endif
    }

    // A single visible target is attacked; refresh live validity after every
    // hit because kills and Varg splash can invalidate an earlier camp view.
    template<class Target>
    bool fight(const Target& target, bool walked, bool dangerous) {
        auto& me = cambc::self();
        if (cambc::fuel_remaining() < 16000) return false;
        auto valid = [&] { return target.is_visible() && target.health() > 0; };
        if (!valid()) return false;
        bool acted = false;
#if PLAN4_MACRO_MODE
        const auto prepare_targeted_action = [&] {
#ifdef PLAN4_ONLINE_EVIDENCE
#ifdef PLAN4_SETUP_TYPE
            if (fixture_warming_) return;
#endif
            if constexpr (std::is_same_v<Target,camp>) production_prepare_clear_target(target);
#endif
        };
        const auto targeted_action = [&] {
#ifdef PLAN4_ONLINE_EVIDENCE
#ifdef PLAN4_SETUP_TYPE
            if (fixture_warming_) return;
#endif
            if constexpr (std::is_same_v<Target,camp>)
                if (production_clear_target(target)) production_clear_action(true);
#endif
        };
#endif
        auto abilities = me.abilities();
        if (!walked) {
            // Tavi/Marcella burst from range before choosing to walk.
            for (const auto& ability : abilities)
                if ((ability.is_damage() || ability.is_cc()) && ability.is_ready()
                    && ability.valid_tiles().is_set(target.position())) {
#if PLAN4_MACRO_MODE
                    prepare_targeted_action();
                    const bool used=ability.use(target);acted |= used;
                    if (used) targeted_action();
#else
                    acted |= ability.use(target);
#endif
                    break;
                }
        }
        if (!valid()) return acted;
        const bool in_range = me.attack_tiles().is_set(target.position());
        if (!in_range) return acted;
        if (dangerous)
            for (const auto& ability : abilities)
                if (ability.is_shield() && ability.is_ready()) { acted |= ability.use(); break; }
#if PLAN4_MACRO_MODE
        if (me.can_attack()) { prepare_targeted_action();me.attack(target);acted=true;targeted_action(); }
#else
        if (me.can_attack()) { me.attack(target); acted = true; }
#endif
        if (!valid()) return acted;
        // Includes ranged champions that walked this turn: a reset explicitly
        // restores their attack allowance before the extra attack.
        if (!me.can_attack())
            for (const auto& ability : abilities)
                if (ability.is_attack_reset() && ability.is_ready()) {
#if PLAN4_MACRO_MODE
                    if (ability.use() && me.can_attack() && valid()) {
                        prepare_targeted_action();me.attack(target);targeted_action();
                    }
#else
                    if (ability.use() && me.can_attack() && valid()) me.attack(target);
#endif
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

#if PLAN4_MACRO_MODE
    bool bank(bool force = false) {
#else
    bool bank() {
#endif
        auto& me = cambc::self();
        if (!me.gem_count() || altars_.empty()) return false;
        int nearest = 10000;
        for (const auto altar : altars_) nearest = std::min(nearest, distance(me.position(), altar));
#if PLAN4_MACRO_MODE
        if (!force && !fundamentals::should_bank(me.gem_count(), cambc::round_number(), nearest)) return false;
#else
        if (!fundamentals::should_bank(me.gem_count(), cambc::round_number(), nearest)) return false;
#endif
#if PLAN4_MACRO_MODE
        attempt_legacy(plan4::attempts::Kind::bank, -1,
            force ? plan4::attempts::Origin::bootstrap : plan4::attempts::Origin::legacy);
#endif
        bitboard goals;
        for (const auto altar : altars_) {
            if (distance(me.position(), altar) <= 1) {
                // Opposing locks take three ordinary attacks. Friendly locks
                // allow tributing. Only use a fresh visible lock observation.
                for (const auto& lock : cambc::vision::altar_locks(team::enemy))
                    if (lock.position() == altar) {
                        if (me.can_attack()) me.attack(lock);
                        return true;
                    }
                me.submit_gems(altar);
#if PLAN4_MACRO_MODE
                macro_tribute_started();
#endif
                return true;
            }
            goals |= around(altar, 1);
        }
#if PLAN4_MACRO_MODE
        const bool moved = move_to(goals);
        return moved || movement_pending_;
#else
        return move_to(goals);
#endif
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
            if (cambc::fuel_remaining() < 45000) break;
            auto& entry = groups_[i];
            if (entry.monolith || entry.unavailable_until > round) continue;
            // Gold establishes the first two emblems. Once equipped, pursue
            // score rather than farming safe gold clusters indefinitely. Maps
            // whose only gems are in monolith groups retain the gold tiebreak.
            if (has_non_monolith_gems_ && monster_shards_ >= 5 && jungle_shards_ >= 5
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
            const float incoming = remaining_incoming * (jungle_shards_ >= 5 ? 0.7f : 1.0f);
            const bool affordable = reward || fundamentals::affordable_camp(health, maximum,
                remaining_health, incoming, dps * (monster_shards_ >= 5 ? 1.1f : 1.0f));
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
        target_ = selected;
        if (selected < 0) {
#if PLAN4_MACRO_MODE
            attempt_end(plan4::attempts::Outcome::aborted);
#endif
            if (!cambc::map::spawn_region(team::ally).is_set(me.position())) recovering_ = true;
            return;
        }
        auto& entry = groups_[selected];
#if PLAN4_MACRO_MODE
        attempt_legacy(plan4::attempts::Kind::one_camp,
            int(static_cast<cambc::handle_camp_group_t>(entry.group)), plan4::attempts::Origin::legacy);
#endif
        if (selected_reward) {
#if PLAN4_MACRO_MODE
            if (!move_to(entry.spawns) && !movement_pending_) entry.unavailable_until = round + 3;
#else
            if (!move_to(entry.spawns)) entry.unavailable_until = round + 3;
#endif
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
#if PLAN4_MACRO_MODE
        } else if (me.can_move() && !movement_pending_) entry.unavailable_until = round + 4;
#else
        } else if (me.can_move()) entry.unavailable_until = round + 4;
#endif
    }

public:
#if PLAN4_MACRO_MODE
#ifdef PLAN4_SETUP_TYPE
    explicit Bot(bool model_enabled = PLAN4_MODEL_ENABLED != 0) : runtime_model_(model_enabled) {}
    void fixture_suspend() { macro_abort("fixture-setup"); }
#include "fixture-methods.h"
#endif
#ifdef PLAN4_ONLINE_EVIDENCE
    void after_turn() {
#if PLAN4_MOVEMENT_RECEIPT
        movement_receipt_flush();
#endif
#if PLAN4_MOVEMENT_STORAGE_PREP
        if (movement_storage_yielded_) return;
#endif
#if PLAN4_CALLER_RECEIPT
        caller_receipt_flush();
#endif
        // startup-after-turn-begin
#if PLAN4_STARTUP_YIELD
        if (startup_yielded_) return;
#endif
        // startup-after-turn-end
        if (initialized_) {
            if (const auto* pending=production_tracker_.pending_key();
                pending && pending->phase==plan4::runtime::ProductionPhase::restore) {
                // Recall/spawn holds can return before the normal kit sampler.
                if(cambc::self().is_alive()) production_observe_kit();
                else production_cancel(plan4::runtime::PhaseCensor::interrupted);
            }
            production_poll(true);
            online_evidence_.finish(groups_);
#ifdef PLAN4_OWN_ACQUISITION
            // Final observation: no game action follows before end_turn().
            own_acquisition_.finish(groups_, group_at_tile_, int(cambc::map::width()));
#endif
        }
        macro_flush_phases();
    }
#endif
#endif
    void turn() {
#if PLAN4_CALLER_RECEIPT
        caller_receipt_begin();
        struct ReceiptFinish { Bot* bot; ~ReceiptFinish() { bot->caller_receipt_finish(); } } receipt_finish{this};
#endif
        auto& me = cambc::self();
        // startup-handoff-begin
#if PLAN4_STARTUP_YIELD
        startup_yielded_ = false;
        if (!initialized_) {
            const auto startup_round = cambc::round_number();
            initialize();
            // Immutable setup can consume most of a first gameplay turn.
            // Begin observation/actions only with the existing turn reserve
            // and a current owner clock; after_turn must respect this yield.
            if (cambc::round_number() != startup_round || cambc::fuel_remaining() < 30000) {
                startup_yielded_ = true;
                return;
            }
        }
#else
        if (!initialized_) initialize();
#endif
        // startup-handoff-end
#if PLAN4_MOVEMENT_STORAGE_PREP
        if (prepare_movement_storage()) return;
#endif
#if PLAN4_MACRO_MODE
#ifdef PLAN4_SETUP_TYPE
        if (!fixture_warming_) {
#endif
        macro_tick();
        PLAN4_CALLER_MARK(1);
        if (segment_executor_ && segment_executor_->active() && macro_segment_execute()) return;
        if constexpr (plan4::runtime::uses_producer_isolation<decltype(runtime_model_)>) {
            const unsigned isolation_round=cambc::round_number();
            if (plan4::runtime::isolation_idle(runtime_model_,{int(isolation_round),
                    {int(static_cast<cambc::handle_champion_t>(me))}}) || cambc::round_number()!=isolation_round) return;
        }
#ifdef PLAN4_SETUP_TYPE
        }
#endif
#endif
        // Dead champions retain shopping turns, but have no usable position.
        if (!me.is_alive()) { shop(); recovering_ = false; target_ = -1; return; }
        if (me.is_recalling() || me.is_submitting_gems() || me.is_teleporting()) return;
        if (!me.can_move() && !me.can_attack() && !me.can_cast()) return;
#if PLAN4_CALLER_RECEIPT
        if (auto* row=caller_receipt_.active) row->readiness=1;
#endif
#if PLAN4_MACRO_MODE
        PLAN4_CALLER_MARK(2);
#endif
        shop();
#if PLAN4_MACRO_MODE
#ifdef PLAN4_ONLINE_EVIDENCE
#ifdef PLAN4_SETUP_TYPE
        if (!fixture_warming_)
#endif
        production_observe_kit();
#endif
#endif
        const bool spawn = cambc::map::spawn_region(team::ally).is_set(me.position());
        if (spawn) {
            recovering_ = false;
            // Recovery happens at turn end; leaving on recall arrival prevents it.
            if (me.health() < me.max_health()) return;
        }
        const auto camps = cambc::vision::camps();
        const auto enemies = cambc::vision::champions(team::enemy);
        if (cambc::fuel_remaining() < 30000) return;
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
#if PLAN4_MACRO_MODE
#ifdef PLAN4_ONLINE_EVIDENCE
        if (recovering_ && production_contract_restoring()) {
            const auto point = me.position();
            const auto tile = point.y * cambc::map::width() + point.x;
            if (tile >= 0 && std::size_t(tile) < group_at_tile_.size()) {
                const int group = group_at_tile_[tile];
                if (group >= 0 && std::size_t(group) < groups_.size() &&
                    groups_[group].spawns.is_set(point) &&
                    std::holds_alternative<cambc::map::camp_group_reward_available>(groups_[group].group.state())) {
#ifdef PLAN4_OWN_ACQUISITION
                    macro_acquisition_hold(true);
#endif
                    return;
                }
            }
        }
#endif
        if (recovering_
#ifdef PLAN4_ONLINE_EVIDENCE
            && !production_contract_restoring()
#endif
        ) {
            attempt_end(plan4::attempts::Outcome::aborted);
            macro_abort("emergency-recovery");
        }
#endif
        if (recovering_) { recover(camps, enemies); return; }
        // Cleared camp ownership takes precedence over banking old gems.
        if (target_ >= 0) {
            auto& entry = groups_[target_];
#if PLAN4_MACRO_MODE
#ifdef PLAN4_OWN_ACQUISITION
            if (entry.spawns.is_set(me.position()) && std::holds_alternative<cambc::map::camp_group_reward_available>(entry.group.state())) {
                macro_acquisition_hold();
                return;
            }
#else
            if (entry.spawns.is_set(me.position()) && std::holds_alternative<cambc::map::camp_group_reward_available>(entry.group.state())) return;
#endif
#else
            if (entry.spawns.is_set(me.position()) && std::holds_alternative<cambc::map::camp_group_reward_available>(entry.group.state())) return;
#endif
        }
#if PLAN4_MACRO_MODE
#ifdef PLAN4_SETUP_TYPE
        if (!fixture_warming_) {
#endif
#if PLAN4_CALLER_RECEIPT
        const bool macro_handled=macro_decide(camps, enemies);
        if (auto* row=caller_receipt_.active) row->macro_result=macro_handled;
        PLAN4_CALLER_MARK(12);
        if (macro_handled) return;
#else
        if (macro_decide(camps, enemies)) return;
#endif
        // A failed/aborted macro may have spent its execution reserve already.
        // Re-admit the inherited fallback before any more SDK actions.
        if (cambc::fuel_remaining() < 60000) return;
#ifdef PLAN4_SETUP_TYPE
        }
#endif
#endif
        if (bank()) return;
        // Defend against an adjacent threat, without chasing opponents.
        for (const auto& enemy : enemies)
            if (distance(me.position(), enemy.position()) <= 1 && fight(enemy, false, true)) return;
        if (!spawn && me.champion_name() != cambc::champion_type::tavi && me.gold_count() >= 300) {
#if PLAN4_MACRO_MODE
            attempt_end(plan4::attempts::Outcome::aborted);
#endif
            recovering_ = true;
            recover(camps, enemies);
            return;
        }
#if PLAN4_MACRO_MODE
        PLAN4_CALLER_MARK(13);
#endif
        farm(camps, cambc::vision::champions(team::ally));
#if PLAN4_MACRO_MODE
        PLAN4_CALLER_MARK(14);
#endif
    }
};
} // namespace

