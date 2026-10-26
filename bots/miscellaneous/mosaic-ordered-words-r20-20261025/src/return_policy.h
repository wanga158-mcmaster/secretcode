#pragma once
// Bounded CAMP-only complete-word/return model. Inputs are adapter premises,
// not SDK authority. A quote never confirms that a predicted effect occurred.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace mosaic::return_policy {
inline constexpr std::size_t source_capacity=64,phase_capacity=8,
    primitive_capacity=4,scenario_capacity=5,candidate_capacity=8;
using Revision=std::uint64_t;
using Mask=std::uint64_t;
using PhaseMask=std::uint8_t;
inline constexpr double amount_limit=281474976710656.0; // exact 2^48

enum class Action {basic,cast,move,channel};
enum class Purpose {work_exit,return_exit,observation,model_escape};
enum class Assurance {none,unsupported,estimated,conditional_bound};
enum class Reason {
    ok,capacity,stale_frame,invalid_amount,invalid_source,invalid_word,
    unavailable_primitive,invalid_scenario,missing_failure_case,refusal_budget,
    incomplete_exit,nonprogressing_observation,missing_coverage
};

struct Source {
    // group is an adapter-assigned ordinal in [0,63]. A hit on a known group
    // wakes ALL its living members, including undamaged siblings.
    unsigned group{};
    bool group_known{true},fresh{},alive{true},health_known{},exact_health{},asleep{};
    double health{};
    PhaseMask stun_phases{};
};
struct Shield {
    double amount{};
    // Explicit chronological CAMP batches, bit0 = next camp batch. This is
    // NOT the camp-stun expiry rule or an owner-timer conversion.
    PhaseMask active_phases{};
};
struct Frame {
    Revision frame{},observation{};
    double health{};
    bool can_move{},can_attack{},can_cast{};
    Shield shield{};
    std::size_t source_count{};
    std::array<Source,source_capacity> sources{};
    bool coverage_complete{},no_exogenous_wake{},refusal_used{};
};
struct Primitive {
    Action action{Action::basic};
    bool observed_legal{},may_refuse{};
    // Basic always spends A, Cast C, Move M. These EXTRA transitions encode
    // actual champion compatibility: ranged Basic/Move and walking-excluding
    // spells; reset_attack applies after the primitive consumes its allowances.
    bool consumes_move{},consumes_attack{},resets_attack{};
    std::array<double,source_capacity> damage{};
    // The adapter must also enumerate potential OFF-FRAME splash/AoE hits.
    // Those hits may wake a represented sibling without damaging any source
    // in damage[]. Bits use the same [0,63] group ordinals as Source.group.
    Mask wakes_groups{};
    // Include every actual splash target explicitly; a primary kill does not
    // erase collateral group wake. Estimated effects never kill in upper state.
    bool exact_damage{},exact_control{};
    std::array<PhaseMask,source_capacity> stun_phases{};
    bool replaces_shield{};
    Shield shield_after{};
};
struct Phase {
    // Incoming per-source bounds at this scenario's ACTUAL nominated position.
    // Refused Move stays at origin; future positions/expiry shift explicitly.
    std::array<double,source_capacity> incoming{};
    bool bounds_valid{},observed_open{};
    bool channel_interruption{};
};
struct Scenario {
    // Success applies the whole word. A refusal of primitive i applies ONLY
    // [0,i), and requires a complete separately priced origin/fallback suffix.
    unsigned applied_prefix{};
    bool refused{};
    unsigned refused_primitive{},additional_refusals{};
    std::size_t phase_count{};
    std::array<Phase,phase_capacity> phases{};
    bool route_complete{},future_failures_bounded{};
    // model_escape is an explicit finite-horizon assumption, never a quiet
    // endpoint or a conservative certificate manufactured by the evaluator.
    bool declared_escape_postcondition{};
    // Recall needs an uninterrupted channel, not merely survival. We require
    // zero raw post-effect incoming damage BEFORE shield absorption, plus no
    // other supplied interruption; no uncertain shield/channel rule is used.
    bool requires_no_damage{};
    unsigned owner_waits{};
};
struct Observation {
    Revision intent{},previous_intent{};
    unsigned completed_before{},completed_after{},total{};
    bool relevant{},actually_advanced{};
};
struct Candidate {
    Revision frame{},observation{};
    Purpose purpose{Purpose::return_exit};
    std::size_t primitive_count{};
    std::array<Primitive,primitive_capacity> word{};
    std::size_t scenario_count{};
    std::array<Scenario,scenario_capacity> scenarios{};
    Observation preparation{};
    unsigned useful_service{};
};
struct Quote {
    // Borrowed immutable subject: changing/reusing Candidate storage revokes
    // this quote, even if its address and frame/observation remain unchanged.
    const Candidate* subject{};
    Revision frame{},observation{};
    bool valid{},supported{},upper_alive{},estimated_alive{};
    bool upper_complete{},estimated_complete{},requires_post_effect_refresh{};
    double upper_loss{},estimated_loss{},useful_damage{};
    unsigned max_phases{},owner_waits{},successful_primitives{},useful_service{};
    Assurance assurance{Assurance::none};
    Reason reason{Reason::invalid_scenario};
};
struct Receipt {
    Revision frame{},observation{};
    unsigned completed_prefix{};
    bool actual_effects_match{},actual_allowances_match{},route_refreshed{},failure_closed{};
};

