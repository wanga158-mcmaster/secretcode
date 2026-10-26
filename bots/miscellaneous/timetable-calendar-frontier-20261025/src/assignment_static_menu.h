#pragma once
#include "config.h"
#include "assignment_static_reach.h"
// Persistent, SDK-free nominations over one sealed static OBJECT. A cached
// target retains its original stamp and omits Initial explicitly. It supplies
// no fresh body, Life, lock, occupancy or command authority; the fresh binder
// alone traces a NEW Initial from the actual closed origin.
#include "assignment_static_geometry.h"
#include "production_job.h"
#include <array>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace tt::assignment_static_menu {
namespace ag=assignment_geometry;
namespace sg=assignment_static_geometry;
namespace pj=production_job;

struct Limits {std::size_t entries{6},families{96},records{96};};
inline constexpr std::size_t max_entries=96,max_records=1024,remembered_failures=8,remembered_supports=16;
struct Request {
    int canonical_group{-1};pj::Snapshot self;ag::Stamp nomination;
    std::array<std::uint32_t,ag::leg_count> handoff_inputs{};
    std::uint64_t capture_policy{1};
};
enum class RequestStatus {Queued,Reused,Deferred,Invalid,Stale,Unsupported};
struct RequestResult {
    RequestStatus status{RequestStatus::Invalid};ag::Reason reason{ag::Reason::None};
    std::size_t queued{},reused{},deferred{};
};
enum class Status {NeedCompute,Ready,Complete,Unsupported};
enum class EntryState {Scanning,Building,Ready,Exhausted,Retired};
// This type admits COMPLETED semantic results only. Funding, Stale and
// NeedObservation are not evidence against a candidate under any support.
// The caller's nonzero support hash binds the complete binding/Producer domain
// (body, actual origin, Life, model/horizon/knobs), never merely fuel or date.
enum class CompletedVerdict {Unsupported,Refuted};
enum class DeclineStatus {Recorded,AlreadyRecorded,Deferred,Exhausted,Invalid,UnknownPublication};
struct DeclineResult {
    DeclineStatus status{DeclineStatus::Invalid};ag::Reason reason{ag::Reason::None};
    bool verdict_recorded{},resumed{};
};
struct SupportVerdict {std::uint64_t semantic_support{};CompletedVerdict verdict{CompletedVerdict::Unsupported};};
enum class RetireStatus {Retired,AlreadyRetired,Pinned,Incumbent,NoCompletedVerdict,Deferred,Invalid,UnknownPublication};
struct RetireResult {RetireStatus status{RetireStatus::Invalid};ag::Reason reason{ag::Reason::None};};
enum class ReconstructStatus {Queued,AlreadyResident,DeclinedForSupport,Deferred,Invalid,UnknownPublication};
struct ReconstructResult {ReconstructStatus status{ReconstructStatus::Invalid};ag::Reason reason{ag::Reason::None};bool owed{};};
// Spell slot, damage magnitude, cooldown and allowance are not geometry.
// Normalized alternatives retain only positive-damage geometry eligibility.
struct SpellGeometry {
    int range{},radius{};
    friend bool operator==(const SpellGeometry&,const SpellGeometry&)=default;
};
struct KitGeometry {
    ag::Family family{ag::Family::Basic};int basic_range{};
    std::array<SpellGeometry,pj::slots> spells{};std::size_t count{};
    friend bool operator==(const KitGeometry&,const KitGeometry&)=default;
};
struct Failure {
    ag::Point perch;ag::Status status{ag::Status::Unsupported};ag::Reason reason{ag::Reason::None};
};
struct Progress {
    std::uint64_t units{},scanned_cells{},builder_units{},started{},failed{},published{};
};
struct Entry {
    int canonical_group{-1};KitGeometry geometry;
    std::array<std::uint32_t,ag::leg_count> handoff_inputs{};
    // Publication identifies one immutable static recipe. Reconstructing its
    // released storage keeps that ID and provenance; residency alone advances.
    std::uint64_t capture_policy{1},publication{},publication_id{},family_id{},residency{};ag::Stamp nomination;
    ag::Point perch;ag::Kit target_kit;
    EntryState state{EntryState::Scanning};ag::Reason reason{ag::Reason::None};
    std::shared_ptr<const sg::TargetGeometry> target;
    Progress progress;std::array<Failure,remembered_failures> failures{};
    std::size_t failure_count{},next_failure{};
    std::array<SupportVerdict,remembered_supports> declines{};std::size_t decline_count{};
};
struct FamilyHistory {
    int canonical_group{-1};KitGeometry geometry;
    std::array<std::uint32_t,ag::leg_count> handoff_inputs{};
    std::uint64_t capture_policy{1},family_id{};ag::Stamp nomination;
    EntryState state{EntryState::Scanning};ag::Reason reason{ag::Reason::None};
    std::size_t cursor{},start{};bool deferred{};
    Progress progress;std::array<Failure,remembered_failures> failures{};
    std::size_t failure_count{},next_failure{};
};
namespace detail {
// Actor-local, single-threaded identity allocator. IDs cannot alias a previous
// Menu after generation replacement. Exhaustion defers instead of wrapping.
inline std::uint64_t next_identity() {
    static std::uint64_t value{};
    if(value==std::numeric_limits<std::uint64_t>::max())return 0;
    return ++value;
}
struct GeometrySet {
    std::array<std::optional<KitGeometry>,3> families;
    ag::Reason reason{ag::Reason::None};
};
inline GeometrySet geometry(const pj::Snapshot& self) {
    GeometrySet out;
    if(self.body.attack_range<0){out.reason=ag::Reason::Kit;return out;}
    constexpr std::array<ag::Family,3> order{ag::Family::Basic,ag::Family::Targeted,ag::Family::Area};
    for(std::size_t f=0;f<3;++f) {
        KitGeometry value;value.family=order[f];value.basic_range=self.body.attack_range;
        if(f==0){out.families[f]=value;continue;}
        for(const auto& spell:self.spells) {
            const bool relevant=f==1?(spell.kind==mb::service::SpellKind::damage||spell.kind==mb::service::SpellKind::cc)
                :spell.kind==mb::service::SpellKind::aoe;
            if(!relevant)continue;
            if(!std::isfinite(spell.damage)||spell.damage<0||spell.range<0||(f==2&&spell.radius<0))
                {out.reason=ag::Reason::Kit;return out;}
            if(spell.damage==0)continue;
            SpellGeometry next{spell.range,f==2?spell.radius:0};
            bool duplicate=false;for(std::size_t i=0;i<value.count;++i)if(value.spells[i]==next)duplicate=true;
            if(!duplicate)value.spells[value.count++]=next;
        }
        if(value.count==2&&std::pair{value.spells[1].range,value.spells[1].radius}<
            std::pair{value.spells[0].range,value.spells[0].radius})std::swap(value.spells[0],value.spells[1]);
        if(value.count)out.families[f]=value;
    }
    return out;
}
inline std::optional<ag::Kit> kit(const KitGeometry& geometry,
    const sg::StaticGeometry& data,int group,ag::Point perch) {
    const auto& members=data.catalogue()[static_cast<std::size_t>(group)].members;
    const auto supported=[&](const ag::Kit& value) {
        for(const auto& member:members)if(!ag::detail::supports_member(value,perch,member.pos))return false;
        return true;
    };
    ag::Kit value;value.family=geometry.family;value.basic_range=geometry.basic_range;
    if(geometry.family==ag::Family::Basic)return supported(value)?std::optional<ag::Kit>{value}:std::nullopt;
    std::optional<ag::Kit> best;
    for(std::size_t i=0;i<geometry.count;++i) {
        value.cast_range=geometry.spells[i].range;value.area_radius=geometry.spells[i].radius;
        if(geometry.family==ag::Family::Area) {
            // The Chebyshev intersection is a rectangle. Clamping the perch
            // into it finds a castable center iff this rectangle is reachable.
            std::int64_t left=0,top=0,right=data.grid().width-1,bottom=data.grid().height-1;
            for(const auto& member:members) {
                left=std::max(left,static_cast<std::int64_t>(member.pos.x)-value.area_radius);
                right=std::min(right,static_cast<std::int64_t>(member.pos.x)+value.area_radius);
                top=std::max(top,static_cast<std::int64_t>(member.pos.y)-value.area_radius);
                bottom=std::min(bottom,static_cast<std::int64_t>(member.pos.y)+value.area_radius);
            }
            if(left>right||top>bottom)continue;
            value.aim={static_cast<int>(std::clamp<std::int64_t>(perch.x,left,right)),
                static_cast<int>(std::clamp<std::int64_t>(perch.y,top,bottom))};
        }
        if(supported(value)&&(!best||std::pair{value.cast_range,value.area_radius}>
            std::pair{best->cast_range,best->area_radius}))best=value;
    }
    return best;
}
}

