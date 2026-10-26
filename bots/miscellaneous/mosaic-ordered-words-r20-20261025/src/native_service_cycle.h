#pragma once
#include "native_service_egress.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace mosaic::native_service {

enum class CyclePhase : std::uint8_t {idle,observe,work,egress,recall,home_heal,dead,bank,redeploy,capture};
enum class CycleUpdate : std::uint8_t {ignored,unchanged,retained,extended,resolved,observed,bound,advanced,restored,released,replaced,stale,invalid,conflict};
struct CycleJob {
    NativeId group_id{},member_id{};int group_ordinal{-1},member_ordinal{-1};
    friend constexpr bool operator==(CycleJob,CycleJob)=default;
    // SDK handles may be zero. Bound catalogue ordinals carry validity.
    constexpr bool valid()const noexcept{return group_ordinal>=0&&member_ordinal>=0;}
};
struct CycleWork {
    EgressWork receipt{};CycleJob job{};
    // A caller-qualified exposure witness, not an inference of damage taken.
    // Liability may remain during a separately owned, finite Work batch.
    bool hazardous{};
};
struct CyclePlace {Epoch epoch{};Point pose{};bool actual{},value{};};
struct CycleEgressClosed {
    Epoch epoch{};Point pose{};std::uint64_t through_serial{},accepted_prefix{};bool actual{};
};
struct CycleReplacement {
    std::uint64_t serial{};Epoch epoch{};CycleJob from{},to{};bool complete{},funded{};
};
struct CycleMemberAdvance {CycleJob from{},to{};Epoch epoch{};bool actual_dead{};};
struct CycleMemberBinding {
    std::uint64_t serial{},group_generation{};Epoch epoch{};bool complete{},funded{};
};
struct CycleMemberBound {CycleJob from{},to{};Epoch epoch{};std::uint64_t serial{},group_generation{};};

namespace cycle_detail {
constexpr bool before(Epoch a,Epoch b)noexcept {
    return a.round!=b.round?a.round<b.round:a.revision!=b.revision?a.revision<b.revision:a.attempt<b.attempt;
}
constexpr bool valid(Epoch value)noexcept{return value.round>=0;}
constexpr bool positive_finite(float value)noexcept {
    return value>0&&value<=std::numeric_limits<float>::max();
}
constexpr bool ready_body(const SelfFact& self)noexcept {
    return valid(self.epoch)&&self.actual(self.epoch)&&self.alive&&!self.stunned&&
        !self.recalling&&!self.submitting&&!self.teleporting&&!self.completed;
}
constexpr bool place(const CyclePlace& witness,const SelfFact& self)noexcept {
    return witness.actual&&witness.value&&witness.epoch==self.epoch&&witness.pose==self.pose;
}
}

