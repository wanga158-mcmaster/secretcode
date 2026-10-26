#include "../shield-window/src/flow.h"
#include "../shield-window/src/policy.h"
#undef HEURISTICS_ENABLED
#undef HEURISTICS_TRACE
#undef HEURISTICS_DIAGNOSTIC
#include "../working-inventory/src/policy.h"
#include <cassert>
#include <string>

namespace shield=h12_shield_window;

// Mutable live-state adapter for the SAME production phase helper. Forecast
// and actual cast results are deliberately independent. Camp injury survives
// withdrawal/sleep; shields absorb incoming damage without changing actual HP.
struct World {
    shield::Observation value;
    float camp_hp=200,damage=50,actual_shield=200;
    int actual_duration=5,casts=0,attempts=0,attacks=0,observations=0;
    bool cast_success=true,attack_success=true,make_progress=true;
    bool hide_after_cast=false,stale_after_cast=false,no_escape_after_cast=false;
    std::string events;
    shield::Observation observe(){++observations;return value;}
    bool cast_shield() {
        ++attempts;
        if(!cast_success||!value.cast||!value.ready)return false;
        ++casts;events+='S';value.cast=false;value.ready=false;
        value.shield=actual_shield;value.duration=actual_duration;
        if(hide_after_cast)value.visible=false;
        if(stale_after_cast)value.fresh=false;
        if(no_escape_after_cast)value.escape=false;
        return true;
    }
    bool attack() {
        if(!attack_success||!value.basic||!value.visible||!value.fresh||camp_hp<=0)return false;
        ++attacks;events+='B';value.basic=false;
        if(make_progress)camp_hp=std::max(0.f,camp_hp-damage);
        return true;
    }
    bool progressed()const{return make_progress;}
    void next_turn(int round,bool expose=true) {
        value.round=round;value.basic=true;value.cast=true;
        if(expose) {
            const float absorbed=std::min(value.shield,value.incoming);
            value.shield-=absorbed;value.hp-=value.incoming-absorbed;
        }
        if(value.duration>0)--value.duration;
    }
};

static World opportunity() {
    World w;auto&o=w.value;o.round=10;o.source=3;o.hp=800;o.maximum=1000;
    o.predicted=200;o.incoming=50;
    o.visible=o.fresh=o.escape=o.ready=o.cast=o.basic=true;
    return w;
}

static void positive_flow() {
    shield::Window window;auto w=opportunity();const float actual_hp=w.value.hp;
    auto r=shield::execute(window,w);
    assert(r.cast&&r.entered&&r.hit&&r.acted&&!r.withdraw);
    assert(window.phase==shield::Phase::work&&window.source==3);
    assert(w.observations==2&&w.events=="SB"&&w.casts==1&&w.attacks==1&&w.camp_hp==150);
    // Next camp phase consumes temporary absorption, not actual HP. The next
    // observation is fresh; a second physical turn is the entire work pulse.
    w.next_turn(11);
    assert(w.value.shield==150&&w.value.hp==actual_hp);
    r=shield::execute(window,w);
    assert(r.hit&&!r.cast&&!r.withdraw&&window.pulses==2&&w.camp_hp==100);
    w.next_turn(12);
    r=shield::execute(window,w);
    assert(r.withdraw&&!r.acted&&window.phase==shield::Phase::withdraw&&w.attacks==2);
    assert(w.value.hp==actual_hp&&w.value.shield==100);
    // Physical withdrawal reaches safety before renewal. No helper attack or
    // cast is permitted while withdrawal is pending, even on another source.
    w.value.source=4;w.value.ready=true;
    assert(shield::execute(window,w).withdraw&&w.casts==1&&w.attacks==2);
    w.value.source=3;window.outside(15);
    assert(window.phase==shield::Phase::renew&&window.renewing());
    const float injured=w.camp_hp;
    w.value.shield=0;w.value.duration=0;w.next_turn(18,false);
    w.value.ready=true;
    assert(w.camp_hp==injured); // asleep damage does not regenerate
    r=shield::execute(window,w);
    assert(r.cast&&r.entered&&r.hit&&!r.withdraw&&w.camp_hp==50&&w.casts==2);
    w.next_turn(19);
    r=shield::execute(window,w);
    assert(r.hit&&w.camp_hp==0&&w.attacks==4&&w.value.hp==actual_hp);
    // Simulated native capture and tribute continuation use production banking
    // decisions. A ready empty source is held for the engine's capture interval.
    heuristics::State s;s.identity=12;s.altar=8;s.capture=5;
    shield::Policy policy;policy.window=window;
    int capture_ticks=0;while(capture_ticks<s.capture){assert(w.camp_hp==0);++capture_ticks;}
    s.round=24;s.bag=8;
    assert(policy.bank(s));
    int tribute_ticks=int(std::ceil(s.bag*s.tribute));
    assert(tribute_ticks==1);s.bag=0;
    assert(!policy.bank(s)&&w.events=="SBBSBB");
}

