#pragma once
#include <cstdint>
#include <vector>
#include <algorithm>
namespace h12_counterpunch {
struct Point {int x,y;friend bool operator==(Point,Point)=default;};
inline bool has_action(bool move,bool attack,bool cast){return move||attack||cast;}
inline bool cast_compatible(bool ready,bool exclusive,bool walked){return ready&&!(exclusive&&walked);}
inline bool capture_receipt(bool held,bool respawn,int old_gold,int old_bag,int gold,int bag){return held&&respawn&&(gold>old_gold||bag>old_bag);}
inline bool defend_allowed(bool enemy_spawn,int round,int retry_after){return !enemy_spawn&&round>=retry_after;}
inline bool component_admitted(std::uint64_t remaining){return remaining>=80000;}
inline bool component_needs_refresh(bool ready,bool inside){return !ready||!inside;}
inline bool cancel_for_exposure(bool unsafe,float incoming){return unsafe&&incoming>0;}
inline int quarantine_after_refusal(int round){return round+48;}
inline bool rest_after_arrival(bool spawn,bool arrival,bool missing_hp){return spawn&&arrival&&missing_hp;}
class SpawnRest {
    bool previous_=false;int arrival_=-1;
public:
    void death(){previous_=false;}
    bool entry(int round,bool spawn,bool missing_hp){
        if(spawn&&!previous_)arrival_=round;
        previous_=spawn;
        return rest_after_arrival(spawn,round==arrival_,missing_hp);
    }
};
class PaidCapture {
    int source_=-1,round_=-100,gold_=0,bag_=0;
public:
    int source()const{return source_;}
    void reset(){source_=-1;round_=-100;}
    void expect(int source){source_=source;round_=-100;}
    void completed(int source){if(source==source_)reset();}
    void hold(int source,int round,int gold,int bag){if(source==source_){round_=round;gold_=gold;bag_=bag;}}
    bool possible(int round,int gold,int bag)const{return source_>=0&&round_==round-1&&(gold>gold_||bag>bag_);}
    bool receipt(int round,bool respawn,int gold,int bag){
        if(!possible(round,gold,bag)||!respawn)return false;
        reset();return true;
    }
};
class RouteWatch {
    int source_=-1,best_=100000,last_=0;Point goal_{-1,-1};
public:
    void reset(){source_=-1;best_=100000;goal_={-1,-1};}
    void adopt(int source,int round){if(source_!=source){source_=source;goal_={-1,-1};best_=100000;last_=round;}}
    void measured(Point goal,int distance,int round){if(distance<0)return;if(goal!=goal_){goal_=goal;best_=distance;last_=round;}else if(distance<best_){best_=distance;last_=round;}}
    bool stale(int round,int age)const{return age>=60&&round-last_>=8;}
};
class Renewal {
    int attempts_=0,next_=0;
public:
    void useful_work(int round){attempts_=0;next_=round+180;}
    bool due(int round,int age,bool spawn,bool exposed)const{return attempts_<2&&round>=next_&&age>=180&&!spawn&&!exposed;}
    void begin(int round){++attempts_;next_=round+180;}
    int attempts()const{return attempts_;}
};
struct Opportunity {
    float health=0,basic=0,stun=0,retaliation=0,hp=0,maximum=1,incoming=0;
    bool alive=false,awake=false,stunned=false,shielded=false,adjacent=false,damaging=false;
    bool basic_ready=false,stun_now=false,stun_next=false,escape=false,competitor=false,enemy=false;
    int bag=0;
};
inline bool admit(const Opportunity&o){
    const float cleanup=o.basic+(o.stun_next?o.stun:0),left=o.health-o.basic;
    return o.alive&&o.awake&&o.damaging&&!o.stunned&&!o.shielded&&o.adjacent&&o.basic_ready
        &&o.incoming>0&&o.retaliation>0&&o.escape&&!o.competitor&&!o.enemy
        &&o.hp>o.maximum*(o.bag?.55f:.4f)+2*o.incoming
        &&o.health>o.basic+(o.stun_now?o.stun:0)
        &&left>cleanup&&left<=cleanup+o.retaliation;
}
struct EscapeFacts {bool move=false,dash=false,visible=false,walk_free=false,interior_free=false,landing_free=false,outside=false;int cooldown=0,range=2;};
inline bool escape_admitted(const EscapeFacts&e,bool execute){return e.move&&e.dash&&e.visible&&e.walk_free&&e.interior_free&&e.landing_free&&e.outside&&e.range>=2&&e.cooldown<=(execute?0:1);}
inline bool cleanup_admitted(const Opportunity&o){return o.alive&&o.adjacent&&o.basic_ready&&!o.shielded&&!o.enemy
    &&o.hp>o.incoming+o.maximum*(o.bag?.55f:.4f)&&o.health<=o.basic+(o.stun_now?o.stun:0);}
enum class CounterResult {None,Basic,Armed,Cleanup,Withdraw,Dead};
inline bool counter_acted(CounterResult r){return r==CounterResult::Basic||r==CounterResult::Armed;}
// The same production caller is driven by SDK and fixture adapters. One camp
// identity remains spent until its observed life changes; an endpoint HP fall
// is recorded as unattributed work, never silently called retaliation.
class Counter {
    int target_=-1,round_=-1;std::vector<int> spent_;float after_=0;bool pending_=false;
public:
    bool pending()const{return pending_;}int target()const{return target_;}float after()const{return after_;}
    void reset(){pending_=false;target_=-1;}
    void renewed(int id){spent_.erase(std::remove(spent_.begin(),spent_.end(),id),spent_.end());}
    template<class World> CounterResult arm(World&w,int id){
        if(pending_||std::find(spent_.begin(),spent_.end(),id)!=spent_.end()||!admit(w.opportunity()))return CounterResult::None;
        if(!w.basic())return CounterResult::None;
        // Real post-action HP and allowances are authoritative.
        const auto o=w.opportunity();const float capacity=o.basic+(o.stun_next?o.stun:0);
        if(!o.alive||o.health<=capacity||o.health>capacity+o.retaliation)return CounterResult::Basic;
        target_=id;spent_.push_back(id);round_=w.round();after_=o.health;pending_=true;return CounterResult::Armed;
    }
    template<class World> CounterResult cleanup(World&w){
        if(!pending_)return CounterResult::None;
        pending_=false;
        if(w.round()!=round_+1||!w.visible()||!w.same_life()){w.withdraw();return CounterResult::Withdraw;}
        if(!w.alive())return CounterResult::Dead;
        if(!w.cleanup_available()){w.withdraw();return CounterResult::Withdraw;}
        w.basic();
        if(w.alive())w.stun();
        if(w.alive()){w.withdraw();return CounterResult::Withdraw;}
        return CounterResult::Cleanup;
    }
};
}
