// Economy surrogate arithmetic (econ_model.h). SDK-free: shared by the bot and the native harness.
#include "econ_model.h"

#include <algorithm>
#include <cmath>

namespace mb::econ {

namespace {

inline int cheb_t(int w, int a, int b)
{
    const int ax = a % w, ay = a / w, bx = b % w, by = b / w;
    const int dx = ax > bx ? ax - bx : bx - ax, dy = ay > by ? ay - by : by - ay;
    return dx > dy ? dx : dy;
}

} // namespace

void finish_group(Group& g)
{
    const int n = static_cast<int>(g.members.size());
    std::vector<int> order(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) order[static_cast<std::size_t>(i)] = i;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const auto& ma = g.members[static_cast<std::size_t>(a)];
        const auto& mb2 = g.members[static_cast<std::size_t>(b)];
        const float l = ma.str * std::max(mb2.hp, 1.0f), r = mb2.str * std::max(ma.hp, 1.0f);
        return l != r ? l > r : a < b;
    });
    g.H = g.W = g.S = 0;
    g.max_range = 0;
    g.max_str = 0;
    g.harmless = true;
    g.elk = g.big = false;
    float nbw = 0;
    long sx = 0, sy = 0;
    g.x0 = g.y0 = 1 << 20;
    g.x1 = g.y1 = -1;
    for (const auto& m : g.members)
    {
        g.x0 = std::min(g.x0, m.x);
        g.y0 = std::min(g.y0, m.y);
        g.x1 = std::max(g.x1, m.x);
        g.y1 = std::max(g.y1, m.y);
    }
    for (int k = 0; k < n; ++k)
    {
        const auto& m = g.members[static_cast<std::size_t>(order[static_cast<std::size_t>(k)])];
        g.H += m.hp;
        g.W += m.str * g.H;
        g.S += m.str;
        if (m.str > 0)
        {
            g.harmless = false;
            g.max_range = std::max(g.max_range, m.range);
            g.max_str = std::max(g.max_str, m.str);
        }
        if (m.type == 1) g.elk = true;
        if (m.type == 4 || m.type == 5) g.big = true;
        int cnt = 0;  // neighbours still alive while this member is the target
        for (int j = k + 1; j < n; ++j)
        {
            const auto& o = g.members[static_cast<std::size_t>(order[static_cast<std::size_t>(j)])];
            if (std::max(std::abs(o.x - m.x), std::abs(o.y - m.y)) <= 1) ++cnt;
        }
        nbw += m.hp * static_cast<float>(cnt);
        sx += m.x;
        sy += m.y;
    }
    g.nb = g.H > 0 ? nbw / g.H : 0;
    if (n > 0)
    {
        const float mx = static_cast<float>(sx) / n, my = static_cast<float>(sy) / n;
        float best = 1e9f;
        for (const auto& m : g.members)
        {
            const float d = std::fabs(m.x - mx) + std::fabs(m.y - my);
            if (d < best)
            {
                best = d;
                g.cx = m.x;
                g.cy = m.y;
            }
        }
    }
}

void field_walls(const StaticModel& m, int group, std::vector<std::uint8_t>& walls)
{
    walls = m.blocked;
    const int n = m.width * m.height;
    for (int t = 0; t < n; ++t)
    {
        const int g = m.camp_tile[static_cast<std::size_t>(t)];
        if (g >= 0 && g != group && !m.groups[static_cast<std::size_t>(g)].harmless) walls[static_cast<std::size_t>(t)] = 1;
    }
}