static void refusals() {
    auto before_cast=[](auto change) {
        shield::Window window;auto w=opportunity();change(w);
        const auto r=shield::execute(window,w);
        assert(r.withdraw&&!r.cast&&!r.hit&&window.phase==shield::Phase::withdraw);
        assert(w.casts==0&&w.attacks==0);
    };
    before_cast([](auto&w){w.value.ready=false;}); // missing/cooling shield
    before_cast([](auto&w){w.value.cast=false;}); // competing ability spent the cast
    before_cast([](auto&w){w.value.basic=false;});
    before_cast([](auto&w){w.value.visible=false;});
    before_cast([](auto&w){w.value.fresh=false;});
    before_cast([](auto&w){w.value.escape=false;});
    before_cast([](auto&w){w.value.forced=true;}); // deadline/winning cargo/risk adapter
    before_cast([](auto&w){w.value.predicted=49;});
    before_cast([](auto&w){w.value.hp=360;w.value.shield=10000;});
    before_cast([](auto&w){w.value.bag=1;w.value.hp=480;});
    before_cast([](auto&w){w.value.incoming=0;w.value.fresh=false;});
    before_cast([](auto&w){w.value.incoming=0;w.value.visible=false;});
    before_cast([](auto&w){w.value.incoming=0;w.value.forced=true;});
    before_cast([](auto&w){w.cast_success=false;});
    // A melee worker deep inside a long-range camp reserves the several
    // exposed walking phases needed to leave, independently of its shield.
    auto long_exit=opportunity();long_exit.value.escape_steps=5;
    long_exit.value.incoming=60;long_exit.value.hp=1000;
    assert(shield::reserve(long_exit.value)==560);
    shield::Window long_window;
    assert(shield::execute(long_window,long_exit).hit);
    before_cast([](auto&w){w.value.escape_steps=5;w.value.incoming=60;w.value.hp=550;});
    // A successful API cast alone is insufficient: actual post-cast buff,
    // legality, visibility, escape and reserve are observed again before work.
    auto after_cast=[](auto change) {
        shield::Window window;auto w=opportunity();change(w);
        const auto r=shield::execute(window,w);
        assert(r.cast&&r.acted&&r.withdraw&&!r.hit&&!r.entered);
        assert(w.casts==1&&w.attacks==0&&w.observations==2);
    };
    after_cast([](auto&w){w.actual_shield=0;});
    after_cast([](auto&w){w.actual_shield=49;});
    after_cast([](auto&w){w.actual_duration=0;});
    after_cast([](auto&w){w.actual_duration=1;});
    after_cast([](auto&w){w.hide_after_cast=true;});
    after_cast([](auto&w){w.stale_after_cast=true;});
    after_cast([](auto&w){w.no_escape_after_cast=true;});

    // Already admitted work expires on duration/value/stale/no-progress/forced
    // boundaries and never casts again merely because its cooldown is ready.
    auto work_exit=[](auto change) {
        shield::Window window;auto w=opportunity();shield::execute(window,w);
        w.next_turn(11);change(window,w);
        const auto r=shield::execute(window,w);
        assert(r.withdraw&&!r.cast&&!r.hit&&w.casts==1&&w.attacks==1);
    };
    work_exit([](auto&,auto&w){w.value.duration=1;});
    work_exit([](auto&,auto&w){w.value.shield=49;});
    work_exit([](auto&,auto&w){w.value.visible=false;});
    work_exit([](auto&,auto&w){w.value.fresh=false;});
    work_exit([](auto&,auto&w){w.value.escape=false;});
    work_exit([](auto&,auto&w){w.value.forced=true;});
    work_exit([](auto&,auto&w){w.value.hp=360;});
    work_exit([](auto&,auto&w){w.value.round=12;});
    work_exit([](auto&window,auto&w){window.started=w.value.round;window.progress=w.value.round-2;});
    work_exit([](auto&,auto&w){w.attack_success=false;});
    shield::Window missed;auto w=opportunity();w.make_progress=false;
    auto r=shield::execute(missed,w);
    assert(r.cast&&r.acted&&!r.hit&&r.withdraw&&missed.phase==shield::Phase::withdraw);
    assert(w.camp_hp==200&&w.attacks==1);
}

