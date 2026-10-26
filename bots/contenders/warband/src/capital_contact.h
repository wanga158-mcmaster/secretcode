#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

// A bounded CONTACT model, not game authority. Every own choice is a caller-
// checked legal first primitive. Replies optimistically ignore foe path blockers,
// allow unknown walk history, and retain every represented single-cast word.
// No uncommitted ally contributes future damage, healing, shielding or tank HP.
namespace wb::capital::contact {
inline constexpr std::size_t max_foes=6,max_choices=16,max_effects=16,max_words=8;
struct Point {int x{},y{};};
inline int distance(Point a,Point b) {
    return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));
}
enum class AbilityKind {None,Reset,Damage,Area,Stun,Dash,Shield,Heal};
struct Ability {
    AbilityKind kind=AbilityKind::None;
    int cooldown=0,max_cooldown=8;
    float range=0,radius=0,damage=0;
    int stun_inputs=0;
    bool excludes_walk=false,enhanced_reset=false;
};
struct Foe {
    int id=-1;Point position{};int position_radius=0;
    float hp=0,max_hp=0,shield=0,basic_damage=0;
    int attack_range=1,move_speed=1,stun_inputs=0;
    std::array<Ability,2> abilities{};
    unsigned opportunities=2;
    bool ranged=false,first_fresh=true,can_move=true,can_attack=true,can_cast=true;
    bool enhanced=false,stun_immune=false,heart=false;
    // damage/basic_damage are upper raw hits AFTER target mitigation but BEFORE
    // shield/HP, at Heart factor one. No current-target HP cap is permitted.
    // If further damage can change Heart HP, use the full 1.5 factor envelope.
    // position_radius is a Chebyshev uncertainty ball around the last position.
    // Stale history is represented=false even when the radius happens to be0.
    bool hp_may_fall=false,cooldowns_may_refresh=false,represented=true;
};
enum class EffectKind {DamageSelf,HealSelf,ShieldSelf,DamageFoe,StunFoe};
// StunFoe supplies the guaranteed post-effect disabled-input count, not an
// assumed additive/maximum refresh rule. ShieldSelf is exact post-cast total.
struct Effect {
    EffectKind kind=EffectKind::DamageSelf;
    unsigned foe=0;float amount=0;int stun_inputs=0;
};
enum class Primitive {Hold,Move,Basic,Cast,Dash,Submit,Recall,HealAlly};
struct Choice {
    Primitive primitive=Primitive::Hold;
    Point endpoint{};
    std::array<Effect,max_effects> effects{};unsigned effect_count=0;
    // These are distinct proposed effects on the current real objective.
    // Starting a tribute is channel_started, NEVER banked_gems.
    unsigned banked_gems=0,channel_started=0;
    // OUR immediate heal, scored in separate actual ally frames. A rescue is a
    // model lower-bound class change, not a claimed inevitable death prevented.
    unsigned ally_rescues=0;float ally_hp_margin=0;bool ally_effect_covered=false;
    float camp_work=0;
    int objective_distance=0,displaced_inputs=0;
    float npc_damage_upper=0,continuation_damage_upper=0;
    unsigned npc_hit_count=0;
    bool native_legal=false,prefix_complete=false,continuation_covered=false;
    bool preserves_receipt=true;
};
struct Frame {
    int owner=-1,round=-1;Point position{};
    float hp=0,max_hp=0,shield=0;
    std::array<Foe,max_foes> foes{};unsigned foe_count=0;
    bool fresh=false,coverage_complete=false,shield_valid_through_horizon=false;
    bool self_stun_immune=false;
    // Unknown foes/resources/other actors and NPC activation coverage must be
    // explicit. Incomplete packets still yield a ranked fallback, no certificate.
};
enum class Status {Invalid,Incomplete,Complete};
struct Evaluation {
    bool valid=false,prefix_alive=false,survives=false,horizon_admissible=false;
    bool may_stun_next=false,continuation_admissible=false;
    float hp_lower=0,damage_upper=0;unsigned reply_hits=0,kills=0;
};
struct Decision {
    Status status=Status::Invalid;int choice=-1;
    Evaluation evaluation{};
    unsigned evaluated=0;
};
namespace detail {
inline bool finite(float x) {return std::isfinite(x)&&x>=0&&x<=1000000000.f;}
inline float up(float x) {
    return x>0?std::nextafter(x,std::numeric_limits<float>::infinity()):0.f;
}
inline void hit(float raw,float& hp,float& shield) {
    const float absorbed=std::min(shield,raw);
    shield=std::max(0.f,shield-raw);
    hp=std::max(0.f,hp-std::max(0.f,raw-absorbed));
}
struct Word {
    int distance=0;std::array<float,3> hits{};unsigned count=0;
    int slot=-1;bool stuns=false,enhanced_after=false;
};
struct ReplyState {
    int distance=0,stun=0;
    std::array<int,2> cooldown{};
    bool enhanced=false;
};
struct Reply {
    double damage=0;unsigned hits=0;bool can_stun=false;
};
inline float factor(const Foe& foe) {
    if(!foe.heart)return 1.f;
    if(foe.hp_may_fall)return 1.5f;
    return std::clamp(1.f+.5f*(1.f-foe.hp/foe.max_hp),1.f,1.5f);
}
inline unsigned words(const Foe& foe,const ReplyState& state,unsigned depth,
                      std::array<Word,max_words>& out) {
    const bool fresh=depth!=0||foe.first_fresh;
    const bool m=fresh||foe.can_move,a=fresh||foe.can_attack,c=fresh||foe.can_cast;
    const int d=state.distance,walk=m?foe.move_speed:0;
    const float basic=up(foe.basic_damage*factor(foe));
    unsigned size=0;
    auto push=[&](int remaining,int slot,unsigned basics,float spell,bool stuns,bool reset_enhanced=false) {
        if(size>=out.size())return;
        auto& w=out[size++];w.distance=remaining;w.slot=slot;w.enhanced_after=state.enhanced;
        if(spell>0)w.hits[w.count++]=up(spell*factor(foe));
        for(unsigned n=0;n<basics&&w.count<w.hits.size();++n)w.hits[w.count++]=basic;
        w.stuns=stuns||((state.enhanced||reset_enhanced)&&basics>0);
        if(basics>0)w.enhanced_after=false;
        if(reset_enhanced&&basics==0)w.enhanced_after=true;
    };
    push(d,-1,0,0,false); // End is always in the reply domain.
    if(state.stun>0)return size;
    if(m)push(std::max(0,d-walk),-1,0,0,false);
    if(a&&d<=foe.attack_range)push(d,-1,1,0,false);
    const int stepped=std::max(0,d-walk);
    if(m&&a&&!foe.ranged&&stepped<=foe.attack_range)push(stepped,-1,1,0,false);
    if(!c)return size;
    for(unsigned slot=0;slot<foe.abilities.size();++slot) {
        const auto& ability=foe.abilities[slot];if(state.cooldown[slot]>0)continue;
        switch(ability.kind) {
        case AbilityKind::Reset:
            if(d<=foe.attack_range)push(d,static_cast<int>(slot),(a?1u:0u)+1u,0,false,ability.enhanced_reset);
            if(m&&stepped<=foe.attack_range)
                push(stepped,static_cast<int>(slot),foe.ranged?1u:(a?2u:1u),0,false,ability.enhanced_reset);
            break;
        case AbilityKind::Damage:case AbilityKind::Area:case AbilityKind::Stun: {
            // Range+radius includes optimistic ground centers. Current terrain
            // and walk-history legality are overapproximated for enemy replies.
            const auto cast=[&](int remaining,bool walked) {
                if(remaining>ability.range+ability.radius)return;
                const unsigned basics=a&&remaining<=foe.attack_range&&!(walked&&foe.ranged)?1u:0u;
                push(remaining,static_cast<int>(slot),basics,ability.damage,ability.stun_inputs>0);
            };
            cast(d,false);if(m&&!ability.excludes_walk)cast(stepped,true);
            break;
        }
        case AbilityKind::Dash: {
            const auto cast=[&](int remaining,bool walked) {
                const bool lands=remaining<=ability.radius;
                const unsigned basics=a&&remaining<=foe.attack_range&&!(walked&&foe.ranged)?1u:0u;
                push(remaining,static_cast<int>(slot),basics,lands?ability.damage:0.f,lands&&ability.stun_inputs>0);
            };
            cast(std::max(0,d-static_cast<int>(ability.range)),false);
            if(m)cast(std::max(0,d-walk-static_cast<int>(ability.range)),true);
            break;
        }
        // Under this horizon's no-new-damage-to-foe premise, shielding/healing
        // cannot increase our victim's damage. Their walking/basic alternative
        // is already present. Unrepresented later foe damage uses hp_may_fall.
        default:break;
        }
    }
    return size;
}
inline Reply replies(const Foe& foe,ReplyState state,unsigned depth) {
    Reply result;if(depth>=foe.opportunities||foe.hp<=0)return result;
    std::array<Word,max_words> domain{};
    const unsigned count=words(foe,state,depth,domain);
    // Different first words can reach the same dated residual resources. In
    // this two-opportunity domain the continuation depends on this exact state,
    // not on the name of its incoming word. Keep the complete reply family but
    // evaluate each distinct continuation once, within this one foe/evaluation.
    struct Cached {ReplyState state;Reply reply;};
    std::array<Cached,max_words> cache{};unsigned cached=0;
    const auto same=[](const ReplyState& a,const ReplyState& b) {
        return a.distance==b.distance&&a.stun==b.stun&&a.cooldown==b.cooldown&&a.enhanced==b.enhanced;
    };
    for(unsigned i=0;i<count;++i) {
        const auto& word=domain[i];Reply next;
        // On the final modeled opportunity there is no successor state.
        // Preserve this word's hit accumulation and tie/stun semantics while
        // avoiding unused cooldown transitions and terminal recursive calls.
        if(depth+1<foe.opportunities) {
            ReplyState after=state;after.distance=word.distance;
            after.enhanced=word.enhanced_after;
            after.stun=std::max(0,after.stun-1);
            for(unsigned slot=0;slot<2;++slot)
                after.cooldown[slot]=std::max(0,(word.slot==static_cast<int>(slot)?
                    foe.abilities[slot].max_cooldown:after.cooldown[slot])-1);
            if(foe.cooldowns_may_refresh)after.cooldown={0,0};
            unsigned known=0;while(known<cached&&!same(cache[known].state,after))++known;
            if(known<cached)next=cache[known].reply;
            else {
                next=replies(foe,after,depth+1);
                cache[cached++]={after,next};
            }
        }
        double damage=next.damage;for(unsigned h=0;h<word.count;++h)damage+=word.hits[h];
        if(damage>result.damage||(damage==result.damage&&word.count+next.hits>result.hits))
            {result.damage=damage;result.hits=word.count+next.hits;}
        // A stun anywhere between consecutive own inputs is still present at
        // that next input. It is separate from the max-damage branch.
        result.can_stun|=word.stuns||next.can_stun;
    }
    return result;
}
inline bool valid_foe(const Foe& f) {
    if(f.position_radius<0||f.position_radius>10000||f.id<0||f.position.x<0||f.position.y<0||f.position.x>100||f.position.y>100
        ||(f.stun_immune&&f.stun_inputs>0)||!finite(f.hp)||!finite(f.max_hp)||f.max_hp<=0||f.hp>f.max_hp
        ||!finite(f.shield)||!finite(f.basic_damage)||f.opportunities>2
        ||f.attack_range<0||f.attack_range>20||f.move_speed<0||f.move_speed>10
        ||f.stun_inputs<0||f.stun_inputs>100)return false;
    for(const auto& a:f.abilities)
        if(static_cast<unsigned>(a.kind)>static_cast<unsigned>(AbilityKind::Heal)
            ||a.cooldown<0||a.cooldown>100||a.max_cooldown<0||a.max_cooldown>100
            ||!finite(a.range)||a.range>20||!finite(a.radius)||a.radius>20
            ||!finite(a.damage)||a.stun_inputs<0||a.stun_inputs>100)return false;
    return true;
}
inline Evaluation evaluate(const Frame& frame,const Choice& choice,bool complete) {
    Evaluation out;
    if(static_cast<unsigned>(choice.primitive)>static_cast<unsigned>(Primitive::HealAlly)
        ||!choice.native_legal||choice.endpoint.x<0||choice.endpoint.y<0
        ||choice.endpoint.x>100||choice.endpoint.y>100||choice.effect_count>max_effects||choice.objective_distance<0
        ||choice.ally_rescues>max_foes||!finite(choice.ally_hp_margin)
        ||((choice.ally_rescues||choice.ally_hp_margin>0)&&choice.primitive!=Primitive::HealAlly)
        ||choice.displaced_inputs<0||!finite(choice.camp_work)||!finite(choice.npc_damage_upper)
        ||!finite(choice.continuation_damage_upper)||choice.npc_hit_count>512)return out;
    float hp=frame.hp,shield=frame.shield;auto foes=frame.foes;
    for(unsigned i=0;i<choice.effect_count;++i) {
        const auto& effect=choice.effects[i];
        if(static_cast<unsigned>(effect.kind)>static_cast<unsigned>(EffectKind::StunFoe)
            ||!finite(effect.amount)||effect.stun_inputs<0||effect.stun_inputs>100)return out;
        if((effect.kind==EffectKind::DamageFoe||effect.kind==EffectKind::StunFoe)&&effect.foe>=frame.foe_count)return out;
        switch(effect.kind) {
        case EffectKind::DamageSelf:hit(effect.amount,hp,shield);break;
        case EffectKind::HealSelf:hp=std::min(frame.max_hp,hp+effect.amount);break;
        case EffectKind::ShieldSelf:shield=effect.amount;break; // exact post-cast total
        case EffectKind::DamageFoe:hit(effect.amount,foes[effect.foe].hp,foes[effect.foe].shield);break;
        case EffectKind::StunFoe:
            if(!foes[effect.foe].stun_immune)
                foes[effect.foe].stun_inputs=effect.stun_inputs;
            break;
        }
        // Later healing cannot resurrect an actor killed by an earlier prefix.
        if(hp<=0) {out.valid=true;return out;}
    }
    out.valid=out.prefix_alive=true;
    double damage=choice.npc_damage_upper;unsigned hits=choice.npc_hit_count;
    for(unsigned i=0;i<frame.foe_count;++i) {
        auto& foe=foes[i];if(foe.hp<=0) {if(frame.foes[i].hp>0)++out.kills;continue;}
        ReplyState state{std::max(0,distance(choice.endpoint,foe.position)-foe.position_radius),foe.stun_inputs,
            {foe.abilities[0].cooldown,foe.abilities[1].cooldown},foe.enhanced};
        const auto response=replies(foe,state,0);
        damage+=response.damage;hits+=response.hits;
        out.may_stun_next|=response.can_stun&&!frame.self_stun_immune;
    }
    // Responses are a raw upper envelope, not one claimed joint native word.
    // Guard float32 shield/subtraction order with one HP and shield ULP per hit.
    const float usable_shield=frame.shield_valid_through_horizon?shield:0.f;
    const double hp_ulp=std::nextafter(frame.max_hp,std::numeric_limits<float>::infinity())-frame.max_hp;
    const double shield_ulp=std::nextafter(usable_shield,std::numeric_limits<float>::infinity())-usable_shield;
    const double slack=(static_cast<double>(hits)+1)*(hp_ulp+shield_ulp);
    const double lower=static_cast<double>(hp)-std::max(0.0,damage-usable_shield)-slack;
    out.hp_lower=lower>0?std::max(0.f,std::nextafter(static_cast<float>(lower),-std::numeric_limits<float>::infinity())):0.f;
    out.damage_upper=up(static_cast<float>(damage));out.reply_hits=hits;
    out.survives=out.hp_lower>0;
    out.horizon_admissible=complete&&choice.prefix_complete&&out.survives&&!out.may_stun_next;
    out.continuation_admissible=out.horizon_admissible&&choice.continuation_covered
        &&out.hp_lower>choice.continuation_damage_upper;
    return out;
}
inline bool better(const Choice& a,const Evaluation& x,const Choice& b,const Evaluation& y) {
    if(x.prefix_alive!=y.prefix_alive)return x.prefix_alive;
    if(x.survives!=y.survives)return x.survives;
    if(x.may_stun_next!=y.may_stun_next)return !x.may_stun_next;
    if(a.preserves_receipt!=b.preserves_receipt)return a.preserves_receipt;
    if(x.continuation_admissible!=y.continuation_admissible)return x.continuation_admissible;
    const auto rescued_a=a.ally_effect_covered?a.ally_rescues:0u;
    const auto rescued_b=b.ally_effect_covered?b.ally_rescues:0u;
    if(x.survives&&rescued_a!=rescued_b)return rescued_a>rescued_b;
    // No covered finite continuation means fallback, not admission to contest.
    // Preserve worst-case remaining HP before chasing a nearer work target.
    if(!x.continuation_admissible&&!y.continuation_admissible&&x.hp_lower!=y.hp_lower)
        return x.hp_lower>y.hp_lower;
    if(a.banked_gems!=b.banked_gems)return a.banked_gems>b.banked_gems;
    if(x.kills!=y.kills)return x.kills>y.kills;
    const float ally_a=a.ally_effect_covered?a.ally_hp_margin:0.f;
    const float ally_b=b.ally_effect_covered?b.ally_hp_margin:0.f;
    if(x.survives&&ally_a!=ally_b)return ally_a>ally_b;
    if(a.channel_started!=b.channel_started)return a.channel_started>b.channel_started;
    if(a.objective_distance!=b.objective_distance)return a.objective_distance<b.objective_distance;
    if(a.camp_work!=b.camp_work)return a.camp_work>b.camp_work;
    if(a.displaced_inputs!=b.displaced_inputs)return a.displaced_inputs<b.displaced_inputs;
    return x.hp_lower>y.hp_lower;
}
} // namespace detail
inline Decision choose(const Frame& frame,std::span<const Choice> choices) {
    Decision out;
    if(!frame.fresh||frame.position.x<0||frame.position.y<0||frame.position.x>100||frame.position.y>100||frame.owner<0||frame.round<0||!detail::finite(frame.hp)||frame.hp<=0
        ||!detail::finite(frame.max_hp)||frame.max_hp<frame.hp||!detail::finite(frame.shield)
        ||frame.foe_count>max_foes||choices.empty()||choices.size()>max_choices)return out;
    bool complete=frame.coverage_complete;
    for(unsigned i=0;i<frame.foe_count;++i) {
        if(!detail::valid_foe(frame.foes[i]))return out;
        complete&=frame.foes[i].represented;
        for(unsigned j=0;j<i;++j)if(frame.foes[i].id==frame.foes[j].id)return out;
    }
    for(unsigned i=0;i<choices.size();++i) {
        const auto evaluation=detail::evaluate(frame,choices[i],complete);++out.evaluated;
        if(!evaluation.valid)continue;
        if(out.choice<0||detail::better(choices[i],evaluation,choices[out.choice],out.evaluation)) {
            out.choice=static_cast<int>(i);out.evaluation=evaluation;
        }
    }
    if(out.choice>=0)out.status=complete&&choices[out.choice].prefix_complete?Status::Complete:Status::Incomplete;
    return out;
}
} // namespace wb::capital::contact
