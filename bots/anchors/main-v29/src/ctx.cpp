#include "ctx.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <variant>
#include "combat.h"
#include "config.h"
#include "world.h"

namespace mb {

namespace {

template <class... F> struct overloaded : F... { using F::operator()...; };
template <class... F> overloaded(F...) -> overloaded<F...>;

SlotKit decode(const cambc::ability& a)
{
    SlotKit s;
    const auto& info = a.info();
    if (!info) return s;
    auto common = [&](const cambc::ability_properties& p) {
        s.cooldown = p.max_cooldown;
        s.exclusive = p.exclusive_with_movement;
        s.camps = p.valid_targets.contains(cambc::ability_target_mask::camps);
        s.enemies = p.valid_targets.contains(cambc::ability_target_mask::enemies);
        s.allies = p.valid_targets.contains(cambc::ability_target_mask::allies);
        s.self = p.valid_targets.contains(cambc::ability_target_mask::self);
        s.ground = p.valid_targets.contains(cambc::ability_target_mask::ground);
    };
    std::visit(overloaded{
        [&](const cambc::ability_attack_reset& v) { common(v); s.kind = model::AbilityKind::attack_reset; s.enhanced = v.enhanced_attack; },
        [&](const cambc::ability_damage& v) { common(v); s.kind = model::AbilityKind::damage; s.range = v.range; s.modifier = v.modifier; },
        [&](const cambc::ability_aoe_damage& v) { common(v); s.kind = model::AbilityKind::aoe; s.range = v.range; s.modifier = v.modifier; s.radius = v.radius; s.duration = v.duration; },
        [&](const cambc::ability_cc& v) { common(v); s.kind = model::AbilityKind::cc; s.range = v.range; s.modifier = v.modifier; s.duration = v.duration; },
        [&](const cambc::ability_heal& v) { common(v); s.kind = model::AbilityKind::heal; s.range = v.range; s.modifier = v.modifier; },
        [&](const cambc::ability_dash& v) { common(v); s.kind = model::AbilityKind::dash; s.range = v.range; s.modifier = v.modifier; s.radius = v.radius; s.duration = v.duration; },
        [&](const cambc::ability_shield& v) { common(v); s.kind = model::AbilityKind::shield; s.range = v.range; s.modifier = v.modifier; s.duration = v.duration; },
    }, *info);
    return s;
}

model::ChampionSummary summarise(const cambc::champion& c, float strength, float focus, float health)
{
    model::ChampionSummary out;
    out.strength = strength;
    out.focus = focus;
    out.health = health;
    const auto stats = c.base_stats();
    out.attack_range = stats.base_attack_range;
    const auto abilities = c.abilities();
    for (int i = 0; i < 2; ++i)
    {
        const auto s = decode(abilities[static_cast<std::size_t>(i)]);
        out.ability[i] = { s.kind, s.modifier, s.cooldown, s.camps, s.enemies, s.range };
    }
    const auto passive = c.passive();
    out.splash = passive == cambc::passive_type::splash_attack;
    out.kill_resets = passive == cambc::passive_type::reset_cooldowns_on_kill;
    return out;
}

Kit build_kit()
{
    Kit k;
    auto& me = cambc::self();
    k.type = me.champion_name();
    const auto stats = me.base_stats();
    k.attack_range = stats.base_attack_range;
    k.ranged = k.attack_range > 1;
    k.vision = stats.vision_radius;
    k.strength_growth = stats.strength_growth;
    k.focus_growth = stats.focus_growth;
    k.health_growth = stats.health_growth;
    const auto abilities = me.abilities();
    for (int i = 0; i < 2; ++i) k.slot[i] = decode(abilities[static_cast<std::size_t>(i)]);
    const auto passive = me.passive();
    k.splash = passive == cambc::passive_type::splash_attack;
    k.kill_resets = passive == cambc::passive_type::reset_cooldowns_on_kill;
    k.remote_shop = passive == cambc::passive_type::remote_shop;
    k.stun_immune = passive == cambc::passive_type::stun_immunity;
    k.bonus_gold = passive == cambc::passive_type::bonus_gold;
    k.gem_focus = passive == cambc::passive_type::gem_focus;
    k.summary = summarise(me, stats.base_strength, stats.base_focus, stats.base_health);
    return k;
}

} // namespace

const Roster& roster(cambc::team side)
{
    static const std::array<Roster, 2> rosters = [] {
        std::array<Roster, 2> out;
        for (int s = 0; s < 2; ++s)
        {
            out[static_cast<std::size_t>(s)].champs = cambc::champions(s == 0 ? cambc::team::ally : cambc::team::enemy);
            for (const auto& c : out[static_cast<std::size_t>(s)].champs) out[static_cast<std::size_t>(s)].types.push_back(c.champion_name());
        }
        return out;
    }();
    return rosters[side == cambc::team::ally ? 0 : 1];
}

const Kit& kit()
{
    static const Kit k = build_kit();
    return k;
}

model::ChampionSummary design_summary(cambc::champion_type type)
{
    const auto& design = cambc::champion_designs[static_cast<std::size_t>(type)];
    const auto stats = design.base_stats();
    return summarise(design, stats.base_strength, stats.base_focus, stats.base_health);
}

void Ctx::refresh_self()
{
    auto& me = cambc::self();
    pos = me.position();
    hp = me.health();
    max_hp = me.max_health();
    state = me.state();
}

void Ctx::observe()
{
    auto& me = cambc::self();
    auto& world = World::get();
    round = static_cast<int>(cambc::round_number());
    refresh_self();
    strength = me.strength();
    focus = me.focus();
    gems = me.gem_count();
    gold = me.gold_count();
    on_spawn = cambc::map::spawn_region(cambc::team::ally).is_set(pos);
    route_pos = pos;
    if (world.group_at(pos) >= 0)
        for (auto d : kDirs)
        {
            const vec2 n = pos + d;
            if (world.group_at(n) < 0 && n.x >= 0 && n.y >= 0 && n.x < world.width && n.y < world.height && !world.blocked.is_set(n))
            {
                route_pos = n;
                break;
            }
        }
    visible = cambc::vision::visible_tiles();

    // Living camps from two occupancy masks; camps stand on their spawn tiles, so the
    // static index yields the SDK camp object without per-camp lookups. Health is read only
    // for camps close enough to matter this turn; others carry their type's maximum.
    camps.clear();
    camps.reserve(48);
    const cambc::bitboard awake = cambc::vision::awake_camp_tiles();
    const cambc::bitboard asleep = cambc::vision::asleep_camp_tiles();
    for (int pass = 0; pass < 2; ++pass)
    {
        for (auto p : (pass == 0 ? awake : asleep).positions())
        {
            const int g = world.group_at(p), m = world.member_at(p);
            if (g < 0 || m < 0)
            {
                MB_LOG("camp off its spawn tile at %d,%d\n", p.x, p.y);
                continue;
            }
            CampView v;
            v.camp = &world.camp(g, m);
            v.pos = p;
            const auto type = world.groups[static_cast<std::size_t>(g)].types[static_cast<std::size_t>(m)];
            v.strength = world.camp_stats[type].strength;
            v.range = world.camp_stats[type].range;
            // Sleeping harmless clusters die in a hit or two: their maximum is a fine estimate.
            const bool exact = pass == 0 || (v.strength > 0.0f && cheb(p, pos) <= 6);
            v.hp = exact ? v.camp->health() : world.camp_stats[type].health;
            v.awake = pass == 0;
            v.group = g;
            v.member = m;
            camps.push_back(v);
        }
    }

    enemies.clear();
    allies.clear();
    const auto me_handle = static_cast<cambc::handle_champion_t>(me);
    for (const auto side : { cambc::team::enemy, cambc::team::ally })
    {
        const auto& r = roster(side);
        for (std::size_t i = 0; i < r.champs.size(); ++i)
        {
            const auto& c = r.champs[i];
            if (static_cast<cambc::handle_champion_t>(c) == me_handle || !c.is_visible()) continue;
            ChampView v;
            v.champ = &c;
            v.pos = c.position();
            v.hp = c.health();
            if (v.hp <= 0.0f) continue;
            v.max_hp = c.max_health();
            v.type = r.types[i];
            v.index = static_cast<int>(i);
            if (cfg::live_cooldowns || cfg::dash_aware_reach)
                try
                {
                    const auto abilities = c.abilities();
                    for (int k = 0; k < 2; ++k) v.cooldown[k] = std::max(0, static_cast<int>(abilities[static_cast<std::size_t>(k)].cooldown()));
                }
                catch (const std::exception&)
                {
                    v.cooldown[0] = v.cooldown[1] = -1;
                }
            if (side == cambc::team::enemy)
            {
                v.strength = c.strength();
                v.focus = c.focus();
                v.range = c.attack_range();
                v.gems = c.gem_count();
                const auto s = c.state();
                v.channeling = s.contains(cambc::champion_state::recalling) || s.contains(cambc::champion_state::submitting_gems) ||
                               s.contains(cambc::champion_state::teleporting);
                v.dps = pvp_dps_of(v.type, v.strength, v.focus);
                enemies.push_back(v);
            }
            else
                allies.push_back(v);
        }
    }

    camp_damage_taken_mult = damage_taken_multiplier(false);
    kit();  // decode once
}

namespace {
float g_recall_scale = 1.0f;  // Home emblem: recall takes this share of its turn ends
} // namespace

int recall_turns()
{
    const float t = static_cast<float>(cambc::constants::recall_time) * g_recall_scale;
    return std::max(1, static_cast<int>(std::ceil(t - 1e-4f)));
}

float damage_taken_multiplier(bool refresh)
{
    static float cached = -1.0f;
    if (cached >= 0.0f && !refresh) return cached;
    auto& me = cambc::self();
    float camp_damage_taken_mult = 1.0f;
    // Emblem passives of one family do not stack (GameRules "Extra item rules").
    const auto items = me.item_count();
    bool jungle = false, durability = false;
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(cambc::item_type::count); ++i)
    {
        const cambc::item_type item{ i };
        if (items[item] == 0) continue;
        const auto passive = item.passive();
        if (!passive) continue;
        if (*passive == cambc::item_passive::home) g_recall_scale = 1.0f - item.passive_magnitude();
        if ((*passive == cambc::item_passive::jungle && !jungle) || (*passive == cambc::item_passive::durability && !durability))
        {
            (*passive == cambc::item_passive::jungle ? jungle : durability) = true;
            camp_damage_taken_mult *= 1.0f - item.passive_magnitude();
        }
    }
    cached = camp_damage_taken_mult;
    return cached;
}

} // namespace mb
