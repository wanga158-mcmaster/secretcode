#include "../src/native_service_types.h"
#include "../src/native_service_execution.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace native_service=mosaic::native_service;
namespace ns=native_service;
struct Pure {
#define MOSAIC_NATIVE_SERVICE_FACTS_PURE_ONLY
#include "../src/native_service_facts.h"
#undef MOSAIC_NATIVE_SERVICE_FACTS_PURE_ONLY
};
using Logic=Pure::NativeFactsLogic;
static unsigned checks;
static void check(bool okay,int line){++checks;if(!okay){std::fprintf(stderr,"native facts line%d check%u\n",line,checks);std::abort();}}
#define CHECK(x) check(bool(x),__LINE__)
struct Body {int team,type,x,y;float health,max_health,strength,focus;int move_speed,attack_range,state;};
struct CampBody {int x,y;float health,max_health,strength,focus;};
struct Buff {unsigned type;struct Payload {struct Shield {int duration;float value;}shield;struct Time{int duration;}stun,enhanced_attack;}data;};
struct CampBuff {unsigned type;float magnitude;int timer;};
struct Props {
    unsigned kind;int cooldown;unsigned targets,attributes;
    struct Payload {
        unsigned enhanced_attack;
        struct Damage {float range,modifier;}damage,heal;
        struct Area {float range,radius,modifier;int duration;}aoe_damage,dash;
        struct Timed {float range,modifier;int duration;}cc,shield;
    }payload;
};
struct QueryBudget {bool before{true},after{true},poisoned{};bool before_query(){return before;}bool after_query(){return after;}};
struct MethodBudget {bool poisoned()const{return true;}bool before_query(){return false;}};
struct Clock {std::uint64_t fuel{};static std::uint64_t read(void* value){return static_cast<Clock*>(value)->fuel;}};
static ns::SelfFact owner(ns::Epoch epoch){
    ns::SelfFact self{};self.epoch=epoch;self.id=91;
    const Body body{1,0,5,6,649.25f,1200,45.5f,99.75f,1,1,1|8|16|2048};
    Logic::self_body(self,body);Logic::restrictions(self,0);
    const Buff empty{};CHECK(Logic::self_buffs(self,&empty,0));return self;
}
int main(){
    const ns::Epoch epoch{7,23,4};auto self=owner(epoch);
    CHECK(self.actual(epoch));CHECK((self.id==91&&self.team==1&&self.pose==ns::Point{5,6}));
    CHECK(self.hp==649.25f&&self.strength==45.5f&&self.focus==99.75f);
    CHECK(self.raw_m&&self.can_move(epoch)&&self.can_attack(epoch)&&self.can_cast(epoch));
    CHECK(self.channel_actual&&self.channel_time_actual&&self.channel_time==0);
    CHECK(!self.ranged&&self.move_speed==1);
    Logic::restrictions(self,1|4);CHECK(self.raw_m&&self.raw_c);
    CHECK(!self.can_move(epoch)&&!self.can_cast(epoch)&&self.can_attack(epoch));
    Logic::restrictions(self,0);
    const std::array<Buff,3> buffs{{{0,{{0,25.5f},{0},{0}}},{1,{{0,0},{2},{0}}},{2,{{0,0},{0},{3}}}}};
    CHECK(Logic::self_buffs(self,buffs.data(),buffs.size()));
    CHECK(self.shield==25.5f&&self.shield_time==0&&self.stunned&&self.stun_time==2);
    CHECK(self.enhanced&&self.enhanced_time==3&&!self.stun_immune);
    CHECK(self.raw_m&&self.raw_a&&self.raw_c&&!self.move_restricted&&!self.attack_restricted&&!self.cast_restricted);
    CHECK(!self.can_move(epoch)&&!self.can_attack(epoch)&&!self.can_cast(epoch));
    // Marcella's personal immunity is independent of an enhanced attack buff.
    self.stun_immune=true;CHECK(Logic::self_buffs(self,buffs.data(),0));
    CHECK(self.stun_immune&&!self.enhanced&&!self.stunned&&self.shield==0);
    Buff bad{};bad.type=99;CHECK(!Logic::self_buffs(self,&bad,1));CHECK(!self.buffs_actual&&self.body_actual);
    CHECK(!self.actual(epoch));CHECK(Logic::self_buffs(self,&bad,0));
    CHECK(!self.actual({7,23,5})&&!self.actual({8,23,4}));
    Body channel{1,0,5,6,649.25f,1200,45.5f,99.75f,1,3,1|4|8|16|2048};
    ns::SelfFact channel_self{};channel_self.epoch=epoch;Logic::self_body(channel_self,channel);
    Logic::restrictions(channel_self,0);CHECK(Logic::self_buffs(channel_self,&bad,0));
    CHECK(channel_self.submitting&&channel_self.channel_actual&&!channel_self.channel_time_actual);
    CHECK(channel_self.ranged&&!channel_self.can_move(epoch)&&!channel_self.can_attack(epoch)&&!channel_self.can_cast(epoch));
    self.channel_time_actual=true;Logic::self_body(self,channel);CHECK(!self.channel_time_actual);
    channel.state=1|64|128;Logic::self_body(channel_self,channel);
    CHECK(channel_self.recall_started&&channel_self.submit_started&&!channel_self.recalling&&!channel_self.submitting);
    self=owner(epoch);

    ns::CampFact camp{};camp.epoch=epoch;camp.id=907;camp.group_id=510;camp.member_ordinal=2;camp.group_ordinal=8;
    CHECK(!camp.actual(epoch)&&camp.wake_potential(epoch)&&camp.life==ns::Life::unknown);
    CHECK(Logic::camp_life(camp,2));CHECK(camp.actual(epoch)&&camp.living(epoch)&&camp.life==ns::Life::asleep);
    CHECK(!camp.body_actual);const CampBody camp_body{6,6,119.5f,120,0,0};Logic::camp_body(camp,camp_body);
    const std::array<CampBuff,2> camp_buffs{{{0,14.25f,1},{1,0,0}}};
    CHECK(Logic::camp_buffs(camp,camp_buffs.data(),camp_buffs.size()));
    CHECK(camp.body_actual&&camp.buffs_actual&&camp.hp==119.5f&&camp.strength==0);
    CHECK(camp.stunned&&camp.stun_time==0&&camp.shield==14.25f&&camp.shield_time==1);
    CHECK(camp.id!=unsigned(camp.member_ordinal)&&camp.group_id!=unsigned(camp.group_ordinal));
    CHECK(!Logic::camp_life(camp,3));CHECK(!camp.actual(epoch)&&camp.life==ns::Life::unknown&&camp.wake_potential(epoch));
    CHECK(Logic::camp_life(camp,0));CHECK(camp.actual(epoch)&&!camp.living(epoch)&&!camp.wake_potential(epoch));
    CHECK(!camp.body_actual&&!camp.buffs_actual);

    ns::GroupFact group{};group.epoch=epoch;group.id=510;group.ordinal=8;group.gem_value=17;
    group.visibility_actual=group.visible=true;
    // The last selected member's Dead does not establish native group Reward.
    CHECK(!group.capturable(epoch));CHECK(Logic::group_life(group,0));
    CHECK(!group.actual&&group.life==ns::GroupLife::unknown&&group.gem_value==17&&group.visible);
    CHECK(Logic::group_life(group,1));CHECK(group.current(epoch)&&!group.capturable(epoch));
    CHECK(Logic::group_life(group,2));CHECK(group.capturable(epoch));
    CHECK(Logic::group_life(group,3));group.respawn_time=0;group.respawn_actual=true;
    CHECK(group.current(epoch)&&group.life==ns::GroupLife::respawning&&!group.capturable(epoch));
    CHECK(!Logic::group_life(group,9));CHECK(!group.actual&&group.life==ns::GroupLife::unknown);

    Props props{};props.kind=3;props.cooldown=8;props.targets=19;props.attributes=1;
    props.payload.aoe_damage={4,1,1,1};ns::AbilityDescriptor ability{};
    CHECK(Logic::descriptor(ability,props));CHECK(ability.actual&&ability.present);
    CHECK(ability.kind==ns::AbilityKind::aoe_damage&&ability.kind!=ns::AbilityKind::cc);
    CHECK(ability.target_mask==19&&ability.camps()&&ability.enemies()&&ability.ground());
    CHECK(ability.duration==1&&ability.applies_stun()&&ability.exclusive_with_movement);
    CHECK(ability.range==4&&ability.radius==1&&ability.modifier==1&&ability.max_cooldown==8);
    ns::AbilityFact cast{epoch,ability,0,true};CHECK(cast.ready(self,epoch));
    auto cached=cast;Logic::ability_begin(cached,cached,{7,23,5});
    CHECK(cached.descriptor.actual&&cached.descriptor.kind==ns::AbilityKind::aoe_damage&&!cached.cooldown_actual);
    CHECK(cached.epoch.attempt==5); // Aliasing must not erase immutable descriptors.
    cast.cooldown=1;CHECK(!cast.ready(self,epoch));cast.cooldown=0;
    CHECK(!cast.ready(self,{7,23,5}));cast.cooldown_actual=false;CHECK(!cast.ready(self,epoch));
    props.kind=1;props.targets=8;props.attributes=0;props.payload.enhanced_attack=1;
    CHECK(Logic::descriptor(ability,props));CHECK(ability.kind==ns::AbilityKind::reset&&ability.enhanced_attack&&!ability.applies_stun());
    props.kind=0;CHECK(Logic::descriptor(ability,props));CHECK(ability.actual&&!ability.present);
    props.kind=8;CHECK(!Logic::descriptor(ability,props));CHECK(!ability.actual&&!ability.present);

    ns::Facts facts{};facts.begin(7);const auto start=facts.epoch;facts.self=owner(start);
    facts.camp=camp;facts.camp.epoch=start;CHECK(Logic::camp_life(facts.camp,2));
    facts.group=group;facts.group.epoch=start;CHECK(Logic::group_life(facts.group,2));
    props.kind=3;CHECK(Logic::descriptor(facts.abilities[0].descriptor,props));facts.abilities[0].cooldown_actual=true;
    facts.attempted(ns::EffectScope::self_only);
    CHECK(facts.epoch.attempt==start.attempt+1&&facts.epoch.revision==start.revision);
    CHECK(!facts.self.actual(facts.epoch)&&facts.camp.actual(facts.epoch)&&facts.group.capturable(facts.epoch));
    CHECK(facts.abilities[0].descriptor.actual&&!facts.abilities[0].cooldown_actual);
    facts.self=owner(facts.epoch);facts.attempted(ns::EffectScope::knowledge_only);
    CHECK(!facts.self.actual(facts.epoch)&&facts.camp.actual(facts.epoch));
    facts.self=owner(facts.epoch);facts.attempted();
    CHECK(facts.epoch.revision==start.revision+1&&!facts.camp.actual(facts.epoch)&&!facts.group.current(facts.epoch));
    CHECK(facts.camp.life==ns::Life::unknown&&facts.camp.wake_potential(facts.epoch));
    const auto before_round=facts.epoch;facts.begin(7);CHECK(facts.epoch==before_round);
    facts.begin(8);CHECK(facts.epoch.round==8&&facts.epoch.attempt==0&&facts.epoch.revision==before_round.revision+1);
    CHECK(!facts.self.body_actual&&!facts.camp.life_actual&&!facts.group.actual&&facts.abilities[0].descriptor.actual);
    ns::CaptureFact capture{facts.epoch,510,ns::GroupLife::reward,true,true,true};CHECK(capture.holding_reward(facts.epoch));
    capture.life_actual=false;CHECK(!capture.holding_reward(facts.epoch));
    ns::BankFact bank{facts.epoch,20,7,true,true,true,true};CHECK(bank.current(facts.epoch));
    bank.submitted_actual=false;CHECK(!bank.current(facts.epoch));
    QueryBudget query{};self=owner(epoch);self.gems_actual=self.custody_actual=true;self.gems=19;
    CHECK(self.gems_actual&&!self.gold_actual&&self.custody_actual); // Bag does not require a private gold import.
    CHECK(Logic::query_before(query,self)&&Logic::query_after(query,self)&&self.actual(epoch));
    query.after=false;CHECK(!Logic::query_after(query,self));
    CHECK(self.epoch.round==-1&&!self.body_actual&&!self.buffs_actual&&!self.gems_actual&&!self.actual(epoch));
    camp.epoch=epoch;CHECK(Logic::camp_life(camp,1));CHECK(!Logic::query_after(query,camp));
    CHECK(camp.id==907&&camp.life==ns::Life::unknown&&!camp.life_actual&&camp.wake_potential(epoch));
    group.epoch=epoch;CHECK(Logic::group_life(group,2));query.before=false;
    CHECK(!Logic::query_before(query,group));CHECK(group.id==510&&group.gem_value==17&&!group.actual&&!group.visibility_actual);
    cached.epoch=epoch;cached.cooldown_actual=true;CHECK(!Logic::query_after(query,cached));
    CHECK(cached.descriptor.actual&&cached.epoch.round==-1&&!cached.cooldown_actual);
    bank={epoch,30,19,true,true,true,true};CHECK(!Logic::query_after(query,bank));
    CHECK(bank.epoch.round==-1&&!bank.custody_actual&&!bank.submitted_actual&&!bank.channel_actual);
    facts.self=owner(facts.epoch);facts.group.actual=facts.camp.life_actual=true;facts.reject();
    CHECK(facts.epoch.round==-1&&!facts.self.body_actual&&!facts.group.actual&&!facts.camp.life_actual);
    CHECK(facts.abilities[0].descriptor.actual&&!facts.abilities[0].cooldown_actual);
    // Real incumbent factory and scalar budget: the complete Basic is funded,
    // but one extra optional ability query would consume its reserved tail.
    facts.begin(20);facts.self=owner(facts.epoch);facts.camp={};facts.camp.epoch=facts.epoch;
    facts.camp.id=907;CHECK(Logic::camp_life(facts.camp,1));Logic::camp_body(facts.camp,camp_body);
    CHECK(Logic::camp_buffs(facts.camp,camp_buffs.data(),camp_buffs.size()));
    const auto incumbent=ns::build_words(facts.self,&facts.camp,facts.abilities);
    CHECK(incumbent.incumbent&&incumbent.count&&incumbent.words[0].actions[0].kind==ns::PrimitiveKind::basic);
    Clock clock{100000};ns::Budget preview(&clock,Clock::read,clock.fuel);
    const auto tail=ns::word_tail(incumbent.words[0],0,preview,0,true);
    clock.fuel=tail+preview.pure_bound+preview.yield_bound+preview.query_bound-1;
    const auto initial_fuel=clock.fuel;ns::Budget actual_budget(&clock,Clock::read,clock.fuel);
    CHECK(tail>0&&actual_budget.admit(tail));actual_budget.reserve(tail);
    const auto retained_epoch=facts.epoch;
    CHECK(!Logic::query_before(actual_budget,facts.abilities[0],&facts));
    CHECK(!actual_budget.poisoned&&actual_budget.queries==0&&actual_budget.spent()==0);
    CHECK(facts.epoch==retained_epoch&&facts.self.actual(facts.epoch)&&facts.camp.living(facts.epoch));
    CHECK(facts.abilities[0].descriptor.actual&&!facts.abilities[0].cooldown_actual);
    const auto retained=ns::build_words(facts.self,&facts.camp,facts.abilities);
    CHECK(retained.incumbent&&retained.words[0].actions[0].kind==ns::PrimitiveKind::basic);
    CHECK(actual_budget.admit(ns::word_tail(retained.words[0],0,actual_budget,0,true)));
    // The actual-clock stale sentinel poisons the same budget permanently.
    clock.fuel=~std::uint64_t{};
    CHECK(!Logic::query_before(actual_budget,facts.abilities[0],&facts));
    CHECK(actual_budget.poisoned&&facts.epoch.round==-1&&!facts.self.body_actual&&!facts.camp.life_actual);
    clock.fuel=initial_fuel;CHECK(!Logic::query_before(actual_budget,facts.abilities[0],&facts));
    CHECK(actual_budget.poisoned&&facts.epoch.round==-1);
    // Method-based budget adapters preserve the same hard/soft distinction.
    facts.begin(21);facts.self=owner(facts.epoch);MethodBudget method;
    CHECK(!Logic::query_before(method,facts.abilities[0],&facts));CHECK(facts.epoch.round==-1&&!facts.self.body_actual);
    facts.begin(22);facts.self=owner(facts.epoch);query.after=false;query.poisoned=true;
    CHECK(!Logic::query_after(query,facts.abilities[0],&facts));CHECK(facts.epoch.round==-1&&!facts.self.body_actual);
    CHECK(sizeof(ns::Facts)<1024);
    std::printf("native-service-facts %u checks passed\n",checks);
}