// Owns unfinished service through restoration. It issues no command, prices no
// continuation, and does not own or clear the separate EgressState. In
// particular, restore_required() is a liability, NOT a per-Basic exit policy.
// batch_active() only describes an unclosed batch; the caller must independently
// qualify/fund its entire continuation and freshly authorize each primitive.
// All work is constant bounded, including two retained lifecycle slots.
class CycleState {
public:
    constexpr CycleUpdate observe_work(const CycleWork& event)noexcept {
        const auto& r=event.receipt;
        if(r.kind!=PrimitiveKind::basic&&r.kind!=PrimitiveKind::cast_unit&&r.kind!=PrimitiveKind::cast_ground)
            return CycleUpdate::ignored;
        if(!cycle_detail::valid(r.epoch)||!r.serial||r.accepted_prefix>r.serial||
           (r.effect==EgressEffect::yes&&!r.accepted_prefix)||!event.job.valid()||
           r.job.group!=event.job.group_ordinal||r.job.member!=event.job.member_ordinal||r.job.target!=event.job.member_id||
           (r.effect!=EgressEffect::unknown&&r.effect!=EgressEffect::no&&r.effect!=EgressEffect::yes))return CycleUpdate::invalid;
        if(r.serial<seen_serial_)return CycleUpdate::stale;
        if(r.serial==seen_serial_){
            if(r.serial!=latest_.receipt.serial||!same_attempt(event,latest_))return CycleUpdate::conflict;
            if(latest_.receipt.effect!=EgressEffect::unknown)
                return r.effect==latest_.receipt.effect&&r.accepted_prefix==latest_.receipt.accepted_prefix?
                    CycleUpdate::unchanged:CycleUpdate::conflict;
            if(r.accepted_prefix<latest_.receipt.accepted_prefix)return CycleUpdate::invalid;
            if(r.effect==EgressEffect::unknown)return r.accepted_prefix==latest_.receipt.accepted_prefix?
                CycleUpdate::unchanged:CycleUpdate::conflict;
            latest_=event;conditional_=false;
            if(origin_.receipt.serial==r.serial)origin_=event;
            if(r.serial>restored_through_){
                if(r.effect==EgressEffect::yes&&event.hazardous)work_restore_=true;
                else work_restore_=restore_before_pending_;
            }
            return CycleUpdate::resolved;
        }
        // One unresolved physical receipt cannot be displaced by another.
        // The external executor/egress ledgers still own every actual prefix.
        if(conditional_){ownership_conflict_=true;return CycleUpdate::conflict;}
        if(latest_.receipt.serial&&cycle_detail::before(r.epoch,latest_.receipt.epoch))return CycleUpdate::stale;
        if(latest_.receipt.serial&&r.accepted_prefix<latest_.receipt.accepted_prefix)return CycleUpdate::invalid;
        if(r.effect==EgressEffect::no)return CycleUpdate::ignored;
        const bool had_job=current_.job.valid(),had_liability=restore_required();
        if(!had_job){
            if(next_group_generation_==std::numeric_limits<std::uint64_t>::max())return CycleUpdate::invalid;
            current_={event.job};current_.generation=++next_group_generation_;
        }
        else if(current_.job!=event.job){
            const bool same_group=current_.job.group_id==event.job.group_id&&current_.job.group_ordinal==event.job.group_ordinal;
            // Actual receipt truth updates the selected member of the same
            // whole-group owner. It neither completes the old member nor
            // establishes damage progress by comparing different targets.
            if(same_group){current_.job=event.job;current_.member_dead=false;current_.member_epoch={};current_.holding=false;}
            else ownership_conflict_=true;
        }
        // An actual Work return is retained even if the caller violated a
        // restoration barrier. A later qualified restoration can repair this
        // barrier; it never retroactively authorizes that command.
        if(event.hazardous&&had_liability&&has_egressed_)rearm_violation_=true;
        if(event.hazardous&&!had_liability)has_egressed_=false;
        if(!latest_.receipt.serial||!had_job||(event.hazardous&&!had_liability))origin_=event;
        restore_before_pending_=work_restore_;work_restore_|=event.hazardous;
        conditional_=r.effect==EgressEffect::unknown;seen_serial_=r.serial;latest_=event;
        latest_group_generation_=current_.job.group_id==event.job.group_id&&current_.job.group_ordinal==event.job.group_ordinal?
            current_.generation:0;
        if(origin_.receipt.serial==r.serial)origin_group_generation_=latest_group_generation_;
        return ownership_conflict_||rearm_violation_?CycleUpdate::conflict:had_job?CycleUpdate::extended:CycleUpdate::retained;
    }

    constexpr CycleUpdate observe_actual(const SelfFact& self,CyclePlace quiet={},CyclePlace home={},CycleEgressClosed closed={})noexcept {
        if(!cycle_detail::valid(self.epoch)||!self.body_actual)return CycleUpdate::invalid;
        if(have_actual_&&cycle_detail::before(self.epoch,actual_.epoch))return CycleUpdate::stale;
        if(actor_bound_&&actor_!=self.id)return CycleUpdate::conflict;
        actor_=self.id;actor_bound_=true;actual_=self;have_actual_=true;
        quiet_=cycle_detail::place(quiet,self);home_=cycle_detail::place(home,self);
        observe_custody(self);
        if(!self.alive){if(current_.job.valid()||suspended_.job.valid()||restore_required())body_restore_=true;return CycleUpdate::observed;}
        const bool after_work=!latest_.receipt.serial||cycle_detail::before(latest_.receipt.epoch,self.epoch);
        const bool closed_here=closed.actual&&closed.epoch==self.epoch&&closed.pose==self.pose&&
            closed.through_serial==latest_.receipt.serial&&closed.accepted_prefix==latest_.receipt.accepted_prefix&&after_work;
        if(restore_required()&&after_work&&(quiet_||home_||closed_here))has_egressed_=true;
        // Quiet and HP are separate observations. A qualified quiet/home pose
        // may close liability without an arbitrary Recall if HP is already
        // actually restored. An ACK/Move/START or merely absent channel cannot.
        const bool full=cycle_detail::positive_finite(self.max_hp)&&self.hp==self.max_hp;
        const bool no_channel=self.channel_actual&&!self.recalling&&!self.submitting&&!self.teleporting;
        if(restore_required()&&after_work&&(quiet_||home_)&&full&&no_channel){
            work_restore_=body_restore_=false;rearm_violation_=false;restored_through_=seen_serial_;
            return CycleUpdate::restored;
        }
        return CycleUpdate::observed;
    }

