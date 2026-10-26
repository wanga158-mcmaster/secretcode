#pragma once
#include "contract.h"
#include <span>
#include <vector>
#ifndef P9_NAV_BOUNDED_PREPARE
#define P9_NAV_BOUNDED_PREPARE 0
#endif
#ifndef P9_NAV_PRICING_RESUME
#define P9_NAV_PRICING_RESUME 0
#endif

namespace p9::live::competition {
class DamageMemory {
    int last_round_=-1,last_hurt_=-1;
    double last_hp_=0;
public:
    void observe(int round,double hp,bool alive,double expected_loss=0) {
        if(round<0||!alive||!std::isfinite(hp)||hp<=0) {
            last_round_=last_hurt_=-1;last_hp_=0;return;
        }
        // Repeated observations within one owner turn do not replace its
        // baseline. Only consecutive owner-turn starts establish HP loss.
        if(round==last_round_)return;
        if(last_round_<0||round!=last_round_+1)last_hurt_=-1;
        else {
            const double expected=std::isfinite(expected_loss)?std::max(0.,expected_loss):0.;
            if(last_hp_-hp>expected+.5)last_hurt_=round;
        }
        last_round_=round;last_hp_=hp;
    }
    bool recently_hurt(int round) const {
        // Retain the signal during the next four quiet owner turns. Healing
        // changes the next comparison baseline without cancelling the signal.
        return last_hurt_>=0&&round>=last_hurt_&&round-last_hurt_<=4;
    }
};
inline bool safe_progress(double current_damage,double next_damage) {
    return std::isfinite(current_damage)&&std::isfinite(next_damage)&&next_damage<=current_damage;
}
inline bool retreat_progress(double current_damage,double next_damage,int current_home,int next_home) {
    if(!std::isfinite(current_damage)||!std::isfinite(next_damage)||current_damage<0||next_damage<0)return false;
    if(next_damage<current_damage)return true;
    if(next_damage>current_damage)return false;
    const auto rank=[](int home) {
        return home<0?std::numeric_limits<std::int64_t>::max():std::int64_t(home);
    };
    return rank(next_home)<rank(current_home);
}
inline double prospect_priority(double retained_priority,int travel) {
    return std::isfinite(retained_priority)&&retained_priority>0?
        retained_priority/(double(std::max(0,travel))+8.):0.;
}
inline int released_bank_distance(int direct,int cleared_exit,bool released_here) {
    if(direct>=0)return direct;
    return released_here&&cleared_exit>=0?cleared_exit:-1;
}
inline int nearest_bank(const World& w,int requested=-1) {
    if(const auto* a=w.altar(requested);a&&a->route_known&&a->travel>=0)return a->id;
    const Altar* best=nullptr;
    for(int i=0;i<std::min(w.altar_count,static_cast<int>(w.altars.size()));++i) {
        const auto& a=w.altars[i];if(!a.route_known||a.travel<0)continue;
        if(!best||(best->enemy_lock&&!a.enemy_lock)||
           (best->enemy_lock==a.enemy_lock&&a.travel<best->travel))best=&a;
    }
    return best?best->id:-1;
}
inline unsigned total_shards(unsigned loose,unsigned emblems,unsigned combine) {
    return loose+emblems*combine;
}
inline int cleared_group_bank_distance(std::span<const Point> spawns,std::span<const int> boundary_costs) {
    if(spawns.empty()||spawns.size()!=boundary_costs.size())return -1;
    const int unknown=std::numeric_limits<int>::max();
    std::vector<int> costs(spawns.size(),unknown);
    std::vector<bool> settled(spawns.size(),false);
    for(std::size_t i=0;i<spawns.size();++i)if(boundary_costs[i]>=0)costs[i]=boundary_costs[i];
    // Multi-source Dijkstra joins every cleared spawn to known external exits.
    // All members are traversable after capture, including interior tiles.
    for(std::size_t pass=0;pass<spawns.size();++pass) {
        std::size_t best=spawns.size();
        for(std::size_t i=0;i<spawns.size();++i)
            if(!settled[i]&&costs[i]!=unknown&&(best==spawns.size()||costs[i]<costs[best]))best=i;
        if(best==spawns.size())break;
        settled[best]=true;
        for(std::size_t i=0;i<spawns.size();++i)
            if(!settled[i]&&distance(spawns[best],spawns[i])<=1&&costs[best]<unknown-1)
                costs[i]=std::min(costs[i],costs[best]+1);
    }
    int worst=0;
    for(int cost:costs) {if(cost==unknown)return -1;worst=std::max(worst,cost);}
    return worst;
}
#if P9_NAV_BOUNDED_PREPARE || P9_NAV_PRICING_RESUME
// The same complete Dijkstra calculation, with one settled member per step.
// Only immutable geometry is retained between owner turns; no live camp or
// champion observation is carried across a yield.
class ClearedBankWork {
    std::vector<Point> spawns_;
    std::vector<int> costs_;
    std::vector<bool> settled_;
    bool done_=false;
    int result_=-1;
public:
    void begin(std::span<const Point> spawns,std::span<const int> boundary) {
        spawns_.assign(spawns.begin(),spawns.end());
        costs_.assign(spawns.size(),std::numeric_limits<int>::max());settled_.assign(spawns.size(),false);
        done_=spawns.empty()||spawns.size()!=boundary.size();result_=-1;
        if(!done_)for(std::size_t i=0;i<spawns.size();++i)if(boundary[i]>=0)costs_[i]=boundary[i];
    }
    bool step() {
        if(done_)return true;
        const int unknown=std::numeric_limits<int>::max();std::size_t best=spawns_.size();
        for(std::size_t i=0;i<spawns_.size();++i)
            if(!settled_[i]&&costs_[i]!=unknown&&(best==spawns_.size()||costs_[i]<costs_[best]))best=i;
        if(best==spawns_.size()) {
            done_=true;result_=0;
            for(int cost:costs_){if(cost==unknown){result_=-1;break;}result_=std::max(result_,cost);}
            return true;
        }
        settled_[best]=true;
        for(std::size_t i=0;i<spawns_.size();++i)
            if(!settled_[i]&&distance(spawns_[best],spawns_[i])<=1&&costs_[best]<unknown-1)
                costs_[i]=std::min(costs_[i],costs_[best]+1);
        return false;
    }
    int result() const {return done_?result_:-1;}
};
#endif
inline int remaining(const World& w) { return std::max(0,w.max_rounds-w.round); }

// World::ticks deliberately caps the older policy horizon at 120. Competition
// prices the real channel duration against the actual remaining match instead.
inline int tribute_time(const World& w,Gems bag) {
    if(bag<=0||!std::isfinite(w.tribute_multiplier)||w.tribute_multiplier<0||
       w.tribute_base<0||w.tribute_minimum<1)return std::numeric_limits<int>::max();
    const double raw=std::ceil(double(bag)*w.tribute_multiplier)+w.tribute_base;
    if(!std::isfinite(raw)||raw>std::numeric_limits<int>::max())return std::numeric_limits<int>::max();
    return std::max(w.tribute_minimum,static_cast<int>(raw));
}
inline Gems combined_bag(const World& w,const Group& g) {
    const auto bag=std::max<Gems>(0,w.self.bag);
    return g.gems>std::numeric_limits<Gems>::max()-bag?std::numeric_limits<Gems>::max():bag+g.gems;
}
inline bool partial_clear_step(const World& w,const Group& g,bool committed,int escape_turns) {
    const auto& actor=w.self;
    if(g.id<0||!g.observed||!g.living||g.cleared||g.respawning||!g.route_known||g.travel<0||
       g.gems<=0||g.contested||escape_turns<0||
       !actor.visible||!actor.alive||actor.channel!=Channel::none||
       !std::isfinite(actor.hp)||!std::isfinite(actor.max_hp)||actor.max_hp<=0||actor.hp<=0||
       !std::isfinite(actor.strength)||actor.strength<=0||
       !std::isfinite(actor.camp_defense_multiplier)||actor.camp_defense_multiplier<0||
       !std::isfinite(g.hp)||g.hp<=0||!std::isfinite(g.incoming)||g.incoming<0||
       !std::isfinite(g.camp_incoming))return false;
    if(!committed&&actor.hp<.75*actor.max_hp)return false;
    const double camps=g.camp_incoming<0?g.incoming:g.camp_incoming;
    const double incoming=camps*actor.camp_defense_multiplier;
    // Pay one attack-turn exposure now, while reserving an independent exit
    // plus two rounds of margin. No teammate damage, tanking or healing credit.
    const double reserve=std::max(.30*actor.max_hp,incoming*(double(escape_turns)+2.));
    return std::isfinite(incoming)&&std::isfinite(reserve)&&actor.hp-incoming>=reserve;
}
template<bool reject_contested_captures=false> inline bool available(const World& w,const Group& g) {
    return g.id>=0&&g.observed&&g.route_known&&g.safe_solo&&!g.respawning&&
        (!reject_contested_captures||!(g.cleared&&g.contested))&&
        g.travel>=0&&g.clear_turns>=0&&w.capture_turns>=0&&safe_for(g,w.self);
}
template<bool reject_contested_captures=false> inline std::int64_t service_turns(const World& w,const Group& g) {
    if(!available<reject_contested_captures>(w,g)||g.gems<=0||g.to_bank<0)return -1;
    const auto ticks=tribute_time(w,combined_bag(w,g));
    if(ticks==std::numeric_limits<int>::max())return -1;
    const auto turns=std::int64_t(g.travel)+g.clear_turns+w.capture_turns+g.to_bank+ticks;
    return turns<=remaining(w)?std::max<std::int64_t>(1,turns):-1;
}
inline int nearby_allies(const World& w,const Group& g) {
    int count=0;
    for(int i=0;i<std::min(w.ally_count,static_cast<int>(w.allies.size()));++i) {
        const auto& a=w.allies[i];
        if(a.id!=w.self.id&&a.ally&&ready(&a)&&distance(a.position,g.position)<=2)++count;
    }
    return count;
}
template<bool reject_contested_captures=false> inline int gem_source(const World& w) {
    const Group* best=nullptr;
    double best_rate=-1;
    std::int64_t best_turns=0;
    for(int i=0;i<std::min(w.group_count,static_cast<int>(w.groups.size()));++i) {
        const auto& g=w.groups[i];const auto turns=service_turns<reject_contested_captures>(w,g);
        if(turns<0)continue;
        // Nearby allies are a congestion penalty, not a claimed future share.
        const auto rate=static_cast<double>(g.gems)/(turns*(1+nearby_allies(w,g)));
        if(!best||rate>best_rate||(rate==best_rate&&
           (turns<best_turns||(turns==best_turns&&
            (g.travel<best->travel||(g.travel==best->travel&&g.id<best->id)))))) {
            best=&g;best_rate=rate;best_turns=turns;
        }
    }
    return best?best->id:-1;
}
template<bool reject_contested_captures=false> inline int gold_source(const World& w) {
    const Group* best=nullptr;
    double best_rate=-1;
    std::int64_t best_turns=0;
    for(int i=0;i<std::min(w.group_count,static_cast<int>(w.groups.size()));++i) {
        const auto& g=w.groups[i];
        if(!available<reject_contested_captures>(w,g)||g.gems!=0||g.gold<=0||
           std::int64_t(g.travel)+g.clear_turns+w.capture_turns>remaining(w))continue;
        const auto turns=std::max<std::int64_t>(1,std::int64_t(g.travel)+g.clear_turns+w.capture_turns);
        const auto rate=static_cast<double>(g.gold)/(turns*(1+nearby_allies(w,g)));
        if(!best||rate>best_rate||(rate==best_rate&&
           (turns<best_turns||(turns==best_turns&&
            (g.travel<best->travel||(g.travel==best->travel&&g.id<best->id)))))) {
            best=&g;best_rate=rate;best_turns=turns;
        }
    }
    return best?best->id:-1;
}
template<bool reject_contested_captures=false> inline bool bank_now(const World& w,int bank_distance,Gems batch=24) {
    if(w.self.bag<=0)return false;
    if(w.immediate_bank()||(bank_distance>=0&&bank_distance<=3)||w.self.bag>=std::max<Gems>(1,batch))return true;
    if(!w.safe_now||w.self.max_hp<=0||w.self.hp<.45*w.self.max_hp)return true;
    const auto ticks=tribute_time(w,w.self.bag);
    if(ticks==std::numeric_limits<int>::max()||remaining(w)<=
       std::int64_t(std::max(0,bank_distance))+ticks+12)return true;
    for(int i=0;i<std::min(w.group_count,static_cast<int>(w.groups.size()));++i) {
        const auto& g=w.groups[i];
        if(g.travel<=10&&service_turns<reject_contested_captures>(w,g)>=0)return false;
    }
    return true;
}
struct Exploration {
    int target=-1,deadline=-1,lastDistance=-1,lastProgress=-1;
    void start(int id,int round,int route_distance) {
        target=id;lastDistance=route_distance;lastProgress=round;
        const auto budget=std::clamp<std::int64_t>(std::int64_t(route_distance)+8,12,120);
        deadline=static_cast<int>(std::min<std::int64_t>(std::numeric_limits<int>::max(),std::int64_t(round)+budget));
        if(id<0||round<0||route_distance<0)target=-1;
    }
    bool continue_route(int id,int round,int route_distance) {
        if(target<0||id!=target||round<lastProgress||round>=deadline||route_distance<0) {
            target=-1;return false;
        }
        if(route_distance<lastDistance) {lastDistance=route_distance;lastProgress=round;}
        if(round-lastProgress>=3) {target=-1;return false;}
        return true;
    }
};
} // namespace p9::live::competition
