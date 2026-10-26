#include "service_observe.h"
#include <array>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace {
using namespace mb::service;
using namespace mb::service::observation_detail;
int failures{};
void check(bool condition, const char* label)
{
    if (!condition) { std::printf("FAIL %s\n",label); ++failures; }
}

// SDK-shaped observation double. The real adapter and this double both enter
// collect_work_frame/refresh_work_frame; these tests exercise their actual fences.
struct Backend
{
    int epoch{7};
    std::size_t count{4};
    bool fuel{true}, other_sources{true}, throw_health{}, suspend_on_health{};
    int prepares{}, bodies{}, availability_queries{}, commits{};
    std::size_t last_query_bound{};
    Body own;
    std::array<Member,max_members> designs{};
    std::array<bool,max_members> seen{}, alive{}, awoken{};
    std::array<float,max_members> actual_hp{};
    std::array<int,max_members> health_queries{}, stun_queries{}, actual_stun{};
    std::array<Spell,spell_slots> spells{};
    std::array<int,spell_slots> cooldowns{};
    AvailabilityObservation group_state{Availability::Alive,-1};

    Backend()
    {
        own.pos={4,4}; own.hp=own.max_hp=1000; own.strength=40;
        own.attack_range=1;
        const Point positions[]={{5,4},{5,5},{8,4},{9,9}};
        for (std::size_t i=0;i<max_members;++i)
        {
            designs[i]={static_cast<int>(i),3,i<4?positions[i]:Point{12,12},120,0,1,false,0,false};
            seen[i]=alive[i]=true;
            actual_hp[i]=40;
        }
    }
    int round() const { return epoch; }
    std::size_t member_count() const { return count; }
    bool enough_fuel(std::size_t queries,bool) { last_query_bound=queries; return fuel; }
    void prepare() { ++prepares; }
    void body(WorkFrame& frame,bool moved)
    {
        ++bodies;
        frame.state.body=own;
        frame.state.body.moved_this_turn=moved;
        frame.state.spells=spells;
        frame.cooldowns=cooldowns;
        frame.state.width=frame.state.height=20;
        frame.projection_limits=0;
    }
    Member design(std::size_t i) const { return designs[i]; }
    bool visible(std::size_t i) const { return seen[i]; }
    bool living(std::size_t i) const { return alive[i]; }
    bool awake(std::size_t i) const { return awoken[i]; }
    float health(std::size_t i)
    {
        ++health_queries[i];
        if (throw_health) throw std::runtime_error("error fixture query");
        if (suspend_on_health) { ++epoch; suspend_on_health=false; }
        return actual_hp[i];
    }
    int stun(std::size_t i) { ++stun_queries[i]; return actual_stun[i]; }
    AvailabilityObservation availability() { ++availability_queries; return group_state; }
    bool incoming_complete(WorkFrame& frame)
    {
        if (!other_sources) frame.projection_limits|=ExternalCampThreats;
        return other_sources;
    }
    void commit(const WorkFrame&) { ++commits; }
    void reset_queries() { health_queries={}; stun_queries={}; }
};
Primitive basic(int id) { return {PrimitiveKind::basic,id,-1,{}}; }
Primitive cast(int slot,Point aim={},int id=-1) { return {PrimitiveKind::cast,id,slot,aim}; }
int health_queries(const Backend& backend)
{
    int total{}; for (int count:backend.health_queries) total+=count; return total;
}
}

