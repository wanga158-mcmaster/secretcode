#pragma once
#ifndef MOSAIC_ADAPTIVE_OBSERVATIONS_EXTERNAL_SDK
#include "cambc.h"
#endif
#include "micro.h"
#include "route.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace mosaic::adaptive {
using namespace cambc;

struct ObservationMember {
    camp handle;int group{-1};vec2 pos{};float base_hp{},base_strength{};int range{};
};
struct ObservationMemory {std::size_t begin{},end{};};

// Reads and immutable catalogue bindings only. This adapter neither chooses a
// job nor issues a command. The archived fragment supplies its tested scalar
// readers; their OUT arguments are scratch answers, not automatic publication
// into a retained fact. Optional callers read locals before replacing caches.
struct Observations {
    int epoch{-1};bool walked{};float camp_multiplier{1};passive_type passive{};
    std::vector<map::camp_group> groups;
    std::vector<ObservationMember> members;
    std::vector<ObservationMemory> memory;

    #include "../../../miscellaneous/mosaic-ordered-words-r20-20261025/src/native_service_facts.h"

    // Borrow the parent's COMPLETE immutable SDK-derived passability (walls,
    // holes, altars and enemy spawn excluded), never a visibility/occupancy
    // mask. Retire the binding before releasing or modifying these bytes.
    bool set_terrain(int width,int height,std::span<const std::uint8_t> passable)noexcept {
        terrain_={};width_=height_=0;
        if(width<=0||height<=0||std::size_t(width)>std::numeric_limits<std::size_t>::max()/std::size_t(height))return false;
        const auto cells=std::size_t(width)*std::size_t(height);if(passable.size()<cells)return false;
        width_=width;height_=height;terrain_=passable.first(cells);return true;
    }
    void retire_terrain()noexcept {terrain_={};width_=height_=0;}

    // Hidden/absent is Unknown body/life, not an invented dead champion. The
    // body, buffs and immutable type design all use this VM's SDK perspective.
    // Soft denial leaves the caller's prior output and retained facts intact.
    template<class Budget> bool native_read_champion(Budget& budget,game::NativeId id,MicroChampion& out){
        MicroChampion next{};next.epoch=native_facts().epoch;next.id=id;
        detail::champion_info raw{};int code{};
        if(!observation_span(budget,[&]{return detail::sdk_champion(id,&raw);},code))return false;
        if(champion_hidden(code,next)){out=next;return true;}detail::check(code);
        next.pose={raw.x,raw.y};next.team=raw.team;next.hp=raw.health;next.max_hp=raw.max_health;
        next.alive=(std::uint32_t(raw.state)&1u)!=0;next.body_actual=true;
        next.visible=raw.x>=0&&raw.y>=0;next.visibility_actual=true;
        if(!next.visible){out=next;return true;}
        std::array<detail::buff_data,3> buffs{};std::uint32_t count{};
        if(!observation_span(budget,[&]{return detail::sdk_champion_buff_data(id,buffs.data(),buffs.size(),&count);},code))return false;
        if(champion_hidden(code,next)){out=next;return true;}detail::check(code);
        game::SelfFact converted{};
        if(!NativeFactsLogic::self_buffs(converted,buffs.data(),count))throw std::runtime_error("error unknown adaptive champion buff");
        next.shield=converted.shield;next.stun_time=converted.stun_time;next.buffs_actual=true;
        detail::champion_design design{};
        if(!observation_span(budget,[&]{return detail::sdk_champion_design(raw.type,&design);},code))return false;
        detail::check(code);next.stun_immune=design.definition.passive_type==std::uint32_t(passive_type::stun_immunity);
        out=next;return true;
    }

    // At most one point is inspected here; the caller bounds its nominees to
    // eight. Fresh visible occupancy is queried separately from visibility.
    // An unseen statically legal landing remains conditional TRY permission:
    // no hidden occupancy read, empty claim, command, raw FALSE or M mutation.
    template<class Budget> bool movement(Budget& budget,const game::SelfFact& me,Point to,
                                         BlockKind& reason,bool& permitted){
        reason=BlockKind::unknown;permitted=false;
        if(!me.actual(native_facts().epoch))return false;
        if(!me.can_move(native_facts().epoch))return true;
        if(terrain_.empty())return false;
        const auto dx=std::int64_t(to.x)-me.pose.x,dy=std::int64_t(to.y)-me.pose.y;
        const auto distance=std::max(dx<0?-dx:dx,dy<0?-dy:dy);
        if(to.x<0||to.y<0||to.x>=width_||to.y>=height_||distance==0||distance>me.move_speed||
           terrain_[std::size_t(to.y)*std::size_t(width_)+std::size_t(to.x)]==0){reason=BlockKind::terrain;return true;}
        std::int32_t visible{};int code{};
        if(!observation_span(budget,[&]{return detail::sdk_is_visible(to.x,to.y,&visible);},code))return false;
        detail::check(code);if(!visible){permitted=true;return true;}
        std::int32_t occupied{};
        if(!observation_span(budget,[&]{return detail::sdk_has_camp(to.x,to.y,&occupied);},code))return false;
        if(code==int(error_code::api_not_in_vision)){permitted=true;return true;}detail::check(code);
        if(occupied){reason=BlockKind::camp;return true;}
        if(!observation_span(budget,[&]{return detail::sdk_has_champion(to.x,to.y,&occupied);},code))return false;
        if(code==int(error_code::api_not_in_vision)){permitted=true;return true;}detail::check(code);
        if(occupied){reason=BlockKind::champion;return true;}
        if(!observation_span(budget,[&]{return detail::sdk_has_teleport(to.x,to.y,&occupied);},code))return false;
        if(code==int(error_code::api_not_in_vision)){permitted=true;return true;}detail::check(code);
        if(occupied){reason=BlockKind::teleport;return true;}
        permitted=true;return true;
    }

private:
    std::span<const std::uint8_t> terrain_{};int width_{},height_{};
    static bool champion_hidden(int code,MicroChampion& next){
        if(code!=int(error_code::api_not_in_vision)&&code!=int(error_code::api_not_found))return false;
        next.visible=false;next.visibility_actual=code==int(error_code::api_not_in_vision);
        next.body_actual=next.buffs_actual=false;next.alive=false;return true;
    }
    template<class Budget,class Import> static bool observation_span(Budget& budget,Import&& import,int& code){
        if(!budget.before_query()){
            if(NativeFactsLogic::hard_poison(budget))native_facts().reject();return false;
        }
        code=import();
        if(!budget.after_query()){native_facts().reject();return false;}
        return true;
    }
};

} // namespace mosaic::adaptive
