#pragma once
#include <cambc.h>
#include "capital_word_native.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Bounded native execution, not a safety or fuel certificate. A Scene must be
// newly prepared from the caller's ACTUAL current input. Owner/round equality
// cannot establish that same-input precondition. Predictions, old masks and
// effect intervals never authorize a primitive or become production receipts.
// After every host action attempt (including refusal/exception), invalidate the
// Scene. Only immutable (kind,id) bindings and proposed steps survive locally;
// all following permissions are freshly queried. The caller prices every
// possible partial endpoint, refusal, computation stop and opponent/NPC reply.
// Receipts/life sampling cover only the actual primary and one tracked camp,
// deduplicated. Collateral kills are not counted; every damaging word requests
// fresh nomination afterward. Fresh suffix M/A/C/CD never depend on kill counts.
namespace wb::capital::word_execute {
using cambc::vec2;
enum class Stop {Complete,InvalidWord,Stale,NeedFuel,Inactive,NotReady,TargetHidden,
    TargetDead,TargetChanged,OutOfRange,KnownBlocked,Unsupported,Refused,NeedNomination,DependencyChanged};
struct CampReceipt {
    int camp_id=-1;unsigned step=0;word::Kind primitive=word::Kind::Basic;
    float hp_before=0,hp_after=0,hp_damage=0;
    bool observed=false,death=false;
};
struct Outcome {
    bool attempted=false,acted=false,moved=false,killed=false,refused=false,renominate=false;
    bool scene_invalidated=false;
    unsigned attempted_steps=0,accepted_steps=0,kills=0,camp_receipt_count=0;
    float camp_hp_damage=0;
    std::array<CampReceipt,word::max_steps> camp_receipts{};
    Stop stop=Stop::InvalidWord;
    bool dependency_changed=false;
};
struct Hooks {
    void* context=nullptr;
    void (*movement)(void*,bool ordinary,vec2 before,vec2 after)=nullptr;
    // Bounded QUERY-ONLY check of the caller's external reply dependencies.
    // False grants no fallback/action authority. The caller must suppress old
    // quotes and freshly nominate before any further physical command.
    bool (*dependency)(void*)=nullptr;
};
struct Options {
    // Empirical reserves only; exact enabled native paths still need health.
    unsigned primitive_reserve=12000,dispatch_reserve=8000;
    // Default kill barrier retains self/physical continuations, then returns
    // offensive suffixes to the parent for a new actual nomination/risk quote.
    // Disabling it allows only bounded revalidation of the supplied exact
    // (kind,id), never a new target search or a new exposure certificate.
    bool stop_offense_after_kill=true;
};
namespace detail {
inline int owner(){return static_cast<int>(static_cast<cambc::handle_champion_t>(cambc::self()));}
inline bool finite(float x){return std::isfinite(x)&&x>=0;}
inline bool epoch(int who,int round){return owner()==who&&cambc::round_number()==static_cast<unsigned>(round);}
inline bool same(vec2 a,vec2 b){return a.x==b.x&&a.y==b.y;}
inline bool inside(vec2 p){return p.x>=0&&p.y>=0&&p.x<cambc::map::width()&&p.y<cambc::map::height();}
inline bool unavailable(int code) {
    return code==static_cast<int>(cambc::error_code::api_not_in_vision)
        ||code==static_cast<int>(cambc::error_code::api_not_found);
}
struct Self {
    cambc::detail::champion_info raw{};
    cambc::champion_state state{};
    cambc::buff_restrictions restrictions{};
    bool free=false;
};
inline Self self(int who) {
    Self s;cambc::detail::check(cambc::detail::sdk_champion(who,&s.raw));
    s.state=cambc::champion_state::from_bits_retain(s.raw.state);
    if(!s.state.contains(cambc::champion_state::alive))return s;
    std::uint8_t bits=0;cambc::detail::check(cambc::detail::sdk_champion_restrictions(who,&bits));
    s.restrictions=cambc::buff_restrictions::from_bits_retain(bits);
    s.free=s.state.contains(cambc::champion_state::alive)&&!s.state.contains(cambc::champion_state::completed_turn)
        &&!s.state.contains(cambc::champion_state::recalling)&&!s.state.contains(cambc::champion_state::submitting_gems)
        &&!s.state.contains(cambc::champion_state::teleporting)&&!cambc::self().buffs().has_stun();
    return s;
}
struct PostSelf {vec2 position{};bool alive=false;};
inline PostSelf decode_post_self(const cambc::detail::champion_info& raw) {
    return {{raw.x,raw.y},cambc::champion_state::from_bits_retain(raw.state).contains(cambc::champion_state::alive)};
}
inline PostSelf post_self(int who) {
    // Only these fields are consumed after an action. Restrictions/stun/MAC
    // are freshly queried by self() before any following primitive, not copied
    // from this endpoint/life receipt or from the old scene.
    cambc::detail::champion_info raw{};cambc::detail::check(cambc::detail::sdk_champion(who,&raw));
    return decode_post_self(raw);
}
struct InputIdentity {int owner=-1,round=-1;std::uint64_t generation=0;};
struct Intent {
    word::Kind kind=word::Kind::Basic;int slot=-1,primary=-1;word::Point destination{};
    InputIdentity input{};
};
inline Intent compact(const word::Candidate& c) {
    return {c.kind,c.slot,c.primary,c.destination,{c.before.owner,c.before.round,c.before.generation}};
}
inline bool bound(const Intent& intent,InputIdentity input) {
    return intent.input.owner==input.owner&&intent.input.round==input.round&&intent.input.generation==input.generation;
}
struct Target {
    bool observed=false,life_known=false,alive=false;
    vec2 position{};float hp=0;
};
inline Target target(word_native::Binding binding) {
    Target t;if(binding.id<0)return t;
    if(binding.kind==word::camp) {
        std::uint32_t life=0;const auto code=cambc::detail::sdk_camp_state(binding.id,&life);
        if(unavailable(code))return t;cambc::detail::check(code);
        if(life>2)throw std::runtime_error("invalid camp life in word executor");
        t.life_known=t.observed=true;t.alive=life!=0;
        if(!t.alive)return t; // actual visible dead state implies zero HP
        cambc::detail::camp_state raw{};const auto status=cambc::detail::sdk_camp(binding.id,&raw);
        if(unavailable(status)){t.observed=false;return t;}cambc::detail::check(status);
        t.position={raw.x,raw.y};t.hp=raw.health;
        if(!finite(t.hp)||t.hp<=0||!inside(t.position))throw std::runtime_error("invalid living camp in word executor");
    } else if(binding.kind==word::enemy) {
        std::int32_t living=0;cambc::detail::check(cambc::detail::sdk_champion_is_alive(binding.id,&living));
        t.life_known=true;t.alive=living!=0;if(!t.alive)return t;
        cambc::detail::champion_info raw{};const auto code=cambc::detail::sdk_champion(binding.id,&raw);
        if(unavailable(code))return t;cambc::detail::check(code);
        if(raw.team==cambc::detail::sdk_my_team())throw std::runtime_error("non-enemy word target");
        t.position={raw.x,raw.y};t.hp=raw.health;t.observed=true;
        if(!finite(t.hp)||t.hp<=0||!inside(t.position))throw std::runtime_error("invalid living enemy in word executor");
    }
    return t;
}
// At most two straight 8-neighbour steps, matching the native adapter's bounded
// nomination class. Static terrain is immutable; every dynamic occupancy query
// below is CURRENT. Hidden occupancy stays unknown and may cause host refusal.
inline Stop path(vec2 from,vec2 to,bool dash,int range) {
    const auto& g=word_native::geometry();
    if(!g.ready||!g.walking_static.valid()||!g.holes.valid()||!inside(to))return Stop::Unsupported;
    const int dx=to.x-from.x,dy=to.y-from.y,length=std::max(std::abs(dx),std::abs(dy));
    if(length<1||length>range)return Stop::OutOfRange;
    if(length>2||(dx&&dy&&std::abs(dx)!=std::abs(dy)))return Stop::Unsupported;
    const int sx=(dx>0)-(dx<0),sy=(dy>0)-(dy<0);
    for(int n=1;n<=length;++n) {
        const vec2 p{from.x+sx*n,from.y+sy*n};const auto q=word_native::point(p);
        if(g.walking_static.get(q)&&(!dash||!g.holes.get(q)))return Stop::KnownBlocked;
        if(cambc::vision::has_champion(p)||cambc::vision::has_camp(p)||cambc::vision::has_teleport(p))return Stop::KnownBlocked;
    }
    if(g.holes.get(word_native::point(to)))return Stop::KnownBlocked;
    return Stop::Complete;
}
inline float range(const cambc::detail::ability_props& p) {
    switch(static_cast<cambc::ability_kind>(p.kind)) {
    case cambc::ability_kind::damage:return p.payload.damage.range;
    case cambc::ability_kind::aoe_damage:return p.payload.aoe_damage.range;
    case cambc::ability_kind::cc:return p.payload.cc.range;
    case cambc::ability_kind::dash:return p.payload.dash.range;
    default:return 0;
    }
}
inline unsigned entity(unsigned kind) {
    return static_cast<unsigned>(kind==word::camp?cambc::detail::entity_kind::camp:cambc::detail::entity_kind::champion);
}
inline bool pure_dash_payload(const cambc::detail::ability_props& props) {
    return static_cast<cambc::ability_kind>(props.kind)==cambc::ability_kind::dash
        &&finite(props.payload.dash.modifier)&&props.payload.dash.modifier==0
        &&props.payload.dash.duration==0;
}
// Per-target zero intervals alone are insufficient when no target was captured.
// Only a known zero-damage, zero-CC native payload plus compatible captured
// rules can omit life sampling. Unknown or harmful variants remain sampled.
inline bool dash_needs_life_samples(const word::Ability& rule,
                                   const cambc::detail::ability_props* payload,unsigned targets) {
    if(rule.kind!=word::AbilityKind::Dash||rule.stun_inputs!=0||targets>word::max_targets
        ||!payload||!pure_dash_payload(*payload))return true;
    for(unsigned i=0;i<targets;++i)if(!word::detail::valid(rule.damage[i])||rule.damage[i].upper!=0)return true;
    return false;
}
// Run after actual action/HP/movement receipts have been retained. Acceptance
// is not a promise that a move/dash reached its proposed endpoint; no later
// step derived from that endpoint may run on a partial or stationary success.
inline bool reject_changed_endpoint(Outcome& out,word::Kind kind,bool accepted,vec2 proposed,vec2 actual) {
    if(!accepted||(kind!=word::Kind::Move&&kind!=word::Kind::Dash)||same(proposed,actual))return false;
    out.stop=Stop::TargetChanged;out.renominate=true;return true;
}
inline bool reject_changed_dependency(Outcome& out,bool holds) {
    if(holds)return false;
    out.dependency_changed=out.renominate=true;out.stop=Stop::DependencyChanged;return true;
}
}

inline Outcome execute(word_native::Scene& scene,const word::Word& supplied,
                       word_native::Provenance& history,Hooks hooks={},Options options={}) {
    Outcome out;
    if(!word_native::epoch_matches(scene)||!word::detail::valid(scene.input)||!scene.input.fresh||!scene.input.owner_action_phase
        ||!scene.input.owner_effects_closed||supplied.count==0||supplied.count>word::max_steps
        ||scene.input.initial.target_count>word::max_targets
        ||!word::detail::same(supplied.trace[0].candidate.before,scene.input.initial)
        ||history.walked!=scene.input.initial.walked||history.exclusive_cast!=scene.input.initial.exclusive_cast
        ||options.dispatch_reserve<5000||options.primitive_reserve<options.dispatch_reserve)return out;
    const detail::InputIdentity initial{scene.input.initial.owner,scene.input.initial.round,scene.input.initial.generation};
    const auto bindings=scene.bindings;const unsigned count=scene.input.initial.target_count;
    // Preserve immutable execution intents across invalidation/callbacks without
    // copying each candidate's complete six-target speculative State. Invalid
    // suffix binding still fails at its ORIGINAL step, after any actual prefix.
    std::array<detail::Intent,word::max_steps> intents{};
    for(unsigned step=0;step<supplied.count;++step)intents[step]=detail::compact(supplied.trace[step].candidate);
    // Immutable classification is captured before any action invalidates Scene.
    // The actual props are already re-queried for cast legality below; any
    // unexpected live harmful payload also retains conservative sampling.
    const auto dash_causes_damage=[&] {
        std::array<bool,2> flags{true,true};const auto& g=word_native::geometry();
        for(unsigned slot=0;slot<flags.size();++slot)
            flags[slot]=detail::dash_needs_life_samples(scene.input.rules.abilities[slot],
                g.identity_checked?&g.design.definition.abilities[slot]:nullptr,count);
        return flags;
    }();
    const int tracked=scene.camp_index;
    if(tracked<-1||tracked>=static_cast<int>(count)||(tracked>=0&&bindings[tracked].kind!=word::camp))return out;
    for(unsigned i=0;i<count;++i)if(bindings[i].id!=scene.input.initial.targets[i].id||bindings[i].kind!=scene.input.initial.targets[i].kind)
        return out;
    bool movement_attempted=false,dependency_current=false;
    const auto dependencies_hold=[&]() {
        if(!hooks.dependency)return true;
        const bool holds=hooks.dependency(hooks.context);
        const bool changed=detail::reject_changed_dependency(out,holds);
        if(changed){word_native::invalidate(scene);out.scene_invalidated=true;}
        // A callback may suspend while querying. Its Boolean never replaces
        // actual input/fuel fences, even when its own dependency check passed.
        if(!detail::epoch(initial.owner,initial.round)){out.stop=Stop::Stale;out.renominate=true;return false;}
        if(changed)return false;
        if(cambc::fuel_remaining()<options.dispatch_reserve){out.stop=Stop::NeedFuel;return false;}
        dependency_current=true;return true;
    };
    for(unsigned step=0;step<supplied.count;++step) {
        const auto& candidate=intents[step];
        if(!detail::bound(candidate,initial)){out.stop=Stop::InvalidWord;break;}
        if(out.killed&&options.stop_offense_after_kill&&
            (candidate.kind==word::Kind::Basic||candidate.kind==word::Kind::Spell)) {
            out.stop=Stop::NeedNomination;out.renominate=true;break;
        }
        if(!detail::epoch(initial.owner,initial.round)){word_native::invalidate(scene);out.scene_invalidated=true;out.stop=Stop::Stale;break;}
        if(cambc::fuel_remaining()<options.primitive_reserve){out.stop=Stop::NeedFuel;break;}
        const auto own=detail::self(initial.owner);const vec2 origin{own.raw.x,own.raw.y};
        if(!own.free){out.stop=Stop::Inactive;break;}
        word_native::Binding primary{};detail::Target victim;
        const bool targeted=candidate.kind==word::Kind::Basic||(candidate.kind==word::Kind::Spell&&candidate.primary>=0);
        if(targeted) {
            if(candidate.primary<0||candidate.primary>=static_cast<int>(count)){out.stop=Stop::InvalidWord;break;}
            primary=bindings[candidate.primary];victim=detail::target(primary);
            if(victim.life_known&&!victim.alive){out.stop=Stop::TargetDead;out.renominate=true;break;}
            if(!victim.observed){out.stop=Stop::TargetHidden;out.renominate=true;break;}
        }
        bool cast=false,exclusive=false;vec2 destination{candidate.destination.x,candidate.destination.y};
        cambc::detail::ability_props props{};
        if(candidate.kind==word::Kind::Move) {
            if(!own.state.contains(cambc::champion_state::can_move)||own.restrictions.contains(cambc::buff_restrictions::move)
                ||history.exclusive_cast){out.stop=Stop::NotReady;break;}
            const auto legal=detail::path(origin,destination,false,own.raw.move_speed);
            if(legal!=Stop::Complete){out.stop=legal;break;}
        } else if(candidate.kind==word::Kind::Basic) {
            if(!own.state.contains(cambc::champion_state::can_attack)||own.restrictions.contains(cambc::buff_restrictions::attack))
                {out.stop=Stop::NotReady;break;}
            if(origin.chebyshev_distance(victim.position)>own.raw.attack_range){out.stop=Stop::OutOfRange;break;}
        } else {
            if(candidate.slot<0||candidate.slot>=2){out.stop=Stop::InvalidWord;break;}
            cast=true;cambc::detail::check(cambc::detail::sdk_champion_ability_props(initial.owner,candidate.slot,&props));
            if(props.max_charges!=0){out.stop=Stop::Unsupported;break;}
            std::int32_t cooldown=0;cambc::detail::check(cambc::detail::sdk_champion_cooldown(initial.owner,candidate.slot,&cooldown));
            exclusive=(props.attributes&cambc::detail::ability_attribute_exclusive_with_movement)!=0;
            if(!own.state.contains(cambc::champion_state::can_cast)||own.restrictions.contains(cambc::buff_restrictions::cast)
                ||cooldown!=0||(exclusive&&history.walked)){out.stop=Stop::NotReady;break;}
            const auto kind=static_cast<cambc::ability_kind>(props.kind);
            if(candidate.kind==word::Kind::Reset||candidate.kind==word::Kind::Shield) {
                if(!(props.targets&word::self)||(candidate.kind==word::Kind::Reset?kind!=cambc::ability_kind::attack_reset:kind!=cambc::ability_kind::shield))
                    {out.stop=Stop::Unsupported;break;}
                if(candidate.kind==word::Kind::Reset&&own.state.contains(cambc::champion_state::can_attack))
                    // Retained E26 witnesses require an actually spent attack.
                    // A-ready enhanced-reset variants remain unqualified.
                    {out.stop=Stop::Unsupported;break;}
            } else if(candidate.kind==word::Kind::Dash||candidate.kind==word::Kind::Spell) {
                const bool dash=candidate.kind==word::Kind::Dash;
                if((dash&&kind!=cambc::ability_kind::dash)||(!dash&&kind!=cambc::ability_kind::damage
                    &&kind!=cambc::ability_kind::aoe_damage&&kind!=cambc::ability_kind::cc)){out.stop=Stop::Unsupported;break;}
                if(dash) {
                    if(!(props.targets&word::ground)){out.stop=Stop::Unsupported;break;}
                    const float range=detail::range(props);
                    if(!detail::finite(range)||range>20){out.stop=Stop::Unsupported;break;}
                    const auto legal=detail::path(origin,destination,true,static_cast<int>(range));
                    if(legal!=Stop::Complete){out.stop=legal;break;}
                } else if(targeted) {
                    if(!(props.targets&primary.kind)){out.stop=Stop::Unsupported;break;}
                    if(!detail::same(victim.position,destination)){out.stop=Stop::TargetChanged;out.renominate=true;break;}
                } else if(!(props.targets&word::ground)||kind!=cambc::ability_kind::aoe_damage
                    ||!detail::inside(destination)||!cambc::vision::in_vision(destination)){out.stop=Stop::Unsupported;break;}
                const auto ability=cambc::self().abilities()[candidate.slot];
                if(!ability.valid_tiles().is_set(destination)){out.stop=Stop::OutOfRange;break;}
            } else {out.stop=Stop::InvalidWord;break;}
        }
        const bool damaging=candidate.kind==word::Kind::Basic||candidate.kind==word::Kind::Spell
            ||(candidate.kind==word::Kind::Dash&&(dash_causes_damage[static_cast<unsigned>(candidate.slot)]
                ||!detail::pure_dash_payload(props)));
        std::array<int,2> sampled{-1,-1};unsigned sampled_count=0;
        std::array<detail::Target,2> before_life{};
        if(damaging) {
            if(targeted){sampled[sampled_count]=candidate.primary;before_life[sampled_count++]=victim;}
            if(tracked>=0&&(!targeted||tracked!=candidate.primary)) {
                sampled[sampled_count]=tracked;before_life[sampled_count++]=detail::target(bindings[tracked]);
            }
        }
        if(!detail::epoch(initial.owner,initial.round)){word_native::invalidate(scene);out.scene_invalidated=true;out.stop=Stop::Stale;break;}
        if(cambc::fuel_remaining()<options.dispatch_reserve){out.stop=Stop::NeedFuel;break;}
        // The post-movement check already precedes the immediately following
        // dispatch. Queries cannot change that dependency; another physical
        // primitive dirties it and requires a new callback before dispatch.
        if(movement_attempted&&!dependency_current&&hooks.dependency&&!dependencies_hold())break;
        // Invalidate BEFORE entering the host, so exceptions cannot leave the
        // old scene reusable. No following check consults Scene permissions.
        word_native::invalidate(scene);out.scene_invalidated=out.attempted=true;++out.attempted_steps;
        bool accepted=false;
        if(candidate.kind==word::Kind::Move)accepted=cambc::self().move_absolute(destination);
        else if(candidate.kind==word::Kind::Basic) {
            cambc::detail::check(cambc::detail::sdk_attack(detail::entity(primary.kind),primary.id));accepted=true;
        } else {
            std::int32_t success=0;const bool unit=candidate.kind==word::Kind::Spell&&targeted;
            const bool positional=candidate.kind==word::Kind::Dash||(candidate.kind==word::Kind::Spell&&!targeted);
            cambc::detail::check(cambc::detail::sdk_cast(candidate.slot,unit?detail::entity(primary.kind):0,
                unit?primary.id:0,positional?destination.x:-1,positional?destination.y:-1,&success));accepted=success!=0;
        }
        const bool movement=candidate.kind==word::Kind::Move||candidate.kind==word::Kind::Dash;
        movement_attempted|=movement;
        if(movement_attempted)dependency_current=false;
        out.acted|=accepted;if(accepted)++out.accepted_steps;
        if(accepted&&cast&&exclusive)history.exclusive_cast=true;
        if(!detail::epoch(initial.owner,initial.round)){out.stop=Stop::Stale;out.renominate=true;break;}
        const auto after_own=detail::post_self(initial.owner);const auto endpoint=after_own.position;
        if(!detail::epoch(initial.owner,initial.round)){out.stop=Stop::Stale;out.renominate=true;break;}
        const bool changed=accepted&&!detail::same(origin,endpoint)&&after_own.alive;
        out.moved|=changed;
        if(accepted&&candidate.kind==word::Kind::Move)history.walked=true;
        bool post_stale=false;
        if(damaging) {
            out.renominate=true; // unqueried collateral effects are unknown
            for(unsigned sample=0;sample<sampled_count;++sample) {
                const auto i=static_cast<unsigned>(sampled[sample]);
                const auto after=detail::target(bindings[i]);
                if(!detail::epoch(initial.owner,initial.round)){post_stale=true;break;}
                const auto& before=before_life[sample];
                if(before.life_known&&before.alive&&after.life_known&&!after.alive){out.killed=true;++out.kills;}
                if(static_cast<int>(i)==tracked&&out.camp_receipt_count<word::max_steps) {
                    auto& receipt=out.camp_receipts[out.camp_receipt_count++];receipt.camp_id=bindings[i].id;
                    receipt.step=step;receipt.primitive=candidate.kind;receipt.observed=before.observed&&after.observed;
                    if(receipt.observed) {
                        receipt.hp_before=before.hp;receipt.hp_after=after.hp;
                        receipt.hp_damage=std::max(0.f,receipt.hp_before-receipt.hp_after);
                        receipt.death=before.alive&&!after.alive;out.camp_hp_damage+=receipt.hp_damage;
                    }
                }
            }
        }
        if(post_stale){out.stop=Stop::Stale;out.renominate=true;break;}
        if(!detail::epoch(initial.owner,initial.round)){out.stop=Stop::Stale;out.renominate=true;break;}
        if(changed&&hooks.movement)hooks.movement(hooks.context,candidate.kind==word::Kind::Move,origin,endpoint);
        // A refused move can reveal an unseen blocking champion, too. Keep
        // every actual action/HP/movement receipt and the true host-refusal
        // flag before allowing dependency failure to stop the old plan.
        if(movement&&hooks.dependency&&after_own.alive) {
            if(!accepted)out.refused=true;
            if(!dependencies_hold())break;
        }
        if(!accepted){out.refused=out.renominate=true;out.stop=Stop::Refused;break;}
        if(!after_own.alive){out.stop=Stop::Inactive;out.renominate=true;break;}
        if(detail::reject_changed_endpoint(out,candidate.kind,accepted,destination,endpoint))break;
        if(!detail::epoch(initial.owner,initial.round)){out.stop=Stop::Stale;out.renominate=true;break;}
        out.stop=Stop::Complete;
        // A kill does not consume movement or an unused self-reset. Continue
        // those supplied steps; a later dead-target step stops for nomination.
    }
    return out;
}
} // namespace wb::capital::word_execute
