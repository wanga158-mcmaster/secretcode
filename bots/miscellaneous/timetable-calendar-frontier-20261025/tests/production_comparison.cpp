#include "production_comparison.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <type_traits>

namespace { std::size_t allocations{}; }
void* operator new(std::size_t size)
{
    ++allocations;
    if(auto* memory=std::malloc(size?size:1))return memory;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* memory) noexcept{std::free(memory);}
void operator delete[](void* memory) noexcept{std::free(memory);}
void operator delete(void* memory,std::size_t) noexcept{std::free(memory);}
void operator delete[](void* memory,std::size_t) noexcept{std::free(memory);}

namespace {
using namespace tt::production_comparison;
int checks{},failures{};
void check(bool condition,const char* label)
{++checks;if(!condition){++failures;std::printf("FAIL %s\n",label);}}
constexpr Key key{0x100000001,0x200000002};
static_assert(std::is_trivially_copyable_v<Comparison>);
static_assert(sizeof(Comparison)<=64);
}

int main()
{
    struct Point { int x{},y{}; bool operator==(const Point&) const=default; };
    struct Packet { Point pos; float hp{},max_hp{},raw_strength{},strength{};
        int attack_range{}; std::uint32_t gems{},gold{}; };
    // Heart's same raw attack has a larger projected outgoing value when hurt.
    Packet heart{{4,7},500,1000,40,60,1,0,0};
    check(same_raw_body(heart,Point{4,7},500,1000,40,1,0,0),
          "damaged Heart retains its query when physical raw provenance is unchanged");
    check(!same_raw_body(heart,Point{4,7},500,1000,60,1,0,0),
          "projected outgoing damage cannot masquerade as raw stat provenance");
    check(!same_raw_body(heart,Point{4,7},499,1000,40,1,0,0) &&
          !same_raw_body(heart,Point{4,7},500,1000,41,1,0,0) &&
          !same_raw_body(heart,Point{5,7},500,1000,40,1,0,0),
          "real HP, raw stat or pose changes revoke retained body identity");
    void* instrumentation=::operator new(1);
    ::operator delete(instrumentation);
    const auto allocation_start=allocations;
    check(allocation_start>0,"allocation tracker observes explicit C++ allocation");
    std::array<std::uint8_t,131> lhs{},rhs{};
    for(std::size_t i=0;i<lhs.size();++i)lhs[i]=rhs[i]=static_cast<std::uint8_t>(i*17u);
    Comparison singles{lhs.size(),rhs.size(),key};
    check(singles.status()==Status::NeedCompute && singles.compared()==0,"nonempty equal-sized vectors begin without an equality claim");
    check(singles.advance(lhs,rhs,key,0)==Status::NeedCompute && singles.compared()==0 && singles.last_work()==0,
          "zero byte units do not inspect or publish a partial prefix");
    for(std::size_t i=0;i<lhs.size();++i)
    {
        const auto expected=i+1==lhs.size()?Status::Equal:Status::NeedCompute;
        check(singles.advance(lhs,rhs,key,1)==expected && singles.compared()==i+1 && singles.last_work()==1,
              "one-byte slices retain exact cursor and publish Equal only after final byte");
    }
    check(singles.advance(lhs,rhs,key,64)==Status::Equal && singles.last_work()==0 && singles.compared()==131,
          "completed equality does not rescan a prefix");
    Comparison slices{lhs.size(),rhs.size(),key};
    check(slices.advance(lhs,rhs,key,64)==Status::NeedCompute && slices.compared()==64 && slices.last_work()==64,
          "first sixty-four-byte input retains partial comparison");
    check(slices.advance(lhs,rhs,key,64)==Status::NeedCompute && slices.compared()==128 && slices.last_work()==64,
          "second input resumes after sixty-four instead of resetting on an owner round");
    check(slices.advance(lhs,rhs,key,64)==Status::Equal && slices.compared()==131 && slices.last_work()==3,
          "final slice charges only remaining bytes");
    rhs[129]^=1;
    Comparison late{lhs.size(),rhs.size(),key};
    check(late.advance(lhs,rhs,key,64)==Status::NeedCompute && late.advance(lhs,rhs,key,64)==Status::NeedCompute,
          "late mismatch cannot be claimed from a matching partial prefix");
    check(late.advance(lhs,rhs,key,64)==Status::Different && late.compared()==130 && late.last_work()==2,
          "late differing pair is conclusive and includes only its own slice work");
    check(late.advance(lhs,rhs,key,1)==Status::Different && late.last_work()==0,"completed difference does not inspect later bytes");
    rhs[129]^=1;rhs[64]^=1;
    Comparison resumed{lhs.size(),rhs.size(),key};
    check(resumed.advance(lhs,rhs,key,64)==Status::NeedCompute &&
          resumed.advance(lhs,rhs,key,1)==Status::Different && resumed.compared()==65 && resumed.last_work()==1,
          "next one-byte call reads index sixty-four rather than rescanning index zero");
    rhs[64]^=1;
    for(int changed=0;changed<4;++changed)
    {
        Comparison partial{lhs.size(),rhs.size(),key};partial.advance(lhs,rhs,key,1);
        auto actual=key;
        if(changed==0)++actual.query;
        if(changed==1)++actual.field;
        const auto left=std::span<const std::uint8_t>{lhs}.first(lhs.size()-(changed==2));
        const auto right=std::span<const std::uint8_t>{rhs}.first(rhs.size()-(changed==3));
        check(partial.advance(left,right,actual,64)==Status::Unknown && partial.last_work()==0 && partial.compared()==1,
              "query field or either size change revokes a partial comparison before reading");
        check(partial.advance(lhs,rhs,key,64)==Status::Unknown && partial.last_work()==0,
              "restoring old inputs cannot revive Unknown comparison");
    }
    Comparison completed{lhs.size(),rhs.size(),key};completed.advance(lhs,rhs,key,lhs.size());
    check(completed.advance(lhs,rhs,Key{key.query+1,key.field},0)==Status::Unknown,
          "key is validated even after Equal and with zero requested work");
    Comparison completed_size{lhs.size(),rhs.size(),key};completed_size.advance(lhs,rhs,key,lhs.size());
    check(completed_size.advance(std::span<const std::uint8_t>{lhs}.first(130),rhs,key,0)==Status::Unknown,
          "size is validated even after Equal");
    Comparison lengths{lhs.size(),rhs.size()-1,key};
    check(lengths.status()==Status::Different && lengths.compared()==0 &&
          lengths.advance(lhs,std::span<const std::uint8_t>{rhs}.first(130),key,64)==Status::Different && lengths.last_work()==0,
          "registered unequal vector lengths are conclusively Different without byte reads");
    check(lengths.advance(lhs,rhs,key,0)==Status::Unknown,"actual size mutation revokes registered length Difference");
    Comparison differing_key{1,2,key};
    check(differing_key.advance(std::span<const std::uint8_t>{lhs}.first(1),std::span<const std::uint8_t>{rhs}.first(2),
          Key{key.query,key.field+1},0)==Status::Unknown,"key changes revoke a completed Difference too");
    Comparison empty{0,0,key};
    check(empty.status()==Status::Equal && empty.advance({}, {},key,64)==Status::Equal && empty.last_work()==0,
          "two empty vectors are exactly Equal without allocation or iteration");
    check(empty.advance({}, {},Key{key.query+1,key.field},0)==Status::Unknown,"empty Equal still validates its generation key");
    Comparison huge_units{lhs.size(),rhs.size(),key};
    check(huge_units.advance(lhs,rhs,key,std::numeric_limits<std::size_t>::max())==Status::Equal &&
          huge_units.last_work()==lhs.size(),"large requested units stop at finite actual vector size");
    check(allocations==allocation_start,"constructors and all comparison advances perform zero heap allocations");
    std::printf("production comparison: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
