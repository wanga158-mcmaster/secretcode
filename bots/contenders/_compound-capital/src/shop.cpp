#include "shop.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include "config.h"
#include "knowledge.h"
#include "world.h"
#include "capital_model.h"
#include "capital_episode.h"

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
    const bool battery = cfg::prism_battery && k.type == cambc::champion_type::prism;
    if (cfg::shop_plan == 99 || battery)  // the plan as data (cfg::shop_seq / shop_seq_ranged / shop_seq_prism)
    {
        const auto& seq = battery ? cfg::shop_seq_prism : k.ranged ? cfg::shop_seq_ranged : cfg::shop_seq;
        for (const int code : seq)
        {
            if (code < 0) break;
            const int fam = code / 100, st = (code / 10) % 10, shards = code % 10;
            if (fam > static_cast<int>(Family::bruiser) || st > 3) continue;
            plan.push_back({ static_cast<Family>(fam), st == 3 ? main : static_cast<Stat>(st), shards });
            if (shards == 0) break;  // unlimited: the last step
        }
        if (plan.empty() || plan.back().shards != 0) plan.push_back({ Family::bruiser, main, 0 });
        return plan;
    }
    if (cfg::shop_plan == 14)  // health first (procless, 19-21 of 23 games): team max HP r300 7.4K vs our 5.3K
    {
        plan.push_back({ Family::treasure, Stat::health, 5 });
        plan.push_back({ Family::jungle, main, 5 });
        plan.push_back({ Family::bruiser, Stat::health, 5 });
        plan.push_back({ Family::durability, Stat::health, 5 });
        plan.push_back({ Family::bruiser, main, 0 });
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

// ---- State-driven shop (cfg::shop_plan 100) -----------------------------------------------------
//
// Currency: champion-rounds over the expected rest of the game. Each champion spends a share of its
// rounds on things items shrink - fighting camps (1 / camp damage), heal trips forced by camp damage
// (taken ~ fight length x camp-damage multiplier, trips ~ taken / max health) and by champion damage,
// deaths (60 rounds plus the walk back; rate ~ effective health^-a x damage^-b) and recall channels.
// Those shares are measured on this champion (ShopSignals, blended with priors). A block's value is
// the per-round saving of the build after it, times the rounds left; Treasure adds 20% of the base
// income, spent later on blocks worth shop_future_share of the best block's value per gold now.

struct Build
{
    float strength{}, focus{}, health{};
    bool passive[7]{};  // emblem of the family owned (passive families only)
};

struct Tables
{
    float magnitude[7]{};    // passive magnitude of each family's emblem
    float bonus[7][3][3]{};  // per family and chosen stat: emblem points minus the five shards' (str, foc, hp)
    int combine{ 5 };
    float cost{ 30.0f };
    float neighbours{ 0.5f };  // mean neighbours of camp group members (splash and area damage)
    float walk_back{ 20.0f };  // typical walk from our spawn to the farming area
    // model::camp_dps and model::pvp_dps are linear in Strength and Focus: per-point coefficients.
    float camp_str{ 1.0f }, camp_foc{}, pvp_str{ 1.0f }, pvp_foc{};
};

const Tables& tables()
{
    static const Tables t = [] {
        Tables out;
        const auto probe = shard(Family::monster, Stat::strength).info();
        out.combine = static_cast<int>(std::max<std::uint32_t>(1, probe.combine_count));
        out.cost = static_cast<float>(std::max<std::uint32_t>(1, probe.cost));
        for (int f = 0; f < 7; ++f)
            for (int s = 0; s < 3; ++s)
            {
                const auto fam = static_cast<Family>(f);
                const bool needed = fam == Family::power || fam == Family::bruiser || s == 0;
                if (!needed) continue;
                const auto e = emblem(fam, static_cast<Stat>(s)).info();
                if (s == 0) out.magnitude[f] = e.passive ? e.passive_magnitude : 0.0f;
                if (fam != Family::power && fam != Family::bruiser) continue;
                float pts[3] = { static_cast<float>(e.strength_points), static_cast<float>(e.focus_points), static_cast<float>(e.health_points) };
                pts[s] -= static_cast<float>(out.combine);  // the shards' own points
                for (int k = 0; k < 3; ++k) out.bonus[f][s][k] = std::max(0.0f, pts[k]);
            }
        const auto& world = World::get();
        if (!world.groups.empty())
        {
            float n = 0.0f;
            std::vector<int> dist;
            for (const auto& g : world.groups)
            {
                n += g.neighbours;
                dist.push_back(cheb(g.center, world.ally_spawn_center));
            }
            out.neighbours = n / static_cast<float>(world.groups.size());
            // Champions farm the nearer half of the map most: the median group distance.
            std::nth_element(dist.begin(), dist.begin() + static_cast<long>(dist.size() / 2), dist.end());
            out.walk_back = static_cast<float>(std::max(8, dist[dist.size() / 2]));
        }
        auto unit = kit().summary;
        unit.strength = 1.0f;
        unit.focus = 0.0f;
        out.camp_str = model::camp_dps(unit, out.neighbours, cfg::camp_dash_damage);
        out.pvp_str = model::pvp_dps(unit, cfg::sim_dash_damage);
        unit.strength = 0.0f;
        unit.focus = 1.0f;
        out.camp_foc = model::camp_dps(unit, out.neighbours, cfg::camp_dash_damage);
        out.pvp_foc = model::pvp_dps(unit, cfg::sim_dash_damage);
        return out;
    }();
    return t;
}

float growth_of(Stat s)
{
    const auto& k = kit();
    return s == Stat::strength ? k.strength_growth : s == Stat::focus ? k.focus_growth : k.health_growth;
}

void add_points(Build& b, int stat, float points)
{
    const float g = growth_of(static_cast<Stat>(stat)) * points;
    if (stat == 0) b.strength += g;
    else if (stat == 1) b.focus += g;
    else
    {
        b.health += g;
#if capital_policy
        if (kit().type == cambc::champion_type::ryth)
                b.strength += g * cambc::constants::passive_abilities::ryth_health_strength_ratio;
#endif
    }
}

struct Pick
{
    Family family{ Family::bruiser };
    Stat stat{ Stat::strength };
    float value{ -1.0f };  // rounds saved over the horizon
    float rate{};          // rounds saved per round of owning it (delay cost)
    float flow{};          // the Treasure emblem's gold flow in rounds saved per round (0 once owned)
};

class ValueModel
{
public:
    // `gold`: what the champion holds now (the Treasure flow is priced on later purchases).
    ValueModel(const Build& now, float max_hp_now, int round)
    {
        const auto& t = tables();
        const auto& sig = shop_signals();
        b0_ = now;
        const float a = static_cast<float>(sig.alive_rounds), a0 = cfg::shop_prior_rounds, span = a + a0;
        const float pool = std::max(50.0f, (1.0f - cfg::recall_health_frac) * max_hp_now);
        trip_ = static_cast<float>(recall_turns_of(now)) + 1.0f + t.walk_back;
        fight_ = (static_cast<float>(sig.fight_rounds) + cfg::shop_prior_fight * a0) / span;
        heal_camp_ = cfg::shop_camp_damage_weight * (sig.camp_damage / pool * trip_ + cfg::shop_prior_heal * a0) / span;
        heal_pvp_ = sig.pvp_damage / pool * trip_ / span;
        const float bodies = static_cast<float>(std::max<std::size_t>(1, Knowledge::get().allies.size()));
        deaths_ = (static_cast<float>(sig.deaths + sig.team_deaths) / bodies + cfg::shop_prior_deaths * a0) / span;
        death_cost_ = static_cast<float>(cambc::detail::cached_game_settings.respawn_time) + t.walk_back;
        recalls_ = (static_cast<float>(sig.recalls) + 0.012f * a0) / span;
        float bonus = kit().bonus_gold ? 0.33f : 0.0f;
        if (now.passive[static_cast<int>(Family::treasure)]) bonus += t.magnitude[static_cast<int>(Family::treasure)];
        income_ = (sig.earned_gold + cfg::shop_prior_income * a0) / span / (1.0f + bonus);
        horizon_ = horizon(round);
        dps0_ = camp_dps(now);
        power0_ = pvp_power(now);
        t0_ = time_cost(now);
    }

    float horizon_rounds() const { return horizon_; }

#if capital_policy
    float pvp_cost(const Build& b) const
    {
        const float hp_ratio = std::max(1.0f, b0_.health) / std::max(1.0f, b.health);
        const float all_ratio = all_mult(b) / std::max(0.01f, all_mult(b0_));
        return heal_pvp_ * hp_ratio * all_ratio + deaths_ * death_cost_ * power0_ / std::max(1e-6f, pvp_power(b));
    }

#endif
    // Per-round time the build costs (shares of a round), relative to the measured build.
    float time_cost(const Build& b) const
    {
        const float dps_ratio = dps0_ / std::max(1.0f, camp_dps(b));
        const float hp_ratio = std::max(1.0f, b0_.health) / std::max(1.0f, b.health);
        const float camp_ratio = camp_mult(b) / std::max(0.01f, camp_mult(b0_));
        const float all_ratio = all_mult(b) / std::max(0.01f, all_mult(b0_));
        return fight_ * dps_ratio + heal_camp_ * dps_ratio * hp_ratio * camp_ratio + heal_pvp_ * hp_ratio * all_ratio +
               deaths_ * death_cost_ * power0_ / std::max(1e-6f, pvp_power(b)) + recalls_ * static_cast<float>(recall_turns_of(b));
    }

    // Best next block from build `b` (passive families not owned, Power, Bruiser; main stat or Health).
    Pick best(const Build& b, Pick* runner_up = nullptr) const
    {
        const auto& t = tables();
        const Stat main = main_stat(kit());
        const Stat stats[2] = { main, Stat::health };
        const float tb = time_cost(b);
        Pick best, second;
        float best_rate_other = 0.0f;
        Pick treasure[2];
        auto consider = [&](const Pick& p) {
            if (p.value > best.value) { second = best; best = p; }
            else if (p.value > second.value) second = p;
        };
        for (int f = 0; f < 7; ++f)
        {
            const auto fam = static_cast<Family>(f);
            const bool passive = fam != Family::power && fam != Family::bruiser;
            if (passive && b.passive[f]) continue;
            for (int si = 0; si < 2; ++si)
            {
                const int s = static_cast<int>(stats[si]);
                Build n = b;
                add_points(n, s, static_cast<float>(t.combine));
                for (int k = 0; k < 3; ++k)
                    if (t.bonus[f][s][k] > 0.0f) add_points(n, k, t.bonus[f][s][k]);
                if (passive) n.passive[f] = true;
                Pick p{ fam, static_cast<Stat>(s) };
                p.rate = std::max(0.0f, tb - time_cost(n));
                p.value = p.rate * horizon_;
                if (fam == Family::treasure) { treasure[si] = p; continue; }  // priced below
                best_rate_other = std::max(best_rate_other, p.rate);
                consider(p);
            }
        }
        // Treasure: magnitude x base income more gold for the rest of the game, spent as it comes on
        // blocks worth shop_future_share of the best block's saving per gold now: H^2 / 2 per unit.
        float flow = 0.0f;
        if (!b.passive[static_cast<int>(Family::treasure)])
        {
            const float per_gold = cfg::shop_future_share * best_rate_other / (t.cost * static_cast<float>(t.combine));
            const float extra = t.magnitude[static_cast<int>(Family::treasure)] * income_;
            flow = extra * per_gold * horizon_;
            for (auto& p : treasure)
            {
                p.rate += flow;
                p.value += flow * horizon_ * 0.5f;
                consider(p);
            }
        }
        best.flow = flow;
        if (runner_up) *runner_up = second;
        return best;
    }

    // One given block from build `b` (as priced in best(); `flow` is best()'s Treasure flow for `b`).
    Pick evaluate(const Build& b, Family fam, Stat stat, float flow) const
    {
        const auto& t = tables();
        const int f = static_cast<int>(fam), s = static_cast<int>(stat);
        const bool passive = fam != Family::power && fam != Family::bruiser;
        Build n = b;
        add_points(n, s, static_cast<float>(t.combine));
        for (int k = 0; k < 3; ++k)
            if (t.bonus[f][s][k] > 0.0f) add_points(n, k, t.bonus[f][s][k]);
        if (passive) n.passive[f] = true;
        Pick p{ fam, stat };
        p.rate = std::max(0.0f, time_cost(b) - time_cost(n));
        p.value = p.rate * horizon_;
        if (fam == Family::treasure && !b.passive[f])
        {
            p.rate += flow;
            p.value += flow * horizon_ * 0.5f;
        }
        p.flow = flow;
        return p;
    }

    // Debug summary of the measured budget.
    void describe(char* buf, std::size_t n) const
    {
        std::snprintf(buf, n, "H%.0f fi%.2f hc%.2f hp%.2f d%.4f rc%.3f inc%.2f", horizon_, fight_, heal_camp_, heal_pvp_, deaths_, recalls_, income_);
    }

private:
    Build b0_;
    float trip_{}, fight_{}, heal_camp_{}, heal_pvp_{}, deaths_{}, death_cost_{}, recalls_{}, income_{}, horizon_{};
    float dps0_{ 1.0f }, power0_{ 1.0f }, t0_{};

    static float camp_dps(const Build& b)
    {
        const auto& t = tables();
        float d = t.camp_str * b.strength + t.camp_foc * b.focus;
        if (b.passive[static_cast<int>(Family::monster)]) d *= 1.0f + t.magnitude[static_cast<int>(Family::monster)];
        return d;
    }
    static float all_mult(const Build& b)
    {
        return b.passive[static_cast<int>(Family::durability)] ? 1.0f - tables().magnitude[static_cast<int>(Family::durability)] : 1.0f;
    }
    static float camp_mult(const Build& b)
    {
        const float jungle = b.passive[static_cast<int>(Family::jungle)] ? 1.0f - tables().magnitude[static_cast<int>(Family::jungle)] : 1.0f;
        return jungle * all_mult(b);
    }
    static float pvp_power(const Build& b)
    {
        const auto& t = tables();
        const float dps = std::max(1.0f, t.pvp_str * b.strength + t.pvp_foc * b.focus);
        const float ehp = std::max(1.0f, b.health) / std::max(0.05f, all_mult(b));
        return std::pow(ehp / 1000.0f, cfg::shop_pvp_hp_exp) * std::pow(dps / 50.0f, cfg::shop_pvp_dps_exp);
    }
    static int recall_turns_of(const Build& b)
    {
        float scale = 1.0f;
        if (b.passive[static_cast<int>(Family::home)]) scale = 1.0f - tables().magnitude[static_cast<int>(Family::home)];
        return std::max(1, static_cast<int>(std::ceil(static_cast<float>(cambc::constants::recall_time) * scale - 1e-4f)));
    }
    // Expected rounds left: the prior game length, moving towards the projection of the leading
    // banking rate once banking has started.
    static float horizon(int round)
    {
        const float t = static_cast<float>(round);
        const float max_rounds = static_cast<float>(cambc::constants::max_rounds);
        const float win = static_cast<float>(cambc::constants::winning_gems);
        float end = cfg::shop_prior_end;
        if (t > 150.0f)
        {
            float best_left = 1e9f;
            for (const auto side : { cambc::team::ally, cambc::team::enemy })
            {
                const float banked = static_cast<float>(cambc::global_submitted_gems(side));
                const float rate = banked / (t - 50.0f);
                if (rate > 0.05f) best_left = std::min(best_left, (win - banked) / rate);
            }
            const float projected = best_left < 1e8f ? t + best_left : max_rounds;
            const float w = std::clamp((t - 150.0f) / 450.0f, 0.0f, 1.0f);
            end = (1.0f - w) * end + w * projected;
        }
        return std::clamp(end - t, 60.0f, std::max(60.0f, max_rounds - t));
    }
};

// Stats from the design's base and our items (no host queries, so it works while dead too): a shard
// adds one growth of its stat, an emblem its five shards plus the family's bonus points.
Build current_build(const cambc::enum_map<item_type>& items)
{
    const auto& t = tables();
    const auto& base = kit().summary;
    Build b;
    b.strength = base.strength;
    b.focus = base.focus;
    b.health = base.health;
    for (int f = 0; f < 7; ++f)
        for (int s = 0; s < 3; ++s)
        {
            const auto fam = static_cast<Family>(f);
            const auto st = static_cast<Stat>(s);
            const float shards = static_cast<float>(items[shard(fam, st)]);
            const float emblems = static_cast<float>(items[emblem(fam, st)]);
            if (shards <= 0.0f && emblems <= 0.0f) continue;
            add_points(b, s, shards + emblems * static_cast<float>(t.combine));
            for (int k = 0; k < 3; ++k)
                if (t.bonus[f][s][k] > 0.0f) add_points(b, k, emblems * t.bonus[f][s][k]);
            if (emblems > 0.0f && fam != Family::power && fam != Family::bruiser) b.passive[f] = true;
        }
    return b;
}

// A started block (uncombined shards of one family and stat): finish it before anything else.
bool partial_block(const cambc::enum_map<item_type>& items, Family& f, Stat& s)
{
    for (int fi = 0; fi < 7; ++fi)
        for (int si = 0; si < 3; ++si)
            if (items[shard(static_cast<Family>(fi), static_cast<Stat>(si))] > 0)
            {
                f = static_cast<Family>(fi);
                s = static_cast<Stat>(si);
                return true;
            }
    return false;
}

// One shard into the local copy of the build and item counts (emblems combine at `combine`).
void apply_shard(Build& b, cambc::enum_map<item_type>& items, Family f, Stat s)
{
    const auto& t = tables();
    add_points(b, static_cast<int>(s), 1.0f);
    const auto sh = shard(f, s);
    items[sh] = items[sh] + 1;
    if (static_cast<int>(items[sh]) >= t.combine)
    {
        items[sh] = 0;
        const auto em = emblem(f, s);
        items[em] = items[em] + 1;
        for (int k = 0; k < 3; ++k)
            if (t.bonus[static_cast<int>(f)][static_cast<int>(s)][k] > 0.0f) add_points(b, k, t.bonus[static_cast<int>(f)][static_cast<int>(s)][k]);
        if (f != Family::power && f != Family::bruiser) b.passive[static_cast<int>(f)] = true;
    }
}

// Plan 7 (the tuned fixed plan) as the state-driven shop's prior (cfg::shop_plan_margin > 0).
std::vector<Step> plan7_for(const Kit& k)
{
    const Stat main = main_stat(k);
    std::vector<Step> plan;
    plan.push_back({ Family::treasure, main, 5 });
    plan.push_back({ Family::jungle, main, 5 });
    if (!k.ranged) plan.push_back({ Family::durability, Stat::health, 5 });
    plan.push_back({ Family::monster, main, 5 });
    if (!k.ranged) plan.push_back({ Family::bruiser, Stat::health, 5 });
    plan.push_back({ Family::bruiser, main, 0 });
    return plan;
}

// The next block: with cfg::shop_plan_margin > 0 the prior plan's next step, unless the best block
// is worth at least (1 + margin) times as much; otherwise the best block.
Pick choose_block(const ValueModel& vm, const Build& b, const cambc::enum_map<item_type>& items, Pick* second)
{
    const Pick best = vm.best(b, second);
    if (cfg::shop_plan_margin <= 0.0f) return best;
    static const std::vector<Step> plan = plan7_for(kit());
    const auto& t = tables();
    for (const auto& step : plan)
    {
        const int owned = static_cast<int>(items[shard(step.family, step.stat)]) +
                          static_cast<int>(items[emblem(step.family, step.stat)]) * t.combine;
        if (step.shards != 0 && owned >= step.shards) continue;
        const Pick planned = vm.evaluate(b, step.family, step.stat, best.flow);
        if (best.value >= (1.0f + cfg::shop_plan_margin) * planned.value) return best;
        if (second) *second = best;
        return planned;
    }
    return best;
}

#if capital_policy
struct CapitalCache
{
    bool ready{};
    int round{-1000}, gold_shards{-1}, item_version{-1};
    std::uint32_t gems{};
    float price{1.0f / cfg::gem_value}, gem_flow{0.18f}, benefit{}, bank_credit{};
    capital::Flow before{}, after{};
    float survival{}, horizon{}, bundle_cost{};
#if capital_forecast
    int work_epoch{};
    capital::Build after_build;
#endif
};

CapitalCache& capital_cache()
{
    if constexpr (capital_policy) { static CapitalCache cache; return cache; }
    else __builtin_unreachable();
}

struct Workload
{
    std::array<capital::Work, 8> tasks{};
    std::array<int, 8> groups{};
    int count{};
    std::span<const capital::Work> view() const { return { tasks.data(), static_cast<std::size_t>(count) }; }
};

vec2 capital_exit(int group)
{
    auto& world = World::get();
    if (group < 0 || group >= static_cast<int>(world.groups.size())) return world.ally_spawn_center;
    for (const auto member : world.groups[static_cast<std::size_t>(group)].tiles)
        for (const auto direction : kDirs)
        {
            const auto exit = member + direction;
            if (exit.x < 0 || exit.y < 0 || exit.x >= world.width || exit.y >= world.height) continue;
            if (!world.blocked.is_set(exit) && world.group_at(exit) < 0) return exit;
        }
    return {-1, -1};
}

capital::Work capital_task(int index, int travel)
{
    auto& world = World::get();
    const auto& g = world.groups[static_cast<std::size_t>(index)];
    auto summary = kit().summary;
    summary.strength = 1;
    summary.focus = 0;
    const float str_coefficient = model::camp_dps(summary, g.neighbours, cfg::camp_dash_damage);
    summary.strength = 0;
    summary.focus = 1;
    const float focus_coefficient = model::camp_dps(summary, g.neighbours, cfg::camp_dash_damage);
    return { g.total_hp, g.weighted_hp, g.total_strength, static_cast<float>(g.gems), static_cast<float>(g.gold),
             str_coefficient, focus_coefficient, static_cast<float>(travel),
             static_cast<float>(World::distance(world.group_field(index, false), world.ally_spawn_center, g.center)),
             static_cast<float>(cambc::constants::capture_time + 1), static_cast<float>(g.respawn) };
}

// Background geometry advances across turns. It admits only full fields which the executor
// has really constructed and which connect the current route, spawn, and specified task exit.
struct CapitalWorkGeneration
{
    struct Candidate { int group{}; float key{}; };
    std::array<Candidate, 8> selected{};
    Workload workload;
    vec2 origin{};
    int anchor{-2}, round{-1000}, cursor{}, selected_count{}, made{}, epoch{};
    bool complete{};
};

CapitalWorkGeneration& capital_work_generation()
{
    static CapitalWorkGeneration generation;
    return generation;
}

void capital_work_step(const Ctx& ctx, int anchor)
{
    auto& g = capital_work_generation();
    auto& world = World::get();
    const auto start = fuel();
    const auto permitted = [&] { return capital::route_budget(start, fuel(), cfg::fuel_reserve + 8000, 6000); };
    if (!permitted()) return;
    if (g.anchor != anchor || ctx.round - g.round >= 40)
    {
        const int epoch = g.epoch + 1;
        g = {};
        g.anchor = anchor;
        g.round = ctx.round;
        g.epoch = epoch;
        g.origin = capital_exit(anchor);
    }
    if (g.complete || g.origin.x < 0) return;
    int scanned = 0;
    while (g.cursor < static_cast<int>(world.groups.size()) && scanned++ < 12 && permitted())
    {
        const int index = g.cursor++;
        const auto& group = world.groups[static_cast<std::size_t>(index)];
        if (group.gems == 0 || cheb(group.center, world.ally_spawn_center) > cheb(group.center, world.enemy_spawn_center) + 2) continue;
        const auto* field = world.group_field(index, false);
        if (!field) continue;
        const int travel = field->get(g.origin);
        if (travel >= (1 << 14) || field->get(ctx.route_pos) >= (1 << 14) || field->get(world.ally_spawn_center) >= (1 << 14)) continue;
        const float key = static_cast<float>(group.gems) /
                          (static_cast<float>(travel + cambc::constants::capture_time + 1) + group.total_hp / std::max(1.0f, ctx.strength));
        if (g.selected_count == 8 && key <= g.selected[7].key) continue;
        int at = g.selected_count < 8 ? g.selected_count++ : 7;
        while (at > 0 && g.selected[static_cast<std::size_t>(at - 1)].key < key)
        {
            g.selected[static_cast<std::size_t>(at)] = g.selected[static_cast<std::size_t>(at - 1)];
            --at;
        }
        g.selected[static_cast<std::size_t>(at)] = {index, key};
    }
    if (g.cursor < static_cast<int>(world.groups.size())) return;
    int built = 0;
    while (g.made < g.selected_count && built++ < 2 && permitted())
    {
        const int index = g.selected[static_cast<std::size_t>(g.made++)].group;
        const auto* field = world.group_field(index, false);
        const int travel = field->get(g.origin);
        const int at = g.workload.count++;
        g.workload.groups[static_cast<std::size_t>(at)] = index;
        g.workload.tasks[static_cast<std::size_t>(at)] = capital_task(index, travel);
    }
    g.complete = g.made == g.selected_count;
}

Workload capital_workload(const Ctx& ctx)
{
    auto& world = World::get();
    const auto origin = ctx.state.is_dead() ? world.ally_spawn_center : ctx.route_pos;
    const float strength = ctx.strength > 0 ? ctx.strength : kit().summary.strength;
    struct Candidate { int group{}; float key{}; };
    std::array<Candidate, 8> selected{};
    int count = 0;
    for (std::size_t i = 0; i < world.groups.size(); ++i)
    {
        const auto& g = world.groups[i];
        const auto* known = world.group_field(static_cast<int>(i), false);
        if (g.gems == 0 || !known || known->get(origin) >= (1 << 14) || known->get(world.ally_spawn_center) >= (1 << 14)) continue;
        if (cheb(g.center, world.ally_spawn_center) > cheb(g.center, world.enemy_spawn_center) + 2) continue;
        const float key = static_cast<float>(g.gems) / (static_cast<float>(cambc::constants::capture_time + 1) + g.total_hp / std::max(1.0f, strength) +
                                                     static_cast<float>(World::distance(world.group_field(static_cast<int>(i), false), origin, g.center)));
        if (count == 8 && key <= selected[7].key) continue;
        int at = count < 8 ? count++ : 7;
        while (at > 0 && selected[static_cast<std::size_t>(at - 1)].key < key)
        {
            selected[static_cast<std::size_t>(at)] = selected[static_cast<std::size_t>(at - 1)];
            --at;
        }
        selected[static_cast<std::size_t>(at)] = { static_cast<int>(i), key };
    }
    Workload out;
    for (int i = 0; i < count; ++i)
    {
        if (!fuel_ok(45000)) break;
        const auto index = selected[static_cast<std::size_t>(i)].group;
        const auto& g = world.groups[static_cast<std::size_t>(index)];
        const auto* field = world.group_field(index, false);
        int travel = 1 << 14;
        for (int j = 0; j < count; ++j)
            if (i != j)
            {
                if (!fuel_ok(45000)) break;
                const auto& source = world.groups[static_cast<std::size_t>(selected[static_cast<std::size_t>(j)].group)];
                const auto exit = capital_exit(selected[static_cast<std::size_t>(j)].group);
                if (exit.x >= 0) travel = std::min(travel, static_cast<int>(field->get(exit)) + 1);
            }
        if (travel >= (1 << 14)) travel = World::distance(field, world.ally_spawn_center, g.center);
        if (travel >= (1 << 14)) continue;
        out.groups[static_cast<std::size_t>(out.count)] = index;
        out.tasks[static_cast<std::size_t>(out.count++)] = capital_task(index, travel);
    }
    return out;
}

capital::Build capital_build(const Build& b)
{
    const auto& t = tables();
    capital::Build result;
    result.strength = b.strength;
    result.focus = b.focus;
    result.health = b.health;
    result.camp_damage = b.passive[static_cast<int>(Family::monster)] ? 1.0f + t.magnitude[static_cast<int>(Family::monster)] : 1.0f;
    result.damage_taken = (b.passive[static_cast<int>(Family::jungle)] ? 1.0f - t.magnitude[static_cast<int>(Family::jungle)] : 1.0f) *
                          (b.passive[static_cast<int>(Family::durability)] ? 1.0f - t.magnitude[static_cast<int>(Family::durability)] : 1.0f);
    result.gold_mult = 1.0f + (kit().bonus_gold ? static_cast<float>(cambc::constants::passive_abilities::vesper_vale_bonus_gold_percent) / 100.0f : 0.0f) +
                       (b.passive[static_cast<int>(Family::treasure)] ? t.magnitude[static_cast<int>(Family::treasure)] : 0.0f);
    result.recall = static_cast<float>(cambc::constants::recall_time) *
                    (b.passive[static_cast<int>(Family::home)] ? 1.0f - t.magnitude[static_cast<int>(Family::home)] : 1.0f);
    result.usable_share = 1.0f - cfg::recall_health_frac;
    result.safety = cfg::camp_safety_margin;
    result.max_trips = static_cast<float>(cfg::siege_max_trips);
    return result;
}

struct CapitalPick
{
    Family family{Family::bruiser};
    Stat stat{Stat::strength};
    int need{5};
    float value{}, price{};
    bool valid{};
};

#if capital_forecast
struct CapitalForecast
{
    struct Candidate { CapitalPick pick; capital::Build build; capital::Flow after; float survival{}; };
    std::array<Candidate, 14> candidates{};
    Workload workload;
    capital::Build before_build;
    capital::Flow before;
    capital::Build prefix_build;
    capital::Flow prefix_flow;
    float prefix_survival{}, observed_strength{}, observed_focus{}, observed_health{};
    float horizon{}, hp{}, pvp{}, nongold{};
    int round{}, kit{}, x{}, y{}, purse{}, emit{}, selected_family{}, selected_stat{}, bought{};
    unsigned eligible{}, evaluated{};
    bool captured{};
    struct Recall
    {
        int round{}, cache_round{}, purse{}, cache_purse{}, version{}, cache_version{}, target{-1}, extra{};
        float hp{}, max_hp{}, bundle{}, before{}, after{}, survival{}, advance{}, heal{}, benefit{}, caller_benefit{};
        bool captured{}, bound{}, emitted{}, shopping{};
    } recall;
    struct RecallTask
    {
        int group{}, seen{}, status{}, committed{}, capturers{}, from_current{}, from_spawn{}, x{}, y{};
        float hp{}, gems{}, gold{}, after_gems{}, after_gold{}, damage{}, fight{};
    };
    std::array<RecallTask, 8> recall_tasks{};
    int recall_task_count{}, recall_task_emit{};
};

CapitalForecast& capital_forecast_record()
{
    static CapitalForecast record;
    return record;
}
#endif

CapitalPick capital_candidate(const Build& b, const cambc::enum_map<item_type>& items, const Workload& workload,
                              const ValueModel& vm, capital::Flow before, Family family, Stat stat
#if capital_forecast
                              , CapitalForecast::Candidate* forecast = nullptr
#endif
                              )
{
    const auto& t = tables();
    auto next = b;
    auto counts = items;
    const int need = std::max(1, t.combine - static_cast<int>(counts[shard(family, stat)]));
    for (int k = 0; k < need; ++k) apply_shard(next, counts, family, stat);
    const auto after = capital::flow(workload.view(), capital_build(next));
    const float value = capital::bundle_value(before, after, vm.pvp_cost(b) - vm.pvp_cost(next), vm.horizon_rounds());
    const CapitalPick result{ family, stat, need, value, value / (static_cast<float>(need) * t.cost), true };
#if capital_forecast
    if (forecast) *forecast = { result, capital_build(next), after, vm.pvp_cost(b) - vm.pvp_cost(next) };
#endif
    return result;
}

CapitalPick capital_pick(const Build& b, const cambc::enum_map<item_type>& items, const Workload& workload, const ValueModel& vm, float* nongold_out = nullptr
#if capital_forecast
                         , CapitalForecast* forecast = nullptr
#endif
                         )
{
    const auto& t = tables();
    const auto before = capital::flow(workload.view(), capital_build(b));
    CapitalPick best;
    std::array<CapitalPick, 2> treasure{};
    float nongold_price = 0;
    for (int fi = 0; fi < 7; ++fi)
    {
        const auto f = static_cast<Family>(fi);
        if (f != Family::power && f != Family::bruiser && b.passive[fi]) continue;
#if capital_forecast
        if (forecast) forecast->eligible |= 3u << (fi * 2);
#endif
        for (const auto stat : { main_stat(kit()), Stat::health })
        {
#if capital_forecast
            const int slot = fi * 2 + (stat == Stat::health ? 1 : 0);
#endif
            if (!fuel_ok(20000)) break;
            const auto candidate = capital_candidate(b, items, workload, vm, before, f, stat
#if capital_forecast
                                                     , forecast ? &forecast->candidates[static_cast<std::size_t>(slot)] : nullptr
#endif
                                                     );
#if capital_forecast
            if (forecast) forecast->evaluated |= 1u << slot;
#endif
            if (f == Family::treasure) treasure[stat == Stat::health ? 1u : 0u] = candidate;
            else
            {
                nongold_price = std::max(nongold_price, candidate.price);
                if (candidate.price > best.price) best = candidate;
            }
        }
    }
    if (nongold_out) *nongold_out = nongold_price;
    if (!b.passive[static_cast<int>(Family::treasure)])
        for (auto candidate : treasure)
        {
            if (!candidate.valid) continue;
            // Gold from the same productive portfolio, reinvested halfway through its remaining
            // horizon at today's non-Treasure marginal yield. No self-reproducing lambda term.
            candidate.value += before.gold / std::max(1.0f, capital_build(b).gold_mult) *
                               t.magnitude[static_cast<int>(Family::treasure)] * vm.horizon_rounds() * 0.5f * nongold_price;
            candidate.price = candidate.value / (static_cast<float>(candidate.need) * t.cost);
#if capital_forecast
            if (forecast)
                forecast->candidates[static_cast<int>(candidate.family) * 2 + (candidate.stat == Stat::health ? 1 : 0)].pick = candidate;
#endif
            if (candidate.price > best.price) best = candidate;
        }
    return best;
}

extern int g_items_version;
struct CapitalSearch
{
    int epoch{-1}, item_version{-1}, round{-1000}, cursor{};
    Build build{};
    cambc::enum_map<item_type> items{};
    std::optional<ValueModel> vm;
    capital::Flow before{};
    CapitalPick best;
    std::array<CapitalPick, 2> treasure{};
    float nongold_price{};
    bool complete{};
};

CapitalSearch& capital_search()
{
    static CapitalSearch search;
    return search;
}

// Candidate fairness is persistent too: a small tail must not re-evaluate the same cheap
// prefix each turn and publish it as the best of a set which was never actually considered.
void capital_search_step(const Ctx& ctx, const CapitalWorkGeneration& geometry)
{
    auto& search = capital_search();
    const auto start = fuel();
    const auto permitted = [&] { return capital::route_budget(start, fuel(), cfg::fuel_reserve + 8000, 6000); };
    if (!permitted()) return;
    if (search.epoch != geometry.epoch || search.item_version != g_items_version || ctx.round - search.round >= 40)
    {
        search = {};
        search.epoch = geometry.epoch;
        search.item_version = g_items_version;
        search.round = ctx.round;
        search.items = cambc::self().item_count();
        search.build = current_build(search.items);
        search.vm.emplace(search.build, search.build.health, ctx.round);
        search.before = capital::flow(geometry.workload.view(), capital_build(search.build));
    }
    if (search.complete) return;
    int evaluated = 0;
    while (search.cursor < 14 && evaluated < 2 && permitted())
    {
        const int cursor = search.cursor++;
        const auto family = static_cast<Family>(cursor / 2);
        const auto stat = cursor % 2 == 0 ? main_stat(kit()) : Stat::health;
        if (family != Family::power && family != Family::bruiser && search.build.passive[cursor / 2]) continue;
        ++evaluated;
        const auto candidate = capital_candidate(search.build, search.items, geometry.workload, *search.vm, search.before, family, stat);
        if (family == Family::treasure) search.treasure[cursor % 2] = candidate;
        else
        {
            search.nongold_price = std::max(search.nongold_price, candidate.price);
            if (candidate.price > search.best.price) search.best = candidate;
        }
    }
    if (search.cursor < 14 || !permitted()) return;
    for (auto candidate : search.treasure)
    {
        if (!candidate.valid) continue;
        candidate.value += search.before.gold / std::max(1.0f, capital_build(search.build).gold_mult) *
                           tables().magnitude[static_cast<int>(Family::treasure)] * search.vm->horizon_rounds() * 0.5f * search.nongold_price;
        candidate.price = candidate.value / (static_cast<float>(candidate.need) * tables().cost);
        if (candidate.price > search.best.price) search.best = candidate;
    }
    search.complete = true;
}

CapitalPick capital_next_purchase(const Build& b, const cambc::enum_map<item_type>& items, const Workload& workload, const ValueModel& vm,
                                  const CapitalPick* proposal = nullptr, float future_price = 0)
{
    Family family{};
    Stat stat{};
    if (!partial_block(items, family, stat)) return proposal ? *proposal : capital_pick(b, items, workload, vm);
    auto after = b;
    auto counts = items;
    const int need = std::max(1, tables().combine - static_cast<int>(counts[shard(family, stat)]));
    for (int i = 0; i < need; ++i) apply_shard(after, counts, family, stat);
    const auto before_flow = capital::flow(workload.view(), capital_build(b));
    const auto after_flow = capital::flow(workload.view(), capital_build(after));
    float value = capital::bundle_value(before_flow, after_flow, vm.pvp_cost(b) - vm.pvp_cost(after), vm.horizon_rounds());
    if (family == Family::treasure && !b.passive[static_cast<int>(Family::treasure)])
        value += before_flow.gold / std::max(1.0f, capital_build(b).gold_mult) * tables().magnitude[static_cast<int>(Family::treasure)] *
                 vm.horizon_rounds() * 0.5f * future_price;
    return { family, stat, need, value, value / (static_cast<float>(need) * tables().cost), true };
}

int shop_by_capital(Ctx& ctx)
{
    auto& me = cambc::self();
    const auto& t = tables();
    if (me.gold_count() < static_cast<std::uint32_t>(t.cost) || !fuel_ok(60000)) return 0;
    const auto workload = capital_workload(ctx);
    if (!fuel_ok(40000)) return 0;
    auto items = me.item_count();
    auto build = current_build(items);
    const ValueModel vm(build, build.health, ctx.round);
    int bought = 0;
    for (int guard = 0; guard < 4 && fuel_ok(45000); ++guard)
    {
#if capital_forecast
        auto& forecast = capital_forecast_record();
        bool capture_prefix = false;
        CapitalPick pick;
        if (!forecast.captured && static_cast<int>(kit().type) == capital_forecast_kit)
        {
            forecast = {};
            forecast.round = ctx.round;
            forecast.kit = static_cast<int>(kit().type);
            forecast.x = ctx.pos.x;
            forecast.y = ctx.pos.y;
            forecast.hp = ctx.hp;
            forecast.purse = static_cast<int>(me.gold_count());
            forecast.workload = workload;
            forecast.before_build = capital_build(build);
            forecast.before = capital::flow(workload.view(), forecast.before_build);
            forecast.pvp = vm.pvp_cost(build);
            forecast.horizon = vm.horizon_rounds();
            const auto proposal = capital_pick(build, items, workload, vm, &forecast.nongold, &forecast);
            pick = capital_next_purchase(build, items, workload, vm, &proposal, forecast.nongold);
        }
        else pick = capital_next_purchase(build, items, workload, vm);
#else
        const auto pick = capital_next_purchase(build, items, workload, vm);
#endif
        if (!pick.valid) break;
        const auto family = pick.family;
        const auto stat = pick.stat;
        const int need = std::max(1, t.combine - static_cast<int>(items[shard(family, stat)]));
        const int take = std::min(need, static_cast<int>(static_cast<float>(me.gold_count()) / t.cost));
        if (take <= 0 || !me.can_buy(shard(family, stat), static_cast<std::uint32_t>(take))) break;
        me.buy(shard(family, stat), static_cast<std::uint32_t>(take));
#if capital_forecast
        if (!forecast.captured && static_cast<int>(kit().type) == capital_forecast_kit)
        {
            forecast.captured = true;
            forecast.selected_family = static_cast<int>(family);
            forecast.selected_stat = static_cast<int>(stat);
            forecast.bought = take;
            capture_prefix = true;
        }
#endif
        bought += take;
        for (int i = 0; i < take; ++i) apply_shard(build, items, family, stat);
#if capital_forecast
        if (capture_prefix)
        {
            forecast.prefix_build = capital_build(build);
            forecast.prefix_flow = capital::flow(workload.view(), forecast.prefix_build);
            forecast.prefix_survival = forecast.pvp - vm.pvp_cost(build);
            forecast.observed_strength = me.strength();
            forecast.observed_focus = me.focus();
            forecast.observed_health = me.max_health();
        }
#endif
        if constexpr (cfg::capital_trace)
            MB_LOG("capital buy r%d k%d f%d s%d n%d\n", ctx.round, static_cast<int>(kit().type), static_cast<int>(family), static_cast<int>(stat), take);
    }
    return bought;
}

#endif
int shop_by_value(Ctx& ctx)
{
    auto& me = cambc::self();
    const auto& t = tables();
    if (me.gold_count() < static_cast<std::uint32_t>(t.cost) || !fuel_ok(30000)) return 0;
    auto items = me.item_count();
    Build b = current_build(items);
    const ValueModel vm(b, b.health, ctx.round > 0 ? ctx.round : static_cast<int>(cambc::round_number()));
    int bought = 0;
    for (int guard = 0; guard < 40; ++guard)
    {
        if (!fuel_ok(12000)) break;
        Family f{};
        Stat s{};
        if (!partial_block(items, f, s))
        {
            Pick second;
            const Pick p = choose_block(vm, b, items, &second);
            f = p.family;
            s = p.stat;
            if (cfg::debug_shop && fuel_ok(20000))
            {
                char desc[160];
                vm.describe(desc, sizeof desc);
                MB_LOG("shopv r%d k%d f%d s%d v%.0f r%.3f | f%d s%d v%.0f | %s\n", ctx.round, static_cast<int>(kit().type), static_cast<int>(f),
                       static_cast<int>(s), p.value, p.rate, static_cast<int>(second.family), static_cast<int>(second.stat), second.value, desc);
            }
        }
        const auto item = shard(f, s);
        if (!me.can_buy(item, 1)) break;
        me.buy(item, 1);  // can_buy() held; engine errors reach the turn boundary
        ++bought;
        apply_shard(b, items, f, s);
    }
    return bought;
}

// Recall benefit cache: the per-round value of what our gold buys changes only with the gold (in
// shards) and with our items.
struct RecallCache
{
    int shards{ -1 };
    int version{ -1 };
    float rate{};
    float horizon{};
};

int g_items_version = 0;

} // namespace

