// The provider oracle uses the unchanged general combat kernel, rather than
// the native cost table whose equivalence has its own differential test.
#include "../dividend/src/policy.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace p5;
using namespace p5::dividend;
static int checks=0;
#define CHECK(condition) do { ++checks; if(!(condition)) { std::fprintf(stderr,"Provider check %d failed line %d: %s\n",checks,__LINE__,#condition);std::abort(); } } while(false)
static RouteLeg route(int destination,int distance=0) { return {destination,distance,true,true}; }
static Observation base() {
    Observation in;
    in.settings={2,10,1,200,1000,2}; // Synthetic inputs; no copied engine setting.
    ScoreHistory history;CHECK(history.observe(0,0));in.windows=history.windows(0,200,1000,0);
    in.self.id=7;in.self.self=in.self.visible=in.self.own_team=true;
    in.self.bag={20,Knowledge::current,0};in.self.gold={0,Knowledge::current,0};
    in.stats.id=7;in.stats.self=in.stats.visible=true;
    in.stats.health=in.stats.max_health=100;in.stats.strength=10;
    in.current_to_altar=route(50);in.altar_to_spawn=route(60);in.bank_return=route(900);
    in.bank_legal=in.bank_route_safe=true;in.count=2;
    for(std::size_t i=0;i<in.count;++i) {
        auto& g=in.opportunities[i];g.group=int(i)+1;g.live_observed=g.reward_or_alive=true;
        g.gems=i?40:10;g.from_current=g.from_spawn=g.from_altar=route(g.group);
        if(!i)g.from_altar.distance=2;
        g.to_altar=route(50);g.next_productive=route(900);
        g.combat.actor_count=g.combat.monster_count=1;g.combat.actors[0]=in.stats;
        g.combat.monsters[0]={int(i),i?180.0:40.0,i?180.0:40.0,i?5.0:0.0,true};
    }
    in.purchase_count=1;in.purchases[0]={0,1,40,20,0,0,0,true};return in;
}

struct Oracle {
    CombatQuoteProvider* provider = nullptr;
    int calls = 0, fail_at = -1;
    int hold = 0;
    static CombatForecast quote(void* context, const Opportunity&, const CombatInput& combat) {
        auto& self = *static_cast<Oracle*>(context);
        CHECK(combat.hold_ticks == self.hold);
        if (++self.calls == self.fail_at) {
            self.provider->complete = false;
            return {};
        }
        return forecast_combat(combat);
    }
};

