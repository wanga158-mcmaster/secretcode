#include "../common/pure.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <new>

using namespace plan4;

// These are SDK-free predicted M0 trajectories, not exact engine witnesses.
static unsigned allocations = 0;
void* operator new(std::size_t size) {
    ++allocations;
    if (void* result = std::malloc(size)) return result;
    throw std::bad_alloc();
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }

int metric(Point a, Point b, const void*) {
    return std::max(std::abs(a.x-b.x), std::abs(a.y-b.y));
}
struct RouteContext { int mode = 0; bool* permit = nullptr; };
int route(Point from, Point to, Point* output, int wanted, const void* opaque) {
    const auto* context = static_cast<const RouteContext*>(opaque);
    if (context && context->permit) *context->permit = false;
    if (context && context->mode == 1) return -1;
    for (int n = 0; n < wanted; ++n) {
        from.x += (to.x > from.x) - (to.x < from.x);
        from.y += (to.y > from.y) - (to.y < from.y);
        output[n] = from;
    }
    if (context && context->mode == 2 && wanted) output[0].x += 2;
    if (context && context->mode == 3 && wanted) output[0] = {0, 1};
    return wanted;
}
bool budget(const void* opaque) { return *static_cast<const bool*>(opaque); }

struct Fixture {
    std::unique_ptr<Information> source = std::make_unique<Information>();
    std::unique_ptr<Forecast> prefix = std::make_unique<Forecast>();
    std::unique_ptr<Forecast> output = std::make_unique<Forecast>();
    std::unique_ptr<Forecast> tail = std::make_unique<Forecast>();
    std::unique_ptr<SimulationState> state = std::make_unique<SimulationState>();
    std::unique_ptr<SimulationState> next = std::make_unique<SimulationState>();
    std::unique_ptr<SimulationState> saved = std::make_unique<SimulationState>();
    Menu menu{};
    PhaseCheckpoint meta{};
    Fixture() {
        auto& i = *source;
        i.own.id = 7; i.own.health = i.own.maximum_health = 1000;
        i.own.bag = 10; i.own.gold = 3; i.own.recall_safe = true;
        i.own_spawn = {9, 9}; i.travel = {metric, nullptr, route};
        i.settings.max_rounds = 40; i.settings.capture_rounds = 1;
        i.group_count = i.altar_count = 1;
        auto& g = i.groups[0];
        g.id = 10; g.position = {0, 0}; g.health = g.full_health = 100;
        g.clear_rounds = g.full_clear_rounds = 5; g.clear_damage_per_round = 20;
        g.incoming_damage = g.full_incoming_damage = 3;
        g.gems = 24; g.gold = 7; g.respawn_rounds = 200;
        g.lifecycle = Lifecycle::living;
        g.visible = g.all_spawns_visible = g.occupancy_known = true;
        g.provenance = g.health_provenance = {0, Source::personal_vision, Status::exact_now};
        i.altars[0].id = 20; i.altars[0].position = {4, 0};
        i.altars[0].altar_position = {4, 1};
        i.altars[0].accessible = i.altars[0].lock_known = true;
        menu.count = 4; menu.single_count = 1;
        menu.jobs[0] = {JobKind::farm, 0, -1, 0};
        menu.jobs[1].kind = JobKind::bank; menu.jobs[1].altar = 0;
        menu.jobs[2].kind = JobKind::renew;
        menu.jobs[3].kind = JobKind::hold;
    }
    bool cut(const Job& job, int round, const ForecastHooks& hooks = {}) {
        const unsigned before = allocations;
        const bool result = simulate_first_until_into(*source, job, menu, round, *prefix, *state, meta, hooks);
        assert(allocations == before);
        return result;
    }
};

