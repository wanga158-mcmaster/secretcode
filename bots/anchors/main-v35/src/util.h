#pragma once
// Small helpers shared by every module: geometry, fuel checks and bounded logging.
#include <cambc.h>
#include <cstdint>
#include <cstdio>

namespace mb {

using cambc::vec2;

// Chebyshev distance in 32-bit arithmetic (the SDK's 64-bit version costs ~5x more fuel).
inline int cheb(vec2 a, vec2 b)
{
    const int dx = a.x > b.x ? a.x - b.x : b.x - a.x;
    const int dy = a.y > b.y ? a.y - b.y : b.y - a.y;
    return dx > dy ? dx : dy;
}

inline constexpr vec2 kDirs[8] = { vec2{ 0, 1 }, vec2{ 1, 1 }, vec2{ 1, 0 }, vec2{ 1, -1 },
                                   vec2{ 0, -1 }, vec2{ -1, -1 }, vec2{ -1, 0 }, vec2{ -1, 1 } };

inline std::uint64_t fuel() { return cambc::fuel_remaining(); }

// True when at least `need` fuel remains on top of the configured reserve.
bool fuel_ok(std::uint64_t need);

// Bounded logging: each instance prints at most a fixed number of lines so the platform's
// per-team log limit is never the reason later diagnostics disappear.
bool log_allowed();
#define MB_LOG(...)                                                                            \
    do {                                                                                       \
        if (::mb::log_allowed()) std::printf(__VA_ARGS__);                                     \
    } while (0)

// Fuel checkpoints within one turn. A suspension (fuel exhaustion) shows up as fuel
// increasing between checkpoints because the turn resumed with a fresh budget.
// Logs a scope that used more than `limit` fuel (diagnostics for budget spikes).
struct FuelSpike
{
    const char* name;
    std::uint64_t start;
    std::uint64_t limit;
    FuelSpike(const char* n, std::uint64_t l = 25000) : name{ n }, start{ fuel() }, limit{ l } {}
    ~FuelSpike()
    {
        const auto now = fuel();
        if (start > now && start - now > limit) MB_LOG("spike %s %llu\n", name, static_cast<unsigned long long>(start - now));
    }
};

struct FuelTrace
{
    static constexpr int kMax = 14;
    std::uint64_t start{};
    std::uint64_t last{};
    int n{};
    const char* names[kMax]{};
    std::uint64_t used[kMax]{};
    bool suspended{};
    const char* suspended_in{};

    void begin()
    {
        start = last = fuel();
        n = 0;
        suspended = false;
    }
    void mark(const char* name)
    {
        const auto now = fuel();
        if (now > last)
        {
            suspended = true;
            suspended_in = name;
        }
        if (n < kMax)
        {
            names[n] = name;
            used[n] = last > now ? last - now : 0;
            ++n;
        }
        last = now;
    }
    // Print a breakdown when the turn was heavy or suspended.
    void report(int round, std::uint64_t threshold) const;
};

} // namespace mb
