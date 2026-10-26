#pragma once
#include "roles.h"
namespace p8::assay_guild {
// One fixed primary-source handoff plus at most one declared own continuation.
// Receipt serials survive starts; the secondary payout keeps its actual source.
struct RoleTrace {
    std::uint32_t serial=0;
    int next_receipt=0,owner=-1,last=-1,first_attack=-1,income_attack=-1,clear=-1,payout=-1,bank=-1,renewal=-1;
    int payout_event=-1,bank_event=-1,renewal_event=-1;
    std::uint64_t primary_generation=0,next_generation=0;
    p6::ViewClock primary_start{},next_start{};
    bool active=false,failed=false,own_attack=false,other_attack=false,handoff=false,released=false,complete=false;
    std::int64_t payout_lower=0,bank_lower=0;
    RoleSpec frozen{};
    bool begin(const EvidenceLedger& ledger,const RoleSpec& role,const Work& work) {
        const auto* episode=ledger.active();
        if (active || !episode || !work.active || work.last_round>=work.started_clock.round ||
            work.productive_action_seen || work.started_clock.self!=episode->premises.key.actor_id || episode->premises.key!=role.context.key ||
            episode->issue_round!=work.started_clock.round || role_hash(role,episode->premises.key.actor_id)!=role.context.key.route_hash ||
            plan_route_hash(work.plan)!=plan_route_hash(role.primary)) return false;
        serial=episode->serial;owner=episode->premises.key.actor_id;last=first_attack=income_attack=clear=payout=bank=renewal=-1;
        payout_event=bank_event=renewal_event=-1;primary_generation=work.generation;primary_start=work.started_clock;
        next_generation=0;next_start={};active=true;failed=own_attack=other_attack=handoff=released=complete=false;
        payout_lower=bank_lower=0;frozen=role;return true;
    }
    bool start_next(const Work& work) {
        if (!active || !released || collecting(frozen,owner) || next_generation || !work.active ||
            work.generation<=primary_generation || work.started_clock.self!=owner || work.started_clock.round!=last+1 ||
            work.last_round>=work.started_clock.round || plan_route_hash(work.plan)!=plan_route_hash(frozen.next)) return false;
        next_generation=work.generation;next_start=work.started_clock;return true;
    }
    void attack(const Work& work,bool next,int round) {
        if (!active || work.generation!=(next?next_generation:primary_generation)) return;
        if (!next) {own_attack=true;if(first_attack<0)first_attack=round;}
    }
    int receipt() { if(next_receipt==std::numeric_limits<int>::max()){failed=true;return -1;}return ++next_receipt; }
    void observe(const LocalFrame& local,const Work& work,bool next) {
        if (!active) return;
        const int round=local.frame.clock.round;
        if (!local.fresh() || local.frame.clock.self!=owner || (last<0?round!=primary_start.round:round!=last+1) ||
            work.last_round!=round || work.generation!=(next?next_generation:primary_generation) ||
            work.started_clock!=(next?next_start:primary_start) || !work.route_witness_active ||
            local.fuel_resumed || !local.frame.own.actor.alive ||
            plan_route_hash(work.plan)!=plan_route_hash(next?frozen.next:frozen.primary)) {failed=true;return;}
        last=round;
        if (!next) {
            const int other=frozen.primary.crew[0]==owner?frozen.primary.crew[1]:frozen.primary.crew[0];
            const auto* member=local.frame.visible(other);
            if(member && member->action==p6::WitnessAction::useful_attack && member->action_group==frozen.primary.group) {
                other_attack=true;if(first_attack<0)first_attack=round;
            }
            const auto* group=local.frame.live_group(frozen.primary.group);
            if(clear<0 && work.clearance_seen && group && group->all_spawns_visible &&
                group->lifecycle==p6::Lifecycle::reward_available) clear=round;
            const auto* safe=local.certificate(frozen.primary.collector,frozen.primary.group);
            if(clear>=0 && safe && independent_safety(*safe) &&
                p6::witnessed_release(local.frame,frozen.primary.collector,frozen.primary.group,*safe)) handoff=true;
            if(collecting(frozen,owner)) {
                const int pad=crew_index(frozen.primary,other);
                if(member && pad>=0 && other_attack && member->spawn_group!=frozen.primary.group && handoff &&
                    member->position!=frozen.primary.pads[pad]) released=true;
            } else if(work.helper_released && work.release_admitted && work.renewal_seen) released=handoff;
        }
        if (next || collecting(frozen,owner)) {
            if(work.plan.group!=frozen.context.key.source_group){failed=true;return;}
            if(work.productive_action_seen && income_attack<0)income_attack=round;
            if(work.payout_seen && payout<0){payout=round;payout_lower=work.payout_lower;payout_event=receipt();}
            if(work.bank_seen && bank<0){bank=round;bank_lower=work.bank_lower;bank_event=receipt();}
            if(work.cycle_completed && work.renewal_seen && renewal<0){renewal=round;renewal_event=receipt();complete=true;}
        }
    }
    Resolution finish(EvidenceLedger& ledger,const NumericState& terminal,int round) {
        Resolution none;if(!active || round!=last)return none;
        if(!ledger.active() || ledger.active()->serial!=serial){active=false;return none;}
        const bool is_collector=collecting(frozen,owner);
        const bool duties=clear>=first_attack && first_attack>=primary_start.round && handoff && released &&
            (frozen.configuration==Configuration::serial && is_collector?other_attack:own_attack) &&
            (frozen.configuration!=Configuration::joint_clear || other_attack) && (is_collector || next_generation);
        Observation observed;observed.round=round;observed.premises={frozen.context.key,terminal,all_premises};
        observed.kind=complete && duties && !failed?ObservationKind::completed:ObservationKind::unavailable;
        observed.duration={{round-primary_start.round,round-primary_start.round},DurationSource::personally_exact};
        auto& r=observed.receipts;r.source=ReceiptSource::own_observation;r.production=income_attack>=0;
        r.payout_owned=payout>=0;r.conversion=bank>=0;r.renewed=renewal>=0;r.supported_roles_witnessed=duties;
        r.attribution_excludes_other_bag_loss=payout_lower>0 && bank_lower>0 && !failed;
        r.production_round=income_attack;r.payout_round=payout;r.conversion_round=bank;r.renewal_round=renewal;
        r.payout_event=payout_event;r.conversion_event=bank_event;r.renewal_event=renewal_event;
        const auto lower=std::min(payout_lower,bank_lower);
        if(lower>0 && lower<=std::numeric_limits<int>::max())r.positive_reward_lower=int(lower);
        active=false;return ledger.resolve(serial,observed);
    }
};
}
