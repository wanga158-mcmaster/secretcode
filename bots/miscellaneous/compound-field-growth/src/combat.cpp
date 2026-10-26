#include "combat.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <type_traits>
#include "config.h"
#include "nav.h"
#include "world.h"

namespace mb {

namespace {

using model::AbilityKind;

bool guarded(const char* what, const std::function<bool()>& action)
{
    try
    {
        // Retaliation (Ren's passive) can kill us during our own turn; a dead champion's actions throw.
        if (!cambc::self().is_alive()) return false;
        return action();
    }
    catch (const std::exception& e)
    {
        MB_LOG("action %s failed: %s\n", what, e.what());
        return false;
    }
}

// Smith's rule priority: strength per remaining health; harmless monsters go last.
float camp_priority(const CampView& c)
{
    if (c.strength <= 0.0f) return -1.0f + 1.0f / std::max(1.0f, c.hp);
    return c.strength / std::max(1.0f, c.hp);
}

// Re-read a target after we hit it; estimates drift (mitigation, splash, ally hits).
void refresh(CampView& c)
{
    try
    {
        if constexpr (cfg::refresh_dead_camp)
        {
            if (c.camp->state().is_dead())
            {
                c.hp = 0.0f;
                c.awake = false;
            }
            else c.hp = c.camp->health();
        }
        else c.hp = c.camp->state().is_dead() ? 0.0f : c.camp->health();
    }
    catch (...)
    {
        c.hp = 0.0f;
    }
}

void refresh(ChampView& c)
{
    try
    {
        c.hp = c.champ->is_alive() && c.champ->is_visible() ? c.champ->health() : 0.0f;
    }
    catch (...)
    {
        c.hp = 0.0f;
    }
}

float spell_damage(const cambc::ability& a, const SlotKit& s, float focus)
{
    try
    {
        return a.damage_amount();
    }
    catch (...)
    {
        return s.modifier * focus;
    }
}

bool in_attack_range(vec2 from, const std::vector<CampView*>& targets, int range)
{
    for (auto* t : targets)
        if (t->hp > 0.0f && cheb(from, t->pos) <= range) return true;
    return false;
}

bool try_attack_camp(Ctx& ctx, TurnActions& acts, std::vector<CampView*>& targets)
{
    auto& me = cambc::self();
    if (!me.can_attack()) return false;
    CampView* best = nullptr;
    float best_p = -1e9f;
    for (auto* t : targets)
    {
        if (t->hp <= 0.0f || cheb(ctx.pos, t->pos) > kit().attack_range) continue;
        if (!cambc::vision::in_vision(t->pos)) continue;  // our own move can hide targets
        float p = camp_priority(*t);
        if (t->strength > 0.0f && t->hp <= ctx.strength) p += 1000.0f;  // finishing removes a damage source
        if (p > best_p) { best_p = p; best = t; }
    }
    if (!best) return false;
    if (!guarded("attack camp", [&] { me.attack(*best->camp); return true; })) return false;
    acts.attacked = true;
    refresh(*best);
    if (kit().splash)
        for (auto* t : targets)
            if (t != best && cheb(t->pos, best->pos) <= 1) refresh(*t);
    return true;
}

// Cast the most valuable ready ability against camps. Sets `reset` when an attack reset
// was used so the caller can attack again.
bool try_spell_camp(Ctx& ctx, TurnActions& acts, std::vector<CampView*>& targets, bool want_move, bool& reset)
{
    auto& me = cambc::self();
    if (acts.cast || !me.can_cast()) return false;
    const auto abilities = me.abilities();
    const auto& k = kit();
    int best_slot = -1;
    float best_value = 0.0f;
    CampView* best_target = nullptr;
    vec2 best_tile{};
    for (int i = 0; i < 2; ++i)
    {
        const auto& s = k.slot[i];
        if (s.kind == AbilityKind::none) continue;
        if (s.exclusive && (acts.moved || want_move)) continue;
        const auto& a = abilities[static_cast<std::size_t>(i)];
        if (a.cooldown() > 0) continue;
        float value = 0.0f;
        CampView* target = nullptr;
        vec2 tile{};
        switch (s.kind)
        {
        case AbilityKind::damage:
        case AbilityKind::cc:
        {
            if (!s.camps) break;
            const float dmg = s.modifier > 0.0f ? spell_damage(a, s, ctx.focus) : 0.0f;
            for (auto* t : targets)
            {
                if (t->hp <= 0.0f || cheb(ctx.pos, t->pos) > static_cast<int>(s.range)) continue;
                if (acts.moved && !cambc::vision::in_vision(t->pos)) continue;
                float v = std::min(t->hp, dmg);
                if (s.kind == AbilityKind::cc && t->awake) v += t->strength * 2.0f;
                if (t->strength <= 0.0f) v *= 0.3f;  // do not waste spells on clusters
                if (v > value) { value = v; target = t; }
            }
            break;
        }
        case AbilityKind::aoe:
        {
            if (!s.camps) break;
            const float dmg = spell_damage(a, s, ctx.focus);
            for (auto* c : targets)
            {
                if (c->hp <= 0.0f || cheb(ctx.pos, c->pos) > static_cast<int>(s.range)) continue;
                float v = 0.0f;
                for (auto* t : targets)
                    if (t->hp > 0.0f && cheb(c->pos, t->pos) <= static_cast<int>(s.radius))
                        v += std::min(t->hp, dmg) * (t->strength > 0.0f ? 1.0f : 0.3f) + (s.duration > 0 && t->awake ? t->strength : 0.0f);
                if (v > value) { value = v; tile = c->pos; target = c; }
            }
            break;
        }
        case AbilityKind::attack_reset:
            // Only worth it once our attack is spent and a target remains in range.
            if (!me.can_attack() && in_attack_range(ctx.pos, targets, k.attack_range)) value = ctx.strength;
            break;
        case AbilityKind::shield:
            if (camp_threat_at(ctx, ctx.pos) > 0.0f && ctx.hp_frac() < 0.9f) value = s.modifier * ctx.focus * 0.8f;
            break;
        case AbilityKind::heal:
            for (auto& ally : ctx.allies)
            {
                if (cheb(ctx.pos, ally.pos) > static_cast<int>(s.range) || !ally.champ->is_visible()) continue;
                const float missing = ally.max_hp - ally.hp;
                const float v = std::min(missing, s.modifier * ctx.focus);
                if (v > value && missing > 0.5f * s.modifier * ctx.focus) { value = v; best_target = nullptr; }
            }
            break;
        default:
            break;
        }
        if (value > best_value)
        {
            best_value = value;
            best_slot = i;
            best_target = target;
            best_tile = tile;
        }
    }
    if (best_slot < 0) return false;
    const auto& s = k.slot[best_slot];
    const auto& a = abilities[static_cast<std::size_t>(best_slot)];
    bool ok = false;
    switch (s.kind)
    {
    case AbilityKind::damage:
    case AbilityKind::cc:
        ok = best_target && guarded("camp spell", [&] { return a.use(*best_target->camp); });
        if (ok) refresh(*best_target);
        break;
    case AbilityKind::aoe:
        ok = guarded("camp aoe", [&] { return a.use(best_tile); });
        if (ok)
            for (auto* t : targets)
                if (cheb(best_tile, t->pos) <= static_cast<int>(s.radius)) refresh(*t);
        break;
    case AbilityKind::attack_reset:
    case AbilityKind::shield:
        ok = guarded("self spell", [&] { return a.use(); });
        if (ok && s.kind == AbilityKind::attack_reset) reset = true;
        break;
    case AbilityKind::heal:
    {
        const ChampView* who = nullptr;
        float most = 0.0f;
        for (auto& ally : ctx.allies)  // heals need a visible target (an ally seen at turn start can be hidden after we move)
            if (cheb(ctx.pos, ally.pos) <= static_cast<int>(s.range) && ally.max_hp - ally.hp > most && ally.champ->is_visible())
            {
                most = ally.max_hp - ally.hp;
                who = &ally;
            }
        ok = who && guarded("heal", [&] { return a.use(*who->champ); });
        break;
    }
    default:
        break;
    }
    if (ok) acts.cast = true;
    return ok;
}

struct FightPlan
{
    bool tank_near{};     // a melee ally is close to the group (it should be the target)
    bool tank_engaged{};  // a melee ally stands next to a living member
    bool awake{};
    int ally_dist[16]{};  // per target (same order): nearest visible ally's Chebyshev distance
};

bool is_melee_type(cambc::champion_type type)
{
    static int cache[16]{};  // 0 unknown, 1 melee, 2 ranged
    const auto i = static_cast<std::size_t>(type) & 15u;
    if (!cache[i]) cache[i] = cambc::champion_designs[static_cast<std::size_t>(type)].base_stats().base_attack_range <= 1 ? 1 : 2;
    return cache[i] == 1;
}

FightPlan plan_fight(const Ctx& ctx, const std::vector<CampView*>& targets)
{
    FightPlan plan;
    for (std::size_t i = 0; i < targets.size() && i < 16; ++i)
    {
        if (targets[i]->awake) plan.awake = true;
        int d = 1 << 20;
        for (const auto& a : ctx.allies) d = std::min(d, cheb(a.pos, targets[i]->pos));
        plan.ally_dist[i] = d;
    }
    for (const auto& a : ctx.allies)
    {
        if (!is_melee_type(a.type)) continue;
        int d = 1 << 20;
        for (auto* t : targets) d = std::min(d, cheb(a.pos, t->pos));
        if (d <= 6) plan.tank_near = true;
        if (d <= 1) plan.tank_engaged = true;
    }
    return plan;
}

// Score a tile for fighting the group: be in range of a target and, when ranged with a
// melee ally around, never be the nearest champion to a dangerous monster (monsters hit
// the nearest champion in range, so the tank should absorb it).
int fight_tile_score(const Ctx& ctx, vec2 n, const std::vector<CampView*>& targets, const FightPlan& plan,
                     const cambc::distance_field* route = nullptr)
{
    const int ar = kit().attack_range;
    int best_d = 1 << 20;
    for (auto* t : targets)
        if (t->hp > 0.0f) best_d = std::min(best_d, cheb(n, t->pos));
    int gap = best_d - ar;
    if (route && best_d > ar)
    {
        // Out of reach, count the walk too: by straight line alone a tile across a wall looked
        // closer, and the champion swapped with the travel step every turn (01broken-curve).
        const auto rd = route->get(n);
        gap = std::max(gap, rd == cambc::distance_field::unreachable ? (1 << 12) : static_cast<int>(rd) - ar);
    }
    int s = best_d <= ar ? 1000 : -40 * gap;
    if (kit().ranged && best_d <= ar) s += 10 * best_d;  // prefer keeping distance
    // Calm farming beside enemies: never step into a tile one of them can strike next turn.
    if (cfg::calm_farm > 0)
        for (const auto& e : ctx.enemies)
            if (cheb(n, e.pos) <= enemy_reach(e)) s -= 3000;
    // Melee champions tank by design: only ranged champions avoid being the nearest target.
    if (!kit().ranged) return s;
    const float factor = plan.tank_near ? 40.0f : 4.0f;
    for (std::size_t i = 0; i < targets.size() && i < 16; ++i)
    {
        const auto* t = targets[i];
        if (t->hp <= 0.0f || t->strength <= 0.0f) continue;
        const int my_d = cheb(n, t->pos);
        if (my_d > t->range) continue;
        if (plan.ally_dist[i] >= my_d) s -= static_cast<int>(factor * t->strength);
    }
    (void)ctx;
    return s;
}

// Hold just outside every dangerous monster's range (waiting for the tank to engage).
int hold_tile_score(vec2 n, const std::vector<CampView*>& targets)
{
    int s = 0;
    int nearest = 1 << 20;
    for (auto* t : targets)
    {
        const int d = cheb(n, t->pos);
        nearest = std::min(nearest, d);
        if (t->strength > 0.0f && d <= t->range) s -= 100000;
    }
    int want = 1;
    for (auto* t : targets)
        if (t->strength > 0.0f) want = std::max(want, t->range + 1);
    return s - 50 * std::abs(nearest - want);
}

bool fight_move(Ctx& ctx, TurnActions& acts, int group, std::vector<CampView*>& targets, const FightPlan& plan)
{
    auto& world = World::get();
    int nearest = 1 << 20;
    vec2 near_pos{};
    for (auto* t : targets)
        if (t->hp > 0.0f && cheb(ctx.pos, t->pos) < nearest) { nearest = cheb(ctx.pos, t->pos); near_pos = t->pos; }
    bool moved = false;
    // Out of reach with a wall in between (route longer than the straight line): follow the
    // route. Tile scoring is straight-line and pinned a champion against a wall corner, losing
    // sight of the camp and oscillating with travel for hundreds of rounds (01broken-curve).
    bool detour = false;
    if (nearest <= 6 && nearest > kit().attack_range)
        if (const auto* f = world.group_field(group, false))
        {
            const int d = World::distance(f, ctx.route_pos, near_pos);
            detour = d > nearest + 1;
        }
    if (nearest > 6 || detour)
    {
        const auto* field = world.group_field(group, true);
        if (!field) field = world.route_field(group, ctx.route_pos);
        if (field) moved = step_along(*field);
        if (!moved) moved = step_greedy(near_pos);
    }
    else if (!fuel_ok(cfg::fuel_fight_move))
        moved = nearest > kit().attack_range && step_greedy(near_pos);  // short budget: just close in
    else
    {
        const auto* route = cfg::fight_route_scoring ? world.group_field(group, false) : nullptr;
        const int stay = fight_tile_score(ctx, ctx.pos, targets, plan, route);
        moved = step_best([&](vec2 n) { return fight_tile_score(ctx, n, targets, plan, route); }, stay);
    }
    if (moved)
    {
        acts.moved = true;
        ctx.refresh_self();
    }
    return moved;
}

// --- Champions -------------------------------------------------------------------------

// Runtime designs are immutable. Cache at most one passive query per champion type;
// the default-off path performs no queries or bookkeeping.
bool can_stun(cambc::champion_type type)
{
    if constexpr (!cfg::control_immunity) return true;
    static std::uint8_t known[16]{};
    const auto index = static_cast<std::size_t>(type);
    if (index >= std::size(known) || index >= std::size(cambc::champion_designs)) return true;
    if (!known[index])
        known[index] = cambc::champion_designs[index].passive() == cambc::passive_type::stun_immunity ? 2 : 1;
    return known[index] == 1;
}

// Design summaries are immutable; querying them costs several host calls, so cache.
const model::ChampionSummary& cached_summary(cambc::champion_type type)
{
    static model::ChampionSummary cache[16];
    static bool known[16]{};
    const auto i = static_cast<std::size_t>(type) & 15u;
    if (!known[i])
    {
        cache[i] = design_summary(type);
        known[i] = true;
    }
    return cache[i];
}

float champ_dps(const ChampView& c)
{
    auto summary = cached_summary(c.type);
    summary.strength = c.strength;
    return model::camp_dps(summary, 0.0f, cfg::camp_dash_damage);
}

bool try_attack_enemy(Ctx& ctx, TurnActions& acts, const ChampView* focus)
{
    auto& me = cambc::self();
    if (!me.can_attack()) return false;
    ChampView* best = nullptr;
    float best_score = -1e9f;
    for (auto& e : ctx.enemies)
    {
        if (e.hp <= 0.0f || cheb(ctx.pos, e.pos) > kit().attack_range) continue;
        if (!cambc::vision::in_vision(e.pos)) continue;
        float score = -e.hp + static_cast<float>(e.gems) * 20.0f;
        if (e.hp <= ctx.strength) score += 5000.0f;
        if (focus && e.champ == focus->champ) score += 3000.0f;
        if (score > best_score) { best_score = score; best = &e; }
    }
    if (!best) return false;
    if (!best->champ->is_visible()) { best->hp = 0.0f; return false; }
    if (!guarded("attack champion", [&] { me.attack(*best->champ); return true; })) return false;
    acts.attacked = true;
    refresh(*best);
    return true;
}

bool try_spell_enemy(Ctx& ctx, TurnActions& acts, bool want_move, bool& reset, const ChampView* focus)
{
    auto& me = cambc::self();
    if (acts.cast || !me.can_cast()) return false;
    const auto abilities = me.abilities();
    const auto& k = kit();
    int best_slot = -1;
    float best_value = 0.0f;
    ChampView* best_target = nullptr;
    vec2 best_tile{};
    for (int i = 0; i < 2; ++i)
    {
        const auto& s = k.slot[i];
        if (s.kind == AbilityKind::none) continue;
        if (s.exclusive && (acts.moved || want_move)) continue;
        const auto& a = abilities[static_cast<std::size_t>(i)];
        if (a.cooldown() > 0) continue;
        float value = 0.0f;
        ChampView* target = nullptr;
        vec2 tile{};
        switch (s.kind)
        {
        case AbilityKind::damage:
        case AbilityKind::cc:
            if (!s.enemies) break;
            for (auto& e : ctx.enemies)
            {
                if (e.hp <= 0.0f || cheb(ctx.pos, e.pos) > static_cast<int>(s.range)) continue;
                if (acts.moved && !cambc::vision::in_vision(e.pos)) continue;
                float v = std::min(e.hp, s.modifier * ctx.focus) + (s.kind == AbilityKind::cc ? 60.0f + (e.channeling ? 200.0f : 0.0f) : 0.0f);
                if (e.hp <= s.modifier * ctx.focus) v += 500.0f;
                if (focus && e.champ == focus->champ) v += 300.0f;
                if (v > value) { value = v; target = &e; }
            }
            break;
        case AbilityKind::aoe:
            if (!s.enemies) break;
            for (auto& c : ctx.enemies)
            {
                if (c.hp <= 0.0f || cheb(ctx.pos, c.pos) > static_cast<int>(s.range)) continue;
                float v = 0.0f;
                for (auto& e : ctx.enemies)
                    if (e.hp > 0.0f && cheb(c.pos, e.pos) <= static_cast<int>(s.radius))
                        v += std::min(e.hp, s.modifier * ctx.focus) + (s.duration > 0 && can_stun(e.type) ? 60.0f : 0.0f);
                if (v > value) { value = v; tile = c.pos; }
            }
            break;
        case AbilityKind::attack_reset:
        {
            bool target_in_range = false;
            for (auto& e : ctx.enemies)
                if (e.hp > 0.0f && cheb(ctx.pos, e.pos) <= k.attack_range) target_in_range = true;
            if (!me.can_attack() && target_in_range) value = ctx.strength;
            break;
        }
        case AbilityKind::shield:
            if (enemy_threat_at(ctx, ctx.pos) > 0.0f) value = s.modifier * ctx.focus * 0.8f;
            break;
        case AbilityKind::heal:
            for (auto& ally : ctx.allies)
                if (cheb(ctx.pos, ally.pos) <= static_cast<int>(s.range) && ally.champ->is_visible())
                    value = std::max(value, std::min(ally.max_hp - ally.hp, s.modifier * ctx.focus));
            break;
        default:
            break;
        }
        if (value > best_value)
        {
            best_value = value;
            best_slot = i;
            best_target = target;
            best_tile = tile;
        }
    }
    if (best_slot < 0) return false;
    const auto& s = k.slot[best_slot];
    const auto& a = abilities[static_cast<std::size_t>(best_slot)];
    bool ok = false;
    switch (s.kind)
    {
    case AbilityKind::damage:
    case AbilityKind::cc:
        ok = best_target && best_target->champ->is_visible() && guarded("enemy spell", [&] { return a.use(*best_target->champ); });
        if (ok) refresh(*best_target);
        break;
    case AbilityKind::aoe:
        ok = guarded("enemy aoe", [&] { return a.use(best_tile); });
        if (ok)
            for (auto& e : ctx.enemies)
                if (cheb(best_tile, e.pos) <= static_cast<int>(s.radius)) refresh(e);
        break;
    case AbilityKind::attack_reset:
    case AbilityKind::shield:
        ok = guarded("self spell", [&] { return a.use(); });
        if (ok && s.kind == AbilityKind::attack_reset) reset = true;
        break;
    case AbilityKind::heal:
    {
        const ChampView* who = nullptr;
        float most = 0.0f;
        for (auto& ally : ctx.allies)  // heals need a visible target (an ally seen at turn start can be hidden after we move)
            if (cheb(ctx.pos, ally.pos) <= static_cast<int>(s.range) && ally.max_hp - ally.hp > most && ally.champ->is_visible())
            {
                most = ally.max_hp - ally.hp;
                who = &ally;
            }
        ok = who && guarded("heal", [&] { return a.use(*who->champ); });
        break;
    }
    default:
        break;
    }
    if (ok) acts.cast = true;
    return ok;
}

} // namespace

float pvp_dps_of(cambc::champion_type type, float strength, float focus)
{
    auto summary = cached_summary(type);
    if (strength > 0.0f) summary.strength = strength;
    if (focus > 0.0f) summary.focus = focus;
    return model::pvp_dps(summary, cfg::sim_dash_damage);
}

float shield_now(const cambc::champion& c)
{
    try
    {
        const auto b = c.buffs();
        if (b.has_shield()) return b.get<cambc::buff_shield>().value;
    }
    catch (const std::exception&)
    {
    }
    return 0.0f;
}

ShieldAbility shield_ability_of(cambc::champion_type type)
{
    const auto& s = cached_summary(type);
    for (int k = 0; k < 2; ++k)
        if (s.ability[k].kind == model::AbilityKind::shield) return { k, s.ability[k].modifier, s.focus };
    return {};
}

model::PvpProfile pvp_profile_of(cambc::champion_type type, float strength, float focus)
{
    auto summary = cached_summary(type);
    if (strength > 0.0f) summary.strength = strength;
    if (focus > 0.0f) summary.focus = focus;
    return model::pvp_profile(summary, cfg::sim_dash_damage);
}

float camp_threat_at(const Ctx& ctx, vec2 at)
{
    float total = 0.0f;
    for (auto& c : ctx.camps)
        if (c.awake && c.strength > 0.0f && cheb(at, c.pos) <= c.range) total += c.strength;
    for (int i = 0; i < ctx.n_unseen; ++i)
        if (cheb(at, ctx.unseen[i].pos) <= ctx.unseen[i].range) total += ctx.unseen[i].strength;
    return total * ctx.camp_damage_taken_mult;
}

float incoming_camp_rate(const Ctx& ctx, vec2 at)
{
    float total = 0.0f;
    for (const auto& c : ctx.camps)
    {
        if (!c.awake || c.strength <= 0.0f) continue;
        const int d = cheb(at, c.pos);
        if (d > c.range) continue;
        bool nearest = true;
        for (const auto& a : ctx.allies)
            if (cheb(a.pos, c.pos) < d) { nearest = false; break; }
        if (nearest) total += c.strength;
    }
    for (int i = 0; i < ctx.n_unseen; ++i)  // unseen: assume it targets us
        if (cheb(at, ctx.unseen[i].pos) <= ctx.unseen[i].range) total += ctx.unseen[i].strength;
    return total * ctx.camp_damage_taken_mult;
}

int escape_rounds(const Ctx& ctx)
{
    int worst = 0;
    for (const auto& c : ctx.camps)
        if (c.awake && c.strength > 0.0f) worst = std::max(worst, c.range - cheb(ctx.pos, c.pos) + 1);
    for (int i = 0; i < ctx.n_unseen; ++i) worst = std::max(worst, ctx.unseen[i].range - cheb(ctx.pos, ctx.unseen[i].pos) + 1);
    return worst;
}

int dash_reach(const ChampView& e)
{
    if (!cfg::dash_aware_reach) return 0;
    const auto& s = cached_summary(e.type);
    int best = 0;
    for (int k = 0; k < 2; ++k)
        if (s.ability[k].kind == model::AbilityKind::dash && e.cooldown[k] == 0) best = std::max(best, static_cast<int>(s.ability[k].range));
    return best;
}

int enemy_reach(const ChampView& e)
{
    // Melee can walk and strike; ranged attack in place but cast at spell range. A ready dash
    // (live cooldown) covers its range on top, in the same turn.
    if constexpr (!cfg::ready_cast_reach) return (e.range <= 1 ? 2 : 4) + dash_reach(e);
    else
    {
        // Keep the legacy envelope as a floor. A spell is a separate action plan,
        // never a second cast after the other slot's dash. Designs are immutable;
        // this cache performs no live champion or allowance queries.
        struct SpellReach { float span{}; bool harmful{}; bool exclusive{}; };
        struct ReachKit { bool known{}; float move{}; SpellReach spell[2]{}; };
        static ReachKit kits[16];
        const auto index = static_cast<std::size_t>(e.type);
        int reach = (e.range <= 1 ? 2 : 4) + dash_reach(e);
        if (index >= std::size(kits) || index >= std::size(cambc::champion_designs)) return reach;
        auto& k = kits[index];
        if (!k.known)
        {
            const auto& design = cambc::champion_designs[index];
            k.move = static_cast<float>(design.base_stats().base_move_speed);
            const auto abilities = design.abilities();
            for (int slot = 0; slot < 2; ++slot)
            {
                const auto& info = abilities[static_cast<std::size_t>(slot)].info();
                if (!info) continue;
                std::visit([&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, cambc::ability_damage> || std::is_same_v<T, cambc::ability_cc> ||
                                  std::is_same_v<T, cambc::ability_aoe_damage>)
                    {
                        const bool enemy = value.valid_targets.contains(cambc::ability_target_mask::enemies);
                        bool ground = false, harmful = value.modifier > 0.0f;
                        float radius = 0.0f;
                        if constexpr (std::is_same_v<T, cambc::ability_aoe_damage>)
                        {
                            ground = value.valid_targets.contains(cambc::ability_target_mask::ground);
                            radius = value.radius;
                            harmful = harmful || value.duration > 0;
                        }
                        if constexpr (std::is_same_v<T, cambc::ability_cc>) harmful = harmful || value.duration > 0;
                        k.spell[slot] = { value.range + radius, harmful && (enemy || ground), value.exclusive_with_movement };
                    }
                }, *info);
            }
            k.known = true;
        }
        for (int slot = 0; slot < 2; ++slot)
        {
            // Cooldown ticks at owner end; CD1 is not ready at the next input.
            // Unknown cooldown remains potentially ready. Conditional kill resets
            // and unseen enemy movement are outside this augmentation's scope.
            if (e.cooldown[slot] > 0 || !k.spell[slot].harmful) continue;
            const float move = k.spell[slot].exclusive ? 0.0f : k.move;
            reach = std::max(reach, static_cast<int>(std::ceil(k.spell[slot].span + move)));
        }
        return reach;
    }
}

