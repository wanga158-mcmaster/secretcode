// Focused pure lifecycle oracle; no engine or native timing claim.
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
void check(bool value, const char* name) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL exposure lifecycle: %s\n", name); std::exit(1); }
}
bool equal(double a, double b) { return std::abs(a - b) < 1e-9; }
template<class T> Fact<T> fact(T value) { return {value, Source::public_map}; }
template<class T> Fact<T> setting(T value) { return {value, Source::queried_setting}; }
template<class T> Observation<T> seen(T value, ViewClock clock, int subject) {
    return {value, clock, subject, Knowledge::exact};
}
struct Fixture { Frame frame{}; fc::Request request{}; };
void leg(fc::Request& r, Point from, Point to, int distance) {
    r.legs[r.leg_count++] = {from, to, fact(distance)};
}
std::unique_ptr<Fixture> fixture(bool attributed = true) {
    auto f = std::make_unique<Fixture>();
    f->frame.clock = {30, 1, 7};
    auto& a = f->frame.own.actor;
    a.id = 1; a.team = Team::ally; a.alive = a.can_move = a.can_attack = true;
    a.health = a.max_health = 10000; a.strength = 1; a.attack_range = 1;
    a.ready_attack_group = 17;
    auto& r = f->request;
    r.group_count = 1; r.shape = fc::Shape::produce_bank;
    r.timing.settings = setting(Settings{2000, 1000, 2});
    r.timing.tribute_multiplier = setting(.1);
    r.timing.tribute_base = setting(0); r.timing.tribute_minimum = setting(1);
    r.kit = seen(Productivity{1, 0, false}, f->frame.clock, 1);
    r.ambient_incoming = seen(5., f->frame.clock, 1); // external3 + source2
    r.bank_waypoint = 90; r.renewal_waypoint = 91;
    r.bank_position = {1, 0}; r.renewal_position = {0, 0};
    r.bank_available = seen(true, f->frame.clock, 90);
    auto& g = r.groups[0];
    g.observed = seen(Group{17, {}, {}, {}, Lifecycle::living, true, true, true}, f->frame.clock, 17);
    g.membership_complete = fact(true); g.member_count = 1;
    g.members[0].public_full = fact(MonsterWork{100, 2, 2});
    g.members[0].current = seen(MonsterWork{100, 2, 2}, f->frame.clock, 100);
    g.gems = fact<Gems>(10); g.gold = fact<Gems>(7);
    if (attributed) {
        g.ambient_overlap = seen(2., f->frame.clock, 17);
    }
    f->frame.group = g.observed;
    leg(r, {}, {}, 0); leg(r, {}, {}, 0);
    leg(r, {}, {1, 0}, 1); leg(r, {1, 0}, {}, 1);
    return f;
}
std::unique_ptr<fc::Result> build(const Fixture& f) {
    auto r = std::make_unique<fc::Result>();
    fc::build_into(f.frame, f.request, *r);
    return r;
}
const fc::PhaseTerm& phase(const fc::Result& r, fc::PhaseKind kind, int occurrence = 0) {
    for (int i = 0; i < r.phase_count; ++i)
        if (r.phases[i].kind == kind && occurrence-- == 0) return r.phases[i];
    check(false, "requested phase exists"); std::abort();
}
void feasible(const Fixture& f, const fc::Result& r) {
    check(r.status == Status::feasible && r.issue == fc::Issue::none, "complete cycle feasible");
    check(r.certificate.valid(f.frame, 1, 17), "certificate remains cumulative and bound");
}
void rejects(const Fixture& f, fc::Issue issue) {
    const auto r = build(f);
    check(r->status != Status::feasible && r->issue == issue, "invalid attribution rejected");
    check(r->schedule.actor_count == 0 && !r->certificate.route_known,
          "rejected attribution leaves no usable earlier result");
}
void lifecycle() {
    auto old = fixture(false); auto legacy = build(*old); feasible(*old, *legacy);
    check(equal(legacy->certificate.recovery.incoming_total, 64),
          "omitted metadata retains original conservative bound");
    auto f = fixture(); auto r = build(*f); feasible(*f, *r);
    check(equal(phase(*r, fc::PhaseKind::clear).incoming, 10), "source counted once during clear");
    check(equal(phase(*r, fc::PhaseKind::hold).incoming, 9), "dead source absent during hold");
    check(equal(phase(*r, fc::PhaseKind::tribute).incoming, 3), "external stress persists during tribute");
    check(equal(r->certificate.recovery.incoming_total, 40), "external stress persists through renewal");
    check(r->schedule.actors[0].occupied_turns.value == legacy->schedule.actors[0].occupied_turns.value,
          "lifecycle correction changes no ranged or timing arithmetic");
    f->request.ambient_incoming.value += 4; r = build(*f); feasible(*f, *r);
    check(equal(r->certificate.recovery.incoming_total, 88), "unrepresented awake source is never subtracted");

    f = fixture(); f->frame.own.actor.position = {-1, 0};
    f->request.legs[0] = {{-1, 0}, {}, fact(1)};
    r = build(*f); feasible(*f, *r);
    check(equal(phase(*r, fc::PhaseKind::travel).incoming, 15), "awake source remains on approach");
    check(equal(r->certificate.recovery.incoming_total, 55), "approach and external exposure accumulate");

    f = fixture();
    f->request.bank_position = {40, 0};
    f->request.legs[2] = {{}, {40, 0}, fact(40)};
    f->request.legs[3] = {{40, 0}, {}, fact(40)};
    f->request.groups[0].members[0].public_full.value.effective_strength = 5000;
    r = build(*f); feasible(*f, *r);
    check(equal(r->certificate.recovery.incoming_total, 274),
          "long bank and renewal do not invent attacks from a dormant fresh life");
    check(equal(r->certificate.recovery.next_phase_incoming, 3),
          "renewal reserves external stress without uncaused source awakening");

    f = fixture(); f->request.ambient_incoming.value = 3;
    f->request.groups[0].ambient_overlap.value = 0; r = build(*f); feasible(*f, *r);
    check(equal(r->certificate.recovery.incoming_total, 40), "sleeping source still wakes during its represented clearance");

    // A fresh later observation can establish awakening; the old quote is not
    // a promise about third-party damage during its conditional continuation.
    f = fixture(); f->frame.clock.round += 60;
    auto& q = f->request; auto& group = q.groups[0];
    q.kit.clock = q.ambient_incoming.clock = q.bank_available.clock = f->frame.clock;
    group.observed.clock = group.ambient_overlap.clock = group.members[0].current.clock = f->frame.clock;
    f->frame.group = group.observed;
    q.ambient_incoming.value = 9; group.ambient_overlap.value = 6;
    group.members[0].public_full.value.effective_strength = group.members[0].current.value.effective_strength = 6;
    r = build(*f); feasible(*f, *r);
    check(equal(phase(*r, fc::PhaseKind::clear).incoming, 18),
          "newly observed awake life is charged again in a fresh quote");
}
void validation() {
    auto f = fixture(); f->request.ambient_incoming.knowledge = Knowledge::unknown;
    rejects(*f, fc::Issue::unknown_input);
    f = fixture(); --f->request.groups[0].ambient_overlap.clock.revision;
    rejects(*f, fc::Issue::unknown_input);
    f = fixture(); f->request.groups[0].ambient_overlap.subject = 18;
    rejects(*f, fc::Issue::unknown_input);
    f = fixture(); f->request.groups[0].ambient_overlap.value = -1;
    rejects(*f, fc::Issue::malformed);
    f = fixture(); f->request.groups[0].ambient_overlap.value = std::numeric_limits<double>::infinity();
    rejects(*f, fc::Issue::malformed);
    f = fixture(); f->request.groups[0].ambient_overlap.value = 3;
    rejects(*f, fc::Issue::malformed);
    f = fixture(); f->request.ambient_incoming.value = 1;
    rejects(*f, fc::Issue::malformed);
    f = fixture(); f->request.groups[0].ambient_overlap.knowledge = Knowledge::stale;
    rejects(*f, fc::Issue::unknown_input);
    f = fixture(); f->request.groups[0].members[0].current.value.effective_strength = 1e308;
    rejects(*f, fc::Issue::overflow);
}
void two_sources() {
    auto f = fixture(); auto& r = f->request;
    r.shape = fc::Shape::produce_bank_produce_bank; r.group_count = 2;
    r.ambient_incoming.value = 7; // external3 + source2 + source2
    auto& g = r.groups[1]; g = r.groups[0];
    g.observed.subject = g.observed.value.id = 18;
    g.observed.value.approach = g.observed.value.spawn = {2, 0};
    g.members[0].public_full.value.id = 101;
    g.members[0].current.subject = g.members[0].current.value.id = 101;
    g.ambient_overlap.subject = 18;
    r.leg_count = 0;
    leg(r, {}, {}, 0); leg(r, {}, {}, 0); leg(r, {}, {1, 0}, 1);
    leg(r, {1, 0}, {2, 0}, 1); leg(r, {2, 0}, {2, 0}, 0);
    leg(r, {2, 0}, {1, 0}, 1); leg(r, {1, 0}, {}, 1);
    auto result = build(*f); feasible(*f, *result);
    check(equal(phase(*result, fc::PhaseKind::clear).incoming, 14), "uncleared second source keeps its ambient term");
    check(equal(phase(*result, fc::PhaseKind::clear, 1).incoming, 10), "second source overlap removed exactly once");
    check(equal(result->certificate.recovery.incoming_total, 104), "two-source external stress conserved");
    r.ambient_incoming.value = 3; rejects(*f, fc::Issue::malformed);
}
}
int main() {
    lifecycle(); validation(); two_sources();
    std::printf("PASS exposure lifecycle %d checks\n", checks);
}