namespace {

void point_roots(const StaticModel& m, int p, std::vector<int>& roots)
{
    roots.clear();
    const int G = m.G();
    if (p == 0)
        roots = m.spawn;
    else if (p <= G)
        roots = m.groups[static_cast<std::size_t>(p - 1)].tiles;
    else
    {
        const int a = m.altars[static_cast<std::size_t>(p - 1 - G)];
        const int r = m.rules.submit_range, ax = a % m.width, ay = a / m.width;
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx)
            {
                const int x = ax + dx, y = ay + dy;
                if (x < 0 || y < 0 || x >= m.width || y >= m.height) continue;
                const int t = y * m.width + x;
                if (!m.is_blocked(t) && m.camp_tile[static_cast<std::size_t>(t)] < 0) roots.push_back(t);
            }
    }
}

} // namespace

void compute_fields_native(StaticModel& m)
{
    const int n = m.width * m.height, G = m.G();
    m.n_points = 1 + G + m.A();
    m.fields.assign(static_cast<std::size_t>(m.n_points), {});
    std::vector<std::uint8_t> walls;
    std::vector<int> roots, q(static_cast<std::size_t>(n));
    for (int p = 0; p < m.n_points; ++p)
    {
        field_walls(m, p >= 1 && p <= G ? p - 1 : -1, walls);
        point_roots(m, p, roots);
        auto& f = m.fields[static_cast<std::size_t>(p)];
        f.assign(static_cast<std::size_t>(n), kFar);
        int head = 0, tail = 0;
        for (int r : roots)
            if (f[static_cast<std::size_t>(r)] == kFar)
            {
                f[static_cast<std::size_t>(r)] = 0;
                q[static_cast<std::size_t>(tail++)] = r;
            }
        while (head < tail)
        {
            const int t = q[static_cast<std::size_t>(head++)], x = t % m.width, y = t / m.width;
            const std::uint16_t nd = static_cast<std::uint16_t>(f[static_cast<std::size_t>(t)] + 1);
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                {
                    if (!dx && !dy) continue;
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= m.width || ny >= m.height) continue;
                    const int u = ny * m.width + nx;
                    if (walls[static_cast<std::size_t>(u)] || f[static_cast<std::size_t>(u)] <= nd) continue;
                    f[static_cast<std::size_t>(u)] = nd;
                    q[static_cast<std::size_t>(tail++)] = u;
                }
        }
    }
}

bool StaticModel::has_field(int p) const
{
    if (p < static_cast<int>(fields.size()) && !fields[static_cast<std::size_t>(p)].empty()) return true;
    return getter && p < static_cast<int>(have.size()) && have[static_cast<std::size_t>(p)];
}

int StaticModel::raw(int p, int tile) const
{
    if (p < static_cast<int>(fields.size()) && !fields[static_cast<std::size_t>(p)].empty()) return fields[static_cast<std::size_t>(p)][static_cast<std::size_t>(tile)];
    return getter(p, tile);
}

int StaticModel::tile_to_point(int tile, int p) const
{
    const int G = this->G();
    // Groups: standing beside a member is enough (melee reach); the field counts steps onto it.
    const int adj = (p >= 1 && p <= G) ? 1 : 0;
    if (has_field(p))
    {
        int v = raw(p, tile);
        if (v == kFar)
        {
            // On a tile that is a wall in this field (another group's camp tile): via a neighbour.
            const int x = tile % width, y = tile / width;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
                    const int u = raw(p, ny * width + nx);
                    if (u != kFar && u + 1 < v) v = u + 1;
                }
        }
        if (v == kFar) return kFar;
        return std::max(0, v - adj);
    }
    const int c = cheb_t(width, tile, rep[static_cast<std::size_t>(p)]);
    return static_cast<int>(std::ceil(static_cast<float>(std::max(0, c - adj)) * detour));
}

void StaticModel::pack_blocked()
{
    const int n = width * height;
    blocked_bits.assign(static_cast<std::size_t>((n + 7) / 8), 0);
    for (int t = 0; t < n; ++t)
        if (blocked[static_cast<std::size_t>(t)]) blocked_bits[static_cast<std::size_t>(t >> 3)] |= static_cast<std::uint8_t>(1u << (t & 7));
}

