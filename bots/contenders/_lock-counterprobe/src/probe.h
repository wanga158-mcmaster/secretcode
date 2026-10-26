#pragma once
// Conditional native fixture: every position, coin, gem, cooldown and lock is reached by
// ordinary SDK actions. The full unchanged v41 Brain is called, without private-state writes.
#define main unused_production_entrypoint
#include "production_entry.h"
#undef main
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace counterprobe {
using namespace cambc;
int side() { return map::spawn_region(team::ally).is_set({1, 1}) ? 0 : 1; }
bool defending() { return side() == counterlock_active_side; }
vec2 orient(vec2 p, int origin) {
    return origin == 0 ? p : vec2{map::width()-1-p.x, map::height()-1-p.y};
}
vec2 frame(vec2 p) { return orient(p, counterlock_active_side); }
vec2 own(vec2 p) { return orient(p, side()); }
vec2 altar() { return frame({15, 15}); }
bool defender() { return defending() && self().champion_name() == champion_type::marcella; }
bool banker() { return !defending() && self().champion_name() == champion_type::ryth; }
const char* role() { return defender() ? "defender" : banker() ? "banker" : "filler"; }

void draft_turn() {
    const std::array a{champion_draft::tavi, champion_draft::marcella, champion_draft::ren};
    const std::array b{champion_draft::ryth, champion_draft::willow, champion_draft::prism};
    const auto& picks = defending() ? a : b;
    const auto chosen = draft::current_draft(team::ally);
    for (const auto pick : picks)
        if (std::find(chosen.begin(), chosen.end(), pick) == chosen.end()) { draft::pick(pick); return; }
}
int lock_hits(team owner) {
    for (const auto& lock : vision::altar_locks(owner))
        if (lock.position() == altar()) return static_cast<int>(lock.hits_remaining());
    return -1;
}
void log(const char* phase) {
    auto& me = self();
    const auto p = me.position();
    const auto a = me.abilities();
    const bool channel = me.state().contains(champion_state::submitting_gems);
    std::printf("LP phase=%s r=%u side=%c role=%s id=%u x=%d y=%d hp=%.0f str=%.0f gold=%u gems=%u bank=%u lock=%u own=%d enemy=%d channel=%d timer=%d A=%d M=%d C=%d cd0=%d cd1=%d enhanced=%d fuel=%llu\n",
        phase, round_number(), side()==0?'A':'B', role(), static_cast<unsigned>(static_cast<handle_champion_t>(me)),
        p.x, p.y, me.health(), me.strength(), me.gold_count(), me.gem_count(),
        global_submitted_gems(team::ally), me.consumables()[consumable_type::altar_lock].count(),
        lock_hits(team::ally), lock_hits(team::enemy), channel,
        channel ? me.remaining_channel_time() : -1, me.can_attack(), me.can_move(), me.can_cast(),
        a[0].cooldown(), a[1].cooldown(), me.buffs().has_enhanced_attack(),
        static_cast<unsigned long long>(fuel_remaining()));
}
bool farm(vec2 camp_at, vec2 stand) {
    auto& me = self();
    if (!me.visible_tiles().is_set(camp_at)) { me.move_towards(stand); return false; }
    if (vision::has_camp(camp_at)) {
        const auto camp = vision::camp_at(camp_at);
        if (camp.is_alive()) {
            if (std::max(std::abs(me.position().x-camp_at.x), std::abs(me.position().y-camp_at.y)) <= me.attack_range()) {
                if (me.can_attack()) me.attack(camp);
            } else if (me.can_move()) me.move_towards(stand);
            if (camp.is_alive()) return false;
        }
    }
    if (me.position() != camp_at && me.can_move()) me.move_towards(camp_at);
    return true;
}
bool prepare() {
    static int stage = 0;
    auto& me = self();
    if (stage == 0) {
        if (me.gold_count() < 20) { farm(own({4,2}), own({3,2})); return false; }
        log("earned-gold"); stage = 1;
    }
    if (stage == 1) {
        if (!map::spawn_region(team::ally).is_set(me.position())) {
            if (me.can_move()) me.move_towards(own({2,2}));
            return false;
        }
        if (me.consumables()[consumable_type::altar_lock].count() == 0) {
            if (!me.can_buy(consumable_type::altar_lock)) return false;
            me.buy(consumable_type::altar_lock);
        }
        log("bought-lock"); stage = 2;
    }
    if (stage == 2 && banker()) {
        if (me.gem_count() == 0) { farm(own({4,6}), own({3,6})); return false; }
        if (me.gem_count() > 10) { std::printf("LP refusal=bag_not_one_tick\n"); return false; }
        log("earned-gems"); stage = 3;
    } else if (stage == 2) stage = 3;
    const auto destination = frame(defender() ? vec2{13,15} : vec2{16,15});
    if (me.position() != destination) {
        if (me.can_move()) me.move_towards(destination);
        return false;
    }
    if (stage == 3) { log("ready"); stage = 4; }
    return stage == 4;
}

void turn() {
    static mb::Brain brain;
    static bool reset_used = false, damage_used = false;
    auto& me = self();
    if (!me.is_alive()) return;
    const int r = static_cast<int>(round_number());
    if (!defender() && !banker()) {
        const auto p = own(me.champion_name() == champion_type::tavi || me.champion_name() == champion_type::willow ? vec2{0,0} : vec2{0,1});
        if (me.position() != p && me.can_move()) me.move_towards(p);
        return;
    }
    // Warm the actual production state at the first legal gameplay owner turn. No flags,
    // target, beliefs, stats, cooldowns or shared bytes are assigned by the fixture.
    if (defender() && r == 1) { brain.turn(); log("warm-v41"); return; }
    if (!prepare()) {
        if (r >= counterlock_epoch-2) log("refused-not-ready");
        return;
    }
    const auto prep = frame({13,14});
    if (defender() && r == counterlock_epoch-2) {
        if (me.position() != frame({13,15}) || std::abs(me.strength()-45.0f) > 0.01f
            || !vision::has_camp(prep)) { log("refused-reset-setup"); return; }
        const auto camp = vision::camp_at(prep);
        me.attack(camp);
        if (!me.can_attack() && me.abilities()[0].use() && me.can_attack()) {
            me.attack(camp); reset_used = true;
        }
        log("legal-reset-burn"); return;
    }
    if (defender() && r == counterlock_epoch-1) {
        if (!reset_used || !vision::has_camp(prep)) { log("refused-damage-setup"); return; }
        const auto camp = vision::camp_at(prep);
        damage_used = me.abilities()[1].use(camp);
        log("legal-damage-burn"); return;
    }
    if (banker() && r == counterlock_epoch-1) {
        if constexpr (counterlock_probe_mode == 1 || counterlock_prelock) {
            if (!me.consumables()[consumable_type::altar_lock].use(altar())) {
                log("refused-prelock"); return;
            }
            log("prelock");
        } else log("no-prelock");
        return;
    }
    if (r == counterlock_epoch) {
        if (banker()) {
            log("before-tribute"); me.submit_gems(altar()); log("after-tribute"); return;
        }
        const auto spells = me.abilities();
        if (!reset_used || !damage_used || spells[0].cooldown() <= 0 || spells[1].cooldown() <= 0
            || me.buffs().has_enhanced_attack() || me.gem_count() != 0) {
            log("refused-caller-prerequisites"); return;
        }
        bool ready_enemy = false;
        for (const auto& other : vision::champions(team::enemy))
            if (other.champion_name() == champion_type::ryth && other.is_submitting_gems()
                && other.remaining_channel_time() == 0 && other.gem_count() > 0) ready_enemy = true;
        if (!ready_enemy) { log("refused-pending-enemy-channel"); return; }
        log("before-defender");
        if constexpr (counterlock_probe_mode == 1)
            me.consumables()[consumable_type::altar_lock].use(altar());
        else brain.turn();
        log("after-defender"); return;
    }
    if (r > counterlock_epoch && r <= counterlock_epoch+9) {
        log("later-before");
        if constexpr (counterlock_probe_mode == 1) {
            if (banker() && me.gem_count() > 0 && !me.is_submitting_gems()) {
                me.submit_gems(altar()); log("submit-or-hit");
            }
        }
        log("later-after");
    }
}
} // namespace counterprobe

int main() {
    while (true) {
        try {
            if (cambc::is_draft()) counterprobe::draft_turn(); else counterprobe::turn();
        } catch (const std::exception& error) {
            std::printf("counterlock probe error r%u: %s\n", cambc::is_draft()?0u:cambc::round_number(), error.what());
        }
        cambc::end_turn();
    }
}
