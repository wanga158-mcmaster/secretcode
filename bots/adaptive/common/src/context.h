#pragma once
// Actor-private Plan 8 evidence identities. Pure offline types; no replay/SDK access.
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace p8 {
constexpr int max_contexts = 8, max_hypotheses = 4, max_outcomes = 4;
constexpr int max_participants = 6, qualification_rounds = 256;
using PremiseMask = std::uint32_t;
enum Premise : PremiseMask {
    settings_known = 1u << 0, identity_known = 1u << 1,
    source_known = 1u << 2, route_known = 1u << 3,
    membership_known = 1u << 4, build_known = 1u << 5,
    health_band_known = 1u << 6, threat_known = 1u << 7,
    endpoint_known = 1u << 8, transition_known = 1u << 9
};
constexpr PremiseMask all_premises = (1u << 10) - 1;
enum class HealthBand { below_half, half_to_three_quarters, three_quarters_to_full };
inline HealthBand health_band(double health, double maximum) {
    if (health >= maximum * .75)
        return HealthBand::three_quarters_to_full;
    if (health >= maximum * .5) return HealthBand::half_to_three_quarters;
    return HealthBand::below_half;
}
struct EntryBuild {
    std::uint64_t equipment_hash = 0;
    double max_health = 0, strength = 0, focus = 0, growth = 0;
    bool operator==(const EntryBuild&) const = default;
};
struct ContextKey {
    std::uint64_t settings_id = 0, route_hash = 0;
    // Optional explicitly witnessed bank/renewal component. A new organization
    // may change its assembly/clear prefix while retaining these exact legs.
    std::uint64_t return_route_hash = 0;
    int actor_id = -1, champion = -1, role = -1;
    int source_group = -1, route_template = -1, endpoint = -1, threat_regime = -1;
    std::array<int, max_participants> participants{-1,-1,-1,-1,-1,-1};
    int participant_count = 0;
    EntryBuild build{};
    HealthBand hp_band = HealthBand::three_quarters_to_full;
    bool operator==(const ContextKey&) const = default;
};
inline bool valid(const ContextKey& k) {
    if (!k.settings_id || !k.route_hash || k.actor_id < 0 || k.champion < 0 ||
        k.role < 0 || k.source_group < 0 || k.route_template < 0 || k.endpoint < 0 ||
        k.threat_regime < 0 || k.participant_count < 1 ||
        k.participant_count > max_participants || !std::isfinite(k.build.max_health) || k.build.max_health <= 0 ||
        !std::isfinite(k.build.strength) || k.build.strength < 0 ||
        !std::isfinite(k.build.focus) || k.build.focus < 0 ||
        !std::isfinite(k.build.growth) || k.build.growth < 0) return false;
    bool own = false;
    for (int i = 0; i < max_participants; ++i) {
        if (i < k.participant_count) {
            if (k.participants[i] < 0 || (i && k.participants[i-1] >= k.participants[i])) return false;
            own |= k.participants[i] == k.actor_id;
        } else if (k.participants[i] != -1) return false;
    }
    return own;
}
struct NumericRange {
    double lower = 0, upper = 0;
    bool valid() const { return std::isfinite(lower) && std::isfinite(upper) && lower >= 0 && upper >= lower; }
    bool contains(double value) const { return valid() && std::isfinite(value) && value >= lower && value <= upper; }
    bool contains_bag(std::int64_t value) const {
        if (!valid() || value < 0) return false;
        // 2^63 is exactly representable. Cast only finite nonnegative bounds
        // strictly below it, avoiding both int64 overflow and >2^53 rounding.
        constexpr double past_max = 9223372036854775808.;
        if (lower >= past_max) return false;
        const auto first = static_cast<std::int64_t>(std::ceil(lower));
        const auto last = upper >= past_max ? std::numeric_limits<std::int64_t>::max() :
            static_cast<std::int64_t>(std::floor(upper));
        return value >= first && value <= last;
    }
};
struct NumericState {
    double health = 0, max_health = 0, strength = 0, focus = 0, growth = 0;
    std::int64_t bag = 0;
    std::uint64_t equipment_hash = 0;
};
inline bool valid_numeric_state(const NumericState& s) {
    return std::isfinite(s.health) && s.health >= 0 && std::isfinite(s.max_health) &&
        s.max_health > 0 && s.health <= s.max_health && std::isfinite(s.strength) && s.strength >= 0 &&
        std::isfinite(s.focus) && s.focus >= 0 && std::isfinite(s.growth) && s.growth >= 0 && s.bag >= 0;
}
// Freeze this before the first start. Planned purchases/casts/Focus transitions
// must already fit these bounds; they cannot widen in response to a failure.
struct TransitionEnvelope {
    NumericRange health{}, max_health{}, strength{}, focus{}, growth{}, bag{};
    std::uint64_t planned_equipment_hash = 0;
    bool valid() const {
        return health.valid() && health.lower > 0 && max_health.valid() &&
            max_health.lower > 0 && strength.valid() && focus.valid() &&
            growth.valid() && bag.valid();
    }
    bool contains(const NumericState& s, const EntryBuild& initial) const {
        return valid() && valid_numeric_state(s) && health.contains(s.health) && max_health.contains(s.max_health) &&
            s.health <= s.max_health && strength.contains(s.strength) &&
            focus.contains(s.focus) && growth.contains(s.growth) && bag.contains_bag(s.bag) &&
            (s.equipment_hash == initial.equipment_hash ||
             (planned_equipment_hash && s.equipment_hash == planned_equipment_hash));
    }
};
struct Premises {
    ContextKey key{};
    NumericState state{};
    // Bits are set only for premises personally observed throughout their
    // relevant stage. Synthetic fixtures construct these at the same boundary.
    PremiseMask personally_observed = 0;
};
enum class Applicability { applicable, unknown, changed };
inline Applicability applicability(const ContextKey& expected, const TransitionEnvelope& envelope,
                                   const Premises& observed, bool entry) {
    const auto& k = observed.key;
    const auto m = observed.personally_observed;
    if (((m & settings_known) && k.settings_id != expected.settings_id) ||
        ((m & identity_known) && (k.actor_id != expected.actor_id || k.champion != expected.champion || k.role != expected.role)) ||
        ((m & source_known) && k.source_group != expected.source_group) ||
        ((m & route_known) && (k.route_template != expected.route_template || k.route_hash != expected.route_hash ||
                              k.return_route_hash != expected.return_route_hash)) ||
        ((m & membership_known) && (k.participant_count != expected.participant_count || k.participants != expected.participants)) ||
        ((m & build_known) && k.build != expected.build) ||
        ((m & health_band_known) && k.hp_band != expected.hp_band) ||
        ((m & threat_known) && k.threat_regime != expected.threat_regime) ||
        ((m & endpoint_known) && k.endpoint != expected.endpoint) ||
        ((m & transition_known) && !envelope.contains(observed.state, expected.build)))
        return Applicability::changed;
    if ((m & all_premises) != all_premises) return Applicability::unknown;
    if (entry && (observed.state.max_health != expected.build.max_health ||
        observed.state.strength != expected.build.strength ||
        observed.state.focus != expected.build.focus || observed.state.growth != expected.build.growth ||
        observed.state.equipment_hash != expected.build.equipment_hash ||
        health_band(observed.state.health, observed.state.max_health) != expected.hp_band))
        return Applicability::changed;
    return Applicability::applicable;
}
// This explicit transfer checks the unchanged return component only. It never
// transfers complete organization qualification, new combat or new membership.
// Own build/health band remain exact; planned within-episode transitions belong
// in the frozen envelope, not in an silently broadened component key.
inline bool route_component_matches(const ContextKey& component, const ContextKey& organization) {
    ContextKey same = organization;
    if (component.return_route_hash || organization.return_route_hash) {
        if (!component.return_route_hash || component.return_route_hash!=organization.return_route_hash) return false;
        same.route_hash=component.route_hash;
    }
    same.participants = component.participants;
    same.participant_count = component.participant_count;
    same.role = component.role;
    return same == component;
}
inline void saturating_increment(std::uint16_t& count) {
    if (count != std::numeric_limits<std::uint16_t>::max()) ++count;
}
} // namespace p8
