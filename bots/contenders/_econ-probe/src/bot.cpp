// Economy probe: one or more champions clear a chosen camp group, capture it, optionally wait
// for its respawn and repeat, then tribute; every turn logs health, stats, gold, gems and the
// group's members, and every hit logs the target's health before and after. Knobs: knobs.h.
#include <cambc.h>
#include "bot.h"
#include "knobs.h"
#include <algorithm>
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

int cheb(vec2 a, vec2 b) { return static_cast<int>(a.chebyshev_distance(b)); }

char camp_char(const cambc::camp& c) {
    if (c.is_wolf()) return 'w';
    if (c.is_elk()) return 'e';
    if (c.is_gem_cluster()) return 'g';
    if (c.is_gold_cluster()) return '$';
    if (c.is_monolith()) return 'M';
    return 'G';
}

void say(const char* text) { std::cout << text << '\n'; }

std::optional<cambc::map::camp_group> group_at(int x, int y) {
    if (x < 0 || y < 0) return std::nullopt;
    for (auto& g : cambc::map::camp_groups())
        for (const auto& c : g)
            if (c.spawn_position().x == x && c.spawn_position().y == y) return g;
    return std::nullopt;
}

// 0 unseen, 1 alive, 2 reward available, 3 respawning (timer in *timer).
int group_state(const cambc::map::camp_group& g, int* timer) {
    try {
        const auto s = g.state();
        if (std::holds_alternative<cambc::map::camp_group_alive>(s)) return 1;
        if (std::holds_alternative<cambc::map::camp_group_reward_available>(s)) return 2;
        *timer = std::get<cambc::map::camp_group_respawning>(s).timer;
        return 3;
    } catch (...) {
        return 0;
    }
}

// Living visible camp tiles, refreshed lazily after every action that can kill (fuel: one
// occupancy query instead of three queries per member).
cambc::bitboard g_live;
bool g_live_dirty = true;

void mark_dirty() { g_live_dirty = true; }

bool living(const cambc::camp& c) {
    if (g_live_dirty) {
        g_live = cambc::vision::awake_camp_tiles() | cambc::vision::asleep_camp_tiles();
        g_live_dirty = false;
    }
    return g_live.is_set(c.spawn_position());
}

int lowest_active_type() {
    for (int t = 0; t < 13; ++t)
        if ((P_ACTIVE >> t) & 1) return t;
    return -1;
}

cambc::bitboard blocked_tiles() {
    auto b = cambc::map::walls() | cambc::map::holes() | cambc::map::altars() |
             cambc::map::spawn_region(team::enemy) | cambc::vision::champion_tiles() |
             cambc::vision::camp_tiles();
    b.set(cambc::self().position(), false);
    return b;
}

bool on_member_tile(const cambc::map::camp_group& g, vec2 p) {
    for (const auto& c : g)
        if (c.spawn_position() == p) return true;
    return false;
}

// Step to an adjacent free tile that is not a member tile (prefers tiles nearer the group).
void step_off(const cambc::map::camp_group& g, int round) {
    auto& me = cambc::self();
    if (!me.can_move() || !on_member_tile(g, me.position())) return;
    const auto blocked = blocked_tiles();
    for (auto t : blocked.reachable_tiles(me.position(), 1).positions()) {
        if (t == me.position() || on_member_tile(g, t)) continue;
        try {
            if (me.move_absolute(t)) {
                char buf[96];
                std::snprintf(buf, sizeof buf, "STEPOFF r%d to %d,%d", round, t.x, t.y);
                say(buf);
                return;
            }
        } catch (const std::exception& e) {
            std::cout << "probe error stepoff: " << e.what() << '\n';
        }
    }
}

// Order score: higher first.
float order_score(const cambc::camp& c, vec2 me) {
    if (P_ORDER == 1) return -c.health();
    if (P_ORDER == 2) return -static_cast<float>(cheb(c.position(), me)) * 100000.0f - c.health();
    return c.strength() / c.max_health() * 1.0e6f - c.health();
}

void log_hit(const char* what, int round, const cambc::camp& c, float h0, float h1) {
    char buf[160];
    std::snprintf(buf, sizeof buf, "%s r%d %c@%d,%d %.4f->%.4f d%.4f", what, round, camp_char(c),
                  c.spawn_position().x, c.spawn_position().y, h0, h1, h0 - h1);
    say(buf);
}

