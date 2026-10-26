#include "production_trip.h"
#include <cstdio>
#include <limits>
#include <queue>

namespace {
using mb::production::Point;
int failures{};
struct ActorDouble
{
    Point actual{78,12}, absolute_argument{ -1,-1 };
    int absolute_calls{}, relative_calls{};
    bool move_absolute(Point to)
    {
        ++absolute_calls;
        absolute_argument = to;
        return mb::production::adjacent(actual,to);
    }
    bool move(Point) { ++relative_calls; return false; }
};
void check(bool value, const char* name)
{
    if (!value) { std::printf("FAIL %s\n", name); ++failures; }
}

// Independent graph fixture: one complete eight-neighbor search, no SDK or game access.
std::vector<std::uint16_t> field(int width, int height, Point start, const std::vector<Point>& walls)
{
    std::vector<std::uint16_t> out(static_cast<std::size_t>(width * height), std::numeric_limits<std::uint16_t>::max());
    const auto index = [=](Point p) { return static_cast<std::size_t>(p.y * width + p.x); };
    std::vector<bool> blocked(out.size(), false);
    for (auto wall : walls) blocked[index(wall)] = true;
    std::queue<Point> queue;
    queue.push(start);
    out[index(start)] = 0;
    while (!queue.empty())
    {
        const auto at = queue.front();
        queue.pop();
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
            {
                const Point next{ at.x + dx, at.y + dy };
                if ((dx == 0 && dy == 0) || next.x < 0 || next.y < 0 || next.x >= width || next.y >= height) continue;
                if (blocked[index(next)] || out[index(next)] != std::numeric_limits<std::uint16_t>::max()) continue;
                out[index(next)] = static_cast<std::uint16_t>(out[index(at)] + 1);
                queue.push(next);
            }
    }
    return out;
}
}

int main()
{
    using namespace mb::production;
    const Point start{1,1}, goal{5,1};
    std::vector<Point> barrier;
    for (int y = 0; y < 6; ++y) barrier.push_back({3,y});
    const auto distances = field(7, 7, start, barrier);
    const auto route = reconstruct_route(7, 7, distances, start, goal);
    bool legal = !route.empty();
    for (std::size_t i = 0; i < route.size(); ++i)
    {
        for (auto wall : barrier) if (route[i] == wall) legal = false;
        if (i > 0 && !adjacent(route[i - 1], route[i])) legal = false;
    }
    check(legal && route.front() == start && route.back() == goal && route.size() == 11,
          "barrier requires ten witnessed steps despite four-tile Chebyshev distance");

    auto truncated = distances;
    truncated[12] = std::numeric_limits<std::uint16_t>::max();
    check(reconstruct_route(7, 7, truncated, start, goal).empty(), "unreached or truncated goal cannot authorize a route");
    auto corrupt = distances;
    corrupt[12] = 1;
    check(reconstruct_route(7, 7, corrupt, start, goal).empty(), "finite scalar without adjacent descent is not a route");
    auto wrong_root = distances;
    wrong_root[8] = 2;
    check(reconstruct_route(7, 7, wrong_root, start, goal).empty() &&
          reconstruct_route(7, 7, std::span(distances).first(48), start, goal).empty() &&
          reconstruct_route(7, 7, distances, {-1,1}, goal).empty(), "invalid root, dimensions and coordinates are rejected");

    Trip trip;
    check(trip.install(9, 2, 10, {{1,1},{2,1},{2,2}}) && !trip.next(10) && trip.observe(10,{1,1}) &&
          trip.next(10) == Point{2,1} && !trip.next(11), "movement authority requires this actor position and current round");
    check(trip.moved(10,{2,1},true) && trip.remaining_steps() == 1 && !trip.next(10) &&
          trip.observe(11,{2,1}) && trip.moved(11,{2,2},true) && trip.arrived() && !trip.next(11),
          "successful steps consume authority and reach the physically selected front");

    trip.install(9,2,20,{{1,1},{2,1}});
    trip.observe(20,{1,1});
    check(!trip.moved(20,{2,1},false) && !trip.valid() && !trip.next(20) && !trip.observe(20,{1,1}),
          "refused movement invalidates the trip without advancing or retry authority");
    trip.install(9,2,30,{{1,1},{2,1}});
    check(!trip.observe(31,{5,5}) && !trip.valid(), "recall or other unexpected displacement invalidates the old path");
    trip.install(9,2,40,{{1,1},{2,1}});
    trip.observe(42,{1,1});
    check(!trip.observe(41,{1,1}) && !trip.valid(), "an older observation cannot restore stale actor-round authority");

    Trip other;
    trip.install(9,2,50,{{1,1},{2,1}});
    other.install(4,0,50,{{1,1},{2,1}});
    check(trip.path.back() == other.path.back() && trip.group != other.group && trip.member != other.member,
          "a shared front retains distinct group and member action identities");
    check(!trip.install(9,2,50,{{1,1},{3,1}}) && !trip.valid(), "installation cannot bless a nonadjacent shortcut");
    trip.install(9,2,60,{{1,1},{2,1}});
    trip.observe(60,{1,1});
    check(!trip.moved(60,{1,2},true) && !trip.valid(), "an applied destination outside the witnessed continuation invalidates it");
    check(trip.install(9,2,70,{{1,1}}) && trip.arrived() && trip.remaining_steps() == 0 &&
          trip.observe(70,{1,1}) && !trip.next(70), "an already reached front is a zero-step valid trip");
    ActorDouble actor;
    check(apply_absolute_destination(actor,Point{77,11}) && actor.absolute_calls == 1 &&
          actor.relative_calls == 0 && actor.absolute_argument == Point{77,11},
          "actual caller adapter sends an absolute non-origin tile to move_absolute rather than move delta");
    std::printf("trip witness checks: %s\n", failures == 0 ? "passed" : "failed");
    return failures ? 1 : 0;
}
