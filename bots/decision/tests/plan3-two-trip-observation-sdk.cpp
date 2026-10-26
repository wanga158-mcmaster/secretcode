// Actual pinned SDK classes and public records; live member states and 8x8
// placements are explicit host assumptions, not a native visibility witness.
#if defined(__wasm__)
#undef __wasm__
#include <cambc.h>
#define __wasm__ 1
#else
#include <cambc.h>
#endif
#include "../common/two-trip-observation.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include "backpressure-config-v1.h"
namespace {
bool all_public_spawns_visible=true;
unsigned group_state_calls=0;
void maybe_change_round();
}
#include "backpressure-host-v1.h"
namespace {
std::array<unsigned,9> member_states{};
std::array<float,9> member_health{},member_max_health{},member_strength{},member_focus{};
std::vector<unsigned> member_state_calls,member_info_calls;
int hidden_state_id=-1,missing_state_id=-1,hidden_info_id=-1,missing_info_id=-1;
int public_mismatch_id=-1,public_group_mismatch_id=-1,round_change_public_id=-1;
unsigned native_group_count=3;
unsigned events=0,change_round_after=0,flip_group_after=0;
bool tile_visible(unsigned id);
void maybe_change_round(){++events;if(events==change_round_after)++current_round;if(events==flip_group_after)group_state[0]=2;}
}
#include "two-trip-observation-host.h"
namespace cambc::detail { extern "C" {
std::int32_t sdk_map_camp_group_num_camps(std::uint32_t group,std::uint32_t* out){
    record("sdk_map_camp_group_num_camps");verify(group<3);*out=native_group_count;return 0;
}
}}