ShopSignals& shop_signals()
{
    static ShopSignals s;
    return s;
}

#if capital_policy
#if capital_forecast
void capital_forecast_emit()
{
    auto& f = capital_forecast_record();
    if (!f.captured || !fuel_ok(8000)) return;
    if (f.emit >= 2 + f.workload.count + 14)
    {
        auto& r = f.recall;
        if (!r.captured || !r.bound) return;
        if (r.emitted)
        {
            if (f.recall_task_emit >= f.recall_task_count) return;
            const auto& t = f.recall_tasks[static_cast<std::size_t>(f.recall_task_emit++)];
            MB_LOG("forecast recalltask r%d group%d seen%d status%d hp%.3f committed%d capturers%d current%d spawn%d p%d,%d gems%.3f gold%.3f aftergems%.6f aftergold%.6f fight%.3f damage%.3f\n",
                   r.round, t.group, t.seen, t.status, t.hp, t.committed, t.capturers, t.from_current, t.from_spawn,
                   t.x, t.y, t.gems, t.gold, t.after_gems, t.after_gold, t.fight, t.damage);
            return;
        }
        r.emitted = true;
        MB_LOG("forecast recall r%d cache%d gold%d cachepurse%d version%d cacheversion%d hp%.3f maxhp%.3f bundle%.3f before%.6f after%.6f survival%.6f advance%.6f heal%.6f benefit%.6f target%d extra%d callerbenefit%.6f shopping%d\n",
               r.round, r.cache_round, r.purse, r.cache_purse, r.version, r.cache_version, r.hp, r.max_hp, r.bundle,
               r.before, r.after, r.survival, r.advance, r.heal, r.benefit, r.target, r.extra, r.caller_benefit, r.shopping);
        return;
    }
    const int at = f.emit++;
    if (at == 0)
    {
        const auto& b = f.before_build;
        MB_LOG("forecast state r%d k%d p%d,%d hp%.1f gold%d str%.3f foc%.3f health%.3f camp%.4f taken%.4f goldmult%.4f recall%.2f share%.3f safety%.3f trips%.2f horizon%.3f gems%.6f flowgold%.6f tasks%d pvp%.6f eligible%u evaluated%u chosen%d,%d bought%d nongold%.6f\n",
               f.round, f.kit, f.x, f.y, f.hp, f.purse, b.strength, b.focus, b.health, b.camp_damage, b.damage_taken,
               b.gold_mult, b.recall, b.usable_share, b.safety, b.max_trips, f.horizon, f.before.gems, f.before.gold,
               f.before.tasks, f.pvp, f.eligible, f.evaluated, f.selected_family, f.selected_stat, f.bought, f.nongold);
    }
    else if (at == 1)
    {
        const auto& b = f.prefix_build;
        MB_LOG("forecast prefix r%d bought%d str%.3f foc%.3f health%.3f camp%.4f taken%.4f goldmult%.4f gems%.6f gold%.6f tasks%d survival%.6f observedstr%.3f observedfoc%.3f observedhealth%.3f\n",
               f.round, f.bought, b.strength, b.focus, b.health, b.camp_damage, b.damage_taken, b.gold_mult,
               f.prefix_flow.gems, f.prefix_flow.gold, f.prefix_flow.tasks, f.prefix_survival,
               f.observed_strength, f.observed_focus, f.observed_health);
    }
    else if (at <= 1 + f.workload.count)
    {
        const int index = at - 2;
        const auto& w = f.workload.tasks[static_cast<std::size_t>(index)];
        MB_LOG("forecast work r%d i%d group%d hp%.3f weighted%.3f strength%.3f gems%.3f gold%.3f strcoef%.6f foccoef%.6f travel%.3f back%.3f capture%.3f respawn%.3f\n",
               f.round, index, f.workload.groups[static_cast<std::size_t>(index)], w.hp, w.weighted_hp, w.strength,
               w.gems, w.gold, w.camp_str, w.camp_focus, w.travel, w.walk_back, w.capture, w.respawn);
    }
    else
    {
        const int index = at - 2 - f.workload.count;
        const auto& c = f.candidates[static_cast<std::size_t>(index)];
        MB_LOG("forecast candidate r%d i%d valid%d f%d s%d need%d str%.3f foc%.3f health%.3f camp%.4f taken%.4f goldmult%.4f gems%.6f gold%.6f tasks%d survival%.6f value%.6f price%.6f\n",
               f.round, index, c.pick.valid, static_cast<int>(c.pick.family), static_cast<int>(c.pick.stat), c.pick.need,
               c.build.strength, c.build.focus, c.build.health, c.build.camp_damage, c.build.damage_taken, c.build.gold_mult,
               c.after.gems, c.after.gold, c.after.tasks, c.survival, c.pick.value, c.pick.price);
    }
}
#endif

