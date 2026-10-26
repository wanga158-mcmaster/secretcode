#pragma once
// Independent production episodes. Observations and commands live in the caller: this kernel
// never promotes an ally message, expired respawn estimate or remembered HP into world truth.
#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <vector>

namespace mb::production {

enum class Phase { idle, approach, work, recovery, capture, conversion };

struct WakeMember
{
    int group{}, x{}, y{};
    bool alive{};
};

// Damaging any live member wakes its whole group, including harmful siblings beyond the
// splash radius. Strength is deliberately absent: a harmless struck member also triggers it.
inline std::vector<unsigned char> collateral_wake_groups(std::span<const WakeMember> members,
                                                        int target_group, int radius, int group_count)
{
    std::vector<unsigned char> woken(static_cast<std::size_t>(std::max(0, group_count)), 0);
    if (target_group < 0 || radius <= 0) return woken;
    for (const auto& struck : members)
    {
        if (!struck.alive || struck.group == target_group || struck.group < 0 || struck.group >= group_count) continue;
        for (const auto& target : members)
            if (target.alive && target.group == target_group &&
                std::max(std::abs(struck.x - target.x), std::abs(struck.y - target.y)) <= radius)
            {
                woken[static_cast<std::size_t>(struck.group)] = 1;
                break;
            }
    }
    return woken;
}

struct Episode
{
    int group{ -1 };
    Phase phase{ Phase::idle };
    int started_round{ -1 }, last_progress_round{ -1 }, recovery_round{ -1 };
    float best_hp{ -1.0f };
    unsigned bags_before{};
    int recoveries{};

    void begin(int g, int round, float hp, unsigned bag)
    {
        release();
        if (g < 0) return;
        group = g;
        phase = Phase::approach;
        started_round = last_progress_round = round;
        best_hp = std::isfinite(hp) && hp >= 0.0f ? hp : -1.0f;
        bags_before = bag;
    }

    void release() { *this = Episode{}; }

    // The caller supplies an actually observed remaining group HP. Equal/increased HP is
    // not progress; an unknown initial HP merely establishes the first measurement.
    void observe_progress(int round, float hp)
    {
        if (group < 0 || !std::isfinite(hp) || hp < 0.0f) return;
        phase = Phase::work;
        if (best_hp < 0.0f)
            best_hp = hp;
        else if (hp < best_hp - 0.01f)
        {
            best_hp = hp;
            last_progress_round = round;
        }
    }

    // Recovery preserves the identity and observed partial work. Repeated retreat turns
    // remain one recovery, rather than extending the recovery deadline indefinitely.
    void recover(int round)
    {
        if (group < 0 || phase == Phase::recovery) return;
        phase = Phase::recovery;
        recovery_round = round;
        ++recoveries;
    }

    void resume(int round)
    {
        if (group < 0) return;
        phase = Phase::approach;
        last_progress_round = round;
        recovery_round = -1;
    }
};

struct Quote
{
    bool feasible{};
    int trips{}, rounds{};
    float incoming_damage{};
    float score{};
};

// Cost one complete episode, including the trips required to cover estimated remaining
// damage and the walk/recall/heal/return overhead of each extra trip. The fight forecast is
// an estimate, not a survival certificate: actual exposure and escape must be checked at
// every action boundary. No ally health or hypothetical joint damage enters this quote.
inline Quote quote(float reward, int travel, int return_walk, int exit_steps, int recall_ticks,
                   int capture_ticks, int bank_walk, int bank_ticks, int rounds_left,
                   float fight_rounds, float fight_damage, float damage_mult,
                   float hp, float max_hp, float reserve_damage)
{
    Quote out;
    const auto finite_nonnegative = [](float x) { return std::isfinite(x) && x >= 0.0f; };
    if (!std::isfinite(reward) || reward <= 0.0f || !finite_nonnegative(fight_rounds) ||
        !finite_nonnegative(fight_damage) || !finite_nonnegative(damage_mult) ||
        !finite_nonnegative(hp) || !std::isfinite(max_hp) || max_hp <= 0.0f ||
        !finite_nonnegative(reserve_damage) || travel < 0 || return_walk < 0 || exit_steps < 0 ||
        recall_ticks < 0 || capture_ticks < 0 || bank_walk < 0 || bank_ticks < 0 || rounds_left < 0)
        return out;

    const double need = static_cast<double>(fight_damage) * damage_mult;
    if (!std::isfinite(need) || need > std::numeric_limits<float>::max()) return out;
    out.incoming_damage = static_cast<float>(need);
    const double full_pool = static_cast<double>(max_hp) - reserve_damage;
    double trips = 1.0;
    if (need > 0.0)
    {
        if (full_pool <= 0.0) return out;
        const double first_pool = std::clamp(static_cast<double>(hp) - reserve_damage, 0.0, full_pool);
        if (need > first_pool) trips += std::ceil((need - first_pool) / full_pool);
    }
    const double overhead = static_cast<double>(exit_steps) + recall_ticks + 1.0 + return_walk;
    const double rounds = static_cast<double>(travel) + std::ceil(fight_rounds) + capture_ticks +
                          bank_walk + bank_ticks + (trips - 1.0) * overhead;
    if (!std::isfinite(rounds) || trips > std::numeric_limits<int>::max() ||
        rounds > std::numeric_limits<int>::max())
        return out;
    out.trips = static_cast<int>(trips);
    out.rounds = std::max(1, static_cast<int>(rounds));
    out.feasible = out.rounds <= rounds_left;
    if (out.feasible) out.score = reward / static_cast<float>(out.rounds);
    return out;
}

} // namespace mb::production
