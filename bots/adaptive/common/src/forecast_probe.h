#pragma once
#include "../../distributed-common/src/observe.h"

// Plan8-owned forecast variant derived from canonical80a3acd6. The canonical
// builder remains the default; opt-in callers use a fieldwise-checked variant.
namespace p6::forecast::plan8_probe {
// Optional synchronous numeric instrumentation. Core forecasting has no engine
// dependency; callers own the context and may not derive permissions from it.
struct BuildProbe {
    void* context = nullptr;
    void (*mark)(void*, unsigned) noexcept = nullptr;
    void operator()(unsigned stage) const noexcept { if (mark) mark(context, stage); }
};
inline void build_into(const Frame& frame, const Request& request, Result& out, BuildProbe probe = {}) {
    struct ExitMark {
        BuildProbe probe;
        ~ExitMark() noexcept { probe(8); }
    } exit_mark{probe};
    probe(0);
    // A runtime may reuse this storage. Counts and validity gates hide unused
    // payload; initialize each active record when appending it instead of
    // clearing the full schedule and phase arrays on every candidate.
    out.status = Status::unresolved; out.issue = Issue::unknown_input;
    out.abort = Failure::none;
    out.schedule.actor_count = out.schedule.production_count = 0;
    out.schedule.target_id = -1;
    out.schedule.immediate_win = {};
    out.schedule.terminal_tiebreak = {};
    out.certificate = {};
    out.phase_count = 0; out.first = {};
    out.recovery_health_unmodeled = false; out.purchases_deferred = true;
    probe(1);
    // The bounds are semantic/fuel guards, not game settings. Larger inputs are
    // rejected, never truncated or assigned a zero duration.
    constexpr int work_cap = 1000000;
    constexpr Gems exact_gem_cap = 9007199254740991LL;
    auto fail = [&](Issue issue, Status status = Status::unresolved,
                    Failure abort = Failure::none) {
        out.issue = issue; out.status = status; out.abort = abort;
        // Even a failure after schedule construction must not leave a usable
        // earlier certificate or proposal in caller-owned reusable storage.
        out.schedule.actor_count = out.schedule.production_count = 0;
        out.schedule.target_id = -1;
        out.phase_count = 0; out.first = {};
        out.certificate.route_known = out.certificate.bank_executable =
            out.certificate.renewal_reachable = false;
    };
    auto number = [](double value) { return std::isfinite(value) && value >= 0; };
    auto static_source = [](Source source) {
        return source == Source::public_map || source == Source::queried_setting;
    };
    auto point_ok = [](Point point) {
        return point.x >= -1000000 && point.x <= 1000000 &&
               point.y >= -1000000 && point.y <= 1000000;
    };
    const auto& own = frame.own.actor;
    if (!frame.valid() || !number(own.health) || !number(own.max_health) ||
        !number(own.strength) || own.max_health <= 0 || own.health > own.max_health ||
        own.bag < 0 || frame.own.gold < 0 || !point_ok(own.position))
        return fail(Issue::malformed, Status::invalid);
    if (!own.alive || own.health <= 0 || own.channel != Channel::none)
        return fail(Issue::unavailable, Status::unresolved, Failure::observed_unavailable);
    // The queried multiplier is a double; do not round a larger integer bag
    // down while converting it for the engine's ceil(multiplier * bag) rule.
    if (own.bag > exact_gem_cap) return fail(Issue::overflow, Status::invalid);
    if (request.purchase_requested) return fail(Issue::unsupported_purchase);
    const auto& timing = request.timing;
    if (timing.settings.source != Source::queried_setting ||
        timing.tribute_multiplier.source != Source::queried_setting ||
        timing.tribute_base.source != Source::queried_setting ||
        timing.tribute_minimum.source != Source::queried_setting)
        return fail(Issue::unknown_input, Status::unresolved, Failure::not_observed);
    const auto settings = timing.settings.value;
    if (!p6::valid(settings) || !number(timing.tribute_multiplier.value) ||
        timing.tribute_minimum.value < 1 || timing.tribute_base.value < -work_cap ||
        timing.tribute_base.value > work_cap)
        return fail(Issue::malformed, Status::invalid);
    if (settings.max_rounds > work_cap || settings.capture_rounds >= work_cap ||
        timing.tribute_minimum.value > work_cap)
        return fail(Issue::overflow, Status::invalid);
    if (frame.clock.round > settings.max_rounds) return fail(Issue::horizon);
    const int horizon = settings.max_rounds - frame.clock.round;
    int expected_groups = 0;
    switch (request.shape) {
    case Shape::bank_only: expected_groups = 0; break;
    case Shape::produce_bank: case Shape::bank_then_produce_bank: expected_groups = 1; break;
    case Shape::produce_bank_produce_bank: case Shape::retain_two_then_bank: expected_groups = 2; break;
    default: return fail(Issue::malformed, Status::invalid);
    }
    if (request.group_count != expected_groups ||
        (request.shape == Shape::bank_only && request.project_group < 0) || request.leg_count < 0 ||
        request.leg_count > static_cast<int>(request.legs.size()) ||
        request.enemy_cast_count < 0 || request.enemy_cast_count > 6 ||
        request.bank_waypoint < 0 || request.renewal_waypoint < 0 ||
        !point_ok(request.bank_position) || !point_ok(request.renewal_position))
        return fail(Issue::malformed, Status::invalid);
    if ((expected_groups > 0 && !request.kit.current(frame.clock, own.id)) ||
        !request.ambient_incoming.current(frame.clock, own.id))
        return fail(Issue::unknown_input, Status::unresolved, Failure::not_observed);
    const bool bank_clear=request.bank_available.current(frame.clock,request.bank_waypoint) && request.bank_available.value;
    int bank_lock_turns=0;
    if (!bank_clear) {
        if (request.bank_lock_hits.source!=Source::queried_setting ||
            request.bank_lock_hits.value<1 || request.bank_lock_hits.value>=work_cap || !own.can_attack)
            return fail(Issue::unknown_input,Status::unresolved,Failure::not_observed);
        bank_lock_turns=request.bank_lock_hits.value+1; // full lock plus personal inspection
    }
    const auto kit = request.kit.value;
    if (!number(request.ambient_incoming.value) ||
        (expected_groups > 0 && (!number(kit.strength) || !number(kit.scheduled_single_cast) ||
                                kit.strength != own.strength || kit.ranged != own.ranged)))
        return fail(Issue::malformed, Status::invalid, Failure::conflicting_evidence);

    // All personally visible live enemies are charged on every represented
    // turn, even when a photographed range would temporarily exclude them.
    // A full cast stress per turn is deliberately stronger than cooldown credit.
    double ambient = request.ambient_incoming.value;
    int enemies = 0;
    std::array<bool, 6> stress_used{};
    for (int a = 0; a < frame.actor_count; ++a) {
        const auto& observed = frame.actors[a];
        if (!observed.current(frame.clock, observed.subject) ||
            observed.value.team != Team::enemy || !observed.value.alive) continue;
        const auto& enemy = observed.value;
        if (!number(enemy.health) || !number(enemy.strength) || enemy.health <= 0)
            return fail(Issue::malformed, Status::invalid);
        int match = -1;
        for (int i = 0; i < request.enemy_cast_count; ++i)
            if (request.enemy_cast_stress[i].current(frame.clock, enemy.id)) {
                if (match >= 0) return fail(Issue::malformed, Status::invalid);
                match = i;
            }
        if (match < 0) return fail(Issue::unknown_input, Status::unresolved, Failure::not_observed);
        if (!number(request.enemy_cast_stress[match].value))
            return fail(Issue::malformed, Status::invalid);
        stress_used[match] = true;
        ambient += enemy.strength + request.enemy_cast_stress[match].value;
        ++enemies;
    }
    if (enemies != request.enemy_cast_count)
        return fail(Issue::malformed, Status::invalid, Failure::conflicting_evidence);
    for (int i = 0; i < request.enemy_cast_count; ++i)
        if (!stress_used[i]) return fail(Issue::malformed, Status::invalid);
    if (!number(ambient)) return fail(Issue::overflow, Status::invalid);

    probe(2);
    std::array<std::array<MonsterWork, 32>, 2> work{};
    std::array<int, 2> counts{};
    std::array<double, 2> source_overlap{};
    std::array<bool, 2> stationary_basic{};
    double total_overlap = 0;
    probe(3);
    for (int g = 0; g < request.group_count; ++g) {
        const auto& input = request.groups[g];
        const int id = input.observed.value.id;
        if (id < 0 || input.member_count < 1 || input.member_count > 32 ||
            !point_ok(input.observed.value.approach) || !point_ok(input.observed.value.spawn))
            return fail(Issue::malformed, Status::invalid);
        if (!input.observed.current(frame.clock, id) ||
            !input.observed.value.original_spawn_visible ||
            !input.membership_complete.known() ||
            !static_source(input.membership_complete.source) ||
            !input.membership_complete.value || !input.gems.known() || !input.gold.known() ||
            !static_source(input.gems.source) || !static_source(input.gold.source))
            return fail(Issue::unknown_input, Status::unresolved, Failure::not_observed);
        const auto& group = input.observed.value;
        if (group.lifecycle == Lifecycle::unknown)
            return fail(Issue::unknown_input, Status::unresolved, Failure::not_observed);
        if (group.lifecycle == Lifecycle::respawning || !group.uncontested_now)
            return fail(Issue::unavailable, Status::unresolved, Failure::observed_unavailable);
        if (group.lifecycle != Lifecycle::living && group.lifecycle != Lifecycle::reward_available)
            return fail(Issue::malformed, Status::invalid);
        if (input.gems.value < 0 || input.gold.value < 0)
            return fail(Issue::malformed, Status::invalid);
        for (int previous = 0; previous < g; ++previous)
            if (request.groups[previous].observed.value.id == id)
                return fail(Issue::unavailable); // No invented respawn timer for a repeated group.
        for (int a = 0; a < frame.actor_count; ++a) {
            const auto& observed = frame.actors[a];
            if (observed.current(frame.clock, observed.subject) && observed.value.alive &&
                observed.value.team == Team::ally && observed.value.spawn_group == id)
                return fail(Issue::unavailable, Status::unresolved, Failure::observed_unavailable);
        }
        counts[g] = input.member_count;
        bool living = false;
        bool exact_members = true;
        double living_strength = 0;
        for (int m = 0; m < input.member_count; ++m) {
            const auto& member = input.members[m];
            if (!member.public_full.known() || !static_source(member.public_full.source))
                return fail(Issue::unknown_input, Status::unresolved, Failure::not_observed);
            const auto full = member.public_full.value;
            if (full.id < 0 || !number(full.health) || full.health <= 0 ||
                !number(full.effective_strength)) return fail(Issue::malformed, Status::invalid);
            for (int previous_group = 0; previous_group <= g; ++previous_group) {
                const int end = previous_group == g ? m : counts[previous_group];
                for (int previous = 0; previous < end; ++previous)
                    if (request.groups[previous_group].members[previous].public_full.value.id == full.id)
                        return fail(Issue::malformed, Status::invalid);
            }
            auto value = full;
            exact_members &= member.current.current(frame.clock, full.id);
            if (member.current.current(frame.clock, full.id)) {
                value = member.current.value;
                if (value.id != full.id || !number(value.health) || !number(value.effective_strength))
                    return fail(Issue::malformed, Status::invalid, Failure::conflicting_evidence);
                if (group.lifecycle == Lifecycle::reward_available && value.health > 0)
                    return fail(Issue::malformed, Status::invalid, Failure::conflicting_evidence);
            }
            if (group.lifecycle == Lifecycle::reward_available) value.health = 0;
            living |= value.health > 0;
            if (value.health > 0) living_strength += value.effective_strength;
            work[g][m] = value;
        }
        stationary_basic[g] = input.stationary_basic.current(frame.clock, id) &&
            input.stationary_basic.value && own.ranged && own.can_attack &&
            own.position == group.approach && exact_members;
        if (!number(living_strength))
            return fail(Issue::overflow, Status::invalid);
        const bool attributed = input.ambient_overlap.knowledge != Knowledge::unknown;
        if (attributed) {
            if (!input.ambient_overlap.current(frame.clock, id))
                return fail(Issue::unknown_input, Status::unresolved, Failure::not_observed);
            const double overlap = input.ambient_overlap.value;
            if (!number(overlap) || overlap > living_strength)
                return fail(Issue::malformed, Status::invalid, Failure::conflicting_evidence);
            source_overlap[g] = overlap;
            total_overlap += overlap;
        }
        if (group.lifecycle == Lifecycle::living && !living)
            return fail(Issue::malformed, Status::invalid, Failure::conflicting_evidence);
        std::sort(work[g].begin(), work[g].begin() + counts[g], [](auto a, auto b) {
            return a.health == b.health ? a.id < b.id : a.health < b.health;
        });
    }
    // Attribution cannot subtract a visible enemy or an unrepresented source.
    // The producer's complete ambient observation must already contain it all.
    if (!number(total_overlap) || total_overlap > request.ambient_incoming.value)
        return fail(Issue::malformed, Status::invalid, Failure::conflicting_evidence);

    probe(4);
    double shield_remaining = 0;
    int shield_expiry = 0;
    if (request.shield.enabled) {
        if (!request.shield.amount.current(frame.clock, own.id) ||
            !request.shield.expires_before_tick.current(frame.clock, own.id))
            return fail(Issue::unknown_input, Status::unresolved, Failure::not_observed);
        if (!number(request.shield.amount.value) || request.shield.expires_before_tick.value < 0)
            return fail(Issue::malformed, Status::invalid);
        shield_remaining = request.shield.amount.value;
        shield_expiry = request.shield.expires_before_tick.value;
    }
    if (request.recovery.enabled) {
        out.recovery_health_unmodeled = true;
        if (request.recovery.waypoint < 0 || !point_ok(request.recovery.position) ||
            request.recovery.turns.value < 0 || request.recovery.turns.value > work_cap)
            return fail(Issue::malformed, Status::invalid);
        if (!request.recovery.turns.known())
            return fail(Issue::unknown_input, Status::unresolved, Failure::not_observed);
    } else if (request.recovery.requires_health_restore) {
        return fail(Issue::malformed, Status::invalid);
    }

    out.schedule.actor_count = 1;
    out.schedule.target_id = request.shape == Shape::bank_only ? request.bank_waypoint :
                             request.groups[0].observed.value.id;
    auto& actor = out.schedule.actors[0];
    actor.event_count = 0;
    actor.actor_id = own.id;
    actor.initial_bag = {own.bag, Source::self_observed};
    actor.initial_gold = {frame.own.gold, Source::self_observed};
    out.certificate.clock = frame.clock;
    out.certificate.actor = own.id;
    out.certificate.group = request.shape == Shape::bank_only ? request.project_group : out.schedule.target_id;
    out.certificate.solo = true;
    int tick = 0, leg_index = 0, event_id = 1;
    double cumulative = 0, absorbed = 0;
    Gems bag = own.bag, gold = frame.own.gold;
    Point position = own.position;
    std::array<double, 16> phase_rates{};
    Issue issue = Issue::none;
    Failure abort = Failure::none;
    // The starting actor was validated once above. These scalar inputs are
    // checked at each call site; preserve the canonical remaining-health order.
    const double starting_margin = .20 * own.max_health;
    auto survives_phase = [&](double incoming, double shield, double next) {
        const double remaining = own.health + shield + 0. - incoming;
        return remaining > 0 && remaining >= std::max(starting_margin, 2 * next);
    };
    auto snapshot = [&](double next) {
        Exposure exposure;
        exposure.health = own.health; exposure.max_health = own.max_health;
        exposure.incoming_total = cumulative; exposure.own_timed_shield = absorbed;
        exposure.next_phase_incoming = next; exposure.observations_complete = true;
        return exposure;
    };
    auto first = [&](FirstStage stage, int group, int waypoint, int event) {
        if (out.first.stage == FirstStage::none)
            out.first = {stage, own.id, group, waypoint, event};
    };
    auto start_phase = [&](PhaseKind kind, int group, int waypoint, int event, double rate) -> PhaseTerm* {
        if (out.phase_count >= static_cast<int>(out.phases.size())) {
            issue = Issue::overflow; return nullptr;
        }
        const int index = out.phase_count++;
        phase_rates[index] = rate;
        auto& phase = out.phases[index];
        phase.kind = kind; phase.actor = own.id; phase.group = group;
        phase.waypoint = waypoint; phase.event_id = event;
        phase.start = phase.finish = tick;
        phase.incoming = phase.shield_absorbed = phase.next_phase_incoming = 0;
        phase.cumulative_incoming = cumulative;
        phase.health_after = own.health + absorbed - cumulative;
        phase.source = Source::scheduled;
        return &phase;
    };
    auto advance = [&](PhaseTerm& phase, int turns, double rate, double next) {
        if (turns < 0 || turns > work_cap || turns > horizon - tick) {
            issue = Issue::horizon; abort = Failure::expired; return false;
        }
        const double incoming = turns * rate;
        if (!number(rate) || !number(next) || !number(incoming) || !number(cumulative + incoming)) {
            issue = Issue::overflow; return false;
        }
        // Damage is placed at each subsequent tick. Strictly-before expiry
        // avoids granting a camp-phase shield to later champion damage there.
        const int covered = std::min(turns, std::max(0, shield_expiry - tick - 1));
        const double saved = std::min(shield_remaining, covered * rate);
        shield_remaining -= saved; absorbed += saved; cumulative += incoming;
        tick += turns;
        phase.finish = tick; phase.incoming += incoming; phase.shield_absorbed += saved;
        phase.cumulative_incoming = cumulative; phase.health_after = own.health + absorbed - cumulative;
        phase.next_phase_incoming = next;
        if (!number(absorbed) || !number(phase.incoming) || !number(phase.shield_absorbed) ||
            !number(own.health + absorbed)) {
            issue = Issue::overflow; return false;
        }
        if (!survives_phase(cumulative, absorbed, next)) {
            issue = Issue::unsafe; abort = Failure::unsafe; return false;
        }
        return true;
    };
    auto phase = [&](PhaseKind kind, int group, int waypoint, int event,
                     int turns, double rate, FirstStage stage) {
        auto* term = start_phase(kind, group, waypoint, event, rate);
        if (!term) return false;
        if (turns > 0) first(stage, group, waypoint, event);
        return advance(*term, turns, rate, ambient);
    };
    auto travel = [&](Point destination, int group, int waypoint) {
        if (leg_index >= request.leg_count) { issue = Issue::unknown_input; abort = Failure::not_observed; return false; }
        const auto& leg = request.legs[leg_index++];
        if (!(leg.from == position) || !(leg.to == destination)) {
            issue = Issue::malformed; abort = Failure::conflicting_evidence; return false;
        }
        if (!leg.distance.known() || !static_source(leg.distance.source)) {
            issue = Issue::unknown_input; abort = Failure::not_observed; return false;
        }
        const int distance = leg.distance.value;
        const auto dx = std::abs(std::int64_t(position.x) - destination.x);
        const auto dy = std::abs(std::int64_t(position.y) - destination.y);
        if (distance < 0 || distance > 256 || distance < std::max(dx, dy) ||
            ((position == destination) != (distance == 0))) {
            issue = Issue::malformed; return false;
        }
        if (distance > 0 && !own.can_move) {
            issue = Issue::unavailable; abort = Failure::observed_unavailable; return false;
        }
        if (!phase(PhaseKind::travel, group, waypoint, -1, distance ? distance + 2 : 0,
                   ambient, FirstStage::travel)) return false;
        position = destination;
        return true;
    };
    auto event = [&](EventKind kind, int id, int production = -1, int waypoint = -1) {
        if (actor.event_count >= max_actor_events) { issue = Issue::overflow; return false; }
        auto& added = actor.events[actor.event_count++];
        added.kind = kind; added.event_id = id; added.tick = tick;
        added.production_index = production; added.waypoint_id = waypoint;
        added.gold_cost = {};
        added.source = Source::scheduled; return true;
    };
    auto bank = [&](bool after_production = false) {
        if (!travel(request.bank_position, -1, request.bank_waypoint)) return false;
        // A modeled gold-only payout leaves nothing to tribute. Retain the
        // named route and cumulative exposure without inventing a bank event.
        // Initial empty bank-only/bank-first requests remain unavailable.
        if (after_production && bag == 0) {
            out.certificate.bank = snapshot(ambient);
            return true;
        }
        if (bank_lock_turns && !phase(PhaseKind::clear,-1,request.bank_waypoint,
                event_id++,bank_lock_turns,ambient,FirstStage::clear)) return false;
        if (bag <= 0) { issue = Issue::unavailable; abort = Failure::observed_unavailable; return false; }
        if (bag > exact_gem_cap) { issue = Issue::overflow; return false; }
        const double scaled = std::ceil(static_cast<double>(bag) * timing.tribute_multiplier.value);
        if (!number(scaled) || scaled > work_cap) { issue = Issue::overflow; return false; }
        const auto duration = std::max<std::int64_t>(timing.tribute_minimum.value,
            std::int64_t(timing.tribute_base.value) + static_cast<std::int64_t>(scaled));
        if (duration < 1 || duration > work_cap) { issue = Issue::overflow; return false; }
        const int id = event_id++;
        // D owner turn-end decrements complete at owner-start start+D. Adding
        // another tick here would incorrectly delay a one-tick channel twice.
        if (!phase(PhaseKind::tribute, -1, request.bank_waypoint, id,
                   static_cast<int>(duration), ambient, FirstStage::tribute) ||
            !event(EventKind::tribute, id)) return false;
        bag = 0;
        out.certificate.bank = snapshot(ambient);
        return true;
    };
    auto produce = [&](int index) {
        const auto& input = request.groups[index];
        const auto& group = input.observed.value;
        if (!travel(group.approach, group.id, group.id)) return false;
        const int id = event_id++;
        double living_strength = 0;
        bool has_target = false;
        for (int m = 0; m < counts[index]; ++m)
            if (work[index][m].health > 0) {
                living_strength += work[index][m].effective_strength; has_target = true;
            }
        if (!number(living_strength) || !number(ambient + living_strength)) { issue = Issue::overflow; return false; }
        if (has_target && !own.can_attack) { issue = Issue::unavailable; abort = Failure::observed_unavailable; return false; }
        // A current mask proves stationary basic availability only for work
        // starting now. It does not promise readiness after travel, a previous
        // production, or a channel, even if a later route returns to this tile.
        const bool stationary = tick == 0 && stationary_basic[index];
        double rate = kit.strength * (kit.ranged && !stationary ? .5 : 1.);
        int first_target = -1;
        for (int m = 0; m < counts[index]; ++m)
            if (work[index][m].health > 0) { first_target = work[index][m].id; break; }
        bool target_current = false;
        for (int m = 0; m < counts[index]; ++m)
            if (input.members[m].public_full.value.id == first_target &&
                input.members[m].current.current(frame.clock, first_target)) target_current = true;
        // Current readiness is not a promise that a cast will be legal after
        // travel. Only an immediately represented, personally inspected target
        // receives the one A/8 contribution; later groups use basic attacks.
        if (!stationary && tick == 0 && target_current && own.ready_attack_group == group.id && kit.cast_ready &&
            kit.cast_legal_at_first_turn && !kit.cast_reserved_for_protection)
            rate += kit.scheduled_single_cast / 8.;
        if (!number(rate)) { issue = Issue::overflow; return false; }
        // Approach kept this already-awake threat. Clearance below represents
        // every living member, so its attributed ambient term is counted once.
        ambient -= source_overlap[index];
        auto* term = start_phase(PhaseKind::clear, group.id, group.id, id, ambient + living_strength);
        if (!term) return false;
        if (has_target) first(FirstStage::clear, group.id, group.id, id);
        bool previous_target = false;
        for (int m = 0; m < counts[index]; ++m) {
            const auto& target = work[index][m];
            if (target.health <= 0) continue;
            if (previous_target && !advance(*term, 2, ambient + living_strength,
                                           ambient + living_strength)) return false;
            const double turns = std::ceil(target.health / std::max(1., rate));
            if (!number(turns) || turns > work_cap) { issue = Issue::overflow; return false; }
            const double next = ambient + std::max(0., living_strength - target.effective_strength);
            if (!advance(*term, static_cast<int>(turns), ambient + living_strength, next)) return false;
            living_strength = std::max(0., living_strength - target.effective_strength);
            previous_target = true;
        }
        if (!has_target && !advance(*term, 0, ambient, ambient)) return false;
        // This program does not strike the cleared source again. Respawn alone
        // does not wake its fresh life, so the dead life's awake ambient term
        // cannot persist into banking or renewal. Third-party damage can change
        // that condition; execution must observe again before further actions.
        out.certificate.clear = snapshot(ambient);
        if (!travel(group.spawn, group.id, group.id)) return false;
        // There is no observable capture countdown. The full setting plus the
        // initializing capture phase is forecast afresh, never treated as payout.
        if (!phase(PhaseKind::hold, group.id, group.id, id, settings.capture_rounds + 1,
                   ambient, FirstStage::hold)) return false;
        out.certificate.hold = snapshot(ambient);
        if (!p6::add(bag, input.gems.value, bag) || !p6::add(gold, input.gold.value, gold)) {
            issue = Issue::overflow; return false;
        }
        const int production = out.schedule.production_count++;
        auto& payout = out.schedule.productions[production];
        payout.event_id = id; payout.tick = tick;
        payout.gems = {input.gems.value, Source::scheduled};
        payout.gold = {input.gold.value, Source::scheduled};
        payout.holders[0] = own.id; payout.holder_count = 1; payout.source = Source::scheduled;
        return event(EventKind::production, id, production);
    };

    probe(5);
    if (request.shape == Shape::bank_only) {
        out.certificate.clear = out.certificate.hold = snapshot(ambient);
        if (!bank()) return fail(issue, Status::unresolved, abort);
    } else if (request.shape == Shape::bank_then_produce_bank && !bank()) {
        return fail(issue, Status::unresolved, abort);
    }
    for (int g = 0; g < request.group_count; ++g) {
        if (!produce(g)) return fail(issue, Status::unresolved, abort);
        if ((request.shape != Shape::retain_two_then_bank || g + 1 == request.group_count) && !bank(true))
            return fail(issue, Status::unresolved, abort);
    }
    if (request.recovery.enabled) {
        const auto& recovery = request.recovery;
        if (!travel(recovery.position, -1, recovery.waypoint)) return fail(issue, Status::unresolved, abort);
        const int id = event_id++;
        if (!phase(PhaseKind::recovery, -1, recovery.waypoint, id, recovery.turns.value,
                   ambient, FirstStage::recovery) || !event(EventKind::recovery, id))
            return fail(issue, Status::unresolved, abort);
    }
    if (!travel(request.renewal_position, -1, request.renewal_waypoint)) return fail(issue, Status::unresolved, abort);
    const int renewal = event_id++;
    if (!event(EventKind::renewal, renewal, -1, request.renewal_waypoint)) return fail(issue, Status::unresolved, abort);
    if (leg_index != request.leg_count) return fail(Issue::malformed, Status::invalid);
    if (out.first.stage == FirstStage::none) first(FirstStage::renewal, -1, request.renewal_waypoint, renewal);
    actor.occupied_turns = {tick, Source::scheduled};
    out.certificate.recovery = snapshot(ambient);
    out.certificate.route_known = true;
    out.certificate.bank_executable = true;
    out.certificate.renewal_reachable = true;
    probe(6);
    // Recheck each aggregate milestone against the actual next phase's maximum
    // visible incoming bound. Internal target/reposition boundaries were checked
    // separately above, so aggregation cannot hide a dangerous early segment.
    for (int i = 0; i < out.phase_count; ++i) {
        auto& term = out.phases[i];
        term.next_phase_incoming = i + 1 < out.phase_count ? phase_rates[i + 1] : ambient;
        double saved = 0;
        for (int j = 0; j <= i; ++j) saved += out.phases[j].shield_absorbed;
        if (!number(term.cumulative_incoming) || !number(saved) || !number(term.next_phase_incoming) ||
            !survives_phase(term.cumulative_incoming, saved, term.next_phase_incoming))
            return fail(Issue::unsafe, Status::unresolved, Failure::unsafe);
    }
    probe(7);
    if (!out.certificate.valid(frame, own.id, out.certificate.group))
        return fail(Issue::unsafe, Status::unresolved, Failure::unsafe);
    if (request.recovery.enabled && request.recovery.requires_health_restore)
        return fail(Issue::health_restore_unmodeled);
    out.status = Status::feasible; out.issue = Issue::none;
}
}
namespace p8::native { namespace forecast_probe = p6::forecast::plan8_probe; }