static void same(const Decision& a, const Decision& b) {
    CHECK(a.feasible == b.feasible && a.job == b.job && a.group == b.group &&
          a.purchase_family == b.purchase_family);
    CHECK(a.purchase_strength==b.purchase_strength && a.purchase_max_health==b.purchase_max_health &&
          a.purchase_basic_modifier==b.purchase_basic_modifier && a.purchase_min_health==b.purchase_min_health);
    CHECK(std::strcmp(a.first_action,b.first_action)==0 &&
          std::strcmp(a.abort_condition,b.abort_condition)==0);
    CHECK(a.forecast.state == b.forecast.state && a.forecast.capture_tick == b.forecast.capture_tick &&
          a.forecast.bank_tick == b.forecast.bank_tick && a.forecast.renewal_tick == b.forecast.renewal_tick);
    CHECK(a.forecast.combat.state == b.forecast.combat.state &&
          a.forecast.combat.clear_ticks == b.forecast.combat.clear_ticks &&
          a.forecast.combat.worst_camp_damage == b.forecast.combat.worst_camp_damage &&
          a.forecast.combat.remaining_health == b.forecast.combat.remaining_health);
    const auto& x=a.rank; const auto& y=b.rank;
    CHECK(x.status==y.status && x.Y==y.Y && x.D==y.D && x.M==y.M &&
          x.final_bag==y.final_bag && x.final_gold==y.final_gold && x.actor_ids==y.actor_ids &&
          x.actor_count==y.actor_count && x.earned_gold==y.earned_gold && x.kills==y.kills &&
          x.occupied_turns==y.occupied_turns && x.peak_bag==y.peak_bag &&
          x.productive_arrival==y.productive_arrival && x.target_id==y.target_id &&
          x.windows==y.windows && x.immediate_win==y.immediate_win &&
          x.official_endpoint==y.official_endpoint);
    const auto& s=a.schedule; const auto& t=b.schedule;
    CHECK(s.actor_count==t.actor_count && s.event_count==t.event_count &&
          s.target_id==t.target_id && s.starting_team_score==t.starting_team_score &&
          s.win_score==t.win_score && s.tribute_gems_per_tick==t.tribute_gems_per_tick &&
          s.tribute_min_ticks==t.tribute_min_ticks && s.terminal_win==t.terminal_win &&
          s.terminal_win_tick==t.terminal_win_tick &&
          s.immediate_legal_winning_bank==t.immediate_legal_winning_bank &&
          s.doctrine_id==t.doctrine_id && s.denial_evidenced==t.denial_evidenced &&
          s.denied_gems==t.denied_gems && s.denied_bank_tick==t.denied_bank_tick &&
          s.projected_kills==t.projected_kills);
    for(int i=0;i<s.actor_count;++i) {
        const auto& u=s.actors[i];const auto& v=t.actors[i];
        CHECK(u.id==v.id && u.self==v.self && u.visible==v.visible && u.bag_known==v.bag_known &&
              u.bag==v.bag && u.gold==v.gold && u.tribute_gold_per_gem==v.tribute_gold_per_gem &&
              u.occupied_turns==v.occupied_turns);
    }
    for(int i=0;i<s.event_count;++i) {
        const auto& u=s.events[i];const auto& v=t.events[i];
        CHECK(u.id==v.id && u.tick==v.tick && u.kind==v.kind && u.actor==v.actor &&
              u.group==v.group && u.payout_id==v.payout_id && u.participants==v.participants &&
              u.holders==v.holders && u.gems==v.gems && u.gold==v.gold &&
              u.waypoint==v.waypoint && u.legal==v.legal);
    }
}

static void compare(Observation& input) {
    Oracle oracle; oracle.hold=input.settings.capture_rounds+1;
    CombatQuoteProvider provider{&oracle,Oracle::quote,true}; oracle.provider=&provider;
    for(Arm arm:{Arm::active,Arm::route_first_control}) {
        const auto original=choose_job(input,{},Profile{arm});
        const auto supplied=choose_job(input,{},Profile{arm},&provider);
        CHECK(provider.complete);same(original,supplied);
    }
    for(std::size_t i=0;i<input.count;++i) for(bool bank_first:{false,true}) {
        same(project(input,input.opportunities[i],bank_first),
             project(input,input.opportunities[i],bank_first,nullptr,&provider));
        if(bank_first) for(std::size_t p=0;p<input.purchase_count;++p)
            same(project(input,input.opportunities[i],true,&input.purchases[p]),
                 project(input,input.opportunities[i],true,&input.purchases[p],&provider));
    }
}

