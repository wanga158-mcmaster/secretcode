#pragma once
#include "transactions.h"
#include "../../earned-center/src/trace.h"
#include <utility>

namespace p8::reversible_workshop {
using common::Arm; using common::Offer; using common::Decision;
using common::Admission; using common::Rejection; using common::ProbeBudget;
using CycleTrace=earned_center::CycleTrace;
// Private synchronous numeric instrumentation; no SDK dependency or permission.
// Only offer zero has detailed gate/evaluation marks; other offers stay opaque.
// 1/2 refresh; 3/4 Decision and clock; 5 owner/header; 6 market gate.
// 7/8 offer-zero legality; 9 crew; 10 design/health; 11/12 retained score; 13 return.
// 15/16/17 split offer-zero legality after plan, actor-set and safety gates.
struct DecisionProbe {
    void* context=nullptr;
    void (*mark)(void*,unsigned,int) noexcept=nullptr;
    void operator()(unsigned point,int value=-1) const noexcept { if(mark)mark(context,point,value); }
    static void legal_mark(void* context,unsigned stage) noexcept {
        if(stage<3)(*static_cast<DecisionProbe*>(context))(15+stage);
    }
    common::LegalityProbe legality() noexcept {
        return mark?common::LegalityProbe{this,legal_mark}:common::LegalityProbe{};
    }
};
enum class ConfigurationId { A, B };
enum class Phase { A, rebuilding_B, B, rollback_pending, rebuilding_A, retired_A, blocked };
struct Configuration {
    Inventory inventory{};
    ContextKey entry{};
    Plan plan{};
    int context=-1;
    // Public full-source upper work, not a measured-throughput coefficient.
    p6::Fact<double> public_clear_work{};
};
struct RebuildComparison {
    // Complete affected-actor schedules include spawn preparation, the actual
    // new duties and independent labor displaced by the supported proposal.
    // Refunds are not productions or earned gold. The purchase event accounts
    // only the positive net cash outlay after the separately queried sales.
    p6::Schedule changed{},unchanged{};
};
inline bool configuration_valid(const Configuration& c,int self,ConfigurationId id) {
    const bool basic=c.context>=0 && c.inventory.valid() && valid(c.entry) && plan_valid(c.plan,self) &&
        c.entry.actor_id==self && c.plan.collector==self && c.entry.source_group==c.plan.group &&
        c.entry.route_hash==plan_route_hash(c.plan) && c.entry.build.equipment_hash==c.inventory.hash() &&
        (!c.entry.return_route_hash || c.entry.return_route_hash==plan_return_hash(c.plan)) &&
        c.plan.organization==(id==ConfigurationId::A?Organization::U:Organization::H) &&
        c.entry.participant_count==c.plan.crew_count && std::isfinite(c.public_clear_work.value) &&
        c.public_clear_work.value>0 && (c.public_clear_work.source==p6::Source::public_map ||
            c.public_clear_work.source==p6::Source::synthetic_fixture);
    if (!basic) return false;
    for (int i=0;i<c.entry.participant_count;++i) if (!common::participant(c.plan,c.entry.participants[i])) return false;
    return true;
}
inline bool same_configuration(const Offer& o,const Configuration& c) {
    return o.context_slot==c.context && o.entry==c.entry && o.plan.organization==c.plan.organization &&
        plan_route_hash(o.plan)==plan_route_hash(c.plan) && o.plan.collector==c.plan.collector &&
        o.plan.crew_count==c.plan.crew_count && o.plan.crew==c.plan.crew && o.plan.pads==c.plan.pads;
}
struct Workshop {
    std::array<Configuration,2> configurations{};
    Rebuild rebuild{};
    Phase phase=Phase::A;
    std::uint32_t rebuild_permit=0;
    bool declared=false,forward_used=false,rollback_used=false;
    // Bind the exact two configurations before the component's first action.
    // This is a pair, not a learned collection of policies or equipment builds.
    bool declare(const Configuration& a,const Configuration& b,const EvidenceLedger& ledger) {
        const auto* sa=ledger.state(a.context); const auto* sb=ledger.state(b.context);
        if (declared || !sa || !sb || ledger.active() || sa->attempts || sb->attempts ||
            !configuration_valid(a,a.entry.actor_id,ConfigurationId::A) ||
            !configuration_valid(b,a.entry.actor_id,ConfigurationId::B) ||
            sa->declaration.kind!=EpisodeKind::return_component ||
            sb->declaration.kind!=EpisodeKind::full_organization ||
            !(sa->declaration.key==a.entry) || !(sb->declaration.key==b.entry) ||
            !sb->declaration.require_supported_work ||
            a.entry.actor_id!=b.entry.actor_id || a.entry.champion!=b.entry.champion ||
            a.entry.settings_id!=b.entry.settings_id || a.inventory==b.inventory ||
            a.inventory.count[damage_item]<=b.inventory.count[damage_item] ||
            b.inventory.count[survival_item]<=a.inventory.count[survival_item] ||
            b.public_clear_work.value<=a.public_clear_work.value ||
            a.plan.group==b.plan.group || a.entry.route_hash==b.entry.route_hash) return false;
        configurations={a,b}; declared=true; return true;
    }
    const Configuration& a() const { return configurations[0]; }
    const Configuration& b() const { return configurations[1]; }
    bool rebuilding() const { return phase==Phase::rebuilding_B || phase==Phase::rebuilding_A; }
    bool comparison_benefits(const Inputs& in,const RebuildComparison& comparison,const Funding& funds) const {
        if (!funds.valid || !p6::same_actors(comparison.changed,comparison.unchanged)) return false;
        const auto yes=common::evaluate(in,comparison.changed),no=common::evaluate(in,comparison.unchanged);
        if (!yes.eligible() || !no.eligible() || p6::compare(yes,no)<=0) return false;
        p6::Gems spending=0; int own_occupied=-1,commands=0;
        for (int i=0;i<item_types;++i) commands+=std::abs(a().inventory.count[i]-b().inventory.count[i]);
        for (int i=0;i<comparison.changed.actor_count;++i) {
            const auto& actor=comparison.changed.actors[i];
            if (actor.actor_id==in.frame.clock.self && actor.occupied_turns.known())
                own_occupied=static_cast<int>(std::min<p6::Gems>(actor.occupied_turns.value,std::numeric_limits<int>::max()));
            for (int e=0;e<actor.event_count;++e) if (actor.events[e].kind==p6::EventKind::purchase) {
                const auto& event=actor.events[e];
                if (actor.actor_id!=in.frame.clock.self || !event.gold_cost.known() ||
                    event.gold_cost.value<0 || !p6::add(spending,event.gold_cost.value,spending)) return false;
            }
        }
        return spending==std::max<p6::Gems>(0,funds.purchases-funds.refunds) &&
            own_occupied>=b().plan.forecast_rounds+commands;
    }
    bool begin_forward(const Inputs& in,const LocalFrame& local,EvidenceLedger& ledger,Arm arm,
                       const Work& work,const RebuildComparison& comparison) {
        if (!declared || arm!=Arm::active || phase!=Phase::A || forward_used ||
            !collector_actor(in.frame.own.actor,in.draft) || !safe_shop(in,local,work) || ledger.active() ||
            !(in.market.inventory.value==a().inventory) ||
            !common::slot_current(in,ledger,a().context,a().entry) ||
            !ledger.component_qualified(a().context,a().entry,in.frame.clock.round)) return false;
        const auto funds=funded(in,a().inventory,b().inventory);
        if (!comparison_benefits(in,comparison,funds)) return false;
        // Reversibility uses the same current queried prices and actual cash
        // remaining after the forward rebuild, not future captures or score.
        if (!funded(in,b().inventory,a().inventory,funds.final_gold).valid) return false;
        bool visible_feasible=false;
        for (int i=0;i<in.offer_count;++i) if (same_configuration(in.offers[i],b()) &&
            common::legal_offer(in,in.offers[i])==Rejection::none && common::crew_legal(in,in.offers[i],false))
            visible_feasible=true;
        if (!visible_feasible) return false;
        const auto permit=ledger.reserve_declared_rebuild(a().context,b().context,in.frame.clock.round,b().entry);
        if (!permit) return false;
        if (!rebuild.begin(in,local,work,b().inventory)) { ledger.abandon_declared_rebuild(permit); return false; }
        rebuild_permit=permit; forward_used=true; phase=Phase::rebuilding_B; return true;
    }
    void request_rollback(EvidenceLedger& ledger) {
        if (phase==Phase::A || phase==Phase::retired_A || phase==Phase::rebuilding_A || phase==Phase::blocked) return;
        if (rebuild_permit) ledger.abandon_declared_rebuild(rebuild_permit);
        rebuild_permit=0;
        if (rebuild.active || rebuild.pending) rebuild.interrupt();
        phase=Phase::rollback_pending;
    }
    bool begin_rollback(const Inputs& in,const LocalFrame& local,EvidenceLedger& ledger,const Work& work) {
        if (!declared || phase!=Phase::rollback_pending || rollback_used || ledger.active() ||
            !safe_shop(in,local,work)) return false;
        if (in.market.inventory.value==a().inventory) { phase=Phase::retired_A; return true; }
        if (!rebuild.begin(in,local,work,a().inventory)) return false;
        rollback_used=true; phase=Phase::rebuilding_A; return true;
    }
    TransactionCommand transaction(const Inputs& in,const LocalFrame& local,EvidenceLedger& ledger,const Work& work) {
        if (!rebuilding()) return {};
        if (in.threat_regime.current(in.frame.clock,in.frame.clock.self) &&
            in.threat_regime.value!=(phase==Phase::rebuilding_B?b():a()).entry.threat_regime) {
            transaction_failed(ledger); return {};
        }
        const auto command=rebuild.next(in,local,work);
        if (rebuild.failed) transaction_failed(ledger);
        return command;
    }
    bool confirm(const TransactionCommand& c,const TransactionReceipt& r,const Inputs& after,EvidenceLedger& ledger) {
        if (!rebuilding() || !rebuild.confirm(c,r,after)) return false;
        // Actual immediate stats/HP, including Health purchase healing only at
        // spawn, now replace the previous snapshot. No predicted stat increment.
        common::refresh_evidence(after,ledger);
        if (rebuild.completed && phase==Phase::rebuilding_A) {
            if (after.own_state.value.max_health!=a().entry.build.max_health ||
                after.own_state.value.strength!=a().entry.build.strength ||
                after.own_state.value.focus!=a().entry.build.focus ||
                after.own_state.value.growth!=a().entry.build.growth) phase=Phase::blocked;
            else phase=Phase::retired_A;
        }
        return true;
    }
    void transaction_failed(EvidenceLedger& ledger) {
        const bool restoring=phase==Phase::rebuilding_A;
        rebuild.interrupt();
        if (restoring) phase=Phase::blocked;
        else request_rollback(ledger);
    }
    bool commission(const Inputs& in,const LocalFrame& local,EvidenceLedger& ledger,const Work& work) {
        if (!declared || phase!=Phase::rebuilding_B || !rebuild.completed || !rebuild_permit ||
            !safe_shop(in,local,work)) return false;
        if (!(in.market.inventory.value==b().inventory) || !common::slot_current(in,ledger,b().context,b().entry)) {
            request_rollback(ledger); return false;
        }
        const Offer* offer=nullptr;
        for (int i=0;i<in.offer_count;++i) if (same_configuration(in.offers[i],b())) offer=&in.offers[i];
        if (!offer || common::legal_offer(in,*offer)!=Rejection::none || !common::crew_legal(in,*offer,false) ||
            !common::package_funded(in,*offer)) return false;
        CampaignAdmission admission{{b().entry,in.own_state.value,all_premises},true,true,true,true,true};
        if (!ledger.begin_after_declared_rebuild(rebuild_permit,in.frame.clock.round,admission)) {
            request_rollback(ledger); return false;
        }
        rebuild_permit=0; phase=Phase::B; return true;
    }
    void observe_result(const Resolution& result,EvidenceLedger& ledger) {
        if (phase!=Phase::B || !result.accepted) return;
        if (result.status==Status::context_invalid || (result.applicable &&
            (result.status==Status::safety_failed || (result.eligible && result.contradiction))))
            request_rollback(ledger);
    }
    void refresh(const Inputs& in,EvidenceLedger& ledger) {
        common::refresh_evidence(in,ledger);
        if (declared && phase==Phase::B) {
            const auto* slot=ledger.state(b().context);
            if (slot && slot->material_premise_changed)
                request_rollback(ledger);
        }
    }
    Decision choose(const Inputs& in,const EvidenceLedger& ledger,Arm arm,DecisionProbe probe={}) const {
        probe(3);
        Decision out; out.clock=in.frame.clock;
        probe(4);
        const bool owner=collector_actor(in.frame.own.actor,in.draft);
        probe(5);
        if (!own_market(in) || (owner&&!declared)) {
            out.reason=Rejection::malformed; probe(6,0); probe(13,out.option); return out;
        }
        probe(6,1);
        for (int i=0;i<in.offer_count;++i) {
            const auto& o=in.offers[i];
            // The successful legality result belongs only to this offer/call.
            // Subsequent gates borrow these inputs without changing its score.
            p6::ScoreEvaluation score{};
            if(i==0)probe(7);
            auto reject=common::legal_offer(in,o,&score,i==0?probe.legality():common::LegalityProbe{});
            if(i==0)probe(8,int(reject));
            if (reject==Rejection::none && !common::crew_legal(in,o,false)) reject=Rejection::unsafe;
            if(i==0)probe(9,int(reject));
            bool record=false;
            if (reject==Rejection::none && (o.diagnostic_segment || o.marked_extra_turns!=0)) reject=Rejection::evidence;
            if (reject==Rejection::none && o.plan.organization==Organization::H) {
                if (arm==Arm::control || (!common::helper_accepts(in,o) && !owner)) reject=Rejection::evidence;
                else if (owner) {
                    const auto* slot=ledger.state(b().context);
                    if (phase!=Phase::B || !(in.market.inventory.value==b().inventory) ||
                        !same_configuration(o,b()) || !common::slot_current(in,ledger,b().context,b().entry) ||
                        !slot || !slot->campaign_started || ledger.suspended(b().context,in.frame.clock.round/common::project_rounds) ||
                        (ledger.campaign_exhausted(b().context) && !ledger.organization_qualified(b().context,b().entry,in.frame.clock.round)))
                        reject=Rejection::evidence;
                    else record=true;
                }
                if (reject==Rejection::none && (!common::package_funded(in,o) ||
                    p6::compare(score,common::evaluate(in,in.offers[in.independent].schedule))<=0))
                    reject=Rejection::no_gain;
            } else if (reject==Rejection::none && (o.plan.organization==Organization::U || o.plan.organization==Organization::G)) {
                // Withdrawal uses a presently verified independent route with
                // the actual retained gear. It is no third specialization and
                // earns no new configuration proof or spending permission.
                if (owner && rebuilding()&&!rebuild.completed) reject=Rejection::evidence;
                if (owner && phase==Phase::A && same_configuration(o,a()) &&
                    common::slot_current(in,ledger,a().context,a().entry) && !ledger.campaign_exhausted(a().context)) record=true;
            } else if (reject==Rejection::none) reject=Rejection::evidence;
            if (reject==Rejection::none && record &&
                in.frame.own.actor.health<common::minimum_trial_health_fraction*in.frame.own.actor.max_health)
                reject=Rejection::unsafe;
            if(i==0)probe(10,int(reject));
            out.rejected[i]=reject; if (reject!=Rejection::none) continue;
            if(i==0)probe(11);
            const auto& rank_score=score;
            if(i==0)probe(12);
            if (out.option<0 || p6::compare(rank_score,out.score)>0) {
                out.option=i; out.organization=o.plan.organization; out.reason=Rejection::none;
                out.record_episode=arm==Arm::active&&record; out.score=rank_score;
            }
        }
        probe(13,out.option);
        return out;
    }
    Decision decide(const Inputs& in,EvidenceLedger& ledger,Arm arm,DecisionProbe probe={}) {
        probe(1); refresh(in,ledger); probe(2); return choose(in,ledger,arm,probe);
    }
    // Ordinary undeclared producers use their current private policy once,
    // then obtain a synchronous admission-only observation. No old proposal
    // or callback supplies episode, spending or experimental permission.
    template<class ObserveLocal>
    Admission admit_ordinary_current(const Inputs& in,EvidenceLedger& ledger,Arm arm,
                                     Work& work,ObserveLocal&& observe_local) {
        Admission out;
        if(declared||phase!=Phase::A||collector_actor(in.frame.own.actor,in.draft)) {
            out.rejection=Rejection::malformed;return out;
        }
        if(work.active||ledger.active()) {out.rejection=Rejection::stale;return out;}
        if(in.offer_count!=1||in.independent!=0) {out.rejection=Rejection::malformed;return out;}
        const auto& offer=in.offers[0];
        if(offer.diagnostic_segment||offer.marked_extra_turns!=0) {
            out.rejection=Rejection::evidence;return out;
        }
        if(offer.plan.organization!=Organization::U||offer.plan.crew_count!=1||
            offer.plan.collector!=in.frame.clock.self||offer.plan.crew[0]!=in.frame.clock.self||
            offer.plan.ingress.count<2||offer.safety_count!=1||offer.schedule.actor_count!=1||
            offer.schedule.actors[0].actor_id!=in.frame.clock.self) {
            out.rejection=Rejection::malformed;return out;
        }
        const auto current=decide(in,ledger,arm);
        if(current.option<0) {out.rejection=current.reason;return out;}
        if(current.option!=0) {out.rejection=Rejection::malformed;return out;}
        if(current.record_episode||current.deliberate_probe||current.capital_allowed) {
            out.rejection=Rejection::evidence;return out;
        }
        const auto& local=std::forward<ObserveLocal>(observe_local)(current);
        if(!local.admission_only||!local.fresh()||!(local.frame.clock==in.frame.clock)||
            !(current.clock==in.frame.clock)||work.active||ledger.active()) {
            out.rejection=Rejection::stale;return out;
        }
        out.started=work.start(offer.plan,local);
        out.rejection=out.started?Rejection::none:Rejection::unsafe;
        return out;
    }
    Admission commit(const Inputs& in,const LocalFrame& local,const Decision& proposed,EvidenceLedger& ledger,
                     Arm arm,Work& work) {
        Admission out;
        if (!local.fresh() || !(local.frame.clock==in.frame.clock) || !(proposed.clock==in.frame.clock) || work.active || (rebuilding()&&!rebuild.completed)) {
            out.rejection=Rejection::stale; return out;
        }
        const auto now=decide(in,ledger,arm);
        if (now.option<0 || now.option!=proposed.option || !physical_admissible(in.offers[now.option].plan,local)) return out;
        const auto& offer=in.offers[now.option];
        if (now.record_episode) {
            EpisodeRequest request; request.context=offer.context_slot; request.issue_round=in.frame.clock.round;
            request.deadline=request.issue_round+std::min(common::forecast_window,offer.plan.remaining_rounds);
            request.nominal_duration=offer.plan.forecast_rounds; request.action=int(offer.plan.organization);
            request.premises={offer.entry,in.own_state.value,all_premises};
            out.episode=ledger.issue(request); if (!out.episode) return out;
        }
        out.started=work.start(offer.plan,local); out.rejection=out.started?Rejection::none:Rejection::unsafe;
        if (!out.started && out.episode) ledger.invalidate_active(in.frame.clock.round);
        return out;
    }
    Command advance(const Inputs& in,const LocalFrame& local,EvidenceLedger& ledger,Work& work) {
        if (!own_market(in) || !(in.frame.clock==local.frame.clock)) {
            work.request_stop(); if (ledger.active()&&local.fresh()) ledger.invalidate_active(local.frame.clock.round);
        } else refresh(in,ledger);
        if (phase==Phase::rollback_pending || phase==Phase::blocked) work.request_stop();
        if (declared && work.active && work.plan.organization==Organization::H &&
            work.plan.collector==in.frame.clock.self &&
            ledger.suspended(b().context,local.frame.clock.round/common::project_rounds)) work.request_stop();
        const auto command=work.step(local);
        if (ledger.active()&&!work.active&&!work.cycle_completed) ledger.invalidate_active(local.frame.clock.round);
        return command;
    }
};
} // namespace p8::reversible_workshop
