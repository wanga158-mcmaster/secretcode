// Fresh, action-specific camp exposure coverage. No legacy runtime is used.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>
#include "service_model.h"

namespace tt::safety_detail {
struct Point { int x{},y{}; };
inline int chebyshev(Point a,Point b)
{ return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y)); }
struct Candidate { int group{}; Point pos; bool alive{}; };
struct Footprint { bool hit{}; Point center; int radius{}; };
struct Source
{
    int group{},member{};
    Point pos;
    float strength{};
    int range{},stun{};
    bool alive{},awake{},known_active{};
};

// A projected or unqueried stun supplies no denied-volley credit.
inline int observed_stun(int duration,bool exact) { return exact ? duration : 0; }

inline bool packed_shape(int width,int height,std::size_t bytes)
{
    if (width<=0 || height<=0) return false;
    const auto w=static_cast<std::size_t>(width),h=static_cast<std::size_t>(height);
    if (w>std::numeric_limits<std::size_t>::max()/h) return false;
    const auto cells=w*h;
    return bytes==cells/8+(cells%8!=0);
}

template<class PointType>
bool can_borrow_packet(bool ready,bool complete,int epoch,int now,PointType origin,PointType actual,
                       int width,int height,std::size_t visible,std::size_t living,std::size_t awake)
{
    return ready && complete && epoch==now && origin==actual &&
        packed_shape(width,height,visible) && packed_shape(width,height,living) &&
        packed_shape(width,height,awake);
}

class MaskView
{
    int width_,height_;
    std::span<const std::uint8_t> bytes_;
public:
    MaskView(int width,int height,std::span<const std::uint8_t> bytes)
        :width_{width},height_{height},bytes_{bytes}
    {
        if (!packed_shape(width,height,bytes.size()))
            throw std::runtime_error("error invalid safety packed mask shape");
    }
    bool test(int x,int y) const
    {
        if (x<0 || y<0 || x>=width_ || y>=height_)
            throw std::out_of_range("error safety packed mask tile outside map");
        const auto index=static_cast<std::size_t>(y)*width_+x;
        return (bytes_[index/8]>>(index%8))&1u;
    }
};
struct CampPresence { bool visible{},living{},awake{}; };

// Storage is reusable, observation authority is not. Each invocation clears the
// queried tags and wakes before any cached value may be consumed. Only immutable
// group offsets survive as facts; flag storage is overwritten on its first use.
struct WitnessScratch
{
    std::vector<std::size_t> offsets;
    std::vector<CampPresence> flags;
    std::vector<unsigned char> queried,new_wake;
    std::vector<Source> sources;
    bool initialized{};

    template<class Groups>
    bool prepare(const Groups& groups)
    {
        if (!initialized)
        {
            if (groups.size()==std::numeric_limits<std::size_t>::max()) return false;
            offsets.assign(groups.size()+1,0);
            for (std::size_t g=0;g<groups.size();++g)
            {
                const auto members=groups[g].members.size();
                if (members>std::numeric_limits<std::size_t>::max()-offsets[g]) return false;
                offsets[g+1]=offsets[g]+members;
            }
            const auto count=offsets.back();
            if (count>flags.max_size() || count>queried.max_size() ||
                count>sources.max_size() || groups.size()>new_wake.max_size()) return false;
            flags.resize(count);
            queried.resize(count);
            new_wake.resize(groups.size());
            sources.reserve(count);
            initialized=true;
        }
        else
        {
            // A single immutable World per VM is required; do not rebuild live
            // support or allocate for a different shape under that assumption.
            if (offsets.size()!=groups.size()+1) return false;
            for (std::size_t g=0;g<groups.size();++g)
                if (offsets[g+1]-offsets[g]!=groups[g].members.size()) return false;
        }
        std::fill(queried.begin(),queried.end(),0);
        std::fill(new_wake.begin(),new_wake.end(),0);
        sources.clear();
        return true;
    }

    template<class Read>
    CampPresence presence(std::size_t index,Read read)
    {
        if (!queried[index])
        {
            flags[index]=read();
            queried[index]=1;
        }
        return flags[index];
    }
};
inline CampPresence packet_presence(const MaskView& visible,const MaskView& living,
                                    const MaskView& awake,int x,int y)
{ return {visible.test(x,y),living.test(x,y),awake.test(x,y)}; }
inline bool possible_alive(CampPresence flags) { return !flags.visible || flags.living; }
inline bool possible_awake(CampPresence flags,bool alive,bool remembered,float strength)
{ return flags.visible ? alive && flags.awake : remembered || strength>0.0f; }
inline bool source_checkpoint(std::size_t index) { return index%16==0; }

// The native position query may suspend. Read the epoch after it, never before
// it, so resuming at an unchanged origin cannot preserve an earlier epoch stamp.
template<class PointType,class Position,class Clock>
bool position_epoch_current(PointType origin,int epoch,Position position,Clock clock)
{ return position()==origin && clock()==epoch; }

// Immutable camp-kind geometry is enough to certify a point outside every
// possible harmful range. Life, visibility and awake state cannot weaken this
// certificate: even every hidden camp being alive and awake cannot hit it.
template<class Groups>
bool immutable_quiet_point(const Groups& groups,Point point)
{
    for (const auto& group : groups)
        for (const auto& spec : group.members)
        {
            if (!std::isfinite(spec.strength)) return false;
            if (spec.strength>0.0f && (spec.range<0 ||
                chebyshev({spec.pos.x,spec.pos.y},point)<=spec.range)) return false;
        }
    return true;
}

