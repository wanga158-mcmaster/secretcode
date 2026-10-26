// Unit tests of the SDK-free models (model.h, econ_model.h). Run: ./arena.cmd test contenders/compound
#include <cmath>
#include <cstdio>
#include <vector>

#include "econ_model.h"
#include "model.h"

static int failures = 0;
#define CHECK(cond)                                                                \
    do                                                                             \
    {                                                                              \
        if (!(cond))                                                               \
        {                                                                          \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);              \
            ++failures;                                                            \
        }                                                                          \
    } while (0)

static bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

static cm::Kit vesper_kit()
{
    cm::Kit k;
    k.str = 55.0f;
    k.focus = 100.0f;
    k.ranged = true;
    k.attack_reset = true;
    return k;
}

static cm::CampSpec spec(std::vector<cm::Monster> ms, float gold, float gems, float travel, float home)
{
    cm::MonsterSet set;
    for (auto m : ms) set.add(m);
    cm::CampSpec c;
    c.sc = cm::smith_constants(set);
    c.gold = gold;
    c.gems = gems;
    c.travel = travel;
    c.home = home;
    c.members = set.n;
    return c;
}

static void test_camp_forecast()
{
    // One wolf (1350 hp, 20 str) at 61 damage a round: dies in the 23rd round, struck 22 times.
    const auto f = cm::forecast_camp(std::vector<cm::Monster>{ { 1350.0f, 20.0f } }, 61.0f);
    CHECK(f.rounds == 23);
    CHECK(near(f.damage, 440.0f, 0.1f));
    // Smith order: the elk (60 str / 2400 hp) is killed before the harmless clusters.
    const auto g = cm::forecast_camp(std::vector<cm::Monster>{ { 120.0f, 0.0f }, { 2400.0f, 60.0f }, { 120.0f, 0.0f } }, 100.0f);
    CHECK(g.rounds == 27);
    CHECK(near(g.damage, 60.0f * 23.0f, 0.1f));
    // Mitigation multiplies the damage.
    const auto h = cm::forecast_camp(std::vector<cm::Monster>{ { 1350.0f, 20.0f } }, 61.0f, 0.63f);
    CHECK(near(h.damage, 440.0f * 0.63f, 0.1f));
}

static void test_smith_estimate()
{
    // The O(1) estimate stays within half a strike per monster of the exact forecast.
    cm::MonsterSet ms;
    ms.add({ 1350.0f, 20.0f });
    ms.add({ 1350.0f, 20.0f });
    ms.add({ 120.0f, 0.0f });
    const auto c = cm::smith_constants(ms);
    for (float dps : { 40.0f, 61.0f, 90.0f, 140.0f })
    {
        const auto exact = cm::forecast_camp(ms, dps, 1.0f).damage;
        const auto est = cm::estimate_camp_damage(c, dps, 1.0f);
        CHECK(std::fabs(exact - est) <= 0.5f * c.strength + 1.0f);
    }
    CHECK(cm::estimate_camp_damage(c, 61.0f, 1.0f, 0.5f) < cm::estimate_camp_damage(c, 61.0f, 1.0f, 1.0f));
}

static void test_camp_rate_feasibility()
{
    cm::Profile p;
    p.kit = vesper_kit();
    p.hp = 1000.0f;
    cm::Econ e;
    e.gold_w = 0.3f;
    // Two elks with clusters: far beyond one trip at base (needs more than three trips).
    const auto pack = spec({ { 2400.0f, 60.0f }, { 2400.0f, 60.0f }, { 120.0f, 0.0f }, { 120.0f, 0.0f } }, 220.0f, 44.0f, 10.0f, 20.0f);
    CHECK(cm::camp_rate(p, pack, e).value(e.gold_w) == 0.0f);
    // A wolf camp is feasible and worth something.
    const auto wolf = spec({ { 1350.0f, 20.0f }, { 120.0f, 0.0f } }, 60.0f, 4.0f, 8.0f, 15.0f);
    CHECK(cm::camp_rate(p, wolf, e).value(e.gold_w) > 0.0f);
    // Jungle and health make the elk camp cheaper.
    const auto elk = spec({ { 2400.0f, 60.0f }, { 120.0f, 0.0f }, { 120.0f, 0.0f } }, 110.0f, 22.0f, 10.0f, 20.0f);
    cm::Profile q = p;
    q.hp = 1500.0f;
    q.taken_camp = 0.7f;
    CHECK(cm::camp_rate(q, elk, e).value(e.gold_w) > cm::camp_rate(p, elk, e).value(e.gold_w));
    // A loop pays at most once per respawn of its slowest camp: one trivial gold camp alone is
    // capped by its cooldown (50 + 6 capture phases).
    auto gold2 = spec({ { 120.0f, 0.0f }, { 120.0f, 0.0f } }, 40.0f, 0.0f, 2.0f, 10.0f);
    gold2.cooldown = 50.0f;
    cm::Econ e1 = e;
    e1.top_k = 1;
    CHECK(cm::farm_rate(p, std::vector<cm::CampSpec>{ gold2 }, e1).gold <= 40.0f / 56.0f + 1e-4f);
}