    // Inspect the selected member's actual life. This is not a requirement
    // that it die before another fresh member of the whole group can be bound.
    // In particular, a sleep/HP observation never completes that member.
    constexpr CycleUpdate observe_member(const CampFact& member,Epoch now)noexcept {
        if(!cycle_detail::valid(now)||!member.actual(now)||member.member_ordinal<0||member.group_ordinal<0||
           member.life==Life::unknown)return CycleUpdate::invalid;
        if(have_actual_&&cycle_detail::before(now,actual_.epoch))return CycleUpdate::stale;
        bool matched=false;
        for(auto* slot:std::array<Slot*,2>{&current_,&suspended_}){
            const auto& owner=slot->job;
            if(!owner.valid()||owner.group_id!=member.group_id||owner.member_id!=member.id||
               owner.group_ordinal!=member.group_ordinal||owner.member_ordinal!=member.member_ordinal||
               cycle_detail::before(now,slot->member_epoch))continue;
            slot->member_epoch=now;slot->member_dead=member.life==Life::dead;matched=true;
        }
        return matched?CycleUpdate::observed:CycleUpdate::stale;
    }

    // A funded SAME-group member nomination requires fresh actual living body
    // facts. It is not a Work effect, completion, HP delta or SDK permission.
    // The old selected member may still live; the whole-group job/generation,
    // restoration, custody and exact physical histories remain attached.
    constexpr CycleUpdate bind_member(const CampFact& member,Epoch now,const CycleMemberBinding& proof)noexcept {
        if(!cycle_detail::valid(now)||conditional_||!current_.job.valid()||!proof.serial||
           proof.serial<=member_binding_serial_||proof.group_generation!=current_.generation||proof.epoch!=now||
           !proof.complete||!proof.funded||!have_actual_||actual_.epoch!=now||!actual_.actual(now)||
           !member.living(now)||cycle_detail::before(now,member.epoch)||!member.body_actual||!member.buffs_actual||member.member_ordinal<0||
           member.group_id!=current_.job.group_id||member.group_ordinal!=current_.job.group_ordinal)return CycleUpdate::invalid;
        if((member.member_ordinal==current_.job.member_ordinal)!=(member.id==current_.job.member_id))return CycleUpdate::invalid;
        if(current_.life==GroupLife::reward&&same_world(current_.lifecycle,now))return CycleUpdate::conflict;
        const CycleJob next{member.group_id,member.id,member.group_ordinal,member.member_ordinal};
        bound_={current_.job,next,now,proof.serial,current_.generation};member_binding_serial_=proof.serial;
        const bool same=next==current_.job;current_.job=next;current_.member_dead=false;current_.member_epoch=member.epoch;current_.holding=false;
        return same?CycleUpdate::unchanged:CycleUpdate::bound;
    }

    // Consume the selected member's fresh actual death before walking toward
    // another member. This changes only nomination: no Work/effect/completion
    // credit, no physical authority, and no dead unfinished suspended member.
    // The new member can be freshly validated after future approach inputs;
    // the old death does not have to be re-queried from outside personal vision.
    constexpr CycleUpdate advance_member(CycleJob next,const CampFact& dead,Epoch now)noexcept {
        if(!cycle_detail::valid(now)||conditional_||!current_.job.valid()||!next.valid()||next.group_id!=current_.job.group_id||
           next.group_ordinal!=current_.job.group_ordinal||dead.life!=Life::dead||!dead.actual(now)||
           dead.id!=current_.job.member_id||dead.group_id!=current_.job.group_id||
           dead.member_ordinal!=current_.job.member_ordinal||dead.group_ordinal!=current_.job.group_ordinal)
            return CycleUpdate::invalid;
        if(have_actual_&&cycle_detail::before(now,actual_.epoch))return CycleUpdate::stale;
        if(current_.life==GroupLife::reward&&same_world(current_.lifecycle,now))return CycleUpdate::invalid;
        if(next==current_.job)return CycleUpdate::unchanged;
        if(observe_member(dead,now)!=CycleUpdate::observed)return CycleUpdate::stale;
        advance_={current_.job,next,now,true};current_.job=next;current_.member_dead=false;
        current_.member_epoch={};current_.holding=false;return CycleUpdate::advanced;
    }

    // Positive bag observation is retained through missing/stale readings.
    // Actual zero releases custody, but certifies no payout, theft or loss.
    constexpr CycleUpdate observe_custody(const SelfFact& self)noexcept {
        if(!cycle_detail::valid(self.epoch)||!self.body_actual||!self.gems_actual||!self.custody_actual||self.gems<0)
            return CycleUpdate::invalid;
        if(cycle_detail::before(self.epoch,custody_epoch_))return CycleUpdate::stale;
        if(actor_bound_&&actor_!=self.id)return CycleUpdate::conflict;
        actor_=self.id;actor_bound_=true;custody_epoch_=self.epoch;custody_=self.gems;custody_known_=true;return CycleUpdate::observed;
    }

