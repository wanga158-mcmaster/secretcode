#include "frame.h"
#include <cstdio>

// Supply the immutable SDK settings import; the pure fixture creates no engine bitboards
// and uses an observation-stage double, never a live game or native fuel measurement.
namespace cambc::detail {
int pure_camp_queries{};
int pure_board_copies{};
extern "C" std::int32_t sdk_get_settings(game_settings* out) { *out={};return 0; }
extern "C" std::int32_t sdk_shared_memory_size(std::uint32_t* out) { *out=8;return 0; }
extern "C" std::int32_t sdk_bc_metadata(metadata* out) { *out={5,4,1,1,1,8,128};return 0; }
extern "C" std::int32_t sdk_consumable_info(std::uint32_t,consumable_info* out) { *out={};return 0; }
extern "C" std::int32_t sdk_champion_design(std::int32_t,champion_design* out) { *out={};return 0; }
extern "C" std::int32_t sdk_draft_memory_size(std::uint32_t* out) { *out=0;return 0; }
extern "C" std::int32_t sdk_read_draft_memory(std::int32_t,void*,std::uint32_t) { return 0; }
extern "C" std::int32_t sdk_my_champion() { return -1; }
extern "C" std::int32_t sdk_bc_map_symmetry(std::int32_t* out) { *out=0;return 0; }
extern "C" std::int32_t sdk_bc_map_mask(std::int32_t,std::uint64_t* out) { *out=0;return 0; }
extern "C" std::int32_t sdk_Bitboard_Zero(std::uint64_t* out) { *out=0;return 0; }
extern "C" std::int32_t sdk_my_team() { return 0; }
extern "C" std::int32_t sdk_spawn_region(std::int32_t,aabb* out) { *out={};return 0; }
extern "C" std::int32_t sdk_Bitboard_Set(std::uint64_t*,std::int32_t,std::int32_t,std::int32_t) { return 0; }
extern "C" std::int32_t sdk_bc_get_last_error(char*,std::uint32_t) { return 0; }
extern "C" std::int32_t sdk_champion(std::uint32_t,champion_info* out) { *out={};return 0; }
extern "C" std::int32_t sdk_champion_ability_props(std::uint32_t,std::uint32_t,ability_props* out) { *out={};return 0; }
extern "C" std::int32_t sdk_camp(std::uint32_t,camp_state* out) { ++pure_camp_queries;*out={};return 0; }
extern "C" std::int32_t sdk_camp_design(std::int32_t,camp_design* out) { ++pure_camp_queries;*out={};return 0; }
extern "C" std::int32_t sdk_Bitboard_ImportRawBuffer(std::uint64_t*,const std::uint8_t*,std::size_t) { ++pure_board_copies;return 0; }
extern "C" std::int32_t sdk_Bitboard_ExportRawBuffer(const std::uint64_t*,std::uint8_t*,std::size_t) { ++pure_board_copies;return 0; }
}

