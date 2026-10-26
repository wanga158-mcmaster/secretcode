#include "service_route.h"
#include <cassert>
#include <iostream>

using namespace wb::route;

static Grid grid(int w, int h) {
    const int n = w * h;
    return {w, h, std::vector<std::uint8_t>(n), std::vector<std::uint8_t>(n, 1),
        std::vector<std::uint8_t>(n), std::vector<std::uint8_t>(n)};
}
static void finish(Planner& planner) {
    unsigned calls = 0;
    while (planner.state() == State::Searching || planner.state() == State::Reconstructing) {
        planner.advance(1);
        assert(++calls < 100000);
    }
}
int main() {
    auto g = grid(9, 5);
    g.spawn[0] = 1; g.bank[8] = 1;
    const Member members[]{{101, 20, true}, {72, 21, true}, {999, 22, true}};
    Planner planner;
    assert(planner.begin(g, 0, members));
    planner.advance(1);
    assert(planner.state() == State::Searching);
    finish(planner);
    assert(planner.state() == State::Complete);
    const auto& plan = planner.result();
    assert(plan.size() == 6);
    bool killed[3]{};
    int pos = 0;
    auto occupied = g.blocked;
    for (const auto& m : members) occupied[m.tile] = 1;
    for (const auto& segment : plan) {
        for (const int tile : segment.path) {
            assert(g.quiet[tile] && !occupied[tile]);
            assert(std::abs(tile % 9 - pos % 9) <= 1 && std::abs(tile / 9 - pos / 9) <= 1);
            pos = tile;
        }
        assert(pos == segment.goal);
        if (segment.role == Role::WorkMember) {
            assert(!killed[segment.member_index]);
            killed[segment.member_index] = true;
            occupied[members[segment.member_index].tile] = 0;
        } else if (segment.role == Role::Capture) {
            for (const auto& m : members) occupied[m.tile] = 1;
        }
    }
    assert(killed[0] && killed[1] && killed[2] && g.spawn[pos]);
    // A quiet full suffix is part of admission: blocked conversion fails the recipe.
    g.bank.assign(45, 0);
    assert(planner.begin(g, 0, members)); finish(planner);
    assert(planner.state() == State::Unsupported);
    // A potential harmful footprint remains forbidden even when currently asleep/dead.
    g.bank[8] = 1; g.quiet[21] = 0;
    assert(!planner.begin(g, 0, members));
    // Capture-only recipes do not demand another strike on already dead members.
    g.quiet[21] = 1;
    const Member dead[]{{101,20,false},{72,21,false},{999,22,false}};
    assert(planner.begin(g, 0, dead)); finish(planner);
    assert(planner.state() == State::Complete && planner.result().size() == 3);
    assert(planner.result()[0].role == Role::Capture);
    // IDs are identities, not array offsets; duplicates reject explicitly.
    const Member duplicate[]{{101,20,true},{101,21,true}};
    assert(!planner.begin(g, 0, duplicate));
    assert(planner.begin_suffix(g, 0));finish(planner);
    assert(planner.state()==State::Complete && planner.result().size()==2);
    assert(planner.result()[0].role==Role::Bank && planner.result()[1].role==Role::Spawn);
    assert(!planner.begin_suffix(g,0,Role::WorkMember));
    std::cout << "service route: complete itinerary, sliced search, actual opening dependency, suffix rejection, capture-only, identity checks PASS\n";
}