float enemy_threat_at(const Ctx& ctx, vec2 at)
{
    float total = 0.0f;
    for (auto& e : ctx.enemies)
        if (cheb(at, e.pos) <= enemy_reach(e)) total += e.dps;
    return total;
}

float power_ratio(const Ctx& ctx, int radius)
{
    float our_hp = ctx.hp, our_dps = model::camp_dps(kit().summary, 0.0f, cfg::camp_dash_damage) * (ctx.strength / std::max(1.0f, kit().summary.strength));
    for (auto& a : ctx.allies)
        if (cheb(ctx.pos, a.pos) <= radius) { our_hp += a.hp; our_dps += champ_dps(a); }
    float their_hp = 0.0f, their_dps = 0.0f;
    for (auto& e : ctx.enemies)
        if (cheb(ctx.pos, e.pos) <= radius) { their_hp += e.hp; their_dps += champ_dps(e); }
    if (their_hp <= 0.0f) return 1e9f;
    return (our_hp * our_dps) / std::max(1.0f, their_hp * their_dps);
}

bool fight_camps(Ctx& ctx, TurnActions& acts, int group, bool allow_move)
{
    FuelSpike spike{ "fight_camps", 40000 };
    std::vector<CampView*> targets;
    for (auto& c : ctx.camps)
        if (c.group == group && c.hp > 0.0f) targets.push_back(&c);
    if (targets.empty()) return false;
    // Large groups: only the nearest members matter this turn (tile scoring is O(tiles x members)).
    constexpr std::size_t kMaxTargets = 10;
    if (targets.size() > kMaxTargets)
    {
        std::vector<int> dist(targets.size());
        for (std::size_t j = 0; j < targets.size(); ++j) dist[j] = cheb(targets[j]->pos, ctx.pos);
        for (std::size_t i = 0; i < kMaxTargets; ++i)
        {
            std::size_t best = i;
            for (std::size_t j = i + 1; j < targets.size(); ++j)
                if (dist[j] < dist[best]) best = j;
            std::swap(targets[i], targets[best]);
            std::swap(dist[i], dist[best]);
        }
        targets.resize(kMaxTargets);
    }
    const auto plan = plan_fight(ctx, targets);
    auto& me = cambc::self();
    // Ranged champions let a nearby melee ally open the fight (the first hit wakes the group).
    if (kit().ranged && plan.tank_near && !plan.tank_engaged && !plan.awake)
    {
        if (allow_move && me.can_move() &&
            step_best([&](vec2 n) { return hold_tile_score(n, targets); }, hold_tile_score(ctx.pos, targets)))
        {
            acts.moved = true;
            ctx.refresh_self();
        }
        return true;
    }
    const int ar = kit().attack_range;
    bool acted = false;
    bool reset = false;
    if (try_strike_dash_camps(ctx, acts, targets)) acted = true;
    const int here = fight_tile_score(ctx, ctx.pos, targets, plan);
    // Ranged champions move first if their tile would make them the monsters' target.
    const bool bad_spot = kit().ranged && here < 1000 - 200;
    const bool want_move = allow_move && (!in_attack_range(ctx.pos, targets, ar) || bad_spot);
    if (!bad_spot)
    {
        acted |= try_attack_camp(ctx, acts, targets);
        acted |= try_spell_camp(ctx, acts, targets, want_move, reset);
        if (reset) { acted |= try_attack_camp(ctx, acts, targets); reset = false; }
    }
    if (allow_move && me.can_move() && want_move && fight_move(ctx, acts, group, targets, plan))
    {
        acted = true;
        acted |= try_attack_camp(ctx, acts, targets);
        acted |= try_spell_camp(ctx, acts, targets, false, reset);
        if (reset) acted |= try_attack_camp(ctx, acts, targets);
    }
    return acted;
}

