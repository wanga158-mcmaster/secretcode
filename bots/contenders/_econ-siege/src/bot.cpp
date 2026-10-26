// Coordinated siege probe (economy.md E8/E9 verification): the active champions rally out of the
// target group's reach, engage together, rotate the tank with cover (a hurt tank steps back while
// another melee stays adjacent, and recalls at once), end the wave when the last tank would have to
// leave uncovered (everyone walks out, the hurt ones recall), heal, rally and engage again. Priority
// fixes the tank order among equally near melee (M6). Shared memory (team, 8 bytes):
//   0 state (0 gather, 1 fight, 2 retreat, 3 done), 1 wave, 2 ready mask, 3 engaged mask,
//   4 capture claim (slot + 1).
// Every turn logs a status line; events: WAVE_START, JOIN, LEAVE_COVERED, LEAVE_UNCOVERED,
// WAVE_END, RECALL, READY, GAIN, BUY. Knobs: knobs.h.
#include <cambc.h>
#include "bot.h"
#include "knobs.h"
#include <algorithm>
#include <bit>
#include <cstdio>
#include <iostream>
#include <string>
#include <variant>
#include <vector>

using cambc::vec2;
using cambc::team;

namespace {

const char* kTypeName[] = {"ryth", "vesper", "vendetta", "cassi", "issac", "stellar", "prism",
                           "marcella", "willow", "ren", "varg", "tavi", "heart"};
enum State : std::uint8_t { ST_GATHER = 0, ST_FIGHT = 1, ST_RETREAT = 2, ST_DONE = 3 };

int cheb(vec2 a, vec2 b) { return static_cast<int>(a.chebyshev_distance(b)); }
void say(const char* text) { std::cout << text << '\n'; }

std::uint8_t sm(int i) { return cambc::shared_memory[i]; }
void sm_set(int i, std::uint8_t v) { cambc::shared_memory[i] = v; }

int active_count() { return std::popcount(static_cast<unsigned>(S_ACTIVE)); }
std::uint8_t all_mask() { return static_cast<std::uint8_t>((1u << active_count()) - 1u); }
int slot_of(int type) {
    int s = 0;
    for (int t = 0; t < type; ++t)
        if ((S_ACTIVE >> t) & 1) ++s;
    return s;
}

cambc::bitboard g_live;
bool g_dirty = true;
void dirty() { g_dirty = true; }
bool living(const cambc::camp& c) {
    if (g_dirty) {
        g_live = cambc::vision::awake_camp_tiles() | cambc::vision::asleep_camp_tiles();
        g_dirty = false;
    }
    return g_live.is_set(c.spawn_position());
}

std::optional<cambc::map::camp_group> group_at(int x, int y) {
    if (x < 0 || y < 0) return std::nullopt;
    for (auto& g : cambc::map::camp_groups())
        for (const auto& c : g)
            if (c.spawn_position().x == x && c.spawn_position().y == y) return g;
    return std::nullopt;
}

// 0 unseen, 1 alive, 2 reward available, 3 respawning
int group_state(const cambc::map::camp_group& g) {
    try {
        const auto s = g.state();
        if (std::holds_alternative<cambc::map::camp_group_alive>(s)) return 1;
        if (std::holds_alternative<cambc::map::camp_group_reward_available>(s)) return 2;
        return 3;
    } catch (...) {
        return 0;
    }
}

cambc::bitboard blocked_tiles() {
    auto b = cambc::map::walls() | cambc::map::holes() | cambc::map::altars() | cambc::map::spawn_region(team::enemy) |
             cambc::vision::champion_tiles() | cambc::vision::camp_tiles();
    b.set(cambc::self().position(), false);
    return b;
}

bool on_member(const cambc::map::camp_group& g, vec2 p) {
    for (const auto& c : g)
        if (c.spawn_position() == p) return true;
    return false;
}

// Smith order: highest strength per health first, then lowest health.
const cambc::camp* pick_target(const cambc::map::camp_group& g, vec2 me, bool nearest) {
    const cambc::camp* best = nullptr;
    float score = -1e30f;
    for (const auto& c : g) {
        if (!living(c)) continue;
        float s = nearest ? -static_cast<float>(cheb(c.spawn_position(), me)) * 1e5f - c.health()
                          : c.strength() / c.max_health() * 1e6f - c.health();
        if (!best || s > score) best = &c, score = s;
    }
    return best;
}

int reach(const cambc::map::camp_group& g) {
    int r = 0;
    for (const auto& c : g)
        if (living(c) && c.strength() > 0.0f) r = std::max(r, static_cast<int>(c.attack_range()));
    return r;
}

// Is p inside the reach of any living striking member?
bool exposed(const cambc::map::camp_group& g, vec2 p) {
    for (const auto& c : g)
        if (living(c) && c.strength() > 0.0f && cheb(c.spawn_position(), p) <= c.attack_range()) return true;
    return false;
}

// Step to the free neighbour farthest from the living striking members (min distance).
void step_away(const cambc::map::camp_group& g) {
    auto& me = cambc::self();
    if (!me.can_move()) return;
    const auto blocked = blocked_tiles();
    vec2 best = me.position();
    int best_d = -1;
    for (auto t : blocked.reachable_tiles(me.position(), 1).positions()) {
        int d = 99;
        for (const auto& c : g)
            if (living(c) && c.strength() > 0.0f) d = std::min(d, cheb(c.spawn_position(), t));
        if (d > best_d) best_d = d, best = t;
    }
    if (best != me.position()) {
        try {
            me.move_absolute(best);
        } catch (const std::exception& e) {
            std::cout << "siege error step: " << e.what() << '\n';
        }
    }
}

void move_near(vec2 goal) {
    auto& me = cambc::self();
    if (!me.can_move() || me.position() == goal) return;
    try {
        if (cheb(goal, me.position()) == 1 && !blocked_tiles().is_set(goal)) me.move_absolute(goal);
        else me.move_towards(goal);
    } catch (const std::exception& e) {
        std::cout << "siege error move: " << e.what() << '\n';
    }
}

}  // namespace

