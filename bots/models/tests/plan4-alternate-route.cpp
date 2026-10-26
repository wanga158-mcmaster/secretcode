// SDK-free detour fixtures; no engine, files, network, or gameplay launches.
#include "../common/alternate-route.h"
#include <cassert>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <new>

namespace allocation_probe {
bool enabled = false;
unsigned calls = 0;
}
void* operator new(std::size_t size) {
    if (allocation_probe::enabled) ++allocation_probe::calls;
    if (void* value = std::malloc(size ? size : 1)) return value;
    std::abort();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

using plan4::Point;
namespace detour = plan4::detour;
namespace {
struct Grid {
    int width, height;
    // 0 = personally free; 1 = occupied; 2 = unknown; 3 = static wall.
    std::array<unsigned char, detour::max_tiles> cells{};
    mutable unsigned occupancy_calls = 0;
    Grid(int w, int h, unsigned char initial = 0) : width(w), height(h) {
        assert(w > 0 && h > 0 && w * h <= detour::max_tiles);
        std::fill_n(cells.begin(), w * h, initial);
    }
    void set(Point p, unsigned char value) { cells[p.y * width + p.x] = value; }
    static bool walkable(Point p, const void* raw) {
        const auto& grid = *static_cast<const Grid*>(raw);
        assert(p.x >= 0 && p.y >= 0 && p.x < grid.width && p.y < grid.height);
        return grid.cells[p.y * grid.width + p.x] != 3;
    }
    static detour::Occupancy occupancy(Point p, const void* raw) {
        const auto& grid = *static_cast<const Grid*>(raw);
        assert(p.x >= 0 && p.y >= 0 && p.x < grid.width && p.y < grid.height);
        ++grid.occupancy_calls;
        const auto value = grid.cells[p.y * grid.width + p.x];
        return value == 0 ? detour::Occupancy::free : value == 1 ? detour::Occupancy::occupied : detour::Occupancy::unknown;
    }
    detour::Frame frame(Point origin, Point obstruction, std::span<const Point> goals) const {
        return {7, 17, width, height, origin, obstruction, goals,
                walkable, this, occupancy, this, {}};
    }
};
struct Work {
    mutable unsigned remaining, calls = 0;
    static bool allowed(const void* raw) {
        const auto& self = *static_cast<const Work*>(raw);
        ++self.calls;
        if (!self.remaining) return false;
        --self.remaining;
        return true;
    }
    plan4::WorkBudget budget() const { return {allowed, this}; }
};
int distance(Point a, Point b) { return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y)); }
void check_route(const Grid& grid, const detour::Frame& frame, const detour::Route& route) {
    assert(detour::valid(route));
    assert(route.actor == frame.actor && route.round == frame.round);
    assert(route.origin == frame.origin && route.obstruction == frame.obstruction);
    assert(std::find(frame.goals.begin(), frame.goals.end(), route.goal) != frame.goals.end());
    Point previous = route.origin;
    for (int i = 0; i < route.count; ++i) {
        const Point step = route.steps[i];
        assert(distance(previous, step) == 1 && step != frame.obstruction);
        assert(Grid::walkable(step, &grid));
        assert(Grid::occupancy(step, &grid) == detour::Occupancy::free);
        previous = step;
    }
    assert(previous == route.goal);
}

