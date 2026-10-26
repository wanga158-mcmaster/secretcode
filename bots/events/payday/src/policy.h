#pragma once
#include "../../tempo/src/policy.h"
#include "payday.h"

namespace p11_payday {
struct Policy : p11_tempo::Policy {
    static constexpr const char* name="p11-payday";
    static constexpr bool bounded_scan=true;
    static constexpr int window_size=3;
    static constexpr bool trace_scan=true;

    static double score(const p11::State&s,const p11::Candidate&c) {
        const auto events=reward_events(s,c);
        const auto&rewards=events.payable;
        if(rewards.gold<=0&&rewards.gems<=0)return -std::numeric_limits<double>::infinity();
        const int spread=int((std::int64_t(c.id)*3+std::int64_t(s.identity)*11)%7);
        const double crowd=12.*c.allies+spread;
        const double gem_rate=rewards.gems>0?3.*rewards.gems/(1.+events.bank+crowd):0.;
        const double gold_rate=(s.mature?.04:3.)*rewards.gold
            /(1.+events.capture+(s.field_shop?0:double(s.recall)+1)+crowd);
        return (gem_rate+gold_rate)*(c.committed?1.10:1.);
    }
    static double screen(const p11::State&s,const p11::Candidate&c) {return score(s,c);}
    static bool bank(const p11::State&s) {
        if(s.bag<=0||s.altar<0||s.round<0||s.limit<0||s.recall<0
            ||!std::isfinite(s.tribute_multiplier)||s.tribute_multiplier<0
            ||s.tribute_base<0||s.tribute_minimum<1)return false;
        const double scaled=std::ceil(double(s.bag)*s.tribute_multiplier);
        if(!std::isfinite(scaled)||scaled>std::numeric_limits<int>::max()-s.tribute_base)return false;
        const int ticks=p11::tribute_ticks(s.bag,s.tribute_multiplier,s.tribute_base,s.tribute_minimum);
        if(std::int64_t(s.altar)+ticks>std::int64_t(s.limit)-s.round)return false;
        if(s.bag<20&&s.altar>3&&std::int64_t(s.altar)+p11::tribute_ticks(s.bag)+8
            >std::numeric_limits<int>::max())return false;
        return p11_tempo::Policy::bank(s);
    }
};
} // namespace p11_payday
