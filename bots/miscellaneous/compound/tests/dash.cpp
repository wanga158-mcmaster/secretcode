#include <cstdio>
#include "dash_plan.h"

int main()
{
    cd::Grid grid;
    grid.radius = 2;
    grid.path[cd::index(0, 0)] = true;
    grid.path[cd::index(1, 0)] = true; // hole: usable on the path, illegal as a landing
    grid.path[cd::index(2, 0)] = true;
    grid.landing[cd::index(0, 0)] = true;
    grid.landing[cd::index(2, 0)] = true;
    auto reach = cd::flood(grid);
    if (!reach.can_land(grid, 2, 0) || reach.can_land(grid, 1, 0)) return 1;
    // The only corridor is now occupied/walled: a geometric range mask must not admit the landing.
    grid.path[cd::index(1, 0)] = false;
    reach = cd::flood(grid);
    if (reach.can_land(grid, 2, 0)) return 2;
    // Respect actual step count, not just the scan window's Chebyshev extent.
    grid.path[cd::index(1, 0)] = true;
    grid.radius = 1;
    reach = cd::flood(grid);
    if (reach.can_land(grid, 2, 0)) return 3;
    // Diagonal steps are legal; paths may begin on the actor's otherwise occupied origin.
    grid = {};
    grid.radius = 2;
    grid.path[cd::index(1, 1)] = true;
    grid.path[cd::index(2, 2)] = true;
    grid.landing[cd::index(2, 2)] = true;
    reach = cd::flood(grid);
    if (!reach.can_land(grid, 2, 2) || reach.distance[cd::index(2, 2)] != 2) return 4;
    std::puts("dash: hole paths, legal landings, blockers and finite step budget passed");
    return 0;
}
