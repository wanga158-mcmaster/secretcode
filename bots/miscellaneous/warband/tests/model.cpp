// Unit tests of warband's pure modules: the pack protocol and the models (no SDK calls).
// Run: ./arena.cmd test contenders/warband
#include <cassert>
#include <cmath>
#include <cstdio>
#include "model.h"
#include "protocol.h"

using namespace wb;

static bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

int main() {
    // ---- protocol ----
    {
        PackMsg m;
        m.objective = 37;
        m.ax = 99;
        m.ay = 0;
        m.mode = Mode::fight;
        m.focus = 4;
        m.stamp = 300;          // stored as round & 255
        m.avail = 0x2d;
        m.busy = 0x05;
        m.sighting = true;
        m.sdx = -3;
        m.sdy = 7;
        m.sight_age = 1;
        const auto d = PackMsg::decode(m.encode());
        assert(d.objective == 37 && d.ax == 99 && d.ay == 0 && d.mode == Mode::fight && d.focus == 4);
        assert(d.stamp == (300 & 255) && d.avail == 0x2d && d.busy == 0x05);
        assert(d.sighting && d.sdx == -3 && d.sdy == 7 && d.sight_age == 1);
        assert(d.has_anchor() && d.has_sighting());
        assert(d.fresh(300) && !d.fresh(301) && d.recent(301, 1) && !d.recent(303, 2));
        assert(PackMsg::popcount(0x2d) == 4);
    }
    {
        // The game's initial all-zero memory decodes as "nothing": no focus group, anchor, focus
        // target or sighting (indices and coordinates are stored plus one).
        const auto z = PackMsg::decode(std::array<std::uint8_t, 8>{});
        assert(z.objective == -1 && !z.has_anchor() && z.focus == -1 && !z.has_sighting());
        assert(z.mode == Mode::travel && z.avail == 0 && z.busy == 0);
        PackMsg none;
        const auto d = PackMsg::decode(none.encode());
        assert(d.objective == -1 && !d.has_anchor() && d.focus == -1 && !d.has_sighting());
        // Out-of-range values become "none"; offsets are clamped into [-8, 7].
        PackMsg bad;
        bad.objective = 300;
        bad.focus = 9;
        bad.ax = 5;
        bad.ay = 5;
        bad.sighting = true;
        bad.sdx = -20;
        bad.sdy = -20;
        bad.sight_age = 7;
        const auto b = PackMsg::decode(bad.encode());
        assert(b.objective == -1 && b.focus == -1 && b.sight_age == 3 && !b.has_sighting());
        assert(b.sighting && b.sdx == -8 && b.sdy == -7);  // (-8, -8) would collide with "none"
        // Stamps wrap at 256.
        PackMsg w;
        w.stamp = 255;
        assert(PackMsg::decode(w.encode()).recent(256, 1));
    }
    // ---- camp clears ----
    {
        // One monster: killed in ceil(hp/dps) rounds; it attacks k - 1 times.
        const auto f = model::forecast_clear({{1000.0f, 50.0f}}, 300.0f);
        assert(f.rounds == 4 && near(f.damage, 150.0f, 1e-3f) && near(f.peak, 50.0f, 1e-3f));
        // Smith order is input-order invariant; the elk (60/2400) dies before the wolf (20/1350),
        // the harmless cluster last.
        const auto a = model::forecast_clear({{2400.0f, 60.0f}, {120.0f, 0.0f}, {1350.0f, 20.0f}}, 200.0f);
        const auto b = model::forecast_clear({{120.0f, 0.0f}, {1350.0f, 20.0f}, {2400.0f, 60.0f}}, 200.0f);
        assert(near(a.damage, b.damage, 1e-3f) && a.rounds == b.rounds);
        assert(near(a.damage, 60.0f * 11 + 20.0f * 18, 1e-3f) && a.rounds == 20);
        // A pack of five deals five times the damage: the damage taken falls roughly as 1/dps.
        const auto solo = model::forecast_clear({{2400.0f, 60.0f}, {2400.0f, 60.0f}}, 60.0f);
        const auto pack = model::forecast_clear({{2400.0f, 60.0f}, {2400.0f, 60.0f}}, 300.0f);
        assert(pack.damage * 4.0f < solo.damage);
        // No damage: infeasible.
        const auto z = model::forecast_clear({{100.0f, 10.0f}}, 0.0f);
        assert(z.damage > 1e8f);
    }
    // ---- force ----
    {
        model::Fighter u{1000.0f, 50.0f, 0.0f, 0, false};
        const std::vector<model::Fighter> one{u}, two{u, u}, three{u, u, u};
        assert(near(model::force_ratio(one, one), 1.0f, 1e-4f));
        assert(near(model::force_ratio(two, one), 4.0f, 1e-3f));   // square law
        assert(near(model::force_ratio(three, two), 2.25f, 1e-3f));
        assert(model::force_ratio(one, {}) >= 99.0f);
        // A ready burst counts as extra damage over the horizon.
        model::Fighter burst = u;
        burst.burst = 300.0f;
        assert(model::force_ratio({burst}, one) > 1.0f);
    }
    // ---- focus ----
    {
        // Lower effective health first; carried gems and a pending stun make a target better.
        assert(model::focus_score(400, 200, 0, false, 2) < model::focus_score(900, 200, 0, false, 2));
        assert(model::focus_score(900, 200, 120, false, 2) < model::focus_score(900, 200, 0, false, 2));
        assert(model::focus_score(900, 200, 0, true, 2) < model::focus_score(900, 200, 0, false, 2));
        assert(model::focus_score(900, 200, 0, false, 1) < model::focus_score(900, 200, 0, false, 6));
    }
    std::puts("warband model tests passed");
    return 0;
}
