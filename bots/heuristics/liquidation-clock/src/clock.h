#pragma once
#include <algorithm>
#include <cmath>
namespace h12_liquidation_clock {
struct Timing {
    int horizon=2000,capture=5,base=0,minimum=1;
    double multiplier=.1;
    int duration(int bag)const{return std::max(minimum,base+int(std::ceil(std::max(0,bag)*multiplier)));}
    int reserve(int bag)const{return std::max(24,2*capture+duration(std::max(1,bag)));}
    bool start_fits(int round,int bag)const{return bag>0&&round+duration(bag)<=horizon;}
    // Unknown progress may pay next phase; this is an optimistic admission,
    // not a claim that an unobserved fresh capture finishes in one round.
    bool held_fits(int round,int altar_estimate)const{return round+1+std::max(0,altar_estimate)+duration(1)<=horizon;}
    bool gold_fits(int round,int travel,float work,bool held=false)const{
        return round+std::max(0,travel)+int(std::ceil(std::max(0.f,work)))+(held?1:capture+1)<=horizon;
    }
};
enum class Program {Ordinary,Channel,Bank,Held,Gold,Exit};
inline bool gold_admitted(int gems,int gold,bool affordable,bool eta_fits){return gems==0&&gold>0&&affordable&&eta_fits;}
struct TerminalFacts {
    bool channel=false,safe=true,held_reward=false,held_pad=false;
    int bag=0,source=-1,round=0,altar=0;
};
class Clock {
    bool terminal_=false;
    int held_=-1,entered_=-1;
public:
    bool terminal()const{return terminal_;}
    int held()const{return held_;}
    int entered()const{return entered_;}
    bool enter(const Timing&t,int round,int bag,int selected_held,bool enabled=true){
        if(!enabled||terminal_||t.horizon-round>t.reserve(bag))return false;
        terminal_=true;entered_=round;held_=bag==0?selected_held:-1;return true;
    }
    // No bank, death, respawn or empty-bag operation clears terminal_.
    void retire_hold(){held_=-1;}
    Program decide(const Timing&t,const TerminalFacts&f) {
        if(!terminal_)return Program::Ordinary;
        if(f.channel)return Program::Channel;
        if(f.bag>0){retire_hold();return Program::Bank;}
        if(held_>=0){
            if(f.source==held_&&f.held_reward&&f.held_pad&&f.safe&&t.held_fits(f.round,f.altar))return Program::Held;
            retire_hold();
        }
        return f.safe?Program::Gold:Program::Exit;
    }
};
struct ProgramResult {Program plan=Program::Ordinary;bool acted=false;};
// A production adapter supplies fresh facts and returns ACTUAL action receipts.
// Plan acceptance never manufactures a move/cast/tribute or erases its receipt.
template<class World> ProgramResult execute_terminal(Clock&clock,const Timing&t,World&w){
    const auto plan=clock.decide(t,w.terminal_facts());
    switch(plan){
        case Program::Bank:return {plan,w.bank_terminal()};
        case Program::Held:return {plan,w.hold_terminal(clock.held())};
        case Program::Gold:return {plan,w.gold_terminal()};
        case Program::Exit:return {plan,w.exit_terminal()};
        default:return {plan,false};
    }
}
}
