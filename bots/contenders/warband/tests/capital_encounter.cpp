#include "capital_encounter.h"
#include <array>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <type_traits>
using namespace wb::capital::encounter;
static_assert(std::is_same_v<decltype(ObservedBody{}.body),const Body*>);
static_assert(sizeof(Observation)<=sizeof(Body));
static_assert(sizeof(BodySlot)==sizeof(Body)&&std::is_trivially_copyable_v<BodySlot>);
static_assert(!std::is_same_v<decltype(World{}.bodies)::value_type,Body>);
static unsigned checks{},failures{};
static void check(bool value,const char* label){++checks;if(!value){++failures;if(failures<12)std::printf("FAIL %s\n",label);}}
static Body body(int id,int team,Point p={4,4}) {
    Body b;b.id=id;b.team=team;b.position=p;b.hp=b.max_hp=1000;b.basic=40;b.move_range=b.attack_range=1;
    b.alive=b.m=b.a=b.c=b.provenance_known=b.effects_closed=true;return b;
}
static World world(unsigned own=1,unsigned foe=1) {
    World w;w.count=own+foe;w.round=10;w.owner=0;w.revision=7;w.respawn_ticks=60;w.spawn_healing_known=true;
    for(unsigned i=0;i<own;++i)w.bodies[i]=body(static_cast<int>(i),1,{4,static_cast<int>(4+2*i)});
    for(unsigned i=0;i<foe;++i)w.bodies[own+i]=body(static_cast<int>(own+i),0,{5,static_cast<int>(4+2*i)});
    return w;
}
static Primitive move(Point p){return {PrimitiveKind::Move,-1,-1,p};}
static Primitive basic(int target){return {PrimitiveKind::Basic,target,-1,{}};}
static Primitive cast(int slot,int target=-1,Point p={}){return {PrimitiveKind::Cast,target,slot,p};}
static Word word(std::initializer_list<Primitive> steps={}) {
    Word out;out.complete=true;for(const auto& p:steps)if(out.count<word_limit)out.steps[out.count++]=p;return out;
}
static Ability reset(bool enhanced=false) {
    Ability a;a.kind=Kind::Reset;a.max_cooldown=8;a.target_mask=self_target;a.enhanced_reset=enhanced;return a;
}
static Ability damage(float amount,bool exclusive=false) {
    Ability a;a.kind=Kind::Damage;a.amount=amount;a.range=4;a.max_cooldown=8;a.target_mask=enemy_target;a.excludes_walk=exclusive;return a;
}
struct Timeline {
    std::array<Slot,96> storage{};Scenario scenario{};unsigned count{};
    Timeline(const World& w,int horizon,int starter=1) {
        scenario.reply_id=11;scenario.revision=2;scenario.assumptions=13;scenario.horizon_round=horizon;
        scenario.starting_team=starter;scenario.current_remaining_complete=scenario.future_rounds_complete=true;
        for(int round=w.round;round<=horizon;++round) {
            std::array<int,body_limit> first{},second{};unsigned f=0,s=0;
            const int current_starter=starter^((round-w.round)&1);
            for(unsigned i=0;i<w.count;++i) {
                if(w.bodies[i].team==current_starter)first[f++]=w.bodies[i].id;else second[s++]=w.bodies[i].id;
            }
            std::array<int,body_limit> order{};unsigned n=0;
            for(unsigned i=0;i<std::max(f,s);++i){if(i<f)order[n++]=first[i];if(i<s)order[n++]=second[i];}
            bool after=round!=w.round;
            for(unsigned i=0;i<n;++i) {
                if(round==w.round&&order[i]==w.owner){scenario.current_slot=static_cast<int>(i);after=true;continue;}
                if(after)storage[count++]={round,order[i]};
            }
        }
        scenario.schedule={storage.data(),count};
    }
    Timeline(const Timeline&)=delete;Timeline& operator=(const Timeline&)=delete;
};
enum class Mode {Hold,Attack,ResetStrike,Chain,Recovery,SeeHidden};
struct Context {
    Mode mode{Mode::Hold};int owner{},target{1},hidden{-1},hide_from_round{-1};Point spawn{0,9};
    bool unknown_geometry{},blocked_geometry{},unknown_visibility{},unknown_phase{},unknown_spawn{},unknown_readiness{},require_spawn{},leaked{},saw_hidden{},saw_visible{};
    float phase_damage{};int phase_target{};unsigned policy_calls{};
};
static const Body* find(const World& w,int id){const int index=detail::at(w,id);return index<0?nullptr:&w.bodies[static_cast<unsigned>(index)];}
static Verdict geometry(void* ptr,const World& w,int actor,const Primitive& p) {
    const auto& c=*static_cast<Context*>(ptr);if(c.unknown_geometry)return Verdict::Unknown;if(c.blocked_geometry)return Verdict::Blocked;
    if(p.kind==PrimitiveKind::Move||(p.kind==PrimitiveKind::Cast&&p.slot>=0&&find(w,actor)->abilities[static_cast<unsigned>(p.slot)].kind==Kind::Dash))
        for(unsigned i=0;i<w.count;++i)if(w.bodies[i].modeled&&w.bodies[i].alive&&w.bodies[i].id!=actor&&w.bodies[i].position==p.destination)return Verdict::Blocked;
    return Verdict::Allowed;
}
static Verdict visible(void* ptr,const World& w,int,int target) {
    const auto& c=*static_cast<Context*>(ptr);if(c.unknown_visibility)return Verdict::Unknown;
    return target==c.hidden&&(c.hide_from_round<0||w.round>=c.hide_from_round)?Verdict::Blocked:Verdict::Allowed;
}
static Phase phase(void* ptr,const World&,int) {
    const auto& c=*static_cast<Context*>(ptr);Phase p;p.complete=!c.unknown_phase;
    if(c.phase_damage>0){p.count=1;p.effects[0]={PhaseKind::Damage,-1,c.phase_target,c.phase_damage,0};}return p;
}
static Verdict spawn(void* ptr,const World& w,int actor) {
    const auto& c=*static_cast<Context*>(ptr);if(c.unknown_spawn)return Verdict::Unknown;
    const auto* b=find(w,actor);return actor==w.owner&&b&&b->position==c.spawn?Verdict::Allowed:Verdict::Blocked;
}
static Verdict ready(void* ptr,const Observation& o) {
    const auto& c=*static_cast<Context*>(ptr);const auto* b=o.find(o.observer);if(!b)return Verdict::Unknown;
    if(c.unknown_readiness)return Verdict::Unknown;
    if(!c.require_spawn)return Verdict::Allowed;
    return b->position==c.spawn&&b->hp==b->max_hp&&b->cooldown[0]==0&&b->cooldown[1]==0?Verdict::Allowed:Verdict::Blocked;
}
static Word policy(void* ptr,const Observation& o) {
    auto& c=*static_cast<Context*>(ptr);++c.policy_calls;const auto* me=o.find(o.observer);if(!me)return {};
    if(c.mode==Mode::SeeHidden) {
        for(unsigned i=0;i<o.count;++i)if(o.bodies[i].id==c.hidden&&o.observer!=c.hidden) {
            const auto& b=o.bodies[i];
            if(b.visible){c.saw_visible=true;c.leaked|=!b.body||b.body->id!=b.id;}
            else {c.saw_hidden=true;c.leaked|=b.body!=nullptr;}
        }
        return word();
    }
    if(c.mode==Mode::Recovery&&o.observer==c.owner) {
        if(me->position==c.spawn||!me->m)return word();
        const int dx=(c.spawn.x>me->position.x)-(c.spawn.x<me->position.x),dy=(c.spawn.y>me->position.y)-(c.spawn.y<me->position.y);
        return word({move({me->position.x+dx,me->position.y+dy})});
    }
    if(c.mode==Mode::Hold||c.mode==Mode::Recovery)return word();
    const auto* target=o.find(c.mode==Mode::Chain?c.owner:c.target);if(!target||!target->alive)return word();
    if(c.mode!=Mode::Chain&&me->id==target->id)return word();
    if(c.mode!=Mode::Chain&&me->team==target->team)return word();
    const int d=distance(me->position,target->position);Word out=word();
    auto push=[&](Primitive p){if(out.count<word_limit)out.steps[out.count++]=p;};
    if(c.mode==Mode::Attack){if(me->a&&d<=me->attack_range)push(basic(target->id));return out;}
    if(c.mode==Mode::ResetStrike) {
        if(d<=me->attack_range){if(me->a)push(basic(target->id));if(me->c&&me->cooldown[0]==0){push(cast(0));push(basic(target->id));}}
        return out;
    }
    if(c.mode==Mode::Chain) {
        if(me->id==c.owner) {
            if(c.require_spawn&&me->m&&me->position!=c.spawn) {
                const int dx=(c.spawn.x>me->position.x)-(c.spawn.x<me->position.x),dy=(c.spawn.y>me->position.y)-(c.spawn.y<me->position.y);
                push(move({me->position.x+dx,me->position.y+dy}));
            }return out;
        }
        if(me->id==3)return out;
        if(me->id==2&&o.round==50)return out;
        if(me->a&&d<=me->attack_range)push(basic(target->id));
        if(me->id==1) {
            if(me->c&&me->cooldown[0]==0&&d<=me->abilities[0].range)push(cast(0,-1,target->position));
            else if(me->c&&me->cooldown[1]==0&&d<=me->attack_range){push(cast(1));push(basic(target->id));}
        } else if(me->id==2) {
            if(me->c&&me->cooldown[0]==0&&d<=me->attack_range){push(cast(0));push(basic(target->id));}
            else if(me->c&&me->cooldown[1]==0&&d<=me->abilities[1].range)push(cast(1,target->id));
        }
        return out;
    }
    return out;
}
static Callbacks callbacks(Context& c){return {&c,geometry,visible,policy,phase,spawn};}
static Requirement requirement(const World& w,int deadline,Context* c=nullptr) {
    Requirement r;r.owner=w.owner;r.deadline_round=deadline;
    if(c){r.readiness=ready;r.context=c;}return r;
}
static const Body& terminal(const Outcome& o,int id){return *find(o.terminal,id);}

