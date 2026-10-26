#pragma once
#include "../../../../covenant/src/product.h"

namespace plan4_covenant_fixture {
// A scripted encounter starts after personally observed setup/capture stages.
// Its first adjacent sample is an anchor, not a completed opponent turn.
struct EncounterSequence {
    bool checkpoint_seen=false,hold_seen=false,capture_seen=false;
    int intervals=0,last_round=-1,last_adjacent=-1;
    void observe(int round,bool checkpoint_living,bool first_capture_hold,bool first_respawning,bool adjacent) {
        if(round<0||round<=last_round)return;
        last_round=round;
        capture_seen|=hold_seen&&first_respawning;
        hold_seen|=checkpoint_seen&&first_capture_hold;
        checkpoint_seen|=checkpoint_living;
        if(capture_seen&&adjacent) {
            intervals=last_adjacent>=0&&round==last_adjacent+1?std::min(9,intervals+1):0;
            last_adjacent=round;
        } else {intervals=0;last_adjacent=-1;}
    }
    bool withdrawal_due() const {return capture_seen&&intervals>=9;}
};
// This is preparation readiness, not native mechanism qualification. In
// particular, a fresh contract has no bound execution phase yet; its initial
// choice legitimately uses the same flat complete forecast in both arms.
template<class Evidence>
bool accept_evaluation(const plan4::Information& info, const plan4::Evaluation& evaluation,
                       const Evidence& evidence) {
    if (!info.own.alive || info.own.bag || info.own.maximum_health <= 0 ||
        info.own.health < info.own.maximum_health || info.own.channel != plan4::Channel::none ||
        info.own.position != plan4::Point{28,26} || info.altar_count < 1 ||
        info.altar_count > plan4::max_altars || info.group_count < 2 ||
        info.group_count > plan4::max_groups || info.observation_truncated || info.public_layout_truncated ||
        evaluation.menu.truncated || evaluation.budget_exhausted || evaluation.menu.count < 1 ||
        evaluation.menu.count > plan4::max_jobs || evaluation.evaluated_count != evaluation.menu.count ||
        evaluation.chosen < 0 || evaluation.chosen >= evaluation.menu.count) return false;
    if (!evidence.reported || evidence.coupling_enabled ||
        (evidence.supported ? evidence.reason != 0 :
         evidence.reason != int(plan4::covenant::BindingGap::execution))) return false;
    const auto& job = evaluation.menu.jobs[evaluation.chosen];
    if (job.kind != plan4::JobKind::farm || job.group < 0 || job.group >= info.group_count ||
        job.second_group < 0 || job.second_group >= info.group_count || job.group == job.second_group ||
        job.altar < 0 || job.altar >= info.altar_count) return false;
    const auto& first = info.groups[job.group]; const auto& second = info.groups[job.second_group];
    if (first.position != plan4::Point{27,26} || second.position != plan4::Point{17,26} ||
        !first.gems || !second.gems || first.lifecycle != plan4::Lifecycle::living ||
        second.lifecycle != plan4::Lifecycle::living) return false;
    const auto& forecast = evaluation.forecasts[evaluation.chosen];
    return plan4::valid_objective(forecast.objective) ? forecast.objective.our_secured > 0 :
        forecast.completed_conversion && forecast.our_secured > 0;
}
} // namespace plan4_covenant_fixture