void sideways_and_increasing_distance() {
    {
        Grid grid(5, 4, 3);
        const Point origin{1, 2}, obstruction{2, 2};
        const std::array<Point, 1> goals{{{3, 2}}};
        for (Point p : std::array<Point, 5>{{origin, {1, 1}, {2, 0}, {3, 1}, goals[0]}}) grid.set(p, 0);
        grid.set(obstruction, 1);
        auto frame = grid.frame(origin, obstruction, goals);
        auto route = detour::plan(frame, 192);
        check_route(grid, frame, route);
        assert(route.count == 4);
        assert(distance(route.steps[0], goals[0]) == distance(origin, goals[0]));
    }
    {
        Grid grid(9, 9);
        for (int y = 1; y <= 6; ++y) { grid.set({2, y}, 3); grid.set({6, y}, 3); }
        for (int x = 2; x <= 6; ++x) grid.set({x, 1}, 3);
        const Point origin{4, 3}, obstruction{4, 2};
        grid.set(obstruction, 1);
        const std::array<Point, 1> goals{{{4, 0}}};
        auto frame = grid.frame(origin, obstruction, goals);
        auto route = detour::plan(frame, 192);
        check_route(grid, frame, route);
        assert(route.count == 13);
        assert(distance(route.steps[0], goals[0]) > distance(origin, goals[0]));
    }
}

void personally_known_complete_paths() {
    Grid grid(6, 2, 3);
    for (int x = 0; x < 6; ++x) grid.set({x, 0}, 0);
    const Point origin{0, 0}, obstruction{0, 1};
    grid.set(origin, 1); // The actor's own tile need not be reported free.
    grid.set(obstruction, 1);
    const std::array<Point, 1> goals{{{5, 0}}};
    auto frame = grid.frame(origin, obstruction, goals);
    for (unsigned char blocker : {1, 2, 3}) {
        grid.set({3, 0}, blocker);
        auto failed = detour::plan(frame, 192);
        assert(!failed.proven && failed.count == 0 && failed.visits == 3);
        assert(!detour::valid(failed));
    }
    grid.set({3, 0}, 0);
    for (unsigned char blocker : {0, 2, 3}) {
        grid.set(obstruction, blocker);
        auto failed = detour::plan(frame, 192);
        assert(!failed.proven && failed.visits == 0);
    }
    grid.set(obstruction, 1);
    for (unsigned char blocker : {1, 2, 3}) {
        grid.set(goals[0], blocker);
        auto failed = detour::plan(frame, 192);
        assert(!failed.proven && failed.count == 0 && failed.visits == 5);
    }
    grid.set(goals[0], 0);
    auto route = detour::plan(frame, 192);
    check_route(grid, frame, route);
    assert(route.count == 5);
    const std::array<Point, 2> alternatives{{obstruction, {2, 0}}};
    frame.goals = alternatives;
    route = detour::plan(frame, 192);
    check_route(grid, frame, route);
    assert(route.goal == alternatives[1]);
}

