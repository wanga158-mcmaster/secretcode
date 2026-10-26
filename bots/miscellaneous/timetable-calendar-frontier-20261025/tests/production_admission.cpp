#include "production_admission.h"
#include <cstdio>
namespace {
using namespace tt::production_admission;
int failures{};
void check(bool value,const char* text)
{ if (!value) { std::printf("FAIL %s\n",text); ++failures; } }
Nomination offer(int group,Family family,float rate,std::uint64_t fragment=1,
                 Point perch={3,3},Primitive primitive=Primitive::Basic)
{
    return {{group,family,fragment,{0,0},perch,primitive},{10,20,30,40,50},All,rate,rate/2,12,100,1};
}
template<class M> Selection finish(M& menu)
{
    Selection result;
    for (unsigned i=0;i<1000;++i)
    { result=menu.advance(1); if (result.status!=Status::NeedCompute) return result; }
    check(false,"bounded fixture cursor completes"); return result;
}
void absent_winner()
{
    Menu<3,6,8> menu;
    menu.observe(0,{Life::Alive,100,-1}); menu.observe(1,{Life::Alive,100,-1});
    auto a=offer(0,Family::Work,10),b=offer(1,Family::Work,5);
    menu.put(0,a); menu.put(1,b); menu.seal();
    check(finish(menu).slot==0,"dated high-rate A is first nomination");
    check(menu.admit(0,{Life::Respawning,160,0},a.premises).status==Status::Waiting,
        "fresh Respawning0 rejects A's DamageWork rather than manufacturing living work");
    auto selected=finish(menu);
    check(selected.status==Status::Nominee && selected.slot==1 &&
        menu.admit(1,{Life::Alive,160,-1},b.premises).status==Status::Nominee,
        "rejected A preserves already prepared B and B receives its own fresh admission");
    const auto generation=menu.life(0)->generation;
    a.dated_epoch=999; a.query=999; ++a.premises.body; ++a.premises.geometry;
    menu.put(0,a); menu.observe(0,{Life::Respawning,999,0});
    check(finish(menu).slot==1 && menu.life(0)->generation==generation,
        "query epoch timer0 body and geometry cannot clear actual source absence");
    check(menu.observe(0,{Life::Alive,159,-1})==Status::Stale && finish(menu).slot==1,
        "older Alive packet cannot overwrite newer absence");
    menu.observe(0,{Life::Unknown,1000,0});
    check(menu.life(0)->state==Life::Respawning && finish(menu).slot==1,
        "unknown readback preserves the actual known lifecycle fact");
    menu.observe(0,{Life::Alive,1001,-1});
    check(finish(menu).status==Status::Reprice && finish(menu).slot==0,
        "actual return to Alive reopens A as a dated nomination needing price renewal");
    check(menu.refute(0,a.premises,All,Refutation::RejectedFragment)==Store::Stale,
        "an old-life slot cannot transplant its rejection into the new actual lifecycle");
    menu.put(0,a);
    check(finish(menu).slot==0 && menu.admit(0,{Life::Alive,1001,-1},a.premises).status==Status::Nominee,
        "new-life A can be repriced then fresh-admitted without a permanent group ban");
}
void reward_and_resolvers()
{
    Menu<1,4,4> menu;
    menu.observe(0,{Life::Respawning,10,5});
    auto work=offer(0,Family::Work,10),capture=offer(0,Family::Capture,9);
    auto observe=offer(0,Family::Observe,1,2,{-1,-1},Primitive::Observe);
    auto release=offer(0,Family::Release,0,3,{3,3},Primitive::Release);
    menu.put(0,work); menu.put(1,capture); menu.put(2,observe); menu.put(3,release); menu.seal();
    check(finish(menu).status==Status::Resolver && finish(menu).slot==2,
        "absent Work and unavailable Capture leave a named Observe family");
    check(menu.admit(2,{Life::Unknown,11,0},observe.premises).status==Status::Resolver &&
        menu.life(0)->state==Life::Respawning,
        "Observe may resolve unknown lifecycle and does not fabricate Alive");
    menu.refute(2,observe.premises,Visibility,Refutation::RejectedFragment);
    check(finish(menu).slot==3 && menu.admit(3,{Life::Respawning,12,0},release.premises).status==Status::Resolver &&
        menu.life(0)->state==Life::Respawning,
        "Release is distinct from Work; its nomination does not prove movement or post-phase respawn");
    menu.observe(0,{Life::Reward,13,-1});
    check(finish(menu).status==Status::Reprice && finish(menu).slot==1,
        "actual Reward opens separately priced Capture rather than DamageWork");
    menu.put(1,capture);
    check(menu.admit(1,{Life::Reward,13,-1},capture.premises).status==Status::Nominee &&
        menu.admit(0,{Life::Reward,13,-1},work.premises).status==Status::Waiting,
        "Reward capture admission never stands in for an Alive work observation");
}
void fragment_refinement()
{
    Menu<2,7,12> menu;
    menu.observe(0,{Life::Alive,100,-1}); menu.observe(1,{Life::Alive,100,-1});
    auto a=offer(0,Family::Work,10),fragment=offer(0,Family::Work,9,2);
    auto perch=offer(0,Family::Work,8,1,{4,3});
    auto primitive=offer(0,Family::Work,7,1,{3,3},Primitive::Cast);
    auto b=offer(1,Family::Work,2);
    menu.put(0,a); menu.put(1,fragment); menu.put(2,perch); menu.put(3,primitive); menu.put(4,b); menu.seal();
    const unsigned relevant=Geometry|Members|Model;
    menu.refute(0,a.premises,relevant,Refutation::RejectedFragment,Scope::StandingAtPerch);
    check(finish(menu).slot==1,"one standing primitive rejection preserves an alternate fragment");
    menu.refute(1,fragment.premises,All,Refutation::RejectedFragment);
    check(finish(menu).slot==2,"one fragment rejection preserves another perch");
    menu.refute(2,perch.premises,All,Refutation::RejectedFragment);
    check(finish(menu).slot==3,"one perch rejection preserves another primitive class");
    menu.refute(3,primitive.premises,All,Refutation::RejectedFragment);
    check(finish(menu).slot==4,"refuted A fragments preserve B instead of banning or restarting all work");
    // Conditional G13 counterexample: fresh Alive Work(A), coverage rejects the
    // standing primitive at P, actual Exit to quiet Q, Acquire from Q. The menu
    // must remember P's unchanged rejection even though the departure changed.
    a.subject.origin={1,1}; ++a.premises.body; a.query=200; a.dated_epoch=200;
    menu.put(0,a); menu.observe(0,{Life::Alive,200,-1});
    check(finish(menu).slot==4 && menu.life(0)->state==Life::Alive,
        "FreshAlive -> failed coverage(P) -> Exit(Q) -> Acquire retains P refutation without source absence");
    ++a.premises.visibility; menu.put(0,a);
    check(finish(menu).slot==4,"changing an irrelevant fact does not reopen a standing-family refutation");
    ++a.premises.geometry; menu.put(0,a);
    check(finish(menu).slot==0,"changed relevant geometry reopens that exact standing family");
    menu.refute(0,a.premises,Geometry,Refutation::RejectedFragment,Scope::ExactFragment);
    a.subject.origin={2,2}; menu.put(0,a);
    check(finish(menu).slot==0,"an exact ingress/root rejection does not ban a different root");
}
void semantic_admission()
{
    Menu<1,3,5> menu;
    menu.observe(0,{Life::Alive,10,-1});
    auto work=offer(0,Family::Work,3),observe=offer(0,Family::Observe,1,2,{},Primitive::Observe);
    menu.put(0,work); menu.put(1,observe); menu.seal();
    auto changed=work.premises; ++changed.body;
    check(menu.admit(0,{Life::Alive,11,-1},changed).status==Status::Reprice,
        "changed relevant body facts revoke a dated quote before publication");
    work.premises=changed; menu.put(0,work);
    check(menu.admit(0,{Life::Alive,11,-1},changed).status==Status::Nominee,
        "actual fresh semantics can replace a dated offer without erasing other nominations");
    check(menu.admit(0,{Life::Unknown,12,-1},changed).wait==Wait::NeedsObservation &&
        menu.life(0)->state==Life::Alive && finish(menu).slot==1,
        "unknown selected readback defers only that Work family and preserves Observe");
    menu.clear_nominations(); menu.put(0,work); menu.seal();
    check(finish(menu).status==Status::Unknown,
        "unsupported/unknown admission is not empty physical work and survives query rebuild");
    menu.observe(0,{Life::Alive,13,-1});
    check(finish(menu).status==Status::Nominee,
        "actual positive lifecycle read resolves observation-unknown without requiring a synthetic state change");
    menu.refute(0,work.premises,Model,Refutation::UnknownFragment);
    menu.observe(0,{Life::Alive,14,-1});
    check(finish(menu).status==Status::Unknown,
        "repeated Alive cannot clear an unsupported operator-model family");
    ++work.premises.model; menu.put(0,work);
    menu.admit(0,{Life::Unknown,15,-1},work.premises);
    ++work.premises.visibility; menu.put(0,work);
    check(finish(menu).status==Status::Nominee,
        "changed relevant visibility can discharge an observation-dependent unknown");
}
void finite_selection_and_capacity()
{
    Menu<2,3,2> menu;
    menu.observe(0,{Life::Alive,1,-1}); menu.observe(1,{Life::Alive,1,-1});
    auto a=offer(0,Family::Work,10),b=offer(1,Family::Work,2);
    menu.put(0,a); menu.put(1,b); menu.seal();
    check(menu.advance(0).status==Status::NeedCompute && menu.advance(1).status==Status::NeedCompute,
        "selection has an explicit finite retained cursor");
    menu.observe(0,{Life::Alive,2,-1});
    check(menu.advance(1).status==Status::NeedCompute && menu.advance(1).slot==0 && menu.advance(0).slot==0,
        "unchanged Alive observations and polling do not restart completed selection");
    menu.put(1,offer(1,Family::Work,10));
    check(finish(menu).slot==0,"exact economic ties use stable ascending slots");
    menu.put(1,b);
    for (int i=0;i<30;++i)
    {
        const int epoch=10+3*i;
        menu.observe(0,{Life::Alive,epoch,-1}); menu.put(0,a);
        check(menu.refute(0,a.premises,All,Refutation::RejectedFragment)==Store::Stored && finish(menu).slot==1,
            "current-generation refutation preserves B across many actual source generations");
        menu.observe(0,{Life::Reward,epoch+1,-1});
        menu.observe(0,{Life::Respawning,epoch+2,0});
    }
    menu.observe(0,{Life::Alive,101,-1}); menu.put(0,a);
    menu.refute(0,a.premises,All,Refutation::RejectedFragment);
    ++a.subject.fragment; menu.put(0,a); menu.refute(0,a.premises,All,Refutation::RejectedFragment);
    ++a.subject.fragment; menu.put(0,a);
    check(menu.refute(0,a.premises,All,Refutation::RejectedFragment)==Store::Capacity,
        "truly active refutation overflow remains an explicit model limit");
    const auto limited=finish(menu);
    check(limited.status==Status::Capacity && limited.slot==1 && menu.life(0)->state==Life::Alive,
        "capacity exposes retained B fallback without pretending empty work or source absence");
    check(menu.admit(0,{Life::Alive,102,-1},a.premises).status==Status::Capacity,
        "fresh Alive cannot admit a fragment whose refutation overflow remains unresolved");
    Menu<1,1> empty; empty.seal();
    check(finish(empty).status==Status::Waiting && finish(empty).wait==Wait::NoOffers,
        "exhaustion is a named retained wait without invented production progress");
    auto bad=offer(0,Family::Work,1); bad.gem_rate=std::numeric_limits<float>::quiet_NaN();
    check(empty.put(0,bad)==Store::Invalid && finish(empty).status==Status::Unknown,
        "invalid representation cannot masquerade as a complete negative menu");
}
}
int main()
{
    absent_winner(); reward_and_resolvers(); fragment_refinement(); semantic_admission(); finite_selection_and_capacity();
    std::printf("production admission failures=%d\n",failures);
    return failures?1:0;
}
