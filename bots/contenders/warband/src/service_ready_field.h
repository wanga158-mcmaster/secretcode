#pragma once

// Pure, dated body-state postcondition monitor. Observing readiness, releasing
// an old continuation and admitting a future operator are distinct operations.
// The runtime supplies sound dated facts; this never completes the old core.
#include "service.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace wb::ready_field {
using service::Authority;
using service::Position;

enum class Terminal { LegacySpawn, ReadyFieldHoldCash };
enum class Family { Unknown, FullReadyField, BareMoveBasic, ResetBasic };
enum class Scope { Unknown, NPCOnlyNoFoes };
enum class ProofSource { Unknown, TeamCounter, ActorJoinedOwnCompletion };
enum class Phase { Idle, AwaitBank, BankVerified, Ready, HandedOff, Released, Failed };
enum class Status { NoCommand, NeedObservation, BankVerified, ReadyForNextOperator,
    ObservedReadyForBasic, NeedsCapability, HandedOff, Released, Failed };
enum class TicketKind { None, Observed, Prospective };
enum class Capability { None, ReturnToSpawn, Buy };
enum class Reason { None, InvalidEpisode, ActiveEpisode, ReusedEpisode,
    MissingBank, WrongBankProvenance, StaleAuthority, PartialObservation,
    NotLaterInput, ActorDead, Loaded, NotReady, UnknownDependency,
    UnsupportedSignature, UnsupportedDomain, InvalidNextStart,
    RequiredShop, LegacyTerminal, WaitBound, StaleTicket, WrongTicketKind,
    EpisodeInvalidated, InputTicketConsumed, CallerContinues, NextBeginRefused };

// Closed schemas: callers cannot drop an inconvenient dependency by editing a
// bit. ResetBasic is named but UNSUPPORTED: C, a specific ready slot and enhanced
// attack/reset interactions need their own represented operator/refinement.
struct PrimitiveSchema {
    Family family{Family::Unknown};
    std::uint8_t permissions{}, restrictions{}, cooldowns{};
    bool enhanced_support{};
};
constexpr PrimitiveSchema full_ready() { return {Family::FullReadyField, 7, 63, 3, false}; }
constexpr PrimitiveSchema bare_move_basic() { return {Family::BareMoveBasic, 3, 3, 0, false}; }
constexpr bool operator==(PrimitiveSchema a, PrimitiveSchema b) {
    return a.family == b.family && a.permissions == b.permissions
        && a.restrictions == b.restrictions && a.cooldowns == b.cooldowns
        && a.enhanced_support == b.enhanced_support;
}
constexpr bool represented(PrimitiveSchema s) { return s == full_ready() || s == bare_move_basic(); }

struct Episode {
    std::uint64_t id{};
    service::Contract sealed{};
    service::Receipt capture{};
    std::uint64_t bank_serial_baseline{};
    Authority opened_at{};
    Terminal terminal{Terminal::LegacySpawn}; // no implicit weakening of d01
    int deadline_round{-1};
    unsigned max_observation_calls{32};
};
struct OwnBankEvidence {
    Authority authority{};
    int owner{-1}, group{-1};
    std::uint64_t episode{}, capture_serial{};
    ProofSource source{ProofSource::Unknown};
    service::OwnBankReceipt receipt{};
};
enum class InventoryKind { Item, Consumable };
struct InventorySlot {
    InventoryKind kind{InventoryKind::Item};
    int id{-1};
    unsigned count{};
};
struct Inventory {
    Authority authority{};
    bool known{}, complete{};
    std::array<InventorySlot, 64> slots{};
    std::size_t count{};
};
struct FieldContext {
    Authority authority{};
    bool known{}, static_npc_quiet{};
    Position position{};
};
struct Facts {
    Authority authority{};
    bool self_known{}, life_known{}, health_known{}, position_known{};
    bool bag_known{}, gold_known{}, channel_known{}, stun_known{}, stunned{};
    std::array<bool, 3> permissions_known{}; // M,A,C, separately known
    std::uint8_t restrictions_known_mask{}, restrictions{};
    std::array<bool, 2> cooldown_known{};
    std::array<int, 2> cooldown{-1, -1};
    bool input_unused_known{}, input_unused{};
    // A sound dated producer must track the active episode/lifecycle. An alive
    // snapshot alone cannot exclude death/respawn or an intervening service.
    bool episode_continuity_known{}, same_episode{};
    Inventory inventory{};
    FieldContext field{};
};
struct NextBasicStart {
    Authority authority{};
    PrimitiveSchema schema{};
    Scope scope{Scope::Unknown};
    bool known{}, scoped_assumptions_certified{}, profile_supported{};
    bool whole_group_harmless{}, static_quiet_origin{}, static_quiet_landing{};
    bool bounded_continuation_prepared{};
    std::uint64_t next_episode{};
    service::Contract contract{};
    service_micro::Frame first{};
    service_micro::Request primitive{};
};
struct HeldResources {
    Authority authority{};
    int gold{}, bag{};
    Inventory inventory{};
};
struct Decision {
    Status status{Status::NoCommand};
    Reason reason{Reason::None};
    Capability capability{Capability::None};
    PrimitiveSchema schema{};
    TicketKind ticket_kind{TicketKind::None};
    std::uint64_t ticket{};
    unsigned observation_calls_left{};
    int deadline_round{-1};
    HeldResources held{};
    FieldContext field{};
};

