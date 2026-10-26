#include <cassert>
#include "../matching/src/policy.h"

namespace {
using p11::Candidate;
using p11::State;
using namespace p11_matching;

void shared_job(const State&tavi,const State&varg,const std::array<Job,1>&jobs,
        const std::array<Worker,2>&workers,int tavi_handle,int varg_handle) {
    const auto tavi_worker=std::find_if(workers.begin(),workers.end(),
        [&](const Worker&worker){return worker.id==tavi_handle;});
    const auto varg_worker=std::find_if(workers.begin(),workers.end(),
        [&](const Worker&worker){return worker.id==varg_handle;});
    assert(tavi_worker!=workers.end()&&varg_worker!=workers.end());
    const auto&job=jobs.front();
    // Each observer retains its own disclosed financial proxy. Worker design
    // identities must nevertheless agree, despite distinct physical handles.
    assert(row_utility(tavi,job,*tavi_worker,tavi_handle)==6.);
    assert(row_utility(tavi,job,*varg_worker,tavi_handle)==60./13.);
    assert(row_utility(varg,job,*tavi_worker,varg_handle)==60./16.);
    assert(row_utility(varg,job,*varg_worker,varg_handle)==60./19.);
    const int tavi_assignment=Policy::assign(tavi,jobs,workers,tavi_handle);
    const int varg_assignment=Policy::assign(varg,jobs,workers,varg_handle);
    assert(tavi_assignment==2&&varg_assignment==-1);
    const std::array<Candidate,1> details{{job.candidate}};
    assert(Policy::select(tavi,details,tavi_assignment)==2);
    // The exact assigned private wait survives the production selector even
    // though this available ready reward is individually feasible for Varg.
    assert(p11::choose<p11_tempo::Policy>(varg,details)==2);
    assert(Policy::select(varg,details,varg_assignment)==-1);
}
}

int main() {
    // Current pinned champion enums: Tavi11, Varg10. Their physical handles
    // on the saved five-champion roster are0/1, not these design identities.
    State tavi;tavi.identity=11;tavi.hp=tavi.maximum=900;tavi.strength=45;tavi.field_shop=true;
    State varg;varg.identity=10;varg.hp=varg.maximum=1000;varg.strength=40;
    std::array<Worker,2> workers{{{0,0,0,900,900,45,true,11},{1,0,2,1000,1000,40,true,10}}};
    Job job;job.x=2;job.y=0;job.candidate.id=2;job.candidate.travel=2;
    job.candidate.ready=true;job.candidate.clear=0;job.candidate.capture=6;
    job.candidate.gold=20;job.candidate.hp=120;
    const std::array<Job,1> jobs{{job}};
    shared_job(tavi,varg,jobs,workers,0,1);
    std::reverse(workers.begin(),workers.end());
    shared_job(tavi,varg,jobs,workers,0,1);

    // Renumber physical handles so their sort order reverses. The utility
    // optimum is unique, so ownership still follows Tavi's canonical row.
    for(auto&worker:workers)worker.id=worker.score_identity==11?41:3;
    shared_job(tavi,varg,jobs,workers,41,3);
    std::reverse(workers.begin(),workers.end());
    shared_job(tavi,varg,jobs,workers,41,3);

    // Canonical scoring identity is explicit for self as well as peers; an
    // observer's different local identity cannot overwrite a worker snapshot.
    auto other_identity=tavi;other_identity.identity=1;
    assert(row_utility(other_identity,job,workers[0],workers[0].id)
        ==row_utility(tavi,job,workers[0],workers[0].id));

    Worker unknown;unknown.id=0;unknown.hp=unknown.maximum=900;unknown.strength=45;
    assert(unknown.score_identity==-1);
    assert(!std::isfinite(row_utility(tavi,job,unknown,0)));
    const std::array<Worker,1> unknown_self{{unknown}};
    assert(Policy::assign(tavi,jobs,unknown_self,0)==-1);
    auto missing_peer=workers;missing_peer[1].score_identity=-1;
    const int known_handle=missing_peer[0].id,unknown_handle=missing_peer[1].id;
    assert(Policy::assign(tavi,jobs,missing_peer,known_handle)==2);
    assert(Policy::assign(varg,jobs,missing_peer,unknown_handle)==-1);

    // Physical identity remains the row key: canonical identities do not
    // bypass duplicate-handle rejection, and negative handles remain invalid.
    auto duplicate=workers;duplicate[1].id=duplicate[0].id;
    assert(Policy::assign(tavi,jobs,duplicate,duplicate[0].id)==-1);
    auto invalid_handle=workers[0];invalid_handle.id=-1;
    assert(!std::isfinite(row_utility(tavi,job,invalid_handle,-1)));
}
