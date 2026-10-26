#pragma once
#include "contract.h"
#include "guard.h"
#include <optional>
#include <span>
#include <stdexcept>

namespace p10 {
template<class Raw,class StateQuery,class LiveQuery,class Check>
bool fresh_camp_snapshot(Monster&m,Raw&raw,int hidden,StateQuery state_query,LiveQuery live_query,Check check){
 std::uint32_t state=0;const int state_code=state_query(std::uint32_t(m.id),&state);
 if(state_code==hidden){m.observed=false;return false;}check(state_code);
 if(state>2)throw std::runtime_error("invalid visible camp lifecycle");
 m.observed=true;m.alive=state!=0;m.awake=state==1;m.stun=0;
 // A visible corpse has a legal state query but no live combat object. Do not
 // interpret its expected combat API97 as an unseen member of a living group.
 if(!m.alive){m.hp=0;return true;}
 const int live_code=live_query(std::uint32_t(m.id),&raw);
 if(live_code==hidden){m.observed=false;return false;}check(live_code);
 m.hp=raw.health;m.max_hp=raw.max_health;m.strength=raw.strength;return true;
}
inline void append_open_neighborhood(std::vector<Point>&out,std::span<const std::uint8_t> mask,int width,int height,Point p,std::size_t cap=64){
 for(int y=std::max(0,p.y-1);y<=std::min(height-1,p.y+1);++y)for(int x=std::max(0,p.x-1);x<=std::min(width-1,p.x+1);++x){
  const auto n=std::size_t(y)*width+x;Point tile{x,y};
  if(n/8<mask.size()&&((mask[n/8]>>(n%8))&1)&&out.size()<cap&&std::find(out.begin(),out.end(),tile)==out.end())out.push_back(tile);
 }
}
inline void append_open_cardinal_rays(std::vector<Point>&out,std::span<const std::uint8_t>mask,int width,int height,Point p,int length=4,std::size_t cap=64){
 if(p.x<0||p.y<0||p.x>=width||p.y>=height)return;
 auto free=[&](Point q){if(q.x<0||q.y<0||q.x>=width||q.y>=height)return false;const auto n=std::size_t(q.y)*width+q.x;return n/8<mask.size()&&((mask[n/8]>>(n%8))&1);};
 auto add=[&](Point q){if(out.size()<cap&&std::find(out.begin(),out.end(),q)==out.end())out.push_back(q);};
 if(free(p))add(p);for(Point d:std::array<Point,4>{{{1,0},{-1,0},{0,1},{0,-1}}})for(int step=1;step<=std::clamp(length,0,4);++step){Point q{p.x+d.x*step,p.y+d.y*step};if(!free(q))break;add(q);}
}
// Pure extraction seam for the exact pinned champion_info ABI. A single live
// host read supplies all mutable combat fields; no property wrapper is called
// while unpacking. Explicit masks keep the fixture independent of SDK startup.
struct SnapshotFlags {
 std::uint32_t alive,move,attack,cast,tribute,recall,teleport;
};
template<class Snapshot,class Query,class Check>
bool fresh_actor_snapshot(std::uint32_t id,Snapshot&raw,int hidden_code,Query query,Check check){
 const int code=query(id,&raw);
 if(code==hidden_code)return false;
 check(code);
 return raw.x>=0&&raw.y>=0;
}
template<class Snapshot>
Actor unpack_actor_snapshot(Actor design,const Snapshot&raw,int id,int own_team,const Profile&p,
                           SnapshotFlags flags,bool at_spawn){
 Actor a=design;a.id=id;a.ally=raw.team==own_team;a.visible=true;
 const auto state=std::uint32_t(raw.state);
 a.alive=(state&flags.alive)!=0;
 for(int i=0;i<5;++i)if(p.draft[i]==a.type||(a.type==Champion::issac&&p.draft[i]==Champion::cassi))a.pick=i;
 if(!a.alive)return a;
 a.position={raw.x,raw.y};a.hp=raw.health;a.max_hp=raw.max_health;
 a.strength=raw.strength;a.focus=raw.focus;a.range=raw.attack_range;
 a.can_move=(state&flags.move)!=0;a.can_attack=(state&flags.attack)!=0;a.can_cast=(state&flags.cast)!=0;
 a.at_spawn=at_spawn;
 if(state&flags.tribute)a.channel=Channel::tribute;
 else if(state&flags.recall)a.channel=Channel::recall;
 else if(state&flags.teleport)a.channel=Channel::teleport;
 return a;
}
inline void observed_cooldowns(Actor&a,const std::array<int,2>&cooldowns){
 a.cooldown=cooldowns;
 for(int i=0;i<2;++i){a.abilities[i].cooldown=cooldowns[i];a.abilities[i].ready=a.can_cast&&cooldowns[i]==0;}
}
// An explicitly lightweight ally still has fresh pose/stats/bag/order. Mark
// unqueried combat details unavailable instead of exposing design cooldown0.
inline void withhold_combat_details(Actor&a){
 a.combat_details_complete=false;a.equipment_details_complete=false;a.cooldown={-1,-1};
 for(auto&ability:a.abilities){ability.ready=false;ability.cooldown=-1;}
}
// A ready reset still cannot consume a cast while the basic allowance remains
// available. The getter must run at the final action boundary, after any walk,
// attack or changed observation; non-reset casts do not need this extra query.
template<class AttackAvailable>
bool reset_cast_eligible(bool attack_reset,AttackAvailable available){return !attack_reset||!available();}
}

