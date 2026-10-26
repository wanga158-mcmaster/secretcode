#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <variant>

namespace p6::runtime_combat {
// These templates use the pinned SDK's public methods. They also permit small
// host checks without importing SDK startup globals or replacing the bot policy.
enum class TargetKind { camp, enemy };
enum class CastMode { none, unit, ground, self };
struct CastChoice {
    int slot = -1;
    CastMode mode = CastMode::none;
    explicit operator bool() const { return slot >= 0 && mode != CastMode::none; }
};
// Enemy readiness is not observable. Bound its single successful cast by the
// largest queried damaging slot or one reset's extra basic attack, including
// dash landing damage. Do not add slots or infer dash legality from an ordinary
// unit/ground target mask. The caller adds the initial basic attack separately.
// The caller retains passive retaliation and worst-health stress adjustments.
template<class Abilities> double enemy_spell_bound(const Abilities& abilities, double strength) {
    if (!std::isfinite(strength) || strength < 0)
        return std::numeric_limits<double>::infinity();
    double bound = 0;
    for (const auto& ability : abilities) {
        if (!ability.info()) continue;
        if (ability.is_attack_reset()) {
            bound = std::max(bound, strength);
            continue;
        }
        if (!(ability.is_damage() || ability.is_aoe_damage() ||
              ability.is_cc() || ability.is_dash())) continue;
        const double amount = ability.damage_amount();
        if (!std::isfinite(amount) || amount < 0)
            return std::numeric_limits<double>::infinity();
        bound = std::max(bound, amount);
    }
    return bound;
}
template<class Position> std::int64_t distance(Position a, Position b) {
    return std::max(std::abs(std::int64_t(a.x) - b.x),
                    std::abs(std::int64_t(a.y) - b.y));
}
template<class Mask> struct SpellOffer {
    std::optional<Mask> range;
    bool camps = false, enemies = false, ground = false, self = false;
    bool damage = false, control = false;
    double self_radius = -1;
};
template<class Mask, class Position> struct Options {
    Position origin{};
    std::optional<Mask> basic;
    std::array<SpellOffer<Mask>, 2> spells{};
    CastMode mode(int slot, Position target, TargetKind kind) const {
        if (slot < 0 || slot >= static_cast<int>(spells.size())) return CastMode::none;
        const auto& spell = spells[slot];
        if (!spell.range) return CastMode::none;
        const bool unit = kind == TargetKind::camp ? spell.camps : spell.enemies;
        if (spell.range->is_set(target)) {
            if (unit) return CastMode::unit;
            if (spell.ground) return CastMode::ground;
        }
        // A self cast has no unit argument or ground destination. Only a
        // represented area radius establishes that it can affect this target.
        if (spell.self && spell.self_radius >= 0 &&
            spell.range->is_set(origin) && distance(origin, target) <= spell.self_radius)
            return CastMode::self;
        return CastMode::none;
    }
    CastChoice choose_cast(Position target, TargetKind kind, bool prefer_control = false,
                           bool control_eligible = true, unsigned skip_slots = 0) const {
        for (int pass = prefer_control ? 0 : 1; pass < 2; ++pass)
            for (int slot = 0; slot < static_cast<int>(spells.size()); ++slot) {
                if (skip_slots & (1u << slot)) continue;
                const auto& spell = spells[slot];
                if (pass == 0 ? !(spell.control && control_eligible) : !spell.damage) continue;
                const auto cast_mode = mode(slot, target, kind);
                if (cast_mode != CastMode::none) return {slot, cast_mode};
            }
        return {};
    }
    bool actionable(Position target, TargetKind kind) const {
        return (basic && basic->is_set(target)) || bool(choose_cast(target, kind));
    }
};

// One attack mask and at most one geometric mask per ready damaging/control
// slot. Dash paths and landing tiles require their own dispatcher and are not
// ordinary target offers. No candidate-loop host queries or forecast damage
// credit are introduced.
// Discard this snapshot after any successful action or revealed obstruction.
template<class Champion> auto observe_options(const Champion& champion, bool walked) {
    using Mask = std::remove_cvref_t<decltype(champion.attack_tiles())>;
    using Position = std::remove_cvref_t<decltype(champion.position())>;
    Options<Mask, Position> out;
    out.origin = champion.position();
    if (champion.can_attack()) out.basic.emplace(champion.attack_tiles());
    if (!champion.can_cast()) return out;
    const auto& abilities = champion.abilities();
    for (int slot = 0; slot < static_cast<int>(out.spells.size()); ++slot) {
        const auto& ability = abilities[slot];
        const auto& info = ability.info();
        if (!info || !(ability.is_damage() || ability.is_aoe_damage() || ability.is_cc()) ||
            (walked && std::visit([](const auto& value) { return value.exclusive_with_movement; }, *info)) ||
            !ability.is_ready()) continue;
        auto& spell = out.spells[slot];
        const double amount = ability.damage_amount();
        spell.damage = std::isfinite(amount) && amount > 0;
        spell.control = ability.is_cc();
        if (!spell.damage && !spell.control) continue;
        const auto flags = ability.valid_targets();
        using Flags = std::remove_cvref_t<decltype(flags)>;
        spell.camps = flags.contains(Flags::camps);
        spell.enemies = flags.contains(Flags::enemies);
        spell.ground = flags.contains(Flags::ground);
        spell.self = flags.contains(Flags::self);
        spell.self_radius = std::visit([](const auto& value) -> double {
            if constexpr (requires { value.radius; })
                return std::isfinite(value.radius) && value.radius >= 0 ? value.radius : -1;
            return -1;
        }, *info);
        spell.range.emplace(ability.valid_tiles());
    }
    return out;
}

struct TargetRank {
    bool actionable = false;
    std::int64_t distance = 0;
    double health = 0;
    int id = -1;
};
inline bool better_target(const TargetRank& candidate, const TargetRank& incumbent) {
    if (candidate.actionable != incumbent.actionable) return candidate.actionable;
    // Use health and identity among immediately useful targets. An approach
    // fallback instead minimizes travel, then uses the same deterministic ties.
    if (!candidate.actionable && candidate.distance != incumbent.distance)
        return candidate.distance < incumbent.distance;
    if (candidate.health != incumbent.health) return candidate.health < incumbent.health;
    return candidate.id < incumbent.id;
}
template<class Camps, class Member, class Mask, class Position, class Identity>
const typename Camps::value_type* select_group_target(const Camps& camps, Member member,
    const Options<Mask, Position>& options, Identity identity) {
    const typename Camps::value_type* best = nullptr;
    TargetRank best_rank;
    for (const auto& camp : camps) {
        if (!member(camp) || !camp.is_visible() || !camp.is_alive()) continue;
        const double health = camp.health();
        if (!std::isfinite(health) || health <= 0) continue;
        const auto position = camp.position();
        const TargetRank rank{options.actionable(position, TargetKind::camp),
                              distance(options.origin, position), health, identity(camp)};
        if (!best || better_target(rank, best_rank)) { best = &camp; best_rank = rank; }
    }
    return best;
}

// Call only while the options snapshot and owner clock remain current. The live
// ready query also enforces the SDK's shared one-successful-cast allowance.
template<class Ability, class Target>
bool use_cast(const Ability& ability, const Target& target, CastMode mode, bool walked) {
    const auto& info = ability.info();
    if (mode == CastMode::none || !info || ability.is_dash() ||
        !target.is_visible() || target.health() <= 0 ||
        (walked && std::visit([](const auto& value) { return value.exclusive_with_movement; }, *info)) ||
        !ability.is_ready()) return false;
    switch (mode) {
    case CastMode::unit: return ability.use(target);
    case CastMode::ground: return ability.use(target.position());
    case CastMode::self: return ability.use();
    default: return false;
    }
}
} // namespace p6::runtime_combat
