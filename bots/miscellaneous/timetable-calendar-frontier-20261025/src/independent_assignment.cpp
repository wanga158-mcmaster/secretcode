#include "assignment_api.h"
#include "config.h"
#include "assignment_stage_probe.h"
#include "assignment_reachability_probe.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <type_traits>
#include <variant>
#include <vector>

namespace tt::assignment_api {
namespace {
namespace ap=assignment_protocol;
namespace pi=production_investment;
constexpr std::array roster={cambc::champion_type::varg,cambc::champion_type::marcella,
    cambc::champion_type::ryth,cambc::champion_type::vesper_vale,cambc::champion_type::heart_of_ruin};
std::array<std::uint8_t,5> roster_types() {
    std::array<std::uint8_t,5> out{};
    for(std::size_t i=0;i<roster.size();++i)out[i]=static_cast<std::uint8_t>(roster[i]);
    return out;
}
bool fence(int epoch) {return epoch>=0 && static_cast<int>(cambc::round_number())==epoch;}
bool guard(int epoch,std::uint64_t floor=20000) {
    if(!fence(epoch))return false;
    const auto fuel=cambc::fuel_remaining();
    if constexpr(cfg::assignment_stage_packet)assignment_stage_probe::record_guard(epoch,floor,fuel);
    if constexpr(cfg::assignment_reachability_audit)assignment_reachability_probe::guard(
        assignment_reachability_probe::role(),epoch,floor,fuel);
    return fuel>floor && fence(epoch);
}
CollectStatus failed(int epoch) {return fence(epoch)?CollectStatus::NeedCompute:CollectStatus::Stale;}
pi::Passive passive(std::optional<cambc::item_passive> p) {
    if(!p)return pi::Passive::None;
    if(*p==cambc::item_passive::monster)return pi::Passive::Monster;
    if(*p==cambc::item_passive::jungle)return pi::Passive::Jungle;
    if(*p==cambc::item_passive::durability)return pi::Passive::Durability;
    if(*p==cambc::item_passive::treasure)return pi::Passive::Treasure;
    if(*p==cambc::item_passive::home)return pi::Passive::Home;
    return pi::Passive::Unsupported;
}
pi::Item item(cambc::item_type kind,const cambc::item_info& v) {
    return {static_cast<std::uint32_t>(kind),v.cost,v.combine_count,
        {v.health_points,v.strength_points,v.focus_points},passive(v.passive),v.passive_magnitude,v.purchasable};
}
bool own_fresh(const ControllerContext& ctx,const ap::Prepared& own) {
    return ctx.quiet && own.complete && own.fresh && ctx.ready_validator &&
        ctx.ready_validator(ctx.validator_context,own) && fence(ctx.epoch);
}
} // namespace

CanonicalGroups canonical_groups(const World& world) {
    CanonicalGroups out;
    out.local_by_canonical.fill(-1);out.canonical_by_local.fill(-1);
    if(world.groups.empty() || world.groups.size()>31)return out;
    using Coordinate=std::array<int,2>; // row-major immutable positions, never handles.
    std::array<std::vector<Coordinate>,31> signatures;
    std::array<int,31> order{};
    for(std::size_t i=0;i<world.groups.size();++i) {
        const auto& members=world.groups[i].members;
        if(members.empty() || members.size()>mb::service::max_members)return out;
        for(const auto& m:members) {
            if(m.pos.x<0 || m.pos.y<0 || m.pos.x>=world.width || m.pos.y>=world.height)return out;
            signatures[i].push_back({m.pos.y,m.pos.x});
        }
        std::sort(signatures[i].begin(),signatures[i].end());
        if(std::adjacent_find(signatures[i].begin(),signatures[i].end())!=signatures[i].end())return out;
        order[i]=static_cast<int>(i);
    }
    std::sort(order.begin(),order.begin()+static_cast<std::ptrdiff_t>(world.groups.size()),
        [&](int a,int b){return signatures[static_cast<std::size_t>(a)]<signatures[static_cast<std::size_t>(b)];});
    for(std::size_t i=0;i<world.groups.size();++i) {
        if(i && signatures[static_cast<std::size_t>(order[i-1])]==signatures[static_cast<std::size_t>(order[i])])return out;
        out.local_by_canonical[i]=order[i];out.canonical_by_local[static_cast<std::size_t>(order[i])]=static_cast<int>(i);
    }
    out.count=world.groups.size();out.supported=true;return out;
}

void publish_roster_contract(bool complete,unsigned actual_team_size) {
    const auto record=ap::roster_contract(roster_types(),complete,actual_team_size);
    cambc::draft_memory=std::span<const std::byte>{record};
}

SelfInputs collect_self(const Observation& observation,std::uint64_t body_revision) {
    if(observation.channeling || !observation.enemies.empty()) {
        SelfInputs out;out.status=CollectStatus::NotQuiet;return out;
    }
    return collect_self(observation,observation.epoch,body_revision);
}

template<class Anchor> SelfInputs collect_self_anchor(const Anchor& observation,int expected_current_epoch,std::uint64_t body_revision) {
    SelfInputs out;
    const int epoch=expected_current_epoch;
    // Ready describes the provenance of the anchored SELF values at its own
    // date. It does not refresh any world observations at the requested date.
    if(observation.status!=Status::Ready || !observation.body_complete || observation.epoch>epoch) {
        out.status=CollectStatus::Unsupported;out.reason=SelfReason::IncompleteSelf;return out;
    }
    if(!guard(epoch,70000)) {out.status=failed(epoch);return out;}
    const auto& me=cambc::self();
    const auto type=me.champion_name();
    const auto role=std::find(roster.begin(),roster.end(),type);
    if(role==roster.end()) {out.status=CollectStatus::Unsupported;out.reason=SelfReason::Roster;return out;}
    out.role=static_cast<unsigned>(role-roster.begin());
    if constexpr(cfg::assignment_reachability_audit)assignment_reachability_probe::select(out.role);
    out.roster_verified=ap::roster_contract_valid(
        std::span<const std::byte>{cambc::draft_memory.data(),cambc::draft_memory.size()},roster_types());
    if(!out.roster_verified) {out.status=CollectStatus::Unsupported;out.reason=SelfReason::Roster;return out;}
    if(cambc::shared_memory.size()!=8 || cambc::constants::shared_memory_bytes!=8 ||
       cambc::constants::max_rounds>2047) {out.status=CollectStatus::Unsupported;out.reason=SelfReason::MemoryCapacity;return out;}
    const auto flags=me.state();
    if(!flags.contains(cambc::champion_state::alive) || flags.contains(cambc::champion_state::recalling) ||
       flags.contains(cambc::champion_state::submitting_gems) || flags.contains(cambc::champion_state::teleporting)) {
        out.status=CollectStatus::NotQuiet;return out;
    }
    const auto buffs=me.buffs();
    // This first producer has no initial-buff lifetime field. Refuse rather than
    // let a dated shield, stun or enhanced basic persist through a forecast.
    if(buffs.size()!=0) {out.status=CollectStatus::Unsupported;out.reason=SelfReason::ActiveBuff;return out;}
    auto& value=out.snapshot;
    value.actor=out.role;value.body_revision=body_revision;value.epoch=static_cast<std::uint32_t>(epoch);
    const auto actual_position=me.position();
    const float actual_hp=me.health(),actual_max_hp=me.max_health();
    const int actual_range=me.attack_range();
    const auto actual_passive=me.passive();
    value.gold=me.gold_count();value.carried_gems=me.gem_count();
    const float raw_strength=me.strength(),focus=me.focus();
    const auto design=me.base_stats();
    const bool actual_move=flags.contains(cambc::champion_state::can_move);
    const bool actual_attack=flags.contains(cambc::champion_state::can_attack);
    const bool actual_cast=flags.contains(cambc::champion_state::can_cast);
    const auto& anchor=observation.service_body;
    const float missing_bonus=actual_passive==cambc::passive_type::missing_health_damage?
        cambc::constants::passive_abilities::heart_of_ruin_missing_health_damage_max_bonus:0;
    if(!std::isfinite(actual_max_hp) || actual_max_hp<=0) {out.status=CollectStatus::Unsupported;return out;}
    const float actual_hp_factor=1+missing_bonus*std::clamp(1.0f-actual_hp/actual_max_hp,0.0f,1.0f);
    const float actual_strength=raw_strength*actual_hp_factor;
    if(actual_position!=observation.pos || actual_hp!=observation.hp || actual_max_hp!=observation.max_hp ||
       raw_strength!=observation.raw_strength || focus!=observation.focus || value.gold!=observation.gold ||
       value.carried_gems!=observation.gems || anchor.pos.x!=actual_position.x || anchor.pos.y!=actual_position.y ||
       anchor.hp!=actual_hp || anchor.max_hp!=actual_max_hp || anchor.focus!=focus ||
       anchor.strength!=actual_strength || observation.strength!=actual_strength || anchor.attack_range!=actual_range ||
       observation.attack_range!=actual_range || observation.ranged!=(actual_range>1) ||
       observation.splash!=(actual_passive==cambc::passive_type::splash_attack) || observation.channeling ||
       anchor.ranged!=(actual_range>1) || anchor.splash!=(actual_passive==cambc::passive_type::splash_attack) ||
       anchor.shield!=0 || anchor.next_basic_stun!=0 || anchor.can_move!=actual_move ||
       anchor.can_attack!=actual_attack || anchor.can_cast!=actual_cast ||
       observation.can_move!=actual_move || observation.can_attack!=actual_attack ||
       observation.can_cast!=actual_cast || !fence(epoch)) {
        out.status=CollectStatus::Stale;return out;
    }
    if(observation.epoch!=epoch && (!actual_move || !actual_attack || !actual_cast)) {
        out.status=CollectStatus::Stale;return out;
    }
    // Publish actual own reads, not a redated copy of the anchor or its masks.
    value.body.pos={actual_position.x,actual_position.y};value.body.hp=actual_hp;value.body.max_hp=actual_max_hp;
    value.body.strength=actual_strength;value.body.focus=focus;value.body.attack_range=actual_range;
    value.body.ranged=actual_range>1;value.body.splash=actual_passive==cambc::passive_type::splash_attack;
    value.body.can_move=actual_move;value.body.can_attack=actual_attack;value.body.can_cast=actual_cast;
    value.body.moved_this_turn=observation.epoch==epoch?anchor.moved_this_turn:false;
    const auto counts=me.item_count();
    if(counts.size()>64) {out.status=CollectStatus::Unsupported;return out;}
    out.inventory_size=counts.size();
    auto& shop=value.shop;
    shop.gold=value.gold;shop.body={value.body.hp,value.body.max_hp,raw_strength,focus};
    shop.growth={design.health_growth,design.strength_growth,design.focus_growth};
    shop.strength_per_bonus_health=type==cambc::champion_type::ryth?
        cambc::constants::passive_abilities::ryth_health_strength_ratio:0;
    std::uint32_t item_index{};
    for(const auto count:counts) {
        out.inventory[item_index]=count;
        if(count) {
            if(!guard(epoch)) {out.status=failed(epoch);return out;}
            const auto metadata=cambc::item_type{item_index}.info();
            if(!pi::detail::activate(shop.effects,passive(metadata.passive),metadata.passive_magnitude)) {
                out.status=CollectStatus::Unsupported;return out;
            }
        }
        ++item_index;
    }
    using I=cambc::item_type;
    const auto shards=type==cambc::champion_type::ryth?
        std::array<I,5>{I::monster_shard_of_focus,I::jungle_shard_of_focus,I::durability_shard_of_focus,I::treasure_shard_of_focus,I::power_shard_of_focus}:
        std::array<I,5>{I::monster_shard_of_strength,I::jungle_shard_of_strength,I::durability_shard_of_strength,I::treasure_shard_of_strength,I::power_shard_of_strength};
    for(std::size_t i=0;i<shards.size();++i) {
        if(!guard(epoch)) {out.status=failed(epoch);return out;}
        const auto metadata=shards[i].info();auto& family=shop.families[i];
        family.shard=item(shards[i],metadata);family.shards_owned=counts[shards[i]];
        if(metadata.result) {
            family.has_result=true;family.result=item(*metadata.result,metadata.result->info());
            family.result_owned=counts[*metadata.result];
        }
    }
    // These are declared FUTURE spawn/readback premises, never current shop permission.
    shop.metadata_complete=true;shop.shop_at_declared_spawn=true;
    shop.stable_raw_stat_projection=true;
    value.shop_supported=true;value.stable_outgoing=true;
    value.raw_strength=raw_strength;value.raw_focus=focus;
    value.missing_health_bonus=missing_bonus;
    value.incoming_scale=design.camp_damage_multiplier;
    const float hp_factor=1+value.missing_health_bonus*std::clamp(1.0f-value.body.hp/value.body.max_hp,0.0f,1.0f);
    if(!std::isfinite(hp_factor) || hp_factor<=0 || !std::isfinite(value.incoming_scale) || value.incoming_scale<0) {
        out.status=CollectStatus::Unsupported;out.reason=SelfReason::HpDependentOutgoing;
        out.unsupported_caps|=SelfCapability::HpDependentOutgoing;return out;
    }
    const float camp_bonus=1+shop.effects.monster;
    // The existing observation's incoming field excludes innate design scaling.
    // Item-only mitigation is an anchored dependency, independently re-derived.
    if(anchor.camp_damage_multiplier!=(1-shop.effects.jungle)*(1-shop.effects.durability) ||
       observation.damage_multiplier!=anchor.camp_damage_multiplier) {
        out.status=CollectStatus::Stale;return out;
    }
    // This fixed verified roster has no further basic outgoing trait. Using
    // its exact scale avoids an inverse float round-trip inventing a bonus.
    value.basic_scale=1;
    value.body.strength*=camp_bonus;
    value.body.camp_damage_multiplier=value.incoming_scale*(1-shop.effects.jungle)*(1-shop.effects.durability);
    const auto abilities=me.abilities();
    for(std::size_t slot=0;slot<abilities.size();++slot) {
        if(!guard(epoch)) {out.status=failed(epoch);return out;}
        const auto& ability=abilities[slot];const auto& info=ability.info();
        if(!info)continue;
        const auto cooldown=ability.cooldown();
        if(cooldown<0) {out.status=CollectStatus::Unsupported;return out;}
        value.cooldown[slot]=static_cast<std::uint32_t>(cooldown);
        bool disabled=false;
        std::visit([&](const auto& typed) {
            using T=std::decay_t<decltype(typed)>;
            if(typed.max_cooldown<0) {disabled=true;return;}
            value.period[slot]=static_cast<std::uint32_t>(typed.max_cooldown);
            auto& spell=value.spells[slot];spell.exclusive=typed.exclusive_with_movement;
            if constexpr(std::is_same_v<T,cambc::ability_attack_reset>) {
                if(typed.enhanced_attack || !typed.valid_targets.contains(cambc::ability_target_mask::self))disabled=true; // omitted stationary subfamily
                else spell.kind=mb::service::SpellKind::reset;
            } else if constexpr(std::is_same_v<T,cambc::ability_shield>) {
                if(!typed.valid_targets.contains(cambc::ability_target_mask::self)) {disabled=true;return;}
                spell.kind=mb::service::SpellKind::shield;spell.damage=ability.shield_amount();
                spell.duration=typed.duration;spell.range=static_cast<int>(typed.range);
                value.focus_modifier[slot]=typed.modifier;
            } else if constexpr(std::is_same_v<T,cambc::ability_damage> || std::is_same_v<T,cambc::ability_aoe_damage> || std::is_same_v<T,cambc::ability_cc>) {
                if(!typed.valid_targets.contains(cambc::ability_target_mask::camps)) {disabled=true;return;}
                spell.range=static_cast<int>(typed.range);
                const auto actual_damage=ability.damage_amount();
                spell.damage=actual_damage*camp_bonus;
                value.focus_modifier[slot]=typed.modifier;
                if constexpr(std::is_same_v<T,cambc::ability_damage>)spell.kind=mb::service::SpellKind::damage;
                else if constexpr(std::is_same_v<T,cambc::ability_aoe_damage>) {
                    spell.kind=mb::service::SpellKind::aoe;spell.radius=static_cast<int>(typed.radius);spell.duration=typed.duration;
                } else {spell.kind=mb::service::SpellKind::cc;spell.duration=typed.duration;}
            } else disabled=true; // dash/heal are not stationary-word operations
            spell.ready=!disabled && cooldown==0 && value.body.can_cast;
        },*info);
        if(disabled) {value.spells[slot]={};out.disabled_slots|=static_cast<std::uint8_t>(1u<<slot);}
    }
    if(!fence(epoch) || me.position()!=observation.pos || !fence(epoch)) {out.status=CollectStatus::Stale;return out;}
    value.own=true;value.complete=true;out.status=CollectStatus::Ready;return out;
}

SelfInputs collect_self(const Observation& observation,int epoch,std::uint64_t revision) {
    return collect_self_anchor(observation,epoch,revision);
}
SelfCheckpoint checkpoint(const Observation& observation) {
    return {observation.status,observation.epoch,observation.attack_range,observation.pos,
        observation.hp,observation.max_hp,observation.raw_strength,observation.strength,observation.focus,
        observation.damage_multiplier,observation.gold,observation.gems,observation.ranged,observation.splash,
        observation.channeling,observation.can_move,observation.can_attack,observation.can_cast,
        observation.service_body,observation.body_complete};
}
SelfInputs collect_self(const SelfCheckpoint& observation,int epoch,std::uint64_t revision) {
    return collect_self_anchor(observation,epoch,revision);
}

SelfInputs collect_current_self(int epoch,std::uint64_t revision,bool walked_this_input,
    SelfCheckpoint* checkpoint_out) {
    SelfInputs out;
    // Explicit new-owner funding gate. This is an admission assumption, not a
    // measured cost bound; the unchanged full collector still requires70K.
    if(!guard(epoch,85000)) {out.status=failed(epoch);return out;}
    if(walked_this_input) {out.status=CollectStatus::NotQuiet;return out;}
    const auto& me=cambc::self();
    const auto flags=me.state();
    if(!fence(epoch)) {out.status=CollectStatus::Stale;return out;}
    if(!flags.contains(cambc::champion_state::alive) || flags.contains(cambc::champion_state::recalling) ||
       flags.contains(cambc::champion_state::submitting_gems) || flags.contains(cambc::champion_state::teleporting) ||
       !flags.contains(cambc::champion_state::can_move) || !flags.contains(cambc::champion_state::can_attack) ||
       !flags.contains(cambc::champion_state::can_cast)) {
        out.status=CollectStatus::NotQuiet;return out;
    }
    const auto buffs=me.buffs();
    if(!fence(epoch)) {out.status=CollectStatus::Stale;return out;}
    if(buffs.size()!=0) {out.status=CollectStatus::Unsupported;out.reason=SelfReason::ActiveBuff;return out;}
    if(!guard(epoch,70000)) {out.status=failed(epoch);return out;}
    SelfCheckpoint anchor;
    anchor.epoch=epoch;anchor.pos=me.position();anchor.hp=me.health();anchor.max_hp=me.max_health();
    anchor.raw_strength=me.strength();anchor.focus=me.focus();anchor.attack_range=me.attack_range();
    const auto own_passive=me.passive();anchor.gold=me.gold_count();anchor.gems=me.gem_count();
    if(!fence(epoch)) {out.status=CollectStatus::Stale;return out;}
    if(anchor.pos.x<0 || anchor.pos.y<0 || !std::isfinite(anchor.hp) || !std::isfinite(anchor.max_hp) ||
       anchor.hp<=0 || anchor.max_hp<=0 || anchor.hp>anchor.max_hp ||
       !std::isfinite(anchor.raw_strength) || anchor.raw_strength<0 || !std::isfinite(anchor.focus) ||
       anchor.focus<0 || anchor.attack_range<0) {
        out.status=CollectStatus::Unsupported;out.reason=SelfReason::IncompleteSelf;return out;
    }
    const float bonus=own_passive==cambc::passive_type::missing_health_damage?
        cambc::constants::passive_abilities::heart_of_ruin_missing_health_damage_max_bonus:0;
    const float factor=1+bonus*std::clamp(1.0f-anchor.hp/anchor.max_hp,0.0f,1.0f);
    anchor.strength=anchor.raw_strength*factor;anchor.ranged=anchor.attack_range>1;
    anchor.splash=own_passive==cambc::passive_type::splash_attack;
    anchor.can_move=anchor.can_attack=anchor.can_cast=true;anchor.channeling=false;
    // The legacy anchor stores ITEM-only incoming mitigation. Derive it from
    // actual inventory; never substitute adjusted Snapshot.body or an old value.
    if(!guard(epoch,70000)) {out.status=failed(epoch);return out;}
    const auto counts=me.item_count();
    if(!fence(epoch)) {out.status=CollectStatus::Stale;return out;}
    if(counts.size()>64) {out.status=CollectStatus::Unsupported;out.reason=SelfReason::ItemMetadata;return out;}
    pi::Effects effects;std::uint32_t index{};
    for(const auto count:counts) {
        if(count) {
            if(!guard(epoch,70000)) {out.status=failed(epoch);return out;}
            const auto metadata=cambc::item_type{index}.info();
            if(!fence(epoch)) {out.status=CollectStatus::Stale;return out;}
            if(!pi::detail::activate(effects,passive(metadata.passive),metadata.passive_magnitude)) {
                out.status=CollectStatus::Unsupported;out.reason=SelfReason::ItemMetadata;return out;
            }
        }
        ++index;
    }
    anchor.damage_multiplier=(1-effects.jungle)*(1-effects.durability);
    auto& body=anchor.service_body;
    body.pos={anchor.pos.x,anchor.pos.y};body.hp=anchor.hp;body.max_hp=anchor.max_hp;
    body.strength=anchor.strength;body.focus=anchor.focus;body.attack_range=anchor.attack_range;
    body.ranged=anchor.ranged;body.splash=anchor.splash;
    body.can_move=body.can_attack=body.can_cast=true;body.moved_this_turn=false;
    body.shield=0;body.next_basic_stun=0;body.camp_damage_multiplier=anchor.damage_multiplier;
    if(!fence(epoch)) {out.status=CollectStatus::Stale;return out;}
    anchor.body_complete=true;anchor.status=Status::Ready;
    out=collect_self_anchor(anchor,epoch,revision);
    if(out.status!=CollectStatus::Ready)return out;
    if(!fence(epoch)) {out.status=CollectStatus::Stale;return out;}
    if(checkpoint_out)*checkpoint_out=anchor;
    return out;
}

std::optional<ap::Prepared> prepared(const production_assignment::Difference& d,std::uint64_t premise,bool fresh,ap::Scenario scenario) {
    const auto& k=d.key;
    if(scenario!=ap::Scenario::FullGenerationAtCommonStart || !d.complete || !d.supported || !d.disjoint_supported || !production_assignment::canonical(k) ||
       k.start>2047 || k.horizon>2047 || k.workers[1]>=5 || k.groups[1]>=31 ||
       (d.actor!=k.workers[0] && d.actor!=k.workers[1]) || d.delta<-1024 || d.delta>1023)return std::nullopt;
    ap::Key key{static_cast<std::uint16_t>(k.start),static_cast<std::uint16_t>(k.horizon),
        static_cast<std::uint8_t>(k.workers[0]),static_cast<std::uint8_t>(k.workers[1]),
        static_cast<std::uint8_t>(k.groups[0]),static_cast<std::uint8_t>(k.groups[1])};
    if(!ap::valid(key))return std::nullopt;
    return ap::Prepared{key,premise,static_cast<int>(d.delta),true,fresh,scenario,static_cast<std::uint8_t>(d.actor)};
}

Exchange step(Runtime& runtime,const ControllerContext& ctx) {
    Exchange out;
    if(ctx.canonical_group_count>31) {out.transport=CollectStatus::Unsupported;return out;}
    const ap::Limits limits{static_cast<unsigned>(cambc::shared_memory.size()),cambc::constants::max_rounds,
        static_cast<unsigned>(ctx.canonical_group_count),ctx.roster_verified};
    if(!ap::supported(limits) || ctx.role>=5) {out.transport=CollectStatus::Unsupported;return out;}
    if(!guard(ctx.epoch)) {out.transport=failed(ctx.epoch);return out;}
    if(cambc::self().champion_name()!=roster[ctx.role]) {out.transport=CollectStatus::Unsupported;return out;}
    if(!guard(ctx.epoch)) {out.transport=failed(ctx.epoch);return out;}
    std::array<std::byte,8> data{};
    cambc::shared_memory.read(0,std::span<std::byte>{data});
    if(!fence(ctx.epoch)) {out.transport=CollectStatus::Stale;return out;}
    const auto read=ap::decode(ap::word(data));
    ap::Input input{limits,static_cast<unsigned>(ctx.epoch),ctx.role,ctx.quiet,ctx.own};
    if(input.own)input.own->fresh=own_fresh(ctx,*input.own);
    if(!fence(ctx.epoch)) {out.transport=CollectStatus::Stale;return out;}
    auto next=runtime.protocol;
    auto decision=ap::advance(next,input,read);
    if(decision.needed && ctx.quiet && ctx.quote_provider) {
        input.own=ctx.quote_provider(ctx.validator_context,*decision.needed);
        if(input.own)input.own->fresh=own_fresh(ctx,*input.own);
        if(!fence(ctx.epoch)) {out.transport=CollectStatus::Stale;return out;}
        if(input.own)decision=ap::advance(next,input,read);
    }
    if(decision.write || decision.nomination) {
        if((decision.write && decision.write->phase!=ap::Phase::Withdraw) || decision.nomination) {
            if(!input.own || !own_fresh(ctx,*input.own)) {
                if(input.own)input.own->fresh=false;
                decision=ap::advance(next,input,read);
            }
        }
        if(!fence(ctx.epoch)) {runtime.protocol=next;out.transport=CollectStatus::Stale;return out;}
    }
    if(decision.write) {
        const auto encoded=ap::encode(*decision.write);
        if(!encoded) {runtime.protocol=next;out.transport=CollectStatus::Unsupported;return out;}
        if(!guard(ctx.epoch)) {runtime.protocol=next;out.transport=failed(ctx.epoch);return out;}
        const auto payload=ap::bytes(*encoded);
        cambc::shared_memory.write(0,std::span<const std::byte>{payload});
        if(!fence(ctx.epoch)) {runtime.protocol=next;out.transport=CollectStatus::Stale;return out;}
        out.wrote=true;
    }
    runtime.protocol=next;out.decision=decision;out.transport=CollectStatus::Ready;return out;
}
} // namespace tt::assignment_api
