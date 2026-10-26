#pragma once
// Bind reusable sealed geometry to NEW own facts. No old Quote is accepted.
// InputReady means only that the complete owned Producer input was built;
// ProducerReady and checked full-H calendar readiness remain later stages.
#include "assignment_static_geometry.h"
#include "production_job.h"
#include "production_word_clock.h"
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

namespace tt::assignment_fresh_binding {
namespace ag=assignment_geometry;namespace sg=assignment_static_geometry;namespace job=production_job;
enum class Status { NeedCompute,InputReady,Stale,Unsupported,Taken };
enum class Reason { None,Own,Premises,Dates,Geometry,Policy,Group,Kit,Metadata,Life,Clock,Path,Allocation,Capacity };
struct Current {
    job::Key key;sg::StaticKey support;ag::Stamp origin;
    // Exact OWN semantic identities supplied by fresh collector. Inventory is
    // the FULL inventory, not merely Snapshot.shop's five represented families.
    // Clock identifies absolute ready dates; round/query alone are not facts.
    std::uint64_t inventory{},wallet{},effects{},clock{},values{};
    job::Life life{job::Life::Unknown};job::Enabler enabler{job::Enabler::ObserveAlive};
    std::uint8_t allowances{};
};
inline std::uint8_t allowances(const mb::service::Body& body) {
    return static_cast<std::uint8_t>((body.can_move?1:0)|(body.can_attack?2:0)|
        (body.can_cast?4:0)|(body.moved_this_turn?8:0));
}
inline bool same_premises(const Current& a,const Current& b) {
    return a.key==b.key&&a.support==b.support&&a.origin.geometry==b.origin.geometry&&
        a.origin.potential==b.origin.potential&&a.origin.body==b.origin.body&&a.origin.origin==b.origin.origin&&
        a.inventory==b.inventory&&a.wallet==b.wallet&&a.effects==b.effects&&a.clock==b.clock&&a.values==b.values&&
        a.life==b.life&&a.enabler==b.enabler&&a.allowances==b.allowances;
}
struct Request {
    std::shared_ptr<const sg::OriginView> origin;
    std::shared_ptr<const sg::TargetGeometry> target;
    job::Snapshot self;job::Job metadata;Current current;
    std::uint32_t start{},horizon{};
    std::array<std::uint32_t,ag::leg_count> handoff_inputs{};
    std::uint64_t capture_policy{1};
    bool exact_own_state{},exact_personal_life{},project_omitted_clocks{};
};
class Builder {
public:
    explicit Builder(Request&& request):request_(std::move(request)),last_epoch_(request_.current.origin.epoch) {}
    Builder(const Builder&)=delete;Builder& operator=(const Builder&)=delete;
    Status advance(const Current&,std::size_t units);
    // Pure retained calculation from the ORIGINAL owning Request. InputReady
    // here is a dated nomination, never a fresh-current validation or action
    // permit. Caller must separately refresh/reconcile all adoption premises.
    // No later epoch, body, Life, key or snapshot is accepted or relabeled.
    Status advance_nomination(std::size_t units);
    Status status() const{return status_;}Reason reason() const{return reason_;}
    std::size_t last_work() const{return last_work_;}std::uint64_t work_used() const{return work_;}
    const Current& binding() const{return request_.current;}
    const job::Snapshot& actual_self() const{return request_.self;}
    const job::Input* input() const{return status_==Status::InputReady?input_.get():nullptr;}
    std::unique_ptr<job::Input> take() {
        if(status_!=Status::InputReady)return {};status_=Status::Taken;return std::move(input_);
    }
private:
    enum class Phase { Validate,Catalogue,Members,Empty,Create,External,InitialReserve,Initial,CopyReserve,Copy,Done };
    Request request_;std::unique_ptr<job::Input> input_;
    Status status_{Status::NeedCompute};Reason reason_{Reason::None};Phase phase_{Phase::Validate};
    const sg::CatalogGroup* selected_{};std::size_t catalogue_{},member_{},leg_{},point_{},external_group_{},external_member_{},last_work_{};
    ag::Point trace_;std::uint32_t distance_{};std::uint64_t work_{};int last_epoch_{};
    void fail(Status status,Reason reason){status_=status;reason_=reason;phase_=Phase::Done;}
    void unit();bool kit_supported() const;
    static job::Leg& leg(job::Job& value,std::size_t i) {
        switch(i){case 0:return value.initial;case 1:return value.capture;case 2:return value.release;
        case 3:return value.bank;case 4:return value.renewal;case 5:return value.home;default:return value.redeploy;}
    }
};
inline bool Builder::kit_supported() const {
    const auto& metadata=request_.metadata;const auto& self=request_.self;const auto& kit=request_.target->kit();
    const auto perch=request_.target->perch();
    if(kit.family==ag::Family::Basic) {
        for(std::size_t i=0;i<metadata.member_count;++i)if(ag::detail::separation(perch,
            {metadata.members[i].pos.x,metadata.members[i].pos.y})>self.body.attack_range)return false;
        return true;
    }
    for(const auto& spell:self.spells) {
        const bool family=kit.family==ag::Family::Area?spell.kind==mb::service::SpellKind::aoe:
            kit.family==ag::Family::Targeted&&(spell.kind==mb::service::SpellKind::damage||spell.kind==mb::service::SpellKind::cc);
        if(!family||spell.range<0||spell.radius<0)continue;
        bool valid=kit.family!=ag::Family::Area||ag::detail::separation(perch,kit.aim)<=spell.range;
        for(std::size_t i=0;i<metadata.member_count;++i) {
            const ag::Point p{metadata.members[i].pos.x,metadata.members[i].pos.y};
            valid&=kit.family==ag::Family::Area?ag::detail::separation(kit.aim,p)<=spell.radius:
                ag::detail::separation(perch,p)<=spell.range;
        }
        if(valid)return true;
    }
    return false;
}
inline void Builder::unit() {
    auto& r=request_;const auto& current=r.current;auto& metadata=r.metadata;
    if(phase_==Phase::Validate) {
        if(!r.exact_own_state||!r.self.own||!r.self.complete||!r.self.stable_outgoing||
           r.self.actor!=current.key.actor||r.self.body_revision!=current.key.body_revision)
            {fail(Status::Unsupported,Reason::Own);return;}
        if(!r.origin||!r.target||r.origin->static_geometry()!=r.target->static_geometry()||
           r.origin->static_key()!=current.support||r.target->static_key()!=current.support||
           r.target->result().status!=ag::Status::Ready)
            {fail(Status::Unsupported,Reason::Geometry);return;}
        const auto& stamp=r.origin->stamp();
        if(stamp.origin!=current.origin.origin||stamp.body!=current.key.body_revision||stamp.epoch<0||
           static_cast<std::uint32_t>(stamp.epoch)!=r.self.epoch||stamp.geometry!=current.support.layout||
           stamp.potential!=current.support.potential||current.origin.body!=stamp.body||
           current.origin.geometry!=stamp.geometry||current.origin.potential!=stamp.potential||
           current.origin.epoch!=stamp.epoch||stamp.origin!=ag::Point{r.self.body.pos.x,r.self.body.pos.y}||
           current.allowances!=allowances(r.self.body))
            {fail(Status::Unsupported,Reason::Premises);return;}
        if(r.start<r.self.epoch||r.horizon<r.start||metadata.observed_round>r.self.epoch)
            {fail(Status::Unsupported,Reason::Dates);return;}
        const bool life=metadata.life==job::Life::Unknown||metadata.life==job::Life::Alive||
            metadata.life==job::Life::Reward||metadata.life==job::Life::Respawning;
        const bool enabler=metadata.enabler==job::Enabler::None||metadata.enabler==job::Enabler::ObserveAlive||
            metadata.enabler==job::Enabler::ObserveReward||metadata.enabler==job::Enabler::ObserveRenewal||
            metadata.enabler==job::Enabler::RefreshWallet||metadata.enabler==job::Enabler::Readback;
        if(!life||!enabler||!r.exact_personal_life||metadata.life!=current.life||metadata.enabler!=current.enabler||
           ((metadata.life==job::Life::Unknown||metadata.life==job::Life::Respawning)&&metadata.enabler==job::Enabler::None))
            {fail(Status::Unsupported,Reason::Life);return;}
        if(metadata.group!=current.key.group||metadata.generation!=current.key.generation||
           metadata.life_revision!=current.key.life_revision||metadata.geometry_revision!=current.key.geometry_revision||
           metadata.word_revision!=current.key.word_revision||r.target->group()<0||
           static_cast<std::uint64_t>(r.target->group())!=metadata.group||!metadata.member_count||
           metadata.member_count>job::max_members||!metadata.full_group_known||
           metadata.width!=r.origin->static_geometry()->grid().width||metadata.height!=r.origin->static_geometry()->grid().height)
            {fail(Status::Unsupported,Reason::Metadata);return;}
        if(r.capture_policy!=r.target->key().capture_policy||r.handoff_inputs!=r.target->key().handoff_inputs)
            {fail(Status::Unsupported,Reason::Policy);return;}
        if(!kit_supported()) {fail(Status::Unsupported,Reason::Kit);return;}
        phase_=Phase::Catalogue;return;
    }
    if(phase_==Phase::Catalogue) {
        const auto& groups=r.origin->static_geometry()->catalogue();
        if(catalogue_>=groups.size()){fail(Status::Unsupported,Reason::Group);return;}
        const auto& group=groups[catalogue_++];if(group.canonical!=r.target->group())return;
        selected_=&group;
        if(group.members.size()!=metadata.member_count||r.target->selected_tiles().size()!=metadata.member_count)
            {fail(Status::Unsupported,Reason::Group);return;}
        phase_=Phase::Members;return;
    }
    if(phase_==Phase::Members) {
        if(member_>=metadata.member_count){leg_=0;phase_=Phase::Empty;return;}
        const auto i=member_++;const auto& member=metadata.members[i];const ag::Point p{member.pos.x,member.pos.y};
        bool target=false,catalogue=false;
        for(const auto point:r.target->selected_tiles())target|=point==p;
        for(const auto& source:selected_->members)if(source.pos==p)
            catalogue=source.max_hp==metadata.full_hp[i]&&source.strength==member.strength&&source.range==member.range;
        for(std::size_t j=0;j<i;++j)if(metadata.members[j].pos==member.pos){fail(Status::Unsupported,Reason::Group);return;}
        if(!target||!catalogue){fail(Status::Unsupported,Reason::Group);return;}return;
    }
    if(phase_==Phase::Empty) {
        if(leg_>=ag::leg_count){phase_=Phase::Create;return;}
        if(!leg(metadata,leg_++).points.empty()){fail(Status::Unsupported,Reason::Metadata);return;}return;
    }
    if(phase_==Phase::Create) {
        input_=std::make_unique<job::Input>();input_->key=current.key;input_->self=r.self;
        input_->job=std::move(metadata);input_->start=r.start;input_->horizon=r.horizon;
        input_->job.perch={r.target->perch().x,r.target->perch().y};
        input_->job.external_work_raw=input_->job.external_capture_raw=0;input_->job.external_sources_complete=true;
        if(r.project_omitted_clocks) {
            const auto clock=production_word_clock::project(input_->self.epoch,input_->self.spells,input_->self.cooldown);
            if(clock.status!=production_word_clock::Status::Ready){fail(Status::Unsupported,Reason::Clock);return;}
            input_->self.cooldown=clock.cooldown;
        }
        phase_=Phase::External;return;
    }
    if(phase_==Phase::External) {
        const auto& groups=r.origin->static_geometry()->catalogue();
        if(external_group_>=groups.size()){phase_=Phase::InitialReserve;return;}
        const auto& group=groups[external_group_];
        if(group.canonical==r.target->group()||external_member_>=group.members.size()) {++external_group_;external_member_=0;return;}
        const auto& source=group.members[external_member_++];
        const auto add=[&](ag::Point p,double& value) {
            return ag::detail::separation(source.pos,p)>source.range||ag::detail::add_upper(value,source.strength,value);
        };
        if(!add(r.target->perch(),input_->job.external_work_raw)||
           !add(r.target->result().capture_tile,input_->job.external_capture_raw))
            fail(Status::Unsupported,Reason::Metadata);
        return;
    }
    if(phase_==Phase::InitialReserve) {
        trace_=current.origin.origin;distance_=r.target->distance_to_perch(trace_);
        if(distance_==ag::detail::none||distance_>=r.origin->static_geometry()->max_path_points())
            {fail(Status::Unsupported,Reason::Path);return;}
        auto& path=input_->job.initial;path.points.reserve(static_cast<std::size_t>(distance_)+1);
        path.handoff_inputs=r.handoff_inputs[0];path.disjoint_supported=true;phase_=Phase::Initial;return;
    }
    if(phase_==Phase::Initial) {
        const auto upper=r.target->raw_upper(trace_);
        if(!upper||!std::isfinite(*upper)||*upper<0||!r.origin->static_geometry()->closed(r.origin->static_geometry()->index(trace_)))
            {fail(Status::Unsupported,Reason::Path);return;}
        input_->job.initial.points.push_back({{trace_.x,trace_.y},*upper});
        if(!distance_) {
            if(trace_!=r.target->perch()){fail(Status::Unsupported,Reason::Path);return;}
            input_->job.initial.complete=true;leg_=1;phase_=Phase::CopyReserve;return;
        }
        const auto next=r.target->next_to_perch(trace_);
        if(!next||ag::detail::separation(trace_,*next)!=1||r.target->distance_to_perch(*next)!=distance_-1)
            {fail(Status::Unsupported,Reason::Path);return;}
        trace_=*next;--distance_;return;
    }
    if(phase_==Phase::CopyReserve) {
        if(leg_>=ag::leg_count){status_=Status::InputReady;phase_=Phase::Done;return;}
        const auto& source=r.target->result().legs[leg_];
        if(!source.complete||!source.disjoint_supported||source.points.empty()||source.handoff_inputs!=r.handoff_inputs[leg_])
            {fail(Status::Unsupported,Reason::Path);return;}
        auto& target=leg(input_->job,leg_);target.points.reserve(source.points.size());
        target.handoff_inputs=source.handoff_inputs;target.disjoint_supported=true;point_=0;phase_=Phase::Copy;return;
    }
    if(phase_==Phase::Copy) {
        auto& target=leg(input_->job,leg_);const auto& source=r.target->result().legs[leg_];
        if(point_<source.points.size()){const auto& p=source.points[point_++];target.points.push_back({{p.point.x,p.point.y},p.raw_upper});return;}
        target.complete=true;++leg_;phase_=Phase::CopyReserve;return;
    }
}
inline Status Builder::advance(const Current& current,std::size_t units) {
    last_work_=0;
    if(status_!=Status::NeedCompute&&status_!=Status::InputReady)return status_;
    if(!same_premises(request_.current,current)||current.origin.epoch<last_epoch_)
        {fail(Status::Stale,Reason::Premises);return status_;}
    last_epoch_=current.origin.epoch;
    try {while(last_work_<units&&status_==Status::NeedCompute){++last_work_;++work_;unit();}}
    catch(const std::bad_alloc&){fail(Status::Unsupported,Reason::Allocation);}
    catch(const std::length_error&){fail(Status::Unsupported,Reason::Capacity);}
    return status_;
}
inline Status Builder::advance_nomination(std::size_t units) {
    last_work_=0;
    if(status_!=Status::NeedCompute)return status_;
    try {while(last_work_<units&&status_==Status::NeedCompute){++last_work_;++work_;unit();}}
    catch(const std::bad_alloc&){fail(Status::Unsupported,Reason::Allocation);}
    catch(const std::length_error&){fail(Status::Unsupported,Reason::Capacity);}
    return status_;
}
} // namespace tt::assignment_fresh_binding
