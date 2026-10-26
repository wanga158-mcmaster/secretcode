#include "cand.h"
#include <algorithm>
#include <array>
#include <cmath>
#include "catalog.h"
#include "rules.h"
#include <cstdio>
#if defined(__wasm__)
#include <wasm_simd128.h>
#endif

namespace zero {

std::uint64_t (*fuel_hook)() = nullptr;
bool goal_state_facts = false;
std::uint64_t goal_fuel_floor = 0;

namespace prof {
bool on = false;
namespace {
std::uint64_t last{}, mx[kSlots]{}, sum[kSlots]{};
std::uint32_t cnt[kSlots]{};
}  // namespace
void begin() { if (on && fuel_hook) last = fuel_hook(); }
void mark_on(int slot)
{
    if (!fuel_hook || slot < 0 || slot >= kSlots) return;
    const auto now = fuel_hook();
    const auto d = last > now ? last - now : 0;
    if (d > mx[slot]) mx[slot] = d;
    sum[slot] += d;
    ++cnt[slot];
    last = fuel_hook();
}
void report(int round)
{
    if (!on) return;
    std::printf("prof r%d", round);
    for (int i = 0; i < kSlots; ++i)
        if (cnt[i]) std::printf(" %d:%llu/%llu", i, static_cast<unsigned long long>(mx[i]), static_cast<unsigned long long>(sum[i] / cnt[i]));
    std::printf("\n");
    for (int i = 0; i < kSlots; ++i) mx[i] = sum[i] = cnt[i] = 0;
}
}  // namespace prof

namespace {


// Normalisation scales (representation only: they keep inputs near unit range).
constexpr float kRounds = 40.0f, kGems = 100.0f, kGold = 300.0f, kHp = 2000.0f, kDist = 20.0f;

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct Self
{
    rules::DamageRates rate;
    float camp_mult{}, camp_taken{}, champ_taken{}, gold_mult{};
};

// Per-turn facts shared by every candidate function (computed once per observation state; no allocation in the
// per-tile functions below).
struct Facts
{
    const Obs* obs{};
    std::uint64_t revision{};
    int round{ -1 };
    Tile at{};
    float hp{};
    std::uint32_t gold{};
    bool can_attack{}, can_move{};
    Self s;
    int ne{};
    Tile e_at[16];
    float e_burst[16]{}, e_sustained[16]{};
    int e_reach[16]{};
    int nc{};
    Tile c_at[32];
    int c_range[32]{};
    float c_str[32]{};
    int na{};
    Tile a_at[16];
    int a_id[16]{};
};

Facts& facts(const Obs& o)
{
    static Facts f;
    const auto revision = o.command_facts ? o.revision : 0;
    if (f.obs == &o && f.revision == revision && f.round == o.round && f.at == o.self.at && f.hp == o.self.hp && f.gold == o.self.gold &&
        f.can_attack == o.can_attack && f.can_move == o.can_move)
        return f;
    f.obs = &o;
    f.revision = revision;
    f.round = o.round;
    f.at = o.self.at;
    f.hp = o.self.hp;
    f.gold = o.self.gold;
    f.can_attack = o.can_attack;
    f.can_move = o.can_move;
    // Item-derived multipliers change only with the items: cached on the item counts.
    static std::array<std::uint8_t, kMaxItems> items_key{};
    static int type_key = -1;
    static float camp_mult = 1.0f, camp_taken = 1.0f, champ_taken = 1.0f, gold_mult = 1.0f;
    if (items_key != o.items || type_key != o.self.type)
    {
        items_key = o.items;
        type_key = o.self.type;
        camp_mult = rules::camp_damage_dealt_multiplier(o.self.type, o.items);
        camp_taken = rules::camp_damage_taken_multiplier(o.items);
        champ_taken = rules::champion_damage_taken_multiplier(o.items);
        gold_mult = rules::gold_multiplier(o.self.type, o.items);
    }
    f.s.rate = rules::rates(o.self.type, o.self.strength, o.self.focus, o.self.cooldown, false);
    f.s.camp_mult = camp_mult;
    f.s.camp_taken = camp_taken;
    f.s.champ_taken = champ_taken;
    f.s.gold_mult = gold_mult;
    f.ne = 0;
    for (const auto& e : o.enemies)
    {
        if (f.ne >= 16) break;
        const auto r = rules::rates(e.type, e.strength, e.focus, e.cooldown, true);
        f.e_at[f.ne] = e.at;
        f.e_burst[f.ne] = r.burst;
        f.e_sustained[f.ne] = r.sustained;
        f.e_reach[f.ne] = r.reach;
        ++f.ne;
    }
    const auto& cat = catalog();
    f.nc = 0;
    for (const auto& c : o.camps)
    {
        if (!c.awake || f.nc >= 32) continue;
        const auto& cd = cat.camp(c.type);
        if (cd.strength <= 0.0f) continue;
        f.c_at[f.nc] = c.at;
        f.c_range[f.nc] = static_cast<int>(cd.attack_range);
        f.c_str[f.nc] = cd.strength;
        ++f.nc;
    }
    f.na = 0;
    for (const auto& a : o.allies)
    {
        if (f.na >= 16) break;
        f.a_at[f.na] = a.at;
        f.a_id[f.na] = a.id;
        ++f.na;
    }
    return f;
}

Self self_facts(const Obs& o) { return facts(o).s; }

// Every fact stays inside the decision-log codec's range [-4, 4] (facts with a wider natural range are rescaled
// where they are made; this bounds the rest, e.g. long path distances / 40).
inline float bound4(float v) { return v < -4.0f ? -4.0f : (v > 4.0f ? 4.0f : v); }
#if defined(__wasm__)
__attribute__((target("simd128"))) void bound_values(float* x, int n)  // n a multiple of 4
{
    const v128_t lo = wasm_f32x4_splat(-4.0f), hi = wasm_f32x4_splat(4.0f);
    for (int k = 0; k < n; k += 4)
    {
        const v128_t v = wasm_v128_load(x + k);
        // Lane-wise v < -4 ? -4 : (v > 4 ? 4 : v), as bound4.
        const v128_t up = wasm_v128_bitselect(hi, v, wasm_f32x4_gt(v, hi));
        wasm_v128_store(x + k, wasm_v128_bitselect(lo, up, wasm_f32x4_lt(v, lo)));
    }
}
#else
void bound_values(float* x, int n) { for (int k = 0; k < n; ++k) x[k] = bound4(x[k]); }
#endif
void bound_facts(CandSet& out)
{
    const int phi = kPhi[out.head];
    for (int c = 0; c < out.n; ++c) bound_values(out.c[c].phi, phi);
}

bool goal_out_of_fuel() { return goal_fuel_floor && fuel_hook && fuel_hook() < goal_fuel_floor; }

// Enemy damage that can reach `t` next round (each visible enemy's burst if `t` is within its reach).
float enemy_damage_at(const Facts& f, Tile t, float taken)
{
    float d = 0.0f;
    for (int i = 0; i < f.ne; ++i)
        if (cheb(f.e_at[i], t) <= f.e_reach[i]) d += f.e_burst[i];
    return d * taken;
}

// Monster strikes `t` would draw next round: awake camps in range whose nearest champion would be us at `t`
// (ties to the lower actor id).
float camp_strikes_at(const Facts& f, int self_id, Tile t, float taken)
{
    float d = 0.0f;
    for (int c = 0; c < f.nc; ++c)
    {
        const int mine = cheb(f.c_at[c], t);
        if (mine > f.c_range[c]) continue;
        bool us = true;
        for (int a = 0; a < f.na && us; ++a)
        {
            const int da = cheb(f.c_at[c], f.a_at[a]);
            if (da < mine || (da == mine && f.a_id[a] < self_id)) us = false;
        }
        if (us) d += f.c_str[c];
    }
    return d * taken;
}

float enemy_damage_at(const Obs& o, Tile t, float taken) { return enemy_damage_at(facts(o), t, taken); }
float camp_strikes_at(const Obs& o, Tile t, float taken) { return camp_strikes_at(facts(o), o.self.id, t, taken); }

int nearest_enemy(const Obs& o, Tile t)
{
    int n = 99;
    for (const auto& e : o.enemies) n = std::min(n, cheb(e.at, t));
    return n;
}

bool occupied(const Obs& o, Tile t)
{
    for (const auto& u : o.allies) if (u.at == t) return true;
    for (const auto& u : o.enemies) if (u.at == t) return true;
    for (const auto& c : o.camps) if (c.at == t) return true;
    return false;
}

const UnitView* enemy_by_id(const Obs& o, int id)
{
    for (const auto& e : o.enemies) if (e.id == id) return &e;
    return nullptr;
}

const UnitView* ally_by_id(const Obs& o, int id)
{
    for (const auto& a : o.allies) if (a.id == id) return &a;
    return nullptr;
}

// Distance from `t` to the current goal's target (path where a field exists, else Chebyshev).
int goal_distance(const Context& ctx, Tile t)
{
    const auto& o = *ctx.obs;
    const auto& m = *ctx.mem;
    switch (m.goal_kind)
    {
    case g_farm: return m.goal_target >= 0 ? ctx.paths->to_group(m.goal_target, t) : 0;
    case g_bank: return m.goal_target >= 0 ? ctx.paths->to_altar(m.goal_target, t) : 0;
    case g_hold: return ctx.paths->to_spawn(t);
    case g_engage:
        if (const auto* e = enemy_by_id(o, m.goal_target)) return cheb(t, e->at);
        return 0;
    case g_assist:
        if (const auto* a = ally_by_id(o, m.goal_target)) return cheb(t, a->at);
        return 0;
    case g_avoid: return -nearest_enemy(o, t);
    default: return 0;
    }
}

bool goal_target_at(const Context& ctx, Tile target)
{
    const auto& m = *ctx.mem;
    if (m.goal_kind == g_farm && m.goal_target >= 0)
        return ctx.map->group_of[static_cast<std::size_t>(ctx.map->idx(target))] == m.goal_target;
    if (m.goal_kind == g_engage)
        if (const auto* e = enemy_by_id(*ctx.obs, m.goal_target)) return e->at == target;
    return false;
}

}  // namespace

static void state_vector_impl(const Context& ctx, float* x)
{
    const auto& o = *ctx.obs;
    const auto& m = *ctx.mem;
    const auto s = self_facts(o);
    // Tempo: banked rates over the oldest history sample.
    float rate_us = 0.0f, rate_them = 0.0f;
    for (int k = 0; k < 6; ++k)
    {
        const int i = (m.hist_head + k) % 6;
        if (m.hist_round[static_cast<std::size_t>(i)] >= 0 && m.hist_round[static_cast<std::size_t>(i)] < o.round)
        {
            const float dt = static_cast<float>(o.round - m.hist_round[static_cast<std::size_t>(i)]);
            rate_us = (static_cast<float>(o.banked_us) - static_cast<float>(m.banked_us_hist[static_cast<std::size_t>(i)])) / dt;
            rate_them = (static_cast<float>(o.banked_them) - static_cast<float>(m.banked_them_hist[static_cast<std::size_t>(i)])) / dt;
            break;
        }
    }
    float hp8 = o.self.hp, eh8 = 0.0f, enemy_gems = 0.0f;
    for (const auto& a : o.allies) if (cheb(a.at, o.self.at) <= 8) hp8 += a.hp;
    for (const auto& e : o.enemies) { if (cheb(e.at, o.self.at) <= 8) eh8 += e.hp; enemy_gems += static_cast<float>(e.gems); }
    int altar = kUnreachable;
    for (std::size_t a = 0; a < ctx.map->altars.size(); ++a) altar = std::min(altar, cheb(ctx.map->altars[a], o.self.at));
    // Item facts change only with the items: cached on them.
    static std::array<std::uint8_t, kMaxItems> items_key{};
    static bool items_known = false;
    static int fam_items[rules::fam_count]{};
    static int emblem_count = 0;
    if (!items_known || items_key != o.items)
    {
        items_known = true;
        items_key = o.items;
        for (auto& v : fam_items) v = 0;
        for (int i = 0; i < kMaxItems; ++i)
        {
            const auto& info = rules::item_info(i);
            if (info.family != rules::fam_none) fam_items[info.family] += o.items[static_cast<std::size_t>(i)] * (info.kind == rules::ik_emblem ? 5 : 1);
        }
        emblem_count = 0;
        for (bool b : rules::emblems(o.items)) emblem_count += b ? 1 : 0;
    }
    int i = 0;
    x[i++] = static_cast<float>(o.round) / 1000.0f;
    x[i++] = static_cast<float>(o.banked_us) / 1000.0f;
    x[i++] = static_cast<float>(o.banked_them) / 1000.0f;
    x[i++] = rate_us;
    x[i++] = rate_them;
    x[i++] = static_cast<float>(o.allies_alive) / 5.0f;
    x[i++] = static_cast<float>(o.enemies_alive) / 5.0f;
    x[i++] = static_cast<float>(m.deaths) / 10.0f;
    x[i++] = o.self.max_hp > 0.0f ? o.self.hp / o.self.max_hp : 0.0f;
    x[i++] = o.self.max_hp / kHp;
    x[i++] = o.self.strength / 100.0f;
    x[i++] = o.self.focus / 100.0f;
    x[i++] = static_cast<float>(o.self.gems) / kGems;
    x[i++] = static_cast<float>(o.self.gold) / kGold;
    // Command-mode state uses the same 48-wide ABI, replacing redundant derived statistics with
    // exact control context. Without capture status and command history, distinct expert decisions
    // (hold a ready camp / move, issue a priority / stop) can otherwise have identical inputs.
    x[i++] = o.command_facts ? std::asinh(static_cast<float>(o.priority)) / 10.0f : s.gold_mult - 1.0f;
    for (int f = 0; f < rules::fam_count; ++f) x[i++] = static_cast<float>(fam_items[f]) / 10.0f;  // 7
    const int n_em = o.command_facts ? 0 : emblem_count;
    x[i++] = o.command_facts ? static_cast<float>(o.command_index) / 8.0f : static_cast<float>(n_em) / 3.0f;
    if (o.command_facts)
    {
        for (int bit = 0; bit < 4; ++bit) x[i++] = ((o.self.type >> bit) & 1) ? 1.0f : 0.0f;
        const int goal_code = m.goal_kind >= 0 ? m.goal_kind + 1 : 0;
        for (int bit = 0; bit < 4; ++bit) x[i++] = ((goal_code >> bit) & 1) ? 1.0f : 0.0f;
        const int group = o.alive && ctx.map->inside(o.self.at)
            ? ctx.map->group_of[static_cast<std::size_t>(ctx.map->idx(o.self.at))] : -1;
        x[i++] = group >= 0 && group < static_cast<int>(o.group_status.size())
            ? static_cast<float>(o.group_status[static_cast<std::size_t>(group)]) / 3.0f : -1.0f;
        x[i++] = group >= 0 && m.goal_kind == g_farm && m.goal_target == group ? 1.0f : 0.0f;
        x[i++] = o.can_move ? 1.0f : 0.0f;
        x[i++] = o.can_attack ? 1.0f : 0.0f;
        x[i++] = o.can_cast ? 1.0f : 0.0f;  // 13 values -> 36
    }
    else if (goal_state_facts)
    {
        for (int bit = 0; bit < 4; ++bit) x[i++] = ((o.self.type >> bit) & 1) ? 1.0f : 0.0f;
        for (int k = 0; k < g_none; ++k) x[i++] = m.goal_kind == k ? 1.0f : 0.0f;  // 7 option kinds
        x[i++] = m.goal_kind < 0 || m.goal_kind == g_none ? 1.0f : 0.0f;  // no option yet -> 12
        x[i++] = 0.0f;  // spare -> 13
    }
    else for (int t = 0; t < 13; ++t) x[i++] = o.self.type == t ? 1.0f : 0.0f;
    x[i++] = static_cast<float>(o.enemies.size()) / 5.0f;
    x[i++] = static_cast<float>(o.allies.size()) / 5.0f;
    x[i++] = static_cast<float>(std::min(nearest_enemy(o, o.self.at), 20)) / kDist;
    x[i++] = hp8 / (hp8 + eh8 + 1.0f);
    x[i++] = clampf(enemy_damage_at(o, o.self.at, s.champ_taken) / std::max(1.0f, o.self.hp), 0.0f, 3.0f);
    x[i++] = clampf(camp_strikes_at(o, o.self.at, s.camp_taken) / std::max(1.0f, o.self.hp), 0.0f, 3.0f);
    x[i++] = enemy_gems / kGems;
    x[i++] = static_cast<float>(std::min(ctx.paths->to_spawn(o.self.at), 200)) / kRounds;
    x[i++] = static_cast<float>(std::min(altar, 60)) / kRounds;
    x[i++] = o.on_spawn ? 1.0f : 0.0f;
    if (o.command_facts)
    {
        const float remaining = static_cast<float>(std::max(0, o.channel_remaining));
        const float progress = remaining / (remaining + 1.0f);
        x[i++] = o.teleporting ? 2.0f + progress :
            (o.self.recalling ? 1.0f + progress : (o.self.tributing ? -1.0f - progress : 0.0f));
    }
    else x[i++] = o.self.recalling ? 1.0f : (o.self.tributing ? -1.0f : 0.0f);
    x[i++] = m.goal_since >= 0 ? clampf(static_cast<float>(o.round - m.goal_since) / kRounds, 0.0f, 3.0f) : 0.0f;  // 48
    // (goal kind one-hot is in the candidates' "is current goal" facts and in the step/attack/cast facts)
    while (i < kState) x[i++] = 0.0f;
}

static void goal_candidates_impl(const Context& ctx, CandSet& out)
{
    const auto& o = *ctx.obs;
    const auto& m = *ctx.mem;
    const auto& map = *ctx.map;
    const auto& cat = catalog();
    const auto s = self_facts(o);
    out.head = h_goal;
    out.n = 0;
    auto add = [&](GoalKind kind, int target) -> Cand& {
        Cand& c = out.c[out.n++];
        c = Cand{};
        c.kind = kind;
        c.target = target;
        c.phi[kind] = 1.0f;
        c.phi[23] = (m.goal_kind == kind && m.goal_target == target) ? 1.0f : 0.0f;
        return c;
    };
    // Farm: the 6 nearest groups by path (any status: respawn time is a fact).
    struct GD { int g, d; };
    static std::vector<GD> gd;
    gd.clear();
    {
        // Path distances only for the 8 nearest by straight line (a path is never shorter than Chebyshev).
        // The 7 nearest centres (ties to the lower group index), kept sorted in a small array: one comparison
        // rejects a farther group.
        GD near[7];
        std::size_t keep = 0;
        const Tile at = o.self.at;
        for (std::size_t g = 0; g < map.groups.size(); ++g)
        {
            const int d = cheb(map.groups[g].center, at);
            if (keep == 7 && d >= near[6].d) continue;
            std::size_t j = keep < 7 ? keep++ : 6;
            for (; j > 0 && near[j - 1].d > d; --j) near[j] = near[j - 1];
            near[j] = { static_cast<int>(g), d };
        }
        prof::mark(41);
        for (std::size_t k = 0; k < keep; ++k)
        {
            if (goal_out_of_fuel()) { out.n = 0; return; }
            const int d = ctx.paths->to_group(near[k].g, o.self.at);
            if (d < kUnreachable) gd.push_back({ near[k].g, d });
        }
    }
    std::sort(gd.begin(), gd.end(), [](const GD& a, const GD& b) { return a.d < b.d; });
    prof::mark(36);
    if (goal_out_of_fuel()) { out.n = 0; return; }
    // Index the current visible camps once, rather than scanning them for every selected member. Bucket
    // chains run backwards through the view so duplicate tiles retain the old scan's last-match result.
    // Only capacities survive a call: even an in-place refresh with the same revision rebuilds the index.
    static std::vector<int> camp_buckets, camp_next;
    std::size_t n_buckets = 1;
    while (n_buckets < o.camps.size() * 2) n_buckets *= 2;
    camp_buckets.assign(n_buckets, -1);
    camp_next.resize(o.camps.size());
    const auto camp_bucket = [&](Tile t) {
        return (static_cast<std::uint32_t>(t.x) * 0x9e3779b9u ^ static_cast<std::uint32_t>(t.y) * 0x85ebca6bu) &
               static_cast<std::uint32_t>(n_buckets - 1);
    };
    for (std::size_t i = 0; i < o.camps.size(); ++i)
    {
        auto& bucket = camp_buckets[camp_bucket(o.camps[i].at)];
        camp_next[i] = bucket;
        bucket = static_cast<int>(i);
    }
    prof::mark(37);
    for (std::size_t k = 0; k < gd.size() && k < 5; ++k)
    {
        if (goal_out_of_fuel()) { out.n = 0; return; }
        const int g = gd[k].g;
        const auto& gr = map.groups[static_cast<std::size_t>(g)];
        float hp[64];
        float hp_now = 0.0f;
        bool viewed = false;  // a member's health comes from the view (otherwise every member has its design health)
        for (std::size_t i = 0; i < gr.members.size() && i < 64; ++i)
        {
            float h = cat.camp(gr.members[i].camp_type).health;
            if (!o.camps.empty())
                for (int j = camp_buckets[camp_bucket(gr.members[i].at)]; j >= 0; j = camp_next[static_cast<std::size_t>(j)])
                    if (o.camps[static_cast<std::size_t>(j)].at == gr.members[i].at)
                    {
                        h = o.camps[static_cast<std::size_t>(j)].hp;
                        viewed = true;
                        break;
                    }
            hp[i] = h;
            hp_now += h;
        }
        int claimants = 0;
        for (std::size_t a = 0; a < o.claim_kind.size(); ++a)
            if (o.claim_kind[a] == g_farm && o.claim_target[a] == g) ++claimants;
        // The forecast of a group at design health depends only on the group and our damage rate: cached on them.
        // (Keyed on the map too: native tools replay many maps in one process.)
        struct Forecast { float dps, total_hp; std::size_t members; rules::ClearForecast f; bool valid; };
        static std::vector<Forecast> forecasts;
        static const Map* forecast_map = nullptr;
        if (forecast_map != &map || forecasts.size() != map.groups.size())
        {
            forecast_map = &map;
            forecasts.assign(map.groups.size(), Forecast{});
        }
        const float dps = s.rate.sustained * s.camp_mult;
        auto& cached = forecasts[static_cast<std::size_t>(g)];
        rules::ClearForecast alone;
        if (!viewed && cached.valid && cached.dps == dps && cached.total_hp == gr.total_hp && cached.members == gr.members.size()) alone = cached.f;
        else
        {
            alone = rules::clear(map, g, hp, dps);
            if (!viewed) cached = Forecast{ dps, gr.total_hp, gr.members.size(), alone, true };
        }
        // With k claimants the team's damage rate is (1 + k) times ours: rounds and damage scale by 1 / (1 + k).
        rules::ClearForecast shared = alone;
        shared.rounds = static_cast<int>(std::ceil(static_cast<float>(alone.rounds) / static_cast<float>(1 + claimants)));
        shared.damage = alone.damage / static_cast<float>(1 + claimants);
        const int respawn = (g < static_cast<int>(m.respawn_round.size()) && m.respawn_round[static_cast<std::size_t>(g)] > o.round)
                                ? m.respawn_round[static_cast<std::size_t>(g)] - o.round : 0;
        const int enemy_age = g < static_cast<int>(m.enemy_near_round.size()) ? o.round - m.enemy_near_round[static_cast<std::size_t>(g)] : 999;
        const auto st = g < static_cast<int>(o.group_status.size()) ? o.group_status[static_cast<std::size_t>(g)] : gs_unknown;
        Cand& c = add(g_farm, g);
        c.phi[8] = static_cast<float>(gd[k].d) / kRounds;
        c.phi[9] = static_cast<float>(gr.gold) * s.gold_mult / kGold;
        c.phi[10] = static_cast<float>(gr.gems) / kGems;
        c.phi[11] = hp_now / 10000.0f;
        c.phi[12] = gr.total_strength / 200.0f;
        c.phi[13] = clampf(static_cast<float>(alone.rounds) / kRounds, 0.0f, 10.0f) / 2.5f;  // [0, 4]: the log codec's range
        c.phi[14] = clampf(alone.damage * s.camp_taken / std::max(1.0f, o.self.hp), 0.0f, 10.0f) / 2.5f;
        c.phi[15] = clampf(static_cast<float>(shared.rounds) / kRounds, 0.0f, 10.0f) / 2.5f;
        c.phi[16] = clampf(shared.damage * s.camp_taken / static_cast<float>(1 + claimants) / std::max(1.0f, o.self.hp), 0.0f, 10.0f) / 2.5f;
        c.phi[17] = static_cast<float>(respawn) / kRounds;
        c.phi[18] = static_cast<float>(claimants) / 4.0f;
        c.phi[19] = enemy_age < 999 ? clampf(1.0f - static_cast<float>(enemy_age) / kRounds, 0.0f, 1.0f) : 0.0f;
        c.phi[20] = static_cast<float>(cheb(gr.center, map.spawn_center[1 - o.team])) / kRounds;
        c.phi[21] = st == gs_reward ? 1.0f : 0.0f;
        c.phi[22] = st == gs_respawning ? 1.0f : 0.0f;
    }
    prof::mark(38);
    if (goal_out_of_fuel()) { out.n = 0; return; }
    // Bank: the 2 nearest altars by path, when carrying gems.
    if (o.self.gems > 0)
    {
        // The 2 nearest by path, without a path read for every altar: the arrival set is the altar's 3x3, so a
        // path is never shorter than the Chebyshev distance - 1. Altars are visited by that bound; once two altars
        // are at most the next bound, no later altar can be nearer. Ties go to the lower altar index.
        static std::vector<GD> ad, order;
        ad.clear();
        order.clear();
        for (std::size_t a = 0; a < map.altars.size(); ++a)
            order.push_back({ static_cast<int>(a), std::max(0, cheb(map.altars[a], o.self.at) - 1) });
        std::sort(order.begin(), order.end(), [](const GD& a, const GD& b) { return a.d != b.d ? a.d < b.d : a.g < b.g; });
        for (const auto& lb : order)
        {
            if (ad.size() >= 2 && ad[1].d < lb.d) break;
            if (goal_out_of_fuel()) { out.n = 0; return; }
            const int d = ctx.paths->to_altar(lb.g, o.self.at);
            if (d >= kUnreachable) continue;
            ad.push_back({ lb.g, d });
            std::sort(ad.begin(), ad.end(), [](const GD& a, const GD& b) { return a.d != b.d ? a.d < b.d : a.g < b.g; });
        }
        for (std::size_t k = 0; k < ad.size() && k < 2; ++k)
        {
            Cand& c = add(g_bank, ad[k].g);
            c.phi[8] = static_cast<float>(ad[k].d) / kRounds;
            c.phi[9] = static_cast<float>(o.self.gems) / kGems;
            c.phi[10] = static_cast<float>(rules::tribute_turns(o.self.gems)) / 10.0f;
            c.phi[11] = (ad[k].g < static_cast<int>(o.altar_lock.size()) && o.altar_lock[static_cast<std::size_t>(ad[k].g)] == lk_theirs) ? 1.0f : 0.0f;
            int near = 0;
            for (const auto& e : o.enemies) near += cheb(e.at, map.altars[static_cast<std::size_t>(ad[k].g)]) <= 6 ? 1 : 0;
            c.phi[12] = static_cast<float>(near) / 4.0f;
        }
    }
    prof::mark(61);
    if (goal_out_of_fuel()) { out.n = 0; return; }
    // Recall (away from spawn, alive, not channelling).
    if (o.alive && !o.on_spawn)
    {
        Cand& c = add(g_recall, -1);
        c.phi[8] = o.self.max_hp > 0.0f ? 1.0f - o.self.hp / o.self.max_hp : 0.0f;
        c.phi[9] = static_cast<float>(o.self.gold) / kGold;
        // The best stat gain affordable now (one shard), relative to current stats. Each shard's gain is cached on
        // the items and stats (they change only by buying); the best affordable one is then a scan of costs.
        struct Gain { std::uint32_t cost; float value; };
        static std::array<std::uint8_t, kMaxItems> key_items{};
        static float key_hp = -1.0f, key_str = -1.0f, key_focus = -1.0f;
        static int key_type = -1;
        static Gain gains[kMaxItems];
        static int n_gains = 0;
        if (key_items != o.items || key_type != o.self.type || key_hp != o.self.max_hp || key_str != o.self.strength || key_focus != o.self.focus)
        {
            key_items = o.items;
            key_type = o.self.type;
            key_hp = o.self.max_hp;
            key_str = o.self.strength;
            key_focus = o.self.focus;
            n_gains = 0;
            for (int it = 0; it < cat.n_items && n_gains < kMaxItems; ++it)
            {
                const auto& d = cat.item(it);
                if (!d.purchasable || rules::item_info(it).kind != rules::ik_shard) continue;
                const auto gain = rules::buy_gain(o.self.type, o.items, it);
                gains[n_gains++] = { d.cost, gain.health / std::max(1.0f, o.self.max_hp) + gain.strength / std::max(1.0f, o.self.strength) +
                                                 gain.focus / std::max(1.0f, o.self.focus) };
            }
        }
        float best = 0.0f;
        for (int k = 0; k < n_gains; ++k)
            if (gains[k].cost <= o.self.gold) best = std::max(best, gains[k].value);
        c.phi[10] = best * static_cast<float>(o.self.gold / 30u) / 5.0f;
        c.phi[11] = static_cast<float>(std::min(ctx.paths->to_spawn(o.self.at), 200)) / kRounds;
        c.phi[12] = clampf(enemy_damage_at(o, o.self.at, s.champ_taken) / std::max(1.0f, o.self.hp), 0.0f, 3.0f);
        c.phi[13] = static_cast<float>(o.self.gems) / kGems;
        c.phi[14] = static_cast<float>(rules::recall_turns(o.items) - (o.recall_started ? 1 : 0)) / 5.0f;  // turn ends left
    }
    prof::mark(62);
    if (goal_out_of_fuel()) { out.n = 0; return; }
    // Engage: the 3 nearest visible enemies.
    {
        static std::vector<GD> ed;
        ed.clear();
        for (std::size_t e = 0; e < o.enemies.size(); ++e) ed.push_back({ static_cast<int>(e), cheb(o.enemies[e].at, o.self.at) });
        std::sort(ed.begin(), ed.end(), [](const GD& a, const GD& b) { return a.d < b.d; });
        for (std::size_t k = 0; k < ed.size() && k < 2; ++k)
        {
            const auto& e = o.enemies[static_cast<std::size_t>(ed[k].g)];
            const auto er = rules::rates(e.type, e.strength, e.focus, e.cooldown, true);
            Cand& c = add(g_engage, e.id);
            c.phi[8] = clampf(e.hp / std::max(1.0f, s.rate.sustained) / kRounds, 0.0f, 5.0f) / 1.25f;  // [0, 4]
            c.phi[9] = clampf(o.self.hp / std::max(1.0f, er.sustained * s.champ_taken) / kRounds, 0.0f, 5.0f) / 1.25f;
            c.phi[10] = static_cast<float>(e.gems) / kGems;
            c.phi[11] = static_cast<float>(ed[k].d) / kDist;
            int allies_near = 0, enemies_near = 0;
            for (const auto& a : o.allies) allies_near += cheb(a.at, e.at) <= 4 ? 1 : 0;
            for (const auto& f : o.enemies) enemies_near += (&f != &e && cheb(f.at, e.at) <= 4) ? 1 : 0;
            c.phi[12] = static_cast<float>(allies_near) / 4.0f;
            c.phi[13] = static_cast<float>(enemies_near) / 4.0f;
            c.phi[14] = cat.champion(e.type).attack_type == 1 ? 1.0f : 0.0f;
            c.phi[15] = (e.recalling || e.tributing) ? 1.0f : 0.0f;
            c.phi[16] = e.max_hp > 0.0f ? e.hp / e.max_hp : 1.0f;
            c.phi[17] = clampf(er.burst * s.champ_taken / std::max(1.0f, o.self.hp), 0.0f, 3.0f);
        }
    }
    // Avoid (only meaningful with enemies in view).
    if (!o.enemies.empty())
    {
        Cand& c = add(g_avoid, -1);
        c.phi[8] = clampf(enemy_damage_at(o, o.self.at, s.champ_taken) / std::max(1.0f, o.self.hp), 0.0f, 3.0f);
        c.phi[9] = static_cast<float>(std::min(nearest_enemy(o, o.self.at), 20)) / kDist;
        c.phi[10] = static_cast<float>(std::min(ctx.paths->to_spawn(o.self.at), 200)) / kRounds;
    }
    // Hold (always available).
    {
        Cand& c = add(g_hold, -1);
        c.phi[8] = o.on_spawn ? 1.0f : 0.0f;
        c.phi[9] = o.self.max_hp > 0.0f ? o.self.hp / o.self.max_hp : 0.0f;
        int near = 0;
        for (const auto& e : o.enemies) near += cheb(e.at, map.spawn_center[o.team]) <= 8 ? 1 : 0;
        c.phi[10] = static_cast<float>(near) / 4.0f;
    }
    // Assist: up to 2 visible allies with an enemy within 4 of them.
    for (const auto& a : o.allies)
    {
        if (out.n >= kMaxCands) break;
        int threats = 0;
        for (const auto& e : o.enemies) threats += cheb(e.at, a.at) <= 4 ? 1 : 0;
        if (!threats) continue;
        Cand& c = add(g_assist, a.id);
        c.phi[8] = a.max_hp > 0.0f ? a.hp / a.max_hp : 1.0f;
        c.phi[9] = static_cast<float>(threats) / 4.0f;
        c.phi[10] = static_cast<float>(cheb(a.at, o.self.at)) / kDist;
        if (out.n >= 15) break;
    }
    prof::mark(39);
}

static void step_candidates_impl(const Context& ctx, CandSet& out)
{
    const auto& o = *ctx.obs;
    const auto& map = *ctx.map;
    const auto& f = facts(o);
    const auto& s = f.s;
    out.head = h_step;
    out.n = 0;
    const int here = goal_distance(ctx, o.self.at);
    const int spawn_here = ctx.paths->to_spawn(o.self.at);
    // The goal's target tiles in view (living camps of the goal group, or the goal enemy), collected once.
    Tile goal_tiles[32];
    int n_goal_tiles = 0;
    for (const auto& c2 : o.camps)
        if (n_goal_tiles < 32 && goal_target_at(ctx, c2.at)) goal_tiles[n_goal_tiles++] = c2.at;
    for (const auto& e : o.enemies)
        if (n_goal_tiles < 32 && goal_target_at(ctx, e.at)) goal_tiles[n_goal_tiles++] = e.at;
    // Per-call invariants (the same values the per-tile expressions computed each time).
    const float hp1 = std::max(1.0f, o.self.hp);
    const int range = static_cast<int>(catalog().champion(o.self.type).attack_range);
    const std::uint8_t own_spawn = o.team == 0 ? tf_spawn_a : tf_spawn_b;
    const bool farm_goal = ctx.mem->goal_kind == g_farm;
    const int dir_x = ctx.mem->dir_x, dir_y = ctx.mem->dir_y;
    const int dir2 = dir_x * dir_x + dir_y * dir_y;
    const int near_here = o.enemies.empty() ? 99 : nearest_enemy(o, o.self.at);
    int sum_h = 0, near_h = 1 << 20;
    for (const auto& a : o.allies)
    {
        const int dh = cheb(a.at, o.self.at);
        sum_h += dh;
        near_h = std::min(near_h, dh);
    }
    float bh = 1e9f;
    for (int k = 0; k < n_goal_tiles; ++k)
    {
        const float hx = static_cast<float>(goal_tiles[k].x - o.self.at.x), hy = static_cast<float>(goal_tiles[k].y - o.self.at.y);
        bh = std::min(bh, hx * hx + hy * hy);
    }
    const float root_bh = n_goal_tiles > 0 ? std::sqrt(bh) : 0.0f;
    auto fill = [&](Cand& c, Tile t) {
        const int d = goal_distance(ctx, t);
        c.phi[3] = clampf(static_cast<float>(d - here), -2.0f, 2.0f);
        c.phi[4] = clampf(camp_strikes_at(f, o.self.id, t, s.camp_taken) / hp1, 0.0f, 3.0f);
        c.phi[5] = clampf(enemy_damage_at(f, t, s.champ_taken) / hp1, 0.0f, 3.0f);
        bool goal_in_range = false, enemy_in_range = false;
        for (int k = 0; k < n_goal_tiles && !goal_in_range; ++k) goal_in_range = cheb(goal_tiles[k], t) <= range;
        int near_t = 99;
        for (const auto& e : o.enemies)
        {
            const int de = cheb(e.at, t);
            enemy_in_range |= de <= range;
            near_t = std::min(near_t, de);
        }
        c.phi[6] = goal_in_range ? 1.0f : 0.0f;
        c.phi[7] = enemy_in_range ? 1.0f : 0.0f;
        const auto flags = map.at(t);
        c.phi[8] = (flags & tf_bush) ? 1.0f : 0.0f;
        c.phi[9] = (flags & own_spawn) ? 1.0f : 0.0f;
        c.phi[11] = static_cast<float>(std::min(near_t, 20)) / kDist;
        const int g = map.group_of[static_cast<std::size_t>(map.idx(t))];
        c.phi[12] = (farm_goal && g >= 0 && g == ctx.mem->goal_target) ? 1.0f : 0.0f;
        c.phi[13] = clampf(static_cast<float>(ctx.paths->to_spawn(t) - spawn_here), -2.0f, 2.0f);
        // Facts that separate tiles of equal path progress: motion continuity, spacing to allies and the
        // nearest enemy, straight-line progress to the goal's targets, and how open the tile is.
        const int sx = (t.x > o.self.at.x) - (t.x < o.self.at.x), sy = (t.y > o.self.at.y) - (t.y < o.self.at.y);
        c.phi[16] = (sx != 0 || sy != 0) && (dir_x != 0 || dir_y != 0)
                        ? static_cast<float>(sx * dir_x + sy * dir_y) / std::sqrt(static_cast<float>((sx * sx + sy * sy) * dir2))
                        : 0.0f;
        if (!o.allies.empty())
        {
            int adj = 0, sum_t = 0, near_a = 1 << 20;
            for (const auto& a : o.allies)
            {
                const int dt = cheb(a.at, t);
                adj += dt <= 1 ? 1 : 0;
                sum_t += dt;
                near_a = std::min(near_a, dt);
            }
            c.phi[10] = static_cast<float>(adj) / 4.0f;
            c.phi[17] = clampf(static_cast<float>(near_a - near_h), -2.0f, 2.0f);
            c.phi[18] = clampf(static_cast<float>(sum_t - sum_h) / static_cast<float>(o.allies.size()), -2.0f, 2.0f);
        }
        if (!o.enemies.empty()) c.phi[19] = clampf(static_cast<float>(near_t - near_here), -2.0f, 2.0f);
        if (n_goal_tiles > 0)
        {
            float bt = 1e9f;
            for (int k = 0; k < n_goal_tiles; ++k)
            {
                const float ax = static_cast<float>(goal_tiles[k].x - t.x), ay = static_cast<float>(goal_tiles[k].y - t.y);
                bt = std::min(bt, ax * ax + ay * ay);
            }
            c.phi[20] = clampf(std::sqrt(bt) - root_bh, -2.0f, 2.0f);
        }
        int open = 0;
        if (t.x >= 1 && t.y >= 1 && t.x + 1 < map.w && t.y + 1 < map.h)
        {
            const std::uint8_t* row = map.flags.data() + map.idx(t) - map.w - 1;
            for (int dy = 0; dy < 3; ++dy, row += map.w)
                for (int dx = 0; dx < 3; ++dx)
                    if ((dx != 1 || dy != 1) && !(row[dx] & (tf_wall | tf_hole))) ++open;
        }
        else
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const Tile q{ static_cast<std::int16_t>(t.x + dx), static_cast<std::int16_t>(t.y + dy) };
                    if ((dx != 0 || dy != 0) && map.inside(q) && !(map.at(q) & (tf_wall | tf_hole))) ++open;
                }
        c.phi[21] = static_cast<float>(open) / 8.0f;
    };
    prof::mark(40);
    // Stay. Recall and submit act from this tile too: their facts are the stay's (fill writes phi[3..21] only).
    {
        Cand& c = out.c[out.n++];
        c = Cand{};
        c.kind = 1;
        c.at = o.self.at;
        c.phi[0] = 1.0f;
        fill(c, o.self.at);
    }
    const auto here_facts = [&](Cand& c) {
        const Cand& stay = out.c[0];
        for (int k = 3; k <= 21; ++k) c.phi[k] = stay.phi[k];
    };
    if (o.can_move)
        for (const Tile t : o.step_ok)
            {
                if (t == o.self.at || out.n >= kMaxCands - 2) continue;
                Cand& c = out.c[out.n++];
                c = Cand{};
                c.kind = 0;
                c.at = t;
                c.phi[15] = 1.0f;
                fill(c, t);
            }
    // Channels: recall (away from spawn), submit gems (beside an unlocked altar with gems).
    if (o.alive && !o.on_spawn && !o.self.recalling && !o.self.tributing)
    {
        Cand& c = out.c[out.n++];
        c = Cand{};
        c.kind = 2;
        c.at = o.self.at;
        c.phi[1] = 1.0f;
        here_facts(c);
    }
    if (o.self.gems > 0 && !o.self.tributing)
        for (std::size_t a = 0; a < map.altars.size(); ++a)
            if (cheb(map.altars[a], o.self.at) <= catalog().settings.submit_gems_range &&
                !(a < o.altar_lock.size() && o.altar_lock[a] == lk_theirs))
            {
                Cand& c = out.c[out.n++];
                c = Cand{};
                c.kind = 3;
                c.target = static_cast<int>(a);
                c.at = map.altars[a];
                c.phi[2] = 1.0f;
                here_facts(c);
                break;
            }
}

