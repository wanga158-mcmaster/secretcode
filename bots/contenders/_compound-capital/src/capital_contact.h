#pragma once
// Conditional basic-only melee execution at one fixed legal stance. The runtime must
// bind this target order and exclude movement, casts, other attackers and hidden members.
// No route, future camp availability, healing or ability-period estimate is invented here.
#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace mb::capital::contact {

inline constexpr unsigned max_monsters = 10;
inline constexpr int max_turns = 256;

struct Monster
{
    int id{-1};             // stable runtime identity; kill_order contains these ids
    float hp{};
    float incoming{};       // this monster's queried per-volley damage, after mitigation
    bool observed{};        // exact remaining hp/state, not a static maximum or belief
};

struct Input
{
    std::array<Monster, max_monsters> monsters{};
    unsigned count{};
    float basic_damage{};   // queried Strength times outgoing monster item multiplier
    float health{}, health_floor{};
    int horizon{};          // completed champion opportunities; additionally capped at256
    bool opener_before_volley{}; // current round's camp phase has already happened
    bool attack_ready{};    // first champion opportunity only; later turns restore basics
    bool readiness_known{}, known{};
};

enum class Status { unknown, invalid_input, cleared, health_floor, dead, horizon };
struct Result
{
    Status status{Status::unknown};
    int turns{}, hits{}, casts{}, first_target{-1};
    float damage{}, end_health{}, minimum_health{};
    std::array<int, max_monsters> kill_order{};
    unsigned killed{};
};

namespace detail {
inline bool nonnegative(float value) { return std::isfinite(value) && value >= 0; }

inline bool valid(std::span<const Monster> monsters)
{
    if (monsters.empty() || monsters.size() > max_monsters) return false;
    for (unsigned i = 0; i < monsters.size(); ++i)
    {
        const auto& m = monsters[i];
        if (!m.observed || m.id < 0 || !nonnegative(m.hp) || !nonnegative(m.incoming)) return false;
        for (unsigned j = 0; j < i; ++j) if (monsters[j].id == m.id) return false;
    }
    return true;
}

// Smith priority: descending incoming/current hp, then stable identity. Double products
// avoid overflow from ratios of arbitrary finite float inputs. A wounded focus stays first.
inline int select(std::span<const Monster> monsters)
{
    int best = -1;
    for (unsigned i = 0; i < monsters.size(); ++i)
    {
        const auto& m = monsters[i];
        if (m.hp <= 0) continue;
        if (best < 0) { best = static_cast<int>(i); continue; }
        const auto& b = monsters[static_cast<unsigned>(best)];
        const double left = static_cast<double>(m.incoming) * b.hp;
        const double right = static_cast<double>(b.incoming) * m.hp;
        if (left > right || (left == right && m.id < b.id)) best = static_cast<int>(i);
    }
    return best;
}
} // namespace detail

// Returns the array index to attack, or -1 for an unknown/invalid/already cleared snapshot.
// The runtime may call this on refreshed observed hp to enforce exactly the modeled policy.
inline int next_target(std::span<const Monster> monsters)
{
    return detail::valid(monsters) ? detail::select(monsters) : -1;
}

inline Result simulate(const Input& input)
{
    Result result;
    if (!input.known || !input.readiness_known) return result;
    if (input.count == 0 || input.count > max_monsters || !std::isfinite(input.basic_damage) || input.basic_damage <= 0 ||
        !std::isfinite(input.health) || input.health <= 0 || !detail::nonnegative(input.health_floor) || input.horizon < 0)
    {
        result.status = Status::invalid_input;
        return result;
    }
    auto monsters = input.monsters;
    const std::span<Monster> live{monsters.data(), input.count};
    for (const auto& m : live) if (!m.observed) return result;
    if (!detail::valid(live)) { result.status = Status::invalid_input; return result; }
    result.end_health = result.minimum_health = input.health;
    const auto finish = [&](Status status) {
        result.status = status;
        result.damage = input.health - result.end_health; // actual hp lost, no lethal overkill credit
        return result;
    };
    if (result.end_health < input.health_floor) return finish(Status::health_floor);
    if (detail::select(live) < 0) return finish(Status::cleared);

    for (int turn = 0, limit = std::min(input.horizon, max_turns); turn < limit; ++turn)
    {
        result.turns = turn + 1;
        if (turn > 0 || !input.opener_before_volley)
        {
            // Array order is the runtime-supplied camp actor order. Finish the entire volley
            // before applying the champion's floor gate; reaching the floor cannot cancel it.
            for (const auto& m : live)
                if (m.hp > 0)
                {
                    result.end_health = std::max(0.0f, result.end_health - m.incoming);
                    result.minimum_health = std::min(result.minimum_health, result.end_health);
                    if (result.end_health <= 0) return finish(Status::dead);
                }
            if (result.end_health < input.health_floor) return finish(Status::health_floor);
        }
        if (turn == 0 && !input.attack_ready) continue;
        const int target = detail::select(live);
        if (target < 0) return finish(Status::cleared);
        auto& monster = live[static_cast<unsigned>(target)];
        if (result.first_target < 0) result.first_target = monster.id;
        ++result.hits;
        // Basic overkill never transfers to another monster. Casts and splash are excluded.
        monster.hp = std::max(0.0f, monster.hp - input.basic_damage);
        if (monster.hp <= 0) result.kill_order[result.killed++] = monster.id;
        if (detail::select(live) < 0) return finish(Status::cleared);
    }
    return finish(Status::horizon);
}

} // namespace mb::capital::contact
