#pragma once
#include "mobile-work.h"

// Shared observation/legal plumbing only. Source rankings and organizations
// belong to the six separate policies below.
namespace p10_b {
using namespace p10;
struct Economy {
 std::array<int,3> seen{-1,-1,-1};int payouts=0,banks=0,purchases=0,lastSource=-1,lastPayout=-1,lastBank=-1;
 Gems earnedGems=0,bankedGems=0;int earnedGold=0,batch=0;bool payout=false,banked=false,renewed=false;
 void accept(Receipt::Kind kind,int round,int source,Gems gems,int gold){
  const int slot=EconomicInbox::slot(kind);if(slot<0||round<0||round<=seen[slot])return;seen[slot]=round;
  if(kind==Receipt::Kind::payout){++payouts;++batch;lastSource=source;lastPayout=round;earnedGems+=gems;earnedGold+=gold;payout=true;if(banked&&round>lastBank)renewed=true;}
  if(kind==Receipt::Kind::bank){++banks;batch=0;lastBank=round;bankedGems+=gems;banked=true;}
  if(kind==Receipt::Kind::purchase)++purchases;
 }
 void consume(const World&w){
  std::array<bool,3> found{};
  for(const auto&r:w.receipts)if(r.actor==w.self.id){const int slot=EconomicInbox::slot(r.kind);if(slot>=0){found[slot]=true;accept(r.kind,r.round,r.group,r.gems,r.gold);}}
  if(w.own_payout&&!found[0])accept(Receipt::Kind::payout,w.last_payout_round>=0?w.last_payout_round:w.round,w.last_payout_group,0,0);
  if(w.own_bank_completed&&!found[1])accept(Receipt::Kind::bank,w.last_bank_round>=0?w.last_bank_round:w.round,-1,0,0);
  if(w.own_purchase&&!found[2])accept(Receipt::Kind::purchase,w.round,-1,0,0);
 }
};
inline bool work_source(const Group&g){return usable(g)&&g.travel>=0&&g.to_bank>=0&&!g.monsters.empty()&&g.monsters.size()<=p10_mobile::max_mobs;}
inline int altar_trip(const World&w){int best=w.remaining()+1;for(const auto&a:w.altars)if(a.route_known&&!a.enemy_lock&&a.travel>=0)best=std::min(best,a.travel);return best;}
inline bool on_region(const Actor&a,const Group&g){return std::find(g.spawns.begin(),g.spawns.end(),a.position)!=g.spawns.end();}
inline p10_mobile::Options single_options(int cap=48){p10_mobile::Options o;o.turn_cap=cap;o.max_traces=1;return o;}
inline Directive witnessed(const World&w,const p10_mobile::Result&r,int tag,const char*reason){
 if(!r.feasible||r.actor!=w.self.id||r.round!=w.round)return wait("no current physical witness");
 auto d=r.first;d.template_id=tag;d.reason=reason;return d;
}
inline World visible_actor_frame(const World&w,const Actor&a){
 World f=w;f.self=a;f.own_gold=0;f.own_payout=f.own_bank_completed=f.own_purchase=false;f.receipts.clear();f.capture_active=false;
 std::erase_if(f.allies,[&](const auto&other){return other.id==a.id;});if(w.self.id!=a.id)f.allies.push_back(w.self);return f;
}
inline Directive approach(const Group&g,int tag,const char*reason){auto d=farm(g.id,reason);d.task=Task::inspect;d.template_id=tag;d.reserve_reset=true;return d;}
inline bool local_threat(const World&w){for(const auto&e:w.enemies)if(e.visible&&e.alive&&distance(e.position,w.self.position)<=e.range+2)return true;return false;}
}
