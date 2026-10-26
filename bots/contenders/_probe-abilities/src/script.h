// Timed command scripts for probe champions (abilities probe copy). Coordinates are team-A
// coordinates; probe::own() mirrors them for team B (deltas too). One-shot commands run once at
// the first turn at or after their round; GOTO and ATKN persist until replaced or STOP.
#pragma once
#include "probe.h"
#include <functional>

namespace probe {

enum Op { GOTO, ATK, ATKN, CAST, CASTSELF, RECALL, CANCEL, TRIB, WARD, BUY, BUYC, USEC, STOP, PRIO, SMW, SMR, RESIGN, NOTE,
          STEP, QDMG, LOGSTATE, SELL, OSC, LOGV, FARM, LOGITEMS, LOGT };

struct Cmd {
    int round;
    Op op;
    int x = 0, y = 0, a = 0, b = 0;
};

inline vec2 own_delta(vec2 d) {
    if (side() == 0) return d;
    switch (map::symmetry()) {
    case symmetry::horizontal: return {-d.x, d.y};
    case symmetry::vertical: return {d.x, -d.y};
    default: return {-d.x, -d.y};
    }
}

struct Runner {
    std::vector<Cmd> cmds;
    std::size_t next = 0;
    std::optional<vec2> goal;
    std::optional<vec2> chase; // absolute tile
    float chase_floor = 0;     // attack the chase target only while its health is above this
    std::optional<vec2> osc;   // absolute base tile: alternate between base and base+(0,1)
    bool farm = false;         // attack the lowest-health visible camp in attack range every turn
    std::function<void()> custom; // scenario hook run every turn after the commands
    int log_radius = 9;
    bool log_enemies = false;
    bool log_camps_on = false;

    static void after(const char* what) { PLOG("  after %s st=%s", what, state_str(self().state()).c_str()); }

    static bool attack_at(vec2 p, const char* why, float floor = 0) {
        auto& me = self();
        try {
            if (vision::has_champion(p)) {
                auto c = vision::champion_at(p);
                if (floor > 0 && c.health() <= floor) return false;
                me.attack(c); PLOG("%s champion at %d,%d ok", why, p.x, p.y); after(why); return true;
            }
            if (vision::has_camp(p)) {
                auto c = vision::camp_at(p);
                if (floor > 0 && c.health() <= floor) return false;
                me.attack(c); PLOG("%s camp at %d,%d ok", why, p.x, p.y); after(why); return true;
            }
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
            if (!ground && !vision::has_champion(p) && !vision::has_camp(p)) {
                // The target may have stepped to a neighbouring tile (oscillating dummies).
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const vec2 q{p.x + dx, p.y + dy};
                        if (map::is_in_bounds(q) && vision::has_champion(q) && q != me.position()) { p = q; dy = 2; break; }
                    }
            }
            bool ok;
            if (!ground && vision::has_champion(p)) ok = ab.use(vision::champion_at(p));
            else if (!ground && vision::has_camp(p)) ok = ab.use(vision::camp_at(p));
            else ok = ab.use(p);
            PLOG("cast slot%d at %d,%d -> %s", slot, p.x, p.y, ok ? "true" : "false");
            after("cast");
        } catch (const error& e) {
            PLOG("cast slot%d at %d,%d engine error %d %s", slot, p.x, p.y, static_cast<int>(e.code()), e.message().c_str());
        } catch (const std::exception& e) { PLOG("cast slot%d exception %s", slot, e.what()); }
    }

