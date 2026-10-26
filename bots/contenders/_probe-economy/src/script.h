// Economy probe script runner (own copy). Commands run in order; a command waits for its round,
// and WAIT* commands block the queue until their condition holds. GOTO and FARM persist.
// Coordinates are team-A coordinates (probe::own mirrors them for team B).
#pragma once
#include "probe.h"

namespace probe {

enum Op {
    GOTO, ATK, ATKN, CAST, CASTSELF, RECALL, CANCEL, TRIB, BUY, BUYC, SELL, STOP, PRIO, RESIGN, NOTE, STATS,
    FARM,      // x,y = one corner, a,b = other corner: attack live camps inside, walking to them
    WAITAT,    // until at the GOTO goal
    WAITGOLD,  // until gold >= a
    WAITGEMS,  // until gems >= a
    WAITFARM,  // until every camp of the farm rectangle is known dead
    WAITDEAD, WAITALIVE,
    WAITHPLT,  // until health < a
    WAITROUND, // until round >= a (relative waits: use the round field)
    HOLD,      // stand still (cancel goal) for a rounds from now
    WAITGAIN,  // until gold rises above its value when this wait began
    WAITGEMGAIN, // until gems rise above their value when this wait began
    WAITUNIT,  // until a champion is visible at tile x,y
    WAITNOUNIT, // until no champion is visible at tile x,y
    WAITTURN,  // until a later turn than the one this wait began on
};

struct Cmd {
    int round;
    Op op;
    int x = 0, y = 0, a = 0, b = 0;
};

inline std::string items_str() {
    std::string r;
    try {
        auto ic = self().item_count();
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(item_type::count); ++i)
            if (ic[item_type{i}] != 0) r += std::to_string(i) + "x" + std::to_string(ic[item_type{i}]) + " ";
        auto& cs = self().consumables();
        r += "| crystal=" + std::to_string(cs[consumable_type::tp_crystal].count()) +
             " lock=" + std::to_string(cs[consumable_type::altar_lock].count());
    } catch (const std::exception& e) { r += std::string("items-err:") + e.what(); }
    return r;
}

struct Runner {
    std::vector<Cmd> cmds;
    std::size_t next = 0;
    std::optional<vec2> goal;
    std::optional<vec2> chase;
    bool farming = false, farm_done = false;
    vec2 f0{}, f1{};
    std::vector<vec2> farm_tiles;
    int hold_until = -1;
    int log_radius = 9;
    std::size_t gain_cmd = static_cast<std::size_t>(-1);
    std::uint32_t gain_base = 0;

    static bool attack_at(vec2 p, const char* why) {
        auto& me = self();
        try {
            if (vision::has_champion(p)) { me.attack(vision::champion_at(p)); PLOG("%s champion at %d,%d ok", why, p.x, p.y); return true; }
            if (vision::has_camp(p)) { me.attack(vision::camp_at(p)); PLOG("%s camp at %d,%d ok", why, p.x, p.y); return true; }
            if (vision::has_altar_lock(p)) { me.attack(vision::altar_lock_at(p)); PLOG("%s lock at %d,%d ok", why, p.x, p.y); return true; }
            PLOG("%s nothing visible at %d,%d", why, p.x, p.y);
        } catch (const error& e) {
            PLOG("%s at %d,%d engine error %d %s", why, p.x, p.y, static_cast<int>(e.code()), e.message().c_str());
        } catch (const std::exception& e) { PLOG("%s at %d,%d exception %s", why, p.x, p.y, e.what()); }
        return false;
    }

    static void cast_at(int slot, vec2 p, bool ground) {
        auto& me = self();
        auto ab = me.abilities()[slot];
        try {
            bool ok;
            if (!ground && vision::has_champion(p)) ok = ab.use(vision::champion_at(p));
            else if (!ground && vision::has_camp(p)) ok = ab.use(vision::camp_at(p));
            else ok = ab.use(p);
            PLOG("cast slot%d at %d,%d -> %s", slot, p.x, p.y, ok ? "true" : "false");
        } catch (const error& e) {
            PLOG("cast slot%d at %d,%d engine error %d %s", slot, p.x, p.y, static_cast<int>(e.code()), e.message().c_str());
        } catch (const std::exception& e) { PLOG("cast slot%d exception %s", slot, e.what()); }
    }

