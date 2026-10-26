#include "return_route.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

using namespace mosaic::return_route;
unsigned checks=0;
void check(bool value,int line){++checks;if(!value){std::fprintf(stderr,"return-route failed at%d check%u\n",line,checks);std::abort();}}
#define CHECK(x) check(bool(x),__LINE__)

State ready(double hp=10000){State s;s.health=hp;s.allow_one_refusal=false;return s;}
Route route_for(const std::vector<Cost>& damage,std::uint64_t frame=0){
    std::vector<Node> nodes;for(std::size_t i=0;i<damage.size();++i)nodes.push_back({{int(i),0},{damage[i]},true});
    Route route;CHECK(route.assign(nodes,frame)==Reason::ok);return route;
}
Cost oracle(const std::vector<Cost>& damage,const State& s,std::size_t refused){
    std::vector<Cost> batches;
    for(unsigned i=0;i<s.additional_forced_waits+(s.can_move_now?0u:1u);++i)batches.push_back(damage[0]);
    for(std::size_t i=0;i+1<damage.size();++i){
        if(refused==i)batches.push_back(damage[i]);
        batches.push_back(damage[i+1]);
    }
    Cost shield=Cost(std::floor(s.shield)),loss=0;
    for(std::size_t i=0;i<batches.size();++i){
        const Cost absorbed=i<std::size_t(s.shield_phases)?std::min(shield,batches[i]):0;
        shield-=absorbed;loss+=batches[i]-absorbed;
    }
    return loss;
}
void pricing(){
    // An escape can require a temporary increase in exposure. A one-step
    // nonincreasing-risk policy rejects this whole affordable physical suffix.
    auto route=route_for({60,120,0});auto s=ready(241);s.allow_one_refusal=true;
    auto q=route.quote(s);CHECK(q.valid&&q.safe&&q.worst_loss==240&&q.next==Point(1,0));
    s.health=240;CHECK(!route.quote(s).safe);
    s=ready(100);s.allow_one_refusal=true;
    auto short_route=route_for({60,0});CHECK(short_route.quote(s).worst_loss==60&&short_route.quote(s).safe);
    s.additional_forced_waits=1;CHECK(short_route.quote(s).worst_loss==120&&!short_route.quote(s).safe);
    s=ready();s.shield=100;s.shield_phases=1;s.compute_waits=2;s.allow_one_refusal=true;
    q=route.quote(s);CHECK(q.worst_loss==480&&!q.shield_credited&&q.max_phases==5);
    // All small (including zero-risk interior) routes against an independent
    // materialized camp-phase/shield oracle, with every refusal position.
    constexpr std::array<Cost,3> values{0,7,19};
    for(unsigned length=1;length<=4;++length){
        unsigned cases=1;for(unsigned i=0;i<length;++i)cases*=3;
        for(unsigned code=0;code<cases;++code){
            unsigned digits=code;std::vector<Cost> damage;
            for(unsigned i=0;i<length;++i){damage.push_back(values[digits%3]);digits/=3;}damage.push_back(0);
            const auto r=route_for(damage);
            for(double shield:{0.0,6.9,31.0})for(int phases=0;phases<=5;++phases)
            for(unsigned spent=0;spent<2;++spent)for(unsigned waits=0;waits<3;++waits)for(unsigned refusal=0;refusal<2;++refusal){
                auto state=ready();state.shield=shield;state.shield_phases=phases;state.can_move_now=spent==0;
                state.additional_forced_waits=waits;state.allow_one_refusal=refusal;
                auto loss=oracle(damage,state,damage.size());
                if(refusal)for(std::size_t i=0;i+1<damage.size();++i)loss=std::max(loss,oracle(damage,state,i));
                const auto quote=r.quote(state);CHECK(quote.valid&&quote.worst_loss==loss&&quote.required_hp==loss+1);
                state.health=double(loss);CHECK(!r.quote(state).safe);
                state.health=double(loss+1);CHECK(r.quote(state).safe);
            }
        }
    }
}
void lifecycle(){
    auto r=route_for({60,120,0},10);auto s=ready();s.allow_one_refusal=true;
    CHECK(r.observe_step({0,0},{1,0},false)==Reason::ok);
    CHECK(r.at({0,0})&&r.size()==3&&r.refusal_used());
    CHECK(r.quote(s).reason==Reason::needs_refresh);
    const std::array<Damage,3> prices{{{60},{120},{0}}};const std::array<bool,3> open{true,true,true};
    CHECK(r.refresh_damage(prices,open,10)==Reason::ok);
    CHECK(r.quote(s).worst_loss==180); // paid phase has not happened yet
    CHECK(r.refresh_damage(prices,open,11)==Reason::ok);
    CHECK(r.quote(s).worst_loss==120&&r.refusal_used()); // actual HP now includes it
    const std::array<Node,3> replacement{{{{0,0},{60},true},{{0,1},{120},true},{{0,2},{0},true}}};
    CHECK(r.assign(replacement,11)==Reason::ok&&r.refusal_used());
    CHECK(r.observe_step({0,0},{0,1},false)==Reason::refusal_budget&&!r.valid());
    CHECK(r.assign(replacement,12)==Reason::ok&&r.refusal_used());
    r.clear();CHECK(!r.valid()&&r.refusal_used());
    CHECK(r.reset_at_quiet({0,2},{0},13)&&!r.refusal_used()&&r.at({0,2}));
    CHECK(r.quote(s).valid&&r.quote(s).steps==0&&r.quote(s).worst_loss==0);
    r=route_for({60,120,0},10);
    CHECK(r.observe_step({0,0},{1,0},true)==Reason::ok&&r.at({1,0})&&r.size()==2);
    CHECK(r.quote(s).reason==Reason::needs_refresh);
    const std::array<Damage,2> remaining{{{120},{0}}};const std::array<bool,2> free{true,true};
    CHECK(r.refresh_damage(remaining,free,11)==Reason::ok);
    CHECK(r.quote(s).next==Point(2,0)&&r.quote(s).worst_loss==120);
    CHECK(r.observe_step({1,0},{2,0},true)==Reason::ok&&r.at({2,0}));
    CHECK(r.refresh_damage(std::span(remaining).subspan(1),std::span(free).subspan(1),12)==Reason::ok);
    CHECK(r.quote(s).steps==0&&r.quote(s).safe);
    // A failed outward nomination can be off the return suffix. Accounting
    // that actual false must preserve the return geometry, not invent an edge
    // or reopen a refusal when a replacement route is assigned.
    r=route_for({60,120,0},20);
    CHECK(r.observe_refusal({0,0})==Reason::ok&&r.at({0,0})&&r.size()==3&&r.refusal_used());
    CHECK(r.quote(s).reason==Reason::needs_refresh);
    CHECK(r.refresh_damage(prices,open,20)==Reason::ok&&r.quote(s).worst_loss==180);
    CHECK(r.assign(replacement,20)==Reason::ok&&r.refusal_used());
    CHECK(r.observe_refusal({0,0})==Reason::refusal_budget&&!r.valid());
    r=route_for({60,120,0},20);
    CHECK(r.observe_refusal({8,8})==Reason::origin_mismatch&&!r.valid());
}
void authority(){
    auto r=route_for({60,120,0},10);
    const std::array<Damage,3> prices{{{60},{120},{0}}};const std::array<bool,3> blocked{true,true,false};
    CHECK(r.refresh_damage(prices,blocked,11)==Reason::blocked&&!r.valid());
    // A rejected NEWER observation still fences all older authority. Failure
    // to validate a blocked route must not let stale open cells resurrect it.
    const std::array<Node,2> stale_open{{{{0,0},{60},true},{{1,0},{0},true}}};
    CHECK(r.frame()==11&&r.assign(stale_open,10)==Reason::invalid_state&&!r.valid());
    // Clearing/rerouting cannot manufacture a new refusal episode.
    const std::array<Node,2> jump{{{{0,0},{60},true},{{2,0},{0},true}}};
    CHECK(r.assign(jump,11)==Reason::not_ordinary_step);
    const std::array<Node,2> still{{{{0,0},{60},true},{{0,0},{0},true}}};
    CHECK(r.assign(still,11)==Reason::not_ordinary_step);
    const std::array<Node,2> harmful{{{{0,0},{60},true},{{1,0},{10},true}}};
    CHECK(r.assign(harmful,11)==Reason::terminal_not_quiet);
    const std::array<Node,2> unknown{{{{0,0},{60},true},{{1,0},{0,false},true}}};
    CHECK(r.assign(unknown,11)==Reason::invalid_damage);
    const std::array<Node,2> occupied{{{{0,0},{60},true},{{1,0},{0},false}}};
    CHECK(r.assign(occupied,11)==Reason::blocked);
    CHECK(r.assign(occupied,12)==Reason::blocked&&r.frame()==12);
    CHECK(r.assign(stale_open,11)==Reason::invalid_state&&!r.valid());
    CHECK(!r.reset_at_quiet({9,9},{1},13)&&r.frame()==13&&!r.valid());
    CHECK(r.assign(stale_open,12)==Reason::invalid_state);
    r=Route{};
    const std::vector<Node> too_long(Route::capacity+1);CHECK(r.assign(too_long,11)==Reason::capacity);
    CHECK(r.assign({},11)==Reason::empty);
    r=route_for({60,0},10);CHECK(r.observe_step({9,9},{1,0},true)==Reason::origin_mismatch&&!r.valid());
    r=route_for({60,0},10);CHECK(r.observe_step({0,0},{0,1},true)==Reason::wrong_endpoint&&!r.valid());
    r=route_for({60,0},10);const std::array<Damage,2> p{{{60},{0}}};const std::array<bool,2> f{true,true};
    CHECK(r.refresh_damage(p,f,9)==Reason::invalid_state&&!r.valid());
    r=route_for({60,0},10);auto s=ready();
    for(double invalid:{-1.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
        s.health=invalid;CHECK(r.quote(s).reason==Reason::invalid_state);s=ready();s.shield=invalid;CHECK(r.quote(s).reason==Reason::invalid_state);s=ready();
    }
    s.shield_phases=-1;CHECK(r.quote(s).reason==Reason::invalid_state);
    s=ready();s.compute_waits=33;CHECK(r.quote(s).reason==Reason::invalid_state);
    s=ready();s.additional_forced_waits=33;CHECK(r.quote(s).reason==Reason::invalid_state);
}
void incumbent_selection(){
    // A valid existing suffix can be too expensive while another fully
    // materialized suffix from the SAME actual origin/episode is affordable.
    const std::vector<Cost> long_damage{180,500,500,500,500,0};
    auto old=route_for(long_damage,10);auto state=ready(1200);state.allow_one_refusal=true;
    CHECK(old.quote(state).worst_loss==2500&&!old.quote(state).safe);
    const std::array<Node,5> short_nodes{{
        {{0,0},{180},true},{{0,1},{60},true},{{1,2},{80},true},
        {{2,2},{80},true},{{3,2},{0},true}}};
    auto candidate=old;CHECK(candidate.assign(short_nodes,10)==Reason::ok);
    CHECK(candidate.quote(state).worst_loss==400&&candidate.quote(state).safe);
    CHECK(consider(old,candidate,state,36));
    CHECK(old.size()==5&&old.node(4).point==Point(3,2)&&old.quote(state).worst_loss==400);
    // State is the actual requested work/return class; HOLD1 is not silently
    // dropped because a zero-wait version of the same suffix is affordable.
    auto held=state;held.health=500;held.additional_forced_waits=1;
    auto immediate=held;immediate.additional_forced_waits=0;
    CHECK(old.quote(immediate).safe&&old.quote(immediate).worst_loss==400);
    CHECK(!old.quote(held).safe&&old.quote(held).worst_loss==580);
    auto expensive=route_for(long_damage,10);
    CHECK(consider(expensive,candidate,held,36));
    CHECK(!expensive.quote(held).safe&&expensive.quote(held).required_hp==581);

    // Exhausted search or a non-improving nomination must leave every useful
    // old geometry/budget fact intact. This helper never clears the incumbent.
    const auto keep=old;Route incomplete;
    CHECK(!consider(old,incomplete,state,36)&&old.node(4).point==keep.node(4).point);
    auto worse=route_for(long_damage,10);
    CHECK(!consider(old,worse,state,36)&&old.quote(state).worst_loss==400);
    CHECK(!consider(old,old,state,36));
    auto stale=route_for({180,60,80,80,0},9);
    CHECK(!consider(old,stale,state,36)&&old.frame()==10);
    auto wrong_origin=candidate;auto shifted=short_nodes;
    for(auto& n:shifted){n.point.x+=5;n.point.y+=5;}
    CHECK(wrong_origin.assign(shifted,10)==Reason::ok);
    CHECK(!consider(old,wrong_origin,state,36)&&old.at({0,0}));
    auto bad_state=state;bad_state.health=std::numeric_limits<double>::quiet_NaN();
    CHECK(!consider(old,candidate,bad_state,36));
    expensive=route_for(long_damage,10);
    CHECK(!consider(expensive,candidate,state,4)&&expensive.size()==6);
    CHECK(consider(expensive,candidate,state,5)&&expensive.size()==5); // actual quoted horizon fits

    // A refusal already consumed in this SAME exposed episode survives both
    // candidate construction and replacement. The selected route cannot buy a
    // second fictional blocker allowance merely by changing its geometry.
    old=route_for(long_damage,20);
    CHECK(old.observe_refusal({0,0})==Reason::ok);
    const std::array<Damage,6> prices{{{180},{500},{500},{500},{500},{0}}};
    const std::array<bool,6> open{true,true,true,true,true,true};
    CHECK(old.refresh_damage(prices,open,20)==Reason::ok);
    CHECK(old.refusal_used()&&old.pending_refusal_wait());
    CHECK(old.quote(state).worst_loss==2180);
    candidate=old;CHECK(candidate.assign(short_nodes,20)==Reason::ok);
    CHECK(candidate.quote(state).worst_loss==400);
    CHECK(consider(old,candidate,state,36));
    CHECK(old.refusal_used()&&old.pending_refusal_wait()&&old.quote(state).worst_loss==400);
    const std::array<Damage,5> short_prices{{{180},{60},{80},{80},{0}}};
    const std::array<bool,5> short_open{true,true,true,true,true};
    CHECK(old.refresh_damage(short_prices,short_open,21)==Reason::ok);
    CHECK(old.refusal_used()&&!old.pending_refusal_wait()&&old.quote(state).worst_loss==220);
    auto fresh_episode=route_for({180,60,80,80,0},21);
    CHECK(!consider(old,fresh_episode,state,36)&&old.refusal_used());
    auto unconsumed=route_for(long_damage,21);
    CHECK(!consider(unconsumed,old,state,36)&&!unconsumed.refusal_used());
    // Conversely a fully observed candidate may replace an invalidated old
    // geometry without losing that geometry-independent episode state.
    candidate=old;old.clear();
    CHECK(consider(old,candidate,state,36)&&old.refusal_used()&&old.at({0,0}));
}
int main(){pricing();lifecycle();authority();incumbent_selection();std::printf("%u return-route checks passed\n",checks);}
