// Pure path and work-boundary scenarios. No SDK imports or engine fixtures.
#include "../common/movement.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace allocation_probe {
bool enabled = false;
std::size_t calls = 0;
}
void* operator new(std::size_t size) {
    if (void* result = std::malloc(size ? size : 1)) {
        if (allocation_probe::enabled) ++allocation_probe::calls;
        return result;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

using plan4::MovementStatus;
using plan4::PersistentMovement;
using plan4::Point;

namespace {
int chebyshev(Point a, Point b) { return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y)); }
struct Occupants {
    std::vector<Point> blocked;
    static bool contains(Point p, const void* context) {
        const auto& values = static_cast<const Occupants*>(context)->blocked;
        return std::find(values.begin(), values.end(), p) != values.end();
    }
};
struct WorkCounter {
    mutable unsigned remaining = 0, calls = 0;
    static bool allowed(const void* context) {
        const auto& counter = *static_cast<const WorkCounter*>(context);
        ++counter.calls;
        if (!counter.remaining) return false;
        --counter.remaining;
        return true;
    }
    plan4::WorkBudget budget() const { return {allowed, this}; }
};

void u_wall_requires_moving_away() {
    std::vector<unsigned char> walls(9 * 9);
    for (int y = 1; y <= 6; ++y) walls[y * 9 + 2] = walls[y * 9 + 6] = 1;
    for (int x = 2; x <= 6; ++x) walls[1 * 9 + x] = 1;
    PersistentMovement route;
    route.initialize(9, 9, walls);
    Point actor{4, 3};
    const std::vector<Point> goals{{4, 0}};
    int moves = 0;
    bool first = true;
    for (int turn = 0; turn < 100 && actor != goals[0]; ++turn) {
        route.begin_turn(248, 256); // Eight expansions; retain the frontier.
        const auto step = route.next_step(actor, goals);
        assert(step.visits <= 8 && route.vertex_visits() <= 8);
        if (step.status == MovementStatus::in_progress) continue;
        assert(step.status == MovementStatus::ready);
        assert(chebyshev(actor, step.next) == 1);
        assert(!walls[step.next.y * 9 + step.next.x]);
        if (first) { assert(chebyshev(step.next, goals[0]) > chebyshev(actor, goals[0])); first = false; }
        route.notify_move_result(actor, step.next, true);
        actor = step.next;
        ++moves;
    }
    assert(actor == goals[0]);
    assert(moves == 13); // Exact eight-neighbor route around the U's bottom.
}

void budgets_and_cached_routes() {
    for (unsigned allowance : {64u, 256u}) {
        PersistentMovement route;
        route.initialize(40, 40, std::vector<unsigned char>(1600));
        const std::vector<Point> goals{{39, 39}};
        plan4::MovementStep result;
        int turns = 0;
        do {
            route.begin_turn(0, allowance);
            result = route.next_step({0, 0}, goals);
            assert(result.visits <= allowance && route.vertex_visits() <= allowance);
            if (result.status == MovementStatus::in_progress) {
                const auto again = route.next_step({0, 0}, goals);
                assert(again.visits == 0); // Repeated calls cannot reset the budget.
                assert(again.status == MovementStatus::in_progress);
            }
            ++turns;
        } while (result.status == MovementStatus::in_progress && turns <= 30);
        assert(result.status == MovementStatus::ready && result.remaining_distance == 39);
        assert(turns > 1 && turns <= 25);
        route.begin_turn(0, allowance);
        const auto cached = route.next_step(result.next, goals);
        assert(cached.status == MovementStatus::ready && cached.visits == 0);
        assert(cached.remaining_distance == 38);
    }
    PersistentMovement remaining;
    remaining.initialize(40, 40, std::vector<unsigned char>(1600));
    const std::vector<Point> goals{{39, 39}};
    remaining.begin_turn(192, 256);
    assert(remaining.next_step({0, 0}, goals).visits == 64);
    remaining.begin_turn(256, 256);
    const auto no_budget = remaining.next_step({0, 0}, goals);
    assert(no_budget.status == MovementStatus::in_progress && no_budget.visits == 0);
    remaining.begin_turn(0, 256);
    assert(remaining.next_step({0, 0}, goals, nullptr, nullptr, 7).visits == 7);
}

void unknown_differs_from_unreachable() {
    PersistentMovement route;
    std::vector<unsigned char> walls(49);
    for (int x = 0; x < 7; ++x) walls[3 * 7 + x] = 1;
    route.initialize(7, 7, walls);
    const std::vector<Point> goals{{1, 5}};
    route.begin_turn(0, 8);
    assert(route.next_step({1, 1}, goals).status == MovementStatus::in_progress);
    route.begin_turn(0, 256);
    const auto exhausted = route.next_step({1, 1}, goals);
    assert(exhausted.status == MovementStatus::unreachable);
    assert(exhausted.visits <= 256);
    route.begin_turn();
    assert(route.next_step({1, 1}, goals).visits == 0);
}

void dynamic_blockers_and_hidden_failure_are_transient() {
    PersistentMovement route;
    route.initialize(7, 3, std::vector<unsigned char>(21));
    const Point actor{0, 1};
    const std::vector<Point> goals{{6, 1}};
    route.begin_turn();
    const auto first = route.next_step(actor, goals);
    assert(first.status == MovementStatus::ready);
    const auto generation = route.cache_generation();
    Occupants occupied{{first.next}};
    route.begin_turn();
    const auto alternative = route.next_step(actor, goals, Occupants::contains, &occupied);
    assert(alternative.status == MovementStatus::ready && alternative.next != first.next);
    assert(alternative.visits == 0 && route.cache_generation() == generation);
    occupied.blocked = {{1, 0}, {1, 1}, {1, 2}};
    assert(route.next_step(actor, goals, Occupants::contains, &occupied).status == MovementStatus::blocked);
    occupied.blocked.clear();
    assert(route.next_step(actor, goals, Occupants::contains, &occupied).status == MovementStatus::ready);
    route.notify_move_result(actor, first.next, false);
    route.begin_turn();
    const auto after_hidden_collision = route.next_step(actor, goals);
    assert(after_hidden_collision.status == MovementStatus::ready && after_hidden_collision.next != first.next);
    // After one avoided proposal, the formerly hidden tile can be used again.
    occupied.blocked = {{1, 0}, {1, 1}, {1, 2}};
    occupied.blocked.erase(std::remove(occupied.blocked.begin(), occupied.blocked.end(), first.next), occupied.blocked.end());
    const auto retry = route.next_step(actor, goals, Occupants::contains, &occupied);
    assert(retry.status == MovementStatus::ready && retry.next == first.next);
    assert(route.cache_generation() == generation);
}

void changed_goals_and_origins() {
    PersistentMovement route;
    route.initialize(40, 40, std::vector<unsigned char>(1600));
    std::vector<Point> goals{{39, 39}};
    route.begin_turn(0, 64);
    assert(route.next_step({0, 0}, goals).status == MovementStatus::in_progress);
    const auto generation = route.cache_generation();
    // A reverse field remains useful if the actor moves while it is incomplete.
    const auto nearby = route.next_step({38, 38}, goals);
    assert(nearby.status == MovementStatus::ready && nearby.visits == 0);
    assert(route.cache_generation() == generation);
    goals = {{0, 1}};
    route.begin_turn(0, 64);
    const auto retargeted = route.next_step({0, 0}, goals);
    assert(retargeted.status == MovementStatus::ready && retargeted.next == goals[0]);
    assert(route.cache_generation() != generation);
    const auto current = route.cache_generation();
    goals = {{0, 1}, {0, 1}};
    const auto duplicate_goal = route.next_step({0, 0}, goals);
    assert(duplicate_goal.status == MovementStatus::ready);
    assert(route.cache_generation() == current);
    assert(route.next_step({0, 1}, goals).status == MovementStatus::at_goal);
}

void invalid_inputs_and_public_impossibility() {
    PersistentMovement route;
    bool rejected = false;
    try { route.initialize(2, 2, std::vector<unsigned char>(3)); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    std::vector<unsigned char> walls(9); walls[4] = 1;
    route.initialize(3, 3, walls);
    route.begin_turn();
    const std::vector<Point> goals{{2, 2}};
    assert(route.next_step({1, 1}, goals).status == MovementStatus::invalid_origin);
    const std::vector<Point> impossible{{1, 1}, {-1, 0}};
    assert(route.next_step({0, 0}, impossible).status == MovementStatus::unreachable);
}

void fuel_budget_stops_reset_and_vertices_then_resumes() {
    PersistentMovement route;
    route.initialize(40, 40, std::vector<unsigned char>(1600));
    const std::vector<Point> goals{{39, 39}};
    WorkCounter work;
    route.begin_turn();
    const auto stopped = route.next_step({0, 0}, goals, nullptr, nullptr, 256, work.budget());
    assert(stopped.status == MovementStatus::in_progress && stopped.visits == 0);
    assert(route.cache_generation() == 0); // Nothing was read or published.
    work.remaining = 2;
    const auto clearing = route.next_step({0, 0}, goals, nullptr, nullptr, 256, work.budget());
    assert(clearing.status == MovementStatus::in_progress && clearing.visits == 0);
    const auto generation = route.cache_generation();
    assert(generation != 0);
    // Reset can pause at any bitmap prefix without exposing uninitialized
    // distance/queue entries, then finish under later owner-turn allowances.
    int turns = 0;
    plan4::MovementStep result;
    do {
        work.remaining = 20;
        route.begin_turn(192, 256);
        result = route.next_step({0, 0}, goals, nullptr, nullptr, 256, work.budget());
        assert(result.visits <= 19 && route.vertex_visits() <= 64);
        assert(route.cache_generation() == generation);
        ++turns;
    } while (result.status == MovementStatus::in_progress && turns <= 150);
    assert(result.status == MovementStatus::ready && turns > 1);
    // Cached distances also respect a denied budget before proposing an action.
    work.remaining = 0;
    route.begin_turn();
    const auto denied_cached = route.next_step({0, 0}, goals, nullptr, nullptr, 256, work.budget());
    assert(denied_cached.status == MovementStatus::in_progress && denied_cached.visits == 0);
    work.remaining = 100;
    const auto cached = route.next_step({0, 0}, goals, nullptr, nullptr, 256, work.budget());
    assert(cached.status == MovementStatus::ready && cached.visits == 0);
}

void partially_seeded_goals_cannot_publish_or_read_stale_distances() {
    PersistentMovement route;
    route.initialize(3, 3, std::vector<unsigned char>(9));
    const std::vector<Point> goals{{0, 0}, {2, 0}, {0, 2}, {2, 2}};
    WorkCounter work{3, 0}; // Entry, two-byte bitmap clear, first seed.
    route.begin_turn();
    const auto partial = route.next_step({0, 0}, goals, nullptr, nullptr, 256, work.budget());
    assert(partial.status == MovementStatus::in_progress && partial.visits == 0);
    work.remaining = 100;
    const auto completed = route.next_step({0, 0}, goals, nullptr, nullptr, 256, work.budget());
    assert(completed.status == MovementStatus::at_goal && completed.visits == 0);
    // Retargeting discards the previous seed/distance field even when reset
    // itself must suspend before its first byte is cleared.
    const std::vector<Point> moved{{2, 1}};
    work.remaining = 1;
    const auto reset = route.next_step({0, 0}, moved, nullptr, nullptr, 256, work.budget());
    assert(reset.status == MovementStatus::in_progress && reset.visits == 0);
    work.remaining = 100;
    const auto retargeted = route.next_step({0, 0}, moved, nullptr, nullptr, 256, work.budget());
    assert(retargeted.status == MovementStatus::ready && retargeted.remaining_distance == 2);
}

void canonical_goals_preserve_partial_reset_and_started_bfs() {
    PersistentMovement route;
    route.initialize(40, 40, std::vector<unsigned char>(1600));
    const std::vector<Point> goals{{37, 39}, {38, 39}, {39, 39}};
    WorkCounter work{2, 0}; // Entry and first 32-byte bitmap-clear chunk.
    route.begin_turn(0, 64);
    auto result = route.next_step({37, 39}, goals, nullptr, nullptr, 256, work.budget());
    assert(result.status == MovementStatus::in_progress && result.visits == 0);
    const auto generation = route.cache_generation();
    assert(generation == 1);

    // The first normalization swapped its entire vector into goals_, leaving
    // no spare normalization capacity. Old repeated normalization allocates
    // here; the exact canonical path must not build that unused vector.
    allocation_probe::calls = 0;
    allocation_probe::enabled = true;
    work.remaining = 2;
    result = route.next_step({37, 39}, goals, nullptr, nullptr, 256, work.budget());
    allocation_probe::enabled = false;
    assert(allocation_probe::calls == 0 && route.cache_generation() == generation);
    assert(result.status == MovementStatus::in_progress && result.visits == 0);

    // Five more chunks complete the original 200-byte bitmap, not a reset
    // restarted by repeated input. Then all three seed entries must resume.
    for (int chunk = 0; chunk < 5; ++chunk) {
        work.remaining = 2;
        result = route.next_step({37, 39}, goals, nullptr, nullptr, 256, work.budget());
        assert(result.status == MovementStatus::in_progress && result.visits == 0);
        assert(route.cache_generation() == generation);
    }
    work.remaining = 2;
    result = route.next_step({37, 39}, goals, nullptr, nullptr, 256, work.budget());
    assert(result.status == MovementStatus::in_progress && result.visits == 0); // First seed only.
    work.remaining = 3;
    result = route.next_step({37, 39}, goals, nullptr, nullptr, 256, work.budget());
    assert(result.status == MovementStatus::at_goal && result.visits == 0);
    assert(route.cache_generation() == generation);

    route.begin_turn(192, 256);
    result = route.next_step({0, 0}, goals);
    assert(result.status == MovementStatus::in_progress && result.visits == 64);
    const auto repeated = route.next_step({0, 0}, goals);
    assert(repeated.status == MovementStatus::in_progress && repeated.visits == 0);
    const auto cached = route.next_step({38, 38}, goals);
    assert(cached.status == MovementStatus::ready && cached.remaining_distance == 1 && cached.visits == 0);
    assert(route.vertex_visits() == 64 && route.cache_generation() == generation);

    // Arbitrary order and duplicate input still normalize to the same field.
    const std::vector<Point> permutation{{39, 39}, {37, 39}, {38, 39}};
    const std::vector<Point> duplicates{{38, 39}, {39, 39}, {37, 39}, {38, 39}};
    assert(route.next_step({38, 38}, permutation).status == MovementStatus::ready);
    assert(route.next_step({38, 38}, duplicates).status == MovementStatus::ready);
    assert(route.cache_generation() == generation && route.vertex_visits() == 64);

    // (-3,40) has the same linear index as (37,39), but is not a legal goal.
    const std::vector<Point> alias{{-3, 40}, {38, 39}, {39, 39}};
    route.begin_turn(0, 256);
    const auto filtered = route.next_step({37, 39}, alias);
    assert(route.cache_generation() == generation + 1);
    assert(filtered.status == MovementStatus::ready && filtered.remaining_distance == 1);
    const std::vector<Point> valid_remainder{{38, 39}, {39, 39}};
    assert(route.next_step({38, 39}, valid_remainder).status == MovementStatus::at_goal);
    assert(route.cache_generation() == generation + 1);

    const std::vector<Point> changed{{0, 1}};
    route.begin_turn(0, 256);
    const auto retargeted = route.next_step({0, 0}, changed);
    assert(retargeted.status == MovementStatus::ready && retargeted.remaining_distance == 1);
    assert(route.cache_generation() == generation + 2);
    assert(route.next_step({0, 0}, changed).status == MovementStatus::ready);
    assert(route.cache_generation() == generation + 2 && route.vertex_visits() <= 256);
}
} // namespace

int main() {
    u_wall_requires_moving_away();
    budgets_and_cached_routes();
    unknown_differs_from_unreachable();
    dynamic_blockers_and_hidden_failure_are_transient();
    changed_goals_and_origins();
    invalid_inputs_and_public_impossibility();
    fuel_budget_stops_reset_and_vertices_then_resumes();
    partially_seeded_goals_cannot_publish_or_read_stale_distances();
    canonical_goals_preserve_partial_reset_and_started_bfs();
    std::cout << "PLAN4_MOVEMENT {\"passed\":true,\"scenarios\":9,\"engineLaunches\":0}\n";
}