static void attack_candidates_impl(const Context& ctx, CandSet& out)
{
    const auto& o = *ctx.obs;
    const auto& cat = catalog();
    const auto s = self_facts(o);
    out.head = h_attack;
    out.n = 0;
    {
        Cand& c = out.c[out.n++];
        c = Cand{};
        c.kind = 0;
        c.phi[0] = 1.0f;
    }
    if (!o.can_attack) return;
    const int range = static_cast<int>(cat.champion(o.self.type).attack_range);
    for (const auto& cp : o.camps)
    {
        if (o.command_facts && cp.hp <= 0.0f) continue;
        if (out.n >= 9) break;  // none + the 8 nearest-listed camps in range
        if (cheb(cp.at, o.self.at) > range) continue;
        const auto& d = cat.camp(cp.type);
        const float dmg = o.self.strength * s.camp_mult;
        Cand& c = out.c[out.n++];
        c = Cand{};
        c.kind = 1;
        c.at = cp.at;
        c.target = cp.group;
        c.phi[1] = 1.0f;
        c.phi[3] = clampf(cp.hp / std::max(1.0f, dmg) / 20.0f, 0.0f, 5.0f) / 1.25f;  // [0, 4]
        c.phi[4] = cp.hp <= dmg ? 1.0f : 0.0f;
        c.phi[6] = clampf(d.strength * s.camp_taken / std::max(1.0f, o.self.hp), 0.0f, 3.0f);
        c.phi[7] = goal_target_at(ctx, cp.at) ? 1.0f : 0.0f;
        c.phi[8] = cp.awake ? 1.0f : 0.0f;
        c.phi[11] = d.health > 0.0f ? cp.hp / d.health : 0.0f;
        c.phi[13] = d.strength / 200.0f;
        c.phi[14] = d.strength <= 0.0f ? 1.0f : 0.0f;
    }
    for (const auto& e : o.enemies)
    {
        if (o.command_facts && e.hp <= 0.0f) continue;
        if (out.n >= kMaxCands) break;
        if (cheb(e.at, o.self.at) > range) continue;
        const auto er = rules::rates(e.type, e.strength, e.focus, e.cooldown, true);
        Cand& c = out.c[out.n++];
        c = Cand{};
        c.kind = 2;
        c.at = e.at;
        c.target = e.id;
        c.phi[2] = 1.0f;
        c.phi[3] = clampf((e.hp + e.shield) / std::max(1.0f, o.self.strength) / 20.0f, 0.0f, 5.0f) / 1.25f;  // [0, 4]
        c.phi[4] = e.hp + e.shield <= o.self.strength ? 1.0f : 0.0f;
        c.phi[5] = static_cast<float>(e.gems) / kGems;
        c.phi[6] = clampf(er.burst * s.champ_taken / std::max(1.0f, o.self.hp), 0.0f, 3.0f);
        c.phi[7] = goal_target_at(ctx, e.at) ? 1.0f : 0.0f;
        c.phi[9] = (e.recalling || e.tributing) ? 1.0f : 0.0f;
        c.phi[10] = (static_cast<float>(cat.settings.kill_gold) + static_cast<float>(e.gems)) / 200.0f;
        c.phi[11] = e.max_hp > 0.0f ? e.hp / e.max_hp : 1.0f;
        int allies_on = 0;
        for (const auto& a : o.allies) allies_on += cheb(a.at, e.at) <= 2 ? 1 : 0;
        c.phi[15] = static_cast<float>(allies_on) / 4.0f;
    }
}

