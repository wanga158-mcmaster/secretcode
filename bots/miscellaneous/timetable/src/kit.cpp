#include "kit.h"
#include <variant>

namespace tt {

namespace {

template <class... F> struct overloaded : F... { using F::operator()...; };
template <class... F> overloaded(F...) -> overloaded<F...>;

SlotKit decode(const cambc::ability& a)
{
    SlotKit s;
    const auto& info = a.info();
    if (!info) return s;
    auto common = [&](const cambc::ability_properties& p) {
        s.cooldown = p.max_cooldown > 0 ? p.max_cooldown : 8;
        s.exclusive = p.exclusive_with_movement;
        s.camps = p.valid_targets.contains(cambc::ability_target_mask::camps);
        s.enemies = p.valid_targets.contains(cambc::ability_target_mask::enemies);
    };
    std::visit(overloaded{
        [&](const cambc::ability_attack_reset& v) { common(v); s.kind = Slot::reset; s.enhanced = v.enhanced_attack; },
        [&](const cambc::ability_damage& v) { common(v); s.kind = Slot::damage; s.range = v.range; s.modifier = v.modifier; },
        [&](const cambc::ability_aoe_damage& v) { common(v); s.kind = Slot::aoe; s.range = v.range; s.modifier = v.modifier; s.radius = v.radius; },
        [&](const cambc::ability_cc& v) { common(v); s.kind = Slot::cc; s.range = v.range; s.modifier = v.modifier; },
        [&](const cambc::ability_heal& v) { common(v); s.kind = Slot::heal; s.range = v.range; s.modifier = v.modifier; },
        [&](const cambc::ability_dash& v) { common(v); s.kind = Slot::dash; s.range = v.range; s.modifier = v.modifier; s.radius = v.radius; },
        [&](const cambc::ability_shield& v) { common(v); s.kind = Slot::shield; s.range = v.range; s.modifier = v.modifier; },
    }, *info);
    return s;
}

Kit build(const cambc::champion& c)
{
    Kit k;
    k.type = c.champion_name();
    const auto st = c.base_stats();
    k.attack_range = st.base_attack_range;
    k.melee = k.attack_range <= 1;
    k.base_hp = st.base_health;
    k.base_str = st.base_strength;
    k.base_focus = st.base_focus;
    k.hp_growth = st.health_growth;
    k.str_growth = st.strength_growth;
    k.focus_growth = st.focus_growth;
    const auto ab = c.abilities();
    k.slot[0] = decode(ab[0]);
    k.slot[1] = decode(ab[1]);
    k.passive = c.passive();
    return k;
}

} // namespace

Kit kit_of(const cambc::champion& c) { return build(c); }

Kit kit_of(cambc::champion_type type)
{
    return build(cambc::champion_designs[static_cast<std::size_t>(type)]);
}

float camp_dps(const Kit& k, float strength, float focus, int neighbours)
{
    float d = strength;
    const int extra = neighbours > 2 ? 2 : neighbours;
    if (k.passive == cambc::passive_type::splash_attack) d += strength * extra;
    for (const auto& s : k.slot) {
        const float per = 1.0f / static_cast<float>(s.cooldown > 0 ? s.cooldown : 8);
        switch (s.kind) {
        case Slot::reset: d += strength * per; break;
        case Slot::damage:
        case Slot::cc:
            if (s.camps) d += s.modifier * focus * per;
            break;
        case Slot::aoe: d += s.modifier * focus * per * (1 + extra); break;
        case Slot::dash: d += s.modifier * focus * per * (1 + extra); break;
        default: break;
        }
    }
    // Kills of camp monsters reset every cooldown: more resets in multi-member groups.
    if (k.passive == cambc::passive_type::reset_cooldowns_on_kill) d += strength * 0.2f;
    return d;
}

float pvp_dps(const Kit& k, float strength, float focus)
{
    float d = strength;
    for (const auto& s : k.slot) {
        const float per = 1.0f / static_cast<float>(s.cooldown > 0 ? s.cooldown : 8);
        switch (s.kind) {
        case Slot::reset: d += strength * per; break;
        case Slot::damage:
        case Slot::cc:
        case Slot::aoe:
            if (s.enemies || s.kind == Slot::aoe) d += s.modifier * focus * per;
            break;
        case Slot::dash: d += s.modifier * focus * per; break;
        default: break;
        }
    }
    return d;
}

BodySpec body_spec(const Kit& k, float hp, float strength, float focus)
{
    BodySpec b;
    b.type = static_cast<int>(k.type);
    b.hp = hp;
    b.dps = camp_dps(k, strength, focus, 1);
    b.range = k.attack_range;
    b.gold_mult = k.passive == cambc::passive_type::bonus_gold ? 1.33f : 1.0f;
    for (const auto& s : k.slot)
        if (s.kind == Slot::shield) b.shield += s.modifier * focus / static_cast<float>(s.cooldown > 0 ? s.cooldown : 8);
    b.dash = k.has_dash();
    b.fragile = k.type == cambc::champion_type::cassi || k.type == cambc::champion_type::issac;
    return b;
}

} // namespace tt
