#pragma once
// A certificate for the RESTRICTED full-group, single-standing-perch geometry
// family. Empty is not group/game-service infeasibility: sequential targets,
// moving work words and other families remain outside this representation.
// No terrain, occupancy, body, Life, dates, SDK calls or command authority.
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>

namespace tt::assignment_static_reach {
enum class Family {Basic,Targeted,Area};
enum class Verdict {Empty,MayHavePerch,Unknown};
inline constexpr std::size_t max_members=32,max_spells=2;
struct Point {std::int64_t x{},y{};};
struct Spell {std::int64_t range{},radius{};};
struct Input {
    Family family{Family::Basic};std::int64_t width{},height{},basic_range{};
    std::array<Spell,max_spells> spells{};std::size_t spell_count{};
};
namespace detail {
struct Box {
    std::int64_t left{},top{},right{},bottom{};
    constexpr bool empty() const{return left>right||top>bottom;}
};
template<class Integer> constexpr bool coordinate(Integer value,std::int64_t& out) {
    if constexpr(!std::is_integral_v<Integer>||sizeof(Integer)>sizeof(std::int64_t))return false;
    else {
        if constexpr(std::is_unsigned_v<Integer>)
            if(static_cast<std::uint64_t>(value)>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))return false;
        out=static_cast<std::int64_t>(value);return true;
    }
}
// Callers established nonnegative in-grid x/y/r. Subtraction then stays >=-MAX;
// positive sums are checked BEFORE evaluation (including signed64 extremes).
constexpr bool around(std::int64_t x,std::int64_t y,std::int64_t radius,Box& out) {
    constexpr auto maximum=std::numeric_limits<std::int64_t>::max();
    if(x>maximum-radius||y>maximum-radius)return false;
    out={x-radius,y-radius,x+radius,y+radius};return true;
}
constexpr void intersect(Box& a,const Box& b) {
    if(b.left>a.left)a.left=b.left;if(b.top>a.top)a.top=b.top;
    if(b.right<a.right)a.right=b.right;if(b.bottom<a.bottom)a.bottom=b.bottom;
}
constexpr bool expand(Box& box,std::int64_t range,const Box& map) {
    constexpr auto maximum=std::numeric_limits<std::int64_t>::max();
    if(box.right>maximum-range||box.bottom>maximum-range)return false;
    box={box.left-range,box.top-range,box.right+range,box.bottom+range};
    intersect(box,map);return true;
}
}
// MemberAt returns an immutable object with integral x/y fields. It is read
// ONCE per member, at most32 times. All supplied spell specs and member facts
// are checked before any semantic Empty result, even if an earlier rectangle
// already became empty. Invalid/unsupported/overflow is Unknown => FALLBACK.
template<class MemberAt> constexpr Verdict certify(const Input& input,std::size_t member_count,MemberAt member_at) {
    if(input.width<=0||input.height<=0||input.basic_range<0||!member_count||member_count>max_members||input.spell_count>max_spells)
        return Verdict::Unknown;
    if(input.family!=Family::Basic&&input.family!=Family::Targeted&&input.family!=Family::Area)return Verdict::Unknown;
    if(input.family!=Family::Basic&&!input.spell_count)return Verdict::Unknown;
    for(std::size_t i=0;i<input.spell_count;++i)
        if(input.spells[i].range<0||input.spells[i].radius<0)return Verdict::Unknown;
    const detail::Box map{0,0,input.width-1,input.height-1};
    const auto alternatives=input.family==Family::Basic?std::size_t{1}:input.spell_count;
    std::array<detail::Box,max_spells> boxes{map,map};
    for(std::size_t member=0;member<member_count;++member) {
        const auto point=member_at(member);std::int64_t x{},y{};
        if(!detail::coordinate(point.x,x)||!detail::coordinate(point.y,y)||x<0||y<0||x>=input.width||y>=input.height)
            return Verdict::Unknown;
        for(std::size_t i=0;i<alternatives;++i) {
            const auto radius=input.family==Family::Basic?input.basic_range:
                input.family==Family::Targeted?input.spells[i].range:input.spells[i].radius;
            detail::Box member_box;
            if(!detail::around(x,y,radius,member_box))return Verdict::Unknown;
            detail::intersect(boxes[i],member_box);
        }
    }
    bool possible=false;
    for(std::size_t i=0;i<alternatives;++i)if(!boxes[i].empty()) {
        // An Area spell needs a legal in-grid AIM hitting all members, then a
        // legal in-grid perch within cast range of that aim rectangle. This is
        // only a geometric region: even all its cells may be closed/occupied.
        if(input.family==Family::Area&&!detail::expand(boxes[i],input.spells[i].range,map))return Verdict::Unknown;
        possible|=!boxes[i].empty();
    }
    return possible?Verdict::MayHavePerch:Verdict::Empty;
}
constexpr Verdict certify(const Input& input,std::span<const Point> members) {
    return certify(input,members.size(),[&](std::size_t i){return members[i];});
}
}
