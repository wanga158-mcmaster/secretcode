// Deterministic public-terrain/SDK mock witness, not an engine simulation.
// Actual OnlineEvidence, OnlineObserver, PublicNavigation, natural make_menu,
// Habitat adapter and common evaluator remain production code.
#include "../common/online-evidence.h"
#include "../common/observer.h"
#include "../common/nav.h"
#include "../habitat/src/runtime.h"
#include <cassert>
#include <cstdio>
#include <memory>
namespace p = plan4;
namespace rt = plan4::runtime;
namespace cm = cambc::mock;

struct Entry { cambc::map::camp_group group; cambc::vec2 anchor; };
struct Fixture {
    std::vector<Entry> entries;
    std::vector<int> lookup = std::vector<int>(2500, -1);
    rt::OnlineEvidence evidence;
    p::OnlineObserver observer;
    p::PublicNavigation navigation;
    rt::Habitat active{true}, zero_consumers{false};
    std::unique_ptr<p::Information> information = std::make_unique<p::Information>();

    explicit Fixture(bool opposed) {
        cm::reset();
        cm::query_cost = 1; // Query-count doubles do not represent native fuel.
        cm::actor(0).hp = cm::actor(0).maximum = 1000;
        cm::actor(0).strength = 100;
        // The first frame starts on a living target's spawn. A legal adjacent
        // observed step in the second frame leaves that spawn. Existing route
        // reuse then retains both natural ordered-pair quotes.
        cm::actor(0).position = {2, 0};
        cm::allied_spawns = {{0, 0}};
        cm::altar_positions = {{6, 1}};
        add({{2, 0}}, 100);
        add({{2, 2}, {3, 2}, {4, 2}}, 0);
        cambc::map::mock_groups.clear();
        for (const auto& entry : entries) cambc::map::mock_groups.push_back(entry.group);
        cm::states[0] = 0; // Living target.
        cm::states[1] = 1; // Dead members, unclaimed reward available.
        cm::actors.push_back({1, {2, 2}, true, true, cambc::team::ally});
        cm::actors.push_back({2, {3, 2}, true, true, cambc::team::ally});
        if (opposed) cm::actors.push_back({3, {4, 2}, true, true, cambc::team::enemy});
        active.observe_kit(42); zero_consumers.observe_kit(42);
    }
    void add(std::vector<cambc::vec2> points, float hp) {
        const int index = int(entries.size());
        std::vector<cambc::camp> members;
        for (const auto point : points) {
            members.emplace_back(index * 100 + int(members.size()), point, hp);
            lookup[cm::tile(point)] = index;
            cm::visible[cm::tile(point)] = true;
        }
        entries.push_back({cambc::map::camp_group{index, std::move(members)}, points.front()});
    }
    void frame(int round, bool opposed) {
        cm::round = round; cm::fuel = 100000;
        cm::actor(0).position = round == 10 ? cambc::vec2{2, 0} : cambc::vec2{1, 0};
        navigation.begin_turn();
        // Same production ordering: online producers arrive before the later
        // planning snapshot. No manually created ProducerFrame is supplied.
        evidence.start(entries, lookup, 50, 0);
        const auto producers = evidence.producers();
        assert(producers.clock.round == round && producers.clock.own.id == 0);
        assert(producers.count == (opposed ? 3 : 2));
        for (int n = 0; n < producers.count; ++n) {
            const auto& sample = producers.samples[n];
            assert(sample.group_id == 1 && sample.personally_visible && sample.producing);
            assert(sample.ambiguous == opposed);
            assert(sample.provenance.observed_round == round);
        }
        active.observe_lifecycles(evidence.lifecycles());
        zero_consumers.observe_lifecycles(evidence.lifecycles());
        active.observe_producers(evidence.producers());
        zero_consumers.observe_producers(evidence.producers());
        auto& info = *information;
        assert(observer.observe_into(info, navigation.lookup(), 45000));
        assert(!info.observation_truncated && !info.public_layout_truncated);
        assert(info.group_count == 2 && info.altar_count == 1);
        navigation.prepare(info, 45000, 25000);
        // Resolve immutable route fields through ordinary bounded preparation.
        // Keep the production vertex allowance unchanged.
        assert(navigation.initialized());
        active.observe_snapshot(info, evidence.members());
        zero_consumers.observe_snapshot(info, evidence.members());
        evidence.finish(entries);
        assert(!cm::unauthorized);
    }
};