int StaticModel::dist_slow(int a, int b) const
{
    int v;
    if (a == b)
        v = 0;
    else if (a == 0)
    {
        v = kFar;
        for (int t : spawn) v = std::min(v, tile_to_point(t, b));
    }
    else
        v = tile_to_point(rep[static_cast<std::size_t>(a)], b);
    v = std::min(v, static_cast<int>(kFar));
    dist[static_cast<std::size_t>(a * n_points + b)] = static_cast<std::uint16_t>(v);
    return v;
}

void finish_distances(StaticModel& m)
{
    const int G = m.G(), A = m.A();
    m.n_points = 1 + G + A;
    m.rep.assign(static_cast<std::size_t>(m.n_points), 0);
    // Spawn representative: the spawn tile nearest the spawn centroid.
    {
        long sx = 0, sy = 0;
        for (int t : m.spawn)
        {
            sx += t % m.width;
            sy += t / m.width;
        }
        const int n = std::max<int>(1, static_cast<int>(m.spawn.size()));
        const int cx = static_cast<int>(sx / n), cy = static_cast<int>(sy / n);
        int best = 1 << 30;
        for (int t : m.spawn)
        {
            const int d = std::max(std::abs(t % m.width - cx), std::abs(t / m.width - cy));
            if (d < best)
            {
                best = d;
                m.rep[0] = t;
            }
        }
    }
    // Group and altar representatives: a free tile beside the member (or altar) nearest our spawn
    // centre (Chebyshev; no field reads, no scans beyond one 3x3 or submit-range box).
    const int s0 = m.rep[0];
    for (int p = 1; p < m.n_points; ++p)
    {
        int centre, radius = 1;
        if (p <= G)
        {
            const auto& tiles = m.groups[static_cast<std::size_t>(p - 1)].tiles;
            centre = tiles.empty() ? s0 : tiles[0];
            int bd = 1 << 30;
            for (int t : tiles)
            {
                const int d = cheb_t(m.width, t, s0);
                if (d < bd)
                {
                    bd = d;
                    centre = t;
                }
            }
        }
        else
        {
            centre = m.altars[static_cast<std::size_t>(p - 1 - G)];
            radius = std::max(1, m.rules.submit_range);
        }
        int best = centre, bd = 1 << 30;
        const int x0 = centre % m.width, y0 = centre / m.width;
        for (int dy = -radius; dy <= radius; ++dy)
            for (int dx = -radius; dx <= radius; ++dx)
            {
                const int x = x0 + dx, y = y0 + dy;
                if (x < 0 || y < 0 || x >= m.width || y >= m.height) continue;
                const int t = y * m.width + x;
                if (m.is_blocked(t)) continue;
                const int g = m.camp_tile[static_cast<std::size_t>(t)];
                if (g >= 0 && !m.groups[static_cast<std::size_t>(g)].harmless) continue;
                const int d = cheb_t(m.width, t, s0);
                if (d < bd)
                {
                    bd = d;
                    best = t;
                }
            }
        m.rep[static_cast<std::size_t>(p)] = best;
    }
    m.dist.assign(static_cast<std::size_t>(m.n_points * m.n_points), StaticModel::kUnset);
    m.ready = true;
}

void update_column(StaticModel& m, int b)
{
    for (int a = 0; a < m.n_points; ++a) m.dist[static_cast<std::size_t>(a * m.n_points + b)] = StaticModel::kUnset;
}

