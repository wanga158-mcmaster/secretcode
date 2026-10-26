#pragma once
#include "profile.h"
#include "../../common/src/policy.h"
#include "../../finite-counterprotocol/src/response.h"
#include "../../earned-center/src/trace.h"
namespace p8::probe_reveal_commit {
using common::Offer;using common::Arm;using common::Rejection;using common::Admission;
using finite_counterprotocol::Response;using finite_counterprotocol::Site;using finite_counterprotocol::DistanceObservation;
using finite_counterprotocol::ResponseBook;
struct Layout {
    Plan intervention{},supported{},helper{},independent{},generalist{};Site site{};
    bool valid(const DraftRoster& draft)const{
        if(!draft.valid()||!finite_counterprotocol::site_valid(site)||
            !plan_valid(intervention,draft.actors[0])||intervention.organization!=Organization::U||
            !plan_valid(supported,draft.actors[0])||!plan_valid(helper,draft.actors[2])||
            supported.organization!=Organization::H||helper.organization!=Organization::H||
            supported.collector!=draft.actors[0]||helper.collector!=draft.actors[0]||supported.crew_count!=2||helper.crew_count!=2||
            supported.crew[0]!=draft.actors[0]||supported.crew[1]!=draft.actors[2]||supported.crew!=helper.crew||supported.pads!=helper.pads||
            supported.group!=helper.group||supported.group==intervention.group||supported.altar!=site.altar||
            !plan_valid(independent,draft.actors[0])||independent.organization!=Organization::U||
            !plan_valid(generalist,draft.actors[0])||generalist.organization!=Organization::G||
            independent.group!=supported.group||generalist.group==supported.group||generalist.group==intervention.group||
            intervention.renewal.tiles[intervention.renewal.count-1]!=supported.ingress.tiles[0]||
            intervention.renewal.tiles[intervention.renewal.count-1]!=independent.ingress.tiles[0]||
            intervention.renewal.tiles[intervention.renewal.count-1]!=generalist.ingress.tiles[0])return false;
        bool approach=false;for(int i=0;i<site.approach_count;++i)approach|=supported.bank.tiles[supported.bank.count-1]==site.own_approaches[i];
        return approach;
    }
};
inline bool same_plan(const Plan& a,const Plan& b){return plan_route_hash(a)==plan_route_hash(b)&&a.organization==b.organization&&
    a.group==b.group&&a.collector==b.collector&&a.altar==b.altar&&a.crew==b.crew&&a.crew_count==b.crew_count&&a.pads==b.pads&&
    a.forecast_rounds==b.forecast_rounds&&a.capture_window==b.capture_window;}
inline bool same_layout(const Layout& a,const Layout& b){return same_plan(a.intervention,b.intervention)&&same_plan(a.supported,b.supported)&&
    same_plan(a.helper,b.helper)&&same_plan(a.independent,b.independent)&&same_plan(a.generalist,b.generalist)&&
    finite_counterprotocol::same_site(a.site,b.site)&&finite_counterprotocol::site_valid(b.site);}
struct Forecast {
    std::array<p6::Schedule,3> branches{}; // Actual low-load prefix then H, U or G.
    std::array<std::array<p6::CycleCertificate,p6::max_actors>,3> safety{};
    std::array<int,3> safety_count{};
    p6::Fact<p6::Gems> lowload_gems{},lowload_gold{};int lowload_event=-1;
    std::array<int,3> continuation_event{-1,-1,-1};
    std::array<p6::Fact<p6::Gems>,3> continuation_gems{};
    std::array<p6::Fact<p6::Gems>,3> continuation_gold{};
    std::array<int,3> lowload_end{},continuation_start{};
    bool endpoint_distinguishes=false;int marked_extra_turns=0;
};
struct Inputs:common::Inputs {
    Layout layout{};Forecast forecast{};int intervention=-1,rival=-1;DistanceObservation distance{};
    p6::Observation<Offer> own_income{},existing_inventory{};
    p6::Observation<int> collector_proposal{};
};
struct Protocol {
    int owner=-1,slot=-1;ContextDeclaration organization{};Layout frozen{};common::ProbeBudget budget{};ResponseBook responses{};
    bool declared=false,issued=false,deliberate=false,intervention_complete=false,intervention_failed=false,initial_observed=false,initial_approach=false;
    Response initial=Response::unknown;int issue_round=-1,initial_round=-1,last_marked=-1,marked_turns=0;
    std::uint64_t work_generation=0,action_token=0;p6::ViewClock work_start{},action_clock{};
    std::uint32_t response_serial=0;
    explicit Protocol(int actor):owner(actor){}
    bool declare(const Layout& layout,const ContextDeclaration& full,const DraftRoster& draft,EvidenceLedger& ledger){
        if(declared||ledger.active()||!layout.valid(draft))return false;
        if(owner==draft.actors[0]){
            if(full.kind!=EpisodeKind::full_organization||!full.require_supported_work||full.key.actor_id!=owner||
                full.key.route_hash!=plan_route_hash(layout.supported)||full.key.source_group!=layout.supported.group||
                full.key.participant_count!=2||full.key.participants[0]!=std::min(draft.actors[0],draft.actors[2])||
                full.key.participants[1]!=std::max(draft.actors[0],draft.actors[2]))return false;
            slot=ledger.predeclare(full);if(slot<0)return false;organization=full;
        }
        frozen=layout;declared=true;return true;
    }
    bool local(const Inputs& in,const EvidenceLedger& ledger)const{
        if(!declared||owner!=in.frame.clock.self||!same_layout(frozen,in.layout))return false;
        if(owner!=in.draft.actors[0])return true;
        const auto* state=ledger.state(slot);return state&&state->declaration.key==organization.key&&organization.key.actor_id==owner;
    }
    bool response_current(int round)const{const auto& r=responses.window.record;return responses.window.finished&&!responses.window.active&&
        r.owner==owner&&r.terminal_round>=0&&round>=r.terminal_round&&round-r.terminal_round<64;}
    bool ongoing_response_current(int round)const{return responses.rule()==Response::approach&&responses.latest()>=0&&
        round>=responses.latest()&&round-responses.latest()<64;}
    bool initial_current(int round,int duration=0)const{return initial_observed&&initial_approach&&initial_round>=0&&round>=initial_round&&
        std::int64_t(round)+duration<std::int64_t(initial_round)+64;}
    void bind(const Work& work,bool diagnostic){issued=true;deliberate=diagnostic;issue_round=work.started_clock.round;work_generation=work.generation;work_start=work.started_clock;}
    bool bound(const Work& work)const{return issued&&work.generation==work_generation&&work.started_clock==work_start&&same_plan(work.plan,frozen.intervention);}
    void limit_before_step(const LocalFrame& local,Work& work){
        if(!local.fresh()||!work.active||!bound(work)||!deliberate||initial_observed||
            local.frame.clock.round<=last_marked||marked_turns<common::max_probe_turns)return;
        // Eight turns have already happened. Close the eliciting segment
        // before a ninth command, without counting an action that never ran.
        work.request_stop();intervention_failed=true;responses.interrupted(local.frame.clock.round);
        initial_observed=true;initial=Response::unknown;initial_round=local.frame.clock.round;
    }
    void confirm_intervention(const Inputs& in,const Work& work,const Command& command,const ActionReceipt& receipt){
        if(!bound(work)||action_token||command.action!=PhysicalAction::attack||!receipt.success||!receipt.attempted||
            command.clock.self!=owner||command.group!=frozen.intervention.group||in.frame.own.actor.bag!=0||
            !work.productive_action_seen||work.last_round!=command.clock.round)return;
        action_token=command.token;action_clock=command.clock;
    }
    void observe(const Inputs& in,const LocalFrame& local,Work& work){
        if(!declared||owner!=in.frame.clock.self||!local.fresh()||local.frame.clock!=in.frame.clock)return;
        if(bound(work)&&!initial_observed){
            if(deliberate&&local.frame.clock.round>last_marked){last_marked=local.frame.clock.round;++marked_turns;budget.observed_diagnostic_turns(marked_turns);}
            if(budget.overrun){work.request_stop();intervention_failed=true;responses.interrupted(local.frame.clock.round);initial_observed=true;initial=Response::unknown;initial_round=local.frame.clock.round;}
            else if(action_token&&!response_serial){
                // Raw15 cannot authorize this: a successful own attack from
                // this newly issued Work must precede the baseline snapshot.
                if(responses.window.active||responses.window.finished||local.frame.clock.round!=action_clock.round||local.frame.clock.revision<action_clock.revision){intervention_failed=true;initial_observed=true;initial=Response::unknown;initial_round=local.frame.clock.round;}
                else {responses.begin(local.frame,in.layout.site,in.rival,in.distance);response_serial=responses.window.record.serial;}}
        }
        if(responses.window.active)responses.observe(local.frame,in.distance);
        if(response_serial&&!initial_observed&&responses.window.finished&&responses.window.record.serial==response_serial){
            initial_observed=true;initial=responses.window.record.response;initial_approach=initial==Response::approach;initial_round=responses.window.record.terminal_round;}
        if(bound(work)&&!work.active){intervention_complete=work.cycle_completed&&work.productive_action_seen&&work.payout_seen&&work.bank_seen&&work.renewal_seen&&
            work.payout_lower>0&&work.bank_lower>0&&work.route_witness_active;
            if(!intervention_complete)intervention_failed=true;}
    }
    bool begin_passive(const Inputs& in,const Work& work,const Command& command,const ActionReceipt& receipt,bool ordinary){
        if(!issued||!initial_approach||!ordinary||responses.window.active||!intervention_complete||command.action!=PhysicalAction::tribute||
            !receipt.attempted||!receipt.success||!work.tribute_started||work.tribute_starting_bag<=0||
            (work.plan.organization!=Organization::U&&work.plan.organization!=Organization::G&&work.plan.organization!=Organization::H)||work.plan.altar!=in.layout.site.altar||
            command.clock!=in.frame.clock||command.clock.self!=owner)return false;
        return responses.begin(in.frame,in.layout.site,in.rival,in.distance);
    }
};
}