// Health of every member (dead or unseen: -1) so spells' splash can be logged.
std::vector<float> member_hp(const cambc::map::camp_group& g) {
    std::vector<float> out;
    for (const auto& c : g) out.push_back(living(c) ? c.health() : -1.0f);
    return out;
}

void log_group_delta(const char* what, int round, const cambc::map::camp_group& g, const std::vector<float>& before) {
    auto& me = cambc::self();
    mark_dirty();
    std::string line = std::string(what) + " r" + std::to_string(round) + " cm" + std::to_string(me.can_move() ? 1 : 0) +
                       " ca" + std::to_string(me.can_attack() ? 1 : 0) + " hp" + std::to_string(me.health());
    int i = 0;
    for (const auto& c : g) {
        bool seen = false;
        try {
            seen = c.is_visible();
        } catch (...) {
        }
        const float h1 = !seen ? before[i] : (living(c) ? c.health() : 0.0f);
        if (before[i] >= 0.0f && h1 != before[i]) {
            char buf[80];
            std::snprintf(buf, sizeof buf, " %c@%d,%d %.4f->%.4f", camp_char(c), c.spawn_position().x,
                          c.spawn_position().y, before[i], h1);
            line += buf;
        }
        ++i;
    }
    say(line.c_str());
}

bool basic_attack(const cambc::camp& c, int round, const char* tag) {
    auto& me = cambc::self();
    if (!me.can_attack() || !living(c) || cheb(me.position(), c.position()) > me.attack_range()) return false;
    try {
        const float h0 = c.health();
        me.attack(c);
        mark_dirty();
        const float h1 = living(c) ? c.health() : 0.0f;
        log_hit(tag, round, c, h0, h1);
        return true;
    } catch (const std::exception& e) {
        std::cout << "probe error attack: " << e.what() << '\n';
        return false;
    }
}

}  // namespace

void Bot::log_status(int round) {
    auto& me = cambc::self();
    char buf[256];
    const int type = static_cast<int>(me.champion_name());
    if (!me.is_alive()) {
        std::snprintf(buf, sizeof buf, "R%d %s dead", round, kTypeName[type]);
        say(buf);
        return;
    }
    const auto p = me.position();
    const int channel = (me.is_recalling() || me.is_submitting_gems() || me.is_teleporting()) ? me.remaining_channel_time() : -1;
    std::snprintf(buf, sizeof buf, "R%d %s hp%.4f/%.1f prev%.4f str%.3f foc%.3f p%d,%d g%u m%u ph%d ch%d st%u bank%u",
                  round, kTypeName[type], me.health(), me.max_health(), last_hp_end, me.strength(), me.focus(),
                  p.x, p.y, me.gold_count(), me.gem_count(), phase, channel,
                  static_cast<unsigned>(me.state().bits()), cambc::global_submitted_gems(team::ally));
    std::string line = buf;
    const auto* g = target_group ? &*target_group : nullptr;
    if (phase == 0 && gold_group) g = &*gold_group;
    if (g && P_VERBOSE) {
        int timer = 0;
        const int s = group_state(*g, &timer);
        static const char* names[] = {"unseen", "alive", "reward", "resp"};
        line += " | ";
        line += names[s];
        if (s == 3) line += std::to_string(timer);
        if (g->camps().size() > 8) {
            int n = 0;
            float hp = 0.0f;
            for (const auto& c : *g)
                if (living(c)) ++n, hp += c.health();
            line += " living" + std::to_string(n) + " hp" + std::to_string(static_cast<int>(hp));
            say(line.c_str());
            return;
        }
        for (const auto& c : *g) {
            char m[64];
            try {
                if (c.is_visible()) {
                    const auto cs = c.state();
                    std::snprintf(m, sizeof m, " %c%d,%d:%.4f%s", camp_char(c), c.spawn_position().x, c.spawn_position().y,
                                  c.health(), cs.is_dead() ? "d" : (cs.is_awake() ? "a" : "s"));
                } else {
                    std::snprintf(m, sizeof m, " %c%d,%d:?", camp_char(c), c.spawn_position().x, c.spawn_position().y);
                }
            } catch (...) {
                std::snprintf(m, sizeof m, " %c%d,%d:x", camp_char(c), c.spawn_position().x, c.spawn_position().y);
            }
            line += m;
        }
    }
    say(line.c_str());
}

