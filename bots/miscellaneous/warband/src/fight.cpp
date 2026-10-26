// Fights against champions: the local force ratio, focus choice, stun chains, burst and retreat.
#include <cmath>
#include "bot.h"
#include "config.h"
#include "model.h"

namespace wb {

namespace {
bool near_enemy_spawn(const World& w, vec2 p, int r) { return cheb(p, w.enemy_spawn_c) <= r; }
} // namespace

bool Bot::can_cast_now() const { return cambc::self().state().contains(cambc::champion_state::can_cast); }

bool Bot::slot_ready(int s) const {
    if (s < 0 || s > 1 || abil_.size() < 2 || kit_->slots[static_cast<std::size_t>(s)].kind == 0) return false;
    return abil_[static_cast<std::size_t>(s)].cooldown() == 0;
}

bool Bot::slot_use(int s) { return abil_[static_cast<std::size_t>(s)].use(); }
bool Bot::slot_use(int s, const cambc::champion& t) { return abil_[static_cast<std::size_t>(s)].use(t); }
bool Bot::slot_use(int s, const cambc::camp& t) { return abil_[static_cast<std::size_t>(s)].use(t); }
bool Bot::slot_use(int s, vec2 t) { return abil_[static_cast<std::size_t>(s)].use(t); }

// Force ratio (us / them) around this champion. Allies count within 6 tiles of us, enemies within
// 9 plus remembered ones that could be back in the fight; awake camps near us add to their side.
float Bot::local_ratio(bool include_memory) const {
    std::vector<model::Fighter> us, them;
    auto& me = cambc::self();
    const bool home = world_.my_spawn.is_set(pos_) || cheb(pos_, world_.my_spawn_c) <= 3;
    {
        model::Fighter f;
        f.hp = hp_ * (home ? 3.0f : 1.0f);
        f.dps = kit_->dps(me.strength(), me.focus());
        us.push_back(f);
    }
    for (const auto& a : vis_allies_) {
        if (a.recalling || cheb(a.pos, pos_) > 6) continue;
        model::Fighter f;
        f.hp = a.hp * (world_.my_spawn.is_set(a.pos) ? 3.0f : 1.0f);
        f.dps = a.kit->dps(a.str, a.focus);
        us.push_back(f);
    }
    for (const auto& u : vis_enemies_) {
        if (cheb(u.pos, pos_) > 9) continue;
        model::Fighter f;
        f.hp = (u.hp + u.shield) * (world_.enemy_spawn.is_set(u.pos) ? 3.0f : 1.0f);
        f.dps = u.kit->dps(u.str, u.focus);
        f.burst = u.kit->burst(u.focus, u.cd[0], u.cd[1]);
        them.push_back(f);
    }
    if (include_memory) {
        for (std::size_t i = 0; i < emem_.size(); ++i) {
            const auto& m = emem_[i];
            if (m.dead || round_ - m.seen > cfg::enemy_memory || m.seen == round_) continue;
            if (cheb(m.pos, pos_) > 9 + (round_ - m.seen)) continue;
            model::Fighter f;
            f.hp = m.hp;
            f.dps = m.dps;
            them.push_back(f);
        }
    }
    // Awake camps beside us strike whoever is nearest; charge half of them to each side's account
    // by adding to theirs (we are the side standing in range).
    if (!them.empty()) {
        float camp_dps = 0;
        for (const auto& a : awake_list_)
            if (cheb(a.tile, pos_) <= a.range) camp_dps += a.str;
        if (camp_dps > 0) {
            model::Fighter f;
            f.dps = 0.5f * camp_dps;
            them.push_back(f);
        }
    }
    return model::force_ratio(us, them);
}

int Bot::choose_focus() const {
    auto& me = cambc::self();
    float pack_dps = kit_->dps(me.strength(), me.focus());
    for (const auto& a : vis_allies_)
        if (!a.recalling && cheb(a.pos, pos_) <= 6) pack_dps += a.kit->dps(a.str, a.focus);
    int best = -1;
    float best_score = 1e30f;
    for (const auto& u : vis_enemies_) {
        const int d = cheb(u.pos, pos_);
        if (d > 7) continue;
        if (world_.enemy_spawn.is_set(u.pos)) continue;  // it heals there
        const float s = model::focus_score(u.hp + u.shield, pack_dps, u.gems, u.stun_pending(), d);
        if (s < best_score) { best_score = s; best = u.index; }
    }
    return best;
}

bool Bot::cast_stun(const Unit& t) {
    auto& me = cambc::self();
    if (kit_->stun_slot < 0 || t.kit->stun_immune || t.stun_pending()) return false;
    const auto& s = kit_->slots[static_cast<std::size_t>(kit_->stun_slot)];
    const int si = kit_->stun_slot;
    try {
        if (!can_cast_now() || !slot_ready(si)) return false;
        if (s.excl_move && !me.can_move()) {
            // Moving this turn already consumed the movement, so an exclusive cast is illegal.
            // can_move() false can also mean we never moved but are rooted; try anyway below.
        }
        const int d = cheb(pos_, t.pos);
        using K = cambc::ability_kind;
        if (s.kind == static_cast<int>(K::cc)) {
            if (d > static_cast<int>(s.range)) return false;
            return slot_use(si, t.ch);
        }
        if (s.kind == static_cast<int>(K::aoe_damage)) {
            if (d > static_cast<int>(s.range)) return false;
            return slot_use(si, t.pos);
        }
        if (s.kind == static_cast<int>(K::attack_reset)) {
            // Enhanced attack: the next basic attack stuns. Only worth it when adjacent and able.
            if (d > me.attack_range() || me.can_attack()) return false;
            return slot_use(si);
        }
        if (s.kind == static_cast<int>(K::dash)) {
            // Dash landing next to the target: radius 1 around the landing hits it.
            if (d > static_cast<int>(s.range) + 1 || d <= 1) return false;
            vec2 best{-1, -1};
            int best_d = 99;
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy) {
                    const vec2 q{t.pos.x + dx, t.pos.y + dy};
                    if (!in_bounds(q) || q == t.pos || cheb(q, pos_) > static_cast<int>(s.range)) continue;
                    if (!free_tile(q)) continue;
                    const int dd = cheb(q, pos_);
                    if (dd < best_d) { best_d = dd; best = q; }
                }
            if (best.x < 0) return false;
            const bool ok = slot_use(si, best);
            if (ok) { pos_ = best; blocked_ready_ = false; }
            return ok;
        }
    } catch (const cambc::error& e) {
        log_error("warband error stun r%d: %s\n", round_, e.what());
    }
    return false;
}