void quotas_work_and_capacity() {
    {
        Grid grid(100, 100);
        const Point origin{1, 1}, obstruction{2, 1};
        grid.set(obstruction, 1);
        const std::array<Point, 1> goals{{{99, 99}}};
        auto frame = grid.frame(origin, obstruction, goals);
        for (unsigned quota : {0u, 1u, 5u}) {
            auto route = detour::plan(frame, quota);
            assert(!route.proven && route.count == 0 && route.visits == quota);
        }
        auto route = detour::plan(frame, UINT_MAX);
        assert(!route.proven && route.count == 0 && route.visits > 0 && route.visits <= 192);
        assert(grid.occupancy_calls <= 8 * (route.visits + 6) + 4);
    }
    {
        Grid grid(70, 2, 3);
        for (int x = 0; x < 70; ++x) grid.set({x, 0}, 0);
        grid.set({0, 1}, 1);
        std::array<Point, 1> goals{{{64, 0}}};
        auto frame = grid.frame({0, 0}, {0, 1}, goals);
        auto route = detour::plan(frame, 63);
        assert(!route.proven && route.visits == 63);
        route = detour::plan(frame, 64);
        check_route(grid, frame, route);
        assert(route.count == 64 && route.visits == 64);
        auto repeated = route;
        for (int i = 32; i < repeated.count; ++i) repeated.steps[i].x -= 2;
        repeated.goal = repeated.steps[repeated.count - 1];
        // All 64 edges remain adjacent, but the short reversal repeats tiles.
        assert(!detour::valid(repeated));
        goals[0] = {65, 0};
        route = detour::plan(frame, 192);
        assert(!route.proven && route.count == 0 && route.visits == 65);
    }
    {
        Grid grid(5, 2, 3);
        for (int x = 0; x < 5; ++x) grid.set({x, 0}, 0);
        grid.set({0, 1}, 1);
        std::array<Point, 1> goals{{{4, 0}}};
        auto frame = grid.frame({0, 0}, {0, 1}, goals);
        for (unsigned allowance : {0u, 2u, 4u}) {
            Work work{allowance};
            frame.work = work.budget();
            auto route = detour::plan(frame, 192);
            assert(!route.proven && work.calls == allowance + 1);
            assert(route.visits == (allowance == 4 ? 1u : 0u));
        }
        goals[0] = {1, 0};
        Work reconstruction_stop{4};
        frame.work = reconstruction_stop.budget();
        auto route = detour::plan(frame, 192);
        assert(!route.proven && route.count == 0 && route.visits == 1);
        Work enough{5};
        frame.work = enough.budget();
        route = detour::plan(frame, 192);
        check_route(grid, frame, route);
        assert(enough.calls == 5 && route.count == 1);
    }
    {
        Grid grid(128, 128);
        grid.set({0, 1}, 1);
        const std::array<Point, 1> goals{{{1, 0}}};
        auto frame = grid.frame({0, 0}, {0, 1}, goals);
        Work before_clear_end{64};
        frame.work = before_clear_end.budget();
        auto route = detour::plan(frame, 192);
        assert(!route.proven && route.visits == 0 && before_clear_end.calls == 65);
        frame.work = {};
        route = detour::plan(frame, 192);
        check_route(grid, frame, route);
    }
}

void malformed_frames_and_routes() {
    Grid grid(5, 2, 3);
    for (int x = 0; x < 5; ++x) grid.set({x, 0}, 0);
    grid.set({0, 1}, 1);
    std::array<Point, 257> goals;
    goals.fill({4, 0});
    const auto good = grid.frame({0, 0}, {0, 1}, std::span(goals).first(256));
    const auto route = detour::plan(good, 192);
    check_route(grid, good, route);
    for (int test = 0; test < 15; ++test) {
        auto frame = good;
        switch (test) {
        case 0: frame.actor = -1; break;
        case 1: frame.round = -1; break;
        case 2: frame.width = 0; break;
        case 3: frame.height = -1; break;
        case 4: frame.width = INT_MAX; break;
        case 5: frame.height = INT_MAX; break;
        case 6: frame.goals = {}; break;
        case 7: frame.goals = goals; break;
        case 8: frame.walkable = nullptr; break;
        case 9: frame.occupancy = nullptr; break;
        case 10: frame.origin = {-1, 0}; break;
        case 11: frame.obstruction = frame.origin; break;
        case 12: frame.obstruction = {4, 1}; break;
        case 13: frame.obstruction = {INT_MIN, INT_MAX}; break;
        case 14: frame.origin = {INT_MAX, INT_MIN}; break;
        }
        const auto failed = detour::plan(frame, 192);
        assert(!failed.proven && failed.visits == 0);
    }
    goals[255] = {5, 0};
    assert(!detour::plan(good, 192).proven);
    goals[255] = good.origin;
    assert(!detour::plan(good, 192).proven);
    for (int test = 0; test < 14; ++test) {
        auto bad = route;
        switch (test) {
        case 0: bad.proven = false; break;
        case 1: bad.count = 0; break;
        case 2: bad.count = 65; break;
        case 3: bad.visits = 193; break;
        case 4: bad.visits = 0; break;
        case 5: bad.actor = -1; break;
        case 6: bad.round = -1; break;
        case 7: bad.origin = {INT_MIN, INT_MAX}; break;
        case 8: bad.obstruction = {4, 1}; break;
        case 9: bad.goal = {3, 0}; break;
        case 10: bad.steps[0] = bad.origin; break;
        case 11: bad.steps[0] = bad.obstruction; break;
        case 12: bad.steps[1] = bad.steps[0]; break;
        case 13: bad.steps[0] = {3, 0}; break;
        }
        assert(!detour::valid(bad));
        detour::Cursor cursor;
        assert(!cursor.begin(10, bad) && !cursor.active());
    }
}

