#pragma once
// Retained, SDK-free conditional OWN production menu. No command, foreign
// body, factual future Life, occupancy or path reservation is published here.
#include "assignment_geometry.h"
#include "production_job.h"
#include "production_assignment.h"
#include "production_calendar.h"
#include "production_word_clock.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace tt::assignment_owner {
namespace geometry=assignment_geometry;
namespace job=production_job;
namespace assignment=production_assignment;
inline constexpr std::size_t max_groups=31,max_families=3,max_candidates=max_groups*max_families;
inline constexpr std::size_t no_candidate=std::numeric_limits<std::size_t>::max();
enum class Status { NeedCompute,Ready,Incomplete,Unsupported,Stale };
enum class Reason { None,Shape,Ownership,Catalogue,Dates,Body,Geometry,Traffic,Capacity,Number,Allocation };
struct Group {
    std::uint8_t canonical{};
    int geometry_group{-1};
    job::Job metadata; // Complete immutable members/settings; all seven legs empty.
    bool supported{true}; // Unsupported groups still supply complete NPC potential.
};
struct Limits { geometry::Limits geometry;std::size_t candidates{max_candidates}; };
struct Seed {
    geometry::Grid grid;geometry::Stamp stamp;
    job::Snapshot self;std::vector<Group> groups;
    std::uint32_t model_start{},horizon{};
    std::array<std::uint32_t,geometry::leg_count> handoff_inputs{};
    Limits limits;
    bool complete_npc_catalogue{},conditional_no_future_traffic{};
};
struct Choice {
    std::size_t candidate_id{no_candidate};std::uint8_t group{};
    geometry::Family family{geometry::Family::Basic};
    geometry::Point perch,capture_tile,release_tile,bank_stand,home;
    job::Key key;std::uint32_t start{},horizon{};
    std::uint64_t banked_gems_by_H{},carried_gems{};
    bool conditional{true};
};
struct Progress {
    std::uint64_t units{};std::size_t groups_scanned{},candidates{},completed{},ready{},refuted{};
    std::size_t last_work{};bool model_complete{},rank_complete{};
};
// Scalar post-dispatch view only. The accessor neither advances work nor
// refreshes dependencies; absent candidate/producer is explicit 0xffffffff.
struct Diagnostic {
    std::uint32_t phase{},group{},family{},candidate{0xffffffffu},scan_cell{},copy_leg{},copy_point{},
        event{},repeat{},prefix_cursor{},rank_start{},rank_horizon{},rank_cursor{},status{},reason{},
        producer_status{0xffffffffu};
    std::uint64_t producer_work{};
    bool rank_requested{},rank_complete{};
};
class Runtime;
class IndexedProgramme;
// The weak control-block identity prevents an expired handle from naming a
// later Runtime at the same address. Candidate IDs never mean group argmax.
struct Handle {
    std::weak_ptr<const Runtime> runtime;
    std::size_t candidate_id{no_candidate};
    job::Key programme_key;
    friend bool operator==(const Handle& a,const Handle& b) {
        return !a.runtime.owner_before(b.runtime)&&!b.runtime.owner_before(a.runtime)&&
            a.candidate_id==b.candidate_id&&a.programme_key==b.programme_key;
    }
};
struct FrontierCursor {
    std::weak_ptr<const Runtime> runtime;
    std::size_t next{};bool bound{};
};
enum class FrontierState { Candidate,Skipped,NeedCompute,End,Stale };
struct FrontierPoll {FrontierState state{FrontierState::NeedCompute};std::optional<Handle> handle;};
struct Coverage {
    std::size_t discovered{},finished{},eligible{};
    bool all_groups_finished{};
};
// Caller-owned physical evidence survives query replacement and Life refresh.
// New Life/round/proposal metadata alone cannot erase a failed geometry/word.
// A separate Life-specific enabler/refutation belongs to fresh admission.
struct PhysicalRefutation {
    job::Key premises;geometry::Point perch;
    geometry::Family family{geometry::Family::Basic};bool active{};
    bool matches(const Choice& value) const {
        return active&&premises.actor==value.key.actor&&premises.group==value.key.group&&
            premises.body_revision==value.key.body_revision&&
            premises.geometry_revision==value.key.geometry_revision&&
            premises.word_revision==value.key.word_revision&&perch==value.perch&&family==value.family;
    }
    static PhysicalRefutation from(const Choice& value) {return {value.key,value.perch,value.family,true};}
};
// Data-only lease: lifetime is pinned, while execution and fresh admission use
// a distinct caller cursor. Keeping a lease never promises eligibility later.
class ProgrammeLease {
public:
    ProgrammeLease(const ProgrammeLease&)=default;
    ProgrammeLease(ProgrammeLease&&)=default;
    ProgrammeLease& operator=(const ProgrammeLease&)=default;
    ProgrammeLease& operator=(ProgrammeLease&&)=default;
    const Handle& handle() const{return handle_;}
    const Choice& choice() const{return choice_;}
    const job::Input& input() const{return *input_;}
    const job::Quote& forecast() const{return *forecast_;}
    std::span<const std::uint64_t> bank_prefix() const{return banks_;}
    std::span<const std::uint64_t> payout_prefix() const{return payouts_;}
private:
    friend class Runtime;
    ProgrammeLease()=default;
    std::shared_ptr<const Runtime> pin_;Handle handle_;Choice choice_;
    const job::Input* input_{};const job::Quote* forecast_{};
    std::span<const std::uint64_t> banks_,payouts_;
};
class Runtime {
public:
    explicit Runtime(Seed&&);
    Runtime(const Runtime&)=delete;Runtime& operator=(const Runtime&)=delete;
    Runtime(Runtime&&)=delete;Runtime& operator=(Runtime&&)=delete;
    Status advance(std::size_t units);
    // Frontier-only producer: pauses before dense H-slot initialization until
    // the external exact index accepts it or explicitly requests dense fallback.
    Status advance_programmes(std::size_t units);
    // New stationary-word path only. Actual seed/self clocks stay unchanged;
    // the owned conditional programme cannot use its omitted spell kinds.
    Status advance_word_programmes(std::size_t units);
    Status status() const{return status_;}Reason reason() const{return reason_;}
    std::size_t last_work() const{return last_work_;}
    Progress progress() const;
    Diagnostic diagnostic() const;
    // Schedules a retained <=93 one-candidate scan. No event loop runs here.
    Status rank(std::uint32_t start,std::uint32_t horizon);
    Status ranking_status(std::uint32_t start,std::uint32_t horizon) const;
    // Per-group query checks <=3 retained candidates with O(1) prefix lookup.
    std::optional<Choice> best(std::uint8_t group,std::uint32_t start,std::uint32_t horizon) const;
    // Global answer is exposed only after rank(start,H) finishes via advance.
    std::optional<Choice> best(std::uint32_t start,std::uint32_t horizon) const;
    assignment::Answer quotePair(const assignment::Key&) const;
    bool select(std::size_t candidate_id);
    bool refute(std::size_t candidate_id);
    bool refresh_group(std::uint8_t canonical,std::uint64_t generation,std::uint64_t life_revision);
    std::optional<std::size_t> selected() const{return selected_;}
    std::size_t candidate_count() const{return candidates_.size();}
    const Seed& seed() const{return seed_;}
    // One discovered row per poll, including failed rows as Skipped. An
    // unfinished row waits; no group-complete/rank/all-menu gate is imposed.
    FrontierPoll poll_complete(std::shared_ptr<const Runtime> pin,FrontierCursor&) const;
    FrontierPoll poll_programme(std::shared_ptr<const Runtime> pin,FrontierCursor&) const;
    bool resume_dense(const Handle&);
    bool dense_valuation_ready(const Handle&) const;
    std::optional<Choice> lookup_dense_qualified(const Handle&,std::uint32_t start,std::uint32_t horizon,
        const job::Key& current,const PhysicalRefutation* blocked=nullptr) const;
    Coverage coverage() const;
    std::optional<Choice> lookup_exact(const Handle&,std::uint32_t start,std::uint32_t horizon,
        const job::Key& current,const PhysicalRefutation* blocked=nullptr) const;
    std::optional<ProgrammeLease> programme(const Handle&,std::uint32_t start,std::uint32_t horizon,
        const job::Key& current,const PhysicalRefutation* blocked=nullptr) const;
    assignment::Answer quote_pair_exact(const Handle& first,const Handle& second,const assignment::Key&,
        const job::Key& current_first,const job::Key& current_second,
        const PhysicalRefutation* blocked_first=nullptr,const PhysicalRefutation* blocked_second=nullptr) const;
private:
    friend class IndexedProgramme;
    struct Candidate {
        std::size_t group_index{};geometry::Kit kit;geometry::Point perch,capture,release,bank,home;
        std::unique_ptr<job::Producer> producer;
        std::vector<std::uint64_t> banks,payouts;
        bool complete{},ready{},refuted{};
        job::Status model_status{job::Status::NeedCompute};job::Reason model_reason{job::Reason::None};
    };
    struct Representative {bool present{};geometry::Point perch;geometry::Kit kit;std::uint64_t distance{};double raw{};};
    enum class Phase { Validate,Cache,Scan,Plan,External,Copy,Produce,PrefixInit,PrefixEvents,PrefixSum,Done };
    Seed seed_;std::shared_ptr<geometry::Cache> cache_;std::unique_ptr<geometry::Plan> plan_;
    std::unique_ptr<job::Input> input_;std::vector<Candidate> candidates_;
    std::array<std::array<std::size_t,max_families>,max_groups> ids_{};
    std::array<bool,max_groups> group_complete_{};
    std::array<Representative,max_families> representatives_{};
    std::array<std::array<Representative,max_families>,max_families> options_{};
    Status status_{Status::NeedCompute};Reason reason_{Reason::None};Phase phase_{Phase::Validate};
    std::size_t group_{},member_{},scan_cell_{},family_{},candidate_{no_candidate},external_group_{},external_member_{};
    std::size_t copy_leg_{},copy_point_{},prefix_cursor_{},event_{},repeat_{};
    std::size_t last_work_{};std::uint64_t units_{};
    std::optional<std::size_t> selected_,rank_best_;
    std::uint32_t rank_start_{},rank_horizon_{};std::size_t rank_cursor_{};
    bool rank_requested_{},rank_complete_{};
    void fail(Status,Reason);void unit();void next_group();void next_family();
    void scan_unit();void start_plan();void start_input();void external_unit();void copy_unit();
    void finish_candidate();void prefix_event_unit();void rank_unit();
    bool candidate_choice(std::size_t,std::uint32_t,std::uint32_t,Choice&) const;
    std::optional<Choice> retained_choice(const Handle&,std::uint32_t,std::uint32_t,const job::Key&) const;
    bool release_dense(const Handle&);
    static bool preferred(const Choice&,const Choice&);
    static job::Leg& leg(job::Job&,std::size_t);
};