namespace recall_fixture {
namespace r=wb::capital::encounter::recall;
static_assert(static_cast<unsigned>(r::PrimitiveKind::Move)==0&&static_cast<unsigned>(r::PrimitiveKind::Basic)==1&&
    static_cast<unsigned>(r::PrimitiveKind::Cast)==2&&static_cast<unsigned>(r::PrimitiveKind::Recall)==3);
static_assert(!std::is_same_v<r::Body,Body>&&!std::is_same_v<r::World,World>);
static r::Body body(int id,int team,r::Point position={4,4}) {
    r::Body b;b.id=id;b.team=team;b.position=position;b.hp=b.max_hp=1000;b.basic=40;b.move_range=b.attack_range=1;
    b.alive=b.m=b.a=b.c=b.provenance_known=b.effects_closed=true;return b;
}
static r::World world(unsigned own=1,unsigned foe=0) {
    r::World w;w.count=own+foe;w.round=50;w.owner=0;w.revision=7;w.respawn_ticks=60;w.spawn_healing_known=true;
    for(unsigned i=0;i<own;++i)w.bodies[i]=body(static_cast<int>(i),1,{4,static_cast<int>(4+2*i)});
    for(unsigned i=0;i<foe;++i)w.bodies[own+i]=body(static_cast<int>(own+i),0,{5,static_cast<int>(4+2*i)});
    w.bodies[0].recall.queried_start_ticks=5;return w;
}
static r::Word word(std::initializer_list<r::Primitive> steps={}) {
    r::Word out;out.complete=true;for(const auto& p:steps)if(out.count<r::word_limit)out.steps[out.count++]=p;return out;
}
static r::Primitive recall(){return {r::PrimitiveKind::Recall,-1,-1,{}};}
static r::Primitive move(r::Point p){return {r::PrimitiveKind::Move,-1,-1,p};}
static r::Primitive basic(int target){return {r::PrimitiveKind::Basic,target,-1,{}};}
struct Timeline {
    std::array<r::Slot,96> storage{};r::Scenario scenario{};unsigned count{};
    Timeline(const r::World& w,int horizon,int starter=1) {
        scenario.reply_id=11;scenario.revision=2;scenario.assumptions=13;scenario.horizon_round=horizon;
        scenario.starting_team=starter;scenario.current_remaining_complete=scenario.future_rounds_complete=true;
        for(int round=w.round;round<=horizon;++round) {
            std::array<int,r::body_limit> first{},second{},order{};unsigned f=0,s=0,n=0;
            const int side=starter^((round-w.round)&1);
            for(unsigned i=0;i<w.count;++i){if(w.bodies[i].team==side)first[f++]=w.bodies[i].id;else second[s++]=w.bodies[i].id;}
            for(unsigned i=0;i<std::max(f,s);++i){if(i<f)order[n++]=first[i];if(i<s)order[n++]=second[i];}
            bool after=round!=w.round;
            for(unsigned i=0;i<n;++i){if(round==w.round&&order[i]==w.owner){scenario.current_slot=static_cast<int>(i);after=true;continue;}
                if(after)storage[count++]={round,order[i]};}
        }
        scenario.schedule={storage.data(),count};
    }
};
struct Context {
    r::Point spawn{0,9},old{4,4};r::Verdict start{r::Verdict::Allowed},landing{r::Verdict::Allowed};
    bool own_spawn{true},attack{},zero_shield_seen{},arrival_usable_seen{},phase_after_arrival{};
    unsigned starts{},arrivals{},old_camp_phases{},spawn_camp_phases{},owner_policies{},enemy_attempts{};
    int first_arrival{-1},blocked_until{-1},damage_round{-1},stun_round{-1};float damage{},spawn_damage{};
};
static const r::Body& find(const r::World& w,int id){return w.bodies[static_cast<unsigned>(r::detail::at(w,id))];}
static r::Verdict geometry(void*,const r::World&,int,const r::Primitive&){return r::Verdict::Allowed;}
static r::Verdict visible(void*,const r::World&,int,int){return r::Verdict::Allowed;}
static r::Verdict spawn(void* ptr,const r::World& w,int actor) {
    const auto& c=*static_cast<Context*>(ptr);return c.own_spawn&&actor==w.owner&&find(w,actor).position==c.spawn?r::Verdict::Allowed:r::Verdict::Blocked;
}
static r::Verdict start(void* ptr,const r::World&,int){auto& c=*static_cast<Context*>(ptr);++c.starts;return c.start;}
static r::Arrival arrival(void* ptr,const r::World& w,int actor) {
    auto& c=*static_cast<Context*>(ptr);++c.arrivals;if(c.first_arrival<0)c.first_arrival=w.round;
    c.zero_shield_seen=!find(w,actor).shield_present;
    c.arrival_usable_seen=r::detail::usable(find(w,actor));
    return {w.round<=c.blocked_until?r::Verdict::Blocked:c.landing,c.spawn};
}
static r::Phase phase(void* ptr,const r::World& w,int round) {
    auto& c=*static_cast<Context*>(ptr);r::Phase p;p.complete=true;const auto& owner=find(w,w.owner);
    if(owner.position==c.old)++c.old_camp_phases;
    if(owner.position==c.spawn){++c.spawn_camp_phases;c.phase_after_arrival=true;}
    if(round==c.damage_round){p.effects[p.count++]={r::PhaseKind::Damage,-1,w.owner,c.damage,0};}
    if(round==c.stun_round){p.effects[p.count++]={r::PhaseKind::Stun,-1,w.owner,0,1};}
    if(owner.position==c.spawn&&c.spawn_damage>0)p.effects[p.count++]={r::PhaseKind::Damage,-1,w.owner,c.spawn_damage,0};
    return p;
}
static r::Word policy(void* ptr,const r::Observation& o) {
    auto& c=*static_cast<Context*>(ptr);const auto* me=o.find(o.observer);if(!me)return {};
    if(o.observer==0){++c.owner_policies;return word();}
    const auto* target=o.find(0);if(c.attack&&me->team==0&&target&&target->alive&&me->a&&r::distance(me->position,target->position)<=me->attack_range){++c.enemy_attempts;return word({basic(0)});}
    return word();
}
static r::Callbacks callbacks(Context& c){return {&c,geometry,visible,policy,phase,spawn,start,arrival};}
static r::Requirement requirement(const r::World& w,int deadline) {r::Requirement q;q.owner=w.owner;q.deadline_round=deadline;return q;}
static const r::Body& terminal(const r::Outcome& o,int id=0){return find(o.terminal,id);}
static void tests() {
    {
        auto w=world();Context c;Timeline t(w,55);auto result=r::run(w,word({recall()}),t.scenario,callbacks(c),requirement(w,55));
        check(result.status==r::Status::Complete&&result.recall_starts==1&&result.recall_arrivals==1,"Recall5 completes as a physical arrival");
        check(c.first_arrival==55&&c.old_camp_phases==5&&c.owner_policies==1,"Recall END50..54 then camps55 old site before START55 arrival");
        check(result.next_usable_round==55&&result.service_ready_round==55&&terminal(result).position==c.spawn,"arrival precedes usable and readiness");
        check(!c.arrival_usable_seen&&!terminal(result).channel_present&&terminal(result).recall.kind==r::RecallKind::None,"arrival callback does not see a free old-site usable input");
        check(result.slots==5&&result.phases==5&&result.primitives==1,"Recall uses existing schedule limits, not flat substitute wait");
        Timeline short_time(w,54);Context short_c;const auto pending=r::run(w,word({recall()}),short_time.scenario,callbacks(short_c),requirement(w,54));
        check(pending.status==r::Status::Complete&&terminal(pending).recall.remaining==0&&terminal(pending).channel_present,"zero at END54 stays pending");
        check(terminal(pending).position==short_c.old&&pending.service==r::Service::Refuted&&pending.next_usable_round<0,"no early free landing or renewal");
        w.bodies[0].recall.queried_start_ticks=3;Timeline home(w,53);Context home_c;
        const auto faster=r::run(w,word({recall()}),home.scenario,callbacks(home_c),requirement(w,53));
        check(faster.status==r::Status::Complete&&home_c.first_arrival==53&&home_c.old_camp_phases==3,"queried Home3 arrives START53");
        w=world();w.bodies[0].recall.kind=r::RecallKind::Recall;w.bodies[0].recall.remaining=2;w.bodies[0].channel_present=true;
        Timeline retained_time(w,52);Context retained_c;auto retained_cb=callbacks(retained_c);retained_cb.recall_start=nullptr;
        const auto retained=r::run(w,word(),retained_time.scenario,retained_cb,requirement(w,52));
        check(retained.status==r::Status::Complete&&retained.recall_starts==0&&retained_c.first_arrival==52,"captured native channel continues remaining debt without another start");
        w.bodies[0].recall.remaining=0;Timeline zero_time(w,50);
        const auto zero=r::run(w,word(),zero_time.scenario,retained_cb,requirement(w,50));
        check(zero.status==r::Status::Unsupported&&zero.reason==r::Reason::UnknownRecallArrival,"captured postSTART zero Recall is not silently given another wait");
    }
    {
        auto w=world();Context c;Timeline t(w,55);auto cb=callbacks(c);cb.recall_start=nullptr;
        auto result=r::run(w,word({recall()}),t.scenario,cb,requirement(w,55));
        check(result.status==r::Status::Unsupported&&result.reason==r::Reason::UnknownRecallStart&&result.recall_starts==0,"Recall needs explicit start-legality premise");
        c.start=r::Verdict::Blocked;result=r::run(w,word({recall()}),t.scenario,callbacks(c),requirement(w,55));
        check(result.status==r::Status::Invalid&&result.reason==r::Reason::IllegalRecallStart,"declared illegal Recall is not a wait");
        c.start=r::Verdict::Allowed;w.bodies[0].recall.queried_start_ticks=-1;result=r::run(w,word({recall()}),t.scenario,callbacks(c),requirement(w,55));
        check(result.status==r::Status::Unsupported&&result.reason==r::Reason::UnknownRecallDuration,"duration is queried, not copied5");
        w.bodies[0].recall.queried_start_ticks=5;w.bodies[0].m=false;w.bodies[0].c=false;w.bodies[0].cast_used=true;
        Timeline immediate(w,50);result=r::run(w,word({recall()}),immediate.scenario,callbacks(c),requirement(w,50));
        check(result.status==r::Status::Complete&&result.recall_starts==1&&!terminal(result).m&&terminal(result).cast_used,"explicit conditional Recall does not invent unusedM or usedC legality");
        w.bodies[0].m=true;result=r::run(w,word({move({3,4}),recall()}),immediate.scenario,callbacks(c),requirement(w,50));
        check(result.status==r::Status::Complete&&terminal(result).position==r::Point{3,4}&&terminal(result).walked,"Move Recall remains a declared conditional word");
        result=r::run(w,word({recall(),move({3,4})}),immediate.scenario,callbacks(c),requirement(w,50));
        check(result.status==r::Status::Invalid&&result.reason==r::Reason::Resource&&result.primitives==1,"no physical suffix after Recall starts");
        w.bodies[0].recall.started_this_input=true;result=r::run(w,word({recall()}),immediate.scenario,callbacks(c),requirement(w,50));
        check(result.status==r::Status::Invalid&&result.reason==r::Reason::IllegalRecallStart,"one Recall start per input history retained");
    }
    {
        auto w=world();w.bodies[0].hp=500;w.bodies[0].cooldown={8,7};w.spawn_healing_fraction=0.1f;
        w.bodies[0].shield_present=true;w.bodies[0].shield=50;w.bodies[0].shield_turns=5;
        w.bodies[0].enhanced_present=true;w.bodies[0].enhanced_turns=6;w.bodies[0].enhanced_basic_stun=1;
        Context c;c.spawn_damage=50;Timeline t(w,56);auto req=requirement(w,56);req.minimum_hp=600;
        const auto result=r::run(w,word({recall()}),t.scenario,callbacks(c),req);
        check(result.status==r::Status::Complete&&c.zero_shield_seen&&c.phase_after_arrival,"zero shield expires before arrival and future spawn phase still runs");
        check(terminal(result).hp==650&&terminal(result).cooldown[0]==1&&terminal(result).cooldown[1]==0,"arrival END heals, later camp damage and cooldown debt both apply");
        check(!terminal(result).enhanced_present&&result.service==r::Service::Refuted,"enhancement expires normally; END heal is not nextSTART readiness");
        check(c.old_camp_phases==5&&c.spawn_camp_phases==1,"camps51 through55 old site, camps56 at new spawn");
    }
    {
        auto w=world();Context c;c.damage_round=52;c.damage=20;Timeline t(w,55);
        auto result=r::run(w,word({recall()}),t.scenario,callbacks(c),requirement(w,55));
        check(result.status==r::Status::Complete&&result.recall_interruptions==1&&result.recall_arrivals==0&&terminal(result).hp==980,"ordered camp HP damage cancels Recall");
        check(result.next_usable_round==52&&terminal(result).position==c.old,"interruption leaves actual old-site renewal, not a free spawn");
        c={};c.stun_round=52;result=r::run(w,word({recall()}),t.scenario,callbacks(c),requirement(w,55));
        check(result.status==r::Status::Complete&&result.recall_interruptions==1&&result.next_usable_round==53,"stun cancels and retains victim ownerEND duration");
        w.bodies[0].stun_immune=true;c={};c.stun_round=52;result=r::run(w,word({recall()}),t.scenario,callbacks(c),requirement(w,55));
        check(result.status==r::Status::Complete&&result.recall_interruptions==0&&result.recall_arrivals==1,"immune stun does not cancel Recall");
        w.bodies[0].stun_immune=false;c={};c.damage_round=52;c.damage=2000;
        result=r::run(w,word({recall()}),t.scenario,callbacks(c),requirement(w,55));
        check(result.status==r::Status::Partial&&result.service==r::Service::Refuted&&!terminal(result).alive&&!terminal(result).channel_present,"death cancels Recall and retains owner death refutation");
        w=world();w.bodies[0].shield_present=true;w.bodies[0].shield=100;w.bodies[0].shield_turns=9;c={};c.damage_round=51;c.damage=10;
        Timeline shield_time(w,55);result=r::run(w,word({recall()}),shield_time.scenario,callbacks(c),requirement(w,55));
        check(result.status==r::Status::Unsupported&&result.reason==r::Reason::UnknownRecallInterruption&&result.service==r::Service::Unknown,"shield-absorbed positive hit is unresolved");
        auto interrupted=shield_time.scenario;interrupted.shielded_recall_hit=r::Scenario::ShieldedRecallHit::AssumeInterrupt;c={};c.damage_round=51;c.damage=10;
        const auto cancel=r::run(w,word({recall()}),interrupted,callbacks(c),requirement(w,55));
        auto preserved=shield_time.scenario;preserved.shielded_recall_hit=r::Scenario::ShieldedRecallHit::AssumeContinue;c={};c.damage_round=51;c.damage=10;
        const auto keep=r::run(w,word({recall()}),preserved,callbacks(c),requirement(w,55));
        check(cancel.status==r::Status::Complete&&keep.status==r::Status::Complete&&cancel.recall_shield_assumption_used&&keep.recall_shield_assumption_used,"both shield-hit alternatives explicitly conditional");
        check(cancel.recall_arrivals==0&&keep.recall_arrivals==1&&terminal(cancel).hp==terminal(keep).hp,"shield branch changes channel, never invents HP damage");
        auto stunned=world(1,1);stunned.bodies[0].shield_present=true;stunned.bodies[0].shield=100;stunned.bodies[0].shield_turns=9;
        stunned.bodies[1].enhanced_present=true;stunned.bodies[1].enhanced_turns=1;stunned.bodies[1].enhanced_basic_stun=1;
        Context stun_c;stun_c.attack=true;Timeline stun_time(stunned,50);
        const auto certain=r::run(stunned,word({recall()}),stun_time.scenario,callbacks(stun_c),requirement(stunned,50));
        check(certain.status==r::Status::Complete&&certain.recall_interruptions==1&&!certain.recall_shield_assumption_used,"known atomic stun cancels even when the positive hit is shield-absorbed");
        auto invalid=shield_time.scenario;invalid.shielded_recall_hit=static_cast<r::Scenario::ShieldedRecallHit>(99);
        const auto malformed=r::run(w,word({recall()}),invalid,callbacks(c),requirement(w,55));
        check(malformed.status==r::Status::Invalid&&malformed.reason==r::Reason::InvalidScenario,"invalid shield-hit enum cannot become favorable preserve branch");
    }
    {
        auto w=world();Context c;c.landing=r::Verdict::Unknown;Timeline t(w,55);
        auto result=r::run(w,word({recall()}),t.scenario,callbacks(c),requirement(w,55));
        check(result.status==r::Status::Unsupported&&result.reason==r::Reason::UnknownRecallArrival&&terminal(result).position==c.old,"unknown landing never chooses a free spawn");
        c={};c.landing=static_cast<r::Verdict>(99);result=r::run(w,word({recall()}),t.scenario,callbacks(c),requirement(w,55));
        check(result.status==r::Status::Unsupported&&result.reason==r::Reason::UnknownRecallArrival&&terminal(result).position==c.old,"invalid arrival verdict never commits a free spawn");
        c={};c.landing=r::Verdict::Blocked;result=r::run(w,word({recall()}),t.scenario,callbacks(c),requirement(w,55));
        check(result.status==r::Status::Unsupported&&result.reason==r::Reason::RecallLandingBlocked,"blocked zero timer needs explicit blocked-wait premise");
        Timeline delayed(w,56);delayed.scenario.blocked_recall_wait_declared=true;c={};c.blocked_until=55;
        result=r::run(w,word({recall()}),delayed.scenario,callbacks(c),requirement(w,56));
        check(result.status==r::Status::Complete&&result.recall_blocked_arrivals==1&&result.next_usable_round==56&&result.recall_arrivals==1,"declared blocked wait retains zero timer until a later conditional landing");
        c={};c.own_spawn=false;result=r::run(w,word({recall()}),t.scenario,callbacks(c),requirement(w,55));
        check(result.status==r::Status::Unsupported&&result.reason==r::Reason::UnknownSpawn&&terminal(result).position==c.old,"own spawn validation is transactional before pose commit");
        auto occupied=world(1,1);occupied.bodies[1].position={0,9};Timeline occupied_time(occupied,55);c={};
        result=r::run(occupied,word({recall()}),occupied_time.scenario,callbacks(c),requirement(occupied,55));
        check(result.status==r::Status::Unsupported&&result.reason==r::Reason::RecallLandingBlocked&&terminal(result).position==c.old,"known occupied spawn rejects even an Allowed nomination");
    }
    {
        // Current owner ranks leave exactly zero, one, or two later enemy
        // opportunities. Each future round still has one ordered camp phase.
        for(unsigned foes=0;foes<=2;++foes) {
            auto w=world(1,foes);Context c;c.attack=true;Timeline t(w,50);
            for(unsigned i=1;i<=foes;++i)w.bodies[i].position={5,static_cast<int>(3+i)};
            const auto result=r::run(w,word({recall()}),t.scenario,callbacks(c),requirement(w,50));
            check(result.status==r::Status::Complete&&c.enemy_attempts==foes&&terminal(result).hp==1000-40*foes,"ordered0/1/2 enemy opportunities after Recall root");
            check(result.recall_interruptions==(foes?1u:0u)&&result.phases==0,"first positive enemy hit cancels once without a phantom camp batch");
            Timeline with_camp(w,51);Context camp_c;camp_c.attack=true;camp_c.damage_round=51;camp_c.damage=10;
            const auto camp_result=r::run(w,word({recall()}),with_camp.scenario,callbacks(camp_c),requirement(w,51));
            check(camp_result.status==r::Status::Complete&&camp_result.phases==1&&camp_c.old_camp_phases==1&&
                terminal(camp_result).hp==990-80*foes,"0/1/2 enemies plus next ordered CAMP are all counted");
        }
        auto w=world(1,1);w.bodies[1].ranged=true;w.bodies[1].attack_range=10;w.bodies[1].position={0,8};Context c;c.attack=true;
        w.bodies[0].hp=500;w.spawn_healing_fraction=0.1f;Timeline t(w,56);
        // Initial enemy is out of old-site range; after arrival it still has a
        // fresh ranged opportunity according to the actual team order.
        w.bodies[1].attack_range=1;
        const auto result=r::run(w,word({recall()}),t.scenario,callbacks(c),requirement(w,56));
        check(result.status==r::Status::Complete&&result.recall_arrivals==1&&terminal(result).hp==660&&c.enemy_attempts==1,"own spawn healing does not exclude a later ranged hit");
    }
    {
        // No-Recall semantics in the opt-in model agree with the exact legacy
        // engine's movement, HP and future usable-input count.
        const auto old_w=::world();Context c;auto w=world(1,1);w.round=old_w.round;w.bodies[0].recall.queried_start_ticks=-1;
        Timeline t(w,11);::Context old_c;::Timeline old_t(old_w,11);
        const auto old=::run(old_w,::word({::move({3,4})}),old_t.scenario,::callbacks(old_c),::requirement(old_w,11));
        const auto same=r::run(w,word({move({3,4})}),t.scenario,callbacks(c),requirement(w,11));
        check(same.status==r::Status::Complete&&same.next_usable_round==old.next_usable_round&&same.usable_inputs==old.usable_inputs,"opt-in noRecall timing agrees with legacy");
        check(terminal(same).hp==::terminal(old,0).hp&&terminal(same).position.x==::terminal(old,0).position.x,"opt-in noRecall HP and placement agree with legacy");
        w.bodies[0].channel_present=true;const auto unsupported=r::run(w,word(),t.scenario,callbacks(c),requirement(w,11));
        check(unsupported.status==r::Status::Unsupported&&unsupported.reason==r::Reason::UnknownEffects,"untyped old channel remains unsupported");
    }
}
} // namespace recall_fixture