bool Bot::retreat_and_recall(const cambc::map::camp_group& group, int round) {
    auto& me = cambc::self();
    if (cambc::map::spawn_region(team::ally).is_set(me.position())) return false;
    int nearest = 99;
    for (const auto& c : group)
        if (living(c)) nearest = std::min(nearest, cheb(c.position(), me.position()) - c.attack_range());
    if (nearest <= 0 && me.can_move()) {
        // step to the free neighbour farthest from the living members
        const auto blocked = blocked_tiles();
        vec2 best = me.position();
        int best_d = -1;
        for (auto t : blocked.reachable_tiles(me.position(), 1).positions()) {
            int d = 99;
            for (const auto& c : group)
                if (living(c)) d = std::min(d, cheb(c.position(), t));
            if (d > best_d) best_d = d, best = t;
        }
        if (best != me.position()) {
            try {
                me.move_absolute(best);
            } catch (const std::exception& e) {
                std::cout << "probe error retreat: " << e.what() << '\n';
            }
        }
        char buf[64];
        std::snprintf(buf, sizeof buf, "RETREAT r%d", round);
        say(buf);
        return true;
    }
    try {
        me.recall();
        char buf[64];
        std::snprintf(buf, sizeof buf, "RECALL r%d hp%.1f", round, me.health());
        say(buf);
        healing = true;
    } catch (const std::exception& e) {
        std::cout << "probe error recall: " << e.what() << '\n';
    }
    return true;
}

