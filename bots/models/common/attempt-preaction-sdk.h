#pragma once
#include "attempts.h"
#include "pure.h"
#include "../kernel/src/exact-kit.h"
#include "cambc.h"
#include <bit>

namespace plan4::attempts {
namespace preaction_detail {
namespace kb = plan4::runtime::kernel_binding;

// One allowance for the entire optional collection, including body validation,
// inventory, public composition and personal context. Extra first-turn fuel
// never expands it. The common sampler keeps 65k for recorder validation/copy
// and the existing 55k execution reserve; native fuel safety remains unproved.
class Budget {
    int actor_, round_;
    kb::KitPhaseBudget phase_{};
    bool stale_ = false;
public:
    Budget(int actor, int round): actor_(actor), round_(round) {
        phase_.begin(cambc::fuel_remaining());
        phase_.floor = std::max(std::uint64_t(65000), phase_.floor);
    }
    bool same_turn() {
        if (int(cambc::round_number()) != round_ ||
            int(static_cast<cambc::handle_champion_t>(cambc::self())) != actor_) stale_ = true;
        return !stale_;
    }
    bool available() {
        return phase_.permits(cambc::fuel_remaining()) && same_turn();
    }
};

// collect_exact_kit is shared with KernelLive, without instantiating or querying
// Kernel, its table or its runtime. The SDK reader is local because the existing
// OwnKitSource header also defines KernelLive and imports that model runtime.
class OwnKitSource {
    Budget& budget_;
    runtime::ObservationClock clock_{};
    cambc::enum_map<cambc::item_type> inventory_;
public:
    OwnKitSource(Budget& budget, int actor, int round): budget_(budget), clock_{round, {actor}} {}
    bool available() { return budget_.available(); }
    bool own(kb::OwnKitFacts& out) {
        if (!available()) return false;
        const auto& self = cambc::self();
        out.clock = clock_;
        out.champion = int(self.champion_name());
        if (!available()) return false;
        out.attack_range = int(self.attack_range());
        if (!available()) return false;
        out.strength = self.strength();
        if (!available()) return false;
        out.focus = self.focus();
        return available();
    }
    bool ability(int slot, int& kind) {
        if (slot < 0 || slot > 1 || !available()) return false;
        const auto& slots = cambc::self().abilities();
        const auto& info = slots[slot].info();
        kind = info ? std::visit([](const auto& value) { return int(value.kind); }, *info) : 0;
        return available();
    }
    bool inventory(kb::InventoryFacts& out) {
        if (!available()) return false;
        constexpr int count = int(cambc::item_type::count);
        if (count < 1 || count > kb::inventory_catalog_limit) return false;
        inventory_ = cambc::self().item_count();
        if (!available()) return false;
        out = {inventory_.ptr(), count, true};
        return true;
    }
};
static_assert(int(cambc::ability_kind::shield) == kb::maximum_ability_kind);
static_assert(PreactionEvidence::item_capacity == kernel::item_limit);

inline void kit(PreactionEvidence& out, Budget& budget) {
    OwnKitSource source(budget, out.actor, out.round);
    kb::OwnKitFacts own;
    if (!source.own(own)) return;
    runtime::CombatKit combat;
    combat.clock = own.clock; combat.champion = own.champion;
    combat.attack_range = own.attack_range; combat.strength = own.strength; combat.focus = own.focus;
    // These values identify this local validation invocation only. They are
    // never published as an engine kit version/stat epoch. Every body field is
    // an actual current self read and collect_exact_kit independently rereads it.
    combat.version = 1; combat.stat_epoch = 0; combat.exact = true;
    kb::KitCollection collected;
    kb::collect_exact_kit(combat, source, collected);
    if (collected.failure != kb::KitFailure::none || !collected.frame.kit.known) return;
    const auto& exact = collected.frame.kit;
    out.champion = exact.champion; out.attack_range = exact.attack_range;
    out.ability0 = exact.first_ability; out.ability1 = exact.second_ability;
    out.strength_bits = std::bit_cast<std::uint32_t>(exact.strength);
    out.focus_bits = std::bit_cast<std::uint32_t>(exact.focus);
    out.item_count = exact.item_count;
    for (int n = 0; n < exact.item_count; ++n) out.items[n] = {exact.items[n].kind, exact.items[n].count};
    if (budget.available()) out.kit_known = true;
}

inline void composition(PreactionEvidence& out, const Identity& identity,
                        const cambc::map::camp_group* group, Budget& budget) {
    if (identity.kind == Kind::bank) { out.composition_known = true; return; }
    if (!group || identity.kind != Kind::one_camp ||
        int(static_cast<cambc::handle_camp_group_t>(*group)) != identity.group_id || !budget.available()) return;
    // camps()/kind() are constructor-cached public layout metadata. No state,
    // health, visibility or other live camp query is used, even for unseen camps.
    const auto members = group->camps();
    if (members.empty() || members.size() > PreactionEvidence::composition_capacity) return;
    for (const auto& member : members) {
        if (!budget.available()) return;
        const int kind = int(member.kind());
        if (kind < 0) return;
        int at = out.composition_count;
        while (at && out.composition[at - 1] > kind) {
            if (!budget.available()) return;
            out.composition[at] = out.composition[at - 1]; --at;
        }
        out.composition[at] = kind; ++out.composition_count;
    }
    if (budget.available()) out.composition_known = true;
}

inline void context(PreactionEvidence& out, const Observation& own, Budget& budget) {
    const int width = cambc::map::width(), height = cambc::map::height();
    const auto bytes = cambc::bitboard::raw_buffer_size();
    // Local work/allocation bound, independent of engine balance constants.
    constexpr std::size_t word_capacity = 1024, checkpoint_words = 16;
    if (!own.position_known || own.x < 0 || own.y < 0 || width <= 0 || height <= 0 ||
        own.x >= width || own.y >= height || bytes == 0 || bytes % sizeof(std::uint64_t) ||
        bytes / sizeof(std::uint64_t) > word_capacity ||
        std::uint64_t(width) * std::uint64_t(height) > std::uint64_t(bytes) * 8) return;
    bool other = false;
    // Both COMPLETE current personal occupancy masks must finish, even if an
    // ally is found early. This does not construct champions or query any of
    // their fields. Raw engine-chunked words are inspected only for nonzero;
    // the SDK import writes every chunk, and set() handles own tile layout.
    for (const auto side : {cambc::team::ally, cambc::team::enemy}) {
        if (!budget.available()) return;
        auto occupied = cambc::vision::champion_tiles(side);
        if (!budget.available()) return;
        if (side == cambc::team::ally) {
            occupied.set({own.x, own.y}, false);
            if (!budget.available()) return;
        }
        const auto words = bytes / sizeof(std::uint64_t);
        for (std::size_t word = 0; word < words; ++word) {
            if (word % checkpoint_words == 0 && !budget.available()) return;
            other |= occupied.ptr()[word] != 0;
        }
    }
    if (!budget.available()) return;
    out.context = other ? 1 : 0; out.context_known = true;
}
} // namespace preaction_detail

// Call immediately before invoking the macro. job is the frozen selected menu
// entry, never a reconstructed post-action target. bootstrap/no job therefore
// retains UNKNOWN travel/duration. Job nominal_completion is an elapsed nominal
// duration (pure.h menu construction), not an absolute completion round.
inline PreactionEvidence collect_preaction(const Identity& identity, const Observation& own,
    const Job* job = nullptr, const cambc::map::camp_group* group = nullptr) {
    PreactionEvidence out;
    if (!own.own || !own.fresh || own.actor < 0 || own.round < 0) return out;
    out.actor = own.actor; out.round = own.round;
    preaction_detail::Budget budget(out.actor, out.round);
    if (!budget.available()) return out;
    if (identity.origin == Origin::planned && job &&
        ((identity.kind == Kind::bank && job->kind == JobKind::bank) ||
         (identity.kind == Kind::one_camp && job->kind == JobKind::farm && job->group >= 0 && job->second_group < 0))) {
        if (job->nominal_travel >= 0) { out.travel = job->nominal_travel; out.travel_known = true; }
        if (job->nominal_completion > 0) { out.nominal_duration = job->nominal_completion; out.nominal_duration_known = true; }
    }
    // An expected loss of personal visibility invalidates only that component.
    // Unexpected SDK errors propagate to the existing owner-turn boundary.
    try { preaction_detail::kit(out, budget); }
    catch (const cambc::error& error) {
        if (error.code() != cambc::error_code::api_not_in_vision) throw;
    }
    preaction_detail::composition(out, identity, group, budget);
    try { preaction_detail::context(out, own, budget); }
    catch (const cambc::error& error) {
        if (error.code() != cambc::error_code::api_not_in_vision) throw;
    }
    // A stopped category is explicit UNKNOWN, without a partial payload. The
    // producer already has catalog-ordered unique items and budget-checked
    // composition insertion above, so it need not pay the generic recorder's
    // quadratic duplicate validation/canonicalization inside this allowance.
    if (!out.kit_known) {
        out.champion = out.ability0 = out.ability1 = out.attack_range = -1;
        out.strength_bits = out.focus_bits = 0; out.item_count = 0;
        for (auto& item : out.items) item = {};
    }
    if (!out.composition_known) {
        out.composition_count = 0;
        for (auto& kind : out.composition) kind = 0;
    }
    // Suspension can resume on a later round with old locals. A final cheap
    // clock/owner check prevents publishing any earlier-round exact evidence.
    if (!budget.same_turn()) return PreactionEvidence{};
    return out;
}
} // namespace plan4::attempts
