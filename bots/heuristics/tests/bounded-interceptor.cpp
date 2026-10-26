// The fixture supplies the SDK's declared C ABI in-process. Removing only the
// import-attribute switch leaves the real wrappers/constructors unchanged.
#if defined(__wasm__)
#define H12_FIXTURE_RESTORE_WASM 1
#undef __wasm__
#endif
#define H12_RUNTIME_COLLATERAL_FIXTURE 1
// The existing release Runtime is not compiled with the fixture's -Werror.
// Keep its two existing diagnostics local; all new fixture code remains strict.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wunused-variable"
#include "../bounded-interceptor/src/runtime.h"
#pragma clang diagnostic pop
#if defined(H12_FIXTURE_RESTORE_WASM)
#define __wasm__ 1
#undef H12_FIXTURE_RESTORE_WASM
#endif
#include "../bounded-interceptor/src/mechanics.h"
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <stdexcept>
#include <type_traits>

namespace bi=h12_bounded_interceptor;

// This is an engine adapter, not another selector. Invalid live getter calls
// assert, while the production channel_fact owns the guarded query sequence.
struct Target {
    bool shown=true,living=true;
    bi::Channel channel=bi::Channel::Tribute;
    int remaining=0,cargo=11,id=7;
    int state_calls=0,timer_calls=0,bag_calls=0;
    bool visible(){return shown;}
    bool alive(){assert(shown);++state_calls;return living;}
    bool recalling(){assert(shown&&living);++state_calls;return channel==bi::Channel::Recall;}
    bool submitting(){assert(shown&&living);++state_calls;return channel==bi::Channel::Tribute;}
    bool teleporting(){assert(shown&&living);++state_calls;return channel==bi::Channel::Teleport;}
    int timer(){assert(shown&&living&&channel!=bi::Channel::None);++timer_calls;return remaining;}
    int bag(){assert(shown&&living&&channel!=bi::Channel::None);++bag_calls;return cargo;}
    int handle(){return id;}
};

void guarded_channels(){
    Target t;
    auto f=bi::channel_fact(t);
    assert(f.visible&&f.alive&&f.kind==bi::Channel::Tribute&&f.timer==0&&f.bag==11);
    assert(t.timer_calls==1&&t.bag_calls==1); // Zero is still pending.
    t.shown=false;t.state_calls=t.timer_calls=t.bag_calls=0;
    f=bi::channel_fact(t);
    assert(!f.visible&&f.kind==bi::Channel::None);
    assert(t.state_calls==0&&t.timer_calls==0&&t.bag_calls==0);
    t.shown=true;t.living=false;
    f=bi::channel_fact(t);
    assert(f.visible&&!f.alive&&t.timer_calls==0);
    t.living=true;t.channel=bi::Channel::None;
    f=bi::channel_fact(t);assert(f.kind==bi::Channel::None&&t.timer_calls==0);
    for(const auto channel:{bi::Channel::Recall,bi::Channel::Tribute,bi::Channel::Teleport}){
        t.channel=channel;t.remaining=0;f=bi::channel_fact(t);
        assert(f.kind==channel&&f.timer==0);
    }
}

void candidate_cases(){
    using C=bi::Channel;
    std::array<bi::ChannelFact,6> candidates{{
        {true,true,C::Recall,2,12,1},
        {true,true,C::Tribute,0,4,9},
        {true,true,C::Teleport,0,4,7},
        {false,true,C::Tribute,0,30,0},
        {true,false,C::Recall,0,30,0},
        {true,true,C::Tribute,0,0,0}}};
    assert(bi::candidate(candidates)==2); // Earliest pending timer, then handle.
    candidates[2].bag=0;assert(bi::candidate(candidates)==1);
    candidates[1].kind=C::None;assert(bi::candidate(candidates)==0);
    candidates[0].visible=false;assert(bi::candidate(candidates)==-1);
    std::span<const bi::ChannelFact> empty;assert(bi::candidate(empty)==-1);
    // Admission/plan failure on the selected ID must not mutate selection into
    // a search for a lower-ranked reachable opponent.
    candidates[0]={true,true,C::Tribute,0,12,1};
    candidates[1]={true,true,C::Recall,1,12,2};
    const int selected=bi::candidate(candidates);assert(selected==0);
    bi::Capability impossible;impossible.allowed=true;
    assert(bi::plan(candidates[selected].kind,impossible).method==bi::Method::None);
    assert(bi::candidate(candidates)==selected);
}

bi::Capability stun_spell(){
    bi::Capability c;c.allowed=true;c.ready=true;c.exclusive=true;
    c.enemy_target=true;c.geometry=true;c.typed_stun=true;c.target_hp=100;
    return c;
}

struct CCPayload {int duration=0;};
struct AreaPayload {int duration=0;};
struct AbilityPayload {
    enum class Kind {CC,Area,Damage,Reset};
    Kind kind=Kind::Area;int duration=1;
    mutable int cc_queries=0,area_queries=0;
    bool is_cc()const{return kind==Kind::CC;}
    bool is_aoe_damage()const{return kind==Kind::Area;}
    template<class T> T get()const {
        if constexpr(std::is_same_v<T,CCPayload>){assert(kind==Kind::CC);++cc_queries;}
        else {static_assert(std::is_same_v<T,AreaPayload>);assert(kind==Kind::Area);++area_queries;}
        return T{duration};
    }
};

void payload_cases(){
    // Feed the same extraction adapter that production uses. Ryth's Area
    // payload has positive duration despite is_cc() being false.
    AbilityPayload ryth;assert(!ryth.is_cc()&&ryth.is_aoe_damage());
    auto c=stun_spell();c.typed_stun=bi::typed_stun<CCPayload,AreaPayload>(ryth);
    assert(c.typed_stun&&ryth.cc_queries==0&&ryth.area_queries==1);
    assert(bi::plan(bi::Channel::Tribute,c).method==bi::Method::Spell);
    c.collateral=true;assert(bi::plan(bi::Channel::Tribute,c).method==bi::Method::None);
    ryth.duration=0;assert((!bi::typed_stun<CCPayload,AreaPayload>(ryth)));
    ryth.duration=-1;assert((!bi::typed_stun<CCPayload,AreaPayload>(ryth)));
    AbilityPayload cc{AbilityPayload::Kind::CC,1};
    assert((bi::typed_stun<CCPayload,AreaPayload>(cc)));
    assert(cc.cc_queries==1&&cc.area_queries==0);
    cc.duration=0;assert((!bi::typed_stun<CCPayload,AreaPayload>(cc)));
    for(const auto kind:{AbilityPayload::Kind::Damage,AbilityPayload::Kind::Reset}){
        AbilityPayload nonstun{kind,99};
        assert((!bi::typed_stun<CCPayload,AreaPayload>(nonstun)));
        assert(nonstun.cc_queries==0&&nonstun.area_queries==0);
    }
}

