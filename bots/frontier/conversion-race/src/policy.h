#pragma once
#include "model.h"
namespace p10::race {
struct Policy {
 int variant=0;std::array<int,4> campaignSources{-1,-1,-1,-1};
 Gems previousEnemyScore=0;int previousRound=-1,batchGap=0;Gems observedBatch=0;int source=-1;bool payout=false,banked=false,renewed=false;
 std::array<CampaignProgram,4> programs{};RaceKey lastKey{};
 int committed=0,commitStart=-1,commitSource=-1,commitOther=-1,collector=-1;double beforePurchase=0;bool purchaseReceipt=false;
 Directive purchase_or_return(const World&w){
  if(w.self.strength>beforePurchase&&(w.own_purchase||purchaseReceipt)){purchaseReceipt=true;const auto*g=w.group(commitSource);if(!g||g->respawning||g->contested){committed=0;return ordinary(w);}if(!g->observed||!g->complete){Directive d;d.task=Task::inspect;d.group=commitSource;d.reason="funded race regime reobserves productive source";return d;}source=commitSource;return farm(source,"purchased personal race regime reaches real capture and conversion");}
  if(w.own_gold<w.shard_cost){committed=0;return ordinary(w);}if(!w.self.at_spawn&&w.self.type!=Champion::tavi)return {Task::recover};return buy(Item::monster_strength,1,"committed actual personal race capital before later deposits");
 }
 Directive custody(const World&w){
  const auto*g=w.group(commitSource);const auto*c=w.actor(collector);if(!g||g->respawning||g->contested||w.round-commitStart>=8){committed=0;return ordinary(w);}
  if(!c||!c->visible||!c->alive){committed=0;return ordinary(w);}
  if(!g->observed||!g->complete){Directive d;d.task=Task::inspect;d.group=commitSource;return d;}
  if(w.self.id==collector){source=g->id;return farm(source,"actual named collector enters future receipt region");}
  bool collectorHolding=false;for(auto p:g->spawns)collectorHolding|=p==c->position;
  if(g->cleared&&collectorHolding){committed=0;const auto*other=w.group(commitOther);if(other&&usable(*other)&&other->safe_solo)return farm(other->id,"personally witnessed collector custody releases useful worker");return ordinary(w);}
  return farm(g->id,"visible partner contribution until actual future custody adoption");
 }
 Directive step(const World&w,Arm arm){
  if(previousRound>=0&&w.enemy_score>previousEnemyScore){observedBatch=w.enemy_score-previousEnemyScore;batchGap=w.round-previousRound;}if(w.enemy_score!=previousEnemyScore)previousRound=w.round;previousEnemyScore=w.enemy_score;
  if(w.own_payout&&source<0)source=w.last_payout_group;
  if(w.own_payout&&w.last_payout_group==source)payout=true;if(payout&&w.own_bank_completed)banked=true;if(banked&&w.productive_group>=0)renewed=true;
  if(w.self.channel!=Channel::none&&w.safe_now)return wait("preserve actual winning or productive channel");if(!w.safe_now)return {Task::recover};
  if(committed&&w.round-commitStart>160)committed=0;
  if(w.own_purchase)purchaseReceipt=true;
  if(committed&&w.self.bag>0)return bank("actual committed programme deposit precedes replanning");
  if(committed==1)return purchase_or_return(w);
  if(committed==2)return custody(w);
  std::array<RivalScenario,3> rivals{};
  if(observedBatch>0&&batchGap>0){rivals[1].count=1;rivals[1].period=batchGap;rivals[1].events[0].bag=observedBatch;rivals[1].events[0].completionInterval={batchGap,batchGap};}
  int fastest=w.remaining()+1;Gems visibleBags=0;for(const auto&e:w.enemies)if(e.visible&&e.alive&&e.bag>0){visibleBags+=e.bag;int walk=w.remaining()+1;for(const auto&a:w.altars)if(a.route_known)walk=std::min(walk,std::max(0,distance(e.position,a.position)-1));fastest=std::min(fastest,walk+w.ticks(e.bag));}
  if(visibleBags>0){rivals[2].count=1;rivals[2].events[0].bag=visibleBags;rivals[2].events[0].completionInterval={fastest,fastest};}
  const Group*first=nullptr,*second=nullptr,*liquid=nullptr;
  for(const auto&g:w.groups)if(variant?(usable(g)&&g.safe_solo&&g.to_bank>=0&&g.travel>=0):campaign_available(w.self,g)){
   const int cycle=g.travel+campaign_clear(w.self,g,w.self.strength)+w.capture_turns+1+g.to_bank+w.ticks(g.gems);
   const int old=first?first->travel+campaign_clear(w.self,*first,w.self.strength)+w.capture_turns+1+first->to_bank+w.ticks(first->gems):1;
   if(!first||(variant?cycle<old:g.gems*std::max(old,first->respawn)>first->gems*std::max(cycle,g.respawn))){second=first;first=&g;}else if(!second||cycle_ticks(w,g)<cycle_ticks(w,*second))second=&g;
   if(w.score+w.self.bag+g.gems>=w.win_gems&&(!liquid||cycle<cycle_ticks(w,*liquid)))liquid=&g;
  }
  if(!liquid)liquid=first;
  for(int id=0;id<4;++id){programs[id]={};auto&p=programs[id];p.id=id;p.firstOwner=w.self.id;p.feasible=first!=nullptr||w.self.bag>0;p.regimes=id==1?2:1;p.stopPurchase=id==3;p.concentrateFutureOwner=id==2;
   p.ownCost=id==1?w.shard_cost:0;if(p.ownCost>w.own_gold)p.feasible=false;
   const int bankTravel=[&]{int t=w.remaining()+1;for(const auto&a:w.altars)if(a.route_known&&!a.enemy_lock)t=std::min(t,a.travel);return t;}();
   if(w.self.bag>0){auto&e=p.events[p.count++];e.actor=w.self.id;e.bag=w.self.bag;e.channelStart=bankTravel;e.completionInterval={bankTravel+w.ticks(e.bag),bankTravel+w.ticks(e.bag)};p.peakExposure=e.bag;p.repeatFrom=1;}
   const Group*job=!variant&&id==3?liquid:first;campaignSources[id]=job?job->id:-1;
   if(job){auto&e=p.events[p.count++];e.actor=w.self.id;e.sourceShare=job->id;e.bag=job->gems;e.channelStart=job->travel+(variant?std::max(0,job->clear_turns):campaign_clear(w.self,*job,w.self.strength))+w.capture_turns+1+job->to_bank;
    if(id==1){if(w.self.type!=Champion::tavi&&!w.self.at_spawn)e.channelStart+=std::max(0,w.to_spawn)+w.recall_turns;const double gain=w.self.strength_growth;const int saved=gain>0?campaign_clear(w.self,*job,w.self.strength)-campaign_clear(w.self,*job,w.self.strength+gain):0;e.channelStart-=std::max(0,saved);}
    if(id==2){const auto*collector=w.pick(0);if(!collector||!collector->visible||!collector->alive)p.feasible=false;else{e.actor=collector->id;p.firstOwner=collector->id;p.helper=w.self.id;if(variant)e.bag=(e.bag+1)/2;e.channelStart+=std::max(0,distance(collector->position,job->position)-job->travel);}}
    if(w.self.bag>0)e.channelStart+=p.events[0].completionInterval[1];
    e.completionInterval={e.channelStart+w.ticks(e.bag),e.channelStart+w.ticks(e.bag)};p.peakExposure=std::max(p.peakExposure,e.bag);p.renewal=e.completionInterval[1]+job->to_bank;
    if(id!=3){p.period=std::max(p.renewal,job->respawn);if(p.period<=0)p.period=1;}
   }
  }
  // Visible immediate interruption options are represented separately from
  // unknown future approaches. Damage alone never cancels an intact tribute.
  for(auto&p:programs)for(int j=0;j<p.count;++j){auto&e=p.events[j];const auto*holder=w.actor(e.actor);if(!holder){e.conditional=true;continue;}
   std::array<InterruptOption,4>options{};int optionCount=0;
   for(const auto&enemy:w.enemies){if(optionCount>=4)break;if(!enemy.visible||!enemy.alive)continue;
    bool stun=false;for(const auto&ab:enemy.abilities)stun|=ab.ready&&ab.cc&&distance(enemy.position,holder->position)<=ab.range;
    const bool lethal=enemy.can_attack&&distance(enemy.position,holder->position)<=enemy.range&&enemy.strength>=holder->hp+holder->shield;
    options[optionCount++]={enemy.id,0,0,true,stun,lethal,false,true};
   }
   e.interrupted=interrupts(*holder,e.completionInterval[1],std::span<const InterruptOption>(options.data(),optionCount));e.conditional=true;
  }
  TerminalFacts facts;facts.ownTeamB=w.own_is_team_b;int selected=-1;RaceKey best;
  for(int i=0;i<4;++i){auto k=evaluate(programs[i],w.score,w.enemy_score,w.win_gems,w.remaining(),rivals,facts);
   if(arm==Arm::active){if(selected<0||better(k,best)){selected=i;best=k;}}
   else if(programs[i].feasible&&(selected<0||secured(programs[i],w.score,std::min(120,w.remaining()))>secured(programs[selected],w.score,std::min(120,w.remaining()))))selected=i;
  }lastKey=best;
  if(selected<0||!programs[selected].feasible)return ordinary(w);const auto&p=programs[selected];
  if(w.self.bag>0)return bank(selected==3?"earlier absorbing terminal completion":"race-aware personal conversion");
  if(selected==1&&first&&w.own_gold>=w.shard_cost){committed=1;commitStart=w.round;commitSource=first->id;beforePurchase=w.self.strength;purchaseReceipt=false;return purchase_or_return(w);}
  if(selected==2&&first){committed=2;commitStart=w.round;commitSource=first->id;commitOther=second?second->id:-1;collector=p.firstOwner;return custody(w);}
  if(const auto*job=w.group(campaignSources[selected])){source=job->id;auto d=farm(source,selected==3?"smaller terminal collection beats longer capital campaign":"complete race program reaches bank and renewed work");d.template_id=130+selected;return d;}return ordinary(w);
 }
};
} // namespace p10::race
