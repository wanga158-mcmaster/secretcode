#pragma once
#include <algorithm>
#include <cmath>

namespace h12_shield_window {
enum class Phase {prepare,work,withdraw,renew};
struct Observation {
    int round=0,source=-1,bag=0,duration=0,escape_steps=2;
    float hp=0,maximum=1,shield=0,predicted=0,incoming=0;
    bool visible=false,fresh=false,escape=false,forced=false,ready=false,cast=false,basic=false;
};
inline float reserve(const Observation&o) {return o.maximum*(o.bag>0?.38f:.26f)+std::max(2,o.escape_steps)*o.incoming;}
inline bool useful_window(const Observation&o,bool active) {
    // Shield is a separate expiring resource. Actual HP must independently
    // fund bounded exposed exit phases; no shield value is called healed HP.
    return o.visible&&o.fresh&&o.escape&&!o.forced&&o.basic&&o.incoming>0
        &&o.hp>reserve(o)&&(active?(o.duration>1&&o.shield>=o.incoming):
            (o.ready&&o.cast&&o.predicted>=o.incoming));
}
struct Window {
    Phase phase=Phase::prepare;
    int source=-1,started=-1,last_work=-1,progress=-1,withdrawn=-1,pulses=0;
    float last_hp=0;
    void start(int id,int round){source=id;phase=Phase::prepare;started=round;pulses=0;last_work=-1;}
    void exit(int round){phase=Phase::withdraw;withdrawn=round;}
    void outside(int round) {
        if(phase==Phase::withdraw){phase=Phase::renew;withdrawn=round;}
    }
    bool renewing()const{return phase==Phase::withdraw||phase==Phase::renew;}
};
struct Result {bool acted=false,cast=false,hit=false,withdraw=false,entered=false;};
// The adapter owns all live SDK checks; this controller owns the actual phase
// transitions. A cast is followed by a fresh buff observation, never its forecast.
// Each invocation permits one shield cast and one physical attack. A whole pulse
// lasts at most two distinct owner turns, even if progress keeps being made.
template<class World> Result execute(Window&window,World&w) {
    Result result;
    auto o=w.observe();
    // Once fresh complete vision proves no incoming damage at this work tile,
    // continue harmless physical work instead of waiting for another shield.
    // This cannot admit a hidden/stale source or spend a competing cast.
    if(o.visible&&o.fresh&&!o.forced&&o.basic&&o.incoming<=0) {
        if(window.last_work==o.round)return result;
        if(w.attack()) {
            result.acted=true;result.hit=w.progressed();
            window.phase=Phase::prepare;window.source=o.source;window.pulses=0;window.last_work=o.round;
        }
        return result;
    }
    if(window.phase==Phase::withdraw){result.withdraw=true;return result;}
    if(window.source!=o.source)window.start(o.source,o.round);
    if(window.phase==Phase::renew)window.phase=Phase::prepare;
    if(window.phase==Phase::prepare) {
        if(!useful_window(o,false)||!w.cast_shield()){result.withdraw=true;window.exit(o.round);return result;}
        result.acted=result.cast=true;
        o=w.observe();
        if(!useful_window(o,true)){result.withdraw=true;window.exit(o.round);return result;}
        window.phase=Phase::work;window.started=o.round;window.pulses=0;
        window.last_work=-1;window.progress=o.round;result.entered=true;
    }
    if(!useful_window(o,true)||window.pulses>=2||o.round-window.started>=2||
        (window.progress>=0&&o.round-window.progress>1)) {
        window.exit(o.round);result.withdraw=true;return result;
    }
    if(window.last_work==o.round)return result;
    if(!w.attack()){window.exit(o.round);result.withdraw=true;return result;}
    result.acted=true;result.hit=w.progressed();
    ++window.pulses;window.last_work=o.round;
    if(result.hit)window.progress=o.round;
    else {window.exit(o.round);result.withdraw=true;}
    return result;
}
inline float camp_damage(float strength,float bonus,int identity,float hp,float maximum) {
    // SDK Strength is the live item/passive stat. Heart's outgoing damage
    // modifier is not Strength; its passive uses actual missing HP only.
    const float heart=identity==12?1+.5f*(1-std::clamp(hp/std::max(1.f,maximum),0.f,1.f)):1;
    return strength*(1+bonus)*heart;
}
inline bool execution_lease(int round,int progress,int capture,bool admitted){return admitted&&round-progress<capture;}
} // namespace h12_shield_window
