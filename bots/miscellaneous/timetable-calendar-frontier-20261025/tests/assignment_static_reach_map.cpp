// Reuse the exact embedded map/conditional catalogue and REAL static compiler.
// No literal map copy, SDK calls, price, Life or complete-service authority.
#define main characterization_unused_main
#include "assignment_static_characterization.cpp"
#undef main
#include "../src/assignment_static_reach.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace reach_map {
namespace ag=tt::assignment_geometry;namespace sg=tt::assignment_static_geometry;
namespace sm=tt::assignment_static_menu;namespace sr=tt::assignment_static_reach;
int checks{},failures{};
void check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL reach-map %s\n",why);}}

sr::Input input(const sg::StaticGeometry& data,const sm::KitGeometry& family) {
    sr::Input out;out.width=data.grid().width;out.height=data.grid().height;out.basic_range=family.basic_range;
    // The independent enums have DIFFERENT ordinals. Map by named family.
    switch(family.family) {
    case ag::Family::Basic:out.family=sr::Family::Basic;break;
    case ag::Family::Targeted:out.family=sr::Family::Targeted;break;
    case ag::Family::Area:out.family=sr::Family::Area;break;
    }
    out.spell_count=family.count;
    for(std::size_t i=0;i<family.count&&i<out.spells.size();++i)
        out.spells[i]={family.spells[i].range,family.spells[i].radius};
    return out;
}
struct Counts {
    std::uint64_t any{},closed{};int first_any{-1},first_closed{-1};
};
Counts production_cells(const sg::StaticGeometry& data,const sm::KitGeometry& family,int group,std::size_t start) {
    Counts out;
    for(std::size_t cell=0;cell<data.size();++cell) {
        const auto point=data.point(static_cast<std::uint32_t>(cell));
        // Call the ACTUAL production predicate on EVERY map cell. Closedness
        // is counted independently; Empty must exclude even non-closed cells.
        const auto real=sm::detail::kit(family,data,group,point);
        const bool closed=data.closed(static_cast<std::uint32_t>(cell));
        const auto ordinal=static_cast<int>((cell+data.size()-start)%data.size());
        if(real) {
            ++out.any;if(out.first_any<0||ordinal<out.first_any)out.first_any=ordinal;
            if(closed){++out.closed;if(out.first_closed<0||ordinal<out.first_closed)out.first_closed=ordinal;}
        }
    }
    return out;
}
void gold_signature(const sg::StaticGeometry& data,int group) {
    const auto& members=data.catalogue()[static_cast<std::size_t>(group)].members;
    const int y=group==10?12:17;bool exact=members.size()==30;std::array<bool,30> seen{};
    for(const auto& member:members) {
        if(member.pos.x<5||member.pos.x>34||member.pos.y!=y){exact=false;continue;}
        const auto i=static_cast<std::size_t>(member.pos.x-5);if(seen[i])exact=false;seen[i]=true;
    }
    for(const bool present:seen)exact&=present;
    check(exact,"canonical10/17 retain the exact two YBR30-member gold-line coordinate signatures");
}
void family(const std::shared_ptr<const sg::StaticGeometry>& data,const LiteralKit& kit,
            const sm::KitGeometry& geometry,unsigned side) {
    const auto params=input(*data,geometry);const auto origin=side?literal_origin_b:literal_origin_a;
    const auto start=static_cast<std::size_t>(data->index(origin));
    const int expected_ready_group=side?16:11;
    std::uint64_t any{},closed{};std::size_t empty{},may{},unknown{},gold_empty{};Counts target;
    int first_any=-1,first_closed=-1;
    for(std::size_t group=0;group<data->catalogue().size();++group) {
        const auto& members=data->catalogue()[group].members;
        const auto certificate=sr::certify(params,members.size(),[&](std::size_t i){return members[i].pos;});
        const auto actual=production_cells(*data,geometry,static_cast<int>(group),start);
        empty+=certificate==sr::Verdict::Empty;may+=certificate==sr::Verdict::MayHavePerch;unknown+=certificate==sr::Verdict::Unknown;
        check(certificate!=sr::Verdict::Unknown,"all current map/normalized-family specifications are supported certificate inputs");
        check((certificate==sr::Verdict::Empty)==(actual.any==0),
            "certificate empty/nonempty agrees with actual production full-grid standing predicate");
        if(certificate==sr::Verdict::Empty)check(actual.any==0&&actual.closed==0,
            "Empty excludes every production perch before and after the independent closed-cell gate");
        check(actual.closed<=actual.any,"closed filtering cannot invent geometric perches");
        check((actual.any==0)==(actual.first_any<0)&&(actual.closed==0)==(actual.first_closed<0),
            "first cyclic-positive ordinals exist exactly when the corresponding real count is positive");
        any+=actual.any;closed+=actual.closed;
        if(actual.first_any>=0&&(first_any<0||actual.first_any<first_any))first_any=actual.first_any;
        if(actual.first_closed>=0&&(first_closed<0||actual.first_closed<first_closed))first_closed=actual.first_closed;
        if(group==10||group==17) {
            gold_signature(*data,static_cast<int>(group));
            check(certificate==sr::Verdict::Empty&&actual.any==0,"both long gold-line standing families are certified empty");
            gold_empty+=certificate==sr::Verdict::Empty;
        }
        if(static_cast<int>(group)==expected_ready_group) {
            target=actual;
            check(certificate==sr::Verdict::MayHavePerch&&actual.any>0,
                "current first-Ready group remains per-family geometrically eligible; certificate cannot remove it");
            if(geometry.family==ag::Family::Basic)
                check(actual.any==4&&actual.closed==0,
                    "Basic range1 joint perches are the four occupied2x2 source cells; May is not a closed-perch offer");
            else check(actual.closed>0,"the actual ranged first-Ready family retains a closed production perch");
        }
    }
    check(empty+may+unknown==data->catalogue().size()&&unknown==0&&gold_empty==2,
        "all28 groups and both30-member rows were compared without Unknown or omitted groups");
    ag::Point first_target{-1,-1};
    if(target.first_closed>=0)first_target=data->point(static_cast<std::uint32_t>((start+static_cast<std::size_t>(target.first_closed))%data->size()));
    // Bounded aggregate rows; every group/cell was checked above. First-cell
    // ordinals use the same cyclic row-major start, not a route-distance order.
    const char* name=geometry.family==ag::Family::Basic?"basic":geometry.family==ag::Family::Targeted?"targeted":"area";
    std::printf("REACH_MAP {\"s\":\"%c\",\"kit\":\"%s\",\"family\":\"%s\",\"groups\":%zu,\"cells\":%zu,\"empty\":%zu,\"may\":%zu,\"unknown\":%zu,\"any\":%llu,\"closed\":%llu,\"first\":[%d,%d],\"gold_empty\":%zu,\"target\":[%d,%llu,%llu,%d,%d,%d,%d]}\n",
        side?'B':'A',kit.name,name,data->catalogue().size(),data->size(),empty,may,unknown,
        static_cast<unsigned long long>(any),static_cast<unsigned long long>(closed),first_any,first_closed,gold_empty,
        expected_ready_group,static_cast<unsigned long long>(target.any),static_cast<unsigned long long>(target.closed),
        target.first_any,target.first_closed,first_target.x,first_target.y);
}
}
int main() {
    for(unsigned side=0;side<2;++side) {
        const auto built=::compile(side);
        reach_map::check(static_cast<bool>(built.data),"real current-policy map compiler seals before reach comparison");
        if(!built.data)continue;
        for(const auto& kit:literal_kits) {
            const auto snapshot=::geometry_kit(kit);const auto families=tt::assignment_static_menu::detail::geometry(snapshot);
            reach_map::check(families.reason==tt::assignment_geometry::Reason::None,"real positive-magnitude geometry-only kit normalizes");
            std::size_t count{};
            for(const auto& family:families.families)if(family){++count;reach_map::family(built.data,kit,*family,side);}
            reach_map::check(count==2,"each current Marcella/Ryth geometry kit contributes its two normalized families");
        }
    }
    const int total_checks=::checks+reach_map::checks,total_failures=::failures+reach_map::failures;
    std::printf("assignment static reach map (pure): %d checks, %d failures\n",total_checks,total_failures);
    return total_failures?1:0;
}