// Independent caller-side entry facts for the existing Nominate operation.
// This checks current entry only: it is not a future-service or no-foe proof.
struct NominationEntry {
    Authority authority{};
    Position position{};
    bool world_known{}, world_ready{}, position_known{};
    bool static_quiet_known{}, static_npc_quiet{};
    bool candidate_known{}, candidate_present{};
    bool bag_known{};
    int bag{};
    bool input_unused_known{}, input_unused{};
};
inline bool nomination_entry_allowed(const Decision& observed, Authority now,
                                     const NominationEntry& entry) {
    return service_micro::valid(now)
        && observed.status == Status::ObservedReadyForBasic
        && observed.ticket_kind == TicketKind::Observed && observed.ticket
        && observed.held.authority == now && observed.held.bag == 0
        && observed.field.known && observed.field.authority == now
        && observed.field.static_npc_quiet
        && entry.authority == now && entry.world_known && entry.world_ready
        && entry.position_known && entry.position == observed.field.position
        && entry.static_quiet_known && entry.static_npc_quiet
        && entry.candidate_known && entry.candidate_present
        && entry.bag_known && entry.bag == 0
        && entry.input_unused_known && entry.input_unused;
}

class Endpoint {
    Phase phase_{Phase::Idle};
    Episode episode_{};
    int owner_{-1};
    Authority latest_{}, bank_at_{}, ready_at_{}, last_ticket_input_{};
    std::uint64_t highest_episode_{}, highest_capture_{}, highest_bank_{}, ticket_{};
    unsigned calls_{};
    Reason failed_{Reason::None};
    Capability capability_{Capability::None};
    PrimitiveSchema schema_{};
    TicketKind ticket_kind_{TicketKind::None};
    HeldResources held_{};
    FieldContext field_{};
    service::Contract next_{};

