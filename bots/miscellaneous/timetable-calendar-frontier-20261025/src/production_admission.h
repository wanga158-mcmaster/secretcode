#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

// SDK-free retained nominations. A selection/admission is never permission for a
// native command: the adapter still owns fresh body, target, path and epoch guards.
namespace tt::production_admission {
enum class Family { Work,Capture,Observe,Release };
enum class Primitive { None,Basic,Cast,Move,Word,Observe,Release };
enum class Life { Unknown,Alive,Reward,Respawning };
struct Point { int x{-1},y{-1}; friend bool operator==(Point,Point)=default; };
struct Subject
{
    // fragment is a stable operator-family identity, never a query/epoch id.
    int group{-1}; Family family{Family::Work}; std::uint64_t fragment{};
    Point origin,perch; Primitive primitive{Primitive::None};
    friend bool operator==(const Subject&,const Subject&)=default;
};
enum Dependency : unsigned { Body=1,Geometry=2,Members=4,Visibility=8,Model=16,All=31 };
struct Premises
{
    // Canonical semantic stamps supplied by the adapter, not clocks/cursors.
    // Include every dependency the corresponding failure actually used.
    std::uint64_t body{},geometry{},members{},visibility{},model{};
    friend bool operator==(Premises,Premises)=default;
};
inline bool same(Premises a,Premises b,unsigned dependencies)
{
    return (!(dependencies&Body) || a.body==b.body) &&
        (!(dependencies&Geometry) || a.geometry==b.geometry) &&
        (!(dependencies&Members) || a.members==b.members) &&
        (!(dependencies&Visibility) || a.visibility==b.visibility) &&
        (!(dependencies&Model) || a.model==b.model);
}
struct Nomination
{
    Subject subject; Premises premises; unsigned dependencies{All};
    float gem_rate{-1},gold_rate{-1},time{};
    // Audit provenance only. Neither field belongs to a refutation key.
    int dated_epoch{-1}; std::uint64_t query{};
};
struct LifeReading { Life state{Life::Unknown}; int epoch{-1},timer{-1}; };
struct LifeProof
{
    Life state{Life::Unknown}; std::uint64_t generation{};
    int last_read_epoch{-1},timer{-1};
};
enum class Store { Stored,Invalid,Capacity,Stale };
enum class Refutation { RejectedFragment,UnknownFragment,UnobservedLifecycle };
enum class Scope { ExactFragment,StandingAtPerch };
enum class Status { NeedOffers,NeedCompute,Nominee,Resolver,Reprice,Waiting,Unknown,Capacity,Stale };
enum class Wait { NoOffers,AbsentWork,RefutedFamilies,NeedsObservation };
struct Selection
{
    Status status{Status::NeedOffers}; std::size_t slot{std::numeric_limits<std::size_t>::max()};
    int group{-1}; Wait wait{Wait::NoOffers};
};

    // Capacities are caller-declared representation bounds, never physical game
// limits. Overflow is explicit Unknown/Capacity; it must not become an empty menu.
template<std::size_t MaxGroups,std::size_t MaxOffers,std::size_t MaxRefutations=MaxOffers>
class Menu
{
    struct Entry { bool present{},reprice{},capacity_deferred{}; Nomination offer; std::uint64_t life{}; };
    struct Failure
    {
        Subject subject; Premises premises; unsigned dependencies{};
        std::uint64_t life{}; Refutation kind{}; Scope scope{};
    };
public:
    explicit Menu(std::size_t groups=MaxGroups):groups_{groups}
    { if (groups>MaxGroups) capacity_=true; }
    const LifeProof* life(int group) const
    { return valid_group(group)?&life_[static_cast<std::size_t>(group)]:nullptr; }

