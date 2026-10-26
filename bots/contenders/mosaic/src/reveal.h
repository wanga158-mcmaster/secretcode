#pragma once
// PAUSED checkpoint: implementation saved; reveal fixture is not yet written.
// No component test, real-SDK syntax check or native qualification has run.
#include "observations.h"
#include "budget.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <span>

namespace mosaic::adaptive {

struct RevealResult {
    MicroOutcome outcome{MicroOutcome::not_attempted};
    bool attempted{},returned{},applied_known{},after_actual{},pending{},recorded{};
    Point placement{};game::SelfFact before{},after{};Epoch after_epoch{};
};
namespace reveal_detail {
template<class Budget> struct ReserveScope {
    Budget& budget;std::uint64_t outer;
    ~ReserveScope(){budget.reserve(outer);}
};
template<class Obs,class Budget,class Import> bool query(Obs& observations,Budget& budget,Import&& import,int& code){
    if(!budget.before_query()){
        if(Obs::NativeFactsLogic::hard_poison(budget))observations.native_facts().reject();
        return false;
    }
    try {code=import();}
    catch(...){budget.interrupted();observations.native_facts().reject();throw;}
    if(!budget.after_query()){observations.native_facts().reject();return false;}
    return true;
}
template<class Obs,class Budget> bool current_round(Obs& observations,Budget& budget){
    std::uint32_t actual{};int code{};
    if(!query(observations,budget,[&]{return detail::sdk_bc_round_number(&actual);},code))return false;
    detail::check(code);
    if(int(actual)!=observations.epoch||int(actual)!=observations.native_facts().epoch.round){
        observations.native_facts().reject();return false;
    }
    return true;
}
inline std::uint64_t distance(Point left,Point right){
    const auto x=std::int64_t(left.x)-right.x,y=std::int64_t(left.y)-right.y;
    return std::uint64_t(std::max(x<0?-x:x,y<0?-y:y));
}
}

// A bounded dependency observation, not a work/target policy or an action
// allowance. The caller invokes it once only when its selected work is hidden
// and it has no executable micro incumbent. Floor and map metadata must be
// complete. Living NPC/champion occupancy is deliberately irrelevant to wards.
template<class Obs,class Budget,class OnRecord>
RevealResult reveal_work(Obs& observations,Budget& budget,const game::SelfFact& me,Point static_goal,
                          std::span<const std::uint8_t> terrain,int width,int height,OnRecord&& on_record){
    using namespace reveal_detail;RevealResult result{};result.before=me;
    const auto outer=budget.held_tail;ReserveScope<Budget> restore{budget,outer};
    if(!me.actual(observations.native_facts().epoch)||!me.alive||me.stunned||me.completed||
       me.recalling||me.submitting||me.teleporting||me.epoch.attempt==std::numeric_limits<std::uint64_t>::max())return result;
    if(width<=0||height<=0||std::size_t(width)>std::numeric_limits<std::size_t>::max()/std::size_t(height)||
       terrain.size()<std::size_t(width)*std::size_t(height))return result;
    const auto& layout=detail::layout();
    constexpr std::size_t mask_words=256;
    if(layout.width!=width||layout.height!=height||!layout.chunk_stride||layout.board_bytes%8||
       !layout.board_bytes||layout.board_bytes>mask_words*sizeof(std::uint64_t)||
       layout.chunk_stride<(std::uint32_t(width)+7u)/8u)return result;
    const auto last_word=std::uint64_t((height-1)>>3)*layout.chunk_stride+std::uint32_t((width-1)>>3);
    if(last_word>=layout.board_bytes/sizeof(std::uint64_t))return result;
    const float placement_radius=constants::ward_placement_radius;
    if(!std::isfinite(placement_radius)||placement_radius<0)return result;
    const int radius=int(std::floor(std::min(placement_radius,2.0f)));
    // Ward cannot start a channel: four self reads (body, restrictions, buffs,
    // custody) plus the post-command round guard form the selected feedback.
    const auto feedback=TurnBudget::mul(5,budget.query_bound);
    const auto tail=TurnBudget::add(budget.command_bound,feedback);
    if(!budget.admit(TurnBudget::add(outer,tail)))return result;
    budget.reserve(TurnBudget::add(outer,tail));
    if(!current_round(observations,budget))return result;
    detail::ward_resources resources{};int code{};
    if(!query(observations,budget,[&]{return detail::sdk_champion_wards(me.id,&resources);},code))return result;
    if(Obs::native_hidden(code))return result;
    detail::check(code);if(!resources.charges)return result;
    // SelfFact keeps movement/attack/cast restrictions, not the ward bit.
    std::uint8_t restrictions{};
    if(!query(observations,budget,[&]{return detail::sdk_champion_restrictions(me.id,&restrictions);},code))return result;
    if(Obs::native_hidden(code))return result;
    detail::check(code);if(restrictions&(1u<<5))return result;
    alignas(16) std::array<std::uint64_t,mask_words> ally_wards;
    if(!query(observations,budget,[&]{return detail::sdk_ally_ward_occupancy(ally_wards.data());},code))return result;
    detail::check(code);
    bool found=false;std::uint64_t best{};Point placement{};
    for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx){
        const auto x=std::int64_t(me.pose.x)+dx,y=std::int64_t(me.pose.y)+dy;
        if(x<0||y<0||x>=width||y>=height)continue;
        const Point point{int(x),int(y)};
        if(!terrain[std::size_t(point.y)*std::size_t(width)+std::size_t(point.x)])continue;
        const auto word=std::size_t(point.y>>3)*layout.chunk_stride+std::size_t(point.x>>3);
        const auto bit=unsigned((point.y&7)*8+(point.x&7));
        if(ally_wards[word]&(std::uint64_t(1)<<bit))continue;
        const auto rank=distance(point,static_goal);
        if(!found||rank<best){found=true;best=rank;placement=point;}
    }
    if(!found)return result;
    if(!budget.checkpoint()||!current_round(observations,budget))return result;
    budget.reserve(TurnBudget::add(outer,feedback));if(!budget.before_command())return result;
    result.placement=placement;result.attempted=true;result.pending=true;
    std::exception_ptr error{};bool paid=false;
    try {
        std::int32_t applied{};code=detail::sdk_place_ward(placement.x,placement.y,&applied);result.returned=true;
        detail::check(code);result.outcome=applied?MicroOutcome::applied:MicroOutcome::refused;result.applied_known=true;
    }catch(...){result.outcome=MicroOutcome::unknown;error=std::current_exception();}
    try {paid=budget.after_command();}
    catch(...){budget.poisoned=true;budget.in_span=false;if(!error)error=std::current_exception();}
    observations.native_after_attempt(game::EffectScope::knowledge_only);
    result.after_epoch=observations.native_facts().epoch;result.after.epoch=result.after_epoch;
    result.recorded=true;on_record(result);
    if(!paid||error){observations.native_facts().reject();if(error)std::rethrow_exception(error);return result;}
    budget.reserve(outer);
    if(!current_round(observations,budget))return result;
    game::SelfFact after{};
    if(!observations.native_read_self(budget,after,true)||!after.actual(observations.native_facts().epoch))return result;
    observations.native_facts().self=after;result.after=after;result.after_actual=true;
    if(after.pose!=me.pose||after.raw_m!=me.raw_m||after.raw_a!=me.raw_a||after.raw_c!=me.raw_c)
        throw std::runtime_error("error adaptive ward changed action allowances");
    result.pending=false;return result;
}

} // namespace mosaic::adaptive
