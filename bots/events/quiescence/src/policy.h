#pragma once
#include "../../tempo/src/policy.h"
#include "micro.h"
#include "execution.h"

namespace p11_quiescence {
// Keep tempo's financial, bank and retreat decisions controlled. The change
// is the adapter's bounded, reobserved own-turn camp action choice.
struct Policy : p11_tempo::Policy {
    static constexpr const char* name="p11-quiescence";
    static constexpr bool bounded_scan=true;
    static constexpr int window_size=3;
    static constexpr bool trace_scan=true;
    static constexpr bool tactical_search=true;
    using MicroInput=Input;
    using MicroAction=Action;
    using MicroSpellKind=SpellKind;
    static Choice choose_micro(const Input&input) {return choose(input);}
    template<class Now,class Fuel,class Perform>
    static int execute_micro(const Choice&choice,int observed_round,Now now,Fuel fuel,Perform perform) {
        return execute(choice,observed_round,now,fuel,perform);
    }
    static double screen(const p11::State&s,const p11::Candidate&c) {
        return p11_tempo::Policy::score(s,c);
    }
};
} // namespace p11_quiescence
