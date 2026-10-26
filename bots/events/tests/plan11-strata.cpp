#include <cassert>
#include "../strata/src/policy.h"
int main(){
 using namespace p11;using P=p11_strata::Policy;State s;
 std::array<Candidate,3> jobs{};
 jobs[0].id=0;jobs[0].gems=8;jobs[0].gold=20;jobs[0].clear=8;jobs[0].travel=1;jobs[0].loss=100;
 jobs[1].id=1;jobs[1].gems=40;jobs[1].gold=160;jobs[1].clear=20;jobs[1].travel=5;jobs[1].loss=1300;jobs[1].incoming=60;
 jobs[2]=jobs[1];jobs[2].id=2;jobs[2].gems=500;jobs[2].incoming=400;jobs[2].loss=2000;
 assert(!feasible(s,jobs[1])&&P::admissible(s,jobs[1]));
 assert(choose<P>(s,jobs)==1); // valuable stored progress beats cheap safe farming
 assert(P::visits(s,jobs[1])==3);
 s.hp=600;assert(choose<P>(s,jobs)==0); // do not start a fresh partial visit while depleted
 jobs[1].committed=true;assert(choose<P>(s,jobs)==1);
 s.round=1995;assert(choose<P>(s,jobs)==-1);
}
