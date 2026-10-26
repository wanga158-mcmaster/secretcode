#include "../common/sortie-model.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <tuple>
using namespace plan3_sortie;
namespace {
unsigned checks=0;
void require(bool value,unsigned line) {
    ++checks;if(!value){std::fprintf(stderr,"sortie failure line%u check%u\n",line,checks);std::abort();}
}
#define CHECK(...) require(bool(__VA_ARGS__),__LINE__)
Input base() {
    Input in;in.round=100;in.horizon=128;in.memberCount=3;
    for(unsigned i=0;i<3;++i)in.members[i]={int(30+i),1430,20,true};
    in.health=in.maximumHealth=750;in.strength=135;in.gems=66;
    in.outward={3,0,true,true};in.escape={2,1,true,true};in.returnToCamp={4,0,true,true};
    in.capture={2,0,true,true};in.bank={3,0,true,true};
    in.recallTicks=5;in.spawnHealing=.1;in.captureTicks=5;in.tributeTicks=4;
    in.actorKnown=in.memberEvidenceKnown=in.stationarySupported=in.safeRecall=in.quietNoReset=true;
    return in;
}
void failed(const Input& in,State state,Reason reason) {
    const auto result=evaluate(in);CHECK(result.state==state);CHECK(result.reason==reason);CHECK(!result.schedule);
}
void phase_checks(const Input& in,const Schedule& s) {
    CHECK(s.sortieCount>0 && s.sortieCount<=maximum_sorties && s.attackCount<=maximum_attacks);
    unsigned attacks=0;double damage=0;int before=0;
    for(unsigned k=0;k<s.sortieCount;++k) {
        const auto& p=s.phases[k];CHECK(p.beganAt==before);CHECK(p.combatStartedAt>=p.beganAt);
        CHECK(p.combatEndedAt-p.combatStartedAt==int(in.tavi.enabled?p.turnCount:p.attacks));
        unsigned subtotal=0;for(unsigned i=0;i<in.memberCount;++i)subtotal+=p.attacksByMember[i];
        CHECK(subtotal==p.attacks);attacks+=subtotal;
        damage+=p.entryLoss+p.combatLoss+p.escapeLoss+p.returnLoss;
        CHECK(p.combatEndHealth>=in.maximumHealth*.25);
        CHECK(p.escapeEndHealth>=in.maximumHealth*.25 && p.returnHealth>=in.maximumHealth*.25);
        if(p.recovered) {
            CHECK(k+1<s.sortieCount);CHECK(p.escapeEndedAt-p.combatEndedAt==in.escape.turns);
            CHECK(p.recallEndedAt-p.escapeEndedAt==std::max(1,in.recallTicks));
            CHECK(p.healingEndedAt>=p.recallEndedAt);
            CHECK(p.endedAt-p.healingEndedAt==in.returnToCamp.turns);
        } else {CHECK(k+1==s.sortieCount);for(unsigned i=0;i<in.memberCount;++i)CHECK(p.remainingHealth[i]==0);}
        before=p.endedAt;
    }
    CHECK(attacks==s.attackCount && damage==s.healthLoss);
    CHECK(s.tau==before+in.capture.turns+in.captureTicks+1+in.bank.turns+in.tributeTicks);
    CHECK(s.tau<=in.horizon && s.minHealth>=in.maximumHealth*.25);
    CHECK(s.deltaS==in.gems && s.terminalBag==0 && s.conversion.gems==in.gems && s.conversion.completedAt==s.tau);
}
// Independent one-basic-attack-at-a-time reference. These test inputs already
// satisfy the public numeric/evidence domain; no batching/certificate is used.
Result sequential(const Input& in) {
    std::array<double,maximum_members> hp{};double total=0;
    for(unsigned i=0;i<in.memberCount;++i){hp[i]=in.members[i].health;total+=hp[i];}
    if(total<=0)return stop(State::rejected,Reason::no_progress);
    const double damage=in.strength*in.campDamage,healRate=in.spawnHealing*in.maximumHealth,reserve=in.maximumHealth*.25;
    if(!scalar(damage) || damage<=0 || !scalar(healRate) || healRate<=0)return stop(State::invalid,Reason::numeric);
    double health=in.health;int elapsed=0;Schedule out;out.minHealth=health;
    auto advance=[&](int n){elapsed+=n;return elapsed<=in.horizon;};
    auto victim=[&]{int chosen=-1;for(unsigned i=0;i<in.memberCount;++i)if(hp[i]>0 && (chosen<0 || in.members[i].key<in.members[unsigned(chosen)].key))chosen=int(i);return chosen;};
    auto incoming=[&]{double sum=0;for(unsigned i=0;i<in.memberCount;++i)if(hp[i]>0)sum+=in.members[i].incoming;return sum;};
    auto charge=[&](double loss){if(!scalar(loss))return false;health-=loss;out.healthLoss+=loss;out.minHealth=std::min(out.minHealth,health);return std::isfinite(health) && health>0 && health>=reserve;};
    for(unsigned k=0;k<maximum_sorties;++k) {
        auto& p=out.phases[k];++out.sortieCount;p.beganAt=elapsed;p.startHealth=health;
        if(k==0){p.entryLoss=incoming()*in.mitigation*in.outward.exposureTurns;if(!charge(p.entryLoss))return stop(State::rejected,Reason::unsafe_entry);if(!advance(in.outward.turns))return stop(State::rejected,Reason::deadline);}
        p.entryEndHealth=health;p.combatStartedAt=elapsed;
        while(victim()>=0) {
            if(out.attackCount>=maximum_attacks)return stop(State::rejected,Reason::attack_bound);
            const unsigned at=unsigned(victim());const double after=std::max(0.0,hp[at]-damage);
            if(after==hp[at])return stop(State::rejected,Reason::no_progress);
            const double loss=incoming()*in.mitigation;double surviving=0;
            for(unsigned i=0;i<in.memberCount;++i)if(hp[i]>0 && (i!=at || after>0))surviving+=in.members[i].incoming;
            const double escape=surviving*in.mitigation*in.escape.exposureTurns;
            if(!scalar(loss) || !scalar(escape))return stop(State::invalid,Reason::numeric);
            if(health-loss-escape<reserve)break;
            if(!charge(loss))return stop(State::rejected,Reason::unsafe_attack);
            if(!advance(1))return stop(State::rejected,Reason::deadline);
            p.combatLoss+=loss;hp[at]=after;++p.attacksByMember[at];++p.attacks;++out.attackCount;
        }
        p.remainingHealth=hp;p.combatEndHealth=health;p.combatEndedAt=elapsed;
        p.escapeEndHealth=p.returnHealth=health;p.escapeEndedAt=p.recallEndedAt=p.healingEndedAt=p.endedAt=elapsed;
        if(victim()<0) {
            if(!advance(in.capture.turns) || !advance(in.captureTicks+1) || !advance(in.bank.turns) || !advance(in.tributeTicks))return stop(State::rejected,Reason::deadline);
            out.tau=elapsed;out.deltaS=in.gems;out.terminalHealth=health;out.conversion={elapsed,in.gems};return {State::admitted,Reason::none,out};
        }
        if(!p.attacks)return stop(State::rejected,Reason::no_progress);
        if(k+1==maximum_sorties)return stop(State::rejected,Reason::sortie_bound);
        p.recovered=true;p.escapeLoss=incoming()*in.mitigation*in.escape.exposureTurns;
        if(!charge(p.escapeLoss))return stop(State::rejected,Reason::unsafe_escape);
        if(!advance(in.escape.turns))return stop(State::rejected,Reason::deadline);p.escapeEndHealth=health;p.escapeEndedAt=elapsed;
        if(!advance(std::max(1,in.recallTicks)))return stop(State::rejected,Reason::deadline);p.recallEndedAt=elapsed;
        const double heal=health<in.maximumHealth?std::max(1.0,std::ceil((in.maximumHealth-health)/healRate)):0;
        if(!std::isfinite(heal) || heal>maximum_turns)return stop(State::invalid,Reason::numeric);
        if(!advance(int(heal)))return stop(State::rejected,Reason::deadline);health=in.maximumHealth;p.healingEndedAt=elapsed;
        p.returnLoss=incoming()*in.mitigation*in.returnToCamp.exposureTurns;
        if(!charge(p.returnLoss))return stop(State::rejected,Reason::unsafe_return);
        if(!advance(in.returnToCamp.turns))return stop(State::rejected,Reason::deadline);p.returnHealth=health;p.endedAt=elapsed;
    }
    return stop(State::rejected,Reason::sortie_bound);
}
unsigned oracleCases=0,oracleEarlyRejections=0;
void compare_sequential(const Input& in) {
    ++oracleCases;const auto expected=sequential(in),actual=evaluate(in);
    CHECK(expected.state==actual.state && bool(expected.schedule)==bool(actual.schedule));
    // The necessary bound intentionally reports an earlier, explicit rejection
    // reason; admission and every admitted schedule field remain identical.
    if(actual.reason==Reason::combat_budget_bound){CHECK(expected.state==State::rejected && !expected.schedule);++oracleEarlyRejections;}
    else CHECK(expected.reason==actual.reason);
    if(!expected.schedule)return;const auto& a=*expected.schedule;const auto& b=*actual.schedule;
    CHECK(actual.work.fastAttacks+actual.work.scalarAttacks==b.attackCount && actual.work.phase==EvaluationPhase::publication);
    CHECK(std::tie(a.sortieCount,a.attackCount,a.tau,a.deltaS,a.terminalBag,a.healthLoss,a.minHealth,a.terminalHealth,a.conversion.completedAt,a.conversion.gems)==std::tie(b.sortieCount,b.attackCount,b.tau,b.deltaS,b.terminalBag,b.healthLoss,b.minHealth,b.terminalHealth,b.conversion.completedAt,b.conversion.gems));
    for(unsigned i=0;i<maximum_sorties;++i) {
        const auto& x=a.phases[i];const auto& y=b.phases[i];CHECK(x.attacksByMember==y.attacksByMember && x.remainingHealth==y.remainingHealth);
        CHECK(std::tie(x.attacks,x.startHealth,x.entryEndHealth,x.combatEndHealth,x.escapeEndHealth,x.returnHealth,x.entryLoss,x.combatLoss,x.escapeLoss,x.returnLoss,x.beganAt,x.combatStartedAt,x.combatEndedAt,x.escapeEndedAt,x.recallEndedAt,x.healingEndedAt,x.endedAt,x.recovered)==std::tie(y.attacks,y.startHealth,y.entryEndHealth,y.combatEndHealth,y.escapeEndHealth,y.returnHealth,y.entryLoss,y.combatLoss,y.escapeLoss,y.returnLoss,y.beganAt,y.combatStartedAt,y.combatEndedAt,y.escapeEndedAt,y.recallEndedAt,y.healingEndedAt,y.endedAt,y.recovered));
    }
}
void batching_oracles() {
    CHECK(exact_attack_prefix({900,45,60,40,20,225,0,0}));
    CHECK(exact_attack_prefix({.75,.125,100,.5,25,0}));
    CHECK(!exact_attack_prefix({.8,.1,100,0,25,0}));
    CHECK(exact_attack_prefix({std::numeric_limits<double>::denorm_min(),2*std::numeric_limits<double>::denorm_min()}));
    CHECK(exact_attack_prefix({std::numeric_limits<double>::min(),2*std::numeric_limits<double>::min()}));
    CHECK(!exact_attack_prefix({-1,0}) && !exact_attack_prefix({std::numeric_limits<double>::infinity()}));
    CHECK(!exact_attack_prefix({std::numeric_limits<double>::quiet_NaN()}));
    auto in=base();in.memberCount=1;in.members[0]={0,.8,0,true};in.strength=.1;in.health=in.maximumHealth=100;
    in.outward=in.escape=in.returnToCamp=in.capture=in.bank={0,0,true,true};in.captureTicks=0;in.tributeTicks=1;
    compare_sequential(in);CHECK(evaluate(in).schedule->attackCount==9); // ceil(.8/.1) would incorrectly give8.
    for(double damage:{.1,.125,1.0,45.0,60.0})for(unsigned n:{2u,8u,31u,511u,512u,513u})for(int direction:{-1,0,1}) {
        in=base();in.memberCount=1;in.health=in.maximumHealth=900;in.strength=damage;in.horizon=8192;
        in.members[0]={7,std::nextafter(double(n)*damage,direction<0?0.0:direction>0?std::numeric_limits<double>::infinity():double(n)*damage),direction==0?0.0:.125,true};
        in.outward=in.escape=in.returnToCamp=in.capture=in.bank={0,0,true,true};in.captureTicks=0;in.tributeTicks=1;
        compare_sequential(in);in.horizon=int(n);compare_sequential(in);
    }
    for(double loss:{.1,.125,1.0})for(unsigned n:{1u,7u,31u})for(int direction:{-1,0,1}) {
        in=base();in.memberCount=1;in.health=in.maximumHealth=100;in.strength=1;in.horizon=8192;
        const double boundary=25+double(n+1)*loss;
        in.health=std::nextafter(boundary,direction<0?0.0:direction>0?std::numeric_limits<double>::infinity():boundary);
        in.members[0]={0,100,loss,true};in.outward=in.returnToCamp=in.capture=in.bank={0,0,true,true};
        in.escape={1,1,true,true};in.captureTicks=0;in.tributeTicks=1;compare_sequential(in);
    }
    in=base();in.memberCount=1;in.health=40;in.maximumHealth=100;in.strength=1;in.horizon=8192;
    in.members[0]={0,1,10,true};in.outward=in.returnToCamp=in.capture=in.bank={0,0,true,true};in.escape={1,1,true,true};
    in.captureTicks=0;in.tributeTicks=1;compare_sequential(in);CHECK(evaluate(in).state==State::admitted);
    in.members[0].health=2;compare_sequential(in);CHECK(evaluate(in).reason==Reason::no_progress);
    std::uint32_t random=0x90ab1234u;auto next=[&]{random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    for(unsigned example=0;example<2048;++example) {
        in=base();in.horizon=example%3?8192:int(next()%160);in.memberCount=1+next()%8;
        in.health=in.maximumHealth=128+next()%2048;if(example%5==0)in.health=std::nextafter(in.maximumHealth*.25,std::numeric_limits<double>::infinity());
        in.strength=1+next()%160;in.campDamage=example%4==0?.1:example%4==1?1.100000023841858:1;
        in.mitigation=example%3==0?.699999988079071:example%3==1?.5:1;
        for(unsigned i=0;i<in.memberCount;++i)in.members[i]={int((in.memberCount-i)*7),std::ldexp(double(1+next()%4000),-int(example%4)),std::ldexp(double(next()%32),-int(example%3)),true};
        in.outward={int(next()%5),0,true,true};in.outward.exposureTurns=int(next()%unsigned(in.outward.turns+1));
        in.escape={int(next()%4),0,true,true};in.escape.exposureTurns=int(next()%unsigned(in.escape.turns+1));
        in.returnToCamp={int(next()%7),0,true,true};in.returnToCamp.exposureTurns=int(next()%unsigned(in.returnToCamp.turns+1));
        compare_sequential(in);
    }
    // Native-shaped integral Rows costs certify and avoid per-basic checkpoints.
    in=base();in.health=in.maximumHealth=900;in.strength=60;in.horizon=8192;
    unsigned boundaries=0;const auto fast=evaluate(in,[&]{++boundaries;return true;});compare_sequential(in);
    CHECK(fast.schedule && fast.state==sequential(in).state && fast.work.fastAttacks>0);
    const auto phases=fast.schedule->sortieCount;
    const unsigned nonAttackChecks=1+in.memberCount+5+phases+5*(phases-1)+2;
    CHECK(boundaries==fast.work.checkpoints && boundaries<nonAttackChecks+fast.schedule->attackCount);
}
void combat_budget_cases() {
    auto in=base();in.health=in.maximumHealth=900;in.strength=45;in.horizon=8192;
    for(unsigned i=0;i<3;++i)in.members[i].health=1350;
    CHECK(!combat_budget_exceeded(in));compare_sequential(in);
    CHECK(evaluate(in).schedule); // 3600 charged combat loss now fits8*675.
    std::swap(in.members[0],in.members[2]);CHECK(!combat_budget_exceeded(in));compare_sequential(in);
    in=base();in.memberCount=1;in.health=in.maximumHealth=100;in.strength=1;in.horizon=8192;
    in.members[0]={0,300,1,true};in.outward=in.escape=in.returnToCamp=in.capture=in.bank={0,0,true,true};
    in.captureTicks=0;in.tributeTicks=1;CHECK(!combat_budget_exceeded(in));compare_sequential(in);CHECK(evaluate(in).schedule);
    in.members[0].health=601;CHECK(combat_budget_exceeded(in));compare_sequential(in);
    for(unsigned fallback=0;fallback<6;++fallback) {
        auto other=in;
        if(fallback==0)other.mitigation=.5;
        if(fallback==1)other.health=99.5;
        if(fallback==2)other.strength=.5;
        if(fallback==3)other.members[0].health=301.5;
        if(fallback==4)other.members[0].incoming=.5;
        if(fallback==5)other.members[0].health=1000001;
        CHECK(!combat_budget_exceeded(other));compare_sequential(other);
    }
    auto unknown=in;unknown.members[0].known=false;CHECK(!combat_budget_exceeded(unknown));
    unknown=in;unknown.actorKnown=false;CHECK(!combat_budget_exceeded(unknown));
    in.members[0].health=0;in.members[0].incoming=1000000;in.memberCount=2;in.members[1]={1,300,1,true};
    CHECK(!combat_budget_exceeded(in));compare_sequential(in);
    in.members[1].incoming=0;in.members[1].health=512;CHECK(!combat_budget_exceeded(in));compare_sequential(in);
}
void work_diagnostics() {
    auto in=base();in.health=in.maximumHealth=900;in.strength=70;in.horizon=8192;
    const float queriedStyleMagnitude=.1f;
    in.campDamage=1+queriedStyleMagnitude; // Same float addition as current capital.h111.
    for(unsigned i=0;i<3;++i)in.members[i].health=1350;
    unsigned checkpoints=0;const auto rows=evaluate(in,[&]{++checkpoints;return true;});compare_sequential(in);
    CHECK(rows.work.checkpoints==checkpoints && rows.work.prefixChecks>0 && rows.work.fastAttacks>0);
    const double damage=in.strength*in.campDamage;
    CHECK(exact_attack_prefix({1350,damage,900,60,60,225,0,0}));
    std::printf("{\"kind\":\"declared-Rows-scalars\",\"strength\":70,\"health\":900,\"monsterMagnitude\":%.17g,\"campDamage\":%.17g,\"damage\":%.17g,\"state\":%u,\"reason\":%u,\"checkpoints\":%u,\"prefixChecks\":%u,\"fastAttacks\":%u,\"scalarAttacks\":%u,\"phase\":%u}\n",double(queriedStyleMagnitude),in.campDamage,damage,unsigned(rows.state),unsigned(rows.reason),rows.work.checkpoints,rows.work.prefixChecks,rows.work.fastAttacks,rows.work.scalarAttacks,unsigned(rows.work.phase));
    in=base();in.memberCount=1;in.health=in.maximumHealth=100;in.strength=.1;in.members[0]={0,.8,0,true};
    in.outward=in.escape=in.returnToCamp=in.capture=in.bank={0,0,true,true};in.captureTicks=0;in.tributeTicks=1;
    const auto slow=evaluate(in);CHECK(slow.schedule && slow.work.fastAttacks==0 && slow.work.scalarAttacks==9);
}
}
// Independent scalar OWNER-TURN oracle. Absolute ready-at timestamps, rather
// than decrementing cooldown counters, check the model's tick convention.
[[gnu::noinline]] Result tavi_scalar(const Input& in) {
    Schedule s;s.minHealth=in.health;auto hp=std::array<double,maximum_members>{};
    std::array<unsigned,maximum_members> order{};
    for(unsigned i=0;i<in.memberCount;++i){hp[i]=in.members[i].health;order[i]=i;}
    std::sort(order.begin(),order.begin()+in.memberCount,[&](auto a,auto b){return in.members[a].key<in.members[b].key;});
    double health=in.health;int time=0,resetAt=in.tavi.resetCooldown,burstAt=in.tavi.burstCooldown;
    const double d=in.strength*in.campDamage,b=in.tavi.focus*in.tavi.burstModifier*in.tavi.burstCampDamage;
    const double reserve=.25*in.maximumHealth,healing=in.maximumHealth*in.spawnHealing;
    const auto target=[&]{for(unsigned k=0;k<in.memberCount;++k)if(hp[order[k]]>0)return int(order[k]);return -1;};
    const auto incoming=[&]{double v=0;for(unsigned i=0;i<in.memberCount;++i)if(hp[i]>0)v+=in.members[i].incoming;return v;};
    const auto pay=[&](double n){health-=n;s.healthLoss+=n;s.minHealth=std::min(s.minHealth,health);return scalar(n)&&health>0&&health>=reserve;};
    const auto wait=[&](int n){time+=n;return time<=in.horizon;};
    for(unsigned k=0;k<maximum_sorties;++k) {
        auto& p=s.phases[k];++s.sortieCount;p.startHealth=health;p.beganAt=time;p.turnBegin=s.turnCount;
        if(!k){p.entryLoss=incoming()*in.mitigation*in.outward.exposureTurns;if(!pay(p.entryLoss))return stop(State::rejected,Reason::unsafe_entry);if(!wait(in.outward.turns))return stop(State::rejected,Reason::deadline);}
        p.entryEndHealth=health;p.combatStartedAt=time;
        p.combatEntryResetCooldown=std::max(0,resetAt-time);p.combatEntryBurstCooldown=std::max(0,burstAt-time);
        p.combatEntryCanAttack=time>0 || in.tavi.initialCanAttack;
        p.combatEntryCanCast=time>0 || in.tavi.initialCanCast;
        p.combatEntryBurstAllowed=time>0 || in.tavi.initialBurstAllowed;
        while(target()>=0) {
            if(s.turnCount==maximum_combat_turns)return stop(State::rejected,Reason::turn_bound);
            const unsigned at=unsigned(target());double h=hp[at];CombatTurn a;a.member=std::uint8_t(at);
            unsigned count=0;int nr=resetAt,nb=burstAt;
            const bool attack=time!=0 || in.tavi.initialCanAttack,cast=time!=0 || in.tavi.initialCanCast;
            auto hit=[&]{if(s.attackCount+count==maximum_attacks)return Reason::attack_bound;const double n=std::max(0.,h-d);if(n==h)return Reason::no_progress;h=n;++count;return Reason::none;};
            if(cast && time>=burstAt && b>0 && (time!=0 || in.tavi.initialBurstAllowed)) {
                a.cast=TaviCast::burst;nb=time+in.tavi.burstMaxCooldown;h=std::max(0.,h-b);
                if(attack&&h>0){const auto r=hit();if(r!=Reason::none)return stop(State::rejected,r);a.afterBasic=1;}
            } else {
                if(attack){const auto r=hit();if(r!=Reason::none)return stop(State::rejected,r);a.beforeBasic=1;}
                if(h>0&&cast&&time>=resetAt&&s.attackCount+count<maximum_attacks) {
                    a.cast=TaviCast::reset;nr=time+in.tavi.resetMaxCooldown;
                    const auto r=hit();if(r!=Reason::none)return stop(State::rejected,r);a.afterBasic=1;
                }
            }
            if(!count&&a.cast==TaviCast::none&&time>0)return stop(State::rejected,s.attackCount>=maximum_attacks?Reason::attack_bound:Reason::no_progress);
            if(h==hp[at]&&(count||a.cast!=TaviCast::none))return stop(State::rejected,Reason::no_progress);
            const double loss=incoming()*in.mitigation;double after=0;
            for(unsigned i=0;i<in.memberCount;++i)if(hp[i]>0&&(i!=at||h>0))after+=in.members[i].incoming;
            const double escape=after*in.mitigation*in.escape.exposureTurns;
            if(health-loss-escape<reserve)break;
            if(!pay(loss))return stop(State::rejected,Reason::unsafe_attack);
            if(!wait(1))return stop(State::rejected,Reason::deadline);
            resetAt=nr;burstAt=nb;hp[at]=h;p.combatLoss+=loss;p.attacks+=count;p.attacksByMember[at]+=count;s.attackCount+=count;
            s.burstCount+=a.cast==TaviCast::burst;s.resetCount+=a.cast==TaviCast::reset;s.turns[s.turnCount++]=a;++p.turnCount;
        }
        p.remainingHealth=hp;p.combatEndHealth=health;p.combatEndedAt=time;p.escapeEndHealth=p.returnHealth=health;
        p.escapeEndedAt=p.recallEndedAt=p.healingEndedAt=p.endedAt=time;
        if(target()<0) {
            if(!wait(in.capture.turns)||!wait(in.captureTicks+1)||!wait(in.bank.turns)||!wait(in.tributeTicks))return stop(State::rejected,Reason::deadline);
            s.tau=time;s.deltaS=in.gems;s.terminalHealth=health;s.conversion={time,in.gems};
            s.remainingResetCooldown=std::max(0,resetAt-time);s.remainingBurstCooldown=std::max(0,burstAt-time);
            return {State::admitted,Reason::none,std::move(s)};
        }
        if(!p.turnCount)return stop(State::rejected,Reason::no_progress);
        if(k+1==maximum_sorties)return stop(State::rejected,Reason::sortie_bound);
        p.recovered=true;p.escapeLoss=incoming()*in.mitigation*in.escape.exposureTurns;
        if(!pay(p.escapeLoss))return stop(State::rejected,Reason::unsafe_escape);
        if(!wait(in.escape.turns))return stop(State::rejected,Reason::deadline);p.escapeEndHealth=health;p.escapeEndedAt=time;
        if(!wait(std::max(1,in.recallTicks)))return stop(State::rejected,Reason::deadline);p.recallEndedAt=time;
        const int ticks=health<in.maximumHealth?int(std::max(1.,std::ceil((in.maximumHealth-health)/healing))):0;
        if(!wait(ticks))return stop(State::rejected,Reason::deadline);health=in.maximumHealth;p.healingEndedAt=time;
        p.returnLoss=incoming()*in.mitigation*in.returnToCamp.exposureTurns;
        if(!pay(p.returnLoss))return stop(State::rejected,Reason::unsafe_return);
        if(!wait(in.returnToCamp.turns))return stop(State::rejected,Reason::deadline);p.returnHealth=health;p.endedAt=time;
    }
    return stop(State::rejected,Reason::sortie_bound);
}
unsigned taviOracleCases=0,resumedCases=0,resumeCalls=0;
[[gnu::noinline]] void compare_tavi_results(const Input& in,const Result& actual,const Result& expected) {
    CHECK(actual.state==expected.state && actual.reason==expected.reason && bool(actual.schedule)==bool(expected.schedule));
    if(!actual.schedule)return;const auto& a=*actual.schedule;const auto& b=*expected.schedule;
    CHECK(std::tie(a.sortieCount,a.attackCount,a.tau,a.healthLoss,a.minHealth,a.terminalHealth,a.turnCount,a.burstCount,a.resetCount,a.remainingResetCooldown,a.remainingBurstCooldown)==std::tie(b.sortieCount,b.attackCount,b.tau,b.healthLoss,b.minHealth,b.terminalHealth,b.turnCount,b.burstCount,b.resetCount,b.remainingResetCooldown,b.remainingBurstCooldown));
    CHECK(a.deltaS==in.gems && a.terminalBag==0 && !a.stunCredited && a.conversion.completedAt==a.tau);
    for(unsigned n=0;n<a.turnCount;++n)CHECK(std::tie(a.turns[n].member,a.turns[n].cast,a.turns[n].beforeBasic,a.turns[n].afterBasic)==std::tie(b.turns[n].member,b.turns[n].cast,b.turns[n].beforeBasic,b.turns[n].afterBasic));
    for(unsigned n=0;n<a.sortieCount;++n) {
        const auto& x=a.phases[n];const auto& y=b.phases[n];CHECK(x.attacksByMember==y.attacksByMember && x.remainingHealth==y.remainingHealth);
        CHECK(std::tie(x.combatEntryResetCooldown,x.combatEntryBurstCooldown,x.combatEntryCanAttack,x.combatEntryCanCast,x.combatEntryBurstAllowed)==std::tie(y.combatEntryResetCooldown,y.combatEntryBurstCooldown,y.combatEntryCanAttack,y.combatEntryCanCast,y.combatEntryBurstAllowed));
        CHECK(std::tie(x.attacks,x.turnBegin,x.turnCount,x.startHealth,x.entryEndHealth,x.combatEndHealth,x.escapeEndHealth,x.returnHealth,x.entryLoss,x.combatLoss,x.escapeLoss,x.returnLoss,x.beganAt,x.combatStartedAt,x.combatEndedAt,x.escapeEndedAt,x.recallEndedAt,x.healingEndedAt,x.endedAt,x.recovered)==std::tie(y.attacks,y.turnBegin,y.turnCount,y.startHealth,y.entryEndHealth,y.combatEndHealth,y.escapeEndHealth,y.returnHealth,y.entryLoss,y.combatLoss,y.escapeLoss,y.returnLoss,y.beganAt,y.combatStartedAt,y.combatEndedAt,y.escapeEndedAt,y.recallEndedAt,y.healingEndedAt,y.endedAt,y.recovered));
    }
    phase_checks(in,a);
}
// Every existing ability oracle case also runs through arbitrary small
// chunks and repeatedly denied Ready budgets. No private Schedule is visible.
[[gnu::noinline]] void compare_tavi(const Input& in) {
    ++taviOracleCases;const auto actual=evaluate(in),expected=tavi_scalar(in);
    compare_tavi_results(in,actual,expected);
    TaviEvaluation cursor;cursor.start(in);unsigned calls=0;
    while(!cursor.terminal()) {
        CHECK(cursor.result()==nullptr && !cursor.take_complete(in.horizon));
        const unsigned before=cursor.owner_turns(),quota=1+(calls%7),allowed=calls%6;
        unsigned checksHere=0;
        const bool done=cursor.advance([&]{return checksHere++<allowed;},quota);
        CHECK(done==cursor.terminal() && cursor.owner_turns()-before<=quota);
        CHECK(++calls<4096 && cursor.work().scalarAttacks<=maximum_attacks);
    }
    CHECK(cursor.result()!=nullptr);
    auto taken=cursor.take_complete(in.horizon);CHECK(taken && !cursor.result() && !cursor.take_complete(in.horizon));
    compare_tavi_results(in,*taken,expected);++resumedCases;resumeCalls+=calls;
}
Input tavi_base() {
    auto in=base();in.horizon=8192;in.health=in.maximumHealth=900;in.strength=45;
    in.outward=in.escape=in.returnToCamp=in.capture=in.bank={0,0,true,true};in.captureTicks=0;in.tributeTicks=1;
    for(unsigned i=0;i<3;++i)in.members[i].health=1350;
    auto& t=in.tavi;t.enabled=t.known=t.initialCanAttack=t.initialCanCast=t.initialBurstAllowed=true;
    t.focus=110;t.burstModifier=double(1.4f);t.burstRange=4;t.stunRounds=1;t.resetMaxCooldown=t.burstMaxCooldown=8;
    return in;
}
[[gnu::noinline]] void tavi_cases() {
    auto in=tavi_base();CHECK(!combat_budget_exceeded(in));compare_tavi(in);
    auto result=evaluate(in);CHECK(result.schedule && result.schedule->sortieCount<=maximum_sorties && result.schedule->burstCount && result.schedule->resetCount);
    CHECK(result.schedule->turns[0].cast==TaviCast::burst && result.schedule->turns[0].beforeBasic==0 && result.schedule->turns[0].afterBasic==1);
    auto old=in;old.tavi.enabled=false;CHECK(!combat_budget_exceeded(old));CHECK(evaluate(old).schedule);
    // Current public Rows-shaped design, not native route/quiet evidence:
    // three1350/20 wolves plus three120/0 clusters, all900/45/110 base stats.
    in.memberCount=6;for(unsigned i=3;i<6;++i)in.members[i]={int(40+i),120,0,true};
    compare_tavi(in);result=evaluate(in);CHECK(result.schedule);
    std::printf("{\"kind\":\"analytical-Rows-Tavi\",\"totalHP\":4410,\"zeroRouteExposure\":true,\"sorties\":%u,\"turns\":%u,\"basics\":%u,\"bursts\":%u,\"resets\":%u,\"tau\":%d,\"minHealth\":%.17g,\"stunCredit\":false}\n",result.schedule->sortieCount,result.schedule->turnCount,result.schedule->attackCount,result.schedule->burstCount,result.schedule->resetCount,result.schedule->tau,result.schedule->minHealth);
    for(unsigned i=3;i<6;++i)in.members[i].key=int(i-3);compare_tavi(in);
    in=tavi_base();in.memberCount=1;in.members[0]={1,3000,0,true};result=evaluate(in);CHECK(result.schedule);
    CHECK(result.schedule->turns[0].cast==TaviCast::burst && result.schedule->turns[1].cast==TaviCast::reset);
    CHECK(result.schedule->turns[7].cast==TaviCast::none && result.schedule->turns[8].cast==TaviCast::burst && result.schedule->turns[9].cast==TaviCast::reset);compare_tavi(in);
    in.memberCount=1;in.members[0]={1,1,20,true};result=evaluate(in);
    CHECK(result.schedule && result.schedule->attackCount==0 && result.schedule->turnCount==1 && result.schedule->healthLoss==20);
    CHECK(result.schedule->turns[0].cast==TaviCast::burst && !result.schedule->turns[0].afterBasic);compare_tavi(in);
    in.members[0].health=2000;in.tavi.burstCooldown=8;in.tavi.initialCanAttack=false;
    result=evaluate(in);CHECK(result.schedule && result.schedule->turns[0].cast==TaviCast::reset && result.schedule->turns[0].beforeBasic==0 && result.schedule->turns[0].afterBasic==1);compare_tavi(in);
    in.tavi.initialCanAttack=true;result=evaluate(in);CHECK(result.schedule && result.schedule->turns[0].beforeBasic==1 && result.schedule->turns[0].afterBasic==1);compare_tavi(in);
    in.tavi.initialCanCast=false;result=evaluate(in);CHECK(result.schedule && result.schedule->turns[0].cast==TaviCast::none);compare_tavi(in);
    in.tavi.initialCanAttack=false;result=evaluate(in);CHECK(result.schedule && result.schedule->turns[0].beforeBasic==0 && result.schedule->turns[0].afterBasic==0);compare_tavi(in);
    in=tavi_base();in.tavi.initialBurstAllowed=false;result=evaluate(in);CHECK(result.schedule && result.schedule->turns[0].cast==TaviCast::reset);compare_tavi(in);
    in.outward={8,0,true,true};in.tavi.resetCooldown=in.tavi.burstCooldown=8;in.tavi.initialCanCast=in.tavi.initialCanAttack=false;
    result=evaluate(in);CHECK(result.schedule && result.schedule->turns[0].cast==TaviCast::burst && result.schedule->turns[0].afterBasic==1);compare_tavi(in);
    in=tavi_base();in.memberCount=2;in.members[0]={2,1,0,true};in.members[1]={1,1,0,true};result=evaluate(in);
    CHECK(result.schedule && result.schedule->turns[0].member==1 && result.schedule->turns[1].member==0 && result.schedule->turnCount==2);compare_tavi(in);
    in=tavi_base();in.tavi.known=false;failed(in,State::unknown,Reason::ability);
    in=tavi_base();in.tavi.burstCooldown=9;failed(in,State::invalid,Reason::ability);
    in=tavi_base();in.tavi.resetMaxCooldown=0;failed(in,State::invalid,Reason::ability);
    in=tavi_base();in.tavi.focus=std::numeric_limits<double>::infinity();failed(in,State::invalid,Reason::ability);
    in=tavi_base();in.tavi.focus=in.tavi.burstModifier=1e12;failed(in,State::invalid,Reason::numeric);
    in=tavi_base();in.memberCount=1;in.members[0]={0,1,20,true};in.health=244;
    failed(in,State::rejected,Reason::no_progress); // Stun does NOT erase pre-turn20.
    in.health=245;CHECK(evaluate(in).schedule);compare_tavi(in);
    in=tavi_base();in.memberCount=1;in.members[0]={0,512,0,true};in.strength=1;in.tavi.focus=0;in.tavi.resetMaxCooldown=8192;in.tavi.resetCooldown=8192;
    result=evaluate(in);CHECK(result.schedule && result.schedule->attackCount==512);compare_tavi(in);
    in.members[0].health=513;failed(in,State::rejected,Reason::attack_bound);compare_tavi(in);
    in=tavi_base();in.escape={2,1,true,true};in.returnToCamp={3,1,true,true};in.outward={2,1,true,true};in.capture={2,0,true,true};in.bank={3,0,true,true};in.recallTicks=5;in.captureTicks=5;in.tributeTicks=4;compare_tavi(in);
    result=evaluate(in);CHECK(result.schedule);const int exact=result.schedule->tau;in.horizon=exact;CHECK(evaluate(in).schedule);in.horizon=exact-1;failed(in,State::rejected,Reason::deadline);
    in=tavi_base();unsigned count=0;evaluate(in,[&]{++count;return true;});
    for(unsigned cut=0;cut<count;++cut){unsigned calls=0;const auto r=evaluate(in,[&]{return calls++<cut;});CHECK(r.state==State::unknown && r.reason==Reason::cutoff && !r.schedule);}
    std::uint32_t rng=0x771923ad;const auto next=[&]{rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;};
    for(unsigned c=0;c<1024;++c) {
        in=tavi_base();in.memberCount=1+next()%8;in.health=in.maximumHealth=128+next()%2048;in.strength=1+next()%100;
        in.tavi.focus=next()%200;in.tavi.burstModifier=c%2?1.4:.125;in.tavi.burstCampDamage=in.campDamage=c%3?1:1.100000023841858;
        in.mitigation=c%3?.5:1;in.tavi.resetCooldown=next()%9;in.tavi.burstCooldown=next()%9;
        in.tavi.initialCanAttack=next()%2;in.tavi.initialCanCast=next()%2;in.tavi.initialBurstAllowed=next()%2;
        for(unsigned i=0;i<in.memberCount;++i)in.members[i]={int((in.memberCount-i)*3),double(1+next()%2200),double(next()%25),true};
        in.outward={int(next()%4),0,true,true};in.escape={int(next()%3),0,true,true};in.escape.exposureTurns=int(next()%unsigned(in.escape.turns+1));
        in.returnToCamp={int(next()%5),0,true,true};in.returnToCamp.exposureTurns=int(next()%unsigned(in.returnToCamp.turns+1));
        in.horizon=c%3?8192:int(next()%100);compare_tavi(in);
    }
}
[[gnu::noinline]] void cursor_cases() {
    auto in=tavi_base();in.memberCount=6;
    for(unsigned i=3;i<6;++i)in.members[i]={int(40+i),120,0,true};
    const auto expected=evaluate(in);CHECK(expected.schedule);
    unsigned fullChecks=0;evaluate(in,[&]{++fullChecks;return true;});
    for(unsigned cut=0;cut<fullChecks;++cut) {
        TaviEvaluation cursor;cursor.start(in);unsigned calls=0;
        CHECK(!cursor.advance([&]{return calls++<cut;},maximum_combat_turns));
        CHECK(!cursor.terminal() && !cursor.result() && !cursor.take_complete(in.horizon));
        CHECK(cursor.work().checkpoints==cut+1);
        CHECK(cursor.advance([]{return true;},maximum_combat_turns));
        CHECK(cursor.result());compare_tavi_results(in,*cursor.result(),expected);
    }
    for(unsigned quota:{1u,2u,3u,4u,7u,maximum_combat_turns,UINT_MAX}) {
        TaviEvaluation cursor;cursor.start(in);unsigned calls=0;
        while(!cursor.terminal()) {
            const unsigned before=cursor.owner_turns();
            cursor.advance([]{return true;},quota);CHECK(cursor.owner_turns()-before<=quota);
            CHECK(++calls<=maximum_combat_turns+1);
        }
        compare_tavi_results(in,*cursor.result(),expected);
    }
    TaviEvaluation cursor;
    CHECK(!cursor.terminal() && !cursor.result() && !cursor.matches(in));
    CHECK(!cursor.advance([]{return true;},4));
    cursor.start(in);unsigned readyCalls=0;
    CHECK(!cursor.advance([&]{++readyCalls;return true;},0) && readyCalls==0);
    CHECK(cursor.matches(in));auto fresh=in;++fresh.round;--fresh.horizon;CHECK(cursor.matches(fresh));
    fresh.round=INT_MAX;CHECK(!cursor.matches(fresh));
    fresh=in;fresh.health-=1;CHECK(!cursor.matches(fresh));
    fresh=in;fresh.maximumHealth+=1;CHECK(!cursor.matches(fresh));
    fresh=in;fresh.strength+=1;CHECK(!cursor.matches(fresh));
    fresh=in;fresh.campDamage+=.001;CHECK(!cursor.matches(fresh));
    fresh=in;fresh.mitigation=.5;CHECK(!cursor.matches(fresh));
    fresh=in;fresh.spawnHealing=.2;CHECK(!cursor.matches(fresh));
    fresh=in;fresh.quietNoReset=false;CHECK(!cursor.matches(fresh));
    fresh=in;fresh.members[1].health-=1;CHECK(!cursor.matches(fresh));
    fresh=in;fresh.members[1].incoming+=1;CHECK(!cursor.matches(fresh));
    fresh=in;fresh.members[1].known=false;CHECK(!cursor.matches(fresh));
    fresh=in;fresh.members[1].key+=100;CHECK(!cursor.matches(fresh));
    fresh=in;fresh.escape.exposureTurns=1;CHECK(!cursor.matches(fresh));
    fresh=in;fresh.bank.turns=1;CHECK(!cursor.matches(fresh));
    fresh=in;fresh.captureTicks+=1;CHECK(!cursor.matches(fresh));
    fresh=in;fresh.tavi.resetCooldown=1;CHECK(!cursor.matches(fresh));
    fresh=in;fresh.tavi.initialCanAttack=false;CHECK(!cursor.matches(fresh));
    fresh=in;fresh.tavi.burstRange=3;CHECK(!cursor.matches(fresh));
    fresh=in;fresh.tavi.focus+=1;CHECK(!cursor.matches(fresh));
    fresh=in;fresh.tavi.enabled=false;CHECK(!cursor.matches(fresh));
    // The cursor owns its input; caller mutation cannot alter the projection.
    cursor.start(in);fresh=in;in.members[0].health=1;in.health=1;
    CHECK(cursor.advance([]{return true;},maximum_combat_turns));
    compare_tavi_results(fresh,*cursor.result(),expected);
    const int tau=expected.schedule->tau;
    auto limited=cursor.take_complete(tau-1);
    CHECK(limited && limited->state==State::rejected && limited->reason==Reason::deadline && !limited->schedule);
    cursor.start(fresh);CHECK(cursor.advance([]{return true;},maximum_combat_turns));
    limited=cursor.take_complete(tau);CHECK(limited && limited->schedule && limited->schedule->tau==tau);
    cursor.start(fresh);CHECK(cursor.advance([]{return true;},maximum_combat_turns));
    limited=cursor.take_complete(-1);CHECK(limited && limited->state==State::invalid && !limited->schedule);
    // Semantic mismatch requires explicit restart; restart discards all work.
    fresh.members[0].health-=100;
    cursor.start(fresh);CHECK(cursor.owner_turns()==0 && cursor.work().checkpoints==0 && !cursor.result());
    CHECK(cursor.matches(fresh));CHECK(cursor.advance([]{return true;},maximum_combat_turns));
    compare_tavi_results(fresh,*cursor.result(),tavi_scalar(fresh));
}

[[gnu::noinline]] void capacity_cases() {
    static_assert(maximum_sorties==8 && maximum_attacks==512 && maximum_combat_turns==524);
    auto in=base();in.memberCount=1;in.members[0]={7,296,2,true};
    in.health=in.maximumHealth=100;in.strength=1;in.horizon=8192;
    in.outward=in.escape=in.returnToCamp=in.capture=in.bank={0,0,true,true};
    in.captureTicks=0;in.tributeTicks=1;
    // floor(75/2)=37 attacks per sortie. Eight complete296 attacks;
    // the297th needs a ninth, although total incoming594 is below600.
    CHECK(!combat_budget_exceeded(in));auto result=evaluate(in);CHECK(result.schedule);
    CHECK(result.schedule->sortieCount==8 && result.schedule->attackCount==296 &&
          result.schedule->tau==389 && result.schedule->minHealth==26);
    for(const auto& phase:result.schedule->phases)CHECK(phase.attacks==37);
    compare_sequential(in);
    in.members[0].health=297;CHECK(!combat_budget_exceeded(in));
    failed(in,State::rejected,Reason::sortie_bound);compare_sequential(in);
    in.members[0].health=301;CHECK(combat_budget_exceeded(in));
    failed(in,State::rejected,Reason::combat_budget_bound);compare_sequential(in);
    in.members[0].health=296;in.horizon=389;CHECK(evaluate(in).schedule);
    in.horizon=388;failed(in,State::rejected,Reason::deadline);
    // Same capacity boundary through the independent owner-turn oracle:
    // actual kit known, no spell damage and reset still cooling throughout.
    in.horizon=8192;in.tavi=tavi_base().tavi;in.tavi.focus=0;
    in.tavi.resetCooldown=in.tavi.resetMaxCooldown=8192;
    result=evaluate(in);CHECK(result.schedule && result.schedule->sortieCount==8);
    CHECK(result.schedule->burstCount==0 && result.schedule->resetCount==0 &&
          result.schedule->tau==389 && result.schedule->attackCount==296);
    compare_tavi_results(in,result,tavi_scalar(in));
    TaviEvaluation cursor;cursor.start(in);
    while(!cursor.advance([]{return true;},4))CHECK(!cursor.result());
    compare_tavi_results(in,*cursor.result(),result);
    auto latest=cursor.take_complete(388);CHECK(latest && latest->reason==Reason::deadline && !latest->schedule);
    in.members[0].health=297;failed(in,State::rejected,Reason::sortie_bound);
    compare_tavi_results(in,evaluate(in),tavi_scalar(in));
    // Basic and Tavi paths preserve their independent512-basic limit.
    for(bool ability:{false,true}) {
        in.tavi.enabled=ability;in.members[0]={7,512,0,true};
        result=evaluate(in);CHECK(result.schedule && result.schedule->attackCount==512);
        in.members[0].health=513;failed(in,State::rejected,Reason::attack_bound);
    }
    // Source-reconstructed PUBLIC Rows geometry/catalogue, not saved native
    // Input or a proof of quiet/aggro continuity. Earlier4-cap copies matched
    // native committed counters47/54 A and39/43 B before sortie_bound.
    for(bool bottom:{false,true}) {
        in=tavi_base();in.round=bottom?221:225;in.horizon=2000-in.round;in.memberCount=6;
        const int a[]={168,169,218,219,268,269},b[]={2230,2231,2280,2281,2330,2331};
        for(unsigned i=0;i<6;++i) {
            const bool wolf=bottom?i%2!=0:i%2==0;
            in.members[i]={bottom?b[i]:a[i],wolf?1350.:120.,wolf?20.:0.,true};
        }
        in.gems=12;in.outward={14,1,true,true};in.escape={3,2,true,true};
        in.returnToCamp={14,1,true,true};in.capture={bottom?3:2,0,true,true};in.bank={bottom?12:15,0,true,true};
        in.recallTicks=5;in.captureTicks=5;in.tributeTicks=2;in.spawnHealing=1;
        result=evaluate(in);CHECK(result.schedule);compare_tavi_results(in,result,tavi_scalar(in));
        const auto& schedule=*result.schedule;
        CHECK(schedule.sortieCount==5 && schedule.tau==(bottom?193:191) && schedule.minHealth==240);
        CHECK(schedule.turnCount==(bottom?64u:60u) && schedule.attackCount==(bottom?70u:69u));
        CHECK(schedule.terminalHealth==(bottom?320:680) && schedule.healthLoss==(bottom?3200:2860));
        CHECK(schedule.deltaS==12 && schedule.burstCount==(bottom?10u:9u) && schedule.resetCount==9 &&
              schedule.remainingResetCooldown==0 && schedule.remainingBurstCooldown==0 && !schedule.stunCredited);
        cursor.start(in);while(!cursor.advance([]{return true;},4))CHECK(!cursor.result());
        compare_tavi_results(in,*cursor.result(),result);
        std::printf("{\"kind\":\"source-reconstructed-Rows-capacity8\",\"side\":\"%c\",\"sorties\":%u,\"tau\":%d,\"minHP\":%.17g,\"remainingDeadline\":%d,\"nativeInputObserved\":false}\n",bottom?'B':'A',schedule.sortieCount,schedule.tau,schedule.minHealth,in.horizon-schedule.tau);
        in.horizon=128;failed(in,State::rejected,Reason::deadline);
    }
}

int main() {
    auto in=base();auto result=evaluate(in);CHECK(result.state==State::admitted && result.schedule);
    const auto s=*result.schedule;phase_checks(in,s);
    // Analytical full4290HP/60incoming Tavi example. Both first recoveries
    // stop MID-member; no ability contribution or premature reward exists.
    CHECK(s.sortieCount==3 && s.attackCount==33 && s.tau==89);
    CHECK(s.phases[0].attacksByMember[0]==8 && s.phases[0].remainingHealth[0]==350);
    CHECK(s.phases[1].attacksByMember[0]==3 && s.phases[1].attacksByMember[1]==8);
    CHECK(s.phases[1].remainingHealth[1]==350 && s.phases[2].remainingHealth[2]==0);
    CHECK(s.minHealth==210 && s.terminalHealth==410 && s.healthLoss==1420);
    in.horizon=s.tau;CHECK(evaluate(in).state==State::admitted);--in.horizon;failed(in,State::rejected,Reason::deadline);
    in=base();in.health=in.maximumHealth=1500;in.strength=115;
    result=evaluate(in);CHECK(result.state==State::admitted && result.schedule);
    phase_checks(in,*result.schedule);CHECK(result.schedule->sortieCount==2 && result.schedule->attackCount==39);

    in=base();in.outward.exposureTurns=1;result=evaluate(in);CHECK(result.schedule);
    CHECK(result.schedule->phases[0].entryLoss==60);phase_checks(in,*result.schedule);
    in=base();in.outward={10,10,true,true};failed(in,State::rejected,Reason::unsafe_entry);
    in=base();in.returnToCamp={20,20,true,true};failed(in,State::rejected,Reason::unsafe_return);
    in=base();in.escape.exposureTurns=2;in.health=200;failed(in,State::rejected,Reason::no_progress);
    in=base();in.members[0].known=false;failed(in,State::unknown,Reason::evidence);
    in=base();in.quietNoReset=false;failed(in,State::unknown,Reason::continuity);
    in=base();in.escape.supported=false;failed(in,State::rejected,Reason::route);
    in=base();in.bank.known=false;failed(in,State::unknown,Reason::evidence);
    in=base();in.carriedGems=1;failed(in,State::rejected,Reason::old_bag);
    in=base();in.members[1].key=in.members[0].key;in.actorKnown=false;failed(in,State::invalid,Reason::shape);
    in=base();in.members[0].health=std::numeric_limits<double>::infinity();failed(in,State::invalid,Reason::numeric);
    in=base();in.strength=0;failed(in,State::invalid,Reason::numeric);
    in=base();in.maximumHealth=std::numeric_limits<double>::quiet_NaN();failed(in,State::invalid,Reason::numeric);
    in=base();in.round=INT_MAX;failed(in,State::invalid,Reason::shape);
    in=base();in.recallTicks=0;result=evaluate(in);CHECK(result.schedule);
    CHECK(result.schedule->phases[0].recallEndedAt-result.schedule->phases[0].escapeEndedAt==1);
    in=base();std::swap(in.members[0],in.members[2]);result=evaluate(in);CHECK(result.schedule);
    CHECK(result.schedule->tau==s.tau && result.schedule->terminalHealth==s.terminalHealth);
    CHECK(result.schedule->phases[0].attacksByMember[2]==8 && result.schedule->phases[0].attacksByMember[0]==0);

    // Independent integer one-member oracle: fixed incoming until the final
    // kill, exact attacks per75%HP budget, explicit recovery and phase time.
    for(int hp=1;hp<=500;hp+=7)for(int d:{1,3,7})for(int incoming:{1,2,3}) {
        in=base();in.memberCount=1;in.members[0]={7,double(hp),double(incoming),true};
        in.health=in.maximumHealth=100;in.strength=d;in.horizon=8192;
        in.outward=in.escape=in.returnToCamp=in.capture=in.bank={0,0,true,true};
        in.captureTicks=0;in.tributeTicks=1;
        const int count=(hp+d-1)/d,quota=75/incoming,sorties=(count+quota-1)/quota;
        result=evaluate(in);const bool valid=count<=512 && sorties<=int(maximum_sorties);
        CHECK((result.state==State::admitted)==valid);
        if(!valid){CHECK(!result.schedule);continue;}
        const auto& actual=*result.schedule;phase_checks(in,actual);
        const int last=count-(sorties-1)*quota;
        const int heal=(quota*incoming+9)/10;
        CHECK(actual.attackCount==unsigned(count) && actual.sortieCount==unsigned(sorties));
        CHECK(actual.tau==count+(sorties-1)*(5+heal)+2);
        CHECK(actual.terminalHealth==100-last*incoming);
    }
    in=base();in.memberCount=1;in.members[0]={0,300,1,true};in.health=in.maximumHealth=100;
    in.strength=1;in.horizon=400;in.outward=in.escape=in.returnToCamp=in.capture=in.bank={0,0,true,true};
    in.captureTicks=0;in.tributeTicks=1;result=evaluate(in);CHECK(result.schedule);
    CHECK(result.schedule->sortieCount==4 && result.schedule->attackCount==300);
    in.members[0].health=301;result=evaluate(in);CHECK(result.schedule && result.schedule->sortieCount==5);
    in.members[0]={0,512,0,true};in.horizon=1000;result=evaluate(in);CHECK(result.schedule && result.schedule->attackCount==512);
    in.members[0].health=513;failed(in,State::rejected,Reason::attack_bound);
    in.members[0].health=1e12;in.strength=1e-320;failed(in,State::rejected,Reason::no_progress);
    struct Ready {
        unsigned calls=0,allowed;
        explicit Ready(unsigned n):allowed(n){}
        Ready(const Ready&)=delete;
        bool operator()(){return calls++<allowed;}
    };
    in=base();Ready baseline(10000);result=evaluate(in,baseline);CHECK(result.schedule);
    const unsigned boundaries=baseline.calls;CHECK(boundaries>in.memberCount+10 && boundaries<57);
    for(unsigned cut=0;cut<boundaries;++cut) {
        Ready ready(cut);const auto cutResult=evaluate(in,ready);
        CHECK(cutResult.state==State::unknown && cutResult.reason==Reason::cutoff && !cutResult.schedule);
        CHECK(cutResult.work.checkpoints==cut+1 && cutResult.work.fastAttacks+cutResult.work.scalarAttacks<=maximum_attacks);
        CHECK(in.members[0].health==1430 && in.health==750);
    }
    Ready full(boundaries);result=evaluate(in,full);CHECK(result.schedule && result.schedule->tau==s.tau);
    batching_oracles();combat_budget_cases();work_diagnostics();tavi_cases();cursor_cases();capacity_cases();
    std::printf("{\"checks\":%u,\"sequentialOracleCases\":%u,\"taviOwnerTurnOracleCases\":%u,\"necessaryEarlyRejections\":%u,\"resumedCases\":%u,\"resumeCalls\":%u,\"release\":true,\"nativeLaunches\":0}\n",checks,oracleCases,taviOracleCases,oracleEarlyRejections,resumedCases,resumeCalls);
}
