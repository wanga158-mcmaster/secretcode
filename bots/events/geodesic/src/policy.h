#pragma once
#include "../../tempo/src/policy.h"

namespace p11_geodesic {
// The controlled change is the adapter's routed travel/altar distances. Keep
// tempo's financial objective and bank/retreat decisions as the comparison.
struct Policy : p11_tempo::Policy {
    static constexpr const char* name="p11-geodesic";
    static constexpr bool bounded_scan=true;
    static constexpr int window_size=3;
    static constexpr bool geodesic=true;
    static constexpr bool trace_scan=true;
    static double score(const p11::State&s,const p11::Candidate&c) {
        if(c.travel>=10000||(c.gems>0&&c.altar>=10000))
            return -std::numeric_limits<double>::infinity();
        return p11_tempo::Policy::score(s,c);
    }
    static double screen(const p11::State&s,const p11::Candidate&c) {return score(s,c);}
};
}
