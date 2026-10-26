// Reused forecast storage must not carry a proposal or exposure between builds.
#include "../common/src/observe.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

namespace {
using namespace p6;
namespace fc = p6::forecast;
static_assert(std::is_same_v<decltype(std::declval<fc::EvaluatedQuote&>().request()),
                             const fc::Request&>);
static_assert(std::is_same_v<decltype(std::declval<fc::EvaluatedQuote&>().result()),
                             const fc::Result&>);
static_assert(!std::is_constructible_v<fc::EvaluatedQuote, const Frame&,
                                     const fc::Request&, const fc::Result&>);
struct PublicRequestFill { void operator()(fc::Request&) const {} };
template<class Quote>
concept PublicEvaluateInto = requires(Quote& quote, const Frame& frame) {
    quote.evaluate_into(frame, PublicRequestFill{});
};
static_assert(!PublicEvaluateInto<fc::EvaluatedQuote>);
int checks = 0;
void check(bool value, const char* name) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL forecast reuse: %s\n", name); std::exit(1); }
}
template<class T> Observation<T> seen(T value, ViewClock clock, int subject) {
    return {value, clock, subject, Knowledge::exact};
}
template<class T> Fact<T> setting(T value) { return {value, Source::queried_setting}; }
struct Fixture { Frame frame{}; fc::Request request{}; fc::Result result{}; };
std::unique_ptr<Fixture> fixture() {
    auto f = std::make_unique<Fixture>();
    f->frame.clock = {10, 1, 0};
    auto& own = f->frame.own.actor;
    own.id = 1; own.team = Team::ally; own.alive = own.can_move = own.can_attack = true;
    own.health = own.max_health = 100; own.strength = 1; own.bag = 10;
    auto& r = f->request;
    r.shape = fc::Shape::bank_only; r.project_group = 17;
    r.timing.settings = setting(Settings{100, 1000, 2});
    r.timing.tribute_multiplier = setting(.1);
    r.timing.tribute_base = setting(0); r.timing.tribute_minimum = setting(1);
    r.ambient_incoming = seen(1., f->frame.clock, 1);
    r.bank_waypoint = 90; r.renewal_waypoint = 91;
    r.bank_available = seen(true, f->frame.clock, 90);
    r.leg_count = 2;
    r.legs[0] = {{0, 0}, {0, 0}, {0, Source::public_map}};
    r.legs[1] = {{0, 0}, {0, 0}, {0, Source::public_map}};
    return f;
}
void no_payload(const Fixture& f, const char* name) {
    const auto& out = f.result;
    check(out.status != Status::feasible && out.issue != fc::Issue::none, name);
    check(out.schedule.actor_count == 0 && out.schedule.production_count == 0 &&
          out.phase_count == 0, "failure has no active schedule or phases");
    check(out.first.stage == fc::FirstStage::none && out.first.actor == -1,
          "failure has no action proposal");
    check(!out.certificate.valid(f.frame, 1, 17) && !out.certificate.route_known &&
          !out.certificate.bank_executable && !out.certificate.renewal_reachable,
          "failure cannot reuse a successful cycle certificate");
}
void quote_ownership() {
    auto f = fixture(); auto quote = std::make_unique<fc::EvaluatedQuote>();
    check(!quote->ready() && !quote->matches_frame(f->frame), "unevaluated quote cannot match");
    Actor ally;
    ally.id = 2; ally.team = Team::ally; ally.alive = true;
    ally.health = ally.max_health = 100; ally.strength = 2;
    f->frame.actor_count = 1;
    f->frame.actors[0] = seen(ally, f->frame.clock, ally.id);
    quote->evaluate(f->frame, f->request);
    check(quote->ready() && quote->result().status == Status::feasible &&
          quote->matches_frame(f->frame), "completed quote owns a matching feasible result");
    f->request.timing.tribute_multiplier.value = 50;
    f->request.legs[0].distance.value = 123;
    check(quote->request().timing.tribute_multiplier.value == .1 &&
          quote->request().legs[0].distance.value == 0 &&
          quote->result().schedule.actors[0].events[0].tick == 1 &&
          quote->result().status == Status::feasible,
          "mutating original request cannot alter owned inputs or result");
    auto mismatch = [&](auto mutate, const char* name) {
        Frame changed = f->frame;
        mutate(changed);
        check(!quote->matches_frame(changed), name);
    };
    mismatch([](Frame& v) { ++v.own.gold; }, "changed personal gold rejects quote");
    mismatch([](Frame& v) { ++v.own.actor.bag; }, "changed bag rejects quote");
    mismatch([](Frame& v) { ++v.clock.round; }, "changed round rejects quote");
    mismatch([](Frame& v) { ++v.clock.self; }, "changed clock actor rejects quote");
    mismatch([](Frame& v) { ++v.clock.revision; }, "changed revision rejects quote");
    mismatch([](Frame& v) { ++v.own_team_score; }, "changed team score rejects quote");
    mismatch([](Frame& v) { v.own.at_spawn = true; }, "changed self spawn state rejects quote");
    mismatch([](Frame& v) { v.own.spawn_recovery_observed = true; },
             "changed self recovery observation rejects quote");
    mismatch([](Frame& v) { v.own.actor.can_attack = false; }, "changed own availability rejects quote");
    mismatch([](Frame& v) { ++v.own.actor.ready_attack_group; }, "changed own attack group rejects quote");
    mismatch([](Frame& v) { ++v.own.actor.health; }, "changed own health rejects quote");
    mismatch([](Frame& v) { v.actor_count = 0; }, "changed active actor count rejects quote");
    mismatch([](Frame& v) { ++v.actors[0].clock.round; }, "changed observation round rejects quote");
    mismatch([](Frame& v) { ++v.actors[0].clock.self; }, "changed observation owner rejects quote");
    mismatch([](Frame& v) { ++v.actors[0].clock.revision; }, "changed observation revision rejects quote");
    mismatch([](Frame& v) { ++v.actors[0].subject; }, "changed observation subject rejects quote");
    mismatch([](Frame& v) { v.actors[0].knowledge = Knowledge::stale; },
             "changed observation knowledge rejects quote");
    mismatch([](Frame& v) { ++v.actors[0].value.position.x; }, "changed visible position rejects quote");
    mismatch([](Frame& v) { ++v.actors[0].value.strength; }, "changed visible strength rejects quote");
    mismatch([](Frame& v) { ++v.actors[0].value.attack_range; }, "changed visible attack range rejects quote");
    mismatch([](Frame& v) { v.actors[0].value.type = Champion::varg; },
             "changed visible champion type rejects quote");
    mismatch([](Frame& v) { v.actors[0].value.channel = Channel::recall; },
             "changed visible channel rejects quote");
    mismatch([](Frame& v) { ++v.actors[0].value.spawn_group; }, "changed visible spawn group rejects quote");
    mismatch([](Frame& v) { v.actors[0].value.team = Team::enemy; }, "changed visible team rejects quote");
    mismatch([](Frame& v) { v.actors[0].value.health = std::numeric_limits<double>::quiet_NaN(); },
             "NaN visible actor field rejects quote");
    Frame ignored = f->frame;
    ignored.group.value.id = 999; ignored.group.clock = {999, 999, 999};
    ignored.group.knowledge = Knowledge::exact;
    ignored.actors[5] = seen(ally, {123, 321, 1}, 456);
    check(quote->matches_frame(ignored), "group-only and inactive actor-tail mutations preserve match");
    check(quote->matches_frame(f->frame), "mismatch probes do not mutate the quote");

    // Re-evaluating with the now-malformed external request replaces the old
    // feasible result rather than keeping its certificate or proposal ready.
    quote->evaluate(f->frame, f->request);
    check(quote->ready() && quote->result().status != Status::feasible &&
          quote->result().schedule.actor_count == 0 &&
          quote->result().first.stage == fc::FirstStage::none,
          "completed failed evaluation invalidates prior successful quote");
    f->frame.own.actor.health = std::numeric_limits<double>::quiet_NaN();
    quote->evaluate(f->frame, f->request);
    check(!quote->matches_frame(f->frame), "matching NaN own fields cannot validate a quote");
}
void frame_current_ids() {
    auto f = fixture(); auto& frame = f->frame;
    Actor ally; ally.id = 2; ally.team = Team::ally;
    frame.actor_count = 4;
    frame.actors[0] = seen(ally, {9, 1, 0}, 2); // Same ID, old clock.
    frame.actors[1] = seen(ally, frame.clock, 2);
    frame.actors[2] = seen(ally, frame.clock, 2);
    frame.actors[2].knowledge = Knowledge::stale;
    ally.id = 3; frame.actors[3] = seen(ally, frame.clock, 3);
    check(frame.valid() && frame.visible(2) == &frame.actors[1].value,
          "stale IDs before and after a current ID do not duplicate or shadow it");
    frame.actors[2].knowledge = Knowledge::exact;
    check(!frame.valid(), "two exact current IDs are rejected with stale IDs mixed in");
    frame.actors[2].knowledge = Knowledge::unknown;
    check(frame.valid(), "unknown duplicate ID does not participate in current validation");
    frame.actors[3].subject = -1;
    check(!frame.valid(), "negative exact current subject is rejected");
    frame.actors[3].subject = frame.clock.self;
    check(!frame.valid(), "self exact current subject is rejected");
    frame.actors[3].subject = 4;
    check(!frame.valid(), "exact current actor ID must match its subject");
    frame.actors[3].clock.revision = 1;
    check(frame.valid(), "stale-clock subject mismatch remains outside current validation");
}
}

