#include "../src/escape.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <type_traits>

namespace A=mosaic::adaptive;
static unsigned checks{},groups{};
static void check(bool value,const char* expression,int line){
    ++checks;if(!value){std::fprintf(stderr,"escape check failed at %d: %s\n",line,expression);std::abort();}
}
#define CHECK(...) check(bool(__VA_ARGS__),#__VA_ARGS__,__LINE__)
static constinit A::Escape cold_escape{};
static_assert(!std::is_copy_constructible_v<A::Escape>&&!std::is_move_constructible_v<A::Escape>);
static_assert(sizeof(A::Escape)<2048);

// Actual Route/Navigation and Escape production code. Admissions count bounded
// logical leaves, not native fuel or SDK command permission. Raw source amounts
// stand for a complete conservative NPC union; they do not model player replies.
template<std::size_t Cells> static void finish(A::Route<Cells>& route){
    unsigned guard=0;
    while(route.navigation().status()==A::RouteStatus::preparing){
        const auto result=route.advance(13,[]{return true;});CHECK(result.units<=13&&result.progressed);
        CHECK(++guard<Cells*4+300);
    }
}
template<std::size_t Cells> static A::EscapeKey key(const A::Route<Cells>& route,std::uint64_t source=1){
    return {route.revision(),route.semantic(),source};
}
static A::game::SelfFact body(A::Epoch epoch={4,0,0},A::Point pose={0,0},float hp=1000){
    A::game::SelfFact self{};self.epoch=epoch;self.pose=pose;self.hp=self.max_hp=hp;
    self.alive=self.body_actual=self.buffs_actual=true;return self;
}
template<std::size_t Cells,class Source> static void finish(A::Escape& escape,const A::Route<Cells>& route,Source source){
    unsigned guard=0;
    while(escape.status()==A::EscapeStatus::preparing){
        unsigned admits=0;const auto result=escape.advance(route,source,[&]{++admits;return true;},3);
        CHECK(result.units==admits&&result.units<=3&&(result.progressed||result.status!=A::EscapeStatus::preparing));CHECK(++guard<1000);
    }
}

static void cold_denial_and_partial_candidate_retention(){
    ++groups;A::Route<25> route;std::array<std::uint8_t,25> board{};board.fill(1);
    CHECK(route.begin(5,5,board,std::array<A::Point,1>{{{4,4}}},9));finish(route);
    A::Escape escape;CHECK(escape.begin({4,0,0},{0,0},key(route)));
    unsigned samples=0,admits=0;
    auto source=[&](A::Point p){++samples;return A::EscapePotential{p==A::Point{4,4}?0u:10u,true};};
    const auto denied=escape.advance(route,source,[&]{++admits;return false;},64);
    CHECK(denied.units==0&&samples==0&&admits==1&&!denied.progressed&&!escape.complete());
    auto partial=escape.advance(route,source,[]{return true;},2);
    CHECK(partial.units==2&&partial.source_samples==1&&samples==1&&escape.steps()==0);
    CHECK(escape.begin({5,0,0},{0,0},key(route)));
    CHECK(escape.advance(route,source,[]{return true;},1).source_samples==1&&samples==2);
    CHECK(escape.steps()==1&&escape.next()==A::Point{1,1});
    CHECK(!escape.price(body({5,0,0}),key(route)).complete);
    const auto sampled=samples;CHECK(escape.begin({6,0,0},{0,0},key(route)));
    CHECK(escape.advance(route,source,[]{return false;},64).units==0&&samples==sampled);
    finish(escape,route,source);CHECK(escape.complete()&&escape.steps()==4&&escape.path().back()==A::Point{4,4});
    const auto complete_samples=samples;CHECK(escape.begin({7,0,0},{0,0},key(route)));
    CHECK(escape.advance(route,source,[]{return false;},64).units==0&&samples==complete_samples);
    CHECK(escape.price(body({7,0,0}),key(route)).complete);
    CHECK(!escape.price(body({6,0,0}),key(route)).complete);
    CHECK(!escape.begin({6,0,0},{0,0},key(route)));
}

