#pragma once
#include "live-profile.h"
namespace p9::banked_vanguard {
class Policy {
    live::Campaign campaign_{};
    live::Gems whole_bag_=0;
    int bank_requested_=-1,source_=-1,retired_source_=-1;
    double loaded_focus_=0,post_bank_focus_=0;
    bool banked_=false;
    static live::Directive ordinary(const live::World& w,int excluded=-1,const char* why="vanguard-independent") {
        if(!w.self.alive||!w.safe_now) return {live::Task::recover,-1,-1,-1,-1,false,why};
        if(excluded<0)return {live::Task::independent,-1,-1,-1,-1,false,why};
        // A retired future-prize assignment remains excluded from this actor's
        // renewal; the shared economy must not silently reclaim that source.
        if(w.self.bag>0) return live::bank(-1,why);
        const auto* group=live::nearest_safe(w,excluded);
        return group?live::farm(group->id,why):live::Directive{};
    }
    static const live::Group* contested_source(const live::World& w,const live::Actor& principal,
                                               const live::Actor& vanguard,const live::Actor& collector) {
        const live::Group* best=nullptr;
        for(int i=0;i<w.group_count;++i) {
            const auto& g=w.groups[i];
            if(!g.contested||g.respawning||!g.safe_solo||!live::safe_for(g,principal)||
                !live::safe_for(g,vanguard)||!live::safe_for(g,collector)||
                w.group_travel(principal.id,g.id)<0||w.group_travel(vanguard.id,g.id)<0||
                w.group_travel(collector.id,g.id)<0) continue;
            if(!best||g.id<best->id) best=&g;
        }
        return best;
    }
    live::Directive deployed(const live::World& w,const live::Group& group) const {
        using namespace live;
        if(w.self.pick==0) return support(w.pick(2)?w.pick(2)->id:-1,group.id,"banked-prism-supports-with-observed-post-bank-stats");
        if(w.self.pick==1) {
            if(group.cleared) return {Task::capture,group.id,-1,-1,-1,true,"different-marcella-owns-future-whole-payout"};
            auto out=farm(group.id,"future-collector-clears-before-own-capture");out.strategic=true;return out;
        }
        if(group.living) {auto out=farm(group.id,"varg-clears-for-different-future-collector");out.strategic=true;return out;}
        return support(w.pick(1)?w.pick(1)->id:-1,group.id,"varg-releases-capture-to-visible-marcella");
    }
public:
    static constexpr bool avoid_contested_captures() {return true;}
    static constexpr bool durability_after_first_jungle(live::Champion type) {
        return type==live::Champion::prism;
    }
    live::Directive step(const live::World& w,live::Arm arm) {
        using namespace live;
        if(campaign_.active) campaign_.tick(w,false);
        // Runtime preserves an already safe channel/capture before executing a
        // directive. Policy never cancels one to manufacture a handoff.
        if(w.self.channel!=Channel::none||w.capture_active) {
            if(w.self.pick==0&&bank_requested_>=0&&w.self.bag>whole_bag_) whole_bag_=w.self.bag;
            return {};
        }
        if(!w.self.alive||!w.safe_now||w.extra_opposition) {
            campaign_.finish();banked_=false;bank_requested_=-1;
            return ordinary(w,-1,"vanguard-safety-fallback");
        }
        if(w.self.pick<0||w.self.pick>2||w.immediate_bank()) return ordinary(w);
        const auto* prism=w.pick(0);const auto* collector=w.pick(1);const auto* varg=w.pick(2);
        if(arm==Arm::control) {
            if(w.self.pick==1) return ordinary(w,-1,"control-marcella-keeps-independent-work");
            if(!ready(prism)||!ready(varg)||!collector||!collector->visible) return ordinary(w);
            const auto* group=contested_source(w,*prism,*varg,*prism);
            if(!group) return ordinary(w,-1,"control-loaded-deployment-infeasible");
            if(w.self.pick==0) return {group->cleared?Task::capture:Task::farm,group->id,-1,-1,-1,true,"control-loaded-prism-keeps-focus-and-next-prize"};
            return support(prism->id,group->id,"control-varg-supports-loaded-prism-collector");
        }
        if(w.self.pick==0&&bank_requested_>=0&&w.self.bag==0&&!banked_) {
            if(w.own_bank_completed&&w.last_bank_round>=bank_requested_&&w.bank_lower>=whole_bag_) {
                banked_=true;post_bank_focus_=w.self.focus;
            } else {
                bank_requested_=-1;whole_bag_=0;
                return ordinary(w,retired_source_,"vanguard-bag-loss-is-not-a-bank-receipt");
            }
        }
        if(w.self.pick==0&&w.self.bag>0) {
            if(campaign_.active) {retired_source_=source_;campaign_.finish();}
            if(bank_requested_<0||banked_) {
                bank_requested_=w.round;whole_bag_=w.self.bag;loaded_focus_=w.self.focus;banked_=false;
            } else whole_bag_=std::max(whole_bag_,w.self.bag);
            return bank(-1,"prism-banks-whole-actual-bag-before-role-change");
        }
        if(w.self.pick!=0&&w.self.bag>0) return bank(-1,"vanguard-convert-own-actual-payout");
        if(campaign_.active) {
            const auto* group=w.group(source_);
            const bool fresh=group&&group->observed&&ready(prism)&&ready(varg)&&ready(collector)&&prism->bag==0;
            campaign_.tick(w,fresh);
            if(!fresh||campaign_.expired(w)||!safe_for(*group,w.self)||
                (w.self.pick==0&&(!banked_||w.self.focus!=post_bank_focus_))) {
                retired_source_=source_;campaign_.finish();banked_=false;
                return ordinary(w,retired_source_,"vanguard-post-bank-or-crew-feasibility-lost");
            }
            if(w.own_bank_completed&&w.last_bank_round>campaign_.admitted) {
                retired_source_=source_;campaign_.finish();banked_=false;
                return ordinary(w,retired_source_,"vanguard-own-conversion-and-low-bag-renewal");
            }
            return deployed(w,*group);
        }
        if(!ready(prism)||!ready(varg)||!ready(collector)||!prism->ally||!varg->ally||!collector->ally||
            prism->bag!=0||distance(prism->position,w.self.position)>3||distance(varg->position,w.self.position)>3||
            distance(collector->position,w.self.position)>3) return ordinary(w,retired_source_,"vanguard-no-fresh-visible-role-meeting");
        // Only Prism's private policy can admit Prism using his own receipt.
        // Helpers adopt the deterministic visible zero-bag posture through their
        // own current safety; they never receive or spend that private receipt.
        if(w.self.pick==0&&(!banked_||!std::isfinite(post_bank_focus_)||post_bank_focus_>loaded_focus_))
            return ordinary(w,retired_source_,"vanguard-no-whole-bag-bank-and-post-bank-focus");
        const auto* group=contested_source(w,*prism,*varg,*collector);
        if(!group||group->id==retired_source_||!campaign_.start(w)) return ordinary(w,retired_source_,"vanguard-contested-safe-deployment-unavailable");
        source_=group->id;
        return deployed(w,*group);
    }
};
}
