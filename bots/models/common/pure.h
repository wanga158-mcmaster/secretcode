#pragma once

// Plan #4's SDK-free reference. All dynamic facts arrive through Information;
// the only callback is a pure lookup over the observer's public terrain cache.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace plan4 {
inline constexpr int max_groups = 256;
inline constexpr int max_altars = 16;
inline constexpr int max_jobs = 16;
inline constexpr int max_macros = 8;
inline constexpr int max_horizon = 128;
inline constexpr int max_events = 128;
inline constexpr int unreachable = -1;
inline constexpr const char* reference_version = "plan4-m0/2";

struct Point { int x = 0, y = 0; };
inline bool operator==(Point a, Point b) { return a.x == b.x && a.y == b.y; }
inline bool operator!=(Point a, Point b) { return !(a == b); }
inline bool point_less(Point a, Point b) { return a.x != b.x ? a.x < b.x : a.y < b.y; }
enum class Status { exact_now, derived, predicted, unknown };
enum class Source { public_layout, personal_vision, own_state, own_occupancy, model };
struct Provenance {
    int observed_round = -1;
    Source source = Source::model;
    Status status = Status::unknown;
};
enum class Lifecycle { unknown, living, cleared, captured, respawning };
enum class Channel { none, recall, tribute, other };
struct Settings {
    int max_rounds = 2000;
    unsigned winning_gems = 1000;
    int capture_rounds = 5, recall_rounds = 5;
    double tribute_multiplier = 0.1;
    int tribute_base = 0;
    int tribute_minimum = 1;
    float spawn_heal_fraction = 0.2f;
};
struct Actor {
    int id = 0;
    Point position{};
    float health = 0, maximum_health = 0;
    unsigned bag = 0, gold = 0;
    Channel channel = Channel::none;
    int channel_ticks = 0;
    bool alive = true, on_spawn = false;
    bool recall_safe = false;
    int escape_rounds = 0;
    float escape_incoming = 0;
};
struct Group {
    int id = -1;
    Point position{};
    float full_health = 0, health = 0;
    int clear_rounds = -1, full_clear_rounds = -1;
    float incoming_damage = 0, full_incoming_damage = 0;
    unsigned gems = 0, gold = 0;
    int respawn_rounds = 0;
    Lifecycle lifecycle = Lifecycle::unknown;
    Provenance provenance{}, health_provenance{};
    bool visible = false, all_spawns_visible = false;
    bool reward_available = false, contested = false, occupied_spawn = false;
    bool occupancy_known = false;
    int capture_started_round = -1, respawn_remaining = -1;
    unsigned eligible_allied_occupants = 1;
    bool own_is_lowest_occupant = true;
    // Predicted ordinary-clear rate from the same queried kit as clear_rounds.
    // Zero leaves an unfinished combat trajectory unsupported.
    float clear_damage_per_round = 0;
};
struct Altar {
    int id = -1;
    Point position{}, altar_position{}; // legal approach tile, actual altar tile
    bool accessible = true, hostile_lock = false, lock_known = false;
    int remove_lock_rounds = -1; // fresh explicit ordinary attack quote only
};
struct TravelLookup {
    int (*distance)(Point, Point, const void*) = nullptr;
    const void* context = nullptr;
    // Return a read-only adjacent prefix of an already-proved shortest static
    // route, or -1 when no such proof is available. Intermediate distance-cache
    // entries are optional: the provider certifies the whole route itself.
    int (*route_prefix)(Point, Point, Point*, int, const void*) = nullptr;
    // Opt in only when both known distances and unknown results are symmetric.
    bool symmetric_distance = false;
};
struct WorkBudget {
    bool (*can_refine)(const void*) = nullptr;
    const void* context = nullptr;
};
struct Information {
    int round = 0;
    Actor own{};
    Point own_spawn{};
    unsigned our_score = 0, enemy_score = 0;
    Settings settings{};
    std::array<Group, max_groups> groups{};
    int group_count = 0;
    std::array<Altar, max_altars> altars{};
    int altar_count = 0;
    TravelLookup travel{};
    WorkBudget work_budget{};
    int in_progress_group_id = -1;
    // Executor macro retention is distinct from a witnessed capture countdown.
    int retained_group_id = -1, retained_second_group_id = -1, retained_altar_id = -1;
    bool observation_truncated = false;
    bool public_layout_truncated = false;
};
enum class JobKind { farm, bank, renew, hold };
struct Job {
    JobKind kind = JobKind::hold;
    int group = -1, second_group = -1, altar = -1; // Information array indices
    int menu_rank = 0, nominal_travel = 0, nominal_completion = 0;
};
struct Menu {
    std::array<Job, max_jobs> jobs{};
    int count = 0, single_count = 0;
    bool truncated = false;
};
inline int next_fixed_job(const Menu& menu, int now, int deadline, int macros_started, int& cursor,
                          bool (*available)(const Job&, const void*), const void* context) {
    if (!available || now >= deadline || macros_started < 0 || macros_started >= max_macros ||
        menu.count < 0 || menu.count > max_jobs || menu.single_count <= 0 || menu.single_count > menu.count ||
        cursor < 0 || cursor >= menu.single_count) return -1;
    for (int offset = 0; offset < menu.single_count; ++offset) {
        const int index = (cursor + offset) % menu.single_count;
        if (available(menu.jobs[index], context)) { cursor = (index + 1) % menu.single_count; return index; }
    }
    return -1;
}
enum class Phase { round_respawn, capture, camp_action, owner_start, owner_action, owner_end };
// Event storage is deliberately trivial: only its written prefix is live data.
// Every emitted Moment supplies both fields explicitly.
struct Moment { int round; Phase phase; };
inline bool before(Moment a, Moment b) {
    return a.round != b.round ? a.round < b.round : int(a.phase) < int(b.phase);
}
enum class EventKind { travel, clear, capture_start, capture, bank_start, bank,
                       escape, recall_start, recall_arrival, heal, wait, respawn,
                       remove_lock, abort, partial_travel, partial_clear, protected_wait };
struct PhaseCheckpoint {
    EventKind phase = EventKind::wait;
    int group = -1, remaining = 0;
    Channel channel = Channel::none;
    bool protected_progress = false, first_completed = false;
};
struct PhaseEntry {
    EventKind kind = EventKind::travel;
    int group = -1, altar = -1, now = 0, nominal = 0; // Information indices.
    Point from{}, to{};
    float health = 0;
    unsigned bag = 0;
    bool preserves_current_channel_or_capture = false;
};
struct EntryWait { int rounds = 0; bool supported = false, safe_wait = false; };

// Single-path model extensions. All callbacks receive only the same legal
// snapshot as M0. Per-path scratch belongs to the model wrapper and is reset
// for every candidate; observation updates never run during prediction.
struct ForecastHooks {
    const void* model = nullptr;
    void* path = nullptr;
    int (*phase_duration)(EventKind, int group, int nominal, int legal_minimum,
                          int now, const Information&, const void*, void*) = nullptr;
    int (*supply_wait)(int group, int now, int ordinary_service, int readiness_wait,
                       bool initially_ready, const Information&, const void*, void*) = nullptr;
    void (*captured)(int group, int now, const Information&, const void*, void*) = nullptr;
    // Read-only eligibility quote; continuation probing may call it repeatedly.
    bool (*acquisition_allowed)(int group, int now, const Information&, const void*, void*) = nullptr;
    // Initially inserted only before ordinary travel. Positive waits require
    // an explicit legal stationary zero-exposure proof at the physical origin.
    // A refiner must replace the whole candidate with M0 on unsupported_wait.
    EntryWait (*entry_wait)(const PhaseEntry&, const Information&, const void*, void*) = nullptr;
    // Optional shared executor admission. Active/control models using a
    // stricter ordinary safety adapter pass the identical predicate here.
    bool (*full_clear_allowed)(int group,int now,float health,int clear_rounds,float incoming,
                               const Information&,const void*,void*) = nullptr;
};
enum class AbortReason { none, unknown_stock, unknown_travel, unsafe_clear,
                         unsafe_recall, contested, inaccessible, no_tribute,
                         horizon, event_capacity, dead_actor, unsupported_channel, unsupported_wait };
struct Event {
    EventKind kind;
    Moment start, end;
    int group_id;
    unsigned bag_before, bag_after, secured;
    unsigned gold_after;
    float health_after;
    Status status;
};
static_assert(std::is_trivially_default_constructible<Event>::value,
              "unused event capacity must not run per-slot initialization");
struct ObjectiveSum {
    // Zero paths uses the ordinary physical forecast. A nonzero value is an
    // equally weighted summary formed only after each complete outcome path.
    int paths = 0, terminal_z = 0;
    std::uint64_t our_secured = 0, enemy_secured = 0, first_conversion_round = 0;
};
inline bool valid_objective(const ObjectiveSum& value) {
    return value.paths >= 1 && value.paths <= 8 && value.terminal_z >= -value.paths && value.terminal_z <= value.paths &&
        value.our_secured <= std::uint64_t(std::numeric_limits<unsigned>::max()) * unsigned(value.paths) &&
        value.enemy_secured <= std::uint64_t(std::numeric_limits<unsigned>::max()) * unsigned(value.paths) &&
        value.first_conversion_round <= std::uint64_t(std::numeric_limits<int>::max()) * unsigned(value.paths);
}
struct Forecast {
    std::array<Event, max_events> events;
    int event_count = 0, macros_started = 0, final_round = 0;
    int first_conversion_round = 0;
    unsigned our_secured = 0, enemy_secured = 0, final_bag = 0, final_gold = 0;
    float final_health = 0;
    int terminal_z = 0;
    bool terminal_unknown = false, stock_unknown = false, travel_unknown = false;
    bool lock_unknown = false;
    bool truncated = false, completed_conversion = false, no_conversion = false;
    bool budget_exhausted = false;
    AbortReason abort_reason = AbortReason::none;
    ObjectiveSum objective{};