static void stable_minimum_source_step_and_early_quiet(){
    ++groups;A::Route<25> route;std::array<std::uint8_t,25> board{};board.fill(1);
    const std::array<A::Point,2> roots{{{4,0},{4,4}}};CHECK(route.begin(5,5,board,roots,7));finish(route);
    A::Escape escape;CHECK(escape.begin({1,0,0},{0,2},key(route)));
    auto source=[](A::Point p){return A::EscapePotential{p==A::Point{1,3}?0u:p.y==3?2u:9u,true};};
    finish(escape,route,source);
    CHECK(escape.complete()&&escape.steps()==1&&escape.next()==A::Point{1,3});
    CHECK(route.navigation().distance({1,3})==3); // quiet before the destination root
    auto self=body({1,0,0},{0,2});const auto immediate=escape.price(self,key(route),0,false);
    CHECK(immediate.complete&&immediate.hp_loss==0&&immediate.phases==1);
    CHECK(escape.price(self,key(route),1,false).hp_loss==9);
    CHECK(escape.price(self,key(route),0,true).hp_loss==9);
    CHECK(escape.price(self,key(route),1,true).hp_loss==18);
    CHECK(escape.begin({2,0,0},{0,2},key(route,2)));
    auto tied=[](A::Point p){return A::EscapePotential{p.x==1?0u:10u,true};};finish(escape,route,tied);
    CHECK(escape.next()==A::Point{1,1}); // Navigation compass order resolves equal costs
}

static void per_origin_ready_does_not_require_global_ready(){
    ++groups;A::Route<100> route;std::array<std::uint8_t,100> board{};board.fill(1);
    CHECK(route.begin(10,10,board,std::array<A::Point,1>{{{0,0}}},4));
    unsigned guard=0;
    while(!route.navigation().available({1,0})){
        CHECK(route.advance(1,[]{return true;}).units==1);CHECK(++guard<400);
    }
    CHECK(route.navigation().status()==A::RouteStatus::preparing);
    A::Escape escape;CHECK(escape.begin({2,0,0},{1,0},key(route)));
    finish(escape,route,[](A::Point p){return A::EscapePotential{p==A::Point{0,0}?0u:5u,true};});
    CHECK(escape.complete()&&escape.next()==A::Point{0,0});
    CHECK(route.navigation().status()==A::RouteStatus::preparing);
}

static void quiet_unknown_and_short_tail_are_distinct(){
    ++groups;A::Route<70> route;std::array<std::uint8_t,70> board{};board.fill(1);
    CHECK(route.begin(70,1,board,std::array<A::Point,1>{{{69,0}}},3));finish(route);
    A::Escape escape;CHECK(escape.begin({1,0,0},{0,0},key(route)));
    unsigned samples=0;
    finish(escape,route,[&](A::Point){++samples;return A::EscapePotential{0,true};});
    CHECK(samples==1&&escape.status()==A::EscapeStatus::quiet&&escape.complete()&&escape.path().empty());
    CHECK(escape.price(body({1,0,0}),key(route),1,true).hp_loss==0);
    CHECK(escape.begin({2,0,0},{0,0},key(route,2)));
    finish(escape,route,[](A::Point){return A::EscapePotential{0,false};});
    CHECK(escape.status()==A::EscapeStatus::invalid&&!escape.complete()&&!escape.price(body({2,0,0}),key(route,2)).complete);
    CHECK(escape.begin({3,0,0},{0,0},key(route,3)));
    finish(escape,route,[](A::Point p){return A::EscapePotential{p.x==69?0u:1u,true};});
    CHECK(escape.status()==A::EscapeStatus::unsupported&&escape.steps()==64&&!escape.complete());
    CHECK(!escape.price(body({3,0,0}),key(route,3)).complete);
    A::Route<1> idle;CHECK(escape.begin({4,0,0},{0,0},key(idle,4)));
    finish(escape,idle,[](A::Point){return A::EscapePotential{0,true};});
    CHECK(escape.status()==A::EscapeStatus::quiet); // a complete quiet source does not need a cold field
}

