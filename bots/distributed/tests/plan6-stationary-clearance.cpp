// Complete forecast/binding checks for the optional stationary basic witness.
// Action oracle: retained Caravan Monolith B Tavi719 moves without attacking;
// Tavi720 casts then basics, and721 basics/resets/basics from the same tile.
// This correction credits only one stationary basic per turn, not those spells
// or resets. These synthetic checks are not a new native fuel qualification.
#include "../reference/src/policy.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {
using namespace p6;
namespace fc = p6::forecast;
int checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAIL stationary clearance: %s\n", message); std::exit(1); }
}
bool equal(double a, double b) { return std::abs(a - b) < 1e-9; }
template<class T> Observation<T> seen(T value, ViewClock clock, int subject) {
    return {value, clock, subject, Knowledge::exact};
}
template<class T> Fact<T> public_map(T value) { return {value, Source::public_map}; }
template<class T> Fact<T> setting(T value) { return {value, Source::queried_setting}; }
struct Fixture { Frame frame{}; fc::Request request{}; };
std::unique_ptr<Fixture> fixture() {
    auto f = std::make_unique<Fixture>();
    f->frame.clock = {720, 9, 2};
    auto& own = f->frame.own.actor;
    own.id = 9; own.pick_ordinal = 4; own.team = Team::ally;
    own.alive = own.can_move = own.can_attack = own.ranged = true;
    own.position = {34,1}; own.health = own.max_health = 750;
    own.strength = 45; own.attack_range = 3; own.ready_attack_group = 4;
    auto& r = f->request;
    r.shape = fc::Shape::produce_bank; r.group_count = 1;
    r.timing = {setting(Settings{2000,1000,5}),setting(.1),setting(0),setting(1)};
    r.kit = seen(Productivity{45,0,true,false,false,false},f->frame.clock,own.id);
    r.ambient_incoming = seen(12.,f->frame.clock,own.id);
    r.bank_waypoint = 90; r.bank_position = {35,3};
    r.renewal_waypoint = 4; r.renewal_position = own.position;
    r.bank_available = seen(true,f->frame.clock,r.bank_waypoint);
    auto& g = r.groups[0];
    g.observed = seen(Group{4,own.position,{34,3},{33,2},Lifecycle::living,true,true,true},
                      f->frame.clock,4);
    g.membership_complete = public_map(true); g.member_count = 1;
    g.members[0].public_full = public_map(MonsterWork{6,90,10});
    g.members[0].current = seen(MonsterWork{6,90,10},f->frame.clock,6);
    g.gems = public_map<Gems>(10); g.gold = public_map<Gems>(40);
    g.ambient_overlap = seen(10.,f->frame.clock,4);
    f->frame.group = g.observed;
    r.legs[r.leg_count++] = {own.position,g.observed.value.approach,public_map(0)};
    r.legs[r.leg_count++] = {g.observed.value.approach,g.observed.value.spawn,public_map(2)};
    r.legs[r.leg_count++] = {g.observed.value.spawn,r.bank_position,public_map(1)};
    r.legs[r.leg_count++] = {r.bank_position,r.renewal_position,public_map(2)};
    return f;
}
std::unique_ptr<fc::Result> build(const Fixture& f) {
    return std::make_unique<fc::Result>(fc::build(f.frame,f.request));
}
const fc::PhaseTerm& phase(const fc::Result& r, fc::PhaseKind kind) {
    for (int i = 0; i < r.phase_count; ++i) if (r.phases[i].kind == kind) return r.phases[i];
    check(false,"expected phase exists"); std::abort();
}
int clearance(const fc::Result& r) {
    const auto& p = phase(r,fc::PhaseKind::clear); return p.finish - p.start;
}
void publish(Fixture& f) {
    f.request.groups[0].stationary_basic = seen(true,f.frame.clock,4);
}
void basic_timing_and_exposure() {
    auto f = fixture();
    const auto old = build(*f);
    check(old->status == Status::feasible && clearance(*old) == 4,
          "omitted witness keeps four half-rate ranged turns");
    publish(*f);
    const auto current = build(*f);
    check(current->status == Status::feasible && clearance(*current) == 2,
          "current stationary witness permits two Strength45 basics against90HP");
    check(equal(phase(*old,fc::PhaseKind::clear).incoming,48) &&
          equal(phase(*current,fc::PhaseKind::clear).incoming,24),
          "awake source and external incoming remain charged during every clear tick");
    check(equal(phase(*old,fc::PhaseKind::hold).incoming,12) &&
          equal(phase(*current,fc::PhaseKind::hold).incoming,12),
          "six capture phases retain identical external exposure");
    check(current->certificate.route_known && current->certificate.bank_executable &&
          current->certificate.renewal_reachable,"complete bank and renewal proof is retained");
    f->request.groups[0].stationary_basic.value = false;
    check(clearance(*build(*f)) == 4,"explicit false preserves previous rate");
}
void stale_or_incomplete_evidence() {
    auto f = fixture(); publish(*f);
    auto& g = f->request.groups[0];
    --g.stationary_basic.clock.revision;
    check(clearance(*build(*f)) == 4,"stale revision cannot grant stationary rate");
    publish(*f); g.stationary_basic.subject = 5;
    check(clearance(*build(*f)) == 4,"different group cannot grant stationary rate");
    publish(*f); g.stationary_basic.clock.self = 10;
    check(clearance(*build(*f)) == 4,"different actor clock cannot grant stationary rate");
    publish(*f); g.members[0].current = {};
    check(clearance(*build(*f)) == 4,"public full health is not an exact current-member witness");
    g.members[0].current = seen(MonsterWork{6,90,10},f->frame.clock,6);
    g.member_count = 2;
    g.members[1].public_full = public_map(MonsterWork{7,45,0});
    g.members[1].current = seen(MonsterWork{7,0,0},f->frame.clock,7);
    check(clearance(*build(*f)) == 2,"exact dead original member does not obstruct valid witness");
    g.members[1].current = {};
    const auto missing = build(*f);
    g.stationary_basic.value = false;
    check(clearance(*missing) == clearance(*build(*f)),
          "missing original member retains same public-full conservative calculation");
}
void action_and_position_boundaries() {
    auto f = fixture(); publish(*f);
    f->frame.own.actor.can_attack = false;
    const auto moved = build(*f);
    check(moved->status == Status::unresolved && moved->issue == fc::Issue::unavailable,
          "Tavi719-style spent ranged allowance is not restored by witness");
    f = fixture(); publish(*f);
    f->frame.own.actor.position = {34,0};
    f->request.legs[0].from = {34,0}; f->request.legs[0].distance = public_map(1);
    check(clearance(*build(*f)) == 4,"witness does not grant full rate after approach movement");
    f = fixture(); publish(*f);
    f->frame.own.actor.ranged = false;
    f->request.kit.value.ranged = false;
    const auto melee = build(*f);
    f->request.groups[0].stationary_basic = {};
    check(clearance(*melee) == 2 && clearance(*build(*f)) == 2,
          "melee calculation remains unchanged with or without witness");
    f = fixture(); publish(*f);
    f->request.kit.value.scheduled_single_cast = 800;
    f->request.kit.value.cast_ready = f->request.kit.value.cast_legal_at_first_turn = true;
    check(clearance(*build(*f)) == 2,"stationary correction credits no spell or reset contribution");
}
void prior_channel_invalidates_current_readiness() {
    auto f = fixture(); publish(*f);
    auto& r = f->request;
    r.shape = fc::Shape::bank_then_produce_bank; f->frame.own.actor.bag = 10;
    r.leg_count = 0;
    const auto own = f->frame.own.actor.position;
    const auto spawn = r.groups[0].observed.value.spawn;
    r.legs[r.leg_count++] = {own,r.bank_position,public_map(2)};
    r.legs[r.leg_count++] = {r.bank_position,own,public_map(2)};
    r.legs[r.leg_count++] = {own,spawn,public_map(2)};
    r.legs[r.leg_count++] = {spawn,r.bank_position,public_map(1)};
    r.legs[r.leg_count++] = {r.bank_position,r.renewal_position,public_map(2)};
    const auto result = build(*f);
    check(result->status == Status::feasible && clearance(*result) == 4,
          "a later return to the same tile gets no current readiness credit");
}
void caller_admission_preserves_safety() {
    auto f = fixture();
    f->frame.own.actor.health = 50; f->frame.own.actor.max_health = 100;
    f->request.ambient_incoming.value = 10;
    ReferenceInput actual{.frame=f->frame};
    actual.assigned_group = 4; actual.at_approach = true;
    actual.safe_now = true; actual.fresh_target_attack_legal = true;
    ActorState state;
    check(!reference::choose_forecast(state,actual,f->request) && !state.active,
          "old four-tick clearance cannot waive twenty-percent safety reserve");
    publish(*f);
    const auto action = reference::choose_forecast(state,actual,f->request);
    check(action && action->action == Action::attack && state.active,
          "actual forecast-binding caller admits witnessed safe two-tick clearance");
    f->request.ambient_incoming.value = 50;
    const auto unsafe = build(*f);
    check(unsafe->status == Status::unresolved && unsafe->issue == fc::Issue::unsafe,
          "stationary witness cannot erase unsafe external stress");
}
} // namespace
int main() {
    basic_timing_and_exposure(); stale_or_incomplete_evidence();
    action_and_position_boundaries(); prior_channel_invalidates_current_readiness();
    caller_admission_preserves_safety();
    std::printf("stationary clearance: %d checks passed\n",checks);
}
