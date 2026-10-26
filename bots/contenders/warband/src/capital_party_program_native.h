#pragma once
#include "capital_party_program.h"
#include "capital_party_wire.h"
#include <array>
#include <optional>
#include <span>
#ifndef WB_PARTY_PROGRAM_NATIVE_HOST_FREE
#include "capital_party_native.h"
#endif

// Adaptive programme transport/execution. This supplies no canonical group,
// rendezvous, utility estimate, risk certificate or pricing truth. Runtime must
// supply those from its actual public geometry, current packets and complete
// independent interpreters. In particular a nearest-target NPC quote does not
// constrain a player's target choice.
namespace wb::capital::party_program_native {
namespace q=party_program;namespace p=party;namespace w=party_wire;
inline constexpr unsigned max_steps=6,max_receipts=p::max_effects;
enum class Status {Ready,NoOffer,Prestart,NeedObservation,NeedModel,NeedFuel,Stale,Pending,Refused,Replan,Aborting,Handoff,Complete,Invalid};
enum class PrimitiveKind {Move,Basic,Shield,Reset,Heal,Dash,Submit,Recall};
struct Primitive {PrimitiveKind kind{PrimitiveKind::Move};p::Point destination{};int target{-1},slot{-1};bool operator==(const Primitive&)const=default;};
// Invocation-bound resource model supplied by the native measurement owner.
// Costs include the nested guards, host/query work and reconciliation, not
// merely the primitive count. Unknown/default prices admit NO physical word.
struct Price {q::Revision model{},domain{};std::array<unsigned,max_steps> guard{},command{},readback{};unsigned exit_tail{};bool bounded{};
    // Explicit measurement admission, never a zero-cost bound. Only one
    // primitive is allowed, with current fuel fences and actual readback.
    bool measure_now{};};
struct Word {q::Epoch source{};q::Revision contract{},token{};p::Cast selected_cast{p::Cast::None};std::array<Primitive,max_steps> steps{};unsigned count{};int camp_target{-1};Price price{};};
struct Guard {q::Epoch source{};q::Revision word{};unsigned step{};bool legal{},remaining_model_current{},refusal_closure{},funded_tail{};};
// Actual acceptance by the existing complete independent interpreter. This is
// operational service handoff, NOT a receipt for future farming or score.
struct Independent {q::Epoch source{};q::Revision grant{},programme{};bool complete{},funded{},accepted{};};
struct Fence {void* context{};bool(*permit)(void*){};bool operator()()const{return permit&&permit(context);}};
struct Admission {q::Admission programme{};bool trusted_milestone_producer{},final_fence_one_command{};bool valid()const{return programme.valid()&&trusted_milestone_producer&&final_fence_one_command;}};
struct Options {unsigned observe_reserve{35000},command_reserve{25000},wire_reserve{10000};bool allow_measure_now{},allow_paid_exit_handoff{};};
enum class Stage {Idle,Entry,Unpriced,Unfunded,Guard,Command,Readback,Done,Interrupted};
struct FuelMark {unsigned remaining{};int round{-1};bool known{};};
struct FuelInterval {FuelMark before{},after{};unsigned cost{};bool known{};};
struct Telemetry {Stage stage{Stage::Idle};q::Epoch source{};q::Revision price_model{};std::uint64_t entries{},unpriced{},unfunded{},guard_overruns{},command_overruns{},readback_overruns{},attempts{};
    unsigned entry_fuel{},required{},precommand_fuel{},postreadback_fuel{},step{};bool postreadback_current{};
    Word measured_word{};p::Body measured_before{},measured_after{};std::array<FuelInterval,max_steps> guards{},commands{},readbacks{};};
struct Result {Status status{Status::NoOffer};q::Status programme{q::Status::NoOffer};w::Status wire{w::Status::Invalid};bool claimed{},attempted{},accepted{},after_known{},unknown_effect{},physical_released{};unsigned accepted_steps{},actual_receipts{};q::Frame after{};};
struct Prefix {std::array<q::Actual,max_steps*max_receipts> receipts{};unsigned count{};bool unknown_effect{};q::Epoch last_actual{};};
inline w::Frame source(const q::Epoch& e){return {e.owner,e.round,e.input,true};}
inline p::Milestone milestone(w::Phase phase){return static_cast<p::Milestone>(phase);}
inline bool compatible(const q::Offer& o,const Word& word,const Primitive& primitive){
    switch(primitive.kind){
    case PrimitiveKind::Basic:return ((o.kind==q::Kind::WorkTank||o.kind==q::Kind::Strike)&&primitive.target==o.target)
        ||(o.kind==q::Kind::Heal&&word.camp_target>=0&&primitive.target==word.camp_target);
    case PrimitiveKind::Shield:return o.kind==q::Kind::WorkTank&&word.selected_cast==p::Cast::Shield;
    case PrimitiveKind::Reset:return (o.kind==q::Kind::WorkTank||o.kind==q::Kind::Strike)&&word.selected_cast==p::Cast::Reset;
    case PrimitiveKind::Heal:return o.kind==q::Kind::Heal&&primitive.target==o.target&&word.selected_cast==p::Cast::Heal;
    case PrimitiveKind::Dash:return word.selected_cast==p::Cast::Dash&&o.kind!=q::Kind::HoldChannel&&o.kind!=q::Kind::HoldRecovery;
    case PrimitiveKind::Submit:return o.kind==q::Kind::Bank&&primitive.destination==o.destination;
    case PrimitiveKind::Recall:return o.kind==q::Kind::Recover;
    case PrimitiveKind::Move:return o.kind!=q::Kind::PublishCommit&&o.kind!=q::Kind::Heal&&o.kind!=q::Kind::HoldChannel&&o.kind!=q::Kind::HoldRecovery;
    }return false;
}
inline bool bound(const q::Offer& o,const q::Quote& quote,const Word& word){
    if(o.source!=word.source||o.source!=quote.source||o.contract!=word.contract||o.contract!=quote.contract||quote.offer_serial!=o.serial||word.token!=quote.word||!word.token||word.count>max_steps)return false;
    if(o.kind==q::Kind::Heal){
        const bool heal_only=word.count==1&&word.steps[0].kind==PrimitiveKind::Heal&&word.camp_target==-1&&quote.camp_target==-1;
        const bool heal_basic=word.count==2&&word.steps[0].kind==PrimitiveKind::Heal&&word.steps[1].kind==PrimitiveKind::Basic
            &&word.camp_target>=0&&quote.camp_target==word.camp_target&&word.steps[1].target==word.camp_target;
        if(!heal_only&&!heal_basic)return false;
    }else if(word.camp_target!=-1||quote.camp_target!=-1)return false;
    unsigned casts=0,moves=0;for(unsigned i=0;i<word.count;++i){if(!compatible(o,word,word.steps[i]))return false;
        casts+=word.steps[i].kind!=PrimitiveKind::Move&&word.steps[i].kind!=PrimitiveKind::Basic&&word.steps[i].kind!=PrimitiveKind::Submit&&word.steps[i].kind!=PrimitiveKind::Recall;
        moves+=word.steps[i].kind==PrimitiveKind::Move;}
    return casts<=1&&moves<=1;
}
inline bool changed(const q::Epoch& a,const q::Epoch& b){return a.owner!=b.owner||a.round!=b.round||a.input!=b.input||a.life!=b.life||a.foes!=b.foes||a.locks!=b.locks||a.order!=b.order;}
inline const p::Camp* camp(const q::Frame& f,int id){for(unsigned i=0;i<f.group.count&&i<p::max_camps;++i)if(f.group.camps[i].id==id)return &f.group.camps[i];return nullptr;}
inline const p::Body* peer(const q::Frame& f,int id){if(f.self.id==id)return &f.self;for(const auto& x:f.peers)if(x.observed_now&&x.body.id==id)return &x.body;return nullptr;}
inline bool effect(const q::Contract& c,const q::Lease& lease,const q::Frame& before,const q::Frame& after,const q::Actual& a){
    if(!a.observed_actual||!a.accepted||!a.event||a.contract!=c.id||a.life!=c.life||a.actor!=before.self.id||a.word!=lease.word)return false;
    if(a.kind==q::ReceiptKind::CampDamage){const auto* x=camp(before,a.target);const auto* y=camp(after,a.target);
        return a.group==c.group&&x&&y&&x->observed_now&&y->observed_now&&x->hp==a.before&&y->hp==a.after&&a.before>a.after;}
    if(a.kind==q::ReceiptKind::Shield)return a.target==before.self.id&&before.self.shield==a.before&&after.self.shield==a.after&&a.after>a.before;
    if(a.kind==q::ReceiptKind::Heal){const auto* x=peer(before,a.target);const auto* y=peer(after,a.target);return x&&y&&x->hp==a.before&&y->hp==a.after&&a.after>a.before;}
    return a.kind==q::ReceiptKind::None;
}

// Host contract (same in native and fixtures): owner/round/fuel, observe(Epoch,
// Contract,Frame&), read_words(array<uint16_t,4>&), write_word(offset,value,Fence)
// -> issued, guard(Offer,Word,index,Frame,Guard&), execute(Primitive,Fence,
// bool&accepted)->issued, effects(before,after,Primitive,accepted,lease,span<Actual>,
// unsigned&count). Query methods must leave uncertainty explicit. Both physical
// write methods prepare ALL arguments before Fence and execute exactly ONE raw
// SDK command after Fence(TRUE), with NO intervening SDK import. Exceptions
// propagate to the caller's turn-boundary error/yield; pending authority stays.
template<class Host> class Session {
    Host& host_;q::Contract contract_;int owner_;Admission admission_;Options options_;q::Program program_;w::Endpoint endpoint_;
    q::Epoch epoch_{};bool begun_{},wire_owned_{},claimed_{},command_pending_{},word_pending_{},wire_attempted_{},protocol_fault_{},programme_current_{};
    std::optional<w::Write> wire_pending_{};w::View view_{};q::Frame actual_{};bool observed_{};
    q::Lease lease_{};Word word_{};unsigned step_{};Guard guard_{};Prefix prefix_{};bool fence_passed_{};
    Telemetry telemetry_{};
    bool current(){return begun_&&host_.owner()==epoch_.owner&&host_.round()==epoch_.round;}
    bool budget(unsigned reserve){return current()&&host_.fuel()>=reserve&&current();}
    FuelMark measure(){const int round_before=host_.round(),owner_before=host_.owner();const auto fuel=host_.fuel();
        const int owner_after=host_.owner(),round_after=host_.round();return {fuel,round_after,owner_before==owner_after&&owner_after==epoch_.owner
            &&round_before==round_after&&round_after==epoch_.round};}
    static FuelInterval interval(FuelMark before,FuelMark after){const bool known=before.known&&after.known&&before.round==after.round&&after.remaining<=before.remaining;
        return {before,after,known?before.remaining-after.remaining:0,known};}
    unsigned required(unsigned step,bool include_guard)const{
        if(word_.price.measure_now){const std::uint64_t readback=word_.price.readback[0]?word_.price.readback[0]:options_.observe_reserve;
            const auto need=std::max<std::uint64_t>(options_.observe_reserve,readback+options_.command_reserve);return need>~0u?~0u:unsigned(need);}
        std::uint64_t need=std::max(word_.price.exit_tail,options_.command_reserve);
        for(unsigned i=word_.count;i>step;){--i;need+=word_.price.readback[i];need=std::max<std::uint64_t>(need,options_.observe_reserve);
            need+=word_.price.command[i];if(i!=step||include_guard)need+=word_.price.guard[i];if(need>~0u)return ~0u;}
        return static_cast<unsigned>(need);
    }
    bool price_valid(const Word& word,const q::Quote& quote)const{
        if(word.price.measure_now)return options_.allow_measure_now&&word.count==1&&!word.price.bounded&&word.price.model
            &&word.price.domain==quote.domain.id&&word.price.exit_tail>=options_.command_reserve&&!word.price.guard[0]&&!word.price.command[0];
        if(!word.price.bounded||!word.price.model||word.price.domain!=quote.domain.id||word.price.exit_tail<options_.command_reserve)return false;
        for(unsigned i=0;i<word.count;++i)if(!word.price.guard[i]||!word.price.command[i]||!word.price.readback[i])return false;return true;
    }
    static bool physical_fence(void* context){auto& s=*static_cast<Session*>(context);
        // The guard/setup already paid is excluded from this remaining suffix.
        // Preserve the unchanged full-observation entrance floor and the final
        // command/exit reserve ONCE, not a whole-word setup at every primitive.
        if(!s.command_pending_||!s.word_pending_||!s.observed_||!s.programme_current_||s.guard_.source!=s.actual_.source||s.guard_.word!=s.word_.token||s.guard_.step!=s.step_
            ||!s.guard_.legal||!s.guard_.remaining_model_current||!s.guard_.refusal_closure||!s.guard_.funded_tail||!s.current())return false;
        const auto fuel=s.host_.fuel();if(!s.current()||fuel<s.required(s.step_,false))return false;
        s.telemetry_.precommand_fuel=fuel;s.telemetry_.step=s.step_;s.telemetry_.stage=Stage::Command;++s.telemetry_.attempts;
        if(s.word_.price.measure_now)s.telemetry_.commands[s.step_].before={fuel,s.epoch_.round,true};
        s.fence_passed_=true;return true;}
    static bool wire_fence(void* context){auto& s=*static_cast<Session*>(context);unsigned reserve=s.options_.wire_reserve;
        if(s.word_pending_){if(reserve>~0u-s.options_.observe_reserve)return false;reserve+=s.options_.observe_reserve;}
        if(!s.wire_pending_||!s.budget(reserve))return false;s.fence_passed_=true;return true;}
    Status read(w::Snapshot& snapshot){if(!budget(options_.wire_reserve))return current()?Status::NeedFuel:Status::Stale;
        snapshot={};snapshot.source=source(epoch_);host_.read_words(snapshot.words);if(!current())return Status::Stale;
        const auto h=w::decode_header(snapshot.words[0]);snapshot.canonical_contract=admission_.programme.canonical&&h.start==contract_.start&&h.group==contract_.group;snapshot.whole_read=true;return Status::Ready;}
    Status capture(q::Frame& out){if(!budget(options_.observe_reserve))return current()?Status::NeedFuel:Status::Stale;
        out={};if(!host_.observe(epoch_,contract_,out)||!current())return current()?Status::NeedObservation:Status::Stale;
        const auto role=q::detail::member(contract_,owner_);
        if(role<0||!out.fresh||!out.source.sample||out.source.owner!=epoch_.owner||out.source.round!=epoch_.round||out.source.input!=epoch_.input
            ||out.self.id!=owner_||out.self.role!=contract_.members[static_cast<unsigned>(role)].role||!p::detail::body(out.self))return Status::NeedObservation;
        w::Snapshot snapshot;const auto status=read(snapshot);if(status!=Status::Ready)return status;view_=endpoint_.observe(snapshot,source(epoch_));
        out.protocol_armed=view_.armed&&view_.status==w::Status::Active&&!view_.abort_seen;out.canonical_header=snapshot.canonical_contract;
        for(unsigned i=0;i<p::actors;++i){const auto& claim=view_.claims[i];out.commitment_current[i]=claim.status==w::ClaimStatus::DatedPayload&&claim.payload.predicate
            &&claim.payload.phase>=w::Phase::Committed&&!w::abort_branch(claim.payload.phase)&&!view_.abort_seen;}
        actual_=out;observed_=true;programme_current_=false;return Status::Ready;}
    void import(){if(!observed_||!actual_.canonical_header||!view_.armed||view_.status!=w::Status::Active)return;
        if(view_.abort_seen)protocol_fault_=false;
        for(unsigned i=0;i<p::actors;++i){const auto& claim=view_.claims[i];if(claim.actor==owner_||claim.status!=w::ClaimStatus::DatedPayload)continue;
            if(claim.payload.phase==w::Phase::CleanupDone&&claim.payload.predicate&&!needs_reconciliation()){
                (void)program_.cleanup(claim.actor,contract_.id,admission_.trusted_milestone_producer,true);continue;}
            if(view_.abort_seen&&!w::abort_branch(claim.payload.phase))continue;
            p::TrustedPredicateReceipt r;r.contract=contract_.id;r.actor=claim.actor;r.role=static_cast<p::Role>(i);r.source_round=claim.payload.round;r.milestone=milestone(claim.payload.phase);r.predicate=claim.payload.predicate;
            r.read_owner=owner_;r.read_round=epoch_.round;r.read_input=epoch_.input;r.header_start=view_.header.start;r.header_group=view_.header.group;
            r.active_armed=view_.armed;r.whole_read=true;r.trusted_partition_and_producer=admission_.trusted_milestone_producer;
            const auto status=program_.ingest(r);if(status==p::IngestStatus::Conflict||status==p::IngestStatus::Invalid||status==p::IngestStatus::Unverified)protocol_fault_=true;}
        if(!program_.wire_owned())wire_owned_=false;}
    Result finish_wire(const w::Write& prepared){Result out;out.claimed=claimed_;
        if(!prepared.prepared){out.status=Status::NoOffer;out.wire=prepared.status;return out;}
        wire_pending_=prepared;wire_owned_=claimed_=out.claimed=true;fence_passed_=false;wire_attempted_=true;
        const bool issued=host_.write_word(prepared.byte_offset,prepared.word,{this,&wire_fence});
        if(!issued){wire_attempted_=false;(void)endpoint_.discard_unissued(prepared);wire_pending_.reset();out.status=Status::NeedModel;return out;}
        out.attempted=true;if(!fence_passed_){out.status=Status::Invalid;return out;}
        if(!current()){out.status=Status::Pending;return out;}w::Snapshot post;out.status=read(post);if(out.status!=Status::Ready){out.status=Status::Pending;return out;}
        if(prepared.kind==w::WriteKind::Header){view_=endpoint_.confirm_proposal(prepared,post,source(epoch_));out.wire=view_.status;out.accepted=view_.armed;}
        else{out.wire=endpoint_.confirm_record(prepared,post,source(epoch_));out.accepted=out.wire==w::Status::Published;view_=endpoint_.observe(post,source(epoch_));}
        wire_pending_.reset();wire_attempted_=false;out.status=out.accepted?Status::Complete:Status::NeedModel;return out;}
    Result stop(q::Failure failure,Status status,Result out={}){out.claimed=claimed_;out.after=actual_;out.after_known=observed_;
        if(word_pending_){out.programme=program_.interrupt(lease_,actual_,failure);programme_current_=out.programme==q::Status::Ready||out.programme==q::Status::Aborting;
            if(out.programme==q::Status::NeedObservation||out.programme==q::Status::Stale){out.status=Status::Pending;return out;}
            word_pending_=command_pending_=false;out.physical_released=true;}
        out.status=status;return out;}
public:
    Session(Host& host,const q::Contract& canonical,int owner,Admission admission,Options options={})
        :host_(host),contract_(canonical),owner_(owner),admission_(admission),options_(options),program_(canonical,owner,admission.programme),
        endpoint_(w::Roles{{canonical.members[0].id,canonical.members[1].id,canonical.members[2].id},admission.programme.accepted_roles,admission.programme.exclusive_wire,admission.programme.legacy_publish_disabled},owner),wire_owned_(admission.valid()){}
    const q::Program& program()const{return program_;}const Prefix& retained_prefix()const{return prefix_;}const q::Frame& actual()const{return actual_;}
    const Telemetry& telemetry()const{return telemetry_;}
    bool own_abort_visible()const{const auto role=q::detail::member(contract_,owner_);if(!observed_||role<0||!view_.armed||view_.status!=w::Status::Active
            ||view_.header.start!=contract_.start||view_.header.group!=contract_.group)return false;
        const auto& claim=view_.claims[static_cast<unsigned>(role)];return claim.actor==owner_&&claim.status==w::ClaimStatus::DatedPayload
            &&claim.payload.phase==w::Phase::Abort&&claim.payload.predicate;}
    bool wire_owned()const{return wire_owned_;}bool claimed()const{return claimed_;}bool needs_reconciliation()const{return wire_pending_.has_value()||word_pending_||command_pending_;}
    Status begin(q::Epoch next){if(!admission_.valid()||!q::detail::valid(contract_)||contract_.require_useful_support||contract_.capture_mask!=7||contract_.camp_count>max_receipts||!next.input||next.owner!=owner_)return Status::NoOffer;
        if(begun_&&(next.round<epoch_.round||(next.round==epoch_.round&&next.input!=epoch_.input)))return Status::Stale;
        epoch_=next;begun_=true;observed_=programme_current_=false;if(!current())return Status::Stale;
        if(!needs_reconciliation())claimed_=false;return needs_reconciliation()?Status::Pending:Status::Ready;}
    Status observe(){q::Frame frame;const auto captured=capture(frame);if(captured!=Status::Ready)return captured;
        if(view_.status==w::Status::ArmedFuture)return Status::Prestart;
        if(!frame.protocol_armed&&!word_pending_&&!program_.wire_owned())return Status::NoOffer;
        const auto status=program_.observe(frame);programme_current_=status==q::Status::Ready||status==q::Status::AwaitActual;
        if(status==q::Status::Stale)return Status::Stale;if(status==q::Status::NeedObservation)return Status::NeedObservation;if(status==q::Status::NoOffer)return Status::NoOffer;
        if(!word_pending_)import();return protocol_fault_?Status::NeedModel:Status::Ready;}
    // Future arming is only a fenced actual whole-wire read. It does not need
    // invented group life/HP, nor does it grant a programme/action permission.
    Status arm(){w::Snapshot snapshot;const auto status=read(snapshot);if(status!=Status::Ready)return status;view_=endpoint_.observe(snapshot,source(epoch_));
        if(view_.status==w::Status::ArmedFuture)return Status::Prestart;return view_.status==w::Status::Active?Status::Ready:Status::NoOffer;}
    Result propose(bool reset_admitted){Result out;if(needs_reconciliation()){out.status=Status::Pending;return out;}w::Snapshot snapshot;out.status=read(snapshot);if(out.status!=Status::Ready)return out;
        const auto prepared=endpoint_.prepare_proposal(snapshot,source(epoch_),static_cast<unsigned>(contract_.group),{admission_.programme.canonical,reset_admitted});
        if(prepared.prepared&&w::decode_header(prepared.word).start!=contract_.start){(void)endpoint_.discard_unissued(prepared);out.status=Status::NeedModel;return out;}return finish_wire(prepared);}
    q::Result nominate(){if(needs_reconciliation()||!observed_||!programme_current_)return {};if(protocol_fault_){q::Result out;out.status=q::Status::NeedModel;return out;}return program_.next();}
    bool receipt(const q::Actual& a){return observed_&&programme_current_&&!needs_reconciliation()&&a.word==0&&program_.receipt(a);}
    Result execute(const q::Offer& offer,const q::Quote& quote,const Word& word){Result out;
        if(needs_reconciliation()){out.status=Status::Pending;return out;}if(protocol_fault_){out.status=Status::NeedModel;return out;}if(!observed_||!programme_current_||!bound(offer,quote,word)||actual_.source!=offer.source){out.status=Status::Stale;return out;}
        word_=word;++telemetry_.entries;telemetry_.source=offer.source;telemetry_.stage=Stage::Entry;telemetry_.entry_fuel=host_.fuel();telemetry_.price_model=word.price.model;
        if(word.price.measure_now){telemetry_.measured_word=word;telemetry_.measured_before=actual_.self;telemetry_.measured_after={};telemetry_.guards={};telemetry_.commands={};telemetry_.readbacks={};}
        if(!current()){out.status=Status::Stale;return out;}
        if(word.count&&!price_valid(word,quote)){++telemetry_.unpriced;telemetry_.stage=Stage::Unpriced;out.status=Status::NeedModel;return out;}
        telemetry_.required=word.count?required(0,true):0;
        if(word.count&&telemetry_.entry_fuel<telemetry_.required){++telemetry_.unfunded;telemetry_.stage=Stage::Unfunded;out.status=Status::NeedFuel;return out;}
        const auto sealed=program_.seal(offer,quote);out.programme=sealed.status;if(sealed.status!=q::Status::Ready){out.status=Status::NeedModel;return out;}
        lease_=sealed.lease;word_=word;prefix_.count=0;prefix_.unknown_effect=false;prefix_.last_actual={};word_pending_=true;step_=0;
        if(!program_.begin_attempt(lease_)){word_pending_=false;out.status=Status::Stale;return out;}claimed_=out.claimed=true;
        if(offer.kind==q::Kind::PublishCommit){w::Snapshot snapshot;out.status=read(snapshot);if(out.status!=Status::Ready)return out;
            const auto write=endpoint_.prepare_record(snapshot,source(epoch_),w::Phase::Committed,true);out=finish_wire(write);
            if(out.status!=Status::Complete){if(!wire_pending_)return stop(q::Failure::None,Status::NeedModel,out);return out;}q::Frame after;if(capture(after)!=Status::Ready){out.status=Status::Pending;return out;}
            q::Actual a;a.kind=q::ReceiptKind::CommitPublished;a.event=host_.event();a.contract=contract_.id;a.life=contract_.life;a.word=lease_.word;a.actor=owner_;a.observed_actual=a.accepted=a.complete_word=true;
            out.programme=program_.reconcile(lease_,after,a);programme_current_=out.programme==q::Status::Ready||out.programme==q::Status::Aborting;word_pending_=false;out.physical_released=true;out.after=after;out.after_known=true;return out;}
        bool actual_heal=false;
        for(;step_<word_.count;++step_){
            if(offer.kind==q::Kind::Heal&&word_.steps[step_].kind==PrimitiveKind::Basic&&!actual_heal)return stop(q::Failure::None,Status::Replan,out);
            telemetry_.stage=Stage::Guard;telemetry_.step=step_;const auto guard_start=host_.fuel();if(!current()||guard_start<required(step_,true))return stop(q::Failure::None,Status::NeedFuel,out);
            FuelMark guard_mark;if(word_.price.measure_now){guard_mark=measure();telemetry_.guards[step_].before=guard_mark;}
            guard_={};const bool guarded=host_.guard(offer,word_,step_,actual_,guard_);
            if(word_.price.measure_now)telemetry_.guards[step_]=interval(guard_mark,measure());
            if(!guarded||!current()||guard_.source!=actual_.source||!guard_.legal||!guard_.remaining_model_current||!guard_.refusal_closure||!guard_.funded_tail)return stop(q::Failure::None,Status::Replan,out);
            const auto guard_end=host_.fuel();if(!word_.price.measure_now&&guard_start>=guard_end&&guard_start-guard_end>word_.price.guard[step_]){++telemetry_.guard_overruns;return stop(q::Failure::Interrupted,Status::NeedFuel,out);}
            const auto before=actual_;bool accepted=false;command_pending_=true;fence_passed_=false;
            const bool issued=host_.execute(word_.steps[step_],{this,&physical_fence},accepted);
            if(!issued){command_pending_=false;return stop(q::Failure::None,Status::Replan,out);}out.attempted=true;
            if(!fence_passed_){out.status=Status::Invalid;return out;}if(!current()){out.status=Status::Pending;return out;}
            bool cost_overrun=false;const auto command_end=host_.fuel();
            FuelMark command_mark;if(word_.price.measure_now){command_mark=measure();telemetry_.commands[step_]=interval(telemetry_.commands[step_].before,command_mark);telemetry_.readbacks[step_].before=command_mark;}
            if(!word_.price.measure_now&&telemetry_.precommand_fuel>=command_end&&telemetry_.precommand_fuel-command_end>word_.price.command[step_]){++telemetry_.command_overruns;cost_overrun=true;}
            telemetry_.stage=Stage::Readback;q::Frame after;const auto observed=capture(after);if(observed!=Status::Ready){out.status=Status::Pending;return out;}command_pending_=false;
            std::array<q::Actual,max_receipts> effects{};unsigned count=0;
            if(!host_.effects(before,after,word_.steps[step_],accepted,lease_,effects,count)||count>effects.size()){prefix_.unknown_effect=true;out.status=Status::Pending;command_pending_=true;return out;}
            for(unsigned i=0;i<count;++i)if(!effect(contract_,lease_,before,after,effects[i])){prefix_.unknown_effect=true;out.status=Status::Pending;command_pending_=true;return out;}
            if(accepted){out.programme=program_.prefix(lease_,after,std::span<const q::Actual>(effects.data(),count));
                programme_current_=out.programme==q::Status::Ready;
                if(out.programme!=q::Status::Ready)return stop(q::Failure::Interrupted,Status::NeedModel,out);
                for(unsigned i=0;i<count;++i){if(prefix_.count==prefix_.receipts.size())return stop(q::Failure::Interrupted,Status::NeedModel,out);prefix_.receipts[prefix_.count++]=effects[i];}out.actual_receipts+=count;}
            if(accepted&&word_.steps[step_].kind==PrimitiveKind::Heal)for(unsigned i=0;i<count;++i)
                actual_heal|=effects[i].kind==q::ReceiptKind::Heal&&effects[i].target==offer.target&&effects[i].after>effects[i].before;
            telemetry_.postreadback_fuel=host_.fuel();telemetry_.postreadback_current=current();
            if(word_.price.measure_now){telemetry_.readbacks[step_]=interval(command_mark,measure());telemetry_.measured_after=after.self;}
            if(!word_.price.measure_now&&command_end>=telemetry_.postreadback_fuel&&command_end-telemetry_.postreadback_fuel>word_.price.readback[step_]){++telemetry_.readback_overruns;cost_overrun=true;}
            prefix_.last_actual=after.source;out.after=after;out.after_known=true;
            if(accepted){++out.accepted_steps;out.accepted=true;}
            if(changed(before.source,after.source)||!after.protocol_armed||!after.canonical_header||!after.priority_contract_current)return stop(after.source.life!=before.source.life?q::Failure::ChangedLife:q::Failure::ChangedProtocol,Status::Aborting,out);
            if(!accepted){q::Actual refusal;refusal.word=lease_.word;refusal.refused=true;refusal.complete_word=true;out.programme=program_.reconcile(lease_,after,refusal);programme_current_=out.programme==q::Status::Refused||out.programme==q::Status::Aborting;word_pending_=false;out.status=Status::Refused;out.physical_released=true;return out;}
            if(!telemetry_.postreadback_current)return stop(q::Failure::None,Status::Stale,out);
            if(cost_overrun)return stop(q::Failure::Interrupted,Status::NeedModel,out);
            // Target death never consumes the entire owner's input. Close the
            // old single-target word and let the actual Program nominate anew.
            if(step_+1<word_.count&&(word_.steps[step_].kind==PrimitiveKind::Basic)){const auto* target=camp(after,offer.target);if(target&&!target->alive)return stop(q::Failure::None,Status::Replan,out);}
        }
        q::Actual completed;completed.word=lease_.word;completed.accepted=completed.complete_word=true;
        out.programme=program_.reconcile(lease_,actual_,completed);programme_current_=out.programme==q::Status::Ready||out.programme==q::Status::Aborting;word_pending_=false;telemetry_.stage=Stage::Done;out.status=Status::Complete;out.physical_released=true;return out;
    }
    Result reconcile_pending(){Result out;out.claimed=claimed_;bool committed_readback=false;if(!current()){out.status=Status::Stale;return out;}
        if(wire_pending_){w::Snapshot post;out.status=read(post);if(out.status!=Status::Ready)return out;
            if(wire_pending_->kind==w::WriteKind::Header)view_=endpoint_.confirm_proposal(*wire_pending_,post,source(epoch_));
            else{const auto confirmed=endpoint_.confirm_record(*wire_pending_,post,source(epoch_));
                committed_readback=word_pending_&&lease_.offer.kind==q::Kind::PublishCommit&&confirmed==w::Status::Published&&w::decode_record(wire_pending_->word).phase==w::Phase::Committed;}
            wire_pending_.reset();wire_attempted_=false;}
        q::Frame after;out.status=capture(after);if(out.status!=Status::Ready)return out;
        if(word_pending_){q::Actual unknown;unknown.word=lease_.word;unknown.unknown_suffix=command_pending_;prefix_.unknown_effect|=command_pending_;
            if(committed_readback){unknown.kind=q::ReceiptKind::CommitPublished;unknown.event=host_.event();unknown.contract=contract_.id;unknown.life=contract_.life;unknown.actor=owner_;unknown.observed_actual=unknown.accepted=unknown.complete_word=true;}
            out.programme=program_.reconcile(lease_,after,unknown);if(out.programme==q::Status::NeedObservation||out.programme==q::Status::Stale){out.status=Status::Pending;return out;}
            word_pending_=command_pending_=false;out.unknown_effect=prefix_.unknown_effect;out.physical_released=true;}
        const auto status=program_.observe(after);programme_current_=status==q::Status::Ready||status==q::Status::AwaitActual;import();out.after=after;out.after_known=true;out.status=Status::Complete;return out;
    }
    Result withdraw(){Result out;if(needs_reconciliation()){out.status=Status::Pending;return out;}w::Snapshot snapshot;out.status=read(snapshot);if(out.status!=Status::Ready)return out;
        return finish_wire(endpoint_.prepare_record(snapshot,source(epoch_),w::Phase::Abort,true));}
    Result publish(){Result out;if(needs_reconciliation()||!observed_||!programme_current_){out.status=Status::Pending;return out;}if(protocol_fault_)return withdraw();
        const auto complete=program_.completion();const auto self=q::detail::member(contract_,owner_);if(self<0)return out;const auto& own=complete.members[static_cast<unsigned>(self)];
        // Wire phase ranks imply ALL earlier milestones. A zero-gem payout
        // may make the local bank quota vacuous before source release; that
        // is NOT permission to attest Released/Banked prematurely. This first
        // Native class also requires an actual positive own work/support
        // prefix before attesting Payout; pure lifecycle progress alone is not
        // a counterfeit WorkPrefix for a non-contributing participant.
        const bool committed=own.committed,arrived=committed&&own.arrived,entered=arrived&&own.entered;
        const bool worked=entered&&(own.positive_work||own.useful_support),paid=worked&&own.payout,released=paid&&own.released;
        const bool banked=released&&own.banked,recovered=banked&&own.recovered,renewed=recovered&&own.renewed;
        w::Phase phase=w::Phase::Unpublished;
        if(program_.phase()==q::Phase::Abort||program_.phase()==q::Phase::Handoff)phase=w::Phase::Abort;
        else if(renewed)phase=w::Phase::Renewed;else if(recovered)phase=w::Phase::Recovered;else if(banked)phase=w::Phase::Banked;
        else if(released)phase=w::Phase::Released;else if(paid)phase=w::Phase::Payout;else if(worked)phase=w::Phase::WorkPrefix;
        else if(entered)phase=w::Phase::Entered;else if(arrived)phase=w::Phase::Arrived;else if(committed)phase=w::Phase::Committed;
        if(phase==w::Phase::Unpublished)return out;w::Snapshot snapshot;out.status=read(snapshot);if(out.status!=Status::Ready)return out;
        return finish_wire(endpoint_.prepare_record(snapshot,source(epoch_),phase,true));}
    bool handoff(const Independent& independent){return observed_&&programme_current_&&!needs_reconciliation()&&independent.source==actual_.source&&independent.grant&&independent.programme
        &&independent.complete&&independent.funded&&independent.accepted&&host_.independent(independent,actual_)&&current()&&program_.handoff(actual_,true);}
    bool handoff_paid_exit(const Independent& independent){return options_.allow_paid_exit_handoff&&observed_&&programme_current_&&!needs_reconciliation()
        &&program_.phase()==q::Phase::Abort&&independent.source==actual_.source&&independent.grant&&independent.programme
        &&independent.complete&&independent.funded&&independent.accepted&&host_.independent(independent,actual_)&&current()&&program_.handoff_paid_exit(actual_,true);}
    Result renew(const q::Offer& offer,const q::Quote& quote,const Independent& independent){Result out;
        if(needs_reconciliation()||!observed_||!programme_current_||offer.kind!=q::Kind::Renew||offer.source!=actual_.source||independent.source!=actual_.source
            ||!independent.grant||!independent.programme||!independent.complete||!independent.funded||!independent.accepted
            ||!host_.independent(independent,actual_)||!current()){out.status=Status::NeedModel;return out;}
        const auto sealed=program_.seal(offer,quote);if(sealed.status!=q::Status::Ready||!program_.begin_attempt(sealed.lease)){out.status=Status::NeedModel;return out;}
        lease_=sealed.lease;word_pending_=claimed_=true;q::Frame after;out.status=capture(after);if(out.status!=Status::Ready){out.status=Status::Pending;return out;}
        q::Actual accepted;accepted.kind=q::ReceiptKind::Renewed;accepted.contract=contract_.id;accepted.life=contract_.life;accepted.word=lease_.word;accepted.event=host_.event();accepted.actor=owner_;
        accepted.observed_actual=accepted.accepted=accepted.complete_word=true;out.programme=program_.reconcile(lease_,after,accepted);programme_current_=out.programme==q::Status::Ready||out.programme==q::Status::Aborting;word_pending_=false;
        out.status=Status::Complete;out.claimed=true;out.after=after;out.after_known=out.physical_released=true;return out;}
    Result cleanup(){Result out;if(needs_reconciliation()||!observed_||(program_.phase()!=q::Phase::Handoff&&program_.phase()!=q::Phase::Complete)){out.status=Status::NeedModel;return out;}
        w::Snapshot snapshot;out.status=read(snapshot);if(out.status!=Status::Ready)return out;
        out=finish_wire(endpoint_.prepare_record(snapshot,source(epoch_),w::Phase::CleanupDone,true));
        if(out.status==Status::Complete){(void)program_.cleanup(owner_,contract_.id,true,true);if(!program_.wire_owned())wire_owned_=false;}return out;}
};
#ifndef WB_PARTY_PROGRAM_NATIVE_HOST_FREE
namespace legacy=party_native;
struct SdkFacts {
    void* context{};
    // Called after the real SDK core snapshot. Fill only lifecycle, quietness,
    // source release, actual capital/route readiness and dated world revisions
    // that the Runtime can actually establish; unknown defaults stay false.
    bool(*produce)(void*,const q::Contract&,const legacy::Observation&,q::Frame&){};
    // Price/revalidate the remaining selected word against the fresh actual
    // packet. The SDK legality check is separate and cannot make these true.
    bool(*remaining)(void*,const q::Offer&,const Word&,unsigned,const q::Frame&,Guard&){};
    bool(*independent)(void*,const Independent&,const q::Frame&){};
};
inline p::Contract legacy_contract(const q::Contract& c){p::Contract out;out.id=c.id;out.group=c.group;out.start_round=c.start;out.members=c.members;
    out.camp_count=c.camp_count;for(unsigned i=0;i<c.camp_count&&i<p::max_camps;++i)out.camp_ids[i]=c.camps[i];return out;}
inline legacy::Epoch legacy_epoch(const q::Epoch& e){return {e.owner,e.round,e.input,e.effects};}
inline p::WordIntent legacy_intent(const q::Offer& o,const Word& word,const Primitive& primitive){
    p::WordIntent out;out.owner=o.source.owner;out.round=o.source.round;out.input=o.source.input;out.contract=o.contract;out.destination=o.destination;
    out.camp=o.kind==q::Kind::Heal&&primitive.kind==PrimitiveKind::Basic?word.camp_target:o.target;out.recipient=o.target;out.selected_cast=word.selected_cast;
    switch(o.kind){case q::Kind::WorkTank:out.kind=p::Kind::WorkTank;break;case q::Kind::Strike:out.kind=p::Kind::Strike;break;
    case q::Kind::Heal:out.kind=p::Kind::SupportHeal;break;case q::Kind::Bank:out.kind=primitive.kind==PrimitiveKind::Submit?p::Kind::Submit:p::Kind::ApproachAltar;break;
    case q::Kind::Recover:out.kind=p::Kind::Recover;break;case q::Kind::HoldChannel:out.kind=p::Kind::HoldChannel;break;
    case q::Kind::HoldRecovery:out.kind=p::Kind::HoldRecovery;break;case q::Kind::Assemble:out.kind=p::Kind::Assemble;break;
    case q::Kind::Enter:out.kind=p::Kind::Enter;break;case q::Kind::Capture:out.kind=p::Kind::CaptureHold;break;
    case q::Kind::Release:out.kind=p::Kind::Release;break;case q::Kind::Renew:out.kind=p::Kind::Renew;break;
    case q::Kind::AbortExit:out.kind=p::Kind::EmergencyExit;break;default:out.kind=p::Kind::None;break;}return out;}
inline legacy::Primitive legacy_primitive(const Primitive& primitive){return {static_cast<legacy::PrimitiveKind>(primitive.kind),primitive.destination,primitive.target,primitive.slot};}

// Query adapter reuses the existing genuine personal SDK capture (peer private
// gold remains unknown) and legality implementation, not its fixed-slot Session.
// It provides no route/party canonicalization or risk truth. No SDK calls occur
// in constructors; caller-supplied camp handles are immutable identities only.
class SdkHost {
    legacy::SdkHost sdk_;SdkFacts facts_;legacy::Observation raw_{};q::Revision samples_{},effects_{},events_{};q::Epoch captured_{};
    bool walked_{},cast_used_{},exclusive_used_{};int last_round_{-1};q::Revision last_input_{};
public:
    SdkHost(std::span<const cambc::camp> camps,SdkFacts facts):sdk_(camps),facts_(facts){}
    int owner(){return sdk_.owner();}int round(){return sdk_.round();}unsigned fuel(){return sdk_.fuel();}
    q::Revision event(){return ++events_;}
    bool independent(const Independent& grant,const q::Frame& frame){return facts_.independent&&facts_.independent(facts_.context,grant,frame);}
    void read_words(std::array<std::uint16_t,4>& words){sdk_.read_words(words);}
    bool write_word(unsigned offset,std::uint16_t word,Fence fence){
        if(offset>6||(offset&1u))return false;
        // Final fence -> exactly ONE checked raw range write. Never RMW.
        if(!fence())return false;
        cambc::detail::check(cambc::detail::sdk_write_shared_memory(static_cast<std::int32_t>(offset),&word,2));return true;
    }
    bool observe(q::Epoch epoch,const q::Contract& c,q::Frame& out){
        if(!facts_.produce)return false;raw_={};
        if(!sdk_.observe(legacy_epoch(epoch),legacy_contract(c),raw_))return false;
        out={};out.source=epoch;out.source.sample=++samples_;out.source.effects=epoch.effects+effects_;out.self=raw_.frame.self;out.peers=raw_.frame.peers;out.group=raw_.frame.group;
        out.fresh=raw_.frame.fresh;
        if(last_round_!=epoch.round||last_input_!=epoch.input){walked_=!out.self.move;cast_used_=!out.self.cast;exclusive_used_=false;last_round_=epoch.round;last_input_=epoch.input;}
        if(!facts_.produce(facts_.context,c,raw_,out)||!sdk_.current(legacy_epoch(epoch)))return false;
        if(out.source.owner!=epoch.owner||out.source.round!=epoch.round||out.source.input!=epoch.input||!out.source.sample||out.self.id!=epoch.owner)return false;
        captured_=out.source;return out.fresh;
    }
    bool guard(const q::Offer& offer,const Word& word,unsigned step,const q::Frame& actual,Guard& out){
        if(step>=word.count||actual.source!=captured_||!facts_.remaining)return false;
        const auto primitive=legacy_primitive(word.steps[step]);const auto intent=legacy_intent(offer,word,word.steps[step]);
        if(!sdk_.legal(legacy_epoch(actual.source),raw_,intent,primitive,walked_,cast_used_,exclusive_used_))return false;
        if(offer.kind==q::Kind::Heal&&word.count==2&&step==0){const auto basic=legacy_primitive(word.steps[1]);const auto following=legacy_intent(offer,word,word.steps[1]);
            if(!sdk_.legal(legacy_epoch(actual.source),raw_,following,basic,walked_,cast_used_,exclusive_used_))return false;}
        out={};if(!facts_.remaining(facts_.context,offer,word,step,actual,out)||!sdk_.current(legacy_epoch(actual.source)))return false;
        return out.source==actual.source&&out.word==word.token&&out.step==step;
    }
    bool execute(const Primitive& primitive,Fence fence,bool& accepted){
        const auto command=legacy_primitive(primitive);
        // All command arguments are canonical before the final fence. The
        // legacy apply() helper itself contains one raw action import only.
        if(!fence())return false;
        accepted=sdk_.apply(legacy_epoch(captured_),command);++effects_;
        if(accepted){if(primitive.kind==PrimitiveKind::Move)walked_=true;
            if(primitive.kind!=PrimitiveKind::Move&&primitive.kind!=PrimitiveKind::Basic&&primitive.kind!=PrimitiveKind::Submit&&primitive.kind!=PrimitiveKind::Recall){
                cast_used_=true;if(primitive.slot>=0&&primitive.slot<2&&raw_.abilities[static_cast<unsigned>(primitive.slot)].exclusive)exclusive_used_=true;}}
        return true;
    }
    bool effects(const q::Frame& before,const q::Frame& after,const Primitive& primitive,bool accepted,const q::Lease& lease,std::span<q::Actual> out,unsigned& count){
        count=0;if(!accepted)return true;
        auto add=[&](q::ReceiptKind kind,int target,float from,float to){if(count==out.size())return false;auto& a=out[count++];a.kind=kind;a.event=event();a.contract=lease.offer.contract;a.life=before.source.life;a.word=lease.word;
            a.actor=before.self.id;a.group=before.group.id;a.target=target;a.before=from;a.after=to;a.observed_actual=a.accepted=true;return true;};
        if(primitive.kind==PrimitiveKind::Basic){for(unsigned i=0;i<before.group.count;++i){const auto& a=before.group.camps[i];const auto* b=camp(after,a.id);
                if(!b||!a.observed_now||!b->observed_now)return false;if(a.hp>b->hp&&!add(q::ReceiptKind::CampDamage,a.id,a.hp,b->hp))return false;}}
        else if(primitive.kind==PrimitiveKind::Shield&&after.self.shield>before.self.shield){if(!add(q::ReceiptKind::Shield,before.self.id,before.self.shield,after.self.shield))return false;}
        else if(primitive.kind==PrimitiveKind::Heal){const auto* a=peer(before,primitive.target);const auto* b=peer(after,primitive.target);if(!a||!b)return false;
            if(b->hp>a->hp&&!add(q::ReceiptKind::Heal,a->id,a->hp,b->hp))return false;}
        return true;
    }
};
#endif
}
