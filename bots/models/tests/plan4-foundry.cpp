// Pure predicted-resource tests, not legal engine fixtures or attack witnesses.
#include "../foundry/src/foundry.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

using namespace plan4;
using namespace plan4::foundry;

namespace {
int chebyshev(Point a, Point b, const void*) {
    return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y));
}
ParticipationEvidence evidence(int actor, int group, double current) {
    ParticipationEvidence e;
    e.actor_id = actor; e.group_id = group;
    e.previous_health = current + 3; e.current_health = current;
    e.previous = {9, Source::personal_vision, Status::exact_now};
    e.current = {10, Source::personal_vision, Status::exact_now};
    e.eligible_nearby_allies = 1; e.candidate_eligible = e.continuous_coverage = true;
    e.possible_spell = e.possible_departed_actor = e.possible_hidden_rival = false;
    return e;
}
Input base(int count = 2) {
    Input in;
    in.round = 10; in.horizon = 20; in.actor_count = count; in.group_count = 2; in.altar_count = 1;
    in.settings.winning_gems = 1000; in.settings.capture_rounds = 5;
    in.travel.distance = chebyshev;
    in.altars[0].id = 9; in.altars[0].position = {0, 1};
    for (int g = 0; g < 2; ++g) {
        auto& group = in.groups[g];
        group.id = 20 + g; group.position = {g * 10, 0};
        group.health = group.full_health = g == 0 ? 6 : 3;
        group.gems = g == 0 ? 11 : 5; group.gold = g == 0 ? 7 : 3;
        group.health_known = group.reward_available = true; group.respawn_rounds = 50;
        group.health_provenance = {10, Source::personal_vision, Status::exact_now};
    }
    for (int a = 0; a < count; ++a) {
        auto& actor = in.actors[a];
        actor.id = a == 0 ? 4 : 2; actor.order = a; actor.group = 0;
        actor.intent = Intent::attack; actor.position = actor.attack_position = actor.capture_position = {a, 0};
        actor.own = a == 0; actor.visible = true; actor.health = actor.maximum_health = 100;
        actor.work_per_action = 3; actor.bag = a == 0 ? 2 : 7;
        actor.capture_position_is_public_spawn = true;
        actor.observation = {10, Source::personal_vision, Status::exact_now};
        actor.participation = evidence(actor.id, 20, 6);
    }
    return in;
}
std::unique_ptr<PathState> simulate(const Input& input, bool coupled = true) {
    auto path = std::make_unique<PathState>();
    assert(path->initialize(input, coupled)); path->run_to_horizon();
    assert(!path->censored());
    return path;
}
void conserved(const PathState& path) {
    const auto c = path.accounting();
    assert(c.complete && c.health_conserved && c.group_stock_conserved && c.colored_bags_conserved && c.allocation_conserved);
}
int record_round(const PathState& path, RecordKind kind, int actor) {
    for (int e = 0; e < path.event_count(); ++e)
        if (path.event(e).kind == kind && path.event(e).actor == actor) return path.event(e).at.round;
    return -1;
}

void legal_inference_and_ambiguity_fallback() {
    auto e = evidence(2, 20, 6);
    const auto p = infer_participation(e);
    assert(p.supported && !p.ambiguous && p.provenance.status == Status::predicted && p.provenance.source == Source::model);
    auto ambiguous = e; ambiguous.possible_spell = true;
    assert(infer_participation(ambiguous).reason == Ambiguity::possible_other_source);
    ambiguous = e; ambiguous.possible_hidden_rival = true;
    assert(!infer_participation(ambiguous).supported);
    ambiguous = e; ambiguous.possible_departed_actor = true;
    assert(!infer_participation(ambiguous).supported);
    ambiguous = e; ambiguous.own_intervening_attack = true;
    assert(infer_participation(ambiguous).reason == Ambiguity::own_attack);
    ambiguous = e; ambiguous.eligible_nearby_allies = 2;
    assert(infer_participation(ambiguous).reason == Ambiguity::multiple_candidates);
    ambiguous = e; ambiguous.continuous_coverage = false;
    assert(infer_participation(ambiguous).reason == Ambiguity::interrupted_coverage);
    ambiguous = e; ambiguous.current.source = Source::model;
    assert(infer_participation(ambiguous).reason == Ambiguity::missing_snapshot);
    ambiguous = e; ambiguous.current_health = ambiguous.previous_health;
    assert(infer_participation(ambiguous).reason == Ambiguity::inconsistent_snapshot);
    auto in = base(); in.actors[1].participation.possible_hidden_rival = true;
    auto active = simulate(in), disabled = simulate(in, false);
    assert(active->actor(1).fallback && !active->actor(1).supported);
    assert(active->actor(1).bag == 7 && active->actor(1).secured_gems == 0);
    assert(active->clear_quote(0, 0).at.round == 11);
    assert(active->secured_output() == disabled->secured_output());
    assert(active->actor(0).acquired_gems == 11);
    assert(!active->capture_quote(1, 0).supported);
    conserved(*active); conserved(*disabled);
}