int main() {
    auto f = fixture(); auto& r = f->request; auto& out = f->result;
    r.shield.enabled = true;
    r.shield.amount = seen(10., f->frame.clock, 1);
    r.shield.expires_before_tick = seen(10, f->frame.clock, 1);
    fc::build_into(f->frame, r, out);
    check(out.status == Status::feasible && out.certificate.valid(f->frame, 1, 17),
          "first build succeeds in caller storage");
    check(out.phase_count == 3 && out.schedule.actors[0].event_count == 2 &&
          out.first.stage == fc::FirstStage::tribute, "first build has expected active records");
    check(out.phases[1].incoming == 1 && out.phases[1].shield_absorbed == 1 &&
          out.certificate.recovery.own_timed_shield == 1, "first build records its shield");

    f->frame.own.actor.health = std::numeric_limits<double>::quiet_NaN();
    fc::build_into(f->frame, r, out);
    no_payload(*f, "early malformed build fails after success");
    check(out.status == Status::invalid && out.issue == fc::Issue::malformed,
          "early failure preserves reason and status");

    f->frame.own.actor.health = 100; f->frame.own.actor.bag = 20;
    r.shield.enabled = false; r.ambient_incoming.value = 2;
    fc::build_into(f->frame, r, out);
    check(out.status == Status::feasible && out.certificate.valid(f->frame, 1, 17),
          "storage can succeed again after failure");
    check(out.phase_count == 3 && out.schedule.actors[0].event_count == 2 &&
          out.schedule.actors[0].events[0].tick == 2, "new duration replaces prior schedule");
    check(out.phases[1].incoming == 4 && out.phases[1].shield_absorbed == 0 &&
          out.phases[1].health_after == 96 && out.certificate.recovery.own_timed_shield == 0,
          "new phases do not accumulate previous incoming or shield credit");
    const auto fresh = std::make_unique<fc::Result>(fc::build(f->frame, r));
    check(out.certificate.recovery.incoming_total == fresh->certificate.recovery.incoming_total &&
          out.schedule.actors[0].occupied_turns.value == fresh->schedule.actors[0].occupied_turns.value,
          "by-value wrapper agrees with caller-owned result");

    // This failure happens after the route and all exposure phases were built.
    r.recovery.enabled = r.recovery.requires_health_restore = true;
    r.recovery.waypoint = 92; r.recovery.turns = {0, Source::public_map};
    r.leg_count = 3; r.legs[2] = r.legs[1];
    fc::build_into(f->frame, r, out);
    no_payload(*f, "late health-restore failure invalidates completed payload");
    check(out.issue == fc::Issue::health_restore_unmodeled && out.recovery_health_unmodeled,
          "late failure keeps its diagnostic");

    r.recovery.enabled = r.recovery.requires_health_restore = false; r.leg_count = 2;
    fc::build_into(f->frame, r, out);
    check(out.status == Status::feasible && !out.recovery_health_unmodeled &&
          out.phase_count == 3 && out.schedule.actors[0].event_count == 2,
          "smaller successful route resets recovery flags and record counts");
    quote_ownership();
    frame_current_ids();
    std::printf("{\"case\":\"forecast-reuse\",\"checks\":%d,\"status\":\"passed\",\"engineLaunches\":0}\n", checks);
}
