#pragma once
#include <array>
#include <algorithm>
#include <cmath>
namespace shield_foundry {
struct Point {int x=0,y=0;};
inline int distance(Point a,Point b){return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
struct Body {int id=-1;Point tile{};double hp=0,max_hp=0,shield=0,focus=0,strength=0;int shield_until=-1,shield_ready=0;bool ruin=false,eligible=true;};
struct Attack {Point tile{};double damage=0;int range=0,stunned_until=-1;bool awake=true;};
struct TankPair {std::array<Point,2> tile{};std::array<int,12> targetAssignments{};int count=0;bool free_exchange=false;};
struct Phase {int owner=0;Point destination{};bool move=false,shield=false,heal=false,landing=false;int round=-1;};
struct Trajectory {
 std::array<Body,2> bodies{};std::array<Attack,12> monsters{};int count=0;
 std::array<Phase,16> phases{};int length=0;int healer_ready=0;double heal=360;
 int clearance=0,capture=5,bank_travel=0,bank_ticks=1,renew_travel=0,missed_attacks=0;
 bool observation_complete=true,contested=false;int round=0;
};
struct Outcome {bool admissible=false;int transfer_count=0,bank=0,renewal=0;std::array<double,2>hp{},shield{};int heals=0,casts=0;};
inline int strict_target(const Attack&m,const std::array<Body,2>&b){
 int target=-1,best=999;bool tied=false;
 for(int i=0;i<2;++i)if(b[i].eligible&&b[i].hp>0){int d=distance(m.tile,b[i].tile);if(d<=m.range&&d<best){target=i;best=d;tied=false;}else if(d<=m.range&&d==best)tied=true;}
 return tied?-2:target;
}
inline double ruin_multiplier(const Body&b){return b.ruin&&b.max_hp>0?1+.5*(1-std::clamp(b.hp/b.max_hp,0.,1.)):1.;}
inline Outcome run(Trajectory t,bool allow_transfer){
 Outcome out;if(!t.observation_complete||t.contested||t.count>12||t.length>16||t.length<1)return out;
 int previous=-1;for(int i=0;i<t.count;++i)if(t.monsters[i].awake&&t.monsters[i].damage>0){const int lead=strict_target(t.monsters[i],t.bodies);if(lead>=0){if(previous>=0&&previous!=lead){previous=-1;break;}previous=lead;}}
 auto camp=[&](int round){
  int lead=-1;
  for(int i=0;i<t.count;++i){const auto&m=t.monsters[i];if(!m.awake||m.stunned_until>=round||m.damage<=0)continue;
   int target=strict_target(m,t.bodies);if(target==-2)return false;if(target<0)continue;
   if(lead<0)lead=target;else if(lead!=target)lead=2;
   auto&b=t.bodies[target];const double absorbed=std::min(b.shield,m.damage);b.shield-=absorbed;b.hp-=m.damage-absorbed;if(b.hp<=0)return false;
  }
  if(lead>=0&&lead<2){if(previous>=0&&previous!=lead){if(!allow_transfer)return false;++out.transfer_count;}previous=lead;}
  return true;
 };
 int current=-1;std::array<bool,2>cast{},moved{};
 for(int step=0;step<t.length;++step){const auto&p=t.phases[step];const int round=p.round<0?step:p.round;if(round<current||round>15||p.owner<0||p.owner>1)return out;
  if(round!=current){if(current>=0&&!camp(current))return out;for(int r=current+1;r<round;++r)if(!camp(r))return out;current=round;cast={false,false};moved={false,false};}
  auto&b=t.bodies[p.owner];if(b.shield_until<=round)b.shield=0;
  if(p.move){if(moved[p.owner]||distance(b.tile,p.destination)>1||distance(t.bodies[1-p.owner].tile,p.destination)==0)return out;b.tile=p.destination;moved[p.owner]=true;}
  if((p.shield?1:0)+(p.landing?1:0)>1||(cast[p.owner]&&(p.shield||p.landing)))return out;
  if(p.shield){if(b.shield_ready>round)return out;b.shield=b.focus*(b.ruin?1.5:2.5);b.shield_until=round+5;b.shield_ready=round+8;cast[p.owner]=true;++out.casts;}
  if(p.heal){if(t.healer_ready>round)return out;b.hp=std::min(b.max_hp,b.hp+t.heal);t.healer_ready=round+8;++out.heals;}
  if(p.landing){cast[p.owner]=true;++out.casts;for(int i=0;i<t.count;++i)if(distance(t.monsters[i].tile,b.tile)<=1)t.monsters[i].stunned_until=round;}
 }
 if(!camp(current))return out;
 // Two explicit next camp phases price a failed continuation/escape. An expired
 // shield contributes zero even if its original five-tick label is remembered.
 for(int r=current+1;r<=current+2;++r){for(auto&b:t.bodies)if(b.shield_until<=r)b.shield=0;if(!camp(r))return out;}
 for(int i=0;i<2;++i){out.hp[i]=t.bodies[i].hp;out.shield[i]=t.bodies[i].shield;}
 out.bank=t.clearance+1+t.capture+t.bank_travel+t.bank_ticks;
 out.renewal=out.bank+t.renew_travel+t.missed_attacks;
 out.admissible=out.renewal<=96;return out;
}
inline bool productive_transfer(const Outcome&active,const Outcome&fixed){return active.admissible&&active.transfer_count>0&&(!fixed.admissible||active.renewal<fixed.renewal);}
inline bool changed(const Trajectory&a,const Trajectory&b){
 if(a.count!=b.count||b.contested||!b.observation_complete)return true;
 for(int i=0;i<2;++i)if(a.bodies[i].hp!=b.bodies[i].hp||a.bodies[i].shield!=b.bodies[i].shield||a.bodies[i].shield_until!=b.bodies[i].shield_until||distance(a.bodies[i].tile,b.bodies[i].tile))return true;
 for(int i=0;i<a.count;++i)if(a.monsters[i].damage!=b.monsters[i].damage||a.monsters[i].stunned_until!=b.monsters[i].stunned_until)return true;
 return false;
}
} // namespace shield_foundry
