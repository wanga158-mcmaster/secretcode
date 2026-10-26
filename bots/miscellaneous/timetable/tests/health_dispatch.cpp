#include "health_dispatch.h"
#include "model.h"
#include <cassert>
#include <vector>

int main()
{
    int top[10];
    float bounds[10];
    long fuel = 100000;
    std::vector<int> discovered, scored;
    auto enough = [&] { return fuel; };
    auto shortlist = tt::bounded_shortlist(12, top, bounds, 10, [&](int group) {
        discovered.push_back(group);
        return group == 11 ? 50.0f : 100.0f;
    }, enough);
    assert(shortlist.count == 10 && !shortlist.incomplete && discovered.size() == 12);
    for (int i = 0; i < 10; ++i) assert(top[i] == i); // stable ties and the retained top-ten cap
    const auto tied = tt::bounded_options(top, bounds, 10, false, -1, false,
        [&](int group, bool* party) {
            scored.push_back(group); *party = group == 9;
            return group == 9 ? 90.0f : 80.0f;
        }, [](int) { return false; }, [] { return 1.5f; }, enough);
    assert(tied.group == 9 && tied.party && !tied.incomplete && scored.size() == 10);

    // The original 1.3 bound prunes, and the original incumbent hysteresis/party state survives.
    const int two[] = {1, 3};
    const float upper[] = {20, 1};
    scored.clear();
    const auto kept = tt::bounded_options(two, upper, 2, false, 2, true,
        [&](int group, bool* party) {
            scored.push_back(group); *party = false; return group == 1 ? 10.0f : 9.0f;
        }, [](int group) { return group == 2; }, [] { return 1.5f; }, enough);
    assert((scored == std::vector<int>{1, 2}));
    assert(kept.group == 2 && kept.kept && kept.party && !kept.unscored && kept.keep_threshold == 13.5f);

    // Discovery is real work. Stop invoking it once fuel has fallen below admission; later
    // groups are unknown, and a replenished fresh turn must be able to discover them.
    fuel = 34000;
    discovered.clear();
    shortlist = tt::bounded_shortlist(12, top, bounds, 10, [&](int group) {
        discovered.push_back(group); fuel -= 2500; return static_cast<float>(group + 1);
    }, enough);
    assert(shortlist.count == 2 && shortlist.incomplete);
    assert((discovered == std::vector<int>{0, 1}));
    fuel = 100000;
    discovered.clear();
    shortlist = tt::bounded_shortlist(12, top, bounds, 10, [&](int group) {
        discovered.push_back(group); fuel -= 2500; return static_cast<float>(group + 1);
    }, enough);
    assert(!shortlist.incomplete && discovered.size() == 12 && top[0] == 11);

    // One costly option consumes the allowance. No further option or incumbent rescore runs;
    // the permitted incumbent is explicitly unscored, with no invented zero rate.
    const int jobs[] = {0, 1, 2};
    const float optimistic[] = {100, 100, 100};
    fuel = 30000;
    scored.clear();
    auto option = [&](int group, bool* party) {
        scored.push_back(group); fuel -= 8000; *party = false;
        if (group == 1) {
            tt::GroupSpec camp;
            camp.add(1000, 0, 1); camp.sort_members();
            const auto clear = tt::smith_fast(camp, 50);
            return 472.5f / (5 + 6 + clear.rounds); // actual retained economic model: rate15
        }
        return group == 0 ? 4.0f : 3.0f;
    };
    const auto deferred = tt::bounded_options(jobs, optimistic, 3, false, 99, true, option,
        [](int group) { return group == 99; }, [] { return 1.5f; }, enough);
    assert((scored == std::vector<int>{0}));
    assert(deferred.group == 99 && deferred.party && deferred.kept && deferred.incomplete && deferred.unscored);
    assert(deferred.rate < 0);

    // The same queued work on a fresh turn now scores the deferred productive job and finishes
    // the incumbent comparison. The old target's keep multiplier does not block a real gain.
    fuel = 100000;
    scored.clear();
    const auto resumed = tt::bounded_options(jobs, optimistic, 3, false, 99, true, option,
        [](int group) { return group == 99; }, [] { return 1.5f; }, enough);
    assert((scored == std::vector<int>{0, 1, 2, 99}));
    assert(resumed.group == 1 && resumed.rate == 15 && !resumed.kept && !resumed.incomplete);

    // Low fuel never rescues a forbidden incumbent or fabricates a score for an unexamined job.
    fuel = 22000;
    scored.clear();
    const auto refused = tt::bounded_options(jobs, optimistic, 3, true, 99, true, option,
        [](int) { return false; }, [] { return 3.0f; }, enough);
    assert(refused.group == -1 && refused.incomplete && !refused.kept && scored.empty());
}
