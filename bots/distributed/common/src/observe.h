#pragma once
#include "contract.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#ifndef P6_GEM_MODEL_PACKET
#define P6_GEM_MODEL_PACKET 0
#endif
#if P6_GEM_MODEL_PACKET
#include <bit>
#endif

namespace p6::runtime { template<class Policy> class Bot; }

// Pure personal-observation boundary. No engine, replay or shared-memory reader.
namespace p6 {
struct Point { int x = 0, y = 0; bool operator==(const Point&) const = default; };
struct ViewClock {
    int round = 0, self = -1, revision = 0;
    bool operator==(const ViewClock&) const = default;
};
enum class Knowledge { unknown, exact, stale };
template<class T> struct Observation {
    T value{};
    ViewClock clock{};
    int subject = -1;
    Knowledge knowledge = Knowledge::unknown;
    bool operator==(const Observation&) const = default;
    bool current(ViewClock now, int who) const {
        return knowledge == Knowledge::exact && clock == now && subject == who;
    }
};
enum class Champion { marcella, tavi, varg, vendetta, willow, prism, ren,
                      cassi, issac, vesper_vale, ryth, stellar, heart_of_ruin, other };
enum class Channel { none, tribute, recall, teleport };
enum class Team { unknown, ally, enemy };
enum class Lifecycle { unknown, living, reward_available, respawning };
enum class Failure { none, not_observed, missed_appointment, observed_unavailable,
                     observed_failed_action, conflicting_evidence, unsafe, expired };
enum class WitnessAction { none, useful_attack, occupied_spawn };
struct Actor {
    int id = -1, pick_ordinal = -1;
    Champion type = Champion::other;
    Point position{};
    double health = 0, max_health = 0, strength = 0;
    bool alive = false, can_move = false, can_attack = false, ranged = false;
    Channel channel = Channel::none;
    bool channel_valid = false;
    std::int64_t bag = 0;
    // Gold deliberately lives only in Self, never in a visible ally record.
    WitnessAction action = WitnessAction::none;
    int action_group = -1, spawn_group = -1;
    // Current personally inspected legal in-range camp target; capability does
    // not promise that this actor will attack on a later turn.
    int ready_attack_group = -1;
    Team team = Team::unknown;
    int attack_range = -1; // Queried visible-subject value; unknown is not a default range.
    int move_speed = -1;
    bool completed_turn = false; // Current-round fact; never future action readiness.
    bool operator==(const Actor&) const = default;
};
struct Self {
    Actor actor{};
    std::int64_t gold = 0;
    bool at_spawn = false, spawn_recovery_observed = false;
    bool operator==(const Self&) const = default;
};
struct Group {
    int id = -1;
    Point approach{}, spawn{}, retreat{};
    Lifecycle lifecycle = Lifecycle::unknown;
    bool original_spawn_visible = false, all_spawns_visible = false;
    bool uncontested_now = false;
    // There is intentionally no capture timer or claiming-team field.
};
struct Frame {
    ViewClock clock{};
    Self own{};
    std::int64_t own_team_score = 0; // A private timed query, never a common snapshot.
    std::array<Observation<Actor>, 6> actors{};
    int actor_count = 0;
    Observation<Group> group{};
    bool valid() const {
        if (clock.self != own.actor.id || own.actor.team != Team::ally ||
            clock.self < 0 || clock.round < 0 || clock.revision < 0 ||
            actor_count < 0 || actor_count > 6) return false;
        std::array<int, 6> current_ids;
        int current_count = 0;
        for (int i = 0; i < actor_count; ++i) {
            const auto& seen = actors[i];
            if (seen.knowledge != Knowledge::exact || !(seen.clock == clock)) continue;
            if (seen.subject < 0 || seen.subject == clock.self || seen.value.id != seen.subject)
                return false;
            // Clock and knowledge were checked once when each ID was added.
            // Stale/unknown entries do not participate in current duplicates.
            for (int j = 0; j < current_count; ++j)
                if (current_ids[j] == seen.subject) return false;
            current_ids[current_count++] = seen.subject;
        }
        return true;
    }
    const Actor* visible(int id) const {
        if (!valid()) return nullptr;
        if (id == clock.self) return &own.actor;
        for (int i = 0; i < actor_count; ++i)
            if (actors[i].current(clock, id) && actors[i].value.id == id)
                return &actors[i].value;
        return nullptr;
    }
    const Group* live_group(int id) const {
        return valid() && group.current(clock, id) && group.value.id == id &&
               group.value.original_spawn_visible ? &group.value : nullptr;
    }
};
inline Failure participant_failure(const Frame& f, int id) {
    const auto* actor = f.visible(id);
    if (!actor) return Failure::not_observed;
    if (actor->team != Team::ally) return Failure::conflicting_evidence;
    if (!actor->alive || actor->channel != Channel::none)
        return Failure::observed_unavailable;
    return Failure::none;
}
inline std::optional<std::int64_t> current_bag(const Frame& f, int id) {
    const auto* a = f.visible(id);
    if (!a) return std::nullopt;
    return a->bag;
}
inline std::optional<std::int64_t> personal_gold(const Frame& f, int id) {
    if (!f.valid() || id != f.clock.self) return std::nullopt;
    return f.own.gold;
}
inline bool linked_role(const Actor& a, int pick, Champion subtype) {
    return a.pick_ordinal == pick && a.type == subtype;
}

// One conservative per-actor exposure bound; a role cannot assign aggro away.
struct Exposure {
    double health = 0, max_health = 0, incoming_total = 0;
    double next_phase_incoming = 0, own_timed_shield = 0, assigned_timed_heal = 0;
    bool observations_complete = false;
};
inline bool survives(const Exposure& e) {
    const std::array values{e.health, e.max_health, e.incoming_total,
        e.next_phase_incoming, e.own_timed_shield, e.assigned_timed_heal};
    for (double value : values) if (!std::isfinite(value) || value < 0) return false;
    if (!e.observations_complete || e.max_health <= 0 || e.health <= 0) return false;
    const double remaining = e.health + e.own_timed_shield +
                             e.assigned_timed_heal - e.incoming_total;
    return remaining > 0 && remaining >= std::max(.20 * e.max_health,
                                                  2 * e.next_phase_incoming);
}
struct MonsterWork { int id = -1; double health = 0, effective_strength = 0; };
struct Productivity {
    double strength = 0, scheduled_single_cast = 0;
    bool ranged = false, cast_ready = false, cast_legal_at_first_turn = false;
    bool cast_reserved_for_protection = false;
};
struct WorkEstimate { int rounds = 0; double incoming = 0; bool resolved = false; };
inline WorkEstimate combat_work(std::span<const MonsterWork> targets,
                               std::span<const Productivity> participants) {
    // A finite semantic fixture cap. A larger group is unresolved, never truncated.
    if (targets.empty() || targets.size() > 32 || participants.empty() ||
        participants.size() > 6) return {};
    double rate = 0, incoming = 0;
    for (const auto& p : participants) {
        if (!std::isfinite(p.strength) || !std::isfinite(p.scheduled_single_cast) ||
            p.strength < 0 || p.scheduled_single_cast < 0) return {};
        rate += p.strength * (p.ranged ? .5 : 1.);
        if (p.cast_ready && p.cast_legal_at_first_turn && !p.cast_reserved_for_protection)
            rate += p.scheduled_single_cast / 8.;
    }
    std::array<MonsterWork, 32> ordered{};
    std::copy(targets.begin(), targets.end(), ordered.begin());
    for (const auto& m : targets) {
        if (!std::isfinite(m.health) || !std::isfinite(m.effective_strength) ||
            m.health < 0 || m.effective_strength < 0) return {};
        incoming += m.health > 0 ? m.effective_strength : 0;
    }
    std::sort(ordered.begin(), ordered.begin() + targets.size(), [](auto a, auto b) {
        return a.health == b.health ? a.id < b.id : a.health < b.health;
    });
    WorkEstimate result{0, 0, true};
    bool first = true;
    for (std::size_t i = 0; i < targets.size(); ++i) {
        const auto& m = ordered[i];
        if (m.health <= 0) continue;
        if (!first) { result.rounds += 2; result.incoming += 2 * incoming; }
        const double rounds = std::ceil(m.health / std::max(1., rate));
        if (!std::isfinite(rounds) || rounds > 1000000 || result.rounds > 1000000)
            return {};
        result.rounds += static_cast<int>(rounds);
        result.incoming += rounds * incoming;
        incoming -= m.effective_strength;
        first = false;
    }
    return result;
}
struct CycleCertificate {
    ViewClock clock{};
    int actor = -1, group = -1;
    Exposure clear{}, hold{}, bank{}, recovery{};
    bool route_known = false, bank_executable = false, renewal_reachable = false;
    bool solo = false;
    bool valid(const Frame& f, int who, int target) const {
        const auto* subject = f.visible(who);
        if (!f.valid() || !(clock == f.clock) || actor != who || group != target ||
            !subject || subject->team != Team::ally || !subject->alive ||
            !route_known || !bank_executable || !renewal_reachable)
            return false;
        // Each exposure is cumulative from this current observed starting state
        // through that milestone. It is not a refill at every phase boundary.
        double previous_incoming = 0;
        for (const auto& exposure : {clear, hold, bank, recovery}) {
            if (exposure.health != subject->health || exposure.max_health != subject->max_health ||
                (solo && exposure.assigned_timed_heal != 0) ||
                exposure.incoming_total < previous_incoming || !survives(exposure)) return false;
            previous_incoming = exposure.incoming_total;
        }
        return true;
    }
};
inline bool witnessed_release(const Frame& f, int owner, int group,
                             const CycleCertificate& remaining) {
    const auto* a = f.visible(owner);
    const auto* g = f.live_group(group);
    return a && a->alive && a->channel == Channel::none &&
           a->spawn_group == group && g && g->lifecycle == Lifecycle::reward_available &&
           g->uncontested_now && remaining.solo && remaining.valid(f, owner, group);
}
} // namespace p6

