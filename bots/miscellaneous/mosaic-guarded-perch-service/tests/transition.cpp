#include "transition.h"
#include <array>
#include <cassert>
#include <cstdio>

// SDK static initialization needs design/settings imports. Count them separately:
// every evaluator call below must add zero host calls after that initialization.
int sdk_calls{};
namespace cambc::detail {
extern "C" std::int32_t sdk_get_settings(game_settings* out) { ++sdk_calls;*out={};return 0; }
extern "C" std::int32_t sdk_shared_memory_size(std::uint32_t* out) { ++sdk_calls;*out=8;return 0; }
extern "C" std::int32_t sdk_bc_metadata(metadata* out) { ++sdk_calls;*out={5,4,1,1,1,8,128};return 0; }
extern "C" std::int32_t sdk_consumable_info(std::uint32_t,consumable_info* out) { ++sdk_calls;*out={};return 0; }
extern "C" std::int32_t sdk_champion_design(std::int32_t,champion_design* out) { ++sdk_calls;*out={};return 0; }
extern "C" std::int32_t sdk_draft_memory_size(std::uint32_t* out) { ++sdk_calls;*out=0;return 0; }
extern "C" std::int32_t sdk_read_draft_memory(std::int32_t,void*,std::uint32_t) { ++sdk_calls;return 0; }
extern "C" std::int32_t sdk_my_champion() { ++sdk_calls;return -1; }
extern "C" std::int32_t sdk_bc_map_symmetry(std::int32_t* out) { ++sdk_calls;*out=0;return 0; }
extern "C" std::int32_t sdk_bc_map_mask(std::int32_t,std::uint64_t* out) { ++sdk_calls;*out=0;return 0; }
extern "C" std::int32_t sdk_Bitboard_Zero(std::uint64_t* out) { ++sdk_calls;*out=0;return 0; }
extern "C" std::int32_t sdk_my_team() { ++sdk_calls;return 0; }
extern "C" std::int32_t sdk_spawn_region(std::int32_t,aabb* out) { ++sdk_calls;*out={};return 0; }
extern "C" std::int32_t sdk_Bitboard_Set(std::uint64_t*,std::int32_t,std::int32_t,std::int32_t) { ++sdk_calls;return 0; }
extern "C" std::int32_t sdk_bc_get_last_error(char*,std::uint32_t) { ++sdk_calls;return 0; }
extern "C" std::int32_t sdk_champion(std::uint32_t,champion_info* out) { ++sdk_calls;*out={};return 0; }
extern "C" std::int32_t sdk_champion_ability_props(std::uint32_t,std::uint32_t,ability_props* out) { ++sdk_calls;*out={};return 0; }
}

using namespace mosaic;
using namespace mosaic::transition;
using Packet=frame::Snapshot<int>;

Packet packet() {
    Packet f;f.complete=true;f.epoch=10;f.actor=2;f.revision=7;
    auto& s=f.self;s.alive=true;s.hp=s.max_hp=100;s.strength=40;s.range=1;s.move_speed=1;
    s.can_move=s.can_attack=s.can_cast=true;s.pos={0,0};s.camp_multiplier=1;
    s.basic_damage=40;s.basic_damage_known=true;s.damage_modifiers_known=true;
    s.camp_taken_multiplier=s.champion_taken_multiplier=1;
    return f;
}
void camp(Packet& f,int id,int group,int x,int y,float hp,float strength=0,int range=3) {
    frame::Camp c;c.handle=id;c.group=group;c.member=id;c.pos=c.spawn={x,y};
    c.hp=c.design_hp=hp;c.strength=c.design_strength=strength;c.range=range;
    c.known=c.visible=c.alive=c.possible_alive=c.buffs_known=true;f.camps.push_back(c);
}
Command basic(const Packet& f,int id) {auto c=command(f,Kind::basic);c.target_kind=TargetKind::camp;c.target_id=id;return c;}
frame::Ability reset(int slot=0) {frame::Ability a;a.slot=slot;a.ready=a.reset=true;a.self=true;return a;}
void bind_domain(Packet& f,spatial::Index& index) {
    std::vector<spatial::Entry> layout;
    for(const auto& c:f.camps)layout.push_back({c.handle,c.pos.x,c.pos.y,c.range});
    assert(index.build(layout,true));f.spatial_support=index.borrow();
    f.domain=domain::make_limited({f.epoch,f.actor,f.revision},f.spatial_support,f.camps.size(),domain::geometry(f.self,f.abilities));
    assert(f.domain.complete);
}

