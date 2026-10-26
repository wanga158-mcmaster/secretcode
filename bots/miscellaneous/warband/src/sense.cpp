#include "sense.h"
#include <variant>

namespace wb {

bool Slot::stuns() const {
    using K = cambc::ability_kind;
    if (kind == static_cast<int>(K::cc)) return t_enemies;
    if (kind == static_cast<int>(K::aoe_damage) || kind == static_cast<int>(K::dash)) return duration > 0;
    if (kind == static_cast<int>(K::attack_reset)) return enhanced;
    return false;
}

namespace {

Slot decode_slot(const cambc::ability& a) {
    Slot s;
    const auto& info = a.info();
    if (!info) return s;
    std::visit([&](const auto& v) {
        s.kind = static_cast<int>(v.kind);
        s.excl_move = v.exclusive_with_movement;
        s.t_enemies = v.valid_targets.contains(cambc::ability_target_mask::enemies);
        s.t_camps = v.valid_targets.contains(cambc::ability_target_mask::camps);
        s.t_ground = v.valid_targets.contains(cambc::ability_target_mask::ground);
        s.t_allies = v.valid_targets.contains(cambc::ability_target_mask::allies);
        s.t_self = v.valid_targets.contains(cambc::ability_target_mask::self);
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, cambc::ability_attack_reset>) s.enhanced = v.enhanced_attack;
        if constexpr (requires { v.range; }) s.range = v.range;
        if constexpr (requires { v.modifier; }) s.modifier = v.modifier;
        if constexpr (requires { v.radius; }) s.radius = v.radius;
        if constexpr (requires { v.duration; }) s.duration = v.duration;
    }, *info);
    return s;
}

std::array<Kit, 13> build_kits() {
    std::array<Kit, 13> kits{};
    for (std::size_t i = 0; i < kits.size(); ++i) {
        Kit& k = kits[i];
        const auto& d = cambc::champion_designs[i];
        k.type = static_cast<cambc::champion_type>(i);
        const auto stats = d.base_stats();
        k.range = stats.base_attack_range;
        k.melee = k.range <= 1;
        const auto abilities = d.abilities();
        for (int s = 0; s < 2; ++s) {
            k.slots[static_cast<std::size_t>(s)] = decode_slot(abilities[static_cast<std::size_t>(s)]);
            const Slot& sl = k.slots[static_cast<std::size_t>(s)];
            using K = cambc::ability_kind;
            if (sl.stuns() && k.stun_slot < 0) k.stun_slot = s;
            else if (sl.kind == static_cast<int>(K::damage) && k.nuke_slot < 0) k.nuke_slot = s;
            else if (sl.kind == static_cast<int>(K::aoe_damage) && k.aoe_slot < 0) k.aoe_slot = s;
            if (sl.kind == static_cast<int>(K::attack_reset) && k.reset_slot < 0) k.reset_slot = s;
            if (sl.kind == static_cast<int>(K::dash) && k.dash_slot < 0) k.dash_slot = s;
            if (sl.kind == static_cast<int>(K::shield) && k.shield_slot < 0) k.shield_slot = s;
            if (sl.kind == static_cast<int>(K::heal) && k.heal_slot < 0) k.heal_slot = s;
        }
        k.stun_immune = d.passive() == cambc::passive_type::stun_immunity;
        k.tank = k.melee && stats.base_health >= 900.0f;
    }
    return kits;
}

} // namespace

float Kit::dps(float str, float focus) const {
    using K = cambc::ability_kind;
    float rate = str;                                  // one basic attack per round
    for (const auto& s : slots) {
        if (s.kind == static_cast<int>(K::attack_reset)) rate += str / 8.0f;
        else if (s.kind == static_cast<int>(K::damage) || s.kind == static_cast<int>(K::cc)
                 || s.kind == static_cast<int>(K::aoe_damage) || s.kind == static_cast<int>(K::dash))
            rate += s.modifier * focus / 8.0f;
    }
    if (!melee) rate *= 0.85f;                         // ranged lose turns to the move-or-attack rule
    return rate;
}

float Kit::burst(float focus, int cd0, int cd1) const {
    using K = cambc::ability_kind;
    float best = 0;
    for (int i = 0; i < 2; ++i) {
        const auto& s = slots[static_cast<std::size_t>(i)];
        const int cd = i == 0 ? cd0 : cd1;
        if (cd > 0) continue;
        if (s.kind == static_cast<int>(K::damage) || s.kind == static_cast<int>(K::cc)
            || s.kind == static_cast<int>(K::aoe_damage) || s.kind == static_cast<int>(K::dash))
            best = std::max(best, s.modifier * focus);
    }
    return best;  // one cast per turn
}

const Kit& kit_of(cambc::champion_type t) {
    static const std::array<Kit, 13> kits = build_kits();
    const auto i = static_cast<std::size_t>(t);
    return kits[i < kits.size() ? i : 0];
}

Unit read_unit(const cambc::champion& c, int index, bool detailed) {
    Unit u{c};
    u.index = index;
    u.pos = c.position();
    u.hp = c.health();
    u.maxhp = c.max_health();
    u.str = c.strength();
    u.focus = c.focus();
    u.range = c.attack_range();
    u.type = c.champion_name();
    u.kit = &kit_of(u.type);
    const auto st = c.state();
    u.recalling = st.contains(cambc::champion_state::recalling);
    u.tributing = st.contains(cambc::champion_state::submitting_gems);
    if (detailed) {
        const auto b = c.buffs();
        if (b.has_stun()) u.stun_left = b.get<cambc::buff_stun>().duration;
        if (b.has_shield()) u.shield = b.get<cambc::buff_shield>().value;
        u.gems = static_cast<int>(c.gem_count());
        const auto ab = c.abilities();
        for (int s = 0; s < 2; ++s) {
            try { u.cd[s] = ab[static_cast<std::size_t>(s)].cooldown(); } catch (const cambc::error&) { u.cd[s] = 0; }
        }
    }
    return u;
}

} // namespace wb