void capital_refresh(const Ctx& ctx, int productive_target)
{
    if constexpr (!capital_policy) return;
    auto& cache = capital_cache();
    // Separate bounded geometry and candidate generations from actions. Each resumes its
    // cursor, freezes the model's item inputs, and creates no distance fields.
    if (!fuel_ok(8000)) return;
    capital_work_step(ctx, productive_target);
    auto& geometry = capital_work_generation();
    if (!geometry.complete || geometry.workload.count == 0) return;
    auto& world = World::get();
    for (int i = 0; i < geometry.workload.count; ++i)
    {
        const auto* field = world.group_field(geometry.workload.groups[static_cast<std::size_t>(i)], false);
        if (!field || field->get(ctx.route_pos) >= (1 << 14) || field->get(world.ally_spawn_center) >= (1 << 14))
        {
            geometry.round = -1000;
            return;
        }
    }
    capital_search_step(ctx, geometry);
    const auto& search = capital_search();
    if (!search.complete || search.item_version != g_items_version || !search.best.valid || !fuel_ok(8000)) return;
    const int purse = static_cast<int>(static_cast<float>(ctx.gold) / tables().cost);
    if (cache.ready && ctx.round - cache.round < 20 && purse == cache.gold_shards && ctx.gems == cache.gems && cache.item_version == g_items_version) return;
    const auto& workload = geometry.workload;
    const auto& items = search.items;
    const auto& build = search.build;
    const auto& vm = *search.vm;
    const auto before = search.before;
    const auto pick = capital_next_purchase(build, items, workload, vm, &search.best, search.nongold_price);
    if (!pick.valid) return;
    auto affordable = build;
    auto counts = items;
    const int take = std::min(pick.need, static_cast<int>(static_cast<float>(ctx.gold) / tables().cost));
    for (int i = 0; i < take; ++i) apply_shard(affordable, counts, pick.family, pick.stat);
    const auto after = capital::flow(workload.view(), capital_build(affordable));
    const auto& sig = shop_signals();
    const float sample = static_cast<float>(sig.alive_rounds) + cfg::shop_prior_rounds;
    const float damage = std::max(0.5f, (sig.camp_damage + sig.pvp_damage + cfg::shop_prior_damage * cfg::shop_prior_rounds) / sample);
    const float spare = std::max(0.0f, ctx.hp - cfg::recall_health_frac * ctx.max_hp);
    const float gap = (static_cast<float>(sig.alive_rounds) + cfg::shop_prior_gap) / static_cast<float>(sig.spawn_visits + 1);
    const float horizon = std::max(0.0f, vm.horizon_rounds() - static_cast<float>(ctx.round - search.round));
    const float advance = std::min({ spare / damage, gap, horizon, 300.0f });
    const float trip = static_cast<float>(recall_turns()) + 1.0f + tables().walk_back;
    const float heal = trip * std::clamp((ctx.max_hp - ctx.hp) / std::max(1.0f, ctx.max_hp * (1.0f - cfg::recall_health_frac)), 0.0f, 1.0f);
    cache.benefit = take > 0 ? capital::shop_benefit(before, after, vm.pvp_cost(build) - vm.pvp_cost(affordable), advance, heal) : heal;
    cache.price = std::clamp(pick.price, 0.005f, 4.0f);
    cache.gem_flow = std::max(0.01f, before.gems);
    cache.bank_credit = 0;
    if (kit().stun_immune)
    {
        const float income = static_cast<float>(cambc::constants::passive_abilities::marcella_tribute_gold_per_gem) * static_cast<float>(ctx.gems);
        const float bundle_cost = static_cast<float>(pick.need) * tables().cost;
        // Marcella's early bank matters when it unlocks this concrete next investment. Price only
        // the advance before a natural shop visit, not the bundle's entire remaining lifetime.
        if (static_cast<float>(ctx.gold) < bundle_cost && static_cast<float>(ctx.gold) + income >= bundle_cost)
            cache.bank_credit = income * cache.price / cache.gem_flow * advance / std::max(1.0f, horizon);
    }
    cache.ready = true;
    cache.round = ctx.round;
    cache.gold_shards = purse;
    cache.item_version = g_items_version;
    cache.gems = ctx.gems;
    cache.before = before;
    cache.after = after;
    cache.survival = vm.pvp_cost(build) - vm.pvp_cost(affordable);
    cache.horizon = horizon;
    cache.bundle_cost = static_cast<float>(pick.need) * tables().cost;
#if capital_forecast
    cache.work_epoch = geometry.epoch;
    cache.after_build = capital_build(affordable);
#endif
    if constexpr (cfg::capital_trace)
        MB_LOG("capital model r%d k%d price%.4f gems%.3f tasks%d benefit%.2f bank%.2f f%d s%d\n", ctx.round, static_cast<int>(kit().type),
               cache.price, cache.gem_flow, before.tasks, cache.benefit, cache.bank_credit, static_cast<int>(pick.family), static_cast<int>(pick.stat));
}