static void accepted_prefix_and_actual_epoch_dependencies(){
    ++groups;A::Route<4> route;std::array<std::uint8_t,4> board{};board.fill(1);
    CHECK(route.begin(4,1,board,std::array<A::Point,1>{{{3,0}}},8));finish(route);
    A::Escape escape;CHECK(escape.begin({1,0,0},{0,0},key(route)));
    finish(escape,route,[](A::Point p){return A::EscapePotential{p.x==3?0u:10u,true};});
    CHECK(escape.steps()==3&&escape.next()==A::Point{1,0});
    auto denied=escape.observe_move({1,{1,0,0},{0,0},{1,0},A::MoveOutcome::not_attempted},{1,0,0},{0,0});
    CHECK(denied.recorded&&!denied.trimmed&&!denied.reset&&escape.steps()==3);
    auto refused=escape.observe_move({2,{1,0,0},{0,0},{1,0},A::MoveOutcome::refused},{1,0,1},{0,0});
    CHECK(refused.recorded&&!refused.trimmed&&escape.steps()==3);
    CHECK(escape.begin({1,0,1},{0,0},key(route)));
    const A::MoveRecord receipt{3,{1,0,1},{0,0},{1,0},A::MoveOutcome::applied};
    const auto accepted=escape.observe_move(receipt,{1,1,2},{1,0});
    CHECK(accepted.trimmed&&!accepted.reset&&escape.origin()==A::Point{1,0}&&escape.steps()==2);
    CHECK(escape.epoch()==A::Epoch{1,1,2}&&escape.next()==A::Point{2,0});
    CHECK(!escape.observe_move(receipt,{1,1,2},{1,0}).recorded&&escape.steps()==2);
    CHECK(escape.price(body({1,1,2},{1,0}),key(route),0,false).hp_loss==10);
    CHECK(escape.observe_move({4,{1,1,2},{1,0},{2,0},A::MoveOutcome::applied},{1,2,3},{2,0}).trimmed);
    CHECK(escape.observe_move({5,{1,2,3},{2,0},{3,0},A::MoveOutcome::applied},{1,3,4},{3,0}).trimmed);
    CHECK(escape.status()==A::EscapeStatus::quiet&&escape.steps()==0&&!escape.next());
    CHECK(escape.begin({2,0,0},{0,0},key(route)));
    finish(escape,route,[](A::Point p){return A::EscapePotential{p.x==3?0u:1u,true};});
    CHECK(escape.observe_move({6,{2,0,0},{0,0},{1,0},A::MoveOutcome::unknown},{2,0,0},{0,0}).recorded);
    CHECK(escape.steps()==3);
    CHECK(escape.observe_move({6,{2,0,0},{0,0},{1,0},A::MoveOutcome::applied},{2,1,1},{1,0}).trimmed);
    CHECK(escape.steps()==2);
    CHECK(escape.observe_move({7,{2,1,1},{1,0},{3,0},A::MoveOutcome::applied},{2,2,2},{3,0}).reset);
    CHECK(escape.status()==A::EscapeStatus::idle&&!escape.complete());
}