class Menu {
public:
    explicit Menu(std::shared_ptr<const sg::StaticGeometry> data,Limits limits={});
    Menu(const Menu&)=delete;Menu& operator=(const Menu&)=delete;
    Menu(Menu&&)=delete;Menu& operator=(Menu&&)=delete;
    RequestResult request(const Request&);
    Status advance(std::size_t units);
    Status status() const;
    ag::Reason reason() const{return reason_;}
    bool complete() const;
    std::size_t last_work() const{return last_work_;}
    const Progress& progress() const{return progress_;}
    const std::shared_ptr<const sg::StaticGeometry>& static_geometry() const{return data_;}
    std::size_t ready_count() const;
    // Borrowed rows last until the next non-const Menu call. Copy target's
    // shared_ptr to pin it across requests, retirement or generation replacement.
    std::span<const Entry> entries() const{return entries_;}
    std::span<const FamilyHistory> families() const{return families_;}
    bool deferred() const;
    bool eligible(std::uint64_t publication_id,std::uint64_t semantic_support) const;
    DeclineResult decline(std::uint64_t publication_id,std::uint64_t semantic_support,CompletedVerdict);
    // Caller names the incumbent (zero means there is none). Only an exact
    // completed current-support decline permits pressure retirement; pins win.
    RetireResult retire(std::uint64_t publication_id,std::uint64_t current_support,std::uint64_t incumbent_publication_id);
    // A new storage object for the same immutable recipe/original provenance.
    // Repeated same-support refutations remain recorded; no stamp is refreshed.
    ReconstructResult reconstruct(std::uint64_t retired_publication_id,std::uint64_t new_support);
    std::size_t retired_count() const{return retired_.size();}
    const Entry* retired_at(std::size_t i) const{return i<retired_.size()?retired_[i].get():nullptr;}
    std::shared_ptr<const sg::TargetGeometry> first_ready(int canonical_group,const pj::Snapshot& currentkit) const;
    std::shared_ptr<const sg::TargetGeometry> first_ready(int canonical_group,const pj::Snapshot& currentkit,
        std::uint64_t semantic_support) const;
private:
    struct Work {
        std::unique_ptr<sg::TargetBuilder> builder;
        std::optional<std::size_t> output;
        ag::Point perch;ag::Kit kit;bool seeking{true},paused{};
        std::uint64_t reconstruct_id{},pending_reconstruction{};
    };
    std::shared_ptr<const sg::StaticGeometry> data_;Limits limits_;
    std::vector<Entry> entries_;std::vector<FamilyHistory> families_;std::vector<Work> work_;
    std::vector<std::unique_ptr<Entry>> retired_;
    ag::Reason reason_{ag::Reason::None};bool valid_{},metadata_owed_{};
    std::size_t next_{},last_work_{};Progress progress_;
    std::optional<std::size_t> free_slot() const;
    bool same(const FamilyHistory&,const Request&,const KitGeometry&) const;
    bool active(std::size_t i) const{return work_[i].seeking&&!work_[i].paused&&
        (work_[i].output.has_value()||free_slot().has_value());}
    bool reserve_output(std::size_t);
    void snapshot(std::size_t);
    bool family_eligible(std::uint64_t,std::uint64_t) const;
    std::optional<std::size_t> retired_index(std::uint64_t) const;
    bool declined(const Entry&,std::uint64_t) const;
    void after_publication(std::size_t);
    void start_builder(std::size_t);
    void unit(std::size_t);
    void failed(std::size_t,ag::Status,ag::Reason);
    bool publishable(std::size_t,const std::shared_ptr<const sg::TargetGeometry>&) const;
    std::shared_ptr<const sg::TargetGeometry> select(int,const pj::Snapshot&,std::uint64_t,bool) const;
};