namespace {
using namespace mosaic::frame;
struct Board { int token{}; };
struct PackedVisibility {
    std::array<std::uint8_t,3> bytes{0,2,8};
    mutable int exports{};
    static std::size_t linear_buffer_size(){return 3;}
    void export_linear_buffer(std::span<std::uint8_t> out) const {
        ++exports;std::copy(bytes.begin(),bytes.end(),out.begin());
    }
};
struct CapturedCampTiles {
    std::vector<cambc::vec2> occupied;
    mutable int position_reads{};
    const std::vector<cambc::vec2>& positions() const {++position_reads;return occupied;}
};
template<class T> struct DesignSource { T design; };
using Packet=Snapshot<Board>;
int failures{};
void check(bool value,const char* name) {
    if(!value){std::printf("FAIL %s\n",name);++failures;}
}
struct Observer {
    std::array<int,7> calls{};
    int current_epoch{7},current_actor{3};
    bool fail_groups{},change_epoch{},reset_available{},change_visibility{},displaced{};
    int stationary_reads{};
    std::vector<int> stationary_groups;
    mosaic::spatial::Token spatial_token;
    Stamp stamp;
    bool begin(Stamp value){++calls[0];stamp=value;return value.epoch==current_epoch&&value.actor==current_actor;}
    bool begin_self_revision(Stamp value){return begin(value);}
    bool begin_stationary_revision(Stamp value,const CampDamageSupport& support){stationary_groups=support.groups;return begin(value);}
    bool finish_self_revision(Packet& out){return finish(out);}
    bool read_self(Packet& out){
        ++calls[1];out.self.pos=displaced?cambc::vec2{5,5}:cambc::vec2{4,5};out.self.hp=900;out.self.strength=80;out.self.can_move=true;
        out.self.alive=true;
        out.self.can_attack=reset_available;out.self.can_cast=true;out.self.range=3;return true;
    }
    bool read_boards(Packet& out){
        ++calls[2];out.visible.token=19;out.static_blocked.token=2;out.dynamic_blocked.token=5;
        out.move_destinations={{3,5},{4,6}};out.attack_targets={{6,5}};return true;
    }
    bool read_groups_and_camps(Packet& out){
        ++calls[3];if(fail_groups)return false;
        Group first;first.id=0;first.state=GroupState::alive;first.observed_round=current_epoch;first.members={0,1};
        Group other;other.id=1;other.state=GroupState::unknown;other.members={2};other.possibly_woken=true;
        out.groups={first,other};
        Camp target;target.handle=10;target.group=0;target.member=0;target.pos=target.spawn={6,5};
        target.hp=120;target.strength=0;target.known=target.visible=target.alive=true;target.awake=false;
        target.buffs_known=true;
        Camp sibling;sibling.handle=11;sibling.group=0;sibling.member=1;sibling.pos=sibling.spawn={8,5};
        sibling.hp=0;sibling.strength=60;sibling.range=3;sibling.known=false;sibling.possible_alive=true;
        Camp collateral;collateral.handle=20;collateral.group=1;collateral.pos=collateral.spawn={7,6};
        collateral.hp=1350;collateral.strength=20;collateral.range=3;collateral.known=false;
        out.camps={target,sibling,collateral};return true;
    }
    bool read_stationary_boards(Packet& out,const Board& old,const CampDamageSupport&){
        ++calls[2];if(change_visibility||old.token!=19)return false;
        out.dynamic_blocked.token=6;out.move_destinations={{6,5},{4,6}};return true;
    }
    bool read_stationary_groups(Packet& out,const CampDamageSupport& support){
        ++calls[3];++stationary_reads;
        for(int group:support.groups)for(auto index:out.group(group)->members) {
            auto& camp=out.camps[index];
            if(!camp.known)continue;
            camp.hp=camp.handle==10?0.0f:1000.0f;camp.alive=camp.hp>0;camp.awake=camp.alive;
            camp.buffs_known=camp.alive;camp.stunned=camp.alive;camp.stun_duration=camp.alive?1:0;
            camp.shield=0;camp.shield_duration=0;
        }
        return !fail_groups;
    }
    bool read_enemies(Packet& out){++calls[4];Enemy e;e.handle=99;e.pos={8,8};e.hp=500;e.visible=e.alive=true;out.enemies={e};return true;}
    bool read_abilities(Packet& out){++calls[5];out.abilities[0].slot=0;out.abilities[0].ready=true;
        out.abilities[0].reset=true;out.abilities[0].self=true;out.abilities[0].max_cooldown=8;return true;}
    bool finish(Packet& out){++calls[6];out.spatial_support=spatial_token;if(change_epoch)++current_epoch;return out.epoch==current_epoch&&out.actor==current_actor;}
};
struct SetupObserver:Observer {
    int layout_calls{};
    bool ready{};
    bool prepare_layout(Packet& out) {
        ++layout_calls;if(ready)return true;
        out.layout_initialized=ready=true;return false;
    }
};
struct DomainObserver:Observer {
    mosaic::spatial::Index index;
    int actual_speed{1},member_reads{};
    bool wider_ability{},revoke_after_members{},abilities_before_members{};
    DomainObserver() {
        const std::array<mosaic::spatial::Entry,3> entries{{{10,6,5,0},{11,12,5,7},{20,70,60,3}}};
        index.build(entries,true);spatial_token=index.borrow();
    }
    bool read_self(Packet& out) {
        if(!Observer::read_self(out))return false;
        out.self.move_speed=actual_speed;return true;
    }
    bool read_abilities(Packet& out) {
        if(!Observer::read_abilities(out))return false;
        out.abilities[1].range=wider_ability?5.25f:0.0f;
        out.abilities[1].radius=wider_ability?0.5f:0.0f;
        out.abilities[1].ready=false;return true;
    }
    bool prepare_domain(Packet& out) {
        out.spatial_support=index.borrow();
        const auto limited=mosaic::domain::make_limited({stamp.epoch,stamp.actor,stamp.revision},
            out.spatial_support,3,mosaic::domain::geometry(out.self,out.abilities));
        if(limited.complete)out.domain=limited;
        return true;
    }
    bool read_groups_and_camps(Packet& out) {
        abilities_before_members=calls[5]==1;
        if(!Observer::read_groups_and_camps(out))return false;
        out.camps[1].pos=out.camps[1].spawn={12,5};out.camps[1].range=7;
        out.camps[2].pos=out.camps[2].spawn={70,60};
        for(std::size_t i=0;i<out.camps.size();++i) {
            if(out.domain.contains(i)){++member_reads;continue;}
            restore_camp_memory(out.camps[i],MemberMemory{1350,20,2,true,false});
        }
        if(revoke_after_members)index.revoke();
        return true;
    }
    bool finish(Packet& out) {
        if(!Observer::finish(out))return false;
        out.spatial_support=index.borrow();return true;
    }
};
float pure_candidate_value(const Packet& frame,int handle) {
    const auto* target=frame.camp(handle);if(!frame.complete||!target||!target->known||!target->alive)return -1;
    float value=std::min(frame.self.strength,target->hp);
    // Every sibling/non-target group remains represented, including remembered zero HP.
    for(const auto& c:frame.camps)if(c.group==target->group&&c.possible_alive)value-=c.strength;
    return value;
}
}