bool Bot::cast_damage(const Unit& t) {
    auto& me = cambc::self();
    using K = cambc::ability_kind;
    for (int si : {kit_->nuke_slot, kit_->aoe_slot}) {
        if (si < 0) continue;
        const auto& s = kit_->slots[static_cast<std::size_t>(si)];
        try {
            if (!can_cast_now() || !slot_ready(si)) continue;
            if (s.excl_move && !me.can_move()) continue;
            if (cheb(pos_, t.pos) > static_cast<int>(s.range)) continue;
            if (s.kind == static_cast<int>(K::damage) && s.t_enemies) return slot_use(si, t.ch);
            if (s.kind == static_cast<int>(K::aoe_damage)) return slot_use(si, t.pos);
        } catch (const cambc::error& e) {
            log_error("warband error nuke r%d: %s\n", round_, e.what());
        }
    }
    return false;
}

bool Bot::basic_attack_enemy(const Unit& t) {
    auto& me = cambc::self();
    bool hit = false;
    try {
        if (me.can_attack() && cheb(pos_, t.pos) <= me.attack_range() && t.ch.is_visible()) {
            me.attack(t.ch);
            hit = true;
        }
        // Attack reset: a second basic attack this turn.
        if (hit && kit_->reset_slot >= 0 && !me.can_attack() && can_cast_now() && slot_ready(kit_->reset_slot)
            && t.ch.is_alive() && t.ch.is_visible()) {
            if (slot_use(kit_->reset_slot) && me.can_attack()) me.attack(t.ch);
        }
    } catch (const cambc::error& e) {
        log_error("warband error attack r%d: %s\n", round_, e.what());
    }
    return hit;
}