int main() {
    auto enabling=base();
    CHECK(choose_job(enabling,{},Profile{Arm::active}).job==Job::finance_produce);
    CHECK(choose_job(enabling,{},Profile{Arm::route_first_control}).job==Job::produce_bank);
    compare(enabling);
    int cases=0;
    for(int test=0;test<192;++test) {
        auto input=base();
        input.stats.health=20+(test%5)*20;
        input.stats.ranged=(test&1)!=0;
        input.stats.strength=5+(test%7)*5;
        input.stats.basic_modifier=(test%3==0)?1.25:1.0;
        input.stats.shield=(test%4==0)?80:0;
        input.stats.shield_expiry_tick=test%24;
        input.stats.visible_enemy_next_phase=test%3;
        input.self.bag.value=(test%4==0)?0:(test%4==1)?6:20;
        input.self.gold.value=test%4==2?40:0;
        input.current_to_altar.distance=test%5;
        input.altar_to_spawn.distance=test%7;
        input.purchase_count=3;
        input.purchases[0]={0,1,30,20,0,0,0,true};
        input.purchases[1]={1,1,30,0,60,0,0,true};
        input.purchases[2]={2,1,30,0,0,0,8,true};
        input.count=test%8==0?8:2;
        for(std::size_t i=2;i<input.count;++i) {
            input.opportunities[i]=input.opportunities[0];
            auto& g=input.opportunities[i];g.group=int(i)+1;g.gems=10+int(i)*7;
            g.from_current.destination=g.from_spawn.destination=g.from_altar.destination=g.group;
        }
        for(std::size_t i=0;i<input.count;++i) {
            auto& g=input.opportunities[i];
            g.from_current.distance=(test+int(i))%5;
            g.from_altar.distance=(test+2*int(i))%9;
            g.from_spawn.distance=(test+3*int(i))%7;
            g.to_altar.distance=test%4;g.next_productive.distance=test%6;
            g.combat.monster_count=1+test%3;
            for(std::size_t m=0;m<g.combat.monster_count;++m)
                g.combat.monsters[m]={int(10*i+m),30.0+20*m,20.0+10*m,double((test+int(m))%5),bool((test+int(m))%2)};
        }
        if(test%16==0) input.opportunities[1].from_spawn.safety_checked=false;
        if(test%17==0) input.opportunities[1].next_productive.distance=-1;
        if(test%19==0) input.stats.enemy_cast_bound_known=false;
        if(test%23==0) input.opportunities[1].group=input.opportunities[0].group;
        compare(input);++cases;
    }
    auto input=base();
    {
        auto variant=base();variant.count=1;variant.bank_return={};
        auto& g=variant.opportunities[0];g.variant_returns_known=true;
        g.next_productive=route(999); // A tempting legacy union must be ignored.
        g.variant_returns[0]={};g.variant_returns[1]=route(901);
        CHECK(!project(variant,g,false).feasible && !project(variant,g,true).feasible);
        const auto financed=project(variant,g,true,&variant.purchases[0]);
        CHECK(financed.feasible && financed.job==Job::finance_produce);
        CHECK(financed.schedule.events[financed.schedule.event_count-1].waypoint==901);
        CHECK(choose_job(variant,{},Profile{Arm::active}).job==Job::finance_produce);
        CHECK(!choose_job(variant,{},Profile{Arm::route_first_control}).feasible);
        compare(variant);
        // Both kits may have a return, but timing and waypoint stay distinct.
        g.variant_returns[0]=route(902,20);
        const auto direct=project(variant,g,false);
        CHECK(direct.feasible && direct.schedule.events[direct.schedule.event_count-1].waypoint==902);
        CHECK(direct.forecast.renewal_tick > financed.forecast.renewal_tick);
        compare(variant);
    }
    for(Arm arm:{Arm::active,Arm::route_first_control}) {
        Oracle oracle;oracle.hold=input.settings.capture_rounds+1;oracle.fail_at=2;
        CombatQuoteProvider provider{&oracle,Oracle::quote,true};oracle.provider=&provider;
        const auto answer=choose_job(input,{},Profile{arm},&provider);
        CHECK(oracle.calls>=2 && !provider.complete && !answer.feasible);
        CHECK(answer.job==Job::unavailable);
        // A caller cannot recover an early winning bank from an already
        // incomplete provider and mistake that for a complete matched menu.
        input.team_score=990;
        CHECK(!choose_job(input,{},Profile{arm},&provider).feasible);
        input.team_score=0;
    }
    {
        Oracle oracle;oracle.hold=input.settings.capture_rounds+1;oracle.fail_at=2;
        CombatQuoteProvider provider{&oracle,Oracle::quote,true};oracle.provider=&provider;
        CHECK(!choose_purchase(input,input.opportunities[0],&provider).feasible);
        CHECK(oracle.calls>=2 && !provider.complete);
    }
    CombatQuoteProvider unavailable;
    CHECK(!choose_job(input,{},Profile{Arm::active},&unavailable).feasible && !unavailable.complete);
    input.safe_channel=true;
    CHECK(choose_job(input,{},Profile{Arm::active},&unavailable).job==Job::preserve);
    input.safe_channel=false;input.safe_hold=true;
    CHECK(choose_job(input,{},Profile{Arm::route_first_control},&unavailable).job==Job::preserve);
    input.safe_hold=false;
    input.count=0;
    compare(input); // Winning and ordinary bank-only paths need no combat.
    input.team_score=990;compare(input);
    std::printf("Dividend provider differential: %d cases, %d checks passed\n",cases,checks);
    return 0;
}
