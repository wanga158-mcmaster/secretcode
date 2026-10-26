#include "context.h"
#include <cmath>
#include "catalog.h"

namespace zero {

namespace {

// Compile-time 2^(-1/H): both the native and the wasm build fold the same constant.
constexpr double cexp(double x)
{
    double term = 1.0, sum = 1.0;
    for (int k = 1; k < 30; ++k) { term *= x / k; sum += term; }
    return sum;
}
constexpr float half_life(double h) { return static_cast<float>(cexp(-0.6931471805599453 / h)); }

constexpr float kD60 = half_life(60.0), kD100 = half_life(100.0), kD150 = half_life(150.0), kD300 = half_life(300.0);
constexpr int kContact = 8, kPack = 4, kGank = 10, kCampNear = 3, kQuiet = 100;
constexpr int kMaxLoop = 12;  // bound on visible units processed per list (5 per team in this game)

// d^n by squaring (integer n >= 0): deterministic, at most ~11 multiplies for n < 2048.
float powi(float d, int n)
{
    float r = 1.0f;
    while (n > 0) { if (n & 1) r *= d; d *= d; n >>= 1; }
    return r;
}

// log2 by frexp and a fixed atanh series (|z| <= 1/3, error < 2e-5): no libm transcendental.
float log2_fixed(float x)
{
    int e = 0;
    const float m = std::frexp(x, &e);  // x = m * 2^e, m in [0.5, 1)
    const float z = (m - 1.0f) / (m + 1.0f), z2 = z * z;
    const float ln_m = 2.0f * z * (1.0f + z2 * (1.0f / 3.0f + z2 * (1.0f / 5.0f + z2 * (1.0f / 7.0f))));
    return static_cast<float>(e) + ln_m * 1.4426950408889634f;
}

float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

}  // namespace

std::uint8_t opp_kit_bits(int type)
{
    const auto& cat = catalog();
    if (type < 0 || type >= cat.n_champions) return 0;
    const auto& c = cat.champion(type);
    std::uint8_t bits = c.attack_type == 1 ? 1 : 0;
    for (const auto& a : c.ability)
    {
        if (a.kind == ab_dash) bits |= 2;
        if (a.kind == ab_cc || ((a.kind == ab_aoe || a.kind == ab_dash) && a.duration > 0.0f)) bits |= 4;
    }
    return bits;
}

void OppContext::update(const Obs& o, const Map& map)
{
    if (o.round <= last_round) return;
    const int dt = last_round < 0 ? 0 : o.round - last_round;
    last_round = o.round;
    if (dt > 0)
    {
        const int n = dt < 2048 ? dt : 2048;
        const float d60 = powi(kD60, n), d100 = powi(kD100, n), d150 = powi(kD150, n), d300 = powi(kD300, n);
        pack.decay(d60); press.decay(d60); cc.decay(d60);
        contact.decay(d100); fed.decay(d100); gank.decay(d100); camp.decay(d100);
        bank.decay(d150);
        trade.decay(d300);
    }

    // Banked gems (global facts, valid alive or dead).
    if (bank_valid)
    {
        const float du = o.banked_us >= prev_banked_us ? static_cast<float>(o.banked_us - prev_banked_us) : 0.0f;
        const float dh = o.banked_them >= prev_banked_them ? static_cast<float>(o.banked_them - prev_banked_them) : 0.0f;
        if (du + dh > 0.0f) bank.add(dh / (du + dh), du + dh);
    }
    prev_banked_us = o.banked_us;
    prev_banked_them = o.banked_them;
    bank_valid = true;

    // Deaths: our own on the alive -> dead edge; other champions from drops of the alive counts, which the
    // adapter refreshes only while we are alive (a death and a respawn inside one sampling window cancel).
    float ours = 0.0f, theirs = 0.0f;
    if (!o.alive && was_alive) ours += 1.0f;
    was_alive = o.alive;
    if (o.alive)
    {
        if (prev_allies_alive >= 0 && o.allies_alive < prev_allies_alive) ours += static_cast<float>(prev_allies_alive - o.allies_alive);
        if (prev_enemies_alive >= 0 && o.enemies_alive < prev_enemies_alive) theirs += static_cast<float>(prev_enemies_alive - o.enemies_alive);
        prev_allies_alive = o.allies_alive;
        prev_enemies_alive = o.enemies_alive;
    }
    if (ours + theirs > 0.0f) trade.add(ours / (ours + theirs), ours + theirs);

    if (!o.alive) return;  // nothing is in view while dead
    ++alive_rounds;

    const int ne = static_cast<int>(o.enemies.size()) < kMaxLoop ? static_cast<int>(o.enemies.size()) : kMaxLoop;
    const int na = static_cast<int>(o.allies.size()) < kMaxLoop ? static_cast<int>(o.allies.size()) : kMaxLoop;
    const int nc = static_cast<int>(o.camps.size()) < kMaxLoop ? static_cast<int>(o.camps.size()) : kMaxLoop;
    const bool have_spawn = o.team >= 0 && o.team < 2 && !map.spawn[o.team].empty();
    const Tile home = have_spawn ? map.spawn_center[o.team] : Tile{};

    int within8 = 0, within4 = 0, best_pack = 0;
    float enemy_power = 0.0f;
    for (int i = 0; i < ne; ++i)
    {
        const auto& e = o.enemies[static_cast<std::size_t>(i)];
        const int d = cheb(e.at, o.self.at);
        within8 += d <= kContact ? 1 : 0;
        within4 += d <= kPack ? 1 : 0;
        int cluster = 0;
        for (int j = 0; j < ne; ++j) cluster += cheb(o.enemies[static_cast<std::size_t>(j)].at, e.at) <= kPack ? 1 : 0;
        best_pack = cluster > best_pack ? cluster : best_pack;
        const float p = e.max_hp * e.strength;
        enemy_power = p > enemy_power ? p : enemy_power;
        // Where they spend time.
        if (have_spawn) gank.add(cheb(e.at, home) <= kGank ? 1.0f : 0.0f);
        bool near_camp = false;
        for (int c = 0; c < nc && !near_camp; ++c)
        {
            const auto& cv = o.camps[static_cast<std::size_t>(c)];
            near_camp = cv.hp > 0.0f && cheb(cv.at, e.at) <= kCampNear;
        }
        camp.add(near_camp ? 1.0f : 0.0f);
        // Kit identification (first sighting of each id).
        bool known = false;
        for (int k = 0; k < n_ids && !known; ++k) known = ids[k] == e.id;
        if (!known && n_ids < kMaxIds && e.id >= 0)
        {
            ids[n_ids] = e.id;
            kit[n_ids] = opp_kit_bits(e.type);
            ++n_ids;
        }
    }

    const bool in_contact = within8 > 0;
    contact.add(in_contact ? 1.0f : 0.0f);
    if (in_contact)
    {
        ++contact_rounds;
        last_contact = o.round;
        pack.add(static_cast<float>(best_pack) / 5.0f);
        press.add(static_cast<float>(within4) / 5.0f);
        int side = 1, stunned = o.self.stunned ? 1 : 0;
        for (int i = 0; i < na; ++i)
        {
            const auto& a = o.allies[static_cast<std::size_t>(i)];
            if (cheb(a.at, o.self.at) > kContact) continue;
            ++side;
            stunned += a.stunned ? 1 : 0;
        }
        cc.add(static_cast<float>(stunned) / static_cast<float>(side));
    }
    if (ne > 0)
    {
        float own_power = o.self.max_hp * o.self.strength;
        for (int i = 0; i < na; ++i)
        {
            const auto& a = o.allies[static_cast<std::size_t>(i)];
            const float p = a.max_hp * a.strength;
            own_power = p > own_power ? p : own_power;
        }
        if (enemy_power > 0.0f && own_power > 0.0f)
            fed.add(clampf(0.5f * log2_fixed(enemy_power / own_power), -1.0f, 1.0f));
    }
}

void OppContext::write(float* x) const
{
    int ranged = 0, dash = 0, stun = 0;
    for (int k = 0; k < n_ids; ++k)
    {
        ranged += kit[k] & 1;
        dash += (kit[k] >> 1) & 1;
        stun += (kit[k] >> 2) & 1;
    }
    const float inv_ids = n_ids > 0 ? 1.0f / static_cast<float>(n_ids) : 0.0f;
    int i = 0;
    x[i++] = contact.mean();
    x[i++] = pack.mean();
    x[i++] = press.mean();
    x[i++] = cc.mean();
    x[i++] = clampf(fed.mean(), -1.0f, 1.0f);
    x[i++] = trade.den > 1e-6f ? clampf(2.0f * trade.mean() - 1.0f, -1.0f, 1.0f) : 0.0f;
    x[i++] = gank.mean();
    x[i++] = camp.mean();
    x[i++] = bank.den > 1e-6f ? clampf(2.0f * bank.mean() - 1.0f, -1.0f, 1.0f) : 0.0f;
    x[i++] = static_cast<float>(ranged) * inv_ids;
    x[i++] = static_cast<float>(dash) * inv_ids;
    x[i++] = static_cast<float>(stun) * inv_ids;
    x[i++] = last_contact < 0 ? 1.0f : clampf(static_cast<float>(last_round - last_contact) / static_cast<float>(kQuiet), 0.0f, 1.0f);
    x[i++] = static_cast<float>(alive_rounds) / static_cast<float>(alive_rounds + 50);
    x[i++] = static_cast<float>(contact_rounds) / static_cast<float>(contact_rounds + 20);
    x[i++] = static_cast<float>(n_ids < 5 ? n_ids : 5) / 5.0f;
}

}  // namespace zero
