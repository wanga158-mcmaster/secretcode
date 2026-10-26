#include "../common/src/competitive.h"
#include "../common/src/doctrines_a.h"
#include <cstdio>
#include <cstdlib>

namespace {
using namespace p7::live;
int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #condition); std::abort(); } } while (false)

Frame mobility_frame() {
    Frame f; f.design = Design::islander; f.round = 200; f.self = 1;
    f.source_count = 2; f.altar_count = f.port_count = 2; f.team_size = 1;
    f.tile = 10; f.health = f.max_health = 100; f.damage = 20;
    f.kit_level = 10; f.captures = f.banks = 1; f.dash_ready = true;
    for (int i = 0; i < f.source_count; ++i) {
        auto& x = f.sources[i]; x.id = 100 + i; x.tile = 20 + i;
        x.port = i; x.distance = 3; x.exit_distance = x.recovery_distance = 5;
        x.cycle_turns = 20; x.spawn_distance = 6; x.gems = 10 + i * 10;
        x.score = {10 + i * 20, 10 + i * 20, 10 + i * 20};
        x.seen = x.reachable = x.feasible = true;
        x.home_connected = i == 0; x.mobility = i == 1;
    }
    return f;
}
State funded() { State s; s.captures = s.banks = 1; return s; }

// This isolates the real controller -> competitive arbitration path. Supplying
// a mobility certificate here is diagnostic: today's native shortlist cannot
// supply this frame, so this positive is not a reachable admission fixture.
void certified_controller_path_and_continuation() {
    auto f = mobility_frame(); State active = funded(); CompetitiveState execution;
    const Task proposal = choose_04(f, active);
    CHECK(proposal.source == 1 && proposal.mobility && proposal.mechanism);
    const Task executed = competitive_plan(f, proposal, execution);
    CHECK(executed == proposal && execution.source == 1);
    auto control_f = f; control_f.control = true; State control = funded();
    const Task contrast = choose_04(control_f, control);
    CHECK(contrast.source == 0 && !contrast.mobility);
    f.bag = 10;
    const Task loaded = choose_04(f, active);
    CHECK(loaded.goal == Goal::bank && loaded.source == 1 && loaded.mobility);
    const Task conversion = competitive_plan(f, loaded, execution);
    CHECK(conversion.goal == Goal::bank && conversion.mobility && conversion.source == 1);
    f.bag = 0; ++active.captures; ++active.banks;
    active.payout_seen = active.bank_seen = true;
    const Task redeployed = choose_04(f, active);
    CHECK(redeployed.source == 0 && !active.relocated && !redeployed.mobility);
}

void refusal_preserves_connected_work() {
    auto f = mobility_frame(); f.dash_ready = false; State no_kit = funded();
    CHECK(choose_04(f, no_kit).source == 0 && !no_kit.relocated);
    f.dash_ready = true; f.sources[1].exit_distance = -1; State no_exit = funded();
    CHECK(choose_04(f, no_exit).source == 0 && !no_exit.relocated);
    f.sources[1].exit_distance = 5; State no_income;
    CHECK(choose_04(f, no_income).source == 0 && !no_income.relocated);
}

// Runtime initialize() supplies home-field distances here, before geometry().
// Runtime geometry() attempts mobility only for !home_connected. Thus the
// actual production shortlist removes every source eligible for that branch.
void native_shortlist_removes_mobility_opportunity() {
    const std::vector<CompetitiveSource> public_sources{
        {100, 3, 10, 20}, {101, -1, 30, 21}, {102, 5, 4, 22}
    };
    const auto selected = competitive_shortlist(public_sources, Design::islander, 0, 20, 3);
    CHECK(selected.size() == 2);
    for (int index : selected) {
        CHECK(public_sources[index].distance >= 0);
        CHECK(public_sources[index].id != 101);
    }
    auto retained = mobility_frame(); retained.sources[1].mobility = false;
    retained.sources[1].home_connected = true;
    State active = funded(); const Task proposal = choose_04(retained, active);
    CHECK(!proposal.mobility && !active.relocated);
    CHECK(proposal.source >= 0 && retained.sources[proposal.source].home_connected);
}
}

int main() {
    certified_controller_path_and_continuation();
    refusal_preserves_connected_work();
    native_shortlist_removes_mobility_opportunity();
    std::printf("%d population reachability/arbitration checks (no native activation claimed)\n", checks);
}