void interpreter_cases(){
    using C=bi::Channel;using M=bi::Method;
    auto c=stun_spell();
    for(const auto channel:{C::Recall,C::Tribute,C::Teleport})assert(bi::plan(channel,c).method==M::Spell);
    assert(bi::plan(C::None,c).method==M::None);
    c.allowed=false;assert(bi::plan(C::Tribute,c).method==M::None);c.allowed=true;
    c.ready=false;assert(bi::plan(C::Tribute,c).method==M::None);c.ready=true;
    c.enemy_target=false;assert(bi::plan(C::Tribute,c).method==M::None);c.enemy_target=true;
    c.geometry=false;assert(bi::plan(C::Tribute,c).method==M::None);c.geometry=true;
    c.walked=true;assert(bi::plan(C::Tribute,c).method==M::None);
    c.exclusive=false;assert(bi::plan(C::Tribute,c).method==M::Spell);c.walked=false;
    // Ryth's typed positive AoE duration is the stun fact even though is_cc()
    // would be false. Unknown, mixed and harmless camp collateral all veto it.
    c.collateral=true;assert(bi::plan(C::Tribute,c).method==M::None);
    c.spell_hp=200;assert(bi::plan(C::Tribute,c).method==M::None);
    c.collateral=false;c.spell_hp=0;c.immune=true;
    assert(bi::plan(C::Tribute,c).method==M::None); // Marcella cannot be stun-denied.
    c.spell_hp=1;assert(bi::plan(C::Recall,c).method==M::Spell);
    assert(bi::plan(C::Tribute,c).method==M::None&&bi::plan(C::Teleport,c).method==M::None);
    c.spell_hp=c.target_hp;assert(bi::plan(C::Tribute,c).method==M::Spell);
    c.typed_stun=false;c.spell_hp=0;
    assert(bi::plan(C::Recall,c).method==M::None); // Fully absorbed damage.

    c={};c.allowed=true;c.target_hp=100;c.basic_ready=true;c.basic_geometry=true;c.basic_hp=1;
    assert(bi::plan(C::Recall,c).method==M::Basic);
    assert(bi::plan(C::Tribute,c).method==M::None&&bi::plan(C::Teleport,c).method==M::None);
    c.basic_hp=0;assert(bi::plan(C::Recall,c).method==M::None);
    c.enhanced=true;
    auto p=bi::plan(C::Tribute,c);assert(p.method==M::Basic&&p.require_enhanced);
    c.immune=true;assert(bi::plan(C::Tribute,c).method==M::None);
    c.basic_hp=100;assert(bi::plan(C::Teleport,c).method==M::Basic);
    c.basic_geometry=false;assert(bi::plan(C::Recall,c).method==M::None);

    c={};c.allowed=true;c.target_hp=100;c.basic_geometry=true;
    c.reset_ready=true;c.reset_compatible=true;c.reset_enhanced=true;
    // Actual engine ERROR26: an available attack cannot be reset. A normal
    // nonlethal hit would not cancel tribute, and spending it to unlock reset
    // would need two basics. The bounded interpreter must refuse that chain.
    c.basic_ready=true;c.basic_hp=1;
    assert(bi::plan(C::Tribute,c).method==M::None);
    c.basic_ready=false;
    p=bi::plan(C::Tribute,c);assert(p.method==M::ResetBasic&&p.require_enhanced);
    c.reset_ready=false;assert(bi::plan(C::Tribute,c).method==M::None);c.reset_ready=true;
    c.reset_compatible=false;assert(bi::plan(C::Tribute,c).method==M::None);c.reset_compatible=true;
    c.immune=true;assert(bi::plan(C::Tribute,c).method==M::None);c.immune=false;
    // A Willow plain reset supports HP-damaging recall only, never a nonlethal
    // tribute/teleport interruption. No enhancement lifetime becomes a stun.
    c.reset_enhanced=false;c.basic_hp=1;
    p=bi::plan(C::Recall,c);assert(p.method==M::ResetBasic&&!p.require_enhanced);
    assert(bi::plan(C::Tribute,c).method==M::None&&bi::plan(C::Teleport,c).method==M::None);
    c.basic_hp=100;assert(bi::plan(C::Teleport,c).method==M::ResetBasic);
}

// The fake engine supplies action acceptance separately from actual effects.
// The real execute() is responsible for debt-before-action, fresh enhancement,
// legal basic recheck, caps, and post-effect live observation.
struct Engine {
    bi::Resume* resume=nullptr;Target target;
    int now=10,selected=44,casts=0,resets=0,basics=0,after_calls=0,enhancement_queries=0,legal_queries=0;
    bool cast_accept=true,reset_accept=true,basic_accept=true,enhancement=true,basic_allowed=true;
    bool attack_available_before_reset=false;
    bool effective_stun=true,lethal=false,hp_damage=false,vanish=false,throw_cast=false;
    bool actions_done=false;
    int round()const{return now;}int source()const{return selected;}
    void effect(){
        if(lethal)target.living=false;
        else if(effective_stun||(target.channel==bi::Channel::Recall&&hp_damage))target.channel=bi::Channel::None;
        if(vanish)target.shown=false;
    }
    void check_debt(){assert(resume&&resume->owed()&&resume->attempted()==now&&resume->saved()==selected);}
    bool cast(){
        check_debt();assert(!actions_done);++casts;assert(casts+resets<=1);
        if(throw_cast)throw std::runtime_error("rejected SDK call");
        if(cast_accept)effect();actions_done=true;return cast_accept;
    }
    bool reset(){
        check_debt();assert(!actions_done);++resets;assert(casts+resets<=1);
        return reset_accept&&!attack_available_before_reset;
    }
    bool enhanced(){++enhancement_queries;return enhancement;}
    bool basic_legal(){++legal_queries;return basic_allowed&&target.shown&&target.living;}
    bool basic(){
        check_debt();assert(!actions_done);++basics;assert(basics<=1);
        if(basic_accept)effect();actions_done=true;return basic_accept;
    }
    bi::ChannelFact after(){++after_calls;return bi::channel_fact(target);}
};