#if capital_episode
int capital_episode_scout(const Ctx& ctx, int gold_target)
{
    struct Scout { int gold{-1}, gem{-1}, attempted{-1}, left{}, start{}; bool active{}; };
    static Scout state;
    auto& world = World::get();
    auto& knowledge = Knowledge::get();
    if (state.gold != gold_target) { state = {}; state.gold = gold_target; }
    if (gold_target < 0 || !fuel_ok(34000) || ctx.on_spawn || !ctx.enemies.empty() || ctx.n_unseen > 0 || ctx.hit_unseen) return -1;
    const auto& gold = world.groups[static_cast<std::size_t>(gold_target)];
    const auto& belief = knowledge.groups[static_cast<std::size_t>(gold_target)];
    const bool fresh_gold = belief.seen_round == ctx.round && belief.status == GroupStatus::alive;
    const auto nearby_ally = [&](const GroupInfo& group)
    {
        for (const auto& ally : ctx.allies) if (cheb(group.center, ally.pos) <= group.max_range + 3) return true;
        return false;
    };
    if (state.active && (!fresh_gold || knowledge.committed(gold_target) > 0 || nearby_ally(gold) || state.left-- <= 0))
    {
        MB_LOG("episode scoutabort r%d G%d J%d elapsed%d reason%d\n", ctx.round, gold_target, state.gem, ctx.round - state.start,
               !fresh_gold ? 1 : knowledge.committed(gold_target) > 0 ? 2 : nearby_ally(gold) ? 3 : 4);
        state.active = false;
        return -1;
    }
    if (state.active)
    {
        const auto& gem = world.groups[static_cast<std::size_t>(state.gem)];
        const auto& observed = knowledge.groups[static_cast<std::size_t>(state.gem)];
        if (knowledge.committed(state.gem) > 0 || nearby_ally(gem))
        {
            MB_LOG("episode scoutabort r%d G%d J%d elapsed%d reason5\n", ctx.round, gold_target, state.gem, ctx.round - state.start);
            state.active = false;
            return -1;
        }
        if (observed.seen_round == ctx.round)
        {
            MB_LOG("episode scoutobserved r%d G%d J%d elapsed%d gs%d js%d hp%.1f claims%d p%d,%d\n", ctx.round, gold_target, state.gem,
                   ctx.round - state.start, static_cast<int>(belief.status), static_cast<int>(observed.status), observed.hp_left,
                   knowledge.committed(state.gem), ctx.pos.x, ctx.pos.y);
            // Gate-only assay: no recall is authorized until a complete finite appraisal exists.
            state.active = false;
            return -1;
        }
        return state.gem;
    }
    if (!fresh_gold || gold.gems != 0 || gold.gold == 0 || knowledge.committed(gold_target) > 0 || nearby_ally(gold) || state.attempted == gold_target) return -1;
    const auto items = cambc::self().item_count();
    Family family{};
    Stat stat{};
    const int need = partial_block(items, family, stat) ? tables().combine - static_cast<int>(items[shard(family, stat)]) : tables().combine;
    if (static_cast<float>(ctx.gold) < static_cast<float>(need) * tables().cost) return -1;
    int selected = -1, best = 1 << 14;
    const auto scan_start = fuel();
    for (std::size_t i = 0; i < world.groups.size(); ++i)
    {
        if (!capital::route_budget(scan_start, fuel(), cfg::fuel_phase_farm + cfg::fuel_reserve + 4000, 4000)) return -1;
        const auto& gem = world.groups[i];
        if (gem.gems == 0 || gem.has_monolith || cheb(gold.center, gem.center) > 2 * (kit().vision - 1) || knowledge.committed(static_cast<int>(i)) > 0 || nearby_ally(gem)) continue;
        const auto* field = world.group_field(static_cast<int>(i), false);
        if (!field || field->get(world.ally_spawn_center) >= (1 << 14)) continue;
        const auto& seen = knowledge.groups[i];
        if (seen.seen_round == ctx.round && seen.status != GroupStatus::alive) continue;
        const int distance = field->get(ctx.route_pos);
        if (distance < best) { best = distance; selected = static_cast<int>(i); }
    }
    state.attempted = gold_target;
    if (selected < 0) return -1;
    state.gem = selected;
    state.start = ctx.round;
    state.left = best;
    state.active = true;
    MB_LOG("episode scoutstart r%d G%d J%d gold%u hp%.1f steps%d p%d,%d\n", ctx.round, gold_target, selected, ctx.gold, ctx.hp, best, ctx.pos.x, ctx.pos.y);
    if (knowledge.groups[static_cast<std::size_t>(selected)].seen_round == ctx.round)
    {
        const auto& observed = knowledge.groups[static_cast<std::size_t>(selected)];
        MB_LOG("episode scoutobserved r%d G%d J%d elapsed0 gs%d js%d hp%.1f claims%d p%d,%d\n", ctx.round, gold_target, selected,
               static_cast<int>(belief.status), static_cast<int>(observed.status), observed.hp_left, knowledge.committed(selected), ctx.pos.x, ctx.pos.y);
        state.active = false;
        return -1;
    }
    return selected;
}
#endif

