#pragma once
// A bounded stationary-word model, never SDK permission or a safety certificate.
// Inputs are immutable exact scalar observations for this owner input. The host
// applies only first, observes the actual result, and searches again. No predicted
// death or allowance is authority for a later physical command.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>

namespace mosaic::word {
inline constexpr std::size_t max_targets = 32, max_centers = 32, max_beam = 16;
inline constexpr unsigned max_word = 3;
struct Point { int x{}, y{}; bool operator==(const Point&) const = default; };
inline int distance(Point a, Point b) {
    const auto x = std::abs(std::int64_t(a.x) - b.x), y = std::abs(std::int64_t(a.y) - b.y);
    return int(std::min<std::int64_t>(std::max(x, y), std::numeric_limits<int>::max()));
}
enum class TargetKind { camp, enemy };
enum TargetBits : unsigned { camps = 1, enemies = 2, ground = 4, self = 8 };
struct Target {
    int id{-1}; Point pos{}; float hp{}, shield{};
    TargetKind kind{TargetKind::camp}; bool visible{}; double weight{1};
    // Damage AFTER mitigation but BEFORE shields and HP capping. The SDK's
    // damage_amount(enemy) health-loss overload is not this quantity.
    float basic_hit{}; std::array<float, 2> spell_hit{};
};
struct Body {
    Point pos{}; float hp{}, shield{}; int range{}, splash{};
    bool can_attack{}, can_cast{}, walked{}, attack_spent{};
};
enum class AbilityKind { none, damage, reset, shield };
struct Ability {
    AbilityKind kind{AbilityKind::none}; bool ready{}, exclusive{};
    int range{}, radius{}; unsigned target_bits{};
    float shield_amount{};
    // These scalars describe ONLY the next basic after a reset. The host must
    // supply the actual kit/item rule; enhanced-attack damage is not inferred.
    float reset_basic_multiplier{1}, reset_camp_bonus{}, reset_enemy_bonus{};
};
enum class Kind { none, basic, cast, reset, shield };
struct Command {
    Kind kind{Kind::none}; int slot{-1}, target_id{-1}, target_index{-1}; Point center{};
    bool operator==(const Command&) const = default;
};
struct Input {
    Body body; std::span<const Target> targets; std::array<Ability, 2> abilities{};
    // Already nominated ground centers. If empty, visible living target tiles
    // form the finite menu. This header does not establish legal ground tiles.
    std::span<const Point> ground_centers;
    double death_bonus{}, shield_progress{0.05}, self_shield_value{};
    unsigned depth{3}, beam_width{8}, node_quota{512};
};
enum class Issue { none, invalid_input, capacity };
struct Result {
    bool valid{}, complete{}; Issue issue{Issue::none};
    Command first; std::array<Command, max_word> word{}; unsigned length{}, nodes{}, beam_pruned{};
    double score{};
};
inline float rounded(float value) { volatile float out = value; return out; }
struct Residual { float hp{}, shield{}; };
inline Residual hit(Residual target, float damage) {
    const auto absorbed = std::min(target.shield, damage);
    target.shield = std::max(0.0f, rounded(target.shield - absorbed));
    target.hp = std::max(0.0f, rounded(target.hp - rounded(damage - absorbed)));
    return target;
}
namespace detail {
struct RawResidual { float hp, shield; };
struct StoredCommand { Kind kind; int slot, target_id, target_index, x, y; };
struct State {
    std::array<RawResidual, max_targets> targets;
    float shield, basic_multiplier, camp_bonus, enemy_bonus;
    bool can_attack, can_cast, attack_spent;
    std::array<StoredCommand, max_word> word;
    unsigned length; double score;
};
static_assert(std::is_trivial_v<State>, "Unused workspace slots must not run constructors or clearing.");
inline StoredCommand store(const Command& c) { return {c.kind, c.slot, c.target_id, c.target_index, c.center.x, c.center.y}; }
inline Command command(const StoredCommand& c) { return {c.kind, c.slot, c.target_id, c.target_index, {c.x, c.y}}; }
inline void copy(State& out, const State& source, std::size_t targets) {
    for (std::size_t i = 0; i < targets; ++i) out.targets[i] = source.targets[i];
    out.shield = source.shield; out.basic_multiplier = source.basic_multiplier;
    out.camp_bonus = source.camp_bonus; out.enemy_bonus = source.enemy_bonus;
    out.can_attack = source.can_attack; out.can_cast = source.can_cast; out.attack_spent = source.attack_spent;
    out.length = source.length; out.score = source.score;
    for (unsigned i = 0; i < source.length; ++i) out.word[i] = source.word[i];
}
inline bool finite(float value) { return std::isfinite(value) && value >= 0; }
inline bool better(const State& a, const State& b) {
    if (a.score != b.score) return a.score > b.score;
    if (a.length != b.length) return a.length < b.length;
    // Stable enumeration order resolves the remaining equality.
    return false;
}
inline bool validate(const Input& in) {
    if (!finite(in.body.hp) || in.body.hp <= 0 || !finite(in.body.shield) ||
        in.body.range < 0 || in.body.splash < 0 || in.body.splash > 100 ||
        in.depth == 0 || in.depth > max_word || in.beam_width == 0 || in.beam_width > max_beam ||
        !std::isfinite(in.death_bonus) || in.death_bonus < 0 ||
        !std::isfinite(in.shield_progress) || in.shield_progress < 0 ||
        !std::isfinite(in.self_shield_value) || in.self_shield_value < 0) return false;
    double utility_bound = 0;
    for (std::size_t i = 0; i < in.targets.size(); ++i) {
        const auto& t = in.targets[i];
        if (t.id < 0 || !finite(t.hp) || !finite(t.shield) || !finite(t.basic_hit) ||
            !finite(t.spell_hit[0]) || !finite(t.spell_hit[1]) || !std::isfinite(t.weight) || t.weight < 0)
            return false;
        for (std::size_t j = 0; j < i; ++j)
            if (in.targets[j].kind == t.kind && in.targets[j].id == t.id) return false;
        utility_bound += t.weight * (double(t.hp) + in.shield_progress * double(t.shield) + in.death_bonus);
        if (!std::isfinite(utility_bound)) return false;
    }
    for (const auto& a : in.abilities) {
        if (a.range < 0 || a.radius < 0 || a.radius > 100 || !finite(a.shield_amount) ||
            !finite(a.reset_basic_multiplier) || !finite(a.reset_camp_bonus) || !finite(a.reset_enemy_bonus))
            return false;
        if (!std::isfinite(double(a.shield_amount) * in.self_shield_value + utility_bound)) return false;
        for (const auto& t : in.targets)
            if (!finite(rounded(rounded(t.basic_hit * a.reset_basic_multiplier) +
                (t.kind == TargetKind::camp ? a.reset_camp_bonus : a.reset_enemy_bonus)))) return false;
    }
    return true;
}
inline unsigned bit(TargetKind kind) { return kind == TargetKind::camp ? camps : enemies; }
inline bool legal(const Input& in, const State& state, const Command& command) {
    const auto index = command.target_index;
    if (command.kind == Kind::basic) {
        return state.can_attack && index >= 0 && std::size_t(index) < in.targets.size() &&
            in.targets[std::size_t(index)].visible && state.targets[std::size_t(index)].hp > 0 &&
            distance(in.body.pos, in.targets[std::size_t(index)].pos) <= in.body.range;
    }
    if (command.slot < 0 || command.slot >= 2 || !state.can_cast) return false;
    const auto& ability = in.abilities[std::size_t(command.slot)];
    if (!ability.ready || (ability.exclusive && in.body.walked)) return false;
    if (command.kind == Kind::reset)
        return ability.kind == AbilityKind::reset && state.attack_spent && !state.can_attack;
    if (command.kind == Kind::shield)
        return ability.kind == AbilityKind::shield && (ability.target_bits & self) && ability.shield_amount > state.shield;
    if (command.kind != Kind::cast || ability.kind != AbilityKind::damage || distance(in.body.pos, command.center) > ability.range)
        return false;
    if (index < 0) return ability.target_bits & ground;
    return std::size_t(index) < in.targets.size() && in.targets[std::size_t(index)].visible &&
        state.targets[std::size_t(index)].hp > 0 && (ability.target_bits & bit(in.targets[std::size_t(index)].kind));
}
inline void apply(const Input& in, const State& parent, const Command& command, State& child) {
    copy(child, parent, in.targets.size());
    child.word[child.length++] = store(command);
    if (command.kind == Kind::reset) {
        const auto& ability = in.abilities[std::size_t(command.slot)];
        child.can_cast = false; child.can_attack = true;
        child.basic_multiplier = ability.reset_basic_multiplier;
        child.camp_bonus = ability.reset_camp_bonus; child.enemy_bonus = ability.reset_enemy_bonus;
        return;
    }
    if (command.kind == Kind::shield) {
        const auto amount = in.abilities[std::size_t(command.slot)].shield_amount;
        // Declared replace/stronger-only shield model. No enemy response or
        // duration is simulated, and self_shield_value is an explicit utility.
        child.score += double(rounded(amount - child.shield)) * in.self_shield_value;
        child.shield = amount; child.can_cast = false; return;
    }
    const bool basic = command.kind == Kind::basic;
    const auto radius = basic ? in.body.splash : in.abilities[std::size_t(command.slot)].radius;
    for (std::size_t i = 0; i < in.targets.size(); ++i) {
        if (!in.targets[i].visible || child.targets[i].hp <= 0 || distance(in.targets[i].pos, command.center) > radius) continue;
        if (basic && radius == 0 && int(i) != command.target_index) continue;
        if (!basic && command.target_index >= 0 && radius == 0 && int(i) != command.target_index) continue;
        const auto& target = in.targets[i]; const auto before = child.targets[i];
        const float damage = basic ? rounded(rounded(target.basic_hit * child.basic_multiplier) +
            (target.kind == TargetKind::camp ? child.camp_bonus : child.enemy_bonus)) : target.spell_hit[std::size_t(command.slot)];
        const auto residual = hit({before.hp, before.shield}, damage);
        child.targets[i] = {residual.hp, residual.shield};
        const auto after = child.targets[i];
        child.score += target.weight * (double(rounded(before.hp - after.hp)) +
            in.shield_progress * double(rounded(before.shield - after.shield)) +
            (before.hp > 0 && after.hp == 0 ? in.death_bonus : 0));
    }
    if (basic) {
        child.can_attack = false; child.attack_spent = true;
        child.basic_multiplier = 1; child.camp_bonus = child.enemy_bonus = 0;
    } else child.can_cast = false;
}
} // namespace detail

// Every modeled node touches <=32 residuals and inserts into <=16 beam entries.
// Depth<=3; candidate enumeration is finite (<=32 basics + two slots each with
// <=32 direct targets and <=32 ground centers). Quota bounds modeled nodes;
// invalid-menu scans remain bounded by these fixed capacities. Beam pruning
// and quota exhaustion are reported: complete never hides either limitation.
inline Result search(const Input& in) {
    Result result;
    if (in.targets.size() > max_targets || in.ground_centers.size() > max_centers) {
        result.issue = Issue::capacity; return result;
    }
    if (!detail::validate(in)) { result.issue = Issue::invalid_input; return result; }
    result.valid = result.complete = true;
    // Trivial, deliberately uninitialized workspace. Only actual beam slots,
    // target residuals and word prefixes are ever initialized, read or copied.
    std::array<detail::State, max_beam> first, second;
    std::array<unsigned, max_beam> first_order, second_order;
    auto* frontier = first.data(); auto* next = second.data();
    auto* frontier_order = first_order.data(); auto* next_order = second_order.data();
    auto& root = frontier[0];
    root.shield = in.body.shield; root.basic_multiplier = 1; root.camp_bonus = root.enemy_bonus = 0;
    root.can_attack = in.body.can_attack; root.can_cast = in.body.can_cast; root.attack_spent = in.body.attack_spent;
    root.length = 0; root.score = 0;
    for (std::size_t i = 0; i < in.targets.size(); ++i) root.targets[i] = {in.targets[i].hp, in.targets[i].shield};
    frontier_order[0] = 0; unsigned frontier_size = 1;
    bool stopped = false;
    for (unsigned depth = 0; depth < in.depth && frontier_size && !stopped; ++depth) {
        unsigned next_size = 0;
        for (unsigned node = 0; node < frontier_size && !stopped; ++node) {
            const auto& parent = frontier[frontier_order[node]];
            auto consider = [&](Command command) {
                if (stopped || !detail::legal(in, parent, command)) return;
                if (result.nodes == in.node_quota) { result.complete = false; stopped = true; return; }
                ++result.nodes;
                detail::State child;
                detail::apply(in, parent, command, child);
                if (child.score > result.score || (child.score == result.score && child.length < result.length)) {
                    result.score = child.score; result.length = child.length;
                    for (unsigned i = 0; i < child.length; ++i) result.word[i] = detail::command(child.word[i]);
                    result.first = result.word[0];
                }
                unsigned at = 0;
                while (at < next_size && !detail::better(child, next[next_order[at]])) ++at;
                unsigned slot;
                if (next_size == in.beam_width) {
                    ++result.beam_pruned; result.complete = false;
                    if (at == next_size) return;
                    slot = next_order[next_size - 1];
                } else slot = next_size++;
                for (unsigned j = next_size - 1; j > at; --j) next_order[j] = next_order[j - 1];
                next_order[at] = slot;
                detail::copy(next[slot], child, in.targets.size());
            };
            for (std::size_t i = 0; i < in.targets.size() && !stopped; ++i)
                consider({Kind::basic, -1, in.targets[i].id, int(i), in.targets[i].pos});
            for (int slot = 0; slot < 2 && !stopped; ++slot) {
                const auto& ability = in.abilities[std::size_t(slot)];
                if (ability.kind == AbilityKind::reset) consider({Kind::reset, slot});
                else if (ability.kind == AbilityKind::shield) consider({Kind::shield, slot});
                else if (ability.kind == AbilityKind::damage) {
                    for (std::size_t i = 0; i < in.targets.size() && !stopped; ++i)
                        consider({Kind::cast, slot, in.targets[i].id, int(i), in.targets[i].pos});
                    if (ability.target_bits & ground) {
                        if (in.ground_centers.empty()) {
                            for (std::size_t i = 0; i < in.targets.size() && !stopped; ++i)
                                if (in.targets[i].visible && parent.targets[i].hp > 0)
                                    consider({Kind::cast, slot, -1, -1, in.targets[i].pos});
                        } else for (const auto center : in.ground_centers) { consider({Kind::cast, slot, -1, -1, center}); if (stopped) break; }
                    }
                }
            }
        }
        std::swap(frontier, next); std::swap(frontier_order, next_order); frontier_size = next_size;
    }
    return result;
}
} // namespace mosaic::word
