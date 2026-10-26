#pragma once
// Public camp-map context for learned attention. No filtering, target choice or hidden-state inference.
#include <vector>
#include "cand.h"

namespace zero::student {

inline constexpr int kCampContextPhi = 12;
struct CampContext
{
    int group{ -1 };  // public Map::groups index; rows contain every group in this order
    float phi[kCampContextPhi]{};
};

// Every row's static facts are known from the loaded map, even before any member is visible.
// phi: 0 dx/20; 1 dy/20; 2 Chebyshev distance/40; 3 public gold/300; 4 public gems/100;
// 5 static full HP/10000; 6 public member count/64; 7 current GroupStatus/3; 8 status known;
// 9 observed living HP at public member-spawn tiles/10000; 10 distinct observed living member
// spawn fraction; 11 current spawn-footprint HP complete. Unavailable geometry is zero when the
// self coordinate is outside the map. Row presence supplies static-known; dynamic unknown is
// explicitly distinct from a known-empty footprint through 8/10/11.
//
// Footprint observations require BOTH exact public spawn tile and member type. Off-spawn/unassociated
// observations contribute to no group. Repeated matching tile/type observations use the last view.
// This is not an original-group membership inference for displaced camps. Completeness requires all
// member spawns observed living, an empty public group, or reward/respawn status with no observed
// living footprint. Conflicting status and living observations remain partial, with both facts kept.
//
// Preconditions: finished immutable Map with unique member spawns, finite raw Obs facts, canonical
// observation order shared by runtime/source. Output is uncapped and caller-owned. The implementation
// is linear in groups + public members + observed camps and reads no Memory, Paths, SDK or fuel state.
void camp_context(const Context& ctx, std::vector<CampContext>& out);

}  // namespace zero::student
