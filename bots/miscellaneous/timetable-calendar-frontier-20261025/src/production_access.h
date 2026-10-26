#pragma once
// Pure, conditional access facts and acknowledgment bookkeeping; no actuator,
// safety, future allowance, hidden-state, profitability or arrival guarantee.
#include "service_model.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace tt::production_access {
using Point=mb::service::Point;
struct Stamp
{
    std::uint64_t epoch{};
    std::int64_t round{};
    Point origin;
    friend bool operator==(const Stamp&,const Stamp&)=default;
};
enum class Fact { Unknown, No, Yes };
enum class Ordinary { Unknown, Reachable, Blocked }; // exact supplied frozen graph only
enum class Life { Unknown, Alive, Dead };
struct Member { Point pos; bool visible{}, hp_exact{}; Life life{Life::Unknown}; float hp{}; };
struct Landing { Point pos; bool visible{}; Fact empty{Fact::Unknown}, legal_dash{Fact::Unknown}; };
struct Frame
{
    Stamp stamp;
    std::span<const Member> members;
    bool members_complete{};
    Ordinary ordinary{Ordinary::Unknown};
    int attack_range{};
    bool attack_ready{}, has_dash{}, cast_ready{};
    int dash_range{}, dash_cooldown{};
    Landing landing;
};
enum class Residual { Unknown, Living, Clear };
enum class Work { Unknown, None, InRangeCandidate, TemporarilyUnavailable, OutOfRange };
enum class Entry { Unknown, NeedClear, OrdinaryGraphCandidate, DashCandidate,
                   TemporarilyUnavailable, ConditionalOrdinaryBlocked };
struct Assessment
{
    Residual residual{Residual::Unknown};
    Work work{Work::Unknown};
    Ordinary ordinary{Ordinary::Unknown};
    Entry entry{Entry::Unknown};
};

// actual is the caller's post-observation fence. Equality cannot establish that
// fence's provenance: the caller still guards every actual command anew.
inline Assessment assess(const Frame& frame,const Stamp& actual)
{
    Assessment result;
    if (frame.stamp!=actual || actual.round<0 || frame.members.empty()) return result;
    result.ordinary=frame.ordinary;
    bool exact=frame.members_complete, living=false, in_range=false, landing_member=false;
    for (const auto& member:frame.members)
    {
        landing_member|=member.pos==frame.landing.pos;
        const bool known=member.visible && member.hp_exact && std::isfinite(member.hp) &&
            member.hp>=0 && member.life!=Life::Unknown &&
            ((member.life==Life::Alive)==(member.hp>0));
        exact&=known;
        if (known && member.life==Life::Alive)
        {
            living=true;
            in_range|=frame.attack_range>0 &&
                mb::service::distance(actual.origin,member.pos)<=frame.attack_range;
        }
    }
    result.residual=exact?(living?Residual::Living:Residual::Clear):Residual::Unknown;
    // A known target can supply geometric work despite unknown siblings. This
    // says nothing about prospective group wake, exposure, legal LOS or damage.
    if (in_range) result.work=frame.attack_ready?Work::InRangeCandidate:Work::TemporarilyUnavailable;
    else if (living && exact) result.work=Work::OutOfRange;
    else if (exact) result.work=Work::None;
    if (result.residual==Residual::Living) { result.entry=Entry::NeedClear; return result; }
    if (result.residual!=Residual::Clear) return result;
    if (frame.ordinary==Ordinary::Reachable)
    { result.entry=Entry::OrdinaryGraphCandidate; return result; }
    if (!frame.has_dash)
    {
        if (frame.ordinary==Ordinary::Blocked) result.entry=Entry::ConditionalOrdinaryBlocked;
        return result;
    }
    const auto distance=mb::service::distance(actual.origin,frame.landing.pos);
    if (!landing_member || !frame.landing.visible || frame.landing.empty!=Fact::Yes ||
        distance<1 || distance>frame.dash_range) return result;
    if (!frame.cast_ready || frame.dash_cooldown!=0) result.entry=Entry::TemporarilyUnavailable;
    else if (frame.landing.legal_dash==Fact::Yes) result.entry=Entry::DashCandidate;
    return result;
}

