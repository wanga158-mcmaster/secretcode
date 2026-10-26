#define P6_REPLICAS_INSPECTION_REPAIR 1
#define P6_REPLICAS_PARTIAL_INSPECTION_REPAIR 1
#define P6_REPLICAS_STARTUP_ALIAS_REPAIR 1
#define P6_REPLICAS_STARTUP_DISPATCH_REPAIR 1
#include "../replicas/src/policy.h"
#include "../replicas/src/organization.h"
#include <cstdlib>
#include <deque>
#include <iostream>
#include <string_view>

using namespace p6;
namespace rp = p6::replicas;
namespace {
int checks = 0;
void expect(bool condition, std::string_view message) {
    ++checks;
    if (!condition) { std::cerr << "FAIL Replicas: " << message << '\n'; std::exit(1); }
}
Actor actor(int pick) {
    Actor a; a.id = 10 + pick; a.pick_ordinal = pick; a.type = rp::roster[pick];
    a.team = Team::ally; a.health = a.max_health = 100; a.strength = 10;
    a.alive = a.can_move = a.can_attack = true;
    return a;
}
CycleCertificate cycle(const Frame& f, int id, int group) {
    const Exposure safe{100, 100, 0, 0, 0, 0, true};
    return {f.clock, id, group, safe, safe, safe, safe, true, true, true, true};
}
rp::Projection projection(const rp::Context& c, bool startup, Gems first_output = 50,
                          Gems second_output = 10) {
    rp::Projection p; auto& s = p.schedule;
    s.target_id = startup ? c.draft.startup_group :
        c.draft.independent_groups[c.input.frame.own.actor.pick_ordinal];
    s.actor_count = c.continuation_count;
    for (int i = 0; i < s.actor_count; ++i) {
        const auto& f = c.input.frame;
        const int id = c.continuations[i].actor;
        const auto* a = f.visible(id);
        expect(a != nullptr, "fixture projection includes only currently visible actors");
        auto& scheduled = s.actors[i]; scheduled.actor_id = id;
        scheduled.initial_bag = {a->bag, id == f.clock.self ? Source::self_observed : Source::visible_subject};
        scheduled.initial_gold = {id == f.clock.self ? f.own.gold : 0,
            id == f.clock.self ? Source::self_observed : Source::conservative};
        scheduled.occupied_turns = {startup ? 12 : 8, Source::scheduled};
        const bool solo_startup = startup && s.actor_count == 1;
        const Gems output = solo_startup || a->pick_ordinal == 0 ? first_output : second_output;
        if (output > 0) {
            const int pi = s.production_count++;
            auto& reward = s.productions[pi];
            reward = {100 + id, 2, {output, Source::synthetic_fixture},
                {0, Source::synthetic_fixture}, {}, 1, Source::synthetic_fixture};
            reward.holders[0] = id;
            p.groups[pi] = startup && (solo_startup || a->pick_ordinal == 0) ? c.draft.startup_group :
                c.draft.independent_groups[a->pick_ordinal];
            scheduled.events[scheduled.event_count++] = {EventKind::production, 100 + id,
                2, pi, {}, -1, Source::scheduled};
        }
        if (output > 0 || a->bag > 0)
            scheduled.events[scheduled.event_count++] = {EventKind::tribute, 200 + id,
                8, -1, {}, -1, Source::scheduled};
        scheduled.events[scheduled.event_count++] = {EventKind::renewal, 300 + id,
            12, -1, {}, c.continuations[i].cycle.group, Source::scheduled};
    }
    return p;
}
// Context owns two bounded Schedule values. Retain fixtures in stable heap
// storage so compiler inlining cannot place every scenario in main's WASI
// default 64 KiB stack frame. Context is still passed by const reference.
std::deque<rp::Context> fixtures;
rp::Context& context(int pick = 1, bool visible_owner = true, int round = 10) {
    fixtures.emplace_back(); auto& c = fixtures.back(); auto& f = c.input.frame;
    c.settings = {300, 1000, 5};
    c.draft.startup_group = 0; c.draft.independent_groups = {10, 11, 12, 13, 14};
    c.draft.pads[0] = {9, true}; c.draft.pads[1] = {3, true};
    c.draft.pads[2] = {3, true}; c.draft.pads[3] = {5, false}; c.draft.pad_count = 4;
    f.clock = {round, 10 + pick, 1}; f.own.actor = actor(pick); f.own.gold = 7;
    f.group = {Group{0, {}, {}, {}, Lifecycle::living, true, true, true}, f.clock, 0, Knowledge::exact};
    if (pick != 0 && visible_owner) {
        f.actors[0] = {actor(0), f.clock, 10, Knowledge::exact}; f.actor_count = 1;
    }
    c.input.safe_now = true; c.input.fresh_target_attack_legal = true;
    c.input.forecast_rounds = 30; c.input.remaining_rounds = 300 - round;
    c.at_startup_pad = true;
    c.startup_cycle = cycle(f, f.clock.self, 0);
    c.independent_cycle = cycle(f, f.clock.self, c.draft.independent_groups[pick]);
    if (visible_owner && pick != 0) c.owner_cycle = cycle(f, 10, 0);
    c.continuations[0] = {f.clock.self, c.independent_cycle}; c.continuation_count = 1;
    if (visible_owner && pick != 0) {
        c.continuations[1] = {10, cycle(f, 10, 10)}; c.continuation_count = 2;
    }
    c.startup = projection(c, true, 50, 10);
    c.independent = projection(c, false, 20, 10);
    return c;
}
void refresh(rp::Context& c, int round) {
    auto& f = c.input.frame; f.clock.round = round;
    f.group.clock = f.clock;
    for (int i = 0; i < f.actor_count; ++i) f.actors[i].clock = f.clock;
    c.startup_cycle = cycle(f, f.clock.self, 0);
    c.independent_cycle = cycle(f, f.clock.self, c.draft.independent_groups[f.own.actor.pick_ordinal]);
    c.owner_cycle = {};
    c.continuations[0] = {f.clock.self, c.independent_cycle};
    c.continuation_count = 1;
    // The affected set is a fresh personal observation, not retained membership.
    // Hidden actors supply neither resources nor an independent continuation.
    const auto* owner = rp::primary(c);
    if (owner && owner->id != f.clock.self) {
        c.owner_cycle = cycle(f, owner->id, c.draft.startup_group);
        c.continuations[c.continuation_count++] = {
            owner->id, cycle(f, owner->id, c.draft.independent_groups[owner->pick_ordinal])};
    }
    c.input.remaining_rounds = c.settings.max_rounds - round;
    c.startup = projection(c, true, 50, 10);
    c.independent = projection(c, false, 20, 10);
}

void assignments_and_economics() {
    auto& c = context(); const auto pads = rp::startup_pads(c.draft);
    expect(pads[0] == 3 && pads[1] == 9, "duplicate tiles deduplicate; unsafe tile skipped");
    rp::State active, control;
    const auto a = rp::step(active, c); const auto b = rp::step(control, c, rp::Arm::control);
    expect(a.next.action == Action::attack && a.next.group == 0 && a.mode == rp::Mode::redundant,
        "positive: Ren replica does useful redundant source work");
    expect(b.next.group == 11 && b.next.action == Action::inspect,
        "control replica independently inspects its own distinct source");
    expect(a.approach_tile == 9 && a.forecast.cooperative.own_score[1] == 60 &&
        a.forecast.independent.own_score[1] == 30, "hand oracle full-cycle team60 versus30");
    auto& owner = context(0, false); rp::State owner_state;
    auto own_decision = rp::step(owner_state, owner);
    expect(own_decision.next.group == 0 && own_decision.approach_tile == 3,
        "Varg uses same source but a different pad");
    for (int pick = 2; pick < 5; ++pick) {
        auto& extra = context(pick, false); extra.picks = 5; rp::State state;
        expect(rp::step(state, extra).next.group == 10 + pick, "Marcella and extensions independent");
    }
    auto& reliable = context(); reliable.startup = projection(reliable, true, 20, 10);
    rp::State negative;
    auto n = rp::step(negative, reliable);
    expect(n.rejection == rp::Rejection::no_gain && n.next.group == 11,
        "negative: equal delivered30 plus extra labor rejects redundancy");
    auto& unavailable = context(); unavailable.draft.pad_count = 1; rp::State us;
    auto u = rp::step(us, unavailable);
    expect(u.rejection == rp::Rejection::pads && u.next.group == 11,
        "no separate safe pad marks organization unavailable");
    auto& unrelated_draft = context(); unrelated_draft.draft.independent_groups[2] = -1;
    rp::State uds; auto ud = rp::step(uds, unrelated_draft);
    expect(ud.next.group == 11 && ud.next.action == Action::inspect,
        "unavailable unrelated assignment retains own productive independent circuit");
    auto& winning = context(); winning.input.frame.own.actor.bag = 10;
    winning.input.frame.own_team_score = 990;
    winning.input.at_bank = winning.input.altar_legal = true;
    rp::State ws; expect(rp::step(ws, winning).next.action == Action::tribute,
        "legal immediate own winning bank outranks duplicate startup travel");
    auto& collision = context(); collision.blocked_pad = {9, collision.input.frame.clock, 11, Knowledge::exact};
    rp::State cs; auto cd = rp::step(cs, collision);
    expect(cd.next.group == 11 && cs.worker.project.group != 0 && cs.repeat_epoch == 1,
        "observed pad collision uses productive fallback and explicit retry credit");
}
void witnessed_release_and_disagreement() {
    auto& c = context(); rp::State state;
    auto ready = rp::step(state, c);
    expect(!ready.release_witness && ready.next.group == 0, "readiness does not release helper");
    refresh(c, 11); c.input.frame.actors[0].value.action = WitnessAction::useful_attack;
    c.input.frame.actors[0].value.action_group = 0;
    auto release = rp::step(state, c);
    expect(release.release_witness && release.mode == rp::Mode::released && release.next.group == 11,
        "personally witnessed useful attack and fresh solo survival license actual split");
    auto& stale = context(); stale.input.frame.actors[0].value.action = WitnessAction::useful_attack;
    stale.input.frame.actors[0].value.action_group = 0;
    stale.owner_cycle.clock.round = 9; rp::State ss;
    expect(!rp::step(ss, stale).release_witness, "stale owner safety cannot release");
    auto& hold = context(); hold.input.frame.group.value.lifecycle = Lifecycle::reward_available;
    hold.input.frame.actors[0].value.spawn_group = 0; rp::State hs;
    expect(rp::step(hs, hold).release_witness, "fresh visible occupied spawn is a legal useful hold witness");
    hold.input.frame.group.value.uncontested_now = false; rp::State contested;
    expect(!rp::step(contested, hold).release_witness, "contested hold cannot release");
    auto& enemy = context(); enemy.input.frame.actors[0].value.team = Team::enemy;
    enemy.input.frame.actors[0].value.action = WitnessAction::useful_attack;
    enemy.input.frame.actors[0].value.action_group = 0; rp::State es;
    expect(!rp::step(es, enemy).release_witness, "enemy matching roster is never an owner");
    auto& view_a = context(); auto& view_b = context();
    view_a.input.frame.actors[0].value.spawn_group = 0;
    view_a.input.frame.group.value.lifecycle = Lifecycle::reward_available;
    view_b.input.frame.actors[0].knowledge = Knowledge::stale;
    rp::State as, bs;
    expect(rp::step(as, view_a).release_witness && !rp::step(bs, view_b).release_witness,
        "same round distinct personal views license different actions");
    auto& no_heal = context(); no_heal.input.frame.actors[0].value.spawn_group = 0;
    no_heal.input.frame.group.value.lifecycle = Lifecycle::reward_available;
    no_heal.owner_cycle.hold.assigned_timed_heal = 10; rp::State nh;
    expect(!rp::step(nh, no_heal).release_witness, "owner solo safety cannot depend on departing external heal");
}
void hidden_owner_and_accounting() {
    auto& c = context(); rp::State state; rp::step(state, c);
    c.input.frame.actor_count = 0; refresh(c, 11);
    expect(c.continuation_count == 1 && c.startup.schedule.actor_count == 1 &&
        c.startup.groups[0] == 0 && c.startup.schedule.productions[0].gems.value == 50,
        "fresh owner loss regenerates one actor's actual50-gem startup takeover cycle");
    auto take = rp::step(state, c);
    expect(take.mode == rp::Mode::takeover && take.next.group == 0 && state.contact == Failure::not_observed,
        "hidden owner is unknown; current visible feasible source permits takeover");
    refresh(c, 12); c.input.frame.group.knowledge = Knowledge::stale;
    auto gone = rp::step(state, c);
    expect(gone.next.group == 11 && state.startup_closed && state.repeat_epoch == -1,
        "no local source view means fallback without remote ownership or suspicion retry");
    auto& resource = context(); resource.startup.schedule.actors[1].initial_gold = {500, Source::visible_subject};
    rp::State rs; expect(rp::step(rs, resource).rejection == rp::Rejection::resources,
        "foreign gold cannot fund represented purchases");
    auto& bag = context(); bag.startup.schedule.actors[1].initial_bag.value = 500;
    rp::State bs; expect(rp::step(bs, bag).rejection == rp::Rejection::resources, "fresh visible bag must match");
    auto& duplicate = context(); duplicate.startup.groups[1] = duplicate.startup.groups[0];
    rp::State ds; expect(rp::step(ds, duplicate).rejection == rp::Rejection::resources,
        "two separately owned payouts from same source are rejected");
    auto& event = context(); event.startup.schedule.productions[1].event_id = event.startup.schedule.productions[0].event_id;
    rp::State event_state; expect(rp::step(event_state, event).rejection == rp::Rejection::accounting,
        "common evaluator deduplicates payout identity");
    auto& missing = context(); missing.startup.schedule.actor_count = 1;
    missing.startup.schedule.actors[0] = missing.startup.schedule.actors[1];
    rp::State ms; expect(rp::step(ms, missing).rejection == rp::Rejection::resources,
        "affected owner cannot vanish from same-actor independent alternatives");
    auto& unrenewed = context(); auto& actor0 = unrenewed.startup.schedule.actors[0];
    --actor0.event_count; rp::State urs;
    expect(rp::step(urs, unrenewed).rejection == rp::Rejection::resources,
        "complete schedule requires named productive renewal");
}
void bounded_wait_and_retry() {
    auto& c = context(); c.input.fresh_target_attack_legal = false; rp::State state;
    for (int round = 10; round < 18; ++round) {
        refresh(c, round); auto d = rp::step(state, c);
        expect(d.next.group == 0 && !d.release_witness, "at most eight owner-turn inspections");
    }
    refresh(c, 18); auto out = rp::step(state, c);
    expect(out.next.group == 11 && state.repeat_epoch == -1, "eighth wait retires to productive own source");
    refresh(c, 64); expect(rp::step(state, c).next.group == 11 && !state.repeat_used,
        "elapsed epoch alone never repeats redundancy");
    auto& failed = context(); failed.failure = {Failure::observed_failed_action, failed.input.frame.clock, 10, Knowledge::exact};
    failed.failure_group = 0;
    failed.failed_action_result = {Outcome::hidden_collision, failed.input.frame.clock, 10, Knowledge::exact};
    failed.input.frame.group.value.lifecycle = Lifecycle::respawning;
    rp::State fs; rp::step(fs, failed);
    expect(fs.repeat_epoch == 1 && fs.startup_closed, "only observed failure enables next-epoch repeat");
    refresh(failed, 63); failed.input.frame.group.value.lifecycle = Lifecycle::living;
    auto early = rp::step(fs, failed);
    expect(!early.repeat_admitted && early.next.group == 11, "repeat cannot run in failed epoch");
    refresh(failed, 64); auto repeat = rp::step(fs, failed);
    expect(repeat.repeat_admitted && repeat.next.group == 0 && fs.repeat_used,
        "one observed-failure repeat enters next epoch");
    refresh(failed, 65); failed.input.frame.group.value.lifecycle = Lifecycle::respawning;
    failed.failure = {Failure::observed_failed_action, failed.input.frame.clock, 10, Knowledge::exact};
    rp::step(fs, failed); refresh(failed, 128);
    expect(!rp::step(fs, failed).repeat_admitted && fs.repeat_used, "repeat failure does not replenish retry");
    auto& unrelated = context(); unrelated.failure = {Failure::observed_failed_action, unrelated.input.frame.clock, 10, Knowledge::exact};
    unrelated.failure_group = 99;
    unrelated.failed_action_result = {Outcome::hidden_collision, unrelated.input.frame.clock, 10, Knowledge::exact};
    rp::State unrelated_state; rp::step(unrelated_state, unrelated);
    expect(unrelated_state.repeat_epoch == -1, "failure at unrelated group99 cannot repeat startupgroup0");
    auto& healthy = context(); healthy.failure = {Failure::observed_unavailable, healthy.input.frame.clock, 10, Knowledge::exact};
    healthy.failure_group = 0; rp::State healthy_state; rp::step(healthy_state, healthy);
    expect(healthy_state.repeat_epoch == -1, "healthy nonchanneling actor cannot be reported unavailable");
    auto& unconfirmed = context(); unconfirmed.failure = {Failure::observed_failed_action, unconfirmed.input.frame.clock, 10, Knowledge::exact};
    unconfirmed.failure_group = 0; rp::State unconfirmed_state; rp::step(unconfirmed_state, unconfirmed);
    expect(unconfirmed_state.repeat_epoch == -1, "bare fresh failure enum lacks a confirmed action-result receipt");
}
void complete_cycle_and_private_windows() {
    auto& c = context(0, false, 63); rp::State s;
    auto first = rp::step(s, c); expect(first.next.action == Action::attack, "ordinary self source production starts");
    const ProjectID id = s.worker.project;
    refresh(c, 64); c.input.frame.group.value.lifecycle = Lifecycle::reward_available;
    c.input.frame.own.actor.spawn_group = 0;
    expect(rp::step(s, c).next.action == Action::hold && s.worker.project == id,
        "capture across epoch retains admission identity");
    refresh(c, 65); c.input.frame.own.actor.bag = 10;
    c.input.completion = {c.input.frame.clock, 0, true, false, true};
    auto payout = rp::step(s, c);
    expect(payout.next.phase == Phase::bank && s.worker.payout_seen && s.worker.project == id,
        "actual own payout leaves capture for complete bank cycle");
    refresh(c, 66); c.input.at_bank = c.input.altar_legal = true;
    auto bank = rp::step(s, c); expect(bank.next.action == Action::tribute, "bank starts from own observed bag");
    expect(confirm_tribute_started(s.worker, c.input.frame,
        {Action::tribute, c.input.frame.clock, 10, Knowledge::exact}), "successful actual tribute receipt recorded");
    refresh(c, 67); c.input.frame.own.actor.channel = Channel::tribute;
    c.input.frame.own.actor.channel_valid = true; c.draft.pad_count = 0;
    expect(rp::step(s, c).next.action == Action::preserve_channel && s.worker.project == id,
        "valid channel survives unavailable pads and appointment/epoch changes");
    refresh(c, 68); c.input.frame.own.actor.channel = Channel::none;
    c.input.frame.own.actor.bag = 0; c.input.frame.own_team_score = 10;
    c.input.completion = {c.input.frame.clock, 0, false, true, true};
    auto completed = rp::step(s, c);
    expect(s.worker.bank_seen && completed.next.action == Action::move && s.worker.project == id,
        "actual bank completion retains project through return to production");
    refresh(c, 69); c.input.at_renewal = true;
    auto renewed = rp::step(s, c);
    expect(s.startup_closed && renewed.next.group == 10, "banked startup renews own independent source");
    auto& a = context(); auto& b = context(); b.input.frame.clock.self = 12; b.input.frame.own.actor = actor(2);
    b.independent_cycle = cycle(b.input.frame, 12, 12);
    rp::State ah, bh; auto ad = rp::step(ah, a); b.enemy_score = 950; auto bd = rp::step(bh, b);
    expect(ad.windows.windows[2] == 120 && bd.windows.windows[2] == 50,
        "interleaved score threshold gives private windows120 versus50");
    expect(rp::live_shared_bytes == 0, "zero live shared bytes");
}
void arrival_requests_personal_observation() {
    organization::Input in;
    in.frame.clock = {992, 11, 0};
    in.frame.own.actor = actor(1);
    in.frame.own.actor.position = {3, 34};
    in.safe_now = true; in.site_count = 2; in.proposed_group = 10;
    in.independent_group = 10;
    in.sites[0].group.id = 10;
    in.sites[1].group = {12, {3, 34}, {4, 35}, {3, 33}};
    auto request = rp::ExecutablePolicy::independent_continuation(in);
    expect(request.intent == organization::Intent::work && request.group == 12 &&
        !request.explicit_waypoint, "arrived unknown source requests ordinary adapter observation");
    expect(!in.sites[1].known && in.sites[1].actor_count == 0,
        "arrival creates no lifecycle, solo certificate or successful action");
    in.frame.own.actor.position = {3, 33};
    auto travel = rp::ExecutablePolicy::independent_continuation(in);
    expect(travel.intent == organization::Intent::approach && travel.explicit_waypoint &&
        travel.waypoint == Point{3, 34}, "transit retains its named safe inspection pad");
    in.frame.own.actor.position = {3, 34}; in.safe_now = false;
    expect(rp::ExecutablePolicy::independent_continuation(in).intent == organization::Intent::recover,
        "unsafe arrival creates no work request");
    in.safe_now = true; in.current_group = 7;
    in.frame.own.actor.channel = Channel::tribute;
    in.frame.own.actor.channel_valid = true;
    auto channel = rp::ExecutablePolicy::independent_continuation(in);
    expect(channel.intent == organization::Intent::independent && channel.group == 7,
        "actual valid tribute preserves its current source before inspection request");
    in.frame.own.actor.channel = Channel::none; in.sites[1].known = true;
    in.current_group = 12; in.phase = Phase::clear;
    expect(rp::ExecutablePolicy::independent_continuation(in).group == 12,
        "admitted independent clear retains executor continuation without a replacement quote");
    in.current_group = in.proposed_group;
    expect(rp::ExecutablePolicy::independent_continuation(in).intent == organization::Intent::recover,
        "startup source is not retained by the independent-project continuation rule");
    in.current_group = -1; in.phase = Phase::bootstrap;
    expect(rp::ExecutablePolicy::independent_continuation(in).intent == organization::Intent::recover,
        "known source still needs a current safe solo certificate");
}
void partial_source_keeps_guarded_inspection() {
    organization::Input in;
    in.frame.clock = {99, 11, 0}; in.frame.own.actor = actor(1);
    in.frame.own.actor.position = {5, 38}; in.frame.own.gold = 10;
    in.safe_now = true; in.site_count = 2; in.proposed_group = 14;
    in.independent_group = 12;
    in.sites[0].group.id = 14;
    auto& source = in.sites[1];
    source.group = {12, {3, 34}, {4, 35}, {3, 33}};
    source.known = true; source.group.lifecycle = Lifecycle::living;
    rp::ExecutablePolicy policy; policy.state.closed = true;
    auto partial = policy.choose(in, false);
    expect(partial.intent == organization::Intent::work && partial.group == 12 &&
        !partial.explicit_waypoint, "actual policy hands live partial source to guarded inspection adapter");
    expect(!source.group.all_spawns_visible && source.actor_count == 0 &&
        !source.solo(in.frame, in.frame.own.actor.id), "inspection grants no hidden membership or solo certificate");
    source.group.all_spawns_visible = true;
    expect(policy.choose(in, false).intent == organization::Intent::work,
        "full source observation retains its deferred personal approach until quote location");
    in.frame.own.actor.position = source.group.approach;
    expect(policy.choose(in, false).intent == organization::Intent::recover,
        "complete source at quote pad with no safe certificate retains ordinary refusal");
    in.frame.own.actor.position = {5, 38}; in.frame.own.actor.ready_attack_group = 12;
    expect(policy.choose(in, false).intent == organization::Intent::recover,
        "current legal target with no safe certificate cannot be relabeled as deferred approach");
    in.frame.own.actor.ready_attack_group = -1;
    source.group.all_spawns_visible = false; source.group.lifecycle = Lifecycle::respawning;
    expect(policy.choose(in, false).intent == organization::Intent::recover,
        "respawn due alone does not authorize live source inspection");
    source.group.lifecycle = Lifecycle::living; in.safe_now = false;
    expect(policy.choose(in, false).intent == organization::Intent::recover,
        "unsafe partial source preserves recovery");
    in.safe_now = true; in.frame.own.actor.bag = 7;
    expect(policy.choose(in, false).intent == organization::Intent::bank,
        "loaded actor keeps bank continuation before fresh partial inspection");
    in.frame.own.actor.bag = 0; in.current_group = 14;
    expect(policy.choose(in, false).intent == organization::Intent::recover,
        "partial independent source cannot replace a different owned startup project");
    in.current_group = 7; in.frame.own.actor.channel = Channel::tribute;
    in.frame.own.actor.channel_valid = true;
    auto channel = policy.choose(in, false);
    expect(channel.intent == organization::Intent::independent && channel.group == 7,
        "actual channel retains its project before partial inspection");
}
void actual_startup_slot_alias() {
    // Actual first-call public assignment and self/visible-owner values from
    // the noninterfering R5 production packet; no current source is assumed.
    organization::Input in;
    in.frame.clock = {5, 6, 0}; in.frame.own.actor = actor(1);
    in.frame.own.actor.id = 6; in.frame.own.actor.position = {24, 39};
    in.frame.own.actor.health = in.frame.own.actor.max_health = 900;
    in.frame.own.actor.strength = 35; in.frame.own.at_spawn = true;
    auto visible_owner = actor(0); visible_owner.id = 5;
    visible_owner.position = {23, 39};
    visible_owner.health = visible_owner.max_health = 1000; visible_owner.strength = 40;
    in.frame.actors[0] = {visible_owner, in.frame.clock, 5, Knowledge::exact};
    in.frame.actor_count = 1; in.picks = 5; in.safe_now = true; in.site_count = 8;
    in.proposed_group = 14; in.independent_group = 12;
    in.sites[0].group = {12, {3, 34}, {4, 35}, {3, 33}};
    in.sites[1].group = {14, {29, 28}, {30, 29}, {30, 28}};
    rp::ExecutablePolicy active;
    const auto assigned = active.choose(in, false);
    expect(!active.state.closed && active.state.started && active.state.group == 14 &&
        assigned.intent == organization::Intent::approach && assigned.group == 14 &&
        assigned.waypoint == Point{30, 28}, "actual slot1/proposed alias retains spare startup pad with distinct fallback12");
    expect(!active.state.released && !in.sites[1].known && !in.sites[1].solo(in.frame, 6),
        "static assignment is no source, release or production certificate");
    rp::ExecutablePolicy control;
    const auto independent = control.choose(in, true);
    expect(independent.group == 12 && independent.reason == std::string_view{"replicas-personal-fallback-inspection"},
        "organization control remains on named independent fallback");
    auto missing = in; missing.independent_group = 14;
    rp::ExecutablePolicy no_fallback; no_fallback.choose(missing, false);
    expect(no_fallback.state.closed, "alias with no distinct named fallback still closes startup");
    auto blocked = in; blocked.sites[1].blocked = true;
    rp::ExecutablePolicy unavailable; unavailable.choose(blocked, false);
    expect(unavailable.state.closed, "blocked ordinal slot preserves old unavailable assignment closure");
    rp::ExecutablePolicy closed; closed.state.closed = true;
    expect(closed.choose(in, false).group == 12 && closed.state.closed,
        "alias exception cannot reopen a previously retired startup project");
    auto channel = in; channel.current_group = 12;
    channel.frame.own.actor.channel = Channel::tribute; channel.frame.own.actor.channel_valid = true;
    rp::ExecutablePolicy converting;
    expect(converting.choose(channel, false).group == 12 && !converting.state.started,
        "actual conversion channel stays ahead of startup assignment");
    auto witness = in; witness.frame.own.actor.position = {30, 28};
    witness.sites[1].known = witness.sites[1].group.all_spawns_visible = true;
    witness.sites[1].group.lifecycle = Lifecycle::living;
    witness.sites[0].known = true;
    auto certificate = [&](int id, int group) {
        const auto* subject = witness.frame.visible(id);
        const Exposure safe{subject->health, subject->max_health, 0, 0, 0, 0, true};
        return CycleCertificate{witness.frame.clock, id, group, safe, safe, safe, safe, true, true, true, true};
    };
    witness.sites[1].actor_count = 1; witness.sites[1].actors[0] = 5;
    witness.sites[1].cycles[0] = certificate(5, 14);
    witness.sites[0].actor_count = 1; witness.sites[0].actors[0] = 6;
    witness.sites[0].cycles[0] = certificate(6, 12);
    rp::ExecutablePolicy unseen_attack;
    unseen_attack.choose(witness, false);
    expect(!unseen_attack.state.released, "ready visible owner without useful action cannot release spare");
    witness.sites[1].actor_count = 2; witness.sites[1].actors[1] = 6;
    witness.sites[1].cycles[1] = certificate(6, 14);
    rp::ExecutablePolicy explicit_worker;
    const auto request = explicit_worker.choose(witness, false);
    expect(request.intent == organization::Intent::work && request.group == 14 &&
        explicit_worker.explicit_startup_work(witness, request),
        "complete personal startup spare work attests explicit program dispatch");
    auto incomplete = witness; incomplete.sites[1].group.all_spawns_visible = false;
    expect(!explicit_worker.explicit_startup_work(incomplete, request),
        "incomplete personal source cannot bypass independent candidate fence");
    auto fallback_request = request; fallback_request.group = 12;
    expect(!explicit_worker.explicit_startup_work(witness, fallback_request),
        "ordinary independent fallback receives no program dispatch exception");
    auto loaded = witness; loaded.frame.own.actor.bag = 1;
    expect(!explicit_worker.explicit_startup_work(loaded, request),
        "loaded actor retains ordinary conversion priority");
    explicit_worker.state.closed = true;
    expect(!explicit_worker.explicit_startup_work(witness, request),
        "closed startup program cannot gain dispatch exception");
    witness.frame.actors[0].value.action = WitnessAction::useful_attack;
    witness.frame.actors[0].value.action_group = 14;
    rp::ExecutablePolicy witnessed_attack;
    expect(witnessed_attack.choose(witness, false).group == 12 && witnessed_attack.state.released,
        "current useful owner action plus own safe fallback retains witnessed release contract");
}
} // namespace
int main() {
    assignments_and_economics(); witnessed_release_and_disagreement();
    hidden_owner_and_accounting(); bounded_wait_and_retry(); complete_cycle_and_private_windows();
    arrival_requests_personal_observation();
    partial_source_keeps_guarded_inspection();
    actual_startup_slot_alias();
    std::cout << "{\"suite\":\"plan6-replicas\",\"checks\":" << checks << ",\"failed\":0,"
              << "\"sizeof_context\":" << sizeof(rp::Context)
              << ",\"sizeof_state\":" << sizeof(rp::State)
              << ",\"sizeof_decision\":" << sizeof(rp::Decision) << "}\n";
}
