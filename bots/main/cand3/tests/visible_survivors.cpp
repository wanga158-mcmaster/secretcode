#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include "visible_survivors.h"

namespace {

struct Pos { int x{}, y{}; bool operator==(const Pos&) const = default; };
struct Visibility
{
    std::array<bool, 128> tiles{};
    bool is_set(Pos p) const { return p.x >= 0 && p.x < 128 && tiles[static_cast<std::size_t>(p.x)]; }
    bool operator==(const Visibility&) const = default;
};
struct Camp
{
    Pos pos{};
    float hp{}, strength{};
    int group{}, member{};
    bool operator==(const Camp&) const = default;
};
struct Ctx
{
    int round{ 7 };
    Visibility visible;
    std::vector<Camp> camps;
    bool operator==(const Ctx&) const = default;
};
struct Actions { bool moved{}, attacked{}, cast{}; };
struct Group { std::vector<Pos> tiles; bool operator==(const Group&) const = default; };
enum class Status { alive, respawning, reward };
struct Belief
{
    Status status{ Status::alive };
    int seen_round{ 7 };
    bool operator==(const Belief&) const = default;
};
using Borrow = mb::survivors::TurnBorrow<true, Ctx, Actions>;
using mb::model::FightConstants;
using mb::model::FightForecast;
int checks = 0;

void require(bool condition, const std::string& name)
{
    if (!condition) throw std::runtime_error(name);
    ++checks;
}
bool equal(FightConstants a, FightConstants b)
{
    return a.total_hp == b.total_hp && a.weighted_hp == b.weighted_hp && a.total_strength == b.total_strength;
}
bool equal(FightForecast a, FightForecast b)
{
    return a.rounds == b.rounds && a.damage == b.damage && a.peak_rate == b.peak_rate;
}
struct Fixture
{
    Ctx ctx;
    Actions actions;
    Group group{ { { 0, 0 }, { 1, 0 }, { 2, 0 } } };
    Belief belief;
    Fixture()
    {
        ctx.visible.tiles.fill(true);
        ctx.camps = { { { 1, 0 }, 120.0f, 0.0f, 0, 1 }, { { 2, 0 }, 120.0f, 0.0f, 0, 2 } };
    }
    bool extract(FightConstants& out) const
    {
        return mb::survivors::constants<Actions>(ctx, group, belief, 0, out);
    }
};

void reject(Fixture& f, const std::string& name, bool complete = true)
{
    const auto before_ctx = f.ctx;
    const auto before_group = f.group;
    const auto before_belief = f.belief;
    FightConstants out{ -1, -2, -3 };
    const auto before_out = out;
    Borrow scope(f.ctx, f.actions, complete);
    require(!f.extract(out), name);
    require(equal(out, before_out) && f.ctx == before_ctx && f.group == before_group && f.belief == before_belief,
            name + " has no mutation");
}

void composition_contract()
{
    Fixture a, b;
    b.ctx.camps = { { { 0, 0 }, 240.0f, 60.0f, 0, 0 } };
    const auto original = mb::model::fight_constants({ { 2400.0f, 60.0f }, { 120.0f, 0.0f }, { 120.0f, 0.0f } });
    const auto old_a = mb::model::forecast_from_constants(original, 120.0f, 240.0f / original.total_hp);
    const auto old_b = mb::model::forecast_from_constants(original, 120.0f, 240.0f / original.total_hp);
    require(equal(old_a, old_b) && old_a.rounds == 2 && std::abs(old_a.damage - 79.09091f) < 0.0001f,
            "original same-total HP aliases both compositions");
    FightConstants ac, bc;
    const auto before_a = a.ctx;
    const auto before_b = b.ctx;
    {
        Borrow outer(a.ctx, a.actions, true);
        require(a.extract(ac), "dead elk, surviving clusters admitted");
        {
            Borrow inner(b.ctx, b.actions, true);
            require(b.extract(bc), "injured elk, dead clusters admitted");
            require(!a.extract(ac), "borrow identity excludes another snapshot");
        }
        require(a.extract(ac), "nested borrow restores caller facts");
    }
    require(!a.extract(ac), "borrow facts end with scope");
    const auto af = mb::model::forecast_from_constants(ac, 120.0f, 1.0f);
    const auto bf = mb::model::forecast_from_constants(bc, 120.0f, 1.0f);
    require(af.rounds == 2 && af.damage == 0.0f && af.peak_rate == 0.0f, "clusters forecast zero damage");
    require(bf.rounds == 2 && bf.damage == 90.0f && bf.peak_rate == 60.0f, "elk forecast 90 midpoint damage without double scaling");
    require(a.ctx == before_a && b.ctx == before_b, "eligible extraction preserves existing HP and views");
    require(equal(mb::survivors::select(old_a, af), mb::cfg::visible_survivors_apply ? af : old_a),
            "volatile selector applies only the planning result");
}

void freshness_contract()
{
    Fixture f;
    FightConstants out;
    require(!f.extract(out), "caller without observation facts falls back");
    {
        mb::survivors::TurnBorrow<false, Ctx, Actions> off(f.ctx, f.actions, true);
        require(!f.extract(out), "disabled borrow publishes no facts");
    }
    reject(f, "incomplete occupancy", false);
    f.ctx.visible.tiles[0] = false;
    reject(f, "partial original-spawn visibility");
    f.ctx.visible.tiles[0] = true;
    f.belief.seen_round = 6;
    reject(f, "stale group belief");
    f.belief.seen_round = 7;
    {
        Borrow scope(f.ctx, f.actions, true);
        require(f.extract(out), "fresh before actions");
        f.actions.moved = true;
        require(!f.extract(out), "accepted move invalidates live borrow");
        f.actions.moved = false;
        f.actions.attacked = true;
        require(!f.extract(out), "accepted basic invalidates live borrow");
        f.actions.attacked = false;
        f.actions.cast = true;
        require(!f.extract(out), "accepted cast invalidates live borrow");
        f.actions.cast = false;
        ++f.ctx.round;
        f.belief.seen_round = f.ctx.round;
        require(!f.extract(out), "observation round cannot be refreshed by belief alone");
    }
    Fixture all;
    all.ctx.camps.push_back({ { 0, 0 }, 240.0f, 60.0f, 0, 0 });
    reject(all, "all original members living");
    const FightConstants original{ 2640.0f, 144000.0f, 60.0f };
    auto chosen = mb::model::forecast_from_constants(original, 120.0f, 480.0f / 2640.0f);
    const auto old = chosen;
    {
        Borrow scope(all.ctx, all.actions, true);
        if (all.extract(out)) chosen = mb::survivors::select(chosen, mb::model::forecast_from_constants(out, 120.0f, 1.0f));
    }
    require(equal(chosen, old), "all-alive forecast equals original exactly");
    Fixture state;
    state.belief.status = Status::respawning;
    reject(state, "respawning group");
    state.belief.status = Status::reward;
    reject(state, "reward group");
}

void bounds_and_integrity_contract()
{
    Fixture f;
    f.group.tiles.clear();
    reject(f, "empty original group");
    for (int i = 0; i < 33; ++i) f.group.tiles.push_back({ i, 0 });
    reject(f, "33 originals exceed bound");
    f.group.tiles.pop_back();
    f.ctx.camps = { { { 31, 0 }, 120.0f, 20.0f, 0, 31 } };
    FightConstants out;
    {
        Borrow scope(f.ctx, f.actions, true);
        require(f.extract(out) && out.total_hp == 120.0f, "32 originals and bit 31 are supported");
    }
    Fixture cached;
    for (int i = 0; i < 62; ++i) cached.ctx.camps.push_back({ { i, 0 }, 10.0f, 1.0f, 1, i });
    {
        Borrow scope(cached.ctx, cached.actions, true);
        require(cached.extract(out), "64 total cached camps are supported");
    }
    cached.ctx.camps.push_back({ { 63, 0 }, 10.0f, 1.0f, 1, 63 });
    reject(cached, "65 total cached camps exceed bound");
    Fixture invalid;
    invalid.ctx.camps[0].member = -1;
    reject(invalid, "negative member");
    invalid.ctx.camps[0].member = 3;
    reject(invalid, "member beyond original group");
    invalid.ctx.camps[0].member = 1;
    invalid.ctx.camps[0].pos = { 0, 0 };
    reject(invalid, "member position mismatch");
    invalid = Fixture{};
    invalid.ctx.camps.push_back(invalid.ctx.camps[0]);
    reject(invalid, "duplicate occupancy member");
    invalid = Fixture{};
    invalid.ctx.camps[0].hp = 0;
    reject(invalid, "zero-HP view is not living occupancy");
    invalid = Fixture{};
    invalid.ctx.camps.clear();
    reject(invalid, "no confirmed surviving member");
    Fixture ordering;
    ordering.group.tiles.push_back({ 3, 0 });
    ordering.ctx.camps = { { { 0, 0 }, 20.0f, 10.0f, 0, 0 }, { { 2, 0 }, 90.0f, 30.0f, 0, 2 },
                           { { 3, 0 }, 10.0f, 10.0f, 0, 3 } };
    Borrow scope(ordering.ctx, ordering.actions, true);
    require(ordering.extract(out), "ordering fixture eligible");
    const auto expected = out;
    do {
        const auto before = ordering.ctx;
        require(ordering.extract(out) && equal(out, expected) && ordering.ctx == before,
                "all occupancy order permutations preserve constants and inputs");
    } while (std::next_permutation(ordering.ctx.camps.begin(), ordering.ctx.camps.end(),
                                  [](const Camp& a, const Camp& b) { return a.member < b.member; }));
}

} // namespace

int main()
{
    try
    {
        composition_contract();
        freshness_contract();
        bounds_and_integrity_contract();
        std::cout << "visible survivor contracts: " << checks << " passed; selector="
                  << (mb::cfg::visible_survivors_apply ? "use" : "shadow") << '\n';
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
