#pragma once
#include <cambc.h>
#include <algorithm>
#include <vector>
#include "paths.h"
#include "reference-policy.h"
#include "telemetry.h"
#include "quiet-boundary.h"

namespace plan3_runtime {

// This adapter executes only the selected intent. It does not rank jobs, build
// forecasts or acquire ally-private state. Shopping and combat are B0 method
// bodies; navigation follows StaticPaths with a bounded local occupancy detour.
class Executor {
    using vec2 = cambc::vec2;
    using team = cambc::team;
    using champion = cambc::champion;
    using camp = cambc::camp;
    using bitboard = cambc::bitboard;
    using item_type = cambc::item_type;

    static int distance(vec2 a, vec2 b) { return static_cast<int>(a.chebyshev_distance(b)); }
    static bitboard around(vec2 p, int radius) {
        bitboard result;
        result.draw_chebyshev(p, radius);
        return result;
    }
    StaticPaths* recovery_paths_ = nullptr;
    int spawn_field_ = -1;
    Telemetry* telemetry_ = nullptr;
    unsigned hidden_attempts_ = 0;

protected:
#if PLAN3_QUIET_BOUNDARY_PACKET
    QuietBoundaryPacket quiet_boundary_;
#endif
    bitboard terrain_;
    unsigned monster_shards_ = 0, jungle_shards_ = 0;

public:
    enum class MoveStatus { unavailable, at_goal, pending_field, unreachable,
                            occupied_route, fuel_cutoff, hidden_collision, moved };

private:
    MoveStatus last_move_ = MoveStatus::unavailable;
    int detour_field_ = -1, detour_expected_ = -1, detour_baseline_ = 0;
    std::vector<int> detour_steps_;
    std::size_t detour_next_ = 0;

