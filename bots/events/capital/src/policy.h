#pragma once
#include "../../tempo/src/policy.h"
#include "investment.h"

namespace p11_capital {
struct Policy : p11_tempo::Policy {
    static constexpr const char* name="p11-capital";
    static constexpr bool bounded_scan=true;
    static constexpr int window_size=3;
    static constexpr bool trace_scan=true;
    static constexpr bool reserve_action_budget=true;
    static constexpr bool capital_shop=true;
    using CapitalInput=Input;
    using CapitalJob=Job;
    using CapitalProject=Project;
    static Purchase choose_capital(const Input&in,std::span<const Job> jobs) {
        return p11_capital::choose_capital(in,jobs);
    }
    static double screen(const p11::State&s,const p11::Candidate&c) {return score(s,c);}
};
}