bool try_focus_step(Ctx& ctx, TurnActions& acts, const ChampView* focus)
{
    if constexpr (!cfg::melee_focus_step) return false;
    const auto& k = kit();
    if (k.ranged || !focus || focus->hp <= 0.0f) return false;
    const int distance = cheb(ctx.pos, focus->pos);
    if (distance <= k.attack_range || distance > k.attack_range + 1) return false;
    if (!fuel_ok(cfg::fuel_fight_move)) return false;
    auto& me = cambc::self();
    if (!me.can_move() || !me.can_attack()) return false;
    // A model may include remembered enemies; only a current personal observation
    // can authorize an attack. The optional dash above may already have killed it.
    bool observed = false;
    for (const auto& e : ctx.enemies)
        if (e.champ == focus->champ && e.hp > 0.0f) observed = true;
    if (!observed || !cambc::vision::in_vision(focus->pos) || !focus->champ->is_visible()) return false;
    if (cfg::limited_chase && cheb(focus->pos, World::get().enemy_spawn_center) <= 6) return false;
    // Preserve a basic kill already available without spending movement.
    for (const auto& e : ctx.enemies)
        if (e.hp > 0.0f && e.hp <= ctx.strength && cheb(ctx.pos, e.pos) <= k.attack_range &&
            cambc::vision::in_vision(e.pos)) return false;
    const float focus_score = -focus->hp + static_cast<float>(focus->gems) * 20.0f + 3000.0f +
                              (focus->hp <= ctx.strength ? 5000.0f : 0.0f);
    const bool moved = step_best([&](vec2 tile) {
        // Hard exclusion survives hidden-blocker retries in step_best. Reaching a
        // different target is not delivery. Match try_attack_enemy's priorities.
        if (cheb(tile, focus->pos) > k.attack_range) return -1;
        for (const auto& e : ctx.enemies)
        {
            if (e.hp <= 0.0f || e.champ == focus->champ || cheb(tile, e.pos) > k.attack_range) continue;
            const float score = -e.hp + static_cast<float>(e.gems) * 20.0f +
                                (e.hp <= ctx.strength ? 5000.0f : 0.0f);
            if (score >= focus_score) return -1;
        }
        // Among reaching tiles retain the existing greedy geometric preference.
        const int dx = tile.x - focus->pos.x, dy = tile.y - focus->pos.y;
        return 10 - dx * dx - dy * dy;
    }, 0);
    if (!moved) return false;
    acts.moved = true;
    ctx.refresh_self();
    return true;
}

