#include "rules.h"
#include "catalog_data.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace zero::rules {

namespace {

// Passive types (catalog passives[].type = SDK passive_type).
constexpr std::uint8_t kBonusGold = 1;
// Item passives (catalog items[].passive = SDK item_passive).
constexpr std::uint8_t kIpMonster = 1, kIpDurability = 2, kIpTreasure = 3, kIpJungle = 4, kIpHome = 5;

const char* const kFamilies[fam_count] = { "monster", "power", "durability", "treasure", "jungle", "home", "bruiser" };
const char* const kStats[st_count] = { "strength", "health", "focus" };

bool starts(const char* s, const char* p) { return std::strncmp(s, p, std::strlen(p)) == 0; }

const PassiveDesign& passive_of(int champion_type)
{
    const auto& cat = catalog();
    return cat.passives[cat.champion(champion_type).passive];
}

// Magnitude of an item passive if any owned emblem carries it (emblem passives do not stack).
float emblem_magnitude(const std::array<std::uint8_t, kMaxItems>& items, std::uint8_t passive)
{
    const auto& cat = catalog();
    for (int i = 0; i < cat.n_items; ++i)
        if (items[static_cast<std::size_t>(i)] && cat.item(i).passive == passive) return cat.item(i).passive_magnitude;
    return 0.0f;
}

}  // namespace

const ItemInfo& item_info(int item_id)
{
    static const std::array<ItemInfo, kMaxItems> table = [] {
        std::array<ItemInfo, kMaxItems> tt{};
        constexpr int n = static_cast<int>(sizeof(data::kItemFamily));
        for (int k = 0; k < n && k < kMaxItems; ++k)
        {
            auto& info = tt[static_cast<std::size_t>(k)];
            info.family = static_cast<Family>(data::kItemFamily[k]);
            info.stat = static_cast<Stat>(data::kItemStat[k]);
            info.kind = static_cast<ItemKind>(data::kItemKind[k]);
        }
        return tt;
    }();
    static const ItemInfo none{};
    return item_id >= 0 && item_id < kMaxItems ? table[static_cast<std::size_t>(item_id)] : none;
}

std::array<bool, fam_count> emblems(const std::array<std::uint8_t, kMaxItems>& items)
{
    std::array<bool, fam_count> e{};
    for (int i = 0; i < kMaxItems; ++i)
    {
        const auto& info = item_info(i);
        if (items[static_cast<std::size_t>(i)] && info.kind == ik_emblem && info.family != fam_none) e[info.family] = true;
    }
    return e;
}

float gold_multiplier(int champion_type, const std::array<std::uint8_t, kMaxItems>& items)
{
    const auto& p = passive_of(champion_type);
    return 1.0f + (p.type == kBonusGold ? p.magnitude : 0.0f) + emblem_magnitude(items, kIpTreasure);
}

float camp_damage_dealt_multiplier(int champion_type, const std::array<std::uint8_t, kMaxItems>& items)
{
    return catalog().champion(champion_type).camp_damage_mult * (1.0f + emblem_magnitude(items, kIpMonster));
}

float camp_damage_taken_multiplier(const std::array<std::uint8_t, kMaxItems>& items)
{
    return (1.0f - emblem_magnitude(items, kIpJungle)) * (1.0f - emblem_magnitude(items, kIpDurability));
}

float champion_damage_taken_multiplier(const std::array<std::uint8_t, kMaxItems>& items)
{
    return 1.0f - emblem_magnitude(items, kIpDurability);
}

int recall_turns(const std::array<std::uint8_t, kMaxItems>& items)
{
    const float home = emblem_magnitude(items, kIpHome);
    const int base = catalog().settings.recall_time;
    return home > 0.0f ? static_cast<int>(std::ceil(static_cast<float>(base) * (1.0f - home) - 1e-6f)) : base;
}

int tribute_turns(std::uint32_t gems)
{
    const auto& s = catalog().settings;
    const int t = static_cast<int>(std::ceil(static_cast<float>(gems) * s.submit_gem_duration_multiplier - 1e-6f));
    return std::max(s.submit_gem_minimum_duration, t);
}

