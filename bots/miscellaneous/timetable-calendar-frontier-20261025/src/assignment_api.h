#pragma once
// Optional quiet-input adapter. Reports are nominations, never commands or facts
// about another actor's body, wallet, inventory, camp Life or readiness.
#include "runtime.h"
#include "assignment_protocol.h"
#include "production_job.h"
#include "production_assignment.h"
#include <array>
#include <cstdint>
#include <optional>

namespace tt::assignment_api {
enum class CollectStatus { Ready,NeedCompute,Stale,Unsupported,NotQuiet };
enum class SelfReason { None,IncompleteSelf,Roster,MemoryCapacity,ActiveBuff,HpDependentOutgoing,ItemMetadata };
enum SelfCapability : std::uint32_t { HpDependentOutgoing=1u };
struct CanonicalGroups {
    bool supported{};
    std::size_t count{};
    // These are own World vector indices, not assumed engine-native group IDs.
    std::array<int,31> local_by_canonical{},canonical_by_local{};
};
CanonicalGroups canonical_groups(const World&);
// Called only by the new-flag draft path after a successful fixed roster pick.
// Gameplay validates this read-only contract; it never calls draft SDK getters.
void publish_roster_contract(bool complete,unsigned actual_team_size);
struct SelfInputs {
    CollectStatus status{CollectStatus::NeedCompute};
    production_job::Snapshot snapshot;
    unsigned role{5};
    bool roster_verified{};
    std::uint8_t disabled_slots{}; // Explicitly omitted nonstationary/unmodelled operations.
    SelfReason reason{SelfReason::None};
    std::uint32_t unsupported_caps{};
    std::array<std::uint32_t,64> inventory{};
    std::size_t inventory_size{};
};
// Fresh-signature compatibility wrapper; no observation is restamped.
SelfInputs collect_self(const Observation&,std::uint64_t body_revision);
// A dated anchor is ONLY an own semantic checkpoint. Actual self body/items/kit
// are independently read at expected_current_epoch and compared with every
// represented anchored dependency. Old world/camp/ally masks are never read or
// refreshed. Caller separately establishes current P0/no-enemy priority. A dated
// anchor additionally requires unused actual input allowances; otherwise use an
// ordinary fresh observer to establish the current own movement history.
SelfInputs collect_self(const Observation& own_anchor,int expected_current_epoch,
                        std::uint64_t body_revision);
// Owning self-only checkpoint: safe to retain after the acquisition packet is
// destroyed by an already funded ordinary publication. No world masks/handles.
struct SelfCheckpoint {
    Status status{Status::NeedObservation};
    int epoch{},attack_range{};
    vec2 pos;
    float hp{},max_hp{},raw_strength{},strength{},focus{},damage_multiplier{1};
    std::uint32_t gold{},gems{};
    bool ranged{},splash{},channeling{},can_move{},can_attack{},can_cast{};
    mb::service::Body service_body;
    bool body_complete{};
};
SelfCheckpoint checkpoint(const Observation&);
SelfInputs collect_self(const SelfCheckpoint&,int expected_current_epoch,std::uint64_t body_revision);
// New funded preparation boundary only: independently collect current OWN facts
// without an Observation/old anchor. Caller owns fresh P0/contact/physical
// priority and supplies actual walking history. Unused current M/A/C is required.
// checkpoint_out is written only after Ready and the final current-epoch fence;
// it retains the exact unadjusted checkpoint expected by existing collect_self.
SelfInputs collect_current_self(int expected_epoch,std::uint64_t body_revision,
    bool walked_this_input=false,SelfCheckpoint* checkpoint_out=nullptr);

// Difference must be OwnMenu.answer() Ready for this EXACT commonStart/H,
// computed from completed private quotes or a justified shift-qualified trace.
// Matching groups alone never licenses an old total or foreign factual key.
std::optional<assignment_protocol::Prepared> prepared(
    const production_assignment::Difference&,std::uint64_t own_premise,bool fresh,
    assignment_protocol::Scenario scenario=assignment_protocol::Scenario::Unspecified);
struct Runtime { assignment_protocol::State protocol; };
struct ControllerContext {
    int epoch{};
    unsigned role{5}; // From actual collect_self(); step also checks own SDK type.
    std::size_t canonical_group_count{};
    bool roster_verified{},quiet{};
    std::optional<assignment_protocol::Prepared> own;
    void* validator_context{};
    // Caller checks its OWN retained body/inventory/geometry/Life dependencies.
    // A null validator refuses publication and execution. No remote authority.
    bool (*ready_validator)(void*,const assignment_protocol::Prepared&){};
    // Only a lookup/rebase of ALREADY completed private G/H traces. Never start
    // construction in response to a peer. This permits read-and-reply in the
    // same owner input without a second mailbox read or a fifth delay round.
    std::optional<assignment_protocol::Prepared> (*quote_provider)(void*,const assignment_protocol::Key&){};
};
struct Exchange {
    CollectStatus transport{CollectStatus::NeedCompute};
    assignment_protocol::Decision decision;
    bool wrote{};
};
// At most one 8-byte read and one 8-byte write; every import has budget/epoch
// fences. Invoke only after mandatory Work/contact/charged Return. A NeedQuotes
// result requests no construction: absent/incomplete own cache must fall back.
Exchange step(Runtime&,const ControllerContext&);
} // namespace tt::assignment_api
