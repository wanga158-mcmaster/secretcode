#pragma once
#include "mechanics.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

namespace mosaic::adaptive {

inline constexpr unsigned micro_capacity=8;
enum class MicroKind : std::uint8_t {none,basic_camp,basic_champion,cast_self,cast_camp,cast_champion,cast_ground,move};
enum class MicroMovePurpose : std::uint8_t {ingress,exit,reposition};
enum class MicroTargetKind : std::uint8_t {camp,champion};
struct MicroEstimate {float amount{};bool observed{};};
struct MicroThreat {
    float amount{};unsigned owner_starts_before{};bool actual{};
};
struct MicroContext {
    Epoch epoch{};game::NativeId group_id{};bool group_bound{};
    // These are caller-produced physical/funding facts. Optional ability or
    // threat estimates must never be used to turn an admitted Basic off.
    bool work_admitted{},cast_admitted{},quiet{},exit_executable{},exit_required{},preserve_walk{};
    MicroThreat incoming{};
};
struct MicroCamp {
    game::CampFact fact{};bool visible{},visibility_actual{};
    std::array<MicroEstimate,2> damage{};
};
struct MicroChampion {
    Epoch epoch{};game::NativeId id{};Point pose{};int team{};
    float hp{},max_hp{},shield{};int stun_time{};
    bool alive{},body_actual{},buffs_actual{},visible{},visibility_actual{},stun_immune{};
    std::array<MicroEstimate,2> damage{},healing{};
};
struct MicroMove {
    Epoch epoch{};Point point{};MicroMovePurpose purpose{MicroMovePurpose::ingress};
    // SDK-observed legality is conditional attempt permission: an unseen
    // occupant may still return false and preserve M. It is never empty proof.
    bool observed_legal{},funded{},exit_executable{},service_progress{};
    unsigned ability_slot{2}; // 2 is ordinary M; 0/1 nominate that dash slot.
};
struct MicroGround {
    Epoch epoch{};Point point{};bool observed_legal{},funded{},exit_executable{};
};
struct MicroInput {
    game::SelfFact self{};std::array<game::AbilityFact,2> abilities{};
    // Queried spell magnitude for self heal/shield, independent of target
    // estimates. Missing numbers merely omit those optional cast candidates.
    std::array<MicroEstimate,2> self_amount{};
    std::span<const MicroCamp> camps{};std::span<const MicroChampion> champions{};
    std::span<const MicroMove> moves{};std::span<const MicroGround> ground{};
    MicroContext context{};
};
struct MicroAction {
    MicroKind kind{MicroKind::none};unsigned slot{};game::NativeId target_id{},group_id{};
    Point point{};Epoch epoch{};
    friend constexpr bool operator==(MicroAction,MicroAction)=default;
};
struct MicroBenefit {float work{},healing{},shield{};unsigned controls{},targets{},attack_credit{};};
struct MicroChoice {
    MicroAction action{};MicroBenefit benefit{};bool valid{},truncated{};
};
enum class MicroOutcome : std::uint8_t {not_attempted,refused,applied,unknown};
struct MicroRank {
    Epoch epoch{};MicroTargetKind kind{MicroTargetKind::camp};game::NativeId id{},group_id{};
    float hp{},shield{};bool actual{},dead{};
};
struct MicroRankChange {MicroRank before{},after{};};
struct MicroReceipt {
    std::uint64_t event_id{},serial{};MicroAction action{};MicroOutcome outcome{MicroOutcome::not_attempted};
    game::SelfFact before{},after{};std::span<const MicroRankChange> ranks{};bool acknowledged{};
};
class MicroLedger {
public:
    bool observe(const MicroReceipt& receipt);
    // Close a known physical result after its immediate feedback was missed.
    // The fresh body is current acknowledgement only, never an old effect
    // sample or permission to execute the next primitive without fresh guards.
    bool reconcile_ack(std::uint64_t event_id,std::uint64_t serial,const game::SelfFact& fresh);
    std::uint64_t accepted_actions()const noexcept{return accepted_;}
    std::uint64_t raw_refusals()const noexcept{return refused_;}
    std::uint64_t prevalidation_rejections()const noexcept{return prevalidation_;}
    std::uint64_t last_serial()const noexcept{return serial_;}
    std::uint64_t acknowledged_actions()const noexcept{return acknowledged_;}
    float work_removed()const noexcept{return work_;}
    float healing_done()const noexcept{return healing_;}
    float shield_added()const noexcept{return shield_;}
    std::uint64_t deaths()const noexcept{return deaths_;}
    bool pending()const noexcept{return pending_;}
private:
    std::uint64_t event_{},serial_{},accepted_{},refused_{},prevalidation_{},acknowledged_{},deaths_{};
    MicroAction last_action_{};MicroOutcome last_outcome_{MicroOutcome::not_attempted};
    Epoch before_epoch_{},after_epoch_{};game::NativeId before_actor_{};float work_{},healing_{},shield_{};
    bool pending_{},last_ack_{},effects_counted_{},self_effects_counted_{};
};

namespace micro_detail {
inline bool finite_nonnegative(float amount){return std::isfinite(amount)&&amount>=0;}
inline float positive(float amount){return std::isfinite(amount)&&amount>0?amount:0;}
inline unsigned count(std::size_t size){return unsigned(std::min<std::size_t>(size,micro_capacity));}
inline int distance(Point a,Point b){
    const auto dx=std::int64_t(a.x)-b.x,dy=std::int64_t(a.y)-b.y;
    return int(std::min<std::int64_t>(2147483647,std::max(dx<0?-dx:dx,dy<0?-dy:dy)));
}
inline bool in_range(Point a,Point b,float range){return finite_nonnegative(range)&&float(distance(a,b))<=range;}
inline bool camp(const MicroInput& in,const MicroCamp& target){
    return in.context.group_bound&&target.fact.group_id==in.context.group_id&&target.fact.living(in.self.epoch)&&
        target.fact.body_actual&&target.visible&&target.visibility_actual;
}
inline bool champion(const MicroInput& in,const MicroChampion& target){
    return game::same_world(target.epoch,in.self.epoch)&&target.alive&&target.body_actual&&target.visible&&target.visibility_actual;
}
inline float rank(float hp,float shield){
    const auto sum=hp+shield;return finite_nonnegative(hp)&&finite_nonnegative(shield)&&std::isfinite(sum)?sum:0;
}
inline bool compatible(const MicroInput& in,const game::AbilityFact& ability){
    return ability.ready(in.self,in.self.epoch)&&(!ability.descriptor.exclusive_with_movement||
        (in.self.walked_actual&&!in.self.ordinary_walked&&!in.context.preserve_walk));
}
inline float amount(const MicroInput& in,unsigned slot,MicroEstimate estimate){
    if(estimate.observed)return positive(estimate.amount);
    // A queried payload and current Focus can nominate a positive spell. This
    // remains an estimate; only the separate same-identity receipt is progress.
    const auto& descriptor=in.abilities[slot].descriptor;
    return positive(positive(descriptor.modifier)*positive(in.self.focus));
}
inline bool better(const MicroChoice& candidate,const MicroChoice& incumbent){
    if(!candidate.valid)return false;if(!incumbent.valid)return true;
    const auto a=candidate.benefit.work+candidate.benefit.healing+candidate.benefit.shield;
    const auto b=incumbent.benefit.work+incumbent.benefit.healing+incumbent.benefit.shield;
    if(a!=b)return a>b;
    if(candidate.benefit.attack_credit!=incumbent.benefit.attack_credit)return candidate.benefit.attack_credit>incumbent.benefit.attack_credit;
    if(candidate.benefit.controls!=incumbent.benefit.controls)return candidate.benefit.controls>incumbent.benefit.controls;
    return candidate.benefit.targets>incumbent.benefit.targets;
}
inline MicroChoice choice(const MicroInput& in,MicroKind kind,unsigned slot,game::NativeId id,Point point,MicroBenefit benefit){
    return {{kind,slot,id,in.context.group_id,point,in.self.epoch},benefit,true,false};
}
inline bool useful(MicroBenefit value){return value.work>0||value.healing>0||value.shield>0||value.controls||value.attack_credit;}
}

// This selects ONE primitive. No output authorizes a later Basic, assumes a
// killed member survives, or turns a conditional movement mask into emptiness.
// Caller refreshes actual effects/MAC/visibility and calls again after every
// accepted command, refusal or prevalidation change. At most8 entries of each
// span are inspected; the caller supplies its incumbent first.
inline MicroChoice choose_micro(const MicroInput& in){
    using namespace micro_detail;MicroChoice none{};
    const auto& me=in.self;const auto& ctx=in.context;
    const bool truncated=in.camps.size()>micro_capacity||in.champions.size()>micro_capacity||
        in.moves.size()>micro_capacity||in.ground.size()>micro_capacity;
    const auto finish=[&](MicroChoice value){value.truncated=truncated;return value;};
    if(me.epoch.round<0||ctx.epoch!=me.epoch||!me.actual(me.epoch)||!me.alive||me.stunned||me.completed||
       me.recalling||me.submitting||me.teleporting)return finish(none);
    const bool work=ctx.work_admitted&&(ctx.quiet||ctx.exit_executable)&&!ctx.exit_required;
    MicroChoice basic_target{},basic{};
    for(unsigned i=0;work&&i<count(in.camps.size());++i){const auto& target=in.camps[i];
        if(!camp(in,target)||!in_range(me.pose,target.fact.pose,float(me.attack_range)))continue;
        basic_target=choice(in,MicroKind::basic_camp,0,target.fact.id,target.fact.pose,
            {std::min(rank(target.fact.hp,target.fact.shield),positive(me.strength)*positive(me.camp_damage_multiplier)),0,0,0,1,0});break;
    }
    for(unsigned i=0;work&&!basic_target.valid&&i<count(in.champions.size());++i){const auto& target=in.champions[i];
        if(!champion(in,target)||target.team==me.team||!in_range(me.pose,target.pose,float(me.attack_range)))continue;
        basic_target=choice(in,MicroKind::basic_champion,0,target.id,target.pose,
            {std::min(rank(target.hp,target.shield),positive(me.strength)),0,0,0,1,0});
    }
    if(me.can_attack(me.epoch))basic=basic_target;
    MicroChoice cast{};
    const auto consider=[&](MicroChoice value){if(useful(value.benefit)&&better(value,cast))cast=value;};
    for(unsigned slot=0;ctx.cast_admitted&&slot<in.abilities.size();++slot){
        const auto& ability=in.abilities[slot];const auto& descriptor=ability.descriptor;
        if(!compatible(in,ability))continue;
        if(descriptor.kind==game::AbilityKind::reset&&(descriptor.target_mask&game::target_self)&&work&&basic_target.valid&&!me.raw_a&&!me.attack_restricted){
            auto benefit=basic_target.benefit;benefit.attack_credit=1;
            consider(choice(in,MicroKind::cast_self,slot,me.id,me.pose,benefit));
        }
        if(descriptor.kind==game::AbilityKind::shield&&(descriptor.target_mask&game::target_self)&&
           ctx.incoming.actual&&ctx.incoming.owner_starts_before<=1&&positive(ctx.incoming.amount)>0){
            const auto starts=ctx.incoming.owner_starts_before;
            const auto old_shield=starts==0||me.shield_time>int(starts)?positive(me.shield):0;
            const auto new_shield=starts==0||descriptor.duration>int(starts)?amount(in,slot,in.self_amount[slot]):0;
            const auto gain=std::min(new_shield,positive(ctx.incoming.amount))-std::min(old_shield,positive(ctx.incoming.amount));
            if(gain>0)consider(choice(in,MicroKind::cast_self,slot,me.id,me.pose,{0,0,gain,0,1,0}));
        }
        if(descriptor.kind==game::AbilityKind::heal){
            if((descriptor.target_mask&game::target_self)&&finite_nonnegative(me.hp)&&finite_nonnegative(me.max_hp)&&me.max_hp>me.hp){
                const auto healed=std::min(me.max_hp-me.hp,amount(in,slot,in.self_amount[slot]));
                consider(choice(in,MicroKind::cast_champion,slot,me.id,me.pose,{0,healed,0,0,1,0}));
            }
            for(unsigned i=0;i<count(in.champions.size());++i){const auto& target=in.champions[i];
                if(!champion(in,target)||target.team!=me.team||target.id==me.id||!(descriptor.target_mask&game::target_allies)||
                   !in_range(me.pose,target.pose,descriptor.range)||!finite_nonnegative(target.hp)||
                   !finite_nonnegative(target.max_hp)||target.max_hp<=target.hp)continue;
                const auto healed=std::min(target.max_hp-target.hp,amount(in,slot,target.healing[slot]));
                consider(choice(in,MicroKind::cast_champion,slot,target.id,target.pose,{0,healed,0,0,1,0}));
            }
        }
        if(!work)continue;
        const bool damage=descriptor.kind==game::AbilityKind::damage||descriptor.kind==game::AbilityKind::cc;
        if(damage&&descriptor.camps())for(unsigned i=0;i<count(in.camps.size());++i){const auto& target=in.camps[i];
            if(!camp(in,target)||!in_range(me.pose,target.fact.pose,descriptor.range))continue;
            const auto removed=descriptor.kind==game::AbilityKind::damage?
                std::min(rank(target.fact.hp,target.fact.shield),amount(in,slot,target.damage[slot])):0;
            const unsigned controlled=descriptor.duration>0&&target.fact.buffs_actual&&descriptor.duration>target.fact.stun_time;
            consider(choice(in,MicroKind::cast_camp,slot,target.fact.id,target.fact.pose,{removed,0,0,controlled,1,0}));
        }
        if(damage&&descriptor.enemies())for(unsigned i=0;i<count(in.champions.size());++i){const auto& target=in.champions[i];
            if(!champion(in,target)||target.team==me.team||!in_range(me.pose,target.pose,descriptor.range))continue;
            const auto removed=descriptor.kind==game::AbilityKind::damage?
                std::min(rank(target.hp,target.shield),amount(in,slot,target.damage[slot])):0;
            const unsigned controlled=descriptor.duration>0&&target.buffs_actual&&!target.stun_immune&&descriptor.duration>target.stun_time;
            consider(choice(in,MicroKind::cast_champion,slot,target.id,target.pose,{removed,0,0,controlled,1,0}));
        }
        if(descriptor.kind==game::AbilityKind::aoe_damage&&descriptor.ground()&&finite_nonnegative(descriptor.radius))
            for(unsigned aim=0;aim<count(in.ground.size());++aim){const auto& point=in.ground[aim];
                if(point.epoch!=me.epoch||!point.observed_legal||!point.funded||!point.exit_executable||
                   !in_range(me.pose,point.point,descriptor.range))continue;
                MicroBenefit benefit{};
                if(descriptor.camps())for(unsigned i=0;i<count(in.camps.size());++i){const auto& target=in.camps[i];
                    if(!camp(in,target)||!in_range(point.point,target.fact.pose,descriptor.radius))continue;
                    const auto removed=std::min(rank(target.fact.hp,target.fact.shield),amount(in,slot,target.damage[slot]));
                    const unsigned controlled=descriptor.duration>0&&target.fact.buffs_actual&&descriptor.duration>target.fact.stun_time;
                    benefit.work+=removed;benefit.controls+=controlled;benefit.targets+=removed>0||controlled;
                }
                if(descriptor.enemies())for(unsigned i=0;i<count(in.champions.size());++i){const auto& target=in.champions[i];
                    if(!champion(in,target)||target.team==me.team||!in_range(point.point,target.pose,descriptor.radius))continue;
                    const auto removed=std::min(rank(target.hp,target.shield),amount(in,slot,target.damage[slot]));
                    const unsigned controlled=descriptor.duration>0&&target.buffs_actual&&!target.stun_immune&&descriptor.duration>target.stun_time;
                    benefit.work+=removed;benefit.controls+=controlled;benefit.targets+=removed>0||controlled;
                }
                consider(choice(in,MicroKind::cast_ground,slot,0,point.point,benefit));
            }
    }
    if(cast.valid)return finish(cast);
    if(basic.valid)return finish(basic);
    for(unsigned i=0;i<count(in.moves.size());++i){const auto& move=in.moves[i];
        if(move.epoch!=me.epoch||!move.observed_legal||!move.funded||!move.exit_executable||move.point==me.pose||
           (ctx.exit_required&&move.purpose!=MicroMovePurpose::exit))continue;
        bool progresses=move.service_progress||move.purpose==MicroMovePurpose::exit;
        for(unsigned j=0;!progresses&&j<count(in.camps.size());++j)if(camp(in,in.camps[j]))
            progresses=in_range(move.point,in.camps[j].fact.pose,float(me.attack_range))&&
                !in_range(me.pose,in.camps[j].fact.pose,float(me.attack_range));
        if(!progresses||(!ctx.exit_required&&!work))continue;
        if(move.ability_slot<in.abilities.size()){
            const auto& ability=in.abilities[move.ability_slot];
            if(ctx.cast_admitted&&compatible(in,ability)&&ability.descriptor.kind==game::AbilityKind::dash&&
               ability.descriptor.ground()&&in_range(me.pose,move.point,ability.descriptor.range))
                return finish(choice(in,MicroKind::cast_ground,move.ability_slot,0,move.point,{}));
        }else if(move.ability_slot==2&&me.can_move(me.epoch))return finish(choice(in,MicroKind::move,0,0,move.point,{}));
    }
    return finish(none);
}

inline bool MicroLedger::observe(const MicroReceipt& receipt){
    using namespace micro_detail;
    if(!receipt.event_id||receipt.event_id<=event_||receipt.action.kind==MicroKind::none||
       receipt.before.epoch.round<0||!receipt.before.actual(receipt.before.epoch)||receipt.action.epoch!=receipt.before.epoch)return false;
    const bool after=receipt.after.actual(receipt.after.epoch);
    if(after&&receipt.after.id!=receipt.before.id)return false;
    if(receipt.outcome==MicroOutcome::not_attempted){
        if(receipt.serial||!after||receipt.after.epoch!=receipt.before.epoch||receipt.after.pose!=receipt.before.pose||
           receipt.after.raw_m!=receipt.before.raw_m||receipt.after.raw_a!=receipt.before.raw_a||receipt.after.raw_c!=receipt.before.raw_c)return false;
        event_=receipt.event_id;++prevalidation_;return true;
    }
    if(!receipt.serial||receipt.serial<serial_||receipt.after.epoch.round!=receipt.before.epoch.round||
       receipt.before.epoch.attempt==UINT64_MAX||receipt.after.epoch.attempt!=receipt.before.epoch.attempt+1)return false;
    const bool new_attempt=receipt.serial>serial_;
    if(new_attempt&&pending_)return false;
    if(!new_attempt&&(receipt.action!=last_action_||receipt.before.id!=before_actor_||receipt.before.epoch!=before_epoch_||receipt.after.epoch!=after_epoch_||
        (last_outcome_!=MicroOutcome::unknown&&receipt.outcome!=last_outcome_)))return false;
    if(receipt.outcome==MicroOutcome::refused&&after&&
       (receipt.after.pose!=receipt.before.pose||receipt.after.raw_m!=receipt.before.raw_m||
        receipt.after.raw_a!=receipt.before.raw_a||receipt.after.raw_c!=receipt.before.raw_c))return false;
    if(new_attempt){serial_=receipt.serial;last_action_=receipt.action;before_epoch_=receipt.before.epoch;before_actor_=receipt.before.id;
        last_outcome_=MicroOutcome::unknown;last_ack_=effects_counted_=self_effects_counted_=false;}
    event_=receipt.event_id;after_epoch_=receipt.after.epoch;
    if(last_outcome_==MicroOutcome::unknown&&receipt.outcome==MicroOutcome::applied)++accepted_;
    if(last_outcome_==MicroOutcome::unknown&&receipt.outcome==MicroOutcome::refused)++refused_;
    last_outcome_=receipt.outcome;
    if(receipt.acknowledged&&receipt.outcome!=MicroOutcome::unknown&&after&&!last_ack_){last_ack_=true;++acknowledged_;}
    pending_=receipt.outcome==MicroOutcome::unknown||!last_ack_;
    if(receipt.outcome!=MicroOutcome::applied||!last_ack_||!after)return true;
    if(!self_effects_counted_){
        healing_+=positive(receipt.after.hp-receipt.before.hp);shield_+=positive(receipt.after.shield-receipt.before.shield);
        self_effects_counted_=true;
    }
    if(effects_counted_||receipt.ranks.empty())return true;
    for(unsigned i=0;i<count(receipt.ranks.size());++i){const auto& delta=receipt.ranks[i];const auto& a=delta.before;const auto& b=delta.after;
        if(!a.actual||!b.actual||a.epoch!=receipt.before.epoch||b.epoch!=receipt.after.epoch||a.kind!=b.kind||
           a.id!=b.id||a.group_id!=b.group_id||a.dead||!finite_nonnegative(a.hp)||!finite_nonnegative(a.shield)||
           !finite_nonnegative(b.hp)||!finite_nonnegative(b.shield)||(b.dead&&(b.hp!=0||b.shield!=0)))continue;
        if(a.kind==MicroTargetKind::camp&&a.group_id!=receipt.action.group_id)continue;
        bool duplicate=false;for(unsigned j=0;j<i;++j){const auto& prior=receipt.ranks[j].before;
            duplicate|=prior.kind==a.kind&&prior.id==a.id&&prior.group_id==a.group_id;}
        if(duplicate)continue;
        const auto removed=rank(a.hp,a.shield)-rank(b.hp,b.shield);if(removed>0)work_+=removed;
        if(a.kind==MicroTargetKind::champion&&a.id!=receipt.before.id){
            healing_+=positive(b.hp-a.hp);shield_+=positive(b.shield-a.shield);
        }
        if(b.dead)++deaths_;
    }
    effects_counted_=true;return true;
}

inline bool MicroLedger::reconcile_ack(std::uint64_t event_id,std::uint64_t serial,const game::SelfFact& fresh){
    if(!event_id||event_id<=event_||!serial||serial!=serial_||!pending_||last_ack_||
       (last_outcome_!=MicroOutcome::applied&&last_outcome_!=MicroOutcome::refused)||
       fresh.id!=before_actor_||!fresh.actual(fresh.epoch)||fresh.epoch.round<after_epoch_.round||
       (fresh.epoch.round==after_epoch_.round&&
        (fresh.epoch.revision<after_epoch_.revision||fresh.epoch.attempt<after_epoch_.attempt)))return false;
    event_=event_id;last_ack_=true;pending_=false;++acknowledged_;
    // Missing immediate samples stay missing. In particular, home healing,
    // expired shields and another actor's damage in a later input must not be
    // imported as the delayed effect of this known command.
    effects_counted_=self_effects_counted_=true;
    return true;
}

} // namespace mosaic::adaptive
