#include "production_investment.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <type_traits>
using namespace tt::production_investment;
namespace {std::size_t allocations{};}
void* operator new(std::size_t n){++allocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc{};}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
namespace {
int checks{},failures{};
void check(bool okay,const char* message){++checks;if(!okay){++failures;std::printf("FAIL %s\n",message);}}
ShopInput shop(std::uint32_t gold=150) {
    ShopInput s;s.gold=gold;s.body={1000,1000,35,10};s.growth={100,5,10};
    s.metadata_complete=s.shop_at_declared_spawn=s.stable_raw_stat_projection=true;
    constexpr Passive passives[]={Passive::Monster,Passive::Jungle,Passive::Durability,Passive::Treasure,Passive::None};
    constexpr float magnitudes[]={0.1f,0.3f,0.1f,0.2f,0.0f};
    for(std::size_t i=0;i<family_count;++i) {
        auto& f=s.families[i];f.has_result=true;
        f.shard={static_cast<std::uint32_t>(10+i),30,5,{0,0,1},Passive::None,0,true};
        f.result={static_cast<std::uint32_t>(20+i),150,0,{0,0,i+1==family_count?6:5},passives[i],magnitudes[i],false};
    }
    return s;
}
CalendarInput calendar(std::uint32_t gold=150) {
    CalendarInput input;input.shop=shop(gold);input.now=100;input.deadline=600;
    input.quiet_decision=input.no_pending_capture_or_channel=input.stable_outgoing_supported=true;
    input.recall_to_first_shop_ready=5;input.job_count=1;
    auto& job=input.jobs[0];job.group=19;job.generation=1;job.observed_round=100;
    job.available_from=100;job.valid_through=1000;job.member_count=1;
    job.members[0]={{4,2},649,true};job.worker={0,2};job.aim={4,2};
    job.width=job.height=20;job.basic_range=1;job.cast_range=4;job.area_radius=1;
    job.cast_modifier=1;job.cooldown_period=8;job.queried_current_damage=10;
    job.ingress_keep=0;job.ingress_shop=10;job.ingress_between=10;
    job.damage_to_capture=1;job.capture_payout=6;job.payout_to_bank=3;job.gems=11;
    job.full_group_known=job.availability_supported=job.geometry_supported=true;
    job.zero_incoming_through_continuation=job.team_displacement_accounted=job.residual_persists_while_away=true;
    return input;
}
Result finish(Valuation& value,std::size_t units=17) {
    for(std::size_t i=0;i<20000 && value.status()==Status::NeedCompute;++i) {
        value.advance(units);check(value.last_work()<=max_advance_units,"every actual advance stays inside its hard structural unit cap");
    }
    check(value.status()!=Status::NeedCompute,"bounded offered calendar terminates");
    return value.result();
}
Result evaluate(const CalendarInput& input){Valuation value(input);return finish(value);}
}
int main() {
    static_assert(std::is_trivially_copyable_v<CalendarInput>);
    void* tracker=::operator new(1);::operator delete(tracker);
    const auto allocation_start=allocations;

    auto s=shop(360);s.body.focus=70;
    auto quote=project_shop(s);
    check(quote.status==Status::Ready && quote.calls==3 && quote.gold_spent==360 && quote.gold_left==0,
          "actual shop order spends360 as Monster5 Jungle5 Durability2 in three owner inputs");
    check(quote.buys[0].family==0 && quote.buys[1].family==1 && quote.buys[2].family==2 &&
          quote.buys[0].count==5 && quote.buys[2].count==2 && quote.point_delta[2]==12 &&
          quote.projected.focus==190 && quote.effects.monster==0.1f && quote.effects.jungle==0.3f &&
          quote.effects.durability==0,"partial and complete emblems project the actual bought stat points and new passives once");

    s=shop(30);s.families[0].shards_owned=4;s.body.focus=110;
    quote=project_shop(s);
    check(quote.status==Status::Ready && quote.calls==1 && quote.buys[0].count==1 &&
          quote.buys[0].combined && quote.point_delta[2]==1 && quote.projected.focus==120,
          "fifth shard replaces the already-owned four rather than adding a full emblem on top");

    s=shop(30);for(std::size_t i=0;i+1<family_count;++i)s.families[i].result_owned=1;
    s.families[4].shards_owned=4;s.body.focus=310;quote=project_shop(s);
    check(quote.status==Status::Ready && quote.point_delta[2]==2 && quote.projected.focus==330,
          "Power combination counts its queried sixth point and consumes five ingredients");

    s=shop(150);s.effects.monster=0.1f;quote=project_shop(s);
    check(quote.effects.monster==0.1f,"a passive already held on another stat variant is not stacked again");
    s=shop(60);s.families[0].shard.cost=100;s.families[1].shard.cost=10;quote=project_shop(s);
    check(quote.status==Status::Ready && quote.calls==0 && quote.gold_left==60,
          "first eligible unaffordable family stops the actual operator before later cheap stock");

    s=shop(30);s.body={400,1000,35,10};s.growth.health=100;s.strength_per_bonus_health=0.01f;
    s.families[0].shard.points={1,0,0};s.families[0].result.points={5,0,0};quote=project_shop(s);
    check(quote.status==Status::Ready && quote.projected.max_hp==1100 && quote.projected.hp==400 &&
          quote.projected.strength==36 && quote.healing_credited==0,
          "runtime health growth can affect Ryth raw Strength but never invents purchase healing");
    s=shop(1000000);for(std::size_t i=0;i+1<family_count;++i)s.families[i].result_owned=1;
    quote=project_shop(s);
    check(quote.status==Status::Unsupported && quote.reason==Reason::ShopCapacity && quote.calls==max_shop_calls,
          "affordable work beyond the bounded actual purchase prefix is unsupported not a truncated profit claim");
    s=shop(150);s.metadata_complete=false;
    check(project_shop(s).status==Status::Unsupported,"missing runtime item metadata cannot become an investment");
    s=shop(150);s.families[0].shard.passive=Passive::Monster;s.families[0].shard.magnitude=0.2f;
    check(project_shop(s).status==Status::Unsupported,
          "consumed passive-bearing ingredients require an explicit removal model");

    auto input=calendar();Valuation sliced(input);
    check(sliced.advance(0)==Status::NeedCompute && sliced.result().work_used==0,"zero units do no simulated work");
    input.jobs[0].members[0].hp=1e9f; // the value owns the original frozen HP649
    auto result=finish(sliced,1);
    check(result.status==Status::Ready && result.decision==Decision::InvestByDeadline &&
          result.keep.banked_gems==0 && result.invest.banked_gems==11 &&
          result.invest.commands[0]==10 && result.invest.bank_round[0]==198,
          "runtime Focus growth plus Monster once pays a real recall shop reentry cost by the deadline");

    input=calendar();input.deadline=1000;result=evaluate(input);
    check(result.decision==Decision::EarlierSameWork && result.keep.complete_all && result.invest.complete_all &&
          result.keep.bank_round[0]==622 && result.invest.bank_round[0]==198 && result.same_work_time_saved==424 &&
          result.keep.banked_gems==result.invest.banked_gems,
          "same retained work finishes earlier without claiming any additional unmodeled gems");

    input=calendar();input.shop.body.focus=100;input.jobs[0].queried_current_damage=100;
    input.recall_to_first_shop_ready=590;result=evaluate(input);
    check(result.status==Status::Ready && result.decision==Decision::Keep && result.keep.banked_gems==11 &&
          result.invest.banked_gems==0,"the same affordable purse does not pay when actual paid travel misses the race");

    input=calendar();input.remaining_cooldown=80;input.jobs[0].members[0].hp=1;
    input.shop.growth.focus=0;input.shop.families[0].result.passive=Passive::None;
    result=evaluate(input);
    check(result.status==Status::Ready && result.keep.last_damage_round[0]==180 &&
          result.invest.last_damage_round[0]==180 && result.decision==Decision::Keep,
          "recall shop and travel advance the actual cooldown instead of charging the same delay twice");

    input=calendar(0);auto& basic=input.jobs[0];basic.family=Family::Basic;
    input.shop.body.strength=129.8f;basic.queried_current_damage=129.8f;
    basic.worker={1,1};basic.members[0].pos={2,1};result=evaluate(input);
    check(result.status==Status::Ready && result.keep.commands[0]==6 &&
          result.keep.last_damage_round[0]==105,"HP649 at float32 damage129.8 needs six actual subtractions");

    input=calendar(0);input.shop.body.focus=100;auto& area=input.jobs[0];
    area.queried_current_damage=100;area.member_count=2;area.worker={0,2};area.aim={2,2};
    area.members[0]={{2,2},120,true};area.members[1]={{3,2},950,true};
    result=evaluate(input);
    check(result.status==Status::Ready && result.keep.commands[0]==10 && result.keep.bank_round[0]==182,
          "one fixed area cast hits all known residuals so ten casts replace twelve separate member kills");
    input.jobs[0].members[1].pos={7,2};
    check(evaluate(input).status==Status::Unsupported,"an unhit living group member prevents a complete-job valuation");
    input.jobs[0].members[1].hp=0;
    check(evaluate(input).status==Status::Ready,"already-dead members do not invent remaining spatial work");

    input=calendar();input.jobs[0].members[0].hp=1;result=evaluate(input);
    check(result.status==Status::Ready && result.keep.commands[0]==1 && result.invest.commands[0]==1 &&
          result.decision==Decision::Keep,"own partial damage is retained during both arms rather than reset to full design HP");

    input=calendar();input.jobs[0].displaced_shop=11;input.deadline=1000;
    result=evaluate(input);
    check(result.status==Status::Unsupported && result.reason==Reason::TeamDisplacement,
          "positive displaced production needs an incidence-time model even if own bank misses the deadline");

    input=calendar(150);input.shop.body.focus=100;input.jobs[0].queried_current_damage=100;
    input.job_count=2;input.jobs[1]=input.jobs[0];input.jobs[1].generation=2;
    input.jobs[1].repeats_previous=true;input.jobs[1].respawn_after_previous_payout=60;
    result=evaluate(input);
    check(result.status==Status::Ready && result.keep.last_damage_round[1]==263 &&
          result.invest.last_damage_round[1]==231 && result.keep.bank_round[1]==273 &&
          result.invest.bank_round[1]==241 && result.same_work_time_saved==32,
          "the second same-group cycle pays its own actual respawn release after each arm's different first payout");

    input=calendar();input.job_count=2;input.jobs[1]=input.jobs[0];
    result=evaluate(input);
    check(result.status==Status::Unsupported && result.reason==Reason::ResourceReuse,
          "two copied jobs cannot bank the same physical group generation twice");
    input.jobs[1].generation=2;result=evaluate(input);
    check(result.status==Status::Unsupported && result.reason==Reason::ResourceReuse,
          "a new same-group generation still needs its explicit post-payout respawn release");
    input=calendar();input.jobs[0].members[0].hp_exact=false;
    check(evaluate(input).status==Status::Unsupported,"unknown residual HP is not feasible profitable work");
    input=calendar();input.jobs[0].team_displacement_accounted=false;
    check(evaluate(input).status==Status::Unsupported,"unaccounted teammate commitment stays unsupported");
    input=calendar();input.carried_gems=11;
    check(evaluate(input).status==Status::Unsupported,"pending owned conversion is not discarded for a speculative shop");
    input=calendar();input.shop.body.hp=0;
    check(project_shop(input.shop).status==Status::Ready && evaluate(input).status==Status::Unsupported,
          "dead bodies can shop but an unpriced death or respawn cannot become a ready productive investment");
    input=calendar();input.query_generation=0x100000003;Valuation dated(input);
    input.query_generation=7;input.now=200;result=finish(dated);
    check(result.query_generation==0x100000003 && result.decision_round==100,
          "published calendar keeps its original query identity and owner-round provenance");
    input=calendar();input.jobs[0].zero_incoming_through_continuation=false;
    check(evaluate(input).status==Status::Unsupported,"harmful survival outside the represented zero-incoming family is unsupported");
    input=calendar();input.jobs[0].queried_current_damage=11;
    check(evaluate(input).status==Status::Unsupported,"a mismatch against current runtime damage revokes the stat projection");
    input=calendar(0);input.shop.body.focus=1e-20f;input.jobs[0].queried_current_damage=1e-20f;
    input.jobs[0].members[0].hp=1;
    result=evaluate(input);
    check(result.status==Status::Unsupported && result.reason==Reason::NoFloatProgress,
          "a positive damage value that cannot change float32 HP cannot sustain an unbounded hit loop");
    input=calendar();input.jobs[0].valid_through=110;
    check(evaluate(input).status==Status::Unsupported,"expired actual availability is not extended to justify a purchase");
    input=calendar();Valuation capped(input);capped.advance(std::numeric_limits<std::size_t>::max());
    check(capped.last_work()<=max_advance_units,"even an oversized caller work request is clamped to64 units");
    check(allocations==allocation_start,"all actual shop projection and calendar work use owned fixed storage without heap allocation");
    std::printf("production investment: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
