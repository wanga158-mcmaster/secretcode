#pragma once
#include "live-profile.h"
namespace p9::twin_deadlines {
class Policy {
    live::Campaign campaign_{};
    int altar_=-1,target_=-1,waited_=0,last_wait_=-1;
    int capture_defense_reports_=0;
    static live::Directive ordinary(const live::World& w,int altar=-1,const char* why="twin-independent") {
        if(!w.self.alive||!w.safe_now) return {live::Task::recover,-1,-1,-1,-1,false,why};
        return {live::Task::independent,-1,-1,altar,-1,false,why};
    }
    static std::array<const live::Altar*,2> sites(const live::World& w) {
        std::array<const live::Altar*,2> out{}; int count=0;
        for(int i=0;i<w.altar_count&&count<2;++i) if(w.altars[i].route_known) out[count++]=&w.altars[i];
        if(out[1]&&out[1]->id<out[0]->id) std::swap(out[0],out[1]); return out;
    }
public:
    static constexpr int capture_defense_turns(const live::World& w) {
        return w.self.type==live::Champion::tavi&&w.score==0?7:0;
    }
    bool report_capture_defense() {
        if(capture_defense_reports_>=4)return false;
        ++capture_defense_reports_;return true;
    }
    live::Directive step(const live::World& w,live::Arm arm) {
        using namespace live;
        if(campaign_.active) campaign_.tick(w,false);
        const auto altars=sites(w);
        const int assigned=w.self.pick>=0&&w.self.pick<2&&altars[w.self.pick]?altars[w.self.pick]->id:-1;
        if(w.self.channel!=Channel::none||w.capture_active) return {};
        if(!w.self.alive||!w.safe_now||w.immediate_bank()) {
            campaign_.finish();return ordinary(w,assigned,"twin-safety-or-terminal");
        }
        if(w.self.pick==2&&w.self.bag==0&&altars[0]) {
            const Group* best=nullptr;
            for(int i=0;i<w.group_count;++i) {
                const auto& g=w.groups[i];
                if(!g.observed||!g.route_known||!g.safe_solo||g.respawning||w.group_travel(w.self.id,g.id)<0) continue;
                if(!best||distance(g.approach,altars[0]->approach)<distance(best->approach,altars[0]->approach)||
                    (distance(g.approach,altars[0]->approach)==distance(best->approach,altars[0]->approach)&&g.id<best->id)) best=&g;
            }
            if(best) return farm(best->id,"independent-source-near-service-approach");
        }
        if(arm==Arm::control&&w.self.pick>=0&&w.self.pick<2&&w.self.bag>0)
            return bank(assigned,"control-bank-when-ready");
        if(arm==Arm::control||w.self.pick<0||w.self.pick>1)
            return ordinary(w,assigned,arm==Arm::control?"control-bank-when-ready":"independent-service-producer");
        if(campaign_.active) {
            const auto* altar=w.altar(altar_);
            const bool current=altar&&altar->visible&&altar->route_known&&!altar->enemy_lock&&!w.extra_opposition;
            campaign_.tick(w,current);
            if((w.own_bank_completed&&w.last_bank_round>=campaign_.admitted)||w.self.bag==0) {
                campaign_.finish();return ordinary(w,-1,"twin-renew-after-own-bank");
            }
            const int travel=altar?w.altar_travel(w.self.id,altar_):-1;
            if(!current||campaign_.expired(w)||travel<0||w.round+travel>target_||w.round>target_) {
                campaign_.finish();return ordinary(w,altar_,"twin-missed-slot-independent-bank");
            }
            if(travel==0&&w.round<target_&&last_wait_!=w.round) {++waited_;last_wait_=w.round;}
            if(waited_>8) {campaign_.finish();return ordinary(w,altar_,"twin-wait-cap-bank");}
            auto out=bank(altar_,"twin-fixed-slot-own-channel");out.until=target_;out.strategic=true;return out;
        }
        if(w.self.bag<=0||w.round%16>3||w.extra_opposition||!altars[0]||!altars[1]) return ordinary(w,assigned);
        const auto* peer=w.pick(1-w.self.pick);
        if(!ready(peer)||!peer->ally||peer->bag<=0||distance(peer->position,w.self.position)>3)
            return ordinary(w,assigned,"twin-no-personal-loaded-meeting");
        const int target=w.round-w.round%16+12;
        for(int role=0;role<2;++role) {
            const auto* actor=role==w.self.pick?&w.self:peer;
            const auto* altar=altars[role];
            const int travel=w.altar_travel(actor->id,altar->id);
            if(!altar->visible||altar->enemy_lock||travel<0||w.round+travel>target||
                target-w.round-travel>8||target-w.round+w.ticks(actor->bag)>16||
                (w.max_rounds>0&&target+w.ticks(actor->bag)>w.max_rounds))
                return ordinary(w,assigned,"twin-route-or-wait-unavailable");
        }
        if(!campaign_.start(w)) return ordinary(w,assigned,"twin-campaign-cap");
        altar_=assigned;target_=target;waited_=0;last_wait_=-1;
        auto out=bank(altar_,"twin-adopt-visible-clocked-meeting");out.until=target_;out.strategic=true;return out;
    }
};
}
