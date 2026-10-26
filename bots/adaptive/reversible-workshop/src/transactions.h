#pragma once
#include "profile.h"
#include "../../common/src/policy.h"
#include <limits>

namespace p8::reversible_workshop {
// Pinned item IDs, not copied prices/stat coefficients. The eventual SDK
// executor must check its item_type::count before filling this complete view.
inline constexpr int item_types = 42, damage_item = 0, survival_item = 28;
struct Inventory {
    std::array<int,item_types> count{};
    bool operator==(const Inventory&) const = default;
    bool valid() const {
        int total=0;
        // This bounded pair deliberately avoids the fifth-shard automatic
        // emblem conversion. Buying that broader regime needs its own receipt.
        for (int i=0;i<item_types;++i) { const int n=count[i]; if (n<0 || n>20 || (i%2==0 && n>4)) return false; total+=n; }
        return total<=40;
    }
    std::uint64_t hash() const {
        if (!valid()) return 0;
        // Validation bounds every count to one byte, and every item index
        // also fits. Each original 8-byte word has seven zero high bytes.
        // Keep the exact FNV word order, folding its eight multiplications.
        static_assert(item_types<=256);
        constexpr auto word_factor=[] {
            std::uint64_t factor=1;
            for(int byte=0;byte<8;++byte)factor*=1099511628211ULL;
            return factor;
        }();
        std::uint64_t h=1469598103934665603ULL;
        for(int i=0;i<item_types;++i) {
            h=(h^std::uint64_t(i))*word_factor;
            h=(h^std::uint64_t(count[i]))*word_factor;
        }
        return h;
    }
};
struct ItemQuote {
    p6::Fact<p6::Gems> price{}, refund{};
    p6::Fact<bool> purchasable{};
    p6::Observation<bool> buy_legal{};
};
struct Market {
    p6::Observation<Inventory> inventory{};
    p6::Observation<bool> spawn_shop_legal{}, sell_legal{};
    std::array<ItemQuote,item_types> items{};
};
struct Inputs : common::Inputs { Market market{}; };
inline bool own_market(const Inputs& in) {
    return common::valid_inputs(in) && in.market.inventory.current(in.frame.clock,in.frame.clock.self) &&
        in.market.inventory.value.valid() && in.market.inventory.value.hash()==in.own_state.value.equipment_hash;
}
inline bool safe_shop(const Inputs& in,const LocalFrame& local,const Work& work) {
    return own_market(in) && local.fresh() && local.frame.clock==in.frame.clock &&
        local.legal.value.safe_now && !local.fuel_resumed && !work.active && !work.pending &&
        in.frame.own.actor.alive && in.frame.own.actor.bag==0 && in.frame.own.at_spawn &&
        in.frame.own.actor.channel==p6::Channel::none &&
        in.market.spawn_shop_legal.current(in.frame.clock,in.frame.clock.self) && in.market.spawn_shop_legal.value;
}
inline bool quote_current(const Inputs& in,int item,bool sale,p6::Gems& amount,bool command=true) {
    if (item<0 || item>=item_types) return false;
    const auto& q=in.market.items[item];
    if (q.price.source!=p6::Source::queried_setting || q.price.value<=0) return false;
    if (!sale) {
        if (q.purchasable.source!=p6::Source::queried_setting || !q.purchasable.value ||
            (command && (!q.buy_legal.current(in.frame.clock,in.frame.clock.self) || !q.buy_legal.value))) return false;
        amount=q.price.value; return true;
    }
    if (!in.market.sell_legal.current(in.frame.clock,in.frame.clock.self) || !in.market.sell_legal.value) return false;
    if (in.frame.own.actor.type==p6::Champion::tavi) {
        // Only actual Tavi has the pinned full-refund rule. A numeric role or
        // the draft preference is insufficient to activate it.
        amount=q.price.value; return true;
    }
    // The pinned host currently rejects this branch for every other champion.
    // A future queried partial-refund capability may fund it; never assume it.
    if (q.refund.source!=p6::Source::queried_setting || q.refund.value<0 || q.refund.value>q.price.value) return false;
    amount=q.refund.value; return true;
}
struct Funding {
    bool valid=false;
    p6::Gems refunds=0, purchases=0, final_gold=0;
};
inline Funding funded(const Inputs& in,const Inventory& from,const Inventory& to,p6::Gems starting_gold=-1) {
    Funding f; if (!own_market(in) || !from.valid() || !to.valid() || in.frame.own.gold<0) return f;
    p6::Gems cash=starting_gold>=0?starting_gold:in.frame.own.gold;
    for (int i=0;i<item_types;++i) if (from.count[i]>to.count[i]) {
        p6::Gems refund=0; if (!quote_current(in,i,true,refund)) return f;
        const auto n=from.count[i]-to.count[i];
        if (refund && n>std::numeric_limits<p6::Gems>::max()/refund) return f;
        const auto total=refund*n;
        if (!p6::add(f.refunds,total,f.refunds) || !p6::add(cash,total,cash)) return f;
    }
    for (int i=0;i<item_types;++i) if (to.count[i]>from.count[i]) {
        p6::Gems price=0; if (!quote_current(in,i,false,price,false)) return f;
        const auto n=to.count[i]-from.count[i];
        if (n>std::numeric_limits<p6::Gems>::max()/price || !p6::add(f.purchases,price*n,f.purchases)) return f;
    }
    if (f.purchases>cash) return f;
    f.valid=true; f.final_gold=cash-f.purchases; return f;
}
enum class TransactionKind { sell, buy };
struct TransactionCommand {
    bool valid=false;
    TransactionKind kind=TransactionKind::sell;
    p6::ViewClock clock{};
    std::uint64_t token=0;
    int attempt=0,item=-1;
    p6::Gems amount=0,gold_before=0;
    Inventory before{},after{};
    NumericState stats_before{};
};
struct TransactionReceipt {
    p6::ViewClock command_clock{}, observed_clock{};
    std::uint64_t token=0;
    TransactionKind kind=TransactionKind::sell;
    int item=-1;
    bool attempted=false,success=false;
};
struct Rebuild {
    Inventory target{},expected_inventory{};
    TransactionCommand issued{};
    NumericState latest_stats{};
    p6::ViewClock latest_clock{};
    std::uint64_t next_token=1;
    int attempts=0,last_sell_round=-1;
    int champion=-1;
    std::uint64_t settings_id=0;
    bool active=false,pending=false,failed=false,completed=false;
    // Called only after the card has licensed the direction. Charging happens
    // before a command, and no failure/death/epoch resets attempts.
    bool begin(const Inputs& in,const LocalFrame& local,const Work& work,const Inventory& desired) {
        if (active || pending || attempts>=2 || !safe_shop(in,local,work) ||
            desired==in.market.inventory.value || !funded(in,in.market.inventory.value,desired).valid) return false;
        ++attempts; active=true; failed=completed=false; target=desired;
        champion=int(in.frame.own.actor.type); settings_id=in.settings_identity.value;
        latest_stats=in.own_state.value; latest_clock=in.frame.clock; expected_inventory=in.market.inventory.value; return true;
    }
    TransactionCommand next(const Inputs& in,const LocalFrame& local,const Work& work) {
        if (!active || pending || !next_token || !safe_shop(in,local,work) ||
            in.frame.clock.self!=latest_clock.self || in.frame.clock.round<latest_clock.round ||
            int(in.frame.own.actor.type)!=champion || in.settings_identity.value!=settings_id ||
            (in.frame.clock.round==latest_clock.round && in.frame.clock.revision<latest_clock.revision)) return {};
        const auto& actual=in.market.inventory.value;
        if (!(actual==expected_inventory)) { interrupt(); return {}; }
        if (actual==target) { active=false; completed=true; return {}; }
        if (!funded(in,actual,target).valid) { interrupt(); return {}; }
        int item=-1; TransactionKind kind=TransactionKind::sell;
        for (int i=0;i<item_types;++i) if (actual.count[i]>target.count[i]) { item=i; break; }
        if (item>=0 && last_sell_round==in.frame.clock.round) return {};
        if (item<0) {
            kind=TransactionKind::buy;
            for (int i=0;i<item_types;++i) if (actual.count[i]<target.count[i]) { item=i; break; }
        }
        p6::Gems amount=0;
        if (item<0 || !quote_current(in,item,kind==TransactionKind::sell,amount) ||
            (kind==TransactionKind::buy && amount>in.frame.own.gold)) return {};
        Inventory after=actual; after.count[item]+=kind==TransactionKind::sell ? -1:1;
        issued={true,kind,in.frame.clock,next_token++,attempts,item,amount,in.frame.own.gold,actual,after,in.own_state.value};
        pending=true; return issued;
    }
    bool confirm(const TransactionCommand& c,const TransactionReceipt& r,const Inputs& after) {
        if (!active || !pending || !c.valid || c.token!=issued.token || c.attempt!=attempts ||
            c.kind!=issued.kind || c.item!=issued.item || c.amount!=issued.amount ||
            !(c.clock==issued.clock) || !(r.command_clock==issued.clock) || r.token!=issued.token ||
            r.kind!=issued.kind || r.item!=issued.item || !r.attempted || !r.success ||
            !own_market(after) || !(r.observed_clock==after.frame.clock) ||
            int(after.frame.own.actor.type)!=champion || after.settings_identity.value!=settings_id ||
            after.frame.clock.self!=issued.clock.self || after.frame.clock.round!=issued.clock.round ||
            after.frame.clock.revision<=issued.clock.revision || !after.frame.own.at_spawn ||
            !after.frame.own.actor.alive || after.frame.own.actor.channel!=p6::Channel::none ||
            after.frame.own.actor.bag!=0 || !(after.market.inventory.value==issued.after)) return false;
        const auto expected=issued.kind==TransactionKind::sell ? issued.gold_before+issued.amount : issued.gold_before-issued.amount;
        if (after.frame.own.gold!=expected) return false;
        if (issued.kind==TransactionKind::sell) last_sell_round=issued.clock.round;
        pending=false; latest_stats=after.own_state.value; latest_clock=after.frame.clock; expected_inventory=after.market.inventory.value;
        if (after.market.inventory.value==target) { active=false; completed=true; }
        return true;
    }
    void interrupt() { active=pending=completed=false; failed=true; }
};
} // namespace p8::reversible_workshop
