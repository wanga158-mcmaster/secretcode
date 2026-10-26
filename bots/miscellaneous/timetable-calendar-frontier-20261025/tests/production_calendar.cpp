#include "../src/production_calendar.h"
#include <cstdio>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace {
namespace pc=tt::production_calendar;
namespace pj=tt::production_job;
using Event=pj::Event;using Kind=pj::EventKind;
int checks{},failures{};
void check(bool value,const char* message) {++checks;if(!value){++failures;std::printf("FAIL %s\n",message);}}
struct Owned {std::vector<Event> events;};
pc::Source source(const std::shared_ptr<Owned>& owner,pc::Window window) {
    return {std::span<const Event>{owner->events},window,owner,true};
}
pc::Status finish(pc::Index& index,std::size_t slice=1) {
    for(unsigned unit=0;unit<200000&&index.status()==pc::Status::NeedCompute;++unit) {
        const auto before=index.work_used();index.advance(slice);
        check(index.last_work()<=slice&&index.work_used()>before,"positive bounded calendar slice makes retained progress");
    }
    check(index.status()!=pc::Status::NeedCompute,"calendar fixture finishes within a finite structural limit");
    return index.status();
}
// Independent dense oracle: expand every occurrence into a horizon bucket,
// then sum and check EVERY date. It does not use the compact run formula or
// per-kind ordering assumption, including the fallback overlap fixtures.
struct Dense {bool valid{true};std::vector<std::uint64_t> bank,payout;};
Dense dense(const std::vector<Event>& events,pc::Window window) {
    Dense result;
    if(window.horizon<window.start){result.valid=false;return result;}
    const auto length=static_cast<std::size_t>(window.horizon-window.start)+1;
    result.bank.assign(length,0);result.payout.assign(length,0);result.payout[0]=window.initial_carried;
    const auto add=[&](std::uint64_t& target,std::uint64_t amount) {
        if(amount>std::numeric_limits<std::uint64_t>::max()-target)return false;target+=amount;return true;
    };
    for(const auto& event:events) {
        if(!event.count||event.count>length){result.valid=false;return result;}
        if(event.kind!=Kind::Bank&&event.kind!=Kind::Payout){result.valid=false;return result;}
        for(std::uint32_t repetition=0;repetition<event.count;++repetition) {
            const auto offset=static_cast<std::uint64_t>(event.offset)+static_cast<std::uint64_t>(event.period)*repetition;
            if(offset>=length){result.valid=false;return result;}
            auto& bucket=event.kind==Kind::Bank?result.bank:result.payout;
            if(!add(bucket[static_cast<std::size_t>(offset)],event.gems)){result.valid=false;return result;}
        }
    }
    for(std::size_t date=0;date<length;++date) {
        if(date&&(!add(result.bank[date],result.bank[date-1])||!add(result.payout[date],result.payout[date-1])))
            {result.valid=false;return result;}
        if(result.bank[date]>result.payout[date]){result.valid=false;return result;}
    }
    return result;
}
void equal_every_cut(const std::shared_ptr<Owned>& owner,pc::Window window,const char* message) {
    const auto oracle=dense(owner->events,window);check(oracle.valid,"declared property trace is independently dense-valid");
    pc::Index index{source(owner,window)};check(finish(index,3)==pc::Status::Ready,message);
    if(!oracle.valid||index.status()!=pc::Status::Ready)return;
    for(std::uint32_t cut=0;cut<=window.horizon-window.start;++cut) {
        const auto value=index.query(cut);
        check(value.status==pc::Status::Ready&&value.banked==oracle.bank[cut]&&
            value.carried==oracle.payout[cut]-oracle.bank[cut],"compact inclusive cutoff equals independent expanded dense oracle");
    }
    check(index.query(window.horizon-window.start+1).status==pc::Status::Invalid,
        "a query outside the completed window is rejected without changing ready index state");
    check(index.status()==pc::Status::Ready,"an invalid cutoff does not mutate an otherwise completed index");
}
void interleaving_boundaries_and_custody() {
    auto owner=std::make_shared<Owned>();owner->events={{Kind::Payout,10,20,4,11},{Kind::Bank,15,20,4,11}};
    pc::Index index{source(owner,{100,200,0})};
    check(index.work_used()==0&&index.segments()==0,"constructor does not allocate/copy/traverse a calendar");
    check(index.query(32).status==pc::Status::NeedCompute,"Producer completion alone does not publish an unchecked calendar value");
    check(finish(index)==pc::Status::Ready,"cross-kind interleaving arithmetic runs are supported exactly");
    const auto cut=index.query(32);
    check(cut.banked==11&&cut.carried==11,"cut32 includes two payouts and one bank rather than entire first-offset runs");
    check(index.work_used()==2+owner->events.size()+1+4+1+1,
        "construction uses two reserves, E copies, one event transition, Cbank visits, monotone payout crossings and ready transition");
    equal_every_cut(owner,{100,200,0},"all cuts of compressed interleaving match dense expansion");
    owner=std::make_shared<Owned>();owner->events={
        {Kind::Payout,0,0,2,4},{Kind::Bank,0,0,3,3},
        {Kind::Payout,5,0,2,5},{Kind::Bank,5,0,2,6}};
    equal_every_cut(owner,{10,15,3},"zero/H cutoff and period-zero multiplicity remain exact");
    pc::Index simultaneous{source(owner,{10,15,3})};finish(simultaneous);
    check(simultaneous.query(0).banked==9&&simultaneous.query(0).carried==2&&
        simultaneous.query(5).banked==21&&simultaneous.query(5).carried==0,
        "initial custody appears exactly once and same-date payouts are credited before custody comparison");
    owner=std::make_shared<Owned>();owner->events={{Kind::Payout,0,4,3,3},{Kind::Bank,1,3,3,3}};
    check(!dense(owner->events,{0,8,0}).valid,"oracle detects interior custody failure despite equal H totals");
    pc::Index interior{source(owner,{0,8,0})};
    check(finish(interior)==pc::Status::Invalid&&interior.reason()==pc::Reason::Custody,
        "every bank date is checked: bank7 cannot borrow payout8 merely because final totals match");
    owner=std::make_shared<Owned>();owner->events={{Kind::Bank,1,0,1,5},{Kind::Payout,2,0,1,5}};
    pc::Index early{source(owner,{0,5,0})};
    check(finish(early)==pc::Status::Invalid&&early.reason()==pc::Reason::Custody,
        "a bank before its first payout has no invented custody support");
    equal_every_cut(owner,{0,5,5},"actual initial carried custody can support that same early bank");
}
void overflow_shape_and_fallback() {
    const auto maximum=std::numeric_limits<std::uint64_t>::max();
    auto owner=std::make_shared<Owned>();owner->events={{Kind::Payout,0,1,3,maximum/3},{Kind::Bank,0,1,3,maximum/3}};
    equal_every_cut(owner,{0,2,0},"exact UINT64_MAX totals are supported without an invented safety margin");
    pc::Index addition{source(owner,{0,2,1})};
    check(finish(addition)==pc::Status::Invalid&&addition.reason()==pc::Reason::Overflow,
        "initial-carried plus total payout overflow is rejected before valuation readiness");
    owner=std::make_shared<Owned>();owner->events={{Kind::Payout,0,1,3,maximum/3+1}};
    pc::Index multiplication{source(owner,{0,2,0})};
    check(finish(multiplication)==pc::Status::Invalid&&multiplication.reason()==pc::Reason::Overflow,
        "run count-times-gems overflow cannot wrap into a cheap valid prefix");
    owner=std::make_shared<Owned>();owner->events={{Kind::Payout,0,0,4,1}};
    pc::Index count{source(owner,{0,2,0})};
    check(finish(count)==pc::Status::Invalid&&count.reason()==pc::Reason::Count,
        "count exceeds the same inclusive-window bound enforced by dense checking");
    owner->events={{Kind::Payout,0,0,0,1}};pc::Index empty_count{source(owner,{0,2,0})};
    check(finish(empty_count)==pc::Status::Invalid&&empty_count.reason()==pc::Reason::Count,"zero-count source records are rejected");
    owner=std::make_shared<Owned>();owner->events={{Kind::Payout,2,2,2,1}};
    pc::Index outside{source(owner,{0,3,0})};
    check(finish(outside)==pc::Status::Invalid&&outside.reason()==pc::Reason::Window,
        "last arithmetic occurrence outside H is rejected rather than silently truncated");
    owner=std::make_shared<Owned>();owner->events={{Kind::Payout,0,4,3,1},{Kind::Payout,5,0,1,1}};
    check(dense(owner->events,{0,10,0}).valid,"the overlapping shape really has a valid dense interpretation");
    pc::Index overlap{source(owner,{0,10,0})};
    check(finish(overlap)==pc::Status::DenseFallback&&overlap.reason()==pc::Reason::Overlap,
        "same-kind overlapping intervals request dense fallback instead of approximating sorted prefix totals");
    owner=std::make_shared<Owned>();owner->events={{static_cast<Kind>(7),0,0,1,0}};
    pc::Index shape{source(owner,{0,2,0})};
    check(finish(shape)==pc::Status::DenseFallback&&shape.reason()==pc::Reason::Shape,
        "an unsupported event family explicitly requests fallback");
    owner=std::make_shared<Owned>();owner->events.resize(pc::max_segments+1);
    pc::Index capacity{source(owner,{0,2000,0})};
    check(capacity.status()==pc::Status::DenseFallback&&capacity.reason()==pc::Reason::Capacity&&capacity.work_used()==0,
        "oversized representation fails explicitly without an unbounded constructor copy");
    owner=std::make_shared<Owned>();auto incomplete=source(owner,{0,2,0});incomplete.completed_immutable=false;
    pc::Index not_finished{std::move(incomplete)};
    check(not_finished.status()==pc::Status::Invalid&&not_finished.reason()==pc::Reason::Input,
        "calendar checking cannot turn an unfinished programme into eligible valuation");
}
void immutable_lifetime_and_empty_stream() {
    auto owner=std::make_shared<Owned>();owner->events={{Kind::Payout,1,0,1,11},{Kind::Bank,3,0,1,11}};
    std::weak_ptr<Owned> lifetime=owner;
    auto index=std::make_unique<pc::Index>(source(owner,{0,5,0}));owner.reset();
    check(!lifetime.expired(),"a source lifetime pin survives external owner reset while staged validation reads it");
    check(finish(*index)==pc::Status::Ready&&index->query(5).banked==11,"the pinned immutable source completes after external reset");
    auto restored=lifetime.lock();restored->events.clear();restored->events.shrink_to_fit();restored.reset();
    check(index->query(5).banked==11,"postconstruction queries use owned checked index records rather than borrowed source elements");
    index.reset();check(lifetime.expired(),"destroying the index releases its source lifetime pin");
    owner=std::make_shared<Owned>();equal_every_cut(owner,{7,7,17},"an empty completed event stream preserves only its initial custody at cutoff0");
    auto unpinned=source(owner,{0,2,0});unpinned.lifetime.reset();pc::Index missing{std::move(unpinned)};
    check(missing.status()==pc::Status::Invalid&&missing.reason()==pc::Reason::Input,"an unpinned borrowed stream is not accepted");
}
pj::Leg path(std::initializer_list<pj::Point> points,std::uint32_t handoff=1) {
    pj::Leg result;result.complete=result.disjoint_supported=true;result.handoff_inputs=handoff;
    for(auto point:points)result.points.push_back({point,0});return result;
}
void current_programme_event_stream() {
    pj::Input input;input.start=10;input.horizon=200;input.key={0,0,7,3,4,5,6};
    auto& self=input.self;self.actor=0;self.body_revision=3;self.epoch=10;
    self.own=self.complete=self.stable_outgoing=true;self.body.pos={1,2};self.body.hp=self.body.max_hp=1000;
    self.body.strength=self.raw_strength=100;self.body.focus=self.raw_focus=100;self.body.attack_range=1;
    auto& job=input.job;job.group=0;job.generation=7;job.life_revision=4;job.geometry_revision=5;job.word_revision=6;
    job.life=pj::Life::Unknown;job.perch={2,2};job.width=8;job.height=8;job.member_count=1;
    job.members[0]={0,0,{3,2},201,0,1,false,0,true};job.full_hp[0]=201;job.gems=11;
    job.capture_payout_inputs=6;job.respawn_inputs=20;
    job.full_group_known=job.conditional_hp=job.continuation_supported=job.external_sources_complete=job.common_full_generation=true;
    job.repeat=true;job.initial=path({{1,2},{2,2}});job.capture=path({{2,2},{3,2}},0);
    job.release=path({{3,2},{2,2}});job.bank=path({{2,2},{1,3}});job.renewal=path({{1,3},{2,2}});
    job.home=path({{1,3},{0,3}});job.redeploy=path({{0,3},{1,2},{2,2}});
    auto producer=std::make_shared<pj::Producer>(std::move(input));
    for(unsigned unit=0;unit<200000&&producer->status()==pj::Status::NeedCompute;++unit)producer->advance(1);
    const auto& quote=producer->result();
    check(quote.status==pj::Status::Ready&&quote.complete&&quote.supported&&quote.disjoint_supported,
        "the integration fixture uses an actual fully finished current Producer programme");
    check(quote.shift_invariant&&quote.events.size()<quote.cycles*2,"the real source exercised periodic compression");
    pc::Index calendar{{std::span<const Event>{quote.events},{quote.start,quote.horizon,quote.initial_carried},producer,true}};
    check(finish(calendar)==pc::Status::Ready,"actual current Producer events obtain a checked compact calendar");
    const auto oracle=dense(quote.events,{quote.start,quote.horizon,quote.initial_carried});
    check(oracle.valid,"actual Producer trace has independent dense custody support");
    if(calendar.status()==pc::Status::Ready&&oracle.valid)for(std::uint32_t cut=0;cut<=quote.horizon-quote.start;++cut) {
        const auto actual=calendar.query(cut);
        check(actual.banked==oracle.bank[cut]&&actual.carried==oracle.payout[cut]-oracle.bank[cut],
            "every actual programme cutoff preserves banked/carried valuation exactly");
    }
    check(calendar.query(quote.horizon-quote.start).banked==quote.banked_gems_by_H,
        "completed calendar reproduces the original actual common-H banked forecast");
    pc::Source bounded{std::span<const Event>{quote.events},{quote.start,quote.horizon,quote.initial_carried},producer,true};
    bounded.segment_limit=0;pc::Index forced_fallback{std::move(bounded)};
    check(!quote.events.empty()&&forced_fallback.status()==pc::Status::DenseFallback&&
        forced_fallback.reason()==pc::Reason::Capacity&&forced_fallback.work_used()==0,
        "a real completed immutable programme can deterministically retain dense fallback at an explicit smaller representation capacity");
}
void deterministic_properties() {
    std::uint32_t state=0x9bbc0846u;
    const auto random=[&]() {state=state*1664525u+1013904223u;return state;};
    for(unsigned sample=0;sample<100;++sample) {
        auto owner=std::make_shared<Owned>();std::uint32_t base=random()%3;
        const unsigned blocks=1+random()%4;
        for(unsigned block=0;block<blocks;++block) {
            const auto period=4+random()%12,count=1+random()%6,delay=1+random()%(period-1),gems=1+random()%1000;
            owner->events.push_back({Kind::Payout,base,period,count,gems});
            owner->events.push_back({Kind::Bank,base+delay,period,count,gems});
            base+=period*(count-1)+delay+1+random()%5;
        }
        equal_every_cut(owner,{20,20+base,random()%8},"deterministic interleaved property trace indexes exactly");
    }
}
}
int main() {
    interleaving_boundaries_and_custody();overflow_shape_and_fallback();
    immutable_lifetime_and_empty_stream();current_programme_event_stream();deterministic_properties();
    std::printf("production calendar: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
