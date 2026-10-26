#include "native_service_words.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <limits>
using namespace mosaic::native_service;
namespace {
unsigned checks{};
void check(bool condition){++checks;assert(condition);}
SelfFact self_fact(){
    SelfFact s;s.epoch={9,3,0};s.id=1;s.pose={0,0};s.hp=s.max_hp=1000;s.strength=45;s.focus=100;
    s.attack_range=s.move_speed=1;s.raw_m=s.raw_a=s.raw_c=s.alive=true;
    s.body_actual=s.restrictions_actual=s.buffs_actual=s.channel_actual=s.walked_actual=true;return s;
}
CampFact camp_fact(const SelfFact& s,NativeId id=10,Point pose={1,0},NativeId group=100){
    CampFact c;c.epoch=s.epoch;c.id=id;c.group_id=group;c.pose=pose;c.life=Life::awake;
    c.hp=c.max_hp=100000;c.strength=60;c.range=3;c.life_actual=c.body_actual=c.buffs_actual=true;return c;
}
AbilityFact ability(const SelfFact& s,AbilityKind kind,unsigned mask,float range=4,float modifier=1,float radius=0,int duration=0,bool exclusive=false){
    AbilityFact a;a.epoch=s.epoch;a.cooldown_actual=true;
    a.descriptor={kind,mask,range,modifier,radius,duration,8,exclusive,false,true,true};return a;
}
Scenario scenario(const SelfFact& s){
    Scenario value;value.epoch=s.epoch;value.assumption_stamp=123;value.domain=Domain::declared_next_batch;
    value.next_owner_scheduled=value.no_exogenous_wake=value.no_respawn=value.camp_domain_complete=value.passive_domain_complete=true;
    return value;
}
Support support(std::span<const CampFact> camps){Support value;value.camps=camps;value.passives_actual=true;
    value.basic_splash_radius=1;value.enhanced_stun_duration=1;return value;}
const Word* find(const WordSet& words,std::initializer_list<PrimitiveKind> kinds){
    for(unsigned i=0;i<words.count;++i){if(words.words[i].count!=kinds.size())continue;
        unsigned j=0;bool equal=true;for(auto kind:kinds)equal=equal&&words.words[i].actions[j++].kind==kind;
        if(equal)return &words.words[i];}return nullptr;
}
Word manual(const SelfFact& self,const CampFact& camp,std::initializer_list<Primitive> actions){
    Word word;word.epoch=self.epoch;word.target_id=camp.id;word.count=unsigned(actions.size());
    unsigned i=0;for(const auto& action:actions)word.actions[i++]=action;return word;
}
}
int main(){
    auto s=self_fact();auto c=camp_fact(s);std::array<AbilityFact,2> abilities{};
    const MoveOffer move{s.epoch,{-1,0},true,true};
    auto words=build_words(s,&c,abilities,move);
    check(words.incumbent&&words.words[0].count==1&&words.words[0].actions[0].kind==PrimitiveKind::basic);
    check(find(words,{PrimitiveKind::basic,PrimitiveKind::move}));
    check(words.words[0].requires_paid_exit&&words.words[0].refresh_after_each&&words.words[0].retarget_on_change);
    check(words.words[0].dependencies.commands==1&&words.words[0].dependencies.self_refreshes==2);
    // Numeric ignorance never erases a playable direct Basic.
    c.hp=std::numeric_limits<float>::quiet_NaN();abilities[0].descriptor.range=c.hp;
    words=build_words(s,&c,abilities,move);check(words.incumbent&&words.words[0].actions[0].kind==PrimitiveKind::basic);
    auto q=quote_word(words.words[0],s,c,abilities,support({}),scenario(s));check(!q.immediate_known&&!q.full_support);
    c.shield=std::numeric_limits<float>::quiet_NaN();words=build_words(s,&c,abilities,{});
    check(words.incumbent&&words.words[0].actions[0].kind==PrimitiveKind::basic);
    c.life_actual=false;check(!build_words(s,&c,abilities,{}).incumbent);
    c=camp_fact(s);abilities[0]=ability(s,AbilityKind::reset,target_self,0,0);abilities[0].descriptor.enhanced_attack=true;
    words=build_words(s,&c,abilities,move);
    check(find(words,{PrimitiveKind::basic,PrimitiveKind::reset,PrimitiveKind::basic}));
    check(find(words,{PrimitiveKind::basic,PrimitiveKind::reset,PrimitiveKind::basic,PrimitiveKind::move}));
    auto spent=s;spent.raw_a=false;
    auto spent_abilities=abilities;spent_abilities[0].epoch=spent.epoch;
    const auto after_basic=build_words(spent,&c,spent_abilities,move);
    check(find(after_basic,{PrimitiveKind::reset,PrimitiveKind::basic}));
    check(find(after_basic,{PrimitiveKind::reset,PrimitiveKind::basic,PrimitiveKind::move}));
    const auto* reset=find(words,{PrimitiveKind::basic,PrimitiveKind::reset,PrimitiveKind::basic,PrimitiveKind::move});check(reset);
    check(reset->count<=max_successful_primitives&&reset->move_after_work);
    // Marcella's reset grants enhanced stun; immunity alone does not.
    std::array<CampFact,1> one{c};s.stun_immune=true;
    auto basic=build_words(s,&c,{},{}).words[0];q=quote_word(basic,s,c,abilities,support(one),scenario(s));check(q.post_batch_hp==940);
    words=build_words(s,&c,abilities,{});reset=find(words,{PrimitiveKind::basic,PrimitiveKind::reset,PrimitiveKind::basic});check(reset);
    q=quote_word(*reset,s,c,abilities,support(one),scenario(s));check(q.compatible&&q.immediate_known&&q.post_batch_hp==1000);
    // A later normal reset cannot preserve an already consumed enhancement.
    auto c2=camp_fact(s,11,{0,1},101);std::array<CampFact,2> two{c,c2};s.enhanced=true;
    abilities[0].descriptor.enhanced_attack=false;
    auto changed=manual(s,c,{{PrimitiveKind::basic,c.id,c.pose,0},{PrimitiveKind::reset,s.id,s.pose,0},{PrimitiveKind::basic,c2.id,c2.pose,0}});
    q=quote_word(changed,s,c,abilities,support(two),scenario(s));check(q.compatible&&q.post_batch_hp==940);s.enhanced=false;
    // Ranged Basic spends M, Move spends A, reset restores only A.
    s.ranged=true;s.attack_range=3;c=camp_fact(s,10,{2,0});abilities[0]=ability(s,AbilityKind::reset,target_self,0,0);
    const MoveOffer ranged_move{s.epoch,{1,0},true,true};words=build_words(s,&c,abilities,ranged_move);
    check(find(words,{PrimitiveKind::basic,PrimitiveKind::reset,PrimitiveKind::basic}));
    check(!find(words,{PrimitiveKind::basic,PrimitiveKind::move}));
    check(find(words,{PrimitiveKind::move,PrimitiveKind::reset,PrimitiveKind::basic}));
    one={c};q=quote_word(*find(words,{PrimitiveKind::move,PrimitiveKind::reset,PrimitiveKind::basic}),s,c,abilities,support(one),scenario(s));check(q.compatible);
    auto illegal=manual(s,c,{{PrimitiveKind::move,0,{1,0},0},{PrimitiveKind::basic,c.id,c.pose,0}});
    q=quote_word(illegal,s,c,abilities,support(one),scenario(s));check(!q.compatible&&!q.full_support);
    illegal=manual(s,c,{{PrimitiveKind::reset,s.id,s.pose,0},{PrimitiveKind::basic,c.id,c.pose,0}});
    q=quote_word(illegal,s,c,abilities,support(one),scenario(s));check(!q.compatible);
    s.raw_a=false;words=build_words(s,&c,abilities,{});check(find(words,{PrimitiveKind::reset,PrimitiveKind::basic}));
    s.attack_restricted=true;words=build_words(s,&c,abilities,ranged_move);
    check(!find(words,{PrimitiveKind::reset,PrimitiveKind::basic})&&!find(words,{PrimitiveKind::move,PrimitiveKind::reset,PrimitiveKind::basic}));
    s=self_fact();s.raw_a=false;c=camp_fact(s,10,{2,0});abilities[0]=ability(s,AbilityKind::reset,target_self,0,0);
    words=build_words(s,&c,abilities,{s.epoch,{1,0},true,true});check(find(words,{PrimitiveKind::move,PrimitiveKind::reset,PrimitiveKind::basic}));
    c.body_actual=false;words=build_words(s,&c,abilities,{s.epoch,{1,0},true,true});
    check(!find(words,{PrimitiveKind::move,PrimitiveKind::reset,PrimitiveKind::basic}));
    // Ryth kind3/mask19 must use the ground overload and duration1 stun.
    s=self_fact();c=camp_fact(s,10,{5,0});abilities={ability(s,AbilityKind::aoe_damage,19,4,1,1,1,true),{}};
    words=build_words(s,&c,abilities,{});const auto* aoe=find(words,{PrimitiveKind::cast_ground});check(aoe);
    check(aoe->actions[0].point==Point{4,0});one={c};one[0].pose={3,0};
    c=one[0];words=build_words(s,&c,abilities,{});aoe=find(words,{PrimitiveKind::cast_ground});check(aoe);
    q=quote_word(*aoe,s,c,abilities,support(one),scenario(s));check(q.immediate_hp_damage==100&&q.post_batch_hp==1000);
    illegal=manual(s,c,{{PrimitiveKind::cast_unit,c.id,c.pose,0}});
    q=quote_word(illegal,s,c,abilities,support(one),scenario(s));check(!q.compatible&&!q.full_support);
    abilities[0].descriptor.target_mask=target_camps;
    check(!find(build_words(s,&c,abilities,{}),{PrimitiveKind::cast_unit}));
    q=quote_word(illegal,s,c,abilities,support(one),scenario(s));check(!q.compatible);
    abilities[0].descriptor.target_mask=19;
    s.ordinary_walked=true;words=build_words(s,&c,abilities,{});check(!find(words,{PrimitiveKind::cast_ground}));
    s.walked_actual=false;words=build_words(s,&c,abilities,{});check(!find(words,{PrimitiveKind::cast_ground}));
    // A compatible move genuinely enables a spell that is initially out of reach.
    s=self_fact();c=camp_fact(s,10,{5,0});abilities={ability(s,AbilityKind::damage,target_camps,4,1),{}};
    words=build_words(s,&c,abilities,{s.epoch,{1,0},true,true});check(find(words,{PrimitiveKind::move,PrimitiveKind::cast_unit}));
    // Varg shield and reset share C. Timer1 protects exactly the next CAMP batch.
    s=self_fact();c=camp_fact(s);one={c};abilities={ability(s,AbilityKind::reset,target_self,0,0),ability(s,AbilityKind::shield,target_self,0,1,0,1)};
    words=build_words(s,&c,abilities,move);const auto* shield=find(words,{PrimitiveKind::shield,PrimitiveKind::basic,PrimitiveKind::move});check(shield);
    auto shield_words=build_words(s,&c,abilities,{});const auto* still_shield=find(shield_words,{PrimitiveKind::shield,PrimitiveKind::basic});check(still_shield);
    q=quote_word(*still_shield,s,c,abilities,support(one),scenario(s));check(q.post_batch_hp==1000&&q.post_batch_shield==40&&q.camp_batches==1);
    abilities[1].descriptor.duration=0;q=quote_word(*still_shield,s,c,abilities,support(one),scenario(s));check(q.post_batch_hp==940);
    abilities[1].descriptor.duration=1;
    illegal=manual(s,c,{{PrimitiveKind::shield,s.id,s.pose,1},{PrimitiveKind::reset,s.id,s.pose,0}});
    q=quote_word(illegal,s,c,abilities,support(one),scenario(s));check(!q.compatible);
    s.shield=100;s.shield_time=1;q=quote_word(build_words(s,&c,{},{}).words[0],s,c,{},support(one),scenario(s));check(q.post_batch_hp==1000&&q.post_batch_shield==40);
    s.shield_time=0;q=quote_word(build_words(s,&c,{},{}).words[0],s,c,{},support(one),scenario(s));check(q.post_batch_hp==940);
    // Engine f32 lethal boundary: five129.8f hits do not kill649HP.
    Residual residual{649,0};for(unsigned i=0;i<5;++i)residual=deplete(residual,129.8f);
    check(residual.hp>0);residual=deplete(residual,129.8f);check(residual.hp==0);
    // Cross-group splash wakes ALL members. Removing killed target must not
    // enable a second splash centered on a corpse.
    s=self_fact();s.basic_splash=true;c=camp_fact(s,10,{1,0},100);c.life=Life::asleep;c.hp=45;
    auto splash=camp_fact(s,11,{2,0},101);splash.life=Life::asleep;
    auto groupmate=camp_fact(s,12,{3,0},101);groupmate.life=Life::asleep;groupmate.strength=70;
    std::array<CampFact,3> three{groupmate,c,splash};abilities={ability(s,AbilityKind::reset,target_self,0,0),{}};
    basic=build_words(s,&c,abilities,{}).words[0];q=quote_word(basic,s,c,abilities,support(three),scenario(s));
    check(q.predicted_kills==1&&q.post_batch_hp==870);
    words=build_words(s,&c,abilities,{});reset=find(words,{PrimitiveKind::basic,PrimitiveKind::reset,PrimitiveKind::basic});check(reset);
    q=quote_word(*reset,s,c,abilities,support(three),scenario(s));check(q.target_changed&&!q.compatible&&!q.full_support&&q.immediate_hp_damage==90);
    // Actual death / world revision changes invalidate the old nomination.
    auto dead=c;dead.life=Life::dead;check(!matches(basic,s,dead));dead=c;++dead.epoch.revision;check(!matches(basic,s,dead));
    c=camp_fact(s);one={c};basic=build_words(s,&c,{},{}).words[0];auto assumptions=scenario(s);
    q=quote_word(basic,s,c,{},support(one),assumptions);check(q.full_support);
    UnknownSource hidden{900,{5,5},4,100,true};auto partial=support(one);partial.unknown={&hidden,1};
    auto unknown=quote_word(basic,s,c,{},partial,assumptions);check(unknown.immediate_known&&!unknown.full_support&&unknown.unknown_identities==1);
    auto second=assumptions;second.enemy_opportunities=2;auto replies=quote_word(basic,s,c,{},support(one),second);
    check(replies.camp_batches==1&&replies.enemy_opportunities==2&&!replies.full_support&&!comparable(q,replies));
    second=assumptions;++second.assumption_stamp;check(!comparable(q,quote_word(basic,s,c,{},support(one),second)));
    one[0].life=Life::unknown;one[0].life_actual=false;unknown=quote_word(basic,s,c,{},support(one),assumptions);check(!unknown.next_batch_known&&!unknown.full_support);
    std::array<CampFact,max_modeled_camps+1> too_many{};
    unknown=quote_word(basic,s,c,{},support(too_many),assumptions);check(unknown.capacity_exceeded&&!unknown.full_support);
    // AoE collateral wakes a second group's member outside the damaged area.
    s=self_fact();c=camp_fact(s,10,{1,0},100);c.life=Life::asleep;
    splash=camp_fact(s,11,{2,0},101);splash.life=Life::asleep;
    groupmate=camp_fact(s,12,{3,0},101);groupmate.life=Life::asleep;groupmate.strength=70;
    three={c,splash,groupmate};abilities={ability(s,AbilityKind::aoe_damage,19,4,1,1,1,true),{}};
    words=build_words(s,&c,abilities,{});aoe=find(words,{PrimitiveKind::cast_ground,PrimitiveKind::basic});check(aoe);
    q=quote_word(*aoe,s,c,abilities,support(three),scenario(s));check(q.compatible&&q.post_batch_hp==930);
    // Macro operations share the same small physical-word/feedback contract,
    // but cannot receive combat value or support assurance from this model.
    const auto recall_word=single_primitive({PrimitiveKind::recall,s.id,s.pose,0},s.epoch);
    const auto bank_word=single_primitive({PrimitiveKind::bank,0,s.pose,0},s.epoch);
    check(recall_word.count==1&&bank_word.count==1&&recall_word.epoch==s.epoch);
    check(recall_word.refresh_after_each&&recall_word.retarget_on_change&&recall_word.requires_paid_exit);
    check(recall_word.dependencies.commands==1&&recall_word.dependencies.self_refreshes==2&&
        recall_word.dependencies.ability_refreshes==0&&recall_word.dependencies.target_refreshes==0);
    check((recall_word.dependencies.mask&channel_dependency)!=0&&(recall_word.dependencies.mask&ability_dependency)==0);
    check((bank_word.dependencies.mask&custody_dependency)!=0&&(bank_word.dependencies.mask&channel_dependency)!=0);
    const auto recall_quote=quote_word(recall_word,s,c,abilities,support(three),scenario(s));
    const auto bank_quote=quote_word(bank_word,s,c,abilities,support(three),scenario(s));
    check(recall_quote.noncombat&&recall_quote.unknown_numeric&&recall_quote.compatible&&!recall_quote.immediate_known&&
        !recall_quote.next_batch_known&&!recall_quote.full_support&&recall_quote.camp_batches==0);
    check(bank_quote.noncombat&&bank_quote.unknown_numeric&&bank_quote.compatible&&!bank_quote.full_support);
    check(!comparable(recall_quote,bank_quote)&&!comparable(q,recall_quote));
    const auto unstamped=single_primitive({PrimitiveKind::bank,0,{},0});
    check(unstamped.count==1&&unstamped.epoch.round==-1);
    // All generated schemas preserve compatible MAC; numeric inputs never
    // influence the bound or physical incumbent. Exercise every ready subset.
    for(unsigned mask=0;mask<256;++mask){
        s=self_fact();s.raw_m=(mask&1)!=0;s.raw_a=(mask&2)!=0;s.raw_c=(mask&4)!=0;s.ranged=(mask&8)!=0;
        s.attack_range=s.ranged?3:1;s.ordinary_walked=(mask&16)!=0;s.attack_restricted=(mask&32)!=0;
        c=camp_fact(s,10,{1,0});one={c};
        abilities={ability(s,AbilityKind::reset,target_self,0,0),ability(s,AbilityKind::damage,target_camps,4,1,0,0,(mask&64)!=0)};
        if(mask&128)abilities[1]=ability(s,AbilityKind::shield,target_self,0,1,0,1);
        words=build_words(s,&c,abilities,{s.epoch,{-1,0},true,true});
        check(words.count<=max_words&&words.bounded&&words.operations<=max_words+4);
        for(unsigned i=0;i<words.count;++i){
            check(words.words[i].count<=4);q=quote_word(words.words[i],s,c,abilities,support(one),scenario(s));check(q.compatible);
            unsigned casts=0;for(unsigned j=0;j<words.words[i].count;++j){const auto kind=words.words[i].actions[j].kind;
                casts+=kind!=PrimitiveKind::basic&&kind!=PrimitiveKind::move;}check(casts<=1);
        }
    }
    std::printf("native service words: %u checks passed\n",checks);
}