void Bot::status(int round) {
    if (!S_VERBOSE) return;
    auto& me = cambc::self();
    char buf[300];
    const int type = static_cast<int>(me.champion_name());
    if (!me.is_alive()) {
        std::snprintf(buf, sizeof buf, "R%d %s dead ph%d st%u w%u", round, kTypeName[type], static_cast<int>(phase), sm(0), sm(1));
        say(buf);
        return;
    }
    int n = 0;
    float hp = 0.0f;
    if (target)
        for (const auto& c : *target)
            if (living(c)) ++n, hp += c.health();
    const auto p = me.position();
    std::snprintf(buf, sizeof buf, "R%d %s hp%.1f/%.0f prev%.1f p%d,%d g%u m%u ph%d st%u w%u rdy%02x eng%02x | living%d hp%.1f",
                  round, kTypeName[type], me.health(), me.max_health(), last_hp_end, p.x, p.y, me.gold_count(), me.gem_count(),
                  static_cast<int>(phase), sm(0), sm(1), sm(2), sm(3), n, hp);
    say(buf);
}

void Bot::attack_turn(int round, bool allow_move) {
    auto& me = cambc::self();
    const auto& g = phase == FARM ? *farm : *target;
    const cambc::camp* tgt = pick_target(g, me.position(), phase == FARM);
    if (!tgt) return;
    const bool melee = me.attack_range() <= 1;
    const float observed = last_hp_end >= 0.0f ? last_hp_end - me.health() : 0.0f;
    auto abilities = me.abilities();
    bool cast = false;
    for (auto& ab : abilities) {
        if (cast) break;
        try {
            if (!ab.is_ready()) continue;
            const int dt = cheb(me.position(), tgt->position());
            if ((S_SPELLS & 16) && ab.is_shield() && observed > 0.5f) {
                cast = ab.use();
            } else if ((S_SPELLS & 2) && (ab.is_damage() || ab.is_cc()) && ab.can_target_camps() && dt <= 4) {
                cast = ab.use(*tgt);
            } else if ((S_SPELLS & 4) && ab.is_aoe_damage() && dt <= 4) {
                cast = ab.use(tgt->position());
            } else if ((S_SPELLS & 8) && ab.is_dash() && me.champion_name() == cambc::champion_type::heart_of_ruin && dt <= 3) {
                const auto blocked = blocked_tiles();
                vec2 land{-1, -1};
                int land_n = 0;
                const auto p = me.position();
                for (int dx = -2; dx <= 2; ++dx)
                    for (int dy = -2; dy <= 2; ++dy) {
                        const vec2 t{p.x + dx, p.y + dy};
                        if (t == p || t.x < 0 || t.y < 0 || t.x >= cambc::map::width() || t.y >= cambc::map::height()) continue;
                        if (blocked.is_set(t) || cheb(t, tgt->position()) != 1) continue;
                        int n = 0;
                        for (const auto& c : g)
                            if (living(c) && cheb(c.spawn_position(), t) <= 1) ++n;
                        if (n > land_n) land_n = n, land = t;
                    }
                if (land.x >= 0) cast = ab.use(land);
            }
            if (cast) {
                dirty();
                char buf[64];
                std::snprintf(buf, sizeof buf, "CAST r%d slot%u", round, ab.slot());
                if (S_VERBOSE) say(buf);
            }
        } catch (const std::exception& e) {
            std::cout << "siege error cast: " << e.what() << '\n';
        }
    }
    if (!living(*tgt)) {
        tgt = pick_target(g, me.position(), phase == FARM);
        if (!tgt) return;
    }
    auto attack_once = [&]() {
        if (!me.can_attack()) return false;
        const cambc::camp* t = nullptr;
        if (living(*tgt) && cheb(me.position(), tgt->position()) <= me.attack_range()) t = tgt;
        if (!t)
            for (const auto& c : g)
                if (living(c) && cheb(me.position(), c.spawn_position()) <= me.attack_range()) {
                    t = &c;
                    break;
                }
        if (!t) return false;
        try {
            me.attack(*t);
            dirty();
            return true;
        } catch (const std::exception& e) {
            std::cout << "siege error attack: " << e.what() << '\n';
            return false;
        }
    };
    bool hit = attack_once();
    if (!hit && allow_move) {
        // take position: melee next to the target on a free tile, ranged within 3
        if (melee && cheb(me.position(), tgt->position()) > 1) {
            const auto blocked = blocked_tiles();
            vec2 goal{-1, -1};
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy) {
                    const vec2 t{tgt->position().x + dx, tgt->position().y + dy};
                    if ((dx == 0 && dy == 0) || t.x < 0 || t.y < 0 || t.x >= cambc::map::width() || t.y >= cambc::map::height()) continue;
                    if (blocked.is_set(t)) continue;
                    if (goal.x < 0 || cheb(t, me.position()) < cheb(goal, me.position())) goal = t;
                }
            move_near(goal.x >= 0 ? goal : tgt->position());
        } else if (!melee && cheb(me.position(), tgt->position()) > 3) {
            move_near(tgt->position());
        }
        hit = attack_once();
    }
    if (hit && (S_SPELLS & 1) && !cast) {
        for (auto& ab : abilities) {
            try {
                if (ab.is_attack_reset() && ab.is_ready() && ab.use()) {
                    if (!living(*tgt)) tgt = pick_target(g, me.position(), phase == FARM);
                    if (tgt) attack_once();
                    break;
                }
            } catch (const std::exception& e) {
                std::cout << "siege error reset: " << e.what() << '\n';
            }
        }
    }
}

