#include "../common/src/runtime_joint.h"
#include "../launchpad/src/policy.h"
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {
using namespace p6;
namespace joint = p6::runtime_joint;
int checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value) { std::fprintf(stderr,"FAIL runtime joint: %s\n",message); std::exit(1); }
}
template<class T> Observation<T> seen(T value, ViewClock clock, int subject) {
    return {value,clock,subject,Knowledge::exact};
}
struct Quote { bool available = false; forecast::Request request{}; forecast::Result result{}; };
struct Fixture {
    organization::Input in{};
    joint::Models models{};
    joint::Requests requests{};
    std::array<Quote,8> quotes{};
    double source_health = 100, source_strength = 1;
    double alternate_health = 10, alternate_strength = 1;
    Gems source_gems = 100, alternate_gems = 10;
    bool intact_source = false;
};
Actor ally(int pick, Champion type, Point position, double health = 1000) {
    Actor a; a.id = 100 + pick; a.pick_ordinal = pick; a.type = type; a.position = position;
    a.health = a.max_health = health; a.strength = 10; a.attack_range = 1;
    a.team = Team::ally; a.alive = a.can_move = a.can_attack = true; a.ready_attack_group = 0;
    return a;
}
forecast::Request request(const Fixture& fixture, const Frame& frame, int index) {
    const auto& in = fixture.in; const auto& site = in.sites[index]; const auto& actor = frame.own.actor;
    forecast::Request r; r.shape = forecast::Shape::produce_bank; r.project_group = site.group.id;
    r.timing = {{in.settings,Source::queried_setting},{.1,Source::queried_setting},
        {0,Source::queried_setting},{1,Source::queried_setting}};
    r.kit = seen(Productivity{actor.strength,0,actor.ranged},frame.clock,actor.id);
    r.ambient_incoming = seen(0.,frame.clock,actor.id);
    for (int i = 0; i < frame.actor_count; ++i) if (frame.actors[i].value.team == Team::enemy)
        r.enemy_cast_stress[r.enemy_cast_count++] = seen(0.,frame.clock,frame.actors[i].subject);
    r.bank_waypoint = 9000; r.bank_position = {0,1};
    r.renewal_waypoint = site.group.id; r.renewal_position = site.group.approach;
    r.bank_available = seen(true,frame.clock,r.bank_waypoint);
    r.group_count = 1; auto& group = r.groups[0];
    group.observed = seen(site.group,frame.clock,site.group.id);
    group.membership_complete = {true,Source::public_map}; group.member_count = 1;
    const double hp = index == 0 ? fixture.source_health : fixture.alternate_health;
    const double strength = index == 0 ? fixture.source_strength : fixture.alternate_strength;
    const double full_hp = index == 0 && fixture.intact_source ? hp : 2*hp;
    group.members[0].public_full = {MonsterWork{200+index,std::max(1.,full_hp),strength},Source::public_map};
    group.members[0].current = seen(MonsterWork{200+index,
        site.group.lifecycle == Lifecycle::reward_available ? 0 : hp,strength},frame.clock,200+index);
    group.gems = {index == 0 ? fixture.source_gems : fixture.alternate_gems,Source::public_map};
    group.gold = {0,Source::public_map};
    auto leg = [&](Point from, Point to) {
        r.legs[r.leg_count++] = {from,to,{static_cast<int>(joint::distance(from,to)),Source::public_map}};
    };
    leg(actor.position,site.group.approach); leg(site.group.approach,site.group.spawn);
    leg(site.group.spawn,r.bank_position); leg(r.bank_position,site.group.approach);
    return r;
}
void rebuild(Fixture& fixture) {
    auto& in = fixture.in;
    fixture.models.clear_current(); fixture.requests.clear_current();
    std::array<const Actor*,max_actors> actors{}; int actor_count = 0;
    actors[actor_count++] = &in.frame.own.actor;
    for (int i = 0; i < in.frame.actor_count && actor_count < max_actors; ++i) {
        const auto* actor = in.frame.visible(in.frame.actors[i].subject);
        if (actor && actor->team == Team::ally) actors[actor_count++] = actor;
    }
    for (int i = 0; i < in.site_count; ++i) {
        auto& site = in.sites[i]; site.actor_count = 0; site.joint_better = false; site.holder = -1;
        for (int k = 0; k < actor_count; ++k) {
            const auto& actor = *actors[k]; const auto frame = joint::subject_frame(in,actor);
            auto& req = fixture.requests[i][k]; req = request(fixture,frame,i);
            auto& result = fixture.models[i][k]; result = forecast::build(frame,req);
            if (actor.id != in.frame.clock.self && result.status == Status::feasible) {
                result.certificate.clock = in.frame.clock;
                result.schedule.actors[0].initial_bag.source = Source::visible_subject;
                result.schedule.actors[0].initial_gold = {0,Source::conservative};
            }
            if (result.status == Status::feasible) {
                const int slot = site.actor_count++; site.actors[slot] = actor.id; site.cycles[slot] = result.certificate;
            }
        }
        fixture.quotes[i] = {fixture.models[i][0].status == Status::feasible,
            fixture.requests[i][0],fixture.models[i][0]};
    }
}
std::unique_ptr<Fixture> fixture(bool rescue = false) {
    auto f = std::make_unique<Fixture>(); auto& in = f->in;
    in.ordinal = rescue ? 2 : 1; in.site_count = 2; in.safe_now = true; in.settings = {300,1000,2};
    in.frame.own.actor = ally(in.ordinal,rescue ? Champion::ren : Champion::varg,rescue ? Point{1,0} : Point{0,0});
    in.frame.clock = {10,in.frame.own.actor.id,1};
    in.frame.actors[0] = seen(ally(0,Champion::marcella,{0,0}),in.frame.clock,100);
    in.frame.actor_count = 1;
    in.sites[0].known = in.sites[1].known = true;
    in.sites[0].group = {0,{0,0},{0,0},{0,0},Lifecycle::living,true,true,true};
    in.sites[1].group = {rescue ? 2 : 1,{1,0},{1,0},{1,0},Lifecycle::living,true,true,true};
    in.sites[0].damaged_now = true;
    in.independent_group = in.sites[1].group.id;
    in.windows.deadlines = {rescue ? 14 : 21,rescue ? 14 : 21,rescue ? 14 : 21};
    in.windows.windows = in.windows.deadlines; in.windows.sampled_round = 10;
    in.windows.sampled_actor_id = in.frame.clock.self;
    in.windows.source = Source::private_history; in.windows.valid = true;
    if (rescue) {
        in.sites[0].group.lifecycle = Lifecycle::reward_available;
        auto& owner = in.frame.actors[0].value;
        owner.health = 45; owner.max_health = 100; owner.bag = 20; owner.spawn_group = 0;
        owner.action = WitnessAction::occupied_spawn; owner.ready_attack_group = -1;
        in.frame.own.actor.health = in.frame.own.actor.max_health = 100;
        in.frame.own.actor.ready_attack_group = -1;
        Actor enemy; enemy.id = 900; enemy.team = Team::enemy; enemy.alive = true;
        enemy.health = enemy.max_health = 10; enemy.strength = 2; enemy.attack_range = 1; enemy.position = {0,1};
        in.frame.actors[in.frame.actor_count++] = seen(enemy,in.frame.clock,enemy.id);
        in.legal_enemy_target = enemy.id; in.enemy_stress_count = 1;
        in.enemy_stress[0] = seen(2.,in.frame.clock,enemy.id); f->source_gems = 50;
    }
    rebuild(*f); return f;
}
void populate(Fixture& f, std::string_view slug = "owner-builder") {
    joint::populate(f.in,f.models,f.requests,f.quotes,slug);
}
std::unique_ptr<Fixture> launchpad_fixture() {
    auto f = fixture(); auto& in = f->in;
    f->intact_source = true; in.sites[0].damaged_now = false;
    in.frame.own.actor.type = launchpad::profile_value.roster[1];
    in.frame.actors[0].value.type = launchpad::profile_value.roster[0];
    in.frame.actors[in.frame.actor_count++] = seen(
        ally(2,launchpad::profile_value.roster[2],{0,0}),in.frame.clock,102);
    in.proposed_group = 0; // The helper's distinct group 1 remains a real alternative.
    rebuild(*f); return f;
}
void launchpad_bootstrap() {
    auto f = launchpad_fixture(); auto& in = f->in;
    const auto& member = f->requests[0][0].groups[0].members[0];
    check(member.current.value.health == member.public_full.value.health && !in.sites[0].damaged_now,
        "bootstrap source is actually intact rather than only clearing the damaged flag");
    check(in.sites[0].solo(in.frame,100) && in.sites[0].solo(in.frame,101) &&
        in.sites[0].solo(in.frame,102) && f->models[1][0].status == Status::feasible &&
        f->requests[1][0].project_group != f->requests[0][0].project_group,
        "complete current core retains real safe solo and distinct continuation alternatives");
    populate(*f,"launchpad");
    check(in.sites[0].joint_better && in.sites[0].holder == 100,
        "populate admits positive intact-camp speedup for the actual fixed owner");
    launchpad::State active;
    const auto chosen = launchpad::choose(active,in,false);
    check(chosen.intent == organization::Intent::work && chosen.group == 0 &&
        chosen.reason == std::string_view("launchpad-personal-barrier-passed"),
        "real Launchpad policy selects the intact-camp crew branch over its competing solo work");
}
void launchpad_boundaries() {
    auto missing = launchpad_fixture(); auto& in = missing->in;
    in.frame.actor_count = 1; rebuild(*missing); populate(*missing,"launchpad");
    check(in.sites[0].joint_better && in.sites[0].solo(in.frame,in.frame.clock.self),
        "missing third core leaves the real two-actor gain and solo alternatives available");
    launchpad::State active, control;
    check(launchpad::choose(active,in,false).intent == organization::Intent::approach &&
        launchpad::choose(control,in,true).intent == organization::Intent::approach,
        "neither arm invents a presently missing third core actor at admission");
    in.frame.clock.round = 12; ++in.frame.clock.revision;
    in.frame.actors[0].clock = in.frame.clock; in.windows.sampled_round = 12;
    rebuild(*missing); populate(*missing,"launchpad");
    const auto waiting = launchpad::choose(active,in,false);
    const auto scheduled = launchpad::choose(control,in,true);
    check(waiting.intent == organization::Intent::approach &&
        waiting.reason == std::string_view("launchpad-eight-turn-staging"),
        "active launchpad still needs its real complete crew after the scheduled arrival");
    check(scheduled.intent == organization::Intent::work && scheduled.group == 0 &&
        scheduled.reason == std::string_view("launchpad-scheduled-safe-start"),
        "control retains its scheduled start under the real current solo certificate");

    auto unready = launchpad_fixture();
    unready->in.frame.actors[1].value.ready_attack_group = -1;
    rebuild(*unready); populate(*unready,"launchpad");
    launchpad::State unready_state;
    check(unready->in.sites[0].joint_better &&
        launchpad::choose(unready_state,unready->in,false).intent == organization::Intent::approach,
        "visible third actor's predicted arrival cannot pass the actual ready-crew barrier");

    auto stale = launchpad_fixture(); --stale->models[0][1].certificate.clock.revision;
    populate(*stale,"launchpad");
    check(!stale->in.sites[0].joint_better,
        "intact-camp exemption cannot use the fixed owner's stale model");

    auto unsafe = launchpad_fixture();
    unsafe->in.frame.actors[0].value.health = unsafe->in.frame.actors[0].value.max_health = 1;
    rebuild(*unsafe);
    check(unsafe->models[0][1].status != Status::feasible &&
        unsafe->models[0][1].issue == forecast::Issue::unsafe,
        "fixed owner's current solo alternative really fails its exposure bound");
    populate(*unsafe,"launchpad");
    check(!unsafe->in.sites[0].joint_better,
        "intact-camp exemption cannot replace an unsafe individual proof with joint credit");

    auto partial = launchpad_fixture(); partial->in.sites[0].group.all_spawns_visible = false;
    rebuild(*partial); populate(*partial,"launchpad");
    check(!partial->in.sites[0].joint_better,
        "intact-camp exemption still requires complete current member visibility");

    auto no_gain = launchpad_fixture(); no_gain->source_gems = no_gain->alternate_gems = 0;
    rebuild(*no_gain);
    check(no_gain->models[0][1].status == Status::feasible && no_gain->models[1][0].status == Status::feasible,
        "zero-gain case retains real safe competing models");
    populate(*no_gain,"launchpad");
    check(!no_gain->in.sites[0].joint_better,
        "intact-camp exemption still needs positive banked team gain");

    for (const std::string_view slug : {"owner-builder","guilds","caravan","witness"}) {
        auto unchanged = launchpad_fixture(); populate(*unchanged,slug);
        check(!unchanged->in.sites[0].joint_better,
            "non-Launchpad organizations retain the prior-damage requirement");
    }
}
void speedups() {
    auto f = fixture(); populate(*f);
    check(f->in.sites[0].joint_better && f->in.sites[0].holder == 100,
        "speedup credits actual fixed owner, even from helper's private view");
    check(f->in.sites[0].joint_rank.min_margin == 110,
        "one owner source and distinct helper source each contribute one bank");
    auto missing = fixture(); missing->in.frame.actors[0].value.ready_attack_group = -1; rebuild(*missing); populate(*missing);
    check(!missing->in.sites[0].joint_better,"owner not currently in useful attack position cannot receive future speedup");
    auto missing_helper = fixture(); missing_helper->in.frame.own.actor.ready_attack_group = -1; rebuild(*missing_helper); populate(*missing_helper);
    check(!missing_helper->in.sites[0].joint_better,"helper's predicted arrival cannot replace current useful participation");
    auto arrival = fixture(); arrival->in.frame.actors[0].value.position = {2,2}; rebuild(*arrival); populate(*arrival);
    check(!arrival->in.sites[0].joint_better,"pending owner arrival is not charged as combat-only cooperation");
    auto duplicate = fixture(); duplicate->in.site_count = 1; populate(*duplicate);
    check(!duplicate->in.sites[0].joint_better,"two actors cannot each forecast payout from same source");
    auto wrong_role = fixture(); wrong_role->in.sites[1].group.id = 2; rebuild(*wrong_role); populate(*wrong_role);
    check(!wrong_role->in.sites[0].joint_better,"core helper's displaced continuation follows its public owner circuit");
    auto stale = fixture(); --stale->models[0][1].certificate.clock.revision; populate(*stale);
    check(!stale->in.sites[0].joint_better,"persisted feasible result with stale clock is rejected");
    auto cumulative = fixture();
    cumulative->in.frame.own.actor.health = cumulative->in.frame.own.actor.max_health = 100;
    cumulative->source_strength = 4; cumulative->alternate_health = 50; cumulative->alternate_strength = 14;
    rebuild(*cumulative);
    check(cumulative->models[0][0].status == Status::feasible && cumulative->models[1][0].status == Status::feasible,
        "helper's two separate solo certificates are individually feasible");
    populate(*cumulative);
    check(!cumulative->in.sites[0].joint_better,"help damage plus full independent cycle cannot reset helper's starting health");
}
void rescue_workspace_validity() {
    auto f = fixture(true); auto& in = f->in;
    auto work = std::make_unique<joint::RescueWorkspace>();
    for (const auto* request : {&work->bank,&work->owner,&work->reserve}) {
        check(request->group_count == 0 && request->leg_count == 0 && request->project_group == -1 &&
            request->kit.knowledge == Knowledge::unknown &&
            !request->groups[0].membership_complete.known() &&
            !joint::fresh_request(in,in.frame.own.actor,*request,0),
            "fresh rescue workspace request retains unknown inputs and grants no model permission");
    }
    for (const auto* result : {&work->independent_owner,&work->saved_owner,&work->continued_reserve}) {
        check(result->status == Status::unresolved && result->issue == forecast::Issue::unknown_input &&
            result->schedule.actor_count == 0 && result->schedule.production_count == 0 &&
            result->phase_count == 0 && result->certificate.actor == -1 &&
            !joint::fresh_result(in,in.frame.own.actor,*result,0),
            "fresh rescue workspace result retains invalid counts and certificate defaults");
    }
    for (const auto* schedule : {&work->cooperative,&work->separate}) {
        check(!evaluate_schedule(*schedule,in.windows,in.settings,
            {in.frame.clock.round,in.frame.clock.self},in.frame.own_team_score).eligible(),
            "fresh rescue workspace schedule cannot supply an admitted comparison");
    }
    forecast::build_into(in.frame,f->requests[1][0],work->independent_owner);
    check(joint::fresh_result(in,in.frame.own.actor,work->independent_owner,in.sites[1].group.id),
        "in-place rescue output can hold the real safe displaced continuation");
    forecast::build_into(in.frame,work->bank,work->independent_owner);
    check(work->independent_owner.status != Status::feasible &&
        work->independent_owner.schedule.actor_count == 0 &&
        work->independent_owner.schedule.production_count == 0 &&
        work->independent_owner.phase_count == 0 && work->independent_owner.certificate.actor == -1 &&
        !joint::fresh_result(in,in.frame.own.actor,work->independent_owner,in.sites[1].group.id),
        "reused rescue output rejects invalid new input without retaining the prior safe proof");
}
void rescues() {
    auto f = fixture(true);
    check(f->models[0][1].status != Status::feasible && f->models[1][0].status == Status::feasible,
        "threatened owner cannot complete source alone while reserve has displaced safe work");
    populate(*f,"firebrigade");
    check(f->in.sites[0].joint_better && f->in.sites[0].holder == 100,
        "legal one-basic intervention preserves real carried inventory and source bank");
    check(f->in.sites[0].joint_rank.min_margin == 80,
        "saved owner 20 carried plus 50 source and reserve 10 are counted once");
    check(f->in.sites[0].cycle(100) && f->in.sites[0].cycle(100)->valid(f->in.frame,100,0) &&
        f->in.sites[0].cycle(102) && f->in.sites[0].cycle(102)->valid(f->in.frame,102,0),
        "joint certificates retain current observed health and cumulative kill exposure");
    auto no_value = fixture(true); no_value->source_gems = 0; rebuild(*no_value); populate(*no_value,"firebrigade");
    check(!no_value->in.sites[0].joint_better,"surviving an exciting rescue is insufficient without positive banked team gain");
    auto mask = fixture(true); mask->in.legal_enemy_target = -1; populate(*mask,"firebrigade");
    check(!mask->in.sites[0].joint_better,"range alone cannot replace personally checked legal attack mask");
    auto late = fixture(true); late->in.frame.actors[1].value.health = 90; rebuild(*late); populate(*late,"firebrigade");
    check(!late->in.sites[0].joint_better,"ninth basic kill turn is outside intervention budget");
    auto stress = fixture(true); stress->in.enemy_stress[0].value = 50; populate(*stress,"firebrigade");
    check(!stress->in.sites[0].joint_better,"full enemy maximum cast stress is charged through the kill");
    auto no_baseline = fixture(true); no_baseline->in.frame.actors[0].value.health = 20; rebuild(*no_baseline); populate(*no_baseline,"firebrigade");
    check(!no_baseline->in.sites[0].joint_better,"unsafe independent bank cannot be invented as a completed comparison");
    auto already_safe = fixture(true); already_safe->in.frame.actors[0].value.health = 100; rebuild(*already_safe); populate(*already_safe,"firebrigade");
    check(!already_safe->in.sites[0].joint_better,"already feasible valuable owner continuation is not replaced by a weak bank-only incumbent");
    auto continuing = fixture(true); continuing->in.frame.actor_count = 1;
    continuing->in.enemy_stress_count = 0; continuing->in.legal_enemy_target = -1;
    continuing->in.frame.clock = {11,102,2}; continuing->in.frame.actors[0].clock = continuing->in.frame.clock;
    continuing->in.windows.sampled_round = 11; rebuild(*continuing); populate(*continuing,"firebrigade");
    check(!continuing->in.sites[0].joint_better && continuing->in.sites[0].cycle(102) &&
        continuing->in.sites[0].cycle(102)->valid(continuing->in.frame,102,0),
        "actual killed threat leaves fresh safe wait certificate without inventing a new rescue gain");
}
void guard_boundaries() {
    auto stopped = fixture(); stopped->in.sites[0].holder = 777; stopped->in.sites[0].joint_better = true;
    joint::populate(stopped->in,stopped->models,stopped->requests,stopped->quotes,"owner-builder",[]{ return false; });
    check(stopped->in.sites[0].holder == 777 && stopped->in.sites[0].joint_better,
        "fuel callback stops before any site mutation");
    auto limited = fixture(); limited->in.sites[1].holder = 888;
    int calls = 0;
    joint::populate(limited->in,limited->models,limited->requests,limited->quotes,"owner-builder",
        [&]{ return ++calls <= 1; });
    check(!limited->in.sites[0].joint_better && limited->in.sites[1].holder == 888,
        "guard cutoff publishes no partial gain and leaves later sites untouched");
}
} // namespace
int main() {
    launchpad_bootstrap(); launchpad_boundaries(); speedups(); rescue_workspace_validity(); rescues(); guard_boundaries();
    std::printf("Plan6 runtime joint: %d checks passed\n",checks);
}
