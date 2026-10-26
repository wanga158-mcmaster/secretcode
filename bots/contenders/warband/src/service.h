#pragma once

#include "service_micro.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace wb::service {
using Authority = service_micro::Authority;
using Position = service_micro::Position;
inline constexpr std::size_t max_members = 30;

enum class Phase { Idle, Ingress, Work, CaptureEntry, CaptureHold, BankTravel,
    BankChannel, ReturnTravel, RecallChannel, Shop, Heal, Renew, Complete, Aborted };
enum class RouteRole { WorkMember, Capture, Bank, Spawn };
enum class Op { None, Move, Basic, Hold, Submit, Recall, Shop, Heal, Renew };
enum class Status { NoCommand, NeedObservation, ComputeDeferred, Unsupported,
    Issued, Refused, Applied, Complete, Aborted };
enum class Acceptance { Accepted, Refused, Invalidated };
enum class Channel { None, Recall, Tribute, Other };
enum class GroupState { Unknown, Alive, RewardAvailable, Captured };
enum class Reason { None, InvalidContract, WrongOwner, StaleFrame, ConsumedInput,
    AwaitResult, MissingResult, AwaitEffect, NoEffect, ActorDead, UnsafeDomain,
    UnknownMember, WrongMember, UnsupportedMember, RouteMissing, RouteDeferred,
    RouteInvalidated, RefusalLimit, DeferralLimit, Allowance, GroupUnknown,
    CaptureEntry, CaptureWait, CaptureContested, CaptureTimeout, ReceiptMissing,
    ReceiptInvalid, BagMismatch, BankAccess, BankWait, BankProofMissing,
    ChannelInterrupted, ChannelTimeout, SpawnAccess, ShopUnavailable,
    ShopEffectMissing, RecoveryWait, RecoveryTimeout, ReadinessWait,
    Renewed, UnsupportedOperation };

struct Receipt {
    bool present{}, verified{};
    int group{-1}, gems{};
    std::uint64_t serial{};
};

struct Contract {
    int owner{-1}, group{-1};
    std::array<int, max_members> member_ids{};
    std::size_t member_count{};
    int min_own_receipt{1};
    bool start_from_receipt{};
    Receipt carried_receipt{};
    bool require_shop{}, return_by_recall{}, require_full_health{true};
    unsigned max_refusals{2}, max_deferrals{2};
    unsigned capture_wait_bound{8}, channel_wait_bound{64}, recovery_wait_bound{3};
};
struct Member {
    int id{-1};
    bool known{}, alive{}, visible{}, harmless{};
    Position position{};
    float hp{};
};
struct Route {
    RouteRole role{RouteRole::WorkMember};
    std::size_t member_index{};
    bool goal_reached{}, next_step_available{}, next_step_legal{}, quiet{};
    bool deferred{}, invalidated{};
    Position next{};
    std::uint64_t progress_counter{};
};
struct OwnBankReceipt {
    bool present{}, verified{};
    int gems{};
    std::uint64_t serial{};
};
// All physical facts are fresh readbacks. Paths/route counters never grant authority.
// submitted_counter is OWN lifetime submissions or an externally verified own
// accumulation, never an unqualified team-score counter.
struct Frame {
    Authority authority{};
    bool fresh{}, alive{}, domain_harmless{}, static_hazard_excluded{};
    Position position{};
    float hp{}, max_hp{};
    int attack_range{};
    bool can_move{}, can_attack{}, can_cast{}, restricted{};
    Channel channel{Channel::None};
    int channel_remaining{-1};
    std::array<Member, max_members> members{};
    GroupState group_state{GroupState::Unknown};
    bool group_fresh{}, capture_occupied{}, capture_contested{};
    int capture_remaining{-1};
    int bag{}, gold{};
    bool submitted_known{};
    std::uint64_t submitted_counter{};
    Receipt receipt{};
    OwnBankReceipt own_bank_receipt{};
    bool submit_ready{}, at_spawn{}, shop_ready{}, shop_complete{};
    bool renewal_ready{};
    Route route{};
};
struct Result { Acceptance acceptance{Acceptance::Invalidated}; };
struct Rank {
    unsigned remaining_milestones{};
    std::size_t live_members{};
    float observed_remaining_hp{};
    unsigned neutral_budget_left{};
};
struct Step {
    Status status{Status::NoCommand};
    Op op{Op::None};
    RouteRole route_role{RouteRole::WorkMember};
    std::size_t member_index{};
    Position position{};
    Authority authority{};
    // An abort identifies this finite certificate/attempt's failure, never a
    // theorem that no physical path or service exists in the world.
    Reason reason{Reason::None};
    Rank rank{};
};