bool same_forecast(const p::Forecast& a, const p::Forecast& b) {
    if (a.event_count != b.event_count || a.macros_started != b.macros_started ||
        a.final_round != b.final_round || a.first_conversion_round != b.first_conversion_round ||
        a.our_secured != b.our_secured || a.enemy_secured != b.enemy_secured ||
        a.final_bag != b.final_bag || a.final_gold != b.final_gold || a.final_health != b.final_health ||
        a.terminal_z != b.terminal_z || a.terminal_unknown != b.terminal_unknown ||
        a.stock_unknown != b.stock_unknown || a.travel_unknown != b.travel_unknown ||
        a.lock_unknown != b.lock_unknown || a.truncated != b.truncated ||
        a.completed_conversion != b.completed_conversion || a.no_conversion != b.no_conversion ||
        a.budget_exhausted != b.budget_exhausted || a.abort_reason != b.abort_reason ||
        a.objective.paths != b.objective.paths || a.objective.terminal_z != b.objective.terminal_z ||
        a.objective.our_secured != b.objective.our_secured || a.objective.enemy_secured != b.objective.enemy_secured ||
        a.objective.first_conversion_round != b.objective.first_conversion_round) return false;
    for (int n = 0; n < a.event_count; ++n) {
        const auto& x = a.events[n]; const auto& y = b.events[n];
        if (x.kind != y.kind || x.start.round != y.start.round || x.start.phase != y.start.phase ||
            x.end.round != y.end.round || x.end.phase != y.end.phase || x.group_id != y.group_id ||
            x.bag_before != y.bag_before || x.bag_after != y.bag_after || x.secured != y.secured ||
            x.gold_after != y.gold_after || x.health_after != y.health_after || x.status != y.status) return false;
    }
    return true;
}

void run_case(bool opposed) {
    auto fixture = std::make_unique<Fixture>(opposed);
    fixture->frame(10, opposed);
    fixture->frame(11, opposed);
    auto& info = *fixture->information;
    const auto& active_state = fixture->active.state();
    assert(active_state.supported && !active_state.unknown_geometry);
    assert(active_state.patch[0] == 0 && active_state.patch[1] == 0 && active_state.capacity[0] > 0);
    const auto pressure = p::habitat::pressure(active_state, 0, info.round);
    assert(pressure.observed_consumers == (opposed ? 0 : 2));
    assert(pressure.unknown); // Bounded provider never proves whole-patch absence.
    const auto menu = p::make_menu(info);
    std::printf("menu case=%s truncated=%d singles=%d count=%d pairs=%zu pending=%d vertices=%d\n",
        opposed ? "opposed" : "visible-holders", menu.truncated, menu.single_count, menu.count,
        fixture->navigation.completed_pairs(), fixture->navigation.pending_fields(), fixture->navigation.vertex_visits());
    std::fflush(stdout);
    assert(!menu.truncated && menu.single_count == 2 && menu.count == 7);
    int ordered_pairs = 0;
    for (int n = 0; n < menu.count; ++n) ordered_pairs += menu.jobs[n].second_group >= 0;
    assert(ordered_pairs == 2); // Both naturally admitted directions survive.
    auto active = std::make_unique<p::Evaluation>();
    auto zero = std::make_unique<p::Evaluation>();
    rt::evaluate_model(info, menu, fixture->active, *active);
    rt::evaluate_model(info, menu, fixture->zero_consumers, *zero);
    assert(!active->budget_exhausted && !zero->budget_exhausted);
    assert(active->evaluated_count == menu.count && zero->evaluated_count == menu.count);
    const auto active_evidence = fixture->active.model_evidence();
    const auto zero_evidence = fixture->zero_consumers.model_evidence();
    assert(active_evidence.supported && zero_evidence.supported);
    assert(active_evidence.samples == (opposed ? 0 : 2) && active_evidence.paths > 0 && active_evidence.unknown > 0);
    assert(!zero_evidence.coupling_enabled);
    int changed = 0;
    for (int n = 0; n < menu.count; ++n) {
        changed += !same_forecast(active->forecasts[n], zero->forecasts[n]);
        std::printf("case=%s job=%d kind=%d group=%d second=%d active_secured=%u zero_secured=%u active_final=%d zero_final=%d\n",
            opposed ? "opposed" : "visible-holders", n, int(menu.jobs[n].kind), menu.jobs[n].group,
            menu.jobs[n].second_group, active->forecasts[n].our_secured, zero->forecasts[n].our_secured,
            active->forecasts[n].final_round, zero->forecasts[n].final_round);
    }
    std::fflush(stdout);
    assert(opposed ? changed == 0 : changed > 0);
    assert(active_evidence.coupling_enabled == !opposed);
    std::printf("{\"case\":\"%s\",\"sdkFrames\":2,\"menu\":%d,\"singles\":%d,\"consumers\":%d,\"capacitySupported\":true,\"changedForecasts\":%d,\"activeChoice\":%d,\"zeroChoice\":%d,\"engineWitnesses\":0}\n",
        opposed ? "opposed" : "visible-holders", menu.count, menu.single_count,
        pressure.observed_consumers, changed, active->chosen, zero->chosen);
}

