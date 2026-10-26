#pragma once
#include "kernel.h"
#include "../../common/model-runtime.h"

namespace plan4::runtime::kernel_binding {
struct ExactKitFrame {
    ObservationClock clock{};
    kernel::ExactKit kit{};
    Provenance provenance{};
    std::uint64_t kit_version=0;
    std::uint32_t stat_epoch=0;
    bool complete_inventory=false,abilities_known=false;
};
inline constexpr int inventory_catalog_limit=256,inventory_budget_chunk=16;
// Pinned SDK ability_kind ABI: zero is a known unused slot, 1..7 are the
// engine's immutable payload kinds. Champion + ordered slot kinds identify
// the design; cooldown/readiness are not kit identity and are never queried.
inline constexpr int maximum_ability_kind=7;
struct KitPhaseBudget {
    static constexpr unsigned allowance=4000,atomic_margin=500,minimum_floor=55000;
    std::uint64_t floor=minimum_floor;
    void begin(std::uint64_t before){floor=std::max(std::uint64_t(minimum_floor),before>allowance?before-allowance:std::uint64_t(minimum_floor));}
    bool permits(std::uint64_t fuel)const{return fuel>=floor&&fuel-floor>=atomic_margin;}
};
struct OwnKitFacts {
    ObservationClock clock{};
    int champion=-1,attack_range=-1;
    float strength=0,focus=0;
};
struct InventoryFacts {const std::uint32_t* counts=nullptr;int count=0;bool complete=false;};
enum class KitFailure { none,source,budget,body,ability,inventory,capacity };
struct KitCollection {ExactKitFrame frame{};KitFailure failure=KitFailure::source;int reads=0,scanned_items=0;};

// Reader performs self-only reads, bounded by the current owner's budget.
// Nothing is published as exact until all body/slot/catalog checks finish.
template<class Reader>
inline void collect_exact_kit(const CombatKit& combat,Reader& reader,KitCollection& out){
    out={};
    out.frame.clock=combat.clock;out.frame.kit_version=combat.version;out.frame.stat_epoch=combat.stat_epoch;
    out.frame.provenance={combat.clock.round,Source::own_state,Status::unknown};
    if(!combat.exact||combat.clock.round<0||combat.clock.own.id<0||!combat.version||combat.champion<0||combat.attack_range<0||
        !std::isfinite(combat.strength)||combat.strength<0||!std::isfinite(combat.focus)||combat.focus<0)return;
    const auto available=[&]{if(reader.available())return true;out.failure=KitFailure::budget;return false;};
    if(!available())return;
    OwnKitFacts own;const bool body=reader.own(own);++out.reads;
    if(!available())return;
    if(!body||own.clock.round!=combat.clock.round||own.clock.own.id!=combat.clock.own.id||own.champion!=combat.champion||
        own.attack_range!=combat.attack_range||own.strength!=combat.strength||own.focus!=combat.focus){out.failure=KitFailure::body;return;}
    kernel::ExactKit candidate;candidate.champion=own.champion;candidate.attack_range=own.attack_range;
    candidate.strength=own.strength;candidate.focus=own.focus;
    for(int slot=0;slot<2;++slot){
        if(!available())return;
        int kind=-1;const bool observed=reader.ability(slot,kind);++out.reads;
        if(!available())return;
        if(!observed||kind<0||kind>maximum_ability_kind){out.failure=KitFailure::ability;return;}
        (slot?candidate.second_ability:candidate.first_ability)=kind;
    }
    if(!available())return;
    InventoryFacts inventory;const bool observed=reader.inventory(inventory);++out.reads;
    if(!available())return;
    if(!observed||!inventory.complete||!inventory.counts||inventory.count<1||inventory.count>inventory_catalog_limit){out.failure=KitFailure::inventory;return;}
    for(int kind=0;kind<inventory.count;++kind){
        if(kind%inventory_budget_chunk==0&&!available())return;
        ++out.scanned_items;
        if(!inventory.counts[kind])continue;
        if(candidate.item_count==kernel::item_limit){out.failure=KitFailure::capacity;return;}
        candidate.items[candidate.item_count++]={kind,inventory.counts[kind]};
    }
    if(!available())return;
    candidate.known=true;out.frame.kit=candidate;out.frame.complete_inventory=out.frame.abilities_known=true;
    out.frame.provenance.status=Status::exact_now;out.failure=KitFailure::none;
}
} // namespace plan4::runtime::kernel_binding
