#include "../common/src/runtime_economy.h"
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {
using namespace p6;
namespace fc = p6::forecast;
namespace economy = p6::runtime_economy;
int checks = 0;
void check(bool condition, const char* label) {
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAIL runtime economy: %s\n", label); std::exit(1); }
}
template<class T> Fact<T> setting(T value) { return {value, Source::queried_setting}; }
template<class T> Fact<T> map(T value) { return {value, Source::public_map}; }
template<class T> Observation<T> seen(T value, ViewClock clock, int who) {
    return {value, clock, who, Knowledge::exact};
}
struct Fixture {
    Frame frame{};
    fc::Request request{};
    ScenarioWindows windows{};
    Settings settings{200, 1000, 2};
    int to_bank = 1, bank_to_approach = 3;
};
void deadlines(Fixture& f, std::array<int, 3> values) {
    f.windows.deadlines = values;
    for (int i = 0; i < 3; ++i) f.windows.windows[i] = std::min(120, values[i]);
}
std::unique_ptr<Fixture> fixture() {
    auto f = std::make_unique<Fixture>();
    f->frame.clock = {0, 1, 0};
    auto& own = f->frame.own.actor;
    own.id = 1; own.pick_ordinal = 0; own.team = Team::ally;
    own.position = {0, 0}; own.health = own.max_health = 1000;
    own.strength = 10; own.bag = 50; own.alive = own.can_move = own.can_attack = true;
    own.ready_attack_group = 17; own.attack_range = 1; f->frame.own.gold = 9;
    auto& r = f->request;
    r.shape = fc::Shape::produce_bank; r.group_count = 1; r.leg_count = 4;
    r.timing.settings = setting(f->settings);
    r.timing.tribute_multiplier = setting(.1); r.timing.tribute_base = setting(0);
    r.timing.tribute_minimum = setting(1);
    r.kit = seen(Productivity{10, 0, false, false, false, false}, f->frame.clock, own.id);
    r.ambient_incoming = seen(0., f->frame.clock, own.id);
    r.bank_waypoint = 90; r.bank_position = {1, 0};
    r.renewal_waypoint = 91; r.renewal_position = {0, 0};
    r.bank_available = seen(true, f->frame.clock, 90);
    auto& g = r.groups[0];
    g.observed = seen(Group{17, {4, 0}, {5, 0}, {3, 0}, Lifecycle::living,
        true, true, true}, f->frame.clock, 17);
    g.membership_complete = map(true); g.member_count = 1;
    g.members[0].public_full = map(MonsterWork{117, 20, 1});
    g.members[0].current = seen(MonsterWork{117, 20, 1}, f->frame.clock, 117);
    g.gems = map<Gems>(20); g.gold = map<Gems>(7);
    f->frame.group = g.observed;
    r.legs[0] = {own.position, g.observed.value.approach, map(4)};
    r.legs[1] = {g.observed.value.approach, g.observed.value.spawn, map(1)};
    r.legs[2] = {g.observed.value.spawn, r.bank_position, map(4)};
    r.legs[3] = {r.bank_position, r.renewal_position, map(1)};
    f->windows.valid = true; f->windows.source = Source::synthetic_fixture;
    f->windows.sampled_actor_id = own.id; f->windows.sampled_round = 0;
    deadlines(*f, {10, 40, 200});
    return f;
}
std::unique_ptr<economy::Selection> choose(const Fixture& f) {
    auto result = std::make_unique<economy::Selection>();
    economy::choose_into(*result, f.frame, f.request, f.windows, f.settings,
                        f.to_bank, f.bank_to_approach);
    return result;
}
int tick(const fc::Result& result, EventKind kind, int ordinal = 0) {
    for (int i = 0; i < result.schedule.actors[0].event_count; ++i) {
        const auto& e = result.schedule.actors[0].events[i];
        if (e.kind == kind && ordinal-- == 0) return e.tick;
    }
    return -1;
}
void routes_and_resources() {
    auto f = fixture(); auto result = choose(*f);
    check(result->production.status == Status::feasible && result->bank_first.status == Status::feasible &&
          result->bank_only.status == Status::feasible, "all exact current alternatives are feasible");
    // Hand timing: positive legs add two slack turns; clearing costs two;
    // capture costs three; old/new tribute durations are five/two owner ticks.
    check(tick(result->production, EventKind::production) == 14 &&
          tick(result->production, EventKind::tribute) == 27 &&
          tick(result->production, EventKind::renewal) == 30,
          "base route banks old plus new inventory at tick27 and renews30");
    check(tick(result->bank_first, EventKind::tribute) == 8 &&
          tick(result->bank_first, EventKind::production) == 21 &&
          tick(result->bank_first, EventKind::tribute, 1) == 29 &&
          tick(result->bank_first, EventKind::renewal) == 32,
          "bank-first uses self-bank, bank-approach, then original remaining legs exactly once");
    check(tick(result->bank_only, EventKind::tribute) == 8 &&
          tick(result->bank_only, EventKind::renewal) == 11 &&
          result->bank_only.schedule.production_count == 0,
          "bank-only secures actual old bag without any source income");
    check(result->bank_only.certificate.group == 17 && result->bank_only.certificate.valid(f->frame, 1, 17),
          "bank-only retains project anchor without manufacturing a group payout");
    for (const auto* r : {&result->production, &result->bank_first, &result->bank_only}) {
        check(r->schedule.actor_count == 1 && r->schedule.actors[0].actor_id == 1,
              "all alternatives represent exactly the same own actor");
        check(r->schedule.actors[0].initial_bag.value == 50 &&
              r->schedule.actors[0].initial_gold.value == 9,
              "self inventory and self gold initialize every alternative");
    }
    check(f->frame.own.actor.bag == 50 && f->frame.own.gold == 9 &&
          f->request.shape == fc::Shape::produce_bank && f->request.leg_count == 4,
          "selection is pure and does not receipt a bank or mutate source request");
    Actor ally; ally.id = 2; ally.team = Team::ally; ally.alive = true;
    ally.health = ally.max_health = 100; ally.bag = 500; ally.position = {2, 2};
    f->frame.actors[0] = seen(ally, f->frame.clock, 2); f->frame.actor_count = 1;
    auto extra = choose(*f);
    check(extra->bank_rank.own_score == result->bank_rank.own_score &&
          extra->production_rank.own_score == result->production_rank.own_score,
          "visible ally inventory is never added to own alternatives");
}
void deadlines_and_empty_bag() {
    auto f = fixture(); auto mixed = choose(*f);
    check(mixed->production_rank.own_score == std::array<Gems, 3>{0, 70, 70} &&
          mixed->bank_first_rank.own_score == std::array<Gems, 3>{50, 70, 70} &&
          mixed->bank_rank.own_score == std::array<Gems, 3>{50, 50, 50},
          "hand oracle counts each complete old/new bank once at its own deadline");
    check(mixed->bank_preferred && compare(mixed->bank_first_rank, mixed->bank_rank) > 0,
          "urgent old bag plus later new output selects bank-first");
    deadlines(*f, {10, 10, 10}); auto urgent = choose(*f);
    check(urgent->bank_preferred && urgent->bank_rank.occupancy == 11 &&
          urgent->bank_first_rank.occupancy == 32 &&
          compare(urgent->bank_rank, urgent->bank_first_rank) > 0,
          "equal urgent bank output favors shorter bank-only occupation");
    deadlines(*f, {200, 200, 200}); auto relaxed = choose(*f);
    check(!relaxed->bank_preferred && relaxed->production_rank.own_score[0] == 70 &&
          relaxed->production_rank.occupancy == 30 && relaxed->bank_first_rank.occupancy == 32,
          "stable full horizon favors production's shorter complete cycle");
    f->frame.own.actor.bag = 0; auto empty = choose(*f);
    check(empty->production_rank.eligible() && !empty->bank_first_rank.eligible() &&
          !empty->bank_rank.eligible() && !empty->bank_preferred,
          "zero inventory never creates a bank-first or empty bank claim");
}
void unavailable_or_malformed() {
    auto f = fixture();
    f->to_bank = -1; auto missing_bank = choose(*f);
    check(missing_bank->production_rank.eligible() && !missing_bank->bank_rank.eligible() &&
          !missing_bank->bank_first_rank.eligible() && !missing_bank->bank_preferred,
          "unavailable self-bank route licenses no bank alternative");
    f->to_bank = 1; f->bank_to_approach = -1; auto missing_return = choose(*f);
    check(missing_return->bank_rank.eligible() && !missing_return->bank_first_rank.eligible(),
          "unknown return route preserves bank-only without promising new production");
    f->bank_to_approach = 3; f->request.legs[0].to = {3, 0}; auto wrong_leg = choose(*f);
    check(!wrong_leg->production_rank.eligible() && !wrong_leg->bank_rank.eligible() &&
          !wrong_leg->bank_first_rank.eligible() && !wrong_leg->bank_preferred,
          "malformed base route cannot become a valid projected alternative");
    f = fixture(); f->settings.capture_rounds = 3; auto copied_setting = choose(*f);
    check(!copied_setting->production_rank.eligible() && !copied_setting->bank_rank.eligible(),
          "caller settings must match current queried forecast settings");
    f = fixture(); f->request.shape = fc::Shape::retain_two_then_bank; auto wrong_shape = choose(*f);
    check(!wrong_shape->production_rank.eligible() && !wrong_shape->bank_rank.eligible(),
          "selector rejects requests outside its one-group shape contract");
    f = fixture(); f->frame.own.actor.channel = Channel::tribute;
    f->frame.own.actor.channel_valid = true; auto channel = choose(*f);
    check(!channel->production_rank.eligible() && !channel->bank_rank.eligible() &&
          !channel->bank_first_rank.eligible() && !channel->bank_preferred,
          "current channel availability is not forecast away");
    f = fixture(); f->request.bank_available.value = false; auto locked = choose(*f);
    check(!locked->production_rank.eligible() && !locked->bank_rank.eligible() && !locked->bank_preferred,
          "unavailable altar without a queried lock bound has no feasible bank claim");
    f = fixture(); f->windows.sampled_round = 1; auto stale_history = choose(*f);
    check(!stale_history->production_rank.eligible() && !stale_history->bank_rank.eligible() &&
          !stale_history->bank_first_rank.eligible() && !stale_history->bank_preferred,
          "forecast ranks reject stale actor-specific history");
    f = fixture(); f->request.ambient_incoming.clock.revision = 1; auto stale_exposure = choose(*f);
    check(!stale_exposure->production_rank.eligible() && !stale_exposure->bank_rank.eligible(),
          "bank alternative preserves fresh current exposure requirement");
    f = fixture(); auto reused = choose(*f);
    check(reused->bank_rank.eligible(), "heap destination initially contains feasible bank rank");
    f->request.leg_count = 0;
    economy::choose_into(*reused, f->frame, f->request, f->windows, f->settings,
                         f->to_bank, f->bank_to_approach);
    check(!reused->production_rank.eligible() && !reused->bank_rank.eligible() &&
          !reused->bank_first_rank.eligible() && !reused->bank_preferred,
          "reused runtime destination clears all old feasibility on malformed fresh input");
}
} // namespace
int main() {
    routes_and_resources(); deadlines_and_empty_bag(); unavailable_or_malformed();
    std::printf("runtime economy: %d checks passed\n", checks);
}