static std::uint64_t brute_loss(std::span<const std::uint64_t> potential,unsigned holds,
                               std::uint64_t shield,unsigned shield_timer,int refusal){
    std::uint64_t hp_loss=0;
    auto phase=[&](std::uint64_t amount){
        // Explicit event order, independent of Escape's phase-count formula:
        // current owner END decrements, next CAMP uses the present shield,
        // then the following owner START removes it if its timer reached0.
        if(shield_timer)--shield_timer;
        const auto absorbed=std::min(shield,amount);shield-=absorbed;hp_loss+=amount-absorbed;
        if(shield_timer==0)shield=0;
    };
    for(unsigned i=0;i<holds;++i)phase(potential[0]);
    for(unsigned i=0;i+1<potential.size();++i){if(int(i)==refusal)phase(potential[i]);phase(potential[i+1]);}
    return hp_loss;
}
static void shield_expiry_and_every_single_refusal_position(){
    ++groups;A::Route<7> route;std::array<std::uint8_t,7> board{};board.fill(1);
    CHECK(route.begin(7,1,board,std::array<A::Point,1>{{{6,0}}},10));finish(route);
    const std::array<std::uint64_t,7> potentials{{7,3,11,2,9,1,0}};
    A::Escape escape;CHECK(escape.begin({1,0,0},{0,0},key(route)));
    finish(escape,route,[&](A::Point p){return A::EscapePotential{potentials[unsigned(p.x)],true};});
    for(unsigned holds=0;holds<=3;++holds)for(unsigned shield=0;shield<=45;++shield)for(unsigned timer=0;timer<=8;++timer){
        auto self=body({1,0,0});self.shield=float(shield);self.shield_time=int(timer);
        const auto plain=escape.price(self,key(route),holds,false);
        CHECK(plain.complete&&plain.survivable&&plain.hp_loss==brute_loss(potentials,holds,shield,timer,-1));
        auto worst=plain.hp_loss;
        for(int refusal=0;refusal<6;++refusal)worst=std::max(worst,brute_loss(potentials,holds,shield,timer,refusal));
        const auto contingent=escape.price(self,key(route),holds,true);
        CHECK(contingent.complete&&contingent.hp_loss==worst&&contingent.phases==holds+7);
    }
    auto self=body({1,0,0});self.shield=100;self.shield_time=0;
    CHECK(escape.price(self,key(route),0,false).hp_loss==23); // only first destination3 is shielded
    CHECK(escape.price(self,key(route),1,false).hp_loss==26); // origin7 shielded; every destination is after expiry
    self.buffs_actual=false;CHECK(!escape.price(self,key(route),0,false).shield_used&&escape.price(self,key(route),0,false).hp_loss==26);
    self.buffs_actual=true;self.shield_time=-1;CHECK(!escape.price(self,key(route),0,false).shield_used);
    self.shield=0;self.hp=26;CHECK(!escape.price(self,key(route),0,false).survivable);
    self.hp=27;CHECK(escape.price(self,key(route),0,false).survivable);
    CHECK(!escape.price(self,key(route),65,false).complete);
    self.hp=std::numeric_limits<float>::quiet_NaN();CHECK(!escape.price(self,key(route)).complete);
}

static void timer1_expires_after_the_first_future_camp_and_timer2_after_the_second(){
    ++groups;A::Route<4> route;std::array<std::uint8_t,4> board{};board.fill(1);
    CHECK(route.begin(4,1,board,std::array<A::Point,1>{{{3,0}}},13));finish(route);
    const std::array<std::uint64_t,4> potentials{{10,20,30,0}};
    A::Escape escape;CHECK(escape.begin({1,0,0},{0,0},key(route)));
    finish(escape,route,[&](A::Point p){return A::EscapePotential{potentials[unsigned(p.x)],true};});
    auto self=body({1,0,0});self.shield=100;
    self.shield_time=1;
    CHECK(escape.price(self,key(route),0,false).hp_loss==30); // END1->0, camp20 shielded, START expiry, camp30 hitsHP
    CHECK(escape.price(self,key(route),1,false).hp_loss==50); // held-origin10 uses the sole covered camp
    CHECK(escape.price(self,key(route),0,true).hp_loss==60); // refusal at30 after expiry
    self.shield_time=2;
    CHECK(escape.price(self,key(route),0,false).hp_loss==0); // camp20, camp30 both before expiry
    CHECK(escape.price(self,key(route),1,false).hp_loss==30); // held-origin10, destination20 covered;30 after expiry
    CHECK(escape.price(self,key(route),0,true).hp_loss==30); // at most one late extra30 phase
    self.shield_time=0;CHECK(escape.price(self,key(route),0,false).hp_loss==30);
}

