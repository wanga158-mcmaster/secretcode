#pragma once
#include "b-policy.h"
#include "../../common/src/c-capital-11-16.h"
#include <optional>
namespace p10_c16 {
using namespace p10;
struct CPolicy:p10_b16::BPolicy {
 static constexpr bool owns_capital=true;
 static constexpr bool observe_ally_details=false;
 static constexpr std::uint64_t observe_ally_detail_types=std::uint64_t{0};
 p10_c::OwnedOrder capital{};int financedBank=-1,replacementBankDue=-1;bool deferredReplacement=false;
 struct HomeReturn {
  int owner=-1,source=-1,payout=-1,bank=-1,death=-1;Gems gems=0,bankGems=0;bool pending=false;
  void observe(const World&w){
   if(owner!=w.self.id){*this={};owner=w.self.id;}
   int died=death;for(const auto&r:w.receipts)if(r.actor==owner&&r.kind==Receipt::Kind::death&&r.round>=0&&r.round<=w.round)died=std::max(died,r.round);
   if(!w.self.alive||w.self.hp<=0)died=std::max(died,w.round);
   if(died>death){death=died;pending=false;source=-1;gems=0;}
   for(const auto&r:w.receipts)if(r.actor==owner&&r.kind==Receipt::Kind::payout&&r.round>payout&&r.round>death&&r.round<=w.round&&r.group>=0){payout=r.round;source=r.gems>0?r.group:-1;gems=r.gems;pending=false;}
   for(const auto&r:w.receipts)if(r.actor==owner&&r.kind==Receipt::Kind::bank&&r.round>bank&&r.round>=0&&r.round<=w.round){bank=r.round;bankGems=r.gems;}
   // A scoped positive receipt and a matching whole-bag bank are required;
   // another actor, purchase, old bank or unscoped flag supplies no ownership.
   if(source>=0&&gems>0&&payout>death&&bank>=payout&&bankGems>=gems&&w.self.alive&&w.self.hp>0&&w.self.bag==0)pending=true;
  }
 } homeReturn{};
 static bool positive_service(const World&w,const Directive&d,const p10_b16::BPolicy&p){
  const auto*g=w.group(d.group);if(!g||!p10_b::work_source(*g)||personal_share(w,*g)<=0)return false;
  if((d.task==Task::farm||d.task==Task::capture)&&g->cleared)return true;
  const int id=int(p.selected);return id>=0&&id<4&&d.template_id==1610+id&&p.schedules[id].source==g->id&&p.schedules[id].feasible&&p.schedules[id].ownBank>0&&p.schedules[id].ownService>0&&fresh_certificate(w,d);
 }
 Directive return_home(const World&w)const{
  auto budget=p10_mobile::budget_for(w);auto fuel=[&]{return budget.fuel_ok();};
  if(!fuel())return wait("named home return defers before the unchanged action reserve");
  if(!w.self.alive||w.self.hp<=0||w.self.bag>0||w.self.channel!=Channel::none||w.capture_active)return {};
  if(!w.safe_now)return {Task::recover};
  if(!w.geometry_complete||!w.actor_observation_complete||!w.self.can_move)return wait("named home return awaits a fresh movable geometry/actor frame");
  const auto*g=w.group(homeReturn.source);
  if(!g||!g->program_available||!g->route_known||g->travel<0||g->to_bank<0||g->travel>w.remaining()||!p10_mobile::current_geometry_binding(*g))return wait("named home return retains its receipt until the route/geometry is currently represented");
  if(g->contested)return wait("named home return defers actual contested custody");
  if(g->respawning)return wait("named home return defers the observed source cooldown");
  if(g->observed&&g->complete&&personal_share(w,*g)<=0)return wait("named home return refuses the currently zero personal share");
  for(const auto&e:w.enemies){if(!fuel())return wait("named home return defers before the unchanged action reserve");if(e.visible&&e.alive&&(distance(e.position,w.self.position)<=8||distance(e.position,g->position)<=8))return wait("named home return defers a currently visible enemy near actor/source");}
  if(!fuel())return wait("named home return defers before the unchanged action reserve");
  auto d=p10_b::approach(*g,2162,"actual source payout and matching own bank retain the named home reveal/renewal leg");d.valid_round=w.round;return d;
 }
 Directive step(const World&w,Arm arm){
  economy.consume(w);observe_acquisition(w);homeReturn.observe(w);traces=0;
  if(w.self.channel!=Channel::none||w.capture_active)return BPolicy::step(w,arm);
  if(p10_c::winning_bag(w))return bank("border producer closes actual winning inventory before capital");
  auto pending=capital.poll(w,"personally funded border appointment renews its actual capability",2160);if(pending.task!=Task::independent)return pending;
  if(replacementOwned)replacementBankDue=std::max(replacementBankDue,acquiredRound);
  if(economy.lastBank>=replacementBankDue)replacementBankDue=-1;
  deferredReplacement=replacementBankDue>=0;
  // A committed hit is at most eight turns; a committed replacement closes
  // its own real receipt and bank before funding a later appointment.
  if(attempts.active)return BPolicy::step(w,arm);
  if(deferredReplacement){if(w.self.bag>0)return bank("replacement receipt closes its promised bank before personal capital renewal");return BPolicy::step(w,arm);}
  // Retain the original planner/menu. A zero-positive current frontier needs
  // no physical trace; otherwise preview that same planner once, charge its
  // real traces, and commit it only if optional capital does not take priority.
  std::optional<p10_b16::BPolicy> planned;Directive plannedAction;
  if(homeReturn.pending&&homeReturn.owner==w.self.id&&w.self.bag==0){
   auto budget=p10_mobile::budget_for(w);bool potential=false,capability=false;
   for(const auto&g:w.groups){if(!budget.fuel_ok())return wait("named renewal catalogue defers before the unchanged reserve");potential|=p10_b::work_source(g)&&personal_share(w,g)>0;}
   for(const auto&e:w.enemies){if(!budget.fuel_ok())return wait("named renewal context defers before the unchanged reserve");capability|=e.visible&&e.alive&&((e.type==Champion::prism&&e.bag>0)||e.type==Champion::cassi||e.type==Champion::issac);}
   if(!potential&&!capability){auto d=return_home(w);if(d.task!=Task::independent)return d;}
   planned.emplace(static_cast<const p10_b16::BPolicy&>(*this));plannedAction=planned->step(w,arm);traces=planned->traces;
   if(!positive_service(w,plannedAction,*planned)&&!planned->attempts.active&&!planned->replacementOwned&&plannedAction.actor<0&&plannedAction.task!=Task::bank){
    auto d=return_home(w);if(d.task!=Task::independent){static_cast<p10_b16::BPolicy&>(*this)=std::move(*planned);return d;}
   }
  }
  const bool boundary=p10_c::legal_shop(w)||(economy.lastBank>financedBank&&w.self.bag==0);
  if(boundary&&w.remaining()>160&&!p10_c::near_awakening(w)){
   const bool ren=w.self.type==Champion::ren;Item item=ren?Item::monster_focus:Item::monster_strength;int count=std::max(0,5-w.self.inventory[int(item)]);
   if(w.self.monster_emblem){item=!w.self.jungle_emblem?Item::jungle_health:(ren?Item::focus:Item::strength);count=item==Item::jungle_health?std::max(0,5-w.self.inventory[int(item)]):5;}
   if(count>0&&capital.open(w,{{item,count}})){financedBank=economy.lastBank;return capital.poll(w,ren?"own closed border receipts fund recurrent control and burst appointments":"own closed source receipts fund recurrent productive acquisition strength",2161);}
  }
  // Both arms keep this same capital and complete acquisition menu. Only the
  // active nominal denial term can change geography; adverse uncertainty remains.
  if(planned){static_cast<p10_b16::BPolicy&>(*this)=std::move(*planned);return plannedAction;}
  return BPolicy::step(w,arm);
 }
};
}
