#include "melee_exit.h"
#include <cstdio>
#include <cstdlib>

using namespace mosaic;
unsigned checks=0;
void check(bool ok){++checks;if(!ok)std::abort();}
int main(){
    return_budget::Trace trace;
    check(trace.reset_quiet({0,0},{0}));
    check(trace.observe_step({0,0},{1,0},{60},true)==return_budget::Reason::ok);
    return_budget::State state;state.health=100;state.can_move_now=true;
    state.allow_one_refusal=true;
    auto held=state;held.additional_forced_waits=1;
    check(!trace.quote(held).safe);
    melee_exit::Ready ready{true,true,true,true,1};
    const auto word=melee_exit::admit(trace,state,ready);
    check(word.valid&&word.safe&&word.worst_loss==60&&word.steps==1);
    // Lost M, a ranged basic, a blocked first step, no real target or a short
    // action tail cannot inherit the zero-wait melee certificate.
    auto spent=state;spent.can_move_now=false;
    check(!melee_exit::admit(trace,spent,ready).valid);
    check(!melee_exit::admit(trace,held,ready).valid);
    auto delayed=state;delayed.compute_waits=1;
    check(!melee_exit::admit(trace,delayed,ready).valid);
    for(int field=0;field<5;++field){
        auto invalid=ready;
        if(field==0)invalid.can_attack=false;
        if(field==1)invalid.target_in_range=false;
        if(field==2)invalid.first_step_open=false;
        if(field==3)invalid.fuel_tail=false;
        if(field==4)invalid.basic_range=3;
        check(!melee_exit::admit(trace,state,invalid).valid);
    }
    // Native false with unchanged pose preserves useful reverse geometry.
    return_budget::Trace rising;
    check(rising.reset_quiet({0,0},{0}));
    check(rising.observe_step({0,0},{1,0},{120},true)==return_budget::Reason::ok);
    check(rising.observe_step({1,0},{2,0},{60},true)==return_budget::Reason::ok);
    state.health=350;
    const auto before=rising.quote(state);
    check(before.safe&&before.worst_loss==240);
    melee_exit::observe_nonordinary(rising,{2,0});
    check(rising.valid()&&rising.quote(state).worst_loss==before.worst_loss);
    // Actual displacement cannot fabricate an ordinary inverse dash edge.
    melee_exit::observe_nonordinary(rising,{4,0});
    check(!rising.valid());
    std::printf("melee-exit: %u checks passed\n",checks);
}
