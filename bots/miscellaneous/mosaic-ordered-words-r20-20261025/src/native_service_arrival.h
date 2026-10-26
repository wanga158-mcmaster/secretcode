#pragma once
// Arrival facts nominate a service step. The physical executor still performs
// its own fresh authorization, command, feedback and exact-once receipt.
#include "native_service_words.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace mosaic::native_service {
enum class ArrivalKind : std::uint8_t {none,work,bank,capture,lifecycle,observe,locked};
enum class ArrivalPurpose : std::uint8_t {work,capture,bank,exit,recover};
enum ArrivalNeed : unsigned {
    arrival_need_none=0,arrival_need_self=1,arrival_need_life=2,arrival_need_body=4,
    arrival_need_buffs=8,arrival_need_group=16,arrival_need_custody=32,
    arrival_need_lock=64,arrival_need_binding=128,arrival_need_funding=256,
    arrival_need_catalogue=512
};
struct ArrivalRequest {
    ArrivalPurpose purpose{ArrivalPurpose::work};
    int retained_group{-1},retained_member{-1},blocker_member{-1},preferred_altar{-1};
    std::size_t work_cursor{},bank_cursor{};
    bool allow_work{true},allow_bank{true},allow_capture{true},custody_required{},preserve_job{},preserve_member{};
    // The caller may request a Work step under another retained obligation
    // only after its separately owned replacement continuation is funded.
    bool transferred_work{};
};
struct ArrivalBankOffer {
    Epoch epoch{};Point altar{};int ordinal{-1},range{-1};NativeId lock_id{};
    bool static_altar{},lock_actual{},unlocked{};
};
struct ArrivalDecision {
    ArrivalKind kind{ArrivalKind::none};unsigned needs{};Epoch epoch{};
    CampFact camp{};GroupFact group{};CaptureFact capture{};ArrivalBankOffer bank{};
    // Work always identifies the complete camp job to which it contributes.
    constexpr int group_ordinal()const {return camp.group_ordinal;}
    constexpr int member_ordinal()const {return camp.member_ordinal;}
    constexpr bool physical()const {
        return kind==ArrivalKind::work||kind==ArrivalKind::bank||
            (kind==ArrivalKind::capture&&!needs&&group.current(epoch)&&group.id==capture.group_id&&
             group.life==GroupLife::reward&&capture.holding_reward(epoch));
    }
    constexpr Word word()const {
        if(kind==ArrivalKind::work)return single_primitive({PrimitiveKind::basic,camp.id,camp.pose,0},epoch);
        if(kind==ArrivalKind::bank)return single_primitive({PrimitiveKind::bank,0,bank.altar,0},epoch);
        return {};
    }
};
struct ArrivalResult {
    ArrivalDecision decision{};
    std::array<CampFact,2> camps{};std::array<GroupFact,2> groups{};
    // Probe counts bound selected acquisition attempts. Budget.queries records
    // the actually admitted SDK spans; a denied probe is never an SDK result.
    unsigned camp_count{},group_count{},camp_probes{},lock_probes{};
    std::size_t next_work_cursor{},next_bank_cursor{};
    bool more_work{},more_banks{},query_denied{},scan_incomplete{};
};
constexpr bool arrival_channel_ready(const SelfFact& self,Epoch now) {
    return self.actual(now)&&self.alive&&!self.stunned&&!self.recalling&&!self.submitting&&
        !self.teleporting&&!self.completed;
}
constexpr bool arrival_binding(const CampFact& camp) {
    return camp.member_ordinal>=0&&camp.group_ordinal>=0;
}
inline ArrivalDecision arrival_work(const SelfFact& self,const CampFact& camp,
    const GroupFact& group,const CaptureFact& capture,Epoch now,
    bool allow_work=true,bool allow_capture=true) {
    ArrivalDecision out{};out.epoch=now;out.camp=camp;out.group=group;out.capture=capture;
    if(!allow_work&&!allow_capture)return out;
    if(!self.actual(now)){out.kind=ArrivalKind::observe;out.needs=arrival_need_self;return out;}
    if(!arrival_channel_ready(self,now))return out;
    if(!arrival_binding(camp)){out.kind=ArrivalKind::observe;out.needs=arrival_need_binding;return out;}
    if(!camp.actual(now)){out.kind=ArrivalKind::observe;out.needs=arrival_need_life;return out;}
    if(camp.life==Life::dead){
        if(!group.current(now)||group.id!=camp.group_id||group.ordinal!=camp.group_ordinal){
            out.kind=ArrivalKind::observe;out.needs=arrival_need_group;return out;
        }
        if(group.life==GroupLife::reward&&allow_capture){
            out.kind=ArrivalKind::capture;
            // Knowing Reward does not establish occupying a spawn. The caller
            // supplies that independent actual observation when it has it.
            if(capture.epoch!=now||capture.group_id!=group.id||!capture.life_actual||!capture.on_spawn_actual)
                out.needs=arrival_need_binding;
        }else out.kind=ArrivalKind::lifecycle;
        return out;
    }
    if(!camp.living(now)){out.kind=ArrivalKind::observe;out.needs=arrival_need_life;return out;}
    if(!allow_work)return out;
    if(!camp.body_actual){out.kind=ArrivalKind::observe;out.needs=arrival_need_body;return out;}
    if(!camp.buffs_actual){out.kind=ArrivalKind::observe;out.needs=arrival_need_buffs;return out;}
    if(self.can_attack(now)&&distance(self.pose,camp.pose)<=self.attack_range)out.kind=ArrivalKind::work;
    return out;
}
inline ArrivalDecision arrival_bank(const SelfFact& self,const ArrivalBankOffer& offer,Epoch now,bool allow=true) {
    ArrivalDecision out{};out.epoch=now;out.bank=offer;
    if(!allow)return out;
    if(!self.actual(now)){out.kind=ArrivalKind::observe;out.needs=arrival_need_self;return out;}
    if(!arrival_channel_ready(self,now)||self.submit_started)return out;
    if(!self.gems_actual){out.kind=ArrivalKind::observe;out.needs=arrival_need_custody;return out;}
    if(self.gems<=0)return out;
    if(!offer.static_altar||offer.ordinal<0){out.kind=ArrivalKind::observe;out.needs=arrival_need_binding;return out;}
    if(offer.range<0||distance(self.pose,offer.altar)>offer.range)return out;
    if(offer.epoch!=now||!offer.lock_actual){out.kind=ArrivalKind::observe;out.needs=arrival_need_lock;return out;}
    out.kind=offer.unlocked?ArrivalKind::bank:ArrivalKind::locked;return out;
}
inline ArrivalDecision arrival_prefer(const ArrivalDecision& incumbent,const ArrivalDecision& next) {
    if(incumbent.epoch!=next.epoch&&next.epoch.round>=0){
        const bool newer=incumbent.epoch.round<next.epoch.round||
            (incumbent.epoch.round==next.epoch.round&&(incumbent.epoch.revision<next.epoch.revision||
             (incumbent.epoch.revision==next.epoch.revision&&incumbent.epoch.attempt<next.epoch.attempt)));
        if(newer)return next;
        return incumbent;
    }
    // Optional unknown/locked/lifecycle facts cannot erase an already supported
    // primitive or an actual capture hold. Stable caller ordering breaks ties.
    if(incumbent.physical())return incumbent;
    if(next.physical())return next;
    auto selected=incumbent;
    if(incumbent.kind==ArrivalKind::none||
       (next.kind==ArrivalKind::capture&&incumbent.kind!=ArrivalKind::capture)||
       (next.kind==ArrivalKind::lifecycle&&incumbent.kind==ArrivalKind::observe))selected=next;
    selected.needs|=incumbent.needs|next.needs;return selected;
}
} // namespace mosaic::native_service
