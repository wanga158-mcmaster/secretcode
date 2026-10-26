#define TT_REQUEST_PURE_ONLY
#include "../src/independent_request.cpp"
#include <cstdio>

namespace {
struct Point { int x{},y{}; };
struct Member { Point pos; };
struct Group { std::vector<Member> members; };
struct World
{
    int width{},height{};
    std::vector<Group> groups;
    std::vector<Point> altars;
};
struct Assembly
{
    int group{},reach{};
    std::vector<std::uint8_t> visible_bits,living_bits,goal_bits,empty_bits;
    std::vector<Point> targets;
    std::size_t phase{},empty_group{},empty_member{},root_target{},root_cell{};
};
Assembly seed(const World& world,int group,int reach,bool seen,bool living)
{
    Assembly result; result.group=group; result.reach=reach;
    const auto cells=static_cast<std::size_t>(world.width*world.height);
    const auto bytes=cells/8+(cells%8!=0);
    result.visible_bits.assign(bytes,seen?0xff:0);
    result.living_bits.assign(bytes,living?0xff:0);
    return result;
}
bool bit(const std::vector<std::uint8_t>& bits,int width,int x,int y)
{ return tt::request_detail::packed_test(bits,static_cast<std::size_t>(y*width+x)); }
int separation(Point a,Point b)
{ return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y)); }
} // namespace

int main()
{
    using tt::request_detail::GeometryStatus;
    using tt::request_detail::GoalKind;
    using tt::request_detail::advance_geometry;
    int failures=0;
    const auto check=[&](bool okay,const char* label) {
        if (!okay) { std::printf("FAIL %s\n",label); ++failures; }
    };
    bool exhaustive=true;
    for (int width=1;width<=9;++width)
        for (int height=1;height<=7;++height)
            for (int target_y=0;target_y<height;++target_y)
                for (int target_x=0;target_x<width;++target_x)
                    for (int reach=0;reach<=3;++reach)
                    {
                        const Point target{target_x,target_y};
                        const World world{width,height,{{{{target}}}}, {target}};
                        for (int presence=0;presence<3;++presence)
                            for (const auto kind:{GoalKind::Work,GoalKind::Capture,GoalKind::Bank})
                            {
                                const bool seen=presence!=0;
                                const bool living=presence==1;
                                auto assembly=seed(world,0,reach,seen,living);
                                exhaustive&=advance_geometry(assembly,world,kind,[]{return true;})==GeometryStatus::Complete;
                                for (int y=0;y<height;++y)
                                    for (int x=0;x<width;++x)
                                    {
                                        const bool admitted=kind!=GoalKind::Work || !seen || living;
                                        const int radius=kind==GoalKind::Capture?0:reach;
                                        const bool expected=admitted && separation({x,y},target)<=radius;
                                        exhaustive&=bit(assembly.goal_bits,width,x,y)==expected;
                                        exhaustive&=bit(assembly.empty_bits,width,x,y)==
                                            (seen && !living && x==target_x && y==target_y);
                                    }
                                const auto cells=static_cast<std::size_t>(width*height);
                                if (cells%8)
                                {
                                    const auto padding=static_cast<std::uint8_t>(0xffu<<(cells%8));
                                    exhaustive&=(assembly.goal_bits.back()&padding)==0 &&
                                                (assembly.empty_bits.back()&padding)==0;
                                }
                            }
                    }
    check(exhaustive,"all small-grid target/radius/presence cases preserve Work/Capture/Bank geometry and zero padding");

    // Multiple groups distinguish observed emptiness from current work roots.
    const World mixed{9,3,{{{{{0,0}},{{4,1}}}},{{{{8,2}}}}},{{0,2},{8,0}}};
    auto combination=seed(mixed,0,1,false,false);
    tt::request_detail::packed_set(combination.visible_bits,0);
    tt::request_detail::packed_set(combination.visible_bits,26);
    tt::request_detail::packed_set(combination.living_bits,26);
    auto result=advance_geometry(combination,mixed,GoalKind::Work,[]{return true;});
    bool mixed_valid=result==GeometryStatus::Complete && combination.targets.size()==1;
    for (int y=0;y<mixed.height;++y)
        for (int x=0;x<mixed.width;++x)
        {
            mixed_valid&=bit(combination.goal_bits,mixed.width,x,y)==(separation({x,y},{4,1})<=1);
            mixed_valid&=bit(combination.empty_bits,mixed.width,x,y)==(x==0 && y==0);
        }
    check(mixed_valid,"Work omits personally absent selected member, retains hidden member, and records all-group observed emptiness");

    const World large{71,67,{{{{{35,33}}}}},{{0,0}}};
    auto bounded=seed(large,0,100,false,false);
    result=advance_geometry(bounded,large,GoalKind::Work,[]{return true;});
    check(result==GeometryStatus::NeedCompute && bounded.phase==2 &&
          bounded.root_cell==tt::request_detail::advance_limit-1,
          "first advance caps total member/cell work at 2048 and retains its cursor");
    int advances=1;
    while (result==GeometryStatus::NeedCompute && advances<10)
    {
        const auto before=bounded.root_cell;
        result=advance_geometry(bounded,large,GoalKind::Work,[]{return true;});
        ++advances;
        if (result==GeometryStatus::NeedCompute) check(bounded.root_cell>before,"bounded resume advances rather than restarting roots");
    }
    bool all_covered=result==GeometryStatus::Complete && advances==3;
    for (int y=0;y<large.height;++y)
        for (int x=0;x<large.width;++x) all_covered&=bit(bounded.goal_bits,large.width,x,y);
    check(all_covered,"multi-advance clipped radius completes every legal tile exactly as one frozen request");

    auto fuel_limited=seed(large,0,100,false,false);
    int guards=0;
    result=advance_geometry(fuel_limited,large,GoalKind::Work,[&]{return ++guards<=2;});
    check(result==GeometryStatus::NeedCompute && fuel_limited.root_cell==63 && guards==3,
          "fuel guard yields at the 64-item boundary with valid partial progress");
    const auto before=fuel_limited.root_cell;
    result=advance_geometry(fuel_limited,large,GoalKind::Work,[]{return false;});
    check(result==GeometryStatus::NeedCompute && fuel_limited.root_cell==before,
          "entry headroom refusal preserves the partially assembled request");

    auto malformed=seed(mixed,0,1,false,false);
    malformed.visible_bits.pop_back();
    check(advance_geometry(malformed,mixed,GoalKind::Work,[]{return true;})==GeometryStatus::Invalid,
          "incomplete seed byte provenance is rejected before membership reads");
    malformed=seed(mixed,3,1,false,false);
    check(advance_geometry(malformed,mixed,GoalKind::Capture,[]{return true;})==GeometryStatus::Invalid,
          "invalid selected group is observation failure rather than physical unreachability");
    malformed=seed(mixed,0,-1,false,false);
    check(advance_geometry(malformed,mixed,GoalKind::Work,[]{return true;})==GeometryStatus::Invalid,
          "negative root radius is rejected");

    std::printf("independent_request: %s (%d failures)\n",failures?"FAIL":"PASS",failures);
    return failures?1:0;
}
