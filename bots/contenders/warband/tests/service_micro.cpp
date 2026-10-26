#include "service_micro.h"

#include <cstdio>
#include <limits>
#include <type_traits>

using namespace wb::service_micro;
namespace {
int checks{}, failures{}, calls{};
void check(bool ok, const char* label) {
    ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", label); }
}
Frame fresh(int round, int owner = 2) {
    Frame f;
    f.authority = {owner, round, static_cast<std::uint64_t>(round), 1};
    f.observed_self = f.observed_readiness = true;
    f.position = {4, 4}; f.hp = 1000; f.alive = true;
    f.can_move = f.can_attack = f.can_cast = true;
    f.observed_domain = f.domain_supported = true;
    f.observed_target = true; f.target_id = 10;
    f.target_position = {5, 4}; f.target_hp = 120;
    f.target_visible = f.target_alive = f.basic_in_range = true;
    f.observed_move = f.move_legal = true; f.move_endpoint = {5, 5};
    f.move_landing = MoveLanding::KnownFree;
    return f;
}
Frame refreshed(Frame f) { ++f.authority.view_revision; return f; }
Request basic(int id = 10) { return {Kind::Basic, id, {}}; }
Request move_to(Position p) { return {Kind::Move, -1, p}; }
Decision report(Gate& g, const Decision& issued, Frame after, Acceptance a) {
    if (a != Acceptance::Invalidated) ++calls;
    return g.complete(after.authority, issued.attempt, {a}, after);
}

void repeat_ready_work() {
    Gate g; float hp = 5000; const int before_calls = calls;
    for (int round = 1; round <= 24; ++round) {
        auto f = fresh(round); f.target_hp = hp;
        auto issued = g.issue(f.authority, f, basic());
        check(issued.issued() && !issued.command_attempted, "24 fresh ready opportunities issue");
        auto after = refreshed(f); hp -= 55; after.target_hp = hp;
        // These are ACTUAL supplied ranged post-permissions, not a prediction.
        after.can_move = after.can_attack = false;
        auto done = report(g, issued, after, Acceptance::Accepted);
        check(done.status == Status::Applied && done.effect == Effect::Damaged,
              "24 fresh opportunities validate actual damage");
        check(done.command_attempted && done.command_accepted
              && done.post_permissions_observed && !done.post_permissions.move
              && !done.post_permissions.attack && done.post_permissions.cast,
              "ranged post-permissions copied from refreshed facts");
        after = refreshed(after);
        check(!g.issue(after.authority, after, basic()).issued(), "refresh does not rearm input");
    }
    check(calls - before_calls == 24 && hp == 3680, "no first-strike then idle collapse");
}

void member_lifecycle_and_movement() {
    Gate g; float hp = 120;
    for (int round = 1; round <= 3; ++round) {
        auto f = fresh(round); f.target_hp = hp;
        auto issued = g.issue(f.authority, f, basic());
        check(issued.issued(), "three guarded basics issue");
        auto after = refreshed(f); hp -= 55;
        if (hp <= 0) {
            after.target_alive = after.target_visible = false;
            // Dead live-info is deliberately unavailable.
            after.target_hp = std::numeric_limits<float>::quiet_NaN();
        } else after.target_hp = hp;
        const auto done = report(g, issued, after, Acceptance::Accepted);
        check(done.status == Status::Applied
              && done.effect == (round == 3 ? Effect::TargetDied : Effect::Damaged),
              "65 then10 then explicit dead state");
        if (round == 3) {
            auto next = refreshed(after); next.target_id = 11;
            next.target_alive = next.target_visible = true; next.target_hp = 120;
            check(!g.issue(next.authority, next, basic(11)).issued(),
                  "target death does not rearm old authority for next member");
        }
    }
    auto f = fresh(4); f.target_id = 11; f.target_position = {7, 5};
    f.basic_in_range = false; f.move_endpoint = {5, 5};
    check(g.issue(f.authority, f, basic(11)).status == Status::NoCommand,
          "fresh next member out of range does not receive stale basic");
    auto issued = g.issue(f.authority, f, move_to(f.move_endpoint));
    check(issued.issued(), "next member permits actual legal station move");
    auto after = refreshed(f); after.position = f.move_endpoint;
    after.can_move = false; after.can_attack = true; // actual melee post-view
    const auto done = report(g, issued, after, Acceptance::Accepted);
    check(done.status == Status::Applied && done.effect == Effect::Moved,
          "movement requires actual endpoint change");
    check(done.post_permissions.attack && !done.post_permissions.move,
          "melee permission is observed, not inferred by kind");
    after = refreshed(after); after.basic_in_range = true;
    check(!g.issue(after.authority, after, basic(11)).issued(),
          "even actual remaining permission does not bypass one-input grammar");
    auto next = fresh(5); next.target_id = 11; next.position = f.move_endpoint;
    auto strike = g.issue(next.authority, next, basic(11));
    auto struck = refreshed(next); struck.target_hp = 65;
    check(strike.issued() && report(g, strike, struck, Acceptance::Accepted).effect == Effect::Damaged,
          "next fresh input resumes work on surviving member");
}

void observation_and_scope_guards() {
    { Gate g; auto f = fresh(1); auto current = fresh(2).authority;
      check(g.issue(current, f, basic()).status == Status::NeedObservation, "stale packet rejected"); }
    { Gate g; auto f = fresh(1); f.observed_self = false;
      check(g.issue(f.authority, f, basic()).status == Status::NeedObservation, "missing self rejected"); }
    { Gate g; auto f = fresh(1); f.observed_readiness = false;
      check(g.issue(f.authority, f, basic()).status == Status::NeedObservation, "missing permissions rejected"); }
    { Gate g; auto f = fresh(1); f.hp = std::numeric_limits<float>::quiet_NaN();
      check(g.issue(f.authority, f, basic()).status == Status::NeedObservation, "nonfinite actor rejected"); }
    { Gate g; auto f = fresh(1); f.target_hp = std::numeric_limits<float>::infinity();
      check(g.issue(f.authority, f, basic()).status == Status::NeedObservation, "nonfinite live target rejected"); }
    { Gate g; auto f = fresh(1); f.target_visible = false;
      check(g.issue(f.authority, f, basic()).status == Status::NeedObservation, "hidden target rejected"); }
    { Gate g; auto f = fresh(1); f.target_alive = false;
      check(g.issue(f.authority, f, basic()).status == Status::NoCommand, "known dead member not whole clear"); }
    { Gate g; auto f = fresh(1);
      check(g.issue(f.authority, f, basic(99)).status == Status::NeedObservation, "target identity bound"); }
    { Gate g; auto f = fresh(1); f.observed_domain = false;
      check(g.issue(f.authority, f, basic()).status == Status::NeedObservation, "missing whole-domain certificate"); }
    { Gate g; auto f = fresh(1); f.domain_supported = false;
      check(g.issue(f.authority, f, basic()).status == Status::Unsupported, "harmful or unsupported domain rejected"); }
    { Gate g; auto f = fresh(1);
      check(g.issue(f.authority, f, {Kind::Cast, 10, {}}).status == Status::Unsupported,
            "ready cast unsupported, no kit mastery");
      check(g.issue(f.authority, f, basic()).issued(), "unsupported cast consumed no authority"); }
    for (int state = 0; state < 4; ++state) {
        Gate g; auto f = fresh(1);
        if (state == 0) { f.alive = false; f.hp = 0; }
        if (state == 1) f.stunned = true;
        if (state == 2) f.channel = true;
        if (state == 3) f.can_attack = false;
        check(g.issue(f.authority, f, basic()).status == Status::NoCommand,
              "dead stunned channelled or no actual allowance issues nothing");
    }
    { Gate g; auto f = fresh(1);
      check(g.issue(f.authority, f, basic(), false).status == Status::ComputeDeferred, "bounded calculation may defer");
      check(g.issue(f.authority, f, basic()).issued(), "defer consumed no attempt"); }
}

void refusal_and_one_shot_authority() {
    Gate g; auto f = fresh(1); const int before_calls = calls;
    auto issued = g.issue(f.authority, f, move_to(f.move_endpoint));
    check(issued.issued(), "move offer issued once");
    check(!g.issue(f.authority, f, move_to(f.move_endpoint)).issued(), "pending attempt blocks duplicate");
    auto after = refreshed(f);
    const auto refused = report(g, issued, after, Acceptance::Refused);
    check(refused.status == Status::Refused && refused.effect == Effect::Refused
          && refused.command_attempted && !refused.command_accepted
          && !refused.post_permissions_observed, "actual false return is refusal, no predicted effect");
    after = refreshed(after);
    check(!g.issue(after.authority, after, move_to(f.move_endpoint)).issued(), "refusal consumed authority");
    auto forged = after; ++forged.authority.input_generation;
    check(g.issue(forged.authority, forged, basic()).status == Status::NeedObservation,
          "new generation on same owner round cannot rearm");
    auto wrong = fresh(2, 3);
    check(g.issue(wrong.authority, wrong, basic()).status == Status::NeedObservation, "gate binds one owner");
    check(calls - before_calls == 1, "refusal retry emitted exactly one real call");
    check(g.complete(after.authority, issued.attempt, {Acceptance::Accepted}, after).status == Status::NoCommand,
          "completion replay cannot report second effect");
}

void effect_validation_and_invalidations() {
    { Gate g; auto f = fresh(1); f.move_legal = false;
      check(g.issue(f.authority, f, move_to(f.move_endpoint)).status == Status::NoCommand,
            "known false movement has no dispatch and is not executor refusal"); }
    { Gate g; auto f = fresh(1);
      check(g.issue(f.authority, f, move_to({9,9})).status == Status::NeedObservation,
            "move observation bound to chosen endpoint"); }
    { Gate g; auto f = fresh(1); auto issued = g.issue(f.authority, f, move_to(f.move_endpoint));
      auto after = refreshed(f); const auto d = report(g, issued, after, Acceptance::Accepted);
      check(d.status == Status::NeedObservation && d.effect == Effect::Invalidated && d.command_accepted,
            "accepted move with unchanged position is never applied"); }
    { Gate g; auto f = fresh(1); auto issued = g.issue(f.authority, f, basic());
      auto after = refreshed(f); const auto d = report(g, issued, after, Acceptance::Accepted);
      check(d.status == Status::NeedObservation && d.command_accepted && d.effect == Effect::Invalidated,
            "normally returned void attack is accepted, not assumed damage"); }
    { Gate g; auto f = fresh(1); auto issued = g.issue(f.authority, f, basic());
      auto after = refreshed(f); after.alive = false; after.hp = 0; after.target_hp = 65;
      const auto d = report(g, issued, after, Acceptance::Accepted);
      check(d.status == Status::NeedObservation && d.effect == Effect::Invalidated,
            "actor death invalidates result instead of fabricating productive continuation"); }
    { Gate g; auto f = fresh(1); auto issued = g.issue(f.authority, f, basic());
      auto after = refreshed(f); after.target_id = 11; after.target_hp = 65;
      check(report(g, issued, after, Acceptance::Accepted).status == Status::NeedObservation,
            "other target damage cannot validate selected command"); }
    { Gate g; auto f = fresh(1); auto issued = g.issue(f.authority, f, basic());
      auto after = f; after.target_hp = 65;
      check(report(g, issued, after, Acceptance::Accepted).status == Status::NeedObservation,
            "unrefreshed post-state cannot validate effect"); }
    { Gate g; auto f = fresh(1); auto issued = g.issue(f.authority, f, basic());
      auto after = fresh(2); after.target_hp = 65;
      check(report(g, issued, after, Acceptance::Accepted).status == Status::NeedObservation,
            "changed physical input cannot validate old accepted command"); }
    { Gate g; auto f = fresh(1); auto issued = g.issue(f.authority, f, basic());
      auto after = refreshed(f); after.target_hp = 65; const int before_calls = calls;
      auto d = report(g, issued, after, Acceptance::Invalidated);
      check(d.status == Status::NoCommand && d.effect == Effect::Invalidated
            && !d.command_attempted && !d.command_accepted && calls == before_calls,
            "final epoch or fuel fence can cancel before actual dispatch");
      check(!g.issue(after.authority, after, basic()).issued(), "cancelled issuance remains consumed"); }
    { Gate g; auto f = fresh(1); auto issued = g.issue(f.authority, f, basic());
      auto bogus = issued.attempt; ++bogus.ticket; auto after = refreshed(f); after.target_hp = 65;
      check(g.complete(after.authority, bogus, {Acceptance::Accepted}, after).status == Status::NeedObservation,
            "wrong ticket cannot complete pending attempt");
      check(report(g, issued, after, Acceptance::Accepted).status == Status::Applied,
            "wrong ticket did not overwrite correct pending proof"); }
}

void hidden_bush_landing_contract() {
    // Saved failure's geometry only: this fixture supplies facts; it is not a
    // replay/native observation and makes no claim that the real bush is empty.
    auto bush = [](int round) {
        auto f = fresh(round); f.position = {3, 9}; f.move_endpoint = {4, 10};
        f.move_landing = MoveLanding::MayRefuse; f.refusal_prefix_supported = true;
        return f;
    };
    { Gate g; auto f = bush(1); f.observed_move = false;
      check(g.issue(f.authority, f, move_to(f.move_endpoint)).status == Status::NeedObservation,
            "hidden landing with missing current geometry still rejects");
      f.observed_move = true; f.move_landing = MoveLanding::Missing;
      check(g.issue(f.authority, f, move_to(f.move_endpoint)).status == Status::NeedObservation,
            "unknown landing evidence is not assumed free");
      f.move_landing = MoveLanding::MayRefuse; f.refusal_prefix_supported = false;
      const auto unsupported = g.issue(f.authority, f, move_to(f.move_endpoint));
      check(unsupported.status == Status::Unsupported
            && unsupported.reason == Reason::UnsupportedRefusalPrefix,
            "hidden attempt requires explicit supported refusal prefix");
      f.refusal_prefix_supported = true;
      auto issued = g.issue(f.authority, f, move_to(f.move_endpoint));
      check(issued.issued(), "3,9 to4,10 observed reachable hidden landing may be attempted");
      auto after = refreshed(f); after.position = f.move_endpoint; after.can_move = false;
      auto done = report(g, issued, after, Acceptance::Accepted);
      check(done.status == Status::Applied && done.effect == Effect::Moved
            && done.command_accepted, "hidden landing actual true plus fresh endpoint validates move");
      after = refreshed(after); after.can_move = true;
      check(!g.issue(after.authority, after, move_to({5,10})).issued(),
            "hidden successful move cannot reuse same input even if M is observed ready"); }
    { Gate g; auto f = bush(1); auto issued = g.issue(f.authority, f, move_to(f.move_endpoint));
      check(issued.issued(), "hidden refusal case is an actual authorized attempt");
      auto after = refreshed(f); // actual false return: same origin and M still ready
      const auto done = report(g, issued, after, Acceptance::Refused);
      check(done.status == Status::Refused && done.effect == Effect::Refused
            && done.command_attempted && !done.command_accepted,
            "hidden false is typed refusal and never applied movement");
      check(after.position == Position{3,9} && after.can_move && !done.post_permissions_observed,
            "refusal leaves supplied actual origin and M intact without inferring permissions");
      after = refreshed(after);
      check(!g.issue(after.authority, after, move_to(after.move_endpoint)).issued(),
            "native retained M does not erase refused input's physical wait cost");
      auto next = bush(2);
      check(g.issue(next.authority, next, move_to(next.move_endpoint)).issued(),
            "a new actual input can price a bounded retry at the retained origin"); }
    { Gate g; auto f = bush(1); f.move_landing = MoveLanding::KnownBlocked;
      check(g.issue(f.authority, f, move_to(f.move_endpoint)).status == Status::NoCommand,
            "known blocked dominates even an inconsistent true attempt flag"); }
    { Gate g; auto f = bush(1); f.move_legal = false;
      check(g.issue(f.authority, f, move_to(f.move_endpoint)).status == Status::NoCommand,
            "unreachable hidden landing cannot be attempted"); }
    { Gate g; auto f = bush(1); f.observed_domain = false;
      check(g.issue(f.authority, f, move_to(f.move_endpoint)).status == Status::NeedObservation,
            "hidden branch cannot replace complete domain evidence"); }
    { Gate g; auto f = bush(1); f.domain_supported = false;
      check(g.issue(f.authority, f, move_to(f.move_endpoint)).status == Status::Unsupported,
            "refusal certificate cannot bypass unsupported harmful domain"); }
    { Gate g; auto f = bush(1); auto now = bush(2).authority;
      check(g.issue(now, f, move_to(f.move_endpoint)).status == Status::NeedObservation,
            "hidden reachable stale authority still rejects"); }
    { Gate g; auto f = bush(1); f.move_landing = static_cast<MoveLanding>(99);
      check(g.issue(f.authority, f, move_to(f.move_endpoint)).status == Status::NeedObservation,
            "unrecognized landing evidence fails closed"); }
    { Gate g; auto f = bush(1); auto issued = g.issue(f.authority, f, move_to(f.move_endpoint));
      auto after = f; after.position = f.move_endpoint;
      check(report(g, issued, after, Acceptance::Accepted).status == Status::NeedObservation,
            "hidden true without newer actual view does not prove moved"); }
    { Gate g; auto f = bush(1); f.move_landing = MoveLanding::KnownFree;
      f.refusal_prefix_supported = false;
      check(g.issue(f.authority, f, move_to(f.move_endpoint)).issued(),
            "observed known free retains the ordinary domain-certified move branch"); }
}
}

int main() {
    static_assert(Grammar::basic_only && !Grammar::supports(Kind::Cast));
    static_assert(!std::is_copy_constructible_v<Gate> && !std::is_move_constructible_v<Gate>);
    repeat_ready_work(); member_lifecycle_and_movement(); observation_and_scope_guards();
    refusal_and_one_shot_authority(); effect_validation_and_invalidations();
    hidden_bush_landing_contract();
    std::printf("service_micro: %d checks, %d failures; pure fixture only\n", checks, failures);
    return failures ? 1 : 0;
}
