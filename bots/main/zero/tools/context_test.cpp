// Native test of the opponent context (src/core/context.*): values on synthetic observation sequences,
// once-per-round updates, unknown-vs-zero, bounds and determinism. Build and run with context_test.cmd.
#include <cmath>
#include <cstdio>
#include <cstring>
#include "../src/core/context.h"

using namespace zero;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)
static bool near(float a, double b, double tol = 1e-4) { return std::fabs(static_cast<double>(a) - b) <= tol; }

static Map make_map()
{
    Map m;
    m.w = m.h = 40;
    m.spawn[0] = { Tile{ 2, 2 } };
    m.spawn[1] = { Tile{ 37, 37 } };
    m.spawn_center[0] = Tile{ 2, 2 };
    m.spawn_center[1] = Tile{ 37, 37 };
    return m;
}

static UnitView unit(int id, int type, int x, int y, float max_hp = 1000.0f, float strength = 50.0f)
{
    UnitView u;
    u.id = id;
    u.type = static_cast<std::uint8_t>(type);
    u.at = Tile{ static_cast<std::int16_t>(x), static_cast<std::int16_t>(y) };
    u.hp = u.max_hp = max_hp;
    u.strength = strength;
    return u;
}

static Obs base(int round)
{
    Obs o;
    o.round = round;
    o.team = 0;
    o.alive = true;
    o.self = unit(1, 0, 20, 20);
    o.allies_alive = 5;
    o.enemies_alive = 5;
    return o;
}

static void facts(const OppContext& c, float* f) { c.write(f); }

static void check_bounds(const float* f)
{
    for (int i = 0; i < OppContext::kFacts; ++i) CHECK(std::isfinite(f[i]) && f[i] >= -1.0f && f[i] <= 1.0f);
}

