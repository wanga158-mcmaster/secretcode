#include "../reset-pursuit/src/defence.h"
#include "../reset-pursuit/src/flow.h"
#include <array>
#include <cassert>
#include <string>

struct Enemy {int distance; bool visible=true, alive=true;};
struct Combat {
    std::array<int,3> hp{20,30,30};
    bool attack=true, cast=true;
    int cooldown_value=5;
    std::string actions;
    bool enough_fuel() const {return true;}
    bool can_attack() const {return attack;}
    bool can_cast() const {return cast;}
    bool legal(int n) const {return n>=0&&n<3&&hp[n]>0;}
    bool alive(int n) const {return hp[n]>0;}
    int cooldown() const {return cooldown_value;}
    bool reset_ready() const {return cooldown_value==0;}
    int choose(int excluded) const {
        for(int n=0;n<3;++n)if(n!=excluded&&legal(n))return n;
        return -1;
    }
    bool basic(int n) {
        assert(attack&&legal(n));
        attack=false;hp[n]-=40;actions+=char('A'+n);
        if(!alive(n))cooldown_value=0;
        return true;
    }
    bool reset() {
        assert(cast&&reset_ready());
        cast=false;attack=true;cooldown_value=8;actions+='R';return true;
    }
};

int main() {
    using h12_reset_pursuit::defend_in_place;
    std::array<Enemy,3> enemies{{{4},{3},{1}}};
    std::string selected;
    auto distance=[](const Enemy&e){return e.distance;};
    auto fight=[&](const Enemy&e) {
        if(!e.visible||!e.alive)return false;
        selected+=char('0'+e.distance);return true;
    };
    // Ranged defence reaches a legal distance-three enemy without considering
    // the out-of-range enemy first; melee still responds only at distance one.
    assert(defend_in_place(enemies,3,distance,fight)&&selected=="3");
    selected.clear();
    assert(defend_in_place(enemies,1,distance,fight)&&selected=="1");
    // A failed/invalid earlier candidate must not suppress a later actual action.
    enemies[1].visible=false;selected.clear();
    assert(defend_in_place(enemies,3,distance,fight)&&selected=="1");
    enemies[2].alive=false;selected.clear();
    assert(!defend_in_place(enemies,3,distance,fight)&&selected.empty());
    enemies[1].visible=true;enemies[1].distance=4;
    assert(!defend_in_place(enemies,3,distance,fight));
    // The new admission path invokes the existing mutable reset executor:
    // A dies, refreshes reset, then a different B dies; C remains untouched.
    std::array<Enemy,1> ranged{{{3}}};
    Combat world;
    assert(defend_in_place(ranged,3,distance,[&](const Enemy&) {
        const auto result=h12_reset_pursuit::execute(world);
        return result.basics>0||result.casts>0;
    }));
    assert(world.actions=="ARB"&&world.hp[0]<0&&world.hp[1]<0&&world.hp[2]==30);
    assert(!world.attack&&!world.cast);
}