int main() {
    int checks=0;
    const int initialized_calls=sdk_calls;
    {
        auto f=packet();camp(f,1,1,1,0,100);
        auto c=basic(f,1);++c.expected_revision;
        assert(!evaluate(f,c).admissible);++checks;
        c=basic(f,1);f.self.can_attack=false;assert(!evaluate(f,c).admissible);++checks;
    }
    {
        auto f=packet();f.abilities[0]=reset();auto c=command(f,Kind::reset);c.slot=0;
        const auto bad=evaluate(f,c);assert(!bad.admissible&&(bad.flags&reset_requires_spent_attack));++checks;
        // A ranged step spends attack availability without an applied basic.
        f.self.range=3;f.self.moved=true;f.self.can_move=f.self.can_attack=false;
        const auto good=evaluate(f,c);assert(good.eligible&&good.after_self.can_attack&&!good.after_self.can_cast&&!good.after_self.can_move);++checks;
        f.abilities[0].exclusive=true;
        const auto exclusive=evaluate(f,c);assert(!exclusive.admissible&&(exclusive.flags&exclusive_move));++checks;
    }
    {
        auto f=packet();camp(f,1,1,1,0,100);
        auto result=evaluate(f,basic(f,1));assert(result.eligible&&result.after_self.can_move&&!result.after_self.can_attack&&result.camps[0].after_hp==60);++checks;
        const auto again=evaluate(f,basic(f,1));assert(f.camps[0].hp==100&&again.camps[0].after_hp==result.camps[0].after_hp);++checks;
        f.self.range=3;result=evaluate(f,basic(f,1));assert(result.eligible&&!result.after_self.can_move);++checks;
        f.self.range=1;f.self.hp=20;f.self.basic_damage_known=false;f.camps[0].hp=10;f.camps[0].strength=50;
        result=evaluate(f,basic(f,1));assert(!result.effect_exact&&result.next_volley==50&&!result.eligible);++checks;
    }
    {
        auto f=packet();f.self.splash_radius=1;
        camp(f,1,1,1,0,100,20);camp(f,2,1,1,1,100,20);
        camp(f,3,1,2,0,100,20);camp(f,4,1,2,1,100,20);
        camp(f,5,1,4,0,100,60,4); // Unhit sibling wakes with its group.
        auto result=evaluate(f,basic(f,1));
        assert(result.camps.size()==4&&result.useful_hp_removed==160&&result.next_volley==140&&!result.survives_prefix&&!result.eligible);++checks;
        for(int i=0;i<4;++i)f.camps[i].hp=40;
        result=evaluate(f,basic(f,1));assert(result.next_volley==60&&result.survives_prefix&&result.hp_after_prefix==40);++checks;
        f.camps[4].known=f.camps[4].visible=false;
        result=evaluate(f,basic(f,1));assert(result.next_volley==60&&!result.forecast_complete&&!result.eligible);++checks;
    }
    {
        auto f=packet();f.self.hp=40;f.self.move_speed=2;f.move_destinations.push_back({2,0});
        camp(f,1,1,5,0,100,50,3);f.camps[0].awake=true;
        auto c=command(f,Kind::move);c.destination={2,0};const auto result=evaluate(f,c);
        assert(result.admissible&&result.next_volley==50&&!result.survives_prefix);++checks;
        c.destination={1,0};assert(!evaluate(f,c).admissible);++checks;
    }
    {
        const std::array events{Event{EventKind::damage,10},Event{EventKind::heal,20}};
        const auto p=prefixes(5,30,0,events);assert(!p.alive&&p.hp==0&&p.max_prefix_loss==5);++checks;
        const std::array expires{Event{EventKind::expire_shield,0},Event{EventKind::damage,10}};
        const std::array protects{Event{EventKind::damage,10},Event{EventKind::expire_shield,0}};
        assert(!prefixes(5,5,10,expires).alive&&prefixes(5,5,10,protects).alive);++checks;
        const std::array five{Event{EventKind::damage,129.8f},Event{EventKind::damage,129.8f},Event{EventKind::damage,129.8f},Event{EventKind::damage,129.8f},Event{EventKind::damage,129.8f}};
        const auto q=prefixes(649,649,0,five);assert(q.alive&&q.hp>0&&q.hp<0.001f);++checks;
    }
    {
        auto f=packet();f.self.hp=5;f.self.shield=10;f.self.shield_duration=1;
        camp(f,1,1,1,0,100,10);f.camps[0].awake=true;
        auto c=command(f,Kind::wait);const auto conservative=evaluate(f,c);
        assert(!conservative.eligible&&(conservative.flags&short_shield_ignored));++checks;
        Constraints context;context.credit_short_shield=true;
        const auto conditional=evaluate(f,c,context);assert(conditional.eligible&&conditional.hp_after_prefix==5);++checks;
    }
    {
        auto f=packet();f.self.pos={0,2};f.self.damage_modifiers_known=false;
        int id=1;for(int y=1;y<=3;++y)for(int x=1;x<=3;++x)camp(f,id++,1,x,y,120,0,0);
        auto& a=f.abilities[0];a.slot=0;a.ready=a.damage=a.aoe=a.camps=a.ground=true;
        a.range=4;a.radius=2;a.damage_amount=120;a.legal_targets.push_back({2,2});
        auto c=command(f,Kind::cast);c.slot=0;c.target_kind=TargetKind::ground;c.destination={2,2};
        const auto result=evaluate(f,c);assert(result.eligible&&result.camps.size()==9&&result.useful_hp_removed==1080&&result.next_volley==0);++checks;
        PlanningContext context(f);assert(nominal_progress(context,c,1)==1080&&nominal_progress(context,c,2)==0);++checks;
        Constraints bounded;bounded.max_candidates=1;const auto menu=enumerate(f,bounded);
        assert(!menu.complete&&!menu.values.empty());++checks;
    }
    {
        auto f=packet();auto c=command(f,Kind::recall);
        const auto result=evaluate(f,c);assert(result.admissible&&!result.supported&&!result.eligible);++checks;
    }
    {
        auto f=packet();f.self.hp=20;camp(f,1,1,1,0,10,50);f.camps[0].shield=100;
        auto result=evaluate(f,basic(f,1));
        assert(result.effect_exact&&result.camps[0].after_hp==10&&result.next_volley==50&&!result.eligible);++checks;
        f.camps[0].shield=0;f.camps[0].buffs_known=false;
        result=evaluate(f,basic(f,1));
        assert(!result.effect_exact&&result.next_volley==50&&!result.eligible);++checks;
    }
    {
        auto f=packet();for(int i=0;i<161;++i)camp(f,i,1,1,0,100);
        const auto result=evaluate(f,command(f,Kind::wait));
        assert(!result.supported&&!result.eligible&&(result.flags&truncated));++checks;
    }
    {
        auto f=packet();camp(f,1,1,1,0,10,10);camp(f,2,1,6,0,100,10,6);
        PlanningContext direct(f,ContextMode::direct);
        const auto hit=evaluate(direct,basic(f,1));
        assert(hit.eligible&&hit.camps.size()==1&&hit.camps[0].after_hp==0&&hit.next_volley==10);++checks;
        ++f.revision;assert(!evaluate(direct,command(f,Kind::wait)).admissible);++checks;
        f.self.can_attack=false;f.abilities[0]=reset();direct.reset(f);
        auto c=command(f,Kind::reset);c.slot=0;
        assert(evaluate(direct,c).followup_hp_removed==10);++checks;
    }
    {
        auto f=packet();camp(f,27,1,1,0,20,20);camp(f,20,1,2,0,100,30);
        f.camps[1].known=f.camps[1].visible=false;f.camps[1].hp=0;
        const std::array layout{spatial::Entry{27,1,0,3},spatial::Entry{20,2,0,3}};
        spatial::Index index;assert(index.build(layout,true));f.spatial_support=index.borrow();
        PlanningContext sparse(f,ContextMode::spatial);
        auto result=evaluate(sparse,basic(f,27));
        assert(result.camps.size()==1&&result.next_volley==30&&!result.forecast_complete&&!result.eligible);++checks;
        assert(index.observe(0,27,1,0,3));f.camps[1].pos={20,0};
        assert(!index.observe(1,20,20,0,3));
        result=evaluate(sparse,basic(f,27));
        assert(!sparse.spatial_index()&&result.camps.size()==1&&result.next_volley==0);++checks;
        f.spatial_support={};result=evaluate(sparse,basic(f,27));
        assert(result.next_volley==0&&result.forecast_complete);++checks;
    }
    {
        auto f=packet();camp(f,1,1,1,0,100);camp(f,2,1,4,0,100);camp(f,3,2,20,0,100);
        auto& a=f.abilities[0];a.slot=0;a.range=4;a.radius=1;a.cooldown=5;a.ground=true;
        // Include the full geometric payload while cooling, even without a camp
        // unit-target flag: ground effects can still affect camp bodies.
        spatial::Index index;bind_domain(f,index);
        assert(f.domain.geometry.effect_reach==5&&f.domain.contains(0)&&f.domain.contains(1)&&!f.domain.contains(2));++checks;
        auto result=evaluate(f,basic(f,1));assert(result.eligible);++checks;
        f.domain.members[0]&=~std::uint64_t{1};
        result=evaluate(f,basic(f,1));assert(!result.admissible&&(result.flags&outside_domain)&&result.camps.empty());++checks;
        bind_domain(f,index);a.ready=a.damage=a.aoe=a.camps=true;a.cooldown=0;a.damage_amount=20;a.legal_targets={{4,0}};
        ++f.revision;
        assert(f.domain.rebind({f.epoch,f.actor,f.revision},f.spatial_support,f.camps.size(),domain::geometry(f.self,f.abilities)));++checks;
        auto cast=command(f,Kind::cast);cast.slot=0;cast.target_kind=TargetKind::ground;cast.destination={4,0};cast.target_id=1;
        result=evaluate(f,cast);assert(result.eligible&&result.camps.size()==1&&result.camps[0].handle==2);++checks;
        f.domain.members[0]&=~(std::uint64_t{1}<<1);
        result=evaluate(f,cast);assert(!result.admissible&&(result.flags&outside_domain));++checks;
        bind_domain(f,index);a.dash=true;
        result=evaluate(f,cast);assert(!result.admissible&&!result.supported&&(result.flags&unsupported_kind));++checks;
    }
    {
        auto f=packet();camp(f,1,1,4,0,100,30,3);camp(f,2,2,20,0,100);
        f.camps[0].known=f.camps[0].visible=false;f.camps[0].hp=0;
        f.move_destinations={{1,1},{5,5}};spatial::Index index;bind_domain(f,index);
        assert(f.domain.contains(0)&&!f.domain.contains(1));++checks;
        auto move=command(f,Kind::move);move.destination={1,1};
        auto result=evaluate(f,move);assert(result.admissible&&result.next_volley==30&&!result.forecast_complete);++checks;
        f.domain.members[0]&=~std::uint64_t{1};
        result=evaluate(f,move);assert(!result.admissible&&(result.flags&outside_domain));++checks;
        bind_domain(f,index);move.destination={5,5};
        result=evaluate(f,move);assert(!result.admissible&&(result.flags&outside_domain));++checks;
        ++f.revision;f.invalidate();
        assert(!evaluate(f,command(f,Kind::wait)).admissible);++checks;
        f.complete=true;
        result=evaluate(f,command(f,Kind::wait));assert(!result.admissible&&(result.flags&outside_domain));++checks;
        // Refusal/movement needs full recapture: a new packet revision alone is
        // not authority to reuse the old domain. Recreate it before continuing.
        bind_domain(f,index);
        ++f.revision; // A separately proved stationary/self refresh may rebind.
        assert(f.domain.rebind({f.epoch,f.actor,f.revision},f.spatial_support,f.camps.size(),domain::geometry(f.self,f.abilities)));++checks;
        assert(evaluate(f,command(f,Kind::wait)).eligible);++checks;
        auto changed=domain::geometry(f.self,f.abilities);++changed.move_speed;
        assert(!f.domain.rebind({f.epoch,f.actor,f.revision+1},f.spatial_support,f.camps.size(),changed)&&!f.domain.complete);++checks;
        bind_domain(f,index);index.revoke();
        result=evaluate(f,command(f,Kind::wait));assert(!result.admissible&&(result.flags&outside_domain));++checks;
    }
    {
        auto f=packet();camp(f,1,1,1,0,100);spatial::Index index;bind_domain(f,index);
        auto changed=domain::geometry(f.self,f.abilities);++changed.attack_range;
        assert(!f.domain.rebind({f.epoch,f.actor,f.revision+1},f.spatial_support,f.camps.size(),changed));++checks;
        bind_domain(f,index);changed=domain::geometry(f.self,f.abilities);++changed.passive;
        assert(!f.domain.rebind({f.epoch,f.actor,f.revision+1},f.spatial_support,f.camps.size(),changed));++checks;
        bind_domain(f,index);
        assert(!f.domain.rebind({f.epoch+1,f.actor,f.revision+1},f.spatial_support,f.camps.size(),domain::geometry(f.self,f.abilities)));++checks;
        bind_domain(f,index);spatial::Index foreign;
        const std::array foreign_layout{spatial::Entry{1,1,0,3}};
        assert(foreign.build(foreign_layout,true));f.spatial_support=foreign.borrow();
        const auto unrelated=evaluate(f,command(f,Kind::wait));
        assert(!unrelated.admissible&&(unrelated.flags&outside_domain));++checks;
        bind_domain(f,index);f.spatial_support={};
        const auto missing=evaluate(f,command(f,Kind::wait));
        assert(!missing.admissible&&(missing.flags&outside_domain));++checks;
        bind_domain(f,index);
        f.abilities[0].range=std::numeric_limits<float>::infinity();
        const auto invalid=domain::make_limited({f.epoch,f.actor,f.revision},f.spatial_support,f.camps.size(),domain::geometry(f.self,f.abilities));
        assert(invalid.limited&&!invalid.complete);++checks;
        f.domain={};auto& a=f.abilities[0];a.slot=0;a.ready=a.damage=a.aoe=a.camps=a.ground=true;
        a.range=1;a.radius=std::numeric_limits<float>::infinity();
        auto cast=command(f,Kind::cast);cast.slot=0;cast.target_kind=TargetKind::ground;cast.destination={1,0};
        const auto malformed=evaluate(f,cast);
        assert(!malformed.admissible&&!malformed.supported&&(malformed.flags&unsupported_kind));++checks;
    }
    assert(sdk_calls==initialized_calls);++checks;
    std::printf("transition checks: %d passed; candidate host calls: %d\n",checks,sdk_calls-initialized_calls);
    return 0;
}