// evaluate/choose are pure bounded model operations. The adapter owns static
// Frame/Candidate storage; do not materialize the large arrays on a turn stack.
struct Selection {std::size_t index{std::numeric_limits<std::size_t>::max()};Quote quote{};};
namespace detail {
inline bool amount(double value){return std::isfinite(value)&&value>=0&&value<=amount_limit;}
inline Quote invalid(Reason reason){Quote q;q.reason=reason;return q;}
// Error-free TwoSum residual chooses the outward adjacent double only when
// normal addition actually rounded the mathematical result the wrong way.
// Integer rule examples stay exact; fractional sums cannot certify HP==damage
// survival because ten0.1 terms happened to round below one.
inline double residual(double a,double b,double sum){
    const double virtual_b=sum-a;
    return (a-(sum-virtual_b))+(b-virtual_b);
}
inline double add_up(double a,double b){
    const double sum=a+b;
    return residual(a,b,sum)>0?std::nextafter(sum,std::numeric_limits<double>::infinity()):sum;
}
inline double subtract_up(double a,double b){return add_up(a,-b);}
inline double subtract_down(double a,double b){
    const double sum=a-b;
    return residual(a,-b,sum)<0?std::nextafter(sum,-std::numeric_limits<double>::infinity()):sum;
}
struct Allowances {bool move{},attack{},cast{};};
inline bool apply_allowances(Allowances& state,const Primitive& primitive){
    if(!primitive.observed_legal)return false;
    switch(primitive.action){
    case Action::basic:if(!state.attack)return false;state.attack=false;break;
    case Action::cast:if(!state.cast)return false;state.cast=false;break;
    case Action::move:if(!state.move)return false;state.move=false;break;
    case Action::channel:break;
    }
    if(primitive.consumes_move)state.move=false;
    if(primitive.consumes_attack)state.attack=false;
    if(primitive.resets_attack)state.attack=true;
    return true;
}
struct Track {
    std::array<double,source_capacity> health{};
    std::array<bool,source_capacity> alive{},killable{},awake{};
    std::array<PhaseMask,source_capacity> stunned{};
    Shield shield{};
};
inline Track initial(const Frame& frame,bool upper){
    Track state;state.shield=frame.shield;
    for(std::size_t i=0;i<frame.source_count;++i){
        const auto& source=frame.sources[i];
        state.alive[i]=!source.fresh||source.alive;
        state.health[i]=source.health;
        state.killable[i]=source.fresh&&source.health_known&&(!upper||source.exact_health);
        state.awake[i]=upper||!frame.no_exogenous_wake||!source.fresh||!source.asleep;
        state.stunned[i]=source.fresh?source.stun_phases:PhaseMask{};
    }
    return state;
}
inline double apply_effects(Track& state,const Frame& frame,const Primitive& primitive,bool upper){
    Mask groups=primitive.wakes_groups;bool unknown_group_hit=false;
    for(std::size_t i=0;i<frame.source_count;++i){
        if(!state.alive[i]||primitive.damage[i]<=0)continue;
        if(frame.sources[i].group_known)groups|=Mask(1)<<frame.sources[i].group;
        else unknown_group_hit=true;
    }
    for(std::size_t i=0;i<frame.source_count;++i)
        if(unknown_group_hit||(groups&&(!frame.sources[i].group_known||
           (groups&(Mask(1)<<frame.sources[i].group)))))state.awake[i]=true;
    double useful=0;
    for(std::size_t i=0;i<frame.source_count;++i){
        if(state.alive[i]&&primitive.damage[i]>0){
            useful+=state.killable[i]?std::min(state.health[i],primitive.damage[i]):primitive.damage[i];
            if(state.killable[i]&&(!upper||primitive.exact_damage)){
                state.health[i]=primitive.damage[i]>=state.health[i]?0:
                    detail::subtract_up(state.health[i],primitive.damage[i]);
                if(state.health[i]==0)state.alive[i]=false;
            }
        }
        if(!upper||(primitive.exact_control&&frame.sources[i].fresh))
            state.stunned[i]|=primitive.stun_phases[i];
    }
    if(primitive.replaces_shield)
        state.shield=(!upper||primitive.exact_control)?primitive.shield_after:Shield{};
    return useful;
}
inline double incoming(const Frame& frame,const Track& state,const Phase& phase,
                       unsigned index,bool ignore_temporary_control=false){
    double total=0;
    for(std::size_t i=0;i<frame.source_count;++i)
        if(state.alive[i]&&state.awake[i]&&
           (ignore_temporary_control||!(state.stunned[i]&(PhaseMask(1)<<index))))
            total=add_up(total,phase.incoming[i]);
    return total;
}
inline double lose(Track& state,double incoming,unsigned phase){
    const double absorbed=(state.shield.active_phases&(PhaseMask(1)<<phase))?
        std::min(state.shield.amount,incoming):0;
    state.shield.amount=std::max(0.0,subtract_down(state.shield.amount,absorbed));
    return std::max(0.0,subtract_up(incoming,absorbed));
}
inline bool observation_progress(const Observation& observation){
    return observation.relevant&&observation.actually_advanced&&
        observation.intent==observation.previous_intent&&observation.total>0&&
        observation.total<=32&&observation.completed_before<observation.completed_after&&
        observation.completed_after<=observation.total;
}
}

