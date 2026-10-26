#pragma once
#include "contract.h"
#include <span>

#ifndef P10_POST_EXPORT_GUARD
#define P10_POST_EXPORT_GUARD 0
#endif

namespace p10 {
// One row-major export per freshly computed field. All candidate comparisons
// below use guest memory; sorting must never make repeated SDK get_cost calls.
inline int linear_distance(std::span<const std::uint16_t> values,int width,int height,Point p){
 if(width<=0||height<=0||p.x<0||p.y<0||p.x>=width||p.y>=height)return -1;
 const auto index=std::size_t(p.y)*std::size_t(width)+std::size_t(p.x);
 if(index>=values.size())return -1;
 const auto value=values[index];
 return value==std::numeric_limits<std::uint16_t>::max()?-1:int(value);
}
inline int minimum_linear_distance(std::span<const std::uint16_t> values,int width,int height,const std::vector<Point>&pads){
 int best=-1;for(auto p:pads){const int distance=linear_distance(values,width,height,p);if(distance>=0&&(best<0||distance<best))best=distance;}return best;
}
template<class Field>
void export_current_distances(const Field&field,int width,int height,std::vector<std::uint16_t>&values){
 values.resize(std::size_t(width)*std::size_t(height));
 field.export_linear_buffer(std::span<std::uint16_t>{values});
}
}

#ifndef P10_NAVIGATION_PURE_TEST
#include "sdk.h"
#include "trace.h"
#include <iostream>

