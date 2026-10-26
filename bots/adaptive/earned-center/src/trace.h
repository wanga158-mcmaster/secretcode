#pragma once
#include "episode.h"
#include "organization.h"
#include <limits>

namespace p8::earned_center {
// One actor-local physical trace. Receipt serials persist across begin(), death
// and epochs. A successful proposal never supplies a milestone observation.
struct CycleTrace {
    std::uint32_t serial = 0;
    int owner = -1, group = -1, issue_round = -1, last_round = -1, next_receipt = 0;
    int production_round = -1, payout_round = -1, bank_round = -1, spawn_round = -1, renewal_round = -1;
    int payout_event = -1, bank_event = -1, renewal_event = -1;
    std::uint64_t route = 0;
    std::uint64_t work_generation = 0;
    p6::ViewClock work_start{};
    bool active = false, completed = false, spawn_shop = false, failed = false;
    bool purchase_attempted = false, purchase_confirmed = false;
    std::int64_t payout_lower = 0, bank_lower = 0;
    Plan plan{};
    std::array<bool,6> helper_work{}, helper_released{};
    bool begin(const EvidenceLedger& ledger, const Work& work) {
        const auto* e = ledger.active();
        const auto& selected=work.plan;
        if (active || !e || !work.active || work.cycle_completed || !work.generation ||
            work.started_clock.round!=e->issue_round || work.started_clock.self!=e->premises.key.actor_id ||
            work.last_round>=e->issue_round || work.productive_action_seen || work.payout_seen || work.bank_seen ||
            e->premises.key.actor_id != selected.collector ||
            e->premises.key.source_group != selected.group ||
            e->premises.key.route_hash != plan_route_hash(selected)) return false;
        serial = e->serial; owner = e->premises.key.actor_id; group = selected.group;
        issue_round = e->issue_round; last_round = -1; plan = selected; route = plan_route_hash(plan);
        work_generation=work.generation; work_start=work.started_clock;
        production_round = payout_round = bank_round = spawn_round = renewal_round = -1;
        payout_event = bank_event = renewal_event = -1;
        active = true; completed = spawn_shop = failed = purchase_attempted = purchase_confirmed = false;
        payout_lower = bank_lower = 0; helper_work = {}; helper_released = {};
        return true;
    }
    int receipt() {
        if (next_receipt == std::numeric_limits<int>::max()) { failed = true; return -1; }
        return ++next_receipt;
    }
    void observe(const LocalFrame& local, const Work& work, const p6::Observation<bool>& shop_legal) {
        if (!active || !local.fresh() || local.frame.clock.self != owner ||
            local.frame.clock.round < last_round) return;
        last_round = local.frame.clock.round;
        if (work.generation!=work_generation || !(work.started_clock==work_start) ||
            work.last_round!=local.frame.clock.round) { failed=true; return; }
        if (local.fuel_resumed || !local.frame.own.actor.alive ||
            plan_route_hash(work.plan) != route || !work.route_witness_active) failed = true;
        if (work.productive_action_seen && production_round < 0) production_round = last_round;
        if (work.payout_seen && payout_round < 0) {
            payout_round = last_round; payout_lower = work.payout_lower; payout_event = receipt();
        }
        if (work.bank_seen && bank_round < 0) {
            bank_round = last_round; bank_lower = work.bank_lower; bank_event = receipt();
        }
        if (bank_round >= 0 && local.frame.own.at_spawn &&
            shop_legal.current(local.frame.clock,owner) && shop_legal.value && spawn_round < 0) {
            spawn_round = last_round; spawn_shop = true;
        }
        for (int i=0;i<plan.crew_count;++i) if (plan.crew[i] != owner) {
            const auto* helper = local.frame.visible(plan.crew[i]);
            if (!helper || helper->team != p6::Team::ally) continue;
            if (helper->action == p6::WitnessAction::useful_attack && helper->action_group == group)
                helper_work[i] = true;
            const auto* hold = local.certificate(owner,group);
            if (helper_work[i] && helper->spawn_group != group && hold &&
                p6::witnessed_release(local.frame,owner,group,*hold)) helper_released[i] = true;
        }
        if (work.cycle_completed && work.renewal_seen && renewal_round < 0) {
            completed = true; renewal_round = last_round; renewal_event = receipt();
        }
    }
    Resolution finish(EvidenceLedger& ledger, int round, const Premises& terminal) {
        Resolution none;
        if (!active || round < last_round) return none;
        if (!ledger.active() || ledger.active()->serial != serial) {
            // The turn boundary may already have closed a failed/fuel/death
            // episode. Retain spent receipts, but release this one active trace.
            active=false; return none;
        }
        Observation observed;
        observed.round = round; observed.premises = terminal;
        observed.kind = completed && !failed ? ObservationKind::completed : ObservationKind::unavailable;
        observed.duration = {{round-issue_round,round-issue_round},DurationSource::personally_exact};
        auto& r = observed.receipts;
        r.source = ReceiptSource::own_observation;
        r.production = production_round >= 0; r.payout_owned = payout_round >= 0;
        r.conversion = bank_round >= 0; r.renewed = renewal_round >= 0;
        // Work only accepts actual own endpoint receipts; a team-score delta
        // does not populate these values or establish alternate-loss exclusion.
        r.attribution_excludes_other_bag_loss = work_attribution_valid();
        r.payout_event = payout_event; r.conversion_event = bank_event; r.renewal_event = renewal_event;
        r.production_round = production_round; r.payout_round = payout_round;
        r.conversion_round = bank_round; r.renewal_round = renewal_round;
        r.spawn_visited = spawn_round >= 0; r.spawn_shop_legal = spawn_shop; r.spawn_round = spawn_round;
        r.purchase_attempted = purchase_attempted; r.purchase_confirmed = purchase_confirmed;
        r.supported_roles_witnessed = true;
        for (int i=0;i<plan.crew_count;++i) if (plan.crew[i] != owner)
            r.supported_roles_witnessed &= helper_work[i] && helper_released[i];
        const auto lower = std::min(payout_lower,bank_lower);
        if (lower > 0 && lower <= std::numeric_limits<int>::max()) r.positive_reward_lower = int(lower);
        active = false;
        return ledger.resolve(serial,observed);
    }
    bool work_attribution_valid() const { return payout_lower > 0 && bank_lower > 0 && !failed; }
};
} // namespace p8::earned_center