inline Quote evaluate(const Frame& frame,const Candidate& candidate){
    if(frame.source_count>source_capacity||candidate.primitive_count>primitive_capacity||
       candidate.scenario_count==0||candidate.scenario_count>scenario_capacity)
        return detail::invalid(Reason::capacity);
    if(candidate.frame!=frame.frame||candidate.observation!=frame.observation)
        return detail::invalid(Reason::stale_frame);
    if(!detail::amount(frame.health)||frame.health<=0||!detail::amount(frame.shield.amount))
        return detail::invalid(Reason::invalid_amount);
    for(std::size_t i=0;i<frame.source_count;++i){
        const auto& source=frame.sources[i];
        if((source.group_known&&source.group>=source_capacity)||!detail::amount(source.health)||
           (source.fresh&&source.alive&&source.health_known&&source.health<=0))
            return detail::invalid(Reason::invalid_source);
    }
    if(candidate.purpose==Purpose::observation&&!detail::observation_progress(candidate.preparation))
        return detail::invalid(Reason::nonprogressing_observation);
    detail::Allowances allowances{frame.can_move,frame.can_attack,frame.can_cast};
    unsigned required_failures=0;bool channel=false;
    for(std::size_t p=0;p<candidate.primitive_count;++p){
        const auto& primitive=candidate.word[p];
        if(channel||(primitive.may_refuse&&primitive.action!=Action::move&&primitive.action!=Action::cast)||
           (primitive.resets_attack&&primitive.action!=Action::cast))
            return detail::invalid(Reason::invalid_word);
        if(!detail::apply_allowances(allowances,primitive))
            return detail::invalid(Reason::unavailable_primitive);
        if(primitive.may_refuse)required_failures|=1u<<p;
        channel=primitive.action==Action::channel;
        if(!detail::amount(primitive.shield_after.amount))return detail::invalid(Reason::invalid_amount);
        if(((primitive.action==Action::move||primitive.action==Action::channel)&&
            (primitive.replaces_shield||primitive.wakes_groups))||
           (channel&&(primitive.consumes_move||primitive.consumes_attack||primitive.resets_attack)))
            return detail::invalid(Reason::invalid_word);
        for(std::size_t i=0;i<frame.source_count;++i){
            if(!detail::amount(primitive.damage[i]))return detail::invalid(Reason::invalid_amount);
            if((primitive.action==Action::move||primitive.action==Action::channel)&&
               (primitive.damage[i]>0||primitive.stun_phases[i]||primitive.replaces_shield))
                return detail::invalid(Reason::invalid_word);
        }
    }
    Quote quote;quote.subject=&candidate;quote.frame=candidate.frame;quote.observation=candidate.observation;
    quote.valid=true;quote.reason=Reason::ok;quote.supported=frame.coverage_complete;
    quote.upper_alive=quote.estimated_alive=quote.upper_complete=quote.estimated_complete=true;
    quote.successful_primitives=unsigned(candidate.primitive_count);quote.useful_service=candidate.useful_service;
    quote.requires_post_effect_refresh=candidate.primitive_count>0;
    unsigned failures=0,successes=0;
    for(std::size_t n=0;n<candidate.scenario_count;++n){
        const auto& scenario=candidate.scenarios[n];
        if(scenario.applied_prefix>candidate.primitive_count||scenario.phase_count==0||
           scenario.phase_count>phase_capacity)return detail::invalid(Reason::invalid_scenario);
        if(!scenario.route_complete)return detail::invalid(Reason::incomplete_exit);
        if(scenario.additional_refusals>1||unsigned(frame.refusal_used)+scenario.additional_refusals>1)
            return detail::invalid(Reason::refusal_budget);
        if(scenario.refused){
            const unsigned p=scenario.refused_primitive;
            if(p>=candidate.primitive_count||p!=scenario.applied_prefix||
               !candidate.word[p].may_refuse||scenario.additional_refusals!=1||
               (failures&(1u<<p)))return detail::invalid(Reason::invalid_scenario);
            failures|=1u<<p;
        }else if(scenario.applied_prefix!=candidate.primitive_count||++successes!=1)
            return detail::invalid(Reason::invalid_scenario);
        quote.supported=quote.supported&&scenario.future_failures_bounded;
        auto upper=detail::initial(frame,true),estimate=detail::initial(frame,false);
        double useful=0;
        bool scenario_channel=false;
        for(unsigned p=0;p<scenario.applied_prefix;++p){
            detail::apply_effects(upper,frame,candidate.word[p],true);
            useful+=detail::apply_effects(estimate,frame,candidate.word[p],false);
            scenario_channel|=candidate.word[p].action==Action::channel;
        }
        if(scenario_channel&&!scenario.requires_no_damage)
            return detail::invalid(Reason::invalid_scenario);
        double upper_loss=0,estimated_loss=0;
        bool upper_uninterrupted=true,estimate_uninterrupted=true;
        for(unsigned p=0;p<scenario.phase_count;++p){
            const auto& phase=scenario.phases[p];
            if(!phase.observed_open)return detail::invalid(Reason::invalid_scenario);
            quote.supported=quote.supported&&phase.bounds_valid;
            for(std::size_t i=0;i<frame.source_count;++i)
                if(!detail::amount(phase.incoming[i]))return detail::invalid(Reason::invalid_amount);
            const double upper_hit=detail::incoming(frame,upper,phase,p);
            const double estimate_hit=detail::incoming(frame,estimate,phase,p);
            upper_loss=detail::add_up(upper_loss,detail::lose(upper,upper_hit,p));
            estimated_loss=detail::add_up(estimated_loss,detail::lose(estimate,estimate_hit,p));
            upper_uninterrupted=upper_uninterrupted&&upper_hit==0&&!phase.channel_interruption;
            estimate_uninterrupted=estimate_uninterrupted&&estimate_hit==0&&!phase.channel_interruption;
        }
        const auto& terminal=scenario.phases[scenario.phase_count-1];
        bool upper_complete=detail::incoming(frame,upper,terminal,0,true)==0;
        bool estimated_complete=detail::incoming(frame,estimate,terminal,0,true)==0;
        if(candidate.purpose==Purpose::observation)
            upper_complete=estimated_complete=true; // only the finite priced cursor, never quiet arrival
        if(candidate.purpose==Purpose::model_escape){
            upper_complete=false; // a declared horizon is never an upper certificate
            estimated_complete=scenario.declared_escape_postcondition;
        }
        if(scenario.requires_no_damage){
            upper_complete=upper_complete&&upper_uninterrupted;
            estimated_complete=estimated_complete&&estimate_uninterrupted;
        }
        quote.upper_loss=std::max(quote.upper_loss,upper_loss);
        quote.estimated_loss=std::max(quote.estimated_loss,estimated_loss);
        quote.upper_alive=quote.upper_alive&&frame.health>upper_loss;
        quote.estimated_alive=quote.estimated_alive&&frame.health>estimated_loss;
        quote.upper_complete=quote.upper_complete&&upper_complete;
        quote.estimated_complete=quote.estimated_complete&&estimated_complete;
        quote.max_phases=std::max(quote.max_phases,unsigned(scenario.phase_count));
        quote.owner_waits=std::max(quote.owner_waits,scenario.owner_waits);
        if(!scenario.refused)quote.useful_damage=useful;
    }
    if(successes!=1||failures!=required_failures)return detail::invalid(Reason::missing_failure_case);
    if(!quote.supported){quote.reason=Reason::missing_coverage;quote.assurance=Assurance::unsupported;}
    else if(quote.upper_alive&&quote.upper_complete)quote.assurance=Assurance::conditional_bound;
    else if(quote.estimated_alive&&quote.estimated_complete)quote.assurance=Assurance::estimated;
    return quote;
}

