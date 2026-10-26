#pragma once
#include "../../common/src/events.h"
namespace p11_strata {
struct Policy {
    static constexpr const char* name="p11-strata";
    static constexpr bool bounded_scan=true;
    static constexpr int window_size=3;
    static constexpr bool trace_scan=true;
    static bool admissible(const p11::State&s,const p11::Candidate&c) {
        if(p11::feasible(s,c)) return true;
        const float incoming=c.incoming*(s.jungle_shards>=5?.7f:1.f);
        return (s.hp>=s.maximum*.75f || c.committed) && s.hp>4*incoming+60 && c.clear<10000;
    }
    static int visits(const p11::State&s,const p11::Candidate&c) {
        return std::max(1,int(std::ceil(c.loss/std::max(1.f,s.hp*.60f))));
    }
    static int work(const p11::State&s,const p11::Candidate&c) {
        // Damage survives recovery. Charge repeated travel and recovery, but
        // never charge the same monster health again as if it had regenerated.
        return c.payout()+(visits(s,c)-1)*(s.recall+1+c.travel);
    }
    static double score(const p11::State&s,const p11::Candidate&c) {
        if(s.mature&&c.gems==0) return -std::numeric_limits<double>::infinity();
        const int ticks=work(s,c)+(c.gems>0?c.altar+p11::tribute_ticks(s.bag+c.gems):0);
        if(ticks>s.limit-s.round) return -std::numeric_limits<double>::infinity();
        const int spread=(c.id*3+s.identity*11)%7;
        return (2.*c.gems+(s.mature?.4:.8)*c.gold)/((1+ticks+spread)*(1.+c.allies))*(c.committed?1.25:1.);
    }
    static double screen(const p11::State&s,const p11::Candidate&c) {return score(s,c);}
    static bool bank(const p11::State&s) {return s.bag>0&&(s.bag>=24||s.altar<=3||s.limit-s.round<=s.altar+p11::tribute_ticks(s.bag)+8);}
    static bool retreat(float hp,float maximum,float incoming) {return hp<=maximum*.35f||hp<=incoming*2+60;}
};
}
