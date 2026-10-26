#include "../splash-harvester/src/flow.h"
#include "../splash-harvester/src/policy.h"
#undef HEURISTICS_ENABLED
#undef HEURISTICS_TRACE
#undef HEURISTICS_DIAGNOSTIC
#include "../reset-pursuit/src/policy.h"
#include <cassert>
#include <string>
#include <utility>

namespace splash=h12_splash_harvester;

// Mutable adapter for the production execute() interface. Observations always
// expose current HP and allowances, while effects are measured after mutation.
struct World {
    splash::Menu menu;
    bool fuel=true,allow=true,succeed=true;
    bool consume_both=false,refresh_cast=false,stop_after_action=false;
    bool die_before_legal=false;
    float actual_scale=1;
    unsigned observations=0,attempts=0,actions=0;
    std::string events;
    std::array<int,2> indexes{-1,-1};

    bool enough_fuel()const{return fuel&&(!stop_after_action||actions==0);}
    bool available(bool basic,bool cast)const{return (basic&&menu.basic)||(cast&&menu.cast);}
    splash::Menu observe(){++observations;return menu;}
    bool legal(const splash::Plan&p) {
        if(!allow)return false;
        if(die_before_legal&&p.kind!=splash::Kind::area)menu.targets[p.index].hp=0;
        if(p.kind==splash::Kind::area)return menu.cast&&menu.centers[p.index].legal&&menu.safe_area[p.index];
        const auto&t=menu.targets[p.index];
        if(t.hp<=0)return false;
        return p.kind==splash::Kind::basic?menu.basic&&t.basic&&menu.safe_basic[p.index]:
            menu.cast&&t.direct&&menu.safe_direct[p.index];
    }
    bool act(const splash::Plan&p) {
        ++attempts;
        if(!succeed)return false;
        indexes[actions]=p.index;++actions;
        const bool basic=p.kind==splash::Kind::basic;
        events+=basic?'B':p.kind==splash::Kind::area?'A':'D';
        const auto center=p.kind==splash::Kind::area?menu.centers[p.index].p:menu.targets[p.index].p;
        const float damage=(basic?menu.basic_damage:p.kind==splash::Kind::area?menu.area_damage:menu.direct_damage)*actual_scale;
        for(unsigned i=0;i<menu.count&&i<6;++i) {
            const bool hit=(p.kind==splash::Kind::direct||(!menu.splash&&basic))?int(i)==p.index:
                splash::distance(menu.targets[i].p,center)<=(basic?1.f:menu.radius);
            if(hit)menu.targets[i].hp=std::max(0.f,menu.targets[i].hp-damage);
        }
        if(basic)menu.basic=false;else menu.cast=false;
        if(consume_both)menu.basic=menu.cast=false;
        if(refresh_cast)menu.cast=true;
        return true;
    }
    std::pair<unsigned,unsigned>effect(const splash::Menu&before)const {
        unsigned hits=0,kills=0;
        for(unsigned i=0;i<before.count&&i<6;++i)if(before.targets[i].hp>0) {
            hits+=menu.targets[i].hp<before.targets[i].hp;
            kills+=menu.targets[i].hp<=0;
        }
        return {hits,kills};
    }
};

static World area_world() {
    World w;auto&m=w.menu;
    m.count=3;m.cast=true;m.basic=true;m.basic_damage=30;m.area_damage=30;m.radius=1;
    m.targets[0]={{-4,0},10,0,0,true,false};
    m.targets[1]={{-1,0},25,5,0,true,false};
    m.targets[2]={{1,0},25,5,0,true,false};
    m.centers[4]={{0,0},true};
    return w;
}

