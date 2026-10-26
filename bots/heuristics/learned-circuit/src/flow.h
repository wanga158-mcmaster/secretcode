#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include "config.h"
#include "../../common/src/decision.h"

namespace h12_learned_circuit {
// Return for a complete next equipment phase, not an arbitrary wallet balance.
// The caller supplies the queried shard price and combination count. Once both
// opening phases are complete, the incumbent 300-gold return remains in force.
inline bool capital_phase_due(unsigned gold,unsigned owned,unsigned combination,unsigned price,bool mature) {
    if(mature)return gold>=300;
    if(price==0||combination<=owned)return false;
    return static_cast<unsigned long long>(gold)>=
        static_cast<unsigned long long>(combination-owned)*price;
}
enum class Stage {visit,hold,bank,recover};
struct Stop {
    int id=-1,failures=0,payouts=0,last_paid=-1,due=0;
#if CIRCUIT_YIELD_RENEWAL
    struct Paid {int gems=0,gold=0,elapsed=0;};
    std::array<Paid,2> paid{};
    int measured=0,nominal_gems=0,nominal_gold=0;
#endif
};
// Private program, advanced by witnessed outcomes rather than re-ranked each
// turn. Two stops deliberately remain a small learnable circuit.
struct Circuit {
    std::array<Stop,2> stops{};
    Stage stage=Stage::visit,resume=Stage::visit,recovery_resume=Stage::visit;
    int cursor=0,count=0,laps=0,profitable_laps=0,lap_payouts=0;
    int begun=-1,progress=-1,best_distance=10000,detour=-1;
    int paused=-1,recoveries=0;
    float best_hp=1e30f;
    bool boundary=true,replaced=false,probed=false,end_bank=false;
    int replacements=0,total_payouts=0,total_banks=0;
#if CIRCUIT_YIELD_RENEWAL
    std::array<int,2> paid_lap_ticks{};
    int paid_laps=0,lap_begun=-1,yield_replacements=0;
#if CIRCUIT_EARNED_RENEWAL
    bool renewal_earned=false;
#endif
    static int renewal_role(int identity) {
        // Same five bodies as the roster. Different private paid-lap phases
        // release one dissatisfied producer before its peers follow it.
        constexpr std::array identities{1,7,11,9,5};
        for(int i=0;i<int(identities.size());++i)if(identities[i]==identity)return i;
        return -1;
    }
    static double paid_reward(const heuristics::State&s,int gems,int gold) {
        return 2.0*gems+gold*(s.mature?.08:.4);
    }
    bool poor_paid_stop(const heuristics::State&s,const Stop&stop)const {
        if(stop.measured<2)return false;
        const double nominal=paid_reward(s,stop.nominal_gems,stop.nominal_gold);
        // Integer splitting can give each of two holders 9 of 19 gems. That
        // is a fair half share, not evidence that a majority keeps our reward.
        const double rounding=2.0+(s.mature?.08:.4);
        return nominal>0&&2*paid_reward(s,stop.paid[0].gems,stop.paid[0].gold)+rounding<nominal&&
            2*paid_reward(s,stop.paid[1].gems,stop.paid[1].gold)+rounding<nominal;
    }
    bool renewal_window(const heuristics::State&s)const {
#if CIRCUIT_EARNED_RENEWAL
        const int role=renewal_role(s.identity);
        if(!boundary||count!=2||paid_laps<2||role<0||(!renewal_earned&&role!=paid_laps%5))return false;
#else
        if(!boundary||count!=2||paid_laps<2||renewal_role(s.identity)!=paid_laps%5)return false;
#endif
        return poor_paid_stop(s,stops[0])||poor_paid_stop(s,stops[1]);
    }
    void renew_paid_stop(const heuristics::State&s,std::span<const heuristics::Job>jobs) {
        if(replaced)return;
        int worst=-1;double incumbent=1e30;
        for(int i=0;i<count;++i)if(poor_paid_stop(s,stops[i])) {
            const auto&stop=stops[i];
            const double own=(paid_reward(s,stop.paid[0].gems,stop.paid[0].gold)+
                              paid_reward(s,stop.paid[1].gems,stop.paid[1].gold))*.5;
            const double visit=(stop.paid[0].elapsed+stop.paid[1].elapsed)*.5;
            // Split actual full-lap overhead between the two retained visits.
            const double elapsed=std::max(visit,(paid_lap_ticks[0]+paid_lap_ticks[1])*.25);
            const double rate=own/std::max(1.0,elapsed);
            if(rate<incumbent){incumbent=rate;worst=i;}
        }
        if(worst<0)return;
        int outside=-1;double best=incumbent;
        for(const auto&j:jobs) {
            if(contains(j.id)||!j.affordable||j.allies>0||j.gems+j.gold<=0)continue;
            const double cycle=heuristics::cycle(s,j);
            if(cycle<=0||s.limit-s.round<=cycle+5)continue;
            // Discount a forecast rather than assuming unseen solitude.
            const double rate=.5*paid_reward(s,j.gems,j.gold)/cycle;
            if(rate>best){best=rate;outside=j.id;}
        }
        if(outside>=0) {
            stops[worst]=Stop{outside};replaced=true;++replacements;++yield_replacements;
        }
    }
    void measured_payout(int id,int round,int bag,int respawn,int gems,int gold,int nominal_gems,int nominal_gold) {
        if(id==current()&&detour<0&&cursor<count) {
            auto&stop=stops[cursor];
            stop.paid[stop.measured%2]={gems,gold,std::max(1,round-begun)};
            ++stop.measured;stop.nominal_gems=nominal_gems;stop.nominal_gold=nominal_gold;
        }
        payout(id,round,bag,respawn);
    }
#endif
    int current()const{return detour>=0?detour:cursor<count?stops[cursor].id:-1;}
    bool contains(int id)const {for(int i=0;i<count;++i)if(stops[i].id==id)return true;return false;}
    void visit(int round){begun=progress=round;paused=-1;recoveries=0;best_distance=10000;best_hp=1e30f;stage=Stage::visit;}
    static double merit(const heuristics::State&s,const heuristics::Job&j) {
        const double share=1+std::max(0,j.allies);
        const double income=(2*j.gems+j.gold*(s.mature?.08:.4))/share;
        return income/(4+j.travel+j.work+s.capture+j.altar)-.02*std::max(0,j.age-30);
    }
    int candidate(const heuristics::State&s,std::span<const heuristics::Job>jobs,int avoid=-1)const {
        int best=-1;double value=-1e30;
        for(const auto&j:jobs) {
            if(j.id==avoid||contains(j.id)||j.gems+j.gold<=0)continue;
            if(!j.affordable&&s.hp<s.maximum*.75f)continue;
            if(s.limit-s.round<=j.travel+std::min(60.f,j.work)+s.capture+j.altar+5)continue;
            const double v=merit(s,j);if(v>value){value=v;best=j.id;}
        }
        return best;
    }
    void plan(const heuristics::State&s,std::span<const heuristics::Job>jobs) {
        if(!boundary&&current()>=0)return;
#if CIRCUIT_YIELD_RENEWAL
        const bool renew=renewal_window(s);
#if CIRCUIT_EARNED_RENEWAL
        // An unsuccessful quote spends no replacement. Preserve that private
        // opportunity while the same paid history still warrants it; ordinary
        // visits continue until a later normal boundary can execute safely.
        if(boundary)renewal_earned=renew;
#endif
#endif
#if CIRCUIT_BANK_BOUNDARY
        // The one optional detour cannot erase a completed route's bank.
        // A refused bank walk may re-enter planning; retain that obligation.
        if(!boundary&&end_bank&&cursor>=count&&probed&&detour<0) {
            if(s.bag>0){stage=Stage::bank;return;}
            finish(s.round);
        }
#endif
        if(boundary){replaced=false;probed=false;cursor=0;detour=-1;boundary=false;
#if CIRCUIT_YIELD_RENEWAL
            lap_begun=s.round;
#endif
        }
        // Win-stay retains personally productive stops. Lose-shift replaces at
        // most one twice-failed stop per lap, never a transient blocked step.
        for(int i=0;i<count&&!replaced;++i)if(stops[i].id<0||stops[i].failures>=2) {
            const int id=candidate(s,jobs);if(id>=0){stops[i]=Stop{id};replaced=true;++replacements;}
        }
#if CIRCUIT_YIELD_RENEWAL
        if(renew)renew_paid_stop(s,jobs);
#if CIRCUIT_EARNED_RENEWAL
        if(replaced)renewal_earned=false;
#endif
#endif
        while(count<2) {const int id=candidate(s,jobs);if(id<0)break;stops[count++]=Stop{id};}
        // Known sleeping/temporarily unavailable stops were not admitted by
        // the runtime. One productive probe can fill that gap this lap while
        // preserving the ordered, personally learned stops.
        bool offered=false;for(const auto&j:jobs)offered|=j.id==current();
        if(!offered&&!probed) {const int id=candidate(s,jobs);if(id>=0){detour=id;probed=true;}}
        visit(s.round);
    }
    void finish(int round) {
#if CIRCUIT_YIELD_RENEWAL
        if(lap_payouts>0&&lap_begun>=0)paid_lap_ticks[paid_laps++%2]=std::max(1,round-lap_begun);
        lap_begun=-1;
#endif
        ++laps;if(lap_payouts>0)++profitable_laps;lap_payouts=0;
        boundary=true;cursor=0;detour=-1;end_bank=false;visit(round);
    }
    void next(int round,int bag) {
        if(detour>=0){detour=-1;
#if CIRCUIT_BANK_BOUNDARY
            if(end_bank&&cursor>=count) {
                stage=Stage::bank;if(bag<=0)finish(round);return;
            }
#endif
            visit(round);return;}
        ++cursor;
        if(cursor>=count){end_bank=true;stage=Stage::bank;if(bag<=0)finish(round);}
        else visit(round);
    }
    void payout(int id,int round,int bag,int respawn) {
        if(id!=current())return;
        ++total_payouts;++lap_payouts;
        if(detour<0){auto&stop=stops[cursor];++stop.payouts;stop.failures=0;stop.last_paid=round;stop.due=round+respawn;}
        next(round,bag);
    }
    void failed(int id,int round,int bag) {
        if(id!=current())return;
        if(detour<0)++stops[cursor].failures;
        next(round,bag);
    }
    // A witnessed renewal is a deferred visit, not evidence of bad productivity.
    void sleeping(int id,int round,int bag,int due) {
        if(id!=current())return;
        if(detour<0)stops[cursor].due=due;
        next(round,bag);
    }
    void interrupt(bool recovery,int round) {
        if(stage==Stage::recover)return;
        if(recovery){if(stage!=Stage::bank)++recoveries;recovery_resume=stage;stage=Stage::recover;if(paused<0)paused=round;}
        else if(stage!=Stage::bank){resume=stage;end_bank=false;stage=Stage::bank;if(paused<0)paused=round;}
    }
    void resume_lease(int round) {
        if(paused>=0&&begun>=0)begun+=std::max(0,round-paused);
        paused=-1;progress=round;best_distance=10000;best_hp=1e30f;
    }
    void recovered(int round) {
        if(stage!=Stage::recover)return;
        stage=recovery_resume;
        // Recovery inside a bank journey cannot resume the visit prematurely.
        if(stage==Stage::visit||stage==Stage::hold)resume_lease(round);
    }
    void banked(int round) {
        ++total_banks;
        if(end_bank)finish(round);else if(stage==Stage::bank){stage=resume;resume_lease(round);}
    }
    void died(int round) {
#if CIRCUIT_YIELD_RENEWAL
        lap_begun=-1;
#endif
        // A lost bag ends an interrupted attempt, not a profitable completed
        // lap. Personally learned stops survive; exhausted cursors do not.
        const bool work=stage==Stage::visit||stage==Stage::hold||
            (stage==Stage::recover&&(recovery_resume==Stage::visit||recovery_resume==Stage::hold));
        if(work&&detour<0&&cursor<count)++stops[cursor].failures;
        cursor=0;boundary=true;detour=-1;replaced=probed=end_bank=false;lap_payouts=0;
        resume=recovery_resume=Stage::visit;visit(round);stage=Stage::recover;
    }
    void observe_progress(int round,int distance,float hp) {
        if(distance<best_distance||hp<best_hp)progress=round;
        best_distance=std::min(best_distance,distance);best_hp=std::min(best_hp,hp);
    }
    bool expired(int round)const{return begun>=0&&(recoveries>2||round-begun>=120||(progress>=0&&round-progress>=12));}
    void holding(){if(stage==Stage::visit)stage=Stage::hold;}
};

// Own financial/lifecycle receipt. Sample before shopping, save the baseline
// after actions. A kill or predicted renewal alone cannot teach a paid stop.
struct Receipts {
    int bag=0,gold=0,held=-1,submit_round=-1,submit_bag=0,submit_ticks=0;
    bool initialized=false,alive=true,continuous=false,reward=false;
    struct Gain {
        bool payout=false,bank=false;int source=-1;
#if CIRCUIT_YIELD_RENEWAL
        int own_gems=0,own_gold=0;
#endif
    };
    Gain sample(int round,int now_bag,int now_gold,bool living,bool submitting,
        int on_source,bool reward_available,bool respawning) {
        Gain g;
        if(initialized) {
            g.source=held;
            g.payout=living&&alive&&continuous&&reward&&held>=0&&on_source==held&&respawning&&submit_round<0&&
                (now_bag>bag||now_gold>gold);
#if CIRCUIT_YIELD_RENEWAL
            if(g.payout){g.own_gems=std::max(0,now_bag-bag);g.own_gold=std::max(0,now_gold-gold);}
#endif
            g.bank=living&&alive&&submit_round>=0&&round>=submit_round+submit_ticks&&
                !submitting&&now_bag<bag&&submit_bag>0;
        }
        if(!living||!alive){submit_round=-1;continuous=false;reward=false;held=-1;}
        if(!submitting&&submit_round>=0&&!g.bank&&round>submit_round)submit_round=-1;
        if(g.bank)submit_round=-1;
        held=living?on_source:-1;continuous=living&&on_source>=0;
        reward=living&&reward_available;
        bag=now_bag;gold=now_gold;alive=living;initialized=true;
        return g;
    }
    void after(int now_bag,int now_gold,bool living,int on_source,bool reward_available) {
        bag=now_bag;gold=now_gold;alive=living;held=living?on_source:-1;
        continuous=living&&on_source>=0;reward=living&&reward_available;
    }
    void submit(int round,int gems,double multiplier) {
        submit_round=round;submit_bag=gems;submit_ticks=std::max(1,int(std::ceil(gems*multiplier)));
    }
};
template<class Walk,class Work>bool leave_or_work(Walk walk,Work work){return walk()||work();}
} // namespace h12_learned_circuit
