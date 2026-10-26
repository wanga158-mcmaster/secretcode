#pragma once
#include "../../common/src/events.h"
namespace p11_harvest {
struct Policy {
    static constexpr const char* name="p11-harvest";
    static constexpr bool bounded_scan=true;
    static constexpr int window_size=3;
    static constexpr bool trace_scan=true;
    static constexpr bool renewal_model=true;
    static int harvests(const p11::State&s,const p11::Candidate&c) {
        const int first=c.gems>0?c.bank_time(s):c.payout();
        const int remaining=s.limit-s.round-first;
        if(remaining<0) return 0;
        // Conditional exclusive-source forecast. Release the spawn after each
        // payout; occupying it would invalidate this renewal lattice.
        // Respawn and the altar round trip happen concurrently. Their maximum,
        // rather than their sum, determines the next possible clear cycle.
        const int bank_trip=c.gems>0?2*c.altar+p11::tribute_ticks(c.gems,s.tribute_multiplier,s.tribute_base,s.tribute_minimum):0;
        const int cycle=std::max(1,c.clear+c.capture+std::max(c.renewal+1,bank_trip));
        return 1+remaining/cycle;
    }
    static double score(const p11::State&s,const p11::Candidate&c) {
        if(s.mature && c.gems==0) return -std::numeric_limits<double>::infinity();
        const int n=harvests(s,c);
        if(!n) return -std::numeric_limits<double>::infinity();
        const double shared=1.+c.allies;
        const double output=(2.*c.gems+.4*c.gold)*n/shared;
        const int spread=(c.id*3+s.identity*11)%7;
        return output/(1+c.payout()+spread)*(c.committed?1.05:1.);
    }
    static double screen(const p11::State&s,const p11::Candidate&c) {return score(s,c);}
    static bool bank(const p11::State&s) {
        return s.bag>0 && (s.bag>=30 || (s.bag>=20 && s.bag%10==0) || s.altar<=2
            || s.limit-s.round<=s.altar+p11::tribute_ticks(s.bag)+8);
    }
    static bool retreat(float hp,float maximum,float incoming) {return hp<=maximum*.35f || hp<=incoming*2+60;}
};
}