void shared_work_stock_remainder_and_colored_bags() {
    const auto in = base(); auto active = simulate(in), disabled = simulate(in, false);
    assert(active->clear_quote(0, 0).at.round == 10);
    assert(active->capture_quote(0, 0).at.round == 16);
    assert(disabled->clear_quote(0, 0).at.round == 11);
    assert(disabled->capture_quote(0, 0).at.round == 17);
    assert(active->group(0).work_applied == 6 && active->group(0).health == 0);
    assert(active->group(0).allocated_gems == 11 && active->group(0).allocated_gold == 7);
    // Actor 2, not array index 0, receives the full integer remainder.
    assert(active->actor(0).acquired_gems == 5 && active->actor(1).acquired_gems == 6);
    assert(active->actor(0).earned_gold == 3 && active->actor(1).earned_gold == 4);
    assert(active->actor(0).secured_gems == 7 && active->actor(1).secured_gems == 13);
    assert(active->secured_output() == 20);
    assert(disabled->actor(0).acquired_gems == 11 && disabled->actor(1).acquired_gems == 11);
    assert(disabled->secured_output() == 31);
    assert(!active->accounting().independent_resource_copies && !active->accounting().duplicated_physical_capture);
    assert(disabled->accounting().independent_resource_copies && disabled->accounting().duplicated_physical_capture);
    conserved(*active); conserved(*disabled);
    int captures = 0;
    for (int e = 0; e < active->event_count(); ++e) if (active->event(e).kind == RecordKind::capture) {
        assert(active->event(e).occupants == 2); ++captures;
    }
    assert(captures == 2);
}

void own_job(Input& in, int group) {
    auto& own = in.actors[0]; own.group = group;
    own.attack_position = own.capture_position = {0, -2};
    in.groups[group].position = {0, -2};
}
void same_group_ranking_changes_and_different_group_negative() {
    auto same = base(), different = same;
    same.groups[0].gems = different.groups[0].gems = 21;
    same.altars[0].position = different.altars[0].position = {0, -1};
    same.horizon = different.horizon = 10;
    own_job(different, 1);
    auto active_same = simulate(same), control_same = simulate(same, false);
    auto active_other = simulate(different), control_other = simulate(different, false);
    assert(active_same->secured_output() > active_other->secured_output());
    assert(control_same->secured_output() < control_other->secured_output());
    // With disjoint work/stock the two algorithms are identical, including
    // actor-local ally output: no free output appears only in the active arm.
    assert(active_other->secured_output() == control_other->secured_output());
    assert(active_other->actor(0).acquired_gems == control_other->actor(0).acquired_gems);
    assert(active_other->actor(1).acquired_gems == control_other->actor(1).acquired_gems);
    assert(active_other->capture_quote(0, 1).at.round == control_other->capture_quote(0, 1).at.round);
    assert(active_other->capture_quote(1, 0).at.round == control_other->capture_quote(1, 0).at.round);
    assert(!active_other->accounting().duplicated_physical_capture);
    conserved(*active_same); conserved(*control_same); conserved(*active_other); conserved(*control_other);
}

void cutoff_and_no_output_before_completed_bank() {
    auto in = base(1); in.travel.distance = [](Point, Point, const void*) { return 0; };
    in.horizon = 8;
    auto early = simulate(in);
    assert(early->capture_quote(0, 0).at.round == 17);
    assert(early->actor(0).bag == 13 && early->actor(0).mode == Mode::tribute);
    assert(early->secured_output() == 0);
    in.horizon = 9; auto exact = simulate(in);
    assert(record_round(*exact, RecordKind::bank, 0) == 19 && exact->secured_output() == 13);
    in.horizon = 10000; auto capped = simulate(in); assert(capped->cutoff_round() == 138);
    in.settings.max_rounds = 18; auto game_end = simulate(in);
    assert(game_end->secured_output() == 0 && game_end->terminal_comparison_unavailable());
    conserved(*early); conserved(*exact); conserved(*game_end);
}