static void conditional_ingress_prices_actual_body_then_landing_and_exit(){
    ++groups;A::Route<4> route;std::array<std::uint8_t,4> board{};board.fill(1);
    CHECK(route.begin(4,1,board,std::array<A::Point,1>{{{3,0}}},14));finish(route);
    const std::array<std::uint64_t,4> potentials{{7,10,20,0}};
    A::Escape escape;CHECK(escape.begin({1,0,0},{1,0},key(route)));
    finish(escape,route,[&](A::Point p){return A::EscapePotential{potentials[unsigned(p.x)],true};});
    CHECK(escape.steps()==2&&escape.origin()==A::Point{1,0});
    auto actual=body({1,0,0},{0,0});actual.raw_m=actual.restrictions_actual=actual.channel_actual=true;
    const A::EscapePotential actual_origin{7,true};
    CHECK(!escape.price(actual,key(route)).complete); // actual pose is never rewritten to a hypothetical landing
    const auto plain=escape.price_move(actual,key(route),actual_origin,false);
    CHECK(plain.complete&&plain.survivable&&plain.hp_loss==30&&plain.raw_damage==30&&plain.phases==3);
    const auto refused=escape.price_move(actual,key(route),actual_origin,true);
    CHECK(refused.complete&&refused.hp_loss==50&&refused.raw_damage==50&&refused.phases==4);
    CHECK(actual.pose==A::Point{0,0}&&actual.epoch==A::Epoch{1,0,0}&&actual.hp==1000&&actual.raw_m);
    CHECK(escape.origin()==A::Point{1,0}&&escape.steps()==2&&escape.epoch()==A::Epoch{1,0,0});
    actual.shield=100;actual.shield_time=1;
    CHECK(escape.price_move(actual,key(route),actual_origin,false).hp_loss==20); // landing10 shielded, suffix20 after expiry
    CHECK(escape.price_move(actual,key(route),actual_origin,true).hp_loss==40); // refusal at suffix20 after expiry
    actual.shield_time=2;
    CHECK(escape.price_move(actual,key(route),actual_origin,false).hp_loss==0);
    CHECK(escape.price_move(actual,key(route),actual_origin,true).hp_loss==20);
    for(unsigned timer=0;timer<=6;++timer)for(unsigned shield=0;shield<=40;++shield){
        actual.shield=float(shield);actual.shield_time=int(timer);
        auto worst=brute_loss(potentials,0,shield,timer,-1);
        for(int at=0;at<3;++at)worst=std::max(worst,brute_loss(potentials,0,shield,timer,at));
        CHECK(escape.price_move(actual,key(route),actual_origin,false).hp_loss==brute_loss(potentials,0,shield,timer,-1));
        CHECK(escape.price_move(actual,key(route),actual_origin,true).hp_loss==worst);
    }
    actual.shield=0;actual.hp=50;CHECK(!escape.price_move(actual,key(route),actual_origin,true).survivable);
    actual.hp=51;CHECK(escape.price_move(actual,key(route),actual_origin,true).survivable);
    CHECK(!escape.price_move(actual,key(route),{0,false}).complete);
    CHECK(!escape.price_move(actual,key(route,2),actual_origin).complete);
    actual.epoch={2,0,0};CHECK(!escape.price_move(actual,key(route),actual_origin).complete);actual.epoch={1,0,0};
    actual.raw_m=false;CHECK(!escape.price_move(actual,key(route),actual_origin).complete);actual.raw_m=true;
    actual.move_restricted=true;CHECK(!escape.price_move(actual,key(route),actual_origin).complete);actual.move_restricted=false;
    actual.pose={1,0};CHECK(!escape.price_move(actual,key(route),actual_origin).complete);
    actual.pose={4,0};CHECK(!escape.price_move(actual,key(route),actual_origin).complete);actual.pose={0,0};
    actual.shield=100;actual.shield_time=-1;CHECK(!escape.price_move(actual,key(route),actual_origin,false).shield_used);
    CHECK(escape.price_move(actual,key(route),actual_origin,false).hp_loss==30);
    const auto saturated=escape.price_move(actual,key(route),{std::numeric_limits<std::uint64_t>::max(),true},true);
    CHECK(saturated.saturated&&!saturated.complete&&!saturated.survivable);
    CHECK(escape.begin({2,0,0},{1,0},key(route,2)));
    finish(escape,route,[](A::Point){return A::EscapePotential{0,true};});
    actual.epoch={2,0,0};actual.shield=0;
    CHECK(escape.price_move(actual,key(route,2),actual_origin,false).hp_loss==0);
    CHECK(escape.price_move(actual,key(route,2),actual_origin,true).hp_loss==7); // includes refusal at ACTUAL origin even with a quiet landing
}

