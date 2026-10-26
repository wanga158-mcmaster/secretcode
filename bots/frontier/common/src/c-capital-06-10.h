#pragma once
#include "contract.h"

// Personal receipt accounting and legal shop plumbing only. Every C policy
// owns its source order, reinvestment recipe and return threshold.
namespace p10_c_capital {
struct Income {
 int payout=-1,purchase=-1,bank=-1,payouts=0;bool funded=false;
 void read(const p10::World&w){
  int pay=w.own_payout?(w.last_payout_round>=0?w.last_payout_round:w.round):-1;
  int bought=w.own_purchase?w.round:-1,banked=w.own_bank_completed?(w.last_bank_round>=0?w.last_bank_round:w.round):-1;
  for(const auto&r:w.receipts)if(r.actor==w.self.id){
   if(r.kind==p10::Receipt::Kind::payout)pay=std::max(pay,r.round);
   if(r.kind==p10::Receipt::Kind::purchase)bought=std::max(bought,r.round);
   if(r.kind==p10::Receipt::Kind::bank)banked=std::max(banked,r.round);
  }
  if(pay>payout){payout=pay;++payouts;funded=true;}
  purchase=std::max(purchase,bought);bank=std::max(bank,banked);
 }
};
inline bool legal_shop(const p10::World&w){return w.self.at_spawn||w.self.type==p10::Champion::tavi;}
inline int stock(const p10::World&w,p10::Item item){return w.self.inventory[int(item)];}
inline int affordable(const p10::World&w){return w.shard_cost>0?std::clamp(w.own_gold/w.shard_cost,0,5):0;}
inline p10::Directive purchase(const p10::World&w,p10::Item item,int requested,const char*reason){
 return p10::buy(item,std::min(affordable(w),std::clamp(requested,1,5)),reason);
}
inline bool protected_turn(const p10::World&w){return w.self.channel!=p10::Channel::none||w.capture_active;}
}
