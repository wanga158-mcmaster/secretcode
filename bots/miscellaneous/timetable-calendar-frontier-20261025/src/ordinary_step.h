#pragma once

namespace tt::ordinary_step_detail {
// Permission for an ordinary step attempt, not proof of a free landing or a
// movement effect. SDK has_* predicates omit hidden occupants; the native move
// can still refuse. Adjacency, own allowance, HP and epoch belong to the enclosing
// authority. Keep this short-circuit order shared with the production wrappers.
template<class Point,class Terrain,class Champion,class Camp,class Teleport>
bool may_attempt(Point point,int width,int height,Terrain forbidden_terrain,
                 Champion visible_champion,Camp visible_camp,Teleport visible_teleport)
{
    return point.x>=0 && point.y>=0 && point.x<width && point.y<height &&
        !forbidden_terrain(point) && !visible_champion(point) &&
        !visible_camp(point) && !visible_teleport(point);
}
} // namespace tt::ordinary_step_detail

#ifndef TT_ORDINARY_STEP_FIXTURE
#include "runtime.h"
namespace tt {
inline bool ordinary_step_may_attempt(vec2 point)
{
    const auto& world=World::get(); // immutable static blockers initialized by caller
    return ordinary_step_detail::may_attempt(point,world.width,world.height,
        [&](vec2 tile) { return world.terrain.is_set(tile); },
        [](vec2 tile) { return cambc::vision::has_champion(tile); },
        [](vec2 tile) { return cambc::vision::has_camp(tile); },
        [](vec2 tile) { return cambc::vision::has_teleport(tile); });
}
} // namespace tt
#endif
