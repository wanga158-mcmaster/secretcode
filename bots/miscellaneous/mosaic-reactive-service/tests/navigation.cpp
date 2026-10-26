#define MOSAIC_NAV_CORE_ONLY
#include "navigation.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <queue>
#include <vector>

namespace {
struct Pos { int x{}, y{}; bool operator==(const Pos&) const = default; };
constexpr int side = 8;
constexpr int count = side * side;
int index(Pos p) { return p.y * side + p.x; }
bool inside(Pos p) { return p.x >= 0 && p.y >= 0 && p.x < side && p.y < side; }
struct Mask {
    std::uint64_t bits{};
    bool operator==(const Mask&) const = default;
    bool is_empty() const { return bits == 0; }
    bool is_set(Pos p) const { return inside(p) && (bits & (std::uint64_t{1} << index(p))); }
    void set(Pos p, bool on = true) {
        const auto bit = std::uint64_t{1} << index(p);
        if (on) bits |= bit; else bits &= ~bit;
    }
    Mask& operator|=(const Mask& other) { bits |= other.bits; return *this; }
    Mask& operator&=(const Mask& other) { bits &= other.bits; return *this; }
    friend Mask operator~(Mask mask) { mask.bits = ~mask.bits; return mask; }
};
struct Backend {
    struct Field {
        std::array<std::uint16_t, count> distances;
        Field() { distances.fill(0xffff); }
        std::uint16_t get(Pos p) const { return distances[index(p)]; }
    };
    int epoch{5}, searches{};
    std::uint16_t last_cap{};
    std::uint64_t chunks_per_mask{1};
    std::uint64_t fuel{100000};
    bool change_epoch{}, consume_fuel{}, change_epoch_on_reachable{};
    bool same_epoch(int expected) const { return epoch == expected; }
    std::uint64_t remaining() const { return fuel; }
    std::uint64_t chunks(const Mask&) const { return chunks_per_mask; }
    void search(Field& field, const Mask& blocked, const Mask& goals, Pos,
        Mask& visited, std::uint16_t max_distance) {
        ++searches;
        last_cap = max_distance;
        std::queue<Pos> pending;
        for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
            const Pos p{x,y};
            if (goals.is_set(p) && !blocked.is_set(p)) {
                field.distances[index(p)] = 0;
                visited.set(p);
                pending.push(p);
            }
        }
        while (!pending.empty()) {
            const auto p = pending.front(); pending.pop();
            const auto d = field.get(p);
            if (d >= max_distance) continue;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                const Pos q{p.x+dx,p.y+dy};
                if ((!dx && !dy) || !inside(q) || blocked.is_set(q) || visited.is_set(q)) continue;
                field.distances[index(q)] = d + 1;
                visited.set(q); pending.push(q);
            }
        }
        if (change_epoch) ++epoch;
        if (consume_fuel) fuel = 1000;
    }
    std::vector<Pos> reachable(const Mask& blocked, Pos origin, int speed) {
        if (change_epoch_on_reachable) ++epoch;
        std::vector<Pos> answer{origin};
        std::array<int,count> distances; distances.fill(-1); distances[index(origin)] = 0;
        for (std::size_t i = 0; i < answer.size(); ++i) {
            const auto p = answer[i];
            if (distances[index(p)] >= speed) continue;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                const Pos q{p.x+dx,p.y+dy};
                if ((!dx && !dy) || !inside(q) || blocked.is_set(q) || distances[index(q)] >= 0) continue;
                distances[index(q)] = distances[index(p)] + 1;
                answer.push_back(q);
            }
        }
        return answer;
    }
};
using namespace mosaic::nav;
using Input = InputT<Mask,Pos>;
using Plan = MovePlanT<Pos>;
using Cache = PotentialCache<Backend,Mask,Pos>;
void require(bool condition, int line) {
    if (!condition) { std::fprintf(stderr,"navigation assertion at %d\n",line); std::exit(1); }
}
#define CHECK(condition) require(bool(condition), __LINE__)
Input input(const Mask& terrain, const Mask& dynamic, Pos origin) {
    return {terrain,dynamic,origin,1,5,2,123,true,true,true};
}
Mask goal(Pos p) { Mask answer; answer.set(p); return answer; }
DestinationsT<Pos> packet(const Input& frame, const std::vector<Pos>& tiles) {
    return {tiles,frame.epoch,frame.actor,frame.revision,frame.origin};
}
// Independent origin-rooted oracle. The planner backend searches from all goals.
int oracle(const Mask& blocked, Pos origin, const Mask& goals) {
    std::queue<Pos> pending; pending.push(origin);
    std::array<int,count> distances; distances.fill(-1); distances[index(origin)] = 0;
    while (!pending.empty()) {
        const auto p = pending.front(); pending.pop();
        if (goals.is_set(p)) return distances[index(p)];
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            const Pos q{p.x+dx,p.y+dy};
            if ((!dx && !dy) || !inside(q) || blocked.is_set(q) || distances[index(q)] >= 0) continue;
            distances[index(q)] = distances[index(p)] + 1; pending.push(q);
        }
    }
    return -1;
}

