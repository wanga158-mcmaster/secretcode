// Pure decision/receipt fixtures; these do not qualify engine timing or fuel.
#include "../investment-frontier/src/policy.h"
#include "../triadic-arena/src/policy.h"
#include "../conversion-race/src/policy.h"
#include "../paired-lifeline/src/policy.h"
#include "../realtime-guild/src/policy.h"
#include "../progression-interdiction/src/policy.h"
#include <cassert>
#include <iostream>
using namespace p10;
static Actor actor(int id,Champion type,Point position={1,1}){Actor a;a.id=id;a.pick=id;a.type=type;a.position=position;a.visible=a.alive=a.ally=a.can_attack=a.can_cast=a.can_move=true;a.hp=a.max_hp=1000;a.strength=40;a.focus=100;a.strength_growth=5;a.health_growth=100;a.focus_growth=10;return a;}
static Group group(int id,Point position={2,1}){Group g;g.id=id;g.position=position;g.approach={position.x-1,position.y};g.spawns={position};g.observed=g.complete=g.route_known=g.safe_solo=g.living=true;g.travel=2;g.to_bank=2;g.to_spawn=2;g.hp=120;g.clear_turns=3;g.gems=7;g.gold=40;g.respawn=50;Monster m;m.id=id;m.group=id;m.position=position;m.observed=m.alive=true;m.hp=m.max_hp=120;m.range=3;g.monsters={m};return g;}
static World world(Champion type){World w;w.self=actor(0,type);w.groups={group(0),group(1,{4,1})};w.altars.push_back({0,{1,2},{1,1},true,false,false,true,1,0});w.spawn={0,0};w.to_spawn=2;w.geometry_complete=true;w.opposition_complete=true;return w;}
static void frontier(){using namespace investment;auto a=actor(0,Champion::ryth);a.max_hp=a.hp=1200;a.strength=45;a.focus=100;a.health_growth=100;a.strength_growth=5;a.focus_growth=10;
 const auto triples56=triples();assert(triples56.size()==56);int mixed=0;for(auto c:triples56)if(c.h&&c.s&&c.f)++mixed;assert(mixed>0);
 auto all=catalogue(a,150,30);assert(all.size()==112);
 ThresholdProgram p;p.source=3;p.owner=0;p.helper=1;p.independentSource=4;p.observed=p.helperVisible=p.independentFeasible=true;p.requiredHealth=1300;p.requiredStrength=51;p.requiredFocus=110;p.clear=10;p.bankTravel=5;p.renewal=20;
 auto active=select(all,p,Arm::active),control=select(all,p,Arm::control);assert(active.feasible);assert(active.itemCounts.h==1&&active.itemCounts.s==1&&active.itemCounts.f==1);assert(active.ownCost==90&&!common_bundle(active.itemCounts));assert(!control.feasible);
 auto retained=screen(all,p);bool selectedRetained=false;for(int i=0;i<retained.count;++i)selectedRetained|=retained.retained[i].itemCounts==active.itemCounts;
 // The full 56-triple oracle exposes approximation omissions rather than
 // calling the four representative extrema Pareto-complete.
 if(!selectedRetained)assert(retained.approximation);
 p.requiredHealth=1200;p.requiredStrength=45;p.requiredFocus=110;assert(select(all,p,Arm::active).ownCost==select(all,p,Arm::control).ownCost);
 p.helperVisible=false;assert(!select(all,p,Arm::active).feasible);p.helperVisible=true;p.shopDetour=100;assert(!select(all,p,Arm::active).feasible);
 auto partial=catalogue(a,30,30,{4,0,0},{});bool healthCompletion=false,mixedFalse=false;for(auto x:partial){if(x.family==Family::monster&&x.itemCounts.h==1)healthCompletion|=x.exactPassive;if(x.family==Family::monster&&x.itemCounts.s==1)mixedFalse|=x.exactPassive;}assert(healthCompletion&&!mixedFalse);
}
static void arena_fixture(){using namespace arena;Encounter e;e.own=actor(0,Champion::varg,{2,0});e.own.strength=10;e.rival=actor(1,Champion::prism,{3,0});e.rival.ally=false;e.rival.hp=e.rival.max_hp=50;e.rival.strength=0;e.camp=group(0,{3,0});e.camp.monsters[0].hp=20;e.camp.monsters[0].strength=40;e.camp.monsters[0].range=1;e.camp.monsters[0].awake=true;e.camp.gems=14;e.phaseCount=4;e.terrainKnown=e.turnOrderKnown=e.ownFirst=true;e.hiddenEligibleActor=false;e.rivalExits={Point{7,0},Point{7,3}};e.openExit[0]=e.openExit[1]=true;e.fallbackSource=1;e.fallbackGems=7;e.fallbackBank=20;e.ownBank=25;e.renewal=5;
 for(auto&p:e.phases)p={e.own.position,e.rival.position,true,false,false,false,false,false,false}; for(int t=0;t<4;++t){e.exitPath[0][t]={4+t,0};e.exitPath[1][t]={4+t,std::min(t+1,3)};e.switchPath[t]=e.rival.position;} e.phases[3].own={1,0};e.phases[3].ownMove=true;e.phases[3].ownAttack=false;
 auto chosen=evaluate(e,Program::burst_rival);assert(chosen.branches[0].campDamageToRival==40);assert(deploy(chosen,7,Arm::active)&&!deploy(chosen,7,Arm::control));assert(chosen.branches[2].productive&&chosen.branches[2].secured==7);
 for(auto&p:e.phases)p.campStunned=true;assert(!deploy(evaluate(e,Program::burst_rival),7,Arm::active));
 for(auto&p:e.phases)p.campStunned=false;e.openExit[1]=false;assert(!evaluate(e,Program::burst_rival).admitted);
}
static int arenaFuelReads=0,arenaFuelCut=0;
static int falling_arena_fuel(){return ++arenaFuelReads<=arenaFuelCut?100000:40000;}
static int ample_arena_fuel(){++arenaFuelReads;return 100000;}
static void arena_live_fuel_abort(){
 using namespace arena;Encounter e;e.own=actor(0,Champion::varg,{1,1});e.rival=actor(1,Champion::prism,{3,1});e.camp=group(0);e.phaseCount=4;e.terrainKnown=e.turnOrderKnown=true;e.fuel_reader=falling_arena_fuel;e.fuel_floor=40500;
 for(auto&p:e.phases)p={e.own.position,e.rival.position,true,false,false,true,false,false,false};
 arenaFuelReads=0;arenaFuelCut=5;auto branch=simulate(e,Program::finish_camp,RivalBranch::continue_camp);assert(branch.fuel_aborted&&!branch.survive&&arenaFuelReads==6);
 arenaFuelReads=0;arenaFuelCut=8;auto program=evaluate(e,Program::finish_camp);assert(program.fuel_aborted&&!program.admitted);
 auto w=world(Champion::varg);w.self.position={2,0};w.self.strength=10;auto r=actor(9,Champion::prism,{3,0});r.ally=false;r.hp=r.max_hp=50;r.strength=0;w.enemies={r};
 auto&g=w.groups[0];g.position={3,0};g.spawns={{3,0}};g.approach={2,0};g.monsters[0].position={3,0};g.monsters[0].hp=g.hp=20;g.monsters[0].strength=g.incoming=40;g.monsters[0].range=1;g.monsters[0].awake=true;g.gems=14;g.safe_solo=false;w.visible_open_tiles={{1,0},{4,0},{4,1}};
 bool abortedInside=false;
 for(int cut:{40,80,120}){Policy p;p.set_fuel_reader(falling_arena_fuel);arenaFuelReads=0;arenaFuelCut=cut;auto d=p.step(w,Arm::active);assert(p.fresh_programs<=2);
  if(p.aborted_programs){abortedInside=true;assert(d.task==Task::farm&&d.group==1&&d.template_id==0);
   // A later owner turn starts a completely fresh comparison; no unfinished
   // HP arrays or partially ranked branch can resume with an old observation.
   w.round=1;p.set_fuel_reader(ample_arena_fuel);arenaFuelReads=0;d=p.step(w,Arm::active);assert(!p.aborted_programs&&p.fresh_programs<=2&&d.template_id==121);w.round=0;break;
  }
 }
 assert(abortedInside);
 Policy low;low.set_fuel_reader(falling_arena_fuel);arenaFuelReads=0;arenaFuelCut=0;auto d=low.step(w,Arm::active);assert(low.fresh_programs==0&&d.task==Task::farm&&d.group==1);
}
static void race_fixture(){using namespace race;CampaignProgram developed,liquidate;developed.id=1;developed.feasible=true;developed.count=1;developed.period=20;developed.events[0].bag=60;developed.events[0].completionInterval={20,20};developed.renewal=25;
 liquidate=developed;liquidate.id=3;liquidate.period=0;liquidate.events[0].bag=10;liquidate.events[0].completionInterval={8,8};liquidate.renewal=12;liquidate.stopPurchase=true;liquidate.firstOwner=0;liquidate.helper=1;liquidate.needsSupport=true;
 std::array<RivalScenario,3> rivals{};for(auto&s:rivals){s.count=1;s.events[0].bag=10;s.events[0].completionInterval={9,9};}
 TerminalFacts facts;auto early=evaluate(liquidate,990,990,1000,120,rivals,facts),rich=evaluate(developed,990,990,1000,120,rivals,facts);assert(better(early,rich));assert(secured(developed,990,120)>secured(liquidate,990,120));assert(finish(liquidate,990,1000,120)==8);
 auto holder=actor(0,Champion::tavi);InterruptOption option{1,7,0,true,true,false,false,true};assert(interrupts(holder,8,std::span<const InterruptOption>(&option,1)));holder.type=Champion::marcella;assert(!interrupts(holder,8,std::span<const InterruptOption>(&option,1)));option.lock=true;assert(interrupts(holder,8,std::span<const InterruptOption>(&option,1)));option.arrival=8;assert(!interrupts(holder,8,std::span<const InterruptOption>(&option,1)));
 facts.goldKnown=facts.killsKnown=true;facts.ownGold=11;facts.enemyGold=10;assert(terminal_tie(20,20,facts)==1);facts.enemyGold=11;assert(terminal_tie(20,20,facts)==-1);facts.ownTeamB=true;assert(terminal_tie(20,20,facts)==1);facts.goldKnown=false;assert(terminal_tie(20,20,facts)==0);
 CampaignProgram noDuplicate=liquidate;noDuplicate.count=2;noDuplicate.repeatFrom=1;noDuplicate.period=20;noDuplicate.events[0].bag=7;noDuplicate.events[0].completionInterval={1,1};noDuplicate.events[1].bag=3;noDuplicate.events[1].completionInterval={10,10};assert(secured(noDuplicate,0,50)==16);assert(finish(noDuplicate,0,16,50)==50);
}
static void pair_fixture(){using namespace pair;PairService s;s.cassi.actor=actor(0,Champion::cassi);s.cassi.actor.hp=s.cassi.actor.max_hp=600;s.cassi.actor.focus=70;s.cassi.actor.focus_growth=13;s.issac.actor=actor(1,Champion::issac);s.issac.actor.hp=s.issac.actor.max_hp=900;s.issac.actor.health_growth=80;s.cassi.actualGold=s.issac.actualGold=0;s.source=0;s.nextSource=1;s.receiptGold=150;s.receiptGems=15;s.contact=s.exitsSafe=s.dependencyKnown=s.remainingBodySafe=true;s.exactCombatTurns=16;s.issacIncoming=950;s.cassiIncoming=50;s.returnTravel=5;s.travel=2;s.cassiBankTravel=2;s.issacBankTravel=3;
 auto active=select(s,Arm::active),control=select(s,Arm::control);assert(active.feasible&&active.recipient==Recipient::issac&&!control.feasible);assert(active.issacCost==150&&active.cassiCost==0&&active.issacMaxHp==1300);
 s.issacIncoming=300;s.requiredBurst=270;active=select(s,Arm::active);assert(active.feasible&&active.recipient==Recipient::cassi&&active.cassiFocus==135);
 s.requiredBurst=0;s.parallelSafe=false;s.cassi.actor.bag=2;s.issac.actor.bag=40;active=select(s,Arm::active);assert(active.feasible&&active.order!=ServiceOrder::parallel);
 s.contact=false;assert(!select(s,Arm::active).feasible);s.contact=true;s.receiptGold=0;s.cassi.actualGold=80;s.issac.actualGold=80;assert(!select(s,Arm::active).feasible);assert(joint_death(500,0)&&joint_death(0,500)&&!joint_death(1,1));
}
static void guild_fixture(){using namespace guild;std::array<Skill,8>menu{};for(int i=0;i<8;++i){auto&s=menu[i];s.id=i;s.source=0;s.nextSource=1;s.fuel={2,2,2,2,2,5,false};s.guard={0,0,-1,1,100,0,true,true,true};s.firstBank=20;s.renewal=30;s.firstScore=7;s.productive=i==7;}
 Fresh f{0,0,1,0,1000,true,true,true,false};auto active=select(menu,8,f,70,30,Arm::active),control=select(menu,8,f,70,30,Arm::control);assert(active.selected==7&&control.selected==-1&&!active.measured);assert(select(menu,8,f,120,30,Arm::control).selected==7);
 f.hp=99;assert(select(menu,8,f,70,30,Arm::active).selected<0);f.hp=1000;f.moveFailed=true;assert(!valid(menu[7],f));f.moveFailed=false;f.cooldown=1;assert(!valid(menu[7],f));f.cooldown=0;f.channelIntact=false;assert(!valid(menu[7],f));f.channelIntact=true;f.partnerPresent=false;assert(!valid(menu[7],f));
}
static void interdiction_fixture(){using namespace interdiction;ReentryCampaign c;c.observedCapability={4,Champion::prism,40,100,true,false,true};c.favorableIntercept=true;c.interceptionTurns=8;c.producer=2;c.homeCompletion=30;c.homeBank=30;c.lostHomeDeposits=5;c.unknownWallet=true;c.sourceCount=1;c.replacementSources[0]={1,2,10,20,30,50,80,60,20,true,true,true,true};c.alternativeCount=1;c.alternatives[0]={SubstituteKind::other_source,2,4,120,true,false};
 auto a=select(c,Arm::active),b=select(c,Arm::control);assert(a.intercept&&b.intercept&&a.acquire&&!b.acquire&&a.source==1&&a.producer==2&&a.unresolved&&a.adverseReentry==0);
 c.alternatives[0].completion=25;assert(!select(c,Arm::active).acquire);c.alternatives[0].completion=120;c.replacementSources[0].respawn=40;assert(!select(c,Arm::active).acquire);c.replacementSources[0].respawn=80;c.replacementSources[0].observed=false;assert(!select(c,Arm::active).acquire);
 AttemptBudget budget;assert(budget.begin(1,4));assert(!budget.current(9,true,true));assert(budget.begin(10,4));assert(!budget.current(11,false,true));assert(!budget.begin(12,4)&&budget.used==2);
 Witness receipt;receipt.source=1;receipt.producer=2;receipt.stage=Stage::replacement;World w;w.own_payout=true;w.last_payout_group=1;receipt.observe(w);assert(receipt.payout&&receipt.stage==Stage::bank);w.own_payout=false;w.own_bank_completed=true;receipt.observe(w);assert(receipt.bank&&receipt.stage==Stage::renewal);w.own_bank_completed=false;w.productive_group=3;receipt.observe(w);assert(receipt.renewed&&receipt.stage==Stage::complete&&receipt.observedEnemyCompletion==-1);
}
template<class P>static void cold_cycle(Champion type){P policy;World w=world(type);if(type==Champion::cassi){auto other=actor(1,Champion::issac);w.allies.push_back(other);}auto first=policy.step(w,Arm::active);assert(first.task==Task::farm||first.task==Task::inspect||first.task==Task::independent||first.task==Task::attack||first.task==Task::cast);
 w.round=10;w.own_payout=true;w.last_payout_group=0;w.self.bag=7;w.own_gold=40;w.receipts={{Receipt::Kind::payout,10,0,0,0,7,40,0}};assert(policy.step(w,Arm::active).task==Task::bank);
 w.round=11;w.own_payout=false;w.self.channel=Channel::tribute;assert(policy.step(w,Arm::active).task==Task::wait);
 w.round=13;w.self.channel=Channel::none;w.own_bank_completed=true;w.last_bank_round=13;w.score=7;w.self.bag=0;w.receipts={{Receipt::Kind::bank,13,0,0,0,7,0,0}};policy.step(w,Arm::active);assert(policy.payout&&policy.banked);
 // An actual owner receipt changes inventory/wallet; the fixture never pools
 // partner wallets or promotes a forecasted purchase to an observation.
 w.round=14;w.own_bank_completed=false;w.own_purchase=true;w.self.at_spawn=true;w.own_gold=10;w.self.inventory[int(Item::monster_strength)]=1;w.self.strength+=w.self.strength_growth;w.receipts={{Receipt::Kind::purchase,14,-1,0,int(Item::monster_strength),0,30,w.self.strength_growth}};policy.step(w,Arm::active);
 w.round=15;w.own_purchase=false;w.self.at_spawn=false;w.productive_group=1;w.groups[0].respawning=true;w.groups[0].living=false;auto next=policy.step(w,Arm::active);assert(policy.renewed);assert(next.task==Task::farm||next.task==Task::independent||next.task==Task::wait);
}
static int investmentFuelReads=0;
static int falling_investment_fuel(){return ++investmentFuelReads==1?100000:40000;}
static void paired_current_funding_order(){
 Actor c=actor(0,Champion::cassi,{1,1}),i=actor(1,Champion::issac,{2,1});c.hp=c.max_hp=600;c.strength=30;c.range=3;c.focus=70;c.focus_growth=13;i.hp=i.max_hp=900;i.strength=40;i.health_growth=80;
 auto elk=group(0,{3,1});elk.gold=150;elk.hp=elk.monsters[0].hp=elk.monsters[0].max_hp=2400;elk.monsters[0].strength=60;elk.monsters[0].range=4;elk.incoming=60;elk.clear_turns=16;elk.safe_solo=false;
 assert(!pair::Policy::current_fight_safe(c,i,elk));
 for(int futureGold:{0,150,1500}){auto w=world(Champion::issac);w.self=i;w.allies={c};w.groups={elk};w.groups[0].gold=futureGold;w.own_gold=0;pair::Policy p;
  auto d=p.step(w,Arm::active);assert(d.task!=Task::farm&&d.task!=Task::attack&&d.task!=Task::cast&&d.task!=Task::buy);assert(w.self.max_hp==900&&p.fundedRecipe==Item::jungle_health);
 }
 // A real cleared-source receipt funds the shop stage, which survives loss
 // of source/partner vision. Every purchase still uses actual own money.
 auto w=world(Champion::issac);w.self=i;w.allies={c};auto cleared=elk;cleared.cleared=true;cleared.living=false;cleared.hp=0;cleared.monsters[0].alive=false;cleared.monsters[0].hp=0;w.groups={cleared};pair::Policy p;
 assert(p.step(w,Arm::active).task==Task::farm&&p.fundedRecipe==Item::jungle_health);
 w.round=1;w.own_payout=true;w.last_payout_group=0;w.self.bag=15;w.own_gold=150;assert(p.step(w,Arm::active).task==Task::bank);
 w.round=2;w.own_payout=false;w.own_bank_completed=true;w.self.bag=0;w.score=15;w.groups[0].observed=w.groups[0].complete=false;w.allies.clear();assert(p.step(w,Arm::active).task==Task::recover);
 w.self.at_spawn=true;w.own_bank_completed=false;for(int n=0;n<5;++n){w.round=3+n;auto d=p.step(w,Arm::active);assert(d.task==Task::buy&&d.item==Item::jungle_health&&d.buy_count==1);w.own_gold-=30;++w.self.inventory[int(Item::jungle_health)];w.self.hp+=80;w.self.max_hp+=80;w.own_purchase=true;}
 w.round=8;assert(p.step(w,Arm::active).task!=Task::buy&&w.own_gold==0&&w.self.max_hp==1300&&p.fundedRecipe==Item::none);
 // Safe zero-damage income remains executable while an unfunded attacking
 // programme is refused; its real receipt may develop the later circuit.
 auto gold=group(1,{4,1});gold.gems=0;w=world(Champion::issac);w.self=i;w.allies={c};w.groups={elk,gold};pair::Policy firstIncome;auto ordinary=firstIncome.step(w,Arm::active);assert(ordinary.task==Task::farm&&ordinary.group==1&&!ordinary.strict_tank);
}
static void paired_proposal_agreement(){
 Actor c=actor(0,Champion::cassi,{2,1}),i=actor(1,Champion::issac,{3,1});
 c.hp=c.max_hp=600;c.focus=70;c.focus_growth=13;i.hp=i.max_hp=900;i.health_growth=80;
 auto target=group(0,{3,1});target.cleared=true;target.living=false;target.hp=0;target.gold=40;target.clear_turns=0;
 target.monsters[0].alive=false;target.monsters[0].hp=0;target.monsters[0].max_hp=2400;target.monsters[0].strength=60;target.monsters[0].range=4;
 auto other=group(1,{8,1});
 for(int cg:{0,150,900})for(int ig:{0,150,900}){
  World cw=world(Champion::cassi),iw=world(Champion::issac);cw.self=c;cw.allies={i};cw.groups={target,other};cw.own_gold=cg;cw.visible_open_tiles={{1,1},{2,2}};
  iw.self=i;iw.allies={c};iw.groups={target,other};iw.own_gold=ig;iw.visible_open_tiles=cw.visible_open_tiles;
  // Actor-private route/clear estimates are not the common ownership rule.
  iw.groups[0].travel=9;iw.groups[0].clear_turns=16;cw.groups[1].travel=1;
  pair::Policy cp,ip;auto cd=cp.step(cw,Arm::active);auto id=ip.step(iw,Arm::active);
  assert(cp.source==0&&ip.source==0&&cp.proposal==pair::Recipient::issac&&ip.proposal==pair::Recipient::issac);
  assert(cd.task==Task::release&&((id.task==Task::farm&&id.group==0)||id.task==Task::recover));
  pair::Policy cc,ic;cc.step(cw,Arm::control);ic.step(iw,Arm::control);assert(cc.proposal==pair::Recipient::cassi&&ic.proposal==pair::Recipient::cassi);
 }
 // Only the proposed owner spends an actual personal wallet. The other body
 // observes gear; it does not infer funds or substitute its own money.
 World cw=world(Champion::cassi),iw=world(Champion::issac);i.position={0,0};i.at_spawn=true;cw.self=c;cw.allies={i};cw.groups={target,other};cw.own_gold=900;iw.self=i;iw.allies={c};iw.groups=cw.groups;iw.own_gold=150;
 pair::Policy cp,ip;assert(ip.step(iw,Arm::active).task==Task::buy&&ip.proposal==pair::Recipient::issac);assert(cp.step(cw,Arm::active).task!=Task::buy&&cp.proposal==pair::Recipient::issac);
 iw.own_gold=0;pair::Policy unfunded;assert(unfunded.step(iw,Arm::active).task!=Task::buy&&unfunded.proposal==pair::Recipient::issac);
 // The next regime changes only after the recipient's actual recipe is
 // visible, rather than a prediction that the unseen wallet could buy it.
 i.inventory[int(Item::jungle_health)]=5;i.max_hp=i.hp=1300;i.jungle_emblem=true;cw.allies={i};iw.self=i;
 pair::Policy gearedC,gearedI;gearedC.step(cw,Arm::active);gearedI.step(iw,Arm::active);assert(gearedC.proposal==pair::Recipient::cassi&&gearedI.proposal==pair::Recipient::cassi);
 // A proposal cannot remove the current sole holder before the other twin
 // actually occupies a distinct legal reward tile.
 c.position=target.spawns[0];i.position={2,1};i.at_spawn=false;i.inventory[int(Item::jungle_health)]=0;i.max_hp=i.hp=900;i.jungle_emblem=false;cw.self=c;cw.allies={i};pair::Policy sole;assert(sole.step(cw,Arm::active).task==Task::wait&&sole.proposal==pair::Recipient::issac);
}
static void investment_trace_budget(){
 World w=world(Champion::ren);w.groups={group(0,{2,1}),group(1,{2,2}),group(2,{3,1})};
 for(int i=0;i<2;++i){w.groups[i].safe_solo=false;w.groups[i].hp=w.groups[i].monsters[0].hp=200;w.groups[i].monsters[0].strength=1000;}
 auto investor=actor(4,Champion::ryth);investor.strength_shards=1;w.allies={investor};
 investment::Policy p;auto fallback=p.step(w,Arm::active);assert(p.fresh_trajectories==2&&fallback.task==Task::farm&&fallback.group==2);
 // The live adapter can refresh actual remaining fuel between trajectories.
 // A reserve reached after the first forecast refuses the second rather than
 // granting a decision from the cached entry sample.
 p.set_fuel_reader(falling_investment_fuel);investmentFuelReads=0;fallback=p.step(w,Arm::active);assert(investmentFuelReads==2&&p.fresh_trajectories==1&&fallback.group==2);
 w.fuel=40000;investment::Policy low;fallback=low.step(w,Arm::active);assert(low.fresh_trajectories==0&&fallback.group==2);
 // A purchased/committed source uses the same counter and fresh reserve guard.
 w=world(Champion::ryth);investment::Policy committed;committed.committed.feasible=true;committed.committed.changedProgram=0;committed.purchaseStart=w.round;
 assert(committed.step(w,Arm::active).task==Task::farm&&committed.fresh_trajectories==1);
 w.fuel=40000;assert(committed.step(w,Arm::active).task==Task::farm&&committed.fresh_trajectories==0&&committed.committed.feasible);
}
static void executed_stages(){
 {investment::Policy p;World w=world(Champion::ryth);w.own_gold=30;w.groups[0].observed=w.groups[0].complete=false;p.committed.feasible=true;p.committed.changedProgram=0;p.committed.itemCounts.h=1;p.committed.family=investment::Family::jungle;p.purchaseStart=0;
  assert(p.step(w,Arm::active).task==Task::recover&&p.committed.feasible);w.round=1;w.self.at_spawn=true;auto buyOne=p.step(w,Arm::active);assert(buyOne.task==Task::buy&&buyOne.item==Item::jungle_health);w.round=2;w.own_gold=0;w.self.inventory[int(Item::jungle_health)]=1;assert(p.step(w,Arm::active).task==Task::inspect&&p.committed.feasible);}
 {race::Policy p;World w=world(Champion::marcella);w.own_gold=30;w.groups[0].hp=w.groups[0].monsters[0].hp=1000;w.groups[0].clear_turns=25;w.groups[1].hp=w.groups[1].monsters[0].hp=1200;w.groups[1].clear_turns=30;w.self.strength_growth=40;
  assert(p.step(w,Arm::active).task==Task::recover&&p.committed==1);w.round=1;w.self.at_spawn=true;assert(p.step(w,Arm::active).task==Task::buy);w.round=2;w.own_purchase=true;w.self.strength+=40;w.own_gold=0;w.groups[0].observed=w.groups[0].complete=false;assert(p.step(w,Arm::active).task==Task::inspect);}
 {race::Policy p;World w=world(Champion::tavi);w.self.id=1;w.self.pick=1;w.score=990;auto c=actor(2,Champion::marcella,{3,2});c.pick=0;w.allies={c};auto e=actor(3,Champion::ren,{1,2});e.ally=false;e.abilities[0].ready=e.abilities[0].cc=true;e.abilities[0].range=4;w.enemies={e};
  auto join=p.step(w,Arm::active);assert(p.committed==2&&join.task==Task::farm&&join.group==0);w.round=1;w.groups[0].cleared=true;w.groups[0].living=false;w.allies[0].position=w.groups[0].spawns[0];auto leave=p.step(w,Arm::active);assert(leave.task==Task::farm&&leave.group==1);}
 {pair::Policy p;World w=world(Champion::issac);w.self.bag=40;auto c=actor(1,Champion::cassi);c.hp=100;c.max_hp=600;c.bag=2;w.allies={c};assert(p.step(w,Arm::active).task==Task::wait&&p.service==pair::ServiceOrder::cassi_first);w.round=1;w.allies[0].bag=0;assert(p.step(w,Arm::active).task==Task::bank);}
 {guild::Policy p;World w=world(Champion::varg);w.self.abilities[0].ready=w.self.abilities[0].reset=true;guild::Skill s;s.source=0;s.nextSource=1;s.kind=guild::SkillKind::small_source;auto basic=p.execute_skill(w,s);s.kind=guild::SkillKind::splash_reset;auto splash=p.execute_skill(w,s);assert(basic.reserve_reset&&!splash.reserve_reset&&splash.monster==0);auto trace=guild::online_predict(s,w.self,w.groups[0]);assert(trace.operations>0&&trace.feasible&&trace.phases==2);s.kind=guild::SkillKind::burst_hold;w.self.abilities[0].damage=true;assert(p.execute_skill(w,s).cast_slot==0);s.kind=guild::SkillKind::recovery;assert(p.execute_skill(w,s).task==Task::recover);s.kind=guild::SkillKind::loaded_conversion;assert(p.execute_skill(w,s).task==Task::bank);}
}
static void structural_callers(){
 {auto w=world(Champion::ryth);w.self.hp=w.self.max_hp=500;w.self.strength=40;w.self.strength_growth=10;w.own_gold=150;w.allies={actor(1,Champion::ren,{2,2})};
  auto&g=w.groups[0];g.hp=g.monsters[0].hp=400;g.monsters[0].strength=g.incoming=80;g.clear_turns=10;g.safe_solo=false;
  investment::Policy p;auto d=p.step(w,Arm::active);assert(p.committed.feasible&&p.fresh_trajectories==2);assert(!investment::common_bundle(p.committed.itemCounts));assert(d.task==Task::recover||d.task==Task::buy);
 }
 {auto w=world(Champion::varg);w.self.position={2,0};w.self.strength=10;auto r=actor(9,Champion::prism,{3,0});r.ally=false;r.hp=r.max_hp=50;r.strength=0;w.enemies={r};
  auto&g=w.groups[0];g.position={3,0};g.spawns={{3,0}};g.approach={2,0};g.monsters[0].position={3,0};g.monsters[0].hp=g.hp=20;g.monsters[0].strength=g.incoming=40;g.monsters[0].range=1;g.monsters[0].awake=true;g.gems=14;g.safe_solo=false;
  w.visible_open_tiles={{1,0},{4,0},{4,1}};arena::Policy p;auto d=p.step(w,Arm::active);assert(d.task==Task::attack&&d.actor==r.id&&d.template_id==121);
 }
 {auto w=world(Champion::marcella);w.score=990;auto&a=w.groups[0];a.hp=a.monsters[0].hp=400;a.clear_turns=10;a.gems=60;a.travel=a.to_bank=5;a.respawn=20;
  auto&b=w.groups[1];b.hp=b.monsters[0].hp=40;b.clear_turns=1;b.gems=10;b.travel=0;b.to_bank=1;b.respawn=100;race::Policy p;auto d=p.step(w,Arm::active);assert(p.campaignSources[0]==0&&p.campaignSources[3]==1);assert(d.group==1&&d.template_id==133);
 }
 {auto w=world(Champion::cassi);w.self.hp=w.self.max_hp=600;w.self.range=3;w.self.strength=20;w.self.abilities[0].ready=w.self.abilities[0].damage=true;w.self.abilities[0].modifier=2;w.self.abilities[0].range=4;
  auto i=actor(1,Champion::issac,{2,1});i.hp=300;i.max_hp=900;w.allies={i};auto&g=w.groups[0];g.position={3,1};g.spawns={{3,1}};g.monsters[0].position={3,1};g.hp=g.monsters[0].hp=220;g.monsters[0].strength=g.incoming=100;g.monsters[0].range=4;g.safe_solo=false;w.safe_now=false;
  assert(!pair::Policy::current_fight_safe(w.self,i,g));pair::Policy p;auto d=p.step(w,Arm::active);assert(d.task==Task::cast&&d.cast_slot==0&&d.safety_verified);
  pair::Policy service;service.serviceCommitted=true;service.service=pair::ServiceOrder::issac_first;w=world(Champion::issac);w.self.bag=40;auto c=actor(1,Champion::cassi);c.hp=100;c.max_hp=600;c.bag=2;w.allies={c};assert(service.step(w,Arm::active).task==Task::bank);
 }
 {auto w=world(Champion::varg);w.self.abilities[0].ready=w.self.abilities[0].reset=true;w.groups[0].safe_solo=false;auto second=w.groups[0].monsters[0];second.id=7;second.position={2,2};w.groups[0].monsters.push_back(second);w.groups[0].hp=240;
  guild::Policy p;auto d=p.step(w,Arm::active);assert(d.group==0&&d.template_id==2&&p.onlineEvaluations<=2);w.round=1;w.groups[0].respawning=true;d=p.step(w,Arm::active);assert(d.group==1&&p.circuits[0]==1&&p.onlineEvaluations<=2);
 }
 {auto w=world(Champion::marcella);w.groups[0].travel=1;w.groups[0].respawn=40;w.groups[1].gems=14;w.groups[1].clear_turns=1;w.groups[1].travel=4;w.groups[1].respawn=100;w.groups[1].safe_solo=false;w.groups[1].position={5,1};
  auto r=actor(8,Champion::prism,{6,1});r.ally=false;r.hp=100;r.bag=40;w.enemies={r};w.allies={actor(1,Champion::ren,{5,1})};interdiction::Policy p;auto d=p.step(w,Arm::active);assert(d.group==1&&d.template_id==160);w.round=1;w.enemies.clear();assert(p.step(w,Arm::active).group==1);
 }
}
int main(){frontier();arena_fixture();arena_live_fuel_abort();race_fixture();pair_fixture();guild_fixture();interdiction_fixture();paired_current_funding_order();paired_proposal_agreement();investment_trace_budget();executed_stages();structural_callers();cold_cycle<investment::Policy>(Champion::marcella);cold_cycle<arena::Policy>(Champion::ren);cold_cycle<race::Policy>(Champion::marcella);cold_cycle<pair::Policy>(Champion::cassi);cold_cycle<guild::Policy>(Champion::varg);cold_cycle<interdiction::Policy>(Champion::marcella);std::cout<<"Plan10 cards 11-16 pure fixtures passed; native calibration pending\n";}
