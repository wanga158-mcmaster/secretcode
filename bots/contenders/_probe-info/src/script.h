// Timed command scripts for probe champions (copied per probe). Coordinates are team-A
// coordinates; probe::own() mirrors them for team B. One-shot commands run once at the first
// turn at or after their round; GOTO and ATKN persist until replaced or STOP.
#pragma once
#include "probe.h"
#include <map>

namespace probe {

enum Op { GOTO, ATK, ATKN, CAST, CASTSELF, RECALL, CANCEL, TRIB, WARD, BUY, BUYC, USEC, STOP, PRIO, SMW, SMR, RESIGN, NOTE };

struct Cmd {
    int round;
    Op op;
    int x = 0, y = 0, a = 0, b = 0;
};

struct Runner {
    std::vector<Cmd> cmds;
    std::size_t next = 0;
    std::optional<vec2> goal;
    std::optional<vec2> chase; // persistent attack target tile (absolute coordinates)
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
        const vec2 p = own({c.x, c.y});
        try {
            switch (c.op) {
            case GOTO: goal = p; break;
            case STOP: goal.reset(); chase.reset(); break;
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
            }
        } catch (const error& e) {
            PLOG("op %d engine error %d %s", static_cast<int>(c.op), static_cast<int>(e.code()), e.message().c_str());
        } catch (const std::exception& e) { PLOG("op %d exception %s", static_cast<int>(c.op), e.what()); }
    }

    void log_view() {
        auto& me = self();
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
