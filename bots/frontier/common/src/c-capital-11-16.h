#pragma once
#include "economic-observation.h"
#include <initializer_list>

// Mechanical purchase/receipt bookkeeping only. Each C policy chooses its own
// organization, packet, owner, timing and terminal stop rule.
namespace p10_c {
using namespace p10;
inline int power_units(int already,int added){return added+(already+added)/5-already/5;}
inline bool legal_shop(const World&w){return w.self.at_spawn||w.self.type==Champion::tavi;}
inline bool winning_bag(const World&w){return w.self.bag>0&&w.score+w.self.bag>=w.win_gems;}
inline bool near_awakening(const World&w){auto budget=p10_mobile::budget_for(w);for(const auto&g:w.groups)for(const auto&m:g.monsters){if(!budget.fuel_ok())return true;if(m.observed&&m.alive&&m.awake&&distance(w.self.position,m.position)<=m.range+1)return true;}return false;}
struct OwnedOrder {
 std::array<int,14>goal{};bool active=false;int opened=-1,expiry=-1,committedCost=0,completed=0,aborted=0;
 bool open(const World&w,std::initializer_list<std::pair<Item,int>>packet,int life=160){
  if(active)return false;int cost=0;auto next=goal;next.fill(0);
  for(auto[item,count]:packet){if(item==Item::none||item==Item::lock||count<0)return false;cost+=count*w.shard_cost;next[int(item)]=w.self.inventory[int(item)]+count;}
  if(cost<=0||cost>w.own_gold)return false;goal=next;active=true;opened=w.round;expiry=w.round+life;committedCost=cost;return true;
 }
 Directive poll(const World&w,const char*reason,int tag){
  if(!active)return {};
  Item next=Item::none;for(int i=1;i<14;++i)if(goal[i]>w.self.inventory[i]){next=static_cast<Item>(i);break;}
  if(next==Item::none){active=false;++completed;return {};}
  if(w.round>expiry||w.own_gold<w.shard_cost){active=false;++aborted;return {};}
  if(w.self.channel!=Channel::none)return wait("actual channel precedes personally funded capital service");
  if(!legal_shop(w)){if(w.self.bag>0)return bank("real owned bag closes before funded shop visit");Directive d;d.task=Task::recover;d.template_id=tag;d.reason=reason;return d;}
  auto d=buy(next,1,reason);d.template_id=tag;return d;
 }
};
}