float capital_gold_price() { return capital_cache().price; }
float capital_gem_flow() { return capital_cache().gem_flow; }

bool capital_credit_current(const Ctx& ctx)
{
    const auto& cache = capital_cache();
    return capital::credit_current({cache.round, cache.item_version, cache.gold_shards, cache.ready}, ctx.round, g_items_version,
                                   static_cast<int>(static_cast<float>(ctx.gold) / tables().cost));
}

float capital_advance(const Ctx& ctx)
{
    const auto& cache = capital_cache();
    const auto& sig = shop_signals();
    const float sample = static_cast<float>(sig.alive_rounds) + cfg::shop_prior_rounds;
    const float damage = std::max(0.5f, (sig.camp_damage + sig.pvp_damage + cfg::shop_prior_damage * cfg::shop_prior_rounds) / sample);
    const float spare = std::max(0.0f, ctx.hp - cfg::recall_health_frac * ctx.max_hp);
    const float gap = (static_cast<float>(sig.alive_rounds) + cfg::shop_prior_gap) / static_cast<float>(sig.spawn_visits + 1);
    return std::max(0.0f, std::min({ spare / damage, gap, cache.horizon - static_cast<float>(ctx.round - cache.round), 300.0f }));
}

float capital_bank_credit(const Ctx& ctx)
{
    if (!kit().stun_immune || !capital_credit_current(ctx)) return 0;
    const auto& cache = capital_cache();
    const float income = static_cast<float>(cambc::constants::passive_abilities::marcella_tribute_gold_per_gem) * static_cast<float>(ctx.gems);
    if (static_cast<float>(ctx.gold) >= cache.bundle_cost || static_cast<float>(ctx.gold) + income < cache.bundle_cost) return 0;
    return income * cache.price / cache.gem_flow * capital_advance(ctx) / std::max(1.0f, cache.horizon - static_cast<float>(ctx.round - cache.round));
}

