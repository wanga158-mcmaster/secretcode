#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace p11_quiescence {
enum class Action { wait, basic, cast0, cast1 };
enum class SpellKind { unsupported, reset, damage };
struct Slot {
    SpellKind kind=SpellKind::unsupported;
    float damage=0;
    bool ready=false, in_range=false, exclusive_with_movement=false;
};
struct Input {
    float hp=0, basic_damage=0;
    bool basic_ready=false, basic_in_range=false, cast_ready=false;
    bool ranged=false, moved=false, movement_available=false;
    std::array<Slot,2> slots{};
};
struct Choice {
    Action first=Action::wait;
    std::array<Action,3> actions{Action::wait,Action::wait,Action::wait};
    int count=0;
    bool kill=false;
    float damage=0;
    bool retain_cast=false, retain_movement=false;
};

namespace detail {
struct Position {
    float hp;
    bool basic_ready, cast_ready, movement_available;
};
inline bool better(const Choice&a,const Choice&b) {
    if(a.kill!=b.kill)return a.kill;
    if(a.damage!=b.damage)return a.damage>b.damage;
    if(a.retain_cast!=b.retain_cast)return a.retain_cast;
    if(a.retain_movement!=b.retain_movement)return a.retain_movement;
    return a.count<b.count;
}
inline bool positive(float value) {return std::isfinite(value)&&value>0;}

// Pure current-turn search. Waiting is a terminal choice at every node.
// Each branch spends an allowance; there are at most three ordinary actions.
// Damage uses sequential float subtraction, not an HP/damage hit quotient.
class Search {
    const Input& input_;
    float initial_hp_;
    Choice best_{};

    void visit(Position position,std::array<Action,3> actions,int depth) {
        Choice candidate;
        candidate.first=depth?actions[0]:Action::wait;
        candidate.actions=actions;
        candidate.count=depth;
        candidate.kill=position.hp<=0;
        candidate.damage=initial_hp_-position.hp;
        candidate.retain_cast=position.cast_ready;
        candidate.retain_movement=position.movement_available;
        if(better(candidate,best_))best_=candidate;
        // A lethal effect is terminal. A kill never grants a future cast or
        // an imagined cooldown reset inside this single-target forecast.
        if(candidate.kill||depth==3)return;

        if(position.basic_ready&&input_.basic_in_range&&positive(input_.basic_damage)) {
            auto next=position;
            next.hp=std::max(0.f,next.hp-input_.basic_damage);
            next.basic_ready=false;
            if(input_.ranged)next.movement_available=false;
            auto path=actions;path[depth]=Action::basic;
            visit(next,path,depth+1);
        }
        if(!position.cast_ready)return;
        for(int slot=0;slot<2;++slot) {
            const auto& spell=input_.slots[slot];
            if(!spell.ready||(spell.exclusive_with_movement&&input_.moved))continue;
            auto next=position;
            if(spell.kind==SpellKind::reset) {
                // Plain self resets have no targeting range. The adapter
                // maps enhanced resets to unsupported instead of inventing
                // their next-hit damage or other effects.
                if(position.basic_ready)continue;
                next.basic_ready=true;
                // Reset/movement interaction is not forecast as a benefit.
                next.movement_available=false;
            } else if(spell.kind==SpellKind::damage&&spell.in_range&&positive(spell.damage)) {
                next.hp=std::max(0.f,next.hp-spell.damage);
                if(spell.exclusive_with_movement)next.movement_available=false;
            } else continue;
            // Both slots spend this one shared successful-cast allowance.
            next.cast_ready=false;
            auto path=actions;path[depth]=slot==0?Action::cast0:Action::cast1;
            visit(next,path,depth+1);
        }
    }
public:
    explicit Search(const Input& input):input_{input},initial_hp_{std::max(0.f,input.hp)} {
        best_.retain_cast=input.cast_ready;
        best_.retain_movement=input.movement_available;
    }
    Choice run() {
        if(!std::isfinite(input_.hp))return best_;
        visit({initial_hp_,input_.basic_ready,input_.cast_ready,input_.movement_available},
              {Action::wait,Action::wait,Action::wait},0);
        return best_;
    }
};
} // namespace detail

inline Choice choose(const Input& input) {return detail::Search{input}.run();}
} // namespace p11_quiescence