TaviContact tavi_growth_contact(Ctx& ctx, TurnActions& acts)
{
    if constexpr (!cfg::tavi_growth) return TaviContact::quiet;
    else
    {
        if (ctx.enemies.empty()) return TaviContact::quiet;
        auto& me = cambc::self();
        if (!me.is_alive()) return TaviContact::acted;
        if (!fuel_ok(cfg::fuel_fight_move)) return TaviContact::withdraw;
        const auto& k = kit();
        const auto abilities = me.abilities();
        int spell = -1, reset = -1;
        for (int slot = 0; slot < 2; ++slot)
        {
            const auto& s = k.slot[slot];
            if (abilities[static_cast<std::size_t>(slot)].cooldown() != 0) continue;
            if ((s.kind == AbilityKind::cc || s.kind == AbilityKind::damage) && s.enemies && s.modifier > 0.0f)
                spell = slot;
            if (s.kind == AbilityKind::attack_reset && !s.exclusive) reset = slot;
        }
        // A dead victim is gone. A new one-round stun covers the next-input horizon only
        // when this victim already acted this round; otherwise its stun can be spent on
        // its still-pending input, then expire before it acts ahead of us next round.
        const ChampView* controlled = nullptr;
        auto risk = [&](vec2 at, const ChampView* ignore = nullptr) {
            float total = camp_threat_at(ctx, at);
            for (const auto& enemy : ctx.enemies)
                if (&enemy != ignore && enemy.hp > 0.0f && cheb(at, enemy.pos) <= enemy_reach(enemy))
                    total += std::max(1.0f, enemy.dps);
            return total;
        };
        ChampView* focus = nullptr;
        float best = -1e9f;
        const auto& enemy_spawn = cambc::map::spawn_region(cambc::team::enemy);
        for (auto& enemy : ctx.enemies)
        {
            if (!fuel_ok(12000)) break;
            if (enemy.hp <= 0.0f) continue;
            const int distance = cheb(ctx.pos, enemy.pos);
            if (enemy_spawn.is_set(enemy.pos) || !enemy.champ->is_visible() ||
                enemy.champ->passive() == cambc::passive_type::retaliation) continue;
            const float range = spell >= 0 ? std::max(static_cast<float>(k.attack_range), k.slot[spell].range)
                                          : static_cast<float>(k.attack_range);
            if (static_cast<float>(distance) > range) continue;
            const float value = (enemy.channeling ? 250.0f : 0.0f) + static_cast<float>(enemy.gems) * 4.0f
                              + 1000.0f / std::max(1.0f, enemy.hp) - static_cast<float>(distance);
            if (value > best) { best = value; focus = &enemy; }
        }
        float incoming = risk(ctx.pos);
        const bool low_health = ctx.hp_frac() < cfg::recall_health_frac && !ctx.on_spawn;
        if (low_health && incoming <= 0.0f && !ctx.hit_unseen) return TaviContact::quiet;
        if (!focus && incoming <= 0.0f && !ctx.hit_unseen) return TaviContact::quiet;

        // One cast budget: the exclusive poke competes with a compatible extra basic.
        // A threatening sole victim needs actual stun immunity checked before choosing to stay.
        if (!low_health && focus && spell >= 0 && !acts.cast && !acts.moved && me.can_cast() &&
            abilities[static_cast<std::size_t>(spell)].valid_targets().contains(cambc::ability_target_mask::enemies) &&
            static_cast<float>(cheb(ctx.pos, focus->pos)) <= k.slot[spell].range)
        {
            const auto& s = k.slot[spell];
            const bool susceptible = s.duration > 0 && focus->champ->passive() != cambc::passive_type::stun_immunity;
            const bool covers_next_input = susceptible && focus->champ->has_completed_turn();
            const bool covered = incoming <= 0.0f || (covers_next_input && risk(ctx.pos, focus) <= 0.0f);
            const float damage = spell_damage(abilities[static_cast<std::size_t>(spell)], s, ctx.focus);
            const bool prefer = cheb(ctx.pos, focus->pos) > k.attack_range || incoming > 0.0f || reset < 0 ||
                                damage >= ctx.strength || focus->channeling;
            if (covered && prefer && focus->champ->is_visible())
                try
                {
                    if (abilities[static_cast<std::size_t>(spell)].use(*focus->champ))
                    {
                        acts.cast = true;
                        refresh(*focus);
                        ctx.refresh_self();
                        if (focus->hp <= 0.0f || (covers_next_input && focus->champ->buffs().has_stun())) controlled = focus;
                    }
                }
                catch (const std::exception& error)
                {
                    MB_LOG("tavi growth error casting poke: %s\n", error.what());
                }
        }
        incoming = risk(ctx.pos, controlled);
        auto attack = [&] {
            if (!fuel_ok(6000) || !me.is_alive() || !me.can_attack()) return false;
            ChampView* shot = nullptr;
            float value = -1e9f;
            for (auto& enemy : ctx.enemies)
                if (enemy.hp > 0.0f && cheb(ctx.pos, enemy.pos) <= k.attack_range && enemy.champ->is_visible() &&
                    enemy.champ->passive() != cambc::passive_type::retaliation)
                {
                    const float score = (&enemy == focus ? 3000.0f : 0.0f) + (enemy.hp <= ctx.strength ? 5000.0f : 0.0f)
                                      + static_cast<float>(enemy.gems) * 20.0f - enemy.hp;
                    if (score > value) { value = score; shot = &enemy; }
                }
            if (!shot) return false;
            try
            {
                me.attack(*shot->champ);
                acts.attacked = true;
                refresh(*shot);
                return true;
            }
            catch (const std::exception& error)
            {
                MB_LOG("tavi growth error attacking: %s\n", error.what());
                return false;
            }
        };
        auto reset_basic = [&] {
            if (!fuel_ok(6000) || reset < 0 || acts.cast || !me.is_alive() || !me.can_cast() || me.can_attack()) return;
            bool available = false;
            for (const auto& enemy : ctx.enemies)
                if (enemy.hp > 0.0f && cheb(ctx.pos, enemy.pos) <= k.attack_range && enemy.champ->is_visible() &&
                    enemy.champ->passive() != cambc::passive_type::retaliation) available = true;
            if (!available || abilities[static_cast<std::size_t>(reset)].cooldown() != 0) return;
            try
            {
                if (abilities[static_cast<std::size_t>(reset)].use())
                {
                    acts.cast = true;
                    attack();
                }
            }
            catch (const std::exception& error)
            {
                MB_LOG("tavi growth error casting reset: %s\n", error.what());
            }
        };
        if (incoming > 0.0f || ctx.hit_unseen || low_health)
        {
            // Movement comes before any ranged attack. Never accept a landing with more
            // known incoming, including hidden awake camps; retries retain the same exclusion.
            if (me.can_move() && fuel_ok(12000))
            {
                auto score = [&](vec2 at) {
                    const float damage = risk(at, controlled);
                    if (damage > incoming + 0.01f) return -1000000000;
                    int gap = 20;
                    for (const auto& enemy : ctx.enemies)
                        if (enemy.hp > 0.0f && &enemy != controlled)
                            gap = std::min(gap, cheb(at, enemy.pos) - enemy_reach(enemy));
                    return -static_cast<int>(damage * 1000.0f) + std::clamp(gap, -20, 20) * 100
                           - cheb(at, World::get().ally_spawn_center);
                };
                if (step_best(score, score(ctx.pos)))
                {
                    acts.moved = true;
                    ctx.refresh_self();
                    // The step already spent movement; Tavi's other spell is now exclusive.
                    // A legal reset shot cannot give up another escape step. Avoid retaliation.
                    reset_basic();
                    return TaviContact::withdrew;
                }
            }
            return acts.cast || acts.attacked ? TaviContact::acted : TaviContact::withdraw;
        }
        // Stationary basic -> reset -> basic, or exclusive poke -> compatible basic.
        // Reused attack guards query visibility and permissions again after every action.
        attack();
        reset_basic();
        if (acts.cast || acts.attacked || acts.moved) return TaviContact::acted;
        return TaviContact::quiet;  // ordinary farm/tribute/remote shop, never a chase step
    }
}

