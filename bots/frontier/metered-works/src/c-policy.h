#pragma once
#include "../../common/src/c-capital-01-05.h"
#include "model.h"
namespace p10_c_metered {
enum class Kind { two_sources,two_generations };
struct Candidate {bool valid=false;Kind kind=Kind::two_sources;metered_works::Program party=metered_works::Program::sole;std::array<int,2>sources{-1,-1};metered_works::ServiceProgram service{};p10::Gems secured=0;int participant=-1;};
struct CPolicy:p10_c::Physical {
 static constexpr bool observe_ally_details=false;
 static constexpr std::uint64_t observe_ally_detail_types=0;
 p10_c::Wallet wallet;int capital_target=-1,capital_packet=0,capital_events=0;double strength_goal=0;int goal_source=-1,goal_deadline=-1;
 Candidate selected{};int index=0,payouts=0,start=-1,last_receipt=-2;bool chosen=false;int actual_parallel=0;
 bool first_income=false;int first_source=-1;
 struct FirstRefusal {int source=-1,until=-1;};std::array<FirstRefusal,2>first_refusals{};int first_refusal_cursor=0;
 struct FirstPrice {bool valid=false;int tier=1,finish=0;double value=0;};
 mutable const p10::World*work_world=nullptr;mutable bool work_aborted=false;mutable int work_checks=0,work_phase=0;
 bool fuel_ok()const{++work_checks;if(work_aborted)return false;const auto r=reader?reader:p10::work_fuel_reader;const int fuel=r?r():(work_world?work_world->fuel:100000);if(work_world&&fuel<work_world->action_reserve+1500)work_aborted=true;return !work_aborted;}
 p10::Directive fuel_stop()const{return p10::wait("joint batch defers unfinished live work before its action reserve");}
 // This physical input owns only the source-local graph, not ingress.
 // A remote source becoming visible does not attach the actor to that graph.
 bool local_contact(const p10::World&w,const p10::Group&g)const{
  if(g.cleared)return true;
  bool living=false,positioned=w.self.can_attack;
  for(const auto&m:g.monsters){if(!fuel_ok())return false;if(m.alive&&m.hp>0){living=true;positioned&=p10::distance(w.self.position,m.position)<=w.self.range;}}
  if(living&&positioned)return true;
  for(const auto&points:{&g.spawns,&g.approach_pads})for(auto p:*points){if(!fuel_ok())return false;if(p10_mobile::manhattan(w.self.position,p)<=1)return true;}
  return false;
 }
 int appointment(const p10::World&w,const p10::Actor&a,const p10::Group&g)const{
  // Two literal cardinal L-routes through the current immutable/free view.
  // Source spawns may open after this actor's own proven clearance; no other
  // champion's occupied square or unknown terrain is silently vacated.
  int best=1000;for(auto target:g.spawns)for(int xy=0;xy<2;++xy){if(!fuel_ok())return 1000;auto p=a.position;int steps=0;bool legal=true;
   for(int axis=0;axis<2&&legal;++axis)while((axis==xy?p.x:p.y)!=(axis==xy?target.x:target.y)){
    if(++steps>16||!fuel_ok()){legal=false;break;}if(axis==xy)p.x+=p.x<target.x?1:-1;else p.y+=p.y<target.y?1:-1;
    bool terrain=std::find(w.visible_open_tiles.begin(),w.visible_open_tiles.end(),p)!=w.visible_open_tiles.end()||std::find(g.spawns.begin(),g.spawns.end(),p)!=g.spawns.end()||std::find(g.approach_pads.begin(),g.approach_pads.end(),p)!=g.approach_pads.end();
    for(const auto&v:w.groups)if(v.id!=g.id)for(const auto&m:v.monsters){if(!fuel_ok())return 1000;if(m.alive&&m.hp>0&&m.position==p)terrain=false;}
    for(const auto&v:w.allies){if(!fuel_ok())return 1000;if(v.id!=a.id&&v.visible&&v.alive&&v.position==p)terrain=false;}if(w.self.id!=a.id&&w.self.position==p)terrain=false;
    if(!terrain||p10_mobile::visible_enemy_can_intervene(w,p))legal=false;
   }
   if(legal)best=std::min(best,steps);
  }
  return best;
 }
 Candidate candidate(const p10::World&w,const p10::Group&a,const p10::Group&b,int clear_a,int clear_b,const p10::Actor*other,Kind kind,p10::Arm arm,bool capital_price=false)const{
  Candidate c;c.kind=kind;c.sources={a.id,b.id};if(!fuel_ok()||a.to_bank<0||b.to_bank<0||(a.id!=b.id&&(a.to_spawn<0||b.to_spawn<0)))return c;
  if(!capital_price&&(service_capacity(w,a,w.self.strength)<0||service_capacity(w,b,w.self.strength)<0||(kind==Kind::two_generations&&service_capacity(w,a,w.self.strength,true)<0)))return c;
  const int arrival=other?appointment(w,*other,a):0;if(arrival>=1000)other=nullptr;
  if(other){double exposure=0;bool known=true;for(const auto&g:w.groups)for(const auto&m:g.monsters){if(!fuel_ok())return c;if(m.alive&&m.strength>0&&p10::distance(other->position,m.position)<=m.range+arrival){known&=g.observed&&g.complete&&m.observed;exposure+=m.strength;}}if(!known||other->hp<=exposure*(arrival+w.capture_turns+3)+30)other=nullptr;}
  c.participant=other?other->id:-1;metered_works::Batch batch;batch.settings={w.tribute_multiplier,w.tribute_base,w.tribute_minimum};batch.remaining=std::min(192,w.remaining());batch.workers_count=other?2:1;batch.prospective=other&&a.spawns.size()>1&&b.spawns.size()>1;batch.party_verified=other&&p10_c::on_spawn(w.self,a)&&p10_c::on_spawn(*other,a);batch.source_travel=a.id==b.id?0:std::max(0,a.to_spawn)+std::max(0,b.to_spawn);
  auto worker=[&](const p10::Actor&v){metered_works::Worker r;r.id=v.id;r.arrival=v.id==w.self.id?0:arrival;r.altar_travel=std::max(a.to_bank,b.to_bank);r.return_travel=r.altar_travel;r.bag=v.bag;r.marcella=v.type==p10::Champion::marcella;r.healer=v.type==p10::Champion::willow;return r;};batch.workers[0]=worker(w.self);if(other)batch.workers[1]=worker(*other);
  for(int i=0;i<2;++i){const auto&g=i?b:a;auto&s=batch.sources[i];s.id=g.id;s.observed=g.observed&&g.complete;s.contested=g.contested;s.clear=i?clear_b:clear_a;s.capture=w.capture_turns;s.gems=g.gems;s.gold=g.gold;}
  if(kind==Kind::two_generations){int work=0;for(const auto&m:a.monsters){if(!fuel_ok()||m.strength>0)return c;work+=int(std::ceil(std::max(m.hp,m.max_hp)/std::max(1.,w.self.strength)));}batch.sources[1].clear=clear_a+w.capture_turns+1+a.respawn+work;}
  work_phase=4;if(!fuel_ok())return c;c.service=metered_works::choose(batch,arm==p10::Arm::active);if(!fuel_ok())return Candidate{};c.party=c.service.kind;c.valid=c.service.complete;for(auto gems:c.service.banked)c.secured+=gems;return c;
 }
 static bool better(const Candidate&a,const Candidate&b){return a.valid&&(!b.valid||a.secured>b.secured||(a.secured==b.secured&&a.service.finish<b.service.finish));}
 int full(const p10::Group&g,double damage)const{int result=0;for(const auto&m:g.monsters){if(!fuel_ok())return 100000;result+=int(std::ceil(m.max_hp/std::max(1.,damage)));}return result;}
 p10::Directive capital(const p10::World&w,p10::Arm arm){
  work_phase=7;if(!fuel_ok())return {};
  if(!wallet.available(w)||chosen||!fuel_ok())return {};
  if(strength_goal>w.self.strength&&p10_c::Wallet::shop(w)&&w.self.strength_growth>0&&w.round<goal_deadline){
   const auto*g=w.group(goal_source);const int need=int(std::ceil((strength_goal-w.self.strength)/w.self.strength_growth));
   if(fuel_ok()&&g&&g->route_known&&!g->contested&&need>0&&need<=12&&w.own_gold>=need*w.shard_cost&&w.remaining()>need+std::max(0,g->to_spawn)+w.capture_turns+w.ticks(g->gems))return wallet.issue(w,p10::Item::monster_strength,g->id,6401,"fulfil owned service-capacity endpoint; current stats and wallet repriced at shop");
  }
  const double affordable_strength=w.self.strength+std::max(0.,w.self.strength_growth)*std::min(12,w.own_gold/w.shard_cost);
  const p10::Group*a=nullptr,*b=nullptr;
  for(const auto&g:w.groups)if(fuel_ok()&&g.route_known&&g.program_available&&!g.contested&&g.gems>0&&g.to_bank>=0&&service_capacity(w,g,affordable_strength,true)>=0&&(!a||g.gems>a->gems))a=&g;
  if(!a)return {};for(const auto&g:w.groups)if(fuel_ok()&&g.id!=a->id&&g.route_known&&g.program_available&&!g.contested&&g.gems>0&&g.to_bank>=0&&service_capacity(w,g,affordable_strength,true)>=0&&(!b||g.gems>b->gems))b=&g;if(!b)b=a;
  for(const auto*g:{a,b})for(const auto&m:g->monsters)if(!fuel_ok()||m.max_hp<=0)return {};
  const p10::Actor*other=nullptr;for(const auto&v:w.allies)if(fuel_ok()&&v.visible&&v.alive&&v.id!=w.self.id&&v.channel==p10::Channel::none&&(v.type==p10::Champion::marcella||v.type==p10::Champion::willow)&&p10_c::on_spawn(v,*a)&&p10_c::on_spawn(w.self,*a))other=&v;
  const auto kind=a->id==b->id?Kind::two_generations:Kind::two_sources;
  const double factor=w.self.monster_emblem?1.1:1.;auto old=candidate(w,*a,*b,full(*a,w.self.strength*factor),full(*b,w.self.strength*factor),other,kind,arm,true);
  if(!fuel_ok()||!old.valid)return {};const int trip=p10_c::Wallet::detour(w,*a),period=old.service.finish+std::max(a->respawn,b->respawn);int best=0,packet=0;
  for(int n=1;n<=std::min(12,w.own_gold/w.shard_cost);++n)if(w.self.strength_growth>0){work_phase=7;if(!fuel_ok())return {};
   auto improved=candidate(w,*a,*b,full(*a,(w.self.strength+n*w.self.strength_growth)*factor),full(*b,(w.self.strength+n*w.self.strength_growth)*factor),other,kind,arm,true);
   if(!fuel_ok())return {};if(!improved.valid||w.remaining()<improved.service.finish+n+trip)continue;
   const int cycles=arm==p10::Arm::active?std::min(8,(w.remaining()-n-trip)/std::max(1,period)):1;
   // The owner's actual share, not pooled team wealth, pays its menu. Exact
   // per-recipient ceiling service times come from the unchanged batch model.
   if(improved.service.banked[0]<=0)continue;const int saving=old.service.finish-improved.service.finish;
   const int value=saving*cycles-n-trip;if(value>best){best=value;packet=n;}
  }
  if(!fuel_ok()||packet==0)return {};capital_target=a->id;capital_packet=packet;++capital_events;strength_goal=w.self.strength+packet*w.self.strength_growth;goal_source=a->id;goal_deadline=w.round+std::max(1,w.remaining()-old.service.finish);
  return wallet.issue(w,p10::Item::monster_strength,a->id,6401,"reinvest only the owner's actual service capital into a recurring two-capture batch threshold");
 }
 // A first capture is an owned service, not a prospective two-capture batch.
 // The public/full-birth screen is deliberately optimistic: it can reject an
 // impossible 48-turn proposal, never certify its HP, movement or future payout.
 // Only first_choice shares these own-body terms, on its current stack.
 // Public direct helpers below always start fresh. Group HP/size/share is not cached.
 struct AdmissionBody {bool ready=false;int basics=0;double factor=1,spells=0;std::array<double,2>damage{};std::array<bool,2>offensive{},aoe{};};
 int service_capacity(const p10::World&w,const p10::Group&g,double strength,bool birth=false)const{
  AdmissionBody body;return service_capacity(w,g,strength,birth,body);
 }
 int service_capacity(const p10::World&w,const p10::Group&g,double strength,bool birth,AdmissionBody&body)const{
  if(!fuel_ok())return -1;
  const int cap=p10_mobile::Options{}.turn_cap;double hp=0,largest=0,per=0,total=0;int clear=0;
  if(!g.cleared||birth){
   if(g.monsters.empty()||g.monsters.size()>12||strength<=0)return -1;
   for(const auto&m:g.monsters){if(!fuel_ok())return -1;if(!birth&&m.observed&&!m.alive)continue;
    const double h=birth?m.max_hp:m.observed?m.hp:m.max_hp;if(h<=0||!std::isfinite(h))return -1;hp+=h;largest=std::max(largest,h);
   }
   if(hp<=0)return -1;
   const bool prepare=!body.ready;if(prepare){body.factor=w.self.monster_emblem?1.1:1.;body.basics=cap;body.spells=0;}
   double all_spells=0;
   for(int slot=0;slot<2;++slot){if(!fuel_ok())return -1;
    if(prepare){const auto&a=w.self.abilities[slot];
     // Same optimistic current-body ceiling; no cooldown/range or future gear credit.
     const int casts=w.self.type==p10::Champion::vendetta?cap:1+(cap-1)/std::max(1,a.max_cooldown);
     if(a.reset&&a.targets_self)body.basics+=casts;
     body.offensive[slot]=(a.targets_camps||a.targets_ground||ground[slot])&&(a.damage||a.aoe||a.cc);body.aoe[slot]=a.aoe;
     body.damage[slot]=body.offensive[slot]?std::max(0.,a.modifier*w.self.focus*body.factor)*casts:0.;
     if(body.offensive[slot])body.spells+=body.damage[slot];
    }
    // Preserve original slot-order addition and the full member-count factor.
    if(body.offensive[slot])all_spells+=body.damage[slot]*(body.aoe[slot]?g.monsters.size():1);
   }
   if(prepare)body.ready=true; // Never publish a partially built/fuel-aborted body.
   const double basic=strength*body.factor*body.basics;per=basic+body.spells;total=basic*(w.self.type==p10::Champion::varg?g.monsters.size():1)+all_spells;
   if(largest>per||hp>total||total<=0)return -1;
   clear=std::clamp(int(std::ceil(cap*hp/total)),1,cap);
  }
  return clear;
 }
 FirstPrice first_price(const p10::World&w,const p10::Group&g)const{
  AdmissionBody body;return first_price(w,g,body);
 }
 FirstPrice first_price(const p10::World&w,const p10::Group&g,AdmissionBody&body)const{
  FirstPrice out;if(!fuel_ok()||!g.route_known||!g.program_available||g.contested||g.respawning||g.travel<0||(g.gems<=0&&g.gold<=0))return out;
  const int clear=service_capacity(w,g,w.self.strength,false,body);if(clear<0)return out;
  // Price only the owner's share, using the card's exact participation and
  // tribute ceilings. Existing holders never contribute invented future work.
  std::array<int,5>ids{};ids[0]=w.self.id;int count=1;
  for(const auto&a:w.allies){if(!fuel_ok())return out;if(a.id!=w.self.id&&p10_c::on_spawn(a,g)&&count<5)ids[count++]=a.id;}
  const auto gold=metered_works::share(g.gold,ids,count,w.self.id),gems=metered_works::share(g.gems,ids,count,w.self.id);
  if(gold<=0&&gems<=0)return out;
  out.finish=g.travel+clear+w.capture_turns+1;
  if(gems>0){if(g.to_bank<0)return out;out.finish+=2*g.to_bank+w.ticks(gems);}
  if(out.finish>std::min(192,w.remaining()))return out;
  out.tier=g.observed&&g.complete&&(g.cleared||local_contact(w,g))?0:1;if(!fuel_ok())return FirstPrice{};
  out.value=(double(gold)+8.*double(gems))/std::max(1,out.finish);out.valid=true;return out;
 }
 bool first_refused(const p10::World&w,const p10::Group&g)const{
  if(g.cleared)return false;for(const auto&r:first_refusals)if(r.source==g.id&&w.round<r.until)return true;return false;
 }
 static bool durable(const p10_mobile::Result&r){return r.failure==p10_mobile::Failure::health||r.failure==p10_mobile::Failure::blocked||r.failure==p10_mobile::Failure::turn_cap;}
 void refuse(const p10::World&w,int source){first_refusals[first_refusal_cursor++%2]={source,w.round+4};}
 const p10::Group*first_choice(const p10::World&w)const{
  const p10::Group*best=nullptr;FirstPrice price;AdmissionBody body;
  for(const auto&g:w.groups){if(!fuel_ok())return nullptr;if(first_refused(w,g))continue;const auto offer=first_price(w,g,body);
   if(offer.valid&&(!best||offer.tier<price.tier||(offer.tier==price.tier&&(offer.value>price.value||(offer.value==price.value&&g.id<best->id))))){best=&g;price=offer;}
  }
  return best;
 }
 p10::Directive first_service(const p10::World&w,p10_mobile::Budget*shared=nullptr){
  work_phase=8;if(w.self.bag>0)return p10::bank("first actual personal stock completes its own service before a batch");
  if(w.capture_active){const auto*g=w.group(w.productive_group);if(g){first_source=g->id;
   if(!g->observed||!g->complete)return p10_c::inspect(g->id,"first owned custody needs its current lifecycle",4045);
   if(g->cleared&&!g->contested)return p10_c::capture(g->id,"finish actual first personal custody before any batch",4045);
  }}
  auto own_budget=budget(w);auto&b=shared?*shared:own_budget;auto usage=watch(b);
  for(int attempt=0;attempt<2;++attempt){
   if(!fuel_ok())return fuel_stop();const auto*g=w.group(first_source);
   if(!g||first_refused(w,*g)||!first_price(w,*g).valid){g=first_choice(w);if(!fuel_ok())return fuel_stop();first_source=g?g->id:-1;}
   if(!g)return p10::wait("no current first-payout service fits the owner's bounded menu");
   if(!g->observed||!g->complete)return p10_c::inspect(g->id,"first service proposal needs the owner's private arrival observation",4045);
   if(g->cleared)return p10_c::capture(g->id,"secure the actual first owned payout before a two-capture batch",4045);
   if(!local_contact(w,*g)){if(!fuel_ok())return fuel_stop();return p10_c::inspect(g->id,"reach the first service local appointment before proving its work",4045);}
   if(b.spent>=2)return p10::wait("first service preserves the shared two-trace action budget");
   work_phase=9;const auto proof=prove(w,*g,b);if(!fuel_ok())return fuel_stop();
   if(proof.feasible){auto d=proof.first;d.template_id=4045;d.reason="fresh personally bounded first-payout service prefix";return d;}
   if(!durable(proof))return p10_c::inspect(g->id,"first service defers incomplete geometry, hazard or fuel evidence",4045);
   // This is a short proposal refusal, never a retained combat forecast or a
   // permanent HP/occupancy assumption. A newly observed clear bypasses it.
   refuse(w,g->id);first_source=-1;work_phase=8;
  }
  return p10::wait("first service rejected two current physical proposals; reobserve before more work");
 }
 p10::Directive step(const p10::World&w,p10::Arm arm){
  fresh_traces=0;work_world=&w;work_aborted=false;work_checks=0;work_phase=1;wallet.observe(w);
  if(w.own_payout&&last_receipt!=p10_c::receipt_round(w)){last_receipt=p10_c::receipt_round(w);++payouts;if(selected.kind==Kind::two_sources)index=1;}
  if(w.own_bank_completed){chosen=false;index=payouts=0;start=-1;}
  bool owned_receipt=w.own_payout||w.own_bank_completed;
  for(const auto&r:w.receipts)if(r.actor==w.self.id&&r.round>=0&&r.round<=w.round&&(r.kind==p10::Receipt::Kind::payout||r.kind==p10::Receipt::Kind::bank))owned_receipt=true;
  if(owned_receipt){first_income=true;first_source=-1;}
  if(w.self.channel!=p10::Channel::none)return p10::wait("joint batch actual personal channel");
  if(!fuel_ok())return fuel_stop();
  if(w.self.bag>0&&(payouts>=2||w.remaining()<192||(start>=0&&w.round-start>=192)))return p10::bank("all actual recipients finish the declared two-capture service batch");
  if(!first_income)return first_service(w);
  auto investment=capital(w,arm);if(!fuel_ok())return fuel_stop();if(investment.task!=p10::Task::independent)return investment;
  auto budget_=budget(w);auto usage=watch(budget_);for(int selection=0;selection<2;++selection){if(!fuel_ok())return fuel_stop();p10_mobile::Result first_proof,second_proof;const p10::Group*a=nullptr,*b=nullptr;
  if(!chosen){double best=-1;for(const auto&g:w.groups){if(!fuel_ok())return fuel_stop();if(g.route_known&&g.program_available&&!g.contested&&g.gems>0&&!g.respawning&&!first_refused(w,g)&&service_capacity(w,g,w.self.strength)>=0){double q=(g.gems*8+1)/(8+p10::distance(w.spawn,g.position));if(q>best){best=q;a=&g;}}}
   if(!fuel_ok())return fuel_stop();if(!a)return first_service(w,&budget_);if(!a->observed||!a->complete)return p10_c::inspect(a->id,"joint batch public opening appointment needs private work evidence",4040);if(!local_contact(w,*a)){if(!fuel_ok())return fuel_stop();return p10_c::inspect(a->id,"reach the first batch source local appointment before proving its work",4040);}best=-1;for(const auto&g:w.groups){if(!fuel_ok())return fuel_stop();if(g.id!=a->id&&p10::usable(g)&&g.gems>0&&!g.respawning&&!first_refused(w,g)&&service_capacity(w,g,w.self.strength)>=0){double q=(g.gems+1)/(8+g.travel);if(q>best){best=q;b=&g;}}}
   work_phase=2;
   if(a->cleared){first_proof.feasible=true;first_proof.clear_turns=0;}else first_proof=prove(w,*a,budget_);
   if(!fuel_ok())return fuel_stop();if(!first_proof.feasible){if(durable(first_proof)){refuse(w,a->id);continue;}return p10_c::inspect(a->id,"joint batch first physical appointment/warmup",4040);}
   if(b){if(b->cleared){second_proof.feasible=true;second_proof.clear_turns=0;}else if(budget_.spent<2&&local_contact(w,*b))second_proof=prove(w,*b,budget_);}
   if(!fuel_ok())return fuel_stop();if(b&&durable(second_proof))refuse(w,b->id);work_phase=3;const p10::Actor*other=nullptr;for(const auto&v:w.allies){if(!fuel_ok())return fuel_stop();if(v.visible&&v.alive&&v.channel==p10::Channel::none&&v.id!=w.self.id&&(v.type==p10::Champion::marcella||v.type==p10::Champion::willow)&&p10::distance(v.position,a->position)<=6)other=&v;}
   selected=candidate(w,*a,*a,first_proof.clear_turns,first_proof.clear_turns,other,Kind::two_generations,arm);
   if(b&&second_proof.feasible){auto distinct=candidate(w,*a,*b,first_proof.clear_turns,second_proof.clear_turns,other,Kind::two_sources,arm);if(better(distinct,selected))selected=distinct;}
   if(!fuel_ok()){selected={};return fuel_stop();}
   if(!selected.valid){if(a->cleared)return p10_c::capture(a->id,"unavailable common batch still secures real current custody",4044);auto d=first_proof.first;d.template_id=4040;return d;}
   chosen=true;index=payouts=0;start=w.round;
  }
  if(!fuel_ok())return fuel_stop();const auto*g=w.group(selected.sources[index]);if(w.capture_active)g=w.group(w.productive_group);if(!g){chosen=false;return w.self.bag>0?p10::bank():first_service(w,&budget_);}
  if(g->contested){chosen=false;return w.self.bag>0?p10::bank("interrupted declared batch liquidates actual stock"):first_service(w,&budget_);}
  if(g->respawning){if(selected.kind==Kind::two_sources&&index==0){index=1;return p10_c::inspect(selected.sources[1],"second declared source after observed first payout",4040);}if(w.self.bag>0&&g->respawn_remaining+w.ticks(w.self.bag)+g->to_bank>w.remaining())return p10::bank();
   if(p10_c::on_spawn(w.self,*g)){p10::Directive d;d.task=p10::Task::release;d.group=g->id;d.template_id=4042;return d;}return p10::wait("two-generation batch waits off all renewal spawns");}
  if(!g->cleared&&(first_refused(w,*g)||service_capacity(w,*g,w.self.strength)<0)){if(!fuel_ok())return fuel_stop();refuse(w,g->id);chosen=false;if(w.self.bag>0)return p10::bank("unserviceable declared batch liquidates actual personal stock");continue;}
  if(!g->observed||!g->complete)return p10_c::inspect(g->id,"declared next capture requires personal lifecycle observation",4040);
  if(g->cleared){bool party=selected.participant>=0;int holders=0;for(const auto&v:w.allies){if(!fuel_ok())return fuel_stop();if(p10_c::on_spawn(v,*g)&&!(v.position==w.self.position)){++holders;if(v.id!=selected.participant)party=false;}}party&=holders==1&&p10_c::on_spawn(w.self,*g);if(selected.party==metered_works::Program::parallel&&party)++actual_parallel;return p10_c::capture(g->id,"future party becomes actual only at observed payout occupation",selected.party==metered_works::Program::parallel?(party?4043:4041):4044);}
  if(!local_contact(w,*g)){if(!fuel_ok())return fuel_stop();return p10_c::inspect(g->id,"return to the declared batch source local appointment before rebuilding work",4040);}
  p10_mobile::Result proof;if(a&&a->id==g->id&&first_proof.feasible)proof=first_proof;else if(b&&b->id==g->id&&second_proof.feasible)proof=second_proof;else if(budget_.spent<2)proof=prove(w,*g,budget_);
  if(!fuel_ok())return fuel_stop();if(proof.feasible){auto d=proof.first;d.template_id=selected.kind==Kind::two_generations?4042:4041;d.reason="jointly chosen batch's exact mobile physical prefix";return d;}if(durable(proof)){refuse(w,g->id);chosen=false;if(w.self.bag>0)return p10::bank("failed declared physical service liquidates actual personal stock");continue;}return p10_c::inspect(g->id,"declared mobile work warmup",4040);
  }
  return w.self.bag>0?p10::bank("two refused batch proposals preserve actual stock"):p10::wait("two batch physical proposals refused; reobserve before more work");
 }
};
}