void capture_precedes_existing_channel_and_uses_current_bag() {
    auto in = base(); in.groups[0].health = 0; in.groups[0].capture_remaining = 1;
    in.actors[0].intent = Intent::hold; in.actors[0].on_target_spawn = true;
    auto& ally = in.actors[1]; ally.intent = Intent::tribute; ally.on_target_spawn = true;
    ally.channel_type_known = ally.channel_remaining_known = true; ally.channel_remaining = 1;
    auto path = simulate(in);
    assert(path->capture_quote(0, 0).at.round == 11);
    assert(record_round(*path, RecordKind::bank, 1) == 11);
    assert(path->actor(1).secured_gems == 13); // Starting 7 plus same-round capture 6.
    int captured = -1, banked = -1;
    for (int e = 0; e < path->event_count(); ++e) {
        if (path->event(e).actor == 1 && path->event(e).kind == RecordKind::capture) captured = e;
        if (path->event(e).actor == 1 && path->event(e).kind == RecordKind::bank) banked = e;
    }
    assert(captured >= 0 && banked > captured);
    assert(path->event(captured).at.phase == Phase::capture && path->event(banked).at.phase == Phase::owner_start);
    conserved(*path);
    ally.channel_remaining_known = false; auto unknown = simulate(in);
    assert(unknown->actor(1).fallback && unknown->actor(1).secured_gems == 0);
    assert(unknown->actor(1).acquired_gems == 0);
}

void pathwise_terminal_stops_later_actor() {
    auto in = base();
    in.settings.winning_gems = 10; in.our_score = 5;
    for (auto& actor : in.actors) {
        actor.intent = Intent::tribute; actor.channel_remaining = 0;
        actor.channel_type_known = actor.channel_remaining_known = true; actor.initial_owner_start_pending = true;
    }
    in.actors[0].bag = 5; in.actors[1].bag = 9;
    auto path = simulate(in);
    assert(path->won() && path->secured_output() == 5);
    assert(path->actor(1).bag == 9 && path->actor(1).secured_gems == 0);
    assert(record_round(*path, RecordKind::bank, 1) == -1);
    conserved(*path);
    in.our_score = in.enemy_score = 10; auto tied = simulate(in);
    assert(tied->initial_terminal() && tied->ambiguous_initial_terminal() && !tied->won());
    assert(tied->event_count() == 0 && tied->secured_output() == 0);
}

void nearest_altars_are_independent_without_mutex() {
    auto in = base(); in.altar_count = 2;
    in.altars[0].position = {0, 2}; in.altars[1].id = 5; in.altars[1].position = {1, 2};
    auto path = simulate(in);
    assert(path->actor(0).altar == 1 && path->actor(1).altar == 1); // Equal distance: lowest public altar ID.
    assert(record_round(*path, RecordKind::bank_start, 0) == 18);
    assert(record_round(*path, RecordKind::bank_start, 1) == 18);
    assert(record_round(*path, RecordKind::bank, 0) == 19);
    assert(record_round(*path, RecordKind::bank, 1) == 20);
    conserved(*path);
    in.altars[1].hostile_lock = true; auto locked = simulate(in);
    assert(locked->actor(0).altar == 0 && locked->actor(1).altar == 0);
    in.altars[0].accessible = false; auto no_altar = simulate(in);
    assert(no_altar->secured_output() == 0 && no_altar->actor(0).bag == 7 && no_altar->actor(1).bag == 13);
    conserved(*no_altar);
}

void camp_damage_is_once_before_actions_and_death_loses_only_own_bag() {
    auto in = base(); in.groups[0].health = in.groups[0].full_health = 30;
    in.groups[0].incoming_per_round = 50; in.groups[0].awake = true;
    in.actors[1].participation = evidence(2, 20, 30);
    in.actors[0].health = 40;
    auto path = simulate(in);
    assert(path->actor(0).mode == Mode::dead && path->actor(0).lost_gems == 2 && path->actor(0).bag == 0);
    assert(record_round(*path, RecordKind::death, 0) == 11); // No replay of already-passed initial camp phase.
    const auto& first = path->event(0);
    assert(first.kind == RecordKind::work && first.at.round == 10);
    double damage_first_round = 0;
    for (int e = 0; e < path->event_count(); ++e)
        if (path->event(e).kind == RecordKind::damage && path->event(e).at.round == 11) damage_first_round += path->event(e).amount;
    assert(damage_first_round == 40); // One physical group's work, not one copy per ally.
    assert(path->actor(1).initial_bag == 7 && path->actor(1).initial_bag != path->actor(0).lost_gems);
    conserved(*path);
}

