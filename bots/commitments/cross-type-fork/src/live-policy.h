#pragma once
#include "live-profile.h"
namespace p9::cross_type_fork {
class Policy {
    live::Campaign campaign_{};
    int source_=-1,altar_=-1,target_=-1,converter_=-1;
    static live::Directive ordinary(const live::World& w,const char* why="cross-independent") {
        if(!w.self.alive||!w.safe_now) return {live::Task::recover,-1,-1,-1,-1,false,why};
        return {live::Task::independent,-1,-1,-1,-1,false,why};
    }
    static live::Directive work(const live::World& w,int group,int target,const char* why) {
        live::Directive out=w.self.pick==2?live::support(w.pick(0)?w.pick(0)->id:-1,group,why):live::farm(group,why);
        out.until=target;out.strategic=true;return out;
    }
public:
    live::Directive step(const live::World& w,live::Arm arm) {
        using namespace live;
        if(campaign_.active) campaign_.tick(w,false);
        if(w.self.channel!=Channel::none||w.capture_active) return {};
        if(!w.self.alive||!w.safe_now||w.extra_opposition||w.self.pick<0||w.self.pick>2) {
            campaign_.finish();return ordinary(w,"cross-lost-local-feasibility");
        }
        if(w.immediate_bank()) return bank(-1,"cross-terminal-own-bank");
        const auto* converter=w.pick(1);
        if(arm==Arm::control) {
            if(w.self.pick==1||w.self.bag>0) return ordinary(w,"control-convert-before-acquisition");
            // Conversion is a visible prerequisite, never inferred from a
            // private teammate flag. Missing contact resumes own production.
            if(ready(converter)&&converter->bag==0) {
                const auto* group=nearest_safe(w);
                if(group) return farm(group->id,"control-acquire-after-visible-unloaded-converter");
            }
            return {Task::independent,-1,-1,-1,-1,false,"control-conversion-not-yet-observed"};
        }
        if(campaign_.active) {
            const auto* group=w.group(source_);const auto* altar=w.altar(altar_);
            const bool current=group&&group->observed&&group->route_known&&!group->contested&&
                altar&&altar->visible&&!altar->enemy_lock;
            campaign_.tick(w,current);
            if(!current||campaign_.expired(w)||w.round>target_+4) {
                campaign_.finish();return ordinary(w,"cross-slot-or-current-project-failed");
            }
            if(w.own_bank_completed&&w.last_bank_round>=campaign_.admitted) {campaign_.finish();return ordinary(w,"cross-converted-renewal");}
            if(w.self.pick==1) {
                if(w.self.bag<=0) {campaign_.finish();return ordinary(w,"cross-converter-return");}
                const int travel=w.altar_travel(w.self.id,altar_);
                if(travel<0||w.round+travel>target_) {campaign_.finish();return bank(altar_,"cross-bank-missed-slot");}
                auto out=bank(altar_,"cross-own-bank-concurrent-with-source");out.until=target_;out.strategic=true;return out;
            }
            if(w.self.bag>0) return bank(-1,"cross-acquisition-own-conversion");
            if(!safe_for(*group,w.self)) {campaign_.finish();return ordinary(w,"cross-acquisition-unsafe");}
            return work(w,source_,target_,"cross-source-during-conversion-window");
        }
        if(w.self.pick!=1&&w.self.bag>0) return ordinary(w);
        if(w.round%16>3||!ready(converter)||!converter->ally||converter->bag<=0)
            return ordinary(w,"cross-no-loaded-visible-converter");
        const auto* producer=w.pick(0);const auto* helper=w.pick(2);
        if(!ready(producer)||!ready(helper)||!producer->ally||!helper->ally||
            distance(converter->position,w.self.position)>3||distance(producer->position,w.self.position)>3||
            distance(helper->position,w.self.position)>3) return ordinary(w,"cross-no-personal-crew-meeting");
        const Group* group=nearest_safe(w);
        const Altar* altar=nullptr;
        for(int i=0;i<w.altar_count;++i) {
            const auto& candidate=w.altars[i];
            if(candidate.visible&&candidate.route_known&&!candidate.enemy_lock&&
                w.altar_travel(converter->id,candidate.id)>=0&&(!altar||candidate.id<altar->id)) altar=&candidate;
        }
        if(!group||!altar||distance(group->position,altar->position)<=2||
            !safe_for(*group,*producer)||!safe_for(*group,*helper)) return ordinary(w,"cross-two-real-projects-unavailable");
        const int target=w.round-w.round%16+12;
        const int bank_travel=w.altar_travel(converter->id,altar->id);
        const int producer_travel=w.group_travel(producer->id,group->id);
        const int helper_travel=w.group_travel(helper->id,group->id);
        for(const int travel:{bank_travel,producer_travel,helper_travel})
            if(travel<0||w.round+travel>target||target-w.round-travel>8)
                return ordinary(w,"cross-route-or-staging-wait-unavailable");
        if(group->clear_turns<0||target-w.round+group->clear_turns+w.capture_turns>16||
            target-w.round+w.ticks(converter->bag)>16||!campaign_.start(w)) return ordinary(w,"cross-campaign-cap");
        source_=group->id;altar_=altar->id;target_=target;converter_=converter->id;
        if(w.self.pick==1) {auto out=bank(altar_,"cross-adopt-visible-bank-project");out.until=target_;out.strategic=true;return out;}
        return work(w,source_,target_,"cross-adopt-concurrent-source-project");
    }
};
}
