// Actual production guard composition with a spy native backend. No game or SDK
// authority is simulated by a pure field: the backend records each physical use.
#include "dash_service.h"
#include <production_potential.h>
#include <cstdio>
#include <vector>
using namespace tt::dash_service;
namespace potential=mb::production::potential;
int checks{},failures{};
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL line %d: %s\n",__LINE__,#x); } } while (false)
struct Spy {
    int round{70},width{9},height{9},calls{},position_calls{},dash_calls{},tile_calls{},flags_calls{};
    Point pos{3,3},selected{-1,-1};
    Flags state{true,false,true,true,true};
    std::array<Dash,2> abilities{{{}, {true,true,true,false,2,0,0}}};
    std::array<Tile,81> terrain{};
    potential::Field field{9,9,{}};
    bool headroom{true},accepted{true},no_move_effect{},change_epoch_prepare{},change_epoch_use{},change_position_prepare{};
    int stale_position_call{},cancel_ready_dash_call{},cancel_cast_flags_call{};
    Scope scope{Scope::Clear};
    bool nearby_camp{},visible_enemy{},protect_work_passed{};
    Spy() { while (field.status()==potential::Status::NeedCompute) field.advance(64); }
    int epoch() const { return round; }
    Point position() {
        if (++position_calls==stale_position_call) ++round;
        return pos;
    }
    Flags flags() {
        if (++flags_calls==cancel_cast_flags_call) state.can_cast=false;
        return state;
    }
    bool budget() const { return headroom; }
    bool quiet(Point p) const { const auto v=field.at({p.x,p.y}); return v.status==potential::Status::Ready && v.upper==0; }
    Scope prepare(bool protect_work) {
        protect_work_passed=protect_work;
        if (change_epoch_prepare) ++round;
        if (change_position_prepare) ++pos.x;
        if (scope!=Scope::Clear) return scope;
        return visible_enemy || (nearby_camp && protect_work)?Scope::HasTarget:Scope::Clear;
    }
    Tile tile(Point p) { ++tile_calls; return terrain[static_cast<std::size_t>(p.y)*width+p.x]; }
    Dash dash(int slot) {
        if (++dash_calls==cancel_ready_dash_call) abilities[slot].ready=false;
        return abilities[slot];
    }
    bool use(int slot,Point p) {
        ++calls; selected=p;
        if (accepted) {
            if (!no_move_effect) pos=p;
            state.can_cast=false; abilities[slot].ready=false;
        }
        if (change_epoch_use) ++round;
        return accepted;
    }
    void block(Point p) { terrain[static_cast<std::size_t>(p.y)*width+p.x]={true,true}; }
    void hole(Point p) { terrain[static_cast<std::size_t>(p.y)*width+p.x]={false,true}; }
};
struct Case {
    Spy spy;
    std::vector<std::uint16_t> field=std::vector<std::uint16_t>(81,unreachable);
    Purpose purpose{Purpose::Work};
    int group{7},geometry_epoch{10};
    bool completed{true};
    void value(Point p,std::uint16_t d) { field[static_cast<std::size_t>(p.y)*9+p.x]=d; }
    RouteView route() const { return {completed,purpose,group,geometry_epoch,9,9,field}; }
    Outcome run(bool walked=false) { return try_progress(route(),walked,spy); }
};
int main() {
    // Work/Capture/Bank use the same finite landing, despite a dated field and an
    // ordinary-unreachable actual origin. The hole is only an intermediate tile.
    for (auto purpose:{Purpose::Work,Purpose::Capture,Purpose::Bank}) {
        Case c; c.purpose=purpose; if (purpose==Purpose::Bank) c.group=-1;
        c.value({5,3},0); c.spy.hole({4,3});
        const auto out=c.run();
        CHECK(out.status==Status::Applied); CHECK(out.accepted && out.attempted);
        CHECK((out.from==Point{3,3} && out.to==Point{5,3})); CHECK(out.slot==1);
        CHECK(out.ordinary_distance==unreachable && out.landing_distance==0);
        CHECK(out.geometry_epoch==10 && out.epoch==70); CHECK(c.spy.calls==1);
        CHECK(out.allowances_queried && out.can_move && out.can_attack && !out.can_cast);
        CHECK(c.spy.tile_calls<=max_tiles && c.spy.dash_calls<=spell_slots+1);
    }
    // Work preserves basic-reach members even after a basic; Capture and Bank
    // are not diverted into a different nearby live group. Foes reject all three.
    for (auto purpose:{Purpose::Work,Purpose::Capture,Purpose::Bank}) {
        Case c; c.purpose=purpose; c.value({5,3},0); c.spy.nearby_camp=true;
        c.spy.state.can_attack=false;
        CHECK(c.run().status==(purpose==Purpose::Work?Status::NoProgress:Status::Applied));
        CHECK(c.spy.protect_work_passed==(purpose==Purpose::Work));
        Case enemy; enemy.purpose=purpose; enemy.value({5,3},0); enemy.spy.visible_enemy=true;
        CHECK(enemy.run().status==Status::NoProgress); CHECK(enemy.spy.calls==0);
    }
    // Every integer landing is enumerated, including an off-axis (+1,+2) edge.
    { Case c; c.value({4,5},0); const auto o=c.run(); CHECK(o.status==Status::Applied); CHECK((o.to==Point{4,5})); }
    // Must beat the best currently possible dated ordinary neighbor, not only the
    // origin value. A landing equal to that neighbor is not a dash improvement.
    { Case c; c.value({3,3},6); c.value({4,3},4); c.value({5,3},4); CHECK(c.run().status==Status::NoProgress); CHECK(c.spy.calls==0); }
    { Case c; c.value({3,3},6); c.value({4,3},5); c.value({5,3},4); CHECK(c.run().status==Status::Applied); CHECK(c.spy.calls==1); }
    { Case c; c.value({3,3},0); c.value({5,3},1); CHECK(c.run().status==Status::NoProgress); }
    { Case c; c.value({3,3},6); c.value({4,3},0); c.value({5,3},4); c.spy.state.can_move=false; const auto o=c.run(); CHECK(o.status==Status::Applied); CHECK(o.ordinary_distance==6); CHECK(!o.can_move); }
    // Actual operator-family mismatch: a capture field can root BOTH a tile
    // covered by a different harmful group and the sole quiet capture tile.
    // The unsafe ordinary root0 cannot preempt a quiet dash root0. The source
    // below is outside origin/landing range, but covers the ordinary alternative.
    {
        Case c; c.purpose=Purpose::Capture;
        c.value({3,3},1); c.value({2,4},0); c.value({1,3},0);
        c.spy.field=potential::Field(9,9,{{{5,8},20,4}});
        while(c.spy.field.status()==potential::Status::NeedCompute)c.spy.field.advance(64);
        CHECK(c.spy.quiet({3,3}) && c.spy.quiet({1,3}) && !c.spy.quiet({2,4}));
        const auto o=c.run();
        CHECK(o.status==Status::Applied && o.accepted);
        CHECK((o.to==Point{1,3})); CHECK(o.ordinary_distance==1 && o.landing_distance==0);
    }
    // A fully quiet ordinary capture root0 remains an equally good admitted
    // alternative and the dash still declines, preserving the strict comparison.
    {
        Case c; c.purpose=Purpose::Capture;
        c.value({3,3},1); c.value({2,4},0); c.value({1,3},0);
        const auto o=c.run();
        CHECK(o.status==Status::NoProgress && !o.attempted);
        CHECK(o.ordinary_distance==0 && c.spy.calls==0);
    }
    // Hole landing forbidden, holes in the path legal. Opposed native corridor
    // geometries permit either legal middle: the operator must not choose one
    // fixed interpolation or require every possible middle to be clear.
    { Case c; c.value({5,3},0); c.spy.hole({5,3}); CHECK(c.run().status==Status::NoProgress); CHECK(c.spy.calls==0); }
    {
        Case c; c.value({5,3},0); c.spy.block({4,2}); c.spy.block({4,3}); c.spy.block({4,4}); CHECK(c.run().status==Status::NoProgress); CHECK(c.spy.calls==0);
    }
    { Case c; c.value({5,4},0); c.spy.block({4,3}); CHECK(c.run().status==Status::Applied); }
    { Case c; c.value({5,4},0); c.spy.block({4,4}); CHECK(c.run().status==Status::Applied); }
    { Case c; c.value({5,4},0); c.spy.block({4,3}); c.spy.block({4,4}); CHECK(c.run().status==Status::NoProgress); }
    { Case c; c.value({5,3},0); c.spy.block({4,4}); CHECK(c.run().status==Status::Applied); }
    { Case c; c.value({5,3},0); c.spy.block({5,3}); CHECK(c.run().status==Status::NoProgress); }
    // A hidden native obstruction may refuse: no actual movement or consumption
    // is invented, and the enclosing caller must refresh newly revealed support.
    { Case c; c.value({5,3},0); c.spy.accepted=false; const auto o=c.run();
      CHECK(o.status==Status::Refused && o.attempted && !o.accepted); CHECK((o.from==o.to && o.to==Point{3,3}));
      CHECK(o.can_move && o.can_attack && o.can_cast && c.spy.calls==1); }
    // No can_move requirement: a standing ranged basic can precede a compatible
    // dash. Actual successful ordinary-walk history alone blocks exclusive casts.
    { Case c; c.value({5,3},0); c.spy.state.can_move=false; c.spy.state.can_attack=false; const auto o=c.run(); CHECK(o.status==Status::Applied); CHECK(!o.can_move && !o.can_attack); }
    { Case c; c.value({5,3},0); c.spy.abilities[1].exclusive=true; CHECK(c.run(true).status==Status::NoProgress); CHECK(c.spy.calls==0); }
    { Case c; c.value({5,3},0); CHECK(c.run(true).status==Status::Applied); }
    { Case c; c.value({5,3},0); c.spy.abilities[1].exclusive=true; CHECK(c.run(false).status==Status::Applied); }
    // Query slots rather than assume Vesper, metadata rather than copied range.
    { Case c; c.value({5,3},0); c.spy.abilities[0]=c.spy.abilities[1]; c.spy.abilities[1]={}; CHECK(c.run().slot==0); }
    { Case c; c.value({5,3},0); c.spy.abilities[0]=c.spy.abilities[1]; c.spy.abilities[0].modifier=1; const auto o=c.run(); CHECK(o.status==Status::Applied && o.slot==1); }
    { Case c; c.value({5,3},0); c.spy.abilities[1].range=1; CHECK(c.run().status==Status::NoProgress); }
    { Case c; c.value({5,3},0); c.spy.abilities[1].range=3; CHECK(c.run().status==Status::Unsupported); CHECK(c.spy.calls==0); }
    { Case c; c.value({5,3},0); c.spy.abilities[1].modifier=1; CHECK(c.run().status==Status::Unsupported); }
    { Case c; c.value({5,3},0); c.spy.abilities[1].duration=1; CHECK(c.run().status==Status::Unsupported); }
    { Case c; c.value({5,3},0); c.spy.abilities[1].ready=false; CHECK(c.run().status==Status::NoProgress); }
    { Case c; c.value({5,3},0); c.spy.abilities[1].ground=false; CHECK(c.run().status==Status::NoProgress); }
    { Case c; c.value({5,3},0); c.spy.scope=Scope::HasTarget; CHECK(c.run().status==Status::NoProgress); CHECK(c.spy.calls==0); }
    { Case c; c.value({5,3},0); c.spy.state.alive=false; CHECK(c.run().status==Status::NeedObservation); CHECK(c.spy.calls==0); }
    { Case c; c.value({5,3},0); c.spy.state.channeling=true; CHECK(c.run().status==Status::NeedObservation); }
    { Case c; c.value({5,3},0); c.spy.state.can_cast=false; CHECK(c.run().status==Status::NeedObservation); }
    { Case c; c.value({5,3},0); c.spy.headroom=false; CHECK(c.run().status==Status::NeedCompute); CHECK(c.spy.calls==0); }
    // Actual epoch/pose guards reject resumption during observations; old geometry
    // timestamps remain dated. A successful native effect is reported even if a
    // post-action query notices a later input, so the caller invalidates support.
    { Case c; c.value({5,3},0); c.spy.change_epoch_prepare=true; CHECK(c.run().status==Status::Stale); CHECK(c.spy.calls==0); }
    { Case c; c.value({5,3},0); c.spy.change_position_prepare=true; CHECK(c.run().status==Status::Stale); CHECK(c.spy.calls==0); }
    for (int at=2;at<=6;++at) { Case c; c.value({5,3},0); c.spy.stale_position_call=at; const auto o=c.run(); CHECK(o.status==Status::Stale); CHECK(c.spy.calls==0); }
    { Case c; c.value({5,3},0); c.spy.cancel_ready_dash_call=3; CHECK(c.run().status==Status::NeedObservation); CHECK(c.spy.calls==0); }
    { Case c; c.value({5,3},0); c.spy.cancel_cast_flags_call=2; CHECK(c.run().status==Status::NeedObservation); CHECK(c.spy.calls==0); }
    { Case c; c.value({5,3},0); c.spy.change_epoch_use=true; const auto o=c.run(); CHECK(o.status==Status::Stale && o.accepted && o.attempted); CHECK(!o.allowances_queried); }
    { Case c; c.value({5,3},0); c.spy.no_move_effect=true; const auto o=c.run(); CHECK(o.accepted && o.status==Status::NeedObservation); }
    { Case c; c.completed=false; CHECK(c.run().status==Status::Unsupported); }
    { Case c; c.purpose=Purpose::Unsupported; CHECK(c.run().status==Status::Unsupported); }
    { Case c; c.group=-1; CHECK(c.run().status==Status::Unsupported); }
    { Case c; c.field.pop_back(); CHECK(c.run().status==Status::Unsupported); }
    { Case c; c.value({5,3},0); c.spy.field=potential::Field(9,9,{{{3,3},20,0}}); while (c.spy.field.status()==potential::Status::NeedCompute) c.spy.field.advance(64); CHECK(c.run().status==Status::Unsupported); }
    { Case c; c.value({5,3},0); c.spy.field=potential::Field(9,9,{{{5,3},20,0}}); while (c.spy.field.status()==potential::Status::NeedCompute) c.spy.field.advance(64); CHECK(c.run().status==Status::NoProgress); }
    // Border enumeration never asks an invalid native tile.
    { Case c; c.spy.pos={0,0}; c.value({2,0},0); CHECK(c.run().status==Status::Applied); CHECK(c.spy.tile_calls==9); }
    std::printf("dash production guard: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
