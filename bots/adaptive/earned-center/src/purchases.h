#pragma once
#include "policy.h"

namespace p8::earned_center {
struct PurchasePlan {
    int bundle_id = -1;
    p6::Gems cost = 0;
    p6::Fact<p6::Gems> queried_price{};
    p6::Observation<bool> item_legal{};
    std::uint64_t equipment_after = 0;
    p6::Schedule with_purchase{}, without_purchase{};
};
struct PurchaseCommand {
    bool valid = false;
    p6::ViewClock clock{};
    std::uint32_t token = 0;
    int bundle_id = -1;
    p6::Gems cost = 0, gold_before = 0;
    std::uint64_t equipment_before = 0, equipment_after = 0;
};
struct Shop {
    PurchaseCommand issued{};
    std::uint32_t next = 1;
    bool pending = false;
    PurchaseCommand propose(const Inputs& in,const LocalFrame& local,const EvidenceLedger& ledger,
                            Arm arm,const PurchasePlan& plan) {
        if (pending || !next || !local.fresh() || !(local.frame.clock==in.frame.clock) ||
            !local.legal.value.safe_now || !purchase_gate(in,ledger,arm) ||
            plan.bundle_id<0 || plan.bundle_id>=3 || plan.cost<=0 || plan.cost>in.frame.own.gold ||
            plan.queried_price.source!=p6::Source::queried_setting || plan.queried_price.value!=plan.cost ||
            !plan.item_legal.current(in.frame.clock,in.frame.clock.self) || !plan.item_legal.value ||
            !plan.equipment_after || plan.equipment_after==in.own_state.value.equipment_hash ||
            !p6::same_actors(plan.with_purchase,plan.without_purchase)) return {};
        const auto yes=evaluate(in,plan.with_purchase), no=evaluate(in,plan.without_purchase);
        if (!yes.eligible() || !no.eligible() || p6::compare(yes,no)<=0) return {};
        int purchases=0;
        for (int a=0;a<plan.with_purchase.actor_count;++a) {
            const auto& actor=plan.with_purchase.actors[a];
            for (int e=0;e<actor.event_count;++e) if (actor.events[e].kind==p6::EventKind::purchase) {
                const auto& event=actor.events[e]; ++purchases;
                if (actor.actor_id!=in.frame.clock.self || event.tick!=0 ||
                    !event.gold_cost.known() || event.gold_cost.value!=plan.cost) return {};
            }
        }
        for (int a=0;a<plan.without_purchase.actor_count;++a)
            for (int e=0;e<plan.without_purchase.actors[a].event_count;++e)
                if (plan.without_purchase.actors[a].events[e].kind==p6::EventKind::purchase) return {};
        if (purchases!=1) return {};
        issued={true,in.frame.clock,next++,plan.bundle_id,plan.cost,in.frame.own.gold,
            in.own_state.value.equipment_hash,plan.equipment_after};
        pending=true; return issued;
    }
    // Called only after the real purchase attempt and an immediate requery of
    // personal gold/items/stats. Same-round fresh revision brackets the debit;
    // an invocation or later unrelated gold delta is not a purchase receipt.
    bool confirm(const PurchaseCommand& command,const Inputs& after,EvidenceLedger& ledger) {
        if (!pending || !command.valid || command.token!=issued.token ||
            !(command.clock==issued.clock) || command.cost!=issued.cost || command.bundle_id!=issued.bundle_id ||
            !common::valid_inputs(after) || after.frame.clock.self!=issued.clock.self ||
            after.frame.clock.round!=issued.clock.round || after.frame.clock.revision<=issued.clock.revision ||
            !after.frame.own.at_spawn || after.frame.own.gold!=issued.gold_before-issued.cost ||
            after.own_state.value.equipment_hash!=issued.equipment_after) return false;
        pending=false;
        // New actual equipment is a material context change. Do not move any
        // predecessor qualification or campaign permission onto the new build.
        common::refresh_evidence(after,ledger);
        return true;
    }
    void failed_attempt() { pending=false; }
};
} // namespace p8::earned_center