void refresh_stats(const StaticModel& m, int slot, Champ& c)
{
    const Design& d = m.roster[static_cast<std::size_t>(slot)];
    const Rules& r = m.rules;
    float pts[3]{};  // h, s, f
    bool emblem[kMaxFamilies]{};
    for (int f = 0; f < r.n_families; ++f)
        for (int s = 0; s < 3; ++s)
        {
            const int n = c.shards[f][s];
            if (!n) continue;
            const int full = n / 5, rest = n % 5;
            for (int k = 0; k < 3; ++k) pts[k] += full * r.fam[f].emblem_pts[s][k] + rest * r.fam[f].shard_pts[s][k];
            if (full) emblem[f] = true;
        }
    for (int f = 0; f < r.n_families && f < 16; ++f)
        if (c.emblem_mask >> f & 1) emblem[f] = true;
    const float old_max = c.maxhp;
    c.maxhp = d.hp + pts[0] * d.ghp;
    c.str = d.str + pts[1] * d.gstr;
    c.foc = d.foc + pts[2] * d.gfoc;
    if (d.passive == pa_health_strength) c.str += d.passive_mag * (c.maxhp - d.hp);
    c.hp = old_max > 0 ? c.hp + (c.maxhp - old_max) : c.maxhp;
    c.taken_mult = 1.0f;
    c.gold_mult = 1.0f + (d.passive == pa_bonus_gold ? d.passive_mag : 0.0f);
    float monster = 0;
    c.recall_turns = r.recall_time;
    for (int f = 0; f < r.n_families; ++f)
    {
        if (!emblem[f]) continue;
        const auto& fam = r.fam[f];
        switch (fam.passive)
        {
        case fp_monster: monster = fam.magnitude; break;
        case fp_durability:
        case fp_jungle: c.taken_mult *= 1.0f - fam.magnitude; break;
        case fp_treasure: c.gold_mult += fam.magnitude; break;
        case fp_home: c.recall_turns = static_cast<int>(std::ceil(r.recall_time * (1.0f - fam.magnitude) - 1e-4f)); break;
        default: break;
        }
    }
    // Camp damage per round = a + b x living neighbours of the target (economy E2): basic x (1 +
    // resets / cooldown) (+ splash on neighbours), damaging casts / cooldown (area ones on neighbours).
    float reset = 0, single = 0, area = 0;
    c.shield_amt = c.shield_dur = c.shield_cd = 0;
    for (const auto& a : d.ab)
    {
        const float cd = static_cast<float>(std::max(1, a.cooldown));
        switch (a.kind)
        {
        case ab_reset: reset += 1.0f / cd; break;
        case ab_damage:
        case ab_cc:
            if (a.hits_camps) single += a.modifier * c.foc / cd;
            break;
        case ab_aoe:
            if (a.hits_camps) area += a.modifier * c.foc / cd;
            break;
        case ab_dash:
            if (a.modifier > 0) area += a.modifier * c.foc / cd;
            break;
        case ab_shield:
            c.shield_amt = a.modifier * c.foc;
            c.shield_dur = static_cast<float>(std::max(1, a.duration));
            c.shield_cd = cd;
            break;
        default: break;
        }
    }
    float mult = d.camp_mult * (1.0f + monster);
    if (d.passive == pa_missing_health) mult *= 1.15f;  // hurt in long clears (up to +50%)
    const float basic = c.str * (1.0f + reset);
    c.rate_a = (basic + single + area) * mult;
    c.rate_b = ((d.passive == pa_splash ? basic : 0.0f) + area) * mult;
}

std::vector<ShopStep> default_shop_plan(const StaticModel& m, int slot)
{
    const Design& d = m.roster[static_cast<std::size_t>(slot)];
    const Rules& r = m.rules;
    // Main stat: the one whose shard adds more camp damage.
    Champ c;
    refresh_stats(m, slot, c);
    const float base = c.rate_a + c.rate_b;
    int main = 0;
    if (r.fam_power >= 0)
    {
        Champ s = c, f = c;
        s.shards[r.fam_power][0] = 1;
        f.shards[r.fam_power][1] = 1;
        refresh_stats(m, slot, s);
        refresh_stats(m, slot, f);
        main = (f.rate_a + f.rate_b - base) > (s.rate_a + s.rate_b - base) ? 1 : 0;
    }
    std::vector<ShopStep> plan;
    auto add = [&](int fam, int stat, int n) {
        if (fam >= 0) plan.push_back({ static_cast<std::int8_t>(fam), static_cast<std::int8_t>(stat), static_cast<std::int8_t>(n) });
    };
    if (d.passive == pa_bonus_gold)
    {
        add(r.fam_treasure, main, 5);
        add(r.fam_jungle, main, 5);
        add(r.fam_monster, main, 5);
        add(r.fam_bruiser, main, 0);
    }
    else if (!d.melee)
    {
        add(r.fam_jungle, main, 5);
        add(r.fam_treasure, main, 5);
        add(r.fam_monster, main, 5);
        add(r.fam_bruiser, main, 0);
    }
    else
    {
        add(r.fam_jungle, main, 5);
        add(r.fam_treasure, main, 5);
        add(r.fam_bruiser, 2, 5);
        add(r.fam_monster, main, 5);
        add(r.fam_bruiser, main, 0);
    }
    return plan;
}