    // Reward occupancy is capture intent only. Current actual Respawning is
    // explicit group-cycle release; neither observation invents gem receipt.
    constexpr CycleUpdate observe_lifecycle(const GroupFact& group,const CaptureFact& capture,Epoch now)noexcept {
        if(!cycle_detail::valid(now)||!group.current(now)||group.ordinal<0||
           group.life==GroupLife::unknown)return CycleUpdate::invalid;
        if(have_actual_&&cycle_detail::before(now,actual_.epoch))return CycleUpdate::stale;
        bool matched=false,released=false;
        for(auto* slot:std::array<Slot*,2>{&current_,&suspended_}){
            if(!slot->job.valid()||slot->job.group_id!=group.id||slot->job.group_ordinal!=group.ordinal)continue;
            if(cycle_detail::before(now,slot->lifecycle))continue;
            // A pre-Work lifecycle cache cannot release the newly owed job.
            if(group.life==GroupLife::respawning&&latest_.job.group_id==group.id&&
               !cycle_detail::before(latest_.receipt.epoch,now))continue;
            matched=true;slot->life=group.life;slot->lifecycle=now;
            slot->holding=group.life==GroupLife::reward&&capture.group_id==group.id&&capture.holding_reward(now);
            if(group.life==GroupLife::respawning){*slot={};released=true;}
        }
        if(!matched)return CycleUpdate::stale;
        return released?CycleUpdate::released:CycleUpdate::observed;
    }

    // One unfinished displaced job is retained, never silently overwritten.
    // A funded replacement transfers SERVICE ownership only; restoration,
    // custody, receipt history and the separate physical egress stay owed.
    constexpr CycleUpdate replace(const CycleReplacement& proof)noexcept {
        if(!proof.serial||proof.serial<=replacement_serial_||!proof.complete||!proof.funded||!proof.to.valid()||
           !have_actual_||proof.epoch!=actual_.epoch||!actual_.actual(proof.epoch)||conditional_||proof.from!=current_.job)
            return CycleUpdate::invalid;
        if(proof.to==current_.job)return CycleUpdate::unchanged;
        if(suspended_.job.valid()&&proof.to!=suspended_.job)return CycleUpdate::conflict;
        const auto old=current_;
        if(proof.to==suspended_.job){current_=suspended_;suspended_=old;}
        else{
            if(current_.job.valid()&&proof.to.group_id==current_.job.group_id&&proof.to.group_ordinal==current_.job.group_ordinal)
                return CycleUpdate::invalid; // use fresh funded bind_member for an in-group nomination
            if(next_group_generation_==std::numeric_limits<std::uint64_t>::max())return CycleUpdate::invalid;
            current_={proof.to};current_.generation=++next_group_generation_;suspended_=old;
        }
        replacement_serial_=proof.serial;
        if(latest_.job==current_.job)ownership_conflict_=false;
        return CycleUpdate::replaced;
    }

