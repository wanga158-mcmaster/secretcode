#pragma once
// Planned CAMP-only current->quiet suffix. Unlike return_budget::Trace, these
// edges are nominations justified by current observed traversability, never a
// claim that the actor has already walked them in the opposite direction.
#include "return_budget.h"

namespace mosaic::return_route {
using return_budget::Point;
using return_budget::Damage;
using return_budget::Cost;
using return_budget::State;
inline constexpr auto cost_limit=return_budget::cost_limit;

struct Node { Point point{}; Damage damage{}; bool observed_open{true}; };
enum class Reason {
    ok, empty, capacity, not_ordinary_step, invalid_damage, terminal_not_quiet,
    blocked, invalid_state, origin_mismatch, wrong_endpoint, needs_refresh,
    refusal_budget
};
struct Quote {
    bool valid{},safe{};
    Cost worst_loss{},required_hp{};
    Point next{};
    std::size_t steps{};
    unsigned max_phases{};
    bool shield_credited{};
    Reason reason{Reason::empty};
};

class Route {
public:
    static constexpr std::size_t capacity=32; // current plus at most31 edges
    bool valid()const{return head_<count_;}
    bool fresh()const{return valid()&&fresh_;}
    bool at(Point p)const{return valid()&&node(0).point==p;}
    std::size_t size()const{return valid()?count_-head_:0;}
    const Node& node(std::size_t i)const{return nodes_[head_+i];}
    bool refusal_used()const{return refused_;}
    bool pending_refusal_wait()const{return pending_wait_;}
    std::uint64_t frame()const{return frame_;}

    // Invalidation/rerouting does NOT reopen the exposed episode's refusal
    // budget or erase a still-pending wait. Only actual quiet arrival may do so.
    void clear(){head_=count_=0;fresh_=false;}
    bool reset_at_quiet(Point actual,Damage damage,std::uint64_t frame=0){
        if(frame<frame_){clear();return false;}
        advance_frame(frame);
        if(!damage.valid||damage.amount!=0){clear();return false;}
        nodes_[0]={actual,damage,true};head_=0;count_=1;frame_=frame;
        refused_=pending_wait_=false;fresh_=true;return true;
    }

    // All destination cells must be observed legal in this frame; this does not
    // assert that another actor will leave them free in future frames. Caller
    // must refresh before every continuation. Unknown blockers are conditional
    // on this episode's at-most-one actual refusal premise.
    Reason assign(std::span<const Node> route,std::uint64_t frame=0){
        if(frame<frame_){clear();return Reason::invalid_state;}
        advance_frame(frame);
        if(route.empty()){clear();return Reason::empty;}
        if(route.size()>capacity){clear();return Reason::capacity;}
        for(std::size_t i=0;i<route.size();++i){
            if(!route[i].damage.valid||route[i].damage.amount>cost_limit){clear();return Reason::invalid_damage;}
            if(i&&(!route[i].observed_open)){clear();return Reason::blocked;}
            if(i&&return_budget::distance(route[i-1].point,route[i].point)!=1){clear();return Reason::not_ordinary_step;}
        }
        if(route.back().damage.amount!=0){clear();return Reason::terminal_not_quiet;}
        std::copy(route.begin(),route.end(),nodes_.begin());head_=0;count_=route.size();fresh_=true;
        return Reason::ok;
    }

    // Supply the REMAINING suffix, including its actual current origin. Any
    // blocked future cell or nonquiet terminal invalidates the entire plan.
    // A same-frame observation refresh cannot erase the refusal's pending camp
    // phase. A strictly newer ready owner frame has already paid that phase in
    // its actual HP/shield; only then is the pending wait cleared.
    Reason refresh_damage(std::span<const Damage> damage,std::span<const bool> open,
                          std::uint64_t frame=0){
        if(frame<frame_){clear();return Reason::invalid_state;}
        advance_frame(frame);
        if(!valid())return Reason::empty;
        if(damage.size()!=size()||open.size()!=size()){clear();return Reason::invalid_state;}
        for(std::size_t i=0;i<size();++i){
            if(!damage[i].valid||damage[i].amount>cost_limit){clear();return Reason::invalid_damage;}
            if(i&&!open[i]){clear();return Reason::blocked;}
        }
        if(damage.back().amount!=0){clear();return Reason::terminal_not_quiet;}
        for(std::size_t i=0;i<size();++i){nodes_[head_+i].damage=damage[i];nodes_[head_+i].observed_open=open[i];}
        fresh_=true;return Reason::ok;
    }

    Reason observe_step(Point from,Point to,bool applied){
        if(!valid())return Reason::empty;
        if(!at(from)){clear();return Reason::origin_mismatch;}
        if(size()<2||to!=node(1).point){clear();return Reason::wrong_endpoint;}
        if(!fresh_)return Reason::needs_refresh;
        if(!applied)return observe_refusal(from);
        fresh_=false; // success AND false require fresh body/occupancy/support
        ++head_;return Reason::ok;
    }