    Decision answer(Status s, Reason r = Reason::None) const {
        Decision d; d.status = s; d.reason = r; d.capability = capability_;
        d.schema = schema_; d.ticket_kind = ticket_kind_;
        d.ticket = ticket_kind_ == TicketKind::None ? 0 : ticket_;
        d.observation_calls_left = calls_ < episode_.max_observation_calls
            ? episode_.max_observation_calls - calls_ : 0;
        d.deadline_round = episode_.deadline_round; d.held = held_; d.field = field_; return d;
    }
    Decision fail(Reason r, Capability c = Capability::None) {
        phase_ = Phase::Failed; failed_ = r; capability_ = c;
        bank_at_ = ready_at_ = {}; ticket_kind_ = TicketKind::None;
        held_ = {}; field_ = {}; next_ = {}; schema_ = {};
        return answer(c == Capability::None ? Status::Failed : Status::NeedsCapability, r);
    }
    bool current(Authority a) {
        if (!service_micro::valid(a) || a.owner != owner_) return false;
        if (service_micro::valid(latest_)) {
            if (a.round < latest_.round || a.input_generation < latest_.input_generation) return false;
            if ((a.round == latest_.round) != (a.input_generation == latest_.input_generation)) return false;
            if (service_micro::same_input(a, latest_) && a.view_revision < latest_.view_revision) return false;
        }
        latest_ = a; return true;
    }
    static bool inventory_valid(const Inventory& v, Authority a) {
        if (v.authority != a || !v.known || !v.complete || v.count > v.slots.size()) return false;
        for (std::size_t i = 0; i < v.count; ++i) {
            const auto& x = v.slots[i];
            if (x.id < 0 || !x.count || (x.kind != InventoryKind::Item && x.kind != InventoryKind::Consumable)) return false;
            if (i) {
                const auto& p = v.slots[i-1];
                if (static_cast<int>(x.kind) < static_cast<int>(p.kind)
                    || (x.kind == p.kind && x.id <= p.id)) return false;
            }
        }
        return true;
    }
    static bool next_contract_valid(const service::Contract& c, int owner) {
        if (c.owner != owner || c.group < 0 || c.start_from_receipt
            || !c.member_count || c.member_count > c.member_ids.size()) return false;
        for (std::size_t i = 0; i < c.member_count; ++i) {
            if (c.member_ids[i] < 0) return false;
            for (std::size_t j = 0; j < i; ++j) if (c.member_ids[i] == c.member_ids[j]) return false;
        }
        return true;
    }
    static bool first_start(const service::Frame& f, const Facts& o, const NextBasicStart& n) {
        const auto& m = n.first;
        if (!n.known || n.authority != f.authority || n.next_episode == 0
            || !n.profile_supported || !n.whole_group_harmless || !n.bounded_continuation_prepared
            || n.scope != Scope::NPCOnlyNoFoes || !n.scoped_assumptions_certified
            || !n.static_quiet_origin || !f.static_hazard_excluded
            || !next_contract_valid(n.contract, f.authority.owner)) return false;
        if (m.authority != f.authority || !m.observed_self || !m.observed_readiness
            || m.position != f.position || m.hp != f.hp || m.alive != f.alive
            || m.stunned != o.stunned || m.channel || !m.observed_domain || !m.domain_supported
            || m.can_move != f.can_move || m.can_attack != f.can_attack
            || (o.permissions_known[2] && m.can_cast != f.can_cast)) return false;
        if (n.primitive.kind == service_micro::Kind::Basic) {
            bool member = false;
            for (std::size_t i = 0; i < n.contract.member_count; ++i)
                member |= n.contract.member_ids[i] == n.primitive.target_id;
            return member && m.observed_target && m.target_id == n.primitive.target_id
                && m.target_visible && m.target_alive && std::isfinite(m.target_hp)
                && m.target_hp > 0 && m.basic_in_range;
        }
        if (n.primitive.kind == service_micro::Kind::Move) {
            const auto dx = std::int64_t(n.primitive.endpoint.x) - f.position.x;
            const auto dy = std::int64_t(n.primitive.endpoint.y) - f.position.y;
            if (dx < -1 || dx > 1 || dy < -1 || dy > 1 || (!dx && !dy)) return false;
            return n.static_quiet_landing && m.observed_move && m.move_endpoint == n.primitive.endpoint
                && m.move_legal && (m.move_landing == service_micro::MoveLanding::KnownFree
                    || (m.move_landing == service_micro::MoveLanding::MayRefuse && m.refusal_prefix_supported));
        }
        return false; // Cast/reset has no represented next-operator semantics
    }

public:
    Endpoint() = default;
    Endpoint(const Endpoint&) = delete;
    Endpoint& operator=(const Endpoint&) = delete;
    Endpoint(Endpoint&&) = delete;
    Endpoint& operator=(Endpoint&&) = delete;
    Phase phase() const { return phase_; }

