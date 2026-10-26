#pragma once
#include "policy.h"
#include "../../common/src/economic-observation.h"
namespace p10_b13 {
using namespace p10;
struct BPolicy {
 p10_b::Economy economy{};std::array<race::CampaignProgram,4>campaigns{};std::array<int,4>route{-1,-1,-1,-1};
 int selected=-1,source=-1,collector=-1,custodyUntil=-1,traces=0,lastEnemyRound=-1,batchGap=0;
 Gems lastEnemyScore=0,lastEnemyBatch=0;bool developing=false,conditionalForecast=true;double strengthBefore=0;
 Directive step(const World&w,Arm arm){
  economy.consume(w);traces=0;auto budget=p10_mobile::budget_for(w);const auto options=p10_b::single_options();
  if(w.enemy_score>lastEnemyScore){if(lastEnemyRound>=0)batchGap=w.round-lastEnemyRound;lastEnemyBatch=w.enemy_score-lastEnemyScore;lastEnemyRound=w.round;}lastEnemyScore=w.enemy_score;
  if(w.self.channel!=Channel::none)return wait("race preserves actual channel after consuming owned receipts");
  if(w.capture_active)return wait("race does not sacrifice actual capture to forecasted terminal margin");
  if(developing){if(w.self.strength>strengthBefore){developing=false;}else if(w.own_gold<w.shard_cost){developing=false;}else if(w.self.at_spawn||w.self.type==Champion::tavi)return buy(Item::monster_strength,1,"real own wallet executes the selected development regime");else if(w.self.bag>0)return bank("owned existing bag closes before funded development detour");else return {Task::recover};}
  if(collector>=0&&w.round<=custodyUntil){const auto*g=w.group(source);const auto*c=w.actor(collector);
   if(g&&c&&c->visible&&c->alive&&usable(*g)){if(g->cleared&&p10_b::on_region(*c,*g)&&w.self.id!=collector){collector=-1;Directive d;d.task=Task::release;d.group=g->id;d.template_id=1323;d.reason="real terminal collector occupies future payout before helper leaves";return d;}
    if(w.self.id==collector)return farm(g->id,"named terminal collector receives future owned payout");
    // Joint encounter work is still current-gear work, never a future role
    // promise. The next fresh physical trace below may clear this source.
   }else collector=-1;
  }
  const Group*steady=nullptr,*shortJob=nullptr;const Gems needed=std::max<Gems>(0,w.win_gems-w.score-w.self.bag);
  auto rough=[&](const Group&g){return 1+g.travel+std::max(1,g.clear_turns)+w.capture_turns+1+g.to_bank+w.ticks(w.self.bag+g.gems);};
  for(const auto&g:w.groups)if(p10_b::work_source(g)){
   if(!steady||g.gems*std::max(steady->respawn,rough(*steady))>steady->gems*std::max(g.respawn,rough(g)))steady=&g;
   if(g.gems>=needed&&(!shortJob||rough(g)<rough(*shortJob)))shortJob=&g;
  }
  if(!shortJob)for(const auto&g:w.groups)if(p10_b::work_source(g)&&(!shortJob||rough(g)<rough(*shortJob)))shortJob=&g;
  if(!steady){if(w.self.bag>0)return bank("liquidate actual owned gems when no production programme is represented");return w.safe_now?Directive{}:Directive{Task::recover};}
  if(const auto*g=w.group(source);g&&usable(*g)&&g->cleared)return farm(g->id,"actual selected campaign completes physical custody");
  if(steady->cleared)return farm(steady->id,"current campaign claims actual cleared source before forecasting renewal");
  if(shortJob&&shortJob->cleared&&shortJob->gems>=needed)return farm(shortJob->id,"actual cleared terminal source takes precedence over hypothetical development");
  auto steadyWork=p10_mobile::mobile_work(w,*steady,budget,options);auto shortWork=steadyWork;
  if(shortJob&&shortJob->id!=steady->id&&budget.spent<2)shortWork=p10_mobile::mobile_work(w,*shortJob,budget,options);traces=budget.spent;
  const int altar=p10_b::altar_trip(w);const Actor*receiver=w.self.type==Champion::marcella?&w.self:nullptr;for(const auto&a:w.allies)if(a.visible&&a.alive&&a.type==Champion::marcella)receiver=&a;
  for(int id=0;id<4;++id){auto&p=campaigns[id];p={};p.id=id;p.regimes=id==1?2:1;p.firstOwner=w.self.id;p.stopPurchase=id==3;p.concentrateFutureOwner=id==2;
   const Group*g=id==3?shortJob:steady;const auto&proof=id==3?shortWork:steadyWork;route[id]=g?g->id:-1;
   const bool loadedLiquidation=id==3&&w.self.bag>0;
   p.feasible=(g&&proof.feasible)||(loadedLiquidation&&altar<=w.remaining());p.ownCost=id==1?w.shard_cost:0;
   if(id==1&&(w.own_gold<w.shard_cost||(w.self.bag>0&&w.self.type!=Champion::tavi)))p.feasible=false;
   int offset=0;if((id==2||id==3)&&w.self.bag>0){auto&e=p.events[p.count++];e.actor=w.self.id;e.bag=w.self.bag;e.channelStart=altar;e.completionInterval={altar+w.ticks(e.bag),altar+w.ticks(e.bag)};offset=e.completionInterval[1];p.repeatFrom=1;}
   if(g&&proof.feasible&&!(loadedLiquidation&&w.score+w.self.bag>=w.win_gems)){
    auto&e=p.events[p.count++];e.actor=w.self.id;e.sourceShare=g->id;e.bag=g->gems+((id==0||id==1)?w.self.bag:0);
    int clear=proof.clear_turns;
    if(id==1){const int after=race::campaign_clear(w.self,*g,w.self.strength+w.self.strength_growth);clear=std::min(clear,after);offset+=w.self.at_spawn||w.self.type==Champion::tavi?0:std::max(0,w.to_spawn)+w.recall_turns+1;e.conditional=true;}
    if(id==2){if(!receiver)p.feasible=false;else{e.actor=receiver->id;p.firstOwner=receiver->id;p.helper=w.self.id;offset+=std::max(0,distance(receiver->position,g->position)-g->travel)+2;e.conditional=receiver->id!=w.self.id;}}
    e.channelStart=offset+(offset?g->to_bank:g->travel)+clear+w.capture_turns+1+g->to_bank;e.completionInterval={e.channelStart+w.ticks(e.bag),e.channelStart+w.ticks(e.bag)};
    p.peakExposure=e.bag;p.renewal=e.completionInterval[1]+g->to_bank;
    if(id!=3){const int recovery=proof.hp<w.self.hp?std::max(0,g->to_spawn)+w.recall_turns+1:0;p.period=std::max(g->respawn,clear+w.capture_turns+1+2*g->to_bank+w.ticks(g->gems)+recovery);
     // The current carried bag belongs only to the first combined batch.
     // Subsequent verified-period arithmetic repeats source receipts alone.
     if((id==0||id==1)&&w.self.bag>0){auto repeated=e;repeated.bag=g->gems;repeated.channelStart+=p.period;repeated.completionInterval[0]+=p.period;repeated.completionInterval[1]+=p.period;p.repeatFrom=p.count;p.events[p.count++]=repeated;}
    }
   }else if(loadedLiquidation)p.renewal=offset;
   for(int j=0;j<p.count;++j){auto&e=p.events[j];const Actor*holder=w.actor(e.actor);if(!holder){e.conditional=true;continue;}
    std::array<race::InterruptOption,4>interrupt{};int n=0;for(const auto&r:w.enemies)if(r.visible&&r.alive&&n<4){bool stun=false;double reach=r.range;for(const auto&ab:r.abilities)if(ab.ready&&ab.cc){stun=true;reach=std::max(reach,ab.range);}
     const int arrival=std::max(0,distance(r.position,holder->position)-int(reach));interrupt[n++]={r.id,arrival,0,true,stun,r.strength>=holder->hp+holder->shield,false,true};}
    e.interrupted=race::interrupts(*holder,e.completionInterval[1],std::span<const race::InterruptOption>(interrupt.data(),n));
   }
  }
  std::array<race::RivalScenario,3>rivals{};
  if(lastEnemyBatch>0&&batchGap>0){rivals[1].count=1;rivals[1].period=batchGap;rivals[1].events[0].bag=lastEnemyBatch;rivals[1].events[0].completionInterval={batchGap,batchGap};}
  for(const auto&e:w.enemies)if(e.visible&&e.alive&&e.bag>0&&rivals[2].count<6){int reach=w.remaining()+1;for(const auto&a:w.altars)if(a.route_known)reach=std::min(reach,std::max(0,distance(e.position,a.position)-1));auto&event=rivals[2].events[rivals[2].count++];event.actor=e.id;event.bag=e.bag;event.completionInterval={reach+w.ticks(e.bag),reach+w.ticks(e.bag)};}
  race::TerminalFacts facts;facts.ownTeamB=w.own_is_team_b;race::RaceKey best;selected=-1;
  for(int id=0;id<4;++id)if(campaigns[id].feasible){auto key=race::evaluate(campaigns[id],w.score,w.enemy_score,w.win_gems,w.remaining(),rivals,facts);
   const bool better=arm==Arm::active?(selected<0||race::better(key,best)):(selected<0||race::secured(campaigns[id],w.score,std::min(120,w.remaining()))>race::secured(campaigns[selected],w.score,std::min(120,w.remaining())));
   if(better){selected=id;best=key;}
  }
  // Repeated production and opponent rates remain conditional, not a claim
  // of forced victory against hidden inventory or future interference.
  conditionalForecast=true;
  if(selected==3&&w.self.bag>0)return bank("finite actual liquidation wins before a longer production campaign");
  if(selected==1){developing=true;strengthBefore=w.self.strength;if(w.self.at_spawn||w.self.type==Champion::tavi)return buy(Item::monster_strength,1,"funded personal development changes later race deposits");return {Task::recover};}
  if(selected==2&&receiver&&receiver->id!=w.self.id){collector=receiver->id;source=steady->id;custodyUntil=w.round+steadyWork.clear_turns+w.capture_turns+3;}
  const bool shortChoice=selected==3&&shortJob;const auto&proof=shortChoice?shortWork:steadyWork;source=shortChoice?shortJob->id:steady->id;
  if(proof.feasible)return p10_b::witnessed(w,proof,1330+std::max(0,selected),shortChoice?"smaller mobile collection closes the actual terminal race":"throughput campaign uses current owned mobile clearance");
  if(w.self.bag>0)return bank("only represented actual bag survives unavailable source forecast");
  return w.safe_now?p10_b::approach(*steady,1334,"reveal chosen campaign route without future gear exposure"):Directive{Task::recover};
 }
};
}
