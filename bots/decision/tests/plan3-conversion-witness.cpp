// Release-mode own-channel accounting. Synthetic observations, not native proof.
#ifdef CHECK_CURRENT_RUNTIME
#include "../common/runtime.h"
#else
#include "../common/conversion-witness.h"
#endif
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace plan3_runtime;
static unsigned checks=0;
#define CHECK(e) do { ++checks; if(!(e)) { std::fprintf(stderr,"check %u line %d: %s\n",checks,__LINE__,#e); std::exit(1); } } while(false)
static ConversionSnapshot previous() {
    ConversionSnapshot p;
    p.round=p.scoreRound=10;p.ownScore=100;p.bag=21;
    p.valid=p.alive=p.submitting=true;
    p.bankingJob=plan3::JobId{plan3::JobKind::cycle,7,-1,99,-1};p.jobStarted=4;
    return p;
}
static ConversionObservation current() { return {11,121,0,true,true,false,false}; }
static void unique_and_overlapping_output() {
    auto p=previous();auto n=current();
    auto w=observe_conversion(p,n,60);
    CHECK(w.intervalKnown);CHECK(w.confirmed());CHECK(w.lowerBound==21);CHECK(w.exactAmount==21);
    CHECK(conversion_completes_job(p,w,*p.bankingJob,4,true));
    n.ownScore=150;w=observe_conversion(p,n,60);
    CHECK(w.confirmed());CHECK(w.lowerBound==21);CHECK(!w.exactAmount);
    CHECK(conversion_completes_job(p,w,*p.bankingJob,4,true));
    n.ownScore=120;w=observe_conversion(p,n,60);
    CHECK(!w.confirmed());CHECK(!w.exactAmount);
    n.ownScore=99;w=observe_conversion(p,n,60);
    CHECK(!w.intervalKnown);CHECK(!w.confirmed());CHECK(!w.exactAmount);
}
static void gaps_death_and_cancellation() {
    const auto p=previous();const auto n=current();
    for(unsigned which=0;which<11;++which) {
        auto a=p;auto b=n;int respawn=60;
        switch(which) {
        case 0:b.round=12;break;case 1:b.round=10;break;
        case 2:a.scoreRound=9;break;case 3:a.alive=false;break;
        case 4:b.alive=false;break;case 5:a.valid=false;break;
        case 6:b.valid=false;break;case 7:respawn=0;break;
        case 8:respawn=1;break;case 9:b.cancelled=true;break;
        default:a.bag=0;break;
        }
        const auto w=observe_conversion(a,b,respawn);
        CHECK(!w.confirmed());CHECK(!w.exactAmount);
    }
    // The old condition accepted a stale channel plus an ally's +1 submission.
    auto stale=p;auto unrelated=n;stale.round=4;stale.scoreRound=4;unrelated.ownScore=101;
    CHECK(!observe_conversion(stale,unrelated,60).confirmed());
    auto wrapped=p;wrapped.round=wrapped.scoreRound=std::numeric_limits<unsigned>::max();
    unrelated.round=0;unrelated.ownScore=121;
    CHECK(!observe_conversion(wrapped,unrelated,60).confirmed());
}
static void preserved_bag_and_open_channel() {
    auto p=previous();auto n=current();
    n.bag=21;n.ownScore=130;
    auto w=observe_conversion(p,n,60);CHECK(!w.confirmed());CHECK(w.exactAmount==0);
    n.bag=0;n.submitting=true;
    w=observe_conversion(p,n,60);CHECK(!w.confirmed());CHECK(w.exactAmount==0);
    n.submitting=false;p.submitting=false;
    w=observe_conversion(p,n,60);CHECK(!w.confirmed());CHECK(w.exactAmount==0);
}
static void job_phase_and_identity() {
    auto p=previous();const auto w=observe_conversion(p,current(),60);
    auto id=*p.bankingJob;
    CHECK(!conversion_completes_job(p,w,id,5,true));
    CHECK(!conversion_completes_job(p,w,id,4,false));
    id.firstGroup=8;CHECK(!conversion_completes_job(p,w,id,4,true));
    id=*p.bankingJob;id.kind=plan3::JobKind::bank;
    CHECK(!conversion_completes_job(p,w,id,4,true));
    p.bankingJob.reset();CHECK(!conversion_completes_job(p,w,id,4,true));
    for(const auto kind:{plan3::JobKind::cycle,plan3::JobKind::double_cycle,plan3::JobKind::bank,plan3::JobKind::invest}) {
        p=previous();p.bankingJob->kind=kind;
        CHECK(conversion_completes_job(p,w,*p.bankingJob,4,true));
    }
}
static void large_score_and_all_flags() {
    auto p=previous();auto n=current();
    p.ownScore=std::numeric_limits<unsigned>::max()-21;
    n.ownScore=std::numeric_limits<unsigned>::max();
    CHECK(observe_conversion(p,n,60).exactAmount==21);
    for(unsigned mask=0;mask<128;++mask) {
        p=previous();n=current();
        p.valid=mask&1;p.alive=mask&2;p.submitting=mask&4;
        n.valid=mask&8;n.alive=mask&16;n.submitting=mask&32;n.cancelled=mask&64;
        const auto w=observe_conversion(p,n,60);
        const bool expected=p.valid&&p.alive&&p.submitting&&n.valid&&n.alive&&!n.submitting&&!n.cancelled;
        CHECK(w.confirmed()==expected);
        if(w.confirmed()){CHECK(w.lowerBound==21);CHECK(w.exactAmount==21);}
    }
}
int main() {
    unique_and_overlapping_output();gaps_death_and_cancellation();
    preserved_bag_and_open_channel();job_phase_and_identity();large_score_and_all_flags();
    std::printf("conversion witness PASS %u checks\n",checks);
}