float capital_recall_credit(const Ctx& ctx)
{
    if (!capital_credit_current(ctx)) return 0;
    const auto& cache = capital_cache();
    const float trip = static_cast<float>(recall_turns()) + 1.0f + tables().walk_back;
    const float heal = trip * std::clamp((ctx.max_hp - ctx.hp) / std::max(1.0f, ctx.max_hp * (1.0f - cfg::recall_health_frac)), 0.0f, 1.0f);
#if capital_forecast
    const float advance = capital_advance(ctx);
    const float benefit = capital::shop_benefit(cache.before, cache.after, cache.survival, advance, heal);
    auto& f = capital_forecast_record();
    if (f.captured && !f.recall.captured && static_cast<int>(kit().type) == capital_forecast_kit &&
        cache.bundle_cost > 0 && static_cast<float>(ctx.gold) >= cache.bundle_cost &&
        cache.before.gems <= 0.0001f && cache.after.gems > 0.0001f)
    {
        auto& r = f.recall;
        r.captured = true;
        r.round = ctx.round;
        r.cache_round = cache.round;
        r.purse = static_cast<int>(ctx.gold);
        r.cache_purse = cache.gold_shards;
        r.version = g_items_version;
        r.cache_version = cache.item_version;
        r.hp = ctx.hp;
        r.max_hp = ctx.max_hp;
        r.bundle = cache.bundle_cost;
        r.before = cache.before.gems;
        r.after = cache.after.gems;
        r.survival = cache.survival;
        r.advance = advance;
        r.heal = heal;
        r.benefit = benefit;
        const auto& geometry = capital_work_generation();
        if (geometry.epoch == cache.work_epoch)
            for (int i = 0; i < geometry.workload.count; ++i)
            {
                const int group = geometry.workload.groups[static_cast<std::size_t>(i)];
                const auto& task = geometry.workload.tasks[static_cast<std::size_t>(i)];
                const auto& belief = Knowledge::get().groups[static_cast<std::size_t>(group)];
                auto& world = World::get();
                const auto& info = world.groups[static_cast<std::size_t>(group)];
                const auto* field = world.group_field(group, false);
                const auto projection = capital::flow(std::span{&task, 1}, cache.after_build);
                const float dps = std::max(1.0f, (task.camp_str * cache.after_build.strength + task.camp_focus * cache.after_build.focus) * cache.after_build.camp_damage);
                auto& saved = f.recall_tasks[static_cast<std::size_t>(f.recall_task_count++)];
                saved = {group, belief.seen_round, static_cast<int>(belief.status), Knowledge::get().committed(group), Knowledge::get().capturers(group),
                         field ? static_cast<int>(field->get(ctx.route_pos)) : -1,
                         field ? static_cast<int>(field->get(world.ally_spawn_center)) : -1, info.center.x, info.center.y,
                         belief.hp_left, task.gems, task.gold, projection.gems, projection.gold,
                         std::max(0.0f, task.weighted_hp / dps - task.strength * 0.5f) * cache.after_build.damage_taken, std::ceil(task.hp / dps)};
            }
    }
    return benefit;
#else
    return capital::shop_benefit(cache.before, cache.after, cache.survival, capital_advance(ctx), heal);
#endif
}