void exhaustive_frozen_graphs() {
    constexpr std::array<Pos,7> variable{{{1,0},{2,0},{0,1},{1,1},{2,1},{0,2},{1,2}}};
    const Pos origin{0,0}, target{2,2};
    const auto goals = goal(target);
    for (unsigned subset = 0; subset < (1u << variable.size()); ++subset) {
        Mask terrain, dynamic;
        for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x)
            if (x > 2 || y > 2) terrain.set({x,y});
        for (unsigned bit = 0; bit < variable.size(); ++bit)
            if (subset & (1u << bit)) (bit & 1 ? dynamic : terrain).set(variable[bit]);
        auto blocked = terrain; blocked |= dynamic;
        const int initial_distance = oracle(blocked,origin,goals);
        Backend backend;
        const auto first = plan_core(backend,input(terrain,dynamic,origin),goals);
        CHECK(first.expected_epoch == 5 && first.actor == 2 && first.revision == 123);
        CHECK(first.expected_origin == origin);
        if (initial_distance < 0) {
            CHECK(first.result == Result::incomplete && !first.destination && !first.search_complete);
            continue;
        }
        CHECK(first.route_distance == initial_distance && first.search_complete);
        auto position = origin;
        int applied = 0;
        while (position != target) {
            const auto next = plan_core(backend,input(terrain,dynamic,position),goals);
            CHECK(next.result == Result::step_ready && next.destination);
            CHECK(!blocked.is_set(*next.destination));
            CHECK(oracle(blocked,*next.destination,goals) < oracle(blocked,position,goals));
            position = *next.destination;
            CHECK(++applied <= initial_distance);
        }
        const auto arrived = plan_core(backend,input(terrain,dynamic,position),goals);
        CHECK(arrived.result == Result::arrived && arrived.route_distance == 0 && !arrived.destination);
    }
}

void result_boundaries() {
    Mask terrain,dynamic;
    Backend backend;
    auto frame = input(terrain,dynamic,{0,0});
    auto goals = goal({7,7});
    Limits short_search; short_search.max_layers = 2;
    auto answer = plan_core(backend,frame,goals,static_cast<const Mask*>(nullptr),
        static_cast<const Mask*>(nullptr),short_search);
    CHECK(answer.result == Result::incomplete && !answer.destination && !answer.search_complete);
    backend.fuel = 3000;
    answer = plan_core(backend,frame,goals);
    CHECK(answer.result == Result::incomplete && backend.searches == 1);
    backend.fuel = 100000; backend.change_epoch = true;
    answer = plan_core(backend,frame,goals);
    CHECK(answer.result == Result::incomplete && !answer.destination && answer.route_distance == 0xffff);
    // A stale frame cannot even certify geometric arrival.
    answer = plan_core(backend,frame,goal(frame.origin));
    CHECK(answer.result == Result::incomplete);
    backend.epoch = 5; backend.change_epoch = false; backend.consume_fuel = true;
    answer = plan_core(backend,frame,goals);
    CHECK(answer.result == Result::incomplete && !answer.destination && answer.route_distance == 0xffff);
    backend.consume_fuel = false; backend.fuel = 100000;
    // Even a route distance already read before a later epoch change is revoked.
    backend.change_epoch_on_reachable = true;
    answer = plan_core(backend,frame,goals);
    CHECK(answer.result == Result::incomplete && !answer.destination
        && !answer.search_complete && answer.route_distance == 0xffff);
    backend.change_epoch_on_reachable = false; backend.epoch = 5;
    frame.complete = false;
    CHECK(plan_core(backend,frame,goal(frame.origin)).result == Result::incomplete);
    frame.complete = true; frame.can_move = false;
    CHECK(plan_core(backend,frame,goals).result == Result::unavailable);
    CHECK(plan_core(backend,frame,goal(frame.origin)).result == Result::arrived);
    frame.can_move = true;
    dynamic.set({7,7});
    CHECK(plan_core(backend,frame,goals).result == Result::blocked);
    dynamic.set({7,7},false);
    Mask forbidden = goal(frame.origin);
    CHECK(plan_core(backend,frame,goal(frame.origin),&forbidden).result == Result::blocked);
    auto alternative = goal({1,0}); alternative.set(frame.origin);
    answer = plan_core(backend,frame,alternative,&forbidden);
    CHECK((answer.result == Result::step_ready && answer.destination == std::optional<Pos>{{1,0}}));
}