// A second caller witness changes only public geometry and personally observed
// history. The SDK double compresses max_rounds to 80: frames 19/20 represent
// the final 61/60 rounds, rather than claiming current engine balance or timing.
// The one 7/21/14 variant uses the final 65 rounds and an earlier SDK route
// warmup. It is retained as a rejected flip candidate: exact readiness absorbs
// pressure and the full natural menu selects a local pair in both arms.
struct RelocationFixture {
    bool single_target_variant;
    bool continuous;
    std::vector<Entry> entries;
    std::vector<int> lookup = std::vector<int>(2500, -1);
    rt::OnlineEvidence evidence;
    p::OnlineObserver observer;
    p::PublicNavigation navigation;
    rt::Habitat active{true}, zero_consumers{false};
    std::unique_ptr<p::Information> information = std::make_unique<p::Information>();

    explicit RelocationFixture(bool continuous_history, bool variant = false)
        : single_target_variant(variant), continuous(continuous_history) {
        cm::reset();
        cm::current_gem_camps = true;
        // Opt in only here: current gem clusters have 120 Health, no attack,
        // seven gems each and a 50-round respawn. Other cases keep their doubles.
        cm::query_cost = 1;
        cm::actor(0).hp = cm::actor(0).maximum = 1000;
        cm::actor(0).strength = 100;
        cm::actor(0).position = {2, 1};
        cm::allied_spawns = {{0, 0}};
        cm::altar_positions = {{0, 2}, {48, 2}};
        // A small open local room joins a one-tile corridor. The remote altar
        // is reached from the corridor, without turning spawn tiles into walls.
        for (int y = 0; y < 50; ++y) for (int x = 0; x < 50; ++x) {
            // The ONE variant descends at x19, crosses y5 and returns at x26.
            // Four diagonal corner cuts leave four extra shortest-path rounds.
            const bool corridor = !single_target_variant ? y == 1 :
                (y == 1 && (x <= 19 || x >= 26)) ||
                ((x == 19 || x == 26) && y >= 1 && y <= 5) ||
                (y == 5 && x >= 19 && x <= 26);
            if (!((x <= 8 && y <= 3) || corridor || (x == 48 && y == 2)))
                cm::wall_positions.push_back({x, y});
        }
        if (single_target_variant) add({{2, 1}}, 0);
        else add({{2, 1}, {2, 2}}, 0);
        add({{4, 1}, {4, 2}, {5, 2}}, 0);
        if (single_target_variant) add({{46, 1}, {47, 1}}, 120);
        else add({{46, 1}}, 120);
        cambc::map::mock_groups.clear();
        for (const auto& entry : entries) cambc::map::mock_groups.push_back(entry.group);
        cm::actors.push_back({1, continuous ? cambc::vec2{4, 1} : cambc::vec2{3, 0}, true, true, cambc::team::ally});
        cm::actors.push_back({2, continuous ? cambc::vec2{4, 2} : cambc::vec2{3, 1}, true, true, cambc::team::ally});
        cm::actors.push_back({3, single_target_variant ? cambc::vec2{3, 2} : cambc::vec2{2, 2}, true, true, cambc::team::ally});
        active.observe_kit(42); zero_consumers.observe_kit(42);
    }
    void add(std::vector<cambc::vec2> points, float hp) {
        const int index = int(entries.size());
        std::vector<cambc::camp> members;
        for (const auto point : points) {
            members.emplace_back(index * 100 + int(members.size()), point, hp, 0, cambc::camp_type::gem_cluster);
            lookup[cm::tile(point)] = index;
        }
        entries.push_back({cambc::map::camp_group{index, std::move(members)}, points.front()});
    }
    void personal_visibility() {
        cm::visible.fill(false);
        const auto own = cm::actor(0).position;
        // This is a conservative ray-aligned subset of own sight plus ONE own
        // ward at (46,1), not global or allied vision. A prior remote placement
        // followed by recall can supply this view; placement/lifetime are not
        // simulated or qualified by this SDK mock.
        for (int x = 0; x < 50; ++x) {
            if (std::abs(x - own.x) <= 8 || std::abs(x - 46) <= 6)
                cm::visible[cm::tile({x, 1})] = true;
        }
        for (const auto point : std::array<cambc::vec2, 9>{{
                {0, 0}, {0, 2}, {2, 2}, {3, 0}, {3, 1}, {3, 2}, {4, 2}, {5, 2}, {48, 2}}})
            cm::visible[cm::tile(point)] = true;
        assert(!cm::visible[cm::tile({25, 1})]);
    }
    int decision_round() const { return single_target_variant ? 15 : 20; }
    void frame(int round) {
        cm::round = round; cm::fuel = 100000;
        const bool warmup = single_target_variant && round == 5;
        const bool preceding = round == decision_round() - 1;
        cm::actor(0).position = warmup || (!single_target_variant && preceding)
            ? cambc::vec2{2, 1} : cambc::vec2{1, 1};
        // Another ally completes the local capture between these exact owner
        // boundaries, then leaves. We do not invent a 50->50 countdown or any
        // receipt saying this actor received its reward.
        cm::states[0] = warmup ? 0 : preceding ? 1 : 2;
        cm::states[1] = warmup ? 0 : 1;
        cm::states[2] = 0;
        cm::camp_health_overrides.clear();
        if (warmup) for (const int member : {0, 100, 101, 102})
            cm::camp_health_overrides.push_back({member, 120});
        const bool holding = !warmup && (continuous || !preceding);
        cm::actor(1).position = holding ? cambc::vec2{4, 1} : cambc::vec2{3, 0};
        cm::actor(2).position = holding ? cambc::vec2{4, 2} : cambc::vec2{3, 1};
        cm::actor(3).position = preceding ? (single_target_variant ? cambc::vec2{2, 1} : cambc::vec2{2, 2})
            : cambc::vec2{3, 2};
        personal_visibility();
        navigation.begin_turn();
        evidence.start(entries, lookup, 50, 0);
        assert(evidence.producers().count == 3);
        if (round == decision_round()) {
            assert(evidence.capture_count() == 1);
            const auto& capture = evidence.capture(0);
            assert(capture.supported && capture.receipt.group_id == 0 && capture.receipt.round == round);
            assert(capture.receipt.event_status == p::Status::derived && !capture.reward_attribution_known);
        }
        active.observe_lifecycles(evidence.lifecycles());
        zero_consumers.observe_lifecycles(evidence.lifecycles());
        active.observe_producers(evidence.producers());
        zero_consumers.observe_producers(evidence.producers());
        auto& info = *information;
        assert(observer.observe_into(info, navigation.lookup(), 45000));
        assert(!info.observation_truncated && !info.public_layout_truncated);
        assert(info.group_count == 3 && info.altar_count == 2);
        navigation.prepare(info, 45000, 25000);
        assert(navigation.initialized());
        active.observe_snapshot(info, evidence.members());
        zero_consumers.observe_snapshot(info, evidence.members());
        if (warmup || (!single_target_variant && preceding)) {
            // The original two-cluster history leaves while ally 3 holds its
            // other spawn. The variant warms routes on a LIVING single cluster
            // at round 5, before ally 3 later clears and holds it through 14.
            cm::actor(0).position = {1, 1};
            personal_visibility();
        }
        evidence.finish(entries);
        assert(!cm::unauthorized);
    }
};