int main() {
    using namespace mosaic::frame;
    std::vector<std::uint8_t> visibility(3,0);visibility[1]=2;
    check(linear_visible(visibility,5,4,{4,1})&&!linear_visible(visibility,5,4,{1,0})&&
          !linear_visible(visibility,5,4,{4,0})&&!linear_visible(visibility,5,4,{5,1}),
          "packed row-major LSB visibility marks tile nine including an odd-width row boundary");
    PackedVisibility packed;std::vector<std::uint8_t> exported;
    export_linear_visibility(packed,exported);
    int visible_count=0;
    for(int member=0;member<160;++member)visible_count+=linear_visible(exported,5,4,{member%5,(member/5)%4});
    check(packed.exports==1&&visible_count==16&&exported.size()==3,
          "one owning export supplies all160 pure visibility lookups with no per-member host predicate");
    check(!linear_visible(std::span<const std::uint8_t>{},5,4,{4,1})&&
          !linear_visible(std::span<const std::uint8_t>{exported.data(),1},5,4,{4,1}),
          "missing or truncated packed visibility never fabricates visible members");
    CapturedCampTiles camp_mask;
    std::unordered_map<int,std::size_t> camp_indices;
    for(int member=0;member<160;++member) {
        camp_mask.occupied.push_back({member%40,member/40});
        camp_indices.emplace(1000+159-member,static_cast<std::size_t>(member));
    }
    std::vector<bool> enumerated;int camp_tile_queries=0;
    const auto tile_handle=[&](cambc::vec2 position){++camp_tile_queries;return 1000+159-(position.y*40+position.x);};
    check(enumerate_visible_camp_indices(camp_mask,camp_indices,160,enumerated,tile_handle)&&
          camp_mask.position_reads==1&&camp_tile_queries==160&&std::count(enumerated.begin(),enumerated.end(),true)==160,
          "one captured occupancy mask enumerates all160 checked identities without reconstructing metadata");
    CapturedCampTiles sparse_mask{{{39,3},{0,0},{7,2}}};camp_tile_queries=0;
    check(enumerate_visible_camp_indices(sparse_mask,camp_indices,160,enumerated,tile_handle)&&
          sparse_mask.position_reads==1&&camp_tile_queries==3&&enumerated[159]&&enumerated[0]&&enumerated[87]&&
          std::count(enumerated.begin(),enumerated.end(),true)==3,
          "only actual occupied tiles are queried and native identity order maps back to canonical rows");
    check(!enumerate_visible_camp_indices(sparse_mask,camp_indices,160,enumerated,[](cambc::vec2){return -1;}),
          "an unknown current identity rejects enumeration instead of dropping it from a complete packet");
    bool camp_lookup_error=false;
    try {enumerate_visible_camp_indices(sparse_mask,camp_indices,160,enumerated,[](cambc::vec2)->int{
        throw std::runtime_error("camp lookup failure");});}
    catch(const std::runtime_error& error){camp_lookup_error=std::string(error.what())=="camp lookup failure";}
    check(camp_lookup_error,"current tile lookup errors propagate before any complete packet can publish");
    DomainObserver local;Packet local_packet;
    check(collect(local,local_packet,{7,3,60})&&local.abilities_before_members&&local.member_reads==2&&
          local_packet.domain.limited&&local_packet.domain.contains(0)&&local_packet.domain.contains(1)&&
          !local_packet.domain.contains(2)&&local_packet.self.move_speed==1,
          "limited collection derives support after actual self and all abilities including an independent harmful sibling");
    check(local_packet.camps.size()==3&&local_packet.groups.size()==2&&local_packet.enemies.size()==1&&
          local_packet.group(0)->observed_round==7&&local_packet.camps[2].observed_round==2&&
          !local_packet.camps[2].known&&!local_packet.camps[2].visible&&!local_packet.camps[2].buffs_known&&
          local_packet.camps[2].possible_alive,
          "limited member details retain canonical remote memory without making remote facts or global lifecycle dates fresh");
    const auto local_queries=local.calls;
    for(int candidate=0;candidate<100;++candidate)(void)pure_candidate_value(local_packet,10);
    check(local.calls==local_queries&&local.member_reads==2,
          "limited candidate count does not issue new observation stages or member reads");
    local_packet.invalidate();local.reset_available=true;
    check(!local_packet.complete&&local_packet.captured_complete&&local_packet.domain.complete&&
          refresh_self_packet(local,local_packet,local_packet,{7,3,61,true,true,true})&&
          local_packet.domain.stamp.revision==61&&local_packet.self.can_attack&&local_packet.self.attacked,
          "invalidation preserves factual limited support for a proven self-only reset revision");
    local_packet.invalidate();
    check(refresh_stationary_packet(local,local_packet,local_packet,{7,3,62,true,true,true},10,0)&&
          local_packet.domain.stamp.revision==62&&!local_packet.camp(10)->alive&&!local_packet.camp(20)->known,
          "stationary refresh rebinds limited support while retaining unknown remote rows");
    DomainObserver geometry_change;Packet geometry_packet;
    collect(geometry_change,geometry_packet,{7,3,70});geometry_packet.invalidate();geometry_change.wider_ability=true;
    check(!refresh_self_packet(geometry_change,geometry_packet,geometry_packet,{7,3,71})&&
          !geometry_packet.complete&&!geometry_packet.captured_complete,
          "a cooling ability's changed reach invalidates a narrow refresh instead of widening factual support");
    DomainObserver speed_change;Packet speed_packet;
    collect(speed_change,speed_packet,{7,3,72});speed_packet.invalidate();speed_change.actual_speed=2;
    check(!refresh_stationary_packet(speed_change,speed_packet,speed_packet,{7,3,73},10,0)&&
          !speed_packet.complete,
          "actual changed movement speed rejects stationary support reuse");
    DomainObserver revoked_local;revoked_local.revoke_after_members=true;Packet revoked_packet;
    check(!collect(revoked_local,revoked_packet,{7,3,74})&&!revoked_packet.complete&&!revoked_packet.captured_complete&&
          revoked_packet.domain.limited,
          "revoking an index after partial limited reads discards the packet without relabeling it full");
    DomainObserver fallback_local;fallback_local.index.revoke();Packet fallback_packet;
    check(collect(fallback_local,fallback_packet,{7,3,75})&&!fallback_packet.domain.limited&&fallback_local.member_reads==3,
          "an unavailable layout chooses a full member scan before collection");
    fallback_packet.invalidate();fallback_local.wider_ability=true;
    check(!refresh_self_packet(fallback_local,fallback_packet,fallback_packet,{7,3,76})&&!fallback_packet.complete,
          "full-family narrow refresh also rejects changed ability support geometry");
    Camp stale_remote;stale_remote.pos=stale_remote.spawn={70,60};stale_remote.hp=0;stale_remote.observed_round=7;
    stale_remote.known=stale_remote.visible=stale_remote.buffs_known=stale_remote.stunned=true;
    stale_remote.shield=100;stale_remote.shield_duration=2;stale_remote.stun_duration=1;
    restore_camp_memory(stale_remote,MemberMemory{1350,40,2,true,false});
    check(stale_remote.hp==1350&&stale_remote.strength==40&&stale_remote.observed_round==2&&
          !stale_remote.known&&!stale_remote.visible&&!stale_remote.buffs_known&&!stale_remote.stunned&&
          stale_remote.shield==0&&stale_remote.stun_duration==0&&stale_remote.possible_alive,
          "remote rows restore only committed dated memory and revoke failed-revision buffs and visibility");
    DomainObserver remote_position;CapturedCampTiles actual_remote{{{71,60}}};
    const std::unordered_map<int,std::size_t> remote_map{{20,2}};std::vector<bool> remote_seen;
    check(enumerate_visible_camp_indices(actual_remote,remote_map,3,remote_seen,[](cambc::vec2){return 20;},
          [&](std::size_t i,int handle,cambc::vec2 actual){remote_position.index.observe(i,handle,actual.x,actual.y,3);})&&
          remote_seen[2]&&!remote_position.index.valid(),
          "actual occupancy validates remote observed geometry and revokes displaced support without a camp record read");
    SetupObserver staged;Packet staged_packet;
    check(!collect(staged,staged_packet,{7,3,50})&&!staged_packet.complete&&!staged_packet.captured_complete&&
          staged.calls==std::array<int,7>{1,0,0,0,0,0,0},
          "dedicated packet setup cannot stack live SDK collection or publish a partial frame");
    check(collect(staged,staged_packet,{7,3,51})&&staged_packet.complete&&staged.layout_calls==2&&
          staged.calls==std::array<int,7>{2,1,1,1,1,1,1},
          "only the later input continues from retained setup into complete observation");
    Observer observer;Packet frame;
    check(collect(observer,frame,{7,3,1,true,false,false})&&frame.complete&&frame.matches(7,3,1),
          "all observation stages publish one coherent actor epoch and revision");
    check(frame.self.moved&&!frame.self.attacked&&!frame.self.cast_spent&&!frame.self.can_attack&&frame.self.can_cast,
          "actual allowances and applied-command history remain separate after a ranged move");
    const auto counts=observer.calls;
    const float one=pure_candidate_value(frame,10);float many=0;
    for(int candidate=0;candidate<100;++candidate)many+=pure_candidate_value(frame,10);
    check(observer.calls==counts&&one==20&&many==2000,
          "one hundred candidate evaluations preserve decision inputs with zero additional observation queries");
    check(std::all_of(counts.begin(),counts.end(),[](int count){return count==1;})&&frame.camps.size()==3&&
          frame.group(0)->members.size()==2&&frame.group(1)->possibly_woken&&frame.enemies.size()==1,
          "every collection stage runs once and preserves siblings non-target groups and enemies");
    check(!frame.camp(11)->known&&frame.camp(11)->hp==0&&frame.camp(11)->possible_alive&&
          !frame.camp(20)->known,
          "remembered zero HP and unobserved collateral state remain explicit possible threats");
    frame.invalidate();check(!frame.matches(7,3,1)&&pure_candidate_value(frame,10)<0,
          "an applied or refused primitive revokes the old revision before dependent planning");
    check(collect(observer,frame,{7,3,2,true,true,true})&&frame.matches(7,3,2)&&!frame.matches(7,3,1)&&
          frame.self.attacked&&frame.self.cast_spent,
          "refresh creates a new revision with the caller's applied-command history");
    frame.invalidate();observer.reset_available=true;
    const auto before_self_refresh=observer.calls;
    check(refresh_self_packet(observer,frame,frame,{7,3,3,true,true,true})&&frame.matches(7,3,3)&&
          frame.self.can_attack&&frame.self.attacked&&frame.camps.size()==3&&frame.enemies.size()==1&&
          observer.calls[2]==before_self_refresh[2]&&observer.calls[3]==before_self_refresh[3]&&
          observer.calls[4]==before_self_refresh[4]&&observer.calls[1]==before_self_refresh[1]+1&&
          observer.calls[5]==before_self_refresh[5]+1,
          "a proven self-only reset refreshes actual allowances without rereading other entities");
    observer.fail_groups=true;
    check(!collect(observer,frame,{7,3,4})&&!frame.complete&&pure_candidate_value(frame,10)<0&&
          !refresh_self_packet(observer,frame,frame,{7,3,5}),
          "an interrupted group collection cannot publish a partial safe world");
    observer.fail_groups=false;observer.change_epoch=true;
    check(!collect(observer,frame,{7,3,6})&&!frame.complete,
          "a changed final owner epoch rejects the entire observation packet");
    observer.change_epoch=false;observer.current_epoch=7;observer.current_actor=4;
    check(!collect(observer,frame,{7,3,7})&&!frame.complete,
          "actor replacement cannot reuse the prior owner's frame");
    SourceWorld native_world;
    NativeObserver native(native_world,{});
    Frame persistent;
    Group group;group.id=0;group.members={0,1,2};persistent.groups.push_back(group);
    persistent.camps.resize(3);
    const auto* member_storage=persistent.groups[0].members.data();
    const auto* camp_storage=persistent.camps.data();
    persistent.complete=persistent.captured_complete=true;
    for(int repeat=0;repeat<100;++repeat)native.reset_packet(persistent);
    check(!persistent.complete&&!persistent.captured_complete&&persistent.groups[0].members.data()==member_storage&&
          persistent.camps.data()==camp_storage&&persistent.groups[0].members.size()==3,
          "native packet resets retain the complete static member layout without nested vector churn");
    Observer stationary;Packet damage;
    check(collect(stationary,damage,{7,3,10}),"stationary fixture begins from a complete actual packet");
    const auto before_damage=stationary.calls;
    damage.invalidate();
    check(refresh_stationary_packet(stationary,damage,damage,{7,3,11,false,true,false},10,0)&&
          damage.matches(7,3,11)&&damage.camp(10)->known&&!damage.camp(10)->alive&&damage.camp(10)->hp==0&&
          !damage.camp(10)->buffs_known&&!damage.camp(11)->known&&!damage.camp(11)->buffs_known&&
          damage.camp(11)->possible_alive&&damage.group(1)->possibly_woken&&
          stationary.stationary_groups==std::vector<int>{0}&&stationary.calls[4]==before_damage[4]&&
          damage.dynamic_blocked.token==6&&damage.move_destinations.front()==cambc::vec2{6,5},
          "stationary hit rereads full target group and occupancy while preserving unknown siblings and unaffected groups");
    check(stationary.stationary_reads==1&&damage.self.attacked,
          "stationary refresh observes one actual effect without executing or forecasting it twice");
    collect(stationary,damage,{7,3,12});
    damage.camps[2].known=damage.camps[2].visible=damage.camps[2].alive=true;
    const auto splash=camp_damage_support(damage,10,1);
    check(splash.complete&&splash.groups==std::vector<int>({0,1}),
          "splash closure includes a visible collateral group and all its member indices");
    check(refresh_stationary_packet(stationary,damage,damage,{7,3,13},10,1)&&
          damage.camp(20)->hp==1000&&damage.camp(20)->buffs_known&&damage.camp(20)->stunned&&
          damage.camp(20)->stun_duration==1&&stationary.stationary_groups==std::vector<int>({0,1}),
          "affected collateral rows receive observed HP and status instead of retained or predicted buffs");
    collect(stationary,damage,{7,3,14});
    damage.enemies[0].pos={7,6};
    check(!camp_damage_support(damage,10,1).complete,
          "champion collateral rejects the restricted camp-only refresh");
    collect(stationary,damage,{7,3,15});stationary.change_visibility=true;
    check(!refresh_stationary_packet(stationary,damage,damage,{7,3,16},10,0)&&!damage.complete&&!damage.captured_complete,
          "changed visibility cannot reuse outside-support facts");
    stationary.change_visibility=false;collect(stationary,damage,{7,3,17});stationary.displaced=true;
    check(!refresh_stationary_packet(stationary,damage,damage,{7,3,18},10,0)&&!damage.complete,
          "unexpected displacement rejects stationary refresh before group observations");
    stationary.displaced=false;collect(stationary,damage,{7,3,19});stationary.change_epoch=true;
    check(!refresh_stationary_packet(stationary,damage,damage,{7,3,20},10,0)&&!damage.complete,
          "final changed owner epoch rejects stationary publication");
    Observer layout_observer;Packet layout;
    collect(layout_observer,layout,{7,3,30});
    std::vector<DesignSource<Group>> layout_groups;
    std::vector<DesignSource<Camp>> layout_camps;
    for(const auto& group:layout.groups)layout_groups.push_back({group});
    for(const auto& camp:layout.camps)layout_camps.push_back({camp});
    Packet static_packet;const Board static_mask{42};
    check(!advance_packet_layout(static_packet,static_mask,layout_groups,layout_camps,false)&&
          !static_packet.layout_initialized&&static_packet.groups.empty()&&static_packet.camps.empty(),
          "refused first-packet setup leaves static layout unpublished");
    check(!advance_packet_layout(static_packet,static_mask,layout_groups,layout_camps,true)&&
          static_packet.layout_initialized&&!static_packet.complete&&!static_packet.captured_complete&&
          static_packet.static_blocked.token==42&&static_packet.groups[0].members==layout.groups[0].members&&
          static_packet.camps.size()==layout.camps.size()&&static_packet.camps[2].handle==20,
          "admitted setup retains every canonical identity and group sibling in its own input");
    const auto* static_members=static_packet.groups[0].members.data();
    const auto* static_camps=static_packet.camps.data();
    check(advance_packet_layout(static_packet,static_mask,layout_groups,layout_camps,false)&&
          static_packet.groups[0].members.data()==static_members&&static_packet.camps.data()==static_camps,
          "a later packet reuses prepared static storage without another allocation or copy");
    Packet partial_layout;int layout_admissions=2;
    check(!advance_packet_layout_guarded(partial_layout,static_mask,layout_groups,layout_camps,
              [&]{return layout_admissions-->0;})&&partial_layout.layout_group_cursor==1&&
          partial_layout.layout_camp_cursor==0&&!partial_layout.layout_initialized&&!partial_layout.complete,
          "static setup stops between whole group copies and preserves its exact continuation cursor");
    const auto* partial_members=partial_layout.groups[0].members.data();
    check(!advance_packet_layout_guarded(partial_layout,static_mask,layout_groups,layout_camps,[]{return false;})&&
          partial_layout.layout_group_cursor==1&&partial_layout.groups[0].members.data()==partial_members,
          "refused layout continuation does not recopy or discard a completed group");
    check(!advance_packet_layout_guarded(partial_layout,static_mask,layout_groups,layout_camps,[]{return true;})&&
          partial_layout.layout_initialized&&partial_layout.layout_group_cursor==layout_groups.size()&&
          partial_layout.layout_camp_cursor==layout_camps.size()&&partial_layout.groups[0].members.data()==partial_members&&
          !partial_layout.captured_complete,
          "later setup finishes all flat identity rows without publishing any live facts");
    std::vector<DesignSource<Group>> full_groups(28);
    std::vector<DesignSource<Camp>> full_camps(160);
    std::size_t next_member=0;
    for(std::size_t group_index=0;group_index<full_groups.size();++group_index) {
        auto& group=full_groups[group_index].design;group.id=static_cast<int>(group_index);
        const int count=group_index==0?30:(group_index<=22?5:4);
        for(int member=0;member<count;++member) {
            auto& camp=full_camps[next_member].design;
            camp.handle=static_cast<int>(next_member);camp.group=group.id;camp.member=member;
            group.members.push_back(next_member++);
        }
    }
    Packet full_layout;int full_group_admissions=29;
    advance_packet_layout_guarded(full_layout,static_mask,full_groups,full_camps,[&]{return full_group_admissions-->0;});
    int flat_admissions=2;
    advance_packet_layout_guarded(full_layout,static_mask,full_groups,full_camps,[&]{return flat_admissions-->0;});
    const auto flat_prefix=full_layout.layout_camp_cursor;
    advance_packet_layout_guarded(full_layout,static_mask,full_groups,full_camps,[]{return true;});
    check(flat_prefix==32&&full_layout.layout_initialized&&full_layout.camps.size()==160&&
          full_layout.groups[0].members.size()==30&&std::all_of(full_layout.camps.begin(),full_layout.camps.end(),
              [index=0](const Camp& camp) mutable{return camp.handle==index++;})&&!full_layout.complete,
          "split static setup preserves all160 canonical members and the real30-member group");
    SourceWorld scratch_world;scratch_world.initialized=true;
    GroupMemory first_memory;first_memory.observed_round=4;first_memory.members={{120,0,4,true,false},{60,20,3,true,true}};
    GroupMemory second_memory;second_memory.observed_round=2;second_memory.members={{30,15,2,true,true}};
    scratch_world.memory={first_memory,second_memory};
    int scratch_admissions=2;
    check(!scratch_world.advance_memory_scratch([&]{return scratch_admissions-->0;})&&
          scratch_world.memory_scratch_cursor==1&&!scratch_world.memory_scratch_ready&&
          scratch_world.scratch_memory[0].observed_round==4&&scratch_world.scratch_memory[0].members[1].observed_round==3,
          "cold memory storage clones one admitted group while retaining each factual observation date");
    const auto* scratch_members=scratch_world.scratch_memory[0].members.data();
    check(!scratch_world.advance_memory_scratch([]{return false;})&&scratch_world.memory_scratch_cursor==1&&
          scratch_world.scratch_memory[0].members.data()==scratch_members,
          "refused cold-memory continuation retains the completed prefix unchanged");
    check(!scratch_world.advance_memory_scratch([]{return true;})&&scratch_world.memory_scratch_ready&&
          scratch_world.memory_scratch_cursor==2&&scratch_world.scratch_memory[1].observed_round==2&&
          scratch_world.advance_memory_scratch([]{return false;}),
          "finishing cold memory owns its input and only a later call admits dynamic collection");
    scratch_world.scratch_memory[0].members[0]={1,999,99,true,true};
    scratch_world.scratch_memory=scratch_world.memory;
    check(scratch_world.scratch_memory[0].members.data()==scratch_members&&
          scratch_world.scratch_memory[0].members[0].hp==120&&
          scratch_world.scratch_memory[0].members[0].observed_round==4&&scratch_world.memory[0].members[0].hp==120,
          "a failed staged live read is overwritten by dated committed memory without reallocating or contaminating it");
    SourceWorld indexed_world;
    check(!indexed_world.local_support,"limited observation support defaults off");
    indexed_world.set_local_support(true);
    check(indexed_world.local_support,"limited observation support requires its explicit collector opt-in");
    indexed_world.set_spatial_model(true);
    check(!indexed_world.advance_spatial_initialization(SourceWorld::spatial_initialization_admission+1,[]{return true;})&&
          !indexed_world.spatial_built&&!indexed_world.spatial_support.valid(),
          "index construction cannot begin before metadata completion");
    indexed_world.set_spatial_model(false);
    for(const auto& camp:layout.camps) {
        auto handle=cambc::detail::handle_access::make_camp(static_cast<cambc::handle_camp_t>(camp.handle),
            static_cast<cambc::handle_camp_group_t>(camp.group),cambc::camp_type::wolf,camp.spawn);
        indexed_world.camps.push_back({handle,camp});
    }
    indexed_world.initialized=true;
    check(!indexed_world.prepare_spatial_support([]{return true;})&&!indexed_world.spatial_support.valid(),
          "spatial model remains disabled without explicit opt-in");
    indexed_world.set_spatial_model(true);
    const auto* retained_metadata=indexed_world.camps.data();
    const auto retained_count=indexed_world.camps.size();
    check(!indexed_world.prepare_spatial_support([]{return false;})&&!indexed_world.spatial_built&&
          indexed_world.spatial_initialization_pending(),
          "refused index admission neither marks a build nor consumes completed metadata");
    check(!indexed_world.advance_spatial_initialization(SourceWorld::spatial_initialization_admission,[]{return true;})&&
          !indexed_world.spatial_built&&indexed_world.camps.data()==retained_metadata&&
          indexed_world.camps.size()==retained_count,
          "at-threshold headroom defers the index while retaining all monotone metadata");
    check(!indexed_world.advance_spatial_initialization(SourceWorld::spatial_initialization_admission+1,[]{return true;})&&
          indexed_world.spatial_built&&indexed_world.camps.data()==retained_metadata,
          "an admitted index finishes but never stacks dynamic observation on its build input");
    check(indexed_world.advance_spatial_initialization(0,[]{return false;})&&indexed_world.prepare_spatial_support([]{return false;})&&
          indexed_world.spatial_support.count()==layout.camps.size(),
          "accepted model builds one index from the full canonical source order");
    layout_observer.spatial_token=indexed_world.spatial_support.borrow();
    collect(layout_observer,layout,{7,3,31});
    const auto full_token=layout.spatial_support;
    const auto* layout_owner=&indexed_world.spatial_support;
    check(full_token.valid(layout.camps.size())&&full_token.index==layout_owner&&
          full_token.index->hits(6,5,0)[0]==0&&full_token.index->hits(8,5,0)[0]==1,
          "published canonical handle order agrees with the borrowed source indices");
    refresh_self_packet(layout_observer,layout,layout,{7,3,32});
    const auto self_token=layout.spatial_support;
    refresh_stationary_packet(layout_observer,layout,layout,{7,3,33},10,0);
    check(layout.complete&&layout.spatial_support.valid(layout.camps.size())&&
          layout.spatial_support.index==layout_owner&&self_token.index==layout_owner,
          "full self-only and stationary packets preserve the same canonical index association");
    SourceWorld fork=indexed_world;
    layout_observer.spatial_token=fork.spatial_support.borrow();
    collect(layout_observer,layout,{7,3,34});
    check(layout.spatial_support.index==&fork.spatial_support&&layout.spatial_support.index!=layout_owner&&
          layout.spatial_support.valid(layout.camps.size()),
          "a copied world's new observation borrows its own index rather than the original pointer");
    auto observed=indexed_world.camps[0].design;
    check(!indexed_world.observe_spatial(0,observed)&&full_token.valid(indexed_world.camps.size()),
          "matching already-read observations keep the index valid without rebuilding");
    ++observed.pos.x;
    check(indexed_world.observe_spatial(0,observed)&&indexed_world.spatial_model_violated&&
          !full_token.valid(indexed_world.camps.size())&&!self_token.valid(indexed_world.camps.size()),
          "a contradictory live position immediately revokes every prior borrowed token");
    indexed_world.set_spatial_model(false);indexed_world.set_spatial_model(true);
    check(!indexed_world.prepare_spatial_support([]{return true;}),
          "a contradicted spatial model cannot silently rebuild on later revisions");
    observed=fork.camps[0].design;++observed.handle;
    check(fork.observe_spatial(0,observed)&&!layout.spatial_support.valid(layout.camps.size()),
          "source-index handle mismatch revokes a copied world's association");
    SourceWorld range_world;
    range_world.camps=indexed_world.camps;range_world.initialized=true;range_world.set_spatial_model(true);
    range_world.prepare_spatial_support([]{return true;});observed=range_world.camps[0].design;++observed.range;
    check(range_world.observe_spatial(0,observed)&&!range_world.spatial_support.valid(),
          "a contradictory observed design range revokes spatial exclusion");
    SourceWorld oversized;
    oversized.camps.assign(mosaic::spatial::capacity+1,indexed_world.camps[0]);
    oversized.initialized=true;oversized.set_spatial_model(true);
    check(!oversized.advance_spatial_initialization(SourceWorld::spatial_initialization_admission+1,[]{return true;})&&
          !oversized.spatial_built&&oversized.spatial_unsupported&&oversized.advance_spatial_initialization(0,[]{return false;})&&
          !oversized.spatial_support.borrow().valid(oversized.camps.size()),
          "unsupported capacity leaves no token and preserves the full-scan fallback");
    SourceWorld incremental_world;incremental_world.initialized=true;incremental_world.set_spatial_model(true);
    for(const auto& source:full_camps) {
        auto camp=source.design;camp.pos=camp.spawn={camp.handle%40,camp.handle/40};camp.range=3;
        auto handle=cambc::detail::handle_access::make_camp(static_cast<cambc::handle_camp_t>(camp.handle),
            static_cast<cambc::handle_camp_group_t>(camp.group),cambc::camp_type::wolf,camp.spawn);
        incremental_world.camps.push_back({handle,camp});
    }
    const auto* canonical_storage=incremental_world.camps.data();
    int append_admissions=5;
    check(!incremental_world.advance_spatial_initialization(SourceWorld::spatial_initialization_admission+1,
              [&]{return append_admissions-->0;})&&incremental_world.spatial_started&&!incremental_world.spatial_built&&
          !incremental_world.spatial_unsupported&&incremental_world.spatial_support.input_count()==4&&
          !incremental_world.spatial_support.borrow().valid(160),
          "guarded index setup retains a canonical append prefix without a temporary full-array copy or token");
    const auto partial_generation=incremental_world.spatial_support.generation();
    check(!incremental_world.advance_spatial_initialization(SourceWorld::spatial_initialization_admission+1,[]{return false;})&&
          incremental_world.spatial_support.input_count()==4&&incremental_world.spatial_support.generation()==partial_generation,
          "refused index continuation neither restarts nor discards its canonical prefix");
    int finish_append_admissions=157;
    incremental_world.advance_spatial_initialization(SourceWorld::spatial_initialization_admission+1,
        [&]{return finish_append_admissions-->0;});
    int one_sort_batch=1;
    incremental_world.advance_spatial_initialization(SourceWorld::spatial_initialization_admission+1,
        [&]{return one_sort_batch-->0;});
    check(incremental_world.spatial_support.input_count()==160&&
          incremental_world.spatial_support.status()==mosaic::spatial::BuildState::building&&
          !incremental_world.spatial_built&&!incremental_world.spatial_unsupported&&
          !incremental_world.spatial_support.borrow().valid(160)&&one_sort_batch==-1,
          "each admitted sorting batch is bounded and partial160-member sorting cannot publish a token");
    check(!incremental_world.advance_spatial_initialization(SourceWorld::spatial_initialization_admission+1,[]{return true;})&&
          incremental_world.spatial_built&&incremental_world.camps.data()==canonical_storage&&
          incremental_world.spatial_support.borrow().valid(160)&&incremental_world.spatial_support.hits(39,3,0)[0]==159&&
          incremental_world.advance_spatial_initialization(0,[]{return false;}),
          "finished incremental index preserves all canonical identities and yields before later live collection");
    check(cambc::detail::pure_camp_queries==0&&cambc::detail::pure_board_copies==0,
          "building observing and copying canonical metadata invokes no live camp SDK query");
    std::printf("frame checks: %s\n",failures==0?"passed":"failed");
    return failures?1:0;
}