void witnessed_holds_actual_occupancy_and_no_invented_target() {
    auto in = base(); in.groups[0].health = 0;
    for (int a = 0; a < 2; ++a) { in.actors[a].intent = Intent::hold; in.actors[a].on_target_spawn = true; }
    in.groups[0].respawn_rounds = 0;
    auto path = simulate(in);
    assert(path->actor(1).acquired_gems == 6 && path->actor(1).mode == Mode::done);
    assert(path->group(0).captures == 1 && path->group(1).work_applied == 0);
    assert(path->group(0).generated_gems == 22); // One later respawn, never another ally cycle.
    conserved(*path);
    in.groups[0].contested = true; auto contested = simulate(in);
    assert(!contested->capture_quote(0, 0).known && contested->actor(0).acquired_gems == 0);
    in.groups[0].contested = false; in.groups[0].unmodeled_occupant = true; auto uncertain = simulate(in);
    assert(uncertain->secured_output() == 0);
    in.groups[0].unmodeled_occupant = false; in.actors[1].visible = false; auto invisible = simulate(in);
    assert(invisible->actor(1).fallback && invisible->actor(1).acquired_gems == 0);
    assert(invisible->actor(0).acquired_gems == 11);
}

void late_actor_keeps_existing_bag_without_a_second_cycle() {
    auto in = base(); in.actors[0].position = {-10, 0}; in.groups[0].respawn_rounds = 0;
    auto path = simulate(in);
    assert(path->actor(0).target_captured && path->actor(0).acquired_gems == 0);
    assert(path->actor(0).secured_gems == 2 && record_round(*path, RecordKind::bank, 0) == 22);
    assert(path->actor(1).acquired_gems == 11 && path->actor(1).secured_gems == 18);
    assert(path->group(0).work_applied == 6 && path->group(0).health == 6); // Respawn never invents a second target.
    assert(path->secured_output() == 20);
    conserved(*path);
}

struct Gate { bool open = false; };
bool allowed(const void* context) { return static_cast<const Gate*>(context)->open; }
void bounded_resume_copy_and_initial_turn_order() {
    void* storage = ::operator new(sizeof(PathState));
    // Volatile byte access measures actual constructor writes; an optimizer
    // may discard a plain pre-lifetime memset before inspecting indeterminate
    // storage and make this allocation-cost check meaningless.
    auto* bytes = static_cast<volatile unsigned char*>(storage);
    for (std::size_t k = 0; k < sizeof(PathState); ++k) bytes[k] = 0xa5;
    auto* uninitialized = new (storage) PathState();
    std::size_t untouched = 0;
    for (std::size_t k = 0; k < sizeof(PathState); ++k)
        untouched += bytes[k] == 0xa5;
    assert(untouched >= sizeof(Record) * record_limit); // No eager event-buffer zeroing.
    assert(uninitialized->event_count() == 0);
    uninitialized->~PathState(); ::operator delete(storage);
    auto in = base(); Gate gate;
    in.work_budget = {allowed, &gate};
    auto path = std::make_unique<PathState>(); assert(path->initialize(in));
    assert(!path->advance_event() && path->event_count() == 0 && path->round() == 10);
    gate.open = true; assert(path->advance_event()); assert(path->advance_event());
    assert(path->event_count() == 1 && path->event(0).kind == RecordKind::work);
    auto copy = std::make_unique<PathState>(*path);
    path->run_to_horizon(); copy->run_to_horizon();
    assert(path->secured_output() == copy->secured_output() && path->event_count() == copy->event_count());
    for (int e = 0; e < path->event_count(); ++e) {
        assert(path->event(e).at.round == copy->event(e).at.round && path->event(e).kind == copy->event(e).kind);
        assert(path->event(e).actor == copy->event(e).actor && path->event(e).amount == copy->event(e).amount);
    }
    in.work_budget = {}; in.actors[1].initial_turn_pending = false;
    auto late = simulate(in);
    assert(late->clear_quote(0, 0).at.round == 11);
    assert(record_round(*late, RecordKind::work, 1) == -1); // Own finishes before ally's first future action.
    conserved(*late);
}