// External certificate owner: no Runtime self-cycle and no changes to its or
// Candidate's layout. This retains the immutable physical programme separately
// from checked calendar readiness. Old dense APIs remain the fallback.
class IndexedProgramme {
public:
    static std::shared_ptr<IndexedProgramme> open(std::shared_ptr<Runtime>,const Handle&,
        std::size_t segment_limit=production_calendar::max_segments);
    production_calendar::Status advance(std::size_t units);
    production_calendar::Status status() const;
    production_calendar::Reason reason() const{return index_->reason();}
    std::uint64_t work_used() const{return index_->work_used();}
    const Handle& handle() const{return handle_;}
    const job::Input& input() const{return *input_;}
    const job::Quote& forecast() const{return *forecast_;}
    bool valuation_ready() const;
    std::optional<Choice> lookup_exact(std::uint32_t start,std::uint32_t horizon,
        const job::Key& current,const PhysicalRefutation* blocked=nullptr) const;
    bool release_dense();
private:
    IndexedProgramme()=default;
    std::shared_ptr<Runtime> pin_;Handle handle_;
    const job::Input* input_{};const job::Quote* forecast_{};
    std::unique_ptr<production_calendar::Index> index_;
    bool totals_checked_{},totals_match_{};
};

inline job::Leg& Runtime::leg(job::Job& value,std::size_t index) {
    switch(index) {
    case 0:return value.initial;case 1:return value.capture;case 2:return value.release;
    case 3:return value.bank;case 4:return value.renewal;case 5:return value.home;
    default:return value.redeploy;
    }
}
inline void Runtime::fail(Status status,Reason reason) {status_=status;reason_=reason;}
inline Runtime::Runtime(Seed&& seed):seed_(std::move(seed)) {
    for(auto& row:ids_)row.fill(no_candidate);
    if(!seed_.grid.exclusive_ownership) {fail(Status::Unsupported,Reason::Ownership);return;}
    if(!seed_.complete_npc_catalogue) {fail(Status::Incomplete,Reason::Catalogue);return;}
    if(!seed_.conditional_no_future_traffic) {fail(Status::Incomplete,Reason::Traffic);return;}
    if(seed_.groups.size()<2 || seed_.groups.size()>max_groups || !seed_.limits.candidates || seed_.limits.candidates>max_candidates) {
        fail(Status::Unsupported,Reason::Capacity);return;
    }
    if(!seed_.self.own || !seed_.self.complete || !seed_.self.stable_outgoing ||
       seed_.self.actor>=5 || seed_.self.body_revision!=seed_.stamp.body ||
       seed_.stamp.origin.x!=seed_.self.body.pos.x || seed_.stamp.origin.y!=seed_.self.body.pos.y) {
        fail(Status::Incomplete,Reason::Body);return;
    }
    if(seed_.stamp.epoch<0 || static_cast<std::uint32_t>(seed_.stamp.epoch)!=seed_.self.epoch ||
       seed_.model_start<seed_.self.epoch || seed_.model_start>=seed_.horizon || seed_.horizon>2047) {
        fail(Status::Unsupported,Reason::Dates);return;
    }
    try {
        candidates_.reserve(seed_.limits.candidates);
        cache_=std::make_shared<geometry::Cache>(std::move(seed_.grid),seed_.stamp,seed_.limits.geometry);
        if(cache_->status()==geometry::Status::Unsupported)fail(Status::Unsupported,Reason::Geometry);
    } catch(const std::bad_alloc&) {fail(Status::Unsupported,Reason::Allocation);}
      catch(const std::length_error&) {fail(Status::Unsupported,Reason::Capacity);}
}
inline Progress Runtime::progress() const {
    Progress out;out.units=units_;out.groups_scanned=group_;out.candidates=candidates_.size();
    out.last_work=last_work_;out.model_complete=phase_==Phase::Done;out.rank_complete=rank_complete_;
    for(const auto& c:candidates_) {out.completed+=c.complete;out.ready+=c.ready;out.refuted+=c.refuted;}
    return out;
}
inline Diagnostic Runtime::diagnostic() const {
    Diagnostic out;
    out.phase=static_cast<std::uint32_t>(phase_);
    out.group=static_cast<std::uint32_t>(group_);
    out.family=static_cast<std::uint32_t>(family_);
    out.candidate=candidate_==no_candidate?0xffffffffu:static_cast<std::uint32_t>(candidate_);
    out.scan_cell=static_cast<std::uint32_t>(scan_cell_);
    out.copy_leg=static_cast<std::uint32_t>(copy_leg_);
    out.copy_point=static_cast<std::uint32_t>(copy_point_);
    out.event=static_cast<std::uint32_t>(event_);
    out.repeat=static_cast<std::uint32_t>(repeat_);
    out.prefix_cursor=static_cast<std::uint32_t>(prefix_cursor_);
    out.rank_start=rank_start_;out.rank_horizon=rank_horizon_;
    out.rank_cursor=static_cast<std::uint32_t>(rank_cursor_);
    out.status=static_cast<std::uint32_t>(status_);
    out.reason=static_cast<std::uint32_t>(reason_);
    out.rank_requested=rank_requested_;out.rank_complete=rank_complete_;
    if(candidate_<candidates_.size()) {
        const auto& producer=candidates_[candidate_].producer;
        if(producer) {
            out.producer_status=static_cast<std::uint32_t>(producer->status());
            out.producer_work=static_cast<std::uint64_t>(producer->result().work_used);
        }
    }
    return out;
}
inline Status Runtime::advance(std::size_t units) {
    last_work_=0;
    if(status_!=Status::NeedCompute && status_!=Status::Ready)return status_;
    try {
        while(last_work_<units) {
            const bool rank_work=rank_requested_&&!rank_complete_&&
                ((rank_cursor_<candidates_.size()&&candidates_[rank_cursor_].complete)||phase_==Phase::Done);
            if(phase_==Phase::Done&&!rank_work)break;
            ++last_work_;++units_;
            if(rank_work)rank_unit();else unit();
            if(status_!=Status::NeedCompute&&status_!=Status::Ready)break;
        }
    } catch(const std::bad_alloc&) {fail(Status::Unsupported,Reason::Allocation);}
      catch(const std::length_error&) {fail(Status::Unsupported,Reason::Capacity);}
    return status_;
}
inline Status Runtime::advance_programmes(std::size_t units) {
    last_work_=0;
    if(status_!=Status::NeedCompute&&status_!=Status::Ready)return status_;
    try {
        while(last_work_<units&&phase_!=Phase::Done) {
            // An external index owns this valuation opportunity. A completed
            // producer alone neither exposes a score nor starts dense work.
            if(phase_==Phase::PrefixInit&&candidate_<candidates_.size()&&
               candidates_[candidate_].banks.capacity()==0)break;
            ++last_work_;++units_;
            if(phase_==Phase::Produce) {
                auto& candidate=candidates_[candidate_];const auto state=candidate.producer->advance(1);
                if(state==job::Status::NeedCompute)continue;
                candidate.model_status=state;candidate.model_reason=candidate.producer->result().reason;
                const auto& quote=candidate.producer->result();
                if(state!=job::Status::Ready||!quote.complete||!quote.supported||!quote.disjoint_supported)
                    finish_candidate();
                else {prefix_cursor_=0;phase_=Phase::PrefixInit;}
            } else unit();
            if(status_!=Status::NeedCompute&&status_!=Status::Ready)break;
        }
    } catch(const std::bad_alloc&) {fail(Status::Unsupported,Reason::Allocation);}
      catch(const std::length_error&) {fail(Status::Unsupported,Reason::Capacity);}
    return status_;
}
inline Status Runtime::advance_word_programmes(std::size_t units) {
    std::size_t used{};
    while(used<units) {
        const auto state=advance_programmes(1);
        const auto done=last_work_;used+=done;
        if(input_) {
            const auto clock=production_word_clock::project(input_->self.epoch,
                input_->self.spells,input_->self.cooldown);
            if(clock.status!=production_word_clock::Status::Ready) {
                fail(Status::Unsupported,Reason::Dates);break;
            }
            input_->self.cooldown=clock.cooldown;
        }
        if(!done || (state!=Status::NeedCompute && state!=Status::Ready))break;
    }
    last_work_=used;return status_;
}
inline void Runtime::next_group() {
    if(group_<seed_.groups.size())group_complete_[seed_.groups[group_].canonical]=true;
    ++group_;scan_cell_=family_=0;representatives_={};options_={};
    if(group_>=seed_.groups.size()) {phase_=Phase::Done;status_=Status::Ready;}
    else phase_=Phase::Scan;
}
inline void Runtime::next_family() {
    plan_.reset();input_.reset();candidate_=no_candidate;++family_;
    if(family_>=max_families)next_group();else phase_=Phase::Scan;
}
inline void Runtime::scan_unit() {
    if(!seed_.groups[group_].supported) {next_group();return;}
    if(scan_cell_<cache_->size()) {
        const auto point=cache_->point(static_cast<std::uint32_t>(scan_cell_++));
        const auto index=cache_->index(point);
        const auto distance=cache_->distance(point);
        if(!cache_->closed(index)||distance==geometry::detail::none)return;
        const auto& group=seed_.groups[group_];const auto& metadata=group.metadata;
        std::array<geometry::Point,job::max_members> selected{};
        for(std::size_t i=0;i<metadata.member_count;++i)selected[i]={metadata.members[i].pos.x,metadata.members[i].pos.y};
        const auto members=std::span<const geometry::Point>{selected.data(),metadata.member_count};
        std::array<std::optional<geometry::Kit>,max_families> kits;
        geometry::Kit basic;basic.family=geometry::Family::Basic;basic.basic_range=seed_.self.body.attack_range;
        if(geometry::supports_perch(basic,members,point)==geometry::PerchSupport::Supported)kits[0]=basic;
        for(const auto& spell:seed_.self.spells) {
            if(spell.damage<=0||spell.range<0)continue;
            geometry::Kit kit;kit.basic_range=seed_.self.body.attack_range;kit.cast_range=spell.range;
            std::size_t family=1;
            if(spell.kind==mb::service::SpellKind::damage||spell.kind==mb::service::SpellKind::cc)kit.family=geometry::Family::Targeted;
            else if(spell.kind==mb::service::SpellKind::aoe) {
                family=2;kit.family=geometry::Family::Area;kit.area_radius=spell.radius;
                int left=0,top=0,right=cache_->grid().width-1,bottom=cache_->grid().height-1;
                for(const auto member:members) {
                    left=std::max(left,member.x-spell.radius);right=std::min(right,member.x+spell.radius);
                    top=std::max(top,member.y-spell.radius);bottom=std::min(bottom,member.y+spell.radius);
                }
                if(left>right||top>bottom)continue;
                kit.aim={std::clamp(point.x,left,right),std::clamp(point.y,top,bottom)};
            } else continue;
            if(geometry::supports_perch(kit,members,point)!=geometry::PerchSupport::Supported)continue;
            if(!kits[family]||std::pair{kit.cast_range,kit.area_radius}>std::pair{kits[family]->cast_range,kits[family]->area_radius})kits[family]=kit;
        }
        const auto better=[](const Representative& a,const Representative& b) {
            if(!b.present)return true;
            if(a.distance!=b.distance)return a.distance<b.distance;
            if(a.raw!=b.raw)return a.raw<b.raw;
            return std::pair{a.perch.y,a.perch.x}<std::pair{b.perch.y,b.perch.x};
        };
        for(std::size_t f=0;f<max_families;++f)if(kits[f]) {
            Representative candidate{true,point,*kits[f],distance,cache_->grid().raw_upper[index]};
            auto& options=options_[f];
            for(std::size_t i=0;i<options.size();++i)if(better(candidate,options[i])) {
                for(std::size_t j=options.size()-1;j>i;--j)options[j]=options[j-1];
                options[i]=candidate;break;
            }
        }
        return;
    }
    // Select at most one each Basic/Targeted/Area, then fill absent families
    // with distinct retained geometric alternatives. Never repeat one perch
    // under a different family after a physical rejection.
    if(family_==0&&!representatives_[0].present&&!representatives_[1].present&&!representatives_[2].present) {
        const auto duplicate=[&](geometry::Point p) {
            for(const auto& rep:representatives_)if(rep.present&&rep.perch==p)return true;
            return false;
        };
        for(std::size_t f=0;f<max_families;++f)
            for(const auto& option:options_[f])if(option.present&&!duplicate(option.perch)) {representatives_[f]=option;break;}
        for(auto& rep:representatives_)if(!rep.present) {
            bool found=false;
            for(const auto& options:options_) {
                for(const auto& option:options)if(option.present&&!duplicate(option.perch)) {rep=option;found=true;break;}
                if(found)break;
            }
        }
    }
    if(family_>=max_families) {next_group();return;}
    if(!representatives_[family_].present) {next_family();return;}
    start_plan();
}
inline void Runtime::start_plan() {
    if(candidates_.size()>=seed_.limits.candidates) {fail(Status::Unsupported,Reason::Capacity);return;}
    const auto& group=seed_.groups[group_];const auto& rep=representatives_[family_];
    Candidate candidate;candidate.group_index=group_;candidate.kit=rep.kit;candidate.perch=rep.perch;
    candidates_.push_back(std::move(candidate));candidate_=candidates_.size()-1;
    ids_[group.canonical][family_]=candidate_;
    geometry::Request request;request.stamp=seed_.stamp;request.selected_group=group.geometry_group;
    request.perch=rep.perch;request.kit=rep.kit;request.handoff_inputs=seed_.handoff_inputs;
    request.exclusive_ownership=true;
    // Only same-owner sequential inputs under the declared no-future-traffic
    // scenario. Nothing here establishes a foreign route's disjointness.
    request.disjoint_inputs_supported=seed_.conditional_no_future_traffic;
    request.selected_tiles.reserve(group.metadata.member_count);
    for(std::size_t i=0;i<group.metadata.member_count;++i)
        request.selected_tiles.push_back({group.metadata.members[i].pos.x,group.metadata.members[i].pos.y});
    plan_=std::make_unique<geometry::Plan>(cache_,std::move(request));phase_=Phase::Plan;
}
inline void Runtime::start_input() {
    auto& candidate=candidates_[candidate_];const auto& group=seed_.groups[group_];
    const auto& result=plan_->result();
    candidate.capture=result.capture_tile;candidate.release=result.release_tile;
    candidate.bank=result.bank_stand;candidate.home=result.home;
    input_=std::make_unique<job::Input>();auto& input=*input_;
    input.self=seed_.self;input.start=seed_.model_start;input.horizon=seed_.horizon;
    input.job=group.metadata;
    input.job.group=group.canonical;input.job.perch={candidate.perch.x,candidate.perch.y};
    input.job.width=cache_->grid().width;input.job.height=cache_->grid().height;
    input.job.external_work_raw=input.job.external_capture_raw=0;
    input.job.external_sources_complete=seed_.complete_npc_catalogue;
    input.job.continuation_supported=true;
    input.key={seed_.self.actor,group.canonical,input.job.generation,seed_.self.body_revision,
               input.job.life_revision,input.job.geometry_revision,input.job.word_revision};
    external_group_=external_member_=copy_leg_=copy_point_=0;phase_=Phase::External;
}
inline void Runtime::external_unit() {
    if(external_group_>=seed_.groups.size()) {phase_=Phase::Copy;return;}
    const auto& source=seed_.groups[external_group_];
    if(external_group_==group_||external_member_>=source.metadata.member_count) {
        ++external_group_;external_member_=0;return;
    }
    const auto& member=source.metadata.members[external_member_++];
    auto& input=*input_;const auto& candidate=candidates_[candidate_];
    const geometry::Point position{member.pos.x,member.pos.y};
    const auto add=[&](geometry::Point point,double& value) {
        if(geometry::detail::separation(position,point)>member.range)return true;
        return geometry::detail::add_upper(value,static_cast<double>(member.strength),value);
    };
    if(!add(candidate.perch,input.job.external_work_raw)||!add(candidate.capture,input.job.external_capture_raw)) {
        fail(Status::Unsupported,Reason::Number);return;
    }
}
inline void Runtime::copy_unit() {
    if(copy_leg_>=geometry::leg_count) {
        candidates_[candidate_].producer=std::make_unique<job::Producer>(std::move(*input_));
        input_.reset();plan_.reset();phase_=Phase::Produce;return;
    }
    const auto& source=plan_->result().legs[copy_leg_];auto& destination=leg(input_->job,copy_leg_);
    if(copy_point_<source.points.size()) {
        if(copy_point_==0)destination.points.reserve(source.points.size());
        const auto& step=source.points[copy_point_++];
        destination.points.push_back({{step.point.x,step.point.y},step.raw_upper});return;
    }
    destination.handoff_inputs=source.handoff_inputs;destination.complete=source.complete;
    destination.disjoint_supported=source.disjoint_supported&&seed_.conditional_no_future_traffic;
    ++copy_leg_;copy_point_=0;
}
inline void Runtime::finish_candidate() {
    auto& candidate=candidates_[candidate_];candidate.complete=true;
    next_family();
}
inline void Runtime::prefix_event_unit() {
    auto& candidate=candidates_[candidate_];const auto& quote=candidate.producer->result();
    if(event_>=quote.events.size()) {prefix_cursor_=0;phase_=Phase::PrefixSum;return;}
    const auto& event=quote.events[event_];
    if(!event.count||event.count>candidate.banks.size()) {candidate.model_reason=job::Reason::Capacity;finish_candidate();return;}
    if(repeat_>=event.count) {++event_;repeat_=0;return;}
    const auto offset=static_cast<std::uint64_t>(event.offset)+static_cast<std::uint64_t>(event.period)*repeat_++;
    if(offset>=candidate.banks.size()) {candidate.model_reason=job::Reason::Dates;finish_candidate();return;}
    auto& amount=event.kind==job::EventKind::Bank?candidate.banks[static_cast<std::size_t>(offset)]:candidate.payouts[static_cast<std::size_t>(offset)];
    if(event.gems>std::numeric_limits<std::uint64_t>::max()-amount) {candidate.model_reason=job::Reason::Capacity;finish_candidate();return;}
    amount+=event.gems;
}
inline void Runtime::unit() {
    if(phase_==Phase::Validate) {
        if(group_>=seed_.groups.size()) {group_=member_=0;phase_=Phase::Cache;return;}
        const auto& group=seed_.groups[group_];const auto& metadata=group.metadata;
        if(group.canonical!=group_||group.geometry_group!=static_cast<int>(group.canonical)||
           metadata.group!=group.canonical||!metadata.full_group_known||metadata.member_count==0||metadata.member_count>job::max_members) {
            fail(Status::Incomplete,Reason::Catalogue);return;
        }
        if(member_==0) {
            auto& mutable_metadata=seed_.groups[group_].metadata;
            for(std::size_t i=0;i<geometry::leg_count;++i)if(!leg(mutable_metadata,i).points.empty()) {
                fail(Status::Unsupported,Reason::Shape);return;
            }
            if(group.supported&&!metadata.common_full_generation) {fail(Status::Incomplete,Reason::Dates);return;}
        }
        if(member_<metadata.member_count) {
            const auto& member=metadata.members[member_];const geometry::Point p{member.pos.x,member.pos.y};
            if(!cache_->inside(p)||cache_->grid().camp_group[cache_->index(p)]!=group.geometry_group||
               member.range<0||!std::isfinite(member.strength)||member.strength<0||
               !std::isfinite(metadata.full_hp[member_])||metadata.full_hp[member_]<=0) {
                fail(Status::Incomplete,Reason::Catalogue);return;
            }
            for(std::size_t i=0;i<member_;++i)if(metadata.members[i].pos==member.pos) {
                fail(Status::Unsupported,Reason::Catalogue);return;
            }
            ++member_;return;
        }
        ++group_;member_=0;return;
    }
    if(phase_==Phase::Cache) {
        const auto state=cache_->advance(seed_.stamp,1);
        if(state==geometry::Status::Ready)phase_=Phase::Scan;
        else if(state!=geometry::Status::NeedCompute)fail(state==geometry::Status::Stale?Status::Stale:Status::Unsupported,Reason::Geometry);
        return;
    }
    if(phase_==Phase::Scan) {scan_unit();return;}
    if(phase_==Phase::Plan) {
        const auto state=plan_->advance(seed_.stamp,1);
        if(state==geometry::Status::Ready)start_input();
        else if(state!=geometry::Status::NeedCompute) {
            candidates_[candidate_].model_status=job::Status::Unsupported;
            candidates_[candidate_].model_reason=job::Reason::Geometry;finish_candidate();
        }
        return;
    }
    if(phase_==Phase::External) {external_unit();return;}
    if(phase_==Phase::Copy) {copy_unit();return;}
    if(phase_==Phase::Produce) {
        auto& candidate=candidates_[candidate_];const auto state=candidate.producer->advance(1);
        if(state==job::Status::NeedCompute)return;
        candidate.model_status=state;candidate.model_reason=candidate.producer->result().reason;
        if(state!=job::Status::Ready||!candidate.producer->result().complete||!candidate.producer->result().supported||!candidate.producer->result().disjoint_supported) {finish_candidate();return;}
        const auto span=static_cast<std::size_t>(seed_.horizon-seed_.model_start)+1;
        candidate.banks.reserve(span);candidate.payouts.reserve(span);prefix_cursor_=0;phase_=Phase::PrefixInit;return;
    }
    if(phase_==Phase::PrefixInit) {
        auto& candidate=candidates_[candidate_];const auto& quote=candidate.producer->result();
        const auto span=static_cast<std::size_t>(quote.horizon-quote.start)+1;
        if(candidate.banks.size()<span) {candidate.banks.push_back(0);candidate.payouts.push_back(0);return;}
        candidate.payouts[0]=quote.initial_carried;event_=repeat_=0;phase_=Phase::PrefixEvents;return;
    }
    if(phase_==Phase::PrefixEvents) {prefix_event_unit();return;}
    if(phase_==Phase::PrefixSum) {
        auto& candidate=candidates_[candidate_];
        if(prefix_cursor_>=candidate.banks.size()) {candidate.ready=true;finish_candidate();return;}
        const auto p=prefix_cursor_++;
        if(p) {
            if(candidate.banks[p]>std::numeric_limits<std::uint64_t>::max()-candidate.banks[p-1]||
               candidate.payouts[p]>std::numeric_limits<std::uint64_t>::max()-candidate.payouts[p-1]) {
                candidate.model_reason=job::Reason::Capacity;finish_candidate();return;
            }
            candidate.banks[p]+=candidate.banks[p-1];candidate.payouts[p]+=candidate.payouts[p-1];
        }
        if(candidate.banks[p]>candidate.payouts[p]) {candidate.model_reason=job::Reason::Calendar;finish_candidate();}
        return;
    }
}
inline bool Runtime::candidate_choice(std::size_t id,std::uint32_t start,std::uint32_t horizon,Choice& out) const {
    if((status_!=Status::NeedCompute&&status_!=Status::Ready)||id>=candidates_.size()||start>horizon||horizon>seed_.horizon)return false;
    const auto& candidate=candidates_[id];
    if(!candidate.complete||!candidate.ready||candidate.refuted||!candidate.producer)return false;
    const auto& quote=candidate.producer->result();const auto& group=seed_.groups[candidate.group_index];
    if(start<quote.start||horizon-start>quote.horizon-quote.start||
       (start!=quote.start&&!quote.shift_invariant)||!group.metadata.common_full_generation||
       quote.key.actor!=seed_.self.actor||quote.key.group!=group.canonical||
       quote.key.body_revision!=seed_.self.body_revision||
       quote.key.geometry_revision!=group.metadata.geometry_revision||quote.key.word_revision!=group.metadata.word_revision)return false;
    const auto span=static_cast<std::size_t>(horizon-start);
    if(span>=candidate.banks.size()||span>=candidate.payouts.size()||candidate.banks[span]>candidate.payouts[span])return false;
    out={id,group.canonical,candidate.kit.family,candidate.perch,candidate.capture,candidate.release,candidate.bank,candidate.home,
         {seed_.self.actor,group.canonical,group.metadata.generation,seed_.self.body_revision,group.metadata.life_revision,
          group.metadata.geometry_revision,group.metadata.word_revision},start,horizon,
         candidate.banks[span],candidate.payouts[span]-candidate.banks[span],true};
    return true;
}
inline bool Runtime::preferred(const Choice& a,const Choice& b) {
    if(a.banked_gems_by_H!=b.banked_gems_by_H)return a.banked_gems_by_H>b.banked_gems_by_H;
    // Ties have no absolute-score interpretation; stable canonical IDs avoid
    // query/round-driven churn. Carried gems are deliberately not tie utility.
    if(a.group!=b.group)return a.group<b.group;
    return a.candidate_id<b.candidate_id;
}
inline std::optional<Choice> Runtime::best(std::uint8_t group,std::uint32_t start,std::uint32_t horizon) const {
    if(group>=seed_.groups.size()||!group_complete_[group])return std::nullopt;
    std::optional<Choice> out;
    for(const auto id:ids_[group]) {Choice candidate;if(candidate_choice(id,start,horizon,candidate)&&(!out||preferred(candidate,*out)))out=candidate;}
    return out;
}
inline Status Runtime::rank(std::uint32_t start,std::uint32_t horizon) {
    if(status_!=Status::NeedCompute&&status_!=Status::Ready)return status_;
    if(start>horizon||horizon>seed_.horizon||start<seed_.model_start)return Status::Unsupported;
    if(!rank_requested_||rank_start_!=start||rank_horizon_!=horizon) {
        rank_requested_=true;rank_complete_=false;rank_start_=start;rank_horizon_=horizon;
        rank_cursor_=0;rank_best_.reset();
    }
    return ranking_status(start,horizon);
}
inline Status Runtime::ranking_status(std::uint32_t start,std::uint32_t horizon) const {
    if(status_!=Status::NeedCompute&&status_!=Status::Ready)return status_;
    return rank_requested_&&rank_start_==start&&rank_horizon_==horizon&&rank_complete_?Status::Ready:Status::NeedCompute;
}
inline void Runtime::rank_unit() {
    if(rank_cursor_>=candidates_.size()) {if(phase_==Phase::Done)rank_complete_=true;return;}
    const auto id=rank_cursor_++;Choice current;
    if(!candidate_choice(id,rank_start_,rank_horizon_,current))return;
    if(!rank_best_) {rank_best_=id;return;}
    Choice retained;
    if(!candidate_choice(*rank_best_,rank_start_,rank_horizon_,retained)||preferred(current,retained))rank_best_=id;
}
inline std::optional<Choice> Runtime::best(std::uint32_t start,std::uint32_t horizon) const {
    if(ranking_status(start,horizon)!=Status::Ready||!rank_best_)return std::nullopt;
    Choice out;return candidate_choice(*rank_best_,start,horizon,out)?std::optional<Choice>{out}:std::nullopt;
}
inline assignment::Answer Runtime::quotePair(const assignment::Key& key) const {
    assignment::Answer out;
    if(!assignment::canonical(key)||key.groups[1]>=seed_.groups.size()||key.horizon>seed_.horizon||
       (seed_.self.actor!=key.workers[0]&&seed_.self.actor!=key.workers[1])) {out.status=assignment::Status::Unsupported;return out;}
    const auto first=best(static_cast<std::uint8_t>(key.groups[0]),key.start,key.horizon);
    const auto second=best(static_cast<std::uint8_t>(key.groups[1]),key.start,key.horizon);
    if(!first||!second) {out.status=assignment::Status::Unknown;return out;}
    if(first->banked_gems_by_H>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())||
       second->banked_gems_by_H>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {out.status=assignment::Status::Overflow;return out;}
    // key.generations are caller-declared COMMON SCENARIO identifiers shared
    // by the two menus. Private current Life/generation stays in Choice.key;
    // this scalar difference neither transmits nor fabricates foreign Life.
    assignment::Difference difference;difference.key=key;difference.actor=seed_.self.actor;
    difference.body_revision=seed_.self.body_revision;
    difference.life_revisions={first->key.life_revision,second->key.life_revision};
    difference.geometry_revisions={first->key.geometry_revision,second->key.geometry_revision};
    difference.word_revisions={first->key.word_revision,second->key.word_revision};
    difference.delta=static_cast<std::int64_t>(first->banked_gems_by_H)-static_cast<std::int64_t>(second->banked_gems_by_H);
    difference.complete=difference.supported=difference.conditional=difference.disjoint_supported=true;
    out.status=assignment::Status::Ready;out.difference=difference;return out;
}
inline bool Runtime::select(std::size_t id) {
    if(id>=candidates_.size()||!candidates_[id].ready||candidates_[id].refuted)return false;
    selected_=id;return true;
}
inline bool Runtime::refute(std::size_t id) {
    if(id>=candidates_.size()||candidates_[id].refuted)return false;
    candidates_[id].refuted=true;
    if(selected_&&*selected_==id)selected_.reset();
    if(rank_requested_) {rank_cursor_=0;rank_complete_=false;rank_best_.reset();}
    return true;
}
inline bool Runtime::refresh_group(std::uint8_t canonical,std::uint64_t generation,std::uint64_t life_revision) {
    if(canonical>=seed_.groups.size())return false;
    auto& metadata=seed_.groups[canonical].metadata;
    if(metadata.generation==generation&&metadata.life_revision==life_revision)return false;
    metadata.generation=generation;metadata.life_revision=life_revision;
    for(const auto id:ids_[canonical])if(id<candidates_.size())candidates_[id].refuted=false;
    if(rank_requested_) {rank_cursor_=0;rank_complete_=false;rank_best_.reset();}
    return true;
}

