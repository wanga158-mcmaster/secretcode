#include "plan.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include "catalog.h"
#include "rules.h"

namespace zero {
namespace {

constexpr float kRounds = 40.0f, kGems = 100.0f, kGold = 300.0f, kDist = 20.0f;
float clampf(float value, float low, float high) { return value < low ? low : value > high ? high : value; }

bool current(const Context& ctx, const GoalCursor& cursor)
{
    return ctx.obs && ctx.map && ctx.mem && ctx.paths && cursor.observation == ctx.obs && cursor.map == ctx.map &&
           cursor.revision == ctx.obs->revision;
}

bool kind_available(const Context& ctx, int kind)
{
    const auto& o = *ctx.obs;
    switch (kind)
    {
    case g_farm: return !ctx.map->groups.empty();
    case g_bank: return o.self.gems > 0 && !ctx.map->altars.empty();
    case g_recall: return o.alive && !o.on_spawn;
    case g_engage: case g_avoid: return !o.enemies.empty();
    case g_hold: return true;
    case g_assist: return !o.allies.empty();
    default: return false;
    }
}

int target_index(const Context& ctx, int kind, int target)
{
    switch (kind)
    {
    case g_farm: return target >= 0 && target < static_cast<int>(ctx.map->groups.size()) ? target : -1;
    case g_bank: return target >= 0 && target < static_cast<int>(ctx.map->altars.size()) ? target : -1;
    case g_engage:
        for (std::size_t i = 0; i < ctx.obs->enemies.size(); ++i) if (ctx.obs->enemies[i].id == target) return static_cast<int>(i);
        return -1;
    case g_assist:
        for (std::size_t i = 0; i < ctx.obs->allies.size(); ++i) if (ctx.obs->allies[i].id == target) return static_cast<int>(i);
        return -1;
    case g_recall: case g_avoid: case g_hold: return target == -1 ? 0 : -1;
    default: return -1;
    }
}

int target_count(const Context& ctx, int kind)
{
    switch (kind)
    {
    case g_bank: return static_cast<int>(ctx.map->altars.size());
    case g_engage: return static_cast<int>(ctx.obs->enemies.size());
    case g_assist: return static_cast<int>(ctx.obs->allies.size());
    case g_recall: case g_avoid: case g_hold: return 1;
    default: return 0;
    }
}

Cand& add(const Context& ctx, CandSet& out, int kind, int target, float stage)
{
    auto& c = out.c[out.n++];
    c = Cand{};
    c.kind = static_cast<std::uint8_t>(kind);
    c.target = target;
    c.phi[kind] = 1.0f;
    c.phi[7] = stage;
    c.phi[23] = ctx.mem->goal_kind == kind && ctx.mem->goal_target == target ? 1.0f : 0.0f;
    return c;
}

int respawn(const Context& ctx, int group)
{
    const auto& rounds = ctx.mem->respawn_round;
    return group < static_cast<int>(rounds.size()) && rounds[group] > ctx.obs->round ? rounds[group] - ctx.obs->round : 0;
}

float enemy_recency(const Context& ctx, int group)
{
    const auto& rounds = ctx.mem->enemy_near_round;
    const int age = group < static_cast<int>(rounds.size()) ? ctx.obs->round - rounds[group] : 999;
    return age < 999 ? clampf(1.0f - static_cast<float>(age) / kRounds, 0.0f, 1.0f) : 0.0f;
}

int status(const Context& ctx, int group)
{
    return group < static_cast<int>(ctx.obs->group_status.size()) ? ctx.obs->group_status[group] : gs_unknown;
}

void farm_range(const Context& ctx, int begin, int end, float gold_mult, Cand& c)
{
    const auto& map = *ctx.map;
    const auto& o = *ctx.obs;
    const float count = static_cast<float>(end - begin);
    float distance = 0.0f, gold = 0.0f, gems = 0.0f, hp = 0.0f, strength = 0.0f, members = 0.0f;
    float wait = 0.0f, recency = 0.0f, enemy_distance = 0.0f, known = 0.0f, reward = 0.0f, respawning = 0.0f;
    for (int g = begin; g < end; ++g)
    {
        const auto& group = map.groups[g];
        distance += static_cast<float>(cheb(group.center, o.self.at));
        gold += static_cast<float>(group.gold);
        gems += static_cast<float>(group.gems);
        hp += group.total_hp;
        strength += group.total_strength;
        members += static_cast<float>(group.members.size());
        wait += static_cast<float>(respawn(ctx, g));
        recency += enemy_recency(ctx, g);
        enemy_distance += static_cast<float>(cheb(group.center, map.spawn_center[1 - o.team]));
        const int st = status(ctx, g);
        known += st != gs_unknown ? 1.0f : 0.0f;
        reward += st == gs_reward ? 1.0f : 0.0f;
        respawning += st == gs_respawning ? 1.0f : 0.0f;
    }
    int claims = 0, visible = 0;
    for (std::size_t a = 0; a < o.claim_kind.size(); ++a)
        if (o.claim_kind[a] == g_farm && o.claim_target[a] >= begin && o.claim_target[a] < end) ++claims;
    for (const auto& camp : o.camps)
    {
        int group = camp.group;
        if (group < 0 && map.inside(camp.at) && static_cast<std::size_t>(map.idx(camp.at)) < map.group_of.size())
            group = map.group_of[static_cast<std::size_t>(map.idx(camp.at))];
        if (group >= begin && group < end) ++visible;
    }
    c.phi[8] = distance / count / kRounds;
    c.phi[9] = gold / count * gold_mult / kGold;
    c.phi[10] = gems / count / kGems;
    c.phi[11] = hp / count / 10000.0f;
    c.phi[12] = strength / count / 200.0f;
    c.phi[13] = count / static_cast<float>(map.groups.size());
    c.phi[14] = static_cast<float>(visible) / count / 64.0f;
    c.phi[15] = members / count / 64.0f;
    c.phi[16] = known / count;
    c.phi[17] = wait / count / kRounds;
    c.phi[18] = static_cast<float>(claims) / count / 4.0f;
    c.phi[19] = recency / count;
    c.phi[20] = enemy_distance / count / kRounds;
    c.phi[21] = reward / count;
    c.phi[22] = respawning / count;
    c.phi[23] = ctx.mem->goal_kind == g_farm && ctx.mem->goal_target >= begin && ctx.mem->goal_target < end ? 1.0f : 0.0f;
}

float enemy_damage(const Obs& o, float taken)
{
    float damage = 0.0f;
    for (const auto& enemy : o.enemies)
    {
        const auto rates = rules::rates(enemy.type, enemy.strength, enemy.focus, enemy.cooldown, true);
        if (cheb(enemy.at, o.self.at) <= rates.reach) damage += rates.burst;
    }
    return damage * taken;
}

int nearest_enemy(const Obs& o)
{
    int distance = 99;
    for (const auto& enemy : o.enemies) distance = std::min(distance, cheb(enemy.at, o.self.at));
    return distance;
}

void kind_facts(const Context& ctx, int kind, Cand& c)
{
    const auto& o = *ctx.obs;
    const auto& map = *ctx.map;
    const float hp_fraction = o.self.max_hp > 0.0f ? o.self.hp / o.self.max_hp : 0.0f;
    c.phi[23] = ctx.mem->goal_kind == kind && target_index(ctx, kind, ctx.mem->goal_target) >= 0 ? 1.0f : 0.0f;
    if (kind == g_farm)
    {
        farm_range(ctx, 0, static_cast<int>(map.groups.size()), rules::gold_multiplier(o.self.type, o.items), c);
    }
    else if (kind == g_bank)
    {
        c.phi[8] = static_cast<float>(map.altars.size()) / static_cast<float>(kMaxCands);
        c.phi[9] = static_cast<float>(o.self.gems) / kGems;
        c.phi[10] = static_cast<float>(rules::tribute_turns(o.self.gems)) / 10.0f;
        float distance = 0.0f, locked = 0.0f;
        for (std::size_t a = 0; a < map.altars.size(); ++a)
        {
            distance += static_cast<float>(cheb(map.altars[a], o.self.at));
            if (a < o.altar_lock.size() && o.altar_lock[a] == lk_theirs) locked += 1.0f;
        }
        c.phi[11] = locked / static_cast<float>(map.altars.size());
        c.phi[20] = distance / static_cast<float>(map.altars.size()) / kRounds;
    }
    else if (kind == g_recall)
    {
        c.phi[8] = o.self.max_hp > 0.0f ? 1.0f - hp_fraction : 0.0f;
        c.phi[9] = static_cast<float>(o.self.gold) / kGold;
        c.phi[10] = static_cast<float>(o.self.gems) / kGems;
        c.phi[11] = static_cast<float>(cheb(o.self.at, map.spawn_center[o.team])) / kRounds;
    }
    else if (kind == g_engage)
    {
        float hp = 0.0f, gems = 0.0f, distance = 0.0f, strength = 0.0f, ranged = 0.0f;
        for (const auto& enemy : o.enemies)
        {
            hp += enemy.hp; gems += static_cast<float>(enemy.gems); distance += static_cast<float>(cheb(enemy.at, o.self.at));
            strength += enemy.strength; ranged += catalog().champion(enemy.type).attack_type == 1 ? 1.0f : 0.0f;
        }
        const float count = static_cast<float>(o.enemies.size());
        c.phi[8] = count / static_cast<float>(kMaxCands);
        c.phi[9] = hp / count / 2000.0f;
        c.phi[10] = gems / count / kGems;
        c.phi[11] = distance / count / kDist;
        c.phi[12] = static_cast<float>(o.allies.size()) / 4.0f;
        c.phi[13] = strength / count / 100.0f;
        c.phi[14] = ranged / count;
    }
    else if (kind == g_avoid)
    {
        c.phi[8] = static_cast<float>(o.enemies.size()) / static_cast<float>(kMaxCands);
        c.phi[9] = static_cast<float>(std::min(nearest_enemy(o), 20)) / kDist;
        c.phi[10] = static_cast<float>(cheb(o.self.at, map.spawn_center[o.team])) / kRounds;
    }
    else if (kind == g_hold)
    {
        c.phi[8] = o.on_spawn ? 1.0f : 0.0f;
        c.phi[9] = hp_fraction;
        int near = 0;
        for (const auto& enemy : o.enemies) near += cheb(enemy.at, map.spawn_center[o.team]) <= 8 ? 1 : 0;
        c.phi[10] = static_cast<float>(near) / 4.0f;
    }
    else if (kind == g_assist)
    {
        float hp = 0.0f, distance = 0.0f, threats = 0.0f;
        for (const auto& ally : o.allies)
        {
            hp += ally.max_hp > 0.0f ? ally.hp / ally.max_hp : 1.0f;
            distance += static_cast<float>(cheb(ally.at, o.self.at));
            for (const auto& enemy : o.enemies) threats += cheb(enemy.at, ally.at) <= 4 ? 1.0f : 0.0f;
        }
        const float count = static_cast<float>(o.allies.size());
        c.phi[8] = count / static_cast<float>(kMaxCands);
        c.phi[9] = hp / count;
        c.phi[10] = distance / count / kDist;
        c.phi[11] = threats / count / 4.0f;
    }
}

void farm_targets(const Context& ctx, const GoalCursor& cursor, CandSet& out)
{
    const auto& o = *ctx.obs;
    const auto& map = *ctx.map;
    const auto& cat = catalog();
    const auto rates = rules::rates(o.self.type, o.self.strength, o.self.focus, o.self.cooldown, false);
    const float dps = rates.sustained * rules::camp_damage_dealt_multiplier(o.self.type, o.items);
    const float taken = rules::camp_damage_taken_multiplier(o.items), gold_mult = rules::gold_multiplier(o.self.type, o.items);
    std::size_t bucket_count = 1;
    while (bucket_count < o.camps.size() * 2) bucket_count *= 2;
    std::vector<int> buckets(bucket_count, -1), next(o.camps.size());
    const auto bucket_for = [&](Tile at) {
        return (static_cast<std::uint32_t>(at.x) * 0x9e3779b9u ^ static_cast<std::uint32_t>(at.y) * 0x85ebca6bu) &
               static_cast<std::uint32_t>(bucket_count - 1);
    };
    for (std::size_t i = 0; i < o.camps.size(); ++i)
    {
        auto& bucket = buckets[bucket_for(o.camps[i].at)];
        next[i] = bucket; bucket = static_cast<int>(i);
    }
    std::vector<float> hp;
    for (int g = cursor.begin; g < cursor.end; ++g)
    {
        const auto& group = map.groups[g];
        hp.resize(group.members.size());
        float hp_now = 0.0f;
        for (std::size_t i = 0; i < group.members.size(); ++i)
        {
            float value = cat.camp(group.members[i].camp_type).health;
            if (!o.camps.empty())
                for (int j = buckets[bucket_for(group.members[i].at)]; j >= 0; j = next[j])
                    if (o.camps[j].at == group.members[i].at) { value = o.camps[j].hp; break; }
            hp[i] = value; hp_now += value;
        }
        int claimants = 0;
        for (std::size_t a = 0; a < o.claim_kind.size(); ++a)
            if (o.claim_kind[a] == g_farm && o.claim_target[a] == g) ++claimants;
        const auto alone = rules::clear(map, g, hp.data(), dps);
        auto shared = alone;
        shared.rounds = static_cast<int>(std::ceil(static_cast<float>(alone.rounds) / static_cast<float>(1 + claimants)));
        shared.damage = alone.damage / static_cast<float>(1 + claimants);
        auto& c = add(ctx, out, g_farm, g, 0.0f);
        c.phi[8] = static_cast<float>(ctx.paths->to_group(g, o.self.at)) / kRounds;
        c.phi[9] = static_cast<float>(group.gold) * gold_mult / kGold;
        c.phi[10] = static_cast<float>(group.gems) / kGems;
        c.phi[11] = hp_now / 10000.0f;
        c.phi[12] = group.total_strength / 200.0f;
        c.phi[13] = clampf(static_cast<float>(alone.rounds) / kRounds, 0.0f, 10.0f);
        c.phi[14] = clampf(alone.damage * taken / std::max(1.0f, o.self.hp), 0.0f, 10.0f);
        c.phi[15] = clampf(static_cast<float>(shared.rounds) / kRounds, 0.0f, 10.0f);
        c.phi[16] = clampf(shared.damage * taken / static_cast<float>(1 + claimants) / std::max(1.0f, o.self.hp), 0.0f, 10.0f);
        c.phi[17] = static_cast<float>(respawn(ctx, g)) / kRounds;
        c.phi[18] = static_cast<float>(claimants) / 4.0f;
        c.phi[19] = enemy_recency(ctx, g);
        c.phi[20] = static_cast<float>(cheb(group.center, map.spawn_center[1 - o.team])) / kRounds;
        c.phi[21] = status(ctx, g) == gs_reward ? 1.0f : 0.0f;
        c.phi[22] = status(ctx, g) == gs_respawning ? 1.0f : 0.0f;
    }
}

void other_targets(const Context& ctx, int kind, CandSet& out)
{
    const auto& o = *ctx.obs;
    const auto& map = *ctx.map;
    const auto& cat = catalog();
    if (kind == g_bank)
    {
        for (std::size_t a = 0; a < map.altars.size(); ++a)
        {
            auto& c = add(ctx, out, kind, static_cast<int>(a), 0.0f);
            c.phi[8] = static_cast<float>(ctx.paths->to_altar(static_cast<int>(a), o.self.at)) / kRounds;
            c.phi[9] = static_cast<float>(o.self.gems) / kGems;
            c.phi[10] = static_cast<float>(rules::tribute_turns(o.self.gems)) / 10.0f;
            c.phi[11] = a < o.altar_lock.size() && o.altar_lock[a] == lk_theirs ? 1.0f : 0.0f;
            int near = 0;
            for (const auto& enemy : o.enemies) near += cheb(enemy.at, map.altars[a]) <= 6 ? 1 : 0;
            c.phi[12] = static_cast<float>(near) / 4.0f;
        }
    }
    else if (kind == g_engage)
    {
        const auto own_rates = rules::rates(o.self.type, o.self.strength, o.self.focus, o.self.cooldown, false);
        const float taken = rules::champion_damage_taken_multiplier(o.items);
        for (const auto& enemy : o.enemies)
        {
            const auto rates = rules::rates(enemy.type, enemy.strength, enemy.focus, enemy.cooldown, true);
            auto& c = add(ctx, out, kind, enemy.id, 0.0f);
            c.phi[8] = clampf(enemy.hp / std::max(1.0f, own_rates.sustained) / kRounds, 0.0f, 5.0f);
            c.phi[9] = clampf(o.self.hp / std::max(1.0f, rates.sustained * taken) / kRounds, 0.0f, 5.0f);
            c.phi[10] = static_cast<float>(enemy.gems) / kGems;
            c.phi[11] = static_cast<float>(cheb(enemy.at, o.self.at)) / kDist;
            int allies = 0, enemies = 0;
            for (const auto& ally : o.allies) allies += cheb(ally.at, enemy.at) <= 4 ? 1 : 0;
            for (const auto& other : o.enemies) enemies += &other != &enemy && cheb(other.at, enemy.at) <= 4 ? 1 : 0;
            c.phi[12] = static_cast<float>(allies) / 4.0f;
            c.phi[13] = static_cast<float>(enemies) / 4.0f;
            c.phi[14] = cat.champion(enemy.type).attack_type == 1 ? 1.0f : 0.0f;
            c.phi[15] = enemy.recalling || enemy.tributing ? 1.0f : 0.0f;
            c.phi[16] = enemy.max_hp > 0.0f ? enemy.hp / enemy.max_hp : 1.0f;
            c.phi[17] = clampf(rates.burst * taken / std::max(1.0f, o.self.hp), 0.0f, 3.0f);
        }
    }
    else if (kind == g_assist)
    {
        for (const auto& ally : o.allies)
        {
            auto& c = add(ctx, out, kind, ally.id, 0.0f);
            int threats = 0;
            for (const auto& enemy : o.enemies) threats += cheb(enemy.at, ally.at) <= 4 ? 1 : 0;
            c.phi[8] = ally.max_hp > 0.0f ? ally.hp / ally.max_hp : 1.0f;
            c.phi[9] = static_cast<float>(threats) / 4.0f;
            c.phi[10] = static_cast<float>(cheb(ally.at, o.self.at)) / kDist;
        }
    }
    else if (kind == g_recall)
    {
        auto& c = add(ctx, out, kind, -1, 0.0f);
        c.phi[8] = o.self.max_hp > 0.0f ? 1.0f - o.self.hp / o.self.max_hp : 0.0f;
        c.phi[9] = static_cast<float>(o.self.gold) / kGold;
        float best = 0.0f;
        for (int item = 0; item < cat.n_items; ++item)
        {
            const auto& d = cat.item(item);
            if (!d.purchasable || d.cost > o.self.gold || rules::item_info(item).kind != rules::ik_shard) continue;
            const auto gain = rules::buy_gain(o.self.type, o.items, item);
            best = std::max(best, gain.health / std::max(1.0f, o.self.max_hp) + gain.strength / std::max(1.0f, o.self.strength) +
                                      gain.focus / std::max(1.0f, o.self.focus));
        }
        c.phi[10] = best * static_cast<float>(o.self.gold / 30u) / 5.0f;
        c.phi[11] = static_cast<float>(std::min(ctx.paths->to_spawn(o.self.at), 200)) / kRounds;
        c.phi[12] = clampf(enemy_damage(o, rules::champion_damage_taken_multiplier(o.items)) / std::max(1.0f, o.self.hp), 0.0f, 3.0f);
        c.phi[13] = static_cast<float>(o.self.gems) / kGems;
        c.phi[14] = static_cast<float>(rules::recall_turns(o.items) - (o.recall_started ? 1 : 0)) / 5.0f;
    }
    else if (kind == g_avoid)
    {
        auto& c = add(ctx, out, kind, -1, 0.0f);
        c.phi[8] = clampf(enemy_damage(o, rules::champion_damage_taken_multiplier(o.items)) / std::max(1.0f, o.self.hp), 0.0f, 3.0f);
        c.phi[9] = static_cast<float>(std::min(nearest_enemy(o), 20)) / kDist;
        c.phi[10] = static_cast<float>(std::min(ctx.paths->to_spawn(o.self.at), 200)) / kRounds;
    }
    else if (kind == g_hold)
    {
        auto& c = add(ctx, out, kind, -1, 0.0f);
        kind_facts(ctx, kind, c);
    }
}

}  // namespace

void plan_begin(const Context& ctx, GoalCursor& cursor)
{
    cursor = GoalCursor{};
    cursor.observation = ctx.obs;
    cursor.map = ctx.map;
    cursor.revision = ctx.obs ? ctx.obs->revision : 0;
}

GoalPlanBuild plan_candidates(const Context& ctx, const GoalCursor& cursor, CandSet& out)
{
    out.head = h_goal;
    out.n = 0;
    if (!current(ctx, cursor)) return { GoalPlanStatus::stale, 0 };
    if (cursor.stage == GoalStage::done)
        return { kind_available(ctx, cursor.kind) && target_index(ctx, cursor.kind, cursor.target) >= 0 ?
                     GoalPlanStatus::complete : GoalPlanStatus::invalid, 0 };
    if (cursor.stage == GoalStage::kind)
    {
        for (int kind = g_farm; kind <= g_assist; ++kind)
            if (kind_available(ctx, kind)) kind_facts(ctx, kind, add(ctx, out, kind, -1, 1.0f));
        return { GoalPlanStatus::ok, out.n };
    }
    if (!kind_available(ctx, cursor.kind)) return { GoalPlanStatus::unavailable, 0 };
    if (cursor.kind == g_farm)
    {
        if (cursor.begin < 0 || cursor.end > static_cast<int>(ctx.map->groups.size()) || cursor.begin >= cursor.end)
            return { GoalPlanStatus::invalid, 0 };
        const int count = cursor.end - cursor.begin;
        if (cursor.stage == GoalStage::range && count > 4)
        {
            const float gold_mult = rules::gold_multiplier(ctx.obs->self.type, ctx.obs->items);
            for (int child = 0; child < 4; ++child)
            {
                const int begin = cursor.begin + count * child / 4, end = cursor.begin + count * (child + 1) / 4;
                farm_range(ctx, begin, end, gold_mult, add(ctx, out, g_farm, begin, 2.0f));
            }
            return { GoalPlanStatus::ok, out.n };
        }
        if (cursor.stage != GoalStage::target || count > 4) return { GoalPlanStatus::invalid, 0 };
        farm_targets(ctx, cursor, out);
        return { GoalPlanStatus::ok, out.n };
    }
    if (cursor.stage != GoalStage::target) return { GoalPlanStatus::invalid, 0 };
    const int count = target_count(ctx, cursor.kind);
    if (count > kMaxCands) return { GoalPlanStatus::overflow, count };
    if (!count) return { GoalPlanStatus::unavailable, 0 };
    other_targets(ctx, cursor.kind, out);
    return { GoalPlanStatus::ok, out.n };
}

bool plan_select(const Context& ctx, GoalCursor& cursor, const Cand& chosen)
{
    if (!current(ctx, cursor) || cursor.stage == GoalStage::done) return false;
    if (cursor.stage == GoalStage::kind)
    {
        if (chosen.target != -1 || !kind_available(ctx, chosen.kind)) return false;
        cursor.kind = chosen.kind;
        cursor.depth = 0;
        if (cursor.kind == g_farm)
        {
            cursor.begin = 0; cursor.end = static_cast<int>(ctx.map->groups.size());
            cursor.stage = cursor.end > 4 ? GoalStage::range : GoalStage::target;
        }
        else cursor.stage = GoalStage::target;
        return true;
    }
    if (chosen.kind != cursor.kind || !kind_available(ctx, cursor.kind)) return false;
    if (cursor.kind == g_farm)
    {
        const int count = cursor.end - cursor.begin;
        if (cursor.begin < 0 || cursor.end > static_cast<int>(ctx.map->groups.size()) || count <= 0) return false;
        if (cursor.stage == GoalStage::range && count > 4)
        {
            for (int child = 0; child < 4; ++child)
            {
                const int begin = cursor.begin + count * child / 4, end = cursor.begin + count * (child + 1) / 4;
                if (chosen.target != begin) continue;
                cursor.begin = begin; cursor.end = end; ++cursor.depth;
                cursor.stage = end - begin > 4 ? GoalStage::range : GoalStage::target;
                return true;
            }
            return false;
        }
        if (cursor.stage != GoalStage::target || count > 4 || chosen.target < cursor.begin || chosen.target >= cursor.end) return false;
    }
    else if (cursor.stage != GoalStage::target || target_count(ctx, cursor.kind) > kMaxCands ||
             target_index(ctx, cursor.kind, chosen.target) < 0) return false;
    cursor.target = chosen.target;
    cursor.stage = GoalStage::done;
    return true;
}

int plan_teacher_index(const Context& ctx, const GoalCursor& cursor, GoalKind intended_kind, int intended_target)
{
    if (!current(ctx, cursor) || cursor.stage == GoalStage::done || !kind_available(ctx, intended_kind) ||
        target_index(ctx, intended_kind, intended_target) < 0) return -1;
    if (cursor.stage == GoalStage::kind)
    {
        int index = 0;
        for (int kind = g_farm; kind <= g_assist; ++kind)
            if (kind_available(ctx, kind)) { if (kind == intended_kind) return index; ++index; }
        return -1;
    }
    if (cursor.kind != intended_kind) return -1;
    if (intended_kind == g_farm)
    {
        const int count = cursor.end - cursor.begin;
        if (cursor.begin < 0 || cursor.end > static_cast<int>(ctx.map->groups.size()) || count <= 0 ||
            intended_target < cursor.begin || intended_target >= cursor.end) return -1;
        if (cursor.stage == GoalStage::range && count > 4)
            for (int child = 0; child < 4; ++child)
                if (intended_target < cursor.begin + count * (child + 1) / 4) return child;
        return cursor.stage == GoalStage::target && count <= 4 ? intended_target - cursor.begin : -1;
    }
    return cursor.stage == GoalStage::target && target_count(ctx, intended_kind) <= kMaxCands ?
               target_index(ctx, intended_kind, intended_target) : -1;
}

}  // namespace zero

