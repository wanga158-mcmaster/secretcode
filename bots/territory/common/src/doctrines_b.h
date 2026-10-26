#pragma once
#include "native_types.h"

namespace p7::live {
// Port/ring are public geography labels. All admission values below are the
// runtime's complete, personally observed source->capture->bank->recovery cycles.
inline Task b_bank(int port = -1, bool mechanism = false) {
    Task t; t.goal = Goal::bank; t.port = port; t.mechanism = mechanism; return t;
}
inline Task b_observe(int source, bool mechanism = true) {
    Task t; t.goal = Goal::inspect; t.source = source; t.mechanism = mechanism; return t;
}
inline Task b_withdraw(int source) {
    Task t = farm(source, true); t.goal = Goal::withdraw; return t;
}
inline bool b_preserve(const Frame& f) { return f.channel_active || f.safe_capture; }
inline Task b_preserved(const Frame& f, const State& s) {
    if (f.channel_active && f.bag) return b_bank();
    return farm(valid_source(f, f.current) ? f.current : s.source);
}
inline int b_port(const Frame& f, int port, int except = -1) {
    return best(f, [=](int i, const Source& c) {
        return i != except && c.port == port && !c.pressured;
    });
}
inline int b_other_port(const Frame& f, int port) {
    return best(f, [=](int, const Source& c) { return c.port >= 0 && c.port != port && !c.pressured; });
}
inline int b_inner(const Frame& f) {
    int lowest = std::numeric_limits<int>::max();
    for (int i = 0; i < f.source_count && i < 8; ++i)
        if (usable(f, i) && !f.sources[i].pressured && f.sources[i].ring >= 0)
            lowest = std::min(lowest, f.sources[i].ring);
    return best(f, [=](int, const Source& c) { return c.ring == lowest && !c.pressured; });
}
inline int b_forward(const Frame& f, int except = -1) {
    int highest = -1;
    for (int i = 0; i < f.source_count && i < 8; ++i)
        if (usable(f, i) && i != except && !f.sources[i].pressured)
            highest = std::max(highest, f.sources[i].ring);
    return best(f, [=](int i, const Source& c) {
        return i != except && c.ring == highest && !c.pressured;
    });
}
inline int b_assignment(const Frame& f, int port, int ordinal) {
    // A basin holds at most three source tokens. Fixed ordinals distribute
    // initial independent work; this is never a remote instruction to an ally.
    std::array<int, 3> tokens{-1, -1, -1};
    for (int i = 0; i < f.source_count && i < 8; ++i) if (usable(f, i) &&
        f.sources[i].port == port && !f.sources[i].pressured) {
        for (int j = 0; j < 3; ++j) if (tokens[j] < 0 ||
            compare_source(f.sources[i], f.sources[tokens[j]]) > 0) {
            for (int k = 2; k > j; --k) tokens[k] = tokens[k - 1];
            tokens[j] = i; break;
        }
    }
    int n = 0; while (n < 3 && tokens[n] >= 0) ++n;
    return n ? tokens[ordinal % n] : -1;
}
inline void b_start(State& s, const Frame& f, int source) {
    if (!valid_source(f, source)) return;
    admit(s, f, source);
    if (s.port < 0) s.port = f.sources[source].port;
    if (!valid_source(f, s.home_source)) s.home_source = source;
}
inline bool b_pressure(const Frame& f, int source, int port) {
    return f.fresh_pressure && ((port >= 0 && f.pressure_port == port) ||
        (valid_source(f, source) && f.sources[source].pressured));
}
inline bool b_finish_bank(const Frame& f, State& s) {
    return !f.bag && !s.bank_pending && s.payout_seen && s.bank_seen && s.banks > s.banks_at_admission;
}
inline Task b_resident(const Frame& f, State& s, bool mechanism = false) {
    if (f.bag) return b_bank(s.port, mechanism);
    if (usable(f, s.source) && f.sources[s.source].port == s.port && !f.sources[s.source].pressured)
        return farm(s.source, mechanism);
    int next = b_port(f, s.port);
    if (next >= 0) { bind_source(s, next); return farm(next, mechanism); }
    return fallback(f, s);
}

inline Task choose_09(const Frame& f, State& s) {
    if (b_preserve(f)) return b_preserved(f, s);
    if (!s.active) {
        int source = b_assignment(f, f.port_count > 1 ? f.ordinal % 2 : 0, f.ordinal / 2);
        if (source < 0) source = best(f);
        b_start(s, f, source);
    }
    // The control has the same emergency banking executor, but keeps its
    // production catchment. Existing bags are never allocated to another actor.
    if (s.pending_port >= 0 && b_finish_bank(f, s)) {
        if (!f.control && usable(f, s.region) && !f.sources[s.region].pressured) {
            s.port = s.pending_port; bind_source(s, s.region); s.relocated = true;
            s.handoff_epoch = s.epoch; s.last_arrival = f.round;
        }
        s.pending_port = -1;
    }
    if (s.pending_port >= 0 && !f.bag && !s.bank_pending &&
        !s.bank_seen) {
        // A vanished bag (for example, death) is not a completed conversion.
        s.pending_port = -1; s.handoff_epoch = s.epoch; s.inspection_failed = true;
    }
    int alternate = b_other_port(f, s.port);
    const bool two_catchments = alternate >= 0 &&
        (b_port(f, s.port) >= 0 || valid_source(f, s.initial_source));
    if (!f.control && two_catchments && s.pending_port < 0 &&
        s.handoff_epoch != s.epoch && b_pressure(f, s.source, s.port) &&
        improves(f, alternate, s.source)) {
        s.region = alternate; s.pending_port = f.sources[alternate].port;
        s.attempt_started = f.round; s.banks_at_admission = s.banks;
        if (f.bag) { s.bank_pending = true; return b_bank(s.pending_port, true); }
        s.port = s.pending_port; s.pending_port = -1; bind_source(s, alternate);
        s.relocated = true; s.handoff_epoch = s.epoch; s.last_arrival = f.round;
    }
    if (f.bag) return b_bank(alternate >= 0 && b_pressure(f, s.source, s.port)
        ? f.sources[alternate].port : s.port, s.relocated);
    // A single visible rendezvous can license the acting producer's change of
    // responsibility. No absent producer is reassigned or promised as support.
    Task t = b_resident(f, s, s.relocated);
    if (s.relocated && f.round - s.last_arrival < 8) {
        for (int i = 0; i < f.ally_count && i < 6; ++i)
            if (f.allies[i].visible && f.allies[i].group == t.source) { t.peer = f.allies[i].id; break; }
    }
    return t;
}

inline Task choose_10(const Frame& f, State& s) {
    if (b_preserve(f)) return b_preserved(f, s);
    if (f.ordinal >= 3) return fallback(f, s);
    if (!s.active) {
        // The fixed core starts in the cheapest independently viable basin.
        int seed = -1;
        for (int i = 0; i < f.source_count && i < 8; ++i) if (usable(f, i) &&
            !f.sources[i].pressured && (seed < 0 ||
            f.sources[i].cycle_turns < f.sources[seed].cycle_turns ||
            (f.sources[i].cycle_turns == f.sources[seed].cycle_turns && f.sources[i].id < f.sources[seed].id))) seed = i;
        int source = seed < 0 ? -1 : b_assignment(f, f.sources[seed].port, f.ordinal);
        b_start(s, f, source); s.region = s.port;
    }
    if (f.control || s.inspection_failed) return b_resident(f, s);
    if (s.phase == 1 && f.bag) { s.bank_pending = true; return b_bank(s.port, true); }
    if (s.phase == 1 && !f.bag) {
        if (b_finish_bank(f, s)) { s.phase = 2; s.attempt_started = f.round; }
        else if (!s.bank_pending) {
            s.inspection_failed = true; s.phase = 4; s.pending_port = -1;
            return b_withdraw(s.home_source);
        } else return b_bank(s.port, true);
    }
    if (s.phase == 2) {
        int destination = s.previous_source;
        if (valid_source(f, destination) && f.sources[destination].seen &&
            usable(f, destination) && !f.sources[destination].pressured && f.current == destination) {
            s.port = s.pending_port; s.pending_port = -1; bind_source(s, destination);
            s.phase = 3; s.relocated = true; s.last_arrival = f.round;
            s.captures_at_admission = s.captures; s.banks_at_admission = s.banks;
        } else if (attempt_expired(s, f) || (valid_source(f, destination) &&
            f.sources[destination].seen && f.sources[destination].pressured)) {
            s.inspection_failed = true; s.pending_port = -1; s.phase = 4;
            s.port = valid_source(f, s.home_source) ? f.sources[s.home_source].port : s.region;
            return f.bag ? b_bank() : b_withdraw(s.home_source);
        } else return b_observe(destination);
    }
    // Leaving and returning both require a profitable full cycle and current
    // personal evidence. No timer expiry alone certifies renewed supply.
    int destination = b_other_port(f, s.port);
    int local = b_port(f, s.port);
    const bool completed_residence = s.phase == 3 && s.payout_seen && s.bank_seen &&
        s.banks > s.banks_at_admission && s.captures > s.captures_at_admission;
    if ((s.phase == 0 || s.phase == 3) && (s.handoff_epoch != s.epoch || completed_residence) && destination >= 0 &&
        f.sources[destination].seen && improves(f, destination, local) &&
        ((s.payout_seen && s.captures > s.captures_at_admission) || local < 0)) {
        s.previous_source = destination; s.pending_port = f.sources[destination].port;
        s.handoff_epoch = s.epoch; s.phase = f.bag ? 1 : 2; s.attempt_started = f.round;
        s.banks_at_admission = s.banks;
        if (f.bag) { s.bank_pending = true; return b_bank(s.port, true); }
        return b_observe(destination);
    }
    Task t = b_resident(f, s, s.relocated);
    // Marcella is a holder only when present at this real group. Otherwise each
    // actor owns its independent payout; nobody credits an unseen collector.
    const Ally* collector = visible_ally(f, 2);
    if (collector && collector->group == t.source && collector->health > collector->max_health / 2)
        t.holder_ordinal = 2;
    return t;
}

inline Task choose_11(const Frame& f, State& s) {
    if (b_preserve(f)) return b_preserved(f, s);
    int inner = b_inner(f);
    if (!s.active) {
        int source = f.ordinal < 2 ? b_forward(f, inner) : inner;
        if (source < 0) source = inner;
        b_start(s, f, source); s.home_source = inner; s.region = source;
    }
    if (f.ordinal >= 2) return f.ordinal == 2 && inner >= 0 ?
        (f.bag ? b_bank(f.sources[inner].port) : farm(inner)) : fallback(f, s);
    if (f.control) {
        if (b_pressure(f, s.region, s.port) && inner >= 0)
            return f.bag ? b_bank(f.sources[inner].port) : b_withdraw(inner);
        return usable(f, s.region) ? (f.bag ? b_bank(s.port) : farm(s.region)) : fallback(f, s);
    }
    if (s.phase == 0 && inner >= 0 && b_pressure(f, s.region, s.port) && improves(f, inner, s.region)) {
        s.phase = f.bag ? 1 : 2; s.relocated = true; s.pending_port = f.sources[inner].port;
        s.banks_at_admission = s.banks;
        if (!f.bag) { bind_source(s, inner); s.port = s.pending_port; s.pending_port = -1; }
    }
    if (s.phase == 1) {
        if (f.bag) { s.bank_pending = true; return b_bank(s.pending_port, true); }
        if (!s.bank_pending) {
            if (!b_finish_bank(f, s)) s.inspection_failed = true;
            bind_source(s, inner >= 0 ? inner : s.home_source); s.phase = 2;
            s.port = s.pending_port; s.pending_port = -1;
        }
        else return b_bank(s.pending_port, true);
    }
    if (s.phase == 2) {
        // Supply is reverified, access is currently favorable, and a failed
        // re-entry consumes the one attempt in the latched outer project.
        if (!s.inspection_failed && valid_source(f, s.region) && f.sources[s.region].seen &&
            !f.sources[s.region].pressured && improves(f, s.region, s.source) &&
            s.handoff_epoch != s.epoch) {
            s.phase = 3; s.handoff_epoch = s.epoch; s.attempt_started = f.round;
            return b_observe(s.region);
        }
        return f.bag ? b_bank(s.port, true) : b_withdraw(usable(f, s.source) ? s.source : inner);
    }
    if (s.phase == 3) {
        if (b_pressure(f, s.region, valid_source(f, s.region) ? f.sources[s.region].port : -1) || attempt_expired(s, f)) {
            s.inspection_failed = true; s.phase = 2;
            return f.bag ? b_bank(s.port, true) : b_withdraw(inner);
        }
        if (usable(f, s.region) && f.sources[s.region].seen && f.current == s.region) {
            bind_source(s, s.region); s.port = f.sources[s.source].port; s.phase = 0;
        } else return b_observe(s.region);
    }
    return b_resident(f, s, s.relocated);
}

inline Task choose_12(const Frame& f, State& s) {
    if (b_preserve(f)) return b_preserved(f, s);
    if (!s.active) {
        int port = f.port_count > 1 && f.ordinal == 1 ? 1 : 0;
        int source = b_assignment(f, port, f.ordinal == 2 ? 1 : 0);
        if (source < 0) source = best(f);
        b_start(s, f, source); s.region = s.port;
    }
    if (f.ordinal >= 3) return fallback(f, s);
    if (f.ordinal < 2) {
        // Each front producer contracts from its own current observation only.
        int inner = b_inner(f);
        if (b_pressure(f, s.source, s.port) && improves(f, inner, s.source))
            return f.bag ? b_bank(f.sources[inner].port) : b_withdraw(inner);
        return b_resident(f, s);
    }
    if (f.control) return b_resident(f, s);
    if (s.phase == 0 && s.handoff_epoch != s.epoch && b_pressure(f, s.source, s.port)) {
        int opposite = b_other_port(f, s.port);
        if (opposite >= 0 && f.sources[opposite].seen && improves(f, opposite, s.source)) {
            s.previous_source = opposite; s.pending_port = f.sources[opposite].port;
            s.phase = f.bag ? 1 : 2; s.handoff_epoch = s.epoch; s.attempt_started = f.round;
            s.banks_at_admission = s.banks;
        }
    }
    if (s.phase == 1) {
        if (f.bag) { s.bank_pending = true; return b_bank(s.pending_port, true); }
        if (s.bank_pending) return b_bank(s.pending_port, true);
        if (!b_finish_bank(f, s)) {
            s.phase = 4; s.inspection_failed = true; s.pending_port = -1;
            return b_withdraw(s.home_source);
        }
        s.phase = 2; s.attempt_started = f.round;
    }
    if (s.phase == 2) {
        if (attempt_expired(s, f) || !usable(f, s.previous_source) ||
            f.sources[s.previous_source].pressured) {
            s.phase = 4; s.inspection_failed = true; s.pending_port = -1;
            return f.bag ? b_bank() : b_withdraw(s.home_source);
        }
        if (f.current != s.previous_source) return b_observe(s.previous_source);
        bind_source(s, s.previous_source); s.port = s.pending_port; s.pending_port = -1;
        s.phase = 3; s.relocated = true; s.last_arrival = f.round;
    }
    if (s.phase == 3 && !f.bag) {
        const Ally* producer = visible_ally(f, s.port == 0 ? 0 : 1);
        if (producer && producer->group == f.current && producer->health > producer->max_health / 2) {
            int joint = best(f, [&](int, const Source& c) {
                return c.port == s.port && c.seen && !c.pressured && c.joint_feasible;
            }, true);
            if (joint >= 0 && f.sources[joint].minimum() > f.sources[s.source].minimum()) {
                Task t = farm(joint, true); t.peer = producer->id; t.holder_ordinal = producer->ordinal;
                return t;
            }
        }
    }
    return b_resident(f, s, s.relocated);
}

// Only this card reads denied enemy completions. These are three alternative
// enemy responses, never three additive rewards. All must beat the same force's
// independent productive cycles, and none may exceed the witnessed current bag.
inline bool b_denial(const Frame& f) {
    if (!f.denial_certificate || f.altar_count > 2 || f.denial_observed_round != f.round ||
        f.denial_enemy < 0 || f.denial_waypoint < 0 || !f.denial_bag) return false;
    bool witnessed = false;
    for (int i = 0; i < f.enemy_count && i < 2; ++i)
        if (f.enemies[i].visible && f.enemies[i].id == f.denial_enemy &&
            f.enemies[i].bag == f.denial_bag) witnessed = true;
    if (!witnessed) return false;
    for (int i = 0; i < 3; ++i)
        if (f.denial[i] <= f.denial_force_independent[i] || f.denial[i] < 0 ||
            static_cast<unsigned>(f.denial[i]) > f.denial_bag) return false;
    return true;
}
inline Task choose_13(const Frame& f, State& s) {
    if (b_preserve(f)) return b_preserved(f, s);
    if (!s.active) b_start(s, f, best(f));
    if (f.bag) return b_bank();
    if (f.control || f.ordinal >= 2) return b_resident(f, s);
    const bool qualifies = b_denial(f);
    if (s.phase == 0 && qualifies && s.handoff_epoch != s.epoch) {
        s.phase = 1; s.handoff_epoch = s.epoch; s.attempt_started = f.round;
    }
    if (s.phase == 1) {
        if (qualifies) s.last_seen_pressure = f.round;
        if (s.last_seen_pressure < 0 || f.round - s.last_seen_pressure >= 8) {
            s.phase = 2; s.inspection_failed = true;
            return fallback(f, s);
        }
        // Losing the cert immediately removes guaranteed credit and suspends
        // campaigning; a fresh observation may resume only this bounded attempt.
        if (qualifies) {
            Task t; t.goal = Goal::pressure; t.waypoint = f.denial_waypoint;
            t.port = f.denial_port; t.peer = f.denial_enemy; t.mechanism = true;
            return t;
        }
    }
    return b_resident(f, s);
}

inline Task choose_14(const Frame& f, State& s) {
    if (b_preserve(f)) return b_preserved(f, s);
    if (f.ordinal >= 2) return fallback(f, s);
    if (!s.active) {
        int home = b_inner(f), raid = b_forward(f, home);
        b_start(s, f, raid >= 0 ? raid : home);
        s.home_source = home; s.region = s.port; s.raid_captures = s.captures;
    }
    if (s.phase == 2) {
        if (f.bag) return b_bank();
        int home = usable(f, s.home_source) ? s.home_source : b_other_port(f, s.region);
        return home >= 0 ? b_withdraw(home) : fallback(f, s);
    }
    bool failed = b_pressure(f, s.source, s.region) ||
        (valid_source(f, s.source) && f.sources[s.source].seen &&
        !f.sources[s.source].feasible && !f.sources[s.source].respawning &&
        !f.sources[s.source].reward_available);
    int next = best(f, [&](int i, const Source& c) {
        return c.port == s.region && !c.pressured && !s.captured[i];
    });
    const bool depleted = next < 0 || s.captures - s.raid_captures >= 3;
    if (!f.control && (failed || depleted)) {
        s.phase = f.bag ? 1 : 2; s.relocated = true;
        s.banks_at_admission = s.banks;
    }
    if (s.phase == 1) {
        if (f.bag) { s.bank_pending = true; return b_bank(s.port, true); }
        if (s.bank_pending) return b_bank(s.port, true);
        s.phase = 2; return b_withdraw(s.home_source);
    }
    if (s.phase == 2) return b_withdraw(s.home_source);
    if (f.bag) return b_bank(s.port);
    if (next >= 0) { bind_source(s, next); return farm(next, !f.control); }
    // Residence control may rework renewed stock. Active never quietly restarts
    // a retired raid when a respawn timer happens to reach zero.
    if (f.control) return b_resident(f, s);
    return b_withdraw(s.home_source);
}

inline Task choose_15(const Frame& f, State& s) {
    if (b_preserve(f)) return b_preserved(f, s);
    if (!s.active) {
        int source = f.ordinal == 1 ? b_forward(f) :
            b_assignment(f, f.port_count > 1 ? f.ordinal % 2 : 0, f.ordinal / 2);
        if (source < 0) source = best(f);
        b_start(s, f, source); s.region = s.port;
    }
    if (f.control || f.ordinal >= 3 || s.inspection_failed) return b_resident(f, s);
    if (s.phase == 0 && s.handoff_epoch != s.epoch) {
        int reclaimed = best(f, [&](int i, const Source& c) {
            return i != s.initial_source && c.port != s.region && c.seen && c.progress > 0 &&
                c.health > 0 && !c.pressured && c.exit_distance >= 0;
        });
        // Actual residual HP reduces the cycle cost in BOTH arms. Only the
        // active arm changes this actor's regional responsibility.
        if (reclaimed >= 0 && improves(f, reclaimed, s.source)) {
            s.previous_source = reclaimed; s.pending_port = f.sources[reclaimed].port;
            s.phase = f.bag ? 1 : 2; s.attempt_started = f.round; s.handoff_epoch = s.epoch;
            s.banks_at_admission = s.banks;
        }
    }
    if (s.phase == 1) {
        if (f.bag) { s.bank_pending = true; return b_bank(s.port, true); }
        if (s.bank_pending) return b_bank(s.port, true);
        if (!b_finish_bank(f, s)) {
            s.phase = 4; s.inspection_failed = true; s.pending_port = -1;
            return b_withdraw(s.home_source);
        }
        s.phase = 2; s.attempt_started = f.round;
    }
    if (s.phase == 2) {
        const int source = s.previous_source;
        if (attempt_expired(s, f, 4) || !valid_source(f, source) ||
            (f.sources[source].seen && (!usable(f, source) || f.sources[source].pressured))) {
            s.inspection_failed = true; s.phase = 4; s.pending_port = -1;
            return f.bag ? b_bank() : b_withdraw(s.home_source);
        }
        if (!f.sources[source].seen || f.current != source) return b_observe(source);
        bind_source(s, source); s.port = s.pending_port; s.pending_port = -1;
        s.relocated = true; s.phase = 3; s.last_arrival = f.round;
        s.banks_at_admission = s.banks; s.captures_at_admission = s.captures;
    }
    if (s.phase == 3) {
        if (f.bag) return b_bank(s.port, true);
        // Re-observe after the real bank; a projected renewal is not live stock.
        if (b_finish_bank(f, s) && valid_source(f, s.source) && !f.sources[s.source].seen)
            return b_observe(s.source);
        Task t = b_resident(f, s, true);
        for (int i = 0; i < f.ally_count && i < 6; ++i) {
            const Ally& a = f.allies[i];
            if (a.visible && a.group == t.source && a.health > a.max_health / 2) {
                t.peer = a.id; t.holder_ordinal = a.ordinal; break;
            }
        }
        return t;
    }
    return b_resident(f, s);
}

inline Task choose_16(const Frame& f, State& s) {
    if (b_preserve(f)) return b_preserved(f, s);
    int open = -1;
    for (int i = 0; i < f.source_count && i < 8; ++i) if (usable(f, i) &&
        !f.sources[i].occluded && !f.sources[i].pressured && (open < 0 ||
        f.sources[i].score[1] > f.sources[open].score[1] ||
        (f.sources[i].score[1] == f.sources[open].score[1] && f.sources[i].id < f.sources[open].id))) open = i;
    if (!s.active) {
        b_start(s, f, open >= 0 ? open : best(f)); s.home_source = open;
    }
    if (f.control) return f.bag ? b_bank() : (open >= 0 ? farm(open) : fallback(f, s));
    if (f.bag) {
        // The producer, its own current ward and personally observed exit are
        // the evidence. A missing reveal removes the concealed-route admission.
        if (s.relocated && valid_source(f, s.source) &&
            (!f.sources[s.source].seen || b_pressure(f, s.source, s.port))) {
            s.relocated = false; s.inspection_failed = true;
            return b_bank(valid_source(f, open) ? f.sources[open].port : -1);
        }
        Task t = b_bank(s.port, s.relocated);
        t.source = s.source;
        t.ward = s.relocated && f.own_ward_ready;
        // A generic occlusion flag does not certify a walking/mobility leg.
        return t;
    }
    int concealed = best(f, [](int, const Source& c) {
        return c.occluded && c.seen && !c.pressured && c.exit_distance >= 0;
    });
    if (!s.inspection_failed && concealed >= 0 && improves(f, concealed, open)) {
        bind_source(s, concealed); s.port = f.sources[concealed].port; s.relocated = true;
        Task t = farm(concealed, true); t.ward = f.own_ward_ready; return t;
    }
    s.relocated = false;
    return open >= 0 ? farm(open) : fallback(f, s);
}
} // namespace p7::live
