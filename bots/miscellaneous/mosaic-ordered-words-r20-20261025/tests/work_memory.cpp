#include "work_memory.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <limits>

using namespace mosaic::work_memory;
int main() {
    unsigned checks = 0;
    const auto check = [&](bool ok) { if (!ok) std::printf("failed work-memory check %u\n", checks + 1); assert(ok); ++checks; };
    Member member;
    check(member.knowledge() == Knowledge::initial_unknown && member.nomination(0) == Nomination::Work);
    check(member.observe_alive(10) && member.knowledge() == Knowledge::alive);
    check(member.observe_dead(12, 20) && member.dead());
    check(member.nomination(19) == Nomination::DeferredDead && !member.due(19));
    check(member.nomination(20) == Nomination::ProbeDue && member.due(20));
    check(member.nomination(2000) == Nomination::ProbeDue && member.dead());
    // Vision loss is absence of a new event. Arbitrarily many clock inputs
    // cannot turn a corpse into a work target, either before or after deadline.
    for (int epoch = 12; epoch <= 4000; ++epoch) {
        const auto expected = epoch < 20 ? Nomination::DeferredDead : Nomination::ProbeDue;
        check(member.nomination(epoch) == expected && member.dead());
    }
    check(!member.observe_alive(11) && member.dead());
    check(!member.observe_alive(12) && member.dead());
    check(!member.new_generation(11) && member.dead());
    check(!member.new_generation(12) && member.dead());
    check(member.observe_alive(21) && !member.dead() && member.nomination(21) == Nomination::Work);
    check(!member.observe_dead(20, 100) && member.knowledge() == Knowledge::alive);
    check(member.observe_dead(22, 30) && member.dead());
    check(member.new_generation(31) && member.knowledge() == Knowledge::initial_unknown && member.nomination(31) == Nomination::Work);
    check(!member.observe_dead(30, 100) && !member.dead());
    check(member.observe_dead(31, 40) && member.dead());
    check(!member.new_generation(31) && member.dead());
    check(!member.observe_alive(31) && member.dead());
    // Duplicate same-epoch observations cannot keep pushing a deferral later.
    check(member.observe_dead(31, 1000) && member.probe_after() == 40);
    check(member.observe_dead(31, 35) && member.probe_after() == 35);
    check(member.nomination(35) == Nomination::ProbeDue && member.dead());
    check(member.observe_dead(36, -1) && member.nomination(36) == Nomination::ProbeDue);
    check(member.observe_dead(37, 36) && member.nomination(37) == Nomination::ProbeDue);
    check(member.observe_alive(38) && member.nomination(4000) == Nomination::Work);
    check(!member.observe_dead(-1, 100) && !member.new_generation(-1) && !member.observe_alive(-1));
    check(member.observed_epoch() == 38 && member.knowledge() == Knowledge::alive);

    // Every valid future deadline has the same three-way interpretation. In
    // particular equality and overflow-adjacent epochs never nominate work.
    constexpr std::array epochs{0, 1, 7, 1000, std::numeric_limits<int>::max() - 1};
    for (int epoch : epochs) {
        Member state;
        check(state.observe_dead(epoch, epoch + 1));
        check(state.nomination(epoch) == Nomination::DeferredDead);
        check(state.nomination(epoch + 1) == Nomination::ProbeDue && state.dead());
        check(state.nomination(std::numeric_limits<int>::max()) == Nomination::ProbeDue && state.dead());
        check(!state.observe_alive(epoch) && !state.new_generation(epoch));
        check(state.observe_alive(epoch + 1) && state.nomination(epoch + 1) == Nomination::Work);
    }
    // Distinct members cannot contaminate each other's negative knowledge.
    std::array<Member, 3> group;
    check(group[0].observe_dead(5, 8) && group[1].observe_alive(5));
    check(group[0].nomination(100) == Nomination::ProbeDue && group[1].nomination(100) == Nomination::Work && group[2].nomination(100) == Nomination::Work);
    check(group[0].new_generation(101) && group[0].knowledge() == Knowledge::initial_unknown);
    // Actual generation is part of the freshness fence. A later generation
    // cannot be overwritten by an Alive older than that generation merely
    // because the Alive is newer than the previous death.
    Member chronology;
    check(chronology.observe_dead(5, 8) && chronology.new_generation(10));
    check(!chronology.observe_alive(9) && chronology.knowledge() == Knowledge::initial_unknown);
    check(chronology.observe_alive(12) && !chronology.new_generation(11) && chronology.knowledge() == Knowledge::alive);
    // With epoch-only input stamps, Dead wins every ordering of same-epoch
    // Alive, Dead and Generation events, including a repeated generation.
    constexpr std::array<std::array<int, 3>, 6> orders{{
        {{0,1,2}}, {{0,2,1}}, {{1,0,2}}, {{1,2,0}}, {{2,0,1}}, {{2,1,0}}
    }};
    for (const auto& order : orders) {
        Member state;
        for (const int event : order) {
            if (event == 0) state.observe_alive(50);
            else if (event == 1) state.observe_dead(50, 55);
            else state.new_generation(50);
        }
        check(state.dead() && state.observed_epoch() == 50);
        check(!state.new_generation(50) && !state.observe_alive(50));
        check(state.nomination(54) == Nomination::DeferredDead && state.nomination(55) == Nomination::ProbeDue);
        check(state.nomination(2000) == Nomination::ProbeDue && state.dead());
    }
    std::printf("work memory: %u checks\n", checks);
}
