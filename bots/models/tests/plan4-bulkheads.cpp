#include "../bulkheads/src/bulkheads.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace plan4;
namespace b=plan4::bulkheads;
int checks=0;
void check(bool value,const char*message){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
Provenance seen(int round){return {round,Source::personal_vision,Status::exact_now};}
b::Onset onset(int id,int round){b::Onset o;o.episode_id=id;o.connector_id=id+100;o.blocker_id=id+200;o.round=round;o.current=seen(round);o.preceding=seen(round-1);o.preceding_clear=true;o.policy={42,true,true};return o;}
void observation(b::State&state,int id,int round,bool blocked=true){b::Observation o;o.episode_id=id;o.round=round;o.personally_visible=true;o.blocked=blocked;o.provenance=seen(round);o.connector_id=id+100;o.blocker_id=id+200;b::observe(state,o);}
void episode(b::State&state,int id,int start,int duration){b::observe_onset(state,onset(id,start));for(int r=1;r<=duration;r++)observation(state,id,start+r,r<duration);}
const int durations[]={24,28,32,36,40,44,48,52};
void fit(b::State&state){for(int n=0;n<8;n++)episode(state,n+1,10+n*100,durations[n]);}
int main(){b::State state;
 check(!b::freeze_fit(state)&&!state.fit.frozen,"no invented evidence for empty class");
 for(int n=0;n<7;n++)episode(state,n+1,10+n*100,durations[n]);
 check(state.fit.started==7&&!b::freeze_fit(state),"seven uncensored episodes do not meet eight-example gate");
 episode(state,8,710,52);check(b::freeze_fit(state)&&state.fit.supported,"first eight exogenous completed episodes support fixed row");
 check(state.fit.durations[0]==24&&state.fit.durations[7]==52&&state.fit.policy==42,"frozen durations retain first-started order and policy identity");
 check(b::observe_onset(state,onset(100,1000)),"fresh runtime cause admitted");
 auto q=b::release_quote(state,100,1000);
 check(q.supported&&q.examples_beyond_age==8&&q.residual_rounds==38,"onset law uses frozen mean38 from all eight durations");
 for(int r=1001;r<=1010;r++)observation(state,100,r);
 q=b::release_quote(state,100,1010);
 check(q.age==10&&q.mean_residual==28&&q.predicted_release==1038&&q.status==Status::predicted,"fresh persistence conditions age instead of restarting full duration");
 check(!b::release_quote(state,100,1011).supported,"unobserved next frame is not exact persistence or expiry");
 b::Path active,control;b::prepare_path(state,1010,true,active);b::prepare_path(state,1010,false,control);const int id=100;
 auto a=b::phase_wait(active,&id,1,1012),c=b::phase_wait(control,&id,1,1012);
 check(a.supported&&a.wait==26&&a.completion==1038,"active first exposure waits on absolute release tau");
 check(c.wait==28&&c.completion==1040,"control reinitializes local release at phase arrival plus residual");
 a=b::phase_wait(active,&id,1,1045);c=b::phase_wait(control,&id,1,1045);
 check(a.wait==0&&c.wait==28,"later phase inherits resolved absolute cause while disabled charges it anew");
 check(!b::phase_wait(active,&id,1,1000).supported,"backwards absolute phase clock rejected");
 const int duplicate[]={100,100};b::prepare_path(state,1010,true,active);a=b::phase_wait(active,duplicate,2,1012);
 check(a.wait==26,"duplicate cause dependency never adds parallel waits");
 check(b::phase_wait(active,nullptr,0,1040).supported,"no dependencies add no wait");
 check(!b::phase_wait(active,nullptr,1,1040).supported,"missing dependency vector unsupported");
 const int unknown=999;check(!b::phase_wait(active,&unknown,1,1040).supported,"unknown cause uses tagged M0 fallback");
 check(!b::phase_wait(active,&id,9,1040).supported,"dependency bound enforced before reading memory");
 for(int r=1011;r<=1036;r++)observation(state,100,r);
 q=b::release_quote(state,100,1036);check(q.examples_beyond_age==4&&q.residual_rounds==10,"strict D>age boundary retains exactly four survivor durations");
 observation(state,100,1037);q=b::release_quote(state,100,1037);check(q.examples_beyond_age==4&&q.residual_rounds==9,"integer ceil conditional residual uses same survivors");
 for(int r=1038;r<=1040;r++)observation(state,100,r);
 q=b::release_quote(state,100,1040);check(q.examples_beyond_age==3&&!q.supported,"three durations beyond age cannot support a release prediction");
 observation(state,100,1041,false);q=b::release_quote(state,100,1041);
 check(q.supported&&q.resolved&&q.status==Status::exact_now&&q.residual_rounds==0,"personally observed release is exact current fact independent of fit");
 q=b::release_quote(state,100,1042);check(q.supported&&q.resolved&&q.status==Status::derived&&q.predicted_release==1041,"historical episode release remains derived without claiming current connector visibility");
 const auto frozen=state.fit.durations;episode(state,101,1100,1);check(state.fit.started==8&&state.fit.durations==frozen,"later episodes cannot refit frozen first-eight cohort");
 b::State censored;
 b::observe_onset(censored,onset(1,10));observation(censored,1,11);b::finish_frame(censored,12);
 check(censored.fit.episodes[0].censor==b::Censor::sight_lost,"omitted current observation right-censors episode");
 check(!censored.causes[0].resolved&&censored.causes[0].unknown,"lost sight is unknown rather than witnessed release");
 observation(censored,1,13,false);check(!censored.fit.episodes[0].complete,"later visible release cannot uncensor missing interval");
 for(int n=1;n<9;n++)episode(censored,n+1,100*n,24);
 check(censored.fit.started==8&&!b::freeze_fit(censored)&&censored.fit.frozen,"one censored first episode prevents replacing it with ninth survivor");
 b::State endogenous;auto bad=onset(1,10);bad.policy.release_independent_of_own_actions=false;b::observe_onset(endogenous,bad);
 for(int r=11;r<=34;r++)observation(endogenous,1,r,r<34);
 for(int n=1;n<8;n++)episode(endogenous,n+1,n*100,24);
 check(!b::freeze_fit(endogenous)&&endogenous.fit.episodes[0].censor==b::Censor::nonexogenous_policy,"ordinary endogenous reference episodes cannot fit passive law");
 b::State action;b::observe_onset(action,onset(1,10));b::Observation reaction{1,11,true,false,true,seen(11),101,201};b::observe(action,reaction);
 check(action.fit.episodes[0].censor==b::Censor::action_conditioned&&!action.fit.episodes[0].complete,"own attack/stun/kill/movement reaction censors passive release");
 b::State partial;bad=onset(1,10);bad.preceding_clear=false;b::observe_onset(partial,bad);
 check(partial.fit.started==1&&partial.fit.episodes[0].censor==b::Censor::unknown_onset,"unknown earlier onset occupies first cohort slot without total-duration claim");
 check(!b::observe_onset(partial,bad)&&partial.fit.started==1,"same episode ID cannot restart clock or duplicate fit sample");
 b::State duplicatePhysical;b::observe_onset(duplicatePhysical,onset(1,10));bad=onset(2,10);bad.connector_id=101;bad.blocker_id=201;
 check(!b::observe_onset(duplicatePhysical,bad)&&duplicatePhysical.fit.started==1,"different IDs cannot manufacture several samples from one continuing physical cause");
 b::State incomplete;for(int n=0;n<8;n++)b::observe_onset(incomplete,onset(n+1,10+n));
 check(!b::freeze_fit(incomplete)&&incomplete.fit.frozen&&!incomplete.fit.supported,"freezing incomplete fixed cohort leaves class unsupported");
 b::State classes;bad=onset(1,10);bad.cause_class=b::CauseClass::hostile_altar_lock;
 check(!b::observe_onset(classes,bad)&&classes.fit.started==0,"altar locks never enter autonomous expiry class");
 bad.cause_class=b::CauseClass::own_recovery;check(!b::observe_onset(classes,bad),"own M0 recovery cannot become shared repeated disruption");
 b::State policy;fit(policy);check(b::freeze_fit(policy),"independent policy fixture supports another table");
 bad=onset(100,1000);bad.policy.fingerprint=99;b::observe_onset(policy,bad);check(!b::release_quote(policy,100,1000).supported,"runtime cause under different policy cannot borrow exogenous fitted law");
 bad=onset(101,1100);bad.policy.frozen=false;b::observe_onset(policy,bad);check(!b::release_quote(policy,101,1100).supported,"unfrozen runtime policy remains unsupported");
 b::State discontinuous;fit(discontinuous);b::freeze_fit(discontinuous);b::observe_onset(discontinuous,onset(100,1000));observation(discontinuous,100,1002);
 check(!b::release_quote(discontinuous,100,1002).supported,"fresh persistence after a visibility gap never restarts or preserves exact age");
 b::State wrong;bad=onset(1,10);bad.current.source=Source::own_state;b::observe_onset(wrong,bad);
 check(wrong.fit.episodes[0].censor==b::Censor::unknown_onset,"nonpersonal source cannot create exact connector onset");
 b::State many;for(int n=0;n<8;n++)check(b::observe_onset(many,onset(n+1,10)),"bounded cause slot admitted");
 check(!b::observe_onset(many,onset(9,10))&&many.fit.started==8,"ninth unresolved cause cannot overflow eight slots");
 b::State parallel;fit(parallel);b::freeze_fit(parallel);b::observe_onset(parallel,onset(100,1000));b::observe_onset(parallel,onset(101,1000));
 b::prepare_path(parallel,1000,true,active);const int ids[]={100,101};a=b::phase_wait(active,ids,2,1000);
 check(a.wait==38,"two distinct simultaneously releasing causes use maximum38 rather than sum76");
 observation(parallel,100,1001);observation(parallel,101,1001);b::prepare_path(parallel,1001,true,active);
 check(active.causes[0].predicted_release<=1038||active.causes[1].predicted_release<=1038,"fresh persistence recomputes conditional law without arbitrary restart");
 b::prepare_path(parallel,1001,true,active);a=b::phase_wait(active,nullptr,0,-1);check(!a.supported,"negative absolute arrival rejected");
 b::State fractional;for(int n=0;n<8;n++)episode(fractional,n+1,10+n*100,24+n);b::freeze_fit(fractional);b::observe_onset(fractional,onset(100,1000));
 q=b::release_quote(fractional,100,1000);check(q.mean_residual==27.5&&q.residual_rounds==28,"fractional release law conservatively rounds once to owner phases");
 for(int r=1001;r<=1027;r++)observation(fractional,100,r);
 q=b::release_quote(fractional,100,1027);check(q.examples_beyond_age==4&&q.mean_residual==2.5&&q.residual_rounds==3,"conditional fractional mean uses strict survivors and ceil");
 b::State mixed;for(int n=0;n<8;n++){auto v=onset(n+1,10+n*100);if(n==7)v.policy.fingerprint=43;b::observe_onset(mixed,v);for(int r=1;r<=24;r++)observation(mixed,n+1,v.round+r,r<24);}
 check(!b::freeze_fit(mixed),"different frozen fixture policies cannot be pooled into one v1 duration row");
 b::State replaced;fit(replaced);b::freeze_fit(replaced);b::observe_onset(replaced,onset(100,1000));
 b::Observation differentActor;differentActor.episode_id=100;differentActor.round=1001;differentActor.personally_visible=differentActor.blocked=true;differentActor.provenance=seen(1001);differentActor.connector_id=200;differentActor.blocker_id=999;
 b::observe(replaced,differentActor);check(!b::release_quote(replaced,100,1001).supported,"new occupant identity cannot continue an old actor release clock");
 std::printf("plan4-bulkheads: %d checks passed\n",checks);
}
