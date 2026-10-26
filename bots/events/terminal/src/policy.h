#pragma once
#include "../../tempo/src/policy.h"
#include "terminal.h"

namespace p11_terminal {
struct Policy : p11_tempo::Policy {
    static constexpr const char* name="p11-terminal";
    static constexpr bool bounded_scan=true;
    static constexpr int window_size=3;
    static constexpr bool trace_scan=true;
    static constexpr bool terminal_goal=true;
    static constexpr bool route_select=true;
    using RouteJob=p11_terminal::RouteJob;

    static double score(const p11::State&s,const p11::Candidate&c) {
        const int bank=c.bank_time(s);
        if(bank>s.limit-s.round && c.gems>0)return -std::numeric_limits<double>::infinity();
        const int spread=(c.id*3+s.identity*11)%7;
        const int gems=std::max(0,std::min(c.gems,s.needed_gems-s.bag));
        // Only reward is clipped. Transport, tribute and the deadline retain
        // the actual bag; Tempo's financial, crowding and commitment costs stay.
        const double gem_rate=3.*gems/(1+bank+12*c.allies+spread);
        const double gold_rate=(s.mature?.04:3.)*c.gold
            /(1+c.payout()+(s.field_shop?0:s.recall+1)+12*c.allies+spread);
        return (gem_rate+gold_rate)*(c.committed?1.10:1.);
    }
    static bool bank(const p11::State&s) {
        return s.bag>=s.needed_gems || p11_tempo::Policy::bank(s);
    }
    static double screen(const p11::State&s,const p11::Candidate&c) {
        return score(s,c);
    }
    static int select_route(const p11::State&s,std::span<const RouteJob>jobs) {
        const int terminal=p11_terminal::finishing_job(s,jobs);
        if(terminal>=0)return terminal;
        const int count=int(std::min(jobs.size(),std::size_t(3)));
        std::array<p11::Candidate,3> candidates{};
        for(int i=0;i<count;++i)candidates[i]=jobs[i].candidate;
        return p11::choose<Policy>(s,std::span<const p11::Candidate>(candidates.data(),count));
    }
};
} // namespace p11_terminal
