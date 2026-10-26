#pragma once
#include <cambc.h>
#include "capital_contact.h"
#include "config.h"
#include "capital_order_native.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

// Observation only. No primitive is dispatched, and no stored walk history or
// native legality is invented. The caller still supplies NPC activation,
// continuation, actual prefix effects, endpoint legality and a final freshness
// check. Expected hidden foes become incomplete coverage; unexpected SDK errors
// propagate to the owning turn boundary.
namespace wb::capital::contact_native {
struct Snapshot {
    contact::Frame frame;
    std::vector<cambc::champion> enemies; // exactly frame.foes[0..foe_count)
    cambc::detail::champion_info raw_self{};
    cambc::champion_state self_state{};
    cambc::buff_restrictions self_restrictions{};
    bool self_stunned=false,self_enhanced=false;
    int self_stun_inputs=0;
    float self_shield_now=0,self_mitigation=1;
    std::array<float,contact::max_foes> foe_mitigation{1,1,1,1,1,1};
    // Offensive damage is CURRENT raw outgoing damage; heal/shield amounts are
    // not stored in Ability::damage. The caller queries its selected own effect.
    std::array<contact::Ability,2> self_abilities{};
    unsigned hidden_living_foes=0,omitted_dead_foes=0;
};
namespace detail {
inline bool finite(float x) {return std::isfinite(x)&&x>=0;}
inline float upper(float x) {
    return x>0?std::nextafter(x,std::numeric_limits<float>::infinity()):0.f;
}
struct Buffs {
    bool represented=true,enhanced=false,stunned=false;
    int stun=0;
    float shield_now=0,shield_stable=0;
};
inline Buffs buffs(unsigned owner) {
    Buffs out;
    std::array<cambc::detail::buff_data,3> raw{};
    std::uint32_t count=0;
    cambc::detail::check(cambc::detail::sdk_champion_buff_data(owner,raw.data(),raw.size(),&count));
    if(count>raw.size())out.represented=false;
    for(unsigned i=0;i<std::min<std::size_t>(count,raw.size());++i) {
        const auto& b=raw[i];
        switch(static_cast<cambc::buff_types>(b.type)) {
        case cambc::buff_types::shield:
            if(!finite(b.data.shield.value)||b.data.shield.duration<0) {out.represented=false;break;}
            out.shield_now+=b.data.shield.value;
            // Two enemy opportunities are a conservative reply envelope before
            // the next own input, not two guaranteed free native decisions.
            if(b.data.shield.duration>=2)out.shield_stable+=b.data.shield.value;
            break;
        case cambc::buff_types::stun:
            out.stunned=true;
            if(b.data.stun.duration<0)out.represented=false;
            else out.stun=std::max(out.stun,b.data.stun.duration);
            break;
        case cambc::buff_types::enhanced_attack:
            if(b.data.enhanced_attack.duration<0)out.represented=false;
            else out.enhanced=true; // timer zero is still currently present
            break;
        default:out.represented=false;break;
        }
    }
    out.represented&=finite(out.shield_now)&&finite(out.shield_stable);
    return out;
}
inline cambc::buff_restrictions restrictions(unsigned owner,bool& represented) {
    std::uint8_t bits=0;
    cambc::detail::check(cambc::detail::sdk_champion_restrictions(owner,&bits));
    if(bits&~std::uint8_t(63))represented=false;
    return cambc::buff_restrictions::from_bits_retain(bits);
}
inline float mitigation(unsigned owner,bool own,bool& represented) {
    constexpr unsigned count=static_cast<unsigned>(cambc::item_type::count);
    std::array<std::uint32_t,count> inventory{};
    cambc::detail::check(cambc::detail::sdk_champion_items(owner,inventory.data()));
    const std::array variants{cambc::item_type::durability_emblem_of_strength,
        cambc::item_type::durability_emblem_of_focus,cambc::item_type::durability_emblem_of_health};
    for(const auto item:variants)if(inventory[static_cast<unsigned>(item)]>0) {
        // Family presence, not emblem count: duplicate family passives do not
        // stack. Query the actual present family's magnitude exactly once.
        const auto info=item.info();
        if(info.passive!=cambc::item_passive::durability||!finite(info.passive_magnitude)
           ||info.passive_magnitude>1) {
            represented=false;return own?1.f:0.f;
        }
        return 1.f-info.passive_magnitude;
    }
    return 1.f;
}
inline float heart_factor(const cambc::detail::champion_info& raw) {
    if(!(raw.max_health>0))return 1.f;
    return std::clamp(1.f+.5f*(1.f-raw.health/raw.max_health),1.f,1.5f);
}
inline contact::Ability ability(unsigned owner,unsigned slot,
        const cambc::detail::ability_props& p,float incoming_mitigation,
        float current_heart_factor,bool normalize_heart,bool upper_bound,bool& represented) {
    contact::Ability out;
    std::int32_t cooldown=0;
    cambc::detail::check(cambc::detail::sdk_champion_cooldown(owner,slot,&cooldown));
    out.cooldown=cooldown;out.max_cooldown=p.cooldown;
    out.excludes_walk=(p.attributes&cambc::detail::ability_attribute_exclusive_with_movement)!=0;
    if(p.max_charges!=0)represented=false;
    bool damaging=false;
    switch(static_cast<cambc::ability_kind>(p.kind)) {
    case cambc::ability_kind::attack_reset:
        out.kind=contact::AbilityKind::Reset;out.enhanced_reset=p.payload.enhanced_attack!=0;break;
    case cambc::ability_kind::damage:
        out.kind=contact::AbilityKind::Damage;out.range=p.payload.damage.range;damaging=true;break;
    case cambc::ability_kind::aoe_damage:
        out.kind=contact::AbilityKind::Area;out.range=p.payload.aoe_damage.range;
        out.radius=p.payload.aoe_damage.radius;out.stun_inputs=p.payload.aoe_damage.duration;damaging=true;break;
    case cambc::ability_kind::cc:
        out.kind=contact::AbilityKind::Stun;out.range=p.payload.cc.range;
        out.stun_inputs=p.payload.cc.duration;damaging=true;break;
    case cambc::ability_kind::dash:
        out.kind=contact::AbilityKind::Dash;out.range=p.payload.dash.range;
        out.radius=p.payload.dash.radius;out.stun_inputs=p.payload.dash.duration;damaging=true;break;
    case cambc::ability_kind::shield:
        out.kind=contact::AbilityKind::Shield;out.range=p.payload.shield.range;break;
    case cambc::ability_kind::heal:
        out.kind=contact::AbilityKind::Heal;out.range=p.payload.heal.range;break;
    default:
        if(p.kind!=0)represented=false;
        out.max_cooldown=std::max(0,p.cooldown);break;
    }
    if(damaging) {
        float current=0;
        cambc::detail::check(cambc::detail::sdk_champion_ability_damage(owner,slot,&current));
        if(!finite(current))represented=false;
        else {
            // The SDK includes Heart's CURRENT amplification. Replies must
            // reapply the factor after our hit, including hp_may_fall's full
            // 1.5 envelope. Do not amplify that current bonus a second time.
            const float base=normalize_heart?upper(upper(current/current_heart_factor)):current;
            out.damage=upper_bound?upper(base*incoming_mitigation):base*incoming_mitigation;
        }
    }
    if(out.cooldown<0||out.max_cooldown<0||out.max_cooldown>100||out.cooldown>100
       ||!finite(out.range)||out.range>20||!finite(out.radius)||out.radius>20
       ||!finite(out.damage)||out.damage>1000000000.f||out.stun_inputs<0||out.stun_inputs>100)
        represented=false;
    return out;
}
inline bool raw_valid(const cambc::detail::champion_info& r) {
    return r.x>=0&&r.y>=0&&r.x<=100&&r.y<=100&&finite(r.health)&&r.health>0
        &&finite(r.max_health)&&r.max_health>=r.health&&finite(r.strength)&&finite(r.focus)
        &&r.move_speed>=0&&r.move_speed<=10&&r.attack_range>=0&&r.attack_range<=20;
}
inline bool kit_represented(int type) {
    switch(static_cast<cambc::champion_type>(type)) {
    case cambc::champion_type::ryth:case cambc::champion_type::vesper_vale:
    case cambc::champion_type::vendetta:case cambc::champion_type::cassi:
    case cambc::champion_type::issac:case cambc::champion_type::stellar:
    case cambc::champion_type::marcella:case cambc::champion_type::willow:
    case cambc::champion_type::heart_of_ruin:return true;
    // Ren retaliation, Tavi field capital, future Prism gem-Focus and Varg
    // collateral basic reach are absent from the one-target reply domain.
    default:return false;
    }
}
inline bool unsupported_consumables(unsigned owner) {
    std::array<std::uint32_t,4> counts{};
    cambc::detail::check(cambc::detail::sdk_champion_consumables(owner,counts.data()));
    return std::any_of(counts.begin(),counts.end(),[](auto n){return n!=0;});
}
}
inline Snapshot capture() {
    Snapshot out;
    const unsigned round=cambc::round_number();
    const unsigned owner=static_cast<unsigned>(static_cast<cambc::handle_champion_t>(cambc::self()));
    auto& frame=out.frame;
    frame.owner=static_cast<int>(owner);frame.round=static_cast<int>(round);
    cambc::detail::check(cambc::detail::sdk_champion(owner,&out.raw_self));
    const auto& raw=out.raw_self;
    out.self_state=cambc::champion_state::from_bits_retain(raw.state);
    bool covered=detail::raw_valid(raw)&&out.self_state.contains(cambc::champion_state::alive);
    out.self_restrictions=detail::restrictions(owner,covered);
    const auto own_buffs=detail::buffs(owner);
    covered&=own_buffs.represented;
    out.self_stun_inputs=own_buffs.stun;out.self_stunned=own_buffs.stunned;
    out.self_enhanced=own_buffs.enhanced;out.self_shield_now=own_buffs.shield_now;
    frame.position={raw.x,raw.y};frame.hp=raw.health;frame.max_hp=raw.max_health;
    frame.shield=own_buffs.shield_stable;
    // Expiring current shield was discarded, so a genuinely new ShieldSelf
    // effect can be retained by the evaluator. Its amount must likewise omit
    // any old expiring component; self_shield_now is only a prefix observation.
    frame.shield_valid_through_horizon=true;
    out.self_mitigation=detail::mitigation(owner,true,covered);
    cambc::detail::champion_design own_design{};
    cambc::detail::check(cambc::detail::sdk_champion_design(raw.type,&own_design));
    frame.self_stun_immune=own_design.definition.passive_type==static_cast<unsigned>(cambc::passive_type::stun_immunity);
    for(unsigned slot=0;slot<2;++slot)
        out.self_abilities[slot]=detail::ability(owner,slot,own_design.definition.abilities[slot],1.f,1.f,false,false,covered);

    const auto roster=cambc::champions(cambc::team::enemy);
    if constexpr(config::ordered_contact_controller)order_native::begin(roster);
    const auto spawn=cambc::map::spawn_region(cambc::team::enemy).positions();
    out.enemies.reserve(std::min(roster.size(),contact::max_foes));
    for(const auto& enemy:roster) {
        const unsigned id=static_cast<unsigned>(static_cast<cambc::handle_champion_t>(enemy));
        if(!enemy.is_alive()) {
            // Dead-now is not dead-through-horizon without a retained native
            // death-age/respawn model. That history belongs to the caller.
            ++out.omitted_dead_foes;covered=false;continue;
        }
        cambc::detail::champion_info foe_raw{};
        const auto status=cambc::detail::sdk_champion(id,&foe_raw);
        if(status==static_cast<int>(cambc::error_code::api_not_in_vision)) {
            ++out.hidden_living_foes;covered=false;continue;
        }
        cambc::detail::check(status);
        if constexpr(config::ordered_contact_controller)order_native::observe(static_cast<int>(id),foe_raw);
        if(frame.foe_count>=contact::max_foes||!detail::raw_valid(foe_raw)) {covered=false;continue;}
        const unsigned at=frame.foe_count++;
        out.enemies.push_back(enemy);
        auto& foe=frame.foes[at];
        bool represented=detail::kit_represented(foe_raw.type);
        foe.id=static_cast<int>(id);foe.position={foe_raw.x,foe_raw.y};
        foe.hp=foe_raw.health;foe.max_hp=foe_raw.max_health;
        foe.attack_range=foe_raw.attack_range;foe.move_speed=foe_raw.move_speed;
        const auto state=cambc::champion_state::from_bits_retain(foe_raw.state);
        const auto restriction=detail::restrictions(id,represented);
        const auto observed=detail::buffs(id);represented&=observed.represented;
        foe.shield=observed.shield_now;foe.enhanced=observed.enhanced;foe.stun_inputs=observed.stun;
        foe.can_move=state.contains(cambc::champion_state::can_move)&&!restriction.contains(cambc::buff_restrictions::move);
        foe.can_attack=state.contains(cambc::champion_state::can_attack)&&!restriction.contains(cambc::buff_restrictions::attack);
        foe.can_cast=state.contains(cambc::champion_state::can_cast)&&!restriction.contains(cambc::buff_restrictions::cast);
        foe.first_fresh=true;foe.opportunities=2;foe.hp_may_fall=true;
        cambc::detail::champion_design design{};
        cambc::detail::check(cambc::detail::sdk_champion_design(foe_raw.type,&design));
        represented&=design.definition.attack_type<=1;
        foe.ranged=design.definition.attack_type==1;
        foe.stun_immune=design.definition.passive_type==static_cast<unsigned>(cambc::passive_type::stun_immunity);
        foe.heart=design.definition.passive_type==static_cast<unsigned>(cambc::passive_type::missing_health_damage);
        foe.cooldowns_may_refresh=design.definition.passive_type==static_cast<unsigned>(cambc::passive_type::reset_cooldowns_on_kill);
        if(foe.stun_immune&&foe.stun_inputs>0) {represented=false;foe.stun_inputs=0;}
        foe.basic_damage=detail::upper(foe_raw.strength*out.self_mitigation);
        const float current_factor=foe.heart?detail::heart_factor(foe_raw):1.f;
        int dash_reach=0;
        for(unsigned slot=0;slot<2;++slot) {
            foe.abilities[slot]=detail::ability(id,slot,design.definition.abilities[slot],
                out.self_mitigation,current_factor,foe.heart,true,represented);
            if(foe.abilities[slot].kind==contact::AbilityKind::Dash)
                dash_reach=std::max(dash_reach,static_cast<int>(std::ceil(foe.abilities[slot].range)));
        }
        out.foe_mitigation[at]=detail::mitigation(id,false,represented);
        if(detail::unsupported_consumables(id))represented=false;
        if(state.contains(cambc::champion_state::recalling)||state.contains(cambc::champion_state::teleporting))
            represented=false;
        // Unknown enemy wallet can buy on spawn. Optimistically allow two
        // walk+dash approaches before rejecting that resource proof.
        for(const auto tile:spawn)if(contact::distance(foe.position,{tile.x,tile.y})
            <=2*(foe.move_speed+dash_reach)) {represented=false;break;}
        foe.represented=represented;covered&=represented;
    }
    if constexpr(config::ordered_contact_controller) {
        const auto& order=order_native::finish();
        for(unsigned i=0;i<frame.foe_count;++i) {
            const auto window=order.for_id(frame.foes[i].id);
            if(window.admitted)frame.foes[i].opportunities=window.scheduled_upper;
        }
    }
    frame.fresh=cambc::round_number()==round;
    frame.coverage_complete=covered&&frame.fresh;
    return out;
}
}
