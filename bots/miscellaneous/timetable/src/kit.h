#pragma once
// Champion kits: decode a champion's design into planner and tactics views.
#include <cambc.h>
#include "model.h"
#include "tactics.h"

namespace tt {

enum class Slot : int { none, reset, damage, aoe, cc, heal, dash, shield };

struct SlotKit {
    Slot kind = Slot::none;
    float modifier = 0, range = 0, radius = 0;
    int cooldown = 8;
    bool camps = false, enemies = false, exclusive = false;
    bool enhanced = false;
};

struct Kit {
    cambc::champion_type type = cambc::champion_type::ryth;
    int attack_range = 1;
    bool melee = true;
    float base_hp = 0, base_str = 0, base_focus = 0;
    float hp_growth = 0, str_growth = 0, focus_growth = 0;
    SlotKit slot[2];
    cambc::passive_type passive = cambc::passive_type::none;
    bool has_dash() const { return slot[0].kind == Slot::dash || slot[1].kind == Slot::dash; }
    bool has_reset() const { return slot[0].kind == Slot::reset || slot[1].kind == Slot::reset; }
    int slot_of(Slot k) const { return slot[0].kind == k ? 0 : slot[1].kind == k ? 1 : -1; }
};

Kit kit_of(const cambc::champion& c);
Kit kit_of(cambc::champion_type type);

// Sustained damage per round against one camp member, with strength and focus as given.
float camp_dps(const Kit& k, float strength, float focus, int neighbours = 0);
// Planner view of a body at the given stats.
BodySpec body_spec(const Kit& k, float hp, float strength, float focus);
// Sustained damage per round against champions (for skirmish verdicts).
float pvp_dps(const Kit& k, float strength, float focus);

} // namespace tt