void Bot::farm_turn(int round) {
    auto& me = cambc::self();
    if (!farm) {
        phase = RALLY;
        return;
    }
    const int gs = group_state(*farm);
    if (gs == 0) {
        move_near(farm->camps()[0].spawn_position());
        return;
    }
    if (gs == 1) {
        attack_turn(round, true);
        return;
    }
    if (gs == 2) {
        if (on_member(*farm, me.position())) return;
        const auto blocked = blocked_tiles();
        vec2 goal{-1, -1};
        for (const auto& c : *farm) {
            const auto t = c.spawn_position();
            if (blocked.is_set(t)) continue;
            if (goal.x < 0 || cheb(t, me.position()) < cheb(goal, me.position())) goal = t;
        }
        if (goal.x >= 0) move_near(goal);
        return;
    }
    // respawning and not rich enough yet: wait off the tiles
    if (on_member(*farm, me.position())) step_away(*farm);
}

void Bot::rally_turn(int round) {
    auto& me = cambc::self();
    const vec2 rally{S_RX, S_RY};
    const std::uint8_t bit = static_cast<std::uint8_t>(1u << slot);
    bool ready = false;
    if (cheb(me.position(), rally) > 2) {
        move_near(rally);
    }
    if (cheb(me.position(), rally) <= 2 && me.health() >= me.max_health() * static_cast<float>(S_READY) / 100.0f &&
        !exposed(*target, me.position())) {
        ready = true;
        if (!(sm(2) & bit)) {
            sm_set(2, sm(2) | bit);
            char buf[64];
            std::snprintf(buf, sizeof buf, "READY r%d slot%d", round, slot);
            say(buf);
        }
    }
    if (sm(0) == ST_RETREAT && sm(3) == 0) {
        sm_set(0, ST_GATHER);
        char buf[64];
        std::snprintf(buf, sizeof buf, "WAVE_END r%d wave%u", round, sm(1));
        say(buf);
    }
    if (sm(0) == ST_GATHER && sm(2) == all_mask()) {
        sm_set(0, ST_FIGHT);
        sm_set(1, static_cast<std::uint8_t>(sm(1) + 1));
        sm_set(2, 0);
        char buf[64];
        std::snprintf(buf, sizeof buf, "WAVE_START r%d wave%u", round, sm(1));
        say(buf);
    }
    if (sm(0) == ST_FIGHT && ready && (sm(1) > my_wave || !S_WAVE)) {
        my_wave = sm(1);
        sm_set(2, sm(2) & static_cast<std::uint8_t>(~bit));
        phase = FIGHT;
        char buf[64];
        std::snprintf(buf, sizeof buf, "JOIN r%d wave%d slot%d", round, my_wave, slot);
        say(buf);
        fight_turn(round);
    }
}