bool Bot::fight(const cambc::map::camp_group& group, int round, bool may_capture) {
    auto& me = cambc::self();
    int timer = 0;
    const int s = group_state(group, &timer);
    const vec2 anchor = group.camps()[0].spawn_position();
    if (s == 0) {
        // walk towards the nearest member tile until it is in view
        vec2 goal = anchor;
        for (const auto& c : group)
            if (cheb(c.spawn_position(), me.position()) < cheb(goal, me.position())) goal = c.spawn_position();
        if (me.can_move()) me.move_towards(goal);
        return false;
    }
    if (s == 3) {
        step_off(group, round);
        return false;
    }
    if (s == 2) {
        const int type = static_cast<int>(me.champion_name());
        if (!may_capture || (type != lowest_active_type() && !P_ALLCAP)) {
            step_off(group, round);
            return false;
        }
        if (on_member_tile(group, me.position())) return false;
        // nearest free member tile
        const auto blocked = blocked_tiles();
        vec2 goal{-1, -1};
        for (const auto& c : group) {
            const auto t = c.spawn_position();
            if (blocked.is_set(t)) continue;
            if (goal.x < 0 || cheb(t, me.position()) < cheb(goal, me.position())) goal = t;
        }
        if (goal.x >= 0 && me.can_move()) {
            try {
                if (cheb(goal, me.position()) == 1) me.move_absolute(goal);
                else me.move_towards(goal);
                if (me.position() == goal) {
                    char buf[64];
                    std::snprintf(buf, sizeof buf, "ONTILE r%d %d,%d", round, goal.x, goal.y);
                    say(buf);
                }
            } catch (const std::exception& e) {
                std::cout << "probe error capture-move: " << e.what() << '\n';
            }
        }
        return false;
    }
    // alive: choose the target
    if (P_RECALLPCT > 0 && me.health() < me.max_health() * static_cast<float>(P_RECALLPCT) / 100.0f)
        return retreat_and_recall(group, round);
    const cambc::camp* target = nullptr;
    float best = -1e30f;
    for (const auto& c : group) {
        if (!living(c)) continue;
        const float sc = order_score(c, me.position());
        if (!target || sc > best) target = &c, best = sc;
    }
    if (!target) {
        // members not visible yet: approach
        if (me.can_move()) me.move_towards(anchor);
        return false;
    }
    const bool ranged = me.attack_range() > 1;
    auto abilities = me.abilities();
    const auto awake_any = [&] {
        for (const auto& c : group)
            try {
                if (living(c) && c.is_awake()) return true;
            } catch (...) {
            }
        return false;
    };
    // 1. casts before moving
    for (auto& ab : abilities) {
        try {
            if (!ab.is_ready()) continue;
            const auto before = member_hp(group);
            if ((P_SPELLS & 16) && ab.is_shield() && awake_any()) {
                if (ab.use()) {
                    char buf[80];
                    std::snprintf(buf, sizeof buf, "SHIELD r%d slot%u hp%.4f", round, ab.slot(), me.health());
                    say(buf);
                    break;
                }
            }
            if ((P_SPELLS & 2) && (ab.is_damage() || ab.is_cc()) && ab.can_target_camps() &&
                cheb(me.position(), target->position()) <= 4) {
                if (ab.use(*target)) {
                    log_group_delta("SPELL", round, group, before);
                    break;
                }
            }
            if ((P_SPELLS & 4) && ab.is_aoe_damage() && cheb(me.position(), target->position()) <= 4) {
                if (ab.use(target->position())) {
                    log_group_delta("AOE", round, group, before);
                    break;
                }
            }
            if ((P_SPELLS & 8) && ab.is_dash()) {
                // landing tile within 2 adjacent to the most living members
                const auto blocked = blocked_tiles();
                vec2 land{-1, -1};
                int land_n = 0;
                const auto p = me.position();
                for (int dx = -2; dx <= 2; ++dx)
                    for (int dy = -2; dy <= 2; ++dy) {
                        const vec2 t{p.x + dx, p.y + dy};
                        if (t == p || t.x < 0 || t.y < 0 || t.x >= cambc::map::width() || t.y >= cambc::map::height())
                            continue;
                        if (blocked.is_set(t)) continue;
                        int n = 0;
                        for (const auto& c : group)
                            if (living(c) && cheb(c.position(), t) <= 1) ++n;
                        if (n > land_n) land_n = n, land = t;
                    }
                if (land.x >= 0 && ab.use(land)) {
                    log_group_delta("DASH", round, group, before);
                    break;
                }
            }
        } catch (const std::exception& e) {
            std::cout << "probe error cast: " << e.what() << '\n';
        }
    }
    // 2. basic attack in range (the chosen target, else any living member in range)
    auto attack_any = [&](const char* tag) {
        if (target && living(*target) && basic_attack(*target, round, tag)) return true;
        const cambc::camp* alt = nullptr;
        float sc_best = -1e30f;
        for (const auto& c : group) {
            if (!living(c) || cheb(me.position(), c.position()) > me.attack_range()) continue;
            const float sc = order_score(c, me.position());
            if (!alt || sc > sc_best) alt = &c, sc_best = sc;
        }
        return alt && basic_attack(*alt, round, tag);
    };
    bool hit = attack_any("ATK");
    // 3. move into range when needed (melee may still attack afterwards)
    const int want = ranged ? std::min(P_RANGE, me.attack_range()) : 1;
    if (!living(*target)) {
        target = nullptr;
        for (const auto& c : group) {
            if (!living(c)) continue;
            const float sc = order_score(c, me.position());
            if (!target || sc > best) target = &c, best = sc;
        }
        if (!target) return hit;
    }
    if (!hit && me.can_move() && cheb(me.position(), target->position()) > want) {
        try {
            me.move_towards(target->position());
        } catch (const std::exception& e) {
            std::cout << "probe error move: " << e.what() << '\n';
        }
        hit = attack_any("ATK");
    }
    // 4. attack reset for a second hit
    if (hit && (P_SPELLS & 1)) {
        for (auto& ab : abilities) {
            try {
                if (ab.is_attack_reset() && ab.is_ready() && ab.use()) {
                    char buf[48];
                    std::snprintf(buf, sizeof buf, "RESET r%d slot%u", round, ab.slot());
                    say(buf);
                    for (const auto& c : group)
                        if (living(c)) {
                            target = &c;
                            break;
                        }
                    // re-pick by order
                    best = -1e30f;
                    for (const auto& c : group) {
                        if (!living(c)) continue;
                        const float sc = order_score(c, me.position());
                        if (sc > best) target = &c, best = sc;
                    }
                    attack_any("ATK2");
                    break;
                }
            } catch (const std::exception& e) {
                std::cout << "probe error reset: " << e.what() << '\n';
            }
        }
    }
    return hit;
}

void Bot::shop(int round) {
    auto& me = cambc::self();
    const int items[2] = {P_BUY, P_BUY2};
    while (buys_done < 2 && items[buys_done] >= 0) {
        const cambc::item_type item{static_cast<std::uint32_t>(items[buys_done])};
        if (!me.can_buy(item, P_BUYN)) break;
        me.buy(item, P_BUYN);
        char buf[128];
        std::snprintf(buf, sizeof buf, "BUY r%d item%d x%d hp%.2f/%.2f str%.3f foc%.3f g%u", round, items[buys_done], P_BUYN,
                      me.health(), me.max_health(), me.strength(), me.focus(), me.gold_count());
        say(buf);
        ++buys_done;
    }
}