struct RelocationResult {
    p::Menu menu;
    std::unique_ptr<p::Evaluation> active = std::make_unique<p::Evaluation>();
    std::unique_ptr<p::Evaluation> zero = std::make_unique<p::Evaluation>();
};

std::unique_ptr<RelocationResult> run_relocation_case(bool continuous, bool single_target_variant) {
    auto fixture = std::make_unique<RelocationFixture>(continuous, single_target_variant);
    if (single_target_variant) fixture->frame(5);
    fixture->frame(fixture->decision_round() - 1);
    fixture->frame(fixture->decision_round());
    const auto& info = *fixture->information;
    const int remaining = single_target_variant ? 65 : 60;
    assert(p::horizon(info) == remaining && !info.own.bag && info.own.alive);
    assert(info.groups[0].gems == (single_target_variant ? 7u : 14u) && info.groups[1].gems == 21);
    assert(info.groups[2].gems == (single_target_variant ? 14u : 7u));
    for (const auto& group : std::span{info.groups.data(), std::size_t(info.group_count)})
        assert(group.gold == 0 && group.respawn_rounds == 50 && group.full_incoming_damage == 0);
    assert(info.groups[0].visible && info.groups[0].provenance.status == p::Status::exact_now);
    assert(info.groups[0].lifecycle == p::Lifecycle::respawning && info.groups[0].respawn_remaining == 50);
    assert(info.groups[0].occupancy_known && !info.groups[0].occupied_spawn);
    assert(info.groups[2].visible && info.groups[2].lifecycle == p::Lifecycle::living);
    assert(info.groups[2].health_provenance.status == p::Status::exact_now);
    const auto& state = fixture->active.state();
    assert(state.supported && !state.unknown_geometry);
    assert(state.patch[0] == state.patch[1] && state.patch[0] != state.patch[2]);
    const auto pressure = p::habitat::pressure(state, state.patch[0], info.round);
    assert(pressure.observed_consumers == (continuous ? 2 : 0));
    assert(p::habitat::pressure(state, state.patch[2], info.round).observed_consumers == 0);
    auto result = std::make_unique<RelocationResult>();
    result->menu = p::make_menu(info);
    const auto& menu = result->menu;
    std::printf("relocation-menu variant=%s case=%s truncated=%d singles=%d count=%d pending=%d vertices=%d\n",
        single_target_variant ? "single-target" : "no-flip", continuous ? "continuous-holders" : "new-holders", menu.truncated, menu.single_count, menu.count,
        fixture->navigation.pending_fields(), fixture->navigation.vertex_visits());
    std::fflush(stdout);
    assert(!menu.truncated && menu.single_count == 3);
    rt::evaluate_model(info, menu, fixture->active, *result->active);
    rt::evaluate_model(info, menu, fixture->zero_consumers, *result->zero);
    const auto& active = *result->active;
    const auto& zero = *result->zero;
    assert(!active.budget_exhausted && !zero.budget_exhausted);
    assert(active.evaluated_count == menu.count && zero.evaluated_count == menu.count);
    assert(active.chosen >= 0 && zero.chosen >= 0);
    const auto active_evidence = fixture->active.model_evidence();
    const auto zero_evidence = fixture->zero_consumers.model_evidence();
    assert(active_evidence.supported && zero_evidence.supported);
    assert(active_evidence.samples == (continuous ? 2 : 0) && !zero_evidence.coupling_enabled);
    int changed = 0;
    for (int n = 0; n < menu.count; ++n) {
        changed += !same_forecast(active.forecasts[n], zero.forecasts[n]);
        std::printf("relocation variant=%s case=%s job=%d kind=%d group=%d second=%d active_secured=%u zero_secured=%u active_first=%d zero_first=%d\n",
            single_target_variant ? "single-target" : "no-flip", continuous ? "continuous-holders" : "new-holders", n, int(menu.jobs[n].kind), menu.jobs[n].group,
            menu.jobs[n].second_group, active.forecasts[n].our_secured, zero.forecasts[n].our_secured,
            active.forecasts[n].first_conversion_round, zero.forecasts[n].first_conversion_round);
    }
    std::fflush(stdout);
    if (continuous) {
        const auto& selected = menu.jobs[active.chosen];
        const auto& control = menu.jobs[zero.chosen];
        assert(selected.kind == p::JobKind::farm && control.kind == p::JobKind::farm);
        if (single_target_variant) {
            assert(changed == 0 && !active_evidence.coupling_enabled && active.chosen == zero.chosen);
            assert(selected.second_group >= 0 && state.patch[selected.group] == state.patch[0]);
            assert(state.patch[selected.second_group] == state.patch[0]);
        } else {
            // The legal 14/21/7 counterexample is retained: observable pressure
            // changes forecasts without making relocation the selected action.
            assert(changed > 0 && active_evidence.coupling_enabled);
            assert(active.chosen == zero.chosen && state.patch[selected.group] == state.patch[0]);
        }
        assert(active.forecasts[active.chosen].our_secured > 0 && active.forecasts[active.chosen].completed_conversion);
        assert(zero.forecasts[zero.chosen].our_secured > 0 && zero.forecasts[zero.chosen].completed_conversion);
    } else {
        assert(changed == 0 && !active_evidence.coupling_enabled && active.chosen == zero.chosen);
    }
    std::printf("{\"case\":\"relocation-%s-%s\",\"sdkFrames\":%d,\"remainingRounds\":%d,\"menu\":%d,\"singles\":%d,\"consumers\":%d,\"changedForecasts\":%d,\"activeChoice\":%d,\"zeroChoice\":%d,\"engineWitnesses\":0}\n",
        single_target_variant ? "single-target" : "no-flip", continuous ? "continuous-holders" : "new-holders",
        single_target_variant ? 3 : 2, remaining, menu.count, menu.single_count,
        pressure.observed_consumers, changed, active.chosen, zero.chosen);
    return result;
}