bool fight_enemies(Ctx& ctx, TurnActions& acts, bool aggressive, const ChampView* focus)
{
    if (ctx.enemies.empty()) return false;
    bool acted = false;
    bool reset = false;
    bool in_range = false;
    for (auto& e : ctx.enemies)
        if (e.hp > 0.0f && cheb(ctx.pos, e.pos) <= kit().attack_range) in_range = true;
    if (aggressive && try_strike_dash_enemies(ctx, acts, focus))
    {
        acted = true;
        for (auto& e : ctx.enemies)
            if (e.hp > 0.0f && cheb(ctx.pos, e.pos) <= kit().attack_range) in_range = true;
    }
    if constexpr (cfg::melee_focus_step)
        if (aggressive && try_focus_step(ctx, acts, focus))
        {
            acted = true;
            in_range = true;
        }
    acted |= try_attack_enemy(ctx, acts, focus);
    acted |= try_spell_enemy(ctx, acts, aggressive && !in_range, reset, focus);
    if (reset) { acted |= try_attack_enemy(ctx, acts, focus); reset = false; }
    auto& me = cambc::self();
    if (aggressive && !in_range && me.can_move())
    {
        // Close in on the focus target (or the weakest visible enemy).
        const ChampView* target = focus;
        if (!target || target->hp <= 0.0f)
            for (auto& e : ctx.enemies)
                if (e.hp > 0.0f && (!target || e.hp < target->hp)) target = &e;
        // Chase only what we can catch: walking ranged champions cannot attack in the same turn,
        // equal-speed targets keep their distance, and enemies heal fully on their own spawn.
        bool catchable = target != nullptr;
        if (catchable && cfg::limited_chase)
        {
            const int d = cheb(target->pos, ctx.pos);
            const int limit = kit().ranged ? kit().attack_range + 1 : 3;
            const auto& their_spawn = World::get().enemy_spawn_center;
            catchable = d <= limit && cheb(target->pos, their_spawn) > 6;
        }
        if (catchable && step_greedy(target->pos))
        {
            acts.moved = true;
            ctx.refresh_self();
            acted = true;
            acted |= try_attack_enemy(ctx, acts, focus);
            acted |= try_spell_enemy(ctx, acts, false, reset, focus);
            if (reset) acted |= try_attack_enemy(ctx, acts, focus);
        }
    }
    return acted;
}

