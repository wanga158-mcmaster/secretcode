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
#include "hoard-policy.h"
#include "telemetry.h"

#ifndef DRAGONS_CURRENT_CAMPS
#define DRAGONS_CURRENT_CAMPS 0
#endif
#ifndef DRAGONS_ACTION_RESERVE
#define DRAGONS_ACTION_RESERVE 0
#endif
#ifndef DRAGONS_CAPTURE_DEFENSE
#define DRAGONS_CAPTURE_DEFENSE 0
#endif
#ifndef DRAGONS_LOADED_RETURNABILITY
#define DRAGONS_LOADED_RETURNABILITY 0
#endif
#ifndef DRAGONS_LOADED_ESCAPE
#define DRAGONS_LOADED_ESCAPE 0
#endif
#ifndef DRAGONS_CAPITAL_RECOVERY
#define DRAGONS_CAPITAL_RECOVERY 0
#endif
#ifndef DRAGONS_CAPITAL_ROUTE
#define DRAGONS_CAPITAL_ROUTE 0
#endif
#ifndef DRAGONS_PRISM_AREA
#define DRAGONS_PRISM_AREA 0
#endif
#ifndef DRAGONS_PRISM_AREA_CAST
#define DRAGONS_PRISM_AREA_CAST 1
#endif

