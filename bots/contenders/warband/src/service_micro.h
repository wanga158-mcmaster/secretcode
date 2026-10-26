#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

// Pure, bounded gateway for ONE basic or ordinary move per physical owner input.
// The parent owns all SDK queries, the final fuel/epoch fence, and dispatch. It
// certifies the WHOLE harmless camp group and static-potential-hazard-free route.
// This header proves no native legality, fuel safety, PvP safety, or kit mastery.
namespace wb::service_micro {

struct Position { int x{}, y{}; };
constexpr bool operator==(Position a, Position b) { return a.x == b.x && a.y == b.y; }
constexpr bool operator!=(Position a, Position b) { return !(a == b); }

struct Authority {
    int owner{-1}, round{-1};
    std::uint64_t input_generation{}, view_revision{};
};
constexpr bool valid(Authority a) {
    return a.owner >= 0 && a.round >= 0 && a.input_generation && a.view_revision;
}
constexpr bool same_input(Authority a, Authority b) {
    return a.owner == b.owner && a.round == b.round
        && a.input_generation == b.input_generation;
}
constexpr bool operator==(Authority a, Authority b) {
    return same_input(a, b) && a.view_revision == b.view_revision;
}
constexpr bool operator!=(Authority a, Authority b) { return !(a == b); }

enum class Kind { None, Basic, Move, Cast };
// Current observed movement can include a hidden occupant. MayRefuse is an
// authorization to ATTEMPT under the scoped refusal-prefix contract, never a
// statement that the landing is visible/empty or that the move will succeed.
enum class MoveLanding { Missing, KnownFree, MayRefuse, KnownBlocked };
enum class Status { NoCommand, NeedObservation, ComputeDeferred, Unsupported, Issued, Refused, Applied };
enum class Effect { None, Moved, Damaged, TargetDied, Refused, Invalidated };
enum class Acceptance { Accepted, Refused, Invalidated };
enum class Reason {
    None, NoRequest, AuthorityMismatch, StaleAuthority, WrongOwner,
    MissingObservation, InvalidObservation, UnsupportedDomain, BasicOnly,
    ActorUnavailable, NoAllowance, TargetDead, TargetMismatch, OutOfRange,
    IllegalMove, UnsupportedRefusalPrefix, EndpointMismatch, InputConsumed, PendingAttempt,
    UnknownAttempt, Deferred, DispatchInvalidated, EpochChanged, EffectUnverified
};

struct Permissions { bool move{}, attack{}, cast{}; };
struct Frame {
    Authority authority{};
    bool observed_self{}, observed_readiness{};
    Position position{};
    float hp{};
    bool alive{}, stunned{}, channel{}, can_move{}, can_attack{}, can_cast{};
    // Parent certificate: complete committed group harmless; permitted route and
    // hold exclude ALL static potential harmful footprints. Unknown != supported.
    bool observed_domain{}, domain_supported{};
    bool observed_target{};
    int target_id{-1};
    Position target_position{};
    float target_hp{};
    bool target_visible{}, target_alive{}, basic_in_range{};
    // Complete current geometry/reachability evidence for this exact endpoint.
    // move_legal means attempt permission, not known emptiness or success.
    bool observed_move{};
    Position move_endpoint{};
    bool move_legal{};
    MoveLanding move_landing{MoveLanding::Missing};
    // Macro certificate: origin and landing exclude ALL potential NPC harm;
    // under the declared no-foe premise a refused attempt may retain this prefix
    // for a finite wait/retry. One attempt still prices an entire owner input,
    // even though native false preserves M. This is no universal PvP safety.
    bool refusal_prefix_supported{};
};
struct Request {
    Kind kind{Kind::None};
    int target_id{-1};
    Position endpoint{};
};
struct Attempt {
    std::uint64_t ticket{};
    Authority authority{};
    Request request{};
    constexpr bool valid() const { return ticket != 0; }
};
struct DispatchResult {
    // Accepted is a true movement return or normally returned VOID attack.
    // It does not attest HP damage, death, or successful service completion.
    // Invalidated means NO primitive was invoked (for example a final epoch or
    // fuel fence failed). An accepted primitive with missing post-observation
    // remains Accepted; complete then returns NeedObservation, not Applied.
    Acceptance acceptance{Acceptance::Invalidated};
};
struct Decision {
    Status status{Status::NoCommand};
    Effect effect{Effect::None};
    Reason reason{Reason::None};
    Attempt attempt{};
    bool command_attempted{}, command_accepted{};
    bool post_permissions_observed{};
    Permissions post_permissions{};
    constexpr bool issued() const { return status == Status::Issued && attempt.valid(); }
};

// Grammar of this first class: observe -> (Move | Basic) -> dispatch -> refresh.
// Cast/reset/dash words are explicitly unsupported. Ranged basic/move coupling
// and any remaining melee permission come ONLY from refreshed actual facts.
// A new observation revision never rearms the consumed physical input.
struct Grammar {
    static constexpr bool basic_only = true;
    static constexpr bool supports(Kind k) {
        return k == Kind::None || k == Kind::Basic || k == Kind::Move;
    }
};

class Gate {
    bool seen_{};
    Authority latest_{}, used_{};
    bool used_valid_{};
    Attempt pending_{};
    Frame before_{};
    std::uint64_t next_ticket_{1};

