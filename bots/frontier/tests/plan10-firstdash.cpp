#ifndef FIRSTDASH_BODY_HEADER
#error First-dash fixture requires the verbatim extracted production Runtime::dash_to method
#endif
#include "../common/src/contract.h"
#include "../common/src/guard.h"
#include <cassert>
#include <iostream>

// These bounded doubles supply mechanics, not an engine simulation. In this
// 8x8 world a normal walk moves one Chebyshev step. The production method alone
// chooses a dash landing, performs its final guard, and handles use's result.
// No fuel, native visibility, damage, cooldown timing or native cast is claimed.
namespace cambc {
using vec2=p10::Point;
struct bitboard {
 std::uint64_t bits=0;
 static bool inside(vec2 p){return p.x>=0&&p.y>=0&&p.x<8&&p.y<8;}
 bool has(vec2 p)const{return inside(p)&&(bits&(std::uint64_t{1}<<(p.y*8+p.x)));}
 void add(vec2 p){assert(inside(p));bits|=std::uint64_t{1}<<(p.y*8+p.x);}
 std::vector<vec2> positions()const{std::vector<vec2> out;out.reserve(64);for(int y=0;y<8;++y)for(int x=0;x<8;++x)if(has({x,y}))out.push_back({x,y});return out;}
 bitboard reachable_tiles(vec2 start,std::uint16_t radius)const{
  bitboard out;if(!inside(start)||has(start))return out;
  std::array<vec2,64> queue{};std::array<int,64> depth;depth.fill(-1);int head=0,tail=0;
  queue[tail++]=start;depth[start.y*8+start.x]=0;out.add(start);
  while(head<tail){const auto p=queue[head++];const int d=depth[p.y*8+p.x];if(d>=std::min(64,int(radius)))continue;
   for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const vec2 next{p.x+dx,p.y+dy};if(!inside(next)||has(next)||depth[next.y*8+next.x]>=0)continue;
    assert(tail<int(queue.size()));depth[next.y*8+next.x]=d+1;queue[tail++]=next;out.add(next);
   }
  }return out;
 }
 friend bitboard operator&(bitboard a,bitboard b){return {a.bits&b.bits};}
 friend bitboard operator~(bitboard a){return {~a.bits};}
};
struct State {
 int round=7,cooldown=0,uses=0,cast_queries=0,deny_cast_query=-1,flip_round_query=-1;
 bool cast=true,dash=true,exclusive=false,engine_success=true;
 vec2 position{1,1},attempted{-1,-1};bitboard visible{},blocked{};
 double range=2,modifier=0,radius=0;
} state;
struct ability {
 int slot=1;
 bool is_dash()const{return slot==1&&state.dash;}
 int cooldown()const{return state.cooldown;}
 bitboard valid_tiles()const{bitboard out;for(int y=0;y<8;++y)for(int x=0;x<8;++x)if(p10::distance({x,y},state.position)<=state.range)out.add({x,y});return out;}
 bool use(vec2 p)const{
  ++state.uses;state.attempted=p;assert(slot==1&&state.cast&&state.cooldown==0);
  assert(bitboard::inside(p)&&!state.blocked.has(p)&&state.visible.has(p)&&p10::distance(p,state.position)<=state.range);
  if(!state.engine_success)return false;
  state.position=p;state.cooldown=8;state.cast=false;return true;
 }
};
struct champion {
 bool can_cast()const{const int query=++state.cast_queries;if(query==state.flip_round_query)++state.round;return state.cast&&query!=state.deny_cast_query;}
 vec2 position()const{return state.position;}
 std::array<ability,2> abilities()const{return {{{0},{1}}};}
};
inline champion& self(){static champion me;return me;}
inline int round_number(){return state.round;}
namespace vision {inline bitboard visible_tiles(){return state.visible;}}
}
namespace p10 {
inline Point point(cambc::vec2 p){return p;}
inline bool compatible(const cambc::ability&,bool walked){return !walked||!cambc::state.exclusive;}
inline Ability ability_view(const cambc::ability&,bool){Ability out;out.range=cambc::state.range;out.modifier=cambc::state.modifier;out.radius=cambc::state.radius;return out;}
struct Harness {
 struct Navigation {cambc::bitboard blocked()const{return cambc::state.blocked;}} nav_;
 int safety_checks=0;bool safe=false;
 bool current(int round)const{return cambc::round_number()==round;}
 bool safe_hitset(const World&,Point,double,int,Point){++safety_checks;return safe;}
#include FIRSTDASH_BODY_HEADER
};
}
using namespace p10;
static World reset(){cambc::state={};for(Point p:std::array<Point,3>{{{1,1},{2,1},{3,1}}})cambc::state.visible.add(p);
 World w;w.round=7;w.self.type=Champion::vendetta;w.self.position={1,1};w.self.bag=7;
 w.self.alive=w.self.visible=w.self.can_move=w.self.can_cast=true;w.self.hp=w.self.max_hp=1000;return w;}
