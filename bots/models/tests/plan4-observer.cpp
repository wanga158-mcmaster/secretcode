#define MODEL4_OBSERVER_NO_SDK
#include "../common/observer.h"
#include "../common/nav.h"
#include <cassert>
#include <memory>
#include <cstdio>

struct MaskedProvider {
    bool group_seen = false, full_seen = false;
    std::array<bool, 2> member_seen{};
    plan4::Lifecycle lifecycle = plan4::Lifecycle::living;
    int lifecycle_calls = 0, member_calls = 0;
    constexpr bool visible() const { return group_seen; }
    constexpr bool all_spawns_visible() const { return full_seen; }
    constexpr int member_count() const { return 2; }
    constexpr plan4::LifecycleObservation read_lifecycle() {
        if (!group_seen) throw "hidden lifecycle queried";
        ++lifecycle_calls; return {lifecycle, 7};
    }
    constexpr plan4::MemberObservation public_member(int) const { return {100, 10, false}; }
    constexpr bool member_visible(int member) const { return member_seen[member]; }
    constexpr plan4::MemberObservation read_member(int member) {
        if (!member_seen[member]) throw "hidden member queried";
        ++member_calls; return {20, 10, true};
    }
};

constexpr bool masking_contract() {
    plan4::Group design;
    design.full_health = design.health = 200; design.incoming_damage = 20;
    design.full_incoming_damage = 20;
    // A former exact record must not become a current hidden fact.
    design.lifecycle = plan4::Lifecycle::cleared;
    design.capture_started_round = 4;
    design.provenance = {1, plan4::Source::personal_vision, plan4::Status::exact_now};
    MaskedProvider hidden;
    auto observed = plan4::observe_group(design, hidden, 10);
    if (hidden.lifecycle_calls || hidden.member_calls || observed.health != 200
        || observed.lifecycle != plan4::Lifecycle::unknown
        || observed.provenance.status != plan4::Status::unknown) return false;
    MaskedProvider partial;
    partial.group_seen = true; partial.member_seen[0] = true;
    observed = plan4::observe_group(design, partial, 11);
    if (partial.lifecycle_calls != 1 || partial.member_calls != 1
        || observed.health != 120 || observed.occupancy_known
        || observed.provenance.status != plan4::Status::exact_now
        || observed.health_provenance.status != plan4::Status::unknown) return false;
    MaskedProvider complete;
    complete.group_seen = complete.full_seen = true;
    complete.member_seen = {true, true};
    observed = plan4::observe_group(design, complete, 12);
    if (complete.member_calls != 2 || observed.health != 40 || !observed.occupancy_known
        || observed.health_provenance.observed_round != 12
        || observed.health_provenance.status != plan4::Status::exact_now) return false;
    // Consecutive own-turn views do not rule out an intervening contest: the
    // team order can give a rival two turns between these observations.
    observed = plan4::observe_group(observed, complete, 13);
    if (observed.capture_started_round != -1) return false;
    complete.lifecycle = plan4::Lifecycle::respawning;
    complete.member_calls = 0;
    observed = plan4::observe_group(design, complete, 13);
    if (complete.member_calls || observed.health != 0 || observed.respawn_remaining != 7
        || observed.reward_available) return false;
    complete.lifecycle = plan4::Lifecycle::cleared;
    observed = plan4::observe_group(design, complete, 14);
    if (!observed.reward_available || observed.health != 0
        || observed.capture_started_round != -1) return false;
    complete.lifecycle = plan4::Lifecycle::unknown;
    observed = plan4::observe_group(design, complete, 15);
    return observed.lifecycle == plan4::Lifecycle::unknown
        && observed.provenance.status == plan4::Status::unknown;
}
static_assert(masking_contract());

