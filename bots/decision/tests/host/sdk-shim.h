// Base SDK host for the decision fixtures: an explicit 8x8 world that defines every SDK import
// as an extern "C" function (the fixture includes cambc.h with __wasm__ undefined).
// Not compiled directly: tests/hosts.py takes the text from the first extern "C" line and
// applies count-checked seam replacements per fixture family. Folded from the plan3
// champion-design-reuse fixture (v17 host); edit the seams in hosts.py together with this text.
extern "C" void* __cxa_allocate_exception(std::size_t size) { return std::malloc(size); }
extern "C" void __cxa_throw(void* error,void*,void(*)(void*)) {
    std::fprintf(stderr,"Unexpected SDK fixture exception: %s\n",static_cast<std::exception*>(error)->what());
    std::abort();
}
namespace {
std::vector<unsigned> query_trace;std::vector<std::int64_t> fuel_trace;
void record(const char* name) {unsigned h=2166136261u;for(;*name;++name)h=(h^unsigned(*name))*16777619u;query_trace.push_back(h);}
constexpr unsigned side=8,words=1;
int forced_champion=-1;unsigned metadata_removed=0,metadata_pairs=0,constructor_cases=0,dynamic_cases=0,nonopt_cases=0;
bool allow_elision=true;unsigned identity_reads=0,fail_identity_read=0;
unsigned checks=0,cases=0,current_round=1,bag=0,own_score=100,moves=0,gold=0,spawn_edge=1;
std::array<unsigned,static_cast<unsigned>(cambc::item_type::count)> items{};
std::int64_t fuel=1000000;
bool show_enemy=false,reveal_after_move=false;
bool meter_fuel_step=false;
bool admit_one_fuel_query=false;
unsigned design_reads=0,cutoff_design_read=0,fuel_reads=0,cutoff_fuel_read=0;
std::array<std::uint64_t,words> visible{};
std::array<unsigned,2> group_state{2,2};
cambc::detail::champion_info actor{},enemy{};
using Lifecycle=plan3_runtime::Lifecycle;
using JobKind=plan3::JobKind;
void verify_impl(bool value,unsigned line) {
    ++checks;if(!value) { std::fprintf(stderr,"Capital fixture failure: case=%u round=%u line=%u check=%u\n",cases,current_round,line,checks);std::abort(); }
}
#define verify(...) verify_impl((__VA_ARGS__),__LINE__)
void set(std::uint64_t* bits,int x,int y,bool value=true) {
    verify(x>=0 && x<int(side) && y>=0 && y<int(side));
    const unsigned at=unsigned(y)*side+unsigned(x);const auto mask=std::uint64_t{1}<<at;
    if(value)*bits|=mask;else *bits&=~mask;
}
void reset() {
    ++cases;design_reads=0;cutoff_design_read=0;fuel_reads=0;cutoff_fuel_read=0;current_round=1;bag=0;own_score=100;moves=0;fuel=1000000;
    gold=0;spawn_edge=1;items.fill(0);show_enemy=false;reveal_after_move=false;admit_one_fuel_query=false;visible.fill(~std::uint64_t{0});group_state={2,2};
    actor={0,int(cambc::champion_type::tavi),2,2,1000,1000,10,0,1,1,1|(1<<3)|(1<<4)|(1<<11)};
    if(forced_champion>=0)actor.type=forced_champion;
    enemy={1,int(cambc::champion_type::tavi),3,2,1000,1000,1,100,1,1,1};
}
}
namespace cambc::detail { extern "C" {
std::int32_t sdk_get_settings(game_settings* out) { record("sdk_get_settings");
    *out={};out->capture_time=5;out->respawn_time=60;out->recall_time=5;out->submit_gems_range=1;
    out->submit_gem_duration_multiplier=.1;out->submit_gem_minimum_duration=1;
    out->passive_gold=1;out->max_rounds=2000;out->winning_gems=1000;out->spawn_healing=.1;return 0;
}
std::int32_t sdk_bc_metadata(metadata* out) { record("sdk_bc_metadata"); *out={side,side,1,1,1,words*8,side*side*2};return 0; }
std::int32_t sdk_bc_get_last_error(char*,std::uint32_t) { record("sdk_bc_get_last_error"); return 0; }
std::int32_t sdk_shared_memory_size(std::uint32_t* out) { record("sdk_shared_memory_size"); *out=0;return 0; }
std::int32_t sdk_draft_memory_size(std::uint32_t* out) { record("sdk_draft_memory_size"); *out=0;return 0; }
std::int32_t sdk_my_champion() { record("sdk_my_champion"); return 0; }
std::int32_t sdk_my_team() { record("sdk_my_team"); return 0; }
std::int32_t sdk_consumable_info(std::uint32_t,consumable_info* out) { record("sdk_consumable_info"); *out={};return 0; }
std::int32_t sdk_champion_design(std::int32_t,champion_design* out) { record("sdk_champion_design");
    ++design_reads;if(cutoff_design_read && design_reads>=cutoff_design_read)fuel=44000;
    *out={};out->base_stats={8,1,1,1000,100,10,10,0,20,1};return 0;
}
std::int32_t sdk_champion_ability_props(std::uint32_t,std::uint32_t,ability_props* out) { record("sdk_champion_ability_props"); *out={};return 0; }
std::int32_t sdk_champion(std::uint32_t id,champion_info* out) { record("sdk_champion"); verify(id<2);*out=id?enemy:actor;return 0; }
std::int32_t sdk_champion_identity(std::uint32_t id,champion_identity* out) { record("sdk_champion_identity"); verify(id<2);++identity_reads;if(fail_identity_read&&identity_reads==fail_identity_read)return 1;const auto& c=id?enemy:actor;*out={c.team,c.type};return 0; }
std::int32_t sdk_champion_is_alive(std::uint32_t id,std::int32_t* out) { record("sdk_champion_is_alive"); verify(id<2);*out=(id?enemy:actor).state&1;return 0; }
std::int32_t sdk_champion_gems(std::uint32_t id,std::uint32_t* out) { record("sdk_champion_gems"); verify(id==0);*out=bag;return 0; }
std::int32_t sdk_champion_gold(std::uint32_t id,std::uint32_t* out) { record("sdk_champion_gold"); verify(id==0);*out=gold;return 0; }
std::int32_t sdk_champion_items(std::uint32_t id,std::uint32_t* out) { record("sdk_champion_items"); verify(id==0);std::copy(items.begin(),items.end(),out);return 0; }
std::int32_t sdk_item_info(std::uint32_t id,item_info* out) { record("sdk_item_info");
    verify(id<unsigned(cambc::item_type::count));*out={};
    const unsigned family=id/6,stat=(id/2)%3;const bool emblem=id%2;
    out->cost=emblem?150:30;out->purchasable=!emblem;
    out->combine_count=emblem?0:5;out->result=emblem?0:id+1;
    const int points=emblem?(family==1?6:5):1;
    if(stat==0)out->strength_points=points;
    if(stat==1)out->focus_points=points;
    if(stat==2)out->health_points=points;
    if(emblem && family==0){out->passive=unsigned(cambc::item_passive::monster);out->passive_magnitude=.1f;}
    if(emblem && family==4){out->passive=unsigned(cambc::item_passive::jungle);out->passive_magnitude=.3f;}
    return 0;
}
std::int32_t sdk_bc_submitted_gems(std::uint32_t team,std::uint32_t* out) { record("sdk_bc_submitted_gems"); verify(team<2);*out=team?200:own_score;return 0; }
std::int32_t sdk_bc_round_number(std::uint32_t* out) { record("sdk_bc_round_number"); *out=current_round;return 0; }
std::int64_t sdk_fuel_remaining() { record("sdk_fuel_remaining"); ++fuel_reads;if(std::getenv("P3_NEGATIVE_FUEL"))return -1;if(meter_fuel_step)fuel=100000-fuel_reads;if(cutoff_fuel_read && fuel_reads>=cutoff_fuel_read)fuel=44000; if(admit_one_fuel_query) { admit_one_fuel_query=false;return 45000; }fuel_trace.push_back(fuel);return fuel; }
std::int32_t sdk_bc_map_symmetry(std::int32_t* out) { record("sdk_bc_map_symmetry"); *out=0;return 0; }
std::int32_t sdk_bc_map_mask(std::int32_t,std::uint64_t* out) { record("sdk_bc_map_mask"); *out=0;return 0; }
std::int32_t sdk_spawn_region(std::int32_t team,aabb* out) { record("sdk_spawn_region"); *out=team?aabb{7,7,8,8}:aabb{0,0,int(spawn_edge),int(spawn_edge)};return 0; }
std::int32_t sdk_vision(std::uint64_t* out) { record("sdk_vision"); *out=visible[0];return 0; }
std::int32_t sdk_ally_occupancy(std::uint64_t* out) { record("sdk_ally_occupancy"); *out=0;set(out,actor.x,actor.y);return 0; }
std::int32_t sdk_enemy_occupancy(std::uint64_t* out) { record("sdk_enemy_occupancy"); *out=0;if(show_enemy)set(out,enemy.x,enemy.y);return 0; }
std::int32_t sdk_camp_occupancy(std::uint64_t* out) { record("sdk_camp_occupancy"); *out=0;return 0; }
std::int32_t sdk_teleport_occupancy(std::uint64_t* out) { record("sdk_teleport_occupancy"); *out=0;return 0; }
std::int32_t sdk_champion_at(std::int32_t x,std::int32_t y,std::uint32_t* out) { record("sdk_champion_at");
    verify((x==actor.x && y==actor.y)||(show_enemy && x==enemy.x && y==enemy.y));
    *out=(x==actor.x && y==actor.y)?0:1;return 0;
}
std::int32_t sdk_altar_locks(altar_lock_info*,std::uint32_t,std::uint32_t* count) { record("sdk_altar_locks"); *count=0;return 0; }
std::int32_t sdk_camp_group_state(std::uint32_t group,std::uint32_t* out) { record("sdk_camp_group_state"); verify(group<2);*out=group_state[group];return 0; }
std::int32_t sdk_camp_group_cooldown(std::uint32_t group,std::int32_t* out) { record("sdk_camp_group_cooldown"); verify(group<2 && group_state[group]==3);*out=60;return 0; }
std::int32_t sdk_move_to(std::int32_t x,std::int32_t y,std::int32_t* success) { record("sdk_move_to");
    verify((actor.state&(1<<3)) && std::max(std::abs(x-actor.x),std::abs(y-actor.y))==1);
    actor.x=x;actor.y=y;actor.state&=~(1<<3);++moves;*success=1;
    if(reveal_after_move)show_enemy=true;return 0;
}
std::int32_t sdk_Bitboard_Zero(std::uint64_t* out) { record("sdk_Bitboard_Zero"); *out=0;return 0; }
std::int32_t sdk_Bitboard_ImportRawBuffer(std::uint64_t* out,const std::uint8_t* bytes,std::size_t size) { record("sdk_Bitboard_ImportRawBuffer"); verify(size==8);std::memcpy(out,bytes,size);return 0; }
std::int32_t sdk_Bitboard_ExportRawBuffer(const std::uint64_t* in,std::uint8_t* bytes,std::size_t size) { record("sdk_Bitboard_ExportRawBuffer"); verify(size==8);std::memcpy(bytes,in,size);return 0; }
std::int32_t sdk_Bitboard_ExportLinearBuffer(const std::uint64_t* in,std::uint8_t* bytes,std::size_t size) { record("sdk_Bitboard_ExportLinearBuffer"); verify(size==8);std::memcpy(bytes,in,size);return 0; }
std::int32_t sdk_Bitboard_Set(std::uint64_t* out,std::int32_t x,std::int32_t y,std::int32_t value) { record("sdk_Bitboard_Set"); set(out,x,y,value);return 0; }
std::int32_t sdk_Bitboard_Test(const std::uint64_t* in,std::int32_t x,std::int32_t y,std::int32_t* out) { record("sdk_Bitboard_Test"); verify(x>=0&&x<8&&y>=0&&y<8);*out=(*in>>(y*8+x))&1u;return 0; }
std::int32_t sdk_Bitboard_Not(const std::uint64_t* in,std::uint64_t* out) { record("sdk_Bitboard_Not"); *out=~*in;return 0; }
std::int32_t sdk_Bitboard_Union(const std::uint64_t* a,const std::uint64_t* b,std::uint64_t* out) { record("sdk_Bitboard_Union"); *out=*a|*b;return 0; }
std::int32_t sdk_Bitboard_Intersect(const std::uint64_t* a,const std::uint64_t* b,std::uint64_t* out) { record("sdk_Bitboard_Intersect"); *out=*a&*b;return 0; }
std::int32_t sdk_Bitboard_IsZero(const std::uint64_t* in,std::int32_t* out) { record("sdk_Bitboard_IsZero"); *out=*in==0;return 0; }
std::int32_t sdk_Bitboard_NumSetBits(const std::uint64_t* in,std::uint32_t* out) { record("sdk_Bitboard_NumSetBits"); *out=std::popcount(*in);return 0; }
std::int32_t sdk_Bitboard_GetSetBits(const std::uint64_t* in,void* positions,std::size_t capacity,std::uint32_t* count) { record("sdk_Bitboard_GetSetBits");
    auto* out=static_cast<position*>(positions);*count=0;
    for(unsigned at=0;at<64;++at)if((*in>>at)&1u) { if(*count<capacity)out[*count]={int(at%8),int(at/8)};++*count; }
    return 0;
}
std::int32_t sdk_Bitboard_ReachableMask(const std::uint64_t* walls,std::int32_t x,std::int32_t y,std::uint64_t* out,std::uint16_t distance) { record("sdk_Bitboard_ReachableMask");
    verify(distance==1);*out=0;
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
        int nx=x+dx,ny=y+dy;if(nx>=0&&nx<8&&ny>=0&&ny<8&&!((*walls>>(ny*8+nx))&1u))set(out,nx,ny);
    }
    return 0;
}
}}