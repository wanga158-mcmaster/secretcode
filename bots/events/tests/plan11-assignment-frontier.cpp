#include <cassert>
#include "../matching/src/policy.h"

namespace {
using namespace p11_matching;
using p11::Candidate;
using p11::State;
using Menu=std::array<Candidate,20>;

Menu public_menu() {
    Menu menu{};
    for(int id=0;id<int(menu.size());++id) {
        auto&c=menu[id];c.id=id;c.travel=25;c.clear=2;c.capture=6;
        c.hp=120;c.gold=20;c.altar=3;
    }
    // Current-stat isolated gold clusters are safe positive self work. The
    // tempting full Elks are unsafe for all three current-stat workers.
    for(int id:{13,14,15})menu[id].travel=5;
    for(int id:{10,11}) {
        auto&c=menu[id];c.travel=5;c.clear=35;c.hp=2400;
        c.incoming=60;c.loss=2220;c.gold=90;c.gems=15;
    }
    menu[11].committed=true;
    return menu;
}

std::vector<Job> detailed_jobs(const Menu&menu,std::span<const int> order) {
    std::vector<Job> jobs;
    for(const int id:order) {
        auto c=menu[id];c.known=true;
        const int y=id>=13&&id<=15?(id-13)*2:0;
        jobs.push_back({c,6,y});
    }
    return jobs;
}

std::vector<Candidate> candidates(const std::vector<Job>&jobs) {
    std::vector<Candidate> result;
    for(const auto&job:jobs)result.push_back(job.candidate);
    return result;
}

void actual_caller_path() {
    State state;state.identity=2;state.hp=state.maximum=1000;state.strength=70;
    auto menu=public_menu();
    const std::array<Worker,3> workers{{{0,5,0,900,900,45,true,11},
        {1,5,1,1000,1000,40,true,10},{2,0,0,1000,1000,70,true,2}}};
    int fallback=-1,best=100000;
    for(const auto&c:menu)if(p11::feasible(state,c)&&c.travel+c.clear<best) {
        best=c.travel+c.clear;fallback=c.id;
    }
    assert(fallback==13);
    constexpr int incumbent=11,explore=19;
    auto old_order=p11::shortlist<Policy>(state,menu,3,incumbent,fallback);
    auto promote=[&](int id) {
        const auto found=std::find(old_order.begin(),old_order.end(),id);
        if(found!=old_order.end())std::rotate(old_order.begin(),found,found+1);
    };
    promote(incumbent);promote(fallback);
    assert((old_order==std::vector<int>{13,11,10}));
    auto old_jobs=detailed_jobs(menu,old_order);
    const int old_assignment=Policy::assign(state,old_jobs,workers,2);
    assert(old_assignment==-1);
    assert(Policy::select(state,candidates(old_jobs),old_assignment)==-1);

    const auto order=Policy::detail_frontier(state,menu,3,incumbent,fallback,explore);
    assert(order.size()==3);
    auto sorted=order;std::sort(sorted.begin(),sorted.end());
    assert((sorted==std::vector<int>{13,14,15}));
    for(const int id:order) {
        assert(p11::feasible(state,menu[id]));
        assert(std::isfinite(Policy::score(state,menu[id]))&&Policy::score(state,menu[id])>0);
    }
    const auto jobs=detailed_jobs(menu,order);
    const int assigned=Policy::assign(state,jobs,workers,2);
    assert(std::find(order.begin(),order.end(),assigned)!=order.end());
    assert(Policy::select(state,candidates(jobs),assigned)==assigned);
    // The proof assumes these public edges survive completed detail. It does
    // not promise safe hidden fights or bypass re-observed feasibility checks.
    std::reverse(menu.begin(),menu.end());
    assert(Policy::detail_frontier(state,menu,3,incumbent,fallback,explore)==order);
}

void pool_edges() {
    State state;state.identity=2;state.hp=state.maximum=1000;state.strength=70;
    auto menu=public_menu();
    const auto expected=Policy::detail_frontier(state,menu,3,11,13,19);
    std::vector<Candidate> duplicated(menu.begin(),menu.end());
    duplicated.push_back(menu[13]);duplicated.push_back(menu[14]);
    const auto unique=Policy::detail_frontier(state,duplicated,3,11,13,19);
    assert(unique==expected);
    assert(std::find(unique.begin(),unique.end(),11)==unique.end()); // Unsafe incumbent is unprotected.
    for(auto&c:menu)c.loss=2000;
    const auto legacy=p11::shortlist<Policy>(state,menu,3,11,13);
    assert(Policy::detail_frontier(state,menu,3,11,13,19)==legacy);
    menu[13].loss=0;
    const auto one=Policy::detail_frontier(state,menu,3,11,13,19);
    assert(one.size()==3&&one.front()==13);
    auto sorted=one;std::sort(sorted.begin(),sorted.end());
    assert(std::adjacent_find(sorted.begin(),sorted.end())==sorted.end());
    menu[14].loss=0;
    const auto two=Policy::detail_frontier(state,menu,3,11,13,19);
    assert(two.size()==3&&two[0]==14&&two[1]==13);
    assert(Policy::detail_frontier(state,std::span<const Candidate>{},3,11,13,19).empty());
    assert(Policy::detail_frontier(state,menu,0,11,13,19).empty());
    assert(Policy::detail_frontier(state,menu,1,11,13,19).size()==1);
    assert(Policy::detail_frontier(state,menu,2,11,13,19).size()==2);
    auto expired=public_menu();state.round=state.limit;
    for(auto&c:expired)c.gems=1;
    assert(Policy::detail_frontier(state,expired,3,11,13,19).empty());
}

void positive_self_capacity() {
    // Hall/capacity argument: at most two other workers can occupy two jobs.
    // If this active worker has three positive finite edges and was waiting,
    // assigning its unused job would strictly improve the objective. This
    // theorem is about the detailed utility matrix, not its public forecast.
    constexpr std::array<double,3> values{{-std::numeric_limits<double>::infinity(),0.,4.}};
    int checked=0;
    for(int self=0;self<3;++self)for(int pattern=0;pattern<4;++pattern)
        for(int code=0;code<729;++code) {
            Matrix matrix{};int digits=code;
            for(int row=0;row<3;++row)for(int column=0;column<3;++column) {
                if(row==self)matrix[row][column]=column<2&&(pattern&(1<<column))?3.:.5;
                else {matrix[row][column]=values[digits%3];digits/=3;}
            }
            const auto result=maximize(matrix,3,3);
            assert(result.bounded&&result.transitions<=36&&result.job[self]>=0);
            int used=0;
            for(const int job:result.job)if(job>=0){assert(!(used&(1<<job)));used|=1<<job;}
            ++checked;
        }
    assert(checked==8748);
    // Two positive jobs are insufficient: two more useful peer rows can own
    // both, and the exact optimum then correctly leaves this worker waiting.
    Matrix insufficient{};insufficient[0]={.5,.5,0};
    insufficient[1]={100,0,0};insufficient[2]={0,100,0};
    assert(maximize(insufficient,3,2).job[0]==-1);
}
}

int main() {
    actual_caller_path();pool_edges();positive_self_capacity();
}
