#include "../src/reservoir-policy.h"
#include <cassert>
#include <cstdio>
#include <limits>
int main(){
 using namespace reservoir;
 static_assert(!enabled_by_score(47,0));
 static_assert(enabled_by_score(48,0));
 static_assert(!enabled_by_score(80,1));
 static_assert(!enabled_by_score(-1,0));
 static_assert(due_window(100,110,8));
 static_assert(due_window(100,109,8));
 static_assert(!due_window(100,108,8));
 static_assert(!due_window(100,111,8));
 static_assert(!due_window(100,112,9));
 static_assert(!due_window(100,100,0));
 static_assert(due_window(100,101,0));
 static_assert(eligibility_start(100,90)==100);
 static_assert(eligibility_start(100,101)==101);
 static_assert(!eligible_phase(99,98,100,0));
 static_assert(!eligible_phase(100,99,100,1));
 static_assert(eligible_phase(100,99,100,0));
 static_assert(!eligible_phase(103,100,100,0));
 static_assert(!elapsed_hold_limit(168,170));
 static_assert(!elapsed_hold_limit(169,170));
 static_assert(!elapsed_hold_limit(170,170));
 static_assert(!elapsed_hold_limit(201,170));
 static_assert(elapsed_hold_limit(202,170));
 static_assert(elapsed_hold_limit(203,170));
 static_assert(!elapsed_hold_limit(0,1));
 constexpr auto largest=std::numeric_limits<unsigned>::max();
 static_assert(!elapsed_hold_limit(largest-1,largest));
 static_assert(elapsed_hold_limit(largest,largest-32));
 unsigned probe_phases=0,probe_last=168,first_release=0,control_exit=0;
 for(unsigned now=168;now<=202;++now){
  if(eligible_phase(now,probe_last,170,now<170?170-now:0))++probe_phases;
  if(!control_exit&&now+1>=170)control_exit=now;
  if(probe_phases>=maximum_hold||elapsed_hold_limit(now,170)){first_release=now;break;}
  probe_last=now;
 }
 assert(control_exit==169);
 assert(first_release==201);
 assert(probe_phases==32);
 unsigned phases=0,last=98;
 for(unsigned now=99;now<132;now++){
  if(eligible_phase(now,last,100,now<100?1:0))++phases;
  last=now;
 }
 assert(phases==maximum_hold);
 // Early occupation never counts as denial and a lost owner observation does
 // not credit the missing phase. The round limit still forces release at32.
 unsigned conservative=0;
 for(unsigned now=90;now<132;now++)if(now!=105){
  if(eligible_phase(now,last,100,now<100?1:0))++conservative;
  last=now;
 }
 assert(conservative==30);
 std::puts("Reservoir policy: 31 boundary/phase checks passed");
}
