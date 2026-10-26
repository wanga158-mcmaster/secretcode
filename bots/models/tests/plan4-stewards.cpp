#include "../stewards/src/runtime.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
using namespace plan4;
namespace s = plan4::stewards;
int checks = 0;
int distance_quote(Point a,Point b,const void*){return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
void check(bool condition, const char* message) { ++checks; if (!condition) { std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1); } }
const int groups[]={40,10,30,20};const int actors[]={90,30,60};
s::GroupObservation no_service(int round,int group=10) {
    s::GroupObservation o;o.group_id=group;o.lifecycle=Lifecycle::living;
    o.all_member_spawns_visible=o.service_observation_complete=true;o.service=s::Service::none;
    o.lifecycle_provenance=o.visibility_provenance=o.service_provenance={round,Source::personal_vision,Status::exact_now};return o;
}
void put(s::State& state,s::GroupObservation& o,int round){s::observe(state,{round,1,&o});}
void run(s::State& state,int first,int last){for(int r=first;r<=last;r++){auto o=no_service(r);put(state,o,r);}}
int main(){auto state=std::make_unique<s::State>();
 check(s::configure(*state,groups,4,actors,3,60,true),"actual three actor draft supported");
 auto q=s::availability(*state,10,0,true);
 check(q.primary_id==30&&q.backup_id==60,"both ID sets sorted before assignment");
 check(q.predicted_consumed&&!q.own_available&&q.status==Status::predicted,"static other responsibility is a forecast surrogate");
 check(s::availability(*state,20,0,true).primary_id==60,"ordinal mod actual three-slot roster");
 check(s::availability(*state,40,0,true).backup_id==60,"backup wraps group ordinal cyclically");
 run(*state,1,7);q=s::availability(*state,10,7,true);
 check(q.full_sight_clock==7&&!q.backup_eligible&&!q.own_available,"seven full absent rounds insufficient");
 run(*state,8,8);q=s::availability(*state,10,8,true);
 check(q.backup_eligible&&q.own_available&&q.predicted_owner_id==60,"eighth consecutive full-sight round enables designated backup");
 check(!s::availability(*state,10,8,false).own_available,"control retains static owner on identical evidence");
 check(!s::availability(*state,10,9,true).backup_eligible,"old absence cannot be current authority");
 const int reorderedGroups[]={20,30,10,40},reorderedActors[]={60,90,30};
 check(s::configure(*state,reorderedGroups,4,reorderedActors,3,60,true),"permuted same receipt valid");
 check(s::availability(*state,10,8,true).full_sight_clock==8,"input ordering preserves evidence");
 auto o=no_service(9);o.all_member_spawns_visible=false;put(*state,o,9);
 check(!s::availability(*state,10,9,true).backup_eligible,"one hidden member spawn resets responsibility evidence");
 run(*state,10,16);check(s::availability(*state,10,16,true).full_sight_clock==7,"gap starts new clock");
 o=no_service(17);o.service=s::Service::owner_presence;o.servicing_actor=30;put(*state,o,17);
 check(s::availability(*state,10,17,true).full_sight_clock==0,"visible primary service resets clock");
 run(*state,18,24);o=no_service(25);o.service=s::Service::ally_cleared_spawn;o.lifecycle=Lifecycle::cleared;o.servicing_actor=90;put(*state,o,25);
 check(s::availability(*state,10,25,true).full_sight_clock==0,"any legal ally cleared-spawn occupancy is service");
 run(*state,26,32);o=no_service(33);o.service=s::Service::predicted_participation;o.servicing_actor=90;
 o.personally_observed_health_reduction=true;o.participation_ambiguous=false;o.service_provenance.status=Status::predicted;put(*state,o,33);
 check(s::availability(*state,10,33,true).full_sight_clock==0,"supported predicted participation resets absence, without exact attack claim");
 run(*state,34,40);o=no_service(41);o.service=s::Service::ambiguous;put(*state,o,41);
 check(!s::availability(*state,10,41,true).backup_eligible,"ambiguous service is not proved absence");
 run(*state,42,48);o=no_service(49);o.service_observation_complete=false;put(*state,o,49);
 check(s::availability(*state,10,49,true).full_sight_clock==0,"incomplete negative service view cannot enable backup");
 run(*state,50,56);o=no_service(57);o.lifecycle=Lifecycle::captured;put(*state,o,57);
 check(s::availability(*state,10,57,true).full_sight_clock==0,"captured lifecycle resets clock");
 o=no_service(58);put(*state,o,58);check(s::availability(*state,10,58,true).full_sight_clock==0,"fresh renewed lifecycle starts no abandoned-clock credit");
 run(*state,59,66);check(s::availability(*state,10,66,true).backup_eligible,"new cycle can earn independent eight-round eligibility");
 s::observe(*state,{67,0,nullptr});check(!s::availability(*state,10,67,true).backup_eligible,"omitted group resets full-sight clock");
 run(*state,68,75);o=no_service(76);put(*state,o,76);put(*state,o,76);
 check(!s::availability(*state,10,76,true).backup_eligible,"duplicate frame cannot double-count or preserve cherry-picked absence");
 o=no_service(77);o.lifecycle=Lifecycle::cleared;o.own_capture_witnessed=true;o.capture_provenance={77,Source::own_occupancy,Status::exact_now};o.all_member_spawns_visible=false;put(*state,o,77);
 check(s::availability(*state,10,77,false).own_available&&s::availability(*state,10,77,false).protected_capture,"both arms protect own witnessed capture despite ownership/vision changes");
 check(!s::availability(*state,10,78,false).protected_capture,"capture protection needs a fresh witness");
 s::observe(*state,{77,-1,nullptr});check(!s::availability(*state,10,77,false).protected_capture,"malformed frame clears old protected evidence");
 o=no_service(78);o.lifecycle_provenance.observed_round=77;put(*state,o,78);
 check(s::availability(*state,10,78,true).full_sight_clock==0,"stale exact lifecycle cannot advance clock");
 run(*state,79,86);o=no_service(88);put(*state,o,88);check(s::availability(*state,10,88,true).full_sight_clock==1,"missing actor round resets consecutive clock");
 const int badActors[]={30,30};check(!s::configure(*state,groups,4,badActors,2,30,true),"duplicate roster unsupported");
 check(s::availability(*state,10,88,true).own_available&&!s::availability(*state,10,88,true).supported,"unsupported receipt uses tagged M0 fallback");
 check(!s::configure(*state,groups,4,actors,3,60,false),"requested draft without complete actual receipt unsupported");
 check(s::configure(*state,groups,4,actors,3,60,true),"valid receipt may recover");
 check(s::availability(*state,10,88,true).full_sight_clock==0,"recovery from unsupported receipt resets history");
 const int one[]={60};check(s::configure(*state,groups,4,one,1,60,true),"one actual actor needs no nonexistent backup");
 run(*state,1,8);q=s::availability(*state,10,8,true);check(q.primary_id==60&&q.backup_id==60&&q.own_available,"one-slot cyclic backup stays own");
 auto info=std::make_unique<Information>();info->group_count=1;info->groups[0].id=10;info->round=8;
 s::Policy p{state.get(),true};check(s::acquisition_allowed(0,100,*info,&p,nullptr),"private gate adapter maps information group ID at decision clock");
 check(s::availability(*state,999,8,true).own_available&&!s::availability(*state,999,8,true).supported,"unknown public group explicitly falls back");
 check(s::configure(*state,groups,4,actors,3,60,true),"integration configures actual backup roster");run(*state,1,8);
 info->own.id=60;info->own.health=info->own.maximum_health=1000;info->travel={distance_quote,nullptr};
 auto& group=info->groups[0];group.position={1,0};group.lifecycle=Lifecycle::cleared;
 group.gems=20;group.respawn_rounds=200;group.reward_available=group.visible=group.all_spawns_visible=group.occupancy_known=true;
 group.clear_rounds=group.full_clear_rounds=0;group.provenance=group.health_provenance={8,Source::personal_vision,Status::exact_now};
 info->altar_count=1;info->altars[0].position={2,0};info->altars[0].altar_position={3,0};info->altars[0].lock_known=true;
 auto menu=make_menu(*info);Job job;job.kind=JobKind::farm;job.group=0;job.altar=0;
 auto active=std::make_unique<Forecast>(),control=std::make_unique<Forecast>();
 s::predict_into(*info,job,menu,*state,true,*active);s::predict_into(*info,job,menu,*state,false,*control);
 check(active->our_secured==20&&control->our_secured==0,"same common FARM menu consumes static other source until active legal takeover");
 check(menu.count==make_menu(*info).count,"policy forecast never mutates common menu");
 o=no_service(9);o.lifecycle=Lifecycle::cleared;o.own_capture_witnessed=true;o.capture_provenance={9,Source::own_occupancy,Status::exact_now};o.all_member_spawns_visible=false;put(*state,o,9);
 info->round=9;group.provenance.observed_round=group.health_provenance.observed_round=9;
 s::predict_into(*info,job,menu,*state,false,*control);check(control->our_secured==20,"common wrapper protects own witnessed capture in disabled arm");
 info->own.id=999;s::predict_into(*info,job,menu,*state,false,*control);check(control->our_secured==20,"mismatched actor state explicitly falls back to M0 rather than another actor ownership");
 o=no_service(10);o.own_capture_witnessed=true;o.capture_provenance={10,Source::own_occupancy,Status::exact_now};put(*state,o,10);
 check(!s::availability(*state,10,10,false).protected_capture,"living lifecycle cannot borrow an inconsistent cleared-capture witness flag");
 std::printf("plan4-stewards: %d checks passed\n",checks);
}
