#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

// A finite, named, three-body SERVICE interpreter, not a native authority or
// pooled-health model. A caller preserves this ledger and authenticates actual
// peer commitments/effect receipts; the eight-byte ranking intent is not that
// transport. A word compiler supplies fresh individual exposure/legality guards.
// No priority-setting operator exists in this initial class.
// NeedObservation/NeedModel and an unguarded proposed intent grant NO hold,
// yield or other game action. The adapter must compile a priced word/fallback
// in that input; repeated unsupported inputs close this contract boundedly.
namespace wb::capital::party {
inline constexpr unsigned actors=3,max_camps=16,max_slots=24,max_effects=8;
struct Point {int x{},y{};};
inline constexpr bool operator==(Point a,Point b){return a.x==b.x&&a.y==b.y;}
inline int distance(Point a,Point b){return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
enum class Role {Varg,Willow,Vendetta};
enum class Channel {None,Submit,Recall,Other};
enum class Kind {
    None,Commit,Assemble,Enter,WorkTank,SupportHeal,Strike,CaptureHold,
    Release,ApproachAltar,Submit,HoldChannel,Recover,HoldRecovery,Renew,EmergencyExit
};
enum class Phase {Commit,Assemble,Enter,Work,Capture,Release,Convert,Recover,Renew,Complete,AbortCleanup,Closed};
enum class Failure {
    None,InvalidContract,Stale,WrongActor,NeedObservation,NeedModel,CommitmentBroken,
    Death,Deadline,NoProgress,Refusal,MissedWorkSlot,SupportFailed,InvalidEffect,
    CaptureMismatch,BankInterrupted,RecoveryInterrupted,CleanupIncomplete
};
enum class Status {Ready,NeedObservation,NeedModel,AlreadyIssued,Aborting,Closed,Complete,Invalid,AttestedComplete};
enum class Cast {None,Shield,Reset,Heal,Dash,Other};
struct Member {
    int id{-1};Role role{Role::Varg};
    Point rendezvous{},station{},capture{},release{},altar{},spawn{},renewal{};
    bool rendezvous_quiet{},release_quiet{},renewal_quiet{},requires_cast_at_renewal{};
};
struct Slot {
    int round{-1},actor{-1},camp{-1},recipient{-1};
    Kind kind{Kind::None};
    // A previous support slot must have an ACTUAL useful heal receipt.
    int required_support_slot{-1};
};
struct Contract {
    std::uint64_t id{};int group{-1},start_round{-1};
    std::array<Member,actors> members{};
    std::array<int,max_camps> camp_ids{};unsigned camp_count{};
    std::array<Slot,max_slots> slots{};unsigned slot_count{};
    unsigned capture_mask{},capture_ticks{},queried_respawn_ticks{},max_deferrals{},max_refusals{},abort_ticks{};
    std::uint64_t reward_gems{},reward_gold{};
    int join_by{-1},arrive_by{-1},work_by{-1},capture_by{-1},release_by{-1},convert_by{-1},recover_by{-1},renew_by{-1};
    bool geometry_supported{},individual_routes_supported{},actual_order_model_supported{};
    bool require_useful_heal{true};
};
struct Body {
    int id{-1};Role role{Role::Varg};Point position{};
    float hp{},max_hp{},shield{};
    std::uint64_t bag{},gold{},team_score{},capital_revision{};
    Channel channel{Channel::None};
    bool alive{},move{},attack{},cast{},stunned{},npc_quiet{},on_any_source{},at_spawn{};
    bool at_station{},at_capture{},altar_adjacent{};
    unsigned restrictions{},submit_ticks{},recall_ticks{},channel_remaining{};
    bool shield_ready{},reset_ready{},heal_ready{},dash_ready{};
    float shield_amount{},heal_amount{},heal_range{};
    bool operator==(const Body&) const=default;
};
struct Peer {
    Body body{};
    // This is this owner's CURRENT personal observation, not a prior report.
    bool observed_now{};
};
struct Camp {
    int id{-1};float hp{};bool observed_now{},alive{};
    // Runtime-queried geometry; no copied range or guessed source position.
    Point position{};int attack_range{-1};bool geometry_known{};
};
struct Group {
    int id{-1};std::array<Camp,max_camps> camps{};unsigned count{};
    bool observed_now{},whole_clear{},capture_own{},contested{},claimed{};
};
struct Payout {
    bool source_covered{};int group{-1};unsigned participants_mask{};
};
enum class OrderBasis {Unknown,ActualCurrentRound,CommittedNextRound};
struct AggroOrderEntry {
    int actor{-1},draft_ordinal{-1};std::int32_t priority{};
    bool priority_queried{},draft_accepted{},next_priority_committed{};
};
struct AggroOrder {
    OrderBasis basis{OrderBasis::Unknown};int observed_round{-1},phase_round{-1};
    std::array<AggroOrderEntry,actors> members{};
    // Includes EVERY priority writer, especially the existing bank pivot.
    // An accepted/read-back priority is not immutable while that actor or
    // another operator can still replace it before this phase.
    bool no_priority_writes_before_phase{};
};
struct AggroOwnership {
    int camp{-1},tank{-1}; // -1 means no party body is in this monster's range.
    // All other living champions, on either team, are proved out of range.
    bool other_champions_excluded{};
};
struct AggroPhase {
    // Initial supported work class: exactly the next round-start NPC phase.
    // This covers a whole legal M/A/C word, not a single host primitive.
    int round{-1};AggroOrder order{};
    std::array<Point,actors> observed_positions{},phase_positions{};
    // Accepted commitments/station closure and ALL refusal/truncation branches
    // must preserve these positions up to the phase. A planned arrival alone
    // is not a certificate. Missing personal peer vision remains unsupported.
    std::array<bool,actors> positions_covered{};
    // Each body survives EVERY preceding effect and ordered NPC attack through
    // this entire phase, using that body's actual capacity. This also prevents
    // an earlier party-body death from silently redirecting a later monster.
    // Outside-group/splash-woken sources belong to npc_covered as well.
    std::array<bool,actors> alive_through_phase_covered{};
    std::array<float,actors> hp_lower_after_phase{};
    std::array<AggroOwnership,max_camps> monsters{};unsigned count{};
};
struct Guard {
    int owner{-1},round{-1};std::uint64_t input{},capital_revision{},word_token{};
    Kind kind{Kind::None};Point destination{};int camp{-1},recipient{-1};
    float observed_hp{},observed_shield{},hp_lower{};
    int horizon_round{-1};unsigned priced_channel_ticks{};Cast selected_cast{Cast::None};
    bool fresh{},legal_word{},npc_covered{},foes_covered{},effects_covered{},refusals_covered{};
    bool individual_exposure_covered{},actual_order_covered{},station_closure_covered{};
    AggroPhase aggro{};bool future_support_borrowed{};
};
struct Frame {
    int owner{-1},round{-1};std::uint64_t input{};bool fresh{};
    Body self{};std::array<Peer,actors> peers{};
    std::array<bool,actors> commitment_known{},commitment_matches{};
    Group group{};Payout payout{};Guard guard{};
};
struct WordIntent {
    std::uint64_t contract{},serial{},input{},word_token{};int owner{-1},round{-1};
    Kind kind{Kind::None};Point destination{};int camp{-1},recipient{-1},slot{-1};
    // Intent permits an actual legal M/A/C word, not one arbitrary primitive.
    // WorkTank may use shield+basic OR basic-reset-basic; never both casts.
    Cast selected_cast{Cast::None};
    bool guarded{};
};
struct Decision {Status status{Status::Invalid};Failure reason{Failure::None};WordIntent intent{};};
enum class EffectKind {CommitPublished,Move,Dash,BasicDamage,ShieldCast,ResetCast,HealCast,SubmitStarted,RecallStarted};
struct Effect {
    EffectKind kind{EffectKind::Move};int target{-1};Point from{},to{};
    float before{},after{};
    unsigned timer{};
    bool fresh{},accepted{};
};
struct Tape {std::array<Effect,max_effects> effects{};std::uint64_t word_token{};unsigned count{};bool complete{},refused{};};
struct MemberResult {
    int actor{-1};std::uint64_t captured_gems{},captured_gold{},banked_party_gems{};
    float camp_work{},useful_heal{},actual_shield{};
    bool committed{},arrived{},entered{},receipt{},released{},banked{},recovered{},renewed{};
};
struct Completion {
    bool complete_observed{},useful_support_observed{},cleanup_complete{};
    Failure failure{Failure::None};
    std::array<MemberResult,actors> members{};
    std::array<float,max_camps> residual_hp{};std::array<bool,max_camps> residual_known{};
    // Remote predicate reports never fill this quantitative-history mask.
    std::array<bool,actors> quantity_history_known{};
};
struct InterruptedWord {bool matched{},prefix_confirmed{};};
enum class Milestone {Unpublished,Committed,Arrived,Entered,WorkPrefix,Payout,Released,Banked,Recovered,Renewed,Abort,CleanupDone,ClosedClean,ClosedFailed};
struct ProducerReceipt {
    bool valid{};std::uint64_t contract{},source_input{};int actor{-1},source_round{-1};
    Role role{Role::Varg};Milestone milestone{Milestone::Unpublished};bool predicate{};
};
struct TrustedPredicateReceipt {
    std::uint64_t contract{};int actor{-1},source_round{-1};Role role{Role::Varg};
    Milestone milestone{Milestone::Unpublished};bool predicate{};
    // These authenticate the actual receiver's fenced whole-wire read. The
    // public sender key is (actor, FULL round), never a fabricated private input
    // or word token. Canonical equality is separately checked in ingest().
    int read_owner{-1},read_round{-1},header_start{-1},header_group{-1};std::uint64_t read_input{};
    bool active_armed{},whole_read{},trusted_partition_and_producer{};
};
enum class IngestStatus {Accepted,Duplicate,Stale,Unverified,Invalid,Conflict,PendingLocalWord,Cancelled,Closed};
struct MemberMilestones {
    bool committed{},arrived{},entered{},receipt{},released{},banked{},recovered{},renewed{};
    bool positive_work{},useful_support{};
    int source_round{-1};Milestone latest{Milestone::Unpublished};
};
struct AttestedProgress {
    std::array<MemberMilestones,actors> members{};std::array<bool,max_slots> slots{};
    bool attested_complete{},cancelled{};
};
namespace detail {
inline bool finite(float x){return std::isfinite(x)&&x>=0;}
inline bool point(Point p){return p.x>=0&&p.y>=0&&p.x<100000&&p.y<100000;}
inline bool body(const Body& b){return b.id>=0&&static_cast<unsigned>(b.role)<=2&&finite(b.hp)&&finite(b.max_hp)
    &&b.max_hp>0&&b.hp<=b.max_hp&&finite(b.shield)&&(!b.alive||(b.hp>0&&point(b.position)))
    &&static_cast<unsigned>(b.channel)<=static_cast<unsigned>(Channel::Other);}
inline int member(const Contract& c,int id){for(unsigned i=0;i<actors;++i)if(c.members[i].id==id)return static_cast<int>(i);return -1;}
inline int camp(const Contract& c,int id){for(unsigned i=0;i<c.camp_count;++i)if(c.camp_ids[i]==id)return static_cast<int>(i);return -1;}
inline bool work(Kind k){return k==Kind::WorkTank||k==Kind::SupportHeal||k==Kind::Strike;}
inline bool cancellation(Milestone m){return m==Milestone::Abort||m==Milestone::CleanupDone||m==Milestone::ClosedFailed;}
inline bool same(const Contract& a,const Contract& b) {
    if(a.id!=b.id||a.group!=b.group||a.start_round!=b.start_round||a.camp_count!=b.camp_count||a.slot_count!=b.slot_count
        ||a.capture_mask!=b.capture_mask||a.capture_ticks!=b.capture_ticks||a.queried_respawn_ticks!=b.queried_respawn_ticks
        ||a.max_deferrals!=b.max_deferrals||a.max_refusals!=b.max_refusals||a.abort_ticks!=b.abort_ticks
        ||a.reward_gems!=b.reward_gems||a.reward_gold!=b.reward_gold||a.join_by!=b.join_by||a.arrive_by!=b.arrive_by
        ||a.work_by!=b.work_by||a.capture_by!=b.capture_by||a.release_by!=b.release_by||a.convert_by!=b.convert_by
        ||a.recover_by!=b.recover_by||a.renew_by!=b.renew_by||a.geometry_supported!=b.geometry_supported
        ||a.individual_routes_supported!=b.individual_routes_supported||a.actual_order_model_supported!=b.actual_order_model_supported
        ||a.require_useful_heal!=b.require_useful_heal||a.camp_count>max_camps||a.slot_count>max_slots)return false;
    for(unsigned i=0;i<actors;++i){const auto& x=a.members[i];const auto& y=b.members[i];
        if(x.id!=y.id||x.role!=y.role||!(x.rendezvous==y.rendezvous)||!(x.station==y.station)||!(x.capture==y.capture)
            ||!(x.release==y.release)||!(x.altar==y.altar)||!(x.spawn==y.spawn)||!(x.renewal==y.renewal)
            ||x.rendezvous_quiet!=y.rendezvous_quiet||x.release_quiet!=y.release_quiet||x.renewal_quiet!=y.renewal_quiet
            ||x.requires_cast_at_renewal!=y.requires_cast_at_renewal)return false;}
    for(unsigned i=0;i<a.camp_count;++i)if(a.camp_ids[i]!=b.camp_ids[i])return false;
    for(unsigned i=0;i<a.slot_count;++i){const auto& x=a.slots[i];const auto& y=b.slots[i];
        if(x.round!=y.round||x.actor!=y.actor||x.camp!=y.camp||x.recipient!=y.recipient||x.kind!=y.kind
            ||x.required_support_slot!=y.required_support_slot)return false;}
    return true;
}
inline bool aggro(const Contract& c,const Frame& f,const Guard& g) {
    const auto& a=g.aggro;const auto& o=a.order;
    // Camps act BEFORE champions in R+1. Current-round rank cannot establish
    // that phase's tie winner. This class deliberately rejects longer work
    // horizons instead of reusing one phase's geometry/order indefinitely.
    if(g.horizon_round!=f.round+1||a.round!=f.round+1||a.count>max_camps
        ||o.basis!=OrderBasis::CommittedNextRound||o.observed_round!=f.round
        ||o.phase_round!=a.round||!o.no_priority_writes_before_phase)return false;
    for(unsigned i=0;i<actors;++i) {
        const auto& order=o.members[i];
        if(order.actor!=c.members[i].id||order.draft_ordinal<0||!order.priority_queried
            ||!order.draft_accepted||!order.next_priority_committed||!a.positions_covered[i]
            ||!a.alive_through_phase_covered[i]||!finite(a.hp_lower_after_phase[i])||a.hp_lower_after_phase[i]<=0
            ||!point(a.phase_positions[i]))return false;
        const Body* observed=nullptr;
        if(c.members[i].id==f.owner)observed=&f.self;
        else if(f.peers[i].observed_now)observed=&f.peers[i].body;
        if(!observed||observed->id!=c.members[i].id||observed->role!=c.members[i].role
            ||!body(*observed)||!observed->alive||!(a.observed_positions[i]==observed->position))return false;
        for(unsigned j=0;j<i;++j)if(order.draft_ordinal==o.members[j].draft_ordinal
            ||((order.draft_ordinal<o.members[j].draft_ordinal)!=(order.actor<o.members[j].actor))
            ||a.phase_positions[i]==a.phase_positions[j])return false;
    }
    if(!f.group.observed_now||f.group.id!=c.group||f.group.count!=c.camp_count||f.group.count>max_camps)return false;
    unsigned live=0;
    for(unsigned i=0;i<f.group.count;++i) {
        const auto& source=f.group.camps[i];
        if(camp(c,source.id)<0||!source.observed_now||!finite(source.hp)
            ||(source.alive?source.hp<=0:source.hp!=0))return false;
        for(unsigned j=0;j<i;++j)if(source.id==f.group.camps[j].id)return false;
        if(!source.alive)continue;
        ++live;
        if(!source.geometry_known||!point(source.position)||source.attack_range<0||source.attack_range>=100000)return false;
        const AggroOwnership* proof=nullptr;
        for(unsigned j=0;j<a.count;++j)if(a.monsters[j].camp==source.id) {
            if(proof)return false;proof=&a.monsters[j];
        }
        if(!proof||!proof->other_champions_excluded)return false;
        int tank=-1,nearest=std::numeric_limits<int>::max();
        for(unsigned j=0;j<actors;++j) {
            const int d=distance(source.position,a.phase_positions[j]);
            if(d>source.attack_range)continue;
            bool earlier=false;
            if(tank>=0) {
                const auto& prior=o.members[static_cast<unsigned>(tank)];const auto& next=o.members[j];
                earlier=next.priority>prior.priority
                    ||(next.priority==prior.priority&&next.draft_ordinal<prior.draft_ordinal);
            }
            if(d<nearest||(d==nearest&&earlier)){tank=static_cast<int>(j);nearest=d;}
        }
        if(proof->tank!=(tank<0?-1:c.members[static_cast<unsigned>(tank)].id))return false;
    }
    // Exactly one proof for every live source: no omitted or invented monster.
    return a.count==live;
}
inline bool valid(const Contract& c) {
    if(!c.id||c.group<0||c.start_round<0||c.renew_by>=1000000000||!c.geometry_supported
        ||!c.individual_routes_supported||!c.actual_order_model_supported||!c.camp_count||c.camp_count>max_camps
        ||!c.slot_count||c.slot_count>max_slots||!c.capture_mask||(c.capture_mask&~7u)||!c.capture_ticks||c.capture_ticks>10000
        ||!c.queried_respawn_ticks||c.queried_respawn_ticks>10000
        ||!c.max_deferrals||!c.max_refusals||!c.abort_ticks||c.abort_ticks>10000)return false;
    const std::array dates{c.start_round,c.join_by,c.arrive_by,c.work_by,c.capture_by,c.release_by,c.convert_by,c.recover_by,c.renew_by};
    for(unsigned i=1;i<dates.size();++i)if(dates[i]<dates[i-1])return false;
    if(c.capture_by-c.work_by<static_cast<int>(c.capture_ticks))return false;
    for(unsigned i=0;i<actors;++i) {
        const auto& a=c.members[i];if(a.id<0||static_cast<unsigned>(a.role)!=i||!a.rendezvous_quiet
            ||!a.release_quiet||!a.renewal_quiet||!point(a.rendezvous)||!point(a.station)||!point(a.capture)
            ||!point(a.release)||!point(a.altar)||!point(a.spawn)||!point(a.renewal))return false;
        for(unsigned j=0;j<i;++j)if(a.id==c.members[j].id||a.rendezvous==c.members[j].rendezvous)return false;
    }
    for(unsigned i=0;i<c.camp_count;++i){if(c.camp_ids[i]<0)return false;for(unsigned j=0;j<i;++j)if(c.camp_ids[i]==c.camp_ids[j])return false;}
    bool heal=false,tank=false,strike=false;
    for(unsigned i=0;i<c.slot_count;++i) {
        const auto& s=c.slots[i];const int at=member(c,s.actor);
        if(at<0||s.round<c.arrive_by||s.round>c.work_by||!work(s.kind))return false;
        if(s.kind==Kind::WorkTank){if(at!=0||camp(c,s.camp)<0)return false;tank=true;}
        if(s.kind==Kind::Strike){if(at!=2||camp(c,s.camp)<0)return false;strike=true;}
        if(s.kind==Kind::SupportHeal){if(at!=1||member(c,s.recipient)<0||s.recipient==s.actor)return false;heal=true;}
        if(s.required_support_slot>=0) {
            if(s.required_support_slot>=static_cast<int>(i))return false;
            const auto& previous=c.slots[s.required_support_slot];
            if(previous.kind!=Kind::SupportHeal||previous.recipient!=s.actor||previous.round>s.round)return false;
        }
        for(unsigned j=0;j<i;++j)if(s.actor==c.slots[j].actor&&s.round==c.slots[j].round)return false;
    }
    return tank&&strike&&(!c.require_useful_heal||heal);
}
inline std::uint64_t share(const Contract& c,unsigned actor,std::uint64_t reward) {
    unsigned count=0,lowest=actors;
    for(unsigned i=0;i<actors;++i)if(c.capture_mask&(1u<<i)){++count;if(lowest==actors||c.members[i].id<c.members[lowest].id)lowest=i;}
    if(!(c.capture_mask&(1u<<actor)))return 0;
    return reward/count+(actor==lowest?reward%count:0);
}
}

class Controller {
    Contract c_{};Phase phase_{Phase::Closed};Failure failure_{Failure::InvalidContract};
    Completion actual_{};AttestedProgress progress_{};int owner_{-1};
    int processed_round_{-1};std::uint64_t processed_input_{};Body processed_self_{};
    std::array<int,actors> last_round_{-1,-1,-1},issued_round_{-1,-1,-1},last_defer_{-1,-1,-1};
    std::array<unsigned,actors> deferred_{},refused_{};
    std::array<bool,actors> baseline_{},bank_active_{},recovery_active_{};
    std::array<bool,actors> cleanup_recovered_{};
    std::array<std::uint64_t,actors> capture_bag_{},capture_gold_{},bank_bag_{},bank_score_{};
    std::array<int,actors> bank_round_{},recovery_round_{};
    std::array<unsigned,actors> bank_ticks_{},recovery_ticks_{};
    std::array<Point,actors> bank_pose_{};
    std::array<bool,max_slots> slot_done_{};
    std::uint64_t serial_{};WordIntent pending_{};Frame before_{};
    bool pending_valid_{},waiting_work_proof_{};int abort_by_{-1};
    bool all(bool MemberMilestones::*field) const {for(const auto& a:progress_.members)if(!(a.*field))return false;return true;}
    void sync_local() {
        for(unsigned i=0;i<actors;++i) {
            if(owner_>=0&&c_.members[i].id!=owner_)continue;
            const auto& a=actual_.members[i];auto& p=progress_.members[i];
            p.committed|=a.committed;p.arrived|=a.arrived;p.entered|=a.entered;p.receipt|=a.receipt;
            p.released|=a.released;p.banked|=a.banked;p.recovered|=a.recovered;p.renewed|=a.renewed;
            p.positive_work|=a.camp_work>0;p.useful_support|=a.useful_heal>0;
            actual_.quantity_history_known[i]=a.renewed;
        }
        for(unsigned i=0;i<c_.slot_count;++i)progress_.slots[i]|=slot_done_[i];
    }
    bool mandatory_done() const {for(unsigned i=0;i<c_.slot_count;++i)if(!progress_.slots[i])return false;return true;}
    bool own_mandatory_done(int actor) const {for(unsigned i=0;i<c_.slot_count;++i)if(c_.slots[i].actor==actor&&!slot_done_[i])return false;return true;}
    void credit_slot(unsigned i) {
        progress_.slots[i]=true;const auto& s=c_.slots[i];const int at=detail::member(c_,s.actor);
        if(s.kind==Kind::SupportHeal)progress_.members[static_cast<unsigned>(at)].useful_support=true;
        else progress_.members[static_cast<unsigned>(at)].positive_work=true;
    }
    Status complete_status() const{return actual_.complete_observed?Status::Complete:Status::AttestedComplete;}
    void request_cancel(int round) {
        progress_.cancelled=true;progress_.attested_complete=false;
        if(phase_==Phase::Complete){phase_=Phase::Closed;failure_=actual_.failure=Failure::CommitmentBroken;return;}
        // Metadata cannot cancel an issued SDK call or erase its accepted
        // prefix. Keep the private pending word until confirm() reconciles it.
        if(!pending_valid_)abort(Failure::CommitmentBroken,round);
    }
    int deadline() const {
        switch(phase_) {
        case Phase::Commit:case Phase::Assemble:return c_.join_by;
        case Phase::Enter:return c_.arrive_by;
        case Phase::Work:return c_.work_by;
        case Phase::Capture:return c_.capture_by;
        case Phase::Release:return c_.release_by;
        case Phase::Convert:return c_.convert_by;
        case Phase::Recover:return c_.recover_by;
        case Phase::Renew:return c_.renew_by;
        default:return std::numeric_limits<int>::max();
        }
    }
    bool locally_done(unsigned at) const {
        const auto& a=actual_.members[at];
        switch(phase_) {
        case Phase::Commit:return a.committed;case Phase::Assemble:return a.arrived;case Phase::Enter:return a.entered;
        case Phase::Work:return own_mandatory_done(c_.members[at].id);case Phase::Capture:return a.receipt;
        case Phase::Release:return a.released;case Phase::Convert:return a.banked;case Phase::Recover:return a.recovered;
        case Phase::Renew:return a.renewed;default:return false;
        }
    }
    void abort(Failure why,int round) {
        if(phase_==Phase::Complete||phase_==Phase::Closed)return;
        if(phase_!=Phase::AbortCleanup){failure_=why;actual_.failure=why;abort_by_=round+static_cast<int>(c_.abort_ticks);cleanup_recovered_={};}
        phase_=Phase::AbortCleanup;pending_valid_=false;progress_.cancelled=true;progress_.attested_complete=false;
    }
    void residual(const Frame& f) {
        if(phase_==Phase::Release||phase_==Phase::Convert||phase_==Phase::Recover||phase_==Phase::Renew||phase_==Phase::Complete)return;
        if(!f.group.observed_now||f.group.id!=c_.group||f.group.count>max_camps)return;
        for(unsigned i=0;i<f.group.count;++i) {
            const auto& source=f.group.camps[i];const int at=detail::camp(c_,source.id);
            if(at>=0&&source.observed_now&&detail::finite(source.hp)&&(!source.alive||source.hp>0)) {
                actual_.residual_hp[at]=source.alive?source.hp:0;actual_.residual_known[at]=true;
            }
        }
    }
    Decision unavailable(const Frame& f,unsigned at,Failure why) {
        if(last_defer_[at]!=f.round){last_defer_[at]=f.round;++deferred_[at];}
        if(deferred_[at]>=c_.max_deferrals)abort(why,f.round);
        return {phase_==Phase::AbortCleanup?Status::Aborting:
            why==Failure::NeedObservation?Status::NeedObservation:Status::NeedModel,why,{}};
    }
    WordIntent nominate(const Frame& f,unsigned at) const {
        WordIntent w;w.contract=c_.id;w.owner=f.owner;w.round=f.round;w.input=f.input;
        const auto& a=c_.members[at];w.destination=a.station;
        switch(phase_) {
        case Phase::Commit:w.kind=actual_.members[at].committed?Kind::None:Kind::Commit;w.destination=a.rendezvous;break;
        case Phase::Assemble:w.kind=Kind::Assemble;w.destination=a.rendezvous;break;
        case Phase::Enter:w.kind=Kind::Enter;break;
        case Phase::Work:
            for(unsigned i=0;i<c_.slot_count;++i)if(!slot_done_[i]&&c_.slots[i].actor==f.owner&&c_.slots[i].round==f.round) {
                w.kind=c_.slots[i].kind;w.camp=c_.slots[i].camp;w.recipient=c_.slots[i].recipient;w.slot=static_cast<int>(i);return w;
            }
            w.kind=Kind::Enter;break; // guarded station hold/return, never free tank HP
        case Phase::Capture:w.kind=Kind::CaptureHold;w.destination=a.capture;break;
        case Phase::Release:w.kind=Kind::Release;w.destination=a.release;break;
        case Phase::Convert:
            if(bank_active_[at])w.kind=Kind::HoldChannel;
            else if(actual_.members[at].banked)w.kind=Kind::None;
            else {w.kind=f.self.altar_adjacent?Kind::Submit:Kind::ApproachAltar;w.destination=a.altar;}
            break;
        case Phase::Recover:
            w.kind=f.self.channel==Channel::Recall?Kind::HoldChannel:
                f.self.at_spawn?Kind::HoldRecovery:Kind::Recover;w.destination=a.spawn;break;
        case Phase::Renew:w.kind=Kind::Renew;w.destination=a.renewal;break;
        case Phase::AbortCleanup:
            w.kind=f.self.at_spawn?Kind::HoldRecovery:f.self.channel==Channel::Recall?Kind::HoldChannel:
                f.self.on_any_source||!f.self.npc_quiet?Kind::EmergencyExit:Kind::Recover;
            w.destination=w.kind==Kind::EmergencyExit?a.release:a.spawn;break;
        default:break;
        }
        return w;
    }
    bool guard(const Frame& f,const WordIntent& w) const {
        // Publishing metadata is cheap, but its owner input is a real physical
        // wait/word. It needs the same individual horizon guard as other words.
        if(w.kind==Kind::Commit&&!f.self.npc_quiet)return false;
        if(f.self.stunned&&w.kind!=Kind::HoldChannel&&w.kind!=Kind::HoldRecovery)return false;
        const auto& g=f.guard;
        if(!g.fresh||!g.word_token||static_cast<unsigned>(g.selected_cast)>static_cast<unsigned>(Cast::Other)
            ||g.owner!=f.owner||g.round!=f.round||g.input!=f.input||g.kind!=w.kind
            ||!(g.destination==w.destination)||g.camp!=w.camp||g.recipient!=w.recipient
            ||g.capital_revision!=f.self.capital_revision||g.observed_hp!=f.self.hp||g.observed_shield!=f.self.shield
            ||!g.legal_word||!g.npc_covered||!g.foes_covered||!g.effects_covered||!g.refusals_covered
            ||!g.individual_exposure_covered||!g.actual_order_covered||!g.station_closure_covered
            ||g.future_support_borrowed||!detail::finite(g.hp_lower)||g.hp_lower<=0||g.horizon_round<f.round+1)return false;
        if(detail::work(w.kind)&&!detail::aggro(c_,f,g))return false;
        if(w.kind==Kind::Submit&&(!f.self.submit_ticks||f.self.submit_ticks>10000||g.priced_channel_ticks!=f.self.submit_ticks
            ||g.horizon_round<f.round+static_cast<int>(f.self.submit_ticks)))return false;
        if(w.kind==Kind::Recover&&g.priced_channel_ticks&&(!f.self.recall_ticks||f.self.recall_ticks>10000||g.priced_channel_ticks!=f.self.recall_ticks
            ||g.horizon_round<f.round+static_cast<int>(f.self.recall_ticks)))return false;
        if(detail::work(w.kind)&&w.kind!=Kind::SupportHeal) {
            bool actual=false;
            for(unsigned i=0;i<f.group.count&&i<max_camps;++i)if(f.group.camps[i].id==w.camp)
                actual=f.group.camps[i].observed_now&&f.group.camps[i].alive&&detail::finite(f.group.camps[i].hp)&&f.group.camps[i].hp>0;
            if(!actual)return false;
        }
        if(w.kind==Kind::SupportHeal) {
            if(g.selected_cast!=Cast::Heal||!f.self.cast||!f.self.heal_ready||!detail::finite(f.self.heal_amount)
                ||f.self.heal_amount<=0||!detail::finite(f.self.heal_range))return false;
            const int target=detail::member(c_,w.recipient);
            if(target<0||!f.peers[target].observed_now||f.peers[target].body.id!=w.recipient
                ||f.peers[target].body.role!=c_.members[target].role||!detail::body(f.peers[target].body)||!f.peers[target].body.alive
                ||distance(f.self.position,f.peers[target].body.position)>f.self.heal_range
                ||f.peers[target].body.hp>=f.peers[target].body.max_hp)return false;
        }
        if(g.selected_cast!=Cast::None) {
            if(!f.self.cast||f.self.stunned||f.self.restrictions)return false;
            if(g.selected_cast==Cast::Shield&&(!f.self.shield_ready||f.self.role!=Role::Varg))return false;
            if(g.selected_cast==Cast::Reset&&!f.self.reset_ready)return false;
            if(g.selected_cast==Cast::Dash&&!f.self.dash_ready)return false;
        }
        return true;
    }
    void progress(const Frame& f,unsigned at) {
        auto& a=actual_.members[at];const auto& m=c_.members[at];
        if(phase_==Phase::Assemble)a.arrived=f.self.position==m.rendezvous&&f.self.npc_quiet;
        if(phase_==Phase::Enter)a.entered=f.self.at_station&&f.self.position==m.station;
        if(phase_==Phase::Capture) {
            if(!baseline_[at]){capture_bag_[at]=f.self.bag;capture_gold_[at]=f.self.gold;baseline_[at]=true;
                if(f.payout.source_covered)abort(Failure::CaptureMismatch,f.round);}
            if(f.payout.source_covered&&!a.receipt&&phase_==Phase::Capture) {
                const auto gems=detail::share(c_,at,c_.reward_gems),gold=detail::share(c_,at,c_.reward_gold);
                const bool position=(c_.capture_mask&(1u<<at))?f.self.at_capture&&f.self.position==m.capture:!f.self.on_any_source;
                if(!position||f.payout.group!=c_.group||f.payout.participants_mask!=c_.capture_mask||!f.group.observed_now||!f.group.claimed
                    ||f.self.bag<capture_bag_[at]||f.self.bag-capture_bag_[at]<gems
                    ||f.self.gold<capture_gold_[at]||f.self.gold-capture_gold_[at]<gold)abort(Failure::CaptureMismatch,f.round);
                else {a.captured_gems=gems;a.captured_gold=gold;a.receipt=true;}
            }
        }
        if(phase_==Phase::Release)a.released=!f.self.on_any_source;
        if(phase_==Phase::Convert&&!a.banked) {
            if(bank_active_[at]) {
                if(f.self.stunned||!(f.self.position==bank_pose_[at]))abort(Failure::BankInterrupted,f.round);
                else if(f.self.channel==Channel::Submit) {
                    if(f.round>=bank_round_[at]+static_cast<int>(bank_ticks_[at]))abort(Failure::BankInterrupted,f.round);
                } else if(f.round==bank_round_[at]+static_cast<int>(bank_ticks_[at])&&f.self.channel==Channel::None
                    &&f.self.bag==0&&f.self.team_score>=bank_score_[at]
                    &&f.self.team_score-bank_score_[at]>=bank_bag_[at]) {
                    a.banked=true;a.banked_party_gems=a.captured_gems;bank_active_[at]=false;
                } else if(f.round>bank_round_[at])abort(Failure::BankInterrupted,f.round);
            } else if(f.self.bag==0) {
                if(a.captured_gems)abort(Failure::BankInterrupted,f.round);else a.banked=true;
            }
        }
        if(phase_==Phase::Recover||phase_==Phase::AbortCleanup) {
            if(f.self.at_spawn&&f.self.channel==Channel::None) {
                recovery_active_[at]=false;
                if(f.self.hp==f.self.max_hp) {
                    a.recovered=true;
                    if(phase_==Phase::AbortCleanup)cleanup_recovered_[at]=true;
                }
            } else if(recovery_active_[at]) {
                if((f.self.channel!=Channel::Recall&&f.round>recovery_round_[at])
                    ||f.round>=recovery_round_[at]+static_cast<int>(recovery_ticks_[at]))abort(Failure::RecoveryInterrupted,f.round);
            }
        }
        if(phase_==Phase::Renew&&f.self.position==m.renewal&&f.self.npc_quiet&&f.self.channel==Channel::None
            &&!f.self.stunned&&!f.self.restrictions&&f.self.move&&f.self.attack&&(!m.requires_cast_at_renewal||f.self.cast))a.renewed=true;
    }
    void advance(const Frame& f) {
        sync_local();waiting_work_proof_=false;
        if(phase_==Phase::Commit&&all(&MemberMilestones::committed))phase_=Phase::Assemble;
        if(phase_==Phase::Assemble&&all(&MemberMilestones::arrived)) {
            bool actual=true;for(unsigned i=0;i<actors;++i)actual&=f.peers[i].observed_now
                &&f.peers[i].body.id==c_.members[i].id&&detail::body(f.peers[i].body)&&f.peers[i].body.alive
                &&(owner_>=0||(f.peers[i].body.position==c_.members[i].rendezvous&&f.peers[i].body.npc_quiet));
            // Independent actors launch on the canonical public join date.
            // Earlier actors may then be ON ROUTE when a later actor observes
            // them. Do not forge a rendezvous pose: retain actual personal peer
            // observations and price every route/exposure in the word guard.
            if(owner_>=0)actual&=f.round>=c_.join_by&&f.self.npc_quiet
                &&f.self.position==c_.members[static_cast<unsigned>(detail::member(c_,f.owner))].rendezvous;
            if(actual)phase_=Phase::Enter;
        }
        if(phase_==Phase::Enter&&all(&MemberMilestones::entered)) {
            bool actual=true;for(unsigned i=0;i<actors;++i)actual&=f.peers[i].observed_now
                &&f.peers[i].body.id==c_.members[i].id&&detail::body(f.peers[i].body)&&f.peers[i].body.alive
                &&f.peers[i].body.at_station&&f.peers[i].body.position==c_.members[i].station;
            if(actual)phase_=Phase::Work;
        }
        if((phase_==Phase::Work||phase_==Phase::Enter)&&f.group.observed_now&&f.group.id==c_.group&&f.group.whole_clear) {
            // No retirement may masquerade as an executed slot. This supported
            // class has ALL slots mandatory, including future dated slots.
            if(!mandatory_done()) {
                if(owner_<0||!own_mandatory_done(owner_))abort(Failure::MissedWorkSlot,f.round);
                else waiting_work_proof_=true;
                return;
            }
            if(c_.require_useful_heal&&!progress_.members[1].useful_support){abort(Failure::SupportFailed,f.round);return;}
            if(!progress_.members[0].positive_work||!progress_.members[2].positive_work){abort(Failure::InvalidEffect,f.round);return;}
            phase_=Phase::Capture;
        }
        if(phase_==Phase::Capture&&all(&MemberMilestones::receipt))phase_=Phase::Release;
        if(phase_==Phase::Release&&all(&MemberMilestones::released)) {
            bool actual=true;for(unsigned i=0;i<actors;++i)actual&=f.peers[i].observed_now
                &&f.peers[i].body.id==c_.members[i].id&&f.peers[i].body.alive&&!f.peers[i].body.on_any_source;
            if(actual)phase_=Phase::Convert;
        }
        if(phase_==Phase::Convert&&all(&MemberMilestones::banked))phase_=Phase::Recover;
        if(phase_==Phase::Recover&&all(&MemberMilestones::recovered))phase_=Phase::Renew;
        if(phase_==Phase::Renew&&all(&MemberMilestones::renewed)) {
            bool own_current=owner_<0;
            if(owner_>=0) {const auto& m=c_.members[static_cast<unsigned>(detail::member(c_,owner_))];
                own_current=f.self.position==m.renewal&&f.self.npc_quiet&&f.self.channel==Channel::None
                    &&!f.self.stunned&&!f.self.restrictions&&f.self.move&&f.self.attack&&(!m.requires_cast_at_renewal||f.self.cast);}
            if(own_current) {
                phase_=Phase::Complete;progress_.attested_complete=true;
                actual_.complete_observed=true;for(bool known:actual_.quantity_history_known)actual_.complete_observed&=known;
                actual_.cleanup_complete=actual_.complete_observed;actual_.failure=Failure::None;
            }
        }
        if(phase_==Phase::AbortCleanup) {
            bool actual=true;for(unsigned i=0;i<actors;++i)actual&=cleanup_recovered_[i]&&f.peers[i].observed_now
                &&f.peers[i].body.id==c_.members[i].id&&f.peers[i].body.alive&&f.peers[i].body.at_spawn
                &&f.peers[i].body.channel==Channel::None&&f.peers[i].body.hp==f.peers[i].body.max_hp;
            if(actual){phase_=Phase::Closed;actual_.cleanup_complete=true;}
        }
    }
public:
    // -1 is the original single interleaved/local-audit ledger. A native
    // independent controller MUST bind its actual actor identity here.
    explicit Controller(const Contract& contract,int owner=-1):c_(contract),owner_(owner) {
        if(detail::valid(c_)&&(owner_==-1||(detail::member(c_,owner_)>=0&&c_.start_round>0
            &&c_.group<=31&&c_.start_round<=2047&&c_.renew_by<=2047)))
            {phase_=Phase::Commit;failure_=Failure::None;actual_.failure=Failure::None;}
        else actual_.failure=Failure::InvalidContract;
        for(unsigned i=0;i<actors;++i)actual_.members[i].actor=c_.members[i].id;
    }
    Phase phase() const{return phase_;}
    const Completion& result() const{return actual_;}
    const Contract& contract() const{return c_;}
    const AttestedProgress& progress() const{return progress_;}
    ProducerReceipt publication(const Frame& f) const {
        ProducerReceipt out;
        const int local=detail::member(c_,owner_);
        if(owner_<0||f.owner!=owner_||!f.fresh||!f.input||f.round<c_.start_round||f.round>2047
            ||local<0||f.self.id!=owner_||!detail::body(f.self)||f.self.role!=c_.members[static_cast<unsigned>(local)].role)return out;
        const auto at=static_cast<unsigned>(local);const auto& a=actual_.members[at];
        out.contract=c_.id;out.actor=owner_;out.role=c_.members[at].role;out.source_round=f.round;out.source_input=f.input;
        if(progress_.cancelled||phase_==Phase::AbortCleanup||phase_==Phase::Closed) {
            out.valid=true;out.milestone=phase_==Phase::Closed?Milestone::ClosedFailed:Milestone::Abort;return out;
        }
        if(f.round!=processed_round_||f.input!=processed_input_||!(f.self==processed_self_)||!f.self.alive)return {};
        if(pending_valid_) {
            // Commitment publication is the declaration itself. The local
            // CommitPublished effect still needs actual write/readback later.
            if(pending_.kind!=Kind::Commit||pending_.owner!=f.owner||pending_.round!=f.round||pending_.input!=f.input
                ||!(f.self==before_.self))return {};
            out.valid=out.predicate=true;out.milestone=Milestone::Committed;return out;
        }
        Milestone m=Milestone::Unpublished;
        if(a.committed)m=Milestone::Committed;
        if(a.arrived)m=Milestone::Arrived;
        if(a.entered)m=Milestone::Entered;
        bool prefix=a.entered;
        for(unsigned i=0;i<c_.slot_count;++i)if(c_.slots[i].actor==owner_&&c_.slots[i].round<=f.round&&!slot_done_[i])prefix=false;
        if(prefix)m=Milestone::WorkPrefix;
        // Later normal phases are cumulative ALL-mandatory-slot attestations.
        if(a.receipt&&mandatory_done())m=Milestone::Payout;
        if(a.released&&a.receipt&&mandatory_done())m=Milestone::Released;
        if(a.banked&&a.released&&a.receipt&&mandatory_done())m=Milestone::Banked;
        if(a.recovered&&a.banked&&a.released&&a.receipt&&mandatory_done())m=Milestone::Recovered;
        if(a.renewed&&a.recovered&&a.banked&&a.released&&a.receipt&&mandatory_done())m=Milestone::Renewed;
        if(phase_==Phase::Complete&&progress_.attested_complete)m=Milestone::ClosedClean;
        out.valid=m!=Milestone::Unpublished;out.predicate=out.valid;out.milestone=m;return out;
    }
    IngestStatus ingest(const Contract& canonical,const TrustedPredicateReceipt& r,const Frame& f) {
        const int local=detail::member(c_,owner_);
        if(owner_<0||f.owner!=owner_||!f.fresh||!f.input||f.self.id!=owner_||!detail::body(f.self)
            ||local<0||f.round<c_.start_round||f.round<last_round_[static_cast<unsigned>(local)]
            ||f.round>2047||f.self.role!=c_.members[static_cast<unsigned>(local)].role
            ||r.read_owner!=f.owner||r.read_round!=f.round||r.read_input!=f.input)return IngestStatus::Invalid;
        if(!r.active_armed||!r.whole_read||!r.trusted_partition_and_producer)return IngestStatus::Unverified;
        if(r.contract!=c_.id||r.header_start!=c_.start_round||r.header_group!=c_.group)return IngestStatus::Invalid;
        if(!detail::same(c_,canonical)){request_cancel(f.round);return IngestStatus::Conflict;}
        const auto role=static_cast<unsigned>(r.role);
        if(role>=actors||r.actor!=c_.members[role].id||r.actor==owner_
            ||static_cast<unsigned>(r.milestone)>static_cast<unsigned>(Milestone::ClosedFailed))return IngestStatus::Invalid;
        if(r.source_round<c_.start_round||r.source_round>f.round||r.source_round>2047)return IngestStatus::Stale;
        // Withdrawal wins even over a later positive high-water mark and needs
        // no positive predicate. It grants NO cleanup or quantity credit.
        if(detail::cancellation(r.milestone)){request_cancel(f.round);return IngestStatus::Cancelled;}
        if(progress_.cancelled||phase_==Phase::Closed)return IngestStatus::Closed;
        if(!r.predicate||r.milestone==Milestone::Unpublished)return IngestStatus::Unverified;
        auto& p=progress_.members[role];
        if(r.source_round<p.source_round)return IngestStatus::Stale;
        if(r.source_round==p.source_round&&r.milestone==p.latest)return IngestStatus::Duplicate;
        if(static_cast<unsigned>(r.milestone)<static_cast<unsigned>(p.latest)) {request_cancel(f.round);return IngestStatus::Conflict;}
        int last_work=c_.start_round;
        for(unsigned i=0;i<c_.slot_count;++i)last_work=std::max(last_work,c_.slots[i].round);
        if(static_cast<unsigned>(r.milestone)>=static_cast<unsigned>(Milestone::Payout)
            &&r.source_round<last_work+static_cast<int>(c_.capture_ticks)+1) {request_cancel(f.round);return IngestStatus::Conflict;}
        if(pending_valid_)return IngestStatus::PendingLocalWord;
        p.source_round=r.source_round;p.latest=r.milestone;p.committed=true;
        const auto rank=static_cast<unsigned>(r.milestone);
        if(rank>=static_cast<unsigned>(Milestone::Arrived))p.arrived=true;
        if(rank>=static_cast<unsigned>(Milestone::Entered))p.entered=true;
        if(rank>=static_cast<unsigned>(Milestone::WorkPrefix))for(unsigned i=0;i<c_.slot_count;++i)
            if(c_.slots[i].actor==r.actor&&c_.slots[i].round<=r.source_round)credit_slot(i);
        if(rank>=static_cast<unsigned>(Milestone::Payout))p.receipt=true;
        if(rank>=static_cast<unsigned>(Milestone::Released))p.released=true;
        if(rank>=static_cast<unsigned>(Milestone::Banked))p.banked=true;
        if(rank>=static_cast<unsigned>(Milestone::Recovered))p.recovered=true;
        if(rank>=static_cast<unsigned>(Milestone::Renewed))p.renewed=true;
        return IngestStatus::Accepted;
    }
    Decision tick(const Frame& f) {
        if(phase_==Phase::Complete)return {complete_status(),Failure::None,{}};
        if(phase_==Phase::Closed)return {Status::Closed,failure_,{}};
        const int index=detail::member(c_,f.owner);
        if(index<0||(owner_>=0&&f.owner!=owner_)||f.self.id!=f.owner||f.self.role!=c_.members[index].role)return {Status::Invalid,Failure::WrongActor,{}};
        const auto at=static_cast<unsigned>(index);
        if(!f.fresh||!f.input||f.round<c_.start_round||f.round>=1000000000||!detail::body(f.self)||f.round<last_round_[at])
            return {Status::NeedObservation,Failure::Stale,{}};
        processed_round_=f.round;processed_input_=f.input;processed_self_=f.self;
        if(progress_.cancelled&&pending_valid_)return {Status::Aborting,Failure::CommitmentBroken,{}};
        if(issued_round_[at]==f.round)return {Status::AlreadyIssued,Failure::None,{}};
        if(pending_valid_&&(f.owner!=pending_.owner||f.round!=pending_.round||f.input!=pending_.input))
            abort(Failure::InvalidEffect,f.round);
        // Native own inputs are consecutive while this contract owns a body.
        // A gap cannot silently hide death/respawn, payout or a channel break.
        // Positive queried respawn duration makes any intervening death visible
        // in at least one of these consecutive owner observations.
        if(actual_.members[at].committed&&last_round_[at]>=0&&f.round>last_round_[at]+1
            &&phase_!=Phase::AbortCleanup)abort(Failure::NeedObservation,f.round);
        last_round_[at]=f.round;residual(f);
        if(!f.self.alive)abort(Failure::Death,f.round);
        for(unsigned i=0;i<actors;++i)if(f.peers[i].observed_now&&f.peers[i].body.id==c_.members[i].id&&!f.peers[i].body.alive)
            abort(Failure::Death,f.round);
        if(phase_!=Phase::Commit&&phase_!=Phase::AbortCleanup)for(unsigned i=0;i<actors;++i) {
            if(f.commitment_known[i]&&!f.commitment_matches[i])abort(Failure::CommitmentBroken,f.round);
            else if(!f.commitment_known[i])return unavailable(f,at,Failure::NeedObservation);
        }
        // A peer may publish its on-time milestone after our last input in the
        // deadline round. Reconcile it against the CURRENT personally observed
        // world before testing the next phase's deadline. A local milestone
        // first reached late cannot use this allowance.
        if(phase_!=Phase::AbortCleanup&&f.round>deadline()&&!locally_done(at))abort(Failure::Deadline,f.round);
        if(phase_==Phase::AbortCleanup&&f.round>abort_by_) {
            phase_=Phase::Closed;actual_.cleanup_complete=false;return {Status::Closed,Failure::CleanupIncomplete,{}};
        }
        if(!f.self.alive)return {Status::Aborting,Failure::Death,{}}; // dead actions belong to the fallback adapter
        progress(f,at);advance(f);
        if(phase_!=Phase::AbortCleanup&&f.round>deadline())abort(Failure::Deadline,f.round);
        if(phase_==Phase::Complete)return {complete_status(),Failure::None,{}};
        if(phase_==Phase::Closed)return {Status::Closed,failure_,{}};
        if(waiting_work_proof_)return unavailable(f,at,Failure::NeedObservation);
        if(phase_==Phase::Work) {
            if(!f.group.observed_now||f.group.id!=c_.group)return unavailable(f,at,Failure::NeedObservation);
            for(unsigned i=0;i<c_.slot_count;++i)if(!slot_done_[i]&&c_.slots[i].actor==f.owner&&c_.slots[i].round<f.round)
                abort(Failure::MissedWorkSlot,f.round);
        }
        auto intent=nominate(f,at);
        if(intent.kind==Kind::None)return {Status::AlreadyIssued,Failure::None,intent};
        if(intent.slot>=0) {
            const auto dependency=c_.slots[intent.slot].required_support_slot;
            if(dependency>=0&&!progress_.slots[dependency]){abort(Failure::SupportFailed,f.round);intent=nominate(f,at);}
        }
        if(!guard(f,intent)) {
            auto out=unavailable(f,at,Failure::NeedModel);out.intent=intent;return out;
        }
        deferred_[at]=0;intent.guarded=true;intent.serial=++serial_;
        intent.word_token=f.guard.word_token;
        intent.selected_cast=intent.kind==Kind::Commit?Cast::None:f.guard.selected_cast;
        pending_=intent;before_=f;pending_valid_=true;issued_round_[at]=f.round;
        return {Status::Ready,Failure::None,intent};
    }
    // Apply the actual accepted prefix even when its later primitive failed.
    // Each tape is scoped to one issued word; a later input cannot consume it.
    bool confirm(const WordIntent& request,const Frame& after,const Tape& tape) {
        if(!pending_valid_||request.serial!=pending_.serial||request.contract!=c_.id||request.owner!=pending_.owner
            ||request.round!=pending_.round||request.input!=pending_.input||request.word_token!=pending_.word_token
            ||request.kind!=pending_.kind||request.camp!=pending_.camp||request.recipient!=pending_.recipient
            ||request.slot!=pending_.slot||request.selected_cast!=pending_.selected_cast
            ||!(request.destination==pending_.destination)||request.guarded!=pending_.guarded)return false;
        pending_valid_=false;const auto at=static_cast<unsigned>(detail::member(c_,request.owner));
        if(!after.fresh||after.owner!=request.owner||after.round!=request.round||after.input!=request.input
            ||after.self.id!=request.owner||after.self.role!=before_.self.role||!detail::body(after.self)
            ||tape.count>max_effects||tape.word_token!=request.word_token) {
            abort(Failure::InvalidEffect,request.round);return false;
        }
        processed_round_=after.round;processed_input_=after.input;processed_self_=after.self;
        unsigned casts=0,moves=0;Cast used_cast=Cast::None;bool useful=false,invalid=false,channel_started=false;
        float shield=before_.self.shield;bool attack=before_.self.attack;Point cursor=before_.self.position;
        std::array<float,max_camps> expected_hp{};std::array<bool,max_camps> expected_known{},damaged{};
        for(unsigned i=0;i<before_.group.count&&i<max_camps;++i) {
            const auto& source=before_.group.camps[i];const int ci=detail::camp(c_,source.id);
            if(ci>=0&&source.observed_now&&detail::finite(source.hp)){expected_hp[ci]=source.hp;expected_known[ci]=true;}
        }
        for(unsigned i=0;i<tape.count;++i) {
            const auto& e=tape.effects[i];
            if(!e.fresh||!e.accepted){invalid=true;continue;}
            if(static_cast<unsigned>(e.kind)>static_cast<unsigned>(EffectKind::RecallStarted)){invalid=true;continue;}
            if(channel_started){invalid=true;continue;}
            switch(e.kind) {
            case EffectKind::CommitPublished:
                if(request.kind!=Kind::Commit||!after.commitment_known[at]||!after.commitment_matches[at])invalid=true;
                else actual_.members[at].committed=true;
                break;
            case EffectKind::Move:
                if(++moves>1||!before_.self.move||!(e.from==cursor)||!detail::point(e.to))invalid=true;
                cursor=e.to;
                break;
            case EffectKind::Dash:++casts;used_cast=Cast::Dash;
                if(!before_.self.cast||!before_.self.dash_ready||!(e.from==cursor)||!detail::point(e.to))invalid=true;
                cursor=e.to;break;
            case EffectKind::BasicDamage: {
                const int ci=detail::camp(c_,e.target);
                const bool actual_source=ci>=0&&expected_known[ci]&&e.before==expected_hp[ci];
                if(!detail::work(request.kind)||request.kind==Kind::SupportHeal||e.target!=request.camp||ci<0||!detail::finite(e.before)
                    ||!detail::finite(e.after)||e.after>e.before||!attack||!actual_source)invalid=true;
                if(ci>=0&&detail::finite(e.after)) {actual_.residual_hp[ci]=e.after;actual_.residual_known[ci]=true;}
                if(actual_source&&detail::finite(e.before)&&detail::finite(e.after)&&e.before>e.after) {
                    actual_.members[at].camp_work+=e.before-e.after;useful=true;
                }
                if(ci>=0&&detail::finite(e.after)){expected_hp[ci]=e.after;expected_known[ci]=true;damaged[ci]=true;}
                attack=false;break;
            }
            case EffectKind::ResetCast:
                ++casts;used_cast=Cast::Reset;if(!before_.self.cast||!before_.self.reset_ready||attack||request.kind==Kind::SupportHeal)invalid=true;
                attack=true;break;
            case EffectKind::ShieldCast:
                ++casts;used_cast=Cast::Shield;
                if(at!=0||!before_.self.cast||!before_.self.shield_ready||!detail::finite(e.after)||e.after<shield
                    ||e.before!=before_.self.shield||e.after!=after.self.shield)invalid=true;
                if(detail::finite(e.after)&&e.after>=shield){actual_.members[at].actual_shield+=e.after-shield;shield=e.after;}
                break;
            case EffectKind::HealCast: {
                ++casts;used_cast=Cast::Heal;const int target=detail::member(c_,e.target);
                if(request.kind!=Kind::SupportHeal||at!=1||e.target!=request.recipient||target<0||!before_.self.cast
                    ||!before_.self.heal_ready||!detail::finite(e.before)||!detail::finite(e.after)||e.after<=e.before
                    ||!before_.peers[target].observed_now||!after.peers[target].observed_now
                    ||after.peers[target].body.id!=e.target||after.peers[target].body.role!=c_.members[target].role
                    ||e.before!=before_.peers[target].body.hp||e.after!=after.peers[target].body.hp
                    ||e.after>after.peers[target].body.max_hp)invalid=true;
                else {actual_.members[at].useful_heal+=e.after-e.before;actual_.useful_support_observed=true;useful=true;}
                break;
            }
            case EffectKind::SubmitStarted:
                if(request.kind!=Kind::Submit||!e.timer||e.timer!=before_.self.submit_ticks||after.self.channel!=Channel::Submit
                    ||before_.self.bag<actual_.members[at].captured_gems)invalid=true;
                else {bank_active_[at]=true;bank_round_[at]=request.round;bank_ticks_[at]=e.timer;
                    bank_bag_[at]=before_.self.bag;bank_score_[at]=before_.self.team_score;bank_pose_[at]=before_.self.position;}
                channel_started=true;break;
            case EffectKind::RecallStarted:
                if(request.kind!=Kind::Recover||!e.timer||e.timer!=before_.self.recall_ticks
                    ||e.timer!=before_.guard.priced_channel_ticks||after.self.channel!=Channel::Recall)invalid=true;
                else {recovery_active_[at]=true;recovery_round_[at]=request.round;recovery_ticks_[at]=e.timer;}
                channel_started=true;break;
            }
        }
        if(casts>1||used_cast!=request.selected_cast||(after.self.alive&&!(after.self.position==cursor)))invalid=true;
        for(unsigned i=0;i<after.group.count&&i<max_camps;++i) {
            const auto& source=after.group.camps[i];const int ci=detail::camp(c_,source.id);
            if(ci>=0&&damaged[ci]&&source.observed_now&&source.hp!=expected_hp[ci])invalid=true;
        }
        // shields, resets, heals and dashes share ONE cast
        if(request.slot>=0) {
            if(!useful||invalid||!tape.complete)abort(request.kind==Kind::SupportHeal?Failure::SupportFailed:Failure::InvalidEffect,request.round);
            else slot_done_[request.slot]=true;
        }
        if(tape.refused&&++refused_[at]>=c_.max_refusals)abort(Failure::Refusal,request.round);
        if(invalid)abort(Failure::InvalidEffect,request.round);
        if(!after.self.alive)abort(Failure::Death,request.round);
        for(unsigned i=0;i<actors;++i)if(after.peers[i].observed_now&&after.peers[i].body.id==c_.members[i].id&&!after.peers[i].body.alive)
            abort(Failure::Death,request.round);
        if(after.self.stunned) {
            if(request.kind==Kind::Submit)abort(Failure::BankInterrupted,request.round);
            else if(detail::work(request.kind)||request.kind==Kind::Enter||request.kind==Kind::Renew)abort(Failure::NeedModel,request.round);
        }
        if((request.kind==Kind::Commit||request.kind==Kind::Assemble)&&!after.self.npc_quiet)
            abort(Failure::NeedModel,request.round);
        if(progress_.cancelled&&phase_!=Phase::AbortCleanup)abort(Failure::CommitmentBroken,request.round);
        residual(after);progress(after,at);advance(after);
        return !invalid;
    }
    // Close one outstanding word after an input boundary without pretending
    // the uncertain last command had any effect. prefix_after and prefix are
    // the dated ACTUAL last fully observed prefix, not a new-input frame. The
    // caller retains the uncertain attempt separately and supplies a fresh
    // current frame to tick() before any cleanup action.
    InterruptedWord interrupt(const WordIntent& request,const Frame& prefix_after,const Tape& prefix,int current_round) {
        if(!pending_valid_||current_round<request.round||request.serial!=pending_.serial||request.contract!=c_.id
            ||request.owner!=pending_.owner||request.round!=pending_.round||request.input!=pending_.input
            ||request.word_token!=pending_.word_token||request.kind!=pending_.kind||request.camp!=pending_.camp
            ||request.recipient!=pending_.recipient||request.slot!=pending_.slot||request.selected_cast!=pending_.selected_cast
            ||!(request.destination==pending_.destination)||request.guarded!=pending_.guarded)return {};
        auto partial=prefix;partial.complete=false;
        const bool was_aborting=phase_==Phase::AbortCleanup;
        // Cancellation does not erase the private word. confirm() first
        // accounts its actual prefix, then enters cleanup before advancement
        // could publish completion from this interrupted word.
        progress_.cancelled=true;progress_.attested_complete=false;
        const bool confirmed=confirm(request,prefix_after,partial);
        // A nonmatching request leaves the real ticket live in confirm().
        if(pending_valid_)return {};
        // confirm may reject an incomplete mandatory slot at its historical
        // date. Cleanup starts NOW, not in an already elapsed suspended input.
        if(!was_aborting&&phase_==Phase::AbortCleanup) {
            abort_by_=current_round+static_cast<int>(c_.abort_ticks);
            failure_=actual_.failure=Failure::NeedObservation;
        }
        abort(Failure::NeedObservation,current_round);
        return {true,confirmed};
    }
};

// Complete modeled alternatives at ONE common horizon. These fields cannot
// populate Completion or any complete_observed flag; actual receipts stay above.
struct Estimate {
    bool supported{};int start_round{-1},horizon_round{-1};
    std::array<int,actors> actor{-1,-1,-1};
    std::array<double,actors> banked{},owned_capital{},displaced_inputs{},priority_externality{};
};
struct Comparison {
    bool comparable{},modeled_only{true};
    double banked_difference{},capital_difference{},input_difference{},priority_externality_difference{};
};
inline Comparison compare_modeled(const Estimate& joint,const std::array<Estimate,actors>& solo) {
    Comparison out;if(!joint.supported||joint.start_round<0||joint.horizon_round<=joint.start_round)return out;
    for(unsigned i=0;i<actors;++i) {
        const auto& s=solo[i];if(!s.supported||s.start_round!=joint.start_round||s.horizon_round!=joint.horizon_round
            ||joint.actor[i]<0||s.actor[i]!=joint.actor[i])return {};
        for(unsigned j=0;j<i;++j)if(joint.actor[j]==joint.actor[i])return {};
        for(unsigned j=0;j<actors;++j)if(j!=i&&s.actor[j]>=0)return {};
        const std::array values{joint.banked[i],joint.owned_capital[i],joint.displaced_inputs[i],joint.priority_externality[i],
            s.banked[i],s.owned_capital[i],s.displaced_inputs[i],s.priority_externality[i]};
        for(const auto x:values)if(!std::isfinite(x)||x<0)return {};
        out.banked_difference+=joint.banked[i]-s.banked[i];out.capital_difference+=joint.owned_capital[i]-s.owned_capital[i];
        out.input_difference+=joint.displaced_inputs[i]-s.displaced_inputs[i];
        out.priority_externality_difference+=joint.priority_externality[i]-s.priority_externality[i];
    }
    if(!std::isfinite(out.banked_difference)||!std::isfinite(out.capital_difference)||!std::isfinite(out.input_difference)
        ||!std::isfinite(out.priority_externality_difference))return {};
    out.comparable=true;return out;
}
} // namespace wb::capital::party