    void start_farm(vec2 a, vec2 b) {
        f0 = {std::min(a.x, b.x), std::min(a.y, b.y)};
        f1 = {std::max(a.x, b.x), std::max(a.y, b.y)};
        farm_tiles.clear();
        static std::vector<vec2> all_camps; // cached once: map::camp_groups() is expensive
        if (all_camps.empty())
            for (const auto& g : map::camp_groups())
                for (const auto& c : g.camps()) all_camps.push_back(c.spawn_position());
        for (auto p : all_camps)
            if (p.x >= f0.x && p.x <= f1.x && p.y >= f0.y && p.y <= f1.y) farm_tiles.push_back(p);
        farming = true;
        farm_done = farm_tiles.empty();
        PLOG("farm %d,%d..%d,%d: %zu camps", f0.x, f0.y, f1.x, f1.y, farm_tiles.size());
    }

    // A farm tile is "dead" when it is in vision and holds no live camp.
    bool farm_finished() const {
        for (auto p : farm_tiles)
            if (!vision::in_vision(p) || vision::has_camp(p)) return false;
        return true;
    }

    void farm_step() {
        auto& me = self();
        if (farm_finished()) {
            if (!farm_done) PLOG("farm done");
            farm_done = true;
            farming = false;
            return;
        }
        const auto range = me.attack_range();
        auto pick = [&]() -> std::optional<camp> {
            std::optional<camp> best;
            float bh = 1e30f;
            const auto here = me.position();
            for (auto p : farm_tiles) {
                if (!vision::has_camp(p) || p.chebyshev_distance(here) > range) continue;
                auto c = vision::camp_at(p);
                if (c.health() < bh) { bh = c.health(); best = c; }
            }
            return best;
        };
        auto target = pick();
        if (!target && me.can_move()) {
            // walk towards the nearest live farm camp
            const auto here = me.position();
            std::optional<vec2> near;
            std::int64_t bd = 1 << 30;
            for (auto p : farm_tiles) {
                if (vision::in_vision(p) && !vision::has_camp(p)) continue;
                auto d = p.chebyshev_distance(here);
                if (d < bd) { bd = d; near = p; }
            }
            if (near) step_to(*near);
            target = pick();
        }
        if (target && me.can_attack()) {
            try { me.attack(*target); } catch (const error& e) { PLOG("farm attack engine error %d %s", static_cast<int>(e.code()), e.message().c_str()); }
        }
    }

    // True if the command blocks the queue this turn.
    bool blocked(const Cmd& c) {
        auto& me = self();
        switch (c.op) {
        case WAITAT: return goal && me.is_alive() && me.position() != *goal;
        case WAITGOLD: return me.gold_count() < static_cast<std::uint32_t>(c.a);
        case WAITGEMS: return !me.is_alive() || me.gem_count() < static_cast<std::uint32_t>(c.a);
        case WAITFARM: return !farm_done;
        case WAITDEAD: return me.is_alive();
        case WAITALIVE: return !me.is_alive();
        case WAITHPLT: return !me.is_alive() || me.health() >= static_cast<float>(c.a);
        case WAITROUND: return static_cast<int>(round_number()) < c.a;
        case WAITTURN: {
            const auto now = round_number();
            if (gain_cmd != next) { gain_cmd = next; gain_base = now; return true; }
            return now <= gain_base;
        }
        case WAITUNIT: return !vision::has_champion(own({c.x, c.y}));
        case WAITNOUNIT: return vision::has_champion(own({c.x, c.y}));
        case WAITGAIN:
        case WAITGEMGAIN: {
            const auto value = c.op == WAITGAIN ? me.gold_count() : (me.is_alive() ? me.gem_count() : 0u);
            if (gain_cmd != next) { gain_cmd = next; gain_base = value; return true; }
            if (value > gain_base) { PLOG("gain %s %u -> %u", c.op == WAITGAIN ? "gold" : "gems", gain_base, value); return false; }
            return true;
        }
        default: return false;
        }
    }

