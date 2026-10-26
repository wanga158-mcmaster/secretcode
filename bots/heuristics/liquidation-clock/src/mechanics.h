#pragma once
#include <cstdint>
#include <vector>
#include <algorithm>
namespace h12_liquidation_clock {
struct Point {int x,y;friend bool operator==(Point,Point)=default;};
inline bool has_action(bool move,bool attack,bool cast){return move||attack||cast;}
inline bool cast_compatible(bool ready,bool exclusive,bool walked){return ready&&!(exclusive&&walked);}
inline bool capture_receipt(bool held,bool respawn,int old_gold,int old_bag,int gold,int bag){return held&&respawn&&(gold>old_gold||bag>old_bag);}
inline bool defend_allowed(bool enemy_spawn,int round,int retry_after){return !enemy_spawn&&round>=retry_after;}
inline bool component_admitted(std::uint64_t remaining){return remaining>=80000;}
inline bool component_needs_refresh(bool ready,bool inside){return !ready||!inside;}
inline bool cancel_for_exposure(bool unsafe,float incoming){return unsafe&&incoming>0;}
inline int quarantine_after_refusal(int round){return round+48;}
// Conversion routes may leave an existing exposure, but never enter a new
// observed attack footprint or move closer to its owner. No unseen threat is
// inferred and no health floor is financed with future recovery.
inline int terminal_exclusion_radius(int distance,int range){return std::min(range,std::max(0,distance-1));}
#if LIQUIDATION_CAMP_MEMORY
// Losing sight of a personally observed camp does not make its tile vacant.
// Only a fresh personal view can clear that remembered obstruction.
template<class Board> class CampMemory {
    Board occupied_{};
public:
    const Board& refresh(const Board& visible,const Board& occupied){
        occupied_&=~visible;
        occupied_|=occupied&visible;
        return occupied_;
    }
};
#endif
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
struct WorkResult {bool accepted=false,acted=false;};
inline int respawn_quarantine(int round,int timer){return round+std::max(12,timer);}
// A visible respawning source is unavailable work. Timer zero can persist
// indefinitely under an unseen pad occupant; do not reselect it every turn.
// One alternate intent may HOLD productively without issuing an action, while
// a successful physical action survives a later rejected intent.
template<class World> WorkResult handle_respawn_refusal(World&w,int source,bool retry){
    w.quarantine_respawn(source);
    if(w.on_respawn_pad(source)&&w.depart_respawn_pad(source))return {true,true};
    if(retry){const auto result=w.alternate_respawn_work();if(result.accepted||result.acted)return result;}
    const bool acted=w.recover_after_respawn();return {acted,acted};
}
}