int main()
{
    const Map map = make_map();
    float f[OppContext::kFacts];
    const double d100 = std::exp2(-1.0 / 100.0);

    // Fresh: everything neutral, quiet = 1 (never in contact), no evidence.
    {
        OppContext c;
        facts(c, f);
        for (int i = 0; i < OppContext::kFacts; ++i) CHECK(f[i] == (i == 12 ? 1.0f : 0.0f));
    }
    // Ten contact rounds: two enemies (vesper, cassi) 3-4 tiles from us and 1 apart, a third enemy far away.
    OppContext c;
    for (int r = 1; r <= 10; ++r)
    {
        Obs o = base(r);
        o.enemies = { unit(10, 1, 23, 20), unit(11, 3, 24, 20), unit(12, 12, 35, 35) };
        c.update(o, map);
        c.update(o, map);  // a refresh in the same round is ignored
    }
    facts(c, f);
    check_bounds(f);
    CHECK(near(f[0], 1.0));        // contact every alive round
    CHECK(near(f[1], 2.0 / 5.0));  // the pair is the largest cluster
    CHECK(near(f[2], 2.0 / 5.0));  // two within 4 of us
    CHECK(near(f[3], 0.0));        // nobody stunned
    CHECK(near(f[4], 0.0));        // equal power
    CHECK(f[5] == 0.0f);           // no deaths seen: unknown
    CHECK(near(f[6], 0.0));        // nobody within 10 of our spawn (2,2)
    CHECK(near(f[7], 0.0));
    CHECK(near(f[9], 2.0 / 3.0));  // vesper, cassi ranged; heart melee
    CHECK(near(f[10], 2.0 / 3.0)); // vesper and heart dash
    CHECK(near(f[11], 2.0 / 3.0)); // cassi stun, heart dash-stun
    CHECK(near(f[12], 0.0));
    CHECK(near(f[13], 10.0 / 60.0));
    CHECK(near(f[14], 10.0 / 30.0));
    CHECK(near(f[15], 3.0 / 5.0));
    CHECK(c.alive_rounds == 10 && c.contact_rounds == 10);

    // Ten quiet rounds: contact becomes the decay-weighted mean; contact-only facts keep their means.
    for (int r = 11; r <= 20; ++r) c.update(base(r), map);
    facts(c, f);
    {
        double num = 0, den = 0;
        for (int r = 1; r <= 20; ++r) { const double w = std::pow(d100, 20 - r); num += w * (r <= 10 ? 1 : 0); den += w; }
        CHECK(near(f[0], num / den));
    }
    CHECK(near(f[1], 2.0 / 5.0));
    CHECK(near(f[12], 10.0 / 100.0));
    CHECK(c.contact_rounds == 10 && c.alive_rounds == 20);

    // Gank: enemy within 10 of our spawn centre; camp: enemy next to a living visible camp.
    {
        OppContext g;
        Obs o = base(5);
        o.self.at = Tile{ 6, 6 };
        o.enemies = { unit(20, 2, 8, 8) };
        CampView cv;
        cv.at = Tile{ 10, 10 };
        cv.hp = 100.0f;
        o.camps = { cv };
        g.update(o, map);
        facts(g, f);
        CHECK(near(f[6], 1.0));
        CHECK(near(f[7], 1.0));
        CHECK(near(f[11], 0.0) && near(f[9], 0.0) && near(f[10], 1.0));  // vendetta: melee dash, no stun
    }
    // Stun share and fed: self stunned, one ally within 8 not stunned; enemy power 4x ours, then 2x.
    {
        OppContext s;
        Obs o = base(1);
        o.self.stunned = true;
        o.allies = { unit(2, 4, 22, 22), unit(3, 4, 39, 0) };  // the far ally is not on our side of the fight
        o.enemies = { unit(30, 0, 21, 21, 2000.0f, 100.0f) };
        s.update(o, map);
        facts(s, f);
        CHECK(near(f[3], 0.5));
        CHECK(near(f[4], 1.0));
        OppContext s2;
        o.enemies = { unit(30, 0, 21, 21, 2000.0f, 50.0f) };
        s2.update(o, map);
        facts(s2, f);
        CHECK(near(f[4], 0.5, 1e-4));
        o.enemies = { unit(30, 0, 21, 21, 100.0f, 10.0f) };  // far weaker: clamps at -1
        OppContext s3;
        s3.update(o, map);
        facts(s3, f);
        CHECK(near(f[4], -1.0));
    }
    // Trade and bank. Death of self (dead rounds add nothing in view), then three enemy deaths after respawn.
    {
        OppContext t;
        Obs o = base(1);
        o.banked_us = 100;
        o.banked_them = 100;
        t.update(o, map);
        facts(t, f);
        CHECK(f[5] == 0.0f && f[8] == 0.0f);  // first sample sets the reference only
        o = base(2);
        o.banked_us = 110;
        o.banked_them = 130;  // they banked 30 of 40
        t.update(o, map);
        facts(t, f);
        CHECK(near(f[8], 0.5));
        Obs dead = base(3);
        dead.alive = false;
        dead.banked_us = 110;
        dead.banked_them = 130;
        dead.enemies.clear();
        t.update(dead, map);
        facts(t, f);
        CHECK(near(f[5], 1.0));  // only we died
        CHECK(t.alive_rounds == 2);
        for (int r = 4; r < 63; ++r) { dead.round = r; t.update(dead, map); }
        CHECK(t.alive_rounds == 2);
        o = base(63);
        o.banked_us = 110;
        o.banked_them = 130;
        o.enemies_alive = 2;
        t.update(o, map);
        facts(t, f);
        const double d300 = std::exp2(-1.0 / 300.0);
        const double w1 = std::pow(d300, 60);  // our death at round 3, theirs at 63
        const double share = (w1 * 1.0) / (w1 * 1.0 + 3.0);
        CHECK(near(f[5], 2.0 * share - 1.0));
        CHECK(f[5] < 0.0f);
    }
    // Determinism and bounds on a long pseudo-random sequence with skipped rounds and dead stretches.
    {
        OppContext a, b;
        std::uint64_t rng = 12345;
        auto next = [&](int n) { rng = rng * 6364136223846793005ull + 1442695040888963407ull; return static_cast<int>((rng >> 33) % static_cast<std::uint64_t>(n)); };
        int round = 0, alive_left = 0;
        for (int step = 0; step < 3000; ++step)
        {
            round += 1 + (next(10) == 0 ? next(5) : 0);
            Obs o = base(round);
            if (alive_left <= 0 && next(80) == 0) alive_left = -60;
            o.alive = alive_left >= 0;
            alive_left = alive_left < 0 ? alive_left + 1 : alive_left;
            o.self.at = Tile{ static_cast<std::int16_t>(next(40)), static_cast<std::int16_t>(next(40)) };
            o.self.stunned = next(7) == 0;
            o.allies_alive = 3 + next(3);
            o.enemies_alive = 3 + next(3);
            o.banked_us = static_cast<std::uint32_t>(round * 2 + next(5));
            o.banked_them = static_cast<std::uint32_t>(round * 3);
            if (o.alive)
            {
                const int n = next(6);
                for (int k = 0; k < n; ++k) o.enemies.push_back(unit(100 + next(7), next(13), next(40), next(40), 500.0f + static_cast<float>(next(3000)), 20.0f + static_cast<float>(next(200))));
                for (int k = 0; k < next(5); ++k) o.allies.push_back(unit(2 + k, next(13), next(40), next(40), 500.0f + static_cast<float>(next(3000)), 20.0f + static_cast<float>(next(200))));
                for (int k = 0; k < next(6); ++k) { CampView cv; cv.at = Tile{ static_cast<std::int16_t>(next(40)), static_cast<std::int16_t>(next(40)) }; cv.hp = static_cast<float>(next(2)); o.camps.push_back(cv); }
            }
            a.update(o, map);
            b.update(o, map);
            b.update(o, map);
            float fa[OppContext::kFacts], fb[OppContext::kFacts];
            a.write(fa);
            b.write(fb);
            check_bounds(fa);
            if (std::memcmp(fa, fb, sizeof fa) != 0) { std::printf("FAIL nondeterministic at step %d\n", step); ++g_fail; break; }
        }
        a.write(f);
        std::printf("random end:");
        for (int i = 0; i < OppContext::kFacts; ++i) std::printf(" %.3f", f[i]);
        std::printf("\n");
        CHECK(a.n_ids == 7);
    }
    std::printf(g_fail ? "context_test FAILED (%d)\n" : "context_test passed\n", g_fail);
    return g_fail ? 1 : 0;
}
