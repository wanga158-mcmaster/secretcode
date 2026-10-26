#pragma once
#include "live-profile.h"
namespace p9::dividend_redeployment {
class Policy {
    live::Campaign campaign_{};
    int home_=-1, group_=-1, bank_round_=-1, purchase_requested_=-1, purchase_target_=-1;
    bool funded_=false, purchased_=false, saw_loaded_=false;
    live::Directive renew(const live::World& w) {
        campaign_.finish();
        const auto* home=w.group(home_);
        return home&&home->observed&&home->route_known&&home->safe_solo&&!home->respawning ?
            live::farm(home_,"resume original production") : live::Directive{};
    }
public:
    live::Directive step(const live::World& w,live::Arm arm) {
        using namespace live;
        if(home_<0&&w.productive_group>=0) home_=w.productive_group;
        if(w.immediate_bank()) return bank();
        if(!w.self.alive||!w.safe_now) { campaign_.finish(); return {}; }
        if(arm==Arm::control) return {};
        const auto* marcella=w.pick(0); const auto* varg=w.pick(1);
        if(w.self.pick==0) {
            if(w.own_bank_completed&&w.bank_lower>0&&w.last_bank_round>bank_round_) {
                bank_round_=w.last_bank_round; funded_=true; purchased_=false; purchase_requested_=-1;
            }
            if(w.own_purchase_receipt&&purchase_requested_>=0&&w.last_purchase_round>=purchase_requested_&&
                purchase_target_>0&&w.self.defense_shards>=purchase_target_) { purchased_=true; funded_=false; }
            if(campaign_.active) {
                const auto* g=w.group(group_);
                const bool contact=varg&&varg->visible&&varg->alive&&distance(varg->position,w.self.position)<=5;
                campaign_.tick(w,contact);
                if(!contact||campaign_.expired(w)||!g||!safe_for(*g,*varg)) return renew(w);
                saw_loaded_|=varg->bag>0;
                if(saw_loaded_&&varg->bag==0&&varg->channel==Channel::none) return renew(w);
                return support(varg->id,group_,"funded collector becomes present support");
            }
            const auto* g=nearest_safe(w,home_);
            if(!g) g=nearest_safe(w);
            if(!ready(varg)||!g||!safe_for(*g,*varg)||distance(varg->position,w.self.position)>4) return {};
            if(funded_&&!purchased_&&w.defense_cost>0&&w.own_gold>=w.defense_cost&&campaign_.may_start(w)) {
                if(!w.self.at_spawn) return {Task::recover,-1,-1,-1,-1,false,"visit spawn with actual bank-funded gold"};
                purchase_requested_=w.round;
                purchase_target_=w.self.defense_shards+1;
                return {Task::buy_defense,-1,-1,-1,-1,true,"spend only actual personal funds",purchase_target_};
            }
            if(purchased_&&campaign_.start(w)) {
                group_=g->id; saw_loaded_=varg->bag>0;
                return support(varg->id,group_,"post-purchase role exchange");
            }
        } else if(w.self.pick==1) {
            if(w.self.bag>0) return bank();
            if(w.own_bank_completed&&campaign_.active) campaign_.finish();
            const auto* g=campaign_.active?w.group(group_):nearest_safe(w);
            const bool proposal=ready(marcella)&&marcella->defense_shards>0&&marcella->bag==0&&
                distance(marcella->position,w.self.position)<=4;
            if(campaign_.active) {
                campaign_.tick(w,proposal);
                if(!proposal||campaign_.expired(w)) {campaign_.finish();return {};}
            } else if(proposal&&g&&safe_for(*g,w.self)&&campaign_.start(w)) group_=g->id;
            if(campaign_.active&&g&&safe_for(*g,w.self))
                return {g->cleared?Task::capture:Task::farm,g->id,-1,-1,-1,true,"Varg owns future whole payout"};
        }
        return {};
    }
};
}
