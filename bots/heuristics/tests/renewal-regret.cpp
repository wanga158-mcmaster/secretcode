#include "../renewal-regret/src/policy.h"
#include "../renewal-regret/src/frontier.h"
#include "../renewal-regret/src/capture.h"
#undef HEURISTICS_ENABLED
#undef HEURISTICS_TRACE
#include "../satisficer/src/policy.h"
#include <cassert>
#include <cstdint>

using namespace heuristics;
using Policy=h12_renewal_regret::Policy;
struct Mask {
    std::uint64_t bits=0;
    Mask shift_right(int n)const{
        constexpr std::uint64_t left=0x0101010101010101ULL,right=left<<7;
        return {n>0?(bits&~right)<<1:(bits&~left)>>1};
    }
    Mask shift_up(int n)const{return {n>0?bits<<8:bits>>8};}
    friend Mask operator|(Mask a,Mask b){return {a.bits|b.bits};}
};
Job harmless(int id,int travel) {
    Job j;j.id=id;j.travel=travel;j.gems=14;j.original_hp=j.remaining_hp=240;
    j.work=1;j.affordable=true;j.known=true;
    return j;
}
int main(){
    // The same guard serves both production hold paths. Empty enemy vision
    // cannot restart a full hold, and a blocked exit keeps the expired clock.
    heuristics::CaptureWatch watch;
    using h12_renewal_regret::leave_capture;
    using h12_renewal_regret::observe_capture;
    assert(!leave_capture(watch,1,20,5,false));
    assert(!leave_capture(watch,1,26,5,false));
    assert(leave_capture(watch,1,27,5,false));
    observe_capture(watch,true,true); // physical exit was blocked
    assert(watch.source()==1&&leave_capture(watch,1,28,5,true));
    assert(leave_capture(watch,1,40,5,false)); // exclusion expiry is no reset
    observe_capture(watch,false,true);assert(watch.source()==-1);
    assert(!leave_capture(watch,1,41,5,false));
    observe_capture(watch,true,false);assert(watch.source()==-1);
    assert(!leave_capture(watch,2,42,5,false));
    assert(!leave_capture(watch,2,52,12,false)); // queried capture interval
    watch.reset();assert(watch.source()==-1); // death/lifecycle reset
    using h12_renewal_regret::leave_or_work;
    int walks=0,work=0;
    assert(leave_or_work([&]{++walks;return false;},[&]{++work;return true;}));
    assert(walks==1&&work==1); // actual timeout dispatch reaches useful work
    assert(leave_or_work([&]{++walks;return true;},[&]{++work;return true;}));
    assert(walks==2&&work==1); // successful departure needs no extra action
    assert(!leave_or_work([]{return false;},[]{
        return h12_renewal_regret::clear_route_blocker(4,false,true,20,true,true);
    })); // an unsafe mixed group remains refused on a blocked exit
    // A partially cleared long row advances beyond visible dead pads, and
    // all-visible/dead rows instead expose their actual capture tiles.
    std::array members{h12_renewal_regret::Member{5,17,true},
        h12_renewal_regret::Member{6,17,true},h12_renewal_regret::Member{20,17,false}};
    h12_renewal_regret::FrontierMemory memory;
    auto frontier=h12_renewal_regret::goal_frontier(memory,members);
    assert(frontier.discover&&frontier.goals.size()==1&&frontier.goals[0].x==20);
    // Leaving vision does not resurrect previously witnessed dead members.
    members[0].visible=false;members[1].visible=false;
    frontier=h12_renewal_regret::goal_frontier(memory,members,true);
    assert(frontier.goals.size()==1&&frontier.goals[0].x==20);
    members[0].visible=members[1].visible=members[2].visible=true;
    frontier=h12_renewal_regret::goal_frontier(memory,members);
    assert(!frontier.discover&&frontier.goals.size()==3&&frontier.goals[0].x==5);
    // A new live observation invalidates the old generation's deaths.
    memory.note_alive(2);assert(memory.dead[0]==0&&memory.dead[2]==0);
    memory.reset();assert(memory.dead.empty());
    members[0].visible=members[1].visible=true;members[2].visible=false;
    frontier=h12_renewal_regret::goal_frontier(memory,members,true,10,50);
    members[0].visible=members[1].visible=false;
    frontier=h12_renewal_regret::goal_frontier(memory,members,true,20,50);
    assert(frontier.goals.size()==1); // short gaps retain witnessed deaths
    frontier=h12_renewal_regret::goal_frontier(memory,members,true,60,50);
    assert(frontier.goals.size()==3); // an unseen capture/respawn invalidates them
    using h12_renewal_regret::clear_route_blocker;
    assert(clear_route_blocker(4,false,true,0,true,true));
    assert(!clear_route_blocker(3,false,true,0,true,true));
    assert(!clear_route_blocker(8,true,true,0,true,true));
    assert(!clear_route_blocker(8,false,true,20,true,true));
    assert(!clear_route_blocker(8,false,true,0,true,false));
    assert(!clear_route_blocker(8,false,false,0,true,true));
    // Public occupied spawn roots must still yield legal neighboring approach
    // pads. This calls the same perimeter transform as the SDK navigation flow.
    const Mask occupied{1ULL<<(3+3*8)};
    const auto boundary=h12_renewal_regret::approach_perimeter(occupied);
    const auto legal=boundary.bits&~occupied.bits;
    assert((legal&(1ULL<<(2+3*8)))!=0&&(legal&occupied.bits)==0);
    State s;s.mature=true;s.monster=s.jungle=5;
    std::array jobs{harmless(0,1),harmless(1,3)};
    jobs[0].known=false;jobs[0].age=5;
    Policy p;h12_satisficer::Policy predecessor;
    // Two harmless gem pairs are reachable. Unseen work adds an adverse
    // decision window; robust opportunity regret prefers the known longer trip.
    const auto uncertain=Policy::model(s,jobs[0]);
    const auto known=Policy::model(s,jobs[1]);
    assert(uncertain.time[0]==10&&uncertain.time[1]==20&&known.time[2]==12);
    assert(choose(p,s,jobs)==1);
    assert(choose(predecessor,s,jobs)==0);
    jobs[0].known=true;p={};assert(choose(p,s,jobs)==0);
    // One visible rival can reset capture; remove that fact and the choice flips.
    jobs[0].enemy_eta=0;p={};assert(choose(p,s,jobs)==1);
    jobs[0].enemy_eta=10000;p={};assert(choose(p,s,jobs)==0);
    // A dominated trip and a shared payout do not win by nominal reward alone.
    jobs[0].allies=2;p={};assert(choose(p,s,jobs)==1);
    jobs[0].allies=0;jobs[1].travel=8;p={};assert(choose(p,s,jobs)==0);
    // With no payout in the current currency, act on a cheap useful cycle.
    s.mature=false;s.monster=s.jungle=0;s.gold=0;
    p={};assert(choose(p,s,jobs)==0);
    // An unavailable full-clear preview cannot erase a progressing partial job.
    jobs[0].affordable=jobs[1].affordable=false;p={};
    assert(choose(p,s,jobs)==0);
    s.hp=700;jobs[0].committed=true;
    assert(choose(p,s,jobs)==0&&p.current==0);
    jobs[0].committed=false;assert(choose(p,s,jobs)==-1&&p.current==0);
    // Switching friction is conditional and expires without NEW best progress.
    s={};s.mature=true;s.monster=s.jungle=5;
    jobs={harmless(0,3),harmless(1,4)};p={};
    assert(choose(p,s,jobs)==0);
    jobs[1].travel=2;s.round=1;assert(choose(p,s,jobs)==0);
    jobs[0].travel=4;s.round=4;assert(choose(p,s,jobs)==0);
    jobs[0].travel=3;s.round=6;assert(choose(p,s,jobs)==0);
    s.round=9;assert(choose(p,s,jobs)==1);
    // Materially worse commitments are abandoned even while the lease is fresh.
    p={};s.round=0;jobs={harmless(0,3),harmless(1,4)};
    assert(choose(p,s,jobs)==0);
    jobs[0].known=false;jobs[0].age=5;jobs[0].enemy_eta=0;
    s.round=1;assert(choose(p,s,jobs)==1);
    // Traveling batch size, protected cargo and health-first investment differ.
    s={};s.bag=20;s.altar=15;assert(!p.bank(s));
    s.bag=40;assert(p.bank(s));s.bag=10;s.hp=500;assert(p.bank(s));
    s.bag=0;assert(!p.bank(s));
    assert(p.retreat(450,1000,20,10));assert(!p.retreat(450,1000,20,0));
    assert(p.build(0,0)==fundamentals::BuildStage::jungle_health);
    assert(p.build(0,5)==fundamentals::BuildStage::monster_strength);
    assert(p.build(5,5)==fundamentals::BuildStage::power_strength);
    // Native Lanes A: one owned health shard, four still cost120, wallet130.
    // The trip remains subject to the executor's health, capture, bank and
    // recall guards. This helper changes only when financing becomes due.
    s={};s.identity=7;s.jungle=1;s.gold=119;
    assert(!p.finance(s,30,5));s.gold=120;assert(p.finance(s,30,5));
    s.gold=130;assert(p.finance(s,30,5));
    s.bag=40;s.altar=15;assert(p.bank(s)&&!p.finance(s,30,5));
    s.bag=20;assert(!p.bank(s)&&p.finance(s,30,5));
    s.bag=1;s.altar=1;assert(p.bank(s)&&!p.finance(s,30,5));
    s.bag=0;s.altar=0;
    s.jungle=5;assert(!p.finance(s,30,5));
    s.gold=299;assert(!p.finance(s,30,5));s.gold=300;assert(p.finance(s,30,5));
    s.identity=11;assert(!p.finance(s,30,5));
    // Queried prices/combination sizes are inputs, not copied finance constants.
    s.identity=7;s.jungle=2;s.gold=139;assert(!p.finance(s,35,6));
    s.gold=140;assert(p.finance(s,35,6));
    s.gold=130;assert(!p.finance(s,0,5)&&!p.finance(s,30,0));
}
