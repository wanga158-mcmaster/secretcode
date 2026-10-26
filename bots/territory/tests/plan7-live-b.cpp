#include "../common/src/doctrines_b.h"
#include <cstdio>
#include <cstdlib>

namespace {
using namespace p7::live;
#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #condition); std::abort(); \
} } while (false)

Frame frame() {
    Frame f; f.round = 12; f.self = 7; f.team_size = 3;
    f.source_count = 6; f.altar_count = f.port_count = 2; f.home = 0;
    f.health = f.max_health = 100; f.damage = 20;
    const int scores[]{20, 40, 25, 45, 30, 50};
    const int rings[]{0, 2, 0, 2, 1, 1};
    for (int i = 0; i < 6; ++i) {
        auto& c = f.sources[i]; c.id = 100 + i; c.tile = 20 + i;
        c.port = i < 3 ? 0 : 1; c.ring = rings[i];
        c.distance = 4 + i; c.exit_distance = c.recovery_distance = 3;
        c.cycle_turns = 12 + i; c.gems = 20; c.gold = 5; c.health = 20;
        c.seen = c.alive = c.reachable = c.feasible = c.home_connected = true;
        c.score = {scores[i], scores[i], scores[i]};
        for (int j = 0; j < 8; ++j) f.distances[i][j] = i == j ? 0 : 4;
    }
    return f;
}
void poor(Frame& f, int source) { f.sources[source].score = {0, 0, 0}; }
void bank_completed(Frame& f, State& s) {
    // Positive same-project receipt mock. Counter-only foreign/lost-bag cases
    // deliberately bypass this helper and keep the bound flags false.
    if (s.captures <= s.captures_at_admission) ++s.captures;
    f.bag = 0; s.bank_pending = false; ++s.banks;
    s.payout_seen = s.bank_seen = true;
}

void ports() {
    Frame f = frame(); State s;
    Task t = choose_09(f, s); CHECK(t.source == 1); CHECK(s.port == 0);
    f.bag = 17; f.fresh_pressure = true; f.pressure_port = 0;
    f.sources[1].pressured = true; poor(f, 1);
    t = choose_09(f, s);
    CHECK(t.goal == Goal::bank); CHECK(t.port == 1);
    CHECK(s.port == 0); CHECK(s.pending_port == 1); CHECK(!s.relocated);
    bank_completed(f, s); t = choose_09(f, s);
    CHECK(s.port == 1 && s.relocated); CHECK(t.source == 5);
    Frame control = frame(); control.control = true; State cs;
    choose_09(control, cs); control.fresh_pressure = true; control.pressure_port = 0;
    control.sources[1].pressured = true; poor(control, 1); control.bag = 17;
    CHECK(choose_09(control, cs).goal == Goal::bank);
    CHECK(cs.port == 0 && cs.pending_port < 0 && !cs.relocated);
    Frame single = frame(); single.port_count = 1;
    for (int i = 0; i < single.source_count; ++i) single.sources[i].port = 0;
    State ss; choose_09(single, ss); single.fresh_pressure = true; single.pressure_port = 0;
    CHECK(!choose_09(single, ss).mechanism); CHECK(!ss.relocated);
    Frame lost = frame(); State ls; choose_09(lost, ls); ls.banks = 3;
    lost.fresh_pressure = true; lost.pressure_port = 0; lost.sources[ls.source].pressured = true;
    poor(lost, ls.source); lost.bag = 19; choose_09(lost, ls);
    lost.bag = 0; ls.bank_pending = false; ++ls.banks; // A fallback's bank cannot finish this project.
    choose_09(lost, ls);
    CHECK(ls.port == 0 && !ls.relocated && ls.inspection_failed); // Lost gems are not a bank.
}

void migration() {
    Frame f = frame(); State s; choose_10(f, s);
    CHECK(s.port == 0);
    for (int i = 0; i < 3; ++i) f.sources[i].feasible = false;
    ++s.captures; s.payout_seen = true; f.bag = 25;
    Task t = choose_10(f, s); CHECK(t.goal == Goal::bank); CHECK(s.port == 0);
    const int destination = s.previous_source; CHECK(destination >= 3);
    bank_completed(f, s); t = choose_10(f, s);
    CHECK(t.goal == Goal::inspect && s.port == 0);
    f.current = destination; t = choose_10(f, s);
    CHECK(s.port == 1 && s.relocated && t.goal == Goal::farm);
    CHECK(!s.inspection_failed);
    // Freshly visible profitable old supply and actual conversion can license
    // return; a mature timer without observation cannot.
    ++s.captures; ++s.banks; s.payout_seen = s.bank_seen = true;
    f.sources[0].feasible = true; f.sources[0].score = {90, 90, 90};
    f.sources[0].seen = false;
    choose_10(f, s); CHECK(s.port == 1);
    f.sources[0].seen = true; t = choose_10(f, s);
    CHECK(t.goal == Goal::inspect && s.pending_port == 0);

    Frame failed = frame(); State fs; choose_10(failed, fs); ++fs.captures;
    for (int i = 0; i < 3; ++i) failed.sources[i].feasible = false;
    choose_10(failed, fs); failed.round += 8;
    t = choose_10(failed, fs); CHECK(fs.inspection_failed); CHECK(t.goal == Goal::withdraw);
    failed.round += 64; choose_10(failed, fs); CHECK(fs.inspection_failed);

    Frame control = frame(); control.control = true; State cs; choose_10(control, cs);
    for (int i = 0; i < 3; ++i) control.sources[i].feasible = false;
    ++cs.captures; choose_10(control, cs); CHECK(!cs.relocated && cs.phase == 0);
}

