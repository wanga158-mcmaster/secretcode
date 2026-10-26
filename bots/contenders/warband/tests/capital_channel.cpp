#include "../src/capital_channel.h"
#include <cstdio>
#include <initializer_list>
#include <limits>

using namespace wb::capital::channel;
namespace {
int checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (false)
Observation frame(int round = 377) {
    return {1, round, true, true, {14, 19}, 135, 0, Channel::None, false, false};
}
void start(Monitor& m, Observation& o) {
    CHECK(m.observe(o) == Outcome::NoChange);
    CHECK(m.can_start(o, true));
    CHECK(m.accepted_start(o, true));
    CHECK(m.state() == State::Active);
    CHECK(!m.can_start(o, true));
    CHECK(!m.accepted_start(o, true));
    CHECK(m.observe(o) == Outcome::AwaitEffect);
    o.channel = Channel::Recall;
    CHECK(m.observe(o) == Outcome::Pending);
}
void actual_failure_and_real_change() {
    Monitor m; auto o = frame(); start(m, o);
    ++o.round; o.hp = 20; o.channel = Channel::None;
    CHECK(m.observe(o) == Outcome::DamageInterruption);
    CHECK(m.state() == State::Blocked);
    CHECK(!m.can_start(o, true)); // Personally visible foes=0 cannot rearm.
    CHECK(!m.changed_context(o, Change::None));
    CHECK(!m.changed_context(o, Change::ActualStep));
    ++o.round; CHECK(m.observe(o) == Outcome::NoChange);
    CHECK(!m.can_start(o, true)); // Elapsed time cannot rearm.
    o.hp = 19; CHECK(m.observe(o) == Outcome::NoChange);
    CHECK(!m.can_start(o, true)); // A changed HP counter cannot rearm.
    ++o.position.x; CHECK(m.observe(o) == Outcome::NoChange);
    CHECK(!m.can_start(o, true)); // Movement needs actual event attribution.
    CHECK(m.changed_context(o, Change::ActualStep));
    CHECK(!m.can_start(o, false)); // Actual movement still needs fresh pricing.
    CHECK(m.can_start(o, true));
    CHECK(m.accepted_start(o, true));
}
void informational_change() {
    for (auto event : {Change::RelevantOwnWard, Change::ObservedThreatResolution}) {
        Monitor m; auto o = frame(); start(m, o);
        ++o.round; o.channel = Channel::None;
        CHECK(m.observe(o) == Outcome::Interrupted);
        CHECK(m.changed_context(o, event));
        CHECK(!m.can_start(o, false));
        CHECK(m.can_start(o, true));
    }
    Monitor m; auto o = frame(); start(m, o);
    ++o.round; o.channel = Channel::None; m.observe(o);
    CHECK(!m.changed_context(o, static_cast<Change>(99)));
    CHECK(m.state() == State::Blocked);
}
void completion_death_and_stun() {
    Monitor arrival; auto o = frame(); start(arrival, o);
    o.round += 5; o.channel = Channel::None; o.at_spawn = true; o.position = {1, 1};
    CHECK(arrival.observe(o) == Outcome::ArrivedAtSpawn);
    CHECK(arrival.state() == State::Open && !arrival.can_start(o, true));
    // Arrival does not require or claim full health.
    CHECK(o.hp == 135);
    ++o.round; o.at_spawn = false; o.position = {2, 2};
    CHECK(arrival.observe(o) == Outcome::NoChange);
    CHECK(arrival.can_start(o, true));
    Monitor dead; o = frame(); start(dead, o);
    ++o.round; o.alive = false; o.hp = 0; o.position = {-1, -1}; o.channel = Channel::None;
    CHECK(dead.observe(o) == Outcome::Dead);
    CHECK(!dead.can_start(o, true));
    CHECK(!dead.changed_context(o, Change::ObservedThreatResolution));
    o.round += 60; o.alive = true; o.hp = 1200; o.position = {1, 1}; o.at_spawn = true;
    CHECK(dead.observe(o) == Outcome::SpawnRecovery);
    CHECK(dead.state() == State::Open);
    Monitor stun; o = frame(); start(stun, o);
    ++o.round; o.channel = Channel::None; o.stunned = true;
    CHECK(stun.observe(o) == Outcome::StunInterruption);
    o.stunned = false; stun.observe(o);
    CHECK(!stun.can_start(o, true));
    Monitor shield; o = frame(); o.shield = 50; start(shield, o);
    ++o.round; o.channel = Channel::None; o.shield = 0;
    CHECK(shield.observe(o) == Outcome::DamageInterruption);
}
void exact_body_and_input_limits() {
    Monitor m; auto o = frame();
    CHECK(!m.can_start(o, true)); // Must first observe this exact packet.
    start(m, o);
    auto foreign = o; ++foreign.owner;
    CHECK(m.observe(foreign) == Outcome::WrongOwner);
    CHECK(!m.accepted_cancel(foreign));
    CHECK(m.accepted_cancel(o));
    CHECK(!m.accepted_cancel(o));
    o.channel = Channel::None; ++o.position.x; m.observe(o);
    CHECK(m.changed_context(o, Change::ActualStep));
    CHECK(!m.can_start(o, true)); // Already started once in this owner input.
    ++o.round; m.observe(o);
    CHECK(m.can_start(o, true)); CHECK(m.accepted_start(o, true));
    o.channel = Channel::Recall; m.observe(o);
    CHECK(m.accepted_cancel(o)); // Cancellation limit refreshes next round.
    auto stale = o; --stale.round;
    CHECK(m.observe(stale) == Outcome::Stale);
    CHECK(!m.changed_context(stale, Change::RelevantOwnWard));
    o.channel = Channel::None; m.observe(o);
    auto unmatched = o; unmatched.hp = 1;
    CHECK(!m.changed_context(unmatched, Change::RelevantOwnWard));
    CHECK(!m.can_start(unmatched, true));
}
void unknown_and_untracked() {
    Monitor m; auto o = frame(); start(m, o);
    ++o.round; o.channel = Channel::Unknown;
    CHECK(m.observe(o) == Outcome::NeedObservation);
    CHECK(m.state() == State::Active && !m.can_start(o, true));
    o.channel = Channel::None;
    CHECK(m.observe(o) == Outcome::Interrupted);
    Monitor untracked; o = frame(); o.channel = Channel::Recall;
    CHECK(untracked.observe(o) == Outcome::UntrackedChannel);
    CHECK(!untracked.can_start(o, true));
    o.channel = Channel::None; ++o.round; untracked.observe(o);
    CHECK(!untracked.can_start(o, true));
    auto invalid = o; invalid.hp = std::numeric_limits<float>::quiet_NaN();
    CHECK(untracked.observe(invalid) == Outcome::NeedObservation);
    CHECK(!untracked.changed_context(invalid, Change::RelevantOwnWard));
    Monitor other; o = frame(); start(other, o); o.channel = Channel::Other;
    CHECK(other.observe(o) == Outcome::Interrupted);
    Monitor vanished; o = frame(); start(vanished, o); o.channel = Channel::None;
    CHECK(vanished.observe(o) == Outcome::Interrupted); // Seen start then actual loss in this input.
    CHECK(!vanished.can_start(o, true));
}
}
int main() {
    actual_failure_and_real_change(); informational_change(); completion_death_and_stun();
    exact_body_and_input_limits(); unknown_and_untracked();
    std::printf("capital_channel: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