static void action_bounds() {
    shield::Window window;auto w=opportunity();shield::execute(window,w);
    // An externally refreshed allowance cannot trick the same-turn guard into
    // another physical attack or another cast. The next owner turn is required.
    w.value.basic=w.value.cast=w.value.ready=true;
    auto r=shield::execute(window,w);
    assert(!r.acted&&!r.cast&&!r.hit&&!r.withdraw&&w.casts==1&&w.attacks==1);
    w.next_turn(11);w.value.ready=true;
    r=shield::execute(window,w);
    assert(r.hit&&!r.cast&&w.casts==1&&w.attacks==2);
    w.next_turn(12);r=shield::execute(window,w);
    assert(r.withdraw&&w.attacks==2);
    assert(shield::execution_lease(4,0,5,true));
    assert(!shield::execution_lease(5,0,5,true));
    assert(!shield::execution_lease(1,0,5,false));
    // Target-specific Monster bonus and actual missing HP multiply once each;
    // shield value never enters this physical damage forecast.
    assert(std::abs(shield::camp_damage(40,.1f,10,500,1000)-44)<.001f);
    assert(std::abs(shield::camp_damage(40,.1f,12,500,1000)-55)<.001f);
    assert(std::abs(shield::camp_damage(40,0,12,1000,1000)-40)<.001f);
    assert(std::abs(shield::camp_damage(40,0,12,0,1000)-60)<.001f);
}

static void harmless_continuation() {
    // Freshly observed disappearance of all incoming damage ends the shield
    // duty cycle. Remaining injured harmless members can finish with ordinary
    // basics even after the pulse cap and with no cast/shield left.
    shield::Window window;auto w=opportunity();shield::execute(window,w);
    w.next_turn(11);shield::execute(window,w);
    assert(window.pulses==2&&w.camp_hp==100&&w.casts==1&&w.attacks==2);
    w.next_turn(12,false);w.value.incoming=0;w.value.shield=0;w.value.duration=0;
    w.value.ready=w.value.cast=w.value.escape=false;w.value.hp=100;
    auto r=shield::execute(window,w);
    assert(r.acted&&r.hit&&!r.cast&&!r.withdraw&&!r.entered);
    assert(window.phase==shield::Phase::prepare&&window.pulses==0&&window.last_work==12);
    assert(w.camp_hp==50&&w.casts==1&&w.attacks==3);
    // Neither a refreshed allowance nor another source enables an extra
    // same-turn basic. No competing cast is consumed by this safe fallback.
    w.value.basic=w.value.cast=w.value.ready=true;w.value.source=4;
    r=shield::execute(window,w);
    assert(!r.acted&&!r.cast&&!r.hit&&w.attacks==3&&w.casts==1);
    w.value.source=3;w.next_turn(13,false);
    r=shield::execute(window,w);
    assert(r.hit&&!r.cast&&!r.withdraw&&w.camp_hp==0&&w.attacks==4&&w.casts==1);
    // Also allow a completed withdrawal to finish a freshly harmless source,
    // without demanding a useless renewal or treating this as shield work.
    auto safe=opportunity();safe.value.incoming=0;safe.value.ready=safe.value.cast=false;
    safe.value.escape=false;shield::Window withdrawing;withdrawing.exit(safe.value.round-1);
    const auto finish=shield::execute(withdrawing,safe);
    assert(finish.hit&&!finish.cast&&!finish.withdraw&&safe.casts==0&&safe.attacks==1);
    assert(withdrawing.phase==shield::Phase::prepare&&withdrawing.pulses==0);
}

