#include <array>
#include <cassert>
#include <limits>
#include <span>
#include "../triage/src/policy.h"
#include "../common/src/focus_caller.h"

#ifdef NDEBUG
#error This caller fixture requires assertions enabled.
#endif

using View=p11::focus::View;
using Reason=p11::focus::Reason;
using Job=p11_triage::Policy::FocusJob;

namespace {
// Observe the complete caller's admission and frontier, then delegate every
// admitted decision to the production policy. No winner is supplied by a stub.
struct Probe : p11_triage::Policy {
    static inline int calls=0,count=0;
    static inline std::array<Job,3> passed{};
    static void reset(){calls=0;count=0;passed={};}
    static int choose_focus(std::span<const Job> jobs) {
        ++calls;count=int(jobs.size());assert(count<=3);
        for(int i=0;i<count;++i)passed[i]=jobs[i];
        return p11_triage::Policy::choose_focus(jobs);
    }
};
struct OutsidePool : Probe {
    static int choose_focus(std::span<const Job> jobs) {
        Probe::choose_focus(jobs);
        return 404;
    }
};
constexpr std::array<View,2> counterexample{{
    {10,10,1,true,true},{20,60,8,true,true}
}};
auto choose(std::span<const View> views,float damage=10,bool ready=true,
            int fuel=60000,int observed=45,int current=45) {
    Probe::reset();
    return p11::focus::select<Probe>(views,damage,ready,fuel,observed,current);
}
void counterexample_jobs() {
    assert(Probe::calls==1&&Probe::count==2);
    assert(Probe::passed[0].id==20&&Probe::passed[0].hits==6&&Probe::passed[0].incoming==8);
    assert(Probe::passed[1].id==10&&Probe::passed[1].hits==1&&Probe::passed[1].incoming==1);
}
} // namespace

int main() {
    // The actual caller computes Smith's baseline 20, constructs both jobs,
    // and accepts Triage's different answer: 24 rather than 27 exposure over
    // the three modeled phases. This exercises admission through selection.
    auto result=choose(counterexample);
    assert(result.baseline==20&&result.selected==10&&result.reason==Reason::planned);
    assert(result.visible==2&&result.jobs==2);
    counterexample_jobs();

    result=choose(counterexample,10,true,45000);
    assert(result.selected==20&&result.baseline==20&&result.reason==Reason::reserve);
    assert(result.jobs==2&&Probe::calls==0);
    result=choose(counterexample,10,true,45001);
    assert(result.selected==10&&result.baseline==20&&result.reason==Reason::planned);
    counterexample_jobs();

    result=choose(counterexample,10,false);
    assert(result.selected==20&&result.reason==Reason::no_attack&&Probe::calls==0);
    result=choose(counterexample,10,true,60000,44,45);
    assert(result.selected==-1&&result.baseline==-1&&result.reason==Reason::stale);
    assert(result.visible==0&&result.jobs==0&&Probe::calls==0);

    // A different group and a dead observation enter neither calculation.
    // A live group member outside basic range remains a baseline candidate,
    // but cannot enter the planner's legal attack frontier.
    const std::array<View,5> filtered{{
        {99,10,1000,false,true},{30,10,100,true,false},
        {10,10,1,true,true},{20,60,8,true,true},{40,0,100,true,true}
    }};
    result=choose(filtered);
    assert(result.baseline==30&&result.selected==10&&result.reason==Reason::planned);
    assert(result.visible==3&&result.jobs==2);
    counterexample_jobs();

    // The frontier is ranked over the whole cache, rather than taking the
    // first three IDs or observations. The best Smith job arrives last.
    const std::array<View,5> frontier{{
        {10,50,1,true,true},{20,40,1,true,true},{30,30,1,true,true},
        {40,20,1,true,true},{999,10,2,true,true}
    }};
    result=choose(frontier);
    assert(result.baseline==999&&result.selected==999&&result.reason==Reason::planned);
    assert(result.visible==5&&result.jobs==3&&Probe::calls==1&&Probe::count==3);
    assert(Probe::passed[0].id==999&&Probe::passed[1].id==40&&Probe::passed[2].id==30);

    result=choose({});
    assert(result.selected==-1&&result.baseline==-1&&result.reason==Reason::empty);
    assert(result.visible==0&&result.jobs==0&&Probe::calls==0);
    const std::array<View,1> outside_range{{{7,10,2,true,false}}};
    result=choose(outside_range);
    assert(result.selected==7&&result.baseline==7&&result.reason==Reason::empty);
    assert(result.visible==1&&result.jobs==0&&Probe::calls==0);
    const std::array<View,1> singleton{{{3,25,2,true,true}}};
    result=choose(singleton);
    assert(result.selected==3&&result.reason==Reason::planned&&Probe::calls==1);
    assert(Probe::count==1&&Probe::passed[0].id==3&&Probe::passed[0].hits==3);

    // The exact cache limit is accepted; an extra observation is rejected
    // before any policy call, even when almost all observations are dead.
    std::array<View,513> bounded{};
    bounded[0]=counterexample[0];bounded[511]=counterexample[1];
    result=choose(std::span<const View>{bounded}.first(512));
    assert(result.selected==10&&result.reason==Reason::planned&&result.visible==2);
    counterexample_jobs();
    result=choose(bounded);
    assert(result.selected==-1&&result.baseline==-1&&result.reason==Reason::unsupported);
    assert(result.visible==0&&result.jobs==0&&Probe::calls==0);
    for(float damage:{0.f,-10.f}) {
        result=choose(counterexample,damage);
        assert(result.selected==20&&result.baseline==20&&result.reason==Reason::unsupported);
        assert(result.visible==2&&result.jobs==0&&Probe::calls==0);
    }
    for(float damage:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        result=choose(counterexample,damage);
        assert(result.selected==-1&&result.baseline==-1&&result.reason==Reason::unsupported);
        assert(result.visible==0&&result.jobs==0&&Probe::calls==0);
    }

    // The caller validates the policy's returned native ID against precisely
    // the submitted frontier, preserving its computed baseline on rejection.
    Probe::reset();
    result=p11::focus::select<OutsidePool>(counterexample,10,true,60000,45,45);
    assert(result.selected==20&&result.baseline==20&&result.reason==Reason::rejected);
    counterexample_jobs();
}