void maximum_actors_and_fail_closed_unknowns() {
    auto in = base(1); in.actor_count = 5; in.groups[0].health = 0;
    for (int a = 0; a < 5; ++a) {
        if (a > 0) in.actors[a] = in.actors[0];
        auto& t = in.actors[a]; t.id = a; t.order = a; t.own = a == 0; t.bag = a;
        t.position = t.attack_position = t.capture_position = {a, 0};
        t.intent = Intent::hold; t.on_target_spawn = true;
    }
    auto path = simulate(in);
    assert(path->group(0).captures == 1 && path->group(0).allocated_gems == 11);
    assert(path->actor(0).acquired_gems == 3);
    for (int a = 1; a < 5; ++a) assert(path->actor(a).acquired_gems == 2);
    assert(path->event_count() < record_limit);
    conserved(*path);
    auto invalid = std::make_unique<PathState>(); in.actor_count = 6;
    assert(!invalid->initialize(in));
    in = base(); in.actors[1].capture_position = in.actors[0].capture_position;
    assert(!invalid->initialize(in)); // Do not invent two eligible occupants on one tile.
    in = base(1); in.groups[0].health_known = false;
    auto unknown = simulate(in); assert(unknown->actor(0).fallback && unknown->secured_output() == 0);
    in = base(1); in.actors[0].position = {5, 5}; in.travel = {};
    auto no_route = simulate(in); assert(no_route->actor(0).fallback && no_route->secured_output() == 0);
    in = base(1); in.actors[0].bag = std::numeric_limits<unsigned>::max();
    assert(invalid->initialize(in)); invalid->run_to_horizon();
    assert(invalid->censored() && !invalid->accounting().complete);
    assert(invalid->group(0).allocated_gems == 0); // Atomic preflight: no half-allocation.
}

