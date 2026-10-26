#pragma once
// Source-event inverse only. No features, candidates, paths, actor lifecycle or fuel admission.
#include <array>
#include <cstdint>
#include <optional>
#include <vector>
#include "../src/core/action.h"

namespace zero::source {

struct SourceEvent
{
    std::int32_t sequence{}, input{}, kind{}, round{}, actor{ -1 }, priority{};
    std::uint32_t slot{};
    Tile from{}, to{};
    bool from_known{}, to_known{}, slot_known{}, priority_known{};
    // An adapter may assert an exact queried ability kind. An uninformative raw field stays unknown.
    std::int32_t ability_type{ -1 };
    bool ability_type_known{};
    std::int64_t after_gold{};
    std::array<std::uint32_t, kMaxItems> after_items{};
    std::array<std::uint32_t, 4> after_consumables{};
    bool after_gold_known{}, after_items_known{}, after_consumables_known{};
};

struct InverseFacts
{
    // These attest exact SOURCE observations, not student initialization or feature readiness.
    bool self_known{}, before_gold_known{}, before_items_known{}, before_consumables_known{};
    std::array<std::uint32_t, 4> before_consumables{};
    bool visible_units_complete{}, visible_camps_complete{};
    // Aligned with Map::altars. 1 means the source personally knows the lock fact at that altar.
    // Obs::altar_lock alone has no unknown/visibility bit, so a missing assertion blocks the inverse.
    std::vector<std::uint8_t> lock_personally_known;
    // Raw consumables 0 and 2 map to SDK crystal=0 and lock=1. Other slots remain conserved.
    bool consumable_types_known{};
    std::array<std::uint32_t, 4> consumable_cost{};
    std::array<bool, 4> consumable_cost_known{};
};

enum class TargetKind : std::uint8_t { none, tile, self, champion, camp, altar, item, consumable, priority };
enum class Status : std::uint8_t
{
    resolved, exogenous, phase_event, ambiguous, missing_facts, no_match, no_effect,
    unsupported, invalid_event, overflow
};
enum class Reason : std::uint8_t
{
    observed_command, observed_end, fuel_boundary, inverse_ambiguous_kind23, phase_specific_pair,
    unknown_kind, missing_source_facts, source_identity_mismatch, invalid_source_field,
    target_not_identified, multiple_targets, invalid_ability, self_unit_form_ambiguous,
    purchase_no_match, purchase_no_effect, multiple_purchases, unsupported_purchase_rule,
    purchase_overflow
};
enum MissingFact : std::uint64_t
{
    mf_self = 1ull << 0, mf_from = 1ull << 1, mf_to = 1ull << 2, mf_slot = 1ull << 3,
    mf_priority = 1ull << 4, mf_before_gold = 1ull << 5, mf_before_items = 1ull << 6,
    mf_before_consumables = 1ull << 7, mf_after_gold = 1ull << 8, mf_after_items = 1ull << 9,
    mf_after_consumables = 1ull << 10, mf_visible_units = 1ull << 11,
    mf_visible_camps = 1ull << 12, mf_visible_locks = 1ull << 13,
    mf_consumable_rules = 1ull << 14
};

struct Intent
{
    ActionType type{ a_count };  // invalid until an explicit resolved result supplies it
    Cand argument{};            // phi is always zero; no target descriptor work occurs here
    TargetKind target_kind{ TargetKind::none };
    std::int32_t target_id{ -1 };  // real champion/camp handle, altar index, item or typed consumable id
    // Raw PRIORITY or original BUY quantity. BUY's pointer argument keeps count=1 until scalar forcing.
    // int64 preserves a uniquely identified uint32 quantity even when a later model ABI cannot encode it.
    std::int64_t scalar{};
    bool scalar_known{};
};

struct InverseResult
{
    Status status{ Status::unsupported };
    Reason reason{ Reason::unknown_kind };
    std::uint64_t missing_facts{};
    std::uint32_t hypotheses{}, matches{};
    std::optional<Intent> intent;  // absent for every unresolved/exogenous/phase event; never default END
    bool resolved() const { return status == Status::resolved && intent.has_value(); }
};

InverseResult inverse(const SourceEvent& event, const Map& map, const Obs& before, const InverseFacts& facts);
// Identity relevance only, after a separate complete argument builder. Never considers phi or candidate cap.
// BUY ignores count (quantity is a second scalar); PRIORITY ignores target (raw scalar is forced later).
// END/RECALL/CANCEL ignore unused coordinates/targets; self/unit casts ignore unused ground coordinates.
bool match_argument(const Intent& intent, const Cand& candidate);
const char* status_name(Status status);
const char* reason_name(Reason reason);

}  // namespace zero::source