inline Menu::Menu(std::shared_ptr<const sg::StaticGeometry> data,Limits limits):data_(std::move(data)),limits_(limits) {
    if(!data_){reason_=ag::Reason::Ownership;return;}
    if(!limits.entries||limits.entries>max_entries||!limits.families||limits.families>max_entries||
        !limits.records||limits.records>max_records)
        {reason_=ag::Reason::Capacity;return;}
    try{entries_.reserve(limits.entries);families_.reserve(limits.families);work_.reserve(limits.families);
        retired_.reserve(limits.records);valid_=true;}
    catch(const std::bad_alloc&){reason_=ag::Reason::Allocation;}
    catch(const std::length_error&){reason_=ag::Reason::Capacity;}
}
inline std::size_t Menu::ready_count() const {
    std::size_t count=0;for(const auto& entry:entries_)if(entry.state==EntryState::Ready)++count;return count;
}
inline bool Menu::complete() const {
    // Storage/history exhaustion is incomplete refinement, not family failure.
    if(metadata_owed_)return false;
    for(const auto& work:work_)if(work.seeking)return false;return true;
}
inline bool Menu::deferred() const {
    if(metadata_owed_)return true;
    for(const auto& family:families_)if(family.deferred)return true;return false;
}
inline Status Menu::status() const {
    if(!valid_)return Status::Unsupported;
    if(ready_count())return Status::Ready;
    return complete()?Status::Complete:Status::NeedCompute;
}
inline bool Menu::same(const FamilyHistory& entry,const Request& request,const KitGeometry& geometry) const {
    return entry.canonical_group==request.canonical_group&&entry.geometry==geometry&&
        entry.handoff_inputs==request.handoff_inputs&&entry.capture_policy==request.capture_policy;
}
inline std::optional<std::size_t> Menu::free_slot() const {
    if(entries_.size()<limits_.entries)return entries_.size();
    for(std::size_t i=0;i<entries_.size();++i) {
        const auto& entry=entries_[i];
        if(entry.state==EntryState::Exhausted&&!entry.target)return i;
    }
    return {};
}
inline void Menu::snapshot(std::size_t i) {
    if(!work_[i].output)return;
    auto& entry=entries_[*work_[i].output];const auto& family=families_[i];
    entry.state=family.state;entry.reason=family.reason;entry.progress=family.progress;
    entry.failures=family.failures;entry.failure_count=family.failure_count;entry.next_failure=family.next_failure;
}
inline bool Menu::reserve_output(std::size_t i) {
    if(work_[i].output)return true;
    const auto destination=free_slot();auto& family=families_[i];
    if(!destination){family.deferred=true;return false;}
    Entry entry;
    if(work_[i].reconstruct_id) {
        const auto at=retired_index(work_[i].reconstruct_id);
        if(!at){family.deferred=true;return false;}
        entry=*retired_[*at];entry.target.reset();entry.state=EntryState::Scanning;
    } else {
        entry.canonical_group=family.canonical_group;entry.geometry=family.geometry;
        entry.handoff_inputs=family.handoff_inputs;entry.capture_policy=family.capture_policy;
        entry.family_id=family.family_id;entry.nomination=family.nomination;
    }
    if(*destination==entries_.size())entries_.push_back(std::move(entry));else entries_[*destination]=std::move(entry);
    work_[i].output=*destination;family.deferred=false;snapshot(i);return true;
}
inline RequestResult Menu::request(const Request& request) {
    RequestResult result;
    if(!valid_){result.status=RequestStatus::Unsupported;result.reason=reason_;return result;}
    if(request.capture_policy!=1){result.status=RequestStatus::Unsupported;result.reason=ag::Reason::Changed;return result;}
    if(request.nomination.geometry!=data_->key().layout||request.nomination.potential!=data_->key().potential)
        {result.status=RequestStatus::Stale;result.reason=ag::Reason::Changed;return result;}
    if(request.nomination.epoch<0){result.reason=ag::Reason::Epoch;return result;}
    if(request.canonical_group<0||static_cast<std::size_t>(request.canonical_group)>=data_->catalogue().size())
        {result.reason=ag::Reason::Group;return result;}
    if(!data_->inside(request.nomination.origin))
        {result.reason=ag::Reason::Origin;return result;}
    const auto geometry=detail::geometry(request.self);
    if(geometry.reason!=ag::Reason::None){result.reason=geometry.reason;return result;}
    for(const auto& family:geometry.families)if(family) {
        bool found=false;
        for(std::size_t i=0;i<families_.size();++i)if(same(families_[i],request,*family)) {
            ++result.reused;found=true;
            if(work_[i].seeking&&(work_[i].paused||!reserve_output(i))) {
                ++result.deferred;if(work_[i].paused)result.reason=families_[i].reason;
            }
            break;
        }
        if(found)continue;
        if(families_.size()==limits_.families){metadata_owed_=true;++result.deferred;continue;}
        const auto identity=detail::next_identity();
        if(!identity){metadata_owed_=true;++result.deferred;continue;}
        try {
            FamilyHistory entry;entry.canonical_group=request.canonical_group;entry.geometry=*family;
            entry.handoff_inputs=request.handoff_inputs;entry.capture_policy=request.capture_policy;
            entry.nomination=request.nomination;entry.start=data_->index(request.nomination.origin);entry.family_id=identity;
            families_.push_back(std::move(entry));work_.emplace_back();
            if(reserve_output(families_.size()-1))++result.queued;else ++result.deferred;
        } catch(const std::bad_alloc&){metadata_owed_=true;result.reason=ag::Reason::Allocation;++result.deferred;}
          catch(const std::length_error&){metadata_owed_=true;result.reason=ag::Reason::Capacity;++result.deferred;}
    }
    result.status=result.deferred?RequestStatus::Deferred:result.queued?RequestStatus::Queued:RequestStatus::Reused;
    if(result.deferred&&result.reason==ag::Reason::None)result.reason=ag::Reason::Capacity;
    return result;
}
inline std::shared_ptr<const sg::TargetGeometry> Menu::first_ready(int group,const pj::Snapshot& currentkit) const {
    return select(group,currentkit,0,false);
}
inline std::shared_ptr<const sg::TargetGeometry> Menu::first_ready(int group,const pj::Snapshot& currentkit,
    std::uint64_t semantic_support) const {
    return select(group,currentkit,semantic_support,true);
}
inline std::shared_ptr<const sg::TargetGeometry> Menu::select(int group,const pj::Snapshot& currentkit,
    std::uint64_t semantic_support,bool scoped) const {
    if(scoped&&!semantic_support)return {};
    const auto current=detail::geometry(currentkit);if(current.reason!=ag::Reason::None)return {};
    const Entry* first=nullptr;
    for(const auto& entry:entries_)if(entry.canonical_group==group&&entry.state==EntryState::Ready) {
        bool matches=false;for(const auto& family:current.families)if(family&&*family==entry.geometry)matches=true;
        if(matches&&(!scoped||eligible(entry.publication_id,semantic_support))&&
            (!first||entry.publication<first->publication))first=&entry;
    }
    return first?first->target:nullptr;
}
inline bool Menu::eligible(std::uint64_t publication_id,std::uint64_t semantic_support) const {
    if(!publication_id||!semantic_support)return false;
    for(const auto& entry:entries_)if(entry.publication_id==publication_id&&entry.state==EntryState::Ready&&entry.target) {
        return !declined(entry,semantic_support);
    }
    return false;
}
inline bool Menu::declined(const Entry& entry,std::uint64_t support) const {
    for(std::size_t n=0;n<entry.decline_count;++n)if(entry.declines[n].semantic_support==support)return true;
    return false;
}
inline std::optional<std::size_t> Menu::retired_index(std::uint64_t id) const {
    for(std::size_t i=0;i<retired_.size();++i)if(retired_[i]->publication_id==id)return i;
    return {};
}
inline bool Menu::family_eligible(std::uint64_t family,std::uint64_t support) const {
    for(const auto& entry:entries_)if(entry.family_id==family&&eligible(entry.publication_id,support))return true;
    return false;
}
inline DeclineResult Menu::decline(std::uint64_t publication_id,std::uint64_t semantic_support,CompletedVerdict verdict) {
    DeclineResult result;
    if(!valid_||!publication_id||!semantic_support||
        (verdict!=CompletedVerdict::Unsupported&&verdict!=CompletedVerdict::Refuted))return result;
    std::optional<std::size_t> at;
    for(std::size_t i=0;i<entries_.size();++i)if(entries_[i].publication_id==publication_id&&
        entries_[i].state==EntryState::Ready&&entries_[i].target){at=i;break;}
    if(!at){
        const auto old=retired_index(publication_id);
        if(old&&declined(*retired_[*old],semantic_support))
            {result.status=DeclineStatus::AlreadyRecorded;result.verdict_recorded=true;return result;}
        result.status=DeclineStatus::UnknownPublication;return result;
    }
    auto& entry=entries_[*at];
    for(std::size_t n=0;n<entry.decline_count;++n)if(entry.declines[n].semantic_support==semantic_support)
        {result.status=DeclineStatus::AlreadyRecorded;result.verdict_recorded=true;return result;}
    if(entry.decline_count==remembered_supports){metadata_owed_=true;result.status=DeclineStatus::Deferred;result.reason=ag::Reason::Capacity;return result;}
    entry.declines[entry.decline_count++]={semantic_support,verdict};result.verdict_recorded=true;
    result.status=DeclineStatus::Recorded;
    if(family_eligible(entry.family_id,semantic_support))return result;
    for(std::size_t i=0;i<families_.size();++i)if(families_[i].family_id==entry.family_id) {
        auto& family=families_[i];auto& work=work_[i];
        if(work.seeking){result.resumed=active(i);if(!result.resumed){result.status=DeclineStatus::Deferred;
            result.reason=work.paused?family.reason:ag::Reason::Capacity;}return result;}
        if(family.cursor==data_->size()||family.state==EntryState::Exhausted)
            {family.state=EntryState::Exhausted;result.status=DeclineStatus::Exhausted;return result;}
        work.seeking=true;family.state=EntryState::Scanning;family.reason=ag::Reason::None;
        result.resumed=reserve_output(i);
        if(!result.resumed){result.status=DeclineStatus::Deferred;result.reason=ag::Reason::Capacity;}
        return result;
    }
    return result;
}
inline RetireResult Menu::retire(std::uint64_t publication_id,std::uint64_t current_support,
    std::uint64_t incumbent_publication_id) {
    RetireResult result;
    if(!valid_||!publication_id||!current_support)return result;
    for(auto& entry:entries_)if(entry.publication_id==publication_id&&entry.state==EntryState::Ready&&entry.target) {
        if(publication_id==incumbent_publication_id){result.status=RetireStatus::Incumbent;return result;}
        if(entry.target.use_count()>1){result.status=RetireStatus::Pinned;return result;}
        if(!declined(entry,current_support)){result.status=RetireStatus::NoCompletedVerdict;return result;}
        const auto old=retired_index(publication_id);
        if(!old&&retired_.size()==limits_.records){metadata_owed_=true;result.status=RetireStatus::Deferred;result.reason=ag::Reason::Capacity;return result;}
        try {
            auto metadata=std::make_unique<Entry>(entry);metadata->target.reset();metadata->state=EntryState::Retired;
            if(old)retired_[*old]=std::move(metadata);else retired_.push_back(std::move(metadata));
            entry=Entry{};entry.state=EntryState::Exhausted;
            result.status=RetireStatus::Retired;return result;
        } catch(const std::bad_alloc&){result.status=RetireStatus::Deferred;result.reason=ag::Reason::Allocation;return result;}
    }
    result.status=retired_index(publication_id)?RetireStatus::AlreadyRetired:RetireStatus::UnknownPublication;return result;
}
inline ReconstructResult Menu::reconstruct(std::uint64_t publication_id,std::uint64_t new_support) {
    ReconstructResult result;if(!valid_||!publication_id||!new_support)return result;
    for(const auto& entry:entries_)if(entry.publication_id==publication_id&&entry.state==EntryState::Ready&&entry.target) {
        result.status=declined(entry,new_support)?ReconstructStatus::DeclinedForSupport:ReconstructStatus::AlreadyResident;return result;
    }
    const auto at=retired_index(publication_id);
    if(!at){result.status=ReconstructStatus::UnknownPublication;return result;}
    const auto& metadata=*retired_[*at];
    if(declined(metadata,new_support)){result.status=ReconstructStatus::DeclinedForSupport;return result;}
    for(std::size_t i=0;i<families_.size();++i)if(families_[i].family_id==metadata.family_id) {
        auto& work=work_[i];auto& family=families_[i];
        if(work.reconstruct_id==publication_id||work.pending_reconstruction==publication_id) {
            if(work.paused){work.paused=false;reserve_output(i);}
            result.owed=true;result.status=active(i)?ReconstructStatus::Queued:ReconstructStatus::Deferred;
            if(result.status==ReconstructStatus::Deferred)result.reason=ag::Reason::Capacity;
            return result;
        }
        if(work.seeking&&!work.builder&&!work.reconstruct_id) {
            // A scan cursor is retained, not reset. A requested old recipe can
            // use this idle construction slot before the next forward cell.
            if(work.output){entries_[*work.output]=Entry{};entries_[*work.output].state=EntryState::Exhausted;work.output.reset();}
            work.reconstruct_id=publication_id;work.paused=false;family.state=EntryState::Scanning;
            result.owed=true;result.status=reserve_output(i)?ReconstructStatus::Queued:ReconstructStatus::Deferred;
            if(result.status==ReconstructStatus::Deferred)result.reason=ag::Reason::Capacity;
            return result;
        }
        if(work.seeking) {
            if(work.pending_reconstruction){result.status=ReconstructStatus::Deferred;result.reason=ag::Reason::Capacity;return result;}
            work.pending_reconstruction=publication_id;result.owed=true;result.status=ReconstructStatus::Queued;return result;
        }
        work.reconstruct_id=publication_id;work.seeking=true;family.state=EntryState::Scanning;family.reason=ag::Reason::None;
        result.owed=true;result.status=reserve_output(i)?ReconstructStatus::Queued:ReconstructStatus::Deferred;
        if(result.status==ReconstructStatus::Deferred)result.reason=ag::Reason::Capacity;
        return result;
    }
    result.status=ReconstructStatus::UnknownPublication;return result;
}
inline void Menu::failed(std::size_t i,ag::Status status,ag::Reason reason) {
    auto& entry=families_[i];auto& work=work_[i];
    entry.failures[entry.next_failure]={work.perch,status,reason};
    entry.next_failure=(entry.next_failure+1)%remembered_failures;
    entry.failure_count=std::min(entry.failure_count+1,remembered_failures);
    ++entry.progress.failed;++progress_.failed;entry.reason=reason;
    work.builder.reset();entry.state=EntryState::Scanning;
    if(work.reconstruct_id) {
        // Rebuilding previously sealed geometry is not a new model verdict.
        // An allocation/provider failure leaves a typed incomplete result and
        // never advances or poisons the retained forward perch cursor.
        snapshot(i);auto& output=entries_[*work.output];output=Entry{};output.state=EntryState::Exhausted;
        work.output.reset();work.paused=true;entry.deferred=true;
        return;
    }
    snapshot(i);
}
inline bool Menu::publishable(std::size_t i,const std::shared_ptr<const sg::TargetGeometry>& target) const {
    const auto& entry=families_[i];const auto& work=work_[i];
    if(!target||target->static_geometry().get()!=data_.get()||target->static_key()!=data_->key()||
        target->group()!=entry.canonical_group||target->perch()!=work.perch||target->has_initial()||
        target->key().capture_policy!=entry.capture_policy||target->key().handoff_inputs!=entry.handoff_inputs)return false;
    if(!ag::same_support(target->stamp(),entry.nomination)||target->stamp().epoch!=entry.nomination.epoch)return false;
    const auto& selected=target->selected_tiles();
    const auto& members=data_->catalogue()[static_cast<std::size_t>(entry.canonical_group)].members;
    if(selected.size()!=members.size())return false;
    for(std::size_t member=0;member<members.size();++member)if(selected[member]!=members[member].pos)return false;
    const auto& kit=target->kit();
    if(kit.family!=work.kit.family||kit.basic_range!=work.kit.basic_range||kit.cast_range!=work.kit.cast_range||
        kit.area_radius!=work.kit.area_radius||kit.aim!=work.kit.aim)return false;
    const auto& result=target->result();
    if(result.status!=ag::Status::Ready||result.leg(ag::Leg::Initial).complete||!result.leg(ag::Leg::Initial).points.empty())return false;
    for(std::size_t leg=1;leg<ag::leg_count;++leg) {
        const auto& path=result.legs[leg];
        if(!path.complete||!path.disjoint_supported||path.points.empty()||
            path.edge_inputs+1!=path.points.size()||path.handoff_inputs!=entry.handoff_inputs[leg])return false;
    }
    return true;
}
inline void Menu::after_publication(std::size_t i) {
    auto& family=families_[i];auto& work=work_[i];
    work.builder.reset();work.output.reset();work.reconstruct_id=0;work.seeking=false;work.paused=false;family.deferred=false;
    if(work.pending_reconstruction) {
        work.reconstruct_id=work.pending_reconstruction;work.pending_reconstruction=0;
        work.seeking=true;family.state=EntryState::Scanning;
        reserve_output(i);
    }
}
inline void Menu::start_builder(std::size_t i) {
    auto& entry=families_[i];auto& work=work_[i];
    try {
        ag::Request request;request.stamp=entry.nomination;request.selected_group=entry.canonical_group;
        request.perch=work.perch;request.kit=work.kit;request.handoff_inputs=entry.handoff_inputs;
        for(const auto& member:data_->catalogue()[static_cast<std::size_t>(entry.canonical_group)].members)
            request.selected_tiles.push_back(member.pos);
        request.disjoint_inputs_supported=request.exclusive_ownership=true;
        work.builder=std::make_unique<sg::TargetBuilder>(data_,std::move(request),sg::TargetMode::FutureOnly);
        ++entry.progress.started;++progress_.started;
        const auto state=work.builder->status();
        if(state==ag::Status::NeedCompute){entry.state=EntryState::Building;snapshot(i);}
        else failed(i,state,work.builder->reason());
    } catch(const std::bad_alloc&){failed(i,ag::Status::Unsupported,ag::Reason::Allocation);}
      catch(const std::length_error&){failed(i,ag::Status::Unsupported,ag::Reason::Capacity);}
}
inline void Menu::unit(std::size_t i) {
    auto& entry=families_[i];auto& work=work_[i];++entry.progress.units;++progress_.units;
    if(!work.output){reserve_output(i);return;}
    if(entry.state==EntryState::Building) {
        const auto state=work.builder->advance(entry.nomination,1);
        entry.progress.builder_units+=work.builder->last_work();progress_.builder_units+=work.builder->last_work();
        if(state==ag::Status::Ready) {
            const auto target=work.builder->result();
            if(!publishable(i,target)){failed(i,ag::Status::Unsupported,ag::Reason::Changed);return;}
            auto& output=entries_[*work.output];
            const auto identity=work.reconstruct_id?work.reconstruct_id:detail::next_identity();
            if(!identity){failed(i,ag::Status::Unsupported,ag::Reason::Capacity);return;}
            output.target=target;entry.state=EntryState::Ready;entry.reason=ag::Reason::None;
            output.publication=output.publication_id=identity;++output.residency;
            output.perch=work.perch;output.target_kit=work.kit;
            ++entry.progress.published;++progress_.published;
            snapshot(i);after_publication(i);
        } else if(state!=ag::Status::NeedCompute)failed(i,state,work.builder->reason());
        else snapshot(i);
        return;
    }
    if(work.reconstruct_id) {
        const auto at=retired_index(work.reconstruct_id);
        if(!at){failed(i,ag::Status::Unsupported,ag::Reason::Changed);return;}
        work.perch=retired_[*at]->perch;work.kit=retired_[*at]->target_kit;start_builder(i);return;
    }
    if constexpr(cfg::assignment_reach_precheck) {
        if(entry.cursor==0) {
            namespace reach=assignment_static_reach;
            reach::Input proof;bool represented=true;
            switch(entry.geometry.family) {
                case ag::Family::Basic:proof.family=reach::Family::Basic;break;
                case ag::Family::Targeted:proof.family=reach::Family::Targeted;break;
                case ag::Family::Area:proof.family=reach::Family::Area;break;
                default:represented=false;break;
            }
            proof.width=data_->grid().width;proof.height=data_->grid().height;
            proof.basic_range=entry.geometry.basic_range;proof.spell_count=entry.geometry.count;
            for(std::size_t n=0;n<std::min(proof.spell_count,reach::max_spells);++n)
                proof.spells[n]={entry.geometry.spells[n].range,entry.geometry.spells[n].radius};
            const auto& members=data_->catalogue()[static_cast<std::size_t>(entry.canonical_group)].members;
            if(represented&&reach::certify(proof,members.size(),[&](std::size_t n){return members[n].pos;})==reach::Verdict::Empty)
                entry.cursor=data_->size();
            // Unknown/MayHavePerch keep the original closed-cell predicate,
            // cyclic order, spell tie selection and immutable recipe rules.
            // Earlier empty completion releases capacity sooner; cross-family
            // publication timing may change. No body/Life/command fact changes.
        }
    }
    if(entry.cursor==data_->size()) {
        entry.state=EntryState::Exhausted;snapshot(i);work.output.reset();work.seeking=false;
        if(work.pending_reconstruction){entry.state=EntryState::Scanning;work.reconstruct_id=work.pending_reconstruction;
            work.pending_reconstruction=0;work.seeking=true;reserve_output(i);}
        return;
    }
    // A cyclic row-major order starts at the nomination. It is a finite scan,
    // not a route-distance ranking or a current-origin authorization.
    const auto tail=data_->size()-entry.start;
    const auto ordinal=entry.cursor<tail?entry.start+entry.cursor:entry.cursor-tail;++entry.cursor;
    ++entry.progress.scanned_cells;++progress_.scanned_cells;
    snapshot(i);if(!data_->closed(static_cast<std::uint32_t>(ordinal)))return;
    const auto perch=data_->point(static_cast<std::uint32_t>(ordinal));
    const auto kit=detail::kit(entry.geometry,*data_,entry.canonical_group,perch);if(!kit)return;
    work.perch=perch;work.kit=*kit;
    start_builder(i);
}
inline Status Menu::advance(std::size_t units) {
    last_work_=0;if(!valid_||families_.empty())return status();
    while(last_work_<units) {
        std::size_t visited=0;
        while(visited<families_.size()&&!active(next_)){next_=(next_+1)%families_.size();++visited;}
        if(visited==families_.size())break;
        const auto at=next_;next_=(next_+1)%families_.size();++last_work_;unit(at);
    }
    return status();
}
} // namespace tt::assignment_static_menu