static void flow_cases() {
    // Weakest-first finishes the isolated ten-HP target. The ground center
    // instead clips damage to two weak neighbours and kills both in one cast.
    auto w=area_world();auto p=splash::choose(w.menu);
    assert(p.kind==splash::Kind::area&&p.index==4&&p.hits==2&&p.kills==2);
    w.menu.cast=false;
    const auto weakest=splash::choose(w.menu);
    assert(weakest.kind==splash::Kind::basic&&weakest.index!=0);
    // An actual weakest-HP basic still kills only its one selected victim.
    splash::Plan weakest_first{splash::Kind::basic,0};
    assert(w.act(weakest_first)&&w.menu.targets[1].hp==25&&w.menu.targets[2].hp==25);
    w=area_world();auto r=splash::execute(w);
    assert(w.events=="AB"&&w.indexes[0]==4&&w.indexes[1]==0);
    assert(r.actions==2&&r.casts==1&&r.basics==1&&r.observed_hits==3&&r.observed_kills==3);
    assert(w.menu.targets[0].hp==0&&w.menu.targets[1].hp==0&&w.menu.targets[2].hp==0);
    assert(w.observations==2&&r.work<=252);

    // Varg can choose a central primary that clips two weak neighbours, while
    // an isolated weakest-HP target would collect only one last hit.
    w={};auto&m=w.menu;m.basic=true;m.splash=true;m.basic_damage=30;m.count=4;
    m.targets[0]={{-4,0},10,0,0,true,false};
    m.targets[1]={{0,0},100,0,0,true,false};
    m.targets[2]={{-1,0},25,5,0,true,false};
    m.targets[3]={{1,0},25,5,0,true,false};
    p=splash::choose(m);
    assert(p.kind==splash::Kind::basic&&p.index==1&&p.hits==3&&p.kills==2);
    const auto basic_menu=m;
    r=splash::execute(w);assert(w.events=="B"&&r.observed_hits==3&&r.observed_kills==2);
    assert(w.observations==1); // Spent basic and unavailable cast skip menu work.
    assert(w.menu.targets[0].hp==10&&w.menu.targets[1].hp==70);

    // Risk admission refuses a profitable center touching an unaffordable
    // adjoining source, even though the dangerous member is outside this menu.
    w=area_world();w.menu.safe_area[4]=false;
    p=splash::choose(w.menu);assert(p.kind==splash::Kind::basic);
    w.menu.basic=false;r=splash::execute(w);assert(r.actions==0&&w.events.empty());
    w={};w.menu=basic_menu;w.menu.safe_basic.fill(false);
    r=splash::execute(w);assert(r.actions==0);

    // Ordinary single-target hits must respect the same whole-source risk
    // admission. Refuse an unaffordable primary, then still use a safe neighbour.
    w={};w.menu.count=1;w.menu.basic=w.menu.cast=true;
    w.menu.basic_damage=30;w.menu.direct_damage=60;
    w.menu.targets[0]={{0,0},10,0,1,true,true};
    w.menu.safe_basic[0]=w.menu.safe_direct[0]=false;
    const auto refused_menu=w.menu;
    r=splash::execute(w);assert(r.actions==0&&w.attempts==0&&w.events.empty());
    w={};w.menu=refused_menu;w.menu.count=2;
    w.menu.targets[1]={{1,0},20,0,0,true,true};
    const auto safe_neighbour=w.menu;
    r=splash::execute(w);assert(w.events=="B"&&w.indexes[0]==1&&r.actions==1);
    assert(w.menu.targets[0].hp==10&&w.menu.targets[1].hp==0);
    w={};w.menu=safe_neighbour;w.menu.basic=false;
    r=splash::execute(w);assert(w.events=="D"&&w.indexes[0]==1&&r.actions==1);
    assert(w.menu.targets[0].hp==10&&w.menu.targets[1].hp==0);

    // A forecast death cannot substitute for actual observed health. With a
    // target-dependent damage reduction, the fresh second menu sees survivors.
    w=area_world();w.actual_scale=.5f;r=splash::execute(w);
    assert(w.events=="AB"&&r.observed_kills==1&&w.menu.targets[2].hp==10);
    // Fresh action allowances win over the entry menu after an exclusive cast.
    w=area_world();w.consume_both=true;r=splash::execute(w);
    assert(w.events=="A"&&r.actions==1&&r.basics==0&&r.casts==1&&w.observations==1);
    // Even a refreshed cast allowance cannot spend a second cast in this flow.
    w=area_world();w.menu.targets[1].hp=w.menu.targets[2].hp=300;
    w.refresh_cast=true;r=splash::execute(w);
    assert(r.actions==2&&r.casts==1&&r.basics==1&&w.events=="AB");
    // Revalidation detects a death between menu choice and action.
    w={};w.menu=basic_menu;w.die_before_legal=true;r=splash::execute(w);
    assert(r.actions==0&&w.attempts==0&&w.events.empty());
    w=area_world();w.allow=false;r=splash::execute(w);assert(r.actions==0);
    w=area_world();w.succeed=false;r=splash::execute(w);assert(r.actions==0&&w.attempts==1);
    w=area_world();w.fuel=false;r=splash::execute(w);assert(r.actions==0&&w.observations==0);
    w=area_world();w.menu.basic=w.menu.cast=false;r=splash::execute(w);
    assert(r.actions==0&&w.observations==0);
    w=area_world();w.stop_after_action=true;r=splash::execute(w);assert(r.actions==1&&w.observations==1);

    // Maximum-size menus exercise every bounded scoring slot. Two successful
    // observations can inspect no more than 2*(6 basics+9 centers+6 direct)*6.
    w={};w.menu.count=6;w.menu.basic=w.menu.cast=true;
    w.menu.basic_damage=1;w.menu.area_damage=2;w.menu.direct_damage=3;w.menu.radius=1;
    for(unsigned i=0;i<6;++i)w.menu.targets[i]={{int(i),0},100,0,0,true,true};
    for(unsigned i=0;i<9;++i)w.menu.centers[i]={{int(i%3),int(i/3)},true};
    r=splash::execute(w);assert(r.actions==2&&r.casts==1&&r.basics==1&&r.work<=252);
}

