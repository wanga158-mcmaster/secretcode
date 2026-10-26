// Actual PersonalInformation::observe_group and SDK camp identities. The 8x8
// placement, host replies, fuel cutoffs and epoch transitions are explicit models.
#if defined(__wasm__)
#undef __wasm__
#include <cambc.h>
#define __wasm__ 1
#else
#include <cambc.h>
#endif
#include "../common/model.h"
#include "../common/information.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>
#include "backpressure-config-v1.h"
namespace {
void identity_query(unsigned,unsigned);
std::int64_t identity_budget();
std::uint64_t bp_camp_mask(){return 0;}
bool bp_hidden_collision=false;
int cost_zero_item=-1;
}
#include "backpressure-host-v1.h"

namespace {
using plan3_runtime::GroupDesign;
using plan3_runtime::GroupObservation;
using plan3_runtime::PersonalInformation;
constexpr unsigned member_count=30;
std::array<unsigned,65> identity_states{};
std::array<float,65> identity_health{},identity_strength{};
std::vector<std::pair<unsigned,unsigned>> identity_live;
bool identity_recording=false;
unsigned identity_drop_after=0,identity_epoch_after=0,identity_drop_fuel_at=0;
int identity_state_error=-1,identity_info_error=-1,identity_code=0;
unsigned identity_cases=0,identity_boundary_cases=0;

void identity_query(unsigned kind,unsigned id) {
    if(!identity_recording)return;
    identity_live.emplace_back(kind,id);
    if(identity_epoch_after && identity_live.size()==identity_epoch_after)++current_round;
}
std::int64_t identity_budget() {
    if(identity_recording && ((identity_drop_after && identity_live.size()>=identity_drop_after) ||
        (identity_drop_fuel_at && fuel_reads>=identity_drop_fuel_at)))return 29999;
    return fuel;
}
cambc::vec2 identity_spawn(unsigned row) {
    const unsigned at=row%member_count;
    return {bp_axis(int(at%8)),bp_axis(int(at/8))};
}
cambc::camp identity_camp(unsigned id,unsigned row) {
    return cambc::detail::handle_access::make_camp(static_cast<cambc::handle_camp_t>(id),
        static_cast<cambc::handle_camp_group_t>(0),cambc::camp_type::wolf,identity_spawn(row));
}
GroupObservation identity_old() {
    GroupObservation o;o.lifecycle=Lifecycle::respawning;o.round=17;o.respawn_remaining=11;
    o.remaining_health=123.5;o.incoming=7.25;o.all_members_visible=false;return o;
}
bool identity_equal(const GroupObservation& a,const GroupObservation& b) {
    return a.lifecycle==b.lifecycle && a.round==b.round && a.respawn_remaining==b.respawn_remaining &&
        a.remaining_health==b.remaining_health && a.incoming==b.incoming && a.all_members_visible==b.all_members_visible;
}
struct World {
    PersonalInformation p;
    std::vector<cambc::camp> visible_camps;
    unsigned index=0;
    explicit World(unsigned scenario) {
        reset();current_round=32;fuel=100000;group_state={1,1,1};
        identity_recording=false;identity_live.clear();identity_drop_after=identity_epoch_after=identity_drop_fuel_at=0;
        identity_state_error=identity_info_error=-1;identity_code=0;
        for(unsigned id=0;id<65;++id) {
            identity_states[id]=id%7?1+id%2:0;
            identity_health[id]=100.f+float(id)-.25f*float(id+1);
            identity_strength[id]=10.f+.5f*float(id)+.25f;
        }
        std::vector<cambc::camp> stored;
        for(unsigned row=0;row<member_count;++row) {
            if(!(scenario==2 && row==7))stored.push_back(identity_camp(scenario==3 && row==2?1:row,row));
            if(!(scenario==4 && row==1) && !(scenario==5 && row==0) && !(scenario==6 && row==1) && scenario!=19)
                visible_camps.push_back(identity_camp(scenario==20 && row==1?31:row,row));
        }
        if(scenario==1)stored.push_back(identity_camp(30,0));
        if(scenario==27)stored.push_back(identity_camp(0,0));
        if(scenario==18)for(unsigned id=30;id<65;++id)stored.push_back(identity_camp(id,id%member_count));
        if(scenario==21)visible_camps.push_back(identity_camp(1,1));
        auto group=cambc::detail::handle_access::make_camp_group(static_cast<cambc::handle_camp_group_t>(0),
            30,22,60,std::move(stored));
        GroupDesign design{std::move(group),cambc::bitboard{},identity_spawn(0),0,-1,0,0,false,22,30,60,{},{}};
        for(unsigned row=0;row<member_count;++row) {
            const auto spawn=identity_spawn(row);design.spawns.set(spawn);design.spawn_positions.push_back(spawn);
            design.members.push_back({spawn,100.+row,10.+.5*row});
            design.full_health+=100.+row;design.incoming+=10.+.5*row;
        }
        p.groups.push_back(std::move(design));p.observations.resize(1);p.observation_round=32;
        if(scenario==11 || scenario==12) {
            group_state[0]=scenario==11?2:3;identity_states.fill(0);
        }
    }
    void warm() {
        verify(p.observe_group(index,visible_camps,cambc::vision::visible_tiles(),30000));
    }
    void begin(unsigned scenario) {
        p.observations[index]=identity_old();query_trace.clear();fuel_trace.clear();fuel_reads=0;
        identity_live.clear();identity_recording=true;
        if(scenario==6)set(visible.data(),identity_spawn(1).x,identity_spawn(1).y,false);
        if(scenario==7 || scenario==8){identity_state_error=scenario==7?2:3;identity_code=scenario==7?97:98;}
        if(scenario==9 || scenario==10){identity_info_error=scenario==9?4:5;identity_code=scenario==9?97:98;}
        if(scenario==13)identity_drop_after=5;
        if(scenario==14)identity_epoch_after=4;
        if(scenario==15)identity_epoch_after=5;
        if(scenario==16)p.observation_round=31;
        if(scenario==17)visible.fill(0);
        if(scenario==22 || scenario==23) {
            std::vector<cambc::camp> replacement;
            for(unsigned row=0;row<member_count;++row)replacement.push_back(identity_camp(scenario==23 && row==2?32:row,row));
            if(scenario==22)std::reverse(replacement.begin(),replacement.end());
            const auto replacement_group=cambc::detail::handle_access::make_camp_group(
                static_cast<cambc::handle_camp_group_t>(0),30,22,60,std::move(replacement));
            const auto* prior_storage=p.groups[index].group.camps().data();
            // Copy assignment can reuse the owned vector's existing storage;
            // pointer/size/group identity alone must not validate the index.
            p.groups[index].group=replacement_group;
            verify(p.groups[index].group.camps().data()==prior_storage);
        }
        if(scenario==24)std::swap(p.groups[index].members[1],p.groups[index].members[2]);
        if(scenario==25)p.groups[index].members.erase(p.groups[index].members.begin()+7);
        if(scenario==28)p.groups[index].members.clear();
        if(scenario==29) {
            // The warmed immutable join must retain no live scalar or state.
            // Revive0, kill1, and change another living member's exact reply.
            identity_states[0]=2;identity_health[0]=42.25f;identity_strength[0]=3.75f;
            identity_states[1]=0;identity_health[1]=identity_strength[1]=0;
            identity_health[2]=88.5f;identity_strength[2]=99.25f;
        }
        if(scenario==26) {
            for(unsigned i=1;i<65;++i)p.groups.push_back(p.groups[0]);
            p.observations.resize(65);index=64;p.observations[index]=identity_old();
        }
    }
    bool read() {
        cambc::bitboard mask;
        mask.import_raw_buffer({reinterpret_cast<const std::uint8_t*>(visible.data()),sizeof(visible)});
        return p.observe_group(index,visible_camps,mask,30000);
    }
};

struct Expected {
    bool accepted=true;
    GroupObservation observation;
    std::vector<std::pair<unsigned,unsigned>> live;
};
Expected expected(unsigned scenario) {
    Expected e;
    if(scenario==16 || scenario==17){e.accepted=false;e.observation=identity_old();return e;}
    auto& o=e.observation;o.round=32;
    o.lifecycle=scenario==11?Lifecycle::reward:scenario==12?Lifecycle::respawning:Lifecycle::alive;
    o.respawn_remaining=scenario==12?60:0;o.all_members_visible=scenario!=6;
    if(*o.lifecycle==Lifecycle::alive)for(unsigned row=0;row<member_count;++row) {
        o.remaining_health+=100.+row;o.incoming+=10.+.5*row;
    }
    e.live={{4,0},{0,0}};if(scenario==12)e.live.emplace_back(3,0);
    std::vector<unsigned> rows;for(unsigned row=0;row<member_count;++row)if(scenario!=28 && !(scenario==25 && row==7))rows.push_back(row);
    if(scenario==24)std::swap(rows[1],rows[2]);
    for(const unsigned row:rows) {
        if(((scenario==1 || scenario==27) && row==0) || (scenario==2 && row==7) || scenario==18) {o.all_members_visible=false;continue;}
        if(scenario==6 && row==1)continue;
        if(scenario==13 && row>=2){o.all_members_visible=false;break;}
        const unsigned native=scenario==3 && row==2?1:scenario==23 && row==2?32:row;
        e.live.emplace_back(1,native);
        if(scenario==14 && row==1){e.accepted=false;break;}
        if((scenario==7 && row==2) || (scenario==8 && row==3)){o.all_members_visible=false;continue;}
        if(!identity_states[native]) {
            if(*o.lifecycle==Lifecycle::alive){o.remaining_health-=100.+row;o.incoming-=10.+.5*row;}
            continue;
        }
        if((scenario==3 && row==2) || ((scenario==4 || scenario==20) && row==1) || scenario==19 || (scenario==23 && row==2)) {
            o.all_members_visible=false;continue;
        }
        e.live.emplace_back(2,native);
        if(scenario==15 && row==1){e.accepted=false;break;}
        if((scenario==9 && row==4) || (scenario==10 && row==5)){o.all_members_visible=false;continue;}
        if(*o.lifecycle==Lifecycle::alive){o.remaining_health-=100.+row;o.incoming-=10.+.5*row;}
        o.remaining_health+=identity_health[native];o.incoming+=identity_strength[native];
    }
    if(!e.accepted)e.observation=identity_old();
    return e;
}
void emit(unsigned scenario,bool accepted,const GroupObservation& o) {
    std::printf("IDENTITY_JOIN_CALLER {\"side\":%u,\"scenario\":%u,\"accepted\":%s,\"lifecycle\":%d,\"round\":%u,\"timer\":%d,\"health\":%.17g,\"incoming\":%.17g,\"allVisible\":%s,\"live\":[",
        bp_side(),scenario,accepted?"true":"false",o.lifecycle?int(*o.lifecycle):-1,o.round,o.respawn_remaining,
        o.remaining_health,o.incoming,o.all_members_visible?"true":"false");
    for(unsigned i=0;i<identity_live.size();++i)std::printf("%s[%u,%u]",i?",":"",identity_live[i].first,identity_live[i].second);
    std::puts("],\"nativeGames\":0}");
}
void normal(unsigned scenario) {
    World world(scenario);world.warm();world.begin(scenario);const bool accepted=world.read();
    const auto e=expected(scenario);
    verify(accepted==e.accepted && identity_equal(world.p.observations[world.index],e.observation));
    verify(identity_live==e.live);
    emit(scenario,accepted,world.p.observations[world.index]);++identity_cases;
}
void budget_recovery() {
    World world(0);world.begin(0);identity_drop_fuel_at=25;
    const bool partial=world.read();
    if(partial)verify(!world.p.observations[0].all_members_visible);
    else verify(identity_equal(world.p.observations[0],identity_old()));
#if PLAN3_OBSERVATION_IDENTITY_JOIN
    verify(world.p.identity_joins_.empty()); // A partial index is never published.
#endif
    identity_drop_fuel_at=0;identity_recording=false;fuel=100000;
    world.warm();world.begin(0);verify(world.read());const auto e=expected(0);
    verify(identity_equal(world.p.observations[0],e.observation) && identity_live==e.live);
    ++identity_boundary_cases;
}
void cache_limits() {
#if PLAN3_OBSERVATION_IDENTITY_JOIN
    {
        World world(18);verify(world.p.groups[0].group.camps().size()==65);
        verify(!world.p.prepare_identity_join(0,30000) && world.p.identity_joins_.empty());
    }
    {
        World world(0);world.begin(26);identity_recording=false;
        for(unsigned i=0;i<64;++i)verify(world.p.prepare_identity_join(i,30000));
        verify(world.p.identity_joins_.size()==64 && !world.p.prepare_identity_join(64,30000));
        identity_recording=true;identity_live.clear();verify(world.read());const auto e=expected(0);
        verify(identity_equal(world.p.observations[64],e.observation) && identity_live==e.live);
    }
#endif
    ++identity_boundary_cases;
}
}

