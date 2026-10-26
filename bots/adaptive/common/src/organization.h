#pragma once
#include "../../distributed-common/src/observe.h"
#include "route_hash_memo.h"
#include <array>
#include <cstdint>
#include <limits>
#include <span>

#ifndef P8_ORDINARY_COMPLETE_CYCLE
#define P8_ORDINARY_COMPLETE_CYCLE 0
#endif

// Pure, personally observed physical execution. There is deliberately no SDK
// adapter here. observe.h's clocks, cumulative individual survival certificate,
// and witnessed_release were audited against P5/P6; its executor is not reused
// because it does not retain the selected tile route.
namespace p8 {
namespace canary { struct CurrentAdmission; }
enum class Organization { U, G, H, J };
enum class Leg { ingress, capture, bank, renewal, release };
enum class PhysicalStage { idle, travel, assemble, clear, hold, payout, bank,
                           renew, recover, complete };
enum class PhysicalAction { yield, inspect, move, attack, hold, tribute, recall,
                            preserve_channel, recover, escape };
enum class PhysicalFailure { none, unavailable, missing_member, unsafe, stale,
                             fuel, blocked_route, invalid_route, timeout,
                             unconfirmed_endpoint, dead };
inline constexpr int max_route_tiles = 128;
struct Route {
    std::array<p6::Point, max_route_tiles> tiles{};
    int count = 0;
    std::uint64_t blocker_assumptions = 0;
    bool verified = false;
};
inline bool route_valid_uncached(const Route& r) {
    if (!r.verified || r.count <= 0 || r.count > max_route_tiles) return false;
    for (int i = 0; i < r.count; ++i) {
        if (r.tiles[i].x < 0 || r.tiles[i].y < 0) return false;
        if (i) {
            const int dx = r.tiles[i].x - r.tiles[i-1].x;
            const int dy = r.tiles[i].y - r.tiles[i-1].y;
            if (dx < -1 || dx > 1 || dy < -1 || dy > 1 || (!dx && !dy)) return false;
        }
    }
    return true;
}
inline bool route_valid(const Route& r) {
    // The bool callback gives this guest-local table a distinct type from the
    // FNV table. Exact owned geometry preserves the original validity result.
    static ExactRouteHashMemo<Route,max_route_tiles,route_valid_uncached> memo;
    return memo.hash(r)!=0;
}
inline std::uint64_t hash_word(std::uint64_t h, std::uint64_t word) {
    for (int i = 0; i < 8; ++i) { h ^= (word >> (8*i)) & 255; h *= 1099511628211ULL; }
    return h;
}
inline std::uint64_t route_hash_uncached(const Route& r) {
    if (!route_valid_uncached(r)) return 0;
    std::uint64_t h = hash_word(1469598103934665603ULL, r.count);
    h = hash_word(h, r.blocker_assumptions);
    for (int i = 0; i < r.count; ++i) {
        h = hash_word(h, static_cast<std::uint32_t>(r.tiles[i].x));
        h = hash_word(h, static_cast<std::uint32_t>(r.tiles[i].y));
    }
    return h;
}
inline std::uint64_t route_hash(const Route& r) {
    // Each WASI guest is single-threaded. Only exact geometry/hash data lives
    // here; every hit compares the entire owned active payload. This never
    // supplies a route's current legality, observation or execution permission.
    static ExactRouteHashMemo<Route,max_route_tiles,route_hash_uncached> memo;
    return memo.hash(r);
}
struct Plan {
    Organization organization = Organization::U;
    int group = -1, collector = -1, altar = -1;
    std::array<int, 6> crew{-1,-1,-1,-1,-1,-1};
    std::array<p6::Point, 6> pads{};
    int crew_count = 0;
    Route ingress{}, capture{}, bank{}, renewal{}, release{};
    int forecast_rounds = 0, remaining_rounds = 0, capture_window = 0;
    bool gem_bearing = false, complete_verified = false, rally_legal = false;
};
inline const Route& route_for(const Plan& p, Leg leg) {
    switch (leg) {
    case Leg::ingress: return p.ingress;
    case Leg::capture: return p.capture;
    case Leg::bank: return p.bank;
    case Leg::renewal: return p.renewal;
    default: return p.release;
    }
}
inline std::uint64_t plan_route_hash(const Plan& p) {
    // Hash payloads, never an arbitrary caller-supplied route identifier. The
    // return and productive renewal legs are part of the context identity.
    std::uint64_t h = hash_word(1469598103934665603ULL, p.group);
    for (Leg leg : {Leg::ingress, Leg::capture, Leg::bank, Leg::renewal}) {
        const auto part = route_hash(route_for(p, leg));
        if (!part) return 0;
        h = hash_word(h, part);
    }
    if (p.release.count) {
        const auto part = route_hash(p.release);
        if (!part) return 0;
        h = hash_word(h, part);
    }
    return h;
}
inline std::uint64_t plan_return_hash(const Plan& p) {
    const auto bank=route_hash(p.bank),renewal=route_hash(p.renewal);
    if (!bank || !renewal) return 0;
    return hash_word(hash_word(hash_word(1469598103934665603ULL,p.group),bank),renewal);
}
struct StepAccess {
    p6::Point next_tile{};
    bool assumptions_hold = false, next_move_legal = false;
    bool friendly_queue = false;
};
struct Legality {
    bool safe_now = false, attack_legal = false, altar_legal = false;
    bool recall_safe = false, renewal_productive = false;
    bool escape_legal = false;
    p6::Point escape_tile{};
    std::array<StepAccess, 5> routes{};
};
struct Milestones {
    int group = -1;
    // These are actual own endpoint observations supplied by an audited
    // attribution boundary, never predictions or public team-score deltas.
    bool own_payout = false, own_bank = false;
    std::int64_t payout_positive_lower = 0, bank_positive_lower = 0;
};
struct LocalFrame {
    p6::Frame frame{};
    p6::Observation<Legality> legal{};
    std::array<p6::CycleCertificate, 6> safety{};
    int safety_count = 0;
    p6::Observation<Milestones> milestones{};
    // The adapter must query immediately before step/dispatch. Suspension or
    // a mismatched current round invalidates the old proposal.
    int current_round = -1;
    bool fuel_resumed = false;
    // A private job may be admitted before its first charged ingress turn.
    // This frame cannot authorize an action or advance the executor.
    bool admission_only = false;
    std::int64_t fuel_remaining = 100000, action_reserve = 16000;
    bool fresh() const {
        return frame.valid() && current_round == frame.clock.round &&
            legal.current(frame.clock, frame.clock.self) &&
            safety_count >= 0 && safety_count <= 6;
    }
    const p6::CycleCertificate* certificate(int id, int group) const {
        if (!fresh()) return nullptr;
        for (int i = 0; i < safety_count; ++i)
            if (safety[i].actor == id && safety[i].valid(frame, id, group)) return &safety[i];
        return nullptr;
    }
};
inline int crew_index(const Plan& p, int id) {
    for (int i = 0; i < p.crew_count && i < 6; ++i) if (p.crew[i] == id) return i;
    return -1;
}
inline bool independent_safety(const p6::CycleCertificate& c) {
    if (!c.solo) return false;
    for (const auto& e : {c.clear,c.hold,c.bank,c.recovery})
        if (e.assigned_timed_heal != 0) return false;
    return true;
}
#if P8_ORDINARY_COMPLETE_CYCLE
// This classifies timing only. Policy must still prove an unmarked sole-own
// offer and bind remaining_rounds to the queried remaining match.
inline bool ordinary_long_cycle(const Plan& p) {
    return (p.organization==Organization::U||p.organization==Organization::G)&&p.forecast_rounds>120;
}
#endif
inline bool plan_valid(const Plan& p, int self) {
    if (p.group < 0 || p.collector < 0 || p.altar < 0 || !p.gem_bearing ||
        !p.complete_verified || p.forecast_rounds <= 0 ||
#if P8_ORDINARY_COMPLETE_CYCLE
        p.forecast_rounds > (ordinary_long_cycle(p)?p.remaining_rounds:120) ||
        p.forecast_rounds > (std::numeric_limits<int>::max()-4)/2 ||
#else
        p.forecast_rounds > 120 ||
#endif
        p.remaining_rounds <= 0 || p.capture_window <= 0 || p.capture_window > 120 ||
        p.crew_count <= 0 || p.crew_count > 6 || !plan_route_hash(p)) return false;
    const int own = crew_index(p, self);
    if (own < 0 || crew_index(p, p.collector) < 0) return false;
    for (int i = 0; i < p.crew_count; ++i) {
        if (p.crew[i] < 0) return false;
        for (int j = 0; j < i; ++j) if (p.crew[i] == p.crew[j]) return false;
    }
    const auto end = [](const Route& r) { return r.tiles[r.count-1]; };
    if (!(end(p.ingress) == p.capture.tiles[0]) ||
        !(end(p.capture) == p.bank.tiles[0]) ||
        !(end(p.bank) == p.renewal.tiles[0])) return false;
    int walking_lower = 0;
    for (Leg leg : {Leg::ingress,Leg::capture,Leg::bank,Leg::renewal})
        walking_lower += route_for(p,leg).count-1+2; // Common collision slack per retained leg.
    if (p.release.count) walking_lower += p.release.count-1+2;
    if (walking_lower > p.forecast_rounds) return false;
    if (p.organization == Organization::U || p.organization == Organization::G)
        return p.crew_count == 1 && p.collector == self;
    if (p.crew_count < 2 || !p.rally_legal || !(end(p.ingress) == p.pads[own])) return false;
    for (int i = 0; i < p.crew_count; ++i) {
        if (p.pads[i].x < 0 || p.pads[i].y < 0) return false;
        for (int j = 0; j < i; ++j) if (p.pads[i] == p.pads[j]) return false;
    }
    // Helpers can physically leave only along their retained release leg.
    return p.organization != Organization::H || self == p.collector ||
        (route_valid(p.release) && p.release.tiles[0] == end(p.capture));
}
inline bool crew_available(const Plan& p, const LocalFrame& in, bool at_pads) {
    if (!in.fresh()) return false;
    for (int i = 0; i < p.crew_count; ++i) {
        const auto* actor = in.frame.visible(p.crew[i]);
        if (!actor || !actor->alive || actor->channel != p6::Channel::none ||
            !in.certificate(actor->id, p.group) || (at_pads && !(actor->position == p.pads[i])))
            return false;
    }
    return true;
}
inline bool crew_observed(const Plan& p, const LocalFrame& in) {
    if (!in.fresh()) return false;
    for (int i = 0; i < p.crew_count; ++i) {
        const auto* actor = in.frame.visible(p.crew[i]);
        if (!actor || !actor->alive || !in.certificate(actor->id,p.group)) return false;
    }
    return true;
}
inline bool physical_admissible(const Plan& p, const LocalFrame& in) {
    if (!in.fresh() || in.fuel_resumed || in.fuel_remaining < in.action_reserve ||
        !in.legal.value.safe_now || !plan_valid(p, in.frame.clock.self) ||
        !(in.frame.own.actor.position == p.ingress.tiles[0]))
        return false;
    if (p.organization == Organization::U || p.organization == Organization::G) {
        // plan_valid proves that this sole crew member is self. Validate the
        // same current certificate once, then inspect that exact certificate.
        const auto& actor=in.frame.own.actor;
        if(!actor.alive||actor.channel!=p6::Channel::none)return false;
        const auto* own=in.certificate(in.frame.clock.self,p.group);
        return own&&independent_safety(*own);
    }
    if(!crew_available(p,in,false))return false;
    if (p.organization == Organization::H) {
        const auto* collector = in.frame.visible(p.collector);
        if (!collector) return false;
        // Personal adoption sees the named proposer at this group's rally pad;
        // no helper can read the proposer's private qualification counters.
        if (in.frame.clock.self != p.collector &&
            !(collector->position == p.pads[crew_index(p,p.collector)])) return false;
        if (in.frame.clock.self != p.collector && in.frame.own.actor.bag > 0) {
            // A loaded helper withdraws along a prefix of its own retained bank
            // path. Actual arrival joins that path without teleporting a cursor.
            if (!route_valid(p.release) || p.release.count > p.bank.count) return false;
            for (int i=0;i<p.release.count;++i)
                if (!(p.release.tiles[i] == p.bank.tiles[i])) return false;
        }
    }
    return true;
}
struct Command {
    PhysicalAction action = PhysicalAction::yield;
    PhysicalStage stage = PhysicalStage::idle;
    PhysicalFailure reason = PhysicalFailure::none;
    p6::ViewClock clock{};
    p6::Point target{};
    int group = -1;
    Leg leg = Leg::ingress;
    std::uint64_t token = 0, route_hash = 0;
};
struct ActionReceipt {
    p6::ViewClock command_clock{}, current_clock{};
    std::uint64_t token = 0;
    PhysicalAction action = PhysicalAction::yield;
    bool attempted = false, success = false;
};
struct Work {
    Plan plan{};
    PhysicalStage stage = PhysicalStage::idle;
    PhysicalFailure failure = PhysicalFailure::none;
    std::array<int,5> cursor{};
    int last_round = -1, owner_turn = 0, phase_started = 0, phase_limit = 8;
    int assembly_turns = 0, hold_turns = 0, hold_extensions = 0, bank_queue_turns = 0;
    bool active = false, stop_requested = false, route_witness_active = false;
    bool clearance_seen = false, payout_seen = false, bank_seen = false;
    bool productive_action_seen = false;
    bool renewal_seen = false, cycle_completed = false, helper_released = false;
    bool helper_bag_return = false;
    bool tribute_started = false, pending = false, release_admitted = false;
    std::int64_t payout_lower = 0, bank_lower = 0, tribute_starting_bag = 0;
    Command issued{};
    std::uint64_t serial = 0;
    std::uint64_t generation = 0;
    p6::ViewClock started_clock{};

