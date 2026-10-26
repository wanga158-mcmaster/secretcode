#include "../owner-builder/src/organization.h"
#include "../replicas/src/organization.h"
#include "../firebrigade/src/organization.h"
#include <cstdlib>
#include <deque>
#include <iostream>
#include <string_view>

using namespace p6;
namespace org = p6::organization;
namespace ob = p6::owner_builder;
namespace rp = p6::replicas;
namespace fb = p6::firebrigade;
namespace {
int checks = 0;
void expect(bool condition, std::string_view message) {
    ++checks;
    if (!condition) { std::cerr << "FAIL existing adapters: " << message << '\n'; std::exit(1); }
}
Actor actor(const std::array<Champion, 5>& roster, int pick) {
    Actor a; a.id = 100 + pick; a.pick_ordinal = pick; a.type = roster[pick];
    a.team = Team::ally; a.alive = a.can_move = a.can_attack = true;
    a.health = a.max_health = 100; a.strength = 10; a.attack_range = 1;
    a.position = {5 + pick, 5}; return a;
}
CycleCertificate certificate(const Frame& f, const Actor& a, int group) {
    const Exposure safe{a.health, a.max_health, 0, 0, 0, 0, true};
    return {f.clock, a.id, group, safe, safe, safe, safe, true, true, true, true};
}
void refresh(org::Input& in, int round) {
    auto& f = in.frame; f.clock.round = round; ++f.clock.revision;
    for (int i = 0; i < f.actor_count; ++i) f.actors[i].clock = f.clock;
    for (int s = 0; s < in.site_count; ++s) {
        auto& site = in.sites[s]; site.actor_count = 0;
        auto add = [&](const Actor& a) {
            if (a.team != Team::ally) return;
            const int index = site.actor_count++;
            site.actors[index] = a.id; site.cycles[index] = certificate(f, a, site.group.id);
        };
        add(f.own.actor);
        for (int i = 0; i < f.actor_count; ++i) add(f.actors[i].value);
    }
    f.group = {in.sites[0].group, f.clock, in.sites[0].group.id, Knowledge::exact};
}
std::deque<org::Input> fixtures;
org::Input& fixture(const std::array<Champion, 5>& roster, int pick, int round = 10) {
    fixtures.emplace_back(); auto& in = fixtures.back(); auto& f = in.frame;
    in.ordinal = pick; in.picks = 3; in.safe_now = true;
    in.independent_group = 10; in.proposed_group = 0; in.site_count = 3;
    f.own.actor = actor(roster, pick); f.own.actor.position = {0, 0};
    f.clock = {round, f.own.actor.id, 0};
    for (int p = 0; p < 2; ++p) if (p != pick) {
        const auto a = actor(roster, p);
        f.actors[f.actor_count++] = {a, f.clock, a.id, Knowledge::exact};
    }
    in.sites[0].group = {0, {4, 4}, {5, 5}, {4, 5}, Lifecycle::living, true, true, true};
    in.sites[1].group = {10, {20, 20}, {21, 21}, {20, 21}, Lifecycle::living, true, true, true};
    in.sites[2].group = {2, {1, 1}, {2, 2}, {1, 2}, Lifecycle::living, true, true, true};
    for (int s = 0; s < in.site_count; ++s) in.sites[s].known = true;
    in.sites[0].joint_better = true; in.sites[0].holder = 100;
    refresh(in, round); return in;
}
Actor& observed(org::Input& in, int pick) {
    for (int i = 0; i < in.frame.actor_count; ++i)
        if (in.frame.actors[i].value.pick_ordinal == pick) return in.frame.actors[i].value;
    std::abort();
}
void set_solo(org::Site& site, int actor_id, bool value) {
    for (int i = 0; i < site.actor_count; ++i)
        if (site.actors[i] == actor_id) site.cycles[i].solo = value;
}
void add_enemy(org::Input& in) {
    Actor enemy; enemy.id = 900; enemy.team = Team::enemy; enemy.alive = true;
    enemy.health = enemy.max_health = 100; enemy.strength = 10; enemy.attack_range = 1;
    enemy.position = {5, 6};
    in.frame.actors[in.frame.actor_count++] = {enemy, in.frame.clock, enemy.id, Knowledge::exact};
    refresh(in, in.frame.clock.round);
}
bool intervention(const org::Decision& d) {
    return d.intent == org::Intent::escort || d.intent == org::Intent::intercept;
}
void test_owner_builder() {
    auto& in = fixture(ob::roster, 0);
    observed(in, 1).ready_attack_group = 0; refresh(in, 10);
    set_solo(in.sites[0], in.frame.clock.self, false);
    ob::ExecutablePolicy active, control;
    expect(active.choose(in, false).intent == org::Intent::independent,
        "active owner rejects a project requiring future helper contribution");
    expect(control.choose(in, true).intent == org::Intent::work,
        "control can propose a safe joint cycle with a currently useful helper");
    observed(in, 1).ready_attack_group = -1; refresh(in, 11);
    set_solo(in.sites[0], in.frame.clock.self, false);
    const auto missing_helper = control.choose(in, true);
    expect(missing_helper.intent == org::Intent::work && missing_helper.group == 10,
        "control loses helper contribution and works its named safe alternate");
    refresh(in, 12); in.frame.actor_count = 0;
    expect(active.choose(in, false).group == 0,
        "solo-certified active owner remains productive after helper loss");

    auto& runtime = fixture(ob::roster,0);
    runtime.independent_group = 0; // The runtime's public owner circuit aliases the project.
    ob::ExecutablePolicy runtime_active, runtime_control;
    const auto solo_active = runtime_active.choose(runtime,false);
    const auto solo_control = runtime_control.choose(runtime,true);
    expect(solo_active.intent == org::Intent::work && solo_active.group == 0 &&
        solo_control.intent == org::Intent::work && solo_control.group == 2,
        "ordinary solo certificates keep active owner at work and control on a distinct safe source");
    observed(runtime,1).ready_attack_group = 0; refresh(runtime,11);
    expect(runtime_control.choose(runtime,true).group == 0,
        "current useful helper and matching gain admit control's individually safe owner");
    runtime.sites[0].joint_better = false; refresh(runtime,12);
    expect(runtime_control.choose(runtime,true).group == 2,
        "helper presence without actual team gain does not admit coupled control");
    runtime.sites[0].joint_better = true; runtime.sites[0].holder = 101; refresh(runtime,13);
    expect(runtime_control.choose(runtime,true).group == 2,
        "joint schedule for another holder cannot admit fixed-owner control");
    runtime.sites[0].holder = 100; runtime.frame.actor_count = 0; refresh(runtime,14);
    expect(runtime_active.choose(runtime,false).group == 0 && runtime_control.choose(runtime,true).group == 2,
        "actual helper loss leaves active productive and returns control to its real alternate");
    set_solo(runtime.sites[2],runtime.frame.clock.self,false);
    expect(runtime_control.choose(runtime,true).group == 10,
        "control skips an alternate lacking a current solo completion certificate");

    auto& helper = fixture(ob::roster, 1);
    // Remove the helper's own fixed project so this case actually tests support.
    helper.sites[1].known = false;
    observed(helper, 0).ready_attack_group = 0; refresh(helper, 10);
    ob::ExecutablePolicy support;
    expect(support.choose(helper, false).group == 0, "optional helper joins only current useful owner");
    helper.sites[0].joint_better = false; refresh(helper, 11);
    expect(support.choose(helper, false).intent == org::Intent::work,
        "admitted helper does not call an elapsed appointment a release proof");
    helper.sites[0].group.lifecycle = Lifecycle::reward_available;
    observed(helper, 0).spawn_group = 0; refresh(helper, 12);
    auto released = support.choose(helper, false);
    expect(released.intent == org::Intent::independent && support.state.helper_group == -1,
        "helper releases after personally checking an uncontested solo-safe holder");
}
void test_replicas() {
    auto& in = fixture(rp::roster, 1);
    rp::ExecutablePolicy active, control;
    const auto approach = active.choose(in, false);
    expect(approach.intent == org::Intent::approach && approach.explicit_waypoint &&
        approach.waypoint == in.sites[0].group.retreat,
        "active replica gets its separate startup approach pad");
    expect(control.choose(in, true).intent == org::Intent::independent,
        "control sends the second worker directly to its independent source");
    observed(in, 0).ready_attack_group = 0; refresh(in, 11);
    expect(active.choose(in, false).intent == org::Intent::approach && !active.state.released,
        "ready-to-attack is not a personally witnessed productive action");
    observed(in, 0).action = WitnessAction::useful_attack;
    observed(in, 0).action_group = 0; refresh(in, 12);
    set_solo(in.sites[0], 100, false);
    expect(active.choose(in, false).intent != org::Intent::independent && !active.state.released,
        "useful attack cannot release replica without owner's solo completion bound");
    refresh(in, 13);
    expect(active.choose(in, false).intent == org::Intent::independent && active.state.released,
        "actual useful attack plus solo completion releases to certified second source");

    auto& alias = fixture(rp::roster,1);
    alias.independent_group = 0;
    rp::ExecutablePolicy aliased_active, aliased_control;
    expect(aliased_control.choose(alias,true).group == 10,
        "control's static second source overrides a global best aliased to startup");
    aliased_active.choose(alias,false);
    observed(alias,0).action = WitnessAction::useful_attack;
    observed(alias,0).action_group = 0; refresh(alias,11);
    const auto alias_release = aliased_active.choose(alias,false);
    expect(alias_release.group == 10 && aliased_active.state.released &&
        aliased_active.choose(alias,false).group == 10,
        "witnessed release and subsequent work use named second source despite aliased input");
    alias.sites[1].known = false;
    const auto inspect = aliased_control.choose(alias,true);
    expect(inspect.intent == org::Intent::approach && inspect.group == 10 &&
        inspect.explicit_waypoint && inspect.waypoint == alias.sites[1].group.approach,
        "unknown named source requires personal approach inspection");
    auto& extension = fixture(rp::roster,2); extension.independent_group = 0;
    rp::ExecutablePolicy extra;
    expect(extra.choose(extension,false).group == 2,
        "third worker's static site remains distinct from startup and second source");
    auto& unavailable = fixture(rp::roster,1); unavailable.independent_group = 0;
    unavailable.sites[1].blocked = true;
    rp::ExecutablePolicy no_pair;
    expect(no_pair.choose(unavailable,false).group == 2 && no_pair.state.closed,
        "unavailable distinct assignment retires redundancy and keeps a current productive fallback");
    auto& holding = fixture(rp::roster,1);
    holding.current_group = 0; holding.phase = Phase::hold;
    holding.sites[0].group.lifecycle = Lifecycle::reward_available;
    holding.frame.own.actor.spawn_group = 0; refresh(holding,10);
    rp::ExecutablePolicy safe_holder;
    expect(safe_holder.choose(holding,false).group == 0,
        "actual safe capture precedes static independent reassignment");
    holding.frame.own.actor.channel = Channel::tribute;
    holding.frame.own.actor.channel_valid = true; refresh(holding,11);
    expect(safe_holder.choose(holding,true).group == 0,
        "valid current channel survives control's static source assignment");

    auto& absent = fixture(rp::roster, 1);
    absent.frame.actor_count = 0; refresh(absent, 10);
    rp::ExecutablePolicy takeover;
    expect(takeover.choose(absent, false).intent == org::Intent::work,
        "absent owner permits takeover from current personal safe source view");
    auto& unsafe = fixture(rp::roster, 1);
    unsafe.frame.actor_count = 0; refresh(unsafe, 10);
    set_solo(unsafe.sites[0], unsafe.frame.clock.self, false);
    rp::ExecutablePolicy abandon;
    expect(abandon.choose(unsafe, false).intent == org::Intent::independent,
        "absent owner without current solo certificate chooses fallback");

    auto& retry = fixture(rp::roster, 0, 10);
    rp::ExecutablePolicy recurrent;
    recurrent.choose(retry, false); retry.observed_failure = true; refresh(retry, 11);
    recurrent.choose(retry, false);
    expect(recurrent.state.closed && recurrent.state.repeat_epoch == 1,
        "actual failure licenses exactly the next epoch");
    retry.observed_failure = false; refresh(retry, 64);
    expect(recurrent.choose(retry, false).intent == org::Intent::approach && recurrent.state.repeat_used,
        "failure-licensed redundancy recurs once at next epoch");
    retry.observed_failure = true; refresh(retry, 65); recurrent.choose(retry, false);
    retry.observed_failure = false; refresh(retry, 128);
    expect(recurrent.choose(retry, false).intent == org::Intent::independent,
        "second failure cannot authorize another recurrence");
    auto& clock_only = fixture(rp::roster, 0, 63);
    rp::ExecutablePolicy no_failure; no_failure.choose(clock_only, false);
    no_failure.state.closed = true; refresh(clock_only, 64);
    expect(no_failure.choose(clock_only, false).intent == org::Intent::independent,
        "epoch expiry alone never renews duplication");
}
void test_firebrigade() {
    auto& in = fixture(fb::roster, 2);
    fb::ExecutablePolicy active, control;
    expect(active.choose(in, false).group == 2 && control.choose(in, true).group == 10,
        "active Ren produces locally while control keeps its committed circuit");
    expect(fb::responder(in.frame.own.actor), "reserve follows diversified profile's actual Ren ordinal");
    observed(in, 0).bag = 25; add_enemy(in);
    in.sites[0].joint_better = false;
    expect(!intervention(active.choose(in, false)),
        "visible alarm does not override displaced independent output");
    in.sites[0].joint_better = true;
    const auto rescue = active.choose(in, false);
    expect(rescue.intent == org::Intent::escort && rescue.participant == 100,
        "personally threatened loaded owner and positive same-actor comparison admit escort");
    expect(!intervention(control.choose(in, true)), "control retains committed production on same alarm");
    in.actual_payout = true; in.actual_bank = true; refresh(in, 11);
    expect(intervention(active.choose(in, false)), "responder's unrelated completion flags do not finish rescue");
    in.actual_payout = in.actual_bank = false;
    for (int round = 12; round <= 17; ++round) {
        refresh(in, round);
        expect(intervention(active.choose(in, false)), "intervention remains bounded through its eighth owner turn");
    }
    refresh(in, 18);
    expect(!intervention(active.choose(in, false)), "ninth owner turn performs independent production");
    refresh(in, 19);
    expect(!intervention(active.choose(in, false)), "expired incident cannot immediately reopen in same epoch");

    auto& bank = fixture(fb::roster, 2);
    observed(bank, 0).bag = 25; observed(bank, 0).channel = Channel::tribute;
    observed(bank, 0).channel_valid = true; add_enemy(bank);
    fb::ExecutablePolicy banking; expect(intervention(banking.choose(bank, false)), "current dangerous bank is observable");
    observed(bank, 0).bag = 0; observed(bank, 0).channel = Channel::none;
    bank.frame.own_team_score = 25;
    bank.frame.actors[bank.frame.actor_count - 1].value.position = {30, 30}; refresh(bank, 11);
    expect(!intervention(banking.choose(bank, false)) && banking.state.group == -1,
        "observed protected owner bank releases reserve immediately");

    auto& payout = fixture(fb::roster, 2);
    payout.sites[0].group.lifecycle = Lifecycle::reward_available;
    observed(payout, 0).spawn_group = 0;
    observed(payout, 0).action = WitnessAction::occupied_spawn; add_enemy(payout);
    fb::ExecutablePolicy capturing;
    expect(capturing.choose(payout, false).intent == org::Intent::intercept,
        "personally witnessed exposed holder admits stabilization");
    payout.sites[0].group.lifecycle = Lifecycle::respawning;
    observed(payout, 0).bag = 50;
    payout.frame.actors[payout.frame.actor_count - 1].value.position = {30, 30}; refresh(payout, 11);
    expect(!intervention(capturing.choose(payout, false)) && capturing.state.group == -1,
        "consecutive visible source transition and owner receipt finish safe payout rescue");

    auto& recovering = fixture(fb::roster, 2);
    observed(recovering, 0).bag = 25; add_enemy(recovering);
    fb::ExecutablePolicy recovery; recovery.choose(recovering, false);
    observed(recovering, 0).position = recovering.sites[0].group.retreat;
    recovering.frame.actors[recovering.frame.actor_count - 1].value.position = {30, 30}; refresh(recovering, 11);
    expect(!intervention(recovery.choose(recovering, false)) && recovery.state.group == -1,
        "owner's actual safe recovery waypoint ends intervention");

    auto& blocked = fixture(fb::roster, 2);
    observed(blocked, 0).position = {4, 3}; add_enemy(blocked);
    blocked.frame.actors[blocked.frame.actor_count - 1].value.position = blocked.sites[0].group.approach;
    blocked.frame.actors[blocked.frame.actor_count - 1].value.attack_range = 0; refresh(blocked, 10);
    fb::ExecutablePolicy clearing;
    expect(clearing.choose(blocked, false).intent == org::Intent::intercept,
        "hostile physically occupying adjacent owner's approach is an actual incident");

    auto& hold = fixture(fb::roster, 2);
    hold.phase = Phase::hold; hold.current_group = 2;
    hold.sites[2].group.lifecycle = Lifecycle::reward_available;
    hold.frame.own.actor.spawn_group = 2;
    observed(hold, 0).bag = 25; add_enemy(hold);
    fb::ExecutablePolicy holder;
    expect(holder.choose(hold, false).group == 2 && holder.state.group == -1,
        "actual safe capture precedes optional witnessed alarm");
    auto& hidden = fixture(fb::roster, 2);
    observed(hidden, 0).bag = 25; add_enemy(hidden);
    hidden.frame.actors[hidden.frame.actor_count - 1].knowledge = Knowledge::stale;
    fb::ExecutablePolicy private_view;
    expect(!intervention(private_view.choose(hidden, false)), "stale hostile record supplies no distress witness");
}
} // namespace
int main() {
    test_owner_builder(); test_replicas(); test_firebrigade();
    std::cout << "Plan6 existing organization adapters: " << checks << " checks passed\n";
}