    // The adapter certifies this is an actual selected lifecycle read. Epoch
    // only rejects older readback: its advancement/expiry never manufactures life.
    Status observe(int group,LifeReading reading)
    {
        if (!valid_group(group) || reading.epoch<0) return Status::Unknown;
        auto& proof=life_[static_cast<std::size_t>(group)];
        if (reading.epoch<proof.last_read_epoch) return Status::Stale;
        proof.last_read_epoch=reading.epoch; proof.timer=reading.timer;
        if (reading.state==Life::Unknown) return Status::Nominee;
        if (reading.state==proof.state)
        {
            // A selected positive read can resolve an observation omission even
            // when an older cached lifecycle had the same value. It never clears
            // rejected or unsupported primitive/perch/model families.
            std::size_t retained=0;
            for (std::size_t i=0;i<failure_count_;++i)
                if (failures_[i].subject.group!=group || failures_[i].kind!=Refutation::UnobservedLifecycle)
                    failures_[retained++]=failures_[i];
            if (retained!=failure_count_) { failure_count_=retained; restart(); }
            return Status::Nominee;
        }
        if (proof.generation==std::numeric_limits<std::uint64_t>::max())
        { capacity_=true; return Status::Capacity; }
        proof.state=reading.state; ++proof.generation;
        // A proved source transition discharges only this group's old-life
        // failures. Repeated Alive and elapsed timers discharge none.
        std::size_t retained=0;
        for (std::size_t i=0;i<failure_count_;++i)
            if (failures_[i].subject.group!=group || failures_[i].life==proof.generation)
                failures_[retained++]=failures_[i];
        failure_count_=retained; restart();
        return Status::Nominee;
    }
    Store put(std::size_t slot,const Nomination& offer)
    {
        if (slot>=MaxOffers) { capacity_=true; return Store::Capacity; }
        if (!valid_group(offer.subject.group) || (offer.dependencies&~All) ||
            !std::isfinite(offer.gem_rate) || !std::isfinite(offer.gold_rate) ||
            !std::isfinite(offer.time) || offer.time<0)
        { unknown_=true; return Store::Invalid; }
        entries_[slot]={true,false,false,offer,life_[static_cast<std::size_t>(offer.subject.group)].generation};
        restart(); return Store::Stored;
    }
    // A new producer may replace dated offers without erasing physical proofs or
    // fragment refutations. Ordinary fresh rejection should retain this whole menu.
    void clear_nominations()
    { for (auto& entry:entries_) entry.present=false; sealed_=false; unknown_=false; restart(); }
    void seal() { sealed_=true; restart(); }

    Store refute(std::size_t slot,Premises actual,unsigned dependencies,Refutation kind,
                 Scope scope=Scope::ExactFragment)
    {
        if (slot>=MaxOffers || !entries_[slot].present || (dependencies&~All)) return Store::Invalid;
        const auto& entry=entries_[slot]; const auto group=static_cast<std::size_t>(entry.offer.subject.group);
        if (entry.life!=life_[group].generation) return Store::Stale;
        Failure next{entry.offer.subject,actual,dependencies,life_[group].generation,kind,scope};
        for (std::size_t i=0;i<failure_count_;++i)
        {
            auto& prior=failures_[i];
            if (same_subject(prior.subject,next.subject,scope) && prior.scope==scope &&
                prior.life==next.life && prior.dependencies==dependencies &&
                same(prior.premises,actual,dependencies))
            { prior.kind=kind; restart(); return Store::Stored; }
        }
        if (failure_count_==MaxRefutations)
        { capacity_=true; entries_[slot].capacity_deferred=true; restart(); return Store::Capacity; }
        failures_[failure_count_++]=next; restart(); return Store::Stored;
    }

    // Fresh lifecycle + changed semantic facts refine only this dated nominee.
    // Unknown observation consumes no physical Work and invents no absence proof.
    // Resolver admission says nothing about actual ownership/release/completion.
    Selection admit(std::size_t slot,LifeReading reading,Premises actual)
    {
        if (slot>=MaxOffers || !entries_[slot].present) return {Status::Unknown};
        auto& entry=entries_[slot]; const int group=entry.offer.subject.group;
        const auto observed=observe(group,reading);
        if (observed==Status::Stale || observed==Status::Capacity || observed==Status::Unknown)
            return {observed,slot,group};
        if (entry.capacity_deferred) return {Status::Capacity,slot,group};
        const auto family=entry.offer.subject.family;
        if (reading.state==Life::Unknown && family!=Family::Observe && family!=Family::Release)
        {
            if (refute(slot,actual,entry.offer.dependencies,Refutation::UnobservedLifecycle)==Store::Capacity)
                return {Status::Capacity,slot,group};
            return {Status::Waiting,slot,group,Wait::NeedsObservation};
        }
        if ((family==Family::Work && reading.state!=Life::Alive) ||
            (family==Family::Capture && reading.state!=Life::Reward))
        { restart(); return {Status::Waiting,slot,group,Wait::AbsentWork}; }
        if (entry.life!=life_[static_cast<std::size_t>(group)].generation ||
            !same(entry.offer.premises,actual,entry.offer.dependencies))
        { entry.reprice=true; restart(); return {Status::Reprice,slot,group}; }
        if (blocked(entry)) return {Status::Waiting,slot,group,Wait::RefutedFamilies};
        return {family==Family::Observe || family==Family::Release?Status::Resolver:Status::Nominee,slot,group};
    }

