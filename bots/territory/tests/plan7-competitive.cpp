#include "../common/src/competitive.h"
#include "../common/src/doctrines_a.h"
#include "../common/src/doctrines_b.h"
#include <cstdio>
#include <cstdlib>

namespace {
using p7::live::CompetitiveSource;
using p7::live::CompetitiveState;
using p7::live::competitive_shortlist;
using p7::live::competitive_plan;
using p7::live::Frame;
using p7::live::Task;
using p7::live::Goal;
using p7::live::Design;
using p7::live::farm;
int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #condition); std::abort(); } } while (false)

bool contains(const std::vector<CompetitiveSource>& sources, const std::vector<int>& selected, int id) {
    return std::any_of(selected.begin(), selected.end(), [&](int i) { return sources[i].id == id; });
}
void contract(const std::vector<CompetitiveSource>& sources, const std::vector<int>& selected) {
    CHECK(selected.size() <= 8);
    std::vector<bool> used(sources.size(), false);
    for (int i : selected) {
        CHECK(i >= 0 && i < static_cast<int>(sources.size()));
        CHECK(sources[i].distance >= 0);
        CHECK(!used[i]);
        used[i] = true;
    }
}
void progression_before_maximum_reward() {
    const std::vector<CompetitiveSource> sources{
        {0, 1, 4}, {1, 2, 0}, {2, 3, 4}, {3, 4, 0},
        {4, 5, 11}, {5, 8, 18}, {6, 9, 18}, {7, 10, 15},
        {18, 30, 66}, {19, 40, 66}
    };
    const auto selected = competitive_shortlist(sources);
    contract(sources, selected);
    for (int id = 0; id < 8; ++id) CHECK(contains(sources, selected, id));
    CHECK(!contains(sources, selected, 18));
    CHECK(!contains(sources, selected, 19));
}
void ties_and_reachable_fill() {
    const std::vector<CompetitiveSource> sources{
        {4, 1, 9}, {2, 1, 9}, {1, 1, 2}, {0, 1, 0}, {7, 2, 0}, {9, 2, 6}
    };
    const auto selected = competitive_shortlist(sources);
    contract(sources, selected);
    CHECK((selected == std::vector<int>{1, 0, 2, 3, 5, 4}));
    const std::vector<CompetitiveSource> no_gems{
        {9, 9, 0}, {8, 8, 0}, {7, 7, 0}, {6, 6, 0}, {5, 5, 0},
        {4, 4, 0}, {3, 3, 0}, {2, 2, 0}, {1, 1, 0}
    };
    const auto filled = competitive_shortlist(no_gems);
    contract(no_gems, filled);
    for (int id = 1; id <= 8; ++id) CHECK(contains(no_gems, filled, id));
    CHECK(!contains(no_gems, filled, 9));
}
void unreachable_and_empty() {
    CHECK(competitive_shortlist({}).empty());
    CHECK(competitive_shortlist({{1, -1, 66}, {2, -2, 18}}).empty());
    const std::vector<CompetitiveSource> sources{
        {1, -1, 999}, {2, -3, 18}, {3, 0, 0}, {4, 2, 0}
    };
    const auto selected = competitive_shortlist(sources);
    contract(sources, selected);
    CHECK((selected == std::vector<int>{2, 3}));
}
void collision_progression() {
    // Public geometry: results/version-reconciliation-3.0.2/current-map-geometry.json
    // SHA256 b9d50d790248a30ab070c0bce24b1a4f27e2ac43fffa079fd2156168a1deb93e.
    // Distances were derived by multi-root, eight-neighbor BFS from the team's
    // full spawn rectangle, blocking walls, holes, altars and enemy spawn.
    // Each group's distance is the minimum distance to any member spawn tile.
    const std::vector<CompetitiveSource> team_a{
        {18, 30, 66}, {14, 15, 15}, {16, 22, 22}, {0, 4, 4}, {8, 6, 0},
        {2, 8, 4}, {10, 11, 0}, {4, 13, 11}, {12, 16, 0}, {6, 18, 18},
        {7, 18, 18}, {13, 20, 0}, {5, 22, 11}, {11, 25, 0}, {3, 26, 4},
        {9, 28, 0}, {1, 31, 4}, {19, 43, 66}, {15, 33, 15}, {17, 35, 22}
    };
    const std::vector<CompetitiveSource> team_b{
        {18, 44, 66}, {14, 34, 15}, {16, 36, 22}, {0, 32, 4}, {8, 29, 0},
        {2, 27, 4}, {10, 26, 0}, {4, 23, 11}, {12, 21, 0}, {6, 18, 18},
        {7, 18, 18}, {13, 16, 0}, {5, 13, 11}, {11, 11, 0}, {3, 8, 4},
        {9, 6, 0}, {1, 4, 4}, {19, 30, 66}, {15, 15, 15}, {17, 22, 22}
    };
    const auto a = competitive_shortlist(team_a), b = competitive_shortlist(team_b);
    contract(team_a, a); contract(team_b, b);
    for (int id : {0, 2, 8, 10, 4, 6, 7}) CHECK(contains(team_a, a, id));
    for (int id : {1, 3, 9, 11, 5, 6, 7}) CHECK(contains(team_b, b, id));
    for (int id : {18, 19}) {
        CHECK(!contains(team_a, a, id));
        CHECK(!contains(team_b, b, id));
    }
}
Frame production_frame() {
    Frame f; f.round = 1; f.tile = 20; f.health = f.max_health = 100; f.kit_level = 10;
    f.design = Design::islander; // No mobility fact: exercise generic fallback.
    f.team_size = 1; // Isolate general production fixtures from role preferences.
    f.source_count = 4;
    for (int i = 0; i < f.source_count; ++i) {
        auto& x = f.sources[i]; x.id = i + 10; x.port = 0; x.distance = 10 + i;
        x.cycle_turns = 40; x.reachable = x.seen = x.feasible = true;
    }
    f.sources[0].gems = 4;
    f.sources[1].gems = 11; f.sources[1].cycle_turns = 60;
    f.sources[2].gems = 18; f.sources[2].cycle_turns = 70;
    f.sources[3].gold = 200; f.sources[3].cycle_turns = 5;
    return f;
}
Task recovery() { Task t; t.goal = Goal::recover; return t; }
void production_ranking_and_geography() {
    Frame f = production_frame(); CompetitiveState s;
    CHECK(competitive_plan(f, farm(0), s).source == 2);
    CHECK(f.sources[2].minimum() == 0); // A choice never invents secured score.
    s = {}; f.sources[1].cycle_turns = 50;
    Task geographic = farm(1, true);
    const auto t = competitive_plan(f, geographic, s);
    CHECK(t.source == 1 && t.mechanism);
    s = {}; f.sources[1].cycle_turns = 60;
    f.ally_count = 1; f.allies[0].visible = true; f.allies[0].group = 2;
    f.sources[1].cycle_turns = 50;
    CHECK(competitive_plan(f, recovery(), s).source == 1); // Soft occupancy cost.
    s = {}; f.ally_count = 0;
    for (int i = 0; i < 3; ++i) f.sources[i].respawning = true;
    CHECK(competitive_plan(f, recovery(), s).goal == Goal::recover);
    f.kit_level = 0;
    CHECK(competitive_plan(f, recovery(), s).source == 3);
}
void long_progress_and_blocked_retirement() {
    Frame f = production_frame(); CompetitiveState s;
    f.sources[2].seen = false; f.sources[2].feasible = false; f.sources[2].distance = 20;
    CHECK(competitive_plan(f, recovery(), s).goal == Goal::inspect);
    for (int step = 1; step <= 12; ++step) {
        ++f.round; ++f.tile; --f.sources[2].distance;
        f.sources[0].gems = 100; // A moving commitment survives changed rankings.
        const auto t = competitive_plan(f, farm(0), s);
        CHECK(t.source == 2 && t.goal == Goal::inspect);
    }
    f.round += 8;
    CHECK(competitive_plan(f, farm(2), s).source != 2);
    CHECK(s.retry_after[2] == f.round + 32);
    const int retired_until = s.retry_after[2];
    s.source = -1; f.round = retired_until - 1;
    f.sources[2].gems = 1000;
    CHECK(competitive_plan(f, farm(2), s).source != 2);
    s.source = -1; f.round = retired_until;
    CHECK(competitive_plan(f, recovery(), s).source == 2);
    f.sources[2].seen = f.sources[2].feasible = true; f.current = 2;
    for (int step = 0; step < 12; ++step) {
        ++f.round;
        CHECK(competitive_plan(f, recovery(), s).source == 2);
    }
    f.sources[2].respawning = true;
    CHECK(competitive_plan(f, recovery(), s).source != 2);
}
void preserve_capture_channel_bag_and_contact() {
    Frame f = production_frame(); CompetitiveState s;
    competitive_plan(f, recovery(), s); CHECK(s.source == 2);
    Task proposal = farm(0); proposal.goal = Goal::rally;
    f.safe_capture = true; f.bag = p7::live::competitive_bank_target(f.design);
    CHECK(competitive_plan(f, proposal, s).goal == Goal::rally);
    CHECK(s.source == 2);
    f.safe_capture = false; f.channel_active = true;
    CHECK(competitive_plan(f, proposal, s).goal == Goal::rally);
    f.channel_active = false;
    CHECK(competitive_plan(f, proposal, s).goal == Goal::bank);
    CHECK(s.source == -1);
    f.bag = 0; proposal.goal = Goal::pressure; proposal.peer = 70;
    f.enemy_count = 1; f.enemies[0].visible = true; f.enemies[0].id = 70;
    CHECK(competitive_plan(f, proposal, s).goal == Goal::pressure);
    f.enemies[0].visible = false;
    CHECK(competitive_plan(f, proposal, s).goal == Goal::farm);
    proposal.goal = Goal::guard; f.enemies[0].visible = true;
    CHECK(competitive_plan(f, proposal, s).goal == Goal::guard);
}
void inspection_and_owned_health() {
    Frame f = production_frame(); CompetitiveState s;
    for (int i = 0; i < f.source_count; ++i) {
        f.sources[i].gems = i == 1 ? 11 : 0; f.sources[i].gold = 0;
        f.sources[i].feasible = false; f.sources[i].incoming = 40;
    }
    CHECK(competitive_plan(f, recovery(), s).goal == Goal::recover);
    f.sources[1].seen = false;
    auto t = competitive_plan(f, recovery(), s);
    CHECK(t.goal == Goal::inspect && t.source == 1);
    CHECK(!f.sources[1].feasible && f.sources[1].minimum() == 0);
    f.sources[1].seen = true;
    CHECK(competitive_plan(f, recovery(), s).goal == Goal::recover);
    f.sources[1].incoming = 10; f.health = 56;
    CHECK(competitive_plan(f, recovery(), s).goal == Goal::farm);
    CHECK(!f.sources[1].feasible);
    f.health = 55;
    CHECK(competitive_plan(f, recovery(), s).goal == Goal::recover);
    f.health = 1; f.sources[1].reward_available = true;
    CHECK(competitive_plan(f, recovery(), s).goal == Goal::farm);
}
void owned_jungle_health_mitigation() {
    using p7::live::own_camp_damage_factor;
    using p7::live::self_camp_incoming;
    CHECK(own_camp_damage_factor(4, .3f) == 1);
    CHECK(std::abs(own_camp_damage_factor(5, .3f) - .7f) < .00001f);
    CHECK(own_camp_damage_factor(10, .3f) == own_camp_damage_factor(5, .3f));
    CHECK(own_camp_damage_factor(5, 0) == 1);
    CHECK(own_camp_damage_factor(5, 1) == 0);
    for (float invalid : {-1.f, 1.1f, std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::quiet_NaN()})
        CHECK(own_camp_damage_factor(5, invalid) == 1);

    Frame f = production_frame(); CompetitiveState s;
    for (int i = 0; i < f.source_count; ++i) {
        f.sources[i].gems = i == 1 ? 11 : 0; f.sources[i].gold = 0;
        f.sources[i].feasible = false; f.sources[i].incoming = 30;
    }
    f.health = 90;
    CHECK(f.camp_damage_factor == 1 && self_camp_incoming(f, f.sources[1]) == 30);
    CHECK(competitive_plan(f, farm(1), s).goal == Goal::recover);
    f.camp_damage_factor = own_camp_damage_factor(4, .3f);
    CHECK(competitive_plan(f, farm(1), s).goal == Goal::recover);
    f.camp_damage_factor = own_camp_damage_factor(5, .3f);
    CHECK(std::abs(self_camp_incoming(f, f.sources[1]) - 21) < .00001f);
    const auto partial = competitive_plan(f, recovery(), s);
    CHECK(partial.goal == Goal::farm && partial.source == 1);
    CHECK(f.sources[1].incoming == 30 && !f.sources[1].feasible && f.sources[1].minimum() == 0);
    f.health = 82; s = {};
    CHECK(competitive_plan(f, farm(1), s).goal == Goal::recover);
    f.health = 90;
    for (float invalid : {-1.f, 1.1f, std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::quiet_NaN()}) {
        f.camp_damage_factor = invalid; s = {};
        CHECK(self_camp_incoming(f, f.sources[1]) == 30);
        CHECK(competitive_plan(f, farm(1), s).goal == Goal::recover);
    }
    CHECK(f.sources[1].incoming == 30 && !f.sources[1].feasible);
}
Frame affordable_first_frame() {
    Frame f = production_frame(); f.source_count = 2;
    f.health = f.max_health = 975; f.damage = 30; f.focus = 105;
    f.camp_damage_factor = p7::live::own_camp_damage_factor(5, .3f);
    // Actual eight group6 versus group4 public HP/payload/home distances;
    // these existing basic-only cycle estimates put the heavy partial first.
    f.sources[0].gems = 11; f.sources[0].health = 1470; f.sources[0].incoming = 20;
    f.sources[0].distance = f.sources[0].spawn_distance = 13; f.sources[0].cycle_turns = 117;
    f.sources[1].gems = 59; f.sources[1].health = 7560; f.sources[1].incoming = 180;
    f.sources[1].distance = f.sources[1].spawn_distance = 15; f.sources[1].cycle_turns = 364;
    f.sources[1].feasible = false;
    return f;
}
void affordable_before_seen_partial_work() {
    Frame f = affordable_first_frame(); CompetitiveState s;
    CHECK(p7::live::competitive_detail::base_rate(f, 1) > p7::live::competitive_detail::base_rate(f, 0));
    CHECK(competitive_plan(f, farm(1), s).source == 0);
    s.source = 1; s.best_distance = f.sources[1].distance; s.last_progress = f.round;
    f.current = 1;
    CHECK(competitive_plan(f, farm(1), s).source == 0);
    f.current = -1;
    CHECK(!f.sources[1].feasible && f.sources[1].health == 7560 && f.sources[1].minimum() == 0);
    f.sources[0].feasible = false; f.sources[0].reward_available = true; s = {};
    CHECK(competitive_plan(f, farm(1), s).source == 0);
    f = affordable_first_frame(); f.sources[1].seen = false; s = {};
    const auto inspection = competitive_plan(f, farm(1), s);
    CHECK(inspection.source == 1 && inspection.goal == Goal::inspect);
    f = affordable_first_frame(); f.sources[0].feasible = false; s = {};
    CHECK(competitive_plan(f, recovery(), s).source == 1);

    for (int unavailable = 0; unavailable < 5; ++unavailable) {
        f = affordable_first_frame(); s = {};
        if (unavailable == 0) f.sources[0].distance = -1;
        if (unavailable == 1) f.sources[0].respawning = true;
        if (unavailable == 2) f.sources[0].cycle_turns = -1;
        if (unavailable == 3) f.sources[0].gems = f.sources[0].gold = 0;
        if (unavailable == 4) s.retry_after[0] = f.round + 32;
        CHECK(competitive_plan(f, recovery(), s).source == 1);
    }
    f = affordable_first_frame(); f.round = 10; s = {};
    s.source = 0; s.best_distance = f.sources[0].distance; s.last_progress = 2;
    CHECK(competitive_plan(f, recovery(), s).source == 1);
    CHECK(s.retry_after[0] == f.round + 32);

    f = affordable_first_frame(); s = {};
    Task hold = farm(1); hold.goal = Goal::rally;
    f.safe_capture = true;
    CHECK(competitive_plan(f, hold, s) == hold);
    f.safe_capture = false; f.channel_active = true;
    CHECK(competitive_plan(f, hold, s) == hold);
}
void stale_proposals_and_safe_fallback() {
    Frame f = production_frame(); CompetitiveState s;
    for (Goal goal : {Goal::guard, Goal::rally, Goal::withdraw, Goal::pressure}) {
        s = {};
        Task proposal = farm(2, true); proposal.goal = goal; proposal.waypoint = 999;
        proposal.route = 4; proposal.peer = 70; proposal.mobility = proposal.heal = true;
        const auto task = competitive_plan(f, proposal, s);
        CHECK(task.source == 2 && task.goal == Goal::farm);
        CHECK(task.waypoint == -1 && task.route == 0 && task.peer == -1);
        CHECK(!task.mobility && !task.heal && !task.mechanism);
    }
    Task stale = farm(2, true); stale.goal = Goal::withdraw; stale.waypoint = 999;
    stale.port = 1; stale.mobility = true; f.bag = p7::live::competitive_bank_target(f.design);
    const auto bank = competitive_plan(f, stale, s);
    CHECK(bank.goal == Goal::bank && bank.port == -1 && bank.source == -1);
    CHECK(bank.waypoint == -1 && !bank.mobility && !bank.mechanism);
    f.bag = 0;
    for (int i = 0; i < f.source_count; ++i) {
        f.sources[i].gems = f.sources[i].gold = 0;
        f.sources[i].feasible = false; f.sources[i].incoming = 40;
    }
    s = {};
    CHECK(competitive_plan(f, farm(1), s).goal == Goal::recover);
    f.sources[1].seen = false;
    const auto inspect = competitive_plan(f, farm(1), s);
    CHECK(inspect.goal == Goal::inspect && inspect.source == 1);
    Task other; other.goal = Goal::withdraw; other.waypoint = 10;
    CHECK(competitive_plan(f, other, s).goal == Goal::withdraw);
    CHECK(competitive_plan(f, other, s).waypoint == 10);
}
void soft_role_affinity() {
    Frame f = production_frame(); f.source_count = 5; f.team_size = 3;
    for (int i = 0; i < f.source_count; ++i) {
        auto& x = f.sources[i]; x.id = 40 + i; x.distance = x.spawn_distance = 20;
        x.cycle_turns = 40; x.gems = x.gold = 0;
        x.reachable = x.seen = x.feasible = true;
    }
    f.sources[0].gold = f.sources[4].gold = 200;
    f.sources[0].spawn_distance = f.sources[4].spawn_distance = 0;
    f.sources[1].gems = 11; f.sources[1].cycle_turns = 44;
    f.sources[2].gems = f.sources[3].gems = 18;
    f.sources[2].cycle_turns = f.sources[3].cycle_turns = 72;
    f.sources[2].spawn_distance = 30;
    const int expected[]{3, 2, 1};
    for (int ordinal = 0; ordinal < 3; ++ordinal) {
        f.ordinal = ordinal; CompetitiveState s;
        const auto task = competitive_plan(f, farm(1), s);
        CHECK(task.source == expected[ordinal]);
        CHECK(task.goal == Goal::farm);
    }
    // Equal home distance falls back to native identity, regardless of storage.
    f.sources[2].spawn_distance = 20; f.ordinal = 0; CompetitiveState s;
    CHECK(competitive_plan(f, recovery(), s).source == 2);
    f.ordinal = 1; s = {};
    CHECK(competitive_plan(f, recovery(), s).source == 3);
    // Unavailable or unsafe preferred routes yield to productive alternatives.
    f.sources[3].respawning = true; s = {};
    CHECK(competitive_plan(f, recovery(), s).source != 3);
    f.sources[3].respawning = false; f.sources[3].feasible = false;
    f.sources[3].incoming = 40; s = {};
    CHECK(competitive_plan(f, recovery(), s).source != 3);
    f.sources[3].feasible = true; f.sources[3].incoming = 0;
    s = {}; s.retry_after[3] = f.round + 32;
    CHECK(competitive_plan(f, recovery(), s).source != 3);
    // Affinity is soft: a much slower preferred route cannot monopolize work.
    f.sources[3].cycle_turns = 1000; s = {};
    CHECK(competitive_plan(f, recovery(), s).source != 3);
}
Frame layout_frame() {
    Frame f = production_frame(); f.ordinal = 0; f.port_count = f.altar_count = 2;
    const int gems[]{11, 18, 18, 15}, turns[]{45, 60, 60, 70}, homes[]{4, 20, 21, 10};
    for (int i = 0; i < f.source_count; ++i) {
        auto& x = f.sources[i]; x.gems = gems[i]; x.gold = 0; x.cycle_turns = turns[i];
        x.spawn_distance = homes[i]; x.port = i >= 2 ? 1 : 0; x.incoming = 20;
        x.health = 100; x.tile = 100 + i; x.home_connected = true;
        for (int j = 0; j < f.source_count; ++j) f.distances[i][j] = std::abs(i - j) * 4;
    }
    return f;
}
void different_layouts_and_real_continuations() {
    Frame f = layout_frame(); CompetitiveState s;
    f.design = Design::concentric; f.ordinal = 1;
    CHECK(competitive_plan(f, recovery(), s).source == 0);
    f.design = Design::ports; s = {};
    CHECK(competitive_plan(f, recovery(), s).source == 2);
    f.design = Design::forks; f.ordinal = 0; s = {};
    f.sources[3].redundant = true; f.sources[3].alternate_port = 0;
    CHECK(competitive_plan(f, recovery(), s).source == 3);
    f.design = Design::reclamation; s = {}; f.sources[0].progress = .5f;
    CHECK(competitive_plan(f, recovery(), s).source == 0);
    f.sources[0].feasible = false; f.sources[0].incoming = 40; s = {};
    CHECK(competitive_plan(f, recovery(), s).source != 0);
    f = layout_frame(); f.design = Design::counterfront; f.ordinal = 2; f.fresh_pressure = true;
    f.pressure_port = 0; s = {};
    CHECK(competitive_plan(f, recovery(), s).source == 2);
    f.pressure_port = 1; ++f.round;
    CHECK(competitive_plan(f, recovery(), s).source == 1); // A new front can preempt generic retention.
    f = layout_frame(); f.design = Design::migration; s = {};
    CHECK(competitive_plan(f, recovery(), s).source == 1);
    ++f.round; f.captures = 1; f.last_capture_source = 1; f.banks = 1;
    f.sources[2].cycle_turns = 30;
    CHECK(competitive_plan(f, recovery(), s).source == 2);
    f = layout_frame(); f.design = Design::harvest_and_exit; s = {};
    CHECK(competitive_plan(f, recovery(), s).source == 1);
    for (int capture = 1; capture <= 3; ++capture) {
        f.round += 60; f.current = s.source; f.captures = f.banks = capture;
        f.last_capture_source = s.source;
        competitive_plan(f, recovery(), s);
    }
    CHECK(s.exiting && s.source == 0);
}
void executable_metadata_and_banking() {
    Frame f = layout_frame(); CompetitiveState s;
    f.design = Design::sanctuary; f.healing_ready = true; f.ally_count = 1;
    f.allies[0].visible = true; f.allies[0].id = 77; f.allies[0].tile = 55;
    f.allies[0].health = 40; f.allies[0].max_health = 100;
    Task heal = farm(0, true); heal.goal = Goal::guard; heal.heal = true; heal.peer = 77; heal.waypoint = 55;
    const auto healing = competitive_plan(f, heal, s);
    CHECK(healing.goal == Goal::guard && healing.heal && healing.waypoint == 55); // No enemy required.
    f.design = Design::elastic_border; f.fresh_pressure = true;
    Task withdraw = farm(0, true); withdraw.goal = Goal::withdraw;
    CHECK(competitive_plan(f, withdraw, s).goal == Goal::withdraw);
    f.sources[1].joint_feasible = true; f.allies[0].can_attack = true; f.allies[0].group = 1;
    Task rally = farm(1, true); rally.goal = Goal::rally;
    CHECK(competitive_plan(f, rally, s).goal == Goal::rally);
    f.sources[1].occluded = true; f.own_ward_ready = true;
    Task ward = farm(1, true); ward.ward = true;
    CHECK(competitive_plan(f, ward, s).ward);
    f.design = Design::islander; f.bag = 4; f.last_capture_source = 1;
    f.dash_ready = true; f.sources[1].mobility = true;
    Task mobile = farm(1, true); mobile.goal = Goal::bank; mobile.mobility = true; mobile.port = 0;
    const auto exit = competitive_plan(f, mobile, s);
    CHECK(exit.goal == Goal::bank && exit.mobility && exit.source == 1 && exit.port == 0);
    f.design = Design::forks; f.sources[1].redundant = true; f.sources[1].alternate_port = 1;
    mobile.mobility = false; mobile.route = 1; mobile.port = 1;
    const auto alternate = competitive_plan(f, mobile, s);
    CHECK(alternate.goal == Goal::bank && alternate.route == 1 && alternate.source == 1 && alternate.port == 1);
    f = production_frame(); s = {}; f.bag = 4;
    CHECK(competitive_plan(f, recovery(), s).goal == Goal::farm);
    f.bag = p7::live::competitive_bank_target(f.design);
    CHECK(competitive_plan(f, recovery(), s).goal == Goal::bank);
    f.bag = 4; f.windows[1] = 8;
    CHECK(competitive_plan(f, recovery(), s).goal == Goal::bank);
}
void funded_capital_and_bootstrap() {
    Frame f = production_frame(); CompetitiveState s;
    for (int i = 0; i < 3; ++i) f.sources[i].respawning = true;
    f.kit_level = 9;
    CHECK(competitive_plan(f, recovery(), s).source == 3);
    f.kit_level = 10;
    CHECK(competitive_plan(f, recovery(), s).goal == Goal::recover);
    f = production_frame(); s = {}; f.next_purchase_cost = 20; f.gold = 180;
    const auto nine_costs = competitive_plan(f, recovery(), s);
    CHECK(nine_costs.goal == Goal::farm && nine_costs.source == 2);
    f.gold = 199;
    CHECK(competitive_plan(f, recovery(), s).goal == Goal::farm);
    f.gold = 200;
    CHECK(competitive_plan(f, recovery(), s).goal == Goal::recover);
    f.at_spawn = true;
    CHECK(competitive_plan(f, recovery(), s).goal == Goal::farm);
    f.at_spawn = false; f.next_purchase_cost = 0;
    CHECK(competitive_plan(f, recovery(), s).goal == Goal::farm);
}
void funded_carrier_converts_before_shopping() {
    Frame f = production_frame(); CompetitiveState s;
    f.altar_count = f.port_count = 2; f.bag = 4; f.next_purchase_cost = 20; f.gold = 180;
    f.last_capture_source = 1;
    Task conversion = farm(1); conversion.goal = Goal::bank; conversion.port = 0;
    CHECK(competitive_plan(f, conversion, s).goal == Goal::farm);
    f.gold = 200;
    const auto bank = competitive_plan(f, conversion, s);
    CHECK(bank.goal == Goal::bank && bank.source == 1 && bank.port == 0);
    f.sources[1].mobility = true; f.dash_ready = true; conversion.mobility = true;
    CHECK(competitive_plan(f, conversion, s).mobility);
    f.last_capture_source = 2;
    const auto foreign = competitive_plan(f, conversion, s);
    CHECK(foreign.goal == Goal::bank && foreign.source == 2 && !foreign.mobility);
    Task hold = farm(2);
    f.safe_capture = true;
    CHECK(competitive_plan(f, hold, s).goal == Goal::farm);
    f.safe_capture = false; f.channel_active = true;
    CHECK(competitive_plan(f, hold, s).goal == Goal::farm);
    f.channel_active = false; f.bag = 0;
    CHECK(competitive_plan(f, recovery(), s).goal == Goal::recover);
}
void conversion_persists_until_empty() {
    Frame f = production_frame(); CompetitiveState s;
    f.bag = 11; f.converting = true; f.last_capture_source = 1;
    f.altar_count = f.port_count = 2;
    CHECK(f.bag < p7::live::competitive_bank_target(f.design));
    Task conversion = farm(1); conversion.goal = Goal::bank; conversion.port = 1;
    s.source = 2; s.best_distance = 12; s.last_progress = f.round;
    for (int step = 0; step < 10; ++step) {
        ++f.round;
        const auto task = competitive_plan(f, conversion, s);
        CHECK(task.goal == Goal::bank && task.source == 1 && task.port == 1);
        CHECK(s.source == -1 && s.last_progress == -1);
    }
    f.bag = 0;
    CHECK(competitive_plan(f, conversion, s).goal == Goal::farm);
    f.converting = false;
    CHECK(competitive_plan(f, recovery(), s).goal == Goal::farm);

    f.bag = 11; f.converting = true; f.dash_ready = true;
    for (int i : {1, 2}) {
        f.sources[i].mobility = f.sources[i].redundant = true;
        f.sources[i].alternate_port = 1;
    }
    conversion.mobility = true; conversion.route = 1; conversion.mechanism = true;
    const auto owned = competitive_plan(f, conversion, s);
    CHECK(owned.goal == Goal::bank && owned.source == 1 && owned.mobility && owned.route == 1);
    f.last_capture_source = 2;
    const auto foreign = competitive_plan(f, conversion, s);
    CHECK(foreign.goal == Goal::bank && foreign.source == 2 && foreign.port == 1);
    CHECK(!foreign.mobility && foreign.route == 0 && !foreign.mechanism);

    Task hold = farm(0); hold.goal = Goal::rally;
    s.source = 2; f.channel_active = true;
    CHECK(competitive_plan(f, hold, s) == hold && s.source == 2);
    f.channel_active = false; f.safe_capture = true;
    CHECK(competitive_plan(f, hold, s) == hold && s.source == 2);
}
void recovered_carrier_admits_fresh_conversion() {
    Frame f = production_frame(); CompetitiveState s;
    f.at_spawn = true; f.bag = 11; f.last_capture_source = 1;
    CHECK(f.bag < p7::live::competitive_bank_target(f.design));
    s.source = 2; s.best_distance = 12; s.last_progress = f.round;
    Task expected; expected.goal = Goal::bank; expected.source = 1;
    CHECK(competitive_plan(f, recovery(), s) == expected);
    CHECK(s.source == -1 && s.best_distance == -1 && s.last_progress == -1);

    f.health = 99;
    CHECK(competitive_plan(f, recovery(), s).goal == Goal::farm);
    f.health = f.max_health; f.bag = 0;
    CHECK(competitive_plan(f, recovery(), s).goal == Goal::farm);

    f.bag = 11; f.converting = true; f.altar_count = f.port_count = 2;
    f.sources[1].mobility = f.sources[1].redundant = f.sources[1].occluded = true;
    f.sources[1].alternate_port = 1; f.dash_ready = f.own_ward_ready = true;
    Task stale = farm(1, true); stale.goal = Goal::bank; stale.port = 1;
    stale.route = 1; stale.mobility = stale.ward = true;
    CHECK(competitive_plan(f, stale, s) == expected);
    f.last_capture_source = 2; expected.source = 2;
    CHECK(competitive_plan(f, stale, s) == expected);
    f.last_capture_source = -1; expected.source = -1;
    CHECK(competitive_plan(f, stale, s) == expected);

    Task hold = farm(0); hold.goal = Goal::rally;
    s.source = 2; f.channel_active = true;
    CHECK(competitive_plan(f, hold, s) == hold && s.source == 2);
    f.channel_active = false; f.safe_capture = true;
    CHECK(competitive_plan(f, hold, s) == hold && s.source == 2);
}
void project_receipts_are_source_bound() {
    Frame f = layout_frame(); p7::live::State project;
    p7::live::admit(project, f, 1);
    // Generic fallback really captured and banked T, while project S stayed
    // unexecuted. Aggregate counters advance; S's bound receipts do not.
    f.last_capture_source = 0; project.captured[0] = true;
    ++project.captures; ++project.banks;
    CHECK(!p7::live::a_detail::new_payout(project));
    CHECK(!p7::live::a_detail::completed(project));
    CHECK(!p7::live::b_finish_bank(f, project));
    // S's own capture is useful progress, but only S's own bank completes it.
    ++project.captures; project.payout_seen = true;
    CHECK(p7::live::a_detail::new_payout(project));
    CHECK(!p7::live::a_detail::completed(project));
    CHECK(!p7::live::b_finish_bank(f, project));
    ++project.banks; project.bank_seen = true;
    CHECK(p7::live::a_detail::completed(project));
    CHECK(p7::live::b_finish_bank(f, project));
    p7::live::bind_source(project, 0);
    CHECK(!project.payout_seen && !project.bank_seen && !project.bank_pending);
    CHECK(!p7::live::a_detail::completed(project));
    p7::live::admit(project, f, 2);
    ++project.captures; ++project.banks;
    CHECK(!p7::live::a_detail::completed(project));
    CHECK(!p7::live::b_finish_bank(f, project));
}
void bank_exit_is_bound_to_captured_source() {
    Frame f = layout_frame(); f.design = Design::veil; f.bag = 18;
    f.dash_ready = f.own_ward_ready = true;
    for (int i : {1, 2}) {
        f.sources[i].mobility = f.sources[i].redundant = f.sources[i].occluded = true;
        f.sources[i].alternate_port = 1;
    }
    Task proposal = farm(1, true); proposal.goal = Goal::bank; proposal.port = 1;
    proposal.route = 1; proposal.mobility = proposal.ward = true;
    f.last_capture_source = 2;
    const auto foreign = p7::live::competitive_detail::bank_task(f, proposal);
    CHECK(foreign.source == 2 && foreign.port == 1);
    CHECK(foreign.route == 0 && !foreign.mobility && !foreign.ward && !foreign.mechanism);
    f.last_capture_source = 1;
    const auto owned = p7::live::competitive_detail::bank_task(f, proposal);
    CHECK(owned.source == 1 && owned.route == 1 && owned.mobility && owned.ward && owned.mechanism);
    f.last_capture_source = -1;
    const auto unknown = p7::live::competitive_detail::bank_task(f, proposal);
    CHECK(unknown.source == -1 && unknown.route == 0 && !unknown.mobility && !unknown.ward);
}
void continuation_reward_and_spatial_coverage() {
    // Actual 00lanes public camp anchors and home distances. Bottom group20
    // has the same distance/payload as the upper and middle 22-gem groups.
    const std::vector<CompetitiveSource> lanes{
        {30,30,11,6*90+35}, {32,30,11,4*90+35}, {34,30,11,2*90+35}, {36,30,4,13*90+35},
        {38,30,4,15*90+35}, {40,30,4,11*90+35}, {6,34,22,13*90+39}, {8,34,22,4*90+39},
        {20,34,22,25*90+39}, {7,44,22,13*90+49}, {99,-1,2000,0}, {100,70,66,89}
    };
    for (int ordinal = 0; ordinal < 3; ++ordinal) {
        const auto selected = competitive_shortlist(lanes, Design::ports, ordinal, 90);
        contract(lanes, selected);
        for (int id : {30,32,34,36,38,40,20}) CHECK(contains(lanes, selected, id));
        CHECK(lanes[selected[6]].gems == 22 && lanes[selected[7]].gems == 22);
        CHECK(lanes[selected[4]].id == 38 && lanes[selected[5]].id == 40);
        CHECK(!contains(lanes, selected, 99)); CHECK(!contains(lanes, selected, 100));
    }
    const auto legacy = competitive_shortlist(lanes);
    const auto veil = competitive_shortlist(lanes, Design::veil, 0, 90);
    contract(lanes, legacy); contract(lanes, veil);
    CHECK(legacy == veil);
    for (int id : {30,32,34,36,38,40,6,8}) CHECK(contains(lanes, legacy, id));
    CHECK(!contains(lanes, legacy, 20));
    const std::vector<CompetitiveSource> tied{
        {0,1,0}, {1,2,0}, {2,3,0}, {3,4,0}, {20,10,12}, {10,10,12}
    };
    const auto ordered = competitive_shortlist(tied);
    contract(tied, ordered); CHECK(ordered[4] == 5 && ordered[5] == 4);
}
void continuation_roles_use_public_geometry() {
    const std::vector<CompetitiveSource> sources{
        {0,1,0,50*100+50}, {1,1,0,50*100+51}, {2,1,0,51*100+50}, {3,1,0,51*100+51},
        {10,10,22,10*100+30}, {11,10,22,30*100+30}, {12,10,22,50*100+30},
        {13,10,22,70*100+30}, {14,10,22,90*100+30}
    };
    const auto low = competitive_shortlist(sources, Design::ports, 0, 100);
    const auto high = competitive_shortlist(sources, Design::ports, 1, 100);
    contract(sources, low); contract(sources, high);
    CHECK(sources[low[4]].id == 10 && sources[low[5]].id == 11);
    CHECK(low[4] == high[4] && low[5] == high[5]);
    CHECK(sources[low[6]].tile / 100 <= 50);
    CHECK(sources[high[6]].tile / 100 >= 70);
    CHECK(low[6] != high[6]);
    CHECK(low == competitive_shortlist(sources, Design::ports, 0, 100));
}
Frame contested_capture_frame() {
    Frame f = layout_frame(); f.current = 1; f.tile = f.sources[1].tile;
    f.sources[1].reward_available = true; f.sources[1].alive = false;
    f.sources[1].health = f.sources[1].incoming = 0;
    f.capture_enemy = 77; f.capture_enemy_tile = 201;
    f.enemy_count = 1; f.enemies[0].visible = true;
    f.enemies[0].id = 77; f.enemies[0].tile = 201;
    return f;
}
void capture_contest_has_economic_roles() {
    Frame f = contested_capture_frame(); f.health = 60;
    for (Design design : {Design::interior_lines, Design::gatehouse, Design::causeway, Design::counterfront}) {
        f.design = design; CompetitiveState s;
        const auto task = competitive_plan(f, farm(1), s);
        CHECK(task.goal == Goal::guard && task.source == 1);
        CHECK(task.peer == 77 && task.waypoint == 201 && !task.mechanism);
    }
    for (Design design : {Design::frontier, Design::elastic_border, Design::encirclement, Design::veil}) {
        f.design = design; CompetitiveState s;
        const auto task = competitive_plan(f, farm(1), s);
        CHECK(task.goal == Goal::pressure && task.source == 1);
        CHECK(task.peer == 77 && task.waypoint == 201 && !task.mechanism && !f.denial_certificate);
    }
    f.design = Design::ports; CompetitiveState s;
    const auto alternative = competitive_plan(f, farm(1), s);
    CHECK(alternative.source != 1 && alternative.goal == Goal::farm);
    CHECK(s.retry_after[1] == f.round + 32);
    f = contested_capture_frame(); f.design = Design::counterfront; f.health = 59;
    for (int i = 0; i < f.source_count; ++i) if (i != f.current) f.sources[i].respawning = true;
    Task old_pressure = farm(1); old_pressure.goal = Goal::pressure;
    old_pressure.peer = 77; old_pressure.waypoint = 201;
    s = {};
    CHECK(competitive_plan(f, old_pressure, s).goal == Goal::recover);
    CHECK(s.retry_after[1] == f.round + 32 && s.source == -1);
}
void loaded_contest_uses_owned_exit_and_channel_precedence() {
    Frame f = contested_capture_frame(); f.design = Design::forks; f.bag = 11;
    f.last_capture_source = 0;
    f.sources[1].redundant = true; f.sources[1].alternate_port = 1;
    Task proposal = farm(1, true); proposal.goal = Goal::bank; proposal.port = 1; proposal.route = 1;
    CompetitiveState s;
    const auto foreign = competitive_plan(f, proposal, s);
    CHECK(foreign.goal == Goal::bank && foreign.source == 0 && foreign.route == 0 && !foreign.mechanism);
    f.last_capture_source = 1;
    const auto owned = competitive_plan(f, proposal, s);
    CHECK(owned.goal == Goal::bank && owned.source == 1 && owned.route == 1 && owned.port == 1);
    Task channel; channel.goal = Goal::inspect; channel.source = 0;
    f.channel_active = true;
    CHECK(competitive_plan(f, channel, s) == channel);
    f.channel_active = false; f.safe_capture = true;
    CHECK(competitive_plan(f, channel, s) == channel);
}
Frame interior_bootstrap_frame() {
    Frame f; f.design = Design::interior_lines; f.round = 50; f.ordinal = 2; f.team_size = 5;
    f.health = f.max_health = 900; f.damage = 45; f.focus = 90;
    f.source_count = 4; f.port_count = 1; f.home = f.safest = 0; f.tile = 20;
    // The eight-map cheap 1470-HP/11-gem and heavy 7560-HP/59-gem
    // budgets, with ordinary alternative travel/cycle estimates. Only the cheap
    // camp is currently observed feasible; the other proposals earn inspection.
    const float hp[]{1470, 7560, 7560, 5040}, gems[]{11, 59, 59, 37};
    const float incoming[]{20, 180, 180, 120};
    const int home[]{13, 15, 28, 15}, cycle[]{117, 260, 280, 220};
    for (int i = 0; i < f.source_count; ++i) {
        auto& x = f.sources[i]; x.id = 100 + i; x.port = 0; x.tile = 30 + i;
        x.health = hp[i]; x.gems = gems[i]; x.incoming = incoming[i];
        x.distance = x.spawn_distance = home[i]; x.cycle_turns = cycle[i];
        x.reachable = x.home_connected = true;
        for (int j = 0; j < f.source_count; ++j) f.distances[i][j] = std::abs(home[i] - home[j]);
    }
    f.sources[0].seen = f.sources[0].feasible = true;
    return f;
}
Task interior_bootstrap_caller(const Frame& f, p7::live::State& state,
                               CompetitiveState& journey, Task* raw = nullptr) {
    auto proposed = state;
    const Task proposal = p7::live::choose_02(f, proposed);
    if (raw) *raw = proposal;
    const Task final = competitive_plan(f, proposal, journey);
    if (final == proposal) state = proposed;
    return final;
}
void interior_right_bootstraps_before_its_wing() {
    Frame f = interior_bootstrap_frame(); p7::live::State state; CompetitiveState journey;
    Task proposal;
    const Task boot = interior_bootstrap_caller(f, state, journey, &proposal);
    CHECK(proposal.source == 0 && proposal.goal == Goal::farm);
    CHECK(boot == proposal && boot.source == 0 && state.source == 0);
    CHECK(journey.source == 0);
    CHECK(!f.sources[2].seen && !f.sources[2].feasible && f.sources[2].minimum() == 0);
    CHECK(f.sources[0].health == 1470 && f.sources[2].health == 7560);

    // A progressing heavy inspection cannot keep the unfunded right worker cold.
    state = {}; journey = {}; journey.source = 2;
    journey.best_distance = f.sources[2].distance; journey.last_progress = f.round;
    CHECK(interior_bootstrap_caller(f, state, journey).source == 0);

    for (int kit : {9, 10}) {
        f = interior_bootstrap_frame(); f.kit_level = kit; state = {}; journey = {};
        const Task final = interior_bootstrap_caller(f, state, journey, &proposal);
        CHECK(proposal.source == 0);
        CHECK(final.source == (kit < 10 ? 0 : 2));
        CHECK(final.goal == (kit < 10 ? Goal::farm : Goal::inspect));
        CHECK(state.source == (kit < 10 ? 0 : -1));
    }
    // The reserve and left worker retain their pre-existing assignments at kit0.
    for (int ordinal : {0, 1}) {
        f = interior_bootstrap_frame(); f.ordinal = ordinal; state = {}; journey = {};
        const Task final = interior_bootstrap_caller(f, state, journey);
        CHECK(final.source == 1 && final.goal == Goal::inspect);
    }
    f = interior_bootstrap_frame(); f.sources[0].seen = f.sources[0].feasible = false;
    state = {}; journey = {};
    const Task inspect = interior_bootstrap_caller(f, state, journey);
    CHECK(inspect.source == 0 && inspect.goal == Goal::inspect);
    CHECK(!f.sources[0].feasible && f.sources[0].minimum() == 0);
}
void interior_bootstrap_respects_eligibility_and_budget() {
    for (int invalid = 0; invalid < 9; ++invalid) {
        Frame f = interior_bootstrap_frame(); p7::live::State state; CompetitiveState journey;
        auto& x = f.sources[0];
        if (invalid == 0) x.distance = -1;
        if (invalid == 1) x.respawning = true;
        if (invalid == 2) x.cycle_turns = 0;
        if (invalid == 3) x.home_connected = false;
        if (invalid == 4) x.health = 0;
        if (invalid == 5) x.health = std::numeric_limits<float>::quiet_NaN();
        if (invalid == 6) x.incoming = std::numeric_limits<float>::quiet_NaN();
        if (invalid == 7) x.spawn_distance = 40;
        if (invalid == 8) journey.retry_after[0] = f.round + 32;
        CHECK(interior_bootstrap_caller(f, state, journey).source == 2);
    }
    Frame f = interior_bootstrap_frame(); p7::live::State state; CompetitiveState journey;
    f.health = 850; f.sources[0].seen = f.sources[0].feasible = false;
    const Task unavailable = interior_bootstrap_caller(f, state, journey);
    CHECK(unavailable.source == 2 && unavailable.goal == Goal::inspect);
    f = interior_bootstrap_frame(); f.damage = 0; state = {}; journey = {};
    CHECK(interior_bootstrap_caller(f, state, journey).source == 2);

    f = interior_bootstrap_frame(); journey = {}; state = {};
    journey.source = 0; journey.best_distance = f.sources[0].distance;
    journey.last_progress = f.round - 8;
    CHECK(interior_bootstrap_caller(f, state, journey).source == 2);
    CHECK(journey.retry_after[0] == f.round + 32);

    // Owned Jungle protection changes this estimate, never raw group damage.
    f = interior_bootstrap_frame(); f.health = 850; f.camp_damage_factor = .7f;
    state = {}; journey = {};
    CHECK(interior_bootstrap_caller(f, state, journey).source == 0);
    CHECK(f.sources[0].incoming == 20 && f.sources[0].health == 1470);
    f = interior_bootstrap_frame(); f.safe_capture = true; state = {}; journey = {};
    Task proposal;
    const Task hold = interior_bootstrap_caller(f, state, journey, &proposal);
    CHECK(hold == proposal);
    f.safe_capture = false; f.channel_active = true; state = {}; journey = {};
    const Task channel = interior_bootstrap_caller(f, state, journey, &proposal);
    CHECK(channel == proposal);
}
// Actual Islander doctrine -> arbitration -> exact-task state boundary.
Frame islander_local_contest_frame() {
    Frame f = contested_capture_frame(); f.design = Design::islander;
    f.round = 295; f.ordinal = 4; f.team_size = 5; f.home = 0;
    f.health = 980; f.max_health = 1200; f.kit_level = 0;
    // Keep ordinary, known affordable production available. The doctrine offers
    // that alternative; the fresh local capture contest must supersede it.
    for (int i = 0; i < f.source_count; ++i) {
        auto& x = f.sources[i]; x.exit_distance = 4; x.recovery_distance = 5;
        x.home_connected = true; x.score = i == 0 ? std::array<int, 3>{30,30,30} :
                                                                std::array<int, 3>{1,1,1};
    }
    return f;
}
Task islander_local_contest_call(const Frame& f, p7::live::State& state,
                                CompetitiveState& journey, Task& proposal) {
    auto proposed = state;
    proposal = p7::live::choose_04(f, proposed);
    const Task final = competitive_plan(f, proposal, journey);
    if (final == proposal) state = proposed; // Exact production commit boundary.
    return final;
}
void islander_local_contest_actual_caller() {
    for (int witness = 0; witness < 2; ++witness) {
        Frame f = islander_local_contest_frame();
        if (witness) { f.ordinal = 2; f.health = f.max_health = 1000; f.round = 354; }
        p7::live::State state; CompetitiveState journey; Task proposal;
        CHECK(f.sources[0].seen && f.sources[0].feasible && f.sources[0].cycle_turns > 0);
        const Task final = islander_local_contest_call(f, state, journey, proposal);
        CHECK(proposal.goal == Goal::farm && proposal.source == 0 && !proposal.mechanism);
        CHECK(final.goal == Goal::guard && final.source == f.current);
        CHECK(final.peer == f.capture_enemy && final.waypoint == f.capture_enemy_tile);
        CHECK(!final.mechanism && !final.mobility && !final.heal && !final.ward &&
              !final.release_helper && final.route == 0);
        CHECK(journey.source == f.current && journey.retry_after[f.current] == 0);
        CHECK(state.source == -1 && !state.active); // Rejected ordinary project never commits.
        CHECK(f.sources[f.current].health == 0 && f.sources[f.current].reward_available);
        CHECK(f.sources[f.current].payout == 0 && !f.denial_certificate);
    }
}
void islander_local_contest_health_and_freshness() {
    Frame f = islander_local_contest_frame(); f.health = 720; // Exactly60% inclusive.
    p7::live::State state; CompetitiveState journey; Task proposal;
    CHECK(islander_local_contest_call(f, state, journey, proposal).goal == Goal::guard);
    f.health = 719; state = {}; journey = {};
    const Task below = islander_local_contest_call(f, state, journey, proposal);
    CHECK(below.goal != Goal::guard && below.source != f.current);
    CHECK(journey.retry_after[f.current] == f.round + 32);
    for (int boundary = 0; boundary < 5; ++boundary) {
        f = islander_local_contest_frame();
        if (boundary == 0) f.sources[f.current].seen = false;
        if (boundary == 1) f.sources[f.current].reward_available = false;
        if (boundary == 2) f.capture_enemy = -1;
        if (boundary == 3) f.capture_enemy_tile = -1;
        if (boundary == 4) f.current = -1;
        state = {}; journey = {};
        const Task ordinary = islander_local_contest_call(f, state, journey, proposal);
        CHECK(proposal.goal == Goal::farm && proposal.source == 0);
        CHECK(ordinary.goal != Goal::guard && ordinary.goal != Goal::pressure);
        CHECK(!ordinary.mobility && !ordinary.mechanism);
        CHECK(journey.retry_after[1] == 0); // No fresh contest retirement invented.
    }
}
void islander_local_contest_loaded_exit_and_owner_boundaries() {
    for (unsigned bag : {1u, 11u, 18u}) {
        Frame f = islander_local_contest_frame(); f.bag = bag; f.last_capture_source = 1;
        p7::live::State state; state.source = 1; CompetitiveState journey; Task proposal;
        const Task loaded = islander_local_contest_call(f, state, journey, proposal);
        CHECK(proposal.goal == Goal::bank && loaded.goal == Goal::bank && loaded.source == 1);
        CHECK(!loaded.mobility && !loaded.mechanism && journey.retry_after[1] == 0);
    }
    Frame f = islander_local_contest_frame(); f.bag = 1; f.last_capture_source = 1;
    f.sources[1].mobility = true; f.dash_ready = true;
    p7::live::State state; state.source = 1; state.relocated = true;
    CompetitiveState journey; Task proposal;
    const Task exit = islander_local_contest_call(f, state, journey, proposal);
    CHECK(proposal.goal == Goal::bank && proposal.mobility && proposal.mechanism);
    CHECK(exit.goal == Goal::bank && exit.source == 1 && exit.mobility && exit.mechanism);
    // The existing unlocked channel and uncontested-capture precedence stays first.
    for (int owner = 0; owner < 2; ++owner) {
        f = islander_local_contest_frame();
        f.channel_active = owner == 0; f.safe_capture = owner == 1;
        state = {}; journey = {};
        CHECK(islander_local_contest_call(f, state, journey, proposal) == proposal);
        CHECK(journey.source == -1 && journey.retry_after[1] == 0);
    }
}
void islander_local_contest_keeps_funded_mobility() {
    Frame f = islander_local_contest_frame(); f.current = -1;
    f.capture_enemy = f.capture_enemy_tile = -1; f.ordinal = 0;
    f.captures = f.banks = 1; f.dash_ready = true;
    f.sources[2].mobility = true; f.sources[2].score = {60,60,60};
    p7::live::State state; state.captures = state.banks = 1;
    CompetitiveState journey; Task proposal;
    const Task island = islander_local_contest_call(f, state, journey, proposal);
    CHECK(proposal.source == 2 && proposal.goal == Goal::farm && proposal.mobility && proposal.mechanism);
    CHECK(island == proposal && state.relocated && journey.source == 2);
}

Frame concentric_opening_frame() {
    Frame f = interior_bootstrap_frame();
    f.design = Design::concentric; f.ordinal = 0; f.focus = 110;
    for (int i = 0; i < f.source_count; ++i) {
        f.sources[i].ring = i == 2 ? 3 : 1;
        f.sources[i].exit_distance = 8 + i;
        f.sources[i].recovery_distance = f.sources[i].spawn_distance;
    }
    return f;
}
Task concentric_opening_caller(const Frame& f, p7::live::State& state,
                               CompetitiveState& journey, Task* raw = nullptr) {
    auto proposed = state;
    const Task proposal = p7::live::choose_01(f, proposed);
    if (raw) *raw = proposal;
    const Task final = competitive_plan(f, proposal, journey);
    if (final == proposal) state = proposed;
    return final;
}
void concentric_opening_prefers_short_owned_work() {
    Frame f = concentric_opening_frame(); p7::live::State state; CompetitiveState journey;
    Task proposal;
    const Task final = concentric_opening_caller(f, state, journey, &proposal);
    CHECK(proposal.source == 0 && proposal.goal == Goal::farm);
    CHECK(final == proposal && state.source == 0 && state.home_source == 0);
    CHECK(!state.payout_seen && !state.bank_seen && state.captures == 0 && state.banks == 0);
    CHECK(!f.sources[1].seen && !f.sources[1].feasible && f.sources[1].minimum() == 0);

    state = {}; journey = {}; journey.source = 1;
    journey.best_distance = f.sources[1].distance; journey.last_progress = f.round;
    CHECK(concentric_opening_caller(f, state, journey).source == 0);
    for (int kit : {4, 5}) {
        f = concentric_opening_frame(); f.kit_level = kit; f.focus += 12 * kit; state = {}; journey = {};
        const Task selected = concentric_opening_caller(f, state, journey, &proposal);
        CHECK(proposal.source == 0);
        CHECK(selected.source == (kit < 5 ? 0 : 1));
        CHECK(selected.goal == (kit < 5 ? Goal::farm : Goal::inspect));
        CHECK(state.source == (kit < 5 ? 0 : -1));
    }
    for (int ordinal : {1, 2, 3, 4}) {
        f = concentric_opening_frame(); f.ordinal = ordinal; state = {}; journey = {};
        const Task selected = concentric_opening_caller(f, state, journey);
        CHECK(selected.source == 1 && selected.goal == Goal::inspect);
    }
    // Actual funding restores the existing personally inspected next-ring path.
    f = concentric_opening_frame(); f.kit_level = 5; f.focus += 60;
    f.captures = 4; f.banks = 1; f.gold = 10; f.last_capture_source = 0;
    state = {}; journey = {}; state.source = state.home_source = 0; state.ring = 1;
    state.captures = 4; state.banks = 1; state.payout_seen = state.bank_seen = true;
    const Task ring = concentric_opening_caller(f, state, journey, &proposal);
    CHECK(proposal.source == 2 && proposal.goal == Goal::inspect);
    CHECK(ring == proposal && state.source == 2);
    CHECK(!f.sources[2].feasible && f.sources[2].minimum() == 0);

    f = concentric_opening_frame(); f.sources[0].seen = f.sources[0].feasible = false;
    state = {}; journey = {};
    const Task unknown = concentric_opening_caller(f, state, journey);
    CHECK(unknown.source == 0 && unknown.goal == Goal::inspect);
    CHECK(!f.sources[0].feasible && f.sources[0].health == 1470);
}
void concentric_opening_keeps_unavailable_fallback() {
    for (int invalid = 0; invalid < 8; ++invalid) {
        Frame f = concentric_opening_frame(); p7::live::State state; CompetitiveState journey;
        auto& x = f.sources[0];
        if (invalid == 0) x.distance = -1;
        if (invalid == 1) x.respawning = true;
        if (invalid == 2) x.cycle_turns = 0;
        if (invalid == 3) x.home_connected = false;
        if (invalid == 4) x.health = 0;
        if (invalid == 5) x.health = std::numeric_limits<float>::quiet_NaN();
        if (invalid == 6) x.incoming = std::numeric_limits<float>::quiet_NaN();
        if (invalid == 7) journey.retry_after[0] = f.round + 32;
        CHECK(concentric_opening_caller(f, state, journey).source == 1);
    }
    Frame f = concentric_opening_frame(); p7::live::State state; CompetitiveState journey;
    f.health = 850; f.sources[0].seen = f.sources[0].feasible = false;
    CHECK(concentric_opening_caller(f, state, journey).source == 1);
    f = concentric_opening_frame(); state = {}; journey = {};
    journey.source = 0; journey.best_distance = f.sources[0].distance;
    journey.last_progress = f.round - 8;
    CHECK(concentric_opening_caller(f, state, journey).source == 1);
    CHECK(journey.retry_after[0] == f.round + 32);

    f = concentric_opening_frame(); f.safe_capture = true; state = {}; journey = {}; Task proposal;
    const Task hold = concentric_opening_caller(f, state, journey, &proposal);
    CHECK(hold == proposal);
    f.safe_capture = false; f.channel_active = true; state = {}; journey = {};
    const Task channel = concentric_opening_caller(f, state, journey, &proposal);
    CHECK(channel == proposal);
}

Frame forks_opening_frame() {
    Frame f; f.design = Design::forks; f.round = 80; f.ordinal = 3; f.team_size = 5;
    f.tile = 50; f.home = 0; f.health = f.max_health = 650; f.damage = 30; f.focus = 70;
    f.port_count = f.altar_count = 1; f.source_count = 3;
    // Stellar's ordinary high-reward proposal competes with a farther, smaller
    // home camp. Public work ranks the opening without certifying a solo clear.
    for (int i = 0; i < f.source_count; ++i) {
        auto& x = f.sources[i]; x.id = 100 + i; x.tile = 200 + i; x.port = 0;
        x.distance = i == 0 ? 8 : 4; x.exit_distance = x.recovery_distance = 5;
        x.spawn_distance = i == 0 ? 4 : 20; x.cycle_turns = i == 0 ? 60 : 50;
        x.reachable = x.seen = x.alive = x.home_connected = true;
        x.health = i == 0 ? 1470 : 5040; x.incoming = i == 0 ? 20 : 120;
        x.gems = i == 0 ? 11 : 37; x.gold = i == 0 ? 40 : 200;
        x.score = i == 0 ? std::array<int, 3>{10, 10, 10} : std::array<int, 3>{30, 30, 30};
        for (int j = 0; j < f.source_count; ++j) f.distances[i][j] = i == j ? 0 : 8;
    }
    f.sources[2].home_connected = false; f.sources[2].cycle_turns = 100;
    return f;
}
Task forks_opening_caller(const Frame& f, p7::live::State& state,
                          CompetitiveState& journey, Task* raw = nullptr) {
    auto proposed = state;
    const Task proposal = p7::live::choose_05(f, proposed);
    if (raw) *raw = proposal;
    const Task final = competitive_plan(f, proposal, journey);
    if (final == proposal) state = proposed;
    return final;
}
void forks_unfunded_worker_starts_small() {
    for (bool feasible : {false, true}) {
        Frame f = forks_opening_frame();
        f.sources[0].feasible = f.sources[1].feasible = feasible;
        p7::live::State state; CompetitiveState journey; Task proposal;
        const Task final = forks_opening_caller(f, state, journey, &proposal);
        CHECK(proposal.goal == (feasible ? Goal::farm : Goal::recover));
        if (feasible) CHECK(proposal.source == 1);
        CHECK(final.goal == Goal::farm && final.source == 0);
        CHECK(!final.mechanism && !final.mobility && !final.heal && !final.ward &&
              !final.release_helper && final.route == 0);
        CHECK(journey.source == 0 && state.source == -1 && !state.active);
        CHECK(f.sources[0].feasible == feasible && !f.sources[0].joint_feasible &&
              f.sources[0].payout == 0 && f.sources[0].health == 1470);
    }
}
void forks_opening_respects_work_and_eligibility() {
    // The nearer heavy camp cannot erase the smaller job's work advantage;
    // excessive travel can. Neither choice borrows future spells or ally damage.
    Frame f = forks_opening_frame(); p7::live::State state; CompetitiveState journey;
    CHECK(forks_opening_caller(f, state, journey).source == 0);
    f.sources[0].distance = 200; state = {}; journey = {};
    CHECK(forks_opening_caller(f, state, journey).source == 1);

    f = forks_opening_frame(); state = {}; journey = {};
    f.sources[0].feasible = f.sources[1].feasible = true;
    f.sources[2].health = 0; f.sources[2].reward_available = true; f.sources[2].distance = 0;
    CHECK(forks_opening_caller(f, state, journey).source == 0); // Disconnected reward is not an opener.
    for (int unavailable = 0; unavailable < 4; ++unavailable) {
        f = forks_opening_frame(); state = {}; journey = {};
        if (unavailable == 0) journey.retry_after[0] = f.round + 1;
        if (unavailable == 1) f.sources[0].respawning = true;
        if (unavailable == 2) f.sources[0].distance = -1;
        if (unavailable == 3) f.sources[0].home_connected = false;
        CHECK(forks_opening_caller(f, state, journey).source == 1);
    }
    f = forks_opening_frame(); state = {}; journey = {};
    journey.retry_after[0] = f.round + 1;
    CHECK(forks_opening_caller(f, state, journey).source == 1);
    ++f.round; state = {};
    CHECK(forks_opening_caller(f, state, journey).source == 0);

    f = forks_opening_frame(); state = {}; journey = {};
    f.sources[0].reward_available = true; f.sources[0].health = 0; f.sources[0].distance = 20;
    f.sources[1].health = 1200; f.sources[1].distance = 1;
    CHECK(forks_opening_caller(f, state, journey).source == 0); // Cleared reward needs no basic work.
    f = forks_opening_frame(); state = {}; journey = {};
    for (int i = 0; i < f.source_count; ++i) f.sources[i].home_connected = false;
    CHECK(forks_opening_caller(f, state, journey).source == 1); // Ordinary fallback remains available.
    f = forks_opening_frame(); state = {}; journey = {};
    for (int i = 0; i < f.source_count; ++i) f.sources[i].respawning = true;
    CHECK(forks_opening_caller(f, state, journey).goal == Goal::recover);
}
void forks_opening_stops_at_kit_five_and_preserves_roles() {
    for (int kit : {4, 5, 10}) {
        Frame f = forks_opening_frame(); f.kit_level = kit;
        f.sources[0].feasible = f.sources[1].feasible = true;
        p7::live::State state; CompetitiveState journey;
        CHECK(forks_opening_caller(f, state, journey).source == (kit < 5 ? 0 : 1));
    }
    for (int ordinal : {0, 1, 2, 4}) {
        Frame f = forks_opening_frame(); f.ordinal = ordinal;
        for (int i = 0; i < f.source_count; ++i) f.sources[i].feasible = true;
        f.port_count = f.altar_count = 2;
        f.sources[1].redundant = true; f.sources[1].alternate_port = 1;
        f.captures = f.banks = 1;
        p7::live::State state; state.captures = state.banks = 1;
        CompetitiveState journey; Task proposal;
        const Task final = forks_opening_caller(f, state, journey, &proposal);
        CHECK(proposal.source == (ordinal == 2 ? 2 : 1));
        CHECK(final.source == proposal.source && final.goal == Goal::farm);
    }
}
void forks_opening_preserves_owner_and_loaded_exit() {
    for (int owner = 0; owner < 2; ++owner) {
        Frame f = forks_opening_frame(); f.sources[1].feasible = true;
        f.channel_active = owner == 0; f.safe_capture = owner == 1;
        p7::live::State state; CompetitiveState journey; Task proposal;
        CHECK(forks_opening_caller(f, state, journey, &proposal) == proposal);
        CHECK(journey.source == -1);
    }
    Frame f = forks_opening_frame(); f.bag = 20; f.last_capture_source = 1; f.fresh_pressure = true;
    f.port_count = f.altar_count = 2;
    f.sources[1].redundant = f.sources[1].pressured = true; f.sources[1].alternate_port = 1;
    p7::live::State state; state.source = 1; state.relocated = true; state.port = 0;
    CompetitiveState journey; Task proposal;
    const Task final = forks_opening_caller(f, state, journey, &proposal);
    CHECK(final == proposal && final.goal == Goal::bank && final.source == 1);
    CHECK(final.route == 1 && final.port == 1 && final.mechanism);
}

} // namespace

