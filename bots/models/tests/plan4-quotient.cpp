// Pure relational first-macro + physical-continuation tests. No SDK input or engine witnesses.
#include "../quotient/src/quotient.h"
#include <cassert>
#include <cstdio>
#include <memory>

using namespace plan4;
using namespace plan4::quotient;
namespace {
int metric(Point a, Point b, const void*) { return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y)); }
struct Fixture {
    std::unique_ptr<Information> info = std::make_unique<Information>();
    std::array<std::array<Member, 4>, 4> members{};
    std::array<Composition, 4> composition{};
    View view{};
    Menu menu{};
    Fixture() {
        auto& i = *info; i.round = 10; i.own.id = 3;
        i.own.health = i.own.maximum_health = 1000; i.own.bag = 2;
        i.group_count = 2; i.altar_count = 1; i.travel = {metric, nullptr};
        i.altars[0].id = 90; i.altars[0].position = {5, 0}; i.altars[0].altar_position = {5, 1};
        i.altars[0].lock_known = true;
        for (int g = 0; g < 2; ++g) {
            auto& group = i.groups[g];
            group.id = g == 0 ? 30 : 10; group.position = {g == 0 ? 1 : 4, 0};
            group.health = g == 0 ? 26 : 49; group.full_health = 100;
            group.clear_rounds = g == 0 ? 3 : 5; group.full_clear_rounds = 10;
            group.gems = 8; group.gold = 4; group.respawn_rounds = 50;
            group.lifecycle = Lifecycle::living; group.visible = group.all_spawns_visible = group.occupancy_known = true;
            group.provenance = group.health_provenance = {10, Source::personal_vision, Status::exact_now};
            members[g][0] = {0, group.position, 100, group.health, group.health_provenance};
            composition[g] = {members[g].data(), 1, true};
            Job job; job.kind = JobKind::farm; job.group = g; job.altar = 0; job.nominal_travel = 5;
            menu.jobs[g] = job;
        }
        menu.count = menu.single_count = 2;
        view.groups = composition.data(); view.count = 2;
        view.kit.champion_kind = 1; view.kit.attack_range = 1; view.kit.strength = 10; view.kit.focus = 2;
    }
};
struct Output {
    std::unique_ptr<Evaluation> evaluation = std::make_unique<Evaluation>();
    std::array<Endpoint, max_jobs> endpoints{};
    Statistics statistics{};
};
std::unique_ptr<Output> evaluate(Quotient& model, Fixture& fixture, Allowance allowance = {}) {
    auto out = std::make_unique<Output>();
    out->statistics = model.evaluate_first_macro(*fixture.info, fixture.menu, fixture.view, *out->evaluation, out->endpoints, allowance);
    return out;
}
std::unique_ptr<Output> evaluate_full(Quotient& model, Fixture& fixture, Allowance allowance = {}) {
    auto out = std::make_unique<Output>();
    out->statistics = model.evaluate_full(*fixture.info, fixture.menu, fixture.view, *out->evaluation, out->endpoints, allowance);
    return out;
}
bool equal(const Forecast& a, const Forecast& b) {
    if (a.event_count != b.event_count || a.macros_started != b.macros_started || a.final_round != b.final_round ||
        a.first_conversion_round != b.first_conversion_round || a.our_secured != b.our_secured || a.enemy_secured != b.enemy_secured ||
        a.final_bag != b.final_bag || a.final_gold != b.final_gold || a.final_health != b.final_health || a.terminal_z != b.terminal_z ||
        a.terminal_unknown != b.terminal_unknown || a.stock_unknown != b.stock_unknown || a.travel_unknown != b.travel_unknown ||
        a.lock_unknown != b.lock_unknown || a.truncated != b.truncated || a.completed_conversion != b.completed_conversion ||
        a.no_conversion != b.no_conversion || a.budget_exhausted != b.budget_exhausted || a.abort_reason != b.abort_reason) return false;
    for (int e = 0; e < a.event_count; ++e) {
        const auto& x = a.events[e]; const auto& y = b.events[e];
        if (x.kind != y.kind || x.start.round != y.start.round || x.start.phase != y.start.phase ||
            x.end.round != y.end.round || x.end.phase != y.end.phase || x.group_id != y.group_id ||
            x.bag_before != y.bag_before || x.bag_after != y.bag_after || x.secured != y.secured ||
            x.gold_after != y.gold_after || x.health_after != y.health_after || x.status != y.status) return false;
    }
    return true;
}

