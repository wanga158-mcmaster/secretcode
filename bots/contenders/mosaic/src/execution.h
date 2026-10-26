#pragma once
#include "observations.h"
#include "budget.h"
#include <exception>
#include <optional>

namespace mosaic::adaptive {

struct ExecutionContext {
    game::SelfFact actual_self{};std::optional<game::CampFact> camp{};int member_ordinal{-1};
    std::optional<MicroChampion> champion{};bool walk_locked{};
    // Fresh legality for THIS selected ground ability. A dash producer must
    // include its trajectory and landing; an ordinary-M mask is not one.
    std::optional<MicroGround> ground{};
    // The current frame may already have paid this exact conditional landing
    // observation. Its attempt/revision date must still match the action.
    std::optional<MicroMove> move{};
};
struct ExecutionResult {
    MicroAction action{};MicroOutcome outcome{MicroOutcome::not_attempted};
    bool attempted{},returned{},after_actual{},pending{},prevalidation{},recorded{},walk_locked{};
    BlockKind blocker{BlockKind::unknown};game::SelfFact before{},after{};Epoch after_epoch{};
    std::optional<game::CampFact> camp_after{};std::optional<MicroChampion> champion_after{};
};

namespace execution_detail {
inline bool camp_action(MicroKind kind){return kind==MicroKind::basic_camp||kind==MicroKind::cast_camp;}
inline bool champion_action(MicroKind kind){return kind==MicroKind::basic_champion||kind==MicroKind::cast_champion;}
inline bool cast_action(MicroKind kind){
    return kind==MicroKind::cast_self||kind==MicroKind::cast_camp||kind==MicroKind::cast_champion||kind==MicroKind::cast_ground;
}
template<class Budget> struct ReserveScope {
    Budget& budget;std::uint64_t outer;
    ~ReserveScope(){budget.reserve(outer);}
};
template<class Obs,class Budget,class Import> bool query(Obs& observations,Budget& budget,Import&& import,int& code){
    if(!budget.before_query()){
        if(Obs::NativeFactsLogic::hard_poison(budget))observations.native_facts().reject();return false;
    }
    try {code=import();}
    catch(...){budget.interrupted();observations.native_facts().reject();throw;}
    if(!budget.after_query()){observations.native_facts().reject();return false;}return true;
}
template<class Obs,class Budget> bool current_round(Obs& observations,Budget& budget){
    std::uint32_t actual{};int code{};
    if(!query(observations,budget,[&]{return detail::sdk_bc_round_number(&actual);},code))return false;
    detail::check(code);
    if(int(actual)!=observations.epoch||int(actual)!=observations.native_facts().epoch.round){
        observations.native_facts().reject();return false;
    }return true;
}
template<class Obs,class Budget> bool camp_read(Obs& observations,Budget& budget,game::CampFact seed,
                                               bool buffs,game::CampFact& out){
    using Logic=typename Obs::NativeFactsLogic;seed.epoch=observations.native_facts().epoch;
    seed.life=game::Life::unknown;seed.life_actual=seed.body_actual=seed.buffs_actual=false;seed.hp=seed.shield=0;
    std::uint32_t life{};int code{};
    if(!query(observations,budget,[&]{return detail::sdk_camp_state(seed.id,&life);},code))return false;
    if(Obs::native_hidden(code)){out=seed;return true;}detail::check(code);
    if(!Logic::camp_life(seed,life))throw std::runtime_error("error unknown adaptive camp life");
    if(seed.life==game::Life::dead){out=seed;return true;}
    detail::camp_state body{};
    if(!query(observations,budget,[&]{return detail::sdk_camp(seed.id,&body);},code))return false;
    if(Obs::native_hidden(code)){out=seed;return true;}detail::check(code);Logic::camp_body(seed,body);
    if(buffs){
        std::array<detail::camp_buff_data,3> raw{};std::uint32_t count{};
        if(!query(observations,budget,[&]{return detail::sdk_camp_buffs(seed.id,raw.data(),raw.size(),&count);},code))return false;
        if(Obs::native_hidden(code)){out=seed;return true;}detail::check(code);
        if(!Logic::camp_buffs(seed,raw.data(),count))throw std::runtime_error("error unknown adaptive camp buff");
    }
    out=seed;return true;
}
inline MicroChampion own_champion(const game::SelfFact& me){
    MicroChampion target{};target.epoch=me.epoch;target.id=me.id;target.pose=me.pose;target.team=me.team;
    target.hp=me.hp;target.max_hp=me.max_hp;target.shield=me.shield;target.stun_time=me.stun_time;
    target.alive=me.alive;target.body_actual=me.body_actual;target.buffs_actual=me.buffs_actual;
    target.visible=target.visibility_actual=true;target.stun_immune=me.stun_immune;return target;
}
}

// One primitive, one raw command at most. The raw-truth callback runs exactly
// once BEFORE any feedback import, after the actual attempt clock advances.
// It owns serial/prefix publication; this component owns no job/custody ledger.
// A known return with missing feedback stays pending. Unknown never becomes
// applied through a resource/pose guess. Budget is templated for controlled
// import fixtures; production passes TurnBudget and its whole-entry clock.
template<class Obs,class Budget,class OnRecord>
ExecutionResult execute_action(Obs& observations,Budget& budget,const MicroAction& action,
                               const ExecutionContext& context,OnRecord&& on_record){
    using namespace execution_detail;ExecutionResult result{};result.action=action;result.walk_locked=context.walk_locked;
    const auto outer=budget.held_tail;ReserveScope<Budget> restore{budget,outer};
    if(action.kind==MicroKind::none)return result;
    const bool camp_target=camp_action(action.kind),champion_target=champion_action(action.kind);
    const bool ground=action.kind==MicroKind::cast_ground;
    const bool read_camp_after=camp_target||(ground&&context.camp.has_value());
    const bool read_champion_after=champion_target||(ground&&context.champion.has_value());
    // Own body/restrictions/buffs/gems and optional channel timer, plus a round
    // guard. Each selected target read is bounded to its exact ID, never a scan.
    const unsigned feedback_queries=6+unsigned(read_camp_after)*3+unsigned(read_champion_after)*3;
    const auto feedback=TurnBudget::mul(feedback_queries,budget.query_bound);
    const auto command_tail=TurnBudget::add(budget.command_bound,feedback);
    if(!budget.admit(TurnBudget::add(outer,command_tail)))return result;
    budget.reserve(TurnBudget::add(outer,command_tail));
    if(!current_round(observations,budget))return result;
    auto me=context.actual_self;const auto now=observations.native_facts().epoch;
    if(!me.actual(now)){
        if(!observations.native_read_self(budget,me,true))return result;
        if(!me.actual(observations.native_facts().epoch))return result;
    }
    observations.native_facts().self=me;result.before=me;
    const auto prevalidate=[&](BlockKind blocker=BlockKind::unknown){
        result.prevalidation=true;result.blocker=blocker;result.after=result.before;result.after_epoch=result.before.epoch;
        result.after_actual=result.before.actual(observations.native_facts().epoch);
        if(result.after_actual&&action.epoch==result.before.epoch){result.recorded=true;on_record(result);}
        return result;
    };
    if(action.epoch!=me.epoch||!me.alive||me.completed||me.stunned||me.recalling||me.submitting||me.teleporting||
       me.epoch.attempt==std::numeric_limits<std::uint64_t>::max()||me.epoch.revision==std::numeric_limits<std::uint64_t>::max())return prevalidate();
    std::optional<game::CampFact> camp=context.camp;
    std::optional<MicroChampion> champion=context.champion;
    if(camp_target){
        bool identity=camp&&camp->id==action.target_id;
        int ordinal=context.member_ordinal;
        if(ordinal<0&&identity)ordinal=camp->member_ordinal;
        if(!identity&&ordinal>=0&&std::size_t(ordinal)<observations.members.size()){
            const auto& member=observations.members[std::size_t(ordinal)];
            if(std::uint32_t(handle_camp_t(member.handle))==action.target_id&&member.group>=0&&std::size_t(member.group)<observations.groups.size()){
                game::CampFact target{};target.id=action.target_id;target.member_ordinal=ordinal;target.group_ordinal=member.group;
                target.group_id=std::uint32_t(handle_camp_group_t(observations.groups[std::size_t(member.group)]));
                target.pose={member.pos.x,member.pos.y};target.range=member.range;camp=target;identity=true;
            }
        }
        if(!identity)return prevalidate(BlockKind::camp);
        if(!camp->actual(me.epoch)||!camp->body_actual){game::CampFact fresh{};if(!camp_read(observations,budget,*camp,false,fresh))return result;camp=fresh;}
        if(!camp->living(me.epoch)||!camp->body_actual||camp->pose.x<0||camp->pose.y<0||camp->group_id!=action.group_id)return prevalidate(BlockKind::camp);
        observations.native_facts().camp=*camp;
    }
    if(champion_target){
        if(action.target_id==me.id)champion=own_champion(me);
        else if(!champion||champion->id!=action.target_id||!game::same_world(champion->epoch,me.epoch)||
                !champion->body_actual||!champion->visibility_actual){
            MicroChampion fresh{};if(!observations.native_read_champion(budget,action.target_id,fresh))return result;champion=fresh;
        }
        if(!champion->alive||!champion->body_actual||!champion->visibility_actual||!champion->visible)return prevalidate(BlockKind::champion);
    }
    game::AbilityFact ability{};
    if(cast_action(action.kind)){
        if(action.slot>=2||!me.can_cast(me.epoch))return prevalidate();
        if(!observations.native_read_ability(budget,action.slot,ability))return result;
        if(!ability.ready(me,me.epoch))return prevalidate();
        if(ability.descriptor.exclusive_with_movement&&(!me.walked_actual||me.ordinary_walked))return prevalidate();
    }
    const auto& descriptor=ability.descriptor;
    switch(action.kind){
    case MicroKind::basic_camp:
        if(!me.can_attack(me.epoch)||!micro_detail::in_range(me.pose,camp->pose,float(me.attack_range)))return prevalidate(BlockKind::camp);
        break;
    case MicroKind::basic_champion:
        if(!me.can_attack(me.epoch)||champion->team==me.team||!micro_detail::in_range(me.pose,champion->pose,float(me.attack_range)))return prevalidate(BlockKind::champion);
        break;
    case MicroKind::cast_self:
        if(action.target_id!=me.id||!(descriptor.target_mask&game::target_self)||
           (descriptor.kind!=game::AbilityKind::reset&&descriptor.kind!=game::AbilityKind::shield)||
           (descriptor.kind==game::AbilityKind::reset&&(me.raw_a||me.attack_restricted)))return prevalidate();
        break;
    case MicroKind::cast_camp:
        if(!descriptor.camps()||(descriptor.kind!=game::AbilityKind::damage&&descriptor.kind!=game::AbilityKind::cc)||
           !micro_detail::in_range(me.pose,camp->pose,descriptor.range))return prevalidate(BlockKind::camp);
        break;
    case MicroKind::cast_champion:{
        const bool own=champion->id==me.id,ally=champion->team==me.team;
        const bool support=descriptor.kind==game::AbilityKind::heal||descriptor.kind==game::AbilityKind::shield;
        const bool damage=descriptor.kind==game::AbilityKind::damage||descriptor.kind==game::AbilityKind::cc;
        const auto mask=own?game::target_self:ally?game::target_allies:game::target_enemies;
        if(!(descriptor.target_mask&mask)||(support?!ally:!damage||ally)||
           !micro_detail::in_range(me.pose,champion->pose,descriptor.range))return prevalidate(BlockKind::champion);
        break;}
    case MicroKind::cast_ground:
        if(!descriptor.ground()||(descriptor.kind!=game::AbilityKind::aoe_damage&&descriptor.kind!=game::AbilityKind::dash)||
           !context.ground||context.ground->epoch!=me.epoch||context.ground->point!=action.point||!context.ground->observed_legal||
           !micro_detail::in_range(me.pose,action.point,descriptor.range))return prevalidate(BlockKind::terrain);
        break;
    case MicroKind::move:{
        if(context.walk_locked||!me.can_move(me.epoch))return prevalidate();
        bool permitted{};BlockKind reason{};
        const bool acquired=context.actual_self.actual(me.epoch)&&context.move&&context.move->epoch==me.epoch&&
            context.move->point==action.point&&context.move->ability_slot==2&&context.move->observed_legal;
        if(acquired)permitted=true;
        else if(!observations.movement(budget,me,action.point,reason,permitted))return result;
        if(!permitted)return prevalidate(reason);
        break;}
    default:return prevalidate();
    }
    if(!current_round(observations,budget))return result;
    budget.reserve(TurnBudget::add(outer,feedback));if(!budget.before_command())return result;
    result.attempted=true;result.pending=true;std::exception_ptr command_error{};bool paid=false;
    try {
        int code{};std::int32_t applied{};
        switch(action.kind){
        case MicroKind::basic_camp:case MicroKind::basic_champion:
            code=detail::sdk_attack(std::uint32_t(camp_target?detail::entity_kind::camp:detail::entity_kind::champion),action.target_id);
            result.returned=true;detail::check(code);result.outcome=MicroOutcome::applied;break;
        case MicroKind::move:
            code=detail::sdk_move_to(action.point.x,action.point.y,&applied);result.returned=true;detail::check(code);
            result.outcome=applied?MicroOutcome::applied:MicroOutcome::refused;break;
        default:{
            const auto kind=action.kind==MicroKind::cast_camp?std::uint32_t(detail::entity_kind::camp):
                action.kind==MicroKind::cast_champion||descriptor.kind==game::AbilityKind::shield?std::uint32_t(detail::entity_kind::champion):0u;
            const auto target=action.kind==MicroKind::cast_self?(descriptor.kind==game::AbilityKind::shield?me.id:0u):ground?0u:action.target_id;
            const auto x=ground?action.point.x:-1,y=ground?action.point.y:-1;
            code=detail::sdk_cast(action.slot,kind,target,x,y,&applied);result.returned=true;detail::check(code);
            result.outcome=applied?MicroOutcome::applied:MicroOutcome::refused;break;}
        }
    }catch(...){result.outcome=MicroOutcome::unknown;command_error=std::current_exception();}
    try {paid=budget.after_command();}catch(...){budget.poisoned=true;budget.in_span=false;if(!command_error)command_error=std::current_exception();}
    const auto scope=action.kind==MicroKind::cast_self?game::EffectScope::self_only:game::EffectScope::position_or_damage;
    if(result.outcome==MicroOutcome::applied){
        if(action.kind==MicroKind::move)observations.walked=true;
        if(cast_action(action.kind)&&descriptor.exclusive_with_movement)result.walk_locked=true;
    }
    observations.native_after_attempt(scope);result.after_epoch=observations.native_facts().epoch;result.after.epoch=result.after_epoch;
    result.recorded=true;on_record(result); // no feedback or target read precedes this publication
    if(!paid||command_error){observations.native_facts().reject();if(command_error)std::rethrow_exception(command_error);return result;}
    // The entire bounded feedback block was admitted before the command. Its
    // own spans now consume that grant; keeping the full quote here would
    // double-reserve it before every read. The caller's outer tail stays held.
    budget.reserve(outer);
    if(!current_round(observations,budget))return result;
    game::SelfFact after{};
    if(!observations.native_read_self(budget,after,true)||!after.actual(observations.native_facts().epoch))return result;
    observations.native_facts().self=after;result.after=after;result.after_actual=true;
    if(result.outcome==MicroOutcome::refused&&(after.pose!=me.pose||after.raw_m!=me.raw_m||after.raw_a!=me.raw_a||after.raw_c!=me.raw_c))
        throw std::runtime_error("error adaptive FALSE changed action resources");
    if(read_camp_after){
        if(camp){game::CampFact fresh{};if(!camp_read(observations,budget,*camp,true,fresh))return result;
            observations.native_facts().camp=fresh;result.camp_after=fresh;}
    }
    if(read_champion_after){
        const auto target=champion?champion->id:context.champion->id;
        if(target==after.id)result.champion_after=own_champion(after);
        else{MicroChampion fresh{};if(!observations.native_read_champion(budget,target,fresh))return result;result.champion_after=fresh;}
    }
    result.pending=false;return result;
}

} // namespace mosaic::adaptive
