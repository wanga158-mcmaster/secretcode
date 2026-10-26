// Does a ranged step spend enough attack allowance to make reset legal?
#include <cambc.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <exception>
using namespace cambc;
namespace {
inline constexpr int move_reset = 0;
bool side_b() { return !map::spawn_region(team::ally).is_set({1, 1}); }
vec2 own(vec2 p) { return side_b() ? vec2{map::width()-1-p.x, map::height()-1-p.y} : p; }
void record(const char* phase, camp target, int ok=-1) {
    auto& me=self();
    std::printf("V side=%c r=%u phase=%s x=%d y=%d hp=%d M=%d A=%d C=%d cd=%d ok=%d fuel=%llu\n",
        side_b()?'B':'A',round_number(),phase,me.position().x,me.position().y,int(target.health()),
        me.can_move(),me.can_attack(),me.can_cast(),me.abilities()[0].cooldown(),ok,
        static_cast<unsigned long long>(fuel_remaining()));
}
void turn() {
    auto& me=self();
    if (!me.is_alive() || me.champion_name()!=champion_type::vesper_vale) return;
    const auto origin=own({4,5});
    const auto target_tile=own({8,6});
    const int r=int(round_number());
    if (r<30) {
        if (me.position()!=origin && me.can_move() && !vision::has_champion(origin)) me.move_towards(origin);
        return;
    }
    if (r!=30 || me.position()!=origin || !vision::has_camp(target_tile)) return;
    auto target=vision::camp_at(target_tile);
    record("before_step",target);
    const bool moved=me.move_absolute(own({5,5}));
    record("after_step",target,moved);
    if constexpr (move_reset!=0) {
        const bool reset=me.abilities()[0].use();
        record("after_reset",target,reset);
        if (me.can_attack() && target.is_alive() && me.position().chebyshev_distance(target_tile)<=me.attack_range()) {
            me.attack(target);
            record("after_basic",target,1);
        }
    }
    record("done",target);
}
}
int main() {
    while(true) {
        try {
            if (is_draft()) {
                const std::array picks{champion_draft::vesper_vale,champion_draft::ryth,champion_draft::tavi};
                const auto mine=draft::current_draft(team::ally);
                for(auto p:picks) if(std::find(mine.begin(),mine.end(),p)==mine.end()) {draft::pick(p);break;}
            } else turn();
        } catch(const std::exception& e) {std::printf("move-reset error: %s\n",e.what());}
        end_turn();
    }
}
