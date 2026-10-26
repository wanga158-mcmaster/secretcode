#include "../habitat/src/habitat.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace plan4;
namespace h = plan4::habitat;
namespace {
int checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
bool near(double a, double b) { return std::abs(a - b) < 1e-10; }
int travel(Point a, Point b, const void*) { return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y)); }
int unknown_travel(Point, Point, const void*) { return -1; }
void fixture(Information& info, bool second_patch = false) {
    info = Information{};
    info.round = 10; info.own.id = 1; info.own.position = {0, 0}; info.own_spawn = {0, 0};
    info.own.health = info.own.maximum_health = 1000;
    info.settings.capture_rounds = 0;
    info.travel = {travel, nullptr};
    info.group_count = second_patch ? 2 : 1;
    for (int g = 0; g < info.group_count; ++g) {
        auto& row = info.groups[g];
        row.id = 100 + g; row.position = {2 + g * 20, 0};
        row.full_health = row.health = 10;
        row.full_clear_rounds = row.clear_rounds = 1;
        row.gems = 10; row.respawn_rounds = 20;
        row.lifecycle = Lifecycle::living; row.visible = row.all_spawns_visible = row.occupancy_known = true;
        row.provenance = row.health_provenance = {info.round, Source::personal_vision, Status::exact_now};
    }
    info.altar_count = second_patch ? 2 : 1;
    for (int a = 0; a < info.altar_count; ++a) {
        info.altars[a].id = a + 1;
        info.altars[a].position = {5 + a * 20, 0};
        info.altars[a].altar_position = {6 + a * 20, 0};
        info.altars[a].lock_known = true;
    }
}
h::ObservationFrame frame(const Information& info, int actor, int group, int round, bool producing = true) {
    h::ObservationFrame result;
    result.round = round; result.count = 1;
    auto& sample = result.consumers[0];
    sample.actor_id = actor; sample.group_id = info.groups[group].id; sample.position = info.groups[group].position;
    sample.personally_visible = true; sample.producing = producing;
    // Synthetic preclassified participation evidence is explicitly predicted;
    // this DTO does not assert an SDK attack-event witness.
    sample.evidence = h::ProductionEvidence::predicted_participation;
    sample.provenance = {round, Source::personal_vision, Status::exact_now};
    return result;
}
void two_samples(h::State& state, Information& info, int group = 0) {
    info.round = 9; h::observe(state, info, frame(info, 7, group, 9));
    info.round = 10; h::observe(state, info, frame(info, 7, group, 10));
}
Job farm() { Job result; result.kind = JobKind::farm; result.group = 0; result.altar = 0; return result; }
const Event* nth_event(const Forecast& forecast, EventKind kind, int occurrence = 0) {
    for (int n = 0; n < forecast.event_count; ++n)
        if (forecast.events[n].kind == kind && occurrence-- == 0) return &forecast.events[n];
    return nullptr;
}
[[gnu::noinline]] void preparation() {
    auto input = std::make_unique<Information>(); auto& info = *input; fixture(info, true);
    auto state = std::make_unique<h::State>();
    check(h::prepare_state(info, *state, 1), "complete public geometry prepares supported capacity");
    check(state->patch[0] == 0 && state->patch[1] == 1, "groups use nearest public reachable altar");
    check(near(state->capacity[0], 10.0 / 22) && near(state->capacity[1], 10.0 / 22),
          "capacity units are gems per clear, capture initiation/countdown and respawn round");
    const unsigned revision = state->capacity_revision;
    check(h::prepare_state(info, *state, 1) && state->capacity_revision == revision,
          "unchanged public/stat identity reuses capacity without duplicate accumulation");
    info.groups[0].full_clear_rounds = 3;
    check(h::prepare_state(info, *state, 2) && state->capacity_revision == revision + 1 &&
          near(state->capacity[0], 10.0 / 24), "kit/combat-stat change invalidates and rebuilds capacity");
    info.altars[0].position = {0, 0}; info.altars[0].altar_position = {0, 1};
    info.altars[1].position = {4, 0}; info.altars[1].altar_position = {4, 1};
    check(h::prepare_state(info, *state, 2) && state->patch[0] == 0,
          "equal public distances break ties by altar coordinates");
    info.travel = {unknown_travel, nullptr};
    auto unsupported = std::make_unique<h::State>();
    check(!h::prepare_state(info, *unsupported, 2) && !unsupported->supported && unsupported->unknown_geometry,
          "missing public routes do not invent known patches or partial exact capacity");
    info.travel = {travel, nullptr};
    check(h::prepare_state(info, *unsupported, 2), "unresolved geometry can complete when exact public cache becomes available");
}
[[gnu::noinline]] void legal_pressure() {
    auto input = std::make_unique<Information>(); auto& info = *input; fixture(info, true);
    auto state = std::make_unique<h::State>(); h::prepare_state(info, *state);
    check(h::pressure(*state, 0, info.round).observed_consumers == 0 && h::pressure(*state, 0, info.round).unknown,
          "no personally observed consumers means unknown pressure with zero surrogate");
    h::observe(*state, info, frame(info, 7, 0, 10));
    check(h::pressure(*state, 0, 10).observed_consumers == 0, "one observation cannot fabricate two-frame production evidence");
    info.round = 11; h::observe(*state, info, frame(info, 7, 0, 11));
    check(h::pressure(*state, 0, 11).observed_consumers == 1 && h::pressure(*state, 1, 11).observed_consumers == 0,
          "fresh personally observed producer contributes only to its public patch");
    info.round = 11; auto duplicate = frame(info, 7, 0, 11);
    duplicate.count = 2; duplicate.consumers[1] = duplicate.consumers[0];
    h::observe(*state, info, duplicate);
    check(h::pressure(*state, 0, 11).observed_consumers == 1,
          "duplicate actor IDs and repeated same-round frames do not duplicate consumers");
    check(h::pressure(*state, 0, 12).observed_consumers == 1 && h::pressure(*state, 0, 12).unknown,
          "bounded two-round history remains predicted and absence of sight remains unknown");
    check(h::pressure(*state, 0, 13).observed_consumers == 0, "consumer evidence older than two rounds expires");
    info.round = 12; auto ambiguous = frame(info, 7, 0, 12);
    ambiguous.consumers[0].ambiguous = true;
    h::observe(*state, info, ambiguous);
    check(h::pressure(*state, 0, 12).observed_consumers == 0, "ambiguous participation cannot train a consumer count");
    info.round = 13; auto hidden = frame(info, 8, 0, 13);
    hidden.consumers[0].personally_visible = false;
    h::observe(*state, info, hidden);
    check(h::pressure(*state, 0, 13).observed_consumers == 0, "hidden actor features cannot enter the tracker");
    info.round = 14; auto self = frame(info, info.own.id, 0, 14);
    h::observe(*state, info, self);
    check(h::pressure(*state, 0, 14).observed_consumers == 0, "our actor is not counted as another consumer");
    auto known_empty = std::make_unique<h::State>(); h::prepare_state(info, *known_empty);
    h::ObservationFrame empty; empty.count = 0; empty.complete = true; empty.full_patch_visibility[0] = true;
    empty.round = info.round = 15; h::observe(*known_empty, info, empty);
    empty.round = info.round = 16; h::observe(*known_empty, info, empty);
    check(!h::pressure(*known_empty, 0, 16).unknown && h::pressure(*known_empty, 0, 16).observed_consumers == 0,
          "known zero requires complete legal patch coverage in both observation frames");
    auto disjoint = std::make_unique<h::State>(); h::prepare_state(info, *disjoint);
    two_samples(*disjoint, info, 1);
    check(h::pressure(*disjoint, 0, 10).observed_consumers == 0 &&
          h::pressure(*disjoint, 1, 10).observed_consumers == 1, "disjoint-patch producer is a negative pressure condition");
    info.round = 11; h::observe(*disjoint, info, frame(info, 7, 1, 11, false));
    check(h::pressure(*disjoint, 1, 11).observed_consumers == 0,
          "fresh observation of stopped service replaces earlier producer evidence");
    auto departed = std::make_unique<h::State>(); h::prepare_state(info, *departed);
    info.round = 20; auto visible_producer = frame(info, 7, 0, 20);
    visible_producer.complete = true; visible_producer.full_patch_visibility[0] = true;
    h::observe(*departed, info, visible_producer);
    info.round = visible_producer.round = visible_producer.consumers[0].provenance.observed_round = 21;
    h::observe(*departed, info, visible_producer);
    check(h::pressure(*departed, 0, 21).observed_consumers == 1 && !h::pressure(*departed, 0, 21).unknown,
          "two complete visible frames can establish a known current producer");
    empty.round = info.round = 22;
    h::observe(*departed, info, empty);
    check(h::pressure(*departed, 0, 22).observed_consumers == 0 && !h::pressure(*departed, 0, 22).unknown,
          "complete current patch sight invalidates an earlier producer now observed absent");
    empty.round = info.round = 23; empty.count = -1;
    h::observe(*departed, info, empty);
    check(h::pressure(*departed, 0, 23).unknown, "malformed negative consumer count cannot assert complete coverage");
}
[[gnu::noinline]] void supply_and_stock() {
    auto input = std::make_unique<Information>(); auto& info = *input; fixture(info);
    auto state = std::make_unique<h::State>(); h::prepare_state(info, *state); two_samples(*state, info);
    h::Path active, control;
    const auto active_hooks = h::make_hooks(*state, active, true);
    const auto control_hooks = h::make_hooks(*state, control, false);
    const auto quote = [&](const ForecastHooks& hooks, bool ready, int readiness = 0) {
        return hooks.supply_wait(0, 10, 2, readiness, ready, info, hooks.model, hooks.path);
    };
    check(quote(active_hooks, true) == 0 && quote(control_hooks, true) == 0,
          "first witnessed ready harvest is free of aggregate regeneration charge in both arms");
    active_hooks.captured(0, 14, info, active_hooks.model, active_hooks.path);
    control_hooks.captured(0, 14, info, control_hooks.model, control_hooks.path);
    check(quote(active_hooks, true) == 42 && quote(control_hooks, true) == 20,
          "consumed initial stock exposes pressure coupling, with identical capacity in both arms");
    check(quote(active_hooks, false, 30) == 42, "model returns supply wait alone for common max, not readiness plus supply");
    check(active.unknown_pressure && control.unknown_pressure,
          "unknown pressure is logged independently of active/control numerical surrogate");
    h::Path new_path;
    const auto reset_hooks = h::make_hooks(*state, new_path, true);
    check(quote(reset_hooks, true) == 0, "each candidate gets a fresh ready-stock ledger");
    auto unseen = std::make_unique<h::State>(); h::prepare_state(info, *unseen);
    h::Path unknown_path;
    const auto unknown_hooks = h::make_hooks(*unseen, unknown_path, true);
    check(quote(unknown_hooks, false) == 20 && unknown_path.unknown_pressure,
          "unseen consumers use the declared zero-pressure surrogate while retaining unknown flag");
    info.groups[0].gems = 0; h::prepare_state(info, *state);
    check(state->supported && state->capacity[0] == 0 && quote(reset_hooks, false) == 0,
          "known zero capacity uses M0 timing and produces no aggregate replenishment credit");
}
[[gnu::noinline]] void integrated_forecasts() {
    auto input = std::make_unique<Information>(); auto& info = *input; fixture(info);
    auto state = std::make_unique<h::State>(); h::prepare_state(info, *state); two_samples(*state, info);
    info.settings.max_rounds = 60;
    Menu menu; menu.jobs[0] = farm(); menu.count = menu.single_count = 1;
    auto active = std::make_unique<Forecast>(), control = std::make_unique<Forecast>();
    h::Path active_path, control_path;
    h::predict_into(info, farm(), menu, *state, true, *active, &active_path);
    h::predict_into(info, farm(), menu, *state, false, *control, &control_path);
    check(active->first_conversion_round == 18 && control->first_conversion_round == 18,
          "initial ready stock preserves ordinary first conversion timing under pressure");
    check(active->our_secured == 10 && control->our_secured == 20,
          "pressure changes completed later conversions instead of adding rate times horizon");
    const Event* active_wait = nth_event(*active, EventKind::wait);
    const Event* control_wait = nth_event(*control, EventKind::wait);
    check(active_wait && active_wait->end.round - active_wait->start.round == 42 &&
          control_wait && control_wait->end.round - control_wait->start.round == 20,
          "common simulator takes max of16-round readiness and supply42/20 rather than summing them");
    for (const Forecast* forecast : {active.get(), control.get()}) {
        unsigned acquired = 0, converted = 0;
        for (int n = 0; n < forecast->event_count; ++n) {
            const auto& event = forecast->events[n];
            if (event.kind == EventKind::capture) acquired += event.bag_after - event.bag_before;
            if (event.kind == EventKind::bank) converted += event.secured;
        }
        check(converted == forecast->our_secured && acquired == converted + forecast->final_bag,
              "Habitat uses common discrete capture/bank conservation, with no C-times-H score");
    }
    h::Path fresh_path;
    h::predict_into(info, farm(), Menu{}, *state, true, *control, &fresh_path);
    check(control->our_secured == 10 && control->first_conversion_round == 18,
          "another candidate cannot inherit consumed stock from an earlier forecast");
    info.groups[0].visible = false;
    h::predict_into(info, farm(), Menu{}, *state, true, *active);
    check(active->our_secured == 0 && active->stock_unknown, "unknown initial stock contributes no assured burst despite positive capacity");
    fixture(info);
    auto unsupported = std::make_unique<h::State>();
    plan4::predict_into(info, farm(), Menu{}, *control);
    h::Path unsupported_path;
    h::predict_into(info, farm(), Menu{}, *unsupported, true, *active, &unsupported_path);
    check(unsupported_path.unsupported_capacity && active->our_secured == control->our_secured &&
          active->first_conversion_round == control->first_conversion_round && active->event_count == control->event_count,
          "unsupported capacity falls back to the same common M0 forecast");
}
} // namespace
int main() {
    preparation(); legal_pressure(); supply_and_stock(); integrated_forecasts();
    std::printf("Plan4 Habitat: %d checks passed; synthetic pure cases, no legal-engine witness claimed.\n", checks);
}