static void test_e18_cycle()
{
    // Foundation E18: a base Vendetta's solo cycle on elk1 gem1 gold1 (clear + capture + health
    // charged as heal trips at 80% usable, walk back 15) is ~85 rounds; Jungle cuts it by ~35.
    cm::Profile v;
    v.kit.str = 70.0f;
    v.kit.focus = 100.0f;
    v.kit.attack_reset = true;
    v.kit.kill_reset = true;
    v.hp = 1000.0f;
    cm::Econ e;
    e.gold_w = 0.0f;
    const auto elk = spec({ { 2400.0f, 60.0f }, { 120.0f, 0.0f }, { 120.0f, 0.0f } }, 90.0f, 22.0f, 0.0f, 15.0f);
    const auto r = cm::camp_rate(v, elk, e);
    CHECK(r.gems > 0.0f);
    const float cycle = 22.0f / r.gems;
    std::printf("e18 cycle %.1f\n", cycle);
    CHECK(cycle > 70.0f && cycle < 95.0f);
    cm::Profile j = v;
    j.taken_camp = 0.7f;
    j.kit.str += 30.0f;   // five Strength shards (6 each)
    const float cj = 22.0f / cm::camp_rate(j, elk, e).gems;
    std::printf("e18 jungle cycle %.1f\n", cj);
    CHECK(cycle - cj > 25.0f && cycle - cj < 45.0f);
}

static void test_death_risk()
{
    cm::Profile p;
    p.hp = 1000.0f;
    cm::Econ e;
    e.threat_dps = 125.0f;
    e.exposure_rounds = 8.0f;
    const float r1 = cm::death_risk(p, e);
    CHECK(near(r1, 0.5f, 0.01f));   // effective health equal to the exposure: even odds
    p.hp = 2000.0f;
    const float r2 = cm::death_risk(p, e);
    CHECK(r2 < 0.05f);
    p.taken_all = 0.9f;
    CHECK(cm::death_risk(p, e) < r2);
    e.threat_dps = 0.0f;
    CHECK(cm::death_risk(p, e) == 0.0f);
}

static void test_best_purchase()
{
    cm::Profile p;
    p.kit = vesper_kit();
    p.hp = 1000.0f;
    p.growth = { 5.0f, 10.0f, 100.0f };
    std::vector<cm::CampSpec> camps{
        spec({ { 1350.0f, 20.0f }, { 120.0f, 0.0f } }, 60.0f, 4.0f, 8.0f, 15.0f),
        spec({ { 1350.0f, 20.0f }, { 120.0f, 0.0f }, { 120.0f, 0.0f } }, 40.0f, 18.0f, 10.0f, 18.0f),
        spec({ { 2400.0f, 60.0f }, { 120.0f, 0.0f }, { 120.0f, 0.0f } }, 110.0f, 22.0f, 10.0f, 20.0f),
        spec({ { 120.0f, 0.0f }, { 120.0f, 0.0f } }, 40.0f, 0.0f, 8.0f, 12.0f),
    };
    cm::Econ e;
    e.gold_w = 0.3f;
    e.race_left = 900.0f;
    cm::EmblemPassives m;
    std::array<std::array<int, 3>, 7> owned{};
    // No threat: the purchase is a farming item, and it is worth something.
    const auto calm = cm::best_purchase(p, camps, e, m, owned, 30.0f, false);
    CHECK(calm.value > 0.0f);
    CHECK(calm.stat != cm::Stat::focus);
    // A heavy threat makes Health the best stat.
    e.threat_dps = 140.0f;
    e.fights_per_round = 1.0f / 120.0f;
    const auto hot = cm::best_purchase(p, camps, e, m, owned, 30.0f, false);
    CHECK(hot.stat == cm::Stat::health);
    // A half-built emblem is cheaper to finish, so it is not abandoned for a fresh one of equal worth.
    owned[static_cast<std::size_t>(hot.family)][static_cast<std::size_t>(hot.stat)] = 3;
    const auto cont = cm::best_purchase(p, camps, e, m, owned, 30.0f, false);
    CHECK(cont.family == hot.family && cont.stat == hot.stat && cont.shards == 2);
    // A shorter race lowers every item's value.
    e.race_left = 100.0f;
    const auto late = cm::best_purchase(p, camps, e, m, owned, 30.0f, false);
    CHECK(late.value < cont.value);
}

