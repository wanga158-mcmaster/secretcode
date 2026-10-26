#include "../src/capital_service_stage_meter.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <type_traits>
namespace m=wb::capital::service_stage_meter;
namespace allocation {bool watch{};unsigned calls{};}
void* operator new(std::size_t size){if(allocation::watch)++allocation::calls;if(void* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete[](void* p)noexcept{::operator delete(p);}
namespace {
unsigned checks{},failures{};
#define CHECK(...) do{++checks;if(!(__VA_ARGS__)){++failures;if(failures<25)std::printf("FAIL %u: %s\n",__LINE__,#__VA_ARGS__);}}while(false)
m::Sample sample(std::uint64_t fuel=1000){return {{0,0,true},{0,0,0,0,true},fuel,true,true};}
void zero_identity_and_snapshot(){
    static_assert(unsigned(m::Stage::Capture)==0);
    static_assert(std::is_trivially_destructible_v<m::Recorder>);
    static_assert(!std::is_copy_constructible_v<m::Recorder>&&!std::is_move_constructible_v<m::Recorder>);
    static_assert(sizeof(m::Recorder)<=8192);
    m::Recorder meter;CHECK(meter.depth()==0&&meter.last().stage==m::Stage::Unknown&&!meter.last().cost.known);
    CHECK(meter.stats(m::Stage::Unknown)==nullptr&&meter.stats(m::Stage::Count)==nullptr);
    auto before=sample();const auto token=meter.begin(m::Stage::Capture,before);
    CHECK(token.valid()&&meter.depth()==1&&meter.active().stage==m::Stage::Capture&&meter.active().active);
    before.remaining=0;before.epoch.owner=7;before.epoch.round=5;before.source.version=9;
    CHECK(meter.active().before.remaining==1000&&meter.active().before.epoch.owner==0&&meter.active().before.source.version==0);
    const auto out=meter.finish(token,sample(777));
    CHECK(out.closed&&!out.active&&out.stage==m::Stage::Capture&&out.cost.known&&out.cost.value==223&&out.issues==m::None);
    CHECK(out.before.epoch.owner==0&&out.before.epoch.round==0&&out.before.epoch.input==0&&out.before.epoch.effect==0);
    const auto* stats=meter.stats(m::Stage::Capture);CHECK(stats&&stats->starts==1&&stats->returned==1&&stats->unknown==0);
    CHECK(stats->peak_cost.known&&stats->peak_cost.value==223&&stats->peak_source.known&&stats->peak_source.id==0&&stats->peak_epoch.owner==0);
    auto zero=meter.begin(m::Stage::Capture,sample(0));const auto empty=meter.finish(zero,sample(0));
    CHECK(empty.cost.known&&empty.cost.value==0&&meter.stats(m::Stage::Capture)->peak_cost.value==223);
    CHECK(meter.last().stage==m::Stage::Capture&&meter.last().cost.known&&!meter.active().active);
}
void fence_failures(){
    struct Case {m::Sample before,after;std::uint32_t issue;};
    std::array<Case,13> cases{};
    for(auto& c:cases){c.before=sample();c.after=sample(900);}
    cases[0].after.epoch.round=1;cases[0].issue=m::CrossRound;
    cases[1].after.epoch.input=1;cases[1].issue=m::ChangedEpoch;
    cases[2].after.epoch.effect=1;cases[2].issue=m::ChangedEpoch;
    cases[3].after.epoch.owner=1;cases[3].issue=m::ChangedEpoch;
    cases[4].after.source.id=1;cases[4].issue=m::ChangedSource;
    cases[5].after.source.version=1;cases[5].issue=m::ChangedSource;
    cases[6].after.source.known=false;cases[6].issue=m::UnknownSource;
    cases[7].after.epoch.known=false;cases[7].issue=m::UnknownEpoch;
    cases[8].before.epoch.owner=-1;cases[8].issue=m::UnknownEpoch;
    cases[9].after.fresh=false;cases[9].issue=m::StaleSample;
    cases[10].before.fuel_known=false;cases[10].issue=m::UnknownFuel;
    cases[11].after.fuel_known=false;cases[11].issue=m::UnknownFuel;
    cases[12].after.remaining=1001;cases[12].issue=m::NonMonotone;
    for(const auto& c:cases){m::Recorder meter;const auto token=meter.begin(m::Stage::History,c.before);const auto out=meter.finish(token,c.after);
        CHECK(out.closed&&!out.cost.known&&(out.issues&c.issue));
        CHECK(meter.stats(m::Stage::History)->starts==1&&meter.stats(m::Stage::History)->returned==1&&meter.stats(m::Stage::History)->unknown==1);
        CHECK(!meter.stats(m::Stage::History)->peak_cost.known&&meter.last().stage==m::Stage::History);
        CHECK(meter.stats(m::Stage::History)->cross_round==unsigned(c.issue==m::CrossRound));
        CHECK(meter.stats(m::Stage::History)->epoch_mismatch==unsigned(c.issue==m::ChangedEpoch));
    }
    m::Recorder meter;auto before=sample(200000),after=sample(100000);before.epoch.round=1;after.epoch.round=2;
    const auto crossed=meter.finish(meter.begin(m::Stage::SimBegin,before),after);
    CHECK((crossed.issues&m::CrossRound)&&!crossed.cost.known); // Monotone fuel across a reset is still NOT a cost.
    before=sample(100);after=sample(200000);after.epoch.round=3;
    const auto reset=meter.finish(meter.begin(m::Stage::SimAdvance,before),after);
    CHECK((reset.issues&m::CrossRound)&&(reset.issues&m::NonMonotone)&&!reset.cost.known);
    const auto max=std::numeric_limits<std::uint64_t>::max();
    const auto wide=meter.finish(meter.begin(m::Stage::GeometryCopy,sample(max)),sample(max-7));
    CHECK(wide.cost.known&&wide.cost.value==7);
    const auto grows=meter.finish(meter.begin(m::Stage::GeometryCopy,sample(0)),sample(max));
    CHECK(!grows.cost.known&&(grows.issues&m::NonMonotone)&&meter.stats(m::Stage::GeometryCopy)->peak_cost.value==7);
}
void nested_and_tokens(){
    m::Recorder meter;const auto outer=meter.begin(m::Stage::Capture,sample());
    const auto child=meter.begin(m::Stage::Readback,sample(950));
    CHECK(meter.depth()==2&&meter.active().overlapping&&meter.active().stage==m::Stage::Readback);
    const auto wrong=meter.finish(outer,sample(900));
    CHECK(!wrong.closed&&!wrong.cost.known&&(wrong.issues&m::OutOfOrder)&&meter.depth()==2);
    CHECK(wrong.active&&wrong.stage==m::Stage::Readback&&meter.last().stage==m::Stage::Readback);
    CHECK(meter.stats(m::Stage::Capture)->returned==0&&meter.active().stage==m::Stage::Readback);
    const auto inner=meter.finish(child,sample(925));
    CHECK(inner.closed&&inner.cost.known&&inner.cost.value==25&&inner.overlapping&&meter.depth()==1);
    CHECK(meter.active().stage==m::Stage::Capture&&meter.active().overlapping);
    const auto full=meter.finish(outer,sample(900));
    CHECK(full.closed&&full.cost.known&&full.cost.value==100&&full.overlapping&&meter.depth()==0);
    CHECK(meter.stats(m::Stage::Capture)->peak_overlapping&&meter.stats(m::Stage::Readback)->peak_overlapping);
    // The API exposes inclusive per-interval observations, never 100+25 as a unique cost.
    const auto again=meter.finish(child,sample(850));CHECK(!again.closed&&(again.issues&m::InvalidToken));
    CHECK(!again.active&&again.stage==m::Stage::Capture&&again.serial==full.serial);
    const auto next=meter.begin(m::Stage::Capture,sample(800));
    const auto next_serial=meter.active().serial;
    const auto old=meter.finish(outer,sample(700));CHECK(!old.closed&&meter.depth()==1&&meter.active().serial==next_serial&&next_serial!=full.serial);
    CHECK(meter.finish(next,sample(780)).cost.value==20);
    m::Recorder other;const auto foreign=other.begin(m::Stage::Capture,sample(100));
    CHECK(!meter.finish(foreign,sample(90)).closed&&other.depth()==1);
    CHECK(other.finish(foreign,sample(90)).cost.known);
    CHECK(!meter.finish({},sample()).closed&&meter.rejected_finishes()==5);
}
struct FailedCall {};
void interrupted_lifetime(){
    m::Recorder meter;m::Token token;
    try {token=meter.begin(m::Stage::Order,sample());throw FailedCall{};}catch(const FailedCall&){}
    CHECK(meter.depth()==1&&meter.active().stage==m::Stage::Order&&!meter.active().cost.known);
    const auto thrown=meter.finish(token,sample(990),m::Exit::Threw);
    CHECK(thrown.closed&&!thrown.cost.known&&(thrown.issues&m::Thrown)&&meter.last().stage==m::Stage::Order);
    CHECK(meter.stats(m::Stage::Order)->threw==1&&meter.stats(m::Stage::Order)->returned==0);
    const auto incomplete=meter.finish(meter.begin(m::Stage::Vision,sample()),sample(999),m::Exit::Incomplete);
    CHECK(incomplete.closed&&!incomplete.cost.known&&(incomplete.issues&m::Unfinished));
    meter.begin(m::Stage::RouteBegin,sample());meter.begin(m::Stage::RouteAdvance,sample(980));
    auto later=sample(900);later.epoch.round=10;meter.interrupt_all(later);
    CHECK(meter.depth()==0&&meter.last().stage==m::Stage::RouteBegin&&!meter.last().cost.known);
    CHECK(meter.stats(m::Stage::RouteBegin)->cross_round==1&&meter.stats(m::Stage::RouteBegin)->incomplete==1);
    CHECK(meter.stats(m::Stage::RouteAdvance)->cross_round==1&&meter.stats(m::Stage::RouteAdvance)->incomplete==1);
    const auto stale=meter.begin(m::Stage::Activation,sample());meter.restart();
    CHECK(meter.depth()==0&&meter.last().stage==m::Stage::Activation&&!meter.last().cost.known);
    CHECK(!meter.finish(stale,sample(950)).closed&&meter.stats(m::Stage::Activation)->incomplete==1);
    const auto fresh=meter.begin(m::Stage::Activation,sample());CHECK(meter.finish(fresh,sample(950)).cost.known);
}
void capacity_and_no_heap(){
    m::Recorder meter;std::array<m::Token,m::nesting_limit> tokens{};
    for(unsigned i=0;i<m::nesting_limit;++i)tokens[i]=meter.begin(static_cast<m::Stage>(i),sample(1000-i));
    CHECK(meter.depth()==m::nesting_limit);
    const auto full=meter.begin(m::Stage::LegacyTail,sample(900));
    CHECK(!full.valid()&&meter.depth()==m::nesting_limit&&meter.dropped_starts()==1);
    CHECK(meter.last().stage==m::Stage::LegacyTail&&(meter.last().issues&m::Capacity));
    CHECK(meter.stats(m::Stage::LegacyTail)->starts==0&&meter.stats(m::Stage::LegacyTail)->dropped_starts==1);
    for(unsigned i=m::nesting_limit;i>0;--i){const auto out=meter.finish(tokens[i-1],sample(800));CHECK(out.closed&&out.cost.known&&out.overlapping);}
    CHECK(meter.depth()==0);
    CHECK(!meter.begin(m::Stage::Unknown,sample()).valid()&&(meter.last().issues&m::UnknownStage));
    CHECK(!meter.begin(m::Stage::Count,sample()).valid()&&meter.dropped_starts()==3);
    const auto calls=allocation::calls;allocation::watch=true;
    {
        m::Recorder retained;
        for(unsigned i=0;i<128;++i){const auto token=retained.begin(m::Stage::StaticChunk,sample());retained.finish(token,sample(999));}
        retained.restart();retained.interrupt_all();
    }
    allocation::watch=false;CHECK(allocation::calls==calls);
}
}
int main(){zero_identity_and_snapshot();fence_failures();nested_and_tokens();interrupted_lifetime();capacity_and_no_heap();
    std::printf("stage_meter_size=%zu stack=%u stages=%u\n",sizeof(m::Recorder),m::nesting_limit,m::stage_count);
    std::printf("capital_service_stage_meter %u checks %u failures\n",checks,failures);return failures?1:0;}
