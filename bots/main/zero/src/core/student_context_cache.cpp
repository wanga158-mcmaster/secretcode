#include "student_context_cache.h"
#include <limits>
#include <stdexcept>
#include <utility>

namespace zero::student {
namespace {

std::size_t bucket(Tile at, std::size_t count)
{
    return (static_cast<std::uint32_t>(at.x) * 0x9e3779b9u ^
            static_cast<std::uint32_t>(at.y) * 0x85ebca6bu) & (count - 1);
}

}  // namespace

bool CampContextCache::prepared_for(const Map& map) const noexcept
{
    return map_ == &map && groups_.size() == map.groups.size() && static_rows_.size() == map.groups.size();
}

void CampContextCache::prepare(const Map& map)
{
    CampContextCache next;
    next.static_rows_.resize(map.groups.size());
    next.groups_.resize(map.groups.size());
    std::size_t members = 0;
    for (std::size_t g = 0; g < map.groups.size(); ++g)
    {
        const auto& group = map.groups[g];
        auto& row = next.static_rows_[g];
        row.group = static_cast<int>(g);
        row.phi[3] = static_cast<float>(group.gold) / 300.0f;
        row.phi[4] = static_cast<float>(group.gems) / 100.0f;
        row.phi[5] = group.total_hp / 10000.0f;
        row.phi[6] = static_cast<float>(group.members.size()) / 64.0f;
        next.groups_[g] = { group.center, group.members.size() };
        if (group.members.size() > std::numeric_limits<std::size_t>::max() - members)
            throw std::length_error("camp context cache member count overflow");
        members += group.members.size();
    }
    if (members)
    {
        if (members > std::numeric_limits<std::size_t>::max() / 2)
            throw std::length_error("camp context cache member table overflow");
        std::size_t count = 1;
        while (count < members * 2)
        {
            if (count > std::numeric_limits<std::size_t>::max() / 2)
                throw std::length_error("camp context cache member table overflow");
            count *= 2;
        }
        next.members_.resize(count);
        for (std::size_t g = 0; g < map.groups.size(); ++g)
            for (const auto& member : map.groups[g].members)
            {
                auto slot = bucket(member.at, count);
                while (next.members_[slot].group >= 0 && next.members_[slot].at != member.at)
                    slot = (slot + 1) & (count - 1);
                next.members_[slot].at = member.at;
                next.members_[slot].group = static_cast<int>(g);
                next.members_[slot].type = member.camp_type;
            }
    }
    next.map_ = &map;
    std::swap(map_, next.map_);
    static_rows_.swap(next.static_rows_);
    groups_.swap(next.groups_);
    members_.swap(next.members_);
    std::swap(generation_, next.generation_);
}

void camp_context_cached(const Context& ctx, CampContextCache& cache, std::vector<CampContext>& out)
{
    const auto& map = *ctx.map;
    const auto& o = *ctx.obs;
    if (!cache.prepared_for(map)) throw std::logic_error("camp context cache is not prepared for this immutable Map");
    out = cache.static_rows_;  // includes zero geometry/dynamic slots on every call
    const bool origin_known = map.inside(o.self.at);
    for (std::size_t g = 0; g < cache.groups_.size(); ++g)
    {
        const auto& group = cache.groups_[g];
        auto& row = out[g];
        if (origin_known)
        {
            row.phi[0] = static_cast<float>(group.center.x - o.self.at.x) / 20.0f;
            row.phi[1] = static_cast<float>(group.center.y - o.self.at.y) / 20.0f;
            row.phi[2] = static_cast<float>(cheb(group.center, o.self.at)) / 40.0f;
        }
        const int status = g < o.group_status.size() ? o.group_status[g] : gs_unknown;
        row.phi[7] = static_cast<float>(status) / 3.0f;
        row.phi[8] = status != gs_unknown ? 1.0f : 0.0f;
    }
    if (!cache.members_.empty() && !o.camps.empty())
    {
        if (cache.generation_ == std::numeric_limits<std::uint64_t>::max())
        {
            for (auto& member : cache.members_) member.generation = 0;
            cache.generation_ = 0;
        }
        const auto generation = ++cache.generation_;
        const auto count = cache.members_.size();
        const auto find = [&](const CampView& camp) -> CampContextCache::MemberView* {
            auto slot = bucket(camp.at, count);
            while (cache.members_[slot].group >= 0 && cache.members_[slot].at != camp.at)
                slot = (slot + 1) & (count - 1);
            return cache.members_[slot].group >= 0 && cache.members_[slot].type == camp.type ?
                       &cache.members_[slot] : nullptr;
        };
        for (std::size_t c = 0; c < o.camps.size(); ++c)
            if (auto* member = find(o.camps[c]))
            {
                member->last = c;
                member->generation = generation;
            }
        for (std::size_t c = 0; c < o.camps.size(); ++c)
        {
            const auto& camp = o.camps[c];
            if (camp.hp <= 0.0f) continue;
            const auto* member = find(camp);
            if (!member || member->generation != generation || member->last != c) continue;
            auto& row = out[static_cast<std::size_t>(member->group)];
            row.phi[9] += camp.hp;
            row.phi[10] += 1.0f;
        }
    }
    for (std::size_t g = 0; g < cache.groups_.size(); ++g)
    {
        auto& row = out[g];
        const auto member_count = cache.groups_[g].members;
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