StatGain buy_gain(int champion_type, const std::array<std::uint8_t, kMaxItems>& items, int item_id)
{
    StatGain g;
    const auto& cat = catalog();
    const auto& d = cat.champion(champion_type);
    const auto& it = cat.item(item_id);
    const auto& info = item_info(item_id);
    if (info.kind != ik_shard) return g;
    g.health = static_cast<float>(it.health) * d.health_growth;
    g.strength = static_cast<float>(it.strength) * d.strength_growth;
    g.focus = static_cast<float>(it.focus) * d.focus_growth;
    // The fifth shard of a family and stat combines into the emblem: the emblem's points replace the five shards'.
    if (it.combine_count > 0 && items[static_cast<std::size_t>(item_id)] + 1u >= it.combine_count && it.result >= 0)
    {
        const auto& em = cat.item(it.result);
        const float shards = static_cast<float>(it.combine_count);
        g.health = (static_cast<float>(em.health) - shards * static_cast<float>(it.health) + static_cast<float>(it.health)) * d.health_growth;
        g.strength = (static_cast<float>(em.strength) - shards * static_cast<float>(it.strength) + static_cast<float>(it.strength)) * d.strength_growth;
        g.focus = (static_cast<float>(em.focus) - shards * static_cast<float>(it.focus) + static_cast<float>(it.focus)) * d.focus_growth;
        g.completes_emblem = true;
    }
    return g;
}

DamageRates rates(int champion_type, float strength, float focus, const std::int8_t cooldown[2], bool ready_unknown_as_ready)
{
    DamageRates r;
    const auto& d = catalog().champion(champion_type);
    r.basic = strength;
    float sustained = strength;
    int dash = 0;
    for (int k = 0; k < 2; ++k)
    {
        const auto& a = d.ability[k];
        const bool ready = cooldown[k] == 0 || (cooldown[k] < 0 && ready_unknown_as_ready);
        const float cd = std::max<float>(1.0f, static_cast<float>(a.cooldown));
        switch (a.kind)
        {
        case ab_damage:
        case ab_aoe:
        case ab_cc:
            if (ready) r.burst += a.modifier * focus;
            sustained += a.modifier * focus / cd;
            break;
        case ab_dash:
            if (a.modifier > 0.0f)
            {
                if (ready) r.burst += a.modifier * focus;
                sustained += a.modifier * focus / cd;
            }
            if (ready) dash = std::max(dash, static_cast<int>(a.range));
            break;
        case ab_reset:
            if (ready) r.burst += strength;
            sustained += strength / cd;
            break;
        default: break;
        }
    }
    r.burst += r.basic;
    r.sustained = sustained;
    const int range = static_cast<int>(d.attack_range);
    r.reach = (d.attack_type == 0 ? range + 1 : range) + dash;
    return r;
}

ClearForecast clear(const Map& map, int group, const float* member_hp, float dps)
{
    ClearForecast f;
    const auto& gr = map.groups[static_cast<std::size_t>(group)];
    const auto& cat = catalog();
    if (dps <= 0.0f) { f.rounds = kUnreachable; f.damage = 1e9f; return f; }
    // Smith order (highest strength per full health first), fixed per group: computed once.
    static std::vector<std::vector<int>> orders;
    if (orders.size() != map.groups.size()) orders.assign(map.groups.size(), {});
    auto& order = orders[static_cast<std::size_t>(group)];
    if (order.size() != gr.members.size())
    {
        order.resize(gr.members.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            const auto& da = cat.camp(gr.members[static_cast<std::size_t>(a)].camp_type);
            const auto& db = cat.camp(gr.members[static_cast<std::size_t>(b)].camp_type);
            return da.strength * db.health > db.strength * da.health;
        });
    }
    float t = 0.0f, live_strength = 0.0f;
    for (int i : order)
        if (member_hp[i] > 0.0f) live_strength += cat.camp(gr.members[static_cast<std::size_t>(i)].camp_type).strength;
    for (int i : order)
    {
        if (member_hp[i] <= 0.0f) continue;
        const float rounds = member_hp[i] / dps;
        f.damage += live_strength * rounds;
        t += rounds;
        live_strength -= cat.camp(gr.members[static_cast<std::size_t>(i)].camp_type).strength;
    }
    f.rounds = static_cast<int>(std::ceil(t));
    return f;
}

