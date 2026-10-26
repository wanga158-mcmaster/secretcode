#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

// Conditional OWN action words, not SDK authority or a combat policy. Providers
// are bounded PURE lookups over nominated geometry/state; they must never issue
// SDK floods from hypothetical poses. Observed permission belongs only to the
// root observation. Every hypothetical suffix needs an explicit Derived
// permission witness for that exact prefix (range, path, visibility, target
// categories and occupancy). No permission proves a hidden tile empty or that
// a native Boolean attempt will succeed. Native execution must refresh after
// EVERY accepted primitive, stop on refusal/stale epoch, and freshly nominate
// after the indicated kill/geometry barrier. Effects below are conditional on
// all preceding primitives actually succeeding; no predicted effect is a receipt.
//
// The admitted effect class has stationary supplied damage bounds during this
// owner input, except the explicitly modeled any-kill cooldown reset. Reactive
// damage, pulls, summons, healing, consumables, channels, movement refreshes and
// unrepresented on-kill changes are outside this family. Bounded nominations,
// templates and truncation make no exhaustive/optimal/minimax claim.
namespace wb::capital::word {
inline constexpr unsigned max_targets=6,max_steps=4,max_words=16,max_candidates=8,max_effects=16;
inline constexpr unsigned camp=1,enemy=2,self=8,ground=16;
struct Point {int x=0,y=0;};
inline bool operator==(Point a,Point b){return a.x==b.x&&a.y==b.y;}
inline unsigned distance(Point a,Point b){return static_cast<unsigned>(std::max(std::abs(a.x-b.x),std::abs(a.y-b.y)));}
struct Interval {float lower=0,upper=0;};
struct Target {
    int id=-1;unsigned kind=enemy;Point position{};
    Interval hp{},shield{};
    bool observed=false,stun_immune=false;
    int stun_inputs=0;
};
struct State {
    int owner=-1,round=-1;std::uint64_t generation=0;unsigned prefix=0,geometry_revision=0;
    Point position{};
    bool active=false,m=false,a=false,c=false,walked=false,exclusive_cast=false,cast_used=false,enhanced=false;
    bool kill_barrier=false;
    std::array<int,2> cooldown{};
    Interval self_shield{};
    std::array<Target,max_targets> targets{};unsigned target_count=0;
};
enum class AbilityKind {None,Reset,Damage,Shield,Dash};
struct Ability {
    AbilityKind kind=AbilityKind::None;
    int max_cooldown=8;
    float range=0,radius=0;
    unsigned target_mask=0,affect_mask=camp|enemy;
    bool excludes_walk=false,area=false,enhanced_reset=false;
    int stun_inputs=0;
    std::array<Interval,max_targets> damage{};
    // A guaranteed post-cast shield interval, NOT an assumed additive refresh.
    Interval shield_after{};
};
struct Rules {
    bool ranged=false,any_kill_refresh=false,kill_effects_known=false;
    unsigned move_speed=1,attack_range=1;
    float basic_splash_radius=0;
    int enhanced_basic_stun=0;
    std::array<Interval,max_targets> basic_damage{};
    std::array<Ability,2> abilities{};
};
struct Input {
    State initial{};Rules rules{};
    bool fresh=false,owner_action_phase=false,effects_complete=false;
    // Explicit caller admission of this word's immediate owner-effect class:
    // no unmodeled retaliation, owner death, stat/kit changes or other owner
    // effects can truncate/alter the promised prefix. effects_complete alone
    // does not establish this. Unknown interactions retain another fallback.
    bool owner_effects_closed=false;
};
enum class Kind {Move,Basic,Reset,Spell,Shield,Dash};
enum class Permission {Missing,Observed,Derived,Blocked};
struct Candidate {
    Kind kind=Kind::Basic;int slot=-1,primary=-1;Point destination{};
    Permission permission=Permission::Missing;
    // Exact dependency value, not a potentially colliding fingerprint. The
    // generation also binds immutable Rules/equipment to the observed scene.
    State before{};
    bool emit=true;
};
inline Candidate bind(const State& state,Kind kind,int slot=-1,int primary=-1,Point destination={},
                      Permission permission=Permission::Derived) {
    Candidate c;c.kind=kind;c.slot=slot;c.primary=primary;c.destination=destination;c.permission=permission;c.before=state;return c;
}
enum class EffectKind {DamageTarget,StunTarget,ShieldSelf};
struct Effect {EffectKind kind=EffectKind::DamageTarget;unsigned target=0;Interval amount{};int stun_inputs=0;};
struct Trace {
    Candidate candidate{};
    unsigned effect_begin=0,effect_count=0;
    bool requires_actual_refresh=true,may_refuse=true,renomination_before=false,kill_barrier_after=false;
};
enum class Reason {None,InvalidInput,Inactive,StaleDependency,PermissionMissing,PermissionBlocked,
    ObservedSuffix,Resource,WrongAbility,Target,Range,WalkExcluded,UnknownKillEffects,EffectCapacity,InvalidWord,UnsupportedClass};
struct Transition {
    bool admitted=false;Reason reason=Reason::None;State after{};
    std::array<Effect,max_effects> effects{};unsigned effect_count=0;
    bool requires_renomination=false;
};
struct Word {
    std::array<Trace,max_steps> trace{};unsigned count=0;
    std::array<Effect,max_effects> effects{};unsigned effect_count=0;
    State after{};
    bool conditional=true,effects_complete=false,requires_renomination=false;
};
struct Menu {
    std::array<Candidate,max_candidates> candidates{};unsigned count=0;
    bool partial=false; // supplied nominations omit other legal operator families
};
using Provider=Menu(*)(const State&,void*);
struct Limits {unsigned transitions=64,words=max_words,depth=max_steps;};
struct Pattern {std::array<Kind,max_steps> kinds{};unsigned count=0;};
struct Result {
    std::array<Word,max_words> words{};unsigned count=0,attempted=0,rejected=0;
    bool admitted=false,transition_limit=false,word_limit=false,depth_limit=false,effect_limit=false,provider_partial=false;
    Reason reason=Reason::None;
};
namespace detail {
inline bool finite(float x){return std::isfinite(x)&&x>=0&&x<=1000000000.f;}
inline bool valid(Interval x){return finite(x.lower)&&finite(x.upper)&&x.lower<=x.upper;}
inline bool same(Interval a,Interval b){return a.lower==b.lower&&a.upper==b.upper;}
inline bool inside(Point p){return p.x>=0&&p.y>=0&&p.x<=100&&p.y<=100;}
inline bool same(const Target& a,const Target& b) {
    return a.id==b.id&&a.kind==b.kind&&a.position==b.position&&same(a.hp,b.hp)&&same(a.shield,b.shield)
        &&a.observed==b.observed&&a.stun_immune==b.stun_immune&&a.stun_inputs==b.stun_inputs;
}
inline bool same(const State& a,const State& b) {
    if(a.owner!=b.owner||a.round!=b.round||a.generation!=b.generation||a.prefix!=b.prefix
       ||a.geometry_revision!=b.geometry_revision||!(a.position==b.position)||a.active!=b.active||a.m!=b.m||a.a!=b.a||a.c!=b.c
       ||a.walked!=b.walked||a.exclusive_cast!=b.exclusive_cast||a.cast_used!=b.cast_used||a.enhanced!=b.enhanced
       ||a.kill_barrier!=b.kill_barrier||a.cooldown!=b.cooldown||!same(a.self_shield,b.self_shield)||a.target_count!=b.target_count)return false;
    for(unsigned i=0;i<a.target_count;++i)if(!same(a.targets[i],b.targets[i]))return false;
    return true;
}
inline bool valid(const Input& in) {
    const auto& s=in.initial;const auto& r=in.rules;
    if(!in.fresh||!in.owner_action_phase||s.owner<0||s.round<0||s.generation==0||s.prefix!=0||s.target_count>max_targets
       ||!inside(s.position)||!valid(s.self_shield)||(s.cast_used&&s.c)||r.move_speed>10||r.attack_range>20
       ||!finite(r.basic_splash_radius)||r.basic_splash_radius>20||r.enhanced_basic_stun<0||r.enhanced_basic_stun>100)return false;
    for(unsigned i=0;i<s.target_count;++i) {
        const auto& t=s.targets[i];
        if(t.id<0||(t.kind!=camp&&t.kind!=enemy)||!inside(t.position)||!valid(t.hp)||!valid(t.shield)
           ||t.stun_inputs<0||t.stun_inputs>100||!valid(r.basic_damage[i]))return false;
        for(unsigned j=0;j<i;++j)if(t.id==s.targets[j].id&&t.kind==s.targets[j].kind)return false;
    }
    for(unsigned i=0;i<2;++i) {
        const auto& a=r.abilities[i];
        if(s.cooldown[i]<0||s.cooldown[i]>100||static_cast<unsigned>(a.kind)>static_cast<unsigned>(AbilityKind::Dash)
           ||a.max_cooldown<0||a.max_cooldown>100||!finite(a.range)||a.range>20||!finite(a.radius)||a.radius>20
           ||a.stun_inputs<0||a.stun_inputs>100||!valid(a.shield_after))return false;
        for(unsigned j=0;j<s.target_count;++j)if(!valid(a.damage[j]))return false;
    }
    return true;
}
inline void hit(float damage,float& hp,float& shield) {
    const float absorbed=std::min(shield,damage);
    shield=std::max(0.f,shield-damage);hp=std::max(0.f,hp-std::max(0.f,damage-absorbed));
}
inline bool live(const State& s,int index) {
    return index>=0&&index<static_cast<int>(s.target_count)&&s.targets[index].observed&&s.targets[index].hp.lower>0;
}
inline bool valid_prefix(const Input& in,const State& s) {
    const auto& root=in.initial;
    if(s.owner!=root.owner||s.round!=root.round||s.generation!=root.generation||s.target_count!=root.target_count
       ||s.active!=root.active||!inside(s.position)||!valid(s.self_shield)||(s.cast_used&&s.c))return false;
    if(s.prefix==0)return same(s,root);
    for(unsigned slot=0;slot<2;++slot)if(s.cooldown[slot]<0||s.cooldown[slot]>100)return false;
    for(unsigned i=0;i<s.target_count;++i) {
        const auto& t=s.targets[i];const auto& original=root.targets[i];
        if(t.id!=original.id||t.kind!=original.kind||!(t.position==original.position)||t.observed!=original.observed
           ||t.stun_immune!=original.stun_immune||!valid(t.hp)||!valid(t.shield)||t.hp.upper>original.hp.upper
           ||t.shield.upper>original.shield.upper||t.stun_inputs<0||t.stun_inputs>100)return false;
    }
    return true;
}
inline bool append(Transition& out,Effect effect) {
    if(out.effect_count==max_effects){out.reason=Reason::EffectCapacity;return false;}
    out.effects[out.effect_count++]=effect;return true;
}
inline bool known_occupied(const State& state,Point destination) {
    for(unsigned i=0;i<state.target_count;++i)if(state.targets[i].observed&&state.targets[i].hp.upper>0
        &&state.targets[i].position==destination)return true;
    return false;
}
inline bool damage(Transition& out,unsigned index,Interval amount,int stun) {
    auto& target=out.after.targets[index];if(target.hp.upper<=0)return true;
    const bool was_definitely_alive=target.hp.lower>0;
    if(!append(out,{EffectKind::DamageTarget,index,amount,0}))return false;
    hit(amount.upper,target.hp.lower,target.shield.lower);
    hit(amount.lower,target.hp.upper,target.shield.upper);
    // Do not promise a live-target buff after lethal or possibly lethal damage.
    // Omitting uncertain CC is conservative for the later reply envelope.
    if(stun>0&&target.hp.lower>0&&!target.stun_immune) {
        target.stun_inputs=stun;
        if(!append(out,{EffectKind::StunTarget,index,{},stun}))return false;
    }
    if(was_definitely_alive&&target.hp.lower<=0) {
        ++out.after.geometry_revision;out.after.kill_barrier=true;out.requires_renomination=true;
    }
    return true;
}
}
// Standalone transition. All effects and positions are predictions CONDITIONAL
// on this attempted native primitive being accepted. next_prefix is unique
// within the caller's planning invocation; zero requests the next path ordinal.
[[nodiscard]] inline Transition apply_candidate(const Input& in,const State& before,const Candidate& c,unsigned next_prefix=0) {
    Transition out;out.after=before;
    if(!in.owner_effects_closed){out.reason=Reason::UnsupportedClass;return out;}
    if(!detail::valid(in)||!detail::valid_prefix(in,before)){out.reason=Reason::InvalidInput;return out;}
    if(!before.active){out.reason=Reason::Inactive;return out;}
    if(!detail::same(before,c.before)){out.reason=Reason::StaleDependency;return out;}
    if(c.permission==Permission::Missing){out.reason=Reason::PermissionMissing;return out;}
    if(c.permission==Permission::Blocked){out.reason=Reason::PermissionBlocked;return out;}
    if(c.permission!=Permission::Observed&&c.permission!=Permission::Derived){out.reason=Reason::PermissionMissing;return out;}
    if(c.permission==Permission::Observed&&before.prefix!=0){out.reason=Reason::ObservedSuffix;return out;}
    if(before.kill_barrier&&!in.rules.kill_effects_known){out.reason=Reason::UnknownKillEffects;return out;}
    const unsigned successor=next_prefix?next_prefix:before.prefix+1;
    if(successor<=before.prefix||successor>64){out.reason=Reason::InvalidWord;return out;}
    const auto& r=in.rules;
    const auto reject=[&](Reason why){out.reason=why;out.after=before;out.effect_count=0;out.requires_renomination=false;return out;};
    if(c.kind==Kind::Move) {
        if(!before.m||before.exclusive_cast)return reject(Reason::Resource);
        if(!detail::inside(c.destination)||c.destination==before.position||distance(before.position,c.destination)>r.move_speed)return reject(Reason::Range);
        if(detail::known_occupied(before,c.destination))return reject(Reason::Target);
        out.after.position=c.destination;out.after.m=false;out.after.walked=true;if(r.ranged)out.after.a=false;
    } else if(c.kind==Kind::Basic) {
        if(!before.a)return reject(Reason::Resource);
        if(!detail::live(before,c.primary))return reject(Reason::Target);
        const auto primary=static_cast<unsigned>(c.primary);
        if(distance(before.position,before.targets[primary].position)>r.attack_range)return reject(Reason::Range);
        for(unsigned i=0;i<before.target_count;++i)if(i==primary||(r.basic_splash_radius>0&&
            distance(before.targets[i].position,before.targets[primary].position)<=r.basic_splash_radius)) {
            if(!before.targets[i].observed)return reject(Reason::Target);
            if(!detail::damage(out,i,r.basic_damage[i],before.enhanced?r.enhanced_basic_stun:0))return reject(out.reason);
        }
        out.after.a=false;if(r.ranged)out.after.m=false;out.after.enhanced=false;
    } else {
        if(c.slot<0||c.slot>=2)return reject(Reason::WrongAbility);
        const auto slot=static_cast<unsigned>(c.slot);const auto& a=r.abilities[slot];
        if(!before.c||before.cast_used||before.cooldown[slot]!=0)return reject(Reason::Resource);
        if(a.excludes_walk&&before.walked)return reject(Reason::WalkExcluded);
        if(c.kind==Kind::Reset) {
            if(a.kind!=AbilityKind::Reset||!(a.target_mask&self))return reject(Reason::WrongAbility);
            out.after.a=true;if(a.enhanced_reset)out.after.enhanced=true;
        } else if(c.kind==Kind::Shield) {
            if(a.kind!=AbilityKind::Shield||!(a.target_mask&self))return reject(Reason::WrongAbility);
            out.after.self_shield=a.shield_after;
            if(!detail::append(out,{EffectKind::ShieldSelf,0,a.shield_after,0}))return reject(out.reason);
        } else if(c.kind==Kind::Dash||c.kind==Kind::Spell) {
            const bool dash=c.kind==Kind::Dash;
            if(a.kind!=(dash?AbilityKind::Dash:AbilityKind::Damage))return reject(Reason::WrongAbility);
            Point center=c.destination;
            if(dash) {
                if(!(a.target_mask&ground)||center==before.position||!detail::inside(center)||distance(before.position,center)>a.range)return reject(Reason::Range);
                if(detail::known_occupied(before,center))return reject(Reason::Target);
                out.after.position=center;
            } else if(c.primary>=0) {
                if(!detail::live(before,c.primary))return reject(Reason::Target);
                const auto& target=before.targets[c.primary];
                if(!(a.target_mask&target.kind)||!(center==target.position))return reject(Reason::Target);
            } else if(!(a.target_mask&ground)||!a.area)return reject(Reason::Target);
            if(!detail::inside(center)||distance(before.position,center)>a.range)return reject(Reason::Range);
            for(unsigned i=0;i<before.target_count;++i) {
                const bool affected=dash||a.area?distance(before.targets[i].position,center)<=a.radius:static_cast<int>(i)==c.primary;
                if(!affected||!(a.affect_mask&before.targets[i].kind))continue;
                if(!before.targets[i].observed)return reject(Reason::Target);
                if(!detail::damage(out,i,a.damage[i],a.stun_inputs))return reject(out.reason);
            }
        } else return reject(Reason::WrongAbility);
        out.after.c=false;out.after.cast_used=true;out.after.cooldown[slot]=a.max_cooldown;
        if(a.excludes_walk){out.after.m=false;out.after.exclusive_cast=true;}
    }
    bool definite_kill=false;
    for(unsigned i=0;i<before.target_count;++i)definite_kill|=before.targets[i].hp.lower>0&&out.after.targets[i].hp.upper<=0;
    if(definite_kill&&r.any_kill_refresh)out.after.cooldown={0,0}; // never restores the single cast
    out.after.prefix=successor;
    out.admitted=true;return out;
}
namespace detail {
inline bool extend(Word& word,const Candidate& candidate,const Transition& step) {
    if(word.count>=max_steps||word.effect_count+step.effect_count>max_effects)return false;
    auto& trace=word.trace[word.count++];trace.candidate=candidate;trace.effect_begin=word.effect_count;trace.effect_count=step.effect_count;
    trace.renomination_before=candidate.before.kill_barrier;trace.kill_barrier_after=step.after.kill_barrier;
    trace.may_refuse=candidate.kind==Kind::Move||candidate.kind==Kind::Dash||candidate.permission==Permission::Derived;
    for(unsigned i=0;i<step.effect_count;++i)word.effects[word.effect_count++]=step.effects[i];
    word.after=step.after;word.requires_renomination|=step.requires_renomination||trace.renomination_before;return true;
}
}
struct Validation {bool admitted=false;Reason reason=Reason::None;unsigned rejected_at=0;Word word{};};
// Fixed concrete path. Candidate dependencies use root prefix0, then1,2,3.
[[nodiscard]] inline Validation validate_word(const Input& in,std::span<const Candidate> candidates) {
    Validation out;out.word.after=in.initial;out.word.effects_complete=in.effects_complete;
    if(candidates.empty()||candidates.size()>max_steps){out.reason=Reason::InvalidWord;return out;}
    for(unsigned i=0;i<candidates.size();++i) {
        const auto transition=apply_candidate(in,out.word.after,candidates[i],i+1);out.rejected_at=i;
        if(!transition.admitted){out.reason=transition.reason;return out;}
        if(!detail::extend(out.word,candidates[i],transition)){out.reason=Reason::EffectCapacity;return out;}
    }
    out.admitted=true;return out;
}
namespace detail {
struct Search {
    const Input& input;Provider provider;void* context;Limits limits;Result result{};unsigned serial=0;
    bool full() {
        if(result.attempted>=limits.transitions){result.transition_limit=true;return true;}
        if(result.count>=limits.words){result.word_limit=true;return true;}return false;
    }
    void visit(const Word& prefix,const Pattern* pattern) {
        if(full())return;
        if(prefix.count>=limits.depth){result.depth_limit=true;return;}
        const auto menu=provider(prefix.after,context);result.provider_partial|=menu.partial||menu.count>max_candidates;
        for(unsigned i=0;i<std::min(menu.count,max_candidates);++i) {
            const auto& candidate=menu.candidates[i];
            if(pattern&&candidate.kind!=pattern->kinds[prefix.count])continue;
            if(full())return;++result.attempted;
            const auto step=apply_candidate(input,prefix.after,candidate,++serial);
            if(!step.admitted){++result.rejected;continue;}
            auto next=prefix;
            if(!extend(next,candidate,step)){++result.rejected;result.effect_limit=true;result.provider_partial=true;continue;}
            const bool terminal=pattern?next.count==pattern->count:candidate.emit;
            if(terminal)result.words[result.count++]=next;
            if(pattern&&terminal)continue;
            if(next.count<limits.depth)visit(next,pattern);
            else if(!pattern)result.depth_limit=true;
        }
    }
};
inline bool valid(Limits l){return l.transitions>0&&l.transitions<=64&&l.words>0&&l.words<=max_words&&l.depth>0&&l.depth<=max_steps;}
}
[[nodiscard]] inline Result compile(const Input& in,Provider provider,void* context=nullptr,Limits limits={}) {
    if(!in.owner_effects_closed){Result out;out.reason=Reason::UnsupportedClass;return out;}
    if(!detail::valid(in)||!provider||!detail::valid(limits)){Result out;out.reason=Reason::InvalidInput;return out;}
    detail::Search search{in,provider,context,limits};search.result.admitted=true;
    Word root;root.after=in.initial;root.effects_complete=in.effects_complete;search.visit(root,nullptr);return search.result;
}
// Named families avoid spending the bounded nomination budget on arbitrary DFS
// prefixes. All patterns share ONE total transition/word budget, and only
// completed patterns are emitted. Omitted families remain explicitly partial.
[[nodiscard]] inline Result compile_templates(const Input& in,std::span<const Pattern> patterns,Provider provider,
                                              void* context=nullptr,Limits limits={}) {
    if(!in.owner_effects_closed){Result out;out.reason=Reason::UnsupportedClass;return out;}
    if(!detail::valid(in)||!provider||!detail::valid(limits)||patterns.empty()||patterns.size()>max_words) {
        Result out;out.reason=Reason::InvalidInput;return out;
    }
    detail::Search search{in,provider,context,limits};search.result.admitted=true;search.result.provider_partial=true;
    Word root;root.after=in.initial;root.effects_complete=in.effects_complete;
    for(const auto& pattern:patterns) {
        if(pattern.count==0||pattern.count>limits.depth){search.result.reason=Reason::InvalidWord;continue;}
        if(search.full())break;search.visit(root,&pattern);
    }
    return search.result;
}
}