    static Decision outcome(Status s, Reason r, Effect e = Effect::None) {
        Decision d; d.status = s; d.reason = r; d.effect = e; return d;
    }
    Reason accept_current(Authority a) {
        if (!valid(a)) return Reason::AuthorityMismatch;
        if (seen_) {
            if (a.owner != latest_.owner) return Reason::WrongOwner;
            if (a.round < latest_.round || a.input_generation < latest_.input_generation)
                return Reason::StaleAuthority;
            // input_generation is stable throughout one physical owner input;
            // re-observation changes view_revision only. This also rejects a
            // forged new generation used to rearm the same owner round.
            if ((a.round == latest_.round) != (a.input_generation == latest_.input_generation))
                return Reason::StaleAuthority;
            if (same_input(a, latest_) && a.view_revision < latest_.view_revision)
                return Reason::StaleAuthority;
        }
        latest_ = a; seen_ = true; return Reason::None;
    }
    static Reason observed_actor(const Frame& f) {
        if (!f.observed_self || !f.observed_readiness) return Reason::MissingObservation;
        if (!std::isfinite(f.hp) || (f.alive && f.hp <= 0)) return Reason::InvalidObservation;
        return Reason::None;
    }
    static bool matches(const Attempt& a, const Attempt& b) {
        return a.valid() && a.ticket == b.ticket && a.authority == b.authority
            && a.request.kind == b.request.kind && a.request.target_id == b.request.target_id
            && a.request.endpoint == b.request.endpoint;
    }

public:
    Gate() = default;
    Gate(const Gate&) = delete;
    Gate& operator=(const Gate&) = delete;
    Gate(Gate&&) = delete;
    Gate& operator=(Gate&&) = delete;