void exact_features_and_declared_buckets() {
    auto f = std::make_unique<Fixture>(); auto& i = *f->info;
    auto left = extract_features(i, f->menu.jobs[0], f->view), right = extract_features(i, f->menu.jobs[1], f->view);
    assert(left.supported && right.supported && left.key == right.key);
    assert(left.to_camp == 1 && right.to_camp == 4 && left.to_altar == 4 && right.to_altar == 1);
    assert(distance_bin(0) == 0 && distance_bin(1) == 1 && distance_bin(4) == 1 && distance_bin(5) == 2 && distance_bin(-1) == -1);
    assert(health_bucket(0, 100) == 0 && health_bucket(25, 100) == 1 && health_bucket(26, 100) == 2);
    assert(health_bucket(100, 100) == 4 && health_bucket(101, 100) == -1);
    const auto original = left.key;
    ++i.own.bag; assert(!(extract_features(i, f->menu.jobs[0], f->view).key == original)); --i.own.bag;
    --i.own.health; assert(!(extract_features(i, f->menu.jobs[0], f->view).key == original)); ++i.own.health;
    ++i.settings.capture_rounds; assert(!(extract_features(i, f->menu.jobs[0], f->view).key == original)); --i.settings.capture_rounds;
    ++i.settings.winning_gems; assert(!(extract_features(i, f->menu.jobs[0], f->view).key == original)); --i.settings.winning_gems;
    ++f->view.kit.strength; assert(!(extract_features(i, f->menu.jobs[0], f->view).key == original)); --f->view.kit.strength;
    ++i.groups[0].gems; assert(!(extract_features(i, f->menu.jobs[0], f->view).key == original)); --i.groups[0].gems;
    ++i.groups[0].eligible_allied_occupants; assert(!(extract_features(i, f->menu.jobs[0], f->view).key == original)); --i.groups[0].eligible_allied_occupants;
    i.groups[0].health = f->members[0][0].health = 25;
    assert(!(extract_features(i, f->menu.jobs[0], f->view).key == original));
    i.groups[0].health = f->members[0][0].health = 26;
    i.groups[0].provenance.observed_round = 9;
    assert(!extract_features(i, f->menu.jobs[0], f->view).supported);
}

void repeated_types_use_spawn_order_and_no_hidden_health() {
    auto f = std::make_unique<Fixture>(); auto& i = *f->info;
    for (int g = 0; g < 2; ++g) {
        const auto p = i.groups[g].position;
        f->members[g][0] = {2, p, 50, 12, i.groups[g].health_provenance};
        f->members[g][1] = {2, {p.x + 1, p.y}, 50, 26, i.groups[g].health_provenance};
        f->composition[g].count = 2; i.groups[g].health = 38; i.groups[g].clear_rounds = 4;
    }
    std::swap(f->members[1][0], f->members[1][1]);
    const auto a = extract_features(i, f->menu.jobs[0], f->view), b = extract_features(i, f->menu.jobs[1], f->view);
    assert(a.supported && b.supported && a.key == b.key); // Input enumeration order is irrelevant.
    std::swap(f->members[1][0].health, f->members[1][1].health);
    assert(!(a.key == extract_features(i, f->menu.jobs[1], f->view).key));
    f->members[0][0].health_provenance.status = Status::unknown;
    assert(!extract_features(i, f->menu.jobs[0], f->view).supported); // Exact aggregate cannot hide a missing member proof.
    i.groups[0].health_provenance.status = Status::unknown;
    const auto unknown = extract_features(i, f->menu.jobs[0], f->view);
    assert(unknown.supported && !(unknown.key == a.key)); // Unknown member uses full-health bucket, never zero.
    assert(unknown.health == i.groups[0].full_health); // Conservative public upper bound, never stale aggregate health.
    f->composition[0].complete_public_layout = false;
    assert(!extract_features(i, f->menu.jobs[0], f->view).supported);
}