    // One slot consumes one unit. Completed answers remain retained until an
    // actual offer/proof/refutation changes; polling does not restart the cursor.
    Selection advance(std::size_t units)
    {
        if (!sealed_) return {capacity_?Status::Capacity:Status::NeedOffers};
        while (units && cursor_<MaxOffers)
        {
            --units; const auto slot=cursor_++; const auto& entry=entries_[slot];
            if (!entry.present) continue;
            saw_offer_=true;
            if (entry.capacity_deferred) { saw_unknown_=true; continue; }
            if (suppressed_life(entry)) { absent_group_=entry.offer.subject.group; continue; }
            const auto failure=blocked(entry);
            if (failure)
            { saw_refutation_=true; saw_unknown_|=*failure!=Refutation::RejectedFragment; continue; }
            if (best_==none || better(entry.offer,entries_[best_].offer)) best_=slot;
        }
        if (cursor_<MaxOffers) return {Status::NeedCompute};
        if (best_!=none)
        {
            const auto& entry=entries_[best_]; const auto family=entry.offer.subject.family;
            const bool reprice=entry.reprice || entry.life!=life_[static_cast<std::size_t>(entry.offer.subject.group)].generation;
            // Capacity plus a slot exposes a retained fallback nomination; it
            // does not claim the represented menu is complete. The adapter must
            // handle that model limit explicitly and still fresh-admit the slot.
            return {capacity_?Status::Capacity:reprice?Status::Reprice:family==Family::Observe || family==Family::Release?
                Status::Resolver:Status::Nominee,best_,entry.offer.subject.group};
        }
        return {capacity_?Status::Capacity:unknown_ || saw_unknown_?Status::Unknown:Status::Waiting,none,absent_group_,
            absent_group_>=0?Wait::AbsentWork:saw_refutation_?Wait::RefutedFamilies:
            saw_offer_?Wait::NeedsObservation:Wait::NoOffers};
    }
private:
    static constexpr std::size_t none=std::numeric_limits<std::size_t>::max();
    bool valid_group(int group) const
    { return group>=0 && static_cast<std::size_t>(group)<groups_ && static_cast<std::size_t>(group)<MaxGroups; }
    static bool better(const Nomination& a,const Nomination& b)
    { return a.gem_rate!=b.gem_rate?a.gem_rate>b.gem_rate:a.gold_rate>b.gold_rate; }
    static bool same_subject(Subject a,Subject b,Scope scope)
    {
        // The departure pose is irrelevant to a proved rejection of the same
        // standing primitive at P. An ingress/path failure keeps its exact root.
        if (scope==Scope::StandingAtPerch) { a.origin={}; b.origin={}; }
        return a==b;
    }
    bool suppressed_life(const Entry& entry) const
    {
        const auto state=life_[static_cast<std::size_t>(entry.offer.subject.group)].state;
        return (entry.offer.subject.family==Family::Work && state!=Life::Unknown && state!=Life::Alive) ||
            (entry.offer.subject.family==Family::Capture && state!=Life::Unknown && state!=Life::Reward);
    }
    const Refutation* blocked(const Entry& entry) const
    {
        const auto life=life_[static_cast<std::size_t>(entry.offer.subject.group)].generation;
        for (std::size_t i=0;i<failure_count_;++i)
            if (same_subject(failures_[i].subject,entry.offer.subject,failures_[i].scope) && failures_[i].life==life &&
                same(failures_[i].premises,entry.offer.premises,failures_[i].dependencies)) return &failures_[i].kind;
        return nullptr;
    }
    void restart()
    { cursor_=0; best_=none; absent_group_=-1; saw_offer_=saw_refutation_=saw_unknown_=false; }
    std::array<LifeProof,MaxGroups> life_{};
    std::array<Entry,MaxOffers> entries_{};
    std::array<Failure,MaxRefutations> failures_{};
    std::size_t groups_{},failure_count_{},cursor_{},best_{none};
    bool sealed_{},capacity_{},unknown_{},saw_offer_{},saw_refutation_{},saw_unknown_{};
    int absent_group_{-1};
};
} // namespace tt::production_admission
