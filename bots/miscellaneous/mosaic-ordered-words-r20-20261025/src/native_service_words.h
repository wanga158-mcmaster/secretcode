#pragma once
// Pure bounded nominations. No word, prediction, or quote is SDK authority.
// A physical executor refreshes after EVERY attempt and owns the paid exit.
#include "native_service_types.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace mosaic::native_service {
inline constexpr unsigned max_successful_primitives=4,max_attempts=6,max_words=16,max_modeled_camps=32;
enum class PrimitiveKind : std::uint8_t {basic,reset,cast_unit,cast_ground,shield,move,recall,bank};
struct Primitive {
    PrimitiveKind kind{PrimitiveKind::basic}; NativeId target_id{}; Point point{}; unsigned slot{};
    friend constexpr bool operator==(const Primitive&,const Primitive&)=default;
};
enum Dependency : unsigned {self_dependency=1,target_dependency=2,ability_dependency=4,
    geometry_dependency=8,feedback_dependency=16,exit_dependency=32,custody_dependency=64,channel_dependency=128};
struct DependencyCounts {
    unsigned mask{},commands{},self_refreshes{},target_refreshes{},ability_refreshes{},pure_steps{};
};
struct Word {
    Epoch epoch{}; NativeId target_id{}; unsigned count{};
    std::array<Primitive,max_successful_primitives> actions{};
    bool useful_work{},refresh_after_each{true},retarget_on_change{true},requires_paid_exit{true},move_after_work{};
    DependencyCounts dependencies{};
};
// Counts describe dependency acquisition, not qualified fuel prices. The
// executor applies its same whole-entry accounting and feedback tail to macros.
constexpr DependencyCounts primitive_dependencies(PrimitiveKind kind) {
    DependencyCounts counts{self_dependency|feedback_dependency|exit_dependency,1,2,0,0,1};
    switch(kind){
    case PrimitiveKind::basic:counts.mask|=target_dependency;counts.target_refreshes=1;break;
    case PrimitiveKind::cast_unit:case PrimitiveKind::cast_ground:
        counts.mask|=target_dependency;counts.target_refreshes=1;
        counts.mask|=ability_dependency;counts.ability_refreshes=1;break;
    case PrimitiveKind::reset:case PrimitiveKind::shield:
        counts.mask|=ability_dependency;counts.ability_refreshes=1;break;
    case PrimitiveKind::move:counts.mask|=geometry_dependency;break;
    case PrimitiveKind::recall:counts.mask|=channel_dependency;break;
    case PrimitiveKind::bank:counts.mask|=custody_dependency|channel_dependency;break;
    }
    return counts;
}
constexpr Word single_primitive(Primitive primitive,Epoch epoch={},NativeId target_id=0) {
    Word word;word.epoch=epoch;word.target_id=target_id?target_id:primitive.target_id;
    word.count=1;word.actions[0]=primitive;word.useful_work=primitive.kind==PrimitiveKind::basic||
        primitive.kind==PrimitiveKind::cast_unit||primitive.kind==PrimitiveKind::cast_ground;
    word.dependencies=primitive_dependencies(primitive.kind);return word;
}
struct MoveOffer {Epoch epoch{};Point point{};bool present{},terrain_actual{};};
enum Omitted : unsigned {no_omission=0,other_targets=1,other_ground_centers=2,dash_words=4,
    longer_words=8,unknown_numeric=16,capacity_omission=32};