void elastic() {
    Frame f = frame(); State s; choose_11(f, s);
    CHECK(s.region == 3 && s.home_source == 2);
    f.sources[3].pressured = true; poor(f, 3); f.fresh_pressure = true; f.pressure_port = 1;
    f.bag = 13; Task t = choose_11(f, s);
    CHECK(t.goal == Goal::bank && s.phase == 1); CHECK(s.port == 1);
    bank_completed(f, s); t = choose_11(f, s);
    CHECK(t.goal == Goal::withdraw && s.phase == 2 && s.port == 0);
    f.sources[3].pressured = false; f.sources[3].score = {70, 70, 70}; f.fresh_pressure = false;
    f.sources[3].seen = false; t = choose_11(f, s); CHECK(t.goal == Goal::withdraw);
    f.sources[3].seen = true; t = choose_11(f, s); CHECK(t.goal == Goal::inspect);
    f.round += 8; t = choose_11(f, s); CHECK(s.inspection_failed && t.goal == Goal::withdraw);
    f.ordinal = 2; State inner; t = choose_11(f, inner);
    CHECK(t.source == 2 && !t.mechanism);
}

void counterfront() {
    Frame f = frame(); f.ordinal = 2; State s; choose_12(f, s);
    CHECK(s.port == 0);
    f.fresh_pressure = true; f.pressure_port = 0; f.sources[s.source].pressured = true; poor(f, s.source);
    f.bag = 9; Task t = choose_12(f, s); CHECK(t.goal == Goal::bank && s.port == 0);
    const int landing = s.previous_source; CHECK(landing >= 3 && f.sources[landing].feasible);
    bank_completed(f, s); t = choose_12(f, s); CHECK(t.goal == Goal::inspect);
    f.current = landing; t = choose_12(f, s); CHECK(s.port == 1 && s.relocated);
    CHECK(t.peer < 0); // Unseen opposite producer is never counted.
    f.sources[3].feasible = false; f.sources[3].joint_feasible = true; f.sources[3].score = {99, 99, 99};
    f.ally_count = 1; f.allies[0].visible = true; f.allies[0].ordinal = 1;
    f.allies[0].id = 8; f.allies[0].group = landing; f.allies[0].health = f.allies[0].max_health = 100;
    t = choose_12(f, s); CHECK(t.source == 3 && t.peer == 8 && t.holder_ordinal == 1);
    f.allies[0].visible = false; t = choose_12(f, s); CHECK(t.source != 3 && t.peer < 0);
    Frame control = frame(); control.control = true; control.ordinal = 2; State cs;
    choose_12(control, cs); control.fresh_pressure = true; control.pressure_port = 0;
    control.sources[cs.source].pressured = true; choose_12(control, cs);
    CHECK(cs.port == 0 && !cs.relocated);
    Frame lost = frame(); lost.ordinal = 2; State ls; choose_12(lost, ls); ls.banks = 3;
    lost.fresh_pressure = true; lost.pressure_port = 0; lost.sources[ls.source].pressured = true;
    poor(lost, ls.source); lost.bag = 19; choose_12(lost, ls);
    lost.bag = 0; ls.bank_pending = false; t = choose_12(lost, ls);
    CHECK(ls.port == 0 && !ls.relocated && ls.inspection_failed && t.goal == Goal::withdraw);
}

