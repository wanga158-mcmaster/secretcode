#include "../stewards/src/runtime.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
using namespace plan4;
using namespace plan4::runtime;
static int checks=0;
static void check(bool okay,const char* message){++checks;if(!okay){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
static int metric(Point a,Point b,const void*){return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
struct Fixture {
    std::unique_ptr<Information> info=std::make_unique<Information>();
    ActorFrame actors{};ProducerFrame producers{};
    std::array<PublicMember,2> members{{{10,100,0,2,0,3,{10,10},1350},{10,101,1,2,0,3,{11,10},1350}}};
    PublicGroup public_group{10,0,2,true};
    Fixture(){
        info->own.id=60;info->own.position={5,10};info->own.health=info->own.maximum_health=1000;info->travel={metric,nullptr};
        info->group_count=2;info->groups[0].id=20;info->groups[0].position={30,30};info->groups[1].id=10;
        auto& group=info->groups[1];group.position={10,10};group.visible=group.all_spawns_visible=group.reward_available=group.occupancy_known=true;
        group.lifecycle=Lifecycle::cleared;group.gems=20;group.gold=7;group.respawn_rounds=200;group.clear_rounds=group.full_clear_rounds=0;
        info->altar_count=1;info->altars[0].position={12,10};info->altars[0].altar_position={12,11};info->altars[0].lock_known=true;
        actors.allied_count=3;actors.enemy_count=1;actors.allied_roster={90,30,60};actors.enemy_roster[0]=1000;
        actors.roster_complete=actors.own_observed=actors.draft_order_known=true;actors.clock.own.id=60;
        actors.own.id=60;actors.own.position={5,10};actors.own.ally=actors.own.alive=true;actors.own.draft_ordinal=1;
        actors.own.health=actors.own.maximum_health=1000;actors.own.attack_range=1;
        actors.count=2;actors.samples[0]=actors.own;actors.samples[0].id=30;actors.samples[0].draft_ordinal=0;
        actors.samples[0].position={10,5};actors.samples[0].completed_turn=true;
        actors.samples[1]=actors.own;actors.samples[1].id=90;actors.samples[1].draft_ordinal=2;actors.samples[1].position={10,15};
    }
    Group& group(){return info->groups[info->groups[0].id==10?0:1];}
    void round(int r){
        info->round=r;info->own.position=actors.own.position;actors.clock.round=r;
        actors.own.provenance={r,Source::own_state,Status::exact_now};
        for(int n=0;n<actors.count;++n)actors.samples[n].provenance={r,Source::personal_vision,Status::exact_now};
        group().provenance=group().health_provenance={r,Source::personal_vision,Status::exact_now};
        producers={};producers.clock=actors.clock;producers.count=actors.count;
        for(int n=0;n<producers.count;++n){const auto& body=actors.samples[n];auto& row=producers.samples[n];
            row.actor_id=body.id;row.position=body.position;row.personally_visible=true;row.provenance=body.provenance;}
    }
    void feed(Stewards& runtime,int r,bool snapshot=true,bool layout=false){
        round(r);runtime.observe_actors(actors);runtime.observe_lifecycles({actors.clock,&group(),1,false});runtime.observe_producers(producers);
        if(layout)runtime.observe_layout({actors.clock,&public_group,1,members.data(),2,false});
        if(snapshot)runtime.observe_snapshot(*info);
    }
};
static void paired_takeover(){
    auto fixture=std::make_unique<Fixture>();auto& f=*fixture;Stewards active(true),control(false);
    f.feed(active,1,true,true);f.feed(control,1,true,true);
    for(int r=2;r<=7;++r){f.feed(active,r,false);f.feed(control,r,false);}
    f.feed(active,8);f.feed(control,8);
    const auto a=active.availability(10,8),b=control.availability(10,8);
    check(a.backup_eligible&&a.own_available&&a.predicted_owner_id==60&&a.full_sight_clock==8,"eight fresh complete cleared-group frames enable designated backup");
    check(b.backup_eligible&&!b.own_available&&b.predicted_owner_id==30&&b.full_sight_clock==8,"control sees identical evidence but retains static owner");
    check(active.state().round==control.state().round&&active.state().records[0].clock==control.state().records[0].clock,"active and control reconciliation overhead/state match");
    const auto menu=make_menu(*f.info);Job job{JobKind::farm,1,-1,0};
    auto af=std::make_unique<Forecast>(),cf=std::make_unique<Forecast>();active.predict_into(*f.info,job,menu,*af);control.predict_into(*f.info,job,menu,*cf);
    check(af->our_secured==20&&cf->our_secured==0,"unchanged common FARM menu and eligibility hook produce active/control counterexample");
    const auto active_evidence=active.model_evidence(),control_evidence=control.model_evidence();
    check(active_evidence.reported&&active_evidence.supported&&active_evidence.coupling_enabled&&active_evidence.samples==1&&active_evidence.paths==1,
        "current takeover premise and completed eligibility forecast are reported without an effect claim");
    check(control_evidence.supported&&!control_evidence.coupling_enabled&&control_evidence.samples==active_evidence.samples&&control_evidence.unknown==active_evidence.unknown,
        "disabled arm reports same proof counts and static-law support separately from coupling flag");
    check(menu.count==make_menu(*f.info).count,"runtime never removes responsibility rows from shared menu");
    f.feed(active,9);active.observe_snapshot(*f.info);
    check(active.availability(10,9).full_sight_clock==0,"duplicate planning snapshot invalidates clock rather than double counting");
}
static void missing_evidence(){
    auto fixture=std::make_unique<Fixture>();auto& f=*fixture;Stewards runtime;
    check(!runtime.model_evidence().supported&&runtime.model_evidence().reason==int(Stewards::EvidenceReason::unconfigured),"unconfigured adapter evidence reports explicit lack of static binding");
    f.feed(runtime,0,true,true);
    check(runtime.model_evidence().supported&&runtime.model_evidence().samples==0&&runtime.model_evidence().reason==int(Stewards::EvidenceReason::no_takeover_proof),
        "configured static responsibility stays supported before any takeover opportunity");
    for(int r=1;r<=8;++r)f.feed(runtime,r,true,r==1);
    f.actors.count=1;f.feed(runtime,9);
    check(runtime.availability(10,9).full_sight_clock==0,"hidden allied roster actor is not proved absent");
    f.actors.count=2;for(int r=10;r<=16;++r)f.feed(runtime,r);
    f.info->groups[1].all_spawns_visible=false;f.feed(runtime,17);
    check(runtime.availability(10,17).full_sight_clock==0,"one hidden member spawn resets clock despite cached public geometry");
    f.info->groups[1].all_spawns_visible=true;for(int r=18;r<=24;++r)f.feed(runtime,r);
    f.info->groups[1].lifecycle=Lifecycle::living;f.feed(runtime,25);
    check(runtime.availability(10,25).full_sight_clock==0,"living absence stays unknown without bounded allied spell-service reach");
    f.info->groups[1].lifecycle=Lifecycle::cleared;
    f.actors.samples[0].attack_range=-1;f.feed(runtime,26);
    check(runtime.availability(10,26).full_sight_clock==0,"unknown visible attack reach is not negative service proof");f.actors.samples[0].attack_range=1;
    f.round(27);f.actors.samples[0].provenance.observed_round=26;runtime.observe_actors(f.actors);
    runtime.observe_lifecycles({f.actors.clock,&f.info->groups[1],1,false});runtime.observe_producers(f.producers);runtime.observe_snapshot(*f.info);
    check(runtime.availability(10,27).full_sight_clock==0,"stale body cannot advance abandonment");
    f.actors.truncated=true;f.feed(runtime,28);check(runtime.availability(10,28).full_sight_clock==0,"partial actor collector prefix remains unknown");f.actors.truncated=false;
    f.round(29);runtime.observe_actors(f.actors);runtime.observe_lifecycles({f.actors.clock,nullptr,0,false});runtime.observe_producers(f.producers);
    runtime.observe_snapshot(*f.info);check(runtime.availability(10,29).full_sight_clock==1,"current planning snapshot can supply fresh lifecycle omitted by bounded online prefix");
    f.round(30);runtime.observe_actors(f.actors);runtime.observe_producers(f.producers);runtime.observe_snapshot(*f.info);
    check(runtime.availability(10,30).full_sight_clock==0,"missing lifecycle callback cannot reuse prior evidence");
    Stewards no_layout;for(int r=1;r<=9;++r)f.feed(no_layout,r);
    check(!no_layout.availability(10,9).backup_eligible,"no immutable member receipt cannot fabricate cleared-group geometry");
}
static void scheduling_and_service(){
    auto fixture=std::make_unique<Fixture>();auto& f=*fixture;Stewards runtime;
    for(int r=1;r<=7;++r)f.feed(runtime,r,true,r==1);
    f.actors.samples[0].next_priority=-1;f.feed(runtime,8);
    check(runtime.availability(10,8).full_sight_clock==0,"announced own-versus-ally priority flip resets eight-frame proof");
    f.actors.samples[0].current_priority=-1;f.actors.samples[0].completed_turn=false;f.feed(runtime,9);
    check(runtime.availability(10,9).full_sight_clock==0,"realized order change also resets rather than carrying old interval");
    for(int r=10;r<=17;++r)f.feed(runtime,r);
    check(runtime.availability(10,17).backup_eligible,"new stable scheduling interval can earn an independent takeover");
    f.actors.samples[0].completed_turn=true;f.feed(runtime,18);
    check(runtime.availability(10,18).full_sight_clock==0,"inconsistent completed-turn evidence cannot certify order");
    f.actors.samples[0].completed_turn=false;f.actors.samples[0].position={10,10};f.round(19);
    auto& row=f.producers.samples[0];row.group_id=10;row.producing=true;row.evidence=ProductionEvidence::visible_cleared_spawn;
    runtime.observe_actors(f.actors);runtime.observe_lifecycles({f.actors.clock,&f.info->groups[1],1,false});runtime.observe_producers(f.producers);runtime.observe_snapshot(*f.info);
    check(runtime.availability(10,19).full_sight_clock==0,"fresh owner cleared-spawn service joins actual actor ID and position");
    f.actors.samples[0].position={10,5};f.round(20);f.producers.samples[0].position={10,10};f.producers.samples[0].group_id=10;
    runtime.observe_actors(f.actors);runtime.observe_lifecycles({f.actors.clock,&f.info->groups[1],1,false});runtime.observe_producers(f.producers);runtime.observe_snapshot(*f.info);
    check(runtime.availability(10,20).full_sight_clock==0,"contradictory producer/actor position join becomes unknown");
}
static void duplicates_roster_and_capture(){
    auto fixture=std::make_unique<Fixture>();auto& f=*fixture;Stewards runtime(false);
    for(int r=1;r<=8;++r)f.feed(runtime,r,true,r==1);
    f.round(9);runtime.observe_actors(f.actors);const std::array duplicate{f.info->groups[1],f.info->groups[1]};
    runtime.observe_lifecycles({f.actors.clock,duplicate.data(),2,false});runtime.observe_producers(f.producers);runtime.observe_snapshot(*f.info);
    check(runtime.availability(10,9).full_sight_clock==0,"duplicate lifecycle group does not preserve cherry-picked clock");
    f.actors.samples[1].id=30;f.feed(runtime,10);check(runtime.availability(10,10).full_sight_clock==0,"duplicate visible actor identity invalidates dynamic proof");f.actors.samples[1].id=90;
    f.actors.allied_roster[0]=30;f.feed(runtime,11);check(!runtime.state().supported,"duplicate public roster disables static responsibility instead of guessing slots");f.actors.allied_roster[0]=90;
    f.feed(runtime,12);check(runtime.state().supported&&runtime.availability(10,12).full_sight_clock==1,"fresh valid roster recovers with a reset clock");
    std::swap(f.actors.allied_roster[0],f.actors.allied_roster[1]);std::swap(f.actors.samples[0],f.actors.samples[1]);
    std::swap(f.info->groups[0],f.info->groups[1]);
    f.feed(runtime,13);check(runtime.availability(10,13).primary_id==30&&runtime.availability(10,13).full_sight_clock==2,"roster/sample permutation preserves physical draft order and canonical responsibility");
    std::swap(f.info->groups[0],f.info->groups[1]);
    f.actors.own.position={10,10};f.feed(runtime,14);
    const auto own=runtime.availability(10,14);
    check(own.protected_capture&&own.own_available,"disabled arm protects fresh own cleared-spawn occupancy despite static owner");
    CapturedGroup receipt;receipt.group_id=10;receipt.round=15;receipt.personally_witnessed=true;receipt.actor_attribution_known=true;
    receipt.event_status=Status::derived;receipt.provenance={15,Source::personal_vision,Status::exact_now};
    check(!runtime.capture_receipt(*f.info,receipt),"derived world capture receipt cannot create own capture protection");
    f.actors.own.position={5,10};f.feed(runtime,15);check(!runtime.availability(10,15).protected_capture,"own protection expires after leaving the actual spawn");
    f.actors.own.position={10,10};f.info->groups[1].occupancy_known=false;f.feed(runtime,16);
    check(!runtime.availability(10,16).protected_capture,"unknown opposed occupancy cannot certify own capture hold");
    f.info->groups[1].occupancy_known=true;f.info->groups[1].contested=true;f.feed(runtime,17);
    check(!runtime.availability(10,17).protected_capture,"visible opposition cannot become an uncontested own witness");
}
static void malformed_frames(){
    auto fixture=std::make_unique<Fixture>();auto& f=*fixture;Stewards runtime;
    for(int r=1;r<=8;++r)f.feed(runtime,r,true,r==1);
    f.round(9);runtime.observe_actors(f.actors);runtime.observe_actors(f.actors);
    runtime.observe_lifecycles({f.actors.clock,&f.group(),1,false});runtime.observe_producers(f.producers);runtime.observe_snapshot(*f.info);
    check(runtime.availability(10,9).full_sight_clock==0,"repeated actor frame resets rather than preserving committed clock");
    f.round(10);runtime.observe_actors(f.actors);auto stale=f.group();stale.provenance.observed_round=9;
    runtime.observe_lifecycles({f.actors.clock,&stale,1,false});runtime.observe_producers(f.producers);
    runtime.predict_into(*f.info,Job{},Menu{},*std::make_unique<Forecast>());
    check(runtime.availability(10,10).full_sight_clock==0,"stale per-turn lifecycle cannot reuse current actor receipt");
    f.round(11);runtime.observe_actors(f.actors);runtime.observe_lifecycles({f.actors.clock,&f.group(),1,true});runtime.observe_producers(f.producers);runtime.observe_snapshot(*f.info);
    check(runtime.availability(10,11).full_sight_clock==0,"truncated lifecycle collector does not prove no service");
    f.round(12);runtime.observe_actors(f.actors);runtime.observe_lifecycles({f.actors.clock,&f.group(),1,false});f.producers.truncated=true;
    runtime.observe_producers(f.producers);runtime.observe_snapshot(*f.info);
    check(runtime.availability(10,12).full_sight_clock==0,"truncated producer prefix does not create absence");
    f.round(13);runtime.observe_actors(f.actors);runtime.observe_lifecycles({f.actors.clock,&f.group(),1,false});f.producers.samples[1]=f.producers.samples[0];
    runtime.observe_producers(f.producers);runtime.observe_snapshot(*f.info);
    check(runtime.availability(10,13).full_sight_clock==0,"duplicate producer identities invalidate join proof");
    f.round(14);runtime.observe_actors(f.actors);runtime.observe_lifecycles({f.actors.clock,&f.group(),1,false});f.producers.samples[0].provenance.source=Source::model;
    runtime.observe_producers(f.producers);runtime.observe_snapshot(*f.info);
    check(runtime.availability(10,14).full_sight_clock==0,"predicted producer claim never replaces personal service evidence");
    f.info->public_layout_truncated=true;f.feed(runtime,15);
    check(!runtime.state().supported,"incomplete public group universe cannot assign canonical owners");f.info->public_layout_truncated=false;
    f.feed(runtime,16);check(runtime.availability(10,16).full_sight_clock==1,"complete public group receipt restores a fresh clock");
    f.actors.samples[0].draft_ordinal=f.actors.own.draft_ordinal;f.feed(runtime,17);
    check(runtime.availability(10,17).full_sight_clock==0,"duplicate draft ordinal cannot certify scheduling interval");
}
static void geometry_rejects(){
    auto fixture=std::make_unique<Fixture>();auto& f=*fixture;Stewards runtime;
    f.members[1].ordinal=0;f.feed(runtime,1,true,true);check(!runtime.availability(10,1).backup_eligible,"duplicate public member ordinal is unsupported geometry");
    f.members[1].ordinal=1;for(int r=2;r<=9;++r)f.feed(runtime,r,true,r==2);
    check(runtime.availability(10,9).backup_eligible,"fresh complete public geometry can establish the clock after malformed receipt");
    f.round(10);runtime.observe_actors(f.actors);runtime.observe_lifecycles({f.actors.clock,&f.info->groups[1],1,false});runtime.observe_producers(f.producers);
    f.members[0].spawn.x=9;runtime.observe_layout({f.actors.clock,&f.public_group,1,f.members.data(),2,false});runtime.observe_snapshot(*f.info);
    check(runtime.availability(10,10).full_sight_clock==0,"contradictory immutable spawn receipt cannot preserve old geometry clock");
    Stewards missing_member;f.members[0].spawn.x=10;f.public_group.count=3;f.feed(missing_member,1,true,true);
    check(!missing_member.availability(10,1).backup_eligible,"missing declared public member never treated as absent spawn");
    Stewards partial_layout;f.public_group.count=2;f.public_group.complete=false;
    for(int r=1;r<=9;++r)f.feed(partial_layout,r,true,r==1);
    check(!partial_layout.availability(10,9).backup_eligible,"partial immutable group receipt cannot become a full member mask");
}
static void uncertified_draft_order(){
    auto fixture=std::make_unique<Fixture>();auto& f=*fixture;Stewards runtime;
    f.actors.draft_order_known=false;f.actors.own.draft_ordinal=-1;for(auto& actor:f.actors.samples)actor.draft_ordinal=-1;
    for(int r=1;r<=8;++r)f.feed(runtime,r,true,r==1);
    check(runtime.availability(10,8).backup_eligible,"unchanged priorities and actual completed flags prove stable own-relative order without draft ordinals");
    f.actors.samples[0].next_priority=1;f.feed(runtime,9);
    check(runtime.availability(10,9).full_sight_clock==0,"uncertified changed priority cannot preserve abandonment interval");
    f.actors.samples[0].current_priority=1;for(int r=10;r<=17;++r)f.feed(runtime,r);
    check(runtime.availability(10,17).backup_eligible,"fresh stable priority interval recovers without claiming public vector draft order");
    f.actors.samples[0].completed_turn=false;f.feed(runtime,18);
    check(runtime.availability(10,18).full_sight_clock==0,"completed flag contradicting strictly higher current priority resets unknown-draft proof");
}
int main(){paired_takeover();missing_evidence();scheduling_and_service();duplicates_roster_and_capture();geometry_rejects();malformed_frames();
    uncertified_draft_order();
    std::printf("stewards-runtime checks=%d SDK-observation-and-fuel-gates=pending\n",checks);}
