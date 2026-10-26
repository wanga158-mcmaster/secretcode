#pragma once
// Fuel scheduling for the retained top-ten dispatcher. Rates, bounds and hysteresis stay with
// the caller; unfinished optional work is retried from fresh observations on a later turn.

namespace tt {

inline constexpr long kDispatchEntryFuel = 35000;
inline constexpr long kDispatchDiscoveryFuel = 30000;
inline constexpr long kDispatchOptionFuel = 25000;

struct ShortlistResult { int count = 0; bool incomplete = false; };

template<class Bound, class Fuel>
ShortlistResult bounded_shortlist(int groups, int* top, float* bounds, int capacity, Bound bound, Fuel fuel)
{
    int count = 0;
    if (capacity <= 0) return {};
    for (int group = 0; group < groups; ++group) {
        if (fuel() < kDispatchDiscoveryFuel) return {count, true};
        const float upper = bound(group); // negative means the original caller excludes this group
        if (upper < 0) continue;
        int at = count < capacity ? count++ : capacity;
        if (at == capacity) {
            if (upper <= bounds[capacity - 1]) continue;
            at = capacity - 1;
        }
        while (at > 0 && bounds[at - 1] < upper) {
            top[at] = top[at - 1];
            bounds[at] = bounds[at - 1];
            --at;
        }
        top[at] = group;
        bounds[at] = upper;
    }
    return {count, false};
}

struct BudgetedChoice {
    int group = -1;
    float rate = 0;
    bool party = false;
    bool kept = false;
    float keep_threshold = 0;
    bool incomplete = false;
    bool unscored = false;
};

template<class Score, class Allowed, class Keep, class Fuel>
BudgetedChoice bounded_options(const int* top, const float* bounds, int count, bool discovery_incomplete, int current,
                               bool current_party, Score score, Allowed allowed, Keep keep, Fuel fuel)
{
    BudgetedChoice best;
    best.incomplete = discovery_incomplete;
    for (int i = 0; i < count; ++i) {
        if (fuel() < kDispatchOptionFuel) { best.incomplete = true; break; }
        if (bounds[i] * 1.3f <= best.rate) break;
        bool party = false;
        const float rate = score(top[i], &party);
        if (rate > best.rate) { best.group = top[i]; best.rate = rate; best.party = party; }
    }
    if (current >= 0 && best.group != current && allowed(current)) {
        if (fuel() < kDispatchOptionFuel) {
            // An unscored incumbent has no inferred zero rate. Keep its permitted work and retry
            // the unfinished comparison next turn instead of silently dropping its hysteresis.
            return { current, -1, current_party, true, -1, true, true };
        }
        bool party = false;
        const float rate = score(current, &party);
        const float threshold = rate * keep();
        if (rate > 0 && threshold >= best.rate)
            return { current, rate, party || current_party, true, threshold, best.incomplete, false };
    }
    return best;
}

} // namespace tt
