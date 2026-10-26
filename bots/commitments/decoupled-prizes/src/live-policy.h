#pragma once
#include "live-profile.h"
#ifndef P9_DECOUPLED_INTEGER_RATIOS
#define P9_DECOUPLED_INTEGER_RATIOS 0
#endif
namespace p9::decoupled_prizes {
#if P9_DECOUPLED_INTEGER_RATIOS
inline int compare_reward_composition(const live::Group& a,const live::Group& b) {
    // Available rewards are nonnegative. SDK rewards are uint32, and even
    // the contract's full nonnegative int64 products fit this integer width.
    // Zero-gem sources keep the existing positive-infinity ordering.
    if(a.gems==0||b.gems==0) return (a.gems==0)-(b.gems==0);
    const auto left=static_cast<unsigned __int128>(a.gold)*static_cast<unsigned __int128>(b.gems);
    const auto right=static_cast<unsigned __int128>(b.gold)*static_cast<unsigned __int128>(a.gems);
    return (left>right)-(left<right);
}
#endif
class Policy {
    live::Campaign campaign_{};
    int gold_=-1,gems_=-1,payout_round_=-1,purchase_requested_=-1,purchase_target_=-1;
    bool earned_=false,developed_=false;
    static bool available(const live::World& w,const live::Group& g) {
        return g.observed&&g.route_known&&g.safe_solo&&!g.respawning&&g.travel>=0&&g.to_bank>=0&&
            g.clear_turns>=0&&g.gems>=0&&g.gold>=0&&live::safe_for(g,w.self)&&
            g.travel+g.clear_turns+w.capture_turns+2*g.to_bank+w.ticks(std::max<live::Gems>(1,g.gems))+4<=120;
    }
    static long double ratio(const live::Group& g) {
        return g.gems==0 ? std::numeric_limits<long double>::infinity() :
            static_cast<long double>(g.gold)/g.gems;
    }
public:
    live::Directive step(const live::World& w,live::Arm arm) {
        using namespace live;
        if(w.immediate_bank()) return bank();
        if(!w.self.alive||!w.safe_now) {campaign_.finish();return {};}
        if(arm==Arm::control) {
            if(w.self.pick!=0||w.self.bag>0) return w.self.bag>0?bank():Directive{};
            const Group* best=nullptr; Value value;
            for(int i=0;i<w.group_count;++i) {const auto v=cycle_value(w,w.groups[i]);
                if(v.feasible&&(!best||better(v,value))) {best=&w.groups[i];value=v;}}
            return best?farm(best->id,"same principal owns rich cycle and development"):Directive{};
        }
        // A successful payout makes its source unavailable immediately. Consume
        // the personal receipt against the already chosen owner/source before
        // filtering live opportunities or choosing any later production.
        if(w.self.pick==0&&gold_>=0&&w.own_payout&&w.last_payout_group==gold_&&
            w.last_payout_round>payout_round_&&w.last_payout_round<=w.round) {
            payout_round_=w.last_payout_round;earned_=true;
        }
        if(w.self.pick==0&&w.own_purchase_receipt&&purchase_requested_>=0&&
            w.last_purchase_round>=purchase_requested_&&purchase_target_>0&&w.self.defense_shards>=purchase_target_) developed_=true;
        if(campaign_.active) {
            const auto* source=w.group(gold_);
            campaign_.tick(w,source&&source->observed);
            if(campaign_.expired(w)) {campaign_.finish();return {};}
        }
        if(gold_<0||gems_<0) {
            const Group* gold=nullptr;const Group* gems=nullptr;
            for(int i=0;i<w.group_count;++i) {
                const auto& g=w.groups[i];if(!available(w,g)) continue;
#if P9_DECOUPLED_INTEGER_RATIOS
                if(g.gold>0&&(!gold||compare_reward_composition(g,*gold)>0||(compare_reward_composition(g,*gold)==0&&g.id<gold->id))) gold=&g;
                if(g.gems>0&&(!gems||compare_reward_composition(g,*gems)<0||(compare_reward_composition(g,*gems)==0&&g.id<gems->id))) gems=&g;
#else
                if(g.gold>0&&(!gold||ratio(g)>ratio(*gold)||(ratio(g)==ratio(*gold)&&g.id<gold->id))) gold=&g;
                if(g.gems>0&&(!gems||ratio(g)<ratio(*gems)||(ratio(g)==ratio(*gems)&&g.id<gems->id))) gems=&g;
#endif
            }
#if P9_DECOUPLED_INTEGER_RATIOS
            if(!gold||!gems||gold->id==gems->id||compare_reward_composition(*gold,*gems)<=0) {campaign_.finish();return {};}
#else
            if(!gold||!gems||gold->id==gems->id||!(ratio(*gold)>ratio(*gems))) {campaign_.finish();return {};}
#endif
            gold_=gold->id;gems_=gems->id;
        }
        const auto* gold=w.group(gold_);const auto* gems=w.group(gems_);
        const auto* tavi=w.pick(0);const auto* marcella=w.pick(1);
        if(w.self.pick==0) {
            if(w.self.bag>0) return bank();
            if(earned_&&!developed_&&w.defense_cost>0&&w.own_gold>=w.defense_cost) {
                if(!campaign_.active&&!campaign_.start(w)) return {};
                purchase_requested_=w.round;
                purchase_target_=w.self.defense_shards+1;
                return {Task::buy_defense,-1,-1,-1,-1,true,"Tavi develops his actual gold-heavy payout",purchase_target_};
            }
            const bool contact=marcella&&marcella->visible&&marcella->alive&&distance(marcella->position,w.self.position)<=5;
            if(!contact) return {};
            const bool gem_project=gems&&available(w,*gems);
            if(developed_&&!gem_project&&marcella->bag<=0) {campaign_.finish();return {};}
            if(!developed_&&(earned_||!ready(marcella)||!gold||!available(w,*gold))) return {};
            if(!campaign_.active&&!campaign_.start(w)) return {};
            campaign_.tick(w,true);if(campaign_.expired(w)) {campaign_.finish();return {};}
            if(developed_) return support(marcella->id,gem_project?gems_:-1,"combat capital guards separate future gem owner");
            return {gold->cleared?Task::capture:Task::farm,gold_,-1,-1,-1,true,"Tavi owns entire gold-heavy source"};
        }
        if(w.self.pick==1&&ready(tavi)&&distance(tavi->position,w.self.position)<=5) {
            if(w.self.bag>0) return bank();
            if(!gems||!available(w,*gems)) {campaign_.finish();return {};}
            if(!campaign_.active&&!campaign_.start(w)) return {};
            campaign_.tick(w,true);if(campaign_.expired(w)) {campaign_.finish();return {};}
            return {gems->cleared?Task::capture:Task::farm,gems_,-1,-1,-1,true,"Marcella owns entire gem-heavy source"};
        }
        if(w.self.pick==2&&ready(marcella)&&gems&&available(w,*gems)&&safe_for(*gems,*marcella)&&
            distance(marcella->position,w.self.position)<=4) {
            if(!campaign_.active&&!campaign_.start(w)) return {};
            campaign_.tick(w,true);if(campaign_.expired(w)) {campaign_.finish();return {};}
            return support(marcella->id,gems_,"visible safe help to separate converter");
        }
        return {};
    }
};
}
