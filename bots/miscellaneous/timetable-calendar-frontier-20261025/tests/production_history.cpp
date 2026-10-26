#include "production_history.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <type_traits>

namespace {
std::size_t heap_allocations{};
int failures{},checks{};
void check(bool condition,const char* label)
{
    ++checks;
    if (!condition) { ++failures; std::printf("FAIL %s\n",label); }
}
}

void* operator new(std::size_t size)
{
    ++heap_allocations;
    if (void* pointer=std::malloc(size)) return pointer;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer,std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer,std::size_t) noexcept { std::free(pointer); }

int main()
{
    using namespace tt::production_history;
    static_assert(std::is_trivially_copyable_v<Snapshot>);
    const auto allocations_before=heap_allocations;
    const std::array<Point,3> history={Point{1,2},Point{7,8},Point{9,10}};
    Snapshot capture;
    check(capture.status()==Status::Unknown && !capture.matches(history) &&
          !capture.matches(std::span<const Point>{}),"default snapshot makes no semantic claim");
    check(capture.begin(history.size(),7)==Status::NeedCompute && capture.size()==3 && capture.copied()==0,
          "begin retains only bounded size and generation without copying points");
    check(capture.advance(history,7,0)==Status::NeedCompute && capture.last_work()==0 && !capture.matches(history),
          "zero units cannot publish a nonempty history");
    check(capture.advance(history,7,1)==Status::NeedCompute && capture.copied()==1 && capture.last_work()==1 &&
          !capture.matches(history) && !capture.matches(std::span<const Point>{history}.first(1)),
          "one copied point never claims either the partial prefix or complete sequence");
    check(capture.advance(history,7,1)==Status::NeedCompute && capture.copied()==2 && !capture.matches(history),
          "second partial capture remains unavailable");
    check(capture.advance(history,7,1)==Status::Ready && capture.copied()==3 && capture.matches(history),
          "only the final copied point publishes exact history");
    check(capture.advance(history,7,100)==Status::Ready && capture.last_work()==0,
          "complete same-generation capture performs no repeated copy work");
    const std::array<Point,3> different={Point{1,2},Point{7,9},Point{9,10}};
    const std::array<Point,3> reordered={Point{7,8},Point{1,2},Point{9,10}};
    check(!capture.matches(different) && !capture.matches(reordered) &&
          !capture.matches(std::span<const Point>{history}.first(2)),
          "equality compares exact coordinates, order and size without a hash");
    Snapshot copied=capture;
    check(copied.matches(history) && copied.generation()==7,
          "fixed-value snapshot copy preserves exact semantic content without heap storage");

    std::array<Point,capacity> full{};
    for (std::size_t i=0;i<full.size();++i) full[i]={static_cast<int>(i),-static_cast<int>(i)};
    check(capture.begin(full.size(),12)==Status::NeedCompute && !capture.matches(history),
          "new capture immediately withdraws prior Ready content");
    for (int input=0;input<8;++input)
    {
        const auto status=capture.advance(full,12,16);
        check(capture.last_work()==16 && capture.copied()==static_cast<std::size_t>((input+1)*16),
              "publication input copies exactly the supplied sixteen-point work slice");
        check((input==7 ? status==Status::Ready && capture.matches(full) :
                         status==Status::NeedCompute && !capture.matches(full)),
              "full128 capture needs eight bounded slices before Ready");
    }
    check(capture.begin(capacity+1,12)==Status::Unknown && !capture.matches(full) && capture.size()==0,
          "capacity overflow becomes Unknown rather than truncated equality");
    check(capture.begin(std::numeric_limits<std::size_t>::max(),12)==Status::Unknown &&
          capture.advance(full,12,100)==Status::Unknown,
          "extreme requested size never allocates or indexes storage");

    check(capture.begin(0,20)==Status::Ready && capture.matches(std::span<const Point>{}) &&
          capture.advance({},20,16)==Status::Ready && capture.last_work()==0,
          "exact empty history is a valid zero-work Ready semantic snapshot");
    check(!capture.matches(history) && capture.advance(history,20,1)==Status::Unknown,
          "empty equality does not match a nonempty current Trail");
    capture.begin(history.size(),30);
    capture.advance(history,30,1);
    check(capture.advance(history,31,2)==Status::Unknown && !capture.matches(history) && capture.last_work()==0,
          "generation change withdraws staged points before copying any changed input");
    capture.begin(history.size(),40); capture.advance(history,40,3);
    check(capture.advance(history,41,0)==Status::Unknown && !capture.matches(history),
          "generation change also invalidates a previously complete capture");
    capture.begin(history.size(),50); capture.advance(history,50,1);
    check(capture.advance(std::span<const Point>{history}.first(2),50,2)==Status::Unknown &&
          !capture.matches(history),"changed source size cannot publish a hybrid history");
    capture.begin(history.size(),60); capture.advance(history,60,3);
    check(capture.matches(history) && !capture.matches(different),
          "semantic comparison always checks current points even if a caller misuses unchanged revision");
    const std::array<Point,1> extreme={Point{std::numeric_limits<int>::min(),std::numeric_limits<int>::max()}};
    capture.begin(1,std::numeric_limits<std::uint64_t>::max());
    check(capture.advance(extreme,std::numeric_limits<std::uint64_t>::max(),1)==Status::Ready &&
          capture.matches(extreme) && capture.advance(extreme,0,1)==Status::Unknown,
          "snapshot copies extreme coordinates exactly and does not accept revision wrap as equality");
    check(heap_allocations==allocations_before,"all Snapshot capture, equality and fixed-copy operations allocate zero heap objects");

    std::printf("production_history checks=%d failures=%d\n",checks,failures);
    return failures?1:0;
}