namespace zero {

GoalPlanSession::GoalPlanSession(Context context) : context_(context)
{
    valid_ = context_.obs && context_.map && context_.mem && context_.paths;
    plan_begin(context_, cursor_);
    if (valid_)
    {
        current_kind_ = context_.mem->goal_kind;
        current_target_ = context_.mem->goal_target;
    }
}

float GoalPlanSession::goldMultiplier()
{
    if (!gold_ready_)
    {
        gold_mult_ = rules::gold_multiplier(context_.obs->self.type, context_.obs->items);
        gold_ready_ = true;
    }
    return gold_mult_;
}

void GoalPlanSession::prepareGroups()
{
    if (groups_ready_) return;
    const auto& map = *context_.map;
    const auto& o = *context_.obs;
    if (map.groups.size() <= inline_groups_.size()) groups_ = inline_groups_.data();
    else { extra_groups_.resize(map.groups.size()); groups_ = extra_groups_.data(); }
    for (std::size_t g = 0; g < map.groups.size(); ++g)
    {
        const auto& group = map.groups[g]; auto& row = groups_[g];
        row.distance = static_cast<float>(cheb(group.center, o.self.at));
        row.gold = static_cast<float>(group.gold); row.gems = static_cast<float>(group.gems);
        row.hp = group.total_hp; row.strength = group.total_strength; row.members = static_cast<float>(group.members.size());
        row.wait = static_cast<float>(respawn(context_, static_cast<int>(g)));
        row.recency = enemy_recency(context_, static_cast<int>(g));
        row.enemy_distance = static_cast<float>(cheb(group.center, map.spawn_center[1 - o.team]));
        const int st = status(context_, static_cast<int>(g));
        row.known = st != gs_unknown ? 1.0f : 0.0f;
        row.reward = st == gs_reward ? 1.0f : 0.0f;
        row.respawning = st == gs_respawning ? 1.0f : 0.0f;
        row.claims = row.visible = 0;
    }
    for (std::size_t a = 0; a < o.claim_kind.size(); ++a)
        if (o.claim_kind[a] == g_farm && o.claim_target[a] >= 0 && static_cast<std::size_t>(o.claim_target[a]) < map.groups.size())
            ++groups_[static_cast<std::size_t>(o.claim_target[a])].claims;
    for (const auto& camp : o.camps)
    {
        int group = camp.group;
        if (group < 0 && map.inside(camp.at) && static_cast<std::size_t>(map.idx(camp.at)) < map.group_of.size())
            group = map.group_of[static_cast<std::size_t>(map.idx(camp.at))];
        if (group >= 0 && static_cast<std::size_t>(group) < map.groups.size()) ++groups_[static_cast<std::size_t>(group)].visible;
    }
    groups_ready_ = true;
}

void GoalPlanSession::prepareCampLookup()
{
    if (lookup_ready_) return;
    const auto& camps = context_.obs->camps;
    bucket_count_ = 1;
    while (bucket_count_ < camps.size() * 2) bucket_count_ *= 2;
    if (bucket_count_ <= inline_buckets_.size()) buckets_ = inline_buckets_.data();
    else { extra_buckets_.resize(bucket_count_); buckets_ = extra_buckets_.data(); }
    if (camps.size() <= inline_next_.size()) next_ = inline_next_.data();
    else { extra_next_.resize(camps.size()); next_ = extra_next_.data(); }
    std::fill_n(buckets_, bucket_count_, -1);
    for (std::size_t i = 0; i < camps.size(); ++i)
    {
        const auto at = camps[i].at;
        const auto hash = (static_cast<std::uint32_t>(at.x) * 0x9e3779b9u ^ static_cast<std::uint32_t>(at.y) * 0x85ebca6bu) &
                          static_cast<std::uint32_t>(bucket_count_ - 1);
        next_[i] = buckets_[hash]; buckets_[hash] = static_cast<int>(i);
    }
    lookup_ready_ = true;
}

Cand& GoalPlanSession::add(CandSet& out, int kind, int target, float stage)
{
    auto& candidate = out.c[out.n++];
    candidate = Cand{};
    candidate.kind = static_cast<std::uint8_t>(kind); candidate.target = target;
    candidate.phi[kind] = 1.0f; candidate.phi[7] = stage;
    candidate.phi[23] = current_kind_ == kind && current_target_ == target ? 1.0f : 0.0f;
    return candidate;
}

void GoalPlanSession::farmRange(int begin, int end, Cand& c)
{
    prepareGroups();
    const float count = static_cast<float>(end - begin);
    float distance = 0.0f, gold = 0.0f, gems = 0.0f, hp = 0.0f, strength = 0.0f, members = 0.0f;
    float wait = 0.0f, recency = 0.0f, enemy_distance = 0.0f, known = 0.0f, reward = 0.0f, respawning = 0.0f;
    int claims = 0, visible = 0;
    for (int g = begin; g < end; ++g)
    {
        const auto& row = groups_[g];
        distance += row.distance; gold += row.gold; gems += row.gems; hp += row.hp;
        strength += row.strength; members += row.members; wait += row.wait; recency += row.recency;
        enemy_distance += row.enemy_distance; known += row.known; reward += row.reward; respawning += row.respawning;
        claims += row.claims; visible += row.visible;
    }
    c.phi[8] = distance / count / kRounds;
    c.phi[9] = gold / count * goldMultiplier() / kGold;
    c.phi[10] = gems / count / kGems;
    c.phi[11] = hp / count / 10000.0f;
    c.phi[12] = strength / count / 200.0f;
    c.phi[13] = count / static_cast<float>(context_.map->groups.size());
    c.phi[14] = static_cast<float>(visible) / count / 64.0f;
    c.phi[15] = members / count / 64.0f;
    c.phi[16] = known / count;
    c.phi[17] = wait / count / kRounds;
    c.phi[18] = static_cast<float>(claims) / count / 4.0f;
    c.phi[19] = recency / count;
    c.phi[20] = enemy_distance / count / kRounds;
    c.phi[21] = reward / count;
    c.phi[22] = respawning / count;
    c.phi[23] = current_kind_ == g_farm && current_target_ >= begin && current_target_ < end ? 1.0f : 0.0f;
}

void GoalPlanSession::farmTargets(CandSet& out)
{
    prepareGroups();
    prepareCampLookup();
    const auto& o = *context_.obs;
    const auto& map = *context_.map;
    const auto& cat = catalog();
    if (!farm_rules_ready_)
    {
        const auto rates = rules::rates(o.self.type, o.self.strength, o.self.focus, o.self.cooldown, false);
        dps_ = rates.sustained * rules::camp_damage_dealt_multiplier(o.self.type, o.items);
        camp_taken_ = rules::camp_damage_taken_multiplier(o.items);
        farm_rules_ready_ = true;
    }
    for (int g = cursor_.begin; g < cursor_.end; ++g)
    {
        const auto& group = map.groups[g]; const auto& row = groups_[g];
        float* hp = inline_hp_.data();
        if (group.members.size() > inline_hp_.size()) { extra_hp_.resize(group.members.size()); hp = extra_hp_.data(); }
        float hp_now = 0.0f;
        for (std::size_t i = 0; i < group.members.size(); ++i)
        {
            float value = cat.camp(group.members[i].camp_type).health;
            if (!o.camps.empty())
            {
                const auto at = group.members[i].at;
                const auto hash = (static_cast<std::uint32_t>(at.x) * 0x9e3779b9u ^ static_cast<std::uint32_t>(at.y) * 0x85ebca6bu) &
                                  static_cast<std::uint32_t>(bucket_count_ - 1);
                for (int j = buckets_[hash]; j >= 0; j = next_[j])
                    if (o.camps[j].at == at) { value = o.camps[j].hp; break; }
            }
            hp[i] = value; hp_now += value;
        }
        const int claimants = row.claims;
        const auto alone = rules::clear(map, g, hp, dps_);
        auto shared = alone;
        shared.rounds = static_cast<int>(std::ceil(static_cast<float>(alone.rounds) / static_cast<float>(1 + claimants)));
        shared.damage = alone.damage / static_cast<float>(1 + claimants);
        auto& c = add(out, g_farm, g, 0.0f);
        c.phi[8] = static_cast<float>(context_.paths->to_group(g, o.self.at)) / kRounds;
        c.phi[9] = static_cast<float>(group.gold) * goldMultiplier() / kGold;
        c.phi[10] = static_cast<float>(group.gems) / kGems;
        c.phi[11] = hp_now / 10000.0f;
        c.phi[12] = group.total_strength / 200.0f;
        c.phi[13] = clampf(static_cast<float>(alone.rounds) / kRounds, 0.0f, 10.0f);
        c.phi[14] = clampf(alone.damage * camp_taken_ / std::max(1.0f, o.self.hp), 0.0f, 10.0f);
        c.phi[15] = clampf(static_cast<float>(shared.rounds) / kRounds, 0.0f, 10.0f);
        c.phi[16] = clampf(shared.damage * camp_taken_ / static_cast<float>(1 + claimants) / std::max(1.0f, o.self.hp), 0.0f, 10.0f);
        c.phi[17] = row.wait / kRounds;
        c.phi[18] = static_cast<float>(claimants) / 4.0f;
        c.phi[19] = row.recency;
        c.phi[20] = static_cast<float>(cheb(group.center, map.spawn_center[1 - o.team])) / kRounds;
        c.phi[21] = row.reward;
        c.phi[22] = row.respawning;
    }
}

GoalPlanBuild GoalPlanSession::candidates(CandSet& out)
{
    out.head = h_goal; out.n = 0;
    if (!valid_) return { GoalPlanStatus::invalid, 0 };
    if (cursor_.stage == GoalStage::done)
        return { kind_available(context_, cursor_.kind) && target_index(context_, cursor_.kind, cursor_.target) >= 0 ?
                     GoalPlanStatus::complete : GoalPlanStatus::invalid, 0 };
    if (cursor_.stage == GoalStage::kind)
    {
        for (int kind = g_farm; kind <= g_assist; ++kind)
            if (kind_available(context_, kind))
            {
                auto& c = add(out, kind, -1, 1.0f);
                if (kind == g_farm) farmRange(0, static_cast<int>(context_.map->groups.size()), c);
                else kind_facts(context_, kind, c);
            }
        return { GoalPlanStatus::ok, out.n };
    }
    if (!kind_available(context_, cursor_.kind)) return { GoalPlanStatus::unavailable, 0 };
    if (cursor_.kind == g_farm)
    {
        if (cursor_.begin < 0 || cursor_.end > static_cast<int>(context_.map->groups.size()) || cursor_.begin >= cursor_.end)
            return { GoalPlanStatus::invalid, 0 };
        const int count = cursor_.end - cursor_.begin;
        if (cursor_.stage == GoalStage::range && count > 4)
        {
            for (int child = 0; child < 4; ++child)
            {
                const int begin = cursor_.begin + count * child / 4, end = cursor_.begin + count * (child + 1) / 4;
                farmRange(begin, end, add(out, g_farm, begin, 2.0f));
            }
            return { GoalPlanStatus::ok, out.n };
        }
        if (cursor_.stage != GoalStage::target || count > 4) return { GoalPlanStatus::invalid, 0 };
        farmTargets(out);
        return { GoalPlanStatus::ok, out.n };
    }
    if (cursor_.stage != GoalStage::target) return { GoalPlanStatus::invalid, 0 };
    const int count = target_count(context_, cursor_.kind);
    if (count > kMaxCands) return { GoalPlanStatus::overflow, count };
    if (!count) return { GoalPlanStatus::unavailable, 0 };
    other_targets(context_, cursor_.kind, out);
    return { GoalPlanStatus::ok, out.n };
}

bool GoalPlanSession::select(const Cand& chosen)
{
    if (!valid_ || cursor_.stage == GoalStage::done) return false;
    if (cursor_.stage == GoalStage::kind)
    {
        if (chosen.target != -1 || !kind_available(context_, chosen.kind)) return false;
        cursor_.kind = chosen.kind; cursor_.depth = 0;
        if (cursor_.kind == g_farm)
        {
            cursor_.begin = 0; cursor_.end = static_cast<int>(context_.map->groups.size());
            cursor_.stage = cursor_.end > 4 ? GoalStage::range : GoalStage::target;
        }
        else cursor_.stage = GoalStage::target;
        return true;
    }
    if (chosen.kind != cursor_.kind || !kind_available(context_, cursor_.kind)) return false;
    if (cursor_.kind == g_farm)
    {
        const int count = cursor_.end - cursor_.begin;
        if (cursor_.begin < 0 || cursor_.end > static_cast<int>(context_.map->groups.size()) || count <= 0) return false;
        if (cursor_.stage == GoalStage::range && count > 4)
        {
            for (int child = 0; child < 4; ++child)
            {
                const int begin = cursor_.begin + count * child / 4, end = cursor_.begin + count * (child + 1) / 4;
                if (chosen.target != begin) continue;
                cursor_.begin = begin; cursor_.end = end; ++cursor_.depth;
                cursor_.stage = end - begin > 4 ? GoalStage::range : GoalStage::target;
                return true;
            }
            return false;
        }
        if (cursor_.stage != GoalStage::target || count > 4 || chosen.target < cursor_.begin || chosen.target >= cursor_.end) return false;
    }
    else if (cursor_.stage != GoalStage::target || target_count(context_, cursor_.kind) > kMaxCands ||
             target_index(context_, cursor_.kind, chosen.target) < 0) return false;
    cursor_.target = chosen.target; cursor_.stage = GoalStage::done;
    return true;
}

int GoalPlanSession::teacherIndex(GoalKind intended_kind, int intended_target) const
{
    if (!valid_ || cursor_.stage == GoalStage::done || !kind_available(context_, intended_kind) ||
        target_index(context_, intended_kind, intended_target) < 0) return -1;
    if (cursor_.stage == GoalStage::kind)
    {
        int index = 0;
        for (int kind = g_farm; kind <= g_assist; ++kind)
            if (kind_available(context_, kind)) { if (kind == intended_kind) return index; ++index; }
        return -1;
    }
    if (cursor_.kind != intended_kind) return -1;
    if (intended_kind == g_farm)
    {
        const int count = cursor_.end - cursor_.begin;
        if (cursor_.begin < 0 || cursor_.end > static_cast<int>(context_.map->groups.size()) || count <= 0 ||
            intended_target < cursor_.begin || intended_target >= cursor_.end) return -1;
        if (cursor_.stage == GoalStage::range && count > 4)
            for (int child = 0; child < 4; ++child)
                if (intended_target < cursor_.begin + count * (child + 1) / 4) return child;
        return cursor_.stage == GoalStage::target && count <= 4 ? intended_target - cursor_.begin : -1;
    }
    return cursor_.stage == GoalStage::target && target_count(context_, intended_kind) <= kMaxCands ?
               target_index(context_, intended_kind, intended_target) : -1;
}

}  // namespace zero