inline Coverage Runtime::coverage() const {
    Coverage out;out.discovered=candidates_.size();out.all_groups_finished=phase_==Phase::Done;
    for(const auto& candidate:candidates_) {
        out.finished+=candidate.complete;
        out.eligible+=candidate.complete&&candidate.ready&&!candidate.refuted&&candidate.producer&&
            candidate.producer->status()==job::Status::Ready;
    }
    return out;
}
inline FrontierPoll Runtime::poll_complete(std::shared_ptr<const Runtime> pin,FrontierCursor& cursor) const {
    if(!pin||pin.get()!=this||status_==Status::Stale)
        return {FrontierState::Stale,{}};
    const std::weak_ptr<const Runtime> identity=pin;
    if(!cursor.bound) {cursor.runtime=identity;cursor.bound=true;}
    else if(cursor.runtime.owner_before(identity)||identity.owner_before(cursor.runtime))
        return {FrontierState::Stale,{}};
    if(cursor.next>=candidates_.size())
        return {phase_==Phase::Done||(status_!=Status::NeedCompute&&status_!=Status::Ready)?
            FrontierState::End:FrontierState::NeedCompute,{}};
    const auto id=cursor.next;const auto& candidate=candidates_[id];
    if(!candidate.complete)return {status_==Status::NeedCompute||status_==Status::Ready?
        FrontierState::NeedCompute:FrontierState::End,{}};
    ++cursor.next;
    if(!candidate.ready||candidate.refuted||!candidate.producer||
       candidate.producer->status()!=job::Status::Ready)
        return {FrontierState::Skipped,{}};
    const auto& quote=candidate.producer->result();
    if(!quote.complete||!quote.supported||!quote.disjoint_supported||
       candidate.banks.empty()||candidate.banks.size()!=candidate.payouts.size())
        return {FrontierState::Skipped,{}};
    return {FrontierState::Candidate,Handle{identity,id,quote.key}};
}
inline FrontierPoll Runtime::poll_programme(std::shared_ptr<const Runtime> pin,FrontierCursor& cursor) const {
    if(!pin||pin.get()!=this||status_==Status::Stale)return {FrontierState::Stale,{}};
    const std::weak_ptr<const Runtime> identity=pin;
    if(!cursor.bound) {cursor.runtime=identity;cursor.bound=true;}
    else if(cursor.runtime.owner_before(identity)||identity.owner_before(cursor.runtime))return {FrontierState::Stale,{}};
    if(cursor.next>=candidates_.size())return {phase_==Phase::Done||
        (status_!=Status::NeedCompute&&status_!=Status::Ready)?FrontierState::End:FrontierState::NeedCompute,{}};
    const auto id=cursor.next;const auto& candidate=candidates_[id];
    if(candidate.producer&&candidate.producer->status()==job::Status::Ready) {
        const auto& quote=candidate.producer->result();
        if(quote.complete&&quote.supported&&quote.disjoint_supported&&!candidate.refuted) {
            ++cursor.next;return {FrontierState::Candidate,Handle{identity,id,quote.key}};
        }
    }
    if(candidate.complete) {++cursor.next;return {FrontierState::Skipped,{}};}
    return {status_==Status::NeedCompute||status_==Status::Ready?FrontierState::NeedCompute:FrontierState::End,{}};
}
inline std::optional<Choice> Runtime::retained_choice(const Handle& handle,std::uint32_t start,
    std::uint32_t horizon,const job::Key& current) const {
    const auto pin=handle.runtime.lock();
    if(!pin||pin.get()!=this||status_==Status::Stale||handle.candidate_id>=candidates_.size()||
       start>horizon||horizon>seed_.horizon)return std::nullopt;
    const auto& candidate=candidates_[handle.candidate_id];
    if(candidate.refuted||!candidate.producer||candidate.producer->status()!=job::Status::Ready)return std::nullopt;
    const auto& quote=candidate.producer->result();const auto& original=candidate.producer->programme();
    if(quote.key!=handle.programme_key||original.key!=handle.programme_key||!quote.complete||
       !quote.supported||!quote.disjoint_supported||!original.job.common_full_generation||
       start<quote.start||horizon-start>quote.horizon-quote.start||
       (start!=quote.start&&!quote.shift_invariant)||current.actor!=quote.key.actor||
       current.group!=quote.key.group||current.body_revision!=quote.key.body_revision||
       current.geometry_revision!=quote.key.geometry_revision||current.word_revision!=quote.key.word_revision)
        return std::nullopt;
    const auto& group=seed_.groups[candidate.group_index];
    return Choice{handle.candidate_id,group.canonical,candidate.kit.family,candidate.perch,candidate.capture,
        candidate.release,candidate.bank,candidate.home,quote.key,start,horizon,0,0,true};
}
inline bool Runtime::resume_dense(const Handle& handle) {
    if(!retained_choice(handle,seed_.model_start,seed_.horizon,handle.programme_key)||
       candidate_!=handle.candidate_id||phase_!=Phase::PrefixInit)return false;
    auto& candidate=candidates_[candidate_];
    if(candidate.banks.capacity())return true;
    try {
        const auto& quote=candidate.producer->result();const auto span=static_cast<std::size_t>(quote.horizon-quote.start)+1;
        candidate.banks.reserve(span);candidate.payouts.reserve(span);return true;
    } catch(const std::bad_alloc&) {fail(Status::Unsupported,Reason::Allocation);}
      catch(const std::length_error&) {fail(Status::Unsupported,Reason::Capacity);}
    return false;
}
inline bool Runtime::release_dense(const Handle& handle) {
    const auto pin=handle.runtime.lock();
    if(!pin||pin.get()!=this||handle.candidate_id>=candidates_.size()||status_==Status::Stale)return false;
    auto& candidate=candidates_[handle.candidate_id];
    if(candidate.complete)return true;
    if(candidate_!=handle.candidate_id||(phase_!=Phase::PrefixInit&&phase_!=Phase::PrefixEvents&&phase_!=Phase::PrefixSum))return false;
    // No old Ready API may observe a partially accumulated dense table.
    candidate.ready=false;candidate.banks.clear();candidate.payouts.clear();finish_candidate();return true;
}
inline bool Runtime::dense_valuation_ready(const Handle& handle) const {
    if(handle.candidate_id>=candidates_.size())return false;
    const auto& candidate=candidates_[handle.candidate_id];if(!candidate.producer)return false;
    const auto& quote=candidate.producer->result();
    const auto full=lookup_exact(handle,quote.start,quote.horizon,handle.programme_key);
    return full&&full->banked_gems_by_H==quote.banked_gems_by_H&&full->carried_gems==quote.carried_gems;
}
inline std::optional<Choice> Runtime::lookup_dense_qualified(const Handle& handle,std::uint32_t start,
    std::uint32_t horizon,const job::Key& current,const PhysicalRefutation* blocked) const {
    return dense_valuation_ready(handle)?lookup_exact(handle,start,horizon,current,blocked):std::nullopt;
}
inline std::optional<Choice> Runtime::lookup_exact(const Handle& handle,std::uint32_t start,std::uint32_t horizon,
    const job::Key& current,const PhysicalRefutation* blocked) const {
    const auto pin=handle.runtime.lock();
    // A later optional capacity/allocation failure does not erase an already
    // complete independent forecast. Explicit Stale still invalidates it.
    if(!pin||pin.get()!=this||status_==Status::Stale||
       handle.candidate_id>=candidates_.size()||start>horizon||horizon>seed_.horizon)return std::nullopt;
    const auto& candidate=candidates_[handle.candidate_id];
    if(!candidate.complete||!candidate.ready||candidate.refuted||!candidate.producer)return std::nullopt;
    const auto& producer=*candidate.producer;const auto& quote=producer.result();
    const auto& original=producer.programme();
    // Keep immutable programme provenance. Actual Life/generation may enable
    // this same full-generation counterfactual, but never relabel its totals.
    if(quote.key!=handle.programme_key||original.key!=handle.programme_key||
       quote.status!=job::Status::Ready||!quote.complete||!quote.supported||!quote.disjoint_supported||
       !original.job.common_full_generation||start<quote.start||
       horizon-start>quote.horizon-quote.start||(start!=quote.start&&!quote.shift_invariant)||
       current.actor!=quote.key.actor||current.group!=quote.key.group||
       current.body_revision!=quote.key.body_revision||current.geometry_revision!=quote.key.geometry_revision||
       current.word_revision!=quote.key.word_revision)return std::nullopt;
    const auto span=static_cast<std::size_t>(horizon-start);
    if(span>=candidate.banks.size()||span>=candidate.payouts.size()||
       candidate.banks[span]>candidate.payouts[span])return std::nullopt;
    const auto& group=seed_.groups[candidate.group_index];
    Choice out{handle.candidate_id,group.canonical,candidate.kit.family,candidate.perch,candidate.capture,
        candidate.release,candidate.bank,candidate.home,quote.key,start,horizon,
        candidate.banks[span],candidate.payouts[span]-candidate.banks[span],true};
    if(blocked&&blocked->matches(out))return std::nullopt;
    return out;
}
inline std::optional<ProgrammeLease> Runtime::programme(const Handle& handle,std::uint32_t start,std::uint32_t horizon,
    const job::Key& current,const PhysicalRefutation* blocked) const {
    const auto choice=lookup_exact(handle,start,horizon,current,blocked);
    if(!choice)return std::nullopt;
    const auto& candidate=candidates_[handle.candidate_id];
    ProgrammeLease lease;lease.pin_=handle.runtime.lock();lease.handle_=handle;lease.choice_=*choice;
    lease.input_=&candidate.producer->programme();lease.forecast_=&candidate.producer->result();
    lease.banks_=candidate.banks;lease.payouts_=candidate.payouts;return lease;
}
inline assignment::Answer Runtime::quote_pair_exact(const Handle& first,const Handle& second,const assignment::Key& key,
    const job::Key& current_first,const job::Key& current_second,
    const PhysicalRefutation* blocked_first,const PhysicalRefutation* blocked_second) const {
    assignment::Answer out;
    if(!assignment::canonical(key)||key.groups[1]>=seed_.groups.size()||key.horizon>seed_.horizon||
       (seed_.self.actor!=key.workers[0]&&seed_.self.actor!=key.workers[1]))
        {out.status=assignment::Status::Unsupported;return out;}
    const auto a=lookup_exact(first,key.start,key.horizon,current_first,blocked_first);
    const auto b=lookup_exact(second,key.start,key.horizon,current_second,blocked_second);
    if(!a||!b||a->group!=key.groups[0]||b->group!=key.groups[1])return out;
    if(a->banked_gems_by_H>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())||
       b->banked_gems_by_H>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        {out.status=assignment::Status::Overflow;return out;}
    assignment::Difference difference;difference.key=key;difference.actor=seed_.self.actor;
    difference.body_revision=seed_.self.body_revision;
    difference.life_revisions={a->key.life_revision,b->key.life_revision};
    difference.geometry_revisions={a->key.geometry_revision,b->key.geometry_revision};
    difference.word_revisions={a->key.word_revision,b->key.word_revision};
    difference.enablers={static_cast<std::uint64_t>(candidates_[first.candidate_id].producer->result().enabler),
        static_cast<std::uint64_t>(candidates_[second.candidate_id].producer->result().enabler)};
    difference.delta=static_cast<std::int64_t>(a->banked_gems_by_H)-static_cast<std::int64_t>(b->banked_gems_by_H);
    difference.complete=difference.supported=difference.conditional=difference.disjoint_supported=true;
    out.status=assignment::Status::Ready;out.difference=difference;return out;
}