struct CountedMaskProvider : MaskedProvider {
    int members = 2, visibility_calls = 0, all_spawns_calls = 0;
    constexpr bool visible() { ++visibility_calls; return group_seen; }
    constexpr bool all_spawns_visible() { ++all_spawns_calls; return full_seen; }
    constexpr int member_count() const { return members; }
};
constexpr bool observation_provider_queries() {
    plan4::Group design;
    design.full_health = 200; design.full_incoming_damage = 20;
    CountedMaskProvider hidden;
    auto observed = plan4::observe_group(design, hidden, 20);
    if (hidden.visibility_calls != 1 || hidden.all_spawns_calls
        || hidden.lifecycle_calls || hidden.member_calls || observed.visible
        || observed.all_spawns_visible || observed.occupancy_known
        || observed.health != 200) return false;
    // An invisible empty provider still has every spawn visible, without
    // asking it to repeat a visibility scan.
    CountedMaskProvider empty;
    empty.members = 0; empty.full_seen = true;
    observed = plan4::observe_group(design, empty, 21);
    if (empty.visibility_calls != 1 || empty.all_spawns_calls
        || empty.lifecycle_calls || empty.member_calls || observed.visible
        || !observed.all_spawns_visible || !observed.occupancy_known
        || observed.lifecycle != plan4::Lifecycle::unknown) return false;
    CountedMaskProvider living;
    living.group_seen = living.full_seen = true;
    living.member_seen = {true, true};
    observed = plan4::observe_group(design, living, 22);
    if (living.visibility_calls != 1 || living.all_spawns_calls != 1
        || living.lifecycle_calls != 1 || living.member_calls != 2
        || !observed.visible || !observed.all_spawns_visible
        || !observed.occupancy_known || observed.health != 40) return false;
    for (const auto state : {plan4::Lifecycle::cleared, plan4::Lifecycle::respawning}) {
        CountedMaskProvider dead;
        dead.group_seen = dead.full_seen = true; dead.lifecycle = state;
        observed = plan4::observe_group(design, dead, 23);
        if (dead.visibility_calls != 1 || dead.all_spawns_calls != 1
            || dead.lifecycle_calls != 1 || dead.member_calls
            || !observed.all_spawns_visible || !observed.occupancy_known
            || observed.lifecycle != state || observed.health != 0) return false;
    }
    return true;
}
static_assert(observation_provider_queries());

struct BudgetedProvider : MaskedProvider {
    int remaining = 0;
    constexpr bool can_continue() { return remaining-- > 0; }
};
constexpr bool budgeted_masking() {
    plan4::Group design;
    design.full_health = 200; design.full_incoming_damage = 20;
    BudgetedProvider provider;
    provider.group_seen = provider.full_seen = true;
    provider.member_seen = {true, true};
    const auto observed = plan4::observe_group(design, provider, 30);
    return provider.member_calls == 0 && observed.health == 200
        && observed.health_provenance.status == plan4::Status::unknown
        && observed.provenance.status == plan4::Status::exact_now;
}
static_assert(budgeted_masking());

struct CountedTerrain { int calls = 0; };
static bool counted_terrain(plan4::Point, const void* context) {
    ++static_cast<CountedTerrain*>(const_cast<void*>(context))->calls;
    return false;
}
struct FuelBudget { int remaining = 0; };
static bool fuel_budget(const void* context) {
    return static_cast<FuelBudget*>(const_cast<void*>(context))->remaining-- > 0;
}
struct NavigationWindow {
    plan4::NavigationPhaseBudget budget;
    std::uint64_t remaining = 90000;
    int terrain_calls = 0;
};
static bool window_budget(const void* context) {
    const auto& window = *static_cast<const NavigationWindow*>(context);
    return window.budget.permits(window.remaining, window.budget.atomic_margin());
}
static bool charged_terrain(plan4::Point, const void* context) {
    auto& window = *static_cast<NavigationWindow*>(const_cast<void*>(context));
    window.remaining -= 50; ++window.terrain_calls;
    return false;
}

static std::vector<int> oracle_field(plan4::Point target, int width, int height,
                                     const std::vector<unsigned char>& walls) {
    std::vector<int> result(std::size_t(width * height), -1), queue;
    const int first = target.y * width + target.x;
    result[first] = 0; queue.push_back(first);
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const int current = queue[head], x = current % width, y = current / width;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            if (!dx && !dy) continue;
            const int nx = x + dx, ny = y + dy, key = ny * width + nx;
            if (nx < 0 || ny < 0 || nx >= width || ny >= height || walls[key] || result[key] >= 0) continue;
            result[key] = result[current] + 1; queue.push_back(key);
        }
    }
    return result;
}

