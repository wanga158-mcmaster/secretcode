#pragma once
#include "../../common/src/economic-observation.h"
namespace p10_b12::independent {
using namespace p10;
struct Choice {bool selected=false,fuel_aborted=false;Directive first{};double value=0,damage=0,avoided=0,home_value=0;int examined=0;};
// The independent arm credits no camp damage to a rival, no future regional
// ownership, and no promised helper damage. Its legal immediate menu is shared
// with active: ordinary basic, one damage/CC cast plus basic, or shield/basic.
// Camp basic/reset/basic stays in the same mobile-work selector in both arms.
inline Choice choose(const World&w,const Group&home,const p10_mobile::Result&work,const p10_mobile::Budget&budget){
 Choice best;const int cycle=work.feasible?work.clear_turns+w.capture_turns+1+home.to_bank+w.ticks(w.self.bag+home.gems):cycle_ticks(w,home);
 best.home_value=(home.gems*8+home.gold*.04)/std::max(1,cycle);best.value=best.home_value;
 if(!w.safe_now||!w.geometry_complete||!w.actor_observation_complete||!w.self.combat_details_complete)return best;
 const auto enough=[&](){return budget.fuel_ok();};
 const double floor=std::max(1.,w.self.max_hp*.08);
 double camps=0;
 for(const auto&g:w.groups)for(const auto&m:g.monsters){if(!enough()){best.fuel_aborted=true;return best;}
  if(m.alive&&m.strength>0&&distance(w.self.position,m.position)<=m.range){if(!g.observed||!g.complete||!m.observed)return best;camps+=m.strength;}}
 double all_enemy=0;int rivals=0;
 auto threat=[&](const Actor&r){double dash=0;bool reset=false;
  for(const auto&a:r.abilities)if(a.ready){if(a.dash)dash=std::max(dash,a.range);reset|=a.reset;}
  double result=distance(w.self.position,r.position)<=r.range+2+dash?r.strength*(reset?2:1):0.;
  for(const auto&a:r.abilities)if(a.ready&&(a.damage||a.aoe||a.cc||a.dash)&&distance(w.self.position,r.position)<=a.range+a.radius+1)result+=std::max(0.,a.modifier*r.focus);
  return result*(r.type==Champion::heart?1.5:1.);};
 for(const auto&r:w.enemies){if(!enough()){best.fuel_aborted=true;return best;}if(r.visible&&r.alive){if(!r.combat_details_complete)return best;all_enemy+=threat(r);++rivals;}}
 if(rivals>4)return best;
 auto collateral=[&](Point center,double radius){double incoming=0;
  for(const auto&g:w.groups){if(!enough()){best.selected=false;best.fuel_aborted=true;return false;}if(g.id==home.id||g.respawning||g.cleared)continue;bool struck=false;
   for(const auto&m:g.monsters){if(!enough()){best.selected=false;best.fuel_aborted=true;return false;}struck|=m.alive&&distance(m.position,center)<=radius;}
   if(struck){if(!g.observed||!g.complete)return false;for(const auto&m:g.monsters){if(!enough()){best.selected=false;best.fuel_aborted=true;return false;}if(m.alive&&m.hp>0&&distance(m.position,w.self.position)<=m.range)incoming+=m.strength;}}
  }return incoming*3<w.self.hp;};
 for(const auto&r:w.enemies)if(r.visible&&r.alive){
  if(!enough()){best.selected=false;best.fuel_aborted=true;return best;}
  const bool basic=w.self.can_attack&&distance(w.self.position,r.position)<=w.self.range&&(w.self.type!=Champion::varg||collateral(r.position,1));
  if(best.fuel_aborted)return best;
  for(int slot=-1;slot<2;++slot){if(!enough()){best.selected=false;best.fuel_aborted=true;return best;}
   double damage=basic?w.self.strength:0,shield=0,avoided=0;bool cast=false;
   if(slot>=0){const auto&a=w.self.abilities[slot];if(!w.self.can_cast||!a.ready)continue;
    if((a.damage||a.aoe||a.cc)&&distance(w.self.position,r.position)<=a.range){if(!collateral(r.position,a.radius)){if(best.fuel_aborted)return best;continue;}cast=true;damage+=std::max(0.,a.modifier*w.self.focus);
     // One-round Ren/Tavi control removes one rival action window, regardless
     // of which team starts. Never erase two windows for a one-round stun.
     if(a.cc&&a.duration>=1&&r.type!=Champion::marcella)avoided=std::min(2,a.duration)*threat(r);
    }else if(a.shield&&a.targets_self&&a.duration>=3){cast=true;shield=std::max(0.,a.modifier*w.self.focus);}
    else continue;
   }
   if(!basic&&!cast)continue;++best.examined;
   // Full inventory projects only Health Durability, not its Strength/Focus
   // variants. Charge the largest possible current Durability reduction for
   // every rival instead of inventing absence of an unrepresented emblem.
   damage*=.9;const bool kill=damage>=r.hp+r.shield;
   const double without=2*(camps*(w.self.jungle_emblem?.7:1.)+all_enemy);
   const double removed=kill?2*threat(r):avoided;
   const double existing=w.self.shield_until>=w.round+2?w.self.shield:0;
   const double spell_damage=slot>=0&&(w.self.abilities[slot].damage||w.self.abilities[slot].aoe||w.self.abilities[slot].cc)?std::max(0.,w.self.abilities[slot].modifier*w.self.focus):0;
   const double retaliation=r.type==Champion::ren?.2*r.focus*((spell_damage>0?1:0)+(basic&&spell_damage*.9<r.hp+r.shield?1:0)):0;
   const double exposure=std::max(0.,without-removed-shield-existing)+retaliation;
   if(w.self.hp<=exposure+floor)continue;
   // A direct kill can pay its own actual bag/bounty continuation. A nonkill
   // is admitted only as useful current defense, not speculative harassment.
   double value=0;
   if(kill){const int bank=p10_b::altar_trip(w);if(r.bag>0&&(bank>w.remaining()||bank+w.ticks(w.self.bag+r.bag)>w.remaining()))continue;
    value=4+(r.bag*8.)/std::max(1,bank+w.ticks(w.self.bag+r.bag))+damage*.001;
   }else if((avoided+shield)>0&&w.self.hp<=without+floor){value=(avoided+shield)*.1;}
   if(value<=best.value)continue;
   Directive d;d.task=Task::attack;d.actor=r.id;d.cast_slot=slot;d.group=home.id;d.valid_round=w.round;d.reserve_reset=d.strict_tank=true;d.template_id=1212;
   d.reason="same-menu independent burst/control/defense beats the complete home circuit without third-party damage credit";
   best.selected=true;best.first=d;best.value=value;best.damage=damage;best.avoided=avoided;
  }
 }
 return best;
}
}
