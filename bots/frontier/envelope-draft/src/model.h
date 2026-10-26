#pragma once
#include <array>
#include <algorithm>
#include <limits>

namespace p10_envelope {
enum class Kit { willow,varg,marcella,vesper,tavi,vendetta,heart,ren,prism,cassi,issac };
enum class Work { lead,heal,independent,paired,field_capital,converter };
struct Duty {Kit kit;Work work;int partner=-1;};
struct OperatingPackage {
    int id=0;
    std::array<Duty,6> duties{};
    int actors=3;
    bool paired=false,fieldShop=false;
    // Each core is examined as a three-selection organization; extensions are
    // independent circuits, rather than an assumed extra helper everywhere.
};
inline OperatingPackage package(int id,int picks) {
    OperatingPackage p;p.id=id;
    if(id==0)p.duties={Duty{Kit::varg,Work::lead},Duty{Kit::willow,Work::heal,0},Duty{Kit::marcella,Work::converter},Duty{Kit::vesper,Work::independent},Duty{Kit::tavi,Work::field_capital},{}};
    if(id==1)p.duties={Duty{Kit::vendetta,Work::lead},Duty{Kit::heart,Work::independent},Duty{Kit::marcella,Work::converter},Duty{Kit::ren,Work::independent},Duty{Kit::tavi,Work::field_capital},{}};
    if(id==2){p.fieldShop=true;p.duties={Duty{Kit::tavi,Work::field_capital},Duty{Kit::prism,Work::lead},Duty{Kit::vesper,Work::converter},Duty{Kit::willow,Work::independent},Duty{Kit::marcella,Work::independent},{}};}
    if(id==3){p.paired=true;p.duties={Duty{Kit::cassi,Work::paired,1},Duty{Kit::issac,Work::paired,0},Duty{Kit::willow,Work::heal,1},Duty{Kit::marcella,Work::converter},Duty{Kit::vesper,Work::independent},Duty{Kit::ren,Work::independent}};}
    p.actors=std::clamp(picks,3,5)+(p.paired?1:0);return p;
}
struct Design {
    double hp=0,strength=0,focus=0,strengthGrowth=0,focusGrowth=0;
    int range=1;
    bool splash=false,reset=false,heal=false,retaliate=false;
    double damageModifier=0,areaRadius=0,damageRange=4;
    bool damageExclusive=true;
    double shieldModifier=0;int shieldDuration=0;
};
struct Monster {double hp=0,strength=0;int range=0,x=0,y=0;};
struct Source {
    int id=-1,travel=-1,altarTravel=-1,spawnTravel=-1,respawn=0,gems=0,gold=0;
    int count=0;std::array<Monster,8> monsters{};
    bool route=false,freeHold=false,exit=false;
    int leadX=0,leadY=0,helperX=0,helperY=0;
    bool positioned=false,helperPad=true;
};
struct Premises {
    bool fullGroup=false,route=false,exclusiveOccupancy=false,healerVisible=false;
    bool linkedContact=false,enemyInterruptionExcluded=true;
};
struct Circuit {
    bool feasible=false;int source=-1,lead=-1,helper=-1,holder=-1,templateId=0;
    int clear=0,firstBank=10000,renewal=10000,secured=0,gold=0,purchases=0;
    double injury=0;Premises premises{};
};
struct Envelope {
    int packageId=0,selections=3,secured=0,gold=0,firstBank=10000;
    double exposure=0;bool feasible=false;
    std::array<Circuit,6> circuits{};int circuitsUsed=0,excluded=0,certificates=0;
};
inline int ticks(int bag,double multiplier,int base,int minimum) {
    const double raw=bag*multiplier;
    const int ceiled=int(raw)+(raw>int(raw)?1:0);
    return std::max(minimum,ceiled+base);
}
// Two handwritten scripts: sustained basic/reset work, or damage spell before
// exposure. A cast is consumed once; healing competes with healer attack reset.
inline Circuit run(const Source&s,Design lead,const Design*helper,int actor,int support,
                   int mode,int capture,double multiplier,int base,int minimum,int shardCost) {
    Circuit c;c.source=s.id;c.lead=actor;c.helper=support;c.holder=actor;c.templateId=mode;
    c.premises={s.count>0&&s.count<=8,s.route,s.freeHold,helper!=nullptr,true,true};
    if(s.count<=0||s.count>8||!s.route||!s.freeHold||!s.exit||
       (helper&&!s.helperPad)||s.travel<0||s.altarTravel<0)return c;
    auto hp=std::array<double,8>{};for(int i=0;i<s.count;++i)hp[i]=s.monsters[i].hp;
    double health=lead.hp,helperHealth=helper?helper->hp:0;
    int spell=0,healing=0,reset=0,shieldTimer=0;bool done=false;double incoming=0,shield=0;
    auto reach=[&](int i,bool support){return !s.positioned?0:std::max(std::abs(s.monsters[i].x-(support?s.helperX:s.leadX)),std::abs(s.monsters[i].y-(support?s.helperY:s.leadY)));};
    for(int t=0;t<96;++t){
        // Camp-first damage is charged before any useful actor action following
        // the first awakening. No shield, favorable target order or free siege.
        if(t>0){
            incoming=0;double helperIncoming=0;
            for(int i=0;i<s.count;++i)if(hp[i]>0){
                const int leadDistance=reach(i,false),helperDistance=helper?reach(i,true):10000;
                if(leadDistance<=s.monsters[i].range&&leadDistance<=helperDistance)incoming+=s.monsters[i].strength;
                if(helper&&helperDistance<=s.monsters[i].range&&helperDistance<=leadDistance)helperIncoming+=s.monsters[i].strength;
            }
            const double absorbed=std::min(shield,incoming);shield-=absorbed;health-=incoming-absorbed;
            helperHealth-=helperIncoming;
            if(health<=std::max(30.0,incoming))return c; // next phase and exit reserve
            if(helper&&helperHealth<=std::max(30.0,helperIncoming))return c; // twins retain correlated death risk
            if(lead.retaliate)for(int i=0;i<s.count;++i)
                if(hp[i]>0&&s.monsters[i].strength>0&&reach(i,false)<=s.monsters[i].range&&
                   (!helper||reach(i,false)<reach(i,true)))hp[i]-=.2*lead.focus;
        }
        if(shieldTimer<=0)shield=0; // owner-start expiry follows the camp phase
        int target=-1;
        bool living=false;
        for(int i=0;i<s.count;++i)if(hp[i]>0){living=true;
            if((reach(i,false)<=lead.range||(mode==1&&lead.damageModifier>0&&spell<=0&&reach(i,false)<=lead.damageRange))&&
               (target<0||s.monsters[i].strength>s.monsters[target].strength))target=i;
        }
        if(!living){c.clear=t;done=true;break;}
        if(target<0)return c;
        if(helper&&helper->heal&&healing<=0&&health<lead.hp-incoming){health=std::min(lead.hp,health+3*helper->focus);healing=8;}
        const bool castShield=mode==1&&lead.shieldModifier>0&&spell<=0;
        const bool castDamage=mode==1&&lead.damageModifier>0&&spell<=0&&reach(target,false)<=lead.damageRange&&!castShield;
        const bool cast=castShield||castDamage;
        if(castShield){shield=lead.shieldModifier*lead.focus;shieldTimer=lead.shieldDuration;spell=8;}
        if(castDamage){
            const double damage=lead.damageModifier*lead.focus;
            hp[target]-=damage;spell=8;
            if(lead.areaRadius>0)for(int i=0;i<s.count;++i)
                if(i!=target&&std::max(std::abs(s.monsters[i].x-s.monsters[target].x),std::abs(s.monsters[i].y-s.monsters[target].y))<=lead.areaRadius)hp[i]-=damage;
        }
        // At this stance no ordinary movement is scheduled during attack work.
        if(hp[target]>0&&reach(target,false)<=lead.range){
            hp[target]-=lead.strength;
            if(lead.splash)for(int i=0;i<s.count;++i)if(i!=target&&
                std::max(std::abs(s.monsters[i].x-s.monsters[target].x),std::abs(s.monsters[i].y-s.monsters[target].y))<=1)hp[i]-=lead.strength;
            if(!cast&&lead.reset&&reset<=0){
                hp[target]-=lead.strength;
                if(lead.splash)for(int i=0;i<s.count;++i)if(i!=target&&
                    std::max(std::abs(s.monsters[i].x-s.monsters[target].x),std::abs(s.monsters[i].y-s.monsters[target].y))<=1)hp[i]-=lead.strength;
                reset=8;
            }
        }
        if(helper&&!helper->heal&&helperHealth>0&&hp[target]>0&&reach(target,true)<=helper->range)hp[target]-=helper->strength;
        spell=std::max(0,spell-1);healing=std::max(0,healing-1);reset=std::max(0,reset-1);
        if(shieldTimer>0)--shieldTimer;
    }
    if(!done)return c;
    c.injury=lead.hp-health;
    c.firstBank=s.travel+c.clear+capture+1+s.altarTravel+ticks(s.gems,multiplier,base,minimum)+1;
    c.renewal=c.firstBank+s.spawnTravel+s.travel;
    if(c.firstBank>192||c.renewal>192)return c;
    c.feasible=true;c.secured=s.gems;c.gold=s.gold;
    // Personal first payout funds a real stat shard; no helper purse is used.
    c.purchases=shardCost>0?s.gold/shardCost:0;
    const int nextReady=std::max(c.firstBank+s.respawn,c.renewal);
    const int nextBank=nextReady+c.clear+capture+1+s.altarTravel+ticks(s.gems,multiplier,base,minimum)+1;
    if(nextBank<=192){c.secured+=s.gems;c.gold+=s.gold;}
    return c;
}
inline bool better(const Envelope&a,const Envelope&b){
    if(a.feasible!=b.feasible)return a.feasible;
    if(a.secured!=b.secured)return a.secured>b.secured;
    if(a.firstBank!=b.firstBank)return a.firstBank<b.firstBank;
    return a.exposure<b.exposure;
}
// Draft certification never executes a phase-by-phase combat forecast. The
// handwritten script attacks one fixed target until it dies; basic/splash and
// eight-turn cast/reset events are counted exactly in that block. Incoming is
// conservatively charged from the ENTIRE initial group until the final death.
// A shield or healer gets only its scheduled, non-overhealing service. This is
// intentionally narrower than run(), which rechecks a selected live program.
inline Circuit certify(const Source&s,Design lead,const Design*helper,int actor,int support,
                       int mode,int capture,double multiplier,int base,int minimum,int shardCost){
    Circuit c;c.source=s.id;c.lead=actor;c.helper=support;c.holder=actor;c.templateId=mode;
    c.premises={s.count>0&&s.count<=8,s.route,s.freeHold,helper!=nullptr,true,true};
    if(s.count<=0||s.count>8||!s.route||!s.freeHold||!s.exit||s.travel<0||s.altarTravel<0||
       (helper&&!s.helperPad)||lead.hp<=0||lead.strength<=0)return c;
    auto reach=[&](int i,bool second){return !s.positioned?0:std::max(std::abs(s.monsters[i].x-(second?s.helperX:s.leadX)),std::abs(s.monsters[i].y-(second?s.helperY:s.leadY)));};
    std::array<double,8> hp{};double incoming=0,helperIncoming=0;
    for(int i=0;i<s.count;++i){hp[i]=s.monsters[i].hp;if(hp[i]<=0)continue;
        const int a=reach(i,false),b=helper?reach(i,true):10000;
        if(a<=s.monsters[i].range&&a<=b)incoming+=s.monsters[i].strength;
        if(helper&&b<=s.monsters[i].range&&b<=a)helperIncoming+=s.monsters[i].strength;
    }
    int elapsed=0;
    for(int block=0;block<s.count;++block){
        int target=-1;for(int i=0;i<s.count;++i)if(hp[i]>0&&(target<0||s.monsters[i].strength>s.monsters[target].strength))target=i;
        if(target<0)break;
        const bool basic=reach(target,false)<=lead.range;
        const bool shield=mode==1&&lead.shieldModifier>0;
        const bool damage=mode==1&&!shield&&lead.damageModifier>0&&reach(target,false)<=lead.damageRange;
        const bool reset=mode==0&&lead.reset&&basic;
        const bool extra=helper&&!helper->heal&&reach(target,true)<=helper->range;
        auto events=[&](int n){return (elapsed+n+7)/8-(elapsed+7)/8;};
        auto targetDamage=[&](int n){return (basic?lead.strength*n:0)+(extra?helper->strength*n:0)+events(n)*((reset?lead.strength:0)+(damage?lead.damageModifier*lead.focus:0));};
        if(elapsed>=96||targetDamage(96-elapsed)<hp[target])return c;
        int lo=1,hi=96-elapsed;while(lo<hi){const int mid=(lo+hi)/2;if(targetDamage(mid)>=hp[target])hi=mid;else lo=mid+1;}
        const int casts=events(lo);
        for(int i=0;i<s.count;++i)if(hp[i]>0){const int separation=std::max(std::abs(s.monsters[i].x-s.monsters[target].x),std::abs(s.monsters[i].y-s.monsters[target].y));
            if(i==target||(lead.splash&&separation<=1))hp[i]-=(basic?lead.strength*lo:0)+(reset?lead.strength*casts:0);
            if(i==target&&extra)hp[i]-=helper->strength*lo;
            if(damage&&(i==target||(lead.areaRadius>0&&separation<=lead.areaRadius)))hp[i]-=lead.damageModifier*lead.focus*casts;
        }
        elapsed+=lo;
    }
    for(int i=0;i<s.count;++i)if(hp[i]>0)return c;
    const int exposed=std::max(0,elapsed-1),duration=mode==1?std::clamp(lead.shieldDuration-1,0,7):0;
    const double shield=mode==1?lead.shieldModifier*lead.focus:0;
    auto absorbed=[&](int phases){return (phases/8)*std::min(shield,incoming*duration)+std::min(shield,incoming*std::min(duration,phases%8));};
    const double healing=helper&&helper->heal?3*helper->focus:0;
    const double firstHeal=std::min(healing,std::max(0.,incoming*2-absorbed(2)));
    const double cycleHeal=std::min(healing,std::max(0.,incoming*8-absorbed(8)));
    auto loss=[&](int phase){const int heals=healing>0?(phase+5)/8:0;return incoming*phase-absorbed(phase)-(heals>0?firstHeal+(heals-1)*cycleHeal:0);};
    // Loss only increases between scheduled heals; these endpoints certify
    // every prefix, including the camp phase immediately before healing.
    for(int phase=2;phase<=exposed;phase+=8)if(lead.hp-loss(phase)<=std::max(30.,incoming))return c;
    if(lead.hp-loss(exposed)<=std::max(30.,incoming))return c;
    if(helper&&helper->hp-helperIncoming*exposed<=std::max(30.,helperIncoming))return c;
    c.clear=elapsed;c.injury=std::max(0.,loss(exposed));
    c.firstBank=s.travel+c.clear+capture+1+s.altarTravel+ticks(s.gems,multiplier,base,minimum)+1;
    c.renewal=c.firstBank+s.spawnTravel+s.travel;
    if(c.firstBank>192||c.renewal>192)return c;
    c.feasible=true;c.secured=s.gems;c.gold=s.gold;c.purchases=shardCost>0?s.gold/shardCost:0;
    const int nextBank=std::max(c.firstBank+s.respawn,c.renewal)+c.clear+capture+1+s.altarTravel+ticks(s.gems,multiplier,base,minimum)+1;
    if(nextBank<=192){c.secured+=s.gems;c.gold+=s.gold;}
    return c;
}
template<class GetDesign>
inline Envelope evaluate(const OperatingPackage&p,const std::array<Source,6>&sources,int count,
                         GetDesign design,int capture=5,double mult=.1,int base=0,int minimum=1,int shard=30){
    Envelope out;out.packageId=p.id;out.selections=p.actors-(p.paired?1:0);
    std::array<bool,6> assigned{},busy{};
    for(int a=0;a<p.actors;++a){
        if(busy[a]||(p.paired&&a==0))continue;
        int h=-1;for(int j=0;j<p.actors;++j)if(p.duties[j].partner==a)h=j;
        if(p.paired&&a==1)h=0;
        const auto d=design(p.duties[a].kit);Design hd{};if(h>=0)hd=design(p.duties[h].kit);
        Circuit best;int index=-1;
        for(int g=0;g<std::min(6,count);++g)if(!assigned[g]){
            for(int mode=0;mode<2;++mode){
                for(int variant=0;variant<(h>=0&&!(p.paired&&a==1)?2:1);++variant){
                    const int support=variant==1?-1:h;
                    ++out.certificates;
                    auto candidate=certify(sources[g],d,support>=0?&hd:nullptr,a,support,mode,capture,mult,base,minimum,shard);
                    if(!candidate.feasible){++out.excluded;continue;}
                    if(!best.feasible||candidate.secured>best.secured||
                      (candidate.secured==best.secured&&(candidate.firstBank<best.firstBank||
                       (candidate.firstBank==best.firstBank&&candidate.helper<0&&best.helper>=0)))){best=candidate;index=g;}
                }
            }
        }
        if(index<0)continue;
        assigned[index]=true;out.circuits[out.circuitsUsed++]=best;
        busy[a]=true;if(best.helper>=0)busy[best.helper]=true;
        out.secured+=best.secured;out.gold+=best.gold;out.firstBank=std::min(out.firstBank,best.firstBank);out.exposure+=best.injury;
    }
    // First bank AND a useful productive fallback, not an actor-count bonus.
    out.feasible=out.circuitsUsed>0;
    for(int i=0;i<p.actors;++i)if(!busy[i])out.feasible=false;
    return out;
}
inline int select(const std::array<Envelope,4>& e,bool active,int frozen=0){
    if(!active)return std::clamp(frozen,0,3);
    int best=0;for(int i=1;i<4;++i)if(better(e[i],e[best]))best=i;
    return e[best].feasible?best:0;
}
struct RenewalTrace {
    int source=-1,gold=0,bag=0,banks=0;bool captured=false,purchased=false,returned=false;
    bool capture(int id,int gems,int income){if(captured||id<0||income<0)return false;source=id;gold=income;bag=gems;captured=true;return true;}
    bool buy(int cost){if(!captured||cost<=0||gold<cost)return false;gold-=cost;purchased=true;return true;}
    bool bank(int actual){if(!captured||actual!=bag||actual<=0)return false;bag=0;++banks;return true;}
    bool renew(int id){returned=purchased&&banks>0&&id==source;return returned;}
};
}
