#include <cmath>
#include <cstdio>
#include "capital_model.h"

using namespace mb::capital;
static int failures;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line%d: %s\n", __LINE__, #x); ++failures; } } while (0)

int main()
{
    Work nearby{ 2000, 60000, 60, 20, 60, 1, 0, 3, 10, 6, 0 };
    Build base{ 80, 0, 1800 };
    Build geared = base;
    geared.strength = 160;
    const auto a = flow(std::span{ &nearby, 1 }, base);
    const auto b = flow(std::span{ &nearby, 1 }, geared);
    CHECK(b.gems > a.gems);
    const float damage_value = bundle_value(a, b, 0, 600);
    nearby.travel = 400;
    const auto travel_a = flow(std::span{ &nearby, 1 }, base);
    const auto travel_b = flow(std::span{ &nearby, 1 }, geared);
    CHECK(bundle_value(travel_a, travel_b, 0, 600) < damage_value * 0.05f);
    nearby.travel = 3;
    nearby.respawn = 600;
    const auto spawn_a = flow(std::span{ &nearby, 1 }, base);
    const auto spawn_b = flow(std::span{ &nearby, 1 }, geared);
    CHECK(bundle_value(spawn_a, spawn_b, 0, 600) < damage_value * 0.05f);
    CHECK(bundle_value(a, a, 0, 600) == 0);
    CHECK(bundle_value(a, a, 0.1f, 600) > 0); // health/PvP value survives a farming bottleneck
    CHECK(bundle_value(a, b, 0.1f, 0) == 0);
    // Retained calibration counterexample (not desired economics): a fully paid unlock from
    // zero predicted gem flow receives no throughput recall credit under the rejected helper.
    const Flow recorded_zero{};
    const Flow recorded_jungle{0.173309f, 0.787769f, 1};
    CHECK(shop_benefit(recorded_zero, recorded_jungle, 0, 60, 0) == 0);
    // The production target-reward caller consumes the same gold price: capital that no item
    // can use must not keep a gold-only loop attractive forever.
    CHECK(group_reward(0, 60, 0.01f, 1, 0) < group_reward(10, 0, 0.01f, 1, 0));
    CHECK(group_reward(0, 60, 0.4f, 1, 0) > group_reward(10, 0, 0.4f, 1, 0));
    CHECK(group_reward(10, 0, 0.4f, 1, 2) > group_reward(10, 0, 0.4f, 1, 0));
    const float benefit = shop_benefit(a, b, 0.02f, 20, 0);
    CHECK(shop_worth(benefit, 0));
    CHECK(!shop_worth(benefit, 100));
    CHECK(!shop_worth(1.9f, -20)); // transport alone is handled separately by the real caller
    CHECK(shop_worth(2, 0));
    Bank bank{ 10, 1000, 5, 6, 1, 1000 };
    CHECK(!bank_now(bank)); // six tiles to an altar is not a zero-cost route detour
    bank.extra_route = 0;
    CHECK(!bank_now(bank)); // even zero walking still pays its channel without an investment
    bank.investment_credit = 1;
    CHECK(bank_now(bank));
    bank.extra_route = -1;
    CHECK(bank_needs_route(bank));
    bank.investment_credit = 0;
    CHECK(!bank_needs_route(bank));
    bank.contact = true;
    CHECK(bank_now(bank) && !bank_needs_route(bank));
    bank.contact = false;
    RouteMemo route;
    const RouteKey origin{3, 4, 3, 4, 7};
    CHECK(route.get(origin) == -1);
    route.put(origin, -1);
    CHECK(route.get(origin) == -1);
    route.put(origin, 9);
    CHECK(route.get(origin) == 9);
    CHECK(route.get({3, 5, 3, 4, 7}) == -1 && route.get({3, 4, 3, 4, 8}) == -1);
    CHECK(route.get({3, 4, 3, 5, 7}) == -1);
    CHECK(route_budget(55000, 52000, 43000, 4000));
    CHECK(!route_budget(55000, 51000, 43000, 4000));
    CHECK(!route_budget(45000, 43000, 43000, 4000));
    CHECK(!route_budget(45000, 46000, 43000, 4000));
    bank.known_team_carried = bank.remaining_score;
    CHECK(bank_now(bank) && !bank_needs_route(bank));
    bank.known_team_carried = 0;
    const CreditKey credit{ 100, 7, 5, true };
    CHECK(credit_current(credit, 140, 7, 5));
    CHECK(!credit_current(credit, 141, 7, 5));
    CHECK(!credit_current(credit, 101, 8, 5));
    CHECK(!credit_current(credit, 101, 7, 6));
    bank.extra_route = 12;
    CHECK(!bank_now(bank));
    bank.contact = true;
    CHECK(bank_now(bank));
    bank.contact = false;
    bank.gems = 200;
    CHECK(bank_now(bank)); // remaining-score/team-share bound, not a copied fixed bag threshold
    bank.gems = 1;
    bank.rounds_left = 22;
    CHECK(bank_now(bank));
    std::printf("capital workload/caller cases: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