static void test_emblems()
{
    cm::Profile p;
    p.growth = { 5.0f, 10.0f, 100.0f };
    p.hp = 1000.0f;
    cm::EmblemPassives m;
    const auto j = cm::add_emblem(p, cm::Family::jungle, cm::Stat::health, m);
    CHECK(near(j.taken_camp, 0.7f, 1e-4f));
    const auto jj = cm::add_emblem(j, cm::Family::jungle, cm::Stat::health, m);
    CHECK(near(jj.taken_camp, 0.7f, 1e-4f));   // a second Jungle adds no passive
    const auto d = cm::add_emblem(j, cm::Family::durability, cm::Stat::health, m);
    CHECK(near(d.taken_camp, 0.63f, 1e-4f));   // Jungle x Durability multiply (M40)
    const auto b = cm::add_emblem(p, cm::Family::bruiser, cm::Stat::health, m);
    CHECK(near(b.kit.str, 5.0f, 1e-4f) && near(b.kit.focus, 10.0f, 1e-4f) && near(b.hp, 1000.0f, 1e-4f));
    const auto h = cm::add_emblem(p, cm::Family::home, cm::Stat::health, m);
    CHECK(near(h.recall, 3.0f, 1e-4f));
    cm::Profile r = p;
    r.health_strength = true;
    r = cm::add_shards(r, cm::Stat::health, 5.0f);
    CHECK(near(r.kit.str, 5.0f, 1e-4f));   // Ryth: +1 Strength per 100 bonus health (M43)
}

static void test_horizon_and_gold()
{
    // A slow banker does not lengthen the race: the plan rate sets our pace.
    CHECK(cm::race_horizon(100, 2000, 1000, 0, 0, 1.0f, 0.0f) == 1000.0f);
    CHECK(cm::race_horizon(100, 2000, 1000, 0, 500, 1.0f, 2.0f) == 250.0f);   // they finish first
    CHECK(cm::race_horizon(1990, 2000, 1000, 0, 0, 1.0f, 0.0f) == 10.0f);     // the round limit binds
    CHECK(cm::race_horizon(1000, 2000, 1000, 990, 0, 1.0f, 0.0f) == 50.0f);    // floor
    // Gold an item earns back is worth only reinvest x lambda, so value stays bounded.
    cm::Econ e;
    e.gold_w = 0.4f;
    e.race_left = 800.0f;
    const float v = cm::income_value(cm::Rate{ 0.0f, 0.1f }, e);
    CHECK(near(v, 0.5f * 0.4f * 0.1f * 800.0f, 0.01f));
}

static void test_beacon_codec()
{
    const std::array<std::uint8_t, 3> zero{ 0, 0, 0 };
    CHECK(!cm::decode_beacon(zero).valid());   // zeroed shared memory holds no beacon (M50)
    cm::Beacon b;
    b.x = 97;
    b.y = 3;
    b.round_mod = 61;
    b.asks = 2;
    const auto d = cm::decode_beacon(cm::encode_beacon(b));
    CHECK(d.valid() && d.x == 97 && d.y == 3 && d.round_mod == 61 && d.asks == 2);
    CHECK(cm::beacon_age(130, 61) == 5);   // 130 mod 64 = 2: 2 - 61 + 64 = 5
    const auto s = cm::encode_slot(cm::Mode::converge, 17);
    CHECK(cm::slot_mode(s) == cm::Mode::converge && cm::slot_value(s) == 17);
}

static void test_skirmish()
{
    // Three of ours against one isolated enemy: it dies, we lose nobody.
    std::vector<cm::Fighter> fs{
        { 1500.0f, 80.0f, 1.0f, 1, true }, { 1500.0f, 80.0f, 1.0f, 1, true }, { 1500.0f, 80.0f, 1.0f, 2, true },
        { 1200.0f, 90.0f, 1.0f, 1, false },
    };
    const auto r = cm::simulate_skirmish(fs, 12);
    CHECK(r.their_deaths == 1 && r.our_deaths == 0);
    // One of ours alone against two: we die first.
    std::vector<cm::Fighter> lone{ { 1200.0f, 80.0f, 1.0f, 1, true }, { 1200.0f, 80.0f, 1.0f, 1, false }, { 1200.0f, 80.0f, 1.0f, 1, false } };
    const auto l = cm::simulate_skirmish(lone, 12);
    CHECK(l.our_deaths == 1 && l.their_deaths == 0);
}

int main()
{
    test_camp_forecast();
    test_smith_estimate();
    test_camp_rate_feasibility();
    test_e18_cycle();
    test_death_risk();
    test_best_purchase();
    test_emblems();
    test_horizon_and_gold();
    test_beacon_codec();
    test_skirmish();
    if (failures) std::printf("%d failures\n", failures);
    else std::printf("models: all passed\n");
    return failures ? 1 : 0;
}
