#include "action.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include "catalog.h"
#include "moves.h"
#include "rules.h"

namespace zero {
namespace {

struct Summary
{
    int count{};
    int nearest{ 1 << 20 };
    float range{}, power{};
    bool supported{ true };
    void add(Tile at, Tile self)
    {
        ++count;
        nearest = std::min(nearest, cheb(at, self));
    }
};

bool consumable(int item)
{
    const auto kind = rules::item_info(item).kind;
    return kind == rules::ik_lock || kind == rules::ik_crystal;
}

bool utility(ActionType type)
{
    return type >= a_buy_consumable && type < a_count;
}

bool in_range(Tile from, Tile at, float range)
{
    return static_cast<float>(cheb(from, at)) <= range;
}

bool enemy_lock(const Obs& o, std::size_t altar)
{
    return altar < o.altar_lock.size() && o.altar_lock[altar] == lk_theirs &&
        (altar >= o.altar_lock_hits.size() || o.altar_lock_hits[altar] != 0);
}

float visible_damage_potential(const Obs& o, const Ability& ability)
{
    const float reach = ability.range + std::max(0.0f, ability.radius);
    const float power = std::max(0.0f, ability.modifier * o.self.focus);
    float damage = 0.0f;
    if (ability.targets & tg_enemies)
        for (const auto& enemy : o.enemies)
            if (enemy.hp > 0.0f && in_range(o.self.at, enemy.at, reach))
                damage += std::min(power, enemy.hp + enemy.shield);
    if (ability.targets & tg_camps)
    {
        const float camp_power = power * rules::camp_damage_dealt_multiplier(o.self.type, o.items);
        for (const auto& camp : o.camps)
            if (camp.hp > 0.0f && in_range(o.self.at, camp.at, reach)) damage += std::min(camp_power, camp.hp);
    }
    return damage;
}

bool dash_witness(const Context& ctx, int range)
{
    const auto& o = *ctx.obs;
    // A nearby tile can require a longer path around walls; a larger-range BFS list cannot simply
    // be filtered by Chebyshev distance to recover a shorter slot's actual destination set.
    if (!o.dash_destinations_known || o.active_dash_range != range)
        return has_dash_destination(*ctx.map, o, range);
    for (Tile tile : o.dash_ok)
        if (tile != o.self.at && in_range(o.self.at, tile, static_cast<float>(range))) return true;
    return false;
}

Summary summarise(const Context& ctx, ActionType type, bool include_features = true)
{
    Summary s;
    const auto& o = *ctx.obs;
    const auto& cat = catalog();
    const auto& d = cat.champion(o.self.type);
    if (type == a_priority)
    {
        s.supported = (o.utility_known_mask & action_bit(type)) != 0;
        if (s.supported && o.can_priority) s.add(o.self.at, o.self.at);
        return s;
    }
    if (utility(type))
    {
        s.supported = (o.utility_known_mask & action_bit(type)) != 0;
        if (s.supported)
            for (const auto& u : o.utility_actions)
                if (u.type == static_cast<std::uint8_t>(type)) s.add(u.at, o.self.at);
        return s;
    }
    switch (type)
    {
    case a_end: s.add(o.self.at, o.self.at); break;
    case a_move:
        s.range = 1.0f;
        if (o.alive && o.can_move)
            for (Tile t : o.step_ok) if (t != o.self.at) s.add(t, o.self.at);
        break;
    case a_attack:
        s.range = d.attack_range;
        s.power = o.self.strength;
        if (o.alive && o.can_attack)
        {
            for (const auto& cp : o.camps)
                if (cp.hp > 0.0f && in_range(o.self.at, cp.at, d.attack_range)) s.add(cp.at, o.self.at);
            for (const auto& e : o.enemies)
                if (e.hp > 0.0f && in_range(o.self.at, e.at, d.attack_range)) s.add(e.at, o.self.at);
            for (std::size_t altar = 0; altar < ctx.map->altars.size(); ++altar)
                if (enemy_lock(o, altar) && in_range(o.self.at, ctx.map->altars[altar], d.attack_range))
                    s.add(ctx.map->altars[altar], o.self.at);
        }
        break;
    case a_cast0:
    case a_cast1: {
        const int slot = type == a_cast0 ? 0 : 1;
        const auto& a = d.ability[slot];
        s.range = a.range;
        s.power = a.modifier * o.self.focus;
        if (!o.alive || !o.can_cast || o.self.cooldown[slot] != 0 ||
            (o.command_facts && !o.can_cast_slot[slot])) break;
        if (include_features && (a.kind == ab_aoe || a.kind == ab_damage || a.kind == ab_cc))
            s.power = visible_damage_potential(o, a);
        switch (a.kind)
        {
        case ab_reset:
        case ab_shield: s.add(o.self.at, o.self.at); break;
        case ab_heal:
            if (a.targets & tg_self) s.add(o.self.at, o.self.at);
            if (a.targets & tg_allies)
                for (const auto& al : o.allies)
                    if (al.hp > 0.0f && in_range(o.self.at, al.at, a.range)) s.add(al.at, o.self.at);
            break;
        case ab_damage:
        case ab_cc:
        case ab_aoe:
            if (a.kind == ab_aoe || (a.targets & tg_ground))
            {
                const int r = static_cast<int>(std::floor(a.range));
                const int x0 = std::max(0, static_cast<int>(o.self.at.x) - r);
                const int x1 = std::min(ctx.map->w - 1, static_cast<int>(o.self.at.x) + r);
                const int y0 = std::max(0, static_cast<int>(o.self.at.y) - r);
                const int y1 = std::min(ctx.map->h - 1, static_cast<int>(o.self.at.y) + r);
                if (x1 >= x0 && y1 >= y0) s.count = (x1 - x0 + 1) * (y1 - y0 + 1);
                s.nearest = 0;
                break;
            }
            // These are the entity-centred arguments represented by the existing target heads. Other
            // legal ground centres remain an explicit representation gap in exact-action coverage.
            if (a.targets & tg_enemies)
                for (const auto& e : o.enemies)
                    if (e.hp > 0.0f && in_range(o.self.at, e.at, a.range)) s.add(e.at, o.self.at);
            if (a.targets & tg_camps)
                for (const auto& cp : o.camps)
                    if (cp.hp > 0.0f && in_range(o.self.at, cp.at, a.range)) s.add(cp.at, o.self.at);
            break;
        case ab_dash:
            // Root facts describe availability, identically for a cached/full or deferred observation.
            // Enumeration belongs exclusively to the selected spatial argument builder.
            if (dash_witness(ctx, static_cast<int>(std::floor(a.range)))) s.add(o.self.at, o.self.at);
            break;
        default: break;
        }
        break;
    }
    case a_buy:
        s.supported = o.command_facts;
        if (s.supported && o.can_buy)
            for (int id = 0; id < cat.n_items && id < kMaxItems; ++id)
            {
                const auto& it = cat.item(id);
                if (it.purchasable && o.buyable_items[static_cast<std::size_t>(id)] && !consumable(id)) s.add(o.self.at, o.self.at);
            }
        break;
    case a_recall:
        s.supported = o.command_facts;
        if (s.supported && o.can_recall) s.add(o.self.at, o.self.at);
        break;
    case a_tribute:
        s.supported = o.command_facts;
        s.range = static_cast<float>(cat.settings.submit_gems_range);
        if (s.supported && o.can_tribute)
            for (std::size_t i = 0; i < ctx.map->altars.size(); ++i)
                if (in_range(o.self.at, ctx.map->altars[i], s.range) &&
                    !(i < o.altar_lock.size() && o.altar_lock[i] == lk_theirs))
                    s.add(ctx.map->altars[i], o.self.at);
        break;
    case a_cancel:
        s.supported = o.command_facts;
        if (s.supported && o.can_cancel) s.add(o.self.at, o.self.at);
        break;
    case a_replan:
        if (o.alive) s.add(o.self.at, o.self.at);  // internal computation; caller admits its fuel/cap
        break;
    default: s.supported = false; break;
    }
    return s;
}

void aux_base(Cand& c, ActionType type, const Obs& o)
{
    c = Cand{};
    c.kind = static_cast<std::uint8_t>(type);
    c.at = o.self.at;
    c.phi[type] = 1.0f;
}

void erase_noops(CandSet& set, ActionType type)
{
    int n = 0;
    for (int i = 0; i < set.n; ++i)
    {
        const auto& c = set.c[i];
        const bool keep = type == a_move ? c.kind == 0 : c.kind != 0;
        if (keep) set.c[n++] = c;
    }
    set.n = n;
}

void erase_dead_attacks(const Obs& o, CandSet& set)
{
    int n = 0;
    for (int i = 0; i < set.n; ++i)
    {
        const auto& c = set.c[i];
        bool alive = false;
        if (c.kind == 1)
            for (const auto& cp : o.camps) alive |= cp.at == c.at && cp.hp > 0.0f;
        else if (c.kind == 2)
            for (const auto& e : o.enemies) alive |= e.id == c.target && e.hp > 0.0f;
        else if (c.kind == 3 && c.target >= 0)
            alive = enemy_lock(o, static_cast<std::size_t>(c.target));
        if (alive) set.c[n++] = c;
    }
    set.n = n;
}

void append_lock_attacks(const Context& ctx, CandSet& set)
{
    const auto& o = *ctx.obs;
    if (!o.alive || !o.can_attack) return;
    const float range = catalog().champion(o.self.type).attack_range;
    for (std::size_t altar = 0; altar < ctx.map->altars.size() && set.n < kMaxCands; ++altar)
    {
        if (!enemy_lock(o, altar) || !in_range(o.self.at, ctx.map->altars[altar], range)) continue;
        const int hits = altar < o.altar_lock_hits.size() ? o.altar_lock_hits[altar] : -1;
        const bool known = hits >= 0;
        Cand& c = set.c[set.n++];
        c = Cand{};
        c.kind = 3;
        c.target = static_cast<std::int32_t>(altar);
        c.at = ctx.map->altars[altar];
        c.phi[0] = -1.0f;  // distinct from camps/champions; this head has no none candidate in new mode
        c.phi[3] = known ? static_cast<float>(hits) / 20.0f : 0.0f;
        c.phi[4] = known && hits == 1 ? 1.0f : 0.0f;
        c.phi[7] = ctx.mem->goal_kind == g_bank && ctx.mem->goal_target == static_cast<int>(altar) ? 1.0f : 0.0f;
        c.phi[11] = known ? static_cast<float>(hits) / static_cast<float>(std::max(1, catalog().settings.altar_lock_hits)) : 0.0f;
        c.phi[12] = known ? 1.0f : -1.0f;  // missing observations are unknown, never assumed full
    }
}

void purchase_arguments(const Context& ctx, CandSet& out)
{
    const auto& o = *ctx.obs;
    const auto& cat = catalog();
    out.head = h_shop;
    for (int id = 0; id < cat.n_items && id < kMaxItems && out.n < kMaxCands; ++id)
    {
        const auto& d = cat.item(id);
        if (!d.purchasable || !o.buyable_items[static_cast<std::size_t>(id)] || consumable(id)) continue;
        const auto& info = rules::item_info(id);
        const auto gain = rules::buy_gain(o.self.type, o.items, id);
        Cand& c = out.c[out.n++];
        c = Cand{};
        c.kind = 1;
        c.target = id;
        if (info.family != rules::fam_none) c.phi[1 + info.family] = 1.0f;
        c.phi[8] = gain.health / std::max(1.0f, o.self.max_hp);
        c.phi[9] = gain.strength / std::max(1.0f, o.self.strength);
        c.phi[10] = gain.focus / std::max(1.0f, o.self.focus);
        c.phi[11] = gain.completes_emblem ? 1.0f : 0.0f;
        c.phi[12] = gain.completes_emblem && d.result >= 0 ? cat.item(d.result).passive_magnitude : 0.0f;
        c.phi[13] = static_cast<float>(d.cost) / std::max(1.0f, static_cast<float>(o.self.gold));
        c.phi[14] = static_cast<float>(o.items[static_cast<std::size_t>(id)]) / 5.0f;
        c.phi[15] = static_cast<float>(o.self.gold - d.cost) / 300.0f;
    }
}

void cast_arguments(const Context& ctx, int slot, CandSet& out)
{
    const auto& o = *ctx.obs;
    const auto& a = catalog().champion(o.self.type).ability[slot];
    cast_candidates(ctx, slot, out);
    erase_noops(out, slot == 0 ? a_cast0 : a_cast1);
    int n = 0;
    for (int i = 0; i < out.n; ++i)
    {
        const auto& c = out.c[i];
        if (!in_range(o.self.at, c.at, a.range)) continue;
        if ((a.kind == ab_damage || a.kind == ab_cc || a.kind == ab_aoe) &&
            ((c.target < -1 && !(a.targets & tg_camps)) || (c.target >= 0 && !(a.targets & tg_enemies)))) continue;
        if (a.kind == ab_heal && !(a.targets & tg_allies)) continue;
        out.c[n++] = c;
    }
    out.n = n;
    // The legacy target builder suppresses resets before attacking; the SDK has no such legality rule.
    // New mode exposes the command whenever the slot is ready; host rejection remains observable.
    if (a.kind == ab_reset && out.n == 0)
    {
        Cand& c = out.c[out.n++];
        c = Cand{};
        c.kind = ab_reset;
        c.target = o.self.id;
        c.at = o.self.at;
        c.phi[ab_reset] = 1.0f;
        const int range = static_cast<int>(catalog().champion(o.self.type).attack_range);
        bool target = false;
        for (const auto& e : o.enemies) target |= cheb(e.at, o.self.at) <= range;
        for (const auto& cp : o.camps) target |= cheb(cp.at, o.self.at) <= range;
        c.phi[8] = target ? 1.0f : 0.0f;
        c.phi[23] = a.excludes_walking ? 1.0f : 0.0f;
    }
    if (a.kind == ab_heal && (a.targets & tg_self) && out.n < kMaxCands)
    {
        Cand& c = out.c[out.n++];
        c = Cand{};
        c.kind = ab_heal;
        c.target = o.self.id;
        c.at = o.self.at;
        c.phi[ab_heal] = 1.0f;
        c.phi[13] = std::max(0.0f, std::min(a.modifier * o.self.focus, o.self.max_hp - o.self.hp)) / std::max(1.0f, o.self.max_hp);
        c.phi[23] = a.excludes_walking ? 1.0f : 0.0f;
    }
}

}  // namespace

namespace {

struct Column
{
    SpatialColumn key;
    int n{};
    std::int16_t y[kMaxCands]{};
};

bool column_less(SpatialColumn a, SpatialColumn b)
{
    return a.target < b.target || (a.target == b.target && a.x < b.x);
}

bool same_column(SpatialColumn a, SpatialColumn b)
{
    return a.target == b.target && a.x == b.x;
}

ActionBuild columns(std::span<const SpatialTile> legal, Column* out, int& n)
{
    n = 0;
    if (legal.size() > kMaxSpatialTiles) return { ActionBuildStatus::overflow, static_cast<int>(legal.size()) };
    for (const auto& t : legal)
    {
        const SpatialColumn key{ t.target, t.at.x };
        int c = 0;
        while (c < n && column_less(out[c].key, key)) ++c;
        if (c == n || !same_column(out[c].key, key))
        {
            if (n == kMaxCands) return { ActionBuildStatus::overflow, n + 1 };
            for (int j = n; j > c; --j) out[j] = out[j - 1];
            out[c] = Column{};
            out[c].key = key;
            ++n;
        }
        auto& col = out[c];
        int y = 0;
        while (y < col.n && col.y[y] < t.at.y) ++y;
        if (y < col.n && col.y[y] == t.at.y) continue;
        if (col.n == kMaxCands) return { ActionBuildStatus::overflow, col.n + 1 };
        for (int j = col.n; j > y; --j) col.y[j] = col.y[j - 1];
        col.y[y] = t.at.y;
        ++col.n;
    }
    return { n > 0 ? ActionBuildStatus::ok : ActionBuildStatus::unavailable, n };
}

bool spatial_goal_at(const Context& ctx, Tile t)
{
    const auto& m = *ctx.mem;
    if (m.goal_kind == g_farm && m.goal_target >= 0 && ctx.map->inside(t))
        return ctx.map->group_of[static_cast<std::size_t>(ctx.map->idx(t))] == m.goal_target;
    if (m.goal_kind == g_engage)
        for (const auto& e : ctx.obs->enemies) if (e.id == m.goal_target) return e.at == t;
    return false;
}

int spatial_goal_distance(const Context& ctx, Tile t, bool use_paths)
{
    const auto& m = *ctx.mem;
    if (use_paths && ctx.paths)
    {
        if (m.goal_kind == g_farm && m.goal_target >= 0) return ctx.paths->to_group(m.goal_target, t);
        if (m.goal_kind == g_bank && m.goal_target >= 0) return ctx.paths->to_altar(m.goal_target, t);
        if (m.goal_kind == g_hold) return ctx.paths->to_spawn(t);
    }
    if (m.goal_kind == g_farm && m.goal_target >= 0 && m.goal_target < static_cast<int>(ctx.map->groups.size()))
    {
        int d = kUnreachable;
        for (const auto& member : ctx.map->groups[static_cast<std::size_t>(m.goal_target)].members)
            d = std::min(d, cheb(t, member.at));
        return d == kUnreachable ? 0 : d;
    }
    if (m.goal_kind == g_bank && m.goal_target >= 0 && m.goal_target < static_cast<int>(ctx.map->altars.size()))
        return cheb(t, ctx.map->altars[static_cast<std::size_t>(m.goal_target)]);
    if (m.goal_kind == g_hold) return cheb(t, ctx.map->spawn_center[ctx.obs->team]);
    if (m.goal_kind == g_engage)
        for (const auto& e : ctx.obs->enemies) if (e.id == m.goal_target) return cheb(t, e.at);
    if (m.goal_kind == g_assist)
        for (const auto& a : ctx.obs->allies) if (a.id == m.goal_target) return cheb(t, a.at);
    if (m.goal_kind == g_avoid)
    {
        int nearest = 99;
        for (const auto& e : ctx.obs->enemies) nearest = std::min(nearest, cheb(t, e.at));
        return -nearest;
    }
    return 0;
}

struct Threat
{
    Tile at;
    float damage{};
    int reach{};
};

struct SpatialEffects
{
    float camp_mult{}, camp_taken{}, champ_taken{};
    int here_goal{};
    std::vector<Threat> threats, camps;
    explicit SpatialEffects(const Obs& o, bool cast, bool dash)
    {
        if (!cast) return;
        camp_mult = rules::camp_damage_dealt_multiplier(o.self.type, o.items);
        if (!dash) return;
        camp_taken = rules::camp_damage_taken_multiplier(o.items);
        champ_taken = rules::champion_damage_taken_multiplier(o.items);
        threats.reserve(o.enemies.size());
        for (const auto& e : o.enemies)
        {
            if (e.hp <= 0) continue;
            const auto rate = rules::rates(e.type, e.strength, e.focus, e.cooldown, true);
            threats.push_back({ e.at, rate.burst, rate.reach });
        }
        camps.reserve(o.camps.size());
        for (const auto& cp : o.camps)
        {
            if (!cp.awake || cp.hp <= 0) continue;
            const auto& design = catalog().camp(cp.type);
            int reach = static_cast<int>(design.attack_range);
            // At a hypothetical landing we draw this camp exactly when we beat every ally's distance,
            // with actor id breaking ties. This turns the per-landing ally scan into one rule radius.
            for (const auto& ally : o.allies)
            {
                if (ally.hp <= 0) continue;
                const int distance = cheb(cp.at, ally.at);
                reach = std::min(reach, distance - (o.self.id < ally.id ? 0 : 1));
            }
            if (reach >= 0) camps.push_back({cp.at, design.strength, reach});
        }
    }
};

void spatial_effect_facts(const Context& ctx, ActionType type, const Column& col, int first, int last,
                          SpatialStage stage, const SpatialEffects& effects, Cand& c)
{
    const auto& o = *ctx.obs;
    const int slot = type == a_cast0 ? 0 : 1;
    const bool cast = type == a_cast0 || type == a_cast1;
    c = Cand{};
    c.target = col.key.target;
    c.at = { col.key.x, stage == SpatialStage::row ? col.y[first] : o.self.at.y };
    const float weight = 1.0f / static_cast<float>(last - first);
    if (!cast)
    {
        c.kind = static_cast<std::uint8_t>(type);
        c.phi[type] = 1.0f;
        bool seen[kMaxCands]{};
        for (const auto& u : o.utility_actions)
        {
            if (u.type != static_cast<std::uint8_t>(type) || u.target != col.key.target || u.at.x != col.key.x) continue;
            int j = first;
            while (j < last && col.y[j] < u.at.y) ++j;
            if (j == last || col.y[j] != u.at.y || seen[j - first]) continue;
            seen[j - first] = true;
            for (int k = a_count; k < kMaxPhi; ++k) c.phi[k] += u.phi[k] * weight;
        }
    }
    else
    {
        const auto& a = catalog().champion(o.self.type).ability[slot];
        c.kind = a.kind;
        c.phi[a.kind] = 1.0f;
        c.phi[23] = a.excludes_walking ? 1.0f : 0.0f;
        const float power = a.modifier * o.self.focus;
        const int radius = a.kind == ab_aoe || a.kind == ab_dash ? static_cast<int>(a.radius) : 0;
        // Column hit facts integrate exact radius arithmetic over its legal rows; they do not build
        // all leaf candidate vectors. A row stage is the same arithmetic with one legal destination.
        auto hit_fraction = [&](Tile at) {
            if (std::abs(static_cast<int>(at.x) - col.key.x) > radius) return 0.0f;
            const auto low = std::lower_bound(col.y + first, col.y + last, static_cast<int>(at.y) - radius);
            const auto high = std::upper_bound(col.y + first, col.y + last, static_cast<int>(at.y) + radius);
            return static_cast<float>(high - low) * weight;
        };
        bool goal_hit[kMaxCands]{}, channel_hit[kMaxCands]{};
        if (a.kind == ab_dash || (a.targets & tg_enemies))
            for (const auto& e : o.enemies)
            {
                if (e.hp <= 0) continue;
                const float fraction = hit_fraction(e.at);
                if (fraction == 0.0f) continue;
                const float hp = e.hp + e.shield;
                c.phi[8] += std::min(power, hp) / std::max(1.0f, e.max_hp) * fraction;
                if (a.kind != ab_dash)
                {
                    c.phi[9] += hp <= power ? fraction : 0.0f;
                    c.phi[10] += fraction / 4.0f;
                    for (int j = first; j < last; ++j)
                        if (cheb(e.at, {col.key.x, col.y[j]}) <= radius)
                        {
                            goal_hit[j - first] |= spatial_goal_at(ctx, e.at);
                            channel_hit[j - first] |= e.recalling || e.tributing;
                        }
                }
            }
        if (a.kind != ab_dash && (a.targets & tg_camps))
            for (const auto& cp : o.camps)
            {
                if (cp.hp <= 0) continue;
                const float fraction = hit_fraction(cp.at);
                if (fraction == 0.0f) continue;
                const float damage = power * effects.camp_mult;
                c.phi[8] += std::min(damage, cp.hp) / std::max(1.0f, catalog().camp(cp.type).health) * fraction;
                c.phi[9] += cp.hp <= damage ? fraction : 0.0f;
                c.phi[11] += fraction / 4.0f;
                if (spatial_goal_at(ctx, cp.at))
                    for (int j = first; j < last; ++j)
                        goal_hit[j - first] |= cheb(cp.at, {col.key.x, col.y[j]}) <= radius;
            }
        for (int j = 0; j < last - first; ++j)
        {
            c.phi[21] += goal_hit[j] ? weight : 0.0f;
            c.phi[22] += channel_hit[j] ? weight : 0.0f;
        }
        if (a.kind == ab_cc) c.phi[12] = a.duration / 4.0f;
        if (a.kind == ab_dash)
        {
            const bool use_paths = stage == SpatialStage::row;
            float camp_difference[kMaxCands + 1]{}, enemy_difference[kMaxCands + 1]{};
            auto accumulate = [&](const std::vector<Threat>& threats, float* difference) {
                for (const auto& threat : threats)
                {
                    if (std::abs(static_cast<int>(threat.at.x) - col.key.x) > threat.reach) continue;
                    const auto begin = col.y + first, end = col.y + last;
                    const int low = static_cast<int>(std::lower_bound(begin, end, static_cast<int>(threat.at.y) - threat.reach) - begin);
                    const int high = static_cast<int>(std::upper_bound(begin, end, static_cast<int>(threat.at.y) + threat.reach) - begin);
                    difference[low] += threat.damage;
                    difference[high] -= threat.damage;
                }
            };
            accumulate(effects.camps, camp_difference);
            accumulate(effects.threats, enemy_difference);
            float camp_damage = 0.0f, enemy_damage = 0.0f;
            for (int j = first; j < last; ++j)
            {
                const Tile t{ col.key.x, col.y[j] };
                c.phi[15] += std::clamp(static_cast<float>(spatial_goal_distance(ctx, t, use_paths) - effects.here_goal), -3.0f, 3.0f) * weight;
                camp_damage += camp_difference[j - first];
                enemy_damage += enemy_difference[j - first];
                int nearest = 99;
                for (const auto& threat : effects.threats)
                {
                    const int d = cheb(threat.at, t);
                    nearest = std::min(nearest, d);
                }
                c.phi[16] += std::clamp(camp_damage * effects.camp_taken / std::max(1.0f, o.self.hp), 0.0f, 3.0f) * weight;
                c.phi[17] += std::clamp(enemy_damage * effects.champ_taken / std::max(1.0f, o.self.hp), 0.0f, 3.0f) * weight;
                const auto flags = ctx.map->at(t);
                c.phi[18] += (flags & tf_bush) ? weight : 0.0f;
                c.phi[19] += (flags & (o.team == 0 ? tf_spawn_a : tf_spawn_b)) ? weight : 0.0f;
                c.phi[20] += static_cast<float>(std::min(nearest, 20)) / 20.0f * weight;
            }
        }
    }
    c.phi[0] = static_cast<float>(stage);
    c.phi[14] = static_cast<float>(col.key.x - o.self.at.x) / 20.0f;
    c.phi[13] = stage == SpatialStage::row ? static_cast<float>(col.y[first] - o.self.at.y) / 20.0f : 0.0f;
}

}  // namespace

bool spatial_action(const Context& ctx, ActionType type)
{
    if (type == a_ward || type == a_use_consumable) return true;
    if (type != a_cast0 && type != a_cast1) return false;
    const auto& a = catalog().champion(ctx.obs->self.type).ability[type == a_cast0 ? 0 : 1];
    return a.kind == ab_dash || a.kind == ab_aoe || (a.targets & tg_ground);
}

ActionBuild spatial_argument_tiles(const Context& ctx, ActionType type, std::vector<SpatialTile>& out)
{
    out.clear();
    if (!spatial_action(ctx, type)) return { ActionBuildStatus::unsupported, 0 };
    const Summary summary = summarise(ctx, type);
    if (!summary.supported) return { ActionBuildStatus::unsupported, 0 };
    if (summary.count == 0) return { ActionBuildStatus::unavailable, 0 };
    const auto& o = *ctx.obs;
    if (type == a_cast0 || type == a_cast1)
    {
        const auto& a = catalog().champion(o.self.type).ability[type == a_cast0 ? 0 : 1];
        if (a.kind == ab_dash)
        {
            const int range = static_cast<int>(std::floor(a.range));
            std::vector<Tile> generated;
            const std::vector<Tile>* landings = &o.dash_ok;
            if (!o.dash_destinations_known || o.active_dash_range != range)
            {
                generate_dash_destinations(*ctx.map, o, range, generated);
                landings = &generated;
            }
            for (Tile t : *landings)
                if (t != o.self.at && in_range(o.self.at, t, a.range))
                {
                    if (out.size() == kMaxSpatialTiles) { out.clear(); return { ActionBuildStatus::overflow, kMaxSpatialTiles + 1 }; }
                    out.push_back({t, -1});
                }
        }
        else
        {
            if (summary.count > kMaxSpatialTiles) return { ActionBuildStatus::overflow, summary.count };
            const int r = static_cast<int>(std::floor(a.range));
            for (int x = std::max(0, static_cast<int>(o.self.at.x) - r); x <= std::min(ctx.map->w - 1, static_cast<int>(o.self.at.x) + r); ++x)
                for (int y = std::max(0, static_cast<int>(o.self.at.y) - r); y <= std::min(ctx.map->h - 1, static_cast<int>(o.self.at.y) + r); ++y)
                    out.push_back({ { static_cast<std::int16_t>(x), static_cast<std::int16_t>(y) }, -1 });
        }
    }
    else
        for (const auto& u : o.utility_actions)
            if (u.type == static_cast<std::uint8_t>(type))
            {
                if (out.size() == kMaxSpatialTiles) { out.clear(); return { ActionBuildStatus::overflow, kMaxSpatialTiles + 1 }; }
                out.push_back({u.at, u.target});
            }
    return { out.empty() ? ActionBuildStatus::mismatch : ActionBuildStatus::ok, static_cast<int>(out.size()) };
}

ActionBuild spatial_argument_candidates(const Context& ctx, ActionType type, std::span<const SpatialTile> legal,
                                       SpatialStage stage, SpatialColumn selected, CandSet& out)
{
    out.head = type == a_cast0 ? h_cast0 : (type == a_cast1 ? h_cast1 : h_aux);
    out.n = 0;
    if (!spatial_action(ctx, type)) return { ActionBuildStatus::unsupported, 0 };
    if (stage != SpatialStage::column && stage != SpatialStage::row) return { ActionBuildStatus::unsupported, 0 };
    if (legal.size() > kMaxSpatialTiles)
        return { ActionBuildStatus::overflow, static_cast<int>(std::min<std::size_t>(legal.size(), std::numeric_limits<int>::max())) };
    for (const auto& tile : legal)
        if (!ctx.map->inside(tile.at)) return { ActionBuildStatus::mismatch, 0 };
    Column all[kMaxCands];
    int n = 0;
    const auto built = columns(legal, all, n);
    if (!built.usable()) return built;
    const bool cast = type == a_cast0 || type == a_cast1;
    const bool dash = cast && catalog().champion(ctx.obs->self.type).ability[type == a_cast0 ? 0 : 1].kind == ab_dash;
    SpatialEffects effects(*ctx.obs, cast, dash);
    if (dash) effects.here_goal = spatial_goal_distance(ctx, ctx.obs->self.at, stage == SpatialStage::row);
    if (stage == SpatialStage::column)
    {
        out.n = n;
        for (int c = 0; c < n; ++c)
            spatial_effect_facts(ctx, type, all[c], 0, all[c].n, stage, effects, out.c[c]);
    }
    else
    {
        int c = 0;
        while (c < n && !same_column(all[c].key, selected)) ++c;
        if (c == n) return { ActionBuildStatus::unavailable, 0 };
        out.n = all[c].n;
        for (int y = 0; y < out.n; ++y)
            spatial_effect_facts(ctx, type, all[c], y, y + 1, stage, effects, out.c[y]);
    }
    return { ActionBuildStatus::ok, out.n };
}

std::uint32_t action_rule_mask(const Context& ctx, std::uint32_t allowed_mask)
{
    std::uint32_t mask = action_bit(a_end);
    for (int k = 1; k < a_count; ++k)
    {
        const auto type = static_cast<ActionType>(k);
        if (!(allowed_mask & action_bit(type))) continue;
        const auto summary = summarise(ctx, type, false);
        if (summary.supported && summary.count > 0) mask |= action_bit(type);
    }
    return mask;
}

void action_candidates(const Context& ctx, CandSet& out, std::uint32_t allowed_mask)
{
    const auto& o = *ctx.obs;
    out.head = h_action;
    out.n = 0;
    allowed_mask |= action_bit(a_end);
    for (int k = 0; k < a_count; ++k)
    {
        const auto type = static_cast<ActionType>(k);
        if (!(allowed_mask & action_bit(type))) continue;
        const Summary s = summarise(ctx, type);
        if (!s.supported || s.count == 0) continue;
        Cand& c = out.c[out.n++];
        aux_base(c, type, o);
        c.phi[15] = static_cast<float>(s.count) / static_cast<float>(kMaxCands);
        c.phi[16] = o.can_move ? 1.0f : 0.0f;
        c.phi[17] = o.can_attack ? 1.0f : 0.0f;
        c.phi[18] = o.can_cast ? 1.0f : 0.0f;
        const float remaining = static_cast<float>(std::max(0, o.channel_remaining));
        const float channel_progress = remaining / (remaining + 1.0f);
        c.phi[19] = o.teleporting ? 2.0f + channel_progress :
                    (o.self.recalling ? 1.0f + channel_progress :
                    (o.self.tributing ? -1.0f - channel_progress : 0.0f));
        c.phi[20] = static_cast<float>(o.command_index) / 8.0f;
        c.phi[21] = s.range / 20.0f;
        c.phi[22] = s.nearest < (1 << 20) ? static_cast<float>(s.nearest) / 20.0f : 0.0f;
        c.phi[23] = s.power / std::max(1.0f, o.self.max_hp);
        const bool dash = (type == a_cast0 || type == a_cast1) &&
            catalog().champion(o.self.type).ability[type == a_cast0 ? 0 : 1].kind == ab_dash;
        if (type == a_move || type == a_end || dash)
            c.phi[22] = static_cast<float>(spatial_goal_distance(ctx, o.self.at, false)) / 20.0f;
        if (type == a_replan)
        {
            c.phi[21] = ctx.mem->goal_kind >= 0 ? 1.0f : 0.0f;
            c.phi[22] = ctx.mem->goal_since >= 0 ? static_cast<float>(std::max(0, o.round - ctx.mem->goal_since)) / 40.0f : 0.0f;
        }
        if (type == a_priority) c.phi[23] = encode_priority(o.priority);
    }
}

ActionBuild argument_candidates(const Context& ctx, ActionType type, CandSet& out)
{
    out.head = h_aux;
    out.n = 0;
    if (type >= a_count) return { ActionBuildStatus::unsupported, 0 };
    const auto& o = *ctx.obs;
    const Summary s = summarise(ctx, type);
    if (!s.supported) return { ActionBuildStatus::unsupported, 0 };
    if (s.count == 0) return { ActionBuildStatus::unavailable, 0 };
    if (type == a_priority)
    {
        out.head = h_priority;
        out.n = 1;
        out.c[0] = Cand{};
        out.c[0].kind = a_priority;
        out.c[0].phi[0] = 1.0f;
    }
    else if (utility(type))
    {
        for (const auto& u : o.utility_actions)
        {
            if (u.type != static_cast<std::uint8_t>(type) || out.n >= kMaxCands) continue;
            Cand& c = out.c[out.n++];
            aux_base(c, type, o);
            c.target = u.target;
            c.at = u.at;
            for (int j = a_count; j < kMaxPhi; ++j) c.phi[j] = u.phi[j];
        }
    }
    else switch (type)
    {
    case a_move: step_candidates(ctx, out); erase_noops(out, type); break;
    case a_attack:
        attack_candidates(ctx, out);
        erase_noops(out, type);
        erase_dead_attacks(o, out);
        append_lock_attacks(ctx, out);
        break;
    case a_cast0: cast_arguments(ctx, 0, out); break;
    case a_cast1: cast_arguments(ctx, 1, out); break;
    case a_buy: purchase_arguments(ctx, out); break;
    case a_replan:
        goal_candidates(ctx, out);
        return { out.n > 0 ? ActionBuildStatus::ok : ActionBuildStatus::mismatch, out.n };
    case a_tribute:
        for (std::size_t i = 0; i < ctx.map->altars.size() && out.n < kMaxCands; ++i)
        {
            if (!in_range(o.self.at, ctx.map->altars[i], s.range) ||
                (i < o.altar_lock.size() && o.altar_lock[i] == lk_theirs)) continue;
            Cand& c = out.c[out.n++];
            aux_base(c, type, o);
            c.target = static_cast<int>(i);
            c.at = ctx.map->altars[i];
            c.phi[15] = static_cast<float>(rules::tribute_turns(o.self.gems)) / 40.0f;
            c.phi[16] = static_cast<float>(o.self.gems) / 100.0f;
            c.phi[17] = static_cast<float>(cheb(o.self.at, c.at)) / 20.0f;
        }
        break;
    case a_end:
    case a_recall:
    case a_cancel: {
        Cand& c = out.c[out.n++];
        aux_base(c, type, o);
        if (type == a_recall) c.phi[15] = static_cast<float>(rules::recall_turns(o.items)) / 40.0f;
        break;
    }
    default: return { ActionBuildStatus::unsupported, 0 };
    }
    if (out.n == 0) return { ActionBuildStatus::mismatch, s.count };
    return { s.count > out.n ? ActionBuildStatus::truncated : ActionBuildStatus::ok, s.count };
}

float encode_priority(std::int32_t priority)
{
    return static_cast<float>(std::asinh(static_cast<double>(priority)) / 10.0);
}

std::int32_t decode_priority(float scalar)
{
    constexpr auto low = std::numeric_limits<std::int32_t>::min();
    constexpr auto high = std::numeric_limits<std::int32_t>::max();
    if (std::isnan(scalar)) return 0;
    const double exponent = 10.0 * static_cast<double>(scalar);
    if (exponent <= std::asinh(static_cast<double>(low))) return low;
    if (exponent >= std::asinh(static_cast<double>(high))) return high;
    const double value = std::round(std::sinh(exponent));
    if (value <= static_cast<double>(low)) return low;
    if (value >= static_cast<double>(high)) return high;
    return static_cast<std::int32_t>(value);
}

std::int32_t max_affordable_quantity(const Context& ctx, int item_id)
{
    const auto& cat = catalog();
    if (item_id < 0 || item_id >= cat.n_items || item_id >= kMaxItems) return 0;
    const auto& item = cat.item(item_id);
    if (!item.purchasable || item.cost == 0) return 0;
    const auto maximum = static_cast<std::uint64_t>(ctx.obs->self.gold) / item.cost;
    return static_cast<std::int32_t>(std::min<std::uint64_t>(maximum, std::numeric_limits<std::int32_t>::max()));
}

ActionBuild quantity_candidates(const Context& ctx, int item_id, CandSet& out)
{
    out.head = h_quantity;
    out.n = 0;
    const auto& cat = catalog();
    if (item_id < 0 || item_id >= cat.n_items || item_id >= kMaxItems ||
        !cat.item(item_id).purchasable || cat.item(item_id).cost == 0)
        return { ActionBuildStatus::unsupported, 0 };
    const auto& o = *ctx.obs;
    if (!o.command_facts) return { ActionBuildStatus::unsupported, 0 };
    const auto maximum = max_affordable_quantity(ctx, item_id);
    if (!o.can_buy || !o.buyable_items[static_cast<std::size_t>(item_id)] || maximum == 0)
        return { ActionBuildStatus::unavailable, 0 };
    out.n = 1;
    Cand& c = out.c[0];
    c = Cand{};
    c.kind = 1;
    c.target = item_id;
    c.phi[0] = 1.0f;
    c.phi[1] = static_cast<float>(item_id) / static_cast<float>(std::max(1, cat.n_items));
    c.phi[2] = static_cast<float>(cat.item(item_id).cost) / 300.0f;
    c.phi[3] = static_cast<float>(maximum) / 100.0f;
    return { ActionBuildStatus::ok, 1 };
}

ActionBuild quantity_argument_candidates(const Context& ctx, const Cand& item, CandSet& out)
{
    return quantity_candidates(ctx, item.target, out);
}

std::int32_t decode_quantity(float scalar, std::int32_t maximum)
{
    if (maximum <= 0) return 0;
    return std::clamp(decode_priority(scalar), std::int32_t{1}, maximum);
}

const char* action_name(ActionType type)
{
    static constexpr const char* names[a_count] = { "end", "move", "attack", "cast0", "cast1", "buy", "recall", "tribute", "cancel", "replan", "buy_consumable", "use_consumable", "ward", "sell", "priority" };
    return type < a_count ? names[type] : "unsupported";
}

}  // namespace zero
