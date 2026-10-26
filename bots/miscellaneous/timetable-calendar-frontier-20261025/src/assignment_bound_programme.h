#pragma once
// One freshly bound, owned physical programme. No old Runtime/menu is built or
// advanced here. Choice/refutation are shared data types only. This certificate
// is a conditional nomination: fresh Life, inventory/wallet/effects, represented
// absolute clock, M/A/C, occupancy and actual command admission remain caller
// obligations. Keep the fresh binder's complete Current companion separately;
// production_job::Key alone does not encode all of those semantic facts.
#include "assignment_owner.h"
#include "production_calendar.h"
#include "production_job.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>

namespace tt::assignment_bound_programme {
namespace pj=production_job;
namespace pc=production_calendar;
namespace ao=assignment_owner;
namespace geometry=assignment_geometry;

enum class Status { NeedCompute,ProgrammeReady,ValuationReady,Unsupported };
enum class Reason { None,Nomination,Producer,Premises,Calendar,Totals,Allocation,Capacity };

struct Nomination {
    std::size_t candidate_id{ao::no_candidate};
    std::uint8_t canonical_group{};
    geometry::Family family{geometry::Family::Basic};
    geometry::Point perch,capture_tile,release_tile,bank_stand,home;
};
struct Seed {
    pj::Input input;
    Nomination nomination;
    std::size_t segment_limit{pc::max_segments};
};

class Programme {
public:
    explicit Programme(Seed);
    static std::shared_ptr<Programme> make(Seed);
    Programme(const Programme&)=delete;Programme& operator=(const Programme&)=delete;
    Programme(Programme&&)=delete;Programme& operator=(Programme&&)=delete;
    // Each charged unit advances at most one Producer or Index unit, or one
    // fixed-size stage transition/check. No event/path/menu scan is hidden in a
    // transition. These structural units are not a native fuel measurement.
    Status advance(std::size_t units);
    Status status() const{return status_;}
    Reason reason() const{return reason_;}
    std::size_t last_work() const{return last_work_;}
    std::uint64_t work_used() const{return work_used_;}
    std::uint64_t producer_work() const{return producer_->result().work_used;}
    std::uint64_t calendar_work() const{return index_?index_->work_used():0;}
    pj::Status producer_status() const{return producer_->status();}
    pj::Reason producer_reason() const{return producer_->result().reason;}
    bool has_calendar() const{return static_cast<bool>(index_);}
    pc::Status calendar_status() const{return index_?index_->status():pc::Status::NeedCompute;}
    pc::Reason calendar_reason() const{return index_?index_->reason():pc::Reason::None;}
    bool programme_ready() const{return produced_;}
    bool valuation_ready() const{return status_==Status::ValuationReady;}
    bool full_totals_checked() const{return totals_checked_;}
    bool full_totals_match() const{return totals_checked_&&totals_match_;}
    const pj::Input& input() const{return producer_->programme();}
    const pj::Quote& forecast() const{return producer_->result();}
    const pj::CashPlan* cash_plan() const{return producer_->cash_plan();}
    const Nomination& nomination() const{return nomination_;}
    // Read-only weak lifetime diagnostic; it cannot mutate the Producer after
    // Index construction. Programme and Index each pin the separate Producer,
    // while Producer has no reference to either owner (no self-cycle).
    std::weak_ptr<const pj::Producer> producer_lifetime() const{return producer_;}
    std::optional<ao::Choice> lookup(std::uint32_t start,std::uint32_t horizon,
        const pj::Key& current,const ao::PhysicalRefutation* blocked=nullptr) const;
    std::optional<ao::Choice> lookup_exact(std::uint32_t start,std::uint32_t horizon,
        const pj::Key& current,const ao::PhysicalRefutation* blocked=nullptr) const {
        return lookup(start,horizon,current,blocked);
    }
private:
    enum class Phase { Produce,OpenIndex,Index,Totals,Done };
    Nomination nomination_;
    std::size_t segment_limit_{};
    // Declaration order also destroys Index before the final Producer pin.
    std::shared_ptr<pj::Producer> producer_;
    std::unique_ptr<pc::Index> index_;
    Status status_{Status::NeedCompute};Reason reason_{Reason::None};Phase phase_{Phase::Produce};
    std::size_t last_work_{};std::uint64_t work_used_{};
    bool produced_{},totals_checked_{},totals_match_{};
    void fail(Reason reason){status_=Status::Unsupported;reason_=reason;phase_=Phase::Done;}
    void unit();
    bool valid_nomination() const;
    static bool same(geometry::Point a,pj::Point b){return a.x==b.x&&a.y==b.y;}
    static bool endpoints(const pj::Leg& leg,geometry::Point from,geometry::Point to) {
        return leg.complete&&leg.disjoint_supported&&!leg.points.empty()&&
            same(from,leg.points.front().point)&&same(to,leg.points.back().point);
    }
};

inline Programme::Programme(Seed seed):nomination_(seed.nomination),segment_limit_(seed.segment_limit),
    producer_(std::make_shared<pj::Producer>(std::move(seed.input))) {
    if(!valid_nomination()){fail(Reason::Nomination);return;}
    if(producer_->status()!=pj::Status::NeedCompute)fail(Reason::Producer);
}
inline std::shared_ptr<Programme> Programme::make(Seed seed) {
    try {return std::make_shared<Programme>(std::move(seed));}
    catch(const std::bad_alloc&) {return {};}
    catch(const std::length_error&) {return {};}
}
inline bool Programme::valid_nomination() const {
    const auto& value=input();const auto& job=value.job;const auto& n=nomination_;
    if(n.candidate_id==ao::no_candidate||static_cast<std::uint64_t>(n.canonical_group)!=value.key.group||
       (n.family!=geometry::Family::Basic&&n.family!=geometry::Family::Area&&n.family!=geometry::Family::Targeted)||
       !same(n.perch,job.perch))return false;
    const geometry::Point origin{value.self.body.pos.x,value.self.body.pos.y};
    return endpoints(job.initial,origin,n.perch)&&endpoints(job.capture,n.perch,n.capture_tile)&&
        endpoints(job.release,n.capture_tile,n.release_tile)&&endpoints(job.bank,n.release_tile,n.bank_stand)&&
        endpoints(job.renewal,n.bank_stand,n.perch)&&endpoints(job.home,n.bank_stand,n.home)&&
        endpoints(job.redeploy,n.home,n.perch);
}
inline void Programme::unit() {
    switch(phase_) {
    case Phase::Produce: {
        const auto state=producer_->advance(1);
        if(state==pj::Status::NeedCompute)return;
        if(state!=pj::Status::Ready){fail(Reason::Producer);return;}
        const auto& quote=forecast();const auto& original=input();
        if(quote.key!=original.key||!quote.complete||!quote.supported||!quote.disjoint_supported||
           !original.job.common_full_generation||quote.start!=original.start||quote.horizon!=original.horizon) {
            fail(Reason::Premises);return;
        }
        produced_=true;status_=Status::ProgrammeReady;phase_=Phase::OpenIndex;return;
    }
    case Phase::OpenIndex: {
        const auto& quote=forecast();
        pc::Source source{quote.events,{quote.start,quote.horizon,quote.initial_carried},producer_,true,segment_limit_};
        index_=std::make_unique<pc::Index>(std::move(source));
        if(index_->status()!=pc::Status::NeedCompute){fail(Reason::Calendar);return;}
        phase_=Phase::Index;return;
    }
    case Phase::Index: {
        const auto state=index_->advance(1);
        if(state==pc::Status::NeedCompute)return;
        if(state!=pc::Status::Ready){fail(Reason::Calendar);return;}
        phase_=Phase::Totals;return;
    }
    case Phase::Totals: {
        const auto& quote=forecast();const auto value=index_->query(quote.horizon-quote.start);
        totals_checked_=true;totals_match_=value.status==pc::Status::Ready&&
            value.banked==quote.banked_gems_by_H&&value.carried==quote.carried_gems;
        if(!totals_match_){fail(Reason::Totals);return;}
        status_=Status::ValuationReady;phase_=Phase::Done;return;
    }
    case Phase::Done:return;
    }
}
inline Status Programme::advance(std::size_t units) {
    last_work_=0;
    try {
        while(last_work_<units&&phase_!=Phase::Done) {
            if(work_used_==std::numeric_limits<std::uint64_t>::max()){fail(Reason::Capacity);break;}
            ++last_work_;++work_used_;unit();
        }
    } catch(const std::bad_alloc&) {fail(Reason::Allocation);}
      catch(const std::length_error&) {fail(Reason::Capacity);}
    return status_;
}
inline std::optional<ao::Choice> Programme::lookup(std::uint32_t start,std::uint32_t horizon,
    const pj::Key& current,const ao::PhysicalRefutation* blocked) const {
    if(!valuation_ready()||!totals_checked_||!totals_match_||!index_||index_->status()!=pc::Status::Ready)return std::nullopt;
    const auto& quote=forecast();const auto& original=input();
    if(quote.status!=pj::Status::Ready||quote.key!=original.key||!quote.complete||!quote.supported||
       !quote.disjoint_supported||!original.job.common_full_generation||start>horizon||horizon>quote.horizon||
       start<quote.start||horizon-start>quote.horizon-quote.start||(start!=quote.start&&!quote.shift_invariant)||
       current.actor!=quote.key.actor||current.group!=quote.key.group||current.body_revision!=quote.key.body_revision||
       current.geometry_revision!=quote.key.geometry_revision||current.word_revision!=quote.key.word_revision)return std::nullopt;
    const auto value=index_->query(horizon-start);if(value.status!=pc::Status::Ready)return std::nullopt;
    const auto& n=nomination_;
    ao::Choice choice{n.candidate_id,n.canonical_group,n.family,n.perch,n.capture_tile,n.release_tile,n.bank_stand,n.home,
        quote.key,start,horizon,value.banked,value.carried,true};
    if(blocked&&blocked->matches(choice))return std::nullopt;
    return choice;
}
} // namespace tt::assignment_bound_programme
