#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include "capital_episode.h"

using namespace mb::capital::episode;
static int failures;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line%d: %s\n", __LINE__, #x); ++failures; } } while (0)

static Schedule schedule(std::initializer_list<Phase> phases)
{
    Schedule result;
    result.known = true;
    for (const auto phase : phases) result.phases[result.count++] = phase;
    return result;
}
static Phase phase(Kind kind, int rounds, float damage = 0, float healing = 0)
{
    return {kind, rounds, damage, healing, 0, 0, true};
}
static Input fixture()
{
    Input input;
    input.known = true;
    input.purse = 160;
    input.health = 1000;
    input.horizon = 300;
    input.displaced = {4, 0, 60, schedule({phase(Kind::travel,2), phase(Kind::combat,3), phase(Kind::capture,6)}), true};
    input.now.bound = input.defer.bound = true;
    input.now.to_shop = schedule({phase(Kind::recall,5), phase(Kind::travel,15)});
    input.defer.to_shop = schedule({phase(Kind::recall,5), phase(Kind::travel,20)});
    input.now.purchases = {{{Buy{24,3,30},Buy{6,2,30}}}, 2, true, true, 71, 0, 1000};
    input.defer.purchases = input.now.purchases;
    input.now.gem = {10, 33, 150, schedule({phase(Kind::travel,20),phase(Kind::combat,20,300),phase(Kind::capture,6)}), true};
    input.defer.gem = input.now.gem;
    return input;
}

