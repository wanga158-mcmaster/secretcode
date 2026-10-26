#include "../src/capital_service_routes.h"
#include <array>
#include <cstdio>
namespace r=wb::capital::service_routes;
namespace f=wb::capital::service_forecast;
namespace {
unsigned checks{},failures{};
#define CHECK(...) do{++checks;if(!(__VA_ARGS__)){++failures;if(failures<25)std::printf("FAIL %u: %s\n",__LINE__,#__VA_ARGS__);}}while(false)
struct Case {
    static constexpr unsigned width=21,height=15,cells=width*height;
    std::array<std::uint8_t,cells> blocked{},spawn{},foreign{},occupied{};
    std::array<r::Altar,1> altars{{{{18,11},r::Lock::Unlocked}}};
    std::array<r::Participant,3> people{};std::array<std::array<f::Point,1>,3> hints{};
    f::World world{};r::Map map{};r::Request request{};f::Program program;r::Workspace workspace;r::Builder builder;
    Case(unsigned actors=1){
        hints[0][0]={9,2};hints[1][0]={11,2};hints[2][0]={9,3};
        blocked[11*width+18]=1;spawn[12*width]=spawn[12*width+1]=spawn[12*width+2]=1;
        map={int(width),int(height),1,2,3,blocked,spawn,foreign,true,true,altars,40,9,occupied,40,8};
        world.round=40;world.horizon=100;world.team=0;world.phase=f::Phase::ActorAction;world.slot=0;world.actors=actors;world.npcs=4;world.revision=7;
        world.group.id=4;world.group.life=f::Life::Alive;world.group.roster_complete=true;
        for(unsigned a=0;a<actors;++a){auto& b=world.bodies[a];b.id=10+int(a);b.actual=b.alive=true;b.pose={12,int(5+a)};b.kit=a==0?f::Kit::Varg:a==1?f::Kit::Willow:f::Kit::Vendetta;
            b.attack_range=a==1?3:1;b.heal_range=a==1?4:0;b.move_range=1;b.hp=b.max_hp=1000;
            people[a]={b.id,0,0,0,int(a),true,true,true};}
        for(unsigned n=0;n<4;++n){auto& c=world.camps[n];c.id=100+int(n);c.pose={int(7+n/2),int(2+n%2)};c.actual=true;c.hp=c.max_hp=n<2?120.f:1350.f;c.strength=5;c.range=3;}
        world.slots=actors;
        request.world=&world;request.source={1,2,3,4,7,11,6,10,40};request.family=actors==1?r::Family::Solo:r::Family::Joint;
        request.order={std::span<const r::Participant>{people.data(),actors},0,10,0,true,true};
        request.varg_splash_known=true;request.varg_splash_radius=1;
        for(unsigned a=0;a<actors;++a)request.intent[a].work_hints=hints[a];
        if(actors>1)request.intent[1].heal=true;
    }
    r::State begin(std::span<const r::ReverseField* const> cache={}){return builder.begin(map,request,program,workspace,cache);}
    r::State finish(unsigned quantum=13){for(unsigned n=0;n<1000000;++n){const auto s=builder.advance(request.source,quantum);if(s!=r::State::Building&&s!=r::State::Deferred)return s;}return r::State::Unknown;}
};
void validate(const Case& c){
    const auto& product=c.builder.result();CHECK(product.state==r::State::Ready&&product.program==&c.program);
    CHECK(c.program.leg_count==6*c.world.actors&&c.program.route_count<=f::route_limit);
    for(unsigned a=0;a<c.world.actors;++a){CHECK(c.program.bodies[a].post_renewal==f::PostRenewal::Unknown);f::Point previous=c.world.bodies[a].pose;
        for(unsigned k=0;k<6;++k){const auto id=c.program.bodies[a].legs[k];CHECK(id>=0&&unsigned(id)<c.program.leg_count);const auto& leg=c.program.legs[unsigned(id)];
            CHECK(leg.complete&&leg.origin==previous);auto point=leg.origin;
            for(unsigned n=0;n<leg.count;++n){const auto& step=c.program.route[leg.begin+n];CHECK(f::distance(point,step.destination)==1);
                CHECK(!c.blocked[unsigned(step.destination.y)*Case::width+unsigned(step.destination.x)]&&!c.foreign[unsigned(step.destination.y)*Case::width+unsigned(step.destination.x)]);
                CHECK(step.earliest_round==40&&step.latest_round==100);
                if(k!=1&&k!=2)for(unsigned npc=0;npc<c.world.npcs;++npc)CHECK(!(step.destination==c.world.camps[npc].pose));point=step.destination;}
            CHECK(point==product.legs[a][k].station);CHECK(product.legs[a][k].requires_selected_full_clear==(k==1||k==2));
            if(k==1)CHECK(r::detail::candidate_cell(c.world,point,false));
            if(k==2)CHECK(!r::detail::positive(c.world,point));
            if(k==3)CHECK(f::distance(point,c.program.bodies[a].altar)==1&&!(point==c.program.bodies[a].altar));
            if(k==4)CHECK(c.spawn[unsigned(point.y)*Case::width+unsigned(point.x)]!=0);
            previous=point;
        }
        CHECK(previous==product.legs[a][0].station);
    }
}
void solo_and_party(){
    Case solo;CHECK(solo.begin()==r::State::Building);CHECK(!solo.workspace.cold_ready(Case::cells));CHECK(solo.finish()==r::State::Ready);validate(solo);
    CHECK(solo.builder.result().legs[0][0].station==f::Point{9,2});
    CHECK(f::distance({9,2},solo.world.camps[0].pose)==2); // direct-all gate would incorrectly reject Packs
    Case party(3);CHECK(party.begin()==r::State::Building);CHECK(party.finish()==r::State::Ready);validate(party);
    const auto& p=party.builder.result();CHECK(p.order.varg_before_vendetta);
    CHECK(p.legs[0][0].station==f::Point{9,2}&&p.legs[2][0].station==f::Point{9,3}&&p.legs[1][0].station==f::Point{11,2});
    CHECK(f::distance(p.legs[1][0].station,p.legs[0][0].station)<=int(party.world.bodies[1].heal_range));
    for(unsigned a=0;a<3;++a)for(unsigned b=0;b<a;++b)for(unsigned k:{0u,1u,4u})CHECK(!(p.legs[a][k].station==p.legs[b][k].station));
}
struct Budget {unsigned left;static bool permit(void* p,unsigned){auto& b=*static_cast<Budget*>(p);if(!b.left)return false;--b.left;return true;}};
void partial_and_source(){
    Case c;CHECK(c.begin()==r::State::Building);Budget no{0};CHECK(c.builder.advance(c.request.source,100,{&no,Budget::permit})==r::State::Deferred);
    CHECK(c.builder.result().program==nullptr&&c.program.leg_count==0&&c.builder.result().last_work==0);
    CHECK(c.builder.advance(c.request.source,1)==r::State::Building);CHECK(c.workspace.cold_work_remaining(Case::cells)==Case::cells-1);
    CHECK(c.finish()==r::State::Ready);
    auto changed=c.request.source;++changed.effect;CHECK(c.builder.advance(changed,1)==r::State::Unknown&&c.builder.result().reason==r::Reason::Stale&&c.builder.result().program==nullptr);
    Case dead;CHECK(dead.begin()==r::State::Building);dead.world.bodies[0].alive=false;CHECK(dead.builder.advance(dead.request.source,1)==r::State::Unknown);
    Case oversized;oversized.world.actors=99;CHECK(oversized.begin()==r::State::Unknown);
    Case shared;r::Search first,second;r::Query query;query.origin=shared.world.bodies[0].pose;query.goal=r::Goal::Work;
    first.begin(shared.map,shared.world,query,shared.workspace);
    for(unsigned n=0;n<Case::cells+3;++n)first.unit();
    second.begin(shared.map,shared.world,query,shared.workspace);second.unit();CHECK(first.unknown());
}
void order_contract(){
    Case c(3);r::Order out;CHECK(r::order(c.world,c.request.order,out)==r::Reason::None);
    CHECK(out.even[0].controlled&&out.even[0].actor==0&&out.odd[0].actor==0);
    c.people[2].next_priority=5;CHECK(c.begin()==r::State::Unknown&&c.builder.result().reason==r::Reason::OrderChanges);
    c.people[2].current_priority=5;CHECK(c.begin()==r::State::Unknown&&c.builder.result().reason==r::Reason::CurrentSlot);
    c.request.order.actual_slot=1;c.world.slot=1;CHECK(c.begin()==r::State::Unknown&&c.builder.result().reason==r::Reason::VargTieOrder);
    Case unknown;unknown.people[0].next_committed=false;CHECK(unknown.begin()==r::State::Unknown&&unknown.builder.result().reason==r::Reason::Order);
    Case opponent;opponent.world.externals=1;opponent.world.others[0].id=99;opponent.world.others[0].team=1;opponent.world.slots=2;
    opponent.people[1]={99,1,0,0,0,true,true,true};opponent.request.order.participants={opponent.people.data(),2};
    opponent.world.slot=opponent.request.order.actual_slot=1;
    CHECK(opponent.begin()==r::State::Building);const auto& order=opponent.builder.result().order;
    CHECK(!order.even[0].controlled&&order.even[0].external_id==99&&order.odd[0].controlled);
}
void dependencies(){
    Case c;c.occupied.fill(1);CHECK(c.begin()==r::State::Building&&c.finish()==r::State::Ready);
    CHECK(c.builder.result().legs[0][0].first_edge==r::Occupancy::Occupied); // dependency, not permanent refutation
    Case unknown;unknown.altars[0].lock=r::Lock::Unknown;CHECK(unknown.begin()==r::State::Building&&unknown.finish()==r::State::Ready);
    CHECK(unknown.builder.result().locks[0]==r::Lock::Unknown&&unknown.program.bodies[0].altar_known);
    Case locked;locked.altars[0].lock=r::Lock::Locked;CHECK(locked.begin()==r::State::Building&&locked.finish()==r::State::Unknown);
    Case stale;stale.altars[0].lock=r::Lock::Locked;stale.map.lock_round=39;CHECK(stale.begin()==r::State::Building&&stale.finish()==r::State::Ready);
    CHECK(stale.builder.result().locks[0]==r::Lock::Unknown);
    Case footprint;footprint.foreign[5*Case::width+12]=1;CHECK(footprint.begin()==r::State::Unknown&&footprint.builder.result().reason==r::Reason::ForeignFootprint);
    Case missing;missing.map.foreign_complete=false;CHECK(missing.begin()==r::State::Unknown);
    Case orphan;orphan.world.camps[0].pose={19,0};CHECK(orphan.begin()==r::State::Building&&orphan.finish()==r::State::Unknown&&orphan.builder.result().reason==r::Reason::NeedApproach);
}
void reverse_cache(){
    Case c;r::Workspace field_space;r::ReverseField field;r::Query query;query.actor=0;query.goal=r::Goal::Work;query.origin=c.world.bodies[0].pose;query.hints=c.hints[0];
    field.begin(c.map,c.world,query,c.request.source.candidate,field_space);const auto key=r::field_key(c.map,c.world,query,c.request.source.candidate);
    for(unsigned n=0;n<100000;++n){const auto state=field.advance(key,11);if(state==r::State::Ready||state==r::State::Unknown)break;}
    CHECK(field.ready()&&field.reachable(c.world.bodies[0].pose)&&field.distance({9,2})==0);
    const std::array<const r::ReverseField*,1> cached{{&field}};CHECK(c.begin(cached)==r::State::Building&&c.finish()==r::State::Ready);validate(c);
    CHECK(c.builder.result().legs[0][0].field==key);
    Case paused;CHECK(paused.begin(cached)==r::State::Building);CHECK(paused.builder.advance(paused.request.source,1)==r::State::Building);
    auto different=query;different.goal=r::Goal::Point;different.point={16,8};different.hints={};
    field.begin(c.map,c.world,different,4,field_space);const auto different_key=r::field_key(c.map,c.world,different,4);
    for(unsigned n=0;n<100000&&!field.ready();++n)field.advance(different_key,11);
    CHECK(field.ready());CHECK(paused.builder.advance(paused.request.source,1)==r::State::Unknown&&paused.builder.result().reason==r::Reason::Cache);
    auto changed=key;++changed.foreign;CHECK(field.advance(changed,1)==r::State::Unknown&&!field.ready());
    r::ReverseField invalid;r::Query bad=query;bad.actor=3;invalid.begin(c.map,c.world,bad,4,field_space);CHECK(invalid.advance(key,1)==r::State::Unknown);
}
void fresh_rebind(){
    Case original;CHECK(original.begin()==r::State::Building&&original.finish()==r::State::Ready);
    Case fresh;fresh.world.round=44;fresh.world.horizon=120;fresh.world.revision=19;fresh.world.bodies[0].hp=850;fresh.world.bodies[0].cooldown={5,7};fresh.world.bodies[0].gold=400;
    fresh.request.source.round=44;fresh.request.source.world=19;fresh.request.source.input=29;fresh.request.source.effect=18;
    fresh.map.occupancy_round=fresh.map.lock_round=44;
    r::Rebinder rebind;CHECK(rebind.begin(original.builder.result(),fresh.map,fresh.request,fresh.program)==r::State::Building);
    Budget none{0};CHECK(rebind.advance(fresh.request.source,100,{&none,Budget::permit})==r::State::Deferred&&rebind.result().program==nullptr);
    for(unsigned n=0;n<100000;++n){const auto state=rebind.advance(fresh.request.source,7);if(state!=r::State::Building&&state!=r::State::Deferred)break;}
    CHECK(rebind.result().state==r::State::Ready&&rebind.result().source.round==44);
    CHECK(fresh.program.route_count==original.program.route_count&&fresh.program.leg_count==original.program.leg_count);
    for(unsigned n=0;n<fresh.program.route_count;++n){CHECK(fresh.program.route[n].destination==original.program.route[n].destination);
        CHECK(fresh.program.route[n].earliest_round==44&&fresh.program.route[n].latest_round==120);
        CHECK(original.program.route[n].earliest_round==40&&original.program.route[n].latest_round==100);}
    CHECK(fresh.world.bodies[0].hp==850&&fresh.world.bodies[0].cooldown[0]==5&&fresh.world.bodies[0].gold==400);
    CHECK(rebind.begin(original.builder.result(),original.map,original.request,original.program)==r::State::Unknown);
    fresh.world.bodies[0].pose={13,5};CHECK(rebind.begin(original.builder.result(),fresh.map,fresh.request,fresh.program)==r::State::Unknown);
    fresh.world.bodies[0].pose={12,5};fresh.world.bodies[0].move_range=0;CHECK(rebind.begin(original.builder.result(),fresh.map,fresh.request,fresh.program)==r::State::Unknown);
    fresh.world.bodies[0].move_range=1;fresh.request.family=static_cast<r::Family>(99);CHECK(rebind.begin(original.builder.result(),fresh.map,fresh.request,fresh.program)==r::State::Unknown);
    fresh.request.family=r::Family::Solo;
    fresh.world.bodies[0].pose={12,5};fresh.world.camps[0].hp=0;CHECK(rebind.begin(original.builder.result(),fresh.map,fresh.request,fresh.program)==r::State::Unknown);
    fresh.world.camps[0].hp=120;CHECK(rebind.begin(original.builder.result(),fresh.map,fresh.request,fresh.program)==r::State::Building);
    auto changed=original.request.source;++changed.effect;original.builder.advance(changed,1);
    CHECK(rebind.advance(fresh.request.source,1)==r::State::Unknown);
}
}
int main(){solo_and_party();partial_and_source();order_contract();dependencies();reverse_cache();fresh_rebind();std::printf("capital service routes: %u checks, %u failures\n",checks,failures);return failures?1:0;}