class Interpreter {
    Contract contract_{};
    Phase phase_{Phase::Idle};
    std::size_t member_{};
    bool bound_{}, initialized_{}, pending_{}, await_effect_{}, consumed_{};
    Authority issued_{}, consumed_input_{}, last_frame_{};
    Step proposal_{};
    Position before_position_{};
    float before_hp_{};
    std::uint64_t initial_submitted_{}, initial_receipt_serial_{}, initial_bank_serial_{}, receipt_serial_{};
    std::uint64_t settled_receipt_serial_{};
    bool initial_submitted_known_{};
    int receipt_gems_{}, spawn_round_{-1};
    unsigned refusals_{}, deferrals_{}, waits_{};
    int wait_round_{-1};
    Reason terminal_reason_{Reason::None};

    static bool valid(const Authority& a) {
        return a.owner >= 0 && a.round >= 0 && a.input_generation > 0 && a.view_revision > 0;
    }
    static bool same_input(const Authority& a, const Authority& b) {
        return a.owner == b.owner && a.round == b.round && a.input_generation == b.input_generation;
    }
    static bool same_view(const Authority& a, const Authority& b) {
        return same_input(a,b) && a.view_revision == b.view_revision;
    }
    static int distance(Position a, Position b) {
        const auto dx=std::int64_t(a.x)-std::int64_t(b.x);
        const auto dy=std::int64_t(a.y)-std::int64_t(b.y);
        const auto x=dx<0 ? -dx : dx, y=dy<0 ? -dy : dy;
        const auto d=x>y ? x : y;
        return d>std::numeric_limits<int>::max() ? std::numeric_limits<int>::max() : int(d);
    }
    Rank rank(const Frame& f) const {
        Rank r;
        switch (phase_) {
        case Phase::Idle: case Phase::Ingress: case Phase::Work: r.remaining_milestones=6; break;
        case Phase::CaptureEntry: case Phase::CaptureHold: r.remaining_milestones=5; break;
        case Phase::BankTravel: r.remaining_milestones=4; break;
        case Phase::BankChannel: r.remaining_milestones=3; break;
        case Phase::ReturnTravel: case Phase::RecallChannel: r.remaining_milestones=2; break;
        case Phase::Shop: case Phase::Heal: case Phase::Renew: r.remaining_milestones=1; break;
        default: break;
        }
        for (std::size_t i=0;i<contract_.member_count;++i) {
            if (f.members[i].known && f.members[i].alive) {
                ++r.live_members;
                if (std::isfinite(f.members[i].hp) && f.members[i].hp>0) r.observed_remaining_hp+=f.members[i].hp;
            }
        }
        r.neutral_budget_left=deferrals_ < contract_.max_deferrals ? contract_.max_deferrals-deferrals_ : 0;
        return r;
    }
    Step answer(const Frame& f, Status s, Op op=Op::None, Reason reason=Reason::None) const {
        return {s,op,route_role(),member_,{},f.authority,reason,rank(f)};
    }
    Step abort(const Frame& f, Reason reason, Status status=Status::Aborted) {
        phase_=Phase::Aborted;terminal_reason_=reason;pending_=await_effect_=false;
        return answer(f,status,Op::None,reason);
    }
    void progress() { deferrals_=0;waits_=0;wait_round_=-1; }
    Step defer(const Frame& f, Reason reason, Status status=Status::NeedObservation) {
        // Re-observation inside one input is permitted, but does not replenish an
        // epoch's finite neutral budget. Only measured progress replenishes it.
        if (++deferrals_ > contract_.max_deferrals) return abort(f,Reason::DeferralLimit);
        return answer(f,status,Op::None,reason);
    }
    Step wait(const Frame& f, Op op, Reason reason, unsigned bound) {
        if (wait_round_ != f.authority.round) { wait_round_=f.authority.round;++waits_; }
        if (waits_ > bound) return abort(f,(reason==Reason::RecoveryWait || reason==Reason::ReadinessWait) ? Reason::RecoveryTimeout :
            reason==Reason::CaptureWait ? Reason::CaptureTimeout : Reason::ChannelTimeout);
        return answer(f,Status::NoCommand,op,reason);
    }
    Step issue(const Frame& f, Op op, Position p={}) {
        issued_=f.authority;consumed_input_=f.authority;consumed_=true;pending_=true;
        proposal_=answer(f,Status::Issued,op);proposal_.position=p;
        before_position_=f.position;
        before_hp_=member_ < contract_.member_count ? f.members[member_].hp : 0;
        return proposal_;
    }
    bool bank_proven(const Frame& f) const {
        if(receipt_gems_ < contract_.min_own_receipt || f.bag!=0 || f.channel!=Channel::None) return false;
        const auto& r=f.own_bank_receipt;
        const bool receipt=r.present && r.verified && r.serial>initial_bank_serial_ && r.gems>=receipt_gems_;
        const bool counter=initial_submitted_known_ && f.submitted_known &&
            f.submitted_counter>=initial_submitted_ &&
            f.submitted_counter-initial_submitted_>=static_cast<std::uint64_t>(receipt_gems_);
        return receipt || counter;
    }
    bool all_dead(const Frame& f) const {
        for(std::size_t i=0;i<contract_.member_count;++i)
            if(!f.members[i].known || f.members[i].id!=contract_.member_ids[i] || f.members[i].alive) return false;
        return true;
    }
    bool current_clear(const Frame& f) const {
        if(!f.group_fresh || f.group_state!=GroupState::RewardAvailable) return false;
        // Native current reward-available proves the whole group cleared even
        // when hidden sibling HP was never collected. Contradictory fresh live
        // members are not silently called dead.
        for(std::size_t i=0;i<contract_.member_count;++i)
            if(f.members[i].known && f.members[i].alive) return false;
        return true;
    }
    Step move(const Frame& f, RouteRole role) {
        if(f.route.role!=role || (role==RouteRole::WorkMember && f.route.member_index!=member_))
            return defer(f,Reason::RouteMissing);
        if(f.route.invalidated || !f.route.quiet) return abort(f,Reason::RouteInvalidated);
        if(f.route.deferred) return defer(f,Reason::RouteDeferred,Status::ComputeDeferred);
        if(!f.route.next_step_available || !f.route.next_step_legal) return defer(f,Reason::RouteMissing);
        if(!f.can_move) return wait(f,Op::Hold,Reason::Allowance,contract_.max_deferrals);
        if(f.route.next.x<0 || f.route.next.y<0 || distance(f.position,f.route.next)!=1)
            return abort(f,Reason::RouteInvalidated);
        return issue(f,Op::Move,f.route.next);
    }
    bool effect(const Frame& f) const {
        switch(proposal_.op) {
        case Op::Move: return f.position==proposal_.position;
        case Op::Basic: return member_ < contract_.member_count && f.members[member_].known &&
            f.members[member_].id==contract_.member_ids[member_] &&
            (!f.members[member_].alive || (std::isfinite(f.members[member_].hp) && f.members[member_].hp < before_hp_));
        case Op::Submit: return f.channel==Channel::Tribute || bank_proven(f);
        case Op::Recall: return f.channel==Channel::Recall || f.at_spawn;
        case Op::Shop: return f.shop_complete;
        default: return false;
        }
    }
public:
    bool begin(const Contract& c) {
        // A new contract cannot erase a live continuation/neutral budget.
        // Even a terminal replacement retains consumed physical-input authority.
        if(bound_ && phase_!=Phase::Complete && phase_!=Phase::Aborted) return false;
        if(c.start_from_receipt && (!c.carried_receipt.present || !c.carried_receipt.verified ||
            c.carried_receipt.group!=c.group || c.carried_receipt.gems<c.min_own_receipt ||
            c.carried_receipt.serial<=settled_receipt_serial_)) return false;
        const auto prior_input=consumed_input_;const bool prior_consumed=consumed_;
        const auto settled=settled_receipt_serial_;
        *this=Interpreter{};contract_=c;consumed_input_=prior_input;consumed_=prior_consumed;
        settled_receipt_serial_=settled;
        if(c.owner<0 || c.group<0 || (!c.start_from_receipt && c.member_count==0) || c.member_count>max_members ||
            c.min_own_receipt<=0 || c.max_deferrals==0 || c.max_refusals==0 ||
            c.capture_wait_bound==0 || c.channel_wait_bound==0 || c.recovery_wait_bound==0) return false;
        for(std::size_t i=0;i<c.member_count;++i) {
            if(c.member_ids[i]<0) return false;
            for(std::size_t j=0;j<i;++j) if(c.member_ids[i]==c.member_ids[j]) return false;
        }
        bound_=true;phase_=c.start_from_receipt ? Phase::BankTravel : Phase::Ingress;
        if(c.start_from_receipt) {receipt_serial_=c.carried_receipt.serial;receipt_gems_=c.carried_receipt.gems;}
        return true;
    }
    Phase phase() const { return phase_; }
    std::size_t member_index() const { return member_; }
    RouteRole route_role() const {
        if(phase_==Phase::CaptureEntry || phase_==Phase::CaptureHold) return RouteRole::Capture;
        if(phase_==Phase::BankTravel || phase_==Phase::BankChannel) return RouteRole::Bank;
        if(phase_==Phase::ReturnTravel || phase_==Phase::RecallChannel || phase_==Phase::Shop ||
            phase_==Phase::Heal || phase_==Phase::Renew || phase_==Phase::Complete) return RouteRole::Spawn;
        return RouteRole::WorkMember;
    }
    Status complete(const Authority& a, Result result) {
        if(!pending_ || !same_view(a,issued_)) return Status::NoCommand;
        pending_=false;
        if(result.acceptance==Acceptance::Invalidated) {
            phase_=Phase::Aborted;terminal_reason_=Reason::StaleFrame;return Status::Aborted;
        }
        if(result.acceptance==Acceptance::Refused) {
            if(++refusals_>=contract_.max_refusals) {phase_=Phase::Aborted;terminal_reason_=Reason::RefusalLimit;return Status::Aborted;}
            return Status::Refused;
        }
        await_effect_=true;
        if(proposal_.op==Op::Submit) phase_=Phase::BankChannel;
        if(proposal_.op==Op::Recall) phase_=Phase::RecallChannel;
        return Status::NeedObservation; // A normal void return is acceptance, not damage/conversion.
    }
    Step step(const Frame& f) {
        if(!bound_) return answer(f,Status::Aborted,Op::None,Reason::InvalidContract);
        if(phase_==Phase::Aborted) return answer(f,Status::Aborted,Op::None,terminal_reason_);
        if(phase_==Phase::Complete) return answer(f,Status::Complete,Op::Renew,Reason::Renewed);
        if(!valid(f.authority) || !f.fresh) return defer(f,Reason::StaleFrame);
        if(f.authority.owner!=contract_.owner) return abort(f,Reason::WrongOwner);
        if(!f.alive) return abort(f,Reason::ActorDead);
        if(f.position.x<0 || f.position.y<0 || !std::isfinite(f.hp) || !std::isfinite(f.max_hp) ||
            f.max_hp<=0 || f.hp<=0 || f.hp>f.max_hp)
            return defer(f,Reason::StaleFrame);
        if(!f.domain_harmless || !f.static_hazard_excluded || f.restricted)
            return abort(f,Reason::UnsafeDomain,Status::Unsupported);
        if(f.channel!=Channel::None &&
            !((phase_==Phase::BankChannel && f.channel==Channel::Tribute) ||
              (phase_==Phase::RecallChannel && f.channel==Channel::Recall)))
            return abort(f,Reason::ChannelInterrupted,Status::Unsupported);
        if(last_frame_.round>=0 && (f.authority.round<last_frame_.round ||
            (f.authority.round==last_frame_.round && f.authority.input_generation!=last_frame_.input_generation) ||
            (f.authority.round>last_frame_.round && f.authority.input_generation<=last_frame_.input_generation) ||
            (same_input(f.authority,last_frame_) && f.authority.view_revision<last_frame_.view_revision)))
            return abort(f,Reason::StaleFrame);
        last_frame_=f.authority;
        if(!initialized_) {
            if(contract_.start_from_receipt) {
                const auto& r=contract_.carried_receipt;
                if(f.bag<r.gems || !f.receipt.present || !f.receipt.verified || f.receipt.serial!=r.serial ||
                    f.receipt.group!=r.group || f.receipt.gems!=r.gems) return abort(f,Reason::ReceiptInvalid);
            } else if(f.bag!=0) return abort(f,Reason::BagMismatch);
            initial_submitted_=f.submitted_counter;initial_submitted_known_=f.submitted_known;
            initial_receipt_serial_=f.receipt.serial;initial_bank_serial_=f.own_bank_receipt.serial;
            initialized_=true;
        }
        if(pending_) {
            if(!same_input(f.authority,issued_)) return abort(f,Reason::MissingResult);
            return answer(f,Status::NeedObservation,Op::None,Reason::AwaitResult);
        }
        if(await_effect_) {
            if(same_view(f.authority,issued_)) return answer(f,Status::NeedObservation,Op::None,Reason::AwaitEffect);
            if(!effect(f)) return defer(f,Reason::NoEffect);
            await_effect_=false;progress();
        }
        if(consumed_ && same_input(f.authority,consumed_input_))
            return answer(f,Status::NoCommand,Op::None,Reason::ConsumedInput);
        // Phase-only transitions are bounded by a fixed finite grammar; no SDK or search.
        for(unsigned transitions=0;transitions<40;++transitions) {
            switch(phase_) {
            case Phase::Ingress: case Phase::Work: {
                for(std::size_t i=0;i<contract_.member_count;++i) {
                    if(f.members[i].known && f.members[i].id!=contract_.member_ids[i]) return abort(f,Reason::WrongMember);
                    if(f.members[i].known && f.members[i].alive && !f.members[i].harmless)
                        return abort(f,Reason::UnsupportedMember,Status::Unsupported);
                    if(f.group_fresh && f.group_state==GroupState::RewardAvailable &&
                        f.members[i].known && f.members[i].alive) return defer(f,Reason::GroupUnknown);
                }
                if(current_clear(f)) {
                    phase_=Phase::CaptureEntry;progress();continue;
                }
                if(all_dead(f)) return defer(f,Reason::GroupUnknown);
                while(member_<contract_.member_count && f.members[member_].known && !f.members[member_].alive)
                    {++member_;progress();}
                if(member_==contract_.member_count) return abort(f,Reason::WrongMember);
                const auto& m=f.members[member_];
                if(!m.known || !m.visible) {
                    // Immutable harmless type/route facts admit observation
                    // ingress, never an attack on a fabricated live target.
                    phase_=Phase::Ingress;
                    if(f.route.role==RouteRole::WorkMember && f.route.member_index==member_ && f.route.goal_reached)
                        return defer(f,Reason::UnknownMember);
                    return move(f,RouteRole::WorkMember);
                }
                if(m.position.x<0 || m.position.y<0 || !std::isfinite(m.hp) || m.hp<=0 || f.attack_range<1)
                    return defer(f,Reason::UnknownMember);
                if(distance(f.position,m.position)>f.attack_range) {phase_=Phase::Ingress;return move(f,RouteRole::WorkMember);}
                phase_=Phase::Work;
                if(!f.can_attack) return wait(f,Op::Hold,Reason::Allowance,contract_.max_deferrals);
                return issue(f,Op::Basic,m.position);
            }
            case Phase::CaptureEntry:
                if(!current_clear(f))
                    return abort(f,Reason::GroupUnknown);
                if(f.capture_occupied) {phase_=Phase::CaptureHold;progress();continue;}
                if(f.route.role==RouteRole::Capture && f.route.goal_reached) return abort(f,Reason::RouteInvalidated);
                return move(f,RouteRole::Capture);
            case Phase::CaptureHold:
                if(f.receipt.present && f.receipt.serial>initial_receipt_serial_) {
                    if(!f.receipt.verified || f.receipt.group!=contract_.group || f.receipt.gems<contract_.min_own_receipt ||
                        f.receipt.serial==0 || f.receipt.serial==receipt_serial_ || f.bag<f.receipt.gems)
                        return abort(f,Reason::ReceiptInvalid);
                    receipt_serial_=f.receipt.serial;receipt_gems_=f.receipt.gems;
                    phase_=Phase::BankTravel;progress();continue;
                }
                if(f.capture_contested) return abort(f,Reason::CaptureContested);
                if(!f.capture_occupied) return abort(f,Reason::CaptureEntry);
                if(!f.group_fresh) return defer(f,Reason::GroupUnknown);
                if(f.group_state!=GroupState::RewardAvailable) return defer(f,Reason::ReceiptMissing);
                return wait(f,Op::Hold,Reason::CaptureWait,contract_.capture_wait_bound);
            case Phase::BankTravel:
                if(f.bag<receipt_gems_) return abort(f,Reason::BagMismatch);
                if(f.route.role!=RouteRole::Bank || !f.route.goal_reached) return move(f,RouteRole::Bank);
                if(!f.submit_ready) return abort(f,Reason::BankAccess);
                return issue(f,Op::Submit);
            case Phase::BankChannel:
                if(bank_proven(f)) {
                    if(receipt_serial_>settled_receipt_serial_) settled_receipt_serial_=receipt_serial_;
                    phase_=Phase::ReturnTravel;progress();continue;
                }
                if(f.channel!=Channel::Tribute) return abort(f,Reason::BankProofMissing);
                return wait(f,Op::Hold,Reason::BankWait,contract_.channel_wait_bound);
            case Phase::ReturnTravel:
                if(f.at_spawn) {spawn_round_=f.authority.round;phase_=Phase::Shop;progress();continue;}
                if(contract_.return_by_recall) return issue(f,Op::Recall);
                if(f.route.role==RouteRole::Spawn && f.route.goal_reached) return abort(f,Reason::SpawnAccess);
                return move(f,RouteRole::Spawn);
            case Phase::RecallChannel:
                if(f.at_spawn && f.channel==Channel::None) {spawn_round_=f.authority.round;phase_=Phase::Shop;progress();continue;}
                if(f.channel!=Channel::Recall) return abort(f,Reason::ChannelInterrupted);
                return wait(f,Op::Hold,Reason::BankWait,contract_.channel_wait_bound);
            case Phase::Shop:
                if(!f.at_spawn) return abort(f,Reason::SpawnAccess);
                if(contract_.require_shop && !f.shop_complete) {
                    if(!f.shop_ready) return abort(f,Reason::ShopUnavailable);
                    return issue(f,Op::Shop);
                }
                phase_=Phase::Heal;continue;
            case Phase::Heal:
                if(!f.at_spawn) return abort(f,Reason::SpawnAccess);
                if(!std::isfinite(f.hp) || !std::isfinite(f.max_hp) || f.max_hp<=0) return defer(f,Reason::RecoveryWait);
                if(contract_.require_full_health && f.hp<f.max_hp)
                    return wait(f,Op::Heal,Reason::RecoveryWait,contract_.recovery_wait_bound);
                phase_=Phase::Renew;continue;
            case Phase::Renew:
                if(!f.at_spawn || f.bag!=0 || !bank_proven(f)) return abort(f,Reason::BagMismatch);
                if(f.authority.round<=spawn_round_ || !f.renewal_ready || !f.can_move || !f.can_attack)
                    return wait(f,Op::Heal,Reason::ReadinessWait,contract_.recovery_wait_bound);
                phase_=Phase::Complete;return answer(f,Status::Complete,Op::Renew,Reason::Renewed);
            default: return abort(f,Reason::InvalidContract);
            }
        }
        return abort(f,Reason::DeferralLimit);
    }
};
} // namespace wb::service