static void ranged_candidates_impl(const Context& ctx, CandSet& out)
{
    step_candidates(ctx, out);
    out.head = h_ranged;
    CandSet attacks;
    attack_candidates(ctx, attacks);
    // At most 11 steps/channels plus 8 camps and 5 enemy champions: the existing 24 slots suffice.
    for (int i = 1; i < attacks.n && out.n < kMaxCands; ++i)
    {
        Cand& c = out.c[out.n++];
        c = attacks.c[i];
        c.kind = static_cast<std::uint8_t>(c.kind + 3);
        c.phi[22] = c.kind == 4 ? 1.0f : 0.0f;
        c.phi[23] = c.kind == 5 ? 1.0f : 0.0f;
    }
}

static void cast_candidates_impl(const Context& ctx, int slot, CandSet& out)
{
    const auto& o = *ctx.obs;
    const auto& map = *ctx.map;
    const auto& cat = catalog();
    const auto s = self_facts(o);
    out.head = slot == 0 ? h_cast0 : h_cast1;
    out.n = 0;
    {
        Cand& c = out.c[out.n++];
        c = Cand{};
        c.phi[0] = 1.0f;
    }
    if (!o.can_cast || o.self.cooldown[slot] != 0) return;
    const auto& a = cat.champion(o.self.type).ability[slot];
    if (a.kind == ab_none) return;
    const float power = a.modifier * o.self.focus;
    auto base = [&](Tile at, int target) -> Cand& {
        Cand& c = out.c[out.n++];
        c = Cand{};
        c.kind = a.kind;
        c.target = target;
        c.at = at;
        if (a.kind >= 1 && a.kind <= 7) c.phi[a.kind] = 1.0f;
        c.phi[23] = a.excludes_walking ? 1.0f : 0.0f;
        return c;
    };
    const int range = static_cast<int>(a.range);
    // An attack reset restores a spent attack allowance; with the attack still available it is not a legal cast.
    if (a.kind == ab_reset && o.can_attack) return;
    switch (a.kind)
    {
    case ab_reset:
    case ab_shield: {
        Cand& c = base(o.self.at, o.self.id);
        if (a.kind == ab_shield)
            c.phi[14] = clampf(power / std::max(1.0f, enemy_damage_at(o, o.self.at, s.champ_taken) + camp_strikes_at(o, o.self.at, s.camp_taken)), 0.0f, 5.0f) / 1.25f;  // [0, 4]
        else
        {
            int targets = 0;
            const int ar = static_cast<int>(cat.champion(o.self.type).attack_range);
            for (const auto& e : o.enemies) targets += cheb(e.at, o.self.at) <= ar ? 1 : 0;
            for (const auto& cp : o.camps) targets += cheb(cp.at, o.self.at) <= ar ? 1 : 0;
            c.phi[8] = targets > 0 ? 1.0f : 0.0f;
        }
        break;
    }
    case ab_heal:
        for (const auto& al : o.allies)
        {
            if (o.command_facts && (!(a.targets & tg_allies) || al.hp <= 0.0f)) continue;
            if (out.n >= kMaxCands || cheb(al.at, o.self.at) > range) continue;
            Cand& c = base(al.at, al.id);
            c.phi[13] = clampf((al.max_hp - al.hp) > 0.0f ? std::min(power, al.max_hp - al.hp) / std::max(1.0f, al.max_hp) : 0.0f, 0.0f, 1.0f);
        }
        break;
    case ab_damage:
    case ab_cc:
    case ab_aoe: {
        auto hits = [&](Tile centre, Cand& c) {
            const int radius = a.kind == ab_aoe ? static_cast<int>(a.radius) : 0;
            float dealt = 0.0f;
            int kills = 0, enemies = 0, camps = 0;
            for (const auto& e : o.enemies)
                if (cheb(e.at, centre) <= radius)
                {
                    dealt += std::min(power, e.hp + e.shield) / std::max(1.0f, e.max_hp);
                    kills += e.hp + e.shield <= power ? 1 : 0;
                    ++enemies;
                    if (goal_target_at(ctx, e.at)) c.phi[21] = 1.0f;
                    if (e.recalling || e.tributing) c.phi[22] = 1.0f;
                }
            if (a.targets & tg_camps)
                for (const auto& cp : o.camps)
                    if (cheb(cp.at, centre) <= radius)
                    {
                        const float d = power * s.camp_mult;
                        dealt += std::min(d, cp.hp) / std::max(1.0f, cat.camp(cp.type).health);
                        kills += cp.hp <= d ? 1 : 0;
                        ++camps;
                        if (goal_target_at(ctx, cp.at)) c.phi[21] = 1.0f;
                    }
            c.phi[8] = dealt;
            c.phi[9] = static_cast<float>(kills);
            c.phi[10] = static_cast<float>(enemies) / 4.0f;
            c.phi[11] = static_cast<float>(camps) / 4.0f;
            c.phi[12] = a.kind == ab_cc ? a.duration / 4.0f : 0.0f;
        };
        for (const auto& e : o.enemies)
        {
            if (o.command_facts && (!(a.targets & tg_enemies) || e.hp <= 0.0f)) continue;
            if (out.n >= kMaxCands || cheb(e.at, o.self.at) > range) continue;
            Cand& c = base(e.at, e.id);
            hits(e.at, c);
        }
        if (a.targets & tg_camps)
            for (const auto& cp : o.camps)
            {
                if (o.command_facts && cp.hp <= 0.0f) continue;
                if (out.n >= kMaxCands || cheb(cp.at, o.self.at) > range) continue;
                Cand& c = base(cp.at, -2 - cp.group);
                hits(cp.at, c);
            }
        break;
    }
    case ab_dash: {
        const int here = goal_distance(ctx, o.self.at);
        const auto& f = facts(o);
        const float hp1 = std::max(1.0f, o.self.hp);
        const std::uint8_t own_spawn = o.team == 0 ? tf_spawn_a : tf_spawn_b;
        (void)range;
        for (const Tile t : o.dash_ok)
            {
                if (o.command_facts && static_cast<float>(cheb(t, o.self.at)) > a.range) continue;
                if (out.n >= kMaxCands) break;
                if (t == o.self.at) continue;
                Cand& c = base(t, -1);
                c.phi[15] = clampf(static_cast<float>(goal_distance(ctx, t) - here), -3.0f, 3.0f);
                c.phi[16] = clampf(camp_strikes_at(f, o.self.id, t, s.camp_taken) / hp1, 0.0f, 3.0f);
                c.phi[17] = clampf(enemy_damage_at(f, t, s.champ_taken) / hp1, 0.0f, 3.0f);
                const auto flags = map.at(t);
                c.phi[18] = (flags & tf_bush) ? 1.0f : 0.0f;
                c.phi[19] = (flags & own_spawn) ? 1.0f : 0.0f;
                c.phi[20] = static_cast<float>(std::min(nearest_enemy(o, t), 20)) / kDist;
                if (a.modifier > 0.0f)
                {
                    float dealt = 0.0f;
                    for (const auto& e : o.enemies) dealt += cheb(e.at, t) <= static_cast<int>(a.radius) ? std::min(power, e.hp) / std::max(1.0f, e.max_hp) : 0.0f;
                    c.phi[8] = dealt;
                }
            }
        break;
    }
    default: break;
    }
}

