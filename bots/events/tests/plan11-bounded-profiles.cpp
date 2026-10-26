#include <cassert>
#include "../tempo/src/policy.h"
#include "../harvest/src/policy.h"
#include "../strata/src/policy.h"

namespace {
using p11::Candidate;
using p11::State;
using Menu = std::array<Candidate, 20>;

template<class Policy> struct ObservedScreen {
    static inline std::array<int, 20> visits{};
    static double screen(const State& state, const Candidate& candidate) {
        assert(candidate.id >= 0 && candidate.id < int(visits.size()));
        ++visits[candidate.id];
        return Policy::screen(state, candidate);
    }
};

Menu public_menu() {
    Menu menu{};
    for (int id = 0; id < int(menu.size()); ++id) {
        auto& candidate = menu[id];
        candidate.id = id;
        candidate.travel = 2; candidate.clear = 4; candidate.capture = 6;
        candidate.altar = 3; candidate.renewal = 40;
        candidate.gems = 10; candidate.gold = 0;
        candidate.hp = 400; candidate.loss = 20; candidate.incoming = 4;
        candidate.known = true;
    }
    menu[17].gems = 1; menu[17].committed = true;
    menu[18].gems = 1;
    menu[19].gems = 100;
    return menu;
}

template<class Policy>
void check_frontier(const State& state, const Menu& public_candidates,
                    const Menu& completed_candidates, int ranked, int winner) {
    static_assert(Policy::bounded_scan && Policy::window_size == 3 && Policy::trace_scan);
    ObservedScreen<Policy>::visits.fill(0);
    const auto order = p11::shortlist<ObservedScreen<Policy>>(
        state, public_candidates, Policy::window_size, 17, 18);
    for (int visits : ObservedScreen<Policy>::visits) assert(visits == 1);
    assert((order == std::vector<int>{ranked, 17, 18}));

    // Complete every shortlisted alternative, then invoke the actual detailed
    // selector. The fixture never passes it a preselected winner.
    std::vector<Candidate> completed;
    for (int id : order) {
        const auto candidate = std::find_if(completed_candidates.begin(), completed_candidates.end(),
            [id](const Candidate& value) { return value.id == id; });
        assert(candidate != completed_candidates.end());
        completed.push_back(*candidate);
    }
    assert(p11::choose<Policy>(state, completed) == winner);
}

template<class Policy> void check_profile() {
    State state; state.mature = true; state.limit = 500;
    auto menu = public_menu();
    for (const auto& candidate : menu)
        assert(Policy::screen(state, candidate) == Policy::score(state, candidate));
    assert(p11::choose<Policy>(state, menu) == 19);
    assert(p11::choose<Policy>(state, std::span(menu).first(3)) != 19);
    check_frontier<Policy>(state, menu, menu, 19, 19);

    // IDs 12 and 19 have identical spread and costs. Stable public order
    // determines the one ranked slot; both protected slots remain present.
    menu[12].gems = menu[19].gems;
    assert(Policy::screen(state, menu[12]) == Policy::screen(state, menu[19]));
    check_frontier<Policy>(state, menu, menu, 12, 12);
    std::reverse(menu.begin(), menu.end());
    check_frontier<Policy>(state, menu, menu, 19, 19);

    // Public value cannot bypass detailed safety. Populate the whole detailed
    // menu before selecting: the rejected late favorite leaves the incumbent.
    menu = public_menu();
    auto completed = menu;
    completed[19].loss = 5000; completed[19].incoming = 1000;
    check_frontier<Policy>(state, menu, completed, 19, 17);
}
}

int main() {
    check_profile<p11_tempo::Policy>();
    check_profile<p11_harvest::Policy>();
    check_profile<p11_strata::Policy>();
}