    // A user-provided constructor also prevents value-initialization of the
    // entire event array when a caller writes Forecast{} or new Forecast().
    Forecast() noexcept {}
    void reset() noexcept {
        event_count = macros_started = final_round = first_conversion_round = 0;
        our_secured = enemy_secured = final_bag = final_gold = 0;
        final_health = 0;
        terminal_z = 0;
        terminal_unknown = stock_unknown = travel_unknown = lock_unknown = false;
        truncated = completed_conversion = no_conversion = budget_exhausted = false;
        abort_reason = AbortReason::none;
        objective = {};
    }
    Forecast(const Forecast& other) noexcept { copy_from(other); }
    Forecast& operator=(const Forecast& other) noexcept {
        if (this != &other) copy_from(other);
        return *this;
    }
private:
    void copy_from(const Forecast& other) noexcept {
        event_count = other.event_count;
        macros_started = other.macros_started; final_round = other.final_round;
        first_conversion_round = other.first_conversion_round;
        our_secured = other.our_secured; enemy_secured = other.enemy_secured;
        final_bag = other.final_bag; final_gold = other.final_gold;
        final_health = other.final_health; terminal_z = other.terminal_z;
        objective = other.objective;
        terminal_unknown = other.terminal_unknown; stock_unknown = other.stock_unknown;
        travel_unknown = other.travel_unknown; lock_unknown = other.lock_unknown;
        truncated = other.truncated; completed_conversion = other.completed_conversion;
        no_conversion = other.no_conversion; budget_exhausted = other.budget_exhausted;
        abort_reason = other.abort_reason;
        std::copy_n(other.events.begin(), event_count, events.begin());
    }
};
inline bool set_objective(Forecast& forecast, const ObjectiveSum& value) {
    if (!valid_objective(value)) return false;
    forecast.objective = value; return true;
}
struct ModelState {}; // M0 has no additional history beyond legal Information.
struct Evaluation {
    Menu menu{};
    std::array<Forecast, max_jobs> forecasts;
    int chosen = -1;
    int evaluated_count = 0;
    bool budget_exhausted = false;
    Evaluation() noexcept {} // Never sweep unused forecast event capacity.
};

inline int distance(const Information& i, Point a, Point b) {
    if (a == b) return 0;
    return i.travel.distance ? i.travel.distance(a, b, i.travel.context) : unreachable;
}
inline int horizon(const Information& i) {
    return std::max(0, std::min(max_horizon, i.settings.max_rounds - i.round));
}
inline bool can_refine(const Information& i) {
    return !i.work_budget.can_refine || i.work_budget.can_refine(i.work_budget.context);
}
inline int tribute_ticks(unsigned bag, const Settings& s) {
    if (!bag) return 0;
    return std::max(1, std::max(s.tribute_minimum,
        int(std::ceil(double(bag) * s.tribute_multiplier + s.tribute_base))));
}
inline int capture_completion_round(int occupancy_round, int capture_rounds) {
    // Entry happens after this round's capture phase. Next phase initiates;
    // the subsequent capture_rounds phases decrement to completion.
    return occupancy_round + 1 + std::max(0, capture_rounds);
}
inline unsigned allocated_reward(unsigned reward, unsigned occupants, bool lowest) {
    occupants = std::max(1u, occupants);
    return reward / occupants + (lowest ? reward % occupants : 0u);
}
inline bool affordable(float hp, float maximum, float incoming, int clear_rounds) {
    if (clear_rounds < 0 || hp <= 0 || maximum <= 0) return false;
    if (incoming <= 0) return true;
    return hp - incoming * float(clear_rounds + 2) >= maximum * 0.25f;
}
inline int nominal_clear(const Group& g, int round = -1) {
    if (g.lifecycle == Lifecycle::captured || g.lifecycle == Lifecycle::respawning) return g.full_clear_rounds;
    if (g.visible && g.lifecycle == Lifecycle::cleared && g.provenance.status == Status::exact_now &&
        (round < 0 || g.provenance.observed_round == round)) return 0;
    return g.visible && g.health_provenance.status == Status::exact_now &&
        (round < 0 || g.health_provenance.observed_round == round)
        ? g.clear_rounds : g.full_clear_rounds;
}
inline int nearest_altar(const Information& i, Point from) {
    int best = -1, best_distance = std::numeric_limits<int>::max();
    for (int a = 0; a < std::min(i.altar_count, max_altars); ++a) {
        if (!i.altars[a].accessible) continue;
        const int d = distance(i, from, i.altars[a].position);
        if (d < 0) continue;
        if (d < best_distance || (d == best_distance &&
            (best < 0 || point_less(i.altars[a].altar_position, i.altars[best].altar_position)))) {
            best = a; best_distance = d;
        }
    }
    return best;
}
inline bool job_key_less(const Information& i, const Job& a, const Job& b) {
    if (a.kind != b.kind) return int(a.kind) < int(b.kind);
    const int ag = a.group < 0 ? -1 : i.groups[a.group].id;
    const int bg = b.group < 0 ? -1 : i.groups[b.group].id;
    if (ag != bg) return ag < bg;
    const int ah = a.second_group < 0 ? -1 : i.groups[a.second_group].id;
    const int bh = b.second_group < 0 ? -1 : i.groups[b.second_group].id;
    if (ah != bh) return ah < bh;
    if (a.altar < 0 || b.altar < 0) return a.altar < b.altar;
    const Point ap = i.altars[a.altar].altar_position, bp = i.altars[b.altar].altar_position;
    return ap != bp ? point_less(ap, bp) : i.altars[a.altar].id < i.altars[b.altar].id;
}

inline int nominal_farm_time(const Information& i, int g, int h, int altar,
                             int* travel_out = nullptr) {
    const Group& first = i.groups[g];
    const int to_first = distance(i, i.own.position, first.position);
    if (to_first < 0 || altar < 0 || nominal_clear(first, i.round) < 0) return -1;
    int travel = to_first;
    int time = to_first + nominal_clear(first, i.round) + i.settings.capture_rounds + 1;
    if (first.lifecycle == Lifecycle::cleared && first.visible) {
        time -= nominal_clear(first, i.round);
        if (first.capture_started_round >= 0 && to_first == 0)
            time = std::max(0, first.capture_started_round + i.settings.capture_rounds - i.round);
    }
    if (first.lifecycle == Lifecycle::respawning && first.respawn_remaining >= 0)
        time += std::max(0, first.respawn_remaining - to_first);
    unsigned reward = first.gems;
    Point end = first.position;
    if (h >= 0) {
        const Group& second = i.groups[h];
        const int between = distance(i, end, second.position);
        if (between < 0 || nominal_clear(second, i.round) < 0) return -1;
        travel += between;
        if ((second.lifecycle == Lifecycle::captured || second.lifecycle == Lifecycle::respawning) &&
            second.respawn_remaining >= 0)
            time += std::max(0, second.respawn_remaining - (time + between));
        time += between + nominal_clear(second, i.round) + i.settings.capture_rounds + 1;
        reward += second.gems;
        end = second.position;
    }
    const int to_altar = distance(i, end, i.altars[altar].position);
    if (to_altar < 0) return -1;
    travel += to_altar;
    time += to_altar + tribute_ticks(i.own.bag + reward, i.settings);
    if (travel_out) *travel_out = travel;
    return std::max(1, time);
}

