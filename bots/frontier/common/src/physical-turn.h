#pragma once
#include "mobile-work.h"
namespace p10_b_local {
// Physical adapter only: no source order, economic objective or roster choice.
struct Turn {
 const p10::World&w;p10_mobile::Budget budget;
 explicit Turn(const p10::World&world):w(world),budget(p10_mobile::budget_for(world)){}
 p10_mobile::Result prove(const p10::Group&g){p10_mobile::Options o;o.max_traces=1;return p10_mobile::mobile_work(w,g,budget,o);}
 p10::Directive work(const p10::Group&g,const char*why,int label){
  using namespace p10;
  if(g.cleared)return farm(g.id,"actual local custody retains the promised personal payout");
  if(!g.observed||!g.complete){Directive d;d.task=Task::inspect;d.group=g.id;d.destination=g.approach;d.reason="private current inspection precedes mobile proof";return w.safe_now?d:Directive{Task::recover};}
  if(distance(w.self.position,g.position)>w.self.range+2){Directive d;d.task=Task::move;d.group=g.id;d.destination=g.approach;d.reason="ordinary safe ingress; the mobile program must be rebuilt on arrival";return w.safe_now?d:Directive{Task::recover};}
  auto proof=prove(g);if(!proof.feasible)return w.safe_now?wait("complete local mobile work currently unavailable"):Directive{Task::recover};
  auto d=proof.first;d.reason=why;d.template_id=label;return d;
 }
 static p10::Directive first(const p10_mobile::Result&r,const char*why,int label){auto d=r.first;d.reason=why;d.template_id=label;return d;}
};
inline bool on_spawn(const p10::Actor&a,const p10::Group&g){return std::find(g.spawns.begin(),g.spawns.end(),a.position)!=g.spawns.end();}
inline bool safe_custody(const p10::World&w,const p10::Group&g,const p10::Actor&a){
 if(!p10::usable(g)||!g.cleared||!a.visible||!a.alive||!on_spawn(a,g)||!w.geometry_complete||!w.observation_complete)return false;
 for(const auto&m:g.monsters)if(!m.observed||(m.alive&&m.hp>0))return false;
 for(const auto&e:w.enemies)if(e.visible&&e.alive&&p10::distance(e.position,a.position)<=8)return false;
 double incoming=0;for(const auto&other:w.groups)if(other.id!=g.id&&!other.cleared&&!other.respawning){double phase=p10::phase_damage(other,a.position);if(phase>0&&(!other.observed||!other.complete))return false;incoming+=phase;}
 return a.hp>incoming*(w.capture_turns+2)+std::max(1.,a.max_hp*.1);
}
}
