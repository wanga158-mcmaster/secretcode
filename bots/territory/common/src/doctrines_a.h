#pragma once
#include "native_types.h"

namespace p7::live {
namespace a_detail {
// All geographic sets below contain at most three of the eight retained sources.
// Scores already price combat, return, recovery and displaced production; these
// controllers change the geographic assignment, never add territorial bonuses.
struct Basin { std::array<bool, 8> member{}; int anchor = -1, count = 0; };
inline int home_distance(const Source& x) {
    return x.spawn_distance >= 0 ? x.spawn_distance : x.recovery_distance;
}
inline bool complete(const Source& x) {
    return x.exit_distance >= 0 && x.recovery_distance >= 0 && x.cycle_turns > 0;
}
inline int separation(const Frame& f, int a, int b) {
    if (!valid_source(f, a) || !valid_source(f, b)) return -1;
    return a == b ? 0 : f.distances[a][b];
}
inline Basin home_basin(const Frame& f) {
    Basin b;
    for (int k = 0; k < 3; ++k) {
        int next = -1;
        for (int i = 0; i < f.source_count && i < 8; ++i) {
            const Source& x = f.sources[i];
            if (b.member[i] || !x.home_connected || home_distance(x) < 0) continue;
            if (next < 0 || home_distance(x) < home_distance(f.sources[next]) ||
                (home_distance(x) == home_distance(f.sources[next]) && x.id < f.sources[next].id)) next = i;
        }
        if (next < 0) break;
        b.member[next] = true; ++b.count;
        if (b.anchor < 0) b.anchor = next;
    }
    return b;
}
inline Basin around(const Frame& f, int anchor) {
    Basin b; b.anchor = anchor;
    if (!valid_source(f, anchor)) return b;
    b.member[anchor] = true; b.count = 1;
    for (int k = 0; k < 2; ++k) {
        int next = -1;
        for (int i = 0; i < f.source_count && i < 8; ++i) {
            const int d = separation(f, anchor, i);
            if (b.member[i] || d < 0) continue;
            if (next < 0 || d < separation(f, anchor, next) ||
                (d == separation(f, anchor, next) && f.sources[i].id < f.sources[next].id)) next = i;
        }
        if (next < 0) break;
        b.member[next] = true; ++b.count;
    }
    return b;
}
inline int pick(const Frame& f, const Basin& b, int role = 0, bool joint = false) {
    std::array<int, 8> ranked{}; int count = 0;
    for (int i = 0; i < f.source_count && i < 8; ++i) if (b.member[i] && usable(f, i, joint)) {
        int p = count;
        while (p > 0 && compare_source(f.sources[i], f.sources[ranked[p - 1]]) > 0) {
            ranked[p] = ranked[p - 1]; --p;
        }
        ranked[p] = i; ++count;
    }
    return count ? ranked[role % count] : -1;
}
inline void initialize(const Frame& f, State& s) {
    if (!valid_source(f, s.home_source)) {
        Basin b = home_basin(f);
        s.home_source = usable(f, f.home) ? f.home : pick(f, b);
    }
}
inline Task bounded_fallback(const Frame& f, State& s) {
    Task t = fallback(f, s);
    if (t.goal != Goal::inspect || !valid_source(f, t.source)) return t;
    if (!s.inspection_failed) {
        admit(s, f, t.source);
        if (!attempt_expired(s, f)) return t;
        retire(s); s.inspection_failed = true;
    }
    t.goal = Goal::recover; t.source = s.home_source; return t;
}
inline Task work(const Frame& f, State& s, int source, bool mechanism = false) {
    if (!valid_source(f, source)) return bounded_fallback(f, s);
    admit(s, f, source); s.port = f.sources[source].port;
    Task t = farm(source, mechanism); t.port = s.port; return t;
}
inline Task bank(const State& s, bool mechanism = false) {
    Task t; t.goal = Goal::bank; t.source = s.source; t.port = s.port; t.mechanism = mechanism; return t;
}
inline Task home(const Frame& f, State& s) {
    if (f.bag) return bank(s);
    const int source = pick(f, home_basin(f), f.ordinal == 1 ? 0 : f.ordinal + 1);
    if (source >= 0 && s.initial_source < 0) s.ring = std::max(0, f.sources[source].ring);
    return source >= 0 ? work(f, s, source) : bounded_fallback(f, s);
}
inline bool at_source(const Frame& f, const Ally& a, int source) {
    return valid_source(f, source) && (a.group == source || a.tile == f.sources[source].tile);
}
inline bool helper_present(const Frame& f, int ordinal, int source) {
    const Ally* a = visible_ally(f, ordinal);
    return a && a->can_attack && a->health > a->max_health * .45f && at_source(f, *a, source);
}
inline bool new_payout(const State& s) { return s.payout_seen && s.captures > s.captures_at_admission; }
inline bool completed(const State& s) {
    return new_payout(s) && s.bank_seen && s.banks > s.banks_at_admission;
}
inline int uninspected(const Frame& f, const State& s, const Basin& excluded, int min_ring = -1) {
    int target = -1;
    for (int i = 0; i < f.source_count && i < 8; ++i) {
        const Source& x = f.sources[i];
        if (excluded.member[i] || retired(s, f, i) || x.seen || !x.reachable || !complete(x) ||
            (min_ring >= 0 && x.ring <= min_ring)) continue;
        if (target < 0 || x.distance < f.sources[target].distance ||
            (x.distance == f.sources[target].distance && x.id < f.sources[target].id)) target = i;
    }
    return target;
}
inline Task inspect(const Frame& f, State& s, int source, bool mechanism = true) {
    admit(s, f, source);
    if (attempt_expired(s, f)) {
        retire(s); s.inspection_failed = true;
        return home(f, s);
    }
    Task t = farm(source, mechanism); t.goal = Goal::inspect; t.port = f.sources[source].port; return t;
}
inline Task recovery(const Frame& f, State& s) {
    if (f.bag) return bank(s);
    Task t; t.goal = Goal::recover; t.source = s.home_source; return t;
}
inline bool poor_health(const Frame& f) { return f.max_health > 0 && f.health < f.max_health * .3f; }
inline int outer(const Frame& f, const State& s, const Basin& rear, bool joint = false) {
    return best(f, [&](int i, const Source& x) {
        return !rear.member[i] && complete(x) && !retired(s, f, i);
    }, joint);
}
inline Task visible_heal(const Frame& f, int source, bool mechanism) {
    Task t = farm(source, mechanism);
    if (!f.healing_ready) return t;
    const Ally* needy = nullptr;
    for (int i = 0; i < f.ally_count && i < 6; ++i) {
        const Ally& a = f.allies[i];
        if (!a.visible || a.max_health <= 0 || a.health >= a.max_health * .75f || a.health <= 0) continue;
        if (!needy || a.health / a.max_health < needy->health / needy->max_health) needy = &a;
    }
    if (needy) { t.goal = Goal::guard; t.peer = needy->id; t.waypoint = needy->tile; t.heal = true; }
    return t;
}
} // namespace a_detail

inline Task choose_01(const Frame& f, State& s) {
    using namespace a_detail;
    initialize(f, s);
    if (f.control || f.ordinal >= 3) return f.bag ? bank(s) : work(f, s, best(f));
    if (f.bag) return bank(s, s.relocated);
    if (f.ordinal == 1) return home(f, s); // The inner producer never migrates.
    if (s.relocated && completed(s)) { s.ring = f.sources[s.source].ring; s.relocated = false; }
    if (s.relocated && usable(f, s.source) && !f.sources[s.source].pressured)
        return work(f, s, s.source, true);
    if (s.relocated) { retire(s); s.relocated = false; return home(f, s); }
    // Gear without an actually captured and banked opening is insufficient.
    if (s.captures == 0 || s.banks == 0 || f.kit_level == 0) return home(f, s);
    const Basin rear = home_basin(f);
    int next_ring = std::numeric_limits<int>::max();
    for (int i = 0; i < f.source_count && i < 8; ++i) {
        const Source& x = f.sources[i];
        if (!rear.member[i] && x.ring > s.ring && x.seen && complete(x) && usable(f, i) &&
            !retired(s, f, i) && (f.ordinal == 0 || helper_present(f, 0, i))) next_ring = std::min(next_ring, x.ring);
    }
    int target = best(f, [&](int i, const Source& x) {
        return !rear.member[i] && x.ring == next_ring && x.seen && complete(x) && !retired(s, f, i) &&
            (f.ordinal == 0 || helper_present(f, 0, i));
    });
    if (target >= 0) { s.relocated = true; return work(f, s, target, true); }
    // Only Tavi makes the first personal verification trip. Varg needs contact.
    if (f.ordinal == 0 && !s.inspection_failed) {
        target = uninspected(f, s, rear, s.ring);
        if (target >= 0) return inspect(f, s, target);
    }
    return home(f, s);
}

namespace a_detail {
struct Wings {
    int left = -1, right = -1, reserve = -1, response = 0;
    std::array<int, 3> value{};
    std::array<int, 3> independent{};
    bool responsive = false;
};
inline bool better_layout(const Wings& a, const Wings& b) {
    if (b.left < 0) return a.left >= 0;
    const int am = *std::min_element(a.value.begin(), a.value.end());
    const int bm = *std::min_element(b.value.begin(), b.value.end());
    if (am != bm) return am > bm;
    if (a.value[1] != b.value[1]) return a.value[1] > b.value[1];
    if (a.response != b.response) return a.response < b.response;
    return a.left < b.left || (a.left == b.left && a.right < b.right);
}
inline Wings wing_layout(const Frame& f) {
    Wings compact, rich;
    for (int a = 0; a < f.source_count && a < 8; ++a) for (int b = a + 1; b < f.source_count && b < 8; ++b) {
        if (!usable(f, a) || !usable(f, b)) continue;
        Wings proposal; proposal.left = a; proposal.right = b;
        int response = std::numeric_limits<int>::max();
        for (int r = 0; r < f.source_count && r < 8; ++r) {
            const int da = separation(f, r, a), db = separation(f, r, b);
            if (r == a || r == b || !usable(f, r) || da < 0 || db < 0) continue;
            int earlier_pads = 0;
            for (int q = 0; q < f.source_count && q < 8; ++q)
                if (usable(f, q) && (f.sources[q].tile < f.sources[r].tile ||
                    (f.sources[q].tile == f.sources[r].tile && f.sources[q].id < f.sources[r].id))) ++earlier_pads;
            if (earlier_pads >= 4) continue;
            if (da + db < response || (da + db == response && r < proposal.reserve)) {
                proposal.reserve = r; response = da + db;
                proposal.responsive = da <= 10 && db <= 10; // Eight walking turns + two collision slack.
            }
        }
        if (proposal.reserve < 0) continue;
        proposal.response = response;
        for (int k = 0; k < 3; ++k) {
            proposal.independent[k] = f.sources[a].score[k] + f.sources[b].score[k];
            proposal.value[k] = proposal.independent[k] + f.sources[proposal.reserve].score[k];
        }
        const int pm = *std::min_element(proposal.independent.begin(), proposal.independent.end());
        const int rm = *std::min_element(rich.independent.begin(), rich.independent.end());
        if (rich.left < 0 || pm > rm || (pm == rm && proposal.independent[1] > rich.independent[1])) rich = proposal;
        if (proposal.responsive && (compact.left < 0 || proposal.response < compact.response ||
            (proposal.response == compact.response && better_layout(proposal, compact)))) compact = proposal;
    }
    if (f.control) return rich;
    // Exactly the compact-response and independent-yield proposals are compared.
    return rich.responsive && better_layout(rich, compact) ? rich : compact;
}
} // namespace a_detail

inline Task choose_02(const Frame& f, State& s) {
    using namespace a_detail;
    initialize(f, s);
    if (f.bag) return bank(s, !f.control);
    if (f.ordinal >= 3) return work(f, s, best(f));
    const Wings layout = wing_layout(f);
    if (layout.left < 0) return home(f, s);
    const int source = f.ordinal == 0 ? layout.reserve : f.ordinal == 1 ? layout.left : layout.right;
    Task t = work(f, s, source, !f.control);
    if (f.ordinal != 0) return t;
    // Both arms use this identical, personally witnessed, bounded intervention.
    if (f.fresh_pressure && f.enemy_count > 0 && s.handoff_epoch != s.epoch) {
        s.phase = 1; s.last_seen_pressure = f.round; s.handoff_epoch = s.epoch;
    }
    if (s.phase == 1 && f.round - s.last_seen_pressure < 8 && f.fresh_pressure && f.pressure_tile >= 0) {
        t.goal = Goal::guard; t.waypoint = f.pressure_tile; t.ward = f.own_ward_ready;
    } else if (s.phase == 1) s.phase = 2;
    return t;
}

inline Task choose_03(const Frame& f, State& s) {
    using namespace a_detail;
    initialize(f, s);
    const Basin rear = home_basin(f);
    if (f.ordinal >= 3) return f.bag ? bank(s) : work(f, s, best(f));
    if (f.bag) return bank(s, !f.control && s.relocated);
    if (f.control) return work(f, s, pick(f, rear, f.ordinal, true));
    if (poor_health(f)) return recovery(f, s);
    if (s.relocated && valid_source(f, s.source) && f.sources[s.source].pressured) {
        retire(s); s.relocated = false; s.phase = 0; return home(f, s);
    }
    if (s.relocated && completed(s)) {
        if (f.ordinal == 1 && usable(f, s.source)) s.phase = 2; // A genuinely self-sufficient resident.
        else { s.relocated = false; s.phase = 0; return home(f, s); }
    }
    if (s.phase == 2 && usable(f, s.source)) return work(f, s, s.source, true);
    if (s.relocated) {
        if (usable(f, s.source) || (usable(f, s.source, true) &&
            (helper_present(f, 0, s.source) || helper_present(f, 1, s.source)))) {
            Task t = work(f, s, s.source, true);
            return f.ordinal == 2 ? visible_heal(f, t.source, true) : t;
        }
        if (!attempt_expired(s, f)) { Task t = farm(s.source, true); t.goal = Goal::rally; return t; }
        retire(s); s.relocated = false; s.inspection_failed = true; return home(f, s);
    }
    if (s.captures == 0 || s.banks == 0 || s.inspection_failed) return home(f, s);
    const int target = best(f, [&](int i, const Source& x) {
        return !rear.member[i] && !s.captured[i] && !retired(s, f, i) && x.seen && complete(x) &&
            (x.feasible || (x.joint_feasible && f.ordinal != 1 &&
             (helper_present(f, 0, i) || helper_present(f, 1, i))));
    }, true);
    if (target >= 0) { s.relocated = true; return work(f, s, target, true); }
    if (f.ordinal == 0) {
        const int unknown = uninspected(f, s, rear);
        if (unknown >= 0) return inspect(f, s, unknown);
    }
    return home(f, s);
}

inline Task choose_04(const Frame& f, State& s) {
    using namespace a_detail;
    initialize(f, s);
    const auto connected = [&](int, const Source& x) { return !x.mobility && x.home_connected && complete(x); };
    if (f.bag) { Task t = bank(s, s.relocated); t.mobility = s.relocated; return t; }
    if (f.control) return work(f, s, best(f, connected));
    if (s.relocated && completed(s)) {
        s.relocated = false; retire(s);
        return work(f, s, best(f, connected)); // Bank, then actual redeployment.
    }
    if (s.relocated && usable(f, s.source) && f.sources[s.source].mobility) {
        Task t = work(f, s, s.source, true); t.mobility = true; return t;
    }
    if (s.relocated) { retire(s); s.relocated = false; return home(f, s); }
    // Supplier/extra workers do not inherit somebody else's mobility ability.
    const bool funded_supplier = f.ordinal != 2 || (f.teleport_ready && f.gold > 0);
    if (s.captures == 0 || s.banks == 0 || !funded_supplier || (!f.dash_ready && !f.teleport_ready))
        return work(f, s, best(f, connected));
    const int ordinary = best(f, connected);
    const int island = best(f, [&](int i, const Source& x) {
        return x.mobility && x.seen && complete(x) && !retired(s, f, i);
    });
    if (island >= 0 && (ordinary < 0 || compare_source(f.sources[island], f.sources[ordinary]) > 0)) {
        s.relocated = true; Task t = work(f, s, island, true); t.mobility = true; return t;
    }
    return work(f, s, ordinary);
}

inline Task choose_05(const Frame& f, State& s) {
    using namespace a_detail;
    initialize(f, s);
    const auto redundant = [](int, const Source& x) {
        return x.redundant && x.alternate_port >= 0 && x.alternate_port != x.port && complete(x);
    };
    if (f.bag) {
        Task t = bank(s, !f.control && s.relocated);
        if (!f.control && valid_source(f, s.source) && redundant(s.source, f.sources[s.source])) {
            if (f.fresh_pressure && f.sources[s.source].pressured) s.phase = 2;
            if (s.phase == 2) { t.route = 1; t.port = f.sources[s.source].alternate_port; }
        }
        return t;
    }
    if (f.control || f.ordinal >= 3) return work(f, s, best(f));
    if (s.captures == 0 || s.banks == 0) return home(f, s);
    const int district = best(f, redundant);
    if (district < 0) return home(f, s);
    if (f.ordinal == 2) {
        int support = best(f, [&](int i, const Source&) {
            const int d = separation(f, i, district); return i != district && d >= 0 && d <= 10;
        });
        if (support < 0) return home(f, s);
        Task t = work(f, s, support, true);
        if (f.fresh_pressure && helper_present(f, 0, district) && !attempt_expired(s, f)) {
            t.goal = Goal::guard; t.waypoint = f.sources[district].connector;
        }
        return t;
    }
    if (s.source != district) s.phase = 1;
    s.relocated = true;
    return work(f, s, district, true);
}

inline Task choose_06(const Frame& f, State& s) {
    using namespace a_detail;
    initialize(f, s);
    if (f.bag) return bank(s, !f.control && s.relocated);
    if (f.ordinal >= 3) return work(f, s, best(f));
    const int enclave = f.control ? best(f, [](int, const Source& x) { return !x.local_gate && !x.cut_proven; }) : best(f, [](int, const Source& x) {
        return (x.local_gate || x.cut_proven) && x.connector >= 0 && complete(x);
    });
    if (enclave < 0) return home(f, s);
    const Source& anchor = f.sources[enclave];
    if (f.ordinal == 0) {
        const int job = best(f, [&](int i, const Source& x) {
            const int d = separation(f, i, enclave);
            return i != enclave && x.home_connected && d >= 0 && d <= 10;
        });
        Task t = job >= 0 ? work(f, s, job, !f.control) : home(f, s);
        // The same short guard action is applied to both layouts.
        const int incident = f.pressure_tile >= 0 ? f.pressure_tile : anchor.connector;
        if (f.fresh_pressure && f.enemy_count > 0 && incident >= 0) {
            if (s.handoff_epoch != s.epoch) {
                s.handoff_epoch = s.epoch; s.last_seen_pressure = f.round;
            }
            if (s.last_seen_pressure >= 0 && f.round - s.last_seen_pressure < 8 && !poor_health(f)) {
                t.goal = Goal::guard; t.waypoint = incident; t.ward = f.own_ward_ready;
            }
        }
        return t;
    }
    const Ally* guard = visible_ally(f, 0);
    bool contact = guard && guard->can_attack && guard->health > guard->max_health * .45f &&
        (guard->tile == anchor.connector || at_source(f, *guard, enclave) ||
         (valid_source(f, guard->group) && separation(f, guard->group, enclave) >= 0 &&
          separation(f, guard->group, enclave) <= 10));
    if (contact) s.last_arrival = f.round;
    if (anchor.pressured && f.fresh_pressure) {
        s.relocated = false; retire(s);
        Task t = home(f, s); t.goal = Goal::withdraw; return t;
    }
    if (!f.control && (!contact && (s.last_arrival < 0 || f.round - s.last_arrival > 8))) return home(f, s);
    int target = enclave;
    if (!f.control) target = best(f, [&](int, const Source& x) {
        return x.connector == anchor.connector && complete(x) && !x.pressured;
    });
    if (target < 0) return home(f, s);
    s.relocated = !f.control;
    return work(f, s, target, !f.control);
}

inline Task choose_07(const Frame& f, State& s) {
    using namespace a_detail;
    initialize(f, s);
    if (f.bag) return bank(s, !f.control && s.relocated);
    if (f.control || f.ordinal >= 3) return work(f, s, best(f));
    if (s.inspection_failed) return home(f, s);
    int connector = -1;
    for (int i = 0; i < f.source_count && i < 8; ++i) {
        const Source& x = f.sources[i];
        if (!x.cut_proven || x.connector < 0 || !x.seen || !complete(x) || retired(s, f, i)) continue;
        bool opens_work = false;
        for (int j = 0; j < f.source_count && j < 8; ++j)
            if (f.sources[j].opened_by == i && complete(f.sources[j])) opens_work = true;
        if (opens_work && (connector < 0 || compare_source(x, f.sources[connector]) > 0)) connector = i;
    }
    if (connector < 0) return home(f, s);
    const Source& gate = f.sources[connector];
    const Ally* holder = visible_ally(f, 2);
    const bool observed_payout = s.captured[connector] || (holder && at_source(f, *holder, connector) &&
        holder->bag > 0 && gate.respawning && !gate.alive && !gate.reward_available);
    const bool holder_vacated = (f.ordinal == 2 && f.tile != gate.connector) ||
        (holder && holder->tile != gate.connector);
    if (observed_payout && holder_vacated && !gate.alive && !gate.reward_available && gate.ready_after > 0) {
        const int downstream = best(f, [&](int, const Source& x) {
            return x.opened_by == connector && x.seen && complete(x) &&
                x.cycle_turns + 2 < gate.ready_after;
        });
        if (downstream >= 0) {
            s.relocated = true;
            Task t = work(f, s, downstream, true); t.waypoint = gate.connector;
            t.holder_ordinal = 2; return t;
        }
        // Vacate the camp after payout; a blocked respawn timer is no free road.
        s.relocated = false; return home(f, s);
    }
    if (!gate.alive && !gate.reward_available) return home(f, s);
    if (!usable(f, connector) && !helper_present(f, f.ordinal == 0 ? 1 : 0, connector)) {
        if (!s.active || s.source != connector) admit(s, f, connector);
        if (attempt_expired(s, f)) { retire(s); s.inspection_failed = true; return home(f, s); }
        Task t = farm(connector, true); t.goal = Goal::rally; t.holder_ordinal = 2; return t;
    }
    s.relocated = true;
    Task t = work(f, s, connector, true); t.holder_ordinal = 2;
    if (!gate.alive && gate.reward_available && f.ordinal != 2 && holder && holder->at_capture &&
        at_source(f, *holder, connector) && holder->health > holder->max_health * .45f)
        t.release_helper = true;
    return t;
}

inline Task choose_08(const Frame& f, State& s) {
    using namespace a_detail;
    initialize(f, s);
    if (f.bag) return bank(s, !f.control);
    if (poor_health(f)) return recovery(f, s); // Identical restoration rule in both arms.
    if (f.ordinal >= 3) return work(f, s, best(f));
    const Basin basin = f.control ? around(f, best(f)) : home_basin(f);
    int source = pick(f, basin, f.ordinal == 2 ? 1 : 0);
    if (f.ordinal == 0) {
        // Repeated heavy work stays inside the short restoration circuit.
        source = best(f, [&](int i, const Source& x) {
            return basin.member[i] && x.incoming > 0 && complete(x);
        });
        if (source < 0) source = pick(f, basin);
    }
    if (source < 0) return home(f, s);
    Task t = work(f, s, source, !f.control);
    if (f.ordinal == 1) {
        const Ally* heavy = visible_ally(f, 0);
        if (heavy && !heavy->at_capture && (heavy->health < heavy->max_health * .3f || heavy->tile == f.spawn))
            return t; // His actual recovery leaves Willow on her own production.
        Task heal = visible_heal(f, source, !f.control);
        if (heal.heal) {
            if (s.handoff_epoch != s.epoch) {
                s.handoff_epoch = s.epoch; s.last_seen_pressure = f.round;
            }
            if (s.last_seen_pressure >= 0 && f.round - s.last_seen_pressure < 8) return heal;
        }
    }
    return t;
}
} // namespace p7::live
