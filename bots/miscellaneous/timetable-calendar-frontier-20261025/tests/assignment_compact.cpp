#include "../src/assignment_owner.h"
#include <cstdio>
#include <memory>
#include <optional>
#include <utility>

namespace {
namespace ao=tt::assignment_owner;
namespace pj=tt::production_job;
namespace pc=tt::production_calendar;
int checks{},failures{};
void check(bool value,const char* text) {
    ++checks;if(!value){++failures;std::printf("FAIL %s\n",text);}
}
// Synthetic complete physical inputs, shared in shape with the caller fixture.
// No SDK observations, native execution or action authority are asserted here.
ao::Seed seed() {
    ao::Seed v;auto& grid=v.grid;grid.width=9;grid.height=7;
    grid.terrain.assign(63,0);grid.camp_group.assign(63,-1);grid.raw_upper.assign(63,0);
    grid.altars={{0,6}};grid.home_roots={{0,0}};
    grid.exclusive_ownership=grid.require_quiet_bank=true;
    v.stamp={5,6,3,7,10,{1,2}};v.model_start=10;v.horizon=100;v.handoff_inputs.fill(1);
    v.complete_npc_catalogue=v.conditional_no_future_traffic=true;
    auto& s=v.self;s.actor=0;s.body_revision=3;s.epoch=10;
    s.own=s.complete=s.stable_outgoing=true;s.body.pos={1,2};s.body.hp=s.body.max_hp=1000;
    s.body.strength=s.raw_strength=100;s.body.focus=s.raw_focus=100;s.body.attack_range=1;
    s.spells[0]={mb::service::SpellKind::aoe,true,3,100,1,0,false};
    s.period[0]=2;s.focus_modifier[0]=1;
    for(unsigned i=0;i<2;++i) {
        ao::Group group;group.canonical=static_cast<std::uint8_t>(i);group.geometry_group=static_cast<int>(i);
        auto& job=group.metadata;job.group=i;job.generation=7+i;job.life_revision=4+i;
        job.geometry_revision=5;job.word_revision=6;job.life=pj::Life::Alive;job.life_supported=true;
        job.observed_round=job.available_from=10;job.width=9;job.height=7;job.member_count=1;
        const pj::Point point=i==0?pj::Point{3,2}:pj::Point{7,4};
        job.members[0]={0,static_cast<int>(i),point,201,0,1,false,0,true};job.full_hp[0]=201;
        job.gems=i==0?11:220;job.capture_payout_inputs=6;job.submit_divisor=1000;job.spawn_healing=1;
        job.full_group_known=job.conditional_hp=job.continuation_supported=job.external_sources_complete=
            job.common_full_generation=job.repeat=true;
        grid.camp_group[static_cast<std::size_t>(point.y)*9+point.x]=static_cast<int>(i);
        v.groups.push_back(std::move(group));
    }
    return v;
}
std::optional<ao::Handle> first_programme(std::shared_ptr<ao::Runtime> runtime) {
    ao::FrontierCursor cursor;
    for(unsigned work=0;work<200000;++work) {
        const auto row=runtime->poll_programme(runtime,cursor);
        if(row.handle)return row.handle;
        if(row.state==ao::FrontierState::End||row.state==ao::FrontierState::Stale)break;
        if(row.state==ao::FrontierState::NeedCompute)runtime->advance_programmes(1);
    }
    const auto d=runtime->diagnostic();
    std::printf("no producer frontier: runtime%u reason%u phase%u producer%u work%llu\n",
        static_cast<unsigned>(runtime->status()),static_cast<unsigned>(runtime->reason()),d.phase,
        d.producer_status,static_cast<unsigned long long>(d.producer_work));
    return std::nullopt;
}
std::optional<ao::Handle> first_dense(std::shared_ptr<ao::Runtime> runtime,bool programme_path) {
    ao::FrontierCursor cursor;
    for(unsigned work=0;work<200000;++work) {
        const auto row=runtime->poll_complete(runtime,cursor);
        if(row.handle)return row.handle;
        if(row.state==ao::FrontierState::End||row.state==ao::FrontierState::Stale)break;
        if(row.state==ao::FrontierState::NeedCompute) {
            if(programme_path)runtime->advance_programmes(1);else runtime->advance(1);
        }
    }
    return std::nullopt;
}
void finish_index(ao::IndexedProgramme& indexed) {
    for(unsigned work=0;work<200000&&indexed.status()==pc::Status::NeedCompute;++work)indexed.advance(1);
}
bool same_value(const std::optional<ao::Choice>& a,const std::optional<ao::Choice>& b) {
    return a&&b&&a->candidate_id==b->candidate_id&&a->key==b->key&&a->group==b->group&&
        a->perch==b->perch&&a->family==b->family&&a->start==b->start&&a->horizon==b->horizon&&
        a->banked_gems_by_H==b->banked_gems_by_H&&a->carried_gems==b->carried_gems;
}
void compact_before_dense() {
    auto runtime=std::make_shared<ao::Runtime>(seed());const auto handle=first_programme(runtime);
    check(handle.has_value(),"real complete Producer reaches the programme frontier");if(!handle)return;
    const auto before=runtime->coverage();
    check(!before.all_groups_finished&&before.finished==0&&before.eligible==0,
        "ProducerReady precedes every finished dense prefix and later menu completion");
    check(!runtime->lookup_exact(*handle,10,100,handle->programme_key)&&!runtime->best(0,10,100),
        "ProducerReady alone never publishes a dense value or old group answer");
    auto indexed=ao::IndexedProgramme::open(runtime,*handle);
    check(static_cast<bool>(indexed),"exact finished programme opens a separate calendar owner");if(!indexed)return;
    check(indexed->forecast().status==pj::Status::Ready&&indexed->forecast().complete&&
        indexed->input().key==handle->programme_key&&indexed->forecast().key==handle->programme_key,
        "index binds the original strict ProducerReady input and forecast");
    check(!indexed->valuation_ready()&&!indexed->lookup_exact(10,100,handle->programme_key)&&!indexed->release_dense(),
        "an unfinished index cannot make ProducerReady eligible or release its fallback");
    finish_index(*indexed);
    check(indexed->status()==pc::Status::Ready&&indexed->valuation_ready()&&indexed->work_used()>0,
        "checked compact index independently becomes valuation ready");
    check(runtime->coverage().finished==0&&!runtime->lookup_exact(*handle,10,100,handle->programme_key),
        "compact eligibility did not secretly finish legacy PrefixInit/PrefixSum");
    const auto exact=indexed->lookup_exact(10,100,handle->programme_key);
    check(exact&&exact->candidate_id==handle->candidate_id&&exact->banked_gems_by_H>0,
        "first compact value is an actual bankable exact candidate");
    auto dense=std::make_shared<ao::Runtime>(seed());const auto counterpart=first_dense(dense,false);
    check(counterpart&&counterpart->candidate_id==handle->candidate_id&&counterpart->programme_key==handle->programme_key,
        "separate legacy runtime completes the identical deterministic physical programme");
    if(!counterpart)return;
    for(const std::uint32_t start:{10u,14u})for(std::uint32_t cut=start;cut<=100;++cut)
        check(same_value(indexed->lookup_exact(start,cut,handle->programme_key),
            dense->lookup_exact(*counterpart,start,cut,counterpart->programme_key)),
            "every inclusive cutoff and shifted cutoff equals its separate dense counterpart");
    auto changed=handle->programme_key;++changed.body_revision;
    check(!indexed->lookup_exact(10,100,changed),"fresh wrong body cannot borrow compact valuation");
    auto forged=*handle;++forged.programme_key.generation;
    check(!ao::IndexedProgramme::open(runtime,forged),"forged original programme key cannot open an index");
    auto replacement=std::make_shared<ao::Runtime>(seed());
    check(!ao::IndexedProgramme::open(replacement,*handle),"runtime replacement cannot inherit a stable old handle");
    if(exact) {
        const auto blocked=ao::PhysicalRefutation::from(*exact);changed=handle->programme_key;
        runtime->refresh_group(0,900,901);changed.generation=900;changed.life_revision=901;
        const auto shifted=indexed->lookup_exact(14,100,changed);
        check(shifted&&shifted->key==handle->programme_key&&indexed->input().key==handle->programme_key,
            "fresh enabler support leaves immutable full-generation programme provenance unchanged");
        check(!indexed->lookup_exact(14,100,changed,&blocked),"compact lookup preserves physical refutation across dates");
    }
    const auto* original_input=&indexed->input();const auto* original_events=indexed->forecast().events.data();
    check(indexed->release_dense(),"only Ready index can release its obsolete dense work");
    check(runtime->coverage().finished==1&&runtime->coverage().eligible==0&&
        !runtime->lookup_exact(*handle,10,100,handle->programme_key)&&!runtime->best(0,10,100)&&
        !runtime->programme(*handle,10,100,handle->programme_key),
        "release marks compact row finished but never old-dense Ready");
    check(indexed->lookup_exact(10,100,handle->programme_key).has_value(),"compact lookup survives dense release");
    std::weak_ptr<ao::Runtime> lifetime=runtime;runtime.reset();
    check(!lifetime.expired() && &indexed->input()==original_input && indexed->forecast().events.data()==original_events&&
        indexed->input().key==handle->programme_key&&indexed->lookup_exact(10,100,handle->programme_key),
        "sidecar pins immutable physical programme and calendar after controller menu reset");
    indexed.reset();check(lifetime.expired()&&handle->runtime.expired(),"no sidecar/runtime ownership cycle remains");
}
void dense_capacity_fallback() {
    auto runtime=std::make_shared<ao::Runtime>(seed());const auto handle=first_programme(runtime);
    check(handle.has_value(),"capacity fallback starts from a real completed physical trace");if(!handle)return;
    auto indexed=ao::IndexedProgramme::open(runtime,*handle,0);
    check(indexed&&!indexed->forecast().events.empty(),"real trace has events exceeding explicit zero segment capacity");
    if(!indexed)return;
    check(indexed->status()==pc::Status::DenseFallback&&indexed->reason()==pc::Reason::Capacity,
        "bounded representation limit requests dense fallback without inventing an unsupported trace");
    check(!indexed->valuation_ready()&&!indexed->lookup_exact(10,100,handle->programme_key)&&!indexed->release_dense(),
        "unsupported index is neither an eligible value nor permission to discard dense work");
    check(runtime->resume_dense(*handle),"exact existing physical programme resumes its ordinary dense owner");
    const auto dense=first_dense(runtime,true);
    check(dense&&*dense==*handle,"resumed fallback finishes the same original candidate and programme key");
    check(indexed->status()==pc::Status::DenseFallback&&indexed->valuation_ready(),
        "completed old dense prefix supplies eligibility while compact status remains fallback");
    if(dense)for(std::uint32_t cut=10;cut<=100;++cut)
        check(same_value(indexed->lookup_exact(10,cut,handle->programme_key),
            runtime->lookup_exact(*dense,10,cut,dense->programme_key)),
            "fallback sidecar delegates every exact cutoff to the retained old dense result");
    check(!indexed->release_dense()&&runtime->programme(*handle,10,100,handle->programme_key),
        "fallback retains complete ordinary programme and cannot release it as compact Ready");
    auto wrong=handle->programme_key;++wrong.body_revision;
    check(!indexed->lookup_exact(10,100,wrong),"dense fallback still requires fresh own body support");
}
}
int main() {
    compact_before_dense();dense_capacity_fallback();
    std::printf("assignment compact integration (pure, no SDK actions): %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