#if capital_forecast
void capital_forecast_recall_caller(const Ctx& ctx, int target, int extra, float benefit, bool shopping)
{
    auto& r = capital_forecast_record().recall;
    if (!r.captured || r.bound || r.round != ctx.round) return;
    r.bound = true;
    r.target = target;
    r.extra = extra;
    r.caller_benefit = benefit;
    r.shopping = shopping;
}
#endif

#endif
float shop_recall_benefit(const Ctx& ctx)
{
#if capital_policy
    return capital_recall_credit(ctx);
#endif
    static RecallCache cache;
    const auto& t = tables();
    const int shards = static_cast<int>(static_cast<float>(ctx.gold) / t.cost);
    if (shards <= 0 || ctx.max_hp <= 0.0f) return 0.0f;
    if (shards != cache.shards || g_items_version != cache.version)
    {
        if (!fuel_ok(40000)) return 0.0f;
        auto items = cambc::self().item_count();
        Build b = current_build(items);
        const ValueModel vm(b, ctx.max_hp, ctx.round);
        // Spend the purse as the shop would: finish a started block, then the best blocks in turn.
        const float before = vm.time_cost(b);
        float flow = 0.0f;  // Treasure's gold flow per round of owning it earlier
        int left = shards;
        for (int guard = 0; guard < 6 && left > 0 && fuel_ok(25000); ++guard)
        {
            Family f{};
            Stat s{};
            const bool started = partial_block(items, f, s);
            const bool treasure_next = !started || f == Family::treasure;
            Pick p;
            // The shop's own choice (it also prices Treasure's gold flow).
            if (treasure_next) p = cfg::shop_plan == 100 ? choose_block(vm, b, items, nullptr) : vm.best(b);
            if (!started)
            {
                f = p.family;
                s = p.stat;
            }
            const int need = t.combine - static_cast<int>(items[shard(f, s)]);
            const int take = std::min(need, left);
            // Treasure's gold flow starts only with the completed emblem.
            if (f == Family::treasure && take == need) flow += p.flow;
            for (int k = 0; k < take; ++k) apply_shard(b, items, f, s);
            left -= take;
        }
        cache.rate = std::max(0.0f, before - vm.time_cost(b)) + flow;
        cache.horizon = vm.horizon_rounds();
        cache.shards = shards;
        cache.version = g_items_version;
    }
    const auto& sig = shop_signals();
    // Rounds until the next natural spawn visit: the heal recall once our health above the recall
    // threshold is spent at our measured damage rate, and at most the average gap between our visits
    // (heal and transport recalls, deaths).
    const float a = static_cast<float>(sig.alive_rounds) + cfg::shop_prior_rounds;
    const float trip = static_cast<float>(recall_turns()) + 1.0f + t.walk_back;
    const float pool = std::max(50.0f, (1.0f - cfg::recall_health_frac) * ctx.max_hp);
    const float damage_rate = std::max(0.5f, (sig.camp_damage + sig.pvp_damage + cfg::shop_prior_damage * cfg::shop_prior_rounds) / a);
    const float spare = std::max(0.0f, ctx.hp - cfg::recall_health_frac * ctx.max_hp);
    const float gap = (static_cast<float>(sig.alive_rounds) + cfg::shop_prior_gap) / static_cast<float>(sig.spawn_visits + 1);
    const float wait = std::min({ spare / damage_rate, gap, cache.horizon, 300.0f });
    // Healing now replaces part of a later heal trip.
    const float heal = trip * std::clamp((ctx.max_hp - ctx.hp) / pool, 0.0f, 1.0f);
    return cache.rate * wait + heal;
}

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
    // A teleport crystal in stock for terrain shortcuts (10 gold; cfg::tp_shortcut_margin).
    if (cfg::tp_shortcut_margin > 0 && fuel_ok(3000))
        try
        {
            if (me.consumables()[cambc::consumable_type::tp_crystal].count() == 0 && me.can_buy(cambc::consumable_type::tp_crystal))
                me.buy(cambc::consumable_type::tp_crystal);
        }
        catch (const std::exception& e)
        {
            MB_LOG("crystal buy failed: %s\n", e.what());
        }
    // Shop plan 100: the state-driven shop chooses the items.
#if capital_policy
    bought = shop_by_capital(ctx);
#else
    if constexpr (cfg::shop_plan == 100)
        bought = shop_by_value(ctx);
    else
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
#endif
    // An altar lock with the change (10 gold; cfg::lock_mode), after the items.
    if (cfg::lock_mode > 0 && fuel_ok(3000))
        try
        {
            if (me.consumables()[cambc::consumable_type::altar_lock].count() == 0 && me.can_buy(cambc::consumable_type::altar_lock))
                me.buy(cambc::consumable_type::altar_lock);
        }
        catch (const std::exception& e)
        {
            MB_LOG("lock buy failed: %s\n", e.what());
        }
    if (bought)
    {
        if constexpr (cfg::shop_plan == 100 || cfg::shop_recall_mode > 0) ++g_items_version;
        ctx.gold = me.gold_count();
        ctx.camp_damage_taken_mult = damage_taken_multiplier(true);
    }
    return bought;
}

} // namespace mb