void conservative_maxima_cache_and_physical_endpoints() {
    auto f = std::make_unique<Fixture>();
    auto active_storage = std::make_unique<Quotient>(true), control_storage = std::make_unique<Quotient>(false);
    auto& active = *active_storage; auto& control = *control_storage;
    auto a = evaluate(active, *f), c = evaluate(control, *f);
    assert(a->statistics.classes == 1 && c->statistics.classes == 1);
    assert(a->statistics.forecasts_computed == 1 && a->statistics.cache_hits == 1);
    assert(c->statistics.forecasts_computed == 2 && c->statistics.cache_hits == 0 && c->statistics.exact_jobs == 2);
    for (int n = 0; n < 2; ++n) {
        const auto& forecast = a->evaluation->forecasts[n];
        assert(forecast.first_conversion_round == 30 && forecast.our_secured == 10 && forecast.macros_started == 1);
        assert(forecast.first_conversion_round >= c->evaluation->forecasts[n].first_conversion_round);
        for (int e = 0; e < forecast.event_count; ++e)
            assert(forecast.events[e].group_id < 0 || forecast.events[e].group_id == f->info->groups[n].id);
        assert(a->endpoints[n].first_group_id == f->info->groups[n].id);
        assert(a->endpoints[n].camp == f->info->groups[n].position);
        assert(a->endpoints[n].altar_id == 90 && a->endpoints[n].final_position == f->info->altars[0].position);
        assert(a->endpoints[n].final_position_known);
        Forecast exact; predict_into(*f->info, f->menu.jobs[n], Menu{}, exact);
        assert(equal(c->evaluation->forecasts[n], exact));
    }
    assert(c->evaluation->forecasts[0].first_conversion_round == 25 && c->evaluation->forecasts[1].first_conversion_round == 27);
}

void deadline_alias_is_conservative_but_can_lose_a_real_opportunity() {
    auto f = std::make_unique<Fixture>(); f->info->settings.max_rounds = 26;
    auto active_storage = std::make_unique<Quotient>(true), control_storage = std::make_unique<Quotient>(false);
    auto& active = *active_storage; auto& control = *control_storage;
    auto a = evaluate(active, *f), c = evaluate(control, *f);
    assert(a->evaluation->forecasts[0].our_secured == 0 && a->evaluation->forecasts[1].our_secured == 0);
    assert(c->evaluation->forecasts[0].our_secured == 10 && c->evaluation->forecasts[1].our_secured == 0);
    assert(c->evaluation->chosen == 0);
    assert(!a->endpoints[0].final_position_known); // Never claim a completed bank endpoint at cutoff.
    f->info->settings.winning_gems = 10;
    a = evaluate(active, *f); c = evaluate(control, *f);
    assert(a->evaluation->forecasts[0].terminal_z == 0 && a->evaluation->forecasts[0].terminal_unknown);
    assert(c->evaluation->forecasts[0].terminal_z == 1);
}

void ordered_two_camp_jobs_are_exact_and_uncached() {
    auto f = std::make_unique<Fixture>(); f->menu.single_count = 0;
    f->menu.jobs[0].second_group = 1; f->menu.jobs[1].second_group = 0;
    auto active_storage = std::make_unique<Quotient>(true), control_storage = std::make_unique<Quotient>(false);
    auto& active = *active_storage; auto& control = *control_storage;
    auto a = evaluate(active, *f), c = evaluate(control, *f);
    assert(a->statistics.classes == 0 && a->statistics.cache_hits == 0 && a->statistics.exact_jobs == 2);
    for (int n = 0; n < 2; ++n) {
        Forecast exact; predict_into(*f->info, f->menu.jobs[n], Menu{}, exact);
        assert(equal(a->evaluation->forecasts[n], exact) && equal(c->evaluation->forecasts[n], exact));
        assert(a->endpoints[n].second_group_id == f->info->groups[f->menu.jobs[n].second_group].id);
    }
}