// A global snapshot can require refresh after our command while an independently
// collected work frame is already fresh. This gate never upgrades that snapshot.
// The runtime path below refreshes its own facts and masks before granting coverage.
template<class StatusType,class PointType>
StatusType entry_status(StatusType global,int epoch,int now,PointType observed,PointType actual,
                        bool has_work,StatusType work_status,int work_epoch,PointType work_origin)
{
    if (epoch!=now || observed!=actual) return StatusType::Stale;
    if (global!=StatusType::Ready && global!=StatusType::NeedObservation) return global;
    if (has_work)
    {
        if (work_epoch!=epoch) return StatusType::Stale;
        if (work_status!=StatusType::Ready) return work_status;
        if (work_origin!=actual) return StatusType::NeedObservation;
    }
    return StatusType::Ready;
}

inline const mb::service::Member* find_member(const mb::service::State& state,int id)
{
    const int index=mb::service::member_index(state,id);
    return index<0 ? nullptr : &state.members[static_cast<std::size_t>(index)];
}

// The no-incoming lemma can cover only commands that leave the champion at its
// certified point. This says nothing about readiness, target legality or damage.
inline bool stationary_command(const mb::service::State& state,const mb::service::Primitive* action)
{
    if (!action) return true;
    if (action->kind==mb::service::PrimitiveKind::basic) return true;
    if (action->kind!=mb::service::PrimitiveKind::cast || action->slot<0 ||
        action->slot>=static_cast<int>(mb::service::spell_slots)) return false;
    switch (state.spells[static_cast<std::size_t>(action->slot)].kind)
    {
        case mb::service::SpellKind::damage:
        case mb::service::SpellKind::cc:
        case mb::service::SpellKind::aoe:
        case mb::service::SpellKind::reset:
        case mb::service::SpellKind::shield: return true;
        default: return false; // none includes unsupported moving spell families
    }
}

// This exact caller transform is exercised by the pure fixture below. It uses
// an actual ground aim, rather than widening from every potential target aim.
inline Footprint command_footprint(const mb::service::State& before,
                                  const mb::service::Primitive* action)
{
    if (!action) return {};
    if (action->kind==mb::service::PrimitiveKind::basic)
    {
        const auto* target=find_member(before,action->member_id);
        return target ? Footprint{true,{target->pos.x,target->pos.y},before.body.splash ? 1 : 0} : Footprint{};
    }
    if (action->slot<0 || action->slot>=static_cast<int>(mb::service::spell_slots)) return {};
    const auto& spell=before.spells[static_cast<std::size_t>(action->slot)];
    if (spell.kind==mb::service::SpellKind::aoe)
        return {true,{action->aim.x,action->aim.y},spell.radius};
    if (spell.kind==mb::service::SpellKind::damage || spell.kind==mb::service::SpellKind::cc)
        return {true,{action->aim.x,action->aim.y},0};
    return {};
}

// A harmless member can be the struck member that wakes its harmful siblings.
inline std::vector<unsigned char> wake_groups(const std::vector<Candidate>& members,
                                              Footprint footprint,std::size_t groups)
{
    std::vector<unsigned char> result(groups,0);
    if (!footprint.hit || footprint.radius<0) return result;
    for (const auto& member : members)
        if (member.alive && member.group>=0 && static_cast<std::size_t>(member.group)<groups &&
            chebyshev(member.pos,footprint.center)<=footprint.radius)
            result[static_cast<std::size_t>(member.group)]=1;
    return result;
}

inline float rate(const std::vector<Source>& sources,Point position,
                  bool external_only=false,bool current_volley=false)
{
    float sum=0.0f;
    for (const auto& source : sources)
        if (source.alive && source.awake && source.strength>0.0f &&
            (!external_only || !source.known_active) &&
            (!current_volley || source.stun<=0) && chebyshev(source.pos,position)<=source.range)
            sum+=source.strength;
    return sum;
}

// path[0] is the work origin; path.back() is quiet. The known current volley
// belongs to the micro model, so only the exposed exit suffix is charged here.
inline float suffix_damage(const std::vector<Source>& sources,
                           const std::vector<Point>& path,float multiplier)
{
    float sum=0.0f;
    for (std::size_t i=1;i+1<path.size();++i) sum+=rate(sources,path[i]);
    return sum*multiplier;
}

// A source can matter at a later landing even if it cannot hit the work point.
// This is only a frozen-geometry support test, not a route execution promise.
template<class PointType>
bool intersects_landings(Point source,int range,std::span<const PointType> path)
{
    for (const auto& point : path)
        if (chebyshev(source,{point.x,point.y})<=range) return true;
    return false;
}

enum class WitnessCell { Open, Blocked, Unknown };
enum class WitnessPath { Valid, Malformed, Blocked, Unknown };
template<class PointType,class CheckCell>
WitnessPath validate_witness_path(std::span<const PointType> path,Point start,
                                 int width,int height,CheckCell check_cell)
{
    if (path.empty() || path.front().x!=start.x || path.front().y!=start.y)
        return WitnessPath::Malformed;
    for (std::size_t i=0;i<path.size();++i)
    {
        const auto point=path[i];
        if (point.x<0 || point.y<0 || point.x>=width || point.y>=height ||
            (i && chebyshev({path[i-1].x,path[i-1].y},{point.x,point.y})!=1))
            return WitnessPath::Malformed;
        const auto cell=check_cell(point,i);
        if (cell==WitnessCell::Blocked) return WitnessPath::Blocked;
        if (cell==WitnessCell::Unknown) return WitnessPath::Unknown;
    }
    return WitnessPath::Valid;
}

struct WitnessDamage
{
    float known_next{},external_next{},reserve{},current{};
    bool safe{};
};
// A known work volley has already consumed the candidate's shield. The external
// component cannot spend it again. Without that component the current external
// volley may use the observed shield, exactly once. Future landings get no credit.
inline WitnessDamage witness_damage(float hp,float shield,float known_next,
                                    float external_next,float suffix,bool has_work)
{
    WitnessDamage result;
    result.known_next=has_work ? known_next : 0.0f;
    result.external_next=has_work ? external_next : std::max(0.0f,external_next-shield);
    result.reserve=result.external_next+suffix;
    result.current=result.known_next+result.external_next;
    result.safe=std::isfinite(hp) && std::isfinite(result.reserve) &&
        hp-result.known_next>result.reserve;
    return result;
}
} // namespace tt::safety_detail