int camp_target(Tile camp, int range, const std::vector<Tile>& positions, const std::vector<int>& ids)
{
    int best = -1, best_d = 1 << 20;
    for (std::size_t i = 0; i < positions.size(); ++i)
    {
        const int d = cheb(camp, positions[i]);
        if (d > range) continue;
        if (d < best_d || (d == best_d && best >= 0 && ids[i] < ids[static_cast<std::size_t>(best)]))
        {
            best_d = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

// Symmetric shadowcasting (Albert Ford): four quadrants, rows outward, slopes as fractions.
namespace {

struct Row
{
    int depth;
    float start, end;  // slopes
};

}  // namespace

void vision(const Map& map, Tile from, int r, std::vector<std::uint8_t>& seen)
{
    seen.assign(static_cast<std::size_t>(map.w * map.h), 0);
    if (!map.inside(from)) return;
    seen[static_cast<std::size_t>(map.idx(from))] = 1;
    const bool in_bush = map.at(from) & tf_bush;
    // The bush cluster we stand in is transparent to us (connected bush tiles, 8-neighbourhood).
    std::vector<std::uint8_t> own_bush;
    if (in_bush)
    {
        own_bush.assign(seen.size(), 0);
        std::vector<int> q{ map.idx(from) };
        own_bush[static_cast<std::size_t>(map.idx(from))] = 1;
        for (std::size_t h = 0; h < q.size(); ++h)
        {
            const Tile t{ static_cast<std::int16_t>(q[h] % map.w), static_cast<std::int16_t>(q[h] / map.w) };
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const Tile u{ static_cast<std::int16_t>(t.x + dx), static_cast<std::int16_t>(t.y + dy) };
                    if (!map.inside(u) || !(map.at(u) & tf_bush) || own_bush[static_cast<std::size_t>(map.idx(u))]) continue;
                    own_bush[static_cast<std::size_t>(map.idx(u))] = 1;
                    q.push_back(map.idx(u));
                }
        }
    }
    auto opaque = [&](Tile t) {
        if (!map.inside(t)) return true;
        const auto f = map.at(t);
        if (f & tf_wall) return true;
        if (f & tf_bush) return !(in_bush && own_bush[static_cast<std::size_t>(map.idx(t))]);
        return false;
    };
    for (int q = 0; q < 4; ++q)
    {
        auto transform = [&](int row, int col) -> Tile {
            switch (q)
            {
            case 0: return { static_cast<std::int16_t>(from.x + col), static_cast<std::int16_t>(from.y - row) };
            case 1: return { static_cast<std::int16_t>(from.x + row), static_cast<std::int16_t>(from.y + col) };
            case 2: return { static_cast<std::int16_t>(from.x + col), static_cast<std::int16_t>(from.y + row) };
            default: return { static_cast<std::int16_t>(from.x - row), static_cast<std::int16_t>(from.y + col) };
            }
        };
        std::vector<Row> rows{ { 1, -1.0f, 1.0f } };
        while (!rows.empty())
        {
            Row row = rows.back();
            rows.pop_back();
            if (row.depth > r) continue;
            const int min_col = static_cast<int>(std::floor(row.depth * row.start + 0.5f));
            const int max_col = static_cast<int>(std::ceil(row.depth * row.end - 0.5f));
            bool prev_set = false, prev_opaque = false;
            for (int col = min_col; col <= max_col; ++col)
            {
                const Tile t = transform(row.depth, col);
                const bool op = opaque(t);
                const bool symmetric = static_cast<float>(col) >= row.depth * row.start && static_cast<float>(col) <= row.depth * row.end;
                if (map.inside(t) && (op || symmetric) && cheb(from, t) <= r) seen[static_cast<std::size_t>(map.idx(t))] = 1;
                if (prev_set && prev_opaque && !op) row.start = (2.0f * col - 1.0f) / (2.0f * row.depth);
                if (prev_set && !prev_opaque && op) rows.push_back({ row.depth + 1, row.start, (2.0f * col - 1.0f) / (2.0f * row.depth) });
                prev_set = true;
                prev_opaque = op;
            }
            if (prev_set && !prev_opaque) rows.push_back({ row.depth + 1, row.start, row.end });
        }
    }
}

}  // namespace zero::rules
