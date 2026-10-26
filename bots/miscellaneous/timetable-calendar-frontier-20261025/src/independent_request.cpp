#ifndef TT_REQUEST_PURE_ONLY
#include "runtime.h"
#endif
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace tt {
namespace request_detail {

enum class GoalKind { Work, Capture, Bank };
enum class GeometryStatus { NeedCompute, Complete, Invalid };
constexpr std::size_t advance_limit=2048;
constexpr std::size_t guard_stride=64;

bool packed_test(const std::vector<std::uint8_t>& bits,std::size_t tile)
{ return (bits[tile/8] & (std::uint8_t{1}<<(tile%8)))!=0; }

void packed_set(std::vector<std::uint8_t>& bits,std::size_t tile)
{ bits[tile/8]|=static_cast<std::uint8_t>(std::uint8_t{1}<<(tile%8)); }

// This operator consumes immutable positions and a historical observation seed.
// It proves only the requested frozen goal/opening geometry. It does not inspect
// current occupancy, allowances, safety, target life or capture availability.
template<class Assembly,class World,class Guard>
GeometryStatus advance_geometry(Assembly& assembly,const World& world,GoalKind kind,Guard guard)
{
    if (assembly.phase>=3) return GeometryStatus::Complete;
    if (!guard()) return GeometryStatus::NeedCompute;
    if (world.width<=0 || world.height<=0 || assembly.reach<0 ||
        static_cast<std::size_t>(world.width)>
            std::numeric_limits<std::size_t>::max()/static_cast<std::size_t>(world.height))
        return GeometryStatus::Invalid;
    const auto width=static_cast<std::size_t>(world.width);
    const auto height=static_cast<std::size_t>(world.height);
    const auto cells=width*height;
    const bool forced=[&] {
        if constexpr(requires { assembly.forced_work_root;assembly.forced_perch; })return assembly.forced_work_root;
        else return false;
    }();
    if(forced && kind!=GoalKind::Work)return GeometryStatus::Invalid;
    const auto bytes=cells/8+(cells%8!=0);
    if (assembly.visible_bits.size()!=bytes || assembly.living_bits.size()!=bytes)
        return GeometryStatus::Invalid;
    if (kind!=GoalKind::Bank && (assembly.group<0 ||
        static_cast<std::size_t>(assembly.group)>=world.groups.size()))
        return GeometryStatus::Invalid;
    const auto inside=[&](const auto point) {
        return point.x>=0 && point.y>=0 && point.x<world.width && point.y<world.height;
    };
    const auto index=[&](const auto point) {
        return static_cast<std::size_t>(point.y)*width+static_cast<std::size_t>(point.x);
    };
    if (assembly.phase==0)
    {
        assembly.goal_bits.assign(bytes,0); assembly.empty_bits.assign(bytes,0);
        assembly.targets.clear();
        assembly.empty_group=assembly.empty_member=assembly.root_target=assembly.root_cell=0;
        assembly.phase=1;
    }
    std::size_t processed=0;
    const auto can_step=[&] {
        return processed<advance_limit && (processed%guard_stride!=0 || guard());
    };
    if (assembly.phase==1)
    {
        while (assembly.empty_group<world.groups.size())
        {
            const auto& members=world.groups[assembly.empty_group].members;
            while (assembly.empty_member<members.size())
            {
                if (!can_step()) return GeometryStatus::NeedCompute;
                const auto point=members[assembly.empty_member].pos;
                if (!inside(point)) return GeometryStatus::Invalid;
                const auto tile=index(point);
                const bool seen=packed_test(assembly.visible_bits,tile);
                const bool living=packed_test(assembly.living_bits,tile);
                if (seen && !living) packed_set(assembly.empty_bits,tile);
                if (!forced && kind!=GoalKind::Bank && assembly.empty_group==static_cast<std::size_t>(assembly.group) &&
                    (kind==GoalKind::Capture || !seen || living))
                    assembly.targets.push_back(point);
                ++assembly.empty_member; ++processed;
            }
            ++assembly.empty_group; assembly.empty_member=0;
        }
        // Bank targets are immutable altar positions. The caller selected the
        // frozen radius after resolving any current altar lock separately.
        if (kind==GoalKind::Bank)
        {
            while (assembly.root_target<world.altars.size())
            {
                if (!can_step()) return GeometryStatus::NeedCompute;
                const auto point=world.altars[assembly.root_target];
                if (!inside(point)) return GeometryStatus::Invalid;
                assembly.targets.push_back(point);
                ++assembly.root_target; ++processed;
            }
        }
        if(forced) {
            if constexpr(requires { assembly.forced_perch; }) {
                if(!inside(assembly.forced_perch) || assembly.reach!=0)return GeometryStatus::Invalid;
                assembly.targets.push_back(assembly.forced_perch);
            }
        }
        assembly.root_target=assembly.root_cell=0;
        assembly.phase=2;
    }
    const auto radius=kind==GoalKind::Capture?0:static_cast<std::int64_t>(assembly.reach);
    while (assembly.root_target<assembly.targets.size())
    {
        const auto point=assembly.targets[assembly.root_target];
        const auto left=std::max<std::int64_t>(0,static_cast<std::int64_t>(point.x)-radius);
        const auto right=std::min<std::int64_t>(world.width-1,static_cast<std::int64_t>(point.x)+radius);
        const auto top=std::max<std::int64_t>(0,static_cast<std::int64_t>(point.y)-radius);
        const auto bottom=std::min<std::int64_t>(world.height-1,static_cast<std::int64_t>(point.y)+radius);
        const auto columns=static_cast<std::size_t>(right-left+1);
        const auto rectangle_cells=columns*static_cast<std::size_t>(bottom-top+1);
        while (assembly.root_cell<rectangle_cells)
        {
            if (!can_step()) return GeometryStatus::NeedCompute;
            const auto x=static_cast<std::size_t>(left)+assembly.root_cell%columns;
            const auto y=static_cast<std::size_t>(top)+assembly.root_cell/columns;
            packed_set(assembly.goal_bits,y*width+x);
            ++assembly.root_cell; ++processed;
        }
        ++assembly.root_target; assembly.root_cell=0;
    }
    assembly.phase=3;
    return GeometryStatus::Complete;
}

} // namespace request_detail

#ifndef TT_REQUEST_PURE_ONLY
Status advance_request(RequestAssembly& assembly)
{
    if (assembly.status==Status::Ready) return assembly.status;
    request_detail::GoalKind kind;
    switch (assembly.kind)
    {
        case RouteKind::Work: kind=request_detail::GoalKind::Work; break;
        case RouteKind::Capture: kind=request_detail::GoalKind::Capture; break;
        case RouteKind::Bank: kind=request_detail::GoalKind::Bank; break;
        default: return assembly.status=Status::NeedObservation;
    }
    if (!budget(12000)) return assembly.status=Status::NeedCompute;
    auto& world=World::get();
    const auto geometry=request_detail::advance_geometry(assembly,world,kind,[]{return budget(12000);});
    if (geometry==request_detail::GeometryStatus::Invalid) return assembly.status=Status::NeedObservation;
    if (geometry!=request_detail::GeometryStatus::Complete) return assembly.status=Status::NeedCompute;
    assembly.request.kind=assembly.kind; assembly.request.group=assembly.group;
    // Each import has its own phase and fresh empirical headroom. If the second
    // one must yield, the first remains complete and will not be repeated.
    if (assembly.phase==3)
    {
        if (!budget(26000)) return assembly.status=Status::NeedCompute;
        auto goal=std::make_shared<cambc::bitboard>();
        goal->import_linear_buffer(assembly.goal_bits);
        assembly.request.goal_mask=std::move(goal);
        assembly.phase=4;
    }
    if (assembly.phase==4)
    {
        if (!budget(26000)) return assembly.status=Status::NeedCompute;
        auto empty=std::make_shared<cambc::bitboard>();
        empty->import_linear_buffer(assembly.empty_bits);
        assembly.request.observed_empty=std::move(empty);
        assembly.phase=5;
    }
    return assembly.status=Status::Ready;
}
#endif
} // namespace tt