int main() {
    {
        BodySlot slot;check(slot.id==-1&&slot.modeled&&slot.hp==0&&!slot.completed_turn,"body slot keeps default semantics");
        auto source=body(17,0,{6,7});source.cooldown={3,4};source.abilities[0]=reset(true);slot=source;
        check(slot.id==17&&slot.position==Point{6,7}&&slot.cooldown==source.cooldown&&slot.abilities[0].enhanced_reset,"body slot assignment preserves exact body fields");
        const BodySlot copied=slot;check(copied.id==slot.id&&copied.abilities[0].kind==Kind::Reset,"body slot stays trivial copied storage");
    }
    {
        World bad;Context c;Scenario s;Requirement r;auto result=run(bad,word(),s,callbacks(c),r);
        check(result.status==Status::Invalid&&result.reason==Reason::InvalidWorld,"invalid world");
        auto w=world();Timeline t(w,13);result=run(w,word(),t.scenario,callbacks(c),requirement(w,13));
        check(result.status==Status::Complete&&result.service==Service::Ready,"complete hold reaches usable");
        check(result.scheduled_inputs==3&&result.usable_inputs==3&&result.next_usable_round==11,"separate future inputs");
        check(result.phases==3&&result.modeled_only&&result.horizon_reached&&result.alive_at_horizon,"bounded modeled phases");
        check(w.bodies[0].m&&w.bodies[0].hp==1000&&w.round==10,"input immutable");
        check(!terminal(result,0).m&&terminal(result,0).completed_turn,"end closes actual action scope");
    }
    {
        auto w=world();w.bodies[0].ranged=true;w.bodies[0].attack_range=3;w.bodies[0].abilities[0]=reset();
        Context c;Timeline t(w,10);auto r=requirement(w,10);r.require_future_usable=false;
        const auto legal=run(w,word({move({4,3}),cast(0),basic(1)}),t.scenario,callbacks(c),r);
        check(legal.status==Status::Complete&&legal.primitives==3,"ranged move reset basic");
        check(terminal(legal,1).hp==960&&terminal(legal,0).cooldown[0]==7,"reset damage and end cooldown");
        const auto illegal=run(w,word({move({4,3}),basic(1)}),t.scenario,callbacks(c),r);
        check(illegal.status==Status::Invalid&&illegal.reason==Reason::Resource&&illegal.primitives==1,"ranged move consumes attack");
        check(terminal(illegal,0).position==Point{4,3}&&!terminal(illegal,0).a&&terminal(illegal,0).c,"truncated prefix retained");
        w.bodies[0].abilities[1]=damage(50);const auto double_cast=run(w,word({cast(0),cast(1,1)}),t.scenario,callbacks(c),r);
        check(double_cast.status==Status::Invalid&&double_cast.reason==Reason::Resource&&double_cast.primitives==1,"reset never restores cast");
        w.bodies[0].abilities[1].excludes_walk=true;
        const auto excluded=run(w,word({move({4,3}),cast(1,1)}),t.scenario,callbacks(c),r);
        check(excluded.status==Status::Invalid&&excluded.reason==Reason::WalkingExcluded,"walk excludes spell");
        w.bodies[0].provenance_known=false;
        const auto unknown=run(w,word({cast(1,1)}),t.scenario,callbacks(c),r);
        check(unknown.status==Status::Unsupported&&unknown.reason==Reason::WalkingExcluded,"unknown walk provenance gates only exclusive cast");
        const auto compatible=run(w,word({cast(0),basic(1)}),t.scenario,callbacks(c),r);
        check(compatible.status==Status::Complete,"unknown walk permits compatible reset");
        Limits limit;limit.primitives=1;
        const auto limited=run(w,word({move({4,3}),cast(0),basic(1)}),t.scenario,callbacks(c),r,limit);
        check(limited.status==Status::Partial&&limited.reason==Reason::PrimitiveLimit&&limited.primitives==1,"bounded prefix survives search limit");
        check(terminal(limited,0).position==Point{4,3}&&terminal(limited,0).c,"limit does not lose actual modeled prefix");
    }
    {
        auto w=world();w.bodies[0].any_kill_refresh=true;w.bodies[0].cooldown={5,6};w.bodies[1].hp=30;
        Context c;Timeline t(w,10);auto r=requirement(w,10);r.require_future_usable=false;
        const auto kill=run(w,word({basic(1)}),t.scenario,callbacks(c),r);
        check(kill.status==Status::Complete&&terminal(kill,1).hp==0,"lethal modeled basic");
        check(terminal(kill,0).cooldown==std::array<int,2>{0,0},"kill refreshes both cooldowns");
        w.bodies[0].abilities[0]=reset();w.bodies[0].cooldown[0]=0;
        const auto recast=run(w,word({cast(0),basic(1),cast(0)}),t.scenario,callbacks(c),r);
        check(recast.status==Status::Invalid&&recast.reason==Reason::Resource&&recast.primitives==2,"kill does not refresh spent C");
        const auto dead_suffix=run(w,word({basic(1),cast(0),basic(1)}),t.scenario,callbacks(c),r);
        check(dead_suffix.status==Status::Partial&&dead_suffix.reason==Reason::NeedNomination&&dead_suffix.primitives==2,"dead target requires fresh nomination");
        check(terminal(dead_suffix,0).a&&!terminal(dead_suffix,0).c,"death does not end remaining input");
    }
    {
        auto w=world();w.bodies[0].hp=30;w.bodies[1].abilities[0]=reset();Context c;c.mode=Mode::ResetStrike;c.target=0;
        Timeline t(w,11);const auto fatal=run(w,word(),t.scenario,callbacks(c),requirement(w,11));
        check(fatal.status==Status::Partial&&fatal.reason==Reason::RequiredOwnerDied&&fatal.service==Service::Refuted,"fatal first basic refutes before queued reset");
        check(fatal.primitives==1&&terminal(fatal,1).c&&terminal(fatal,1).cooldown[0]==0,"dead required target suffix is not executed");
        check(fatal.usable_inputs==0&&!fatal.horizon_reached&&fatal.refutation_proved,"coherent fatal prefix independent of future policy");
    }
    {
        auto w=world();w.bodies[0].hp=w.bodies[0].max_hp=100;w.bodies[1].basic=30;
        w.bodies[0].shield_present=true;w.bodies[0].shield=100;w.bodies[0].shield_turns=1;
        Context c;c.mode=Mode::Attack;c.target=0;c.phase_damage=80;c.phase_target=0;Timeline t(w,11);
        // Root ends shield1->0. Enemy30, then the ONE camp80 consumes the
        // remaining70, before owner-start expires zero-timer residual shields.
        const auto shielded=run(w,word(),t.scenario,callbacks(c),requirement(w,11));
        check(shielded.status==Status::Complete&&shielded.phases==1,"one camp batch for next scheduled input");
        check(terminal(shielded,0).hp==60&&!terminal(shielded,0).shield_present,"shield ordered consumption no refill");
        // There are TWO enemy turns: current30 and next30 before owner in R11.
        // 100shield absorbs100 of140 total, leaving60HP. The actual result is
        // asserted below after accounting the full ordered schedule.
        check(shielded.service==Service::Ready&&shielded.next_usable_round==11,"shield timer zero did not expire at global start");
        w.bodies[0].shield_turns=0;
        const auto zero=run(w,word(),t.scenario,callbacks(c),requirement(w,11));
        check(zero.status==Status::Complete&&terminal(zero,0).hp==terminal(shielded,0).hp,"root zero shield remains present until next ownerstart");
        w.bodies[0].shield_present=false;w.bodies[0].shield=0;w.bodies[0].abilities[0].kind=Kind::Shield;
        w.bodies[0].abilities[0].amount=100;w.bodies[0].abilities[0].duration=1;w.bodies[0].abilities[0].max_cooldown=8;w.bodies[0].abilities[0].target_mask=self_target;
        const auto cast_shield=run(w,word({cast(0)}),t.scenario,callbacks(c),requirement(w,11));
        check(cast_shield.status==Status::Complete&&terminal(cast_shield,0).hp==terminal(shielded,0).hp,"fresh shield counts root ownerend once");
        w.bodies[0].shield_present=true;w.bodies[0].shield=10;
        const auto overlap=run(w,word({cast(0)}),t.scenario,callbacks(c),requirement(w,11));
        check(overlap.status==Status::Unsupported&&overlap.reason==Reason::OverlappingShield,"no invented shield overlap rule");
    }
    {
        auto w=world();w.bodies[0].stun_present=true;w.bodies[0].stun_turns=0;
        Context c;Timeline t(w,11);
        const auto currently_stunned=run(w,word({basic(1)}),t.scenario,callbacks(c),requirement(w,11));
        check(currently_stunned.status==Status::Invalid&&currently_stunned.reason==Reason::Resource,"already-started root is not reexpired");
        const auto expires=run(w,word(),t.scenario,callbacks(c),requirement(w,11));
        check(expires.status==Status::Complete&&expires.usable_inputs==1,"zero stun expires only next ownerstart");
        w.bodies[0].stun_turns=1;
        const auto one=run(w,word(),t.scenario,callbacks(c),requirement(w,11));
        check(one.status==Status::Complete&&one.usable_inputs==1,"root stun1 ends0 then expires next start");
        w.bodies[0].stun_present=false;w.bodies[0].stun_turns=0;w.bodies[0].non_timed_restrictions=1;
        const auto restriction=run(w,word(),t.scenario,callbacks(c),requirement(w,11));
        check(restriction.status==Status::Unsupported&&restriction.reason==Reason::UnknownEffects,"non timed restriction stays unsupported");
    }
    {
        auto w=world();w.bodies[1].provenance_known=false;w.bodies[1].m=false;w.bodies[1].a=false;
        w.bodies[1].abilities[0]=damage(50,true);Context c;c.mode=Mode::Hold;Timeline t(w,11);
        t.storage[0].already_started=true;
        const auto held=run(w,word(),t.scenario,callbacks(c),requirement(w,11));
        check(held.status==Status::Complete,"captured current input may end without fresh MAC");
        auto bad=t.scenario;t.storage[1].already_started=true;
        const auto future_resume=run(w,word(),bad,callbacks(c),requirement(w,11));
        check(future_resume.status==Status::Invalid&&future_resume.reason==Reason::InvalidScenario,"future input cannot skip its start");
        t.storage[1].already_started=false;c.mode=Mode::Chain;c.owner=0;
        w.bodies[1].abilities[0].kind=Kind::Area;w.bodies[1].abilities[0].target_mask=ground_target;
        w.bodies[1].abilities[0].radius=0;w.bodies[1].abilities[0].stun_turns=1;w.bodies[1].abilities[1]=reset();
        const auto resumed=run(w,word(),t.scenario,callbacks(c),requirement(w,11));
        check(resumed.status==Status::Unsupported&&resumed.reason==Reason::WalkingExcluded,"resumed input keeps unknown walk provenance");
        t.storage[0].already_started=false;
        const auto fresh=run(w,word(),t.scenario,callbacks(c),requirement(w,11));
        check(fresh.status==Status::Complete&&fresh.usable_inputs==0,"declared fresh pending input resets provenance");
    }
    {
        auto w=world();w.bodies[1].abilities[0].kind=Kind::Area;auto& stun=w.bodies[1].abilities[0];
        stun.range=4;stun.radius=0;stun.max_cooldown=8;stun.stun_turns=1;stun.target_mask=ground_target;stun.excludes_walk=true;
        w.bodies[1].abilities[1]=reset();Context c;c.mode=Mode::Chain;c.owner=0;Timeline t(w,11);
        const auto locked=run(w,word(),t.scenario,callbacks(c),requirement(w,11));
        check(locked.status==Status::Complete&&locked.alive_at_horizon&&locked.scheduled_inputs==1,"alive at scheduled cutoff");
        check(locked.usable_inputs==0&&locked.service==Service::Refuted&&locked.refutation==Refutation::MissedUsableDeadline,"alive is not usable service");
        w.bodies[0].stun_immune=true;
        const auto immune=run(w,word(),t.scenario,callbacks(c),requirement(w,11));
        check(immune.status==Status::Complete&&immune.usable_inputs==1&&immune.service==Service::Ready,"stun immunity affects usable horizon");
    }
    {
        // Synthetic amounts with the actual R50..53 ORDER and denial pattern,
        // not a claim to reproduce native damage or unobserved opponent intent.
        World w;w.count=6;w.owner=4;w.round=50;w.revision=54;w.respawn_ticks=60;w.spawn_healing_known=true;w.spawn_healing_fraction=1;
        w.bodies[0]=body(0,0,{4,4});w.bodies[0].basic=32;
        w.bodies[1]=body(1,0,{6,3});w.bodies[1].basic=45;w.bodies[1].max_hp=w.bodies[1].hp=1200;
        auto& area=w.bodies[1].abilities[0];area.kind=Kind::Area;area.range=4;area.radius=1;area.amount=100;area.max_cooldown=8;
        area.target_mask=ground_target;area.stun_turns=1;area.excludes_walk=true;w.bodies[1].abilities[1]=reset();
        w.bodies[2]=body(2,0,{6,4});w.bodies[2].basic=45;w.bodies[2].abilities[0]=reset(true);
        w.bodies[2].abilities[1]=damage(120,true);w.bodies[2].enhanced_basic_stun=1;w.bodies[2].stun_immune=true;
        w.bodies[3]=body(3,1,{5,2});w.bodies[3].completed_turn=true;w.bodies[3].m=w.bodies[3].a=w.bodies[3].c=false;
        w.bodies[4]=body(4,1,{4,5});w.bodies[4].hp=600;w.bodies[4].max_hp=700;w.bodies[4].basic=55;
        w.bodies[4].ranged=true;w.bodies[4].attack_range=3;w.bodies[4].cooldown={6,0};w.bodies[4].abilities[1]=reset();
        w.bodies[4].abilities[0].kind=Kind::Heal;w.bodies[4].abilities[0].amount=120;w.bodies[4].abilities[0].range=4;
        w.bodies[4].abilities[0].max_cooldown=8;w.bodies[4].abilities[0].target_mask=self_target|ally_target;
        w.bodies[5].id=5;w.bodies[5].team=1;w.bodies[5].modeled=false;
        Timeline t(w,56,1);t.scenario.allow_excluded_no_effect=true;Context c;c.mode=Mode::Chain;c.owner=4;c.require_spawn=true;
        const auto ingress=run(w,word({move({5,4})}),t.scenario,callbacks(c),requirement(w,56,&c));
        check(ingress.service==Service::Refuted&&ingress.refutation_proved&&ingress.refutation==Refutation::OwnerDied,"ordered stun renewal refutes recovery");
        check(ingress.status==Status::Partial&&ingress.reason==Reason::RequiredOwnerDied&&!ingress.horizon_reached,"fatal prefix stops without fabricated suffix");
        check(ingress.scheduled_inputs==2&&ingress.usable_inputs==0&&ingress.next_usable_round<0,"two scheduled inputs were unusable");
        check(ingress.refutation_round==53&&terminal(ingress,4).hp==0,"death before next usable input");
        check(ingress.excluded_mask==(1u<<5),"unseen vendor explicit no effect stub");
        // Same initial world/reply key, a different first move, then the same
        // explicit recovery programme. No oracle-dependent hidden suffix.
        Timeline recovery_time(w,56,1);recovery_time.scenario.allow_excluded_no_effect=true;
        Context recovery;recovery.mode=Mode::Chain;recovery.owner=4;recovery.require_spawn=true;
        const auto safer=run(w,word({move({3,6})}),recovery_time.scenario,callbacks(recovery),requirement(w,56,&recovery));
        check(safer.status==Status::Complete&&safer.service==Service::Ready,"paid retreat reaches restored service");
        check(safer.next_usable_round==52&&safer.service_ready_round==56&&safer.alive_at_horizon,"next usable differs from full recovery");
        check(terminal(safer,4).position==recovery.spawn&&terminal(safer,4).hp==700,"actual modeled spawn ownerend healing");
        check(safer.reply_id==ingress.reply_id&&safer.assumptions==ingress.assumptions,"arms share declared reply identity");
        const auto counter=run(w,word({move({5,4}),cast(1),basic(1)}),t.scenario,callbacks(c),requirement(w,56,&c));
        check(terminal(counter,1).hp==1145&&counter.primitives>=3,"move reset basic has modeled persistent hostile effect");
    }
    {
        auto w=world();w.bodies[0].hp=400;w.bodies[0].position={0,9};w.spawn_healing_fraction=1;
        Context c;c.spawn={0,9};c.require_spawn=true;Timeline t(w,11);
        const auto healed=run(w,word(),t.scenario,callbacks(c),requirement(w,11,&c));
        check(healed.status==Status::Complete&&healed.service_ready_round==11,"root ownerend heals before next start readiness");
        check(terminal(healed,0).hp==1000,"queried full spawn healing");
        w.spawn_healing_fraction=.25f;
        const auto partial=run(w,word(),t.scenario,callbacks(c),requirement(w,11,&c));
        check(partial.status==Status::Complete&&partial.service==Service::Refuted&&partial.refutation==Refutation::ReadinessRejected,"alive but not restored service");
        check(terminal(partial,0).hp==900&&partial.service_ready_round<0,"heals at each ownerend not free start heal");
        w.spawn_healing_known=false;const auto unknown=run(w,word(),t.scenario,callbacks(c),requirement(w,11,&c));
        check(unknown.status==Status::Unsupported&&unknown.reason==Reason::UnknownSpawn&&unknown.service==Service::Unknown,"unknown heal cannot become recovery refutation");
        w.spawn_healing_known=true;w.spawn_healing_fraction=1;c.unknown_readiness=true;
        const auto unknown_ready=run(w,word(),t.scenario,callbacks(c),requirement(w,11,&c));
        check(unknown_ready.status==Status::Complete&&unknown_ready.service==Service::Unknown&&!unknown_ready.refutation_proved,"unknown readiness stays distinct from refuted");
    }
    {
        auto w=world();w.bodies[0].heart=true;w.bodies[0].heart_bonus=.5f;w.bodies[0].hp=500;w.bodies[1].mitigation=.5f;
        Context c;c.mode=Mode::Attack;c.target=1;c.phase_target=0;c.phase_damage=100;Timeline t(w,11);
        const auto growing=run(w,word({basic(1)}),t.scenario,callbacks(c),requirement(w,11));
        check(growing.status==Status::Complete&&terminal(growing,0).hp==400,"phase damage before next owner start");
        check(terminal(growing,1).hp==949,"dynamic heart factor then victim mitigation per hit");
        w.bodies[0].abilities[0].kind=Kind::Shield;auto& a=w.bodies[0].abilities[0];a.amount=100;a.duration=5;a.max_cooldown=8;a.target_mask=self_target;
        Timeline current(w,10);auto req=requirement(w,10);req.require_future_usable=false;c.mode=Mode::Hold;c.phase_damage=0;
        const auto shield=run(w,word({cast(0)}),current.scenario,callbacks(c),req);
        check(shield.status==Status::Complete&&terminal(shield,0).shield==100,"heart bonus does not multiply shield");
    }
    {
        auto w=world();w.bodies[0].hp=900;auto& a=w.bodies[0].abilities[0];a.kind=Kind::Heal;a.amount=200;a.range=4;
        a.max_cooldown=8;a.target_mask=self_target|ally_target;Context c;Timeline t(w,10);
        auto req=requirement(w,10);req.require_future_usable=false;
        const auto healing=run(w,word({cast(0,0),basic(1)}),t.scenario,callbacks(c),req);
        check(healing.status==Status::Complete&&terminal(healing,0).hp==1000,"heal capped at actual max HP");
        check(terminal(healing,1).hp==960&&terminal(healing,0).cooldown[0]==7,"heal uses C but preserves compatible basic");
        const auto wrong=run(w,word({cast(0,1)}),t.scenario,callbacks(c),req);
        check(wrong.status==Status::Invalid&&wrong.reason==Reason::Target,"heal cannot target hostile body");
        w.bodies[0].any_kill_refresh=true;w.bodies[1].hp=30;w.bodies[0].abilities[0]=reset();
        auto& dash=w.bodies[0].abilities[1];dash.kind=Kind::Dash;dash.range=2;dash.max_cooldown=8;dash.target_mask=ground_target;
        w.bodies[0].cooldown[1]=5;
        const auto kill_dash=run(w,word({basic(1),cast(1,-1,{2,4}),move({1,4})}),t.scenario,callbacks(c),req);
        check(kill_dash.status==Status::Complete&&terminal(kill_dash,0).position==Point{1,4},"kill refresh funds cooling dash then ordinary move");
        check(kill_dash.primitives==3&&terminal(kill_dash,0).cooldown[1]==7,"dash spends one cast and ticks once");
        const auto reset_kill=run(w,word({cast(0),basic(1),cast(1,-1,{2,4})}),t.scenario,callbacks(c),req);
        check(reset_kill.status==Status::Invalid&&reset_kill.reason==Reason::Resource,"kill after reset leaves dash cast spent");
        w=world(1,2);w.bodies[0].basic_splash=1;w.bodies[1].hp=30;w.bodies[2].position={5,5};w.bodies[2].hp=60;
        Timeline splash_time(w,10);req=requirement(w,10);req.require_future_usable=false;
        const auto splash=run(w,word({basic(1)}),splash_time.scenario,callbacks(c),req);
        check(splash.status==Status::Complete&&terminal(splash,1).hp==0&&terminal(splash,2).hp==20,"basic collateral is part of one coherent primitive");
    }
    {
        auto w=world();Context c;c.hidden=1;c.mode=Mode::SeeHidden;Timeline t(w,11);
        const auto hidden=run(w,word(),t.scenario,callbacks(c),requirement(w,11));
        check(hidden.status==Status::Complete&&c.saw_hidden&&!c.leaked,"policies receive filtered observations");
        const auto hidden_attack=run(w,word({basic(1)}),t.scenario,callbacks(c),requirement(w,11));
        check(hidden_attack.status==Status::Invalid&&hidden_attack.reason==Reason::Target,"unseen unit is not a legal target");
        c.hidden=-1;c.unknown_visibility=true;
        const auto unknown_attack=run(w,word({basic(1)}),t.scenario,callbacks(c),requirement(w,11));
        check(unknown_attack.status==Status::Unsupported&&unknown_attack.reason==Reason::UnknownVisibility,"unknown sight not false permission");
        c.unknown_visibility=false;c.unknown_geometry=true;
        const auto unknown_move=run(w,word({move({3,4})}),t.scenario,callbacks(c),requirement(w,11));
        check(unknown_move.status==Status::Unsupported&&unknown_move.reason==Reason::UnknownGeometry&&terminal(unknown_move,0).position==Point{4,4},"unknown path grants no modeled move");
        c.unknown_geometry=false;c.unknown_phase=true;
        const auto unknown_phase=run(w,word(),t.scenario,callbacks(c),requirement(w,11));
        check(unknown_phase.status==Status::Unsupported&&unknown_phase.reason==Reason::UnknownWorldPhase&&unknown_phase.service==Service::Unknown,"unknown world coverage is not refutation");
        c.unknown_phase=false;
        auto incomplete=t.scenario;incomplete.current_remaining_complete=false;
        const auto invalid_schedule=run(w,word(),incomplete,callbacks(c),requirement(w,11));
        check(invalid_schedule.status==Status::Invalid&&invalid_schedule.reason==Reason::InvalidScenario,"missing current schedule not narrowed silently");
        auto reordered=t.scenario;std::array<Slot,96> bad=t.storage; // overwritten below with explicit duplicate
        bad[0]=t.storage[1];reordered.schedule={bad.data(),t.count};
        const auto duplicate=run(w,word(),reordered,callbacks(c),requirement(w,11));
        check(duplicate.status==Status::Invalid&&duplicate.reason==Reason::InvalidScenario,"wrong scheduled round rejected");
        w.bodies[0].completed_turn=true;
        const auto double_end=run(w,word(),t.scenario,callbacks(c),requirement(w,11));
        check(double_end.status==Status::Invalid&&double_end.reason==Reason::InvalidWorld,"completed root cannot end twice");
        w.bodies[0].completed_turn=false;w.bodies[1].completed_turn=true;
        const auto second_input=run(w,word(),t.scenario,callbacks(c),requirement(w,11));
        check(second_input.status==Status::Invalid&&second_input.reason==Reason::InvalidScenario,"completed current actor cannot gain second input");
        w.bodies[1].completed_turn=false;w.bodies[1].modeled=false;
        const auto stub=run(w,word(),t.scenario,callbacks(c),requirement(w,11));
        check(stub.status==Status::Unsupported&&stub.reason==Reason::UnsupportedBody,"excluded actor needs declared assumption");
        t.scenario.allow_excluded_no_effect=true;
        const auto declared_stub=run(w,word(),t.scenario,callbacks(c),requirement(w,11));
        check(declared_stub.status==Status::Complete&&declared_stub.excluded_mask==2,"declared stub preserves roster timing");
    }
    {
        auto w=world();Context c;c.hidden=1;
        Observation reused;reused.count=body_limit;
        for(auto& entry:reused.bodies){entry.visible=true;entry.visibility_known=true;entry.body=&w.bodies[0];}
        reused=detail::observe(callbacks(c),w,0,1);
        check(reused.find(0)==&w.bodies[0]&&reused.find(1)==nullptr,"borrowed view binds exact visible body only");
        check(reused.bodies[1].body==nullptr&&!reused.bodies[1].visible&&reused.bodies[1].visibility_known,"poisoned hidden pointer is removed");
        for(unsigned i=w.count;i<body_limit;++i)check(reused.bodies[i].body==nullptr,"inactive poison pointer is removed");
        c.mode=Mode::SeeHidden;c.hide_from_round=12;c.phase_target=1;c.phase_damage=100;Timeline t(w,12);
        const auto changing=run(w,word(),t.scenario,callbacks(c),requirement(w,12));
        check(changing.status==Status::Complete&&c.saw_visible&&c.saw_hidden&&!c.leaked,"next phase cannot retain a now hidden body pointer");
        check(terminal(changing,1).hp==800,"hidden model changes never exposed through policy view");
    }
    {
        auto w=world();w.bodies[0].abilities[0]=reset(true);w.bodies[0].enhanced_basic_stun=1;Context c;Timeline t(w,10);
        auto req=requirement(w,10);req.require_future_usable=false;
        const auto unconsumed=run(w,word({cast(0)}),t.scenario,callbacks(c),req);
        check(unconsumed.status==Status::Unsupported&&unconsumed.reason==Reason::UnknownEnhancedDuration,"no invented enhanced buff lifetime");
        const auto consumed=run(w,word({cast(0),basic(1)}),t.scenario,callbacks(c),req);
        check(consumed.status==Status::Complete&&!terminal(consumed,0).enhanced_present,"same word enhancement consumed");
        check(terminal(consumed,1).stun_present&&terminal(consumed,1).stun_turns==0,"new stun decrements at victim ownerend");
        w.bodies[1].stun_immune=true;
        const auto immune=run(w,word({cast(0),basic(1)}),t.scenario,callbacks(c),req);
        check(immune.status==Status::Complete&&!terminal(immune,1).stun_present,"enhanced stun respects immunity");
        w.bodies[0].abilities[0].enhanced_turns=1;
        const auto known_duration=run(w,word({cast(0)}),t.scenario,callbacks(c),req);
        check(known_duration.status==Status::Complete&&terminal(known_duration,0).enhanced_present&&terminal(known_duration,0).enhanced_turns==0,"known enhancement lifetime explicit");
    }
    {
        // Every legal team-size/starter combination gets a full ordered
        // continuation. This checks extra-body append and both boundary orders.
        for(unsigned own=1;own<=3;++own)for(unsigned foe=1;foe<=3;++foe)for(int starter=0;starter<2;++starter) {
            const auto w=world(own,foe);Context c;Timeline t(w,13,starter);
            const auto result=run(w,word(),t.scenario,callbacks(c),requirement(w,13));
            check(result.status==Status::Complete&&result.service==Service::Ready,"unequal roster complete schedule");
            check(result.scheduled_inputs==3&&result.usable_inputs==3&&result.phases==3,"extra bodies do not create camp batches");
            check(result.slots==t.count&&result.primitives==0,"all scheduled slots accounted");
        }
        auto w=world();Context c;Timeline t(w,11);Limits short_slots;short_slots.slots=1;
        const auto short_result=run(w,word(),t.scenario,callbacks(c),requirement(w,11),short_slots);
        check(short_result.status==Status::Partial&&short_result.reason==Reason::SlotLimit&&!short_result.refutation_proved,"slot limit is unknown coverage");
        Limits phases;phases.phases=0;
        const auto short_phase=run(w,word(),t.scenario,callbacks(c),requirement(w,11),phases);
        check(short_phase.status==Status::Partial&&short_phase.reason==Reason::PhaseLimit&&short_phase.service==Service::Unknown,"phase limit distinct from failure");
        auto bad=w;bad.bodies[0].hp=std::numeric_limits<float>::quiet_NaN();
        const auto nan=run(bad,word(),t.scenario,callbacks(c),requirement(w,11));
        check(nan.status==Status::Invalid&&nan.reason==Reason::InvalidWorld,"nonfinite input rejected");
    }
    recall_fixture::tests();
    std::printf("capital_encounter %u checks %u failures\n",checks,failures);
    return failures?1:0;
}
