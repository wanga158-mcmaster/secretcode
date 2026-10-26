#pragma once
#include "phase-tracker.h"

namespace plan4::runtime {
// Fresh physical observations only. A lifecycle endpoint can establish CLEAR;
// it cannot establish the recipient of a later capture transaction.
struct ProductionObservation {
    ObservationClock clock{};
    Provenance own_provenance{};
    bool alive=false,at_requested_goal=false,on_own_spawn=false,channel_active=false;
    float health=0,maximum_health=0;
    int group_id=-1;
    Lifecycle group_lifecycle=Lifecycle::unknown;
    Provenance group_provenance{};
    bool group_visible=false;
};
enum class ProductionVerdict { pending,complete,lost_sight,ambiguous,source };
struct ProductionEndpoint {
    ProductionVerdict verdict=ProductionVerdict::source;
    EndpointKind kind=EndpointKind::none;
    Provenance provenance{};
};
class ProductionEndpointEvidence {
    PhaseStart request_{};
    float restore_maximum_=0;
    bool active_=false,targeted_action_=false;
    int action_round_=-1;
    static bool fresh(Provenance p,int round,Source source) {
        return p.observed_round==round && p.status==Status::exact_now && p.source==source;
    }
    static bool own(const ProductionObservation& observation) {
        return observation.clock.round>=0 && observation.clock.own.id>=0 && observation.alive &&
            fresh(observation.own_provenance,observation.clock.round,Source::own_state) &&
            std::isfinite(observation.health) && std::isfinite(observation.maximum_health) &&
            observation.maximum_health>0 && observation.health>0 && observation.health<=observation.maximum_health;
    }
public:
    void reset(){active_=targeted_action_=false;action_round_=-1;}
    bool begin(const PhaseStart& request,const ProductionObservation& observation) {
        reset();
        if(!own(observation) || request.actor!=observation.clock.own.id || request.start_round!=observation.clock.round ||
            request.nominal_duration<=0 || request.emergency || observation.channel_active)return false;
        if(request.phase==ProductionPhase::clear && (request.group_id<0 || observation.group_id!=request.group_id ||
            !observation.group_visible || !fresh(observation.group_provenance,observation.clock.round,Source::personal_vision) ||
            observation.group_lifecycle!=Lifecycle::living))return false;
        if(request.phase==ProductionPhase::restore && observation.health>=observation.maximum_health)return false;
        if(request.phase!=ProductionPhase::travel && request.phase!=ProductionPhase::clear && request.phase!=ProductionPhase::restore)return false;
        request_=request;restore_maximum_=observation.maximum_health;active_=true;return true;
    }
    void targeted_action(bool executed,int round){if(active_ && request_.phase==ProductionPhase::clear && executed && round>=request_.start_round) {
        targeted_action_=true;action_round_=round;
    }}
    bool has_targeted_action()const{return active_ && targeted_action_;}
    ProductionEndpoint inspect(const PhaseStart& pending,const ProductionObservation& observation,bool at_end)const {
        if(!active_ || pending.actor!=request_.actor || pending.job_epoch!=request_.job_epoch ||
            pending.phase_sequence!=request_.phase_sequence || pending.phase!=request_.phase || pending.group_id!=request_.group_id ||
            pending.start_round!=request_.start_round || pending.nominal_duration!=request_.nominal_duration ||
            pending.stat_epoch!=request_.stat_epoch || pending.mode!=request_.mode || pending.emergency!=request_.emergency ||
            !own(observation) || observation.clock.own.id!=request_.actor || observation.clock.round<request_.start_round)return {};
        if(pending.phase==ProductionPhase::travel) {
            if(observation.channel_active)return {ProductionVerdict::ambiguous};
            if(observation.at_requested_goal)return {ProductionVerdict::complete,EndpointKind::travel_arrival,observation.own_provenance};
        } else if(pending.phase==ProductionPhase::clear) {
            if(observation.group_id!=request_.group_id)return {};
            if(!observation.group_visible || !fresh(observation.group_provenance,observation.clock.round,Source::personal_vision))return {ProductionVerdict::lost_sight};
            if(observation.channel_active)return {ProductionVerdict::ambiguous};
            if(observation.group_lifecycle==Lifecycle::cleared) {
                // A later START proves the world is clear, but cannot tell
                // which round's intervening champion action cleared it.
                if(!targeted_action_ || !at_end || action_round_!=observation.clock.round)return {ProductionVerdict::ambiguous};
                return {ProductionVerdict::complete,EndpointKind::group_clear,observation.group_provenance};
            }
            if(observation.group_lifecycle!=Lifecycle::living || observation.channel_active)return {ProductionVerdict::ambiguous};
        } else if(pending.phase==ProductionPhase::restore) {
            if(observation.maximum_health!=restore_maximum_)return {ProductionVerdict::ambiguous};
            if(!at_end && observation.on_own_spawn && !observation.channel_active && observation.health==observation.maximum_health)
                return {ProductionVerdict::complete,EndpointKind::own_restored,observation.own_provenance};
        }
        return {ProductionVerdict::pending};
    }
};

// Exact uncorrected M0 RENEW duration. A healthy no-op wait is not RESTORE.
// Keep the same float arithmetic as pure.h; return -1 for unsupported sources.
inline int nominal_restore(const Information& info) {
    const auto& own=info.own;
    if(info.round<0 || info.round>=std::numeric_limits<int>::max()-max_horizon || info.settings.max_rounds<=info.round ||
        !own.alive || own.channel!=Channel::none || info.in_progress_group_id>=0 ||
        !std::isfinite(own.health) || !std::isfinite(own.maximum_health) || own.health<=0 || own.maximum_health<=0 || own.health>own.maximum_health)
        return -1;
    if(own.health==own.maximum_health)return 0;
    float hp=own.health;int elapsed=0;
    if(!own.on_spawn && own.position!=info.own_spawn) {
        if(!own.recall_safe || own.escape_rounds<0 || own.escape_rounds>max_horizon || info.settings.recall_rounds<0 ||
            info.settings.recall_rounds>max_horizon || !std::isfinite(own.escape_incoming) || own.escape_incoming<0)return -1;
        hp-=own.escape_incoming*float(own.escape_rounds+1);
        if(hp<=0)return -1;
        elapsed=own.escape_rounds+info.settings.recall_rounds;
    }
    const float per_turn=own.maximum_health*info.settings.spawn_heal_fraction;
    if(!std::isfinite(per_turn) || per_turn<=0)return -1;
    const double healing=std::ceil((own.maximum_health-hp)/per_turn);
    if(!std::isfinite(healing) || healing<0 || healing>max_horizon)return -1;
    elapsed+=int(healing);
    return elapsed>0 && elapsed<=horizon(info)?elapsed:-1;
}
} // namespace plan4::runtime