    // Use one Gate per owner. current must be minted from the parent's actual
    // current input, independently of the proposed observation packet.
    Decision issue(Authority current, const Frame& f, Request request, bool compute_ready = true) {
        if (!Grammar::supports(request.kind)) return outcome(Status::Unsupported, Reason::BasicOnly);
        const auto auth = accept_current(current);
        if (auth != Reason::None) return outcome(Status::NeedObservation, auth);
        if (f.authority != current) return outcome(Status::NeedObservation, Reason::AuthorityMismatch);
        if (pending_.valid()) return outcome(Status::NoCommand, Reason::PendingAttempt);
        if (used_valid_ && same_input(used_, current)) return outcome(Status::NoCommand, Reason::InputConsumed);
        if (!compute_ready) return outcome(Status::ComputeDeferred, Reason::Deferred);
        const auto actor = observed_actor(f);
        if (actor != Reason::None) return outcome(Status::NeedObservation, actor);
        if (!f.alive || f.stunned || f.channel) return outcome(Status::NoCommand, Reason::ActorUnavailable);
        if (request.kind == Kind::None) return outcome(Status::NoCommand, Reason::NoRequest);
        if (!f.observed_domain) return outcome(Status::NeedObservation, Reason::MissingObservation);
        if (!f.domain_supported) return outcome(Status::Unsupported, Reason::UnsupportedDomain);

        if (request.kind == Kind::Basic) {
            if (!f.observed_target) return outcome(Status::NeedObservation, Reason::MissingObservation);
            if (request.target_id < 0 || f.target_id != request.target_id)
                return outcome(Status::NeedObservation, Reason::TargetMismatch);
            if (!f.target_alive) return outcome(Status::NoCommand, Reason::TargetDead);
            if (!f.target_visible) return outcome(Status::NeedObservation, Reason::MissingObservation);
            if (!std::isfinite(f.target_hp) || f.target_hp <= 0)
                return outcome(Status::NeedObservation, Reason::InvalidObservation);
            if (!f.can_attack) return outcome(Status::NoCommand, Reason::NoAllowance);
            if (!f.basic_in_range) return outcome(Status::NoCommand, Reason::OutOfRange);
        } else if (request.kind == Kind::Move) {
            if (!f.observed_move) return outcome(Status::NeedObservation, Reason::MissingObservation);
            if (request.endpoint != f.move_endpoint)
                return outcome(Status::NeedObservation, Reason::EndpointMismatch);
            if (f.move_landing == MoveLanding::Missing)
                return outcome(Status::NeedObservation, Reason::MissingObservation);
            if (f.move_landing != MoveLanding::KnownFree && f.move_landing != MoveLanding::MayRefuse
                && f.move_landing != MoveLanding::KnownBlocked)
                return outcome(Status::NeedObservation, Reason::InvalidObservation);
            if (!f.can_move) return outcome(Status::NoCommand, Reason::NoAllowance);
            if (!f.move_legal || f.move_landing == MoveLanding::KnownBlocked || request.endpoint == f.position)
                return outcome(Status::NoCommand, Reason::IllegalMove);
            if (f.move_landing == MoveLanding::MayRefuse && !f.refusal_prefix_supported)
                return outcome(Status::Unsupported, Reason::UnsupportedRefusalPrefix);
        }
        if (next_ticket_ == std::numeric_limits<std::uint64_t>::max())
            return outcome(Status::Unsupported, Reason::UnsupportedDomain);
        // Consume BEFORE any SDK call. Refusal, exception, changed-clock fence,
        // or fresh view cannot create a second attempt on this input.
        used_ = current; used_valid_ = true; before_ = f;
        pending_ = Attempt{next_ticket_++, current, request};
        auto d = outcome(Status::Issued, Reason::None); d.attempt = pending_; return d;
    }

    Decision complete(Authority current, const Attempt& attempt, DispatchResult result,
                      const Frame& after) {
        if (!pending_.valid()) return outcome(Status::NoCommand, Reason::UnknownAttempt);
        if (!matches(attempt, pending_)) return outcome(Status::NeedObservation, Reason::UnknownAttempt);
        const Attempt issued = pending_; const Frame before = before_;
        pending_ = {}; // Completion is one-shot too, even if post-observation fails.
        if (result.acceptance == Acceptance::Invalidated)
            return outcome(Status::NoCommand, Reason::DispatchInvalidated, Effect::Invalidated);
        if (result.acceptance == Acceptance::Refused) {
            auto d = outcome(Status::Refused, Reason::None, Effect::Refused);
            d.command_attempted = true; return d;
        }
        auto d = outcome(Status::NeedObservation, Reason::EffectUnverified, Effect::Invalidated);
        d.command_attempted = d.command_accepted = true;
        const auto auth = accept_current(current);
        if (auth != Reason::None) { d.reason = auth; return d; }
        if (!same_input(current, issued.authority)) { d.reason = Reason::EpochChanged; return d; }
        if (after.authority != current || current.view_revision <= issued.authority.view_revision) {
            d.reason = Reason::AuthorityMismatch; return d;
        }
        const auto actor = observed_actor(after);
        if (actor != Reason::None) { d.reason = actor; return d; }
        if (!after.alive) { d.reason = Reason::ActorUnavailable; return d; }
        d.post_permissions_observed = true;
        d.post_permissions = {after.can_move, after.can_attack, after.can_cast};
        if (issued.request.kind == Kind::Move) {
            if (after.position != before.position && after.position == issued.request.endpoint)
                d.effect = Effect::Moved;
        } else if (issued.request.kind == Kind::Basic) {
            if (!after.observed_target || after.target_id != issued.request.target_id) {
                d.reason = Reason::TargetMismatch; return d;
            }
            if (after.position != before.position) return d;
            if (!after.target_alive) {
                // Dead state is sufficient: never require dead live-info HP.
                d.effect = Effect::TargetDied;
            } else if (after.target_position == before.target_position
                       && after.target_visible && std::isfinite(after.target_hp)
                       && after.target_hp > 0 && after.target_hp < before.target_hp) {
                d.effect = Effect::Damaged;
            }
        }
        if (d.effect != Effect::Invalidated) { d.status = Status::Applied; d.reason = Reason::None; }
        return d;
    }
};

} // namespace wb::service_micro
