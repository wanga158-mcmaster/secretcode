// Independent synthetic forecast oracle. These ticks are not native calibration.
// Heap fixtures keep the default WASI stack available for bounded builder calls.
#include "../common/src/observe.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>

namespace {
using namespace p6;
namespace fc = p6::forecast;
int checks = 0;
void check(bool condition, const char* name) {
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAIL cycle forecast: %s\n", name); std::exit(1); }
}
bool equal(double a, double b) { return std::abs(a - b) < 1e-9; }
template<class T> Fact<T> map_fact(T value) { return {value, Source::public_map}; }
template<class T> Fact<T> setting(T value) { return {value, Source::queried_setting}; }
template<class T> Observation<T> seen(T value, ViewClock clock, int subject) {
    return {value, clock, subject, Knowledge::exact};
}
struct Fixture { Frame frame{}; fc::Request request{}; };

void leg(fc::Request& request, Point from, Point to, int distance) {
    check(request.leg_count < static_cast<int>(request.legs.size()), "fixture leg capacity");
    request.legs[request.leg_count++] = {from, to, map_fact(distance)};
}
void group(Fixture& f, int index, int id, Point position, Gems gems = 10) {
    auto& g = f.request.groups[index];
    g.observed = seen(Group{id, position, position, position, Lifecycle::living,
                            true, true, true}, f.frame.clock, id);
    g.membership_complete = map_fact(true);
    g.member_count = 1;
    g.members[0].public_full = map_fact(MonsterWork{100 + index, 2, 1});
    g.members[0].current = seen(MonsterWork{100 + index, 2, 1}, f.frame.clock, 100 + index);
    g.gems = map_fact(gems); g.gold = map_fact<Gems>(7);
}
std::unique_ptr<Fixture> fixture(fc::Shape shape = fc::Shape::produce_bank) {
    auto f = std::make_unique<Fixture>();
    f->frame.clock = {0, 1, 0};
    auto& self = f->frame.own.actor;
    self.id = 1; self.pick_ordinal = 0; self.team = Team::ally;
    self.position = {0, 0}; self.health = self.max_health = 100;
    self.strength = 1; self.alive = self.can_move = self.can_attack = true;
    self.attack_range = 1; self.ready_attack_group = 17;
    f->frame.own.gold = 9;
    auto& r = f->request;
    r.shape = shape; r.group_count = 1;
    r.timing.settings = setting(Settings{100, 1000, 2});
    r.timing.tribute_multiplier = setting(.1);
    r.timing.tribute_base = setting(0); r.timing.tribute_minimum = setting(1);
    r.kit = seen(Productivity{1, 0, false, false, false, false}, f->frame.clock, 1);
    r.ambient_incoming = seen(0., f->frame.clock, 1);
    r.bank_waypoint = 90; r.renewal_waypoint = 91;
    r.bank_position = {1, 0}; r.renewal_position = {0, 0};
    r.bank_available = seen(true, f->frame.clock, r.bank_waypoint);
    group(*f, 0, 17, {0, 0});
    f->frame.group = r.groups[0].observed;
    if (shape == fc::Shape::bank_then_produce_bank) {
        leg(r, self.position, r.bank_position, 1);
        leg(r, r.bank_position, {0, 0}, 1);
        leg(r, {0, 0}, {0, 0}, 0);
        leg(r, {0, 0}, r.bank_position, 1);
    } else {
        leg(r, self.position, {0, 0}, 0);
        leg(r, {0, 0}, {0, 0}, 0);
        leg(r, {0, 0}, r.bank_position, 1);
    }
    leg(r, r.bank_position, r.renewal_position, 1);
    return f;
}
std::unique_ptr<fc::Result> build(const Fixture& f) {
    return std::make_unique<fc::Result>(fc::build(f.frame, f.request));
}
void feasible(const Fixture& f, const fc::Result& r, const char* name) {
    check(r.status == Status::feasible && r.issue == fc::Issue::none, name);
    check(r.certificate.valid(f.frame, 1, 17), "generated certificate matches current self/group");
    check(r.schedule.actor_count == 1 && r.schedule.actors[0].actor_id == 1,
          "only own actor is scheduled");
    check(r.phase_count >= 0 && r.phase_count <= 16, "bounded phase count");
    check(r.schedule.production_count <= 2 && r.schedule.actors[0].event_count <= 7,
          "bounded production and owner event counts");
    int productions = 0, tributes = 0;
    for (int i = 0; i < r.schedule.actors[0].event_count; ++i) {
        productions += r.schedule.actors[0].events[i].kind == EventKind::production;
        tributes += r.schedule.actors[0].events[i].kind == EventKind::tribute;
    }
    check(productions <= 2 && tributes <= 2, "at most two productions and two whole-bag tributes");
}
void rejected(const Fixture& f, const char* name) {
    auto r = build(f);
    check(r->status != Status::feasible && r->issue != fc::Issue::none, name);
}
int event_tick(const fc::Result& r, EventKind kind, int ordinal = 0) {
    const auto& owner = r.schedule.actors[0];
    for (int i = 0; i < owner.event_count; ++i)
        if (owner.events[i].kind == kind && ordinal-- == 0) return owner.events[i].tick;
    return -1;
}
const fc::PhaseTerm* phase(const fc::Result& r, fc::PhaseKind kind, int ordinal = 0) {
    for (int i = 0; i < r.phase_count; ++i)
        if (r.phases[i].kind == kind && ordinal-- == 0) return &r.phases[i];
    return nullptr;
}
ScenarioWindows windows(const Fixture& f, int a, int b, int c) {
    ScenarioWindows w;
    w.deadlines = {a, b, c}; w.windows = {a, b, c};
    w.sampled_round = f.frame.clock.round; w.sampled_actor_id = 1;
    w.source = Source::synthetic_fixture; w.valid = true; return w;
}
ScoreEvaluation score(const Fixture& f, const fc::Result& r, int a, int b, int c) {
    return evaluate_schedule(r.schedule, windows(f, a, b, c), f.request.timing.settings.value,
                             {f.frame.clock.round, 1}, f.frame.own_team_score);
}
void baseline() {
    auto f = fixture(); auto r = build(*f); feasible(*f, *r, "single production cycle resolves");
    const auto* clear = phase(*r, fc::PhaseKind::clear);
    const auto* hold = phase(*r, fc::PhaseKind::hold);
    const auto* tribute = phase(*r, fc::PhaseKind::tribute);
    check(clear && clear->start == 0 && clear->finish == 2, "HP2 / melee1 clears in two turns");
    check(hold && hold->start == 2 && hold->finish == 5, "full capture is queried C2 plus initial phase");
    check(tribute && tribute->start == 8 && tribute->finish == 9,
          "one-step route adds two slack; D1 tribute completes at start plus one");
    check(event_tick(*r, EventKind::production) == 5 && event_tick(*r, EventKind::tribute) == 9 &&
          event_tick(*r, EventKind::renewal) == 12, "hand cycle ticks 5/9/12");
    check(equal(r->certificate.recovery.incoming_total, 2) &&
          equal(r->certificate.recovery.health - r->certificate.recovery.incoming_total, 98),
          "two camp damage is cumulative through renewal");
    check(r->schedule.actors[0].occupied_turns.value == 12, "all twelve occupied turns counted");
    check(r->first.stage == fc::FirstStage::clear && r->first.actor == 1 && r->first.group == 17,
          "first action is a clearing proposal");
    check(f->frame.own.actor.bag == 0 && f->frame.own.gold == 9 &&
          r->schedule.productions[0].source == Source::scheduled,
          "quote does not become observed resource receipt");
    auto y = score(*f, *r, 8, 9, 12);
    check(y.eligible() && y.own_score == std::array<Gems, 3>{0, 10, 10},
          "score begins exactly at completed bank tick nine");
    f->frame.own.actor.bag = 20; r = build(*f);
    feasible(*f, *r, "initial bag included in produced bank");
    check(event_tick(*r, EventKind::tribute) == 11 && event_tick(*r, EventKind::renewal) == 14,
          "whole thirty-gem bag costs D3 and returns at fourteen");
    y = score(*f, *r, 10, 11, 14);
    check(y.eligible() && y.own_score == std::array<Gems, 3>{0, 30, 30}, "no partial liquidation");
}
void all_zero_legs(Fixture& f) {
    auto& r = f.request;
    r.bank_position = r.renewal_position = f.frame.own.actor.position;
    r.leg_count = 0;
    for (int i = 0; i < 4; ++i) leg(r, {0, 0}, {0, 0}, 0);
}
void exact_timing() {
    auto f = fixture(); all_zero_legs(*f); auto r = build(*f);
    feasible(*f, *r, "identical endpoints need no movement");
    check(event_tick(*r, EventKind::tribute) == 6 && event_tick(*r, EventKind::renewal) == 6,
          "zero legs incur no collision slack");
    f = fixture(); f->request.timing.settings.value.capture_rounds = 5; r = build(*f);
    check(r->status == Status::feasible && event_tick(*r, EventKind::production) == 8 &&
          event_tick(*r, EventKind::tribute) == 12 && event_tick(*r, EventKind::renewal) == 15,
          "queried C5 changes every later completion");
    f = fixture(); f->request.timing.settings = setting(Settings{16, 20, 2});
    r = build(*f); feasible(*f, *r, "literal sixteen-round twenty-gem settings");
    check(event_tick(*r, EventKind::renewal) == 12, "no copied official round or gem constants");
    f->frame.clock.round = 4;
    f->frame.group.clock = f->request.groups[0].observed.clock = f->frame.clock;
    f->request.groups[0].members[0].current.clock = f->frame.clock;
    f->request.kit.clock = f->request.ambient_incoming.clock = f->request.bank_available.clock = f->frame.clock;
    r = build(*f);
    check(r->status == Status::feasible && event_tick(*r, EventKind::renewal) == 12,
          "relative twelve finishes at absolute queried horizon sixteen from round four");
    f->request.timing.settings.value.max_rounds = 15; r = build(*f);
    check(r->status != Status::feasible && r->issue == fc::Issue::horizon,
          "same route is incomplete one round before absolute horizon sixteen");
    f = fixture();
    f->request.timing.settings.value.max_rounds = 12; r = build(*f);
    check(r->status == Status::feasible, "renewal exactly at official horizon is complete");
    f->request.timing.settings.value.max_rounds = 11; r = build(*f);
    check(r->status != Status::feasible && r->issue == fc::Issue::horizon,
          "bank in horizon cannot excuse renewal after horizon");

    // Start directly at the bank. ceil(.2*11+2)=5, above queried minimum three.
    f = fixture(fc::Shape::bank_then_produce_bank);
    f->frame.own.actor.bag = 11; f->request.bank_position = {0, 0};
    group(*f, 0, 17, {1, 0}); f->frame.group = f->request.groups[0].observed;
    auto& q = f->request; q.leg_count = 0;
    leg(q, {0, 0}, {0, 0}, 0); leg(q, {0, 0}, {1, 0}, 1);
    leg(q, {1, 0}, {1, 0}, 0); leg(q, {1, 0}, {0, 0}, 1);
    leg(q, {0, 0}, q.renewal_position, 0);
    q.timing.tribute_multiplier = setting(.2); q.timing.tribute_base = setting(2);
    q.timing.tribute_minimum = setting(3); r = build(*f);
    const auto* tribute = phase(*r, fc::PhaseKind::tribute);
    check(r->status == Status::feasible && tribute && tribute->start == 0 && tribute->finish == 5,
          "queried tribute multiplier/base/minimum use ceil once and no extra tick");
}
void two_groups(Fixture& f, fc::Shape shape) {
    auto& q = f.request; q.shape = shape; q.group_count = 2;
    group(f, 1, 18, {0, 1}); q.leg_count = 0;
    leg(q, {0, 0}, {0, 0}, 0); leg(q, {0, 0}, {0, 0}, 0);
    if (shape == fc::Shape::produce_bank_produce_bank) {
        leg(q, {0, 0}, q.bank_position, 1); leg(q, q.bank_position, {0, 1}, 1);
    } else leg(q, {0, 0}, {0, 1}, 1);
    leg(q, {0, 1}, {0, 1}, 0); leg(q, {0, 1}, q.bank_position, 1);
    leg(q, q.bank_position, q.renewal_position, 1);
}
void shapes_and_windows() {
    auto f = fixture(); two_groups(*f, fc::Shape::produce_bank_produce_bank);
    auto r = build(*f); feasible(*f, *r, "two groups with two banks resolve");
    const auto* second_clear = phase(*r, fc::PhaseKind::clear, 1);
    check(second_clear && second_clear->start == 12 && second_clear->finish == 14,
          "second clear follows explicit bank-to-approach route");
    check(r->schedule.production_count == 2 && r->schedule.productions[0].tick == 5 &&
          r->schedule.productions[1].tick == 17, "separate payouts at five and seventeen");
    check(event_tick(*r, EventKind::tribute) == 9 && event_tick(*r, EventKind::tribute, 1) == 21 &&
          event_tick(*r, EventKind::renewal) == 24, "two-bank cycle ticks nine/twenty-one/twenty-four");
    check(equal(r->certificate.recovery.incoming_total, 4), "second group does not reset own health");
    auto y = score(*f, *r, 20, 21, 24);
    check(y.eligible() && y.own_score == std::array<Gems, 3>{10, 20, 20}, "both banks count once");
    two_groups(*f, fc::Shape::retain_two_then_bank); r = build(*f);
    feasible(*f, *r, "two retained groups have one whole-bag liquidation");
    check(event_tick(*r, EventKind::production, 1) == 13 && event_tick(*r, EventKind::tribute) == 18 &&
          event_tick(*r, EventKind::tribute, 1) == -1 && event_tick(*r, EventKind::renewal) == 21,
          "retain route: first5, travel3, clear2, capture3, travel3, D2, return3");

    f = fixture(fc::Shape::bank_then_produce_bank); f->frame.own.actor.bag = 20; r = build(*f);
    feasible(*f, *r, "bank initial stock before production");
    check(event_tick(*r, EventKind::tribute) == 5 && event_tick(*r, EventKind::production) == 13 &&
          event_tick(*r, EventKind::tribute, 1) == 17 && event_tick(*r, EventKind::renewal) == 20,
          "generated conventional bank20 at five and bank10 at seventeen");
    y = score(*f, *r, 10, 20, 20);
    check(y.eligible() && y.own_score == std::array<Gems, 3>{20, 30, 30},
          "generated conventional score under ten/twenty windows");
    f = fixture(); f->frame.own.actor.bag = 20; f->request.groups[0].gems = map_fact<Gems>(30);
    r = build(*f); feasible(*f, *r, "larger retained reward has completed liquidation");
    check(event_tick(*r, EventKind::tribute) == 13 && event_tick(*r, EventKind::renewal) == 16,
          "generated retained fifty completes D5 at thirteen and renews sixteen");
    y = score(*f, *r, 10, 20, 20);
    check(y.eligible() && y.own_score == std::array<Gems, 3>{0, 50, 50},
          "generated deadline reversal credits no retained stock before liquidation");
}
void bank_owned_inventory() {
    auto f = fixture(); auto& q = f->request;
    q.shape = fc::Shape::bank_only; q.project_group = 17; q.group_count = 0;
    f->frame.own.actor.bag = 11;
    f->frame.group.value.lifecycle = Lifecycle::respawning;
    q.groups[0].observed.value.lifecycle = Lifecycle::respawning;
    q.bank_position = {0, 0}; q.renewal_position = {1, 0}; q.leg_count = 0;
    leg(q, {0, 0}, {0, 0}, 0); leg(q, {0, 0}, {1, 0}, 1);
    q.timing.tribute_multiplier = setting(.2); q.timing.tribute_base = setting(2);
    q.timing.tribute_minimum = setting(3); auto r = build(*f);
    feasible(*f, *r, "owned inventory can bank while source group respawns");
    check(r->schedule.production_count == 0 && r->schedule.target_id == 90 &&
          r->certificate.group == 17, "bank-only keeps project anchor without inventing production");
    check(event_tick(*r, EventKind::production) == -1 && event_tick(*r, EventKind::tribute) == 5 &&
          event_tick(*r, EventKind::renewal) == 8, "bank-only: D5 then one-step-plus-two renewal");
    check(r->first.stage == fc::FirstStage::tribute && r->first.actor == 1,
          "bank-only first proposal starts own tribute");
    auto y = score(*f, *r, 4, 5, 8);
    check(y.eligible() && y.own_score == std::array<Gems, 3>{0, 11, 11},
          "bank-only credits actual owned bag only on completion");
    q.timing.tribute_multiplier = setting(.1); q.timing.tribute_base = setting(0);
    q.timing.tribute_minimum = setting(1); f->frame.own.actor.bag = 7; r = build(*f);
    check(r->status == Status::feasible && event_tick(*r, EventKind::tribute) == 1 &&
          event_tick(*r, EventKind::renewal) == 4, "bank-only default bag7 costs D1 and renewal3");
    f->frame.own.actor.bag = 0; rejected(*f, "bank-only cannot liquidate empty inventory");
}
void combat_and_observation() {
    auto f = fixture(); auto& own = f->frame.own.actor;
    own.strength = 2; own.ranged = true; own.attack_range = 3;
    f->request.kit.value = {2, 8, true, false, true, false};
    f->request.groups[0].members[0].public_full.value.health = 3;
    f->request.groups[0].members[0].current.value.health = 3;
    auto r = build(*f); const auto* clear = phase(*r, fc::PhaseKind::clear);
    check(r->status == Status::feasible && clear && clear->finish == 3,
          "ranged2 becomes rate1; unavailable A8 supplies no cast damage");
    f->request.kit.value.cast_ready = true; r = build(*f); clear = phase(*r, fc::PhaseKind::clear);
    check(r->status == Status::feasible && clear && clear->finish == 2,
          "one legally scheduled A8 adds one rate, giving ceil3/2=2");
    f->request.kit.value.cast_reserved_for_protection = true; r = build(*f);
    check(r->status == Status::feasible && phase(*r, fc::PhaseKind::clear)->finish == 3,
          "protection-reserved cast supplies no offensive credit");

    f = fixture(); auto& g = f->request.groups[0];
    g.observed.value.all_spawns_visible = false;
    g.members[0].public_full.value.health = 5;
    g.members[0].current = {}; r = build(*f);
    feasible(*f, *r, "one original spawn allows lifecycle with incomplete spawn visibility");
    check(phase(*r, fc::PhaseKind::clear)->finish == 5 && event_tick(*r, EventKind::production) == 8,
          "unseen member uses full public HP; unseen occupancy is not invented");
    g.members[0].current = seen(MonsterWork{100, 1, 1}, f->frame.clock, 100);
    g.members[0].current.clock.revision = 99; r = build(*f);
    check(r->status == Status::feasible && phase(*r, fc::PhaseKind::clear)->finish == 5,
          "stale member HP cannot improve public-full work bound");
    g.members[0].current = seen(MonsterWork{100, 0, 1}, f->frame.clock, 100);
    g.observed.value.lifecycle = Lifecycle::reward_available; r = build(*f);
    check(r->status == Status::feasible && event_tick(*r, EventKind::production) == 3,
          "reward-available lifecycle receives fresh full C2+1 capture, never hidden progress");

    f = fixture(); auto& all = f->request.groups[0]; all.member_count = 2;
    all.members[0].public_full.value = {100, 1, 3};
    all.members[0].current = seen(MonsterWork{100, 1, 3}, f->frame.clock, 100);
    all.members[1].public_full = map_fact(MonsterWork{101, 1, 3});
    all.members[1].current = seen(MonsterWork{101, 1, 3}, f->frame.clock, 101);
    Actor helper = f->frame.own.actor; helper.id = 2; helper.strength = 500; helper.health = 1;
    helper.bag = 300; f->frame.actors[0] = seen(helper, f->frame.clock, 2); f->frame.actor_count = 1;
    r = build(*f); feasible(*f, *r, "visible ally supplies neither invented damage nor healing");
    check(phase(*r, fc::PhaseKind::clear)->finish == 4 &&
          equal(r->certificate.clear.incoming_total, 15),
          "all awake: first1*6 plus reposition2*3 plus second1*3 equals fifteen");
    check(!personal_gold(f->frame, 2).has_value() && r->schedule.actors[0].initial_gold.value == 9 &&
          r->schedule.actors[0].initial_bag.value == 0, "external actor cannot donate personal resources");

    f = fixture(); auto& capped = f->request.groups[0]; capped.member_count = 32;
    for (int i = 1; i < 32; ++i) {
        capped.members[i].public_full = map_fact(MonsterWork{100 + i, 1, 0});
        capped.members[i].current = seen(MonsterWork{100 + i, 0, 0}, f->frame.clock, 100 + i);
    }
    r = build(*f);
    check(r->status == Status::feasible && event_tick(*r, EventKind::production) == 5,
          "exact thirty-two-member prototype cap is accepted without truncation");
}
void shields_and_safety() {
    auto f = fixture(); f->request.shield.enabled = true;
    f->request.shield.amount = seen(100., f->frame.clock, 1);
    f->request.shield.expires_before_tick = seen(1, f->frame.clock, 1);
    auto r = build(*f); feasible(*f, *r, "shield with known boundary expiry resolves");
    check(equal(phase(*r, fc::PhaseKind::clear)->shield_absorbed, 0),
          "damage tick one gets no shield expiring at boundary one");
    f->request.shield.expires_before_tick.value = 2; r = build(*f);
    check(equal(phase(*r, fc::PhaseKind::clear)->shield_absorbed, 1),
          "only tick strictly before expiry gets shield credit");
    f->request.ambient_incoming.value = 1; r = build(*f);
    feasible(*f, *r, "ambient exposure with early-expiring shield resolves");
    double shield = 0;
    for (int i = 0; i < r->phase_count; ++i) {
        shield += r->phases[i].shield_absorbed;
        if (r->phases[i].start >= 2)
            check(equal(r->phases[i].shield_absorbed, 0), "expired shield never protects late bank/return");
    }
    check(equal(shield, 2) && equal(r->phases[r->phase_count - 1].health_after, 88),
          "twelve ambient plus two camp damage minus two early shield leaves HP88");
    f->request.shield.amount.knowledge = Knowledge::unknown; rejected(*f, "unknown shield amount unresolved");
    f->request.shield.amount = seen(100., f->frame.clock, 1);
    f->request.shield.expires_before_tick.knowledge = Knowledge::unknown;
    rejected(*f, "unknown expiry cannot backdate shield credit");

    f = fixture(); f->frame.own.actor.health = 21; rejected(*f, "HP19 after clear fails twenty-percent reserve");
    f->frame.own.actor.health = 22; r = build(*f);
    check(r->status == Status::feasible, "HP20 after clear meets twenty-percent reserve exactly");
    f = fixture(); all_zero_legs(*f);
    f->request.groups[0].observed.value.lifecycle = Lifecycle::reward_available;
    f->request.groups[0].members[0].current.value.health = 0;
    f->request.ambient_incoming.value = 11; f->frame.own.actor.health = 65;
    rejected(*f, "remaining21 passes twenty-percent but fails two-next-phase22 reserve");
    f->frame.own.actor.health = 66; r = build(*f);
    check(r->status == Status::feasible, "remaining22 meets two-next-phase reserve exactly");

    f = fixture(); Actor enemy = f->frame.own.actor;
    enemy.id = 20; enemy.team = Team::enemy; enemy.strength = 2;
    f->frame.actors[0] = seen(enemy, f->frame.clock, 20); f->frame.actor_count = 1;
    rejected(*f, "visible enemy without full cast stress bound is unresolved");
    f->request.enemy_cast_count = 1;
    f->request.enemy_cast_stress[0] = seen(3., f->frame.clock, 20); r = build(*f);
    feasible(*f, *r, "visible enemy full-cast stress bound supplied");
    check(equal(r->certificate.recovery.incoming_total, 62),
          "twelve turns of enemy basic2 plus stress3 and camp2 equals sixty-two");
}
void recovery_and_rejections() {
    auto f = fixture(); auto& q = f->request;
    q.recovery = {true, false, 92, {1, 1}, map_fact(3)};
    q.leg_count = 3; leg(q, q.bank_position, q.recovery.position, 1);
    leg(q, q.recovery.position, q.renewal_position, 1);
    auto r = build(*f); feasible(*f, *r, "named recovery wait with routes resolves");
    check(event_tick(*r, EventKind::recovery) == 15 && event_tick(*r, EventKind::renewal) == 18 &&
          equal(r->certificate.recovery.incoming_total, 2), "recovery waits three without manufacturing health");
    q.recovery.requires_health_restore = true; r = build(*f);
    check(r->status == Status::unresolved && r->issue == fc::Issue::health_restore_unmodeled &&
          r->recovery_health_unmodeled, "requested actual health restoration is explicitly unmodeled");
    f = fixture(); f->request.purchase_requested = true; r = build(*f);
    check(r->status == Status::unresolved && r->issue == fc::Issue::unsupported_purchase &&
          r->purchases_deferred, "purchase request is unresolved rather than imaginary benefit");

    f = fixture(); f->request.legs[2].distance.source = Source::unknown; rejected(*f, "unknown route is never zero");
    f = fixture(); f->request.groups[0].membership_complete.source = Source::unknown;
    rejected(*f, "unknown complete membership is unresolved");
    f = fixture(); f->request.groups[0].gold.source = Source::unknown; rejected(*f, "unknown camp gold is unresolved");
    f = fixture(); f->request.groups[0].gems.source = Source::unknown; rejected(*f, "unknown camp gems is unresolved");
    f = fixture(); f->request.groups[0].members[0].public_full.source = Source::unknown;
    rejected(*f, "unknown public membership work is unresolved");
    f = fixture(); f->request.groups[0].observed.value.lifecycle = Lifecycle::unknown;
    rejected(*f, "unknown lifecycle is unresolved");
    f = fixture(); f->request.groups[0].observed.value.original_spawn_visible = false;
    rejected(*f, "no personally visible original spawn cannot establish lifecycle");
    f = fixture(); f->request.groups[0].observed.value.uncontested_now = false;
    rejected(*f, "observed contested capture is unavailable");
    f = fixture(); f->request.groups[0].observed.clock.revision = 1; rejected(*f, "stale group observation rejected");
    f = fixture(); f->request.kit.knowledge = Knowledge::unknown; rejected(*f, "unknown kit cannot create damage");
    f = fixture(); f->request.ambient_incoming.knowledge = Knowledge::unknown;
    rejected(*f, "missing ambient bound cannot become zero incoming");
    f = fixture(); f->request.bank_available.knowledge = Knowledge::unknown;
    rejected(*f, "bank access requires a current personal observation");
    f = fixture(); f->request.bank_available.value = false; rejected(*f, "known unavailable bank rejected");
    f = fixture(); f->request.timing.settings.source = Source::unknown; rejected(*f, "unknown settings rejected");
    f = fixture(); f->request.timing.tribute_multiplier.source = Source::unknown;
    rejected(*f, "unknown queried tribute knob rejected");

    f = fixture(); two_groups(*f, fc::Shape::produce_bank_produce_bank);
    f->request.groups[1] = f->request.groups[0]; rejected(*f, "same source group cannot pay twice");
    f = fixture(); f->request.group_count = 3; rejected(*f, "third production cannot overflow prototype bound");
    f = fixture(); f->request.groups[0].member_count = 33; rejected(*f, "thirty-third member cannot be truncated");
    f = fixture(); f->request.group_count = -1; rejected(*f, "negative group count rejected");
    f = fixture(); f->request.leg_count = 9; rejected(*f, "leg count checked before indexing");
    f = fixture(); f->request.enemy_cast_count = 7; rejected(*f, "cast count checked before indexing");
    f = fixture(); f->frame.actor_count = 7; rejected(*f, "visible actor count checked before indexing");
    f = fixture(); f->frame.own.actor.team = Team::enemy; rejected(*f, "enemy self cannot supply own resources");
    f = fixture(); f->frame.clock.self = 2; rejected(*f, "frame clock and own identity must agree");
    f = fixture(); f->request.kit.subject = 2; rejected(*f, "kit subject must be self");
    f = fixture(); f->request.groups[0].observed.subject = 18; rejected(*f, "group observation subject must agree");
    f = fixture(); f->request.legs[2].from = {8, 8}; rejected(*f, "route endpoint mismatch rejected");
    f = fixture(); f->request.legs[2].distance.value = 0; rejected(*f, "zero-distance unequal endpoints rejected");
    f = fixture(); f->frame.own.actor.bag = std::numeric_limits<Gems>::max(); r = build(*f);
    check(r->status != Status::feasible && r->issue == fc::Issue::overflow, "bag addition overflow rejected");
    f = fixture(); f->request.legs[2].distance.value = std::numeric_limits<int>::max(); r = build(*f);
    check(r->status != Status::feasible, "out-of-radius route rejected before arithmetic");
    f = fixture(); f->request.groups[0].members[0].current.value.health =
        std::numeric_limits<double>::infinity(); rejected(*f, "nonfinite observed health rejected");
}
} // namespace
int main() {
    baseline(); exact_timing(); shapes_and_windows(); bank_owned_inventory(); combat_and_observation();
    shields_and_safety(); recovery_and_rejections();
    std::printf("{\"case\":\"cycle-forecast\",\"checks\":%d,\"status\":\"passed\","
                "\"oracle\":\"independent-synthetic-ticks\",\"nativeCalibration\":false}\n", checks);
}
