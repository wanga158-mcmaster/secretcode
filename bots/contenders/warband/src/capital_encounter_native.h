#pragma once
#include "capital_encounter.h"
#include "config.h"
#ifndef WB_ENCOUNTER_NATIVE_HOST_FREE
#include <cambc.h>
#include "capital_service_native.h"
#include <iostream>
#else
namespace cambc {class bitboard;class distance_field;}
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// Conditional, finite, observation-bound encounter offers. A modeled prefix
// refutation is not a command permission or a claim about all enemy policies.
// Runtime keeps the actual complete economic/recovery interpreters and owns
// every accepted/refused primitive and its full fresh readback.
namespace wb::capital::encounter_native {
namespace e=encounter;
enum class Status {Ready,NoOffer,NeedFuel,Stale,Unsupported,Unknown,NoRefutation};
enum class Rejection {None,NoFoe,Resource,Capacity,HiddenFoe,UnsupportedKit,Channel,Consumable,
    Restrictions,Buffs,Priority,Geometry,Npc,Forecast,Funding};
// These are observed capability faults, not probabilities or permission to
// ignore the body. Multiple faults on the first rejected body are retained.
enum CoverageFault : unsigned {UnknownPassive=1u,AttackType=2u,ChargedAbility=4u,
    UnmappedAbility=8u,NonfiniteAbility=16u,Consumable=32u,Channel=64u,Restrictions=128u};
struct Coverage {unsigned faults{};int passive{-1},attack_type{-1};bool sampled_bonus_health_strength{};};
inline void classify_effects(Coverage& coverage,const e::Body& b) {
    if(b.channel_present)coverage.faults|=Channel;
    if(b.non_timed_restrictions)coverage.faults|=Restrictions;
}
struct Source {int id{-1};e::Point position{};int range{};float damage{};bool observed_awake{};};
struct Input {
    int owner{-1},round{-1},ordinal{-1},width{},height{};std::uint64_t input{},effect{};
    e::Point economic_step{},recovery_step{};
    std::span<const int> own_order{};
    std::span<const Source> sources{};
    const cambc::bitboard* walk_blocked{};
    const cambc::bitboard* own_spawn{};
    const cambc::bitboard* enemy_spawn{};
    const cambc::bitboard* camp_occupancy{};
    const cambc::distance_field* recovery_field{};
};
struct Result {
    Status status{Status::NoOffer};Rejection rejection{Rejection::None};
    int owner{-1},round{-1};std::uint64_t input{},effect{},assumptions{};
    e::Point source{},recovery{};e::Word word{};
    e::Outcome economic{},restoration{};
    bool conditional{},economic_refuted_before_usable{},restored{};
    int rejected_actor{-1};Coverage coverage{};
};
struct Counters {
    std::uint64_t nominations{},inputs{},offers{},priced{},refuted{},selected{},accepted{},refused{},restored{};
    std::array<std::uint64_t,static_cast<unsigned>(Rejection::Funding)+1> rejected{};
    std::array<std::uint64_t,8> coverage_faults{};
    std::array<std::uint64_t,16> rejected_actor{};
    std::uint64_t sampled_bonus_health_strength{};
};
inline Counters& counters(){static Counters value;return value;}
struct Memory {
    bool restoring{},claimed_this_input{};int owner{-1},start_round{-1};
    std::uint64_t input{},assumptions{},actual_prefixes{};
};
inline Memory& memory(){static Memory value;return value;}
#ifndef WB_ENCOUNTER_NATIVE_HOST_FREE
inline bool current(const Input& i) {
    return i.owner==static_cast<int>(static_cast<cambc::handle_champion_t>(cambc::self()))
        &&i.round==static_cast<int>(cambc::round_number());
}
#endif
inline bool finite(float x){return std::isfinite(x)&&x>=0&&x<=1000000000.f;}
inline std::uint64_t mix(std::uint64_t a,std::uint64_t b){return (a^b)*1099511628211ull;}
inline const e::Body* body(const e::World& world,int id) {
    for(unsigned n=0;n<world.count;++n)if(world.bodies[n].id==id)return &world.bodies[n];return nullptr;
}
inline e::Body* body(e::World& world,int id) {
    for(unsigned n=0;n<world.count;++n)if(world.bodies[n].id==id)return &world.bodies[n];return nullptr;
}
#ifndef WB_ENCOUNTER_NATIVE_HOST_FREE
inline int identifier(const cambc::champion& c){return static_cast<int>(static_cast<cambc::handle_champion_t>(c));}
struct DesignCache {
    std::array<cambc::detail::champion_design,32> values{};std::array<bool,32> known{};
};
inline const cambc::detail::champion_design* design(int type) {
    static DesignCache cached;
    if(type<0||static_cast<unsigned>(type)>=cached.values.size())return nullptr;
    if(!cached.known[static_cast<unsigned>(type)]) {
        cambc::detail::check(cambc::detail::sdk_champion_design(type,&cached.values[static_cast<unsigned>(type)]));
        cached.known[static_cast<unsigned>(type)]=true;
    }
    return &cached.values[static_cast<unsigned>(type)];
}
inline bool represented(cambc::passive_type p) {
    return p==cambc::passive_type::none||p==cambc::passive_type::bonus_gold||p==cambc::passive_type::splash_attack
        ||p==cambc::passive_type::reset_cooldowns_on_kill||p==cambc::passive_type::stun_immunity
        ||p==cambc::passive_type::missing_health_damage||p==cambc::passive_type::health_strength;
}
inline e::Ability ability(const cambc::detail::ability_props& p,float focus) {
    e::Ability a;a.max_cooldown=p.cooldown;a.target_mask=p.targets;
    a.excludes_walk=(p.attributes&cambc::detail::ability_attribute_exclusive_with_movement)!=0;
    switch(static_cast<cambc::ability_kind>(p.kind)) {
    case cambc::ability_kind::attack_reset:a.kind=e::Kind::Reset;a.enhanced_reset=p.payload.enhanced_attack!=0;break;
    case cambc::ability_kind::damage:a.kind=e::Kind::Damage;a.range=p.payload.damage.range;a.amount=focus*p.payload.damage.modifier;break;
    case cambc::ability_kind::aoe_damage:a.kind=e::Kind::Area;a.range=p.payload.aoe_damage.range;a.radius=p.payload.aoe_damage.radius;
        a.amount=focus*p.payload.aoe_damage.modifier;a.stun_turns=p.payload.aoe_damage.duration;break;
    case cambc::ability_kind::cc:a.kind=e::Kind::Stun;a.range=p.payload.cc.range;a.amount=focus*p.payload.cc.modifier;a.stun_turns=p.payload.cc.duration;break;
    case cambc::ability_kind::dash:a.kind=e::Kind::Dash;a.range=p.payload.dash.range;a.radius=p.payload.dash.radius;
        a.amount=focus*p.payload.dash.modifier;a.stun_turns=p.payload.dash.duration;break;
    case cambc::ability_kind::shield:a.kind=e::Kind::Shield;a.range=p.payload.shield.range;a.amount=focus*p.payload.shield.modifier;a.duration=p.payload.shield.duration;break;
    case cambc::ability_kind::heal:a.kind=e::Kind::Heal;a.range=p.payload.heal.range;a.amount=focus*p.payload.heal.modifier;break;
    default:break;
    }
    return a;
}
#endif
struct Snapshot {
    e::World world{};std::array<int,e::body_limit> types{},current_priority{};
    std::array<bool,e::body_limit> completed{},priority_known{};
    std::array<std::vector<std::uint8_t>,e::body_limit> stationary_vision{};
    std::vector<std::uint8_t> walk{},spawn{},enemy_spawn{},camps{};
    std::vector<std::uint16_t> recovery_distances{};
    std::array<float,e::body_limit> npc_mitigation{};
    std::array<Coverage,e::body_limit> coverage{};
    std::vector<unsigned> empty_awake_phases{};
    std::vector<Source> actual_sources{};
    std::array<e::Slot,96> slots{};unsigned slot_count{};
    std::vector<int> own{},foe{};
    Input source{};Rejection rejected{Rejection::None};
    int root_team{-1},horizon{-1};
};
#ifndef WB_ENCOUNTER_NATIVE_HOST_FREE
inline bool capture_body(const Input& input,int id,e::Body& b,int& type,bool& complete,int& priority,bool& priority_known,float& npc_mitigation,Coverage* captured=nullptr) {
    Coverage coverage;
    cambc::detail::champion_identity identity{};cambc::detail::check(cambc::detail::sdk_champion_identity(id,&identity));
    b.id=id;b.team=identity.team;type=identity.type;
    cambc::detail::champion_info raw{};const auto code=cambc::detail::sdk_champion(id,&raw);
    if(!current(input))return false;
    if(code==static_cast<int>(cambc::error_code::api_not_in_vision)||code==static_cast<int>(cambc::error_code::api_not_found)) {
        // No position, HP, support or action is invented for this public ID.
        b.modeled=false;return true;
    }
    cambc::detail::check(code);
    b.position={raw.x,raw.y};b.hp=raw.health;b.max_hp=raw.max_health;b.basic=raw.strength;
    b.move_range=raw.move_speed;b.attack_range=raw.attack_range;
    const auto state=cambc::champion_state::from_bits_retain(raw.state);
    b.alive=state.contains(cambc::champion_state::alive);complete=state.contains(cambc::champion_state::completed_turn);
    b.completed_turn=complete;
    b.m=state.contains(cambc::champion_state::can_move);b.a=state.contains(cambc::champion_state::can_attack);b.c=state.contains(cambc::champion_state::can_cast);
    b.provenance_known=b.m;b.walked=false;
    b.channel_present=state.contains(cambc::champion_state::recalling)||state.contains(cambc::champion_state::submitting_gems)||state.contains(cambc::champion_state::teleporting);
    const auto* d=design(raw.type);if(!d||!current(input))return false;
    const auto passive=static_cast<cambc::passive_type>(d->definition.passive_type);
    coverage.passive=d->definition.passive_type;coverage.attack_type=d->definition.attack_type;
    if(!represented(passive))coverage.faults|=UnknownPassive;
    if(d->definition.attack_type>1)coverage.faults|=AttackType;
    // Ryth converts BONUS MAX health, not lost/current health. Queried raw
    // Strength already contains that contribution. The admitted scenario has
    // no shopping, stat mutation or max-health effect, so this magnitude stays
    // fixed; adding the passive again would double count it. A family admitting
    // equipment/max-health changes must supply their stat transition instead.
    coverage.sampled_bonus_health_strength=passive==cambc::passive_type::health_strength;
    b.effects_closed=represented(passive)&&d->definition.attack_type<=1;b.ranged=d->definition.attack_type==1;
    b.stun_immune=passive==cambc::passive_type::stun_immunity;b.any_kill_refresh=passive==cambc::passive_type::reset_cooldowns_on_kill;
    b.heart=passive==cambc::passive_type::missing_health_damage;
    // These passive parameters have no runtime magnitude import in SDK3.0.5;
    // their values belong to the pinned documented-action model, not tuning.
    b.heart_bonus=b.heart?.5f:0.f;b.basic_splash=passive==cambc::passive_type::splash_attack?1.f:0.f;
    b.enhanced_basic_stun=0;
    for(unsigned slot=0;slot<2;++slot) {
        const auto& props=d->definition.abilities[slot];b.abilities[slot]=ability(props,raw.focus);
        if(props.kind==static_cast<unsigned>(cambc::ability_kind::attack_reset)&&props.payload.enhanced_attack)
            b.enhanced_basic_stun=static_cast<int>(props.payload.enhanced_attack);
        if(props.max_charges)coverage.faults|=ChargedAbility;
        if(!finite(b.abilities[slot].amount))coverage.faults|=NonfiniteAbility;
        if(props.kind&&b.abilities[slot].kind==e::Kind::None)coverage.faults|=UnmappedAbility;
        if(props.max_charges||!finite(b.abilities[slot].amount)||(props.kind&&b.abilities[slot].kind==e::Kind::None))b.effects_closed=false;
        cambc::detail::check(cambc::detail::sdk_champion_cooldown(id,slot,&b.cooldown[slot]));
    }
    std::array<cambc::detail::buff_data,3> buffs{};std::uint32_t count=0;
    cambc::detail::check(cambc::detail::sdk_champion_buff_data(id,buffs.data(),buffs.size(),&count));
    if(count>buffs.size())return false;
    for(unsigned n=0;n<count;++n)switch(static_cast<cambc::buff_types>(buffs[n].type)) {
    case cambc::buff_types::shield:
        if(b.shield_present)return false;b.shield_present=true;b.shield=buffs[n].data.shield.value;b.shield_turns=buffs[n].data.shield.duration;break;
    case cambc::buff_types::stun:b.stun_present=true;b.stun_turns=buffs[n].data.stun.duration;break;
    case cambc::buff_types::enhanced_attack:b.enhanced_present=true;b.enhanced_turns=buffs[n].data.enhanced_attack.duration;break;
    default:return false;
    }
    std::uint8_t restrictions=0;cambc::detail::check(cambc::detail::sdk_champion_restrictions(id,&restrictions));
    b.non_timed_restrictions=b.stun_present?0:restrictions;
    std::array<std::uint32_t,4> consumables{};cambc::detail::check(cambc::detail::sdk_champion_consumables(id,consumables.data()));
    if(std::any_of(consumables.begin(),consumables.end(),[](auto n){return n!=0;})){b.effects_closed=false;coverage.faults|=Consumable;}
    classify_effects(coverage,b);if(captured)*captured=coverage;
    std::array<std::uint32_t,static_cast<unsigned>(cambc::item_type::count)> items{};
    cambc::detail::check(cambc::detail::sdk_champion_items(id,items.data()));
    for(const auto item:{cambc::item_type::durability_emblem_of_strength,cambc::item_type::durability_emblem_of_focus,cambc::item_type::durability_emblem_of_health})
        if(items[static_cast<unsigned>(item)]) {b.mitigation=1.f-item.passive_magnitude();break;}
    npc_mitigation=b.mitigation;
    for(const auto item:{cambc::item_type::jungle_emblem_of_strength,cambc::item_type::jungle_emblem_of_focus,cambc::item_type::jungle_emblem_of_health})
        if(items[static_cast<unsigned>(item)]) {npc_mitigation*=1.f-item.passive_magnitude();break;}
    const auto priority_code=cambc::detail::sdk_bc_champion_priority(id,0,&priority);
    if(priority_code==0)priority_known=true;
    else if(priority_code!=static_cast<int>(cambc::error_code::api_not_in_vision))cambc::detail::check(priority_code);
    return current(input)&&finite(b.hp)&&finite(b.max_hp)&&b.max_hp>0&&finite(b.basic)&&finite(b.mitigation)&&b.mitigation<=1;
}
#endif
inline unsigned index(const e::World& world,int id) {
    for(unsigned n=0;n<world.count;++n)if(world.bodies[n].id==id)return n;return world.count;
}
inline bool occupied(const e::World& w,e::Point p,int except) {
    for(unsigned n=0;n<w.count;++n)if(w.bodies[n].modeled&&w.bodies[n].alive&&w.bodies[n].id!=except&&w.bodies[n].position==p)return true;
    return false;
}
inline bool inside(const Snapshot& s,e::Point p){return p.x>=0&&p.y>=0&&p.x<s.source.width&&p.y<s.source.height;}
inline bool tile(const Snapshot& s,const std::vector<std::uint8_t>& bits,e::Point p) {
    if(!inside(s,p))return false;const auto n=static_cast<unsigned>(p.y*s.source.width+p.x);
    return n/8<bits.size()&&(bits[n/8]&(1u<<(n%8)))!=0;
}
inline constexpr auto unreachable=std::numeric_limits<std::uint16_t>::max();
inline std::uint16_t distance_to_spawn(const Snapshot& s,e::Point p) {
    if(!inside(s,p))return unreachable;
    const auto n=static_cast<unsigned>(p.y*s.source.width+p.x);
    return n<s.recovery_distances.size()?s.recovery_distances[n]:unreachable;
}
inline e::Verdict spawn(void* context,const e::World& world,int actor) {
    const auto& s=*static_cast<const Snapshot*>(context);const auto* b=body(world,actor);if(!b||!b->modeled)return e::Verdict::Unknown;
    return tile(s,b->team==s.root_team?s.spawn:s.enemy_spawn,b->position)?e::Verdict::Allowed:e::Verdict::Blocked;
}
inline e::Verdict visibility(void* context,const e::World& world,int observer,int target) {
    const auto& s=*static_cast<const Snapshot*>(context);if(observer==target)return e::Verdict::Allowed;
    const auto at=index(world,observer);const auto* victim=body(world,target);
    if(at==world.count||!victim||!world.bodies[at].modeled||!victim->modeled)return e::Verdict::Unknown;
    // This first legal reply family is stationary. Moving owner policies do
    // not borrow an old vision mask for attacks from a new pose.
    if(!(world.bodies[at].position==s.world.bodies[at].position))return e::Verdict::Unknown;
    if(s.stationary_vision[at].empty())return e::Verdict::Unknown;
    return tile(s,s.stationary_vision[at],victim->position)?e::Verdict::Allowed:e::Verdict::Blocked;
}
inline e::Verdict geometry(void* context,const e::World& world,int actor,const e::Primitive& primitive) {
    const auto& s=*static_cast<const Snapshot*>(context);const auto* b=body(world,actor);if(!b)return e::Verdict::Unknown;
    if(primitive.kind==e::PrimitiveKind::Move) {
        if(actor!=s.source.owner||b->move_range!=1||e::distance(b->position,primitive.destination)!=1)return e::Verdict::Unknown;
        if(!inside(s,primitive.destination)||tile(s,s.walk,primitive.destination)||occupied(world,primitive.destination,actor))return e::Verdict::Blocked;
        return e::Verdict::Allowed;
    }
    if(primitive.kind==e::PrimitiveKind::Basic) {
        const auto* target=body(world,primitive.target);if(!target)return e::Verdict::Unknown;
        if(b->basic_splash>0&&!s.camps.empty())
            for(int y=target->position.y-static_cast<int>(b->basic_splash);y<=target->position.y+static_cast<int>(b->basic_splash);++y)
                for(int x=target->position.x-static_cast<int>(b->basic_splash);x<=target->position.x+static_cast<int>(b->basic_splash);++x)
                    if(tile(s,s.camps,{x,y}))return e::Verdict::Unknown;
        return e::Verdict::Allowed;
    }
    if(primitive.slot<0||primitive.slot>=2)return e::Verdict::Blocked;
    const auto& a=b->abilities[static_cast<unsigned>(primitive.slot)];
    if(a.kind==e::Kind::Dash)return e::Verdict::Unknown; // distinct moving-reply family, not stationary pressure
    if(a.kind==e::Kind::Area&&primitive.target>=0)return e::Verdict::Unknown; // ground aim is the admitted native form
    if(a.kind==e::Kind::Area&&!s.camps.empty())
        for(int y=primitive.destination.y-static_cast<int>(std::ceil(a.radius));y<=primitive.destination.y+static_cast<int>(std::ceil(a.radius));++y)
            for(int x=primitive.destination.x-static_cast<int>(std::ceil(a.radius));x<=primitive.destination.x+static_cast<int>(std::ceil(a.radius));++x)
                if(tile(s,s.camps,{x,y}))return e::Verdict::Unknown;
    return e::Verdict::Allowed;
}
inline e::Point recovery_step(const Snapshot& s,const e::Observation& observed) {
    const auto* me=observed.find(observed.observer);if(!me||s.recovery_distances.empty())return me?me->position:e::Point{};
    const auto here=distance_to_spawn(s,me->position);auto best=here;auto result=me->position;
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
        const e::Point q{me->position.x+dx,me->position.y+dy};if(q==me->position||!inside(s,q))continue;
        if(tile(s,s.walk,q))continue;
        bool taken=false;for(unsigned n=0;n<observed.count;++n)if(observed.bodies[n].visible&&observed.bodies[n].body&&observed.bodies[n].body->alive
            &&observed.bodies[n].id!=me->id&&observed.bodies[n].body->position==q){taken=true;break;}
        if(taken)continue;const auto distance=distance_to_spawn(s,q);
        if(distance<best){best=distance;result=q;}
    }
    return result;
}
inline void append(e::Word& word,e::Primitive primitive){if(word.count<word.steps.size())word.steps[word.count++]=primitive;else word.complete=false;}
inline e::Word recovery_word(const Snapshot& s,const e::Observation& observed) {
    e::Word word;word.complete=true;const auto* me=observed.find(observed.observer);if(!me)return { };
    if(tile(s,s.spawn,me->position))return word;
    // Initial restoration family spends only movement. Shield/dash/heal arms
    // are distinct resource policies and must price their later cooldown tail.
    if(me->m) {const auto q=recovery_step(s,observed);if(!(q==me->position))append(word,{e::PrimitiveKind::Move,-1,-1,q});}
    return word;
}
inline e::Word policy(void* context,const e::Observation& observed) {
    const auto& s=*static_cast<const Snapshot*>(context);e::Word word;word.complete=true;
    const auto* me=observed.find(observed.observer);if(!me)return {};
    if(me->id==s.source.owner)return recovery_word(s,observed);
    // No uncommitted ally supplies future tanking, healing or damage.
    if(me->team==s.root_team)return word;
    for(unsigned n=0;n<observed.count;++n)if(observed.bodies[n].id==s.source.owner&&!observed.bodies[n].visibility_known)return {};
    const auto* victim=observed.find(s.source.owner);if(!victim||!victim->alive)return word;
    const auto distance=e::distance(me->position,victim->position);const bool basic=me->a&&distance<=me->attack_range;
    if(basic)append(word,{e::PrimitiveKind::Basic,victim->id,-1,{}});
    int cast=-1;
    if(me->c) {
        // One coherent control-first stationary word, not an independent union
        // of maximum damage, maximum stun and a free shield from another word.
        for(unsigned slot=0;slot<2;++slot) {
            const auto& a=me->abilities[slot];if(me->cooldown[slot]||a.kind==e::Kind::None||a.kind==e::Kind::Dash)continue;
            if(a.excludes_walk&&(!me->provenance_known||me->walked))continue;
            if(a.kind==e::Kind::Reset&&distance<=me->attack_range){cast=static_cast<int>(slot);break;}
            if((a.kind==e::Kind::Area||a.kind==e::Kind::Stun)&&a.stun_turns>0&&distance<=a.range){cast=static_cast<int>(slot);break;}
        }
        if(cast<0)for(unsigned slot=0;slot<2;++slot) {
            const auto& a=me->abilities[slot];if(me->cooldown[slot]||(a.excludes_walk&&(!me->provenance_known||me->walked)))continue;
            if((a.kind==e::Kind::Damage||a.kind==e::Kind::Area||a.kind==e::Kind::Stun)&&distance<=a.range){cast=static_cast<int>(slot);break;}
            if(a.kind==e::Kind::Shield&&!me->shield_present){cast=static_cast<int>(slot);break;}
        }
    }
    if(cast>=0) {
        const auto& a=me->abilities[static_cast<unsigned>(cast)];
        const int target=a.kind==e::Kind::Area?-1:a.kind==e::Kind::Reset||a.kind==e::Kind::Shield?me->id:victim->id;
        append(word,{e::PrimitiveKind::Cast,target,cast,victim->position});
        if(a.kind==e::Kind::Reset)append(word,{e::PrimitiveKind::Basic,victim->id,-1,{}});
    }
    return word;
}
inline e::Verdict ready(void* context,const e::Observation& observed) {
    const auto& s=*static_cast<const Snapshot*>(context);const auto* me=observed.find(s.source.owner);if(!me)return e::Verdict::Unknown;
    if(!me->alive||me->stun_present||me->channel_present||me->hp<me->max_hp||!me->m||!me->a||!me->c)return e::Verdict::Blocked;
    // Base Work/exit is usable without a reset cast. Cooldowns remain in the
    // dated terminal state; restoration never fabricates a kit refresh.
    return tile(s,s.spawn,me->position)?e::Verdict::Allowed:e::Verdict::Blocked;
}
inline unsigned rank_at(const Snapshot& s,int actor,int round) {
    const int starter=(s.source.round&1)?0:1;const int first=starter^((round-s.source.round)&1);
    for(unsigned n=0;n<s.own.size();++n)if(s.own[n]==actor)return n*2u+(s.root_team==first?0u:1u);
    for(unsigned n=0;n<s.foe.size();++n)if(s.foe[n]==actor)return n*2u+(s.root_team==first?1u:0u);
    return e::body_limit;
}
inline e::Phase phase(void* context,const e::World& world,int round) {
    auto& s=*static_cast<Snapshot*>(context);e::Phase result;result.complete=true;
    // Hidden sources are explicitly modeled asleep in this PARTIAL world
    // family. Actual observed awake sources cannot be erased. These policies
    // never attack camps; collateral camp wake is outside their geometry.
    std::array<float,e::body_limit> hp{},shield{};std::array<bool,e::body_limit> alive{};
    for(unsigned k=0;k<world.count;++k){hp[k]=world.bodies[k].hp;shield[k]=world.bodies[k].shield;alive[k]=world.bodies[k].alive;}
    for(unsigned n=0;n<s.source.sources.size();++n) {
        const auto& source=s.source.sources[n];if(!source.observed_awake)continue;
        unsigned chosen=world.count;int distance=100000;
        for(unsigned k=0;k<world.count;++k) {
            const auto& b=world.bodies[k];if(!b.modeled||!alive[k])continue;
            const auto d=e::distance(source.position,b.position);if(d>source.range)continue;
            if(d<distance||(d==distance&&(chosen==world.count||rank_at(s,b.id,round)<rank_at(s,world.bodies[chosen].id,round)))){distance=d;chosen=k;}
        }
        if(chosen==world.count){++s.empty_awake_phases[n];continue;}
        // The raw API has no current sleep countdown. A later return after an
        // empty phase needs another state branch, not a guessed awake timer.
        if(s.empty_awake_phases[n]){result.complete=false;return result;}
        if(result.count==result.effects.size()){result.complete=false;return result;}
        const auto damage=source.damage*s.npc_mitigation[chosen];
        result.effects[result.count++]={e::PhaseKind::Damage,source.id,world.bodies[chosen].id,damage,0};
        const auto absorbed=std::min(shield[chosen],damage);shield[chosen]=std::max(0.f,shield[chosen]-damage);
        hp[chosen]=std::max(0.f,hp[chosen]-std::max(0.f,damage-absorbed));if(hp[chosen]<=0)alive[chosen]=false;
    }
    return result;
}
inline Result reject(Result out,Status status,Rejection reason) {
    out.status=status;out.rejection=reason;++counters().rejected[static_cast<unsigned>(reason)];return out;
}
inline Result reject_coverage(Result out,int actor,const Coverage& coverage) {
    out.rejected_actor=actor;out.coverage=coverage;
    if(actor>=0&&static_cast<unsigned>(actor)<counters().rejected_actor.size())++counters().rejected_actor[static_cast<unsigned>(actor)];
    for(unsigned bit=0;bit<counters().coverage_faults.size();++bit)if(coverage.faults&(1u<<bit))++counters().coverage_faults[bit];
    return reject(out,Status::Unsupported,Rejection::UnsupportedKit);
}
#ifndef WB_ENCOUNTER_NATIVE_HOST_FREE
inline Result prepare(const Input& input) {
    Result out;out.owner=input.owner;out.round=input.round;out.input=input.input;out.effect=input.effect;
    ++counters().inputs;
    if(!current(input))return reject(out,Status::Stale,Rejection::Resource);
    if(!input.walk_blocked||!input.own_spawn||!input.enemy_spawn||!input.camp_occupancy||!input.recovery_field
        ||input.own_order.size()!=3||input.ordinal<0||input.ordinal>=3||input.width<=0||input.height<=0)return reject(out,Status::NoOffer,Rejection::Geometry);
    if(service_native::memory().foes.is_empty())return reject(out,Status::NoOffer,Rejection::NoFoe);
    // These are separate empirical observation, finite-model and one-command
    // readback obligations. Native qualification, not this floor, proves health.
    constexpr unsigned execute_tail=18000,model_tail=14000,observation=18000;
    if(cambc::fuel_remaining()<execute_tail+model_tail+observation)return reject(out,Status::NeedFuel,Rejection::Funding);
    Snapshot s;s.source=input;s.world.owner=input.owner;s.world.round=input.round;
    s.walk=input.walk_blocked->export_linear_buffer();s.spawn=input.own_spawn->export_linear_buffer();
    s.enemy_spawn=input.enemy_spawn->export_linear_buffer();s.camps=input.camp_occupancy->export_linear_buffer();
    s.recovery_distances=input.recovery_field->export_linear_buffer();
    const auto awake=cambc::vision::awake_camp_tiles().export_linear_buffer();
    s.actual_sources.assign(input.sources.begin(),input.sources.end());
    for(auto& source:s.actual_sources)source.observed_awake=tile(s,awake,source.position);
    s.source.sources={s.actual_sources.data(),s.actual_sources.size()};
    s.world.revision=mix(mix(1469598103934665603ull,input.input),input.effect);if(!s.world.revision)s.world.revision=1;
    s.world.respawn_ticks=cambc::constants::respawn_time;
    s.world.spawn_healing_fraction=cambc::constants::spawn_healing;s.world.spawn_healing_known=true;
    s.own.assign(input.own_order.begin(),input.own_order.end());
    const auto enemies=cambc::champions(cambc::team::enemy);if(enemies.size()!=3)return reject(out,Status::NoOffer,Rejection::Capacity);
    for(const auto& foe:enemies)s.foe.push_back(identifier(foe));
    s.world.count=6;s.empty_awake_phases.resize(input.sources.size());
    unsigned at=0;for(const auto& ids:{s.own,s.foe})for(const auto id:ids) {
        if(cambc::fuel_remaining()<execute_tail+model_tail+7000)return reject(out,Status::NeedFuel,Rejection::Funding);
        auto& b=s.world.bodies[at];
        if(!capture_body(input,id,b,s.types[at],s.completed[at],s.current_priority[at],s.priority_known[at],s.npc_mitigation[at],&s.coverage[at]))return reject(out,current(input)?Status::Unsupported:Status::Stale,Rejection::Buffs);
        if(b.id==input.owner){s.root_team=b.team;out.source=b.position;}
        if(std::find(s.foe.begin(),s.foe.end(),id)!=s.foe.end()&&!b.modeled)return reject(out,Status::NoOffer,Rejection::HiddenFoe);
        if(b.modeled&&(!b.effects_closed||b.channel_present||b.non_timed_restrictions))return reject_coverage(out,id,s.coverage[at]);
        if(b.modeled&&s.coverage[at].sampled_bonus_health_strength)++counters().sampled_bonus_health_strength;
        if(s.priority_known[at]&&s.current_priority[at]!=0)return reject(out,Status::Unsupported,Rejection::Priority);
        ++at;
    }
    const auto* me=body(s.world,input.owner);if(!me||!me->m||me->move_range!=1||!me->alive||me->stun_present||(!me->ranged&&me->a))return reject(out,Status::NoOffer,Rejection::Resource);
    if(input.economic_step==input.recovery_step||input.recovery_step==me->position)return reject(out,Status::NoOffer,Rejection::Geometry);
    const auto d0=distance_to_spawn(s,input.economic_step);
    const auto d1=distance_to_spawn(s,input.recovery_step);
    if(d0==cambc::distance_field::unreachable||d1==cambc::distance_field::unreachable)return reject(out,Status::NoOffer,Rejection::Geometry);
    int restore=static_cast<int>(std::max(d0,d1))+1;
    if(restore>14)return reject(out,Status::Unsupported,Rejection::Capacity);
    s.horizon=input.round+restore;
    for(unsigned n=0;n<s.world.count;++n)if(s.world.bodies[n].modeled&&s.world.bodies[n].team!=s.root_team) {
        if(cambc::fuel_remaining()<execute_tail+model_tail+5000)return reject(out,Status::NeedFuel,Rejection::Funding);
        const auto identity=cambc::detail::handle_access::make_champion(static_cast<cambc::handle_champion_t>(s.world.bodies[n].id));
        s.stationary_vision[n]=identity.visible_tiles_from({s.world.bodies[n].position.x,s.world.bodies[n].position.y}).export_linear_buffer();
        if(!current(input))return reject(out,Status::Stale,Rejection::Resource);
    }
    if(cambc::fuel_remaining()<execute_tail+model_tail)return reject(out,Status::NeedFuel,Rejection::Funding);
    const int starter=(input.round&1)?0:1;const int root_slot=input.ordinal*2+(s.root_team==starter?0:1);
    for(int round=input.round;round<=s.horizon;++round) {
        const int first=starter^((round-input.round)&1);
        for(unsigned rank=0;rank<6;++rank) {
            if(round==input.round&&static_cast<int>(rank)<=root_slot)continue;
            const bool own=(rank&1u)==static_cast<unsigned>(s.root_team==first?0:1);const auto id=(own?s.own:s.foe)[rank/2u];
            if(round==input.round) {
                const auto k=index(s.world,id);if(s.completed[k])return reject(out,Status::Unsupported,Rejection::Priority);
            }
            if(s.slot_count==s.slots.size())return reject(out,Status::Unsupported,Rejection::Capacity);
            s.slots[s.slot_count++]={round,id};
        }
    }
    // The family is deliberately explicit and partial: fixed compatible ranks,
    // stationary control-first enemy policy, uncommitted allies Hold, hidden
    // sources dormant, excluded ally IDs have no effects, and no purchases or
    // max-health/equipment/stat mutations occur. No safety flag. Every modeled
    // actor retains its actual sampled Strength/Focus/maxHP (including Ryth).
    out.assumptions=0x454e435354415432ull;out.conditional=true;
    e::Scenario scenario{1,s.world.revision,out.assumptions,s.horizon,root_slot,starter,{s.slots.data(),s.slot_count},true,true,true};
    e::Callbacks callbacks{&s,&geometry,&visibility,&policy,&phase,&spawn};
    e::Requirement requirement{input.owner,s.horizon,0,true,true,true,&ready,&s};
    e::Word economic;economic.complete=true;append(economic,{e::PrimitiveKind::Move,-1,-1,input.economic_step});
    e::Word recovery;recovery.complete=true;append(recovery,{e::PrimitiveKind::Move,-1,-1,input.recovery_step});
    ++counters().offers;
    out.economic=e::run(s.world,economic,scenario,callbacks,requirement);
    std::fill(s.empty_awake_phases.begin(),s.empty_awake_phases.end(),0);
    if(cambc::fuel_remaining()<execute_tail+6000)return reject(out,Status::NeedFuel,Rejection::Funding);
    out.restoration=e::run(s.world,recovery,scenario,callbacks,requirement);++counters().priced;
    out.economic_refuted_before_usable=out.economic.refutation_proved&&out.economic.refutation==e::Refutation::OwnerDied&&out.economic.usable_inputs==0;
    out.restored=out.restoration.status==e::Status::Complete&&out.restoration.service==e::Service::Ready&&out.restoration.horizon_reached;
    if(out.economic_refuted_before_usable)++counters().refuted;
    if(!current(input))return reject(out,Status::Stale,Rejection::Resource);
    if(!out.economic_refuted_before_usable||!out.restored)return reject(out,Status::NoRefutation,Rejection::Forecast);
    if(cambc::fuel_remaining()<execute_tail)return reject(out,Status::NeedFuel,Rejection::Funding);
    out.status=Status::Ready;out.word=recovery;out.recovery=input.recovery_step;++counters().selected;return out;
}
inline void log_numbers(const char* name,std::span<const std::uint64_t> values) {
    char line[512];unsigned at=0;while(*name&&at+1<sizeof line)line[at++]=*name++;
    for(const auto value:values) {
        if(at+24>=sizeof line)return;line[at++]=' ';char digits[24];unsigned n=0;auto rest=value;
        do{digits[n++]=static_cast<char>('0'+rest%10);rest/=10;}while(rest);
        while(n)line[at++]=digits[--n];
    }
    line[at++]='\n';std::cout.write(line,at);
}
inline void log_selection(const Result& r) {
    if(cambc::fuel_remaining()<20000)return;
    const std::array<std::uint64_t,8> values{static_cast<unsigned>(r.owner),static_cast<unsigned>(r.round),r.input,r.effect,
        static_cast<unsigned>(r.economic.refutation_round),static_cast<unsigned>(r.restoration.service_ready_round),
        r.economic.usable_inputs,static_cast<unsigned>(r.restoration.terminal.round)};
    log_numbers("wb_encounter_select",values);
}
inline void log_counters(int owner,int round) {
    if constexpr(!config::service_forecast_controller) {
    // Exact played R2 OFF path; its unreachable second packet stays historical.
    if(round%128||cambc::fuel_remaining()<75000)return;
    const auto& c=counters();std::array<std::uint64_t,27> values{};unsigned at=0;
    for(const auto x:{std::uint64_t(owner),std::uint64_t(round),c.nominations,c.inputs,c.offers,c.priced,c.refuted,c.selected,c.accepted,c.refused,c.restored})values[at++]=x;
    for(const auto x:c.rejected)values[at++]=x;
    log_numbers("wb_encounter_v1",{values.data(),at});
    if(cambc::fuel_remaining()<75000)return;
    std::array<std::uint64_t,27> coverage{};at=0;
    coverage[at++]=static_cast<unsigned>(owner);coverage[at++]=static_cast<unsigned>(round);
    coverage[at++]=c.sampled_bonus_health_strength;
    for(const auto x:c.coverage_faults)coverage[at++]=x;
    for(const auto x:c.rejected_actor)coverage[at++]=x;
    log_numbers("wb_encounter_cov_v1",{coverage.data(),at});
    } else {
    if(round%128||cambc::fuel_remaining()<75000)return;
    // One bounded packet owns its complete serialization/write tail. A second
    // packet behind a second entry reserve was not reachable in R2 samples.
    // This never lowers a physical-command or actual-readback obligation.
    const auto& c=counters();std::array<std::uint64_t,51> values{};unsigned at=0;
    for(const auto x:{std::uint64_t(owner),std::uint64_t(round),c.nominations,c.inputs,c.offers,c.priced,c.refuted,c.selected,c.accepted,c.refused,c.restored})values[at++]=x;
    for(const auto x:c.rejected)values[at++]=x;
    for(const auto x:c.coverage_faults)values[at++]=x;
    for(const auto x:c.rejected_actor)values[at++]=x;
    values[at++]=c.sampled_bonus_health_strength;
    log_numbers("wb_encounter_v2",{values.data(),at});
    }
}
#endif
}