int shop(const StaticModel& m, int slot, Champ& c, const std::vector<ShopStep>& plan)
{
    const float cost = static_cast<float>(m.rules.shard_cost);
    int bought = 0;
    while (c.plan_pos < static_cast<int>(plan.size()) && c.gold >= cost)
    {
        const auto& st = plan[static_cast<std::size_t>(c.plan_pos)];
        auto& owned = c.shards[st.family][st.stat];
        if (st.shards != 0 && owned >= st.shards)
        {
            ++c.plan_pos;
            continue;
        }
        if (owned >= 250) break;
        ++owned;
        c.gold -= cost;
        ++bought;
    }
    if (bought) refresh_stats(m, slot, c);
    return bought;
}

ClearForecast forecast_clear(const StaticModel& m, const Champ* const* party, const int* slots, int n, int gi, float hs)
{
    const Group& g = m.groups[static_cast<std::size_t>(gi)];
    const Calib& k = calib();
    ClearForecast out;
    float d = 0;
    int tanks = 0;
    for (int i = 0; i < n; ++i)
    {
        d += party[i]->rate_a + party[i]->rate_b * g.nb;
        if (m.roster[static_cast<std::size_t>(slots[i])].melee) ++tanks;
    }
    d *= n == 1 ? k.rate_solo : k.rate_party;
    if (d <= 0) return out;
    const float H = g.H * hs;
    out.rounds = static_cast<int>(std::ceil(H / d - 1e-4f)) + k.clear_extra + k.party_extra * (n - 1);
    out.ok = true;
    if (g.harmless) return out;
    const float total = std::max(0.0f, g.W * hs / d - 0.5f * g.S) * k.damage_mult;
    const int share = tanks ? tanks : n;
    for (int i = 0; i < n; ++i)
    {
        const bool tank = tanks ? m.roster[static_cast<std::size_t>(slots[i])].melee : true;
        if (!tank) continue;
        const Champ& c = *party[i];
        float taken = total / static_cast<float>(share) * c.taken_mult;
        if (c.shield_cd > 0 && out.rounds > 0)
        {
            const float per = taken / static_cast<float>(out.rounds);
            const float casts = std::ceil(static_cast<float>(out.rounds) / c.shield_cd);
            taken = std::max(0.0f, taken - casts * std::min(c.shield_amt, c.shield_dur * per));
        }
        out.damage[i] = taken;
    }
    return out;
}

int walk_rounds(const StaticModel& m, const Champ& c, int p)
{
    const int raw = c.pt >= 0 ? m.d(c.pt, p) : m.tile_to_point(c.tile, p);
    if (raw >= kFar) return kFar;
    const Calib& k = calib();
    return raw == 0 ? 0 : static_cast<int>(static_cast<float>(raw) * k.walk_mult + 0.5f) + k.walk_extra;
}

int tribute_rounds(const Rules& r, int gems)
{
    const int t = static_cast<int>(std::ceil(gems * r.submit_mult - 1e-9)) + r.submit_base;
    return std::max(r.submit_min, t);
}

} // namespace mb::econ