namespace p10 {
inline cambc::bitboard around(cambc::vec2 p,int r){cambc::bitboard b;b.draw_chebyshev(p,r);return b;}
class Navigation {
public:
 struct Source {cambc::map::camp_group group;cambc::bitboard spawns,approaches;Group metadata;int state_round=-1;std::uint32_t state=0;};
 struct Bank {cambc::vec2 position;cambc::bitboard goals;std::unique_ptr<cambc::distance_field> field;std::vector<Point> pads;};
 std::vector<Source> sources;std::vector<Bank> banks;
 std::vector<cambc::map::camp_group> physical_groups;
 std::vector<std::uint16_t> origin_values,walk_values,spawn_values;
 std::vector<std::uint8_t> visible_values;
 // A mathematical field key only: never alias known_blocked, which fresh
 // corpse observations can open later in the owner turn.
 std::vector<std::uint64_t> origin_wall_key;Point origin_point_key{};
 cambc::detail::metadata origin_layout_key{};bool origin_key_valid=false;
 static bool same_origin_layout(const cambc::detail::metadata&a,const cambc::detail::metadata&b){
  return a.width==b.width&&a.height==b.height&&a.chunked_width==b.chunked_width&&a.chunked_height==b.chunked_height&&a.chunk_stride==b.chunk_stride&&a.board_bytes==b.board_bytes&&a.field_bytes==b.field_bytes;
 }
 cambc::bitboard terrain,static_blocked,known_blocked,own_spawn;std::vector<Point> own_spawn_pads;std::unique_ptr<cambc::distance_field> origin,spawn_field;
 std::size_t cursor=0,bank_cursor=0,route_cursor=0;bool initialized=false,complete=false,routes_complete=false;int origin_round=-1,last_blocked_round=-1;
 std::size_t envelope_cursor=0,envelope_member=0;bool envelope_started=false;
 bool prepare_envelopes(int round,int reserve){
  // The SDK fixes camp position and range to queried design/spawn metadata.
  // Retain only that geometry. Warmup is resumable; no live combat head enters it.
  while(envelope_cursor<sources.size()){
   if(cambc::fuel_remaining()<std::uint64_t(reserve+18000)||!same_round(round))return false;
   auto&g=sources[envelope_cursor].metadata;
   if(!envelope_started){begin_hazard_envelope(g,round);envelope_member=0;envelope_started=true;}
   if(envelope_member<g.monsters.size()){extend_hazard_envelope(g,g.monsters[envelope_member],envelope_member);++envelope_member;continue;}
   ++envelope_cursor;envelope_started=false;
  }
  return complete&&envelope_cursor==sources.size()&&same_round(round);
 }
 int horizon()const{return cambc::map::width()+cambc::map::height();}
 static int steps(const cambc::distance_field*f,cambc::vec2 p){if(!f)return -1;auto n=f->get(p);return n==cambc::distance_field::unreachable?-1:int(n);}
 static int minimum(const cambc::distance_field*f,const cambc::bitboard&b){int n=std::numeric_limits<int>::max();if(f)for(auto p:b.positions()){auto d=steps(f,p);if(d>=0)n=std::min(n,d);}return n==std::numeric_limits<int>::max()?-1:n;}
 static int minimum(const cambc::distance_field*f,const std::vector<Point>&pads){int n=std::numeric_limits<int>::max();if(f)for(auto p:pads){auto d=steps(f,point(p));if(d>=0)n=std::min(n,d);}return n==std::numeric_limits<int>::max()?-1:n;}
 static bool visible_source(const Source&s,const cambc::bitboard&visible){for(auto p:s.metadata.spawns)if(visible.is_set(point(p)))return true;return false;}
 int origin_step(Point p)const{if(p.x<0||p.y<0||p.x>=cambc::map::width()||p.y>=cambc::map::height()||origin_values.empty())return -1;auto n=origin_values[std::size_t(p.y)*cambc::map::width()+p.x];return n==cambc::distance_field::unreachable?-1:int(n);}
 int origin_minimum(const cambc::bitboard&goals)const{int best=-1;for(auto p:goals.positions()){int n=origin_step(point(p));if(n>=0&&(best<0||n<best))best=n;}return best;}
 int origin_minimum(const std::vector<Point>&pads)const{return minimum_linear_distance(origin_values,cambc::map::width(),cambc::map::height(),pads);}
 bool visible_tile(Point p)const{if(p.x<0||p.y<0||p.x>=cambc::map::width()||p.y>=cambc::map::height()||visible_values.empty())return false;const auto n=std::size_t(p.y)*cambc::map::width()+p.x;return (visible_values[n/8]>>(n%8))&1;}
 bool visible_source(const Source&s)const{for(auto p:s.metadata.spawns)if(visible_tile(p))return true;return false;}
 void compute(cambc::distance_field&f,const cambc::bitboard&blocked,const cambc::bitboard&goals){cambc::bitboard seen;f.compute(blocked,goals,seen,horizon());}
 void prepare(int round,int reserve){
  origin_round=-1; // No old/current stamp survives incomplete fresh input work.
  if(!initialized){terrain=cambc::map::walls()|cambc::map::holes()|cambc::map::altars()|cambc::map::spawn_region(cambc::team::enemy);static_blocked=terrain;
   own_spawn=cambc::map::spawn_region(cambc::team::ally);for(auto tile:own_spawn.positions())own_spawn_pads.push_back(point(tile));
   physical_groups=cambc::map::camp_groups();
   for(const auto&g:physical_groups)static_blocked|=g.spawn_tiles();
   known_blocked=static_blocked;
   for(auto p:cambc::map::altars().positions()){
    Bank bank{p,around(p,cambc::constants::submit_gems_range)&~static_blocked,nullptr,{}};
    for(auto tile:bank.goals.positions())bank.pads.push_back(point(tile));banks.push_back(std::move(bank));
   }
   initialized=true;
  }
  const auto&groups=physical_groups;
  // Only immutable geometry may continue across turns. Dynamic observations
  // and unfinished policy forecasts are discarded on every owner turn.
  while(cursor<groups.size()&&cambc::fuel_remaining()>std::uint64_t(reserve+20000)){
   const auto&g=groups[cursor++];Source s{g,g.spawn_tiles(),{}, {}};auto&v=s.metadata;v.id=handle(g);v.gems=g.gem_value();v.gold=g.gold_value();v.respawn=g.max_respawn_timer();
   v.program_available=g.camps().size()<=64; // explicit unavailable, never truncated hazards
   for(const auto&c:g){v.spawns.push_back(point(c.spawn_position()));auto stats=c.base_stats();Monster m;
    m.id=handle(c);m.group=v.id;m.position=point(c.spawn_position());m.hp=m.max_hp=stats.base_health;m.strength=stats.base_strength;m.range=stats.base_attack_range;m.respawn=v.respawn;v.monsters.push_back(m);
    s.approaches|=around(c.spawn_position(),1);
   }
   s.approaches&=~static_blocked;if(!v.spawns.empty())v.position=v.spawns.front();
   const auto pads=s.approaches.positions();for(auto p:pads)v.approach_pads.push_back(point(p));if(!pads.empty())v.approach=point(pads.front());
   auto geometry=std::make_shared<SourceGeometry>();geometry->source=v.id;geometry->spawns=v.spawns;geometry->approach_pads=v.approach_pads;for(const auto&m:v.monsters)geometry->members.push_back({m.id,m.position});v.source_geometry=std::move(geometry);
   sources.push_back(std::move(s));
   if(!same_round(round))return;
  }
  complete=cursor==groups.size();
  if(bank_cursor<banks.size()&&cambc::fuel_remaining()>std::uint64_t(reserve+18000)){
   auto&b=banks[bank_cursor++];b.field=std::make_unique<cambc::distance_field>();compute(*b.field,static_blocked,b.goals);
  }
  if(!spawn_field&&cambc::fuel_remaining()>std::uint64_t(reserve+18000)){
   spawn_field=std::make_unique<cambc::distance_field>();compute(*spawn_field,static_blocked,own_spawn);
   spawn_values=spawn_field->export_linear_buffer();
  }
  // These legs depend only on immutable terrain and source/altar identity.
  // Pay once during bounded warmup, never source×altar work in a live frame.
  if(complete&&bank_cursor==banks.size()&&spawn_field){
   while(route_cursor<sources.size()&&cambc::fuel_remaining()>std::uint64_t(reserve+18000)){
    auto&s=sources[route_cursor++];s.metadata.to_spawn=minimum(spawn_field.get(),s.metadata.approach_pads);s.metadata.to_bank=-1;
    for(const auto&b:banks){int d=minimum(b.field.get(),s.metadata.approach_pads);if(d>=0&&(s.metadata.to_bank<0||d+1<s.metadata.to_bank))s.metadata.to_bank=d+1;}
    if(!same_round(round))return;
   }
   routes_complete=route_cursor==sources.size();
  }
  if(cambc::self().is_alive()&&cambc::fuel_remaining()>std::uint64_t(reserve+10000)){
   auto current_input=[&]{return same_round(round)&&cambc::fuel_remaining()>=std::uint64_t(reserve+500);};
   if(!current_input())return;
   const auto origin_point=cambc::self().position();const auto layout=cambc::detail::layout();
   if(!current_input()||layout.width<=0||layout.height<=0||layout.board_bytes%sizeof(std::uint64_t)!=0)return;
   auto walls=static_blocked;walls.set(origin_point,false);
   const auto visible=cambc::vision::visible_tiles();
   visible_values.resize(cambc::bitboard::linear_buffer_size());visible.export_linear_buffer(visible_values);
   if(!current_input())return;
   for(auto&s:sources){if(!current_input())return;if(visible_source(s)){
    std::uint32_t state=0;cambc::detail::check(cambc::detail::sdk_camp_group_state(std::uint32_t(s.metadata.id),&state));
    if(!current_input())return;
    if(state<1||state>3)throw std::runtime_error("invalid visible group lifecycle");
    s.state=state;s.state_round=round;if(state!=1)walls&=~s.spawns;
   }}
   if(!current_input())return;known_blocked=walls;
   const auto words=layout.board_bytes/sizeof(std::uint64_t);const auto*raw=walls.ptr();
   bool same=origin&&origin_key_valid&&same_origin_layout(layout,origin_layout_key)&&point(origin_point)==origin_point_key&&origin_wall_key.size()==words&&origin_values.size()==std::size_t(layout.width)*layout.height;
   for(std::size_t i=0;same&&i<words;i+=8){if(!current_input())return;const auto end=std::min(words,i+8);for(auto j=i;j<end;++j)if(raw[j]!=origin_wall_key[j]){same=false;break;}}
   if(!current_input())return;
   if(same){origin_round=round;return;}
   // Invalidate before overwriting mathematical storage. Every consumer uses
   // origin_round; a partial miss can never become this or a future cache hit.
   origin_key_valid=false;origin=std::make_unique<cambc::distance_field>();compute(*origin,walls,around(origin_point,0));
   if(!current_input())return;
   origin_values.resize(std::size_t(layout.width)*layout.height);origin->export_linear_buffer(origin_values);
   if(!current_input())return;
   origin_wall_key.resize(words);
   for(std::size_t i=0;i<words;i+=8){if(!current_input())return;const auto end=std::min(words,i+8);for(auto j=i;j<end;++j)origin_wall_key[j]=raw[j];}
   if(!current_input())return;
   origin_point_key=point(origin_point);origin_layout_key=layout;origin_key_valid=true;origin_round=round;
  }
 }
 Source* source(int id){for(auto&s:sources)if(s.metadata.id==id)return &s;return nullptr;}
 // A dead member of a still-living group is physically traversable. Only this
 // owner's fresh observation opens it; next prepare rebuilds conservative walls.
 void open_observed_corpses(const World&w){for(const auto&g:w.groups)if(g.living)for(const auto&m:g.monsters)if(m.observed&&!m.alive&&!terrain.is_set(point(m.position)))known_blocked.set(point(m.position),false);}
 const char* walk_phase="none";int walk_fuel=0;const World*partial_walk_frame=nullptr;
 cambc::bitboard blocked()const{auto b=known_blocked|cambc::vision::camp_tiles()|cambc::vision::champion_tiles()|cambc::vision::teleport_tiles();b.set(cambc::self().position(),false);return b;}
 bool walk(cambc::bitboard goals,int round){
  auto&me=cambc::self();if(cambc::fuel_remaining()<22000||!same_round(round)||channel_active()||!me.can_move())return false;
  cambc::bitboard rejected;
  for(int retry=0;retry<2;++retry){if(cambc::fuel_remaining()<22000||!same_round(round))return false;walk_phase="walls";walk_fuel=int(cambc::fuel_remaining());auto walls=blocked()|rejected;goals&=~walls;if(goals.is_empty())return false;
   const auto start=me.position();if(goals.is_set(start))return false;
   // A current legal adjacent goal needs no whole-map distance field. Preserve
   // the same two-attempt hidden-occupant budget and fresh final action guard.
   bool adjacent_tried=false;for(auto p:(walls.reachable_tiles(start,1)&goals).positions()){
    if(cambc::fuel_remaining()<18000||!same_round(round))return false;
    if(p==start)continue;walk_phase="adjacent-safety";walk_fuel=int(cambc::fuel_remaining());
    if(partial_walk_frame&&!partial_walk_safe(*partial_walk_frame,point(p),[]{return cambc::fuel_remaining()>=18000;}))continue;
    adjacent_tried=true;walk_phase="adjacent-move";walk_fuel=int(cambc::fuel_remaining());
    if(guarded_action(round,[]{return int(cambc::round_number());},[&]{return me.can_move();},[&]{return me.move_absolute(p);}))return true;
    if(!same_round(round))return false;rejected.set(p);last_blocked_round=round;trace("p10 blocked r=",round," x=",p.x," y=",p.y);break;
   }
   if(adjacent_tried)continue;
   cambc::distance_field f;walk_phase="compute";walk_fuel=int(cambc::fuel_remaining());compute(f,walls,goals);
   const int width=cambc::map::width(),height=cambc::map::height();
   if(cambc::fuel_remaining()<14000||!same_round(round))return false;
   walk_phase="export";walk_fuel=int(cambc::fuel_remaining());export_current_distances(f,width,height,walk_values);
#if P10_POST_EXPORT_GUARD
   // A full-board export can spend the allowance that admitted it. The next
   // phase already requires18k; refuse before its allocation/sort as well.
   if(cambc::fuel_remaining()<18000||!same_round(round))return false;
#endif
   const auto cost=[&](cambc::vec2 p){return linear_distance(walk_values,width,height,point(p));};
   const auto here=me.position();const int old=cost(here);if(old<=0)return false;
   walk_phase="candidates";walk_fuel=int(cambc::fuel_remaining());auto candidates=walls.reachable_tiles(here,1).positions();std::sort(candidates.begin(),candidates.end(),[&](auto a,auto b){return cost(a)<cost(b);});
   bool tried=false;for(auto p:candidates){
    if(cambc::fuel_remaining()<18000||!same_round(round))return false;
    const int d=cost(p);if(d<0||d>=old||p==here)continue;
    walk_phase="candidate-safety";walk_fuel=int(cambc::fuel_remaining());
    if(partial_walk_frame&&!partial_walk_safe(*partial_walk_frame,point(p),[]{return cambc::fuel_remaining()>=18000;}))continue;
    if(!same_round(round)||cambc::fuel_remaining()<14000)return false;
    walk_phase="move";walk_fuel=int(cambc::fuel_remaining());tried=true;if(guarded_action(round,[]{return int(cambc::round_number());},[&]{return me.can_move();},[&]{return me.move_absolute(p);}))return true;
    if(!same_round(round))return false;rejected.set(p);last_blocked_round=round;
    trace("p10 blocked r=",round," x=",p.x," y=",p.y);break;
   }if(!tried)return false;
  }return false;
 }
 DraftContext draft_context(){
  DraftContext d;d.picks=int(cambc::draft::team_size());d.width=cambc::map::width();d.height=cambc::map::height();
  const auto spawn=cambc::map::spawn_region(cambc::team::ally).positions();if(!spawn.empty())d.spawn=point(spawn.front());
  auto walls=cambc::map::walls()|cambc::map::holes()|cambc::map::altars()|cambc::map::spawn_region(cambc::team::enemy);
  const auto groups=cambc::map::camp_groups();for(const auto&g:groups)walls|=g.spawn_tiles();
  cambc::distance_field from_spawn;compute(from_spawn,walls,cambc::map::spawn_region(cambc::team::ally));
  const auto from_values=from_spawn.export_linear_buffer();std::vector<std::vector<std::uint16_t>> altar_values;
  for(auto p:cambc::map::altars().positions()){Altar a;a.id=int(d.altars.size());a.position=point(p);d.altars.push_back(a);cambc::distance_field field;compute(field,walls,around(p,1)&~walls);altar_values.push_back(field.export_linear_buffer());}
  for(const auto&g:groups){Group v;v.id=handle(g);v.gems=g.gem_value();v.gold=g.gold_value();v.respawn=g.max_respawn_timer();cambc::bitboard pads;
   for(const auto&c:g){v.spawns.push_back(point(c.spawn_position()));auto s=c.base_stats();Monster m;m.id=handle(c);m.group=v.id;m.position=point(c.spawn_position());m.hp=m.max_hp=s.base_health;m.strength=s.base_strength;m.range=s.base_attack_range;v.monsters.push_back(m);pads|=around(c.spawn_position(),1);}
   pads&=~walls;auto positions=pads.positions();for(auto tile:positions)v.approach_pads.push_back(point(tile));
   v.travel=minimum_linear_distance(from_values,d.width,d.height,v.approach_pads);v.to_spawn=v.travel;v.route_known=v.travel>=0;v.to_bank=-1;
   for(const auto&values:altar_values){int n=minimum_linear_distance(values,d.width,d.height,v.approach_pads);if(n>=0&&(v.to_bank<0||n+1<v.to_bank))v.to_bank=n+1;}
   if(!v.spawns.empty())v.position=v.spawns.front();if(!positions.empty())v.approach=point(positions.front());v.program_available=v.monsters.size()<=64;d.groups.push_back(std::move(v));
  }
  for(const auto&c:cambc::champion_designs)d.champions.push_back(design_view(c));return d;
 }
};
} // namespace p10
#endif
