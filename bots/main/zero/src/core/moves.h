#pragma once
// Legal destinations by the rules (GameRules "Ways to move"): an ordinary step reaches one of the 8 neighbours that is
// not a wall, hole, altar, the enemy spawn or occupied; a dash travels up to `range` steps along a legal path that
// may cross holes (not walls) and lands on a free tile that is not a hole. Computed from the map and the units the
// champion knows, identically in the bot and the native tools.
#include "map.h"
#include "obs.h"

namespace zero {

// The default preserves complete enumeration. Deferred exact producers request ordinary steps only;
// dash_destinations_known then distinguishes an unbuilt list from a known empty list.
void legal_moves(const Map& map, Obs& obs, int dash_range, bool with_dash = true);
bool has_dash_destination(const Map& map, const Obs& obs, int dash_range);
void generate_dash_destinations(const Map& map, const Obs& obs, int dash_range, std::vector<Tile>& out);
void generate_dash_destinations(const Map& map, Obs& obs, int dash_range);

}  // namespace zero

namespace zero {

// Opt-in complete dash membership in canonical (y,x) order. Uses the same
// bounded window, observed occupancy and transit/landing rules as generation
// above; no observation cache is read or written and the default is unchanged.
void generate_dash_destinations_canonical(const Map& map, const Obs& obs, int dash_range, std::vector<Tile>& out);

}  // namespace zero
