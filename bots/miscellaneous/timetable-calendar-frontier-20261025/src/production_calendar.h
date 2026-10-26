#pragma once
// Exact valuation index for a completed immutable production event stream.
// This module establishes calendar arithmetic only. Producer readiness, its
// complete physical programme, scenario/key/shift premises and actual fresh
// command admission remain independent caller obligations.
#include "production_job.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace tt::production_calendar {
namespace job=production_job;
inline constexpr std::size_t max_segments=512;
enum class Status { NeedCompute,Ready,DenseFallback,Invalid };
enum class Reason { None,Input,Window,Count,Capacity,Shape,Overlap,Overflow,Custody,Allocation };
struct Window {std::uint32_t start{},horizon{};std::uint64_t initial_carried{};};
struct Source {
    // Must be exactly the finished Quote's events/window. The lifetime pin
    // owns the event storage (an alias of its enclosing Runtime is sufficient).
    // The producer must never mutate the stream while construction reads it.
    std::span<const job::Event> events;Window window;
    std::shared_ptr<const void> lifetime;
    bool completed_immutable{};
    // Optional stricter representation capacity (also permits deterministic
    // fallback qualification). It may never enlarge the fixed module bound.
    std::size_t segment_limit{max_segments};
};
struct Value {Status status{Status::NeedCompute};Reason reason{Reason::None};std::uint64_t banked{},carried{};};

class Index {
public:
    // No allocation, event traversal, body query or forecast is performed here.
    explicit Index(Source source):source_(std::move(source)) {
        if(!source_.completed_immutable||!source_.lifetime) {fail(Status::Invalid,Reason::Input);return;}
        if(source_.window.horizon<source_.window.start) {fail(Status::Invalid,Reason::Window);return;}
        if(source_.segment_limit>max_segments||source_.events.size()>source_.segment_limit)
            {fail(Status::DenseFallback,Reason::Capacity);return;}
        totals_[0]=source_.window.initial_carried;
    }
    Index(const Index&)=delete;Index& operator=(const Index&)=delete;
    Index(Index&&)=default;Index& operator=(Index&&)=default;
    Status advance(std::size_t units);
    Status status() const{return status_;}Reason reason() const{return reason_;}
    std::size_t last_work() const{return last_work_;}
    std::uint64_t work_used() const{return work_used_;}
    Window window() const{return source_.window;}
    std::size_t segments() const{return runs_[0].size()+runs_[1].size();}
    // Relative inclusive cutoff in [0,horizon-start]. Construction must finish
    // before any value is eligible. A value does not certify actual execution.
    Value query(std::uint32_t relative_inclusive_cut) const;
private:
    struct Run {
        std::uint32_t offset{},period{},count{};
        std::uint64_t last{},gems{},before{},after{};
    };
    enum class Phase { ReservePayout,ReserveBank,Events,Custody,Done };
    Source source_;std::array<std::vector<Run>,2> runs_;
    std::array<std::uint64_t,2> totals_{};
    Status status_{Status::NeedCompute};Reason reason_{Reason::None};Phase phase_{Phase::ReservePayout};
    std::size_t event_{},bank_run_{},payout_run_{},last_work_{};std::uint32_t bank_occurrence_{};
    std::uint64_t work_used_{};
    void fail(Status status,Reason reason) {status_=status;reason_=reason;phase_=Phase::Done;}
    void unit();void event_unit();void custody_unit();
    bool prefix(std::size_t,std::uint32_t,std::uint64_t&) const;
    static bool add(std::uint64_t a,std::uint64_t b,std::uint64_t& out) {
        if(b>std::numeric_limits<std::uint64_t>::max()-a)return false;out=a+b;return true;
    }
    static bool product(std::uint64_t a,std::uint64_t b,std::uint64_t& out) {
        if(a&&b>std::numeric_limits<std::uint64_t>::max()/a)return false;out=a*b;return true;
    }
};

