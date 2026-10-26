#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

// Position envelopes and geometric relevance only. No SDK observations, hidden
// attributes, action selection, chosen scenario, or tactical safety certificate.
namespace wb::capital::encounter_support {
inline constexpr std::size_t max_actors=6,max_cells=10000,max_bytes=(max_cells+7)/8;
inline constexpr unsigned max_propagations=24,max_opportunities=24;
struct Point {int x{-1},y{-1};friend bool operator==(Point,Point)=default;};
struct Stamp {
    std::uint64_t domain{},effect{},sample{};std::uint32_t actor{};int round{-1};
    friend bool operator==(const Stamp&,const Stamp&)=default;
};
enum class State {Unknown,Complete,Visible,Absent,Inconsistent};
enum class Reason {
    None,Shape,Domain,Identity,Date,MissingMovementBound,PropagationLimit,
    Workspace,Partial,MissingNegativeVisionScope,ContradictoryEvidence,
    MissingReachScope,Horizon,UnboundedFuture
};
enum class Membership {Unknown,Excluded,Possible,Observed};
struct Bits {
    std::span<const std::uint8_t> bytes{};
    bool has(std::size_t cell)const {return cell/8<bytes.size()&&(bytes[cell/8]&(1u<<(cell%8)));}
};
struct MutableBits {
    std::span<std::uint8_t> bytes{};
    void set(std::size_t cell)const {bytes[cell/8]|=static_cast<std::uint8_t>(1u<<(cell%8));}
};
struct Map {
    std::uint64_t domain{};int width{},height{};
    // Complete admissible endpoint universe, including every relevant spawn.
    // Interior propagation is NOT constrained by walkability: a dash can cross
    // blocked intermediate cells. A visible actor is never removed by this mask.
    Bits walkable{},spawn{};bool endpoints_complete{};
    // This must be actual current personally observed absence coverage under
    // the actor's correct visibility conditions. Terrain/FOV approximations do
    // not qualify. It applies only to an actor currently unseen.
    Bits negative_vision{};int negative_round{-1};bool negative_vision_complete{};
    Stamp negative_source{}; // Exact actor/visibility-condition/effect/sample scope.
};
enum class Life {Unknown,Alive,Dead};
struct ActorEvidence {
    Stamp current{};int sampled_round{-1};
    bool visible{};Point observed{};
    bool last_seen_known{};Point last_seen{};int last_seen_round{-1};
    Stamp last_seen_source{};
    Life life{Life::Unknown};int life_round{-1};
    bool respawn_time_known{};int respawn_round{-1};
    // This upper bound covers ALL admitted movement from movement_from through
    // current.round. Unknown/teleport branches cannot borrow a walking radius.
    bool displacement_known{};int movement_from{-1};unsigned max_displacement{};
    Stamp displacement_from{},displacement_to{};
    bool unknown_movement{},teleport_branch{};
};
struct Support {
    State state{State::Unknown};Reason reason{Reason::None};Stamp stamp{};
    Bits positions{};int width{},height{};std::size_t count{};
    int absent_from{-1},absent_through{-1};
    bool complete()const {return state==State::Complete||state==State::Visible||state==State::Absent;}
    Membership membership(Point p)const {
        if(!complete())return Membership::Unknown;
        if(p.x<0||p.y<0||p.x>=width||p.y>=height||state==State::Absent)return Membership::Excluded;
        if(!positions.has(std::size_t(p.y)*std::size_t(width)+std::size_t(p.x)))return Membership::Excluded;
        return state==State::Visible?Membership::Observed:Membership::Possible;
    }
};
// All spans are borrowed only for the current call/result lifetime. Reusing
// workspace/output invalidates earlier views; no retained pointer is owned here.
struct Workspace {MutableBits first{},second{};};
struct Work {
    void* context{};bool(*permit)(void*){};
    bool step()const {return !permit||permit(context);}
};
namespace detail {
inline std::size_t cells(const Map& map) {
    if(map.width<=0||map.height<=0||std::uint64_t(map.width)*std::uint64_t(map.height)>max_cells)return 0;
    return std::size_t(map.width)*std::size_t(map.height);
}
inline bool inside(Point p,const Map& map){return p.x>=0&&p.y>=0&&p.x<map.width&&p.y<map.height;}
inline std::size_t index(Point p,const Map& map){return std::size_t(p.y)*std::size_t(map.width)+std::size_t(p.x);}
inline bool admissible(const Map& map,std::size_t cell){return map.walkable.has(cell)||map.spawn.has(cell);}
inline bool overlap(std::span<const std::uint8_t> a,std::span<const std::uint8_t> b) {
    if(a.empty()||b.empty())return false;
    const auto x=reinterpret_cast<std::uintptr_t>(a.data()),y=reinterpret_cast<std::uintptr_t>(b.data());
    return x<=y?y-x<a.size():x-y<b.size();
}
inline bool clear(MutableBits bits,std::size_t bytes,Work work){
    for(std::size_t i=0;i<bytes;++i){if(!work.step())return false;bits.bytes[i]=0;}return true;
}
inline std::int64_t distance(Point a,Point b){
    const auto x=std::int64_t(a.x)-b.x,y=std::int64_t(a.y)-b.y;
    return std::max(x<0?-x:x,y<0?-y:y);
}
}
// A complete envelope can contain many hypothetical positions. It does not say
// any one chosen cell is actual or that a finite scenario list covers replies.
inline Support derive(const Map& map,const ActorEvidence& evidence,const Support* prior,
    Workspace workspace,MutableBits output,Work work={}) {
    Support out;out.stamp=evidence.current;out.width=map.width;out.height=map.height;
    const auto fail=[&](Reason reason,State state=State::Unknown){out.state=state;out.reason=reason;out.positions={};out.count=0;return out;};
    const auto size=detail::cells(map),bytes=(size+7)/8;
    if(!size||!map.endpoints_complete||map.walkable.bytes.size()<bytes||
       (!map.spawn.bytes.empty()&&map.spawn.bytes.size()<bytes))return fail(Reason::Shape);
    if(!map.domain||evidence.current.domain!=map.domain)return fail(Reason::Domain);
    const auto now=evidence.current.round;
    if(now<0||evidence.sampled_round<0||evidence.sampled_round>now)return fail(Reason::Date);
    if(output.bytes.size()<bytes||workspace.first.bytes.size()<bytes||workspace.second.bytes.size()<bytes||
       detail::overlap(output.bytes,workspace.first.bytes)||detail::overlap(output.bytes,workspace.second.bytes)||
       detail::overlap(workspace.first.bytes,workspace.second.bytes)||
       detail::overlap(output.bytes,map.walkable.bytes)||detail::overlap(output.bytes,map.spawn.bytes)||
       detail::overlap(output.bytes,map.negative_vision.bytes)||
       detail::overlap(workspace.first.bytes,map.walkable.bytes)||detail::overlap(workspace.first.bytes,map.spawn.bytes)||
       detail::overlap(workspace.first.bytes,map.negative_vision.bytes)||
       detail::overlap(workspace.second.bytes,map.walkable.bytes)||detail::overlap(workspace.second.bytes,map.spawn.bytes)||
       detail::overlap(workspace.second.bytes,map.negative_vision.bytes))return fail(Reason::Workspace);
    if(prior&&(detail::overlap(prior->positions.bytes,output.bytes)||
       detail::overlap(prior->positions.bytes,workspace.first.bytes)||detail::overlap(prior->positions.bytes,workspace.second.bytes)))
        return fail(Reason::Workspace);
    if(!detail::clear(output,bytes,work))return fail(Reason::Partial);
    if(evidence.visible) {
        if(evidence.sampled_round!=now||!detail::inside(evidence.observed,map))
            return fail(Reason::ContradictoryEvidence,State::Inconsistent);
        // Current positive position wins over stale/contradictory life or
        // negative masks. This module supplies no alive/dead combat authority.
        output.set(detail::index(evidence.observed,map));out.positions={output.bytes.first(bytes)};
        out.count=1;out.state=State::Visible;return out;
    }
    if(evidence.life_round>now||(evidence.respawn_time_known&&
       (evidence.life!=Life::Dead||evidence.life_round<0||evidence.respawn_round<=evidence.life_round)))return fail(Reason::Date);
    const bool absent=evidence.life==Life::Dead&&evidence.life_round>=0&&evidence.life_round<=now&&
        evidence.respawn_time_known&&evidence.respawn_round>now;
    if(absent){out.state=State::Absent;out.positions={output.bytes.first(bytes)};
        out.absent_from=evidence.life_round;out.absent_through=evidence.respawn_round-1;return out;}
    if(!map.negative_vision.bytes.empty()&&(!map.negative_vision_complete||map.negative_round!=now||
       map.negative_source!=evidence.current||
       map.negative_vision.bytes.size()<bytes))return fail(Reason::MissingNegativeVisionScope);
    bool whole=evidence.unknown_movement||evidence.teleport_branch;
    int basis=-1;Stamp basis_stamp{};bool basis_exact=false;
    if(!detail::clear(workspace.first,bytes,work))return fail(Reason::Partial);
    if(!whole&&evidence.life==Life::Dead&&evidence.respawn_time_known&&
       evidence.respawn_round>=0&&evidence.respawn_round<=now&&!map.spawn.bytes.empty()) {
        basis=evidence.respawn_round;
        for(std::size_t i=0;i<bytes;++i){if(!work.step())return fail(Reason::Partial);workspace.first.bytes[i]=map.spawn.bytes[i];}
    } else if(!whole&&prior) {
        if(!prior->complete()||prior->state==State::Absent||prior->width!=map.width||prior->height!=map.height||
           prior->positions.bytes.size()<bytes)return fail(Reason::Identity);
        if(prior->stamp.domain!=map.domain||prior->stamp.actor!=evidence.current.actor)return fail(Reason::Identity);
        if(prior->stamp.round<0||prior->stamp.round>now)return fail(Reason::Date);
        basis=prior->stamp.round;
        basis_stamp=prior->stamp;basis_exact=true;
        for(std::size_t i=0;i<bytes;++i){if(!work.step())return fail(Reason::Partial);workspace.first.bytes[i]=prior->positions.bytes[i];}
    } else if(!whole&&evidence.last_seen_known) {
        if(evidence.last_seen_round<0||evidence.last_seen_round>now||!detail::inside(evidence.last_seen,map))return fail(Reason::Date);
        if(evidence.last_seen_source.round!=evidence.last_seen_round||evidence.last_seen_source.domain!=map.domain||
           evidence.last_seen_source.actor!=evidence.current.actor)return fail(Reason::Identity);
        basis=evidence.last_seen_round;basis_stamp=evidence.last_seen_source;basis_exact=true;
        workspace.first.set(detail::index(evidence.last_seen,map));
    } else whole=true;
    if(!whole&&(!evidence.displacement_known||evidence.movement_from!=basis||
       evidence.displacement_to!=evidence.current||evidence.displacement_from.round!=basis||
       evidence.displacement_from.domain!=map.domain||evidence.displacement_from.actor!=evidence.current.actor||
       (basis_exact&&evidence.displacement_from!=basis_stamp)))whole=true;
    if(!whole&&evidence.max_displacement>max_propagations)return fail(Reason::PropagationLimit);
    auto current=workspace.first,next=workspace.second;
    if(!whole)for(unsigned step=0;step<evidence.max_displacement;++step) {
        if(!detail::clear(next,bytes,work))return fail(Reason::Partial);
        for(std::size_t cell=0;cell<size;++cell) {
            if(!work.step())return fail(Reason::Partial);
            if(!Bits{current.bytes}.has(cell))continue;
            const auto x=int(cell%std::size_t(map.width)),y=int(cell/std::size_t(map.width));
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const Point p{x+dx,y+dy};if(detail::inside(p,map))next.set(detail::index(p,map));}
        }
        std::swap(current,next);
    }
    for(std::size_t cell=0;cell<size;++cell) {
        if(!work.step())return fail(Reason::Partial);
        if(detail::admissible(map,cell)&&(whole||Bits{current.bytes}.has(cell))&&!map.negative_vision.has(cell)){
            output.set(cell);++out.count;
        }
    }
    // Exhausted/contradictory positive evidence is never a vacuous separation
    // proof. Only the explicit dated dead interval above admits an empty set.
    if(!out.count)return fail(Reason::ContradictoryEvidence,State::Inconsistent);
    out.state=State::Complete;out.positions={output.bytes.first(bytes)};return out;
}
struct Opportunity {int round{-1};Point owner_position{};};
struct Reach {
    Stamp source{};int horizon{-1};bool all_words_covered{},movement_known{},attack_known{},control_known{};
    unsigned displacement_through_horizon{},attack{},control{};bool unbounded_branch{};
    bool no_persistent_effects{}; // Required to exclude a dead actor's earlier delayed/ground effects.
};
enum class Relevance {Unknown,Relevant,ProvenIrrelevant};
struct RelevanceResult {Relevance state{Relevance::Unknown};Reason reason{Reason::None};std::size_t checked{};};
// Reach is a caller-supplied envelope over EVERY admitted word, including dash,
// reset/control interactions and delayed effects. Unknown kit/effect scope is
// Unknown, never a free nonparticipant. Overlap is merely possible relevance.
inline RelevanceResult relevance(const Support& support,const Reach& reach,
    std::span<const Opportunity> opportunities,Work work={}) {
    RelevanceResult out;
    const auto fail=[&](Reason reason){out.reason=reason;return out;};
    if(!support.complete())return fail(support.reason==Reason::None?Reason::Identity:support.reason);
    if(reach.source!=support.stamp)return fail(Reason::Identity);
    if(reach.horizon<support.stamp.round||opportunities.empty()||opportunities.size()>max_opportunities)return fail(Reason::Horizon);
    for(const auto& opportunity:opportunities)if(opportunity.round<support.stamp.round||opportunity.round>reach.horizon||
        opportunity.owner_position.x<0||opportunity.owner_position.y<0||opportunity.owner_position.x>=support.width||opportunity.owner_position.y>=support.height)
        return fail(Reason::Horizon);
    if(support.state==State::Absent) {
        if(!reach.all_words_covered||!reach.no_persistent_effects)return fail(Reason::MissingReachScope);
        if(support.absent_from<=support.stamp.round&&support.absent_through>=reach.horizon){out.state=Relevance::ProvenIrrelevant;return out;}
        return fail(Reason::Horizon);
    }
    if(!reach.all_words_covered||!reach.movement_known||!reach.attack_known||!reach.control_known)return fail(Reason::MissingReachScope);
    if(reach.unbounded_branch)return fail(Reason::UnboundedFuture);
    const auto radius=std::uint64_t(reach.displacement_through_horizon)+std::max(reach.attack,reach.control);
    const auto size=std::size_t(support.width)*std::size_t(support.height);
    if(!size||size>max_cells||support.positions.bytes.size()<(size+7)/8||!support.count)return fail(Reason::Shape);
    for(std::size_t cell=0;cell<size;++cell) {
        if(!work.step())return fail(Reason::Partial);
        if(!support.positions.has(cell))continue;
        const Point p{int(cell%std::size_t(support.width)),int(cell/std::size_t(support.width))};
        for(const auto& opportunity:opportunities){if(!work.step())return fail(Reason::Partial);++out.checked;
            if(std::uint64_t(detail::distance(p,opportunity.owner_position))<=radius){out.state=Relevance::Relevant;return out;}}
    }
    out.state=Relevance::ProvenIrrelevant;return out;
}
inline bool roster_supported(std::span<const ActorEvidence> actors) {
    if(actors.size()>max_actors)return false;
    for(std::size_t i=0;i<actors.size();++i)for(std::size_t j=0;j<i;++j)if(actors[i].current.actor==actors[j].current.actor)return false;
    return true;
}
} // namespace wb::capital::encounter_support