namespace {
namespace observation=plan3_two_trip::observation;
using State=observation::State;
unsigned scenarios=0,cutoff_cases=0,complete_cases=0;
unsigned query_id(const char* name){unsigned hash=2166136261u;for(;*name;++name)hash=(hash^unsigned(*name))*16777619u;return hash;}

bool tile_visible(unsigned id){return (visible[0]>>(bp_axis(bp_y[id])*8+bp_axis(bp_x[id])))&1u;}
void reset_case(){
    reset();current_round=32;group_state={1,1,1};
    all_public_spawns_visible=true;hidden_state_id=missing_state_id=hidden_info_id=missing_info_id=-1;
    public_mismatch_id=public_group_mismatch_id=round_change_public_id=-1;native_group_count=3;
    group_state_calls=events=change_round_after=flip_group_after=0;
    for(unsigned id=0;id<9;++id){
        cambc::detail::camp_design design{};
        std::memcpy(&design,bp_config::camps[unsigned(bp_type[id])].data(),sizeof(design));
        member_states[id]=id%2?1:2;
        member_health[id]=member_max_health[id]=design.base_stats.base_health;
        member_strength[id]=design.base_stats.base_strength;member_focus[id]=design.base_stats.base_focus;
    }
    member_state_calls.clear();member_info_calls.clear();query_trace.clear();++scenarios;
}
plan3_runtime::GroupDesign design(){
    plan3_runtime::PersonalInformation p;
    unsigned calls=0;while(!p.initialize_step())verify(++calls<128);
    verify(p.initialization_complete && p.groups.size()==3);
    for(auto& g:p.groups)if(static_cast<unsigned>(static_cast<cambc::handle_camp_group_t>(g.group))==0)return std::move(g);
    std::abort();
}
void clear_queries(){member_state_calls.clear();member_info_calls.clear();group_state_calls=events=0;query_trace.clear();}
void no_live_reads(){verify(member_state_calls.empty() && member_info_calls.empty() && group_state_calls==0);}
struct Ready {
    unsigned calls=0,stop=0;
    explicit Ready(unsigned limit=0):stop(limit){}
    Ready(const Ready&)=delete;
    bool operator()(){++calls;return !stop || calls<stop;}
};
observation::Result read(const plan3_runtime::GroupDesign& g,unsigned round=32){
    Ready ready;return observation::read(g,round,ready);
}
void failure(const observation::Result& r){verify(r.state!=State::complete && !r.snapshot);}

void current_members(){
    reset_case();auto g=design();member_health[1]/=2;clear_queries();const auto r=read(g);
    verify(r.state==State::complete && r.snapshot);const auto& s=*r.snapshot;
    verify(s.round==32 && s.groupKey==g.key && s.groupKey==bp_key(0) && s.groupGems==g.gem_value && s.nativeGroup==0 && s.memberCount==3);
    verify(!s.futureContinuityKnown && member_state_calls.size()==3 && member_info_calls.size()==3 && group_state_calls==2);
    const auto position=[](const char* name){const auto found=std::find(query_trace.begin(),query_trace.end(),query_id(name));verify(found!=query_trace.end());return found-query_trace.begin();};
    verify(position("sdk_vision")<position("sdk_map_camp_info") && position("sdk_map_camp_group_of")<position("sdk_camp_group_state") &&
        position("sdk_camp_group_state")<position("sdk_camp_state") && position("sdk_camp_state")<position("sdk_camp"));
    verify(std::count(query_trace.begin(),query_trace.begin()+position("sdk_map_camp_info"),query_id("sdk_Bitboard_Test"))>=6);
    for(unsigned at=0;at<s.memberCount;++at){
        const auto id=s.nativeMemberIds[at];verify(id<3);
        verify(s.members[at].key==bp_axis(bp_y[id])*8+bp_axis(bp_x[id]));
        verify(s.members[at].observedRound==32 && s.members[at].state==plan3_two_trip::MemberState::alive);
        verify(s.members[at].health==member_health[id] && s.members[at].incoming==member_strength[id]);
        verify(s.memberStates[at]==member_states[id]);
    }
    plan3_two_trip::Input in;in.round=32;in.observationRound=int(s.round);in.groupKey=s.groupKey;
    in.groupGems=s.groupGems;in.groupGemsKnown=true;in.memberCount=s.memberCount;in.members=s.members;
    verify(!in.actorKnown && !in.quiet.complete());for(const auto& leg:in.legs)verify(!leg.turns && !leg.supported);
    for(unsigned at=0;at<s.memberCount;++at)if(s.nativeMemberIds[at]==0){const double captured=s.members[at].health;
        member_health[0]/=2;verify(s.members[at].health==captured && captured>member_health[0]);}
    ++complete_cases;
}
void dead_minimum_identity(){
    reset_case();auto g=design();unsigned dead=0;
    for(unsigned id=0;id<3;++id)if(bp_axis(bp_y[id])*8+bp_axis(bp_x[id])==g.key)dead=id;
    member_states[dead]=0;member_health[dead]=std::numeric_limits<float>::quiet_NaN();clear_queries();
    const auto r=read(g);verify(r.state==State::complete && r.snapshot && r.snapshot->groupKey==g.key);
    bool found=false;int living_min=64;
    for(unsigned at=0;at<r.snapshot->memberCount;++at){const auto& m=r.snapshot->members[at];
        if(r.snapshot->nativeMemberIds[at]==dead){found=true;verify(m.state==plan3_two_trip::MemberState::dead && m.health==0 && m.incoming==0);}
        else living_min=std::min(living_min,m.key);
    }
    verify(found && living_min>g.key && member_info_calls.size()==2 && member_state_calls.size()==3);
    verify(std::find(member_info_calls.begin(),member_info_calls.end(),dead)==member_info_calls.end());++complete_cases;
    reset_case();g=design();member_states[0]=member_states[1]=member_states[2]=0;clear_queries();
    const auto all_dead=read(g);verify(all_dead.state==State::invalid && !all_dead.snapshot && member_info_calls.empty());
}
void visibility_and_lifecycle(){
    for(unsigned mode=0;mode<4;++mode){reset_case();auto g=design();clear_queries();
        visible.fill(0);if(mode==1)set(visible.data(),bp_axis(bp_x[0]),bp_axis(bp_y[0]));
        if(mode>=2){visible.fill(~std::uint64_t{0});group_state[0]=mode==2?2:3;}
        all_public_spawns_visible=mode>=2;const auto r=read(g);failure(r);
        if(mode<2){verify(r.state==State::unknown);no_live_reads();}
        else verify(member_state_calls.empty() && member_info_calls.empty());
    }
    reset_case();auto g=design();clear_queries();failure(read(g,31));no_live_reads();
    failure(read(g,std::numeric_limits<unsigned>::max()));no_live_reads();
}
void availability_and_epoch(){
    for(unsigned mode=0;mode<8;++mode){reset_case();auto g=design();clear_queries();
        if(mode==0)hidden_state_id=1;if(mode==1)missing_state_id=1;
        if(mode==2)hidden_info_id=1;if(mode==3)missing_info_id=1;
        if(mode==4)change_round_after=3;if(mode==5)flip_group_after=3;
        if(mode==6)round_change_public_id=1;if(mode==7)change_round_after=8;
        const auto r=read(g);failure(r);
        if(mode==0 || mode==2 || mode==4 || mode==6 || mode==7)verify(r.state==State::unknown);
        if(mode==1 || mode==3)verify(r.state==State::unavailable);
        if(mode==6)no_live_reads();
    }
}
void malformed_metadata(){
    for(unsigned mode=0;mode<11;++mode){reset_case();auto g=design();
        if(mode==0)++g.key;if(mode==1)g.gem_value=-1;if(mode==2)g.spawn_positions.pop_back();
        if(mode==3)g.members.pop_back();if(mode==4)public_mismatch_id=1;if(mode==5)public_group_mismatch_id=1;
        if(mode==6)g.spawns.set({bp_axis(0),bp_axis(0)});
        if(mode==7)g.group=cambc::detail::handle_access::make_camp_group(cambc::handle_camp_group_t{0},270,
            std::numeric_limits<std::uint32_t>::max(),40,std::vector<cambc::camp>(g.group.begin(),g.group.end()));
        if(mode==8)g.members[0].health=std::numeric_limits<double>::quiet_NaN();
        if(mode==9)native_group_count=2;
        if(mode==10){std::vector<cambc::camp> members(g.group.begin(),g.group.end());
            members[1]=cambc::detail::handle_access::make_camp(cambc::handle_camp_t{1},cambc::handle_camp_group_t{0},
                static_cast<cambc::camp_type>(5),members[1].spawn_position());
            g.group=cambc::detail::handle_access::make_camp_group(cambc::handle_camp_group_t{0},270,66,40,std::move(members));}
        clear_queries();failure(read(g));no_live_reads();
    }
    for(unsigned count:{0u,33u}){reset_case();auto g=design();std::vector<cambc::camp> members;
        for(unsigned id=0;id<count;++id)members.push_back(cambc::detail::handle_access::make_camp(
            cambc::handle_camp_t{id},cambc::handle_camp_group_t{0},cambc::camp_type::wolf,{int(id%8),int(id/8)}));
        g.group=cambc::detail::handle_access::make_camp_group(cambc::handle_camp_group_t{0},270,66,40,std::move(members));
        clear_queries();failure(read(g));no_live_reads();
    }
    for(bool same_spawn:{false,true}){reset_case();auto g=design();std::vector<cambc::camp> members(g.group.begin(),g.group.end());
        members[1]=same_spawn?cambc::detail::handle_access::make_camp(cambc::handle_camp_t{1},cambc::handle_camp_group_t{0},cambc::camp_type::wolf,members[0].spawn_position()):members[0];
        g.group=cambc::detail::handle_access::make_camp_group(cambc::handle_camp_group_t{0},270,66,40,std::move(members));
        clear_queries();failure(read(g));no_live_reads();
    }
}
void malformed_live_values(){
    for(unsigned mode=0;mode<7;++mode){reset_case();auto g=design();clear_queries();
        if(mode==0)member_health[1]=std::numeric_limits<float>::quiet_NaN();
        if(mode==1)member_health[1]=std::numeric_limits<float>::infinity();
        if(mode==2)member_health[1]=-1;if(mode==3)member_health[1]=0;
        if(mode==4)member_health[1]=member_max_health[1]+1;
        if(mode==5)member_strength[1]=-1;if(mode==6)member_strength[1]=std::numeric_limits<float>::infinity();
        failure(read(g));
    }
}
void cutoff_atomic(){
    reset_case();auto g=design();clear_queries();Ready baseline;
    const auto full=observation::read(g,32,baseline);verify(full.snapshot && baseline.calls>0);
    for(unsigned stop=1;stop<=baseline.calls;++stop){reset_case();clear_queries();Ready ready(stop);
        const auto r=observation::read(g,32,ready);verify(r.state==State::cutoff && !r.snapshot);++cutoff_cases;
    }
    reset_case();clear_queries();Ready final(baseline.calls+1);
    verify(observation::read(g,32,final).state==State::complete);
}
}

int main(){
    verify(cambc::detail::absolute_team(cambc::team::ally)==int(bp_side()));
    current_members();dead_minimum_identity();visibility_and_lifecycle();availability_and_epoch();
    malformed_metadata();malformed_live_values();cutoff_atomic();
    std::printf("TWO_TRIP_OBSERVATION_RESULT {\"status\":\"pass\",\"side\":%u,\"checks\":%u,\"scenarios\":%u,\"completeCases\":%u,\"cutoffCases\":%u,\"engineCalls\":0}\n",
        bp_side(),checks,scenarios,complete_cases,cutoff_cases);
}
