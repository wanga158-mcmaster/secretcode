#include "student_context.h"
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace zero::student {
namespace {

struct MemberView
{
    Tile at{};
    int group{ -1 };
    std::uint8_t type{};
    std::size_t last{ std::numeric_limits<std::size_t>::max() };
};

std::size_t bucket(Tile at, std::size_t count)
{
    return (static_cast<std::uint32_t>(at.x) * 0x9e3779b9u ^
            static_cast<std::uint32_t>(at.y) * 0x85ebca6bu) & (count - 1);
}

}  // namespace

void camp_context(const Context& ctx, std::vector<CampContext>& out)
{
    const auto& map = *ctx.map;
    const auto& o = *ctx.obs;
    out.clear();
    out.resize(map.groups.size());
    const bool origin_known = map.inside(o.self.at);
    std::size_t members = 0;
    for (std::size_t g = 0; g < map.groups.size(); ++g)
    {
        const auto& group = map.groups[g];
        auto& row = out[g];
        row.group = static_cast<int>(g);
        if (origin_known)
        {
            row.phi[0] = static_cast<float>(group.center.x - o.self.at.x) / 20.0f;
            row.phi[1] = static_cast<float>(group.center.y - o.self.at.y) / 20.0f;
            row.phi[2] = static_cast<float>(cheb(group.center, o.self.at)) / 40.0f;
        }
        row.phi[3] = static_cast<float>(group.gold) / 300.0f;
        row.phi[4] = static_cast<float>(group.gems) / 100.0f;
        row.phi[5] = group.total_hp / 10000.0f;
        row.phi[6] = static_cast<float>(group.members.size()) / 64.0f;
        const int status = g < o.group_status.size() ? o.group_status[g] : gs_unknown;
        row.phi[7] = static_cast<float>(status) / 3.0f;
        row.phi[8] = status != gs_unknown ? 1.0f : 0.0f;
        if (group.members.size() > std::numeric_limits<std::size_t>::max() - members)
            throw std::length_error("camp context member count overflow");
        members += group.members.size();
    }
    if (members && !o.camps.empty())
    {
        if (members > std::numeric_limits<std::size_t>::max() / 2)
            throw std::length_error("camp context member table overflow");
        std::size_t count = 1;
        while (count < members * 2)
        {
            if (count > std::numeric_limits<std::size_t>::max() / 2)
                throw std::length_error("camp context member table overflow");
            count *= 2;
        }
        std::vector<MemberView> seen(count);
        for (std::size_t g = 0; g < map.groups.size(); ++g)
            for (const auto& member : map.groups[g].members)
            {
                auto slot = bucket(member.at, count);
                while (seen[slot].group >= 0 && seen[slot].at != member.at) slot = (slot + 1) & (count - 1);
                seen[slot].at = member.at;
                seen[slot].group = static_cast<int>(g);
                seen[slot].type = member.camp_type;
            }
        const auto find = [&](const CampView& camp) -> MemberView* {
            auto slot = bucket(camp.at, count);
            while (seen[slot].group >= 0 && seen[slot].at != camp.at) slot = (slot + 1) & (count - 1);
            return seen[slot].group >= 0 && seen[slot].type == camp.type ? &seen[slot] : nullptr;
        };
        for (std::size_t c = 0; c < o.camps.size(); ++c)
            if (auto* member = find(o.camps[c])) member->last = c;
        // Accumulate in the canonical current-observation order, once per matching spawn tile/type.
        for (std::size_t c = 0; c < o.camps.size(); ++c)
        {
            const auto& camp = o.camps[c];
            if (camp.hp <= 0.0f) continue;
            const auto* member = find(camp);
            if (!member || member->last != c) continue;
            auto& row = out[static_cast<std::size_t>(member->group)];
            row.phi[9] += camp.hp;
            row.phi[10] += 1.0f;
        }
    }
    for (std::size_t g = 0; g < map.groups.size(); ++g)
    {
        auto& row = out[g];
        const auto member_count = map.groups[g].members.size();
        const float observed = row.phi[10];
        const int status = g < o.group_status.size() ? o.group_status[g] : gs_unknown;
        const bool empty_status = status == gs_reward || status == gs_respawning;
        const bool conflict = empty_status && observed > 0.0f;
        row.phi[9] /= 10000.0f;
        row.phi[10] = member_count ? observed / static_cast<float>(member_count) : 0.0f;
        row.phi[11] = !conflict && (!member_count || observed == static_cast<float>(member_count) ||
                                  (empty_status && observed == 0.0f)) ? 1.0f : 0.0f;
    }
}

}  // namespace zero::student
