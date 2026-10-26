#include "../firebrigade/src/policy.h"
#include <cstdlib>
#include <iostream>
#include <string_view>

using namespace p6;
namespace fb = p6::firebrigade;
namespace {
int checks = 0;
void expect(bool value, std::string_view name) {
    ++checks;
    if (!value) { std::cerr << "FAIL Firebrigade: " << name << '\n'; std::exit(1); }
}
Exposure safe() { return {100, 100, 10, 1, 0, 0, true}; }
Actor actor(int id, int pick) {
    Actor a;
    a.id = id; a.pick_ordinal = pick; a.type = fb::roster[pick]; a.team = Team::ally;
    a.health = a.max_health = 100; a.strength = 10;
    a.alive = a.can_move = a.can_attack = true;
    return a;
}
CycleCertificate certificate(const Frame& f, int who, int group, bool solo = true) {
    return {f.clock, who, group, safe(), safe(), safe(), safe(), true, true, true, solo};
}
void reobserve(fb::Context& c, int round) {
    c.frame.clock.round = round; ++c.frame.clock.revision;
    for (int i = 0; i < c.frame.actor_count; ++i)
        if (c.frame.actors[i].knowledge == Knowledge::exact) c.frame.actors[i].clock = c.frame.clock;
    c.frame.group.clock = c.safety_clock = c.frame.clock;
    for (int i = 0; i < c.affected_count; ++i)
        c.affected[i].solo = certificate(c.frame, c.affected[i].actor, c.affected[i].solo.group);
    ScoreHistory history;
    if (!history.sample({round, c.frame.clock.self}, 0)) std::exit(2);
    c.windows = history.windows(c.settings, {round, c.frame.clock.self}, 0);
    c.alarm = {}; c.milestone = {}; c.failure = {};
}
fb::Context context(int round = 10) {
    fb::Context c;
    c.frame.clock = {round, 30, 0};
    c.frame.own.actor = actor(30, 2); c.frame.own.actor.position = {2, 0}; c.frame.own.gold = 7;
    auto owner = actor(10, 0); owner.spawn_group = 0;
    owner.action = WitnessAction::occupied_spawn;
    c.frame.actors[0] = {owner, c.frame.clock, 10, Knowledge::exact};
    auto threat = actor(90, 2); threat.team = Team::enemy; threat.position = {1, 0}; threat.attack_range = 1;
    c.frame.actors[1] = {threat, c.frame.clock, 90, Knowledge::exact}; c.frame.actor_count = 2;
    Group group{0, {1, 0}, {0, 0}, {-1, 0}, Lifecycle::reward_available, true, true, true};
    c.frame.group = {group, c.frame.clock, 0, Knowledge::exact};
    c.settings = {200, 1000, 5};
    c.recovery_waypoint = 600;
    c.self_exposure = safe();
    c.affected[0] = {30, 103, certificate(c.frame, 30, 3)};
    c.affected[1] = {10, 104, certificate(c.frame, 10, 4)}; c.affected_count = 2;
    reobserve(c, round);
    return c;
}
void alarm(fb::Context& c, fb::IncidentKind kind = fb::IncidentKind::exposed_hold) {
    c.alarm = {{kind, 10, 0, 90, kind == fb::IncidentKind::interrupted_approach ?
        Failure::observed_unavailable : Failure::unsafe}, c.frame.clock, 10, Knowledge::exact};
}
// Hand oracle: self10 + owner20 => 30 independent; self0 + owner50 => 50
// joint. All production is banked at tick4 and renews at a named pad at tick6.
Schedule schedule(const fb::Context& c, int target, Gems self_output, Gems owner_output,
                  int occupied = 6) {
    Schedule s;
    s.target_id = target; s.actor_count = c.affected_count;
    for (int i = 0; i < c.affected_count; ++i) {
        const int who = c.affected[i].actor;
        const auto* visible = c.frame.visible(who);
        if (!visible) std::exit(3);
        auto& a = s.actors[i]; a.actor_id = who;
        a.initial_bag = {visible->bag, who == c.frame.clock.self ? Source::self_observed : Source::visible_subject};
        a.initial_gold = who == c.frame.clock.self ? Fact<Gems>{c.frame.own.gold, Source::self_observed} :
                                                   Fact<Gems>{0, Source::conservative};
        a.occupied_turns = {occupied, Source::scheduled};
        const Gems output = who == c.frame.clock.self ? self_output : owner_output;
        if (output > 0) {
            const int p = s.production_count++;
            s.productions[p] = {100 + who, 2, {output, Source::synthetic_fixture},
                {0, Source::synthetic_fixture}, {}, 1, Source::synthetic_fixture};
            s.productions[p].holders[0] = who;
            a.events[a.event_count++] = {EventKind::production, 100 + who, 2, p, {}, -1, Source::scheduled};
        }
        if (output + visible->bag > 0)
            a.events[a.event_count++] = {EventKind::tribute, 200 + who, 4, -1, {}, -1, Source::scheduled};
        a.events[a.event_count++] = {EventKind::renewal, 300 + who, 6, -1, {},
                                    c.affected[i].renewal_waypoint, Source::scheduled};
    }
    return s;
}
fb::Option production(const fb::Context& c, fb::Kind kind = fb::Kind::local_circuit,
                      Gems output = 10) {
    fb::Option o;
    o.clock = c.frame.clock; o.kind = kind; o.target = kind == fb::Kind::committed_circuit ? 6 : 3;
    o.executable = true; o.schedule = schedule(c, o.target, output, 20);
    for (int i = 0; i < c.affected_count; ++i) o.cycles[i] = c.affected[i].solo;
    o.cycles[0] = certificate(c.frame, c.frame.clock.self, o.target);
    return o;
}
fb::Option intervention(const fb::Context& c, Gems saved_output = 50, Gems displaced_output = 10) {
    fb::Option o;
    o.clock = c.frame.clock; o.kind = fb::Kind::intervention; o.target = 0; o.owner = 10;
    o.executable = true; o.terminal = fb::Terminal::safe_payout;
    o.terminal_event_id = 110;
    o.schedule = schedule(c, 0, 0, saved_output);
    for (int i = 0; i < o.schedule.production_count; ++i) o.schedule.productions[i].tick = 1;
    for (int i = 0; i < o.schedule.production_count; ++i) o.production_groups[i] = 0;
    for (int i = 0; i < o.schedule.actor_count; ++i)
        for (int j = 0; j < o.schedule.actors[i].event_count; ++j)
            if (o.schedule.actors[i].events[j].kind == EventKind::production)
                o.schedule.actors[i].events[j].tick = 1;
    o.independent = schedule(c, 3, displaced_output, 20);
    for (int i = 0; i < c.affected_count; ++i)
        o.cycles[i] = certificate(c.frame, c.affected[i].actor, 0, false);
    return o;
}
void recovery_terminal(fb::Option& o) {
    o.terminal = fb::Terminal::recovery_waypoint; o.terminal_waypoint = 700;
    o.terminal_position = {-1, 0};
    o.terminal_event_id = 900;
    auto& a = o.schedule.actors[actor_index(o.schedule, o.owner)];
    a.events[a.event_count] = a.events[a.event_count - 1];
    a.events[a.event_count - 1] = {EventKind::recovery, 900, 3, -1, {}, 700, Source::scheduled};
    ++a.event_count;
}
void bank_terminal(fb::Option& o) {
    o.terminal = fb::Terminal::protected_bank; o.terminal_event_id = 200 + o.owner;
}
fb::Decision choose(const fb::Context& c, std::span<const fb::Option> options, fb::Arm arm = fb::Arm::active) {
    fb::State state;
    return fb::step(state, c, options, arm);
}
void economic_and_work_oracles() {
    auto c = context(); alarm(c);
    std::array<fb::Option, 3> menu{production(c), production(c, fb::Kind::committed_circuit, 15), intervention(c)};
    auto active = choose(c, menu);
    auto control = choose(c, menu, fb::Arm::control);
    expect(active.action == fb::Action::intervene && active.work == fb::Work::stabilize_hold &&
        active.target == 0 && active.forecast.min_margin == 50 && active.forecast.occupancy == 12,
        "positive: costed joint50 beats owner20 plus displaced reserve10, twelve occupied actor-turns");
    expect(control.action == fb::Action::produce && control.work == fb::Work::committed_production &&
        control.target == 6 && control.forecast.min_margin == 35,
        "control physically works another longer circuit rather than rescue");
    menu[2] = intervention(c, 50, 40);
    auto expensive = choose(c, menu);
    expect(expensive.action == fb::Action::produce && expensive.work == fb::Work::local_production &&
        expensive.rejected[2] == fb::Rejection::no_team_gain, "same-actor independent60 defeats joint50");
    menu[2] = intervention(c); menu[0] = production(c, fb::Kind::local_circuit, 60);
    expect(choose(c, menu).option == 0, "best offered independent80 defeats underpriced rescue baseline30");
    c.alarm = {};
    menu[0] = production(c); menu[2] = intervention(c);
    auto quiet = choose(c, menu);
    expect(quiet.action == fb::Action::produce && quiet.work == fb::Work::local_production &&
        quiet.forecast.min_margin == 30, "no alarm completes productive income and bank schedule");
    expect(quiet.forecast.denial == std::array<Gems, 3>{0, 0, 0}, "no unpriced saved-cycle or denial credit");
    menu[0].kind = fb::Kind::bank;
    expect(choose(c, menu).action == fb::Action::bank, "quiet reserve can bank instead of standing idle");
}
void personal_evidence_oracles() {
    auto c = context(); alarm(c);
    std::array<fb::Option, 2> menu{production(c), intervention(c)};
    c.alarm.clock.revision--;
    expect(choose(c, menu).rejected[1] == fb::Rejection::alarm_unwitnessed, "stale alarm refused");
    alarm(c); c.frame.actors[1].knowledge = Knowledge::stale;
    expect(choose(c, menu).rejected[1] == fb::Rejection::alarm_unwitnessed, "unseen hostile distress refused");
    c.frame.actors[1].knowledge = Knowledge::exact;
    c.frame.actors[1].value.position = {4, 0}; c.frame.actors[1].value.attack_range = 4;
    expect(choose(c, menu).action == fb::Action::intervene, "queried synthetic range4 reaches distance4");
    c.frame.actors[1].value.attack_range = -1;
    expect(choose(c, menu).action == fb::Action::produce, "unknown hostile range cannot license alarm");
    c.frame.actors[1].value.position = {1, 0}; c.frame.actors[1].value.attack_range = 1;
    menu[1].schedule.actors[1].initial_gold = {99, Source::visible_subject};
    expect(choose(c, menu).rejected[1] == fb::Rejection::resources, "visible ally gold is still private");
    menu[1] = intervention(c); menu[1].schedule.actors[1].initial_bag.value = 50;
    expect(choose(c, menu).rejected[1] == fb::Rejection::resources, "invented saved bag refused");
    menu[1] = intervention(c); menu[1].cycles[1].hold.incoming_total = 95;
    expect(choose(c, menu).rejected[1] == fb::Rejection::unsafe, "every actor bears individual worst exposure");
    menu[1] = intervention(c); menu[1].cycles[0].clock.revision--;
    expect(choose(c, menu).rejected[1] == fb::Rejection::unsafe, "fresh self certificate required");
    c.frame.actors[0].value.team = Team::enemy;
    expect(choose(c, menu).action != fb::Action::intervene, "enemy matching roster never becomes beneficiary");
    c = context(); alarm(c, fb::IncidentKind::interrupted_approach);
    c.frame.actors[0].value.spawn_group = -1; c.frame.actors[0].value.action = WitnessAction::none;
    menu = {production(c), intervention(c)};
    auto blocked = choose(c, menu);
    expect(blocked.action == fb::Action::intervene && blocked.work == fb::Work::clear_approach,
        "visible hostile occupying adjacent named approach licenses different work");
    c.frame.actors[1].value.position = {8, 8};
    expect(choose(c, menu).action == fb::Action::produce, "remote failure tag without physical obstruction refused");
    c = context(); c.frame.actors[0].value.bag = 12; alarm(c, fb::IncidentKind::dangerous_return);
    menu = {production(c), intervention(c)};
    expect(choose(c, menu).work == fb::Work::escort_return, "visible bag and nearby hostile license escort");
    c.frame.actors[0].knowledge = Knowledge::stale;
    expect(choose(c, menu).fallback == fb::Fallback::inspect_primary_source, "missing required current actor has named fallback");
}
void precedence_and_bound_oracles() {
    auto c = context(62); alarm(c);
    std::array<fb::Option, 2> menu{production(c), intervention(c)};
    fb::State s;
    auto first = fb::step(s, c, menu, fb::Arm::active);
    expect(first.intervention_turns == 1 && s.execution.project == ProjectID{0, 0}, "admission latches epoch62");
    auto duplicate = fb::step(s, c, menu, fb::Arm::active);
    expect(duplicate.action == fb::Action::yield && s.execution.owner_turn == 1, "same owner turn cannot spend twice");
    for (int turn = 2; turn <= 8; ++turn) {
        reobserve(c, 61 + turn); menu = {production(c), intervention(c)};
        const auto d = fb::step(s, c, menu, fb::Arm::active);
        expect(d.action == fb::Action::intervene && d.intervention_turns == turn &&
            s.execution.project == ProjectID{0, 0}, "active project retains epoch and eight-turn budget");
    }
    reobserve(c, 70); alarm(c); menu = {production(c), intervention(c)};
    auto expired = fb::step(s, c, menu, fb::Arm::active);
    expect(expired.action == fb::Action::produce && expired.ended && expired.reason == Failure::expired &&
        !s.execution.active && s.execution.retired_epoch == 0, "ninth owner turn retires before optional alarm");
    c = context(); alarm(c); menu = {production(c), intervention(c)};
    c.frame.own.actor.channel = Channel::tribute; c.frame.own.actor.channel_valid = true;
    expect(choose(c, menu).action == fb::Action::preserve_channel, "valid channel precedes optional alarm");
    c.frame.own.actor.channel = Channel::none; c.frame.own.actor.spawn_group = 0;
    c.active_capture = certificate(c.frame, 30, 0);
    expect(choose(c, menu).action == fb::Action::preserve_capture, "safe active own capture precedes alarm");
    c.active_capture.solo = false;
    expect(choose(c, menu).action == fb::Action::intervene, "uncertified own hold does not manufacture valid capture");
    c = context(62); alarm(c); menu = {production(c), intervention(c)}; s = {};
    fb::step(s, c, menu, fb::Arm::active);
    for (int round = 63; round <= 70; ++round) {
        reobserve(c, round); c.frame.own.actor.channel = Channel::recall; c.frame.own.actor.channel_valid = true;
        menu = {production(c), intervention(c)};
        expect(fb::step(s, c, menu, fb::Arm::active).action == fb::Action::preserve_channel,
            "valid recall survives appointment and epoch expiry");
    }
    expect(!s.execution.active && s.execution.retired_epoch == 0, "channel preservation cannot extend rescue budget");
}
void repair_and_terminal_oracles() {
    auto c = context(63); alarm(c);
    std::array<fb::Option, 2> menu{production(c), intervention(c)};
    fb::State s; fb::step(s, c, menu, fb::Arm::active);
    reobserve(c, 64); alarm(c);
    c.failure = {Failure::observed_failed_action, c.frame.clock, 30, Knowledge::exact};
    menu = {production(c), intervention(c)}; menu[1].repair = true;
    auto repaired = fb::step(s, c, menu, fb::Arm::active);
    expect(repaired.action == fb::Action::intervene && s.execution.repairs == 1 &&
        s.execution.project.epoch == 0 && repaired.intervention_turns == 2, "one repair crossing64 keeps original budget");
    reobserve(c, 65); alarm(c);
    c.failure = {Failure::observed_failed_action, c.frame.clock, 30, Knowledge::exact};
    menu = {production(c), intervention(c)}; menu[1].repair = true;
    auto refused = fb::step(s, c, menu, fb::Arm::active);
    expect(refused.action == fb::Action::produce && refused.rejected[1] == fb::Rejection::repair_unlicensed &&
        !s.execution.active, "second repair refused and independent work resumes");
    c = context(); alarm(c); menu = {production(c), intervention(c)}; s = {};
    fb::step(s, c, menu, fb::Arm::active);
    reobserve(c, 11); c.frame.group.value.lifecycle = Lifecycle::respawning;
    c.milestone = {{10, 0, fb::Terminal::safe_payout, -1, true}, c.frame.clock, 10, Knowledge::exact};
    c.terminal_cycle = certificate(c.frame, 10, 0);
    menu = {production(c), intervention(c)};
    auto no_payout = fb::step(s, c, menu, fb::Arm::active);
    expect(!no_payout.terminal_witnessed && s.execution.active, "lifecycle transition alone cannot identify collector");
    reobserve(c, 12); c.frame.actors[0].value.bag = 50;
    c.milestone = {{10, 0, fb::Terminal::safe_payout, -1, true}, c.frame.clock, 10, Knowledge::exact};
    c.terminal_cycle = certificate(c.frame, 10, 0); menu = {production(c), intervention(c)};
    auto paid = fb::step(s, c, menu, fb::Arm::active);
    expect(paid.terminal_witnessed && paid.ended && paid.action == fb::Action::produce && !s.execution.active,
        "fresh visible payout plus safe independent continuation releases reserve");
    c = context(); c.frame.actors[0].value.bag = 20; alarm(c, fb::IncidentKind::dangerous_return);
    menu = {production(c), intervention(c)}; bank_terminal(menu[1]); s = {};
    fb::step(s, c, menu, fb::Arm::active);
    reobserve(c, 11); c.frame.actors[0].value.channel = Channel::tribute;
    c.frame.actors[0].value.channel_valid = true;
    menu = {production(c), intervention(c)}; bank_terminal(menu[1]);
    fb::step(s, c, menu, fb::Arm::active);
    reobserve(c, 12); c.frame.actors[0].value.channel = Channel::none; c.frame.actors[0].value.bag = 0;
    c.frame.own_team_score = 20;
    c.milestone = {{10, 0, fb::Terminal::protected_bank, -1, true}, c.frame.clock, 10, Knowledge::exact};
    c.terminal_cycle = certificate(c.frame, 10, 0); menu = {production(c), intervention(c)};
    auto banked = fb::step(s, c, menu, fb::Arm::active);
    expect(banked.terminal_witnessed && banked.action == fb::Action::produce,
        "personally witnessed valid tribute then empty bag and score bank releases reserve");
    c = context(); alarm(c); menu = {production(c), intervention(c)};
    recovery_terminal(menu[1]); s = {};
    fb::step(s, c, menu, fb::Arm::active);
    reobserve(c, 11); c.frame.actors[0].value.position = {-1, 0};
    c.milestone = {{10, 0, fb::Terminal::recovery_waypoint, 700, true}, c.frame.clock, 10, Knowledge::exact};
    c.terminal_cycle = certificate(c.frame, 10, 0); menu = {production(c), intervention(c)};
    expect(fb::step(s, c, menu, fb::Arm::active).terminal_witnessed,
        "fresh actual arrival at admitted safe recovery waypoint releases reserve");
}
void deadline_and_missing_menu_oracles() {
    auto c = context(); alarm(c);
    std::array<fb::Option, 2> menu{production(c), intervention(c)};
    auto& owner = menu[1].schedule.actors[1];
    owner.events[0].tick = 20; owner.events[1].tick = 22; owner.events[2].tick = 24;
    menu[1].schedule.productions[0].tick = 20;
    expect(choose(c, menu).rejected[1] == fb::Rejection::late_terminal,
        "valuable tick20 payout cannot fit eight-turn intervention");
    // A second, unrelated camp pays early. Its income is real in this schedule
    // but cannot stand in for the specifically promised target0 milestone.
    menu[1].schedule.production_count = 2;
    menu[1].schedule.productions[1] = menu[1].schedule.productions[0];
    menu[1].schedule.productions[1].event_id = 999; menu[1].schedule.productions[1].tick = 2;
    menu[1].schedule.productions[1].gems.value = 5; menu[1].production_groups[1] = 3;
    owner.events[3] = owner.events[2];
    owner.events[2] = {EventKind::production, 999, 2, 1, {}, -1, Source::scheduled};
    owner.event_count = 4;
    expect(evaluate_schedule(menu[1].schedule, c.windows, c.settings, {10, 30}, 0).min_margin == 55,
        "counterexample really has early5 plus late50 represented score");
    expect(choose(c, menu).rejected[1] == fb::Rejection::late_terminal,
        "unrelated early production does not replace designated target payout20");
    bank_terminal(menu[1]);
    owner.events[4] = owner.events[3];
    owner.events[3] = {EventKind::tribute, 998, 3, -1, {}, -1, Source::scheduled}; owner.event_count = 5;
    expect(choose(c, menu).rejected[1] == fb::Rejection::late_terminal,
        "unrelated early tribute does not replace designated protected bank22");
    fb::State s; menu[1] = intervention(c);
    fb::step(s, c, menu, fb::Arm::active);
    for (int turn = 2; turn <= 8; ++turn) {
        reobserve(c, 9 + turn); menu = {production(c), intervention(c)};
        fb::step(s, c, menu, fb::Arm::active);
    }
    reobserve(c, 18); c.frame.group.value.lifecycle = Lifecycle::respawning;
    c.frame.actors[0].value.bag = 50;
    c.milestone = {{10, 0, fb::Terminal::safe_payout, -1, true}, c.frame.clock, 10, Knowledge::exact};
    c.terminal_cycle = certificate(c.frame, 10, 0); menu = {production(c), intervention(c)};
    const auto boundary = fb::step(s, c, menu, fb::Arm::active);
    expect(boundary.terminal_witnessed && boundary.ended && boundary.reason == Failure::none &&
        boundary.action == fb::Action::produce && !s.execution.active,
        "fresh endpoint after eighth action is accepted before deadline fallback");
    c = context(); s = {};
    for (int i = 0; i < 4; ++i) {
        reobserve(c, 10 + i);
        const auto missing = fb::step(s, c, {}, fb::Arm::active);
        expect(i < 2 ? (missing.action == fb::Action::inspect && missing.target == 3) :
            (missing.action == fb::Action::recover && missing.target == 600),
            "empty menu gets two named source inspections then named recovery");
    }
    c = context(); s = {};
    for (int i = 0; i < 3; ++i) {
        reobserve(c, 10 + i); auto unusable = production(c); unusable.executable = false;
        const auto missing = fb::step(s, c, std::span<const fb::Option>(&unusable, 1), fb::Arm::active);
        expect(i < 2 ? missing.action == fb::Action::inspect :
            (missing.action == fb::Action::recover && missing.target == 600),
            "all rejected options also have bounded productive recovery fallback");
    }
    c = context(); c.settings = {};
    expect(choose(c, {}).action == fb::Action::yield, "invalid settings do not invent inspection license");
    for (int count : {7, std::numeric_limits<int>::max()}) {
        c = context(); c.affected_count = count;
        for (auto& a : c.affected) a.actor = 999;
        const auto malformed = choose(c, {});
        expect(malformed.action == fb::Action::recover && malformed.target == 600,
            "oversized affected set rejects before array access and takes named recovery");
    }
}
void complete_circuit_roles() {
    expect(fb::roster[2] == Champion::ren && fb::responder(actor(20,2)),
        "selected mobile responder is bound to actual Ren role");
    for (int pick : {0, 1, 3, 4}) {
        auto c = context(); c.picks = 5;
        c.frame.own.actor = actor(100 + pick, pick); c.frame.clock.self = 100 + pick;
        c.frame.actor_count = 0;
        c.affected[0] = {100 + pick, 103, certificate(c.frame, 100 + pick, 3)};
        c.affected_count = 1; reobserve(c, 10);
        const auto cycle = production(c, fb::Kind::circuit);
        const auto active = choose(c, std::span<const fb::Option>(&cycle, 1));
        const auto control = choose(c, std::span<const fb::Option>(&cycle, 1), fb::Arm::control);
        expect(active.action == fb::Action::produce && control.action == fb::Action::produce &&
            active.work == fb::Work::independent_circuit && active.forecast.min_margin == 10,
            "Marcella Tavi and extensions retain independently certified complete bank-and-renewal cycles");
    }
}
} // namespace
int main() {
    economic_and_work_oracles(); personal_evidence_oracles(); precedence_and_bound_oracles();
    repair_and_terminal_oracles(); deadline_and_missing_menu_oracles(); complete_circuit_roles();
    std::cout << "{\"suite\":\"plan6-firebrigade\",\"checks\":" << checks << ",\"passed\":true}\n";
    return 0;
}
