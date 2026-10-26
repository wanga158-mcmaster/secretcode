#pragma once
// Input-local resource contract, not a native cost theorem or game budget.
// Costs are caller-supplied conservative bounds; native execution must qualify
// them. A paid prefix never pays for missing feedback or stale physical truth.
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace mosaic::return_funding {
using Fuel=std::uint64_t;
using Revision=std::uint64_t;
inline constexpr Fuel limit=std::numeric_limits<Fuel>::max()/4;
enum class Phase {action,feedback,reprice,physical,done};
enum class Reason {ok,unknown_cost,overflow,unfunded,stale_token,wrong_phase,
    missing_witness,postcondition,overrun,invalid_fuel};
struct Token {Revision revision{},frame{},effect{},observation{};};
inline bool same(const Token& a,const Token& b){
    return a.revision==b.revision&&a.frame==b.frame&&a.effect==b.effect&&a.observation==b.observation;
}
struct Cost {Fuel amount{};bool known{};};
struct Bounds {
    Fuel action{},feedback{},reprice{},physical{};
    bool known{},physical_required{true};
};
struct Witness {
    bool actual_attempt{},applied{},actual_feedback{},actual_reprice{},failure_closed{};
    // These are read from actual fresh body/channel feedback, not a predicted
    // primitive transition. A no-physical tail needs spent M or active channel.
    bool movement_ready{},movement_spent{},channel_active{};
    // Only reprice may assert an ACTUALLY refreshed quiet return endpoint.
    // This completes the obligation without fabricating a physical attempt.
    bool return_complete{};
};
inline bool positive(Fuel value){return value>0&&value<=limit;}
inline bool checked_add(Fuel a,Fuel b,Fuel& result){
    if(a>limit||b>limit||a>limit-b)return false;result=a+b;return true;
}
inline bool checked_multiply(Fuel a,Fuel b,Fuel& result){
    if(a>limit||b>limit||(b&&a>limit/b))return false;result=a*b;return true;
}
// base + sources*(per_source + phase_sum*per_source_phase), with no wrapped
// source/phase-dependent evaluation quote. Zero coefficients are permitted.
inline Cost affine_bound(Fuel base,Fuel per_source,Fuel per_source_phase,Fuel sources,Fuel phase_sum){
    Fuel phase{},unit{},all{},total{};
    if(!checked_multiply(phase_sum,per_source_phase,phase)||!checked_add(per_source,phase,unit)||
       !checked_multiply(sources,unit,all)||!checked_add(base,all,total)||!positive(total))return {};
    return {total,true};
}
inline Cost remaining(const Bounds& bounds,Phase from=Phase::action){
    if(!bounds.known||!positive(bounds.action)||!positive(bounds.feedback)||!positive(bounds.reprice)||
       (bounds.physical_required?!positive(bounds.physical):bounds.physical!=0))return {};
    const auto first=std::size_t(from);
    if(first>std::size_t(Phase::done))return {};
    const std::array<Fuel,4> costs{bounds.action,bounds.feedback,bounds.reprice,bounds.physical};
    Fuel total=0;
    for(std::size_t i=first;i<costs.size();++i)if(!checked_add(total,costs[i],total))return {};
    return {total,true};
}
inline bool funded(Fuel fuel,Cost cost){return fuel<=limit&&cost.known&&positive(cost.amount)&&fuel>cost.amount;}

class Tail {
public:
    Reason start(Token token,const Bounds& bounds,Fuel fuel){
        valid_=false;inflight_=false;fallback_=true;next_=Phase::action;
        bounds_=bounds;token_=token;last_fuel_=fuel;failure_pending_=false;terminal_confirmed_=false;
        const auto whole=return_funding::remaining(bounds_);
        if(!whole.known){
            const bool declared=bounds.known&&positive(bounds.action)&&positive(bounds.feedback)&&
                positive(bounds.reprice)&&(bounds.physical_required?positive(bounds.physical):bounds.physical==0);
            return reason_=declared?Reason::overflow:Reason::unknown_cost;
        }
        if(fuel>limit)return reason_=Reason::invalid_fuel;
        if(!funded(fuel,whole))return reason_=Reason::unfunded;
        valid_=true;fallback_=false;return reason_=Reason::ok;
    }
    bool begin(Phase phase,Token token,Fuel fuel){
        if(!valid_||inflight_||phase!=next_||phase==Phase::done){revoke(Reason::wrong_phase);return false;}
        if(!same(token,token_)){revoke(Reason::stale_token);return false;}
        if(fuel>last_fuel_||fuel>limit){revoke(Reason::invalid_fuel);return false;}
        if(!funded(fuel,return_funding::remaining(bounds_,next_))){revoke(Reason::unfunded);return false;}
        before_=fuel;inflight_=true;return true;
    }
    Reason finish(Phase phase,Token after,Fuel fuel,const Witness& witness){
        if(!valid_||!inflight_||phase!=next_||phase==Phase::done)return revoke(Reason::wrong_phase);
        if(after.frame!=token_.frame||after.revision!=token_.revision||
           after.effect<token_.effect||after.observation<token_.observation)return revoke(Reason::stale_token);
        if(fuel>before_||fuel>limit)return revoke(Reason::invalid_fuel);
        if(witness.return_complete&&phase!=Phase::reprice)return revoke(Reason::missing_witness);
        const Fuel bound=phase==Phase::action?bounds_.action:phase==Phase::feedback?bounds_.feedback:
            phase==Phase::reprice?bounds_.reprice:bounds_.physical;
        if(before_-fuel>bound)return revoke(Reason::overrun);
        if(phase==Phase::action||phase==Phase::physical){
            if(!witness.actual_attempt)return revoke(Reason::missing_witness);
            if(witness.applied?(after.effect<=token_.effect):(after.effect!=token_.effect))
                return revoke(Reason::stale_token);
            if(after.observation!=token_.observation&&!witness.actual_feedback)
                return revoke(Reason::missing_witness);
            if(phase==Phase::action)failure_pending_=!witness.applied;
            else if(!witness.applied&&(!witness.actual_feedback||!witness.failure_closed||
                    after.observation<=token_.observation))return revoke(Reason::missing_witness);
        }else if(phase==Phase::feedback){
            if(!witness.actual_feedback||after.observation<=token_.observation||after.effect!=token_.effect||
               (failure_pending_&&!witness.failure_closed))return revoke(Reason::missing_witness);
            if((witness.movement_ready&&witness.movement_spent)||
               (bounds_.physical_required&&witness.channel_active))return revoke(Reason::postcondition);
            if(bounds_.physical_required?!witness.movement_ready:
               !(witness.movement_spent||witness.channel_active))return revoke(Reason::postcondition);
            failure_pending_=false;terminal_confirmed_=!bounds_.physical_required;
        }else if(!witness.actual_reprice||!same(after,token_))return revoke(Reason::missing_witness);
        token_=after;last_fuel_=fuel;inflight_=false;
        next_=phase==Phase::action?Phase::feedback:phase==Phase::feedback?Phase::reprice:
            phase==Phase::reprice?(bounds_.physical_required&&!witness.return_complete?Phase::physical:Phase::done):Phase::done;
        if(next_==Phase::done&&!bounds_.physical_required&&!terminal_confirmed_)return revoke(Reason::postcondition);
        const auto tail=return_funding::remaining(bounds_,next_);
        if(!tail.known||fuel<=tail.amount)return revoke(Reason::unfunded);
        return reason_=Reason::ok;
    }
    bool optional_fits(Token token,Fuel fuel,Fuel optional_cost)const{
        if(!valid_||inflight_||next_==Phase::done||!same(token,token_)||fuel>last_fuel_||
           fuel>limit||!positive(optional_cost))return false;
        const auto tail=return_funding::remaining(bounds_,next_);Fuel whole{};
        return tail.known&&checked_add(optional_cost,tail.amount,whole)&&fuel>whole;
    }
    Fuel remaining()const{
        if(done())return 0;
        const auto tail=return_funding::remaining(bounds_,next_);return tail.known?tail.amount:limit;
    }
    Phase next()const{return next_;}
    // Read-only telemetry of the already captured interval; no new timing query.
    bool inflight()const{return inflight_;}
    Fuel phase_start_fuel()const{return before_;}
    Fuel phase_bound(Phase phase)const{
        return phase==Phase::action?bounds_.action:phase==Phase::feedback?bounds_.feedback:
            phase==Phase::reprice?bounds_.reprice:phase==Phase::physical?bounds_.physical:0;
    }
    bool valid()const{return valid_;}
    bool done()const{return valid_&&next_==Phase::done;}
    bool fallback_required()const{return fallback_;}
    Reason reason()const{return reason_;}
    const Token& token()const{return token_;}
private:
    Bounds bounds_{};Token token_{};
    Phase next_{Phase::action};Reason reason_{Reason::unknown_cost};
    Fuel before_{},last_fuel_{};
    bool valid_{},inflight_{},fallback_{},failure_pending_{},terminal_confirmed_{};
    Reason revoke(Reason reason){valid_=false;inflight_=false;fallback_=true;return reason_=reason;}
};
}