void Bot::tribute(int round) {
    auto& me = cambc::self();
    if (me.gem_count() == 0) {
        phase = 4;
        return;
    }
    vec2 best{-1, -1};
    for (auto a : cambc::map::altars().positions())
        if (best.x < 0 || cheb(a, me.position()) < cheb(best, me.position())) best = a;
    if (best.x < 0) return;
    if (cheb(best, me.position()) <= 1) {
        try {
            me.submit_gems(best);
            char buf[80];
            std::snprintf(buf, sizeof buf, "TRIBUTE r%d bag%u ch%d", round, me.gem_count(),
                          me.is_submitting_gems() ? me.remaining_channel_time() : -1);
            say(buf);
        } catch (const std::exception& e) {
            std::cout << "probe error tribute: " << e.what() << '\n';
        }
        return;
    }
    if (me.can_move()) me.move_towards(best);
}

void Bot::play_turn() {
    if (P_MODE == 0) return;
    auto& me = cambc::self();
    const int type = static_cast<int>(me.champion_name());
    if (!((P_ACTIVE >> type) & 1)) {
        // inactive: make room on the spawn, moving to the free spawn tile farthest from the target
        if (!me.is_alive() || !me.can_move() || P_GX < 0) return;
        const vec2 goal{P_GX, P_GY};
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
    mark_dirty();
    if (phase < 0) {
        target_group = group_at(P_GX, P_GY);
        gold_group = group_at(P_FX, P_FY);
        phase = P_BUY >= 0 ? (gold_group ? 0 : 1) : 2;
        char buf[128];
        std::snprintf(buf, sizeof buf, "INIT r%d %s target%d gold%d phase%d range%d", round, kTypeName[type],
                      target_group ? 1 : 0, gold_group ? 1 : 0, phase, me.attack_range());
        say(buf);
    }
    log_status(round);
    if (!me.is_alive()) {
        last_hp_end = -1.0f;
        return;
    }
    const long gold = me.gold_count(), gems = me.gem_count();
    if (last_gold >= 0 && (gold != last_gold || gems != last_gems)) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "GAIN r%d gold%+ld gems%+ld", round, gold - last_gold, gems - last_gems);
        say(buf);
        if (gold > last_gold || gems > last_gems) {
            if (phase == 2) {
                ++loops_done;
                if (loops_done >= P_LOOPS) phase = P_TRIBUTE ? 3 : 4;
            } else if (phase == 0) {
                const int need = cambc::item_type{static_cast<std::uint32_t>(P_BUY)}.cost() * P_BUYN;
                if (gold >= need) phase = 1;
            }
        }
    }
    last_gold = gold;
    last_gems = gems;
    if (me.is_recalling() || me.is_submitting_gems() || round < P_DELAY) {
        last_hp_end = me.health();
        return;
    }
    const bool at_spawn = cambc::map::spawn_region(team::ally).is_set(me.position());
    if (healing) {
        if (at_spawn && me.health() < me.max_health()) {
            last_hp_end = me.health();
            return;
        }
        healing = false;
    }
    try {
        if (phase == 0 && gold_group) fight(*gold_group, round, true);
        else if (phase == 1) {
            if (at_spawn) {
                shop(round);
                phase = 2;
            } else {
                int best = 1 << 20;
                vec2 goal = me.position();
                for (auto t : cambc::map::spawn_region(team::ally).positions())
                    if (cheb(t, me.position()) < best) best = cheb(t, me.position()), goal = t;
                if (best <= 8) {
                    if (me.can_move()) me.move_towards(goal);
                } else {
                    me.recall();
                    say("RECALL-SHOP");
                }
            }
        } else if (phase == 2 && target_group) {
            if (at_spawn) shop(round);
            fight(*target_group, round, true);
        } else if (phase == 3) {
            if (target_group) step_off(*target_group, round);
            tribute(round);
        }
    } catch (const std::exception& e) {
        std::cout << "probe error turn: " << e.what() << '\n';
    }
    last_hp_end = me.health();
}