namespace {

int dash_slot(bool damaging_only)
{
    for (int i = 0; i < 2; ++i)
    {
        const auto& s = kit().slot[i];
        if (s.kind != AbilityKind::dash) continue;
        if (damaging_only && s.modifier <= 0.0f && s.duration <= 0) continue;
        return i;
    }
    return -1;
}

bool cast_dash(Ctx& ctx, TurnActions& acts, int slot, vec2 landing)
{
    const auto abilities = cambc::self().abilities();
    bool ok = false;
    try
    {
        ok = abilities[static_cast<std::size_t>(slot)].use(landing);
        invalidate_obstacles();
    }
    catch (const std::exception& e)
    {
        MB_LOG("dash %d,%d->%d,%d moved%d can_move%d failed: %s\n", ctx.pos.x, ctx.pos.y, landing.x, landing.y, acts.moved,
               cambc::self().can_move(), e.what());
    }
    if (ok)
    {
        acts.cast = true;
        ctx.refresh_self();
    }
    return ok;
}

bool dash_ready(int slot, const TurnActions& acts)
{
    if (slot < 0 || acts.cast) return false;
    const auto& s = kit().slot[slot];
    if (s.exclusive && acts.moved) return false;
    auto& me = cambc::self();
    if (!me.can_cast()) return false;
    return me.abilities()[static_cast<std::size_t>(slot)].cooldown() == 0;
}

} // namespace

