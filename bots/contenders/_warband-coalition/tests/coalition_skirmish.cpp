// Actual conditional forced-focus predictor: changed removal order can change survival.
#include <cassert>
#include <cstdio>
#include "coalition_skirmish.h"

using mb::coalition::simulate_forced_focus;
using mb::coalition::accept_shared_outcome;
using mb::model::Fighter;
using mb::model::SkirmishOutcome;

static void equal(const SkirmishOutcome& a, const SkirmishOutcome& b) {
    assert(a.our_losses == b.our_losses && a.their_losses == b.their_losses);
    assert(a.rounds == b.rounds && a.first_target == b.first_target);
    assert(a.we_survive == b.we_survive && a.they_survive == b.they_survive);
}

int main() {
    {
        // Leaving the small dangerous enemy alive to focus a large harmless one is fatal.
        Fighter f[] = {{100, 10, true}, {10, 50, false}, {100, 0, false}};
        const auto base = mb::model::simulate_skirmish(f, 3, 15);
        const auto forced = simulate_forced_focus(f, 3, 15, 2);
        assert(base.first_target == 1 && base.our_losses == 0 && base.their_losses == 2 && base.we_survive);
        assert(forced.first_target == 2 && forced.our_losses == 1 && forced.their_losses == 0);
        assert(!forced.we_survive && forced.they_survive && forced.rounds == 2);
        assert(!accept_shared_outcome(base, forced));
        const auto selected = accept_shared_outcome(base, forced) ? forced : base;
        equal(selected, base);  // reject the proposal, not the original engagement
        assert(selected.first_target == 1);
        assert(f[1].hp == 10 && f[2].hp == 100);  // prediction does not mutate caller state
    }
    {
        // The first forced target dies; ordinary focus finishes the survivor instead of stalling.
        Fighter f[] = {{100, 20, true}, {40, 0, false}, {10, 0, false}};
        const auto base = mb::model::simulate_skirmish(f, 3, 15);
        const auto forced = simulate_forced_focus(f, 3, 15, 1);
        assert(forced.first_target == 1 && forced.their_losses == 2 && forced.rounds == 3);
        assert(forced.we_survive && !forced.they_survive);
        assert(base.first_target == 2 && forced.first_target != base.first_target);
        assert(accept_shared_outcome(base, forced));  // equal outcome can coordinate a new primary
    }
    {
        // Spending the first basic on a harmless small enemy leaves dangerous offense alive
        // for a third round; focusing the dangerous enemy first preserves our last HP.
        Fighter f[] = {{101, 10, true}, {10, 0, false}, {20, 50, false}};
        const auto base = mb::model::simulate_skirmish(f, 3, 15);
        const auto forced = simulate_forced_focus(f, 3, 15, 2);
        assert(base.first_target == 1 && base.our_losses == 1 && !base.we_survive);
        assert(forced.first_target == 2 && forced.our_losses == 0 && forced.we_survive);
        assert(forced.their_losses == base.their_losses && !forced.they_survive);
        assert(accept_shared_outcome(base, forced));
    }
    {
        // Survival is an independent comparison dimension, even when weighted loss totals tie.
        const SkirmishOutcome base{0, 0, 1, 1, true, false};
        auto changed = base;
        assert(accept_shared_outcome(base, changed));
        changed.we_survive = false;
        assert(!accept_shared_outcome(base, changed));
        changed = base;
        changed.they_survive = true;
        assert(!accept_shared_outcome(base, changed));
        changed = base;
        changed.our_losses = 1;
        assert(!accept_shared_outcome(base, changed));
        changed = base;
        changed.their_losses = -1;
        assert(!accept_shared_outcome(base, changed));
    }
    {
        Fighter f[] = {{100, 20, true}, {40, 0, false}, {10, 0, false}};
        const auto base = mb::model::simulate_skirmish(f, 3, 15, true);
        for (int request : {-1, 0, 3, 99}) equal(base, simulate_forced_focus(f, 3, 15, request, true));
        f[2].hp = 0;
        equal(mb::model::simulate_skirmish(f, 3, 15), simulate_forced_focus(f, 3, 15, 2));
    }
    {
        // Caller has a current shot, while this enemy's offense will only arrive at round five.
        Fighter f[] = {{20, 10, true}, {10, 100, false}};
        f[1].arrive = 5;
        f[1].cast[0] = 100;
        const auto forced = simulate_forced_focus(f, 2, 15, 1);
        assert(forced.first_target == 1 && forced.rounds == 1);
        assert(forced.their_losses == 1 && forced.our_losses == 0 && forced.we_survive);
        const auto base = mb::model::simulate_skirmish(f, 2, 15);
        assert(base.rounds == 5 && base.our_losses == 1);  // original coupled-arrival limitation
    }
    {
        // Killing a forced linked enemy removes its living partner and future scheduled offense.
        Fighter f[] = {{101, 20, true}, {20, 0, false}, {100, 100, false}};
        f[1].linked = 2;
        f[2].linked = 1;
        const auto forced = simulate_forced_focus(f, 3, 15, 1);
        assert(forced.their_losses == 2 && forced.our_losses == 0 && forced.rounds == 1);
    }
    {
        // Equal ready times still permit only one cast in a round; the weaker slot waits.
        Fighter f[] = {{100, 0, true}, {30, 0, false}};
        f[0].cast[0] = 10;
        f[0].cast[1] = 25;
        const auto forced = simulate_forced_focus(f, 2, 15, 1);
        assert(forced.their_losses == 1 && forced.rounds == 2);
    }
    {
        Fighter f[] = {{1000000, 1, true}, {1000000, 1, false}};
        const auto forced = simulate_forced_focus(f, 2, 500, 1);
        assert(forced.rounds == 32 && forced.our_losses == 0 && forced.their_losses == 0);
        const auto zero = simulate_forced_focus(f, 2, 0, 1);
        assert(zero.rounds == 0 && zero.first_target == 1);
    }
    {
        Fighter f[17]{};
        f[0] = {1000000, 1, true};
        f[1] = {1000000, 1, false};
        f[16] = {1, 1000000, false};
        equal(mb::model::simulate_skirmish(f, 16, 2), simulate_forced_focus(f, 17, 2, 16));
    }
    std::puts("conditional forced-focus predictor: 10 cases passed");
}
