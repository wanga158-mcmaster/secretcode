#pragma once
#include "model.h"
#include "../../common/src/contract.h"
namespace catalyst_forge {
struct Compiled {ForgeSuffix suffix{};Result result{};p10::Directive first{};};
// Two handwritten organizations: preserve useful kill preparation, or greedy
// positioned burst. The executor receives the same first compound we simulate.
inline Compiled compile(const p10::World&w,const p10::Group&g,int mode,int(*fuel_reader)()=nullptr){
 Compiled c;auto&s=c.suffix;if(!g.complete||!g.observed||!w.geometry_complete||g.monsters.size()>12||!w.enemies.empty())return c;
 std::array<const p10::Actor*,3> crew{};int n=0;
 auto eligible=[](const p10::Actor&a){return a.alive&&a.visible&&a.channel==p10::Channel::none&&(a.type==p10::Champion::varg||a.type==p10::Champion::stellar||a.type==p10::Champion::vendetta);};
 if(!eligible(w.self))return c;crew[n++]=&w.self;
 for(const auto&a:w.allies)if(n<3&&eligible(a)&&p10::distance(a.position,g.position)<=6)crew[n++]=&a;
 std::sort(crew.begin(),crew.begin()+n,[](auto a,auto b){return a->priority!=b->priority?a->priority>b->priority:a->pick<b->pick;});
 int own=-1;for(int i=0;i<n;++i){const auto&a=*crew[i];if(a.id==w.self.id)own=i;
  const Kit kit=a.type==p10::Champion::varg?Kit::varg:a.type==p10::Champion::stellar?Kit::stellar:Kit::vendetta;
  s.actors[i]={a.id,kit,{a.position.x,a.position.y},float(a.hp),float(a.strength*(a.monster_emblem?1.1:1.)),float(a.focus*(a.monster_emblem?1.1:1.)),a.range,a.range>1,a.cooldown};
 }
 for(const auto&other:w.groups)if(other.id!=g.id&&!other.cleared&&!other.respawning)for(const auto&m:other.monsters)if(m.alive&&m.awake&&m.strength>0)for(int a=0;a<n;++a)if(p10::distance(crew[a]->position,m.position)<=m.range+2)return c;
 s.fuel_reader=fuel_reader;s.fuel_reserve=w.action_reserve+500;
 s.actors_count=n;s.count=int(g.monsters.size());s.capture_ticks=w.capture_turns;s.altar_travel=std::max(0,g.to_bank);s.tribute_ticks=w.ticks(w.self.bag+g.gems);s.next_source_travel=std::max(0,g.travel);s.observed_round=w.round;s.contested=g.contested;
 for(int i=0;i<s.count;++i){const auto&m=g.monsters[i];s.monsters[i]={m.id,g.id,{m.position.x,m.position.y},float(m.alive?m.hp:0),float(m.strength),m.range,m.awake,m.observed};}
 auto state=s;bool first_set=false;
 auto emit=[&](int actor,int round,Op op,int target,Point tile){
  if(s.step_count>=96)return false;
  HitSet h;if(target>=0)h=op==Op::area?hit_set(state,tile,1):op==Op::single?HitSet{std::uint16_t(1u<<target),true}:state.actors[actor].kit==Kit::varg?hit_set(state,state.monsters[target].tile,1,target):HitSet{std::uint16_t(1u<<target),true};
  if(target>=0&&(state.actors[actor].kit==Kit::varg||op==Op::area))for(const auto&other:w.groups)if(other.id!=g.id&&!other.cleared&&!other.respawning)
   for(const auto&m:other.monsters)if(m.alive&&p10::distance(m.position,{op==Op::area?tile.x:state.monsters[target].tile.x,op==Op::area?tile.y:state.monsters[target].tile.y})<=1)return false;
  s.steps[s.step_count++]={actor,round,op,target,tile,h};
  auto&a=state.actors[actor];
  if(op==Op::move){a.tile=tile;return true;}
  if(op==Op::reset){a.cooldown[0]=8;return true;}
  if(target>=0){const float damage=op==Op::basic?a.strength:a.focus*(op==Op::area?1.4f:2.f);
   for(int i=0;i<state.count;++i)if(h.members&(1u<<i)){state.monsters[i].hp-=damage;for(int j=0;j<state.count;++j)state.monsters[j].awake=true;if(state.monsters[i].hp<=0&&a.kit==Kit::vendetta)a.cooldown={0,0};}
   if(op!=Op::basic)a.cooldown[op==Op::area?1:0]=8;
  }return true;
 };
 for(int round=0;round<16;++round){bool remains=false;for(int i=0;i<s.count;++i)remains|=state.monsters[i].hp>0;if(!remains)break;
  for(int a=0;a<n;++a){if(fuel_reader&&fuel_reader()<w.action_reserve+500)return Compiled{};if(round==0&&a<own)continue;auto&actor=state.actors[a];
   int target=-1;double score=-1;
   for(int i=0;i<s.count;++i)if(state.monsters[i].hp>0){const auto&m=state.monsters[i];double value=m.damage>0?1000+m.damage:1;
    if(actor.kit==Kit::varg){int hits=0;for(int j=0;j<s.count;++j)if(state.monsters[j].hp>0&&distance(m.tile,state.monsters[j].tile)<=1)++hits;value+=hits*80;
     if(mode==0)for(int b=0;b<n;++b)if(state.actors[b].kit==Kit::vendetta&&distance(state.actors[b].tile,m.tile)<=2)
      for(int j=0;j<s.count;++j)if(state.monsters[j].hp>actor.strength&&state.monsters[j].damage==0&&state.monsters[j].hp-actor.strength<=state.actors[b].strength&&distance(m.tile,state.monsters[j].tile)<=1)value+=1500;
    }
    if(mode==0&&actor.kit==Kit::vendetta&&m.damage==0&&m.hp<=actor.strength&&actor.cooldown[0]>0)value+=2000;
    value/=1+std::max(0,distance(actor.tile,m.tile)-actor.range)*2;
    if(value>score){target=i;score=value;}
   }
   if(target<0)continue;
   bool moved=false,cast=false;const int first_index=s.step_count;
   if(distance(actor.tile,state.monsters[target].tile)>actor.range){
    if(round==0&&a==own&&!w.self.can_move)continue;
    Point destination=actor.tile;int best=distance(actor.tile,state.monsters[target].tile);
    for(auto p:w.visible_open_tiles){Point q{p.x,p.y};if(distance(q,actor.tile)!=1)continue;bool occupied=false;for(int b=0;b<n;++b)if(b!=a&&distance(state.actors[b].tile,q)==0)occupied=true;
     const int d=distance(q,state.monsters[target].tile);if(!occupied&&d<best){destination=q;best=d;}}
    if(destination.x==actor.tile.x&&destination.y==actor.tile.y)continue;
    if(!emit(a,round,Op::move,-1,destination))return Compiled{};moved=true;
   }
   if(actor.kit==Kit::stellar&&!moved){
    double area=0;for(int j=0;j<s.count;++j)if(state.monsters[j].hp>0&&distance(state.monsters[j].tile,state.monsters[target].tile)<=1)area+=std::min(double(state.monsters[j].hp),double(actor.focus*1.4f));
    bool preserve=false;if(mode==0)for(int b=0;b<n;++b)if(state.actors[b].kit==Kit::vendetta&&state.actors[b].cooldown[0]>0)preserve=true;
    if(actor.cooldown[1]==0&&(round>0||a!=own||w.self.can_cast)&&!preserve&&area>2*actor.focus&&distance(actor.tile,state.monsters[target].tile)<=4){if(!emit(a,round,Op::area,target,state.monsters[target].tile))return Compiled{};cast=true;}
    else if(actor.cooldown[0]==0&&(round>0||a!=own||w.self.can_cast)&&distance(actor.tile,state.monsters[target].tile)<=4){if(!emit(a,round,Op::single,target,{}))return Compiled{};cast=true;}
   }
   const bool area_only=cast&&s.steps[s.step_count-1].op==Op::area;
   if((round>0||a!=own||w.self.can_attack)&&!area_only&&state.monsters[target].hp>0&&distance(actor.tile,state.monsters[target].tile)<=actor.range&&(!moved||!actor.ranged)){
    if(!emit(a,round,Op::basic,target,{}))return Compiled{};
    if(!cast&&!moved&&actor.kit!=Kit::stellar&&actor.cooldown[0]==0&&(round>0||a!=own||w.self.can_cast)){
     int second=state.monsters[target].hp>0?target:-1;
     if(second<0)for(int i=0;i<s.count;++i)if(state.monsters[i].hp>0&&distance(actor.tile,state.monsters[i].tile)<=actor.range){second=i;break;}
     if(second>=0){if(!emit(a,round,Op::reset,-1,{})||!emit(a,round,Op::basic,second,{}))return Compiled{};cast=true;}
    }
   }
   if(a==own&&round==0&&!first_set&&s.step_count>first_index){
    const auto&first=s.steps[first_index];auto d=p10::farm(g.id,"execute actual positioned forge suffix");d.valid_round=w.round;d.template_id=mode==0?10:11;d.reserve_reset=true;
    if(first.op==Op::move){d.destination={first.tile.x,first.tile.y};d.monster=g.monsters[target].id;}
    else if(first.op==Op::area||first.op==Op::single){d.task=p10::Task::cast;d.cast_slot=first.op==Op::area?1:0;d.monster=first.op==Op::single?g.monsters[first.target].id:-1;if(first.op==Op::area)d.destination={first.tile.x,first.tile.y};}
    else {d.task=p10::Task::attack;d.monster=g.monsters[first.target].id;for(int k=first_index;k<s.step_count;++k)if(s.steps[k].op==Op::reset&&k+1<s.step_count){d.cast_slot=0;d.second_monster=g.monsters[s.steps[k+1].target].id;}}
    c.first=d;first_set=true;
   }
  }
  for(int a=0;a<n;++a)for(auto&cd:state.actors[a].cooldown)cd=std::max(0,cd-1);
 }
 c.result=execute(s);
 if(c.result.complete&&first_set){c.first.expected_min=w.round+c.result.clear_round+w.capture_turns+1;c.first.expected_max=w.round+c.result.bank_round;
  // Current action must survive independently of promised future partner hits.
  double exposure=0;for(const auto&m:g.monsters)if(m.alive&&p10::distance(w.self.position,m.position)<=m.range)exposure+=m.strength;
  c.first.safety_verified=w.self.hp>3*exposure+30;c.first.safety_round=w.round;c.first.minimum_start_hp=3*exposure+30;
 }
 return c;
}
}
