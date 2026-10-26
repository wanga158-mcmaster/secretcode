#include <cassert>
#include "../harvest/src/policy.h"
int main(){
 using namespace p11;using P=p11_harvest::Policy;
 State s;s.limit=100;s.mature=true;
 std::array<Candidate,3> jobs{};
 jobs[0].id=0;jobs[0].gems=20;jobs[0].clear=1;jobs[0].capture=5;jobs[0].renewal=2;
 jobs[1]=jobs[0];jobs[1].id=1;jobs[1].gems=40;jobs[1].renewal=1000;
 jobs[2]=jobs[0];jobs[2].id=2;jobs[2].gems=500;jobs[2].loss=900;
 assert(choose<P>(s,jobs)==0); // repeated low prize beats a single larger prize
 s.limit=10;assert(choose<P>(s,jobs)==1); // finite-horizon floor changes the decision
 assert(P::harvests(s,jobs[1])==1);
 s.limit=9;assert(choose<P>(s,jobs)==0); // larger tribute misses this owner-start deadline
 s.round=5;assert(P::harvests(s,jobs[0])==0);
 s.round=0;s.limit=100;s.bag=29;s.altar=10;assert(!P::bank(s));
 s.bag=30;assert(P::bank(s));s.bag=20;assert(P::bank(s));
 s.bag=19;assert(!P::bank(s));
}