void geometry_is_not_route_cost() {
    Mask terrain,dynamic;
    for (int y = 0; y < 7; ++y) terrain.set({3,y});
    Backend backend;
    const Pos origin{1,1};
    const auto goals = goal({5,1});
    const int exact = oracle(terrain,origin,goals);
    CHECK(exact > 4); // Chebyshev range alone misses the necessary detour.
    const auto answer = plan_core(backend,input(terrain,dynamic,origin),goals);
    CHECK(answer.result == Result::step_ready && answer.route_distance == exact);
}

void cache_keys_and_stamps() {
    Mask terrain,dynamic;
    terrain.set({6,6});
    auto frame = input(terrain,dynamic,{0,0});
    auto goals = goal({3,0});
    Cache cache;
    CHECK(cache.prepare(frame,goals) == PrepareResult::ready);
    CHECK(cache.backend().searches == 1);
    CHECK(cache.prepare(frame,goals) == PrepareResult::reused);
    // A raw goal on an impassable static tile leaves the normalized key equal.
    goals.set({6,6});
    CHECK(cache.prepare(frame,goals) == PrepareResult::reused);
    CHECK(cache.backend().searches == 1);
    // Equal cardinality is insufficient identity: moving one wall rebuilds.
    goals.set({6,6},false);
    terrain.set({6,6},false); terrain.set({6,5});
    CHECK(cache.prepare(frame,goals) == PrepareResult::ready);
    CHECK(cache.backend().searches == 2);
    goals = goal({4,0});
    CHECK(cache.prepare(frame,goals) == PrepareResult::ready);
    CHECK(cache.backend().searches == 3);

    // Actor, origin and epoch change stamps, not the static mathematical key.
    frame.origin = {1,0}; frame.actor = 7; frame.epoch = 6; frame.revision = 124;
    cache.backend().epoch = 6;
    CHECK(cache.prepare(frame,goals) == PrepareResult::reused);
    CHECK(cache.backend().searches == 3);
    const std::vector<Pos> destinations{{1,0},{2,0}};
    auto fresh = packet(frame,destinations);
    const auto answer = cache.propose(frame,fresh);
    CHECK(answer.result == Result::step_ready && answer.destination == std::optional<Pos>({2,0}));
    CHECK(answer.expected_epoch == 6 && answer.actor == 7 && answer.revision == 124);
    CHECK(answer.expected_origin == frame.origin && answer.route_distance == 3);
    CHECK(answer.distance_kind == DistanceKind::static_potential);
    for (int stale = 0; stale < 4; ++stale) {
        auto old = fresh;
        if (stale == 0) --old.epoch;
        if (stale == 1) --old.actor;
        if (stale == 2) --old.revision;
        if (stale == 3) old.origin = {0,0};
        const auto rejected = cache.propose(frame,old);
        CHECK(rejected.result == Result::incomplete && !rejected.destination
            && rejected.route_distance == 0xffff);
    }
    // A changed static input must be prepared before a cached proposal is used.
    terrain.set({6,4});
    CHECK(cache.propose(frame,fresh).result == Result::incomplete);
}

