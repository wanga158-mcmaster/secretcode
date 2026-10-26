// Pure equivalence tests for the exact skirmish memo. Native fuel/dispatch is a
// separate caller test: these cases never substitute a model for engine behavior.
#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <vector>
#include "model.h"

using namespace mb::model;

namespace {
int oracle_checks = 0;

bool same_float(float a, float b)
{
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

void same_outcome(const SkirmishOutcome& a, const SkirmishOutcome& b)
{
    assert(same_float(a.our_losses, b.our_losses));
    assert(same_float(a.their_losses, b.their_losses));
    assert(a.rounds == b.rounds && a.first_target == b.first_target);
    assert(a.we_survive == b.we_survive && a.they_survive == b.they_survive);
}

SkirmishOutcome checked(SkirmishMemo& memo, const Fighter* f, int n, int horizon = 15, bool threat = false)
{
    const auto oracle = simulate_skirmish(f, n, horizon, threat);
    const auto first = memo.evaluate(f, n, horizon, threat);
    same_outcome(first, oracle);
    assert(memo.matches(f, n, horizon, threat) == (n >= 0));
    same_outcome(memo.evaluate(f, n, horizon, threat), oracle);
    ++oracle_checks;
    return first;
}

std::array<Fighter, 4> encounter()
{
    std::array<Fighter, 4> f{Fighter{1200, 70, true}, Fighter{900, 50, true},
                             Fighter{500, 80, false}, Fighter{1100, 40, false}};
    f[0].cast[0] = 90;
    f[0].cast[1] = 30;
    f[1].ready[0] = 2;
    f[2].cast[0] = 120;
    f[2].ready[0] = 3;
    f[3].arrive = 2;
    return f;
}

void mutate(Fighter& f, int field, int index)
{
    switch (field)
    {
    case 0: f.hp += 1; break;
    case 1: f.dps += 1; break;
    case 2: f.ours = !f.ours; break;
    case 3: f.value += 1; break;
    case 4: f.linked = index ^ 1; break;
    case 5: f.cast[0] += 1; break;
    case 6: f.cast[1] += 1; break;
    case 7: ++f.ready[0]; break;
    case 8: ++f.ready[1]; break;
    case 9: ++f.cooldown; break;
    case 10: ++f.arrive; break;
    default: assert(false);
    }
}

// Change only bytes outside every named member. This deliberately creates two
// valid objects with equal semantic values and unequal padding, where present.
std::size_t fill_padding(Fighter& f, unsigned char value)
{
    static_assert(std::is_standard_layout_v<Fighter>);
    std::array<bool, sizeof(Fighter)> named{};
    auto mark = [&](std::size_t offset, std::size_t size) {
        for (std::size_t i = offset; i < offset + size; ++i) named[i] = true;
    };
    mark(offsetof(Fighter, hp), sizeof(f.hp));
    mark(offsetof(Fighter, dps), sizeof(f.dps));
    mark(offsetof(Fighter, ours), sizeof(f.ours));
    mark(offsetof(Fighter, value), sizeof(f.value));
    mark(offsetof(Fighter, linked), sizeof(f.linked));
    mark(offsetof(Fighter, cast), sizeof(f.cast));
    mark(offsetof(Fighter, ready), sizeof(f.ready));
    mark(offsetof(Fighter, cooldown), sizeof(f.cooldown));
    mark(offsetof(Fighter, arrive), sizeof(f.arrive));
    auto* bytes = reinterpret_cast<unsigned char*>(&f);
    std::size_t changed = 0;
    for (std::size_t i = 0; i < named.size(); ++i)
        if (!named[i]) { bytes[i] = value; ++changed; }
    return changed;
}

void complete_keys()
{
    auto original = encounter();
    for (auto& f : original) fill_padding(f, 0);
    SkirmishMemo memo;
    assert(!memo.matches(original.data(), 4, 15, false));
    checked(memo, original.data(), 4);
    auto copy = original;
    assert(copy.data() != original.data());
    assert(memo.matches(copy.data(), 4, 15, false));
    std::size_t changed_padding = 0;
    for (auto& f : copy) changed_padding += fill_padding(f, 0xff);
    if (changed_padding) assert(std::memcmp(copy.data(), original.data(), sizeof(copy)) != 0);
    assert(memo.matches(copy.data(), 4, 15, false));
    checked(memo, copy.data(), 4);

    // Every field of every fighter participates, even when a field happens not
    // to affect this particular encounter's outcome (e.g. a zero-damage slot).
    for (int index = 0; index < 4; ++index)
        for (int field = 0; field < 11; ++field)
        {
            auto changed = original;
            checked(memo, original.data(), 4);
            mutate(changed[index], field, index);
            assert(!memo.matches(changed.data(), 4, 15, false));
            checked(memo, changed.data(), 4);
            assert(!memo.matches(original.data(), 4, 15, false));
        }
    checked(memo, original.data(), 4);
    assert(!memo.matches(original.data(), 3, 15, false));
    checked(memo, original.data(), 3);
    assert(!memo.matches(original.data(), 3, 14, false));
    checked(memo, original.data(), 3, 14);
    assert(!memo.matches(original.data(), 3, 14, true));
    checked(memo, original.data(), 3, 14, true);
    auto reordered = original;
    std::swap(reordered[2], reordered[3]);
    checked(memo, original.data(), 4);
    assert(!memo.matches(reordered.data(), 4, 15, false));
    checked(memo, reordered.data(), 4);
}

void floating_point_keys()
{
    using Access = float& (*)(Fighter&);
    const std::array<Access, 5> field{+[](Fighter& f) -> float& { return f.hp; },
        +[](Fighter& f) -> float& { return f.dps; }, +[](Fighter& f) -> float& { return f.value; },
        +[](Fighter& f) -> float& { return f.cast[0]; }, +[](Fighter& f) -> float& { return f.cast[1]; }};
    for (const auto get : field)
    {
        auto f = encounter();
        SkirmishMemo memo;
        get(f[0]) = 0.0f;
        checked(memo, f.data(), 4);
        get(f[0]) = -0.0f;
        assert(!memo.matches(f.data(), 4, 15, false));
        checked(memo, f.data(), 4);
        get(f[0]) = std::bit_cast<float>(std::uint32_t{0x7fc00001});
        assert(!memo.matches(f.data(), 4, 15, false));
        checked(memo, f.data(), 4);
        auto equal_nan = f;
        assert(memo.matches(equal_nan.data(), 4, 15, false));
        get(f[0]) = std::bit_cast<float>(std::uint32_t{0x7fc00002});
        assert(!memo.matches(f.data(), 4, 15, false));
        checked(memo, f.data(), 4);
        get(f[0]) = std::bit_cast<float>(std::uint32_t{0xffc00002});
        assert(!memo.matches(f.data(), 4, 15, false));
        checked(memo, f.data(), 4);
    }
}

void causal_outcomes()
{
    SkirmishMemo memo;
    Fighter duel[]{Fighter{500, 100, true}, Fighter{500, 100, false}};
    auto equal = checked(memo, duel, 2, 20);
    assert(equal.rounds == 5 && !equal.we_survive && !equal.they_survive);
    duel[1].hp = 501;
    auto extra_hp = checked(memo, duel, 2, 20);
    assert(extra_hp.rounds == 5 && !extra_hp.we_survive && extra_hp.they_survive);
    duel[1].hp = 500; duel[0].dps = 200;
    auto fast = checked(memo, duel, 2, 20);
    assert(fast.rounds == 3 && fast.we_survive && !fast.they_survive);
    duel[0].dps = 100; duel[0].arrive = 3;
    auto arrival = checked(memo, duel, 2, 20);
    assert(arrival.rounds == 7 && !arrival.we_survive && !arrival.they_survive);
    auto short_horizon = checked(memo, duel, 2, 4);
    assert(short_horizon.we_survive && short_horizon.they_survive);

    Fighter burst[]{Fighter{1000, 0, true}, Fighter{500, 1, false}};
    burst[0].cast[0] = 250;
    auto immediate = checked(memo, burst, 2, 20);
    assert(immediate.rounds == 9 && !immediate.they_survive);
    burst[0].ready[0] = 2;
    auto later = checked(memo, burst, 2, 20);
    assert(later.rounds == 10 && !later.they_survive);
    burst[0].ready[0] = 1; burst[0].cooldown = 3;
    assert(checked(memo, burst, 2, 20).rounds == 4);
    burst[0].cast[1] = 300;
    assert(checked(memo, burst, 2, 20).rounds == 2);  // One cast per round, stronger first.
    burst[0].cast[1] = 0; burst[0].cast[0] = 500;
    assert(checked(memo, burst, 2, 20).rounds == 1);

    Fighter linked[]{Fighter{3000, 200, true}, Fighter{400, 50, false}, Fighter{900, 50, false}};
    assert(checked(memo, linked, 3, 20).rounds == 7);
    linked[1].linked = 2; linked[2].linked = 1;
    auto together = checked(memo, linked, 3, 20);
    assert(together.rounds == 2 && together.their_losses == 2 && !together.they_survive);
    linked[1].value = 3;
    assert(checked(memo, linked, 3, 20).their_losses == 4);

    Fighter threat[]{Fighter{1000, 100, true}, Fighter{200, 10, false}, Fighter{400, 200, false}};
    auto plain = checked(memo, threat, 3, 20);
    auto smart = checked(memo, threat, 3, 20, true);
    assert(plain.first_target == 1 && !plain.we_survive);
    assert(smart.first_target == 2 && smart.we_survive && !smart.they_survive);
}

void bounds_and_instances()
{
    SkirmishMemo empty;
    assert(!empty.matches(nullptr, 0, 15, false));
    checked(empty, nullptr, 0);
    assert(!empty.matches(nullptr, -1, 15, false));
    checked(empty, nullptr, -1);
    assert(!empty.matches(nullptr, -1, 15, false));

    std::array<Fighter, 17> many;
    for (int i = 0; i < 17; ++i) many[i] = Fighter{500.0f + i, 10.0f + i, i % 2 == 0};
    SkirmishMemo memo;
    const auto clamped = checked(memo, many.data(), 17, 40);
    same_outcome(clamped, simulate_skirmish(many.data(), 16, 32));
    assert(memo.matches(many.data(), 16, 40, false));
    many[16] = Fighter{1, 999999, false};
    assert(memo.matches(many.data(), 17, 40, false));
    same_outcome(checked(memo, many.data(), 17, 40), clamped);
    many[15].hp += 1;
    assert(!memo.matches(many.data(), 17, 40, false));
    checked(memo, many.data(), 17, 40);
    many[15].hp -= 1;
    checked(memo, many.data(), 17, 40);
    // Horizon is an exact raw key even where the underlying model clamps it.
    assert(!memo.matches(many.data(), 17, 32, false));
    same_outcome(checked(memo, many.data(), 17, 32), clamped);

    auto primary = encounter();
    auto away_from_spawn = primary;
    away_from_spawn[0].hp *= 0.25f;
    SkirmishMemo normal, counterfactual;
    for (int turn = 0; turn < 5; ++turn)
    {
        checked(normal, primary.data(), 4);
        checked(counterfactual, away_from_spawn.data(), 4);
        assert(normal.matches(primary.data(), 4, 15, false));
        assert(counterfactual.matches(away_from_spawn.data(), 4, 15, false));
        assert(!normal.matches(away_from_spawn.data(), 4, 15, false));
        assert(!counterfactual.matches(primary.data(), 4, 15, false));
    }
}

#ifdef SKIRMISH_MEMO_NATIVE_CASES
// Optional regenerable corpus replay; the durable test carries no per-game arrays.
#include "skirmish-memo-native.inc"
#endif
} // namespace

int main()
{
    complete_keys();
    floating_point_keys();
    causal_outcomes();
    bounds_and_instances();
#ifdef SKIRMISH_MEMO_NATIVE_CASES
    saved_native_arrays();
#endif
    std::printf("main-skirmish-memo: %d full-outcome oracle checks passed\n", oracle_checks);
}
