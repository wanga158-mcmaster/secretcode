#pragma once
#include <array>
#include <algorithm>

namespace p10_exposure {
struct ReactionEvent {int attacker=-1,target=-1,phase=0;double damage=0;int retaliationTarget=-1;};
struct MillStep {int directTarget=-1,castChoice=-1,healRecipient=-1,holdOwner=-1;};
struct Attacker {
    int id=-1;double hp=0,damage=0;int range=0,target=-1;
    bool observed=false,inRange=false,qualifying=true,stunned=false;
    bool renReachable=true,stellarReachable=true,willowReachable=true;
};
struct Crew {
    int id=-1;double hp=0,maxHp=0,strength=0,focus=0;bool visible=false,ready=false;
};
struct Mill {
    std::array<Attacker,8> attackers{};int count=0;
    Crew ren{},willow{},stellar{};
    bool exitLegal=false,exclusiveAggro=false,holderSafe=false,healArrival=true,areaHitSet=false;
    int travel=0,altar=0,capture=5,channel=1,returnTravel=0,respawn=40;
    int gems=0,gold=0,helperReleasedWork=0;
    double reserve=80;int round=0;
};
enum class Template { finish_now,control_now,direct_elsewhere,area_first,depart };
struct Program {
    Template kind=Template::depart;
    std::array<MillStep,4> steps{};std::array<ReactionEvent,32> hits{};int hitCount=0;
    int secured=0,gold=0,bank=10000,renewal=10000,clear=10000;
    double endingHp=0;bool legal=false;int naturalFinishes=0;
};
inline int strongest(const Mill&m,const std::array<double,8>&hp){
    int target=-1;for(int i=0;i<m.count;++i)if(hp[i]>0&&
      (target<0||m.attackers[i].damage>m.attackers[target].damage||
       (m.attackers[i].damage==m.attackers[target].damage&&hp[i]<hp[target])))target=i;
    return target;
}
inline Program evaluate(const Mill&m,Template kind){
    Program p;p.kind=kind;p.endingHp=m.ren.hp;
    if(kind==Template::depart){p.legal=m.exitLegal;return p;}
    if(m.count<=0||m.count>8||!m.ren.visible||!m.exitLegal||!m.holderSafe)return p;
    std::array<double,8> hp{};for(int i=0;i<m.count;++i){if(!m.attackers[i].observed)return p;hp[i]=m.attackers[i].hp;}
    double health=m.ren.hp;bool renCast=m.ren.ready,heal=m.willow.visible&&m.willow.ready&&m.healArrival;
    bool stellarCast=m.stellar.visible&&m.stellar.ready;
    const double reaction=.2*m.ren.focus;int hold=m.ren.id;
    for(int phase=0;phase<4;++phase){
        const int dangerous=strongest(m,hp);if(dangerous<0){p.clear=phase;break;}
        int target=dangerous;
        int weak=-1;for(int i=0;i<m.count;++i)if(hp[i]>0&&hp[i]<=reaction&&
            m.attackers[i].damage>0&&m.attackers[i].target==m.ren.id&&m.attackers[i].qualifying&&
            m.attackers[i].inRange&&!m.attackers[i].stunned&&
            (weak<0||m.attackers[i].damage<m.attackers[weak].damage))weak=i;
        if(kind==Template::direct_elsewhere&&phase==0){
            if(weak<0||!m.exclusiveAggro)return p;
            target=-1;for(int i=0;i<m.count;++i)if(i!=weak&&hp[i]>0&&
                (target<0||m.attackers[i].damage>m.attackers[target].damage))target=i;
            if(target<0)return p; // Never idle just to manufacture injury.
        }else if(kind==Template::finish_now&&phase==0&&weak>=0)target=weak;
        auto&step=p.steps[phase];step.directTarget=m.attackers[target].id;step.holdOwner=hold;
        bool stunned=false;
        if(kind==Template::control_now&&renCast){hp[dangerous]-=1.25*m.ren.focus;step.castChoice=0;stunned=true;renCast=false;}
        if(kind==Template::area_first&&stellarCast){
            if(!m.areaHitSet)return p;
            // The caller supplies a certified common area hit set. No inferred
            // landing damage for Ren's dash and no unobserved collateral.
            for(int i=0;i<m.count;++i)if(hp[i]>0)hp[i]-=1.4*m.stellar.focus;
            step.castChoice=2;stellarCast=false;
        }
        if(hp[target]>0&&m.attackers[target].renReachable)hp[target]-=m.ren.strength;
        if(m.stellar.visible&&hp[target]>0&&m.attackers[target].stellarReachable)hp[target]-=m.stellar.strength;
        if(m.willow.visible&&hp[target]>0&&m.attackers[target].willowReachable)hp[target]-=m.willow.strength;
        // Healing occurs in its observed service slot before the next camp
        // phase, and counts only actual missing health. No service after loss
        // of contact or an assumed healer arrival.
        if(heal&&health<m.ren.maxHp){health=std::min(m.ren.maxHp,health+3*m.willow.focus);step.healRecipient=m.ren.id;heal=false;}
        double incoming=0;
        for(int i=0;i<m.count;++i)if(hp[i]>0&&m.attackers[i].inRange&&
            m.attackers[i].target==m.ren.id&&!(stunned&&i==dangerous)&&!m.attackers[i].stunned)incoming+=m.attackers[i].damage;
        if(health-incoming<=m.reserve)return Program{};
        for(int i=0;i<m.count;++i)if(hp[i]>0&&m.attackers[i].inRange&&
            m.attackers[i].target==m.ren.id&&!(stunned&&i==dangerous)&&!m.attackers[i].stunned){
            const auto&a=m.attackers[i];health-=a.damage;
            if(a.damage>0&&a.qualifying){
                p.hits[p.hitCount++]={a.id,m.ren.id,phase,a.damage,a.id};
                const bool finish=hp[i]<=reaction;hp[i]-=reaction;
                if(finish)++p.naturalFinishes;
            }
        }
        if(strongest(m,hp)<0){p.clear=phase+1;break;}
    }
    if(p.clear==10000)return Program{};
    p.bank=m.travel+p.clear+m.capture+1+m.altar+m.channel+1;
    p.renewal=p.bank+m.returnTravel;
    if(p.bank>160||p.renewal>160)return Program{};
    p.legal=true;p.endingHp=health;p.secured=m.gems;p.gold=m.gold;
    // Released service has value only through an explicitly completed bank.
    p.secured+=m.helperReleasedWork;
    const int nextBank=std::max(p.renewal,p.bank+m.respawn)+p.clear+m.capture+1+m.altar+m.channel+1;
    if(nextBank<=160){p.secured+=m.gems;p.gold+=m.gold;}
    return p;
}
inline bool better(const Program&a,const Program&b){
    if(a.legal!=b.legal)return a.legal;
    if(a.secured!=b.secured)return a.secured>b.secured;
    if(a.gold!=b.gold)return a.gold>b.gold;
    if(a.bank!=b.bank)return a.bank<b.bank;
    return a.endingHp>b.endingHp;
}
inline Program choose(const Mill&m,bool active){
    // Cheap screening chooses the strongest immediately executable independent
    // finish/control template. Only baseline and joint get full fresh four-
    // phase trajectories; the offline catalogue is not a live simulation cap.
    Template independent=Template::finish_now;int aoeKills=0;double burstThreat=0;
    for(int i=0;i<m.count&&i<8;++i)if(m.attackers[i].hp>0){
        if(m.attackers[i].hp<=1.4*m.stellar.focus)++aoeKills;
        if(m.attackers[i].inRange&&m.attackers[i].target==m.ren.id)burstThreat+=m.attackers[i].damage;
    }
    if(m.stellar.visible&&m.stellar.ready&&m.areaHitSet&&aoeKills>0)independent=Template::area_first;
    else if(m.ren.ready&&(burstThreat>=m.ren.hp-m.reserve||burstThreat>=m.ren.strength))independent=Template::control_now;
    Program best=evaluate(m,independent);
    if(active){auto joint=evaluate(m,Template::direct_elsewhere);if(better(joint,best))best=joint;}
    return best.legal?best:evaluate(m,Template::depart);
}
struct RenewalTrace {
    int triggers=0,gold=0,bag=0,score=0;bool bought=false,renewed=false;
    bool hit(double damage,bool qualified){if(damage<=0||!qualified)return false;++triggers;return true;}
    void payout(int gems,int ownGold){bag+=gems;gold+=ownGold;}
    bool buy(int cost){if(cost<=0||gold<cost)return false;gold-=cost;bought=true;return true;}
    bool bank(int receipt){if(receipt<=0||receipt!=bag)return false;score+=receipt;bag=0;return true;}
    bool renew(bool observed){renewed=observed&&bought&&score>0&&triggers>0;return renewed;}
};
}