void cache_uses_only_fresh_landings() {
    Mask terrain,dynamic;
    auto frame = input(terrain,dynamic,{0,0});
    const auto goals = goal({4,0});
    Cache cache;
    CHECK(cache.prepare(frame,goals) == PrepareResult::ready);
    // With no destination packet evidence, a static descent is not executable.
    const std::vector<Pos> origin_only{{0,0}};
    CHECK(cache.propose(frame,packet(frame,origin_only)).result == Result::local_blocked);
    dynamic.set({1,0}); dynamic.set({0,1}); dynamic.set({1,1});
    auto blocked = terrain; blocked |= dynamic;
    auto landings = cache.backend().reachable(blocked,frame.origin,frame.move_speed);
    CHECK(cache.prepare(frame,goals) == PrepareResult::reused);
    CHECK(cache.propose(frame,packet(frame,landings)).result == Result::local_blocked);
    // No old occupancy survives a new frame. The same static field now advances.
    dynamic.set({1,0},false);
    ++frame.epoch; ++frame.revision; cache.backend().epoch = frame.epoch;
    blocked = terrain; blocked |= dynamic;
    landings = cache.backend().reachable(blocked,frame.origin,frame.move_speed);
    CHECK(cache.prepare(frame,goals) == PrepareResult::reused);
    const auto answer = cache.propose(frame,packet(frame,landings));
    CHECK(answer.result == Result::step_ready && answer.destination == std::optional<Pos>({1,0}));
    CHECK(cache.backend().searches == 1);
    // Defensive current-mask filtering rejects even an incorrectly supplied tile.
    dynamic.set({1,0});
    CHECK(cache.propose(frame,packet(frame,landings)).result == Result::local_blocked);
    dynamic.set({1,0},false);
    frame.can_move = false;
    CHECK(cache.propose(frame,packet(frame,landings)).result == Result::unavailable);
    frame.can_move = true; frame.complete = false;
    CHECK(cache.propose(frame,packet(frame,landings)).result == Result::incomplete);
    frame.complete = true;
    std::vector<Pos> oversized(82,Pos{1,0});
    CHECK(cache.propose(frame,packet(frame,oversized)).result == Result::incomplete);
    cache.backend().fuel = 20000;
    CHECK(cache.propose(frame,packet(frame,landings)).result == Result::incomplete);
}

void cache_partial_fields_and_admission() {
    Mask terrain,dynamic;
    auto frame = input(terrain,dynamic,{0,0});
    const auto goals = goal({7,7});
    Cache cache;
    CacheLimits short_search; short_search.max_layers = 2;
    CHECK(cache.prepare(frame,goals,short_search) == PrepareResult::incomplete);
    const std::vector<Pos> origin_steps{{0,0},{1,1}};
    CHECK(cache.propose(frame,packet(frame,origin_steps)).result == Result::incomplete);
    // A covered origin can use the partial field; an uncovered one still cannot.
    frame.origin = {6,6}; ++frame.revision;
    CHECK(cache.prepare(frame,goals,short_search) == PrepareResult::reused);
    const std::vector<Pos> near_steps{{6,6},{7,7}};
    CHECK(cache.propose(frame,packet(frame,near_steps)).result == Result::step_ready);
    CHECK(cache.backend().searches == 1);
    frame.origin = {0,0}; ++frame.revision;
    CHECK(cache.propose(frame,packet(frame,origin_steps)).result == Result::incomplete);
    CHECK(cache.prepare(frame,goals) == PrepareResult::ready);
    CHECK(cache.backend().searches == 2);
    CHECK(cache.propose(frame,packet(frame,origin_steps)).route_distance == 7);

    Cache stale_search;
    stale_search.backend().change_epoch = true;
    CHECK(stale_search.prepare(frame,goals) == PrepareResult::incomplete);
    ++frame.epoch; ++frame.revision;
    stale_search.backend().change_epoch = false;
    CHECK(stale_search.prepare(frame,goals) == PrepareResult::reused);
    // Search facts are static, but no stale frame can consume them.
    auto stale = packet(frame,origin_steps); --stale.epoch;
    CHECK(stale_search.propose(frame,stale).result == Result::incomplete);

    Cache admitted;
    frame.epoch = admitted.backend().epoch;
    admitted.backend().fuel = 50000; admitted.backend().chunks_per_mask = 169;
    CHECK(admitted.prepare(frame,goals) == PrepareResult::ready);
    CHECK(admitted.backend().last_cap == 131); // Entry fuel, one setup allowance.
    Cache deferred;
    deferred.backend().fuel = 26000; deferred.backend().chunks_per_mask = 169;
    CHECK(deferred.prepare(frame,goals) == PrepareResult::incomplete);
    CHECK(deferred.backend().searches == 0);
    terrain.set({7,7});
    CHECK(admitted.prepare(frame,goals) == PrepareResult::no_static_goal);
    CHECK(admitted.propose(frame,packet(frame,origin_steps)).result == Result::local_blocked);
}
}

int main() {
    exhaustive_frozen_graphs();
    result_boundaries();
    geometry_is_not_route_cost();
    cache_keys_and_stamps();
    cache_uses_only_fresh_landings();
    cache_partial_fields_and_admission();
    std::puts("navigation: 128 frozen graphs, exact static cache keys, fresh landing stamps, partial fields and admission pass");
}