    void clear_detour() {
        detour_field_=-1;detour_expected_=-1;detour_baseline_=0;
        detour_steps_.clear();detour_next_=0;
    }
    bool prepare_detour(StaticPaths& paths,int fieldId,vec2 origin,const bitboard& occupied) {
        if(cambc::fuel_remaining()<45000) {last_move_=MoveStatus::fuel_cutoff;return false;}
        const int width=paths.width(),height=cambc::map::height(),tile=origin.y*width+origin.x;
        const int baseline=detour_field_==fieldId && detour_expected_==tile ? detour_baseline_:paths.get(fieldId,origin);
        detour_steps_.clear();detour_next_=0;
        detour_field_=fieldId;detour_expected_=tile;detour_baseline_=baseline;
        // Rejoin this same immutable field at a strictly better distance.
        // Blocking every tile outside the 7x7 window bounds the native search
        // to 49 legal tiles. Its final argument is a DISTANCE limit, not work.
        // SDK bitboard/field buffers still have the actual map's dimensions.
        auto window=around(origin,3);
        auto walls=occupied|~window;
        walls.set(origin,false);
        bitboard goals;
        for(int y=std::max(0,origin.y-3);y<=std::min(height-1,origin.y+3);++y) {
            if(cambc::fuel_remaining()<45000) {last_move_=MoveStatus::fuel_cutoff;return false;}
            for(int x=std::max(0,origin.x-3);x<=std::min(width-1,origin.x+3);++x) {
                const vec2 p{x,y};const int d=paths.get(fieldId,p);
                if(d>=0 && d<baseline && !walls.is_set(p))goals.set(p);
            }
        }
        if(goals.is_empty()) {last_move_=MoveStatus::occupied_route;return false;}
        if(cambc::fuel_remaining()<45000) {last_move_=MoveStatus::fuel_cutoff;return false;}
        cambc::distance_field field;bitboard visited;
        field.compute_to(walls,goals,around(origin,0),visited,48);
        auto at=origin;auto distance=field.get(at);
        if(distance==cambc::distance_field::unreachable || distance>48) {
            last_move_=MoveStatus::occupied_route;return false;
        }
        std::vector<int> steps;steps.reserve(distance);
        while(distance>0) {
            if(cambc::fuel_remaining()<30000) {last_move_=MoveStatus::fuel_cutoff;return false;}
            int best=-1;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                const vec2 p{at.x+dx,at.y+dy};
                if((!dx&&!dy) || p.x<0 || p.y<0 || p.x>=width || p.y>=height ||
                   !window.is_set(p) || walls.is_set(p))continue;
                const auto d=field.get(p);const int key=p.y*width+p.x;
                if(d!=cambc::distance_field::unreachable && d+1==distance && (best<0 || key<best))best=key;
            }
            if(best<0) {last_move_=MoveStatus::occupied_route;return false;}
            steps.push_back(best);at={best%width,best/width};--distance;
        }
        if(steps.empty() || !goals.is_set(at)) {last_move_=MoveStatus::occupied_route;return false;}
        detour_steps_=std::move(steps);
        return true;
    }

public:
    void initialize() {
        terrain_ = cambc::map::walls() | cambc::map::holes() | cambc::map::altars()
            | cambc::map::spawn_region(team::enemy);
    }
    void set_recovery_path(StaticPaths& paths, int spawnField) {
        recovery_paths_ = &paths;
        spawn_field_ = spawnField;
    }
    void set_telemetry(Telemetry* telemetry) { telemetry_ = telemetry; }
    void begin_execution_turn() { hidden_attempts_ = 0; last_move_ = MoveStatus::unavailable; }
    unsigned monster_shards() const { return monster_shards_; }
    unsigned jungle_shards() const { return jungle_shards_; }
    MoveStatus last_move_status() const { return last_move_; }
    unsigned buy_target(item_type item, unsigned limit) {
        if(!limit)return 0;
        auto& me=cambc::self();
        const unsigned cost=item.cost();
        const unsigned count=cost?std::min(limit,me.gold_count()/cost):0;
        if(!count || !me.can_buy(item,count))return 0;
        me.buy(item,count);
        if(item==item_type::monster_shard_of_strength)monster_shards_+=count;
        if(item==item_type::jungle_shard_of_health)jungle_shards_+=count;
        return count;
    }
    bool move_one(vec2 destination) {
        auto& me = cambc::self();
        last_move_ = MoveStatus::unavailable;
        if (!me.can_move() || hidden_attempts_>=3) return false;
        if (cambc::fuel_remaining()<30000) { last_move_=MoveStatus::fuel_cutoff; return false; }
        if (distance(me.position(),destination)!=1 || blocked().is_set(destination)) {
            last_move_=MoveStatus::occupied_route; return false;
        }
        if (me.move_absolute(destination)) { last_move_=MoveStatus::moved; return true; }
        ++hidden_attempts_; if (telemetry_) ++telemetry_->collisions;
        last_move_=MoveStatus::hidden_collision; return false;
    }

#include "reference/purchases.inc"
#include "reference/combat.inc"

    bitboard blocked() const {
        auto result = terrain_ | cambc::vision::champion_tiles() | cambc::vision::camp_tiles()
            | cambc::vision::teleport_tiles();
        result.set(cambc::self().position(), false);
        return result;
    }