static void shop_candidates_impl(const Context& ctx, CandSet& out)
{
    const auto& o = *ctx.obs;
    const auto& cat = catalog();
    out.head = h_shop;
    out.n = 0;
    {
        Cand& c = out.c[out.n++];
        c = Cand{};
        c.phi[0] = 1.0f;
    }
    for (int it = 0; it < cat.n_items && out.n < kMaxCands; ++it)
    {
        const auto& d = cat.item(it);
        const auto& info = rules::item_info(it);
        if (!d.purchasable || d.cost > o.self.gold || info.kind != rules::ik_shard) continue;
        const auto gain = rules::buy_gain(o.self.type, o.items, it);
        Cand& c = out.c[out.n++];
        c = Cand{};
        c.kind = 1;
        c.target = it;
        if (info.family != rules::fam_none) c.phi[1 + info.family] = 1.0f;
        c.phi[8] = gain.health / std::max(1.0f, o.self.max_hp);
        c.phi[9] = gain.strength / std::max(1.0f, o.self.strength);
        c.phi[10] = gain.focus / std::max(1.0f, o.self.focus);
        c.phi[11] = gain.completes_emblem ? 1.0f : 0.0f;
        c.phi[12] = (gain.completes_emblem && d.result >= 0) ? cat.item(d.result).passive_magnitude : 0.0f;
        c.phi[13] = static_cast<float>(d.cost) / std::max(1.0f, static_cast<float>(o.self.gold));
        c.phi[14] = static_cast<float>(o.items[static_cast<std::size_t>(it)]) / 5.0f;
        c.phi[15] = static_cast<float>(o.self.gold - d.cost) / kGold;
    }
}

