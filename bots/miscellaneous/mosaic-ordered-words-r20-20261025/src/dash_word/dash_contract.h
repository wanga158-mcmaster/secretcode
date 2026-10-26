#pragma once
// Persistent data is nomination only. This contract grants no SDK, visibility,
// occupancy, health, fuel or future-path authority.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <span>
namespace mosaic::dash_word {
inline constexpr int max_steps=4,max_width=9,max_cells=81;
struct Point {int x{},y{};bool operator==(const Point&)const=default;};
inline int distance(Point a,Point b){return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
struct Body {
 int epoch{-1};std::uint32_t actor{};Point pos;
 bool alive{},can_move{},can_attack{},can_cast{},stunned{},channelled{},completed{};
};
struct Dash {
 int slot{-1},steps{},cooldown{-1};bool ground{},exclusive{},pure_movement{};
 float radius{},modifier{};int stun_duration{};
};
struct Snapshot {Body body;Dash dash;};
enum class Status {ready,no_dash,unavailable,unsupported,stale,deferred,no_candidate,refused,applied,stale_after_attempt};
inline Status admission(const Body& b,const Dash& d,bool walked,bool pure_only=true) {
 if(d.slot<0||d.slot>1||d.steps<1||d.steps>max_steps)return Status::unsupported;
 if(pure_only&&!d.pure_movement)return Status::unsupported;
 if(!b.alive||b.stunned||b.channelled||b.completed||!b.can_cast||d.cooldown!=0||
    !d.ground||(d.exclusive&&walked))return Status::unavailable;
 return Status::ready;
}
inline bool same_context(const Body& a,const Body& b) {
 return a.epoch>=0&&a.epoch==b.epoch&&a.actor==b.actor&&a.pos==b.pos&&a.alive==b.alive&&
 a.can_move==b.can_move&&a.can_attack==b.can_attack&&a.can_cast==b.can_cast&&
 a.stunned==b.stunned&&a.channelled==b.channelled&&a.completed==b.completed;
}
inline bool same_dash(const Dash& a,const Dash& b) {
 return a.slot==b.slot&&a.steps==b.steps&&a.cooldown==b.cooldown&&a.ground==b.ground&&
 a.exclusive==b.exclusive&&a.pure_movement==b.pure_movement&&a.radius==b.radius&&
 a.modifier==b.modifier&&a.stun_duration==b.stun_duration;
}
// Finite conditional eight-neighbor graph model for fixtures. The SDK adapter
// uses the native reachable-mask operator. Transit omits holes, landing keeps
// them. A result here is NOT an engine/path/safety certificate.
struct Grid {
 int width{},height{};Point origin;int steps{};
 std::span<const std::uint8_t> transit_blocked,landing_blocked;
};
struct ModelMenu {
 std::array<Point,max_cells> points{};std::size_t count{};bool complete{};
 bool contains(Point p)const{return std::find(points.begin(),points.begin()+count,p)!=points.begin()+count;}
};
inline ModelMenu model_menu(const Grid& g) {
 ModelMenu r;
 if(g.width<1||g.height<1||g.width>max_width||g.height>max_width||g.steps<1||g.steps>max_steps||
    g.origin.x<0||g.origin.y<0||g.origin.x>=g.width||g.origin.y>=g.height)return r;
 const auto n=std::size_t(g.width*g.height);
 if(g.transit_blocked.size()!=n||g.landing_blocked.size()!=n)return r;
 std::array<Point,max_cells> q;std::array<int,max_cells> depth;std::fill_n(depth.begin(),n,-1);
 std::size_t head=0,tail=1;q[0]=g.origin;depth[std::size_t(g.origin.y*g.width+g.origin.x)]=0;
 while(head<tail) {
  const auto p=q[head++];const auto d=depth[std::size_t(p.y*g.width+p.x)];
  if(p!=g.origin&&!g.landing_blocked[std::size_t(p.y*g.width+p.x)])r.points[r.count++]=p;
  if(d==g.steps)continue;
  for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
   if(!x&&!y)continue;const Point v{p.x+x,p.y+y};
   if(v.x<0||v.y<0||v.x>=g.width||v.y>=g.height)continue;
   const auto at=std::size_t(v.y*g.width+v.x);
   if(depth[at]>=0||g.transit_blocked[at])continue;
   depth[at]=d+1;q[tail++]=v;
  }
 }
 r.complete=true;return r;
}
} // namespace mosaic::dash_word