namespace cambc::detail {extern "C" {
std::int32_t sdk_camp_state(std::uint32_t id,std::uint32_t* out) {
    record("sdk_camp_state");verify(id<65);*out=identity_states[id];identity_query(1,id);
    if(int(id)==identity_state_error)return identity_code;
    return 0;
}
std::int32_t sdk_camp(std::uint32_t id,camp_state* out) {
    record("sdk_camp");verify(id<65);*out={};out->health=identity_health[id];out->strength=identity_strength[id];
    const auto live=identity_spawn((id+1)%member_count);out->x=live.x;out->y=live.y;out->max_health=100.f+float(id);
    identity_query(2,id);if(int(id)==identity_info_error)return identity_code;
    if(!identity_states[id])return int(error_code::api_not_found);return 0;
}
std::int32_t sdk_camp_design(std::int32_t type,camp_design* out) {
    record("sdk_camp_design");verify(type>=0 && type<5);std::memcpy(out,bp_config::camps[unsigned(type)].data(),sizeof(*out));return 0;
}
}}

int main() {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    if(const char* error=std::getenv("P3_IDENTITY_JOIN_ERROR")) {
        World world(0);world.warm();world.begin(0);
        const bool info=error[0]=='i';identity_code=std::atoi(error+2);
        if(error[0]=='v'){identity_states[1]=3;identity_code=0;}
        else if(info)identity_info_error=1;else identity_state_error=1;
        std::fprintf(stderr,"IDENTITY_JOIN_EXPECTED_ERROR %s\n",error);world.read();verify(false);
    }
    for(unsigned scenario=0;scenario<30;++scenario)normal(scenario);
    budget_recovery();cache_limits();
    std::printf("IDENTITY_JOIN_RESULT {\"status\":\"pass\",\"side\":%u,\"arm\":%u,\"cases\":%u,\"boundaryCases\":%u,\"checks\":%u,\"nativeGames\":0}\n",
        bp_side(),unsigned(PLAN3_OBSERVATION_IDENTITY_JOIN),identity_cases,identity_boundary_cases,checks);
}