int count(const Forecast& forecast, EventKind kind) {
    int result = 0;
    for (int n = 0; n < forecast.event_count; ++n) result += forecast.events[n].kind == kind;
    return result;
}
void same_event(const Event& a, const Event& b) {
    assert(a.kind == b.kind && a.start.round == b.start.round && a.start.phase == b.start.phase &&
        a.end.round == b.end.round && a.end.phase == b.end.phase && a.group_id == b.group_id &&
        a.bag_before == b.bag_before && a.bag_after == b.bag_after && a.secured == b.secured &&
        a.health_after == b.health_after && a.gold_after == b.gold_after && a.status == b.status);
}
void travel_cases(int& cases) {
    Fixture f; auto& i = *f.source;
    i.groups[0].position = {6, 2};
    assert(f.cut(f.menu.jobs[0], 3));
    assert(f.state->position == (Point{3, 2}) && f.state->now == 3 && f.state->deadline == 40 &&
        !f.state->stopped && !f.prefix->truncated && f.prefix->abort_reason == AbortReason::none &&
        f.meta.remaining == 3 && f.meta.phase == EventKind::travel && !f.meta.protected_progress &&
        f.state->health == 1000 && f.state->bag == 10 && f.state->gold == 3 &&
        f.state->groups[0].lifecycle == Lifecycle::living && f.prefix->our_secured == 0);
    assert(f.prefix->event_count == 1 && f.prefix->events[0].kind == EventKind::partial_travel &&
        f.prefix->events[0].end.phase == Phase::owner_action && f.prefix->events[0].status == Status::predicted); ++cases;
    i.travel.route_prefix = nullptr; assert(!f.cut(f.menu.jobs[0], 3)); ++cases;
    RouteContext context; i.travel = {metric, &context, route};
    for (int mode = 1; mode <= 3; ++mode) {
        context.mode = mode; assert(!f.cut(f.menu.jobs[0], 3)); ++cases;
    }
    context.mode = 0;
    assert(f.cut(f.menu.jobs[0], 6) && f.state->position == i.groups[0].position &&
        f.meta.phase == EventKind::clear && f.state->health == 1000 &&
        f.state->partial_health[0] == -1 && count(*f.prefix, EventKind::partial_travel) == 0); ++cases;
    assert(f.cut(f.menu.jobs[0], 0) && f.state->position == i.own.position &&
        f.prefix->event_count == 0 && f.state->health == i.own.health); ++cases;
    ForecastHooks delayed;
    delayed.phase_duration = [](EventKind kind, int, int nominal, int, int, const Information&, const void*, void*) {
        return nominal + (kind == EventKind::travel);
    };
    assert(!f.cut(f.menu.jobs[0], 3, delayed)); ++cases;
}
void clear_cases(int& cases) {
    Fixture f; auto& i = *f.source; auto& g = i.groups[0];
    assert(f.cut(f.menu.jobs[0], 2));
    // Five predicted attacks; two have occurred. The +2 incoming reserve is
    // charged now once, rather than prorating the completed event's HP loss.
    assert(f.state->partial_health[0] == 60 && f.state->clear_reserve_paid[0] == 1 &&
        f.state->health == 988 && f.state->bag == 10 && f.state->gold == 3 &&
        f.state->groups[0].lifecycle == Lifecycle::living && f.meta.remaining == 3 &&
        f.meta.phase == EventKind::clear && count(*f.prefix, EventKind::capture) == 0 &&
        f.prefix->events[f.prefix->event_count-1].health_after == 988); ++cases;
    g.clear_damage_per_round = 0; assert(!f.cut(f.menu.jobs[0], 2)); ++cases;
    g.clear_damage_per_round = 20; g.clear_rounds = 4; assert(!f.cut(f.menu.jobs[0], 2)); ++cases;
    g.clear_rounds = 5; g.clear_damage_per_round = std::numeric_limits<float>::quiet_NaN();
    assert(!f.cut(f.menu.jobs[0], 2)); ++cases;
    g.clear_damage_per_round = 20;
    assert(f.cut(f.menu.jobs[0], 5) && f.state->health == 979 && f.state->partial_health[0] == 0 &&
        f.state->groups[0].lifecycle == Lifecycle::cleared && f.state->bag == 10 &&
        count(*f.prefix, EventKind::capture) == 0); ++cases;
    ForecastHooks delayed;
    delayed.phase_duration = [](EventKind kind, int, int nominal, int, int, const Information&, const void*, void*) {
        return nominal + (kind == EventKind::clear);
    };
    assert(!f.cut(f.menu.jobs[0], 2, delayed)); ++cases;
}
void residual_and_tail(int& cases) {
    Fixture f; auto& i = *f.source; i.settings.max_rounds = 25;
    assert(f.cut(f.menu.jobs[0], 1));
    assert(f.state->health == 991 && f.state->partial_health[0] == 80);
    const unsigned before = allocations;
    assert(run_job_from_into(i, f.menu, *f.prefix, *f.state, f.menu.jobs[3], *f.output, *f.next));
    assert(allocations == before && f.next->now == 2 && f.next->health == 991 &&
        f.next->partial_health[0] == 80 && f.next->clear_reserve_paid[0] == 1);
    for (int n = 0; n < f.prefix->event_count; ++n) same_event(f.prefix->events[n], f.output->events[n]);
    int cursor = 0;
    const auto step = advance_fixed_into(i, f.menu, *f.output, *f.next, *f.tail, *f.saved, cursor);
    assert(step.valid && step.advanced && step.job_index == 0 && allocations == before &&
        f.tail->final_health == 979 && f.tail->our_secured == 34 && f.tail->final_bag == 0 &&
        f.tail->final_gold == 10 && count(*f.tail, EventKind::capture) == 1);
    for (int n = 0; n < f.tail->event_count; ++n) if (f.tail->events[n].kind == EventKind::clear)
        assert(f.tail->events[n].end.round-f.tail->events[n].start.round == 4);
    const auto end = advance_fixed_into(i, f.menu, *f.tail, *f.saved, *f.output, *f.next, cursor);
    assert(end.valid && allocations == before && count(*f.output, EventKind::capture) == 1 &&
        f.output->our_secured == 34 && f.output->final_gold == 10 && f.output->final_health == 979); ++cases;
}
void protected_cases(int& cases) {
    Fixture f; auto& i = *f.source; auto& g = i.groups[0];
    g.lifecycle = Lifecycle::cleared; g.reward_available = true; g.capture_started_round = 0;
    i.in_progress_group_id = g.id; i.settings.capture_rounds = 3;
    assert(f.cut(f.menu.jobs[3], 1) && f.meta.protected_progress && f.state->protected_progress &&
        f.meta.phase == EventKind::capture && f.state->bag == 10 && count(*f.prefix, EventKind::capture) == 0);
    assert(!run_job_from_into(i, f.menu, *f.prefix, *f.state, f.menu.jobs[3], *f.output, *f.next)); ++cases;
    assert(f.cut(f.menu.jobs[3], 3) && f.state->bag == 34 && f.state->gold == 10 &&
        f.state->groups[0].lifecycle == Lifecycle::captured && count(*f.prefix, EventKind::capture) == 1 &&
        f.prefix->events[f.prefix->event_count-1].end.phase == Phase::capture); ++cases;
    i.own.channel = Channel::tribute; i.own.channel_ticks = 3;
    assert(f.cut(f.menu.jobs[3], 1) && f.meta.protected_progress && f.state->protected_progress &&
        f.prefix->our_secured == 0 && f.state->bag == 10); ++cases;
    assert(f.cut(f.menu.jobs[3], 3) && f.prefix->our_secured == 34 && f.state->bag == 0 &&
        f.state->gold == 10 && count(*f.prefix, EventKind::capture) == 1 && count(*f.prefix, EventKind::bank) == 1);
    const auto& capture = f.prefix->events[0]; const auto& bank = f.prefix->events[f.prefix->event_count-1];
    assert(capture.kind == EventKind::capture && before(capture.end, bank.end) &&
        bank.end.round == 3 && bank.end.phase == Phase::owner_start && bank.secured == 34); ++cases;
    i.in_progress_group_id = -1; i.group_count = 0; i.own.channel_ticks = 4;
    assert(f.cut(f.menu.jobs[3], 2) && f.meta.channel == Channel::tribute &&
        f.meta.remaining == 2 && f.meta.protected_progress && f.state->bag == 10); ++cases;
    i.own.channel = Channel::recall; i.own.channel_ticks = 3; i.own.health = 500;
    assert(f.cut(f.menu.jobs[3], 2) && f.meta.channel == Channel::recall && f.meta.remaining == 1 &&
        f.state->protected_progress && f.state->position == i.own.position && f.state->health == 500); ++cases;
    assert(f.cut(f.menu.jobs[3], 3) && f.state->position == i.own_spawn && f.state->health == 500 &&
        count(*f.prefix, EventKind::recall_arrival) == 1 && count(*f.prefix, EventKind::heal) == 0 &&
        f.prefix->events[f.prefix->event_count-1].end.phase == Phase::owner_start); ++cases;
    assert(f.cut(f.menu.jobs[3], 4) && f.state->health == 700 && count(*f.prefix, EventKind::heal) == 1 &&
        f.prefix->events[f.prefix->event_count-1].end.round == 3 &&
        f.prefix->events[f.prefix->event_count-1].end.phase == Phase::owner_end); ++cases;
    i.own.channel = Channel::none; i.own.position = i.own_spawn; i.own.on_spawn = true;
    assert(f.cut(f.menu.jobs[2], 1) && f.state->health == 700 && f.meta.phase == EventKind::heal &&
        f.state->now == 1 && count(*f.prefix, EventKind::heal) == 1); ++cases;
}
void source_protection_cases(int& cases) {
    Fixture f; auto& i = *f.source;
    i.group_count = 0; i.own.channel_ticks = 3;
    for (Channel channel : {Channel::tribute, Channel::recall}) {
        i.own.channel = channel;
        assert(f.cut(f.menu.jobs[3], i.round) && f.meta.protected_progress &&
            f.state->protected_progress && f.meta.channel == channel &&
            f.meta.remaining == 3 && f.prefix->event_count == 0 &&
            f.state->position == i.own.position && f.state->bag == i.own.bag);
        assert(!run_job_from_into(i, f.menu, *f.prefix, *f.state, f.menu.jobs[3], *f.output, *f.next)); ++cases;
    }
    i.own.channel = Channel::none; i.group_count = 1; i.in_progress_group_id = 10;
    i.groups[0].lifecycle = Lifecycle::cleared; i.groups[0].reward_available = true;
    i.groups[0].capture_started_round = 0; i.settings.capture_rounds = 3;
    assert(f.cut(f.menu.jobs[3], i.round) && f.meta.protected_progress &&
        f.state->protected_progress && f.meta.phase == EventKind::capture &&
        f.state->bag == 10 && f.prefix->event_count == 0); ++cases;
}
void protected_horizon_cases(int& cases) {
    Fixture f; auto& i = *f.source; auto& g = i.groups[0];
    i.settings.max_rounds = 3; i.settings.capture_rounds = 6;
    i.in_progress_group_id = g.id; g.lifecycle = Lifecycle::cleared;
    g.reward_available = true; g.capture_started_round = 0;
    for (int cut : {1, 3}) {
        assert(f.cut(f.menu.jobs[3], cut) && f.meta.protected_progress &&
            f.meta.remaining == 6-cut && f.state->now == cut &&
            f.state->bag == 10 && f.state->gold == 3 &&
            f.state->groups[0].lifecycle == Lifecycle::cleared &&
            count(*f.prefix, EventKind::capture) == 0); ++cases;
    }
    simulate_first_into(i, f.menu.jobs[3], f.menu, *f.output, *f.next);
    assert(f.output->abort_reason == AbortReason::horizon && f.output->final_bag == 10 &&
        f.output->final_gold == 3 && count(*f.output, EventKind::capture) == 0); ++cases;
    i.in_progress_group_id = -1; i.group_count = 0;
    i.own.channel = Channel::tribute; i.own.channel_ticks = 6;
    for (int cut : {1, 3}) {
        assert(f.cut(f.menu.jobs[3], cut) && f.meta.protected_progress &&
            f.meta.channel == Channel::tribute && f.meta.remaining == 6-cut &&
            f.state->bag == 10 && f.prefix->our_secured == 0 &&
            count(*f.prefix, EventKind::bank) == 0); ++cases;
    }
    simulate_first_into(i, f.menu.jobs[3], f.menu, *f.output, *f.next);
    assert(f.output->abort_reason == AbortReason::horizon && f.output->final_bag == 10 &&
        f.output->our_secured == 0 && count(*f.output, EventKind::bank) == 0); ++cases;
    i.own.channel = Channel::recall; i.own.health = 500;
    assert(f.cut(f.menu.jobs[3], 2) && f.meta.protected_progress &&
        f.meta.channel == Channel::recall && f.meta.remaining == 4 &&
        f.state->position == i.own.position && f.state->health == 500); ++cases;
}
void transactional_cases(int& cases) {
    Fixture f; auto& i = *f.source;
    // Direct detail access only injects event-capacity boundaries. No padded
    // event or resulting state is claimed as an engine observation.
    {
        detail::Simulation simulation(i, f.menu, *f.output);
        simulation.pause_at(2); f.output->event_count = max_events-1;
        simulation.run_first(f.menu.jobs[0]); simulation.checkpoint(*f.next);
    }
    assert(f.output->abort_reason == AbortReason::event_capacity && f.next->now == 0 &&
        f.next->health == 1000 && f.next->partial_health[0] == -1 && f.next->clear_reserve_paid[0] == 0 &&
        f.next->bag == 10 && f.next->gold == 3 && f.next->groups[0].lifecycle == Lifecycle::living); ++cases;
    bool permit = true; i.work_budget = {budget, &permit};
    ForecastHooks hooks; hooks.path = &permit;
    hooks.full_clear_allowed = [](int, int, float, int, float, const Information&, const void*, void* path) {
        *static_cast<bool*>(path) = false; return true;
    };
    {
        detail::Simulation simulation(i, f.menu, *f.output, hooks);
        simulation.pause_at(2); simulation.run_first(f.menu.jobs[0]); simulation.checkpoint(*f.next);
    }
    assert(f.output->budget_exhausted && f.next->now == 0 && f.next->health == 1000 &&
        f.next->partial_health[0] == -1 && f.next->clear_reserve_paid[0] == 0 &&
        count(*f.output, EventKind::partial_clear) == 0 && f.next->bag == 10 && f.next->gold == 3); ++cases;
    permit = true; RouteContext context; context.permit = &permit;
    i.travel = {metric, &context, route}; i.groups[0].position = {6, 0};
    {
        detail::Simulation simulation(i, f.menu, *f.output);
        simulation.pause_at(2); simulation.run_first(f.menu.jobs[0]); simulation.checkpoint(*f.next);
    }
    assert(f.output->budget_exhausted && f.next->now == 0 && f.next->position == i.own.position &&
        count(*f.output, EventKind::partial_travel) == 0 && f.next->bag == 10); ++cases;
}
void rejection_cases(int& cases) {
    Fixture f; auto& i = *f.source;
    assert(f.cut(f.menu.jobs[0], 1)); *f.saved = *f.state;
    const auto rejected = [&] {
        assert(!run_job_from_into(i, f.menu, *f.prefix, *f.state, f.menu.jobs[3], *f.output, *f.next));
        *f.state = *f.saved; ++cases;
    };
    ++f.state->source_actor; rejected();
    ++f.state->source_round; rejected();
    ++f.state->layout_fingerprint; rejected();
    --f.state->deadline; rejected();
    f.state->health = std::numeric_limits<float>::infinity(); rejected();
    f.state->partial_health[0] = std::numeric_limits<float>::quiet_NaN(); rejected();
    f.state->clear_reserve_paid[0] = 2; rejected();
    f.state->lock_remaining[0] = 1; rejected();
    f.state->groups[0].lifecycle = static_cast<Lifecycle>(99); rejected();
    f.state->protected_progress = true; rejected();
    f.state->phased = false; rejected();
    ++i.own.id; rejected(); --i.own.id;
    Job absent; absent.kind = JobKind::hold; absent.menu_rank = 9;
    assert(!run_job_from_into(i, f.menu, *f.prefix, *f.state, absent, *f.output, *f.next)); ++cases;
    i.groups[0].position = {6, 0}; assert(f.cut(f.menu.jobs[0], 2));
    assert(!run_job_from_into(i, f.menu, *f.prefix, *f.state, f.menu.jobs[2], *f.output, *f.next)); ++cases;
    assert(!simulate_first_until_into(i, f.menu.jobs[0], f.menu, -1, *f.prefix, *f.state, f.meta)); ++cases;
    assert(!simulate_first_until_into(i, f.menu.jobs[0], f.menu, 41, *f.prefix, *f.state, f.meta)); ++cases;
}
int main() {
    int cases = 0;
    travel_cases(cases); clear_cases(cases); residual_and_tail(cases);
    protected_cases(cases); source_protection_cases(cases); protected_horizon_cases(cases);
    transactional_cases(cases); rejection_cases(cases);
    std::cout << "{\"schema\":\"plan4-phase-checkpoint-tests/1\",\"passed\":true,\"cases\":" << cases
        << ",\"trajectory\":\"predicted-M0\",\"engineWitnesses\":0,\"allocationsDuringExecution\":0}\n";
}
