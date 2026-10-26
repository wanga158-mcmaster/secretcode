#include "candidate_observation.h"
#include <cstdio>

// SDK startup stubs only. The collector fixtures use a stage double, not a game.
namespace cambc::detail {
int host_camp_queries{};
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
extern "C" std::int32_t sdk_Bitboard_Zero(std::uint64_t* out){*out=0;return 0;}
extern "C" std::int32_t sdk_my_team(){return 0;}
extern "C" std::int32_t sdk_spawn_region(std::int32_t,aabb* out){*out={};return 0;}
extern "C" std::int32_t sdk_Bitboard_Set(std::uint64_t*,std::int32_t,std::int32_t,std::int32_t){return 0;}
extern "C" std::int32_t sdk_bc_get_last_error(char*,std::uint32_t){return 0;}
extern "C" std::int32_t sdk_champion(std::uint32_t,champion_info* out){*out={};return 0;}
extern "C" std::int32_t sdk_champion_ability_props(std::uint32_t,std::uint32_t,ability_props* out){*out={};return 0;}
extern "C" std::int32_t sdk_camp(std::uint32_t,camp_state* out){++host_camp_queries;*out={};return 0;}
extern "C" std::int32_t sdk_camp_design(std::int32_t,camp_design* out){++host_camp_queries;*out={};return 0;}
}

namespace {
using namespace mosaic;
int failures{};
void check(bool ok,const char* name){if(!ok){++failures;std::printf("FAIL %s\n",name);}}
struct Fixture {
    std::vector<frame::SourceCamp> catalog;
    spatial::Index index;
    Fixture() {
        std::vector<spatial::Entry> entries;
        for(int i=0;i<160;++i) {
            frame::Camp camp;camp.handle=100+i;camp.group=i<2?0:(i==2?1:2);camp.member=i;
            camp.spawn=camp.pos=i==0?cambc::vec2{6,5}:(i==1?cambc::vec2{8,5}:
                (i==2?cambc::vec2{7,6}:cambc::vec2{100+i,100}));
            camp.range=i==0?1:(i==1?4:(i==2?3:1));camp.hp=camp.design_hp=i==0?50:80;
            camp.strength=camp.design_strength=i==0?5:(i==1?10:20);
            auto handle=cambc::detail::handle_access::make_camp(static_cast<cambc::handle_camp_t>(camp.handle),
                static_cast<cambc::handle_camp_group_t>(camp.group),cambc::camp_type::wolf,camp.spawn);
            catalog.push_back({handle,camp});entries.push_back({camp.handle,camp.spawn.x,camp.spawn.y,camp.range});
        }
        index.build(entries,true);
    }
};
struct Observer {
    selected::Stamp stamp;
    int epoch{7},actor{3},seeds{},bodies{},buffs{},commits{},moves{},enemy_reads{},self_reads{},fences{};
    int unknown{-1},dead{-1},suspend_after{-1},stop_after{-1},group_reads{};
    bool clear_effect{true},move_legal{true},far_origin{},suspend_commit{},respawning_group{};
    std::vector<std::size_t> order;
    bool begin(selected::Stamp input){stamp=input;return epoch==input.epoch&&actor==input.actor;}
    bool same_input()const{return epoch==stamp.epoch&&actor==stamp.actor;}
    bool fence(std::uint64_t){++fences;return epoch==stamp.epoch&&actor==stamp.actor&&(stop_after<0||bodies<stop_after);}
    bool read_self(frame::Self& self) {
        ++self_reads;self.pos=far_origin?cambc::vec2{500,500}:cambc::vec2{4,5};self.hp=self.max_hp=200;
        self.strength=self.basic_damage=40;self.basic_damage_known=self.damage_modifiers_known=true;
        self.alive=self.can_attack=self.can_move=true;self.range=3;self.move_speed=1;self.splash_radius=1;return true;
    }
    bool read_enemies(selected::Packet&){++enemy_reads;return true;}
    bool effect_clear(cambc::vec2,int){return clear_effect;}
    void seed(std::size_t,frame::Camp& camp) {
        ++seeds;frame::restore_camp_memory(camp,{camp.design_hp,camp.design_strength,2,true,false});
    }
    void group_fact(int id,frame::Group& group){group.id=id;group.possibly_woken=true;}
    bool read_body(selected::Row& row) {
        ++bodies;order.push_back(row.index);
        if(int(row.index)!=unknown) {
            row.state_fresh=true;row.camp.known=row.camp.visible=true;row.camp.alive=int(row.index)!=dead;
            row.record_fresh=row.camp.alive;
            row.camp.hp=row.camp.design_hp;row.camp.strength=row.camp.design_strength;row.camp.observed_round=stamp.epoch;
            if(!row.camp.alive)row.camp.hp=0;
            if(row.effect&&row.camp.alive){++buffs;row.buffs_fresh=row.camp.buffs_known=true;row.camp.shield=row.index==0?25:0;}
        }
        if(bodies==suspend_after)++epoch;
        return true;
    }
    bool move_test(const frame::Self&,cambc::vec2,bool& legal){++moves;legal=move_legal;return true;}
    bool refresh_dead_group(const selected::Row&,frame::Group& group) {
        ++group_reads;group.observed_round=stamp.epoch;
        group.state=respawning_group?frame::GroupState::respawning:frame::GroupState::alive;return true;
    }
    void commit(const selected::Packet&){++commits;if(suspend_commit)++epoch;}
};
selected::Request request(transition::Kind kind,std::uint64_t revision=1) {
    transition::Command command;command.kind=kind;command.expected_epoch=7;command.expected_actor=3;
    command.expected_revision=revision;
    if(kind==transition::Kind::basic){command.target_kind=transition::TargetKind::camp;command.target_id=100;command.target_index=0;}
    if(kind==transition::Kind::move)command.destination={5,5};
    return {command,42};
}
candidate::Status capture(Observer& observer,Fixture& fixture,selected::Packet& packet,const selected::Request& input) {
    return candidate::collect(observer,packet,{7,3,input.command.expected_revision},input,
        {fixture.catalog.data(),fixture.catalog.size()},fixture.index.borrow(),input.command.expected_revision);
}
}

