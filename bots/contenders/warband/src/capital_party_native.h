#pragma once
#include "capital_party.h"
#include "capital_party_wire.h"
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#ifndef WB_PARTY_NATIVE_HOST_FREE
#include <cambc.h>
#endif

// A caller-owned exclusive branch, not a Runtime hook or a safety certificate.
// No discovery, contract selection, guard truth, priority write or automatic
// wait is supplied here. Every admission flag defaults false. Exceptions must
// reach the existing turn-boundary error-log/yield handler.
namespace wb::capital::party_native {
namespace p=party;namespace wire=party_wire;
inline constexpr unsigned max_steps=6;
enum class Status {Ready,NoOffer,NeedModel,NeedObservation,NeedFuel,Stale,Pending,Refused,Complete,Error,Prestart};
struct Epoch {int owner{-1},round{-1};std::uint64_t input{},capital_revision{};};
struct Admission {
    bool canonical_contract{},accepted_roles{},exclusive_protocol_branch{},legacy_publish_disabled{};
    bool fixed_priorities{},range_io_admitted{},trusted_predicate_producer{};
    bool valid() const{return canonical_contract&&accepted_roles&&exclusive_protocol_branch&&legacy_publish_disabled
        &&fixed_priorities&&range_io_admitted&&trusted_predicate_producer;}
};
struct Ability {
    p::Cast kind{p::Cast::None};int cooldown{-1};float range{};unsigned targets{};bool exclusive{},charged{},damaging{};
};
struct Observation {
    p::Frame frame{};std::array<Ability,2> abilities{};
    int move_speed{},attack_range{};bool own_core_known{},completed_turn{},recall_started{},submit_started{};
    // Peers' gold is self-only in the SDK. Their gold/score/channel-duration
    // quotes are NOT observed quantities, even when core pose/HP is visible.
    std::array<bool,p::actors> private_economy_known{};
};
struct Auxiliary {
    int owner{-1},round{-1};std::uint64_t input{},contract{};bool fresh{};
    std::array<bool,p::actors> npc_quiet_known{},npc_quiet{};
    bool capture_state_known{},capture_own{},contested{},claimed_known{},claimed{};
    p::Payout payout{};
    // Caller derives the actual duration from queried current settings/items.
    // The SDK exposes base recall time, not a resolved-duration query.
    bool recall_quote_known{};unsigned recall_ticks{};
};
using FactsProducer=bool(*)(void*,const Observation&,Auxiliary&);
struct Facts {void* context{};FactsProducer produce{};};
enum class PrimitiveKind {Move,Basic,Shield,Reset,Heal,Dash,Submit,Recall};
struct Primitive {PrimitiveKind kind{PrimitiveKind::Move};p::Point destination{};int target{-1},slot{-1};};
struct Word {
    std::uint64_t contract{},input{},token{};int owner{-1},round{-1};
    std::array<Primitive,max_steps> steps{};unsigned count{};
};
using GuardProducer=bool(*)(void*,const Observation&,const p::WordIntent&,const Word&,unsigned);
struct Guard {void* context{};GuardProducer permit{};};
using MetadataProducer=bool(*)(void*,Epoch,const p::Contract&);
struct MetadataGuard {void* context{};MetadataProducer permit{};};
struct Options {unsigned observe_reserve{35000},primitive_reserve{25000},wire_reserve{10000};};
struct Result {
    Status status{Status::NoOffer};bool claimed{},attempted{},accepted{},hold_admitted{},reconciled{},after_known{},unknown_effect{};
    unsigned accepted_steps{};p::Tape tape{};Observation after{};
    wire::Status wire_status{wire::Status::Invalid};
};
struct Attempt {
    Epoch source{};Primitive primitive{};
    bool attempted{},returned{},accepted{};
};
struct Reconciliation {
    Status status{Status::NoOffer},observation_status{Status::NoOffer};
    bool prefix_retained{},controller_reconciled{},prefix_confirmed{},actual_known{},physical_released{},unknown_effect{},metadata_pending{},metadata_closed{};
    p::WordIntent intent{};p::Tape prefix{};Attempt uncertain{};Observation actual{};
};
inline wire::Frame source(Epoch e){return {e.owner,e.round,e.input,true};}
inline p::Milestone milestone(wire::Phase value) {
    switch(value) {
    case wire::Phase::Unpublished:return p::Milestone::Unpublished;case wire::Phase::Committed:return p::Milestone::Committed;
    case wire::Phase::Arrived:return p::Milestone::Arrived;case wire::Phase::Entered:return p::Milestone::Entered;
    case wire::Phase::WorkPrefix:return p::Milestone::WorkPrefix;case wire::Phase::Payout:return p::Milestone::Payout;
    case wire::Phase::Released:return p::Milestone::Released;case wire::Phase::Banked:return p::Milestone::Banked;
    case wire::Phase::Recovered:return p::Milestone::Recovered;case wire::Phase::Renewed:return p::Milestone::Renewed;
    case wire::Phase::Abort:return p::Milestone::Abort;case wire::Phase::CleanupDone:return p::Milestone::CleanupDone;
    case wire::Phase::ClosedClean:return p::Milestone::ClosedClean;case wire::Phase::ClosedFailed:return p::Milestone::ClosedFailed;
    }return p::Milestone::Unpublished;
}
inline wire::Phase phase(p::Milestone value) {
    for(unsigned i=0;i<=static_cast<unsigned>(wire::Phase::ClosedFailed);++i) {
        const auto candidate=static_cast<wire::Phase>(i);if(milestone(candidate)==value)return candidate;
    }return wire::Phase::Unpublished;
}
inline bool bound(const p::WordIntent& i,const Word& w,Epoch e) {
    return i.guarded&&i.word_token&&i.word_token==w.token&&i.contract==w.contract&&i.owner==w.owner&&i.round==w.round
        &&i.input==w.input&&i.owner==e.owner&&i.round==e.round&&i.input==e.input&&w.count<=max_steps;
}
inline bool same_core(const p::Frame& a,const p::Frame& b) {
    if(a.owner!=b.owner||a.round!=b.round||a.input!=b.input||!(a.self==b.self)||a.group.id!=b.group.id||a.group.count!=b.group.count)return false;
    for(unsigned i=0;i<p::actors;++i)if(a.peers[i].observed_now!=b.peers[i].observed_now
        ||(a.peers[i].observed_now&&!(a.peers[i].body==b.peers[i].body)))return false;
    for(unsigned i=0;i<a.group.count&&i<p::max_camps;++i) {
        const auto& x=a.group.camps[i];const auto& y=b.group.camps[i];
        if(x.id!=y.id||x.hp!=y.hp||x.observed_now!=y.observed_now||x.alive!=y.alive||!(x.position==y.position)
            ||x.attack_range!=y.attack_range||x.geometry_known!=y.geometry_known)return false;
    }return true;
}
inline bool same_intent(const p::WordIntent& a,const p::WordIntent& b) {
    return a.contract==b.contract&&a.serial==b.serial&&a.input==b.input&&a.word_token==b.word_token&&a.owner==b.owner
        &&a.round==b.round&&a.kind==b.kind&&a.destination==b.destination&&a.camp==b.camp&&a.recipient==b.recipient
        &&a.slot==b.slot&&a.selected_cast==b.selected_cast&&a.guarded==b.guarded;
}
inline bool same_word(const Word& a,const Word& b) {
    if(a.contract!=b.contract||a.input!=b.input||a.token!=b.token||a.owner!=b.owner||a.round!=b.round||a.count!=b.count||a.count>max_steps)return false;
    for(unsigned i=0;i<a.count;++i){const auto& x=a.steps[i];const auto& y=b.steps[i];
        if(x.kind!=y.kind||x.destination!=y.destination||x.target!=y.target||x.slot!=y.slot)return false;}
    return true;
}
inline bool allowed(const p::WordIntent& i,const Primitive& s) {
    switch(s.kind) {
    case PrimitiveKind::Basic:return (i.kind==p::Kind::WorkTank||i.kind==p::Kind::Strike)&&s.target==i.camp;
    case PrimitiveKind::Shield:return i.kind==p::Kind::WorkTank&&i.selected_cast==p::Cast::Shield;
    case PrimitiveKind::Reset:return (i.kind==p::Kind::WorkTank||i.kind==p::Kind::Strike)&&i.selected_cast==p::Cast::Reset;
    case PrimitiveKind::Heal:return i.kind==p::Kind::SupportHeal&&i.selected_cast==p::Cast::Heal&&s.target==i.recipient;
    case PrimitiveKind::Dash:return i.selected_cast==p::Cast::Dash;
    case PrimitiveKind::Submit:return i.kind==p::Kind::Submit&&s.destination==i.destination;
    case PrimitiveKind::Recall:return i.kind==p::Kind::Recover;
    case PrimitiveKind::Move:return i.kind!=p::Kind::Submit&&i.kind!=p::Kind::HoldChannel&&i.kind!=p::Kind::HoldRecovery;
    }return false;
}

// Host methods perform bounded SDK queries/actions only. Test hosts implement
// the identical interface without engine imports. Epoch checks surround EVERY
// host method/callback; SdkHost additionally fences its individual imports.
template<class Host> class Session {
    Host& host_;p::Contract c_;int owner_;Admission admission_;Options options_;wire::Endpoint endpoint_;Epoch epoch_{};
    bool started_{},input_claimed_{};std::optional<wire::Write> pending_{};wire::View view_{};
    std::optional<Observation> latest_{},issued_observation_{},prefix_observation_{};p::Controller* issued_controller_{};
    p::WordIntent issued_{};Word sealed_{};bool ticket_{},word_started_{},pending_action_{},metadata_closed_{};p::Tape retained_{};
    Attempt uncertain_{};
    bool current(){return started_&&host_.owner()==epoch_.owner&&host_.round()==epoch_.round;}
    Status no_offer() const{return input_claimed_?Status::NeedModel:Status::NoOffer;}
    bool budget(unsigned reserve){return current()&&host_.fuel()>=reserve&&current();}
    Status read(wire::Snapshot& snapshot) {
        if(!budget(options_.wire_reserve))return current()?Status::NeedFuel:Status::Stale;
        snapshot={};snapshot.source=source(epoch_);
        host_.read_words(snapshot.words);if(!current())return Status::Stale;
        const auto header=wire::decode_header(snapshot.words[0]);
        snapshot.canonical_contract=admission_.canonical_contract&&header.start==c_.start_round&&header.group==c_.group;
        snapshot.whole_read=true;return Status::Ready;
    }
    void protocol(p::Frame& f,const wire::View& view) {
        for(unsigned i=0;i<p::actors;++i) {
            const auto& claim=view.claims[i];const bool dated=claim.status==wire::ClaimStatus::DatedPayload;
            f.commitment_known[i]=dated;
            f.commitment_matches[i]=dated&&claim.payload.predicate&&!wire::abort_branch(claim.payload.phase)
                &&!view.abort_seen;
        }
    }
    Result write(const wire::Write& prepared) {
        Result out;out.claimed=input_claimed_;
        if(!prepared.prepared){out.status=no_offer();out.wire_status=prepared.status;return out;}
        if(!current()){out.status=Status::Stale;return out;}
        // Retain immutable source BEFORE the SDK call. A suspended write cannot
        // be discarded/replaced merely because this input has ended.
        pending_=prepared;input_claimed_=out.claimed=true;out.attempted=true;
        host_.write_word(prepared.byte_offset,prepared.word);
        if(!current()){out.status=Status::Pending;return out;}
        wire::Snapshot post;const auto status=read(post);
        if(status!=Status::Ready){out.status=Status::Pending;return out;}
        if(prepared.kind==wire::WriteKind::Header) {
            view_=endpoint_.confirm_proposal(prepared,post,source(epoch_));out.wire_status=view_.status;
            out.accepted=view_.armed;
        } else {
            out.wire_status=endpoint_.confirm_record(prepared,post,source(epoch_));out.accepted=out.wire_status==wire::Status::Published;
            view_=endpoint_.observe(post,source(epoch_));
        }
        pending_.reset();out.status=out.accepted?Status::Complete:Status::NeedModel;return out;
    }
public:
    Session(Host& host,const p::Contract& canonical,int owner,Admission admission,Options options={})
        :host_(host),c_(canonical),owner_(owner),admission_(admission),options_(options),
        endpoint_(wire::Roles{{canonical.members[0].id,canonical.members[1].id,canonical.members[2].id},
            admission.accepted_roles,admission.exclusive_protocol_branch,admission.legacy_publish_disabled},owner) {}
    const Epoch& epoch() const{return epoch_;}
    bool claimed() const{return input_claimed_;}
    const p::Tape& retained_prefix() const{return retained_;}
    bool unknown_action() const{return pending_action_;}
    bool needs_reconciliation() const{return ticket_||pending_action_||pending_.has_value()||metadata_closed_;}
    // Never retry a suspended/throwing command. begin() first binds the new
    // actual owner input; this method consumes the old private ticket using
    // only its dated fully observed prefix, then releases PHYSICAL ownership.
    // Exclusive wire ownership remains with the caller through abort cleanup.
    Reconciliation reconcile(p::Controller& controller,Facts facts) {
        Reconciliation out;out.intent=issued_;out.prefix=retained_;out.prefix_retained=ticket_||pending_action_;
        out.uncertain=uncertain_;out.unknown_effect=pending_action_;out.metadata_pending=pending_.has_value();out.metadata_closed=metadata_closed_;
        if(!current()){out.status=Status::Stale;return out;}
        if(pending_){out.status=Status::Pending;return out;}
        if(!ticket_&&!pending_action_&&!metadata_closed_)return out;
        if(!p::detail::same(controller.contract(),c_)||((ticket_||pending_action_)
            &&(issued_controller_!=&controller||!prefix_observation_||epoch_.round<=issued_.round))){out.status=Status::Pending;return out;}
        out.observation_status=observe(controller,out.actual,facts);
        const auto& f=out.actual.frame;
        out.actual_known=current()&&out.actual.own_core_known&&f.fresh&&f.owner==epoch_.owner
            &&f.round==epoch_.round&&f.input==epoch_.input&&f.self.id==epoch_.owner&&p::detail::body(f.self)
            &&f.self.role==c_.members[static_cast<unsigned>(p::detail::member(c_,epoch_.owner))].role;
        if(!out.actual_known){out.status=current()?out.observation_status:Status::Stale;return out;}
        if(ticket_||pending_action_) {
            const auto interrupted=controller.interrupt(issued_,prefix_observation_->frame,retained_,epoch_.round);
            if(!interrupted.matched){out.status=Status::NeedModel;return out;}
            // A partial mandatory word can fail the old complete-word cast/slot
            // test while confirm() still accounts its dated actual effects.
            out.controller_reconciled=interrupted.matched;out.prefix_confirmed=interrupted.prefix_confirmed;
        }
        ticket_=word_started_=pending_action_=false;issued_controller_=nullptr;issued_observation_.reset();prefix_observation_.reset();
        input_claimed_=metadata_closed_=false;uncertain_={};out.physical_released=true;out.status=Status::Complete;
        return out;
    }
    Status begin(Epoch next) {
        if(!admission_.valid()||!p::detail::valid(c_)||!next.input)return Status::NoOffer;
        if(next.owner!=owner_||host_.owner()!=next.owner||host_.round()!=next.round||p::detail::member(c_,next.owner)<0)return Status::Stale;
        if(started_&&next.round==epoch_.round&&next.input!=epoch_.input)return Status::NeedModel;
        if(started_&&next.round<epoch_.round)return Status::Stale;
        const bool unresolved=pending_.has_value()||pending_action_||ticket_||metadata_closed_;
        if(!started_||next.round!=epoch_.round){latest_.reset();if(!unresolved)input_claimed_=false;}
        epoch_=next;started_=true;
        if(pending_) {
            // The previous synchronous SDK call has now RETURNED. Reconcile
            // its original source against this actual input; never reissue it.
            wire::Snapshot post;const auto status=read(post);if(status!=Status::Ready)return status;
            if(pending_->kind==wire::WriteKind::Header)view_=endpoint_.confirm_proposal(*pending_,post,source(epoch_));
            else (void)endpoint_.confirm_record(*pending_,post,source(epoch_));
            pending_.reset();metadata_closed_=true;return Status::Stale;
        }
        if(pending_action_||ticket_||metadata_closed_)return Status::Stale;
        return Status::Ready;
    }
    Status observe(p::Controller& controller,Observation& out,Facts facts) {
        if(!admission_.valid()||!started_||!p::detail::same(controller.contract(),c_))return no_offer();
        if(!facts.produce)return Status::NeedModel;
        if(!budget(options_.observe_reserve))return current()?Status::NeedFuel:Status::Stale;
        out={};if(!host_.observe(epoch_,c_,out))return current()?Status::NeedObservation:Status::Stale;
        if(!current())return Status::Stale;
        if(!out.own_core_known||!out.frame.fresh||out.frame.owner!=epoch_.owner||out.frame.round!=epoch_.round
            ||out.frame.input!=epoch_.input||out.frame.self.id!=epoch_.owner||!p::detail::body(out.frame.self)
            ||out.frame.self.role!=c_.members[static_cast<unsigned>(p::detail::member(c_,epoch_.owner))].role
            ||out.frame.group.id!=c_.group||out.frame.group.count!=c_.camp_count)return Status::NeedObservation;
        Auxiliary aux;if(!facts.produce(facts.context,out,aux))return current()?Status::NeedModel:Status::Stale;
        if(!current())return Status::Stale;
        if(!aux.fresh||aux.contract!=c_.id||aux.owner!=epoch_.owner||aux.round!=epoch_.round||aux.input!=epoch_.input)return Status::NeedModel;
        auto& f=out.frame;
        for(unsigned i=0;i<p::actors;++i)if(f.peers[i].observed_now)f.peers[i].body.npc_quiet=aux.npc_quiet_known[i]&&aux.npc_quiet[i];
        const auto at=static_cast<unsigned>(p::detail::member(c_,epoch_.owner));
        f.self.npc_quiet=aux.npc_quiet_known[at]&&aux.npc_quiet[at];f.peers[at].body=f.self;
        f.self.recall_ticks=aux.recall_quote_known?aux.recall_ticks:0;f.peers[at].body=f.self;
        f.group.capture_own=aux.capture_state_known&&aux.capture_own;f.group.contested=!aux.capture_state_known||aux.contested;
        f.group.claimed=aux.claimed_known&&aux.claimed;f.payout=aux.payout;
        wire::Snapshot snapshot;const auto status=read(snapshot);if(status!=Status::Ready)return status;
        view_=endpoint_.observe(snapshot,source(epoch_));protocol(f,view_);
        if(view_.status==wire::Status::Active&&view_.armed)for(unsigned i=0;i<p::actors;++i) {
            const auto& claim=view_.claims[i];if(claim.actor==epoch_.owner||claim.status!=wire::ClaimStatus::DatedPayload)continue;
            p::TrustedPredicateReceipt r;r.contract=c_.id;r.actor=claim.actor;r.role=static_cast<p::Role>(i);
            r.source_round=claim.payload.round;r.milestone=milestone(claim.payload.phase);r.predicate=claim.payload.predicate;
            r.read_owner=epoch_.owner;r.read_round=epoch_.round;r.read_input=epoch_.input;
            r.header_start=view_.header.start;r.header_group=view_.header.group;
            r.active_armed=view_.armed;r.whole_read=true;r.trusted_partition_and_producer=admission_.trusted_predicate_producer;
            const auto imported=controller.ingest(c_,r,f);
            if(imported==p::IngestStatus::Conflict||imported==p::IngestStatus::Invalid)return Status::NeedModel;
        }
        if(!current())return Status::Stale;
        if(view_.status==wire::Status::ArmedFuture)return Status::Prestart;
        if(view_.status!=wire::Status::Active||!view_.armed)return no_offer();
        latest_=out;return Status::Ready;
    }
    Result propose(unsigned group,bool reset_admitted,MetadataGuard funding) {
        Result out;out.claimed=input_claimed_;
        if(!funding.permit||!funding.permit(funding.context,epoch_,c_)){out.status=current()?no_offer():Status::Stale;return out;}
        if(!current()){out.status=Status::Stale;return out;}
        wire::Snapshot snapshot;out.status=read(snapshot);
        if(out.status!=Status::Ready)return out;
        if(group!=static_cast<unsigned>(c_.group)||epoch_.round+2!=c_.start_round){out.status=no_offer();return out;}
        return write(endpoint_.prepare_proposal(snapshot,source(epoch_),group,{admission_.canonical_contract,reset_admitted}));
    }
private:
    Result publish_funded(p::Controller& controller,const Observation& observed) {
        Result out;out.claimed=input_claimed_;
        if(!current()){out.status=Status::Stale;return out;}
        if(!p::detail::same(controller.contract(),c_)){out.status=no_offer();return out;}
        const auto receipt=controller.publication(observed.frame);if(!receipt.valid){out.status=no_offer();return out;}
        if(receipt.actor!=epoch_.owner||receipt.source_round!=epoch_.round||receipt.source_input!=epoch_.input){out.status=Status::Stale;return out;}
        wire::Snapshot snapshot;out.status=read(snapshot);if(out.status!=Status::Ready)return out;
        return write(endpoint_.prepare_record(snapshot,source(epoch_),phase(receipt.milestone),receipt.predicate));
    }
public:
    Result publish(p::Controller& controller,const Observation& observed,MetadataGuard funding) {
        Result out;out.claimed=input_claimed_;
        if(!funding.permit||!funding.permit(funding.context,epoch_,c_)){out.status=current()?no_offer():Status::Stale;return out;}
        if(!current()){out.status=Status::Stale;return out;}
        return publish_funded(controller,observed);
    }
    // The session itself obtains the Ready decision and seals its exact word.
    // A forged/replayed private serial can never reach an SDK action first.
    p::Decision admit(p::Controller& controller,const p::Guard& guard,const Word& word) {
        if(!current()||ticket_||metadata_closed_||!latest_||!p::detail::same(controller.contract(),c_)||word.contract!=c_.id
            ||latest_->frame.round!=epoch_.round||latest_->frame.input!=epoch_.input
            ||word.owner!=epoch_.owner||word.round!=epoch_.round||word.input!=epoch_.input||word.count>max_steps
            ||!word.token||word.token!=guard.word_token||view_.status!=wire::Status::Active||!view_.armed)
            return {p::Status::NeedModel,p::Failure::NeedModel,{}};
        auto actual=*latest_;actual.frame.guard=guard;
        const auto decision=controller.tick(actual.frame);
        if(decision.status==p::Status::Ready) {
            unsigned moves=0,casts=0,basics=0,heals=0,submits=0;p::Cast cast=p::Cast::None;bool shape=true;
            for(unsigned i=0;i<word.count;++i) {
                const auto kind=word.steps[i].kind;shape&=allowed(decision.intent,word.steps[i]);
                if(kind==PrimitiveKind::Move)++moves;
                if(kind==PrimitiveKind::Basic)++basics;
                if(kind==PrimitiveKind::Submit)++submits;
                if(kind==PrimitiveKind::Shield){++casts;cast=p::Cast::Shield;}
                if(kind==PrimitiveKind::Reset){++casts;cast=p::Cast::Reset;}
                if(kind==PrimitiveKind::Heal){++casts;++heals;cast=p::Cast::Heal;}
                if(kind==PrimitiveKind::Dash){++casts;cast=p::Cast::Dash;}
                if(kind==PrimitiveKind::Dash&&(word.steps[i].slot<0||word.steps[i].slot>=2
                    ||actual.abilities[static_cast<unsigned>(word.steps[i].slot)].damaging))shape=false;
                if((kind==PrimitiveKind::Submit||kind==PrimitiveKind::Recall)&&i+1!=word.count)shape=false;
            }
            if((decision.intent.kind==p::Kind::WorkTank||decision.intent.kind==p::Kind::Strike)&&!basics)shape=false;
            if(decision.intent.kind==p::Kind::SupportHeal&&!heals)shape=false;
            if(decision.intent.kind==p::Kind::Submit&&submits!=1)shape=false;
            if(!shape||moves>1||casts>1||cast!=decision.intent.selected_cast) {
                p::Tape empty;empty.word_token=decision.intent.word_token;(void)controller.confirm(decision.intent,actual.frame,empty);
                return {p::Status::NeedModel,p::Failure::NeedModel,{}};
            }
            ticket_=true;issued_controller_=&controller;issued_=decision.intent;sealed_=word;issued_observation_=actual;
            prefix_observation_=actual;word_started_=false;uncertain_={};retained_={};retained_.word_token=issued_.word_token;
        }
        return decision;
    }
    Result execute(p::Controller& controller,const p::WordIntent& intent,const Word& supplied,Facts facts,Guard guard) {
        Result out;out.claimed=input_claimed_;out.tape.word_token=intent.word_token;
        if(pending_action_||pending_||metadata_closed_||(ticket_&&word_started_)){out.status=Status::Pending;out.unknown_effect=pending_action_;return out;}
        if(!current()){out.status=Status::Stale;return out;}
        if(!admission_.valid()||!ticket_||issued_controller_!=&controller||!same_intent(intent,issued_)||!same_word(supplied,sealed_)
            ||intent.contract!=c_.id||!p::detail::same(controller.contract(),c_)||!bound(intent,supplied,epoch_)||!guard.permit)
            {out.status=Status::NeedModel;return out;}
        // Immutable local copy: callbacks cannot replace a primitive midway.
        const Word word=sealed_;bool walked=false,cast_used=false,exclusive_used=false;retained_=out.tape;
        Observation before;out.status=observe(controller,before,facts);if(out.status!=Status::Ready)return out;
        prefix_observation_=before;word_started_=true;
        auto finish=[&](Status status,bool complete) {
            out.status=out.claimed&&status==Status::NoOffer?Status::NeedModel:status;
            out.tape.complete=complete;out.after=before;
            // One final epoch sample supplies BOTH the returned observation
            // authority and private-ticket consumption. No host import may
            // intervene between this sample and pure Controller::confirm.
            const bool is_current=current();out.after_known=is_current;
            if(!is_current){out.after.frame.fresh=false;out.status=Status::Stale;}
            retained_=out.tape;
            // Guard/legality/fuel queries can suspend AFTER an accepted prefix
            // but BEFORE a new command. Keep that private ticket until dated
            // reconciliation consumes it; pending_action_ alone is not enough.
            if(is_current){ticket_=false;out.reconciled=controller.confirm(intent,before.frame,out.tape);}
            return out;
        };
        if(!issued_observation_||!same_core(issued_observation_->frame,before.frame))return finish(Status::NeedModel,false);
        if(controller.progress().cancelled||view_.abort_seen)return finish(Status::NeedModel,false);
        if(intent.kind==p::Kind::Commit) {
            if(!guard.permit(guard.context,before,intent,word,0)||!current())return finish(current()?Status::NeedModel:Status::Stale,false);
            const auto publication=publish_funded(controller,before);out.claimed=publication.claimed;out.attempted=publication.attempted;
            if(publication.status!=Status::Complete){out.status=publication.status;out.unknown_effect=out.attempted;return out;}
            out.tape.effects[out.tape.count++]={p::EffectKind::CommitPublished,-1,{}, {},0,0,0,true,true};
            protocol(before.frame,view_);prefix_observation_=before;
            out.accepted=true;retained_=out.tape;
            Observation post;out.status=observe(controller,post,facts);
            if(out.status!=Status::Ready) {
                if(current()&&post.frame.fresh&&post.own_core_known){before=post;protocol(before.frame,view_);return finish(out.status,false);}
                out.unknown_effect=true;return out;
            }
            before=post;
        }
        for(unsigned i=0;i<word.count;++i) {
            const auto primitive=word.steps[i];
            if(!budget(options_.primitive_reserve))return finish(current()?Status::NeedFuel:Status::Stale,false);
            if(!guard.permit(guard.context,before,intent,word,i)||!current())return finish(current()?Status::NeedModel:Status::Stale,false);
            if(controller.progress().cancelled||view_.abort_seen)return finish(Status::NeedModel,false);
            if(!allowed(intent,primitive)||!host_.legal(epoch_,before,intent,primitive,walked,cast_used,exclusive_used)||!current())return finish(current()?Status::NeedModel:Status::Stale,false);
            input_claimed_=out.claimed=out.attempted=true;
            pending_action_=true;uncertain_={epoch_,primitive,true,false,false};
            const bool accepted=host_.apply(epoch_,primitive);
            uncertain_.returned=true;uncertain_.accepted=accepted;
            if(!current()){out.status=Status::Stale;out.unknown_effect=true;return out;}
            Observation after;const auto observed=observe(controller,after,facts);
            const bool core=after.own_core_known&&after.frame.fresh&&after.frame.owner==epoch_.owner&&after.frame.round==epoch_.round&&after.frame.input==epoch_.input;
            if(!current()||!core){out.status=observed;out.unknown_effect=true;return out;}
            out.after=after;out.after_known=true;
            pending_action_=false;
            if(!accepted){before=after;prefix_observation_=before;uncertain_={};out.tape.refused=true;return finish(Status::Refused,false);}
            p::Effect effect;effect.fresh=effect.accepted=true;effect.target=primitive.target;
            const auto& a=before.frame;const auto& b=after.frame;
            switch(primitive.kind) {
            case PrimitiveKind::Move:effect.kind=p::EffectKind::Move;effect.from=a.self.position;effect.to=b.self.position;walked=true;break;
            case PrimitiveKind::Dash:effect.kind=p::EffectKind::Dash;effect.from=a.self.position;effect.to=b.self.position;cast_used=true;break;
            case PrimitiveKind::Basic: {
                effect.kind=p::EffectKind::BasicDamage;const int at=p::detail::camp(c_,primitive.target);bool found=false;
                if(at>=0)for(unsigned j=0;j<a.group.count;++j)if(a.group.camps[j].id==primitive.target)
                    for(unsigned k=0;k<b.group.count;++k)if(b.group.camps[k].id==primitive.target&&a.group.camps[j].observed_now&&b.group.camps[k].observed_now)
                        {effect.before=a.group.camps[j].hp;effect.after=b.group.camps[k].hp;found=true;}
                if(!found){out.status=Status::NeedObservation;out.unknown_effect=pending_action_=true;return out;}break;
            }
            case PrimitiveKind::Shield:effect.kind=p::EffectKind::ShieldCast;effect.before=a.self.shield;effect.after=b.self.shield;cast_used=true;break;
            case PrimitiveKind::Reset:effect.kind=p::EffectKind::ResetCast;cast_used=true;break;
            case PrimitiveKind::Heal: {
                effect.kind=p::EffectKind::HealCast;const int at=p::detail::member(c_,primitive.target);
                if(at<0||!a.peers[static_cast<unsigned>(at)].observed_now||!b.peers[static_cast<unsigned>(at)].observed_now)
                    {out.status=Status::NeedObservation;out.unknown_effect=pending_action_=true;return out;}
                effect.before=a.peers[static_cast<unsigned>(at)].body.hp;effect.after=b.peers[static_cast<unsigned>(at)].body.hp;cast_used=true;break;
            }
            case PrimitiveKind::Submit:effect.kind=p::EffectKind::SubmitStarted;effect.timer=b.self.channel_remaining;
                if(b.self.channel!=p::Channel::Submit){before=after;out.tape.refused=out.unknown_effect=true;return finish(Status::Refused,false);}break;
            case PrimitiveKind::Recall:effect.kind=p::EffectKind::RecallStarted;effect.timer=b.self.channel_remaining;
                if(b.self.channel!=p::Channel::Recall){before=after;out.tape.refused=out.unknown_effect=true;return finish(Status::Refused,false);}break;
            }
            out.tape.effects[out.tape.count++]=effect;++out.accepted_steps;out.accepted=true;before=after;
            retained_=out.tape;prefix_observation_=before;uncertain_={};
            if(primitive.slot>=0&&primitive.slot<2&&primitive.kind!=PrimitiveKind::Move&&primitive.kind!=PrimitiveKind::Basic)
                exclusive_used|=after.abilities[static_cast<unsigned>(primitive.slot)].exclusive;
            if(observed!=Status::Ready||controller.progress().cancelled||view_.abort_seen)return finish(observed==Status::Ready?Status::NeedModel:observed,false);
            if((primitive.kind==PrimitiveKind::Move||primitive.kind==PrimitiveKind::Dash)&&!(after.frame.self.position==primitive.destination))
                return finish(Status::NeedModel,false);
        }
        // Empty guarded hold is possible, but an empty NoOffer NEVER authorizes
        // waiting. The external producer must price this real physical input.
        if(!word.count) {
            if(!guard.permit(guard.context,before,intent,word,0)||!current()||controller.progress().cancelled)
                return finish(current()?Status::NeedModel:Status::Stale,false);
            input_claimed_=out.claimed=true;out.hold_admitted=true;
        }
        return finish(Status::Complete,true);
    }
};

#ifndef WB_PARTY_NATIVE_HOST_FREE
// Concrete native imports. Only immutable camp identities are caller supplied;
// live HP/life/permissions are re-read. No SDK-querying global is introduced.
class SdkHost {
    std::array<std::optional<cambc::camp>,p::max_camps> camps_{};
    int own_team_{-1};
    static bool unavailable(int code){return code==static_cast<int>(cambc::error_code::api_not_in_vision)||code==static_cast<int>(cambc::error_code::api_not_found);}
    bool checked(int code,Epoch e){cambc::detail::check(code);return current(e);}
    bool path(Epoch e,p::Point from,p::Point to,bool dash,float range) {
        const cambc::vec2 destination{to.x,to.y};
        if(!cambc::map::is_in_bounds(destination)||!std::isfinite(range)||range<=0||range>100||from==to)return false;
        const int length=p::distance(from,to);if(length>range)return false;
        auto blocked=cambc::map::walls();if(!current(e))return false;
        auto holes=cambc::map::holes();if(!current(e))return false;
        if(!dash){blocked|=holes;if(!current(e))return false;}
        auto altars=cambc::map::altars();if(!current(e))return false;blocked|=altars;if(!current(e))return false;
        auto spawns=cambc::map::spawn_region(cambc::team::enemy);if(!current(e))return false;blocked|=spawns;if(!current(e))return false;
        auto champions=cambc::vision::champion_tiles();if(!current(e))return false;blocked|=champions;if(!current(e))return false;
        auto camps=cambc::vision::camp_tiles();if(!current(e))return false;blocked|=camps;if(!current(e))return false;
        auto teleports=cambc::vision::teleport_tiles();if(!current(e))return false;blocked|=teleports;if(!current(e))return false;
        blocked.set({from.x,from.y},false);if(!current(e))return false;
        if(!dash) {
            auto reachable=blocked.reachable_tiles({from.x,from.y},static_cast<std::uint16_t>(range));if(!current(e))return false;
            const bool legal=reachable.is_set(destination);return current(e)&&legal;
        }
        // Initial native dash class follows at most twenty straight eight-way
        // steps. Other legal engine dash paths remain explicit unsupported.
        const int dx=to.x-from.x,dy=to.y-from.y;if(length>20||(dx&&dy&&std::abs(dx)!=std::abs(dy)))return false;
        const int sx=(dx>0)-(dx<0),sy=(dy>0)-(dy<0);
        for(int i=1;i<=length;++i) {
            const bool occupied=blocked.is_set({from.x+sx*i,from.y+sy*i});if(!current(e)||occupied)return false;
        }
        const bool hole=holes.is_set(destination);return current(e)&&!hole;
    }
    static p::Role role(int type) {
        if(type==static_cast<int>(cambc::champion_type::varg))return p::Role::Varg;
        if(type==static_cast<int>(cambc::champion_type::willow))return p::Role::Willow;
        return p::Role::Vendetta;
    }
    static bool supported(int type){return type==static_cast<int>(cambc::champion_type::varg)||type==static_cast<int>(cambc::champion_type::willow)||type==static_cast<int>(cambc::champion_type::vendetta);}
    bool body(Epoch e,int id,bool self,p::Body& b,std::array<Ability,2>* abilities=nullptr,int* move=nullptr,int* attack=nullptr) {
        cambc::detail::champion_info raw{};const int code=cambc::detail::sdk_champion(id,&raw);
        if(!current(e)||unavailable(code))return false;cambc::detail::check(code);
        if(!supported(raw.type))return false;if(self)own_team_=raw.team;else if(raw.team!=own_team_)return false;
        b.id=id;b.role=role(raw.type);b.position={raw.x,raw.y};b.hp=raw.health;b.max_hp=raw.max_health;b.capital_revision=e.capital_revision;
        const auto state=cambc::champion_state::from_bits_retain(raw.state);
        b.alive=state.contains(cambc::champion_state::alive);b.move=state.contains(cambc::champion_state::can_move);
        b.attack=state.contains(cambc::champion_state::can_attack);b.cast=state.contains(cambc::champion_state::can_cast);
        b.channel=state.contains(cambc::champion_state::submitting_gems)?p::Channel::Submit:state.contains(cambc::champion_state::recalling)?p::Channel::Recall:
            state.contains(cambc::champion_state::teleporting)?p::Channel::Other:p::Channel::None;
        if(move)*move=raw.move_speed;if(attack)*attack=raw.attack_range;
        if(!b.alive)return p::detail::body(b);
        std::uint8_t restrictions=0;if(!checked(cambc::detail::sdk_champion_restrictions(id,&restrictions),e))return false;b.restrictions=restrictions;
        std::array<cambc::detail::buff_data,static_cast<unsigned>(cambc::buff_types::count)> buffs{};std::uint32_t count=0;
        if(!checked(cambc::detail::sdk_champion_buff_data(id,buffs.data(),buffs.size(),&count),e)||count>buffs.size())return false;
        for(unsigned i=0;i<count;++i) {
            if(buffs[i].type==static_cast<unsigned>(cambc::buff_types::stun))b.stunned=true;
            if(buffs[i].type==static_cast<unsigned>(cambc::buff_types::shield))b.shield+=buffs[i].data.shield.value;
        }
        if(b.channel!=p::Channel::None){std::int32_t timer=0;if(!checked(cambc::detail::sdk_champion_channel_time(id,&timer),e))return false;b.channel_remaining=static_cast<unsigned>(std::max(0,timer));}
        std::uint32_t gems=0;if(!checked(cambc::detail::sdk_champion_gems(id,&gems),e))return false;b.bag=gems;
        if(self) {
            std::uint32_t gold=0;if(!checked(cambc::detail::sdk_champion_gold(id,&gold),e))return false;b.gold=gold;
            std::uint32_t score=0;if(!checked(cambc::detail::sdk_bc_submitted_gems(own_team_,&score),e))return false;b.team_score=score;
            const double duration=cambc::constants::submit_gem_base_duration+std::ceil(static_cast<double>(gems)*cambc::constants::submit_gem_duration_multiplier);
            const double ticks=std::max<double>(cambc::constants::submit_gem_minimum_duration,duration);
            if(std::isfinite(ticks)&&ticks>0&&ticks<=10000)b.submit_ticks=static_cast<unsigned>(ticks);
        }
        const auto spawn=cambc::map::spawn_region(cambc::team::ally);if(!current(e))return false;b.at_spawn=spawn.is_set({raw.x,raw.y});if(!current(e))return false;
        std::uint32_t source_id=0;const int at=cambc::detail::sdk_map_camp_at(raw.x,raw.y,&source_id);if(!current(e))return false;
        if(at==0)b.on_any_source=true;else if(at!=static_cast<int>(cambc::error_code::api_not_found))cambc::detail::check(at);
        if(self)for(unsigned slot=0;slot<2;++slot) {
            cambc::detail::ability_props props{};if(!checked(cambc::detail::sdk_champion_ability_props(id,slot,&props),e))return false;
            std::int32_t cooldown=0;if(!checked(cambc::detail::sdk_champion_cooldown(id,slot,&cooldown),e))return false;
            Ability a;a.cooldown=cooldown;a.targets=props.targets;a.exclusive=(props.attributes&cambc::detail::ability_attribute_exclusive_with_movement)!=0;a.charged=props.max_charges!=0;
            switch(static_cast<cambc::ability_kind>(props.kind)) {
            case cambc::ability_kind::attack_reset:a.kind=p::Cast::Reset;b.reset_ready=cooldown==0;break;
            case cambc::ability_kind::shield:a.kind=p::Cast::Shield;a.range=props.payload.shield.range;b.shield_ready=cooldown==0;
                if(!checked(cambc::detail::sdk_champion_ability_shield(id,slot,&b.shield_amount),e))return false;break;
            case cambc::ability_kind::heal:a.kind=p::Cast::Heal;a.range=props.payload.heal.range;b.heal_ready=cooldown==0;b.heal_range=a.range;
                if(!checked(cambc::detail::sdk_champion_ability_heal(id,slot,&b.heal_amount),e))return false;break;
            case cambc::ability_kind::dash:a.kind=p::Cast::Dash;a.range=props.payload.dash.range;b.dash_ready=cooldown==0;
                a.damaging=props.payload.dash.modifier!=0||props.payload.dash.duration!=0;break;
            default:a.kind=p::Cast::Other;break;
            }
            if(abilities)(*abilities)[slot]=a;
        }
        return current(e)&&p::detail::body(b);
    }
public:
    explicit SdkHost(std::span<const cambc::camp> camps) {for(unsigned i=0;i<camps.size()&&i<p::max_camps;++i)camps_[i]=camps[i];}
    int owner(){return static_cast<int>(static_cast<cambc::handle_champion_t>(cambc::self()));}
    int round(){return static_cast<int>(cambc::round_number());}
    unsigned fuel(){return cambc::fuel_remaining();}
    bool current(Epoch e){return owner()==e.owner&&round()==e.round;}
    void read_words(std::array<std::uint16_t,4>& words){cambc::shared_memory.read(0,std::span<std::uint16_t,4>(words));}
    void write_word(unsigned offset,std::uint16_t word){cambc::shared_memory.write(offset,std::span<std::uint16_t,1>(&word,1));}
    bool observe(Epoch e,const p::Contract& c,Observation& out) {
        auto& f=out.frame;f.owner=e.owner;f.round=e.round;f.input=e.input;
        if(!body(e,e.owner,true,f.self,&out.abilities,&out.move_speed,&out.attack_range))return false;
        const auto own=static_cast<unsigned>(p::detail::member(c,e.owner));if(f.self.role!=c.members[own].role)return false;
        out.own_core_known=true;out.private_economy_known[own]=f.self.alive;
        cambc::detail::champion_info flags{};if(!checked(cambc::detail::sdk_champion(e.owner,&flags),e))return false;
        const auto availability=cambc::champion_state::from_bits_retain(flags.state);
        out.completed_turn=availability.contains(cambc::champion_state::completed_turn);
        out.recall_started=availability.contains(cambc::champion_state::recall_started);
        out.submit_started=availability.contains(cambc::champion_state::submit_gems_started);
        for(unsigned i=0;i<p::actors;++i) {
            if(i==own)f.peers[i]={f.self,true};else {p::Body b;if(body(e,c.members[i].id,false,b)&&b.role==c.members[i].role)f.peers[i]={b,true};}
            if(!current(e))return false;
            if(f.peers[i].observed_now) {auto& b=f.peers[i].body;b.at_station=b.position==c.members[i].station;b.at_capture=b.position==c.members[i].capture;
                b.altar_adjacent=p::distance(b.position,c.members[i].altar)<=cambc::constants::submit_gems_range;}
        }
        f.self=f.peers[own].body;f.group.id=c.group;f.group.count=c.camp_count;bool all=true,dead=true;
        for(unsigned i=0;i<c.camp_count;++i) {
            auto& sample=f.group.camps[i];sample.id=c.camp_ids[i];
            if(!camps_[i]||static_cast<int>(static_cast<cambc::handle_camp_t>(*camps_[i]))!=sample.id)return false;
            const auto pos=camps_[i]->spawn_position();sample.position={pos.x,pos.y};sample.attack_range=camps_[i]->attack_range();if(!current(e))return false;sample.geometry_known=true;
            std::uint32_t life=0;const int code=cambc::detail::sdk_camp_state(sample.id,&life);if(!current(e))return false;
            if(unavailable(code)){all=dead=false;continue;}cambc::detail::check(code);if(life>2)return false;
            sample.observed_now=true;sample.alive=life!=0;
            if(sample.alive){dead=false;cambc::detail::camp_state raw{};const int read=cambc::detail::sdk_camp(sample.id,&raw);if(!current(e))return false;
                if(unavailable(read)){sample.observed_now=false;all=false;continue;}cambc::detail::check(read);sample.hp=raw.health;sample.position={raw.x,raw.y};
                if(!p::detail::finite(sample.hp)||sample.hp<=0)return false;}
        }
        f.group.observed_now=all;f.group.whole_clear=all&&dead;f.fresh=current(e);return f.fresh;
    }
    bool legal(Epoch e,const Observation& o,const p::WordIntent& intent,const Primitive& s,bool walked,bool cast_used,bool exclusive_used) {
        const auto& b=o.frame.self;
        if(!current(e)||!b.alive||b.stunned||o.completed_turn||b.channel!=p::Channel::None)return false;
        if(s.kind==PrimitiveKind::Move) {
            if(!b.move||exclusive_used||(b.restrictions&cambc::buff_restrictions::move.bits())||s.destination==b.position
                ||o.move_speed<=0||p::distance(b.position,s.destination)>o.move_speed)return false;
            return path(e,b.position,s.destination,false,o.move_speed);
        }
        if(s.kind==PrimitiveKind::Basic) {
            if(!b.attack||(b.restrictions&cambc::buff_restrictions::attack.bits())||s.target!=intent.camp)return false;
            for(unsigned i=0;i<o.frame.group.count;++i){const auto& camp=o.frame.group.camps[i];if(camp.id==s.target)
                return camp.observed_now&&camp.alive&&p::distance(b.position,camp.position)<=o.attack_range;}
            return false;
        }
        if(s.kind==PrimitiveKind::Submit) {
            if(!b.bag||o.submit_started||!b.altar_adjacent||!(s.destination==intent.destination))return false;
            const cambc::vec2 altar{s.destination.x,s.destination.y};const bool known=cambc::vision::in_vision(altar);if(!current(e)||!known)return false;
            const bool exists=cambc::map::altars().is_set(altar);if(!current(e)||!exists)return false;
            const bool locked=cambc::vision::altar_lock_tiles(cambc::team::enemy).is_set(altar);return current(e)&&!locked;
        }
        if(s.kind==PrimitiveKind::Recall)return b.recall_ticks>0&&!o.recall_started;
        if(!b.cast||cast_used||(b.restrictions&cambc::buff_restrictions::cast.bits())||s.slot<0||s.slot>=2)return false;
        const auto& a=o.abilities[static_cast<unsigned>(s.slot)];if(a.charged||a.cooldown!=0||(a.exclusive&&walked))return false;
        if(s.kind==PrimitiveKind::Shield)return a.kind==p::Cast::Shield&&b.role==p::Role::Varg&&intent.selected_cast==p::Cast::Shield&&(a.targets&cambc::detail::ability_target_self);
        if(s.kind==PrimitiveKind::Reset)return a.kind==p::Cast::Reset&&!b.attack&&intent.selected_cast==p::Cast::Reset&&(a.targets&cambc::detail::ability_target_self);
        // Party's current Tape has no damaging-dash effect/HP-chain operator.
        // Stock harmful dashes are explicit unsupported, never disguised as a
        // BasicDamage receipt. Ordinary move+basic remains supported.
        if(s.kind==PrimitiveKind::Dash)return !a.damaging&&a.kind==p::Cast::Dash&&intent.selected_cast==p::Cast::Dash
            &&(a.targets&cambc::detail::ability_target_ground)&&path(e,b.position,s.destination,true,a.range);
        if(s.kind==PrimitiveKind::Heal) {
            if(a.kind!=p::Cast::Heal||intent.selected_cast!=p::Cast::Heal||s.target!=intent.recipient)return false;
            for(const auto& peer:o.frame.peers)if(peer.observed_now&&peer.body.id==s.target)return peer.body.alive&&peer.body.hp<peer.body.max_hp
                &&p::distance(b.position,peer.body.position)<=a.range&&(a.targets&cambc::detail::ability_target_allies);
        }return false;
    }
    bool apply(Epoch,const Primitive& s) {
        // Public action wrappers first import require_self(), leaving an
        // unfenced suspension seam before the command. This authenticated VM
        // dispatches exactly ONE raw action import per checked primitive.
        if(s.kind==PrimitiveKind::Move){std::int32_t accepted=0;cambc::detail::check(cambc::detail::sdk_move_to(s.destination.x,s.destination.y,&accepted));return accepted!=0;}
        if(s.kind==PrimitiveKind::Basic){cambc::detail::check(cambc::detail::sdk_attack(static_cast<unsigned>(cambc::detail::entity_kind::camp),s.target));return true;}
        if(s.kind==PrimitiveKind::Submit){cambc::detail::check(cambc::detail::sdk_submit_gems(s.destination.x,s.destination.y));return true;}
        if(s.kind==PrimitiveKind::Recall){cambc::detail::check(cambc::detail::sdk_recall());return true;}
        std::int32_t accepted=0;const bool target=s.kind==PrimitiveKind::Heal;const bool ground=s.kind==PrimitiveKind::Dash;
        cambc::detail::check(cambc::detail::sdk_cast(s.slot,target?static_cast<unsigned>(cambc::detail::entity_kind::champion):0,
            target?s.target:0,ground?s.destination.x:-1,ground?s.destination.y:-1,&accepted));return accepted!=0;
    }
};
#endif
}