    constexpr CyclePhase phase()const noexcept {
        if(!current_.job.valid()&&!suspended_.job.valid()&&!restore_required()&&!bank_required()&&!conditional_)return CyclePhase::idle;
        if(!have_actual_)return CyclePhase::observe;
        if(!actual_.alive)return CyclePhase::dead;
        if(actual_.channel_actual&&(actual_.recalling||actual_.teleporting))return CyclePhase::recall;
        if(restore_required()&&home_)return CyclePhase::home_heal;
        if(restore_required()&&has_egressed_)return CyclePhase::recall;
        if(bank_required())return CyclePhase::bank;
        if(conditional_||ownership_conflict_||!cycle_detail::ready_body(actual_))return CyclePhase::observe;
        if(restore_required())return !current_.job.valid()||current_.life==GroupLife::reward?CyclePhase::egress:CyclePhase::work;
        if(current_.job.valid())return current_.life==GroupLife::reward?CyclePhase::capture:CyclePhase::redeploy;
        return CyclePhase::idle;
    }
    constexpr bool restore_required()const noexcept{return work_restore_||body_restore_;}
    constexpr bool has_egressed()const noexcept{return has_egressed_;}
    constexpr bool restoration_barrier()const noexcept{return restore_required()&&has_egressed_;}
    constexpr bool conditional()const noexcept{return conditional_;}
    constexpr bool ownership_conflict()const noexcept{return ownership_conflict_;}
    constexpr bool rearm_violation()const noexcept{return rearm_violation_;}
    constexpr bool bank_required()const noexcept{return custody_known_&&custody_>0;}
    constexpr int custody()const noexcept{return custody_;}
    constexpr Epoch custody_epoch()const noexcept{return custody_epoch_;}
    constexpr CycleJob retained_job()const noexcept{return current_.job;}
    constexpr CycleJob suspended_job()const noexcept{return suspended_.job;}
    // Local unreused service episodes, not invented SDK respawn identifiers.
    constexpr std::uint64_t group_generation()const noexcept{return current_.job.valid()?current_.generation:0;}
    constexpr std::uint64_t suspended_group_generation()const noexcept{return suspended_.job.valid()?suspended_.generation:0;}
    constexpr std::uint64_t latest_work_group_generation()const noexcept{return latest_group_generation_;}
    constexpr std::uint64_t origin_work_group_generation()const noexcept{return origin_group_generation_;}
    constexpr CycleMemberBound last_member_binding()const noexcept{return bound_;}
    constexpr bool selected_member_dead()const noexcept{return current_.member_dead;}
    constexpr Epoch member_observation_epoch()const noexcept{return current_.member_epoch;}
    constexpr CycleMemberAdvance last_member_advance()const noexcept{return advance_;}
    constexpr const CycleWork& origin_work()const noexcept{return origin_;}
    constexpr const CycleWork& latest_work()const noexcept{return latest_;}
    constexpr std::uint64_t restored_through()const noexcept{return restored_through_;}
    constexpr bool capture_holding()const noexcept {
        return have_actual_&&current_.holding&&current_.lifecycle==actual_.epoch;
    }
    // Neither readiness accessor grants movement/attack/cast permission.
    constexpr bool resume_ready()const noexcept {
        return current_.job.valid()&&!restore_required()&&!conditional_&&!ownership_conflict_&&!bank_required()&&
            have_actual_&&cycle_detail::ready_body(actual_)&&actual_.gems_actual&&actual_.custody_actual&&actual_.gems==0;
    }
    constexpr bool batch_active()const noexcept {
        return current_.job.valid()&&current_.life!=GroupLife::reward&&restore_required()&&!has_egressed_&&!conditional_&&!ownership_conflict_&&!bank_required()&&
            have_actual_&&cycle_detail::ready_body(actual_);
    }

private:
    struct Slot {CycleJob job{};GroupLife life{GroupLife::unknown};Epoch lifecycle{},member_epoch{};bool holding{},member_dead{};std::uint64_t generation{};};
    Slot current_{},suspended_{};CycleWork origin_{},latest_{};CycleMemberAdvance advance_{};CycleMemberBound bound_{};SelfFact actual_{};
    NativeId actor_{};Epoch custody_epoch_{};
    std::uint64_t seen_serial_{},restored_through_{},replacement_serial_{};
    std::uint64_t next_group_generation_{},member_binding_serial_{},latest_group_generation_{},origin_group_generation_{};
    int custody_{};
    bool actor_bound_{},have_actual_{},quiet_{},home_{},custody_known_{},work_restore_{},body_restore_{},conditional_{};
    bool restore_before_pending_{},has_egressed_{},ownership_conflict_{},rearm_violation_{};
    static constexpr bool same_attempt(const CycleWork& a,const CycleWork& b)noexcept {
        const auto& x=a.receipt;const auto& y=b.receipt;
        return a.job==b.job&&a.hazardous==b.hazardous&&x.kind==y.kind&&x.epoch==y.epoch&&x.pose==y.pose&&x.job==y.job;
    }
};

enum class CycleGroupConfigure : std::uint8_t {configured,retained,invalid,changed};
enum class CycleGroupUpdate : std::uint8_t {observed,invalid,stale,foreign};
struct CycleGroupShape {
    NativeId group_id{};int group_ordinal{-1};std::uint64_t group_generation{};
    int first_member{-1};std::size_t member_count{};
    friend constexpr bool operator==(CycleGroupShape,CycleGroupShape)=default;
};
struct CycleMemberAbsence {
    std::uint64_t group_generation{};int absence_until{-1};bool actual{};
};
struct CycleRemaining {
    std::size_t total{},observed_dead{},observed_living{},unseen{},excluded_dead{},possible_members{},fresh_hp{},unknown_hp{};
    double known_hp_shield{};bool configured{},all_remaining_hp_current{};
};
enum class CycleEffectStatus : std::uint8_t {applied,unchanged,duplicate,unknown,invalid,stale};
struct CycleMemberEffect {
    std::uint64_t group_generation{};CycleWork work{};Epoch after{};bool exact_ack{};
    CampFact before{},after_fact{};CycleMemberAbsence absence{};
};
struct CycleEffectUpdate {
    CycleEffectStatus status{CycleEffectStatus::unknown};bool rank_decreased{},member_dead{};
    constexpr bool progress()const noexcept{return status==CycleEffectStatus::applied&&(rank_decreased||member_dead);}
};
struct CycleEffectRecord {
    std::uint64_t group_generation{},serial{},accepted_prefix{};CycleJob job{};Epoch before{},after{};
    float before_rank{},after_rank{};bool numeric_actual{},member_dead{};
};