namespace detail {
// Menu-only nominal pieces. Each public distance is looked up once per scanned
// group; the final eight rows are reused across all 56 ordered pairs.
struct MenuPiece {
    Job single{};
    int own_travel = -1, clear = -1, before_bank = -1;
    int nearest_altar = -1, nearest_distance = -1, selected_distance = -1;
};
inline MenuPiece menu_piece(const Information& i, int g, int own_travel, int clear) {
    MenuPiece p;
    const Group& group = i.groups[g];
    p.single.kind = JobKind::farm; p.single.group = g;
    p.own_travel = own_travel;
    p.clear = clear;
    int best_distance = std::numeric_limits<int>::max();
    int override_altar = -1, override_distance = -1;
    for (int a = 0; a < std::min(i.altar_count, max_altars); ++a) {
        if (!i.altars[a].accessible) continue;
        const int d = distance(i, group.position, i.altars[a].position);
        if (group.id == i.retained_group_id && i.retained_second_group_id < 0 &&
            i.altars[a].id == i.retained_altar_id) {
            override_altar = a; override_distance = d;
        }
        if (d < 0) continue;
        if (d < best_distance || (d == best_distance &&
            (p.nearest_altar < 0 || point_less(i.altars[a].altar_position,
                                               i.altars[p.nearest_altar].altar_position)))) {
            p.nearest_altar = a; p.nearest_distance = d; best_distance = d;
        }
    }
    p.single.altar = override_altar >= 0 ? override_altar : p.nearest_altar;
    p.selected_distance = override_altar >= 0 ? override_distance : p.nearest_distance;
    if (p.own_travel >= 0 && p.clear >= 0) {
        p.before_bank = p.own_travel + p.clear + i.settings.capture_rounds + 1;
        if (group.lifecycle == Lifecycle::cleared && group.visible) {
            p.before_bank -= p.clear;
            if (group.capture_started_round >= 0 && p.own_travel == 0)
                p.before_bank = std::max(0, group.capture_started_round + i.settings.capture_rounds - i.round);
        }
        if (group.lifecycle == Lifecycle::respawning && group.respawn_remaining >= 0)
            p.before_bank += std::max(0, group.respawn_remaining - p.own_travel);
    }
    p.single.nominal_completion = -1;
    if (p.before_bank >= 0 && p.single.altar >= 0 && p.selected_distance >= 0) {
        p.single.nominal_travel = p.own_travel + p.selected_distance;
        p.single.nominal_completion = std::max(1, p.before_bank + p.selected_distance +
            tribute_ticks(i.own.bag + group.gems, i.settings));
    }
    return p;
}
inline MenuPiece menu_piece(const Information& i, int g) {
    return menu_piece(i, g, distance(i, i.own.position, i.groups[g].position),
                      nominal_clear(i.groups[g], i.round));
}
inline Job menu_pair(const Information& i, const MenuPiece& first, const MenuPiece& second,
                     int between, int altar, int to_altar, int bank_ticks = -1) {
    Job j{}; j.kind = JobKind::farm;
    j.group = first.single.group; j.second_group = second.single.group; j.altar = altar;
    j.nominal_completion = -1;
    if (first.before_bank < 0 || between < 0 || second.clear < 0 || altar < 0 || to_altar < 0) return j;
    int time = first.before_bank;
    const Group& second_group = i.groups[j.second_group];
    if ((second_group.lifecycle == Lifecycle::captured || second_group.lifecycle == Lifecycle::respawning) &&
        second_group.respawn_remaining >= 0)
        time += std::max(0, second_group.respawn_remaining - (time + between));
    time += between + second.clear + i.settings.capture_rounds + 1;
    const unsigned reward = i.groups[j.group].gems + second_group.gems;
    time += to_altar + (bank_ticks >= 0 ? bank_ticks : tribute_ticks(i.own.bag + reward, i.settings));
    j.nominal_completion = std::max(1, time);
    j.nominal_travel = first.own_travel + between + to_altar;
    return j;
}
} // namespace detail

