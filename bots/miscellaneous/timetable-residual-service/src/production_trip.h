#pragma once
// A walking trip is a witnessed path, not a target plus an estimated distance. The caller
// supplies an origin-rooted field and remains responsible for terrain/occupancy authority.
// This kernel has no engine imports; it never substitutes a geometric fallback for a path.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace mb::production {

struct Point
{
    int x{}, y{};
    friend bool operator==(Point, Point) = default;
};

inline bool adjacent(Point a, Point b)
{
    const auto dx = static_cast<std::int64_t>(a.x) - b.x;
    const auto dy = static_cast<std::int64_t>(a.y) - b.y;
    return dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1 && (dx != 0 || dy != 0);
}

// Distances must come from one origin. Descending by exactly one proves the route's
// adjacency and length within that supplied graph; it does not prove future occupancy.
inline std::vector<Point> reconstruct_route(int width, int height,
                                           std::span<const std::uint16_t> linear,
                                           Point start, Point goal)
{
    constexpr auto unreachable = std::numeric_limits<std::uint16_t>::max();
    if (width <= 0 || height <= 0) return {};
    const auto w = static_cast<std::size_t>(width);
    const auto h = static_cast<std::size_t>(height);
    if (w > std::numeric_limits<std::size_t>::max() / h || linear.size() != w * h) return {};
    const auto inside = [=](Point p) { return p.x >= 0 && p.y >= 0 && p.x < width && p.y < height; };
    if (!inside(start) || !inside(goal)) return {};
    const auto index = [=](Point p) { return static_cast<std::size_t>(p.y) * w + static_cast<std::size_t>(p.x); };
    if (linear[index(start)] != 0 || linear[index(goal)] == unreachable) return {};
    const auto distance = static_cast<std::size_t>(linear[index(goal)]);
    if (distance >= linear.size()) return {};
    std::vector<Point> reverse;
    reverse.reserve(distance + 1);
    Point at = goal;
    constexpr Point directions[] = { {0,-1}, {1,-1}, {1,0}, {1,1}, {0,1}, {-1,1}, {-1,0}, {-1,-1} };
    for (std::size_t remaining = distance;; --remaining)
    {
        reverse.push_back(at);
        if (remaining == 0)
        {
            if (at != start) return {};
            break;
        }
        bool found = false;
        for (auto direction : directions)
        {
            const Point next{ at.x + direction.x, at.y + direction.y };
            if (inside(next) && linear[index(next)] == remaining - 1)
            {
                at = next;
                found = true;
                break;
            }
        }
        if (!found) return {};
    }
    std::reverse(reverse.begin(), reverse.end());
    return reverse;
}

struct Trip
{
    int group{ -1 }, member{ -1 };
    int planned_round{ -1 }, observed_round{ -1 };
    std::vector<Point> path;
    std::size_t cursor{};

    bool valid() const { return group >= 0 && member >= 0 && !path.empty() && cursor < path.size(); }
    bool arrived() const { return valid() && cursor + 1 == path.size(); }
    std::size_t remaining_steps() const { return valid() ? path.size() - cursor - 1 : 0; }

    void invalidate() { *this = Trip{}; }

    bool install(int new_group, int new_member, int round, std::vector<Point> route)
    {
        invalidate();
        if (new_group < 0 || new_member < 0 || round < 0 || route.empty()) return false;
        for (std::size_t i = 0; i < route.size(); ++i)
            if (route[i].x < 0 || route[i].y < 0 || (i > 0 && !adjacent(route[i - 1], route[i]))) return false;
        group = new_group;
        member = new_member;
        planned_round = last_round_ = round;
        path = std::move(route);
        return true;
    }

    bool observe(int round, Point actual)
    {
        if (!valid() || round < last_round_ || actual != path[cursor])
        {
            invalidate();
            return false;
        }
        last_round_ = observed_round = round;
        return true;
    }

    std::optional<Point> next(int round) const
    {
        if (!valid() || round != observed_round || arrived()) return std::nullopt;
        return path[cursor + 1];
    }

    bool moved(int round, Point destination, bool success)
    {
        const auto expected = next(round);
        if (!success || !expected || destination != *expected)
        {
            invalidate();
            return false;
        }
        ++cursor;
        last_round_ = round;
        observed_round = -1;
        return true;
    }

private:
    int last_round_{ -1 };
};

// A caller may attach these costs only after witnessing their respective physical routes.
// They are not populated from unavailable fields or geometric distance substitutes.
struct RouteCosts
{
    int walk{ -1 }, return_walk{ -1 }, bank_walk{ -1 };
    bool certified{};
};

// This is the actual caller adapter: routes store absolute tile coordinates. Keeping
// the SDK method selection here lets a double detect a destination/delta mismatch.
template<class Actor, class EnginePoint>
bool apply_absolute_destination(Actor& actor, EnginePoint destination)
{
    return actor.move_absolute(destination);
}

} // namespace mb::production