struct WordSet {
    Epoch epoch{};unsigned count{},operations{},omitted{other_targets|other_ground_centers|dash_words|longer_words};
    std::array<Word,max_words> words{};
    // index0 is a physical incumbent, retained independently of numeric value.
    bool incumbent{},bounded{true};
};
inline int distance(Point a,Point b) {
    const auto dx=std::int64_t(a.x)-b.x,dy=std::int64_t(a.y)-b.y;
    const auto ax=dx<0?-dx:dx,ay=dy<0?-dy:dy;
    return int(std::min<std::int64_t>(std::max(ax,ay),std::numeric_limits<int>::max()));
}
inline bool same_epoch(Epoch a,Epoch b){return a==b;}
inline bool matches(const Word& word,const SelfFact& self,const CampFact& target) {
    return word.epoch==self.epoch&&target.id==word.target_id&&target.living(self.epoch);
}
inline bool finite_amount(float value){return std::isfinite(value)&&value>=0;}
// Force each storage operation to binary32, including on native utility hosts.
inline float engine_float(float value){volatile float rounded=value;return rounded;}
struct Residual {float hp{},shield{};};
inline Residual deplete(Residual before,float damage) {
    const float absorbed=std::min(before.shield,damage);
    return {std::max(0.0f,engine_float(before.hp-engine_float(damage-absorbed))),
        std::max(0.0f,engine_float(before.shield-absorbed))};
}
namespace word_detail {
inline bool basic(const SelfFact& self,const CampFact* target,Point pose) {
    return target&&target->body_actual&&self.can_attack(self.epoch)&&target->living(self.epoch)&&
        distance(pose,target->pose)<=self.attack_range;
}
inline bool move(const SelfFact& self,const MoveOffer& offer) {
    return offer.present&&offer.terrain_actual&&offer.epoch==self.epoch&&self.can_move(self.epoch)&&
        offer.point!=self.pose&&distance(self.pose,offer.point)<=self.move_speed;
}
inline bool compatible(const SelfFact& self,const AbilityFact& ability) {
    return !ability.descriptor.exclusive_with_movement||
        (self.walked_actual&&!self.ordinary_walked);
}
inline bool spell(const SelfFact& self,const CampFact* target,const AbilityFact& ability,Point pose,Primitive& out,unsigned slot) {
    const auto& d=ability.descriptor;
    if(!target||!target->body_actual||!target->living(self.epoch)||!ability.ready(self,self.epoch)||!compatible(self,ability)||
       !finite_amount(d.range)||!finite_amount(d.radius)||!finite_amount(d.modifier))return false;
    if(d.kind!=AbilityKind::damage&&d.kind!=AbilityKind::aoe_damage&&d.kind!=AbilityKind::cc)return false;
    // Unit-overload acceptance for AoE is unestablished even if raw target
    // categories contain the camps bit. This supported class is ground-only.
    if(d.kind==AbilityKind::aoe_damage&&!d.ground())return false;
    if(d.modifier<=0&&!d.applies_stun())return false;
    out={PrimitiveKind::cast_unit,target->id,target->pose,slot};
    if(d.ground()) {
        // The selected body's tile supplies a canonical center. At an outer
        // perch, clip that center to cast reach while retaining radius reach.
        const auto span=distance(pose,target->pose);
        const int reach=int(std::min(d.range,float(std::numeric_limits<int>::max()/2)));
        if(float(span)>d.range+d.radius)return false;
        if(span>reach){
            out.point.x=pose.x+std::clamp(target->pose.x-pose.x,-reach,reach);
            out.point.y=pose.y+std::clamp(target->pose.y-pose.y,-reach,reach);
        }
        if(float(distance(out.point,target->pose))>d.radius)return false;
        out.kind=PrimitiveKind::cast_ground;return true;
    }
    return d.camps()&&float(distance(pose,target->pose))<=d.range;
}
inline void add(WordSet& set,const SelfFact& self,const CampFact* target,std::span<const Primitive> actions,bool work) {
    ++set.operations;
    if(actions.empty()||actions.size()>max_successful_primitives)return;
    for(unsigned i=0;i<set.count;++i){
        if(set.words[i].count!=actions.size())continue;
        bool equal=true;for(unsigned j=0;j<actions.size();++j)equal=equal&&set.words[i].actions[j]==actions[j];
        if(equal)return;
    }
    if(set.count==max_words){set.omitted|=capacity_omission;return;}
    auto& word=set.words[set.count++];word.epoch=self.epoch;word.target_id=target?target->id:0;
    word.count=unsigned(actions.size());word.useful_work=work;
    word.dependencies.mask=self_dependency|feedback_dependency|exit_dependency;
    word.dependencies.commands=word.count;word.dependencies.self_refreshes=word.count+1;
    for(unsigned j=0;j<word.count;++j){
        word.actions[j]=actions[j];
        if(actions[j].kind==PrimitiveKind::move){word.dependencies.mask|=geometry_dependency;word.move_after_work=j>0&&work;}
        else if(actions[j].kind==PrimitiveKind::basic){word.dependencies.mask|=target_dependency;++word.dependencies.target_refreshes;}
        else {word.dependencies.mask|=ability_dependency;++word.dependencies.ability_refreshes;
            if(actions[j].kind==PrimitiveKind::cast_unit||actions[j].kind==PrimitiveKind::cast_ground){
                word.dependencies.mask|=target_dependency;++word.dependencies.target_refreshes;}}
    }
    word.dependencies.pure_steps=word.count;
    set.incumbent=true;
}
inline void work_word(WordSet& set,const SelfFact& self,const CampFact* target,
                      std::array<Primitive,4> actions,unsigned count,const MoveOffer& offer,bool preserves_move) {
    // The shorter complete work prefix remains playable when the optional
    // movement/refusal tail cannot be funded in this input.
    add(set,self,target,std::span<const Primitive>(actions.data(),count),true);
    if(preserves_move&&move(self,offer)&&count<max_successful_primitives)
        actions[count++]={PrimitiveKind::move,0,offer.point,0};
    add(set,self,target,std::span<const Primitive>(actions.data(),count),true);
}
}
// Fixed schemas, no recursive frontier or numeric ranking. At most16 words,
// four successes and six physical attempts including refusals per input.
inline WordSet build_words(const SelfFact& self,const CampFact* target,
                           const std::array<AbilityFact,2>& abilities,const MoveOffer& offer={}) {
    WordSet out;out.epoch=self.epoch;
    const bool hit=word_detail::basic(self,target,self.pose),step=word_detail::move(self,offer);
    const Primitive attack{PrimitiveKind::basic,target?target->id:0,target?target->pose:Point{},0};
    const Primitive walk{PrimitiveKind::move,0,offer.point,0};
    std::array<Primitive,4> actions{};
    // Construct executable incumbent BEFORE inspecting optional ability data.
    if(hit){actions[0]=attack;word_detail::add(out,self,target,{actions.data(),1},true);}
    if(hit&&step&&!self.ranged){actions={attack,walk};word_detail::add(out,self,target,{actions.data(),2},true);}
    for(unsigned slot=0;slot<abilities.size();++slot){
        ++out.operations;const auto& ability=abilities[slot];const auto& d=ability.descriptor;
        if(!ability.ready(self,self.epoch)||!word_detail::compatible(self,ability))continue;
        auto reset_self=self;reset_self.raw_a=true;
        if(d.kind==AbilityKind::reset&&(d.target_mask&target_self)&&target&&target->body_actual&&target->living(self.epoch)&&reset_self.can_attack(self.epoch)){
            const Primitive reset{PrimitiveKind::reset,self.id,self.pose,slot};
            if(hit){actions={attack,reset,attack};word_detail::work_word(out,self,target,actions,3,offer,!self.ranged);}
            else if(!self.raw_a&&distance(self.pose,target->pose)<=self.attack_range){
                actions={reset,attack};word_detail::work_word(out,self,target,actions,2,offer,self.raw_m&&!self.ranged);}
            if(step&&(self.ranged||!self.raw_a)&&distance(offer.point,target->pose)<=self.attack_range&&
               !d.exclusive_with_movement){
                actions={walk,reset,{PrimitiveKind::basic,target->id,target->pose,0}};
                word_detail::add(out,self,target,{actions.data(),3},true);}
            continue;
        }
        Primitive cast;bool current=false;
        if(d.kind==AbilityKind::shield&&(d.target_mask&target_self)&&finite_amount(d.modifier)&&
           (!self.buffs_actual||engine_float(d.modifier*self.focus)>self.shield))
            {cast={PrimitiveKind::shield,self.id,self.pose,slot};current=true;}
        else current=word_detail::spell(self,target,ability,self.pose,cast,slot);
        const bool shield=cast.kind==PrimitiveKind::shield;
        if(current&&hit){
            actions={cast,attack};word_detail::work_word(out,self,target,actions,2,offer,!self.ranged&&!d.exclusive_with_movement);
            actions={attack,cast};word_detail::work_word(out,self,target,actions,2,offer,!self.ranged&&!d.exclusive_with_movement);
        }else if(current&&!shield){actions={cast};word_detail::work_word(out,self,target,actions,1,offer,!d.exclusive_with_movement);}
        else if(current&&shield){actions={cast};unsigned count=1;
            if(step&&!d.exclusive_with_movement)actions[count++]=walk;
            word_detail::add(out,self,target,{actions.data(),count},false);}
        // An ordinary step can enable a compatible ability and melee Basic.
        if(step&&!d.exclusive_with_movement&&!shield&&
           word_detail::spell(self,target,ability,offer.point,cast,slot)){
            actions={walk,cast};unsigned count=2;
            if(!self.ranged&&self.can_attack(self.epoch)&&distance(offer.point,target->pose)<=self.attack_range)
                actions[count++]={PrimitiveKind::basic,target->id,target->pose,0};
            word_detail::add(out,self,target,{actions.data(),count},true);
        }
    }
    if(step){
        if(target&&target->body_actual&&target->living(self.epoch)&&!self.ranged&&self.can_attack(self.epoch)&&
           distance(offer.point,target->pose)<=self.attack_range){
            actions={walk,attack};word_detail::add(out,self,target,{actions.data(),2},true);}
        actions={walk};word_detail::add(out,self,target,{actions.data(),1},false);
    }
    return out;
}