void execution_cases(){
    using M=bi::Method;using O=bi::Outcome;
    bi::Resume resume;Engine w;w.resume=&resume;
    auto r=bi::execute(resume,w,{M::None,false});
    assert(!r.attempted&&!resume.owed()&&w.after_calls==0);
    r=bi::execute(resume,w,{M::Spell,false});
    assert(r.attempted&&r.cast&&!r.basic&&r.outcome==O::Cancelled);
    assert(w.casts==1&&w.basics==0&&w.after_calls==1&&resume.owed());

    resume={};w={};w.resume=&resume;w.cast_accept=false;
    r=bi::execute(resume,w,{M::Spell,false});
    assert(r.attempted&&!r.cast&&r.outcome==O::Unchanged&&resume.owed());
    assert(w.casts==1&&w.basics==0&&w.after_calls==1);
    resume={};w={};w.resume=&resume;w.effective_stun=false;
    r=bi::execute(resume,w,{M::Spell,false});
    assert(r.cast&&r.outcome==O::Unchanged); // Acceptance is not effective stun.
    resume={};w={};w.resume=&resume;w.vanish=true;
    r=bi::execute(resume,w,{M::Spell,false});assert(r.outcome==O::Unknown);
    resume={};w={};w.resume=&resume;w.lethal=true;
    r=bi::execute(resume,w,{M::Spell,false});assert(r.outcome==O::Dead);

    resume={};w={};w.resume=&resume;w.throw_cast=true;
    try{bi::execute(resume,w,{M::Spell,false});assert(false);}catch(const std::runtime_error&){}
    assert(resume.owed()&&resume.saved()==44&&w.casts==1&&w.after_calls==0);

    resume={};w={};w.resume=&resume;
    r=bi::execute(resume,w,{M::ResetBasic,true});
    assert(r.cast&&r.basic&&r.outcome==O::Cancelled);
    assert(w.resets==1&&w.basics==1&&w.enhancement_queries==1&&w.legal_queries==1&&w.after_calls==1);
    resume={};w={};w.resume=&resume;w.attack_available_before_reset=true;
    r=bi::execute(resume,w,{M::ResetBasic,true});
    assert(!r.cast&&!r.basic&&r.outcome==O::Unchanged&&resume.owed());
    assert(w.resets==1&&w.basics==0&&w.enhancement_queries==0&&w.legal_queries==0);
    resume={};w={};w.resume=&resume;w.reset_accept=false;
    r=bi::execute(resume,w,{M::ResetBasic,true});
    assert(!r.cast&&!r.basic&&r.outcome==O::Unchanged&&resume.owed());
    assert(w.resets==1&&w.enhancement_queries==0&&w.legal_queries==0&&w.basics==0);
    resume={};w={};w.resume=&resume;w.enhancement=false;
    r=bi::execute(resume,w,{M::ResetBasic,true});
    assert(r.cast&&!r.basic&&r.outcome==O::Unchanged&&w.legal_queries==0);
    resume={};w={};w.resume=&resume;w.basic_allowed=false;
    r=bi::execute(resume,w,{M::ResetBasic,true});
    assert(r.cast&&!r.basic&&r.outcome==O::Unchanged&&w.legal_queries==1);
    resume={};w={};w.resume=&resume;w.enhancement=false;w.hp_damage=true;w.effective_stun=false;w.target.channel=bi::Channel::Recall;
    r=bi::execute(resume,w,{M::ResetBasic,false});
    assert(r.cast&&r.basic&&r.outcome==O::Cancelled&&w.enhancement_queries==0);
    resume={};w={};w.resume=&resume;w.basic_accept=false;
    r=bi::execute(resume,w,{M::Basic,true});
    assert(!r.cast&&!r.basic&&r.outcome==O::Unchanged&&w.basics==1&&resume.owed());
}

void resume_cases(){
    using P=bi::Production;
    bi::Resume r;r.owe(10,44);
    assert(r.owed()&&r.saved()==44);
    assert(!r.repay(10,P::CampDamage)&&!r.repay(10,P::PadHold)&&!r.repay(10,P::Bank));
    // No action acceptance, movement, enemy damage or interception kill reward
    // has a repayment kind. None represents the absence of a normal effect.
    assert(!r.repay(11,P::None)&&r.owed());
    r.invalidate_source();assert(r.owed()&&r.saved()==-1); // Death invalidates the job only.
    assert(r.take_source()==-1&&r.owed());
    assert(!r.repay(15,P::None)&&r.owed()); // Respawn or switching cannot erase it.
    assert(r.repay(16,P::CampDamage)&&!r.owed());
    for(const auto p:{P::CampDamage,P::PadHold,P::PersonalPayout,P::Bank}){
        r.owe(20,9);assert(!r.repay(20,p)&&r.owed());
        assert(r.repay(21,p)&&!r.owed()&&r.saved()==-1);
    }
    r.owe(30,7);assert(r.take_source()==7&&r.owed()&&r.saved()==-1);
    assert(!r.repay(31,P::None));
    assert(r.repay(32,P::PadHold));
}

struct RespawnWorld {
    int quarantine=0,departure=0,alternate=0,recovery=0;
    bool pad=true,depart=false,recover=false;
    bi::WorkResult alt;
    void quarantine_respawn(int id){assert(id==44);++quarantine;}
    bool on_respawn_pad(int id){assert(id==44);return pad;}
    bool depart_respawn_pad(int id){assert(id==44);++departure;return depart;}
    bi::WorkResult alternate_respawn_work(){++alternate;return alt;}
    bool recover_after_respawn(){++recovery;return recover;}
};

void inherited_lifecycle_cases(){
    bi::SpawnRest rest;
    assert(rest.entry(1,true,true));assert(!rest.entry(2,true,true));assert(!rest.entry(3,true,true));
    assert(!rest.entry(4,false,true));assert(rest.entry(5,true,true));
    rest.death();assert(rest.entry(9,true,true));assert(!rest.entry(10,true,true));
    bi::PaidCapture paid;paid.expect(44);
    assert(!paid.possible(11,100,11)); // Group death alone cannot pay this actor.
    paid.hold(44,10,0,0);assert(paid.possible(11,40,0));
    assert(!paid.receipt(11,false,40,0));assert(paid.source()==44);
    assert(paid.receipt(11,true,40,0)&&paid.source()==-1);
    paid.expect(9);paid.hold(8,20,0,0);assert(!paid.possible(21,40,0));
    paid.hold(9,20,0,0);assert(!paid.possible(22,40,0)); // Stale residence.
    paid.completed(9);assert(paid.source()==-1);
    assert(bi::respawn_quarantine(100,0)==112&&bi::respawn_quarantine(100,25)==125);
    RespawnWorld w;w.alt={true,false};auto r=bi::handle_respawn_refusal(w,44,true);
    assert(r.accepted&&!r.acted&&w.alternate==1&&w.recovery==0); // Useful no-action capture wait.
    w={};w.alt={false,true};r=bi::handle_respawn_refusal(w,44,true);
    assert(!r.accepted&&r.acted&&w.recovery==0); // Earlier successful move survives later refusal.
    w={};w.depart=true;r=bi::handle_respawn_refusal(w,44,true);
    assert(r.accepted&&r.acted&&w.departure==1&&w.alternate==0);
    w={};w.recover=true;r=bi::handle_respawn_refusal(w,44,false);
    assert(r.accepted&&r.acted&&w.alternate==0&&w.recovery==1);
}

