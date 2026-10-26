#include "native_observation.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

namespace fixture {
cambc::detail::champion_info champion{};
cambc::detail::camp_state camp{};
int champion_reads{},camp_reads{},lifecycle_reads{},group_reads{},timer_reads{},vision_reads{},tile_reads{};
int champion_error{},camp_error{},group_error{},timer_error{};
int camp_at_reads{},camp_metadata_reads{},camp_group_of_reads{},camp_at_error{},camp_metadata_error{},lookup_x{},lookup_y{};
std::uint32_t camp_at_handle{20};
std::uint32_t group_state{1};int timer{7};bool visible{true};
struct Geometry {int clears{},draws{},x{},y{};float radius{};} geometry;
}
// Native-record doubles retain host error/status behavior. All non-record SDK
// imports below are startup or explicit public-wrapper comparison dependencies.
namespace cambc::detail {
extern "C" std::int32_t sdk_get_settings(game_settings* out){*out={};return 0;}
extern "C" std::int32_t sdk_shared_memory_size(std::uint32_t* out){*out=8;return 0;}
extern "C" std::int32_t sdk_bc_metadata(metadata* out){*out={5,4,1,1,1,8,128};return 0;}
extern "C" std::int32_t sdk_consumable_info(std::uint32_t,consumable_info* out){*out={};return 0;}
extern "C" std::int32_t sdk_champion_design(std::int32_t,champion_design* out){*out={};return 0;}
extern "C" std::int32_t sdk_draft_memory_size(std::uint32_t* out){*out=0;return 0;}
extern "C" std::int32_t sdk_read_draft_memory(std::int32_t,void*,std::uint32_t){return 0;}
extern "C" std::int32_t sdk_my_champion(){return -1;}
extern "C" std::int32_t sdk_bc_map_symmetry(std::int32_t* out){*out=0;return 0;}
extern "C" std::int32_t sdk_bc_map_mask(std::int32_t,std::uint64_t* out){*out=0;return 0;}
extern "C" std::int32_t sdk_Bitboard_Zero(std::uint64_t* out){++fixture::geometry.clears;*out=0;return 0;}
extern "C" std::int32_t sdk_my_team(){return 0;}
extern "C" std::int32_t sdk_spawn_region(std::int32_t,aabb* out){*out={};return 0;}
extern "C" std::int32_t sdk_Bitboard_Set(std::uint64_t*,std::int32_t,std::int32_t,std::int32_t){return 0;}
extern "C" std::int32_t sdk_Bitboard_DrawChebyshev(std::uint64_t*,std::int32_t x,std::int32_t y,float radius){
    ++fixture::geometry.draws;fixture::geometry.x=x;fixture::geometry.y=y;fixture::geometry.radius=radius;return 0;
}
extern "C" std::int32_t sdk_bc_get_last_error(char* out,std::uint32_t length){
    constexpr char message[]="native record fixture";
    if(out&&length)std::memcpy(out,message,std::min<std::size_t>(length,sizeof(message)-1));
    return sizeof(message)-1;
}
extern "C" std::int32_t sdk_champion(std::uint32_t,champion_info* out){
    ++fixture::champion_reads;if(fixture::champion_error)return fixture::champion_error;*out=fixture::champion;return 0;
}
extern "C" std::int32_t sdk_champion_ability_props(std::uint32_t,std::uint32_t,ability_props* out){*out={};return 0;}
extern "C" std::int32_t sdk_camp(std::uint32_t,camp_state* out){
    ++fixture::camp_reads;if(fixture::camp_error)return fixture::camp_error;*out=fixture::camp;return 0;
}
extern "C" std::int32_t sdk_camp_design(std::int32_t,camp_design* out){*out={};return 0;}
extern "C" std::int32_t sdk_camp_at(std::int32_t x,std::int32_t y,std::uint32_t* out){
    ++fixture::camp_at_reads;fixture::lookup_x=x;fixture::lookup_y=y;
    if(fixture::camp_at_error)return fixture::camp_at_error;*out=fixture::camp_at_handle;return 0;
}
extern "C" std::int32_t sdk_map_camp_info(std::uint32_t,camp_info* out){
    ++fixture::camp_metadata_reads;if(fixture::camp_metadata_error)return fixture::camp_metadata_error;
    *out={3,2,static_cast<int>(camp_type::wolf)};return 0;
}
extern "C" std::int32_t sdk_map_camp_group_of(std::uint32_t,std::uint32_t* out){++fixture::camp_group_of_reads;*out=3;return 0;}
extern "C" std::int32_t sdk_camp_state(std::uint32_t,std::uint32_t* out){++fixture::lifecycle_reads;*out=2;return 0;}
extern "C" std::int32_t sdk_camp_group_state(std::uint32_t,std::uint32_t* out){
    ++fixture::group_reads;if(fixture::group_error)return fixture::group_error;*out=fixture::group_state;return 0;
}
extern "C" std::int32_t sdk_camp_group_cooldown(std::uint32_t,std::int32_t* out){
    ++fixture::timer_reads;if(fixture::timer_error)return fixture::timer_error;*out=fixture::timer;return 0;
}
extern "C" std::int32_t sdk_vision(std::uint64_t* out){++fixture::vision_reads;*out=0;return 0;}
extern "C" std::int32_t sdk_Bitboard_Test(const std::uint64_t*,std::int32_t,std::int32_t,std::int32_t* out){
    ++fixture::tile_reads;*out=fixture::visible;return 0;
}
}
struct Failure {int code{};std::string message;bool operator==(const Failure&)const=default;};
template<class Call> Failure failed(Call&& call){
    try{call();}
    catch(const cambc::error& error){return {static_cast<int>(error.code()),error.what()};}
    catch(const std::invalid_argument& error){return {-1,error.what()};}
    catch(const std::runtime_error& error){return {-2,error.what()};}
    assert(false);return {};
}
bool same_state(const cambc::map::camp_group_state& a,const cambc::map::camp_group_state& b){
    if(a.index()!=b.index())return false;
    const auto* timer=std::get_if<cambc::map::camp_group_respawning>(&a);
    return !timer||timer->timer==std::get<cambc::map::camp_group_respawning>(b).timer;
}
int main(){
    using namespace cambc;
    namespace observation=mosaic::native_observation;
    const observation::Stamp stamp{12,2,9};int checks=0;
    fixture::champion={1,2,2,1,123.25f,500.0f,45.5f,80.25f,1,3,static_cast<int>((1u<<28)|1u|8u|16u|2048u)};
    fixture::camp={3,2,110.5f,120.0f,20.25f,0.0f,{2,6}};
    const auto champion=detail::handle_access::make_champion(static_cast<handle_champion_t>(7));
    const auto camp=detail::handle_access::make_camp(static_cast<handle_camp_t>(20),static_cast<handle_camp_group_t>(3),camp_type::wolf,{3,2});
    const auto group=detail::handle_access::make_camp_group(static_cast<handle_camp_group_t>(3),10,4,20,{camp});
    const int champion_before=fixture::champion_reads;
    const auto owner=observation::read_champion(champion,stamp);
    assert(fixture::champion_reads==champion_before+1&&owner.stamp.matches(stamp));++checks;
    assert(owner.position()==champion.position()&&owner.health()==champion.health()&&owner.max_health()==champion.max_health()&&
        owner.strength()==champion.strength()&&owner.focus()==champion.focus()&&owner.attack_range()==champion.attack_range()&&
        owner.state()==champion.state()&&owner.move_speed()==1);++checks;
    const int captured_reads=fixture::champion_reads;
    for(int i=0;i<100;++i)assert(owner.health()==123.25f&&owner.position()==vec2(2,1)&&owner.state().contains(champion_state::can_attack));
    assert(fixture::champion_reads==captured_reads);++checks;
    fixture::champion.health=90;
    assert(owner.health()==123.25f&&observation::read_champion(champion,stamp).health()==90);++checks;
    fixture::champion.health=123.25f;
    fixture::geometry={};
    const auto public_mask=champion.attack_tiles();(void)public_mask;const auto public_geometry=fixture::geometry;
    const int mask_reads=fixture::champion_reads;fixture::geometry={};
    const auto cached_mask=owner.attack_tiles();(void)cached_mask;
    assert(fixture::champion_reads==mask_reads&&fixture::geometry.clears==public_geometry.clears&&
        fixture::geometry.draws==public_geometry.draws&&fixture::geometry.x==public_geometry.x&&
        fixture::geometry.y==public_geometry.y&&fixture::geometry.radius==public_geometry.radius);++checks;
    const int camp_before=fixture::camp_reads,lifecycle_before=fixture::lifecycle_reads;
    const auto body=observation::read_camp(camp,stamp);
    assert(fixture::camp_reads==camp_before+1&&fixture::lifecycle_reads==lifecycle_before&&body.stamp.matches(stamp));++checks;
    assert(body.position()==camp.position()&&body.health()==camp.health()&&body.strength()==camp.strength()&&
        body.max_health()==camp.max_health()&&body.focus()==camp.focus()&&body.value.cooldowns[1]==6);++checks;
    fixture::camp.health=0;assert(body.health()==110.5f);fixture::camp.health=110.5f;++checks;
    for(const int code:{static_cast<int>(error_code::api_not_in_vision),static_cast<int>(error_code::api_not_found),-1}){
        fixture::champion_error=code;
        assert(failed([&]{(void)champion.health();})==failed([&]{(void)observation::read_champion(champion,stamp);}));++checks;
        fixture::camp_error=code;
        assert(failed([&]{(void)camp.health();})==failed([&]{(void)observation::read_camp(camp,stamp);}));++checks;
    }
    fixture::champion_error=fixture::camp_error=0;
    const int design_before=fixture::champion_reads;
    const auto& design=champion_designs[0];
    assert(failed([&]{(void)design.health();})==failed([&]{(void)observation::read_champion(design,stamp);})&&fixture::champion_reads==design_before);++checks;
    const observation::GroupVisibility gate{stamp,static_cast<handle_camp_group_t>(3),true};
    for(const auto state:{1u,2u,3u}){
        fixture::group_state=state;
        const auto public_state=group.state();const int reads=fixture::group_reads,timers=fixture::timer_reads,vision=fixture::vision_reads;
        const auto recorded=observation::read_group(group,stamp,gate);
        assert(same_state(recorded.value,public_state)&&recorded.stamp.matches(stamp)&&fixture::group_reads==reads+1&&
            fixture::timer_reads==timers+(state==3)&&fixture::vision_reads==vision);++checks;
    }
    for(const auto state:{0u,9u}){
        fixture::group_state=state;
        assert(failed([&]{(void)group.state();})==failed([&]{(void)observation::read_group(group,stamp,gate);}));++checks;
    }
    fixture::group_state=3;
    fixture::timer_error=static_cast<int>(error_code::api_not_found);
    assert(failed([&]{(void)group.state();})==failed([&]{(void)observation::read_group(group,stamp,gate);}));++checks;
    fixture::timer_error=0;fixture::group_error=static_cast<int>(error_code::api_not_in_vision);
    assert(failed([&]{(void)group.state();})==failed([&]{(void)observation::read_group(group,stamp,gate);}));++checks;
    fixture::group_error=0;fixture::visible=false;
    auto invisible=gate;invisible.any_spawn_visible=false;
    const int hidden_before=fixture::group_reads;
    assert(failed([&]{(void)group.state();})==failed([&]{(void)observation::read_group(group,stamp,invisible);})&&fixture::group_reads==hidden_before);++checks;
    auto stale=gate;++stale.stamp.revision;
    auto wrong_group=gate;wrong_group.group=static_cast<handle_camp_group_t>(4);
    const int refused_before=fixture::group_reads;
    assert(failed([&]{(void)observation::read_group(group,stamp,stale);}).code==static_cast<int>(error_code::api_not_in_vision)&&
        failed([&]{(void)observation::read_group(group,stamp,wrong_group);}).code==static_cast<int>(error_code::api_not_in_vision)&&
        fixture::group_reads==refused_before);++checks;
    assert(!stamp.matches({stamp.epoch+1,stamp.actor,stamp.revision})&&!stamp.matches({stamp.epoch,stamp.actor+1,stamp.revision})&&
        !stamp.matches({stamp.epoch,stamp.actor,stamp.revision+1}));++checks;
    const vec2 lookup_position{3,2};
    const auto public_camp=vision::camp_at(lookup_position);
    const int lookup_before=fixture::camp_at_reads,metadata_before=fixture::camp_metadata_reads,group_of_before=fixture::camp_group_of_reads;
    const auto raw_handle=observation::camp_handle_at(lookup_position);
    assert(raw_handle==static_cast<handle_camp_t>(public_camp)&&fixture::camp_at_reads==lookup_before+1&&
        fixture::camp_metadata_reads==metadata_before&&fixture::camp_group_of_reads==group_of_before&&
        fixture::lookup_x==lookup_position.x&&fixture::lookup_y==lookup_position.y);++checks;
    for(const int code:{static_cast<int>(error_code::api_not_in_vision),static_cast<int>(error_code::api_not_found),
                        static_cast<int>(error_code::api_value_out_of_range),-1}) {
        fixture::camp_at_error=code;
        const int metadata_count=fixture::camp_metadata_reads,group_count=fixture::camp_group_of_reads;
        assert(failed([&]{(void)vision::camp_at(lookup_position);})==failed([&]{(void)observation::camp_handle_at(lookup_position);})&&
            fixture::camp_metadata_reads==metadata_count&&fixture::camp_group_of_reads==group_count);++checks;
    }
    fixture::camp_at_error=0;fixture::camp_metadata_error=static_cast<int>(error_code::api_not_found);
    (void)failed([&]{(void)vision::camp_at(lookup_position);});
    const int unused_metadata=fixture::camp_metadata_reads,unused_group=fixture::camp_group_of_reads,raw_lookups=fixture::camp_at_reads;
    assert(observation::camp_handle_at(lookup_position)==static_cast<handle_camp_t>(fixture::camp_at_handle)&&
        fixture::camp_at_reads==raw_lookups+1&&fixture::camp_metadata_reads==unused_metadata&&fixture::camp_group_of_reads==unused_group);++checks;
    std::printf("native observation checks: %d passed\n",checks);
}
