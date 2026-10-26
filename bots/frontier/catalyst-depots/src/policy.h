#pragma once
#include "model.h"
#include "../../common/src/contract.h"

namespace p10_depots {
inline const p10::Profile profile{"p10-catalyst-depots",{p10::Champion::vendetta,p10::Champion::marcella,p10::Champion::tavi,p10::Champion::varg,p10::Champion::willow}};
struct LegacyPolicy {
    std::array<Depot,2> stock{};
    int firstIncome=-1,lastJob=-1,dependentJob=-1,liquidation=-1,lastPurchase=-1;
    int confirmedUse=-1,bankAfterUse=-1,lastSeenRound=-1,pendingRound=-1,pendingMonster=-1;
    double pendingHp=0;
    bool bootstrapped=false;
    p10::Directive step(const p10::World&w,p10::Arm arm){
        using namespace p10;
        if(pendingRound>=0&&w.round>pendingRound){
            bool receipt=false;
            for(const auto&r:w.receipts)if(r.round==pendingRound&&r.group==liquidation&&r.actor==w.self.id&&
                r.kind==p10::Receipt::Kind::attack&&r.amount>=pendingHp)receipt=true;
            const auto*g=w.group(liquidation);bool observedDead=false;
            if(g&&g->observed&&g->complete)for(const auto&m:g->monsters)if(m.id==pendingMonster)observedDead=!m.alive;
            if(receipt&&observedDead){confirmedUse=pendingRound;for(auto&d:stock)if(d.group==liquidation){d.state=Stock::consumed;d.consumedRound=pendingRound;}}
            else {for(auto&d:stock)if(d.group==liquidation)d.state=Stock::invalid;liquidation=-1;}
            pendingRound=-1;
        }
        if(w.own_payout){bootstrapped=true;firstIncome=w.last_payout_group;}
        if(w.own_purchase)lastPurchase=w.round;
        if(w.own_bank_completed&&confirmedUse>=0)bankAfterUse=w.round;
        if(w.self.channel!=Channel::none||w.capture_active)return wait("finish witnessed custody/service");
        if(!w.safe_now)return {Task::recover};
        // Real individual capital, after observed first income only. A field
        // preparer cannot assume the consumer's income can pay this bill.
        if(bootstrapped&&lastPurchase<0&&w.own_gold>=w.shard_cost&&
           (w.self.at_spawn||w.self.type==Champion::tavi))return buy(Item::strength,1,"first payout funds personal preparation threshold");
        if(arm==Arm::control)return ordinary(w); // same reset-aware executor
        if(bankAfterUse>=0&&liquidation>=0){
            const auto*g=w.group(liquidation);
            if(g&&usable(*g)&&!g->respawning){auto d=farm(g->id,"pay deferred depot capture and renewal");d.template_id=3;return d;}
            liquidation=-1;
        }
        if(w.self.type==Champion::vendetta){
            const Group*job=nullptr;
            for(const auto&g:w.groups){
                bool reserved=false;for(const auto&d:stock)if(d.group==g.id&&d.state!=Stock::invalid&&d.state!=Stock::renewed)reserved=true;
                if(!reserved&&usable(g)&&g.safe_solo&&g.living&&g.gems>0&&g.travel>=0&&
                    (!job||g.travel<job->travel))job=&g;
            }
            if(job&&job->id!=liquidation){lastJob=job->id;dependentJob=job->id;}
            for(const auto&g:w.groups){
                if(!usable(g)||!g.living||g.monsters.empty())continue;
                bool allDead=true;int cluster=-1;
                for(const auto&m:g.monsters){
                    if(!m.observed){allDead=false;break;}
                    if(m.alive&&m.strength>0)allDead=false;
                    if(m.alive&&m.strength==0&&m.hp>0&&m.hp<=w.self.strength)cluster=m.id;
                }
                if(!allDead||cluster<0)continue;
                Depot*d=nullptr;for(auto&v:stock)if(v.group==g.id)d=&v;
                if(!d)for(auto&v:stock)if(v.state==Stock::absent||v.state==Stock::invalid||v.state==Stock::renewed){d=&v;*d={};d->group=g.id;d->monster=cluster;d->preparedRound=w.round;d->state=Stock::proposed;d->liquidationDeadline=w.round+32;break;}
                if(!d)continue;
                const auto&member=*std::find_if(g.monsters.begin(),g.monsters.end(),[&](const auto&m){return m.id==cluster;});
                const bool continuous=d->observedRound<0||d->observedRound==w.round-1||d->observedRound==w.round;
                // A fresh 120-HP cluster at high Strength cannot resurrect an
                // old record after a hidden capture/respawn interval.
                StockObservation o{g.id,cluster,w.round,int(member.hp),int(member.strength),int(w.self.strength),true,true,allDead,continuous,g.route_known,w.safe_now,!w.enemies.empty()};
                if(!refresh(*d,o))continue;
                if(d->preparedRound==w.round||lastJob<0||lastJob==g.id||dependentJob<0)continue;
                const auto*next=w.group(dependentJob);if(!next||!next->route_known||next->to_bank<0)continue;
                const int natural=w.self.cooldown[1];
                bool ordinaryKill=false;
                for(const auto&m:next->monsters)if(m.observed&&m.alive&&m.hp<=w.self.strength&&distance(w.self.position,m.position)<=w.self.range)ordinaryKill=true;
                if(natural<=0||ordinaryKill||!w.self.can_attack||distance(w.self.position,member.position)>w.self.range)continue;
                std::array<Script,4> programs{};
                for(int i=0;i<4;++i){auto&s=programs[i];s.kind=Script::Kind(i);s.jobTravel=next->travel;s.jobWork=std::max(0,next->clear_turns);s.altarTravel=next->to_bank;s.jobGems=int(next->gems);s.jobGold=int(next->gold);s.depotGems=int(g.gems);s.depotGold=int(g.gold);s.capturePhases=w.capture_turns+1;s.depotCapturePhases=w.capture_turns+1;s.channelTicks=w.ticks(next->gems+w.self.bag);s.depotChannelTicks=w.ticks(g.gems);s.returnTravel=std::max(0,next->to_spawn);s.sourceRespawn=g.respawn;s.complete=i!=2;}
                // Immediate capture consumes the current stock before the job;
                // the reserved branch pays for its later liquidation visit.
                programs[0].jobTravel+=w.capture_turns+1+std::max(0,g.to_bank)+w.ticks(g.gems)+1;
                programs[1].dashSaved=std::min(2,natural-1);programs[1].legalReset=programs[1].dashSaved>0;
                programs[1].depotTravel=g.travel;programs[1].harmlessStock=true;
                programs[3].complete=false;
                if(choose(programs,true)!=Script::reserved)continue;
                Directive out;out.task=Task::attack;out.group=g.id;out.monster=cluster;out.reserve_reset=true;out.cast_slot=1;out.destination=next->approach;out.reason="consume personally reverified cross-job stock then use one refreshed dash";out.template_id=1;out.valid_round=w.round;
                liquidation=g.id;pendingRound=w.round;pendingMonster=cluster;pendingHp=member.hp;return out;
            }
            if(w.self.bag>0)return bank("secure productive output before depot liquidation");
            if(job)return farm(job->id,"named dependent production job");
        }
        if((w.self.type==Champion::marcella||w.self.type==Champion::tavi)&&bootstrapped){
            const Actor*consumer=nullptr;for(const auto&a:w.allies)if(a.visible&&a.alive&&a.type==Champion::vendetta)consumer=&a;
            if(consumer&&consumer->cooldown[1]>1){
                for(const auto&g:w.groups)if(usable(g)&&g.safe_solo&&g.living&&g.travel<=2&&g.monsters.size()<=8){
                    int reserveCount=0;const Monster*attacker=nullptr,*prepare=nullptr;
                    for(const auto&m:g.monsters)if(m.alive){
                        if(m.strength>0){if(!attacker||m.strength>attacker->strength)attacker=&m;}
                        else {++reserveCount;if(m.hp>consumer->strength&&m.hp-w.self.strength>0)prepare=&m;}
                    }
                    if(reserveCount<=0||reserveCount>3)continue;
                    // A visible consumer must have another complete source and
                    // a cooldown-sensitive route. No self-financed speculative
                    // warehouse, unsupported private proof or permanent guard.
                    const Group*next=nullptr;for(const auto&j:w.groups)if(j.id!=g.id&&usable(j)&&j.safe_solo&&j.living&&j.to_bank>=0&&j.travel>=0&&j.travel<consumer->cooldown[1]){next=&j;break;}
                    if(!next)continue;
                    if(attacker){Directive d;d.task=Task::attack;d.group=g.id;d.monster=attacker->id;d.reserve_reset=true;d.reason="non-splash defanging for witnessed consumer route";return d;}
                    if(prepare){Directive d;d.task=Task::attack;d.group=g.id;d.monster=prepare->id;d.reserve_reset=true;d.reason="prepare surviving one-hit physical stock without killing it";return d;}
                    return farm(next->id,"release preparer to independent income after manufacturing stock");
                }
            }
        }
        return ordinary(w);
    }
};
}
#include "wave.h"