void price_cases(){
    assert(bi::intercept_priority(false,false,false,false));
    assert(!bi::intercept_priority(true,false,false,false));
    assert(!bi::intercept_priority(false,true,false,false));
    assert(!bi::intercept_priority(false,false,true,false));
    assert(!bi::intercept_priority(false,false,false,true));
    assert(!bi::intercept_priority(true,true,true,true));
    assert(bi::worthwhile(4,0,2,70,100,10));
    assert(!bi::worthwhile(0,0,0,100,100,0));
    assert(!bi::worthwhile(3,0,0,100,100,0));
    assert(!bi::worthwhile(4,5,0,100,100,0));
    assert(!bi::worthwhile(4,0,3,100,100,0)); // Foregone production.
    assert(!bi::worthwhile(100,0,0,69,100,10)); // Cargo cannot buy future survival.
    assert(!bi::worthwhile(100,0,0,49,100,0));
}

// Synthetic host contract for the real SDK and Runtime. These inputs describe
// this fixture, not reconstructed private memory or a native replay state.
namespace fixture_host {
constexpr int width=16,height=8,own=5,enemy=3;
struct Scene {
    bool collateral=false,channel=true,cast_available=true,attack_available=true;
    int cooldown=0,casts=0,enemy_basics=0,camp_basics=0,layouts=0,channels=0,readiness=0;
    int spell_props=0,damage_queries=0,visible_queries=0,champion_queries=0;
    float enemy_hp=460,camp_hp=30;
};
Scene scene;
[[noreturn]] void unexpected(const char*name){std::fprintf(stderr,"error: undeclared fixture ABI %s\n",name);std::abort();}
void valid(int x,int y){assert(x>=0&&x<width&&y>=0&&y<height);}
bool test(const std::uint64_t*b,int x,int y){valid(x,y);return (b[x/8]>>(8*y+x%8))&1;}
void set(std::uint64_t*b,int x,int y,bool value=true){valid(x,y);const auto mask=std::uint64_t{1}<<(8*y+x%8);if(value)b[x/8]|=mask;else b[x/8]&=~mask;}
void zero(std::uint64_t*b){b[0]=b[1]=0;}
cambc::detail::ability_props props(unsigned slot){
    assert(slot<2);cambc::detail::ability_props p{};p.cooldown=8;
    if(slot==0){p.kind=3;p.targets=19;p.attributes=1;p.payload.aoe_damage={4,1,1,1};}
    else {p.kind=1;p.targets=8;p.payload.enhanced_attack=0;}
    return p;
}
std::uint32_t flags(bool own_actor){
    using C=cambc::champion_state;
    auto s=C::alive|C::can_move|C::can_attack|C::can_cast;
    if(own_actor){if(!scene.cast_available)s.remove(C::can_cast);if(!scene.attack_available)s.remove(C::can_attack);}
    else if(scene.channel)s.insert(C::recalling);
    return s.bits();
}
void reset(bool collateral){scene=Scene{};scene.collateral=collateral;}
}