static void saturation_invalidation_and_reentrant_admission(){
    ++groups;A::Route<3> route;std::array<std::uint8_t,3> board{};board.fill(1);
    CHECK(route.begin(3,1,board,std::array<A::Point,1>{{{2,0}}},5));finish(route);
    A::Escape escape;CHECK(escape.begin({1,0,0},{0,0},key(route)));
    finish(escape,route,[](A::Point p){return A::EscapePotential{p.x==2?0:std::numeric_limits<std::uint64_t>::max(),true};});
    const auto saturated=escape.price(body({1,0,0}),key(route),1,true);
    CHECK(saturated.saturated&&!saturated.complete&&!saturated.survivable);
    CHECK(!escape.price(body({1,0,0}),key(route,2)).complete);
    CHECK(escape.begin({2,0,0},{0,0},key(route,2)));
    unsigned samples=0;
    const auto rebound=escape.advance(route,[&](A::Point){++samples;return A::EscapePotential{0,true};},[&]{CHECK(escape.begin({3,0,0},{0,0},key(route,3)));return true;},8);
    CHECK(rebound.units==0&&samples==0&&!rebound.progressed&&escape.epoch()==A::Epoch{3,0,0});
    CHECK(escape.advance(route,[&](A::Point){++samples;CHECK(escape.begin({4,0,0},{1,0},key(route,4)));return A::EscapePotential{0,true};},[]{return true;},8).units==1);
    CHECK(!escape.complete()&&escape.origin()==A::Point{1,0});
    finish(escape,route,[](A::Point p){return A::EscapePotential{p.x==2?0u:1u,true};});
    route.retire();CHECK(route.begin(3,1,board,std::array<A::Point,1>{{{2,0}}},6));
    // The caller MUST supply current dependencies even for a cached price.
    CHECK(!escape.price(body({4,0,0},{1,0}),key(route,4)).complete);
    CHECK(escape.begin({5,0,0},{1,0},key(route,4)));
    CHECK(escape.advance(route,[](A::Point){return A::EscapePotential{1,true};},[]{return true;},2).source_samples==1);
    CHECK(!escape.complete());
    CHECK(cold_escape.status()==A::EscapeStatus::idle&&!cold_escape.complete());
}

int main(){
    cold_denial_and_partial_candidate_retention();
    stable_minimum_source_step_and_early_quiet();
    per_origin_ready_does_not_require_global_ready();
    quiet_unknown_and_short_tail_are_distinct();
    accepted_prefix_and_actual_epoch_dependencies();
    shield_expiry_and_every_single_refusal_position();
    timer1_expires_after_the_first_future_camp_and_timer2_after_the_second();
    conditional_ingress_prices_actual_body_then_landing_and_exit();
    saturation_invalidation_and_reentrant_admission();
    std::printf("PASS escape: %u checks, %u groups\n",checks,groups);
}
