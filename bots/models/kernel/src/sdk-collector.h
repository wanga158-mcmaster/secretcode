#pragma once
#include "runtime.h"
#include "cambc.h"

namespace plan4::runtime {
namespace kernel_binding {
class OwnKitSource {
    ObservationClock expected_{};
    WorkBudget outer_{};
    KitPhaseBudget phase_{};
    cambc::enum_map<cambc::item_type> inventory_;
public:
    // Leave the common execution reserve and a small M0 evaluation allowance.
    // This independent collection phase never receives first-turn extra fuel.
    OwnKitSource(ObservationClock clock,WorkBudget outer):expected_(clock),outer_(outer){
        phase_.begin(cambc::fuel_remaining());
    }
    bool available()const{
        if(outer_.can_refine&&!outer_.can_refine(outer_.context))return false;
        if(int(cambc::round_number())!=expected_.round)return false;
        return phase_.permits(cambc::fuel_remaining());
    }
    bool own(OwnKitFacts& out){
        if(!available())return false;
        const auto& self=cambc::self();out.clock={expected_.round,{int(static_cast<cambc::handle_champion_t>(self))}};
        if(out.clock.own.id!=expected_.own.id)return false;
        out.champion=int(self.champion_name());
        if(!available())return false;
        out.attack_range=int(self.attack_range());
        if(!available())return false;
        out.strength=self.strength();
        if(!available())return false;
        out.focus=self.focus();return available();
    }
    bool ability(int slot,int& kind){
        if(slot<0||slot>1||!available())return false;
        const auto slots=cambc::self().abilities();
        // Metadata is constructor-cached. Empty is a known unused slot;
        // cached visibility failure remains unknown rather than kind zero.
        try{
            const auto& info=slots[slot].info();
            kind=info?std::visit([](const auto& value){return int(value.kind);},*info):0;
        }catch(const cambc::error& error){
            if(error.code()==cambc::error_code::api_not_in_vision)return false;
            throw;
        }
        return available();
    }
    bool inventory(InventoryFacts& out){
        if(!available())return false;
        constexpr int count=int(cambc::item_type::count);
        if(count<1||count>inventory_catalog_limit)return false;
        inventory_=cambc::self().item_count();
        if(!available())return false;
        out={inventory_.ptr(),count,true};return true;
    }
};
static_assert(int(cambc::ability_kind::shield)==maximum_ability_kind);
} // namespace kernel_binding

// Both mains use this exact collector. It supplies own current kit evidence,
// not a production table or a substitute for all-started/economic receipts.
class KernelLive : public Kernel {
    CombatKit combat_{};
    kernel_binding::KitCollection collection_{};
public:
    explicit KernelLive(bool enabled=true):Kernel(enabled){}
    void observe_combat(const CombatKit& frame){combat_=frame;Kernel::observe_combat(frame);}
    void observe_snapshot(const Information& info,MemberFrame members={}){
        Kernel::observe_snapshot(info,members);
        // Never relabel an earlier kit as exact in this observation round.
        collection_={};
        if(combat_.clock.round!=info.round||combat_.clock.own.id!=info.own.id||!can_refine(info)){
            collection_.frame.clock={info.round,{info.own.id}};
            collection_.failure=can_refine(info)?kernel_binding::KitFailure::source:kernel_binding::KitFailure::budget;
        }else{
            kernel_binding::OwnKitSource source({info.round,{info.own.id}},info.work_budget);
            kernel_binding::collect_exact_kit(combat_,source,collection_);
        }
        observe_exact_kit(collection_.frame);
    }
    const kernel_binding::KitCollection& exact_kit_collection()const{return collection_;}
};
} // namespace plan4::runtime
