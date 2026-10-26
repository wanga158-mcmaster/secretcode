#include "../reference/src/policy.h"
#include "../common/src/runtime_catalogue.h"
#include "../common/src/runtime_opportunities.h"
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {
using namespace p6;
namespace fc = p6::forecast;
int checks = 0;
void check(bool value, const char* label) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL zero-gem cycle: %s\n", label); std::exit(1); }
}
template<class T> Fact<T> setting(T value) { return {value, Source::queried_setting}; }
template<class T> Fact<T> map(T value) { return {value, Source::public_map}; }
template<class T> Observation<T> seen(T value, ViewClock clock, int who) {
    return {value, clock, who, Knowledge::exact};
}
struct Fixture { ReferenceInput actual{}; fc::Request request{}; };
void leg(fc::Request& r, Point from, Point to, int distance) {
    r.legs[r.leg_count++] = {from, to, map(distance)};
}
std::unique_ptr<Fixture> fixture(int group = 17, Point approach = {0, 0}) {
    auto t = std::make_unique<Fixture>(); auto& f = t->actual.frame; auto& r = t->request;
    f.clock = {10, 1, 0};
    auto& own = f.own.actor;
    own.id = 1; own.team = Team::ally; own.type = Champion::varg;
    own.health = own.max_health = 100; own.strength = 1;
    own.alive = own.can_move = own.can_attack = true; own.ready_attack_group = group;
    f.own.gold = 9;
    r.group_count = 1; r.timing.settings = setting(Settings{100, 1000, 2});
    r.timing.tribute_multiplier = setting(.1); r.timing.tribute_base = setting(0);
    r.timing.tribute_minimum = setting(1);
    r.kit = seen(Productivity{1, 0, false, false, false, false}, f.clock, 1);
    r.ambient_incoming = seen(0., f.clock, 1);
    r.bank_waypoint = 90; r.bank_position = {3, 0};
    r.renewal_waypoint = 91; r.renewal_position = {0, 0};
    r.bank_available = seen(true, f.clock, 90);
    auto& g = r.groups[0];
    const Point spawn{approach.x + 1, approach.y};
    g.observed = seen(Group{group, approach, spawn, {approach.x, approach.y + 1},
        Lifecycle::living, true, true, true}, f.clock, group);
    g.membership_complete = map(true); g.member_count = 1;
    g.members[0].public_full = map(MonsterWork{100 + group, 2, 1});
    g.members[0].current = seen(MonsterWork{100 + group, 2, 1}, f.clock, 100 + group);
    g.gems = map<Gems>(0); g.gold = map<Gems>(7); f.group = g.observed;
    leg(r, own.position, approach, std::abs(approach.x - own.position.x));
    leg(r, approach, spawn, 1);
    leg(r, spawn, r.bank_position, std::abs(spawn.x - r.bank_position.x));
    leg(r, r.bank_position, r.renewal_position, 3);
    t->actual.assigned_group = group; t->actual.safe_now = true;
    t->actual.at_approach = own.position == approach;
    t->actual.forecast_rounds = 17; t->actual.remaining_rounds = 90;
    t->actual.capture_window = 3; t->actual.fresh_target_attack_legal = true;
    return t;
}
std::unique_ptr<fc::Result> build(const Fixture& t) {
    return std::make_unique<fc::Result>(fc::build(t.actual.frame, t.request));
}
int events(const fc::Result& r, EventKind kind) {
    int count = 0;
    for (int i = 0; i < r.schedule.actors[0].event_count; ++i)
        count += r.schedule.actors[0].events[i].kind == kind;
    return count;
}
int tick(const fc::Result& r, EventKind kind) {
    for (int i = 0; i < r.schedule.actors[0].event_count; ++i)
        if (r.schedule.actors[0].events[i].kind == kind) return r.schedule.actors[0].events[i].tick;
    return -1;
}
void certificate(ReferenceInput& actual) {
    const auto& f = actual.frame; const auto& a = f.own.actor;
    const Exposure safe{a.health, a.max_health, 2, 1, 0, 0, true};
    actual.solo = {f.clock, a.id, actual.assigned_group, safe, safe, safe, safe,
                   true, true, true, true};
}
void round(ReferenceInput& actual, int value) {
    actual.frame.clock.round = value; ++actual.frame.clock.revision;
    actual.frame.group.clock = actual.frame.clock; actual.completion = {};
    certificate(actual);
}
void forecast_gold_only() {
    auto t = fixture(); auto result = build(*t);
    check(result->status == Status::feasible && result->certificate.valid(t->actual.frame, 1, 17),
          "gold-only production has a complete current safe cycle");
    check(result->schedule.production_count == 1 && result->schedule.productions[0].gems.value == 0 &&
          result->schedule.productions[0].gold.value == 7,
          "zero-gem production still represents its exact gold payout");
    check(events(*result, EventKind::tribute) == 0 && events(*result, EventKind::renewal) == 1,
          "empty liquidation creates no tribute event and still requires named renewal");
    // Clear2 + approach/spawn3 + capture3 = payout8; bank route4 and renewal5.
    check(tick(*result, EventKind::production) == 8 && tick(*result, EventKind::renewal) == 17 &&
          result->schedule.actors[0].occupied_turns.value == 17,
          "hand timing retains route/exposure without adding a fake minimum tribute tick");
    check(result->certificate.bank.incoming_total >= result->certificate.hold.incoming_total &&
          result->certificate.recovery.incoming_total >= result->certificate.bank.incoming_total,
          "vacuous liquidation keeps cumulative milestone exposure");
    ScenarioWindows windows; windows.valid = true; windows.source = Source::synthetic_fixture;
    windows.sampled_round = 10; windows.sampled_actor_id = 1;
    windows.deadlines.fill(90); windows.windows.fill(90);
    const auto score = evaluate_schedule(result->schedule, windows, {100, 1000, 2}, {10, 1}, 0);
    check(score.eligible() && score.own_score == std::array<Gems, 3>{0, 0, 0},
          "gold-only bootstrap is feasible without pretending it scores tributed gems");
    t->request.bank_available.value = false;
    t->request.bank_lock_hits = setting(5); auto no_lock_work = build(*t);
    check(no_lock_work->status == Status::feasible && tick(*no_lock_work, EventKind::renewal) == 17 &&
          events(*no_lock_work, EventKind::tribute) == 0,
          "empty liquidation does not clear a hostile lock for an unnecessary tribute");
    t->request.bank_available.value = true; t->actual.frame.own.actor.bag = 5;
    auto retained = build(*t);
    check(retained->status == Status::feasible && events(*retained, EventKind::tribute) == 1 &&
          tick(*retained, EventKind::tribute) == 13,
          "gold-only production still liquidates a real pre-existing bag");
}
void reject_initial_empty_banking() {
    auto t = fixture(); auto& r = t->request;
    r.shape = fc::Shape::bank_only; r.project_group = 17; r.group_count = 0; r.leg_count = 0;
    leg(r, {0, 0}, r.bank_position, 3); leg(r, r.bank_position, r.renewal_position, 3);
    auto bank_only = build(*t);
    check(bank_only->status != Status::feasible && bank_only->issue == fc::Issue::unavailable,
          "initial empty bank-only remains unavailable");
    t = fixture(); auto& first = t->request;
    first.shape = fc::Shape::bank_then_produce_bank; first.leg_count = 0;
    leg(first, {0, 0}, first.bank_position, 3);
    leg(first, first.bank_position, first.groups[0].observed.value.approach, 3);
    leg(first, {0, 0}, {1, 0}, 1); leg(first, {1, 0}, first.bank_position, 2);
    leg(first, first.bank_position, first.renewal_position, 3);
    auto bank_first = build(*t);
    check(bank_first->status != Status::feasible && bank_first->issue == fc::Issue::unavailable,
          "initial empty bank-first cannot borrow the future production event");
}
void actual_zero_bag_cycle() {
    auto t = fixture(); auto& actual = t->actual; ActorState state;
    check(state.admit(17, 10, 17, 90, 0, 9), "actual gold-only incumbent admits");
    state.phase = Phase::hold; state.previous_observed = true; state.previous_gold = 9;
    actual.frame.own.actor.position = {1, 0}; actual.frame.own.actor.spawn_group = 17;
    actual.frame.group.value.lifecycle = Lifecycle::reward_available;
    round(actual, 40);
    auto elapsed = reference::choose(state, actual);
    check(elapsed.action == Action::hold && !state.payout_seen && !state.empty_liquidation,
          "elapsed forecast payout time cannot start zero-bag renewal");
    round(actual, 41); actual.frame.own.gold = 16;
    auto unattributed = reference::choose(state, actual);
    check(unattributed.action == Action::hold && !state.empty_liquidation,
          "unattributed gold increase alone is no camp payout receipt");
    round(actual, 42); actual.completion = {actual.frame.clock, 17, true, false, true};
    auto unchanged = reference::choose(state, actual);
    check(unchanged.action == Action::hold && !state.empty_liquidation,
          "payout label without a current resource increase cannot start renewal");
    round(actual, 43); actual.frame.own.gold = 23;
    actual.frame.group.value.lifecycle = Lifecycle::respawning;
    actual.completion = {actual.frame.clock, 17, true, false, true};
    auto paid = reference::choose_forecast(state, actual, fc::Request{});
    check(paid && paid->action == Action::move && paid->phase == Phase::travel &&
          state.active && state.payout_seen && state.empty_liquidation && !state.bank_seen,
          "actual gold increase plus attributed payout starts physical renewal without a new source quote");
    check(!actual.completion.own_bank && !observed_bank(state, actual) && !state.tribute_started &&
          actual.frame.own_team_score == 0 && actual.frame.own.actor.bag == 0,
          "vacuous liquidation is never an actual bank receipt");
    check(!confirm_tribute_started(state, actual.frame, seen(Action::tribute, actual.frame.clock, 1)),
          "zero inventory cannot confirm a successful tribute action");
    round(actual, 44); auto approaching = reference::choose_forecast(state, actual, fc::Request{});
    check(approaching && approaching->action == Action::move && state.active && state.empty_liquidation,
          "later empty quote preserves pending actual renewal while actor remains at old spawn");
    round(actual, 45); actual.frame.own.actor.position = {0, 0};
    actual.frame.own.actor.spawn_group = -1; actual.at_renewal = true;
    auto renewed = reference::choose_forecast(state, actual, fc::Request{});
    check(renewed && !state.active && !state.empty_liquidation && !state.bank_seen && !state.payout_seen,
          "actual named renewal position closes zero-bag cycle without changing bank receipts");
}
void terminal_guard_and_failure_history() {
    auto t = fixture(18, {2, 0}); ActorState incumbent;
    check(incumbent.admit(17, 10, 17, 90, 0, 9), "old zero-bag project admits");
    incumbent.empty_liquidation = incumbent.payout_seen = true; incumbent.phase = Phase::travel;
    check(!bind_cycle_forecast(incumbent, t->actual, t->request),
          "next source cannot replace pending physical zero-bag renewal");
    t->actual.at_renewal = true;
    check(bind_cycle_forecast(incumbent, t->actual, t->request).has_value(),
          "fresh next source binds only after actual old renewal is reached");
    auto next = reference::choose_forecast(incumbent, t->actual, t->request);
    check(next && incumbent.active && incumbent.project.group == 18 &&
          !incumbent.empty_liquidation && !incumbent.bank_seen,
          "physical renewal can admit a fresh next cycle without forged bank history");
    ActorState failed;
    check(failed.admit(9, 10, 3, 90, 0, 9) && failed.repair(), "failed epoch retains one spent repair");
    failed.empty_liquidation = true; failed.retire(Failure::observed_failed_action);
    check(!failed.empty_liquidation && !failed.admit(9, 11, 3, 89, 0, 9) &&
          failed.failure == Failure::observed_failed_action,
          "failed re-admission neither clears current failure nor revives retired project");
    check(failed.admit(10, 11, 3, 89, 0, 9) && failed.failure == Failure::none,
          "successful fresh admission clears stale execution failure");
    const auto* old = failed.budget(9, 0);
    check(old && old->retired && old->repairs == 1,
          "clearing current failure preserves old epoch retirement and spent repair");
}
void catalogue_ordering() {
    namespace catalogue = runtime_catalogue;
    const std::array<catalogue::Site, 13> sites{{
        {90, 0, 40, 0}, {5, 17, 40, 0}, {5, 18, 40, 0},
        {0, 20, 60, 4}, {1, 21, 60, 4}, {2, 22, 60, 4}, {3, 23, 60, 4},
        {4, 24, 60, 4}, {5, 25, 60, 4}, {6, 26, 60, 4}, {7, 27, 60, 4},
        {8, 28, 60, 4}, {0, 29, 0, 0}}};
    const auto first = catalogue::select(sites);
    check(first.count == 8 && sites[first.indices[0]].id == 17,
          "catalogue retains a local gold bootstrap before seven best gem sources");
    bool best_gems = true;
    for (std::size_t i = 1; i < first.count; ++i)
        best_gems &= sites[first.indices[i]].id == 19 + static_cast<int>(i);
    check(best_gems, "bootstrap reservation preserves competing best gem opportunities");
    const auto second = catalogue::select(sites, 1);
    check(sites[second.indices[0]].id == 18 &&
          sites[catalogue::select(sites, 2).indices[0]].id == 17,
          "actor spread rotates only exact minimum-key bootstrap ties");
    check(sites[catalogue::select(sites, 9).indices[0]].id != 0,
          "large actor ordinal cannot select the distant lower-ID gold source");
    const auto no_gold = catalogue::select(std::span<const catalogue::Site>(sites).subspan(3, 9));
    check(no_gold.count == 8 && sites[3 + no_gold.indices[0]].id == 20 &&
          sites[3 + no_gold.indices[7]].id == 27,
          "without a bootstrap the catalogue keeps eight best gem sources");
    const std::array<catalogue::Site, 5> small{{
        {3, 4, 40, 0}, {1, 1, 60, 4}, {2, 3, 40, 0},
        {2, 3, 40, 0}, {0, 9, 0, 0}}};
    const auto filled = catalogue::select(small);
    check(filled.count == 3 && small[filled.indices[0]].id == 3 &&
          small[filled.indices[1]].id == 1 && small[filled.indices[2]].id == 4,
          "small catalogue fills useful gold sources without duplicate identities or empty sites");
    check(catalogue::select({}).count == 0, "empty public catalogue stays empty");
}
void catalogue_to_actual_bootstrap() {
    namespace catalogue = runtime_catalogue;
    const std::array<catalogue::Site, 3> sites{{{50, 2, 40, 0}, {3, 17, 40, 0}, {1, 18, 60, 4}}};
    const auto order = catalogue::select(sites);
    const int chosen = sites[order.indices[0]].id;
    auto t = fixture(chosen); auto& actual = t->actual; auto& request = t->request;
    auto& own = actual.frame.own.actor;
    own.health = own.max_health = 1000; own.strength = 40; actual.frame.own.gold = 0;
    request.kit.value.strength = 40;
    auto& group = request.groups[0]; group.member_count = 2; group.gold = map<Gems>(40);
    for (int i = 0; i < group.member_count; ++i) {
        const MonsterWork member{100 + chosen + i, 120, 0};
        group.members[i].public_full = map(member);
        group.members[i].current = seen(member, actual.frame.clock, member.id);
    }
    auto evaluated = std::make_unique<fc::EvaluatedQuote>();
    evaluated->evaluate(actual.frame, request);
    check(chosen == 17 && evaluated->ready() && evaluated->result().status == Status::feasible,
          "chosen public gold catalogue entry becomes a fresh evaluated safe zero-gem cycle");
    check(evaluated->result().schedule.productions[0].gold.value == 40 &&
          events(evaluated->result(), EventKind::tribute) == 0 &&
          evaluated->result().purchases_deferred && own.strength == 40,
          "bootstrap quote preserves exact gold and credits neither tribute nor future stat growth");
    // A live competing gem quote remains available; the catalogue has not
    // replaced the organization policy or suppressed all other opportunities.
    auto gem = fixture(sites[order.indices[1]].id);
    gem->actual.frame = actual.frame; gem->request.kit.value.strength = own.strength;
    gem->request.groups[0].gems = map<Gems>(4);
    auto competing = std::make_unique<fc::EvaluatedQuote>();
    competing->evaluate(gem->actual.frame, gem->request);
    check(order.count == 3 && competing->result().status == Status::feasible &&
          competing->matches_frame(actual.frame),
          "catalogue retains a genuinely evaluated current gem alternative");
    ActorState state;
    auto started = reference::choose_forecast_replacing_into(state, actual, evaluated.get());
    check(started && started->action == Action::attack && state.active && state.project.group == chosen,
          "real reference caller admits and attacks the selected evaluated bootstrap");
    round(actual, 11); own.position = group.observed.value.spawn; own.spawn_group = chosen;
    actual.frame.group.value.lifecycle = Lifecycle::reward_available;
    actual.frame.group.value.approach = own.position; actual.at_approach = true;
    group.observed = actual.frame.group;
    for (int i = 0; i < group.member_count; ++i) {
        auto member = group.members[i].public_full.value; member.health = 0;
        group.members[i].current = seen(member, actual.frame.clock, member.id);
    }
    request.kit.clock = request.ambient_incoming.clock = request.bank_available.clock = actual.frame.clock;
    request.leg_count = 0;
    leg(request, own.position, own.position, 0); leg(request, own.position, own.position, 0);
    leg(request, own.position, request.bank_position, 2);
    leg(request, request.bank_position, request.renewal_position, 3);
    auto capture_quote = std::make_unique<fc::EvaluatedQuote>();
    capture_quote->evaluate(actual.frame, request);
    auto hold = reference::choose_forecast_into(state, actual, *capture_quote);
    check(capture_quote->result().status == Status::feasible && hold && hold->action == Action::hold &&
          state.phase == Phase::hold && !state.payout_seen,
          "fresh evaluated cleared-source quote enters actual capture without inventing its gold receipt");
    state.previous_observed = true; state.previous_gold = 0; state.previous_bag = 0;
    round(actual, 12); actual.frame.own.gold = 40;
    actual.frame.group.value.lifecycle = Lifecycle::respawning;
    actual.completion = {actual.frame.clock, chosen, true, false, true};
    auto payout = reference::choose(state, actual);
    check(payout.action == Action::move && state.empty_liquidation && state.payout_seen &&
          !state.bank_seen && actual.frame.own_team_score == 0,
          "attributed actual gold-only capture starts physical renewal without a bank receipt");
    round(actual, 13); own.position = request.bank_position;
    actual.frame.own.gold = 10; own.strength = 45; // Observed legal 30-gold Strength purchase.
    auto shopping = reference::choose(state, actual);
    check(shopping.action == Action::move && state.active && state.empty_liquidation &&
          actual.frame.own.gold == 10 && own.strength == 45 &&
          evaluated->request().kit.value.strength == 40 && !evaluated->matches_frame(actual.frame),
          "actual shop/stat observation preserves renewal and cannot restamp the old pre-purchase quote");
    round(actual, 14); own.position = request.renewal_position; own.spawn_group = -1;
    actual.at_renewal = true; actual.assigned_group = -1;
    reference::choose(state, actual);
    check(!state.active && !state.empty_liquidation && !state.payout_seen && !state.bank_seen,
          "actual named renewal closes the selected bootstrap after observed shopping");
}
void repriced_opportunity_preference() {
    namespace catalogue = runtime_catalogue;
    const std::array<catalogue::Site, 3> hard{{
        {3, 17, 40, 0, 240, 0, 2}, {1, 18, 150, 15, 2760, 60, 4},
        {2, 19, 100, 8, 2820, 40, 3}}};
    const auto order = catalogue::select(hard);
    const catalogue::Actor opening{1000, 1000, 40, false};
    check(hard[catalogue::preferred(hard, order, opening)].id == 17,
          "unaffordable multi-member gem opening retains safe gold bootstrap preference");
    const catalogue::Actor grown{1500, 1500, 140, false};
    check(hard[catalogue::preferred(hard, order, grown)].id == 19 &&
          hard[catalogue::preferred(hard, order, opening)].id == 17,
          "later observed growth reprices a viable gem without mutating or forecasting actor stats");
    const std::array<catalogue::Site, 2> viable{{
        {3, 17, 40, 0, 240, 0, 2}, {1, 18, 60, 4, 600, 10, 2}}};
    const auto viable_order = catalogue::select(viable);
    check(viable[viable_order.indices[0]].id == 17 &&
          viable[catalogue::preferred(viable, viable_order, opening)].id == 18,
          "gold reservation does not displace initially viable gem production preference");
    const std::array<catalogue::Site, 2> ranged{{
        {3, 17, 40, 0, 240, 0, 2}, {1, 18, 60, 4, 1200, 20, 1}}};
    const auto ranged_order = catalogue::select(ranged);
    const catalogue::Actor moving{1000, 1000, 40, true};
    const catalogue::Actor stationary{1000, 1000, 40, true, 18};
    check(ranged[catalogue::preferred(ranged, ranged_order, moving)].id == 17 &&
          ranged[catalogue::preferred(ranged, ranged_order, stationary)].id == 18 &&
          ranged[catalogue::preferred(ranged, ranged_order, {1000,1000,40,true,19})].id == 17,
          "ranged half-rate changes only with a current witness for that exact group");
    check(!catalogue::basic_work_viable({1,18,60,4,1200,20,5}, stationary),
          "all-member rounding and target switches remain charged at whole-group incoming");
    check(!catalogue::basic_work_viable(viable[1], {1000,1000,0,false}) &&
          !catalogue::basic_work_viable({1,18,60,4}, opening) &&
          catalogue::preferred({}, opening) == 0,
          "missing source work or invalid actor strength cannot invent viable production");
}
void preferred_gem_to_reference_caller() {
    namespace catalogue = runtime_catalogue;
    const std::array<catalogue::Site, 2> sites{{
        {3,17,40,0,240,0,2}, {1,18,60,4,600,10,2}}};
    const auto order = catalogue::select(sites);
    const auto chosen = catalogue::preferred(sites, order, {1000,1000,40,false});
    auto t = fixture(sites[chosen].id); auto& actual = t->actual; auto& request = t->request;
    actual.frame.own.actor.health = actual.frame.own.actor.max_health = 1000;
    actual.frame.own.actor.strength = 40; request.kit.value.strength = 40;
    auto& group = request.groups[0]; group.member_count = 2;
    group.gold = map<Gems>(60); group.gems = map<Gems>(4);
    for (int i = 0; i < group.member_count; ++i) {
        const MonsterWork member{200+i, 300, 5};
        group.members[i].public_full = map(member);
        group.members[i].current = seen(member, actual.frame.clock, member.id);
    }
    auto evaluated = std::make_unique<fc::EvaluatedQuote>();
    evaluated->evaluate(actual.frame, request);
    ActorState state;
    auto action = reference::choose_forecast_replacing_into(state, actual, evaluated.get());
    check(order.count == 2 && sites[order.indices[0]].gems == 0 && sites[chosen].gems == 4 &&
          evaluated->result().status == Status::feasible && action && action->action == Action::attack &&
          state.active && state.project.group == sites[chosen].id,
          "catalogue opportunity preference reaches fresh evaluated gem execution with gold retained");
    request.ambient_incoming.value = 500;
    auto unsafe = std::make_unique<fc::EvaluatedQuote>();
    unsafe->evaluate(actual.frame, request); ActorState blocked;
    auto refused = reference::choose_forecast_replacing_into(blocked, actual, unsafe.get());
    check(catalogue::preferred(sites, order, {1000,1000,40,false}) == chosen &&
          unsafe->result().status != Status::feasible && !refused && !blocked.active,
          "public opportunity screen cannot authorize actions rejected by fresh full exposure proof");
}
void inspection_preference_keeps_admission_and_ownership() {
    namespace opportunities = runtime_opportunities;
    using Kind = opportunities::Kind;
    opportunities::History history;
    std::array<opportunities::Candidate, 3> candidates{{
        {17,Kind::inspection,true,false}, {18,Kind::inspection,true,false},
        {19,Kind::deferred,true,false}}};
    const auto better = [](int a, int b) { return a > b; };
    check(opportunities::select(candidates, history, 10, better) == 0 &&
          opportunities::select(candidates, history, 10, better, -1) == 0 &&
          opportunities::select(candidates, history, 10, better, 99) == 0,
          "omitted or invalid preference preserves first eligible inspection behavior");
    check(opportunities::select(candidates, history, 10, better, 1) == 1,
          "preferred current eligible inspection can precede the old first inspection");
    check(history.reject(1,18,10), "completed rejected production starts the real opportunity cooldown");
    const auto cooled = history;
    check(opportunities::select(candidates, history, 11, better, 1) == 0 && history == cooled &&
          opportunities::select(candidates, history, 18, better, 1) == 1,
          "inspection preference cannot bypass or extend rejection cooldown and resumes at expiry");
    ActorState retired;
    check(retired.admit(18,10,17,90,0,9), "retired preferred destination has a genuine prior admission");
    retired.retire(Failure::unsafe);
    candidates[1].eligible = reference::admission_eligible(retired,18,18);
    check(!candidates[1].eligible && opportunities::select(candidates, history, 18, better, 1) == 0,
          "expired selection cooldown cannot bypass the reference retirement eligibility fence");

    opportunities::History fresh;
    const std::array<opportunities::Candidate, 4> ranked{{
        {17,Kind::inspection,true,false}, {18,Kind::inspection,true,false},
        {19,Kind::quote,true,false}, {20,Kind::quote,true,false}}};
    const std::array<int,4> quote_ranks{0,0,7,2};
    const auto rank = [&](int a, int b) { return quote_ranks[a] > quote_ranks[b]; };
    check(opportunities::select(ranked,fresh,20,rank) == 2 &&
          opportunities::select(ranked,fresh,20,rank,1) == 2 &&
          opportunities::select(ranked,fresh,20,rank,3) == 2,
          "preferred inspection or weaker quote cannot displace the best current ranked quote");
    std::array<opportunities::Candidate,3> waiting{{
        {17,Kind::deferred,true,false}, {18,Kind::inspection,true,false},
        {19,Kind::deferred,true,false}}};
    check(opportunities::select(waiting,fresh,20,better,2) == 1,
          "preferred deferred source cannot preempt a current actual inspection");
    waiting[1].kind = Kind::none;
    check(opportunities::select(waiting,fresh,20,better) == 0 &&
          opportunities::select(waiting,fresh,20,better,2) == 2,
          "deferred preference applies only when no current inspection or quote is usable");

    auto t = fixture(); auto& actual = t->actual; ActorState conversion;
    check(conversion.admit(17,10,17,90,0,9), "protected conversion starts from a real admitted source");
    conversion.phase = Phase::hold; conversion.previous_observed = true; conversion.previous_gold = 9;
    round(actual,11); actual.frame.own.gold = 16;
    actual.frame.own.actor.position = {1,0}; actual.frame.own.actor.spawn_group = 17;
    actual.frame.group.value.lifecycle = Lifecycle::respawning;
    actual.completion = {actual.frame.clock,17,true,false,true};
    record_completion(conversion,actual);
    const std::array<opportunities::Candidate,2> owned{{
        {17,Kind::none,reference::admission_eligible(conversion,17,11),reference::replacement_locked(conversion)},
        {18,Kind::inspection,reference::admission_eligible(conversion,18,11),false}}};
    check(conversion.empty_liquidation && owned[0].protected_incumbent && !owned[1].eligible &&
          opportunities::select(owned,fresh,11,better,1) == 0,
          "preferred new inspection cannot replace attributed owned gold conversion and renewal");
    auto renewal = reference::choose(conversion,actual);
    check(renewal.action == Action::move && conversion.active && conversion.empty_liquidation &&
          !conversion.bank_seen && !conversion.tribute_started,
          "protected selection continues the real reference renewal without fabricating a bank");
}
void last_approach_step_then_fresh_production() {
    namespace opportunities = runtime_opportunities;
    using Kind = opportunities::Kind;
    auto t = fixture(17,{1,0}); auto& actual = t->actual; auto& request = t->request;
    auto& own = actual.frame.own.actor; own.ready_attack_group = -1;
    ActorState state; opportunities::History history;
    const bool last_step = opportunities::defer_production_approach(true,false,1,17,
        own.ready_attack_group,own.spawn_group);
    // Match the runtime's real distinction: quote-deferred approach work is an
    // inspection, whereas exact-pad setup deferral is not an inspection result.
    const std::array<opportunities::Candidate,1> approaching{{
        {17,last_step?Kind::inspection:Kind::deferred,reference::admission_eligible(state,17,10),false}}};
    const int selected = opportunities::select(approaching,history,10,[](int,int){return false;});
    const int group = reference::resolve_work_group(state,17,17,10,[&](int wanted) {
        return selected == 0 && wanted == approaching[0].group &&
            opportunities::usable(approaching[0],history,0,10);
    });
    check(last_step && selected == 0 && group == 17 &&
          !opportunities::deferred(approaching[0]) && !state.active,
          "one remaining approach tile stays an unquoted inspection opportunity without project admission");
    check(!reference::choose_forecast_replacing_into(state,actual,nullptr) && !state.active,
          "unquoted approach selection grants no production attack or ownership");
    TurnBudget movement{actual.frame.clock,50000};
    ActionGuard step; step.clock = actual.frame.clock; step.available = step.safe = true;
    step.known_occupancy_legal = step.path_legal = step.landing_legal = true;
    check(movement.allows(Action::move,step),
          "current personal path and landing guard can authorize last inspection move without a production quote");
    const Frame before_move = actual.frame;
    movement.result(Action::move,Outcome::success);
    own.position = actual.frame.group.value.approach; actual.at_approach = true;
    actual.frame.clock = {11,1,movement.clock.revision};
    actual.frame.group.clock = actual.frame.clock; actual.solo = {};
    check(!opportunities::defer_production_approach(true,false,0,17,-1,-1),
          "observed exact-pad arrival ends quote deferral rather than treating adjacency as arrival");
    const opportunities::Candidate setup{17,Kind::deferred,true,false};
    check(opportunities::deferred(setup) &&
          !reference::choose_forecast_replacing_into(state,actual,nullptr) && !state.active,
          "exact-pad setup without a fresh quote yields instead of fabricating an inspection or attack");
    auto stale = std::make_unique<fc::EvaluatedQuote>();
    stale->evaluate(before_move,request);
    check(stale->result().status == Status::feasible &&
          !reference::choose_forecast_replacing_into(state,actual,stale.get()) && !state.active,
          "a feasible pre-move quote cannot replace the required current exact-pad proof");
    request.groups[0].observed = actual.frame.group;
    for (int i = 0; i < request.groups[0].member_count; ++i)
        request.groups[0].members[i].current.clock = actual.frame.clock;
    request.kit.clock = request.ambient_incoming.clock = request.bank_available.clock = actual.frame.clock;
    request.legs[0] = {own.position,actual.frame.group.value.approach,map(0)};
    auto fresh = std::make_unique<fc::EvaluatedQuote>(); fresh->evaluate(actual.frame,request);
    auto attack = reference::choose_forecast_replacing_into(state,actual,fresh.get());
    check(fresh->result().status == Status::feasible && attack && attack->action == Action::attack &&
          state.active && state.project.group == 17,
          "fresh exact-pad evaluated cycle reaches the real reference production caller");
}
void immediate_readiness_and_unavailable_sites() {
    namespace opportunities = runtime_opportunities;
    auto ranged = fixture(17,{1,0}); auto& actual = ranged->actual; auto& request = ranged->request;
    auto& own = actual.frame.own.actor; own.ranged = true;
    request.kit.value.ranged = true;
    check(!opportunities::defer_production_approach(true,false,1,17,own.ready_attack_group,-1) &&
          opportunities::defer_production_approach(true,false,1,17,18,-1) &&
          !opportunities::defer_production_approach(true,false,1,17,-1,17) &&
          opportunities::defer_production_approach(true,false,1,17,-1,18),
          "only exact current ready-attack or occupied-spawn group identity skips approach deferral");
    // The real adapter may rebase a ranged quote only after its complete fresh
    // basic mask proves every living member at this fixed firing position.
    actual.frame.group.value.approach = own.position; actual.at_approach = true;
    request.groups[0].observed = actual.frame.group;
    request.groups[0].stationary_basic = seen(true,actual.frame.clock,17);
    request.legs[0] = {own.position,own.position,map(0)};
    request.legs[1] = {own.position,request.groups[0].observed.value.spawn,map(2)};
    auto evaluated = std::make_unique<fc::EvaluatedQuote>(); evaluated->evaluate(actual.frame,request);
    ActorState state;
    auto attack = reference::choose_forecast_replacing_into(state,actual,evaluated.get());
    check(evaluated->result().status == Status::feasible && attack && attack->action == Action::attack &&
          !state.empty_liquidation,
          "current ranged stationary witness retains immediate fresh-quote basic execution off original pad");

    auto unavailable = fixture(); auto& missing = unavailable->actual;
    unavailable->request.groups[0].observed.value.lifecycle = Lifecycle::unknown;
    missing.frame.group = unavailable->request.groups[0].observed;
    auto unknown = std::make_unique<fc::EvaluatedQuote>();
    unknown->evaluate(missing.frame,unavailable->request); ActorState unknown_state;
    check(!opportunities::defer_production_approach(false,false,1,17,-1,-1) &&
          unknown->result().status != Status::feasible &&
          !reference::choose_forecast_replacing_into(unknown_state,missing,unknown.get()) &&
          missing.frame.group.value.lifecycle == Lifecycle::unknown && !unknown_state.active,
          "unknown-site approach handling cannot forge a living production quote");
    unavailable->request.groups[0].observed.value.lifecycle = Lifecycle::respawning;
    missing.frame.group = unavailable->request.groups[0].observed;
    auto respawning = std::make_unique<fc::EvaluatedQuote>();
    respawning->evaluate(missing.frame,unavailable->request); ActorState asleep;
    opportunities::History history;
    const std::array<opportunities::Candidate,1> unavailable_site{{{17,opportunities::Kind::none,true,false}}};
    check(!opportunities::defer_production_approach(true,true,1,17,-1,-1) &&
          opportunities::select(unavailable_site,history,10,[](int,int){return false;}) == -1 &&
          respawning->result().issue == fc::Issue::unavailable &&
          !reference::choose_forecast_replacing_into(asleep,missing,respawning.get()) && !asleep.active,
          "known respawning source stays unavailable rather than becoming deferred living inspection");
}
} // namespace
int main() {
    forecast_gold_only(); reject_initial_empty_banking(); actual_zero_bag_cycle();
    terminal_guard_and_failure_history();
    catalogue_ordering(); catalogue_to_actual_bootstrap();
    repriced_opportunity_preference(); preferred_gem_to_reference_caller();
    inspection_preference_keeps_admission_and_ownership();
    last_approach_step_then_fresh_production(); immediate_readiness_and_unavailable_sites();
    std::printf("zero-gem cycles: %d checks passed\n", checks);
}
