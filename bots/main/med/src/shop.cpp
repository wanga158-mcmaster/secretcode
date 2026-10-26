#include "shop.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include "config.h"
#include "knowledge.h"
#include "learn.h"
#include "world.h"

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

}  // namespace

int emblems_owned()
{
    const auto items = cambc::self().item_count();
    int n = 0;
    for (int f = 0; f < 7; ++f)
        for (int s = 0; s < 3; ++s) n += static_cast<int>(items[emblem(static_cast<Family>(f), static_cast<Stat>(s))]);
    return n;
}

namespace {

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
    // Plan 15 (D330): plan 7 with Jungle before Treasure (economy model E7/E20: one Jungle emblem
    // turns a solo elk from 2-3 trips into one; Treasure first costs ~13 rounds).
    const bool bruiser = cfg::shop_plan == 7 || cfg::shop_plan == 12 || cfg::shop_plan == 13 || cfg::shop_plan == 15;
    if (cfg::shop_plan == 5 || (bruiser && cfg::shop_plan != 15)) plan.push_back({ Family::treasure, main, 5 });  // +20% gold first
    if (cfg::shop_plan == 12) plan.push_back({ Family::home, main, 5 });
    plan.push_back({ Family::jungle, main, 5 });
    if (cfg::shop_plan == 6 || cfg::shop_plan == 15) plan.push_back({ Family::treasure, main, 5 });  // +20% gold second
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
    else b.health += g;
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

float shop_recall_benefit(const Ctx& ctx, std::uint32_t extra_gold)
{
    static RecallCache cache;
    const auto& t = tables();
    const int shards = static_cast<int>(static_cast<float>(ctx.gold + extra_gold) / t.cost);
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

namespace {

// cfg::learned (D340): the plan's steps move by their family's and stat's learned weights (in places;
// zero weights keep the plan). Five-shard steps of the families and stats the plan leaves out follow
// its last step, which is unlimited, so they are reached only when weights move them before it.
std::vector<Step> learned_plan(const Kit& k)
{
    auto plan = plan_for(k);
    const Stat main = main_stat(k);
    for (int f = 0; f <= static_cast<int>(Family::bruiser); ++f)
        for (const Stat s : { main, Stat::health })
        {
            bool present = false;
            for (const auto& step : plan) present = present || (step.family == static_cast<Family>(f) && step.stat == s);
            if (!present) plan.push_back({ static_cast<Family>(f), s, 5 });
        }
    std::vector<std::pair<float, Step>> scored;
    for (std::size_t i = 0; i < plan.size(); ++i)
        scored.push_back({ learn::shop_weight(static_cast<int>(plan[i].family)) + learn::shop_weight(7 + static_cast<int>(plan[i].stat)) -
                               static_cast<float>(i),
                           plan[i] });
    std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (std::size_t i = 0; i < plan.size(); ++i) plan[i] = scored[i].second;
    return plan;
}

} // namespace

int shop(Ctx& ctx)
{
    auto& me = cambc::self();
    static const std::vector<Step> plan = cfg::learned > 0 ? learned_plan(kit()) : plan_for(kit());
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