bool try_travel_dash(Ctx& ctx, TurnActions& acts, const cambc::distance_field& field, bool spend_strike)
{
    if (!ctx.enemies.empty() || !fuel_ok(12000)) return false;
    const int slot = dash_slot(false);
    if (!dash_ready(slot, acts)) return false;
    const auto& s = kit().slot[slot];
    if ((s.modifier > 0.0f || s.duration > 0) && !spend_strike) return false;
    const int here = field.get(ctx.pos);
    vec2 best = ctx.pos;
    int best_d = here - 1;  // a normal step gains one tile
    for (auto n : dash_landings(ctx.pos, static_cast<int>(s.range)))
    {
        const int d = field.get(n);
        if (d + 1 < best_d) { best_d = d + 1; best = n; }
    }
    if (best == ctx.pos) return false;
    return cast_dash(ctx, acts, slot, best);
}

bool try_strike_dash_camps(Ctx& ctx, TurnActions& acts, std::vector<CampView*>& targets)
{
    const int slot = dash_slot(true);
    if (!dash_ready(slot, acts) || !fuel_ok(cfg::fuel_strike_dash)) return false;  // landing scan up to ~22K
    const auto& s = kit().slot[slot];
    const float dmg = s.modifier * ctx.focus;
    const int radius = static_cast<int>(s.radius);
    // Cheap pre-check before computing landings: enough targets within dash range + radius?
    int near = 0;
    bool dangerous = false;
    for (const auto* c : targets)
        if (c->hp > 0.0f && cheb(c->pos, ctx.pos) <= static_cast<int>(s.range) + radius)
        {
            ++near;
            dangerous = dangerous || c->strength > 0.0f;
        }
    if (near < 2 && !(near == 1 && dangerous && s.duration > 0)) return false;
    vec2 best{};
    float best_value = 1.5f * dmg;  // worth it only with two hits or a stun on a damage dealer
    for (auto n : dash_landings(ctx.pos, static_cast<int>(s.range)))
    {
        float v = 0.0f;
        for (const auto* c : targets)
            if (c->hp > 0.0f && cheb(n, c->pos) <= radius)
                v += std::min(c->hp, dmg) * (c->strength > 0.0f ? 1.0f : 0.3f) + (s.duration > 0 && c->strength > 0.0f ? c->strength * 3.0f : 0.0f);
        if (v > best_value) { best_value = v; best = n; }
    }
    if (best_value <= 1.5f * dmg) return false;
    if (!cast_dash(ctx, acts, slot, best)) return false;
    for (auto* c : targets)
        if (cheb(best, c->pos) <= radius) refresh(*c);
    return true;
}

