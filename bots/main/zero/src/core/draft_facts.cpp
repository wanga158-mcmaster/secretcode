#include "draft_facts.h"
#include <algorithm>
#include "catalog.h"

namespace zero {

DraftMapFacts draft_map_facts(const Map& map)
{
    DraftMapFacts facts;
    facts.area = static_cast<std::uint32_t>(map.w * map.h);
    facts.group_count = static_cast<std::uint32_t>(map.groups.size());
    facts.altar_count = static_cast<std::uint32_t>(map.altars.size());
    for (const auto& group : map.groups)
    {
        facts.gems += static_cast<float>(group.gems);
        facts.gold += static_cast<float>(group.gold);
        for (const auto& member : group.members)
        {
            facts.members += 1.0f;
            facts.strength += catalog().camp(member.camp_type).strength;
        }
    }
    return facts;
}

void draft_facts(const DraftMapFacts& map, int team_size, std::span<const int> ours, std::span<const int> theirs,
                 float* state, CandSet& candidates)
{
    std::fill(state, state + kState, 0.0f);
    state[0] = static_cast<float>(map.area) / 10000.0f;
    state[1] = static_cast<float>(map.group_count) / 30.0f;
    state[2] = map.gems / 1000.0f;
    state[3] = map.gold / 5000.0f;
    state[4] = map.members > 0.0f ? map.strength / map.members / 100.0f : 0.0f;
    state[5] = static_cast<float>(map.altar_count) / 4.0f;
    state[6] = static_cast<float>(team_size) / 5.0f;
    for (int pick : ours) if (pick >= 0 && pick < 13) state[8 + pick] = 1.0f;
    for (int pick : theirs) if (pick >= 0 && pick < 13) state[21 + pick] = 1.0f;

    candidates.head = h_draft;
    candidates.n = 0;
    for (int pick = 0; pick < 12 && candidates.n < kMaxCands; ++pick)
    {
        if (std::find(ours.begin(), ours.end(), pick) != ours.end()) continue;
        Cand& candidate = candidates.c[candidates.n++];
        candidate = Cand{};
        candidate.kind = 1;
        candidate.target = pick;
        // Cassi & Issac is pick 3 (physical types 3 and 4); the ranged caster describes that joint pick.
        const auto& design = catalog().champion(pick <= 3 ? pick : pick + 1);
        candidate.phi[0] = design.health / 2000.0f;
        candidate.phi[1] = design.strength / 100.0f;
        candidate.phi[2] = design.focus / 200.0f;
        candidate.phi[3] = design.attack_range / 3.0f;
        candidate.phi[4] = design.attack_type == 1 ? 1.0f : 0.0f;
        candidate.phi[5] = design.health_growth / 200.0f;
        candidate.phi[6] = design.strength_growth / 10.0f;
        candidate.phi[7] = design.focus_growth / 20.0f;
        for (int slot = 0; slot < 2; ++slot)
        {
            const auto& ability = design.ability[slot];
            if (ability.kind >= 1 && ability.kind <= 7) candidate.phi[7 + ability.kind] = 1.0f;
            candidate.phi[15 + slot] = ability.modifier;
            candidate.phi[17 + slot] = ability.range / 4.0f;
        }
        candidate.phi[19 + std::min<int>(4, catalog().passives[design.passive].type / 3)] = 1.0f;
    }
}

void draft_facts(const Map& map, int team_size, std::span<const int> ours, std::span<const int> theirs,
                 float* state, CandSet& candidates)
{
    draft_facts(draft_map_facts(map), team_size, ours, theirs, state, candidates);
}

}  // namespace zero