static void moving_origin_progress() {
    constexpr int width = 50, height = 50;
    std::vector<unsigned char> walls(width * height, 0);
    for (int y = 0; y < 40; ++y) walls[y * width + 20] = 1;
    for (int y = 10; y < height; ++y) walls[y * width + 35] = 1;
    const auto info = std::make_unique<plan4::Information>();
    const std::array<plan4::Point, 12> camps{{{12,5},{5,20},{10,35},{16,45},
        {25,5},{28,20},{24,35},{28,45},{40,5},{45,20},{42,35},{40,45}}};
    info->group_count = int(camps.size()); info->altar_count = 2;
    for (int g = 0; g < info->group_count; ++g) info->groups[g].position = camps[g];
    info->altars[0].position = {45,45}; info->altars[1].position = {5,12};
    std::array<std::vector<int>, 14> truth;
    for (int g = 0; g < info->group_count; ++g) truth[g] = oracle_field(camps[g], width, height, walls);
    for (int a = 0; a < 2; ++a) truth[12+a] = oracle_field(info->altars[a].position, width, height, walls);
    auto local = std::unique_ptr<unsigned char[]>(new unsigned char[walls.size()]);
    std::copy(walls.begin(), walls.end(), local.get());
    const auto nav = std::make_unique<plan4::BoundedNavigation>();
    nav->initialize_local(width, height, std::move(local)); nav->set_vertex_limit(192);
    FuelBudget work;
    nav->set_fuel_budget(fuel_budget, &work);
    int first_single = -1, all_complete = -1, total_visits = 0;
    for (int turn = 0; turn < 600; ++turn) {
        info->own.position = {2 + turn % 8, 2 + (turn / 8) % 8};
        nav->begin_turn(); work.remaining = 256; nav->prepare(*info);
        total_visits += nav->vertex_visits(); assert(nav->vertex_visits() <= 192);
        const auto lookup = nav->lookup();
        const int saved_work = work.remaining, own_key = info->own.position.y * width + info->own.position.x;
        bool all = true;
        for (int g = 0; g < info->group_count; ++g) {
            const int own = lookup.distance(info->own.position, camps[g], lookup.context);
            if (own >= 0) assert(own == truth[g][own_key]); else all = false;
            for (int a = 0; a < 2; ++a) {
                const int service = lookup.distance(camps[g], info->altars[a].position, lookup.context);
                if (service >= 0) assert(service == truth[12+a][camps[g].y * width + camps[g].x]); else all = false;
                if (own >= 0 && service >= 0 && first_single < 0) first_single = turn;
            }
            for (int h = 0; h < info->group_count; ++h) if (h != g) {
                const int between = lookup.distance(camps[g], camps[h], lookup.context);
                if (between >= 0) assert(between == truth[g][camps[h].y * width + camps[h].x]); else all = false;
            }
        }
        for (int a = 0; a < 2; ++a) {
            const int value = lookup.distance(info->own.position, info->altars[a].position, lookup.context);
            if (value >= 0) assert(value == truth[12+a][own_key]); else all = false;
        }
        assert(work.remaining == saved_work); // checking forecasts does no work
        if (all && all_complete < 0) all_complete = turn;
        // Keep moving after convergence to expose an origin-specific cache.
        if (all_complete >= 0 && turn >= all_complete + 64) { assert(all); break; }
    }
    assert(first_single >= 0 && all_complete >= 0);
    std::printf("navigationProgress 50x50 groups=12 altars=2 firstSingle=%d allLegs=%d totalVisits=%d perTurnCap=192 workChecks=256\n",
                first_single, all_complete, total_visits);
}

