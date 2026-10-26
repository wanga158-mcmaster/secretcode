#include <cassert>
#include <cmath>
#include <limits>
#include <type_traits>
#include "../quiescence/src/policy.h"

using p11_quiescence::Action;
using p11_quiescence::Choice;
using p11_quiescence::Input;
using p11_quiescence::Slot;
using p11_quiescence::SpellKind;
using Policy=p11_quiescence::Policy;

namespace {
Slot reset() {return {SpellKind::reset,0,true,false,false};}
Slot damage(float value,bool exclusive=false) {return {SpellKind::damage,value,true,true,exclusive};}
Input duel(float hp,float basic) {
    Input input;input.hp=hp;input.basic_damage=basic;
    input.basic_ready=input.basic_in_range=input.cast_ready=input.movement_available=true;
    return input;
}
void plan(const Choice&choice,std::initializer_list<Action> actions) {
    assert(choice.count==int(actions.size()));
    int at=0;for(auto action:actions)assert(choice.actions[at++]==action);
    assert(choice.first==(actions.size()?*actions.begin():Action::wait));
    for(;at<3;++at)assert(choice.actions[at]==Action::wait);
}
} // namespace

int main() {
    static_assert(Policy::bounded_scan&&Policy::window_size==3&&Policy::trace_scan&&Policy::tactical_search);
    static_assert(std::is_same_v<Policy::MicroInput,Input>);
    static_assert(std::is_same_v<Policy::MicroAction,Action>);
    static_assert(std::is_same_v<Policy::MicroSpellKind,SpellKind>);

    // Both real alternatives are supplied to the production chooser: a
    // lethal normal basic preserves a ready, much larger damage spell.
    auto input=duel(50,70);input.slots[0]=damage(200);
    auto choice=Policy::choose_micro(input);
    plan(choice,{Action::basic});
    assert(choice.kill&&choice.damage==50&&choice.retain_cast&&choice.retain_movement);

    // The entire basic/reset/basic line beats every single cast and every
    // basic+damage ordering. Execute only its first choice, then reobserve.
    input=duel(190,100);input.slots={reset(),damage(80)};
    choice=Policy::choose_micro(input);
    plan(choice,{Action::basic,Action::cast0,Action::basic});
    assert(choice.kill&&choice.damage==190&&!choice.retain_cast&&!choice.retain_movement);
    input.hp=90;input.basic_ready=false;
    choice=Policy::choose_micro(input);
    plan(choice,{Action::cast0,Action::basic});
    input.cast_ready=false;input.basic_ready=true;input.movement_available=false;
    choice=Policy::choose_micro(input);
    plan(choice,{Action::basic});
    assert(choice.kill&&!choice.retain_cast);

    // Two ready slots share one cast. A fictitious second cast would kill;
    // the actual chooser stops after one80-damage effect with no basic.
    input=duel(130,20);input.basic_ready=false;
    input.slots={damage(80),damage(80)};
    choice=Policy::choose_micro(input);
    plan(choice,{Action::cast0});
    assert(!choice.kill&&choice.damage==80&&!choice.retain_cast);
    input.cast_ready=false;choice=Policy::choose_micro(input);
    plan(choice,{});
    assert(choice.damage==0&&!choice.kill);

    // A ranged champion that already walked may reset and attack. The
    // stronger movement-exclusive damage spell is not legal after walking.
    input=duel(60,70);input.ranged=input.moved=true;
    input.basic_ready=input.movement_available=false;
    input.slots={reset(),damage(100,true)};
    choice=Policy::choose_micro(input);
    plan(choice,{Action::cast0,Action::basic});
    assert(choice.kill&&!choice.retain_movement);

    // Equal lethal spell alternatives preserve movement before considering
    // action count or slot order. Exclusivity spends movement, not a walk.
    input=duel(40,10);input.basic_ready=false;
    input.slots={damage(40,true),damage(40,false)};
    choice=Policy::choose_micro(input);
    plan(choice,{Action::cast1});
    assert(choice.kill&&choice.retain_movement);
    input.slots[1].ready=false;choice=Policy::choose_micro(input);
    plan(choice,{Action::cast0});
    assert(!choice.retain_movement);
    input.moved=true;choice=Policy::choose_micro(input);
    plan(choice,{});

    // Ranged basics consume movement; melee basics retain it. An otherwise
    // equal nonexclusive spell wins the movement tie when cast is already
    // unavailable only through the literal input (no invented restoration).
    input=duel(100,30);input.ranged=true;input.cast_ready=false;
    choice=Policy::choose_micro(input);plan(choice,{Action::basic});
    assert(!choice.retain_movement);
    input.ranged=false;choice=Policy::choose_micro(input);
    assert(choice.retain_movement);

    // Unsupported includes enhanced resets mapped out by the SDK adapter.
    // No ready or in-range supported action makes explicit wait optimal.
    input=duel(100,30);input.basic_ready=false;
    input.slots[0]={SpellKind::unsupported,1000,true,true,false};
    input.slots[1]=damage(1000);input.slots[1].ready=false;
    choice=Policy::choose_micro(input);plan(choice,{});
    assert(choice.retain_cast&&choice.retain_movement);
    input.slots[1].ready=true;input.slots[1].in_range=false;
    choice=Policy::choose_micro(input);plan(choice,{});
    input.slots[0]=reset();input.basic_ready=true;input.basic_in_range=false;
    choice=Policy::choose_micro(input);plan(choice,{}); // Already-ready reset is omitted.

    // Fewer actions breaks a fully equal lethal tie. Any overkill is capped,
    // and no reset/cooldown future is explored after the lethal effect.
    input=duel(100,40);input.slots={reset(),damage(100)};
    choice=Policy::choose_micro(input);
    plan(choice,{Action::cast1});
    assert(choice.kill&&choice.damage==100&&choice.retain_movement);
    input.hp=0;choice=Policy::choose_micro(input);plan(choice,{});
    assert(choice.kill&&choice.damage==0&&choice.retain_cast&&choice.retain_movement);

    // Current float HP is retained;100.001 is not rounded into a two-hit
    // kill. This is sequential model arithmetic, not engine-bit parity.
    input=duel(100.001f,50);input.slots={reset(),Slot{}};
    choice=Policy::choose_micro(input);
    plan(choice,{Action::basic,Action::cast0,Action::basic});
    assert(!choice.kill&&choice.damage==100);
    input.hp=100;choice=Policy::choose_micro(input);
    assert(choice.kill&&choice.count==3);
    input.hp=std::numeric_limits<float>::quiet_NaN();
    choice=Policy::choose_micro(input);plan(choice,{});

    // The top-level objective stays exactly tempo's while the actual
    // detailed selector continues to receive competing camp candidates.
    p11::State state;
    std::array<p11::Candidate,2> menu{};
    menu[0].id=0;menu[0].gold=30;menu[0].clear=20;
    menu[1].id=1;menu[1].gold=100;menu[1].clear=2;
    assert(Policy::score(state,menu[0])==p11_tempo::Policy::score(state,menu[0]));
    assert(Policy::screen(state,menu[1])==p11_tempo::Policy::score(state,menu[1]));
    assert(p11::choose<Policy>(state,menu)==1);
}
