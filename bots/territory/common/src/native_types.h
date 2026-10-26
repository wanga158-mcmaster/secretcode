#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace p7::live {
enum class Design : int { concentric = 1, interior_lines, frontier, islander, forks,
    gatehouse, causeway, sanctuary, ports, migration, elastic_border, counterfront,
    encirclement, harvest_and_exit, reclamation, veil };
enum class Goal { farm, bank, recover, inspect, rally, guard, pressure, withdraw };
struct Source {
    int id = -1, tile = -1, port = -1, ring = -1;
    int distance = -1, exit_distance = -1, recovery_distance = -1, cycle_turns = -1;
    int spawn_distance = -1, port_distance = -1, reserve_distance = -1;
    int ready_after = -1, connector = -1, alternate_port = -1;
    int opened_by = -1;
    int payout = 0; // Conservative actor-owned payload; route changes cannot enlarge it.
    float gems = 0, gold = 0, health = 0, incoming = 0, progress = 0;
    bool seen = false, alive = false, reward_available = false, respawning = false;
    bool reachable = false, feasible = false, joint_feasible = false;
    bool pressured = false, home_connected = false, mobility = false;
    bool redundant = false, cut_proven = false, local_gate = false, occluded = false;
    std::array<int, 3> score{};
    int minimum() const { return *std::min_element(score.begin(), score.end()); }
};
struct Ally {
    int id = -1, ordinal = -1, tile = -1, group = -1, bank_distance = -1;
    float health = 0, max_health = 0, damage = 0, focus = 0;
    unsigned bag = 0;
    bool visible = false, can_attack = false, can_heal = false, at_capture = false;
};
struct Enemy {
    int id = -1, tile = -1, bank_distance = -1;
    float health = 0, damage = 0;
    unsigned bag = 0;
    bool visible = false, channeling = false, stun_immune = false;
};
struct Frame {
    Design design = Design::concentric;
    bool control = false;
    int round = 0, self = -1, ordinal = 0, team_size = 3, tile = -1, spawn = -1;
    int source_count = 0, altar_count = 0, port_count = 0;
    int current = -1, home = -1, safest = -1;
    int capture_enemy = -1, capture_enemy_tile = -1;
    unsigned bag = 0, gold = 0, own_score = 0, enemy_score = 0;
    float health = 0, max_health = 0, damage = 0, focus = 0;
    float camp_damage_factor = 1;
    int kit_level = 0;
    int captures = 0, banks = 0, last_capture_source = -1;
    unsigned next_purchase_cost = 0;
    bool at_spawn = false, healing_ready = false, dash_ready = false, teleport_ready = false;
    bool channel_active = false, safe_capture = false, fresh_pressure = false;
    bool converting = false;
    int pressure_tile = -1, pressure_port = -1;
    bool own_ward_ready = false;
    // Encirclement alone may use this certificate. Three enemy responses are
    // continued passage/fighting, waiting, and alternate conversion. Unknown
    // response/exclusion proofs leave it false and earn zero denial credit.
    bool denial_certificate = false;
    int denial_observed_round = -1, denial_enemy = -1, denial_waypoint = -1, denial_port = -1;
    unsigned denial_bag = 0;
    std::array<int, 3> denial{}, denial_force_independent{};
    int ally_count = 0, enemy_count = 0;
    std::array<Source, 8> sources{};
    std::array<Ally, 6> allies{};
    std::array<Enemy, 2> enemies{};
    std::array<std::array<int, 8>, 8> distances{};
    std::array<int, 3> windows{120, 120, 120};
};
inline float own_camp_damage_factor(unsigned owned_health_shards, float queried_reduction) {
    if (owned_health_shards < 5 || !std::isfinite(queried_reduction) ||
        queried_reduction < 0 || queried_reduction > 1) return 1;
    // Five owned matching shards grant one passive; duplicate emblems do not stack.
    return 1 - queried_reduction;
}
inline float self_camp_incoming(const Frame& frame, const Source& source) {
    const float factor = frame.camp_damage_factor;
    return source.incoming * (std::isfinite(factor) && factor >= 0 && factor <= 1 ? factor : 1);
}
struct State {
    int source = -1, initial_source = -1, home_source = -1, previous_source = -1;
    int port = -1, pending_port = -1, region = -1, phase = 0, ring = 0;
    int admitted_round = -1, epoch = -1, attempt_started = -1;
    int retired_source = -1, retired_epoch = -1, handoff_epoch = -1;
    int captures = 0, banks = 0, renewals = 0, last_kit = -1;
    int project_captures = 0, banks_at_admission = 0, captures_at_admission = 0;
    int last_seen_pressure = -1, last_arrival = -1, raid_captures = 0;
    bool active = false, payout_seen = false, bank_seen = false;
    bool bank_pending = false, relocated = false, inspection_failed = false;
    std::array<bool, 8> captured{};
};
struct Task {
    Goal goal = Goal::farm;
    int source = -1, waypoint = -1, port = -1, route = 0, peer = -1;
    int holder_ordinal = -1;
    bool mobility = false, heal = false, ward = false;
    bool mechanism = false, release_helper = false;
    friend bool operator==(const Task&, const Task&) = default;
};
inline void bind_source(State& state, int source) {
    if (state.source != source) {
        state.payout_seen = state.bank_seen = state.bank_pending = false;
        state.captures_at_admission = state.captures;
        state.banks_at_admission = state.banks;
    }
    state.source = source;
}
inline bool valid_source(const Frame& f, int i) { return i >= 0 && i < f.source_count && i < 8; }
inline bool usable(const Frame& f, int i, bool joint = false) {
    return valid_source(f, i) && (f.sources[i].feasible || (joint && f.sources[i].joint_feasible));
}
inline int compare_source(const Source& a, const Source& b) {
    if (a.minimum() != b.minimum()) return a.minimum() > b.minimum() ? 1 : -1;
    if (a.score[1] != b.score[1]) return a.score[1] > b.score[1] ? 1 : -1;
    if (a.cycle_turns != b.cycle_turns) return a.cycle_turns < b.cycle_turns ? 1 : -1;
    if (a.distance != b.distance) return a.distance < b.distance ? 1 : -1;
    return a.id < b.id ? 1 : a.id > b.id ? -1 : 0;
}
template<class Predicate>
inline int best(const Frame& f, Predicate predicate, bool joint = false) {
    int selected = -1;
    for (int i = 0; i < f.source_count && i < 8; ++i)
        if (usable(f, i, joint) && predicate(i, f.sources[i]) &&
            (selected < 0 || compare_source(f.sources[i], f.sources[selected]) > 0)) selected = i;
    return selected;
}
inline int best(const Frame& f) { return best(f, [](int, const Source&) { return true; }); }
inline int nearest(const Frame& f, int port = -1, bool joint = false) {
    int selected = -1;
    for (int i = 0; i < f.source_count && i < 8; ++i) if (usable(f, i, joint) &&
        (port < 0 || f.sources[i].port == port) && f.sources[i].distance >= 0 &&
        (selected < 0 || f.sources[i].distance < f.sources[selected].distance ||
         (f.sources[i].distance == f.sources[selected].distance && f.sources[i].id < f.sources[selected].id))) selected = i;
    return selected;
}
inline Task farm(int source, bool mechanism = false) {
    Task t; t.source = source; t.mechanism = mechanism; return t;
}
inline Task fallback(const Frame& f, const State& s) {
    if (f.bag) { Task t; t.goal = Goal::bank; return t; }
    if (usable(f, s.home_source)) return farm(s.home_source);
    if (usable(f, f.home)) return farm(f.home);
    int target = best(f);
    if (target >= 0) return farm(target);
    int inspect = -1;
    for (int i = 0; i < f.source_count && i < 8; ++i)
        if (f.sources[i].reachable && !f.sources[i].seen &&
            (inspect < 0 || f.sources[i].distance < f.sources[inspect].distance)) inspect = i;
    Task t; t.goal = inspect >= 0 ? Goal::inspect : Goal::recover; t.source = inspect; return t;
}
inline void admit(State& s, const Frame& f, int source) {
    if (s.active && s.source == source) return;
    s.previous_source = s.source; bind_source(s, source); s.active = true;
    s.admitted_round = f.round; s.epoch = f.round / 64;
    s.attempt_started = f.round; s.banks_at_admission = s.banks;
    s.captures_at_admission = s.captures; s.payout_seen = s.bank_seen = false;
    if (s.initial_source < 0) s.initial_source = source;
    if (s.home_source < 0) s.home_source = f.home;
}
inline bool retired(const State& s, const Frame& f, int source) {
    return source == s.retired_source && s.retired_epoch == f.round / 64;
}
inline bool attempt_expired(const State& s, const Frame& f, int limit = 8) {
    return s.attempt_started >= 0 && f.round - s.attempt_started >= limit;
}
inline void retire(State& s) {
    s.retired_source = s.source; s.retired_epoch = s.epoch; s.active = false;
}
inline const Ally* visible_ally(const Frame& f, int ordinal) {
    for (int i = 0; i < f.ally_count && i < 6; ++i)
        if (f.allies[i].visible && f.allies[i].ordinal == ordinal) return &f.allies[i];
    return nullptr;
}
inline bool improves(const Frame& f, int candidate, int incumbent) {
    return usable(f, candidate) && (!usable(f, incumbent) ||
           f.sources[candidate].minimum() >= f.sources[incumbent].minimum() + 1);
}
} // namespace p7::live