static void unchanged(){assert(cambc::state.position==Point(1,1));assert(cambc::state.cooldown==0&&cambc::state.cast&&cambc::state.uses==0);}
static void intended_and_substituted(){auto w=reset();Harness direct;
 assert(direct.dash_to(w,{3,1},1,7,false));assert(cambc::state.position==Point(3,1));assert(cambc::state.attempted==Point(3,1));
 assert(cambc::state.uses==1&&cambc::state.cooldown==8&&!cambc::state.cast&&direct.safety_checks==0);
 assert(distance(w.self.position,cambc::state.position)==2);
 w=reset();Harness substituted;cambc::state.blocked.add({3,1});
 assert(substituted.dash_to(w,{3,1},1,7,false));assert(cambc::state.position==Point(2,1)&&cambc::state.attempted==Point(2,1));
 const int displacement=distance(w.self.position,cambc::state.position),ordinary_walk_steps=1;
 assert(w.self.can_move&&displacement==ordinary_walk_steps&&cambc::state.uses==1&&cambc::state.cooldown==8);
 // Actual method chose only one step although a two-step destination was
 // requested. Under the explicit one-step walking double this saves no walk.
}
static void refreshed_refusals(){auto w=reset();Harness h;cambc::state.visible.bits=0;cambc::state.visible.add({1,1});assert(!h.dash_to(w,{3,1},1,7,false));unchanged();
 w=reset();cambc::state.cast=false;assert(!h.dash_to(w,{3,1},1,7,false));assert(cambc::state.uses==0&&cambc::state.position==Point(1,1));
 w=reset();cambc::state.cooldown=3;assert(!h.dash_to(w,{3,1},1,7,false));assert(cambc::state.uses==0&&cambc::state.cooldown==3);
 w=reset();cambc::state.dash=false;assert(!h.dash_to(w,{3,1},1,7,false));unchanged();
 w=reset();cambc::state.exclusive=true;assert(!h.dash_to(w,{3,1},1,7,true));unchanged();
 w=reset();assert(!h.dash_to(w,{3,1},1,6,false));unchanged();
 w=reset();cambc::state.deny_cast_query=2;assert(!h.dash_to(w,{3,1},1,7,false));unchanged(); // Final readiness changed after selection.
 w=reset();cambc::state.flip_round_query=2;assert(!h.dash_to(w,{3,1},1,7,false));unchanged();assert(cambc::state.round==8); // Guard's second clock refuses drift during legality.
 w=reset();assert(!h.dash_to(w,{-1,1},1,7,false));unchanged();
 w=reset();assert(!h.dash_to(w,{3,1},2,7,false));unchanged();
}
static void synthetic_hazard_and_failed_use(){auto w=reset();Harness hazard;hazard.safe=false;
 assert(hazard.dash_to(w,{3,1},1,7,false));assert(hazard.safety_checks==0&&cambc::state.position==Point(3,1));
 // The configured hazard callback would reject every landing. This proves
 // only its zero-modifier bypass; it predicts no real damage or native risk.
 w=reset();Harness damaging;damaging.safe=false;cambc::state.modifier=1;
 assert(!damaging.dash_to(w,{3,1},1,7,false));assert(damaging.safety_checks>0);unchanged();
 w=reset();Harness failed;cambc::state.engine_success=false;
 assert(!failed.dash_to(w,{3,1},1,7,false));assert(cambc::state.uses==1&&cambc::state.attempted==Point(3,1));
 assert(cambc::state.position==Point(1,1)&&cambc::state.cooldown==0&&cambc::state.cast);
 // A one-step alternative remains in the candidate mask, but failure returns
 // immediately. There is no second attempt, fallback displacement or spending.
}
int main(){intended_and_substituted();refreshed_refusals();synthetic_hazard_and_failed_use();
 std::cout<<"PASS extracted production Runtime::dash_to: intended two-step, substituted one-step with no walking advantage, refreshed refusals, synthetic zero-modifier hazard bypass, failed use without fallback; bounded doubles only, no native ability or fuel claim\n";}