void Bot::use_defensive() {
    if (kit_->shield_slot < 0) return;
    try {
        if (can_cast_now() && slot_ready(kit_->shield_slot)) slot_use(kit_->shield_slot);
    } catch (const cambc::error&) {
    }
}

// Engage the focus target: stun it if no stun is pending on it, burst it, close in, strike.
bool Bot::engage(const Unit& t) {
    auto& me = cambc::self();
    const int d = cheb(pos_, t.pos);
    bool acted = false;
    // Casts first: exclusive casts are only legal before moving.
    if (cast_stun(t)) acted = true;
    else if (cast_damage(t)) acted = true;
    if (!t.ch.is_alive()) return true;
    const bool chase_ok = !near_enemy_spawn(world_, t.pos, cfg::chase_spawn_guard) || d <= 2;
    if (kit_->melee) {
        if (cheb(pos_, t.pos) > 1 && me.can_move() && chase_ok) {
            if (goto_tile(t.pos, 1)) acted = true;
            pos_ = me.position();
        }
        if (basic_attack_enemy(t)) acted = true;
    } else {
        if (cheb(pos_, t.pos) <= me.attack_range()) {
            if (basic_attack_enemy(t)) acted = true;
        } else if (me.can_move() && chase_ok) {
            if (goto_tile(t.pos, me.attack_range())) acted = true;
        }
    }
    // Nothing reachable on the focus target: hit any enemy in range.
    if (me.can_attack()) {
        for (const auto& u : vis_enemies_)
            if (cheb(pos_, u.pos) <= me.attack_range() && basic_attack_enemy(u)) { acted = true; break; }
    }
    for (const auto& u : vis_enemies_)
        if (cheb(pos_, u.pos) <= 2) { use_defensive(); break; }
    return acted || !me.can_move();
}

bool Bot::retreat() {
    auto& me = cambc::self();
    std::vector<vec2> threats;
    for (const auto& u : vis_enemies_)
        if (cheb(u.pos, pos_) <= 9) threats.push_back(u.pos);
    for (const auto& m : emem_)
        if (!m.dead && m.seen != round_ && round_ - m.seen <= 3 && cheb(m.pos, pos_) <= 9) threats.push_back(m.pos);
    // Peel: a walk-compatible stun on an adjacent chaser first.
    for (const auto& u : vis_enemies_) {
        if (cheb(u.pos, pos_) > 1 || kit_->stun_slot < 0) continue;
        if (kit_->slots[static_cast<std::size_t>(kit_->stun_slot)].excl_move) continue;
        if (cast_stun(u)) break;
    }
    if (me.can_attack() && !kit_->melee) {
        // Ranged: a free shot before running is not free (shared allowance); run instead.
    }
    const bool moved = step_away(threats, 1);
    if (!moved && me.can_attack()) {
        for (const auto& u : vis_enemies_)
            if (cheb(pos_, u.pos) <= me.attack_range() && basic_attack_enemy(u)) break;
    }
    use_defensive();
    return true;
}

bool Bot::fight_turn() {
    bool any = false;
    for (const auto& u : vis_enemies_)
        if (cheb(u.pos, pos_) <= 8 && (!world_.enemy_spawn.is_set(u.pos) || cheb(u.pos, pos_) <= 2)) any = true;
    if (!any) return false;
    const float ratio = local_ratio(true);
    const bool pack_fight = pack_.mode == Mode::fight && pack_.recent(round_, 1);
    const bool engage_ok = ratio >= cfg::engage_ratio || (pack_fight && ratio >= cfg::flee_ratio);
    if (ratio < cfg::flee_ratio && !at_spawn_) return retreat();
    int focus = -1;
    if (pack_.focus >= 0)
        for (const auto& u : vis_enemies_)
            if (u.index == pack_.focus && cheb(u.pos, pos_) <= 7) focus = u.index;
    if (focus < 0) focus = choose_focus();
    if (focus < 0) return false;
    const Unit* t = nullptr;
    for (const auto& u : vis_enemies_)
        if (u.index == focus) t = &u;
    if (!t) return false;
    if (engage_ok) return engage(*t);
    // Hold: strike what is in reach without advancing, otherwise carry on with the economy.
    auto& me = cambc::self();
    if (cheb(pos_, t->pos) <= me.attack_range()) return engage(*t);
    return false;
}

} // namespace wb
