// Preparation-policy boundaries, not a native mechanism witness. Reuse the
// actual fixture method and its established SDK doubles in both policy modes.
#include "fixtures/covenant-fixture/src/policy.h"
#define PLAN4_FIXTURE_POLICY_TEST
#define PLAN4_FIXTURE_ACCEPT_EVALUATION plan4_covenant_fixture::accept_evaluation
#include "plan4-fixture-warmup.cpp"

int metric(plan4::Point a, plan4::Point b, const void*) {
    return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));
}
int main() {
    using plan4_covenant_fixture::EncounterSequence;
    EncounterSequence early;
    for(int round=0;round<12;++round)early.observe(round,false,false,false,true);
    assert(!early.capture_seen&&!early.withdrawal_due());
    EncounterSequence sequence;
    sequence.observe(1,true,false,false,false);
    sequence.observe(2,false,true,false,false);
    sequence.observe(3,false,false,true,true);
    assert(sequence.capture_seen&&sequence.intervals==0&&!sequence.withdrawal_due());
    for(int round=4;round<=11;++round)sequence.observe(round,false,false,false,true);
    assert(sequence.intervals==8&&!sequence.withdrawal_due());
    sequence.observe(12,false,false,false,true);assert(sequence.withdrawal_due());
    sequence.observe(12,false,false,false,false);assert(sequence.withdrawal_due());
    sequence.observe(11,false,false,false,false);assert(sequence.withdrawal_due());
    sequence.observe(13,false,false,false,false);assert(sequence.capture_seen&&sequence.intervals==0&&!sequence.withdrawal_due());
    sequence.observe(14,false,false,false,true);sequence.observe(16,false,false,false,true);
    assert(sequence.intervals==0&&!sequence.withdrawal_due());
    EncounterSequence unproved;
    unproved.observe(1,true,true,true,true);assert(unproved.checkpoint_seen&&!unproved.hold_seen&&!unproved.capture_seen);
    unproved.observe(2,false,false,true,true);assert(!unproved.capture_seen);
    auto info = std::make_unique<plan4::Information>();
    info->round=1; info->settings.max_rounds=121; info->own.id=7;
    info->own.position={28,26}; info->own.health=info->own.maximum_health=100;
    info->own.recall_safe=true; info->own_spawn={28,25}; info->travel={metric,nullptr};
    info->group_count=2; info->altar_count=1;
    info->altars[0].id=30; info->altars[0].position={17,25}; info->altars[0].lock_known=true;
    for (int g=0;g<2;++g) {
        auto& group=info->groups[g]; group.id=23-g; group.position={27-10*g,26};
        group.health=group.full_health=100; group.clear_rounds=group.full_clear_rounds=2;
        group.gems=7; group.respawn_rounds=50; group.visible=group.all_spawns_visible=group.occupancy_known=true;
        group.lifecycle=plan4::Lifecycle::living;
        group.provenance=group.health_provenance={1,plan4::Source::personal_vision,plan4::Status::exact_now};
    }
    auto evaluation=std::make_unique<plan4::Evaluation>();
    // Find a real finite-horizon admission, rather than forcing the desired
    // pair or replacing any candidate forecast. This is policy-only input,
    // not the full native map's menu or a mechanism-readiness claim.
    for(int remaining=1;remaining<=128;++remaining) {
        info->settings.max_rounds=2000; info->round=2000-remaining;
        for(int g=0;g<2;++g)info->groups[g].provenance.observed_round=info->groups[g].health_provenance.observed_round=info->round;
        plan4::evaluate_into(*info,plan4::make_menu(*info),*evaluation);
        if(evaluation->chosen>=0) {
            const auto& selected=evaluation->menu.jobs[evaluation->chosen];
            if(selected.kind==plan4::JobKind::farm&&selected.group==0&&selected.second_group==1&&
                evaluation->forecasts[evaluation->chosen].our_secured>0) break;
        }
    }
    assert(!evaluation->budget_exhausted&&evaluation->evaluated_count==evaluation->menu.count);
    assert(evaluation->chosen>=0);
    const auto& job=evaluation->menu.jobs[evaluation->chosen];
    if(job.kind!=plan4::JobKind::farm||job.group!=0||job.second_group!=1) {
        for(int n=0;n<evaluation->menu.count;++n) {
            const auto& candidate=evaluation->menu.jobs[n];
            std::cout<<"prepare input candidate="<<n<<" group="<<candidate.group<<" second="<<candidate.second_group
                <<" secured="<<evaluation->forecasts[n].our_secured<<" chosen="<<evaluation->chosen<<std::endl;
        }
    }
    assert(job.kind==plan4::JobKind::farm&&job.group==0&&job.second_group==1);
    const auto original_info=*info;
    for(int scenario=0;scenario<19;++scenario) {
        *info=original_info; cambc::round=unsigned(original_info.round); cambc::fuel=100000;
        Bot bot; bot.initialized_=bot.movement_initialized_=true; bot.runtime_model_.supported=false;
        bot.plan_info=info.get(); bot.plan_evaluation=evaluation.get();
        auto candidate=std::make_unique<plan4::Evaluation>(*evaluation); bot.plan_evaluation=candidate.get();
        struct Commit {int calls=0;int scenario=0;} commit{0,scenario};
        switch(scenario) {
            case 1: --candidate->evaluated_count; break;
            case 2: candidate->budget_exhausted=true; break;
            case 3: info->own.health=99; break;
            case 4: info->own.bag=7; break;
            case 5: info->own.channel=plan4::Channel::tribute; break;
            case 6: info->own.position={27,26}; break;
            case 7: candidate->chosen=0; break;
            case 8: bot.runtime_model_.unsupported_reason=int(plan4::covenant::BindingGap::unknown_guard); break;
            case 9: bot.runtime_model_.coupling_enabled=true; break;
            case 10: bot.runtime_model_.reported=false; break;
            case 11: info->round=0; break;
            case 12: cambc::fuel=39999; break;
            case 13: candidate->menu.truncated=true; break;
            case 14: candidate->chosen=-1; break;
            case 18: bot.runtime_model_.supported=true; break;
            default: break;
        }
        bot.fixture_prepare([](void* raw) {
            auto& value=*static_cast<Commit*>(raw); ++value.calls;
            if(value.scenario==16)++cambc::round;
            if(value.scenario==17)cambc::fuel=29999;
            return value.scenario!=15;
        },&commit);
        assert(commit.calls==int(scenario==0||scenario>=15));
        assert(bot.selections==int(scenario==0||scenario==18));
        assert(!bot.fixture_preparing_&&!bot.fixture_commit_&&bot.fixture_prepare_round_==-1);
        assert(candidate->menu.count==evaluation->menu.count);
    }
    std::cout<<"Covenant preparation:19 policy/common-boundary scenarios plus capture-gated nine-interval timing; no native readiness claim\n";
}
