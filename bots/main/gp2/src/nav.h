#pragma once
// One-step movement along distance fields.
//
// Mechanics assumed (GameRules "Ways to move"): one 8-directional step per turn; landing
// cannot be a wall, hole, altar, enemy spawn or occupied tile; an unseen occupant makes the
// move return false without consuming the allowance, so another tile may be tried. The SDK's
// reachable_tiles() supplies the engine's legal one-step set for a blocked mask.
#include <cambc.h>
#include <functional>
#include "util.h"

namespace mb {

// Current dynamic occupancy (visible champions, camps, teleport reservations) plus terrain,
// cached for the turn (call new_turn_obstacles() once at each turn start).
const cambc::bitboard& blocked_now();
void new_turn_obstacles();
// Our own move/dash changes vision and a failed step reveals a hidden unit: refresh next use.
void invalidate_obstacles();
// A step to `tile` failed (hidden occupant or rejection): never retry it this turn.
void remember_revealed(vec2 tile);

// Legal destinations for one ordinary step from `from` (excludes `from`).
std::vector<vec2> step_options(const cambc::bitboard& blocked, vec2 from);

// Step to the neighbour with the lowest field value that is strictly better than ours
// (or equal when `allow_sideways`). `penalty` adds a cost per candidate tile (e.g. threat).
// Returns true when we moved.
bool step_along(const cambc::distance_field& field, bool allow_sideways = false,
                const std::function<int(vec2)>& penalty = {});

// Step that maximises `score(tile)` among legal destinations (and staying put, scored as
// `stay_score`). Returns true when we moved.
bool step_best(const std::function<int(vec2)>& score, int stay_score);

// Greedy step toward a tile by Chebyshev distance (fallback when no field is available).
bool step_greedy(vec2 target);

// Legal dash landings from `from` within `range`: dash paths may cross holes but not walls,
// altars, enemy spawn tiles or units (probe + rejected cases); landings must be free.
std::vector<vec2> dash_landings(vec2 from, int range);

} // namespace mb
