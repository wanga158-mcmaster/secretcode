#include "route_observation.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <type_traits>

namespace fixture {
int epoch=7,actor=0;std::int64_t fuel=100000;bool alive=true;
std::uint64_t ally=(1ull<<6)|(1ull<<19),enemy=(1ull<<7)|(1ull<<9),camp=1ull<<8,tp=1ull<<18;
int ally_reads{},enemy_reads{},camp_reads{},tp_reads{},exports{},unions{},sets{},empties{},records{},camp_records{};
int throw_stage{},cross_stage{},low_stage{},error_stage{},total_stages{};
void stage(int number){++total_stages;if(throw_stage==number)throw std::runtime_error("fixture failure");if(cross_stage==number)++epoch;if(low_stage==number)fuel=20000;}
void reset(){epoch=7;actor=0;fuel=100000;alive=true;ally=(1ull<<6)|(1ull<<19);enemy=(1ull<<7)|(1ull<<9);camp=1ull<<8;tp=1ull<<18;
    ally_reads=enemy_reads=camp_reads=tp_reads=exports=unions=sets=empties=records=camp_records=0;throw_stage=cross_stage=low_stage=error_stage=total_stages=0;}
}
// These doubles exercise the real checked self reader and SDK board wrappers.
// Their packed export is an explicit fixture premise, not native ABI evidence.
namespace cambc::detail {
extern "C" std::int32_t sdk_get_settings(game_settings* out){*out={};return 0;}
extern "C" std::int32_t sdk_shared_memory_size(std::uint32_t* out){*out=8;return 0;}
extern "C" std::int32_t sdk_bc_metadata(metadata* out){*out={5,4,1,1,1,8,128};return 0;}
extern "C" std::int32_t sdk_consumable_info(std::uint32_t,consumable_info* out){*out={};return 0;}
extern "C" std::int32_t sdk_champion_design(std::int32_t,champion_design* out){*out={};return 0;}
extern "C" std::int32_t sdk_draft_memory_size(std::uint32_t* out){*out=0;return 0;}
extern "C" std::int32_t sdk_read_draft_memory(std::int32_t,void*,std::uint32_t){return 0;}
extern "C" std::int32_t sdk_my_champion(){return fixture::actor;}
extern "C" std::int32_t sdk_bc_map_symmetry(std::int32_t* out){*out=0;return 0;}
extern "C" std::int32_t sdk_bc_map_mask(std::int32_t,std::uint64_t* out){*out=0;return 0;}
extern "C" std::int32_t sdk_Bitboard_Zero(std::uint64_t* out){*out=0;return 0;}
extern "C" std::int32_t sdk_my_team(){return 0;}
extern "C" std::int32_t sdk_spawn_region(std::int32_t,aabb* out){*out={};return 0;}
extern "C" std::int32_t sdk_Bitboard_Set(std::uint64_t* out,std::int32_t x,std::int32_t y,std::int32_t set){
    ++fixture::sets;const auto bit=1ull<<(y*5+x);if(set)*out|=bit;else *out&=~bit;return 0;}
extern "C" std::int32_t sdk_Bitboard_Union(const std::uint64_t* a,const std::uint64_t* b,std::uint64_t* out){++fixture::unions;*out=*a|*b;return 0;}
extern "C" std::int32_t sdk_Bitboard_IsZero(const std::uint64_t* a,std::int32_t* out){++fixture::empties;*out=*a==0;return 0;}
extern "C" std::int32_t sdk_Bitboard_ExportLinearBuffer(const std::uint64_t* a,std::uint8_t* out,std::size_t count){
    ++fixture::exports;fixture::stage(5);if(fixture::error_stage==5||count!=3)return 1;for(unsigned i=0;i<count;++i)out[i]=std::uint8_t(*a>>(i*8));return 0;}
extern "C" std::int32_t sdk_ally_occupancy(std::uint64_t* out){++fixture::ally_reads;fixture::stage(1);*out=fixture::ally;return 0;}
extern "C" std::int32_t sdk_enemy_occupancy(std::uint64_t* out){++fixture::enemy_reads;fixture::stage(2);*out=fixture::enemy;return 0;}
extern "C" std::int32_t sdk_camp_occupancy(std::uint64_t* out){++fixture::camp_reads;fixture::stage(3);*out=fixture::camp;return 0;}
extern "C" std::int32_t sdk_teleport_occupancy(std::uint64_t* out){++fixture::tp_reads;fixture::stage(4);*out=fixture::tp;return 0;}
extern "C" std::int32_t sdk_bc_get_last_error(char*,std::uint32_t){return 0;}
extern "C" std::int32_t sdk_bc_round_number(std::uint32_t* out){*out=fixture::epoch;return 0;}
extern "C" std::int64_t sdk_fuel_remaining(){return fixture::fuel;}
extern "C" std::int32_t sdk_champion_is_alive(std::uint32_t,std::int32_t* out){*out=fixture::alive;return 0;}
extern "C" std::int32_t sdk_champion(std::uint32_t,champion_info* out){++fixture::records;*out={};out->x=1;out->y=1;out->health=out->max_health=1000;out->strength=40;out->attack_range=1;out->move_speed=1;
    out->state=(champion_state::alive|champion_state::can_move|champion_state::can_attack|champion_state::can_cast).bits();return 0;}
extern "C" std::int32_t sdk_champion_identity(std::uint32_t,champion_identity* out){*out={};return 0;}
extern "C" std::int32_t sdk_champion_gold(std::uint32_t,std::uint32_t* out){*out=17;return 0;}
extern "C" std::int32_t sdk_champion_gems(std::uint32_t,std::uint32_t* out){*out=3;return 0;}
extern "C" std::int32_t sdk_champion_items(std::uint32_t,std::uint32_t* out){std::memset(out,0,sizeof(std::uint32_t)*std::size_t(item_type::count));return 0;}
extern "C" std::int32_t sdk_champion_buff_data(std::uint32_t,buff_data*,std::uint32_t,std::uint32_t* out){*out=0;return 0;}
extern "C" std::int32_t sdk_item_info(std::uint32_t,item_info* out){*out={};return 0;}
extern "C" std::int32_t sdk_champion_ability_props(std::uint32_t,std::uint32_t,ability_props* out){*out={};return 0;}
extern "C" std::int32_t sdk_camp(std::uint32_t,camp_state* out){++fixture::camp_records;*out={};return 0;}
extern "C" std::int32_t sdk_camp_buffs(std::uint32_t,camp_buff_data*,std::uint32_t,std::uint32_t* out){++fixture::camp_records;*out=0;return 0;}
extern "C" std::int32_t sdk_camp_design(std::int32_t,camp_design* out){*out={};return 0;}
}
namespace {
using namespace mosaic;namespace route=route_observation;
int failures{},checks{};
void check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
struct World {
    frame::SourceWorld world;
    World(){world.initialized=true;world.static_blocked.emplace();world.self_design.emplace();
        world.self_design->camp_damage_multiplier=1;std::vector<spatial::Entry> entries;
        for(int i=0;i<160;++i){frame::Camp design;design.handle=100+i;
            const auto handle=cambc::detail::handle_access::make_camp(static_cast<cambc::handle_camp_t>(100+i),
                static_cast<cambc::handle_camp_group_t>(0),cambc::camp_type::wolf,{20+i,20});
            world.camps.push_back({handle,design});entries.push_back({100+i,20+i,20,3});}
        world.spatial_support.build(entries,true);}
};
struct Observer {
    selected::Stamp stamp;spatial::Index* index{};int epoch=7,actor=0;
    int fail{},cross{},reads{},boards{},fences{},width=5,height=4;bool dead{},short_export{},throw_export{};
    bool begin(selected::Stamp value){stamp=value;return fail!=1&&same_input();}
    bool same_input()const{return stamp.epoch==epoch&&stamp.actor==actor;}
    bool read_self(frame::Self& out){++reads;out.alive=!dead;out.pos={1,1};if(cross==1)++epoch;return fail!=2;}
    bool read_observed(route::Packet& out){++boards;out.observed_blocked={0x80,0x03,0x08};out.has_visible_enemy=true;
        if(throw_export)throw std::runtime_error("interrupted export");if(cross==2)++epoch;if(cross==3)++actor;
        if(cross==4)index->revoke();if(short_export)out.observed_blocked.resize(1);return fail!=3;}
    bool fence(std::uint64_t reserve){++fences;return reserve==20000&&fail!=4&&same_input();}
    bool same_layout(spatial::Token token,std::size_t count,int w,int h){return fail!=5&&token.valid(count)&&w==width&&h==height;}
};
bool revoked(const route::Packet& out){return !out.complete&&out.observed_blocked.empty()&&out.width==0&&out.height==0&&!out.body.alive&&!out.has_visible_enemy;}
}
int main(){
    static_assert(!std::is_convertible_v<route::Packet,selected::Packet>);
    World owner;route::Cache cache;route::Packet out;
    fixture::reset();const selected::Stamp stamp{7,0,9};
    check(route::capture(owner.world,cache,stamp,out)==route::Status::ready&&out.complete&&out.stamp.matches(stamp)&&out.member_count==160,
          "real native adapter publishes coherent dated packet");
    check(fixture::ally_reads==1&&fixture::enemy_reads==1&&fixture::camp_reads==1&&fixture::tp_reads==1&&fixture::exports==1&&fixture::unions==3&&fixture::empties==1&&fixture::sets==1,
          "four occupancy masks once, OR once each, one enemy test and one packed export");
    check(fixture::records==1&&fixture::camp_records==0&&out.body.gold==17&&out.body.gems==3&&out.body.can_move,
          "one owning checked self record and zero member record scan at N160");
    check(out.observed_blocked.size()==3&&out.tile(1,1)==route::Tile::unknown_may_refuse&&out.tile(0,0)==route::Tile::unknown_may_refuse,
          "only self origin cleared and zero means unknown, never known free");
    check(out.tile(2,1)==route::Tile::observed_blocked&&out.tile(3,1)==route::Tile::observed_blocked&&out.tile(4,1)==route::Tile::observed_blocked&&
          out.tile(3,3)==route::Tile::observed_blocked&&out.tile(4,3)==route::Tile::observed_blocked&&out.has_visible_enemy,
          "LSB packed byte boundaries, proposed destination, all families and last valid bit survive");
    check(out.tile(-1,0)==route::Tile::out_of_bounds&&out.tile(5,0)==route::Tile::out_of_bounds&&route::packed_size(100,100)==1250&&route::packed_size(101,1)==0,
          "checked bounds and packed maximum1250 without decoded tile array");
    const auto saved=out;const auto before=fixture::total_stages;
    for(int i=0;i<100;++i)(void)out.tile(i%5,(i/5)%4);
    check(fixture::total_stages==before,"pure dated tile lookups have no host observations");
    *owner.world.static_blocked->ptr()=1ull<<0;
    check(out.tile(0,0)==route::Tile::unknown_may_refuse&&*owner.world.static_blocked->ptr()==1,
          "terrain is retained separately and never misrepresented as observed occupancy");
    fixture::ally=fixture::enemy=fixture::camp=fixture::tp=0;route::Packet empty;
    check(route::capture(owner.world,cache,stamp,empty)==route::Status::ready&&!empty.has_visible_enemy&&empty.tile(4,1)==route::Tile::unknown_may_refuse&&saved.observed_blocked==out.observed_blocked&&saved.tile(4,1)==route::Tile::observed_blocked,
          "cache/board reuse cannot mutate owned prior bytes and empty observation remains unknown");
    route::SelfPacket body;const auto masks=fixture::total_stages;
    check(route::capture_self(owner.world,cache,{7,0,10},body)==route::Status::ready&&body.complete&&body.stamp.revision==10&&fixture::total_stages==masks&&out.stamp.revision==9,
          "fresh self-only packet performs no occupancy acquisition or route restamp");
    fixture::fuel=52000;out=saved;
    check(route::capture(owner.world,cache,stamp,out)==route::Status::incomplete&&revoked(out),"52K strict admission failure revokes prior complete packet");
    fixture::reset();fixture::alive=false;
    check(route::capture(owner.world,cache,stamp,out)==route::Status::unsupported&&revoked(out)&&fixture::total_stages==0&&
          route::capture_self(owner.world,cache,stamp,body)==route::Status::ready&&!body.body.alive,
          "dead self is a self-only fact, never an occupancy-origin route assumption");
    fixture::reset();World foreign;out=saved;
    check(route::capture(foreign.world,cache,stamp,out)==route::Status::unsupported&&revoked(out)&&fixture::total_stages==0,
          "cache cannot rebind to another equal-count world");
    auto design=owner.world.self_design;owner.world.self_design.reset();out=saved;
    check(route::capture(owner.world,cache,stamp,out)==route::Status::unsupported&&revoked(out),"missing self design rejects before dereference");owner.world.self_design=design;
    for(int stage=1;stage<=5;++stage){fixture::reset();fixture::throw_stage=stage;out=saved;bool threw=false;
        try{(void)route::capture(owner.world,cache,stamp,out);}catch(const std::runtime_error&){threw=true;}
        check(threw&&revoked(out),"SDK-wrapper exception revokes complete and partial output");}
    fixture::reset();fixture::cross_stage=5;out=saved;
    check(route::capture(owner.world,cache,stamp,out)==route::Status::stale&&revoked(out),"actual epoch change inside packed export revokes publication");
    fixture::reset();fixture::low_stage=5;out=saved;
    check(route::capture(owner.world,cache,stamp,out)==route::Status::incomplete&&revoked(out),"late20K admission failure revokes completed board bytes");
    fixture::reset();fixture::error_stage=5;out=saved;bool native_error=false;
    try{(void)route::capture(owner.world,cache,stamp,out);}catch(const cambc::error& error){native_error=int(error.code())==1;}
    check(native_error&&revoked(out),"actual SDK check propagates native error and revokes partial packet");
    for(int fail=1;fail<=5;++fail){World one;Observer source;source.index=&one.world.spatial_support;source.fail=fail;out=saved;
        const auto status=route::collect(source,out,stamp,one.world.spatial_support.borrow(),160,5,4);
        check(status!=route::Status::ready&&revoked(out),"every collection stage failure revokes prior output");}
    for(int cross=1;cross<=4;++cross){World one;Observer source;source.index=&one.world.spatial_support;source.cross=cross;out=saved;
        check(route::collect(source,out,stamp,one.world.spatial_support.borrow(),160,5,4)==route::Status::stale&&revoked(out),
              "epoch actor and generation changes cannot publish current packet");}
    {World one;Observer source;source.index=&one.world.spatial_support;source.short_export=true;out=saved;
        check(route::collect(source,out,stamp,one.world.spatial_support.borrow(),160,5,4)==route::Status::unsupported&&revoked(out),"short packed span is unsupported and revoked");}
    {World one;Observer source;source.index=&one.world.spatial_support;source.dead=true;out=saved;
        check(route::collect(source,out,stamp,one.world.spatial_support.borrow(),160,5,4)==route::Status::unsupported&&source.boards==0&&revoked(out),"dead stage-double never acquires boards");}
    {World one;auto token=one.world.spatial_support.borrow();one.world.spatial_support.revoke();Observer source;source.index=&one.world.spatial_support;out=saved;
        check(route::collect(source,out,stamp,token,160,5,4)==route::Status::unsupported&&source.reads==0&&revoked(out),"stale borrowed layout rejected before any self read");}
    {World one;auto token=one.world.spatial_support.borrow();std::vector<spatial::Entry> rebuilt;
        for(int i=0;i<160;++i)rebuilt.push_back({100+i,20+i,20,3});one.world.spatial_support.revoke();one.world.spatial_support.build(rebuilt,true);
        Observer source;source.index=&one.world.spatial_support;out=saved;
        check(one.world.spatial_support.valid()&&one.world.spatial_support.count()==160&&
              route::collect(source,out,stamp,token,160,5,4)==route::Status::unsupported&&revoked(out),
              "equal-count rebuilt layout cannot revive a previous generation token");}
    check(saved.tile(0,0)==route::Tile::unknown_may_refuse,"dated route input never upgrades empty to selected landing authority");
    std::printf("route observation: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
