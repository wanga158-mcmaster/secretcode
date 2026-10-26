#pragma once
#include <array>
#include <algorithm>

namespace p10_initiative {
enum class Dispatch { circulation=0,source=1,loaded=2,completion=3,protection=4 };
constexpr int priority(Dispatch category,int stableRole){
    return stableRole>=0&&stableRole<=15?16*int(category)+(15-stableRole):-1;
}
struct Slot {int round=0,phase=0,index=0;};
inline bool before(Slot a,Slot b){if(a.round!=b.round)return a.round<b.round;if(a.phase!=b.phase)return a.phase<b.phase;return a.index<b.index;}
struct Worker {
    int id=-1,role=0,current=0,next=0,issue=-1,bag=0,travel=0,service=0;
    bool visible=false,channelActive=false,safe=false;
};
struct Event {
    int actor=-1,earliest=0,delay=0,predecessor=-1,secondPredecessor=-1;
    bool campPhase=false,ownerStart=true;Slot at{};
};
struct SlotProgram {
    std::array<Worker,5> workers{};int count=0,now=0;
    bool ownIsTeamB=false,ourTeamStarts=false,prerequisites=false;
    std::array<Event,8> events{};int eventsUsed=0;
    int holder=-1,originalHolder=-1,secured=0,personalGold=0,bankEvent=-1,renewEvent=-1;
    int displacedUsefulTurns=0;bool legal=false;
};
inline bool team_starts(const SlotProgram&p,int round){return p.ourTeamStarts==((round-p.now)%2==0);}
inline int rank(const SlotProgram&p,int actor,int round,bool active){
    const auto&w=p.workers[actor];int ours=0;
    auto value=[&](const Worker&a){return active&&a.issue<round?a.next:a.current;};
    for(int i=0;i<p.count;++i)if(i!=actor&&(value(p.workers[i])>value(w)||
        (value(p.workers[i])==value(w)&&p.workers[i].role<w.role)))++ours;
    return 2*ours+(team_starts(p,round)?0:1);
}
inline Slot at_turn(const SlotProgram&p,int actor,int round,bool active){return {round,1,rank(p,actor,round,active)};}
inline bool solve(SlotProgram&p,bool active){
    if(p.count<=0||p.count>5||p.eventsUsed>8||p.eventsUsed<=0||
       p.holder<0||p.holder>=p.count||!p.workers[p.holder].safe)return false;
    if(active&&!p.prerequisites)return false;
    for(int e=0;e<p.eventsUsed;++e){
        auto&v=p.events[e];int r=std::max(p.now,v.earliest);
        const int preds[2]={v.predecessor,v.secondPredecessor};
        for(int predecessor:preds){
            if(predecessor<0)continue;
            if(predecessor>=e)return false; // bounded acyclic handwritten program
            const Slot previous=p.events[predecessor].at;
            r=std::max(r,previous.round+v.delay);
            Slot candidate=v.campPhase?Slot{r,0,0}:at_turn(p,v.actor,r,active);
            if(!before(previous,candidate))++r;
        }
        v.at=v.campPhase?Slot{r,0,0}:at_turn(p,v.actor,r,active);
        if(r-p.now>48)return false;
    }
    p.legal=p.bankEvent>=0&&p.renewEvent>=0&&
        p.events[p.bankEvent].at.round-p.now<=48&&p.events[p.renewEvent].at.round-p.now<=48;
    return p.legal;
}
inline bool better(const SlotProgram&a,const SlotProgram&b){
    if(a.legal!=b.legal)return a.legal;
    if(a.secured!=b.secured)return a.secured>b.secured;
    if(a.personalGold!=b.personalGold)return a.personalGold>b.personalGold;
    const auto ab=a.events[a.bankEvent].at,bb=b.events[b.bankEvent].at;
    if(before(ab,bb))return a.displacedUsefulTurns<=b.displacedUsefulTurns;
    if(before(bb,ab))return false;
    return before(a.events[a.renewEvent].at,b.events[b.renewEvent].at)&&
           a.displacedUsefulTurns<=b.displacedUsefulTurns;
}
struct Commitment {
    int current=0,next=0,issue=-100,lastChange=-100;Dispatch category=Dispatch::circulation;
    bool set(int round,int role,Dispatch wanted,bool channelActive,bool prerequisite,bool safety){
        if(channelActive||role<0||role>15)return false;
        if(!prerequisite&&int(wanted)>=int(Dispatch::completion))wanted=Dispatch::loaded;
        const int value=priority(wanted,role);
        if(value==next)return false;
        if(!safety&&round-lastChange<2)return false;
        current=next;next=value;issue=round;lastChange=round;category=wanted;return true;
    }
};
struct Trace {
    int gold=0,bag=0,score=0;bool bought=false,renewed=false;Slot captured{},banked{};
    bool capture(Slot at,int gems,int income){if(at.phase!=0||gems<=0)return false;captured=at;bag=gems;gold=income;return true;}
    bool buy(int cost){if(cost<=0||gold<cost)return false;gold-=cost;bought=true;return true;}
    bool bank(Slot at,int actual){if(!before(captured,at)||actual!=bag||actual<=0)return false;score+=actual;bag=0;banked=at;return true;}
    bool renew(Slot at){renewed=bought&&score>0&&before(banked,at);return renewed;}
};
}
