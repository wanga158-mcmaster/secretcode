#define P10_REMOTE_WORK_GUARD 1
#define main prior_fixture_main
#include "plan10-cards-b-11-16.cpp"
#undef main
int main(){
 for(auto arm:{Arm::active,Arm::control}){
  (void)arm;
  auto w=frame(Champion::ryth);w.actor_observation_complete=true;w.groups={source(81,{20,20})};
  auto budget=p10_mobile::budget_for(w);p10_mobile::SkeletonCache cache;
  auto r=p10_mobile::mobile_work(w,w.groups[0],budget,cache);
  assert(!r.feasible&&r.failure==p10_mobile::Failure::blocked&&budget.spent==0);
  for(const auto&s:cache.slots)assert(s.source<0);
  // A ready ranged spell retains the full proof despite disconnected ingress.
  w.self.focus=1000;auto&a=w.self.abilities[0];a.ready=a.damage=a.targets_camps=true;a.modifier=1;a.range=30;
  budget=p10_mobile::budget_for(w);r=p10_mobile::mobile_work(w,w.groups[0],budget,cache);
  assert(r.feasible&&budget.spent==1&&r.first.task==Task::cast&&fresh_certificate(w,r.first));
  // Cooling-down or unavailable spells do not invent an initial interaction.
  w.self.cooldown[0]=1;budget=p10_mobile::budget_for(w);r=p10_mobile::mobile_work(w,w.groups[0],budget,cache);
  assert(!r.feasible&&r.failure==p10_mobile::Failure::blocked&&budget.spent==0);
  // Local work still uses the complete actual body, geometry and fresh hazards.
  w=frame(Champion::ryth);w.actor_observation_complete=true;w.groups={source(82,{2,0})};
  budget=p10_mobile::budget_for(w);r=p10_mobile::mobile_work(w,w.groups[0],budget,cache);
  assert(r.feasible&&budget.spent==1&&fresh_certificate(w,r.first));
  auto hazard=source(83,{1,1});hazard.observed=hazard.complete=false;hazard.monsters[0].observed=false;hazard.monsters[0].range=3;hazard.monsters[0].strength=500;w.groups.push_back(hazard);
  budget=p10_mobile::budget_for(w);r=p10_mobile::mobile_work(w,w.groups[0],budget,cache);
  assert(!r.feasible&&r.failure==p10_mobile::Failure::unknown_hazard);
 }
 return prior_fixture_main();
}
