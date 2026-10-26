#pragma once
#include "profile.h"
#include "../../common/src/policy.h"
namespace p8::assay_guild {
struct AssaySpec {
    Plan ordinary{};
    ContextDeclaration fixed{};
    p6::Fact<int> nominal_clear{},nominal_logistics{};
};
inline bool nominal_fact(p6::Fact<int> value) {
    return value.value>=0 && (value.source==p6::Source::public_map || value.source==p6::Source::scheduled ||
        value.source==p6::Source::queried_setting || value.source==p6::Source::synthetic_fixture);
}
struct Assay {
    bool attempted=false,active=false,resolved=false,failed=false;
    int owner=-1,first_attack=-1,full_clear=-1,payout=-1,bank=-1,renewal=-1,last=-1;
    int clear=-1,logistics=-1,nominal_clear=-1,nominal_logistics=-1;
    std::uint64_t generation=0;
    p6::ViewClock started{};
    AssaySpec spec{};
    bool begin(const AssaySpec& fixed,const common::Inputs& in,const Work& work) {
        if (attempted || !work.active || !work.generation || work.started_clock!=in.frame.clock || work.last_round>=work.started_clock.round ||
            work.productive_action_seen || work.payout_seen || work.bank_seen ||
            in.frame.own.actor.pick_ordinal!=0 || work.plan.organization!=Organization::U ||
            work.plan.collector!=in.frame.clock.self || work.plan.crew_count!=1 ||
            !nominal_fact(fixed.nominal_clear) || !nominal_fact(fixed.nominal_logistics) ||
            fixed.fixed.key.route_hash!=plan_route_hash(fixed.ordinary) ||
            plan_route_hash(work.plan)!=plan_route_hash(fixed.ordinary) ||
            !common::guarded_key_current(in,fixed.fixed.key) ||
            !fixed.fixed.envelope.contains(in.own_state.value,fixed.fixed.key.build)) return false;
        attempted=active=true;owner=in.frame.clock.self;generation=work.generation;started=work.started_clock;
        spec=fixed;nominal_clear=fixed.nominal_clear.value;nominal_logistics=fixed.nominal_logistics.value;return true;
    }
    void unavailable() { if (!attempted) {attempted=true;failed=true;} }
    void observe(const common::Inputs& in,const LocalFrame& local,const Work& work) {
        if (!active) return;
        const int round=local.frame.clock.round;
        if (!common::valid_inputs(in) || !local.fresh() || local.frame.clock!=in.frame.clock || local.frame.clock.self!=owner ||
            (last<0?round!=started.round:round!=last+1) || work.generation!=generation ||
            work.started_clock!=started || work.last_round!=round || local.fuel_resumed ||
            !work.route_witness_active || plan_route_hash(work.plan)!=spec.fixed.key.route_hash ||
            !spec.fixed.envelope.contains(in.own_state.value,spec.fixed.key.build)) {
            failed=true;active=false;return;
        }
        last=round;
        if (work.productive_action_seen && first_attack<0) first_attack=round;
        const auto* group=local.frame.live_group(work.plan.group);
        if (full_clear<0 && first_attack>=0 && work.clearance_seen && group &&
            group->all_spawns_visible && group->lifecycle==p6::Lifecycle::reward_available) full_clear=round;
        if (work.payout_seen && payout<0) payout=round;
        if (work.bank_seen && bank<0) bank=round;
        if (work.cycle_completed && work.renewal_seen && work.plan.renewal.count>0 &&
            local.frame.own.actor.position==work.plan.renewal.tiles[work.plan.renewal.count-1]) renewal=round;
        if (renewal>=0) {
            active=false;resolved=!failed && first_attack>=started.round && full_clear>=first_attack &&
                payout>=full_clear && bank>=payout && renewal>=bank && work.payout_lower>0 && work.bank_lower>0;
            if (resolved) {clear=full_clear-first_attack;logistics=renewal-payout;} else failed=true;
        } else if (!work.active) {active=false;failed=true;}
    }
    Configuration desired(common::Arm arm) const {
        if (!resolved) return Configuration::independent;
        return arm==common::Arm::active?division(clear,logistics):division(nominal_clear,nominal_logistics);
    }
    bool current(const common::Inputs& in) const {
        return resolved && common::guarded_key_current(in,spec.fixed.key) &&
            spec.fixed.envelope.contains(in.own_state.value,spec.fixed.key.build);
    }
};
}