// This gate acknowledges a NEW actual SDK observation of an already completed
// prefix; it never derives receipt truth from evaluate(). Re-evaluate on later
// owner inputs. Refusal likewise needs actual refreshed failure closure. The
// adapter must preserve its chosen allowance for estimates; this freshness
// gate does not independently select or upgrade an assurance mode.
inline bool continuation_ready(const Candidate& candidate,const Quote& quote,const Receipt& receipt){
    return quote.valid&&quote.supported&&quote.subject==&candidate&&
        quote.frame==candidate.frame&&quote.observation==candidate.observation&&
        ((quote.upper_alive&&quote.upper_complete)||(quote.estimated_alive&&quote.estimated_complete))&&
        receipt.completed_prefix>0&&
        receipt.completed_prefix<=candidate.primitive_count&&receipt.frame==candidate.frame&&
        receipt.observation>candidate.observation&&receipt.actual_effects_match&&
        receipt.actual_allowances_match&&receipt.route_refreshed&&receipt.failure_closed;
}

inline Selection choose(const Frame& frame,const Candidate* candidates,std::size_t count,bool allow_estimates){
    Selection result;
    if(!candidates||count>candidate_capacity)return result;
    for(std::size_t i=0;i<count;++i){
        const auto quote=evaluate(frame,candidates[i]);
        const bool admitted=quote.valid&&quote.supported&&
            ((quote.upper_alive&&quote.upper_complete)||
             (allow_estimates&&quote.estimated_alive&&quote.estimated_complete));
        if(!admitted)continue;
        const auto& old=result.quote;
        // Same model utility for all assurance tags. Assurance only breaks a
        // complete utility tie; it cannot veto a useful supported estimate.
        const double loss=allow_estimates?quote.estimated_loss:quote.upper_loss;
        const double old_loss=allow_estimates?old.estimated_loss:old.upper_loss;
        bool better=result.index==std::numeric_limits<std::size_t>::max();
        if(!better){
            if(loss!=old_loss)better=loss<old_loss;
            else if(quote.owner_waits!=old.owner_waits)better=quote.owner_waits<old.owner_waits;
            else if(quote.useful_damage!=old.useful_damage)better=quote.useful_damage>old.useful_damage;
            else if(quote.useful_service!=old.useful_service)better=quote.useful_service>old.useful_service;
            else better=unsigned(quote.assurance)>unsigned(old.assurance);
        }
        if(better){result.index=i;result.quote=quote;}
    }
    return result;
}

