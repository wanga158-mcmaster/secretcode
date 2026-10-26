#ifndef POST_EXPORT_BODY_HEADER
#error Requires the verbatim production Navigation::walk method
#endif
#include "../common/src/contract.h"
#include "../common/src/guard.h"
#include <cassert>
#include <iostream>
#include <span>

// Bounded8x8 mechanics and an explicitly supplied post-export allowance.
// This tests production control flow; it does not meter engine or guest fuel.
namespace cambc {
using vec2=p10::Point;
struct State {int round=7,fuel=60000,after_export=40000,exports=0,post_export_positions=0,moves=0;bool exported=false,advance=false,fail_first=false;vec2 position{1,1};} state;
struct bitboard {
 std::uint64_t bits=0;
 bool is_set(vec2 p)const{return p.x>=0&&p.y>=0&&p.x<8&&p.y<8&&(bits&(std::uint64_t{1}<<(p.y*8+p.x)));}
 void set(vec2 p,bool value=true){const auto b=std::uint64_t{1}<<(p.y*8+p.x);if(value)bits|=b;else bits&=~b;}
 bool is_empty()const{return bits==0;}
 std::vector<vec2>positions()const{if(state.exported)++state.post_export_positions;std::vector<vec2>out;for(int y=0;y<8;++y)for(int x=0;x<8;++x)if(is_set({x,y}))out.push_back({x,y});return out;}
 bitboard reachable_tiles(vec2 p,int radius)const{bitboard out;for(int y=0;y<8;++y)for(int x=0;x<8;++x)if(!is_set({x,y})&&p10::distance({x,y},p)<=radius)out.set({x,y});return out;}
 bitboard&operator&=(bitboard b){bits&=b.bits;return *this;}
 friend bitboard operator&(bitboard a,bitboard b){return {a.bits&b.bits};}
 friend bitboard operator|(bitboard a,bitboard b){return {a.bits|b.bits};}
 friend bitboard operator~(bitboard a){return {~a.bits};}
};
struct distance_field {std::array<std::uint16_t,64>values{};};
struct champion {bool can_move()const{return true;}vec2 position()const{return state.position;}bool move_absolute(vec2 p){++state.moves;if(state.fail_first&&state.moves==1)return false;state.position=p;return true;}};
inline champion&self(){static champion me;return me;}
inline std::uint64_t fuel_remaining(){return std::uint64_t(state.fuel);}
inline int round_number(){return state.round;}
namespace map {inline int width(){return 8;}inline int height(){return 8;}}
}
namespace p10 {
inline Point point(cambc::vec2 p){return p;}
inline bool same_round(int round){return cambc::state.round==round;}
inline bool channel_active(){return false;}
inline int linear_distance(std::span<const std::uint16_t>values,int width,int height,Point p){if(p.x<0||p.y<0||p.x>=width||p.y>=height)return -1;return values[p.y*width+p.x];}
inline void export_current_distances(const cambc::distance_field&f,int,int,std::vector<std::uint16_t>&out){out.assign(f.values.begin(),f.values.end());++cambc::state.exports;cambc::state.exported=true;cambc::state.fuel=cambc::state.after_export;if(cambc::state.advance)++cambc::state.round;}
template<class...T>void trace(const T&...){}
struct Harness {
 const char*walk_phase="none";int walk_fuel=0,last_blocked_round=-1;const World*partial_walk_frame=nullptr;std::vector<std::uint16_t>walk_values;
 cambc::bitboard blocked()const{return {};}
 void compute(cambc::distance_field&f,const cambc::bitboard&,const cambc::bitboard&goals){for(int y=0;y<8;++y)for(int x=0;x<8;++x){int best=64;for(auto goal:goals.positions())best=std::min(best,int(distance({x,y},goal)));f.values[y*8+x]=std::uint16_t(best);}}
#include POST_EXPORT_BODY_HEADER
};
}
static void reset(int after,bool advance=false){cambc::state={};cambc::state.after_export=after;cambc::state.advance=advance;}
static bool walk(p10::Harness&nav){cambc::bitboard goals;goals.set({6,1});return nav.walk(goals,7);}
int main(){
 reset(714);p10::Harness low;assert(!walk(low)&&cambc::state.exports==1&&cambc::state.moves==0);
#if P10_POST_EXPORT_GUARD
 assert(cambc::state.post_export_positions==0);
#else
 assert(cambc::state.post_export_positions>0);
#endif
 reset(40000,true);p10::Harness stale;assert(!walk(stale)&&cambc::state.moves==0);
#if P10_POST_EXPORT_GUARD
 assert(cambc::state.post_export_positions==0);
#endif
 reset(40000);p10::Harness high;assert(walk(high)&&cambc::state.moves==1&&cambc::state.position==(p10::Point{2,0}));
 reset(17999);p10::Harness below;assert(!walk(below)&&cambc::state.moves==0);
 reset(18000);p10::Harness edge;assert(walk(edge)&&cambc::state.moves==1&&cambc::state.position==(p10::Point{2,0}));
 reset(40000);p10::World incomplete;p10::Harness unsafe;unsafe.partial_walk_frame=&incomplete;assert(!walk(unsafe)&&cambc::state.moves==0);
 reset(40000);cambc::state.fail_first=true;p10::Harness retry;assert(walk(retry)&&cambc::state.moves==2&&retry.last_blocked_round==7&&cambc::state.exports==2);
 std::cout<<"PASS production Navigation::walk supplied714tail, export-round drift, unchanged funded ordering and failed-move retry; guard="<<P10_POST_EXPORT_GUARD<<"; no native cost or yield claim\n";
}
