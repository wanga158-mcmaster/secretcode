#include "../metronome/src/metronome.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
using namespace plan4;namespace m=plan4::metronome;
int checks=0;void check(bool v,const char*s){++checks;if(!v){std::fprintf(stderr,"FAIL: %s\n",s);std::exit(1);}}
m::MatchedLaw law(){return {77,100,m::ClockLaw::engine_calendar_and_phase_order,true,true};}
m::Limitation limitation(){return {101,77,true,true,true};}
m::CostEnvelope cost(){return {102,77,100,8,true};}
m::Rollouts row(int nominal,int optimistic,int pessimistic,int evidence=10){m::Rollouts r;r.chebyshev={0,optimistic,20};r.nominal={0,nominal,30};r.pessimistic={0,pessimistic,50};r.forecast_round=10;r.route_evidence_round=evidence;r.pessimistic_arrival=50;r.physical_epoch=55;r.law_identity=77;r.all_complete=r.completed_legal_upper_route=true;return r;}
struct Context{std::array<m::Rollouts,max_jobs>updates{};int calls=0,complete_after=1;int candidates[8]{};int quotas[8]{};std::uint64_t fuel=100;bool bad_vertices=false,bad_candidate=false;};
m::Refinement refine(int candidate,int quota,void*ptr){auto&c=*static_cast<Context*>(ptr);c.candidates[c.calls]=candidate;c.quotas[c.calls]=quota;++c.calls;m::Refinement r;r.candidate=c.bad_candidate?candidate+1:candidate;r.vertices_used=c.bad_vertices?quota+1:quota;r.fuel_used=c.fuel;r.completed=c.calls>=c.complete_after;r.rollouts=c.updates[candidate];return r;}
void init(Information&info,Menu&menu){info=Information{};info.round=10;info.own.id=99;info.group_count=3;info.altar_count=1;for(int g=0;g<3;g++){info.groups[g].id=100+g;info.groups[g].position={g,0};menu.jobs[g].kind=JobKind::farm;menu.jobs[g].group=g;menu.jobs[g].altar=0;menu.jobs[g].menu_rank=g;}menu.count=3;}
bool begin(m::State&s,const Information&i,const Menu&menu,const std::array<m::Rollouts,max_jobs>&rows,bool enabled=true,int used=0,std::uint64_t fuel=10000){return m::begin(s,i,menu,rows.data(),menu.count,55,law(),limitation(),cost(),used,fuel,300,enabled);}
int main(){auto info=std::make_unique<Information>();Menu menu;init(*info,menu);auto state=std::make_unique<m::State>();std::array<m::Rollouts,max_jobs>rows{};rows[0]=row(50,50,45);rows[1]=row(40,100,10);rows[2]=row(20,30,0);
 check(begin(*state,*info,menu,rows)&&state->chosen==0,"complete cheap M0 nominal forecasts establish initial leader");
 check(state->optional_supported&&m::next_candidate(*state,*info)==1,"margin uncertainty prioritizes largest component gap");
 auto zRows=rows;zRows[2].chebyshev.z=1;begin(*state,*info,menu,zRows);check(m::next_candidate(*state,*info)==2,"uncertain terminal component precedes arbitrarily larger margin gap");
 auto timeRows=rows;for(int j=0;j<3;j++){timeRows[j]=row(50,50,50);timeRows[j].pessimistic.first_conversion=60;timeRows[j].chebyshev.first_conversion=25;}timeRows[2].chebyshev.first_conversion=11;
 begin(*state,*info,menu,timeRows);check(m::next_candidate(*state,*info)==2,"conversion-time gap has correct smaller-is-better direction");
 auto ageRows=rows;ageRows[0]=row(50,50,50);ageRows[1]=row(40,80,10,9);ageRows[2]=row(40,80,10,8);
 begin(*state,*info,menu,ageRows);check(m::next_candidate(*state,*info)==2,"equal component gap prioritizes oldest route evidence");ageRows[1].route_evidence_round=8;
 begin(*state,*info,menu,ageRows);check(m::next_candidate(*state,*info)==1,"equal age uses ascending unchanged common key");
 auto extrema=rows;extrema[0].chebyshev={0,40,20};extrema[0].nominal={0,50,30};extrema[0].pessimistic={1,1,100};begin(*state,*info,menu,extrema);
 check(state->candidates[0].interval.optimistic.z==1&&state->candidates[0].interval.pessimistic.margin==40,"interval extrema are lexicographic forecast tuples rather than scalar weights");
 auto invalid=rows;invalid[0].all_complete=false;check(!begin(*state,*info,menu,invalid)&&state->invalidated,"partial initial forecast never becomes a comparable cache entry");
 invalid=rows;invalid[0].completed_legal_upper_route=false;invalid[0].pessimistic_arrival=40;check(!begin(*state,*info,menu,invalid),"missing complete legal route requires beyond-H pessimistic arrival");
 invalid[0].pessimistic_arrival=139;check(!begin(*state,*info,menu,invalid),"beyond-H arrival cannot disguise positive pre-H pessimistic output");
 invalid[0].pessimistic={0,0,139};check(begin(*state,*info,menu,invalid),"missing route supports a complete no-conversion beyond-H pessimistic tuple");
 invalid[0].pessimistic.margin=1;check(!begin(*state,*info,menu,invalid),"missing route cannot support positive margin even with no-conversion sentinel");
 invalid[0].pessimistic={1,0,139};check(!begin(*state,*info,menu,invalid),"missing route cannot manufacture a terminal win");
 invalid[0].pessimistic={0,0,50};check(!begin(*state,*info,menu,invalid),"missing route cannot claim a completed pre-H conversion at zero margin");
 invalid=rows;invalid[0].completed_legal_upper_route=true;invalid[0].pessimistic_arrival=40;check(begin(*state,*info,menu,invalid),"completed cached legal route may provide finite upper arrival");
 invalid=rows;invalid[0].forecast_round=9;check(!begin(*state,*info,menu,invalid),"calendar-stale forecast rejected despite complete route cache");
 invalid=rows;invalid[0].physical_epoch=54;check(!begin(*state,*info,menu,invalid),"action or observation epoch mismatch invalidates physical forecast");
 invalid=rows;invalid[0].law_identity=76;check(!begin(*state,*info,menu,invalid),"different physical transition law cannot supply refinement tuple");
 auto badMenu=menu;badMenu.jobs[0].group=999;check(!begin(*state,*info,badMenu,rows),"malformed common key cannot escape bounded information arrays");
 auto missing=cost();missing.measured_operations=0;
 check(m::begin(*state,*info,menu,rows.data(),3,55,law(),limitation(),missing,0,10000,300,true)&&!state->optional_supported,"missing measured operation costs disable optional computation");Context ctx;ctx.updates=rows;
 check(m::refine(*state,*info,55,refine,&ctx)==0&&ctx.calls==0,"unsupported refinement retains complete cheap leader without spending vertices");
 auto noOpportunity=limitation();noOpportunity.competing_refinements_exceed_reserve=false;
 m::begin(*state,*info,menu,rows.data(),3,55,law(),noOpportunity,cost(),0,10000,300,true);check(!state->optional_supported,"no measured reference limitation means no Metronome opportunity");
 auto unmatched=law();unmatched.repeatable=false;m::begin(*state,*info,menu,rows.data(),3,55,unmatched,limitation(),cost(),0,10000,300,true);check(!state->optional_supported,"unmatched repeatable physical law stays unsupported");
 unmatched=law();unmatched.clock=m::ClockLaw::unsupported;m::begin(*state,*info,menu,rows.data(),3,55,unmatched,limitation(),cost(),0,10000,300,true);check(!state->optional_supported,"fuel elapsed time cannot substitute for calendar/phase-order law");
 ctx=Context{};ctx.updates=rows;for(auto&r:ctx.updates)r.useful_refinement_remaining=false;ctx.updates[1]=row(80,80,80);ctx.updates[1].useful_refinement_remaining=false;ctx.complete_after=2;
 begin(*state,*info,menu,rows);check(m::refine(*state,*info,55,refine,&ctx)==1&&ctx.calls==2,"only completed refinement publishes improved nominal and changes choice");
 check(state->candidates[1].interval.nominal.margin==80&&state->round==10&&state->candidates[1].interval.nominal.first_conversion==30,"computation changes estimate but never directly advances physical calendar time");
 check(state->vertices_used==64&&state->refinements==2,"two bounded operations debit same shared visit pool");
 ctx=Context{};ctx.updates=rows;ctx.complete_after=99;begin(*state,*info,menu,rows);
 check(m::refine(*state,*info,55,refine,&ctx)==0&&ctx.calls==8&&state->vertices_used==256,"eight-operation cap stops incomplete expensive candidate at shared quota");
 check(state->candidates[1].interval.nominal.margin==40,"partial graph work cannot overwrite complete cheap forecast");
 ctx=Context{};ctx.updates=rows;ctx.complete_after=99;begin(*state,*info,menu,rows,true,240);
 m::refine(*state,*info,55,refine,&ctx);check(ctx.calls==1&&ctx.quotas[0]==16&&state->vertices_used==256,"reference preprocessing consumes quota before optional work");
 ctx=Context{};ctx.updates=rows;ctx.complete_after=99;begin(*state,*info,menu,rows,true,256);m::refine(*state,*info,55,refine,&ctx);check(ctx.calls==0,"exhausted shared quota supplies no extra optional vertices");
 ctx=Context{};ctx.updates=rows;ctx.complete_after=99;
 check(m::begin(*state,*info,menu,rows.data(),3,55,law(),limitation(),cost(),80,10000,300,true,17),"stricter optional allowance keeps actual reference count");
 m::refine(*state,*info,55,refine,&ctx);
 check(ctx.calls==1&&ctx.quotas[0]==17&&state->vertices_used==97&&state->optional_vertices_used==17,"movement reservation is a separate cap rather than fictitious visited vertices");
 ctx=Context{};ctx.updates=rows;ctx.complete_after=99;begin(*state,*info,menu,rows,true,0,449);m::refine(*state,*info,55,refine,&ctx);check(ctx.calls==0,"1.5 maximum-cost plus reserve strict lower boundary stops before operation");
 begin(*state,*info,menu,rows,true,0,450);m::refine(*state,*info,55,refine,&ctx);check(ctx.calls==1&&state->fuel_remaining==350,"exact measured envelope boundary admits one operation without gem-time penalty");
 ctx=Context{};ctx.updates=rows;ctx.complete_after=99;ctx.bad_vertices=true;begin(*state,*info,menu,rows);m::refine(*state,*info,55,refine,&ctx);check(state->contract_violation&&ctx.calls==1,"refiner exceeding supplied32-vertex quota is a technical contract failure");
 check(!m::action_fresh(*state,*info,55),"quota contract failure makes an otherwise current chosen action inadmissible");
 ctx=Context{};ctx.updates=rows;ctx.complete_after=99;ctx.fuel=101;begin(*state,*info,menu,rows);m::refine(*state,*info,55,refine,&ctx);check(state->contract_violation&&ctx.calls==1,"measured envelope violation stops adaptation instead of online fitting a new bound");
 check(!m::action_fresh(*state,*info,55),"cost contract failure cannot retain action freshness");
 m::refine(*state,*info,55,refine,&ctx);check(ctx.calls==1,"same decision cannot resume optional work after a broken cost envelope");
 ctx=Context{};ctx.updates=rows;ctx.updates[1].physical_epoch=54;begin(*state,*info,menu,rows);m::refine(*state,*info,55,refine,&ctx);check(state->contract_violation&&state->candidates[1].interval.nominal.margin==40,"complete stale refinement cannot publish across physical epochs");
 check(!m::action_fresh(*state,*info,55),"identity contract failure cannot retain action freshness");
 ctx=Context{};ctx.updates=rows;ctx.updates[1]=row(999,999,999);ctx.fuel=101;begin(*state,*info,menu,rows);m::refine(*state,*info,55,refine,&ctx);
 check(state->contract_violation&&state->candidates[1].interval.nominal.margin==40&&!m::action_fresh(*state,*info,55),"completed profitable result from a broken cost envelope is never published or actionable");
 begin(*state,*info,menu,rows,true,0,299);check(!m::action_fresh(*state,*info,55),"reported fuel one below current action reserve rejects action freshness");
 begin(*state,*info,menu,rows,true,0,300);check(m::action_fresh(*state,*info,55),"exact current action reserve boundary remains admissible");
 ctx=Context{};ctx.updates=rows;ctx.complete_after=99;begin(*state,*info,menu,rows,false);m::refine(*state,*info,55,refine,&ctx);check(ctx.candidates[0]==0&&ctx.candidates[1]==1&&ctx.candidates[2]==2,"disabled arm uses round-robin with identical initial forecasts and limits");
 check(state->refinements==8&&state->vertices_used==256,"round-robin receives same operation cap and shared quota");
 auto obvious=rows;obvious[0]=row(100,100,100);obvious[1]=row(10,20,0);obvious[2]=row(5,10,0);begin(*state,*info,menu,obvious);ctx=Context{};ctx.updates=obvious;m::refine(*state,*info,55,refine,&ctx);check(ctx.calls==0,"obviously dominant interval stops when no operation can change ranking");
 check(m::action_fresh(*state,*info,55),"complete selected action retains current physical epoch");m::invalidate(*state);check(!m::action_fresh(*state,*info,55),"observation/action invalidation forbids continuing old suspended decision");
 begin(*state,*info,menu,rows);info->round=11;check(m::refine(*state,*info,55,refine,&ctx)==-1,"new engine round invalidates a suspended cached decision");info->round=10;begin(*state,*info,menu,rows);check(m::refine(*state,*info,56,refine,&ctx)==-1,"new physical epoch invalidates decision even in same engine round");
 std::printf("plan4-metronome: %d checks passed\n",checks);
}
