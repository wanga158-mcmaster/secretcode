#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace h12_splash_harvester {
struct Point {int x=0,y=0;};
inline int distance(Point a,Point b){return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
struct Victim {Point p;float hp=0,incoming=0;int group=-1;bool basic=false,direct=false;};
struct Center {Point p;bool legal=false;};
struct Menu {
    std::array<Victim,6> targets{};unsigned count=0;
    std::array<Center,9> centers{};
    bool basic=false,cast=false,splash=false;
    float basic_damage=0,area_damage=0,direct_damage=0,radius=0;
    // The live adapter conservatively marks a center/primary unsafe whenever
    // its whole hit region touches an unaffordable different source, including
    // members not admitted to the six-victim scoring menu.
    std::array<bool,6> safe_basic{true,true,true,true,true,true};
    std::array<bool,6> safe_direct{true,true,true,true,true,true};
    std::array<bool,9> safe_area{true,true,true,true,true,true,true,true,true};
};
enum class Kind {none,basic,area,direct};
struct Plan {Kind kind=Kind::none;int index=-1;float value=0;unsigned hits=0,kills=0,work=0;};
inline Plan choose(const Menu&m) {
    Plan best;
    auto consider=[&](Kind kind,int index,Point center,float damage,float radius,bool legal) {
        if(!legal||damage<=0)return;
        Plan p{kind,index};float clipped=0,avoided=0;
        for(unsigned i=0;i<m.count&&i<6;++i) {
            ++best.work;
            const auto&t=m.targets[i];
            const bool hit=(kind==Kind::direct||(!m.splash&&kind==Kind::basic))?int(i)==index:distance(t.p,center)<=radius;
            if(!hit||t.hp<=0)continue;
            ++p.hits;clipped+=std::min(t.hp,damage);
            if(t.hp<=damage){++p.kills;avoided+=t.incoming;}
        }
        // Saved future basic attacks and avoided camp phases matter more than
        // damage beyond death. Kills are useful, but do not dominate two nearly
        // completed enemies with a single trivial last hit.
        p.value=clipped+std::max(1.f,m.basic_damage)*p.kills+2*avoided;
        if(p.hits&&p.value>best.value){p.work=best.work;best=p;}
    };
    if(m.basic)for(unsigned i=0;i<m.count&&i<6;++i)
        consider(Kind::basic,int(i),m.targets[i].p,m.basic_damage,m.splash?1.f:0.f,m.targets[i].basic&&m.safe_basic[i]);
    if(m.cast) {
        for(unsigned i=0;i<9;++i)consider(Kind::area,int(i),m.centers[i].p,m.area_damage,m.radius,m.centers[i].legal&&m.safe_area[i]);
        for(unsigned i=0;i<m.count&&i<6;++i)consider(Kind::direct,int(i),m.targets[i].p,m.direct_damage,0,m.targets[i].direct&&m.safe_direct[i]);
    }
    return best;
}
struct FlowResult {unsigned actions=0,basics=0,casts=0,observed_hits=0,observed_kills=0,work=0;};
inline bool execution_lease(int round,int progress,int capture,bool admitted) {
    return admitted&&round-progress<capture;
}
// Production and tests use the same observation -> hit-set choice -> revalidate
// -> action -> actual-health observation transition. One basic and one cast,
// no reset/dash chain. Menus are rebuilt only after successful actions.
template<class World> FlowResult execute(World&w) {
    FlowResult result;bool basic=true,cast=true;
    for(unsigned step=0;step<2&&w.enough_fuel();++step) {
        if constexpr(requires{w.available(basic,cast);})if(!w.available(basic,cast))break;
        auto menu=w.observe();menu.basic&=basic;menu.cast&=cast;
        const auto plan=choose(menu);result.work+=plan.work;
        if(plan.kind==Kind::none||!w.legal(plan))break;
        if(!w.act(plan))break;
        ++result.actions;
        if(plan.kind==Kind::basic){basic=false;++result.basics;}
        else {cast=false;++result.casts;}
        const auto effect=w.effect(menu);
        result.observed_hits+=effect.first;result.observed_kills+=effect.second;
    }
    return result;
}
} // namespace h12_splash_harvester