static heuristics::Job job(int id,int travel,int gems) {
    heuristics::Job j;j.id=id;j.travel=travel;j.gems=gems;j.work=1;
    j.remaining_hp=j.original_hp=100;j.known=true;j.affordable=true;return j;
}

static void policy_cases() {
    using Policy=splash::Policy;using namespace heuristics;
    // The runtime's ongoing-source fast path has the same bounded lease used
    // here: admission is immediate, and only new execution progress renews it.
    assert(splash::execution_lease(1,0,5,true));
    assert(!splash::execution_lease(5,0,5,true));
    assert(!splash::execution_lease(1,0,5,false));
    assert(splash::execution_lease(5,4,5,true));
    State s;s.mature=true;s.monster=s.jungle=5;s.identity=10;
    std::array jobs{job(0,1,4),job(1,3,12)};
    Policy p;h12_reset_pursuit::Policy preceding;
    // Personal return over the whole capture/bank cycle can admit a worthwhile
    // journey that predecessor03's nearby quick-completion tier rejects.
    assert(choose(p,s,jobs)==1&&choose(preceding,s,jobs)==0);
    p={};jobs[1].allies=3;assert(choose(p,s,jobs)==0);
    assert(Policy::estimate(s,jobs[1]).personal==3);
    // A same-payout, slower and riskier job is Pareto dominated.
    p={};jobs={job(0,2,8),job(1,3,8)};jobs[1].loss=200;
    assert(choose(p,s,jobs)==0);
    // Progress commitment protects travel, then expires after a queried capture
    // window; revisiting the old minimum does not renew it.
    p={};jobs={job(0,5,10),job(1,6,10)};assert(choose(p,s,jobs)==0);
    jobs[0].travel=6;jobs[1].travel=4;s.round=1;assert(choose(p,s,jobs)==0);
    jobs[0].travel=5;s.round=3;assert(choose(p,s,jobs)==0);
    s.round=5;assert(choose(p,s,jobs)==1);
    // Invalidation overrides the commitment immediately.
    jobs[1].affordable=false;s.round=6;assert(choose(p,s,jobs)==0);
    s.round=1999;p={};assert(choose(p,s,jobs)==0);
    s={};s.mature=true;jobs={job(0,1,4),job(1,3,12)};
    jobs[0].affordable=jobs[1].affordable=false;s.hp=700;
    p={};assert(choose(p,s,jobs)==-1);
    jobs[0].committed=true;assert(choose(p,s,jobs)==0);

    using P=Purchase;
    assert(Policy::purchase(5,0,0)==P::monster_focus);
    assert(Policy::purchase(5,5,0)==P::jungle_focus);
    assert(Policy::purchase(5,5,5)==P::power_focus);
    assert(Policy::purchase(0,0,0)==P::monster_health);
    assert(Policy::purchase(0,5,0)==P::jungle_health);
    assert(Policy::purchase(0,5,5)==P::power_strength);
    assert(Policy::purchase(10,0,0)==P::monster_strength);
    assert(Policy::purchase(10,5,0)==P::jungle_health);
    assert(Policy::purchase(10,5,5)==P::power_strength);
    assert((Policy::roster==std::array{1,8,9,0,4}));
    s={};s.altar=10;s.bag=6;s.identity=5;assert(p.bank(s));
    s.identity=10;assert(!p.bank(s));s.bag=8;assert(p.bank(s));
    s.identity=0;assert(!p.bank(s));s.bag=12;assert(p.bank(s));
    s.bag=3;s.hp=540;assert(p.bank(s));
    s.hp=1000;s.altar=1;assert(p.bank(s));
    s.altar=10;s.round=1999;assert(p.bank(s));
    s.bag=0;assert(!p.bank(s));
    assert(p.retreat(390,1000,20,1));assert(!p.retreat(390,1000,20,0));
}

int main(){flow_cases();policy_cases();}
