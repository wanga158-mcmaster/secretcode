#include <cassert>
#include "../quiescence/src/policy.h"

#ifdef NDEBUG
#error This execution fixture requires assertions enabled.
#endif

using Policy=p11_quiescence::Policy;
using p11_quiescence::Action;
using p11_quiescence::Choice;
using p11_quiescence::Input;
using p11_quiescence::SpellKind;

namespace {
Input duel() {
    Input input;input.hp=190;input.basic_damage=100;
    input.basic_ready=input.basic_in_range=input.cast_ready=input.movement_available=true;
    input.slots[0]={SpellKind::reset,0,true,true,false};
    input.slots[1]={SpellKind::damage,80,true,true,false};
    return input;
}
// The production chooser is the only plan provider. The callback represents
// the SDK adapter's fresh legality checks and its existing reset/basic option.
struct Live {
    int round=331,fuel=24000,calls=0;
    float hp=190;
    bool attack_ready=true,cast_ready=true,legal=true,change_round=false;
    int perform(int index,Action action,Action next) {
        ++calls;
        assert(index>=0&&index<3);
        if(!legal)return 0;
        if(action==Action::basic) {
            if(!attack_ready)return 0;
            hp=std::max(0.f,hp-100);attack_ready=false;
            if(change_round)++round;
            return 1;
        }
        if(action==Action::cast0) {
            if(!cast_ready)return 0;
            cast_ready=false;attack_ready=true;
            if(next==Action::basic) {
                hp=std::max(0.f,hp-100);attack_ready=false;return 2;
            }
            return 1;
        }
        return 0;
    }
    int execute(const Choice&choice) {
        return Policy::execute_micro(choice,331,[&]{return round;},[&]{return fuel;},
            [&](int index,Action action,Action next){return perform(index,action,next);});
    }
};
}

int main() {
    const auto choice=Policy::choose_micro(duel());
    assert(choice.count==3&&choice.kill);
    assert(choice.actions[0]==Action::basic&&choice.actions[1]==Action::cast0
        &&choice.actions[2]==Action::basic);

    // At the same24000 reserve, the old fresh-search gate discards the
    // selected lethal suffix after the basic. The completed-plan seam needs
    // no full input reconstruction and admits the atomic reset/basic option.
    Live old;old.perform(0,choice.actions[0],choice.actions[1]);
    if(old.fuel>25000)old.perform(1,choice.actions[1],choice.actions[2]);
    assert(old.hp==90&&old.calls==1);
    Live completed;
    assert(completed.execute(choice)==3&&completed.hp==0&&completed.calls==2);

    Live stale;stale.change_round=true;
    assert(stale.execute(choice)==1&&stale.hp==90&&stale.calls==1);
    Live already_stale;already_stale.round=332;
    assert(already_stale.execute(choice)==0&&already_stale.calls==0);

    Live missing;missing.legal=false;
    assert(missing.execute(choice)==0&&missing.calls==1&&missing.hp==190);
    Live missing_suffix;missing_suffix.cast_ready=false;
    assert(missing_suffix.execute(choice)==1&&missing_suffix.calls==2&&missing_suffix.hp==90);

    // The first legal primitive is preserved below the continuation reserve;
    // the exact10000 boundary prevents another callback.
    Live low;low.fuel=0;
    assert(low.execute(choice)==1&&low.calls==1&&low.hp==90);
    Live boundary;boundary.fuel=10000;
    assert(boundary.execute(choice)==1&&boundary.calls==1);
    Live above;above.fuel=10001;
    assert(above.execute(choice)==3&&above.hp==0);

    auto malformed=choice;malformed.count=-1;
    Live negative;assert(negative.execute(malformed)==0&&negative.calls==0);
    malformed.count=100;
    Live excess;assert(excess.execute(malformed)==3&&excess.calls==2);
    malformed=choice;malformed.count=1;
    Live prefix;assert(prefix.execute(malformed)==1&&prefix.calls==1&&prefix.hp==90);
    malformed.actions[0]=Action::wait;
    Live wait;assert(wait.execute(malformed)==0&&wait.calls==0);

    // A broken adapter cannot advance outside the clamped primitive count or
    // make the executor spin. Its reported impossible consumption is rejected.
    int calls=0;
    assert(Policy::execute_micro(choice,331,[]{return 331;},[]{return 24000;},
        [&](int,Action,Action){++calls;return 4;})==0&&calls==1);
    calls=0;
    assert(Policy::execute_micro(choice,331,[]{return 331;},[]{return 24000;},
        [&](int,Action,Action){++calls;return 3;})==0&&calls==1);
    calls=0;
    assert(Policy::execute_micro(choice,331,[]{return 331;},[]{return 24000;},
        [&](int,Action,Action){++calls;return -1;})==0&&calls==1);
}
