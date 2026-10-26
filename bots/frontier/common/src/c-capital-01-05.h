#pragma once
#include "mobile-work.h"
namespace p10_c {
inline std::array<p10::Point,4>cardinals(p10::Point p){return {{{p.x+1,p.y},{p.x-1,p.y},{p.x,p.y+1},{p.x,p.y-1}}};}
inline bool on_spawn(const p10::Actor&a,const p10::Group&g){return a.visible&&a.alive&&a.channel==p10::Channel::none&&std::find(g.spawns.begin(),g.spawns.end(),a.position)!=g.spawns.end();}
inline const p10::Actor*visible_type(const p10::World&w,p10::Champion type){for(const auto&a:w.allies)if(a.type==type&&a.visible&&a.alive&&a.channel==p10::Channel::none)return &a;return nullptr;}
inline p10::Directive inspect(int id,const char*reason,int tag){auto d=p10::farm(id,reason);d.task=p10::Task::inspect;d.template_id=tag;return d;}
inline p10::Directive capture(int id,const char*reason,int tag){p10::Directive d;d.task=p10::Task::capture;d.group=id;d.reason=reason;d.template_id=tag;return d;}
inline int receipt_round(const p10::World&w){return w.last_payout_round>=0?w.last_payout_round:w.round;}
inline bool free_cardinal(const p10::World&w,p10::Point p){
 if(p10_mobile::manhattan(p,w.self.position)!=1||p10_mobile::occupied(w,p))return false;
 bool terrain=std::find(w.visible_open_tiles.begin(),w.visible_open_tiles.end(),p)!=w.visible_open_tiles.end();
 for(const auto&g:w.groups)for(const auto&m:g.monsters)if(m.position==p){if(m.alive&&m.hp>0)return false;if(g.observed&&g.complete&&m.observed)terrain=true;}
 return terrain;
}
inline bool safe_vacate(const p10::World&w,p10::Point p,p10::Directive&out){
 if(!w.self.equipment_details_complete||!free_cardinal(w,p)||p10_mobile::visible_enemy_can_intervene(w,p))return false;double exposure=0;
 for(const auto&g:w.groups)for(const auto&m:g.monsters)if(m.alive&&m.strength>0&&p10::distance(p,m.position)<=m.range){if(!g.observed||!g.complete||!m.observed)return false;if(m.awake)exposure+=m.strength;}
 const double required=std::max(1.,exposure*2*(w.self.jungle_emblem?.7:1.)+30);if(w.self.hp<=required)return false;
 out.task=p10::Task::move;out.destination=p;out.valid_round=out.safety_round=w.round;out.safety_verified=true;out.minimum_start_hp=required;out.reserve_reset=true;return true;
}
struct Physical {
 static constexpr bool observe_open_tiles=true;
 static constexpr bool owns_capital=true;
 int fresh_traces=0;
 struct Usage {p10_mobile::Budget&budget;int&out;~Usage(){out=budget.spent;}};
 Usage watch(p10_mobile::Budget&b){return {b,fresh_traces};}
 p10::WorkFuelReader reader=nullptr;std::array<bool,2>ground{};
 void set_fuel_reader(p10::WorkFuelReader r){reader=r;}
 void set_ground_targets(std::array<bool,2>mask){ground=mask;}
 p10_mobile::Budget budget(const p10::World&w)const{return p10_mobile::budget_for(w,reader?reader:p10::work_fuel_reader);}
 p10_mobile::Result prove(const p10::World&w,const p10::Group&g,p10_mobile::Budget&b,int preferred=-1,bool no_reset=false,p10_mobile::Mode mode=p10_mobile::Mode::threat_first)const{
  p10_mobile::Options o;o.ground_targets=ground;o.preferred_monster=preferred;o.suppress_reset=no_reset;o.mode=mode;o.max_traces=1;return p10_mobile::mobile_work(w,g,b,o);
 }
};
// Shared receipt/wallet mechanics only. Each card owns its item menu, named
// operating threshold, cycle count and admission price; no capital/source ranker.
struct Wallet {
 int issued=0,confirmed=0,trips=0,last_purchase=-2,last_bank=-2,last_payout=-2,request_round=-2;
 bool pending=false;double before_strength=0,before_focus=0,before_max_hp=0;
 void observe(const p10::World&w){
  int purchase=-2;for(const auto&r:w.receipts)if(r.kind==p10::Receipt::Kind::purchase&&r.actor==w.self.id)purchase=std::max(purchase,r.round);
  if(w.own_purchase&&purchase<0)purchase=w.round;
  if(purchase>last_purchase){last_purchase=purchase;++confirmed;pending=false;}
  if(w.own_bank_completed)last_bank=w.last_bank_round>=0?w.last_bank_round:w.round;
  if(w.own_payout)last_payout=w.last_payout_round>=0?w.last_payout_round:w.round;
  if(pending&&(w.self.strength>before_strength||w.self.focus>before_focus||w.self.max_hp>before_max_hp))pending=false;
  if(pending&&(w.last.kind==p10::Receipt::Kind::blocked||w.last.kind==p10::Receipt::Kind::death||w.round>request_round+2))pending=false;
 }
 bool available(const p10::World&w)const{
  if(pending||!w.safe_now||w.shard_cost<=0||w.own_gold<w.shard_cost||w.self.bag>0||w.self.channel!=p10::Channel::none||w.capture_active)return false;
  for(const auto&g:w.groups)if(g.cleared&&on_spawn(w.self,g))return false;return true;
 }
 static int detour(const p10::World&w,const p10::Group&g){if(shop(w))return 0;if(w.to_spawn<0||g.to_spawn<0)return 100000;return std::max(w.recall_turns,w.to_spawn)+g.to_spawn+1;}
 static bool shop(const p10::World&w){return w.self.at_spawn||!w.self.alive||w.self.type==p10::Champion::tavi;}
 p10::Directive issue(const p10::World&w,p10::Item item,int source,int tag,const char*why){
  p10::Directive d;if(!available(w))return d;d.group=source;d.template_id=tag;d.reason=why;
  if(!shop(w)){++trips;d.task=p10::Task::recover;return d;}
  d.task=p10::Task::buy;d.item=item;d.buy_count=1;pending=true;request_round=w.round;before_strength=w.self.strength;before_focus=w.self.focus;before_max_hp=w.self.max_hp;++issued;return d;
 }
};

}
