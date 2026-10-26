// Diagnostic only: one greedy choose or argmax call per fresh ordinary owner turn.
// Includes production inference unchanged. No gameplay actions or learned-policy choices.
#include "../../zero/src/core/net.cpp"
#include <cambc.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

using zero::net::Pick;
volatile int index_sink;
volatile float logp_sink;

__attribute__((noinline)) Pick old_choice(const float* logits, int n, std::uint64_t& rng)
{
    asm volatile("" : : "r"(logits), "r"(n) : "memory");
    return zero::net::choose(logits, n, 0.0f, rng);
}

__attribute__((noinline)) Pick new_choice(const float* logits, int n)
{
    asm volatile("" : : "r"(logits), "r"(n) : "memory");
    Pick pick;
    // Same strict comparison as the explicit controller's deterministic, unlogged path.
    for (int i = 1; i < n; ++i) if (logits[i] > logits[pick.index]) pick.index = i;
    pick.logp = 0.0f;
    return pick;
}

struct Measurement
{
    Pick pick;
    std::uint64_t before{}, after{}, cost{};
    int round{}, rounds{};
};

struct Probe
{
    bool started{}, old_phase{ true };
    int test{}, actor{}, n{}, variant{}, expected{};
    float logits[zero::kMaxCands]{};
    std::uint64_t rng{}, rng_before{};
    Measurement old;
};

Probe& probe() { static Probe p; return p; }
constexpr int counts[]{ 1, 2, 4, 8, 12, 16, 24 };
constexpr int kTests = static_cast<int>(sizeof(counts) / sizeof(counts[0])) * 3;

void prepare(Probe& p)
{
    p.n = counts[p.test / 3];
    p.variant = p.test % 3;
    p.rng = zero::net::mix(p.rng ^ static_cast<std::uint64_t>(cambc::round_number()));
    const float base = static_cast<float>((p.rng >> 32) & 1023u) / 128.0f - 8.0f;
    for (int i = 0; i < p.n; ++i) p.logits[i] = base - static_cast<float>(i + 1) * 0.137f;
    if (p.variant == 0)
    {
        p.expected = static_cast<int>(p.rng % static_cast<std::uint64_t>(p.n));
        p.logits[p.expected] = base + 4.25f;  // one maximum; finite unequal values
    }
    else if (p.variant == 1)
    {
        std::fill(p.logits, p.logits + p.n, base);  // all tied: first maximum wins
        p.expected = 0;
    }
    else
    {
        p.expected = p.n < 3 ? 0 : p.n / 3;
        p.logits[p.expected] = p.logits[p.n - 1] = base + 3.25f;
    }
    p.rng_before = p.rng;
}

Measurement measure(Probe& p, bool old)
{
    Measurement m;
    m.round = static_cast<int>(cambc::round_number());
    asm volatile("" : : : "memory");
    m.before = cambc::fuel_remaining();
    asm volatile("" : : : "memory");
    m.pick = old ? old_choice(p.logits, p.n, p.rng) : new_choice(p.logits, p.n);
    index_sink = m.pick.index;
    logp_sink = m.pick.logp;
    asm volatile("" : : : "memory");
    m.after = cambc::fuel_remaining();
    asm volatile("" : : : "memory");
    m.rounds = static_cast<int>(cambc::round_number()) - m.round;
    m.cost = m.before + static_cast<std::uint64_t>(std::max(0, m.rounds)) * 100000u - m.after;
    return m;
}

void play()
{
    auto& p = probe();
    if (!p.started)
    {
        p.actor = static_cast<int>(static_cast<cambc::handle_champion_t>(cambc::self()));
        p.rng = zero::net::mix(static_cast<std::uint64_t>(p.actor + 1) * 0x9e3779b97f4a7c15ull);
        prepare(p);
        p.started = true;
        return;  // initialization uses the 200K first gameplay turn; measurements start next turn
    }
    if (p.test >= kTests) return;
    if (p.old_phase)
    {
        p.old = measure(p, true);
        p.old_phase = false;
        return;  // no formatted output competes with the old call's remaining fuel
    }
    const auto fresh = measure(p, false);
    const bool match = p.old.pick.index == fresh.pick.index && fresh.pick.index == p.expected;
    const bool rng_same = p.rng == p.rng_before;
    std::printf("choicecheck actor%d test%d n%d variant%d old_round%d new_round%d old_before%llu old_after%llu old_fuel%llu new_before%llu new_after%llu new_fuel%llu old_index%d new_index%d expected%d old_logp%.9g new_logp%.9g old_rounds%d new_rounds%d rng_same%d match%d\n",
                p.actor, p.test, p.n, p.variant, p.old.round, fresh.round,
                static_cast<unsigned long long>(p.old.before), static_cast<unsigned long long>(p.old.after), static_cast<unsigned long long>(p.old.cost),
                static_cast<unsigned long long>(fresh.before), static_cast<unsigned long long>(fresh.after), static_cast<unsigned long long>(fresh.cost),
                p.old.pick.index, fresh.pick.index, p.expected, static_cast<double>(p.old.pick.logp), static_cast<double>(fresh.pick.logp),
                p.old.rounds, fresh.rounds, rng_same, match);
    if (!match || !rng_same || !std::isfinite(p.old.pick.logp) || fresh.pick.logp != 0.0f || p.old.rounds || fresh.rounds)
        std::printf("error: choicecheck mismatch or suspension actor%d test%d\n", p.actor, p.test);
    ++p.test;
    p.old_phase = true;
    if (p.test < kTests) prepare(p);
    else std::printf("choicecheck done actor%d tests%d\n", p.actor, kTests);
}

}  // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    while (true)
    {
        try
        {
            if (cambc::is_draft())
            {
                const auto ours = cambc::draft::current_draft(cambc::team::ally);
                for (int i = 0; i < 12; ++i)
                {
                    const auto pick = static_cast<cambc::champion_draft>(i);
                    if (std::find(ours.begin(), ours.end(), pick) == ours.end()) { cambc::draft::pick(pick); break; }
                }
            }
            else play();
        }
        catch (const std::exception& e) { std::printf("error: choicecheck exception %s\n", e.what()); }
        catch (...) { std::printf("error: choicecheck unknown exception\n"); }
        cambc::end_turn();
    }
}
