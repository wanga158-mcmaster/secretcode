#define P10_OBSERVATION_PURE_TEST
#include "../common/src/sdk.h"
#include <cassert>
#include <iostream>
#include <stdexcept>

#ifdef NDEBUG
#error Optimized observation regression requires assertions enabled.
#endif

namespace {
// Exact scalar shape of the pinned SDK champion_info. Production also asserts
// sizeof44 against the real header; the seam requires no guest engine imports.
struct Raw {
 std::int32_t team=0,type=0,x=0,y=0;
 float health=0,max_health=0,strength=0,focus=0;
 std::int32_t move_speed=1,attack_range=1,state=0;
};
static_assert(sizeof(Raw)==44);
constexpr p10::SnapshotFlags flags{1u<<0,1u<<3,1u<<4,1u<<11,1u<<2,1u<<1,1u<<12};
p10::Profile profile{"snapshot-fixture",{p10::Champion::vendetta,p10::Champion::marcella,p10::Champion::cassi,p10::Champion::willow,p10::Champion::tavi}};

void one_read_and_fresh_mutable_state(){
 int queries=0,checks=0;Raw source{0,2,4,5,450,800,76,100,1,1,int(flags.alive|flags.move|flags.attack|flags.cast)};
 auto query=[&](std::uint32_t id,Raw*out){assert(id==7);++queries;*out=source;return 0;};
 auto check=[&](int code){++checks;if(code)throw std::runtime_error("engine error");};
 Raw first;assert(p10::fresh_actor_snapshot(7,first,97,query,check));
 p10::Actor design;design.type=p10::Champion::vendetta;design.hp=design.max_hp=800;design.strength=70;design.focus=100;design.strength_growth=6;
 auto actor=p10::unpack_actor_snapshot(design,first,7,0,profile,flags,false);
 p10::observed_cooldowns(actor,{0,5});
 assert(queries==1&&checks==1);assert(actor.hp==450&&actor.max_hp==800&&actor.strength==76);
 assert(actor.position==(p10::Point{4,5})&&actor.id==7&&actor.pick==0&&actor.ally&&actor.visible&&actor.alive);
 assert(actor.can_move&&actor.can_attack&&actor.can_cast&&actor.channel==p10::Channel::none);
 assert(actor.abilities[0].ready&&!actor.abilities[1].ready&&actor.cooldown[1]==5);
 assert(actor.strength_growth==6);
 // A second observation changes every mutable combat field and allowances.
 // Immutable design reuse must not keep the first health, location or readiness.
 source={0,2,7,8,300,900,82,110,1,3,int(flags.alive|flags.tribute)};
 Raw second;assert(p10::fresh_actor_snapshot(7,second,97,query,check));
 auto changed=p10::unpack_actor_snapshot(design,second,7,0,profile,flags,true);
 p10::observed_cooldowns(changed,{0,0});
 assert(queries==2&&checks==2);assert(changed.hp==300&&changed.max_hp==900&&changed.strength==82&&changed.focus==110);
 assert(changed.position==(p10::Point{7,8})&&changed.range==3&&changed.at_spawn);
 assert(!changed.can_cast&&!changed.can_attack&&!changed.can_move);
 assert(!changed.abilities[0].ready&&!changed.abilities[1].ready);
 assert(changed.channel==p10::Channel::tribute);
 // Base metadata itself was never overwritten with a live observation.
 assert(design.hp==800&&design.strength==70&&design.strength_growth==6);
}

void visibility_and_error_boundaries(){
 Raw raw;int queries=0,checks=0;
 const auto check=[&](int code){++checks;if(code)throw std::runtime_error("host failure");};
 assert(!p10::fresh_actor_snapshot(8,raw,97,[&](auto,auto*out){++queries;out->health=999;return 97;},check));
 assert(queries==1&&checks==0); // hidden never becomes visible because data was filled
 bool caught=false;
 try{(void)p10::fresh_actor_snapshot(8,raw,97,[&](auto,auto*){++queries;return 99;},check);}
 catch(const std::runtime_error&){caught=true;}
 assert(caught&&queries==2&&checks==1);
 assert(!p10::fresh_actor_snapshot(8,raw,97,[&](auto,auto*out){++queries;out->x=-1;out->y=-1;return 0;},check));
 assert(queries==3&&checks==2); // nonphysical/dead location is not personal visibility
}

void channels_death_roles_and_team_are_separate(){
 p10::Actor design;design.type=p10::Champion::issac;design.max_hp=900;
 Raw raw{1,4,2,3,700,900,40,100,1,1,int(flags.alive|flags.recall)};
 auto a=p10::unpack_actor_snapshot(design,raw,12,0,profile,flags,false);
 assert(a.channel==p10::Channel::recall&&!a.ally&&a.pick==2); // physical twin, same draft choice
 raw.state=int(flags.alive|flags.teleport);a=p10::unpack_actor_snapshot(design,raw,12,1,profile,flags,false);
 assert(a.channel==p10::Channel::teleport&&a.ally);
 raw.state=0;a=p10::unpack_actor_snapshot(design,raw,12,1,profile,flags,false);
 assert(!a.alive&&!a.can_move&&!a.can_attack&&!a.can_cast);
 assert(a.channel==p10::Channel::none&&a.bag==0&&a.inventory[1]==0);
}
void attack_reset_requires_freshly_spent_basic_allowance(){
 int queries=0;bool attack_available=true;
 auto available=[&]{++queries;return attack_available;};
 assert(!p10::reset_cast_eligible(true,available)&&queries==1);
 attack_available=false;assert(p10::reset_cast_eligible(true,available)&&queries==2);
 attack_available=true;assert(p10::reset_cast_eligible(false,available)&&queries==2); // shields and other casts stay eligible
 int round=10,casts=0;bool forecast_attack_available=false;(void)forecast_attack_available;
 auto legal=[&]{return p10::reset_cast_eligible(true,available);};
 assert(!p10::guarded_action(10,[&]{return round;},legal,[&]{++casts;return true;}));assert(casts==0&&queries==3);
 attack_available=false;assert(p10::guarded_action(10,[&]{return round;},legal,[&]{++casts;return true;}));assert(casts==1&&queries==4);
 auto suspended=[&]{++round;return false;};
 assert(!p10::guarded_action(10,[&]{return round;},[&]{return p10::reset_cast_eligible(true,suspended);},[&]{++casts;return true;}));assert(casts==1);
}
}
int main(){one_read_and_fresh_mutable_state();visibility_and_error_boundaries();channels_death_roles_and_team_are_separate();attack_reset_requires_freshly_spent_basic_allowance();
 p10::Actor ally;ally.id=7;ally.visible=ally.alive=ally.ally=ally.can_cast=true;ally.position={3,4};ally.hp=150;ally.strength=70;ally.bag=51;ally.priority=-2;ally.abilities[0].ready=true;
 p10::withhold_combat_details(ally);assert(!ally.combat_details_complete&&!ally.equipment_details_complete&&!ally.abilities[0].ready&&ally.cooldown[0]==-1);
 assert(ally.id==7&&ally.position==(p10::Point{3,4})&&ally.hp==150&&ally.strength==70&&ally.bag==51&&ally.priority==-2&&ally.can_cast);
 struct CampRaw {float health=0,max_health=0,strength=0;};CampRaw camp;p10::Monster member;member.id=11;member.hp=120;int live_reads=0;
 const auto checked=[](int code){if(code)throw std::runtime_error("camp API failure");};
 assert(p10::fresh_camp_snapshot(member,camp,97,[](auto,auto*out){*out=0;return 0;},[&](auto,auto*){++live_reads;return 97;},checked));
 assert(member.observed&&!member.alive&&member.hp==0&&live_reads==0);
 assert(p10::fresh_camp_snapshot(member,camp,97,[](auto,auto*out){*out=1;return 0;},[&](auto,auto*out){++live_reads;out->health=out->max_health=120;return 0;},checked));
 assert(member.observed&&member.alive&&member.awake&&member.hp==120&&live_reads==1);
 assert(!p10::fresh_camp_snapshot(member,camp,97,[](auto,auto*){return 97;},[&](auto,auto*){++live_reads;return 0;},checked));
 assert(!member.observed&&live_reads==1);
 std::vector<p10::Point> exits;const std::array<std::uint8_t,2> free{0b10110001,0b00000001};
 p10::append_open_neighborhood(exits,free,3,3,{1,1});
 assert((exits==std::vector<p10::Point>{{0,0},{1,1},{2,1},{1,2},{2,2}}));
 p10::append_open_neighborhood(exits,free,3,3,{0,0});assert(exits.size()==5);
 exits.clear();p10::append_open_neighborhood(exits,free,3,3,{0,0},1);assert(exits.size()==1&&exits[0]==(p10::Point{0,0}));
 exits.clear();p10::append_open_neighborhood(exits,free,3,3,{-2,-2});assert(exits.empty());
 p10::append_open_cardinal_rays(exits,free,3,3,{1,1},4);assert((exits==std::vector<p10::Point>{{1,1},{2,1},{1,2}}));
 std::uint8_t row=0x7f;const std::span<const std::uint8_t>ray_mask{&row,1};exits.clear();p10::append_open_cardinal_rays(exits,ray_mask,7,1,{3,0},100);assert(exits.size()==7);
 row=0x5f;exits.clear();p10::append_open_cardinal_rays(exits,ray_mask,7,1,{3,0});assert(exits.size()==5&&std::find(exits.begin(),exits.end(),p10::Point{6,0})==exits.end());
 exits.clear();p10::append_open_cardinal_rays(exits,ray_mask,7,1,{3,0},4,1);assert(exits.size()==1&&exits[0]==(p10::Point{3,0}));
 exits.clear();p10::append_open_cardinal_rays(exits,ray_mask,7,1,{-1,0});assert(exits.empty());
 std::cout<<"PASS native actor observation: fresh ABI snapshots, visibility/errors, changed state, twins, reset eligibility and packed legal exits\n";}