    bool seal(Authority now, const Episode& e) {
        if (phase_ == Phase::AwaitBank || phase_ == Phase::BankVerified || phase_ == Phase::Ready) return false;
        if (!service_micro::valid(now) || now != e.opened_at || e.sealed.owner != now.owner
            || !e.id || e.id <= highest_episode_ || !e.capture.present || !e.capture.verified
            || e.capture.group != e.sealed.group || e.capture.group < 0
            || e.capture.gems < e.sealed.min_own_receipt || e.capture.gems <= 0
            || !e.capture.serial || e.capture.serial <= highest_capture_
            || e.bank_serial_baseline < highest_bank_ || !e.max_observation_calls
            || e.deadline_round <= now.round || (owner_ >= 0 && owner_ != now.owner)) return false;
        owner_ = now.owner;
        if (!current(now)) return false;
        episode_ = e; highest_episode_ = e.id; highest_capture_ = e.capture.serial;
        calls_ = 0; bank_at_ = ready_at_ = {}; held_ = {}; field_ = {};
        next_ = {}; schema_ = {}; ticket_kind_ = TicketKind::None;
        capability_ = Capability::None; failed_ = Reason::None;
        phase_ = Phase::AwaitBank; return true;
    }

private:
    Decision evaluate(Authority now, const service::Frame& f, const Facts& o,
                      PrimitiveSchema schema, const OwnBankEvidence* proof) {
        if (phase_ == Phase::Failed) return answer(capability_ == Capability::None
            ? Status::Failed : Status::NeedsCapability, failed_);
        if (phase_ == Phase::Idle || phase_ == Phase::HandedOff || phase_ == Phase::Released)
            return answer(Status::NoCommand);
        if (phase_ == Phase::Ready) {
            if (now != ready_at_) return fail(Reason::StaleTicket);
            return answer(Status::NoCommand); // no second certificate/ticket
        }
        if (++calls_ > episode_.max_observation_calls) return fail(Reason::WaitBound);
        if (!current(now))
            return answer(Status::NeedObservation, Reason::StaleAuthority);
        if (now.round > episode_.deadline_round) return fail(Reason::WaitBound);
        if (f.authority != now || o.authority != now || !f.fresh)
            return answer(Status::NeedObservation, Reason::StaleAuthority);
        if (o.life_known && !f.alive) return fail(Reason::ActorDead);
        if (!o.self_known || !o.life_known || !o.bag_known || !o.channel_known)
            return answer(Status::NeedObservation, Reason::PartialObservation);
        if (!f.alive) return fail(Reason::ActorDead);
        if (f.bag != 0) return fail(Reason::Loaded);
        if (f.channel != service::Channel::None) return answer(Status::NeedObservation, Reason::NotReady);
        if (phase_ == Phase::AwaitBank) {
            if (!proof) return answer(Status::NeedObservation, Reason::MissingBank);
            const auto& r = proof->receipt;
            const auto& actual = f.own_bank_receipt;
            if (proof->source != ProofSource::ActorJoinedOwnCompletion || proof->authority != now
                || proof->owner != owner_ || proof->group != episode_.capture.group
                || proof->episode != episode_.id || proof->capture_serial != episode_.capture.serial
                || !r.present || !r.verified || r.gems != episode_.capture.gems
                || r.serial <= episode_.bank_serial_baseline || r.serial <= highest_bank_
                || actual.present != r.present || actual.verified != r.verified
                || actual.gems != r.gems || actual.serial != r.serial)
                return fail(Reason::WrongBankProvenance);
            highest_bank_ = r.serial; bank_at_ = now; phase_ = Phase::BankVerified;
            return answer(Status::BankVerified); // NEVER Ready on receipt input
        }
        if (now.round <= bank_at_.round || now.input_generation <= bank_at_.input_generation)
            return answer(Status::NeedObservation, Reason::NotLaterInput);
        if (!o.episode_continuity_known || !o.same_episode || !f.own_bank_receipt.present
            || !f.own_bank_receipt.verified || f.own_bank_receipt.serial != highest_bank_
            || f.own_bank_receipt.gems != episode_.capture.gems)
            return fail(Reason::WrongBankProvenance);
        if (service_micro::valid(last_ticket_input_)
            && service_micro::same_input(now, last_ticket_input_))
            return answer(Status::NoCommand, Reason::InputTicketConsumed);
        if (episode_.sealed.require_shop) return fail(Reason::RequiredShop, Capability::Buy);
        if (episode_.terminal != Terminal::ReadyFieldHoldCash)
            return fail(Reason::LegacyTerminal, Capability::ReturnToSpawn);
        if (!represented(schema)) return fail(Reason::UnsupportedSignature);
        if (!o.health_known || !o.position_known || !o.gold_known || !o.stun_known
            || !o.input_unused_known || !inventory_valid(o.inventory, now)
            || !o.field.known || o.field.authority != now || o.field.position != f.position
            || o.field.static_npc_quiet != f.static_hazard_excluded)
            return answer(Status::NeedObservation, Reason::PartialObservation);
        if (!std::isfinite(f.hp) || !std::isfinite(f.max_hp) || f.max_hp <= 0 || f.gold < 0)
            return answer(Status::NeedObservation, Reason::PartialObservation);
        if (f.hp != f.max_hp || o.stunned || f.restricted || !o.input_unused)
            return answer(Status::NeedObservation, Reason::NotReady);
        const bool permissions[3]{f.can_move, f.can_attack, f.can_cast};
        for (unsigned i = 0; i < 3; ++i) if (schema.permissions & (1u << i)) {
            if (!o.permissions_known[i]) return answer(Status::NeedObservation, Reason::UnknownDependency);
            if (!permissions[i]) return answer(Status::NeedObservation, Reason::NotReady);
        }
        if ((o.restrictions_known_mask & schema.restrictions) != schema.restrictions)
            return answer(Status::NeedObservation, Reason::UnknownDependency);
        if (o.restrictions & schema.restrictions) return answer(Status::NeedObservation, Reason::NotReady);
        for (unsigned i = 0; i < 2; ++i) if (schema.cooldowns & (1u << i)) {
            if (!o.cooldown_known[i] || o.cooldown[i] < 0)
                return answer(Status::NeedObservation, Reason::UnknownDependency);
            if (o.cooldown[i] != 0) return answer(Status::NeedObservation, Reason::NotReady);
        }
        // Internal body-state predicate only. No safety, foe absence, target,
        // landing, prepared route or future service feasibility follows.
        return answer(Status::ObservedReadyForBasic);
    }
    Decision certify(Authority now, const service::Frame& f, const Facts& o,
                     PrimitiveSchema schema, TicketKind kind) {
        if (ticket_ == std::numeric_limits<std::uint64_t>::max()) return fail(Reason::WaitBound);
        ready_at_ = now; schema_ = schema; ticket_kind_ = kind; field_ = o.field;
        last_ticket_input_ = now;
        held_ = {now, f.gold, f.bag, o.inventory}; ++ticket_;
        phase_ = Phase::Ready;
        return answer(kind == TicketKind::Observed ? Status::ObservedReadyForBasic
                                                   : Status::ReadyForNextOperator);
    }

public:
    // Observed postcondition only. Danger/visible/hidden foes do not negate
    // observed M+A readiness. The known current field VALUE is retained even
    // when false; it grants no safe hold or permission to begin another service.
    Decision observe_readiness(Authority now, const service::Frame& f, const Facts& o,
                               PrimitiveSchema schema = bare_move_basic(),
                               const OwnBankEvidence* proof = nullptr) {
        auto d = evaluate(now, f, o, schema, proof);
        if (d.status != Status::ObservedReadyForBasic) return d;
        return certify(now, f, o, schema, TicketKind::Observed);
    }

