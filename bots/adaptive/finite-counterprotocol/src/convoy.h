#pragma once
#include "profile.h"
#include "response.h"
#include "../../common/src/policy.h"
#include "../../earned-center/src/trace.h"
namespace p8::finite_counterprotocol {
using common::Arm; using common::Offer; using common::Decision; using common::Admission; using common::Rejection;
struct Layout {
    Plan short_solo{},collector{},helper{},helper_after{};
    Site site{}; p6::Point escort_pad{}; int escort_wait_bound=0;
    bool valid(const DraftRoster& draft) const {
        if (!draft.valid()||!site_valid(site)||escort_wait_bound<1||escort_wait_bound>120||
            !plan_valid(short_solo,draft.actors[0])||short_solo.organization!=Organization::U||
            !plan_valid(collector,draft.actors[0])||!plan_valid(helper,draft.actors[1])||!plan_valid(helper_after,draft.actors[1])||
            collector.organization!=Organization::H||helper.organization!=Organization::H||helper_after.organization!=Organization::U||
            collector.collector!=draft.actors[0]||helper.collector!=collector.collector||collector.crew_count!=2||helper.crew_count!=2||
            collector.crew!=helper.crew||collector.pads!=helper.pads||collector.crew[0]!=draft.actors[0]||collector.crew[1]!=draft.actors[1]||
            helper.group!=collector.group||short_solo.group!=collector.group||collector.altar!=site.altar||helper.altar!=site.altar||
            collector.bank.tiles[collector.bank.count-1]==escort_pad||helper.release.tiles[helper.release.count-1]!=escort_pad||
            helper_after.ingress.tiles[0]!=escort_pad||helper.renewal.count!=1||helper.renewal.tiles[0]!=escort_pad||
            route_hash(short_solo.bank)!=route_hash(collector.bank)||route_hash(short_solo.renewal)!=route_hash(collector.renewal)||
            short_solo.ingress.tiles[short_solo.ingress.count-1]==collector.pads[0]) return false;
        bool approach=false; for (int i=0;i<site.approach_count;++i) approach|=collector.bank.tiles[collector.bank.count-1]==site.own_approaches[i];
        return approach;
    }
};
struct Inputs:common::Inputs { Layout layout{}; int rival=-1; DistanceObservation distance{}; };
enum class HelperPhase { independent,escort,own_income };
struct EscortDuty {
    HelperPhase phase=HelperPhase::independent;
    std::uint64_t generation=0; p6::ViewClock start{};
    int arrival=-1; bool saw_positive_bag=false,released=false,failed=false;
    std::uint16_t escorted=0,own_incomes=0;
    void begin(const Work& work,const Layout& layout,int self) {
        if (work.plan.organization==Organization::H&&self!=work.plan.collector&&plan_route_hash(work.plan)==plan_route_hash(layout.helper)) {
            phase=HelperPhase::escort; generation=work.generation; start=work.started_clock; arrival=-1;
            saw_positive_bag=released=failed=false;
        } else if (phase==HelperPhase::own_income&&work.plan.organization==Organization::U&&plan_route_hash(work.plan)==plan_route_hash(layout.helper_after)) {
            generation=work.generation; start=work.started_clock;
        }
    }
    LocalFrame prepare(const Inputs& in,const LocalFrame& actual,const Work& work) {
        LocalFrame out=actual;
        if (phase!=HelperPhase::escort||!work.active||work.generation!=generation||!(work.started_clock==start)||
            !actual.fresh()||actual.frame.own.actor.position!=in.layout.escort_pad||actual.frame.own.actor.channel!=p6::Channel::none) return out;
        if (arrival<0) arrival=actual.frame.clock.round;
        const auto* collector=actual.frame.visible(work.plan.collector); const auto* safe=actual.certificate(work.plan.collector,work.plan.group);
        const bool known=collector&&common::roster_actor(*collector,in.draft)&&collector->alive&&safe&&independent_safety(*safe);
        if (known&&collector->bag>0) saw_positive_bag=true;
        // The helper sees current inventory only. This endpoint does not claim
        // a peer tribute receipt or attribute another actor's bag decrease.
        const bool empty_now=known&&saw_positive_bag&&collector->bag==0&&collector->channel==p6::Channel::none;
        const bool expired=actual.frame.clock.round-arrival>=in.layout.escort_wait_bound;
        if (!known||expired) failed=true;
        released=empty_now||failed;
        out.legal.value.renewal_productive=released;
        return out;
    }
    void observe(const Inputs& in,const LocalFrame& local,const Work& work) {
        if (!local.fresh()||work.generation!=generation||!(work.started_clock==start)||work.last_round!=local.frame.clock.round||work.active) return;
        if (phase==HelperPhase::escort) {
            if (work.helper_released&&work.renewal_seen) { if (!failed&&saw_positive_bag) saturating_increment(escorted); phase=HelperPhase::own_income; }
            else { failed=true; phase=HelperPhase::own_income; }
        } else if (phase==HelperPhase::own_income&&plan_route_hash(work.plan)==plan_route_hash(in.layout.helper_after)&&
            work.cycle_completed&&work.bank_seen&&work.renewal_seen) { saturating_increment(own_incomes); phase=HelperPhase::independent; }
    }
};
struct ConvoyTrace:earned_center::CycleTrace {
    int escort=-1,coverage_observations=0; p6::Point escort_pad{};
    bool begin(const EvidenceLedger& ledger,const Work& work,const Layout& layout) {
        if (!earned_center::CycleTrace::begin(ledger,work)) return false;
        escort=layout.collector.crew[1]; escort_pad=layout.escort_pad; coverage_observations=0; return true;
    }
    void observe(const LocalFrame& local,const Work& work) {
        earned_center::CycleTrace::observe(local,work,{});
        if (!active||!local.fresh()||!work.payout_seen||(bank_round>=0&&bank_round<local.frame.clock.round)) return;
        const auto* peer=local.frame.visible(escort);
        if (!peer||peer->team!=p6::Team::ally||!peer->alive||peer->channel!=p6::Channel::none||peer->position!=escort_pad) failed=true;
        else ++coverage_observations;
    }
};
class Protocol {
    bool declared_=false; int slot_=-1;
    ContextDeclaration organization_{}; Layout layout_{};
    std::uint64_t sampled_generation_=0; p6::ViewClock sampled_start_{};
public:
    ResponseBook responses{}; EscortDuty helper{};
    bool declared() const { return declared_; }
    int slot() const { return slot_; }
    const ContextDeclaration& organization() const { return organization_; }
    const Layout& layout() const { return layout_; }
    bool declare(const Layout& layout,const ContextDeclaration& full,const DraftRoster& draft,EvidenceLedger& ledger) {
        if (declared_||ledger.active()||ledger.size()!=0||!layout.valid(draft)||full.kind!=EpisodeKind::full_organization||!full.require_supported_work||
            full.key.actor_id!=draft.actors[0]||full.key.champion!=int(draft.champions[0])||full.key.source_group!=layout.collector.group||
            full.key.route_hash!=plan_route_hash(layout.collector)||full.key.participant_count!=2||
            !common::participant(layout.collector,full.key.participants[0])||!common::participant(layout.collector,full.key.participants[1])) return false;
        const int slot=ledger.predeclare(full); if (slot<0) return false;
        organization_=full; layout_=layout; slot_=slot; declared_=true; return true;
    }
    void observe_passive(const Inputs& in,const LocalFrame& local,const Work& work) {
        if (!declared_||!collector_actor(in.frame.own.actor,in.draft)) return;
        if (!local.fresh()||local.fuel_resumed||!work.route_witness_active||work.last_round!=local.frame.clock.round||
            (responses.window.active&&(work.generation!=sampled_generation_||!(work.started_clock==sampled_start_)))) {
            responses.interrupted(local.frame.clock.round); return;
        }
        responses.observe(in.frame,in.distance);
        if (responses.window.active||!work.active||work.stage!=PhysicalStage::bank||in.frame.own.actor.bag<=0||
            work.plan.altar!=layout_.site.altar||!work.tribute_started||
            (work.generation==sampled_generation_&&work.started_clock==sampled_start_)) return;
        bool at_approach=false;
        for (int i=0;i<layout_.site.approach_count;++i) at_approach|=in.frame.own.actor.position==layout_.site.own_approaches[i];
        const auto* rival=in.frame.visible(in.rival);
        if (!at_approach||!rival||rival->team!=p6::Team::enemy||!rival->alive) return;
        sampled_generation_=work.generation; sampled_start_=work.started_clock;
        responses.begin(in.frame,layout_.site,in.rival,in.distance);
    }
};
} // namespace p8::finite_counterprotocol