    void exec(const Cmd& c) {
        auto& me = self();
        const vec2 p = c.x == -99 ? me.position() : own({c.x, c.y});
        try {
            switch (c.op) {
            case GOTO: goal = p; break;
            case STOP: goal.reset(); chase.reset(); osc.reset(); break;
            case ATK: attack_at(p, "atk"); break;
            case ATKN: chase = p; chase_floor = static_cast<float>(c.b); break;
            case CAST: cast_at(c.a, p, c.b != 0); break;
            case CASTSELF: {
                bool ok = me.abilities()[c.a].use();
                PLOG("castself slot%d -> %s", c.a, ok ? "true" : "false");
                after("castself");
                break;
            }
            case RECALL: me.recall(); PLOG("recall started"); after("recall"); break;
            case CANCEL: detail::check(detail::sdk_cancel_channel()); PLOG("cancel channel ok"); break;
            case TRIB: me.submit_gems(p); PLOG("tribute started at %d,%d", p.x, p.y); after("tribute"); break;
            case WARD: PLOG("ward at %d,%d -> %s", p.x, p.y, me.place_ward(p) ? "true" : "false"); break;
            case BUY: me.buy(item_type{static_cast<std::uint32_t>(c.a)}, static_cast<std::uint32_t>(c.b ? c.b : 1)); PLOG("buy item %d x%d ok", c.a, c.b ? c.b : 1); break;
            case SELL: me.sell(item_type{static_cast<std::uint32_t>(c.a)}, 1); PLOG("sell item %d ok", c.a); break;
            case BUYC: me.buy(consumable_type{static_cast<consumable_type::enum_type>(c.a)}); PLOG("buy consumable %d ok", c.a); break;
            case USEC: {
                auto t = consumable_type{static_cast<consumable_type::enum_type>(c.a)};
                bool ok = me.consumables()[t].use(p);
                PLOG("use consumable %d at %d,%d -> %s", c.a, p.x, p.y, ok ? "true" : "false");
                after("consumable");
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
            case STEP: {
                const vec2 d = own_delta({c.x, c.y});
                bool ok = me.move(d);
                PLOG("step %d,%d -> %s", d.x, d.y, ok ? "true" : "false");
                after("step");
                break;
            }
            case QDMG: {
                auto ab = me.abilities()[c.a];
                std::string s = "slot" + std::to_string(c.a);
                auto c100 = [](float v) { return std::to_string(static_cast<int>(v * 100)); };
                try { s += " dmg100=" + c100(ab.damage_amount()); } catch (const std::exception& e) { s += std::string(" dmg-err:") + e.what(); }
                try { s += " heal100=" + c100(ab.heal_amount()); } catch (const std::exception&) {}
                try { s += " shield100=" + c100(ab.shield_amount()); } catch (const std::exception&) {}
                if (vision::has_champion(p)) {
                    auto t = vision::champion_at(p);
                    try { s += " vs-target-dmg100=" + c100(ab.damage_amount(t)); } catch (const std::exception& e) { s += std::string(" vsdmg-err:") + e.what(); }
                    try { s += " vs-target-heal100=" + c100(ab.heal_amount(t)); } catch (const std::exception& e) { s += std::string(" vsheal-err:") + e.what(); }
                    try { s += " vs-target-shield100=" + c100(ab.shield_amount(t)); } catch (const std::exception& e) { s += std::string(" vsshield-err:") + e.what(); }
                }
                PLOG("qdmg %s", s.c_str());
                break;
            }
            case LOGSTATE: after("log"); break;
            case OSC: osc = p; goal.reset(); break;
            case LOGV: log_enemies = (c.a & 1) != 0; log_camps_on = (c.a & 2) != 0; break;
            case FARM: farm = c.a != 0; break;
            case LOGT: {
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const vec2 q{p.x + dx, p.y + dy};
                        if (!map::is_in_bounds(q) || q == me.position() || !vision::has_champion(q)) continue;
                        auto t = vision::champion_at(q);
                        PLOG("target %s at %d,%d hp100=%d st=%s buffs=%s", champ_name(t.champion_name()), q.x, q.y,
                             static_cast<int>(t.health() * 100), state_str(t.state()).c_str(), buffs_str(t).c_str());
                    }
                break;
            }
            case LOGITEMS: {
                auto items = me.item_count();
                std::string s;
                for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(item_type::count); ++i)
                    if (items[item_type{i}]) s += std::to_string(i) + "x" + std::to_string(items[item_type{i}]) + " ";
                PLOG("items %s maxhp100=%d hp100=%d", s.c_str(), static_cast<int>(me.max_health() * 100), static_cast<int>(me.health() * 100));
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
        if (log_camps_on) log_camps(log_radius);
        if (!log_enemies) return;
        for (auto p : vision::champion_tiles(team::enemy).positions()) {
            if (p.chebyshev_distance(me.position()) > log_radius) continue;
            try {
                auto c = vision::champion_at(p);
                auto a = c.abilities();
                PLOG("enemy %s id=%u at %d,%d hp100=%d/%d st=%s cd=%d,%d buffs=%s", champ_name(c.champion_name()),
                     static_cast<unsigned>(static_cast<handle_champion_t>(c)), p.x, p.y, static_cast<int>(c.health() * 100),
                     static_cast<int>(c.max_health() * 100), state_str(c.state()).c_str(), a[0].cooldown(), a[1].cooldown(),
                     buffs_str(c).c_str());
            } catch (const std::exception& e) { PLOG("enemy at %d,%d error %s", p.x, p.y, e.what()); }
        }
    }

    void turn() {
        auto& me = self();
        const int now = static_cast<int>(round_number());
        const auto fuel0 = fuel_remaining();
        PLOG("fuel %llu", static_cast<unsigned long long>(fuel0));
        log_self("start");
        log_view();
        while (next < cmds.size() && cmds[next].round <= now) exec(cmds[next++]);
        if (me.is_alive()) {
            if (custom) custom();
            if (chase && me.can_attack()) attack_at(*chase, "atkn", chase_floor);
            if (farm && me.can_attack()) {
                std::optional<camp> best;
                float bh = 1e9f;
                const auto here = me.position();
                for (auto q : vision::camp_tiles().positions()) {
                    if (q.chebyshev_distance(here) > me.attack_range()) continue;
                    try {
                        auto c = vision::camp_at(q);
                        if (c.health() < bh) { bh = c.health(); best = c; }
                    } catch (const std::exception&) {}
                }
                if (best) attack_at(best->position(), "farm");
            }
            if (osc) {
                const vec2 here = me.position(), up{osc->x, osc->y + 1};
                const vec2 to = here == *osc ? up : *osc;
                try {
                    bool ok = me.move_absolute(to);
                    PLOG("osc %d,%d -> %d,%d %s", here.x, here.y, to.x, to.y, ok ? "true" : "false");
                } catch (const error& e) {
                    PLOG("osc %d,%d -> %d,%d engine error %d %s", here.x, here.y, to.x, to.y, static_cast<int>(e.code()), e.message().c_str());
                } catch (const std::exception& e) { PLOG("osc exception %s", e.what()); }
            }
            if (goal && me.position() != *goal && me.can_move()) {
                auto before = me.position();
                bool moved = step_to(*goal);
                if (!moved) PLOG("goto %d,%d no step from %d,%d", goal->x, goal->y, before.x, before.y);
            }
        }
        log_self("end");
        PLOG("fuel used %llu", static_cast<unsigned long long>(fuel0 - fuel_remaining()));
    }
};
} // namespace probe
