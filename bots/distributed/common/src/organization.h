#pragma once
#include "execute.h"
#include "runtime_threat.h"
#include <array>
#include <string_view>

// SDK-independent organization decisions. Every field is refreshed from the
// executing champion's personal view; a draft appointment is only geometry.
namespace p6::organization {
enum class Intent { independent, work, approach, rally, bank, recover, escort,
                    intercept, control_watch, alternate };
struct Profile {
    std::string_view slug;
    std::array<Champion, 5> roster;
};
struct Site {
    Group group{};
    bool known = false, blocked = false;
    std::array<CycleCertificate, max_actors> cycles{};
    std::array<ScoreEvaluation, max_actors> independent{};
    std::array<int, max_actors> actors{-1,-1,-1,-1,-1,-1};
    int actor_count = 0;
    // Joint gain uses current visible actors and explicit alternatives. No
    // promised arrival, private gold or unseen state is included.
    bool joint_better = false;
    // Only the current adapter's typed three-core quote can publish this hint;
    // execution still binds and revalidates the owned quote independently.
    bool crew_current = false;
    ScoreEvaluation joint_rank{};
    int holder = -1;
    bool damaged_now = false;
    const CycleCertificate* cycle(int actor) const {
        for (int i=0; i<actor_count; ++i) if (actors[i]==actor) return &cycles[i];
        return nullptr;
    }
    bool solo(const Frame& frame, int actor) const {
        const auto* c=cycle(actor); return c && c->solo && c->valid(frame,actor,group.id);
    }
};
struct Passage {
    bool available = false, personally_clear = false, exit_safe = false;
    std::array<Point, 3> tiles{};
    int length = 0, direction = 0;
    Point entry{}, exit{}, alternate{};
    bool loaded_ally_returning = false;
};
struct Input {
    Frame frame{};
    Settings settings{};
    ScenarioWindows windows{};
    int ordinal = -1, picks = 3;
    std::array<Site, 8> sites{};
    int site_count = 0, independent_group = -1, proposed_group = -1;
    int current_group = -1;
    Phase phase = Phase::bootstrap;
    bool actual_payout = false, actual_bank = false, restored = false;
    bool observed_failure = false, hidden_collision = false;
    bool safe_now = false, at_rally = false, at_bank = false;
    Point rally{}, bank_approach{};
    Passage passage{};
    std::array<Observation<double>,max_actors> enemy_stress{};
    int enemy_stress_count = 0;
    std::array<Observation<runtime_threat::Enemy>,max_actors> local_enemies{};
    int local_enemy_count = 0;
    int legal_enemy_target = -1;
    ScoreEvaluation delivery_rank{},contest_rank{};
    bool role_ranks_current = false;
    int intercept_target = -1;
    // Public appointment geometry and positive personally verified basic facts.
    // No bit promises a future partner action or grants execution authority.
    int verification_travel = -1;
    Observation<std::uint8_t> witness_core_basic{};
    bool verification_model_complete = false;
    void reset_current() {
        // Array tails stay unreachable behind these counts. Current sites and
        // ranks are reset even when a later fuel guard skips model population.
        frame.clock={};frame.own={};frame.own_team_score=0;frame.actor_count=0;frame.group={};
        settings={};windows={};ordinal=-1;picks=3;
        site_count=0;independent_group=proposed_group=current_group=-1;phase=Phase::bootstrap;
        actual_payout=actual_bank=restored=observed_failure=hidden_collision=false;
        safe_now=at_rally=at_bank=false;rally={};bank_approach={};passage={};
        enemy_stress_count=local_enemy_count=0;legal_enemy_target=-1;delivery_rank={};contest_rank={};
        role_ranks_current=false;intercept_target=-1;
        verification_travel=-1;witness_core_basic={};verification_model_complete=false;
        for(auto& site:sites) {
            site.group={};site.known=site.blocked=false;site.actor_count=0;
            site.joint_better=site.crew_current=false;site.joint_rank={};site.holder=-1;site.damaged_now=false;
        }
    }
    const Site* site(int group) const {
        for (int i=0; i<site_count; ++i) if (sites[i].group.id==group) return &sites[i];
        return nullptr;
    }
    const Actor* ordinal_actor(int ordinal_value) const {
        if (ordinal==ordinal_value) return &frame.own.actor;
        for (int i=0; i<frame.actor_count; ++i) {
            const auto& a=frame.actors[i];
            if (a.current(frame.clock,a.subject) && a.value.team==Team::ally &&
                a.value.pick_ordinal==ordinal_value) return &a.value;
        }
        return nullptr;
    }
};
struct Decision {
    Intent intent = Intent::independent;
    int group = -1, participant = -1;
    Point waypoint{};
    bool explicit_waypoint = false;
    // Executor still applies safety, legal masks, current action allowances and
    // successful action receipts. This is a proposal, never an action receipt.
    const char* reason = "independent";
};
inline Decision independent(const Input& in) {
    return {Intent::independent,in.independent_group,-1,{},false,"independent"};
}
inline bool ready(const Actor* actor) {
    return actor && actor->team==Team::ally && actor->alive && actor->channel==Channel::none;
}
inline Exposure local_exposure(const Actor& actor,double incoming) {
    Exposure e;e.health=actor.health;e.max_health=actor.max_health;
    e.incoming_total=e.next_phase_incoming=incoming;e.observations_complete=true;
    return e;
}
// Immediate movement safety only. The raw full-cycle stress remains separate.
// An omitted, stale or mismatched enemy cannot become a zero-damage discount.
inline runtime_threat::Prepared prepare_immediate(const Input& in) {
    if(!in.frame.valid() || in.local_enemy_count<0 || in.local_enemy_count>max_actors ||
       in.local_enemy_count!=in.enemy_stress_count)return {};
    std::array<bool,max_actors> used{};
    for(int i=0;i<in.local_enemy_count;++i) {
        const auto& local=in.local_enemies[i];int matched=-1;
        for(int j=0;j<in.enemy_stress_count;++j) {
            const auto& full=in.enemy_stress[j];
            if(full.current(in.frame.clock,local.subject) && local.value.stress==full.value)
                matched=j;
        }
        if(matched<0 || used[matched])return {};
        used[matched]=true;
    }
    return runtime_threat::prepare(in.frame.clock,
        std::span<const Observation<runtime_threat::Enemy>>{in.local_enemies.data(),
            static_cast<std::size_t>(in.local_enemy_count)});
}
inline Exposure immediate_exposure(const Input& in,Point position,double camp_incoming=0) {
    auto actor=in.frame.own.actor;actor.position=position;
    return runtime_threat::exposure(actor,in.frame.clock,prepare_immediate(in),camp_incoming);
}
inline bool useful(const Actor* actor,int group) {
    return ready(actor) && (actor->ready_attack_group==group ||
        (actor->action==WitnessAction::useful_attack && actor->action_group==group) ||
        (actor->action==WitnessAction::occupied_spawn && actor->spawn_group==group));
}
inline bool joint(const Input& in,int group) {
    const auto* s=in.site(group); return s && s->known && s->joint_better && in.safe_now;
}
inline Decision work(int group,const char* reason) {
    return {Intent::work,group,-1,{},false,reason};
}
inline Decision waypoint(Intent intent,int group,Point point,const char* reason) {
    return {intent,group,-1,point,true,reason};
}
} // namespace p6::organization
