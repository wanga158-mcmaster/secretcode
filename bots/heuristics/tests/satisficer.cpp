#include "../satisficer/src/policy.h"
#include "../satisficer/src/capture_work.h"
#include "../satisficer/src/defense_step.h"
#include <cassert>

static void occupied_capture_retains_paid_work_and_stationary_fighting() {
    struct Enemy {int id;bool in_range,valid;int hp;};
    std::array enemies{Enemy{0,false,true,1900},Enemy{1,true,true,2175},Enemy{2,true,true,1000}};
    int x=43,y=4,bag=15,ticks=0,attacks=0,target=-1;
    // Production flow receives the R1015 opportunity: Vesper's range-three
    // basic can hit the visible Marcella without relinquishing her reward pad.
    auto fight=[&](const Enemy&enemy){
        if(!enemy.valid)return false;
        ++attacks;target=enemy.id;return true;
    };
    assert(h12_satisficer::work_while_capturing(true,true,false,enemies,
        [](const Enemy&e){return e.in_range;},fight));
    assert(attacks==1&&target==1&&x==43&&y==4&&bag==15);
    // The capture clock is still serviced on the same physical tile. The
    // additional action cannot bypass the later payout or spend the old bag.
    for(;ticks<5;++ticks) {
        assert(x==43&&y==4);
        if(ticks==4)bag+=2;
    }
    assert(bag==17&&ticks==5);
    for(const auto mode:std::array{0,1,2}) {
        attacks=0;
        const bool own_pad=mode!=0,reward=mode!=1,contested=mode==2;
        assert(!h12_satisficer::work_while_capturing(own_pad,reward,contested,enemies,
            [](const Enemy&e){return e.in_range;},fight));
        assert(attacks==0);
    }
    enemies[1].valid=false;attacks=0;
    assert(h12_satisficer::work_while_capturing(true,true,false,enemies,
        [](const Enemy&e){return e.in_range;},fight));
    assert(attacks==1&&target==2);
}

int main(){
    occupied_capture_retains_paid_work_and_stationary_fighting();
    {
        using namespace h12_satisficer;
        struct Point {int x,y;bool operator==(const Point&)const=default;};
        const Point origin{36,25};
        const std::array threats{StepThreat{35,24,2,30},StepThreat{35,26,2,70}};
        const std::array legal{origin,Point{36,24},Point{37,25}};
        assert(defense_destination(origin,legal,threats)==Point(37,25));
        // A fresh camp behind the actor vetoes the same tempting step. Net
        // clearance from enemies cannot hide moving closer to a damaging camp.
        const std::array blocked_escape{threats[0],threats[1],StepThreat{39,25,3,80}};
        assert(defense_destination(origin,legal,blocked_escape)==Point(36,24));
        const std::array crossfire{StepThreat{35,25,2,30},StepThreat{37,25,2,70}};
        assert(defense_destination(origin,legal,crossfire)==origin);
        assert(defense_destination(origin,legal,std::span<const StepThreat>{})==origin);
    }
    using namespace heuristics;
    State s;s.hp=s.maximum=1000;
    Job risky;risky.id=0;risky.gems=80;risky.gold=120;risky.travel=2;risky.work=10;risky.loss=650;risky.affordable=true;
    Job useful;useful.id=1;useful.gems=8;useful.gold=40;useful.travel=5;useful.work=4;useful.loss=100;useful.affordable=true;
    h12_satisficer::Policy p;
    std::array jobs{risky,useful};
    assert(choose(p,s,jobs)==1);
    // Large reward is deliberately insufficient to override the safety aspiration.
    jobs[0].loss=100;jobs[0].work=1;jobs[0].travel=0;p={};assert(choose(p,s,jobs)==0);
    jobs[1].ready=true;p={};assert(choose(p,s,jobs)==1);
    s.bag=20;s.altar=8;assert(p.bank(s));
    assert(p.retreat(350,1000,0,20));assert(!p.retreat(500,1000,100,0));
    s.bag=0;assert(!p.bank(s));
    s.hp=700;jobs[0].affordable=jobs[1].affordable=false;
    jobs[0].committed=false;jobs[1].committed=true;
    p={};
    assert(choose(p,s,jobs)==1);
    // A reachable destination keeps its lease while travel makes progress;
    // a stale commitment expires instead of freezing the actor indefinitely.
    jobs[0].affordable=jobs[1].affordable=true; jobs[1].ready=false;
    s.hp=1000;p={};jobs[0].travel=0;assert(choose(p,s,jobs)==0);
    jobs[1].ready=true;s.round=1;assert(choose(p,s,jobs)==0);
    s.round=10;assert(choose(p,s,jobs)==1);
}
