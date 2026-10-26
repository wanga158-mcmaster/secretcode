#include "shop.h"
#include <array>
#include <vector>
#include "config.h"

namespace mb {

namespace {

using cambc::item_type;
using model::AbilityKind;

enum class Family : std::uint8_t { monster, power, durability, treasure, jungle, home, bruiser };

// Shard item for a family and stat; matches the SDK's enum layout (family-major, then
// strength/focus/health, each followed by its emblem).
item_type shard(Family f, Stat s)
{
    const auto base = static_cast<std::uint32_t>(f) * 6u + static_cast<std::uint32_t>(s) * 2u;
    return item_type{ base };
}

item_type emblem(Family f, Stat s) { return item_type{ static_cast<std::uint32_t>(shard(f, s)) + 1u }; }

struct Step
{
    Family family;
    Stat stat;
    int shards;  // shards to own in this family/stat after this step (0 = unlimited)
};

std::vector<Step> plan_for(const Kit& k)
{
    const Stat main = main_stat(k);
    std::vector<Step> plan;
    if (cfg::shop_plan == 1)  // camp damage first, then camp survival, then raw stat
    {
        plan.push_back({ Family::monster, main, 5 });
        plan.push_back({ Family::jungle, Stat::health, 5 });
        plan.push_back({ Family::power, main, 0 });
        return plan;
    }
    if (cfg::shop_plan == 2)
    {
        plan.push_back({ Family::monster, main, 5 });
        plan.push_back({ Family::power, main, 0 });
        return plan;
    }
    if (cfg::shop_plan == 3)  // health-backed survival emblems for everyone
    {
        plan.push_back({ Family::jungle, Stat::health, 5 });
        plan.push_back({ Family::durability, Stat::health, 5 });
        plan.push_back({ Family::monster, main, 5 });
        plan.push_back({ Family::power, main, 0 });
        return plan;
    }
    const bool tank = !k.ranged || cfg::shop_plan == 4;  // plan 4: ranged champions buy the melee survival steps too
    // Plans 12/13: plan 7 with Home (recall 5 -> 3 turn ends) after Treasure / after Monster.
    const bool bruiser = cfg::shop_plan == 7 || cfg::shop_plan == 12 || cfg::shop_plan == 13;
    if (cfg::shop_plan == 5 || bruiser) plan.push_back({ Family::treasure, main, 5 });  // +20% gold first
    if (cfg::shop_plan == 12) plan.push_back({ Family::home, main, 5 });
    plan.push_back({ Family::jungle, main, 5 });
    if (cfg::shop_plan == 6) plan.push_back({ Family::treasure, main, 5 });  // +20% gold second
    if (tank) plan.push_back({ Family::durability, Stat::health, 5 });
    plan.push_back({ Family::monster, main, 5 });
    if (cfg::shop_plan == 13) plan.push_back({ Family::home, main, 5 });
    // Plan 7: Bruiser (5 of the chosen stat plus 1 of each other: 7 shards' worth) instead of Power (6).
    const Family stack = bruiser ? Family::bruiser : Family::power;
    if (tank) plan.push_back({ stack, Stat::health, 5 });
    plan.push_back({ stack, main, 0 });
    return plan;
}

int owned_shards(const cambc::enum_map<item_type>& items, Family f, Stat s)
{
    const auto sh = shard(f, s);
    const auto em = emblem(f, s);
    const int per = static_cast<int>(sh.combine_count());
    return static_cast<int>(items[sh]) + static_cast<int>(items[em]) * std::max(per, 1);
}

} // namespace

Stat main_stat(const Kit& k)
{
    if (cfg::focus_builds) return Stat::focus;  // ability-burst builds (proxy of top online teams)
    if (cfg::heart_focus && k.type == cambc::champion_type::heart_of_ruin) return Stat::focus;
    float attacks = 1.0f;
    float focus_value = 0.0f;
    for (const auto& s : k.slot)
    {
        const float per_round = 1.0f / static_cast<float>(std::max(1, s.cooldown));
        if (s.kind == AbilityKind::attack_reset) attacks += per_round;
        if ((s.kind == AbilityKind::damage || s.kind == AbilityKind::cc || s.kind == AbilityKind::aoe) && s.camps)
            focus_value += s.modifier * per_round * (s.kind == AbilityKind::aoe ? 1.5f : 1.0f);
        if (s.kind == AbilityKind::shield || s.kind == AbilityKind::heal) focus_value += s.modifier * per_round * 0.3f;
    }
    const float strength_value = k.strength_growth * attacks * (k.splash ? 2.0f : 1.0f);
    return k.focus_growth * focus_value > strength_value ? Stat::focus : Stat::strength;
}

int shop(Ctx& ctx)
{
    auto& me = cambc::self();
    static const std::vector<Step> plan = plan_for(kit());
    int bought = 0;
    for (int guard = 0; guard < 24; ++guard)
    {
        if (!fuel_ok(3000)) break;
        const auto items = me.item_count();
        const Step* next = nullptr;
        for (const auto& step : plan)
        {
            if (step.shards == 0 || owned_shards(items, step.family, step.stat) < step.shards) { next = &step; break; }
        }
        if (!next) break;
        const auto item = shard(next->family, next->stat);
        if (!me.can_buy(item, 1)) break;
        try
        {
            me.buy(item, 1);
            ++bought;
        }
        catch (const std::exception& e)
        {
            MB_LOG("buy failed: %s\n", e.what());
            break;
        }
    }
    if (bought)
    {
        ctx.gold = me.gold_count();
        ctx.camp_damage_taken_mult = damage_taken_multiplier(true);
    }
    return bought;
}

} // namespace mb
