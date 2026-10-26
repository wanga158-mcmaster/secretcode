#include <cassert>
#include "../matching/src/policy.h"

namespace {
using namespace p11_matching;
Job reward(int id,int x,int gold=10) {
    Job job;job.candidate.id=id;job.x=x;job.candidate.gold=gold;
    job.candidate.ready=true;job.candidate.hp=100;job.candidate.incoming=50;
    return job;
}
// Independent recursive oracle: enumerate each real choice and each row's own
// wait, checking unit capacity directly rather than sharing the DP recurrence.
double oracle(const Matrix&matrix,int rows,int columns,int row=0,int used=0) {
    if(row==rows)return 0;
    double best=oracle(matrix,rows,columns,row+1,used);
    for(int column=0;column<columns;++column)
        if(!(used&(1<<column))&&std::isfinite(matrix[row][column]))
            best=std::max(best,matrix[row][column]+oracle(matrix,rows,columns,row+1,used|(1<<column)));
    return best;
}
}

int main() {
    using namespace p11;
    using namespace p11_matching;
    static_assert(Policy::bounded_scan&&Policy::window_size==3&&Policy::local_assignment&&Policy::trace_scan);
    State state;state.field_shop=true;
    std::array<Worker,2> workers{{{0,0,0,1000,1000,100,true,0},{7,2,0,1000,1000,100,true,7}}};
    std::array<Job,2> jobs{{reward(0,1),reward(7,-2)}};
    jobs[0].candidate.travel=1;jobs[1].candidate.travel=2;
    // Both rows greedily prefer job 0, but row 7 loses more if forced to job 7.
    assert(row_utility(state,jobs[0],workers[0],0)>row_utility(state,jobs[1],workers[0],0));
    assert(row_utility(state,jobs[0],workers[1],0)>row_utility(state,jobs[1],workers[1],0));
    assert(Policy::assign(state,jobs,workers,0)==7);
    State ally_state=state;ally_state.identity=7;
    auto ally_jobs=jobs;ally_jobs[1].candidate.travel=4; // Row 7's own completed details.
    assert(Policy::assign(ally_state,ally_jobs,workers,7)==0);
    std::array<Candidate,2> detailed{{jobs[0].candidate,jobs[1].candidate}};
    assert(choose<p11_tempo::Policy>(state,detailed)==0);
    assert(Policy::select(state,detailed,Policy::assign(state,jobs,workers,0))==7);
    std::reverse(jobs.begin(),jobs.end());std::reverse(workers.begin(),workers.end());
    assert(Policy::assign(state,jobs,workers,0)==7); // Input order cannot change ties.

    std::array<Worker,3> three{{{14,0,0,1000,1000,100,true,0},{0,0,0,1000,1000,100,true,0},{7,0,0,1000,1000,100,true,7}}};
    std::array<Job,1> one{{reward(0,0)}};
    assert(Policy::assign(state,one,three,0)==0);
    assert(Policy::assign(state,one,three,7)==-1);
    assert(Policy::assign(state,one,three,14)==-1);
    assert(Policy::select(state,detailed,-1)==-1); // Explicit wait is executable.
    one[0].candidate.gold=0;
    assert(Policy::assign(state,one,three,0)==-1);

    // A visibly busy worker cannot reserve a second job while channeling,
    // holding a reward region or healing at spawn. Its private wait survives
    // the actual selector, even when it would otherwise clear fastest.
    std::array<Worker,2> available{{{0,0,0,1000,1000,100,true,0},{7,0,0,1000,1000,1000,false,7}}};
    std::array<Job,1> work{{reward(0,0)}};
    work[0].candidate.ready=false;work[0].candidate.hp=400;work[0].candidate.incoming=0;work[0].candidate.clear=4;
    std::array<Candidate,1> work_details{{work[0].candidate}};
    auto fast_work=work;fast_work[0].candidate.clear=1;
    std::array<Candidate,1> fast_details{{fast_work[0].candidate}};
    assert(!std::isfinite(row_utility(state,work[0],available[1],0)));
    assert(Policy::assign(state,work,available,0)==0);
    assert(Policy::select(state,work_details,Policy::assign(state,work,available,0))==0);
    assert(Policy::select(ally_state,fast_details,Policy::assign(ally_state,fast_work,available,7))==-1);
    available[1].available=true;
    assert(row_utility(state,work[0],available[1],0)>row_utility(state,work[0],available[0],0));
    assert(Policy::assign(ally_state,fast_work,available,7)==0);
    assert(Policy::select(ally_state,fast_details,Policy::assign(ally_state,fast_work,available,7))==0);
    assert(Policy::select(state,work_details,Policy::assign(state,work,available,0))==-1);

    Worker vulnerable{0,0,0,400,400,100,true,0};
    Job unsafe=reward(1,0);unsafe.candidate.ready=false;unsafe.candidate.hp=300;unsafe.candidate.incoming=70;
    // Even a zero detailed loss or observer Jungle emblem gives no mitigation
    // credit in another worker's conservative full-public-health coarse row.
    unsafe.candidate.loss=0;state.jungle_shards=5;
    assert(!std::isfinite(row_utility(state,unsafe,vulnerable,7)));
    auto detailed_unsafe=unsafe;detailed_unsafe.candidate.clear=3;detailed_unsafe.candidate.loss=500;
    std::array<Job,2> safety{{detailed_unsafe,reward(0,0)}};
    std::array<Worker,1> solo{{vulnerable}};
    assert(Policy::assign(state,safety,solo,0)==0);
    unsafe.candidate.hp=1050;unsafe.candidate.incoming=50;
    vulnerable.hp=vulnerable.maximum=800;state.monster_shards=5;
    assert(!std::isfinite(row_utility(state,unsafe,vulnerable,7))); // Raw Strength, no 1.1 bonus.

    // Full public HP would make this almost-finished camp unsafe. The self row
    // must keep its completed observed progress, mitigation and travel instead.
    State progressed=state;progressed.identity=2;progressed.hp=progressed.maximum=400;
    std::array<Worker,2> progress_workers{{{0,0,0,400,400,100,true,2},{7,0,0,400,400,100,true,7}}};
    std::array<Job,1> progress{{reward(3,3)}};
    auto&remaining=progress[0].candidate;remaining.ready=false;remaining.known=true;
    remaining.hp=3600;remaining.incoming=50;remaining.travel=2;remaining.clear=1;remaining.loss=20;
    remaining.allies=2;remaining.committed=true;
    auto controlled=remaining;controlled.allies=0;
    assert(row_utility(progressed,progress[0],progress_workers[0],0)==p11_tempo::Policy::score(progressed,controlled));
    assert(!std::isfinite(row_utility(progressed,progress[0],progress_workers[1],0)));
    assert(Policy::assign(progressed,progress,progress_workers,0)==3);
    std::array<Candidate,1> progress_details{{remaining}};
    assert(Policy::select(progressed,progress_details,Policy::assign(progressed,progress,progress_workers,0))==3);

    detailed[1].ready=false;detailed[1].loss=2000;
    assert(Policy::select(state,detailed,7)==0); // Fresh detailed safety wins.
    assert(Policy::select(state,detailed,123)==0); // Missing assignment falls back.
    detailed[1].ready=true;detailed[1].gems=10;state.round=state.limit;
    assert(Policy::select(state,detailed,7)==0); // A finite score is mandatory.
    state.round=0;
    // Private fog may produce a different local optimum. No shared observation
    // or cross-client agreement is presumed: a client seeing one job can pick it.
    std::array<Job,1> private_jobs{{reward(7,-2)}};
    std::array<Worker,1> private_worker{{{0,0,0,1000,1000,100,true,0}}};
    assert(Policy::assign(state,private_jobs,private_worker,0)==7);
    private_jobs[0]=reward(0,1);
    assert(Policy::assign(state,private_jobs,private_worker,0)==0);
    std::array<Worker,4> unsupported{{three[0],three[1],three[2],{21,0,0,1000,1000,100,true,0}}};
    assert(Policy::assign(state,jobs,unsupported,0)==-1);
    assert(Policy::assign(state,jobs,three,99)==-1);

    // All 3^8 matrices with entries {-1,0,2}, plus varied 3x3 matrices: fewer
    // than 10,000 cases, independently checking the exact objective and cap.
    int cases=0;
    for(int code=0;code<6561;++code) {
        int digits=code;Matrix matrix{};
        for(int row=0;row<3;++row)for(int column=0;column<3;++column) {
            const int digit=digits%3;digits/=3;
            matrix[row][column]=digit==0?-1:digit==1?0:2;
        }
        const auto result=maximize(matrix,3,3);
        assert(result.bounded&&result.transitions<=120);
        assert(result.value==oracle(matrix,3,3));
        int used=0;double value=0;
        for(int row=0;row<3;++row)if(result.job[row]>=0) {
            const int job=result.job[row];assert(!(used&(1<<job)));used|=1<<job;
            value+=matrix[row][job];
        }
        assert(value==result.value);++cases;
    }
    for(int code=0;code<1000;++code) {
        Matrix matrix{};
        for(int row=0;row<3;++row)for(int column=0;column<3;++column) {
            const int mixed=(code*(row+3)+column*17+row*7)%29;
            matrix[row][column]=mixed==0?std::numeric_limits<double>::quiet_NaN():double(mixed-8)/4;
        }
        const int rows=1+code%3,columns=code/3%4;
        const auto result=maximize(matrix,rows,columns);
        assert(result.bounded&&result.transitions<=120);
        assert(result.value==oracle(matrix,rows,columns));++cases;
    }
    assert(cases==7561);
    Matrix empty{};assert(!maximize(empty,4,3).bounded);
    assert(!maximize(empty,3,4).bounded);
}