void add_strict_winner(Fixture& f) {
    auto& i = *f.info; i.group_count = f.view.count = 3;
    i.groups[2] = i.groups[0]; i.groups[2].id = 200; i.groups[2].position = {2, 2}; i.groups[2].gems = 20;
    f.members[2][0] = {0, {2, 2}, 100, i.groups[2].health, i.groups[2].health_provenance};
    f.composition[2] = {f.members[2].data(), 1, true};
    f.menu.jobs[2] = f.menu.jobs[0]; f.menu.jobs[2].group = 2;
    f.menu.count = f.menu.single_count = 3;
}
void reflection_and_id_array_permutation_preserve_strict_decisions() {
    auto f = std::make_unique<Fixture>(); add_strict_winner(*f);
    auto active_storage = std::make_unique<Quotient>(); auto& active = *active_storage;
    auto original = evaluate(active, *f);
    assert(original->evaluation->chosen == 2 && original->evaluation->forecasts[2].our_secured == 22);
    const auto chosen_position = original->endpoints[2].camp;
    for (int g = 0; g < 3; ++g) {
        f->info->groups[g].position.x *= -1;
        for (int m = 0; m < f->composition[g].count; ++m) f->members[g][m].spawn.x *= -1;
    }
    f->info->altars[0].position.x *= -1; f->info->altars[0].altar_position.x *= -1;
    auto reflected = evaluate(active, *f); const int selected = reflected->evaluation->chosen;
    assert(reflected->endpoints[selected].camp == Point({-chosen_position.x, chosen_position.y}));
    assert(reflected->statistics.classes == original->statistics.classes && reflected->statistics.cache_hits == original->statistics.cache_hits);
    for (int g = 0; g < 3; ++g) f->info->groups[g].id = 500 - g * 100;
    auto relabeled = evaluate(active, *f);
    assert(relabeled->endpoints[relabeled->evaluation->chosen].camp == reflected->endpoints[selected].camp);
    std::swap(f->info->groups[0], f->info->groups[2]); std::swap(f->composition[0], f->composition[2]);
    for (int n = 0; n < 3; ++n) {
        if (f->menu.jobs[n].group == 0) f->menu.jobs[n].group = 2;
        else if (f->menu.jobs[n].group == 2) f->menu.jobs[n].group = 0;
    }
    std::swap(f->menu.jobs[0], f->menu.jobs[2]);
    auto reordered = evaluate(active, *f);
    assert(reordered->endpoints[reordered->evaluation->chosen].camp == reflected->endpoints[selected].camp);
}

void every_batch_invalidates_changed_bag_settings_health_and_stats() {
    auto f = std::make_unique<Fixture>(); auto active_storage = std::make_unique<Quotient>(); auto& active = *active_storage;
    auto original = evaluate(active, *f); assert(original->statistics.forecasts_computed == 1);
    f->info->own.bag = 25;
    auto bag = evaluate(active, *f); assert(bag->statistics.forecasts_computed == 1 && bag->evaluation->forecasts[0].our_secured == 33);
    f->info->settings.tribute_multiplier = 0.2;
    auto setting = evaluate(active, *f); assert(setting->evaluation->forecasts[0].first_conversion_round > bag->evaluation->forecasts[0].first_conversion_round);
    ++f->view.kit.version; ++f->view.kit.focus;
    auto kit = evaluate(active, *f); assert(kit->statistics.forecasts_computed == 1);
    f->info->groups[1].health = f->members[1][0].health = 75; f->info->groups[1].clear_rounds = 8;
    auto hp = evaluate(active, *f); assert(hp->statistics.classes == 2 && hp->statistics.forecasts_computed == 2 && hp->statistics.cache_hits == 0);
}

