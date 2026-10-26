#ifndef TT_OBSERVATION_PURE_ONLY
#include "runtime.h"
#include "config.h"
#include "assignment_reachability_probe.h"
#else
#include "service_model.h"
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace tt {
namespace observation_detail {

// SDK linear buffers are packed row-major, LSB first. Dimensions and exact
// byte length are part of this decoder's contract; padding never names a tile.
bool valid_packed_shape(int width,int height,std::size_t bytes)
{
    if (width<=0 || height<=0 || static_cast<std::size_t>(width)>
            std::numeric_limits<std::size_t>::max()/static_cast<std::size_t>(height)) return false;
    const auto tiles=static_cast<std::size_t>(width)*static_cast<std::size_t>(height);
    return bytes==tiles/8+(tiles%8!=0);
}

// The production startup and fixture share this exact packed-position decoder.
// One byte is one work unit and emits at most eight immutable positions. Output
// capacity is reserved by the caller before a bounded decoding input.
template<class Point>
bool advance_packed_positions(int width,int height,std::span<const std::uint8_t> bytes,
    std::size_t& cursor,std::size_t units,std::vector<Point>& output)
{
    if(!valid_packed_shape(width,height,bytes.size()) || cursor>bytes.size())
        throw std::runtime_error("error invalid bootstrap packed positions");
    const auto cells=static_cast<std::size_t>(width)*static_cast<std::size_t>(height);
    for(std::size_t work=0;work<units && cursor<bytes.size();++work,++cursor)
    {
        const auto value=bytes[cursor];
        for(unsigned bit=0;bit<8;++bit)
        {
            const auto tile=cursor*8+bit;
            if(tile<cells && (value&(std::uint8_t{1}<<bit)))
                output.push_back(Point{static_cast<int>(tile%static_cast<std::size_t>(width)),
                    static_cast<int>(tile/static_cast<std::size_t>(width))});
        }
    }
    return cursor==bytes.size();
}

// Batching immutable read/decoder chunks does not require an intervening End.
// Guards are deliberately separate: SDK headroom sampling can itself suspend,
// and a completed final chunk need not retain the high threshold for ANOTHER
// chunk. No result here is live movement/ability authority.
enum class BootstrapBatchStatus { NeedCompute,Ready,Stale };
struct BootstrapBatchResult
{
    BootstrapBatchStatus status{BootstrapBatchStatus::NeedCompute};
    std::size_t chunks{};
};
template<class Chunk,class FreshEpoch,class MayStartChunk>
BootstrapBatchResult advance_bootstrap_batch(std::size_t maximum_chunks,Chunk chunk,
    FreshEpoch fresh_epoch,MayStartChunk may_start_chunk)
{
    BootstrapBatchResult result;
    while(result.chunks<maximum_chunks)
    {
        if(!fresh_epoch()) {result.status=BootstrapBatchStatus::Stale;return result;}
        if(!may_start_chunk())return result;
        // Headroom is an SDK query in production. Fence its return before
        // beginning another indivisible retained chunk.
        if(!fresh_epoch()) {result.status=BootstrapBatchStatus::Stale;return result;}
        const bool ready=chunk();
        ++result.chunks;
        if(!fresh_epoch()) {result.status=BootstrapBatchStatus::Stale;return result;}
        if(ready) {result.status=BootstrapBatchStatus::Ready;return result;}
    }
    return result;
}

enum class BootstrapStatus { NeedCompute,Ready,Invalid };
// Actual camp-layout traversal, shared by the SDK backend and fixture. Native
// count/list imports are indivisible bounded-ABI obligations; C++ buffer filling,
// group decoding and member publication have retained explicit work cursors.
class BootstrapCursor
{
public:
    BootstrapStatus status() const {return status_;}
    std::size_t last_work() const {return last_work_;}
    std::size_t group() const {return group_;}
    std::size_t member() const {return member_;}
    template<class Backend,class Guard>
    BootstrapStatus advance(Backend& backend,std::size_t cells,std::size_t units,Guard guard)
    {
        last_work_=0;
        while(status_==BootstrapStatus::NeedCompute && last_work_<units && guard())
        {
            ++last_work_;
            switch(phase_)
            {
            case Phase::Count:
            {
                const auto count=backend.group_count();
                if(count>cells) {status_=BootstrapStatus::Invalid;break;}
                groups_count_=count;group_ids_.reserve(count);backend.reserve_groups(count);
                phase_=Phase::GroupBuffer;return status_;
            }
            case Phase::GroupBuffer:
                if(group_ids_.size()<groups_count_)group_ids_.push_back(0);
                else phase_=Phase::GroupIds;
                break;
            case Phase::GroupIds:
                if(last_work_>1) {--last_work_;return status_;}
                if(groups_count_ && !backend.group_ids(std::span<std::uint32_t>{group_ids_}))
                {status_=BootstrapStatus::Invalid;break;}
                phase_=Phase::Header;return status_;
            case Phase::Header:
                if(group_==groups_count_) {status_=BootstrapStatus::Ready;break;}
                members_count_=backend.begin_group(group_ids_[group_],group_);
                if(members_count_>cells || total_members_>cells-members_count_)
                {status_=BootstrapStatus::Invalid;break;}
                total_members_+=members_count_;member_ids_.clear();member_ids_.reserve(members_count_);
                backend.reserve_members(members_count_);
                member_=0;phase_=Phase::MemberBuffer;break;
            case Phase::MemberBuffer:
                if(member_ids_.size()<members_count_)member_ids_.push_back(0);
                else phase_=Phase::MemberIds;
                break;
            case Phase::MemberIds:
                if(last_work_>1) {--last_work_;return status_;}
                if(members_count_ && !backend.member_ids(group_ids_[group_],std::span<std::uint32_t>{member_ids_}))
                {status_=BootstrapStatus::Invalid;break;}
                phase_=Phase::Member;return status_;
            case Phase::Member:
                if(member_<members_count_)
                {backend.append_member(group_ids_[group_],member_ids_[member_],group_,member_);++member_;}
                else phase_=Phase::Publish;
                break;
            case Phase::Publish:
                backend.publish_group(group_ids_[group_],group_);++group_;phase_=Phase::Header;break;
            }
        }
        return status_;
    }
private:
    enum class Phase {Count,GroupBuffer,GroupIds,Header,MemberBuffer,MemberIds,Member,Publish};
    Phase phase_{Phase::Count};
    BootstrapStatus status_{BootstrapStatus::NeedCompute};
    std::vector<std::uint32_t> group_ids_,member_ids_;
    std::size_t group_{},member_{},groups_count_{},members_count_{},total_members_{},last_work_{};
};

bool packed_tile(int width,int height,std::span<const std::uint8_t> bytes,int x,int y)
{
    if (x<0 || y<0 || x>=width || y>=height)
        throw std::out_of_range("error packed mask tile outside map");
    const auto tile=static_cast<std::size_t>(y)*static_cast<std::size_t>(width)+static_cast<std::size_t>(x);
    return (bytes[tile/8] & (std::uint8_t{1}<<(tile%8)))!=0;
}

class PackedView
{
public:
    PackedView(int width,int height,std::span<const std::uint8_t> bytes)
        : width_(width),height_(height),bytes_(bytes)
    {
        if (!valid_packed_shape(width,height,bytes.size()))
            throw std::runtime_error("error invalid packed mask view");
    }
    bool test(int x,int y) const { return packed_tile(width_,height_,bytes_,x,y); }
    const std::uint8_t* data() const { return bytes_.data(); }
private:
    int width_,height_;
    std::span<const std::uint8_t> bytes_;
};

class PackedMask
{
public:
    PackedMask(int width,int height,std::vector<std::uint8_t> bytes)
        : width_(width),height_(height),bytes_(std::move(bytes))
    {
        if (!valid_packed_shape(width,height,bytes_.size()))
            throw std::runtime_error("error invalid packed mask shape");
    }

    bool test(int x,int y) const
    {
        return packed_tile(width_,height_,bytes_,x,y);
    }
    PackedView view() const { return {width_,height_,bytes_}; }

    // Publish exactly this validated export after all local reads are finished.
    // The SDK masks are retained separately by the observation packet.
    std::vector<std::uint8_t> take_bytes() && { return std::move(bytes_); }

private:
    int width_,height_;
    std::vector<std::uint8_t> bytes_;
};

enum class PacketPath { Borrow, Refresh, Reject };

// A Ready stamp is a caller contract: no own action since packet publication.
// The operator still verifies the actual origin and epoch. NeedObservation can
// refresh, but can never borrow even when all old bytes remain allocated.
template<class Point,class Borrow,class Refresh>
PacketPath use_packet(bool ready,bool refresh_allowed,bool complete,int packet_epoch,int actual_epoch,
                      Point packet_origin,Point actual_origin,Borrow borrow,Refresh refresh)
{
    if (packet_epoch!=actual_epoch || packet_origin.x!=actual_origin.x || packet_origin.y!=actual_origin.y)
        return PacketPath::Reject;
    if (ready && complete) { borrow(); return PacketPath::Borrow; }
    if (refresh_allowed) { refresh(); return PacketPath::Refresh; }
    return PacketPath::Reject;
}

// Ally presence has no handle authority or metadata dependency. The same bounded
// publication leaf resolves an enemy handle exactly once for each emitted tile.
template<bool Resolve,class Tiles,class Point,class Guard,class Publish,class Resolver>
bool publish_heroes(const Tiles& tiles,Point self,Guard guard,Publish publish,Resolver resolve)
{
    for (const auto tile:tiles)
    {
        if (tile.x==self.x && tile.y==self.y) continue;
        if (!guard()) return false;
        if constexpr (Resolve) publish(tile,resolve(tile));
        else publish(tile);
    }
    return true;
}

template<class Packet>
void declare_service_scope(Packet& packet)
{
    packet.camp_presence_complete=false;
    packet.ally_presence_complete=false;
}

template<class Packet,class Body,class Point>
bool publish_service_ready(Packet& packet,const Body& body,int epoch,int now,Point origin,Point actual,
                           PackedMask& visible,PackedMask& living,PackedMask& awake)
{
    if (epoch!=now || origin.x!=actual.x || origin.y!=actual.y ||
        body.pos.x!=origin.x || body.pos.y!=origin.y) return false;
    packet.visible_bits=std::move(visible).take_bytes();
    packet.living_bits=std::move(living).take_bytes();
    packet.awake_bits=std::move(awake).take_bytes();
    packet.service_body=body;
    packet.body_complete=true;
    return true;
}

// Owned Work masks have the same caller-enforced no-intervening-command lifetime
// as its fresh body. They do not repair or promote the preceding global packet.
template<class Packet,class Point>
bool publish_work_masks(Packet& packet,bool ready,int epoch,int now,Point origin,Point actual,
                        std::optional<PackedMask>& visible,std::optional<PackedMask>& living,
                        std::optional<PackedMask>& awake)
{
    if (!ready || epoch!=now || origin.x!=actual.x || origin.y!=actual.y ||
        !visible || !living || !awake) return false;
    packet.visible_bits=std::move(*visible).take_bytes();
    packet.living_bits=std::move(*living).take_bytes();
    packet.awake_bits=std::move(*awake).take_bytes();
    packet.masks_complete=true;
    return true;
}

float outgoing_strength(float raw,float hp,float maximum,bool missing_health_passive,float maximum_bonus)
{
    if (!missing_health_passive || maximum<=0) return raw;
    const float missing=std::clamp(1.0f-hp/maximum,0.0f,1.0f);
    return raw*(1.0f+maximum_bonus*missing);
}

// Both runtime observation paths and the provenance fixture enter this function.
// Unseen design HP is an upper bound, never an exact residual or a generation claim.
template<class Spec,class Point,class Health,class Stun>
mb::service::Member member_reading(const Spec& spec,bool visible,bool living,bool awake,
                                    Point origin,Health health,Stun stun)
{
    mb::service::Member member;
    member.id=spec.member; member.group=spec.group;
    member.pos={spec.pos.x,spec.pos.y}; member.hp=spec.max_hp;
    member.strength=spec.strength; member.range=spec.range;
    if (!visible) return member;
    member.hp_exact=true;
    if (!living) { member.hp=0; return member; }
    member.hp=health();
    if (!std::isfinite(member.hp) || member.hp<0) throw std::runtime_error("error invalid observed camp health");
    member.awake=awake;
    if (member.strength>0 && mb::service::distance({origin.x,origin.y},member.pos)<=member.range) member.stun_remaining=stun();
    return member;
}

// The caller supplies one actual accepted mutation and an explicit unchanged
// spatial/inventory ticket. Pose equality alone cannot prove that no walk,
// ward, shop or other vision-changing operation intervened.
enum class StationaryRefresh { Reject, ResolveSelected, PreserveSelected };
StationaryRefresh stationary_refresh_kind(const mb::service::State& before,
    const mb::service::Primitive& accepted,bool spatial_or_inventory_changed)
{
    if (spatial_or_inventory_changed || before.body.moved_this_turn ||
        before.member_count>mb::service::max_members) return StationaryRefresh::Reject;
    if (accepted.kind==mb::service::PrimitiveKind::basic)
    {
        for (std::size_t i=0;i<before.member_count;++i)
            if (before.members[i].id==accepted.member_id && before.members[i].hp_exact &&
                before.members[i].hp>0) return StationaryRefresh::ResolveSelected;
        return StationaryRefresh::Reject;
    }
    if (accepted.kind!=mb::service::PrimitiveKind::cast || accepted.slot<0 ||
        static_cast<std::size_t>(accepted.slot)>=before.spells.size()) return StationaryRefresh::Reject;
    switch (before.spells[static_cast<std::size_t>(accepted.slot)].kind)
    {
    case mb::service::SpellKind::reset:
    case mb::service::SpellKind::shield: return StationaryRefresh::PreserveSelected;
    case mb::service::SpellKind::damage:
    case mb::service::SpellKind::cc:
    case mb::service::SpellKind::aoe: return StationaryRefresh::ResolveSelected;
    case mb::service::SpellKind::none: return StationaryRefresh::Reject;
    }
    return StationaryRefresh::Reject;
}

enum class SelectedLife { Unknown, Dead, Awake, Asleep };
// Only a prior exact observation within the SAME unchanged spatial input
// permits the state query. Unknown support performs no SDK probe/exception.
// DEAD is a valid state result and never enters the live health accessor.
template<class Spec,class Point,class Life,class Health,class Stun>
mb::service::Member refresh_selected_member(const Spec& spec,const mb::service::Member& before,
    Point origin,Life life,Health health,Stun stun)
{
    const bool supported=before.hp_exact && before.id==spec.member && before.group==spec.group &&
        before.pos.x==spec.pos.x && before.pos.y==spec.pos.y;
    if (!supported) return member_reading(spec,false,false,false,origin,health,stun);
    const auto current_life=life();
    if (current_life==SelectedLife::Unknown)
        return member_reading(spec,false,false,false,origin,health,stun);
    return member_reading(spec,true,current_life!=SelectedLife::Dead,
        current_life==SelectedLife::Awake,origin,health,stun);
}

#ifndef TT_OBSERVATION_PURE_ONLY

PackedMask packed_mask(const cambc::bitboard& mask,const World& world)
{
    if (mask.width()!=world.width || mask.height()!=world.height)
        throw std::runtime_error("error packed mask differs from world dimensions");
    return {world.width,world.height,mask.export_linear_buffer()};
}

struct SelfReading
{
    Status status{Status::NeedCompute};
    mb::service::Body body;
    float raw_strength{};
    std::uint32_t gold{},gems{};
    bool alive{},channeling{};
};

float incoming_multiplier(const cambc::champion& me)
{
    const auto owned=me.item_count();
    static std::vector<std::optional<cambc::item_info>> metadata(owned.size());
    float durability=0,jungle=0;
    std::size_t index=0;
    for (const auto count : owned)
    {
        const auto slot=index++;
        if (count==0) continue;
        if (!metadata[slot])
        {
            if (!budget(5000)) return -1;
            const auto kind=cambc::item_type{static_cast<cambc::item_type::enum_type>(slot)};
            metadata[slot]=kind.info();
        }
        const auto& item=*metadata[slot];
        if (item.passive==cambc::item_passive::durability) durability=std::max(durability,item.passive_magnitude);
        if (item.passive==cambc::item_passive::jungle) jungle=std::max(jungle,item.passive_magnitude);
    }
    // A family passive applies once, even when several stat variants are owned.
    return (1.0f-durability)*(1.0f-jungle);
}

SelfReading self_reading(int epoch,bool combat_buffs=true,
                         std::optional<float> retained_inventory_multiplier=std::nullopt)
{
    SelfReading result;
    if (!budget(12000)) return result;
    auto& me=cambc::self();
    result.alive=me.is_alive();
    if (!result.alive)
    {
        result.body.pos={-1,-1};
        result.body.can_move=result.body.can_attack=result.body.can_cast=false;
        result.status=current(epoch)?Status::Ready:Status::Stale;
        return result;
    }
    const auto point=me.position();
    auto& body=result.body;
    body.pos={point.x,point.y};
    body.hp=me.health(); body.max_hp=me.max_health();
    const float raw_strength=me.strength();
    result.raw_strength=raw_strength;
    body.focus=me.focus(); body.attack_range=me.attack_range();
    const auto passive=me.passive();
    // Diagnostic identity uses this EXISTING own passive read. The fixed
    // Varg/Marcella/Ryth/Vesper/Heart roster has exactly one stun-immunity kit.
    if constexpr(cfg::assignment_reachability_audit)
        if(passive==cambc::passive_type::stun_immunity)assignment_reachability_probe::select(1);
    body.strength=outgoing_strength(raw_strength,body.hp,body.max_hp,
        passive==cambc::passive_type::missing_health_damage,
        cambc::constants::passive_abilities::heart_of_ruin_missing_health_damage_max_bonus);
    body.ranged=body.attack_range>1;
    body.splash=passive==cambc::passive_type::splash_attack;
    const auto state=me.state();
    body.can_move=state.contains(cambc::champion_state::can_move);
    body.can_attack=state.contains(cambc::champion_state::can_attack);
    body.can_cast=state.contains(cambc::champion_state::can_cast);
    result.channeling=state.contains(cambc::champion_state::recalling) ||
        state.contains(cambc::champion_state::submitting_gems) || state.contains(cambc::champion_state::teleporting);
    if (combat_buffs)
    {
        const auto buffs=me.buffs();
        body.shield=buffs.has_shield()?buffs.get<cambc::buff_shield>().value:0.0f;
    }
    // Enhanced buff lifetime is not a stun-duration premise. Actual target stuns
    // are queried after commands; the pure projection deliberately credits none.
    body.next_basic_stun=0;
    result.gold=me.gold_count(); result.gems=me.gem_count();
    // The optional value is supplied only by the post-stationary same-input
    // operator, whose ticket excludes every inventory mutation. HP, shield,
    // raw/outgoing stats and actual allowances above are still fresh imports.
    body.camp_damage_multiplier=retained_inventory_multiplier?
        *retained_inventory_multiplier:incoming_multiplier(me);
    if (!std::isfinite(body.camp_damage_multiplier) || body.camp_damage_multiplier<0) return result;
    result.status=current(epoch)?Status::Ready:Status::Stale;
    return result;
}

int current_stun(const cambc::camp& camp)
{
    const auto buffs=camp.buffs();
    return buffs.has_stun()?buffs.get<cambc::buff_stun>().duration:0;
}

Status observe_spells(mb::service::State& state,int epoch,bool moved)
{
    if (!budget(6000)) return Status::NeedCompute;
    const auto abilities=cambc::self().abilities();
    for (std::size_t slot=0;slot<state.spells.size();++slot)
    {
        if (!budget(3000)) return Status::NeedCompute;
        const auto& ability=abilities[slot];
        const auto& info=ability.info();
        auto& spell=state.spells[slot];
        spell={};
        if (!info) continue;
        const auto cooldown=ability.cooldown();
        std::visit([&](const auto& typed) {
            using T=std::decay_t<decltype(typed)>;
            spell.exclusive=typed.exclusive_with_movement;
            if constexpr (std::is_same_v<T,cambc::ability_attack_reset>) spell.kind=mb::service::SpellKind::reset;
            else if constexpr (std::is_same_v<T,cambc::ability_shield>)
            {
                if (typed.valid_targets.contains(cambc::ability_target_mask::self))
                {
                    spell.kind=mb::service::SpellKind::shield;
                    spell.range=static_cast<int>(typed.range); spell.duration=typed.duration;
                    spell.damage=ability.shield_amount();
                }
            }
            else if constexpr (std::is_same_v<T,cambc::ability_damage> ||
                               std::is_same_v<T,cambc::ability_aoe_damage> || std::is_same_v<T,cambc::ability_cc>)
            {
                if (!typed.valid_targets.contains(cambc::ability_target_mask::camps)) return;
                spell.range=static_cast<int>(typed.range);
                // This runtime accessor already includes caster outgoing bonuses.
                spell.damage=ability.damage_amount();
                if constexpr (std::is_same_v<T,cambc::ability_damage>) spell.kind=mb::service::SpellKind::damage;
                else if constexpr (std::is_same_v<T,cambc::ability_cc>)
                { spell.kind=mb::service::SpellKind::cc; spell.duration=typed.duration; }
                else
                { spell.kind=mb::service::SpellKind::aoe; spell.radius=static_cast<int>(typed.radius); spell.duration=typed.duration; }
            }
        },*info);
        spell.ready=spell.kind!=mb::service::SpellKind::none && cooldown==0 && state.body.can_cast &&
            !(spell.exclusive && moved);
    }
    return current(epoch)?Status::Ready:Status::Stale;
}

#endif
} // namespace observation_detail

#ifndef TT_OBSERVATION_PURE_ONLY

namespace {
struct WorldBootstrap
{
    unsigned static_phase{};
    std::size_t packed_cursor{};
    std::vector<std::uint8_t> spawn_bits,altar_bits;
    std::optional<cambc::bitboard> altar_mask;
    observation_detail::BootstrapCursor layout;
    std::unordered_map<int,cambc::camp_stats> stats;
    GroupSpec pending;
    std::vector<cambc::camp> pending_camps;
    std::int64_t center_x{},center_y{};
    bool ready{};
};
WorldBootstrap& world_bootstrap() {static WorldBootstrap state;return state;}
World& partial_world() {static World value;return value;}

struct BootstrapNativeBackend
{
    World& world;
    WorldBootstrap& state;
    std::uint32_t group_count()
    {
        std::uint32_t count{};
        cambc::detail::check(cambc::detail::sdk_map_num_camp_groups(&count));return count;
    }
    void reserve_groups(std::size_t count) {world.handles.reserve(count);world.groups.reserve(count);}
    bool group_ids(std::span<std::uint32_t> ids)
    {
        std::uint32_t count{};
        cambc::detail::check(cambc::detail::sdk_map_camp_groups(ids.data(),static_cast<std::uint32_t>(ids.size()),&count));
        return count==ids.size();
    }
    std::uint32_t begin_group(std::uint32_t id,std::size_t)
    {
        cambc::detail::camp_group_info info{};
        std::int32_t respawn{};std::uint32_t count{};
        cambc::detail::check(cambc::detail::sdk_map_camp_group(id,&info));
        cambc::detail::check(cambc::detail::sdk_bc_camp_group_max_respawn(id,&respawn));
        cambc::detail::check(cambc::detail::sdk_map_camp_group_num_camps(id,&count));
        state.pending=GroupSpec{};
        // Raw camp_group_info is gems,gold; access named fields rather than
        // treating its ABI layout as the make_camp_group constructor order.
        state.pending.gold=info.gold;state.pending.gems=info.gems;state.pending.respawn=respawn;
        state.pending.min_x=world.width;state.pending.min_y=world.height;
        state.pending.max_x=state.pending.max_y=-1;state.center_x=state.center_y=0;
        state.pending_camps.clear();
        return count;
    }
    void reserve_members(std::size_t count) {state.pending.members.reserve(count);state.pending_camps.reserve(count);}
    bool member_ids(std::uint32_t group,std::span<std::uint32_t> ids)
    {
        std::uint32_t count{};
        cambc::detail::check(cambc::detail::sdk_map_camp_group_camps(group,ids.data(),static_cast<std::uint32_t>(ids.size()),&count));
        return count==ids.size();
    }
    void append_member(std::uint32_t group,std::uint32_t id,std::size_t index,std::size_t member)
    {
        cambc::detail::camp_info info{};
        cambc::detail::check(cambc::detail::sdk_map_camp_info(id,&info));
        const vec2 point{info.x,info.y};
        if(point.x<0 || point.y<0 || point.x>=world.width || point.y>=world.height)
            throw std::runtime_error("error bootstrap camp spawn outside map");
        const auto camp=cambc::detail::handle_access::make_camp(static_cast<cambc::handle_camp_t>(id),
            static_cast<cambc::handle_camp_group_t>(group),static_cast<cambc::camp_type>(info.type),point);
        auto found=state.stats.find(info.type);
        if(found==state.stats.end())found=state.stats.emplace(info.type,camp.base_stats()).first;
        const auto& design=found->second;
        // Complete all throwing native work before either paired vector append.
        // Both vectors were reserved, and these immutable value pushes allocate0.
        world.camp_tiles.set(point);
        state.pending_camps.push_back(camp);
        state.pending.members.push_back({point,design.base_health,design.base_strength,design.base_attack_range,
            static_cast<int>(index),static_cast<int>(member)});
        state.center_x+=point.x;state.center_y+=point.y;
        state.pending.min_x=std::min(state.pending.min_x,point.x);state.pending.max_x=std::max(state.pending.max_x,point.x);
        state.pending.min_y=std::min(state.pending.min_y,point.y);state.pending.max_y=std::max(state.pending.max_y,point.y);
    }
    void publish_group(std::uint32_t id,std::size_t)
    {
        if(!state.pending.members.empty())
        {
            state.pending.center.x=static_cast<int>(state.center_x/static_cast<std::int64_t>(state.pending.members.size()));
            state.pending.center.y=static_cast<int>(state.center_y/static_cast<std::int64_t>(state.pending.members.size()));
        }
        world.handles.push_back(cambc::detail::handle_access::make_camp_group(static_cast<cambc::handle_camp_group_t>(id),
            state.pending.gold,state.pending.gems,state.pending.respawn,std::move(state.pending_camps)));
        world.groups.push_back(std::move(state.pending));
    }
};
}

bool World::bootstrap_ready()
{
    if constexpr(!cfg::bounded_bootstrap)return true;
    return world_bootstrap().ready;
}
bool World::advance_bootstrap()
{
    if constexpr(!cfg::bounded_bootstrap)return true;
    // With batching disabled this is exactly one legacy retained chunk: no
    // extra epoch/headroom SDK calls are added to that compiled branch.
    const auto advance_chunk=[]() -> bool
    {
        auto& state=world_bootstrap();
        if(state.ready)return true;
        if(!budget(40000))return false;
        const int epoch=static_cast<int>(cambc::round_number());
        auto& value=partial_world();
        if(!current(epoch))return false;
        // Each whole-board/list import finishes a retained chunk. The eager
        // driver may resume another chunk in this same input with fresh headroom.
        // Packed exports use an explicit byte cursor instead of an all-map loop.
        switch(state.static_phase)
        {
        case 0:
            value.width=cambc::map::width();value.height=cambc::map::height();
            if(value.width<=0 || value.height<=0 || static_cast<std::size_t>(value.width)>
                std::numeric_limits<std::uint32_t>::max()/static_cast<std::size_t>(value.height))
                throw std::runtime_error("error invalid bootstrap dimensions");
            ++state.static_phase;return false;
        case 1:value.own_spawn=cambc::map::spawn_region(cambc::team::ally);++state.static_phase;return false;
        case 2:
            state.spawn_bits=value.own_spawn.export_linear_buffer();
            value.spawn_tiles.reserve(static_cast<std::size_t>(value.width)*value.height);
            state.packed_cursor=0;++state.static_phase;return false;
        case 3:
            if(observation_detail::advance_packed_positions(value.width,value.height,state.spawn_bits,
                state.packed_cursor,256,value.spawn_tiles))++state.static_phase;
            return false;
        case 4:value.terrain=cambc::map::walls();++state.static_phase;return false;
        case 5:value.terrain|=cambc::map::holes();++state.static_phase;return false;
        case 6:state.altar_mask.emplace(cambc::map::altars());++state.static_phase;return false;
        case 7:value.terrain|=*state.altar_mask;++state.static_phase;return false;
        case 8:value.terrain|=cambc::map::spawn_region(cambc::team::enemy);++state.static_phase;return false;
        case 9:
            state.altar_bits=state.altar_mask->export_linear_buffer();
            value.altars.reserve(static_cast<std::size_t>(value.width)*value.height);
            state.packed_cursor=0;++state.static_phase;return false;
        case 10:
            if(observation_detail::advance_packed_positions(value.width,value.height,state.altar_bits,
                state.packed_cursor,256,value.altars))++state.static_phase;
            return false;
        default:break;
        }
        BootstrapNativeBackend backend{value,state};
        const auto result=state.layout.advance(backend,static_cast<std::size_t>(value.width)*value.height,64,
            [&]{return budget(30000) && current(epoch);});
        if(result==observation_detail::BootstrapStatus::Invalid)
            throw std::runtime_error("error invalid bootstrap immutable camp layout");
        if(!current(epoch))return false;
        state.ready=result==observation_detail::BootstrapStatus::Ready;
        return state.ready;
    };
    if constexpr(cfg::eager_service)
    {
        // Immutable completion needs no new import and no rebatching headroom.
        if(world_bootstrap().ready)return true;
        const int epoch=static_cast<int>(cambc::round_number());
        const auto result=observation_detail::advance_bootstrap_batch(64,advance_chunk,
            [&]{return current(epoch);},[]{return budget(56000);});
        return result.status==observation_detail::BootstrapBatchStatus::Ready;
    }
    else return advance_chunk();
}

World& World::get()
{
    if constexpr(cfg::bounded_bootstrap)return partial_world();
    else
    {
    static World world=[] {
        World value;
        value.width=cambc::map::width(); value.height=cambc::map::height();
        value.own_spawn=cambc::map::spawn_region(cambc::team::ally);
        value.spawn_tiles=value.own_spawn.positions();
        value.terrain=cambc::map::walls() | cambc::map::holes() | cambc::map::altars() |
            cambc::map::spawn_region(cambc::team::enemy);
        value.altars=cambc::map::altars().positions();
        value.handles=cambc::map::camp_groups();
        value.groups.reserve(value.handles.size());
        std::unordered_map<int,cambc::camp_stats> stats;
        for (std::size_t index=0;index<value.handles.size();++index)
        {
            const auto& handle=value.handles[index];
            GroupSpec group;
            group.gold=handle.gold_value(); group.gems=handle.gem_value(); group.respawn=handle.max_respawn_timer();
            group.min_x=value.width; group.min_y=value.height; group.max_x=group.max_y=-1;
            int member_index=0;
            for (const auto& camp:handle.camps())
            {
                const int type=static_cast<int>(camp.kind());
                auto found=stats.find(type);
                if (found==stats.end()) found=stats.emplace(type,camp.base_stats()).first;
                const auto point=camp.spawn_position();
                const auto& design=found->second;
                group.members.push_back({point,design.base_health,design.base_strength,design.base_attack_range,
                                         static_cast<int>(index),member_index++});
                group.center.x+=point.x; group.center.y+=point.y;
                group.min_x=std::min(group.min_x,point.x); group.max_x=std::max(group.max_x,point.x);
                group.min_y=std::min(group.min_y,point.y); group.max_y=std::max(group.max_y,point.y);
                value.camp_tiles.set(point);
            }
            if (!group.members.empty())
            { group.center.x/=static_cast<int>(group.members.size()); group.center.y/=static_cast<int>(group.members.size()); }
            value.groups.push_back(std::move(group));
        }
        return value;
    }();
    return world;
    }
}

Observation observe()
{
    const int epoch=static_cast<int>(cambc::round_number()); // before object/mask construction
    Observation result;
    result.epoch=epoch;
    if (!budget(25000)) { result.status=Status::NeedCompute; return result; }
    auto& world=World::get();
    const auto self=observation_detail::self_reading(epoch,true);
    result.status=self.status;
    if (self.status!=Status::Ready) return result;
    const auto& body=self.body;
    result.pos={body.pos.x,body.pos.y}; result.hp=body.hp; result.max_hp=body.max_hp;
    result.attack_range=body.attack_range;
    result.raw_strength=self.raw_strength;
    result.strength=body.strength; result.focus=body.focus; result.damage_multiplier=body.camp_damage_multiplier;
    result.gold=self.gold; result.gems=self.gems; result.ranged=body.ranged; result.splash=body.splash;
    result.channeling=self.channeling; result.can_move=body.can_move; result.can_attack=body.can_attack; result.can_cast=body.can_cast;
    if (!self.alive) return result;
    result.on_spawn=world.own_spawn.is_set(result.pos);
    result.visible=cambc::vision::visible_tiles();
    result.living=cambc::vision::camp_tiles(); result.awake=cambc::vision::awake_camp_tiles();
    const auto allied_tiles=cambc::vision::champion_tiles(cambc::team::ally);
    const auto enemy_tiles=cambc::vision::champion_tiles(cambc::team::enemy);
    const auto hero_tiles=allied_tiles | enemy_tiles;
    result.occupied=result.living | hero_tiles;
    result.occupied.set(result.pos,false);
    auto visible=observation_detail::packed_mask(result.visible,world);
    auto living=observation_detail::packed_mask(result.living,world);
    auto awake=observation_detail::packed_mask(result.awake,world);
    for (const auto& group:world.groups)
        for (const auto& spec:group.members)
        {
            if (!budget(1000)) { result.status=Status::NeedCompute; return result; }
            if (!visible.test(spec.pos.x,spec.pos.y) || !living.test(spec.pos.x,spec.pos.y)) continue;
            // This packet publishes presence, not a service residual. Design HP
            // remains an upper bound and unqueried stun supplies no protection.
            result.camps.push_back({spec.group,spec.member,spec.pos,spec.max_hp,spec.strength,spec.range,
                                    0,awake.test(spec.pos.x,spec.pos.y),false,false});
        }
    const auto resolve=[](vec2 tile) { return cambc::vision::champion_at(tile); };
    if (!observation_detail::publish_heroes<false>(allied_tiles.positions(),result.pos,[]{return budget(2500);},
            [&](vec2 tile) { result.allies.push_back({tile}); },resolve) ||
        !observation_detail::publish_heroes<true>(enemy_tiles.positions(),result.pos,[]{return budget(2500);},
            [&](vec2 tile,cambc::champion handle) {
                SeenHero hero{std::move(handle),tile};
                hero.numbers_exact=false;
                result.enemies.push_back(std::move(hero));
            },resolve))
    {
        result.status=Status::NeedCompute;
        return result;
    }
    result.status=current(epoch)?Status::Ready:Status::Stale;
    if (result.status==Status::Ready)
    {
        result.visible_bits=std::move(visible).take_bytes();
        result.living_bits=std::move(living).take_bytes();
        result.awake_bits=std::move(awake).take_bytes();
        result.service_body=body;
        result.body_complete=true;
    }
    return result;
}

Observation observe_service()
{
    const int epoch=static_cast<int>(cambc::round_number());
    Observation result;
    result.epoch=epoch;
    observation_detail::declare_service_scope(result);
    if (!budget(18000)) { result.status=Status::NeedCompute; return result; }
    auto& world=World::get();
    const auto self=observation_detail::self_reading(epoch,true);
    result.status=self.status;
    if (self.status!=Status::Ready) return result;
    if (!self.alive) { result.status=Status::NeedObservation; return result; }
    const auto& body=self.body;
    result.pos={body.pos.x,body.pos.y}; result.hp=body.hp; result.max_hp=body.max_hp;
    result.attack_range=body.attack_range;
    result.raw_strength=self.raw_strength;
    result.strength=body.strength; result.focus=body.focus; result.damage_multiplier=body.camp_damage_multiplier;
    result.gold=self.gold; result.gems=self.gems; result.ranged=body.ranged; result.splash=body.splash;
    result.channeling=self.channeling; result.can_move=body.can_move; result.can_attack=body.can_attack; result.can_cast=body.can_cast;
    result.on_spawn=world.own_spawn.is_set(result.pos);
    result.visible=cambc::vision::visible_tiles();
    result.living=cambc::vision::camp_tiles(); result.awake=cambc::vision::awake_camp_tiles();
    auto visible=observation_detail::packed_mask(result.visible,world);
    auto living=observation_detail::packed_mask(result.living,world);
    auto awake=observation_detail::packed_mask(result.awake,world);
    const auto enemies=cambc::vision::champion_tiles(cambc::team::enemy);
    if (!observation_detail::publish_heroes<true>(enemies.positions(),result.pos,[]{return budget(2500);},
        [&](vec2 tile,cambc::champion handle) {
            SeenHero hero{std::move(handle),tile};
            hero.numbers_exact=false;
            result.enemies.push_back(std::move(hero));
        },[](vec2 tile) { return cambc::vision::champion_at(tile); }))
    { result.status=Status::NeedCompute; return result; }
    const auto actual=cambc::self().position();
    const int now=static_cast<int>(cambc::round_number());
    result.status=observation_detail::publish_service_ready(result,body,epoch,now,result.pos,actual,
        visible,living,awake)?Status::Ready:Status::Stale;
    return result;
}

GroupObservation observe_group(const Observation& previous,int group)
{
    const int epoch=static_cast<int>(cambc::round_number());
    GroupObservation result;
    result.epoch=epoch; result.group=group;
    // Ready permits same-epoch mask borrowing only under the caller's no-command
    // contract. A post-command NeedObservation always takes the fresh mask path.
    if ((previous.status!=Status::Ready && previous.status!=Status::NeedObservation) || previous.epoch!=epoch)
    { result.status=Status::Stale; return result; }
    if (!budget(6000)) { result.status=Status::NeedCompute; return result; }
    auto& world=World::get();
    if (group<0 || static_cast<std::size_t>(group)>=world.groups.size()) return result;
    auto& me=cambc::self();
    if (!me.is_alive()) { result.status=Status::Stale; return result; }
    const auto actual_origin=me.position();
    if (!current(epoch)) { result.status=Status::Stale; return result; }
    const auto& spec=world.groups[static_cast<std::size_t>(group)];
    if (spec.members.size()>mb::service::max_members) return result;
    std::optional<observation_detail::PackedMask> owned_visible,owned_living;
    std::optional<observation_detail::PackedView> visible,living;
    const bool complete=observation_detail::valid_packed_shape(world.width,world.height,previous.visible_bits.size()) &&
        observation_detail::valid_packed_shape(world.width,world.height,previous.living_bits.size());
    const auto path=observation_detail::use_packet(previous.status==Status::Ready,true,complete,previous.epoch,epoch,
        previous.pos,actual_origin,[&] {
            visible.emplace(world.width,world.height,previous.visible_bits);
            living.emplace(world.width,world.height,previous.living_bits);
        },[&] {
            owned_visible.emplace(observation_detail::packed_mask(cambc::vision::visible_tiles(),world));
            owned_living.emplace(observation_detail::packed_mask(cambc::vision::camp_tiles(),world));
            visible.emplace(owned_visible->view()); living.emplace(owned_living->view());
        });
    if (path==observation_detail::PacketPath::Reject) { result.status=Status::Stale; return result; }
    if (!current(epoch)) { result.status=Status::Stale; return result; }
    bool any_visible=false;
    for (const auto& member:spec.members)
    {
        if (!budget(1500)) { result.status=Status::NeedCompute; return result; }
        const auto index=result.member_count++;
        result.visible[index]=visible->test(member.pos.x,member.pos.y);
        // False living with false visibility is Unknown, never exact absence.
        result.living[index]=result.visible[index] && living->test(member.pos.x,member.pos.y);
        any_visible|=result.visible[index];
    }
    if (any_visible)
    {
        if (!budget(2500)) { result.status=Status::NeedCompute; return result; }
        const auto state=world.handles[static_cast<std::size_t>(group)].state();
        if (std::holds_alternative<cambc::map::camp_group_alive>(state)) result.availability=Availability::Alive;
        else if (std::holds_alternative<cambc::map::camp_group_reward_available>(state)) result.availability=Availability::Reward;
        else { result.availability=Availability::Respawning; result.respawn_timer=std::get<cambc::map::camp_group_respawning>(state).timer; }
    }
    const bool same_origin=me.position()==previous.pos;
    result.status=same_origin && current(epoch)?Status::Ready:Status::Stale;
    return result;
}

WorkObservation observe_work(const Observation& previous,int group,bool moved)
{
    const int epoch=static_cast<int>(cambc::round_number());
    WorkObservation result;
    result.epoch=epoch; result.group=group;
    // The caller invalidates Ready after every own command. Before that boundary
    // this operator may borrow the complete global body/masks; selected residuals,
    // lifecycle and spells are still queried now. NeedObservation refreshes all.
    if ((previous.status!=Status::Ready && previous.status!=Status::NeedObservation) || previous.epoch!=epoch)
    { result.status=Status::Stale; return result; }
    if (!budget(18000)) { result.status=Status::NeedCompute; return result; }
    auto& world=World::get();
    if (group<0 || static_cast<std::size_t>(group)>=world.groups.size()) return result;
    auto& me=cambc::self();
    if (!me.is_alive()) { result.status=Status::Stale; return result; }
    const auto actual_origin=me.position();
    if (!current(epoch)) { result.status=Status::Stale; return result; }
    std::optional<observation_detail::PackedMask> owned_visible,owned_living,owned_awake;
    std::optional<observation_detail::PackedView> visible,living,awake;
    const bool complete=previous.body_complete &&
        vec2{previous.service_body.pos.x,previous.service_body.pos.y}==previous.pos &&
        observation_detail::valid_packed_shape(world.width,world.height,previous.visible_bits.size()) &&
        observation_detail::valid_packed_shape(world.width,world.height,previous.living_bits.size()) &&
        observation_detail::valid_packed_shape(world.width,world.height,previous.awake_bits.size());
    const auto path=observation_detail::use_packet(previous.status==Status::Ready,true,complete,previous.epoch,epoch,
        previous.pos,actual_origin,[&] {
            result.state.body=previous.service_body;
            visible.emplace(world.width,world.height,previous.visible_bits);
            living.emplace(world.width,world.height,previous.living_bits);
            awake.emplace(world.width,world.height,previous.awake_bits);
            result.status=Status::Ready;
        },[&] {
            const auto self=observation_detail::self_reading(epoch,true);
            result.status=self.status;
            if (self.status!=Status::Ready) return;
            if (!self.alive || vec2{self.body.pos.x,self.body.pos.y}!=previous.pos)
            { result.status=Status::Stale; return; }
            result.state.body=self.body;
            owned_visible.emplace(observation_detail::packed_mask(cambc::vision::visible_tiles(),world));
            owned_living.emplace(observation_detail::packed_mask(cambc::vision::camp_tiles(),world));
            owned_awake.emplace(observation_detail::packed_mask(cambc::vision::awake_camp_tiles(),world));
            visible.emplace(owned_visible->view()); living.emplace(owned_living->view()); awake.emplace(owned_awake->view());
        });
    if (path==observation_detail::PacketPath::Reject) { result.status=Status::Stale; return result; }
    if (result.status!=Status::Ready) return result;
    if (!current(epoch)) { result.status=Status::Stale; return result; }
    result.state.body.moved_this_turn=moved;
    result.state.width=world.width; result.state.height=world.height;
    const auto& spec=world.groups[static_cast<std::size_t>(group)];
    if (spec.members.size()>mb::service::max_members) { result.status=Status::NeedObservation; return result; }
    bool any_visible=false;
    // This is target-member collection completeness only. External awake/wake
    // coverage is separately required by the controller before any command.
    result.state.incoming_complete=true;
    for (const auto& member:spec.members)
    {
        if (!budget(3500)) { result.status=Status::NeedCompute; return result; }
        const bool seen=visible->test(member.pos.x,member.pos.y);
        any_visible|=seen;
        result.state.incoming_complete&=seen;
        const auto& camp=world.camp(group,member.member);
        result.state.members[result.state.member_count++]=observation_detail::member_reading(member,seen,
            living->test(member.pos.x,member.pos.y),awake->test(member.pos.x,member.pos.y),previous.pos,
            [&]{return camp.health();},[&]{return observation_detail::current_stun(camp);});
    }
    if (any_visible)
    {
        const auto state=world.handles[static_cast<std::size_t>(group)].state();
        if (std::holds_alternative<cambc::map::camp_group_alive>(state)) result.availability=Availability::Alive;
        else if (std::holds_alternative<cambc::map::camp_group_reward_available>(state)) result.availability=Availability::Reward;
        else { result.availability=Availability::Respawning; result.respawn_timer=std::get<cambc::map::camp_group_respawning>(state).timer; }
    }
    result.status=observation_detail::observe_spells(result.state,epoch,moved);
    const auto final_origin=me.position();
    const int final_epoch=static_cast<int>(cambc::round_number());
    if (final_origin!=previous.pos || final_epoch!=epoch) result.status=Status::Stale;
    observation_detail::publish_work_masks(result,result.status==Status::Ready,epoch,final_epoch,
        previous.pos,final_origin,owned_visible,owned_living,owned_awake);
    return result;
}

// Dependency-specific continuation after one supported accepted stationary
// primitive. No global masks or enemy/camp presence packet is made fresh here.
// A failed ticket/new enemy is a withdrawal, not permission for blind fallback.
WorkObservation observe_after_stationary(const Observation& anchor,const WorkObservation& before,
    const mb::service::Primitive& accepted,bool spatial_or_inventory_changed)
{
    const int epoch=static_cast<int>(cambc::round_number());
    WorkObservation result;result.epoch=epoch;result.group=before.group;
    const auto kind=observation_detail::stationary_refresh_kind(
        before.state,accepted,spatial_or_inventory_changed);
    if (kind==observation_detail::StationaryRefresh::Reject ||
        before.status!=Status::Ready ||
        (anchor.status!=Status::Ready && anchor.status!=Status::NeedObservation)) return result;
    if (epoch!=anchor.epoch || epoch!=before.epoch)
    { result.status=Status::Stale;return result; }
    if (!budget(12000)) { result.status=Status::NeedCompute;return result; }
    auto& me=cambc::self();
    const auto origin=me.position();
    if (origin!=anchor.pos || before.state.body.pos.x!=origin.x || before.state.body.pos.y!=origin.y ||
        !current(epoch)) { result.status=Status::Stale;return result; }
    // This catches newly exposed enemies without constructing/exporting world
    // masks. The global/threat caller must own any resulting observation.
    if (!cambc::vision::champions(cambc::team::enemy).empty()) return result;
    if (me.position()!=origin || !current(epoch)) { result.status=Status::Stale;return result; }
    const auto self=observation_detail::self_reading(
        epoch,true,before.state.body.camp_damage_multiplier);
    result.status=self.status;
    if (self.status!=Status::Ready) return result;
    if (!self.alive || self.channeling || self.body.pos.x!=origin.x || self.body.pos.y!=origin.y)
    { result.status=Status::NeedObservation;return result; }
    auto& world=World::get();
    if (before.group<0 || static_cast<std::size_t>(before.group)>=world.groups.size())
    { result.status=Status::NeedObservation;return result; }
    const auto& group=world.groups[static_cast<std::size_t>(before.group)];
    if (group.members.size()>mb::service::max_members || before.state.member_count!=group.members.size())
    { result.status=Status::NeedObservation;return result; }
    result.state.body=self.body;
    // Actual walk history stays distinct from the freshly queried allowance.
    // A ranged basic can leave can_move=false without any ordinary walking.
    result.state.body.moved_this_turn=before.state.body.moved_this_turn;
    result.state.width=world.width;result.state.height=world.height;
    result.state.incoming_complete=true;
    bool any_visible=false;
    for (std::size_t i=0;i<group.members.size();++i)
    {
        const auto& spec=group.members[i];
        const auto& old=before.state.members[i];
        if (old.id!=spec.member || old.group!=before.group ||
            old.pos.x!=spec.pos.x || old.pos.y!=spec.pos.y)
        { result.status=Status::NeedObservation;return result; }
        mb::service::Member member;
        if (kind==observation_detail::StationaryRefresh::PreserveSelected)
        {
            // A self reset/shield mutates no selected camp. Reusing its exact
            // local state is conditional on the explicit same-input ticket.
            member=old;
        }
        else
        {
            // Normal unknown support never invokes an exception-bearing SDK
            // query. A violated exact-visibility premise withdraws after at most
            // one expected not-in-vision exception, rather than probing a suffix.
            if (old.hp_exact && !budget(10000))
            { result.status=Status::NeedCompute;return result; }
            const auto& camp=world.camp(before.group,spec.member);
            member=observation_detail::refresh_selected_member(spec,old,origin,[&] {
                try
                {
                    const auto life=camp.state();
                    if (life.is_dead()) return observation_detail::SelectedLife::Dead;
                    return life.is_awake()?observation_detail::SelectedLife::Awake:
                        observation_detail::SelectedLife::Asleep;
                }
                catch (const cambc::error& error)
                {
                    if (error.code()==cambc::error_code::api_not_in_vision)
                        return observation_detail::SelectedLife::Unknown;
                    throw;
                }
            },[&]{return camp.health();},[&]{return observation_detail::current_stun(camp);});
            if (old.hp_exact && !member.hp_exact)
            { result.status=Status::NeedObservation;return result; }
        }
        result.state.members[result.state.member_count++]=member;
        result.state.incoming_complete&=member.hp_exact;
        any_visible|=member.hp_exact;
        if (me.position()!=origin || !current(epoch))
        { result.status=Status::Stale;return result; }
    }
    if (any_visible)
    {
        if (!budget(3000)) { result.status=Status::NeedCompute;return result; }
        try
        {
            const auto state=world.handles[static_cast<std::size_t>(before.group)].state();
            if (std::holds_alternative<cambc::map::camp_group_alive>(state)) result.availability=Availability::Alive;
            else if (std::holds_alternative<cambc::map::camp_group_reward_available>(state)) result.availability=Availability::Reward;
            else { result.availability=Availability::Respawning;result.respawn_timer=std::get<cambc::map::camp_group_respawning>(state).timer; }
        }
        catch (const cambc::error& error)
        {
            if (error.code()!=cambc::error_code::api_not_in_vision) throw;
            result.status=Status::NeedObservation;return result;
        }
    }
    result.status=observation_detail::observe_spells(result.state,epoch,false);
    if (me.position()!=origin || !current(epoch)) result.status=Status::Stale;
    // masks_complete and all exported mask vectors deliberately remain empty.
    return result;
}

#endif
} // namespace tt
