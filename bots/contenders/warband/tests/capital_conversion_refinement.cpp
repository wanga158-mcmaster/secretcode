#define WB_CONVERSION_NATIVE_HOST_FREE
#include "../src/capital_conversion_refinement.h"
#include <array>
#include <cstdio>
#include <stdexcept>
namespace r=wb::capital::conversion_refinement;
namespace n=wb::capital::conversion_native;
namespace t=wb::capital::tribute;
namespace {
unsigned checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;if(failures<=20)std::printf("FAIL %u: %s\n",__LINE__,#x);}}while(false)
struct Host {
    int who{2},now{40};unsigned fuel_left{100000},legal_cost{50},legal_calls{},move_calls{},observe_calls{};
    bool legal{true},accepted{true},change_on_legal{},change_on_move{},throw_move{};
    n::Packet raw{};std::array<t::Point,8> blocked{};unsigned blocked_count{};
    int owner(){return who;}int round(){return now;}unsigned fuel(){return fuel_left;}
    bool move_legal(n::Epoch e,const n::Packet& p,t::Point to,const n::Geometry& g){
        ++legal_calls;fuel_left=fuel_left>legal_cost?fuel_left-legal_cost:0;
        if(change_on_legal)++now;
        bool occupied=false;for(unsigned i=0;i<blocked_count;++i)occupied|=to==blocked[i];
        return legal&&!occupied&&e.owner==who&&e.round==now&&g.root_history_known&&!g.exclusive_cast_used&&
            p.own.alive&&!p.own.stunned&&!p.completed_turn&&p.own.can_move&&!(p.restrictions&1u)&&
            p.own.channel==t::Channel::None&&p.move_speed>=1&&r::detail::adjacent(p.own.position,to);
    }
    bool move(t::Point to){
        ++move_calls;if(throw_move)throw std::runtime_error("simulated boundary error");
        if(accepted){raw.own.position=to;raw.own.can_move=false;if(raw.attack_range>1)raw.own.can_attack=false;}
        if(change_on_move)++now;return accepted;
    }
    bool observe(n::Epoch e,const n::Geometry&,n::Packet& p,bool=true){
        ++observe_calls;p=raw;return e.owner==who&&e.round==now;
    }
    bool altar(n::Epoch e,const n::Packet& p,t::Point a,t::Context& c){
        c.altar_known=c.unlocked=true;c.adjacent=n::distance(p.own.position,a)<=p.submit_range;
        return e.owner==who&&e.round==now;
    }
};
n::Epoch epoch(){return {2,40,17};}
n::Packet packet(){
    n::Packet p;p.hp=p.max_hp=100;p.kind=3;p.move_speed=p.attack_range=p.submit_range=1;p.queried_ticks=3;
    auto& o=p.own;o.owner=2;o.round=40;o.input=17;o.sample=5;o.position={5,5};o.bag=14;
    o.channel=t::Channel::None;o.observed=o.fresh=o.alive=o.submit_permitted=true;
    o.can_move=o.can_attack=o.can_cast=true;return p;
}
t::Context context(const n::Packet& p){
    t::Context c;c.owner=p.own.owner;c.round=p.own.round;c.input=p.own.input;c.sample=p.own.sample;
    c.altar={6,6};c.fresh=c.altar_known=c.adjacent=c.unlocked=true;c.queried_ticks=3;c.planned_horizon=43;return c;
}
struct Terrain {std::array<t::Point,8> blocked{};unsigned count{},calls{};};
bool walkable(void* user,t::Point point){
    auto& terrain=*static_cast<Terrain*>(user);++terrain.calls;
    for(unsigned i=0;i<terrain.count;++i)if(terrain.blocked[i]==point)return false;return true;
}
n::Geometry geometry(Terrain& terrain){
    n::Geometry g;g.source=epoch();g.context=&terrain;g.static_walkable=walkable;g.root_history_known=true;return g;
}
r::Options options(std::span<const t::Point> points){r::Options o;o.admitted_destinations=points;return o;}
void prepare_then_execute(){
    auto p=packet();auto c=context(p);Terrain terrain;auto g=geometry(terrain);Host host;host.raw=p;
    const std::array<t::Point,3> ranked{{{5,5},{6,5},{4,5}}};
    auto plan=r::prepare(p,c,g,epoch(),false,host,options(ranked));
    CHECK(plan.kind==r::Kind::Move&&plan.destination==ranked[1]);
    CHECK(host.move_calls==0&&host.observe_calls==0&&host.legal_calls==1);
    CHECK(plan.source.input==17&&plan.sample==5&&plan.position==p.own.position);
    CHECK(plan.remaining_attack&&plan.remaining_cast);
    const auto executed=r::execute(plan,p,c,g,epoch(),host);
    CHECK(executed.status==r::Status::Moved&&executed.attempted&&executed.moved&&!executed.closed&&!executed.fresh);
    CHECK(host.move_calls==1&&host.observe_calls==0&&plan.consumed);
    CHECK(p.own.can_move&&p.own.can_attack&&p.own.can_cast); // helper never fabricates post-MAC
    CHECK(r::execute(plan,p,c,g,epoch(),host).status==r::Status::Consumed&&host.move_calls==1);
}
void no_plan_does_not_dispatch(){
    auto p=packet();auto c=context(p);Terrain terrain;auto g=geometry(terrain);Host host;host.raw=p;
    const std::array<t::Point,2> points{{{6,5},{4,5}}};
    auto none=r::prepare(p,c,g,epoch(),false,host);
    CHECK(none.kind==r::Kind::NoPlan&&none.reason==r::Reason::NoAdmittedLanding);
    CHECK(r::execute(none,p,c,g,epoch(),host).status==r::Status::NoPlan&&host.move_calls==0&&!none.consumed);
    terrain.blocked[0]=points[0];terrain.count=1;host.blocked[0]=points[1];host.blocked_count=1;
    none=r::prepare(p,c,g,epoch(),false,host,options(points));
    CHECK(none.kind==r::Kind::NoPlan&&none.reason==r::Reason::NoLegalLanding&&host.move_calls==0);
    p.own.bag=0;c=context(p);none=r::prepare(p,c,g,epoch(),true,host,options(points));
    CHECK(none.kind==r::Kind::NoPlan&&none.reason==r::Reason::ReleasedCapital);
    p=packet();c=context(p);p.restrictions=1;
    none=r::prepare(p,c,g,epoch(),true,host,options(points));
    CHECK(none.kind==r::Kind::NoPlan&&none.reason==r::Reason::MovementRestricted);
    p=packet();c=context(p);g.exclusive_cast_used=true;
    CHECK(r::prepare(p,c,g,epoch(),false,host,options(points)).kind==r::Kind::NoPlan);
}
void paid_movement_closes_only_its_word(){
    auto p=packet();p.own.can_move=false;auto c=context(p);Terrain terrain;auto g=geometry(terrain);Host host;host.raw=p;
    auto plan=r::prepare(p,c,g,epoch(),true,host);
    CHECK(plan.kind==r::Kind::ActualClosedOrPaidPrefix&&plan.closure_scope==r::ClosureScope::MovementWord);
    CHECK(plan.remaining_attack&&plan.remaining_cast&&p.own.can_attack&&p.own.can_cast);
    auto out=r::execute(plan,p,c,g,epoch(),host);
    CHECK(out.closed&&out.fresh&&!out.attempted&&!out.moved&&out.status==r::Status::Closed);
    CHECK(out.closure_scope==r::ClosureScope::MovementWord&&out.remaining_attack&&out.remaining_cast);
    CHECK(host.move_calls==0&&host.legal_calls==0&&host.observe_calls==0);
    CHECK(r::execute(plan,p,c,g,epoch(),host).status==r::Status::Consumed);
    CHECK(r::prepare(p,c,g,epoch(),false,host).reason==r::Reason::NoMovementUnpaid);
    p.own.can_move=true;c=context(p);
    CHECK(r::prepare(p,c,g,epoch(),true,host).kind==r::Kind::NoPlan); // paid ingress alone is not closure
}
void actual_physical_states_are_not_payouts(){
    Terrain terrain;auto g=geometry(terrain);Host host;
    for(unsigned kind=0;kind<4;++kind){
        auto p=packet();if(kind==0){p.own.alive=false;p.hp=0;}
        if(kind==1)p.own.stunned=true;if(kind==2){p.own.channel=t::Channel::Submit;p.own.remaining_known=true;p.own.remaining_ticks=0;}
        if(kind==3)p.completed_turn=true;auto c=context(p);host.raw=p;
        auto plan=r::prepare(p,c,g,epoch(),false,host);
        CHECK(plan.kind==r::Kind::ActualClosedOrPaidPrefix&&plan.closure_scope==r::ClosureScope::ActualPhysicalState);
        auto out=r::execute(plan,p,c,g,epoch(),host);
        CHECK(out.closed&&out.fresh&&!out.attempted&&p.own.bag==14&&p.own.team_score==0);
    }
    CHECK(host.move_calls==0&&host.observe_calls==0);
    auto p=packet();p.own.channel=t::Channel::Unknown;auto c=context(p);
    CHECK(r::prepare(p,c,g,epoch(),false,host).kind==r::Kind::NoPlan);
}
void stale_and_changed_dependencies(){
    const std::array<t::Point,1> points{{{6,5}}};
    for(unsigned change=0;change<9;++change){
        auto p=packet();auto c=context(p);Terrain terrain;auto g=geometry(terrain);Host host;host.raw=p;
        auto plan=r::prepare(p,c,g,epoch(),false,host,options(points));CHECK(plan.kind==r::Kind::Move);
        auto e=epoch();if(change==0){++p.own.sample;c.sample=p.own.sample;}
        if(change==1){++e.input;p.own.input=c.input=e.input;g.source=e;}
        if(change==2)++p.own.position.x;
        if(change==3)p.own.can_move=false;if(change==4)p.own.can_attack=false;if(change==5)p.own.can_cast=false;
        if(change==6)p.restrictions=1;if(change==7)p.hp=90;if(change==8)++host.now;
        const auto out=r::execute(plan,p,c,g,e,host);
        CHECK((out.status==r::Status::Changed||out.status==r::Status::Stale)&&!out.attempted&&host.move_calls==0);
    }
    auto p=packet();auto c=context(p);Terrain terrain;auto g=geometry(terrain);Host host;host.raw=p;
    auto plan=r::prepare(p,c,g,epoch(),false,host,options(points));host.legal=false;
    CHECK(r::execute(plan,p,c,g,epoch(),host).status==r::Status::PermissionChanged&&host.move_calls==0&&plan.consumed);
    CHECK(r::execute(plan,p,c,g,epoch(),host).status==r::Status::Consumed);
}
void finite_refusal_epoch_and_budget(){
    const std::array<t::Point,1> points{{{6,5}}};auto p=packet();auto c=context(p);Terrain terrain;auto g=geometry(terrain);
    Host host;host.raw=p;auto plan=r::prepare(p,c,g,epoch(),false,host,options(points));host.accepted=false;
    auto out=r::execute(plan,p,c,g,epoch(),host);
    CHECK(out.status==r::Status::Refused&&out.attempted&&!out.moved&&!out.closed&&!out.fresh);
    CHECK(host.raw.own.position==p.own.position&&host.raw.own.can_move&&host.raw.own.can_attack&&host.raw.own.can_cast);
    CHECK(r::execute(plan,p,c,g,epoch(),host).status==r::Status::Consumed&&host.move_calls==1);
    host=Host{};host.raw=p;host.change_on_legal=true;
    CHECK(r::prepare(p,c,g,epoch(),false,host,options(points)).reason==r::Reason::Stale&&host.move_calls==0);
    host=Host{};host.raw=p;plan=r::prepare(p,c,g,epoch(),false,host,options(points));host.change_on_legal=true;
    out=r::execute(plan,p,c,g,epoch(),host);CHECK(out.status==r::Status::Stale&&!out.attempted&&!out.fresh);
    host=Host{};host.raw=p;plan=r::prepare(p,c,g,epoch(),false,host,options(points));host.change_on_move=true;
    out=r::execute(plan,p,c,g,epoch(),host);CHECK(out.status==r::Status::Stale&&out.attempted&&out.moved&&!out.fresh);
    host=Host{};host.raw=p;host.fuel_left=13999;
    CHECK(r::prepare(p,c,g,epoch(),false,host,options(points)).reason==r::Reason::NeedFuel&&host.move_calls==0);
    host=Host{};host.raw=p;plan=r::prepare(p,c,g,epoch(),false,host,options(points));host.fuel_left=13999;
    CHECK(r::execute(plan,p,c,g,epoch(),host).status==r::Status::NeedFuel&&host.move_calls==0&&plan.consumed);
    host=Host{};host.raw=p;plan=r::prepare(p,c,g,epoch(),false,host,options(points));host.throw_move=true;bool caught=false;
    try{r::execute(plan,p,c,g,epoch(),host);}catch(const std::runtime_error&){caught=true;}
    CHECK(caught&&plan.consumed&&host.move_calls==1);
    std::array<t::Point,9> many{};
    CHECK(r::prepare(p,c,g,epoch(),false,host,options(many)).reason==r::Reason::TooManyLandings);
}
struct Callback {
    r::Plan* plan{};Host* host{};n::Geometry* geometry{};r::Execution result{};unsigned calls{};
};
bool fallback(void* user,const n::Packet& packet,const t::Context& context){
    auto& cb=*static_cast<Callback*>(user);++cb.calls;
    cb.result=r::execute(*cb.plan,packet,context,*cb.geometry,cb.geometry->source,*cb.host);
    return cb.result.moved||cb.result.closed;
}
void native_callback_boundary(){
    Terrain terrain;auto g=geometry(terrain);Host host;host.raw=packet();host.raw.attack_range=3;
    n::Adapter<Host> adapter(host);t::Monitor monitor;
    auto actual=adapter.observe_input(epoch(),g,{},&monitor);
    CHECK(actual.fresh&&!actual.claimed&&actual.actual.own.sample!=0);
    const std::array<t::Point,1> points{{{6,5}}};
    auto plan=r::prepare(actual.actual,actual.context,g,epoch(),false,host,options(points));
    CHECK(r::playable(plan)&&!adapter.claimed()&&host.move_calls==0);
    Callback cb{&plan,&host,&g};
    auto after=adapter.fund_fallback(monitor,actual,g,{},n::Policy{&cb,nullptr,fallback});
    CHECK(cb.calls==1&&cb.result.moved&&after.claimed&&after.fallback_funded&&after.fresh);
    CHECK(after.actual.own.position==points[0]&&!after.actual.own.can_move&&!after.actual.own.can_attack&&after.actual.own.can_cast);
    CHECK(after.actual.own.sample>actual.actual.own.sample&&after.receipt.credited_original==0);
    CHECK(host.move_calls==1&&host.observe_calls==2);
    // NoPlan is filtered BEFORE fund_fallback; no callback/no claimed idle.
    Host empty_host;empty_host.raw=packet();empty_host.raw.own.bag=0;
    n::Adapter<Host> empty_adapter(empty_host);t::Monitor empty_monitor;
    auto empty=empty_adapter.observe_input(epoch(),g,{},&empty_monitor);
    auto no=r::prepare(empty.actual,empty.context,g,epoch(),false,empty_host,options(points));
    CHECK(!r::playable(no)&&no.reason==r::Reason::ReleasedCapital&&!empty_adapter.claimed());
    CHECK(empty_host.move_calls==0&&empty.receipt.credited_original==0);
    // Actual M-spent prefix closes the movement WORD with A/C retained.
    Host paid_host;paid_host.raw=packet();paid_host.raw.own.can_move=false;
    n::Adapter<Host> paid_adapter(paid_host);t::Monitor paid_monitor;
    auto paid=paid_adapter.observe_input(epoch(),g,{},&paid_monitor);
    auto closed=r::prepare(paid.actual,paid.context,g,epoch(),true,paid_host);
    Callback closed_cb{&closed,&paid_host,&g};
    auto paid_after=paid_adapter.fund_fallback(paid_monitor,paid,g,{},n::Policy{&closed_cb,nullptr,fallback});
    CHECK(closed_cb.result.closure_scope==r::ClosureScope::MovementWord&&paid_after.fresh&&paid_after.fallback_funded);
    CHECK(paid_after.actual.own.can_attack&&paid_after.actual.own.can_cast&&!paid_after.actual.own.can_move&&paid_host.move_calls==0);
    CHECK(paid_after.receipt.credited_original==0&&!paid_after.started&&paid_after.actual.own.bag==14);
}
}
int main(){
    prepare_then_execute();no_plan_does_not_dispatch();paid_movement_closes_only_its_word();
    actual_physical_states_are_not_payouts();stale_and_changed_dependencies();finite_refusal_epoch_and_budget();native_callback_boundary();
    std::printf("capital conversion refinement: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