// Forecasts contain scheduled events, never action permission or payout receipts.
namespace p6::forecast {
enum class Shape { produce_bank, bank_then_produce_bank, bank_only,
                   produce_bank_produce_bank, retain_two_then_bank };
enum class FirstStage { none, travel, clear, hold, tribute, recovery, renewal };
enum class PhaseKind { travel, clear, hold, tribute, recovery };
enum class Issue { none, malformed, unknown_input, unavailable, unsafe,
                   overflow, horizon, unsupported_purchase, health_restore_unmodeled };
struct Timing {
    Fact<Settings> settings{};
    Fact<double> tribute_multiplier{};
    Fact<int> tribute_base{}, tribute_minimum{};
};
struct Member {
    Fact<MonsterWork> public_full{}; // Complete original membership, public full HP/Strength.
    Observation<MonsterWork> current{}; // Prefer current HP; otherwise use public full HP.
};
struct GroupInput {
    Observation<Group> observed{};
    Fact<bool> membership_complete{};
    std::array<Member, 32> members{};
    int member_count = 0;
    Fact<Gems> gems{}, gold{};
    // Optional personal attribution of strength already included in ambient.
    // The observation must be current for this group. Omission retains the
    // original all-phase bound; it never means that external stress is absent.
    Observation<double> ambient_overlap{};
    // Optional personal SDK witness: every original member has an exact current
    // observation, and every living member is in this actor's same current basic
    // attack mask from observed.approach. The adapter publishes this only before
    // any action. Omission/false/stale evidence retains the old ranged rate.
    Observation<bool> stationary_basic{};
};
struct Leg { Point from{}, to{}; Fact<int> distance{}; };
struct Shield {
    bool enabled = false;
    Observation<double> amount{};
    // Conservative relative owner-start expiry bound. Boundary damage gets no
    // shield credit, because camp and champion phases straddle owner-start.
    Observation<int> expires_before_tick{};
};
struct Recovery {
    bool enabled = false, requires_health_restore = false;
    int waypoint = -1;
    Point position{};
    Fact<int> turns{}; // Wait only: this increment never manufactures healing.
};
struct Request {
    Shape shape = Shape::produce_bank;
    int project_group = -1; // Existing project identity for bank_only; no source claim.
    Timing timing{};
    std::array<GroupInput, 2> groups{};
    int group_count = 0;
    Observation<Productivity> kit{};
    // Current complete personal bound for other awake monsters. Explicit zero
    // is required; absent observation is unknown. Overlap with a modeled group
    // is safe but pessimistic; do not omit an already-awake visible threat.
    Observation<double> ambient_incoming{};
    // One maximum damaging-cast stress bound per visible enemy; unknown
    // cooldowns use the full bound, never beneficial readiness assumptions.
    std::array<Observation<double>, 6> enemy_cast_stress{};
    int enemy_cast_count = 0;
    int bank_waypoint = -1, renewal_waypoint = -1;
    Point bank_position{}, renewal_position{};
    Observation<bool> bank_available{};
    // A future/unseen altar is not observed unlocked. With this queried bound,
    // model inspection plus clearing a full hostile lock before tribute. The
    // executor must still inspect and attack the actual visible lock.
    Fact<int> bank_lock_hits{};
    // Exact ordered legs: to each approach, approach->spawn, each bank, renewal.
    // Recovery adds bank->recovery and recovery->renewal instead of final renewal.
    // Every positive distance receives two slack turns; zero requires same point.
    std::array<Leg, 8> legs{};
    int leg_count = 0;
    Shield shield{};
    Recovery recovery{};
    bool purchase_requested = false;
};
struct PhaseTerm {
    PhaseKind kind = PhaseKind::travel;
    int actor = -1, group = -1, waypoint = -1, event_id = -1;
    int start = 0, finish = 0;
    double incoming = 0, shield_absorbed = 0, cumulative_incoming = 0;
    double health_after = 0, next_phase_incoming = 0;
    Source source = Source::scheduled;
};
struct Proposal {
    FirstStage stage = FirstStage::none;
    int actor = -1, group = -1, waypoint = -1, event_id = -1;
};
struct Result {
    Status status = Status::unresolved;
    Issue issue = Issue::unknown_input;
    Failure abort = Failure::none;
    Schedule schedule{};
    CycleCertificate certificate{};
    std::array<PhaseTerm, 16> phases{};
    int phase_count = 0;
    Proposal first{};
    bool recovery_health_unmodeled = false, purchases_deferred = true;
};
#if P6_GEM_MODEL_PACKET
// Diagnostic storage only: exact original-clock failure facts, never authority.
struct GemFailurePacket {
    bool pending=false;
    std::array<std::int64_t,48> fields{};
};
inline GemFailurePacket gem_failure_packet;
#endif
inline void build_into(const Frame& frame, const Request& request, Result& out) {
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
    // The bounds are semantic/fuel guards, not game settings. Larger inputs are
    // rejected, never truncated or assigned a zero duration.
    constexpr int work_cap = 1000000;
    constexpr Gems exact_gem_cap = 9007199254740991LL;
    auto fail = [&](Issue issue, Status status = Status::unresolved,
                    Failure abort = Failure::none) {
#if P6_GEM_MODEL_PACKET
        if(!gem_failure_packet.pending && issue==Issue::unsafe && request.group_count==1 &&
           ((frame.clock.round==256 && frame.clock.self==2 && request.groups[0].observed.value.id==11) ||
            (frame.clock.round==752 && frame.clock.self==5 && request.groups[0].observed.value.id==10))) {
            const auto bits=[](double value){return std::bit_cast<std::int64_t>(value);};
            const auto& own=frame.own.actor;const auto& group=request.groups[0];
            const PhaseTerm empty{};
            const auto& phase=out.phase_count>0?out.phases[out.phase_count-1]:empty;
            double health=0,strength=0;
            for(int i=0;i<group.member_count && i<32;++i) {
                const auto& member=group.members[i];
                const auto value=member.current.current(frame.clock,member.public_full.value.id)?
                    member.current.value:member.public_full.value;
                if(value.health>0) {health+=value.health;strength+=value.effective_strength;}
            }
            auto& packet=gem_failure_packet.fields;
            packet={frame.clock.round,frame.clock.self,group.observed.value.id,
                static_cast<int>(issue),static_cast<int>(status),static_cast<int>(abort),out.phase_count,
                out.phase_count?static_cast<int>(phase.kind):-1,phase.group,phase.start,phase.finish,
                bits(phase.health_after),bits(phase.incoming),bits(phase.cumulative_incoming),
                bits(phase.next_phase_incoming),bits(phase.shield_absorbed),
                bits(own.health),bits(own.max_health),bits(own.strength),own.ranged,own.can_move,own.can_attack,
                bits(request.kit.value.strength),bits(request.ambient_incoming.value),request.enemy_cast_count,
                frame.actor_count,request.bank_available.current(frame.clock,request.bank_waypoint),request.bank_available.value,
                request.leg_count,-1,-1,-1,-1,-1,-1,-1,-1,
                bits(health),bits(strength),group.member_count,group.observed.value.all_spawns_visible,
                own.position.x,own.position.y,group.observed.value.approach.x,group.observed.value.approach.y,
                own.bag,frame.own.gold,-1};
            for(int i=0;i<request.leg_count && i<8;++i)packet[29+i]=request.legs[i].distance.value;
            gem_failure_packet.pending=true;
        }
#endif
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

    std::array<std::array<MonsterWork, 32>, 2> work{};
    std::array<int, 2> counts{};
    std::array<double, 2> source_overlap{};
    std::array<bool, 2> stationary_basic{};
    double total_overlap = 0;
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
        if (!survives(snapshot(next))) {
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
    // Recheck each aggregate milestone against the actual next phase's maximum
    // visible incoming bound. Internal target/reposition boundaries were checked
    // separately above, so aggregation cannot hide a dangerous early segment.
    for (int i = 0; i < out.phase_count; ++i) {
        auto& term = out.phases[i];
        term.next_phase_incoming = i + 1 < out.phase_count ? phase_rates[i + 1] : ambient;
        Exposure exposure;
        exposure.health = own.health; exposure.max_health = own.max_health;
        exposure.incoming_total = term.cumulative_incoming;
        double saved = 0;
        for (int j = 0; j <= i; ++j) saved += out.phases[j].shield_absorbed;
        exposure.own_timed_shield = saved;
        exposure.next_phase_incoming = term.next_phase_incoming;
        exposure.observations_complete = true;
        if (!survives(exposure)) return fail(Issue::unsafe, Status::unresolved, Failure::unsafe);
    }
    if (!out.certificate.valid(frame, own.id, out.certificate.group))
        return fail(Issue::unsafe, Status::unresolved, Failure::unsafe);
    if (request.recovery.enabled && request.recovery.requires_health_restore)
        return fail(Issue::health_restore_unmodeled);
    out.status = Status::feasible; out.issue = Issue::none;
}
inline Result build(const Frame& frame, const Request& request) {
    Result out;
    build_into(frame, request, out);
    return out;
}

// A quote owns the exact request that produced its result. Runtime adapters may
// borrow const views, but cannot attach an unrelated result or mutate a ready
// quote's inputs without evaluating again.
class EvaluatedQuote {
    template<class Policy> friend class p6::runtime::Bot;
    bool ready_ = false;
    Frame frame_{};
    Request request_{};
    Result result_{};
    // Only the runtime adapter may fill owned request storage in place. Public
    // callers retain the copying evaluate boundary and cannot retain this ref.
    template<class FillRequest>
    void evaluate_into(const Frame& frame, FillRequest fill) {
        ready_ = false;
        frame_ = frame;
        fill(request_);
        build_into(frame_, request_, result_);
        ready_ = true;
    }
public:
    // User-provided to avoid redundant whole-object value-initialization in
    // make_unique; member initializers still establish every validity gate.
    EvaluatedQuote() {}
    void evaluate(const Frame& frame, const Request& request) {
        ready_ = false;
        frame_ = frame;
        request_ = request;
        build_into(frame_, request_, result_);
        ready_ = true;
    }
    bool ready() const { return ready_; }
    const Request& request() const { return request_; }
    const Result& result() const { return result_; }
    bool matches_frame(const Frame& frame) const {
        if (!ready_ || !(frame_.clock == frame.clock) || !(frame_.own == frame.own) ||
            frame_.own_team_score != frame.own_team_score ||
            frame_.actor_count != frame.actor_count || frame.actor_count < 0 ||
            frame.actor_count > static_cast<int>(frame.actors.size())) return false;
        for (int i = 0; i < frame.actor_count; ++i)
            if (!(frame_.actors[i] == frame.actors[i])) return false;
        // The builder reads request-owned group inputs and active actors only.
        // frame.group and the inactive actor tail have no forecast semantics.
        return true;
    }
};
} // namespace p6::forecast
