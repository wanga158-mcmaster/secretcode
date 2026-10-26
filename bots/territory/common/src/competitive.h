#pragma once
#include "native_types.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#ifndef P7_NO_PARTIAL_ADMISSION
#define P7_NO_PARTIAL_ADMISSION 0
#endif

namespace p7::live {
struct CompetitiveSource {
    int id = -1, distance = -1, gems = 0, tile = -1;
};

// Keep the nearby opening, then retain reachable gem-bearing progression
// camps before distant maximum-reward camps. Public reachability only selects
// candidates; this does not certify observation, combat safety or income.
inline std::vector<int> competitive_shortlist(const std::vector<CompetitiveSource>& sources,
        Design design = Design::concentric, int ordinal = 0, int width = 0, int team_size = 3) {
    std::vector<int> ranked;
    ranked.reserve(sources.size());
    for (int i = 0; i < static_cast<int>(sources.size()); ++i)
        if (sources[i].distance >= 0) ranked.push_back(i);
    std::sort(ranked.begin(), ranked.end(), [&](int a, int b) {
        if (sources[a].distance != sources[b].distance) return sources[a].distance < sources[b].distance;
        if (sources[a].gems != sources[b].gems) return sources[a].gems > sources[b].gems;
        if (sources[a].id != sources[b].id) return sources[a].id < sources[b].id;
        return a < b;
    });
    std::vector<int> selected;
    selected.reserve(std::min<std::size_t>(8, ranked.size()));
    std::vector<bool> used(sources.size(), false);
    auto add = [&](int i) { selected.push_back(i); used[i] = true; };
    for (std::size_t n = 0; n < std::min<std::size_t>(4, ranked.size()); ++n) add(ranked[n]);
    // The diversified source ladder is a Ports-only regional experiment.
    // Other designs retain the measured nearest-gem continuation control.
    if (design != Design::ports) {
        for (int i : ranked) if (selected.size() < 8 && !used[i] && sources[i].gems > 0) add(i);
        for (int i : ranked) if (selected.size() < 8 && !used[i]) add(i);
        return selected;
    }
    // Keep the nearest gem-bearing stepping stones before expanding. Removing
    // both small camps forced unscaled opening kits directly into heavy camps.
    for (int i : ranked) if (selected.size() < 6 && !used[i] && sources[i].gems > 0) add(i);

    int nearest = std::numeric_limits<int>::max();
    for (int i : ranked) if (!used[i] && sources[i].gems > 0) nearest = std::min(nearest, sources[i].distance);
    // Public payload alone cannot price a remote camp's combat. Advance one
    // bounded travel band before comparing reward and geographic coverage.
    const long long limit = static_cast<long long>(nearest) + std::max(8, nearest / 3);
    auto continuation = [&](int i) {
        return !used[i] && sources[i].gems > 0 && sources[i].distance <= limit;
    };
    int min_x = std::numeric_limits<int>::max(), min_y = min_x, max_x = -1, max_y = -1;
    for (int i : ranked) if ((used[i] || continuation(i)) && width > 0 && sources[i].tile >= 0) {
        const int x = sources[i].tile % width, y = sources[i].tile / width;
        min_x = std::min(min_x, x); max_x = std::max(max_x, x);
        min_y = std::min(min_y, y); max_y = std::max(max_y, y);
    }
    const bool geometry = max_x >= min_x && max_y >= min_y;
    const bool spread_x = geometry && max_x - min_x >= max_y - min_y;
    const int low = spread_x ? min_x : min_y, high = spread_x ? max_x : max_y;
    const int roles = std::max(1, team_size), role = ((ordinal % roles) + roles) % roles;
    double preferred = (role + .5) / roles;
    switch (design) {
    case Design::interior_lines: preferred = role % 3 == 0 ? .5 : role % 3 == 1 ? .15 : .85; break;
    case Design::counterfront: preferred = role % 3 == 2 ? .5 : role % 3 == 0 ? .15 : .85; break;
    case Design::ports: preferred = role % 3 == 2 ? .5 : role % 2 == 0 ? .15 : .85; break;
    case Design::concentric: case Design::sanctuary: if (role % 3 == 1) preferred = .5; break;
    case Design::gatehouse: case Design::causeway: case Design::encirclement: if (role % 3 == 0) preferred = .5; break;
    case Design::islander: if (role % 3 == 2) preferred = .5; break;
    case Design::frontier: case Design::migration: case Design::elastic_border:
    case Design::forks: case Design::harvest_and_exit: case Design::reclamation: case Design::veil: break;
    }
    auto score = [&](int i) {
        const CompetitiveSource& x = sources[i];
        double value = x.gems / (x.distance + 8.0);
        if (geometry && x.tile >= 0) {
            const int px = x.tile % width, py = x.tile / width;
            int separation = std::numeric_limits<int>::max();
            for (int j : selected) if (sources[j].tile >= 0)
                separation = std::min(separation, std::max(std::abs(px - sources[j].tile % width),
                                                          std::abs(py - sources[j].tile / width)));
            if (separation < std::numeric_limits<int>::max()) value *= 1 + std::min(12, separation) / 24.0;
            if (high > low) {
                const double position = ((spread_x ? px : py) - low) / static_cast<double>(high - low);
                value *= .9 + .2 * (1 - std::abs(position - preferred));
            }
        }
        return value;
    };
    while (selected.size() < 8) {
        int next = -1;
        for (int i : ranked) if (continuation(i) && (next < 0 || score(i) > score(next) ||
            (score(i) == score(next) && (sources[i].id < sources[next].id ||
             (sources[i].id == sources[next].id && i < next))))) next = i;
        if (next < 0) break;
        add(next);
    }
    for (int i : ranked) if (selected.size() < 8 && !used[i]) add(i);
    return selected;
}

struct CompetitiveState {
    int source = -1, last_tile = -1, best_distance = -1, last_progress = -1;
    std::array<int, 8> retry_after{};
    int project_port = -1, project_captures = 0, captures_seen = 0;
    int banks_at_project = 0, anchor_source = -1, retreat_until = -1;
    bool exiting = false;
};

inline unsigned competitive_bank_target(Design design) {
    switch (design) {
    case Design::concentric: return 20;
    case Design::interior_lines: return 24;
    case Design::frontier: return 24;
    case Design::islander: return 18;
    case Design::forks: return 20;
    case Design::gatehouse: return 16;
    case Design::causeway: return 12;
    case Design::sanctuary: return 16;
    case Design::ports: return 24;
    case Design::migration: return 28;
    case Design::elastic_border: return 16;
    case Design::counterfront: return 24;
    case Design::encirclement: return 12;
    case Design::harvest_and_exit: return 24;
    case Design::reclamation: return 20;
    case Design::veil: return 32;
    }
    return 20;
}

namespace competitive_detail {
inline void reset(CompetitiveState& state) {
    state.source = state.last_tile = state.best_distance = state.last_progress = -1;
}
inline bool productive(const Frame& f, const Source& source, bool affordable_available = false) {
    if (source.distance < 0 || source.cycle_turns <= 0 || source.respawning) return false;
    if (source.gems <= 0 && (f.kit_level >= 10 || source.gold <= 0)) return false;
    if (affordable_available && source.seen && !source.feasible && !source.reward_available) return false;
#if P7_NO_PARTIAL_ADMISSION
    // Diagnostic ablation only: retain unknown inspection and the existing
    // solo/joint clear and reward certificates. Do not treat an absent solo
    // estimate as a proof of danger or replace multi-visit work permanently.
    if (source.seen && !source.feasible && !source.joint_feasible && !source.reward_available) return false;
#endif
    // Unknown camps earn only a personal inspection. A known camp can admit
    // partial work on owned health, without changing its feasibility or score.
    const float reserve = std::max(f.max_health * .55f, self_camp_incoming(f, source) * 3 + f.max_health * .2f);
    return !source.seen || source.feasible || source.reward_available || f.health > reserve;
}
// Scoped opening admission only. This public basic-only budget
// chooses an inspection/work destination; it never certifies an unseen clear.
inline bool interior_bootstrap_affordable(const Frame& f, const Source& source) {
    if (!std::isfinite(f.health) || !std::isfinite(f.max_health) || !std::isfinite(f.damage) ||
        f.health <= 0 || f.max_health <= 0 || f.damage <= 0 ||
        !std::isfinite(source.health) || !std::isfinite(source.incoming) ||
        source.health <= 0 || source.incoming < 0) return false;
    const double turns = std::ceil(source.health / std::max(1.f, f.damage));
    return f.health > self_camp_incoming(f, source) * (turns + 2) + f.max_health * .2;
}
inline double role_affinity(const Frame& f, int source) {
    if (f.sources[source].gems <= 0 || f.team_size <= 0 || f.ordinal < 0 || f.ordinal >= f.team_size)
        return 1;
    const Source& x = f.sources[source];
    auto home = [](const Source& s) {
        return s.spawn_distance >= 0 ? s.spawn_distance : std::numeric_limits<int>::max();
    };
    int rank = 0;
    for (int i = 0; i < f.source_count && i < 8; ++i) {
        const Source& other = f.sources[i];
        if (i == source || other.gems <= 0) continue;
        if (other.gems > x.gems || (other.gems == x.gems &&
            (home(other) < home(x) || (home(other) == home(x) &&
             (other.id < x.id || (other.id == x.id && i < source)))))) ++rank;
    }
    // A public, static preference spreads comparable routes. It is neither
    // ownership nor an assumption about an unseen ally's position or damage.
    return rank % f.team_size == f.ordinal ? 1.25 : .85;
}
inline double base_rate(const Frame& f, int source) {
    const Source& x = f.sources[source];
    // Gold-only work can fund the usable opening kit, not just its first shard.
    // It has no secured-gem credit.
    const double reward = x.gems > 0 ? x.gems : x.gold / 20.0;
    double value = reward / x.cycle_turns;
    for (int i = 0; i < f.ally_count && i < 6; ++i)
        if (f.allies[i].visible && f.allies[i].group == source) {
            value *= .8; break; // Crowding costs time; this is not ally DPS.
        }
    return value;
}
inline double rate(const Frame& f, int source) { return base_rate(f, source) * role_affinity(f, source); }
inline bool credible_contact(const Frame& f, const Task& proposal) {
    if (proposal.goal != Goal::guard && proposal.goal != Goal::pressure) return false;
    for (int i = 0; i < f.enemy_count && i < 2; ++i)
        if (f.enemies[i].visible && (proposal.goal == Goal::guard || proposal.peer < 0 ||
                                   proposal.peer == f.enemies[i].id)) return true;
    return false;
}

inline bool waypoint_supported(const Frame& f, const Task& proposal) {
    if (proposal.waypoint < 0) return true;
    if (!valid_source(f, proposal.source)) return false;
    const Source& x = f.sources[proposal.source];
    if (proposal.waypoint == x.tile || proposal.waypoint == x.connector || proposal.waypoint == f.spawn) return true;
    return valid_source(f, x.opened_by) && proposal.waypoint == f.sources[x.opened_by].connector;
}
inline bool supported_intent(const Frame& f, const Task& proposal, const CompetitiveState& state,
                             bool affordable_available) {
    if (!valid_source(f, proposal.source) || f.round < state.retry_after[proposal.source]) return false;
    const Source& x = f.sources[proposal.source];
    if (x.distance < 0 || x.cycle_turns <= 0 || x.respawning) return false;
    if (proposal.heal && f.healing_ready) {
        for (int i = 0; i < f.ally_count && i < 6; ++i) {
            const Ally& a = f.allies[i];
            if (a.visible && a.id == proposal.peer && a.health > 0 && a.health < a.max_health * .9f &&
                proposal.waypoint == a.tile) return true;
        }
    }
    if (!waypoint_supported(f, proposal)) return false;
    if (proposal.goal == Goal::rally && f.current != proposal.source && x.seen && x.joint_feasible) {
        for (int i = 0; i < f.ally_count && i < 6; ++i)
            if (f.allies[i].visible && f.allies[i].can_attack && f.allies[i].group == proposal.source) return true;
    }
    if (!productive(f, x, affordable_available)) return false;
    if (proposal.goal == Goal::withdraw && !x.pressured &&
        (f.fresh_pressure || f.health < f.max_health * .8f ||
         (proposal.mechanism && (f.design == Design::migration || f.design == Design::elastic_border ||
                                f.design == Design::counterfront || f.design == Design::harvest_and_exit)))) return true;
    if (proposal.mobility && x.mobility && (f.dash_ready || f.teleport_ready)) return true;
    if (proposal.ward && f.own_ward_ready && x.occluded) return true;
    return proposal.mechanism && valid_source(f, x.opened_by) &&
        f.sources[x.opened_by].respawning && f.sources[x.opened_by].ready_after > x.cycle_turns + 2;
}
inline Task bank_task(const Frame& f, const Task& proposal) {
    Task task; task.goal = Goal::bank;
    if (proposal.goal != Goal::bank) return task;
    if (proposal.port >= 0 && proposal.port < f.altar_count) task.port = proposal.port;
    const int source = f.last_capture_source;
    if (!valid_source(f, source)) return task;
    const Source& x = f.sources[source];
    task.source = source;
    // An unexecuted project S cannot lend its exit to gems actually captured
    // at T. Port preference is independent; circuit/route/ward evidence is not.
    if (proposal.source != source) return task;
    if (proposal.mobility && x.mobility && (f.dash_ready || f.teleport_ready)) {
        task.mobility = true; task.mechanism = true;
    }
    if (proposal.route == 1 && x.redundant && x.alternate_port >= 0 && proposal.port == x.alternate_port) {
        task.route = 1; task.port = x.alternate_port; task.mechanism = true;
    }
    if (f.design == Design::veil && proposal.mechanism && x.occluded) {
        task.mechanism = true; task.ward = proposal.ward && f.own_ward_ready;
    }
    return task;
}
inline void adopt(CompetitiveState& state, const Frame& f, int source) {
    if (state.source != source) {
        state.source = source; state.best_distance = f.sources[source].distance;
        state.last_progress = f.round;
    }
    state.last_tile = f.tile;
    if (state.project_port < 0) {
        state.project_port = f.sources[source].port; state.banks_at_project = f.banks;
    }
}

// Each branch selects a different executable layout or continuation. A zero
// score means that geographic opportunity is unavailable; generic production
// remains the fallback rather than inventing access, support or denial.
inline std::array<double, 8> design_scores(const Frame& f, CompetitiveState& state,
                                         const std::array<bool, 8>& eligible) {
    std::array<double, 8> out{};
    auto pick = [&](auto matches, auto value) {
        int result = -1;
        for (int i = 0; i < f.source_count && i < 8; ++i) if (eligible[i] && matches(i) &&
            (result < 0 || value(i) > value(result) ||
             (value(i) == value(result) && f.sources[i].id < f.sources[result].id))) result = i;
        return result;
    };
    auto value = [&](int i) { return base_rate(f, i); };
    auto all = [](int) { return true; };
    const int best = pick(all, value);
    int nearest_home = std::numeric_limits<int>::max();
    for (int i = 0; i < f.source_count && i < 8; ++i)
        if (f.sources[i].gems > 0 && f.sources[i].spawn_distance >= 0)
            nearest_home = std::min(nearest_home, f.sources[i].spawn_distance);
    auto inner = [&](int i) { return f.sources[i].spawn_distance >= 0 &&
        nearest_home != std::numeric_limits<int>::max() && f.sources[i].spawn_distance <= nearest_home + 10; };
    const int home = pick(inner, value);
    auto distance = [&](int a, int b) {
        return valid_source(f, a) && valid_source(f, b) ? f.distances[a][b] : -1;
    };
    auto put = [&](auto matches, auto score) {
        for (int i = 0; i < f.source_count && i < 8; ++i)
            if (eligible[i] && matches(i)) out[i] = score(i);
    };
    auto port = [&](int p) { return pick([&](int i) { return f.sources[i].port == p; }, value); };
    const int count = std::max(1, f.port_count);
    switch (f.design) {
    case Design::concentric: {
        // The field-shopping opener funds its existing five-shard threshold
        // before comparing heavy inner camps. Other roles keep their layout.
        if (f.ordinal == 0 && f.kit_level < 5) {
            const int opening = pick([&](int i) {
                const Source& x = f.sources[i];
                return inner(i) && x.home_connected &&
                    (x.reward_available || interior_bootstrap_affordable(f, x));
            }, [&](int i) {
                const Source& x = f.sources[i];
                const double work = x.reward_available ? 0 : std::ceil(x.health / std::max(1.f, f.damage));
                return 1 / (1 + x.distance + work);
            });
            if (opening >= 0) { out[opening] = value(opening); break; }
        }
        if (f.ordinal == 1 || f.banks == 0 || f.kit_level < 5) put(inner, value);
        else {
            int next = std::numeric_limits<int>::max();
            for (int i = 0; i < f.source_count && i < 8; ++i)
                if (eligible[i] && !inner(i) && f.sources[i].spawn_distance >= 0)
                    next = std::min(next, f.sources[i].spawn_distance);
            put([&](int i) { return !inner(i) && f.sources[i].spawn_distance >= 0 &&
                f.sources[i].spawn_distance <= next + (next < 100000 ? 4 : 0); }, value);
        }
        break;
    }
    case Design::interior_lines: {
        // Only the unfunded right worker borrows a short home camp. The reserve,
        // left wing and funded right worker retain the original layout below.
        if (f.ordinal == 2 && f.kit_level < 10) {
            const int bootstrap = pick([&](int i) {
                const Source& x = f.sources[i];
                return inner(i) && x.home_connected &&
                    (x.reward_available || interior_bootstrap_affordable(f, x));
            }, [&](int i) {
                const Source& x = f.sources[i];
                const double work = x.reward_available ? 0 : std::ceil(x.health / std::max(1.f, f.damage));
                return 1 / (1 + x.distance + work);
            });
            if (bootstrap >= 0) { out[bootstrap] = value(bootstrap); break; }
        }
        const int left = port(0);
        const int right = pick([&](int i) { return i != left && (f.port_count < 2 || f.sources[i].port == 1); }, value);
        if (left < 0 || right < 0) break;
        if (f.ordinal % 3 == 1) out[left] = value(left);
        else if (f.ordinal % 3 == 2) out[right] = value(right);
        else put([&](int i) { return distance(i, left) >= 0 && distance(i, right) >= 0; },
                 [&](int i) { return value(i) / (1 + std::max(distance(i, left), distance(i, right)) / 8.0); });
        break;
    }
    case Design::frontier:
        if (f.ordinal % 3 == 2 || f.banks == 0) put(inner, value);
        else if (f.ordinal % 3 == 1 && valid_source(f, state.anchor_source))
            put([&](int i) { const int d = distance(i, state.anchor_source); return d >= 0 && d <= 8; }, value);
        else put([&](int i) { return !inner(i) && f.sources[i].seen && !f.sources[i].pressured; }, value);
        break;
    case Design::islander:
        if (f.dash_ready || f.teleport_ready)
            put([&](int i) { return f.sources[i].mobility; }, value);
        break;
    case Design::forks: {
        // The unfunded caster producer starts on a small home-connected job.
        // Basic work ranks candidates; it grants no solo-clear certificate.
        if (f.ordinal == 3 && f.kit_level < 5) {
            const int opening = pick([&](int i) { return f.sources[i].home_connected; }, [&](int i) {
                const Source& x = f.sources[i];
                const double work = x.reward_available ? 0 :
                    std::ceil(std::max(0.f, x.health) / std::max(1.f, f.damage));
                return -static_cast<double>(x.distance) - work;
            });
            if (opening >= 0) { out[opening] = value(opening); break; }
        }
        const int district = pick([&](int i) { return f.sources[i].redundant && f.sources[i].alternate_port >= 0; }, value);
        if (district < 0) break;
        if (f.ordinal % 3 == 2)
            put([&](int i) { const int d = distance(i, district); return i != district && d >= 0 && d <= 10; }, value);
        else out[district] = value(district);
        break;
    }
    case Design::gatehouse: {
        const int enclave = pick([&](int i) { return (f.sources[i].local_gate || f.sources[i].cut_proven) && f.sources[i].connector >= 0; }, value);
        if (enclave < 0) break;
        if (f.ordinal % 3 == 0)
            put([&](int i) { const int d = distance(i, enclave); return i != enclave && d >= 0 && d <= 10; }, value);
        else {
            bool guard = false;
            for (int i = 0; i < f.ally_count && i < 6; ++i)
                guard |= f.allies[i].visible && f.allies[i].ordinal == 0 && f.allies[i].can_attack &&
                    f.allies[i].health > f.allies[i].max_health * .45f &&
                    (f.allies[i].group == enclave || f.allies[i].tile == f.sources[enclave].connector);
            if (guard) put([&](int i) { return f.sources[i].connector == f.sources[enclave].connector && !f.sources[i].pressured; }, value);
        }
        break;
    }
    case Design::causeway: {
        int opening = -1;
        for (int i = 0; i < f.source_count && i < 8; ++i) if (f.sources[i].cut_proven)
            for (int j = 0; j < f.source_count && j < 8; ++j)
                if (f.sources[j].opened_by == i && f.sources[j].gems > 0) opening = i;
        if (opening < 0) break;
        const Source& gate = f.sources[opening];
        if (gate.seen && gate.respawning)
            put([&](int i) { return f.sources[i].opened_by == opening && f.sources[i].seen &&
                f.sources[i].cycle_turns + 2 < gate.ready_after; }, value);
        else if (eligible[opening] && gate.seen) out[opening] = value(opening);
        break;
    }
    case Design::sanctuary:
        put([&](int i) { return inner(i) && (f.ordinal % 3 != 0 || f.sources[i].incoming > 0); },
            [&](int i) { return (f.sources[i].gems > 0 ? f.sources[i].gems : f.sources[i].gold / 20.0) /
                (f.sources[i].cycle_turns + 2.0 * std::max(0, f.sources[i].spawn_distance)); });
        break;
    case Design::ports:
        put([&](int i) { return f.sources[i].port == f.ordinal % count; }, value);
        break;
    case Design::migration: {
        const int resident = state.project_port >= 0 ? state.project_port : (best >= 0 ? f.sources[best].port : -1);
        const int local = port(resident);
        const int alternative = pick([&](int i) { return f.sources[i].port != resident && f.sources[i].seen; }, value);
        const bool move = alternative >= 0 && (local < 0 ||
            (state.project_captures > 0 && f.banks > state.banks_at_project && value(alternative) > value(local) * 1.25));
        const int destination = move ? f.sources[alternative].port : resident;
        if (move) { state.project_port = destination; state.project_captures = 0; state.banks_at_project = f.banks; }
        put([&](int i) { return f.sources[i].port == destination; }, value);
        break;
    }
    case Design::elastic_border:
        if (f.fresh_pressure && (state.source < 0 || f.sources[state.source].pressured)) state.retreat_until = f.round + 32;
        if (f.ordinal % 3 == 2 || f.round < state.retreat_until) put(inner, value);
        else put([&](int i) { return !inner(i) && f.sources[i].seen && !f.sources[i].pressured; }, value);
        break;
    case Design::counterfront: {
        int destination = f.ordinal % 3 == 1 && count > 1 ? 1 : 0;
        if (f.ordinal % 3 == 2) {
            destination = state.project_port >= 0 ? state.project_port : 0;
            if (f.fresh_pressure && count > 1 && f.pressure_port == destination) destination = 1 - destination;
            state.project_port = destination;
        }
        put([&](int i) { return f.sources[i].port == destination && !f.sources[i].pressured; }, value);
        break;
    }
    case Design::encirclement:
        // A real certified pressure task is preserved separately. The income
        // actor keeps a compact circuit while the other actors farm freely.
        if (f.ordinal % 3 == 2) put(inner, value);
        break;
    case Design::harvest_and_exit:
        if (state.project_captures >= 3) state.exiting = true;
        if (state.exiting) {
            if (state.anchor_source < 0) state.anchor_source = home;
            put(inner, value);
        } else {
            const int region = state.project_port >= 0 ? state.project_port : (best >= 0 ? f.sources[best].port : -1);
            put([&](int i) { return !inner(i) && f.sources[i].port == region && !f.sources[i].pressured; }, value);
        }
        break;
    case Design::reclamation:
        put([&](int i) { return f.sources[i].seen && f.sources[i].progress > 0 &&
            f.sources[i].health > 0 && !f.sources[i].pressured; }, value);
        break;
    case Design::veil:
        put([&](int i) { return f.sources[i].seen && f.sources[i].occluded && !f.sources[i].pressured; }, value);
        break;
    }
    return out;
}
} // namespace competitive_detail

inline Task competitive_plan(const Frame& f, const Task& proposal, CompetitiveState& state) {
    using namespace competitive_detail;
    if (f.captures > state.captures_seen) {
        if (valid_source(f, f.last_capture_source) && f.last_capture_source == state.source) {
            if (state.project_port < 0 || f.sources[f.last_capture_source].port == state.project_port)
                ++state.project_captures;
            if (f.design == Design::frontier) {
                int nearest = std::numeric_limits<int>::max();
                for (int i = 0; i < f.source_count && i < 8; ++i)
                    if (f.sources[i].gems > 0 && f.sources[i].spawn_distance >= 0)
                        nearest = std::min(nearest, f.sources[i].spawn_distance);
                if (nearest < 100000 && f.sources[f.last_capture_source].spawn_distance > nearest + 10)
                    state.anchor_source = f.last_capture_source;
            }
        }
        state.captures_seen = f.captures;
    }
    if (f.design == Design::harvest_and_exit && state.exiting && valid_source(f, state.anchor_source) &&
        state.anchor_source == f.last_capture_source && f.banks > state.banks_at_project) {
        state.exiting = false; state.project_captures = 0; state.project_port = -1;
        state.anchor_source = -1; state.banks_at_project = f.banks;
    }
    if (f.channel_active || f.safe_capture) return proposal;
    if (f.bag && f.at_spawn && f.max_health > 0 && f.health >= f.max_health) {
        reset(state);
        Task task; task.goal = Goal::bank;
        if (valid_source(f, f.last_capture_source)) task.source = f.last_capture_source;
        return task;
    }
    if (f.bag && f.converting) { reset(state); return bank_task(f, proposal); }

    bool disengaging_capture = false;
    if (f.capture_enemy >= 0 && f.capture_enemy_tile >= 0 && valid_source(f, f.current) &&
        f.sources[f.current].seen && f.sources[f.current].reward_available) {
        // The caller supplies these fields only from a freshly observed enemy
        // on another spawn of our fully visible, cleared capture group.
        if (f.bag) { reset(state); return bank_task(f, proposal); }
        Goal confrontation = Goal::recover;
        switch (f.design) {
        case Design::interior_lines: case Design::gatehouse: case Design::islander:
        case Design::causeway: case Design::counterfront: confrontation = Goal::guard; break;
        case Design::frontier: case Design::elastic_border:
        case Design::encirclement: case Design::veil: confrontation = Goal::pressure; break;
        default: break;
        }
        if (f.max_health > 0 && f.health * 5 >= f.max_health * 3 && confrontation != Goal::recover) {
            adopt(state, f, f.current);
            Task task; task.goal = confrontation; task.source = f.current;
            task.peer = f.capture_enemy; task.waypoint = f.capture_enemy_tile;
            // This is a legal local contest, not a denial or kill certificate.
            return task;
        }
        state.retry_after[f.current] = f.round + 32;
        reset(state); disengaging_capture = true;
    }

    bool affordable_available = false;
    auto refresh_affordable = [&] {
        affordable_available = false;
        for (int i = 0; i < f.source_count && i < 8; ++i) {
            const Source& x = f.sources[i];
            if (f.round >= state.retry_after[i] && x.seen && (x.feasible || x.reward_available) &&
                productive(f, x)) { affordable_available = true; break; }
        }
    };
    refresh_affordable();
    auto eligible = [&](int i) {
        return valid_source(f, i) && f.round >= state.retry_after[i] &&
            productive(f, f.sources[i], affordable_available);
    };
    if (state.source >= 0) {
        if (!eligible(state.source)) reset(state);
        else {
            const Source& x = f.sources[state.source];
            if (state.best_distance < 0 || x.distance < state.best_distance) {
                state.best_distance = x.distance; state.last_progress = f.round;
            }
            // Physical contact is productive work, not a stalled journey. Camp
            // clears may legitimately require more than eight stationary turns.
            if (f.current == state.source && x.seen) state.last_progress = f.round;
            state.last_tile = f.tile;
            if (state.last_progress >= 0 && f.round - state.last_progress >= 8) {
                state.retry_after[state.source] = f.round + 32; reset(state);
                refresh_affordable();
            }
        }
    }
    const Task conversion = bank_task(f, proposal);
    if (!f.at_spawn && f.next_purchase_cost > 0 && f.gold / f.next_purchase_cost >= 10) {
        reset(state);
        if (f.bag) return conversion;
        Task task; task.goal = Goal::recover; return task;
    }
    const bool special_exit = conversion.mobility || conversion.route == 1 ||
        (f.design == Design::veil && conversion.mechanism);
    const bool end_window = f.windows[1] <= std::max(12, static_cast<int>(f.bag / 10) + 4);
    if (f.bag && (f.bag >= competitive_bank_target(f.design) || end_window || special_exit)) {
        reset(state); return conversion;
    }
    if (!disengaging_capture && credible_contact(f, proposal) &&
        (f.design != Design::encirclement || proposal.goal != Goal::pressure || f.denial_certificate)) return proposal;
    if (supported_intent(f, proposal, state, affordable_available)) {
        adopt(state, f, proposal.source); return proposal;
    }

    std::array<bool, 8> available{};
    int generic = -1;
    for (int i = 0; i < f.source_count && i < 8; ++i) if ((available[i] = eligible(i)))
        if (generic < 0 || rate(f, i) > rate(f, generic) ||
            (rate(f, i) == rate(f, generic) && f.sources[i].id < f.sources[generic].id)) generic = i;
    const auto scores = design_scores(f, state, available);
    int designed = -1;
    for (int i = 0; i < f.source_count && i < 8; ++i) if (scores[i] > 0 &&
        (designed < 0 || scores[i] > scores[designed] ||
         (scores[i] == scores[designed] && f.sources[i].id < f.sources[designed].id))) designed = i;
    int selected = state.source;
    if (designed >= 0) {
        if (selected < 0 || scores[selected] <= 0) selected = designed;
        if (state.source < 0 && valid_source(f, proposal.source) && scores[proposal.source] > 0 &&
            scores[proposal.source] >= scores[designed] * .8) selected = proposal.source;
    } else if (selected < 0) {
        selected = generic;
        if (selected >= 0 && eligible(proposal.source) && rate(f, proposal.source) >= rate(f, selected) * .8)
            selected = proposal.source;
    }
    if (selected < 0) {
        if (disengaging_capture) { Task task; task.goal = Goal::recover; return task; }
        if (f.bag) { reset(state); return conversion; }
        if (proposal.goal == Goal::farm && valid_source(f, proposal.source)) {
            const Source& x = f.sources[proposal.source];
            if (!x.seen) { Task task = farm(proposal.source); task.goal = Goal::inspect; return task; }
            const float reserve = std::max(f.max_health * .55f, self_camp_incoming(f, x) * 3 + f.max_health * .2f);
            if (!x.feasible && !x.reward_available && f.health <= reserve) {
                Task task; task.goal = Goal::recover; return task;
            }
        }
        return proposal;
    }
    adopt(state, f, selected);
    Task task = farm(selected); task.port = f.sources[selected].port;
    if (selected == proposal.source && (proposal.goal == Goal::farm || proposal.goal == Goal::inspect) &&
        waypoint_supported(f, proposal)) {
        task.waypoint = proposal.waypoint; task.mechanism = proposal.mechanism;
        task.route = proposal.route == 1 && f.sources[selected].redundant ? 1 : 0;
        task.mobility = proposal.mobility && f.sources[selected].mobility && (f.dash_ready || f.teleport_ready);
        task.ward = proposal.ward && f.own_ward_ready && f.sources[selected].occluded;
        if (f.design == Design::veil && !f.sources[selected].occluded) task.mechanism = false;
    }
    task.goal = f.sources[selected].seen ? Goal::farm : Goal::inspect;
    return task;
}
} // namespace p7::live