static heuristics::Job job(int id,int travel,int gems) {
    heuristics::Job j;j.id=id;j.travel=travel;j.gems=gems;j.work=1;
    j.remaining_hp=j.original_hp=100;j.known=true;j.affordable=true;return j;
}

static void policy_cases() {
    using namespace heuristics;using Policy=shield::Policy;
    State s;s.identity=12;s.mature=true;s.monster=s.jungle=5;s.altar=10;
    std::array jobs{job(0,1,20),job(1,2,8)};
    Policy p;h12_working_inventory::Policy previous;
    // Short horizon progress value differs from predecessor's bounded personal
    // inventory batch, even before a partial source requires shield admission.
    assert(choose(p,s,jobs)==0&&choose(previous,s,jobs)==1);
    p={};jobs[0].affordable=jobs[1].affordable=false;
    jobs[0].incoming=jobs[1].incoming=50;
    assert(choose(p,s,jobs)==-1);
    p.shield_ready=true;p.predicted_shield=50;
    assert(choose(p,s,jobs)>=0);
    p={};p.shield_ready=true;p.predicted_shield=49;assert(choose(p,s,jobs)==-1);
    p.predicted_shield=500;s.identity=8;assert(choose(p,s,jobs)>=0); // normal healthy partial fallback
    p={};s.hp=749;assert(choose(p,s,jobs)==-1);
    jobs[1].committed=true;assert(choose(p,s,jobs)==1);jobs[1].committed=false;
    s.identity=12;s.hp=360;assert(choose(p,s,jobs)==-1);
    s.hp=1000;p={};p.window.source=0;p.window.phase=shield::Phase::renew;
    jobs[0].affordable=true;jobs[1].affordable=false;
    assert(choose(p,s,jobs)==-1); // cannot re-enter dangerous old source before renewal
    p={};s={};s.identity=12;s.altar=10;s.bag=4;
    assert(!p.bank(s));p.window.phase=shield::Phase::renew;assert(p.bank(s));
    p.window.phase=shield::Phase::work;assert(!p.bank(s));
    p.forced_bank=true;assert(p.bank(s));p.forced_bank=false;
    s.round=1999;assert(p.bank(s));s.round=0;s.bag=8;assert(p.bank(s));
    s.bag=1;s.hp=579;assert(p.bank(s));s.hp=1000;s.altar=2;assert(p.bank(s));
    s.bag=0;p.forced_bank=true;assert(!p.bank(s));
    assert(p.retreat(420,1000,20,1)&&!p.retreat(420,1000,20,0));
    using P=Purchase;
    assert(Policy::purchase(12,0,0)==P::monster_strength);
    assert(Policy::purchase(10,5,0)==P::jungle_focus);
    assert(Policy::purchase(12,5,5)==P::power_focus);
    assert(Policy::purchase(8,0,0)==P::monster_focus);
    assert(Policy::purchase(8,5,0)==P::jungle_health);
    assert(Policy::purchase(9,5,5)==P::power_strength);
    assert(Policy::purchase(10,5,0)!=h12_working_inventory::Policy::purchase(10,5,0));
    assert((Policy::roster==std::array{6,1,7,10,0}));
    assert(Policy::roster!=h12_working_inventory::Policy::roster);
}

int main(){positive_flow();refusals();action_bounds();harmless_continuation();policy_cases();}
