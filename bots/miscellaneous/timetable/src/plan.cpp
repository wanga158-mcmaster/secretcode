#include "plan.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace tt {

namespace {

constexpr std::uint8_t kMagic0 = 'T', kMagic1 = 'T';
constexpr int kCassi = 3, kIssac = 4; // cambc::champion_type values

std::uint8_t checksum(const std::uint8_t* p, std::size_t n)
{
    std::uint32_t h = 2166136261u;
    for (std::size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 16777619u;
    return static_cast<std::uint8_t>(h ^ (h >> 8) ^ (h >> 16) ^ (h >> 24));
}

// Distances travel as one byte in units of two tiles (enough for 100 x 100 maps).
std::uint8_t pack_dist(int d) { return static_cast<std::uint8_t>(std::clamp((d + 1) / 2, 0, 255)); }
int unpack_dist(std::uint8_t v) { return v >= 255 ? kInf : 2 * v; }

float sturdiness(const BodySpec& b)
{
    return b.hp * (b.range <= 1 ? 1.2f : 1.0f) * (b.fragile ? 0.8f : 1.0f);
}

} // namespace

std::size_t encode_plan(const Plan& p, std::uint8_t* out, std::size_t cap)
{
    std::size_t k = 0;
    auto put = [&](int v) {
        if (k < cap) out[k] = static_cast<std::uint8_t>(v);
        ++k;
    };
    put(kMagic0); put(kMagic1); put(kPlanVersion);
    put(p.n_groups); put(p.n_bodies); put(p.n_units); put(p.structure); put(p.width); put(p.height);
    for (int i = 0; i < kMaxBodies; ++i) put(p.roster[i] < 0 ? 255 : p.roster[i]);
    for (int i = 0; i < kMaxBodies; ++i) put(p.body_unit[i]);
    put(p.bank_gems);
    put(static_cast<int>(std::lround(p.gold_weight * 100.0f)));
    put(p.est_rounds & 0xFF); put((p.est_rounds >> 8) & 0xFF);
    for (int u = 0; u < kMaxUnits; ++u) put(static_cast<int>(std::clamp(p.est_rate[u] * 100.0f, 0.0f, 255.0f)));
    for (int g = 0; g < p.n_groups; ++g)
        put((p.owner[g] & 7) | (p.party[g] ? 0x08 : 0) | (p.ours[g] ? 0x20 : 0));
    for (int g = 0; g < p.n_groups; ++g) put(std::clamp(p.tour[g], 0, 255));
    put(p.n_matrix);
    for (int i = 0; i < p.n_matrix; ++i) put(p.matrix_group[i]);
    for (int i = 0; i < p.n_matrix; ++i)
        for (int j = i + 1; j < p.n_matrix; ++j) put(pack_dist(p.mdist[i][j]));
    if (k + 1 > cap) return 0;
    out[k] = checksum(out, k);
    return k + 1;
}

Plan decode_plan(const std::uint8_t* in, std::size_t size)
{
    Plan p;
    decode_plan_into(in, size, p);
    return p;
}

void decode_plan_into(const std::uint8_t* in, std::size_t size, Plan& p)
{
    p.valid = false;
    std::size_t k = 0;
    bool bad = false;
    auto get = [&]() -> int {
        if (k >= size) { bad = true; return 0; }
        return in[k++];
    };
    if (get() != kMagic0 || get() != kMagic1 || get() != kPlanVersion) return;
    p.n_groups = get(); p.n_bodies = get(); p.n_units = get(); p.structure = get(); p.width = get(); p.height = get();
    if (p.n_groups > kMaxGroups || p.n_bodies > kMaxBodies || p.n_units > kMaxUnits) return;
    for (int i = 0; i < kMaxBodies; ++i) { const int v = get(); p.roster[i] = v == 255 ? -1 : v; }
    for (int i = 0; i < kMaxBodies; ++i) p.body_unit[i] = get();
    p.bank_gems = get();
    p.gold_weight = get() / 100.0f;
    p.est_rounds = get(); p.est_rounds |= get() << 8;
    for (int u = 0; u < kMaxUnits; ++u) p.est_rate[u] = get() / 100.0f;
    for (int g = 0; g < p.n_groups; ++g) {
        const int v = get();
        p.owner[g] = v & 7;
        p.party[g] = (v & 0x08) != 0;
        p.ours[g] = (v & 0x20) != 0;
    }
    for (int g = 0; g < p.n_groups; ++g) p.tour[g] = get();
    p.n_matrix = get();
    if (p.n_matrix > kMaxMatrix) return;
    for (int g = 0; g < kMaxGroups; ++g) p.matrix_index[g] = -1;
    for (int i = 0; i < p.n_matrix; ++i) {
        p.matrix_group[i] = get();
        if (p.matrix_group[i] >= p.n_groups) return;
        p.matrix_index[p.matrix_group[i]] = i;
    }
    for (int i = 0; i < p.n_matrix; ++i) {
        p.mdist[i][i] = 0;
        for (int j = i + 1; j < p.n_matrix; ++j) {
            const int d = unpack_dist(static_cast<std::uint8_t>(get()));
            p.mdist[i][j] = p.mdist[j][i] = static_cast<std::uint16_t>(std::min(d, 65535));
        }
    }
    if (bad || k >= size) return;
    if (in[k] != checksum(in, k)) return;
    p.valid = true;
    return;
}

// ---------------------------------------------------------------------------------------------

int form_units(const PlanInput& in, Structure s, UnitSpec* units)
{
    const int n = in.n_bodies;
    int order[kMaxBodies];
    for (int i = 0; i < n; ++i) order[i] = i;
    std::sort(order, order + n, [&](int a, int b) { return sturdiness(in.bodies[a]) > sturdiness(in.bodies[b]); });
    int assign[kMaxBodies];
    int n_units = 0;
    switch (s) {
    case Structure::solo:
        for (int i = 0; i < n; ++i) assign[order[i]] = i;
        n_units = n;
        break;
    case Structure::pairs: {
        // Sturdiest with least sturdy; an odd body out works alone.
        int lo = 0, hi = n - 1;
        while (lo < hi) { assign[order[lo]] = n_units; assign[order[hi]] = n_units; ++n_units; ++lo; --hi; }
        if (lo == hi) assign[order[lo]] = n_units++;
        break;
    }
    case Structure::trios: {
        const int k = n <= 3 ? 1 : 2;
        for (int i = 0; i < n; ++i) {
            // Snake order deals sturdy and fragile bodies evenly over the units.
            const int round = i / k, pos = i % k;
            assign[order[i]] = round % 2 == 0 ? pos : k - 1 - pos;
        }
        n_units = k;
        break;
    }
    default:
        for (int i = 0; i < n; ++i) assign[i] = 0;
        n_units = 1;
        break;
    }
    // Cassi and Issac share one fate: keep them in one unit when units have room.
    if (s != Structure::solo) {
        int c = -1, is = -1;
        for (int i = 0; i < n; ++i) {
            if (in.bodies[i].type == kCassi) c = i;
            if (in.bodies[i].type == kIssac) is = i;
        }
        if (c >= 0 && is >= 0 && assign[c] != assign[is]) {
            // Swap Issac with the member of Cassi's unit that is not Cassi (sturdiest such).
            for (int i = 0; i < n; ++i)
                if (i != c && assign[i] == assign[c]) { std::swap(assign[i], assign[is]); break; }
        }
    }
    for (int u = 0; u < n_units; ++u) units[u] = UnitSpec{};
    for (int i = 0; i < n; ++i) {
        UnitSpec& u = units[assign[i]];
        const BodySpec& b = in.bodies[i];
        u.body[u.n++] = i;
        u.dps += b.dps;
        u.pool += b.hp;
        u.best_hp = std::max(u.best_hp, b.hp);
        u.shield += b.shield;
        u.gold_mult += b.gold_mult;
        u.dash = u.dash && b.dash;
    }
    for (int k = 0; k < n_units; ++k) {
        UnitSpec& u = units[k];
        u.gold_mult = (u.gold_mult - 1.0f) / std::max(1, u.n);
        if (u.n >= 2) u.pool *= in.pool_share;
    }
    return n_units;
}

void Planner::prepare_groups()
{
    for (int g = 0; g < in_.n_groups; ++g) {
        // G1: the pool is shared. A contested group is worth the share of its yield we expect to
        // take, which falls with how much nearer the enemy is.
        const int lead = in_.d_theirs[g] - in_.d_ours[g];
        ours_[g] = in_.d_ours[g] < kInf && lead >= -(in_.territory_margin + in_.contest_margin)
            && in_.groups[g].gems + in_.groups[g].gold > 0;
        weight_[g] = std::clamp(0.5f + static_cast<float>(lead) / in_.contest_scale, in_.contest_floor, 1.0f);
        solo_[g] = in_.groups[g].harmless;
        for (int b = 0; b < in_.n_bodies && !solo_[g]; ++b) {
            const BodySpec& body = in_.bodies[b];
            Clear c = smith_fast(in_.groups[g], body.dps);
            const float dmg = solo_damage(in_.groups[g], body, c);
            const float margin = body.fragile ? in_.camp_margin * 1.5f : in_.camp_margin;
            if (dmg * margin <= body.hp) solo_[g] = true;
        }
    }
}

void Planner::prepare_units(Structure s)
{
    n_units_ = form_units(in_, s, units_);
    for (int u = 0; u < n_units_; ++u) {
        const UnitSpec& unit = units_[u];
        for (int g = 0; g < in_.n_groups; ++g) {
            const GroupSpec& grp = in_.groups[g];
            const Clear c = smith_fast(grp, unit.dps);
            clears_[u][g] = c;
            damage_[u][g] = grp.harmless ? 0.0f : std::max(0.0f, c.damage - unit.shield * c.rounds);
            const float cap = unit.n >= 2 ? unit.pool : unit.best_hp;
            feasible_[u][g] = grp.harmless || damage_[u][g] * in_.camp_margin <= cap * static_cast<float>(in_.siege_max_trips);
            value_[u][g] = (grp.gems + in_.gold_weight * grp.gold * unit.gold_mult) * weight_[g];
        }
        score_[u] = 0;
    }
    for (int g = 0; g < in_.n_groups; ++g) owner_[g] = kNoOwner;
}

Circuit Planner::circuit(int u, const int* groups, int m) const
{
    Circuit c;
    if (m <= 0) return c;
    const UnitSpec& unit = units_[u];
    const float n = static_cast<float>(std::max(1, unit.n));
    ops_ += m * 4;
    int min_altar = kInf;
    float longest_need = 0;
    for (int i = 0; i < m; ++i) {
        const int g = groups[i];
        const GroupSpec& grp = in_.groups[g];
        const Clear& cl = clears_[u][g];
        // Groups one member can take are split over the members; the rest take the whole unit.
        const float work = solo_[g] ? cl.rounds + in_.capture_rounds / n : cl.rounds + in_.capture_rounds;
        c.work += work;
        c.gems += grp.gems;
        c.value += value_[u][g];
        if (damage_[u][g] > 0) c.heal += damage_[u][g] / std::max(1.0f, unit.pool) * (6.0f + walk_rounds(in_.d_ours[g], unit.dash));
        if (m > 1) c.travel += walk_rounds(in_.dist[g][groups[(i + 1) % m]], unit.dash);
        min_altar = std::min(min_altar, in_.d_altar[g]);
        longest_need = std::max(longest_need, grp.cooldown + work);
    }
    // Every gem needs channel time; each bag also needs a detour to the altar nearest the circuit.
    const float bags = c.gems / std::max(10.0f, static_cast<float>(in_.bank_gems));
    c.bank = (c.gems / 10.0f + (min_altar < kInf ? bags * walk_rounds(2 * min_altar, unit.dash) : 0.0f)) / n;
    // A group cannot be taken again before it respawns: short circuits wait.
    const float t = c.travel + c.work + c.bank + c.heal;
    if (t < longest_need) c.wait = longest_need - t;
    return c;
}

void Planner::order_tour(int* groups, int m) const
{
    if (m <= 2) return;
    ops_ += m * m * 2;
    // Nearest neighbour from the group nearest our spawn, then 2-opt.
    int start = 0;
    for (int i = 1; i < m; ++i)
        if (in_.d_ours[groups[i]] < in_.d_ours[groups[start]]) start = i;
    std::swap(groups[0], groups[start]);
    for (int i = 1; i < m; ++i) {
        int best = i;
        for (int j = i + 1; j < m; ++j)
            if (in_.dist[groups[i - 1]][groups[j]] < in_.dist[groups[i - 1]][groups[best]]) best = j;
        std::swap(groups[i], groups[best]);
    }
    for (int pass = 0; pass < 3; ++pass) {
        bool changed = false;
        for (int i = 0; i + 1 < m; ++i)
            for (int j = i + 2; j < m; ++j) {
                const int a = groups[i], a2 = groups[i + 1], c = groups[j], c2 = groups[(j + 1) % m];
                if (c2 == a) continue;
                const int before = in_.dist[a][a2] + in_.dist[c][c2];
                const int after = in_.dist[a][c] + in_.dist[a2][c2];
                ops_ += 4;
                if (after < before) {
                    std::reverse(groups + i + 1, groups + j + 1);
                    changed = true;
                }
            }
        if (!changed) break;
    }
}

float Planner::unit_score(int u) const
{
    int set[kMaxGroups];
    int m = 0;
    for (int g = 0; g < in_.n_groups; ++g)
        if (owner_[g] == u) set[m++] = g;
    if (m == 0) return 0.0f;
    order_tour(set, m);
    return circuit(u, set, m).value_rate();
}

void Planner::initial_assignment()
{
    // Greedy insertion by value: each group goes to the unit whose circuit gains most.
    int order[kMaxGroups];
    int n = 0;
    for (int g = 0; g < in_.n_groups; ++g)
        if (ours_[g]) order[n++] = g;
    std::sort(order, order + n, [&](int a, int c) {
        return (in_.groups[a].gems + in_.groups[a].gold * in_.gold_weight) * weight_[a]
            > (in_.groups[c].gems + in_.groups[c].gold * in_.gold_weight) * weight_[c];
    });
    for (int i = 0; i < n && !low(); ++i) {
        const int g = order[i];
        int best = kNoOwner;
        float gain = 0.0f, best_score = 0.0f;
        for (int u = 0; u < n_units_; ++u) {
            if (!feasible_[u][g]) continue;
            owner_[g] = u;
            const float s = unit_score(u);
            owner_[g] = kNoOwner;
            if (s - score_[u] > gain) { gain = s - score_[u]; best = u; best_score = s; }
        }
        if (best != kNoOwner) {
            owner_[g] = best;
            score_[best] = best_score;
        }
    }
}

bool Planner::improve_pass(long budget)
{
    bool improved = false;
    for (int g = 0; g < in_.n_groups && budget > ops_ && !low(); ++g) {
        if (!ours_[g]) continue;
        const int from = owner_[g];
        float best_gain = 1e-4f;
        int best_to = from;
        float best_from_score = 0, best_to_score = 0;
        float from_without = 0.0f;
        if (from != kNoOwner) {
            owner_[g] = kNoOwner;
            from_without = unit_score(from);
            owner_[g] = from;
            const float gain = from_without - score_[from];
            if (gain > best_gain) { best_gain = gain; best_to = kNoOwner; best_from_score = from_without; }
        }
        for (int u = 0; u < n_units_; ++u) {
            if (u == from || !feasible_[u][g]) continue;
            owner_[g] = u;
            const float with = unit_score(u);
            owner_[g] = from;
            const float gain = (with - score_[u]) + (from != kNoOwner ? from_without - score_[from] : 0.0f);
            if (gain > best_gain) {
                best_gain = gain;
                best_to = u;
                best_from_score = from_without;
                best_to_score = with;
            }
        }
        if (best_to != from) {
            owner_[g] = best_to;
            if (from != kNoOwner) score_[from] = best_from_score;
            if (best_to != kNoOwner) score_[best_to] = best_to_score;
            improved = true;
        }
    }
    return improved;
}

Plan Planner::build_plan(Structure s) const
{
    Plan p;
    p.valid = true;
    p.width = in_.width;
    p.height = in_.height;
    p.n_groups = in_.n_groups;
    p.n_bodies = in_.n_bodies;
    p.n_units = n_units_;
    p.structure = static_cast<int>(s);
    for (int b = 0; b < in_.n_bodies; ++b) p.roster[b] = in_.bodies[b].type;
    for (int u = 0; u < n_units_; ++u)
        for (int i = 0; i < units_[u].n; ++i) p.body_unit[units_[u].body[i]] = u;
    p.gold_weight = in_.gold_weight;
    p.bank_gems = in_.bank_gems;
    float total_rate = 0;
    int first_walk = 0, owners = 0;
    for (int u = 0; u < n_units_; ++u) {
        int set[kMaxGroups];
        int m = 0;
        for (int g = 0; g < in_.n_groups; ++g)
            if (owner_[g] == u) set[m++] = g;
        order_tour(set, m);
        for (int i = 0; i < m; ++i) p.tour[set[i]] = i;
        const Circuit c = circuit(u, set, m);
        p.est_rate[u] = c.rate();
        total_rate += c.rate();
        if (m > 0) { first_walk += in_.d_ours[set[0]]; ++owners; }
    }
    for (int g = 0; g < in_.n_groups; ++g) {
        p.owner[g] = owner_[g];
        p.party[g] = !solo_[g];
        p.ours[g] = ours_[g];
    }
    const float startup = owners > 0 ? static_cast<float>(first_walk) / owners : 0.0f;
    p.est_rounds = total_rate > 0 ? static_cast<int>(std::min(2000.0f, startup + 1000.0f / total_rate)) : 2000;
    // Matrix groups: owned groups first (by owner, tour), then other territory groups by value.
    int order[kMaxGroups];
    int n = 0;
    for (int g = 0; g < in_.n_groups; ++g)
        if (owner_[g] != kNoOwner || ours_[g]) order[n++] = g;
    std::sort(order, order + n, [&](int a, int c) {
        const int oa = owner_[a] == kNoOwner ? 99 : owner_[a], oc = owner_[c] == kNoOwner ? 99 : owner_[c];
        if (oa != oc) return oa < oc;
        if (oa != 99) return p.tour[a] < p.tour[c];
        return in_.groups[a].gems > in_.groups[c].gems;
    });
    p.n_matrix = std::min(n, kMaxMatrix);
    for (int g = 0; g < kMaxGroups; ++g) p.matrix_index[g] = -1;
    for (int i = 0; i < p.n_matrix; ++i) {
        p.matrix_group[i] = order[i];
        p.matrix_index[order[i]] = i;
    }
    for (int i = 0; i < p.n_matrix; ++i)
        for (int j = 0; j < p.n_matrix; ++j)
            p.mdist[i][j] = in_.dist[p.matrix_group[i]][p.matrix_group[j]];
    return p;
}

Plan Planner::solve(long budget)
{
    ops_ = 0;
    prepare_groups();
    Plan best;
    float best_score = -1.0f;
    const int count = static_cast<int>(Structure::count);
    done_ = 0;
    for (int si = 0; si < count; ++si) {
        if (in_.structure >= 0 && si != in_.structure) continue;
        const Structure s = static_cast<Structure>(si);
        if (s == Structure::trios && in_.n_bodies < 3) continue;
        if (s != Structure::solo && in_.n_bodies < 2) continue;
        if (low()) break;
        prepare_units(s);
        initial_assignment();
        if (low()) break;
        const long share = budget / count;
        const long stop = ops_ + share;
        for (int pass = 0; pass < 6 && ops_ < stop && !low(); ++pass)
            if (!improve_pass(stop)) break;
        ++done_;
        float total = 0;
        for (int u = 0; u < n_units_; ++u) total += score_[u];
        scores_[si] = total;
        if (total > best_score) {
            best_score = total;
            best = build_plan(s);
        }
    }
    return best;
}

} // namespace tt