void encirclement() {
    Frame f = frame(); State s; f.fresh_pressure = true;
    f.enemy_count = 1; f.enemies[0].visible = true; f.enemies[0].id = 11; f.enemies[0].bag = 70;
    CHECK(choose_13(f, s).goal == Goal::farm); // A visible bag alone proves no denial.
    f.denial_certificate = true; f.denial_observed_round = f.round; f.denial_enemy = 11;
    f.denial_waypoint = 44; f.denial_port = 1; f.denial_bag = 70;
    f.denial = {60, 60, 60}; f.denial_force_independent = {20, 20, 20};
    CHECK(choose_13(f, s).goal == Goal::pressure);
    f.denial[0] = 0; CHECK(choose_13(f, s).goal == Goal::farm); // Continued fight unresolved.
    f.denial[0] = 60; f.denial[2] = 0; CHECK(choose_13(f, s).goal == Goal::farm); // Substitute succeeds.
    f.denial[2] = 60; f.altar_count = 3; CHECK(choose_13(f, s).goal == Goal::farm);
    f.altar_count = 2; f.enemies[0].visible = false; CHECK(choose_13(f, s).goal == Goal::farm);
    f.enemies[0].visible = true; f.denial[1] = 90; CHECK(choose_13(f, s).goal == Goal::farm);
    f.denial[1] = 60; f.round += 8; CHECK(choose_13(f, s).goal == Goal::farm);
    CHECK(s.phase == 2); f.denial_observed_round = f.round;
    CHECK(choose_13(f, s).goal == Goal::farm); // A retired attempt stays retired.
    State control; f.control = true; CHECK(choose_13(f, control).goal == Goal::farm);
}

void harvest() {
    Frame f = frame(); State s; choose_14(f, s); CHECK(s.port == 1 && s.home_source == 2);
    s.captured[3] = s.captured[4] = s.captured[5] = true; s.captures = 3; f.bag = 22;
    Task t = choose_14(f, s); CHECK(t.goal == Goal::bank && s.phase == 1);
    bank_completed(f, s); t = choose_14(f, s);
    CHECK(t.goal == Goal::withdraw && t.source == 2 && s.phase == 2);
    s.captured.fill(false); f.round += 80; t = choose_14(f, s);
    CHECK(t.goal == Goal::withdraw); // Renewed raid supply cannot restart a retired raid.
    Frame control = frame(); control.control = true; State cs; choose_14(control, cs);
    cs.captured.fill(true); cs.captures = 3; t = choose_14(control, cs);
    CHECK(t.goal == Goal::farm && control.sources[t.source].port == 1 && cs.phase == 0);
}

void reclamation() {
    Frame f = frame(); f.ordinal = 1; State s; choose_15(f, s);
    CHECK(s.port == 1);
    f.sources[1].progress = 0.7F; f.sources[1].health = 6; f.sources[1].score = {90, 90, 90};
    f.bag = 11; Task t = choose_15(f, s); CHECK(t.goal == Goal::bank && s.port == 1);
    bank_completed(f, s); t = choose_15(f, s); CHECK(t.goal == Goal::inspect);
    f.current = 1; t = choose_15(f, s); CHECK(t.source == 1 && s.port == 0 && s.relocated);
    CHECK(t.peer < 0); // No promise of another actor arriving.
    Frame failed = frame(); failed.ordinal = 1; State fs; choose_15(failed, fs);
    failed.sources[1].progress = 0.6F; failed.sources[1].score = {99, 99, 99};
    choose_15(failed, fs); failed.round += 4; t = choose_15(failed, fs);
    CHECK(t.goal == Goal::withdraw && fs.inspection_failed);
    Frame control = frame(); control.ordinal = 1; control.control = true; State cs;
    choose_15(control, cs); control.sources[1].progress = 0.9F; control.sources[1].score = {99, 99, 99};
    choose_15(control, cs); CHECK(!cs.relocated && cs.port == 1);
}

void veil_and_preservation() {
    Frame f = frame(); f.sources[5].occluded = true; f.own_ward_ready = true; State s;
    Task t = choose_16(f, s); CHECK(t.source == 5 && t.mechanism && t.ward);
    f.own_ward_ready = false; t = choose_16(f, s); CHECK(!t.ward);
    f.bag = 15; f.sources[5].seen = false; t = choose_16(f, s);
    CHECK(t.goal == Goal::bank && !t.mechanism && s.inspection_failed);
    Frame control = frame(); control.sources[5].occluded = true; control.control = true; State cs;
    t = choose_16(control, cs); CHECK(t.source == 3 && !t.mechanism);
    using Controller = Task(*)(const Frame&, State&);
    constexpr Controller controllers[]{choose_09, choose_10, choose_11, choose_12,
        choose_13, choose_14, choose_15, choose_16};
    for (Controller choose : controllers) {
        Frame capture = frame(); State state; capture.safe_capture = true; capture.current = 0;
        CHECK(choose(capture, state).source == 0);
        capture.safe_capture = false; capture.channel_active = true; capture.bag = 13;
        CHECK(choose(capture, state).goal == Goal::bank);
        Frame empty; State es; Task idle = choose(empty, es);
        CHECK(idle.source < 0 && idle.goal != Goal::pressure);
    }
}
} // namespace
int main() {
    ports(); migration(); elastic(); counterfront(); encirclement();
    harvest(); reclamation(); veil_and_preservation();
    std::puts("plan7 live doctrines 09-16 checks passed");
}
