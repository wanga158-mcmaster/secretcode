#pragma once
#include "information.h"
#include "two-trip-cycle.h"

namespace plan3_two_trip::observation {
enum class State { complete, unknown, cutoff, invalid, unavailable };
enum class Reason { none, metadata, identity, numeric, visibility, round, lifecycle, missing_runtime };
struct Snapshot {
    unsigned round=0,memberCount=0;
    int groupKey=-1,groupGems=0;
    std::uint32_t nativeGroup=0;
    std::array<Member,maximum_members> members{};
    std::array<std::uint32_t,maximum_members> nativeMemberIds{},memberStates{};
    // No observed epoch identifier is available. Two separated alive reads do
    // not establish future continuity, exclusive capture, recovery or survival.
    bool futureContinuityKnown=false;
};
struct Result {
    State state=State::unknown;
    Reason reason=Reason::visibility;
    std::optional<Snapshot> snapshot;
};
inline Result stop(State state,Reason reason) {return {state,reason,std::nullopt};}
inline std::optional<Result> status(std::int32_t code) {
    if(!code)return std::nullopt;
    if(code==static_cast<std::int32_t>(cambc::error_code::api_not_in_vision))
        return stop(State::unknown,Reason::visibility);
    if(code==static_cast<std::int32_t>(cambc::error_code::api_not_found))
        return stop(State::unavailable,Reason::missing_runtime);
    cambc::detail::check(code); // Unexpected engine errors reach the turn boundary.
    return std::nullopt;
}
inline bool close(double a,double b) {
    return std::abs(a-b)<=1e-9*std::max({1.0,std::abs(a),std::abs(b)});
}
// No all-group scan or remembered observation is used. Raw status probes mirror
// pinned camp::state()/info() and camp_group::state(); they expose only the two
// expected unavailable cases without claiming WASI exception-unwind coverage.
// Ready is borrowed throughout, including the final group/round fence.
template<class Ready>
Result read(const plan3_runtime::GroupDesign& design,unsigned expectedRound,Ready&& ready) {
    if(!ready())return stop(State::cutoff,Reason::none);
    const int width=cambc::map::width(),height=cambc::map::height();
    const auto camps=design.group.camps();const auto count=camps.size();
    if(width<1 || height<1 || std::int64_t(width)*height>INT_MAX || !count || count>maximum_members ||
       design.members.size()!=count || design.spawn_positions.size()!=count || expectedRound>INT_MAX)
        return stop(State::invalid,Reason::metadata);
    const auto tile=[&](cambc::vec2 p) {return p.x>=0 && p.x<width && p.y>=0 && p.y<height;};
    if(!scalar(design.full_health) || !scalar(design.incoming) || design.max_respawn_timer<0 ||
       design.gem_value<0 || design.gold_value<0 || design.group.gem_value()>INT_MAX ||
       design.group.gold_value()>INT_MAX || unsigned(design.gem_value)!=design.group.gem_value() ||
       unsigned(design.gold_value)!=design.group.gold_value() ||
       design.max_respawn_timer!=design.group.max_respawn_timer())return stop(State::invalid,Reason::metadata);
    Snapshot out;out.round=expectedRound;out.memberCount=unsigned(count);
    out.nativeGroup=static_cast<std::uint32_t>(static_cast<cambc::handle_camp_group_t>(design.group));
    out.groupGems=design.gem_value;
    double healthSum=0,incomingSum=0;int minimum=-1;
    for(unsigned i=0;i<count;++i) {
        if(!ready())return stop(State::cutoff,Reason::none);
        const auto p=camps[i].spawn_position();
        const auto kind=static_cast<std::int32_t>(camps[i].kind());
        if(!tile(p) || kind<static_cast<std::int32_t>(cambc::camp_type::wolf) ||
           kind>static_cast<std::int32_t>(cambc::camp_type::monolith))return stop(State::invalid,Reason::identity);
        const int key=p.y*width+p.x;
        const auto id=static_cast<std::uint32_t>(static_cast<cambc::handle_camp_t>(camps[i]));
        for(unsigned j=0;j<i;++j)if(out.nativeMemberIds[j]==id || out.members[j].key==key)
            return stop(State::invalid,Reason::identity);
        unsigned memberMatches=0,spawnMatches=0;
        for(unsigned j=0;j<count;++j) {
            if(j%8==0 && !ready())return stop(State::cutoff,Reason::none);
            const auto& m=design.members[j];
            if(!tile(m.spawn) || !scalar(m.health) || m.health<=0 || !scalar(m.strength))
                return stop(State::invalid,Reason::metadata);
            if(m.spawn==p)++memberMatches;
            if(design.spawn_positions[j]==p)++spawnMatches;
        }
        if(memberMatches!=1 || spawnMatches!=1)return stop(State::invalid,Reason::identity);
        const auto& publicMember=design.members[i];
        healthSum+=publicMember.health;incomingSum+=publicMember.strength;
        out.nativeMemberIds[i]=id;out.members[i].key=key;
        if(minimum<0 || key<minimum)minimum=key;
    }
    if(!scalar(healthSum) || !scalar(incomingSum) || !close(healthSum,design.full_health) ||
       !close(incomingSum,design.incoming) || minimum!=design.key || !tile(design.anchor) ||
       design.anchor.y*width+design.anchor.x!=minimum)return stop(State::invalid,Reason::metadata);
    out.groupKey=minimum;
    if(!ready())return stop(State::cutoff,Reason::none);
    if(cambc::round_number()!=expectedRound)return stop(State::unknown,Reason::round);
    if(!ready())return stop(State::cutoff,Reason::none);
    const auto visible=cambc::vision::visible_tiles();
    for(unsigned i=0;i<count;++i) {
        if(!ready())return stop(State::cutoff,Reason::none);
        // Public-spawn visibility authorizes a group probe, but does NOT prove
        // an individual camp's current/last occupied tile is queryable. The
        // subsequent state probe still handles api_not_in_vision explicitly.
        if(!visible.is_set(camps[i].spawn_position()))return stop(State::unknown,Reason::visibility);
        if(!design.spawns.is_set(camps[i].spawn_position()))return stop(State::invalid,Reason::metadata);
    }
    if(!ready())return stop(State::cutoff,Reason::none);
    if(design.spawns.count()!=count)return stop(State::invalid,Reason::metadata);
    if(!ready())return stop(State::cutoff,Reason::none);
    std::uint32_t actualCount{};
    if(const auto failure=status(cambc::detail::sdk_map_camp_group_num_camps(out.nativeGroup,&actualCount)))return *failure;
    if(actualCount!=count || actualCount>maximum_members)return stop(State::invalid,Reason::metadata);
    // Verify immutable handle/group/spawn joins without camp::camp_group(),
    // which would reload and allocate another arbitrary-sized group object.
    for(unsigned i=0;i<count;++i) {
        if(!ready())return stop(State::cutoff,Reason::none);
        cambc::detail::camp_info info{};
        if(const auto failure=status(cambc::detail::sdk_map_camp_info(out.nativeMemberIds[i],&info)))return *failure;
        const auto p=camps[i].spawn_position();
        if(info.x!=p.x || info.y!=p.y || info.type!=static_cast<std::int32_t>(camps[i].kind()))
            return stop(State::invalid,Reason::identity);
        if(!ready())return stop(State::cutoff,Reason::none);
        std::uint32_t group{};
        if(const auto failure=status(cambc::detail::sdk_map_camp_group_of(out.nativeMemberIds[i],&group)))return *failure;
        if(group!=out.nativeGroup)return stop(State::invalid,Reason::identity);
    }
    if(!ready())return stop(State::cutoff,Reason::none);
    if(cambc::round_number()!=expectedRound)return stop(State::unknown,Reason::round);
    if(!ready())return stop(State::cutoff,Reason::none);
    std::uint32_t lifecycle{};
    if(const auto failure=status(cambc::detail::sdk_camp_group_state(out.nativeGroup,&lifecycle)))return *failure;
    if(lifecycle>3)throw cambc::error{cambc::error_code::api_value_out_of_range,"unknown engine camp group state"};
    if(!lifecycle)return stop(State::unknown,Reason::lifecycle);
    if(lifecycle!=1)return stop(State::unavailable,Reason::lifecycle);
    unsigned living=0;
    for(unsigned i=0;i<count;++i) {
        if(!ready())return stop(State::cutoff,Reason::none);
        std::uint32_t state{};
        if(const auto failure=status(cambc::detail::sdk_camp_state(out.nativeMemberIds[i],&state)))return *failure;
        if(state>2)throw cambc::error{cambc::error_code::api_value_out_of_range,"unknown engine camp state"};
        out.memberStates[i]=state;auto& member=out.members[i];member.observedRound=int(expectedRound);
        if(!state) {
            member.state=MemberState::dead;member.health=member.incoming=0;
            continue; // Dead members need no live info (which may be not_found).
        }
        if(!ready())return stop(State::cutoff,Reason::none);
        cambc::detail::camp_state info{};
        if(const auto failure=status(cambc::detail::sdk_camp(out.nativeMemberIds[i],&info)))return *failure;
        const auto p=camps[i].spawn_position();
        if(info.x!=p.x || info.y!=p.y)return stop(State::invalid,Reason::identity);
        if(!scalar(info.health) || info.health<=0 || !scalar(info.max_health) || info.max_health<=0 ||
           info.health>info.max_health || !scalar(info.strength) || !scalar(info.focus))
            return stop(State::invalid,Reason::numeric);
        member.state=MemberState::alive;member.health=info.health;member.incoming=info.strength;++living;
    }
    if(!living)return stop(State::invalid,Reason::lifecycle);
    if(!ready())return stop(State::cutoff,Reason::none);
    if(cambc::round_number()!=expectedRound)return stop(State::unknown,Reason::round);
    if(!ready())return stop(State::cutoff,Reason::none);
    std::uint32_t after{};
    if(const auto failure=status(cambc::detail::sdk_camp_group_state(out.nativeGroup,&after)))return *failure;
    if(after>3)throw cambc::error{cambc::error_code::api_value_out_of_range,"unknown engine camp group state"};
    if(after!=lifecycle)return stop(State::unavailable,Reason::lifecycle);
    // Last read: suspension inside the end group query cannot pass a round
    // sampled BEFORE that query. No history/epoch claim follows from equality.
    if(!ready())return stop(State::cutoff,Reason::none);
    if(cambc::round_number()!=expectedRound)return stop(State::unknown,Reason::round);
    return {State::complete,Reason::none,std::move(out)};
}
} // namespace plan3_two_trip::observation
