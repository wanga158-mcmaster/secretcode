// Timed command scripts for probe champions (copied per probe). Coordinates are team-A
// coordinates; probe::own() mirrors them for team B. One-shot commands run once at the first
// turn at or after their round; GOTO and ATKN persist until replaced or STOP.
#pragma once
#include "probe.h"
#include <map>

namespace probe {

enum Op { GOTO, ATK, ATKN, CAST, CASTSELF, RECALL, CANCEL, TRIB, WARD, BUY, BUYC, USEC, STOP, PRIO, SMW, SMR, RESIGN, NOTE,
          CAPTURE, GROUPLOG, KILL, TRYALL, ATKLOCK };

struct Cmd {
    int round;
    Op op;
    int x = 0, y = 0, a = 0, b = 0;
    bool abs = false; // absolute coordinates (no mirroring for team B)
};

struct Runner {
    std::vector<Cmd> cmds;
    std::size_t next = 0;
    std::optional<vec2> goal;
    std::optional<vec2> chase; // persistent attack target tile (absolute coordinates)
    std::optional<vec2> capture; // kill the camp on this tile (if visible), then stand on it
    std::optional<vec2> kill; // attack the camp on this tile until it is gone (approach if needed)
    std::vector<vec2> group_logs; // camp spawn tiles whose group state is logged every turn
    int log_radius = 9;
    bool log_enemies = true;

    // Unit (champion or camp) at an absolute tile, if visible.
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

    void exec(const Cmd& c) {
        auto& me = self();
        const vec2 p = c.abs ? vec2{c.x, c.y} : own({c.x, c.y});
        try {
            switch (c.op) {
            case GOTO: goal = p; capture.reset(); break;
            case STOP: goal.reset(); chase.reset(); capture.reset(); break;
            case CAPTURE: capture = p; goal.reset(); chase.reset(); break;
            case KILL: kill = p; goal.reset(); break;
            case GROUPLOG: group_logs.push_back(p); break;
            case ATK: attack_at(p, "atk"); break;
            case ATKN: chase = p; break;
            case CAST: cast_at(c.a, p, c.b != 0); break;
            case CASTSELF: {
                bool ok = me.abilities()[c.a].use();
                PLOG("castself slot%d -> %s", c.a, ok ? "true" : "false");
                break;
            }
            case RECALL: me.recall(); PLOG("recall started"); break;
            case CANCEL: detail::check(detail::sdk_cancel_channel()); PLOG("cancel channel ok"); break;
            case TRIB: me.submit_gems(p); PLOG("tribute started at %d,%d", p.x, p.y); break;
            case WARD: PLOG("ward at %d,%d -> %s", p.x, p.y, me.place_ward(p) ? "true" : "false"); break;
            case BUY: me.buy(item_type{static_cast<std::uint32_t>(c.a)}, static_cast<std::uint32_t>(c.b ? c.b : 1)); PLOG("buy item %d x%d ok", c.a, c.b ? c.b : 1); break;
            case BUYC: me.buy(consumable_type{static_cast<consumable_type::enum_type>(c.a)}); PLOG("buy consumable %d ok", c.a); break;
            case USEC: {
                auto t = consumable_type{static_cast<consumable_type::enum_type>(c.a)};
                bool ok = me.consumables()[t].use(p);
                PLOG("use consumable %d at %d,%d -> %s", c.a, p.x, p.y, ok ? "true" : "false");
                break;
            }
            case PRIO: me.set_priority(c.a); PLOG("set_priority %d (now %d next %d)", c.a, me.prev_priority(), me.priority()); break;
            case SMW: shared_memory[static_cast<std::size_t>(c.a)] = static_cast<std::uint8_t>(c.b); PLOG("shared[%d] := %d", c.a, c.b); break;
            case SMR: {
                std::string s;
                for (std::size_t i = 0; i < shared_memory_buffer::size(); ++i) s += std::to_string(static_cast<int>(static_cast<std::uint8_t>(shared_memory[i]))) + " ";
                PLOG("shared = %s", s.c_str());
                break;
            }
            case RESIGN: PLOG("resign"); std::fflush(nullptr); resign(); break;
            case NOTE: PLOG("note %d", c.a); break;
            case TRYALL: try_all(); break;
            case ATKLOCK: {
                bool found = false;
                for (const auto& l : vision::altar_locks(team::enemy))
                    if (l.position() == p) {
                        found = true;
                        me.attack(l);
                        PLOG("attack enemy lock at %d,%d ok (had %u hits)", p.x, p.y, l.hits_remaining());
                    }
                if (!found) PLOG("no visible enemy lock at %d,%d", p.x, p.y);
                break;
            }
            }
        } catch (const error& e) {
            PLOG("op %d engine error %d %s", static_cast<int>(c.op), static_cast<int>(e.code()), e.message().c_str());
        } catch (const std::exception& e) { PLOG("op %d exception %s", static_cast<int>(c.op), e.what()); }
    }