    bool move_field(StaticPaths& paths, int fieldId, const bitboard* danger = nullptr) {
        auto& me = cambc::self();
        last_move_ = MoveStatus::unavailable;
        if (!me.can_move()) return false;
        const auto origin = me.position();
        const int here = paths.get(fieldId, origin);
        if (here == StaticPaths::unknown) { last_move_ = MoveStatus::pending_field; return false; }
        if (here == StaticPaths::unreachable) { last_move_ = MoveStatus::unreachable; return false; }
        if (here == 0) { last_move_ = MoveStatus::at_goal; return false; }
        if (cambc::fuel_remaining() < 30000) { last_move_ = MoveStatus::fuel_cutoff; return false; }
        // Fields contain immutable terrain only. Every retry rebuilds current
        // known occupancy after a hidden collision revealed its destination.
        for (int retry = 0; retry < 3 && hidden_attempts_ < 3; ++retry) {
            if(cambc::fuel_remaining()<30000) {last_move_=MoveStatus::fuel_cutoff;return false;}
            auto walls = blocked();
            if (danger) walls |= *danger;
            walls.set(origin, false);
            auto best = origin;
            int best_distance = here;
            const int origin_tile=origin.y*paths.width()+origin.x;
            bool detouring=detour_field_==fieldId && detour_expected_==origin_tile;
            if(detour_field_>=0 && !detouring)clear_detour();
            if(detouring && detour_next_<detour_steps_.size()) {
                const int tile=detour_steps_[detour_next_];
                const vec2 next{tile%paths.width(),tile/paths.width()};
                if(!walls.is_set(next))best=next;
            }
            // Honor an owned detour until its better-distance rejoin; taking
            // an available static descent mid-detour could walk straight back.
            if(!detouring)for (const auto p : walls.reachable_tiles(origin, 1).positions()) {
                const int d = paths.get(fieldId, p);
                // V3 shared adapter tie: tile index, independent of SDK bit
                // enumeration. StaticPaths caches precisely this descent.
                if (d >= 0 && d < here && (d < best_distance ||
                    (d == best_distance && p.y*paths.width()+p.x < best.y*paths.width()+best.x))) {
                    best = p; best_distance = d;
                }
            }
            if(best==origin) {
                if(!prepare_detour(paths,fieldId,origin,walls))return false;
                const int tile=detour_steps_[detour_next_];
                best={tile%paths.width(),tile/paths.width()};detouring=true;
            }
            if(cambc::fuel_remaining()<30000) {last_move_=MoveStatus::fuel_cutoff;return false;}
            if (me.move_absolute(best)) {
                if(detouring) {
                    detour_expected_=best.y*paths.width()+best.x;
                    if(++detour_next_==detour_steps_.size())clear_detour();
                }
                last_move_ = MoveStatus::moved; return true;
            }
            if(detouring) {detour_steps_.clear();detour_next_=0;}
            last_move_ = MoveStatus::hidden_collision;
            ++hidden_attempts_;
            if (telemetry_) ++telemetry_->collisions;
        }
        return false;
    }

    bool bank_at(vec2 altar, StaticPaths& paths, int fieldId) {
        auto& me = cambc::self();
        if (!me.gem_count()) return false;
        if (distance(me.position(), altar) <= 1) {
            for (const auto& lock : cambc::vision::altar_locks(team::enemy))
                if (lock.position() == altar) {
                    if (me.can_attack()) me.attack(lock);
                    return true;
                }
            me.submit_gems(altar);
            return true;
        }
        return move_field(paths, fieldId);
    }

    // A group field has every spawn as a root. Once standing on one cleared
    // member it cannot direct us toward another living visible member. This
    // bounded local approach handles that execution case without creating a
    // second path field. A wall can still stall it; the campaign records/aborts
    // that stall rather than claiming an uncomputed route.
    bool approach_visible(vec2 target, int range) {
#if PLAN3_QUIET_BOUNDARY_PACKET
        quiet_boundary_.stamp(25);
#endif
        auto& me = cambc::self();
        last_move_ = MoveStatus::unavailable;
        if (!me.can_move()) return false;
        const auto origin = me.position();
        const int here = distance(origin,target);
        if (here <= range) { last_move_ = MoveStatus::at_goal; return false; }
        if (cambc::fuel_remaining() < 30000) {
#if PLAN3_QUIET_BOUNDARY_PACKET
            quiet_boundary_.stamp(26);quiet_boundary_.reason(11);
#endif
            last_move_ = MoveStatus::fuel_cutoff; return false;
        }
        for (int retry=0;retry<3 && hidden_attempts_<3;++retry) {
            auto best=origin; int closest=here;
            for (const auto p : blocked().reachable_tiles(origin,1).positions()) {
                const int d=distance(p,target);
                if (d<here && (d<closest || (d==closest &&
                    (p.y<best.y || (p.y==best.y && p.x<best.x))))) { best=p; closest=d; }
            }
            if (best==origin) { last_move_ = MoveStatus::occupied_route; return false; }
            if (me.move_absolute(best)) { last_move_ = MoveStatus::moved; return true; }
            last_move_ = MoveStatus::hidden_collision;
            ++hidden_attempts_;
            if (telemetry_) ++telemetry_->collisions;
        }
        return false;
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
        if (best != origin && me.can_move()) {
            if (me.move_absolute(best)) return true;
            ++hidden_attempts_;
            if (telemetry_) ++telemetry_->collisions;
        }
        if (recovery_paths_) move_field(*recovery_paths_, spawn_field_, &danger);
        return true;
    }
};

} // namespace plan3_runtime
