#include "../dividend/src/basic-quotes.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace p5;
namespace q=p5::dividend::basic_quotes;
static int checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"Dividend quote check%d line%d: %s\n",checks,__LINE__,#x);std::abort();}}while(false)
static CombatActor actor(double health=100,double maximum=100,double strength=10,bool ranged=false,double modifier=1){
    CombatActor a;a.id=7;a.self=a.visible=true;a.health=health;a.max_health=maximum;a.strength=strength;a.ranged=ranged;a.basic_modifier=modifier;return a;
}
static CombatInput camp(double hp=20,double strength=3){
    CombatInput c;c.actor_count=c.monster_count=1;c.actors[0]=actor();c.monsters[0]={101,hp,hp,strength,true};c.hold_ticks=6;return c;
}
static void equal(const CombatForecast& a,const CombatForecast& b){
    CHECK(a.state==b.state);CHECK(a.clear_ticks==b.clear_ticks);CHECK(a.worst_camp_damage==b.worst_camp_damage);
    CHECK(a.remaining_health[0]==b.remaining_health[0]);
}
static void differential(){
    auto input=camp();input.monster_count=3;input.monsters[0]={103,25,0,1,false};
    input.monsters[1]={102,30,20,3,true};input.monsters[2]={101,20,20,2,true};
    q::CostTable table;
    for(double strength:{0.,1.,10.,20.})for(bool ranged:{false,true})for(double modifier:{0.,.5,1.,1.5}){
        CHECK(table.reset(7,true));CHECK(table.set_group(0,9,true,input));
        const int p=table.profile(actor(100,100,strength,ranged,modifier));CHECK(p==0 && !table.is_cached(0,p));
        CHECK(table.get(0,p).state==q::CostState::invalid && table.kernel_calls()==0);
        const auto cost=table.compute(0,p);CHECK(cost.state==q::CostState::valid && table.kernel_calls()==1);
        for(double health:{1.,20.,70.,100.,200.})for(double extra:{0.,20.,100.})for(double fraction:{0.,.25,.5}){
            const double maximum=health+extra,loss=health*fraction;
            auto direct=input;direct.actors[0]=actor(health-loss,maximum,strength,ranged,modifier);
            equal(q::reprice(cost,health,maximum,loss),forecast_combat(direct));
            CHECK(table.profile(actor(health,maximum,strength,ranged,modifier))==p);
            CHECK(table.compute(0,p).worst_camp_damage==cost.worst_camp_damage && table.kernel_calls()==1);
        }
    }
}
static void limits_and_cache(){
    q::CostTable table;CHECK(table.reset(7,true));auto input=camp(120,10);CHECK(table.set_group(0,4,true,input));
    const int base=table.profile(actor()),health=table.profile(actor(100,200)),focus=table.profile(actor());
    CHECK(base==health && base==focus && table.profile_count()==1);
    auto cost=table.compute(0,base);CHECK(cost.state==q::CostState::valid && cost.worst_camp_damage==120);
    CHECK(q::reprice(cost,100,100).state==EstimateState::unsafe); // Canonical unsafe is a valid numeric cost.
    CHECK(q::reprice(cost,100,200).state==EstimateState::unsafe); // Health adds no HP.
    const int shard=table.profile(actor(100,100,20));CHECK(shard==1);
    const auto funded=table.compute(0,shard);CHECK(funded.clear_ticks==6 && funded.worst_camp_damage==60);
    CHECK(q::reprice(funded,100,100).state==EstimateState::feasible);
    CHECK(q::reprice(funded,100,100,20).remaining_health[0]==20 && q::reprice(funded,100,100,20).state==EstimateState::feasible);
    CHECK(q::reprice(funded,100,100,21).state==EstimateState::unsafe);
    CHECK(q::reprice(funded,100,200,20).state==EstimateState::unsafe); // A larger reserve is binding.
    CHECK(q::reprice(funded,100,100,100).state==EstimateState::invalid);
    CHECK(table.profile(actor(100,100,30))==2 && table.profile(actor(100,100,40))==3);
    CHECK(table.profile(actor(100,100,50))==-1 && table.profile_count()==4);
    CHECK(table.kernel_calls()==2);
    input=camp(20,3);CHECK(table.set_group(0,4,true,input) && !table.is_cached(0,base));
    cost=table.compute(0,base);CHECK(cost.clear_ticks==2 && cost.worst_camp_damage==6 && table.kernel_calls()==3);
    CHECK(!table.set_group(1,4,true,input) && table.compute(1,base).state==q::CostState::invalid);
    CHECK(table.reset(7,true) && !table.is_cached(0,base) && table.get(0,base).state==q::CostState::invalid && table.kernel_calls()==0);
    CHECK(!table.reset(7,false) && table.profile(actor())==-1 && !table.set_group(0,4,true,input));
    CHECK(!table.reset(-1,true));
    CHECK(table.reset(7,true));
    for(int g=0;g<q::group_cap;++g)CHECK(table.set_group(g,100+g,true,camp(20+g,3)));
    for(int p=0;p<q::profile_cap;++p)CHECK(table.profile(actor(100,100,10+p))==p);
    for(int g=0;g<q::group_cap;++g)for(int p=0;p<q::profile_cap;++p)CHECK(table.compute(g,p).state==q::CostState::valid);
    CHECK(table.kernel_calls()==32 && !table.set_group(8,999,true,input));
    (void)table.compute(7,3);CHECK(table.kernel_calls()==32);
}
static void rounding(){
    q::CostTable table;CHECK(table.reset(7,true));CHECK(table.set_group(0,1,true,camp()));
    const int exact=table.profile(actor()),below=table.profile(actor(100,100,std::nextafter(10.,0.))),ranged=table.profile(actor(100,100,10,true)),floor=table.profile(actor(100,100,0));
    CHECK(table.compute(0,exact).clear_ticks==2 && table.compute(0,below).clear_ticks==3);
    CHECK(table.compute(0,ranged).clear_ticks==4 && table.compute(0,floor).clear_ticks==20);
    const auto cost=table.compute(0,exact);
    CHECK(q::reprice(cost,26,100).state==EstimateState::feasible);
    CHECK(q::reprice(cost,std::nextafter(26.,0.),100).state==EstimateState::unsafe);
    for(int hold:{0,1,6,100000}){auto input=camp();input.hold_ticks=hold;
        CHECK(table.set_group(0,1,true,input));
        equal(q::reprice(table.compute(0,exact),100,100),forecast_combat(input));
    }
}
static void rejected_inputs(){
    q::CostTable table;const double nan=std::numeric_limits<double>::quiet_NaN();
    for(int bad=0;bad<15;++bad){CHECK(table.reset(7,true));auto input=camp();
        if(bad==0)input.actor_count=0;if(bad==1)input.actor_count=2;
        if(bad==2)input.monster_count=0;if(bad==3)input.monster_count=9;
        if(bad==4)input.hold_ticks=-1;if(bad==5)input.monsters[0].id=-1;
        if(bad==6)input.monsters[0].public_health=0;if(bad==7)input.monsters[0].observed_health=0;
        if(bad==8)input.monsters[0].observed_health=21;if(bad==9)input.monsters[0].effective_strength=-1;
        if(bad==10){input.monster_count=2;input.monsters[1]=input.monsters[0];}
        if(bad==11)input.monsters[0].public_health=nan;if(bad==12)input.monsters[0].observed_health=nan;
        if(bad==13)input.monsters[0].effective_strength=nan;if(bad==14)input.actors[0].id=8;
        CHECK(!table.set_group(0,1,true,input));const int p=table.profile(actor());
        CHECK(table.compute(0,p).state==q::CostState::invalid && table.kernel_calls()==0);
    }
    for(int bad=0;bad<13;++bad){CHECK(table.reset(7,true));auto a=actor();
        if(bad==0)a.shield=1;if(bad==1)a.shield_expiry_tick=1;if(bad==2)a.heal_amount=1;
        if(bad==3)a.heal_legal_ready=true;if(bad==4)a.heal_recipient=7;if(bad==5)a.direct_or_reset_damage=1;
        if(bad==6)a.spell_known_ready=true;if(bad==7)a.legal_first_cast=true;if(bad==8)a.reserved_cast=true;
        if(bad==9)a.visible_enemy_next_phase=1;if(bad==10)a.health=nan;
        if(bad==11)a.strength=nan;if(bad==12){a.strength=1e308;a.basic_modifier=1e308;}
        CHECK(table.profile(a)==-1);
    }
    CHECK(table.reset(7,true));auto input=camp();input.actors[0].enemy_cast_bound_known=false;
    CHECK(!table.set_group(0,1,true,input));int p=table.profile(actor());auto unknown=table.compute(0,p);
    CHECK(unknown.state==q::CostState::unknown && q::reprice(unknown,100,100).state==EstimateState::unknown && table.kernel_calls()==0);
    CHECK(!table.set_group(1,2,false,camp()) && table.compute(1,p).state==q::CostState::unknown);
    CHECK(table.compute(-1,p).state==q::CostState::invalid && table.compute(8,p).state==q::CostState::invalid && table.compute(0,4).state==q::CostState::invalid);
    CHECK(q::reprice({},100,100).state==EstimateState::invalid && q::reprice({},nan,100).state==EstimateState::invalid);
}
static int continuation(q::CostTable& table,int p,int excluded,double loss){
    const std::array<int,3> ids{9,8,7},distance{4,4,1};int best=-1;
    for(int i=0;i<3;++i)if(ids[i]!=excluded && q::reprice(table.compute(i,p),100,100,loss).state==EstimateState::feasible &&
        (best<0 || distance[i]<distance[best] || (distance[i]==distance[best] && ids[i]<ids[best])))best=i;
    return best<0?-1:ids[best];
}
static void route_ties(){
    q::CostTable table;CHECK(table.reset(7,true));
    CHECK(table.set_group(0,9,true,camp(10,10)) && table.set_group(1,8,true,camp(20,10)) && table.set_group(2,7,true,camp(90,10)));
    const int p=table.profile(actor());CHECK(continuation(table,p,-1,0)==8);
    CHECK(continuation(table,p,8,0)==9 && continuation(table,p,9,0)==8);
    CHECK(continuation(table,p,-1,75)==-1 && table.kernel_calls()==3);
}
static void specialized_kernel(){
    q::CostTable table;
    // One through eight members, deliberately unsorted and tied HP, with
    // fractional incoming damage that makes summation order observable.
    for(int count=1;count<=8;++count)for(int sample=0;sample<16;++sample){
        auto input=camp();input.monster_count=std::size_t(count);
        for(int m=0;m<count;++m){
            const double hp=1+(sample*17+m*13)%97;
            input.monsters[m]={200+count-m,hp+5,(m+sample)%3?hp:0,
                (sample+m)%4==0?1e-10:((sample*3+m*7)%19)/3.,(m+sample)%3!=0};
        }
        CHECK(table.reset(7,true) && table.set_group(0,9,true,input));
        for(int offense=0;offense<4;++offense){
            const double strength=offense?7.25*offense:0,modifier=offense==3?1.1:1.;
            const bool ranged=offense%2!=0;
            const int profile=table.profile(actor(10000,10000,strength,ranged,modifier));
            const auto cost=table.compute(0,profile);CHECK(cost.state==q::CostState::valid);
            for(double hp:{1.,100.,10000.}){
                auto direct=input;direct.actors[0]=actor(hp,hp+20,strength,ranged,modifier);
                equal(q::reprice(cost,hp,hp+20),forecast_combat(direct));
            }
        }
        CHECK(table.kernel_calls()==4);
    }
    auto input=camp(100001,0);CHECK(table.reset(7,true) && table.set_group(0,1,true,input));
    int profile=table.profile(actor(100,100,0));
    CHECK(table.compute(0,profile).state==q::CostState::invalid && forecast_combat(input).state==EstimateState::feasible);
    input.actors[0]=actor(100,100,0);CHECK(forecast_combat(input).state==EstimateState::invalid);
    input=camp(100000,0);input.actors[0]=actor(100,100,0);
    CHECK(table.set_group(0,1,true,input));equal(q::reprice(table.compute(0,profile),100,100),forecast_combat(input));
    input=camp(1,1e308);input.monster_count=2;input.monsters[1]={102,1,1,1e308,true};
    CHECK(table.set_group(0,1,true,input) && table.compute(0,profile).state==q::CostState::invalid);
}
static void exposed_tail(){
    q::CostTable table;auto input=camp(29,2.25);input.monster_count=2;
    input.monsters[1]={102,19,19,1./3,true};
    CHECK(table.reset(7,true) && table.set_group(0,1,true,input));
    const int p=table.profile(actor());const auto cost=table.compute(0,p);
    for(double hp:{50.,100.,1000.})for(double maximum:{1000.,1200.})for(double threat:{0.,1./3,10.,50.})
        for(int hold:{0,6,20})for(double first:{0.,10.}){
            const double actual=hp-(first+threat*3);if(actual<=0)continue;
            auto direct=input;direct.hold_ticks=hold;direct.actors[0]=actor(actual,maximum);
            direct.actors[0].visible_enemy_next_phase=threat;
            equal(q::reprice_exposed(cost,actual,maximum,threat,hold),forecast_combat(direct));
        }
    input=camp(1,0);CHECK(table.set_group(0,1,true,input));
    const auto harmless=table.compute(0,p);
    CHECK(q::reprice_exposed(harmless,100,100,40,0).state==EstimateState::unsafe); // Two-hit reserve80, remaining60.
    CHECK(q::reprice_exposed(harmless,100,100,-1,0).state==EstimateState::invalid);
    CHECK(q::reprice_exposed(harmless,100,100,0,-1).state==EstimateState::invalid);
    CHECK(q::reprice_exposed(harmless,100,100,std::numeric_limits<double>::infinity(),0).state==EstimateState::invalid);
    CHECK(q::reprice_exposed(harmless,100,100,0,std::numeric_limits<int>::max()).state==EstimateState::invalid);
}
int main(){differential();limits_and_cache();rounding();rejected_inputs();route_ties();specialized_kernel();exposed_tail();
    std::printf("Dividend basic quote checks passed: %d (differential synthetic, no native gameplay)\n",checks);}
