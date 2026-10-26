#pragma once
// Conditional finite macro-endpoint model, not an engine transition proof.
#include "../common/model.h"
#include <array>

namespace plan3_lifeline {
inline constexpr unsigned maximum_jobs=28,maximum_states=340;
enum class Status { known,unavailable,invalid,cutoff };
enum class Location { field,altar,spawn,unknown };
enum class Reason { none,not_admitted,unknown_forecast,invalid_input,unsupported,
    deadline,survival_unknown,bag_loss,position_unknown,no_certified_recovery,unsafe_successor };
struct Assumptions {
    // These are analytical assumptions, never observations or calibrated odds.
    bool quiet=false,safeRecall=false;
};
struct Score { std::uint64_t converted=0,occupied=1; };
struct Choice { bool present=false;plan3::JobId job;Score score; };
struct State {
    // Health rounds down. Bag, credited gems and remaining rounds stay exact.
    std::int64_t health=0;
    int bag=0,tile=-1,remaining=0,credited=0;
    Location location=Location::unknown;
    bool safe=false,target=false,recoveryEdge=false,reachable=false;
    int recoveryTurns=-1,rank=-1;
    Reason reason=Reason::none;
};
struct Action {
    plan3::JobId job;
    Score score;
    bool reactiveAllowed=false,admitted=false;
    Reason reason=Reason::none;
    std::array<unsigned,plan3::outcome_cap> successors{};
    unsigned successorCount=0;
};
struct Result {
    Status status=Status::unavailable;
    Choice active,control;
    std::vector<State> states;
    std::vector<Action> actions;
    unsigned iterations=0,work=0,omitted=0;
    bool assumed=true;
};
inline bool better(const Score& a,const Score& b) {
    // Both factors are at most INT_MAX, including validated tau+R.
    return a.converted*b.occupied>b.converted*a.occupied;
}
inline bool valid_number(double value) {
    return std::isfinite(value) && value>=0 && value<=std::numeric_limits<int>::max();
}
template<class Ready>
Result evaluate(const plan3::InformationState& info,const std::vector<plan3::JobForecast>& jobs,
                Assumptions assumptions,Ready&& ready) {
    Result result;
    const auto stop=[&](Status status) {
        Result empty;empty.status=status;empty.work=result.work;return empty;
    };
    const auto step=[&] {++result.work;return ready();};
    if(!step())return stop(Status::cutoff);
    if(info.round<0 || info.roundLimit<info.round || info.carriedGems<0 || info.position.tile<0 ||
       !valid_number(info.health) || !valid_number(info.maximumHealth) ||
       info.maximumHealth<=0 || info.health>info.maximumHealth || jobs.size()>maximum_jobs)
        return stop(Status::invalid);
    if(!assumptions.quiet || !assumptions.safeRecall)return stop(Status::unavailable);
    for(const auto& job:jobs) {
        if(!step())return stop(Status::cutoff);
        if(job.outcomes.size()>plan3::outcome_cap)return stop(Status::invalid);
    }
    const auto menu=plan3::canonical_menu(jobs);
    const auto count=static_cast<unsigned>(menu.jobs.size());
    result.omitted=static_cast<unsigned>(jobs.size())-count;
    result.states.reserve(2+count*plan3::outcome_cap);result.actions.reserve(count);
    State initial;initial.health=static_cast<std::int64_t>(std::floor(info.health));
    initial.bag=info.carriedGems;initial.tile=info.position.tile;initial.location=Location::field;
    initial.remaining=info.roundLimit-info.round;initial.safe=initial.health>0;
    result.states.push_back(initial);
    // A shared goal abstraction: full health at some friendly spawn, with at
    // least the original bag/credited value preserved, and nonnegative time.
    // It is a modeled goal class, not a fabricated observed coordinate/history.
    State recovered;recovered.health=static_cast<std::int64_t>(std::floor(info.maximumHealth));
    recovered.bag=info.carriedGems;recovered.location=Location::spawn;
    recovered.safe=recovered.target=recovered.reachable=recovered.health>0;
    recovered.rank=recovered.target?0:-1;
    result.states.push_back(recovered);
    for(unsigned index=0;index<count;++index) {
        if(!step())return stop(Status::cutoff);
        const auto& job=*menu.jobs[index];
        Action action;action.job=job.id;
        if(!job.complete)return stop(Status::unavailable);
        action.reactiveAllowed=plan3::admitted(info,job,false);
        if(!action.reactiveAllowed) {
            action.reason=Reason::not_admitted;result.actions.push_back(action);continue;
        }
        if(static_cast<unsigned>(job.id.kind)>static_cast<unsigned>(plan3::JobKind::renew))
            return stop(Status::unavailable);
        const auto* nominal=job.nominal();
        const auto occupied=std::int64_t(nominal->tau)+job.projectedRenewalAndReturnTime;
        if(nominal->tau<1 || nominal->deltaS<0 || job.projectedRenewalAndReturnTime<0 ||
           occupied>std::numeric_limits<int>::max())return stop(Status::invalid);
        action.score={static_cast<unsigned>(nominal->deltaS),static_cast<unsigned>(std::max<std::int64_t>(1,occupied))};
        for(const auto& outcome:job.outcomes) {
            if(!step())return stop(Status::cutoff);
            const auto& terminal=outcome.terminal;
            if(outcome.tau<1 || outcome.deltaS<0 || !valid_number(outcome.healthLoss) ||
               !valid_number(terminal.health) || terminal.maximumHealth!=info.maximumHealth ||
               terminal.health>terminal.maximumHealth || terminal.bag<0 || outcome.conversions.size()>2 ||
               (outcome.minimumHealth && (!valid_number(*outcome.minimumHealth) ||
                *outcome.minimumHealth>info.maximumHealth)) ||
               static_cast<unsigned>(outcome.response)>3 || outcome.lifecycle<0 || outcome.lifecycle>2)
                return stop(Status::invalid);
            std::int64_t credited=0;
            for(const auto& event:outcome.conversions) {
                if(!step())return stop(Status::cutoff);
                if(event.completedAt<1 || event.completedAt>outcome.tau || event.gems<=0)
                    return stop(Status::invalid);
                credited+=event.gems;
            }
            if(credited!=outcome.deltaS)return stop(Status::invalid);
            if(job.id.kind==plan3::JobKind::renew &&
               (credited!=0 || terminal.bag!=info.carriedGems))return stop(Status::invalid);
            State endpoint;endpoint.health=static_cast<std::int64_t>(std::floor(terminal.health));
            endpoint.bag=terminal.bag;endpoint.credited=outcome.deltaS;
            const auto remaining=std::int64_t(initial.remaining)-outcome.tau;
            endpoint.remaining=static_cast<int>(remaining);
            endpoint.tile=terminal.position.tile;
            endpoint.location=job.id.kind==plan3::JobKind::renew?Location::spawn:
                outcome.deltaS>0?Location::altar:Location::field;
            // terminal(bag>0) moves its potential position to a FUTURE cashout
            // approach. Renew's actual macro endpoint is spawn; preserve bag.
            if(job.id.kind==plan3::JobKind::renew && terminal.bag>0)endpoint.tile=-1;
            endpoint.safe=endpoint.health>0 && remaining>=0;
            if(remaining<0)endpoint.reason=Reason::deadline;
            // Complete recovery schedules use their phase minimum, separately
            // from endpoint health. Absent that witness, keep the conservative
            // one-sortie test: a healed respawn endpoint proves no survival.
            const bool survived=outcome.minimumHealth ? *outcome.minimumHealth>0 : outcome.healthLoss<info.health;
            if(!survived) {endpoint.safe=false;endpoint.reason=Reason::survival_unknown;}
            if(credited+terminal.bag<info.carriedGems) {endpoint.safe=false;endpoint.reason=Reason::bag_loss;}
            if(job.id.kind!=plan3::JobKind::renew && (terminal.bag>0 || endpoint.tile<0)) {
                endpoint.safe=false;endpoint.location=Location::unknown;endpoint.reason=Reason::position_unknown;
            }
            endpoint.target=endpoint.safe &&
                ((terminal.bag==0 && credited>=std::max(1,info.carriedGems)) ||
                 (job.id.kind==plan3::JobKind::renew && terminal.health>=info.maximumHealth));
            if(endpoint.safe && !endpoint.target && terminal.bag>=info.carriedGems &&
               terminal.preparationTurns && *terminal.preparationTurns>=0) {
                endpoint.recoveryTurns=std::max(1,*terminal.preparationTurns);
                endpoint.recoveryEdge=endpoint.recoveryTurns<=remaining;
            }
            if(endpoint.safe && !endpoint.target && !endpoint.recoveryEdge)endpoint.reason=Reason::no_certified_recovery;
            if(result.states.size()>=maximum_states)return stop(Status::unavailable);
            action.successors[action.successorCount++]=static_cast<unsigned>(result.states.size());
            endpoint.reachable=endpoint.target;endpoint.rank=endpoint.target?0:-1;
            result.states.push_back(endpoint);
        }
        result.actions.push_back(action);
    }
    // Same synchronous backward target-reachability operator as wave_b_math.
    // Endpoint actions are a single conditional recall/heal continuation;
    // only root actions borrow current forecasts. No recursive forecasting.
    for(unsigned iteration=1;iteration<=result.states.size();++iteration) {
        std::vector<unsigned> added;added.reserve(result.states.size());
        for(unsigned s=0;s<result.states.size();++s) {
            if(!step())return stop(Status::cutoff);
            const auto& state=result.states[s];if(!state.safe || state.reachable)continue;
            bool reaches=s!=0 && state.recoveryEdge && result.states[1].reachable;
            if(s==0)for(const auto& action:result.actions) {
                if(!step())return stop(Status::cutoff);
                if(!action.reactiveAllowed || !action.successorCount)continue;
                bool all=true;
                for(unsigned d=0;d<action.successorCount;++d) {
                    if(!step())return stop(Status::cutoff);
                    all&=result.states[action.successors[d]].reachable;
                }
                reaches|=all;
            }
            if(reaches)added.push_back(s);
        }
        if(added.empty())break;
        ++result.iterations;
        for(const auto s:added) {result.states[s].reachable=true;result.states[s].rank=static_cast<int>(iteration);}
    }
    const auto nominate=[](Choice& choice,const Action& action) {
        if(!choice.present || better(action.score,choice.score) ||
           (!better(choice.score,action.score) && action.job<choice.job))
            choice={true,action.job,action.score};
    };
    for(auto& action:result.actions) {
        if(!step())return stop(Status::cutoff);
        if(!action.reactiveAllowed)continue;
        nominate(result.control,action);
        action.admitted=result.states[0].reachable && action.successorCount>0;
        for(unsigned d=0;d<action.successorCount;++d) {
            if(!step())return stop(Status::cutoff);
            action.admitted&=result.states[action.successors[d]].reachable;
        }
        if(action.admitted)nominate(result.active,action);
        else action.reason=Reason::unsafe_successor;
    }
    result.status=Status::known;return result;
}
inline Result evaluate(const plan3::InformationState& info,const std::vector<plan3::JobForecast>& jobs,
                       Assumptions assumptions) {
    return evaluate(info,jobs,assumptions,[] {return true;});
}
} // namespace plan3_lifeline