    bool start(const Plan& selected, const LocalFrame& in) {
        if (active || generation == std::numeric_limits<std::uint64_t>::max() ||
            in.frame.clock.round<=last_round ||
            !physical_admissible(selected,in)) return false;
        return start_after_physical(selected,in);
    }
private:
    // Only the complete synchronous Canary admission transaction may reuse its
    // own physical check. It issues evidence before this lifecycle guard and
    // publishes no unchecked permission or proof to another turn/caller.
    friend struct canary::CurrentAdmission;
    bool start_after_physical(const Plan& selected,const LocalFrame& in) {
        if(active||generation==std::numeric_limits<std::uint64_t>::max()||
            in.frame.clock.round<=last_round)return false;
        ++generation; started_clock=in.frame.clock;
        // This is a freshly observed new job, possibly after inactive fallback
        // rounds. Only gaps within an active job invalidate its route witness.
        // Keep the cross-job same-turn guard above and monotonic receipt IDs.
        last_round=in.frame.clock.round-1;
        // Permission above validated every active count. Preserve the exact
        // active plan without copying thousands of unused route coordinates.
        plan.organization=selected.organization;plan.group=selected.group;
        plan.collector=selected.collector;plan.altar=selected.altar;
        plan.crew=selected.crew;plan.pads=selected.pads;plan.crew_count=selected.crew_count;
        plan.forecast_rounds=selected.forecast_rounds;plan.remaining_rounds=selected.remaining_rounds;
        plan.capture_window=selected.capture_window;plan.gem_bearing=selected.gem_bearing;
        plan.complete_verified=selected.complete_verified;plan.rally_legal=selected.rally_legal;
        const auto copy=[](Route& to,const Route& from) {
            for(int i=0;i<from.count;++i)to.tiles[i]=from.tiles[i];
            to.count=from.count;to.blocker_assumptions=from.blocker_assumptions;to.verified=from.verified;
        };
        copy(plan.ingress,selected.ingress);copy(plan.capture,selected.capture);
        copy(plan.bank,selected.bank);copy(plan.renewal,selected.renewal);copy(plan.release,selected.release);
        stage = PhysicalStage::travel; failure = PhysicalFailure::none;
        cursor = {}; active = route_witness_active = true; stop_requested = false;
        clearance_seen = payout_seen = bank_seen = renewal_seen = cycle_completed = false;
        productive_action_seen = false;
        helper_released = helper_bag_return = tribute_started = pending = release_admitted = false;
        payout_lower = bank_lower = tribute_starting_bag = 0;
        assembly_turns = hold_turns = hold_extensions = bank_queue_turns = 0;
        phase_started = owner_turn;
        phase_limit = std::min(selected.remaining_rounds, std::max(8, 2*selected.forecast_rounds+4));
        return true;
    }
public:
    void request_stop() { stop_requested = true; }
    bool consume_admission_turn(const LocalFrame& in) {
        if(!in.admission_only||!in.fresh()||in.fuel_resumed||!active||!route_witness_active||
            stop_requested||pending||stage!=PhysicalStage::travel||failure!=PhysicalFailure::none||
            plan.organization!=Organization::U||plan.crew_count!=1||plan.collector!=in.frame.clock.self||
            plan.ingress.count<2||!(started_clock==in.frame.clock)||last_round!=in.frame.clock.round-1||
            phase_started!=owner_turn||owner_turn==std::numeric_limits<int>::max()||
            clearance_seen||payout_seen||bank_seen||renewal_seen||cycle_completed||productive_action_seen)return false;
        for(int at:cursor)if(at!=0)return false;
        // The caller proves one already forecast ingress slack turn and a safe
        // origin. Charge this owner turn without an action or progress receipt.
        last_round=in.frame.clock.round;++owner_turn;
        return true;
    }
    void enter(PhysicalStage next) { stage = next; phase_started = owner_turn; }
    Command emit(const LocalFrame& in, PhysicalAction action, p6::Point target = {},
                 Leg leg = Leg::ingress) {
        // Physical moves keep their exact dispatch identity even outside a
        // qualified route witness (inspection or an owned bag's safe return).
        // route_witness_active remains the separate evidence permission.
        issued = {action,stage,failure,in.frame.clock,target,plan.group,leg,++serial,
                  route_witness_active || action==PhysicalAction::move ? plan_route_hash(plan) : 0};
        pending = true; return issued;
    }
    void invalidate(PhysicalFailure reason) {
        failure = reason; active = route_witness_active = false; pending = false;
        cycle_completed = false; stage = PhysicalStage::recover;
    }
    Command fallback(const LocalFrame& in) {
        if (!in.fresh()) return {};
        const auto& a = in.frame.own.actor;
        if (a.channel != p6::Channel::none && a.channel_valid && in.legal.value.safe_now)
            return emit(in,PhysicalAction::preserve_channel);
        if (!a.alive || in.frame.own.at_spawn) return emit(in,PhysicalAction::recover);
        if (!in.legal.value.safe_now && in.legal.value.escape_legal)
            return emit(in,PhysicalAction::escape,in.legal.value.escape_tile);
        if (in.legal.value.recall_safe) return emit(in,PhysicalAction::recall);
        return emit(in,PhysicalAction::yield);
    }
    // -1 invalid, 0 arrived, 1 emitted movement. Only actual observed arrival
    // advances a cursor; a successful invocation is not arrival evidence.
    int route_step(const LocalFrame& in, Leg leg, Command& out) {
        const auto& r = route_for(plan,leg);
        const auto& access = in.legal.value.routes[static_cast<int>(leg)];
        auto& at = cursor[static_cast<int>(leg)];
        if (!route_valid(r) || at < 0 || at >= r.count) {
            invalidate(PhysicalFailure::invalid_route); return -1;
        }
        if (!access.assumptions_hold) { invalidate(PhysicalFailure::blocked_route); return -1; }
        const auto position = in.frame.own.actor.position;
        if (at+1 < r.count && position == r.tiles[at+1]) ++at;
        else if (!(position == r.tiles[at])) { invalidate(PhysicalFailure::invalid_route); return -1; }
        if (at == r.count-1) return 0;
        if (!in.frame.own.actor.can_move) {
            out = emit(in,PhysicalAction::hold); return 1;
        }
        if (!access.next_move_legal ||
            !(access.next_tile == r.tiles[at+1])) {
            bool visible_friend=false;
            if (leg==Leg::bank&&access.friendly_queue&&access.next_tile==r.tiles[at+1])
                for (int i=0;i<in.frame.actor_count;++i) {
                    const auto& seen=in.frame.actors[i];
                    visible_friend|=seen.current(in.frame.clock,seen.subject)&&seen.value.team==p6::Team::ally&&
                        seen.value.alive&&seen.value.position==access.next_tile;
                }
            if (visible_friend&&bank_queue_turns<std::min(120,plan.forecast_rounds)) {
                ++bank_queue_turns;out=emit(in,PhysicalAction::hold);return 1;
            }
            invalidate(PhysicalFailure::blocked_route); return -1;
        }
        out = emit(in,PhysicalAction::move,r.tiles[at+1],leg); return 1;
    }
    bool confirm(const Command& command, const ActionReceipt& receipt) {
        if (!pending || command.token != issued.token || command.token != receipt.token ||
            command.action != issued.action || receipt.action != issued.action ||
            !(command.clock == issued.clock) || !(receipt.command_clock == issued.clock) ||
            !(receipt.current_clock == issued.clock) || !receipt.attempted) return false;
        pending = false;
        if (issued.action == PhysicalAction::move && !receipt.success) {
            invalidate(PhysicalFailure::blocked_route); return true;
        }
        if (issued.action == PhysicalAction::tribute && receipt.success) {
            tribute_started = true; // Receipt is distinct from the proposal.
        }
        if (issued.action == PhysicalAction::attack && receipt.success)
            productive_action_seen = true;
        return true;
    }
    Command step(const LocalFrame& in) {
        if(in.admission_only)return {};
        if (!in.fresh()) {
            pending = false; failure = PhysicalFailure::stale;
            stop_requested = true; route_witness_active = false; return {};
        }
        if (last_round >= in.frame.clock.round) return {};
        pending = false;
        const bool skipped = last_round >= 0 && in.frame.clock.round != last_round+1;
        last_round = in.frame.clock.round; ++owner_turn;
        const auto& a = in.frame.own.actor;
        if (!a.alive) { invalidate(PhysicalFailure::dead); return fallback(in); }
        if (!in.legal.value.safe_now) { invalidate(PhysicalFailure::unsafe); return fallback(in); }
        if (in.fuel_resumed || skipped) {
            failure = in.fuel_resumed ? PhysicalFailure::fuel : PhysicalFailure::stale;
            stop_requested = true; route_witness_active = false; cycle_completed = false;
        }
        if (active && plan.organization == Organization::J &&
            stage != PhysicalStage::assemble && !crew_observed(plan,in)) {
            // The control remains a personally visible persistent unit. A
            // missing member cannot silently turn its later bank/renewal into
            // a completed J mechanism. Safe channels and bags still finish.
            failure = PhysicalFailure::missing_member;
            stop_requested = true; route_witness_active = false;
        }
        // A safe ongoing channel has precedence over expiry, role changes,
        // missing members and low fuel. This does not confirm its endpoint.
        if (a.channel != p6::Channel::none && a.channel_valid)
            return emit(in,PhysicalAction::preserve_channel);
        if (in.fuel_remaining < in.action_reserve) {
            failure = PhysicalFailure::fuel;
            return emit(in,stage == PhysicalStage::hold && a.spawn_group == plan.group ?
                        PhysicalAction::hold : PhysicalAction::yield);
        }
        if (!active) return fallback(in);
        const auto* own_certificate = in.certificate(a.id,plan.group);
        if (!own_certificate) { invalidate(PhysicalFailure::unsafe); return fallback(in); }
        for (int i = 0; i < plan.crew_count; ++i) if (plan.crew[i] != a.id) {
            // Actor.action is an actual personally witnessed action supplied
            // at this exact observation stamp, not a promised future attack.
            const auto* member = in.frame.visible(plan.crew[i]);
            if (member && member->team == p6::Team::ally &&
                member->action == p6::WitnessAction::useful_attack &&
                member->action_group == plan.group) productive_action_seen = true;
        }
        // Consume actual endpoints in the stage which produced them before a
        // safe stop changes that stage. A rollback must not erase its payout.
        if (in.milestones.current(in.frame.clock,a.id) && in.milestones.value.group == plan.group) {
            const auto& m = in.milestones.value;
            if (stage == PhysicalStage::hold && m.own_payout && m.payout_positive_lower > 0) {
                payout_seen = true; payout_lower = m.payout_positive_lower; enter(PhysicalStage::payout);
            }
            if (stage == PhysicalStage::bank && tribute_started && m.own_bank &&
                m.bank_positive_lower > 0 && a.bag == 0) {
                bank_seen = true; bank_lower = m.bank_positive_lower; enter(PhysicalStage::renew);
            }
        }
        const bool dependent = plan.organization == Organization::H || plan.organization == Organization::J;
        const auto* group = in.frame.live_group(plan.group);
        const bool own_safe_hold = stage == PhysicalStage::hold && a.spawn_group == plan.group &&
            independent_safety(*own_certificate) && group && group->lifecycle == p6::Lifecycle::reward_available;
        if (stop_requested && !own_safe_hold && stage != PhysicalStage::bank && stage != PhysicalStage::renew) {
            if (a.bag > 0 && a.position == plan.bank.tiles[0] &&
                in.legal.value.routes[static_cast<int>(Leg::bank)].assumptions_hold)
                enter(PhysicalStage::bank);
            else { invalidate(failure == PhysicalFailure::none ? PhysicalFailure::unavailable : failure); return fallback(in); }
        }
        // Missing future labor cancels dependent clearance immediately. A safe
        // actual capture remains protected, including an observed replacement.
        if (dependent && stage != PhysicalStage::assemble && stage != PhysicalStage::hold &&
            stage != PhysicalStage::payout && stage != PhysicalStage::bank && stage != PhysicalStage::renew &&
            !crew_available(plan,in,false)) {
            invalidate(PhysicalFailure::missing_member); return fallback(in);
        }
        if (dependent && stage == PhysicalStage::hold && !crew_available(plan,in,false) &&
            !independent_safety(*own_certificate)) {
            invalidate(PhysicalFailure::missing_member); return fallback(in);
        }
        if (dependent && (stage == PhysicalStage::bank || stage == PhysicalStage::renew) &&
            !crew_observed(plan,in) && !independent_safety(*own_certificate)) {
            invalidate(PhysicalFailure::missing_member); return fallback(in);
        }
        if (owner_turn-phase_started > phase_limit && stage != PhysicalStage::hold) {
            invalidate(PhysicalFailure::timeout); return fallback(in);
        }
        Command out;
        // At most the finite stage chain is traversed; there is no planner loop.
        for (int transition = 0; transition < 9; ++transition) {
            switch (stage) {
            case PhysicalStage::travel: {
                const int result = route_step(in,Leg::ingress,out);
                if (result) return result > 0 ? out : fallback(in);
                enter(dependent ? PhysicalStage::assemble : PhysicalStage::clear); break;
            }
            case PhysicalStage::assemble:
                if (crew_available(plan,in,true)) { enter(PhysicalStage::clear); break; }
                failure = PhysicalFailure::missing_member;
                if (++assembly_turns >= 8) { invalidate(PhysicalFailure::missing_member); return fallback(in); }
                return emit(in,PhysicalAction::hold);
            case PhysicalStage::clear:
                if (!group) return emit(in,PhysicalAction::inspect);
                if (group->lifecycle == p6::Lifecycle::living)
                    return emit(in,in.legal.value.attack_legal && a.can_attack ? PhysicalAction::attack : PhysicalAction::hold);
                if (group->lifecycle != p6::Lifecycle::reward_available) {
                    invalidate(PhysicalFailure::unavailable); return fallback(in);
                }
                clearance_seen = true; enter(PhysicalStage::hold); break;
            case PhysicalStage::hold: {
                const int result = route_step(in,Leg::capture,out);
                if (result) return result > 0 ? out : fallback(in);
                if (plan.organization == Organization::H && a.id != plan.collector) {
                    const auto* holder = in.certificate(plan.collector,plan.group);
                    if (holder && independent_safety(*holder) &&
                        p6::witnessed_release(in.frame,plan.collector,plan.group,*holder)) {
                        release_admitted = true; helper_bag_return = a.bag > 0;
                        enter(PhysicalStage::renew); break;
                    }
                    // A helper already clearing from outside the capture region
                    // waits for the actual safe holder there. It need not step
                    // onto a reward tile merely to leave it again.
                    if (a.spawn_group != plan.group) {
                        if (++hold_turns>plan.capture_window+8) {
                            invalidate(PhysicalFailure::missing_member);return fallback(in);
                        }
                        return emit(in,PhysicalAction::hold);
                    }
                }
                if (a.spawn_group != plan.group) { invalidate(PhysicalFailure::invalid_route); return fallback(in); }
                if (group && group->lifecycle == p6::Lifecycle::respawning) {
                    // Lifecycle does not identify who received the reward.
                    enter(PhysicalStage::payout); break;
                }
                if (++hold_turns > plan.capture_window+1) {
                    if (hold_extensions++ == 0) hold_turns = 0;
                    else { invalidate(PhysicalFailure::unconfirmed_endpoint); return fallback(in); }
                }
                return emit(in,PhysicalAction::hold);
            }
            case PhysicalStage::payout:
                if (a.bag <= 0) { invalidate(PhysicalFailure::unconfirmed_endpoint); return fallback(in); }
                enter(PhysicalStage::bank); break;
            case PhysicalStage::bank: {
                const int result = route_step(in,Leg::bank,out);
                if (result) return result > 0 ? out : fallback(in);
                if (a.bag == 0 && tribute_started) {
                    // Resume useful work even if attribution is unavailable;
                    // full-cycle evidence remains false without an own receipt.
                    failure = PhysicalFailure::unconfirmed_endpoint; enter(PhysicalStage::renew); break;
                }
                if (!in.legal.value.altar_legal || a.bag <= 0) {
                    invalidate(PhysicalFailure::unavailable); return fallback(in);
                }
                tribute_starting_bag = a.bag;
                return emit(in,PhysicalAction::tribute);
            }
            case PhysicalStage::renew: {
                const Leg leg = release_admitted && !helper_released ? Leg::release : Leg::renewal;
                const int result = route_step(in,leg,out);
                if (result) return result > 0 ? out : fallback(in);
                if (release_admitted && !helper_released && helper_bag_return) {
                    if (a.spawn_group == plan.group) { invalidate(PhysicalFailure::invalid_route); return fallback(in); }
                    helper_released = true;
                    cursor[static_cast<int>(Leg::bank)] = plan.release.count-1;
                    enter(PhysicalStage::bank); break;
                }
                if (!in.legal.value.renewal_productive) return emit(in,PhysicalAction::inspect);
                helper_released = release_admitted && a.spawn_group != plan.group;
                renewal_seen = true; cycle_completed = productive_action_seen && clearance_seen &&
                    payout_seen && bank_seen && route_witness_active;
                active = false; enter(PhysicalStage::complete); return emit(in,PhysicalAction::hold);
            }
            default: return fallback(in);
            }
        }
        invalidate(PhysicalFailure::invalid_route); return fallback(in);
    }
};

enum class StartupKind { solo, inspect, coalition, recover, waiting };
struct StartupCandidate {
    Plan plan{};
    int complete_cost = -1, static_distance = -1;
    bool currently_verified = false, static_candidate = false;
};
struct StartupDecision {
    StartupKind kind = StartupKind::recover;
    Plan plan{};
    PhysicalAction action = PhysicalAction::recover;
    p6::Point target{};
    PhysicalFailure reason = PhysicalFailure::unavailable;
};
struct Startup {
    int last_round = -1, turns = 0, inspection_turns = 0, inspect_group = -1;
    int cursor = 0, next_rescan = 0;
    Route inspection{};
    bool inspecting = false, opportunity_unavailable = false;
    StartupDecision step(const LocalFrame& in, std::span<const StartupCandidate> candidates,
                         int represented_altars) {
        if (!in.fresh() || candidates.size() > 8 || represented_altars < 0 ||
            represented_altars > 2 || last_round == in.frame.clock.round) return {StartupKind::waiting};
        last_round = in.frame.clock.round; ++turns;
        const auto& actor = in.frame.own.actor;
        if (actor.channel != p6::Channel::none && actor.channel_valid && in.legal.value.safe_now)
            return {StartupKind::waiting,{},PhysicalAction::preserve_channel,{},PhysicalFailure::none};
        if (!actor.alive || !in.legal.value.safe_now) {
            if (actor.alive && in.legal.value.escape_legal)
                return {StartupKind::recover,{},PhysicalAction::escape,in.legal.value.escape_tile,PhysicalFailure::unsafe};
            return {};
        }
        if (in.fuel_resumed || in.fuel_remaining < in.action_reserve)
            return {StartupKind::waiting,{},PhysicalAction::yield,{},PhysicalFailure::fuel};
        if (turns < next_rescan) return {StartupKind::waiting};
        const StartupCandidate* cheapest = nullptr;
        for (const auto& c : candidates) {
            if (!c.currently_verified || c.complete_cost < 0 ||
                (c.plan.organization != Organization::U && c.plan.organization != Organization::G) ||
                !physical_admissible(c.plan,in)) continue;
            if (!cheapest || c.complete_cost < cheapest->complete_cost ||
                (c.complete_cost == cheapest->complete_cost && c.plan.group < cheapest->plan.group)) cheapest = &c;
        }
        if (cheapest) { inspecting = false; return {StartupKind::solo,cheapest->plan,PhysicalAction::hold,{},PhysicalFailure::none}; }
        if (!inspecting && inspection_turns == 0) {
            const StartupCandidate* nearest = nullptr;
            for (const auto& c : candidates) if (c.static_candidate && c.static_distance >= 0 &&
                route_valid(c.plan.ingress) && c.plan.ingress.tiles[0] == in.frame.own.actor.position &&
                (!nearest || c.static_distance < nearest->static_distance ||
                 (c.static_distance == nearest->static_distance && c.plan.group < nearest->plan.group))) nearest = &c;
            if (nearest) {
                inspecting = true; inspect_group = nearest->plan.group;
                inspection = nearest->plan.ingress; cursor = 0;
            }
        }
        if (inspecting && inspection_turns < 8) {
            ++inspection_turns;
            const auto& access = in.legal.value.routes[static_cast<int>(Leg::ingress)];
            const auto pos = in.frame.own.actor.position;
            if (cursor+1 < inspection.count && pos == inspection.tiles[cursor+1]) ++cursor;
            else if (!(pos == inspection.tiles[cursor])) inspecting = false;
            if (!access.assumptions_hold) inspecting = false;
            if (inspecting) {
                StartupDecision decision{StartupKind::inspect}; decision.plan.group = inspect_group;
                if (cursor+1 == inspection.count) decision.action = PhysicalAction::inspect;
                else if (in.frame.own.actor.can_move && access.next_move_legal &&
                         access.next_tile == inspection.tiles[cursor+1]) {
                    decision.action = PhysicalAction::move; decision.target = inspection.tiles[cursor+1];
                } else inspecting = false;
                if (inspecting) return decision;
            }
            inspection_turns = 8; // Broken witness cannot be silently replaced.
        }
        inspecting = false;
        for (const auto& c : candidates)
            if (c.currently_verified && (c.plan.organization == Organization::H || c.plan.organization == Organization::J) &&
                physical_admissible(c.plan,in)) return {StartupKind::coalition,c.plan,PhysicalAction::hold,{},PhysicalFailure::none};
        opportunity_unavailable = true; next_rescan = turns+8;
        // Each later rescan may inspect a newly verified static route for one
        // bounded attempt; the preceding failure remains an unavailable result.
        inspection_turns = 0;
        return {};
    }
};
} // namespace p8
