#pragma once
#include <algorithm>
#include <span>
namespace h12_bounded_interceptor {
enum class Channel {None,Recall,Tribute,Teleport};
struct ChannelFact {bool visible=false,alive=false;Channel kind=Channel::None;int timer=0,bag=0,handle=-1;};
// The guarded timer is deliberately queried for pending timer ZERO too.
template<class Target> ChannelFact channel_fact(Target&w){
    ChannelFact f;f.visible=w.visible();if(!f.visible)return f;
    f.alive=w.alive();if(!f.alive)return f;
    if(w.recalling())f.kind=Channel::Recall;
    else if(w.submitting())f.kind=Channel::Tribute;
    else if(w.teleporting())f.kind=Channel::Teleport;
    if(f.kind!=Channel::None){f.timer=w.timer();f.bag=w.bag();f.handle=w.handle();}
    return f;
}
inline int candidate(std::span<const ChannelFact> facts){
    int best=-1;
    for(int i=0;i<int(facts.size());++i){const auto&f=facts[i];
        if(!f.visible||!f.alive||f.kind==Channel::None||f.bag<=0)continue;
        if(best<0||f.timer<facts[best].timer||(f.timer==facts[best].timer&&f.handle<facts[best].handle))best=i;
    }return best;
}
enum class Method {None,Spell,Basic,ResetBasic};
struct Capability {
    bool allowed=false,ready=false,exclusive=false,walked=false,enemy_target=false,geometry=false;
    bool typed_stun=false,immune=false,collateral=false,basic_ready=false,basic_geometry=false;
    bool reset_ready=false,reset_compatible=false,reset_enhanced=false,enhanced=false;
    float spell_hp=0,basic_hp=0,target_hp=1;
};
struct Plan {Method method=Method::None;bool require_enhanced=false;};
template<class CC,class Area,class Ability> bool typed_stun(const Ability&a){
    if(a.is_cc())return a.template get<CC>().duration>0;
    if(a.is_aoe_damage())return a.template get<Area>().duration>0;
    return false;
}
inline bool intercept_priority(bool own_channel,bool urgent_bank,bool held_capture,bool owed){
    return !own_channel&&!urgent_bank&&!held_capture&&!owed;
}
inline bool cancels(Channel kind,bool stun,float damage,float hp){
    return stun||damage>=hp||(kind==Channel::Recall&&damage>0);
}
// This is the actual production interpreter. No spell name or is_cc shortcut
// substitutes for the adapter's typed payload and fresh action facts.
inline Plan plan(Channel channel,const Capability&c){
    if(!c.allowed||channel==Channel::None)return {};
    const bool stun=c.typed_stun&&!c.immune;
    if(c.ready&&!(c.exclusive&&c.walked)&&c.enemy_target&&c.geometry&&!c.collateral
        &&cancels(channel,stun,c.spell_hp,c.target_hp))return {Method::Spell,false};
    if(c.basic_ready&&c.basic_geometry&&cancels(channel,c.enhanced&&!c.immune,c.basic_hp,c.target_hp))
        return {Method::Basic,c.enhanced};
    // The engine refuses resetting an attack which is already available. The
    // one-basic bound forbids spending an initial hit merely to unlock reset.
    if(!c.basic_ready&&c.reset_ready&&c.reset_compatible&&c.basic_geometry
        &&cancels(channel,c.reset_enhanced&&!c.immune,c.basic_hp,c.target_hp))
        return {Method::ResetBasic,c.reset_enhanced};
    return {};
}
inline bool worthwhile(int cargo,int own_bag,float production_per_turn,float hp,float maximum,float incoming){
    // Cargo is denial opportunity, never forecast own income. Reserve current
    // HP independently of a successful stun, kill, future heal or allied help.
    return cargo>=std::max(4,own_bag)&&float(cargo)>=2.f+production_per_turn
        &&hp-2.f*incoming>=.5f*maximum;
}
enum class Production {None,CampDamage,PadHold,PersonalPayout,Bank};
class Resume {
    bool owed_=false;int attempted_=-1,saved_=-1;
public:
    bool owed()const{return owed_;}int attempted()const{return attempted_;}int saved()const{return saved_;}
    void owe(int round,int source){owed_=true;attempted_=round;saved_=source;}
    void invalidate_source(){saved_=-1;}
    int take_source(){const int result=saved_;saved_=-1;return result;}
    bool repay(int round,Production effect){
        if(!owed_||round<=attempted_||effect==Production::None)return false;
        owed_=false;saved_=-1;return true;
    }
};
enum class Outcome {Unchanged,Cancelled,Dead,Unknown};
struct Attempt {bool attempted=false,cast=false,basic=false;Outcome outcome=Outcome::Unchanged;};
template<class World> Attempt execute(Resume&resume,World&w,Plan p){
    Attempt r;if(p.method==Method::None)return r;
    // A rejected SDK action still incurs the resume obligation. Exceptions also
    // leave it set, because the turn boundary owns the catch.
    resume.owe(w.round(),w.source());r.attempted=true;
    if(p.method==Method::Spell)r.cast=w.cast();
    else {
        bool proceed=true;
        if(p.method==Method::ResetBasic){r.cast=w.reset();proceed=r.cast;}
        if(proceed&&(!p.require_enhanced||w.enhanced())&&w.basic_legal())r.basic=w.basic();
    }
    // Query before any possible later movement or enemy owner-start completion.
    const auto after=w.after();
    if(!after.visible)r.outcome=Outcome::Unknown;
    else if(!after.alive)r.outcome=Outcome::Dead;
    else if(after.kind==Channel::None)r.outcome=Outcome::Cancelled;
    return r;
}
}