inline void Index::event_unit() {
    if(event_>=source_.events.size()) {phase_=Phase::Custody;return;}
    const auto& event=source_.events[event_++];
    std::size_t kind{};
    if(event.kind==job::EventKind::Payout)kind=0;
    else if(event.kind==job::EventKind::Bank)kind=1;
    else {fail(Status::DenseFallback,Reason::Shape);return;}
    const std::uint64_t duration=source_.window.horizon-source_.window.start;
    // Same count/window acceptance as the existing dense checker. A zero
    // period is exact count-fold multiplicity at one date, not a division.
    if(!event.count||event.count>duration+1) {fail(Status::Invalid,Reason::Count);return;}
    std::uint64_t displacement{},last{},amount{},total{};
    if(!product(event.count-1,event.period,displacement)||!add(event.offset,displacement,last))
        {fail(Status::Invalid,Reason::Overflow);return;}
    if(last>duration) {fail(Status::Invalid,Reason::Window);return;}
    auto& runs=runs_[kind];
    if(!runs.empty()&&event.offset<=runs.back().last)
        {fail(Status::DenseFallback,Reason::Overlap);return;}
    if(!product(event.count,event.gems,amount)||!add(totals_[kind],amount,total))
        {fail(Status::Invalid,Reason::Overflow);return;}
    runs.push_back({event.offset,event.period,event.count,last,event.gems,totals_[kind],total});
    totals_[kind]=total;
}
inline bool Index::prefix(std::size_t kind,std::uint32_t cut,std::uint64_t& out) const {
    const auto& runs=runs_[kind];out=kind==0?source_.window.initial_carried:0;
    // Last run whose first date is <=cut. Previous runs are all finished by
    // the checked same-kind non-overlap premise, including gaps between runs.
    std::size_t left=0,right=runs.size();
    while(left<right) {
        const auto middle=left+(right-left)/2;
        if(runs[middle].offset<=cut)left=middle+1;else right=middle;
    }
    if(!left)return true;
    const auto& run=runs[left-1];
    const std::uint64_t count=run.period?
        std::min<std::uint64_t>(run.count,1+(static_cast<std::uint64_t>(cut)-run.offset)/run.period):run.count;
    std::uint64_t amount{};return product(count,run.gems,amount)&&add(run.before,amount,out);
}
inline void Index::custody_unit() {
    if(bank_run_>=runs_[1].size()) {status_=Status::Ready;phase_=Phase::Done;return;}
    const auto& run=runs_[1][bank_run_];
    // Endpoint validation above proved this uint64 date lies in the uint32
    // window. Visit EVERY Bank occurrence, including coincident multiplicity.
    const auto date=static_cast<std::uint32_t>(static_cast<std::uint64_t>(run.offset)+
        static_cast<std::uint64_t>(run.period)*bank_occurrence_);
    // Bank dates are nondecreasing. Retain a monotone payout cursor rather
    // than hiding a binary search per occurrence in construction. Crossing
    // one payout segment is its own unit; total work is O(E+Bank occurrences).
    if(payout_run_<runs_[0].size()&&runs_[0][payout_run_].last<date) {++payout_run_;return;}
    std::uint64_t payouts=totals_[0],banks{},amount{};
    if(payout_run_<runs_[0].size()) {
        const auto& payout=runs_[0][payout_run_];payouts=payout.before;
        if(payout.offset<=date) {
            const std::uint64_t count=payout.period?
                std::min<std::uint64_t>(payout.count,1+(static_cast<std::uint64_t>(date)-payout.offset)/payout.period):payout.count;
            if(!product(count,payout.gems,amount)||!add(payout.before,amount,payouts))
                {fail(Status::Invalid,Reason::Overflow);return;}
        }
    }
    const std::uint64_t count=run.period?static_cast<std::uint64_t>(bank_occurrence_)+1:run.count;
    if(!product(count,run.gems,amount)||!add(run.before,amount,banks))
        {fail(Status::Invalid,Reason::Overflow);return;}
    if(banks>payouts) {fail(Status::Invalid,Reason::Custody);return;}
    if(++bank_occurrence_>=run.count) {++bank_run_;bank_occurrence_=0;}
}
inline void Index::unit() {
    switch(phase_) {
    // Separate bounded allocation units, no zero-filled horizon-sized storage.
    case Phase::ReservePayout:runs_[0].reserve(source_.events.size());phase_=Phase::ReserveBank;break;
    case Phase::ReserveBank:runs_[1].reserve(source_.events.size());phase_=Phase::Events;break;
    case Phase::Events:event_unit();break;
    case Phase::Custody:custody_unit();break;
    case Phase::Done:break;
    }
}
inline Status Index::advance(std::size_t units) {
    last_work_=0;
    try {
        while(last_work_<units&&status_==Status::NeedCompute) {++last_work_;++work_used_;unit();}
    } catch(const std::bad_alloc&) {fail(Status::DenseFallback,Reason::Allocation);}
      catch(const std::length_error&) {fail(Status::DenseFallback,Reason::Capacity);}
    return status_;
}
inline Value Index::query(std::uint32_t cut) const {
    if(status_!=Status::Ready)return {status_,reason_,0,0};
    if(cut>source_.window.horizon-source_.window.start)return {Status::Invalid,Reason::Window,0,0};
    std::uint64_t payouts{},banks{};
    if(!prefix(0,cut,payouts)||!prefix(1,cut,banks))return {Status::Invalid,Reason::Overflow,0,0};
    if(banks>payouts)return {Status::Invalid,Reason::Custody,0,0};
    return {Status::Ready,Reason::None,banks,payouts-banks};
}
} // namespace tt::production_calendar
