#pragma once
#include "../../common/src/observe.h"
#include <array>
#include <cmath>
#include <limits>

// Snapshot-local factorization of the existing solo basic-only kernel. This
// stores combat work, never a feasibility certificate for another HP/maxHP.
namespace p5::dividend::basic_quotes {
inline constexpr int group_cap=8,profile_cap=4;
enum class CostState { valid,unknown,invalid };
struct Cost {
    CostState state=CostState::invalid;
    int clear_ticks=0;
    double worst_camp_damage=0;
};
inline bool finite(double n){return std::isfinite(n);}
inline bool sanitized_actor(const CombatActor& a,int self){
    return a.id==self && a.self && a.visible && a.enemy_cast_bound_known &&
        finite(a.health) && finite(a.max_health) && a.health>0 && a.max_health>=a.health &&
        finite(a.strength) && a.strength>=0 && finite(a.basic_modifier) && a.basic_modifier>=0 &&
        finite(a.strength*a.basic_modifier*(a.ranged?.5:1)) &&
        a.direct_or_reset_damage==0 && !a.spell_known_ready && !a.legal_first_cast && !a.reserved_cast &&
        !a.heal_legal_ready && a.heal_recipient==-1 && a.heal_amount==0 &&
        a.shield==0 && a.shield_expiry_tick==0 && a.visible_enemy_next_phase==0;
}
inline CombatForecast reprice(const Cost& cost,double health,double maximum,double first_loss=0){
    CombatForecast out;
    if(!finite(health) || !finite(maximum) || health<=0 || maximum<health ||
        !finite(first_loss) || first_loss<0 || health-first_loss<=0)return out;
    if(cost.state==CostState::unknown){out.state=EstimateState::unknown;out.reason="unknown-basic-cost";return out;}
    if(cost.state!=CostState::valid || cost.clear_ticks<0 || cost.clear_ticks>100000 ||
        !finite(cost.worst_camp_damage) || cost.worst_camp_damage<0)return out;
    out.clear_ticks=cost.clear_ticks;out.worst_camp_damage=cost.worst_camp_damage;
    out.remaining_health[0]=health-first_loss-cost.worst_camp_damage;
    out.state=out.remaining_health[0]>0 && out.remaining_health[0]>=.2*maximum?EstimateState::feasible:EstimateState::unsafe;
    out.reason=out.state==EstimateState::feasible?"conservative-visible-party":"individual-worst-exposure";
    return out;
}
// The numeric camp cost still excludes outside threat. A selected complete
// tail must additionally charge its actual visible threat for the clear/hold
// and retain the common two-hit reserve; no shield/heal/spell is introduced.
inline CombatForecast reprice_exposed(const Cost& cost,double health,double maximum,
                                     double threat,int hold){
    if(!finite(threat) || threat<0 || hold<0 || hold>std::numeric_limits<int>::max()-100000)return {};
    auto out=reprice(cost,health,maximum);
    if(out.state!=EstimateState::feasible && out.state!=EstimateState::unsafe)return out;
    out.remaining_health[0]-=threat*(out.clear_ticks+hold);
    out.state=out.remaining_health[0]>0 &&
        out.remaining_health[0]>=std::max(.2*maximum,2*threat)?EstimateState::feasible:EstimateState::unsafe;
    out.reason=out.state==EstimateState::feasible?"conservative-visible-party":"individual-worst-exposure";
    return out;
}
class CostTable {
    struct Group {
        std::array<CombatMonster,8> monsters{};
        std::array<double,8> incoming{};
        int id=-1;
        std::size_t count=0;
        CostState state=CostState::invalid;
        bool present=false;
    };
    struct Profile {double strength=0,modifier=1;bool ranged=false;};
    std::array<Group,group_cap> groups_{};
    std::array<Profile,profile_cap> profiles_{};
    std::array<std::array<Cost,profile_cap>,group_cap> costs_{};
    std::array<std::array<bool,profile_cap>,group_cap> cached_{};
    int self_=-1,profiles_count_=0,calls_=0;
    bool enabled_=false;
    bool indices(int g,int p)const{return enabled_ && g>=0 && g<group_cap && p>=0 && p<profiles_count_ && groups_[g].present;}
public:
    // The caller resets for EVERY complete current packet. No SDK reads,
    // cross-round lookup, public reward accounting or action authorization.
    bool reset(int self_actor,bool actual_marcella){
        self_=self_actor;enabled_=actual_marcella && self_actor>=0;profiles_count_=calls_=0;
        for(auto& g:groups_)g.present=false;
        for(auto& row:cached_)row.fill(false);
        return enabled_;
    }
    bool set_group(int slot,int id,bool current,const CombatInput& input){
        if(!enabled_ || slot<0 || slot>=group_cap)return false;
        auto& g=groups_[slot];g.present=true;g.id=id;g.state=CostState::invalid;g.count=0;
        cached_[slot].fill(false);
        if(id<0 || input.actor_count!=1 || input.monster_count<1 || input.monster_count>8 ||
            input.hold_ticks<0 || input.hold_ticks>std::numeric_limits<int>::max()-100000)return false;
        for(int i=0;i<group_cap;++i)if(i!=slot && groups_[i].present && groups_[i].id==id)return false;
        const auto& a=input.actors[0];
        if(!current || !a.enemy_cast_bound_known || (!a.self && !a.visible)){g.state=CostState::unknown;return false;}
        if(!sanitized_actor(a,self_))return false;
        for(std::size_t i=0;i<input.monster_count;++i){const auto& m=input.monsters[i];
            if(m.id<0 || !finite(m.public_health) || m.public_health<=0 || !finite(m.observed_health) ||
                !finite(m.effective_strength) || m.effective_strength<0 ||
                (m.visible && (m.observed_health<=0 || m.observed_health>m.public_health)))return false;
            for(std::size_t j=0;j<i;++j)if(input.monsters[j].id==m.id)return false;
            g.monsters[i]=m;
        }
        g.count=input.monster_count;
        const auto hp=[](const CombatMonster& m){return m.visible?m.observed_health:m.public_health;};
        // Validate and sort the snapshot once, rather than constructing a full
        // six-actor CombatInput and sorting it again for every offense profile.
        std::sort(g.monsters.begin(),g.monsters.begin()+g.count,[&](const auto& a,const auto& b){
            return hp(a)==hp(b)?a.id<b.id:hp(a)<hp(b);});
        for(std::size_t i=0;i<g.count;++i){
            g.incoming[i]=0;
            // Preserve the kernel's addition order, including floating-point
            // rounding: every suffix is summed from its first member onward.
            for(std::size_t j=i;j<g.count;++j)g.incoming[i]+=g.monsters[j].effective_strength;
        }
        g.state=CostState::valid;return true;
    }
    int profile(const CombatActor& actor){
        if(!enabled_ || !sanitized_actor(actor,self_))return -1;
        for(int i=0;i<profiles_count_;++i){const auto& p=profiles_[i];
            if(p.strength==actor.strength && p.modifier==actor.basic_modifier && p.ranged==actor.ranged)return i;}
        if(profiles_count_==profile_cap)return -1;
        const int index=profiles_count_++;profiles_[index]={actor.strength,actor.basic_modifier,actor.ranged};return index;
    }
    bool is_cached(int g,int p)const{return indices(g,p) && cached_[g][p];}
    Cost get(int g,int p)const{return is_cached(g,p)?costs_[g][p]:Cost{};}
    Cost compute(int g,int p){
        if(!indices(g,p))return {};
        if(cached_[g][p])return costs_[g][p];
        cached_[g][p]=true;auto& cost=costs_[g][p];cost={};const auto& group=groups_[g];
        if(group.state!=CostState::valid){cost.state=group.state;return cost;}
        const auto& profile=profiles_[p];
        const double rate=profile.strength*profile.modifier*(profile.ranged?.5:1);
        int clear=0;double damage=0;++calls_;
        // This is precisely forecast_combat's solo basic-only work/loss loop.
        // Shields, spells, healing and external threat are excluded at the
        // boundary; actual HP/maxHP remain a separate fresh repricing step.
        for(std::size_t i=0;i<group.count;++i){
            const auto& m=group.monsters[i];
            const double hp=m.visible?m.observed_health:m.public_health;
            const double turns=std::ceil(hp/std::max(1.,rate));
            if(!finite(turns) || turns>100000 || clear>100000-turns)return cost;
            clear+=static_cast<int>(turns);damage+=turns*group.incoming[i];
            if(i+1<group.count){clear+=2;damage+=2*(group.incoming[i]-m.effective_strength);}
        }
        if(clear>=0 && clear<=100000 && finite(damage) && damage>=0)
            cost={CostState::valid,clear,damage};
        return cost;
    }
    int kernel_calls()const{return calls_;}
    int profile_count()const{return profiles_count_;}
};
} // namespace p5::dividend::basic_quotes
