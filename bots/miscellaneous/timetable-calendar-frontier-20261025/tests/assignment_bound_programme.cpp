#include "../src/assignment_bound_programme.h"
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <optional>
#include <utility>

namespace {
namespace bp=tt::assignment_bound_programme;
namespace pj=tt::production_job;
namespace pc=tt::production_calendar;
namespace ao=tt::assignment_owner;
namespace ag=tt::assignment_geometry;
int checks{},failures{};
void check(bool value,const char* message) {
    ++checks;if(!value){++failures;std::printf("FAIL %s\n",message);}
}
pj::Leg path(std::initializer_list<pj::Point> points,std::uint32_t handoff=1) {
    pj::Leg out;out.complete=out.disjoint_supported=true;out.handoff_inputs=handoff;
    for(const auto point:points)out.points.push_back({point,0});return out;
}
pj::Input input(std::uint32_t horizon=120) {
    pj::Input value;value.start=10;value.horizon=horizon;value.key={0,0,7,3,4,5,6};
    auto& self=value.self;self.actor=0;self.body_revision=3;self.epoch=10;
    self.own=self.complete=self.stable_outgoing=true;self.body.pos={1,2};
    self.body.hp=self.body.max_hp=1000;self.body.strength=self.raw_strength=100;
    self.body.focus=self.raw_focus=100;self.body.attack_range=1;self.body.camp_damage_multiplier=1;
    auto& job=value.job;job.group=0;job.generation=7;job.life_revision=4;job.geometry_revision=5;job.word_revision=6;
    job.life=pj::Life::Unknown;job.enabler=pj::Enabler::ObserveAlive;job.observed_round=job.available_from=10;
    job.perch={2,2};job.width=8;job.height=8;job.member_count=1;
    job.members[0]={0,0,{3,2},201,0,1,false,0,true};job.full_hp[0]=201;
    job.gems=11;job.capture_payout_inputs=6;job.respawn_inputs=20;
    job.full_group_known=job.conditional_hp=job.continuation_supported=job.external_sources_complete=true;
    job.common_full_generation=job.repeat=true;
    job.initial=path({{1,2},{2,2}});job.capture=path({{2,2},{3,2}},0);
    job.release=path({{3,2},{2,2}});job.bank=path({{2,2},{1,3}});
    job.renewal=path({{1,3},{2,2}});job.home=path({{1,3},{0,3}});
    job.redeploy=path({{0,3},{1,2},{2,2}});return value;
}
bp::Nomination nomination() {
    return {37,0,ag::Family::Basic,{2,2},{3,2},{2,2},{1,3},{0,3}};
}
bp::Seed seed() {return {input(),nomination()};}

void finish(bp::Programme& programme,std::size_t slice=1) {
    for(unsigned iterations=0;iterations<200000&&programme.status()!=bp::Status::ValuationReady&&
        programme.status()!=bp::Status::Unsupported;++iterations) {
        const auto before=programme.work_used(),producer=programme.producer_work(),calendar=programme.calendar_work();
        programme.advance(slice);
        check(programme.last_work()<=slice&&programme.work_used()-before==programme.last_work(),
            "requested bounded slice accounts every retained unit");
        check(programme.producer_work()-producer+programme.calendar_work()-calendar<=programme.last_work(),
            "no wrapper unit hides multiple real Producer or Index units");
        if(programme.status()==bp::Status::NeedCompute||programme.status()==bp::Status::ProgrammeReady)
            check(!programme.lookup(10,120,programme.input().key),"partial construction exposes no zero or partial valuation");
    }
    check(programme.status()!=bp::Status::NeedCompute&&programme.status()!=bp::Status::ProgrammeReady,
        "finite real fixture completes its independent bounded model");
}
void staged_real_producer_and_index() {
    auto programme=bp::Programme::make(seed());check(static_cast<bool>(programme),"owned fresh Seed makes a separate programme");
    if(!programme)return;
    const auto key=programme->input().key;
    check(programme->status()==bp::Status::NeedCompute&&!programme->programme_ready()&&!programme->valuation_ready()&&
        programme->forecast().status==pj::Status::NeedCompute&&!programme->lookup(10,120,key),
        "a complete bound Input is not a produced programme or full-H valuation");
    const auto initial=programme->work_used();programme->advance(0);
    check(programme->work_used()==initial&&programme->last_work()==0&&!programme->has_calendar(),
        "zero budget does not construct an index or mark totals checked");
    bool saw_programme=false,saw_unchecked_calendar=false;
    for(unsigned count=0;count<200000&&programme->status()!=bp::Status::ValuationReady&&
        programme->status()!=bp::Status::Unsupported;++count) {
        const auto before=programme->work_used(),producer=programme->producer_work(),calendar=programme->calendar_work();
        programme->advance(1);
        check(programme->last_work()==1&&programme->work_used()==before+1&&
            programme->producer_work()-producer+programme->calendar_work()-calendar<=1,
            "one unit advances at most one real producer/index unit or one explicit transition");
        if(programme->status()==bp::Status::ProgrammeReady) {
            saw_programme=true;
            check(programme->programme_ready()&&programme->forecast().status==pj::Status::Ready&&
                programme->forecast().complete&&programme->forecast().supported&&!programme->valuation_ready()&&
                !programme->lookup(10,120,key),"ProducerReady remains ineligible while the exact index is pending");
            if(programme->calendar_status()==pc::Status::Ready) {
                saw_unchecked_calendar=true;
                check(!programme->full_totals_checked()&&!programme->lookup(10,120,key),
                    "calendar readiness alone cannot skip checked full-H agreement");
                const auto work=programme->work_used();programme->advance(0);
                check(programme->work_used()==work&&!programme->full_totals_checked(),
                    "full-H comparison is an explicitly budgeted stage");
            }
        }
    }
    check(saw_programme&&saw_unchecked_calendar&&programme->valuation_ready()&&programme->full_totals_match(),
        "real Producer, real Index and total agreement complete as distinct stages");
    const auto full=programme->lookup(10,120,key);
    check(full&&full->banked_gems_by_H==programme->forecast().banked_gems_by_H&&
        full->carried_gems==programme->forecast().carried_gems&&full->key==key,
        "full-H choice matches both actual Producer resource totals and original provenance");
    check(programme->input().job.life==pj::Life::Unknown&&programme->forecast().enabler==pj::Enabler::ObserveAlive&&
        (programme->forecast().assumptions&pj::FutureHp),
        "calendar readiness preserves conditional future HP and does not promote actual Unknown to Alive");
    const auto work=programme->work_used();programme->advance(100);
    check(programme->work_used()==work&&programme->last_work()==0,"completed work is never restarted by later slices");
}
void exact_all_cutoffs_and_provenance() {
    auto value=seed();value.input.self.carried_gems=3;
    auto independent=std::make_shared<pj::Producer>(value.input);
    for(unsigned count=0;count<200000&&independent->status()==pj::Status::NeedCompute;++count)independent->advance(1);
    check(independent->status()==pj::Status::Ready,"separate real Producer finishes the exact same owned physical input");
    if(independent->status()!=pj::Status::Ready)return;
    const auto& quote=independent->result();
    pc::Index index{{quote.events,{quote.start,quote.horizon,quote.initial_carried},independent,true}};
    for(unsigned count=0;count<200000&&index.status()==pc::Status::NeedCompute;++count)index.advance(1);
    check(index.status()==pc::Status::Ready,"separate real calendar Index qualifies the immutable reference events");
    auto programme=bp::Programme::make(std::move(value));if(!programme){check(false,"programme allocation");return;}
    finish(*programme,7);check(programme->valuation_ready(),"sliced detached programme independently qualifies");
    if(!programme->valuation_ready()||index.status()!=pc::Status::Ready)return;
    const auto key=programme->input().key;
    for(const std::uint32_t start:{10u,14u})for(std::uint32_t horizon=start;horizon<=120;++horizon) {
        const auto expected=index.query(horizon-start);const auto actual=programme->lookup(start,horizon,key);
        check(actual&&expected.status==pc::Status::Ready&&actual->banked_gems_by_H==expected.banked&&
            actual->carried_gems==expected.carried&&actual->candidate_id==37&&actual->group==0&&
            actual->family==ag::Family::Basic&&actual->perch==ag::Point{2,2}&&actual->capture_tile==ag::Point{3,2}&&
            actual->release_tile==ag::Point{2,2}&&actual->bank_stand==ag::Point{1,3}&&actual->home==ag::Point{0,3}&&
            actual->key==key&&actual->start==start&&actual->horizon==horizon&&actual->conditional,
            "every inclusive original/shifted cutoff preserves literal bank/carried and exact nomination provenance");
    }
    const auto before_payout=programme->lookup(10,quote.last_payout-1,key);
    const auto at_payout=programme->lookup(10,quote.last_payout,key);
    check(before_payout&&at_payout&&at_payout->carried_gems>=before_payout->carried_gems,
        "cutoff query uses paid payout dates rather than relabelling a future bag as submitted score");
    for(unsigned field=0;field<5;++field) {
        auto changed=key;
        switch(field){case 0:++changed.actor;break;case 1:++changed.group;break;case 2:++changed.body_revision;break;
            case 3:++changed.geometry_revision;break;default:++changed.word_revision;break;}
        check(!programme->lookup(10,120,changed),"changed actual actor/group/body/geometry/word rejects the old programme");
    }
    auto fresh=key;fresh.generation+=100;fresh.life_revision+=100;
    const auto original=programme->lookup(14,120,fresh);
    check(original&&original->key==key&&programme->input().key==key&&programme->forecast().key==key,
        "fresh Life/generation may enable a common-generation conditional nomination without rekeying the old Quote");
    if(original) {
        const auto blocked=ao::PhysicalRefutation::from(*original);
        check(!programme->lookup(14,120,fresh,&blocked),"physical refutation survives changed dates and Life metadata");
        auto other=blocked;other.perch={6,6};
        check(programme->lookup(14,120,fresh,&other).has_value(),"a different named geometry does not erase the retained alternative");
    }
    check(!programme->lookup(9,120,key)&&!programme->lookup(10,121,key)&&!programme->lookup(20,19,key),
        "unsupported absolute windows cannot borrow relative-index totals");
}
void lifetime_and_no_cycle() {
    std::shared_ptr<bp::Programme> programme;const pj::Step* original_points=nullptr;
    {
        // The moved owned Input builder and Seed both die here. This exercises
        // the ownership handoff, not the separate SDK-free fresh binder model.
        auto builder=std::make_unique<pj::Input>(input());original_points=builder->job.initial.points.data();
        programme=bp::Programme::make({std::move(*builder),nomination()});
    }
    check(programme&&programme->input().job.initial.points.data()==original_points,
        "owned input paths survive the dead input builder without borrowed storage");
    if(!programme)return;
    auto lifetime=programme->producer_lifetime();finish(*programme);
    const auto* events=programme->forecast().events.data();const auto key=programme->input().key;
    check(!lifetime.expired()&&programme->valuation_ready()&&events==programme->forecast().events.data()&&
        programme->lookup(10,120,key).has_value(),"stable separate Producer owns the event span after builder destruction");
    programme.reset();check(lifetime.expired(),"Index pins Producer rather than Programme; releasing the owner leaves no self-cycle");
}
void shift_certificate_and_unsupported_fallback() {
    auto cold=seed();cold.input.job.repeat=false;
    cold.input.self.spells[0]={mb::service::SpellKind::reset,true,0,0,0,0,false};
    cold.input.self.period[0]=8;cold.input.self.cooldown[0]=5;
    auto programme=bp::Programme::make(std::move(cold));if(!programme){check(false,"cold programme allocation");return;}
    finish(*programme);
    check(programme->valuation_ready()&&!programme->forecast().shift_invariant&&
        programme->lookup(10,120,programme->input().key)&&!programme->lookup(11,120,programme->input().key),
        "a real cold represented dependency permits its original dated value but no uncertified date shift");
    auto limited=seed();limited.segment_limit=0;auto fallback=bp::Programme::make(std::move(limited));
    if(!fallback){check(false,"fallback allocation");return;}
    finish(*fallback);
    check(fallback->status()==bp::Status::Unsupported&&fallback->programme_ready()&&
        fallback->calendar_status()==pc::Status::DenseFallback&&fallback->calendar_reason()==pc::Reason::Capacity&&
        !fallback->valuation_ready()&&!fallback->lookup(10,120,fallback->input().key),
        "explicit real Index capacity fallback defers the family without a partial zero value or impossibility claim");
    auto wrong=seed();wrong.nomination.bank_stand={4,4};auto invalid=bp::Programme::make(std::move(wrong));
    check(invalid&&invalid->status()==bp::Status::Unsupported&&invalid->reason()==bp::Reason::Nomination,
        "nomination endpoints must bind the actual owned path rather than a plausible alternate bank");
    auto broken=seed();broken.input.job.home.complete=false;invalid=bp::Programme::make(std::move(broken));
    check(invalid&&invalid->status()==bp::Status::Unsupported,"all seven declared complete legs are retained, including home/redeploy");
    auto factual=seed();factual.input.job.common_full_generation=false;factual.input.job.life=pj::Life::Alive;
    factual.input.job.life_supported=true;auto unqualified=bp::Programme::make(std::move(factual));
    if(!unqualified){check(false,"non-generation allocation");return;}finish(*unqualified);
    check(unqualified->status()==bp::Status::Unsupported&&!unqualified->lookup(10,120,unqualified->input().key),
        "an actual-Life-only trace cannot masquerade as the retained common-generation programme family");
}
} // namespace
int main() {
    staged_real_producer_and_index();exact_all_cutoffs_and_provenance();lifetime_and_no_cycle();
    shift_certificate_and_unsupported_fallback();
    std::printf("assignment bound programme (pure real Producer/Index): %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
