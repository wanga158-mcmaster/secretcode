#pragma once
#include "model.h"
#include "../../common/src/contract.h"

namespace p10_initiative {
inline const p10::Profile profile{"p10-initiative-works",{p10::Champion::marcella,p10::Champion::vendetta,p10::Champion::willow,p10::Champion::tavi,p10::Champion::vesper}};
struct LegacyPolicy {
    Commitment commitment{};int lastRound=-1,selectedHolder=-1,source=-1;
    bool firstIncome=false,bought=false;
    static bool on_spawn(const p10::Actor&a,const p10::Group&g){for(auto tile:g.spawns)if(tile==a.position)return true;return false;}
    p10::Directive step(const p10::World&w,p10::Arm arm){
        using namespace p10;
        if(w.own_payout)firstIncome=true;
        if(w.own_purchase)bought=true;
        // Never change configuration during a live channel, even to a value
        // that would have been useful had it been issued the previous round.
        if(w.self.channel!=Channel::none||w.capture_active)return wait("retain channel/capture and previously issued order");
        if(!w.safe_now)return {Task::recover};
        const bool active=arm==Arm::active;
        if(lastRound<0){commitment.current=w.self.priority;commitment.next=w.self.priority;}
        lastRound=w.round;
        if(firstIncome&&!bought&&w.own_gold>=w.shard_cost&&(w.self.at_spawn||w.self.type==Champion::tavi))
            return buy(w.self.type==Champion::willow?Item::focus:Item::strength,1,"first income buys actual personal renewal gear");
        const Group*g=nullptr;
        for(const auto&candidate:w.groups)if(usable(candidate)&&candidate.travel>=0&&candidate.to_bank>=0&&candidate.travel<=8&&
            (!g||candidate.travel<g->travel))g=&candidate;
        Dispatch category=Dispatch::circulation;bool safety=false,prerequisite=false;
        const Actor*protect=nullptr;
        if(w.self.type==Champion::willow)for(const auto&a:w.allies)if(a.visible&&a.alive&&a.hp<a.max_hp*.4&&distance(a.position,w.self.position)<=4){
            bool danger=false;for(const auto&e:w.enemies)if(e.visible&&e.alive&&distance(a.position,e.position)<=e.range+1)danger=true;
            if(danger){protect=&a;category=Dispatch::protection;safety=true;prerequisite=true;break;}
        }
        bool atAltar=false;for(const auto&a:w.altars)if(a.visible&&!a.enemy_lock&&distance(w.self.position,a.position)<=1)atAltar=true;
        if(!protect&&w.self.bag>0){category=atAltar?Dispatch::completion:Dispatch::loaded;prerequisite=atAltar;}
        else if(!protect&&g&&g->living&&g->clear_turns<=2)category=Dispatch::source;
        p10::Directive result;
        if(w.self.bag>0)result=bank("actual bag and own service ticks determine conversion slot");
        else result=ordinary(w);
        if(protect&&w.self.abilities[0].ready){result.task=Task::heal;result.actor=protect->id;result.cast_slot=0;result.reason="personally observed prerequisite protection has highest dispatch";}
        if(g&&w.self.bag==0&&!protect){
            SlotProgram base;base.now=w.round;base.ownIsTeamB=w.own_is_team_b;base.ourTeamStarts=w.our_team_starts;
            std::array<const Actor*,5> actors{};
            actors[base.count++]=&w.self;
            for(const auto&a:w.allies)if(a.visible&&a.alive&&base.count<5&&distance(a.position,g->position)<=8)actors[base.count++]=&a;
            int original=-1,lead=-1;
            for(int i=0;i<base.count;++i){const auto*a=actors[i];
                base.workers[i]={a->id,std::clamp(a->pick,0,15),a->priority,a->priority,w.round,int(a->bag),distance(a->position,g->position),w.ticks(a->bag+g->gems),true,a->channel!=Channel::none,a->hp>g->incoming+60};
                if(a->type==Champion::marcella)original=i;
                if(a->type==Champion::vendetta)lead=i;
            }
            if(original<0)original=0;if(lead<0)lead=original;
            base.originalHolder=original;base.prerequisites=base.count>=2&&g->observed&&g->complete;
            base.secured=int(g->gems);for(int i=0;i<base.count;++i)base.secured+=int(actors[i]->bag);
            base.personalGold=int(g->gold);
            auto program=[&](int holder,bool reorder){
                SlotProgram p=base;p.holder=holder;
                for(int i=0;i<p.count;++i){
                    auto cls=actors[i]->bag>0?Dispatch::loaded:Dispatch::circulation;
                    if(i==lead||i==holder)cls=Dispatch::source;
                    if(actors[i]->type==Champion::willow&&protect){cls=Dispatch::protection;}
                    p.workers[i].next=priority(cls,p.workers[i].role);
                    if(actors[i]->channel!=Channel::none)p.workers[i].next=p.workers[i].current;
                }
                p.eventsUsed=8;
                p.events[0]={lead,w.round+std::max(0,g->clear_turns),0,-1,-1,false,true,{}};
                p.events[1]={holder,w.round,std::max(0,p.workers[holder].travel-1),0,-1,false,true,{}};
                p.events[2]={holder,w.round,0,1,-1,true,true,{}}; // capture starts next round-start phase
                p.events[3]={holder,w.round,w.capture_turns,2,-1,true,true,{}};
                p.events[4]={holder,w.round,g->to_bank,3,-1,false,true,{}};
                p.events[5]={holder,w.round,p.workers[holder].service,4,-1,false,true,{}};
                p.events[6]={lead,w.round,0,1,-1,false,true,{}}; // released productive worker
                p.events[7]={lead,w.round,std::max(1,g->travel),5,6,false,true,{}};
                p.bankEvent=5;p.renewEvent=7;
                // Existing bags stay personal. A reassigned holder is allowed
                // only if its service does not delay another loaded collector.
                p.displacedUsefulTurns=holder!=original&&actors[holder]->bag>0?1:0;
                solve(p,reorder);return p;
            };
            auto chosen=program(original,false);
            if(active&&base.prerequisites){
                // At most two complete fresh programmes: fixed original and
                // committed best static holder, rather than all actor tours.
                int candidate=original;
                for(int i=0;i<base.count;++i)if(base.workers[i].safe&&!base.workers[i].channelActive&&
                    base.workers[i].travel+base.workers[i].service<base.workers[candidate].travel+base.workers[candidate].service)candidate=i;
                auto committed=program(candidate,true);if(better(committed,chosen))chosen=committed;
            }
            if(chosen.legal){
                selectedHolder=base.workers[chosen.holder].id;source=g->id;
                const Actor*actual=actors[chosen.holder];
                if(w.self.id==selectedHolder){result=farm(g->id,"phase/slot critical path assigns this future holder");category=g->living?Dispatch::source:Dispatch::loaded;}
                else if(g->cleared&&actual->visible&&actual->alive&&on_spawn(*actual,*g)&&actual->hp>60){
                    result.task=Task::release;result.group=g->id;result.destination=w.spawn;result.reason="visible real holder releases independent worker under actual order";category=Dispatch::circulation;
                }
                result.expected_min=chosen.events[chosen.bankEvent].at.round;result.expected_max=result.expected_min+1;
                result.template_id=chosen.holder==original?0:1;
            }
        }
        if(active){
            commitment.set(w.round,std::clamp(w.self.pick,0,15),category,false,prerequisite||int(category)<3,safety);
            // Executor issues this before starting the requested channel. The
            // value applies NEXT round, never to the current source/camp phase.
            result.priority=commitment.next;
        }
        result.valid_round=w.round;return result;
    }
};
}
#include "wave.h"
