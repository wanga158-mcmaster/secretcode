#pragma once
#include "contract.h"
#include "competition.h"
#include "../../../../sdk/cpp/cambc.h"
#include <optional>
#include <type_traits>
#include <vector>

namespace p9::live {
inline Point point(cambc::vec2 p) { return {p.x,p.y}; }
inline cambc::vec2 point(Point p) { return {p.x,p.y}; }
inline int handle(const cambc::champion& c) { return static_cast<int>(static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(c))); }
inline int handle(const cambc::map::camp_group& g) { return static_cast<int>(static_cast<std::uint32_t>(static_cast<cambc::handle_camp_group_t>(g))); }
inline unsigned owned_shards(const cambc::champion& c,cambc::item_type shard) {
    const auto counts=c.item_count();const auto info=shard.info();
    return competition::total_shards(counts[shard],info.result?counts[*info.result]:0,info.combine_count);
}
inline Champion champion_type(cambc::champion_type c) {
    switch(c) {
    case cambc::champion_type::marcella:return Champion::marcella;
    case cambc::champion_type::ren:return Champion::ren;
    case cambc::champion_type::tavi:return Champion::tavi;
    case cambc::champion_type::varg:return Champion::varg;
    case cambc::champion_type::willow:return Champion::willow;
    case cambc::champion_type::vesper_vale:return Champion::vesper;
    case cambc::champion_type::prism:return Champion::prism;
    case cambc::champion_type::vendetta:return Champion::vendetta;
    case cambc::champion_type::ryth:return Champion::ryth;
    case cambc::champion_type::stellar:return Champion::stellar;
    case cambc::champion_type::cassi:return Champion::cassi;
    case cambc::champion_type::issac:return Champion::issac;
    case cambc::champion_type::heart_of_ruin:return Champion::heart;
    default:return Champion::other;
    }
}
inline cambc::champion_draft draft_type(Champion c) {
    switch(c) {
    case Champion::marcella:return cambc::champion_draft::marcella;
    case Champion::ren:return cambc::champion_draft::ren;
    case Champion::tavi:return cambc::champion_draft::tavi;
    case Champion::varg:return cambc::champion_draft::varg;
    case Champion::willow:return cambc::champion_draft::willow;
    case Champion::vesper:return cambc::champion_draft::vesper_vale;
    case Champion::prism:return cambc::champion_draft::prism;
    case Champion::vendetta:return cambc::champion_draft::vendetta;
    case Champion::ryth:return cambc::champion_draft::ryth;
    case Champion::stellar:return cambc::champion_draft::stellar;
    case Champion::cassi:case Champion::issac:return cambc::champion_draft::cassi_and_issac;
    case Champion::heart:return cambc::champion_draft::heart_of_ruin;
    default:return cambc::champion_draft::varg;
    }
}
inline int ordinal(const Profile& profile, Champion c) {
    for(int i=0;i<5;++i) if(profile.draft[i]==c || (c==Champion::issac&&profile.draft[i]==Champion::cassi)) return i;
    return -1;
}
// The pinned SDK's public camp accessors each fetch this same live record.
// Keep it local to the current observation; hidden and missing camps match
// camp::is_visible(), and every other engine error still propagates.
inline bool read_camp(const cambc::camp& c,cambc::detail::camp_state& out) {
    const auto id=static_cast<std::uint32_t>(static_cast<cambc::handle_camp_t>(c));
    const auto code=cambc::detail::sdk_camp(id,&out);
    if(code==static_cast<std::int32_t>(cambc::error_code::api_not_in_vision)||
       code==static_cast<std::int32_t>(cambc::error_code::api_not_found))return false;
    cambc::detail::check(code);return true;
}
inline Actor actor_view(const cambc::champion& c,const Profile& profile,bool self=false) {
    const int round=static_cast<int>(cambc::round_number());
    Actor out;out.id=handle(c);
    // SDK 3.0.2 implements position, combat stats and state accessors by
    // querying this identical record each time. Read it once per observation;
    // no live record survives a return, action, or round change.
    cambc::detail::champion_info live{};
    const auto code=cambc::detail::sdk_champion(static_cast<std::uint32_t>(out.id),&live);
    if(static_cast<int>(cambc::round_number())!=round)return out;
    if(code==static_cast<std::int32_t>(cambc::error_code::api_not_in_vision)) {
        out.type=champion_type(c.champion_name());out.pick=ordinal(profile,out.type);
        out.ally=c.team()==cambc::team::ally;return out;
    }
    cambc::detail::check(code);
    out.type=champion_type(static_cast<cambc::champion_type>(live.type));out.pick=ordinal(profile,out.type);
    out.ally=live.team==cambc::detail::sdk_my_team();out.visible=self||(live.x>=0&&live.y>=0);
    if(!out.visible) return out;
    const auto state=cambc::champion_state::from_bits_retain(static_cast<std::uint32_t>(live.state));
    out.alive=state.contains(cambc::champion_state::alive);
    out.position={live.x,live.y};out.hp=live.health;out.max_hp=live.max_health;
    out.strength=live.strength;out.focus=live.focus;out.range=live.attack_range;
    out.can_move=state.contains(cambc::champion_state::can_move);
    out.can_attack=state.contains(cambc::champion_state::can_attack);
    out.can_cast=state.contains(cambc::champion_state::can_cast);
    if(state.contains(cambc::champion_state::submitting_gems))out.channel=Channel::tribute;
    else if(state.contains(cambc::champion_state::recalling))out.channel=Channel::recall;
    else if(state.contains(cambc::champion_state::teleporting))out.channel=Channel::teleport;
    out.at_spawn=out.alive&&cambc::map::spawn_region(cambc::team::ally).is_set({live.x,live.y});
    if(static_cast<int>(cambc::round_number())!=round){out.visible=false;return out;}
    out.bag=c.gem_count();
    // The pinned SDK calls these constructor-cached immutable properties
    // (champion::abilities / ability::info). Cache only their derived traits;
    // health, strength, focus, inventory and cooldowns remain live observations.
    struct AbilityTraits {
        bool present=false,attack_reset=false,enemy_spell=false;
        double modifier=0;
        int reach=0;
    };
    struct AbilityTraitsCache {bool ready=false;std::array<AbilityTraits,2> slots{};};
    constexpr auto physical_types=static_cast<std::size_t>(cambc::champion_type::heart_of_ruin)+1;
    static std::array<AbilityTraitsCache,physical_types> cached_traits{};
    AbilityTraitsCache uncached;
    auto& traits=live.type>=0&&static_cast<std::size_t>(live.type)<cached_traits.size()?
        cached_traits[static_cast<std::size_t>(live.type)]:uncached;
    if(!traits.ready) {
        // A retained roster handle may have been constructed while hidden,
        // caching a visibility error in its ability views. Physical designs
        // expose the same immutable metadata without requiring live vision.
        const auto abilities=live.type>=0&&static_cast<std::size_t>(live.type)<cambc::champion_designs.size()?
            cambc::champion_designs[static_cast<std::size_t>(live.type)].abilities():c.abilities();
        for(int slot=0;slot<2;++slot)if(auto info=abilities[slot].info()) {
            auto& value=traits.slots[slot];value.present=true;
            value.attack_reset=abilities[slot].is_attack_reset();
            std::visit([&](const auto& payload) {
                if constexpr(requires {payload.modifier;payload.range;}) {
                    if(!payload.valid_targets.contains(cambc::ability_target_mask::enemies))return;
                    value.enemy_spell=true;value.modifier=payload.modifier;
                    double reach=payload.range;
                    if constexpr(requires {payload.radius;})reach+=payload.radius;
                    value.reach=static_cast<int>(std::ceil(reach));
                }
            },*info);
        }
        traits.ready=true;
    }
    bool ready_spell=false;
    for(int slot=0;slot<2;++slot)if(const auto& value=traits.slots[slot];value.present) {
        if(static_cast<int>(cambc::round_number())!=round){out.visible=false;return out;}
        std::int32_t cooldown{};
        cambc::detail::check(cambc::detail::sdk_champion_cooldown(static_cast<std::uint32_t>(out.id),slot,&cooldown));
        out.cooldown[slot]=cooldown;
        ready_spell|=cooldown>=0&&cooldown<=1&&(value.attack_reset||value.enemy_spell);
    }
    const bool cast_threat=out.alive&&ready_spell;
    // Starting teams alternate, so a rival may get two owner turns between
    // ours: cooldown one reaches zero at the first turn end and is usable on
    // the second. Forecast both opportunities conservatively; cast allowance
    // renews and current channels/restrictions may end. Actual cooldowns and
    // can_cast still describe current legality.
    double spell=0;out.threat_range=out.range;
    for(int slot=0;slot<2;++slot)if(const auto& value=traits.slots[slot];
        value.present&&cast_threat&&out.cooldown[slot]>=0&&out.cooldown[slot]<=1) {
        if(value.attack_reset)spell=std::max(spell,out.strength);
        if(value.enemy_spell) {
            spell=std::max(spell,out.focus*value.modifier);
            out.threat_range=std::max(out.threat_range,value.reach);
        }
    }
    out.burst=out.strength+spell;
    if(static_cast<int>(cambc::round_number())!=round){out.visible=false;return out;}
    const auto items=c.item_count();
    static const auto defense=cambc::item_type::durability_shard_of_health.info();
    out.defense_shards=static_cast<int>(competition::total_shards(
        items[cambc::item_type::durability_shard_of_health],defense.result?items[*defense.result]:0,defense.combine_count));
    static const auto monster=cambc::item_type::monster_emblem_of_strength.info();
    static const auto jungle=cambc::item_type::jungle_emblem_of_health.info();
    if(items[cambc::item_type::monster_emblem_of_strength]||items[cambc::item_type::monster_emblem_of_focus]||
       items[cambc::item_type::monster_emblem_of_health]) {
        const double bonus=monster.passive_magnitude;
        if(std::isfinite(bonus)&&bonus>=0&&bonus<=1)out.camp_attack_multiplier=1+bonus;
    }
    if(items[cambc::item_type::jungle_emblem_of_strength]||items[cambc::item_type::jungle_emblem_of_focus]||
       items[cambc::item_type::jungle_emblem_of_health]) {
        const double reduction=jungle.passive_magnitude;
        if(std::isfinite(reduction)&&reduction>=0&&reduction<1)out.camp_defense_multiplier=1-reduction;
    }
    if(static_cast<int>(cambc::round_number())!=round)out.visible=false;
    return out;
}
inline bool same_round(int round) { return static_cast<int>(cambc::round_number())==round; }
inline bool channel_active() {
    const auto state=cambc::self().state();
    return state.contains(cambc::champion_state::submitting_gems)||
        state.contains(cambc::champion_state::recalling)||state.contains(cambc::champion_state::teleporting);
}
inline bool movement_compatible(const cambc::ability& ability,bool walked) {
    if(!walked&&cambc::self().can_move())return true;
    const auto info=ability.info();
    return info&&std::visit([](const auto& payload){return !payload.exclusive_with_movement;},*info);
}
struct CombatTarget {
    cambc::vec2 position{};
    double hp=0,max_hp=0,strength=0;
    int range=0;
    bool ally=false;
};
// An action can kill a target or remove the vision it supplied. Read a fresh
// complete record after every action, rather than following is_visible() with
// several separate live getters. These are the SDK's visibility semantics;
// unexpected errors still reach the turn-boundary logger.
inline bool combat_target(const cambc::champion& target,CombatTarget& out) {
    cambc::detail::champion_info live{};
    const auto code=cambc::detail::sdk_champion(static_cast<std::uint32_t>(handle(target)),&live);
    if(code==static_cast<std::int32_t>(cambc::error_code::api_not_in_vision))return false;
    cambc::detail::check(code);
    if(live.x<0||live.y<0||live.health<=0||
       !cambc::champion_state::from_bits_retain(live.state).contains(cambc::champion_state::alive))return false;
    out={{live.x,live.y},live.health,live.max_health,live.strength,live.attack_range,
        live.team==cambc::detail::sdk_my_team()};
    return true;
}
inline bool combat_target(const cambc::camp& target,CombatTarget& out) {
    cambc::detail::camp_state live{};
    if(!read_camp(target,live)||live.health<=0)return false;
    out={{live.x,live.y},live.health,live.max_health,live.strength,target.attack_range(),false};
    return true;
}
enum class CombatCast { none,unit,ground };
inline constexpr CombatCast combat_cast(const cambc::ability_info& info,bool enemy) {
    return std::visit([enemy](const auto& payload) {
        using Payload=std::remove_cvref_t<decltype(payload)>;
        if constexpr(std::is_same_v<Payload,cambc::ability_damage>||std::is_same_v<Payload,cambc::ability_cc>||
                     std::is_same_v<Payload,cambc::ability_aoe_damage>) {
            const auto target=enemy?cambc::ability_target_mask::enemies:cambc::ability_target_mask::camps;
            if(payload.valid_targets.contains(target))return CombatCast::unit;
            if(payload.valid_targets.contains(cambc::ability_target_mask::ground))return CombatCast::ground;
        }
        // Dashes need a separate landing/path decision, even when their
        // payload can damage or stun a unit near the landing tile.
        return CombatCast::none;
    },info);
}
inline bool cast_in_range(const cambc::ability_info& info,cambc::vec2 origin,cambc::vec2 target) {
    return std::visit([&](const auto& payload) {
        if constexpr(requires {payload.range;})return origin.chebyshev_distance(target)<=payload.range;
        else return false;
    },info);
}
template<class Target> bool combat_spell(const cambc::ability& ability,const Target& target,
                                        const CombatTarget& live,CombatCast mode,int round) {
    if(!same_round(round)||cambc::fuel_remaining()<20000)return false;
    if(mode==CombatCast::ground)return ability.use(live.position);
    if(mode==CombatCast::unit)return ability.use(target);
    return false;
}
inline bool cast_camp_before_move(const cambc::camp& target,const Actor& observed_self,
                                  Point observed_target_position,int round) {
    if(!observed_self.can_cast)return false;
    auto& me=cambc::self();const auto abilities=me.abilities();
    for(int slot=0;slot<2;++slot) {
        if(observed_self.cooldown[slot]!=0)continue;
        const auto& ability=abilities[slot];const auto& info=ability.info();if(!info)continue;
        const auto mode=combat_cast(*info,false);
        // Screen cached observations/metadata before any extra live query.
        // Only spells that walking would exclude need this earlier chance.
        if(mode==CombatCast::none||
           !std::visit([](const auto& payload){return payload.exclusive_with_movement;},*info)||
           !cast_in_range(*info,point(observed_self.position),point(observed_target_position)))continue;
        if(!same_round(round)||cambc::fuel_remaining()<35000||channel_active())return false;
        if(!ability.is_ready()||!movement_compatible(ability,false))return false;
        CombatTarget live;
        if(!combat_target(target,live)||!same_round(round)||
           !cast_in_range(*info,me.position(),live.position))return false;
        // One guarded cast attempt; success ends this farm action before moving.
        return combat_spell(ability,target,live,mode,round);
    }
    return false;
}
inline bool cast_target(cambc::champion target,bool heal=false,bool interrupt_only=false,int expected_round=-1,bool walked=false) {
    auto& me=cambc::self();
    const int round=static_cast<int>(cambc::round_number());
    if((expected_round>=0&&round!=expected_round)||cambc::fuel_remaining()<30000)return false;
    CombatTarget live;
    if(!combat_target(target,live)||!same_round(round)||!me.can_cast()||channel_active())return false;
    if(heal?(target==me||!live.ally||live.hp>=live.max_hp):live.ally)return false;
    for(const auto& ability:me.abilities()) {
        if(!same_round(round)||cambc::fuel_remaining()<20000)return false;
        if(!ability.is_ready()||!movement_compatible(ability,walked))continue;
        const auto info=ability.info();if(!info)continue;
        auto mode=CombatCast::none;
        if(heal) {
            if(!ability.is_heal()||!ability.can_target_allies())continue;
            mode=CombatCast::unit;
        } else {
            if(interrupt_only&&!ability.is_cc())continue;
            mode=combat_cast(*info,true);
        }
        if(mode==CombatCast::none||!cast_in_range(*info,me.position(),live.position))continue;
        return combat_spell(ability,target,live,mode,round);
    }
    return false;
}
template<class Target> bool fight(const Target& target,bool enemy=false,int expected_round=-1,bool walked=false) {
    auto& me=cambc::self();
    const int round=static_cast<int>(cambc::round_number());
    if((expected_round>=0&&round!=expected_round)||cambc::fuel_remaining()<30000)return false;
    CombatTarget live;
    if(channel_active()||!same_round(round)||!combat_target(target,live)||!same_round(round))return false;
    if(enemy!=std::is_same_v<Target,cambc::champion>||live.ally)return false;
    bool acted=false;
    // Shields must precede the incoming hit, while harmless clusters need no
    // defensive cast. At most one successful ability is allowed per turn.
    for(const auto& ability:me.abilities()) {
        if(!same_round(round)||cambc::fuel_remaining()<20000)return acted;
        if(!me.can_cast()||!ability.is_ready()||!movement_compatible(ability,walked))continue;
        if(ability.is_shield()&&ability.can_target_self()&&live.strength>0&&
           me.position().chebyshev_distance(live.position)<=live.range+1&&
           (me.health()<me.max_health()*.85f||me.health()<=3*live.strength+60)) {
            if(!same_round(round)||cambc::fuel_remaining()<20000)return acted;
            acted=ability.use();if(acted)break;
        }
    }
    if(!same_round(round)||cambc::fuel_remaining()<20000||!combat_target(target,live)||!same_round(round))return acted;
    if(!acted)for(const auto& ability:me.abilities()) {
        if(!same_round(round)||cambc::fuel_remaining()<20000)return acted;
        if(!me.can_cast()||!ability.is_ready()||!movement_compatible(ability,walked))continue;
        const auto info=ability.info();if(!info)continue;
        const auto mode=combat_cast(*info,enemy);
        if(mode==CombatCast::none||!cast_in_range(*info,me.position(),live.position))continue;
        acted=combat_spell(ability,target,live,mode,round);if(acted)break;
    }
    if(!same_round(round)||cambc::fuel_remaining()<20000||!combat_target(target,live)||!same_round(round))return acted;
    if(me.can_attack()&&me.attack_tiles().is_set(live.position)&&same_round(round)&&cambc::fuel_remaining()>=16000) {me.attack(target);acted=true;}
    if(!same_round(round)||cambc::fuel_remaining()<20000||!combat_target(target,live)||!same_round(round))return acted;
    if(!me.can_attack()&&me.can_cast()&&me.attack_tiles().is_set(live.position))for(const auto& ability:me.abilities()) {
        if(!same_round(round)||cambc::fuel_remaining()<20000)return acted;
        if(ability.is_attack_reset()&&ability.can_target_self()&&ability.is_ready()&&movement_compatible(ability,walked)) {
            if(!same_round(round)||cambc::fuel_remaining()<20000)return acted;
            const bool reset=ability.use();acted|=reset;
            if(reset&&same_round(round)&&cambc::fuel_remaining()>=20000&&me.can_attack()&&combat_target(target,live)&&
                same_round(round)&&me.attack_tiles().is_set(live.position)&&
                same_round(round)&&cambc::fuel_remaining()>=16000)me.attack(target);
            break;
        }
    }
    return acted;
}
} // namespace p9::live
