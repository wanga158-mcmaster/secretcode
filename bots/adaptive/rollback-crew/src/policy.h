#pragma once
#include "profile.h"
#include "trace.h"
#include "../../common/src/policy.h"
namespace p8::rollback_crew {
using common::Arm; using common::Offer; using common::Decision; using common::Rejection; using common::Admission;
struct Inputs:common::Inputs {};
enum class Phase { incumbent,trial,restore };
struct SavedIncumbent {
    Plan plan{};
    ContextKey entry{};
    int context=-1,proof_round=-1;
    bool present=false;
};
class Crew {
    SavedIncumbent saved_{};
    Plan replacement_{};
    ContextKey replacement_entry_{};
    int replacement_context_=-1,deadline_=-1;
    unsigned attempts_=0;
    Phase phase_=Phase::incumbent;
    bool retired_=false;
    bool replacement_in_flight_=false;
    static bool source_current(const Inputs& in,int group) {
        const auto* g=in.frame.live_group(group);
        return g&&g->original_spawn_visible&&g->all_spawns_visible&&g->uncontested_now&&
            (g->lifecycle==p6::Lifecycle::living||g->lifecycle==p6::Lifecycle::reward_available);
    }
    static bool exact(const Offer& o,const SavedIncumbent& s) {
        return o.plan.organization==Organization::U&&o.plan.group==s.plan.group&&o.plan.collector==s.plan.collector&&
            o.plan.crew==s.plan.crew&&plan_route_hash(o.plan)==plan_route_hash(s.plan)&&o.entry==s.entry&&o.context_slot==s.context;
    }
    static bool crew_current(const Inputs& in,const Offer& o) {
        const auto& p=o.plan;
        if (!common::participant(p,in.frame.clock.self)) return false;
        for (int i=0;i<p.crew_count;++i) {
            const auto* a=in.frame.visible(p.crew[i]); const auto* c=common::certificate(o,p.crew[i]);
            if (!a||!common::roster_actor(*a,in.draft)||!a->alive||a->channel!=p6::Channel::none||!c||!c->valid(in.frame,a->id,p.group)) return false;
        }
        if (p.organization==Organization::U||p.organization==Organization::G)
            return p.crew_count==1&&p.collector==in.frame.clock.self&&independent_safety(*common::certificate(o,in.frame.clock.self));
        if (p.organization!=Organization::J||p.crew_count!=3||p.collector!=in.draft.actors[2]) return false;
        for (int i=0;i<3;++i) if (!common::participant(p,in.draft.actors[i])) return false;
        return true;
    }
    bool saved_current(const Inputs& in,const EvidenceLedger& ledger) const {
        return saved_.present&&source_current(in,saved_.plan.group)&&common::slot_current(in,ledger,saved_.context,saved_.entry)&&
            ledger.component_qualified(saved_.context,saved_.entry,in.frame.clock.round);
    }
    void refresh_context(const Inputs& in,EvidenceLedger& ledger,int slot,const ContextKey& key) const {
        const auto* declaration=ledger.state(slot); if (!declaration) return;
        Premises p{key,in.own_state.value,all_premises}; p.key.settings_id=in.settings_identity.value;
        p.key.actor_id=in.frame.clock.self; p.key.champion=int(in.frame.own.actor.type); p.key.threat_regime=in.threat_regime.value;
        const auto* active=ledger.active();
        if (!active||active->context!=slot) {
            p.key.build={p.state.equipment_hash,p.state.max_health,p.state.strength,p.state.focus,p.state.growth};
            p.key.hp_band=health_band(p.state.health,p.state.max_health);
        }
        for (int i=0;i<p.key.participant_count;++i) if (!in.frame.visible(p.key.participants[i])) p.personally_observed&=~membership_known;
        ledger.observe_context(slot,p,in.frame.clock.round);
    }
public:
    const SavedIncumbent& saved() const { return saved_; }
    Phase phase() const { return phase_; }
    unsigned attempts() const { return attempts_; }
    bool retired() const { return retired_; }
    bool remember(const Inputs& in,const EvidenceLedger& ledger) {
        if (saved_.present||!common::valid_inputs(in)||!incumbent_actor(in.frame.own.actor,in.draft)||ledger.active()) return false;
        const auto* s=ledger.state(in.component_slot);
        if (!s||!common::slot_current(in,ledger,in.component_slot,in.component_entry)||
            !ledger.component_qualified(in.component_slot,in.component_entry,in.frame.clock.round)) return false;
        for (int i=0;i<in.offer_count;++i) {
            const auto& o=in.offers[i];
            if (o.plan.organization==Organization::U&&o.context_slot==in.component_slot&&o.entry==in.component_entry&&
                common::context_matches(in,o)&&common::legal_offer(in,o)==Rejection::none&&crew_current(in,o)) {
                saved_={o.plan,o.entry,o.context_slot,s->latest_positive,true}; return true;
            }
        }
        return false;
    }
    void retire(Work& work) {
        if (attempts_) { retired_=true; phase_=Phase::restore;
            replacement_in_flight_=work.active&&work.plan.organization==Organization::J; work.request_stop(); }
    }
    void observe_result(const Resolution& result,Work& work) {
        // Literal card cap: one replacement episode, including a success. One
        // positive may support consistency but cannot qualify J's two-cycle
        // doctrine. This bounded version restores U/G instead of retrying.
        if (phase_==Phase::trial&&result.accepted) retire(work);
    }
    void refresh(const Inputs& in,EvidenceLedger& ledger,Work* work=nullptr) {
        if (!common::valid_inputs(in)) return;
        if (phase_==Phase::trial) {
            refresh_context(in,ledger,replacement_context_,replacement_entry_);
            const bool expired=in.frame.clock.round>=deadline_;
            if (expired&&ledger.active()) ledger.invalidate_active(in.frame.clock.round);
            if (expired||!ledger.active()||ledger.suspended(replacement_context_,in.frame.clock.round/common::project_rounds)) {
                retired_=true; phase_=Phase::restore; if (work) work->request_stop();
            }
        } else if (!(phase_==Phase::restore&&replacement_in_flight_)) {
            // The incumbent is checked at its actual proposed entry; damage
            // during a different J trial does not widen its frozen envelope.
            refresh_context(in,ledger,in.component_slot,in.component_entry);
        }
    }
    Decision choose(const Inputs& in,const EvidenceLedger& ledger,const ProbeBudget&,Arm arm) const {
        Decision out; out.clock=in.frame.clock;
        if (!common::valid_inputs(in)) { out.reason=Rejection::malformed; return out; }
        const bool incumbent=incumbent_actor(in.frame.own.actor,in.draft);
        for (int i=0;i<in.offer_count;++i) {
            const auto& o=in.offers[i]; const auto& p=o.plan; auto reject=common::legal_offer(in,o); bool record=false;
            if (reject==Rejection::none&&!crew_current(in,o)) reject=Rejection::unsafe;
            if (reject==Rejection::none&&(o.capital_cost||!common::package_funded(in,o))) reject=Rejection::resources;
            if (reject==Rejection::none&&(o.diagnostic_segment||o.marked_extra_turns!=0)) reject=Rejection::evidence;
            if (reject==Rejection::none&&p.organization==Organization::J) {
                if (p6::compare(common::evaluate(in,o.schedule),common::evaluate(in,in.offers[in.independent].schedule))<=0) reject=Rejection::no_gain;
                if (reject==Rejection::none&&p.collector!=in.frame.clock.self&&!common::helper_accepts(in,o)) reject=Rejection::evidence;
                if (reject==Rejection::none&&arm==Arm::active) {
                    const auto* s=ledger.state(in.supported_slot);
                    if (attempts_||retired_||phase_!=Phase::incumbent||in.frame.own.actor.bag!=0||
                        (incumbent&&(!saved_.present||!common::slot_current(in,ledger,saved_.context,saved_.entry)||
                            !ledger.component_qualified(saved_.context,saved_.entry,in.frame.clock.round)))||
                        o.context_slot!=in.supported_slot||o.entry!=in.supported_entry||!common::context_matches(in,o)||
                        !s||s->declaration.kind!=EpisodeKind::full_organization||s->campaign_started||
                        ledger.suspended(in.supported_slot,in.frame.clock.round/common::project_rounds)) reject=Rejection::evidence;
                    else record=true;
                }
            } else if (reject==Rejection::none&&(p.organization==Organization::U||p.organization==Organization::G)) {
                if (arm==Arm::active&&phase_==Phase::restore&&incumbent) {
                    const bool verified=saved_current(in,ledger);
                    if (p.organization==Organization::U&&(!verified||!exact(o,saved_))) reject=Rejection::evidence;
                    if (p.organization==Organization::G&&(verified||!source_current(in,p.group))) reject=Rejection::evidence;
                }
                if (arm==Arm::active&&phase_==Phase::incumbent&&incumbent&&!saved_.present&&
                    o.context_slot==in.component_slot&&o.entry==in.component_entry&&common::context_matches(in,o)&&
                    !ledger.suspended(in.component_slot,in.frame.clock.round/common::project_rounds)&&!ledger.campaign_exhausted(in.component_slot)&&
                    in.frame.own.actor.bag==0) record=true;
            } else if (reject==Rejection::none) reject=Rejection::evidence;
            if (reject==Rejection::none&&record&&in.frame.own.actor.health<common::minimum_trial_health_fraction*in.frame.own.actor.max_health) reject=Rejection::unsafe;
            out.rejected[i]=reject; if (reject!=Rejection::none) continue;
            const auto score=common::evaluate(in,o.schedule);
            if (out.option<0||p6::compare(score,out.score)>0) {
                out.option=i; out.organization=p.organization; out.reason=Rejection::none; out.record_episode=record; out.score=score;
            }
        }
        return out;
    }
    Decision decide(const Inputs& in,EvidenceLedger& ledger,const ProbeBudget& probes,Arm arm) {
        refresh(in,ledger); if (arm==Arm::active&&!saved_.present) remember(in,ledger); return choose(in,ledger,probes,arm);
    }
    Admission commit(const Inputs& in,const LocalFrame& local,const Decision& proposed,EvidenceLedger& ledger,
                     const ProbeBudget& probes,Arm arm,Work& work) {
        Admission out;
        if (!local.fresh()||!(in.frame.clock==local.frame.clock)||!(proposed.clock==in.frame.clock)||work.active) { out.rejection=Rejection::stale; return out; }
        const auto now=decide(in,ledger,probes,arm); if (now.option<0||now.option!=proposed.option||now.record_episode!=proposed.record_episode) return out;
        const auto& o=in.offers[now.option]; if (!physical_admissible(o.plan,local)) { out.rejection=Rejection::unsafe; return out; }
        if (now.record_episode) {
            const Premises p{o.entry,in.own_state.value,all_premises};
            if (o.plan.organization==Organization::J&&!ledger.begin_initial_campaign(o.context_slot,in.frame.clock.round,{p,true,true,true})) return out;
            EpisodeRequest r; r.context=o.context_slot; r.issue_round=in.frame.clock.round;
            r.deadline=r.issue_round+std::min(common::forecast_window,o.plan.remaining_rounds); r.nominal_duration=o.plan.forecast_rounds;
            r.action=int(o.plan.organization); r.premises=p; out.episode=ledger.issue(r); if (!out.episode) return out;
        }
        if (arm==Arm::active&&o.plan.organization==Organization::J) {
            ++attempts_; phase_=Phase::trial; replacement_=o.plan; replacement_entry_=o.entry;
            replacement_context_=o.context_slot; deadline_=ledger.active()?ledger.active()->deadline:in.frame.clock.round;
        }
        out.started=work.start(o.plan,local); out.rejection=out.started?Rejection::none:Rejection::unsafe;
        if (arm==Arm::active&&o.plan.organization==Organization::J) replacement_in_flight_=out.started;
        if (!out.started&&out.episode) { ledger.invalidate_active(in.frame.clock.round); retire(work); }
        return out;
    }
    Command advance(const Inputs& in,const LocalFrame& local,EvidenceLedger& ledger,Arm arm,Work& work) {
        if (!common::valid_inputs(in)||!(in.frame.clock==local.frame.clock)) {
            work.request_stop(); if (ledger.active()&&local.fresh()) ledger.invalidate_active(local.frame.clock.round);
        } else if (arm==Arm::active) refresh(in,ledger,&work);
        if (arm==Arm::active&&phase_==Phase::restore&&work.active&&work.plan.organization==Organization::J) work.request_stop();
        const auto command=work.step(local);
        if (ledger.active()&&!work.active&&!work.cycle_completed) ledger.invalidate_active(local.frame.clock.round);
        if (arm==Arm::active&&phase_==Phase::trial&&!ledger.active()) retire(work);
        if (!work.active) replacement_in_flight_=false;
        return command;
    }
};
} // namespace p8::rollback_crew