// Explicit caller opt-in ONLY after ordinary choose() has no surviving plan.
// This ranks supported complete forecasts, including predicted fatalities. It
// does not change HP floors, upgrade an estimate, confirm future absence, or
// supply SDK legality. The caller must declare the modeled risk and obtain
// actual physical authority before attempting its first primitive.
inline Selection choose_risk(const Frame& frame,const Candidate* candidates,std::size_t count){
    Selection result;
    if(!candidates||count>candidate_capacity)return result;
    for(std::size_t i=0;i<count;++i){
        auto quote=evaluate(frame,candidates[i]);
        if(!quote.valid||!quote.supported||!quote.estimated_complete)continue;
        if(!quote.estimated_alive)quote.assurance=Assurance::none;
        const auto& old=result.quote;
        bool better=result.index==std::numeric_limits<std::size_t>::max();
        if(!better){
            if(quote.estimated_alive!=old.estimated_alive)better=quote.estimated_alive;
            else if(quote.estimated_loss!=old.estimated_loss)better=quote.estimated_loss<old.estimated_loss;
            else if(quote.owner_waits!=old.owner_waits)better=quote.owner_waits<old.owner_waits;
            else if(quote.useful_damage!=old.useful_damage)better=quote.useful_damage>old.useful_damage;
            else if(quote.useful_service!=old.useful_service)better=quote.useful_service>old.useful_service;
            else better=unsigned(quote.assurance)>unsigned(old.assurance);
        }
        if(better){result.index=i;result.quote=quote;}
    }
    return result;
}
}
