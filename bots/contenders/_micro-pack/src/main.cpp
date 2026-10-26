// Bounded native action-packing probe; never a contender or ladder opponent.
#include <cambc.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <exception>

using namespace cambc;
namespace {
inline constexpr int micro_followup = 0;
int cheb(vec2 a, vec2 b) { return a.chebyshev_distance(b); }
int side() { return map::spawn_region(team::ally).is_set({1, 1}) ? 0 : 1; }
vec2 own(vec2 p) { return side() == 0 ? p : vec2{map::width() - 1 - p.x, map::height() - 1 - p.y}; }
int start() { return 30 + (map::width() & 1); }
const char* name() {
    switch (self().champion_name()) {
    case champion_type::tavi: return "tavi";
    case champion_type::ryth: return "ryth";
    default: return "vesper";
    }
}
void log(const char* phase, camp gold, camp wolf, int ok = -1) {
    auto& me = self();
    std::printf("U side=%c r=%u who=%s phase=%s g=%d w=%d ga=%d wa=%d M=%d A=%d C=%d ok=%d fuel=%llu\n",
        side() == 0 ? 'A' : 'B', round_number(), name(), phase,
        gold.is_alive() ? int(gold.health()) : 0, wolf.is_alive() ? int(wolf.health()) : 0, gold.is_alive(), wolf.is_alive(),
        me.can_move(), me.can_attack(), me.can_cast(), ok,
        static_cast<unsigned long long>(fuel_remaining()));
}
void draft_turn() {
    const std::array picks{champion_draft::tavi, champion_draft::ryth, champion_draft::vesper_vale};
    auto mine = draft::current_draft(team::ally);
    for (auto p : picks) if (std::find(mine.begin(), mine.end(), p) == mine.end()) { draft::pick(p); return; }
}
void turn() {
    auto& me = self();
    if (!me.is_alive()) return;
    const bool tavi = me.champion_name() == champion_type::tavi;
    const bool ryth = me.champion_name() == champion_type::ryth;
    const int y = tavi ? 6 : ryth ? 12 : 18;
    const auto here = own({ryth ? 7 : 6, y - 1});
    const auto gp = own({7, y}), wp = own({8, y});
    const int r = int(round_number()), s = start();
    if (r < s - 2) {
        // move_towards intentionally permits its target in its path field;
        // do not dispatch when another visible champion occupies that target.
        if (me.position() != here && me.can_move() && !vision::has_champion(here)) me.move_towards(here);
        return;
    }
    if (r > s || me.position() != here || !vision::has_camp(gp) || !vision::has_camp(wp)) return;
    auto gold = vision::camp_at(gp), wolf = vision::camp_at(wp);
    if (r < s) {
        if ((!tavi && !ryth) || (ryth && r == s - 1)) me.attack(gold);
        return;
    }
    log("before", gold, wolf);
    if (tavi) {
        const bool ok = me.abilities()[1].use(gold);
        log("after_lethal_cast", gold, wolf, ok);
    } else if (ryth) {
        const bool ok = me.abilities()[0].use(gp);
        log("after_lethal_cast", gold, wolf, ok);
    } else {
        me.attack(gold);
        log("after_lethal_basic", gold, wolf, 1);
    }
    if constexpr (micro_followup != 0) {
        // Fresh runtime guards: never dispatch another action at the dead target.
        if (!tavi && !ryth && !me.can_attack() && me.abilities()[0].is_ready()) {
            const bool ok = me.abilities()[0].use();
            log("after_reset", gold, wolf, ok);
        }
        if (me.can_attack() && wolf.is_alive() && cheb(me.position(), wolf.position()) <= me.attack_range()) {
            me.attack(wolf);
            log("after_survivor_basic", gold, wolf, 1);
        }
    }
    log("done", gold, wolf);
}
}
int main() {
    while (true) {
        try { if (is_draft()) draft_turn(); else turn(); }
        catch (const std::exception& e) { std::printf("micro error: %s\n", e.what()); }
        end_turn();
    }
}
