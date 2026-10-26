#pragma once
#include "model.h"
namespace p10::arena {
struct Policy {int variant=0,source=-1,encounterRound=-1;bool payout=false,banked=false,renewed=false;
 static constexpr bool observe_enemy_exits=true;
 using FuelReader=int(*)();FuelReader fuel_reader=nullptr;int fresh_programs=0,aborted_programs=0;
 void set_fuel_reader(FuelReader reader){fuel_reader=reader;}
 bool fuel_ok(const World&w)const{return (fuel_reader?fuel_reader():w.fuel)>=std::max(26000,w.action_reserve+500);}
 Directive cheap(const World&w)const{
  if(const auto*g=w.group(source);g&&usable(*g)&&g->safe_solo&&g->travel>=0&&g->to_bank>=0)return farm(g->id,"arena fuel abort retains fresh independent productive source");
  return ordinary(w);
 }
 static Directive first_action(const World&w,const Group&g,const Actor&r,Program program,const Encounter&e){
  Directive d;d.group=g.id;d.reserve_reset=true;d.template_id=120+int(program);d.reason="finite camp/rival continuation with own bankable fallback";
  if(program==Program::burst_rival){d.task=Task::attack;d.actor=r.id;
   for(int slot=0;slot<2;++slot)if(w.self.abilities[slot].ready&&(w.self.abilities[slot].damage||w.self.abilities[slot].aoe||w.self.abilities[slot].cc)&&distance(w.self.position,r.position)<=w.self.abilities[slot].range){d.cast_slot=slot;break;}
  }else if(program==Program::shield_exit){d.task=Task::move;d.destination=e.phases[e.phaseCount-1].own;
   for(int slot=0;slot<2;++slot)if(w.self.abilities[slot].ready&&w.self.abilities[slot].shield){d.task=Task::shield;d.cast_slot=slot;break;}
  }else if(program==Program::wait_outside){d.task=Task::wait;d.reason="productive outside observation while actual rival advances camp";
  }else if(program==Program::contest_clear){d.task=Task::capture;
  }else {d.task=Task::farm;
   for(const auto&m:g.monsters)if(m.alive&&(d.monster<0||m.hp<g.hp)){d.monster=m.id;break;}
   for(int slot=0;slot<2;++slot)if(w.self.abilities[slot].ready&&(w.self.abilities[slot].damage||w.self.abilities[slot].aoe||w.self.abilities[slot].cc)){d.cast_slot=slot;break;}
  }
  return d;
 }
 Directive step(const World&w,Arm arm){
  fresh_programs=aborted_programs=0;
  if(w.own_payout&&source<0)source=w.last_payout_group;
  if(w.own_payout&&w.last_payout_group==source)payout=true;if(payout&&w.own_bank_completed)banked=true;if(banked&&w.productive_group>=0)renewed=true;
  if(w.self.channel!=Channel::none&&w.safe_now)return wait();if(w.self.bag>0)return bank("arena actual capture conversion");if(!w.safe_now)return {Task::recover};
  if(w.self.type==Champion::marcella||w.self.type==Champion::tavi||!fuel_ok(w)||w.fuel<w.action_reserve+(variant?15000:1800))return cheap(w);
  int fresh=0;for(const auto&g:w.groups){if(!fuel_ok(w))return cheap(w);if(fresh>=1)break;if(!g.observed||!g.complete||!g.route_known||g.travel<0||g.to_bank<0||g.monsters.size()>48||(!variant&&w.fuel<w.action_reserve+1500+350*int(g.monsters.size())))continue;
   for(const auto&r:w.enemies){if(!fuel_ok(w))return cheap(w);if(!r.visible||!r.alive||distance(r.position,g.position)>6)continue;++fresh;
    const Group*alternative=nullptr;for(const auto&h:w.groups){if(!fuel_ok(w))return cheap(w);if(h.id!=g.id&&usable(h)&&h.safe_solo&&(!alternative||h.travel<alternative->travel))alternative=&h;}if(!alternative)break;
    Encounter e;e.own=w.self;e.rival=r;e.camp=g;e.phaseCount=4;e.terrainKnown=w.geometry_complete;e.turnOrderKnown=true;e.ownFirst=w.our_team_starts;e.hiddenEligibleActor=!w.opposition_complete;
    e.fuel_reader=fuel_reader;e.fuel_floor=std::max(26000,w.action_reserve+500);if(!fuel_ok(w))return cheap(w);
    e.fallbackSource=alternative->id;e.fallbackGems=alternative->gems;e.fallbackBank=cycle_ticks(w,*alternative);e.ownBank=cycle_ticks(w,g);e.renewal=std::max(1,alternative->travel);
    // Two distinct physically open rival departures are required. Route
    // corridors come from current group approach and the observed other source.
    int exits=0;for(auto pad:w.visible_open_tiles){if(!fuel_ok(w))return cheap(w);if(exits<2&&distance(r.position,pad)==1){e.rivalExits[exits]=pad;e.openExit[exits]=true;for(auto&step:e.exitPath[exits])step=pad;++exits;}}
    if(exits<2)break;
    Point switchStep=r.position;for(auto pad:w.visible_open_tiles){if(!fuel_ok(w))return cheap(w);if(distance(pad,r.position)==1&&distance(pad,w.self.position)<distance(switchStep,w.self.position))switchStep=pad;}
    for(auto&step:e.switchPath)step=switchStep;
    for(int t=0;t<4;++t)e.phases[t]={w.self.position,r.position,w.self.can_attack,w.self.can_cast&&t==0,false,true,r.can_cast&&t==0,false,false};
    for(auto pad:w.visible_open_tiles){if(!fuel_ok(w))return cheap(w);if(distance(pad,w.self.position)==1){bool outside=true;for(const auto&m:g.monsters){if(!fuel_ok(w))return cheap(w);outside&=!m.alive||m.strength==0||distance(pad,m.position)>m.range;}if(outside){e.phases[3].own=pad;e.phases[3].ownMove=true;e.phases[3].ownAttack=false;e.phases[3].ownCast=false;break;}}}
    Program candidate=Program::finish_camp;
    if(!variant){
     bool outside=true;for(const auto&m:g.monsters){if(!fuel_ok(w))return cheap(w);outside&=!m.alive||m.strength==0||distance(w.self.position,m.position)>m.range;}
     if(g.cleared)candidate=Program::contest_clear;
     else if(r.hp<=w.self.strength+w.self.focus&&distance(w.self.position,r.position)<=std::max(1,w.self.range))candidate=Program::burst_rival;
     else if(outside)candidate=Program::wait_outside;
     else if(w.self.hp<w.self.max_hp*.5)for(const auto&ab:w.self.abilities)if(ab.ready&&ab.shield)candidate=Program::shield_exit;
    }
    if(!fuel_ok(w))return cheap(w);++fresh_programs;auto p=evaluate(e,candidate);if(p.fuel_aborted){++aborted_programs;return cheap(w);}
    // One tactical alternative and one direct source program, never a live
    // six-program tree. Both arms use this same legal tactical vocabulary.
    if(candidate!=Program::finish_camp){if(!fuel_ok(w))return cheap(w);++fresh_programs;auto direct=evaluate(e,Program::finish_camp);if(direct.fuel_aborted){++aborted_programs;return cheap(w);}if(deploy(direct,alternative->gems,arm)&&(!deploy(p,alternative->gems,arm)||direct.adverse>p.adverse||(direct.adverse==p.adverse&&direct.endpoint<p.endpoint)))p=direct;}
    if(deploy(p,alternative->gems,arm)){source=g.id;encounterRound=w.round;return first_action(w,g,r,p.program,e);}
    // The principal control still farms the same safe local opportunity and
    // uses the same executor; it simply omits new camp-mediated deployment.
    if(arm==Arm::control&&g.safe_solo&&!g.contested)return farm(g.id,"same tactical executor; independent arena production");
    break;
   }
  }return ordinary(w);
 }
};
} // namespace p10::arena
