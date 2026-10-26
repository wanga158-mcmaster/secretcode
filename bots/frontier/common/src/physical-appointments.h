#pragma once
#include "mobile-work.h"
namespace p10_b {
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
}
