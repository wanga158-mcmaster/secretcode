// Unit tests for a main line's model.h (pure, SDK-free). Compiled once per line against its own
// sources (-I bots/main/<line>/src) by: python -B bots/main/tests/run.py [line ...]
#include <cassert>
#include <cmath>
#include <cstdio>
#include "model.h"

using namespace mb::model;

static bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

int main()
{
    // A respawn ready on arrival must not cause the target to be dropped and selected again.
    // Invalid/unreachable distance sentinels cannot justify walking, and a later respawn uses the
    // existing bounded wait policy rather than this exception.
    {
        assert(approach_respawn(12, 12));
        assert(approach_respawn(11, 12));
        assert(!approach_respawn(13, 12));
        assert(!approach_respawn(12, 1 << 14));
        assert(!approach_respawn(0, -1));
        assert(approach_respawn(0, 0));
    }
    // Item gem weight: early (base stats, 1000 gems left) gold is worth more than 1/floor gems, so the
    // floor holds; stacked items and a short race raise it; the cap bounds it.
    {
        assert(near(item_gem_weight(1000.0f, 6.0f, 70.0f, 30.0f, 0.5f, 1.5f, 20.0f), 1.5f, 1e-4f));
        const float mid = item_gem_weight(600.0f, 6.0f, 160.0f, 30.0f, 0.5f, 1.5f, 20.0f);
        assert(near(mid, 1.0f / (0.5f * 600.0f * 6.0f / (30.0f * 160.0f)), 1e-3f) && mid > 2.5f);
        assert(item_gem_weight(300.0f, 6.0f, 250.0f, 30.0f, 0.5f, 1.5f, 20.0f) > mid);
        assert(near(item_gem_weight(50.0f, 6.0f, 400.0f, 30.0f, 0.5f, 1.5f, 20.0f), 20.0f, 1e-4f));
        assert(near(item_gem_weight(50.0f, 6.0f, 400.0f, 30.0f, 0.5f, 25.0f, 20.0f), 25.0f, 1e-4f));  // floor wins over a lower cap
    }
    // Single monster: killed in ceil(hp/dps) rounds; it attacks k-1 times.
    {
        const auto f = forecast_camp_fight({ { 1000.0f, 50.0f } }, 300.0f);
        assert(f.rounds == 4);
        assert(near(f.damage, 150.0f, 1e-3f));
        assert(near(f.peak_rate, 50.0f, 1e-3f));
    }
    // Smith's rule: the high strength/health monster dies first regardless of input order.
    {
        const std::vector<Monster> a{ { 2400.0f, 60.0f }, { 120.0f, 0.0f }, { 1350.0f, 20.0f } };
        const std::vector<Monster> b{ { 1350.0f, 20.0f }, { 2400.0f, 60.0f }, { 120.0f, 0.0f } };
        const auto fa = forecast_camp_fight(a, 200.0f);
        const auto fb = forecast_camp_fight(b, 200.0f);
        assert(near(fa.damage, fb.damage, 1e-3f));
        // Elk first: dies in round 12 (11 hits of 60); wolf next at cumulative 3750 -> round 19 (18 hits of 20).
        assert(near(fa.damage, 60.0f * 11 + 20.0f * 18, 1e-3f));
        assert(fa.rounds == 20);  // the harmless cluster dies last at cumulative 3870
    }
    // Zero damage rate is infeasible, empty group is free.
    {
        assert(forecast_camp_fight({ { 100.0f, 10.0f } }, 0.0f).damage > 1e20f);
        assert(forecast_camp_fight({}, 100.0f).damage == 0.0f);
    }
    // O(1) constants bracket the exact discrete forecast: W/d - S <= exact <= W/d.
    {
        const std::vector<Monster> group{ { 2400.0f, 60.0f }, { 2400.0f, 60.0f }, { 1350.0f, 20.0f }, { 120.0f, 0.0f } };
        const auto c = fight_constants(group);
        assert(near(c.total_hp, 6270.0f, 1e-2f));
        assert(near(c.total_strength, 140.0f, 1e-3f));
        for (float dps : { 50.0f, 137.0f, 400.0f, 1000.0f })
        {
            const auto exact = forecast_camp_fight(group, dps);
            const float w_over_d = c.weighted_hp / dps;
            assert(exact.damage <= w_over_d + 1e-2f);
            assert(exact.damage >= w_over_d - c.total_strength - 1e-2f);
            const auto approx = forecast_from_constants(c, dps, 1.0f);
            assert(std::fabs(approx.damage - exact.damage) <= 0.5f * c.total_strength + 1e-2f);
            assert(approx.rounds == exact.rounds);
        }
        // Scaling health halves the weighted term.
        const auto half = forecast_from_constants(c, 100.0f, 0.5f);
        assert(near(half.damage, std::max(0.0f, c.weighted_hp * 0.5f / 100.0f - 70.0f), 1e-2f));
    }
    // Damage rates: an attack reset adds one attack per cooldown; splash multiplies basics.
    {
        ChampionSummary c;
        c.strength = 80.0f;
        c.focus = 100.0f;
        c.ability[0] = { AbilityKind::attack_reset, 0.0f, 8, false };
        c.ability[1] = { AbilityKind::damage, 2.0f, 8, true };
        assert(near(camp_dps(c, 0.0f), 80.0f * (1.0f + 1.0f / 8.0f) + 200.0f / 8.0f, 1e-3f));
        c.splash = true;
        assert(near(camp_dps(c, 2.0f), 80.0f * (1.0f + 1.0f / 8.0f) * 3.0f + 200.0f / 8.0f, 1e-3f));
        ChampionSummary aoe;
        aoe.focus = 100.0f;
        aoe.ability[0] = { AbilityKind::aoe, 1.0f, 8, true };
        assert(near(camp_dps(aoe, 1.5f), 100.0f / 8.0f * 2.5f, 1e-3f));
    }
    // Rate: reward over total time; never divides by zero.
    {
        assert(near(group_rate(100.0f, 5, 3, 2), 10.0f, 1e-4f));
        assert(near(group_rate(100.0f, 0, 0, 0), 100.0f, 1e-4f));
    }
    // Field cost model (regression 2026-10-21, 01fracture): a 2-step search of a group in a pocket
    // costs ~2990 fuel, almost all fixed overhead; charging it to the steps gave ~1720 per step and
    // every later field was refused (35% of champion turns blocked). Short searches are ignored and
    // the overhead is taken out of longer ones.
    {
        assert(updated_step_cost(120, true, 2990, 2, 3000) == 120);    // pocket search ignored
        assert(updated_step_cost(260, false, 2990, 2, 3000) == 260);   // not even as a first measurement
        const auto first = updated_step_cost(260, false, 15105, 179, 3000);
        assert(first == (15105 - 3000) * 115 / 100 / 179 + 1);         // first real measurement replaces the guess
        assert(updated_step_cost(first, true, 9872, 95, 3000) == std::max<std::uint64_t>(first, (9872 - 3000) * 115 / 100 / 95 + 1));
        assert(updated_step_cost(100, true, 2000, 50, 3000) == 100);   // cost below overhead: rate 1, max keeps 100
    }
    // Siege: trips scale with damage over pool; overhead applies to extra trips only.
    {
        FightForecast f;
        f.rounds = 30;
        f.damage = 2000.0f;
        const auto one = plan_siege(f, 4000.0f, 25, 1.25f, 6);
        assert(one.feasible && one.trips == 1 && one.rounds == 30);
        const auto three = plan_siege(f, 1000.0f, 25, 1.25f, 6);
        assert(three.feasible && three.trips == 3 && three.rounds == 30 + 2 * 25);
        assert(!plan_siege(f, 100.0f, 25, 1.25f, 6).feasible);
        assert(plan_siege(FightForecast{}, 0.0f, 25, 1.25f, 6).feasible);  // harmless group
        // Two pools (siege planning that matches the recall rule): a fresh champion plans as the
        // single-pool version; a drained one (nothing left above the recall threshold) pays one
        // extra trip; a half-drained one covers part of the need before its first recall.
        const auto fresh = plan_siege(f, 1000.0f, 1000.0f, 25, 1.25f, 6);
        assert(fresh.trips == three.trips && fresh.rounds == three.rounds);
        const auto drained = plan_siege(f, 0.0f, 1000.0f, 25, 1.25f, 6);
        assert(drained.feasible && drained.trips == 4 && drained.rounds == 30 + 3 * 25);
        const auto part = plan_siege(f, 600.0f, 1000.0f, 25, 1.25f, 6);  // 2500 needed: 600 + 2 x 1000
        assert(part.trips == 3);
        assert(plan_siege(f, 4000.0f, 4000.0f, 25, 1.25f, 6).trips == 1);
        assert(!plan_siege(f, 0.0f, 1000.0f, 25, 1.25f, 3).feasible);  // 4 trips > max 3
        assert(plan_siege(FightForecast{}, 0.0f, 0.0f, 25, 1.25f, 6).feasible);
        // Progress: pool 1000 at average 2000/30 per round lasts 15 rounds; 100 dps removes 1500.
        assert(near(trip_progress(f, 100.0f, 1000.0f, 3000.0f), 0.5f, 1e-3f));
        assert(near(trip_progress(f, 100.0f, 1e6f, 3000.0f), 1.0f, 1e-6f));
    }
    // Squads: small teams stay whole; linked members share a squad; each squad gets melee.
    {
        assert((partition_squads({ 1, 1, 1 }, { true, false, false }, { -1, -1, -1 }) == std::vector<int>{ 0, 0, 0 }));
        // Cassi(0, ranged) + Issac(1, melee) linked; Varg, Vendetta melee; Tavi ranged; Marcella melee.
        const std::vector<float> power{ 0.5f, 0.8f, 1.0f, 1.1f, 0.9f, 1.0f };
        const std::vector<bool> melee{ false, true, true, true, false, true };
        const std::vector<int> linked{ 1, 0, -1, -1, -1, -1 };
        const auto sq = partition_squads(power, melee, linked);
        assert(sq[0] == sq[1]);
        int count[2]{};
        bool m[2]{};
        float total[2]{};
        for (std::size_t i = 0; i < sq.size(); ++i)
        {
            ++count[sq[i]];
            m[sq[i]] = m[sq[i]] || melee[i];
            total[sq[i]] += power[i];
        }
        assert(count[0] > 0 && count[1] > 0 && m[0] && m[1]);
        assert(std::fabs(total[0] - total[1]) <= 1.2f);
        // Two ranged-only blocks cannot both lack melee when melee can be swapped in.
        const auto sq2 = partition_squads({ 1.0f, 1.0f, 0.9f, 0.9f }, { false, false, true, true }, { -1, -1, -1, -1 });
        bool m2[2]{};
        for (std::size_t i = 0; i < sq2.size(); ++i) m2[sq2[i]] = m2[sq2[i]] || (i >= 2);
        assert(m2[0] && m2[1]);
    }
    // Pairs: linked members share a squad; others pair strongest with weakest; odd one alone.
    {
        const auto p = partition_small_squads({ 0.5f, 0.8f, 1.0f, 1.1f, 0.9f, 1.2f }, { 1, 0, -1, -1, -1, -1 }, 2);
        assert(p[0] == p[1]);
        int count[8]{};
        for (int s : p) { assert(s >= 0 && s < 8); ++count[s]; }
        for (int c : count) assert(c <= 2);
        assert(p[5] == p[4]);  // strongest (1.2) with weakest remaining (0.9)
        assert(p[3] == p[2]);
        const auto odd = partition_small_squads({ 1.0f, 2.0f, 3.0f }, { -1, -1, -1 }, 2);
        assert(odd[2] == odd[0] && odd[1] != odd[0]);
    }
    // Skirmish: focus fire, linked deaths, simultaneous damage.
    {
        // 2 v 1: two 1000hp/100dps vs one 1500hp/150dps -> we win, lose nobody? They deal 150/round
        // to our 1000hp fighter (dies round 7); we deal 200/round -> they die in round 8 (cumulative 1600).
        auto o = simulate_skirmish({ { 1000, 100, true }, { 1000, 100, true }, { 1500, 150, false } }, 20);
        assert(o.we_survive && !o.they_survive);
        assert(near(o.their_losses, 1.0f, 1e-4f) && near(o.our_losses, 1.0f, 1e-4f));
        assert(o.first_target == 2);
        // Even fight: both die in the same round (damage is simultaneous).
        auto e = simulate_skirmish({ { 500, 100, true }, { 500, 100, false } }, 20);
        assert(!e.we_survive && !e.they_survive && e.rounds == 5);
        // Linked: killing their Cassi (index 1) also kills Issac (index 2).
        auto l = simulate_skirmish({ { 3000, 200, true }, { 400, 50, false, 1.0f, 2 }, { 900, 50, false, 1.0f, 1 } }, 20);
        assert(!l.they_survive && near(l.their_losses, 2.0f, 1e-4f) && l.rounds == 2);
        // Gem value weighs losses.
        auto v = simulate_skirmish({ { 100, 10, true, 3.0f }, { 1000, 200, false } }, 20);
        assert(near(v.our_losses, 3.0f, 1e-4f));
    }
    // Casts: front-loaded burst, one cast per round, readiness and cooldown.
    {
        Fighter us;
        us.hp = 1000.0f;
        us.dps = 50.0f;
        us.ours = true;
        us.cast[0] = 400.0f;
        Fighter them{ 600.0f, 100.0f, false };
        // Round 1: 50 + 400 leaves 150; then 50 per round -> dead in round 4 (not 12).
        auto b = simulate_skirmish({ us, them }, 20);
        assert(b.rounds == 4 && b.we_survive && !b.they_survive);
        Fighter two;
        two.hp = 1000.0f;
        two.ours = true;
        two.cast[0] = 300.0f;
        two.cast[1] = 200.0f;
        assert(simulate_skirmish({ two, Fighter{ 450.0f, 10.0f, false } }, 20).rounds == 2);  // not both in round 1
        Fighter late = two;
        late.cast[1] = 0.0f;
        late.cast[0] = 500.0f;
        late.ready[0] = 3;
        assert(simulate_skirmish({ late, Fighter{ 400.0f, 10.0f, false } }, 20).rounds == 3);
        Fighter cd;
        cd.hp = 1000.0f;
        cd.ours = true;
        cd.cast[0] = 100.0f;
        cd.cooldown = 2;  // casts in rounds 1, 3, 5
        assert(simulate_skirmish({ cd, Fighter{ 250.0f, 10.0f, false } }, 20).rounds == 5);
    }
    // Arrivals: a fighter still walking in neither hits nor is hit before its round.
    {
        Fighter us{ 500.0f, 100.0f, true };
        Fighter near_one{ 400.0f, 100.0f, false };
        Fighter far_one{ 400.0f, 100.0f, false };
        // Both in contact: 200 a round kills us in round 3 while the first enemy keeps 100.
        auto both = simulate_skirmish({ us, near_one, far_one }, 8);
        assert(!both.we_survive && near(both.our_losses, 1.0f, 1e-4f) && near(both.their_losses, 0.0f, 1e-4f));
        // The second arrives after the horizon: a 1v1 we win in round 4 (100 health left).
        far_one.arrive = 10;
        auto split = simulate_skirmish({ us, near_one, far_one }, 8);
        assert(split.we_survive && split.they_survive && near(split.their_losses, 1.0f, 1e-4f));
        assert(near(split.our_losses, 0.0f, 1e-4f) && split.first_target == 1 && split.rounds == 4);
        // Arriving inside the horizon it fights the survivor: 100 health against 400 loses.
        far_one.arrive = 6;
        auto late = simulate_skirmish({ us, near_one, far_one }, 8);
        assert(!late.we_survive && near(late.their_losses, 1.0f, 1e-4f) && late.rounds == 6);
        // A cast waits for its caster to arrive.
        Fighter caster{ 1000.0f, 0.0f, true };
        caster.cast[0] = 500.0f;
        caster.arrive = 3;
        auto wait = simulate_skirmish({ caster, Fighter{ 400.0f, 10.0f, false } }, 8);
        assert(!wait.they_survive && wait.rounds == 3 && near(wait.our_losses, 0.0f, 1e-4f));
        // The focus is the least health among fighters in contact, not the initial order: a has
        // 50 left when b (220) arrives in round 3, so a dies then (b first would kill nobody).
        Fighter tank{ 3000.0f, 100.0f, true };
        Fighter a{ 250.0f, 1.0f, false };
        Fighter b{ 220.0f, 1.0f, false };
        b.arrive = 3;
        auto focus = simulate_skirmish({ tank, a, b }, 3);
        assert(near(focus.their_losses, 1.0f, 1e-4f) && focus.first_target == 1 && focus.rounds == 3);
    }
    // A damaging dash landing (ground-targeted: not flagged as hitting enemies) counts only with
    // `dashes`; a pure movement dash (modifier 0) never does.
    {
        ChampionSummary heart;
        heart.strength = 30.0f;
        heart.focus = 100.0f;
        heart.ability[1] = { AbilityKind::dash, 1.5f, 8, true, false, 2.0f };
        assert(pvp_profile(heart).cast[1] == 0.0f && near(pvp_profile(heart, true).cast[1], 150.0f, 1e-3f));
        assert(near(pvp_dps(heart), 30.0f, 1e-3f) && near(pvp_dps(heart, true), 30.0f + 150.0f / 8.0f, 1e-3f));
        ChampionSummary vesper = heart;
        vesper.ability[1].modifier = 0.0f;
        assert(pvp_profile(vesper, true).cast[1] == 0.0f);
    }
    // Threat focus: kill the most damage per remaining health first (a 400hp/200dps carry before a
    // 200hp/10dps tank); the enemy side keeps least-health focus.
    {
        Fighter us{ 1000.0f, 100.0f, true };
        Fighter tank{ 200.0f, 10.0f, false };
        Fighter carry{ 400.0f, 200.0f, false };
        const auto plain = simulate_skirmish({ us, tank, carry }, 20);
        const auto smart = simulate_skirmish({ us, tank, carry }, 20, true);
        assert(plain.first_target == 1 && smart.first_target == 2);
        // Least health first: 210 a round for 2 rounds, then 200 -> we die in round 5 with the
        // carry at 100. Carry first: 210 for 4 rounds (840) then 10 -> we live with 140.
        assert(!plain.we_survive && smart.we_survive && !smart.they_survive);
    }
    std::puts("main-model: ok");
    return 0;
}
