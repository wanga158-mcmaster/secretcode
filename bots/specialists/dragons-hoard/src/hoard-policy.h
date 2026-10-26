#pragma once
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <span>

namespace hoard {
enum class Reason { none, threshold, winning, hard_deadline, projected_deadline, ordinary };
constexpr unsigned deadline(unsigned maximum, unsigned reserve) {
    return maximum > reserve ? maximum - reserve : 0;
}
inline int channel(unsigned gems, int minimum, int base, double multiplier) {
    return std::max(minimum, base + static_cast<int>(std::ceil(gems * multiplier)));
}
constexpr bool projection_due(unsigned round, unsigned maximum, bool reachable,
    unsigned travel, unsigned duration, unsigned lock_hits, unsigned reserve) {
    return reachable && static_cast<std::uint64_t>(round) + travel + duration + lock_hits + reserve >= maximum;
}
constexpr Reason request(bool enabled, unsigned bag, unsigned round, unsigned maximum,
    unsigned score, unsigned winning, int nearby, bool reachable, unsigned travel,
    unsigned duration, unsigned lock_hits, unsigned threshold=144, unsigned reserve=128,
    unsigned projection_reserve=16) {
    if (!bag) return Reason::none;
    if (score < winning && bag >= winning-score) return Reason::winning;
    if (!enabled) return bag >= 24 || nearby <= 4 || round >= deadline(maximum,100) ? Reason::ordinary : Reason::none;
    if (bag >= threshold) return Reason::threshold;
    if (round >= deadline(maximum,reserve)) return Reason::hard_deadline;
    if (projection_due(round,maximum,reachable,travel,duration,lock_hits,projection_reserve)) return Reason::projected_deadline;
    return Reason::none;
}
// An accepted origin field completes within the executor's 256-layer bound.
// Before even that latest possible completion is due, a field cannot change
// the hoard's bank request. Threshold, winning and hard-deadline requests still
// need a real route now; no reachable altar is invented by this upper bound.
constexpr bool projection_required(unsigned bag, unsigned round, unsigned maximum,
    unsigned score, unsigned winning, unsigned duration, unsigned lock_hits,
    unsigned threshold=144, unsigned reserve=128, unsigned projection_reserve=16,
    unsigned travel_bound=256) {
    if (!bag) return false;
    return request(true,bag,round,maximum,score,winning,0,false,0,duration,0,
                   threshold,reserve,projection_reserve) != Reason::none ||
        projection_due(round,maximum,true,travel_bound,duration,lock_hits,projection_reserve);
}
struct EscapeContact { int x, y, reach; float damage; };
constexpr int separation(int x, int y, int tx, int ty) {
    const int dx=x>tx?x-tx:tx-x,dy=y>ty?y-ty:ty-y;
    return dx>dy?dx:dy;
}
constexpr float basic_exposure(int x, int y, const EscapeContact& contact) {
    return separation(x,y,contact.x,contact.y)<=contact.reach?contact.damage:0.0f;
}
// A damaged capital carrier must not add a visible basic/reset exposure that
// spends its ordinary 25% HP reserve plus the last personally suffered loss.
// Already unavoidable exposure never forbids an equal-or-better escape step.
constexpr bool capital_step(bool loaded, float health, float maximum, float recent_loss,
    float origin_exposure, float destination_exposure) {
    return !loaded || recent_loss<=0 || destination_exposure<=origin_exposure ||
        health-destination_exposure-recent_loss>=maximum*0.25f;
}
class CapitalRecovery {
    bool active_=false;
public:
    constexpr void reject_approach() { active_=true; }
    constexpr void observe(bool alive, bool loaded, bool restored) {
        if(!alive||!loaded||restored)active_=false;
    }
    [[nodiscard]] constexpr bool active() const { return active_; }
    [[nodiscard]] constexpr bool may_recall(bool visible_safe, float personal_loss) const {
        return visible_safe&&(!active_||personal_loss<=0);
    }
};
// Immutable public design only: activity and life state remain unknown here.
struct PublicMonolith { int x,y,range; float damage; };
constexpr bool monolith_risk(int x,int y,const PublicMonolith& hazard) {
    return hazard.damage>0&&separation(x,y,hazard.x,hazard.y)<=hazard.range;
}
enum class CapitalTransit { ordinary, egress, avoid };
constexpr CapitalTransit capital_transit(bool loaded,bool has_hazards,bool inside) {
    return !loaded||!has_hazards?CapitalTransit::ordinary:
        inside?CapitalTransit::egress:CapitalTransit::avoid;
}
struct AreaVictim { float health; bool hit,selected; };
struct BasicCount { unsigned count=0; bool certain=false; };
struct AreaOutcome { unsigned remaining_basics=0,targets=0; float useful_damage=0; bool certain=true; };
inline BasicCount required_basics(float health,float damage,unsigned bound=256) {
    if(health<=0)return {0,true};
    if(!(damage>0)||!std::isfinite(health)||!std::isfinite(damage))return {};
    // Native health/damage are float32. Division/ceil can miss a final tiny
    // positive remainder; refuse prices beyond a bounded subtraction proof.
    for(unsigned hits=1;hits<=bound;++hits) {
        const float next=health-damage;
        if(next>=health)return {};
        health=next;
        if(health<=0)return {hits,true};
    }
    return {};
}
inline AreaOutcome area_outcome(std::span<const AreaVictim> victims,float spell_damage,
    float basic_damage,bool area,bool basic_available) {
    AreaOutcome result;
    unsigned proof_left=128;
    for(const auto& victim:victims) {
        float health=victim.health;
        if(area?victim.hit:victim.selected) {
            result.useful_damage+=std::min(health,spell_damage);
            health=std::max(0.0f,health-spell_damage);
            ++result.targets;
        }
        // The existing fight may basic only its selected target, and cannot
        // retarget that basic when the spell killed it.
        if(basic_available&&victim.selected&&health>0)health=std::max(0.0f,health-basic_damage);
        const auto basics=required_basics(health,basic_damage,proof_left);
        result.certain&=basics.certain;
        if(!basics.certain)return result;
        result.remaining_basics+=basics.count;
        proof_left-=basics.count;
    }
    return result;
}
constexpr bool prefer_area(AreaOutcome area,AreaOutcome ordinary) {
    return area.certain&&ordinary.certain&&area.targets>=2&&area.remaining_basics<ordinary.remaining_basics;
}
constexpr bool no_new_camp_hit(bool awake,float strength,int separation,float range) {
    return awake||strength<=0||separation>range;
}
constexpr const char* name(Reason r) {
    switch(r) {
        case Reason::threshold:return "threshold";
        case Reason::winning:return "winning";
        case Reason::hard_deadline:return "hard_deadline";
        case Reason::projected_deadline:return "projected_deadline";
        case Reason::ordinary:return "ordinary";
        default:return "none";
    }
}
} // namespace hoard
