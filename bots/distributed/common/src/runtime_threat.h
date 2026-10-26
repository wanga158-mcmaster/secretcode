#pragma once
#include "observe.h"
#include <limits>

namespace p6::runtime_threat {
struct Slot {
    bool known=true, reset=false, dash=false, harmful=false, exclusive_movement=false;
    double range=0, radius=0;
};
struct Enemy {
    int id=-1;
    Point position{};
    double stress=0;
    int move_speed=-1, attack_range=-1;
    bool ranged=false, completed_turn=false, complete=false;
    double splash_radius=0;
    std::array<Slot,2> slots{};
};
struct Entry {
    int id=-1;
    Point position{};
    double stress=0, reach=std::numeric_limits<double>::infinity();
    int phases=2;
};
struct Prepared {
    ViewClock clock{};
    std::array<Entry,max_actors> entries{};
    int count=0;
    bool complete=false;
};

// Geometry only: an infinite result means that no reach discount is available.
// Each phase chooses at most one cast. Readiness, cooldowns, terrain and future
// target visibility never narrow this envelope.
inline double max_reach(const Enemy& enemy,int phases) {
    const double unknown=std::numeric_limits<double>::infinity();
    const auto nonnegative=[](double value) {
        return std::isfinite(value) && value>=0;
    };
    if(!enemy.complete || phases<1 || phases>2 || enemy.move_speed<0 ||
       enemy.attack_range<0 || !nonnegative(enemy.splash_radius))return unknown;
    for(const auto& slot:enemy.slots)
        if(!slot.known || !nonnegative(slot.range) || !nonnegative(slot.radius) ||
           (slot.reset && (slot.dash || slot.harmful)))return unknown;
    const double walk=enemy.move_speed;
    const double basic=enemy.attack_range+enemy.splash_radius;
    double prefix=0,reach=0;
    for(int phase=0;phase<phases;++phase) {
        // Ordinary movement shares the ranged basic allowance. A reset is a
        // separate cast choice below, never added to a same-phase dash/cast.
        reach=std::max(reach,prefix+(enemy.ranged?0:walk)+basic);
        double next_prefix=prefix+walk;
        for(const auto& slot:enemy.slots) {
            const double compatible_walk=slot.exclusive_movement?0:walk;
            if(slot.reset)
                reach=std::max(reach,prefix+compatible_walk+basic);
            if(slot.dash) {
                const double landing=prefix+compatible_walk+slot.range;
                next_prefix=std::max(next_prefix,landing);
                // Allow a basic after a dash conservatively, but ranged actors
                // cannot also gain ordinary walking without a second cast.
                reach=std::max(reach,prefix+slot.range+
                    (enemy.ranged?0:compatible_walk)+basic);
                if(slot.harmful)reach=std::max(reach,landing+slot.radius);
            } else if(slot.harmful) {
                reach=std::max(reach,prefix+compatible_walk+slot.range+slot.radius);
            }
        }
        if(!std::isfinite(reach) || !std::isfinite(next_prefix))return unknown;
        prefix=next_prefix;
    }
    return reach;
}

// Validate the personal list and enumerate slots once per observation. A failed
// preparation never publishes its partial prefix as a complete snapshot.
inline Prepared prepare(ViewClock clock,std::span<const Observation<Enemy>> enemies) {
    Prepared result;result.clock=clock;
    if(clock.self<0 || clock.round<0 || clock.revision<0 ||
       enemies.size()>static_cast<std::size_t>(max_actors))return result;
    for(const auto& observed:enemies) {
        const auto& enemy=observed.value;
        if(enemy.id<0 || enemy.id==clock.self || !observed.current(clock,enemy.id) ||
           !std::isfinite(enemy.stress) || enemy.stress<0)return result;
        for(int i=0;i<result.count;++i)if(result.entries[i].id==enemy.id)return result;
        const int phases=enemy.completed_turn?1:2;
        result.entries[result.count++]={enemy.id,enemy.position,enemy.stress,
                                       max_reach(enemy,phases),phases};
    }
    result.complete=true;
    return result;
}

// Only a local bound through this actor's next owner boundary. Prepared is the
// adapter's private, unchanged result of prepare(), never a remote proof. The
// adapter owns list completeness and conservative per-enemy-turn stress.
// Unknown channels/effects keep infinite reach. No escape/production proof.
inline Exposure exposure(const Actor& actor,ViewClock clock,
    const Prepared& prepared,double camp_incoming=0) {
    Exposure result;result.health=actor.health;result.max_health=actor.max_health;
    const auto nonnegative=[](double value) {
        return std::isfinite(value) && value>=0;
    };
    if(clock.self<0 || clock.round<0 || clock.revision<0 || actor.id!=clock.self ||
       actor.team!=Team::ally || !actor.alive || !nonnegative(actor.health) ||
       !nonnegative(actor.max_health) || actor.health<=0 || actor.max_health<=0 ||
       actor.health>actor.max_health || !nonnegative(camp_incoming) ||
       !prepared.complete || prepared.clock!=clock || prepared.count<0 ||
       prepared.count>max_actors)return result;
    result.incoming_total=result.next_phase_incoming=camp_incoming;
    for(int i=0;i<prepared.count;++i) {
        const auto& enemy=prepared.entries[i];
        if(enemy.phases<1 || enemy.phases>2)return result;
        const double distance=std::max(
            std::abs(double(actor.position.x)-double(enemy.position.x)),
            std::abs(double(actor.position.y)-double(enemy.position.y)));
        if(std::isfinite(enemy.reach) && distance>enemy.reach)continue;
        const double incoming=enemy.phases*enemy.stress;
        if(!nonnegative(incoming))return result;
        result.incoming_total+=incoming;
        result.next_phase_incoming+=enemy.stress;
        if(!nonnegative(result.incoming_total) ||
           !nonnegative(result.next_phase_incoming))return result;
    }
    result.observations_complete=true;
    return result;
}
inline Exposure exposure(const Actor& actor,ViewClock clock,
    std::span<const Observation<Enemy>> enemies,double camp_incoming=0) {
    return exposure(actor,clock,prepare(clock,enemies),camp_incoming);
}
} // namespace p6::runtime_threat