void cursor_identity_actions_and_completion() {
    Grid grid(5, 2, 3);
    for (int x = 0; x < 5; ++x) grid.set({x, 0}, 0);
    grid.set({0, 1}, 1);
    const std::array<Point, 1> goals{{{4, 0}}};
    auto frame = grid.frame({0, 0}, {0, 1}, goals);
    const auto route = detour::plan(frame, 192);
    check_route(grid, frame, route);
    detour::Cursor cursor;
    Point next{};
    int remaining = -1;
    assert(!cursor.active());
    assert(!cursor.next(10, 7, 17, frame.origin, goals, next, remaining) && remaining == 0);
    for (int test = 0; test < 7; ++test) {
        assert(cursor.begin(10, route));
        auto epoch = std::uint64_t{10};
        int actor = 7, round = 17;
        Point origin = frame.origin;
        std::span<const Point> requested = goals;
        const std::array<Point, 1> changed{{{3, 0}}};
        const std::array<Point, 257> oversized{};
        switch (test) {
        case 0: epoch = 11; break;
        case 1: actor = 8; break;
        case 2: round = 16; break;
        case 3: origin = {1, 0}; break;
        case 4: requested = changed; break;
        case 5: requested = {}; break;
        case 6: requested = oversized; break;
        }
        assert(!cursor.next(epoch, actor, round, origin, requested, next, remaining));
        assert(!cursor.active() && remaining == 0);
    }
    assert(cursor.begin(10, route));
    assert(cursor.next(10, 7, 20, frame.origin, goals, next, remaining));
    assert(!cursor.next(10, 7, 19, frame.origin, goals, next, remaining));
    assert(!cursor.active());
    for (int test = 0; test < 3; ++test) {
        assert(cursor.begin(10, route));
        if (test != 2) assert(cursor.next(10, 7, 17, frame.origin, goals, next, remaining));
        cursor.commit(test != 0, test == 1 ? Point{2, 0} : Point{1, 0});
        assert(!cursor.active());
    }
    assert(cursor.begin(10, route));
    assert(cursor.next(10, 7, 17, frame.origin, goals, next, remaining));
    cursor.commit(true, next);
    assert(cursor.active());
    assert(!cursor.next(10, 7, 18, frame.origin, goals, next, remaining)); // Unexpected old origin.
    assert(!cursor.active());
    assert(cursor.begin(10, route));
    cursor.reset();
    assert(!cursor.active());
    assert(cursor.begin(10, route));
    Point origin = route.origin;
    for (int i = 0; i < route.count; ++i) {
        assert(cursor.next(10, 7, 17 + i / 2, origin, goals, next, remaining));
        assert(remaining == route.count - i && next == route.steps[i]);
        Point repeated{};
        int repeated_remaining = 0;
        assert(cursor.next(10, 7, 17 + i / 2, origin, goals, repeated, repeated_remaining));
        assert(repeated == next && repeated_remaining == remaining);
        // Occupancy and SDK move readiness must be checked by the caller here.
        cursor.commit(true, next);
        origin = next;
        assert(cursor.active() == (i + 1 < route.count));
    }
    assert(origin == goals[0]);
    assert(!cursor.next(10, 7, 20, origin, goals, next, remaining));
}
} // namespace

int main() {
    allocation_probe::enabled = true;
    sideways_and_increasing_distance();
    personally_known_complete_paths();
    quotas_work_and_capacity();
    malformed_frames_and_routes();
    cursor_identity_actions_and_completion();
    allocation_probe::enabled = false;
    assert(allocation_probe::calls == 0);
    std::puts("plan4 alternate-route fixtures passed (no heap allocations)");
}
