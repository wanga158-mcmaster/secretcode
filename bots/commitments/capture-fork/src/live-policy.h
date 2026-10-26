#pragma once
#include "live-profile.h"
namespace p9::capture_fork {
class Policy {
    live::Campaign campaign_{};
    int first_=-1,second_=-1,target_=-1;
    static live::Directive clearance(int group,const char* why) {
        auto out=live::farm(group,why);out.strategic=true;return out;
    }
    static live::Directive independent(const live::World& w,const char* why="fork-independent") {
        if(!w.self.alive||!w.safe_now) return {live::Task::recover,-1,-1,-1,-1,false,why};
        return {live::Task::independent,-1,-1,-1,-1,false,why};
    }
    static std::array<const live::Group*,2> sources(const live::World& w) {
        std::array<const live::Group*,2> result{};
        for(int i=0;i<w.group_count;++i) {
            const auto& g=w.groups[i];
            if(!g.observed||!g.route_known||g.respawning||g.contested||!g.safe_solo) continue;
            if(!result[0]||g.id<result[0]->id) {result[1]=result[0];result[0]=&g;}
            else if(!result[1]||g.id<result[1]->id) result[1]=&g;
        }
        return result;
    }
public:
    live::Directive step(const live::World& w,live::Arm arm) {
        using namespace live;
        if(campaign_.active) campaign_.tick(w,false);
        if(w.self.channel!=Channel::none||w.capture_active) return {};
        if(!w.self.alive||!w.safe_now||w.extra_opposition||w.self.pick<0||w.self.pick>2) {
            campaign_.finish();return independent(w,"fork-local-feasibility-fallback");
        }
        if(w.self.bag>0) return bank(-1,"fork-convert-actual-own-reward");
        const auto pair=sources(w);
        if(arm==Arm::control) {
            if(!pair[0]) return independent(w);
            return {pair[0]->cleared?Task::capture:Task::farm,pair[0]->id,-1,-1,-1,false,"control-serial-first-group"};
        }
        if(campaign_.active) {
            const auto* first=w.group(first_);const auto* second=w.group(second_);
            const bool current=first&&second&&first->observed&&second->observed;
            campaign_.tick(w,current);
            if(!current||campaign_.expired(w)||w.round>target_+4||first->contested||second->contested) {
                campaign_.finish();return independent(w,"fork-lost-site-or-capped");
            }
            if(w.own_bank_completed&&w.last_bank_round>=campaign_.admitted) {campaign_.finish();return independent(w,"fork-renew");}
            if(w.self.pick==0) {
                if(first->living) return clearance(first_,"fork-clear-first-then-release");
                const auto* holder=w.pick(2);
                if(!ready(holder)||distance(holder->position,first->position)>1) {
                    campaign_.finish();
                    return {Task::capture,first_,-1,-1,-1,false,"fork-failed-handoff-finish-safely"};
                }
                if(second->living) return clearance(second_,"fork-released-clearer-second-source");
                const auto* other_holder=w.pick(1);
                if(!ready(other_holder)||distance(other_holder->position,second->position)>1)
                    return support(other_holder?other_holder->id:-1,second_,"fork-second-handoff-not-yet-observed");
                for(int i=0;i<w.group_count;++i) {
                    const auto& next=w.groups[i];
                    if(next.id!=first_&&next.id!=second_&&next.safe_solo&&next.observed&&next.route_known&&!next.respawning) {
                        campaign_.finish();return farm(next.id,"fork-released-clearer-renewal");
                    }
                }
                return {Task::wait,-1,-1,-1,w.round+1,true,"fork-no-unowned-renewal-source-rescan"};
            }
            const int id=w.self.pick==2?first_:second_;
            const auto* group=w.group(id);
            if(!group||!safe_for(*group,w.self)) {campaign_.finish();return independent(w,"fork-holder-unsafe");}
            if(group->living&&w.self.pick==1) return clearance(id,"fork-second-holder-clears-own-source");
            if(group->living&&w.round>=target_) {campaign_.finish();return independent(w,"fork-missed-clearance");}
            return {Task::capture,id,-1,-1,target_,true,"fork-fixed-distinct-holder-window"};
        }
        if(!pair[0]||!pair[1]||(!pair[0]->living&&!pair[1]->living)||w.round%16>3)
            return independent(w,"fork-no-two-source-staging");
        const int target=w.round-w.round%16+12;
        for(int pick=0;pick<3;++pick) {
            const auto* actor=w.pick(pick);
            if(!ready(actor)||!actor->ally||distance(actor->position,w.self.position)>3)
                return independent(w,"fork-no-visible-crew-meeting");
            const auto* source=pick==1?pair[1]:pair[0];
            const int travel=w.group_travel(actor->id,source->id);
            if(travel<0||w.round+travel>target||target-w.round-travel>8||
                !safe_for(*source,*actor)||target-w.round+w.capture_turns>16)
                return independent(w,"fork-holder-route-or-exposure-unavailable");
            const int clearance=static_cast<int>(std::ceil(source->hp/std::max(1.,actor->strength)))+2;
            if(pick<2&&source->living&&travel+clearance>target-w.round)
                return independent(w,"fork-clearance-cannot-meet-hold-slot");
        }
        if(!campaign_.start(w)) return independent(w,"fork-campaign-cap");
        first_=pair[0]->id;second_=pair[1]->id;target_=target;
        if(w.self.pick==0) return clearance(first_,"fork-adopt-two-source-clearance");
        if(w.self.pick==1&&pair[1]->living) return clearance(second_,"fork-second-holder-clearance");
        return {Task::capture,first_,-1,-1,target_,true,"fork-adopt-first-future-holder"};
    }
};
}