// Entry during owner round k first starts capture at k+1. With c capture ticks,
// payout is at k+c+1. A recall started at s with r owner-end ticks arrives at s+r.
// Capture precedes channels at that boundary. This is only phase arithmetic for
// uninterrupted uncontested occupation, uncancelled recall and current settings.
inline std::optional<std::int64_t> recall_start(std::int64_t entry_round,int capture_ticks,int recall_ticks)
{
    if (entry_round<0 || capture_ticks<1 || recall_ticks<1) return std::nullopt;
    const auto delay=std::max<std::int64_t>(1,static_cast<std::int64_t>(capture_ticks)+1-recall_ticks);
    if (entry_round>std::numeric_limits<std::int64_t>::max()-delay) return std::nullopt;
    return entry_round+delay; // never advises immediate recall on fresh solo entry
}

enum class Phase { Work, NeedEntry, Hold, RecallEligible, Receipt, NeedBank, Renew };
enum class EntryMethod { Ordinary, Dash };
struct Acknowledgment
{
    Stamp stamp;
    int group{-1};
    bool clear_observed{}, entry_observed{}, dash_applied{};
    bool region_observed{}, in_region{}, uncontested{}, uninterrupted{};
    bool receipt_observed{}, carried_gems_observed{}, bank_observed{}, refill_observed{};
};

// One acknowledged boundary per call. Every *_observed is an actual event/state
// for this operation/group, never a deadline, intended command or bag prediction.
// Refill is actual post-bank own-resource renewal, not invented camp respawn.
class Cycle
{
public:
    Cycle(int group,int capture_ticks,int recall_ticks,EntryMethod method)
        : group_{group}, capture_ticks_{capture_ticks}, recall_ticks_{recall_ticks}, method_{method} {}
    Phase phase() const { return phase_; }
    std::uint64_t completed() const { return completed_; }
    std::optional<std::int64_t> entered_round() const { return entered_; }
    Phase advance(const Acknowledgment& ack,const Stamp& actual)
    {
        if (ack.group!=group_ || ack.stamp!=actual || actual.round<0 || actual.round<last_round_) return phase_;
        last_round_=actual.round;
        switch (phase_)
        {
        case Phase::Work:
            if (ack.clear_observed) phase_=Phase::NeedEntry;
            break;
        case Phase::NeedEntry:
            if (ack.clear_observed && ack.entry_observed && ack.region_observed && ack.in_region &&
                ack.uncontested && (method_==EntryMethod::Ordinary || ack.dash_applied))
            { entered_=actual.round; phase_=Phase::Hold; }
            break;
        case Phase::Hold: case Phase::RecallEligible:
            // An observed receipt wins over present region absence: capture may
            // pay before recall arrives at this same owner-start boundary.
            if (ack.receipt_observed) { phase_=Phase::Receipt; break; }
            if (!ack.clear_observed || !ack.region_observed || !ack.in_region || !ack.uncontested || !ack.uninterrupted)
            { entered_.reset(); phase_=Phase::NeedEntry; break; }
            if (entered_)
            {
                const auto start=recall_start(*entered_,capture_ticks_,recall_ticks_);
                phase_=start && actual.round>=*start?Phase::RecallEligible:Phase::Hold;
            }
            break;
        case Phase::Receipt:
            if (ack.carried_gems_observed) phase_=Phase::NeedBank;
            break;
        case Phase::NeedBank:
            if (ack.bank_observed) phase_=Phase::Renew;
            break;
        case Phase::Renew:
            if (ack.refill_observed) { ++completed_; entered_.reset(); phase_=Phase::Work; }
            break;
        }
        return phase_;
    }
private:
    int group_,capture_ticks_,recall_ticks_;
    EntryMethod method_;
    Phase phase_{Phase::Work};
    std::optional<std::int64_t> entered_;
    std::int64_t last_round_{-1};
    std::uint64_t completed_{};
};
} // namespace tt::production_access
