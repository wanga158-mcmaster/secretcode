#pragma once
#include "capital_party.h"
#include <array>
#include <cstdint>
#include <span>

// Adaptive, SDK-free named service ownership. This is an interpreter contract,
// not a source of vision, guard truth, future support, or comparative utility.
// Every offer is unguarded until the caller prices its complete word and all
// refusal branches in the declared domain. No forecast becomes an actual tape.
// In particular, NPC nearest-target ownership says nothing about player aim.
namespace wb::capital::party_program {
namespace p=party;
using Revision=std::uint64_t;
enum class Phase {Commit,Assemble,Enter,Work,Capture,Release,Bank,Recover,Renew,Abort,Handoff,Complete};
enum class Kind {None,PublishCommit,Assemble,Enter,WorkTank,Heal,Strike,Capture,Release,Bank,HoldChannel,Recover,HoldRecovery,Renew,AbortExit,Handoff};
enum class Status {NoOffer,Ready,NeedObservation,NeedModel,NeedCommitment,AwaitActual,Stale,Refused,Aborting,Handoff,Complete,Invalid};
enum class Failure {None,Commitment,Deadline,Death,ChangedLife,ChangedProtocol,UnknownEffect,Refusal,NoProgress,Interrupted,SourceCustody};
enum class Risk {Unknown,ConditionalModel,NpcAndReplyCertificate};
struct Domain {Revision id{},rules{},world{},replies{},services{},assumptions{};int start{-1},horizon{-1};Risk risk{Risk::Unknown};bool operator==(const Domain&) const=default;};
struct Contract {
    Revision id{},life{};int group{-1},start{-1},commit_by{-1},arrive_by{-1},work_by{-1},capture_by{-1},bank_by{-1},renew_by{-1},abort_by{-1};
    std::array<p::Member,p::actors> members{};
    std::array<int,p::max_camps> camps{};unsigned camp_count{},capture_mask{};
    unsigned max_refusals{},max_no_progress{};bool require_useful_support{};
    Domain domain{};
};
// This admits a conditional playable programme, not a claim it beats solos.
// All three independent fallback interpreters must actually exist. An omitted
// comparison remains Unknown instead of assigning the displaced bodies zero.
struct Admission {
    bool canonical{},accepted_roles{},exclusive_wire{},legacy_publish_disabled{},fixed_priority_protocol{},conditional_risk_admitted{};
    std::array<bool,p::actors> complete_independent_interpreters{};
    bool valid() const {return canonical&&accepted_roles&&exclusive_wire&&legacy_publish_disabled&&fixed_priority_protocol
        &&conditional_risk_admitted&&complete_independent_interpreters[0]&&complete_independent_interpreters[1]&&complete_independent_interpreters[2];}
};
struct Epoch {int owner{-1},round{-1};Revision input{},sample{},effects{},geometry{},foes{},locks{},order{},life{};bool operator==(const Epoch&) const=default;};
struct Frame {
    Epoch source{};p::Body self{};std::array<p::Peer,p::actors> peers{};p::Group group{};
    bool fresh{},protocol_armed{},canonical_header{},priority_contract_current{},capital_obligation_clear{},independent_interpreter_ready{};
    // Current contractual commitments, not a guess from colocation. A wire
    // producer must bind these to this contract/role and reject abort records.
    std::array<bool,p::actors> commitment_current{};
    bool group_life_known{},source_released{},altar_unlocked_known{},altar_unlocked{};
    int nominated_target{-1};
};
struct Offer {
    Epoch source{};Revision contract{},serial{};Kind kind{Kind::None};p::Point destination{};int target{-1};p::Cast cast{p::Cast::None};
    bool operator==(const Offer&) const=default;
};
struct Quote {
    Epoch source{};Revision contract{},offer_serial{},word{};Domain domain{};
    // Entire legal selected word + actual refresh/refusal/return obligations.
    // A conditional quote need not pretend its enemy model is a certificate.
    bool complete_word{},funded_tail{},fresh_legality{},individual_npc{},ordered_effects{},refusal_closure{},station_commitments{};
    // A work quote borrowing a heal needs the actual useful receipt's event.
    Revision required_support_event{};
    // Optional secondary camp in a Heal -> Basic word. The Heal recipient
    // remains Offer.target; this camp is separately observed and admitted.
    int camp_target{-1};
};
struct Lease {Offer offer{};Revision word{},token{};bool valid{};int camp_target{-1};};
enum class ReceiptKind {None,CommitPublished,CampDamage,Shield,Heal,GroupPayout,BankPayout,Renewed,IndependentHandoff};
struct Actual {
    ReceiptKind kind{ReceiptKind::None};Revision event{},contract{},life{},word{};int actor{-1},group{-1},target{-1};
    float before{},after{};std::uint64_t gems{},gold{};
    // observed_actual means a caller-authenticated SDK readback/actual event,
    // never predicted damage, accepted Submit, or a remote numeric claim.
    bool observed_actual{},accepted{},complete_word{},refused{},unknown_suffix{},source_covered{},bank_source_attribution_known{};
};
struct Progress {
    bool committed{},arrived{},entered{},positive_work{},useful_support{},payout{},released{},banked{},recovered{},renewed{},handoff{},cleanup_ack{};
    int report_round{-1};p::Milestone latest{p::Milestone::Unpublished};
};
struct Quantities {float work{},useful_heal{},shield{};std::uint64_t received_gems{},received_gold{},banked_gems{};bool history_known{true};};
struct Result {Status status{Status::NoOffer};Failure failure{Failure::None};Offer offer{};Lease lease{};};
struct Completion {
    bool lifecycle_attested{},complete_observed{},useful_support_observed{},wire_owned{},physical_owned{};
    Failure failure{Failure::None};std::array<Progress,p::actors> members{};
    std::array<Quantities,p::actors> quantities{};std::array<bool,p::actors> quantity_history_known{};
};
// Model forecasts and actual observations have distinct tags; only same-kind,
// same-domain complete alternatives can be compared. No weighted conversion of
// gold, health or unbanked gems, and no unknown displaced service defaults zero.
enum class Evidence {Unknown,Forecast,Actual};
struct Service {
    int actor{-1};Domain domain{};Evidence evidence{Evidence::Unknown};Revision reply{};
    bool complete{},restored_service{},capital_tail_included{},custody_known{};
    std::uint64_t scored{},carried{},gold{};
};
struct Comparison {bool comparable{},party_better{},independent_better{};std::uint64_t party_scored{},independent_scored{};};
inline Comparison compare(const Contract& c,const std::array<Service,p::actors>& joint,const std::array<Service,p::actors>& solo) {
    Comparison out;Evidence evidence=Evidence::Unknown;Revision reply=0;
    for(unsigned i=0;i<p::actors;++i) {
        const auto& a=joint[i];const auto& b=solo[i];
        if(a.actor!=c.members[i].id||b.actor!=a.actor||a.domain!=c.domain||b.domain!=c.domain||!a.complete||!b.complete
            ||!a.restored_service||!b.restored_service||!a.capital_tail_included||!b.capital_tail_included||!a.custody_known||!b.custody_known
            ||a.evidence==Evidence::Unknown||a.evidence!=b.evidence||!a.reply||a.reply!=b.reply||a.carried!=b.carried||a.gold!=b.gold)return out;
        if(i&&(a.evidence!=evidence||a.reply!=reply))return out;
        evidence=a.evidence;reply=a.reply;
        if(UINT64_MAX-out.party_scored<a.scored||UINT64_MAX-out.independent_scored<b.scored)return {};
        out.party_scored+=a.scored;out.independent_scored+=b.scored;
    }
    out.comparable=true;out.party_better=out.party_scored>out.independent_scored;out.independent_better=out.party_scored<out.independent_scored;return out;
}
namespace detail {
inline int member(const Contract& c,int id){for(unsigned i=0;i<p::actors;++i)if(c.members[i].id==id)return static_cast<int>(i);return -1;}
inline bool domain(const Domain& d){return d.id&&d.rules&&d.world&&d.replies&&d.services&&d.assumptions&&d.start>=0&&d.horizon>d.start&&d.risk!=Risk::Unknown;}
inline bool valid(const Contract& c) {
    if(!c.id||!c.life||c.group<0||c.group>31||c.start<0||!c.camp_count||c.camp_count>p::max_camps||!c.capture_mask||(c.capture_mask&~7u)
        ||!domain(c.domain)||c.domain.start!=c.start||c.domain.horizon<c.renew_by||c.commit_by<c.start||c.arrive_by<c.commit_by||c.work_by<c.arrive_by
        ||c.capture_by<c.work_by||c.bank_by<c.capture_by||c.renew_by<c.bank_by||c.abort_by<c.renew_by||!c.max_refusals||!c.max_no_progress)return false;
    for(unsigned i=0;i<p::actors;++i){const auto& m=c.members[i];if(m.id<0||m.role!=static_cast<p::Role>(i)||!p::detail::point(m.rendezvous)||!p::detail::point(m.station)
        ||!p::detail::point(m.capture)||!p::detail::point(m.release)||!p::detail::point(m.altar)||!p::detail::point(m.spawn)||!p::detail::point(m.renewal))return false;
        for(unsigned j=0;j<i;++j)if(m.id==c.members[j].id)return false;}
    for(unsigned i=0;i<c.camp_count;++i){if(c.camps[i]<0)return false;for(unsigned j=0;j<i;++j)if(c.camps[i]==c.camps[j])return false;}
    return true;
}
inline bool frame_core(const Contract& c,int owner,const Frame& f){return f.fresh&&f.source.owner==owner&&f.source.round>=c.start&&f.source.input&&f.source.sample
    &&f.self.id==owner&&p::detail::body(f.self)&&f.self.role==c.members[static_cast<unsigned>(member(c,owner))].role;}
inline bool frame(const Contract& c,int owner,const Frame& f){return frame_core(c,owner,f)&&f.source.life==c.life;}
inline bool later(const Epoch& a,const Epoch& b) {
    return a.owner==b.owner&&(a.round>b.round||(a.round==b.round&&a.input==b.input&&a.sample>b.sample));
}
inline bool same(const Frame& a,const Frame& b) {
    if(a.source!=b.source||!(a.self==b.self)||a.fresh!=b.fresh||a.protocol_armed!=b.protocol_armed||a.canonical_header!=b.canonical_header
        ||a.priority_contract_current!=b.priority_contract_current||a.capital_obligation_clear!=b.capital_obligation_clear
        ||a.independent_interpreter_ready!=b.independent_interpreter_ready||a.commitment_current!=b.commitment_current||a.group_life_known!=b.group_life_known
        ||a.source_released!=b.source_released||a.altar_unlocked_known!=b.altar_unlocked_known||a.altar_unlocked!=b.altar_unlocked||a.nominated_target!=b.nominated_target
        ||a.group.id!=b.group.id||a.group.count!=b.group.count||a.group.observed_now!=b.group.observed_now||a.group.whole_clear!=b.group.whole_clear
        ||a.group.capture_own!=b.group.capture_own||a.group.contested!=b.group.contested||a.group.claimed!=b.group.claimed||a.group.count>p::max_camps)return false;
    for(unsigned i=0;i<p::actors;++i)if(a.peers[i].observed_now!=b.peers[i].observed_now||(a.peers[i].observed_now&&!(a.peers[i].body==b.peers[i].body)))return false;
    for(unsigned i=0;i<a.group.count;++i){const auto& x=a.group.camps[i];const auto& y=b.group.camps[i];if(x.id!=y.id||x.hp!=y.hp||x.observed_now!=y.observed_now
        ||x.alive!=y.alive||x.position!=y.position||x.attack_range!=y.attack_range||x.geometry_known!=y.geometry_known)return false;}
    return true;
}
}
class Program {
    Contract c_{};Admission admission_{};int owner_{-1},self_{-1};Phase phase_{Phase::Commit};Failure failure_{Failure::None};
    Frame actual_{};bool observed_{},wire_owned_{},pending_{},attempted_{},word_progress_{},heal_confirmed_{};Offer issued_{};Lease lease_{};
    Epoch prefix_source_{};p::Point origin_pose_{};unsigned origin_channel_{};
    std::array<Progress,p::actors> progress_{};std::array<Quantities,p::actors> quantities_{};
    Revision serial_{},token_{},last_event_{},last_support_{};unsigned refusals_{},no_progress_{};int target_{-1};
    bool all(bool Progress::* field) const {for(const auto& m:progress_)if(!(m.*field))return false;return true;}
    Progress& own(){return progress_[static_cast<unsigned>(self_)];}
    const p::Member& member() const{return c_.members[static_cast<unsigned>(self_)];}
    void abort(Failure reason){if(phase_!=Phase::Complete&&phase_!=Phase::Handoff){failure_=reason;phase_=Phase::Abort;}}
    bool group_known() const {
        if(!actual_.group_life_known||!actual_.group.observed_now||actual_.group.id!=c_.group||actual_.group.count!=c_.camp_count)return false;
        for(unsigned i=0;i<c_.camp_count;++i){bool found=false;for(unsigned j=0;j<actual_.group.count;++j){const auto& x=actual_.group.camps[j];if(x.id==c_.camps[i]){
            if(found||!x.observed_now||!p::detail::finite(x.hp)||(x.alive?x.hp<=0:x.hp!=0))return false;found=true;}}if(!found)return false;}return true;
    }
    int live_target() const {
        int best=-1;float hp=0;
        for(unsigned i=0;i<actual_.group.count;++i){const auto& x=actual_.group.camps[i];if(!x.alive)continue;
            if(x.id==actual_.nominated_target)return x.id;
            if(best<0||x.hp<hp||(x.hp==hp&&x.id<best)){best=x.id;hp=x.hp;}}
        return best;
    }
    bool apply(const Actual& a,Kind expected) {
        if(!a.observed_actual||!a.accepted||a.event<=last_event_||a.contract!=c_.id||a.life!=c_.life||a.actor!=owner_)return false;
        bool progressed=false;auto& m=own();auto& q=quantities_[static_cast<unsigned>(self_)];
        switch(a.kind){
        case ReceiptKind::CommitPublished:if(expected==Kind::PublishCommit){m.committed=true;progressed=true;}break;
        case ReceiptKind::CampDamage:if((expected==Kind::WorkTank||expected==Kind::Strike||(expected==Kind::Heal&&heal_confirmed_&&lease_.camp_target==a.target))&&a.group==c_.group&&p::detail::finite(a.before)&&p::detail::finite(a.after)&&a.before>a.after){q.work+=a.before-a.after;m.positive_work=true;progressed=true;}break;
        case ReceiptKind::Heal:if(expected==Kind::Heal&&a.target==c_.members[0].id&&p::detail::finite(a.before)&&p::detail::finite(a.after)&&a.after>a.before){q.useful_heal+=a.after-a.before;m.useful_support=true;last_support_=a.event;heal_confirmed_=true;progressed=true;}break;
        case ReceiptKind::Shield:if(expected==Kind::WorkTank&&p::detail::finite(a.before)&&p::detail::finite(a.after)&&a.after>a.before){q.shield+=a.after-a.before;progressed=true;}break;
        case ReceiptKind::GroupPayout:if(a.source_covered&&a.group==c_.group&&m.entered&&!m.payout){q.received_gems=a.gems;q.received_gold=a.gold;m.payout=true;if(!a.gems)m.banked=true;progressed=true;}break;
        case ReceiptKind::BankPayout:if(a.bank_source_attribution_known&&m.payout&&a.gems<=q.received_gems-q.banked_gems){q.banked_gems+=a.gems;m.banked=q.banked_gems==q.received_gems;progressed=true;}break;
        case ReceiptKind::Renewed:if(expected==Kind::Renew&&actual_.independent_interpreter_ready&&actual_.capital_obligation_clear&&m.recovered){m.renewed=true;progressed=true;}break;
        case ReceiptKind::IndependentHandoff:case ReceiptKind::None:break;
        }
        // Even a rejected semantic attribution consumes this actual event ID;
        // it cannot be replayed later against another word kind.
        last_event_=a.event;return progressed;
    }
    bool ticket(const Lease& l) const {
        return pending_&&l.valid&&lease_.valid&&l.token==lease_.token&&l.word==lease_.word&&l.offer==lease_.offer&&l.camp_target==lease_.camp_target;
    }
    bool after_frame(const Frame& f,bool allow_same_prefix) const {
        if(self_<0||!detail::frame_core(c_,owner_,f)||f.source.round<actual_.source.round)return false;
        if(f.source.round==actual_.source.round&&(f.source.input!=actual_.source.input||f.source.sample<actual_.source.sample))return false;
        if(f.source==actual_.source&&!detail::same(f,actual_))return false;
        return detail::later(f.source,prefix_source_)||(allow_same_prefix&&f.source==prefix_source_&&detail::same(f,actual_));
    }
    bool known_prefix(const Actual& a,Kind expected,Revision word,Revision previous) const {
        if(!a.observed_actual||!a.accepted||a.refused||a.unknown_suffix||a.event<=previous||a.contract!=c_.id||a.life!=c_.life||a.actor!=owner_||a.word!=word)return false;
        switch(a.kind) {
        case ReceiptKind::None:return true;
        case ReceiptKind::CommitPublished:return expected==Kind::PublishCommit;
        case ReceiptKind::CampDamage:{bool target=false;for(unsigned i=0;i<c_.camp_count;++i)target|=c_.camps[i]==a.target;
            return (expected==Kind::WorkTank||expected==Kind::Strike||(expected==Kind::Heal&&heal_confirmed_&&a.target==lease_.camp_target))&&target&&a.group==c_.group&&p::detail::finite(a.before)&&p::detail::finite(a.after)&&a.before>=a.after;}
        case ReceiptKind::Shield:return expected==Kind::WorkTank&&p::detail::finite(a.before)&&p::detail::finite(a.after)&&a.after>=a.before;
        case ReceiptKind::Heal:return expected==Kind::Heal&&a.target==c_.members[0].id&&p::detail::finite(a.before)&&p::detail::finite(a.after)&&a.after>=a.before;
        case ReceiptKind::Renewed:return expected==Kind::Renew;
        // Delayed source/conversion receipts enter through receipt(), before
        // another physical word; they are not an invented primitive effect.
        case ReceiptKind::GroupPayout:case ReceiptKind::BankPayout:case ReceiptKind::IndependentHandoff:return false;
        }
        return false;
    }
    void advance() {
        if(!observed_||phase_==Phase::Abort||phase_==Phase::Handoff||phase_==Phase::Complete)return;
        auto& m=own();const auto& b=actual_.self;
        if(!b.alive){abort(Failure::Death);return;}
        if(!actual_.protocol_armed||!actual_.canonical_header||!actual_.priority_contract_current){abort(Failure::ChangedProtocol);return;}
        if(phase_==Phase::Commit&&all(&Progress::committed))phase_=Phase::Assemble;
        if(phase_==Phase::Assemble&&b.position==member().rendezvous)m.arrived=true;
        if(phase_==Phase::Assemble&&all(&Progress::arrived))phase_=Phase::Enter;
        if(phase_==Phase::Enter&&b.position==member().station)m.entered=true;
        if(phase_==Phase::Enter&&all(&Progress::entered))phase_=Phase::Work;
        if(phase_==Phase::Work&&group_known()){
            target_=live_target();if(target_<0&&actual_.group.whole_clear)phase_=Phase::Capture;
        }
        if(phase_==Phase::Capture&&all(&Progress::payout))phase_=Phase::Release;
        if(phase_==Phase::Release&&actual_.source_released&&b.position==member().release&&b.npc_quiet)m.released=true;
        if(phase_==Phase::Release&&all(&Progress::released))phase_=Phase::Bank;
        if(phase_==Phase::Bank&&all(&Progress::banked))phase_=Phase::Recover;
        if(phase_==Phase::Recover&&b.at_spawn&&b.hp==b.max_hp&&!b.stunned&&b.channel==p::Channel::None&&actual_.capital_obligation_clear)m.recovered=true;
        if(phase_==Phase::Recover&&all(&Progress::recovered))phase_=Phase::Renew;
        if(phase_==Phase::Renew&&all(&Progress::renewed)){phase_=Phase::Complete;}
        const int now=actual_.source.round;
        if((phase_==Phase::Commit&&now>c_.commit_by)||(phase_==Phase::Assemble&&now>c_.arrive_by)||(phase_==Phase::Enter&&now>c_.arrive_by)
            ||(phase_==Phase::Work&&now>c_.work_by)||(phase_==Phase::Capture&&now>c_.capture_by)||(phase_==Phase::Release&&now>c_.bank_by)
            ||(phase_==Phase::Bank&&now>c_.bank_by)||((phase_==Phase::Recover||phase_==Phase::Renew)&&now>c_.renew_by))abort(Failure::Deadline);
    }
public:
    Program(const Contract& c,int owner,Admission a):c_(c),admission_(a),owner_(owner),self_(detail::member(c,owner)) {
        for(auto& q:quantities_)q.history_known=false;if(self_>=0)quantities_[static_cast<unsigned>(self_)].history_known=true;
    }
    const Contract& contract() const{return c_;}Phase phase() const{return phase_;}Failure failure() const{return failure_;}
    bool wire_owned() const{return wire_owned_;}bool awaiting_actual() const{return pending_;}
    Revision support_event() const{return last_support_;}
    Status observe(const Frame& f) {
        if(self_<0||!detail::valid(c_)||!admission_.valid())return Status::NoOffer;
        if(!detail::frame(c_,owner_,f))return Status::NeedObservation;
        if(observed_&&(f.source.round<actual_.source.round||(f.source.round==actual_.source.round
            &&(f.source.input!=actual_.source.input||f.source.sample<actual_.source.sample))))return Status::Stale;
        if(observed_&&f.source==actual_.source){
            if(detail::same(f,actual_))return pending_?Status::AwaitActual:Status::Ready;
            // Equal stamps with different facts are a conflicting producer
            // packet, never fresh authority for the old quote.
            if(pending_&&!attempted_){pending_=false;lease_.valid=false;}observed_=false;issued_={};return Status::NeedObservation;
        }
        // A quote is invocation-bound. Merely preparing it creates no durable
        // action authority across a changed observation. A command already
        // attempted is different: retain its ticket for actual reconciliation.
        if(pending_&&!attempted_){pending_=false;lease_.valid=false;}
        actual_=f;observed_=true;wire_owned_=true;
        if(!pending_)advance();return pending_?Status::AwaitActual:Status::Ready;
    }
    // Capture/tribute resolve before a later owner input, not because Submit
    // was accepted. Reconcile these delayed actual events BEFORE next(), even
    // during channel/stun. No lease or physical command is fabricated here.
    bool receipt(const Actual& a) {
        if(!observed_||pending_||a.word||(a.kind!=ReceiptKind::GroupPayout&&a.kind!=ReceiptKind::BankPayout))return false;
        const bool accepted=apply(a,Kind::None);if(accepted)no_progress_=0;advance();return accepted;
    }
    // Only the trusted whole-wire consumer can manufacture this type. Numeric
    // remote histories remain unknown even after Renewed/ClosedClean reports.
    p::IngestStatus ingest(const p::TrustedPredicateReceipt& r) {
        if(self_<0||!observed_||pending_)return p::IngestStatus::PendingLocalWord;
        const int i=detail::member(c_,r.actor);
        if(i<0||r.actor==owner_||r.contract!=c_.id||r.role!=static_cast<p::Role>(i)||r.header_start!=c_.start||r.header_group!=c_.group)return p::IngestStatus::Invalid;
        if(!r.active_armed||!r.whole_read||!r.trusted_partition_and_producer||!r.predicate||r.read_owner!=owner_||r.read_round!=actual_.source.round
            ||r.read_input!=actual_.source.input||r.source_round<c_.start||r.source_round>r.read_round)return p::IngestStatus::Unverified;
        auto& m=progress_[static_cast<unsigned>(i)];if(r.source_round<m.report_round)return p::IngestStatus::Stale;
        if(p::detail::cancellation(r.milestone)){abort(Failure::Commitment);return p::IngestStatus::Cancelled;}
        if(static_cast<unsigned>(r.milestone)>static_cast<unsigned>(p::Milestone::ClosedClean))return p::IngestStatus::Invalid;
        if(r.source_round==m.report_round&&r.milestone==m.latest)return p::IngestStatus::Duplicate;
        if(static_cast<unsigned>(r.milestone)<static_cast<unsigned>(m.latest))return p::IngestStatus::Conflict;
        m.report_round=r.source_round;m.latest=r.milestone;const auto rank=static_cast<unsigned>(r.milestone);
        m.committed=rank>=static_cast<unsigned>(p::Milestone::Committed);m.arrived=rank>=static_cast<unsigned>(p::Milestone::Arrived);
        m.entered=rank>=static_cast<unsigned>(p::Milestone::Entered);m.positive_work=rank>=static_cast<unsigned>(p::Milestone::WorkPrefix);
        m.payout=rank>=static_cast<unsigned>(p::Milestone::Payout);m.released=rank>=static_cast<unsigned>(p::Milestone::Released);
        m.banked=rank>=static_cast<unsigned>(p::Milestone::Banked);m.recovered=rank>=static_cast<unsigned>(p::Milestone::Recovered);
        m.renewed=rank>=static_cast<unsigned>(p::Milestone::Renewed);advance();return p::IngestStatus::Accepted;
    }
    Result next() {
        Result out;if(self_<0||!observed_||!admission_.valid())return out;
        if(pending_){out.status=Status::AwaitActual;return out;}advance();out.failure=failure_;
        if(phase_==Phase::Complete){out.status=Status::Complete;return out;}
        if(phase_==Phase::Handoff){out.status=Status::Handoff;return out;}
        Offer o;o.source=actual_.source;o.contract=c_.id;o.serial=++serial_;const auto& b=actual_.self;
        if(phase_==Phase::Abort){
            if(!b.alive||actual_.source.round>c_.abort_by||(actual_.source_released&&b.npc_quiet&&actual_.independent_interpreter_ready)){
                o.kind=Kind::Handoff;out.status=Status::Handoff;out.offer=o;issued_=o;return out;}
            o.kind=Kind::AbortExit;o.destination=member().release;
        } else {
            if(phase_!=Phase::Commit)for(unsigned i=0;i<p::actors;++i)if(!actual_.commitment_current[i]){abort(Failure::Commitment);return next();}
            if(b.stunned||b.channel==p::Channel::Other){out.status=Status::NeedModel;return out;}
            switch(phase_) {
            case Phase::Commit:o.kind=Kind::PublishCommit;break;
            case Phase::Assemble:o.kind=Kind::Assemble;o.destination=member().rendezvous;break;
            case Phase::Enter:o.kind=Kind::Enter;o.destination=member().station;break;
            case Phase::Work:{if(!group_known()){out.status=Status::NeedObservation;return out;}
                const auto& tank=actual_.peers[0];
                if(self_==1&&tank.observed_now&&tank.body.alive&&tank.body.id==c_.members[0].id&&b.heal_ready&&b.cast
                    &&tank.body.hp<tank.body.max_hp&&p::distance(b.position,tank.body.position)<=b.heal_range){o.kind=Kind::Heal;o.target=tank.body.id;o.cast=p::Cast::Heal;}
                else{o.kind=self_==0?Kind::WorkTank:Kind::Strike;o.target=target_;}
                break;}
            case Phase::Capture:if(!group_known()){out.status=Status::NeedObservation;return out;}o.kind=Kind::Capture;o.destination=member().capture;break;
            case Phase::Release:o.kind=Kind::Release;o.destination=member().release;break;
            case Phase::Bank:
                if(b.channel==p::Channel::Submit)o.kind=Kind::HoldChannel;
                else {o.kind=Kind::Bank;o.destination=member().altar;}break;
            case Phase::Recover:o.kind=b.channel==p::Channel::Recall?Kind::HoldRecovery:Kind::Recover;o.destination=member().spawn;break;
            case Phase::Renew:o.kind=Kind::Renew;o.destination=member().renewal;break;
            default:out.status=Status::NeedModel;return out;
            }
        }
        issued_=o;out.status=Status::Ready;out.offer=o;return out;
    }
    Result seal(const Offer& offer,const Quote& q) {
        Result out;out.failure=failure_;if(pending_){out.status=Status::AwaitActual;return out;}
        if(!observed_||offer!=issued_||offer.source!=actual_.source||q.source!=actual_.source||q.contract!=c_.id||q.offer_serial!=offer.serial||!q.word||q.domain!=c_.domain){out.status=Status::Stale;return out;}
        if(offer.kind==Kind::Handoff||!q.complete_word||!q.funded_tail||!q.fresh_legality||!q.individual_npc||!q.ordered_effects||!q.refusal_closure||!q.station_commitments
            ||(q.required_support_event&&q.required_support_event!=last_support_)){out.status=Status::NeedModel;return out;}
        if(q.camp_target>=0){bool live=false;if(offer.kind!=Kind::Heal||!actual_.self.attack||!group_known()){out.status=Status::NeedModel;return out;}
            for(unsigned i=0;i<actual_.group.count;++i){const auto& camp=actual_.group.camps[i];live|=camp.id==q.camp_target&&camp.observed_now&&camp.alive&&camp.hp>0;}
            if(!live){out.status=Status::NeedModel;return out;}}
        lease_={offer,q.word,++token_,true,q.camp_target};pending_=true;attempted_=word_progress_=heal_confirmed_=false;prefix_source_=offer.source;
        origin_pose_=actual_.self.position;origin_channel_=actual_.self.channel_remaining;
        out.status=Status::Ready;out.offer=offer;out.lease=lease_;return out;
    }
    bool begin_attempt(const Lease& l) {
        if(!observed_||!pending_||attempted_||!l.valid||l.token!=lease_.token||l.word!=lease_.word||l.offer!=lease_.offer||l.camp_target!=lease_.camp_target
            ||l.offer.source!=actual_.source)return false;
        attempted_=true;return true;
    }
    // A known accepted primitive can have several independently observed camp
    // deltas (e.g. splash). Validate the entire bounded span before recording
    // ANY event. The attempted whole-word ticket remains live; the Native
    // interpreter must separately refresh/guard every later physical command.
    Status prefix(const Lease& l,const Frame& after,std::span<const Actual> receipts) {
        if(!ticket(l)||!attempted_)return Status::Stale;
        if(!after_frame(after,false))return Status::NeedObservation;
        if(receipts.size()>p::max_effects)return Status::NeedModel;
        Revision previous=last_event_;
        for(const auto& a:receipts){if(!known_prefix(a,l.offer.kind,l.word,previous))return Status::NeedModel;previous=a.event;}
        const auto before_pose=actual_.self.position;const auto before_channel=actual_.self.channel_remaining;
        actual_=after;observed_=true;prefix_source_=after.source;
        for(const auto& a:receipts)word_progress_|=apply(a,l.offer.kind);
        word_progress_|=after.self.position!=before_pose||after.self.channel_remaining!=before_channel;
        if(word_progress_)no_progress_=0;
        return Status::Ready;
    }
    Status prefix(const Lease& l,const Frame& after,const Actual& receipt) {
        return prefix(l,after,std::span<const Actual>(&receipt,1));
    }
    // Consume a KNOWN UNISSUED remainder. No refusal or unknown effect is
    // fabricated. None means fresh nomination/repricing may continue (a killed
    // target does not end the owner's input); a real refutation enters abort.
    Status interrupt(const Lease& l,const Frame& after,Failure reason=Failure::None) {
        if(!ticket(l))return Status::Stale;
        if(reason==Failure::UnknownEffect||reason==Failure::Refusal)return Status::NeedModel;
        if(!after_frame(after,true))return Status::NeedObservation;
        actual_=after;observed_=true;prefix_source_=after.source;pending_=attempted_=false;lease_.valid=false;issued_={};
        if(after.source.life!=c_.life)abort(Failure::ChangedLife);
        else if(reason!=Failure::None)abort(reason);
        advance();return phase_==Phase::Abort?Status::Aborting:Status::Ready;
    }
    Status reconcile(const Lease& l,const Frame& after,const Actual& a) {
        if(!ticket(l)||!attempted_)return Status::Stale;
        const bool new_command=a.observed_actual||a.refused||a.unknown_suffix;
        if(!after_frame(after,!new_command))return Status::NeedObservation;
        if((new_command||a.accepted||a.complete_word)&&a.word!=l.word)return Status::Stale;
        actual_=after;observed_=true;pending_=attempted_=false;lease_.valid=false;issued_={};prefix_source_=after.source;
        const bool progressed=a.word==l.word&&apply(a,l.offer.kind);auto& q=quantities_[static_cast<unsigned>(self_)];
        if(a.refused&&++refusals_>=c_.max_refusals)abort(Failure::Refusal);
        if(a.unknown_suffix){q.history_known=false;abort(Failure::UnknownEffect);}
        if(after.source.life!=c_.life)abort(Failure::ChangedLife);
        // A changed actual pose is useful assembly/exit progress even without
        // a numeric work receipt. NoProgress is counted only for a complete,
        // accepted word with unchanged pose and no dated useful effect.
        if(word_progress_||progressed||after.self.position!=origin_pose_||after.self.channel_remaining!=origin_channel_)no_progress_=0;
        else if(a.complete_word&&a.accepted&&++no_progress_>=c_.max_no_progress)abort(Failure::NoProgress);
        advance();return phase_==Phase::Abort?Status::Aborting:a.refused?Status::Refused:Status::Ready;
    }
    // Metadata closure is explicit. Physical handoff NEVER re-enables legacy
    // whole-eight-byte writers; retain wire ownership until all three owners
    // authenticate cleanup and there is no private pending command/write.
    bool handoff(const Frame& f,bool interpreter_accepted) {
        if(pending_||!interpreter_accepted||!detail::frame(c_,owner_,f)||!f.independent_interpreter_ready||phase_!=Phase::Abort)return false;
        if(f.self.alive&&!(f.source_released&&f.self.npc_quiet)&&f.source.round<=c_.abort_by)return false;
        actual_=f;own().handoff=true;phase_=Phase::Handoff;return true;
    }
    // Explicit operational transfer of the outstanding exit/recovery service.
    // This does not attest that the still-exposed body has already departed,
    // recovered, banked, renewed or survived the accepted interpreter's future
    // commands. The Native caller authenticates its complete funded grant.
    bool handoff_paid_exit(const Frame& f,bool interpreter_accepted) {
        if(pending_||!interpreter_accepted||!observed_||phase_!=Phase::Abort||!detail::frame(c_,owner_,f)
            ||f.source!=actual_.source||!detail::same(f,actual_)||!f.independent_interpreter_ready)return false;
        actual_=f;own().handoff=true;phase_=Phase::Handoff;return true;
    }
    bool cleanup(int actor,Revision contract,bool authenticated,bool no_pending) {
        const int i=detail::member(c_,actor);if(i<0||contract!=c_.id||!authenticated||!no_pending||pending_||(phase_!=Phase::Handoff&&phase_!=Phase::Complete))return false;
        progress_[static_cast<unsigned>(i)].cleanup_ack=true;if(all(&Progress::cleanup_ack))wire_owned_=false;return true;
    }
    Completion completion() const {
        Completion out;out.lifecycle_attested=phase_==Phase::Complete;out.complete_observed=out.lifecycle_attested;out.failure=failure_;out.wire_owned=wire_owned_;out.physical_owned=pending_;
        out.members=progress_;out.quantities=quantities_;
        for(unsigned i=0;i<p::actors;++i){out.quantity_history_known[i]=quantities_[i].history_known;out.complete_observed&=quantities_[i].history_known;out.useful_support_observed|=progress_[i].useful_support;}
        return out;
    }
};
}
