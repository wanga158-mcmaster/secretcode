#pragma once
#include <bit>
#include <cstdint>

namespace plan4::attempts {

// Exact, pre-action observations only. This SDK-neutral payload does not assert
// that a model supports the kit, context or macro represented by these fields.
struct PreactionEvidence {
    static constexpr int item_capacity = 32, composition_capacity = 32;
    struct Item { int kind = -1; unsigned count = 0; };
    int actor = -1, round = -1;
    int champion = -1, ability0 = -1, ability1 = -1, attack_range = -1;
    // Preserve the SDK's raw IEEE f32 values without rounding or inference.
    std::uint32_t strength_bits = 0, focus_bits = 0;
    Item items[item_capacity]{};
    int item_count = 0;
    int composition[composition_capacity]{};
    int composition_count = 0;
    // 0: solo_visible_context; 1: other_actor_visible. Neither proves solitude.
    int context = -1, travel = -1, nominal_duration = 0;
    bool kit_known = false, composition_known = false, context_known = false;
    bool travel_known = false, nominal_duration_known = false;
};

// A foreign/stale collection cannot annotate this start. Independent unknown
// categories are canonicalized, and every bounded sequence is validated before
// indexing. Sorting makes exact inventories and public kind multisets stable.
inline PreactionEvidence freeze_preaction(const PreactionEvidence& source, int actor, int round) {
    PreactionEvidence result;
    if (actor < 0 || round < 0 || source.actor != actor || source.round != round) return result;
    result.actor = actor; result.round = round;
    // Schema 1 uses the pinned SDK3 physical champion range 0..12,
    // independently of model support.
    const auto finite_nonnegative = [](std::uint32_t bits) {
        return (bits & 0x7f800000u) != 0x7f800000u && std::bit_cast<float>(bits) >= 0;
    };
    bool kit = source.kit_known && source.champion >= 0 && source.champion <= 12 &&
        source.ability0 >= 0 && source.ability0 <= 7 && source.ability1 >= 0 && source.ability1 <= 7 &&
        finite_nonnegative(source.strength_bits) && finite_nonnegative(source.focus_bits) &&
        source.attack_range >= 0 && source.item_count >= 0 && source.item_count <= PreactionEvidence::item_capacity;
    if (kit) {
        for (int n = 0; n < source.item_count; ++n) {
            if (source.items[n].kind < 0 || source.items[n].kind > 41 || !source.items[n].count) {
                kit = false; break;
            }
        }
    }
    if (kit) {
        for (int n = 0; n < source.item_count; ++n) {
            int p = n;
            while (p && result.items[p - 1].kind > source.items[n].kind) {
                result.items[p] = result.items[p - 1]; --p;
            }
            result.items[p] = source.items[n];
        }
        // The production collector emits catalog order: validation is linear
        // there, without the previous unconditional 496 pair comparisons.
        for (int n = 1; n < source.item_count; ++n)
            if (result.items[n - 1].kind == result.items[n].kind) kit = false;
        if (kit) {
            result.kit_known = true; result.champion = source.champion;
            result.ability0 = source.ability0; result.ability1 = source.ability1; result.attack_range = source.attack_range;
            result.strength_bits = source.strength_bits; result.focus_bits = source.focus_bits;
            result.item_count = source.item_count;
        } else for (auto& item : result.items) item = {};
    }
    bool composition = source.composition_known && source.composition_count >= 0 &&
        source.composition_count <= PreactionEvidence::composition_capacity;
    if (composition)
        for (int n = 0; n < source.composition_count; ++n)
            if (source.composition[n] < 0 || source.composition[n] > 4) composition = false;
    if (composition) {
        result.composition_known = true; result.composition_count = source.composition_count;
        for (int n = 0; n < source.composition_count; ++n) {
            int p = n;
            while (p && result.composition[p - 1] > source.composition[n]) {
                result.composition[p] = result.composition[p - 1]; --p;
            }
            result.composition[p] = source.composition[n];
        }
    }
    if (source.context_known && (source.context == 0 || source.context == 1)) {
        result.context_known = true; result.context = source.context;
    }
    if (source.travel_known && source.travel >= 0) {
        result.travel_known = true; result.travel = source.travel;
    }
    if (source.nominal_duration_known && source.nominal_duration > 0) {
        result.nominal_duration_known = true; result.nominal_duration = source.nominal_duration;
    }
    return result;
}

} // namespace plan4::attempts