struct FixedFixture {
    Input input;
    Information info;
    FixedPlan plan;
    FixedFixture(int actors = 2) : input(base(actors)) {
        for (int g = 0; g < input.group_count; ++g) input.groups[g].full_incoming_per_round = input.groups[g].incoming_per_round;
        input.settings.max_rounds = 70;
        input.horizon = max_horizon;
        info.round = input.round; info.settings = input.settings;
        info.own.id = input.actors[0].id; info.own.position = input.actors[0].position;
        info.own.health = float(input.actors[0].health); info.own.maximum_health = float(input.actors[0].maximum_health);
        info.own.bag = input.actors[0].bag; info.own_spawn = {0, 1}; info.travel = input.travel;
        info.group_count = input.group_count; info.altar_count = input.altar_count;
        for (int a = 0; a < input.altar_count; ++a) info.altars[a] = input.altars[a];
        for (int g = 0; g < input.group_count; ++g) {
            const auto& source = input.groups[g]; auto& target = info.groups[g];
            target.id = source.id; target.position = source.position;
            target.health = float(source.health); target.full_health = float(source.full_health);
            target.clear_rounds = target.full_clear_rounds = int(std::ceil(source.health / 3));
            target.gems = source.gems; target.gold = source.gold; target.respawn_rounds = source.respawn_rounds;
            target.lifecycle = Lifecycle::living; target.visible = target.all_spawns_visible = target.occupancy_known = true;
            target.provenance = target.health_provenance = {info.round, Source::personal_vision, Status::exact_now};
            plan.targets[g] = {source.position, source.position, true, true};
        }
        plan.information = &info; plan.menu = make_menu(info); plan.first = plan.menu.jobs[0];
    }
    Job pair(int first, int second) const {
        for (int n = 0; n < plan.menu.count; ++n) {
            auto job = plan.menu.jobs[n];
            if (job.kind == JobKind::farm && job.group == first && job.second_group == second) return job;
        }
        assert(false); return {};
    }
};
void assert_prefixes(PathState& path) {
    int steps = 0;
    while (path.advance_event()) {
        conserved(path); assert(path.own_macros_started() <= max_macros);
        assert(++steps <= (max_horizon + 1) * (actor_limit * 3 + 3));
    }
}
void fixed_pair_team_conservation_and_persistent_control_copies() {
    auto fixture = std::make_unique<FixedFixture>();
    fixture->plan.first = fixture->pair(0, 1);
    auto active = std::make_unique<PathState>(), control = std::make_unique<PathState>();
    assert(active->initialize_fixed(fixture->input, fixture->plan));
    assert(control->initialize_fixed(fixture->input, fixture->plan, false));
    assert_prefixes(*active); assert_prefixes(*control);
    assert(!active->fixed_premise_failed() && !control->fixed_premise_failed());
    assert(active->group(0).captures == 1 && active->group(1).captures == 1);
    assert(active->actor(0).secured_gems == 12 && active->actor(1).secured_gems == 13);
    assert(active->secured_output() == 25 && control->secured_output() == 36);
    assert(control->group(0, 0).captures == 1 && control->group(0, 1).captures == 1);
    int own_first_bank = -1, second_capture = -1;
    for (int n = 0; n < active->event_count(); ++n) {
        const auto& event = active->event(n);
        if (event.actor == 0 && event.kind == RecordKind::capture && event.group == 1) second_capture = event.at.round;
        if (event.actor == 0 && event.kind == RecordKind::bank && own_first_bank < 0) own_first_bank = event.at.round;
    }
    assert(second_capture >= 0 && own_first_bank > second_capture);
    assert(active->actor(1).acquired_gems == 6); // No invented second ally target.
    assert(active->own_macros_started() >= 2); // Same fixed continuation cursor.
}
void fixed_respawn_wait_macro_cap_and_missing_closure() {
    auto initial = std::make_unique<FixedFixture>(1);
    initial->input.group_count = initial->info.group_count = 1;
    initial->input.settings.max_rounds = initial->info.settings.max_rounds = 40;
    initial->input.groups[0].health = initial->info.groups[0].health = 3;
    initial->input.groups[0].position = {1, 0}; // Last living member; own stands on another dead member spawn.
    initial->input.actors[0].on_target_spawn = true; initial->info.groups[0].occupied_spawn = true;
    initial->plan.menu = make_menu(initial->info);
    for (int n = 0; n < initial->plan.menu.count; ++n)
        if (initial->plan.menu.jobs[n].kind == JobKind::hold) initial->plan.first = initial->plan.menu.jobs[n];
    auto initial_path = std::make_unique<PathState>();
    assert(initial_path->initialize_fixed(initial->input, initial->plan)); assert_prefixes(*initial_path);
    assert(initial_path->own_macros_started() >= 2 && initial_path->actor(0).acquired_gems == 11);
    assert(initial_path->group(0).captures == 1); // Initial physical occupancy did not delete a ready FARM from the fixed cycle.
    auto fixture = std::make_unique<FixedFixture>(1);
    fixture->input.group_count = fixture->info.group_count = 1;
    fixture->input.actors[0].bag = fixture->info.own.bag = 0;
    fixture->input.settings.capture_rounds = fixture->info.settings.capture_rounds = 0;
    fixture->input.groups[0].health = fixture->input.groups[0].full_health = 0;
    fixture->input.groups[0].gems = fixture->info.groups[0].gems = 0;
    fixture->input.groups[0].respawn_rounds = fixture->info.groups[0].respawn_rounds = 0;
    fixture->info.groups[0].health = fixture->info.groups[0].full_health = 0;
    fixture->info.groups[0].clear_rounds = fixture->info.groups[0].full_clear_rounds = 0;
    fixture->info.groups[0].lifecycle = Lifecycle::cleared; fixture->info.groups[0].reward_available = true;
    fixture->plan.menu = make_menu(fixture->info); fixture->plan.first = fixture->plan.menu.jobs[0];
    auto path = std::make_unique<PathState>(); assert(path->initialize_fixed(fixture->input, fixture->plan));
    assert_prefixes(*path);
    assert(path->own_macros_started() == 8 && path->group(0).captures == 8 && !path->secured_output());
    assert(path->group(0).generated_gold == 9 * fixture->input.groups[0].gold);
    // The old occupied spawn prevents immediate respawn until owner departure.
    int capture_round = -1;
    for (int n = 0; n < path->event_count(); ++n) {
        const auto& event = path->event(n);
        if (event.kind == RecordKind::capture) capture_round = event.at.round;
        if (event.kind == RecordKind::respawn) assert(event.at.round > capture_round);
    }
    fixture->input.groups[0].full_health = 6;
    fixture->input.groups[0].full_incoming_per_round = 10;
    fixture->info.groups[0].full_incoming_damage = 10;
    fixture->info.groups[0].full_health = 6; fixture->info.groups[0].full_clear_rounds = 2;
    assert(path->initialize_fixed(fixture->input, fixture->plan)); assert_prefixes(*path);
    assert(path->own_macros_started() == 8 && path->actor(0).health == 30);
    assert(path->group(0).incoming_per_round == 10); // Cleared snapshot incoming0 is not copied into respawn.
    fixture->input.groups[0].full_incoming_per_round = -1;
    assert(!path->initialize_fixed(fixture->input, fixture->plan));
    fixture = std::make_unique<FixedFixture>(1);
    fixture->input.actors[0].position = fixture->info.own.position = {0, 2};
    fixture->input.groups[0].health = 0; fixture->input.groups[0].reward_available = false;
    fixture->input.groups[0].respawn_remaining = 3;
    fixture->info.groups[0].health = 0; fixture->info.groups[0].lifecycle = Lifecycle::respawning;
    fixture->info.groups[0].respawn_remaining = 3;
    fixture->plan.menu = make_menu(fixture->info); fixture->plan.first = fixture->plan.menu.jobs[0];
    assert(path->initialize_fixed(fixture->input, fixture->plan)); path->run_to_horizon(); conserved(*path);
    assert(!path->fixed_premise_failed() && path->group(0).generated_health >= 6);
    fixture->plan.targets[0].supported = false;
    assert(path->initialize_fixed(fixture->input, fixture->plan)); path->run_to_horizon();
    assert(path->fixed_premise_failed()); // Unknown route is not a private menu deletion.
    fixture->plan.targets[0].supported = true;
    fixture->input.group_count = 1;
    assert(!path->initialize_fixed(fixture->input, fixture->plan)); // Full menu still names source1.
}
void attack_consumption_and_fixed_terminal_order() {
    auto in = base(1); in.groups[0].health = in.groups[0].full_health = 3;
    in.actors[0].capture_position = {2, 0};
    auto path = simulate(in);
    assert(record_round(*path, RecordKind::arrival, 0) == 13);
    assert(record_round(*path, RecordKind::capture, 0) == 19);
    auto moved = std::make_unique<FixedFixture>(1);
    moved->input.groups[0].health = moved->info.groups[0].health = 3;
    moved->info.groups[0].clear_rounds = 1;
    moved->input.actors[0].on_target_spawn = true;
    moved->info.groups[0].occupied_spawn = true;
    moved->plan.targets[0].capture_position = {3, 0}; // Distinct quoted public member spawn.
    assert(path->initialize_fixed(moved->input, moved->plan)); path->run_to_horizon(); conserved(*path);
    assert(record_round(*path, RecordKind::capture, 0) == 20); // Countdown restarts after actual departure.
    moved = std::make_unique<FixedFixture>(1);
    moved->input.group_count = moved->info.group_count = 1;
    moved->input.groups[0].health = moved->info.groups[0].health = 0;
    moved->info.groups[0].lifecycle = Lifecycle::cleared; moved->info.groups[0].reward_available = true;
    moved->input.actors[0].on_target_spawn = true; moved->info.groups[0].occupied_spawn = true;
    moved->input.actors[0].health = moved->info.own.health = 50;
    moved->info.own.recall_safe = true; moved->info.own.escape_rounds = 8;
    moved->plan.escape_endpoint = {0, 8}; moved->plan.escape_endpoint_supported = true;
    moved->plan.menu = make_menu(moved->info);
    for (int n = 0; n < moved->plan.menu.count; ++n)
        if (moved->plan.menu.jobs[n].kind == JobKind::renew) moved->plan.first = moved->plan.menu.jobs[n];
    assert(path->initialize_fixed(moved->input, moved->plan));
    while (path->round() < 17) assert(path->advance_event());
    conserved(*path); assert(path->group(0).captures == 0 && path->actor(0).acquired_gems == 0);
    auto fixture = std::make_unique<FixedFixture>();
    fixture->input.our_score = fixture->info.our_score = 987;
    fixture->plan.first = fixture->pair(0, 1);
    assert(path->initialize_fixed(fixture->input, fixture->plan)); assert_prefixes(*path);
    assert(path->won() && path->secured_output() == 13 && path->actor(0).secured_gems == 0);
    assert(record_round(*path, RecordKind::bank, 0) == -1);
}
void entire_fixed_menu_common_selection_and_renewal() {
    auto fixture = std::make_unique<FixedFixture>();
    auto path = std::make_unique<PathState>();
    auto forecasts = std::make_unique<std::array<Forecast, max_jobs>>();
    for (int n = 0; n < fixture->plan.menu.count; ++n) {
        fixture->plan.first = fixture->plan.menu.jobs[n];
        assert(predict_fixed_into(fixture->input, fixture->plan, true, *path, (*forecasts)[n]));
        assert((*forecasts)[n].our_secured == path->secured_output());
        assert(valid_objective((*forecasts)[n].objective));
        conserved(*path);
    }
    const int chosen = choose(fixture->info, fixture->plan.menu, *forecasts);
    assert(chosen >= 0 && chosen < fixture->plan.menu.count);
    fixture->plan.first = fixture->plan.menu.jobs[chosen];
    assert(predict_fixed_into(fixture->input, fixture->plan, false, *path, (*forecasts)[chosen]));
    assert(path->accounting().independent_resource_copies);
    for (int n = 0; n < fixture->plan.menu.count; ++n) if (fixture->plan.menu.jobs[n].kind == JobKind::renew) {
        fixture->plan.first = fixture->plan.menu.jobs[n];
        fixture->input.actors[0].health = fixture->info.own.health = 50;
        fixture->info.own.recall_safe = true; fixture->info.own.escape_rounds = 4;
        fixture->plan.escape_endpoint = {0, 4}; fixture->plan.escape_endpoint_supported = true;
        assert(predict_fixed_into(fixture->input, fixture->plan, true, *path, (*forecasts)[n]));
        assert(record_round(*path, RecordKind::recall_arrival, 0) == 19);
        assert(path->actor(0).health == 100 && path->own_macros_started() >= 2);
        fixture->plan.escape_endpoint_supported = false;
        assert(!predict_fixed_into(fixture->input, fixture->plan, true, *path, (*forecasts)[n]));
        break;
    }
    fixture = std::make_unique<FixedFixture>();
    fixture->info.groups[1].provenance.observed_round--;
    assert(!path->initialize_fixed(fixture->input, fixture->plan));
    fixture = std::make_unique<FixedFixture>();
    fixture->input.groups[0].gems = 1000;
    assert(!path->initialize_fixed(fixture->input, fixture->plan));
    fixture = std::make_unique<FixedFixture>();
    fixture->input.groups[0].health = 0;
    assert(!path->initialize_fixed(fixture->input, fixture->plan));
    fixture = std::make_unique<FixedFixture>();
    fixture->input.groups[0].reward_available = false;
    assert(!path->initialize_fixed(fixture->input, fixture->plan));
    fixture = std::make_unique<FixedFixture>();
    fixture->plan.first = fixture->pair(0, 1);
    auto& ally = fixture->input.actors[1];
    ally.group = 1; ally.position = ally.attack_position = {11, 0}; ally.capture_position = {10, 0};
    ally.work_per_action = .001; ally.participation = evidence(ally.id, fixture->input.groups[1].id, 3);
    assert(!predict_fixed_into(fixture->input, fixture->plan, true, *path, (*forecasts)[0]));
    assert(path->fixed_premise_failed()); // A later target cannot invent a duplicate physical capture tile.
    fixture = std::make_unique<FixedFixture>();
    Gate gate; fixture->input.work_budget = {allowed, &gate};
    assert(!predict_fixed_into(fixture->input, fixture->plan, true, *path, (*forecasts)[0]));
    assert((*forecasts)[0].budget_exhausted && !(*forecasts)[0].our_secured);
}
}

int main() {
    legal_inference_and_ambiguity_fallback();
    shared_work_stock_remainder_and_colored_bags();
    same_group_ranking_changes_and_different_group_negative();
    cutoff_and_no_output_before_completed_bank();
    capture_precedes_existing_channel_and_uses_current_bag();
    pathwise_terminal_stops_later_actor();
    nearest_altars_are_independent_without_mutex();
    camp_damage_is_once_before_actions_and_death_loses_only_own_bag();
    witnessed_holds_actual_occupancy_and_no_invented_target();
    late_actor_keeps_existing_bag_without_a_second_cycle();
    bounded_resume_copy_and_initial_turn_order();
    maximum_actors_and_fail_closed_unknowns();
    fixed_pair_team_conservation_and_persistent_control_copies();
    fixed_respawn_wait_macro_cap_and_missing_closure();
    attack_consumption_and_fixed_terminal_order();
    entire_fixed_menu_common_selection_and_renewal();
    std::puts("plan4 foundry: 16 pure scenarios passed (no engine witness)");
}
