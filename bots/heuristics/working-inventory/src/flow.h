#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace h12_working_inventory {
enum class Receipt {none,gain,bank,loss};
struct Cargo {
    int bag=0;float focus=0;bool initialized=false,pending_submit=false;
    void started_submit(bool started){pending_submit=started;}
    Receipt observe(int next_bag,float next_focus,bool alive,bool submitting) {
        Receipt r=Receipt::none;
        if(initialized&&next_bag!=bag)r=next_bag>bag?Receipt::gain:
            pending_submit&&alive&&next_bag==0?Receipt::bank:Receipt::loss;
        bag=next_bag;focus=next_focus;initialized=true;
        // A start action records the pending bit even when a one-tick tribute
        // completes before another entry can observe an active channel.
        pending_submit=alive&&submitting;
        return r;
    }
};
enum class Phase {replenish,work,liquidate};
struct StockObservation {
    int round=0,bag=0,capture=5;
    bool prism=false,forced=false,useful=false,cashable=false,valid=true,pending_capture=false;
};
struct Inventory {
    Phase phase=Phase::replenish;
    int started=0,progress=0,source=-1;
    void note_progress(int round){progress=round;}
    Phase update(const StockObservation&o) {
        if(o.bag<=0){phase=Phase::replenish;source=-1;started=progress=o.round;return phase;}
        if(o.forced||o.bag>=24||!o.prism){phase=Phase::liquidate;return phase;}
        if(phase==Phase::liquidate)return phase;
        const bool option=o.valid&&o.cashable&&(o.useful||o.pending_capture);
        if(phase==Phase::replenish) {
            phase=option?Phase::work:Phase::liquidate;started=progress=o.round;
        } else if(!option||o.round-started>=2*o.capture+2||o.round-progress>=o.capture+1)
            phase=Phase::liquidate;
        return phase;
    }
};
struct Target {
    float hp=0,basic_damage=0;
    std::array<float,2> loaded{},empty{};
    std::array<bool,2> spell{};
    bool basic=false;
};
struct Menu {std::array<Target,6> targets{};unsigned count=0;bool basic=false,cast=false;};
inline float camp_damage(float raw,float monster_bonus){return raw*(1+monster_bonus);}
// A conservative gap avoids float-equality claims. The empty counterfactual is
// an unclipped forecast, never a scaled target-overload/HP-capped result.
inline bool stock_kill(float hp,float loaded,float empty) {
    return hp>0&&loaded>=hp+.05f&&empty+.05f<hp;
}
inline int remaining_attacks(float hp,float damage,float basic) {
    return int(std::ceil(std::max(0.f,hp-damage)/std::max(1.f,basic)));
}
inline bool stock_useful(const Target&t,unsigned slot) {
    if(!t.spell[slot]||t.hp<=0)return false;
    return stock_kill(t.hp,t.loaded[slot],t.empty[slot])||
        remaining_attacks(t.hp,t.loaded[slot],t.basic_damage)<remaining_attacks(t.hp,t.empty[slot],t.basic_damage);
}
enum class Kind {none,basic,spell};
struct Plan {Kind kind=Kind::none;int index=-1,slot=-1;bool stock=false,kill_threshold=false;};
inline Plan choose(const Menu&m) {
    Plan best;int best_tier=100,best_after=100000;float best_hp=1e30f;
    for(unsigned i=0;i<m.count&&i<6;++i) {
        const auto&t=m.targets[i];if(t.hp<=0)continue;
        // Take an ordinary last hit before spending a spell. Otherwise prefer
        // a stock-enabled kill, then the fewest discrete finishing attacks.
        if(m.basic&&t.basic&&t.basic_damage>=t.hp)
            return {Kind::basic,int(i)};
        if(m.cast)for(unsigned slot=0;slot<2;++slot)if(t.spell[slot]&&t.loaded[slot]>0) {
            const bool useful=stock_useful(t,slot),kill=stock_kill(t.hp,t.loaded[slot],t.empty[slot]);
            const int tier=kill?0:useful?1:2;
            const int after=remaining_attacks(t.hp,t.loaded[slot],t.basic_damage);
            if(tier<best_tier||(tier==best_tier&&(after<best_after||(after==best_after&&t.hp<best_hp)))) {
                best={Kind::spell,int(i),int(slot),useful,kill};best_tier=tier;best_after=after;best_hp=t.hp;
            }
        }
    }
    if(best.kind!=Kind::none)return best;
    for(unsigned i=0;i<m.count&&i<6;++i)if(m.basic&&m.targets[i].basic&&m.targets[i].hp>0&&m.targets[i].hp<best_hp) {
        best={Kind::basic,int(i)};best_hp=m.targets[i].hp;
    }
    return best;
}
struct FlowResult {unsigned actions=0,basics=0,casts=0,hits=0,kills=0;};
inline bool execution_lease(int round,int progress,int capture,bool admitted){return admitted&&round-progress<capture;}
template<class World> FlowResult execute(World&w) {
    FlowResult r;bool basic=true,cast=true;
    for(unsigned step=0;step<2&&w.enough_fuel();++step) {
        if(!w.available(basic,cast))break;
        auto m=w.observe();m.basic&=basic;m.cast&=cast;
        const auto p=choose(m);if(p.kind==Kind::none||!w.legal(p)||!w.act(p))break;
        ++r.actions;if(p.kind==Kind::basic){basic=false;++r.basics;}else{cast=false;++r.casts;}
        const auto e=w.effect(m,p);r.hits+=e.first;r.kills+=e.second;
    }
    return r;
}
} // namespace h12_working_inventory
