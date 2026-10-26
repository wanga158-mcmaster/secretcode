// Member actions: zone tasks (clusters and small camps near the anchor), convergence on the focus
// group (tanks in front), capture, banking, recovery and following the anchor.
#include <variant>
#include "bot.h"
#include "config.h"
#include "model.h"
#include "camp_actions.h"
#include "nav_journal.h"
#include "follow_route.h"

namespace wb {

// Living visible member of group g to strike first: the highest strength per health (Smith order),
// harmless members last; ties by distance to us. With max_dist >= 0 only members within that
// Chebyshev distance of us count.
int Bot::camp_target(int g, int max_dist) const {
    const auto& grp = world_.groups[static_cast<std::size_t>(g)];
    int best = -1;
    float best_key = -1;
    int best_d = 1 << 20;
    for (int i = 0; i < static_cast<int>(grp.members.size()); ++i) {
        const auto& m = grp.members[static_cast<std::size_t>(i)];
        if (!living_.is_set(m.tile)) continue;
        const int d = cheb(m.tile, pos_);
        if (max_dist >= 0 && d > max_dist) continue;
        const float key = m.str / std::max(1.0f, m.cur > 0 ? m.cur : m.hp);
        if (key > best_key + 1e-6f || (std::abs(key - best_key) <= 1e-6f && d < best_d)) {
            best_key = key;
            best_d = d;
            best = i;
        }
    }
    return best;
}

bool Bot::tank_engaged(int g) const {
    const auto& grp = world_.groups[static_cast<std::size_t>(g)];
    auto adjacent = [&](vec2 p) {
        for (const auto& m : grp.members)
            if (living_.is_set(m.tile) && cheb(m.tile, p) <= 1) return true;
        return false;
    };
    if (kit_->tank && adjacent(pos_)) return true;
    for (const auto& a : vis_allies_)
        if (a.kit->tank && adjacent(a.pos)) return true;
    return false;
}

// Strike group g from where we stand: a stun on its most dangerous awake member in reach first (a
// stunned camp skips its next attack), else a damage spell, then the basic attack (with a reset) on
// the best member in attack range.
bool Bot::attack_camp(int g) {
    auto& me = cambc::self();
    const int ar = me.attack_range();
    const int mi = camp_target(g, ar);
    bool acted = false;
    using K = cambc::ability_kind;
    const auto& grp = world_.groups[static_cast<std::size_t>(g)];
    try {
        // Casts on camps: a stun on an awake harmful member, else a damage spell on the target.
        if (can_cast_now()) {
            for (int si : {kit_->stun_slot, kit_->nuke_slot, kit_->aoe_slot}) {
                if (si < 0 || acted || !slot_ready(si)) continue;
                const auto& s = kit_->slots[static_cast<std::size_t>(si)];
                if (s.kind == static_cast<int>(K::attack_reset) || s.kind == static_cast<int>(K::dash)) continue;
                if (s.excl_move && !me.can_move()) continue;
                const bool stun = si == kit_->stun_slot;
                const int ti = stun ? camp_target(g, static_cast<int>(s.range)) : mi;
                if (ti < 0) continue;
                const auto& m = grp.members[static_cast<std::size_t>(ti)];
                if (m.str <= 0 && (stun || m.cur < 200)) continue;  // a basic attack does for clusters
                if (stun && !awake_.is_set(m.tile)) continue;       // the strike wakes it anyway
                if (cheb(pos_, m.tile) > static_cast<int>(s.range)) continue;
                const auto& camp = world_.camp_of(g, m);
                if (!camp.is_visible()) continue;
                if (s.t_camps) acted = slot_use(si, camp);
                else if (s.t_ground) acted = slot_use(si, m.tile);
            }
        }
        if constexpr (cfg::camp_retarget) {
            // Refresh this group's visible members while retaining the action reserve. A lethal
            // cast leaves the ordinary attack available; never dispatch it at its dead target.
            auto choose = [&]() {
                if (fuel() < cfg::fuel_reserve) return -1;
                const auto visible = cambc::vision::visible_tiles();
                auto& current = world_.groups[static_cast<std::size_t>(g)];
                for (auto& member : current.members) {
                    if (fuel() < cfg::fuel_reserve) return -1;
                    if (!living_.is_set(member.tile)) continue;
                    if (!visible.is_set(member.tile)) continue;
                    const auto& camp = world_.camp_of(g, member);
                    if (!camp.is_alive()) {
                        living_.set(member.tile, false);
                        member.dead = true;
                        member.cur = 0;
                    } else member.cur = camp.health();
                }
                return camp_target(g, ar);
            };
            const auto strike = [&](int target) {
                const auto& member = grp.members[static_cast<std::size_t>(target)];
                const auto& camp = world_.camp_of(g, member);
                if (!camp.is_visible() || !camp.is_alive()) return false;
                me.attack(camp);
                return true;
            };
            const auto reset = [&]() {
                if (kit_->reset_slot < 0 || !can_cast_now() || !slot_ready(kit_->reset_slot)) return false;
                // Do not consume the one cast when there is no useful living target in range.
                const int target = choose();
                if (target < 0 || grp.members[static_cast<std::size_t>(target)].str <= 0) return false;
                return slot_use(kit_->reset_slot);
            };
            if (camp_basic_sequence([&]() { return me.can_attack(); }, choose, strike, reset)) acted = true;
        } else if (mi >= 0 && me.can_attack()) {
            const auto& m = grp.members[static_cast<std::size_t>(mi)];
            const auto& camp = world_.camp_of(g, m);
            if (camp.is_visible()) {
                me.attack(camp);
                acted = true;
                if (kit_->reset_slot >= 0 && m.str > 0 && !me.can_attack() && can_cast_now() && slot_ready(kit_->reset_slot)
                    && camp.is_visible() && slot_use(kit_->reset_slot) && me.can_attack())
                    me.attack(camp);
            }
        }
    } catch (const cambc::error& e) {
        log_error("warband error camp r%d: %s\n", round_, e.what());
    }
    return acted;
}

// Clear group g. Melee members walk to a free tile next to a living member, ranged members to a free
// tile within their range; ranged members hold fire on a sleeping dangerous group until a tank is
// adjacent (the first hit wakes the whole group, which then strikes the nearest champion, M5, M9).
bool Bot::clear_group(int g, bool allow_wake) {
    auto& me = cambc::self();
    const auto& grp = world_.groups[static_cast<std::size_t>(g)];
    const int mi = camp_target(g, -1);
    if (mi < 0) return goto_group(g, 1);
    const auto& m = grp.members[static_cast<std::size_t>(mi)];
    bool awake = false;
    for (const auto& a : awake_list_)
        if (a.group == g) awake = true;
    const bool dangerous = !grp.harmless;
    const int range = me.attack_range();
    // Tank rotation: a hurt tank steps out of range when another ally can take the hits.
    if (dangerous && awake && hp_ < cfg::tank_swap_frac * maxhp_) {
        bool relief = false;
        for (const auto& a : vis_allies_)
            if (a.frac() > 0.6f && cheb(a.pos, m.tile) <= 2) relief = true;
        if (relief && in_camp_danger(pos_, -1)) {
            std::vector<vec2> threats;
            for (const auto& c : awake_list_)
                if (c.range > 0) threats.push_back(c.tile);
            if (step_away(threats, 1)) return true;
        }
    }
    const bool may_strike = !dangerous || awake || allow_wake;
    if (!kit_->melee && dangerous && !awake) {
        // Ranged: wait for a tank in contact when the pack has one close by, standing behind it
        // (never nearer the group than the tank, so the approach lane stays open).
        // The wait is bounded (hold_patience): in a corridor a waiting ranged champion can block the
        // tank behind it, and a few camp hits cost less than a deadlock.
        int tank_d = 99;
        for (const auto& a : vis_allies_)
            if (a.kit->tank) tank_d = std::min(tank_d, cheb(a.pos, m.tile));
        if (hold_group_ != g) { hold_group_ = g; hold_since_ = round_; }
        if (tank_d <= 6 && !tank_engaged(g) && round_ - hold_since_ < cfg::hold_patience) {
            const int hold = std::max(range + 1, tank_d + 1);
            const int d = cheb(pos_, m.tile);
            bool moved = false;
            if (d < hold) moved = step_away({m.tile}, 0);
            else if (d > hold + 1) moved = goto_tile(m.tile, hold);
            if (moved || d >= hold) return true;
        }
    }
    if (may_strike && camp_target(g, range) >= 0) {
        attack_camp(g);
        if (!kit_->melee || camp_target(g, range) >= 0) return true;
    }
    if constexpr (cfg::debug_every > 0) {
        if (round_ % cfg::debug_every == 0 && round_ <= cfg::debug_until)
            std::printf("C r%d me%d g%d mi%d tile(%d,%d) awake%d strike%d melee%d canmove%d living%u fuel%ld inrange%d range%d canatk%d\n", round_, me_, g, mi,
                m.tile.x, m.tile.y, awake ? 1 : 0, may_strike ? 1 : 0, kit_->melee ? 1 : 0, me.can_move() ? 1 : 0, living_.count(), fuel(),
                camp_target(g, range), range, me.can_attack() ? 1 : 0);
    }
    if (!me.can_move()) return true;
    // Walk to an attack tile: free, not a camp tile, within our range of a living member.
    bitboard roots;
    for (const auto& mm : grp.members)
        if (living_.is_set(mm.tile)) roots.draw_chebyshev(mm.tile, static_cast<float>(range));
    const unsigned raw_roots = roots.count();
    roots &= ~blocked();
    roots &= ~(world_.camp_tiles & ~world_.tiles(g));  // dead tiles of this group are free ground
    if (roots.is_empty()) {
        const bool moved = goto_tile(m.tile, range);
        if constexpr (cfg::debug_every > 0) {
            if (round_ % cfg::debug_every == 0 && round_ <= cfg::debug_until)
                std::printf("C3 r%d me%d raw%u moved%d\n", round_, me_, raw_roots, moved ? 1 : 0);
        }
    } else if (fuel() > 15000) {
        bitboard walls = world_.terrain | living_;
        walls.set(pos_, false);
        bitboard dest;
        dest.set(pos_);
        bitboard visited;
        cambc::distance_field f;
        const int bound = std::min(250, 2 * cheb(pos_, m.tile) + 12);
        f.compute_to(walls, roots, dest, visited, static_cast<std::uint16_t>(bound));
        const auto fv = f.get(pos_);
        bool moved = false;
        if (fv == cambc::distance_field::unreachable) moved = goto_tile(m.tile, range);
        else moved = step_field(f, 0, g);
        if constexpr (cfg::debug_every > 0) {
            if (round_ % cfg::debug_every == 0 && round_ <= cfg::debug_until)
                std::printf("C2 r%d me%d roots%u fv%d moved%d n(%d,%d)=%d\n", round_, me_, roots.count(), static_cast<int>(fv), moved ? 1 : 0,
                    pos_.x, pos_.y - 1, in_bounds({pos_.x, pos_.y - 1}) ? static_cast<int>(f.get({pos_.x, pos_.y - 1})) : -1);
        }
    }
    if (may_strike && (kit_->melee || me.can_attack()) && camp_target(g, range) >= 0) attack_camp(g);
    return true;
}

bool Bot::capture_turn() {
    auto& me = cambc::self();
    // Never idle on a camp tile we are not capturing: it holds the group's respawn.
    const int under = world_.group_of(pos_);
    if (capture_group_ >= 0) {
        auto& g = world_.groups[static_cast<std::size_t>(capture_group_)];
        if (g.state != GState::reward) {
            capture_group_ = -1;
        } else {
            if (under == capture_group_) return true;   // holding the capture
            if (!me.can_move()) return true;
            if (goto_group(capture_group_, 0)) return true;
            capture_group_ = -1;
            g.banned_until = round_ + 5;
        }
    }
    if (under >= 0 && under != capture_group_) {
        auto& g = world_.groups[static_cast<std::size_t>(under)];
        if (g.state == GState::reward) {
            capture_group_ = under;   // we stand on it: capture
            return true;
        }
        // Step off the block: towards the nearest non-camp free tile.
        bitboard roots = square(pos_, 4) & ~world_.terrain & ~world_.camp_tiles & ~cambc::vision::champion_tiles();
        if (!roots.is_empty() && me.can_move()) {
            bitboard walls = world_.terrain | cambc::vision::champion_tiles() | cambc::vision::camp_tiles();
            walls.set(pos_, false);
            bitboard dest;
            dest.set(pos_);
            bitboard visited;
            cambc::distance_field f;
            f.compute_to(walls, roots, dest, visited, 10);
            auto best = pos_;
            auto best_v = f.get(pos_);
            for (auto d : kDirs) {
                const vec2 q{pos_.x + d.x, pos_.y + d.y};
                if (!free_tile(q)) continue;
                const auto v = f.get(q);
                if (v < best_v) { best_v = v; best = q; }
            }
            try {
                if (best != pos_ && me.move_absolute(best)) { nav_journal_step(pos_.x,pos_.y,best.x,best.y); pos_ = best; blocked_ready_ = false; }
            } catch (const cambc::error&) {}
        }
    }
    // A cleared group near us that nobody of ours is closer to: capture it.
    for (int gi = 0; gi < static_cast<int>(world_.groups.size()); ++gi) {
        auto& g = world_.groups[static_cast<std::size_t>(gi)];
        if (g.state != GState::reward || g.seen_round != round_ || g.banned_until > round_) continue;
        const int mine = cheb(pos_, g.center);
        if (mine > 7) continue;
        bool closer = false;
        for (const auto& a : vis_allies_) {
            const int d = cheb(a.pos, g.center);
            if (d < mine || (d == mine && a.index < me_)) closer = true;
        }
        if (closer) continue;
        capture_group_ = gi;
        if (!me.can_move()) return true;
        if (goto_group(gi, 0)) return true;
        capture_group_ = -1;
        g.banned_until = round_ + 5;
    }
    return false;
}

// A zone task: a camp group of the flock's zone (centre within zone_radius of the anchor) that I
// can clear alone, harmless clusters above all, and that no visible ally is closer to. Dangerous
// groups beyond one champion are the pack's business (the focus group, converge_turn).
bool Bot::task_feasible(int gi) const {
    const auto& g = world_.groups[static_cast<std::size_t>(gi)];
    if (g.harmless || g.state == GState::reward) return true;
    auto& me = cambc::self();
    model::Monster buf[32];
    const int n = world_.believed_monsters(gi, round_, buf);
    const auto f = model::forecast_sorted(buf, n, kit_->dps(me.strength(), me.focus()));
    return f.damage * cfg::camp_safety <= std::max(0.0f, hp_ - cfg::recall_frac * maxhp_);
}

bool Bot::task_turn() {
    if (pack_.mode == Mode::fight || pack_.mode == Mode::retreat) { task_ = -1; return false; }
    if (!pack_.has_anchor()) return false;
    // A fresh sighting near the zone contracts the flock: no new tasks.
    const bool contract = pack_.has_sighting();
    const vec2 anchor{pack_.ax, pack_.ay};
    auto valid = [&](int gi) {
        const auto& g = world_.groups[static_cast<std::size_t>(gi)];
        if (g.banned_until > round_) return false;
        if (g.state == GState::respawning && g.ready_round > round_) return false;
        if (cheb(g.center, anchor) > cfg::zone_radius + 2) return false;
        if (g.pack_job && g.state != GState::reward) return false;  // the pack's job (converge_turn)
        return true;
    };
    if (task_ >= 0 && (!valid(task_) || round_ - task_since_ > 40)) task_ = -1;
    if (task_ >= 0 && world_.groups[static_cast<std::size_t>(task_)].state == GState::reward) {
        capture_group_ = task_;   // cleared: capture_turn holds it from here
        task_ = -1;
        return capture_turn();
    }
    if (task_ < 0 && !contract) {
        int best = -1;
        float best_rate = 0;
        for (int gi = 0; gi < static_cast<int>(world_.groups.size()); ++gi) {
            const auto& g = world_.groups[static_cast<std::size_t>(gi)];
            if (cheb(g.center, anchor) > cfg::zone_radius || !valid(gi)) continue;
            const int d = cheb(g.center, pos_);
            bool closer = false;
            for (const auto& u : vis_allies_)
                if (cheb(u.pos, g.center) < d || (cheb(u.pos, g.center) == d && u.index < me_)) closer = true;
            if (closer) continue;
            const float value = static_cast<float>(g.gold) * cfg::gold_value + static_cast<float>(g.gems) * cfg::gem_value;
            const float bound = value / (static_cast<float>(d) + 7.0f);
            if (bound <= best_rate) continue;
            if (!task_feasible(gi)) continue;
            const float clear = g.harmless ? 1.0f : world_.remaining_hp(gi, round_) / std::max(1.0f, kit_->dps(cambc::self().strength(), cambc::self().focus()));
            const float rate = value / (static_cast<float>(world_.dist_to_group(pos_, gi)) + clear + 7.0f);
            if (rate > best_rate) { best_rate = rate; best = gi; }
        }
        if (best < 0) return false;
        task_ = best;
        task_since_ = round_;
    }
    if (task_ < 0) return false;
    return clear_group(task_, true);
}

bool Bot::bank_turn(bool force) {
    auto& me = cambc::self();
    const int gems = static_cast<int>(me.gem_count());
    if (gems <= 0 || world_.altars.empty()) { banking_ = false; return false; }
    const int d = world_.dist_to_altar(pos_);
    const int banked = static_cast<int>(cambc::global_submitted_gems(team::ally));
    const bool win = banked + gems >= static_cast<int>(cambc::constants::winning_gems);
    const bool late = round_ + d + gems / 10 + 25 >= static_cast<int>(cambc::constants::max_rounds);
    if (!(banking_ || force || win || late || gems >= cfg::bank_gems_far || (gems >= cfg::bank_gems && d <= cfg::bank_near)))
        return false;
    banking_ = true;
    for (auto a : world_.altars) {
        if (cheb(a, pos_) > cambc::constants::submit_gems_range) continue;
        try {
            if constexpr (cfg::action_guard) {
                if (!me.can_attack() && cambc::vision::altar_lock_tiles(team::enemy).is_set(a)) return true;
            }
            me.submit_gems(a);
            banking_ = false;
            return true;
        } catch (const cambc::error& e) {
            // Locked by the enemy: the tribute is converted into a strike on the lock.
            log_error("warband error tribute r%d: %s\n", round_, e.what());
        }
    }
    if (!me.can_move()) return true;
    if (goto_altar()) return true;
    banking_ = false;
    return false;
}

bool Bot::recover_turn() {
    auto& me = cambc::self();
    const bool low = hp_ < cfg::recall_frac * maxhp_;
    const bool rich = me.gold_count() >= static_cast<unsigned>(cfg::shop_recall_gold) && hp_ < 0.7f * maxhp_
        && pack_.mode != Mode::fight;
    if (!low && !rich) return false;
    if (at_spawn_) return false;
    if (in_camp_danger(pos_, -1)) {
        std::vector<vec2> threats;
        for (const auto& c : awake_list_)
            if (c.range > 0) threats.push_back(c.tile);
        step_away(threats, 0);
        return true;
    }
    for (const auto& u : vis_enemies_)
        if (cheb(u.pos, pos_) <= 6) { retreat(); return true; }
    try {
        me.recall();
        capture_group_ = -1;
        task_ = -1;
        return true;
    } catch (const cambc::error& e) {
        log_error("warband error recall r%d: %s\n", round_, e.what());
    }
    return false;
}

bool Bot::follow_anchor(int within) {
    if (!pack_.has_anchor()) return false;
    const vec2 a{pack_.ax, pack_.ay};
    if (cheb(pos_, a) <= within) return false;
    // Far behind: walk the focus group's static field (the anchor walks the same field) instead of
    // a long fresh search each turn; a failed local search falls back to it too.
    const int f = pack_.objective;
    if constexpr(cfg::anchor_follow_fix) {
        return dispatch_follow_route(pack_.mode,!i_lead_,cheb(pos_,a),cfg::zone_radius+2,f>=0,
            [&](int route) {
                nav_journal_route(route,cheb(pos_,a),pack_);
                return goto_tile(a,within);
            },
            [&](int route) {
                nav_journal_route(route,cheb(pos_,a),pack_);
                return goto_group(f,world_.groups[static_cast<std::size_t>(f)].range+1);
            });
    }
    if (cheb(pos_, a) > cfg::zone_radius + 2 && f >= 0) {
        nav_journal_route(1,cheb(pos_,a),pack_);
        return goto_group(f, world_.groups[static_cast<std::size_t>(f)].range + 1);
    }
    nav_journal_route(2,cheb(pos_,a),pack_);
    if (goto_tile(a, within)) return true;
    nav_journal_route(3,cheb(pos_,a),pack_);
    return f >= 0 && goto_group(f, world_.groups[static_cast<std::size_t>(f)].range + 1);
}

bool Bot::avoid_camps() {
    if (!in_camp_danger(pos_, -1)) return false;
    std::vector<vec2> threats;
    for (const auto& c : awake_list_)
        if (c.range > 0) threats.push_back(c.tile);
    return step_away(threats, 1);
}

// Convergence on the focus group: in gather mode every member near the anchor walks to the group's
// edge and waits; in clear mode they clear it together (tanks in front). A cleared focus group is
// captured by its nearest member (rewards never expire and anyone may take them, M13).
bool Bot::converge_turn() {
    const int g = pack_.objective;
    if (g < 0 || g >= static_cast<int>(world_.groups.size()) || !pack_.has_anchor()) return false;
    auto& grp = world_.groups[static_cast<std::size_t>(g)];
    const vec2 anchor{pack_.ax, pack_.ay};
    if (cheb(pos_, anchor) > cfg::zone_radius + cfg::converge_margin) return false;
    if (grp.state == GState::reward) {
        const int mine = world_.dist_to_group(pos_, g);
        if (mine > cfg::zone_radius + 2) return false;
        for (const auto& u : vis_allies_) {
            if (!(pack_.avail & (1u << u.index))) continue;
            const int d = world_.dist_to_group(u.pos, g);
            if (d < mine || (d == mine && u.index < me_)) return false;
        }
        capture_group_ = g;
        return capture_turn();
    }
    if (!grp.pack_job) return false;   // a task for one member, not a convergence
    if (pack_.mode == Mode::clear) {
        task_ = -1;
        return clear_group(g, true);
    }
    if (pack_.mode == Mode::gather) {
        task_ = -1;
        const int edge = grp.range + 1;
        if (world_.dist_to_group(pos_, g) > edge) goto_group(g, edge);
        return true;
    }
    return false;
}

} // namespace wb
