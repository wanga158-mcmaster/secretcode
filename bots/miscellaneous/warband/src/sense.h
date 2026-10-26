#pragma once
// Champion kits decoded from the engine's design table, and one observation snapshot per turn.
#include <cambc.h>
#include <array>
#include <vector>
#include "util.h"

namespace wb {

struct Slot {
    int kind = 0;           // cambc::ability_kind value, 0 when empty
    float range = 0, modifier = 0, radius = 0;
    int duration = 0;
    bool excl_move = false, enhanced = false;
    bool t_enemies = false, t_camps = false, t_ground = false, t_allies = false, t_self = false;
    bool stuns() const;
};

struct Kit {
    cambc::champion_type type{};
    bool melee = true;
    int range = 1;
    std::array<Slot, 2> slots{};
    int stun_slot = -1, nuke_slot = -1, aoe_slot = -1, reset_slot = -1, dash_slot = -1, shield_slot = -1, heal_slot = -1;
    bool stun_immune = false;
    bool tank = false;      // melee with a large health pool: stands in front of camps
    // Expected damage per round in a sustained brawl (basic attacks, resets, damaging casts).
    float dps(float str, float focus) const;
    // Damage of ready damaging casts (slots whose cooldown is zero).
    float burst(float focus, int cd0, int cd1) const;
};

const Kit& kit_of(cambc::champion_type t);

struct Unit {
    cambc::champion ch;
    int index = -1;          // roster index (ally or enemy)
    vec2 pos{};
    float hp = 0, maxhp = 1, str = 0, focus = 0, shield = 0;
    int range = 1;
    cambc::champion_type type{};
    int gems = 0;
    int stun_left = -1;      // stun buff duration if stunned (0: consumed this round), -1 none
    bool recalling = false, tributing = false;
    int cd[2] = {0, 0};
    const Kit* kit = nullptr;
    float frac() const { return maxhp > 0 ? hp / maxhp : 0; }
    bool stun_pending() const { return stun_left >= 1; }
};

// Read one visible champion (throws when it is hidden).
Unit read_unit(const cambc::champion& c, int index, bool detailed);

} // namespace wb
