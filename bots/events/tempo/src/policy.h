#pragma once
#include "../../common/src/events.h"
#if defined(P11_TEMPO_CONVERSION_GUARD) && P11_TEMPO_CONVERSION_GUARD
#include "conversion.h"
#endif
namespace p11_tempo {
struct Policy {
    static constexpr const char* name="p11-tempo";
    static constexpr bool bounded_scan=true;
    static constexpr int window_size=3;
    static constexpr bool trace_scan=true;
    static double score(const p11::State&s,const p11::Candidate&c) {
        const int horizon=s.limit-s.round;
        if(c.bank_time(s)>horizon && c.gems>0) return -std::numeric_limits<double>::infinity();
        // Gold and gems complete different pipelines: gold pays at capture,
        // whereas gems still need transport and tribute. Early equipment is
        // productive capital; do not charge gold a fictitious altar journey.
        const int spread=(c.id*3+s.identity*11)%7;
        const double gem_rate=3.*c.gems/(1+c.bank_time(s)+12*c.allies+spread);
        const double gold_rate=(s.mature?.04:3.)*c.gold/(1+c.payout()+(s.field_shop?0:s.recall+1)+12*c.allies+spread);
        return (gem_rate+gold_rate)*(c.committed?1.10:1.);
    }
    static double screen(const p11::State&s,const p11::Candidate&c) {return score(s,c);}
    static bool bank(const p11::State&s) {
        return s.bag>0 && (s.bag>=20 || s.altar<=3 || s.limit-s.round<=s.altar+p11::tribute_ticks(s.bag)+8);
    }
    static bool retreat(float hp,float maximum,float incoming) {return hp<=maximum*.30f || hp<=incoming*2+45;}
};
}