// Dated facts for ONE owned group, not a world/visibility copy. Put a suitably
// sized instance in separately owned static storage; copying is disabled.
// Configuration changes only metadata/stamps, never clears Capacity cells.
// member_needed is O(1). remaining scans the configured group for reporting;
// callers admit that bounded scan separately instead of invoking it per edge.
template<std::size_t Capacity=64> class CycleGroupWork {
    static_assert(Capacity>0&&Capacity<=std::size_t(std::numeric_limits<int>::max()));
public:
    constexpr CycleGroupWork()noexcept=default;
    CycleGroupWork(const CycleGroupWork&)=delete;
    CycleGroupWork& operator=(const CycleGroupWork&)=delete;
    CycleGroupWork(CycleGroupWork&&)=delete;
    CycleGroupWork& operator=(CycleGroupWork&&)=delete;

    constexpr CycleGroupConfigure configure(CycleGroupShape next)noexcept {
        if(exhausted_||next.group_ordinal<0||!next.group_generation||next.first_member<0||!next.member_count||
           next.member_count>Capacity||next.member_count-1>std::size_t(std::numeric_limits<int>::max()-next.first_member))
            return CycleGroupConfigure::invalid;
        if(configured_&&next==shape_)return CycleGroupConfigure::retained;
        if(configured_&&next.group_generation==shape_.group_generation)return CycleGroupConfigure::changed;
        if(stamp_==std::numeric_limits<std::uint64_t>::max())return CycleGroupConfigure::invalid;
        if(!bump_route())return CycleGroupConfigure::invalid;
        ++stamp_;shape_=next;configured_=true;earliest_expiry_=-1;proof_count_=0;damage_events_=death_events_=0;return CycleGroupConfigure::configured;
    }

    // One conservative alarm invalidates ALL nomination-absence proofs in O(1)
    // by changing their era. Dated life/HP and effect history survive. Renewal
    // may leave an earlier conservative alarm; it never extends stale proof.
    constexpr bool refresh_time(Epoch now)noexcept {
        if(!configured_||!cycle_detail::valid(now)||cycle_detail::before(now,latest_time_))return false;
        latest_time_=now;
        if(earliest_expiry_<0||now.round<earliest_expiry_)return false;
        earliest_expiry_=-1;proof_count_=0;
        if(proof_era_==std::numeric_limits<std::uint64_t>::max()){exhausted_=true;configured_=false;route_revision_=0;return true;}
        ++proof_era_;bump_route();return true;
    }

    constexpr CycleGroupUpdate observe(const CampFact& member,Epoch now,CycleMemberAbsence absence={})noexcept {
        if(!configured_||!cycle_detail::valid(now))return CycleGroupUpdate::invalid;
        if(cycle_detail::before(now,latest_time_))return CycleGroupUpdate::stale;
        refresh_time(now);
        if(!configured_||!member.actual(now)||cycle_detail::before(now,member.epoch)||member.life==Life::unknown)
            return CycleGroupUpdate::invalid;
        if(member.group_id!=shape_.group_id||member.group_ordinal!=shape_.group_ordinal||!inside(member.member_ordinal))
            return CycleGroupUpdate::foreign;
        if(absence.actual&&(absence.group_generation!=shape_.group_generation||member.life!=Life::dead))
            return CycleGroupUpdate::invalid;
        auto& record=records_[index(member.member_ordinal)];
        const bool known=record.stamp==stamp_;
        if(known&&record.id!=member.id)return CycleGroupUpdate::foreign;
        if(known&&cycle_detail::before(member.epoch,date(record)))return CycleGroupUpdate::stale;
        const bool old_needed=member_needed(member.member_ordinal,now);
        const bool keep_absence=known&&record.life==Life::dead&&member.life==Life::dead&&
            record.absence_actual&&record.absence_era==proof_era_&&!absence.actual;
        const auto old_until=record.absence_until;
        record.stamp=stamp_;record.id=member.id;record.round=member.epoch.round;record.revision=member.epoch.revision;record.attempt=member.epoch.attempt;
        record.life=member.life;record.hp_actual=rank(member,record.hp_shield);
        record.absence_actual=member.life==Life::dead&&(absence.actual||keep_absence);
        record.absence_until=absence.actual?absence.absence_until:keep_absence?old_until:0;
        record.absence_era=record.absence_actual?proof_era_:0;
        if(record.absence_actual&&record.absence_until>now.round&&(earliest_expiry_<0||record.absence_until<earliest_expiry_))
            earliest_expiry_=record.absence_until;
        const bool new_needed=member_needed(member.member_ordinal,now);
        if(old_needed&&!new_needed)++proof_count_;
        else if(!old_needed&&new_needed&&proof_count_)--proof_count_;
        if(!proof_count_)earliest_expiry_=-1;
        if(old_needed!=new_needed)bump_route();
        return CycleGroupUpdate::observed;
    }

    // Unknown, expired and foreign-generation absence remain possible Work.
    // A matching actual death alone is NOT an eternal respawn exclusion. The
    // caller must supply a conservative finite absence_until from its actual
    // lifecycle/immutable timer witness. The bound is exclusive in round.
    constexpr bool member_needed(int ordinal,Epoch now)const noexcept {
        if(!configured_||!inside(ordinal)||!cycle_detail::valid(now))return true;
        if(earliest_expiry_>=0&&now.round>=earliest_expiry_)return true;
        const auto& record=records_[index(ordinal)];
        return record.stamp!=stamp_||record.life!=Life::dead||!record.absence_actual||record.absence_era!=proof_era_||
            cycle_detail::before(now,date(record))||now.round>=record.absence_until;
    }

    // O(1) current-frontier summary. Before a dated proof snapshot, or after
    // its conservative expiry alarm, every member remains possible. Zero is
    // an instruction to reobserve actual lifecycle, never Capture/completion.
    constexpr std::size_t possible_members(Epoch now)const noexcept {
        if(!configured_||!cycle_detail::valid(now)||cycle_detail::before(now,latest_time_)||
           (earliest_expiry_>=0&&now.round>=earliest_expiry_))return shape_.member_count;
        return shape_.member_count-proof_count_;
    }
    constexpr bool all_members_excluded(Epoch now)const noexcept {
        return configured_&&shape_.member_count&&possible_members(now)==0;
    }

    constexpr CycleRemaining remaining(Epoch now)const noexcept {
        CycleRemaining out{};out.configured=configured_;if(!configured_)return out;
        out.total=shape_.member_count;
        for(std::size_t i=0;i<shape_.member_count;++i){
            const auto& record=records_[i];
            if(record.stamp!=stamp_){++out.unseen;continue;}
            if(record.life==Life::dead)++out.observed_dead;else ++out.observed_living;
            if(!member_needed(shape_.first_member+int(i),now)){++out.excluded_dead;continue;}
            if(record.life!=Life::dead&&record.hp_actual&&same_world(date(record),now)&&!cycle_detail::before(now,date(record))){
                ++out.fresh_hp;out.known_hp_shield+=double(record.hp_shield);
            }
        }
        out.possible_members=out.total-out.excluded_dead;out.unknown_hp=out.possible_members-out.fresh_hp;
        out.all_remaining_hp_current=out.unknown_hp==0;return out;
    }

    // Only an exact accepted SAME-member before/after pair can establish a
    // Work effect. Switching targets, importing ordinary facts, START, team
    // totals and a cross-round observation cannot produce damage credit.
    constexpr CycleEffectUpdate observe_effect(const CycleMemberEffect& proof)noexcept {
        const auto& r=proof.work.receipt;const auto& owner=proof.work.job;
        if(!configured_||proof.group_generation!=shape_.group_generation||!owner.valid()||
           owner.group_id!=shape_.group_id||owner.group_ordinal!=shape_.group_ordinal||!inside(owner.member_ordinal)||
           r.job.group!=owner.group_ordinal||r.job.member!=owner.member_ordinal||r.job.target!=owner.member_id||
           !r.serial||(r.effect==EgressEffect::yes&&!r.accepted_prefix)||r.accepted_prefix>r.serial||
           (r.effect!=EgressEffect::unknown&&r.effect!=EgressEffect::no&&r.effect!=EgressEffect::yes)||
           (r.kind!=PrimitiveKind::basic&&r.kind!=PrimitiveKind::cast_unit&&r.kind!=PrimitiveKind::cast_ground))
            return {CycleEffectStatus::invalid};
        if(r.serial<last_effect_.serial)return {CycleEffectStatus::stale};
        if(r.serial==last_effect_.serial){
            const bool same=proof.group_generation==last_effect_.group_generation&&owner==last_effect_.job&&
                r.accepted_prefix==last_effect_.accepted_prefix&&r.epoch==last_effect_.before&&proof.after==last_effect_.after;
            return {same?CycleEffectStatus::duplicate:CycleEffectStatus::invalid};
        }
        if(!proof.exact_ack||r.effect!=EgressEffect::yes||!cycle_detail::valid(r.epoch)||!cycle_detail::valid(proof.after)||
           proof.after.round!=r.epoch.round||proof.after.revision<=r.epoch.revision||!cycle_detail::before(r.epoch,proof.after))
            return {CycleEffectStatus::unknown};
        if(cycle_detail::before(proof.after,latest_time_))return {CycleEffectStatus::stale};
        if(!matches(proof.before,owner)||!matches(proof.after_fact,owner))return {CycleEffectStatus::invalid};
        float before_rank{},after_rank{};
        if(!proof.before.living(r.epoch)||cycle_detail::before(r.epoch,proof.before.epoch)||!rank(proof.before,before_rank)||
           !proof.after_fact.actual(proof.after)||proof.after_fact.epoch!=proof.after||proof.after_fact.life==Life::unknown)
            return {CycleEffectStatus::unknown};
        const bool dead=proof.after_fact.life==Life::dead;
        const bool numeric=rank(proof.after_fact,after_rank);
        if(!dead&&(!proof.after_fact.living(proof.after)||!numeric))return {CycleEffectStatus::unknown};
        const auto& old=records_[index(owner.member_ordinal)];
        if(old.stamp==stamp_&&cycle_detail::before(proof.after,date(old)))return {CycleEffectStatus::stale};
        const auto observed=observe(proof.after_fact,proof.after,proof.absence);
        if(observed!=CycleGroupUpdate::observed)return {CycleEffectStatus::invalid};
        const bool decreased=numeric&&after_rank<before_rank;
        last_effect_={proof.group_generation,r.serial,r.accepted_prefix,owner,r.epoch,proof.after,
            before_rank,after_rank,numeric,dead};
        if(decreased)++damage_events_;if(dead)++death_events_;
        return {decreased||dead?CycleEffectStatus::applied:CycleEffectStatus::unchanged,decreased,dead};
    }

    constexpr bool configured()const noexcept{return configured_;}
    constexpr CycleGroupShape shape()const noexcept{return shape_;}
    constexpr std::uint64_t damage_events()const noexcept{return damage_events_;}
    constexpr std::uint64_t death_events()const noexcept{return death_events_;}
    constexpr CycleEffectRecord last_effect()const noexcept{return last_effect_;}
    constexpr std::uint64_t route_revision()const noexcept{return route_revision_;}
    constexpr int earliest_absence_expiry()const noexcept{return earliest_expiry_;}
    static constexpr std::size_t capacity=Capacity;

private:
    // The zero stamp makes all-zero constinit storage empty; no per-record
    // Epoch(-1) data or whole-array clear is needed for a new group/cycle.
    struct Record {
        std::uint64_t stamp{},revision{},attempt{},absence_era{};NativeId id{};int round{},absence_until{};
        float hp_shield{};Life life{};bool hp_actual{},absence_actual{};
    };
    std::array<Record,Capacity> records_{};CycleGroupShape shape_{};CycleEffectRecord last_effect_{};Epoch latest_time_{};
    std::uint64_t stamp_{},damage_events_{},death_events_{},route_revision_{},proof_era_{1};std::size_t proof_count_{};
    int earliest_expiry_{-1};bool configured_{},exhausted_{};
    constexpr bool bump_route()noexcept {
        if(route_revision_==std::numeric_limits<std::uint64_t>::max()){exhausted_=true;configured_=false;route_revision_=0;return false;}
        ++route_revision_;return true;
    }
    constexpr bool inside(int ordinal)const noexcept {
        return ordinal>=shape_.first_member&&std::size_t(ordinal-shape_.first_member)<shape_.member_count;
    }
    constexpr std::size_t index(int ordinal)const noexcept{return std::size_t(ordinal-shape_.first_member);}
    static constexpr Epoch date(const Record& record)noexcept{return {record.round,record.revision,record.attempt};}
    static constexpr bool matches(const CampFact& member,CycleJob owner)noexcept {
        return member.id==owner.member_id&&member.group_id==owner.group_id&&member.member_ordinal==owner.member_ordinal&&member.group_ordinal==owner.group_ordinal;
    }
    static constexpr bool rank(const CampFact& member,float& out)noexcept {
        if(!member.body_actual||!member.buffs_actual||member.hp<0||member.shield<0||
           !(member.hp<=std::numeric_limits<float>::max())||!(member.shield<=std::numeric_limits<float>::max()))return false;
        const auto value=member.hp+member.shield;
        if(!(value>=0&&value<=std::numeric_limits<float>::max()))return false;
        out=value;return true;
    }
};

} // namespace mosaic::native_service
