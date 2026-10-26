#pragma once
#include <array>
#include <algorithm>

// A depot is physical unfinished work, never a currency balance or an allied fact.
namespace p10_depots {
enum class Stock { absent, proposed, verified, consumed, liquidating, renewed, invalid };
enum class Purpose { none, outward_job, loaded_return };
struct Depot {
    int group=-1, monster=-1, observedHp=0, observedRound=-1;
    Purpose reservationPurpose=Purpose::none;
    int liquidationDeadline=0, preparer=-1, finalHolder=-1;
    int preparedRound=-1, consumedRound=-1, interveningJob=-1;
    Stock state=Stock::absent;
};
struct StockObservation {
    int group=-1, monster=-1, round=0, hp=0, strength=0, strikerStrength=0;
    bool personallyVisible=false, allMembersObserved=false, attackingMembersDead=false;
    bool sameLifecycle=false, safeIngress=false, safeEgress=false, enemyPresent=false;
};
inline bool refresh(Depot& d,const StockObservation& o) {
    const bool current=o.group==d.group && o.monster==d.monster &&
        o.personallyVisible && o.allMembersObserved && o.attackingMembersDead &&
        o.sameLifecycle && o.strength==0 && o.hp>0 && o.hp<=o.strikerStrength &&
        o.safeIngress && o.safeEgress && !o.enemyPresent && o.round<=d.liquidationDeadline;
    if(!current) { d.state=Stock::invalid; return false; }
    d.observedHp=o.hp; d.observedRound=o.round; d.state=Stock::verified; return true;
}
struct ResetUse {
    int round=0, job=-1, cooldownBefore=0, cooldownAfter=0, turnsToDependentAction=0;
    bool confirmedKill=false, castAlreadyUsed=false, basicAllowed=false;
    bool unavoidableOrdinaryKill=false, dependentDashLegal=false;
};
inline bool consume(Depot& d,const ResetUse& use) {
    // A verified observation expires at the boundary of this owner turn. The
    // confirmed kill and cooldown requery are separate from the reservation.
    if(d.state!=Stock::verified || d.observedRound!=use.round ||
       use.job<0 || use.job==d.group || use.round<=d.preparedRound ||
       !use.confirmedKill || !use.basicAllowed || use.cooldownBefore<=0 ||
       use.cooldownAfter!=0 || use.unavoidableOrdinaryKill ||
       use.turnsToDependentAction>=use.cooldownBefore || !use.dependentDashLegal)
        return false;
    d.state=Stock::consumed; d.consumedRound=use.round; d.interveningJob=use.job;
    return true;
}
inline bool dash_after_kill(const Depot& d,const ResetUse& use) {
    return d.state==Stock::consumed && !use.castAlreadyUsed && use.dependentDashLegal;
}
struct Receipt {
    int secured=0, personalGold=0, firstBank=10000, renewal=10000;
    bool legal=false, actualBank=false, actualLiquidation=false;
};
struct Script {
    enum Kind { immediate, reserved, separated, liquidation } kind=immediate;
    int preparation=0, jobTravel=0, jobWork=0, dashSaved=0, capturePhases=6;
    int altarTravel=0, channelTicks=1, returnTravel=0, depotTravel=0;
    int depotCapturePhases=6, depotChannelTicks=1, sourceRespawn=50;
    int jobGems=0, depotGems=0, jobGold=0, depotGold=0;
    int prepForegoneGems=0, prepForegoneGold=0, helperLostTurns=0;
    bool complete=true, exclusiveHolder=true, harmlessStock=true;
    bool legalReset=false, ordinaryKillSubstitute=false, enemyTakesStock=false;
};
inline Receipt forecast(const Script& s) {
    Receipt r;
    if(!s.complete || !s.exclusiveHolder || s.enemyTakesStock ||
       (s.kind==Script::reserved && (!s.harmlessStock || !s.legalReset ||
          s.ordinaryKillSubstitute || s.dashSaved<=0))) return r;
    const int saved=s.kind==Script::reserved?s.dashSaved:0;
    const int bank=s.preparation+s.helperLostTurns+s.jobTravel+s.jobWork-saved+
        s.capturePhases+s.altarTravel+s.channelTicks+1;
    const int liquidate=bank+s.depotTravel+s.depotCapturePhases+s.altarTravel+
        s.depotChannelTicks+1;
    r.legal=bank>=0 && liquidate<=96;
    if(!r.legal) return r;
    r.firstBank=bank; r.secured=std::max(0,s.jobGems-s.prepForegoneGems);
    r.personalGold=std::max(0,s.jobGold-s.prepForegoneGold);
    if(liquidate<=96) { r.secured+=s.depotGems; r.personalGold+=s.depotGold; }
    r.renewal=liquidate+s.returnTravel+s.sourceRespawn;
    // Renewal output is counted only if another complete source-bank fits.
    const int nextBank=r.renewal+s.jobWork+s.capturePhases+s.altarTravel+s.channelTicks+1;
    if(nextBank<=96) {r.secured+=s.depotGems; r.personalGold+=s.depotGold;}
    return r;
}
inline bool better(const Receipt&a,const Receipt&b) {
    if(a.legal!=b.legal) return a.legal;
    if(a.secured!=b.secured) return a.secured>b.secured;
    if(a.personalGold!=b.personalGold) return a.personalGold>b.personalGold;
    return a.firstBank<b.firstBank;
}
inline Script::Kind choose(const std::array<Script,4>& scripts,bool active) {
    int winner=0; auto result=forecast(scripts[0]);
    for(int i=1;i<4;++i) {
        if(!active && (scripts[i].kind==Script::reserved||scripts[i].kind==Script::separated))continue;
        const auto r=forecast(scripts[i]); if(better(r,result)){winner=i;result=r;}
    }
    return scripts[winner].kind;
}
// Observed receipts, deliberately separate from forecasts above.
struct Loop {
    enum Stage { cold, first_capture, purchased, manufactured, other_job, used,
                 banked, liquidated, renewed } stage=cold;
    int bank=0, ownGold=0;
    bool capture(int gems,int gold) {if(stage!=cold)return false;bank=gems;ownGold=gold;stage=first_capture;return true;}
    bool buy(int cost){if(stage!=first_capture||cost<=0||ownGold<cost)return false;ownGold-=cost;stage=purchased;return true;}
    bool prepare(){if(stage!=purchased)return false;stage=manufactured;return true;}
    bool work(){if(stage!=manufactured)return false;stage=other_job;return true;}
    bool reset(bool nonredundant){if(stage!=other_job||!nonredundant)return false;stage=used;return true;}
    bool tribute(int receipt){if(stage!=used||receipt<bank)return false;bank=0;stage=banked;return true;}
    bool liquidate(bool observed){if(stage!=banked||!observed)return false;stage=liquidated;return true;}
    bool renew(bool observed){if(stage!=liquidated||!observed)return false;stage=renewed;return true;}
};
}
