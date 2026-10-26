#include "../src/support_plan.h"
#include <cassert>
#include <limits>

namespace sp = mb::support_plan;

int main()
{
    std::array<float, sp::kFeatures> zero{}, weights{};
    assert(!sp::eligible_ally(0, 1));
    assert(!sp::eligible_ally(1, 1));
    assert(!sp::eligible_ally(2, -1));
    assert(sp::eligible_ally(1, 0));
    for (int self = 0; self < 8; ++self)
        for (int ally = 0; ally < 8; ++ally)
            assert(!(sp::eligible_ally(self, ally) && sp::eligible_ally(ally, self)));
    weights[sp::bias] = 1.0f;
    int built = 0;
    auto positive = [&](int i) {
        ++built;
        sp::Observation o;
        o.distance = 5;
        o.own_hp = o.ally_hp = 1.0f;
        o.allies = 2;
        o.allied_power = 100.0f;
        return sp::Candidate{ i, true, sp::features(o) };
    };
    // Zero weights/disabled mode skip the expensive candidate builder entirely.
    assert(sp::choose(true, zero, 2, positive).candidate == -1 && built == 0);
    assert(sp::choose(false, weights, 2, positive).candidate == -1 && built == 0);
    assert(sp::choose(true, weights, 0, positive).candidate == -1 && built == 0);
    const auto selected = sp::choose(true, weights, 2, positive);
    assert(selected.ally == 0 && selected.score == 1.0f && built == 2);
    assert(sp::may_move(true, false, 5));
    assert(sp::step_score(5, 4, 0.0f, 0.0f, 1000.0f) > 0);
    // A dead/invalid/hidden candidate is represented as invalid by the adapter.
    auto invalid = [&](int) { return sp::Candidate{ 0, false, positive(0).x }; };
    assert(sp::choose(true, weights, 1, invalid).candidate == -1);
    auto missing_identity = [&](int) { return sp::Candidate{ -1, true, positive(0).x }; };
    assert(sp::choose(true, weights, 1, missing_identity).candidate == -1);
    auto reverse = [&](int i) { return sp::Candidate{ 1 - i, true, positive(i).x }; };
    assert(sp::choose(true, weights, 2, reverse).ally == 0);  // stable roster tie, not view order
    weights[sp::bias] = -1.0f;
    assert(sp::choose(true, weights, 1, positive).candidate == -1);  // FARM wins 0
    weights[sp::bias] = std::numeric_limits<float>::quiet_NaN();
    assert(sp::choose(true, weights, 1, positive).candidate == -1);
    // Screening waits near the ally; a costly incoming step can also lose to staying.
    assert(!sp::may_move(true, false, 2));
    assert(sp::step_score(2, 1, 0.0f, 0.0f, 1000.0f) <= 0);
    assert(sp::step_score(5, 4, 0.0f, 400.0f, 1000.0f) <= 0);
    assert(sp::step_score(5, 5, 300.0f, 0.0f, 1000.0f) <= 0);  // never lateral/chase
    // A used movement allowance, including a ranged basic or an exclusive cast reported
    // by the engine as !can_move, cannot produce a support step.
    assert(!sp::may_move(true, true, 5));
    assert(!sp::may_move(false, false, 5));
    assert(!sp::may_move(false, true, 5));

    // Completing an ally's camp requires that exact published group, personal
    // live/awake evidence, and the ally already within its attack range.
    assert(sp::camp_task_eligible(true, true, true, true, 1, 1, 3));
    assert(sp::camp_task_eligible(true, true, true, true, 3, 3, 6));
    assert(!sp::camp_task_eligible(false, true, true, true, 1, 1, 3));
    assert(!sp::camp_task_eligible(true, false, true, true, 1, 1, 3));
    assert(!sp::camp_task_eligible(true, true, false, true, 1, 1, 3));
    assert(!sp::camp_task_eligible(true, true, true, false, 1, 1, 3));
    assert(!sp::camp_task_eligible(true, true, true, true, 2, 1, 3));
    assert(!sp::camp_task_eligible(true, true, true, true, 1, 1, 7));
    assert(!sp::camp_task_eligible(true, true, true, true, 0, 0, 3));
    assert(!sp::camp_task_eligible(true, true, true, true, -1, 1, 3));
    assert(sp::camp_move_local(false, false, 6));
    assert(!sp::camp_move_local(false, false, 7));
    assert(!sp::camp_move_local(false, true, 6));
    assert(sp::camp_move_local(true, true, 12));
    assert(sp::combat_task_eligible(true, true, 4, 12));
    assert(!sp::combat_task_eligible(false, true, 4, 3));
    assert(!sp::combat_task_eligible(true, false, 4, 3));
    assert(!sp::combat_task_eligible(true, true, 4, 13));
    assert(!sp::combat_task_eligible(true, true, -1, 3));

    // Standing beside the ally no longer says we can affect its target. The
    // witnessed melee gap of three tiles needs two steps, even at buddy radius.
    sp::Observation task;
    task.distance = 1;
    assert(sp::features(task)[sp::arrival] == 0.0f);  // original mode unchanged
    task.effect_arrival = sp::effect_arrival(3, 1);
    assert(task.effect_arrival == 2);
    assert(sp::features(task)[sp::arrival] == 0.25f);
    task.effect_arrival = sp::effect_arrival(3, 3);
    assert(sp::features(task)[sp::arrival] == 0.0f);
    assert(sp::effect_arrival(1, 3) == 0);
    task.effect_arrival = sp::effect_arrival(12, 1);
    assert(sp::features(task)[sp::arrival] == 11.0f / 8.0f);  // no new clamp
    weights.fill(0.0f);
    weights[sp::bias] = 10.0f;
    const auto sleeping = [&](int) {
        return sp::Candidate{ 0, sp::camp_task_eligible(true, true, true, false, 1, 1, 3), sp::features(task) };
    };
    assert(sp::choose(true, weights, 1, sleeping).candidate == -1);

    // SUPPORT can approach the enemy threatening its ally while staying local;
    // no range-independent chase, lateral step or excessive incoming cost wins.
    assert(sp::may_task_move(true, false));
    assert(!sp::may_task_move(true, true));
    assert(!sp::may_task_move(false, false));
    assert(sp::combat_task_step_score(3, 2, 6, 0.0f, 0.0f, 1000.0f) > 0);
    assert(sp::combat_task_step_score(3, 2, 7, 0.0f, 0.0f, 1000.0f) <= 0);
    assert(sp::combat_task_step_score(3, 3, 2, 100.0f, 0.0f, 1000.0f) <= 0);
    assert(sp::combat_task_step_score(0, 0, 2, 0.0f, 0.0f, 1000.0f) <= 0);
    assert(sp::combat_task_step_score(3, 2, 2, 0.0f, 400.0f, 1000.0f) <= 0);
    assert(sp::combat_task_step_score(3, 2, 2, 0.0f, std::numeric_limits<float>::quiet_NaN(), 1000.0f) <= 0);

    // A successful-looking tank wait still falls through to FARM. Prior actions
    // from another phase are not counted again, and no new move is authorized.
    const sp::ActionFlags idle{};
    assert(!sp::effect_delta(idle, idle).any());
    const sp::ActionFlags prior_move{ true, false, false };
    assert(!sp::effect_delta(prior_move, prior_move).any());
    const auto attack = sp::effect_delta(prior_move, { true, true, false });
    assert(attack.attacked && !attack.moved && attack.any());
    const auto cast = sp::effect_delta(idle, { false, false, true });
    assert(cast.cast && !cast.attacked && cast.any());
    const auto move = sp::effect_delta(idle, prior_move);
    assert(move.moved && move.any());
    assert(!sp::may_task_move(true, prior_move.moved));

    // 39K FARM floor plus the full 15K proposal reservation admits at 54K.
    // Every chunk must fit the remaining finite allowance and leave that floor.
    assert(sp::proposal_budget_available(54000, 54000, 15000, 39000, 5000));
    assert(sp::proposal_budget_available(54000, 44000, 15000, 39000, 5000));
    assert(!sp::proposal_budget_available(54000, 44000, 15000, 39000, 5001));
    assert(sp::proposal_budget_available(54000, 39000, 15000, 39000));
    assert(!sp::proposal_budget_available(54000, 39000, 15000, 39000, 1));
    assert(!sp::proposal_budget_available(53999, 53999, 15000, 39000));
    assert(!sp::proposal_budget_available(54000, 38999, 15000, 39000));
    assert(!sp::proposal_budget_available(54000, 54001, 15000, 39000));
    assert(!sp::proposal_budget_available(54000, 50000, 15000, 39000,
                                          std::numeric_limits<std::uint64_t>::max()));
    // Production partitions the same 15K admission into 14K construction plus
    // a 1K return tail, leaving FARM eligible after an exhausted proposal/control.
    assert(sp::proposal_budget_available(54000, 40000, 14000, 40000));
    assert(!sp::proposal_budget_available(54000, 40000, 14000, 40000, 1));
    assert(40000 - 1000 >= 39000);
}
