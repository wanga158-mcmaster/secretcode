#pragma once
#include "profile.h"
#include <memory>

namespace plan4::counterpart {
// Narrow private use of the common physical state machine. The caller must
// validate its exact travel-only source/route. All stock, source fingerprints,
// score, deadline and the fixed continuation menu remain common-owned values.
class PhysicalContinuation {
    std::unique_ptr<Forecast> prefix_=std::make_unique<Forecast>(), first_=std::make_unique<Forecast>();
    std::unique_ptr<SimulationState> state_=std::make_unique<SimulationState>();
public:
    bool predict_into(const Information& info,const Job& job,const Menu& menu,const Boundary& boundary,
                      const Result& response,Forecast& out) {
        if (!response.supported || boundary.turns<1 || boundary.turns>max_turns || response.own_turns<0 ||
            response.own_turns>boundary.turns || response.own_delay || response.recall_interrupted ||
            response.final_own.bag!=(response.predicted_death?0:info.own.bag) || response.final_health<0 || response.final_health>info.own.health ||
            response.own_damage<0 || response.own_damage>info.own.health ||
            response.final_health!=std::max(0.0f,info.own.health-response.own_damage) ||
            (!response.predicted_death && response.own_turns!=boundary.turns) ||
            !std::isfinite(response.final_health) || (response.predicted_death!=(response.final_health==0)) ||
            !initialize_state_into(info,menu,*prefix_,*state_) || !can_refine(info)) return false;
        if (response.own_turns && response.final_own.position!=boundary.after[response.own_turns-1].position) return false;
        if (!response.own_turns && response.final_own.position!=info.own.position) return false;
        const int elapsed=response.predicted_death?response.death_round-info.round:response.own_turns;
        if (elapsed<0 || elapsed>boundary.turns || elapsed>state_->deadline-info.round) return false;
        state_->position=response.final_own.position;state_->health=response.final_health;state_->now=info.round+elapsed;
        state_->bag=response.final_own.bag;
        state_->stopped=response.predicted_death;
        prefix_->final_round=state_->now;prefix_->final_health=state_->health;prefix_->final_bag=state_->bag;
        if (elapsed) prefix_->events[prefix_->event_count++]={EventKind::travel,{info.round,Phase::owner_action},
            {state_->now,Phase::owner_action},-1,info.own.bag,info.own.bag,0,info.own.gold,state_->health,Status::predicted};
        if (response.predicted_death) {
            prefix_->abort_reason=AbortReason::dead_actor;
            prefix_->events[prefix_->event_count++]={EventKind::abort,{state_->now,Phase::owner_action},
                {state_->now,Phase::owner_action},-1,info.own.bag,0,0,info.own.gold,0,Status::predicted};
        }
        // Constructor clears only its own output. Restore carries the physical
        // prefix, then executes the ORIGINAL candidate, including ordered pairs.
        detail::Simulation simulation(info,menu,*first_);
        if (!simulation.restore(*state_)) return false;
        *first_=*prefix_;simulation.run_first(job);simulation.checkpoint(*state_);
        return continue_from_into(info,menu,*first_,*state_,out);
    }
};
} // namespace plan4::counterpart