int main()
{
    auto input = fixture();
    auto result = evaluate(input);
    CHECK(result.decision == Decision::return_now);
    CHECK(result.now.capture_round == 66 && result.defer.capture_round == 82);
    CHECK(result.displaced_capture_round == 11);
    CHECK(result.now.spent == 150 && result.now.cash_after_shop == 10);
    CHECK(result.defer.cash_after_shop == 70 && result.defer.cash_end == 220);
    CHECK(result.now.health_end == 700 && result.defer.health_end == 700);
    CHECK(result.added_items == 0 && result.added_spend == 0 && result.extra_gold_required == 0);
    CHECK(result.advantage == 33 * 16); // zero-gem gold work is a real11-round displacement

    // A Health purchase does not heal the champion before the spawn turn ends. Put the
    // recovery after the actual transaction; both branches then leave at their new maximum.
    input.health = 440;
    input.now.purchases.max_health_after = input.defer.purchases.max_health_after = 1500;
    input.now.post_shop = schedule({phase(Kind::healing, 1, 0, 1060)});
    input.defer.post_shop = input.now.post_shop;
    auto healed = evaluate(input);
    CHECK(healed.now.capture_round == 67 && healed.defer.capture_round == 83);
    CHECK(healed.now.health_end == 1200 && healed.defer.health_end == 1200);
    input = fixture();

    // Extra unspent gold changes no modeled build or cycle: no lambda invents a payoff.
    input.displaced.gold = 600;
    auto more_cash = evaluate(input);
    CHECK(more_cash.advantage == result.advantage && more_cash.defer.cash_after_shop == 610);
    input.horizon = 200;
    CHECK(evaluate(input).advantage == result.advantage); // one target payout; no free second cycle

    // Only explicitly purchased extra items can improve the deferred execution.
    input = fixture();
    input.defer.purchases.lines[1].count = 4;
    input.defer.purchases.resulting_build = 72;
    input.defer.gem.remaining.phases[1].rounds = 1;
    result = evaluate(input);
    CHECK(result.decision == Decision::keep_working);
    CHECK(result.defer.capture_round == 63 && result.now.capture_round == 66);
    CHECK(result.added_items == 2 && result.added_spend == 60 && result.extra_gold_required == 50);
    CHECK(result.completion_time_heuristic && result.terminal_inventory_differs && result.terminal_gold_unvalued);

    input = fixture();
    input.purse = 150;
    input.displaced.gold = 29; // below the next30-gold shard, same concrete purchases
    CHECK(evaluate(input).added_items == 0);
    CHECK(evaluate(input).advantage == 528);
    input.defer.purchases.lines[1].count = 3;
    CHECK(evaluate(input).reason == Reason::unaffordable);

    input = fixture();
    input.purse = 90;
    input.now.purchases.count = 1; // the already-affordable3-shard investment is in BOTH plans
    input.defer.purchases.resulting_build = 72;
    result = evaluate(input);
    CHECK(result.added_items == 2 && result.added_spend == 60 && result.extra_gold_required == 60);

    input = fixture();
    input.purse = 120;
    input.displaced.gold = 30;
    input.now.purchases.count = 1;
    input.now.purchases.lines[0].count = 4;
    input.defer.purchases = input.now.purchases;
    input.defer.purchases.lines[0].count = 5;
    input.defer.purchases.resulting_build = 72; // runtime supplies the actual passive boundary
    CHECK(evaluate(input).added_items == 1);

    input = fixture();
    input.now.purchases.lines[1].count = 3;
    CHECK(evaluate(input).reason == Reason::unaffordable); // cannot credit an unaffordable full block
    input = fixture();
    input.now.purchases.complete = false;
    CHECK(evaluate(input).decision == Decision::unknown);
    input = fixture();
    input.now.to_shop.phases[0].known = false;
    CHECK(evaluate(input).reason == Reason::unknown_schedule);
    input = fixture();
    input.defer.bound = false;
    CHECK(evaluate(input).reason == Reason::unbound_plan);
    input = fixture();
    input.now.gem.known = false; // retainedr284 gem alternatives were stale/unknown or committed
    CHECK(evaluate(input).decision == Decision::unknown);
    input = fixture();
    input.displaced.known = false;
    CHECK(evaluate(input).decision == Decision::unknown);
    input = fixture();
    input.displaced.remaining.count = 0;
    CHECK(evaluate(input).reason == Reason::unknown_schedule);
    input = fixture();
    input.now.gem.remaining.phases[2].rounds = 0;
    CHECK(evaluate(input).reason == Reason::unknown_schedule);
    input = fixture();
    input.horizon = 65;
    CHECK(evaluate(input).reason == Reason::outside_horizon);
    input = fixture();
    input.displaced.remaining.phases[1].damage = 1000;
    CHECK(evaluate(input).reason == Reason::infeasible_health);
    input = fixture();
    input.now.gem.remaining.phases[1].damage = 1100;
    input.now.gem.remaining.phases[1].healing = 1000;
    input.defer.gem.remaining = input.now.gem.remaining;
    CHECK(evaluate(input).reason == Reason::infeasible_health); // a terminal heal cannot rescue death

    input = fixture();
    input.defer.purchases.resulting_build = 72;
    CHECK(evaluate(input).reason == Reason::inconsistent_build);
    input = fixture();
    input.defer.gem.remaining.phases[1].rounds = 1;
    CHECK(evaluate(input).reason == Reason::inconsistent_execution);
    input = fixture();
    input.defer.purchases.lines[0].item = 25;
    CHECK(evaluate(input).reason == Reason::purchase_not_prefix);
    input = fixture();
    input.defer.gem.id = 11;
    CHECK(evaluate(input).reason == Reason::unequal_target);
    input = fixture();
    input.defer.gem.gold += 30; // a concrete Treasure bonus changes unvalued terminalcash only
    CHECK(evaluate(input).advantage == 528);
    input = fixture();
    input.now.gem.remaining.count = 13;
    CHECK(evaluate(input).decision == Decision::unknown); // no out-of-bounds schedule comparison
    input = fixture();
    input.health = std::numeric_limits<float>::quiet_NaN();
    CHECK(evaluate(input).decision == Decision::unknown);

    input = fixture();
    input.displaced.gems = 20;
    CHECK(evaluate(input).decision == Decision::keep_working); // finite actual baseline gem payout counts
    input = fixture();
    input.now.gem.remaining = schedule({phase(Kind::respawn_wait,5), phase(Kind::travel,20),
                                       phase(Kind::combat,20,300), phase(Kind::capture,6)});
    CHECK(evaluate(input).decision == Decision::return_now); // arrival-dependent waits are not extra item value

    return failures ? 1 : 0;
}
