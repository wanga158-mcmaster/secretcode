#pragma once
#include <cstdint>
#include "portfolio.h"
namespace h12_capability_portfolio {
enum class Phase {Ordinary,Approach,Cross,Clear,Return,Bank,Buy,Continue,Complete,Refused};
enum class WorkResult {Working,Captured,Refused};
enum class BuyResult {Bought,Wait,Refused};
inline bool has_action(bool move,bool attack,bool cast){return move||attack||cast;}
inline bool cast_compatible(bool ready,bool exclusive,bool walked){return ready&&!(exclusive&&walked);}
inline bool capture_receipt(bool held_last_turn,bool source_respawned,int old_gold,int old_bag,int gold,int bag){
    return held_last_turn&&source_respawned&&(gold>old_gold||bag>old_bag);
}
inline bool defend_allowed(bool enemy_spawn,int round,int retry_after){return !enemy_spawn&&round>=retry_after;}
class RouteWatch {
    int source_=-1,best_=100000,last_=0;
    Point goal_{-1,-1};
public:
    void reset(){source_=-1;best_=100000;goal_={-1,-1};}
    void adopt(int source,int round){if(source_!=source){source_=source;goal_={-1,-1};best_=100000;last_=round;}}
    void measured(Point goal,int walking_distance,int round){
        if(walking_distance<0)return;
        if(goal!=goal_){goal_=goal;best_=walking_distance;last_=round;}
        else if(walking_distance<best_){best_=walking_distance;last_=round;}
    }
    bool stale(int round,int no_work_age)const{return no_work_age>=60&&round-last_>=8;}
};
class Renewal {
    int attempts_=0,next_=0;
public:
    void useful_work(int round){attempts_=0;next_=round+180;}
    bool due(int round,int no_work_age,bool on_spawn,bool exposed)const{
        return attempts_<2&&round>=next_&&no_work_age>=180&&!on_spawn&&!exposed;
    }
    void begin(int round){++attempts_;next_=round+180;}
    int attempts()const{return attempts_;}
};
#if PORTFOLIO_CARGO_RENEWAL
// A loaded ordinary worker cannot use empty-bag recovery. Give its real cargo
// one bounded conversion attempt before returning to independent production.
class CargoRenewal {
    int until_=0,next_=0;
public:
    bool active(int round)const{return round<until_;}
    bool tick(int round,int no_work_age,int bag,bool program_active){
        if(bag<=0){until_=0;return false;}
        if(active(round))return true;
        if(program_active||round<next_||no_work_age<180)return false;
        until_=round+96;next_=round+180;return true;
    }
};
#endif
inline bool component_admitted(std::uint64_t remaining){return remaining>=80000;}
inline bool component_needs_refresh(bool ready,bool inside){return !ready||!inside;}
inline bool cancel_for_exposure(bool unsafe,float incoming){return unsafe&&incoming>0;}
inline int quarantine_after_refusal(int round){return round+48;}

// Tiny role interpreter. World methods are the production adapter used by the
// native bot and focused fixtures. No teammate report or cooperative barrier.
class Program {
    Role role_=Role::Ordinary;
    Phase phase_=Phase::Ordinary;
    int started_=0,phase_started_=0,attempts_=0;
public:
    Phase phase()const{return phase_;}
    Role role()const{return role_;}
    void start(Role role,int round){role_=role;phase_=role==Role::Access?Phase::Approach:role==Role::Supply?Phase::Clear:Phase::Ordinary;started_=phase_started_=round;attempts_=0;}
    bool active()const{return phase_!=Phase::Ordinary&&phase_!=Phase::Complete&&phase_!=Phase::Refused;}
    void refuse(){phase_=Phase::Refused;}
    template<class World> bool tick(World&w) {
        if(!active())return false;
        if(!w.permitted())return true;
        const int round=w.round();
        auto enter=[&](Phase p){phase_=p;phase_started_=round;attempts_=0;};
        auto abort=[&](){phase_=Phase::Refused;w.exit();return true;};
        if(w.unsafe()||round-started_>160)return abort();
        if(phase_==Phase::Approach) {
            if(!w.admit())return abort();
            if(w.at_entry()){enter(Phase::Cross);return true;}
            if(round-phase_started_>96)return abort();
            w.approach();return true;
        }
        if(phase_==Phase::Cross) {
            if(!w.admit()||round-phase_started_>10||attempts_>=3)return abort();
            if(!w.cross_ready())return true;
            ++attempts_;
            if(w.cross(false))enter(Phase::Clear);
            return true;
        }
        if(phase_==Phase::Clear) {
            if(round-phase_started_>80)return abort();
            if(role_==Role::Supply&&!w.threshold())return abort();
            const auto result=w.clear();
            if(result==WorkResult::Refused)return abort();
            if(result==WorkResult::Captured)enter(role_==Role::Access?Phase::Return:Phase::Buy);
            return true;
        }
        if(phase_==Phase::Return) {
            if(round-phase_started_>24)return abort();
            if(!w.at_landing()){w.return_approach();return true;}
            if(!w.cross_ready())return true;
            if(++attempts_>3)return abort();
            if(w.cross(true))enter(Phase::Bank);
            return true;
        }
        if(phase_==Phase::Buy) {
            if(round-phase_started_>6)return abort();
            const auto result=w.field_buy();
            if(result==BuyResult::Bought){if(!w.refresh_capacity())return abort();enter(Phase::Continue);}
            else if(result==BuyResult::Refused)return abort();
            return true;
        }
        if(phase_==Phase::Continue) {
            if(w.useful_work()||round-phase_started_>12)enter(Phase::Bank);
            return true;
        }
        if(phase_==Phase::Bank) {
            if(w.bag()==0){enter(Phase::Complete);return false;}
            if(round-phase_started_>60)return abort();
            w.bank();return true;
        }
        return false;
    }
};
}