#ifndef TT_SAFETY_PURE
#include "runtime.h"
#include "config.h"

namespace tt {
namespace {
using namespace safety_detail;
struct WakeMemory
{
    int width{},height{};
    std::vector<unsigned char> awake;
    std::vector<int> camp_at;
    std::vector<std::size_t> offsets;
};
WakeMemory& memory() { static WakeMemory value; return value; }
constexpr vec2 steps[]={{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}};
bool inside(const World& world,vec2 p)
{ return p.x>=0 && p.y>=0 && p.x<world.width && p.y<world.height; }
bool fresh_at(const cambc::champion& self,vec2 origin,int epoch)
{
    return position_epoch_current(origin,epoch,[&] { return self.position(); },
        [] { return static_cast<int>(cambc::round_number()); });
}

float fresh_multiplier(const cambc::champion& self)
{
    // Query current inventory: a post-action global snapshot might precede a sale.
    const auto inventory=self.item_count();
    const std::array<cambc::item_type,6> kinds{
        cambc::item_type::durability_emblem_of_strength,cambc::item_type::durability_emblem_of_focus,
        cambc::item_type::durability_emblem_of_health,cambc::item_type::jungle_emblem_of_strength,
        cambc::item_type::jungle_emblem_of_focus,cambc::item_type::jungle_emblem_of_health};
    static std::array<float,6> magnitudes{};
    float durability=0.0f,jungle=0.0f;
    for (std::size_t i=0;i<kinds.size();++i)
        if (inventory[kinds[i]]>0)
        {
            if (magnitudes[i]==0.0f) magnitudes[i]=kinds[i].passive_magnitude();
            if (i<3) durability=std::max(durability,magnitudes[i]);
            else jungle=std::max(jungle,magnitudes[i]);
        }
    return (1.0f-durability)*(1.0f-jungle);
}

mb::service::State exact_state(const WorkObservation& work)
{
    auto state=work.state;
    state.member_count=0;
    for (std::size_t i=0;i<work.state.member_count;++i)
        if (work.state.members[i].hp_exact)
            state.members[state.member_count++]=work.state.members[i];
    state.incoming_complete=true;
    return state;
}

} // namespace

PrefixCoverage cover_prefix(const Observation& observation,const WorkObservation* work,
                            const mb::service::Primitive* action,vec2 prospective_origin)
{
    PrefixCoverage result;
    if constexpr (cfg::trace_entry) result.trace_stage="cover-entry";
    const int epoch=observation.epoch;
    if (!current(epoch))
    { result.status=Status::Stale; return result; }
    if (observation.status!=Status::Ready && observation.status!=Status::NeedObservation)
    { result.status=observation.status; return result; }
    auto& world=World::get(); // Observation already initialized this immutable cache.
    const bool proposed=prospective_origin.x>=0 || prospective_origin.y>=0;
    const auto quiet_origin=proposed ? prospective_origin : observation.pos;
    if (proposed && (action || work))
    { result.status=Status::NeedObservation; return result; }
    // Stationary supported commands cannot leave this point; every possible camp
    // is out of range even if this command wakes it. No kill or opened tile is
    // assumed, and actual command legality still belongs to the actuator.
    if (immutable_quiet_point(world.groups,{quiet_origin.x,quiet_origin.y}))
    {
        if constexpr (cfg::trace_entry) result.trace_stage="cover-body";
        if (!budget(4000)) return result;
        auto& self=cambc::self();
        const auto actual=self.position();
        const float hp=self.health();
        const auto gate=entry_status(observation.status,epoch,static_cast<int>(cambc::round_number()),
            observation.pos,actual,work!=nullptr,work ? work->status : Status::Ready,
            work ? work->epoch : epoch,work ? vec2{work->state.body.pos.x,work->state.body.pos.y} : actual);
        if (gate!=Status::Ready) { result.status=gate; return result; }
        if (work && (work->group<0 || static_cast<std::size_t>(work->group)>=world.groups.size() ||
            work->state.member_count>mb::service::max_members))
        { result.status=Status::NeedObservation; return result; }
        if (action && (!work || !stationary_command(work->state,action)))
        { result.status=Status::NeedObservation; return result; }
        if (!inside(world,quiet_origin) || !std::isfinite(hp))
        { result.status=Status::NeedObservation; return result; }
        if (proposed)
        {
            if (distance(actual,quiet_origin)!=1 || !self.can_move())
            { result.status=Status::NeedObservation; return result; }
            auto walls=world.terrain | cambc::vision::champion_tiles() |
                cambc::vision::camp_tiles() | cambc::vision::teleport_tiles();
            walls.set(actual,false);
            if (!walls.reachable_tiles(actual,1).is_set(quiet_origin))
            { result.status=Status::NeedObservation; return result; }
        }
        result.exit_path.push_back(quiet_origin);
        if (!fresh_at(self,actual,epoch))
        { result.status=Status::Stale; result.exit_path.clear(); return result; }
        if constexpr (cfg::trace_entry) result.trace_stage="cover-ready";
        result.status=Status::Ready;
        result.safe=hp>0.0f;
        return result;
    }
    if constexpr (cfg::trace_entry) result.trace_stage="cover-body";
    if (!budget(42000)) return result;
    auto& self=cambc::self();
    const auto actual_origin=self.position();
    const bool prospective=prospective_origin.x>=0 || prospective_origin.y>=0;
    const auto origin=prospective ? prospective_origin : actual_origin;
    const float actual_hp=self.health();
    const auto own_buffs=self.buffs();
    const float actual_shield=own_buffs.has_shield() ? own_buffs.get<cambc::buff_shield>().value : 0.0f;
    const float multiplier=fresh_multiplier(self);
    const auto gate=entry_status(observation.status,epoch,static_cast<int>(cambc::round_number()),
        observation.pos,actual_origin,work!=nullptr,work ? work->status : Status::Ready,
        work ? work->epoch : epoch,work ? vec2{work->state.body.pos.x,work->state.body.pos.y} : actual_origin);
    if (gate!=Status::Ready) { result.status=gate; return result; }
    if (prospective && (action || work || !inside(world,origin) ||
        distance(actual_origin,origin)!=1))
    { result.status=Status::NeedObservation; return result; }
    if (!std::isfinite(actual_hp) || !std::isfinite(multiplier) ||
        multiplier<0.0f || (action && !work))
    { result.status=Status::NeedObservation; return result; }

    if constexpr (cfg::trace_entry) result.trace_stage="cover-projection";
    mb::service::State before,after;
    if (work)
    {
        if (work->epoch!=epoch)
        { result.status=Status::Stale; return result; }
        if (work->status!=Status::Ready)
        { result.status=work->status; return result; }
        if (work->group<0 ||
            static_cast<std::size_t>(work->group)>=world.groups.size() ||
            work->state.member_count>mb::service::max_members ||
            work->state.body.pos!=mb::service::Point{origin.x,origin.y})
        { result.status=Status::NeedObservation; return result; }
        before=exact_state(*work);
        before.body.hp=actual_hp;
        before.body.shield=actual_shield;
        before.body.camp_damage_multiplier=multiplier;
        after=before;
        if (action)
        {
            const bool applied=action->kind==mb::service::PrimitiveKind::basic
                ? mb::service::apply_basic(after,action->member_id)
                : mb::service::apply_cast(after,action->slot,action->aim,action->member_id);
            if (!applied)
            { result.status=Status::NeedObservation; return result; }
            // A projected health kill must not silently ignore an actual camp shield.
            // Unsupported shielded kills withdraw this authority, rather than opening
            // a landing tile or removing its prospective damage source.
            for (std::size_t i=0;i<before.member_count;++i)
                if (before.members[i].hp>0.0f && after.members[i].hp<=0.0f)
                {
                    if (!budget(16000)) return result;
                    const auto buffs=world.camp(work->group,before.members[i].id).buffs();
                    if (buffs.has_shield() && buffs.get<cambc::buff_shield>().value>0.0f)
                    { result.status=Status::NeedObservation; return result; }
                }
        }
        // A different body can wake any possible harmful sibling before our
        // next input. Current sleep is not an execution/return premise.
        for (std::size_t i=0;i<after.member_count;++i)
            if (after.members[i].hp>0 && after.members[i].strength>0) after.members[i].awake=true;
        const auto next=mb::service::next_camp_volley(after);
        if (!next.complete)
        { result.status=Status::NeedObservation; return result; }
        result.known_next_damage=next.hp_damage;
    }
    const auto hit=command_footprint(before,action);

    if constexpr (cfg::trace_entry) result.trace_stage="cover-masks";
    // Ready is supported here only under the caller invariant that every applied
    // command invalidates that packet. Epoch/origin alone is not an action stamp.
    const bool packet_shape=observation.body_complete &&
        packed_shape(world.width,world.height,observation.visible_bits.size()) &&
        packed_shape(world.width,world.height,observation.living_bits.size()) &&
        packed_shape(world.width,world.height,observation.awake_bits.size());
    bool borrow_packet=false;
    if (observation.status==Status::Ready && packet_shape)
    {
        const int now=static_cast<int>(cambc::round_number());
        const auto actual=self.position();
        if (now!=epoch || actual!=actual_origin)
        { result.status=Status::Stale; return result; }
        borrow_packet=can_borrow_packet(true,observation.body_complete,epoch,now,observation.pos,actual,
            world.width,world.height,observation.visible_bits.size(),observation.living_bits.size(),observation.awake_bits.size());
    }
    std::optional<MaskView> packet_visible,packet_living,packet_awake;
    std::optional<cambc::bitboard> refreshed_visible,refreshed_living,refreshed_awake;
    const cambc::bitboard* living_board=&observation.living;
    if (borrow_packet)
    {
        packet_visible.emplace(world.width,world.height,observation.visible_bits);
        packet_living.emplace(world.width,world.height,observation.living_bits);
        packet_awake.emplace(world.width,world.height,observation.awake_bits);
    }
    else
    {
        // Post-command or unsupported packets retain fresh SDK mask collection.
        refreshed_visible.emplace(cambc::vision::visible_tiles());
        refreshed_living.emplace(cambc::vision::camp_tiles());
        refreshed_awake.emplace(cambc::vision::awake_camp_tiles());
        living_board=&*refreshed_living;
    }
    auto& remembered=memory();
    if (remembered.width!=world.width || remembered.height!=world.height ||
        remembered.offsets.size()!=world.groups.size()+1)
    {
        remembered.width=world.width;
        remembered.height=world.height;
        remembered.camp_at.assign(static_cast<std::size_t>(world.width)*world.height,-1);
        remembered.offsets.assign(world.groups.size()+1,0);
        std::size_t count=0;
        for (std::size_t g=0;g<world.groups.size();++g)
        {
            remembered.offsets[g]=count;
            for (const auto& spec : world.groups[g].members)
            {
                if (!inside(world,spec.pos))
                { result.status=Status::NeedObservation; return result; }
                remembered.camp_at[static_cast<std::size_t>(spec.pos.y)*world.width+spec.pos.x]=static_cast<int>(count++);
            }
        }
        remembered.offsets.back()=count;
        remembered.awake.assign(count,0);
    }
    const auto count=remembered.awake.size();
    // Native positions on a camp intersection avoid a host membership query for
    // every map member, while preserving the SDK's documented packed representation.
    const auto mask_flags=[&](const cambc::bitboard& mask) {
        std::vector<unsigned char> flags(count,0);
        for (auto tile : (mask & world.camp_tiles).positions())
            if (inside(world,tile))
            {
                const int index=remembered.camp_at[static_cast<std::size_t>(tile.y)*world.width+tile.x];
                if (index>=0) flags[static_cast<std::size_t>(index)]=1;
            }
        return flags;
    };
    std::vector<unsigned char> visible_flags,living_flags,awake_flags;
    if (!borrow_packet)
    {
        visible_flags=mask_flags(*refreshed_visible);
        living_flags=mask_flags(*refreshed_living);
        awake_flags=mask_flags(*refreshed_awake);
    }
    std::vector<const SeenCamp*> statistics(count,nullptr);
    for (const auto& camp : observation.camps)
        if (camp.group>=0 && static_cast<std::size_t>(camp.group)<world.groups.size() &&
            camp.member>=0 && static_cast<std::size_t>(camp.member)<world.groups[static_cast<std::size_t>(camp.group)].members.size())
            statistics[remembered.offsets[static_cast<std::size_t>(camp.group)]+static_cast<std::size_t>(camp.member)]=&camp;
    std::array<const mb::service::Member*,mb::service::max_members> before_members{},after_members{};
    for (std::size_t i=0;i<before.member_count;++i)
        if (before.members[i].id>=0 && static_cast<std::size_t>(before.members[i].id)<before_members.size())
            before_members[static_cast<std::size_t>(before.members[i].id)]=&before.members[i];
    for (std::size_t i=0;i<after.member_count;++i)
        if (after.members[i].id>=0 && static_cast<std::size_t>(after.members[i].id)<after_members.size())
            after_members[static_cast<std::size_t>(after.members[i].id)]=&after.members[i];
    if constexpr (cfg::trace_entry) result.trace_stage="cover-sources";
    std::vector<Candidate> candidates;
    std::vector<Source> sources;
    candidates.reserve(160);
    sources.reserve(160);
    std::size_t source_index=0;
    for (std::size_t g=0;g<world.groups.size();++g)
        for (std::size_t m=0;m<world.groups[g].members.size();++m)
        {
            if constexpr (cfg::trace_entry) result.trace_detail=(borrow_packet ? 1000 : 0)+static_cast<int>(source_index);
            if (source_checkpoint(source_index++) && !budget(14000)) return result;
            const auto& spec=world.groups[g].members[m];
            const auto flat=remembered.offsets[g]+m;
            const auto flags=borrow_packet ? packet_presence(*packet_visible,*packet_living,*packet_awake,spec.pos.x,spec.pos.y)
                : CampPresence{visible_flags[flat]!=0,living_flags[flat]!=0,awake_flags[flat]!=0};
            const bool seen=flags.visible;
            bool alive=possible_alive(flags);
            const bool active=work && static_cast<int>(g)==work->group;
            const auto* exact=active && m<after_members.size() ? after_members[m] : nullptr;
            const bool known=exact!=nullptr;
            if (exact) alive=exact->hp>0.0f;
            bool source_awake=possible_awake(flags,alive,remembered.awake[flat],spec.strength);
            if (seen)
            {
                remembered.awake[flat]=source_awake;
            }
            // An unseen source may be awake because of a different actor; lack
            // of an awake bit or remembered sighting does not establish sleep.
            if (exact) source_awake=exact->awake;
            float strength=spec.strength;
            int range=spec.range,stun=exact ? exact->stun_remaining : 0;
            if (const auto* camp=statistics[flat])
            { strength=camp->strength; range=camp->range; if (!exact) stun=observed_stun(camp->stun,camp->stun_exact); }
            if (alive && strength>0) source_awake=true;
            const auto* old_exact=active && m<before_members.size() ? before_members[m] : nullptr;
            const bool alive_before=old_exact ? old_exact->hp>0.0f : alive;
            candidates.push_back({static_cast<int>(g),{spec.pos.x,spec.pos.y},alive_before});
            sources.push_back({static_cast<int>(g),static_cast<int>(m),{spec.pos.x,spec.pos.y},
                strength,range,stun,alive,source_awake,known});
        }
    const auto new_wake=safety_detail::wake_groups(candidates,hit,world.groups.size());
    for (auto& source : sources)
        if (new_wake[static_cast<std::size_t>(source.group)]) source.awake=true;
    if (!fresh_at(self,actual_origin,epoch))
    { result.status=Status::Stale; return result; }

    float external_current=rate(sources,{origin.x,origin.y},true,true)*multiplier;
    if (!work)
    {
        // No separate micro component: this current volley spends the shield once.
        external_current=std::max(0.0f,external_current-actual_shield);
    }
    // With a micro component it already consumes up to the full shield. Leaving
    // the external component unshielded is conservative and never spends it twice.
    result.current_damage=result.known_next_damage+external_current;
    result.reserve=external_current;

    if constexpr (cfg::trace_entry) result.trace_stage="cover-danger";
    cambc::bitboard danger;
    for (const auto& source : sources)
        if (source.alive && source.awake && source.strength>0.0f)
        {
            if (!budget(4000)) return result;
            danger.draw_chebyshev(vec2{source.pos.x,source.pos.y},static_cast<float>(source.range));
        }
    if (!budget(4000)) return result;
    auto walls=world.terrain | cambc::vision::champion_tiles() | *living_board | cambc::vision::teleport_tiles();
    walls.set(actual_origin,false);
    if (prospective && walls.is_set(origin))
    { result.status=Status::NeedObservation; return result; }
    walls.set(origin,false);
    // Only a source whose projected kill was shield-checked may open its tile.
    if (work && action)
        for (std::size_t i=0;i<after.member_count;++i)
            if (after.members[i].hp<=0.0f)
            {
                const vec2 tile{after.members[i].pos.x,after.members[i].pos.y};
                if (!world.terrain.is_set(tile)) walls.set(tile,false);
            }
    const auto roots=~(danger|walls);
    result.exit_path.push_back(origin);
    if (!danger.is_set(origin))
    {
        if (!fresh_at(self,actual_origin,epoch))
        { result.status=Status::Stale; result.exit_path.clear(); return result; }
        if constexpr (cfg::trace_entry) result.trace_stage="cover-ready";
        result.status=Status::Ready;
        result.safe=actual_hp-result.known_next_damage>result.reserve;
        return result;
    }
    if constexpr (cfg::trace_entry) result.trace_stage="cover-field";
    if (!budget(24000)) { result.exit_path.clear(); return result; }
    cambc::bitboard destinations,visited;
    destinations.set(origin);
    cambc::distance_field field;
    field.compute_to(walls,roots,destinations,visited);
    const auto d=field.get(origin);
    if (!fresh_at(self,actual_origin,epoch))
    { result.status=Status::Stale; result.exit_path.clear(); return result; }
    if (d==cambc::distance_field::unreachable)
    { result.status=Status::Unreachable; result.exit_path.clear(); return result; }
    if constexpr (cfg::trace_entry) result.trace_stage="cover-path";
    const auto linear=field.export_linear_buffer();
    const auto cells=static_cast<std::size_t>(world.width)*static_cast<std::size_t>(world.height);
    if (linear.size()!=cells || static_cast<std::size_t>(d)>=cells)
    { result.status=Status::NeedObservation; result.exit_path.clear(); return result; }
    auto at=origin;
    int remaining=d;
    while (remaining>0)
    {
        if (!budget(14000)) { result.exit_path.clear(); return result; }
        bool found=false;
        for (auto step : steps)
        {
            const auto next=at+step;
            if (inside(world,next) && linear[static_cast<std::size_t>(next.y)*world.width+next.x]==remaining-1)
            { at=next; found=true; break; }
        }
        if (!found)
        { result.status=Status::NeedObservation; result.exit_path.clear(); return result; }
        --remaining;
        result.exit_path.push_back(at);
        // Final quiet landing produces no camp volley in this conditional route.
        if (remaining>0) result.reserve+=rate(sources,{at.x,at.y})*multiplier;
    }
    if (!fresh_at(self,actual_origin,epoch))
    { result.status=Status::Stale; result.exit_path.clear(); return result; }
    if constexpr (cfg::trace_entry) result.trace_stage="cover-ready";
    result.status=Status::Ready;
    result.safe=std::isfinite(result.reserve) && actual_hp-result.known_next_damage>result.reserve;
    return result;
}

bool immutable_camp_quiet(vec2 point)
{
    auto& world=World::get();
    if (!inside(world,point)) return false;
    // One immutable World is installed per VM. Repeated current/destination/
    // anchor checks therefore need only this small value cache, never a map grid.
    struct QuietEntry { vec2 point; bool value{},valid{}; };
    static std::array<QuietEntry,4> cache{};
    static std::size_t next=0;
    for (const auto& entry : cache)
        if (entry.valid && entry.point==point) return entry.value;
    const bool quiet=immutable_quiet_point(world.groups,{point.x,point.y});
    cache[next]={point,quiet,true};
    next=(next+1)%cache.size();
    return quiet;
}

PrefixCoverage cover_witness(const Observation& observation,const WorkObservation* work,
                             const mb::service::Primitive* action,
                             std::span<const vec2> return_path,vec2 prospective_origin)
{
    PrefixCoverage result;
    if constexpr (cfg::trace_entry) result.trace_stage="witness-entry";
    const int epoch=observation.epoch;
    if (!current(epoch)) { result.status=Status::Stale; return result; }
    if (observation.status!=Status::Ready && observation.status!=Status::NeedObservation)
    { result.status=observation.status; return result; }
    // These are bounded-work admission checks, not a numeric fuel proof. Every
    // native tail retains the common 14k reserve; the full fallback is unchanged.
    if (!budget(12000)) return result;
    auto& world=World::get();
    auto& self=cambc::self();
    const auto actual=self.position();
    const bool proposed=prospective_origin.x>=0 || prospective_origin.y>=0;
    const auto origin=proposed ? prospective_origin : actual;
    const auto gate=entry_status(observation.status,epoch,static_cast<int>(cambc::round_number()),
        observation.pos,actual,work!=nullptr,work ? work->status : Status::Ready,
        work ? work->epoch : epoch,work ? vec2{work->state.body.pos.x,work->state.body.pos.y} : actual);
    if (gate!=Status::Ready) { result.status=gate; return result; }
    if ((proposed && (work || action || !inside(world,origin) || distance(actual,origin)!=1)) ||
        (action && !work) || return_path.empty())
    { result.status=Status::NeedObservation; return result; }
    if (work && (work->group<0 || static_cast<std::size_t>(work->group)>=world.groups.size() ||
        work->state.member_count>mb::service::max_members ||
        (action && !stationary_command(work->state,action))))
    { result.status=Status::NeedObservation; return result; }
    if (!immutable_camp_quiet(return_path.back()))
    { result.status=Status::NeedObservation; return result; }

    if constexpr (cfg::trace_entry) result.trace_stage="witness-body";
    mb::service::Body body;
    // The caller invalidates global Ready after every command, and freshly
    // replaces WorkReady after an applied primitive. Origin/epoch alone do not
    // encode that command ordering. A fresh Work body never promotes masks.
    if (work) body=work->state.body;
    else if (observation.status==Status::Ready && observation.body_complete &&
        observation.service_body.pos==mb::service::Point{actual.x,actual.y})
        body=observation.service_body;
    else
    {
        body.pos={actual.x,actual.y};
        body.hp=self.health();
        const auto buffs=self.buffs();
        body.shield=buffs.has_shield() ? buffs.get<cambc::buff_shield>().value : 0.0f;
        body.camp_damage_multiplier=fresh_multiplier(self);
    }
    if (!std::isfinite(body.hp) || !std::isfinite(body.shield) || body.shield<0.0f ||
        !std::isfinite(body.camp_damage_multiplier) || body.camp_damage_multiplier<0.0f)
    { result.status=Status::NeedObservation; return result; }
    if (proposed && !self.can_move())
    { result.status=Status::NeedObservation; return result; }

    if constexpr (cfg::trace_entry) result.trace_stage="witness-projection";
    mb::service::State before,after;
    if (work)
    {
        before=exact_state(*work);
        before.body=body;
        after=before;
        if (action)
        {
            const bool applied=action->kind==mb::service::PrimitiveKind::basic
                ? mb::service::apply_basic(after,action->member_id)
                : mb::service::apply_cast(after,action->slot,action->aim,action->member_id);
            if (!applied) { result.status=Status::NeedObservation; return result; }
            for (std::size_t i=0;i<before.member_count;++i)
                if (before.members[i].hp>0.0f && after.members[i].hp<=0.0f)
                {
                    // The model has health but no camp shield. A candidate kill
                    // needs the same native shield check as the full fallback.
                    if (!budget(16000)) return result;
                    const auto buffs=world.camp(work->group,before.members[i].id).buffs();
                    if (buffs.has_shield() && buffs.get<cambc::buff_shield>().value>0.0f)
                    { result.status=Status::NeedObservation; return result; }
                }
        }
        for (std::size_t i=0;i<after.member_count;++i)
            if (after.members[i].hp>0 && after.members[i].strength>0) after.members[i].awake=true;
        const auto next=mb::service::next_camp_volley(after);
        if (!next.complete) { result.status=Status::NeedObservation; return result; }
        result.known_next_damage=next.hp_damage;
    }
    const auto footprint=command_footprint(before,action);

    if constexpr (cfg::trace_entry) result.trace_stage="witness-masks";
    const bool borrow_global=can_borrow_packet(observation.status==Status::Ready,observation.body_complete,
        epoch,static_cast<int>(cambc::round_number()),observation.pos,self.position(),
        world.width,world.height,observation.visible_bits.size(),observation.living_bits.size(),
        observation.awake_bits.size());
    // A Work packet owns fields captured after the last actual command. Its
    // masks have the same caller-enforced lifetime as its fresh body/residuals;
    // consuming them never publishes global Ready.
    const bool borrow_work=work && work->masks_complete && can_borrow_packet(
        work->status==Status::Ready,true,work->epoch,static_cast<int>(cambc::round_number()),
        vec2{work->state.body.pos.x,work->state.body.pos.y},self.position(),world.width,world.height,
        work->visible_bits.size(),work->living_bits.size(),work->awake_bits.size());
    const bool borrow=borrow_work || borrow_global;
    if (!fresh_at(self,actual,epoch))
    { result.status=Status::Stale; return result; }
    std::optional<MaskView> packet_visible,packet_living,packet_awake;
    std::optional<cambc::bitboard> fresh_visible,fresh_living,fresh_awake;
    if (borrow)
    {
        packet_visible.emplace(world.width,world.height,borrow_work ? work->visible_bits : observation.visible_bits);
        packet_living.emplace(world.width,world.height,borrow_work ? work->living_bits : observation.living_bits);
        packet_awake.emplace(world.width,world.height,borrow_work ? work->awake_bits : observation.awake_bits);
    }
    else
    {
        if (!budget(8000)) return result;
        fresh_visible.emplace(cambc::vision::visible_tiles());
        fresh_living.emplace(cambc::vision::camp_tiles());
        fresh_awake.emplace(cambc::vision::awake_camp_tiles());
    }
    if (!budget(6000)) return result;
    const auto champions=cambc::vision::champion_tiles();
    const auto teleports=cambc::vision::teleport_tiles();
    const auto living_at=[&](vec2 point) {
        return borrow ? packet_living->test(point.x,point.y) : fresh_living->is_set(point);
    };
    if constexpr (cfg::trace_entry) result.trace_stage="witness-path";
    int min_x=origin.x,max_x=origin.x,min_y=origin.y,max_y=origin.y;
    const auto path_check=validate_witness_path(return_path,{origin.x,origin.y},world.width,world.height,
        [&](vec2 point,std::size_t index) {
            if (index%4==0 && !budget(4000)) return WitnessCell::Unknown;
            // No projected kill opens a breadcrumb cell here. The witness is a
            // currently free path, conditional on future movement/occupancy.
            if (world.terrain.is_set(point) || teleports.is_set(point) || living_at(point) ||
                (point!=actual && champions.is_set(point))) return WitnessCell::Blocked;
            min_x=std::min(min_x,point.x); max_x=std::max(max_x,point.x);
            min_y=std::min(min_y,point.y); max_y=std::max(max_y,point.y);
            return WitnessCell::Open;
        });
    if (path_check!=WitnessPath::Valid)
    {
        result.witness_blocked=path_check==WitnessPath::Blocked;
        result.status=path_check==WitnessPath::Unknown ? Status::NeedCompute : Status::NeedObservation;
        return result;
    }
    // Ordinary movement is one of the eight adjacent landings. Its current
    // allowance and every landing's terrain/camp/champion/teleport occupancy
    // were checked above; no whole-map field is needed for that local premise.
    // The actuator independently refreshes actual command legality afterward.
    // Every future path leg remains conditional on its later fresh authority.

    if constexpr (cfg::trace_entry) result.trace_stage="witness-sources";
    const int source_entry_fuel=cfg::trace_entry ? static_cast<int>(cambc::fuel_remaining()) : 0;
    if constexpr (cfg::trace_entry) result.trace_detail=source_entry_fuel*1000;
    // Only metadata is scanned globally. Native presence is queried lazily for
    // a struck member or a source supported by at least one represented landing.
    static WitnessScratch scratch;
    if (!scratch.prepare(world.groups))
    { result.status=Status::NeedObservation; return result; }
    const auto& offsets=scratch.offsets;
    const auto count=offsets.back();
    auto& new_wake=scratch.new_wake;
    auto& sources=scratch.sources;
    const auto presence=[&](std::size_t g,std::size_t m) {
        const auto flat=offsets[g]+m;
        return scratch.presence(flat,[&] {
            const auto point=world.groups[g].members[m].pos;
            return borrow ? packet_presence(*packet_visible,*packet_living,*packet_awake,point.x,point.y)
                : CampPresence{fresh_visible->is_set(point),fresh_living->is_set(point),fresh_awake->is_set(point)};
        });
    };
    std::array<const mb::service::Member*,mb::service::max_members> old_members{},new_members{};
    for (std::size_t i=0;i<before.member_count;++i)
        if (before.members[i].id>=0 && static_cast<std::size_t>(before.members[i].id)<old_members.size())
            old_members[static_cast<std::size_t>(before.members[i].id)]=&before.members[i];
    for (std::size_t i=0;i<after.member_count;++i)
        if (after.members[i].id>=0 && static_cast<std::size_t>(after.members[i].id)<new_members.size())
            new_members[static_cast<std::size_t>(after.members[i].id)]=&after.members[i];
    std::size_t metadata_index=0;
    if (footprint.hit)
        for (std::size_t g=0;g<world.groups.size();++g)
            for (std::size_t m=0;m<world.groups[g].members.size();++m)
            {
                if (source_checkpoint(metadata_index++) && !budget(4000)) return result;
                const auto& spec=world.groups[g].members[m];
                if (!inside(world,spec.pos)) { result.status=Status::NeedObservation; return result; }
                if (chebyshev({spec.pos.x,spec.pos.y},footprint.center)>footprint.radius) continue;
                const bool active=work && static_cast<int>(g)==work->group;
                const auto* exact=active && m<old_members.size() ? old_members[m] : nullptr;
                const bool alive=exact ? exact->hp>0.0f : possible_alive(presence(g,m));
                // Even a harmless possible hit wakes all possible live siblings.
                if (alive) new_wake[g]=1;
            }
    struct RememberedSources { std::vector<unsigned char> awake; };
    static RememberedSources remembered;
    if (remembered.awake.size()!=count) remembered.awake.assign(count,0);
    metadata_index=0;
    for (std::size_t g=0;g<world.groups.size();++g)
        for (std::size_t m=0;m<world.groups[g].members.size();++m)
        {
            if constexpr (cfg::trace_entry) result.trace_detail=source_entry_fuel*1000+static_cast<int>(metadata_index);
            if (source_checkpoint(metadata_index++) && !budget(4000)) return result;
            const auto& spec=world.groups[g].members[m];
            if (!inside(world,spec.pos) || !std::isfinite(spec.strength) ||
                (spec.strength>0.0f && spec.range<0))
            { result.status=Status::NeedObservation; return result; }
            if (spec.strength<=0.0f) continue;
            // A necessary bounding-box test avoids native budget checks for
            // every remote source while retaining every landing in the support.
            if (spec.pos.x+spec.range<min_x || spec.pos.x-spec.range>max_x ||
                spec.pos.y+spec.range<min_y || spec.pos.y-spec.range>max_y) continue;
            bool supported=false;
            for (std::size_t first=0;first<return_path.size() && !supported;first+=16)
            {
                if (!budget(4000)) return result;
                supported=intersects_landings({spec.pos.x,spec.pos.y},spec.range,
                    return_path.subspan(first,std::min<std::size_t>(16,return_path.size()-first)));
            }
            if (!supported) continue;
            if (!budget(4000)) return result;
            const auto flat=offsets[g]+m;
            const auto present=presence(g,m);
            const bool active=work && static_cast<int>(g)==work->group;
            const auto* exact=active && m<new_members.size() ? new_members[m] : nullptr;
            bool alive=exact ? exact->hp>0.0f : possible_alive(present);
            bool awake=possible_awake(present,alive,remembered.awake[flat]!=0,spec.strength);
            if (present.visible) remembered.awake[flat]=awake;
            if (exact) awake=exact->awake;
            if (new_wake[g]) awake=true;
            if (alive && spec.strength>0) awake=true;
            int stun=exact ? exact->stun_remaining : 0;
            // A Ready packet is the only supported source for its exact stun.
            // Post-command global statistics cannot deny the next volley.
            if (!exact && borrow_global)
                for (const auto& seen : observation.camps)
                    if (seen.group==static_cast<int>(g) && seen.member==static_cast<int>(m))
                    { stun=observed_stun(seen.stun,seen.stun_exact); break; }
            sources.push_back({static_cast<int>(g),static_cast<int>(m),{spec.pos.x,spec.pos.y},
                spec.strength,spec.range,stun,alive,awake,exact!=nullptr});
            if constexpr (cfg::trace_entry) result.trace_detail=source_entry_fuel*1000+static_cast<int>(metadata_index);
        }
    if (!fresh_at(self,actual,epoch))
    { result.status=Status::Stale; return result; }
    const float multiplier=body.camp_damage_multiplier;
    const float external=rate(sources,{origin.x,origin.y},true,true)*multiplier;
    float suffix=0.0f;
    for (std::size_t i=1;i+1<return_path.size();++i)
    {
        if (source_checkpoint(i) && !budget(4000)) return result;
        suffix+=rate(sources,{return_path[i].x,return_path[i].y})*multiplier;
    }
    const auto damage=witness_damage(body.hp,body.shield,result.known_next_damage,external,suffix,work!=nullptr);
    result.reserve=damage.reserve;
    result.current_damage=damage.current;
    result.safe=damage.safe;
    result.exit_path.assign(return_path.begin(),return_path.end());
    if (!fresh_at(self,actual,epoch))
    { result.status=Status::Stale; result.exit_path.clear(); return result; }
    if constexpr (cfg::trace_entry) result.trace_stage="witness-ready";
    result.status=Status::Ready;
    return result;
}
} // namespace tt
#endif