int main() {
    moving_origin_progress();
    assert(masking_contract());
    assert(observation_provider_queries());
    assert(budgeted_masking());
    NavigationWindow window;
    window.budget.begin(window.remaining, 75000, 15000);
    assert(window.budget.floor == 75000);
    assert(window.budget.atomic_margin() == 5000);
    assert(window.budget.startup_margin() == 6000 && window.budget.ordering_margin() == 6000);
    assert(window.budget.startup_margin() < window.budget.allowance);
    assert(window.budget.ordering_margin() < window.budget.allowance);
    // Regression: the former 10k startup guard rejected every 8k phase before
    // it could perform even one graph visit, forever leaving FARM unresolved.
    plan4::NavigationPhaseBudget former_phase;
    former_phase.begin(window.remaining, 82000, 8000);
    assert(!former_phase.permits(window.remaining, 10000));
    assert(former_phase.permits(window.remaining, former_phase.startup_margin()));
    plan4::NavigationPhaseBudget insufficient_phase;
    insufficient_phase.begin(90000, 86000, 4000);
    assert(!insufficient_phase.permits(90000, insufficient_phase.startup_margin()));
    assert(window.budget.permits(window.remaining, window.budget.startup_margin()));
    plan4::BoundedNavigation limited_phase;
    limited_phase.initialize(20, 20, {}, nullptr, charged_terrain, &window);
    limited_phase.set_fuel_budget(window_budget, &window);
    assert(limited_phase.distance({2,2}, {9,3}) == 7);
    assert(limited_phase.vertex_visits() > 0 && window.terrain_calls > 0);
    assert(window.remaining >= window.budget.floor && limited_phase.completed_pairs() == 1);
    assert(limited_phase.completed_route_entries() >= 1);
    const auto phase_visits = limited_phase.vertex_visits();
    const auto phase_terrain = window.terrain_calls;
    const auto phase_lookup = limited_phase.lookup();
    assert(phase_lookup.distance({2,2}, {9,3}, phase_lookup.context) == 7);
    plan4::BoundedNavigation tiny_quantum;
    tiny_quantum.initialize(20, 3, std::vector<unsigned char>(60));
    FuelBudget quantum;
    tiny_quantum.set_fuel_budget(fuel_budget, &quantum);
    int long_route = -1;
    for (int turn = 0; turn < 1000 && long_route < 0; ++turn) {
        tiny_quantum.begin_turn(); quantum.remaining = 10;
        long_route = tiny_quantum.distance({0,1}, {19,1});
        assert(tiny_quantum.vertex_visits() <= 256);
    }
    assert(long_route == 19);
    const auto tiny_lookup = tiny_quantum.lookup();
    const int remaining_quantum = quantum.remaining;
    assert(tiny_lookup.distance({0,1}, {19,1}, tiny_lookup.context) == 19);
    assert(quantum.remaining == remaining_quantum);
    assert(limited_phase.vertex_visits() == phase_visits && window.terrain_calls == phase_terrain);
    const auto snapshot = std::make_unique<plan4::Information>();
    snapshot->group_count = 1; snapshot->altar_count = 1;
    snapshot->groups[255].health = 9876;
    snapshot->altars[15].id = 7654;
    snapshot->own.channel = plan4::Channel::tribute; snapshot->own.channel_ticks = 9;
    snapshot->retained_group_id = 123;
    plan4::begin_observation(*snapshot, 42, {});
    assert(snapshot->round == 42 && snapshot->group_count == 0 && snapshot->altar_count == 0);
    assert(snapshot->own.channel == plan4::Channel::none && snapshot->own.channel_ticks == 0);
    assert(!snapshot->own.alive && snapshot->retained_group_id == -1);
    assert(snapshot->observation_truncated);
    assert(snapshot->groups[255].health == 9876 && snapshot->altars[15].id == 7654);
    snapshot->group_count = 12;
    for (int g = 0; g < snapshot->group_count; ++g) snapshot->groups[g].position = {100 - g, 0};
    const auto refresh = plan4::observation_order(*snapshot);
    assert(refresh.count == 12);
    for (int n = 0; n < 8; ++n) assert(refresh.indices[n] == 11 - n);
    for (int n = 8; n < 12; ++n) assert(refresh.indices[n] == n - 8);
    CountedTerrain terrain;
    plan4::BoundedNavigation host_adapter;
    host_adapter.initialize(20, 20, {}, nullptr, counted_terrain, &terrain);
    assert(host_adapter.distance({1,1}, {3,3}) == 2);
    assert(terrain.calls > 0);
    const int adapter_calls = terrain.calls;
    auto pure_lookup = host_adapter.lookup();
    assert(pure_lookup.distance({1,1}, {3,3}, pure_lookup.context) == 2);
    assert(pure_lookup.distance({1,1}, {4,4}, pure_lookup.context) == -1);
    assert(terrain.calls == adapter_calls);
    FuelBudget denied;
    host_adapter.set_fuel_budget(fuel_budget, &denied);
    assert(host_adapter.distance({1,1}, {4,4}) == -1);
    assert(terrain.calls == adapter_calls);
    assert(host_adapter.distance({1,1}, {3,3}) == 2);
    plan4::BoundedNavigation open;
    open.initialize(20, 20, std::vector<unsigned char>(400));
    int open_route = -1;
    for (int turn = 0; turn < 30 && open_route < 0; ++turn) {
        open.begin_turn();
        for (int request = 0; request < 20; ++request) open_route = open.distance({1,1}, {18,18});
    }
    assert(open_route == 17);
    assert(open.vertex_visits() <= 256);
    auto readonly = open.lookup();
    const int readonly_before = open.vertex_visits();
    assert(readonly.distance({1,1}, {18,18}, readonly.context) == 17);
    assert(readonly.distance({1,1}, {18,1}, readonly.context) == -1);
    assert(open.vertex_visits() == readonly_before);
    open.seal();
    const int before = open.vertex_visits();
    assert(open.distance({1,1}, {18,1}) == -1);
    assert(open.vertex_visits() == before);

    std::vector<unsigned char> walls(400);
    for (int y = 0; y < 18; ++y) walls[y * 20 + 10] = 1;
    plan4::BoundedNavigation detour;
    detour.initialize(20, 20, walls);
    detour.set_vertex_limit(192);
    int route = -1;
    for (int turn = 0; turn < 30 && route < 0; ++turn) {
        detour.begin_turn();
        for (int request = 0; request < 20; ++request)
            route = detour.distance({1,1}, {18,1});
        assert(detour.vertex_visits() <= 192);
    }
    assert(route > 17);
    assert(detour.distance({18,1}, {1,1}) == route);
    detour.seal();
    const int cached_visits = detour.vertex_visits();
    assert(detour.distance({1,1}, {18,1}) == route);
    assert(detour.vertex_visits() == cached_visits);
    assert(detour.distance({10,1}, {18,1}) == -1);
    std::array<plan4::Point,256> full_route;
    const int prefix_visits=detour.vertex_visits();
    assert(detour.route_prefix({1,1},{18,1},full_route.data(),256)==route);
    assert(full_route[route-1]==(plan4::Point{18,1}));
    plan4::Point previous{1,1};
    for(int n=0;n<route;++n) {
        assert(detour.walkable(full_route[n]));
        assert(std::max(std::abs(previous.x-full_route[n].x),std::abs(previous.y-full_route[n].y))==1);
        previous=full_route[n];
    }
    std::array<plan4::Point,8> short_route;
    assert(detour.route_prefix({1,1},{18,1},short_route.data(),8)==8);
    assert(std::equal(short_route.begin(),short_route.end(),full_route.begin()));
    assert(detour.route_prefix({18,1},{1,1},short_route.data(),8)==-1); // Source field cannot prove a forward prefix.
    assert(detour.route_prefix({10,1},{18,1},short_route.data(),8)==-1);
    assert(detour.route_prefix({1,1},{18,1},short_route.data(),-1)==-1);
    assert(detour.vertex_visits()==prefix_visits);
    int route_reads=0;
    const plan4::WorkBudget route_budget{[](const void* value){return ++*static_cast<int*>(const_cast<void*>(value))<=2;},&route_reads};
    assert(detour.route_prefix({1,1},{18,1},short_route.data(),8,route_budget)==-1 && route_reads==3);
    assert(detour.vertex_visits()==prefix_visits);
    plan4::BoundedNavigation direct_prefix;
    direct_prefix.initialize(8,8,std::vector<unsigned char>(64));
    assert(direct_prefix.distance({1,1},{4,4})==3);
    direct_prefix.seal();const int direct_visits=direct_prefix.vertex_visits();
    assert(direct_prefix.route_prefix({1,1},{4,4},short_route.data(),2)==2);
    assert(short_route[0]==(plan4::Point{2,2}) && short_route[1]==(plan4::Point{3,3}));
    assert(direct_prefix.route_prefix({1,1},{6,4},short_route.data(),8)==-1 && direct_prefix.vertex_visits()==direct_visits);

    plan4::BoundedNavigation interrupted;
    interrupted.initialize(20, 20, walls);
    FuelBudget interrupted_budget{25};
    interrupted.set_fuel_budget(fuel_budget, &interrupted_budget);
    assert(interrupted.distance({1,1}, {18,1}) == -1);
    int interrupted_route = -1;
    for (int turn = 0; turn < 30 && interrupted_route < 0; ++turn) {
        interrupted.begin_turn(); interrupted_budget.remaining = 10000;
        for (int request = 0; request < 20; ++request)
            interrupted_route = interrupted.distance({1,1}, {18,1});
        assert(interrupted.vertex_visits() <= 256);
    }
    assert(interrupted_route == route);

    for (int y = 18; y < 20; ++y) walls[y * 20 + 10] = 1;
    plan4::BoundedNavigation disconnected;
    disconnected.initialize(20, 20, walls);
    for (int turn = 0; turn < 15; ++turn) {
        disconnected.begin_turn();
        for (int request = 0; request < 20; ++request)
            assert(disconnected.distance({1,1}, {18,1}) == -1);
        assert(disconnected.vertex_visits() <= 256);
    }
}
