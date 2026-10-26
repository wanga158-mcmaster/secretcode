#pragma once
#include <array>
#include <cstdint>
#include <algorithm>

namespace cd {
inline constexpr int radius_cap = 4; // finite search work, not a game balance assumption
inline constexpr int side = radius_cap * 2 + 1;
inline constexpr int cells = side * side;
inline constexpr int index(int x, int y) { return (y + radius_cap) * side + x + radius_cap; }

struct Grid
{
    int radius{};
    std::array<bool, cells> path{};
    std::array<bool, cells> landing{};
};

struct Reachable
{
    std::array<std::uint8_t, cells> distance{};
    bool can_land(const Grid& grid, int x, int y) const
    {
        return std::max(x < 0 ? -x : x, y < 0 ? -y : y) <= grid.radius
            && distance[index(x, y)] != 255 && grid.landing[index(x, y)];
    }
};

inline Reachable flood(const Grid& grid)
{
    Reachable out;
    out.distance.fill(255);
    struct Offset { int x{}, y{}; };
    std::array<Offset, cells> queue{};
    int tail = 1;
    out.distance[index(0, 0)] = 0; // own origin is always a valid path root
    for (int head = 0; head < tail; ++head)
    {
        const auto p = queue[static_cast<std::size_t>(head)];
        const auto d = out.distance[index(p.x, p.y)];
        if (d >= grid.radius) continue;
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x)
            {
                if (x == 0 && y == 0) continue;
                const int nx = p.x + x, ny = p.y + y;
                if (std::max(nx < 0 ? -nx : nx, ny < 0 ? -ny : ny) > grid.radius) continue;
                const auto key = static_cast<std::size_t>(index(nx, ny));
                if (!grid.path[key] || out.distance[key] != 255) continue;
                out.distance[key] = static_cast<std::uint8_t>(d + 1);
                queue[static_cast<std::size_t>(tail++)] = { nx, ny };
            }
    }
    return out;
}
} // namespace cd