namespace cambc::detail {
extern "C" {
std::int32_t sdk_get_settings(game_settings*out){
    *out={};out->capture_time=5;out->camp_sleep_time=2;out->respawn_time=60;out->recall_time=5;
    out->submit_gem_duration_multiplier=.1;out->submit_gem_minimum_duration=1;out->submit_gems_range=1;
    out->max_rounds=2000;out->winning_gems=1000;out->kill_gold=100;out->spawn_healing=1;
    out->altar_lock_hits=5;out->tp_crystal_time=3;return 0;
}
std::int32_t sdk_shared_memory_size(std::uint32_t*out){*out=0;return 0;}
std::int32_t sdk_draft_memory_size(std::uint32_t*out){*out=0;return 0;}
std::int32_t sdk_bc_metadata(metadata*out){*out={16,8,2,1,2,16,256};return 0;}
std::int32_t sdk_my_champion(){return fixture_host::own;}
std::int32_t sdk_my_team(){return 0;}
std::int32_t sdk_bc_map_symmetry(std::int32_t*out){*out=0;return 0;}
std::int32_t sdk_bc_map_mask(std::int32_t kind,std::uint64_t*out){assert(kind>=0&&kind<=5);fixture_host::zero(out);return 0;}
std::int32_t sdk_consumable_info(std::uint32_t id,consumable_info*out){assert(id==0||id==2);*out={10,1};return 0;}
std::int32_t sdk_champion_design(std::int32_t type,champion_design*out){
    assert(type>=0&&type<13);*out={};out->definition.type=type;
    // All startup design records are declared; only Ryth's kit is consumed by
    // the live controlled champion. Other synthetic designs have empty slots.
    out->base_stats={8,1,1,1200,0,40,0,100,0,1};
    if(type==0){out->definition.abilities[0]=fixture_host::props(0);out->definition.abilities[1]=fixture_host::props(1);}
    return 0;
}
std::int32_t sdk_champion_ability_props(std::uint32_t id,std::uint32_t slot,ability_props*out){
    assert((id==fixture_host::own||id==fixture_host::enemy)&&slot<2);*out={};
    if(id==fixture_host::own){*out=fixture_host::props(slot);if(slot==0)++fixture_host::scene.spell_props;}return 0;
}
std::int32_t sdk_champion_identity(std::uint32_t id,champion_identity*out){assert(id==5||id==3);*out={id==5?0:1,id==5?0:2};return 0;}
std::int32_t sdk_champion(std::uint32_t id,champion_info*out){
    assert(id==5||id==3);const bool me=id==5;auto&s=fixture_host::scene;
    ++s.champion_queries;
    *out={me?0:1,me?0:2,me?8:4,2,me?1200:s.enemy_hp,me?1200.f:1200.f,40,100,1,1,int(fixture_host::flags(me))};return 0;
}
std::int32_t sdk_champion_is_alive(std::uint32_t id,std::int32_t*out){assert(id==5||id==3);*out=1;return 0;}
std::int32_t sdk_champion_at(std::int32_t x,std::int32_t y,std::uint32_t*out){assert(y==2&&(x==8||x==4));*out=x==8?5:3;return 0;}
std::int32_t sdk_champion_gems(std::uint32_t id,std::uint32_t*out){assert(id==5||id==3);*out=id==3?22:0;return 0;}
std::int32_t sdk_champion_gold(std::uint32_t id,std::uint32_t*out){assert(id==5);*out=10;return 0;}
std::int32_t sdk_champion_channel_time(std::uint32_t id,std::int32_t*out){assert(id==3&&fixture_host::scene.channel);*out=1;++fixture_host::scene.channels;return 0;}
std::int32_t sdk_champion_cooldown(std::uint32_t id,std::uint32_t slot,std::int32_t*out){assert(id==5&&slot<2);*out=slot==0?fixture_host::scene.cooldown:0;++fixture_host::scene.readiness;return 0;}
std::int32_t sdk_champion_buff_data(std::uint32_t id,buff_data*,std::uint32_t,std::uint32_t*out){assert(id==5||id==3);*out=0;return 0;}
std::int32_t sdk_champion_ability_damage_against(std::uint32_t owner,std::uint32_t slot,std::uint32_t id,float*out){assert(owner==5&&slot==0&&id==3);*out=100;++fixture_host::scene.damage_queries;return 0;}
std::int32_t sdk_num_enemies(std::uint32_t*out){*out=1;return 0;}
std::int32_t sdk_enemies(std::uint32_t*out,std::uint32_t capacity,std::uint32_t*count){assert(capacity>=1);out[0]=3;*count=1;return 0;}
std::int32_t sdk_is_visible(std::int32_t x,std::int32_t y,std::int32_t*out){fixture_host::valid(x,y);*out=1;++fixture_host::scene.visible_queries;return 0;}
std::int32_t sdk_awake_camp_occupancy(std::uint64_t*out){fixture_host::zero(out);return 0;}
std::int32_t sdk_map_camp_layout(std::uint32_t*out,std::uint32_t capacity,std::uint32_t*count){
    assert(capacity>=18);auto&s=fixture_host::scene;const std::uint32_t words[]={0,60,4,60,1,100,0,9,2,1,1,1,60,1,101,0,4,s.collateral?3u:4u};
    std::copy(std::begin(words),std::end(words),out);*count=18;++s.layouts;return 0;
}
std::int32_t sdk_camp_design(std::int32_t type,camp_design*out){assert(type==0);*out={};out->definition.type=0;out->base_stats={8,0,1,30,0,0,0,0,0};return 0;}
std::int32_t sdk_camp(std::uint32_t id,camp_state*out){assert(id==100);*out={9,2,fixture_host::scene.camp_hp,30,0,0,{0,0}};return 0;}
std::int32_t sdk_camp_state(std::uint32_t id,std::uint32_t*out){assert(id==100);*out=fixture_host::scene.camp_hp>0?1:0;return 0;}
std::int32_t sdk_camp_at(std::int32_t x,std::int32_t y,std::uint32_t*out){assert(x==9&&y==2);*out=100;return 0;}
std::int32_t sdk_bc_round_number(std::uint32_t*out){*out=340;return 0;}
std::int64_t sdk_fuel_remaining(){return 100000;}
std::int32_t sdk_cast(std::uint32_t slot,std::uint32_t kind,std::uint32_t id,std::int32_t x,std::int32_t y,std::int32_t*accepted){
    auto&s=fixture_host::scene;assert(slot==0&&kind==1&&id==3&&x==-1&&y==-1);
    // Camp collateral is legal to the host. The production policy alone must
    // refuse it; making this sink reject collateral would hide that regression.
    assert(s.cast_available&&s.cooldown==0&&s.channel);
    ++s.casts;s.cast_available=false;s.cooldown=8;s.enemy_hp-=100;s.channel=false;*accepted=1;return 0;
}
std::int32_t sdk_attack(std::uint32_t kind,std::uint32_t id){
    auto&s=fixture_host::scene;assert(kind==2&&id==100&&s.attack_available);++s.camp_basics;s.attack_available=false;s.camp_hp=0;return 0;
}
std::int32_t sdk_Bitboard_Zero(std::uint64_t*b){fixture_host::zero(b);return 0;}
std::int32_t sdk_Bitboard_Copy(const std::uint64_t*a,std::uint64_t*b){std::copy(a,a+2,b);return 0;}
std::int32_t sdk_Bitboard_Union(const std::uint64_t*a,const std::uint64_t*b,std::uint64_t*out){for(int i=0;i<2;++i)out[i]=a[i]|b[i];return 0;}
std::int32_t sdk_Bitboard_Intersect(const std::uint64_t*a,const std::uint64_t*b,std::uint64_t*out){for(int i=0;i<2;++i)out[i]=a[i]&b[i];return 0;}
std::int32_t sdk_Bitboard_Test(const std::uint64_t*b,std::int32_t x,std::int32_t y,std::int32_t*out){*out=fixture_host::test(b,x,y);return 0;}
std::int32_t sdk_Bitboard_IsZero(const std::uint64_t*b,std::int32_t*out){*out=(b[0]|b[1])==0;return 0;}
std::int32_t sdk_Bitboard_DrawChebyshev(std::uint64_t*b,std::int32_t x,std::int32_t y,float radius){
    fixture_host::valid(x,y);assert(radius>=0);for(int yy=0;yy<8;++yy)for(int xx=0;xx<16;++xx)if(std::max(std::abs(xx-x),std::abs(yy-y))<=radius)fixture_host::set(b,xx,yy);return 0;
}
std::int32_t sdk_Bitboard_GetSetBits(const std::uint64_t*b,void*positions,std::size_t capacity,std::uint32_t*count){
    auto*out=static_cast<position*>(positions);*count=0;for(int y=0;y<8;++y)for(int x=0;x<16;++x)if(fixture_host::test(b,x,y)){assert(*count<capacity);out[(*count)++]={x,y};}return 0;
}
// Explicitly deny all other ABI inputs/actions. An expanded production
// prefix must declare its real host contract rather than get zero-success.
std::int32_t sdk_item_info(std::uint32_t, item_info*){fixture_host::unexpected("sdk_item_info");}
std::int32_t sdk_champion_ability_damage(std::uint32_t, std::uint32_t, float*){fixture_host::unexpected("sdk_champion_ability_damage");}
std::int32_t sdk_champion_ability_heal(std::uint32_t, std::uint32_t, float*){fixture_host::unexpected("sdk_champion_ability_heal");}
std::int32_t sdk_champion_ability_heal_against(std::uint32_t, std::uint32_t, std::uint32_t, float*){fixture_host::unexpected("sdk_champion_ability_heal_against");}
std::int32_t sdk_champion_ability_shield(std::uint32_t, std::uint32_t, float*){fixture_host::unexpected("sdk_champion_ability_shield");}
std::int32_t sdk_champion_ability_shield_against(std::uint32_t, std::uint32_t, std::uint32_t, float*){fixture_host::unexpected("sdk_champion_ability_shield_against");}
std::int32_t sdk_bc_get_last_error(char*, std::uint32_t){fixture_host::unexpected("sdk_bc_get_last_error");}
std::int32_t sdk_num_allies(std::uint32_t*){fixture_host::unexpected("sdk_num_allies");}
std::int32_t sdk_allies(std::uint32_t*, std::uint32_t, std::uint32_t*){fixture_host::unexpected("sdk_allies");}
std::int32_t sdk_ally_occupancy(std::uint64_t*){fixture_host::unexpected("sdk_ally_occupancy");}
std::int32_t sdk_enemy_occupancy(std::uint64_t*out){fixture_host::zero(out);fixture_host::set(out,4,2);return 0;}
std::int32_t sdk_camp_occupancy(std::uint64_t*){fixture_host::unexpected("sdk_camp_occupancy");}
std::int32_t sdk_asleep_camp_occupancy(std::uint64_t*){fixture_host::unexpected("sdk_asleep_camp_occupancy");}
std::int32_t sdk_ally_ward_occupancy(std::uint64_t*){fixture_host::unexpected("sdk_ally_ward_occupancy");}
std::int32_t sdk_enemy_ward_occupancy(std::uint64_t*){fixture_host::unexpected("sdk_enemy_ward_occupancy");}
std::int32_t sdk_ward_at(std::int32_t, std::int32_t, std::uint32_t*){fixture_host::unexpected("sdk_ward_at");}
std::int32_t sdk_ally_ward_at(std::int32_t, std::int32_t, std::uint32_t*){fixture_host::unexpected("sdk_ally_ward_at");}
std::int32_t sdk_enemy_ward_at(std::int32_t, std::int32_t, std::uint32_t*){fixture_host::unexpected("sdk_enemy_ward_at");}
std::int32_t sdk_ward(std::uint32_t, ward_info*){fixture_host::unexpected("sdk_ward");}
std::int32_t sdk_teleport_occupancy(std::uint64_t*){fixture_host::unexpected("sdk_teleport_occupancy");}
std::int32_t sdk_has_teleport(std::int32_t, std::int32_t, std::int32_t*){fixture_host::unexpected("sdk_has_teleport");}
std::int32_t sdk_teleports(teleport_info*, std::uint32_t, std::uint32_t*){fixture_host::unexpected("sdk_teleports");}
std::int32_t sdk_teleport_at(std::int32_t, std::int32_t, teleport_info*){fixture_host::unexpected("sdk_teleport_at");}
std::int32_t sdk_ward_vision(std::int32_t, std::int32_t, std::uint64_t*){fixture_host::unexpected("sdk_ward_vision");}
std::int32_t sdk_ally_altar_lock_occupancy(std::uint64_t*){fixture_host::unexpected("sdk_ally_altar_lock_occupancy");}
std::int32_t sdk_enemy_altar_lock_occupancy(std::uint64_t*){fixture_host::unexpected("sdk_enemy_altar_lock_occupancy");}
std::int32_t sdk_altar_lock(std::int32_t, std::int32_t, std::int32_t, altar_lock_info*){fixture_host::unexpected("sdk_altar_lock");}
std::int32_t sdk_altar_locks(altar_lock_info*, std::uint32_t, std::uint32_t*){fixture_host::unexpected("sdk_altar_locks");}
std::int32_t sdk_use_consumable(std::uint32_t, std::int32_t, std::int32_t, std::int32_t*){fixture_host::unexpected("sdk_use_consumable");}
std::int32_t sdk_buy_consumable(std::uint32_t){fixture_host::unexpected("sdk_buy_consumable");}
std::int32_t sdk_buy_item(std::uint32_t, std::uint32_t){fixture_host::unexpected("sdk_buy_item");}
std::int32_t sdk_sell(std::uint32_t){fixture_host::unexpected("sdk_sell");}
std::int32_t sdk_recall(){fixture_host::unexpected("sdk_recall");}
std::int32_t sdk_cancel_channel(){fixture_host::unexpected("sdk_cancel_channel");}
std::int32_t sdk_submit_gems(std::int32_t, std::int32_t){fixture_host::unexpected("sdk_submit_gems");}
std::int32_t sdk_place_ward(std::int32_t, std::int32_t, std::int32_t*){fixture_host::unexpected("sdk_place_ward");}
std::int32_t sdk_move_to(std::int32_t, std::int32_t, std::int32_t*){fixture_host::unexpected("sdk_move_to");}
std::int32_t sdk_set_priority(std::int32_t){fixture_host::unexpected("sdk_set_priority");}
std::int32_t sdk_bc_champion_priority(std::uint32_t, std::int32_t, std::int32_t*){fixture_host::unexpected("sdk_bc_champion_priority");}
std::int32_t sdk_bc_submitted_gems(std::uint32_t, std::uint32_t*){fixture_host::unexpected("sdk_bc_submitted_gems");}
std::int32_t sdk_resign(){fixture_host::unexpected("sdk_resign");}
std::int32_t sdk_read_shared_memory(std::int32_t, void*, std::uint32_t){fixture_host::unexpected("sdk_read_shared_memory");}
std::int32_t sdk_write_shared_memory(std::int32_t, const void*, std::uint32_t){fixture_host::unexpected("sdk_write_shared_memory");}
std::int32_t sdk_champion_restrictions(std::uint32_t, std::uint8_t*){fixture_host::unexpected("sdk_champion_restrictions");}
std::int32_t sdk_champion_items(std::uint32_t, std::uint32_t*){fixture_host::unexpected("sdk_champion_items");}
std::int32_t sdk_champion_consumables(std::uint32_t, std::uint32_t*){fixture_host::unexpected("sdk_champion_consumables");}
std::int32_t sdk_bc_champion_vision(std::uint32_t, std::uint64_t*){fixture_host::unexpected("sdk_bc_champion_vision");}
std::int32_t sdk_hypothetical_vision(std::int32_t, std::int32_t, std::int32_t, std::uint64_t*){fixture_host::unexpected("sdk_hypothetical_vision");}
std::int32_t sdk_champion_wards(std::uint32_t, ward_resources*){fixture_host::unexpected("sdk_champion_wards");}
std::int32_t sdk_champion_ward_positions(std::uint32_t, position*, std::uint32_t, std::uint32_t*){fixture_host::unexpected("sdk_champion_ward_positions");}
std::int32_t sdk_champion_can_shop(std::uint32_t, std::int32_t, std::int32_t*){fixture_host::unexpected("sdk_champion_can_shop");}
std::int32_t sdk_champion_can_sell(std::uint32_t, std::int32_t*){fixture_host::unexpected("sdk_champion_can_sell");}
std::int32_t sdk_wards(ward_info*, std::uint32_t, std::uint32_t*){fixture_host::unexpected("sdk_wards");}
std::int32_t sdk_map_num_camp_groups(std::uint32_t*){fixture_host::unexpected("sdk_map_num_camp_groups");}
std::int32_t sdk_map_camp_groups(std::uint32_t*, std::uint32_t, std::uint32_t*){fixture_host::unexpected("sdk_map_camp_groups");}
std::int32_t sdk_map_camp_group_num_camps(std::uint32_t, std::uint32_t*){fixture_host::unexpected("sdk_map_camp_group_num_camps");}
std::int32_t sdk_map_camp_group_camps(std::uint32_t, std::uint32_t*, std::uint32_t, std::uint32_t*){fixture_host::unexpected("sdk_map_camp_group_camps");}
std::int32_t sdk_map_camp_group(std::uint32_t, camp_group_info*){fixture_host::unexpected("sdk_map_camp_group");}
std::int32_t sdk_map_camp_info(std::uint32_t, camp_info*){fixture_host::unexpected("sdk_map_camp_info");}
std::int32_t sdk_map_camp_group_of(std::uint32_t, std::uint32_t*){fixture_host::unexpected("sdk_map_camp_group_of");}
std::int32_t sdk_map_camp_at(std::int32_t, std::int32_t, std::uint32_t*){fixture_host::unexpected("sdk_map_camp_at");}
std::int32_t sdk_camp_group_cooldown(std::uint32_t, std::int32_t*){fixture_host::unexpected("sdk_camp_group_cooldown");}
std::int32_t sdk_camp_group_state(std::uint32_t, std::uint32_t*){fixture_host::unexpected("sdk_camp_group_state");}
std::int32_t sdk_camp_buffs(std::uint32_t, camp_buff_data*, std::uint32_t, std::uint32_t*){fixture_host::unexpected("sdk_camp_buffs");}
std::int32_t sdk_bc_camp_group_max_respawn(std::uint32_t, std::int32_t*){fixture_host::unexpected("sdk_bc_camp_group_max_respawn");}
std::int32_t sdk_has_camp(std::int32_t, std::int32_t, std::int32_t*){fixture_host::unexpected("sdk_has_camp");}
std::int32_t sdk_has_champion(std::int32_t, std::int32_t, std::int32_t*){fixture_host::unexpected("sdk_has_champion");}
std::int32_t sdk_vision(std::uint64_t*){fixture_host::unexpected("sdk_vision");}
std::int32_t sdk_spawn_region(std::int32_t side,aabb*out){assert(side==0||side==1);*out=side==0?aabb{0,0,1,1}:aabb{15,7,16,8};return 0;}
std::int32_t sdk_draft_num_rounds(std::uint8_t*){fixture_host::unexpected("sdk_draft_num_rounds");}
std::int32_t sdk_draft_get(std::int32_t, std::uint32_t, std::int32_t*){fixture_host::unexpected("sdk_draft_get");}
std::int32_t sdk_draft_champion(std::int32_t){fixture_host::unexpected("sdk_draft_champion");}
std::int32_t sdk_bc_submit_draft_memory(const void*, std::size_t){fixture_host::unexpected("sdk_bc_submit_draft_memory");}
std::int32_t sdk_read_draft_memory(std::int32_t, void*, std::uint32_t){fixture_host::unexpected("sdk_read_draft_memory");}
std::int32_t sdk_debug_dot(std::int32_t, std::int32_t, std::uint32_t){fixture_host::unexpected("sdk_debug_dot");}
std::int32_t sdk_debug_line(std::int32_t, std::int32_t, std::int32_t, std::int32_t, std::uint32_t){fixture_host::unexpected("sdk_debug_line");}
std::int32_t sdk_Bitboard_One(std::uint64_t*){fixture_host::unexpected("sdk_Bitboard_One");}
std::int32_t sdk_Bitboard_ImportRawBuffer(std::uint64_t*, const std::uint8_t*, std::size_t){fixture_host::unexpected("sdk_Bitboard_ImportRawBuffer");}
std::int32_t sdk_Bitboard_ImportLinearBuffer(std::uint64_t*, const std::uint8_t*, std::size_t){fixture_host::unexpected("sdk_Bitboard_ImportLinearBuffer");}
std::int32_t sdk_Bitboard_ExportRawBuffer(const std::uint64_t*, std::uint8_t*, std::size_t){fixture_host::unexpected("sdk_Bitboard_ExportRawBuffer");}
std::int32_t sdk_Bitboard_ExportLinearBuffer(const std::uint64_t*, std::uint8_t*, std::size_t){fixture_host::unexpected("sdk_Bitboard_ExportLinearBuffer");}
std::int32_t sdk_Bitboard_Print(const std::uint64_t*){fixture_host::unexpected("sdk_Bitboard_Print");}
std::int32_t sdk_Bitboard_Set(std::uint64_t* buffer,std::int32_t x,std::int32_t y,std::int32_t value){assert(value==0||value==1);fixture_host::set(buffer,x,y,value!=0);return 0;}
std::int32_t sdk_Bitboard_Toggle(std::uint64_t*, std::int32_t, std::int32_t){fixture_host::unexpected("sdk_Bitboard_Toggle");}
std::int32_t sdk_Bitboard_NumSetBits(const std::uint64_t*buffer,std::uint32_t*value){*value=0;for(int i=0;i<2;++i)for(auto word=buffer[i];word;word&=word-1)++*value;return 0;}
std::int32_t sdk_Bitboard_Not(const std::uint64_t*, std::uint64_t*){fixture_host::unexpected("sdk_Bitboard_Not");}
std::int32_t sdk_Bitboard_Xor(const std::uint64_t*, const std::uint64_t*, std::uint64_t*){fixture_host::unexpected("sdk_Bitboard_Xor");}
std::int32_t sdk_Bitboard_IsEqual(const std::uint64_t*, const std::uint64_t*, std::int32_t*){fixture_host::unexpected("sdk_Bitboard_IsEqual");}
std::int32_t sdk_Bitboard_ShiftRight(const std::uint64_t*, std::uint64_t*, std::int32_t){fixture_host::unexpected("sdk_Bitboard_ShiftRight");}
std::int32_t sdk_Bitboard_ShiftUp(const std::uint64_t*, std::uint64_t*, std::int32_t){fixture_host::unexpected("sdk_Bitboard_ShiftUp");}
std::int32_t sdk_Bitboard_Reflect(const std::uint64_t*, std::uint64_t*, std::int32_t){fixture_host::unexpected("sdk_Bitboard_Reflect");}
std::int32_t sdk_Bitboard_DrawCircle(std::uint64_t*, std::int32_t, std::int32_t, float){fixture_host::unexpected("sdk_Bitboard_DrawCircle");}
std::int32_t sdk_Bitboard_ReflectHorizontalPosition(std::int32_t, std::int32_t, std::int32_t*, std::int32_t*){fixture_host::unexpected("sdk_Bitboard_ReflectHorizontalPosition");}
std::int32_t sdk_Bitboard_ReflectVerticalPosition(std::int32_t, std::int32_t, std::int32_t*, std::int32_t*){fixture_host::unexpected("sdk_Bitboard_ReflectVerticalPosition");}
std::int32_t sdk_Bitboard_Rotate180Position(std::int32_t, std::int32_t, std::int32_t*, std::int32_t*){fixture_host::unexpected("sdk_Bitboard_Rotate180Position");}
std::int32_t sdk_Bitboard_ReachableMask(const std::uint64_t*, std::int32_t, std::int32_t, std::uint64_t*, std::uint16_t){fixture_host::unexpected("sdk_Bitboard_ReachableMask");}
std::int32_t sdk_Bitboard_ShadowCast(const std::uint64_t*, std::int32_t, std::int32_t, std::uint64_t*, std::uint16_t){fixture_host::unexpected("sdk_Bitboard_ShadowCast");}
std::int32_t sdk_Bitboard_ReachableMaskWithScratch(const std::uint64_t*, std::int32_t, std::int32_t, std::uint64_t*, std::uint16_t, std::uint64_t*, std::uint64_t*){fixture_host::unexpected("sdk_Bitboard_ReachableMaskWithScratch");}
std::int32_t sdk_DistanceField_ImportRawBuffer(std::uint16_t*, const std::uint8_t*, std::size_t){fixture_host::unexpected("sdk_DistanceField_ImportRawBuffer");}
std::int32_t sdk_DistanceField_ImportLinearBuffer(std::uint16_t*, const std::uint8_t*, std::size_t){fixture_host::unexpected("sdk_DistanceField_ImportLinearBuffer");}
std::int32_t sdk_DistanceField_ExportRawBuffer(const std::uint16_t*, std::uint8_t*, std::size_t){fixture_host::unexpected("sdk_DistanceField_ExportRawBuffer");}
std::int32_t sdk_DistanceField_ExportLinearBuffer(const std::uint16_t*, std::uint8_t*, std::size_t){fixture_host::unexpected("sdk_DistanceField_ExportLinearBuffer");}
std::int32_t sdk_DistanceField_Print(const std::uint16_t*){fixture_host::unexpected("sdk_DistanceField_Print");}
std::int32_t sdk_DistanceField_Get(const std::uint16_t*, std::int32_t, std::int32_t, std::uint16_t*){fixture_host::unexpected("sdk_DistanceField_Get");}
std::int32_t sdk_DistanceField_Set(std::uint16_t*, std::int32_t, std::int32_t, std::uint16_t){fixture_host::unexpected("sdk_DistanceField_Set");}
std::int32_t sdk_DistanceField_Copy(const std::uint16_t*, std::uint16_t*){fixture_host::unexpected("sdk_DistanceField_Copy");}
std::int32_t sdk_DistanceField_CopyWithMask(const std::uint16_t*, const std::uint64_t*, std::uint16_t*){fixture_host::unexpected("sdk_DistanceField_CopyWithMask");}
std::int32_t sdk_DistanceField_ComputeWithExpansionCount(const std::uint64_t*, const std::uint64_t*, std::uint64_t*, std::uint16_t*, std::uint16_t, std::uint16_t*){fixture_host::unexpected("sdk_DistanceField_ComputeWithExpansionCount");}
std::int32_t sdk_DistanceField_ComputeToWithExpansionCount(const std::uint64_t*, const std::uint64_t*, const std::uint64_t*, std::uint64_t*, std::uint16_t*, std::uint16_t, std::uint16_t*){fixture_host::unexpected("sdk_DistanceField_ComputeToWithExpansionCount");}
}
}

