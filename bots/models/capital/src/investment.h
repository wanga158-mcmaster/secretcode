#pragma once
#include "capital.h"
#include "../../common/model-runtime.h"

namespace plan4::capital {
struct InvestmentFrame {
    runtime::ObservationClock clock{};
    Stats stats{};
    Equipment equipment{};
    Definitions definitions{};
    Point position{};
    float health=0;
    unsigned cash=0,bag=0;
    Provenance provenance{};
    std::uint64_t kit_version=0,inventory_fingerprint=0;
    std::uint32_t stat_epoch=0;
    int champion=-1,queried_passive=-1,parity_receipts=0;
    bool exact=false,inventory_complete=false,passive_supported=false,remote_shop=false,on_spawn=false,alive=false;
};
struct PurchaseReceipt {
    InvestmentFrame before{},after{};
    Family family=Family::monster_strength;
    unsigned count=0;
    bool isolated_successful_legal_purchase=false;
};
inline bool same_stats(const Stats&a,const Stats&b) {
    return a.maximum_health==b.maximum_health&&a.strength==b.strength&&a.focus==b.focus&&
        a.camp_outgoing==b.camp_outgoing&&a.camp_incoming==b.camp_incoming;
}
inline bool same_definitions(const Definitions&a,const Definitions&b){
    return a.public_source==b.public_source&&a.quote_law==b.quote_law&&a.item==b.item&&a.health_growth==b.health_growth&&
        a.strength_growth==b.strength_growth&&a.focus_growth==b.focus_growth&&a.queried_public==b.queried_public;
}
inline bool exact_investment(const InvestmentFrame& frame) {
    return frame.exact&&frame.inventory_complete&&frame.inventory_fingerprint&&frame.passive_supported&&frame.queried_passive>=0&&
        frame.clock.round>=0&&frame.clock.own.id>=0&&frame.provenance.observed_round==frame.clock.round&&
        frame.provenance.source==Source::own_state&&frame.provenance.status==Status::exact_now&&valid_stats(frame.stats)&&
        std::isfinite(frame.health)&&(frame.alive?frame.health>0:frame.health==0)&&frame.health<=frame.stats.maximum_health&&
        valid_definitions(frame.definitions);
}
inline bool verify_purchase_receipt(const PurchaseReceipt& receipt) {
    const auto&a=receipt.before;const auto&b=receipt.after;const unsigned f=unsigned(receipt.family);
    if(!receipt.isolated_successful_legal_purchase||!receipt.count||f>=3||!exact_investment(a)||!exact_investment(b)||
        a.clock.round!=b.clock.round||a.clock.own.id!=b.clock.own.id||a.champion!=b.champion||a.position!=b.position||
        a.alive!=b.alive||a.on_spawn!=b.on_spawn||a.remote_shop!=b.remote_shop||a.queried_passive!=b.queried_passive||a.bag!=b.bag||
        !same_definitions(a.definitions,b.definitions))return false;
    for(unsigned n=0;n<3;++n) {
        const std::uint64_t expected=std::uint64_t(a.equipment.total_shards[n])+(n==f?receipt.count:0);
        if(expected>std::numeric_limits<unsigned>::max()||b.equipment.total_shards[n]!=expected)return false;
    }
    const ShopAccess access{a.on_spawn,!a.alive,a.remote_shop,true};
    const auto quote=public_quote(a.stats,a.equipment,receipt.family,receipt.count,access,a.definitions,nullptr);
    if(!quote.supported||quote.cost>a.cash||b.cash!=a.cash-quote.cost||!same_stats(b.stats,quote.after))return false;
    const float expected_health=a.alive?std::min(quote.after.maximum_health,a.health+quote.spawn_healing):0;
    return b.health==expected_health;
}
// Coverage is learned only from isolated actual legal purchase receipts. Pure
// fixtures may supply synthetic frames to test the predicate; that is not an
// engine calibration receipt. Each combination mechanism must be witnessed.
struct Parity {
    std::uint64_t public_source=0,quote_law=0;
    unsigned families=0,combinations=0;
    int receipts=0;
    bool failed=false;
    bool observe(const PurchaseReceipt& receipt) {
        const auto&defs=receipt.before.definitions;
        if(!public_source){public_source=defs.public_source;quote_law=defs.quote_law;}
        if(defs.public_source!=public_source||defs.quote_law!=quote_law||!verify_purchase_receipt(receipt)){failed=true;return false;}
        const unsigned f=unsigned(receipt.family);families|=1u<<f;
        if(receipt.before.equipment.total_shards[f]/defs.item[f].combine_count!=
           receipt.after.equipment.total_shards[f]/defs.item[f].combine_count)combinations|=1u<<f;
        ++receipts;return true;
    }
    bool supported(const Definitions& defs) const {
        return !failed&&families==7&&combinations==7&&receipts>=3&&public_source==defs.public_source&&quote_law==defs.quote_law;
    }
};
} // namespace plan4::capital