#ifndef P10_OBSERVATION_PURE_TEST
#include "../../../../sdk/cpp/cambc.h"
namespace p10 {
inline Point point(cambc::vec2 p){return {p.x,p.y};}
inline cambc::vec2 point(Point p){return {p.x,p.y};}
template<class T> int handle(T c){
 if constexpr(std::is_same_v<T,cambc::champion>)return int(std::uint32_t(static_cast<cambc::handle_champion_t>(c)));
 else if constexpr(std::is_same_v<T,cambc::camp>)return int(std::uint32_t(static_cast<cambc::handle_camp_t>(c)));
 else return int(std::uint32_t(static_cast<cambc::handle_camp_group_t>(c)));
}
inline Champion champion_type(cambc::champion_type c){
 switch(c){
 case cambc::champion_type::varg:return Champion::varg;case cambc::champion_type::stellar:return Champion::stellar;
 case cambc::champion_type::vendetta:return Champion::vendetta;case cambc::champion_type::willow:return Champion::willow;
 case cambc::champion_type::tavi:return Champion::tavi;case cambc::champion_type::vesper_vale:return Champion::vesper;
 case cambc::champion_type::marcella:return Champion::marcella;case cambc::champion_type::prism:return Champion::prism;
 case cambc::champion_type::ren:return Champion::ren;case cambc::champion_type::ryth:return Champion::ryth;
 case cambc::champion_type::heart_of_ruin:return Champion::heart;case cambc::champion_type::cassi:return Champion::cassi;
 case cambc::champion_type::issac:return Champion::issac;default:return Champion::other;}
}
inline cambc::champion_draft draft_type(Champion c){
 switch(c){
 case Champion::varg:return cambc::champion_draft::varg;case Champion::stellar:return cambc::champion_draft::stellar;
 case Champion::vendetta:return cambc::champion_draft::vendetta;case Champion::willow:return cambc::champion_draft::willow;
 case Champion::tavi:return cambc::champion_draft::tavi;case Champion::vesper:return cambc::champion_draft::vesper_vale;
 case Champion::marcella:return cambc::champion_draft::marcella;case Champion::prism:return cambc::champion_draft::prism;
 case Champion::ren:return cambc::champion_draft::ren;case Champion::ryth:return cambc::champion_draft::ryth;
 case Champion::heart:return cambc::champion_draft::heart_of_ruin;
 case Champion::cassi:case Champion::issac:return cambc::champion_draft::cassi_and_issac;
 default:return cambc::champion_draft::varg;}
}
inline cambc::item_type item_type(Item i){
 switch(i){
 case Item::health:return cambc::item_type::power_shard_of_health;
 case Item::focus:return cambc::item_type::power_shard_of_focus;
 case Item::monster_strength:return cambc::item_type::monster_shard_of_strength;
 case Item::monster_focus:return cambc::item_type::monster_shard_of_focus;
 case Item::monster_health:return cambc::item_type::monster_shard_of_health;
 case Item::jungle_strength:return cambc::item_type::jungle_shard_of_strength;
 case Item::jungle_focus:return cambc::item_type::jungle_shard_of_focus;
 case Item::jungle_health:return cambc::item_type::jungle_shard_of_health;
 case Item::durability_health:return cambc::item_type::durability_shard_of_health;
 case Item::home_health:return cambc::item_type::home_shard_of_health;
 case Item::treasure_strength:return cambc::item_type::treasure_shard_of_strength;
 default:return cambc::item_type::power_shard_of_strength;}
}
inline bool same_round(int r){return int(cambc::round_number())==r;}
inline bool channel_active(){const auto state=cambc::self().state();return state.contains(cambc::champion_state::recalling)||state.contains(cambc::champion_state::submitting_gems)||state.contains(cambc::champion_state::teleporting);}
inline Ability ability_view(const cambc::ability&a,bool design=false){
 Ability b;b.damage=a.is_damage();b.aoe=a.is_aoe_damage();b.reset=a.is_attack_reset();b.shield=a.is_shield();
 b.heal=a.is_heal();b.cc=a.is_cc();b.dash=a.is_dash();
 if(!design){b.cooldown=a.cooldown();b.ready=a.is_ready();}
 if(auto info=a.info())std::visit([&](const auto&p){
  b.exclusive=p.exclusive_with_movement;
  b.max_cooldown=p.max_cooldown;
  b.targets_camps=p.valid_targets.contains(cambc::ability_target_mask::camps);b.targets_self=p.valid_targets.contains(cambc::ability_target_mask::self);b.targets_ground=p.valid_targets.contains(cambc::ability_target_mask::ground);
  if constexpr(requires{p.range;})b.range=p.range;
  if constexpr(requires{p.modifier;})b.modifier=p.modifier;
  if constexpr(requires{p.radius;})b.radius=p.radius;
  if constexpr(requires{p.duration;})b.duration=p.duration;
 },*info);
 return b;
}
inline Actor immutable_design(cambc::champion_type type){
 static std::array<std::optional<Actor>,13> cache{};
 const int index=int(type);if(index<0||index>=int(cache.size()))throw std::runtime_error("unknown champion design");
 if(!cache[index]){
  // Official design objects already own their two immutable ability payloads.
  // Base stats need one design query per type, rather than one per observation.
  const auto&c=cambc::champion_designs[index];const auto s=c.base_stats();
  Actor a;a.equipment_details_complete=false;a.type=champion_type(type);a.hp=a.max_hp=s.base_health;a.strength=s.base_strength;a.focus=s.base_focus;a.range=s.base_attack_range;
  a.strength_growth=s.strength_growth;a.health_growth=s.health_growth;a.focus_growth=s.focus_growth;
  const auto abilities=c.abilities();for(int i=0;i<2;++i)a.abilities[i]=ability_view(abilities[i],true);
  cache[index]=a;
 }
 return *cache[index];
}
inline Actor design_view(const cambc::champion&c){
 return immutable_design(c.champion_name());
}
// Physical champion identities are public and fixed after draft. Reading the
// handle lists once avoids reconstructing two host-bound ability views for
// every visible actor merely to obtain its identity on every owner turn.
inline const std::vector<std::uint32_t>& public_actor_ids(cambc::team side){
 static const auto ids=[] {
  std::array<std::vector<std::uint32_t>,2> result;
  for(int i=0;i<2;++i){std::uint32_t count=0;cambc::detail::check(i==0?cambc::detail::sdk_num_allies(&count):cambc::detail::sdk_num_enemies(&count));
   auto&out=result[i];out.resize(count);std::uint32_t received=count;
   cambc::detail::check(i==0?cambc::detail::sdk_allies(out.data(),count,&received):cambc::detail::sdk_enemies(out.data(),count,&received));
   if(received>count)throw std::runtime_error("invalid public champion count");out.resize(received);
  }
  return result;
 }();
 return ids[side==cambc::team::ally?0:1];
}
inline const std::vector<std::uint32_t>& public_actor_ids(){
 static const auto ids=[] {auto result=public_actor_ids(cambc::team::ally);const auto&enemy=public_actor_ids(cambc::team::enemy);result.insert(result.end(),enemy.begin(),enemy.end());return result;}();
 return ids;
}
inline Actor actor_view(std::uint32_t id,const Profile&p,bool self=false,int observed_round=-1,bool full_details=true,std::uint64_t ally_detail_types=0,bool observe_enemy_equipment=true){
 static_assert(sizeof(cambc::detail::champion_info)==44);
 static const int own_team=cambc::detail::sdk_my_team();
 struct PublicIdentity{std::uint32_t id,type;int team;};
 static std::vector<PublicIdentity> identities;
 cambc::detail::champion_info raw{};
 const bool visible=fresh_actor_snapshot(id,raw,int(cambc::error_code::api_not_in_vision),
  [](auto actor,auto*out){return cambc::detail::sdk_champion(actor,out);},[](int code){cambc::detail::check(code);});
 if(!visible){
  // Identity is public even for hidden/dead actors. No hidden HP, equipment,
  // cooldown, bag or private gold query is attempted after the visibility veto.
  auto known=std::find_if(identities.begin(),identities.end(),[&](auto value){return value.id==id;});
  if(known==identities.end()){cambc::detail::champion_identity identity{};cambc::detail::check(cambc::detail::sdk_champion_identity(id,&identity));identities.push_back({id,std::uint32_t(identity.type),int(identity.team)});known=std::prev(identities.end());}
  Actor a=immutable_design(static_cast<cambc::champion_type>(known->type));a.id=int(id);a.ally=known->team==own_team;
  for(int i=0;i<5;++i)if(p.draft[i]==a.type||(a.type==Champion::issac&&p.draft[i]==Champion::cassi))a.pick=i;
  return a;
 }
 if(std::none_of(identities.begin(),identities.end(),[&](auto value){return value.id==id;}))identities.push_back({id,std::uint32_t(raw.type),int(raw.team)});
 static const auto own_spawn=cambc::map::spawn_region(cambc::team::ally).positions();
 constexpr SnapshotFlags flags{cambc::champion_state::alive.bits(),cambc::champion_state::can_move.bits(),
  cambc::champion_state::can_attack.bits(),cambc::champion_state::can_cast.bits(),
  cambc::champion_state::submitting_gems.bits(),cambc::champion_state::recalling.bits(),cambc::champion_state::teleporting.bits()};
 Actor a=unpack_actor_snapshot(immutable_design(static_cast<cambc::champion_type>(raw.type)),raw,int(id),own_team,p,flags,
  std::any_of(own_spawn.begin(),own_spawn.end(),[&](auto tile){return tile.x==raw.x&&tile.y==raw.y;}));
 a.equipment_details_complete=false;
 if(!a.alive)return a;
 std::uint32_t bag{};cambc::detail::check(cambc::detail::sdk_champion_gems(id,&bag));a.bag=bag;
 cambc::detail::check(cambc::detail::sdk_bc_champion_priority(id,false,&a.priority));
 if(a.channel!=Channel::none)cambc::detail::check(cambc::detail::sdk_champion_channel_time(id,&a.channel_remaining));
 if(!self&&a.ally&&!full_details&&!(ally_detail_types&(std::uint64_t{1}<<unsigned(a.type)))){withhold_combat_details(a);return a;}
 std::array<int,2> cooldowns{};
 for(int i=0;i<2;++i)cambc::detail::check(cambc::detail::sdk_champion_cooldown(id,i,&cooldowns[i]));
 observed_cooldowns(a,cooldowns);
 // Same pinned ABI and validation as buff_collection::load. Decode the exact
 // shield snapshot without constructing a champion/ability or variant map.
 std::array<cambc::detail::buff_data,static_cast<std::size_t>(cambc::buff_types::count)> buffs{};std::uint32_t buff_count=0;
 cambc::detail::check(cambc::detail::sdk_champion_buff_data(id,buffs.data(),std::uint32_t(buffs.size()),&buff_count));
 if(buff_count>buffs.size())throw std::runtime_error("invalid engine buff count");
 for(std::uint32_t i=0;i<buff_count;++i){const auto&b=buffs[i];if(b.type>=std::uint32_t(cambc::buff_types::count))throw std::runtime_error("unknown engine buff kind");
  if(b.type==std::uint32_t(cambc::buff_types::shield)){a.shield=b.data.shield.value;a.shield_until=(observed_round>=0?observed_round:int(cambc::round_number()))+b.data.shield.duration;}
 }
 // Combat stats, bag/order/channel, cooldowns and shield are already fresh.
 // Omitted equipment remains explicitly unknown; self/allies never use this veto.
 if(!self&&!a.ally&&!observe_enemy_equipment)return a;
 cambc::enum_map<cambc::item_type> items;cambc::detail::check(cambc::detail::sdk_champion_items(id,items.ptr()));
 static const auto recipes=[] {std::array<std::optional<cambc::item_type>,14> r{};for(int i=1;i<14;++i)if(static_cast<Item>(i)!=Item::lock)r[i]=item_type(static_cast<Item>(i)).result();return r;}();
 for(int i=1;i<14;++i)if(static_cast<Item>(i)!=Item::lock)a.inventory[i]=equivalent_shards(int(items[item_type(static_cast<Item>(i))]),recipes[i]?int(items[*recipes[i]]):0);
 // Inventory stores purchased-shard equivalents (five per completed recipe),
 // so combining cannot erase capital. Actual extra Power stats come from the
 // engine's current Strength/Focus/HP; modulo five is the loose recipe count.
 a.strength_shards=a.inventory[int(Item::strength)]+a.inventory[int(Item::monster_strength)]+a.inventory[int(Item::jungle_strength)]+a.inventory[int(Item::treasure_strength)];
 a.health_shards=a.inventory[int(Item::health)]+a.inventory[int(Item::monster_health)]+a.inventory[int(Item::jungle_health)]+a.inventory[int(Item::durability_health)]+a.inventory[int(Item::home_health)];
 a.focus_shards=a.inventory[int(Item::focus)]+a.inventory[int(Item::monster_focus)]+a.inventory[int(Item::jungle_focus)];
 a.monster_emblem=items[cambc::item_type::monster_emblem_of_strength]>0||items[cambc::item_type::monster_emblem_of_health]>0||items[cambc::item_type::monster_emblem_of_focus]>0;
 a.jungle_emblem=items[cambc::item_type::jungle_emblem_of_strength]>0||items[cambc::item_type::jungle_emblem_of_health]>0||items[cambc::item_type::jungle_emblem_of_focus]>0;
 a.equipment_details_complete=true;
 return a;
}
inline Actor actor_view(const cambc::champion&c,const Profile&p,bool self=false){return actor_view(std::uint32_t(handle(c)),p,self);}
inline bool compatible(const cambc::ability&a,bool walked){return !walked||!ability_view(a,true).exclusive;}
inline bool cast_self(int slot,int round,bool walked=false){
 auto&me=cambc::self();if(!same_round(round)||slot<0||slot>1)return false;
 auto a=me.abilities()[slot];if(!a.can_target_self()||!compatible(a,walked))return false;
 return guarded_action(round,[]{return int(cambc::round_number());},[&]{const auto state=me.state();
  return state.contains(cambc::champion_state::can_cast)&&!state.contains(cambc::champion_state::recalling)&&!state.contains(cambc::champion_state::submitting_gems)&&!state.contains(cambc::champion_state::teleporting)&&a.cooldown()==0&&reset_cast_eligible(a.is_attack_reset(),[&]{return state.contains(cambc::champion_state::can_attack);});
 },[&]{return a.use();});
}
template<class Target> bool cast_on(const Target&t,int slot,int round,bool walked=false){
 auto&me=cambc::self();if(!same_round(round)||channel_active()||!me.can_cast()||!t.is_visible()||!t.is_alive()||slot<0||slot>1)return false;
 auto a=me.abilities()[slot];if(a.cooldown()!=0||!compatible(a,walked))return false;
 const bool ground=a.can_target_ground();
 if constexpr(std::is_same_v<Target,cambc::camp>){if(!ground&&!a.can_target_camps())return false;}
 else {if(!ground&&(t.is_ally()?!a.can_target_allies():!a.can_target_enemies()))return false;}
 if(!a.valid_tiles().is_set(t.position())||!same_round(round))return false;
 // A ground spell uses its tile overload; a unit spell uses its entity overload.
 const auto destination=t.position();
 return guarded_action(round,[]{return int(cambc::round_number());},[&]{return me.can_cast()&&a.cooldown()==0&&t.is_visible()&&t.is_alive();},[&]{return ground?a.use(destination):a.use(t);});
}
} // namespace p10
#endif
