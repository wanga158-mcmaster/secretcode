#pragma once
#include "policy.h"
namespace p8::common_cause_firewall {
struct PurchasePlan {
    int item=-1;
    p6::Schedule with_purchase{},without_purchase{};
};
struct PurchaseCommand {
    bool valid=false;
    p6::ViewClock clock{};
    std::uint64_t token=0;
    int item=-1;
    p6::Gems cost=0,gold_before=0;
    Inventory before{},after{};
    bool at_spawn=false;
    NumericState state_before{};
    int champion=-1;
    std::uint64_t settings_id=0;
};
struct PurchaseReceipt {
    p6::ViewClock command_clock{},observed_clock{};
    std::uint64_t token=0;
    int item=-1;
    bool attempted=false,success=false;
};
inline bool purchase_gate(const Inputs& in,const LocalFrame& local,const EvidenceLedger& ledger,const Work& work) {
    if (!reversible_workshop::own_market(in)||!in.layout.valid(in.draft)||!local.fresh()||
        !(local.frame.clock==in.frame.clock)||!local.legal.value.safe_now||local.fuel_resumed||
        work.active||work.pending||ledger.active()||!in.frame.own.actor.alive||in.frame.own.actor.bag!=0||
        in.frame.own.actor.channel!=p6::Channel::none) return false;
    if (field_actor(in.frame.own.actor,in.draft))
        return in.frame.own.actor.type==p6::Champion::tavi&&own_component(in,ledger)&&
            in.field_shop_legal.current(in.frame.clock,in.frame.clock.self)&&in.field_shop_legal.value;
    return spawn_collector(in.frame.own.actor,in.draft)&&own_organization(in,ledger)&&in.frame.own.at_spawn&&
        in.frame.own.actor.position==in.layout.spawn_shop&&
        in.market.spawn_shop_legal.current(in.frame.clock,in.frame.clock.self)&&in.market.spawn_shop_legal.value;
}
struct Shop {
    PurchaseCommand issued{};
    std::uint64_t next=1;
    bool pending=false;
    PurchaseCommand propose(const Inputs& in,const LocalFrame& local,const EvidenceLedger& ledger,const Work& work,const PurchasePlan& p) {
        if (pending||!next||!purchase_gate(in,local,ledger,work)||p.item<0||p.item>=reversible_workshop::item_types||
            !p6::same_actors(p.with_purchase,p.without_purchase)) return {};
        const auto& quote=in.market.items[p.item];
        if (quote.price.source!=p6::Source::queried_setting||quote.price.value<=0||quote.price.value>in.frame.own.gold||
            quote.purchasable.source!=p6::Source::queried_setting||!quote.purchasable.value||
            !quote.buy_legal.current(in.frame.clock,in.frame.clock.self)||!quote.buy_legal.value) return {};
        const auto yes=common::evaluate(in,p.with_purchase),no=common::evaluate(in,p.without_purchase);
        if (!yes.eligible()||!no.eligible()||p6::compare(yes,no)<=0) return {};
        int buys=0;
        for (int a=0;a<p.with_purchase.actor_count;++a) for (int e=0;e<p.with_purchase.actors[a].event_count;++e) {
            const auto& event=p.with_purchase.actors[a].events[e];
            if (event.kind==p6::EventKind::purchase) {
                ++buys;
                if (p.with_purchase.actors[a].actor_id!=in.frame.clock.self||event.tick!=0||
                    !event.gold_cost.known()||event.gold_cost.value!=quote.price.value) return {};
            }
        }
        for (int a=0;a<p.without_purchase.actor_count;++a) for (int e=0;e<p.without_purchase.actors[a].event_count;++e)
            if (p.without_purchase.actors[a].events[e].kind==p6::EventKind::purchase) return {};
        if (buys!=1) return {};
        auto after=in.market.inventory.value; ++after.count[p.item]; if (!after.valid()) return {};
        issued={true,in.frame.clock,next++,p.item,quote.price.value,in.frame.own.gold,in.market.inventory.value,after,in.frame.own.at_spawn,
            in.own_state.value,int(in.frame.own.actor.type),in.settings_identity.value};
        pending=true; return issued;
    }
    bool confirm(const PurchaseCommand& c,const PurchaseReceipt& r,const Inputs& after,EvidenceLedger& ledger) {
        if (!pending||!c.valid||c.token!=issued.token||c.item!=issued.item||!(c.clock==issued.clock)||
            r.token!=issued.token||r.item!=issued.item||!(r.command_clock==issued.clock)||!r.attempted||!r.success||
            !reversible_workshop::own_market(after)||!(r.observed_clock==after.frame.clock)||
            after.frame.clock.self!=issued.clock.self||after.frame.clock.round!=issued.clock.round||after.frame.clock.revision<=issued.clock.revision||
            after.frame.own.gold!=issued.gold_before-issued.cost||!(after.market.inventory.value==issued.after)||
            after.frame.own.at_spawn!=issued.at_spawn||!after.frame.own.actor.alive||after.frame.own.actor.channel!=p6::Channel::none||after.frame.own.actor.bag!=0||
            int(after.frame.own.actor.type)!=issued.champion||after.settings_identity.value!=issued.settings_id||
            (!issued.at_spawn&&after.own_state.value.health!=issued.state_before.health)) return false;
        pending=false;
        // Field Health may raise max health, but it supplies no invented heal;
        // all stats/HP here are the actual immediate post-transaction requery.
        common::refresh_evidence(after,ledger); return true;
    }
    void failed_attempt() { pending=false; }
};
} // namespace p8::common_cause_firewall
