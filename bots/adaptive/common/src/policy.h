#pragma once
#include "profile.h"
#include "episode.h"
#include "organization.h"
#include <algorithm>
#include <array>
#include <span>

namespace p8::common {
enum class Arm { active, control };
enum class Rejection { none, malformed, stale, resources, unsafe, no_gain,
    evidence, opportunity_unavailable, probe_limit, probe_value, capacity };

struct Offer {
    Plan plan{};
    p6::Schedule schedule{};
    // Certificates for every affected actor, including independent producers
    // displaced by the offered crew. Their groups may differ for U/G.
    std::array<p6::CycleCertificate, p6::max_actors> safety{};
    int safety_count = 0;
    int context_slot = -1;
    ContextKey entry{};
    bool diagnostic_segment = false;
    int marked_extra_turns = 0;
    // A whole frontier package is funded from current personal resources.
    p6::Gems capital_cost = 0;
};
#if P8_ORDINARY_COMPLETE_CYCLE
inline bool ordinary_complete_offer(const Offer& offer,int self) {
    return ordinary_long_cycle(offer.plan)&&!offer.diagnostic_segment&&offer.marked_extra_turns==0&&
        offer.capital_cost==0&&offer.plan.crew_count==1&&offer.plan.collector==self&&offer.plan.crew[0]==self&&
        offer.schedule.actor_count==1&&offer.schedule.actors[0].actor_id==self&&
        offer.safety_count==1&&offer.safety[0].actor==self&&offer.safety[0].solo;
}
#endif
struct Probe {
    int offer = -1;
    std::array<Organization, 2> named_continuations{Organization::U, Organization::H};
    // Complete intervention + continuation, including ambiguous/G branch.
    // These are forecasts, never runtime completion receipts.
    std::array<p6::Schedule, 3> branches{};
    std::array<Plan, 3> continuation_plans{};
    std::array<std::array<p6::CycleCertificate, p6::max_actors>, 3> safety{};
    std::array<int, 3> safety_count{};
    bool endpoint_distinguishes = false;
};
struct Inputs {
    p6::Frame frame{};
    p6::Settings settings{};
    p6::ScenarioWindows windows{};
    p6::Fact<std::uint64_t> settings_identity{};
    p6::Observation<NumericState> own_state{};
    p6::Observation<int> threat_regime{};
    int picks = 3, target_group = -1;
    DraftRoster draft{}; // Actual own draft IDs: an enemy's duplicate kit is not an ally.
    int component_slot = -1, supported_slot = -1;
    ContextKey component_entry{}, supported_entry{};
    std::array<Offer, max_options> offers{};
    int offer_count = 0, independent = -1;
    Probe probe{};
    // A helper can personally see the named collector on the deterministic group rally pad.
    // It sees willingness/current capability, never Tavi's private ledger.
    p6::Observation<int> visible_proposer{};
};
struct Decision {
    int option = -1;
    Organization organization = Organization::G;
    Rejection reason = Rejection::opportunity_unavailable;
    p6::ViewClock clock{};
    bool deliberate_probe = false, capital_allowed = false;
    bool record_episode = false; // Card-scoped observations; productive fallback may abstain.
    p6::ScoreEvaluation score{};
    std::array<Rejection, max_options> rejected{};
};

inline bool resources_match(const Inputs& in, const p6::Schedule& schedule) {
    if (schedule.actor_count < 1 || schedule.actor_count > p6::max_actors) return false;
    bool self_present = false;
    for (int i = 0; i < schedule.actor_count; ++i) {
        const auto& a = schedule.actors[i];
        const auto* observed = in.frame.visible(a.actor_id);
        if (!observed || !roster_actor(*observed, in.draft) || !a.initial_bag.known() ||
            a.initial_bag.value != observed->bag || !a.initial_gold.known()) return false;
        if (a.actor_id == in.frame.clock.self) {
            self_present = true;
            if (a.initial_bag.source != p6::Source::self_observed ||
                a.initial_gold.source != p6::Source::self_observed ||
                a.initial_gold.value != in.frame.own.gold) return false;
        } else if (a.initial_bag.source != p6::Source::visible_subject ||
                   a.initial_gold.source != p6::Source::conservative || a.initial_gold.value != 0) return false;
    }
    return self_present;
}
inline p6::ScoreEvaluation evaluate(const Inputs& in, const p6::Schedule& schedule) {
    if (!resources_match(in, schedule)) return {};
    return p6::evaluate_schedule(schedule, in.windows, in.settings,
        {in.frame.clock.round, in.frame.clock.self}, in.frame.own_team_score);
}
inline const p6::CycleCertificate* certificate(const Offer& offer, int actor) {
    for (int i = 0; i < offer.safety_count; ++i)
        if (offer.safety[i].actor == actor) return &offer.safety[i];
    return nullptr;
}
// Synchronous numeric observations only; default callers perform no callbacks.
// Stages follow successful structural, actor-set and resource/safety gates.
struct LegalityProbe {
    void* context=nullptr;
    void (*mark)(void*,unsigned) noexcept=nullptr;
    void operator()(unsigned stage) const noexcept { if(mark)mark(context,stage); }
};
inline Rejection legal_offer(const Inputs& in, const Offer& offer,p6::ScoreEvaluation* score=nullptr,
                             LegalityProbe probe={}) {
    if(score)*score={}; // Output only: prior caller values never authorize an offer.
    if (offer.safety_count < 1 || offer.safety_count > p6::max_actors ||
        offer.capital_cost < 0 || offer.plan.group < 0 ||
        offer.schedule.target_id != offer.plan.group ||
        !plan_valid(offer.plan,in.frame.clock.self) ||
        offer.plan.remaining_rounds > in.settings.max_rounds - in.frame.clock.round)
        return Rejection::malformed;
#if P8_ORDINARY_COMPLETE_CYCLE
    if(ordinary_long_cycle(offer.plan)&&(!ordinary_complete_offer(offer,in.frame.clock.self)||
        offer.plan.remaining_rounds!=in.settings.max_rounds-in.frame.clock.round))return Rejection::evidence;
#endif
    probe(0);
    if (!p6::same_actors(offer.schedule, in.offers[in.independent].schedule)) return Rejection::malformed;
    probe(1);
    if (!resources_match(in, offer.schedule)) return Rejection::resources;
    for (int i = 0; i < offer.schedule.actor_count; ++i) {
        const int id = offer.schedule.actors[i].actor_id;
        const auto* cert = certificate(offer, id);
        if (!cert || !cert->valid(in.frame, id, cert->group)) return Rejection::unsafe;
    }
    if (offer.capital_cost > in.frame.own.gold) return Rejection::resources;
    probe(2);
    const auto evaluated=evaluate(in,offer.schedule);
    if(score)*score=evaluated;
    if (!evaluated.eligible()) return Rejection::malformed;
    return Rejection::none;
}
inline bool valid_inputs(const Inputs& in) {
    return in.frame.valid() && p6::valid(in.settings) && in.offer_count > 0 &&
        in.offer_count <= max_options && in.independent >= 0 && in.independent < in.offer_count &&
        in.draft.picks == in.picks && roster_actor(in.frame.own.actor, in.draft) && in.target_group >= 0 &&
        in.settings_identity.source == p6::Source::queried_setting && in.settings_identity.value != 0 &&
        in.own_state.current(in.frame.clock, in.frame.clock.self) &&
        valid_numeric_state(in.own_state.value) &&
        in.threat_regime.current(in.frame.clock, in.frame.clock.self) &&
        in.own_state.value.health == in.frame.own.actor.health &&
        in.own_state.value.max_health == in.frame.own.actor.max_health &&
        in.own_state.value.strength == in.frame.own.actor.strength &&
        in.own_state.value.bag == in.frame.own.actor.bag;
}

// Admission methods below are deliberately actor local. Helpers use the visible
// proposal and their current certificates; no argument supplies a partner ledger.
inline bool participant(const Plan& plan, int id) {
    if (plan.crew_count < 1 || plan.crew_count > p6::max_actors) return false;
    for (int i = 0; i < plan.crew_count; ++i) if (plan.crew[i] == id) return true;
    return false;
}
inline bool guarded_key_current(const Inputs& in, const ContextKey& key) {
    const auto& state = in.own_state.value;
    return p8::valid(key) && key.actor_id == in.frame.clock.self &&
        key.champion == static_cast<int>(in.frame.own.actor.type) &&
        key.settings_id == in.settings_identity.value && key.threat_regime == in.threat_regime.value &&
        key.build == EntryBuild{state.equipment_hash,state.max_health,state.strength,state.focus,state.growth} &&
        key.hp_band == health_band(state.health,state.max_health);
}
inline bool context_matches(const Inputs& in, const Offer& offer) {
    const auto& key = offer.entry;
    if (!guarded_key_current(in,key) ||
        key.source_group != offer.plan.group || key.route_hash != plan_route_hash(offer.plan) ||
        (key.return_route_hash && key.return_route_hash!=plan_return_hash(offer.plan)) ||
        key.participant_count != offer.plan.crew_count ||
        key.build.max_health != in.frame.own.actor.max_health ||
        key.build.strength != in.frame.own.actor.strength) return false;
    for (int i = 0; i < key.participant_count; ++i)
        if (!participant(offer.plan, key.participants[i])) return false;
    return true;
}
inline bool slot_current(const Inputs& in, const EvidenceLedger& ledger, int id, const ContextKey& key) {
    const auto* slot = ledger.state(id);
    return slot && guarded_key_current(in,key) &&
        applicability(slot->declaration.key,slot->declaration.envelope,
                      {key,in.own_state.value,all_premises},true) == Applicability::applicable;
}
inline bool crew_legal(const Inputs& in, const Offer& offer, bool core_only = true) {
    const auto& plan = offer.plan;
    if (!participant(plan, in.frame.clock.self)) return false;
    for (int i = 0; i < plan.crew_count; ++i) {
        const auto* actor = in.frame.visible(plan.crew[i]);
        const auto* cert = certificate(offer, plan.crew[i]);
        if (!actor || !roster_actor(*actor, in.draft) || !actor->alive ||
            actor->channel != p6::Channel::none || !cert ||
            cert->group != plan.group || !cert->valid(in.frame, actor->id, plan.group)) return false;
        for (int j = 0; j < i; ++j) if (plan.crew[i] == plan.crew[j]) return false;
        if ((plan.organization == Organization::H || plan.organization == Organization::J) &&
            core_only && actor->pick_ordinal >= 3) return false; // P8-01 extensions stay U.
    }
    const auto* collector = in.frame.visible(plan.collector);
    if (!collector || !participant(plan, plan.collector)) return false;
    if (plan.organization == Organization::U || plan.organization == Organization::G)
        return plan.crew_count == 1 && plan.collector == in.frame.clock.self &&
            certificate(offer, in.frame.clock.self)->solo;
    if (plan.organization == Organization::H)
        return plan.crew_count >= 2 && collector->pick_ordinal == 0;
    return plan.crew_count == 3 && collector->pick_ordinal == 0;
}
inline bool package_funded(const Inputs& in, const Offer& offer) {
    if (offer.capital_cost > in.frame.own.gold) return false;
    p6::Gems spending = 0;
    for (int a = 0; a < offer.schedule.actor_count; ++a) {
        const auto& actor = offer.schedule.actors[a];
        if (!participant(offer.plan, actor.actor_id)) continue;
        for (int e = 0; e < actor.event_count; ++e) {
            const auto& event = actor.events[e];
            if (event.kind != p6::EventKind::purchase) continue;
            // Partner gold is self-only: version 1 cannot fund its purchase.
            if (actor.actor_id != in.frame.clock.self || !event.gold_cost.known() ||
                !p6::add(spending, event.gold_cost.value, spending)) return false;
        }
    }
    return spending <= in.frame.own.gold &&
        (offer.plan.group != in.target_group || spending <= offer.capital_cost);
}
inline bool helper_accepts(const Inputs& in, const Offer& offer) {
    const auto* proposer = in.frame.visible(offer.plan.collector);
    const int ordinal = crew_index(offer.plan,offer.plan.collector);
    return proposer && ordinal >= 0 && proposer->position == offer.plan.pads[ordinal] &&
        offer.plan.collector != in.frame.clock.self &&
        in.visible_proposer.current(in.frame.clock, offer.plan.collector) &&
        in.visible_proposer.value == offer.plan.group;
}
inline int ordinary_choice(const Inputs& in, const std::array<Rejection,max_options>& admissible) {
    int best = -1;
    p6::ScoreEvaluation rank;
    for (int i = 0; i < in.offer_count; ++i) {
        const auto& offer = in.offers[i];
        if (offer.diagnostic_segment || offer.marked_extra_turns != 0 ||
            admissible[i] != Rejection::none) continue;
        const auto next = evaluate(in, offer.schedule);
        if (best < 0 || p6::compare(next, rank) > 0) { best = i; rank = next; }
    }
    return best;
}
// This is only a conditional forecast permission, never a completion receipt.
// One diagnostic cannot manufacture the two completed component cycles needed
// to commission a new organization. The earlier success must survive through
// the worst still-consistent duration of the proposed second episode.
inline std::int64_t probe_terminal_round(const Inputs& in,const Offer& offer,const EvidenceLedger& ledger) {
    const auto* s=ledger.state(offer.context_slot);
    if (!s || ledger.suspended(offer.context_slot,in.frame.clock.round/project_rounds)) return -1;
    int worst=0;
    for (int h=0;h<s->declaration.hypothesis_count;++h)
        if (s->consistent[h]) worst=std::max(worst,s->declaration.hypotheses[h].offset.upper);
    const std::int64_t duration=std::int64_t(offer.plan.forecast_rounds)+worst;
    return duration>0 && duration<=std::min(forecast_window,offer.plan.remaining_rounds) ?
        std::int64_t(in.frame.clock.round)+duration : -1;
}
inline bool continuation_component_permitted(const Inputs& in, const Offer& offer,
                                              const EvidenceLedger& ledger) {
    if (!slot_current(in,ledger,in.component_slot,in.component_entry)) return false;
    const auto* s=ledger.state(in.component_slot);
    const auto terminal=probe_terminal_round(in,offer,ledger);
    if (!s || terminal<0 || ledger.active()) return false;
    if (ledger.component_qualified(in.component_slot,in.component_entry,in.frame.clock.round))
        return terminal-s->latest_positive<qualification_rounds;
    if (!s || ledger.active() || offer.context_slot!=in.component_slot ||
        offer.entry!=in.component_entry || !context_matches(in,offer) ||
        s->declaration.kind!=EpisodeKind::return_component ||
        s->campaign_episodes!=1 || s->positive_completions!=1 || s->latest_positive<0 ||
        ledger.suspended(in.component_slot,in.frame.clock.round/project_rounds)) return false;
    return in.frame.clock.round>s->latest_positive && terminal-s->latest_positive<qualification_rounds;
}
inline bool probe_useful(const Inputs& in, const Offer& offer, const EvidenceLedger& ledger,
                         const ProbeBudget& budget, int option, int alternative=-1,
                         bool core_only=true) {
    const auto& probe = in.probe;
    const auto* slot = ledger.state(offer.context_slot);
    if (probe.offer != option || !probe.endpoint_distinguishes || !slot ||
        probe.named_continuations[0] == probe.named_continuations[1] ||
        !budget.available(in.frame.clock.round / project_rounds) ||
        offer.marked_extra_turns < 0 || offer.marked_extra_turns > max_probe_turns ||
        in.frame.own.actor.bag != 0 || in.frame.own.actor.max_health <= 0 ||
        in.frame.own.actor.health < minimum_trial_health_fraction * in.frame.own.actor.max_health)
        return false;
    int explanations = 0;
    for (int i = 0; i < slot->declaration.hypothesis_count; ++i) explanations += slot->consistent[i];
    if (explanations < 2) return false;
    if (alternative<0) alternative=in.independent;
    if (alternative>=in.offer_count) return false;
    const auto comparator = evaluate(in, in.offers[alternative].schedule);
    if (!comparator.eligible()) return false;
    for (const auto& branch : probe.branches) {
        if (!p6::same_actors(branch, offer.schedule)) return false;
        const auto score = evaluate(in, branch);
        if (!score.eligible() || score.productive_arrival > forecast_window ||
            score.occupancy < evaluate(in, offer.schedule).occupancy ||
            p6::compare(score, comparator) < 0) return false;
    }
    for (int branch = 0; branch < 3; ++branch) {
        const auto& continuation = probe.continuation_plans[branch];
        if (!plan_valid(continuation,in.frame.clock.self) ||
            probe.branches[branch].target_id != continuation.group ||
            probe.safety_count[branch] < 1 || probe.safety_count[branch] > p6::max_actors ||
            (branch < 2 && continuation.organization != probe.named_continuations[branch]) ||
            (branch == 2 && continuation.organization != Organization::G &&
                            continuation.organization != Organization::U)) return false;
        for (int i = 0; i < continuation.crew_count; ++i)
            if (!in.frame.visible(continuation.crew[i]) ||
                p6::actor_index(probe.branches[branch],continuation.crew[i]) < 0) return false;
        for (int a = 0; a < probe.branches[branch].actor_count; ++a) {
            const int id = probe.branches[branch].actors[a].actor_id;
            const p6::CycleCertificate* safe = nullptr;
            for (int c = 0; c < probe.safety_count[branch]; ++c)
                if (probe.safety[branch][c].actor == id) safe = &probe.safety[branch][c];
            if (!safe || !safe->valid(in.frame,id,safe->group) ||
                (participant(continuation,id) && safe->group != continuation.group)) return false;
        }
        Offer future=offer; future.plan=continuation;
        future.schedule=probe.branches[branch]; future.safety=probe.safety[branch];
        future.safety_count=probe.safety_count[branch];
        if (!crew_legal(in,future,core_only) || !package_funded(in,future)) return false;
        if (continuation.organization==Organization::H) {
            const auto* target=ledger.state(in.supported_slot);
            if (!continuation_component_permitted(in,offer,ledger) || !target ||
                target->declaration.kind!=EpisodeKind::full_organization ||
                !slot_current(in,ledger,in.supported_slot,in.supported_entry) ||
                !route_component_matches(in.component_entry,in.supported_entry) ||
                continuation.group!=in.supported_entry.source_group ||
                plan_route_hash(continuation)!=in.supported_entry.route_hash ||
                continuation.crew_count!=in.supported_entry.participant_count ||
                ledger.suspended(in.supported_slot,in.frame.clock.round/project_rounds) ||
                (ledger.campaign_exhausted(in.supported_slot) &&
                 (!ledger.organization_qualified(in.supported_slot,in.supported_entry,in.frame.clock.round) ||
                  probe_terminal_round(in,offer,ledger)-target->latest_positive>=qualification_rounds))) return false;
            for (int m=0;m<in.supported_entry.participant_count;++m)
                if (!participant(continuation,in.supported_entry.participants[m])) return false;
        } else if (continuation.organization==Organization::J) return false;
    }
    return true;
}
// Refresh observed material changes before querying admission. The component's
// own membership remains self; the supported context may be unknown when a
// partner is out of sight. Unknown never becomes a new success.
inline void refresh_evidence(const Inputs& in, EvidenceLedger& ledger) {
    if (!valid_inputs(in)) return;
    for (int slot_id : {in.component_slot,in.supported_slot}) {
        const auto* slot = ledger.state(slot_id);
        if (!slot) continue;
        Premises observed;
        observed.key = slot_id == in.component_slot ? in.component_entry : in.supported_entry;
        observed.key.actor_id = in.frame.clock.self;
        observed.key.champion = static_cast<int>(in.frame.own.actor.type);
        observed.state = in.own_state.value;
        observed.key.settings_id = in.settings_identity.value;
        observed.key.threat_regime = in.threat_regime.value;
        const auto* active=ledger.active();
        const auto* active_slot=active ? ledger.state(active->context) : nullptr;
        const bool represented_component=active_slot &&
            route_component_matches(slot->declaration.key,active_slot->declaration.key) &&
            active_slot->declaration.envelope.contains(observed.state,active_slot->declaration.key.build);
        if (active && (active->context == slot_id || represented_component)) {
            observed.key.build = slot->declaration.key.build;
            observed.key.hp_band = slot->declaration.key.hp_band;
        } else {
            observed.key.build = {observed.state.equipment_hash,observed.state.max_health,
                observed.state.strength,observed.state.focus,observed.state.growth};
            observed.key.hp_band = health_band(observed.state.health,observed.state.max_health);
        }
        observed.personally_observed = all_premises;
        for (int i = 0; i < observed.key.participant_count; ++i)
            if (!in.frame.visible(observed.key.participants[i]))
                observed.personally_observed &= ~membership_known;
        ledger.observe_context(slot_id,observed,in.frame.clock.round);
    }
}
struct Admission {
    bool started = false;
    std::uint32_t episode = 0;
    Rejection rejection = Rejection::evidence;
};
struct DiagnosticSegment {
    int last_round = -1, spent_turns = 0;
    bool active = false;
    void start() { if (active) return; last_round = -1; spent_turns = 0; active = true; }
    void observe(const LocalFrame& in, bool inside_premarked_segment, ProbeBudget& budget, Work& work) {
        if (!active || !in.fresh() || in.frame.clock.round <= last_round) return;
        last_round = in.frame.clock.round;
        if (inside_premarked_segment) ++spent_turns;
        budget.observed_diagnostic_turns(spent_turns);
        if (budget.overrun) { work.request_stop(); active = false; }
    }
};
} // namespace p8::common