void relocation_continuity_witness(bool single_target_variant) {
    const auto positive = run_relocation_case(true, single_target_variant);
    const auto negative = run_relocation_case(false, single_target_variant);
    // Current SDK state, natural jobs, control objectives and complete event
    // streams agree. Only whether the holders produced in the preceding frame
    // differs; no pressure frame or candidate job is supplied by the test.
    assert(positive->menu.count == negative->menu.count);
    assert(positive->menu.single_count == negative->menu.single_count);
    for (int n = 0; n < positive->menu.count; ++n) {
        const auto& a = positive->menu.jobs[n]; const auto& b = negative->menu.jobs[n];
        assert(a.kind == b.kind && a.group == b.group && a.second_group == b.second_group && a.altar == b.altar);
        assert(a.menu_rank == b.menu_rank && a.nominal_travel == b.nominal_travel && a.nominal_completion == b.nominal_completion);
        assert(same_forecast(positive->zero->forecasts[n], negative->zero->forecasts[n]));
    }
    assert(positive->zero->chosen == negative->zero->chosen);
}

int main() {
    run_case(false);
    run_case(true);
    relocation_continuity_witness(false);
    relocation_continuity_witness(true);
    std::puts("{\"schema\":\"plan4-habitat-sdk-caller/1\",\"passed\":true,\"cases\":6,\"engineWitnesses\":0}");
}