int main() {
    islander_local_contest_actual_caller();
    islander_local_contest_health_and_freshness();
    islander_local_contest_loaded_exit_and_owner_boundaries();
    islander_local_contest_keeps_funded_mobility();
    progression_before_maximum_reward();
    ties_and_reachable_fill();
    unreachable_and_empty();
    collision_progression();
    production_ranking_and_geography();
    long_progress_and_blocked_retirement();
    preserve_capture_channel_bag_and_contact();
    inspection_and_owned_health();
    owned_jungle_health_mitigation();
    affordable_before_seen_partial_work();
    interior_right_bootstraps_before_its_wing();
    interior_bootstrap_respects_eligibility_and_budget();
    concentric_opening_prefers_short_owned_work();
    concentric_opening_keeps_unavailable_fallback();
    stale_proposals_and_safe_fallback();
    soft_role_affinity();
    different_layouts_and_real_continuations();
    executable_metadata_and_banking();
    funded_capital_and_bootstrap();
    funded_carrier_converts_before_shopping();
    conversion_persists_until_empty();
    recovered_carrier_admits_fresh_conversion();
    project_receipts_are_source_bound();
    bank_exit_is_bound_to_captured_source();
    continuation_reward_and_spatial_coverage();
    continuation_roles_use_public_geometry();
    capture_contest_has_economic_roles();
    loaded_contest_uses_owned_exit_and_channel_precedence();
    forks_unfunded_worker_starts_small();
    forks_opening_respects_work_and_eligibility();
    forks_opening_stops_at_kit_five_and_preserves_roles();
    forks_opening_preserves_owner_and_loaded_exit();
    std::printf("plan7 competitive: %d checks passed\n", checks);
}
