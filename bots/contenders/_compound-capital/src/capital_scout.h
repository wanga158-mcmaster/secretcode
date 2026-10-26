#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

namespace mb::capital::scout {

// Three bytes: owner+1 (4 bits), group+1 (8), modulo4096 expiry (12). Short leases
// stay below128 rounds; max_rounds<4096 prevents an abandoned byte record resurrecting.
struct WireLease { int owner{-1}, group{-1}, deadline{-1}; };
inline std::array<std::uint8_t, 3> encode(WireLease lease)
{
    if (lease.owner < 0 || lease.owner >= 15 || lease.group < 0 || lease.group >= 255 || lease.deadline < 0)
        return {};
    const std::uint32_t bits = static_cast<unsigned>(lease.owner + 1) |
        (static_cast<unsigned>(lease.group + 1) << 4) | ((static_cast<unsigned>(lease.deadline) & 4095u) << 12);
    return {static_cast<std::uint8_t>(bits), static_cast<std::uint8_t>(bits >> 8), static_cast<std::uint8_t>(bits >> 16)};
}
inline WireLease decode(const std::array<std::uint8_t, 3>& bytes)
{
    const unsigned bits = bytes[0] | (static_cast<unsigned>(bytes[1]) << 8) | (static_cast<unsigned>(bytes[2]) << 16);
    if ((bits & 15u) == 0) return {};
    return {static_cast<int>(bits & 15u) - 1, static_cast<int>((bits >> 4) & 255u) - 1,
            static_cast<int>((bits >> 12) & 4095u)};
}
inline bool live(WireLease lease, int round, int max_rounds, bool owner_alive)
{
    if (!owner_alive || round < 0 || max_rounds <= 0 || max_rounds >= 4096 || round >= max_rounds ||
        lease.owner < 0 || lease.owner >= 15 || lease.group < 0 || lease.group >= 255 ||
        lease.deadline < 0 || lease.deadline > max_rounds) return false;
    const int left = (lease.deadline - round) & 4095;
    return left > 0 && left < 128;
}
inline int short_deadline(int round, int fixed_horizon)
{
    return fixed_horizon > round ? std::min(fixed_horizon, round + 16) : -1;
}

struct Point { int x{-1}, y{-1}; bool operator==(const Point&) const = default; };
struct Rect { int min_x{}, min_y{}, max_x{-1}, max_y{-1}; };
inline bool valid(Rect rect) { return rect.min_x <= rect.max_x && rect.min_y <= rect.max_y; }
inline Rect joint_lens(Rect gold, Rect gem, int radius, int width, int height)
{
    if (!valid(gold) || !valid(gem) || radius < 0 || width <= 0 || height <= 0) return {};
    return {std::max({0, gold.max_x - radius, gem.max_x - radius}),
            std::max({0, gold.max_y - radius, gem.max_y - radius}),
            std::min({width - 1, gold.min_x + radius, gem.min_x + radius}),
            std::min({height - 1, gold.min_y + radius, gem.min_y + radius})};
}
inline int lens_size(Rect rect)
{
    return valid(rect) ? (rect.max_x - rect.min_x + 1) * (rect.max_y - rect.min_y + 1) : 0;
}
inline Point lens_candidate(Rect rect, Point origin, int cursor)
{
    if (!valid(rect) || cursor < 0 || cursor >= lens_size(rect) + 2) return {};
    if (cursor == 0) return {std::clamp(origin.x, rect.min_x, rect.max_x), std::clamp(origin.y, rect.min_y, rect.max_y)};
    if (cursor == 1) return {(rect.min_x + rect.max_x) / 2, (rect.min_y + rect.max_y) / 2};
    const int index = cursor - 2, width = rect.max_x - rect.min_x + 1;
    return {rect.min_x + index % width, rect.min_y + index / width};
}

} // namespace mb::capital::scout

#ifndef CAPITAL_SCOUT_PURE_TEST
#include <cambc.h>
namespace mb {
struct Ctx;
enum class ScoutAbort : std::uint8_t { external, contact, death, changed_target, state, claim, timeout, no_vantage, no_route, occupancy };
struct ScoutDirective
{
    enum class Kind : std::uint8_t { none, planning, move, observed };
    Kind kind{Kind::none};
    const cambc::distance_field* route{};
    int gold{-1}, gem{-1};
    cambc::vec2 waypoint{-1,-1};
    int started{}, elapsed{}, deadline{};
    bool all_visible{}; // current personal vision and fresh alive states, never hypothetical vision
};
struct EpisodeLease
{
    int owner{-1}, group{-1}, expires{-1}, absolute_limit{-1};
    bool active{}, self_owned{};
};

// Call once per observed turn, including channels/health overrides. Death/contact releases;
// health-only trips may renew an already transferred episode lease to its fixed horizon.
void episode_lease_refresh(const Ctx& ctx);
EpisodeLease episode_lease_snapshot(int round);
bool episode_reserved(int group, int round); // only foreign live owner; cached per round
bool episode_lease_reserve(const Ctx& ctx, int group, int absolute_deadline);
bool episode_lease_transfer(const Ctx& ctx, int group, int absolute_deadline);
bool episode_lease_renew(const Ctx& ctx);
void episode_lease_release(int round);
void episode_scout_abort(int round, ScoutAbort reason = ScoutAbort::external);

// Information gathering only. Parent must use step_along(*route) for current dynamic legality,
// then consume only a freshly returned observed directive before a bounded episode appraisal.
ScoutDirective episode_scout(const Ctx& ctx, int gold_target, bool purse_can_invest);
} // namespace mb
#endif
