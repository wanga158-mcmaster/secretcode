#pragma once
#include "sdk.h"
#include "../../reversible-workshop/src/transactions.h"

namespace p8::sdk {
namespace workshop=reversible_workshop;
static_assert(static_cast<int>(cambc::item_type::count)==workshop::item_types);

inline workshop::Inventory own_inventory() {
    workshop::Inventory result;const auto items=cambc::self().item_count();
    for(int i=0;i<workshop::item_types;++i) {
        const auto count=items[static_cast<cambc::item_type>(i)];
        if(count>20) {result.count[0]=-1;return result;}
        result.count[i]=int(count);
    }
    return result;
}
// Only named quote rows become known. Inventory and shop observations remain
// complete and current, including when no quote rows are requested.
inline workshop::Market market(const Snapshot& snapshot,std::span<const int> quote_ids) {
    workshop::Market result=native::exact_default<workshop::Market>();
    std::array<bool,workshop::item_types> requested{};
    for(const int id:quote_ids) {
        if(id<0||id>=workshop::item_types)return result;
        requested[id]=true;
    }
    if(!snapshot.valid)return result;
    const auto clock=snapshot.frame.clock;const auto self=clock.self;const auto& me=cambc::self();
    if(int(cambc::round_number())!=clock.round||identity(me)!=self)return result;
    const auto inventory=own_inventory();
    if(!inventory.valid()||inventory.hash()!=snapshot.own.equipment_hash)return result;
    result.inventory={inventory,clock,self,p6::Knowledge::exact};
    result.spawn_shop_legal=snapshot.spawn_shop_legal;
    result.sell_legal={me.can_sell(),clock,self,p6::Knowledge::exact};
    for(int i=0;i<workshop::item_types;++i) {
        if(!requested[i])continue;
        const auto item=static_cast<cambc::item_type>(i);const auto info=item.info();auto& quote=result.items[i];
        quote.price={info.cost,p6::Source::queried_setting};
        quote.purchasable={info.purchasable,p6::Source::queried_setting};
        quote.buy_legal={me.can_buy(item),clock,self,p6::Knowledge::exact};
        if(snapshot.frame.own.actor.type==p6::Champion::tavi)
            quote.refund={info.cost,p6::Source::queried_setting};
    }
    if(int(cambc::round_number())!=clock.round||equipment_identity(me)!=snapshot.own.equipment_hash)
        return native::exact_default<workshop::Market>();
    return result;
}
inline workshop::Market market(const Snapshot& snapshot) {
    static constexpr auto quote_ids=[] {
        std::array<int,workshop::item_types> ids{};
        for(int i=0;i<workshop::item_types;++i)ids[i]=i;
        return ids;
    }();
    return market(snapshot,quote_ids);
}
inline bool unchanged_shop_state(const Snapshot& snapshot,const p6::ViewClock& command_clock,
                                 const NumericState& before,p6::Gems gold) {
    const auto& observed=snapshot.own;
    if(!snapshot.valid||snapshot.frame.clock!=command_clock||observed.health!=before.health||
        observed.max_health!=before.max_health||observed.strength!=before.strength||observed.focus!=before.focus||
        observed.growth!=before.growth||observed.bag!=before.bag||observed.equipment_hash!=before.equipment_hash||
        snapshot.frame.own.gold!=gold||int(cambc::round_number())!=command_clock.round)return false;
    const auto& me=cambc::self();
    return identity(me)==command_clock.self&&me.is_alive()&&
        point(me.position())==snapshot.frame.own.actor.position&&
        cambc::map::spawn_region(cambc::team::ally).is_set(me.position())&&
        !me.is_recalling()&&!me.is_submitting_gems()&&
        !me.is_teleporting()&&std::int64_t(me.gold_count())==gold&&std::int64_t(me.gem_count())==before.bag&&
        double(me.health())==before.health&&double(me.max_health())==before.max_health&&
        double(me.strength())==before.strength&&double(me.focus())==before.focus&&
        equipment_identity(me)==before.equipment_hash&&!me.buffs().has_stun()&&
        int(cambc::round_number())==command_clock.round;
}
struct TransactionResult {
    workshop::TransactionReceipt receipt{};
    Snapshot after{};
    workshop::Market after_market{};
};
// This executor performs exactly one already-admitted command. A successful
// SDK call alone is not confirmation: the card must consume the fresh queried
// inventory, gold, stats and receipt through Rebuild::confirm afterward.
inline TransactionResult transact(Adapter& adapter,const Snapshot& snapshot,
                                  const workshop::TransactionCommand& command) {
    TransactionResult result;auto& receipt=result.receipt;
    receipt.command_clock=command.clock;receipt.token=command.token;receipt.kind=command.kind;receipt.item=command.item;
    if(!command.valid||!command.token||command.item<0||command.item>=workshop::item_types||
        (command.kind!=workshop::TransactionKind::sell&&command.kind!=workshop::TransactionKind::buy)||
        !unchanged_shop_state(snapshot,command.clock,command.stats_before,command.gold_before)||
        !snapshot.frame.own.at_spawn||snapshot.own.bag!=0||
        !snapshot.spawn_shop_legal.current(command.clock,command.clock.self)||!snapshot.spawn_shop_legal.value||
        !(own_inventory()==command.before)||cambc::fuel_remaining()<18000)return result;
    const auto item=static_cast<cambc::item_type>(command.item);const auto& me=cambc::self();
    const auto info=item.info();
    if(command.amount!=info.cost)return result;
    if(command.kind==workshop::TransactionKind::sell) {
        if(snapshot.frame.own.actor.type!=p6::Champion::tavi||!me.can_sell()||command.before.count[command.item]<1)return result;
        if(int(cambc::round_number())!=command.clock.round||cambc::fuel_remaining()<12000)return result;
        receipt.attempted=true;me.sell(item,1);
    } else {
        if(!info.purchasable||!me.can_buy(item)||command.amount>command.gold_before)return result;
        if(int(cambc::round_number())!=command.clock.round||cambc::fuel_remaining()<12000)return result;
        receipt.attempted=true;me.buy(item,1);
    }
    receipt.success=true;result.after=adapter.observe();receipt.observed_clock=result.after.frame.clock;
    // Confirmation consumes the fresh inventory. Any later command obtains
    // its own current quotes; no pre-transaction legality is carried forward.
    result.after_market=market(result.after,std::span<const int>{});return result;
}
} // namespace p8::sdk