    void exec(const Cmd& c) {
        auto& me = self();
        const vec2 p = own({c.x, c.y});
        try {
            switch (c.op) {
            case GOTO: goal = p; break;
            case STOP: goal.reset(); chase.reset(); farming = false; break;
            case ATK: attack_at(p, "atk"); break;
            case ATKN: chase = p; break;
            case CAST: cast_at(c.a, p, c.b != 0); break;
            case CASTSELF: PLOG("castself slot%d -> %s", c.a, me.abilities()[c.a].use() ? "true" : "false"); break;
            case RECALL: me.recall(); PLOG("recall started"); break;
            case CANCEL: detail::check(detail::sdk_cancel_channel()); PLOG("cancel channel ok"); break;
            case TRIB: me.submit_gems(p); PLOG("tribute started at %d,%d gems=%u", p.x, p.y, me.gem_count()); break;
            case BUY: {
                const auto n = static_cast<std::uint32_t>(c.b ? c.b : 1);
                const auto before = me.gold_count();
                me.buy(item_type{static_cast<std::uint32_t>(c.a)}, n);
                PLOG("buy item %d x%u ok gold %u->%u hp=%.2f/%.2f str=%.2f foc=%.2f items=%s", c.a, n, before, me.gold_count(),
                     me.health(), me.max_health(), me.strength(), me.focus(), items_str().c_str());
                break;
            }
            case BUYC: {
                const auto before = me.gold_count();
                me.buy(consumable_type{static_cast<consumable_type::enum_type>(c.a)});
                PLOG("buy consumable %d ok gold %u->%u", c.a, before, me.gold_count());
                break;
            }
            case SELL: {
                const auto n = static_cast<std::uint32_t>(c.b ? c.b : 1);
                const auto before = me.gold_count();
                me.sell(item_type{static_cast<std::uint32_t>(c.a)}, n);
                PLOG("sell item %d x%u ok gold %u->%u hp=%.2f/%.2f str=%.2f foc=%.2f items=%s", c.a, n, before, me.gold_count(),
                     me.health(), me.max_health(), me.strength(), me.focus(), items_str().c_str());
                break;
            }
            case PRIO: me.set_priority(c.a); PLOG("set_priority %d", c.a); break;
            case RESIGN: PLOG("resign"); std::fflush(nullptr); resign(); break;
            case NOTE: PLOG("note %d", c.a); break;
            case STATS: PLOG("stats hp=%.3f/%.3f str=%.3f foc=%.3f range=%d items=%s", me.is_alive() ? me.health() : 0.f,
                             me.is_alive() ? me.max_health() : 0.f, me.is_alive() ? me.strength() : 0.f,
                             me.is_alive() ? me.focus() : 0.f, me.is_alive() ? me.attack_range() : 0, items_str().c_str()); break;
            case FARM: start_farm(p, own({c.a, c.b})); break;
            case HOLD: goal.reset(); hold_until = static_cast<int>(round_number()) + c.a; break;
            default: break;
            }
        } catch (const error& e) {
            PLOG("op %d engine error %d %s", static_cast<int>(c.op), static_cast<int>(e.code()), e.message().c_str());
        } catch (const std::exception& e) { PLOG("op %d exception %s", static_cast<int>(c.op), e.what()); }
    }

    void log_view() {
        auto& me = self();
        if (!me.is_alive()) return;
        for (auto p : vision::champion_tiles(team::enemy).positions()) {
            try {
                auto c = vision::champion_at(p);
                PLOG("enemy %s id=%u at %d,%d hp=%.2f/%.2f gems=%u", champ_name(c.champion_name()),
                     static_cast<unsigned>(static_cast<handle_champion_t>(c)), p.x, p.y, c.health(), c.max_health(), c.gem_count());
            } catch (const std::exception& e) { PLOG("enemy at %d,%d error %s", p.x, p.y, e.what()); }
        }
    }

    void turn() {
        auto& me = self();
        const int now = static_cast<int>(round_number());
        log_self("start");
        log_view();
        while (next < cmds.size() && cmds[next].round <= now) {
            if (blocked(cmds[next])) break;
            exec(cmds[next++]);
        }
        if (me.is_alive() && now >= hold_until) {
            if (farming) farm_step();
            if (chase && me.can_attack()) attack_at(*chase, "atkn");
            if (goal && me.position() != *goal && me.can_move()) {
                auto before = me.position();
                if (!step_to(*goal)) PLOG("goto %d,%d no step from %d,%d", goal->x, goal->y, before.x, before.y);
            }
        }
        // a WAIT satisfied by this turn's actions releases the following commands now (same turn)
        if (next < cmds.size() && (cmds[next].op == WAITAT || cmds[next].op == WAITFARM))
            while (next < cmds.size() && cmds[next].round <= now && !blocked(cmds[next])) exec(cmds[next++]);
        log_self("end");
    }
};
} // namespace probe
