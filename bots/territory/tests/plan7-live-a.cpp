#include "../common/src/doctrines_a.h"
#include <cstdio>
#include <cstdlib>

namespace {
using namespace p7::live;
int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #condition); std::abort(); } } while (false)

Frame frame() {
    Frame f; f.source_count = 5; f.round = 20; f.home = 0; f.tile = 10; f.spawn = 1;
    f.max_health = f.health = 100; f.kit_level = 2; f.damage = 10;
    f.port_count = f.altar_count = 2;
    for (int i = 0; i < 8; ++i) for (int j = 0; j < 8; ++j) f.distances[i][j] = i == j ? 0 : 4 * std::abs(i - j);
    for (int i = 0; i < f.source_count; ++i) {
        Source& x = f.sources[i]; x.id = 100 + i; x.tile = 20 + i; x.port = 0;
        x.ring = i < 3 ? 0 : i - 2; x.distance = i + 1; x.exit_distance = 4;
        x.spawn_distance = x.recovery_distance = i < 3 ? i + 1 : 30 + i;
        x.cycle_turns = 20; x.gems = i < 3 ? 5 : 30;
        const int score = i < 3 ? 10 - i : 50 + (i - 3) * 30;
        x.score = {score, score, score}; x.seen = x.alive = x.reachable = x.feasible = true;
        x.home_connected = i < 3;
    }
    return f;
}
State funded() { State s; s.captures = s.banks = 1; return s; }
void project_completed(State& s) {
    ++s.captures; ++s.banks; s.payout_seen = s.bank_seen = true;
}
void put_ally(Frame& f, int ordinal, int source, bool holder = false) {
    Ally a; a.id = ordinal + 40; a.ordinal = ordinal; a.group = source;
    a.tile = f.sources[source].tile; a.health = 80; a.max_health = 100;
    a.visible = a.can_attack = true; a.at_capture = holder;
    f.allies[f.ally_count++] = a;
}
void concentric() {
    auto f = frame(); State s;
    CHECK(choose_01(f, s).source < 3); // Equipment cannot manufacture banked opening income.
    s = funded(); CHECK(choose_01(f, s).source == 3); // Next ring, even though ring two is richer.
    CHECK(s.relocated && s.admitted_round == 20);
    f.round = 21; choose_01(f, s); CHECK(s.admitted_round == 20);
    f.bag = 10; CHECK(choose_01(f, s).goal == Goal::bank);
    f.bag = 0; project_completed(s);
    CHECK(choose_01(f, s).source == 4 && s.ring == 1);
    f.ordinal = 1; s = funded(); CHECK(choose_01(f, s).source < 3);
    f.ordinal = 2; s = funded(); CHECK(choose_01(f, s).source < 3);
    put_ally(f, 0, 3); s = funded(); CHECK(choose_01(f, s).source == 3);
    f = frame(); f.sources[3].seen = f.sources[3].feasible = false;
    f.sources[4].seen = f.sources[4].feasible = false; s = funded();
    CHECK(choose_01(f, s).goal == Goal::inspect);
    const int admitted = s.admitted_round; f.round += 8;
    CHECK(choose_01(f, s).source < 3 && s.inspection_failed);
    CHECK(s.retired_source == 3 && admitted == 20);
    f.round = 128; CHECK(choose_01(f, s).source < 3); // No speculative retry after crossing an epoch.
    f = frame(); f.control = true; s = {}; CHECK(choose_01(f, s).source == 4);
}
void interior() {
    auto f = frame();
    for (int i = 0; i < 5; ++i) for (int j = 0; j < 5; ++j) f.distances[i][j] = i == j ? 0 : 50;
    f.distances[0][1] = f.distances[1][0] = 5;
    f.distances[0][2] = f.distances[2][0] = 5;
    f.distances[1][2] = f.distances[2][1] = 8;
    f.ordinal = 1; State active, control;
    const Task compact = choose_02(f, active); f.control = true;
    const Task direct = choose_02(f, control);
    CHECK(compact.source == 1 && direct.source == 3);
    f.control = false; f.ordinal = 0; f.fresh_pressure = true; f.pressure_tile = 77;
    f.enemy_count = 1; f.enemies[0].visible = true; State reserve;
    CHECK(choose_02(f, reserve).goal == Goal::guard);
    f.round += 8; CHECK(choose_02(f, reserve).goal == Goal::farm);
    f.round += 1; CHECK(choose_02(f, reserve).goal == Goal::farm);
    f.enemy_count = 0; reserve = {}; CHECK(choose_02(f, reserve).goal == Goal::farm);
}
void frontier() {
    auto f = frame(); f.ordinal = 1; State resident = funded();
    CHECK(choose_03(f, resident).source == 4);
    project_completed(resident);
    CHECK(choose_03(f, resident).source == 4 && resident.phase == 2);
    f.sources[4].pressured = true;
    CHECK(choose_03(f, resident).source < 3 && !resident.relocated);
    f = frame(); f.control = true; State control = funded();
    CHECK(choose_03(f, control).source < 3);
    f.control = false; State opener = funded();
    CHECK(choose_03(f, opener).source == 4);
    project_completed(opener);
    CHECK(choose_03(f, opener).source < 3 && !opener.relocated);
    State failed = funded(); admit(failed, f, 3); failed.relocated = true;
    f.sources[3].feasible = false; f.sources[3].joint_feasible = true;
    CHECK(choose_03(f, failed).goal == Goal::rally);
    f.round += 8; CHECK(choose_03(f, failed).source < 3 && failed.inspection_failed);
}
void islander() {
    auto f = frame(); f.sources[3].mobility = true; f.dash_ready = true; State s = funded();
    CHECK(choose_04(f, s).source == 3 && s.relocated);
    project_completed(s); CHECK(choose_04(f, s).source < 3 && !s.relocated);
    s = funded(); f.dash_ready = false; CHECK(choose_04(f, s).source < 3);
    f.dash_ready = true; f.sources[3].exit_distance = -1;
    CHECK(choose_04(f, s).source < 3); // Ingress alone is not a district certificate.
    f.sources[3].exit_distance = 4; f.ordinal = 2; s = funded();
    CHECK(choose_04(f, s).source < 3);
    f.gold = 20; f.teleport_ready = true; CHECK(choose_04(f, s).source == 3);
    f.control = true; s = funded(); CHECK(choose_04(f, s).source < 3);
}
void forks() {
    auto f = frame(); f.sources[3].redundant = true; f.sources[3].alternate_port = 1;
    State s = funded(); CHECK(choose_05(f, s).source == 3);
    f.bag = 20; f.sources[3].pressured = true;
    CHECK(choose_05(f, s).route == 0); // A forecast is insufficient for rerouting.
    f.fresh_pressure = true; auto alternative = choose_05(f, s);
    CHECK(alternative.goal == Goal::bank && alternative.route == 1 && alternative.port == 1);
    f.bag = 0; f.sources[3].alternate_port = 0; s = funded();
    CHECK(choose_05(f, s).source < 3); // Shared altar suffix defeats complete independence.
    f.control = true; s = funded(); CHECK(choose_05(f, s).source == 4);
}
void gatehouse() {
    auto f = frame(); f.ordinal = 1; f.sources[3].local_gate = true; f.sources[3].connector = 88;
    f.distances[0][3] = f.distances[3][0] = 8; State s;
    CHECK(choose_06(f, s).source < 3); // A guard title supplies no remote protection.
    put_ally(f, 0, 0); CHECK(choose_06(f, s).source == 3);
    f.sources[3].pressured = f.fresh_pressure = true;
    CHECK(choose_06(f, s).goal == Goal::withdraw && !s.relocated);
    f.sources[3].pressured = f.fresh_pressure = false; f.control = true; s = {};
    CHECK(choose_06(f, s).source == 4);
    f.control = false; f.ordinal = 0; f.fresh_pressure = true; f.enemy_count = 1;
    f.pressure_tile = 87; s = {}; CHECK(choose_06(f, s).goal == Goal::guard);
    f.round += 8; CHECK(choose_06(f, s).goal == Goal::farm);
}
void causeway() {
    auto f = frame(); f.sources[3].cut_proven = true; f.sources[3].connector = 88;
    f.sources[4].opened_by = 3; State s;
    CHECK(choose_07(f, s).source == 3 && choose_07(f, s).holder_ordinal == 2);
    f.sources[3].alive = false; f.sources[3].reward_available = true;
    put_ally(f, 2, 3, true); f.allies[0].tile = 88;
    CHECK(choose_07(f, s).release_helper);
    f.sources[3].reward_available = false; f.sources[3].respawning = true;
    f.sources[3].ready_after = 80; f.allies[0].bag = 20; s.captured[3] = true;
    CHECK(choose_07(f, s).source < 3); // Holder still physically blocks the transport tile.
    f.allies[0].tile = 89; auto crossing = choose_07(f, s);
    CHECK(crossing.source == 4 && crossing.waypoint == 88 && crossing.mechanism);
    f.sources[3].ready_after = 5; CHECK(choose_07(f, s).source < 3);
    f.sources[3].cut_proven = false; s = {}; CHECK(choose_07(f, s).source < 3);
    f = frame(); f.round = 63; f.sources[3].cut_proven = true; f.sources[3].connector = 88;
    f.sources[3].feasible = false; f.sources[3].joint_feasible = true;
    f.sources[4].opened_by = 3; s = {};
    CHECK(choose_07(f, s).goal == Goal::rally);
    f.round = 71; CHECK(choose_07(f, s).source < 3 && s.inspection_failed);
    f.round = 72; CHECK(choose_07(f, s).source < 3);
}
void sanctuary() {
    auto f = frame(); for (auto& x : f.sources) x.incoming = 1;
    State active, control; CHECK(choose_08(f, active).source == 0);
    f.control = true; CHECK(choose_08(f, control).source == 4);
    f.ordinal = 1; f.control = false; f.healing_ready = true; put_ally(f, 0, 0);
    f.allies[0].health = 50; active = {};
    CHECK(choose_08(f, active).heal && choose_08(f, active).peer == 40);
    f.round += 8; CHECK(!choose_08(f, active).heal);
    f.control = true; control = {}; CHECK(choose_08(f, control).heal);
    f.health = 20; CHECK(choose_08(f, active).goal == Goal::recover);
}
void empty_and_extensions() {
    Frame f; State s;
    CHECK(choose_01(f, s).goal == Goal::recover);
    CHECK(choose_02(f, s).goal == Goal::recover);
    CHECK(choose_03(f, s).goal == Goal::recover);
    CHECK(choose_04(f, s).goal == Goal::recover);
    CHECK(choose_05(f, s).goal == Goal::recover);
    CHECK(choose_06(f, s).goal == Goal::recover);
    CHECK(choose_07(f, s).goal == Goal::recover);
    CHECK(choose_08(f, s).goal == Goal::recover);
    f = frame(); f.ordinal = 4; s = {}; CHECK(choose_01(f, s).source == 4);
    f = frame(); f.control = true;
    for (auto& source : f.sources) source.seen = source.feasible = false;
    s = {}; CHECK(choose_04(f, s).goal == Goal::inspect);
    f.round += 8; CHECK(choose_04(f, s).goal == Goal::recover && s.inspection_failed);
}
} // namespace
int main() {
    concentric(); interior(); frontier(); islander(); forks(); gatehouse(); causeway(); sanctuary(); empty_and_extensions();
    std::printf("plan7 live doctrines 01..08: %d checks passed\n", checks);
}