void Bot::retreat_turn(int round) {
    auto& me = cambc::self();
    const std::uint8_t bit = static_cast<std::uint8_t>(1u << slot);
    if (exposed(*target, me.position())) {
        step_away(*target);
        if (exposed(*target, me.position())) return;
    }
    sm_set(3, sm(3) & static_cast<std::uint8_t>(~bit));
    if (me.health() < 0.9f * me.max_health()) {
        try {
            me.recall();
            phase = RECALLING;
            char buf[64];
            std::snprintf(buf, sizeof buf, "RECALL r%d hp%.1f out", round, me.health());
            say(buf);
        } catch (const std::exception& e) {
            std::cout << "siege error recall: " << e.what() << '\n';
        }
    } else {
        phase = RALLY;
    }
}

void Bot::fight_turn(int round) {
    auto& me = cambc::self();
    const std::uint8_t bit = static_cast<std::uint8_t>(1u << slot);
    if (sm(0) == ST_RETREAT) {
        phase = RETREAT;
        retreat_turn(round);
        return;
    }
    const cambc::camp* tgt = pick_target(*target, me.position(), false);
    if (!tgt) {
        // members not in vision yet: walk towards the nearest member tile
        vec2 goal = target->camps()[0].spawn_position();
        for (const auto& c : *target)
            if (cheb(c.spawn_position(), me.position()) < cheb(goal, me.position())) goal = c.spawn_position();
        move_near(goal);
        return;
    }
    const bool melee = me.attack_range() <= 1;
    const float hp = me.health();
    const float observed = last_hp_end >= 0.0f ? last_hp_end - hp : 0.0f;
    if (exposed(*target, me.position())) sm_set(3, sm(3) | bit);
    if (observed > 0.5f) {
        const int R = reach(*target);
        const int d = cheb(me.position(), tgt->position());
        bool covered = false;
        // others: allies still engaged (leavers clear their bit when they decide to leave)
        const bool others = (sm(3) & static_cast<std::uint8_t>(~bit)) != 0;
        for (auto& c : cambc::vision::champions(team::ally)) {
            if (c.champion_name() == me.champion_name() || !c.is_alive()) continue;
            const int dc = cheb(c.position(), tgt->position());
            if (dc <= 1 && c.attack_range() <= 1 && c.health() > observed * 1.5f) covered = true;
        }
        const int n_out = std::max(0, R - d);
        const int n_lost = (covered && melee) ? 0 : (others ? std::min(n_out, 2) : n_out);
        const float thr = observed * static_cast<float>(1 + n_lost) * static_cast<float>(S_LEAVE100) / 100.0f;
        if (hp <= thr + 0.5f) {
            char buf[96];
            if (covered && melee) {
                // step back out of melee contact (strikes go to the nearer ally), then recall
                const auto blocked = blocked_tiles();
                vec2 best = me.position();
                int best_d = -1;
                for (auto t : blocked.reachable_tiles(me.position(), 1).positions()) {
                    const int dt = cheb(t, tgt->position());
                    if (dt > best_d) best_d = dt, best = t;
                }
                if (best != me.position()) {
                    try {
                        me.move_absolute(best);
                    } catch (const std::exception& e) {
                        std::cout << "siege error stepback: " << e.what() << '\n';
                    }
                }
                sm_set(3, sm(3) & static_cast<std::uint8_t>(~bit));
                std::snprintf(buf, sizeof buf, "LEAVE_COVERED r%d hp%.1f obs%.1f to%d,%d", round, hp, observed, me.position().x,
                              me.position().y);
                say(buf);
                if (S_RECALLIN) {
                    try {
                        me.recall();
                        phase = RECALLING;
                    } catch (const std::exception& e) {
                        std::cout << "siege error recall-covered: " << e.what() << '\n';
                        phase = RETREAT;
                    }
                } else {
                    phase = RETREAT;  // walk out under cover, recall out of reach
                }
            } else {
                sm_set(0, ST_RETREAT);
                sm_set(3, sm(3) & static_cast<std::uint8_t>(~bit));
                std::snprintf(buf, sizeof buf, "LEAVE_UNCOVERED r%d hp%.1f obs%.1f nlost%d wave%u", round, hp, observed, n_lost, sm(1));
                say(buf);
                phase = RETREAT;
                retreat_turn(round);
            }
            return;
        }
    }
    attack_turn(round, true);
    if (exposed(*target, me.position())) sm_set(3, sm(3) | bit);
}