inline std::shared_ptr<IndexedProgramme> IndexedProgramme::open(std::shared_ptr<Runtime> pin,
    const Handle& handle,std::size_t segment_limit) {
    if(!pin)return {};
    const auto identity=handle.runtime.lock();if(!identity||identity.get()!=pin.get())return {};
    const auto& key=handle.programme_key;
    if(handle.candidate_id>=pin->candidates_.size())return {};
    const auto& candidate=pin->candidates_[handle.candidate_id];
    if(!candidate.producer)return {};
    const auto& quote=candidate.producer->result();
    if(!pin->retained_choice(handle,quote.start,quote.horizon,key))return {};
    try {
        auto out=std::shared_ptr<IndexedProgramme>(new IndexedProgramme);
        out->pin_=std::move(pin);out->handle_=handle;out->input_=&candidate.producer->programme();out->forecast_=&quote;
        production_calendar::Source source{quote.events,{quote.start,quote.horizon,quote.initial_carried},out->pin_,true,segment_limit};
        out->index_=std::make_unique<production_calendar::Index>(std::move(source));return out;
    } catch(const std::bad_alloc&) {return {};}
      catch(const std::length_error&) {return {};}
}
inline production_calendar::Status IndexedProgramme::status() const {
    const auto state=index_->status();
    if(state==production_calendar::Status::Ready&&(!totals_checked_||!totals_match_))
        return totals_checked_?production_calendar::Status::DenseFallback:production_calendar::Status::NeedCompute;
    return state;
}
inline production_calendar::Status IndexedProgramme::advance(std::size_t units) {
    const auto state=index_->advance(units);
    if(state==production_calendar::Status::Ready&&!totals_checked_) {
        const auto value=index_->query(forecast_->horizon-forecast_->start);
        totals_checked_=true;totals_match_=value.status==production_calendar::Status::Ready&&
            value.banked==forecast_->banked_gems_by_H&&value.carried==forecast_->carried_gems;
    }
    return status();
}
inline bool IndexedProgramme::valuation_ready() const {
    if(status()==production_calendar::Status::Ready)return true;
    return pin_->dense_valuation_ready(handle_);
}
inline std::optional<Choice> IndexedProgramme::lookup_exact(std::uint32_t start,std::uint32_t horizon,
    const job::Key& current,const PhysicalRefutation* blocked) const {
    if(status()!=production_calendar::Status::Ready)return pin_->lookup_dense_qualified(handle_,start,horizon,current,blocked);
    auto choice=pin_->retained_choice(handle_,start,horizon,current);if(!choice)return std::nullopt;
    const auto value=index_->query(horizon-start);if(value.status!=production_calendar::Status::Ready)return std::nullopt;
    choice->banked_gems_by_H=value.banked;choice->carried_gems=value.carried;
    if(blocked&&blocked->matches(*choice))return std::nullopt;
    return choice;
}
inline bool IndexedProgramme::release_dense() {
    return status()==production_calendar::Status::Ready&&pin_->release_dense(handle_);
}

} // namespace tt::assignment_owner