void full_feature_capacity_unknown_kit_and_gold_only_endpoint() {
    auto f = std::make_unique<Fixture>(); std::array<Member, member_limit> members;
    for (int n = 0; n < member_limit; ++n)
        members[n] = {n % 4, {n, 3}, 100, 1, {10, Source::personal_vision, Status::exact_now}};
    f->composition[0] = {members.data(), member_limit, true};
    f->info->groups[0].full_health = 3200; f->info->groups[0].health = 32;
    f->info->groups[0].clear_rounds = 4; f->info->groups[0].full_clear_rounds = 320;
    const auto maximum = extract_features(*f->info, f->menu.jobs[0], f->view);
    assert(maximum.supported && maximum.key.count < key_limit);
    f->composition[0].count = member_limit + 1;
    assert(!extract_features(*f->info, f->menu.jobs[0], f->view).supported);
    f = std::make_unique<Fixture>(); f->view.kit.champion_kind = -1;
    assert(!extract_features(*f->info, f->menu.jobs[0], f->view).supported);
    f->view.kit.champion_kind = 1; f->info->own.bag = 0;
    f->info->groups[0].gems = f->info->groups[1].gems = 0;
    auto model = std::make_unique<Quotient>(); auto out = evaluate(*model, *f);
    assert(out->statistics.cache_hits == 1);
    for (int n = 0; n < 2; ++n) {
        const auto& forecast = out->evaluation->forecasts[n];
        assert(forecast.no_conversion && !forecast.completed_conversion && forecast.our_secured == 0);
        assert(forecast.final_gold == 4 && out->endpoints[n].final_position_known);
        for (int e = 0; e < forecast.event_count; ++e) assert(forecast.events[e].kind != EventKind::bank_start && forecast.events[e].kind != EventKind::bank);
    }
}

struct Gate { bool open = true; };
bool allowed(const void* context) { return static_cast<const Gate*>(context)->open; }
bool one_completed_prefix(const void* context) { return static_cast<const Evaluation*>(context)->evaluated_count < 1; }
void identical_budget_cap_and_no_partial_cache_publication() {
    auto f = std::make_unique<Fixture>();
    auto active_storage = std::make_unique<Quotient>(true), control_storage = std::make_unique<Quotient>(false);
    auto& active = *active_storage; auto& control = *control_storage;
    auto a = evaluate(active, *f, {1}), c = evaluate(control, *f, {1});
    assert(a->evaluation->evaluated_count == 2 && c->evaluation->evaluated_count == 1);
    assert(a->statistics.forecasts_computed == 1 && c->statistics.forecasts_computed == 1);
    assert(!a->statistics.budget_exhausted && c->statistics.budget_exhausted);
    a = evaluate(active, *f, {0}); c = evaluate(control, *f, {0});
    assert(a->evaluation->evaluated_count == 0 && c->evaluation->evaluated_count == 0);
    Gate gate{false}; f->info->work_budget = {allowed, &gate};
    a = evaluate(active, *f);
    assert(a->statistics.preparation_incomplete && a->statistics.budget_exhausted && a->evaluation->evaluated_count == 0);
    gate.open = true; a = evaluate(active, *f);
    assert(a->statistics.forecasts_computed == 1 && a->statistics.cache_hits == 1);
    // The same fuel-reserve gate also stops an otherwise ready cache hit.
    auto prefix = std::make_unique<Output>(); f->info->work_budget = {one_completed_prefix, prefix->evaluation.get()};
    prefix->statistics = active.evaluate_first_macro(*f->info, f->menu, f->view, *prefix->evaluation, prefix->endpoints);
    assert(prefix->evaluation->evaluated_count == 1 && prefix->statistics.budget_exhausted && prefix->statistics.cache_hits == 0);
    f->info->work_budget = {};
    f->info->own.channel = Channel::tribute; f->info->own.channel_ticks = 2;
    a = evaluate(active, *f); c = evaluate(control, *f);
    assert(a->statistics.classes == 0 && a->statistics.exact_jobs == 2);
    assert(equal(a->evaluation->forecasts[0], c->evaluation->forecasts[0]));
}