// Optional enrichment has a declared domain and never alters WordSet.
// Unknown identities stay in this view even when they have no numeric value.
struct UnknownSource {NativeId id{};Point center{};unsigned radius{};float damage_envelope{};bool envelope_known{};};
enum class Domain : std::uint8_t {initial_npc_passive,declared_next_batch};
struct Scenario {
    Epoch epoch{};std::uint64_t assumption_stamp{};Domain domain{Domain::initial_npc_passive};
    unsigned enemy_opportunities{}; // 0/1/2 are champion inputs, NEVER CAMP batches.
    bool next_owner_scheduled{},no_exogenous_wake{},no_respawn{},camp_domain_complete{},passive_domain_complete{};
};
struct Support {
    std::span<const CampFact> camps{};std::span<const UnknownSource> unknown{};
    float camp_damage_taken{1};int basic_splash_radius{},enhanced_stun_duration{};
    bool damage_products_exact{true},passives_actual{};
};
struct WordQuote {
    Epoch epoch{};std::uint64_t assumption_stamp{};Domain domain{Domain::initial_npc_passive};
    bool immediate_known{},next_batch_known{},full_support{};
    float immediate_hp_damage{},post_batch_hp{},post_batch_shield{};
    unsigned predicted_kills{},camp_batches{},enemy_opportunities{},unknown_identities{},operations{};
    bool target_changed{},unknown_numeric{},capacity_exceeded{},noncombat{},compatible{true};
};
inline bool comparable(const WordQuote& a,const WordQuote& b) {
    return !a.noncombat&&!b.noncombat&&a.epoch==b.epoch&&a.assumption_stamp==b.assumption_stamp&&a.domain==b.domain&&a.camp_batches==b.camp_batches&&
        a.enemy_opportunities==b.enemy_opportunities;
}
namespace word_detail {
struct ModeledCamp {CampFact fact{};bool known{},awake{},stun{};};
inline bool numeric(const CampFact& camp,Epoch now){return camp.actual(now)&&camp.body_actual&&camp.buffs_actual&&
    finite_amount(camp.hp)&&finite_amount(camp.shield)&&finite_amount(camp.strength)&&camp.range>=0;}
}
inline WordQuote quote_word(const Word& word,const SelfFact& self,const CampFact& selected,
                            const std::array<AbilityFact,2>& abilities,const Support& support,const Scenario& scenario) {
    WordQuote quote;quote.epoch=self.epoch;quote.assumption_stamp=scenario.assumption_stamp;quote.domain=scenario.domain;
    quote.enemy_opportunities=scenario.enemy_opportunities;
    quote.unknown_identities=unsigned(support.unknown.size());
    if(word.count>max_successful_primitives){quote.unknown_numeric=true;return quote;}
    for(unsigned i=0;i<word.count;++i)if(word.actions[i].kind==PrimitiveKind::recall||word.actions[i].kind==PrimitiveKind::bank){
        quote.noncombat=true;quote.unknown_numeric=true;return quote;}
    if(word.epoch!=self.epoch||scenario.epoch!=self.epoch||!self.actual(self.epoch)||
       scenario.enemy_opportunities>2){quote.unknown_numeric=true;return quote;}
    if(support.camps.size()>max_modeled_camps){quote.capacity_exceeded=true;quote.unknown_numeric=true;return quote;}
    std::array<word_detail::ModeledCamp,max_modeled_camps> camps{};unsigned count=0;bool contains=false;
    for(const auto& camp:support.camps){
        contains=contains||camp.id==selected.id;
        camps[count++]={camp,word_detail::numeric(camp,self.epoch),camp.life==Life::awake,camp.stunned&&camp.stun_time>0};
    }
    if(!contains){if(count==max_modeled_camps){quote.capacity_exceeded=true;quote.unknown_numeric=true;return quote;}
        camps[count++]={selected,word_detail::numeric(selected,self.epoch),selected.life==Life::awake,selected.stunned&&selected.stun_time>0};}
    // Engine camp order is native id order. Reordering sources changes f32
    // subtraction in general, so caller enumeration order is not a premise.
    for(unsigned i=1;i<count;++i){auto camp=camps[i];unsigned j=i;
        while(j>0&&camps[j-1].fact.id>camp.fact.id){camps[j]=camps[j-1];--j;}camps[j]=camp;}
    bool all_known=true;for(unsigned i=0;i<count;++i)all_known=all_known&&camps[i].known;
    for(unsigned i=1;i<count;++i)if(camps[i-1].fact.id==camps[i].fact.id)all_known=false;
    bool work_known=finite_amount(self.strength)&&finite_amount(self.focus)&&finite_amount(self.camp_damage_multiplier)&&
        support.passives_actual&&support.damage_products_exact;
    Point pose=self.pose;float shield=self.shield;int shield_time=self.shield_time;
    bool enhanced=self.enhanced&&self.enhanced_time>=0;
    bool can_m=self.can_move(self.epoch),can_a=self.can_attack(self.epoch),can_c=self.can_cast(self.epoch);
    bool walked=self.ordinary_walked,walk_locked=false;
    for(unsigned action=0;action<word.count;++action){
        ++quote.operations;const auto& primitive=word.actions[action];
        if(primitive.kind==PrimitiveKind::move){
            if(!can_m||walk_locked||distance(pose,primitive.point)>self.move_speed||pose==primitive.point){quote.compatible=false;break;}
            pose=primitive.point;can_m=false;walked=true;if(self.ranged)can_a=false;continue;}
        if(primitive.slot>=abilities.size()){work_known=false;continue;}
        const auto& descriptor=abilities[primitive.slot].descriptor;
        const bool basic=primitive.kind==PrimitiveKind::basic;
        if(basic||primitive.kind==PrimitiveKind::cast_unit){
            bool found=false;for(unsigned i=0;i<count;++i)if(camps[i].fact.id==primitive.target_id){
                found=true;if(camps[i].known&&camps[i].fact.hp<=0){quote.target_changed=true;quote.compatible=false;}}
            if(!found)work_known=false;
            // A dead direct target cannot enable another splash or unit cast.
            // Stop this model suffix; actual feedback nominates a new word.
            if(!quote.compatible)break;
        }
        if(basic){
            if(!can_a){quote.compatible=false;break;}
            bool found=false;for(unsigned i=0;i<count;++i)if(camps[i].fact.id==primitive.target_id){
                found=true;if(distance(pose,camps[i].fact.pose)>self.attack_range)quote.compatible=false;}
            if(!found)work_known=false;
            if(!quote.compatible)break;
            can_a=false;if(self.ranged)can_m=false;
        }else{
            if(!can_c||!abilities[primitive.slot].ready(self,self.epoch)||
               (descriptor.exclusive_with_movement&&(!self.walked_actual||walked))){quote.compatible=false;break;}
            if(!finite_amount(descriptor.modifier)||!finite_amount(descriptor.range)||!finite_amount(descriptor.radius)||
               descriptor.radius>float(std::numeric_limits<int>::max()/2)){work_known=false;break;}
            if((primitive.kind==PrimitiveKind::reset||primitive.kind==PrimitiveKind::shield)&&!(descriptor.target_mask&target_self)){
                quote.compatible=false;break;}
            if(primitive.kind==PrimitiveKind::cast_ground&&!descriptor.ground()){quote.compatible=false;break;}
            if(primitive.kind==PrimitiveKind::cast_unit&&(!descriptor.camps()||descriptor.ground()||descriptor.kind==AbilityKind::aoe_damage)){
                quote.compatible=false;break;}
            if((primitive.kind==PrimitiveKind::cast_unit||primitive.kind==PrimitiveKind::cast_ground)&&
               descriptor.kind!=AbilityKind::damage&&descriptor.kind!=AbilityKind::aoe_damage&&descriptor.kind!=AbilityKind::cc){
                quote.compatible=false;break;}
            can_c=false;if(descriptor.exclusive_with_movement){can_m=false;walk_locked=true;}
        }
        if(primitive.kind==PrimitiveKind::reset){
            if(can_a||descriptor.kind!=AbilityKind::reset){quote.compatible=false;break;}
            auto restored=self;restored.raw_a=true;can_a=restored.can_attack(self.epoch);
            enhanced=descriptor.enhanced_attack;continue;}
        if(primitive.kind==PrimitiveKind::shield){
            if(descriptor.kind!=AbilityKind::shield){quote.compatible=false;break;}
            shield=engine_float(descriptor.modifier*self.focus);shield_time=descriptor.duration;continue;}
        if(!basic&&float(distance(pose,primitive.point))>descriptor.range){quote.compatible=false;break;}
        const float raw=basic?self.strength:engine_float(descriptor.modifier*self.focus);
        const float damage=engine_float(raw*self.camp_damage_multiplier);
        if(!finite_amount(damage)){work_known=false;continue;}
        const int radius=basic?(self.basic_splash?support.basic_splash_radius:0):int(descriptor.radius);
        const int stun=basic?(enhanced?support.enhanced_stun_duration:0):(descriptor.applies_stun()?descriptor.duration:0);
        bool direct_found=primitive.kind==PrimitiveKind::cast_ground;
        for(unsigned i=0;i<count;++i){
            auto& victim=camps[i];++quote.operations;
            if(victim.fact.id==primitive.target_id)direct_found=true;
            const bool affected=basic||primitive.kind==PrimitiveKind::cast_unit?
                (victim.fact.id==primitive.target_id||(radius>0&&distance(victim.fact.pose,primitive.point)<=radius)):
                float(distance(victim.fact.pose,primitive.point))<=descriptor.radius;
            if(!affected||victim.fact.life==Life::dead)continue;
            if(!victim.known){work_known=false;continue;}
            if(victim.fact.hp<=0){quote.target_changed=true;work_known=false;continue;}
            if(damage>0){
                // Every affected body wakes its WHOLE group, including groups
                // reached by Varg splash or AoE. No stale asleep exclusion.
                for(unsigned j=0;j<count;++j)if(camps[j].fact.group_id==victim.fact.group_id)camps[j].awake=true;
                const auto after=deplete({victim.fact.hp,victim.fact.shield},damage);
                quote.immediate_hp_damage+=engine_float(victim.fact.hp-after.hp);
                if(after.hp==0)++quote.predicted_kills;
                victim.fact.hp=after.hp;victim.fact.shield=after.shield;
            }
            if(stun>0)victim.stun=true;
        }
        if(!direct_found)work_known=false;
        if(basic)enhanced=false; // Marcella's enhanced stun is consumed once.
    }
    quote.immediate_known=work_known&&quote.compatible;quote.unknown_numeric=!quote.immediate_known;
    quote.post_batch_hp=self.hp;quote.post_batch_shield=shield;
    if(!scenario.next_owner_scheduled)return quote;
    quote.camp_batches=1;
    bool batch_known=all_known&&quote.immediate_known&&scenario.no_exogenous_wake&&scenario.no_respawn&&
        finite_amount(self.hp)&&finite_amount(shield)&&finite_amount(support.camp_damage_taken);
    // Owner end decrements a timer1 shield to0; it still covers the NEXT CAMP
    // before the next owner start expires it. There is no CAMP inside the word.
    if(shield_time<1)quote.post_batch_shield=0;
    for(unsigned i=0;i<count;++i){
        const auto& camp=camps[i];++quote.operations;
        if(!camp.known){batch_known=false;continue;}
        if(camp.fact.life==Life::dead||camp.fact.hp<=0||camp.stun||!camp.awake||distance(pose,camp.fact.pose)>camp.fact.range)continue;
        const auto after=deplete({quote.post_batch_hp,quote.post_batch_shield},engine_float(camp.fact.strength*support.camp_damage_taken));
        quote.post_batch_hp=after.hp;quote.post_batch_shield=after.shield;
    }
    quote.next_batch_known=batch_known;
    // This only covers the declared NPC/passive domain. Enemy opportunities
    // remain explicitly unpriced, and unknown identities never disappear.
    quote.full_support=batch_known&&support.unknown.empty()&&scenario.camp_domain_complete&&
        scenario.passive_domain_complete&&scenario.enemy_opportunities==0;
    return quote;
}
}
