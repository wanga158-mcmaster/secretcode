#include "../src/capital_encounter_support.h"
#include <array>
#include <cstdio>
namespace es=wb::capital::encounter_support;
namespace {
unsigned checks{},failures{};
#define CHECK(x) do{++checks;if(!(x)){++failures;if(failures<25)std::printf("FAIL %u: %s\n",__LINE__,#x);}}while(false)
struct Case {
    std::array<std::uint8_t,es::max_bytes> walk{},spawn{},negative{},a{},b{},out{},prior_bits{};
    es::Map map;es::ActorEvidence actor;
    Case(int width=7,int height=5){
        map.domain=17;map.width=width;map.height=height;map.endpoints_complete=true;
        map.walkable={walk};map.spawn={spawn};map.negative_vision={negative};
        map.negative_round=10;map.negative_vision_complete=true;
        for(std::size_t i=0;i<std::size_t(width)*std::size_t(height)&&i<es::max_cells;++i)es::MutableBits{walk}.set(i);
        actor.current={17,3,9,22,10};actor.sampled_round=10;actor.life=es::Life::Alive;actor.life_round=10;
        map.negative_source=actor.current;
        actor.last_seen_known=true;actor.last_seen={1,2};actor.last_seen_round=8;
        actor.last_seen_source={17,1,6,22,8};
        actor.displacement_known=true;actor.movement_from=8;actor.max_displacement=2;
        actor.displacement_from=actor.last_seen_source;actor.displacement_to=actor.current;
    }
    es::Workspace workspace(){return {{a},{b}};}
    es::Support derive(es::Work work={}){return es::derive(map,actor,nullptr,workspace(),{out},work);}
    void negative_at(int x,int y){es::MutableBits{negative}.set(std::size_t(y)*std::size_t(map.width)+std::size_t(x));}
    es::Reach reach()const{es::Reach r;r.source=actor.current;r.horizon=14;r.all_words_covered=r.movement_known=r.attack_known=r.control_known=r.no_persistent_effects=true;return r;}
};
void dilation_and_walls(){
    Case c(7,1);c.walk.fill(0);es::MutableBits{c.walk}.set(0);es::MutableBits{c.walk}.set(4);
    c.actor.last_seen={0,0};c.actor.max_displacement=4;
    auto s=c.derive();CHECK(s.complete()&&s.count==2);CHECK(s.membership({4,0})==es::Membership::Possible);
    CHECK(s.membership({2,0})==es::Membership::Excluded);
    c.actor.max_displacement=es::max_propagations+1;s=c.derive();CHECK(s.state==es::State::Unknown&&s.reason==es::Reason::PropagationLimit);
    c.actor.max_displacement=0;c.actor.last_seen={0,0};s=c.derive();CHECK(s.complete()&&s.count==1);
}
void positive_negative_and_empty(){
    Case c;c.negative.fill(255);c.actor.visible=true;c.actor.observed={4,4};
    auto s=c.derive();CHECK(s.state==es::State::Visible&&s.count==1);CHECK(s.membership({4,4})==es::Membership::Observed);
    c.actor.life=es::Life::Dead;s=c.derive();CHECK(s.state==es::State::Visible);
    c.actor.visible=false;c.actor.life=es::Life::Alive;s=c.derive();CHECK(s.state==es::State::Inconsistent&&!s.complete());
    const std::array<es::Opportunity,1> route{{{12,{4,4}}}};
    CHECK(es::relevance(s,c.reach(),route).state==es::Relevance::Unknown);
    c.negative.fill(0);c.negative_at(1,2);s=c.derive();CHECK(s.complete()&&s.membership({1,2})==es::Membership::Excluded);
    c.map.negative_round=9;s=c.derive();CHECK(s.state==es::State::Unknown&&s.reason==es::Reason::MissingNegativeVisionScope);
    c.map.negative_round=10;c.map.negative_vision_complete=false;s=c.derive();CHECK(!s.complete());
    c.map.negative_vision_complete=true;c.map.negative_source.effect++;s=c.derive();CHECK(s.reason==es::Reason::MissingNegativeVisionScope);
    c.map.negative_source=c.actor.current;c.map.negative_source.actor++;s=c.derive();CHECK(s.reason==es::Reason::MissingNegativeVisionScope);
    c.actor.visible=true;s=c.derive();CHECK(s.state==es::State::Visible); // negative scope cannot erase actual sighting
    c.actor.sampled_round=9;s=c.derive();CHECK(s.state==es::State::Inconsistent);
}
void unknown_and_respawn(){
    Case c(7,1);c.walk.fill(0);es::MutableBits{c.walk}.set(0);es::MutableBits{c.walk}.set(6);es::MutableBits{c.spawn}.set(3);
    c.actor.last_seen={0,0};c.actor.teleport_branch=true;
    auto s=c.derive();CHECK(s.complete()&&s.count==3);CHECK(s.membership({6,0})==es::Membership::Possible);
    c.actor.teleport_branch=false;c.actor.displacement_known=false;s=c.derive();CHECK(s.complete()&&s.count==3);
    c.actor.life=es::Life::Dead;c.actor.life_round=8;c.actor.respawn_time_known=true;c.actor.respawn_round=13;
    s=c.derive();CHECK(s.state==es::State::Absent&&s.absent_through==12);
    const std::array<es::Opportunity,1> route{{{12,{6,0}}}};auto reach=c.reach();reach.horizon=12;
    CHECK(es::relevance(s,reach,route).state==es::Relevance::ProvenIrrelevant);
    reach.no_persistent_effects=false;CHECK(es::relevance(s,reach,route).state==es::Relevance::Unknown);reach.no_persistent_effects=true;
    reach.horizon=13;CHECK(es::relevance(s,reach,route).state==es::Relevance::Unknown);
    c.actor.respawn_round=10;c.actor.displacement_known=true;c.actor.movement_from=10;c.actor.max_displacement=0;
    c.actor.displacement_from={17,3,8,22,10};
    s=c.derive();CHECK(s.complete()&&s.count==1&&s.membership({3,0})==es::Membership::Possible);
    c.actor.current.round=9;c.actor.sampled_round=10;s=c.derive();CHECK(s.state==es::State::Unknown&&s.reason==es::Reason::Date);
}
void prior_and_stamps(){
    Case c;c.actor.max_displacement=0;auto s=c.derive();CHECK(s.complete());
    c.prior_bits=c.out;s.positions={c.prior_bits};
    c.actor.current.round=11;c.actor.current.sample=10;c.actor.sampled_round=11;c.map.negative_round=11;
    c.map.negative_source=c.actor.current;
    c.actor.movement_from=10;c.actor.max_displacement=1;
    c.actor.displacement_from=s.stamp;c.actor.displacement_to=c.actor.current;
    auto next=es::derive(c.map,c.actor,&s,c.workspace(),{c.out});CHECK(next.complete()&&next.count==9);
    auto invalid=s;invalid.stamp.actor=4;next=es::derive(c.map,c.actor,&invalid,c.workspace(),{c.out});CHECK(!next.complete()&&next.reason==es::Reason::Identity);
    invalid=s;invalid.stamp.domain=8;next=es::derive(c.map,c.actor,&invalid,c.workspace(),{c.out});CHECK(!next.complete());
    invalid=s;invalid.stamp.round=12;next=es::derive(c.map,c.actor,&invalid,c.workspace(),{c.out});CHECK(next.reason==es::Reason::Date);
    invalid=s;invalid.state=es::State::Unknown;next=es::derive(c.map,c.actor,&invalid,c.workspace(),{c.out});CHECK(!next.complete());
    c.actor.displacement_from.effect++;next=es::derive(c.map,c.actor,&s,c.workspace(),{c.out});CHECK(next.complete()&&next.count==35);
    c.actor.displacement_from=s.stamp;c.actor.displacement_to.sample++;next=es::derive(c.map,c.actor,&s,c.workspace(),{c.out});CHECK(next.complete()&&next.count==35);
    c.actor.displacement_to=c.actor.current;
    s.positions={c.out};next=es::derive(c.map,c.actor,&s,c.workspace(),{c.out});CHECK(next.reason==es::Reason::Workspace);
    c.actor.current.domain=99;CHECK(c.derive().reason==es::Reason::Domain);
}
struct Budget{unsigned left;static bool permit(void* p){auto& b=*static_cast<Budget*>(p);if(!b.left)return false;--b.left;return true;}};
void poison_and_partial(){
    Case c;c.a.fill(255);c.b.fill(255);c.out.fill(255);c.actor.max_displacement=0;
    auto s=c.derive();CHECK(s.complete()&&s.count==1);CHECK((c.out[4]&0xf8u)==0);
    c.actor.max_displacement=2;
    for(unsigned allowed:{0u,1u,10u,30u,75u}){
        c.out.fill(255);Budget b{allowed};s=c.derive({&b,Budget::permit});
        CHECK(s.state==es::State::Unknown&&s.reason==es::Reason::Partial&&s.positions.bytes.empty());
        CHECK(s.membership({1,2})==es::Membership::Unknown);
    }
    auto bad=c.workspace();bad.second=bad.first;s=es::derive(c.map,c.actor,nullptr,bad,{c.out});CHECK(s.reason==es::Reason::Workspace);
    c.map.walkable.bytes=c.out;s=c.derive();CHECK(s.reason==es::Reason::Workspace);
}
void dated_relevance(){
    Case c(10,1);c.actor.visible=true;c.actor.observed={0,0};auto s=c.derive();auto reach=c.reach();reach.attack=1;reach.control=2;
    const std::array<es::Opportunity,2> route{{{11,{9,0}},{14,{8,0}}}};
    CHECK(es::relevance(s,reach,route).state==es::Relevance::ProvenIrrelevant);
    reach.displacement_through_horizon=6;CHECK(es::relevance(s,reach,route).state==es::Relevance::Relevant);
    reach.control_known=false;CHECK(es::relevance(s,reach,route).reason==es::Reason::MissingReachScope);
    reach.control_known=true;reach.unbounded_branch=true;CHECK(es::relevance(s,reach,route).reason==es::Reason::UnboundedFuture);
    reach.unbounded_branch=false;reach.source.effect++;CHECK(es::relevance(s,reach,route).reason==es::Reason::Identity);
    reach=c.reach();reach.horizon=13;CHECK(es::relevance(s,reach,route).reason==es::Reason::Horizon);
    reach=c.reach();reach.source.sample++;CHECK(es::relevance(s,reach,route).reason==es::Reason::Identity);
    reach=c.reach();Budget b{1};CHECK(es::relevance(s,reach,route,{&b,Budget::permit}).reason==es::Reason::Partial);
    std::array<es::Opportunity,es::max_opportunities+1> large{};CHECK(es::relevance(s,reach,large).state==es::Relevance::Unknown);
    CHECK(es::relevance(s,reach,{}).state==es::Relevance::Unknown);
}
void capacities(){
    Case c(100,100);c.actor.unknown_movement=true;auto s=c.derive();CHECK(s.complete()&&s.count==es::max_cells);
    c.map.width=101;CHECK(c.derive().reason==es::Reason::Shape);
    std::array<es::ActorEvidence,7> actors{};for(unsigned i=0;i<actors.size();++i)actors[i].current.actor=i;
    CHECK(!es::roster_supported(actors));CHECK(es::roster_supported(std::span{actors}.first(6)));
    actors[1].current.actor=actors[0].current.actor;CHECK(!es::roster_supported(std::span{actors}.first(6)));
}
}
int main(){dilation_and_walls();positive_negative_and_empty();unknown_and_respawn();prior_and_stamps();poison_and_partial();dated_relevance();capacities();
    std::printf("capital encounter support: %u checks, %u failures\n",checks,failures);return failures?1:0;}