struct TravelBounds { int to_camp = 0, to_altar = 0; };
int bounded_travel(EventKind kind, int, int nominal, int, int, const Information&, const void* model, void* path) {
    if (kind != EventKind::travel) return nominal;
    const auto& bounds = *static_cast<const TravelBounds*>(model); auto& leg = *static_cast<int*>(path);
    return std::max(nominal, leg++ == 0 ? bounds.to_camp : bounds.to_altar);
}
void manual_full(Fixture& fixture, int index, Forecast& out) {
    auto representative = std::make_unique<Information>(*fixture.info);
    const auto& actual = *fixture.info; const auto& job = fixture.menu.jobs[index];
    const auto feature = extract_features(actual, job, fixture.view);
    TravelBounds travel{feature.to_camp, feature.to_altar}; int leg = 0, clear = feature.clear_rounds;
    float health = feature.health;
    for (int n = 0; n < fixture.menu.count; ++n) {
        const auto other = extract_features(actual, fixture.menu.jobs[n], fixture.view);
        if (other.supported && other.key == feature.key) {
            travel.to_camp = std::max(travel.to_camp, other.to_camp); travel.to_altar = std::max(travel.to_altar, other.to_altar);
            clear = std::max(clear, other.clear_rounds); health = std::max(health, other.health);
        }
    }
    representative->groups[job.group].clear_rounds = clear;
    representative->groups[job.group].health = health;
    ForecastHooks hooks; hooks.phase_duration = bounded_travel; hooks.model = &travel; hooks.path = &leg;
    auto first = std::make_unique<Forecast>(); auto state = std::make_unique<SimulationState>();
    simulate_first_into(*representative, job, fixture.menu, *first, *state, hooks);
    assert(continue_from_into(actual, fixture.menu, *first, *state, out));
}
void full_control_is_exact_and_active_rebases_only_the_first_camp() {
    auto f = std::make_unique<Fixture>();
    auto active = std::make_unique<Quotient>(true), control = std::make_unique<Quotient>(false);
    auto a = evaluate_full(*active, *f), c = evaluate_full(*control, *f);
    assert(!a->statistics.first_macro_only && a->statistics.full_paths == 2 && a->statistics.rebased_paths == 2);
    assert(a->statistics.forecasts_computed == 1 && a->statistics.cache_hits == 1 && !a->statistics.rebase_fallbacks);
    assert(c->statistics.full_paths == 2 && c->statistics.exact_jobs == 2 && !c->statistics.rebased_paths);
    for (int n = 0; n < 2; ++n) {
        auto exact = std::make_unique<Forecast>(), manual = std::make_unique<Forecast>();
        predict_into(*f->info, f->menu.jobs[n], f->menu, *exact);
        assert(equal(c->evaluation->forecasts[n], *exact));
        manual_full(*f, n, *manual);
        assert(equal(a->evaluation->forecasts[n], *manual));
        assert(a->evaluation->forecasts[n].macros_started > 1 && a->endpoints[n].final_position_known);
    }
}
void full_two_camp_remains_exact_and_never_uses_a_pair_cache() {
    auto f = std::make_unique<Fixture>(); f->menu.jobs[2] = f->menu.jobs[0];
    f->menu.jobs[2].second_group = 1; f->menu.count = 3;
    auto active = std::make_unique<Quotient>(true), control = std::make_unique<Quotient>(false);
    auto a = evaluate_full(*active, *f), c = evaluate_full(*control, *f);
    auto exact = std::make_unique<Forecast>(); predict_into(*f->info, f->menu.jobs[2], f->menu, *exact);
    assert(equal(a->evaluation->forecasts[2], *exact) && equal(c->evaluation->forecasts[2], *exact));
    assert(a->statistics.exact_jobs == 1 && a->statistics.full_paths == 3 && a->statistics.cache_hits == 1);
    assert(a->endpoints[2].first_group_id == 30 && a->endpoints[2].second_group_id == 10);
}
void full_paths_reflect_and_source_arrays_permute_without_changing_the_fixed_menu() {
    auto f = std::make_unique<Fixture>(); add_strict_winner(*f); auto active = std::make_unique<Quotient>();
    auto original = evaluate_full(*active, *f); assert(original->evaluation->chosen == 2);
    for (int g = 0; g < 3; ++g) {
        f->info->groups[g].position.x *= -1;
        for (int m = 0; m < f->composition[g].count; ++m) f->members[g][m].spawn.x *= -1;
    }
    f->info->altars[0].position.x *= -1; f->info->altars[0].altar_position.x *= -1;
    auto reflected = evaluate_full(*active, *f); assert(reflected->evaluation->chosen == 2);
    for (int n = 0; n < 3; ++n) {
        assert(equal(original->evaluation->forecasts[n], reflected->evaluation->forecasts[n]));
        assert(reflected->endpoints[n].final_position == Point({-original->endpoints[n].final_position.x, original->endpoints[n].final_position.y}));
        f->info->groups[n].id = 500 - n * 100;
    }
    auto relabeled = evaluate_full(*active, *f); assert(relabeled->evaluation->chosen == 2);
    const auto winning_position = relabeled->endpoints[2].camp;
    std::swap(f->info->groups[0], f->info->groups[2]); std::swap(f->composition[0], f->composition[2]);
    for (int n = 0; n < 3; ++n) {
        if (f->menu.jobs[n].group == 0) f->menu.jobs[n].group = 2;
        else if (f->menu.jobs[n].group == 2) f->menu.jobs[n].group = 0;
    }
    // Physical source enumeration changes; the frozen physical menu order does not.
    auto permuted = evaluate_full(*active, *f); assert(permuted->endpoints[permuted->evaluation->chosen].camp == winning_position);
    for (int n = 0; n < 3; ++n) assert(equal(relabeled->evaluation->forecasts[n], permuted->evaluation->forecasts[n]));
}
void distinct_physical_altar_tails_are_not_cached() {
    auto f = std::make_unique<Fixture>();
    f->info->altar_count = 2; f->info->altars[1] = f->info->altars[0];
    f->info->altars[1].id = 91; f->info->altars[1].position = {-5, 0}; f->info->altars[1].altar_position = {-5, 1};
    f->info->groups[1].position = {-1, 0}; f->members[1][0].spawn = {-1, 0}; f->menu.jobs[1].altar = 1;
    auto active = std::make_unique<Quotient>(); auto a = evaluate_full(*active, *f);
    assert(a->statistics.classes == 1 && a->statistics.cache_hits == 1 && a->statistics.rebased_paths == 2);
    for (int n = 0; n < 2; ++n) {
        auto manual = std::make_unique<Forecast>(); manual_full(*f, n, *manual);
        assert(equal(a->evaluation->forecasts[n], *manual));
    }
    assert(a->endpoints[0].altar != a->endpoints[1].altar);
    const auto capture_round = [](const Forecast& forecast, int group) {
        for (int e = 0; e < forecast.event_count; ++e)
            if (forecast.events[e].kind == EventKind::capture && forecast.events[e].group_id == group) return forecast.events[e].end.round;
        return -1;
    };
    const auto before = capture_round(a->evaluation->forecasts[1], 30);
    f->info->groups[0].position.y = 30; f->members[0][0].spawn.y = 30;
    auto changed = evaluate_full(*active, *f);
    assert(changed->statistics.classes == 2 && before >= 0 && capture_round(changed->evaluation->forecasts[1], 30) > before);
}
void rebasing_rejects_false_stock_prefixes_stale_sources_and_ambiguous_endpoints() {
    auto f = std::make_unique<Fixture>(); auto normalized_info = std::make_unique<Information>(*f->info);
    normalized_info->group_count = normalized_info->altar_count = 1;
    normalized_info->groups[0].id = normalized_info->altars[0].id = 0;
    normalized_info->groups[0].clear_rounds = 5; normalized_info->groups[0].health = 49;
    Job single = f->menu.jobs[0]; single.group = single.altar = 0;
    auto first = std::make_unique<Forecast>(), scratch = std::make_unique<Forecast>();
    auto normalized = std::make_unique<SimulationState>(), baseline = std::make_unique<SimulationState>(), output = std::make_unique<SimulationState>();
    simulate_first_into(*normalized_info, single, Menu{}, *first, *normalized);
    for (int e = 0; e < first->event_count; ++e) if (first->events[e].group_id == 0) first->events[e].group_id = f->info->groups[1].id;
    const Frame frame{normalized_info->own.position, normalized_info->groups[0].position, normalized_info->altars[0].position, normalized_info->own_spawn};
    assert(initialize_state_into(*f->info, f->menu, *scratch, *baseline));
    const auto rebase = [&] { return rebase_single_into(*f->info, f->menu.jobs[1], *first, *normalized, frame, *baseline, *output); };
    assert(rebase() == Rebase::supported);
    assert(output->groups[0].lifecycle == Lifecycle::living && output->groups[1].lifecycle == Lifecycle::captured);
    assert(output->groups[1].ready_round == normalized->groups[0].ready_round && output->position == f->info->altars[0].position);
    const int ready = normalized->groups[0].ready_round; normalized->groups[0].ready_round = 0;
    assert(rebase() == Rebase::invalid_lifecycle); normalized->groups[0].ready_round = ready;
    ++first->final_bag; assert(rebase() == Rebase::invalid_prefix); --first->final_bag;
    baseline->groups[0].lifecycle = Lifecycle::captured; assert(rebase() == Rebase::invalid_candidate);
    baseline->groups[0].lifecycle = Lifecycle::living;
    ++normalized->source_round; assert(rebase() == Rebase::invalid_candidate); --normalized->source_round;
    const auto position = normalized->position; normalized->position = {999, 999};
    assert(rebase() == Rebase::invalid_endpoint); normalized->position = position;
    auto ambiguous = frame; ambiguous.camp = ambiguous.altar;
    assert(rebase_single_into(*f->info, f->menu.jobs[1], *first, *normalized, ambiguous, *baseline, *output) == Rebase::invalid_endpoint);
    assert(rebase() == Rebase::supported);
    ++f->info->groups[0].id;
    assert(!continue_from_into(*f->info, f->menu, *first, *output, *scratch)); // Common binding remains the final hard gate.
}
void full_path_quota_deadline_and_missing_composition_are_explicit() {
    auto f = std::make_unique<Fixture>(); auto active = std::make_unique<Quotient>(), control = std::make_unique<Quotient>(false);
    auto a = evaluate_full(*active, *f, {max_jobs, 1}), c = evaluate_full(*control, *f, {max_jobs, 1});
    assert(a->statistics.full_paths == 1 && c->statistics.full_paths == 1 && a->evaluation->evaluated_count == 1 && c->evaluation->evaluated_count == 1);
    assert(a->statistics.budget_exhausted && c->statistics.budget_exhausted);
    f->view.groups = nullptr;
    a = evaluate_full(*active, *f); c = evaluate_full(*control, *f);
    assert(a->statistics.classes == 0 && a->statistics.unsupported_features == 2 && a->statistics.exact_jobs == 2);
    assert(equal(a->evaluation->forecasts[0], c->evaluation->forecasts[0]));
    f = std::make_unique<Fixture>(); f->info->settings.max_rounds = 26;
    a = evaluate_full(*active, *f); c = evaluate_full(*control, *f);
    assert(a->statistics.full_paths == 2 && a->evaluation->forecasts[0].our_secured == 0 && c->evaluation->forecasts[0].our_secured == 10);
    assert(a->evaluation->forecasts[0].terminal_unknown);
}
}
int main() {
    exact_features_and_declared_buckets();
    repeated_types_use_spawn_order_and_no_hidden_health();
    conservative_maxima_cache_and_physical_endpoints();
    deadline_alias_is_conservative_but_can_lose_a_real_opportunity();
    ordered_two_camp_jobs_are_exact_and_uncached();
    reflection_and_id_array_permutation_preserve_strict_decisions();
    every_batch_invalidates_changed_bag_settings_health_and_stats();
    full_feature_capacity_unknown_kit_and_gold_only_endpoint();
    identical_budget_cap_and_no_partial_cache_publication();
    full_control_is_exact_and_active_rebases_only_the_first_camp();
    full_two_camp_remains_exact_and_never_uses_a_pair_cache();
    full_paths_reflect_and_source_arrays_permute_without_changing_the_fixed_menu();
    distinct_physical_altar_tails_are_not_cached();
    rebasing_rejects_false_stock_prefixes_stale_sources_and_ambiguous_endpoints();
    full_path_quota_deadline_and_missing_composition_are_explicit();
    std::puts("plan4 quotient: 15 pure scenarios passed (full physical continuation; native behavior/fuel validation pending)");
}
