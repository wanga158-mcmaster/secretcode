#include "../src/assignment_owner.h"
#include <cstdio>
#include <memory>
#include <type_traits>
#include <utility>

namespace {
namespace ao=tt::assignment_owner;
namespace ag=tt::assignment_geometry;
namespace pj=tt::production_job;
namespace pa=tt::production_assignment;
int checks{},failures{};
void check(bool value,const char* message) {
    ++checks;if(!value) {++failures;std::printf("FAIL %s\n",message);}
}
static_assert(std::is_same_v<decltype(std::declval<const pj::Producer&>().programme()),const pj::Input&>);
static_assert(std::is_same_v<decltype(std::declval<const ao::ProgrammeLease&>().input()),const pj::Input&>);
ao::Seed seed(bool failed_first=false) {
    ao::Seed value;auto& grid=value.grid;grid.width=9;grid.height=7;
    grid.terrain.assign(63,0);grid.camp_group.assign(63,-1);grid.raw_upper.assign(63,0);
    grid.altars={{0,6}};grid.home_roots={{0,0}};
    grid.exclusive_ownership=grid.require_quiet_bank=true;
    value.stamp={5,6,3,7,10,{1,2}};value.model_start=10;value.horizon=100;
    value.handoff_inputs.fill(1);value.complete_npc_catalogue=value.conditional_no_future_traffic=true;
    auto& self=value.self;self.actor=0;self.body_revision=3;self.epoch=10;
    self.own=self.complete=self.stable_outgoing=true;
    self.body.pos={1,2};self.body.hp=self.body.max_hp=1000;
    self.body.strength=self.raw_strength=100;self.body.focus=self.raw_focus=100;
    self.body.attack_range=1;self.body.camp_damage_multiplier=1;
    for(unsigned i=0;i<2;++i) {
        ao::Group group;group.canonical=static_cast<std::uint8_t>(i);group.geometry_group=static_cast<int>(i);
        auto& job=group.metadata;job.group=i;job.generation=7+i;job.life_revision=4+i;
        job.geometry_revision=5;job.word_revision=6;
        job.life=pj::Life::Alive;job.life_supported=true;job.observed_round=job.available_from=10;
        job.width=9;job.height=7;job.member_count=1;
        const pj::Point point=i==0?pj::Point{3,2}:failed_first?pj::Point{4,2}:pj::Point{7,4};
        job.members[0]={0,static_cast<int>(i),point,201,failed_first&&i==1?1.0f:0.0f,1,false,0,true};
        job.full_hp[0]=201;job.gems=i==0?11:220;
        job.capture_payout_inputs=6;job.respawn_inputs=0;job.submit_divisor=1000;job.spawn_healing=1;
        job.full_group_known=job.conditional_hp=job.continuation_supported=
            job.external_sources_complete=job.common_full_generation=true;
        job.repeat=true;grid.camp_group[static_cast<std::size_t>(point.y)*9+point.x]=static_cast<int>(i);
        value.groups.push_back(std::move(group));
    }
    if(failed_first) {
        // A real external source at(4,2),range1 covers group0's only capture
        // tile. The complete Producer must reject that family; it is not a
        // publishable programme merely because its calculation finished.
        for(int y=0;y<7;++y)for(int x=0;x<9;++x)
            grid.raw_upper[static_cast<std::size_t>(y)*9+x]=ag::detail::separation({x,y},{4,2})<=1?1:0;
    }
    return value;
}
std::optional<ao::Handle> first(std::shared_ptr<ao::Runtime> runtime,ao::FrontierCursor& cursor,std::size_t& skipped) {
    for(unsigned work=0;work<200000&&runtime->status()==ao::Status::NeedCompute;++work) {
        runtime->advance(1);
        const auto row=runtime->poll_complete(runtime,cursor);
        if(row.state==ao::FrontierState::Skipped)++skipped;
        if(row.handle)return row.handle;
        if(row.state==ao::FrontierState::Stale)break;
    }
    return std::nullopt;
}
void finish(ao::Runtime& runtime) {
    for(unsigned work=0;work<200000&&runtime.status()==ao::Status::NeedCompute;++work)runtime.advance(1);
    check(runtime.status()==ao::Status::Ready,"bounded retained fixture completes the optional remaining menu");
}
void frontier_and_exact_identity() {
    auto runtime=std::make_shared<ao::Runtime>(seed());ao::FrontierCursor cursor;std::size_t skipped=0;
    const auto handle=first(runtime,cursor,skipped);
    check(handle.has_value(),"the first completed Producer and successful prefix publish before the whole menu");
    if(!handle)return;
    const auto before=runtime->coverage();
    check(!before.all_groups_finished&&before.eligible==1&&before.finished==1,
        "first-programme eligibility does not require later candidates or groups to finish");
    check(!runtime->best(0,10,100),"the old complete-group argmax remains unavailable at the new frontier");
    const auto exact=runtime->lookup_exact(*handle,10,100,handle->programme_key);
    check(exact&&exact->candidate_id==handle->candidate_id&&exact->group==0,
        "exact lookup returns the nominated candidate without an all-menu argmax claim");
    auto lease=runtime->programme(*handle,10,100,handle->programme_key);
    check(lease.has_value(),"a completed programme can be pinned without copying its owned geometry");
    if(!exact||!lease)return;
    const auto* immutable=&lease->input();const auto* events=lease->forecast().events.data();
    check(immutable->key==handle->programme_key&&immutable->job.common_full_generation&&
        immutable->job.initial.complete&&immutable->job.capture.complete&&immutable->job.release.complete&&
        immutable->job.bank.complete&&immutable->job.renewal.complete&&immutable->job.home.complete&&immutable->job.redeploy.complete,
        "lease preserves original conditional scenario, word snapshot and every owned complete leg");
    check(lease->forecast().status==pj::Status::Ready&&lease->bank_prefix().back()==lease->forecast().banked_gems_by_H,
        "successful event prefix belongs to the same completed Producer forecast");
    pa::Key pair{1,10,100,{0,1},{0,1},{700,701}};
    ao::Handle missing;
    check(runtime->quote_pair_exact(*handle,missing,pair,handle->programme_key,{}).status==pa::Status::Unknown,
        "one complete own job never fabricates the missing second own pair price");
    finish(*runtime);
    const auto retained=runtime->lookup_exact(*handle,10,100,handle->programme_key);
    check(retained&&retained->candidate_id==exact->candidate_id&&retained->banked_gems_by_H==exact->banked_gems_by_H&&
        &lease->input()==immutable&&lease->forecast().events.data()==events,
        "enrichment never substitutes or mutates an already named complete programme");
    std::optional<ao::Handle> later;
    for(std::size_t i=0;i<ao::max_candidates+1;++i) {
        const auto row=runtime->poll_complete(runtime,cursor);
        if(row.handle&&row.handle->programme_key.group==1) {later=row.handle;break;}
        if(row.state==ao::FrontierState::End)break;
    }
    check(later.has_value(),"later independent group enrichment really creates a second complete own price");
    if(later) {
        const auto better=runtime->lookup_exact(*later,10,100,later->programme_key);
        check(better&&better->banked_gems_by_H>exact->banked_gems_by_H,
            "a later higher-value candidate cannot silently replace the stable first handle");
        if(better) {
            const auto answer=runtime->quote_pair_exact(*handle,*later,pair,handle->programme_key,later->programme_key);
            check(answer.status==pa::Status::Ready&&answer.difference.delta==
                static_cast<std::int64_t>(exact->banked_gems_by_H)-static_cast<std::int64_t>(better->banked_gems_by_H),
                "pair response uses exactly the two retained own prices under the named common calendar");
        }
    }
    auto changed=handle->programme_key;++changed.body_revision;
    check(!runtime->lookup_exact(*handle,10,100,changed),"changed self body withdraws cached eligibility");
    changed=handle->programme_key;++changed.geometry_revision;
    check(!runtime->lookup_exact(*handle,10,100,changed),"changed geometry cannot borrow the old complete paths");
    changed=handle->programme_key;++changed.word_revision;
    check(!runtime->lookup_exact(*handle,10,100,changed),"changed compatible word cannot borrow the old event trace");
    auto tampered=*handle;++tampered.programme_key.generation;
    check(!runtime->lookup_exact(tampered,10,100,tampered.programme_key),"a different immutable programme identity is rejected");
    check(!runtime->lookup_exact(*handle,9,100,handle->programme_key),"programme cannot be shifted before its supported ready start");
    const auto blocked=ao::PhysicalRefutation::from(*exact);
    runtime->refresh_group(0,900,901);changed=handle->programme_key;changed.generation=900;changed.life_revision=901;
    const auto rebased=runtime->lookup_exact(*handle,14,100,changed);
    check(rebased&&rebased->key==handle->programme_key,
        "a fresh Life enabler may reuse a full-generation counterfactual without relabeling its original provenance");
    check(!runtime->lookup_exact(*handle,14,100,changed,&blocked),
        "new Life and later calendar do not erase caller-retained failed geometry evidence");
    auto other=std::make_shared<ao::Runtime>(seed());
    check(!other->lookup_exact(*handle,10,100,handle->programme_key),"handles cannot cross Runtime lifetime identities");
    check(other->poll_complete(other,cursor).state==ao::FrontierState::Stale,
        "an old frontier cursor cannot accidentally skip a replacement Runtime's candidates");
    std::weak_ptr<ao::Runtime> lifetime=runtime;runtime.reset();
    check(!lifetime.expired()&&lease->input().key==handle->programme_key,
        "the immutable programme survives caller Runtime reset through its explicit lifetime lease");
    lease.reset();
    check(lifetime.expired()&&handle->runtime.expired(),"a bare stable handle does not keep a retired Runtime alive");
}
void failed_completion_is_not_a_programme() {
    auto runtime=std::make_shared<ao::Runtime>(seed(true));ao::FrontierCursor cursor;std::size_t skipped=0;
    const auto handle=first(runtime,cursor,skipped);
    check(handle&&handle->programme_key.group==1&&skipped>0,
        "completed unsupported external-capture rows are skipped while a later feasible group remains discoverable");
    if(handle)check(runtime->coverage().finished>runtime->coverage().eligible,
        "calculation completion and programme eligibility remain distinct real states");
}
void optional_enrichment_failure_preserves_complete_programme() {
    auto value=seed();value.limits.candidates=1;
    auto runtime=std::make_shared<ao::Runtime>(std::move(value));ao::FrontierCursor cursor;std::size_t skipped=0;
    const auto handle=first(runtime,cursor,skipped);
    check(handle.has_value(),"a one-candidate capacity still publishes its complete supported first programme");
    if(!handle)return;
    for(unsigned work=0;work<200000&&runtime->status()==ao::Status::NeedCompute;++work)runtime->advance(1);
    check(runtime->status()==ao::Status::Unsupported&&runtime->reason()==ao::Reason::Capacity,
        "the fixture actually stops unfinished enrichment at its explicit representation capacity");
    check(runtime->lookup_exact(*handle,10,100,handle->programme_key).has_value(),
        "an optional later menu capacity failure does not erase completed independent programme eligibility");
    check(runtime->poll_complete(runtime,cursor).state==ao::FrontierState::End,
        "a stopped enrichment owner does not promise progress beyond its completed frontier");
}
}
int main() {
    frontier_and_exact_identity();failed_completion_is_not_a_programme();
    optional_enrichment_failure_preserves_complete_programme();
    std::printf("assignment frontier: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
