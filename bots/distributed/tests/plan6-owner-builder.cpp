#include "../owner-builder/src/policy.h"
#include <cstdlib>
#include <iostream>
#include <string_view>

using namespace p6;
namespace ob = p6::owner_builder;
namespace {
int checks = 0;
void expect(bool value, std::string_view name) {
    ++checks;
    if (!value) { std::cerr << "FAIL Owner Builder: " << name << '\n'; std::exit(1); }
}
Exposure safe() { return {100, 100, 0, 0, 0, 0, true}; }
CycleCertificate certificate(const Frame& f, int actor, int group, bool solo = true) {
    return {f.clock, actor, group, safe(), safe(), safe(), safe(), true, true, true, solo};
}
Actor actor(int id, int pick, int group) {
    Actor a;
    a.team = Team::ally;
    a.id = id; a.pick_ordinal = pick; a.type = ob::roster[pick];
    a.health = a.max_health = 100; a.strength = 10;
    a.alive = a.can_move = a.can_attack = true;
    a.action = WitnessAction::useful_attack; a.action_group = group;
    return a;
}
ob::Context context(bool with_helper = true) {
    ob::Context c;
    c.frame.clock = {10, 10, 1};
    c.frame.own.actor = actor(10, 0, 0); c.frame.own.gold = 7;
    if (with_helper) {
        c.frame.actors[0] = {actor(20, 1, 0), c.frame.clock, 20, Knowledge::exact};
        c.frame.actor_count = 1;
    }
    c.frame.group = {Group{0, {}, {}, {}, Lifecycle::living, true, true, true},
                     c.frame.clock, 0, Knowledge::exact};
    c.settings = {200, 1000, 5};
    ScoreHistory history;
    expect(history.sample({10, 10}, 0), "fixture private score sample");
    c.windows = history.windows(c.settings, {10, 10}, 0);
    c.affected[0] = {10, certificate(c.frame, 10, 3)};
    c.affected_count = 1;
    if (with_helper) {
        c.affected[1] = {20, certificate(c.frame, 20, 4)};
        c.affected_count = 2;
    }
    return c;
}
// Synthetic full cycles. A zero-output actor still has a named productive
// renewal; nonzero payouts get exactly one whole-bag tribute before renewal.
Schedule schedule(const ob::Context& c, int target, Gems owner_output, Gems helper_output = 0) {
    Schedule s;
    s.target_id = target; s.actor_count = c.affected_count;
    for (int i = 0; i < c.affected_count; ++i) {
        const int id = c.affected[i].actor;
        const auto* observed = c.frame.visible(id);
        auto& a = s.actors[i]; a.actor_id = id;
        a.initial_bag = {observed->bag, id == c.frame.clock.self ?
            Source::self_observed : Source::visible_subject};
        a.initial_gold = id == c.frame.clock.self ? Fact<Gems>{c.frame.own.gold, Source::self_observed} :
                                                   Fact<Gems>{0, Source::conservative};
        a.occupied_turns = {6, Source::scheduled};
        const Gems output = id == 10 ? owner_output : helper_output;
        if (output > 0) {
            const int p = s.production_count++;
            s.productions[p] = {100 + id, 2, {output, Source::synthetic_fixture},
                {0, Source::synthetic_fixture}, {}, 1, Source::synthetic_fixture};
            s.productions[p].holders[0] = id;
            a.events[a.event_count++] = {EventKind::production, 100 + id, 2, p,
                {}, -1, Source::scheduled};
        }
        if (output + observed->bag > 0)
            a.events[a.event_count++] = {EventKind::tribute, 200 + id, 4, -1,
                {}, -1, Source::scheduled};
        a.events[a.event_count++] = {EventKind::renewal, 300 + id, 6, -1,
            {}, 400 + id, Source::scheduled};
    }
    return s;
}
ob::Option project(const ob::Context& c, int group, Gems output, int helper = -1, bool solo = true) {
    ob::Option o;
    o.clock = c.frame.clock; o.kind = ob::Kind::project;
    o.target = group; o.owner = 10; o.helper = helper;
    o.owner_cycle = certificate(c.frame, 10, group, solo);
    if (helper >= 0) o.helper_cycle = certificate(c.frame, helper, group, false);
    o.self_exposure = safe(); o.executable = true;
    o.schedule = schedule(c, group, output, helper >= 0 ? 0 : 40);
    o.independent = schedule(c, group, 20, 40);
    return o;
}
void fixed_roster_and_disagreement() {
    auto c = context();
    expect(ob::roster[0] == Champion::marcella && ob::roster[2] == Champion::vendetta,
        "selected owner pool includes personal conversion and kill-reset production");
    expect(ob::owner_pick(0) == 0 && ob::owner_pick(4) == 1 && ob::owner_pick(5) == 2,
        "owner comes from public group and pick ordinal");
    std::array<ob::Option, 2> options{project(c, 0, 70, 20, false), project(c, 3, 20)};
    const auto active = ob::select(c, options, ob::Arm::active);
    const auto control = ob::select(c, options, ob::Arm::control);
    expect(active.option == 1 && active.target == 3 && active.forecast.min_margin == 60,
        "active chooses independently certified project and counts helper independent40");
    expect(active.rejected[0] == ob::Rejection::requires_helper,
        "active rejects helper-dependent owner certificate");
    expect(control.option == 0 && control.forecast.min_margin == 70,
        "control admits same safe high project with observed required helper");
    c.frame.actors[0].value.action = WitnessAction::none;
    c.frame.actors[0].value.ready_attack_group = 0;
    expect(ob::select(c, options, ob::Arm::control).option == 0,
        "fresh helper with current legal useful basic attack permits joint startup");
    expect(ob::select(c, options, ob::Arm::active).option == 1,
        "ready helper cannot replace active owner's solo certificate");
    c.frame.actors[0].knowledge = Knowledge::stale;
    expect(ob::select(c, options, ob::Arm::control).action == ob::Action::yield,
        "stale ready helper cannot license startup");
    c.frame.actors[0].knowledge = Knowledge::exact;
    c.frame.group.knowledge = Knowledge::stale;
    expect(ob::select(c, options, ob::Arm::control).rejected[0] == ob::Rejection::helper_not_useful,
        "ready attack requires current living group observation");
    c.frame.group.knowledge = Knowledge::exact;
    c.frame.actors[0].value.can_attack = false;
    expect(ob::select(c, options, ob::Arm::control).rejected[0] == ob::Rejection::helper_not_useful,
        "a spent basic attack cannot supply current helper readiness");
    c.frame.actors[0].value = actor(20, 1, 0);
    options[0].owner_cycle.clear.incoming_total = 90;
    expect(ob::select(c, options, ob::Arm::control).option == 1,
        "control preserves safety instead of manufacturing fragility");
    options[0] = project(c, 0, 70, 20, false);
    options[0].owner = 20;
    options[0].helper = 10;
    options[0].owner_cycle = certificate(c.frame, 20, 0, false);
    options[0].helper_cycle = certificate(c.frame, 10, 0, false);
    expect(ob::select(c, options, ob::Arm::control).rejected[0] == ob::Rejection::wrong_owner,
        "visible richer actor never changes fixed owner");
}
void helper_loss_and_counterfactual() {
    auto c = context();
    std::array<ob::Option, 2> options{project(c, 0, 50, 20), project(c, 3, 20)};
    auto result = ob::select(c, options, ob::Arm::active);
    expect(result.rejected[0] == ob::Rejection::no_team_gain && result.option == 1,
        "independent owner20 plus helper40 beats joint50");
    options[0] = project(c, 0, 60, 20);
    expect(ob::select(c, options, ob::Arm::active).rejected[0] == ob::Rejection::no_team_gain,
        "equal score has no one-gem cooperation gain");
    options[0] = project(c, 0, 61, 20);
    expect(ob::select(c, options, ob::Arm::active).option == 0,
        "one actual team gem admits optional help");
    c.frame.actors[0].value.action = WitnessAction::none;
    expect(ob::select(c, options, ob::Arm::active).rejected[0] == ob::Rejection::helper_not_useful,
        "visibility without a current useful action or legal attack target is insufficient");
    c.frame.actors[0].value.channel = Channel::recall;
    expect(ob::select(c, options, ob::Arm::control).rejected[0] == ob::Rejection::helper_missing,
        "observed recalling helper contributes nothing");
    c = context(false);
    options = {project(c, 0, 70, 20, false), project(c, 3, 20)};
    const auto active = ob::select(c, options, ob::Arm::active);
    const auto control = ob::select(c, options, ob::Arm::control);
    expect(active.option == 1 && active.target == 3 && active.forecast.min_margin == 20,
        "active owner's same project survives helper disappearance");
    expect(control.option == 1 && control.rejected[0] == ob::Rejection::helper_missing,
        "control loses required-helper admission and chooses actual solo fallback");
    options[0] = project(c, 0, 30);
    expect(ob::select(c, options, ob::Arm::active).option == 0,
        "lost optional helper changes speed without invalidating solo owner");
    options[0].owner_cycle.bank.incoming_total = 90;
    expect(ob::select(c, options, ob::Arm::active).option == 1,
        "full solo certificate includes executable safe banking");
}
void freshness_and_matched_resources() {
    auto c = context();
    std::array<ob::Option, 1> options{project(c, 0, 70, 20)};
    c.windows.sampled_actor_id = 20;
    expect(ob::select(c, options, ob::Arm::active).action == ob::Action::yield,
        "another actor's same-round private score history cannot rank options");
    c.windows.sampled_actor_id = c.frame.clock.self;
    options[0].clock.revision = 0;
    expect(ob::select(c, options, ob::Arm::active).rejected[0] == ob::Rejection::stale,
        "earlier same-round view is stale");
    options[0] = project(c, 0, 70, 20);
    options[0].schedule.actors[1].initial_bag.value = 40;
    expect(ob::select(c, options, ob::Arm::active).rejected[0] == ob::Rejection::resources,
        "cached visible ally bag is not current resources");
    options[0] = project(c, 0, 70, 20);
    options[0].schedule.actors[1].initial_gold = {20, Source::visible_subject};
    expect(ob::select(c, options, ob::Arm::active).rejected[0] == ob::Rejection::resources,
        "other actor's gold cannot fund an option");
    options[0] = project(c, 0, 70, 20);
    options[0].independent.actor_count = 1;
    expect(ob::select(c, options, ob::Arm::active).rejected[0] == ob::Rejection::resources,
        "independent alternative retains same helper actor");
    options[0] = project(c, 0, 70, 20);
    c.frame.actors[0].knowledge = Knowledge::stale;
    expect(ob::select(c, options, ob::Arm::active).action == ob::Action::yield,
        "hidden actor cannot remain in represented affected set");
    c = context(); options[0] = project(c, 0, 70, 20);
    c.frame.own.actor.channel = Channel::tribute; c.frame.own.actor.channel_valid = true;
    expect(ob::select(c, options, ob::Arm::active).action == ob::Action::preserve_channel,
        "valid channel precedes optional organization");
}
void helper_release() {
    auto c = context();
    const auto owner = c.frame.own.actor;
    c.frame.own.actor = c.frame.actors[0].value;
    c.frame.own.gold = 9; c.frame.clock.self = 20;
    ScoreHistory helper_history;
    expect(helper_history.sample({10, 20}, 0), "helper has its own private score sample");
    c.windows = helper_history.windows(c.settings, {10, 20}, 0);
    c.frame.actors[0] = {owner, c.frame.clock, 10, Knowledge::exact};
    c.frame.actors[0].value.spawn_group = 0;
    c.frame.actors[0].value.action = WitnessAction::occupied_spawn;
    c.frame.own.actor.action = WitnessAction::occupied_spawn;
    c.frame.own.actor.spawn_group = 0;
    c.frame.group.clock = c.frame.clock;
    c.frame.group.value.lifecycle = Lifecycle::reward_available;
    c.affected[0].cycle = certificate(c.frame, 10, 0);
    c.affected[1].cycle = certificate(c.frame, 20, 4);
    ob::Option release = project(c, 0, 20, 20);
    release.kind = ob::Kind::release;
    release.schedule = schedule(c, 0, 20, 40);
    std::array<ob::Option, 1> options{release};
    expect(ob::select(c, options, ob::Arm::active).action == ob::Action::release,
        "helper releases at freshly witnessed safe holder milestone");
    options[0].owner_cycle.hold.incoming_total = 90;
    expect(ob::select(c, options, ob::Arm::active).action == ob::Action::yield,
        "unsafe remaining holder cannot license release");
    options[0] = release;
    c.frame.actors[0].value.spawn_group = -1;
    expect(ob::select(c, options, ob::Arm::active).rejected[0] == ob::Rejection::unlicensed_release,
        "predicted holder arrival is insufficient");
    // Safe support is a separately represented split; no withdrawal is inferred.
    options[0].kind = ob::Kind::project;
    options[0].schedule = schedule(c, 0, 71, 1);
    options[0].independent = schedule(c, 0, 20, 40);
    expect(ob::select(c, options, ob::Arm::active).action == ob::Action::support_split,
        "unwitnessed holder means support and split while safe");
    options[0] = release; c.frame.actors[0].value.spawn_group = 0;
    c.frame.group.value.uncontested_now = false;
    expect(ob::select(c, options, ob::Arm::active).rejected[0] == ob::Rejection::unlicensed_release,
        "contested spawn cannot license release");
}
} // namespace
int main() {
    fixed_roster_and_disagreement(); helper_loss_and_counterfactual();
    freshness_and_matched_resources(); helper_release();
    std::cout << "Owner Builder offline: " << checks << " checks passed; no native claim\n";
}