inline Menu make_menu(const Information& i) {
    Menu m{};
    std::array<detail::MenuPiece, 8> pieces;
    m.truncated = i.observation_truncated || i.public_layout_truncated || i.group_count > max_groups || i.altar_count > max_altars;
    auto single_less = [&](const Job& a, const Job& b) {
        if (a.nominal_completion != b.nominal_completion) return a.nominal_completion < b.nominal_completion;
        if (i.groups[a.group].gems != i.groups[b.group].gems)
            return i.groups[a.group].gems > i.groups[b.group].gems;
        return job_key_less(i, a, b);
    };
    for (int g = 0; g < std::min(i.group_count, max_groups); ++g) {
        if (!can_refine(i)) { m.truncated = true; break; }
        const int own_travel = distance(i, i.own.position, i.groups[g].position);
        const int clear = nominal_clear(i.groups[g], i.round);
        // Unknown arrival or service cannot admit this single. Retained pairs
        // below still use the full quote: their second camp may be reachable
        // from the first despite an unknown direct own-to-second distance.
        if (own_travel < 0 || clear < 0) continue;
        const auto piece = detail::menu_piece(i, g, own_travel, clear);
        const Job& j = piece.single;
        if (j.nominal_completion < 0) continue; // unresolved terrain is not accessible
        int place = m.single_count;
        while (place > 0 && single_less(j, m.jobs[place - 1])) --place;
        if (place >= 8) continue;
        const int last = std::min(7, m.single_count);
        for (int p = last; p > place; --p) { m.jobs[p] = m.jobs[p - 1]; pieces[p] = pieces[p - 1]; }
        m.jobs[place] = j;
        pieces[place] = piece;
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
            const auto piece = detail::menu_piece(i, retained_indices[r]);
            const Job& j = piece.single;
            if (j.nominal_completion >= 0) {
                int slot = m.single_count < 8 ? m.single_count++ : 7;
                // A second retained camp cannot evict the retained first camp.
                while (slot > 0 && r > 0 && i.groups[m.jobs[slot].group].id == retained_ids[0]) --slot;
                m.jobs[slot] = j;
                pieces[slot] = piece;
            }
        }
    }
    m.count = m.single_count;
    std::array<Job, 4> pairs{};
    struct PairQuote { int distance, bank_ticks; };
    std::array<PairQuote, 64> between;
    std::array<bool, 64> between_known{};
    auto pair_quote = [&](int a, int b) -> const PairQuote& {
        const int key = i.travel.symmetric_distance && b < a ? b * 8 + a : a * 8 + b;
        if (!between_known[key]) {
            auto& quote = between[key];
            quote.distance = distance(i, i.groups[m.jobs[a].group].position, i.groups[m.jobs[b].group].position);
            quote.bank_ticks = quote.distance < 0 ? -1 : tribute_ticks(i.own.bag +
                i.groups[m.jobs[a].group].gems + i.groups[m.jobs[b].group].gems, i.settings);
            between_known[key] = true;
        }
        return between[key];
    };
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
        const auto& quote = pair_quote(a, b);
        const Job j = detail::menu_pair(i, pieces[a], pieces[b], quote.distance,
                                       pieces[b].nearest_altar, pieces[b].nearest_distance, quote.bank_ticks);
        if (j.nominal_completion < 0) continue;
        int place = pair_count;
        while (place > 0 && pair_less(j, pairs[place - 1])) --place;
        if (place >= 4) continue;
        for (int p = std::min(3, pair_count); p > place; --p) pairs[p] = pairs[p - 1];
        pairs[place] = j; pair_count = std::min(4, pair_count + 1);
    }
    if (retained_indices[0] >= 0 && retained_indices[1] >= 0 && retained_indices[0] != retained_indices[1]) {
        auto retained_piece = [&](int g) {
            for (int p = 0; p < m.single_count; ++p) if (m.jobs[p].group == g) return pieces[p];
            return detail::menu_piece(i, g);
        };
        const auto first = retained_piece(retained_indices[0]);
        const auto second = retained_piece(retained_indices[1]);
        int altar = second.nearest_altar;
        for (int a = 0; a < std::min(i.altar_count, max_altars); ++a)
            if (i.altars[a].id == i.retained_altar_id && i.altars[a].accessible) altar = a;
        int a_slot = -1, b_slot = -1;
        for (int p = 0; p < m.single_count; ++p) {
            if (m.jobs[p].group == retained_indices[0]) a_slot = p;
            if (m.jobs[p].group == retained_indices[1]) b_slot = p;
        }
        const PairQuote* quote = a_slot >= 0 && b_slot >= 0 ? &pair_quote(a_slot, b_slot) : nullptr;
        const int d = quote ? quote->distance :
            distance(i, i.groups[retained_indices[0]].position, i.groups[retained_indices[1]].position);
        const int to_altar = altar == second.nearest_altar ? second.nearest_distance : altar < 0 ? -1 :
            distance(i, i.groups[retained_indices[1]].position, i.altars[altar].position);
        const Job retained = detail::menu_pair(i, first, second, d, altar, to_altar, quote ? quote->bank_ticks : -1);
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

namespace detail {
struct GroupState {
    Lifecycle lifecycle;
    int ready_round;
    bool reward_known, occupied_by_us;
};
struct SimulationState {
    // Only group_count entries are live. Caller owns this on the heap.
    std::array<GroupState, max_groups> groups;
    int group_count = 0;
    int source_actor = -1, source_round = -1;
    std::uint64_t layout_fingerprint = 0;
    Point position{};
    float health = 0;
    unsigned bag = 0, gold = 0;
    int now = 0, deadline = 0;
    bool stopped = false;
    // Only a phase-aware run writes/reads these live prefixes. Ordinary M0
    // construction does not initialize unused capacity.
    std::array<float, max_groups> partial_health;
    std::array<unsigned char, max_groups> clear_reserve_paid;
    std::array<int, max_altars> lock_remaining;
    bool phased = false, protected_progress = false;
};
class Simulation {
    const Information& i;
    const Menu& menu;
    std::array<GroupState, max_groups> groups;
    Point position{};
    float hp = 0;
    unsigned bag = 0, gold = 0;
    int now = 0, deadline = 0;
    bool stopped = false;
    Forecast& f;
    ForecastHooks hooks;
    std::array<float, max_groups> partial_health;
    std::array<unsigned char, max_groups> clear_reserve_paid;
    std::array<int, max_altars> lock_remaining;
    bool phased = false, paused = false, unsupported_prefix = false;
    bool protected_now = false, protected_before_cut = false;
    int cut_round = -1;
    PhaseCheckpoint cut{};

    bool pause(EventKind kind, int group, int remaining, Channel channel = Channel::none,
               bool protecting = false) {
        paused = true; protected_now = protecting;
        cut = {kind, group, std::max(0, remaining), channel,
            protecting || protected_before_cut, false};
        return false;
    }
    bool phase_entry(EventKind kind, int group = -1, int remaining = 0) {
        if (cut_round >= 0 && now >= cut_round) {
            if (now==i.round && f.macros_started==0) {
                if (i.own.channel!=Channel::none)
                    return pause(i.own.channel==Channel::tribute?EventKind::bank:EventKind::recall_arrival,
                        -1,std::max(0,i.own.channel_ticks),i.own.channel,true);
                for (int g=0;g<std::min(i.group_count,max_groups);++g)
                    if (i.groups[g].id==i.in_progress_group_id && groups[g].lifecycle==Lifecycle::cleared && groups[g].reward_known)
                        return pause(EventKind::capture,g,std::max(0,i.settings.capture_rounds+1),Channel::none,true);
            }
            return pause(kind, group, remaining);
        }
        return true;
    }
    bool partial_travel(Point target, int nominal) {
        const int elapsed = cut_round - now;
        if (elapsed <= 0) return pause(EventKind::travel, -1, nominal);
        if (!i.travel.route_prefix || elapsed > max_horizon || !event_space()) {
            unsupported_prefix = true; return false;
        }
        std::array<Point, max_horizon> route;
        const int count = i.travel.route_prefix(position, target, route.data(), elapsed, i.travel.context);
        if (!budget() || count != elapsed) { unsupported_prefix = true; return false; }
        Point previous = position;
        for (int n = 0; n < count; ++n) {
            if ((n % 8) == 0 && !budget()) return false;
            const auto next = route[n];
            const int d = distance(i, next, target);
            if (next.x < 0 || next.y < 0 || next == previous ||
                std::max(std::abs(next.x-previous.x), std::abs(next.y-previous.y)) != 1 ||
                (d >= 0 && d != nominal-n-1)) { unsupported_prefix = true; return false; }
            previous = next;
        }
        const int start = now;
        if (!emit(EventKind::partial_travel, {start, Phase::owner_action},
                  {cut_round, Phase::owner_action}, -1, bag)) return false;
        for (int g = 0; g < std::min(i.group_count,max_groups); ++g) if (groups[g].occupied_by_us) {
            groups[g].occupied_by_us = false;
            groups[g].ready_round = std::max(groups[g].ready_round,start+1);
        }
        position = previous; now = cut_round;
        return pause(EventKind::travel,-1,nominal-elapsed);
    }

    std::uint64_t layout_fingerprint() const {
        std::uint64_t value = 1469598103934665603ull;
        auto mix = [&](std::uint64_t n) { value = (value ^ n) * 1099511628211ull; };
        // Evolved body/kit values belong to SimulationState. Starting scores
        // and spawn identity remain the original decision's public frame.
        mix(i.our_score); mix(i.enemy_score);
        mix(unsigned(i.own.position.x)); mix(unsigned(i.own.position.y)); mix(i.own.on_spawn);
        mix(unsigned(i.own_spawn.x)); mix(unsigned(i.own_spawn.y));
        mix(unsigned(i.group_count)); mix(unsigned(i.altar_count));
        for (int g = 0; g < std::min(i.group_count, max_groups); ++g) {
            const auto& group = i.groups[g];
            mix(unsigned(group.id)); mix(unsigned(group.position.x)); mix(unsigned(group.position.y));
            mix(group.gems); mix(group.gold); mix(unsigned(group.respawn_rounds));
        }
        for (int a = 0; a < std::min(i.altar_count, max_altars); ++a) {
            const auto& altar = i.altars[a];
            mix(unsigned(altar.id)); mix(unsigned(altar.position.x)); mix(unsigned(altar.position.y));
            mix(unsigned(altar.altar_position.x)); mix(unsigned(altar.altar_position.y));
        }
        mix(unsigned(i.settings.max_rounds)); mix(i.settings.winning_gems);
        mix(unsigned(i.settings.capture_rounds)); mix(unsigned(i.settings.recall_rounds));
        mix(unsigned(i.settings.tribute_base)); mix(unsigned(i.settings.tribute_minimum));
        std::uint64_t fraction;
        static_assert(sizeof(fraction) == sizeof(i.settings.tribute_multiplier));
        __builtin_memcpy(&fraction, &i.settings.tribute_multiplier, sizeof(fraction)); mix(fraction);
        return value;
    }

    int duration(EventKind kind, int group, int nominal, int legal_minimum) const {
        if (!hooks.phase_duration) return nominal;
        const int quoted = hooks.phase_duration(kind, group, nominal, legal_minimum, now, i, hooks.model, hooks.path);
        return std::max(legal_minimum, quoted);
    }

    bool budget() {
        if (!i.work_budget.can_refine || i.work_budget.can_refine(i.work_budget.context)) return true;
        f.budget_exhausted = true; f.truncated = true; stopped = true; return false;
    }

    bool event_space(int needed = 1) {
        if (f.event_count < 0 || needed > max_events - f.event_count) {
            f.truncated = true; f.abort_reason = AbortReason::event_capacity; stopped = true; return false;
        }
        return true;
    }
    bool emit(EventKind kind, Moment start, Moment end, int group = -1,
              unsigned before_bag = 0, unsigned secured = 0) {
        if (!event_space()) return false;
        Event e; e.kind = kind; e.start = start; e.end = end;
        e.group_id = group < 0 ? -1 : i.groups[group].id;
        e.bag_before = before_bag; e.bag_after = bag; e.secured = secured; e.health_after = hp;
        e.gold_after = gold; e.status = Status::predicted;
        f.events[f.event_count++] = e;
        return true;
    }
    bool advance(int duration, EventKind kind, int group = -1,
                 Phase end_phase = Phase::owner_action) {
        if (!budget() || !event_space()) return false;
        duration = std::max(0, duration);
        if (!phase_entry(kind,group,duration)) return false;
        if (cut_round >= 0 && duration > cut_round-now) {
            if (kind == EventKind::escape) { unsupported_prefix = true; return false; }
            const bool channel = kind == EventKind::recall_arrival;
            const int start = now, elapsed = cut_round-now;
            if (!emit(channel ? EventKind::protected_wait : EventKind::wait,
                    {start,Phase::owner_action},{cut_round,Phase::owner_action},group,bag)) return false;
            now = cut_round;
            return pause(kind,group,duration-elapsed,channel?Channel::recall:Channel::none,channel);
        }
        if (duration > deadline - now) {
            f.truncated = true; f.abort_reason = AbortReason::horizon; stopped = true;
            now = deadline; return false;
        }
        const int start = now;
        now += duration;
        if (cut_round == now && duration > 0 && kind == EventKind::recall_arrival)
            protected_before_cut = true;
        return emit(kind, {start, Phase::owner_action}, {now, end_phase}, group, bag);
    }
    bool abort(AbortReason reason, int group = -1) {
        if (f.abort_reason == AbortReason::none) f.abort_reason = reason;
        if (reason == AbortReason::unknown_stock) f.stock_unknown = true;
        if (reason == AbortReason::unknown_travel) f.travel_unknown = true;
        // Observing a failed premise and selecting another macro costs an owner turn.
        advance(1, EventKind::abort, group);
        return false;
    }
    bool travel(Point target, int group = -1, int altar = -1) {
        if (!phase_entry(EventKind::travel,group)) return false;
        const int d = distance(i, position, target);
        if (d < 0) return abort(AbortReason::unknown_travel);
        if (hooks.entry_wait) {
            if (!budget()) return false;
            const auto wait = hooks.entry_wait({EventKind::travel,group,altar,now,d,position,target,hp,bag,false},
                i,hooks.model,hooks.path);
            if (!budget()) return false;
            if (!wait.supported || wait.rounds < 0 || (wait.rounds > 0 && !wait.safe_wait))
                return abort(AbortReason::unsupported_wait,group);
            if (wait.rounds > 0 && !advance(wait.rounds,EventKind::wait,group)) return false;
        }
        const int departure = now;
        const bool leaving = position != target;
        const int travel_duration = duration(EventKind::travel,-1,d,d);
        if (cut_round >= 0 && travel_duration > cut_round-now) {
            if (travel_duration != d) { unsupported_prefix = true; return false; }
            return partial_travel(target,d);
        }
        if (!advance(travel_duration, EventKind::travel)) return false;
        if (leaving) {
            for (int g = 0; g < std::min(i.group_count, max_groups); ++g)
                if (groups[g].occupied_by_us) {
                    groups[g].occupied_by_us = false;
                    groups[g].ready_round = std::max(groups[g].ready_round, departure + 1);
                }
        }
        position = target;
        return true;
    }
    void terminal() {
        if (i.our_score >= i.settings.winning_gems && i.enemy_score >= i.settings.winning_gems) {
            f.terminal_unknown = true; f.terminal_z = 0; stopped = true;
        } else if (i.enemy_score >= i.settings.winning_gems) {
            f.terminal_z = -1; stopped = true;
        } else if (std::uint64_t(i.our_score) + f.our_secured >= i.settings.winning_gems) {
            f.terminal_z = 1; stopped = true;
        }
    }
    bool capture(int g, bool current_witness = false) {
        if (!budget()) return false;
        if (!phase_entry(EventKind::capture,g)) return false;
        const Group& source = i.groups[g];
        GroupState& state = groups[g];
        if (source.contested) return abort(AbortReason::contested, g);
        if (!source.occupancy_known) return abort(AbortReason::unknown_stock, g);
        const int start = now;
        int completion = capture_completion_round(now, i.settings.capture_rounds);
        if (current_witness && source.capture_started_round >= 0 && position == source.position)
            completion = std::max(now, source.capture_started_round + i.settings.capture_rounds);
        const int nominal = completion - now;
        const int corrected = duration(EventKind::capture, g, nominal, nominal);
        const bool needs_start = !(current_witness && source.capture_started_round >= 0);
        if (cut_round >= 0 && corrected > cut_round-now) {
            if (!event_space(needs_start ? 2 : 1)) return false;
            if (needs_start && !emit(EventKind::capture_start,{start,Phase::owner_action},
                    {start+1,Phase::capture},g,bag)) return false;
            if (!emit(EventKind::protected_wait,{start,Phase::owner_action},
                    {cut_round,Phase::owner_action},g,bag)) return false;
            const int remaining = corrected-(cut_round-now);now = cut_round;
            return pause(EventKind::capture,g,remaining,Channel::none,true);
        }
        if (corrected > deadline - now) {
            f.truncated = true; f.abort_reason = AbortReason::horizon; stopped = true; now = deadline; return false;
        }
        completion = now + corrected;
        // Capture is one transaction: no reward or lifecycle mutation can be
        // published without room for its complete represented event prefix.
        if (!event_space(needs_start ? 2 : 1)) return false;
        if (needs_start && !emit(EventKind::capture_start, {start, Phase::owner_action}, {start + 1, Phase::capture}, g, bag)) return false;
        const unsigned before_bag = bag;
        bag += allocated_reward(source.gems, source.eligible_allied_occupants, source.own_is_lowest_occupant);
        gold += allocated_reward(source.gold, source.eligible_allied_occupants, source.own_is_lowest_occupant);
        now = completion;
        if (cut_round == now && start < now) protected_before_cut = true;
        state.lifecycle = Lifecycle::captured;
        state.reward_known = true;
        state.ready_round = now + std::max(0, source.respawn_rounds);
        state.occupied_by_us = true;
        const bool emitted = emit(EventKind::capture, {start, Phase::owner_action}, {now, Phase::capture}, g, before_bag);
        if (emitted && hooks.captured) hooks.captured(g, now, i, hooks.model, hooks.path);
        return emitted;
    }
    bool harvest(int g) {
        if (g < 0 || g >= std::min(i.group_count, max_groups)) return abort(AbortReason::inaccessible);
        if (hooks.acquisition_allowed && !hooks.acquisition_allowed(g, now, i, hooks.model, hooks.path))
            return abort(AbortReason::unknown_stock, g);
        const Group& source = i.groups[g];
        GroupState& state = groups[g];
        const bool replenishing = state.lifecycle == Lifecycle::captured || state.lifecycle == Lifecycle::respawning;
        int readiness_wait = 0;
        if (replenishing) {
            if (state.ready_round < 0 || (source.occupied_spawn && !state.occupied_by_us))
                return abort(AbortReason::unknown_stock, g);
            if (state.occupied_by_us || position == source.position) return abort(AbortReason::unknown_stock, g);
            // The aggregate anchor becomes our position after travel. Waiting
            // there would itself block the camp's respawn. Wait off this spawn
            // first, then travel; this deliberately forgoes travel/wait overlap.
            // Occupancy identity is unavailable: an observed occupied spawn
            // remains unsupported even if our later route might have vacated it.
            readiness_wait = std::max(0, state.ready_round - now);
        }
        const bool initially_ready = source.visible && source.provenance.status == Status::exact_now &&
            source.provenance.observed_round == i.round &&
            (source.lifecycle == Lifecycle::living || (source.lifecycle == Lifecycle::cleared && source.reward_available));
        const int ordinary_service = std::max(0, state.lifecycle == Lifecycle::cleared ? 0 :
            state.ready_round < 0 ? source.clear_rounds : source.full_clear_rounds) + i.settings.capture_rounds + 1;
        const int supply_wait = hooks.supply_wait
            ? std::max(0, hooks.supply_wait(g, now, ordinary_service, readiness_wait, initially_ready, i, hooks.model, hooks.path)) : 0;
        const int wait = std::max(readiness_wait, supply_wait);
        if (wait > 0) {
            if (!advance(wait, EventKind::wait, g, Phase::round_respawn)) return false;
            if (replenishing && !emit(EventKind::respawn, {now, Phase::round_respawn}, {now, Phase::round_respawn}, g, bag)) return false;
        }
        if (replenishing) {
            state.lifecycle = Lifecycle::living;
            if (phased) {partial_health[g]=-1;clear_reserve_paid[g]=0;}
        }
        if (!travel(source.position,g)) return false;
        if (!state.reward_known) return abort(AbortReason::unknown_stock, g);
        if (source.contested) return abort(AbortReason::contested, g);
        const bool initial_clear = state.lifecycle == Lifecycle::cleared;
        if (!initial_clear) {
            int clear = source.full_clear_rounds;
            if (state.ready_round < 0 && source.visible && source.health_provenance.status == Status::exact_now &&
                source.health_provenance.observed_round == i.round)
                clear = source.clear_rounds;
            const bool residual = phased && partial_health[g] >= 0;
            if (residual) {
                if (!std::isfinite(source.clear_damage_per_round) || source.clear_damage_per_round <= 0) {
                    unsupported_prefix = true; return false;
                }
                clear = int(std::ceil(partial_health[g]/source.clear_damage_per_round));
            }
            clear = duration(EventKind::clear, g, clear, clear > 0 ? 1 : clear);
            if (!phase_entry(EventKind::clear,g,clear)) return false;
            const float incoming = state.ready_round < 0 ? source.incoming_damage : source.full_incoming_damage;
            if (hooks.full_clear_allowed) {
                const bool admitted=hooks.full_clear_allowed(g,now,hp,clear,incoming,i,hooks.model,hooks.path);
                if (!budget()) return false;
                if (!admitted) return abort(AbortReason::unsafe_clear,g);
            }
            const bool reserve_paid = phased && clear_reserve_paid[g];
            const float loss = incoming * float(clear + (reserve_paid ? 0 : 2));
            if (!(reserve_paid ? clear>=0 && hp>0 && i.own.maximum_health>0 &&
                    (incoming<=0 || hp-loss>=i.own.maximum_health*.25f) :
                    affordable(hp, i.own.maximum_health, incoming, clear)))
                return abort(AbortReason::unsafe_clear, g);
            if (cut_round >= 0 && clear > cut_round-now) {
                const float health = residual ? partial_health[g] : state.ready_round<0 && source.visible &&
                    source.health_provenance.status==Status::exact_now && source.health_provenance.observed_round==i.round
                    ? source.health : source.full_health;
                const float rate = source.clear_damage_per_round;
                if (!std::isfinite(rate) || rate<=0 || !std::isfinite(health) || health<=0 ||
                    !std::isfinite(incoming) || incoming<0 || std::ceil(health/rate)!=clear || !event_space()) {
                    unsupported_prefix = true; return false;
                }
                const int start=now, elapsed=cut_round-now;
                const float prefix_loss=incoming*float(elapsed+(reserve_paid?0:2));
                if (!emit(EventKind::partial_clear,{start,Phase::owner_action},
                        {cut_round,Phase::owner_action},g,bag)) return false;
                hp=std::max(0.0f,hp-prefix_loss);now=cut_round;
                f.events[f.event_count-1].health_after=hp;
                partial_health[g]=std::max(0.0f,health-rate*float(elapsed));clear_reserve_paid[g]=1;
                return pause(EventKind::clear,g,clear-elapsed);
            }
            if (!advance(clear, EventKind::clear, g)) return false;
            hp = std::max(0.0f, hp - loss);
            f.events[f.event_count - 1].health_after = hp;
            state.lifecycle = Lifecycle::cleared;
            if (phased) {partial_health[g]=0;clear_reserve_paid[g]=1;}
        }
        return capture(g, initial_clear && source.visible);
    }
    bool bank(int a) {
        if (!budget()) return false;
        if (!phase_entry(EventKind::bank)) return false;
        if (a < 0 || a >= std::min(i.altar_count, max_altars) || !i.altars[a].accessible)
            return abort(AbortReason::inaccessible);
        if (!travel(i.altars[a].position,-1,a)) return false;
        const Altar& altar = i.altars[a];
        f.lock_unknown |= !altar.lock_known;
        const int removal = phased ? lock_remaining[a] : altar.remove_lock_rounds;
        if (altar.hostile_lock && (!phased || removal!=0)) {
            if (altar.remove_lock_rounds <= 0) return abort(AbortReason::inaccessible);
            const int start=now;
            if (!advance(removal, EventKind::remove_lock)) {
                if (phased && paused) lock_remaining[a]=removal-(now-start);
                return false;
            }
            if (phased) lock_remaining[a]=0;
        }
        if (!bag) { f.no_conversion = true; return true; }
        if (!phase_entry(EventKind::bank)) return false;
        const int start = now;
        const int minimum_ticks = tribute_ticks(bag, i.settings);
        const int ticks = duration(EventKind::bank, -1, minimum_ticks, minimum_ticks);
        if (!event_space(2)) return false;
        if (!emit(EventKind::bank_start, {start, Phase::owner_action}, {start, Phase::owner_end}, -1, bag)) return false;
        if (cut_round >= 0 && ticks > cut_round-now) {
            if (!emit(EventKind::protected_wait,{start,Phase::owner_action},
                    {cut_round,Phase::owner_action},-1,bag)) return false;
            now=cut_round;
            return pause(EventKind::bank,-1,ticks-(now-start),Channel::tribute,true);
        }
        if (ticks > deadline - now) {
            f.truncated = true; f.abort_reason = AbortReason::horizon; stopped = true; now = deadline; return false;
        }
        const unsigned converted = bag;
        now += ticks;
        if (cut_round==now && ticks>0) protected_before_cut=true;
        bag = 0;
        f.our_secured += converted;
        if (!f.completed_conversion) f.first_conversion_round = now;
        f.completed_conversion = true;
        if (!emit(EventKind::bank, {start, Phase::owner_action}, {now, Phase::owner_start}, -1, converted, converted)) return false;
        terminal();
        return true;
    }
    bool renew(bool existing_recall = false) {
        if (!budget()) return false;
        if (!phase_entry(EventKind::heal)) return false;
        if (hp >= i.own.maximum_health && !existing_recall) return advance(1, EventKind::wait);
        const int restore_start = now;
        const bool on_spawn = position == i.own_spawn || (position == i.own.position && i.own.on_spawn);
        if (!on_spawn) {
            if (!existing_recall && !i.own.recall_safe) return abort(AbortReason::unsafe_recall);
            if (!existing_recall) {
                if (hp - i.own.escape_incoming * float(i.own.escape_rounds + 1) <= 0)
                    return abort(AbortReason::unsafe_recall);
                if (!advance(i.own.escape_rounds, EventKind::escape)) return false;
                hp -= i.own.escape_incoming * float(i.own.escape_rounds + 1);
                f.events[f.event_count - 1].health_after = hp;
                if (!emit(EventKind::recall_start, {now, Phase::owner_action}, {now, Phase::owner_end}, -1, bag)) return false;
            }
            const int ticks = existing_recall ? i.own.channel_ticks : i.settings.recall_rounds;
            if (!advance(std::max(0, ticks), EventKind::recall_arrival, -1, Phase::owner_start)) return false;
            position = i.own_spawn;
        }
        const float per_turn = i.own.maximum_health * i.settings.spawn_heal_fraction;
        if (per_turn <= 0 || i.own.maximum_health <= 0) return abort(AbortReason::unsafe_recall);
        const int healing_turns = std::max(0, int(std::ceil((i.own.maximum_health - hp) / per_turn)));
        for (int n = 0; n < healing_turns; ++n) {
            if (!phase_entry(EventKind::heal,-1,healing_turns-n)) return false;
            if (now >= deadline) {
                f.truncated = true; f.abort_reason = AbortReason::horizon; stopped = true; return false;
            }
            const int heal_round = now;
            if (!event_space()) return false;
            hp = std::min(i.own.maximum_health, hp + per_turn);
            if (!emit(EventKind::heal, {heal_round, Phase::owner_action}, {heal_round, Phase::owner_end}, -1, bag)) return false;
            ++now; // The arrival turn's end is the first healing opportunity.
        }
        const int nominal_restore = now - restore_start;
        const int corrected_restore = duration(EventKind::heal, -1, nominal_restore, nominal_restore);
        if (corrected_restore > nominal_restore && !advance(corrected_restore - nominal_restore, EventKind::wait)) return false;
        return true;
    }
    bool hold() {
        if (!budget()) return false;
        if (!phase_entry(EventKind::wait)) return false;
        if (f.macros_started == 1 && i.own.channel == Channel::tribute) {
            if (!bag) return abort(AbortReason::no_tribute);
            const int start = now;
            const int completion = now + std::max(0, i.own.channel_ticks);
            if (completion > deadline && cut_round < 0) {
                f.truncated = true; f.abort_reason = AbortReason::horizon; stopped = true; now = deadline; return false;
            }
            // Captures resolve before owner-start channels. An uninterrupted own
            // occupancy witness can add gems to the already-started tribute.
            for (int g = 0; g < std::min(i.group_count, max_groups); ++g) {
                const Group& source = i.groups[g];
                if (source.id == i.in_progress_group_id && position == source.position &&
                    groups[g].lifecycle == Lifecycle::cleared && groups[g].reward_known &&
                    source.capture_started_round >= 0 &&
                    source.capture_started_round + i.settings.capture_rounds <= completion) {
                    if (!capture(g, true)) return false;
                    break;
                }
            }
            if (cut_round>=0 && completion>cut_round) {
                if (!emit(EventKind::protected_wait,{now,Phase::owner_action},
                        {cut_round,Phase::owner_action},-1,bag)) return false;
                const int remaining=completion-cut_round;now=cut_round;
                return pause(EventKind::bank,-1,remaining,Channel::tribute,true);
            }
            if (completion > now && !advance(completion - now, EventKind::wait, -1, Phase::owner_start)) return false;
            if (!event_space()) return false;
            const unsigned converted = bag; bag = 0; f.our_secured += converted;
            if (cut_round==now && now>start) protected_before_cut=true;
            if (!f.completed_conversion) f.first_conversion_round = now;
            f.completed_conversion = true;
            if (!emit(EventKind::bank, {start, i.own.channel_ticks > 0 ? Phase::owner_action : Phase::owner_start},
                      {now, Phase::owner_start}, -1, converted, converted)) return false;
            terminal(); return true;
        }
        if (f.macros_started == 1 && i.own.channel == Channel::recall) return renew(true);
        if (f.macros_started == 1 && i.own.channel == Channel::other)
            return abort(AbortReason::unsupported_channel);
        if (f.macros_started == 1 && i.in_progress_group_id >= 0) {
            for (int g = 0; g < std::min(i.group_count, max_groups); ++g)
                if (i.groups[g].id == i.in_progress_group_id &&
                    groups[g].lifecycle == Lifecycle::cleared && groups[g].reward_known &&
                    position == i.groups[g].position) return capture(g, true);
        }
        return advance(1, EventKind::wait);
    }
    bool run_job(const Job& j) {
        if (!budget() || !event_space()) return false;
        if (!phase_entry(EventKind::wait)) return false;
        ++f.macros_started;
        if (j.kind == JobKind::farm) {
            if (!harvest(j.group)) return false;
            if (j.second_group >= 0 && !harvest(j.second_group)) return false;
            return bank(j.altar);
        }
        if (j.kind == JobKind::bank) return bank(j.altar);
        if (j.kind == JobKind::renew) return renew();
        return hold();
    }
    bool available(const Job& j) const {
        if (j.group < 0 || j.group >= std::min(i.group_count, max_groups)) return false;
        if (hooks.acquisition_allowed && !hooks.acquisition_allowed(j.group, now, i, hooks.model, hooks.path)) return false;
        const auto& state = groups[j.group];
        if (!state.reward_known || state.occupied_by_us || i.groups[j.group].contested) return false;
        if ((state.lifecycle == Lifecycle::captured || state.lifecycle == Lifecycle::respawning) && state.ready_round < 0) return false;
        return true;
    }
public:
    Simulation(const Information& info, const Menu& jobs, Forecast& out, ForecastHooks policy = {})
        : i(info), menu(jobs), f(out), hooks(policy) {
        f.reset();
        position = i.own.position; hp = i.own.health; bag = i.own.bag; gold = i.own.gold;
        now = i.round; deadline = now + horizon(i);
        f.first_conversion_round = deadline + 1;
        f.truncated = i.observation_truncated || i.public_layout_truncated || menu.truncated;
        for (int g = 0; g < std::min(i.group_count, max_groups); ++g) {
            const Group& source = i.groups[g];
            auto& state = groups[g];
            state = {Lifecycle::unknown, -1, false, false};
            const bool fresh = source.visible && source.provenance.observed_round == i.round &&
                source.provenance.status == Status::exact_now;
            state.lifecycle = fresh ? source.lifecycle : Lifecycle::unknown;
            // A living group's reward is not available for immediate capture,
            // but its freshly witnessed lifecycle supports one predicted clear.
            // A standalone flag must never turn an unknown lifecycle into stock.
            state.reward_known = fresh && (source.lifecycle == Lifecycle::living ||
                (source.lifecycle == Lifecycle::cleared && source.reward_available) ||
                source.lifecycle == Lifecycle::respawning || source.lifecycle == Lifecycle::captured);
            if (fresh && source.respawn_remaining >= 0)
                state.ready_round = i.round + source.respawn_remaining;
        }
    }
    void pause_at(int round) {
        phased=true;cut_round=round;
        for (int g=0;g<std::min(i.group_count,max_groups);++g) {
            partial_health[g]=-1;clear_reserve_paid[g]=0;
        }
        for (int a=0;a<std::min(i.altar_count,max_altars);++a)
            lock_remaining[a]=i.altars[a].hostile_lock?i.altars[a].remove_lock_rounds:0;
    }
    bool phase_checkpoint(PhaseCheckpoint& result) const {
        if (unsupported_prefix || now!=cut_round || f.budget_exhausted || f.abort_reason!=AbortReason::none) return false;
        result=cut;
        if (!paused) result={EventKind::wait,-1,0,Channel::none,protected_before_cut,true};
        return true;
    }
    void run_first(const Job& first) {
        terminal();
        if (!i.own.alive) { f.abort_reason = AbortReason::dead_actor; stopped = true; }
        if (!stopped && now < deadline) run_job(first);
        snapshot_result();
    }
    void finish_farm_after_first(const Job& first) {
        // Resume the same macro after its first capture. No new macro or
        // continuation cursor is consumed by the pair's remaining work.
        if (!stopped && now < deadline && (first.second_group < 0 || harvest(first.second_group))) bank(first.altar);
        snapshot_result();
    }
    int next_continuation(int& continuation) const {
        if (stopped) return -1;
        const auto supported = [](const Job& job, const void* context) {
            return static_cast<const Simulation*>(context)->available(job);
        };
        return next_fixed_job(menu, now, deadline, f.macros_started, continuation, supported, this);
    }
    void run_continuations(int continuation = 0) {
        while (!stopped && now < deadline && f.macros_started < max_macros && menu.single_count > 0) {
            const int selected = next_continuation(continuation);
            if (selected < 0) break;
            run_job(menu.jobs[selected]);
        }
        finish();
    }
    void snapshot_result() {
        f.final_round = now; f.final_bag = bag; f.final_health = hp; f.final_gold = gold;
    }
    void finish() {
        if (!stopped && f.macros_started >= max_macros && now < deadline) f.truncated = true;
        snapshot_result();
        // At max rounds hidden lifetime gold leaves the final winner unresolved.
        if (!f.terminal_z && deadline == i.settings.max_rounds) f.terminal_unknown = true;
    }
    void checkpoint(SimulationState& state) const {
        state.group_count = std::min(i.group_count, max_groups);
        state.source_actor = i.own.id; state.source_round = i.round;
        state.layout_fingerprint = layout_fingerprint();
        for (int g = 0; g < state.group_count; ++g) state.groups[g] = groups[g];
        state.position = position; state.health = hp; state.bag = bag; state.gold = gold;
        state.now = now; state.deadline = deadline; state.stopped = stopped;
        state.phased=phased;state.protected_progress=protected_now;
        if (phased) {
            for (int g=0;g<state.group_count;++g) {
                state.partial_health[g]=partial_health[g];state.clear_reserve_paid[g]=clear_reserve_paid[g];
            }
            for (int a=0;a<std::min(i.altar_count,max_altars);++a) state.lock_remaining[a]=lock_remaining[a];
        }
    }
    bool restore(const SimulationState& state) {
        if (state.group_count != std::min(i.group_count, max_groups) || state.group_count < 0 ||
            state.source_actor != i.own.id || state.source_round != i.round || state.layout_fingerprint != layout_fingerprint() ||
            state.now < i.round || state.deadline != i.round + horizon(i) || state.now > state.deadline ||
            !std::isfinite(state.health) || state.health < 0 || state.health > i.own.maximum_health ||
            (state.health == 0 && !state.stopped)) return false;
        for (int g = 0; g < state.group_count; ++g) {
            if (unsigned(state.groups[g].lifecycle) > unsigned(Lifecycle::respawning) || state.groups[g].ready_round < -1) return false;
            if (state.phased && (!std::isfinite(state.partial_health[g]) || state.partial_health[g]<-1 ||
                state.clear_reserve_paid[g]>1 || (state.partial_health[g]>0 &&
                (!std::isfinite(i.groups[g].clear_damage_per_round) || i.groups[g].clear_damage_per_round<=0)))) return false;
        }
        if (state.phased) for (int a=0;a<std::min(i.altar_count,max_altars);++a)
            if (state.lock_remaining[a]<-1 || state.lock_remaining[a]>std::max(0,i.altars[a].remove_lock_rounds)) return false;
        for (int g = 0; g < state.group_count; ++g) groups[g] = state.groups[g];
        position = state.position; hp = state.health; bag = state.bag; gold = state.gold;
        now = state.now; deadline = state.deadline; stopped = state.stopped;
        phased=state.phased;protected_now=state.protected_progress;
        if (phased) {
            for (int g=0;g<state.group_count;++g) {
                partial_health[g]=state.partial_health[g];clear_reserve_paid[g]=state.clear_reserve_paid[g];
            }
            for (int a=0;a<std::min(i.altar_count,max_altars);++a) lock_remaining[a]=state.lock_remaining[a];
        }
        return true;
    }
    void run(const Job& first) { run_first(first); run_continuations(); }
};
} // namespace detail

using SimulationState = detail::SimulationState;
// Initialize a live decision's unadvanced baseline without putting Forecast's
// event capacity on the stack. This validates source structure, not acquisition
// or travel premises; missing legal observations retain their unknown states.
// Invalid sources leave both caller-owned outputs unchanged.
inline bool valid_initial_state_source(const Information& i, const Menu& menu) {
    return !(i.round < 0 || i.round >= std::numeric_limits<int>::max() - max_horizon || i.settings.max_rounds <= i.round ||
        !i.settings.winning_gems || i.our_score >= i.settings.winning_gems || i.enemy_score >= i.settings.winning_gems ||
        i.group_count < 0 || i.group_count > max_groups || i.altar_count < 0 || i.altar_count > max_altars ||
        menu.count < 0 || menu.count > max_jobs || menu.single_count < 0 || menu.single_count > 8 || menu.single_count > menu.count ||
        i.own.id < 0 || !i.own.alive || i.own.position.x < 0 || i.own.position.y < 0 ||
        !std::isfinite(i.own.health) || !std::isfinite(i.own.maximum_health) ||
        i.own.health <= 0 || i.own.maximum_health <= 0 || i.own.health > i.own.maximum_health);
}
inline bool initialize_state_into(const Information& i, const Menu& menu, Forecast& scratch, SimulationState& state) {
    if (!valid_initial_state_source(i,menu)) return false;
    detail::Simulation simulation(i, menu, scratch);
    simulation.snapshot_result(); simulation.checkpoint(state);
    return true;
}
inline void simulate_first_into(const Information& i, const Job& first, const Menu& menu, Forecast& out,
                                SimulationState& state, const ForecastHooks& hooks = {}) {
    detail::Simulation simulation(i, menu, out, hooks);
    simulation.run_first(first); simulation.checkpoint(state);
}
// Forward execution to a real owner-action boundary inside the first macro.
// A false result leaves scratch unusable; it never admits an aggregate rebase.
inline bool simulate_first_until_into(const Information& i,const Job& first,const Menu& menu,int owner_action_round,
        Forecast& out,SimulationState& state,PhaseCheckpoint& meta,const ForecastHooks& hooks={}) {
    if (!valid_initial_state_source(i,menu) || owner_action_round<i.round ||
        owner_action_round>i.round+horizon(i)) return false;
    detail::Simulation simulation(i,menu,out,hooks);
    simulation.pause_at(owner_action_round);simulation.run_first(first);
    if (!simulation.phase_checkpoint(meta)) return false;
    simulation.checkpoint(state);return true;
}
// Cancel only an unprotected unfinished suffix, run one retained repair and
// keep the original event/stock/body prefix and decision horizon intact.
inline bool run_job_from_into(const Information& i,const Menu& menu,const Forecast& prior,const SimulationState& state,
        const Job& repair,Forecast& out,SimulationState& next,const ForecastHooks& hooks={}) {
    if (&prior==&out || !valid_initial_state_source(i,menu) || !state.phased || state.protected_progress || state.stopped ||
        prior.event_count<0 || prior.event_count>max_events || prior.macros_started<0 || prior.macros_started>=max_macros ||
        prior.final_round!=state.now || prior.final_bag!=state.bag || prior.final_gold!=state.gold || prior.final_health!=state.health ||
        prior.terminal_z || prior.objective.paths || prior.budget_exhausted || prior.abort_reason!=AbortReason::none) return false;
    bool retained=false;
    for (int n=0;n<menu.count;++n) {
        const auto& job=menu.jobs[n];
        retained|=job.kind==repair.kind && job.group==repair.group && job.second_group==repair.second_group &&
            job.altar==repair.altar && job.menu_rank==repair.menu_rank;
    }
    if (!retained || (repair.kind==JobKind::renew && state.position!=i.own.position && state.position!=i.own_spawn)) return false;
    detail::Simulation simulation(i,menu,out,hooks);
    if (!simulation.restore(state)) return false;
    out=prior;simulation.run_first(repair);simulation.checkpoint(next);return true;
}
// Narrow admission for a model's physical prefix ending at the first capture
// of an ordinary FARM. Partial work is never an acquisition. Every other camp
// retains its original snapshot stock; the caller still owns its predicted
// partial-work tape and must establish its timing/safety premises separately.
inline bool complete_farm_after_first_into(const Information& i, const Job& job, const Menu& menu,
        const Forecast& prior, const SimulationState& state, Forecast& out, SimulationState& next,
        const ForecastHooks& hooks = {}) {
    if (&prior==&out || i.group_count<0 || i.group_count>max_groups || !can_refine(i) ||
        job.kind!=JobKind::farm || job.group<0 || job.group>=i.group_count ||
        job.second_group==job.group || job.second_group>=i.group_count || job.second_group < -1 ||
        menu.count<0 || menu.count>max_jobs || menu.single_count<0 || menu.single_count>menu.count ||
        prior.macros_started!=1 || prior.event_count<1 || prior.event_count>max_events || prior.objective.paths ||
        prior.our_secured || prior.enemy_secured || prior.completed_conversion || prior.terminal_z ||
        prior.abort_reason!=AbortReason::none || prior.budget_exhausted || prior.truncated || state.stopped ||
        prior.final_round!=state.now || prior.final_bag!=state.bag || prior.final_gold!=state.gold ||
        prior.final_health!=state.health || state.position!=i.groups[job.group].position) return false;
    bool retained=false;
    for (int n=0;n<menu.count;++n) {
        const auto& candidate=menu.jobs[n];
        retained|=candidate.kind==job.kind && candidate.group==job.group && candidate.second_group==job.second_group &&
            candidate.altar==job.altar && candidate.menu_rank==job.menu_rank;
    }
    if (!retained) return false;
    const auto& source=i.groups[job.group];const auto& stock=state.groups[job.group];
    const unsigned gems=allocated_reward(source.gems,source.eligible_allied_occupants,source.own_is_lowest_occupant);
    const unsigned gold=allocated_reward(source.gold,source.eligible_allied_occupants,source.own_is_lowest_occupant);
    if (state.now<0 || source.respawn_rounds>std::numeric_limits<int>::max()-state.now ||
        i.own.bag>std::numeric_limits<unsigned>::max()-gems || i.own.gold>std::numeric_limits<unsigned>::max()-gold ||
        state.bag!=i.own.bag+gems || state.gold!=i.own.gold+gold || stock.lifecycle!=Lifecycle::captured ||
        !stock.reward_known || !stock.occupied_by_us || stock.ready_round!=state.now+std::max(0,source.respawn_rounds)) return false;
    int captures=0;
    for (int n=0;n<prior.event_count;++n) {
        if ((n%8)==0 && !can_refine(i)) return false;
        const auto& event=prior.events[n];
        if (event.start.round<i.round || event.end.round<event.start.round || event.end.round>state.now ||
            event.secured || event.kind==EventKind::bank || event.kind==EventKind::abort ||
            !std::isfinite(event.health_after) || event.health_after<=0 || event.health_after>i.own.maximum_health) return false;
        if (event.kind==EventKind::capture) {
            ++captures;
            if (event.group_id!=source.id || n!=prior.event_count-1 || event.end.round!=state.now ||
                event.end.phase!=Phase::capture || event.bag_before!=i.own.bag || event.bag_after!=state.bag ||
                event.gold_after!=state.gold || event.health_after!=state.health) return false;
        } else if (event.bag_before!=i.own.bag || event.bag_after!=i.own.bag || event.gold_after!=i.own.gold) return false;
    }
    if (captures!=1 || state.group_count!=i.group_count) return false;
    for (int g=0;g<i.group_count;++g) if (g!=job.group) {
        const auto& original=i.groups[g];const auto& current=state.groups[g];
        const bool fresh=original.visible && original.provenance.observed_round==i.round && original.provenance.status==Status::exact_now;
        const bool reward=fresh && (original.lifecycle==Lifecycle::living ||
            (original.lifecycle==Lifecycle::cleared && original.reward_available) || original.lifecycle==Lifecycle::respawning ||
            original.lifecycle==Lifecycle::captured);
        const int ready=fresh && original.respawn_remaining>=0 ? i.round+original.respawn_remaining : -1;
        if (current.lifecycle!=(fresh?original.lifecycle:Lifecycle::unknown) || current.reward_known!=reward ||
            current.ready_round!=ready || current.occupied_by_us) return false;
    }
    detail::Simulation simulation(i,menu,out,hooks);
    if (!simulation.restore(state)) return false;
    out=prior;simulation.finish_farm_after_first(job);simulation.checkpoint(next);return true;
}
inline bool continue_from_into(const Information& i, const Menu& menu, const Forecast& first,
                               const SimulationState& state, Forecast& out, const ForecastHooks& hooks = {}) {
    // Construct before copying: the constructor resets its caller-owned output.
    // Keep first and out distinct so its event prefix remains available.
    if (&first == &out || menu.count < 0 || menu.count > max_jobs || menu.single_count < 0 || menu.single_count > menu.count ||
        first.macros_started < 0 || first.macros_started > max_macros ||
        first.event_count < 0 || first.event_count > max_events || first.final_round != state.now ||
        first.terminal_z < -1 || first.terminal_z > 1 || (first.terminal_z != 0 && !state.stopped) || first.objective.paths != 0 ||
        first.final_bag != state.bag || first.final_gold != state.gold || first.final_health != state.health) return false;
    detail::Simulation simulation(i, menu, out, hooks);
    if (!simulation.restore(state)) return false;
    out = first;
    simulation.run_continuations();
    return true;
}
struct ContinuationStep {
    bool valid = false, advanced = false;
    int job_index = -1;
};
inline ContinuationStep advance_fixed_into(const Information& i, const Menu& menu, const Forecast& prior,
                                           const SimulationState& state, Forecast& out, SimulationState& next,
                                           int& cursor, const ForecastHooks& hooks = {}) {
    // Pause only between complete macro jobs. Models may update their predicted
    // physical kit/cash here, then request the SAME cyclic continuation rule.
    if (&prior == &out || menu.count < 0 || menu.count > max_jobs || menu.single_count < 0 || menu.single_count > menu.count ||
        prior.macros_started < 0 || prior.macros_started > max_macros ||
        prior.event_count < 0 || prior.event_count > max_events || prior.objective.paths != 0 ||
        prior.final_round != state.now || prior.final_bag != state.bag || prior.final_gold != state.gold ||
        prior.final_health != state.health || prior.terminal_z < -1 || prior.terminal_z > 1 ||
        (prior.terminal_z != 0 && !state.stopped) || cursor < 0 || (menu.single_count > 0 && cursor >= menu.single_count)) return {};
    detail::Simulation simulation(i, menu, out, hooks);
    if (!simulation.restore(state)) return {};
    out = prior;
    const int selected = simulation.next_continuation(cursor);
    if (selected >= 0) simulation.run_first(menu.jobs[selected]);
    else simulation.finish();
    simulation.checkpoint(next);
    return {true, selected >= 0, selected};
}

inline Forecast predict(const Information& i, const Job& j, const Menu& m) {
    Forecast out;
    detail::Simulation(i, m, out).run(j);
    return out;
}
inline void predict_into(const Information& i, const Job& j, const Menu& m, Forecast& out) {
    detail::Simulation(i, m, out).run(j);
}
inline void predict_into(const Information& i, const Job& j, const Menu& m, Forecast& out, const ForecastHooks& hooks) {
    detail::Simulation(i, m, out, hooks).run(j);
}
inline Forecast predict(const Information& i, const Job& j, const ModelState&, const Menu& m) {
    return predict(i, j, m);
}
inline bool forecast_better(const Information& i, const Forecast& a, const Job& aj,
                            const Forecast& b, const Job& bj) {
    const auto value = [](const Forecast& f) {
        if (valid_objective(f.objective)) return f.objective;
        return ObjectiveSum{1, f.terminal_z, f.our_secured, f.enemy_secured, std::uint64_t(f.first_conversion_round)};
    };
    const auto av = value(a), bv = value(b);
    const auto az = std::int64_t(av.terminal_z) * bv.paths, bz = std::int64_t(bv.terminal_z) * av.paths;
    if (az != bz) return az > bz;
    const auto am = (std::int64_t(av.our_secured) - std::int64_t(av.enemy_secured)) * bv.paths;
    const auto bm = (std::int64_t(bv.our_secured) - std::int64_t(bv.enemy_secured)) * av.paths;
    if (am != bm) return am > bm;
    const auto at = av.first_conversion_round * unsigned(bv.paths), bt = bv.first_conversion_round * unsigned(av.paths);
    if (at != bt) return at < bt;
    if (aj.nominal_travel != bj.nominal_travel) return aj.nominal_travel < bj.nominal_travel;
    return job_key_less(i, aj, bj);
}
inline int choose(const Information& i, const Menu& m, const std::array<Forecast, max_jobs>& f) {
    int chosen = -1;
    for (int n = 0; n < m.count; ++n)
        if (chosen < 0 || forecast_better(i, f[n], m.jobs[n], f[chosen], m.jobs[chosen])) chosen = n;
    return chosen;
}
inline void evaluate_into(const Information& i, const Menu& m, Evaluation& e, const ForecastHooks& hooks) {
    // Do not allocate a 16-forecast Evaluation on the 64 KiB WASI stack. The
    // runtime owns Information/Evaluation on its actor-local heap.
    e.menu = m; e.chosen = -1; e.evaluated_count = 0; e.budget_exhausted = false;
    for (int n = 0; n < e.menu.count; ++n) {
        if (i.work_budget.can_refine && !i.work_budget.can_refine(i.work_budget.context)) {
            e.budget_exhausted = true; break;
        }
        predict_into(i, e.menu.jobs[n], e.menu, e.forecasts[n], hooks);
        if (e.forecasts[n].budget_exhausted) { e.budget_exhausted = true; break; }
        ++e.evaluated_count;
    }
    Menu completed = e.menu; completed.count = e.evaluated_count;
    e.chosen = choose(i, completed, e.forecasts);
}
inline void evaluate_into(const Information& i, const Menu& m, Evaluation& e) {
    evaluate_into(i, m, e, ForecastHooks{});
}
inline Evaluation evaluate(const Information& i, const Menu& m) {
    Evaluation e{}; evaluate_into(i, m, e); return e;
}
inline Evaluation evaluate(const Information& i) { return evaluate(i, make_menu(i)); }
} // namespace plan4

