#pragma once
// A retained nominee is dated scheduling state, never an action grant. This
// entry dispatcher precedes ordinary Wait/acquisition in the parent scheduler.
// Every execution consumes a newly captured selected certificate in parent Ops.
#include "selected_transition.h"

namespace mosaic::continuation {
using Stamp=selected::Stamp;
using Command=transition::Command;
struct Context {
    std::uint32_t mode{};
    int group{-1};
    std::uint64_t group_cycle{},goal_generation{},receipt_generation{},body_generation{},channel_generation{};
    spatial::Token layout;
};
struct BodySignature {
    float max_hp{},strength{},focus{};
    int range{},move_speed{};
    cambc::passive_type passive{};
    bool alive{},stunned{},recalling{},submitting{},teleporting{};
    bool operator==(const BodySignature&) const=default;
};
struct Binding {
    int actor{-1},bag{};
    cambc::vec2 origin{};
    BodySignature body;
    Context context;
};
inline Binding make_binding(int actor,const frame::Self& self,Context context) {
    return {actor,self.gems,self.pos,
        {self.max_hp,self.strength,self.focus,self.range,self.move_speed,self.passive,
         self.alive,self.stunned,self.recalling,self.submitting,self.teleporting},context};
}
inline bool valid(const Binding& value) {
    const auto& b=value.body;
    return value.actor>=0&&value.bag>=0&&b.alive&&!b.stunned&&!b.recalling&&!b.submitting&&!b.teleporting&&
        b.range>=0&&b.move_speed>=0&&std::isfinite(b.max_hp)&&b.max_hp>0&&
        std::isfinite(b.strength)&&std::isfinite(b.focus)&&value.context.layout.valid(value.context.layout.count);
}
inline bool same_binding(const Binding& a,const Binding& b) {
    const auto& x=a.context;const auto& y=b.context;
    return valid(a)&&valid(b)&&a.actor==b.actor&&a.bag==b.bag&&a.origin==b.origin&&a.body==b.body&&
        x.mode==y.mode&&x.group==y.group&&x.group_cycle==y.group_cycle&&x.goal_generation==y.goal_generation&&
        x.receipt_generation==y.receipt_generation&&x.body_generation==y.body_generation&&
        x.channel_generation==y.channel_generation&&selected::same_token(x.layout,y.layout);
}
inline bool supported(const Command& command) {
    return command.kind==transition::Kind::move||
        (command.kind==transition::Kind::basic&&command.target_kind==transition::TargetKind::camp&&command.target_id>=0);
}
enum class Phase {empty,need_nominee,ready};
enum class Attempt {not_attempted,refused,applied};
enum class Outcome {replan,deferred,refused,applied};
struct Report {
    Outcome outcome{Outcome::replan};
    bool captured{},attempted{};
    // Only the pre-attempt current capture's origin proof. Cleared on any
    // attempted primitive or detected loss of the captured input authority.
    selected::OriginWait origin_wait;
};
class Pending {
    Phase phase_{Phase::empty};
    Binding binding_;
    Command command_;
    int nominated_epoch_{-1};
    template<class Ops> friend Report dispatch_first(Pending&,Stamp,const Binding&,Ops&);
public:
    Phase phase() const{return phase_;}
    bool active() const{return phase_!=Phase::empty;}
    void clear(){phase_=Phase::empty;binding_={};command_={};nominated_epoch_=-1;}
    bool retain(Command command,const Binding& binding,int epoch) {
        clear();if(!supported(command)||!valid(binding)||epoch<0)return false;
        if(command.kind==transition::Kind::move&&command.destination==binding.origin)return false;
        command_=command;binding_=binding;nominated_epoch_=epoch;phase_=Phase::ready;return true;
    }
    bool retain_nominee(const Binding& binding,int epoch) {
        clear();if(!valid(binding)||epoch<0)return false;
        binding_=binding;nominated_epoch_=epoch;phase_=Phase::need_nominee;return true;
    }
};

// Ops is the real parent boundary, also exercised by the fixture:
// current(Stamp), nominate(Binding)->optional<Command>, request(Command)->Request,
// capture(Request)->Assessment& (parent-owned), packet()->const Packet&,
// context(Packet)->Context, valid_target(Command,Binding),
// attempt(Assessment&,Request)->Attempt. nominate reads ONLY immutable cached
// route/draft values; it must not compute a field or query fresh game state.
// current checks actual clock/actor/revision coherence, NOT a fuel-admission
// reserve. A same-input fuel refusal belongs in attempt -> not_attempted; it
// must not masquerade as a crossed input and erase the retained raw nominee.
// context supplies current private mode/cycle/goal/receipt identities. Body,
// bag and origin are derived here directly from the freshly captured Self.
// Current HP/readiness are intentionally not proposal identity: the new selected
// assessment must price them, so damage never reuses an earlier health grant.
// On Replan the parent reuses its capture or acquires fresh Wait authority; this
// report is never permission for an unpriced physical yield.
template<class Ops>
Report dispatch_first(Pending& pending,Stamp stamp,const Binding& dated,Ops& ops) {
    Report report;
    selected::Assessment* captured_assessment=nullptr;
    auto finish=[&]() {
        if(report.captured&&!ops.current(stamp)) {
            report.origin_wait={};
            if(!report.attempted) {
                pending.clear();report.outcome=Outcome::replan;
                if(captured_assessment)captured_assessment->certificate.reset();
            }
        }
        return report;
    };
    auto replan=[&]() {pending.clear();report.outcome=Outcome::replan;return finish();};
    if(!pending.active())return finish();
    if(!stamp.matches(stamp)||stamp.actor!=pending.binding_.actor||stamp.epoch<pending.nominated_epoch_||
        !same_binding(pending.binding_,dated)||!ops.current(stamp))return replan();
    if(pending.phase_==Phase::need_nominee) {
        const auto nominee=ops.nominate(pending.binding_);
        if(!nominee||!pending.retain(*nominee,dated,pending.nominated_epoch_))return replan();
    }
    if(!ops.valid_target(pending.command_,pending.binding_))return replan();
    auto command=pending.command_;
    command.expected_epoch=stamp.epoch;command.expected_actor=stamp.actor;command.expected_revision=stamp.revision;
    const auto request=ops.request(command);
    if(!selected::same_command(request.command,command)||!ops.current(stamp))return replan();
    auto& assessment=ops.capture(request);captured_assessment=&assessment;report.captured=true;
    if(!ops.current(stamp)){assessment.certificate.reset();return replan();} // Revokes even origin proof.
    const auto& packet=ops.packet();
    if(!packet.stamp.matches(stamp)||!selected::same_request(packet.request,request)) {
        assessment.certificate.reset();return replan();
    }
    report.origin_wait=assessment.result.origin_wait;
    if(!packet.self_complete){report.outcome=Outcome::deferred;return finish();}
    const auto actual=make_binding(stamp.actor,packet.self,ops.context(packet));
    if(!same_binding(pending.binding_,actual)||!ops.valid_target(command,actual)) {
        assessment.certificate.reset();return replan();
    }
    if(!packet.matches(stamp,request)) {
        assessment.certificate.reset();
        report.outcome=Outcome::deferred;return finish();
    }
    // Every complete no-grant result, including model-incomplete evidence, must
    // replan. Only a partial capture retains its raw nominee for a fresh retry.
    // A current origin proof may survive; it grants no implicit physical yield.
    if(!assessment.certificate)return replan();
    if(!ops.current(stamp))return replan();
    const auto saved=pending;
    pending.clear(); // Includes refusal/error/exception paths, before any SDK attempt.
    const auto attempt=ops.attempt(assessment,request);
    if(attempt==Attempt::applied){report.origin_wait={};report.attempted=true;report.outcome=Outcome::applied;return finish();}
    if(attempt==Attempt::refused){report.origin_wait={};report.attempted=true;report.outcome=Outcome::refused;return finish();}
    // A certificate may have been burned without an SDK call. Retain only the
    // nominee; a later entry still needs a new request, capture and certificate.
    if(ops.current(stamp)&&packet.matches(stamp,request)&&
        same_binding(saved.binding_,make_binding(stamp.actor,packet.self,ops.context(packet)))&&
        ops.valid_target(command,saved.binding_))pending=saved;
    if(!pending.active())report.origin_wait={};
    report.outcome=pending.active()?Outcome::deferred:Outcome::replan;
    return finish();
}
} // namespace mosaic::continuation
