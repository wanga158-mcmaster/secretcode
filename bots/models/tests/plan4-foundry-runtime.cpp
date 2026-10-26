#include "../foundry/src/runtime.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
using namespace plan4;
namespace r = plan4::runtime;
int checks = 0;
void check(bool value, const char* reason) { ++checks; if (!value) { std::fprintf(stderr, "FAIL: %s\n", reason); std::exit(1); } }
struct Board {
    bool unknown = false, routes = true, extra_occupied = false;
    static bool walkable(Point p, const void*) { return p.x >= 0 && p.x < 32 && p.y >= 0 && p.y < 32; }
    static r::TileOccupancy occupancy(Point p, const void* c) {
        const auto& board = *static_cast<const Board*>(c);
        if (board.unknown && p.y == 1) return r::TileOccupancy::unknown;
        return p == Point{1, 3} || (board.extra_occupied && p == Point{5, 0}) ? r::TileOccupancy::occupied : r::TileOccupancy::free;
    }
    static int distance(Point a, Point b, const void*) { return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y)); }
    static int prefix(Point a, Point b, Point* out, int capacity, const void* c) {
        if (!static_cast<const Board*>(c)->routes) return -1;
        int count = 0;
        while (a != b && count < capacity) {
            Point best{}; bool found = false;
            for (int dx = -1; dx <= 1; ++dx) for (int dy = -1; dy <= 1; ++dy) {
                Point next{a.x + dx, a.y + dy};
                if (!walkable(next, c) || occupancy(next, c) != r::TileOccupancy::free ||
                    distance(next, b, c) != distance(a, b, c) - 1) continue;
                if (!found || point_less(next, best)) { found = true; best = next; }
            }
            if (!found) return -1;
            out[count++] = a = best;
        }
        return count;
    }
};
struct Fixture {
    Information info;
    Menu menu;
    r::ActorFrame actors;
    r::ProducerFrame producers;
    r::CombatKit kit;
    r::SpatialFrame spatial;
    std::array<r::PublicGroup, 2> groups;
    std::array<r::PublicMember, 3> members;
    Board board;
    Fixture() {
        info.round = 10; info.settings.max_rounds = 70;
        info.own.id = 4; info.own.position = {0, 2}; info.own_spawn = {0, 3};
        info.own.health = info.own.maximum_health = 100; info.own.recall_safe = true;
        info.group_count = 2; info.altar_count = 1; info.travel = {Board::distance, &board};
        info.altars[0].id = 9; info.altars[0].position = {0, 3}; info.altars[0].altar_position = {0, 4}; info.altars[0].lock_known = true;
        for (int g = 0; g < 2; ++g) {
            auto& group = info.groups[g]; group.id = 20 + g; group.position = {g * 5, 0};
            group.full_health = g ? 1 : 2; group.health = 0; group.gems = g ? 5 : 11; group.gold = g ? 3 : 7;
            group.clear_rounds = 0; group.full_clear_rounds = 1; group.respawn_rounds = 50;
            group.lifecycle = Lifecycle::cleared; group.reward_available = true;
            group.visible = group.all_spawns_visible = group.occupancy_known = true;
            group.provenance = group.health_provenance = {10, Source::personal_vision, Status::exact_now};
        }
        info.groups[0].occupied_spawn = true; info.groups[0].eligible_allied_occupants = 1;
        menu = make_menu(info);
        actors.clock = producers.clock = kit.clock = spatial.clock = {10, {4}};
        actors.roster_complete = actors.own_observed = true; actors.allied_count = 2; actors.enemy_count = 1;
        actors.allied_roster[0] = 4; actors.allied_roster[1] = 2; actors.enemy_roster[0] = 9;
        actors.own = {4, 0, 11, 3, {0, 2}, 100, 100, 10, 0, 0, 0, {10, Source::own_state, Status::exact_now}, true, true, false};
        actors.count = 1; actors.samples[0] = {2, 1, 10, 1, {1, 3}, 100, 100, 10, 0, 0, 0,
            {10, Source::personal_vision, Status::exact_now}, true, true, false};
        producers.count = 1; producers.samples[0] = {2, 20, {1, 3}, {10, Source::personal_vision, Status::exact_now},
            true, true, false, r::ProductionEvidence::visible_cleared_spawn};
        kit.champion = 11; kit.attack_range = 3; kit.strength = 10; kit.version = 1; kit.exact = true;
        spatial.topology = {32, 32, true, Board::walkable, &board}; spatial.current_complete = true;
        spatial.occupancy = Board::occupancy; spatial.occupancy_context = &board;
        spatial.travel = {Board::distance, &board}; spatial.route_prefix = Board::prefix; spatial.route_context = &board;
        spatial.own_recall_here = true; spatial.graph_quota = 0;
        spatial.information = &info; spatial.menu = &menu;
        groups[0] = {20, 0, 2, true}; groups[1] = {21, 2, 1, true};
        members[0] = {20, 100, 0, 2, 0, 3, {0, 0}, 1}; members[1] = {20, 101, 1, 2, 0, 3, {1, 3}, 1};
        members[2] = {21, 102, 0, 1, 0, 3, {5, 0}, 1};
    }
    void publish(r::Foundry& model) {
        model.observe_actors(actors); model.observe_producers(producers); model.observe_combat(kit); model.observe_spatial(spatial);
        model.observe_layout({{info.round, {info.own.id}}, groups.data(), 2, members.data(), 3, false});
        model.observe_snapshot(info);
    }
};
void equal_reference(const Fixture& f, const Evaluation& actual) {
    auto expected = std::make_unique<Evaluation>(); plan4::evaluate_into(f.info, f.menu, *expected);
    check(actual.evaluated_count == expected->evaluated_count && actual.chosen == expected->chosen &&
          actual.budget_exhausted == expected->budget_exhausted && actual.menu.count == expected->menu.count &&
          actual.menu.single_count == expected->menu.single_count && actual.menu.truncated == expected->menu.truncated,
          "fallback uses complete identical common menu and selector");
    for (int n = 0; n < actual.evaluated_count; ++n) {
        const auto& a = actual.forecasts[n]; const auto& b = expected->forecasts[n];
        check(a.our_secured == b.our_secured && a.enemy_secured == b.enemy_secured &&
              a.first_conversion_round == b.first_conversion_round && a.final_round == b.final_round &&
              a.final_bag == b.final_bag && a.final_gold == b.final_gold && a.final_health == b.final_health &&
              a.terminal_z == b.terminal_z && a.terminal_unknown == b.terminal_unknown && a.stock_unknown == b.stock_unknown &&
              a.travel_unknown == b.travel_unknown && a.lock_unknown == b.lock_unknown && a.truncated == b.truncated &&
              a.budget_exhausted == b.budget_exhausted && a.abort_reason == b.abort_reason && a.event_count == b.event_count &&
              a.objective.paths == 0,
              "fallback replaces every model result rather than mixing forecast families");
    }
}
struct Gate { int remaining = 750; static bool available(const void* p) { return --const_cast<Gate*>(static_cast<const Gate*>(p))->remaining >= 0; } };
struct OccupancyGate {
    Board* board = nullptr;
    bool open = true; int calls = 0, after_denial = 0;
    static bool available(const void* p) { return static_cast<const OccupancyGate*>(p)->open; }
    static r::TileOccupancy query(Point p, const void* c) {
        auto& gate = *const_cast<OccupancyGate*>(static_cast<const OccupancyGate*>(c));
        if (!gate.open) ++gate.after_denial;
        if (++gate.calls == 2) gate.open = false;
        return Board::occupancy(p, gate.board);
    }
};
struct Receipts {
    std::array<r::ModelPredictionEvent, 8> rows;
    int count = 0, limit = 8;
    static bool collect(const r::ModelPredictionEvent& row, void* context) {
        auto& self = *static_cast<Receipts*>(context);
        self.rows[self.count++] = row; return self.count < self.limit;
    }
};
struct TravelGate {
    bool open = true; int prefix_calls = 0; Board* board = nullptr;
    static bool available(const void* p) { return static_cast<const TravelGate*>(p)->open; }
    static int distance(Point a, Point b, const void* p) {
        auto& self = *const_cast<TravelGate*>(static_cast<const TravelGate*>(p));
        self.open = false; return Board::distance(a, b, self.board);
    }
    static int prefix(Point a, Point b, Point* out, int capacity, const void* p) {
        auto& self = *const_cast<TravelGate*>(static_cast<const TravelGate*>(p)); ++self.prefix_calls;
        return Board::prefix(a, b, out, capacity, self.board);
    }
};
struct DeniedAdmission {
    int calls = 0;
    static bool available(const void* p) { ++const_cast<DeniedAdmission*>(static_cast<const DeniedAdmission*>(p))->calls; return false; }
};
void coverage_boundaries() {
    auto model = std::make_unique<r::Foundry>(); auto output = std::make_unique<Evaluation>();
    check(model->truncation_mask() == -1, "new model has not reached the truncation guard");
    for (int mask : {1, 2, 4, 8, 16, 32, 40, 63}) {
        auto f = std::make_unique<Fixture>();
        f->info.public_layout_truncated = (mask & 1) != 0; f->menu.truncated = (mask & 2) != 0;
        f->actors.truncated = (mask & 16) != 0; f->producers.truncated = (mask & 32) != 0;
        f->publish(*model);
        if (mask & 4) model->observe_layout({{f->info.round, {f->info.own.id}}, nullptr, 0, nullptr, 0, true});
        if (mask & 8) model->observe_snapshot(f->info, {nullptr, 0, true});
        check(model->truncation_mask() == -1, "new observations clear previous guard diagnostics");
        r::evaluate_model(f->info, f->menu, *model, *output);
        check(model->truncation_mask() == mask && model->unsupported() == r::Foundry::Unsupported::truncated &&
              !model->used_foundry() && !model->model_evidence().supported,
              "each existing truncation source and shared cascade retains its reason and exact mask");
        equal_reference(*f, *output);
        const auto revision = model->model_evidence().revision;
        model->selected(f->info, f->menu.jobs[0], 1, false);
        check(model->truncation_mask() == mask && model->model_evidence().revision == revision,
              "selection and diagnostic readback preserve the evaluated blockade");
    }
    auto f = std::make_unique<Fixture>();
    f->publish(*model); r::evaluate_model(f->info, f->menu, *model, *output);
    check(model->truncation_mask() == 0 && model->used_foundry(), "successful complete binding records a clear guard");
    f->spatial.current_complete = false; f->publish(*model); r::evaluate_model(f->info, f->menu, *model, *output);
    check(model->truncation_mask() == 0 && model->unsupported() == r::Foundry::Unsupported::geometry,
          "a later rejection retains clear truncation coverage rather than inventing a blocker");
    equal_reference(*f, *output);
    // Invalid frame shape remains distinct from a frame's own truncated flag.
    f = std::make_unique<Fixture>(); f->publish(*model);
    model->observe_layout({{f->info.round, {f->info.own.id}}, nullptr, 1, nullptr, 0, false});
    model->observe_snapshot(f->info, {nullptr, 1, false});
    r::evaluate_model(f->info, f->menu, *model, *output);
    check(model->truncation_mask() == 12 && model->unsupported() == r::Foundry::Unsupported::truncated,
          "invalid layout and member shapes keep their original fail-closed blockade");
    equal_reference(*f, *output);
    for (int replacement = 0; replacement < 6; ++replacement) {
        f = std::make_unique<Fixture>(); f->menu.truncated = true; f->publish(*model);
        r::evaluate_model(f->info, f->menu, *model, *output);
        check(model->truncation_mask() == 2, "seeded diagnostic belongs to its evaluated menu");
        if (replacement == 0) model->observe_actors(f->actors);
        if (replacement == 1) model->observe_producers(f->producers);
        if (replacement == 2) model->observe_combat(f->kit);
        if (replacement == 3) model->observe_spatial(f->spatial);
        if (replacement == 4) model->observe_layout({{f->info.round, {f->info.own.id}}, nullptr, 0, nullptr, 0, false});
        if (replacement == 5) model->observe_snapshot(f->info);
        check(model->truncation_mask() == -1, "every replacement evidence entry point clears prior coverage");
        DeniedAdmission gate; f->info.work_budget = {DeniedAdmission::available, &gate};
        const auto revision = model->model_evidence().revision;
        r::evaluate_model(f->info, f->menu, *model, *output);
        const int calls = gate.calls;
        model->selected(f->info, f->menu.jobs[0], 2, false);
        check(output->budget_exhausted && output->evaluated_count == 0 && model->truncation_mask() == -1 &&
              model->model_evidence().revision == revision && gate.calls == calls,
              "fresh evidence then common admission denial stays unknown through selection without diagnostic work");
    }
    f = std::make_unique<Fixture>(); f->menu.truncated = true; f->publish(*model);
    r::evaluate_model(f->info, f->menu, *model, *output);
    DeniedAdmission gate; f->info.work_budget = {DeniedAdmission::available, &gate};
    model->evaluate_into(f->info, f->menu, *output);
    check(model->truncation_mask() == -1 && model->unsupported() == r::Foundry::Unsupported::budget,
          "private evaluation clears a prior positive mask before its early budget guard");
    f = std::make_unique<Fixture>(); f->info.own.channel = Channel::recall; f->publish(*model);
    r::evaluate_model(f->info, f->menu, *model, *output);
    check(model->truncation_mask() == -1 && model->unsupported() == r::Foundry::Unsupported::forecast,
          "the earlier common channel path never claims to inspect truncation coverage");
    equal_reference(*f, *output);
}
int main() {
    coverage_boundaries();
    auto fixture = std::make_unique<Fixture>(); auto active = std::make_unique<r::Foundry>(true);
    auto control = std::make_unique<r::Foundry>(false); auto output = std::make_unique<Evaluation>();
    fixture->publish(*active); r::evaluate_model(fixture->info, fixture->menu, *active, *output);
    check(active->used_foundry() && output->evaluated_count == fixture->menu.count && output->chosen >= 0,
          "direct cleared-spawn evidence binds full menu and completed team forecasts");
    check(active->binding().actor_count == 2 && active->binding().actors[1].intent == foundry::Intent::hold &&
          active->binding().actors[1].work_per_action == 0 && active->binding().actors[1].bag == 0,
          "visible hold admits no guessed ally work, items or transferred bag");
    const auto evidence = active->model_evidence();
    check(evidence.reported && evidence.supported && evidence.coupling_enabled && evidence.reason == 0 && evidence.samples == 1 &&
          evidence.paths == fixture->menu.count && evidence.work > 0 && evidence.revision > 0,
          "evidence reports actual bound holders and completed whole-menu model batch");
    Receipts receipts;
    const auto chosen = output->menu.jobs[output->chosen];
    active->prediction_receipts(fixture->info, chosen, Receipts::collect, &receipts);
    check(receipts.count > 0 && receipts.count <= 8, "chosen job emits only its cached bounded salient prefix");
    bool ally_receipt = false; int last_round = fixture->info.round, last_phase = -1;
    for (int n = 0; n < receipts.count; ++n) {
        const auto& row = receipts.rows[n];
        check((row.actor_id == 4 || row.actor_id == 2) && (row.group_id == 20 || row.group_id == 21) &&
              row.amount_milli >= 0 && row.amount_milli % 1000 == 0,
              "salient receipts use physical actor/source IDs and integer thousandths");
        check(row.predicted_round > last_round || (row.predicted_round == last_round && row.phase >= last_phase),
              "cached colored receipt prefix preserves global phase chronology");
        last_round = row.predicted_round; last_phase = row.phase; ally_receipt |= row.actor_id == 2;
    }
    check(ally_receipt, "team prediction prefix retains separately colored ally output");
    Receipts short_sink; short_sink.limit = 1;
    fixture->board.routes = false;
    active->prediction_receipts(fixture->info, chosen, Receipts::collect, &short_sink);
    check(short_sink.count == 1, "sink refusal stops immediately and cached output never reruns route prediction");
    fixture->board.routes = true;
    Receipts unknown_key; Job absent = chosen; absent.group = 200;
    active->prediction_receipts(fixture->info, absent, Receipts::collect, &unknown_key);
    check(unknown_key.count == 0, "unselected absent candidate key cannot borrow another receipt prefix");
    Receipts different_key;
    const int other = output->chosen ? 0 : 1;
    active->prediction_receipts(fixture->info, fixture->menu.jobs[other], Receipts::collect, &different_key);
    check(different_key.count == 0, "completed unchosen candidate cannot be emitted as the selected path");
    ++fixture->info.own.bag; Receipts changed_source;
    active->prediction_receipts(fixture->info, chosen, Receipts::collect, &changed_source);
    check(changed_source.count == 0, "mutated source bag invalidates cached predicted output"); --fixture->info.own.bag;
    active->observe_actors(fixture->actors); Receipts observed_again;
    active->prediction_receipts(fixture->info, chosen, Receipts::collect, &observed_again);
    check(observed_again.count == 0 && !active->model_evidence().supported,
          "even a same-round replacement frame invalidates previously bound receipts");
    fixture->publish(*control); r::evaluate_model(fixture->info, fixture->menu, *control, *output);
    check(control->used_foundry() && control->binding().actor_count == active->binding().actor_count,
          "disabled uses same observed actors, menu, quotes and batch shape");
    check(control->model_evidence().reported && control->model_evidence().supported && !control->model_evidence().coupling_enabled,
          "disabled model reports the same supported adapter with only coupling disabled");
    fixture->actors.own.draft_ordinal = fixture->actors.samples[0].draft_ordinal = -1;
    fixture->publish(*active); r::evaluate_model(fixture->info, fixture->menu, *active, *output);
    check(active->used_foundry() && active->binding().actors[0].order == 0 && active->binding().actors[1].order == 1,
          "fresh completion relative to own resolves one stable priority tie without guessing SDK enumeration order");
    fixture->actors.samples[0].completed_turn = true;
    fixture->publish(*active); r::evaluate_model(fixture->info, fixture->menu, *active, *output);
    check(active->used_foundry() && active->binding().actors[1].order == 0 && !active->binding().actors[1].initial_turn_pending,
          "a holder already acted before own is not granted another initial-round action");
    fixture->actors.samples[0].completed_turn = false;
    fixture->producers.samples[0].evidence = r::ProductionEvidence::predicted_participation;
    fixture->publish(*active); r::evaluate_model(fixture->info, fixture->menu, *active, *output);
    check(!active->used_foundry() && active->unsupported() == r::Foundry::Unsupported::participation,
          "living inferred participation cannot bypass missing spell/departure/rival exclusions"); equal_reference(*fixture, *output);
    Receipts fallback_receipts; active->prediction_receipts(fixture->info, chosen, Receipts::collect, &fallback_receipts);
    check(active->model_evidence().reported && !active->model_evidence().supported && fallback_receipts.count == 0,
          "whole-batch M0 fallback reports unsupported and discards older model receipts");
    fixture = std::make_unique<Fixture>(); fixture->spatial.current_complete = false;
    fixture->publish(*active); r::evaluate_model(fixture->info, fixture->menu, *active, *output);
    check(!active->used_foundry() && active->unsupported() == r::Foundry::Unsupported::geometry, "missing current spatial proof stays unsupported"); equal_reference(*fixture, *output);
    fixture = std::make_unique<Fixture>(); fixture->actors.samples[0].next_priority = 1;
    fixture->publish(*active); r::evaluate_model(fixture->info, fixture->menu, *active, *output);
    check(active->unsupported() == r::Foundry::Unsupported::changed_order && !active->used_foundry(), "different current/next schedules stay unsupported");
    fixture = std::make_unique<Fixture>(); fixture->actors.samples[0].provenance.observed_round = 9;
    fixture->publish(*active); r::evaluate_model(fixture->info, fixture->menu, *active, *output);
    check(active->unsupported() == r::Foundry::Unsupported::participation && !active->used_foundry(), "stale exact-looking ally body cannot bind a current hold");
    fixture = std::make_unique<Fixture>(); fixture->board.routes = false;
    fixture->publish(*active); r::evaluate_model(fixture->info, fixture->menu, *active, *output);
    check(!active->used_foundry(), "unknown cached route refuses the entire model batch"); equal_reference(*fixture, *output);
    fixture = std::make_unique<Fixture>(); fixture->board.unknown = true;
    fixture->publish(*active); r::evaluate_model(fixture->info, fixture->menu, *active, *output);
    check(!active->used_foundry(), "a personally unknown route tile cannot be silently declared free"); equal_reference(*fixture, *output);
    fixture = std::make_unique<Fixture>(); fixture->producers.samples[0].ambiguous = true;
    fixture->publish(*active); r::evaluate_model(fixture->info, fixture->menu, *active, *output);
    check(!active->used_foundry() && active->unsupported() == r::Foundry::Unsupported::participation, "ambiguous hold never becomes assured source stock");
    fixture = std::make_unique<Fixture>(); fixture->actors.allied_count = 3; fixture->actors.allied_roster[2] = 3;
    fixture->actors.count = 2; fixture->actors.samples[1] = fixture->actors.samples[0];
    fixture->actors.samples[1].id = 3; fixture->actors.samples[1].position = {5, 0};
    fixture->producers.count = 2; fixture->producers.samples[1] = fixture->producers.samples[0];
    fixture->producers.samples[1].actor_id = 3; fixture->producers.samples[1].group_id = 21; fixture->producers.samples[1].position = {5, 0};
    fixture->board.extra_occupied = true; fixture->info.groups[1].occupied_spawn = true;
    fixture->publish(*active); r::evaluate_model(fixture->info, fixture->menu, *active, *output);
    check(!active->used_foundry() && active->unsupported() == r::Foundry::Unsupported::changed_order,
          "two equally prioritized unacted holders have unresolved mutual order and stay unsupported");
    fixture = std::make_unique<Fixture>(); Gate gate; fixture->info.work_budget = {Gate::available, &gate};
    fixture->publish(*active); r::evaluate_model(fixture->info, fixture->menu, *active, *output);
    check(!active->used_foundry() && output->budget_exhausted && output->evaluated_count == 0 && output->chosen == -1,
          "a budget interruption discards an incomplete model batch instead of selecting its completed prefix");
    fixture = std::make_unique<Fixture>(); OccupancyGate occupancy_gate; occupancy_gate.board = &fixture->board;
    fixture->info.work_budget = {OccupancyGate::available, &occupancy_gate};
    fixture->spatial.occupancy = OccupancyGate::query; fixture->spatial.occupancy_context = &occupancy_gate;
    fixture->publish(*active); r::evaluate_model(fixture->info, fixture->menu, *active, *output);
    check(output->budget_exhausted && output->evaluated_count == 0 && occupancy_gate.calls == 2 && occupancy_gate.after_denial == 0,
          "cached path validation checks the reserve between occupancy callbacks, including mid-prefix denial");
    fixture = std::make_unique<Fixture>(); TravelGate travel_gate; travel_gate.board = &fixture->board;
    fixture->info.work_budget = {TravelGate::available, &travel_gate};
    fixture->spatial.travel = {TravelGate::distance, &travel_gate};
    fixture->spatial.route_prefix = TravelGate::prefix; fixture->spatial.route_context = &travel_gate;
    fixture->publish(*active); r::evaluate_model(fixture->info, fixture->menu, *active, *output);
    check(output->budget_exhausted && output->evaluated_count == 0 && travel_gate.prefix_calls == 0,
          "a distance lookup that loses the reserve cannot start another route-prefix callback");
    fixture = std::make_unique<Fixture>(); fixture->info.round = 11;
    r::evaluate_model(fixture->info, fixture->menu, *active, *output);
    check(!active->used_foundry() && active->unsupported() == r::Foundry::Unsupported::stale_frame, "new decision round rejects cached spatial/actor/layout evidence");
    std::printf("plan4-foundry-runtime: %d checks passed (synthetic DTOs; no legal engine witness)\n", checks);
}