namespace h12_bounded_interceptor {
struct RuntimeCollateralFixture {
    static void run(bool collateral){
        fixture_host::reset(collateral);Bot bot{};
        const auto ctor=cambc::detail::handle_access::make_champion(static_cast<cambc::handle_champion_t>(5));
        assert(ctor.abilities()[0].is_aoe_damage());
        assert(bot.incumbent_==-1&&!bot.resume_.owed()&&!bot.walked_&&!bot.prompt_bank_&&!bot.intercepted_);
        bot.initialize();bot.snapshot();bot.safety();
        assert(bot.identity_==0&&bot.fresh_.bag==0&&bot.fresh_.hp==1200&&bot.nearby_incoming_==0);
        assert(bot.sources_.size()==2&&bot.sources_[1].pads.size()==1);
        assert(bot.sources_[1].pads[0]==vec2(4,collateral?3:4));
        const auto ability=cambc::self().abilities()[0];
        assert(ability.is_aoe_damage()&&!ability.is_cc()&&ability.is_ready());
        const auto area=ability.get<cambc::ability_aoe_damage>();assert(area.range==4&&area.radius==1&&area.duration==1);
        const auto enemy=cambc::detail::handle_access::make_champion(static_cast<cambc::handle_champion_t>(3));
        assert(ability.valid_tiles().is_set(enemy.position())&&!cambc::self().attack_tiles().is_set(enemy.position()));
        const bool attempted=bot.try_intercept();auto&s=fixture_host::scene;
        assert(s.layouts==1&&s.channels>0&&s.readiness>0&&s.spell_props>0&&s.champion_queries>0);
        if(!collateral){
            assert(attempted&&s.casts==1&&!s.channel&&s.enemy_hp==360&&s.cooldown==8&&bot.resume_.owed());
            assert(bot.intercepted_&&bot.action_kind_==9&&s.camp_basics==0&&s.damage_queries==1);
        }else{
            assert(!attempted&&s.casts==0&&s.channel&&s.enemy_hp==460&&s.cooldown==0&&!bot.resume_.owed());
            assert(!bot.intercepted_&&s.damage_queries==0);
            // Invoke the actual ordinary action/HP-receipt helper, not a
            // synthetic Production effect or copied selector. This does not
            // claim complete fallback routing, source selection or payment.
            assert(bot.normal_fight(bot.sources_[0].group.camps()[0],false));
            assert(s.camp_basics==1&&s.camp_hp==0&&s.casts==0&&s.channel&&!bot.resume_.owed());
        }
        assert(s.enemy_basics==0);
        std::printf("runtime collateral=%d attempted=%d enemy_casts=%d enemy_hp=%.0f recall=%d owed=%d ordinary_camp_basics=%d ordinary_camp_hp=%.0f\n",
            collateral,attempted,s.casts,s.enemy_hp,s.channel,bot.resume_.owed(),s.camp_basics,s.camp_hp);
    }
};
}

int main(){
    guarded_channels();candidate_cases();payload_cases();interpreter_cases();execution_cases();
    resume_cases();inherited_lifecycle_cases();price_cases();
    bi::RuntimeCollateralFixture::run(false);bi::RuntimeCollateralFixture::run(true);
    std::puts("bounded interceptor production interpreter, action receipts and later-work latch pass");
}