static void tail_candidates_impl(const Context& ctx, bool home, bool committed, CandSet& out)
{
    const auto& o = *ctx.obs;
    const auto& m = *ctx.mem;
    out.head = h_tail;
    out.n = 0;
    out.c[out.n] = Cand{};
    out.c[out.n++].phi[0] = 1.0f;  // none
    if (!o.alive || home || committed) return;
    const auto s = self_facts(o);
    Cand& c = out.c[out.n++];
    c = Cand{};
    c.kind = 1;
    c.phi[1] = 1.0f;
    // Hints: how likely a recall is soon (the goal head's view), what keeping one next turn saves, why recall.
    c.phi[2] = m.goal_kind == g_recall ? 1.0f : 0.0f;
    c.phi[3] = m.goal_recall_p;
    c.phi[4] = m.goal_decided >= 0 ? clampf(static_cast<float>(o.round - m.goal_decided) / 10.0f, 0.0f, 2.0f) : 2.0f;
    const int turns = std::max(1, rules::recall_turns(o.items));
    c.phi[5] = 1.0f / static_cast<float>(turns);
    c.phi[6] = static_cast<float>(turns) / 5.0f;
    c.phi[7] = o.self.max_hp > 0.0f ? 1.0f - o.self.hp / o.self.max_hp : 0.0f;
    c.phi[8] = clampf(enemy_damage_at(o, o.self.at, s.champ_taken) / std::max(1.0f, o.self.hp), 0.0f, 3.0f);
    c.phi[9] = static_cast<float>(std::min(ctx.paths->to_spawn(o.self.at), 200)) / kRounds;
    c.phi[10] = static_cast<float>(o.self.gold) / kGold;
    c.phi[11] = static_cast<float>(o.self.gems) / kGems;
    c.phi[12] = static_cast<float>(o.enemies.size()) / 4.0f;
}

// The public builders: the facts as made, bounded to the log codec's range.
void state_vector(const Context& ctx, float* x) { state_vector_impl(ctx, x); bound_values(x, kState); }
void goal_candidates(const Context& ctx, CandSet& out) { goal_candidates_impl(ctx, out); bound_facts(out); }
void step_candidates(const Context& ctx, CandSet& out) { step_candidates_impl(ctx, out); bound_facts(out); }
void attack_candidates(const Context& ctx, CandSet& out) { attack_candidates_impl(ctx, out); bound_facts(out); }
void ranged_candidates(const Context& ctx, CandSet& out) { ranged_candidates_impl(ctx, out); bound_facts(out); }
void cast_candidates(const Context& ctx, int slot, CandSet& out) { cast_candidates_impl(ctx, slot, out); bound_facts(out); }
void shop_candidates(const Context& ctx, CandSet& out) { shop_candidates_impl(ctx, out); bound_facts(out); }
void tail_candidates(const Context& ctx, bool home, bool committed, CandSet& out) { tail_candidates_impl(ctx, home, committed, out); bound_facts(out); }

}  // namespace zero