    // Attempt one of each action kind and log the outcome (ok / false / engine error code).
    static void try_all() {
        auto& me = self();
        auto attempt = [](const char* what, auto&& fn) {
            try {
                auto r = fn();
                PLOG("try %s -> %s", what, r ? "ok" : "false");
            } catch (const error& e) {
                PLOG("try %s -> error %d %s", what, static_cast<int>(e.code()), e.message().c_str());
            } catch (const std::exception& e) { PLOG("try %s -> exception %s", what, e.what()); }
        };
        attempt("move", [&] {
            for (auto d : {vec2::east, vec2::west, vec2::north, vec2::south}) {
                const vec2 t = me.position() + d;
                if (map::is_in_bounds(t) && !map::walls().is_set(t) && !map::holes().is_set(t) && !map::altars().is_set(t))
                    return me.move(d);
            }
            return false;
        });
        attempt("ward", [&] { return me.place_ward(me.position()); });
        attempt("castself0", [&] { return me.abilities()[0].use(); });
        attempt("castself1", [&] { return me.abilities()[1].use(); });
        attempt("buy-shard", [&] { me.buy(item_type::monster_shard_of_health); return true; });
        attempt("buy-crystal", [&] { me.buy(consumable_type::tp_crystal); return true; });
        attempt("priority", [&] { me.set_priority(1); return true; });
        attempt("shared-write", [&] { shared_memory[7] = static_cast<std::uint8_t>(round_number() & 0xff); return true; });
        attempt("recall", [&] { me.recall(); return true; });
    }

    void log_groups() {
        for (auto p : group_logs) {
            try {
                auto g = map::camp(p).camp_group();
                auto st = g.state();
                if (std::holds_alternative<map::camp_group_alive>(st)) PLOG("group@%d,%d alive", p.x, p.y);
                else if (std::holds_alternative<map::camp_group_reward_available>(st)) PLOG("group@%d,%d reward", p.x, p.y);
                else PLOG("group@%d,%d respawning timer=%d", p.x, p.y, std::get<map::camp_group_respawning>(st).timer);
            } catch (const error& e) { PLOG("group@%d,%d error %d", p.x, p.y, static_cast<int>(e.code())); }
            catch (const std::exception& e) { PLOG("group@%d,%d exception %s", p.x, p.y, e.what()); }
        }
    }

    void capture_step() {
        auto& me = self();
        const vec2 t = *capture;
        const vec2 here = me.position();
        if (here == t) return; // holding the tile
        if (vision::has_camp(t)) {
            if (here.chebyshev_distance(t) <= me.attack_range()) {
                if (me.can_attack()) attack_at(t, "capture-atk");
            } else if (me.can_move()) {
                step_to(t);
            }
            return;
        }
        if (me.can_move()) {
            try {
                bool ok = me.move_absolute(t);
                if (!ok) step_to(t);
            } catch (const std::exception&) { step_to(t); }
        }
    }

    void log_view() {
        auto& me = self();
        log_groups();
        if (!me.is_alive()) return;
        log_camps(log_radius);
        if (!log_enemies) return;
        for (auto p : vision::champion_tiles(team::enemy).positions()) {
            try {
                auto c = vision::champion_at(p);
                PLOG("enemy %s id=%u at %d,%d hp=%.2f/%.2f st=%s buffs=%s", champ_name(c.champion_name()),
                     static_cast<unsigned>(static_cast<handle_champion_t>(c)), p.x, p.y, c.health(), c.max_health(),
                     state_str(c.state()).c_str(), buffs_str(c).c_str());
            } catch (const std::exception& e) { PLOG("enemy at %d,%d error %s", p.x, p.y, e.what()); }
        }
    }

    void turn() {
        auto& me = self();
        const int now = static_cast<int>(round_number());
        log_self("start");
        log_view();
        while (next < cmds.size() && cmds[next].round <= now) exec(cmds[next++]);
        if (me.is_alive()) {
            if (chase && me.can_attack()) attack_at(*chase, "atkn");
            if (capture) capture_step();
            if (kill) {
                if (!vision::has_camp(*kill)) { PLOG("kill %d,%d done", kill->x, kill->y); kill.reset(); }
                else if (me.position().chebyshev_distance(*kill) <= me.attack_range()) { if (me.can_attack()) attack_at(*kill, "kill"); }
                else step_to(*kill);
            }
            if (goal && me.position() != *goal && me.can_move()) {
                auto before = me.position();
                bool moved = step_to(*goal);
                if (!moved) PLOG("goto %d,%d no step from %d,%d", goal->x, goal->y, before.x, before.y);
            }
        }
        log_self("end");
    }
};
} // namespace probe
