#include "task_actions.h"
#include <cassert>
#include <cstdio>

int sdk_calls{};
namespace cambc::detail {
extern "C" std::int32_t sdk_get_settings(game_settings* out) {++sdk_calls;*out={};return 0;}
extern "C" std::int32_t sdk_shared_memory_size(std::uint32_t* out) {++sdk_calls;*out=8;return 0;}
extern "C" std::int32_t sdk_bc_metadata(metadata* out) {++sdk_calls;*out={5,4,1,1,1,8,128};return 0;}
extern "C" std::int32_t sdk_consumable_info(std::uint32_t,consumable_info* out) {++sdk_calls;*out={};return 0;}
extern "C" std::int32_t sdk_champion_design(std::int32_t,champion_design* out) {++sdk_calls;*out={};return 0;}
extern "C" std::int32_t sdk_draft_memory_size(std::uint32_t* out) {++sdk_calls;*out=0;return 0;}
extern "C" std::int32_t sdk_read_draft_memory(std::int32_t,void*,std::uint32_t) {++sdk_calls;return 0;}
extern "C" std::int32_t sdk_my_champion() {++sdk_calls;return -1;}
extern "C" std::int32_t sdk_bc_map_symmetry(std::int32_t* out) {++sdk_calls;*out=0;return 0;}
extern "C" std::int32_t sdk_bc_map_mask(std::int32_t,std::uint64_t* out) {++sdk_calls;*out=0;return 0;}
extern "C" std::int32_t sdk_Bitboard_Zero(std::uint64_t* out) {++sdk_calls;*out=0;return 0;}
extern "C" std::int32_t sdk_my_team() {++sdk_calls;return 0;}
extern "C" std::int32_t sdk_spawn_region(std::int32_t,aabb* out) {++sdk_calls;*out={};return 0;}
extern "C" std::int32_t sdk_Bitboard_Set(std::uint64_t*,std::int32_t,std::int32_t,std::int32_t) {++sdk_calls;return 0;}
extern "C" std::int32_t sdk_bc_get_last_error(char*,std::uint32_t) {++sdk_calls;return 0;}
extern "C" std::int32_t sdk_champion(std::uint32_t,champion_info* out) {++sdk_calls;*out={};return 0;}
extern "C" std::int32_t sdk_champion_ability_props(std::uint32_t,std::uint32_t,ability_props* out) {++sdk_calls;*out={};return 0;}
}
using namespace mosaic;
using namespace mosaic::transition;
using Packet=frame::Snapshot<int>;
Packet packet() {
    Packet f;f.complete=true;f.epoch=10;f.actor=2;f.revision=1;
    auto& s=f.self;s.alive=true;s.hp=s.max_hp=100;s.strength=s.basic_damage=40;
    s.basic_damage_known=s.damage_modifiers_known=true;s.camp_multiplier=s.camp_taken_multiplier=s.champion_taken_multiplier=1;
    s.range=s.move_speed=1;s.can_move=s.can_attack=s.can_cast=true;s.pos={0,0};
    frame::Camp c;c.handle=5;c.group=0;c.pos=c.spawn={8,0};c.hp=c.design_hp=100;
    c.known=c.visible=c.alive=c.possible_alive=true;f.camps.push_back(c);
    frame::Group g;g.id=0;g.members={0};g.state=frame::GroupState::alive;f.groups.push_back(g);
    return f;
}
int main() {
    const int before=sdk_calls;
    auto f=packet();Constraints limits;limits.group=0;
    PlanningContext context(f);
    for(int turn=0;turn<2;++turn) {
        f.epoch=10+turn;f.revision=1;f.self.pos={turn,0};f.move_destinations={{turn+1,0}};
        context.reset(f);auto move=command(f,Kind::move);move.destination={turn+1,0};
        const auto nominated=task_actions::nominate(context,limits,move);
        assert(!nominated.progress&&nominated.movement);
        const auto certified=evaluate(context,*nominated.movement,limits);
        assert(certified.eligible&&certified.command.kind==Kind::move&&certified.after_self.pos.x==turn+1);
        assert(distance(certified.after_self.pos,f.camps[0].pos)<distance(f.self.pos,f.camps[0].pos));
    }
    // Nomination is not a survival certificate: the same advancing operator can fail.
    f.self.hp=20;f.camps[0].strength=50;f.camps[0].range=20;f.camps[0].awake=true;context.reset(f);
    auto move=command(f,Kind::move);move.destination=f.move_destinations.front();
    const auto nominated=task_actions::nominate(context,limits,move);
    assert(nominated.movement&&!evaluate(context,*nominated.movement,limits).eligible);
    // Legal progress is selected over movement when a current-task basic is in range.
    f=packet();f.camps[0].pos=f.camps[0].spawn={1,0};f.attack_targets={{1,0}};context.reset(f);
    const auto work=task_actions::nominate(context,limits);
    assert(work.progress&&work.progress->kind==Kind::basic&&evaluate(context,*work.progress,limits).eligible);
    // Cheap local geometry still nominates the center which completes the most
    // useful area work; unrelated world rows do not enter this target ranking.
    f=packet();f.camps.clear();f.groups[0].members.clear();f.self.range=1;
    for(int x=1;x<=3;++x)for(int y=0;y<3;++y) {
        frame::Camp c;c.handle=int(f.camps.size())+10;c.group=0;c.pos=c.spawn={x,y};
        c.hp=c.design_hp=100;c.visible=c.known=c.alive=c.possible_alive=true;
        f.groups[0].members.push_back(f.camps.size());f.camps.push_back(c);
    }
    auto& area=f.abilities[0];area.slot=0;area.ready=area.damage=area.aoe=area.ground=area.camps=true;
    area.range=3;area.radius=1;area.damage_amount=40;
    for(const auto& c:f.camps)area.legal_targets.push_back(c.pos);
    context.reset(f);const auto dense=task_actions::nominate(context,limits);
    assert(dense.progress&&dense.progress->kind==Kind::cast&&dense.progress->destination==cambc::vec2(2,1));
    assert(evaluate(context,*dense.progress,limits).camps.size()==9);
    assert(sdk_calls==before);
    std::puts("task action checks: passed");
}
