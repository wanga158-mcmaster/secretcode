// Synthetic pure storage oracle; no SDK, gameplay or performance claim.
#include "../common/src/solo_storage.h"
#include "../common/src/forecast_probe.h"
#include "../reversible-workshop/src/native/menu_storage.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <tuple>
#include <type_traits>
namespace {
using namespace p6;
namespace fc=p6::forecast;
namespace fp=p8::native::forecast_probe;
namespace menu=p8::reversible_workshop::native;
using p8::native::Solo;using p8::native::SoloStorage;using p8::native::reset_solo;
static_assert(std::is_trivially_copyable_v<SoloStorage>);
int checks=0;
void check(bool value,const char* message){++checks;if(!value){std::fprintf(stderr,"FAIL solo storage: %s\n",message);std::exit(1);}}
template<class T> Fact<T> map(T value){return {value,Source::public_map};}
template<class T> Fact<T> setting(T value){return {value,Source::queried_setting};}
template<class T> Observation<T> seen(T value,ViewClock clock,int id){return {value,clock,id,Knowledge::exact};}
template<class T> void fact_equal(const Fact<T>& a,const Fact<T>& b){check(a.value==b.value&&a.source==b.source,"active fact matches fresh oracle");}
template<class T> void unknown(const Observation<T>& value){check(value.subject==-1&&value.knowledge==Knowledge::unknown&&value.clock==ViewClock{},"optional observation reset to unknown");}
void exposure_equal(const Exposure& a,const Exposure& b){
    check(std::tie(a.health,a.max_health,a.incoming_total,a.next_phase_incoming,a.own_timed_shield,a.assigned_timed_heal,a.observations_complete)==
        std::tie(b.health,b.max_health,b.incoming_total,b.next_phase_incoming,b.own_timed_shield,b.assigned_timed_heal,b.observations_complete),"cumulative exposure matches fresh oracle");
}
void certificate_equal(const CycleCertificate& a,const CycleCertificate& b){
    check(a.clock==b.clock&&a.actor==b.actor&&a.group==b.group&&a.route_known==b.route_known&&a.bank_executable==b.bank_executable&&
        a.renewal_reachable==b.renewal_reachable&&a.solo==b.solo,"certificate identity/validity matches fresh oracle");
    exposure_equal(a.clear,b.clear);exposure_equal(a.hold,b.hold);exposure_equal(a.bank,b.bank);exposure_equal(a.recovery,b.recovery);
}
void active_equal(const Solo& a,const Solo& b){
    check(a.feasible==b.feasible&&a.site==b.site&&a.bank==b.bank&&a.ambient_incoming==b.ambient_incoming,"solo active headers match fresh oracle");
    const auto& ap=a.plan;const auto& bp=b.plan;
    check(std::tie(ap.organization,ap.group,ap.collector,ap.altar,ap.crew_count,ap.forecast_rounds,ap.remaining_rounds,ap.capture_window,ap.gem_bearing,ap.complete_verified,ap.rally_legal)==
        std::tie(bp.organization,bp.group,bp.collector,bp.altar,bp.crew_count,bp.forecast_rounds,bp.remaining_rounds,bp.capture_window,bp.gem_bearing,bp.complete_verified,bp.rally_legal),"physical Plan headers match fresh oracle");
    check(ap.crew==bp.crew&&ap.pads==bp.pads,"physical crew/pad headers match fresh oracle");
    for(auto leg:{p8::Leg::ingress,p8::Leg::capture,p8::Leg::bank,p8::Leg::renewal,p8::Leg::release}) {
        const auto& ar=p8::route_for(ap,leg);const auto& br=p8::route_for(bp,leg);
        check(ar.count==br.count&&ar.blocker_assumptions==br.blocker_assumptions&&ar.verified==br.verified,"physical route gates match fresh oracle");
        for(int i=0;i<ar.count;++i)check(ar.tiles[i]==br.tiles[i],"active physical route tile matches fresh oracle");
    }
    const auto& x=a.model;const auto& y=b.model;
    check(x.status==y.status&&x.issue==y.issue&&x.abort==y.abort&&x.phase_count==y.phase_count&&
        x.recovery_health_unmodeled==y.recovery_health_unmodeled&&x.purchases_deferred==y.purchases_deferred,"result gates match fresh oracle");
    check(std::tie(x.first.stage,x.first.actor,x.first.group,x.first.waypoint,x.first.event_id)==
        std::tie(y.first.stage,y.first.actor,y.first.group,y.first.waypoint,y.first.event_id),"first action matches fresh oracle");
    certificate_equal(x.certificate,y.certificate);
    const auto& s=x.schedule;const auto& t=y.schedule;
    check(s.actor_count==t.actor_count&&s.production_count==t.production_count&&s.target_id==t.target_id,"schedule active counts match fresh oracle");
    check(s.immediate_win.actor_id==t.immediate_win.actor_id&&s.immediate_win.tribute_event_id==t.immediate_win.tribute_event_id,"win witness IDs match fresh oracle");
    fact_equal(s.immediate_win.legal_now,t.immediate_win.legal_now);
    fact_equal(s.terminal_tiebreak.earned_gold_delta,t.terminal_tiebreak.earned_gold_delta);fact_equal(s.terminal_tiebreak.kills_delta,t.terminal_tiebreak.kills_delta);
    check(s.terminal_tiebreak.matched_opponent_continuation==t.terminal_tiebreak.matched_opponent_continuation,"terminal match gate reset");
    for(int i=0;i<s.actor_count;++i){const auto& l=s.actors[i];const auto& r=t.actors[i];
        check(l.actor_id==r.actor_id&&l.event_count==r.event_count,"active actor identity/events match fresh oracle");
        fact_equal(l.initial_bag,r.initial_bag);fact_equal(l.initial_gold,r.initial_gold);fact_equal(l.occupied_turns,r.occupied_turns);
        for(int e=0;e<l.event_count;++e){const auto& le=l.events[e];const auto& re=r.events[e];
            check(std::tie(le.kind,le.event_id,le.tick,le.production_index,le.waypoint_id,le.source)==
                std::tie(re.kind,re.event_id,re.tick,re.production_index,re.waypoint_id,re.source),"active event matches fresh oracle");fact_equal(le.gold_cost,re.gold_cost);}
    }
    for(int i=0;i<s.production_count;++i){const auto& l=s.productions[i];const auto& r=t.productions[i];
        check(l.event_id==r.event_id&&l.tick==r.tick&&l.holder_count==r.holder_count&&l.source==r.source,"active payout matches fresh oracle");
        fact_equal(l.gems,r.gems);fact_equal(l.gold,r.gold);for(int h=0;h<l.holder_count;++h)check(l.holders[h]==r.holders[h],"active payout holder matches fresh oracle");
    }
    for(int i=0;i<x.phase_count;++i){const auto& l=x.phases[i];const auto& r=y.phases[i];
        check(std::tie(l.kind,l.actor,l.group,l.waypoint,l.event_id,l.start,l.finish,l.incoming,l.shield_absorbed,l.cumulative_incoming,l.health_after,l.next_phase_incoming,l.source)==
            std::tie(r.kind,r.actor,r.group,r.waypoint,r.event_id,r.start,r.finish,r.incoming,r.shield_absorbed,r.cumulative_incoming,r.health_after,r.next_phase_incoming,r.source),"active phase matches fresh oracle");
    }
}
// Independent old menu interpretation: whole-copy the Solo, then apply the
// resource normalization and certificate stamp that preceded every menu use.
void old_menu_normalize(Solo& model,const Frame& frame){
    auto& actor=model.model.schedule.actors[0];
    if(actor.actor_id!=frame.clock.self){actor.initial_bag.source=Source::visible_subject;actor.initial_gold={0,Source::conservative};}
    model.model.certificate.clock=frame.clock;
}
void compare_menu_borrow(const Solo& own,const Frame& frame,bool expected_borrow){
    auto bytes=std::make_unique<std::array<unsigned char,sizeof(Solo)>>();
    std::memcpy(bytes->data(),&own,sizeof(Solo));
    auto original=std::make_unique<Solo>(own);old_menu_normalize(*original,frame);
    auto copied=std::make_unique<Solo>();
    const bool borrow=menu::can_borrow_menu_own(own,frame);
    check(borrow==expected_borrow,"menu borrow guard reaches the independently expected ownership case");
    std::span<const Solo> selected;
    if(borrow)selected={&own,1};
    else{menu::copy_menu_baseline(*copied,own);old_menu_normalize(*copied,frame);selected={copied.get(),1};}
    check(selected.size()==1&&selected.data()==(expected_borrow?&own:copied.get()),"menu view refers to its still-live own or copied storage");
    active_equal(selected[0],*original);
    check(selected[0].model.certificate.clock==frame.clock,"selected menu certificate has the original normalization clock");
    if(own.model.schedule.actors[0].actor_id!=frame.clock.self){
        const auto& actor=selected[0].model.schedule.actors[0];
        check(actor.initial_bag.value==own.model.schedule.actors[0].initial_bag.value&&actor.initial_bag.source==Source::visible_subject&&
            actor.initial_gold.value==0&&actor.initial_gold.source==Source::conservative,"foreign fallback preserves visible bag and conservative partner gold");
    }
    check(std::memcmp(bytes->data(),&own,sizeof(Solo))==0,"menu guard and selection leave every source byte including inactive payload unchanged");
}
void dirty(SoloStorage& storage){
    auto& s=storage.result;auto& r=storage.request;s.site=7;s.bank=1;s.feasible=true;s.ambient_incoming=91;
    s.plan.organization=p8::Organization::J;s.plan.group=77;s.plan.collector=9;s.plan.altar=8;s.plan.crew.fill(9);s.plan.pads.fill({9,9});s.plan.crew_count=6;
    s.plan.forecast_rounds=80;s.plan.remaining_rounds=90;s.plan.capture_window=9;s.plan.gem_bearing=s.plan.complete_verified=s.plan.rally_legal=true;
    for(auto* route:{&s.plan.ingress,&s.plan.capture,&s.plan.bank,&s.plan.renewal,&s.plan.release}){route->count=128;route->verified=true;route->blocker_assumptions=19;route->tiles[127]={71,72};}
    s.model.status=Status::feasible;s.model.issue=fc::Issue::none;s.model.abort=Failure::unsafe;
    s.model.schedule.actor_count=6;s.model.schedule.production_count=12;s.model.schedule.target_id=77;
    s.model.schedule.immediate_win={9,9,{true,Source::self_observed}};s.model.schedule.terminal_tiebreak={{90,Source::scheduled},{2,Source::scheduled},true};
    s.model.schedule.actors[5].events[6].event_id=321;s.model.schedule.productions[11].gems={654,Source::scheduled};
    s.model.certificate.actor=9;s.model.certificate.group=77;s.model.certificate.route_known=s.model.certificate.bank_executable=s.model.certificate.renewal_reachable=true;
    s.model.certificate.solo=true;s.model.certificate.clear={100,100,1,1,9,3,true};s.model.phase_count=16;s.model.phases[15].event_id=987;
    s.model.first={fc::FirstStage::clear,9,77,9,9};s.model.recovery_health_unmodeled=true;s.model.purchases_deferred=false;
    r.shape=fc::Shape::retain_two_then_bank;r.project_group=77;r.group_count=2;r.enemy_cast_count=6;r.leg_count=8;
    r.timing.settings=setting(Settings{9,9,9});r.timing.tribute_multiplier=setting(9.);r.timing.tribute_base=setting(9);r.timing.tribute_minimum=setting(9);
    r.kit=seen(Productivity{90,9,true,true,true,true},{10,1,0},1);r.ambient_incoming=seen(9.,{10,1,0},1);
    r.bank_waypoint=r.renewal_waypoint=90;r.bank_position=r.renewal_position={9,9};r.bank_available=seen(true,{10,1,0},90);r.bank_lock_hits=setting(9);
    r.shield={true,seen(9.,{10,1,0},1),seen(99,{10,1,0},1)};r.recovery={true,true,9,{9,9},map(9)};r.purchase_requested=true;
    r.groups[0].observed=seen(Group{77,{9,9},{9,9},{9,9},Lifecycle::living,true,true,true},{10,1,0},77);
    r.groups[0].membership_complete=map(true);r.groups[0].member_count=32;r.groups[0].gems=r.groups[0].gold=map<Gems>(999);
    r.groups[0].ambient_overlap=seen(9.,{10,1,0},77);r.groups[0].stationary_basic=seen(true,{10,1,0},77);
    r.groups[0].members[0].current=seen(MonsterWork{100,0,0},{10,1,0},100);r.groups[0].members[31].current=seen(MonsterWork{888,1,1},{10,1,0},888);
    r.groups[1].member_count=19;r.groups[1].observed.value.id=456;r.enemy_cast_stress[5]=seen(333.,{10,1,0},55);
    r.legs[7]={{71,72},{73,74},map(4321)};
}
void reset_checks(const SoloStorage& storage){
    const auto& s=storage.result;const auto& p=s.plan;const auto& r=storage.request;
    check(!s.feasible&&s.site==-1&&s.bank==-1&&s.ambient_incoming==0,"Solo old admission cleared");
    check(p.organization==p8::Organization::U&&p.group==-1&&p.collector==-1&&p.altar==-1&&p.crew_count==0,"Plan role headers reset");
    check(!p.gem_bearing&&!p.complete_verified&&!p.rally_legal&&p.forecast_rounds==0&&p.remaining_rounds==0&&p.capture_window==0,"Plan forecast/permission gates reset");
    for(int i=0;i<6;++i)check(p.crew[i]==-1&&p.pads[i]==Point{},"Plan crew and pads reset");
    for(const auto* route:{&p.ingress,&p.capture,&p.bank,&p.renewal,&p.release}){check(!route->count&&!route->verified&&!route->blocker_assumptions,"route active headers reset");check(route->tiles[127]==Point{71,72},"inactive route tiles retained");}
    check(s.model.status==Status::unresolved&&s.model.issue==fc::Issue::unknown_input&&s.model.abort==Failure::none,"result status reset");
    check(s.model.schedule.actor_count==0&&s.model.schedule.production_count==0&&s.model.schedule.target_id==-1&&s.model.phase_count==0,"no old active model payload");
    check(s.model.first.stage==fc::FirstStage::none&&s.model.first.actor==-1&&s.model.first.group==-1&&s.model.first.event_id==-1&&s.model.first.waypoint==-1,"old action proposal reset");
    check(!s.model.recovery_health_unmodeled&&s.model.purchases_deferred,"result option flags reset");
    certificate_equal(s.model.certificate,CycleCertificate{});
    check(s.model.schedule.immediate_win.actor_id==-1&&s.model.schedule.immediate_win.tribute_event_id==-1&&!s.model.schedule.immediate_win.legal_now.known(),"win proof reset");
    check(!s.model.schedule.terminal_tiebreak.matched_opponent_continuation&&!s.model.schedule.terminal_tiebreak.earned_gold_delta.known()&&!s.model.schedule.terminal_tiebreak.kills_delta.known(),"terminal proof reset");
    check(s.model.schedule.actors[5].events[6].event_id==321&&s.model.schedule.productions[11].gems.value==654&&s.model.phases[15].event_id==987,"inactive schedule and phase arrays retained");
    check(r.shape==fc::Shape::produce_bank&&r.project_group==-1&&r.group_count==0&&r.enemy_cast_count==0&&r.leg_count==0,"request shape/count gates reset");
    check(!r.timing.settings.known()&&!r.timing.tribute_multiplier.known()&&!r.timing.tribute_base.known()&&!r.timing.tribute_minimum.known(),"request timing knowledge reset");
    unknown(r.kit);unknown(r.ambient_incoming);unknown(r.bank_available);
    check(r.bank_waypoint==-1&&r.renewal_waypoint==-1&&r.bank_position==Point{}&&r.renewal_position==Point{}&&!r.bank_lock_hits.known(),"bank/renewal facts reset");
    check(!r.shield.enabled&&!r.recovery.enabled&&!r.recovery.requires_health_restore&&r.recovery.waypoint==-1&&r.recovery.position==Point{}&&!r.recovery.turns.known()&&!r.purchase_requested,"shield/recovery/purchase permissions reset");
    unknown(r.shield.amount);unknown(r.shield.expires_before_tick);unknown(r.groups[0].observed);unknown(r.groups[0].ambient_overlap);unknown(r.groups[0].stationary_basic);
    check(r.groups[0].member_count==0&&r.groups[0].observed.value.id==-1&&!r.groups[0].membership_complete.known()&&!r.groups[0].gems.known()&&!r.groups[0].gold.known(),"active group0 evidence/count headers reset");
    check(r.groups[1].member_count==19&&r.groups[1].observed.value.id==456&&r.groups[0].members[31].current.subject==888&&r.groups[0].members[0].current.knowledge==Knowledge::exact,"inactive groups/member arrays retained for caller refill");
    check(r.enemy_cast_stress[5].value==333&&r.legs[7].distance.value==4321,"inactive stress/leg arrays retained");
}
void route(p8::Route& out,Point from,Point to){out.count=1;out.verified=true;out.tiles[0]=from;while(from!=to){from.x+=(to.x>from.x)-(to.x<from.x);from.y+=(to.y>from.y)-(to.y<from.y);out.tiles[out.count++]=from;}}
Frame configure(SoloStorage& storage,int which){
    Frame frame;frame.clock={10,1,0};auto& own=frame.own.actor;own.id=1;own.pick_ordinal=0;own.team=Team::ally;
    own.alive=own.can_move=own.can_attack=true;own.health=own.max_health=200;own.strength=10;own.attack_range=which==0?3:1;own.ranged=which==0;
    const int group=which==2?29:17;const Point approach{1,0},capture{2,0},bank{3,0},home{0,0};own.position=which==0?approach:home;
    if(which==1)own.can_move=false;
    auto& r=storage.request;r.project_group=group;r.group_count=1;
    r.timing={setting(Settings{200,1000,2}),setting(.1),setting(0),setting(1)};
    r.kit=seen(Productivity{own.strength,0,own.ranged,false,false,false},frame.clock,own.id);r.ambient_incoming=seen(which==0?1.:0.,frame.clock,own.id);
    r.bank_waypoint=90;r.bank_position=bank;r.bank_lock_hits=setting(5);r.renewal_waypoint=group;r.renewal_position=approach;
    auto& g=r.groups[0];g.observed=seen(Group{group,approach,capture,approach,Lifecycle::living,true,true,true},frame.clock,group);
    g.membership_complete=map(true);g.member_count=which==2?2:1;g.gems=map<Gems>(which==2?3:10);g.gold=map<Gems>(which==2?11:7);
    for(int m=0;m<g.member_count;++m){g.members[m].public_full=map(MonsterWork{which==2?200+m:100,which==2?25.:4.,which==2?2.:1.});g.members[m].current={};}
    if(which==0){g.members[0].current=seen(g.members[0].public_full.value,frame.clock,100);g.ambient_overlap=seen(1.,frame.clock,group);g.stationary_basic=seen(true,frame.clock,group);
        r.shield={true,seen(5.,frame.clock,own.id),seen(100,frame.clock,own.id)};r.bank_available=seen(true,frame.clock,90);r.recovery={true,false,99,home,map(3)};}
    auto leg=[&](Point a,Point b){r.legs[r.leg_count++]={a,b,map(std::max(std::abs(a.x-b.x),std::abs(a.y-b.y)))};};
    leg(own.position,approach);leg(approach,capture);leg(capture,bank);if(which==0){leg(bank,home);leg(home,approach);}else leg(bank,approach);
    auto& s=storage.result;s.site=which;s.bank=0;s.ambient_incoming=r.ambient_incoming.value;auto& p=s.plan;
    p.group=group;p.collector=own.id;p.altar=90;p.crew[0]=own.id;p.crew_count=1;p.pads[0]=approach;p.remaining_rounds=120;p.capture_window=2;
    route(p.ingress,own.position,approach);route(p.capture,approach,capture);route(p.bank,capture,bank);
    if(which==0){route(p.renewal,bank,home);p.renewal.tiles[p.renewal.count++]=approach;}else route(p.renewal,bank,approach);
    frame.group=g.observed;return frame;
}
// Same complete five-leg bank -> spawn -> productive-return shape as the
// Earned deferred-admission fixture. Spawn time is charged without HP credit.
Frame configure_earned_spawn(SoloStorage& storage){
    Frame frame=configure(storage,0);frame.clock={10,1,3};auto& own=frame.own.actor;
    own.type=Champion::marcella;own.position={0,0};own.strength=20;own.attack_range=1;own.ranged=false;
    auto& r=storage.request;r.project_group=7;
    r.timing={setting(Settings{2000,1000,5}),setting(.1),setting(0),setting(1)};
    r.kit=seen(Productivity{20,0,false,false,false,false},frame.clock,own.id);
    r.ambient_incoming=seen(1.,frame.clock,own.id);r.shield={};r.bank_available={};
    r.bank_waypoint=4;r.bank_position={4,0};r.renewal_waypoint=7;r.renewal_position={2,0};
    r.recovery={true,false,1000000,{3,1},{1,Source::conservative}};
    auto& g=r.groups[0];g.observed=seen(Group{7,{2,0},{3,0},{2,0},Lifecycle::living,true,true,true},frame.clock,7);
    g.gems=map<Gems>(4);g.gold=map<Gems>(2);g.ambient_overlap={};g.stationary_basic={};
    g.members[0].public_full=map(MonsterWork{70,20,1});g.members[0].current=seen(g.members[0].public_full.value,frame.clock,70);
    r.leg_count=5;r.legs[0]={{0,0},{2,0},map(2)};r.legs[1]={{2,0},{3,0},map(1)};
    r.legs[2]={{3,0},{4,0},map(1)};r.legs[3]={{4,0},{3,1},map(1)};r.legs[4]={{3,1},{2,0},map(1)};
    auto& s=storage.result;s.site=0;s.ambient_incoming=1;auto& p=s.plan;
    p.organization=p8::Organization::U;p.group=7;p.altar=4;p.crew.fill(-1);p.crew[0]=own.id;p.pads.fill({});p.pads[0]={2,0};
    p.capture_window=5;p.rally_legal=false;
    route(p.ingress,{0,0},{2,0});route(p.capture,{2,0},{3,0});route(p.bank,{3,0},{4,0});
    route(p.renewal,{4,0},{3,1});p.renewal.tiles[p.renewal.count++]={2,0};p.release.count=0;p.release.verified=false;
    for(auto* leg:{&p.ingress,&p.capture,&p.bank,&p.renewal,&p.release})leg->blocker_assumptions=0;
    frame.group=g.observed;return frame;
}
void build(SoloStorage& storage,const Frame& frame){
    fc::build_into(frame,storage.request,storage.result.model);auto& s=storage.result;s.feasible=s.model.status==Status::feasible;
    if(s.feasible){s.plan.forecast_rounds=int(s.model.schedule.actors[0].occupied_turns.value);s.plan.gem_bearing=s.plan.complete_verified=true;}
}
struct ProbeTrace {
    unsigned mask=0,calls=0,last=0;bool ordered=true;
    static void mark(void* context,unsigned stage) noexcept {
        auto& trace=*static_cast<ProbeTrace*>(context);
        if(stage>8){trace.ordered=false;return;}
        const unsigned bit=1u<<stage;
        if((trace.mask&bit)||(trace.calls&&stage<=trace.last))trace.ordered=false;
        trace.mask|=bit;++trace.calls;trace.last=stage;
    }
};
void trace_equal(const ProbeTrace& trace,unsigned mask,unsigned calls){
    check(trace.ordered&&trace.mask==mask&&trace.calls==calls&&trace.last==8,"probe emits each reached boundary once in order and exits");
}
void build_probe(SoloStorage& storage,const Frame& frame,fp::BuildProbe probe={}){
    fp::build_into(frame,storage.request,storage.result.model,probe);auto& s=storage.result;s.feasible=s.model.status==Status::feasible;
    if(s.feasible){s.plan.forecast_rounds=int(s.model.schedule.actors[0].occupied_turns.value);s.plan.gem_bearing=s.plan.complete_verified=true;}
}
void failure_gates(const Solo& solo){
    const auto& result=solo.model;
    check(!solo.feasible&&result.schedule.actor_count==0&&result.schedule.production_count==0&&result.schedule.target_id==-1&&result.phase_count==0,"failed probe cannot retain active schedule or phases");
    check(result.first.stage==fc::FirstStage::none&&!result.certificate.route_known&&!result.certificate.bank_executable&&!result.certificate.renewal_reachable,"failed probe cannot retain action or cycle permission");
}
void menu_own_borrow_sequence(){
    auto storage=std::make_unique<SoloStorage>();dirty(*storage);reset_solo(storage->result,storage->request);
    Frame frame=configure(*storage,0);build(*storage,frame);auto& own=storage->result;
    check(own.feasible&&own.model.schedule.actor_count==1,"menu borrow fixture starts with a complete current own forecast");
    check(own.plan.ingress.count<128&&own.model.phase_count<int(own.model.phases.size()),"menu borrow fixture leaves explicit inactive route and phase payload");
    own.plan.ingress.tiles[127]={-777,-888};
    own.model.schedule.actors.back().actor_id=901;
    own.model.schedule.actors.back().events.back().event_id=902;
    own.model.schedule.productions.back().gems={903,Source::unknown};
    own.model.phases.back().event_id=904;
    compare_menu_borrow(own,frame,true);compare_menu_borrow(own,frame,true);

    const auto clock=own.model.certificate.clock;
    ++own.model.certificate.clock.round;compare_menu_borrow(own,frame,false);
    own.model.certificate.clock=clock;++own.model.certificate.clock.revision;compare_menu_borrow(own,frame,false);
    own.model.certificate.clock=clock;++own.model.certificate.clock.self;compare_menu_borrow(own,frame,false);
    own.model.certificate.clock=clock;compare_menu_borrow(own,frame,true);
    ++frame.clock.revision;compare_menu_borrow(own,frame,false);
    frame.clock=clock;compare_menu_borrow(own,frame,true);

    auto& actor=own.model.schedule.actors[0];
    const auto bag=actor.initial_bag,gold=actor.initial_gold;
    actor.actor_id=2;actor.initial_bag={37,Source::self_observed};actor.initial_gold={91,Source::self_observed};
    compare_menu_borrow(own,frame,false);
    check(actor.initial_bag.source==Source::self_observed&&actor.initial_gold.value==91,"foreign source retains its own resources after fallback normalization");
    actor.actor_id=frame.clock.self;actor.initial_bag=bag;actor.initial_gold=gold;compare_menu_borrow(own,frame,true);
    for(int count:{0,2,p6::max_actors}){own.model.schedule.actor_count=count;compare_menu_borrow(own,frame,false);}
    for(int count:{-1,p6::max_actors+1}){own.model.schedule.actor_count=count;check(!menu::can_borrow_menu_own(own,frame),"out-of-range actor counts cannot produce a borrowed menu view");}
    own.model.schedule.actor_count=1;compare_menu_borrow(own,frame,true);

    const auto status=own.model.status;const auto issue=own.model.issue;
    own.feasible=false;own.model.status=Status::unresolved;own.model.issue=fc::Issue::unknown_input;
    compare_menu_borrow(own,frame,true);
    check(!own.feasible&&own.model.status==Status::unresolved,"representation borrowing never restores failed forecast permission");
    own.feasible=true;own.model.status=status;own.model.issue=issue;

    // A borrow is a view of live owner storage, not a retained forecast. Reset
    // invalidates its headers; a later caller must check the guard afresh.
    const Solo* owner_address=&own;const std::span<const Solo> view{owner_address,1};
    reset_solo(own);
    check(&storage->result==owner_address&&view.data()==owner_address&&!view[0].feasible&&view[0].model.schedule.actor_count==0,
        "reset keeps the owner alive while invalidating a previously borrowed result");
    check(!menu::can_borrow_menu_own(own,frame),"fresh guard after owner reset cannot reuse the prior single actor");
    p8::native::reset_request(storage->request);frame=configure(*storage,2);build(*storage,frame);
    check(own.feasible&&view[0].model.schedule.target_id==29,"the same live owner now exposes a freshly built different source");
    compare_menu_borrow(own,frame,true);
}
void probe_equivalence_sequence(){
    auto canonical=std::make_unique<SoloStorage>();auto hooked=std::make_unique<SoloStorage>();auto unhooked=std::make_unique<SoloStorage>();
    for(auto* storage:{canonical.get(),hooked.get(),unhooked.get()})dirty(*storage);
    // Reset only requests: each builder must clear dirty output, then its own
    // prior success/failure, while active records remain fieldwise comparable.
    for(int which=0;which<4;++which){
        for(auto* storage:{canonical.get(),hooked.get(),unhooked.get()})p8::native::reset_request(storage->request);
        const int fixture=which==3?2:which;
        const auto expected_frame=configure(*canonical,fixture);const auto hooked_frame=configure(*hooked,fixture);const auto unhooked_frame=configure(*unhooked,fixture);
        if(which==3)for(auto* storage:{canonical.get(),hooked.get(),unhooked.get()})--storage->request.leg_count;
        build(*canonical,expected_frame);ProbeTrace trace;
        build_probe(*hooked,hooked_frame,{&trace,&ProbeTrace::mark});build_probe(*unhooked,unhooked_frame);
        active_equal(hooked->result,canonical->result);active_equal(unhooked->result,canonical->result);
        if(which==0||which==2){
            check(canonical->result.feasible,"probe oracle fixture is feasible");trace_equal(trace,0x1ffu,9);
            check(hooked->result.model.certificate.valid(hooked_frame,1,fixture==2?29:17),"hooked probe retains current safe cycle");
        }else{
            trace_equal(trace,0x13fu,7);failure_gates(hooked->result);failure_gates(unhooked->result);
            const auto& result=hooked->result.model;
            check(result.issue==(which==1?fc::Issue::unavailable:fc::Issue::unknown_input)&&result.abort==(which==1?Failure::observed_unavailable:Failure::not_observed),"probe preserves initial-travel and partial-schedule failure reasons");
        }
    }
    for(auto* storage:{canonical.get(),hooked.get(),unhooked.get()})p8::native::reset_request(storage->request);
    Frame invalid;invalid.clock={10,1,0};invalid.own.actor.id=1;invalid.own.actor.team=Team::ally;
    build(*canonical,invalid);ProbeTrace early;
    build_probe(*hooked,invalid,{&early,&ProbeTrace::mark});build_probe(*unhooked,invalid);
    active_equal(hooked->result,canonical->result);active_equal(unhooked->result,canonical->result);
    trace_equal(early,0x103u,3);failure_gates(hooked->result);failure_gates(unhooked->result);
    check(hooked->result.model.status==Status::invalid&&hooked->result.model.issue==fc::Issue::malformed,"early-invalid probe matches canonical rejection");
}
template<class Mutate> void scalar_case(SoloStorage& canonical,SoloStorage& hooked,SoloStorage& unhooked,
        int fixture,Mutate mutate,Status status,fc::Issue issue,const char* message){
    const std::array storage{&canonical,&hooked,&unhooked};std::array<Frame,3> frames;
    for(int i=0;i<3;++i){p8::native::reset_request(storage[i]->request);frames[i]=fixture==3?
        configure_earned_spawn(*storage[i]):configure(*storage[i],fixture);mutate(*storage[i],frames[i]);}
    build(canonical,frames[0]);ProbeTrace trace;
    build_probe(hooked,frames[1],{&trace,&ProbeTrace::mark});build_probe(unhooked,frames[2]);
    check(canonical.result.model.status==status&&canonical.result.model.issue==issue,message);
    active_equal(hooked.result,canonical.result);active_equal(unhooked.result,canonical.result);
    const unsigned body=trace.mask&0xffu;
    check(trace.ordered&&trace.last==8&&(trace.mask&0x103u)==0x103u&&(body&(body+1))==0,"scalar probe keeps an ordered boundary prefix and exit");
    if(status==Status::feasible)trace_equal(trace,0x1ffu,9);
    else{failure_gates(hooked.result);failure_gates(unhooked.result);}
}
void scalar_survival_sequence(){
    auto canonical=std::make_unique<SoloStorage>();auto hooked=std::make_unique<SoloStorage>();auto unhooked=std::make_unique<SoloStorage>();
    for(auto* storage:{canonical.get(),hooked.get(),unhooked.get()})dirty(*storage);
    const auto unchanged=[](SoloStorage&,Frame&){};
    auto run=[&](int fixture,auto mutate,Status status,fc::Issue issue,const char* message){
        scalar_case(*canonical,*hooked,*unhooked,fixture,mutate,status,issue,message);
    };
    run(2,unchanged,Status::feasible,fc::Issue::none,"multiple-target threshold oracle starts feasible");
    // Derive the starting-health boundary from a canonical completed exposure.
    // This fixture has no shield and its 20%-of-max floor dominates next stress.
    const double floor_boundary=canonical->result.model.certificate.recovery.incoming_total+.20*200.;
    for(int side:{-1,0,1}){
        const double health=side<0?std::nextafter(floor_boundary,0.):side>0?std::nextafter(floor_boundary,std::numeric_limits<double>::infinity()):floor_boundary;
        run(2,[&](SoloStorage&,Frame& frame){frame.own.actor.health=health;},side<0?Status::unresolved:Status::feasible,
            side<0?fc::Issue::unsafe:fc::Issue::none,"canonical max-health margin changes at exact/adjacent threshold");
    }
    // Reuse the existing fixture as a one-tick bank-only cycle, with zero-length
    // routes: next incoming dominates the health floor without prior exposure.
    const auto bank_only=[](SoloStorage& storage,Frame& frame){
        auto& r=storage.request;auto& own=frame.own.actor;own.bag=1;own.health=6;own.max_health=10;
        r.shape=fc::Shape::bank_only;r.group_count=0;r.shield={};r.recovery={};
        r.ambient_incoming=seen(2.,frame.clock,own.id);r.bank_position=r.renewal_position=own.position;
        r.leg_count=2;for(int i=0;i<2;++i)r.legs[i]={own.position,own.position,map(0)};
    };
    for(int side:{-1,0,1}){
        const double health=side<0?std::nextafter(6.,0.):side>0?std::nextafter(6.,std::numeric_limits<double>::infinity()):6.;
        run(0,[&](SoloStorage& storage,Frame& frame){bank_only(storage,frame);frame.own.actor.health=health;},
            side<0?Status::unresolved:Status::feasible,side<0?fc::Issue::unsafe:fc::Issue::none,"canonical next-incoming margin changes at exact/adjacent threshold");
    }
    for(int expiry:{0,1,2,100}){
        run(0,[&](SoloStorage& storage,Frame&){storage.request.shield.expires_before_tick.value=expiry;},Status::feasible,fc::Issue::none,"shield expiry remains canonical-equivalent");
        check(canonical->result.model.certificate.clear.own_timed_shield==(expiry<2?0.:1.),"shield at expiry boundary protects only strictly earlier damage");
    }
    run(2,[](SoloStorage& storage,Frame& frame){
        frame.own.actor.health=frame.own.actor.max_health=10000;auto& g=storage.request.groups[0];g.member_count=32;
        for(int m=0;m<g.member_count;++m){g.members[m].public_full=map(MonsterWork{200+m,double(1+m%3),.25});g.members[m].current={};}
    },Status::feasible,fc::Issue::none,"all 32 original targets preserve repeated advance and phase proof");
    const double infinity=std::numeric_limits<double>::infinity(),nan=std::numeric_limits<double>::quiet_NaN(),largest=std::numeric_limits<double>::max();
    for(double bad:{nan,infinity,-1.}){
        run(2,[&](SoloStorage& storage,Frame&){storage.request.ambient_incoming.value=bad;},Status::invalid,fc::Issue::malformed,"nonfinite/negative ambient is rejected");
        run(0,[&](SoloStorage& storage,Frame&){storage.request.shield.amount.value=bad;},Status::invalid,fc::Issue::malformed,"nonfinite/negative shield is rejected after group validation");
    }
    for(double bad:{nan,infinity}){
        run(2,[&](SoloStorage&,Frame& frame){frame.own.actor.health=bad;},Status::invalid,fc::Issue::malformed,"nonfinite starting health is rejected");
        run(2,[&](SoloStorage&,Frame& frame){frame.own.actor.max_health=bad;},Status::invalid,fc::Issue::malformed,"nonfinite max health is rejected before caching its margin");
        run(2,[&](SoloStorage& storage,Frame&){storage.request.groups[0].members[0].public_full.value.health=bad;},Status::invalid,fc::Issue::malformed,"nonfinite target health is rejected");
        run(2,[&](SoloStorage& storage,Frame&){storage.request.kit.value.scheduled_single_cast=bad;},Status::invalid,fc::Issue::malformed,"nonfinite scheduled damage is rejected");
    }
    run(2,[&](SoloStorage& storage,Frame& frame){
        Actor enemy;enemy.id=2;enemy.team=Team::enemy;enemy.alive=true;enemy.health=enemy.max_health=100;enemy.strength=largest;
        frame.actors[0]=seen(enemy,frame.clock,enemy.id);frame.actor_count=1;
        storage.request.enemy_cast_count=1;storage.request.enemy_cast_stress[0]=seen(0.,frame.clock,enemy.id);storage.request.ambient_incoming.value=largest;
    },Status::invalid,fc::Issue::overflow,"finite enemy and ambient sum overflow is rejected");
    run(2,[&](SoloStorage& storage,Frame& frame){
        frame.own.actor.health=frame.own.actor.max_health=largest;storage.request.ambient_incoming.value=largest/2.;
    },Status::unresolved,fc::Issue::overflow,"finite travel rate times duration overflow is rejected");
    run(2,[&](SoloStorage& storage,Frame& frame){
        auto& own=frame.own.actor;own.health=own.max_health=largest;auto& r=storage.request;r.ambient_incoming.value=largest/4.;
        r.shield={true,seen(largest,frame.clock,own.id),seen(100,frame.clock,own.id)};
    },Status::unresolved,fc::Issue::overflow,"finite starting health plus absorbed shield overflow is rejected");
    run(0,[&](SoloStorage& storage,Frame& frame){
        bank_only(storage,frame);frame.own.actor.health=frame.own.actor.max_health=largest;storage.request.ambient_incoming.value=largest*.75;
    },Status::unresolved,fc::Issue::unsafe,"finite next incoming with overflowing doubled margin is unsafe");
    run(0,[](SoloStorage& storage,Frame&){storage.request.recovery.requires_health_restore=true;},Status::unresolved,fc::Issue::health_restore_unmodeled,"full-proof late failure clears prior cycle permission");
    run(0,unchanged,Status::feasible,fc::Issue::none,"normal shield/recovery forecast succeeds after numeric and late failures");
}
void earned_spawn_scalar_sequence(){
    auto canonical=std::make_unique<SoloStorage>();auto hooked=std::make_unique<SoloStorage>();auto unhooked=std::make_unique<SoloStorage>();
    for(auto* storage:{canonical.get(),hooked.get(),unhooked.get()})dirty(*storage);
    const auto unchanged=[](SoloStorage&,Frame&){};
    const auto run=[&](auto mutate,Status status,fc::Issue issue,const char* message){
        scalar_case(*canonical,*hooked,*unhooked,3,mutate,status,issue,message);
    };
    run(unchanged,Status::feasible,fc::Issue::none,"Earned five-leg spawn-return scalar body matches canonical forecast");
    const auto& r=canonical->request;const auto& s=canonical->result;
    check(r.leg_count==5&&r.recovery.enabled&&!r.recovery.requires_health_restore&&r.recovery.turns.value==1&&
        r.recovery.turns.source==Source::conservative&&!r.shield.enabled&&!r.purchase_requested&&r.kit.value.scheduled_single_cast==0,
        "Earned oracle retains exactly one conservative spawn wait without healing purchase shield or cast credit");
    check(s.plan.renewal.count==3&&s.plan.renewal.tiles[0]==r.bank_position&&
        s.plan.renewal.tiles[1]==r.recovery.position&&s.plan.renewal.tiles[2]==r.renewal_position&&
        s.plan.forecast_rounds==31&&s.model.certificate.recovery.incoming_total==32&&s.model.recovery_health_unmodeled,
        "joined bank-spawn-return keeps both travel slacks and the spawn exposure in its complete cycle");
    int recovery_phases=0;
    for(int i=0;i<s.model.phase_count;++i)if(s.model.phases[i].kind==fc::PhaseKind::recovery){
        const auto& phase=s.model.phases[i];++recovery_phases;
        check(phase.finish-phase.start==1&&phase.incoming==1&&phase.waypoint==1000000,
            "canonical and scalar phase ledger charges the actual named one-turn spawn wait");
    }
    check(recovery_phases==1,"five-leg return has one recovery phase and a later productive renewal");
    run([](SoloStorage& storage,Frame&){storage.request.recovery.turns.value=2;},Status::feasible,fc::Issue::none,
        "changed spawn duration is recomputed in reused scalar storage");
    check(canonical->result.plan.forecast_rounds==32&&canonical->result.model.certificate.recovery.incoming_total==33,
        "a second spawn turn adds time and cumulative exposure rather than health");
    run([](SoloStorage& storage,Frame& frame){
        frame.own.actor.completed_turn=true;frame.own.actor.ready_attack_group=7;
        storage.request.legs[4].distance=map(2);auto& renewal=storage.result.plan.renewal;
        renewal.tiles[2]={3,0};renewal.tiles[3]={2,0};renewal.count=4;
    },Status::feasible,fc::Issue::none,"current actor metadata and a longer retained return retain exact canonical fields");
    run([](SoloStorage& storage,Frame&){storage.request.recovery.requires_health_restore=true;},Status::unresolved,
        fc::Issue::health_restore_unmodeled,"Earned cannot treat an unmodeled health restore as completed proof");
    run([](SoloStorage& storage,Frame&){storage.request.recovery.turns.source=Source::unknown;},Status::unresolved,
        fc::Issue::unknown_input,"unknown spawn wait cannot inherit a previous successful duration");
    run([](SoloStorage& storage,Frame&){--storage.request.kit.clock.revision;},Status::unresolved,
        fc::Issue::unknown_input,"stale current own kit rejects the five-leg proposal");
    run([](SoloStorage& storage,Frame&){--storage.request.groups[0].observed.clock.round;},Status::unresolved,
        fc::Issue::unknown_input,"stale source observation cannot reuse a prior spawn-return forecast");
    run([](SoloStorage& storage,Frame&){storage.request.leg_count=4;},Status::unresolved,
        fc::Issue::unknown_input,"missing productive return leg clears full cycle permission");
    run([](SoloStorage& storage,Frame&){storage.request.legs[4].from={4,1};},Status::unresolved,
        fc::Issue::malformed,"disconnected joined return is not silently replaced by a direct bank leg");
    run([](SoloStorage&,Frame& frame){frame.own.actor.can_move=false;},Status::unresolved,
        fc::Issue::unavailable,"current own inability to move rejects the initial positive ingress");
    run(unchanged,Status::feasible,fc::Issue::none,"restored five-leg Earned input succeeds after late and stale failures");
    check(canonical->result.plan.forecast_rounds==31&&canonical->result.model.certificate.recovery.incoming_total==32&&
        hooked->result.model.schedule.actors[5].events[6].event_id==321&&
        unhooked->result.model.schedule.productions[11].gems.value==654&&
        hooked->request.legs[7].distance.value==4321,
        "restored active Earned proof ignores retained poisoned inactive actor production and route tails");
}
void reuse_sequence(){
    auto reused=std::make_unique<SoloStorage>();dirty(*reused);reset_solo(reused->result,reused->request);reset_checks(*reused);
    for(int which=0;which<3;++which){
        if(which){reset_solo(reused->result);p8::native::reset_request(reused->request);}
        auto fresh=std::make_unique<SoloStorage>();const auto actual=configure(*reused,which);const auto oracle=configure(*fresh,which);
        if(which==2){unknown(reused->request.groups[0].members[0].current);unknown(reused->request.groups[0].members[1].current);check(!reused->request.recovery.enabled&&!reused->request.shield.enabled,"no spawn/shield flags borrowed by new source");}
        build(*reused,actual);build(*fresh,oracle);active_equal(reused->result,fresh->result);
        if(which==1){check(!reused->result.feasible&&reused->result.model.issue==fc::Issue::unavailable,"failure after success is truly unavailable");check(!reused->result.model.certificate.valid(actual,1,17),"failure cannot borrow old safe cycle");}
        else {check(reused->result.feasible,"fresh-equivalent productive forecast succeeds");check(reused->result.model.certificate.valid(actual,1,which==2?29:17),"productive forecast has current source certificate");}
        if(which==0)check(reused->result.model.recovery_health_unmodeled&&reused->request.recovery.enabled,"first cycle represents spawn-return wait without healing");
        if(which==2)check(!reused->result.model.recovery_health_unmodeled&&reused->result.model.schedule.target_id==29,"different source has no old spawn flags or target");
    }
    reset_solo(reused->result,reused->request);check(!reused->result.feasible&&reused->result.model.schedule.actor_count==0&&!reused->result.model.certificate.route_known,"early candidate failure has no prior permission");
    auto fresh=std::make_unique<SoloStorage>();Frame frame;frame.clock={10,1,0};frame.own.actor.id=1;frame.own.actor.team=Team::ally;
    fc::build_into(frame,reused->request,reused->result.model);fc::build_into(frame,fresh->request,fresh->result.model);
    active_equal(reused->result,fresh->result);
}
}
int main(){reuse_sequence();probe_equivalence_sequence();scalar_survival_sequence();earned_spawn_scalar_sequence();menu_own_borrow_sequence();std::printf("PASS plan8 solo storage %d checks\n",checks);}
