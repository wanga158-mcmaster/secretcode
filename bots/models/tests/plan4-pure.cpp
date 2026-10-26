#include "../common/pure.h"
#define MODEL4_OBSERVER_NO_SDK
#include "../common/observer.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <cstring>

using namespace plan4;
namespace {
int checks = 0;
void check(bool result, const char* message) {
    ++checks;
    if (!result) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
int grid_distance(Point a, Point b, const void*) {
    return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y));
}
int no_distance(Point, Point, const void*) { return -1; }
void base(Information& i) {
    i = Information{};
    i.round = 10; i.own.id = 9; i.own.position = {0, 0}; i.own_spawn = {0, 0};
    i.own.health = 1000; i.own.maximum_health = 1000; i.own.gold = 7;
    i.own.recall_safe = true;
    i.settings.spawn_heal_fraction = 0.2f;
    i.travel.distance = grid_distance;
    i.group_count = 1;
    auto& g = i.groups[0];
    g.id = 101; g.position = {2, 0}; g.full_health = g.health = 100;
    g.clear_rounds = g.full_clear_rounds = 2; g.incoming_damage = g.full_incoming_damage = 10;
    g.gems = 20; g.gold = 12; g.respawn_rounds = 30;
    g.lifecycle = Lifecycle::living; g.visible = g.all_spawns_visible = true;
    g.reward_available = g.occupancy_known = true;
    g.provenance = {i.round, Source::personal_vision, Status::exact_now};
    g.health_provenance = g.provenance;
    i.altar_count = 1; i.altars[0].id = 1;
    i.altars[0].position = {5, 0}; i.altars[0].altar_position = {6, 0};
    i.altars[0].lock_known = true;
}
Job farm() { Job j{}; j.kind = JobKind::farm; j.group = 0; j.altar = 0; return j; }
Job bank() { Job j{}; j.kind = JobKind::bank; j.altar = 0; return j; }
Job hold() { Job j{}; j.kind = JobKind::hold; return j; }
Job renew() { Job j{}; j.kind = JobKind::renew; return j; }
int events(const Forecast& f, EventKind kind) {
    int count = 0;
    for (int n = 0; n < f.event_count; ++n) count += f.events[n].kind == kind;
    return count;
}
const Event* event(const Forecast& f, EventKind kind) {
    for (int n = 0; n < f.event_count; ++n) if (f.events[n].kind == kind) return &f.events[n];
    return nullptr;
}
void conservation(const Information& i, const Forecast& f) {
    unsigned acquired = 0, submitted = 0;
    for (int n = 0; n < f.event_count; ++n) {
        const auto& e = f.events[n];
        if (e.kind == EventKind::capture) acquired += e.bag_after - e.bag_before;
        if (e.kind == EventKind::bank) submitted += e.secured;
        if (before(e.end, e.start)) std::fprintf(stderr, "backwards kind=%d start=%d/%d end=%d/%d\n",
            int(e.kind), e.start.round, int(e.start.phase), e.end.round, int(e.end.phase));
        check(!before(e.end, e.start), "phase event cannot run backwards");
        if (n) check(!before(e.end, f.events[n - 1].end), "phase receipts ordered by completion");
    }
    check(i.own.bag + acquired == f.final_bag + submitted, "bag conservation across capture and tribute");
    check(submitted == f.our_secured, "submitted ledger equals secured objective");
}
[[gnu::noinline]] void timing_and_conservation() {
    auto fixture = std::make_unique<Information>(); auto& i = *fixture; base(i); i.own.bag = 3;
    const auto f = predict(i, farm(), Menu{});
    check(f.completed_conversion && f.our_secured == 23, "capture and existing bag convert once");
    check(f.first_conversion_round == 26, "travel-clear-capture initiation-countdown-bank phases");
    check(f.final_bag == 0 && f.final_gold == 19 && f.final_health == 960,
          "bag, gold and conservative health propagate");
    check(event(f, EventKind::capture_start)->end.round == 15, "entry waits for next capture phase");
    check(event(f, EventKind::capture)->end.round == 20, "five countdown phases after initiation");
    check(event(f, EventKind::bank)->end.phase == Phase::owner_start, "tribute resolves at later owner start");
    conservation(i, f);
    check(tribute_ticks(0, i.settings) == 0, "empty bag cannot start tribute");
    check(tribute_ticks(1, i.settings) == 1 && tribute_ticks(10, i.settings) == 1 &&
          tribute_ticks(11, i.settings) == 2, "tribute thresholds preserve double engine arithmetic");
    i.settings.tribute_multiplier = 1.0000000001;
    check(tribute_ticks(1, i.settings) == 2, "queried double multiplier cannot narrow across ceil boundary");
    i.settings.tribute_multiplier = 0.25; i.settings.tribute_base = 2;
    i.settings.tribute_minimum = 4;
    check(tribute_ticks(1, i.settings) == 4 && tribute_ticks(12, i.settings) == 5,
          "runtime tribute settings replace shorthand constants");
    check(capture_completion_round(10, 5) == 16, "capture initiation differs from countdown");
}
[[gnu::noinline]] void cutoff_and_terminal() {
    auto fixture = std::make_unique<Information>(); auto& i = *fixture; base(i); i.own.bag = 3; i.settings.max_rounds = 25;
    auto f = predict(i, farm(), Menu{});
    check(!f.completed_conversion && f.our_secured == 0 && f.final_bag == 23 && f.truncated,
          "straddling tribute earns no fractional secured reward");
    check(f.terminal_unknown, "max-round hidden tiebreak remains unknown");
    conservation(i, f);
    i.settings.max_rounds = 26;
    f = predict(i, farm(), Menu{});
    check(f.completed_conversion && f.first_conversion_round == 26, "cutoff includes its endpoint phase");
    i.settings.max_rounds = 2000; i.our_score = 980;
    const auto m = make_menu(i);
    f = predict(i, farm(), m);
    check(f.terminal_z == 1 && f.our_secured == 23 && f.macros_started == 1,
          "first threshold crossing stops future continuations");
    i.our_score = 1000; i.enemy_score = 1000;
    f = predict(i, farm(), m);
    check(f.terminal_z == 0 && f.terminal_unknown && f.macros_started == 0,
          "already unresolved crossing has neutral surrogate");
    i.our_score = 0;
    f = predict(i, farm(), m);
    check(f.terminal_z == -1 && f.our_secured == 0, "existing public enemy victory stops forecast");
}
[[gnu::noinline]] void renewal_boundaries() {
    auto fixture = std::make_unique<Information>(); auto& i = *fixture; base(i); i.round = 0; i.own.position = i.groups[0].position = {0, 0};
    i.groups[0].provenance.observed_round = i.groups[0].health_provenance.observed_round = 0;
    i.groups[0].gems = 24; i.groups[0].clear_rounds = i.groups[0].full_clear_rounds = 1;
    i.groups[0].incoming_damage = i.groups[0].full_incoming_damage = 0;
    // Off-spawn readiness waiting is serial with travel. With cooldown33,
    // this fixture's completed conversion recurrence is exactly50 rounds.
    i.groups[0].respawn_rounds = 33; i.settings.capture_rounds = 0;
    i.altars[0].position = {15, 0};
    Menu one{}; one.jobs[0] = farm(); one.count = one.single_count = 1;
    const int horizons[] = {19, 20, 69, 70};
    const unsigned expected[] = {0, 24, 24, 48};
    for (int n = 0; n < 4; ++n) {
        i.settings.max_rounds = horizons[n];
        const auto f = predict(i, farm(), one);
        check(f.our_secured == expected[n], "c20 p50 G24 renewal cutoff boundary");
        conservation(i, f);
    }
    base(i); i.own.bag = 10; i.groups[0].gems = 24;
    const auto first = predict(i, farm(), Menu{});
    check(first.our_secured == 34 && first.final_bag == 0, "starting10 plus solo24 converted exactly34");
    i.own.position = i.altars[0].position; i.own.bag = 0;
    const auto empty = predict(i, bank(), Menu{});
    check(empty.our_secured == 0 && events(empty, EventKind::bank_start) == 0,
          "subsequent empty bank cannot duplicate prior conversion");
}
[[gnu::noinline]] void information_and_safety() {
    auto fixture = std::make_unique<Information>(); auto& i = *fixture; base(i);
    i.groups[0].visible = false;
    auto f = predict(i, farm(), Menu{});
    check(f.stock_unknown && f.our_secured == 0, "public unseen group is scouting, no assured stock");
    base(i); i.groups[0].provenance.observed_round = i.round - 1;
    f = predict(i, farm(), Menu{});
    check(f.stock_unknown && f.our_secured == 0, "stale exact lifecycle is not current knowledge");
    base(i); i.groups[0].health_provenance.observed_round = i.round - 1;
    i.groups[0].clear_rounds = 0; i.groups[0].full_clear_rounds = 6;
    f = predict(i, farm(), Menu{});
    check(event(f, EventKind::clear)->end.round - event(f, EventKind::clear)->start.round == 6,
          "stale health uses full public health timing");
    base(i); i.groups[0].occupancy_known = false;
    f = predict(i, farm(), Menu{});
    check(f.stock_unknown && f.our_secured == 0, "unseen member-spawn occupancy cannot guarantee reward split");
    base(i); i.groups[0].contested = true;
    f = predict(i, farm(), Menu{});
    check(f.abort_reason == AbortReason::contested && f.our_secured == 0,
          "visible rival resets capture, not a delayed free reward");
    base(i); i.own.health = 260; i.groups[0].incoming_damage = 20;
    f = predict(i, farm(), Menu{});
    check(f.abort_reason == AbortReason::unsafe_clear && events(f, EventKind::clear) == 0,
          "common conservative full-clear admission applies");
    base(i); i.travel.distance = no_distance;
    f = predict(i, farm(), Menu{});
    check(f.travel_unknown && f.our_secured == 0, "unknown route cannot fabricate travel");
    base(i); i.own.alive = false;
    f = predict(i, farm(), Menu{});
    check(f.abort_reason == AbortReason::dead_actor && !f.macros_started, "dead actor cannot start macro");
}
[[gnu::noinline]] void capture_channels_and_recovery() {
    auto fixture = std::make_unique<Information>(); auto& i = *fixture; base(i); i.own.position = i.groups[0].position;
    i.in_progress_group_id = 101; i.groups[0].lifecycle = Lifecycle::cleared;
    i.groups[0].health = 0; i.groups[0].clear_rounds = 0;
    i.groups[0].capture_started_round = 8;
    auto f = predict(i, hold(), Menu{});
    check(f.final_round == 13 && f.final_bag == 20 && events(f, EventKind::capture_start) == 0,
          "uninterrupted own capture preserves witnessed countdown");
    conservation(i, f);
    i.own.bag = 3; i.own.channel = Channel::tribute; i.own.channel_ticks = 3;
    f = predict(i, hold(), Menu{});
    check(f.our_secured == 23 && f.first_conversion_round == 13,
          "capture before channel completion adds to entire current bag");
    check(event(f, EventKind::capture)->end.phase == Phase::capture &&
          event(f, EventKind::bank)->end.phase == Phase::owner_start,
          "same-round capture precedes tribute owner-start resolution");
    conservation(i, f);
    i.own.channel_ticks = 0; i.groups[0].capture_started_round = -1;
    i.in_progress_group_id = -1;
    f = predict(i, hold(), Menu{});
    check(f.first_conversion_round == i.round && f.our_secured == 3,
          "zero tribute timer resolves at current owner start");
    conservation(i, f);
    i.own.channel = Channel::none;
    f = predict(i, hold(), Menu{});
    check(!f.completed_conversion && f.final_bag == 3, "interrupted channel cannot complete from discarded history");
    base(i); i.own.position = {5, 5}; i.own.health = 600;
    f = predict(i, renew(), Menu{});
    check(f.final_round == 17 && f.final_health == 1000, "recall then two spawn healing ends");
    const auto* arrival = event(f, EventKind::recall_arrival);
    const auto* heal = event(f, EventKind::heal);
    check(arrival && heal && arrival->end.round == 15 && heal->end.round == 15 &&
          before(arrival->end, heal->end), "recall arrival heals at same round end");
    i.own.recall_safe = false;
    f = predict(i, renew(), Menu{});
    check(f.abort_reason == AbortReason::unsafe_recall && events(f, EventKind::recall_arrival) == 0,
          "unsafe recall is not a restoration shortcut");
    i.own.channel = Channel::recall; i.own.channel_ticks = 1;
    f = predict(i, hold(), Menu{});
    check(f.final_round == 13 && f.final_health == 1000, "preserved recall uses remaining legal ticks");
    i.own.health = 1000; i.own.channel_ticks = 4;
    f = predict(i, hold(), Menu{});
    check(f.final_round == 14 && event(f, EventKind::recall_arrival),
          "healthy ongoing recall still preserves remaining channel rather than leave early");
}
[[gnu::noinline]] void respawns_gold_and_split() {
    auto fixture = std::make_unique<Information>(); auto& i = *fixture; base(i); i.groups[0].lifecycle = Lifecycle::respawning;
    i.groups[0].reward_available = false; i.groups[0].respawn_remaining = 0;
    i.groups[0].occupied_spawn = true;
    auto f = predict(i, farm(), Menu{});
    check(f.stock_unknown && events(f, EventKind::capture) == 0, "occupied spawn delays whole-group renewal");
    base(i); i.groups[0].gems = 0;
    f = predict(i, farm(), Menu{});
    check(f.final_gold == 19 && f.no_conversion && f.our_secured == 0 &&
          events(f, EventKind::bank_start) == 0, "gold-only harvest has no fictitious empty tribute");
    conservation(i, f);
    base(i); i.groups[0].gems = 23; i.groups[0].eligible_allied_occupants = 3;
    f = predict(i, farm(), Menu{});
    check(f.our_secured == 9 && allocated_reward(23, 3, false) == 7,
          "lowest-ID remainder allocated without creating reward");
    check(allocated_reward(23, 3, true) + 2 * allocated_reward(23, 3, false) == 23,
          "capture split conserves total gems");
    base(i); i.own.position = i.groups[0].position;
    i.altars[0].position = i.groups[0].position; i.groups[0].respawn_rounds = 0;
    Menu one{}; one.jobs[0] = farm(); one.count = one.single_count = 1;
    f = predict(i, farm(), one);
    check(events(f, EventKind::capture) == 1, "remaining on spawn cannot harvest zero-cooldown duplicate stock");
    conservation(i, f);
}
[[gnu::noinline]] void menu_and_selector() {
    auto fixture = std::make_unique<Information>(); auto& i = *fixture; base(i); i.group_count = 9;
    for (int g = 1; g < i.group_count; ++g) {
        i.groups[g] = i.groups[0]; i.groups[g].id = 101 + g; i.groups[g].position = {2 + g, g};
    }
    i.altar_count = 2; i.altars[1] = i.altars[0];
    i.altars[1].id = 2; i.altars[1].position = {0, 5}; i.altars[1].altar_position = {0, 6};
    i.in_progress_group_id = i.groups[8].id;
    const auto m = make_menu(i);
    check(m.count == 16 && m.single_count == 8, "flat menu has eight singles four pairs four nonharvest");
    bool retained = false;
    int pairs = 0, banks = 0;
    for (int n = 0; n < m.count; ++n) {
        if (n < m.single_count) retained |= i.groups[m.jobs[n].group].id == i.in_progress_group_id;
        pairs += m.jobs[n].second_group >= 0;
        banks += m.jobs[n].kind == JobKind::bank;
        check(m.jobs[n].menu_rank == n, "common initial menu rank retained");
    }
    check(retained && pairs == 4 && banks == 2, "in-progress farm retained and pair/bank bounds honored");
    i.retained_group_id = i.groups[7].id; i.retained_second_group_id = i.groups[8].id;
    i.retained_altar_id = i.altars[1].id; i.in_progress_group_id = -1;
    const auto retained_menu = make_menu(i);
    bool first_retained = false, second_retained = false, pair_retained = false;
    for (int n = 0; n < retained_menu.count; ++n) {
        const auto& j = retained_menu.jobs[n];
        if (n < retained_menu.single_count) {
            first_retained |= i.groups[j.group].id == i.retained_group_id;
            second_retained |= i.groups[j.group].id == i.retained_second_group_id;
        }
        pair_retained |= j.group == 7 && j.second_group == 8 && j.altar == 1;
    }
    check(first_retained && second_retained && pair_retained && retained_menu.single_count == 8,
          "active pair retains both original camps and altar within common bounds");
    Forecast a{}, b{}; Job aj = farm(), bj = bank();
    a.our_secured = 1; b.our_secured = 100; a.terminal_z = 1;
    check(forecast_better(i, a, aj, b, bj), "terminal component outranks raw secured margin");
    a.terminal_z = 0; a.our_secured = b.our_secured;
    a.first_conversion_round = 20; b.first_conversion_round = 21;
    check(forecast_better(i, a, aj, b, bj), "earlier first conversion breaks equal margin");
    a.first_conversion_round = b.first_conversion_round; aj.nominal_travel = 1; bj.nominal_travel = 2;
    check(forecast_better(i, a, aj, b, bj), "nominal travel is common next tie break");
    base(i); i.own.health = 1000; i.groups[0].incoming_damage = 0; i.groups[0].respawn_rounds = 0;
    auto heap_e = std::make_unique<Evaluation>(); auto& e = *heap_e; evaluate_into(i, make_menu(i), e);
    check(e.chosen >= 0 && e.evaluated_count == e.menu.count, "complete offline evaluations choose common ranking");
    for (int n = 0; n < e.evaluated_count; ++n) {
        check(e.forecasts[n].macros_started <= 8, "all candidates obey eight-macro cap");
        check(e.forecasts[n].final_round <= i.round + 128, "all candidates obey 128-round horizon");
        conservation(i, e.forecasts[n]);
    }
}
struct Budget { mutable int remaining = 0; };
bool budget(const void* context) { return static_cast<const Budget*>(context)->remaining-- > 0; }
[[gnu::noinline]] void computation_budget() {
    auto fixture = std::make_unique<Information>(); auto& i = *fixture; base(i);
    const auto menu = make_menu(i); // Freeze the identical menu before timing candidates.
    Budget b{0}; i.work_budget = {budget, &b};
    auto heap_e = std::make_unique<Evaluation>(); auto& e = *heap_e; evaluate_into(i, menu, e);
    check(e.budget_exhausted && e.evaluated_count == 0 && e.chosen == -1,
          "exhausted reserve leaves selection to common bootstrap");
    b.remaining = 2;
    evaluate_into(i, menu, e);
    check(e.budget_exhausted && e.evaluated_count == 0 && e.chosen == -1,
          "partially evaluated candidate is never compared with complete candidates");
    Menu two{}; two.jobs[0] = bank(); two.jobs[1] = farm(); two.count = 2;
    i.own.bag = 3; b.remaining = 6;
    evaluate_into(i, two, e);
    check(e.budget_exhausted && e.evaluated_count == 1 && e.chosen == 0,
          "completed candidate retained when later candidate runs out of reserve");
}
// Frozen pre-v5 menu algorithm: independent behavioral oracle for the lookup-cache optimization.
[[gnu::noinline]] Menu make_menu_reference(const Information& i) {
    Menu m{};
    m.truncated = i.observation_truncated || i.public_layout_truncated ||
                  i.group_count > max_groups || i.altar_count > max_altars;
    auto single_less = [&](const Job& a, const Job& b) {
        if (a.nominal_completion != b.nominal_completion) return a.nominal_completion < b.nominal_completion;
        if (i.groups[a.group].gems != i.groups[b.group].gems)
            return i.groups[a.group].gems > i.groups[b.group].gems;
        return job_key_less(i, a, b);
    };
    auto single_job = [&](int g) {
        Job j{};
        j.kind = JobKind::farm; j.group = g;
        j.altar = nearest_altar(i, i.groups[g].position);
        if (i.groups[g].id == i.retained_group_id && i.retained_second_group_id < 0)
            for (int a = 0; a < std::min(i.altar_count, max_altars); ++a)
                if (i.altars[a].id == i.retained_altar_id && i.altars[a].accessible) j.altar = a;
        j.nominal_completion = nominal_farm_time(i, g, -1, j.altar, &j.nominal_travel);
        return j;
    };
    for (int g = 0; g < std::min(i.group_count, max_groups); ++g) {
        if (!can_refine(i)) { m.truncated = true; break; }
        Job j = single_job(g);
        if (j.nominal_completion < 0) continue; // unresolved terrain is not accessible
        int place = m.single_count;
        while (place > 0 && single_less(j, m.jobs[place - 1])) --place;
        if (place >= 8) continue;
        const int last = std::min(7, m.single_count);
        for (int p = last; p > place; --p) m.jobs[p] = m.jobs[p - 1];
        m.jobs[place] = j;
        m.single_count = std::min(8, m.single_count + 1);
    }
    const int retained_ids[2] = {
        i.retained_group_id >= 0 ? i.retained_group_id : i.in_progress_group_id,
        i.retained_second_group_id
    };
    int retained_indices[2] = {-1, -1};
    for (int r = 0; r < 2; ++r) {
        if (retained_ids[r] < 0) continue;
        bool present = false;
        for (int g = 0; g < std::min(i.group_count, max_groups); ++g)
            if (i.groups[g].id == retained_ids[r]) retained_indices[r] = g;
        for (int p = 0; p < m.single_count; ++p)
            present |= i.groups[m.jobs[p].group].id == retained_ids[r];
        if (!present && retained_indices[r] >= 0) {
            Job j = single_job(retained_indices[r]);
            if (j.nominal_completion >= 0) {
                int slot = m.single_count < 8 ? m.single_count++ : 7;
                // A second retained camp cannot evict the retained first camp.
                while (slot > 0 && r > 0 && i.groups[m.jobs[slot].group].id == retained_ids[0]) --slot;
                m.jobs[slot] = j;
            }
        }
    }
    m.count = m.single_count;
    std::array<Job, 4> pairs{};
    int pair_count = 0;
    auto pair_less = [&](const Job& a, const Job& b) {
        const std::uint64_t ar = std::uint64_t(i.groups[a.group].gems) + i.groups[a.second_group].gems;
        const std::uint64_t br = std::uint64_t(i.groups[b.group].gems) + i.groups[b.second_group].gems;
        const auto left = ar * std::uint64_t(b.nominal_completion);
        const auto right = br * std::uint64_t(a.nominal_completion);
        if (left != right) return left > right;
        if (a.nominal_completion != b.nominal_completion) return a.nominal_completion < b.nominal_completion;
        return job_key_less(i, a, b);
    };
    for (int a = 0; a < m.single_count; ++a) for (int b = 0; b < m.single_count; ++b) {
        if (a == b) continue;
        if (!can_refine(i)) { m.truncated = true; continue; }
        Job j{};
        j.kind = JobKind::farm; j.group = m.jobs[a].group; j.second_group = m.jobs[b].group;
        j.altar = nearest_altar(i, i.groups[j.second_group].position);
        j.nominal_completion = nominal_farm_time(i, j.group, j.second_group, j.altar, &j.nominal_travel);
        if (j.nominal_completion < 0) continue;
        int place = pair_count;
        while (place > 0 && pair_less(j, pairs[place - 1])) --place;
        if (place >= 4) continue;
        for (int p = std::min(3, pair_count); p > place; --p) pairs[p] = pairs[p - 1];
        pairs[place] = j; pair_count = std::min(4, pair_count + 1);
    }
    if (retained_indices[0] >= 0 && retained_indices[1] >= 0 && retained_indices[0] != retained_indices[1]) {
        int altar = nearest_altar(i, i.groups[retained_indices[1]].position);
        for (int a = 0; a < std::min(i.altar_count, max_altars); ++a)
            if (i.altars[a].id == i.retained_altar_id && i.altars[a].accessible) altar = a;
        Job retained{}; retained.kind = JobKind::farm;
        retained.group = retained_indices[0]; retained.second_group = retained_indices[1]; retained.altar = altar;
        retained.nominal_completion = nominal_farm_time(i, retained.group, retained.second_group,
                                                       altar, &retained.nominal_travel);
        bool present = false;
        for (int p = 0; p < pair_count; ++p)
            present |= pairs[p].group == retained.group && pairs[p].second_group == retained.second_group &&
                       pairs[p].altar == retained.altar;
        if (!present && retained.nominal_completion >= 0)
            pairs[pair_count < 4 ? pair_count++ : 3] = retained;
    }
    for (int p = 0; p < pair_count; ++p) m.jobs[m.count++] = pairs[p];
    std::array<Job, 2> banks{};
    int bank_count = 0;
    for (int a = 0; a < std::min(i.altar_count, max_altars); ++a) {
        if (!can_refine(i)) { m.truncated = true; break; }
        if (!i.altars[a].accessible) continue;
        const int d = distance(i, i.own.position, i.altars[a].position);
        if (d < 0) continue;
        Job j{}; j.kind = JobKind::bank; j.altar = a;
        j.nominal_travel = d; j.nominal_completion = d + tribute_ticks(i.own.bag, i.settings);
        int place = bank_count;
        while (place > 0 && (d < banks[place - 1].nominal_travel ||
            (d == banks[place - 1].nominal_travel && job_key_less(i, j, banks[place - 1])))) --place;
        if (place >= 2) continue;
        for (int p = std::min(1, bank_count); p > place; --p) banks[p] = banks[p - 1];
        banks[place] = j; bank_count = std::min(2, bank_count + 1);
    }
    for (int p = 0; p < bank_count; ++p) m.jobs[m.count++] = banks[p];
    Job renew{}; renew.kind = JobKind::renew; m.jobs[m.count++] = renew;
    Job hold{}; hold.kind = JobKind::hold; m.jobs[m.count++] = hold;
    for (int p = 0; p < m.count; ++p) m.jobs[p].menu_rank = p;
    return m;
}

