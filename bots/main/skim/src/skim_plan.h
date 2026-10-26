#pragma once
// Small, SDK-independent contracts for the optional ordinary-FARM spare basic.
#include <cstddef>
#include <cstdint>

namespace mb::skim {

// Engineering limits, not measured fuel claims. A cutoff abandons the proposal,
// retaining the original walk and its existing farm plus turn-tail fuel reserve.
inline constexpr std::uint64_t kProposalBudget = 8000;
inline constexpr std::size_t kMaxCamps = 64;
inline constexpr std::size_t kMaxAllies = 8;

struct Resources
{
    bool ordinary_farm{}, melee{}, alive{}, channeling{}, stunned{}, capturing{};
    bool moved{}, attacked{}, cast{}, can_move{}, can_attack{}, quiet{}, healthy{};
};

constexpr bool may_propose(const Resources& r)
{
    return r.ordinary_farm && r.melee && r.alive && !r.channeling && !r.stunned && !r.capturing &&
           !r.moved && !r.attacked && !r.cast && r.can_move && r.can_attack && r.quiet && r.healthy;
}

constexpr bool budget_open(std::uint64_t start, std::uint64_t now, std::uint64_t reserve)
{
    return now > reserve && now <= start && start - now < kProposalBudget;
}

constexpr bool eligible(int group, int own_group, bool visible, bool awake, float hp,
                        int distance, int range, bool assisted)
{
    return group >= 0 && group != own_group && visible && awake && hp > 0.0f &&
           distance <= range && assisted;
}

// Lowest live HP first, then stable physical identity. This is only a deterministic
// tie break between legal spare basics, not a new target/value model.
constexpr bool better(float hp, std::uint32_t id, float best_hp, std::uint32_t best_id)
{
    return hp < best_hp || (hp == best_hp && id < best_id);
}

} // namespace mb::skim
