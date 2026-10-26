#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

// Finite ordered conditional encounter forecasts; never SDK authority or receipts.
namespace wb::capital::encounter {
inline constexpr unsigned body_limit=6,word_limit=4,phase_effect_limit=24;
inline constexpr unsigned camp_target=1,enemy_target=2,ally_target=4,self_target=8,ground_target=16;
struct Point {int x{},y{};};
inline constexpr bool operator==(Point a,Point b){return a.x==b.x&&a.y==b.y;}
inline int distance(Point a,Point b){return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
enum class Kind {None,Reset,Damage,Area,Stun,Dash,Shield,Heal};
enum class ShieldRule {OnlyAbsent,Replace,Add,Maximum};
struct Ability {
    Kind kind{Kind::None};float range{},radius{},amount{};
    int max_cooldown{},duration{},stun_turns{},enhanced_turns{-1};
    unsigned target_mask{},affect_mask{enemy_target};
    bool excludes_walk{},enhanced_reset{};
    ShieldRule shield_rule{ShieldRule::OnlyAbsent};
};
struct Body {
    int id{-1},team{-1};Point position{};
    float hp{},max_hp{},shield{},basic{},mitigation{1},basic_splash{},heart_bonus{};
    int move_range{},attack_range{},shield_turns{},stun_turns{},enhanced_turns{},enhanced_basic_stun{},respawn_turns{-1};
    std::array<int,2> cooldown{};std::array<Ability,2> abilities{};
    bool alive{},m{},a{},c{},walked{},cast_used{},exclusive_cast{},ranged{},stun_immune{},any_kill_refresh{},heart{};
    bool shield_present{},stun_present{},enhanced_present{},provenance_known{},effects_closed{};
    bool modeled{true};unsigned non_timed_restrictions{};bool channel_present{},completed_turn{};
};
// Each published Body pointer refers to ONE base subobject, not an element of
// a public Body array that permits legal adjacent indexing into hidden state.
// This remains a trusted C++ callback contract, not a malicious-code sandbox.
struct BodySlot final:Body {
    BodySlot()=default;
    BodySlot(const Body& value):Body(value){}
    BodySlot& operator=(const Body& value){static_cast<Body&>(*this)=value;return *this;}
};
static_assert(sizeof(BodySlot)==sizeof(Body));
struct World {
    std::array<BodySlot,body_limit> bodies{};unsigned count{};int round{-1},owner{-1};
    std::uint64_t revision{};
    int respawn_ticks{-1};float spawn_healing_fraction{};bool spawn_healing_known{};
};
enum class PrimitiveKind {Move,Basic,Cast};
struct Primitive {PrimitiveKind kind{PrimitiveKind::Basic};int target{-1},slot{-1};Point destination{};};
struct Word {std::array<Primitive,word_limit> steps{};unsigned count{};bool complete{};};
struct Slot {
    int round{-1},actor{-1};
    // A CURRENT-round captured action phase may be resumed without laundering
    // unknown walk/MAC history into a fresh input. Future rounds always start.
    bool already_started{};
};
struct Scenario {
    std::uint64_t reply_id{},revision{},assumptions{};
    int horizon_round{-1},current_slot{-1},starting_team{-1};
    std::span<const Slot> schedule{};
    bool current_remaining_complete{},future_rounds_complete{},allow_excluded_no_effect{};
};
enum class Verdict {Allowed,Blocked,Unknown};
struct ObservedBody {int id{-1},team{-1};bool visible{},visibility_known{};const Body* body{};};
struct Observation {
    int observer{-1},round{-1};unsigned scheduled_input{};
    std::array<ObservedBody,body_limit> bodies{};unsigned count{};
    const Body* find(int id) const {
        for(unsigned i=0;i<count;++i)if(bodies[i].id==id&&bodies[i].visible)return bodies[i].body;
        return nullptr;
    }
};
enum class PhaseKind {Damage,Heal,Stun,Shield};
struct PhaseEffect {PhaseKind kind{PhaseKind::Damage};int source{-1},target{-1};float amount{};int duration{};};
struct Phase {std::array<PhaseEffect,phase_effect_limit> effects{};unsigned count{};bool complete{};};
using GeometryFn=Verdict(*)(void*,const World&,int,const Primitive&);
using VisibilityFn=Verdict(*)(void*,const World&,int,int);
using PolicyFn=Word(*)(void*,const Observation&);
using PhaseFn=Phase(*)(void*,const World&,int);
using SpawnFn=Verdict(*)(void*,const World&,int);
using ReadinessFn=Verdict(*)(void*,const Observation&);
struct Callbacks {
    void* context{};GeometryFn geometry{};VisibilityFn visibility{};
    PolicyFn policy{};PhaseFn phase{};SpawnFn spawn{};
};
struct Requirement {
    int owner{-1},deadline_round{-1};float minimum_hp{};
    bool require_alive{true},require_future_usable{true},survive_entire_horizon{true};
    ReadinessFn readiness{};void* context{};
};
struct Limits {unsigned slots{96},primitives{384},phases{16};};
enum class Status {Complete,Partial,Invalid,Unsupported};
enum class Service {Ready,Refuted,Unknown};
enum class Reason {
    None,InvalidWorld,InvalidScenario,InvalidRequirement,InvalidLimits,InvalidPolicy,
    UnsupportedBody,UnknownEffects,UnknownGeometry,UnknownVisibility,UnknownPolicy,
    UnknownWorldPhase,UnknownSpawn,UnknownEnhancedDuration,OverlappingShield,
    UnsupportedStun,UnsupportedRespawn,IllegalPrimitive,Resource,Target,Range,
    WalkingExcluded,SlotLimit,PrimitiveLimit,PhaseLimit,RequiredOwnerDied,NeedNomination
};
enum class Refutation {None,OwnerDied,MissedUsableDeadline,ReadinessRejected};
struct Outcome {
    Status status{Status::Invalid};Reason reason{Reason::InvalidWorld};
    Service service{Service::Unknown};Refutation refutation{Refutation::None};
    std::uint64_t reply_id{},revision{},assumptions{};World terminal{};
    unsigned slots{},primitives{},phases{},scheduled_inputs{},usable_inputs{},excluded_mask{};
    int next_usable_round{-1},service_ready_round{-1},refutation_round{-1};
    bool horizon_reached{},alive_at_horizon{},refutation_proved{},readiness_unknown{};
    bool modeled_only{true};
};

// Callbacks receive no private gold. Hidden policy observations carry identity
// and team only; even their modeled future positions have no borrowed pointer.
// Visible Body pointers are borrowed ONLY for this synchronous callback: never
// index/arithmetic a Body pointer or retain it across callback return, a model effect or an SDK
// invalidation. A policy must return incomplete when selection depends on an
// unknown visibility predicate. Committed words independent of that predicate
// remain possible. Legal geometry is a separate pure callback.
// Shield overlap rules other than OnlyAbsent need an admitted engine contract.
// Zero-timer buffs keep explicit presence until THAT body's owner start.
inline Outcome run(const World& initial,const Word& root,const Scenario& scenario,
                   const Callbacks& callbacks,const Requirement& requirement,Limits limits={});

namespace detail {
inline bool finite(float x){return std::isfinite(x)&&x>=0&&x<=1000000000.f;}
inline bool point(Point p){return p.x>=0&&p.y>=0&&p.x<=100000&&p.y<=100000;}
inline int at(const World& w,int id){for(unsigned i=0;i<w.count;++i)if(w.bodies[i].id==id)return static_cast<int>(i);return -1;}
inline bool team(int t){return t==0||t==1;}
inline bool ability(const Ability& a) {
    return static_cast<unsigned>(a.kind)<=static_cast<unsigned>(Kind::Heal)&&finite(a.range)&&finite(a.radius)&&finite(a.amount)
        &&a.range<=1000&&a.radius<=1000&&a.max_cooldown>=0&&a.max_cooldown<=10000&&a.duration>=0&&a.duration<=10000
        &&a.stun_turns>=0&&a.stun_turns<=1&&a.enhanced_turns>=-1&&a.enhanced_turns<=10000
        &&!(a.target_mask&~31u)&&!(a.affect_mask&~31u)&&static_cast<unsigned>(a.shield_rule)<=static_cast<unsigned>(ShieldRule::Maximum);
}
inline bool body(const Body& b) {
    if(b.id<0||!team(b.team))return false;
    if(!b.modeled)return true;
    if((!point(b.position)&&!(!b.alive&&b.position==Point{-1,-1}))||!finite(b.hp)||!finite(b.max_hp)||b.max_hp<=0||b.hp>b.max_hp
        ||b.alive!=(b.hp>0)||!finite(b.shield)||!finite(b.basic)||!finite(b.mitigation)||b.mitigation>1
        ||!finite(b.basic_splash)||b.basic_splash>1000||!finite(b.heart_bonus)||b.heart_bonus>10
        ||b.move_range<0||b.move_range>100||b.attack_range<0||b.attack_range>100
        ||b.shield_turns<0||b.shield_turns>10000||b.stun_turns<0||b.stun_turns>1
        ||b.enhanced_turns<0||b.enhanced_turns>10000||b.enhanced_basic_stun<0||b.enhanced_basic_stun>1
        ||b.respawn_turns< -1||b.respawn_turns>10000
        ||(!b.shield_present&&b.shield!=0)||(!b.stun_present&&b.stun_turns!=0)
        ||(!b.enhanced_present&&b.enhanced_turns!=0)||(b.c&&b.cast_used))return false;
    for(unsigned i=0;i<2;++i)if(b.cooldown[i]<0||b.cooldown[i]>10000||!ability(b.abilities[i]))return false;
    return true;
}
inline bool world(const World& w) {
    if(!w.count||w.count>body_limit||w.round<0||w.round>1000000000||!w.revision||w.respawn_ticks< -1||w.respawn_ticks>10000
        ||!finite(w.spawn_healing_fraction)||w.spawn_healing_fraction>1||at(w,w.owner)<0)return false;
    for(unsigned i=0;i<w.count;++i) {
        if(!body(w.bodies[i]))return false;
        for(unsigned j=0;j<i;++j)if(w.bodies[i].id==w.bodies[j].id)return false;
    }
    const auto& owner=w.bodies[static_cast<unsigned>(at(w,w.owner))];return owner.modeled&&!owner.completed_turn;
}
inline unsigned team_count(const World& w,int side){unsigned n=0;for(unsigned i=0;i<w.count;++i)n+=w.bodies[i].team==side?1u:0u;return n;}
inline int expected_team(const World& w,int starter,unsigned rank) {
    const auto first=team_count(w,starter),second=team_count(w,1-starter),paired=std::min(first,second);
    if(rank<2u*paired)return rank%2?1-starter:starter;
    return first>second?starter:1-starter;
}
inline bool schedule(const World& w,const Scenario& s) {
    if(!s.reply_id||!s.revision||!s.assumptions||s.horizon_round<w.round||s.horizon_round>w.round+10000
        ||s.current_slot<0||static_cast<unsigned>(s.current_slot)>=w.count||!team(s.starting_team)
        ||!s.current_remaining_complete||!s.future_rounds_complete)return false;
    const auto root=static_cast<unsigned>(at(w,w.owner));
    if(w.bodies[root].team!=expected_team(w,s.starting_team,static_cast<unsigned>(s.current_slot)))return false;
    const std::uint64_t needed=std::uint64_t(w.count)-static_cast<unsigned>(s.current_slot)-1u
        +std::uint64_t(s.horizon_round-w.round)*w.count;
    if(needed!=s.schedule.size())return false;
    unsigned cursor=0;
    for(int round=w.round;round<=s.horizon_round;++round) {
        const unsigned begin=round==w.round?static_cast<unsigned>(s.current_slot)+1u:0u;
        const int starter=s.starting_team^((round-w.round)&1);unsigned seen=round==w.round?(1u<<root):0u;
        for(unsigned rank=begin;rank<w.count;++rank) {
            const auto& slot=s.schedule[cursor++];const int who=at(w,slot.actor);
            if(slot.round!=round||who<0||(seen&(1u<<static_cast<unsigned>(who)))||(slot.already_started&&round!=w.round))return false;
            seen|=1u<<static_cast<unsigned>(who);
            if(round==w.round&&w.bodies[static_cast<unsigned>(who)].modeled&&w.bodies[static_cast<unsigned>(who)].completed_turn)return false;
            if(w.bodies[static_cast<unsigned>(who)].team!=expected_team(w,starter,rank))return false;
        }
    }
    return true;
}
inline float factor(const Body& b){return b.heart?std::clamp(1.f+b.heart_bonus*(1.f-b.hp/b.max_hp),1.f,1.f+b.heart_bonus):1.f;}
inline bool usable(const Body& b){return b.alive&&!b.stun_present&&!b.channel_present&&!b.completed_turn&&(b.m||b.a||b.c);}
inline void remove_shield(Body& b){b.shield_present=false;b.shield=0;b.shield_turns=0;}
inline void die(World& w,Body& b) {
    b.hp=0;b.alive=false;b.m=b.a=b.c=false;b.cast_used=false;b.exclusive_cast=false;
    remove_shield(b);b.stun_present=b.enhanced_present=false;b.stun_turns=b.enhanced_turns=0;b.respawn_turns=w.respawn_ticks;
}
inline void hit(World& w,Body& b,float amount) {
    const float absorbed=std::min(b.shield,amount);b.shield=std::max(0.f,b.shield-amount);
    b.hp=std::max(0.f,b.hp-std::max(0.f,amount-absorbed));
    if(b.shield_present&&b.shield<=0)remove_shield(b);
    if(b.hp<=0)die(w,b);
}
inline void stun(Body& b,int turns) {
    if(!b.alive||b.stun_immune||turns<=0)return;
    b.stun_present=true;b.stun_turns=std::max(b.stun_turns,turns);b.m=b.a=b.c=false;
}
inline void heal(Body& b,float amount){if(b.alive)b.hp=std::min(b.max_hp,b.hp+amount);}
inline bool shield(Body& b,float amount,int turns,ShieldRule rule) {
    if(!b.alive)return true;
    if(b.shield_present&&rule==ShieldRule::OnlyAbsent)return false;
    float value=amount;
    if(b.shield_present&&rule==ShieldRule::Add)value+=b.shield;
    if(b.shield_present&&rule==ShieldRule::Maximum)value=std::max(value,b.shield);
    if(!finite(value))return false;
    b.shield_present=true;b.shield=value;b.shield_turns=turns;return true;
}
inline Verdict visibility(const Callbacks& c,const World& w,int observer,int target) {
    if(observer==target)return Verdict::Allowed;
    return c.visibility?c.visibility(c.context,w,observer,target):Verdict::Unknown;
}
inline Observation observe(const Callbacks& c,const World& w,int observer,unsigned input) {
    Observation out;out.observer=observer;out.round=w.round;out.scheduled_input=input;out.count=w.count;
    for(unsigned i=0;i<w.count;++i) {
        const auto& b=w.bodies[i];auto& o=out.bodies[i];o.id=b.id;o.team=b.team;
        const auto v=b.modeled?visibility(c,w,observer,b.id):Verdict::Unknown;
        o.visibility_known=b.modeled&&v!=Verdict::Unknown;o.visible=b.modeled&&v==Verdict::Allowed;
        if(o.visible)o.body=&b;
    }
    return out;
}
} // namespace detail

namespace detail {
class Engine {
    Outcome out_{};const Scenario& scenario_;const Callbacks& callbacks_;const Requirement& requirement_;Limits limits_;
    unsigned owner_{};int deadline_{};std::array<unsigned,body_limit> inputs_{};
    bool readiness_rejected_{};
    World& world(){return out_.terminal;}
    const World& world() const{return out_.terminal;}
    bool fail(Status status,Reason reason){out_.status=status;out_.reason=reason;return false;}
    void refute(Refutation reason) {
        if(out_.refutation_proved)return;
        out_.service=Service::Refuted;out_.refutation=reason;out_.refutation_round=world().round;out_.refutation_proved=true;
    }
    void death_obligation() {
        if(requirement_.survive_entire_horizon&&!world().bodies[owner_].alive)refute(Refutation::OwnerDied);
    }
    bool geometry(int actor,const Primitive& primitive) {
        if(!callbacks_.geometry)return fail(Status::Unsupported,Reason::UnknownGeometry);
        const auto answer=callbacks_.geometry(callbacks_.context,world(),actor,primitive);
        if(answer==Verdict::Unknown)return fail(Status::Unsupported,Reason::UnknownGeometry);
        if(answer!=Verdict::Allowed)return fail(Status::Invalid,Reason::IllegalPrimitive);
        return true;
    }
    bool visible(int actor,int target) {
        const auto answer=visibility(callbacks_,world(),actor,target);
        if(answer==Verdict::Unknown)return fail(Status::Unsupported,Reason::UnknownVisibility);
        if(answer!=Verdict::Allowed)return fail(Status::Invalid,Reason::Target);
        return true;
    }
    int target(int id) {
        const int result=at(world(),id);
        if(result<0){fail(Status::Invalid,Reason::Target);return -1;}
        if(!world().bodies[static_cast<unsigned>(result)].modeled){fail(Status::Unsupported,Reason::UnsupportedBody);return -1;}
        if(!world().bodies[static_cast<unsigned>(result)].alive){fail(Status::Partial,Reason::NeedNomination);return -1;}
        return result;
    }
    bool category(const Body& source,const Body& victim,unsigned mask) const {
        const unsigned bit=source.id==victim.id?self_target:source.team==victim.team?ally_target:enemy_target;
        return (mask&bit)!=0;
    }
    void damage(unsigned source,unsigned victim,float raw,int stun_turns) {
        auto& w=world();auto& target_body=w.bodies[victim];const bool living=target_body.alive;
        hit(w,target_body,raw*target_body.mitigation);
        if(target_body.alive)stun(target_body,stun_turns);
        if(living&&!target_body.alive&&w.bodies[source].any_kill_refresh)w.bodies[source].cooldown={0,0};
        death_obligation();
    }
    void area(unsigned source,Point center,float radius,float amount,int stun_turns,unsigned mask) {
        const auto side=world().bodies[source].team;
        for(unsigned i=0;i<world().count;++i) {
            const auto& victim=world().bodies[i];
            if(!victim.modeled||!victim.alive||distance(center,victim.position)>radius)continue;
            const unsigned bit=i==source?self_target:victim.team==side?ally_target:enemy_target;
            if(mask&bit)damage(source,i,amount,stun_turns);
        }
    }
    bool primitive(unsigned who,const Primitive& primitive) {
        auto& b=world().bodies[who];
        if(static_cast<unsigned>(primitive.kind)>static_cast<unsigned>(PrimitiveKind::Cast))return fail(Status::Invalid,Reason::IllegalPrimitive);
        if(!b.alive||b.stun_present||b.channel_present||b.completed_turn)return fail(Status::Invalid,Reason::Resource);
        if(primitive.kind==PrimitiveKind::Move) {
            if(!b.m||b.exclusive_cast)return fail(Status::Invalid,Reason::Resource);
            if(!point(primitive.destination)||primitive.destination==b.position||distance(b.position,primitive.destination)>b.move_range)
                return fail(Status::Invalid,Reason::Range);
            if(!geometry(b.id,primitive))return false;
            b.position=primitive.destination;b.m=false;b.walked=true;b.provenance_known=true;if(b.ranged)b.a=false;
            return true;
        }
        if(primitive.kind==PrimitiveKind::Basic) {
            if(!b.a)return fail(Status::Invalid,Reason::Resource);
            const int target_index=target(primitive.target);if(target_index<0)return false;
            const auto victim=static_cast<unsigned>(target_index);const auto target_position=world().bodies[victim].position;
            if(b.team==world().bodies[victim].team)return fail(Status::Invalid,Reason::Target);
            if(distance(b.position,target_position)>b.attack_range)return fail(Status::Invalid,Reason::Range);
            if(!visible(b.id,primitive.target)||!geometry(b.id,primitive))return false;
            const float raw=b.basic*factor(b);const int stun_turns=b.enhanced_present?b.enhanced_basic_stun:0;
            b.a=false;if(b.ranged)b.m=false;b.enhanced_present=false;b.enhanced_turns=0;
            if(b.basic_splash>0)area(who,target_position,b.basic_splash,raw,stun_turns,enemy_target);
            else damage(who,victim,raw,stun_turns);
            return true;
        }
        if(!b.c||b.cast_used||primitive.slot<0||primitive.slot>=2)return fail(Status::Invalid,Reason::Resource);
        const auto slot=static_cast<unsigned>(primitive.slot);const auto ability=b.abilities[slot];
        if(ability.kind==Kind::None||b.cooldown[slot]>0)return fail(Status::Invalid,Reason::Resource);
        if(ability.excludes_walk&&(!b.provenance_known||b.walked))return fail(b.provenance_known?Status::Invalid:Status::Unsupported,Reason::WalkingExcluded);
        int victim=-1;Point center=primitive.destination;
        if(ability.kind==Kind::Reset||ability.kind==Kind::Shield) {
            if(!(ability.target_mask&self_target)||(primitive.target>=0&&primitive.target!=b.id))return fail(Status::Invalid,Reason::Target);
            if(ability.kind==Kind::Shield&&b.shield_present&&ability.shield_rule==ShieldRule::OnlyAbsent)
                return fail(Status::Unsupported,Reason::OverlappingShield);
        } else if(ability.kind==Kind::Dash) {
            if(!(ability.target_mask&ground_target)||!point(center)||center==b.position||distance(b.position,center)>ability.range)
                return fail(Status::Invalid,Reason::Range);
            if(!geometry(b.id,primitive))return false;
        } else if(primitive.target>=0) {
            victim=target(primitive.target);if(victim<0)return false;const auto& target_body=world().bodies[static_cast<unsigned>(victim)];
            if(!category(b,target_body,ability.target_mask))return fail(Status::Invalid,Reason::Target);
            if(distance(b.position,target_body.position)>ability.range)return fail(Status::Invalid,Reason::Range);
            if(!visible(b.id,primitive.target)||!geometry(b.id,primitive))return false;
            center=target_body.position;
        } else {
            if(ability.kind==Kind::Heal||!(ability.target_mask&ground_target)||!point(center)||distance(b.position,center)>ability.range)
                return fail(Status::Invalid,Reason::Target);
            if(!geometry(b.id,primitive))return false;
        }
        b.c=false;b.cast_used=true;b.cooldown[slot]=ability.max_cooldown;
        if(ability.excludes_walk){b.exclusive_cast=true;b.m=false;}
        switch(ability.kind) {
        case Kind::Reset:
            b.a=true;
            if(ability.enhanced_reset){b.enhanced_present=true;b.enhanced_turns=ability.enhanced_turns;}
            break;
        case Kind::Shield:
            if(!shield(b,ability.amount,ability.duration,ability.shield_rule))return fail(Status::Unsupported,Reason::OverlappingShield);
            break;
        case Kind::Heal:heal(world().bodies[static_cast<unsigned>(victim)],ability.amount);break;
        case Kind::Dash:
            b.position=center;area(who,center,ability.radius,ability.amount*factor(b),ability.stun_turns,ability.affect_mask);break;
        case Kind::Damage:case Kind::Area:case Kind::Stun:
            if(ability.kind==Kind::Area||victim<0)area(who,center,ability.radius,ability.amount*factor(b),ability.stun_turns,ability.affect_mask);
            else damage(who,static_cast<unsigned>(victim),ability.amount*factor(b),ability.stun_turns);
            break;
        default:return fail(Status::Invalid,Reason::IllegalPrimitive);
        }
        return true;
    }
    bool word(unsigned who,const Word& word) {
        if(!word.complete)return fail(Status::Unsupported,Reason::UnknownPolicy);
        if(word.count>word_limit)return fail(Status::Invalid,Reason::InvalidPolicy);
        for(unsigned i=0;i<word.count;++i) {
            if(out_.primitives>=limits_.primitives)return fail(Status::Partial,Reason::PrimitiveLimit);
            if(!primitive(who,word.steps[i]))return false;
            ++out_.primitives;
            if(out_.refutation_proved&&out_.refutation==Refutation::OwnerDied)
                return fail(Status::Partial,Reason::RequiredOwnerDied);
        }
        return true;
    }
    bool start(unsigned who) {
        auto& b=world().bodies[who];
        b.completed_turn=false;
        if(!b.alive) {
            if(b.respawn_turns<=0)return fail(Status::Unsupported,Reason::UnsupportedRespawn);
            b.m=b.a=b.c=false;return true;
        }
        if(b.shield_present&&b.shield_turns==0)remove_shield(b);
        if(b.stun_present&&b.stun_turns==0)b.stun_present=false;
        if(b.enhanced_present&&b.enhanced_turns==0)b.enhanced_present=false;
        b.walked=b.cast_used=b.exclusive_cast=false;b.provenance_known=true;
        b.m=b.a=b.c=!b.stun_present&&!b.channel_present;
        return true;
    }
    bool end(unsigned who) {
        auto& b=world().bodies[who];
        if(b.enhanced_present&&b.enhanced_turns<0)return fail(Status::Unsupported,Reason::UnknownEnhancedDuration);
        for(auto& cooldown:b.cooldown)if(cooldown>0)--cooldown;
        if(b.shield_present&&b.shield_turns>0)--b.shield_turns;
        if(b.stun_present&&b.stun_turns>0)--b.stun_turns;
        if(b.enhanced_present&&b.enhanced_turns>0)--b.enhanced_turns;
        b.m=b.a=b.c=false;b.completed_turn=true;
        if(!b.alive){if(b.respawn_turns>0)--b.respawn_turns;return true;}
        if(world().spawn_healing_known&&world().spawn_healing_fraction==0)return true;
        if(!callbacks_.spawn)return fail(Status::Unsupported,Reason::UnknownSpawn);
        const auto where=callbacks_.spawn(callbacks_.context,world(),b.id);
        if(where==Verdict::Blocked)return true;
        if(where!=Verdict::Allowed||!world().spawn_healing_known)return fail(Status::Unsupported,Reason::UnknownSpawn);
        heal(b,b.max_hp*world().spawn_healing_fraction);return true;
    }
    bool phase(int round) {
        if(out_.phases>=limits_.phases)return fail(Status::Partial,Reason::PhaseLimit);
        world().round=round;
        if(!callbacks_.phase)return fail(Status::Unsupported,Reason::UnknownWorldPhase);
        const auto batch=callbacks_.phase(callbacks_.context,world(),round);
        if(!batch.complete)return fail(Status::Unsupported,Reason::UnknownWorldPhase);
        if(batch.count>phase_effect_limit)return fail(Status::Invalid,Reason::InvalidPolicy);
        ++out_.phases;
        for(unsigned i=0;i<batch.count;++i) {
            const auto& effect=batch.effects[i];const int victim=at(world(),effect.target);
            if(victim<0||!world().bodies[static_cast<unsigned>(victim)].modeled||!finite(effect.amount)||effect.duration<0||effect.duration>10000
                ||static_cast<unsigned>(effect.kind)>static_cast<unsigned>(PhaseKind::Shield))return fail(Status::Invalid,Reason::InvalidPolicy);
            auto& b=world().bodies[static_cast<unsigned>(victim)];if(!b.alive)continue;
            switch(effect.kind) {
            // Phase damage is already source-scaled and target-mitigated; its
            // producer prices the actual ordered world/NPC hits exactly once.
            case PhaseKind::Damage:hit(world(),b,effect.amount);death_obligation();break;
            case PhaseKind::Heal:heal(b,effect.amount);break;
            case PhaseKind::Stun:
                if(effect.duration>1)return fail(Status::Unsupported,Reason::UnsupportedStun);
                stun(b,effect.duration);break;
            case PhaseKind::Shield:
                if(!shield(b,effect.amount,effect.duration,ShieldRule::OnlyAbsent))return fail(Status::Unsupported,Reason::OverlappingShield);
                break;
            }
            if(out_.refutation_proved&&out_.refutation==Refutation::OwnerDied)
                return fail(Status::Partial,Reason::RequiredOwnerDied);
        }
        return true;
    }
    void readiness(bool future) {
        if(out_.refutation_proved||world().round>deadline_)return;
        const auto& b=world().bodies[owner_];
        if(requirement_.require_future_usable&&(!future||!usable(b)))return;
        if(requirement_.require_alive&&!b.alive)return;
        if(b.hp<requirement_.minimum_hp){readiness_rejected_=true;return;}
        const auto view=observe(callbacks_,world(),b.id,inputs_[owner_]);
        const auto ready=requirement_.readiness?requirement_.readiness(requirement_.context,view):Verdict::Allowed;
        if(ready==Verdict::Unknown){out_.readiness_unknown=true;return;}
        if(ready==Verdict::Blocked){readiness_rejected_=true;return;}
        if(out_.service_ready_round<0)out_.service_ready_round=world().round;
    }
    void finish_complete() {
        out_.status=Status::Complete;out_.reason=Reason::None;out_.horizon_reached=true;
        out_.alive_at_horizon=world().bodies[owner_].alive;death_obligation();
        if(out_.refutation_proved)return;
        if(requirement_.require_alive&&!out_.alive_at_horizon){refute(Refutation::OwnerDied);return;}
        if(out_.service_ready_round>=0){out_.service=Service::Ready;return;}
        if(out_.readiness_unknown){out_.service=Service::Unknown;return;}
        refute(readiness_rejected_?Refutation::ReadinessRejected:Refutation::MissedUsableDeadline);
    }
public:
    Engine(const World& initial,const Scenario& scenario,const Callbacks& callbacks,const Requirement& requirement,Limits limits)
        :scenario_(scenario),callbacks_(callbacks),requirement_(requirement),limits_(limits) {
        out_.terminal=initial;out_.reply_id=scenario.reply_id;out_.revision=scenario.revision;out_.assumptions=scenario.assumptions;
    }
    Outcome execute(const Word& root) {
        if(!detail::world(world()))return out_;
        owner_=static_cast<unsigned>(at(world(),world().owner));deadline_=requirement_.deadline_round<0?scenario_.horizon_round:requirement_.deadline_round;
        if(limits_.slots>4096||limits_.primitives>16384||limits_.phases>1000){fail(Status::Invalid,Reason::InvalidLimits);return out_;}
        if(scenario_.schedule.size()>limits_.slots){fail(Status::Partial,Reason::SlotLimit);return out_;}
        if(!detail::schedule(world(),scenario_)){fail(Status::Invalid,Reason::InvalidScenario);return out_;}
        if(requirement_.owner!=world().owner||deadline_<world().round||deadline_>scenario_.horizon_round||!finite(requirement_.minimum_hp))
            {fail(Status::Invalid,Reason::InvalidRequirement);return out_;}
        for(unsigned i=0;i<world().count;++i) {
            const auto& b=world().bodies[i];
            if(!b.modeled){out_.excluded_mask|=1u<<i;if(!scenario_.allow_excluded_no_effect){fail(Status::Unsupported,Reason::UnsupportedBody);return out_;}}
            else if(!b.effects_closed||b.non_timed_restrictions||b.channel_present){fail(Status::Unsupported,Reason::UnknownEffects);return out_;}
        }
        death_obligation();
        if(out_.refutation_proved){fail(Status::Partial,Reason::RequiredOwnerDied);return out_;}
        readiness(false);
        if(!word(owner_,root)||!end(owner_))return out_;
        for(const auto& slot:scenario_.schedule) {
            if(out_.slots>=limits_.slots){fail(Status::Partial,Reason::SlotLimit);return out_;}
            if(slot.round!=world().round&&!phase(slot.round))return out_;
            const auto who=static_cast<unsigned>(at(world(),slot.actor));++out_.slots;
            auto& b=world().bodies[who];if(!b.modeled)continue;
            if(!slot.already_started&&!start(who))return out_;
            ++inputs_[who];
            if(who==owner_) {
                ++out_.scheduled_inputs;
                if(usable(b)){++out_.usable_inputs;if(out_.next_usable_round<0)out_.next_usable_round=world().round;}
                readiness(true);
            }
            if(b.alive&&!b.stun_present) {
                if(!callbacks_.policy){fail(Status::Unsupported,Reason::UnknownPolicy);return out_;}
                const auto observation=observe(callbacks_,world(),b.id,inputs_[who]);
                const auto next=callbacks_.policy(callbacks_.context,observation);
                if(!word(who,next))return out_;
            }
            if(!end(who))return out_;
        }
        finish_complete();return out_;
    }
};
} // namespace detail

inline Outcome run(const World& initial,const Word& root,const Scenario& scenario,
                   const Callbacks& callbacks,const Requirement& requirement,Limits limits) {
    return detail::Engine(initial,scenario,callbacks,requirement,limits).execute(root);
}
}

// Opt-in timed Recall model. Legacy namespace/types/Engine above are exact.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

// Finite ordered conditional encounter forecasts; never SDK authority or receipts.
namespace wb::capital::encounter::recall {
inline constexpr unsigned body_limit=6,word_limit=4,phase_effect_limit=24;
inline constexpr unsigned camp_target=1,enemy_target=2,ally_target=4,self_target=8,ground_target=16;
struct Point {int x{},y{};};
inline constexpr bool operator==(Point a,Point b){return a.x==b.x&&a.y==b.y;}
inline int distance(Point a,Point b){return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
enum class Kind {None,Reset,Damage,Area,Stun,Dash,Shield,Heal};
enum class ShieldRule {OnlyAbsent,Replace,Add,Maximum};
struct Ability {
    Kind kind{Kind::None};float range{},radius{},amount{};
    int max_cooldown{},duration{},stun_turns{},enhanced_turns{-1};
    unsigned target_mask{},affect_mask{enemy_target};
    bool excludes_walk{},enhanced_reset{};
    ShieldRule shield_rule{ShieldRule::OnlyAbsent};
};
enum class RecallKind {None,Recall};
struct RecallState {
    RecallKind kind{RecallKind::None};int remaining{},queried_start_ticks{-1};
    bool started_this_input{};
};
// In this opt-in model M/A/C are the underlying allowances/history. A known
// Recall gates their use separately through channel_present; a native "cannot
// act while recalling" result does not establish those allowances were spent.
// Initial/current states must be caller-qualified, not reconstructed from such
// a blocked-action observation. This does not alter the legacy Body above.
struct Body {
    int id{-1},team{-1};Point position{};
    float hp{},max_hp{},shield{},basic{},mitigation{1},basic_splash{},heart_bonus{};
    int move_range{},attack_range{},shield_turns{},stun_turns{},enhanced_turns{},enhanced_basic_stun{},respawn_turns{-1};
    std::array<int,2> cooldown{};std::array<Ability,2> abilities{};
    bool alive{},m{},a{},c{},walked{},cast_used{},exclusive_cast{},ranged{},stun_immune{},any_kill_refresh{},heart{};
    bool shield_present{},stun_present{},enhanced_present{},provenance_known{},effects_closed{};
    bool modeled{true};unsigned non_timed_restrictions{};bool channel_present{},completed_turn{};
    RecallState recall{};
};
// Each published Body pointer refers to ONE base subobject, not an element of
// a public Body array that permits legal adjacent indexing into hidden state.
// This remains a trusted C++ callback contract, not a malicious-code sandbox.
struct BodySlot final:Body {
    BodySlot()=default;
    BodySlot(const Body& value):Body(value){}
    BodySlot& operator=(const Body& value){static_cast<Body&>(*this)=value;return *this;}
};
static_assert(sizeof(BodySlot)==sizeof(Body));
struct World {
    std::array<BodySlot,body_limit> bodies{};unsigned count{};int round{-1},owner{-1};
    std::uint64_t revision{};
    int respawn_ticks{-1};float spawn_healing_fraction{};bool spawn_healing_known{};
};
enum class PrimitiveKind {Move,Basic,Cast,Recall};
struct Primitive {PrimitiveKind kind{PrimitiveKind::Basic};int target{-1},slot{-1};Point destination{};};
struct Word {std::array<Primitive,word_limit> steps{};unsigned count{};bool complete{};};
struct Slot {
    int round{-1},actor{-1};
    // A CURRENT-round captured action phase may be resumed without laundering
    // unknown walk/MAC history into a fresh input. Future rounds always start.
    bool already_started{};
};
struct Scenario {
    std::uint64_t reply_id{},revision{},assumptions{};
    int horizon_round{-1},current_slot{-1},starting_team{-1};
    std::span<const Slot> schedule{};
    bool current_remaining_complete{},future_rounds_complete{},allow_excluded_no_effect{};
    // Fully shield-absorbed positive hits are not an established engine Recall
    // interrupt rule. Explicit alternatives are conditional scenario branches,
    // never an engine fact or a certificate that a shield protects a channel.
    enum class ShieldedRecallHit {Unknown,AssumeInterrupt,AssumeContinue};
    ShieldedRecallHit shielded_recall_hit{ShieldedRecallHit::Unknown};
    bool blocked_recall_wait_declared{};
};
enum class Verdict {Allowed,Blocked,Unknown};
struct ObservedBody {int id{-1},team{-1};bool visible{},visibility_known{};const Body* body{};};
struct Observation {
    int observer{-1},round{-1};unsigned scheduled_input{};
    std::array<ObservedBody,body_limit> bodies{};unsigned count{};
    const Body* find(int id) const {
        for(unsigned i=0;i<count;++i)if(bodies[i].id==id&&bodies[i].visible)return bodies[i].body;
        return nullptr;
    }
};
enum class PhaseKind {Damage,Heal,Stun,Shield};
struct PhaseEffect {PhaseKind kind{PhaseKind::Damage};int source{-1},target{-1};float amount{};int duration{};};
struct Phase {std::array<PhaseEffect,phase_effect_limit> effects{};unsigned count{};bool complete{};};
using GeometryFn=Verdict(*)(void*,const World&,int,const Primitive&);
using VisibilityFn=Verdict(*)(void*,const World&,int,int);
using PolicyFn=Word(*)(void*,const Observation&);
using PhaseFn=Phase(*)(void*,const World&,int);
using SpawnFn=Verdict(*)(void*,const World&,int);
using ReadinessFn=Verdict(*)(void*,const Observation&);
struct Arrival {Verdict verdict{Verdict::Unknown};Point point{};};
// Allowed is a conditional landing/occupancy premise for this same modeled
// scenario. Unknown occupants/spawn choice stay Unknown, never a convenient
// free cell. SpawnFn independently validates the candidate as the actor's own
// spawn before pose commit; arrival itself neither heals nor clears effects.
using ArrivalFn=Arrival(*)(void*,const World&,int);
using RecallStartFn=Verdict(*)(void*,const World&,int);
struct Callbacks {
    void* context{};GeometryFn geometry{};VisibilityFn visibility{};
    PolicyFn policy{};PhaseFn phase{};SpawnFn spawn{};
    // Explicit conditional start-legality premise. No can_recall SDK predicate
    // or unused-M/A/C inference exists here; Native must still accept/read back.
    RecallStartFn recall_start{};ArrivalFn arrival{};
};
struct Requirement {
    int owner{-1},deadline_round{-1};float minimum_hp{};
    bool require_alive{true},require_future_usable{true},survive_entire_horizon{true};
    ReadinessFn readiness{};void* context{};
};
struct Limits {unsigned slots{96},primitives{384},phases{16};};
enum class Status {Complete,Partial,Invalid,Unsupported};
enum class Service {Ready,Refuted,Unknown};
enum class Reason {
    None,InvalidWorld,InvalidScenario,InvalidRequirement,InvalidLimits,InvalidPolicy,
    UnsupportedBody,UnknownEffects,UnknownGeometry,UnknownVisibility,UnknownPolicy,
    UnknownWorldPhase,UnknownSpawn,UnknownEnhancedDuration,OverlappingShield,
    UnsupportedStun,UnsupportedRespawn,IllegalPrimitive,Resource,Target,Range,
    WalkingExcluded,SlotLimit,PrimitiveLimit,PhaseLimit,RequiredOwnerDied,NeedNomination,
    UnknownRecallStart,IllegalRecallStart,UnknownRecallDuration,UnknownRecallArrival,
    RecallLandingBlocked,UnknownRecallInterruption
};
enum class Refutation {None,OwnerDied,MissedUsableDeadline,ReadinessRejected};
struct Outcome {
    Status status{Status::Invalid};Reason reason{Reason::InvalidWorld};
    Service service{Service::Unknown};Refutation refutation{Refutation::None};
    std::uint64_t reply_id{},revision{},assumptions{};World terminal{};
    unsigned slots{},primitives{},phases{},scheduled_inputs{},usable_inputs{},excluded_mask{};
    int next_usable_round{-1},service_ready_round{-1},refutation_round{-1};
    bool horizon_reached{},alive_at_horizon{},refutation_proved{},readiness_unknown{};
    bool modeled_only{true};
    unsigned recall_starts{},recall_arrivals{},recall_interruptions{},recall_blocked_arrivals{};
    bool recall_shield_assumption_used{};
};

// Callbacks receive no private gold. Hidden policy observations carry identity
// and team only; even their modeled future positions have no borrowed pointer.
// Visible Body pointers are borrowed ONLY for this synchronous callback: never
// index/arithmetic a Body pointer or retain it across callback return, a model effect or an SDK
// invalidation. A policy must return incomplete when selection depends on an
// unknown visibility predicate. Committed words independent of that predicate
// remain possible. Legal geometry is a separate pure callback.
// Shield overlap rules other than OnlyAbsent need an admitted engine contract.
// Zero-timer buffs keep explicit presence until THAT body's owner start.
inline Outcome run(const World& initial,const Word& root,const Scenario& scenario,
                   const Callbacks& callbacks,const Requirement& requirement,Limits limits={});

namespace detail {
inline bool finite(float x){return std::isfinite(x)&&x>=0&&x<=1000000000.f;}
inline bool point(Point p){return p.x>=0&&p.y>=0&&p.x<=100000&&p.y<=100000;}
inline int at(const World& w,int id){for(unsigned i=0;i<w.count;++i)if(w.bodies[i].id==id)return static_cast<int>(i);return -1;}
inline bool team(int t){return t==0||t==1;}
inline bool ability(const Ability& a) {
    return static_cast<unsigned>(a.kind)<=static_cast<unsigned>(Kind::Heal)&&finite(a.range)&&finite(a.radius)&&finite(a.amount)
        &&a.range<=1000&&a.radius<=1000&&a.max_cooldown>=0&&a.max_cooldown<=10000&&a.duration>=0&&a.duration<=10000
        &&a.stun_turns>=0&&a.stun_turns<=1&&a.enhanced_turns>=-1&&a.enhanced_turns<=10000
        &&!(a.target_mask&~31u)&&!(a.affect_mask&~31u)&&static_cast<unsigned>(a.shield_rule)<=static_cast<unsigned>(ShieldRule::Maximum);
}
inline bool body(const Body& b) {
    if(b.id<0||!team(b.team))return false;
    if(!b.modeled)return true;
    if((!point(b.position)&&!(!b.alive&&b.position==Point{-1,-1}))||!finite(b.hp)||!finite(b.max_hp)||b.max_hp<=0||b.hp>b.max_hp
        ||b.alive!=(b.hp>0)||!finite(b.shield)||!finite(b.basic)||!finite(b.mitigation)||b.mitigation>1
        ||!finite(b.basic_splash)||b.basic_splash>1000||!finite(b.heart_bonus)||b.heart_bonus>10
        ||b.move_range<0||b.move_range>100||b.attack_range<0||b.attack_range>100
        ||b.shield_turns<0||b.shield_turns>10000||b.stun_turns<0||b.stun_turns>1
        ||b.enhanced_turns<0||b.enhanced_turns>10000||b.enhanced_basic_stun<0||b.enhanced_basic_stun>1
        ||b.respawn_turns< -1||b.respawn_turns>10000
        ||(!b.shield_present&&b.shield!=0)||(!b.stun_present&&b.stun_turns!=0)
        ||(!b.enhanced_present&&b.enhanced_turns!=0)||(b.c&&b.cast_used)
        ||static_cast<unsigned>(b.recall.kind)>static_cast<unsigned>(RecallKind::Recall)
        ||b.recall.remaining<0||b.recall.remaining>10000||b.recall.queried_start_ticks< -1||b.recall.queried_start_ticks>10000
        ||(b.recall.kind==RecallKind::None&&b.recall.remaining!=0)
        ||(b.recall.kind==RecallKind::Recall&&(!b.alive||!b.channel_present||b.recall.queried_start_ticks<=0||
            b.recall.remaining>b.recall.queried_start_ticks)))return false;
    for(unsigned i=0;i<2;++i)if(b.cooldown[i]<0||b.cooldown[i]>10000||!ability(b.abilities[i]))return false;
    return true;
}
inline bool world(const World& w) {
    if(!w.count||w.count>body_limit||w.round<0||w.round>1000000000||!w.revision||w.respawn_ticks< -1||w.respawn_ticks>10000
        ||!finite(w.spawn_healing_fraction)||w.spawn_healing_fraction>1||at(w,w.owner)<0)return false;
    for(unsigned i=0;i<w.count;++i) {
        if(!body(w.bodies[i]))return false;
        for(unsigned j=0;j<i;++j)if(w.bodies[i].id==w.bodies[j].id)return false;
    }
    const auto& owner=w.bodies[static_cast<unsigned>(at(w,w.owner))];return owner.modeled&&!owner.completed_turn;
}
inline unsigned team_count(const World& w,int side){unsigned n=0;for(unsigned i=0;i<w.count;++i)n+=w.bodies[i].team==side?1u:0u;return n;}
inline int expected_team(const World& w,int starter,unsigned rank) {
    const auto first=team_count(w,starter),second=team_count(w,1-starter),paired=std::min(first,second);
    if(rank<2u*paired)return rank%2?1-starter:starter;
    return first>second?starter:1-starter;
}
inline bool schedule(const World& w,const Scenario& s) {
    if(!s.reply_id||!s.revision||!s.assumptions||s.horizon_round<w.round||s.horizon_round>w.round+10000
        ||s.current_slot<0||static_cast<unsigned>(s.current_slot)>=w.count||!team(s.starting_team)
        ||!s.current_remaining_complete||!s.future_rounds_complete)return false;
    const auto root=static_cast<unsigned>(at(w,w.owner));
    if(w.bodies[root].team!=expected_team(w,s.starting_team,static_cast<unsigned>(s.current_slot)))return false;
    const std::uint64_t needed=std::uint64_t(w.count)-static_cast<unsigned>(s.current_slot)-1u
        +std::uint64_t(s.horizon_round-w.round)*w.count;
    if(needed!=s.schedule.size())return false;
    unsigned cursor=0;
    for(int round=w.round;round<=s.horizon_round;++round) {
        const unsigned begin=round==w.round?static_cast<unsigned>(s.current_slot)+1u:0u;
        const int starter=s.starting_team^((round-w.round)&1);unsigned seen=round==w.round?(1u<<root):0u;
        for(unsigned rank=begin;rank<w.count;++rank) {
            const auto& slot=s.schedule[cursor++];const int who=at(w,slot.actor);
            if(slot.round!=round||who<0||(seen&(1u<<static_cast<unsigned>(who)))||(slot.already_started&&round!=w.round))return false;
            seen|=1u<<static_cast<unsigned>(who);
            if(round==w.round&&w.bodies[static_cast<unsigned>(who)].modeled&&w.bodies[static_cast<unsigned>(who)].completed_turn)return false;
            if(w.bodies[static_cast<unsigned>(who)].team!=expected_team(w,starter,rank))return false;
        }
    }
    return true;
}
inline float factor(const Body& b){return b.heart?std::clamp(1.f+b.heart_bonus*(1.f-b.hp/b.max_hp),1.f,1.f+b.heart_bonus):1.f;}
inline bool usable(const Body& b){return b.alive&&!b.stun_present&&!b.channel_present&&!b.completed_turn&&(b.m||b.a||b.c);}
inline void remove_shield(Body& b){b.shield_present=false;b.shield=0;b.shield_turns=0;}
inline bool cancel_recall(Body& b) {
    if(b.recall.kind!=RecallKind::Recall)return false;
    b.recall.kind=RecallKind::None;b.recall.remaining=0;b.channel_present=false;
    // Starting/interrupting a channel does not refund or spend underlying MAC,
    // cast debt, or ordinary-walk history. END/START retain their own semantics.
    return true;
}
inline void die(World& w,Body& b) {
    b.hp=0;b.alive=false;b.m=b.a=b.c=false;b.cast_used=false;b.exclusive_cast=false;
    remove_shield(b);b.stun_present=b.enhanced_present=false;b.stun_turns=b.enhanced_turns=0;b.respawn_turns=w.respawn_ticks;
    cancel_recall(b);
}
inline void hit(World& w,Body& b,float amount) {
    const float absorbed=std::min(b.shield,amount);b.shield=std::max(0.f,b.shield-amount);
    b.hp=std::max(0.f,b.hp-std::max(0.f,amount-absorbed));
    if(b.shield_present&&b.shield<=0)remove_shield(b);
    if(b.hp<=0)die(w,b);
}
inline void stun(Body& b,int turns) {
    if(!b.alive||b.stun_immune||turns<=0)return;
    b.stun_present=true;b.stun_turns=std::max(b.stun_turns,turns);b.m=b.a=b.c=false;
    cancel_recall(b);
}
inline void heal(Body& b,float amount){if(b.alive)b.hp=std::min(b.max_hp,b.hp+amount);}
inline bool shield(Body& b,float amount,int turns,ShieldRule rule) {
    if(!b.alive)return true;
    if(b.shield_present&&rule==ShieldRule::OnlyAbsent)return false;
    float value=amount;
    if(b.shield_present&&rule==ShieldRule::Add)value+=b.shield;
    if(b.shield_present&&rule==ShieldRule::Maximum)value=std::max(value,b.shield);
    if(!finite(value))return false;
    b.shield_present=true;b.shield=value;b.shield_turns=turns;return true;
}
inline Verdict visibility(const Callbacks& c,const World& w,int observer,int target) {
    if(observer==target)return Verdict::Allowed;
    return c.visibility?c.visibility(c.context,w,observer,target):Verdict::Unknown;
}
inline Observation observe(const Callbacks& c,const World& w,int observer,unsigned input) {
    Observation out;out.observer=observer;out.round=w.round;out.scheduled_input=input;out.count=w.count;
    for(unsigned i=0;i<w.count;++i) {
        const auto& b=w.bodies[i];auto& o=out.bodies[i];o.id=b.id;o.team=b.team;
        const auto v=b.modeled?visibility(c,w,observer,b.id):Verdict::Unknown;
        o.visibility_known=b.modeled&&v!=Verdict::Unknown;o.visible=b.modeled&&v==Verdict::Allowed;
        if(o.visible)o.body=&b;
    }
    return out;
}
} // namespace detail

namespace detail {
class Engine {
    Outcome out_{};const Scenario& scenario_;const Callbacks& callbacks_;const Requirement& requirement_;Limits limits_;
    unsigned owner_{};int deadline_{};std::array<unsigned,body_limit> inputs_{};
    bool readiness_rejected_{};
    World& world(){return out_.terminal;}
    const World& world() const{return out_.terminal;}
    bool fail(Status status,Reason reason){out_.status=status;out_.reason=reason;return false;}
    void refute(Refutation reason) {
        if(out_.refutation_proved)return;
        out_.service=Service::Refuted;out_.refutation=reason;out_.refutation_round=world().round;out_.refutation_proved=true;
    }
    void death_obligation() {
        if(requirement_.survive_entire_horizon&&!world().bodies[owner_].alive)refute(Refutation::OwnerDied);
    }
    bool geometry(int actor,const Primitive& primitive) {
        if(!callbacks_.geometry)return fail(Status::Unsupported,Reason::UnknownGeometry);
        const auto answer=callbacks_.geometry(callbacks_.context,world(),actor,primitive);
        if(answer==Verdict::Unknown)return fail(Status::Unsupported,Reason::UnknownGeometry);
        if(answer!=Verdict::Allowed)return fail(Status::Invalid,Reason::IllegalPrimitive);
        return true;
    }
    bool visible(int actor,int target) {
        const auto answer=visibility(callbacks_,world(),actor,target);
        if(answer==Verdict::Unknown)return fail(Status::Unsupported,Reason::UnknownVisibility);
        if(answer!=Verdict::Allowed)return fail(Status::Invalid,Reason::Target);
        return true;
    }
    int target(int id) {
        const int result=at(world(),id);
        if(result<0){fail(Status::Invalid,Reason::Target);return -1;}
        if(!world().bodies[static_cast<unsigned>(result)].modeled){fail(Status::Unsupported,Reason::UnsupportedBody);return -1;}
        if(!world().bodies[static_cast<unsigned>(result)].alive){fail(Status::Partial,Reason::NeedNomination);return -1;}
        return result;
    }
    bool category(const Body& source,const Body& victim,unsigned mask) const {
        const unsigned bit=source.id==victim.id?self_target:source.team==victim.team?ally_target:enemy_target;
        return (mask&bit)!=0;
    }
    bool hit_body(Body& b,float amount,bool certain_stun=false) {
        if(b.recall.kind==RecallKind::Recall&&amount>0) {
            if(certain_stun||amount>b.shield) {cancel_recall(b);++out_.recall_interruptions;}
            else {
                const auto branch=scenario_.shielded_recall_hit;
                if(branch==Scenario::ShieldedRecallHit::Unknown)return fail(Status::Unsupported,Reason::UnknownRecallInterruption);
                out_.recall_shield_assumption_used=true;
                if(branch==Scenario::ShieldedRecallHit::AssumeInterrupt){cancel_recall(b);++out_.recall_interruptions;}
            }
        }
        hit(world(),b,amount);return true;
    }
    bool apply_stun(Body& b,int turns) {
        const bool recalling=b.recall.kind==RecallKind::Recall;
        stun(b,turns);
        if(recalling&&b.recall.kind==RecallKind::None)++out_.recall_interruptions;
        return true;
    }
    bool damage(unsigned source,unsigned victim,float raw,int stun_turns) {
        auto& w=world();auto& target_body=w.bodies[victim];const bool living=target_body.alive;
        if(!hit_body(target_body,raw*target_body.mitigation,stun_turns>0&&!target_body.stun_immune))return false;
        if(target_body.alive)apply_stun(target_body,stun_turns);
        if(living&&!target_body.alive&&w.bodies[source].any_kill_refresh)w.bodies[source].cooldown={0,0};
        death_obligation();return true;
    }
    bool area(unsigned source,Point center,float radius,float amount,int stun_turns,unsigned mask) {
        const auto side=world().bodies[source].team;
        for(unsigned i=0;i<world().count;++i) {
            const auto& victim=world().bodies[i];
            if(!victim.modeled||!victim.alive||distance(center,victim.position)>radius)continue;
            const unsigned bit=i==source?self_target:victim.team==side?ally_target:enemy_target;
            if((mask&bit)&&!damage(source,i,amount,stun_turns))return false;
        }
        return true;
    }
    bool primitive(unsigned who,const Primitive& primitive) {
        auto& b=world().bodies[who];
        if(static_cast<unsigned>(primitive.kind)>static_cast<unsigned>(PrimitiveKind::Recall))return fail(Status::Invalid,Reason::IllegalPrimitive);
        if(!b.alive||b.stun_present||b.channel_present||b.completed_turn)return fail(Status::Invalid,Reason::Resource);
        if(primitive.kind==PrimitiveKind::Recall) {
            if(b.recall.started_this_input)return fail(Status::Invalid,Reason::IllegalRecallStart);
            if(b.recall.queried_start_ticks<=0)return fail(Status::Unsupported,Reason::UnknownRecallDuration);
            if(!callbacks_.recall_start)return fail(Status::Unsupported,Reason::UnknownRecallStart);
            const auto allowed=callbacks_.recall_start(callbacks_.context,world(),b.id);
            if(allowed==Verdict::Unknown)return fail(Status::Unsupported,Reason::UnknownRecallStart);
            if(allowed!=Verdict::Allowed)return fail(Status::Invalid,Reason::IllegalRecallStart);
            b.recall.kind=RecallKind::Recall;b.recall.remaining=b.recall.queried_start_ticks;
            b.recall.started_this_input=true;b.channel_present=true;++out_.recall_starts;
            // Keep pre-channel M/A/C and walk/cast history distinct from the
            // channel-imposed unusability. No later physical suffix is legal.
            return true;
        }
        if(primitive.kind==PrimitiveKind::Move) {
            if(!b.m||b.exclusive_cast)return fail(Status::Invalid,Reason::Resource);
            if(!point(primitive.destination)||primitive.destination==b.position||distance(b.position,primitive.destination)>b.move_range)
                return fail(Status::Invalid,Reason::Range);
            if(!geometry(b.id,primitive))return false;
            b.position=primitive.destination;b.m=false;b.walked=true;b.provenance_known=true;if(b.ranged)b.a=false;
            return true;
        }
        if(primitive.kind==PrimitiveKind::Basic) {
            if(!b.a)return fail(Status::Invalid,Reason::Resource);
            const int target_index=target(primitive.target);if(target_index<0)return false;
            const auto victim=static_cast<unsigned>(target_index);const auto target_position=world().bodies[victim].position;
            if(b.team==world().bodies[victim].team)return fail(Status::Invalid,Reason::Target);
            if(distance(b.position,target_position)>b.attack_range)return fail(Status::Invalid,Reason::Range);
            if(!visible(b.id,primitive.target)||!geometry(b.id,primitive))return false;
            const float raw=b.basic*factor(b);const int stun_turns=b.enhanced_present?b.enhanced_basic_stun:0;
            b.a=false;if(b.ranged)b.m=false;b.enhanced_present=false;b.enhanced_turns=0;
            if(b.basic_splash>0)return area(who,target_position,b.basic_splash,raw,stun_turns,enemy_target);
            return damage(who,victim,raw,stun_turns);
        }
        if(!b.c||b.cast_used||primitive.slot<0||primitive.slot>=2)return fail(Status::Invalid,Reason::Resource);
        const auto slot=static_cast<unsigned>(primitive.slot);const auto ability=b.abilities[slot];
        if(ability.kind==Kind::None||b.cooldown[slot]>0)return fail(Status::Invalid,Reason::Resource);
        if(ability.excludes_walk&&(!b.provenance_known||b.walked))return fail(b.provenance_known?Status::Invalid:Status::Unsupported,Reason::WalkingExcluded);
        int victim=-1;Point center=primitive.destination;
        if(ability.kind==Kind::Reset||ability.kind==Kind::Shield) {
            if(!(ability.target_mask&self_target)||(primitive.target>=0&&primitive.target!=b.id))return fail(Status::Invalid,Reason::Target);
            if(ability.kind==Kind::Shield&&b.shield_present&&ability.shield_rule==ShieldRule::OnlyAbsent)
                return fail(Status::Unsupported,Reason::OverlappingShield);
        } else if(ability.kind==Kind::Dash) {
            if(!(ability.target_mask&ground_target)||!point(center)||center==b.position||distance(b.position,center)>ability.range)
                return fail(Status::Invalid,Reason::Range);
            if(!geometry(b.id,primitive))return false;
        } else if(primitive.target>=0) {
            victim=target(primitive.target);if(victim<0)return false;const auto& target_body=world().bodies[static_cast<unsigned>(victim)];
            if(!category(b,target_body,ability.target_mask))return fail(Status::Invalid,Reason::Target);
            if(distance(b.position,target_body.position)>ability.range)return fail(Status::Invalid,Reason::Range);
            if(!visible(b.id,primitive.target)||!geometry(b.id,primitive))return false;
            center=target_body.position;
        } else {
            if(ability.kind==Kind::Heal||!(ability.target_mask&ground_target)||!point(center)||distance(b.position,center)>ability.range)
                return fail(Status::Invalid,Reason::Target);
            if(!geometry(b.id,primitive))return false;
        }
        b.c=false;b.cast_used=true;b.cooldown[slot]=ability.max_cooldown;
        if(ability.excludes_walk){b.exclusive_cast=true;b.m=false;}
        switch(ability.kind) {
        case Kind::Reset:
            b.a=true;
            if(ability.enhanced_reset){b.enhanced_present=true;b.enhanced_turns=ability.enhanced_turns;}
            break;
        case Kind::Shield:
            if(!shield(b,ability.amount,ability.duration,ability.shield_rule))return fail(Status::Unsupported,Reason::OverlappingShield);
            break;
        case Kind::Heal:heal(world().bodies[static_cast<unsigned>(victim)],ability.amount);break;
        case Kind::Dash:
            b.position=center;return area(who,center,ability.radius,ability.amount*factor(b),ability.stun_turns,ability.affect_mask);
        case Kind::Damage:case Kind::Area:case Kind::Stun:
            if(ability.kind==Kind::Area||victim<0)return area(who,center,ability.radius,ability.amount*factor(b),ability.stun_turns,ability.affect_mask);
            return damage(who,static_cast<unsigned>(victim),ability.amount*factor(b),ability.stun_turns);
        default:return fail(Status::Invalid,Reason::IllegalPrimitive);
        }
        return true;
    }
    bool word(unsigned who,const Word& word) {
        if(!word.complete)return fail(Status::Unsupported,Reason::UnknownPolicy);
        if(word.count>word_limit)return fail(Status::Invalid,Reason::InvalidPolicy);
        for(unsigned i=0;i<word.count;++i) {
            if(out_.primitives>=limits_.primitives)return fail(Status::Partial,Reason::PrimitiveLimit);
            if(!primitive(who,word.steps[i]))return false;
            ++out_.primitives;
            if(out_.refutation_proved&&out_.refutation==Refutation::OwnerDied)
                return fail(Status::Partial,Reason::RequiredOwnerDied);
        }
        return true;
    }
    bool start(unsigned who) {
        auto& b=world().bodies[who];
        b.completed_turn=false;
        if(!b.alive) {
            if(b.respawn_turns<=0)return fail(Status::Unsupported,Reason::UnsupportedRespawn);
            b.m=b.a=b.c=false;return true;
        }
        if(b.shield_present&&b.shield_turns==0)remove_shield(b);
        if(b.stun_present&&b.stun_turns==0)b.stun_present=false;
        if(b.enhanced_present&&b.enhanced_turns==0)b.enhanced_present=false;
        b.recall.started_this_input=false;
        if(b.recall.kind==RecallKind::Recall&&b.recall.remaining==0) {
            if(!callbacks_.arrival)return fail(Status::Unsupported,Reason::UnknownRecallArrival);
            const auto arrival=callbacks_.arrival(callbacks_.context,world(),b.id);
            if(arrival.verdict==Verdict::Unknown)return fail(Status::Unsupported,Reason::UnknownRecallArrival);
            if(arrival.verdict!=Verdict::Allowed&&arrival.verdict!=Verdict::Blocked)
                return fail(Status::Unsupported,Reason::UnknownRecallArrival);
            bool blocked=arrival.verdict==Verdict::Blocked;
            if(!blocked) {
                if(!point(arrival.point))return fail(Status::Unsupported,Reason::UnknownRecallArrival);
                for(unsigned i=0;i<world().count;++i)if(i!=who&&world().bodies[i].modeled&&world().bodies[i].alive&&
                    world().bodies[i].position==arrival.point)blocked=true;
            }
            if(blocked) {
                ++out_.recall_blocked_arrivals;
                if(!scenario_.blocked_recall_wait_declared)return fail(Status::Unsupported,Reason::RecallLandingBlocked);
            } else {
                if(!callbacks_.spawn)return fail(Status::Unsupported,Reason::UnknownSpawn);
                World candidate=world();candidate.bodies[who].position=arrival.point;
                const auto own_spawn=callbacks_.spawn(callbacks_.context,candidate,b.id);
                if(own_spawn!=Verdict::Allowed)return fail(Status::Unsupported,Reason::UnknownSpawn);
                b.position=arrival.point;cancel_recall(b);++out_.recall_arrivals;
            }
        }
        b.walked=b.cast_used=b.exclusive_cast=false;b.provenance_known=true;
        b.m=b.a=b.c=!b.stun_present&&(!b.channel_present||b.recall.kind==RecallKind::Recall);
        return true;
    }
    bool end(unsigned who) {
        auto& b=world().bodies[who];
        if(b.enhanced_present&&b.enhanced_turns<0)return fail(Status::Unsupported,Reason::UnknownEnhancedDuration);
        for(auto& cooldown:b.cooldown)if(cooldown>0)--cooldown;
        if(b.shield_present&&b.shield_turns>0)--b.shield_turns;
        if(b.stun_present&&b.stun_turns>0)--b.stun_turns;
        if(b.enhanced_present&&b.enhanced_turns>0)--b.enhanced_turns;
        if(b.recall.kind==RecallKind::Recall&&b.recall.remaining>0)--b.recall.remaining;
        b.m=b.a=b.c=false;b.completed_turn=true;
        if(!b.alive){if(b.respawn_turns>0)--b.respawn_turns;return true;}
        if(world().spawn_healing_known&&world().spawn_healing_fraction==0)return true;
        if(!callbacks_.spawn)return fail(Status::Unsupported,Reason::UnknownSpawn);
        const auto where=callbacks_.spawn(callbacks_.context,world(),b.id);
        if(where==Verdict::Blocked)return true;
        if(where!=Verdict::Allowed||!world().spawn_healing_known)return fail(Status::Unsupported,Reason::UnknownSpawn);
        heal(b,b.max_hp*world().spawn_healing_fraction);return true;
    }
    bool phase(int round) {
        if(out_.phases>=limits_.phases)return fail(Status::Partial,Reason::PhaseLimit);
        world().round=round;
        if(!callbacks_.phase)return fail(Status::Unsupported,Reason::UnknownWorldPhase);
        const auto batch=callbacks_.phase(callbacks_.context,world(),round);
        if(!batch.complete)return fail(Status::Unsupported,Reason::UnknownWorldPhase);
        if(batch.count>phase_effect_limit)return fail(Status::Invalid,Reason::InvalidPolicy);
        ++out_.phases;
        for(unsigned i=0;i<batch.count;++i) {
            const auto& effect=batch.effects[i];const int victim=at(world(),effect.target);
            if(victim<0||!world().bodies[static_cast<unsigned>(victim)].modeled||!finite(effect.amount)||effect.duration<0||effect.duration>10000
                ||static_cast<unsigned>(effect.kind)>static_cast<unsigned>(PhaseKind::Shield))return fail(Status::Invalid,Reason::InvalidPolicy);
            auto& b=world().bodies[static_cast<unsigned>(victim)];if(!b.alive)continue;
            switch(effect.kind) {
            // Phase damage is already source-scaled and target-mitigated; its
            // producer prices the actual ordered world/NPC hits exactly once.
            case PhaseKind::Damage:if(!hit_body(b,effect.amount))return false;death_obligation();break;
            case PhaseKind::Heal:heal(b,effect.amount);break;
            case PhaseKind::Stun:
                if(effect.duration>1)return fail(Status::Unsupported,Reason::UnsupportedStun);
                apply_stun(b,effect.duration);break;
            case PhaseKind::Shield:
                if(!shield(b,effect.amount,effect.duration,ShieldRule::OnlyAbsent))return fail(Status::Unsupported,Reason::OverlappingShield);
                break;
            }
            if(out_.refutation_proved&&out_.refutation==Refutation::OwnerDied)
                return fail(Status::Partial,Reason::RequiredOwnerDied);
        }
        return true;
    }
    void readiness(bool future) {
        if(out_.refutation_proved||world().round>deadline_)return;
        const auto& b=world().bodies[owner_];
        if(requirement_.require_future_usable&&(!future||!usable(b)))return;
        if(requirement_.require_alive&&!b.alive)return;
        if(b.hp<requirement_.minimum_hp){readiness_rejected_=true;return;}
        const auto view=observe(callbacks_,world(),b.id,inputs_[owner_]);
        const auto ready=requirement_.readiness?requirement_.readiness(requirement_.context,view):Verdict::Allowed;
        if(ready==Verdict::Unknown){out_.readiness_unknown=true;return;}
        if(ready==Verdict::Blocked){readiness_rejected_=true;return;}
        if(out_.service_ready_round<0)out_.service_ready_round=world().round;
    }
    void finish_complete() {
        out_.status=Status::Complete;out_.reason=Reason::None;out_.horizon_reached=true;
        out_.alive_at_horizon=world().bodies[owner_].alive;death_obligation();
        if(out_.refutation_proved)return;
        if(requirement_.require_alive&&!out_.alive_at_horizon){refute(Refutation::OwnerDied);return;}
        if(out_.service_ready_round>=0){out_.service=Service::Ready;return;}
        if(out_.readiness_unknown){out_.service=Service::Unknown;return;}
        refute(readiness_rejected_?Refutation::ReadinessRejected:Refutation::MissedUsableDeadline);
    }
public:
    Engine(const World& initial,const Scenario& scenario,const Callbacks& callbacks,const Requirement& requirement,Limits limits)
        :scenario_(scenario),callbacks_(callbacks),requirement_(requirement),limits_(limits) {
        out_.terminal=initial;out_.reply_id=scenario.reply_id;out_.revision=scenario.revision;out_.assumptions=scenario.assumptions;
    }
    Outcome execute(const Word& root) {
        if(!detail::world(world()))return out_;
        owner_=static_cast<unsigned>(at(world(),world().owner));deadline_=requirement_.deadline_round<0?scenario_.horizon_round:requirement_.deadline_round;
        if(limits_.slots>4096||limits_.primitives>16384||limits_.phases>1000){fail(Status::Invalid,Reason::InvalidLimits);return out_;}
        if(scenario_.schedule.size()>limits_.slots){fail(Status::Partial,Reason::SlotLimit);return out_;}
        if(!detail::schedule(world(),scenario_)){fail(Status::Invalid,Reason::InvalidScenario);return out_;}
        if(requirement_.owner!=world().owner||deadline_<world().round||deadline_>scenario_.horizon_round||!finite(requirement_.minimum_hp))
            {fail(Status::Invalid,Reason::InvalidRequirement);return out_;}
        for(unsigned i=0;i<world().count;++i) {
            const auto& b=world().bodies[i];
            if(!b.modeled){out_.excluded_mask|=1u<<i;if(!scenario_.allow_excluded_no_effect){fail(Status::Unsupported,Reason::UnsupportedBody);return out_;}}
            else if(!b.effects_closed||b.non_timed_restrictions||
                (b.channel_present&&b.recall.kind!=RecallKind::Recall)){fail(Status::Unsupported,Reason::UnknownEffects);return out_;}
            if(b.modeled&&b.recall.kind==RecallKind::Recall&&b.recall.remaining==0&&!scenario_.blocked_recall_wait_declared) {
                // Root and explicitly resumed current slots have already had
                // START. An unresolved zero channel there needs the separate
                // blocked-arrival premise, not a newly invented extra wait.
                bool already_started=i==owner_;
                for(const auto& slot:scenario_.schedule)if(slot.round==world().round&&slot.actor==b.id)
                    {already_started=slot.already_started;break;}
                if(already_started){fail(Status::Unsupported,Reason::UnknownRecallArrival);return out_;}
            }
        }
        if(static_cast<unsigned>(scenario_.shielded_recall_hit)>static_cast<unsigned>(Scenario::ShieldedRecallHit::AssumeContinue))
            {fail(Status::Invalid,Reason::InvalidScenario);return out_;}
        death_obligation();
        if(out_.refutation_proved){fail(Status::Partial,Reason::RequiredOwnerDied);return out_;}
        readiness(false);
        if(!word(owner_,root)||!end(owner_))return out_;
        for(const auto& slot:scenario_.schedule) {
            if(out_.slots>=limits_.slots){fail(Status::Partial,Reason::SlotLimit);return out_;}
            if(slot.round!=world().round&&!phase(slot.round))return out_;
            const auto who=static_cast<unsigned>(at(world(),slot.actor));++out_.slots;
            auto& b=world().bodies[who];if(!b.modeled)continue;
            if(!slot.already_started&&!start(who))return out_;
            ++inputs_[who];
            if(who==owner_) {
                ++out_.scheduled_inputs;
                if(usable(b)){++out_.usable_inputs;if(out_.next_usable_round<0)out_.next_usable_round=world().round;}
                readiness(true);
            }
            if(b.alive&&!b.stun_present&&!b.channel_present) {
                if(!callbacks_.policy){fail(Status::Unsupported,Reason::UnknownPolicy);return out_;}
                const auto observation=observe(callbacks_,world(),b.id,inputs_[who]);
                const auto next=callbacks_.policy(callbacks_.context,observation);
                if(!word(who,next))return out_;
            }
            if(!end(who))return out_;
        }
        finish_complete();return out_;
    }
};
} // namespace detail

inline Outcome run(const World& initial,const Word& root,const Scenario& scenario,
                   const Callbacks& callbacks,const Requirement& requirement,Limits limits) {
    return detail::Engine(initial,scenario,callbacks,requirement,limits).execute(root);
}
}
