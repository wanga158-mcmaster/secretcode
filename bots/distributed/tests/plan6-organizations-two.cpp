#include "../bridgecrew/src/policy.h"
#include "../witness/src/policy.h"
#include "../salvage/src/policy.h"
#include "../checkpoints/src/policy.h"
#include "../contingencies/src/policy.h"
#include "../public-modes/src/policy.h"
#include "../recovery-relay/src/policy.h"
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>

using namespace p6;
using namespace p6::organization;
namespace {
int checks = 0;
void expect(bool value, std::string_view label) {
    ++checks;
    if (!value) { std::cerr << "FAIL Plan6 organizations two: " << label << '\n'; std::exit(1); }
}
Actor actor(int ordinal, const Profile& profile) {
    Actor a;
    a.id = 10 + ordinal * 10; a.pick_ordinal = ordinal; a.type = profile.roster[ordinal];
    a.position = {2, 2}; a.health = a.max_health = 100; a.strength = 10;
    a.alive = a.can_move = a.can_attack = true; a.team = Team::ally;
    a.ready_attack_group = 7;
    return a;
}
ScoreEvaluation rank(int group, int score) {
    ScoreEvaluation r;
    r.status = Status::feasible; r.issue = Issue::none;
    r.min_margin = r.nominal_margin = score; r.own_score.fill(score); r.margin.fill(score);
    r.target_id = group; r.occupancy = 6; r.productive_arrival = 1;
    return r;
}
void refresh(Input& in, int round = -1) {
    if (round >= 0) in.frame.clock.round = round;
    ++in.frame.clock.revision;
    for (int i = 0; i < in.frame.actor_count; ++i)
        if (in.frame.actors[i].knowledge == Knowledge::exact)
            in.frame.actors[i].clock = in.frame.clock;
    in.frame.group.clock = in.frame.clock;
    for (int i = 0; i < in.site_count; ++i) {
        auto& site = in.sites[i];
        for (int j = 0; j < site.actor_count; ++j) {
            const auto* a = in.frame.visible(site.actors[j]);
            if (!a) continue;
            const Exposure safe{a->health, a->max_health, 5, 1, 0, 0, true};
            site.cycles[j] = {in.frame.clock, a->id, site.group.id,
                safe, safe, safe, safe, true, true, true, true};
        }
    }
}
std::unique_ptr<Input> input(const Profile& profile, int ordinal = 0, int round = 0) {
    auto storage = std::make_unique<Input>();
    auto& in = *storage;
    in.ordinal = ordinal; in.picks = 3; in.safe_now = true;
    in.frame.clock = {round, 10 + ordinal * 10, 0};
    in.frame.own.actor = actor(ordinal, profile); in.frame.own.gold = 10;
    for (int i = 0; i < 3; ++i) if (i != ordinal) {
        const auto a = actor(i, profile);
        in.frame.actors[in.frame.actor_count++] = {a, in.frame.clock, a.id, Knowledge::exact};
    }
    in.settings = {2000, 1000, 5}; in.rally = {2, 2}; in.bank_approach = {4, 4};
    in.proposed_group = in.current_group = 7; in.independent_group = 8;
    in.phase = Phase::clear; in.site_count = 2;
    for (int i = 0; i < 2; ++i) {
        auto& s = in.sites[i];
        s.group = {7 + i, {5 + i, 2}, {6 + i, 2}, {3 + i, 1}, Lifecycle::living, true, true, true};
        s.known = s.joint_better = true; s.actor_count = 3;
        s.actors = {10, 20, 30, -1, -1, -1};
        for (int j = 0; j < 3; ++j) s.independent[j] = rank(s.group.id, i ? 5 : 10);
        s.joint_rank = rank(s.group.id, 50); s.holder = 10;
    }
    in.frame.group = {in.sites[0].group, in.frame.clock, 7, Knowledge::exact};
    refresh(in); return storage;
}
Actor* ally(Input& in, int ordinal) {
    if (in.ordinal == ordinal) return &in.frame.own.actor;
    for (int i = 0; i < in.frame.actor_count; ++i)
        if (in.frame.actors[i].value.pick_ordinal == ordinal) return &in.frame.actors[i].value;
    return nullptr;
}
void hide(Input& in, int ordinal) {
    for (int i = 0; i < in.frame.actor_count; ++i)
        if (in.frame.actors[i].value.pick_ordinal == ordinal)
            in.frame.actors[i].knowledge = Knowledge::unknown;
}
void enemy(Input& in) {
    Actor a; a.id = 90; a.alive = true; a.team = Team::enemy;
    a.position = in.bank_approach; a.health = a.max_health = 100; a.attack_range = 1;
    in.frame.actors[in.frame.actor_count++] = {a, in.frame.clock, a.id, Knowledge::exact};
}
void rosters() {
    for (const Profile* profile : {&bridgecrew::profile_value, &witness::profile_value,
        &salvage::profile_value, &checkpoints::profile_value, &contingencies::profile_value,
        &public_modes::profile_value, &recovery_relay::profile_value}) {
        for (int i = 0; i < 5; ++i) {
            expect(profile->roster[i] != Champion::cassi && profile->roster[i] != Champion::issac,
                   "roster avoids linked subtype assumptions");
            for (int j = 0; j < i; ++j)
                expect(profile->roster[i] != profile->roster[j], "five distinct role picks");
        }
    }
}
void bridge() {
    auto in_storage = input(bridgecrew::profile_value); auto& in = *in_storage;
    bridgecrew::State active, control;
    expect(bridgecrew::choose(active, in, false).intent == Intent::independent,
           "absence of real passage never fabricates traffic organization");
    in.passage.available = in.passage.personally_clear = in.passage.exit_safe = true;
    in.passage.length = 2; in.passage.tiles = {{{7, 2}, {8, 2}, {}}};
    in.passage.entry = {6, 2}; in.passage.exit = {9, 2}; in.passage.alternate = {6, 3};
    expect(bridgecrew::choose(active, in, false).waypoint == Point{9, 2}, "clear correct window enters");
    refresh(in, 4);
    expect(bridgecrew::choose(active, in, false).intent == Intent::alternate, "wrong window tries listed bypass");
    expect(bridgecrew::choose(control, in, true).intent == Intent::approach, "control follows route without window");
    expect(bridgecrew::choose(active, in, false).intent == Intent::independent, "one alternate then local production");
    refresh(in, 64);
    expect(bridgecrew::choose(active, in, false).intent == Intent::independent, "traffic retirement does not roll during project");
    bridgecrew::State precedence;
    in.passage.loaded_ally_returning = true;
    expect(bridgecrew::choose(precedence, in, false).intent == Intent::alternate, "visible loaded return precedes empty outbound");
    bridgecrew::State inside;
    in.frame.own.actor.position = {7, 2}; refresh(in, 4);
    expect(bridgecrew::choose(inside, in, false).waypoint == Point{9, 2}, "actor already inside clears even at window boundary");
}
void witness_cases() {
    auto in_storage = input(witness::profile_value, 1); auto& in = *in_storage;
    in.sites[0].known = false;
    witness::State active, control;
    expect(witness::choose(active, in, false).intent == Intent::approach, "worker travels only to cheap verification");
    expect(witness::choose(control, in, true).intent == Intent::work, "control commits static project before verification");
    expect(witness::choose(active, in, false).intent == Intent::approach, "visible scout is not shared camp vision");
    in.frame.own.actor.position = in.sites[0].group.approach;
    expect(witness::choose(active, in, false).intent == Intent::approach && active.inspections == 0,
           "pad arrival without a completed model is not a failed inspection");
    refresh(in, 8);
    expect(witness::choose(active, in, false).intent == Intent::independent &&
           !active.verification_active(7, 8), "private probe expires by owner rounds without inventing an inspection");
    refresh(in, 64);
    expect(witness::choose(active, in, false).intent == Intent::approach &&
           active.verification_active(7, 64), "new epoch permits a fresh bounded verification");
    auto bounded_storage = input(witness::profile_value, 1); auto& bounded = *bounded_storage; bounded.sites[0].known = false;
    witness::State waiting;
    for (int i = 0; i < 8; ++i) {
        refresh(bounded, i);
        expect(witness::choose(waiting, bounded, false).intent == Intent::approach, "verification bound permits eight approach turns");
    }
    refresh(bounded, 8);
    expect(witness::choose(waiting, bounded, false).intent == Intent::independent, "ninth verification turn produces elsewhere");
    auto known_storage = input(witness::profile_value, 1); auto& known = *known_storage; witness::State verified;
    expect(known.site(known.independent_group)->solo(known.frame, known.frame.clock.self),
           "the real independent alternative remains available during crew verification");
    const auto crew = witness::choose(verified, known, false);
    expect(crew.intent == Intent::work && crew.group == known.proposed_group &&
           std::string_view(crew.reason) == "witness-personally-verified-crew",
           "current crew verification is reachable alongside its safe solo alternative");
    witness::State static_control;
    const auto committed = witness::choose(static_control, known, true);
    expect(std::string_view(committed.reason) == "witness-control-static-commitment",
           "control retains static commitment with the same competing alternatives");
    known.sites[0].joint_better = false;
    const auto solo = witness::choose(verified, known, false);
    expect(solo.intent == Intent::work && std::string_view(solo.reason) == "witness-personally-verified-solo-cycle",
           "missing current joint benefit retains the genuine safe solo alternative");
    known.sites[0].joint_better = true;
    hide(known, 2);
    const auto missing = witness::choose(verified, known, false);
    expect(missing.intent == Intent::work && std::string_view(missing.reason) == "witness-personally-verified-solo-cycle",
           "unseen partner cannot supply personal crew verification");
    auto stale_storage = input(witness::profile_value, 1); auto& stale = *stale_storage;
    --stale.sites[0].cycles[2].clock.revision;
    witness::State stale_partner;
    const auto stale_choice = witness::choose(stale_partner, stale, false);
    expect(stale_choice.intent == Intent::work &&
           std::string_view(stale_choice.reason) == "witness-personally-verified-solo-cycle",
           "stale partner proof cannot replace the available current solo choice");
    known.frame.own.actor.bag = 10; known.actual_payout = false;
    expect(witness::choose(verified, known, false).intent == Intent::work, "forecast is no payout receipt");
    known.actual_payout = true;
    expect(witness::choose(verified, known, false).intent == Intent::bank, "actual inventory milestone begins conversion");

    const auto appointment = [](int ordinal = 0, int round = 10) {
        auto value = input(witness::profile_value, ordinal, round);
        auto& view = *value;
        const std::array<Point, 3> pads{{{2, 2}, {3, 2}, {4, 3}}};
        for (int i = 0; i < 3; ++i) {
            auto* a = ally(view, i); a->position = pads[i];
            a->ranged = i != 2; a->attack_range = i == 2 ? 1 : 3;
        }
        view.sites[0].group.spawn = {3, 3};
        view.sites[0].group.approach = {2, 3};
        view.frame.group.value = view.sites[0].group;
        view.sites[0].actor_count = 0; view.sites[0].joint_better = false;
        view.rally = pads[ordinal]; view.at_rally = true; view.verification_travel = 0;
        view.witness_core_basic = {std::uint8_t{7}, view.frame.clock, view.proposed_group, Knowledge::exact};
        return value;
    };
    const auto advance = [](Input& view, int round, std::uint8_t bits = 7) {
        refresh(view, round);
        view.at_rally = view.frame.own.actor.position == view.rally;
        view.witness_core_basic = {bits, view.frame.clock, view.proposed_group, Knowledge::exact};
    };
    auto ready_storage = appointment(); auto& ready_view = *ready_storage;
    witness::Policy rendezvous;
    const auto basic = rendezvous.choose(ready_view, false);
    expect(basic.intent == Intent::approach && basic.explicit_waypoint && basic.waypoint == ready_view.rally &&
           std::string_view(basic.reason) == "witness-personally-verified-basic-crew" &&
           !ready_view.sites[0].solo(ready_view.frame, ready_view.frame.clock.self),
           "current basic crew verifies distinct pads without granting solo or joint work");
    expect(rendezvous.state.successful_core_verifications == 1 && rendezvous.state.inspections == 0,
           "setup deferral records current crew but consumes no completed failure");
    rendezvous.choose(ready_view, false);
    expect(rendezvous.state.successful_core_verifications == 1 && rendezvous.state.inspections == 0,
           "repeated same-clock deferral does not duplicate verification");
    ready_view.verification_model_complete = true;
    rendezvous.choose(ready_view, false); rendezvous.choose(ready_view, false);
    expect(rendezvous.state.inspections == 1 && rendezvous.verification_active(7, 10),
           "one completed at-pad failure is counted once per owner round");
    advance(ready_view, 11);
    expect(rendezvous.choose(ready_view, false).intent == Intent::independent &&
           rendezvous.state.inspections == 2 && !rendezvous.verification_active(7, 11),
           "leader retires after two completed verified failures");
    ready_view.actual_bank = ready_view.restored = true; advance(ready_view, 12);
    expect(rendezvous.choose(ready_view, false).intent == Intent::independent &&
           rendezvous.state.inspections == 2, "bank and restoration do not renew retired same-epoch verification");
    ready_view.actual_bank = ready_view.restored = false; ready_view.verification_model_complete = false;
    advance(ready_view, 64);
    expect(rendezvous.verification_active(7, 64) && rendezvous.choose(ready_view, false).intent == Intent::approach,
           "policy hook allows new epoch without reviving old inspection rights");

    for (int unavailable = 0; unavailable < 8; ++unavailable) {
        auto value = appointment(); auto& view = *value;
        if (unavailable == 0) view.witness_core_basic = {};
        if (unavailable == 1) view.witness_core_basic.value = 3;
        if (unavailable == 2) --view.witness_core_basic.clock.revision;
        if (unavailable == 3) view.witness_core_basic.subject = 8;
        if (unavailable == 4) hide(view, 2);
        if (unavailable == 5) ally(view, 2)->can_attack = false;
        if (unavailable == 6) view.sites[0].known = false;
        if (unavailable == 7) view.sites[0].group.all_spawns_visible = false;
        view.verification_model_complete = true;
        witness::State absent;
        const auto next = witness::choose(absent, view, false);
        expect(next.intent == Intent::approach &&
               std::string_view(next.reason) == "witness-cheap-personal-verification" &&
               absent.successful_core_verifications == 0 && absent.inspections == 0,
               "missing bits, stale source or unavailable real core cannot complete crew inspection");
    }
    auto certified_storage = appointment(); auto& certified = *certified_storage;
    certified.sites[0].actor_count = 3; certified.sites[0].joint_better = true;
    advance(certified, 10); witness::State certified_state;
    expect(std::string_view(witness::choose(certified_state, certified, false).reason) ==
           "witness-personally-verified-crew", "existing complete certificates retain the genuine joint speedup path");
    certified.witness_core_basic = {};
    expect(std::string_view(witness::choose(certified_state, certified, false).reason) ==
           "witness-personally-verified-solo-cycle", "missing crew access cannot hide a genuinely safe solo alternative");
    auto follower_storage = appointment(1, 20); auto& follower = *follower_storage;
    follower.verification_model_complete = true; witness::State finite_checks;
    for (int i = 0; i < 8; ++i) {
        advance(follower, 20 + i);
        const auto next = witness::choose(finite_checks, follower, false);
        expect(next.intent == (i < 7 ? Intent::approach : Intent::independent),
               "follower permits exactly eight completed at-pad checks");
    }

    auto travel_storage = appointment(); auto& travel = *travel_storage;
    travel.frame.own.actor.position = {0, 0}; travel.at_rally = false; travel.verification_travel = 2;
    witness::State finite_travel;
    expect(witness::choose(finite_travel, travel, false).intent == Intent::approach &&
           finite_travel.record(7, 0)->travel_deadline == 20, "first public travel distance fixes appointment deadline");
    travel.frame.own.actor.position = {1, 1}; travel.verification_travel = 1;
    travel.proposed_group = 8; travel.sites[1].actor_count = 0; travel.sites[1].joint_better = false;
    advance(travel, 11); witness::choose(finite_travel, travel, false);
    travel.proposed_group = 7; travel.actual_bank = travel.restored = true; advance(travel, 12);
    witness::choose(finite_travel, travel, false);
    expect(finite_travel.record(7, 0)->travel_deadline == 20 && finite_travel.inspections == 0,
           "source switch, shorter route, bank and restoration cannot renew travel or spend inspections");
    travel.actual_bank = travel.restored = false; advance(travel, 19);
    expect(witness::choose(finite_travel, travel, false).intent == Intent::approach,
           "travel remains available strictly before fixed deadline");
    advance(travel, 20);
    expect(witness::choose(finite_travel, travel, false).intent == Intent::independent &&
           !finite_travel.verification_active(7, 20), "travel deadline retires without converting deferral into failure");

    auto arrival_storage = appointment(); auto& arrival = *arrival_storage;
    arrival.verification_travel = 4; arrival.at_rally = false; arrival.frame.own.actor.position = {0, 0};
    witness::State finite_wait; witness::choose(finite_wait, arrival, false);
    // These are current observations of an early arrival, not forecast movement credit.
    arrival.frame.own.actor.position = arrival.rally; arrival.verification_travel = 0; advance(arrival, 12);
    witness::choose(finite_wait, arrival, false);
    arrival.frame.own.actor.position = {1, 1}; arrival.verification_travel = 1; advance(arrival, 19);
    expect(witness::choose(finite_wait, arrival, false).intent == Intent::approach &&
           finite_wait.record(7, 0)->arrival_round == 12 && finite_wait.inspections == 0,
           "leaving pad does not renew eight-round crew wait or invent completed inspection");
    arrival.frame.own.actor.position = arrival.rally; arrival.verification_travel = 0; advance(arrival, 20);
    expect(witness::choose(finite_wait, arrival, false).intent == Intent::independent &&
           finite_wait.record(7, 0)->travel_deadline == 22, "arrival wait expires before later travel deadline");

    auto protected_storage = appointment(); auto& protected_view = *protected_storage;
    witness::State protected_state; protected_state.record(7, 0)->retired = true;
    protected_view.phase = Phase::hold; protected_view.frame.own.actor.spawn_group = 7;
    expect(witness::choose(protected_state, protected_view, false).intent == Intent::work &&
           !protected_state.verification_active(7, 10), "owned capture precedes retired appointment without restoring rights");
    protected_view.actual_payout = true; protected_view.frame.own.actor.bag = 4;
    expect(witness::choose(protected_state, protected_view, false).intent == Intent::bank,
           "actual payout inventory precedes appointment");
    protected_view.frame.own.actor.channel = Channel::recall; protected_view.frame.own.actor.channel_valid = true;
    expect(witness::choose(protected_state, protected_view, false).intent == Intent::independent,
           "valid channel is not interrupted by verification or payout proposal");
    protected_view.frame.own.actor.channel = Channel::none; protected_view.safe_now = false;
    expect(witness::choose(protected_state, protected_view, false).intent == Intent::independent,
           "current safety diversion precedes appointment");
    auto contrast_storage = appointment(); auto& contrast = *contrast_storage; contrast.witness_core_basic = {};
    witness::State static_appointment;
    expect(std::string_view(witness::choose(static_appointment, contrast, true).reason) ==
           "witness-control-static-commitment" && static_appointment.group == -1,
           "control ignores appointment verification and keeps static contrast");
    auto extension_storage = input(witness::profile_value, 3); auto& extension = *extension_storage;
    extension.picks = 4; extension.sites[0].actor_count = 0; extension.sites[0].joint_better = false;
    extension.verification_travel = 0; extension.rally = {9, 9}; extension.at_rally = false;
    extension.witness_core_basic = {std::uint8_t{7}, extension.frame.clock, 7, Knowledge::exact};
    witness::State extension_state;
    expect(witness::choose(extension_state, extension, false).waypoint == extension.sites[0].group.approach &&
           extension_state.successful_core_verifications == 0, "extension does not inherit three-core appointment authority");
}
// Untyped observed_failure inputs remain pure peer/membership fixtures, not
// confirmed native peer failures. Native self retirement is tested separately
// below and must never license the peer-repair branch.
void salvage_cases() {
    auto in_storage = input(salvage::profile_value); auto& in = *in_storage; in.observed_failure = true; in.sites[0].damaged_now = true;
    salvage::State active, control;
    expect(salvage::choose(active, in, false).intent == Intent::work && active.holder == 10,
           "preserved visible work elects lowest safe current holder");
    expect(salvage::choose(control, in, true).intent == Intent::independent, "control abandons same membership failure");
    expect(salvage::choose(active, in, false).intent == Intent::work, "persistent failure receipt is one repair event");
    refresh(in, 64);
    expect(salvage::choose(active, in, false).intent == Intent::work && active.epoch == 0,
           "repair budget is latched through project, not reset every 64 rounds");
    in.observed_failure = false; salvage::choose(active, in, false);
    in.observed_failure = true;
    expect(salvage::choose(active, in, false).intent == Intent::independent, "second actual failure retires repaired project");
    auto renewed_storage = input(salvage::profile_value); auto& renewed = *renewed_storage; renewed.observed_failure = true;
    salvage::State no_preservation;
    expect(salvage::choose(no_preservation, renewed, false).intent == Intent::independent,
           "old damage is not assumed to persist in renewed monsters");
    auto replacement_storage = input(salvage::profile_value, 1); auto& replacement = *replacement_storage; replacement.observed_failure = true;
    replacement.sites[0].damaged_now = true; ally(replacement, 0)->alive = false; refresh(replacement);
    salvage::State new_holder;
    expect(salvage::choose(new_holder, replacement, false).intent == Intent::work && new_holder.holder == 20,
           "visible dead holder produces actual surviving role transfer");
    auto helper_storage = input(salvage::profile_value, 1); auto& helper = *helper_storage; helper.observed_failure = true;
    helper.sites[0].damaged_now = true;
    salvage::State released;
    expect(salvage::choose(released, helper, false).intent == Intent::work, "visible ready holder licenses useful repair help");
    helper.sites[0].group.lifecycle = Lifecycle::reward_available; ally(helper, 0)->spawn_group = 7;
    expect(salvage::choose(released, helper, false).intent == Intent::independent, "reduced holder releases redundant occupant before payout");
}
void salvage_self_failure_cases() {
    const auto find_epoch = [](const salvage::State& state, ProjectID project)
        -> const salvage::State::Epoch* {
        for (const auto& entry : state.history)
            if (entry.group == project.group && entry.epoch == project.epoch) return &entry;
        return nullptr;
    };
    const ProjectID failed{7, 0};
    salvage::Policy policy;
    expect(!policy.receive_failure({-1, 0}, Failure::observed_unavailable) &&
           !policy.receive_failure({7, -1}, Failure::observed_unavailable) &&
           !policy.receive_failure(failed, Failure::unsafe),
           "malformed or non-action self failures cannot occupy the durable inbox");
    expect(policy.receive_failure(failed, Failure::observed_unavailable),
           "policy accepts the exact already-retired self project");
    const auto accepted = policy.state.pending_self;
    expect(policy.receive_failure(failed, Failure::observed_unavailable) &&
           policy.state.pending_self == accepted,
           "duplicate pending delivery acknowledges without changing the token");
    expect(!policy.receive_failure({8, 1}, Failure::observed_failed_action) &&
           policy.state.pending_self == accepted &&
           !policy.receive_failure(failed, Failure::observed_failed_action) &&
           policy.state.pending_self == accepted,
           "busy inbox rejects another project or reason without overwriting the first failure");

    auto in_storage = input(salvage::profile_value); auto& in = *in_storage;
    in.sites[0].damaged_now = in.sites[1].damaged_now = true;
    // This notice describes private executor retirement, not a peer failure.
    in.observed_failure = false;
    in.safe_now = false;
    expect(policy.choose(in, false).intent == Intent::independent &&
           policy.state.pending_self == accepted && !find_epoch(policy.state, failed),
           "unsafe boundary defers durable self failure without repairing or retiring policy history");
    in.safe_now = true;
    const auto self = in.frame.clock.self;
    in.frame.clock.self = -1;
    expect(policy.choose(in, false).intent == Intent::independent &&
           policy.state.pending_self == accepted && !find_epoch(policy.state, failed),
           "invalid frame cannot consume the accepted failure notice");
    in.frame.clock.self = self; refresh(in, 1);
    for (const auto channel : {Channel::recall, Channel::tribute, Channel::teleport}) {
        in.frame.own.actor.channel = channel; in.frame.own.actor.channel_valid = true;
        expect(policy.choose(in, false).intent == Intent::independent &&
               policy.state.pending_self == accepted && !find_epoch(policy.state, failed),
               "valid channel keeps the self-failure token pending across policy entry");
    }
    in.frame.own.actor.channel = Channel::none; in.frame.own.actor.channel_valid = false;
    in.actual_payout = true; in.frame.own.actor.bag = 4;
    expect(policy.choose(in, false).intent == Intent::bank &&
           policy.state.pending_self == accepted && !find_epoch(policy.state, failed),
           "actual payout secures inventory before consuming the deferred failure");

    in.actual_payout = false; in.current_group = -1; in.proposed_group = in.independent_group = 8;
    refresh(in, 65);
    const auto consumed = policy.choose(in, false);
    const auto* retired = find_epoch(policy.state, failed);
    expect(consumed.intent == Intent::bank &&
           std::string_view(consumed.reason) == "salvage-retired-self-project" &&
           policy.state.pending_self.reason == Failure::none && policy.state.handled_self == accepted,
           "later safe boundary consumes once while preserving real carried inventory");
    expect(retired && retired->retired && !retired->repaired &&
           !find_epoch(policy.state, {8, 1}) && policy.state.group == -1 && policy.state.holder == -1,
           "self retirement closes the exact old epoch without repairing or blaming the proposed source");
    expect(policy.receive_failure(failed, Failure::observed_unavailable) &&
           policy.state.pending_self.reason == Failure::none,
           "handled duplicate cannot enqueue another self repair");
    in.frame.own.actor.bag = 0; refresh(in, 66);
    const auto alternative = policy.choose(in, false);
    const auto* independent_epoch = find_epoch(policy.state, {8, 1});
    expect(alternative.intent == Intent::work && alternative.group == 8 &&
           independent_epoch && !independent_epoch->retired && !independent_epoch->repaired &&
           policy.state.handled_self == accepted,
           "fresh competing source remains productive without replenishing the failed epoch");
    in.proposed_group = in.independent_group = 7; refresh(in, 128);
    expect(policy.receive_failure(failed, Failure::observed_unavailable) &&
           policy.state.pending_self.reason == Failure::none,
           "old duplicate remains acknowledged after the source becomes eligible in a later epoch");
    const auto renewed = policy.choose(in, false);
    const auto* renewal = find_epoch(policy.state, {7, 2});
    expect(renewed.intent == Intent::work && renewed.group == 7 && renewal &&
           !renewal->retired && !renewal->repaired && policy.state.handled_self == accepted,
           "old acknowledged retirement does not poison the fresh same-source epoch");

    const ProjectID second{8, 1};
    expect(policy.receive_failure(second, Failure::observed_failed_action),
           "consumed inbox accepts a later distinct self retirement");
    const auto pending_second = policy.state.pending_self;
    expect(policy.receive_failure(failed, Failure::observed_unavailable) &&
           policy.state.pending_self == pending_second &&
           !policy.receive_failure({9, 1}, Failure::observed_unavailable) &&
           policy.state.pending_self == pending_second,
           "handled acknowledgement cannot displace another accepted pending token");
    const auto second_decision = policy.choose(in, false);
    const auto* second_retired = find_epoch(policy.state, second);
    expect(second_decision.intent == Intent::independent && second_decision.group == 7 &&
           second_retired && second_retired->retired && !second_retired->repaired &&
           find_epoch(policy.state, {7, 2}) && !find_epoch(policy.state, {7, 2})->retired,
           "next self failure retires its own project while the current alternative keeps its rights");

    auto control_storage = input(salvage::profile_value); auto& control = *control_storage;
    control.current_group = -1; control.proposed_group = control.independent_group = 8;
    salvage::Policy control_policy;
    expect(control_policy.receive_failure(failed, Failure::observed_failed_action),
           "control arm receives the same typed self-retirement protocol");
    const auto control_decision = control_policy.choose(control, true);
    const auto* control_retired = find_epoch(control_policy.state, failed);
    expect(control_decision.intent == Intent::independent && control_decision.group == 8 &&
           control_retired && control_retired->retired &&
           !control_retired->repaired && !find_epoch(control_policy.state, {8, 0}),
           "control consumes self retirement without fabricating a peer repair or retiring the fallback");

    auto full_storage = input(salvage::profile_value, 0, 640); auto& full = *full_storage;
    full.current_group = -1; full.proposed_group = full.independent_group = 8;
    full.frame.own.actor.bag = 9;
    salvage::Policy saturated;
    for (std::size_t i = 0; i < saturated.state.history.size(); ++i)
        saturated.state.history[i] = {100 + static_cast<int>(i), 10, i % 2 == 0, i % 3 == 0};
    const auto original_history = saturated.state.history;
    const auto history_unchanged = [&] {
        for (std::size_t i = 0; i < original_history.size(); ++i) {
            const auto& before = original_history[i]; const auto& after = saturated.state.history[i];
            if (before.group != after.group || before.epoch != after.epoch ||
                before.repaired != after.repaired || before.retired != after.retired) return false;
        }
        return true;
    };
    expect(saturated.receive_failure(failed, Failure::observed_unavailable),
           "history capacity does not prevent durable receipt of an already-retired self project");
    const auto terminal = saturated.choose(full, false);
    expect(terminal.intent == Intent::bank &&
           std::string_view(terminal.reason) == "salvage-retired-self-project" &&
           saturated.state.pending_self.reason == Failure::none &&
           saturated.state.handled_self.project == failed && history_unchanged(),
           "full history terminally consumes the notice, secures inventory and preserves every occupied slot");
    expect(saturated.receive_failure(failed, Failure::observed_unavailable) &&
           saturated.state.pending_self.reason == Failure::none,
           "full-history duplicate is acknowledged without an infinite pending retry");
    const auto still_secure = saturated.choose(full, false);
    expect(still_secure.intent == Intent::bank &&
           std::string_view(still_secure.reason) == "salvage-secure-real-inventory" && history_unchanged(),
           "subsequent saturated choice remains ordinary inventory security rather than repeat retirement");
    expect(saturated.receive_failure({9, 0}, Failure::observed_failed_action) &&
           saturated.choose(full, false).intent == Intent::bank &&
           saturated.state.pending_self.reason == Failure::none && history_unchanged(),
           "terminal consumption frees inbox capacity without evicting unrelated repair history");
}
void checkpoint_cases() {
    auto in_storage = input(checkpoints::profile_value, 1); auto& in = *in_storage;
    checkpoints::State active, control;
    expect(checkpoints::choose(active, in, false).intent == Intent::work, "initial checkpoint signs visible capable core");
    expect(checkpoints::choose(control, in, true).intent == Intent::work, "whole expedition control initially admits same core");
    hide(in, 2); refresh(in);
    expect(checkpoints::choose(active, in, false).intent == Intent::work, "membership does not reschedule inside signed stage");
    in.phase = Phase::hold;
    expect(checkpoints::choose(active, in, false).intent == Intent::independent, "next checkpoint fails personal re-signing and releases");
    expect(checkpoints::choose(control, in, true).intent == Intent::work, "control retains whole-expedition commitment");
    auto missing_storage = input(checkpoints::profile_value); auto& missing = *missing_storage; hide(missing, 2); refresh(missing);
    checkpoints::State bounded;
    for (int i = 0; i < 8; ++i)
        expect(checkpoints::choose(bounded, missing, false).intent == Intent::rally, "initial checkpoint contact is bounded");
    expect(checkpoints::choose(bounded, missing, false).intent == Intent::independent, "ninth missed signing releases to production");
    missing.phase = Phase::hold; missing.frame.own.actor.spawn_group = 7;
    checkpoints::State holding;
    expect(checkpoints::choose(holding, missing, false).intent == Intent::work, "missing participant cannot revoke safe own capture");
    missing.frame.own.actor.channel = Channel::tribute; missing.frame.own.actor.channel_valid = true;
    expect(checkpoints::choose(holding, missing, false).intent == Intent::independent, "valid channel bypasses organization renewal");
}
void contingency_cases() {
    auto in_storage = input(contingencies::profile_value, 1); auto& in = *in_storage;
    ally(in, 0)->channel = Channel::recall; refresh(in);
    contingencies::State active, control;
    expect(contingencies::choose(active, in, false).intent == Intent::work && active.replacement == 20,
           "visible holder recall transfers capture to safe executing survivor");
    expect(contingencies::choose(control, in, true).intent == Intent::independent, "generic control restarts on same recall failure");
    auto unknown_storage = input(contingencies::profile_value, 1); auto& unknown = *unknown_storage; hide(unknown, 0); refresh(unknown);
    contingencies::State absent;
    contingencies::choose(absent, unknown, false);
    expect(absent.branch == contingencies::Branch::none, "unseen holder is not a recall or death premise");
    auto blocked_storage = input(contingencies::profile_value); auto& blocked = *blocked_storage; blocked.sites[0].blocked = true;
    contingencies::State alternate;
    expect(contingencies::choose(alternate, blocked, false).waypoint == blocked.sites[0].group.retreat,
           "blocked approach follows listed common alternate");
    ally(blocked, 1)->position = blocked.sites[0].group.retreat;
    for (int i = 0; i < 4; ++i)
        expect(contingencies::choose(alternate, blocked, false).intent == Intent::rally,
               "visible destination conflict permits at most four rally turns");
    expect(contingencies::choose(alternate, blocked, false).intent == Intent::independent, "conflict leaves to solo after four");
    refresh(blocked, 64);
    expect(contingencies::choose(alternate, blocked, false).intent == Intent::independent, "conflict retirement cannot restart on clock rollover");
    auto permanent_block_storage = input(contingencies::profile_value); auto& permanent_block = *permanent_block_storage; permanent_block.sites[0].blocked = true;
    contingencies::State finite_branch;
    for (int i = 0; i < 8; ++i)
        expect(contingencies::choose(finite_branch, permanent_block, false).intent == Intent::alternate,
               "listed alternate branch has an eight-turn physical bound");
    expect(contingencies::choose(finite_branch, permanent_block, false).intent == Intent::independent,
           "permanent obstruction retires alternate into productive fallback");
    auto support_storage = input(contingencies::profile_value); auto& support = *support_storage; ally(support, 1)->alive = false; refresh(support);
    contingencies::State lower_group;
    const auto branch = contingencies::choose(lower_group, support, false);
    expect(branch.intent == Intent::work && branch.group == 8, "visible support failure chooses declared safe lower group");
}
void public_cases() {
    using public_modes::Mode;
    expect(public_modes::prescribed(63) == Mode::circulate && public_modes::prescribed(64) == Mode::deliver &&
           public_modes::prescribed(80) == Mode::contest && public_modes::prescribed(96) == Mode::circulate,
           "pure public 64/16/16 prescription");
    auto in_storage = input(public_modes::profile_value); auto& in = *in_storage; in.frame.own.actor.bag = 10;
    public_modes::State active, control;
    expect(public_modes::choose(active, in, false).intent == Intent::independent, "clock circulate keeps complete independent cycle");
    expect(public_modes::choose(control, in, true).intent == Intent::bank, "local comparator chooses feasible higher delivery template");
    refresh(in, 64); in.actual_payout = true;
    expect(public_modes::choose(active, in, false).intent == Intent::bank, "deliver mode changes actual converter action");
    auto escort_storage = input(public_modes::profile_value, 1, 64); auto& escort = *escort_storage; ally(escort, 0)->bag = 10;
    public_modes::State present;
    expect(public_modes::choose(present, escort, false).intent == Intent::escort, "personally visible loaded converter at rendezvous licenses escort");
    ally(escort, 0)->position = {9, 9};
    expect(public_modes::choose(present, escort, false).intent == Intent::independent, "converter elsewhere does not staff rendezvous");
    ally(escort, 0)->position = escort.rally; escort.sites[0].joint_better = false;
    expect(public_modes::choose(present, escort, false).intent == Intent::independent, "escort cannot displace superior independent score");
    auto intercept_storage = input(public_modes::profile_value, 2, 80); auto& intercept = *intercept_storage; enemy(intercept);
    public_modes::State bounded;
    for (int i = 0; i < 8; ++i)
        expect(public_modes::choose(bounded, intercept, false).intent == Intent::intercept, "visible intercept has finite eight-turn opportunity");
    expect(public_modes::choose(bounded, intercept, false).intent == Intent::independent, "interceptor resumes productive cycle after bound");
    auto lag_storage = input(public_modes::profile_value, 0, 63); auto& lag = *lag_storage; public_modes::State finishing;
    public_modes::choose(finishing, lag, false); lag.frame.own.actor.bag = 10; refresh(lag, 64);
    expect(public_modes::choose(finishing, lag, false).intent == Intent::independent && finishing.execution_lag == 1,
           "public boundary records lag while safe current stage finishes");
    lag.actual_payout = true;
    expect(public_modes::choose(finishing, lag, false).intent == Intent::bank, "actual stage completion releases delayed mode");
    auto winner_storage = input(public_modes::profile_value); auto& winner = *winner_storage; winner.at_bank = true;
    winner.frame.own.actor.bag = 10; winner.frame.own_team_score = 990;
    public_modes::State first;
    expect(public_modes::choose(first, winner, false).intent == Intent::bank, "fresh own legal winning bank overrides clock");
    auto later_storage = input(public_modes::profile_value, 1); auto& later = *later_storage; later.frame.own_team_score = 1000;
    public_modes::State second;
    expect(public_modes::choose(second, later, false).intent == Intent::independent &&
           first.prescribed_mode == second.prescribed_mode,
           "score crossing between ally turns changes no common score snapshot or prescription");
    auto ranked_storage = input(public_modes::profile_value); auto& ranked = *ranked_storage;
    ranked.frame.own.actor.bag = 10; ranked.role_ranks_current = true;
    public_modes::State unavailable_rank;
    expect(public_modes::choose(unavailable_rank, ranked, true).intent == Intent::independent,
           "current runtime flag forbids invented delivery rank from a site's productive forecast");
    ranked.delivery_rank = rank(90, 60); public_modes::State feasible_rank;
    expect(public_modes::choose(feasible_rank, ranked, true).intent == Intent::bank,
           "explicit current bank-only rank selects feasible delivery template");
    ranked.delivery_rank = rank(90, 1); public_modes::State inferior_rank;
    expect(public_modes::choose(inferior_rank, ranked, true).intent == Intent::independent,
           "current inferior bank-only rank loses to complete independent production");
    auto local_contest_storage = input(public_modes::profile_value, 2); auto& local_contest = *local_contest_storage;
    enemy(local_contest); local_contest.role_ranks_current = true;
    public_modes::State absent_contest;
    expect(public_modes::choose(absent_contest, local_contest, true).intent == Intent::independent,
           "site joint rank is not substituted for missing explicit runtime contest rank");
    local_contest.contest_rank = rank(7, 80); public_modes::State feasible_contest;
    expect(public_modes::choose(feasible_contest, local_contest, true).intent == Intent::intercept,
           "current explicit joint rescue rank selects visible contest template");
}
void relay_cases() {
    auto in_storage = input(recovery_relay::profile_value, 1); auto& in = *in_storage; ally(in, 0)->bag = 10;
    recovery_relay::State active, control;
    expect(recovery_relay::choose(active, in, false).intent == Intent::work, "feasible remainder keeps actual producer working");
    expect(recovery_relay::choose(control, in, true).intent == Intent::recover, "control sends admitted core to service together");
    auto returning_storage = input(recovery_relay::profile_value); auto& returning = *returning_storage; returning.frame.own.actor.bag = 10;
    recovery_relay::State relay;
    expect(recovery_relay::choose(relay, returning, false).intent == Intent::bank, "scheduled return leaves only after visible useful remainder");
    returning.frame.own.actor.bag = 0; returning.actual_bank = true;
    expect(recovery_relay::choose(relay, returning, false).intent == Intent::rally, "serviced actor approaches current visible crew before rejoin");
    returning.at_rally = true;
    expect(recovery_relay::choose(relay, returning, false).intent == Intent::work, "personal rally contact completes rejoin");
    auto slow_return_storage = input(recovery_relay::profile_value); auto& slow_return = *slow_return_storage; slow_return.frame.own.actor.bag = 10;
    recovery_relay::State finite_rejoin;
    recovery_relay::choose(finite_rejoin, slow_return, false);
    slow_return.frame.own.actor.bag = 0; slow_return.actual_bank = true;
    for (int i = 0; i < 8; ++i)
        expect(recovery_relay::choose(finite_rejoin, slow_return, false).intent == Intent::rally,
               "returning actor spends at most eight turns approaching visible rejoin");
    expect(recovery_relay::choose(finite_rejoin, slow_return, false).intent == Intent::independent &&
           recovery_relay::choose(finite_rejoin, slow_return, false).intent == Intent::independent,
           "rejoin timeout stays productive without silently rejoining old project");
    auto missing_storage = input(recovery_relay::profile_value, 1); auto& missing = *missing_storage; ally(missing, 0)->bag = 10;
    hide(missing, 2); refresh(missing); recovery_relay::State unavailable;
    expect(recovery_relay::choose(unavailable, missing, false).intent == Intent::recover, "unseen remainder cannot be called productive");
    auto urgent_storage = input(recovery_relay::profile_value, 2, 0); auto& urgent = *urgent_storage; urgent.safe_now = false;
    recovery_relay::State survival;
    expect(recovery_relay::choose(survival, urgent, false).intent == Intent::recover, "survival does not wait for actor's service epoch");
    auto holder_storage = input(recovery_relay::profile_value); auto& holder = *holder_storage; holder.frame.own.actor.bag = 10;
    holder.phase = Phase::hold; holder.frame.own.actor.spawn_group = 7;
    recovery_relay::State capture;
    expect(recovery_relay::choose(capture, holder, false).intent == Intent::work, "relay does not abandon sole valid capture");
}
void immediate_exposure_cases() {
    auto storage=input(witness::profile_value);auto& in=*storage;
    runtime_threat::Enemy threat;
    threat.id=90;threat.position={6,2};threat.stress=20;
    threat.move_speed=1;threat.attack_range=1;threat.complete=true;
    in.local_enemy_count=in.enemy_stress_count=1;
    in.local_enemies[0]={threat,in.frame.clock,threat.id,Knowledge::exact};
    in.enemy_stress[0]={threat.stress,in.frame.clock,threat.id,Knowledge::exact};
    const auto raw=in.enemy_stress;
    const auto prepared=prepare_immediate(in);
    expect(prepared.complete && prepared.clock==in.frame.clock && prepared.count==1 &&
           prepared.entries[0].id==90 && prepared.entries[0].stress==20,
           "prepared wrapper publishes the current complete raw-ID join and its clock");
    auto changed_clock=in.frame.clock;++changed_clock.revision;
    const auto stale_snapshot=runtime_threat::exposure(in.frame.own.actor,changed_clock,prepared,5);
    expect(!stale_snapshot.observations_complete && !survives(stale_snapshot),
           "caller cannot reuse a prepared raw-ID join after an action revision");
    const auto far=immediate_exposure(in,in.frame.own.actor.position,5);
    expect(far.observations_complete && far.incoming_total==5 && far.next_phase_incoming==5,
           "wrapper excludes disproven distant offense while charging camp damage once");
    expect(in.enemy_stress==raw && in.local_enemies[0].current(in.frame.clock,90) &&
           in.local_enemies[0].value.stress==20 && in.local_enemies[0].value.position==Point{6,2},
           "local query preserves current raw full-cycle stress and enemy records");

    const Point boundary{3,2};
    const auto inside=immediate_exposure(in,boundary,5);
    expect(inside.observations_complete && inside.incoming_total==45 && inside.next_phase_incoming==25 &&
           survives(inside) && in.frame.own.actor.position==Point{2,2},
           "adjacent candidate crossing the reach boundary uses candidate position without changing the frame");
    auto completed=std::make_unique<Input>(in);completed->local_enemies[0].value.completed_turn=true;
    const auto one_turn=immediate_exposure(*completed,{4,2},5);
    expect(one_turn.observations_complete && one_turn.incoming_total==25 && one_turn.next_phase_incoming==25,
           "fresh completed-turn record charges one enemy turn and one camp phase");
    const auto two_turns=immediate_exposure(in,{4,2},5);
    expect(two_turns.observations_complete && two_turns.incoming_total==45 && two_turns.next_phase_incoming==25,
           "unfinished current enemy charges two turns with the same single camp phase");

    auto rejects=[&](auto mutate,std::string_view label) {
        auto bad=std::make_unique<Input>(in);mutate(*bad);
        const auto rejected=prepare_immediate(*bad);
        const auto exposure=immediate_exposure(*bad,boundary);
        expect(!rejected.complete && !exposure.observations_complete && !survives(exposure),label);
    };
    rejects([](Input& bad){bad.local_enemy_count=0;},
            "missing local enemy cannot hide a current full-stress record");
    rejects([](Input& bad){bad.enemy_stress_count=0;},
            "missing full-stress record cannot authorize a local discount");
    rejects([](Input& bad){++bad.frame.clock.revision;},
            "new action revision cannot reuse old current enemy records");
    rejects([](Input& bad){--bad.local_enemies[0].clock.round;},
            "stale local clock fails closed despite a current full-stress match");
    rejects([](Input& bad){--bad.enemy_stress[0].clock.revision;},
            "stale full-stress clock fails closed despite current local geometry");
    rejects([](Input& bad){bad.local_enemies[0].subject=bad.current_group;},
            "a source-group stamp is not an enemy identity stamp");
    rejects([](Input& bad){++bad.local_enemies[0].value.id;},
            "local payload identity must match its observation and full-stress identity");
    rejects([](Input& bad){++bad.enemy_stress[0].subject;},
            "wrong full-stress identity cannot match the local enemy");
    rejects([](Input& bad){++bad.local_enemies[0].value.stress;},
            "local geometry cannot silently replace the raw full damage bound");
    rejects([](Input& bad){bad.local_enemies[0].knowledge=Knowledge::unknown;},
            "unknown local observation does not gain exact geometry authority");
    rejects([](Input& bad){
        bad.local_enemy_count=bad.enemy_stress_count=2;
        bad.local_enemies[1]=bad.local_enemies[0];
        bad.enemy_stress[1]=bad.enemy_stress[0];++bad.enemy_stress[1].subject;
    },"duplicate local mapping cannot conceal the omitted second full-stress enemy");

    auto aggregate=std::make_unique<Input>(in);
    aggregate->local_enemy_count=aggregate->enemy_stress_count=2;
    auto second=threat;second.id=91;
    aggregate->local_enemies[1]={second,in.frame.clock,second.id,Knowledge::exact};
    aggregate->enemy_stress[1]={second.stress,in.frame.clock,second.id,Knowledge::exact};
    const auto combined=immediate_exposure(*aggregate,boundary,5);
    expect(combined.observations_complete && combined.incoming_total==85 &&
           combined.next_phase_incoming==45 && !survives(combined),
           "wrapper sums two individually survivable nearby enemies before the safety reserve");
    auto unknown=std::make_unique<Input>(in);unknown->local_enemies[0].value.complete=false;
    const auto fallback=immediate_exposure(*unknown,in.frame.own.actor.position,5);
    expect(fallback.observations_complete && fallback.incoming_total==45 &&
           fallback.next_phase_incoming==25 && unknown->enemy_stress==raw,
           "unknown geometry retains full current stress without changing forecast records");

    auto reset=std::make_unique<Input>(in);auto fresh=in.frame;
    ++fresh.clock.round;++fresh.clock.revision;reset->reset_current();
    expect(reset->local_enemy_count==0 && reset->enemy_stress_count==0 && !reset->frame.valid() &&
           !prepare_immediate(*reset).complete,
           "reset clears enemy publication counts and requires a fresh frame");
    reset->frame=fresh;
    const auto empty_prepared=prepare_immediate(*reset);
    const auto empty=immediate_exposure(*reset,fresh.own.actor.position,5);
    expect(reset->local_enemies[0].value.id==90 && reset->enemy_stress[0].value==20 &&
           empty_prepared.complete && empty_prepared.clock==fresh.clock && empty_prepared.count==0 &&
           empty.observations_complete && empty.incoming_total==5 && empty.next_phase_incoming==5,
           "fresh empty enemy publication cannot reach retained stale array tails");
}
} // namespace
int main() {
    rosters(); bridge(); witness_cases(); salvage_cases(); salvage_self_failure_cases(); checkpoint_cases();
    contingency_cases(); public_cases(); relay_cases(); immediate_exposure_cases();
    std::cout << "Plan6 organizations two: " << checks << " checks passed\n";
}
