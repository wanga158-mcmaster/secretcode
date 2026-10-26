#pragma once
#include "live-profile.h"
namespace p9::bounty_syndicate {
class Policy {
    live::Campaign campaign_{};
    int original_=-1,kill_round_=-1,purchase_requested_=-1,purchase_target_=-1,expansion_=-1,expansion_payout_=-1;
    bool credited_=false,developed_=false;
public:
    live::Directive step(const live::World& w,live::Arm arm) {
        using namespace live;
        if(original_<0&&w.productive_group>=0) original_=w.productive_group;
        if(w.immediate_bank()) return bank();
        if(!w.self.alive||!w.safe_now) {campaign_.finish();return {};}
        if(w.self.pick>1) return {};
        if(w.self.pick==0&&arm==Arm::active&&w.own_kill_receipt&&w.last_kill_target>=0&&
            w.last_kill_round>kill_round_&&w.last_kill_round<=w.round&&w.round-w.last_kill_round<=1) {
            kill_round_=w.last_kill_round;credited_=true;
        }
        if(w.own_payout&&expansion_>=0&&w.last_payout_group==expansion_&&w.last_payout_round>kill_round_)
            expansion_payout_=w.last_payout_round;
        if(campaign_.active) {campaign_.tick(w,w.enemy_count>0||w.ally_count>0);
            if(campaign_.expired(w)) {campaign_.finish();return {};}}
        if(w.self.pick==0&&arm==Arm::active&&credited_) {
            if(w.self.bag>0) return bank();
            if(w.own_purchase_receipt&&purchase_requested_>=0&&w.last_purchase_round>=purchase_requested_&&
                purchase_target_>0&&w.self.defense_shards>=purchase_target_) developed_=true;
            if(!developed_&&w.defense_cost>0&&w.own_gold>=w.defense_cost) {
                if(!campaign_.active&&!campaign_.start(w)) return {};
                if(!w.self.at_spawn) return {Task::recover,-1,-1,-1,-1,true,"bank killer's actual loot then shop"};
                purchase_requested_=w.round;
                purchase_target_=w.self.defense_shards+1;
                return {Task::buy_defense,-1,-1,-1,-1,true,"actual Vesper credit funds productive expansion",purchase_target_};
            }
            if(developed_) {
                const auto* g=expansion_>=0?w.group(expansion_):nearest_safe(w,original_);
                if(g&&safe_for(*g,w.self)&&cycle_value(w,*g).feasible) {
                    if(!campaign_.active&&!campaign_.start(w)) return {};
                    expansion_=g->id;
                    if(w.own_bank_completed&&expansion_payout_>=0&&w.last_bank_round>=expansion_payout_) {
                        credited_=developed_=false;campaign_.finish();return {};
                    }
                    return {g->cleared?Task::capture:Task::farm,g->id,-1,-1,-1,true,"killer changes to new complete production source"};
                }
            }
        }
        if(const auto* enemy=favorable_rival(w)) {
            if(!campaign_.active&&!campaign_.start(w)) return {};
            return {Task::intercept,-1,enemy->id,-1,-1,true,"same favorable local interception; no promised kill credit"};
        }
        if(campaign_.active&&!credited_) campaign_.finish();
        return {};
    }
};
}