void Bot::capture_turn(int round) {
    auto& me = cambc::self();
    const int gs = group_state(*target);
    const std::uint8_t claim = sm(4);
    if (gs == 3) {
        sm_set(0, ST_DONE);
        phase = DONE;
        if (on_member(*target, me.position())) step_away(*target);
        return;
    }
    if (claim == 0) {
        bool near = false;
        for (const auto& c : *target)
            if (cheb(c.spawn_position(), me.position()) <= 3) near = true;
        if (near) {
            sm_set(4, static_cast<std::uint8_t>(slot + 1));
            char buf[64];
            std::snprintf(buf, sizeof buf, "CLAIM r%d slot%d", round, slot);
            say(buf);
        }
    }
    if (sm(4) == slot + 1) {
        if (on_member(*target, me.position())) return;
        const auto blocked = blocked_tiles();
        vec2 goal{-1, -1};
        for (const auto& c : *target) {
            const auto t = c.spawn_position();
            if (blocked.is_set(t)) continue;
            if (goal.x < 0 || cheb(t, me.position()) < cheb(goal, me.position())) goal = t;
        }
        if (goal.x >= 0) move_near(goal);
    } else if (on_member(*target, me.position())) {
        step_away(*target);
    }
}

void Bot::play_turn() {
    if (S_MODE == 0) return;
    auto& me = cambc::self();
    const int type = static_cast<int>(me.champion_name());
    dirty();
    if (!((S_ACTIVE >> type) & 1)) {
        // inactive: make room on the spawn
        if (!me.is_alive() || !me.can_move() || S_GX < 0) return;
        const vec2 goal{S_GX, S_GY};
        const auto blocked = blocked_tiles();
        vec2 best = me.position();
        for (auto t : blocked.reachable_tiles(me.position(), 1).positions())
            if (cambc::map::spawn_region(team::ally).is_set(t) && cheb(t, goal) > cheb(best, goal)) best = t;
        if (best != me.position()) {
            try {
                me.move_absolute(best);
            } catch (...) {
            }
        }
        return;
    }
    const int round = static_cast<int>(cambc::round_number());
    if (phase == INIT) {
        slot = slot_of(type);
        target = group_at(S_GX, S_GY);
        farm = S_FARM ? group_at(S_FX, S_FY0 + S_FSTEP * slot) : std::nullopt;
        // tank order among equally near melee: most health first (M6: priority, then draft id)
        int rank = 0;
        if (me.attack_range() <= 1) {
            const float hp0 = me.base_stats().base_health;
            for (auto& c : cambc::vision::champions(team::ally))
                if ((S_ACTIVE >> static_cast<int>(c.champion_name())) & 1)
                    if (c.attack_range() <= 1 && c.champion_name() != me.champion_name()) {
                        const float h = c.base_stats().base_health;
                        // S_TANKORDER 0: most health tanks first; 1: least health first (the
                        // healthiest melee is the last tank, the one that must walk out uncovered)
                        const bool before = S_TANKORDER ? h < hp0 : h > hp0;
                        if (before || (h == hp0 && static_cast<int>(c.champion_name()) < type)) ++rank;
                    }
            me.set_priority(50 - 10 * rank);
        } else {
            me.set_priority(0);
        }
        phase = S_FARM ? FARM : RALLY;
        char buf[128];
        std::snprintf(buf, sizeof buf, "INIT r%d %s slot%d of%d target%d farm%d prio%d", round, kTypeName[type], slot,
                      active_count(), target ? 1 : 0, farm ? 1 : 0, me.attack_range() <= 1 ? 50 - 10 * rank : 0);
        say(buf);
    }
    status(round);
    if (!me.is_alive()) {
        last_hp_end = -1.0f;
        if (phase == FIGHT || phase == RETREAT) {
            sm_set(3, sm(3) & static_cast<std::uint8_t>(~(1u << slot)));
            phase = RECALLING;
        }
        return;
    }
    const long gold = me.gold_count(), gems = me.gem_count();
    if (last_gold >= 0 && (gold != last_gold || gems != last_gems)) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "GAIN r%d gold%+ld gems%+ld", round, gold - last_gold, gems - last_gems);
        say(buf);
    }
    last_gold = gold;
    last_gems = gems;
    if (me.is_recalling() || me.is_submitting_gems()) {
        last_hp_end = me.health();
        return;
    }
    const bool at_spawn = cambc::map::spawn_region(team::ally).is_set(me.position());
    try {
        if (phase == RECALLING) {
            if (at_spawn && me.health() < me.max_health()) {
                last_hp_end = me.health();
                return;  // heal at this turn's end (M18)
            }
            phase = RALLY;
        }
        if (phase == SHOP || (phase == FARM && S_BUY >= 0 && !bought &&
                              gold >= static_cast<long>(cambc::item_type{static_cast<std::uint32_t>(S_BUY)}.cost()) * S_BUYN)) {
            phase = SHOP;
            if (at_spawn) {
                const cambc::item_type item{static_cast<std::uint32_t>(S_BUY)};
                if (me.can_buy(item, S_BUYN)) {
                    me.buy(item, S_BUYN);
                    char buf[96];
                    std::snprintf(buf, sizeof buf, "BUY r%d item%d x%d hp%.0f/%.0f", round, S_BUY, S_BUYN, me.health(), me.max_health());
                    say(buf);
                }
                bought = true;
                phase = RALLY;
            } else {
                int best = 1 << 20;
                vec2 goal = me.position();
                for (auto t : cambc::map::spawn_region(team::ally).positions())
                    if (cheb(t, me.position()) < best) best = cheb(t, me.position()), goal = t;
                move_near(goal);
            }
        } else if (target && (group_state(*target) == 2 || group_state(*target) == 3 || sm(0) == ST_DONE)) {
            if (phase != DONE) capture_turn(round);
        } else if (phase == FARM) {
            farm_turn(round);
        } else if (phase == RALLY) {
            rally_turn(round);
        } else if (phase == FIGHT) {
            fight_turn(round);
        } else if (phase == RETREAT) {
            retreat_turn(round);
        }
    } catch (const std::exception& e) {
        std::cout << "siege error turn: " << e.what() << '\n';
    }
    last_hp_end = me.health();
}