namespace {
#if DRAGONS_PRISM_AREA
// Both measurement arms retain identical instructions and inspection work.
// Nonzero tag keeps the final 0/1 switch in initialized data, including OFF.
inline volatile std::uint32_t hoard_area_cast_word=0xa8260000u|DRAGONS_PRISM_AREA_CAST;
#endif
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
    return hoard::channel(gems, cambc::constants::submit_gem_minimum_duration,
        cambc::constants::submit_gem_base_duration, cambc::constants::submit_gem_duration_multiplier);
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
#if DRAGONS_CAPITAL_ROUTE
template<bool Capital=false>
#endif
std::vector<Group> public_groups(
#if DRAGONS_CAPITAL_ROUTE
    std::vector<hoard::PublicMonolith>* hazards=nullptr
#endif
) {
    std::vector<Group> result;
    // Public immutable designs are shared by kind. Avoid one SDK design query
    // per physical monster on large maps during the first gameplay turn.
#if DRAGONS_CURRENT_CAMPS
    // The current public catalog includes Golem after Monolith. Preserve each
    // kind's runtime stats; a Golem must not abort every actor's initialization.
    std::array<std::optional<cambc::camp_stats>,static_cast<unsigned>(cambc::camp_type::golem)+1> designs;
#else
    std::array<std::optional<cambc::camp_stats>,5> designs;
#endif
    for (auto group : cambc::map::camp_groups()) {
        Group g{group, group.spawn_tiles(), group.camps().front().spawn_position()};
        g.id = std::numeric_limits<int>::max();
        for (const auto& monster : group) {
            const auto p = monster.spawn_position();
            if (tile_index(p) < g.id) { g.id = tile_index(p); g.anchor = p; }
            auto& design=designs.at(static_cast<unsigned>(monster.kind()));
            if(!design) design=monster.base_stats();
            const auto& stats=*design;
            g.health += stats.base_health; g.incoming += stats.base_strength;
#if DRAGONS_CAPITAL_ROUTE
            const bool monolith=monster.is_monolith();g.monolith|=monolith;
            if constexpr(Capital)if(monolith&&hazards)
                hazards->push_back({p.x,p.y,stats.base_attack_range,stats.base_strength});
#else
            g.monolith |= monster.is_monolith();
#endif
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
    unsigned capture_bag_ = 0;
    int capture_group_ = -1;
    float previous_focus_ = 0, previous_raw_ = 0;

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
    bool is_actor(cambc::champion_type name) const { return identity_ == static_cast<unsigned>(name); }
    bool pair_actor() const { return is_actor(cambc::champion_type::cassi) || is_actor(cambc::champion_type::issac); }
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
#if DRAGONS_CAPITAL_ROUTE
        identity_=static_cast<unsigned>(cambc::self().champion_name());
        if(holding_prism())groups_=public_groups<true>(&hoard_public_monoliths_);
        else groups_=public_groups<false>();
#else
        groups_ = public_groups();
#endif
        for (const auto& g : groups_) has_non_monolith_gems_ |= !g.monolith && g.group.gem_value() > 0;
#if !DRAGONS_CAPITAL_ROUTE
        identity_ = static_cast<unsigned>(cambc::self().champion_name());
#endif
        initialized_ = true;
    }
    void shop() {
        auto& me = cambc::self();
        core_shop();
        if constexpr (profile::probe_mode && profile::core_number == 15) return;
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
#if DRAGONS_CAPITAL_ROUTE
    bool capital_mask_ready() {
        if(hoard_public_monoliths_.empty())return true;
        if(!hoard_monolith_mask_)hoard_monolith_mask_.emplace();
        while(hoard_monolith_cursor_<hoard_public_monoliths_.size()) {
            if(cambc::fuel_remaining()<45000)return false;
            const auto& hazard=hoard_public_monoliths_[hoard_monolith_cursor_];
            if(hazard.damage>0)*hoard_monolith_mask_|=around({hazard.x,hazard.y},hazard.range);
            ++hoard_monolith_cursor_;
        }
        return true;
    }
    bool capital_loaded_transit() const {
        return holding_prism()&&hoard_bag_>0&&!hoard_public_monoliths_.empty();
    }
#endif
#if DRAGONS_LOADED_ESCAPE
    struct CapitalFrame {
        bool active=false;
        float health=0,maximum=0,recent_loss=0;
        vec2 origin{};
        std::vector<hoard::EscapeContact> contacts;
        float exposure(vec2 p) const {
            float loss=0;
            for(const auto& contact:contacts)loss+=hoard::basic_exposure(p.x,p.y,contact);
            return loss;
        }
        int clearance(vec2 p) const {
            int result=1000000;
            for(const auto& contact:contacts)result=std::min(result,hoard::separation(p.x,p.y,contact.x,contact.y));
            return result;
        }
        bool permits(vec2 p) const {
            return !active||hoard::capital_step(true,health,maximum,recent_loss,exposure(origin),exposure(p));
        }
    };
    CapitalFrame capital_frame() const {
        CapitalFrame frame;
        if(!holding_prism()||hoard_recent_damage_<=0)return frame;
        if(!hoard_bag_||hoard_escape_contacts_.empty())return frame;
        frame.health=hoard_current_health_;frame.maximum=hoard_current_maximum_;frame.origin=hoard_escape_origin_;
        frame.recent_loss=hoard_recent_damage_;
        frame.contacts=hoard_escape_contacts_;frame.active=true;
        return frame;
    }
#endif
    bool ordinary_move(bitboard goals, const bitboard* avoid = nullptr) {
        auto& me = cambc::self();
        if (!me.can_move() || goals.is_set(me.position())) return false;
#if DRAGONS_CAPITAL_ROUTE
        auto transport=hoard::CapitalTransit::ordinary;
        if(capital_loaded_transit()) {
            if(!capital_mask_ready())return false;
            transport=hoard::capital_transit(true,true,hoard_monolith_mask_->is_set(me.position()));
            if(transport==hoard::CapitalTransit::egress)goals=~*hoard_monolith_mask_&~terrain_;
        }
#endif
#if DRAGONS_LOADED_ESCAPE
        auto capital=capital_frame();
#endif
#if DRAGONS_ACTION_RESERVE
        // No other actor runs between these reads and the attempted move.
        // Reuse the same occupancy snapshot until an actual failed action can
        // reveal a hidden blocker; refresh it before a retry in that case.
        auto live_walls = blocked();
        auto walls = live_walls; if (avoid) walls |= *avoid;
#else
        auto walls = blocked(); if (avoid) walls |= *avoid;
#endif
#if DRAGONS_CAPITAL_ROUTE
        if(transport==hoard::CapitalTransit::avoid)walls|=*hoard_monolith_mask_;
#endif
        walls.set(me.position(), false); goals &= ~walls;
        Field route;
        if (!field(route, goals, walls, false) || route.get(me.position()) == unreachable) return false;
        for (int retry = 0; retry < 3; ++retry) {
#if DRAGONS_ACTION_RESERVE
            const auto legal = live_walls.reachable_tiles(me.position(),static_cast<std::uint16_t>(me.base_stats().base_move_speed)) & ~walls;
#else
            const auto legal = legal_walk() & ~walls & ~blocked();
#endif
            auto best = me.position(); auto best_d = route.get(best);
#if DRAGONS_LOADED_ESCAPE
            auto unguarded_best=best;auto unguarded_d=best_d;
#endif
            for (const auto p : legal.positions()) {
                const auto d = route.get(p);
#if DRAGONS_LOADED_ESCAPE
                if(d<unguarded_d||(d==unguarded_d&&d<route.get(me.position())&&tile_index(p)<tile_index(unguarded_best))) {
                    unguarded_best=p;unguarded_d=d;
                }
                if(!capital.permits(p))continue;
#endif
                if (d < best_d || (d == best_d && d < route.get(me.position()) && tile_index(p) < tile_index(best))) { best = p; best_d = d; }
            }
#if DRAGONS_LOADED_ESCAPE
            if(capital.active&&best==me.position()&&unguarded_best!=me.position()&&!capital.permits(unguarded_best)) {
                // Rejecting a loaded approach is not permission to wait under
                // continuing damage. Take a legal evasive step, even backward.
                float loss=capital.exposure(best);int clearance=capital.clearance(best);
                for(const auto p:legal.positions()) if(capital.permits(p)) {
                    const float next_loss=capital.exposure(p);const int next_clearance=capital.clearance(p);
                    if(next_loss<loss||(next_loss==loss&&(next_clearance>clearance||
                        (next_clearance==clearance&&(route.get(p)<route.get(best)||
                         (route.get(p)==route.get(best)&&tile_index(p)<tile_index(best))))))) {
                        best=p;loss=next_loss;clearance=next_clearance;
                    }
                }
#if DRAGONS_CAPITAL_RECOVERY
                // The rejected capital approach stays rejected through lost
                // vision; return toward recovery instead of undoing this step.
                hoard_capital_recovery_.reject_approach();recovering_=true;target_=-1;
#endif
            }
#endif
            if (best == me.position()) return false;
#if DRAGONS_ACTION_RESERVE
            // The successful move refreshes native actor/vision state too.
            // A route proof is not permission to suspend inside that action.
            if (cambc::fuel_remaining() < 30000) return false;
#endif
            if (me.move_absolute(best)) return true;
#if DRAGONS_LOADED_ESCAPE
            // A failed step can reveal a new opponent. Do not use the old
            // contact frame for another guarded capital move in that owner turn.
            if(holding_prism()&&hoard_bag_>0&&hoard_recent_damage_>0)return false;
#endif
#if DRAGONS_ACTION_RESERVE
            live_walls = blocked(); walls = live_walls; if (avoid) walls |= *avoid;
#if DRAGONS_CAPITAL_ROUTE
            if(transport==hoard::CapitalTransit::avoid)walls|=*hoard_monolith_mask_;
#endif
            walls.set(me.position(), false);
#if DRAGONS_LOADED_ESCAPE
            capital=capital_frame();
#endif
#endif
        }
        return false;
    }
    bool move_to(bitboard goals, const bitboard* avoid = nullptr) {
        if (core_move(goals, avoid)) return true;
        return ordinary_move(goals, avoid);
    }
#if DRAGONS_PRISM_AREA
    std::optional<bool> capital_area(const camp& target,const std::array<cambc::ability,2>& abilities,float primary_raw) {
        auto& me=cambc::self();
        if(cambc::fuel_remaining()<50000||target_<0||target_>=static_cast<int>(groups_.size()))return {};
        const auto& group=groups_[target_];
        if(group.group.camps().size()<2||!group.spawns.is_set(target.spawn_position()))return {};
        const auto& area=abilities[1];
        if(!area.is_aoe_damage())return {};
        const auto design=area.get<cambc::ability_aoe_damage>();
        // Constructor-cached queried properties and the original ready S0
        // already prove can_cast. No actor/action can intervene between reads.
        if(!design.valid_targets.contains(cambc::ability_target_mask::ground)||
            !design.valid_targets.contains(cambc::ability_target_mask::camps)||area.cooldown()!=0)return {};
        const auto center=target.position(),origin=me.position();
        if(distance(origin,center)>design.range)return {};
        const float radius=design.radius;
        if(!cambc::vision::champions(team::enemy).empty())return {};
        const float multiplier=me.base_stats().camp_damage_multiplier*
            (monster_passive_?1.0f+item_type::monster_emblem_of_strength.passive_magnitude():1.0f);
        const float basic=me.strength()*multiplier,area_damage=area.damage_amount()*multiplier;
        const bool basic_available=me.can_attack()&&me.attack_tiles().is_set(center);
        // Caller has already selected a ready, legal ordinary slot-zero cast.
        // Reuse its receipt; movement paths never perform this inspection.
        const float primary_damage=primary_raw*multiplier;
        std::vector<hoard::AreaVictim> victims;
        float health=0,incoming=0;
        for(const auto& monster:group.group) {
            if(cambc::fuel_remaining()<36000||!monster.is_visible())return {};
            if(!monster.is_alive())continue;
            const auto position=monster.position();const float hp=monster.health(),strength=monster.strength();
            const bool hit=distance(center,position)<=radius;
            if(hit&&strength>0&&!hoard::no_new_camp_hit(monster.is_awake(),strength,
                distance(origin,position),monster.attack_range()))return {};
            victims.push_back({hp,hit,monster.spawn_position()==target.spawn_position()});
            health+=hp;incoming+=strength;
        }
        const float dps=(me.strength()*1.125f+me.focus()*0.12f)*multiplier;
        if(jungle_passive_)incoming*=1.0f-item_type::jungle_emblem_of_health.passive_magnitude();
        if(!fundamentals::affordable_camp(me.health(),me.max_health(),health,incoming,dps))return {};
        if(cambc::fuel_remaining()<45000)return {};
        const auto area_value=hoard::area_outcome(victims,area_damage,basic,true,basic_available);
        const auto ordinary=hoard::area_outcome(victims,primary_damage,basic,false,basic_available);
        if(!hoard::prefer_area(area_value,ordinary))return {};
        const auto footprint=around(center,static_cast<int>(radius));
        if(!(footprint&~cambc::vision::visible_tiles()).is_empty())return {};
        // Inspect immutable foreign spawns only after a productive proposal.
        // Unknown live activity never licenses hitting another group.
        for(const auto& other:groups_) {
            if(cambc::fuel_remaining()<42000)return {};
            if(other.id==group.id)continue;
            for(const auto& monster:other.group)
                if(distance(center,monster.spawn_position())<=radius)return {};
        }
        // Full personal footprint vision plus exact live occupancy excludes
        // displaced foreign campers as well as the immutable-spawn veto.
        if((footprint&cambc::vision::camp_tiles()).positions().size()!=area_value.targets)return {};
        if(cambc::fuel_remaining()<32000)return {};
        if((hoard_area_cast_word&1u)==0)return {};
        const bool success=area.use(center);
        if(success)telemetry_.event("action_result","hoard_area",number("rawDamage",area_damage/multiplier)
            +","+number("campDamage",area_damage)+","+integer("targets",area_value.targets)
            +","+integer("ordinaryBasics",ordinary.remaining_basics)+","+integer("areaBasics",area_value.remaining_basics)
            +","+integer("bag",hoard_bag_));
        return success;
    }
#endif
    template<class Target> bool fight(const Target& target, bool walked, bool dangerous) {
        auto& me = cambc::self();
#if DRAGONS_ACTION_RESERVE
        // Casting, its inventory-power receipt, then a fresh basic/reset chain
        // must all fit this owner turn. Do not enter that chain on 16K fuel.
        if (cambc::fuel_remaining() < 30000) return false;
#else
        if (cambc::fuel_remaining() < 16000) return false;
#endif
        auto valid = [&] { return target.is_visible() && target.health() > 0; };
        if (!valid()) return false;
        bool acted = false;
        const auto abilities = me.abilities();
        if (!walked) for (const auto& ability : abilities) {
            const bool category = [&] { if constexpr (std::is_same_v<Target, camp>) return ability.can_target_camps(); else return ability.can_target_enemies(); }();
            if ((ability.is_damage() || ability.is_cc()) && category && ability.is_ready() && ability.valid_tiles().is_set(target.position())) {
                if constexpr (profile::core_number == 15) {
                    if (is_actor(cambc::champion_type::prism) && ability.slot() != 0) continue;
                }
                const float before = target.health(), raw = ability.damage_amount();
#if DRAGONS_PRISM_AREA
                if constexpr(std::is_same_v<Target,camp>) {
                    if(hoard_bag_>0&&holding_prism()&&ability.slot()==0) {
                        if(const auto area=capital_area(target,abilities,raw)) {
                            if(!*area)return true; // fresh hidden obstruction; no stale follow-up
                            acted=true;break;
                        }
                    }
                }
#endif
                acted = ability.use(target);
                if (acted && !profile::probe_mode && is_actor(cambc::champion_type::prism))
                    telemetry_.event("action_result", "hoard_cast", number("rawDamage", raw) + "," + number("healthBefore", before)
                        + "," + (target.is_visible() ? number("healthAfter", target.health()) : "\"healthAfter\":null"));
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
#if DRAGONS_CAPITAL_ROUTE
        bool capital_avoid=false;
        if(capital_loaded_transit()) {
            // A partial public mask is not permission for an unguarded direct
            // step. Continue its bounded construction on the next owner turn.
            if(!capital_mask_ready())return true;
            capital_avoid=!hoard_monolith_mask_->is_set(origin);
            if(!capital_avoid) {
                // Prove an actual egress route. If none exists, retain the old
                // observation-driven recovery rather than hard-block forever.
                const auto avoid=danger(camps,enemies);
                if(ordinary_move(~*hoard_monolith_mask_&~terrain_,&avoid))return true;
            }
        }
#endif
#if DRAGONS_LOADED_ESCAPE
        const auto capital=capital_frame();
#endif
        bool safe = !enemy_near(origin, enemies); const auto avoid = danger(camps, enemies);
        for (const auto& m : camps) if (m.is_alive() && m.is_awake() && m.strength() > 0 && distance(origin, m.position()) <= m.attack_range() + 1) safe = false;
#if DRAGONS_LOADED_ESCAPE && DRAGONS_CAPITAL_RECOVERY
        if(holding_prism())safe=hoard_capital_recovery_.may_recall(safe,hoard_recent_damage_);
#endif
        if (safe) { me.recall(); return true; }
        auto best = origin; float best_score = -1e30f;
        for (const auto p : legal_walk().positions()) {
#if DRAGONS_CAPITAL_ROUTE
            if(capital_avoid&&hoard_monolith_mask_->is_set(p))continue;
#endif
#if DRAGONS_LOADED_ESCAPE
            if(!capital.permits(p))continue;
#endif
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
        if (is_actor(cambc::champion_type::prism) && profile::core_enabled && !profile::draft_only) return hoard_bank();
        bitboard goals;
        for (const auto altar : altars_) {
            if (distance(me.position(), altar) <= cambc::constants::submit_gems_range) {
                for (const auto& lock : cambc::vision::altar_locks(team::enemy)) if (lock.position() == altar) {
                    if (me.can_attack() && me.attack_tiles().is_set(altar)) me.attack(lock);
                    return true;
                }
                me.submit_gems(altar);
                previous_submit_ = true;
                if (is_actor(cambc::champion_type::prism)) {
                    hoard_previous_submit_ = true;
                    if (!hoard_first_bank_) hoard_first_bank_ = cambc::round_number();
                }
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
        const auto origin=me.position(); const float own_health=me.health(),own_maximum=me.max_health();
        struct CampObservation {vec2 spawn;float health,strength;};
        std::vector<CampObservation> observed;
        observed.reserve(camps.size());
        for(const auto& m:camps) if(m.is_alive()) observed.push_back({m.spawn_position(),m.health(),m.strength()});
        std::vector<vec2> ally_positions;
        for(const auto& ally:allies) if(ally!=me) ally_positions.push_back(ally.position());
        std::vector<Choice> result;
        for (std::size_t i = 0; i < groups_.size(); ++i) {
#if DRAGONS_ACTION_RESERVE
            // Retain enough for blocked masks, one route, fresh legal steps
            // and the native move. Keep the original source scores and ties.
            if (cambc::fuel_remaining() < 65000) break;
#else
            if (cambc::fuel_remaining() < 45000) break;
#endif
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
                for (const auto& m : observed) if (g.spawns.is_set(m.spawn)) { health += m.health; incoming += m.strength; }
            }
            incoming *= jungle_passive_ ? 0.7f : 1.0f;
            const float damage = dps * (monster_passive_ ? 1.1f : 1.0f);
            const float loss = incoming * (health / std::max(1.0f, damage) + 2);
            if (!core_group_allowed(static_cast<int>(i), loss)) continue;
            const bool affordable = available || fundamentals::affordable_camp(own_health, own_maximum, health, incoming, damage);
            float travel = static_cast<float>(distance(origin, g.anchor));
            for (const auto p : ally_positions) if (distance(p, g.anchor) <= 3) travel += 12;
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
#if DRAGONS_CAPITAL_ROUTE
            const bool moved=move_to(g.spawns);
            if(!moved&&capital_loaded_transit()&&me.can_move())g.unavailable_until=cambc::round_number()+4;
#else
            move_to(g.spawns); return true;
#endif
            return true;
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
        fields_ = 0; ++turn_;
#if !DRAGONS_ACTION_RESERVE
        origin_field_.reset();
#endif
        auto& me = cambc::self(); if (!initialized_) initialize();
#if DRAGONS_ACTION_RESERVE
        // Altar projection uses only immutable terrain and this origin. Build
        // that full field as bounded preparation on a fresh owner turn, then
        // yield; never add it after a capture receipt and ordinary work have
        // already spent the action reserve. Reuse it only at the same origin.
        if(holding_prism()&&me.is_alive()&&me.gem_count()>0&&
            (!origin_field_||origin_field_position_!=me.position())) {
#if DRAGONS_LOADED_RETURNABILITY
            // Early loaded movement must retain its next action/escape turn.
            // Prepare only when a complete field could affect the unchanged
            // bank request; all actual altar selection still uses that field.
            if(hoard::projection_required(me.gem_count(),cambc::round_number(),
                cambc::constants::max_rounds,cambc::global_submitted_gems(team::ally),
                cambc::constants::winning_gems,static_cast<unsigned>(channel_duration(me.gem_count())),
                cambc::constants::altar_lock_hits,profile::bank_threshold,
                profile::deadline_reserve,profile::projection_reserve)) {
                if(cambc::fuel_remaining()>=80000)ensure_origin();
                return;
            }
#else
            if(cambc::fuel_remaining()>=80000)ensure_origin();
            return;
#endif
        }
#endif
        telemetry_.tick();
        const auto score = cambc::global_submitted_gems(team::ally);
        if (!is_actor(cambc::champion_type::prism)) {
            if (previous_alive_ && !me.is_alive()) telemetry_.event("observation", "own_death", integer("previousBag", previous_bag_), true);
            if (previous_submit_ && !me.is_submitting_gems() && me.gem_count() == 0 && score > previous_score_)
                telemetry_.event("observation", "tribute_complete", integer("scoreDelta", score - previous_score_) + "," + integer("previousBag", previous_bag_), true);
        }
        core_observe();
        previous_bag_ = me.gem_count(); previous_score_ = score; previous_alive_ = me.is_alive(); previous_submit_ = me.is_submitting_gems();
        if (!me.is_alive()) {
#if DRAGONS_LOADED_ESCAPE
            hoard_health_observed_=false;hoard_recent_damage_=0;hoard_escape_contacts_.clear();
#if DRAGONS_CAPITAL_RECOVERY
            hoard_capital_recovery_.observe(false,false,false);
#endif
#endif
            shop(); recovering_ = false; target_ = -1; return;
        }
        if (me.is_recalling() || me.is_submitting_gems() || me.is_teleporting()) return;
        if (!me.can_move() && !me.can_attack() && !me.can_cast()) return;
        shop();
        const bool spawn = cambc::map::spawn_region(team::ally).is_set(me.position());
        if (spawn) {
#if DRAGONS_LOADED_ESCAPE
            hoard_health_observed_=false;hoard_recent_damage_=0;hoard_escape_contacts_.clear();
#if DRAGONS_CAPITAL_RECOVERY
            hoard_capital_recovery_.observe(true,hoard_bag_>0,true);
#endif
#endif
            recovering_ = false; if (me.health() < me.max_health()) return;
        }
        const auto camps = cambc::vision::camps(); const auto enemies = cambc::vision::champions(team::enemy);
        if (cambc::fuel_remaining() < 30000) return;
        float incoming = 0;
        for (const auto& m : camps) if (m.is_alive() && m.is_awake() && distance(me.position(), m.position()) <= m.attack_range()) incoming += m.strength();
#if DRAGONS_LOADED_ESCAPE
        const bool capital_actor=holding_prism();
        if(!spawn&&capital_actor) {
            // Reuse the same self HP/max queries the old retreat test performs.
            // The inactive/no-contact path adds no SDK getter or vision query.
            hoard_current_health_=me.health();hoard_current_maximum_=me.max_health();
            hoard_recent_damage_=hoard_health_observed_&&hoard_previous_health_turn_+1==turn_?
                std::max(0.0f,hoard_previous_health_-hoard_current_health_):0.0f;
            hoard_previous_health_=hoard_current_health_;hoard_previous_health_turn_=turn_;hoard_health_observed_=true;
            hoard_escape_contacts_.clear();
        }
        if(capital_actor&&hoard_bag_>0&&hoard_recent_damage_>0) {
            for(const auto& enemy:enemies) {
                const auto origin=me.position(),p=enemy.position();const int range=enemy.attack_range();
                const bool near=distance(origin,p)<=range+1;
                float strength=near?enemy.strength():0.0f;if(near)incoming+=strength;
                const int reach=range+static_cast<int>(enemy.base_stats().base_move_speed);
                if(distance(origin,p)>reach+1)continue;
                if(!near)strength=enemy.strength();
                bool reset=false;
                for(const auto& ability:enemy.abilities())
                    if(ability.is_attack_reset()&&ability.cooldown()<=1)reset=true;
                hoard_escape_origin_=origin;
                hoard_escape_contacts_.push_back({p.x,p.y,reach,strength*(reset?2.0f:1.0f)});
            }
        } else for (const auto& e : enemies) if (distance(me.position(), e.position()) <= e.attack_range() + 1) incoming += e.strength();
        const bool retreat=!spawn&&(capital_actor?
            fundamentals::should_retreat(hoard_current_health_,hoard_current_maximum_,incoming*(partial_clear_?2.0f:1.0f)):
            fundamentals::should_retreat(me.health(),me.max_health(),incoming*(partial_clear_?2.0f:1.0f)));
        if(!spawn&&(retreat||core_retreat(enemies)))recovering_=true;
#if DRAGONS_CAPITAL_RECOVERY
        if(capital_actor) {
            hoard_capital_recovery_.observe(true,hoard_bag_>0,hoard_current_health_>=hoard_current_maximum_);
            if(hoard_capital_recovery_.active())recovering_=true;
        }
#endif
#else
        for (const auto& e : enemies) if (distance(me.position(), e.position()) <= e.attack_range() + 1) incoming += e.strength();
        if (!spawn && (fundamentals::should_retreat(me.health(), me.max_health(), incoming * (partial_clear_ ? 2.0f : 1.0f)) || core_retreat(enemies))) recovering_ = true;
#endif
        if (recovering_) { core_recovery(); recover(camps, enemies); return; }
        for (std::size_t i = 0; i < groups_.size(); ++i) if (groups_[i].spawns.is_set(me.position()) && reward(groups_[i]) && !core_departing(static_cast<int>(i))) {
            target_ = static_cast<int>(i);
#if DRAGONS_CAPTURE_DEFENSE
            // An enemy on this same reward region prevents capture. Preserve
            // the hold, but do not suppress the existing adjacent-enemy fight.
            // Retreat and recovery have already run before this branch.
            for (const auto& e : enemies)
                if (groups_[i].spawns.is_set(e.position()) && distance(me.position(), e.position()) <= 1 &&
                    !e.is_submitting_gems() && fight(e, false, true)) return;
#endif
            return;
        }
        if (bank()) return;
#if DRAGONS_ACTION_RESERVE
        if(cambc::fuel_remaining()<40000)return;
#endif
        for (const auto& e : enemies) if (distance(me.position(), e.position()) <= 1 && !e.is_submitting_gems() && fight(e, false, true)) return;
        const auto allies = cambc::vision::champions(team::ally);
        if (core_turn(camps, enemies, allies)) return;
        if (!spawn && !is_actor(cambc::champion_type::tavi) && me.gold_count() >= 300) { recovering_ = true; core_recovery(); recover(camps, enemies); return; }
#if DRAGONS_ACTION_RESERVE
        // choices has live observation setup before its per-source guard.
        if(cambc::fuel_remaining()<65000)return;
#endif
        farm(camps, allies);
    }
};
} // namespace
