#include "../src/assignment_static_canonical.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <utility>
#include <vector>

namespace {
namespace sc=tt::assignment_static_canonical;
struct Point {int x{},y{};};struct Member {Point pos;};
struct Group {std::vector<Member> members;};struct World {int width{100},height{100};std::vector<Group> groups;};
int checks{},failures{};
void check(bool value,const char* message){++checks;if(!value){++failures;std::printf("FAIL %s\n",message);}}
World world(std::initializer_list<std::initializer_list<Point>> groups) {
    World out;for(const auto& members:groups){Group group;for(const auto p:members)group.members.push_back({p});out.groups.push_back(std::move(group));}return out;
}
// Independent direct reference mirrors canonical_groups: complete sorted
// (y,x) vectors, std::sort of local IDs, and final duplicate signature check.
sc::Result reference(const World& input,std::size_t bound=sc::max_members) {
    sc::Result out;out.local_by_canonical.fill(-1);out.canonical_by_local.fill(-1);
    if(input.groups.empty()||input.groups.size()>31||!bound||bound>32)return out;
    using Coordinate=std::array<int,2>;std::array<std::vector<Coordinate>,31> signatures;std::array<int,31> order{};
    for(std::size_t i=0;i<input.groups.size();++i) {
        const auto& members=input.groups[i].members;if(members.empty()||members.size()>bound)return out;
        for(const auto& member:members) {
            const auto p=member.pos;if(p.x<0||p.y<0||p.x>=input.width||p.y>=input.height)return out;
            signatures[i].push_back({p.y,p.x});
        }
        std::sort(signatures[i].begin(),signatures[i].end());
        if(std::adjacent_find(signatures[i].begin(),signatures[i].end())!=signatures[i].end())return out;
        order[i]=static_cast<int>(i);
    }
    std::sort(order.begin(),order.begin()+static_cast<std::ptrdiff_t>(input.groups.size()),
        [&](int a,int b){return signatures[static_cast<std::size_t>(a)]<signatures[static_cast<std::size_t>(b)];});
    for(std::size_t i=0;i<input.groups.size();++i) {
        if(i&&signatures[static_cast<std::size_t>(order[i-1])]==signatures[static_cast<std::size_t>(order[i])])return out;
        out.local_by_canonical[i]=order[i];out.canonical_by_local[static_cast<std::size_t>(order[i])]=static_cast<int>(i);
    }
    out.count=input.groups.size();out.supported=true;return out;
}
void differential(const World& input,std::size_t bound=sc::max_members) {
    const auto expected=reference(input,bound);
    for(const std::size_t slice:{1u,2u,7u,19u}) {
        sc::CanonicalBuilder<World> builder{input,bound};
        check(builder.work_used()==0&&builder.last_work()==0&&!builder.result(),"constructor performs no member/sort work and publishes no mapping");
        const auto before=builder.status();builder.advance(0);
        check(builder.status()==before&&builder.work_used()==0&&!builder.result(),"zero units cannot make progress or publish a result");
        for(unsigned calls=0;calls<100000&&builder.status()==sc::Status::NeedCompute;++calls) {
            const auto work=builder.work_used();builder.advance(slice);
            check(builder.last_work()<=slice&&builder.work_used()>work,"positive slices obey their bound and retain progress through comparisons/shifts");
            if(builder.status()==sc::Status::NeedCompute)check(!builder.result(),"partially sorted or mapped state never publishes supported identity");
        }
        check(builder.status()!=sc::Status::NeedCompute,"all finite declared signatures complete or reject within the explicit fixture bound");
        const auto* actual=builder.result();check((actual!=nullptr)==expected.supported,"staged rejection/success decision matches the independent old-function oracle");
        if(actual&&expected.supported)check(actual->supported&&actual->count==expected.count&&
            actual->local_by_canonical==expected.local_by_canonical&&actual->canonical_by_local==expected.canonical_by_local,
            "every successful canonical order and inverse mapping exactly match old sorted-vector semantics");
        const auto total=builder.work_used();builder.advance(100);
        check(builder.last_work()==0&&builder.work_used()==total,"terminal builders never restart work on a later call");
    }
}
void semantic_cases() {
    auto input=world({{{9,0}},{{0,1}},{{0,0},{9,0}},{{0,0},{1,0}},{{0,0}}});
    differential(input);for(auto& group:input.groups)std::reverse(group.members.begin(),group.members.end());differential(input);
    std::reverse(input.groups.begin(),input.groups.end());differential(input);
    const auto suffix=world({{{0,0},{9,0}},{{0,0},{1,0}},{{0,0}}});
    sc::CanonicalBuilder<World> explicit_order{suffix};
    for(unsigned i=0;i<10000&&explicit_order.status()==sc::Status::NeedCompute;++i)explicit_order.advance(1);
    const auto* exact=explicit_order.result();
    check(exact&&exact->local_by_canonical[0]==2&&exact->local_by_canonical[1]==1&&exact->local_by_canonical[2]==0,
        "shorter common prefix precedes both longer signatures, whose full suffix orders B before A despite identical minima");
    // Distinct overlapping groups are legal to this exact canonicalizer. The
    // separate static-world compiler owns physical occupied-tile rejection.
    differential(world({{{0,0},{1,0}},{{0,0},{2,0}},{{0,0}}}));
    differential(world({{{1,1},{1,1}}}));
    differential(world({{{1,1},{2,2}},{{2,2},{1,1}}}));
    differential(world({{{-1,0}}}));differential(world({{{100,0}}}));differential(world({{{0,100}}}));
    differential(world({{}}));differential(World{});
    auto shape=world({{{0,0}}});shape.width=0;differential(shape);shape.width=100;shape.height=-1;differential(shape);
    differential(world({{{0,0},{1,1}}}),1);differential(world({{{0,0}}}),0);differential(world({{{0,0}}}),33);
}
void capacity_and_partitions() {
    World maximum;
    for(int g=0;g<31;++g) {
        Group group;for(int m=31;m>=0;--m)group.members.push_back({{m,g}});
        maximum.groups.push_back(std::move(group));
    }
    std::reverse(maximum.groups.begin(),maximum.groups.end());differential(maximum);
    World deep_prefix;
    for(int g=30;g>=0;--g) {
        Group group;for(int m=0;m<31;++m)group.members.push_back({{m,0}});
        group.members.push_back({{g,1}});deep_prefix.groups.push_back(std::move(group));
    }
    differential(deep_prefix); // Forces retained group comparisons through31 equal coordinates.
    auto too_many=maximum;too_many.groups.push_back({{{{0,32}}}});differential(too_many);
    auto too_large=world({{{0,0}}});for(int i=1;i<33;++i)too_large.groups[0].members.push_back({{i,0}});differential(too_large);
    std::uint32_t seed=0xf8620u;
    for(unsigned sample=0;sample<50;++sample) {
        World input;const auto random=[&](){seed=1664525u*seed+1013904223u;return seed;};
        const auto groups=1+random()%12;
        for(std::uint32_t g=0;g<groups;++g) {
            Group group;const auto members=1+random()%12;
            for(std::uint32_t m=0;m<members;++m)group.members.push_back({{static_cast<int>(m),static_cast<int>(g)}});
            for(std::size_t i=group.members.size();i>1;--i)std::swap(group.members[i-1],group.members[random()%i]);
            input.groups.push_back(std::move(group));
        }
        for(std::size_t i=input.groups.size();i>1;--i)std::swap(input.groups[i-1],input.groups[random()%i]);
        differential(input);
    }
    auto mutable_source=world({{{0,0}}});sc::CanonicalBuilder<World> builder{mutable_source};
    builder.advance(1);mutable_source.width=101;
    check(builder.advance(1)==sc::Status::Unsupported&&builder.reason()==sc::Reason::SourceChanged&&!builder.result(),
        "an observed source shape change retires retained work; complete content immutability remains the caller premise");
}
}
int main() {
    semantic_cases();capacity_and_partitions();
    std::printf("assignment static canonical: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