int main()
{
    using namespace mb::service;
    using namespace mb::service::observation_detail;
    Backend backend;
    backend.designs[1].strength=20;
    backend.awoken[1]=true;
    backend.actual_stun[1]=2;
    backend.alive[2]=false;
    backend.seen[3]=false;
    backend.other_sources=false;
    auto frame=collect_work_frame(backend,7,3,false);
    check(frame.status==FrameStatus::Ready && backend.commits==1,"complete collection commits in its owner epoch");
    check(frame.state.members[0].hp==40 && frame.state.members[0].hp_exact && backend.health_queries[0]==1,
          "sleeping harmless partial health is queried exactly once");
    check(frame.state.members[1].hp==40 && frame.state.members[1].stun_remaining==2 && backend.stun_queries[1]==1,
          "visible threatening source carries actual health and stun");
    check(frame.state.members[2].hp==0 && frame.state.members[2].hp_exact && backend.health_queries[2]==0,
          "visible absent member is exact zero without a failing object health query");
    check(frame.state.members[3].hp==120 && !frame.state.members[3].hp_exact && backend.health_queries[3]==0 &&
          !frame.all_members_visible && !frame.state.incoming_complete &&
          (frame.projection_limits&HiddenMembers) && (frame.projection_limits&ExternalCampThreats),
          "hidden design bound and external threat deficit retain distinct provenance");
    check(backend.availability_queries==1 && frame.availability==Availability::Alive,
          "personally visible spawn permits one actual lifecycle query");

    Backend no_sight;
    no_sight.seen.fill(false);
    auto unknown=collect_work_frame(no_sight,7,3,false);
    check(unknown.availability==Availability::Unknown && no_sight.availability_queries==0 && health_queries(no_sight)==0,
          "no personally visible spawn gives no lifecycle or health inference");
    Backend reward;
    reward.alive.fill(false);
    reward.group_state={Availability::Reward,-1};
    auto capture=collect_work_frame(reward,7,3,false);
    check(capture.availability==Availability::Reward && health_queries(reward)==0 && capture.all_members_visible,
          "actual reward availability is observed without own damage or prior work");
    reward.group_state={Availability::Respawning,0};
    auto respawn=collect_work_frame(reward,7,3,false);
    check(respawn.availability==Availability::Respawning && respawn.respawn_timer==0,
          "zero respawn timer remains eligibility rather than invented availability");
    Backend renewed;
    auto damaged=collect_work_frame(renewed,7,3,false);
    renewed.actual_hp[0]=120;
    auto risen=collect_work_frame(renewed,7,3,false);
    check(damaged.state.members[0].hp==40 && risen.state.members[0].hp==120 && risen.availability==Availability::Alive,
          "actual health increase does not manufacture a lifecycle transition");

    Backend low_fuel;
    low_fuel.fuel=false;
    auto need=collect_work_frame(low_fuel,7,3,false);
    check(need.status==FrameStatus::NeedFuel && low_fuel.prepares==0 && low_fuel.bodies==0 &&
          health_queries(low_fuel)==0 && low_fuel.commits==0,"fuel refusal performs no partial observation or commit");
    Backend suspended;
    suspended.suspend_on_health=true;
    auto stale=collect_work_frame(suspended,7,3,false);
    check(stale.status==FrameStatus::Stale && suspended.commits==0,"mid-query epoch change revokes the whole frame");
    Backend wrong_epoch;
    auto prior=collect_work_frame(wrong_epoch,6,3,false);
    check(prior.status==FrameStatus::Stale && wrong_epoch.prepares==0,"entry epoch is fenced before any observation");
    Backend failed;
    failed.throw_health=true;
    bool propagated=false;
    try { (void)collect_work_frame(failed,7,3,false); } catch (const std::runtime_error&) { propagated=true; }
    check(propagated && failed.commits==0,"unexpected health query errors propagate without zero-health substitution");
    Backend malformed;
    malformed.actual_hp[0]=std::numeric_limits<float>::quiet_NaN();
    propagated=false;
    try { (void)collect_work_frame(malformed,7,3,false); } catch (const std::runtime_error&) { propagated=true; }
    check(propagated && malformed.commits==0,"invalid queried health cannot become an actionable frame");
    Backend oversized;
    oversized.count=max_members+1;
    auto incomplete=collect_work_frame(oversized,7,3,false);
    check(incomplete.status==FrameStatus::Incomplete && oversized.prepares==0,"capacity deficit is incomplete before indexing storage");
    Backend large;
    large.count=30;
    auto pack=collect_work_frame(large,7,3,false);
    check(pack.status==FrameStatus::Ready && pack.state.member_count==30 && health_queries(large)==30,
          "observed thirty-member map groups are not excluded by the old sixteen-member cap");

    Backend splash;
    splash.own.splash=true;
    auto residual=collect_work_frame(splash,7,3,false);
    splash.reset_queries();
    splash.actual_hp[0]=0; splash.alive[0]=false; splash.actual_hp[1]=17;
    splash.own.can_attack=false;
    check(refresh_work_frame(splash,residual,7,basic(0),false)==FrameStatus::Ready &&
          residual.state.members[0].hp==0 && residual.state.members[1].hp==17 && !residual.state.body.can_attack &&
          splash.health_queries[0]==0 && splash.health_queries[1]==1 && splash.health_queries[2]==0 && splash.health_queries[3]==0,
          "applied splash refresh reads affected living residual only and actual spent allowance");
    splash.spells[0]={SpellKind::reset,true,0,0,0,0,false};
    residual.state.spells=splash.spells;
    splash.reset_queries();
    splash.own.can_attack=true; splash.own.can_cast=false; splash.cooldowns[0]=8;
    check(refresh_work_frame(splash,residual,7,cast(0),false)==FrameStatus::Ready && health_queries(splash)==0 &&
          residual.state.body.can_attack && !residual.state.body.can_cast && residual.cooldowns[0]==8,
          "reset refresh observes renewed allowance and spent cooldown without rereading unchanged camp HP");
    splash.spells[1]={SpellKind::shield,true,0,90,0,4,false};
    residual.state.spells=splash.spells;
    splash.own.shield=90; splash.reset_queries();
    check(refresh_work_frame(splash,residual,7,cast(1),false)==FrameStatus::Ready && health_queries(splash)==0 &&
          residual.state.body.shield==90,"shield refresh reads actual shield rather than projecting an applied value");

    Backend aoe;
    aoe.spells[0]={SpellKind::aoe,true,10,30,1,0,false};
    auto area=collect_work_frame(aoe,7,3,false);
    aoe.reset_queries(); aoe.actual_hp[2]=10;
    check(refresh_work_frame(aoe,area,7,cast(0,{8,4}),false)==FrameStatus::Ready && aoe.health_queries[2]==1 &&
          health_queries(aoe)==1 && area.state.members[2].hp==10,
          "area refresh uses the applied absolute aim and radius rather than caster or nominal target");
    aoe.spells[0]={SpellKind::cc,true,10,30,0,2,false};
    area.state.spells=aoe.spells;
    aoe.reset_queries();
    check(refresh_work_frame(aoe,area,7,cast(0,{5,5},1),false)==FrameStatus::Ready && aoe.health_queries[1]==1 && health_queries(aoe)==1,
          "single-target control refresh preserves its actual member identity");
    aoe.reset_queries(); aoe.seen[1]=false;
    check(refresh_work_frame(aoe,area,7,cast(0,{8,4},2),false)==FrameStatus::Ready &&
          area.state.members[1].hp==120 && !area.state.members[1].hp_exact,
          "lost visibility revokes exact residual and restores the design upper bound");

    Backend cleared;
    cleared.count=1;
    auto last_member=collect_work_frame(cleared,7,3,false);
    cleared.alive[0]=false; cleared.group_state={Availability::Reward,-1}; cleared.reset_queries();
    check(refresh_work_frame(cleared,last_member,7,basic(0),false)==FrameStatus::Ready &&
          last_member.state.members[0].hp==0 && last_member.availability==Availability::Reward && health_queries(cleared)==0,
          "last-member refinement observes actual reward state rather than assuming clear implies capture");

    Backend refresh_fail;
    auto intact=collect_work_frame(refresh_fail,7,3,false);
    refresh_fail.fuel=false; refresh_fail.actual_hp[0]=1;
    check(refresh_work_frame(refresh_fail,intact,7,basic(0),false)==FrameStatus::NeedFuel && intact.state.members[0].hp==40,
          "insufficient refresh fuel invalidates authority while preserving the previous residual");
    Backend displaced;
    auto old_origin=collect_work_frame(displaced,7,3,false);
    displaced.own.pos={5,4}; displaced.reset_queries();
    check(refresh_work_frame(displaced,old_origin,7,basic(0),false)==FrameStatus::Stale && health_queries(displaced)==0 && displaced.commits==1,
          "displacement revokes the frame before applying stale target refinement");
    Backend refresh_suspend;
    auto prior_epoch=collect_work_frame(refresh_suspend,7,3,false);
    refresh_suspend.suspend_on_health=true;
    check(refresh_work_frame(refresh_suspend,prior_epoch,7,basic(0),false)==FrameStatus::Stale && refresh_suspend.commits==1,
          "mid-refinement suspension cannot publish an otherwise plausible mixed-epoch residual");
    Backend refresh_error;
    auto prior_hp=collect_work_frame(refresh_error,7,3,false);
    refresh_error.throw_health=true; propagated=false;
    try { (void)refresh_work_frame(refresh_error,prior_hp,7,basic(0),false); }
    catch (const std::runtime_error&) { propagated=true; }
    check(propagated && prior_hp.state.members[0].hp==40 && refresh_error.commits==1,
          "failed incremental query propagates while retaining the last committed residual");
    Backend invalid;
    auto bad_target=collect_work_frame(invalid,7,3,false);
    check(refresh_work_frame(invalid,bad_target,7,basic(99),false)==FrameStatus::Incomplete,
          "unknown applied target cannot select an arbitrary residual to refresh");
    auto bad_slot=collect_work_frame(invalid,7,3,false);
    check(refresh_work_frame(invalid,bad_slot,7,cast(2),false)==FrameStatus::Incomplete,
          "out-of-bound applied spell cannot index observer storage");

    std::printf("service_observe: %s (%d failures)\n",failures?"FAIL":"PASS",failures);
    return failures?1:0;
}