    // Distinct, stronger prospective API. It is NOT the observed terminal API.
    Decision observe_prospective(Authority now, const service::Frame& f, const Facts& o,
                                 const NextBasicStart& n, const OwnBankEvidence* proof = nullptr) {
        auto d = evaluate(now, f, o, n.schema, proof);
        if (d.status != Status::ObservedReadyForBasic) return d;
        if (!first_start(f, o, n) || n.next_episode <= episode_.id)
            return answer(Status::NeedObservation, Reason::InvalidNextStart);
        next_ = n.contract;
        return certify(now, f, o, n.schema, TicketKind::Prospective);
    }

    Decision release(Authority now, std::uint64_t ticket) {
        if (phase_ == Phase::Ready && ticket_kind_ != TicketKind::Observed)
            return answer(Status::NoCommand, Reason::WrongTicketKind);
        if (phase_ != Phase::Ready || now != ready_at_ || !ticket || ticket != ticket_)
            return phase_ == Phase::Released ? answer(Status::NoCommand, Reason::StaleTicket)
                : fail(Reason::StaleTicket);
        phase_ = Phase::Released; return answer(Status::Released);
    }

    // Declining a current observed ticket leaves the existing Interpreter
    // untouched. It preserves the original episode, bank, deadline and depleted
    // polling allowance; this physical input cannot mint another ticket.
    Decision continue_existing(Authority now, std::uint64_t ticket) {
        if (phase_ == Phase::Ready && ticket_kind_ != TicketKind::Observed)
            return answer(Status::NoCommand, Reason::WrongTicketKind);
        if (phase_ != Phase::Ready || now != ready_at_ || !ticket || ticket != ticket_)
            return fail(Reason::StaleTicket);
        phase_ = Phase::BankVerified; ready_at_ = {}; ticket_kind_ = TicketKind::None;
        held_ = {}; field_ = {}; schema_ = {}; next_ = {};
        return answer(Status::NoCommand, Reason::CallerContinues);
    }

    // Conservative episode cancellation, including an actual-death callback
    // from the sound native producer. This does not itself attest death/cause.
    Decision invalidate(Authority now, Reason why = Reason::EpisodeInvalidated) {
        if (phase_ == Phase::Idle || phase_ == Phase::Released || phase_ == Phase::HandedOff)
            return answer(Status::NoCommand);
        if (!current(now)) return fail(Reason::StaleAuthority);
        return fail(why);
    }

    Decision begin_next(Authority now, std::uint64_t ticket, service::Interpreter& next) {
        if (phase_ == Phase::Ready && ticket_kind_ != TicketKind::Prospective)
            return answer(Status::NoCommand, Reason::WrongTicketKind);
        if (phase_ != Phase::Ready || now != ready_at_ || !ticket || ticket != ticket_)
            return phase_ == Phase::HandedOff ? answer(Status::NoCommand, Reason::StaleTicket)
                : fail(Reason::StaleTicket);
        // Metadata only. A fresh global native primitive gateway must still
        // authorize every eventual action; this cannot reset that gateway.
        if (!next.begin(next_)) return fail(Reason::NextBeginRefused);
        phase_ = Phase::HandedOff; return answer(Status::HandedOff);
    }
};
} // namespace wb::ready_field
