#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <vector>
#include "../surveyor/src/policy.h"

namespace {
using p11::Candidate;
using p11::State;
struct Ranked { double value; int id; };
int checked = 0;

// Independent specification: rank every finite input row with stable_sort;
// take the raw first limit-2 rows, append finite protected IDs, then rank-fill.
// Invalid/duplicate IDs consume raw prefix slots but never output slots.
template<class Policy>
std::vector<Ranked> full_rank(const State& state, std::span<const Candidate> menu) {
    std::vector<Ranked> ranked;
    for (const auto& candidate : menu) {
        const double value = Policy::screen(state, candidate);
        if (std::isfinite(value)) ranked.push_back({value, candidate.id});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        return a.value > b.value;
    });
    return ranked;
}

bool contains(std::span<const int> ids, int id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

template<class Policy>
void check(const State& state, std::span<const Candidate> menu,
           int requested_limit, int incumbent, int exploration) {
    ++checked;
    assert(checked <= 10000);
    const int limit = std::clamp(requested_limit, 2, 32);
    const auto ranked = full_rank<Policy>(state, menu);
    std::vector<int> expected, prefix;
    auto append = [&](int id) {
        if (id >= 0 && int(expected.size()) < limit && !contains(expected, id))
            expected.push_back(id);
    };
    for (std::size_t i = 0; i < ranked.size() && i < std::size_t(limit - 2); ++i)
        append(ranked[i].id);
    prefix = expected;
    auto finite_id = [&](int id) {
        return std::any_of(ranked.begin(), ranked.end(), [&](const auto& row) {
            return row.id == id;
        });
    };
    if (finite_id(incumbent)) append(incumbent);
    if (finite_id(exploration)) append(exploration);
    for (const auto& row : ranked) append(row.id);

    const auto actual = p11::shortlist<Policy>(state, menu, requested_limit,
                                               incumbent, exploration);
    if (actual != expected) {
        std::cerr << "shortlist mismatch case=" << checked << " limit=" << requested_limit
                  << " incumbent=" << incumbent << " exploration=" << exploration << '\n';
        for (const auto& row : ranked)
            std::cerr << "rank id=" << row.id << " score=" << row.value << '\n';
        std::cerr << "expected:";
        for (int id : expected) std::cerr << ' ' << id;
        std::cerr << "\nactual:";
        for (int id : actual) std::cerr << ' ' << id;
        std::cerr << '\n';
    }
    assert(actual == expected);
    // This invariant is separate from equality: protection cannot displace the
    // valid, deduplicated IDs from the raw highest-ranked prefix.
    assert(actual.size() >= prefix.size());
    assert(std::equal(prefix.begin(), prefix.end(), actual.begin()));
    assert(actual.size() <= std::size_t(limit));
    for (std::size_t i = 0; i < actual.size(); ++i) {
        assert(actual[i] >= 0);
        assert(std::find(actual.begin(), actual.begin() + i, actual[i]) == actual.begin() + i);
    }
    for (int id : {incumbent, exploration})
        if (id >= 0 && finite_id(id)) assert(contains(actual, id));
}

struct SyntheticScreen {
    static double screen(const State&, const Candidate& candidate) {
        switch (candidate.clear) {
        case 10000: return std::numeric_limits<double>::infinity();
        case 10001: return -std::numeric_limits<double>::infinity();
        case 10002: return std::numeric_limits<double>::quiet_NaN();
        default: return candidate.loss;
        }
    }
};

std::uint32_t seed = 0x5a17c0deu;
std::uint32_t next() {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}
}

int main() {
    State state;
    std::vector<Candidate> menu;
    for (int limit : {3, 8}) {
        check<SyntheticScreen>(state, menu, limit, -1, 100);
        menu.resize(20);
        for (int i = 0; i < 20; ++i) { menu[i] = {}; menu[i].id = i; menu[i].loss = 1; }
        // Stable ties, both protected slots, overlaps with the ranked prefix,
        // overlapping protections, and absent protected IDs.
        for (const auto pair : {std::pair{17, 18}, std::pair{0, 19}, std::pair{19, 19},
                                std::pair{0, 0}, std::pair{-1, 100}})
            check<SyntheticScreen>(state, menu, limit, pair.first, pair.second);
        menu[0].id = -1;
        menu[2].id = menu[1].id;
        check<SyntheticScreen>(state, menu, limit, 17, 18);
        menu[18].id = menu[1].id;
        menu[18].loss = 3;
        menu[19].loss = 3;
        check<SyntheticScreen>(state, menu, limit, 17, 18);
        menu[0].clear = 10000;
        menu[1].clear = 10001;
        menu[2].clear = 10002;
        check<SyntheticScreen>(state, menu, limit, -1, menu[1].id);
        menu.clear();
    }

    // Short and broad menus, stable equal/negative/fractional scores, negative
    // IDs, duplicates (including later better copies), and nonfinite screens.
    for (int trial = 0; trial < 2400; ++trial) {
        menu.assign(next() % 65, Candidate{});
        for (std::size_t i = 0; i < menu.size(); ++i) {
            auto& candidate = menu[i];
            candidate.id = trial % 3 == 0 ? int(i) : int(next() % 17) - 3;
            candidate.loss = trial % 7 == 0 ? 1.f : float(int(next() % 65) - 32) / 4.f;
            if (next() % 19 == 0) candidate.clear = 10000 + int(next() % 3);
            if (next() % 23 == 0) candidate.loss = -0.f;
        }
        const int incumbent = menu.empty() || trial % 5 == 0 ? 1000 : menu[next() % menu.size()].id;
        const int exploration = trial % 4 == 0 ? incumbent :
            (menu.empty() || trial % 6 == 0 ? -1 : menu[next() % menu.size()].id);
        for (int limit : {3, 8})
            check<SyntheticScreen>(state, menu, limit, incumbent, exploration);
    }

    // Actual public-policy shape: 20 distinct camp IDs and the caller's window
    // 3, alongside the wider 8-candidate fixture. Fields remain legal public
    // estimates; no hidden state or selected-only menu enters the oracle.
    for (int trial = 0; trial < 1200; ++trial) {
        state.mature = next() % 2;
        menu.assign(20, Candidate{});
        for (int i = 0; i < 20; ++i) {
            auto& candidate = menu[i];
            candidate.id = i;
            candidate.gems = int(next() % 201);
            candidate.gold = int(next() % 301);
            candidate.travel = int(next() % 101);
            candidate.clear = next() % 29 == 0 ? 10000 : int(next() % 101);
            candidate.capture = 6;
            candidate.ready = next() % 11 == 0;
            candidate.committed = next() % 13 == 0;
            if (trial % 9 == 0) { candidate.gems = 10; candidate.gold = 0;
                candidate.travel = 3; candidate.clear = 4; candidate.ready = false;
                candidate.committed = false; }
        }
        const int incumbent = int(next() % 24) - 2;
        const int exploration = trial % 3 == 0 ? incumbent : int(next() % 24) - 2;
        for (int limit : {3, 8})
            check<p11_surveyor::Policy>(state, menu, limit, incumbent, exploration);
    }
    for (int limit : {-10, 0, 1, 2, 32, 40})
        check<p11_surveyor::Policy>(state, menu, limit, 17, 18);
    std::cout << "shortlist oracle: " << checked
              << " deterministic cases; reference equality and prefix invariant passed\n";
}