struct CountedTravel {
    mutable int calls = 0;
    int unknown_mode = 0;
    std::array<Point, 8> tracked_points{};
    int tracked_count = 0;
    mutable std::array<int, 64> pair_visits{};
};
int counted_distance(Point from, Point to, const void* raw) {
    auto& counter = *static_cast<const CountedTravel*>(raw);
    ++counter.calls;
    int a = -1, b = -1;
    for (int n = 0; n < counter.tracked_count; ++n) {
        if (from == counter.tracked_points[n]) a = n;
        if (to == counter.tracked_points[n]) b = n;
    }
    if (a >= 0 && b >= 0) {
        ++counter.pair_visits[a * 8 + b];
        if (counter.unknown_mode == 4)
            return grid_distance(from, to, nullptr) + (a > b ? 9 : 0);
        if (counter.unknown_mode == 5 && a > b) return -1;
        if (counter.unknown_mode == 6 && (a + b) % 3 == 0) return -1;
    }
    if (counter.unknown_mode == 1 && (from.x == 7 || to.x == 7)) return -1;
    if (counter.unknown_mode == 2 && from == Point{0, 0} && to.x >= 10) return -1;
    if (counter.unknown_mode == 3 && from == Point{0, 0} && to.y < 8) return -1;
    return grid_distance(from, to, nullptr);
}
void compare_menus(const Menu& expected, const Menu& actual) {
    check(expected.count == actual.count && expected.single_count == actual.single_count &&
          expected.truncated == actual.truncated, "cached menu preserves old bounds, counts and truncation");
    if (expected.count != actual.count) return;
    for (int n = 0; n < actual.count; ++n) {
        const auto& a = actual.jobs[n]; const auto& b = expected.jobs[n];
        check(a.kind == b.kind && a.group == b.group && a.second_group == b.second_group &&
              a.altar == b.altar && a.menu_rank == b.menu_rank &&
              a.nominal_travel == b.nominal_travel && a.nominal_completion == b.nominal_completion,
              "cached menu preserves every original candidate byte field");
        static_assert(std::has_unique_object_representations<Job>::value,
                      "native oracle byte comparison requires padding-free job records");
        check(std::memcmp(&a, &b, sizeof(Job)) == 0, "cached candidate is byte-identical to old algorithm");
    }
}
[[gnu::noinline]] void cached_menu_equivalence() {
    auto fixture = std::make_unique<Information>(); auto& i = *fixture;
    CountedTravel counter{};
    for (int scenario = 0; scenario < 288; ++scenario) {
        base(i);
        counter.unknown_mode = (scenario / 2) % 3;
        i.travel = {counted_distance, &counter, nullptr,
                    scenario % 2 != 0 && counter.unknown_mode != 2};
        i.observation_truncated = scenario % 19 == 0;
        i.public_layout_truncated = scenario % 23 == 0;
        i.group_count = scenario % 14;
        for (int g = 0; g < i.group_count; ++g) {
            i.groups[g] = i.groups[0];
            auto& group = i.groups[g];
            group.id = 1000 + (g * 5) % 17;
            group.position = {g + 2, (g + scenario) % 5};
            group.gems = unsigned((g * 7 + scenario) % 37);
            group.lifecycle = Lifecycle((scenario + g) % 5);
            group.visible = (scenario + g) % 4 != 0;
            group.clear_rounds = (g + scenario) % 4;
            group.full_clear_rounds = (g + scenario) % 6 + 1;
            group.respawn_remaining = (scenario + g) % 3 ? 5 + g * 3 : -1;
            group.capture_started_round = group.lifecycle == Lifecycle::cleared ? i.round - 2 : -1;
        }
        i.altar_count = scenario % 5;
        for (int a = 0; a < i.altar_count; ++a) {
            i.altars[a] = i.altars[0]; i.altars[a].id = 30 + a;
            i.altars[a].position = {a * 3, 6 - a}; i.altars[a].altar_position = {a * 3 + 1, 6 - a};
            i.altars[a].accessible = (a + scenario) % 4 != 0;
        }
        if (i.group_count && scenario % 2) {
            i.retained_group_id = i.groups[i.group_count - 1].id;
            if (i.group_count > 1 && scenario % 3) i.retained_second_group_id = i.groups[i.group_count - 2].id;
            if (i.altar_count) i.retained_altar_id = i.altars[i.altar_count - 1].id;
        }
        if (i.group_count && scenario % 7 == 0) {
            i.own.position = i.groups[0].position;
            i.in_progress_group_id = i.groups[0].id;
        }
        compare_menus(make_menu_reference(i), make_menu(i));
    }
    base(i); i.travel = {counted_distance, &counter}; counter.unknown_mode = 0;
    i.group_count = 9; i.altar_count = 2;
    for (int g = 1; g < i.group_count; ++g) {
        i.groups[g] = i.groups[0]; i.groups[g].id = 101 + g; i.groups[g].position = {g + 2, g % 3};
    }
    i.altars[1] = i.altars[0]; i.altars[1].id = 2;
    i.altars[1].position = {0, 8}; i.altars[1].altar_position = {0, 9};
    counter.calls = 0;
    const auto old = make_menu_reference(i);
    const int old_calls = counter.calls;
    counter.calls = 0;
    const auto cached = make_menu(i);
    const int cached_calls = counter.calls;
    compare_menus(old, cached);
    check(cached_calls * 2 < old_calls, "menu cache removes more than half the repeated terrain lookups");
    std::printf("Menu terrain lookups: old=%d cached=%d; candidates byte-identical.\n", old_calls, cached_calls);
    for (int g = 0; g < i.group_count; ++g) i.groups[g].position = {g + 2, 1 + g % 3};
    i.altars[0].position = {0, 8}; i.altars[0].altar_position = {0, 9};
    i.altars[1].position = {15, 8}; i.altars[1].altar_position = {15, 9};
    counter.unknown_mode = 3; counter.calls = 0;
    const auto unknown_old = make_menu_reference(i);
    const int unknown_old_calls = counter.calls;
    counter.calls = 0;
    const auto unknown_cached = make_menu(i);
    const int unknown_cached_calls = counter.calls;
    compare_menus(unknown_old, unknown_cached);
    check(unknown_cached.single_count == 0 && unknown_cached_calls == i.group_count + i.altar_count &&
          unknown_cached_calls < unknown_old_calls,
          "unknown own routes reject singles before costly group-to-altar lookups");
    std::printf("Unknown-own-route lookups: old=%d cached=%d; candidates byte-identical.\n",
                unknown_old_calls, unknown_cached_calls);
    // A retained second camp must retain its full quote even when its direct
    // own route is unknown: the first camp can still lead to it legally.
    counter.unknown_mode = 2;
    i.retained_group_id = i.groups[0].id; i.retained_second_group_id = i.groups[8].id;
    i.retained_altar_id = i.altars[1].id;
    const auto retained_unknown = make_menu(i);
    compare_menus(make_menu_reference(i), retained_unknown);
    bool retained_pair = false;
    for (int n = 0; n < retained_unknown.count; ++n)
        retained_pair |= retained_unknown.jobs[n].group == 0 && retained_unknown.jobs[n].second_group == 8 &&
                         retained_unknown.jobs[n].altar == 1;
    check(retained_pair, "single shortcircuit cannot remove a retained pair with supported inter-camp access");
    auto output = std::make_unique<Evaluation>();
    for (int n = 0; n < max_jobs; ++n) output->forecasts[n].our_secured = unsigned(900 + n);
    Budget reserve{0}; i.work_budget = {budget, &reserve};
    evaluate_into(i, cached, *output);
    check(output->chosen == -1 && output->evaluated_count == 0 && output->budget_exhausted,
          "exhausted first candidate cannot select a reused stale forecast");
    for (int n = 0; n < max_jobs; ++n)
        check(output->forecasts[n].our_secured == unsigned(900 + n), "unevaluated output slots remain untouched");
    reserve.remaining = 2;
    evaluate_into(i, cached, *output);
    check(output->chosen == -1 && !output->evaluated_count && output->budget_exhausted &&
          output->forecasts[0].our_secured == 0 && output->forecasts[1].our_secured == 901,
          "only started output slot resets, and incomplete prefix cannot enter selection");
}
struct MenuBudgetTrace {
    int allowance = 0;
    bool recover = false;
    mutable int calls = 0;
    mutable std::array<bool, 128> responses{};
};
bool traced_menu_budget(const void* raw) {
    const auto& trace = *static_cast<const MenuBudgetTrace*>(raw);
    const int ordinal = trace.calls++;
    check(ordinal < int(trace.responses.size()), "menu budget trace remains bounded");
    return trace.responses[ordinal] = ordinal < trace.allowance || (trace.recover && ordinal % 3 == 0);
}
[[gnu::noinline]] void symmetric_menu_quotes() {
    auto fixture = std::make_unique<Information>(); auto& i = *fixture; base(i);
    CountedTravel counter{};
    i.travel = {counted_distance, &counter};
    check(!i.travel.symmetric_distance, "generic aggregate travel retains ordered distance semantics");
    i.group_count = counter.tracked_count = 8; i.altar_count = 2; i.own.bag = 3;
    i.settings.tribute_multiplier = 0.25; i.settings.tribute_base = 2; i.settings.tribute_minimum = 4;
    for (int g = 0; g < i.group_count; ++g) {
        i.groups[g] = i.groups[0]; i.groups[g].id = 800 - g;
        i.groups[g].position = counter.tracked_points[g] = {10 + g, 2};
    }
    i.altars[0].position = {30, 2}; i.altars[0].altar_position = {31, 2};
    i.altars[1] = i.altars[0]; i.altars[1].id = 2;
    i.altars[1].position = {0, 30}; i.altars[1].altar_position = {0, 31};
    auto reset_calls = [&] { counter.calls = 0; counter.pair_visits.fill(0); };
    auto pair_calls = [&] {
        int total = 0;
        for (int count : counter.pair_visits) total += count;
        return total;
    };
    for (int mode : {0, 6, 4, 5}) {
        counter.unknown_mode = mode; i.travel.symmetric_distance = false; i.work_budget = {};
        const auto reference = make_menu_reference(i);
        reset_calls();
        const auto ordered = make_menu(i);
        const int ordered_calls = counter.calls;
        compare_menus(reference, ordered);
        check(ordered.single_count == 8 && pair_calls() == 56,
              "generic eight-single menu quotes all 56 ordered camp routes");
        for (int a = 0; a < 8; ++a) for (int b = 0; b < 8; ++b)
            check(counter.pair_visits[a * 8 + b] == (a != b),
                  "asymmetric callback preserves each ordered pair independently");
        if (mode == 0) {
            for (int g = 0; g < 8; ++g)
                check(ordered.jobs[g].group == 7 - g &&
                      ordered.jobs[g].nominal_completion == ordered.jobs[0].nominal_completion,
                      "equal singles retain original identity tie breaking and menu rank");
        }
        if (mode == 0 || mode == 6) {
            i.travel.symmetric_distance = true;
            reset_calls();
            const auto symmetric = make_menu(i);
            compare_menus(reference, symmetric);
            check(pair_calls() == 28 && ordered_calls - counter.calls == 28,
                  "symmetric quote layer saves exactly 28 reverse distance callbacks");
            for (int a = 0; a < 8; ++a) for (int b = a + 1; b < 8; ++b)
                check(counter.pair_visits[a * 8 + b] + counter.pair_visits[b * 8 + a] == 1,
                      "symmetric positive and unknown pair routes are each quoted once");
        }
        const bool supports_symmetry = mode == 0 || mode == 6;
        for (int symmetric = 0; symmetric <= int(supports_symmetry); ++symmetric) {
            i.travel.symmetric_distance = symmetric != 0;
            for (bool recover : {false, true}) for (int allowance : {0, 1, 7, 8, 9, 16, 35, 63, 64, 65, 66}) {
                MenuBudgetTrace old_budget{allowance, recover}, new_budget{allowance, recover};
                i.work_budget = {traced_menu_budget, &old_budget};
                const auto old_menu = make_menu_reference(i);
                i.work_budget = {traced_menu_budget, &new_budget};
                const auto new_menu = make_menu(i);
                compare_menus(old_menu, new_menu);
                check(old_budget.calls == new_budget.calls && old_budget.responses == new_budget.responses,
                      "pair quote cache preserves the complete budget stop and recovery callback sequence");
                if (allowance == 66)
                    check(new_budget.calls == 66 && !new_menu.truncated,
                          "eight singles still check all 56 ordered jobs and both banks");
            }
        }
        i.work_budget = {};
    }
    std::printf("Symmetric pair-distance callbacks: known=56->28 unknown=56->28; ordered candidate bytes exact.\n");
    // Ordered respawn waits and a retained altar override remain direction specific
    // even when the underlying distance and tribute quote is shared.
    counter.unknown_mode = 6; i.travel.symmetric_distance = true;
    i.groups[7].lifecycle = Lifecycle::captured; i.groups[7].respawn_remaining = 150;
    i.groups[0].lifecycle = Lifecycle::respawning; i.groups[0].respawn_remaining = 200;
    i.retained_group_id = i.groups[7].id; i.retained_second_group_id = i.groups[0].id;
    i.retained_altar_id = i.altars[1].id;
    const auto retained = make_menu(i);
    compare_menus(make_menu_reference(i), retained);
    bool retained_pair = false;
    for (int n = 0; n < retained.count; ++n)
        retained_pair |= retained.jobs[n].group == 7 && retained.jobs[n].second_group == 0 &&
                         retained.jobs[n].altar == 1;
    check(retained_pair, "symmetric route reuse preserves ordered retained pair and altar override across respawns");
}
struct BridgeProvider {
    bool seen = true;
    Lifecycle lifecycle = Lifecycle::living;
    bool visible() const { return seen; }
    bool all_spawns_visible() const { return seen; }
    int member_count() const { return 1; }
    LifecycleObservation read_lifecycle() {
        check(seen, "observer bridge never queries a hidden lifecycle");
        return {lifecycle, -1};
    }
    MemberObservation public_member(int) const { return {100, 10, false}; }
    bool member_visible(int) const { return seen; }
    MemberObservation read_member(int) {
        check(seen, "observer bridge never queries a hidden member");
        return {100, 10, true};
    }
};
[[gnu::noinline]] void living_stock_observer_bridge() {
    auto fixture = std::make_unique<Information>(); auto& i = *fixture; base(i);
    const Group public_group = i.groups[0];
    BridgeProvider provider;
    i.groups[0] = observe_group(public_group, provider, i.round);
    check(i.groups[0].lifecycle == Lifecycle::living && !i.groups[0].reward_available &&
          i.groups[0].provenance.status == Status::exact_now,
          "real observer distinguishes living stock from immediately available capture reward");
    auto forecast = std::make_unique<Forecast>();
    predict_into(i, farm(), Menu{}, *forecast);
    check(forecast->our_secured == 20 && forecast->completed_conversion && !forecast->stock_unknown &&
          events(*forecast, EventKind::clear) == 1 && events(*forecast, EventKind::capture) == 1,
          "fresh exact living stock clears, captures and banks despite reward_available false");
    conservation(i, *forecast);
    i.groups[0].provenance.observed_round = i.round - 1;
    predict_into(i, farm(), Menu{}, *forecast);
    check(!forecast->our_secured && forecast->stock_unknown && !events(*forecast, EventKind::clear),
          "stale living lifecycle cannot assure a clear or reward");
    i.groups[0] = observe_group(public_group, provider, i.round);
    i.groups[0].provenance.status = Status::derived;
    predict_into(i, farm(), Menu{}, *forecast);
    check(!forecast->our_secured && forecast->stock_unknown, "derived living lifecycle is not a fresh exact stock observation");
    provider.seen = false;
    i.groups[0] = observe_group(public_group, provider, i.round);
    predict_into(i, farm(), Menu{}, *forecast);
    check(!forecast->our_secured && forecast->stock_unknown && !events(*forecast, EventKind::capture),
          "adapter-hidden group contributes no assured stock");
    provider.seen = true; provider.lifecycle = Lifecycle::unknown;
    i.groups[0] = observe_group(public_group, provider, i.round);
    predict_into(i, farm(), Menu{}, *forecast);
    check(!forecast->our_secured && forecast->stock_unknown, "adapter-unknown lifecycle contributes no assured stock");
    i.groups[0].visible = true; i.groups[0].provenance = {i.round, Source::personal_vision, Status::exact_now};
    i.groups[0].reward_available = true;
    predict_into(i, farm(), Menu{}, *forecast);
    check(!forecast->our_secured && forecast->stock_unknown, "inconsistent available flag cannot override an unknown lifecycle");
}
[[gnu::noinline]] void off_spawn_respawn_wait() {
    auto fixture = std::make_unique<Information>(); auto& i = *fixture; base(i);
    auto& group = i.groups[0];
    group.lifecycle = Lifecycle::respawning; group.reward_available = false;
    group.health = 0; group.clear_rounds = 0;
    group.respawn_remaining = 10; group.incoming_damage = 0;
    group.occupied_spawn = false;
    auto forecast = std::make_unique<Forecast>();
    predict_into(i, farm(), Menu{}, *forecast);
    const auto* waited = event(*forecast, EventKind::wait);
    const auto* respawned = event(*forecast, EventKind::respawn);
    const auto* arrived = event(*forecast, EventKind::travel);
    check(waited && waited->start.round == 10 && waited->end.round == 20 &&
          respawned && respawned->end.round == 20 && arrived && arrived->start.round == 20 && arrived->end.round == 22,
          "respawning camp is awaited off-spawn before travel can occupy its anchor");
    check(forecast->completed_conversion && forecast->first_conversion_round == 35,
          "conservative respawn forecast charges both full off-spawn wait and later travel");
    conservation(i, *forecast);
    i.settings.max_rounds = 34;
    predict_into(i, farm(), Menu{}, *forecast);
    check(!forecast->our_secured && forecast->truncated, "lost wait/travel overlap cannot credit a cutoff-straddling tribute");
    i.settings.max_rounds = 2000;
    i.own.position = group.position;
    predict_into(i, farm(), Menu{}, *forecast);
    check(forecast->stock_unknown && !events(*forecast, EventKind::respawn) && !forecast->our_secured,
          "starting on the target spawn cannot assure its respawn even if input occupancy flag is absent");
    base(i); i.groups[0].respawn_rounds = 30; i.groups[0].incoming_damage = i.groups[0].full_incoming_damage = 0;
    Menu one{}; one.jobs[0] = farm(); one.count = one.single_count = 1;
    predict_into(i, farm(), one, *forecast);
    check(events(*forecast, EventKind::capture) >= 2, "supported continuation still harvests renewed stock");
    int first_capture_round = -1, last_respawn_round = -1;
    for (int n = 0; n < forecast->event_count; ++n) {
        const auto& e = forecast->events[n];
        if (e.kind == EventKind::capture && first_capture_round < 0) first_capture_round = e.end.round;
        if (e.kind == EventKind::respawn) last_respawn_round = e.end.round;
        if (e.kind == EventKind::clear && last_respawn_round >= 0)
            check(e.start.round >= last_respawn_round && last_respawn_round >= first_capture_round + 30,
                  "continuation clear follows a supported respawn and full cooldown");
        if (e.kind == EventKind::travel && last_respawn_round >= 0 && e.end.round == last_respawn_round + 3)
            check(e.start.round >= last_respawn_round, "continuation return travel begins only after off-spawn readiness");
    }
    conservation(i, *forecast);
}
Event sentinel_event(int marker) {
    Event e;
    e.kind = EventKind::wait;
    e.start = {marker, Phase::owner_action}; e.end = {marker + 1, Phase::owner_action};
    e.group_id = marker; e.bag_before = e.bag_after = unsigned(marker);
    e.secured = e.gold_after = unsigned(marker); e.health_after = float(marker);
    e.status = Status::predicted;
    return e;
}
[[gnu::noinline]] void lazy_storage_and_reuse() {
    auto source = std::make_unique<Forecast>();
    auto destination = std::make_unique<Forecast>();
    for (int n = 0; n < max_events; ++n) destination->events[n] = sentinel_event(700 + n);
    destination->event_count = max_events;
    destination->our_secured = 99; destination->completed_conversion = true;
    destination->reset();
    check(destination->event_count == 0 && destination->our_secured == 0 && !destination->completed_conversion,
          "reuse resets output headers before any candidate comparison");
    for (int n = 0; n < max_events; ++n)
        check(destination->events[n].group_id == 700 + n, "reset leaves unused fixed event capacity untouched");
    source->event_count = 2; source->events[0] = sentinel_event(11); source->events[1] = sentinel_event(12);
    source->our_secured = 24; source->final_bag = 8; source->final_health = 90;
    source->terminal_unknown = true; source->stock_unknown = true; source->abort_reason = AbortReason::horizon;
    *destination = *source;
    check(destination->event_count == 2 && destination->events[0].group_id == 11 &&
          destination->events[1].group_id == 12, "prefix-only assignment copies all initialized events");
    check(destination->our_secured == 24 && destination->final_bag == 8 && destination->final_health == 90 &&
          destination->terminal_unknown && destination->stock_unknown && destination->abort_reason == AbortReason::horizon,
          "prefix-only assignment preserves forecast headers and uncertainty");
    for (int n = 2; n < max_events; ++n)
        check(destination->events[n].group_id == 700 + n, "copy cannot sweep or read unused source tail");
    const auto copied = std::make_unique<Forecast>(*source);
    check(copied->event_count == 2 && copied->events[1].group_id == 12 && copied->our_secured == 24,
          "return-value copy constructor keeps valid prefix semantics");
    Forecast& same = *destination;
    *destination = same;
    check(destination->events[1].group_id == 12, "prefix self-assignment is safe");
    source->reset();
    *destination = *source;
    check(!destination->event_count && !destination->our_secured && destination->events[1].group_id == 12,
          "empty copied forecast clears headers without reading an uninitialized tail");
    auto fixture = std::make_unique<Information>(); auto& i = *fixture; base(i);
    predict_into(i, farm(), Menu{}, *destination);
    check(destination->completed_conversion && destination->our_secured == 20, "caller-owned output receives complete forecast");
    i.groups[0].visible = false;
    predict_into(i, farm(), Menu{}, *destination);
    check(!destination->completed_conversion && destination->our_secured == 0 && destination->stock_unknown &&
          events(*destination, EventKind::bank) == 0, "reused forecast cannot retain earlier captured or secured stock");
    conservation(i, *destination);
}
} // namespace
int main() {
    timing_and_conservation(); cutoff_and_terminal(); renewal_boundaries(); information_and_safety();
    capture_channels_and_recovery(); respawns_gold_and_split(); menu_and_selector(); computation_budget(); lazy_storage_and_reuse(); cached_menu_equivalence(); symmetric_menu_quotes(); living_stock_observer_bridge(); off_spawn_respawn_wait();
    std::printf("Plan4 pure reference: %d checks passed; synthetic offline states, zero game launches.\n", checks);
}
