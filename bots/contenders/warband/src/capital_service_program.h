#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

// SDK-free operational ownership, not a tactical/value/safety proof. Runtime
// supplies an actually implemented economic interpreter, including refusal,
// exit, custody, conversion and recovery branches, or one funded bounded piece
// of preparation. Neither a Grant nor an investment obligation predicts work,
// a purchase, a score receipt, vision completeness or zero enemy service cost.
namespace wb::capital::service_program {
using Revision=std::uint64_t;
struct Point {int x{},y{};};
inline constexpr bool operator==(Point a,Point b){return a.x==b.x&&a.y==b.y;}
enum Permission:std::uint32_t {Move=1u,Attack=2u,Cast=4u,Submit=8u,Buy=16u,Recall=32u};
inline constexpr std::uint32_t all_permissions=Move|Attack|Cast|Submit|Buy|Recall;
enum class Channel {Unknown,None,Submit,Other};
enum class Kind {Acquire,Work,Exit,Release,Capture,Bank,Investment,Recovery};
enum class Knowledge {Unknown,Modeled};
struct RiskDomain {
    Revision id{},reply_model_revision{},service_model_revision{};int horizon_round{-1};
    Knowledge replies{Knowledge::Unknown},services{Knowledge::Unknown};
};
struct InvestmentQuote {
    int target{-1};std::uint64_t cost{};Revision revision{},inventory_revision{};
    // observed means this TARGET's actual current quote, not an alternate
    // target nomination. available is target/kit/inventory eligibility, NOT
    // current spawn distance or shop access; Frame::Buy supplies immediate
    // purchase permission. Moving away cannot discharge the investment debt.
    bool observed{},available{};
};
struct Frame {
    int owner{-1},round{-1};std::uint64_t input{},sample{};Point pose{};
    std::uint64_t bag{},gold{};double hp{};bool alive{},stunned{},completed_turn{};
    Channel channel{Channel::Unknown};std::uint32_t permissions{};
    Revision kit_revision{},inventory_revision{};
    int selected_group{-1},source_family{-1};
    Revision source_life{},source_revision{},capability_revision{},route_revision{},occupancy_revision{},lock_revision{};
    Revision foe_revision{},observation_revision{},fuel_revision{};unsigned available_fuel{};
    InvestmentQuote next_investment{};bool observed{},fresh{};
};
enum class FirstKind {None,PhysicalWord,FundedPreparation,ActualClosedInput};
struct FirstAction {
    // action_id names the caller's immutable interpreted first word or bounded
    // preparation stage for this invocation. Grant is no authority to yield
    // before executing it, or to turn repeated no-progress queries into a wait.
    FirstKind kind{FirstKind::None};std::uint64_t action_id{};std::uint32_t required_permissions{};
    unsigned preparation_fuel{},mandatory_fuel{};
};
struct Program {
    Kind kind{Kind::Acquire};Frame source{};FirstAction action{};RiskDomain risk{};
    bool complete_interpreter{},funded{};
};
enum class GrantStatus {NoOffer,CompleteIncumbent,FundedPreparation,ActualClosedInput,
    NeedObservation,WrongOwner,ChangedDependencies,AwaitActual,MissingInterpreter,Unfunded,
    NeedFuel,PermissionChanged,MissingSource,MissingInvestment,Invalid};
struct Lease {
    std::uint64_t serial{},primitive_generation{};Program program{};unsigned minimum_fuel{};
};
struct Grant {GrantStatus status{GrantStatus::NoOffer};Lease lease{};};
inline bool granted(const Grant& g){return g.status==GrantStatus::CompleteIncumbent||g.status==GrantStatus::FundedPreparation||g.status==GrantStatus::ActualClosedInput;}

enum class InvestmentEnd {None,Purchased,AffordabilityRefuted,TargetRefuted,Death};
struct Investment {
    bool active{},current{},affordability_known{},affordable{};std::uint64_t id{};
    int target{-1},created_round{-1};std::uint64_t created_input{},cost{};
    Revision quote_revision{},inventory_revision{},kit_revision{};
};
struct InvestmentResolution {
    InvestmentEnd reason{InvestmentEnd::None};std::uint64_t obligation{},purchase_event{};
    int target{-1},round{-1};std::uint64_t input{},sample{};
};
struct ActualPurchase {
    bool observed{};std::uint64_t event{};int owner{-1},round{-1},target{-1};
    std::uint64_t input{},sample{},paid{};Revision quote_revision{},inventory_before{},inventory_after{};
};
enum class ObserveStatus {Accepted,Duplicate,NeedObservation,WrongOwner,Stale,ConflictingObservation};
struct ObservationResult {
    ObserveStatus status{ObserveStatus::NeedObservation};bool accepted{},purchase_accepted{},purchase_rejected{},lease_revoked{};
    InvestmentEnd investment_end{InvestmentEnd::None};
};

enum class FailureReason {None,TransientOccupied,NeedFuel,NeedObservation,PoseChanged,RouteUnavailable,UnsupportedFamily,Locked,NativeRefused};
struct FailureTarget {Kind stage{Kind::Acquire};int id{-1};Point pose{};unsigned required_fuel{};};
struct FailureKey {
    FailureReason reason{FailureReason::None};FailureTarget target{};Frame source{};bool valid{};
};
struct Failure {FailureKey key{};};
inline constexpr std::size_t failure_capacity=8;

namespace detail {
inline bool valid(const InvestmentQuote& q){return !q.observed||(q.target>=0&&q.revision&&(!q.available||q.cost));}
inline bool valid(const Frame& f) {
    return f.observed&&f.fresh&&f.owner>=0&&f.round>=0&&f.input&&f.sample&&std::isfinite(f.hp)&&f.hp>=0
        &&(!f.alive||f.hp>0)&&((f.pose.x>=0&&f.pose.y>=0&&f.pose.x<=100&&f.pose.y<=100)||(!f.alive&&f.pose==Point{-1,-1}))
        &&f.selected_group>=-1&&f.source_family>=-1&&static_cast<unsigned>(f.channel)<=static_cast<unsigned>(Channel::Other)
        &&!(f.permissions&~all_permissions)&&valid(f.next_investment);
}
inline bool same(const InvestmentQuote& a,const InvestmentQuote& b) {
    return a.target==b.target&&a.cost==b.cost&&a.revision==b.revision&&a.inventory_revision==b.inventory_revision
        &&a.observed==b.observed&&a.available==b.available;
}
// Fuel itself decreases during harmless SDK queries. Its discrete revision
// names a scheduling/budget class; exact fuel equality is never a lease test.
inline bool same(const Frame& a,const Frame& b) {
    return a.owner==b.owner&&a.round==b.round&&a.input==b.input&&a.sample==b.sample&&a.pose==b.pose
        &&a.bag==b.bag&&a.gold==b.gold&&a.hp==b.hp&&a.alive==b.alive&&a.stunned==b.stunned&&a.completed_turn==b.completed_turn
        &&a.channel==b.channel&&a.permissions==b.permissions&&a.kit_revision==b.kit_revision&&a.inventory_revision==b.inventory_revision
        &&a.selected_group==b.selected_group&&a.source_family==b.source_family&&a.source_life==b.source_life
        &&a.source_revision==b.source_revision&&a.capability_revision==b.capability_revision&&a.route_revision==b.route_revision
        &&a.occupancy_revision==b.occupancy_revision&&a.lock_revision==b.lock_revision&&a.foe_revision==b.foe_revision
        &&a.observation_revision==b.observation_revision&&a.fuel_revision==b.fuel_revision
        &&same(a.next_investment,b.next_investment)&&a.observed==b.observed&&a.fresh==b.fresh;
}
inline bool same(const RiskDomain& a,const RiskDomain& b) {
    return a.id==b.id&&a.reply_model_revision==b.reply_model_revision&&a.service_model_revision==b.service_model_revision
        &&a.horizon_round==b.horizon_round&&a.replies==b.replies&&a.services==b.services;
}
inline bool same(const FirstAction& a,const FirstAction& b) {
    return a.kind==b.kind&&a.action_id==b.action_id&&a.required_permissions==b.required_permissions
        &&a.preparation_fuel==b.preparation_fuel&&a.mandatory_fuel==b.mandatory_fuel;
}
inline bool same(const Program& a,const Program& b) {
    return a.kind==b.kind&&same(a.source,b.source)&&a.source.available_fuel==b.source.available_fuel&&same(a.action,b.action)&&same(a.risk,b.risk)
        &&a.complete_interpreter==b.complete_interpreter&&a.funded==b.funded;
}
inline bool camp_kind(Kind k){return k==Kind::Work||k==Kind::Exit||k==Kind::Release||k==Kind::Capture;}
inline bool closed(const Frame& f){
    // Dead, stunned and channeling bodies can still have economic shop work.
    // Buy must be actual target-specific availability, not a spawn-only hint.
    return !(f.permissions&Buy)&&(!f.alive||f.stunned||f.completed_turn||f.channel==Channel::Submit||f.channel==Channel::Other);
}
inline bool quote_current(const Frame& f){return f.next_investment.observed&&f.next_investment.inventory_revision==f.inventory_revision;}
inline bool same_target(const FailureTarget& a,const FailureTarget& b) {
    return a.stage==b.stage&&a.id==b.id&&a.pose==b.pose&&a.required_fuel==b.required_fuel;
}
inline bool same_life(const Frame& a,const Frame& b){return a.selected_group==b.selected_group&&a.source_life==b.source_life;}
inline bool same_failure(const FailureKey& a,const FailureKey& b) {
    if(!a.valid||!b.valid||a.reason!=b.reason||a.source.owner!=b.source.owner)return false;
    const auto& x=a.source;const auto& y=b.source;
    if(a.reason==FailureReason::UnsupportedFamily)
        return x.source_family==y.source_family&&x.capability_revision==y.capability_revision;
    if(!same_target(a.target,b.target))return false;
    // No elapsed-round/input comparison: time alone neither freezes nor
    // refutes a physical predicate. These revisions must describe THIS target
    // or route predicate, not an unrelated observation counter.
    switch(a.reason) {
    case FailureReason::TransientOccupied:
        return same_life(x,y)&&x.pose==y.pose&&x.route_revision==y.route_revision&&x.occupancy_revision==y.occupancy_revision;
    case FailureReason::NeedFuel:
        return x.fuel_revision==y.fuel_revision&&y.available_fuel<b.target.required_fuel;
    case FailureReason::NeedObservation:
        return same_life(x,y)&&x.observation_revision==y.observation_revision;
    case FailureReason::PoseChanged:
        return same_life(x,y)&&x.pose==y.pose;
    case FailureReason::RouteUnavailable:
        return same_life(x,y)&&x.pose==y.pose&&x.route_revision==y.route_revision
            &&x.occupancy_revision==y.occupancy_revision&&x.capability_revision==y.capability_revision;
    case FailureReason::Locked:
        return x.lock_revision==y.lock_revision;
    case FailureReason::NativeRefused:
        return same_life(x,y)&&x.pose==y.pose&&x.permissions==y.permissions&&x.route_revision==y.route_revision
            &&x.occupancy_revision==y.occupancy_revision&&x.lock_revision==y.lock_revision&&x.observation_revision==y.observation_revision;
    default:return false;
    }
}
}
// Failure is an invocation-dated observation about a particular dependency
// key, not a blacklist of a group/stat profile. Vacating the sole exit changes
// its target-scoped occupancy revision even if every combat stat is unchanged.
inline Failure failure(FailureReason reason,const Frame& frame,FailureTarget target={}) {
    Failure f;f.key={reason,target,frame,false};
    f.key.valid=detail::valid(frame)&&reason!=FailureReason::None
        &&static_cast<unsigned>(reason)<=static_cast<unsigned>(FailureReason::NativeRefused)
        &&static_cast<unsigned>(target.stage)<=static_cast<unsigned>(Kind::Recovery);
    if(reason==FailureReason::NeedFuel)f.key.valid=f.key.valid&&target.required_fuel>0;
    if(reason==FailureReason::UnsupportedFamily)f.key.valid=f.key.valid&&frame.source_family>=0&&frame.capability_revision;
    return f;
}
inline bool invalidated(const FailureKey& prior,const FailureKey& current){return prior.valid&&(!current.valid||!detail::same_failure(prior,current));}

class State {
    int owner_{-1};Frame actual_{};bool observed_{},await_actual_{},lease_valid_{};
    std::uint64_t generation_{},serial_{},next_investment_{},last_purchase_{};Lease lease_{};
    Investment investment_{};InvestmentResolution resolution_{};
    std::array<Failure,failure_capacity> failures_{};std::size_t failure_cursor_{};
    void end_investment(InvestmentEnd reason,const Frame& f,std::uint64_t event=0) {
        resolution_={reason,investment_.id,event,investment_.target,f.round,f.input,f.sample};
        investment_.active=false;investment_.current=false;investment_.affordability_known=false;investment_.affordable=false;
    }
    bool purchase(const ActualPurchase& p,const Frame& f) {
        return p.observed&&p.event>last_purchase_&&investment_.active&&p.owner==owner_&&p.round==f.round&&p.input==f.input&&p.sample==f.sample
            &&p.target==investment_.target&&p.quote_revision==investment_.quote_revision&&p.paid==investment_.cost
            &&p.inventory_before==investment_.inventory_revision&&p.inventory_after==f.inventory_revision&&p.inventory_after!=p.inventory_before;
    }
    void reconcile_investment(const Frame& f,ObservationResult& out,const ActualPurchase* p) {
        if(p&&p->observed) {
            if(purchase(*p,f)){last_purchase_=p->event;out.purchase_accepted=true;end_investment(InvestmentEnd::Purchased,f,p->event);out.investment_end=InvestmentEnd::Purchased;}
            else out.purchase_rejected=true;
        }
        // Gold/shops survive champion death. Keep funded investment through
        // dead inputs; actual death with no gold can close the old positive-
        // cost obligation, otherwise an actual target query resolves it.
        if(investment_.active&&!f.alive&&!f.gold){end_investment(InvestmentEnd::Death,f);out.investment_end=InvestmentEnd::Death;}
        const auto& q=f.next_investment;
        if(investment_.active&&detail::quote_current(f)&&q.target==investment_.target) {
            if(!q.available){end_investment(InvestmentEnd::TargetRefuted,f);out.investment_end=InvestmentEnd::TargetRefuted;}
            else if(f.gold<q.cost){end_investment(InvestmentEnd::AffordabilityRefuted,f);out.investment_end=InvestmentEnd::AffordabilityRefuted;}
            else {investment_.cost=q.cost;investment_.quote_revision=q.revision;investment_.inventory_revision=f.inventory_revision;investment_.kit_revision=f.kit_revision;}
        }
        if(!investment_.active&&detail::quote_current(f)&&q.available&&f.gold>=q.cost) {
            // A just-observed purchase cannot mint another debt from a stale
            // quote for the SAME purchased inventory generation. A genuinely
            // refreshed post-purchase quote has the new inventory revision.
            investment_={true,true,true,true,++next_investment_,q.target,f.round,f.input,q.cost,q.revision,f.inventory_revision,f.kit_revision};
        }
    }
    bool fresh(const Frame& f) const{return observed_&&!await_actual_&&detail::valid(f)&&detail::same(actual_,f)&&f.available_fuel<=actual_.available_fuel;}
public:
    explicit State(int owner=-1):owner_(owner){}
    const Frame& actual() const{return actual_;}
    bool actual_current() const{return observed_&&!await_actual_&&actual_.fresh;}
    bool awaiting_actual() const{return await_actual_;}
    std::uint64_t primitive_generation() const{return generation_;}
    InvestmentResolution last_investment_resolution() const{return resolution_;}
    ObservationResult observe(const Frame& frame,const ActualPurchase* actual_purchase=nullptr) {
        ObservationResult out;
        if(!detail::valid(frame))return out;
        if(frame.owner!=owner_){out.status=ObserveStatus::WrongOwner;return out;}
        if(observed_) {
            if(frame.round<actual_.round||frame.input<actual_.input
                ||(frame.input==actual_.input&&frame.round!=actual_.round)
                ||(frame.input==actual_.input&&frame.sample<actual_.sample)) {out.status=ObserveStatus::Stale;return out;}
            if(frame.input==actual_.input&&frame.sample==actual_.sample) {
                if(await_actual_){out.status=ObserveStatus::Stale;return out;}
                if(!detail::same(frame,actual_)){out.status=ObserveStatus::ConflictingObservation;return out;}
                if(frame.available_fuel>actual_.available_fuel){out.status=ObserveStatus::Stale;return out;}
                // Queries lower the remaining-fuel ceiling even when they
                // reveal no new physical facts/sample. An old high-fuel copy
                // cannot subsequently manufacture the missing tail budget.
                actual_.available_fuel=frame.available_fuel;out.status=ObserveStatus::Duplicate;return out;
            }
            if(frame.input==actual_.input&&frame.available_fuel>actual_.available_fuel){out.status=ObserveStatus::Stale;return out;}
        }
        out.lease_revoked=lease_valid_;lease_valid_=false;await_actual_=false;actual_=frame;observed_=true;
        out.status=ObserveStatus::Accepted;out.accepted=true;reconcile_investment(frame,out,actual_purchase);return out;
    }
    Investment investment_obligation(const Frame& frame) const {
        auto result=investment_;result.current=fresh(frame);
        result.affordability_known=result.current&&detail::quote_current(frame)&&frame.next_investment.target==result.target;
        result.affordable=result.active&&result.affordability_known&&frame.next_investment.available&&frame.gold>=frame.next_investment.cost;
        return result;
    }
    Grant grant(const Frame& frame,const Program& program) {
        Grant out;const auto deny=[&](GrantStatus status){out.status=status;return out;};
        if(frame.owner!=owner_)return deny(GrantStatus::WrongOwner);
        if(await_actual_)return deny(GrantStatus::AwaitActual);
        if(!fresh(frame))return deny(GrantStatus::NeedObservation);
        if(!detail::same(program.source,frame))return deny(GrantStatus::ChangedDependencies);
        if(static_cast<unsigned>(program.kind)>static_cast<unsigned>(Kind::Recovery)
            ||static_cast<unsigned>(program.action.kind)>static_cast<unsigned>(FirstKind::ActualClosedInput)
            ||static_cast<unsigned>(program.risk.replies)>static_cast<unsigned>(Knowledge::Modeled)
            ||static_cast<unsigned>(program.risk.services)>static_cast<unsigned>(Knowledge::Modeled)
            ||program.action.kind==FirstKind::None||!program.action.action_id||program.risk.horizon_round<frame.round)return deny(GrantStatus::Invalid);
        if((program.risk.replies==Knowledge::Modeled&&!program.risk.reply_model_revision)
            ||(program.risk.services==Knowledge::Modeled&&!program.risk.service_model_revision))return deny(GrantStatus::Invalid);
        if(!program.funded)return deny(GrantStatus::Unfunded);
        if(detail::camp_kind(program.kind)&&(frame.selected_group<0||!frame.source_life||!frame.source_revision))return deny(GrantStatus::MissingSource);
        if(program.action.required_permissions&~all_permissions)return deny(GrantStatus::Invalid);
        if((frame.permissions&program.action.required_permissions)!=program.action.required_permissions)return deny(GrantStatus::PermissionChanged);
        unsigned minimum=program.action.mandatory_fuel;
        if(program.action.kind==FirstKind::FundedPreparation) {
            if(!program.action.preparation_fuel)return deny(GrantStatus::Unfunded);
            if(program.action.preparation_fuel>minimum)minimum=program.action.preparation_fuel;
            out.status=GrantStatus::FundedPreparation;
        } else {
            if(!program.complete_interpreter)return deny(GrantStatus::MissingInterpreter);
            if(program.action.kind==FirstKind::ActualClosedInput) {
                if(!detail::closed(frame))return deny(GrantStatus::Invalid);
                out.status=GrantStatus::ActualClosedInput;
            } else {
                const bool buy_word=program.kind==Kind::Investment&&program.action.required_permissions==Buy;
                if((!frame.alive||frame.stunned||frame.channel!=Channel::None)&&!buy_word)return deny(GrantStatus::PermissionChanged);
                if(frame.completed_turn)return deny(GrantStatus::PermissionChanged);
                if(!minimum)return deny(GrantStatus::Unfunded);
                if(program.kind==Kind::Investment&&!investment_obligation(frame).affordable)return deny(GrantStatus::MissingInvestment);
                out.status=GrantStatus::CompleteIncumbent;
            }
        }
        if(frame.available_fuel<minimum)return deny(GrantStatus::NeedFuel);
        lease_={++serial_,generation_,program,minimum};lease_valid_=true;out.lease=lease_;return out;
    }
    bool matches(const Lease& lease,const Frame& frame) const {
        return lease_valid_&&fresh(frame)&&lease.serial==lease_.serial&&lease.primitive_generation==generation_
            &&lease.minimum_fuel==lease_.minimum_fuel&&detail::same(lease.program,lease_.program)
            &&detail::same(lease.program.source,frame)&&frame.available_fuel>=lease.minimum_fuel;
    }
    // Call BEFORE the actual callback/command, including refusal, suspension,
    // exception and partial effects. This does NOT spend M/A/C or predict any
    // outcome. Only fresh explicit observe(sample++) can rebind actual MAC.
    bool before_primitive(const Lease& lease,const Frame& frame) {
        if(!matches(lease,frame))return false;
        ++generation_;lease_valid_=false;await_actual_=true;actual_.fresh=false;return true;
    }
    bool record_failure(const Failure& f) {
        if(!f.key.valid||!fresh(f.key.source))return false;
        if(f.key.reason==FailureReason::NeedFuel&&f.key.source.available_fuel>=f.key.target.required_fuel)return false;
        for(auto& old:failures_)if(detail::same_failure(old.key,f.key)){old=f;return true;}
        failures_[failure_cursor_]=f;failure_cursor_=(failure_cursor_+1)%failure_capacity;return true;
    }
    bool blocked(const FailureKey& current) const {
        if(!current.valid||!fresh(current.source))return false;
        for(const auto& old:failures_)if(detail::same_failure(old.key,current))return true;return false;
    }
    std::size_t invalidate_changed(const FailureKey& current) {
        if(!current.valid||!fresh(current.source))return 0;std::size_t changed=0;
        for(auto& old:failures_) {
            if(!old.key.valid||old.key.reason!=current.reason||old.key.source.owner!=current.source.owner)continue;
            const bool same_scope=current.reason==FailureReason::UnsupportedFamily
                ?old.key.source.source_family==current.source.source_family
                :detail::same_target(old.key.target,current.target);
            if(same_scope&&invalidated(old.key,current)){old={};++changed;}
        }
        return changed;
    }
};
} // namespace wb::capital::service_program