bool try_strike_dash_enemies(Ctx& ctx, TurnActions& acts, const ChampView* focus)
{
    const int slot = dash_slot(true);
    if (!dash_ready(slot, acts) || !fuel_ok(12000)) return false;
    const auto& s = kit().slot[slot];
    const float dmg = s.modifier * ctx.focus;
    const int radius = static_cast<int>(s.radius);
    bool any = false;
    for (const auto& e : ctx.enemies)
        if (e.hp > 0.0f && cheb(e.pos, ctx.pos) <= static_cast<int>(s.range) + radius) any = true;
    if (!any) return false;
    vec2 best{};
    float best_value = 0.8f * dmg;
    for (auto n : dash_landings(ctx.pos, static_cast<int>(s.range)))
    {
        float v = 0.0f;
        for (const auto& e : ctx.enemies)
        {
            if (e.hp <= 0.0f || cheb(n, e.pos) > radius) continue;
            v += std::min(e.hp, dmg) + (s.duration > 0 && can_stun(e.type) ? 120.0f : 0.0f) + (e.hp <= dmg ? 400.0f : 0.0f);
            if (focus && e.champ == focus->champ) v += 200.0f;
        }
        if (v > best_value) { best_value = v; best = n; }
    }
    if (best_value <= 0.8f * dmg) return false;
    if (!cast_dash(ctx, acts, slot, best)) return false;
    for (auto& e : ctx.enemies)
        if (cheb(best, e.pos) <= radius) refresh(e);
    return true;
}

bool use_support(Ctx& ctx, TurnActions& acts, bool threatened)
{
    auto& me = cambc::self();
    if (acts.cast || !me.can_cast()) return false;
    const auto abilities = me.abilities();
    for (int i = 0; i < 2; ++i)
    {
        const auto& s = kit().slot[i];
        const auto& a = abilities[static_cast<std::size_t>(i)];
        if (s.kind == AbilityKind::shield && threatened && !(s.exclusive && acts.moved) && a.cooldown() == 0)
        {
            if (guarded("shield", [&] { return a.use(); })) { acts.cast = true; return true; }
        }
        if (s.kind == AbilityKind::heal && !(s.exclusive && acts.moved) && a.cooldown() == 0)
        {
            // Support runs at turn end: measure from where we stand now and re-check that the
            // ally is still in our vision (an ally seen at turn start can be hidden after we move).
            const ChampView* who = nullptr;
            float most = 0.5f * s.modifier * ctx.focus;
            const vec2 here = me.position();
            for (auto& ally : ctx.allies)
                if (cheb(here, ally.pos) <= static_cast<int>(s.range) && ally.max_hp - ally.hp > most && ally.champ->is_visible())
                {
                    most = ally.max_hp - ally.hp;
                    who = &ally;
                }
            if (who && guarded("heal", [&] { return a.use(*who->champ); })) { acts.cast = true; return true; }
        }
    }
    return false;
}

} // namespace mb