int main() {
    using namespace mosaic;
    Fixture fixture;Observer observer;selected::Packet packet;const auto basic=request(transition::Kind::basic);
    check(capture(observer,fixture,packet,basic)==candidate::Status::ready&&packet.complete&&
          packet.rows.size()==3&&observer.seeds==3&&observer.bodies==3&&observer.buffs==2&&observer.commits==1,
          "160 canonical identities produce only three selected rows with exact effect buffs and actual retaliation records");
    check(observer.order==std::vector<std::size_t>{1,2,0}&&packet.row(1)->retaliation_origin&&!packet.row(1)->effect&&
          !packet.row(1)->buffs_fresh&&packet.row(0)->effect&&packet.row(0)->camp.shield==25&&
          packet.catalog.size()==160&&packet.rows.size()!=packet.catalog.size(),
          "origin support is observed first while independent sibling and collateral roles remain complete");
    const auto before=std::array{observer.seeds,observer.bodies,observer.buffs,observer.enemy_reads,observer.self_reads};
    transition::Constraints constraints;constraints.require_cover=false;
    for(int i=0;i<100;++i)(void)selected::evaluate(packet,basic,constraints);
    check(before==std::array{observer.seeds,observer.bodies,observer.buffs,observer.enemy_reads,observer.self_reads},
          "one hundred pure candidate evaluations issue no observation-stage or member reads");
    const auto assessed=selected::evaluate(packet,basic,constraints);
    check(assessed.status==selected::Status::ready&&assessed.action.useful_hp_removed==55,
          "fresh shielded primary and collateral feed the real selected evaluator without assuming shield zero");
    Observer moving;selected::Packet move_packet;const auto move=request(transition::Kind::move,2);
    check(capture(moving,fixture,move_packet,move)==candidate::Status::ready&&moving.moves==1&&moving.buffs==0&&
          move_packet.move_test_complete&&move_packet.move_legal&&move_packet.row(0)->retaliation_after&&
          move_packet.row(1)->retaliation_origin,
          "move observes exact destination legality and both success and refusal threat supports without hit buffs");
    Observer hidden;hidden.unknown=1;selected::Packet hidden_packet;
    check(capture(hidden,fixture,hidden_packet,basic)==candidate::Status::ready&&
          !hidden_packet.row(1)->state_fresh&&!hidden_packet.row(1)->camp.known&&
          selected::evaluate(hidden_packet,basic,constraints).status==selected::Status::incomplete,
          "hidden harmful sibling stays unknown and cannot borrow a design-strength upper bound");
    Observer interrupted;interrupted.suspend_after=1;selected::Packet partial;
    check(capture(interrupted,fixture,partial,basic)==candidate::Status::stale&&!partial.complete&&!partial.origin_complete&&
          !partial.self_complete&&!partial.enemies_complete&&interrupted.commits==0&&
          !partial.row(1)->state_fresh&&!partial.row(1)->record_fresh&&
          !selected::evaluate(partial,basic,constraints).origin_wait.complete,
          "an epoch change inside collection revokes the packet before memory commit or actuation");
    Observer origin_only;origin_only.stop_after=2;selected::Packet origin_packet;
    check(capture(origin_only,fixture,origin_packet,basic)==candidate::Status::incomplete&&!origin_packet.complete&&
          selected::evaluate(origin_packet,basic,constraints).origin_wait.complete&&
          selected::evaluate(origin_packet,basic,constraints).origin_wait.safe&&origin_only.commits==0,
          "fresh origin support prices the actual wait even when the selected effect collection cannot finish");
    Observer after_origin;after_origin.suspend_after=3;selected::Packet crossed_packet;
    check(capture(after_origin,fixture,crossed_packet,basic)==candidate::Status::stale&&!crossed_packet.origin_complete&&
          !crossed_packet.row(0)->state_fresh&&!selected::evaluate(crossed_packet,basic,constraints).origin_wait.complete,
          "a later effect read crossing the input revokes even an earlier complete origin subset");
    Observer late_suspend;late_suspend.suspend_commit=true;selected::Packet late_packet;
    check(capture(late_suspend,fixture,late_packet,basic)==candidate::Status::stale&&!late_packet.complete&&!late_packet.origin_complete,
          "a final publication fence prevents resumed locals from publishing old-input authority");
    Observer dead_source;dead_source.dead=1;selected::Packet dead_packet;
    check(capture(dead_source,fixture,dead_packet,basic)==candidate::Status::ready&&dead_source.group_reads==1&&
          selected::evaluate(dead_packet,basic,constraints).origin_wait.complete&&
          dead_packet.groups[0].observed_round==7,
          "dead retaliation obtains actual current lifecycle instead of using dated memory to rule out respawn");
    Observer respawning;respawning.dead=1;respawning.respawning_group=true;selected::Packet respawn_packet;
    check(capture(respawning,fixture,respawn_packet,basic)==candidate::Status::ready&&
          !selected::evaluate(respawn_packet,basic,constraints).origin_wait.complete&&
          selected::evaluate(respawn_packet,basic,constraints).status==selected::Status::incomplete,
          "a currently respawning harmful group remains incomplete without a proved next-phase generation bound");
    Observer next_input;next_input.far_origin=true;const auto wait=request(transition::Kind::wait,3);
    check(capture(next_input,fixture,partial,wait)==candidate::Status::ready&&partial.rows.empty()&&next_input.seeds==0&&
          partial.stamp.revision==3&&partial.request.serial==42,
          "the next request clears old sparse deltas instead of completing a prior partial overlay");
    Observer collateral_enemy;collateral_enemy.clear_effect=false;selected::Packet enemy_packet;
    check(capture(collateral_enemy,fixture,enemy_packet,basic)==candidate::Status::ready&&
          !enemy_packet.effect_champions_clear&&selected::evaluate(enemy_packet,basic,constraints).status==selected::Status::unsupported,
          "unproved enemy-free splash footprint is unsupported rather than silently dropping champion collateral");
    Observer stale;selected::Packet stale_packet;auto stale_request=basic;stale_request.command.expected_epoch=6;
    check(candidate::collect(stale,stale_packet,{7,3,1},stale_request,{fixture.catalog.data(),fixture.catalog.size()},
          fixture.index.borrow(),7)==candidate::Status::stale&&stale.self_reads==0&&!stale_packet.complete,
          "stale request authority is rejected before any live observation");
    const auto old_token=fixture.index.borrow();fixture.index.revoke();Observer revoked;selected::Packet revoked_packet;
    check(candidate::collect(revoked,revoked_packet,{7,3,1},basic,{fixture.catalog.data(),fixture.catalog.size()},old_token,8)==
          candidate::Status::unsupported&&revoked.self_reads==0,
          "a revoked layout cannot certify a sparse support or fall back to guessed remote data");
    candidate::Proposal proposal;proposal.command=basic.command;proposal.serial=99;proposal.nominated_epoch=2;
    const auto rebound=proposal.request({10,3,8});
    check(rebound.serial==99&&rebound.command.expected_epoch==10&&rebound.command.expected_revision==8&&
          proposal.nominated_epoch==2,
          "a dated proposal can request fresh validation while retaining its original nomination date");
    check(cambc::detail::host_camp_queries==0,"stage-double tests make no native camp queries or hidden SDK evaluations");
    std::printf("candidate observation checks: %s\n",failures?"failed":"passed");return failures?1:0;
}
