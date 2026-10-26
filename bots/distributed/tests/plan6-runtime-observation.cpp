// Pure regression cases for runtime observation lowering. No SDK or native game.
#include "../common/src/organization.h"
#include "../common/src/runtime_observation.h"
#include "../common/src/runtime_catalogue.h"
#include "../common/src/runtime_opportunities.h"
#include "../reference/src/policy.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using namespace p6;
namespace fc=p6::forecast;
int checks=0;
void check(bool condition,const char* name) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr,"FAIL runtime observation: %s\n",name);
        std::exit(1);
    }
}
bool equal(double a,double b) { return std::abs(a-b)<1e-9; }
template<class T> Fact<T> setting(T value) { return {value,Source::queried_setting}; }
template<class T> Fact<T> public_map(T value) { return {value,Source::public_map}; }
template<class T> Observation<T> seen(T value,ViewClock clock,int subject) {
    return {value,clock,subject,Knowledge::exact};
}
struct Fixture { Frame frame{}; fc::Request request{}; };
std::unique_ptr<Fixture> fixture(bool bank_only=false) {
    auto f=std::make_unique<Fixture>();
    f->frame.clock={7,11,2};
    auto& a=f->frame.own.actor;
    a.id=11; a.pick_ordinal=0; a.team=Team::ally;
    a.alive=a.can_move=a.can_attack=true;
    a.position={0,0}; a.health=a.max_health=100;
    a.strength=1; a.attack_range=1; a.ready_attack_group=17;
    auto& r=f->request;
    r.shape=bank_only?fc::Shape::bank_only:fc::Shape::produce_bank;
    r.project_group=17;
    r.timing.settings=setting(Settings{100,1000,2});
    r.timing.tribute_multiplier=setting(.1);
    r.timing.tribute_base=setting(0); r.timing.tribute_minimum=setting(1);
    r.kit=seen(Productivity{1,0,false,false,false,false},f->frame.clock,a.id);
    r.ambient_incoming=seen(1.,f->frame.clock,a.id);
    r.bank_waypoint=90; r.renewal_waypoint=91;
    r.bank_position={1,0}; r.renewal_position={0,0};
    r.bank_available=seen(true,f->frame.clock,r.bank_waypoint);
    auto leg=[&](Point from,Point to,int distance) {
        r.legs[r.leg_count++]={from,to,public_map(distance)};
    };
    if (!bank_only) {
        r.group_count=1;
        auto& g=r.groups[0];
        g.observed=seen(Group{17,{0,0},{0,0},{0,0},Lifecycle::living,true,true,true},
                        f->frame.clock,17);
        g.membership_complete=public_map(true); g.member_count=1;
        g.members[0].public_full=public_map(MonsterWork{101,2,1});
        g.members[0].current=seen(MonsterWork{101,2,1},f->frame.clock,101);
        g.gems=public_map<Gems>(10); g.gold=public_map<Gems>(7);
        f->frame.group=g.observed;
        leg(a.position,{0,0},0); leg({0,0},{0,0},0);
        leg({0,0},r.bank_position,1); leg(r.bank_position,r.renewal_position,1);
    } else {
        // Already at the altar: its unknown lock is the first planned work.
        a.position=r.bank_position; a.bag=10;
        r.renewal_position=r.bank_position;
        leg(a.position,r.bank_position,0); leg(r.bank_position,r.renewal_position,0);
    }
    return f;
}
std::unique_ptr<fc::Result> build(const Fixture& f) {
    return std::make_unique<fc::Result>(fc::build(f.frame,f.request));
}
int event_tick(const fc::Result& result,EventKind kind) {
    for (int e=0; e<result.schedule.actors[0].event_count; ++e)
        if (result.schedule.actors[0].events[e].kind==kind)
            return result.schedule.actors[0].events[e].tick;
    return -1;
}
void observation_exposure() {
    Actor a; a.health=a.max_health=100;
    const auto lethal=organization::local_exposure(a,90.);
    check(equal(lethal.health,100) && equal(lethal.max_health,100),"observed health preserved");
    check(equal(lethal.incoming_total,90),"current incoming is charged");
    check(equal(lethal.next_phase_incoming,90),"next-phase reserve uses current incoming");
    check(equal(lethal.assigned_timed_heal,0) && equal(lethal.own_timed_shield,0),
          "lowering invents neither healing nor a shield");
    check(!survives(lethal),"100 health against 90 incoming is unsafe");
    check(survives(organization::local_exposure(a,33.)),"67 remaining covers two phases of 33");
    check(!survives(organization::local_exposure(a,34.)),"66 remaining cannot cover two phases of 34");
    a.health=30;
    check(survives(organization::local_exposure(a,10.)),"exact 20-percent and two-phase reserve accepted");
    check(!survives(organization::local_exposure(a,11.)),"reserve below threshold rejected");
    a.health=19;
    check(!survives(organization::local_exposure(a,0.)),"zero damage cannot waive health reserve");
    a.health=100;
    check(!survives(organization::local_exposure(a,-1.)),"negative incoming fails closed");
}
void unknown_bank_forecasts() {
    auto f=fixture();
    const auto unlocked=build(*f);
    check(unlocked->status==Status::feasible,"queried minimal public group has a complete cycle");
    check(unlocked->certificate.valid(f->frame,11,17),"unlocked certificate is current");
    f->request.bank_available={};
    auto unknown=build(*f);
    check(unknown->status==Status::unresolved && unknown->issue==fc::Issue::unknown_input,
          "unseen altar without queried lock bound remains unresolved");
    f->request.bank_lock_hits=public_map(5);
    unknown=build(*f);
    check(unknown->status==Status::unresolved,"copied public lock count is not queried setting");
    f->request.bank_lock_hits=setting(5);
    const auto locked=build(*f);
    check(locked->status==Status::feasible,"unknown altar gets conservative full-lock forecast");
    check(locked->certificate.valid(f->frame,11,17),"lock extension retains current cycle certificate");
    check(locked->schedule.actors[0].occupied_turns.value==
          unlocked->schedule.actors[0].occupied_turns.value+6,
          "five lock hits plus inspection consume six owner turns");
    check(event_tick(*locked,EventKind::production)==event_tick(*unlocked,EventKind::production),
          "future altar lock cannot move the camp payout");
    check(event_tick(*locked,EventKind::tribute)==event_tick(*unlocked,EventKind::tribute)+6 &&
          event_tick(*locked,EventKind::renewal)==event_tick(*unlocked,EventKind::renewal)+6,
          "lock work delays both banking and productive renewal");
    check(equal(locked->certificate.bank.incoming_total,
                unlocked->certificate.bank.incoming_total+6),
          "lock turns charge ambient exposure instead of free waiting");
    int lock_terms=0;
    for (int p=0; p<locked->phase_count; ++p) {
        const auto& phase=locked->phases[p];
        if (phase.kind==fc::PhaseKind::clear && phase.group==-1 && phase.waypoint==90) {
            ++lock_terms;
            check(phase.finish-phase.start==6,"explicit altar-clear phase duration");
        }
    }
    check(lock_terms==1,"altar work is separate from camp clearance");
    // Stale true is not a fresh unlocked observation.
    f->request.bank_available=seen(true,f->frame.clock,90);
    --f->request.bank_available.clock.round;
    const auto stale=build(*f);
    check(stale->status==Status::feasible &&
          event_tick(*stale,EventKind::tribute)==event_tick(*locked,EventKind::tribute),
          "stale unlocked observation still pays the full lock bound");
}
void first_stage_is_not_camp_permission() {
    auto f=fixture(true);
    f->request.bank_available={}; f->request.bank_lock_hits=setting(5);
    auto r=build(*f);
    check(r->status==Status::feasible,"bank-only unknown-lock continuation is feasible");
    check(r->first.stage==fc::FirstStage::clear && r->first.group==-1 &&
          r->first.waypoint==f->request.bank_waypoint && r->first.actor==11,
          "first altar-clear proposal identifies altar rather than a camp group");
    check(r->schedule.production_count==0,"altar clear manufactures no camp payout");
    f->frame.own.actor.can_attack=false;
    r=build(*f);
    check(r->status==Status::unresolved && r->issue==fc::Issue::unknown_input,
          "unavailable basic attack cannot clear a hypothetical lock");
    // Known unlocked bank-only work needs no attack capability or lock bound.
    f->request.bank_available=seen(true,f->frame.clock,90);
    f->request.bank_lock_hits={};
    r=build(*f);
    check(r->status==Status::feasible && r->first.stage==fc::FirstStage::tribute,
          "verified unlocked altar permits tribute without attack capability");
}
struct CampState {
    int life = 0;
    bool is_alive() const { return life != 0; }
    bool is_awake() const { return life == 1; }
};
struct VisibleCamp {
    int id = 0, life = 0;
    Point spawn{}, location{};
    double hp = 0, maximum = 0, power = 0;
    mutable std::array<int,5> reads{};
    Point spawn_position() const { return spawn; }
    CampState state() const { ++reads[0];return {life}; }
    double strength() const { ++reads[1];return power; }
    Point position() const { ++reads[2];return location; }
    double health() const { ++reads[3];return hp; }
    double max_health() const { ++reads[4];return maximum; }
};
void complete_camp_snapshot() {
    using runtime_observation::CampSnapshot;
    const ViewClock clock{17,3,9};
    std::array<VisibleCamp,3> visible{};
    visible[0].id=100;visible[0].life=1;visible[0].spawn={1,0};visible[0].location={2,0};
    visible[0].hp=70;visible[0].maximum=100;visible[0].power=20;
    visible[1].id=101;visible[1].life=2;visible[1].hp=120;visible[1].maximum=120;
    visible[2].id=102;visible[2].life=0;visible[2].power=60;
    CampSnapshot snapshot;int ranges=0;
    const auto identity=[](const VisibleCamp& camp){return camp.id;};
    const auto range=[&](const VisibleCamp&){++ranges;return 3;};
    check(snapshot.collect(visible,clock,identity,range,[]{return true;}),"complete same-turn camp snapshot commits");
    check(snapshot.current(clock) && snapshot.values(clock).size()==3,"all visible lifecycle observations retained");
    check(visible[0].reads==std::array<int,5>{1,1,1,1,1} &&
          visible[1].reads==std::array<int,5>{1,1,1,1,1},"each living camp property read once");
    check(visible[2].reads==std::array<int,5>{1,1,0,0,0},"dead camp needs no invented live position or health query");
    const auto* awake=snapshot.find(clock,100);const auto* asleep=snapshot.find(clock,101);
    check(awake && awake->awake && awake->alive && awake->spawn==Point{1,0} &&
          awake->position==Point{2,0} && equal(awake->health,70) && equal(awake->max_health,100) &&
          equal(awake->strength,20) && awake->attack_range==3,"numeric camp fields retain their exact personal observation");
    check(asleep && asleep->alive && !asleep->awake && equal(asleep->health,120),"sleeping camp remains living without incoming attack credit");
    check(snapshot.find(clock,102) && !snapshot.find(clock,102)->alive &&
          equal(snapshot.find(clock,102)->health,0),"dead camp is explicitly dead, not an unseen member");
    check(snapshot.find(clock,999)==nullptr,"unseen member stays unknown without an SDK probe");
    const auto before=visible[0].reads;
    visible[0].hp=1;
    for(int i=0;i<10;++i) {
        check(equal(snapshot.find(clock,100)->health,70),"site/damage/request reuse reads the same numeric snapshot");
        check(snapshot.values(clock).size()==3,"numeric snapshot can be reused without dropping members");
    }
    check(visible[0].reads==before && ranges==3,"reuse performs no additional live or design query");
    auto changed=clock;++changed.round;
    check(snapshot.values(changed).empty() && !snapshot.find(changed,100),"next round cannot reuse a stale camp snapshot");
    changed=clock;++changed.revision;
    check(!snapshot.current(changed),"a different observation revision is not restamped current");
    changed=clock;++changed.self;
    check(!snapshot.current(changed),"another actor cannot claim a private snapshot clock");
    snapshot.invalidate();
    check(!snapshot.current(clock) && !snapshot.find(clock,100),"action invalidation prevents post-action cached reads");
    check(snapshot.collect(visible,clock,identity,range,[]{return true;}) &&
          equal(snapshot.find(clock,100)->health,1),"fresh collection observes changed camp health");
    int calls=0;
    check(!snapshot.collect(visible,clock,identity,range,[&]{return calls++==0;}),"budget cutoff stops a partial collection");
    check(!snapshot.current(clock) && snapshot.values(clock).empty(),"partial collection never publishes a usable snapshot");
    calls=0;
    check(!snapshot.collect(visible,clock,identity,range,[&]{return calls++<3;}),"final same-turn guard can reject completed query work");
    check(!snapshot.current(clock) && !snapshot.find(clock,100),"failed final guard cannot leak earlier current values");
}
void injected_camp_numeric_reader() {
    using runtime_observation::Camp;
    using runtime_observation::CampSnapshot;
    ViewClock clock{30,3,4};
    std::array<VisibleCamp,3> sources{};
    for(int i=0;i<3;++i) {
        sources[i].id=200+i;sources[i].life=i==0?1:i==1?2:0;
        sources[i].spawn={10+i,11};sources[i].location={99,99};
        sources[i].hp=sources[i].maximum=sources[i].power=999;
    }
    CampSnapshot snapshot;std::array<int,3> calls{};int ranges=0;
    double first_health=30,first_strength=4;Point first_position{3,5};
    const auto identity=[](const VisibleCamp& source){return source.id;};
    const auto range=[&](const VisibleCamp&){++ranges;return 3;};
    // Independent numeric producer, not an SDK record/layout imitation. The
    // source's legacy numeric getters intentionally contain different facts.
    const auto reader=[&](const VisibleCamp& source,Camp& value) {
        ++calls[source.id-200];
        value.strength=source.id==200?first_strength:source.id==201?6:99;
        if(value.alive) {
            value.position=source.id==200?first_position:Point{4,5};
            value.health=source.id==200?first_health:20;
            value.max_health=source.id==200?80:70;
        }
    };
    const auto collect=[&] {return snapshot.collect(sources,clock,identity,range,[]{return true;},reader);};
    const auto work=[&] {
        std::array<MonsterWork,3> members{};int count=0;
        for(const auto& camp:snapshot.values(clock))
            members[count++]={camp.id,camp.health,camp.strength};
        const std::array kit{Productivity{10,0,false,false,false,false}};
        return combat_work(std::span<const MonsterWork>{members.data(),static_cast<std::size_t>(count)},kit);
    };
    check(collect() && snapshot.current(clock) && snapshot.values(clock).size()==3,
          "injected numeric reader publishes only the complete current member set");
    check(calls==std::array<int,3>{1,1,1} && ranges==3,
          "injected producer and immutable range lookup run once per member including dead members");
    check(sources[0].reads==std::array<int,5>{1,0,0,0,0} &&
          sources[1].reads==std::array<int,5>{1,0,0,0,0} &&
          sources[2].reads==std::array<int,5>{1,0,0,0,0},
          "collector retains public lifecycle reads without duplicate legacy numeric queries");
    const auto* alive=snapshot.find(clock,200);const auto* asleep=snapshot.find(clock,201);
    const auto* dead=snapshot.find(clock,202);
    check(alive && alive->alive && alive->awake && alive->spawn==Point{10,11} &&
          alive->position==first_position && equal(alive->health,30) && equal(alive->max_health,80) &&
          equal(alive->strength,4) && alive->attack_range==3,
          "public identity spawn lifecycle and range combine with fresh injected living facts");
    check(asleep && asleep->alive && !asleep->awake && asleep->id==201 &&
          equal(asleep->health,20) && equal(asleep->strength,6),
          "sleeping living member retains exact numeric work without being classified dead");
    check(dead && !dead->alive && !dead->awake && dead->spawn==Point{12,11} &&
          equal(dead->strength,99) && dead->position==Point{} && equal(dead->health,0) &&
          equal(dead->max_health,0),
          "dead member retains strength and public spawn but no invented live HP maximum or position");
    const auto original=work();
    check(original.resolved && original.rounds==7 && equal(original.incoming,40),
          "actual basic-work consumer uses injected living HP and strength while excluding dead99 strength");
    const auto prior_calls=calls;
    check(snapshot.find(clock,999)==nullptr && snapshot.values(clock).size()==3 && calls==prior_calls,
          "numeric reuse and unknown-member lookup perform no extra injected query");
    auto stale=clock;++stale.round;
    check(!snapshot.current(stale) && snapshot.values(stale).empty() && !snapshot.find(stale,200),
          "later round cannot reuse injected current facts");
    stale=clock;++stale.revision;
    check(!snapshot.current(stale) && !snapshot.find(stale,200),
          "another action revision cannot restamp injected data");
    stale=clock;++stale.self;
    check(!snapshot.current(stale),"another actor cannot borrow the injected personal snapshot");

    const auto old_clock=clock;++clock.revision;
    first_health=5;first_strength=2;first_position={7,8};
    check(collect() && !snapshot.current(old_clock) && snapshot.current(clock) &&
          equal(snapshot.find(clock,200)->health,5) && equal(snapshot.find(clock,200)->strength,2) &&
          snapshot.find(clock,200)->position==first_position,
          "new collection receives changed numeric facts rather than reusing an old bulk record");
    const auto fresh=work();
    check(fresh.resolved && fresh.rounds==5 && equal(fresh.incoming,32),
          "changed current numbers alter actual clear work without changing public membership");

    calls={};int budget_calls=0;
    check(!snapshot.collect(sources,clock,identity,range,[&]{return budget_calls++==0;},reader) &&
          calls==std::array<int,3>{1,0,0},
          "budget cutoff prevents later reader calls after one member");
    check(!snapshot.current(clock) && snapshot.values(clock).empty() && !snapshot.find(clock,200),
          "budget-aborted injected collection exposes neither partial work nor the previous complete snapshot");
    calls={};int owner_round=clock.round;
    const auto interrupted=[&](const VisibleCamp& source,Camp& value) {
        reader(source,value);if(source.id==201)++owner_round;
    };
    check(!snapshot.collect(sources,clock,identity,range,[&]{return owner_round==clock.round;},interrupted) &&
          calls==std::array<int,3>{1,1,0} && !snapshot.current(clock) && snapshot.values(clock).empty(),
          "owner-round interruption cannot publish an old-clock prefix or query the remaining member");
    calls={};budget_calls=0;
    check(!snapshot.collect(sources,clock,identity,range,[&]{return budget_calls++<3;},reader) &&
          calls==std::array<int,3>{1,1,1} && !snapshot.current(clock) && !snapshot.find(clock,202),
          "final guard rejects publication even after every injected numeric record was read");

    check(collect(),"fresh complete collection recovers after budget and clock interruptions");
    calls={};bool threw=false;
    try {
        snapshot.collect(sources,clock,identity,range,[]{return true;},
            [&](const VisibleCamp& source,Camp& value) {
                reader(source,value);if(source.id==201)throw std::runtime_error("injected numeric query failed");
            });
    } catch(const std::runtime_error&) {threw=true;}
    check(threw && calls==std::array<int,3>{1,1,0},
          "reader error propagates to the turn boundary and stops remaining live queries");
    check(!snapshot.current(clock) && snapshot.values(clock).empty() && !snapshot.find(clock,200),
          "reader exception cannot leave prior or partially refreshed facts published");
    ++clock.round;++clock.revision;
    check(collect() && snapshot.current(clock) && work().resolved,
          "later fresh collection can atomically publish after a reader error");
    snapshot.invalidate();
    check(!snapshot.current(clock) && snapshot.values(clock).empty(),
          "successful action invalidation still applies to injected numeric observations");
}
void whole_basic_current_members() {
    using runtime_observation::CampSnapshot;
    const ViewClock clock{50,11,3};
    auto read_clock=clock;
    std::array<VisibleCamp,3> members{};
    for(int i=0;i<3;++i) {
        members[i].id=300+i;members[i].life=i==2?0:1;
        members[i].spawn={11,10};members[i].location={10+i,10};
        members[i].hp=members[i].maximum=100;
    }
    // The dead member deliberately has no usable current position. Its public
    // membership must still be observed, but it must not enter the basic mask.
    members[2].spawn={99,99};members[2].power=99;
    CampSnapshot snapshot;
    const auto identity=[](const VisibleCamp& camp){return camp.id;};
    const auto collect=[&](auto sources) {
        return snapshot.collect(sources,clock,identity,[](const auto&){return 3;},[]{return true;});
    };
    int mask_reads=0;
    const Point origin{10,10};
    const auto covered=[&] {
        mask_reads=0;
        return runtime_observation::whole_basic_members(members,
            [&](int id){return snapshot.find(read_clock,id);},
            [&](Point p) {
                ++mask_reads;
                return std::max(std::abs(p.x-origin.x),std::abs(p.y-origin.y))<=1;
            });
    };
    check(!covered() && mask_reads==0,
          "whole basic contact needs a published current snapshot, not immutable member positions");
    check(collect(std::span<const VisibleCamp>{members.data(),2}) && !covered(),
          "an unobserved third member cannot be assumed dead even when every observed living member is covered");
    check(collect(std::span<const VisibleCamp>{members}) && covered() && mask_reads==2,
          "complete fresh whole-member contact ignores the observed dead member's absent live position");
    ++read_clock.round;
    check(!covered() && mask_reads==0,"next round cannot reuse whole-member contact");
    read_clock=clock;++read_clock.revision;
    check(!covered(),"another action revision cannot reuse whole-member contact");
    read_clock=clock;++read_clock.self;
    check(!covered(),"another observer cannot borrow whole-member contact");
    read_clock=clock;
    members[1].location={12,10};
    check(collect(std::span<const VisibleCamp>{members}) && !covered(),
          "covered public spawn cannot replace a living member's current out-of-mask position");
    members[1].life=0;
    check(collect(std::span<const VisibleCamp>{members}) && covered() && mask_reads==1,
          "fresh death removes only that member from current attack coverage");
    members[0].life=0;
    check(collect(std::span<const VisibleCamp>{members}) && !covered() && mask_reads==0,
          "a completely observed all-dead source is capture geometry, never basic production contact");
    members[0].life=1;
    check(collect(std::span<const VisibleCamp>{members}) && covered(),
          "fresh living state restores contact without carrying an old dead classification");
    snapshot.invalidate();
    check(!covered(),"action invalidation removes whole-member contact until fresh collection");
    check(!runtime_observation::whole_basic_members(std::span<const VisibleCamp>{},
        [&](int id){return snapshot.find(clock,id);},[](Point){return true;}),
          "empty immutable membership cannot certify basic contact");
}
void verification_pad_geometry() {
    // A public three-member column has exactly two whole-member melee pads in
    // this corridor. The third pad is usable only at the queried range of3.
    const std::array<Point,3> members{{{8,8},{8,9},{8,10}}};
    const std::array<Point,3> legal_pads{{{7,9},{9,9},{8,7}}};
    const auto legal=[&](Point p) {
        return std::find(legal_pads.begin(),legal_pads.end(),p)!=legal_pads.end();
    };
    const auto distance=[](Point a,Point b) {
        return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));
    };
    const std::array<int,3> ranges{1,3,1};
    auto pads=runtime_catalogue::verification_pads<Point>(members,ranges,{0,0},legal);
    check(pads.available,"distinct queried-role pads can stage at one public whole source");
    for(int role=0;role<3;++role) {
        bool whole=legal(pads.pads[role]);
        for(const auto member:members)whole &= distance(pads.pads[role],member)<=ranges[role];
        check(whole,"each chosen public pad is legal and covers every original member at that role's range");
        for(int prior=0;prior<role;++prior)
            check(pads.pads[role]!=pads.pads[prior],"public verification pads cannot stack roles on one tile");
    }
    check(pads.pads[1]==Point{8,7} && distance(pads.pads[1],members.back())==3,
          "ranged role uses its exact whole-source boundary after the two melee pads are reserved");
    const std::array<int,3> reordered{3,1,1};
    pads=runtime_catalogue::verification_pads<Point>(members,reordered,{0,0},legal);
    check(pads.available && pads.pads[0]==Point{8,7},
          "queried range, not ordinal, reserves scarce melee pads before a ranged role");
    check(!runtime_catalogue::verification_pads<Point>(members,{1,2,1},{0,0},legal).available,
          "range2 cannot borrow the distant member coverage supplied by range3");
    check(!runtime_catalogue::verification_pads<Point>(members,ranges,{0,0},
        [&](Point p){return legal(p) && p!=Point{8,7};}).available,
          "a blocked ranged landing cannot be replaced by overlapping a melee pad");
    const std::array<Point,4> square{{{8,8},{9,8},{8,9},{9,9}}};
    check(!runtime_catalogue::verification_pads<Point>(square,ranges,{0,0},[&](Point p) {
        return p.x>=0 && p.y>=0 && p.x<20 && p.y<20 &&
            std::find(square.begin(),square.end(),p)==square.end();
    }).available,"a full2x2 source has no noncamp whole-member melee pad despite adjacent free tiles");
    check(!runtime_catalogue::verification_pads<Point>({},ranges,{0,0},legal).available,
          "missing public source membership cannot manufacture a staging appointment");
}
void ready_group_precedence() {
    struct Mask { int range;bool is_set(Point p) const {return p.x>=0 && p.x<=range;} };
    std::vector<runtime_observation::AttackTarget> targets{{5,55,{2,0}},{1,11,{1,0}},
        {7,77,{9,0}},{3,33,{0,0}}};
    const auto position=[](Point value){return value;};
    check(runtime_observation::ready_group(targets,Mask{2},position)==55,
          "flat target pass preserves last eligible site, not last target order");
    std::reverse(targets.begin(),targets.end());
    check(runtime_observation::ready_group(targets,Mask{2},position)==55,
          "visible target enumeration does not change ready-group precedence");
    check(runtime_observation::ready_group(targets,Mask{-1},position)==-1,
          "no legal target preserves unknown ready group");
}
void occupancy_and_dead_members() {
    std::array<VisibleCamp,4> members{};
    for(int i=0;i<4;++i)members[i].id=100+i;
    members[0].life=1;members[1].life=0;members[2].life=1;members[3].life=0;
    std::vector<VisibleCamp> sources{members[0]};
    std::array<int,4> visibility_queries{};
    const auto identity=[](const VisibleCamp& camp){return camp.id;};
    const auto spawn_visible=[](const VisibleCamp& camp){return camp.id!=102;};
    const auto available=[&](const VisibleCamp& camp){++visibility_queries[camp.id-100];return camp.id==101;};
    check(runtime_observation::append_visible_members(sources,members,spawn_visible,identity,available,
        []{return true;}),"visible missing-member merge completes");
    check(sources.size()==2 && sources[1].id==101,"visible dead member joins the complete observation source list");
    check(visibility_queries==std::array<int,4>{0,1,0,1},
          "no repeated listed-member query or unseen-spawn visibility query");
    runtime_observation::CampSnapshot snapshot;
    const ViewClock clock{20,1,0};
    check(snapshot.collect(sources,clock,identity,[](const auto&){return 3;},[]{return true;}),
          "merged live and dead members produce one complete numeric snapshot");
    check(snapshot.find(clock,101) && !snapshot.find(clock,101)->alive,
          "absent occupancy plus actual visibility retains exact dead state");
    check(!snapshot.find(clock,102) && !snapshot.find(clock,103),
          "unseen current or last position remains unknown rather than resurrected or dead");
    sources.resize(1);
    check(!runtime_observation::append_visible_members(sources,members,spawn_visible,identity,available,
        []{return false;}) && sources.size()==1,"budget cutoff prevents a missing-member live query");
}
std::array<Group,8> catalogue() {
    std::array<Group,8> groups{};
    for(int i=0;i<8;++i) {
        // Deliberately poison dynamic fields: only immutable geometry may be copied.
        groups[i]={100+i,{i,1},{i,2},{i,3},Lifecycle::respawning,true,true,true};
    }
    return groups;
}
void bounded_site_publication() {
    using namespace runtime_observation;
    auto in=std::make_unique<organization::Input>();auto f=fixture();in->frame=f->frame;
    const auto groups=catalogue();const auto own=in->frame.own;
    in->enemy_stress_count=1;in->enemy_stress[0]=seen(12.,in->frame.clock,77);in->safe_now=false;
    std::vector<int> visits;
    auto read=[&](int index,SiteReading& value) {
        visits.push_back(index);
        value={in->frame.clock,groups[index].id,Lifecycle::living,true,true,true,index==5};
        return true;
    };
    auto pass=publish_sites(*in,groups,105,102,[&]{return visits.size()<2;},read);
    check(pass.ready() && pass.required_known && pass.cutoff && pass.visited==2,
          "optional cutoff retains a fully published owned source");
    check(visits==std::vector<int>{5,2},"owned then latest requested source precede catalogue work");
    check(pass.visited_sites==std::array<bool,8>{false,false,true,false,false,true,false,false},
          "publication evidence retains original indices and excludes budget tails");
    check(in->site_count==8 && in->site(105)==&in->sites[5] && in->site(102)==&in->sites[2],
          "partial site pass preserves real organization lookup and original policy indices");
    check(in->frame.group.current(in->frame.clock,105) && in->sites[5].damaged_now,
          "owned current lifecycle and numeric damage are published together");
    check(in->frame.own==own && in->enemy_stress_count==1 &&
          in->enemy_stress[0].current(in->frame.clock,77) && equal(in->enemy_stress[0].value,12) && !in->safe_now,
          "optional cutoff preserves completed self and full enemy danger including unsafe result");
    for(int i=0;i<8;++i) {
        check(in->sites[i].group.id==groups[i].id && in->sites[i].group.approach==groups[i].approach &&
              in->sites[i].group.spawn==groups[i].spawn && in->sites[i].group.retreat==groups[i].retreat,
              "all declared static movement geometry survives optional cutoff");
        if(i!=5 && i!=2)check(!in->sites[i].known && in->sites[i].group.lifecycle==Lifecycle::unknown &&
            !in->sites[i].group.original_spawn_visible && !in->sites[i].group.all_spawns_visible &&
            !in->sites[i].group.uncontested_now && !in->sites[i].damaged_now,
            "unvisited tail is unknown, never a copied lifecycle or fresh visibility witness");
    }
    ++in->frame.clock.round;++in->frame.clock.revision;visits.clear();
    in->sites[5].blocked=true;in->sites[5].actor_count=1;in->sites[5].joint_better=true;
    pass=publish_sites(*in,groups,-1,103,[&]{return visits.size()<1;},read);
    check(pass.ready() && pass.cutoff && visits==std::vector<int>{3},
          "unowned requested source is first without inventing a required project");
    check(!in->sites[5].known && in->sites[5].group.lifecycle==Lifecycle::unknown &&
          !in->sites[5].blocked && in->sites[5].actor_count==0 && !in->sites[5].joint_better &&
          in->frame.group.knowledge==Knowledge::unknown,
          "next pass clears old site facts/ranks and never restamps previous owned observation");
    visits.clear();pass=publish_sites(*in,groups,105,105,[&]{return visits.size()<3;},read);
    check(visits==std::vector<int>{5,0,1},"duplicate owned/requested group is visited once then stable catalogue order");
    visits.clear();pass=publish_sites(*in,groups,-1,-1,[]{return true;},read);
    check(pass.ready() && !pass.cutoff && pass.visited==8 && visits==std::vector<int>{0,1,2,3,4,5,6,7},
          "full-budget unowned pass retains existing catalogue order");
    visits.clear();pass=publish_sites(*in,groups,105,102,[]{return false;},read);
    check(!pass.ready() && pass.valid && pass.cutoff && visits.empty() && !in->site(105)->known &&
          in->frame.group.knowledge==Knowledge::unknown,"required budget cutoff cannot publish or forge a current group");
    pass=publish_sites(*in,groups,-1,102,[]{return false;},read);
    check(pass.ready() && pass.cutoff && pass.visited==0 && in->site_count==8,
          "no optional fuel still leaves complete unowned action frame with static destinations");
    auto stale=[&](int index,SiteReading& value) {
        read(index,value);--value.clock.revision;return true;
    };
    pass=publish_sites(*in,groups,105,-1,[]{return true;},stale);
    check(!pass.ready() && pass.visited==0 && !in->site(105)->known,
          "stale reading cannot be restamped as a current lifecycle");
    check(pass.visited_sites==std::array<bool,8>{},"stale reader supplies no completed inspection evidence");
    pass=publish_sites(*in,groups,105,-1,[]{return true;},[&](int index,SiteReading& value) {
        read(index,value);value.group=104;return true;
    });
    check(!pass.ready() && pass.visited==0 && in->frame.group.knowledge==Knowledge::unknown,
          "wrong-source reading cannot become owned evidence");
    pass=publish_sites(*in,groups,105,-1,[]{return true;},[&](int index,SiteReading& value) {
        read(index,value);return false;
    });
    check(!pass.ready() && pass.visited==0 && !in->site(105)->known,
          "interrupted reader publishes none of its partially collected current facts");
    check(!pass.visited_sites[5],"interrupted owned reader cannot count as a completed inspection");
    pass=publish_sites(*in,groups,105,-1,[]{return true;},[&](int index,SiteReading& value) {
        read(index,value);value.original_spawn_visible=false;return true;
    });
    check(pass.ready() && !pass.required_known && !in->site(105)->known &&
          in->site(105)->group.lifecycle==Lifecycle::unknown && !in->site(105)->group.all_spawns_visible &&
          !in->site(105)->group.uncontested_now && in->frame.group.knowledge==Knowledge::unknown,
          "completed unseen travel source retains geometry but cannot claim fresh lifecycle or contest");
    check(pass.visited_sites[5],"completed unknown source remains distinguishable from an unvisited tail");
    pass=publish_sites(*in,groups,999,-1,[]{return true;},read);
    check(!pass.valid && !pass.ready(),"missing required catalogue source fails explicitly");
    auto duplicate=groups;duplicate[7].id=duplicate[0].id;
    pass=publish_sites(*in,duplicate,-1,-1,[]{return true;},read);
    check(!pass.valid && in->site_count==0,"duplicate group identities cannot alias policy indices");
    std::array<Group,9> excess{};
    pass=publish_sites(*in,excess,-1,-1,[]{return true;},read);
    check(!pass.valid && in->site_count==0,"capacity overflow cannot publish a truncated catalogue");
}
void publication_inspection_history() {
    using namespace runtime_observation;
    auto in=std::make_unique<organization::Input>();auto f=fixture();in->frame=f->frame;
    const auto groups=catalogue();runtime_opportunities::History history;
    const auto empty=history;
    const auto incomplete=[&](int index,SiteReading& value) {
        value={in->frame.clock,groups[index].id,Lifecycle::unknown,false,false,false,false};return true;
    };
    const auto one_site=[&](int requested) {
        int reads=0;
        return publish_sites(*in,groups,-1,requested,[&]{return reads<1;},
            [&](int index,SiteReading& value){++reads;return incomplete(index,value);});
    };
    const auto inspect=[&](const SitePass& pass) {
        const auto& site=in->sites[5];
        return history.inspect(5,105,in->frame.clock.round,true,
            site.known && site.group.all_spawns_visible,!pass.visited_sites[5]);
    };
    for(int round=100;round<103;++round) {
        in->frame.clock.round=round;++in->frame.clock.revision;
        const auto pass=one_site(102);
        check(pass.visited_sites[2] && !pass.visited_sites[5] && !in->sites[5].known &&
              !inspect(pass) && history==empty && history.available(5,105,round),
              "real history treats an unvisited exact-pad budget tail as a pure deferred no-op");
    }
    in->frame.clock.round=103;++in->frame.clock.revision;
    auto pass=one_site(105);
    check(pass.visited_sites[5] && !in->sites[5].known && !inspect(pass) && history.available(5,105,103),
          "first actual incomplete current inspection counts without cooling the site");
    const auto once=history;
    check(!inspect(pass) && history==once,"same-round published inspection cannot count twice");
    in->frame.clock.round=104;++in->frame.clock.revision;pass=one_site(102);
    check(!pass.visited_sites[5] && !inspect(pass) && history==once,
          "intervening publication cutoff preserves rather than increments or resets prior inspection");
    in->frame.clock.round=105;++in->frame.clock.revision;pass=one_site(105);
    check(pass.visited_sites[5] && inspect(pass) && !history.available(5,105,105) &&
          !history.available(5,105,112) && history.available(5,105,113),
          "only the second fresh completed incomplete inspection starts the ordinary eight-round cooldown");
}
void damage_history_observations() {
    using runtime_observation::DamageHistory;
    DamageHistory history;
    check(history.records().empty(),"damage history exposes no unobserved capacity slots");
    constexpr int source=987654321;
    history.observe(source,10,100);
    check(history.records().size()==1 && history.records()[0].id==source &&
          history.records()[0].seen_round==10 && history.records()[0].damage_round==-2 &&
          equal(history.records()[0].health,100),
          "first arbitrary source establishes a baseline without inventing damage");
    history.observe(source,10,100);
    check(history.records().size()==1 && history.records()[0].damage_round==-2,
          "repeated unchanged observation neither duplicates a source nor witnesses damage");
    history.observe(source,10,90);
    check(history.records()[0].damage_round==10 && equal(history.records()[0].health,90),
          "a lower personally observed HP in the same round witnesses current damage");
    history.observe(source,11,95);
    check(history.records()[0].seen_round==11 && history.records()[0].damage_round==10 &&
          equal(history.records()[0].health,95),
          "higher HP updates the baseline while preserving only the earlier damage timestamp");
    history.observe(source,12,85);
    check(history.records()[0].seen_round==12 && history.records()[0].damage_round==12 &&
          equal(history.records()[0].health,85),
          "a consecutive-round HP reduction refreshes the damage witness");
    history.observe(source,15,80);
    check(history.records()[0].seen_round==15 && history.records()[0].damage_round==12 &&
          equal(history.records()[0].health,80),
          "an observation gap permits a fresh baseline but cannot attribute intervening damage");
    history.observe(2027,20,300);
    check(history.records().size()==2 && history.records()[0].id==source &&
          history.records()[0].seen_round==15 && history.records()[0].damage_round<20-1 &&
          history.records()[1].id==2027 && history.records()[1].damage_round==-2,
          "another noncontiguous source does not refresh stale damage for the current-round witness");

    DamageHistory full;
    constexpr int first=100003,step=17;
    for(int i=0;i<256;++i)full.observe(first+step*i,31,1000+i);
    check(full.records().size()==256 && full.records().front().id==first &&
          full.records().back().id==first+step*255 && full.records().back().damage_round==-2,
          "all capacity slots accept distinct noncontiguous identities through real observations");
    full.observe(first+step*255,32,1000);
    check(full.records().size()==256 && full.records().back().seen_round==32 &&
          full.records().back().damage_round==32 && equal(full.records().back().health,1000),
          "a full history still finds and updates an existing source at its last occupied slot");
    full.observe(7,32,5);
    bool found_new=false;
    for(const auto& record:full.records())found_new|=record.id==7;
    check(full.records().size()==256 && !found_new && full.records().front().id==first &&
          full.records().back().damage_round==32 && equal(full.records().back().health,1000),
          "a new source at capacity is ignored without evicting or overwriting existing evidence");
    full.observe(first+step*128,34,900);
    check(full.records()[128].id==first+step*128 && full.records()[128].seen_round==34 &&
          full.records()[128].damage_round==-2 && equal(full.records()[128].health,900),
          "full-capacity lookup preserves the observation-gap rule for a middle source");
}
void optional_cutoff_owned_receipts() {
    using namespace runtime_observation;
    auto in=std::make_unique<organization::Input>();auto f=fixture();in->frame=f->frame;
    const auto groups=catalogue();ActorState state;
    check(state.admit(105,6,8,90,0,0),"receipt fixture owns its actual source");
    state.phase=Phase::hold;state.previous_observed=true;
    in->frame.own.actor.spawn_group=105;in->frame.own.gold=7;
    const auto read=[&](int index,SiteReading& value) {
        value={in->frame.clock,groups[index].id,Lifecycle::respawning,true,true,true,false};return true;
    };
    auto pass=publish_sites(*in,groups,105,102,[]{return false;},read);
    // The adapter must stop before reference_step/Remember when the required
    // site was not visited. In particular, fuel deferral cannot consume payout
    // baselines just because the new gold count was already observed.
    check(!pass.ready() && state.previous_gold==0 && !state.payout_seen && state.phase==Phase::hold,
          "required deferral retains the prior resource baseline and pending HOLD receipt");
    ++in->frame.clock.round;++in->frame.clock.revision;
    int reads=0;
    pass=publish_sites(*in,groups,105,102,[&]{return reads<1;},[&](int index,SiteReading& value) {
        ++reads;return read(index,value);
    });
    check(pass.ready() && pass.required_known && pass.cutoff && in->frame.valid(),
          "next fresh required-first pass retains payout proof despite optional cutoff");
    const auto* group=in->site(state.project.group);
    const bool payout=group && group->known && group->group.lifecycle==Lifecycle::respawning &&
        in->frame.own.actor.spawn_group==group->group.id && state.previous_observed &&
        (in->frame.own.actor.bag>state.previous_bag || in->frame.own.gold>state.previous_gold);
    ReferenceInput actual{.frame=in->frame};actual.safe_now=true;
    actual.completion={in->frame.clock,state.project.group,payout,false,true};
    // Exercise the real reference caller, without a quote or invented score.
    const auto step=reference::choose_forecast(state,actual,fc::Request{});
    check(step && step->action==Action::move && state.active && state.payout_seen && state.empty_liquidation &&
          state.phase==Phase::travel && state.previous_gold==7,
          "real caller ingests gold-only payout and continues physical renewal after cutoff");
    check(state.repairs==0 && !state.bank_seen && !state.tribute_started,
          "optional observation work manufactures no retirement/repair/bank/channel receipt");
    // A completed but unseen HOLD must also stop before advancing its resource
    // baseline; travel can use that same unknown geometry on a later fresh view.
    ActorState waiting;check(waiting.admit(105,6,8,90,0,0),"unseen receipt fixture owns source");
    waiting.phase=Phase::hold;waiting.previous_observed=true;
    pass=publish_sites(*in,groups,105,-1,[]{return true;},[&](int index,SiteReading& value) {
        read(index,value);value.original_spawn_visible=false;return true;
    });
    check(pass.ready() && !pass.required_known && waiting.previous_gold==0 &&
          waiting.phase==Phase::hold && !waiting.payout_seen,
          "unknown current HOLD is distinguishable from a completed lifecycle and retains pending receipt baseline");
}
}
int main() {
    observation_exposure(); unknown_bank_forecasts(); first_stage_is_not_camp_permission();
    complete_camp_snapshot();injected_camp_numeric_reader();whole_basic_current_members();verification_pad_geometry();
    ready_group_precedence();occupancy_and_dead_members();
    bounded_site_publication();publication_inspection_history();optional_cutoff_owned_receipts();
    damage_history_observations();
    std::printf("Plan 6 runtime observation: %d checks passed\n",checks);
}