    // An ACTUAL false outward/work move need not nominate the next return
    // endpoint. It still consumes this exposed episode's sole refusal and
    // leaves the planned return origin/geometry untouched. This method is not
    // an admission for attempting an arbitrary command; the adapter must have
    // priced its success AND false-at-origin continuations before dispatch.
    Reason observe_refusal(Point actual){
        if(!valid())return Reason::empty;
        if(!at(actual)){clear();return Reason::origin_mismatch;}
        if(!fresh_)return Reason::needs_refresh;
        fresh_=false;
        if(refused_){clear();return Reason::refusal_budget;}
        refused_=true;pending_wait_=true;return Reason::ok;
    }

    Quote quote(const State& state)const{
        if(!valid())return invalid(Reason::empty);
        if(!fresh_)return invalid(Reason::needs_refresh);
        if(!std::isfinite(state.health)||!std::isfinite(state.shield)||state.health<0||
           state.shield<0||state.health>double(cost_limit)||state.shield>double(cost_limit)||
           state.shield_phases<0||state.additional_forced_waits>return_budget::max_waits||
           state.compute_waits>return_budget::max_waits)return invalid(Reason::invalid_state);
        const unsigned waits=state.additional_forced_waits+(state.can_move_now?0u:1u)+(pending_wait_?1u:0u);
        const bool refusal=state.allow_one_refusal&&!refused_;
        Cost total=Cost(waits)*node(0).damage.amount,max_origin=0;
        for(std::size_t i=1;i<size();++i){
            total+=node(i).damage.amount;max_origin=std::max(max_origin,node(i-1).damage.amount);
        }
        Cost worst=total;
        if(state.compute_waits){
            // Extra waits may occur anywhere and change shield expiry: omit
            // shield credit, then price each wait at the largest route origin.
            worst+=Cost(state.compute_waits)*max_origin;
            if(refusal)worst+=max_origin;
        }else{
            const Cost shield=Cost(std::floor(state.shield));
            const auto phases=unsigned(state.shield_phases);
            const Cost covered=prefix_damage(phases,waits);
            const Cost before_last=phases?prefix_damage(phases-1,waits):0;
            worst-=std::min(shield,covered);
            if(refusal){
                unsigned slot=waits;
                for(std::size_t i=0;i+1<size();++i,++slot){
                    const Cost hit=node(i).damage.amount;
                    const Cost protected_damage=slot<phases?before_last+hit:covered;
                    worst=std::max(worst,total+hit-std::min(shield,protected_damage));
                }
            }
        }
        Quote result;result.valid=true;result.reason=Reason::ok;
        result.worst_loss=worst;result.required_hp=worst+1;
        result.safe=Cost(std::floor(state.health))>=result.required_hp;
        result.steps=size()-1;result.next=size()>1?node(1).point:node(0).point;
        result.max_phases=waits+unsigned(result.steps)+state.compute_waits+(refusal&&result.steps?1u:0u);
        result.shield_credited=state.compute_waits==0&&state.shield>0&&state.shield_phases>0;
        return result;
    }

private:
    std::array<Node,capacity> nodes_{};
    std::size_t head_{},count_{};
    std::uint64_t frame_{};
    bool fresh_{},refused_{},pending_wait_{};
    void advance_frame(std::uint64_t frame){if(frame>frame_)pending_wait_=false;frame_=frame;}
    static Quote invalid(Reason reason){Quote q;q.reason=reason;return q;}
    Cost prefix_damage(unsigned phases,unsigned waits)const{
        const unsigned held=std::min(phases,waits);
        Cost total=Cost(held)*node(0).damage.amount;phases-=held;
        for(std::size_t i=1;i<size()&&phases;++i,--phases)total+=node(i).damage.amount;
        return total;
    }
};

// Compare COMPLETE nominations under the actual requested input phase. An
// affordable refreshed incumbent does not justify suppressing a cheaper route,
// and geometric validity alone does not justify keeping an unaffordable one.
// A deferred/incomplete alternative never reaches this replacement operation.
// Damage on BOTH plans must be supplied as a bound over damage_horizon phases;
// the candidate's quoted route/waits must fit that horizon before adoption.
// Candidate search must start by copying the incumbent episode: neither a new
// unrefused object nor an artificially "already refused" candidate may alter
// the exposed actor's physical history to manufacture a better price.
inline bool consider(Route& incumbent,const Route& candidate,const State& state,
                     unsigned damage_horizon){
    if(!candidate.valid()||!candidate.fresh()||candidate.frame()<incumbent.frame())return false;
    if(incumbent.valid()&&!candidate.at(incumbent.node(0).point))return false;
    if(candidate.refusal_used()!=incumbent.refusal_used())return false;
    const bool expected_pending=candidate.frame()==incumbent.frame()&&incumbent.pending_refusal_wait();
    if(candidate.pending_refusal_wait()!=expected_pending)return false;
    const auto alternative=candidate.quote(state);
    if(!alternative.valid||alternative.max_phases>damage_horizon)return false;
    const auto old=incumbent.quote(state);
    const bool old_current=incumbent.frame()==candidate.frame()&&old.valid&&old.max_phases<=damage_horizon;
    if(!old_current||(alternative.safe&&!old.safe)||alternative.required_hp<old.required_hp){
        incumbent=candidate;return true;
    }
    return false;
}
}
