#pragma once
// Restricted, pure owner-turn model. Geometry/HP and next-volley conclusions are
// conditional on the caller's complete frame; these functions make no SDK calls.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace mb::service {

inline constexpr std::size_t max_members = 32;
inline constexpr std::size_t spell_slots = 2;
inline constexpr std::size_t max_primitives = 3; // basic, one cast, reset basic
inline constexpr std::size_t first_basic_limit = 4;
inline constexpr std::size_t max_fragments = (first_basic_limit + 1) * (spell_slots + 1);

struct Point
{
    int x{}, y{};
    friend bool operator==(Point, Point) = default;
};

inline int distance(Point a, Point b)
{
    const auto dx = std::abs(static_cast<long long>(a.x) - b.x);
    const auto dy = std::abs(static_cast<long long>(a.y) - b.y);
    return static_cast<int>(std::min<long long>(std::max(dx,dy),std::numeric_limits<int>::max()));
}

struct Body
{
    Point pos;
    float hp{}, max_hp{}, shield{}, strength{}, focus{};
    int attack_range{1};
    bool ranged{}, splash{};
    float camp_damage_multiplier{1.0f}; // damage received from camps
    bool can_move{true}, can_attack{true}, can_cast{true}, moved_this_turn{};
    int next_basic_stun{}; // an enhanced reset's next primary basic
};

struct Member
{
    int id{-1}, group{};
    Point pos;
    float hp{}, strength{};
    int range{};
    bool awake{};
    int stun_remaining{};
    bool hp_exact{};
};

enum class SpellKind { none, damage, aoe, cc, reset, shield };
struct Spell
{
    SpellKind kind{SpellKind::none};
    bool ready{};
    int range{};
    float damage{}; // runtime-resolved HP damage, or shield amount for shield
    int radius{}, duration{};
    bool exclusive{};
};

struct State
{
    Body body;
    std::array<Member,max_members> members{};
    std::size_t member_count{};
    std::array<Spell,spell_slots> spells{};
    int width{}, height{}; // required for exhaustive in-map AoE centers
    bool incoming_complete{true};
};

enum class PrimitiveKind { basic, cast };
struct Primitive
{
    PrimitiveKind kind{PrimitiveKind::basic};
    int member_id{-1}, slot{-1};
    Point aim;
};

struct Volley
{
    float gross_damage{}, shield_absorbed{}, hp_damage{};
    bool complete{};
};

struct Fragment
{
    std::array<Primitive,max_primitives> primitives{};
    std::size_t primitive_count{};
    State result;
    float effective_damage{}, stopped_strength{};
    int kills{};
    Volley next_volley;
    float minimum_prefix_hp_after_volley{};
    bool complete{};
};

enum class ModelStatus { ready, incomplete, invalid };
enum class HpRelation { unknown, unchanged, decreased, increased };

inline bool nonnegative(float value) { return std::isfinite(value) && value >= 0.0f; }

inline ModelStatus state_status(const State& state)
{
    if (state.member_count > max_members) return ModelStatus::incomplete;
    const auto& body = state.body;
    if (!nonnegative(body.hp) || !nonnegative(body.max_hp) || !nonnegative(body.shield) ||
        !nonnegative(body.strength) || !nonnegative(body.focus) || !nonnegative(body.camp_damage_multiplier) ||
        body.attack_range < 0 || body.next_basic_stun < 0) return ModelStatus::invalid;
    bool complete = state.incoming_complete;
    const auto inside = [&](Point point) {
        return point.x >= 0 && point.y >= 0 && point.x < state.width && point.y < state.height;
    };
    if (state.width > 0 && state.height > 0 && !inside(body.pos)) return ModelStatus::invalid;
    for (std::size_t i = 0; i < state.member_count; ++i)
    {
        const auto& member = state.members[i];
        if (member.id < 0 || !nonnegative(member.hp) || !nonnegative(member.strength) ||
            member.range < 0 || member.stun_remaining < 0) return ModelStatus::invalid;
        if (state.width > 0 && state.height > 0 && !inside(member.pos)) return ModelStatus::invalid;
        complete = complete && member.hp_exact;
        for (std::size_t j = 0; j < i; ++j)
            if (member.id == state.members[j].id) return ModelStatus::invalid;
    }
    for (const auto& spell : state.spells)
    {
        if (!nonnegative(spell.damage) || spell.range < 0 || spell.radius < 0 || spell.duration < 0)
            return ModelStatus::invalid;
        if (spell.ready && spell.kind == SpellKind::aoe && (state.width <= 0 || state.height <= 0)) complete = false;
    }
    return complete ? ModelStatus::ready : ModelStatus::incomplete;
}

// A projected maximum is not a new observation. Even an exact increase only detects
// a residual-state contradiction; this function does not certify a respawn/generation.
inline HpRelation compare_hp_observations(const Member& before, const Member& after)
{
    if (!before.hp_exact || !after.hp_exact || before.id != after.id || before.group != after.group ||
        before.pos != after.pos || !nonnegative(before.hp) || !nonnegative(after.hp)) return HpRelation::unknown;
    if (after.hp < before.hp) return HpRelation::decreased;
    if (after.hp > before.hp) return HpRelation::increased;
    return HpRelation::unchanged;
}

inline int member_index(const State& state, int id)
{
    if (state.member_count > max_members) return -1;
    for (std::size_t i = 0; i < state.member_count; ++i)
        if (state.members[i].id == id) return static_cast<int>(i);
    return -1;
}

inline void wake_group(State& state, int group)
{
    for (std::size_t i = 0; i < state.member_count; ++i)
        if (state.members[i].group == group && state.members[i].hp > 0.0f) state.members[i].awake = true;
}

namespace detail {
inline bool apply_basic_unchecked(State& state, int member_id)
{
    if (state.member_count > max_members || state.body.hp <= 0.0f || !state.body.can_attack || state.body.strength <= 0.0f) return false;
    const int target = member_index(state, member_id);
    if (target < 0 || state.members[static_cast<std::size_t>(target)].hp <= 0.0f ||
        distance(state.body.pos, state.members[static_cast<std::size_t>(target)].pos) > state.body.attack_range) return false;
    const auto center = state.members[static_cast<std::size_t>(target)].pos;
    std::array<bool,max_members> hit{};
    for (std::size_t i = 0; i < state.member_count; ++i)
        hit[i] = state.members[i].hp > 0.0f && (i == static_cast<std::size_t>(target) ||
            (state.body.splash && distance(center,state.members[i].pos) <= 1));
    for (std::size_t i = 0; i < state.member_count; ++i) if (hit[i]) wake_group(state,state.members[i].group);
    for (std::size_t i = 0; i < state.member_count; ++i)
        if (hit[i]) state.members[i].hp = std::max(0.0f,state.members[i].hp-state.body.strength);
    if (state.body.next_basic_stun > 0 && state.members[static_cast<std::size_t>(target)].hp > 0.0f)
        state.members[static_cast<std::size_t>(target)].stun_remaining = std::max(
            state.members[static_cast<std::size_t>(target)].stun_remaining,state.body.next_basic_stun);
    state.body.next_basic_stun = 0;
    state.body.can_attack = false;
    if (state.body.ranged) state.body.can_move = false;
    return true;
}
} // namespace detail

inline bool apply_basic(State& state, int member_id)
{
    return state_status(state) != ModelStatus::invalid && detail::apply_basic_unchecked(state,member_id);
}

namespace detail {
inline bool apply_cast_unchecked(State& state, int slot, Point aim, int member_id)
{
    if (state.member_count > max_members || slot < 0 ||
        slot >= static_cast<int>(spell_slots) || state.body.hp <= 0.0f || !state.body.can_cast) return false;
    auto& spell = state.spells[static_cast<std::size_t>(slot)];
    if (!spell.ready || spell.kind == SpellKind::none || (spell.exclusive && state.body.moved_this_turn)) return false;
    int target = -1;
    if (spell.kind == SpellKind::damage || spell.kind == SpellKind::cc)
    {
        target = member_index(state,member_id);
        if (target < 0 || state.members[static_cast<std::size_t>(target)].hp <= 0.0f ||
            state.members[static_cast<std::size_t>(target)].pos != aim || distance(state.body.pos,aim) > spell.range) return false;
    }
    if (spell.kind == SpellKind::aoe && (state.width <= 0 || state.height <= 0 || aim.x < 0 || aim.y < 0 ||
        aim.x >= state.width || aim.y >= state.height || distance(state.body.pos,aim) > spell.range)) return false;
    if (spell.kind == SpellKind::reset && state.body.can_attack) return false;
    if (spell.kind == SpellKind::shield && (spell.damage <= 0.0f || spell.duration <= 0)) return false;
    if (spell.kind == SpellKind::reset)
    {
        state.body.can_attack = true;
        // Caller supplies an established enhanced-basic stun premise, or zero.
        state.body.next_basic_stun = std::max(state.body.next_basic_stun,spell.duration);
    }
    else if (spell.kind == SpellKind::shield)
    {
        // In the supported self-shield kit a new cast cannot overlap its own previous
        // duration under the pinned cooldown. Max is conservative for other shielding.
        state.body.shield = std::max(state.body.shield,spell.damage);
    }
    else
    {
        std::array<bool,max_members> hit{};
        for (std::size_t i = 0; i < state.member_count; ++i)
            hit[i] = state.members[i].hp > 0.0f && (spell.kind == SpellKind::aoe
                ? distance(aim,state.members[i].pos) <= spell.radius : i == static_cast<std::size_t>(target));
        for (std::size_t i = 0; i < state.member_count; ++i) if (hit[i]) wake_group(state,state.members[i].group);
        for (std::size_t i = 0; i < state.member_count; ++i)
            if (hit[i])
            {
                state.members[i].hp = std::max(0.0f,state.members[i].hp-spell.damage);
                if (state.members[i].hp > 0.0f && (spell.kind == SpellKind::cc || spell.kind == SpellKind::aoe))
                    state.members[i].stun_remaining = std::max(state.members[i].stun_remaining,spell.duration);
            }
    }
    spell.ready = false;
    state.body.can_cast = false;
    if (spell.exclusive) state.body.can_move = false;
    return true;
}
} // namespace detail

inline bool apply_cast(State& state, int slot, Point aim, int member_id = -1)
{
    return state_status(state) != ModelStatus::invalid && detail::apply_cast_unchecked(state,slot,aim,member_id);
}

namespace detail {
inline Volley volley_unchecked(const State& state, bool complete)
{
    Volley out;
    out.complete = complete;
    // Conditional camp-opportunity model: a positive current stun denies this next
    // opportunity. No ally tanking, unseen wake, retaliation or intervening enemy action
    // is credited. The caller supplies all potential sources and a separate escape reserve.
    for (std::size_t i = 0; i < state.member_count; ++i)
    {
        const auto& member = state.members[i];
        if (member.hp > 0.0f && member.awake && member.stun_remaining <= 0 &&
            distance(state.body.pos,member.pos) <= member.range)
            out.gross_damage += member.strength*state.body.camp_damage_multiplier;
    }
    out.shield_absorbed = std::min(state.body.shield,out.gross_damage);
    out.hp_damage = out.gross_damage-out.shield_absorbed;
    out.complete = out.complete && std::isfinite(out.gross_damage) && std::isfinite(out.hp_damage);
    return out;
}
} // namespace detail

inline Volley next_camp_volley(const State& state)
{
    const auto status = state_status(state);
    if (status == ModelStatus::invalid || state.member_count > max_members) return {};
    return detail::volley_unchecked(state,status == ModelStatus::ready);
}

namespace detail {
struct Effect
{
    float stopped{}, damage{};
    int kills{};
};
inline bool better(Effect a, Effect b)
{
    if (a.stopped != b.stopped) return a.stopped > b.stopped;
    if (a.damage != b.damage) return a.damage > b.damage;
    return a.kills > b.kills;
}
inline Effect effect(const State& state, Point center, int primary, float damage, int radius, int stun)
{
    Effect out;
    for (std::size_t i = 0; i < state.member_count; ++i)
    {
        const auto& member = state.members[i];
        if (member.hp <= 0.0f || (radius >= 0 ? distance(center,member.pos) > radius : static_cast<int>(i) != primary)) continue;
        out.damage += std::min(member.hp,damage);
        const bool killed = member.hp <= damage;
        out.kills += killed;
        if ((killed || stun > 0) && member.stun_remaining <= 0 && distance(state.body.pos,member.pos) <= member.range)
            out.stopped += member.strength;
    }
    return out;
}
inline std::vector<int> basic_targets(const State& state, std::size_t limit)
{
    struct Candidate { int id; Effect effect; };
    std::vector<Candidate> best;
    best.reserve(std::min(limit,state.member_count));
    if (!state.body.can_attack || state.body.strength <= 0.0f) return {};
    for (std::size_t i = 0; i < state.member_count; ++i)
    {
        const auto& member = state.members[i];
        if (member.hp <= 0.0f || distance(state.body.pos,member.pos) > state.body.attack_range) continue;
        auto score = effect(state,member.pos,static_cast<int>(i),state.body.strength,state.body.splash ? 1 : -1,0);
        if (state.body.next_basic_stun > 0 && member.stun_remaining <= 0 && member.hp > state.body.strength &&
            distance(state.body.pos,member.pos) <= member.range) score.stopped += member.strength;
        auto at = best.begin();
        while (at != best.end() && !better(score,at->effect)) ++at;
        best.insert(at,{member.id,score});
        if (best.size() > limit) best.pop_back();
    }
    std::vector<int> result;
    result.reserve(best.size());
    for (const auto& item : best) result.push_back(item.id);
    return result;
}
inline std::optional<Primitive> best_cast(const State& state, int slot)
{
    const auto& spell = state.spells[static_cast<std::size_t>(slot)];
    if (!state.body.can_cast || !spell.ready || spell.kind == SpellKind::none ||
        (spell.exclusive && state.body.moved_this_turn)) return std::nullopt;
    if (spell.kind == SpellKind::reset)
        return state.body.can_attack ? std::nullopt : std::optional<Primitive>{{PrimitiveKind::cast,-1,slot,state.body.pos}};
    if (spell.kind == SpellKind::shield)
        return spell.damage > 0.0f && spell.duration > 0 ? std::optional<Primitive>{{PrimitiveKind::cast,-1,slot,state.body.pos}} : std::nullopt;
    std::optional<Primitive> best;
    Effect best_effect;
    const auto consider = [&](Point aim, int target, Effect candidate) {
        if ((candidate.damage > 0.0f || candidate.stopped > 0.0f) && (!best || better(candidate,best_effect)))
        {
            best = Primitive{PrimitiveKind::cast,target,slot,aim};
            best_effect = candidate;
        }
    };
    if (spell.kind == SpellKind::aoe)
    {
        if (state.width <= 0 || state.height <= 0) return std::nullopt;
        const int lo_x = static_cast<int>(std::max<long long>(0,static_cast<long long>(state.body.pos.x)-spell.range));
        const int hi_x = static_cast<int>(std::min<long long>(state.width-1,static_cast<long long>(state.body.pos.x)+spell.range));
        const int lo_y = static_cast<int>(std::max<long long>(0,static_cast<long long>(state.body.pos.y)-spell.range));
        const int hi_y = static_cast<int>(std::min<long long>(state.height-1,static_cast<long long>(state.body.pos.y)+spell.range));
        if (hi_x < lo_x || hi_y < lo_y) return std::nullopt;
        const auto columns = static_cast<std::size_t>(hi_x-lo_x+1);
        const auto rows = static_cast<std::size_t>(hi_y-lo_y+1);
        if (rows > std::numeric_limits<std::size_t>::max()/columns) return std::nullopt;
        std::vector<Effect> center_effects(rows*columns);
        // Accumulate each member into the centers that hit it. With radius one this
        // visits at most nine centers/member, rather than scanning every member for
        // every center. Bounds come from the supplied map/range/radius, not a fuel claim.
        for (std::size_t i = 0; i < state.member_count; ++i)
        {
            const auto& member = state.members[i];
            if (member.hp <= 0.0f) continue;
            Effect contribution;
            contribution.damage = std::min(member.hp,spell.damage);
            contribution.kills = member.hp <= spell.damage;
            if ((contribution.kills || spell.duration > 0) && member.stun_remaining <= 0 &&
                distance(state.body.pos,member.pos) <= member.range) contribution.stopped = member.strength;
            const int from_x = static_cast<int>(std::max<long long>(lo_x,static_cast<long long>(member.pos.x)-spell.radius));
            const int to_x = static_cast<int>(std::min<long long>(hi_x,static_cast<long long>(member.pos.x)+spell.radius));
            const int from_y = static_cast<int>(std::max<long long>(lo_y,static_cast<long long>(member.pos.y)-spell.radius));
            const int to_y = static_cast<int>(std::min<long long>(hi_y,static_cast<long long>(member.pos.y)+spell.radius));
            for (int y = from_y; y <= to_y; ++y)
                for (int x = from_x; x <= to_x; ++x)
                {
                    auto& value = center_effects[static_cast<std::size_t>(y-lo_y)*columns+static_cast<std::size_t>(x-lo_x)];
                    value.damage += contribution.damage;
                    value.stopped += contribution.stopped;
                    value.kills += contribution.kills;
                }
        }
        for (int y = lo_y; y <= hi_y; ++y)
            for (int x = lo_x; x <= hi_x; ++x)
                consider({x,y},-1,center_effects[static_cast<std::size_t>(y-lo_y)*columns+static_cast<std::size_t>(x-lo_x)]);
    }
    else
        for (std::size_t i = 0; i < state.member_count; ++i)
        {
            const auto& member = state.members[i];
            if (member.hp > 0.0f && distance(state.body.pos,member.pos) <= spell.range)
            {
                Effect contribution;
                contribution.damage = std::min(member.hp,spell.damage);
                contribution.kills = member.hp <= spell.damage;
                if ((contribution.kills || (spell.kind == SpellKind::cc && spell.duration > 0)) &&
                    member.stun_remaining <= 0 && distance(state.body.pos,member.pos) <= member.range)
                    contribution.stopped = member.strength;
                consider(member.pos,member.id,contribution);
            }
        }
    return best;
}
inline void append(Fragment& fragment, const Primitive& primitive)
{
    fragment.primitives[fragment.primitive_count++] = primitive;
    const auto volley = volley_unchecked(fragment.result,fragment.complete);
    fragment.minimum_prefix_hp_after_volley = std::min(fragment.minimum_prefix_hp_after_volley,
        fragment.result.body.hp-volley.hp_damage);
    fragment.complete = fragment.complete && volley.complete;
}
inline void finish(const State& before, Fragment& fragment)
{
    fragment.next_volley = volley_unchecked(fragment.result,fragment.complete);
    fragment.complete = fragment.complete && fragment.next_volley.complete;
    if (fragment.primitive_count == 0)
        fragment.minimum_prefix_hp_after_volley = fragment.result.body.hp-fragment.next_volley.hp_damage;
    for (std::size_t i = 0; i < before.member_count; ++i)
    {
        const auto& old = before.members[i];
        const auto& now = fragment.result.members[i];
        fragment.effective_damage += std::max(0.0f,old.hp-now.hp);
        fragment.kills += old.hp > 0.0f && now.hp <= 0.0f;
        if (old.hp > 0.0f && old.stun_remaining <= 0 && (now.hp <= 0.0f || now.stun_remaining > 0) &&
            distance(before.body.pos,old.pos) <= old.range) fragment.stopped_strength += old.strength;
    }
    fragment.complete = fragment.complete && std::isfinite(fragment.effective_damage) &&
        std::isfinite(fragment.stopped_strength) && std::isfinite(fragment.minimum_prefix_hp_after_volley);
}
} // namespace detail

// Representative, explicitly nonoptimal family: no basic plus the best four first
// basics; per prefix each slot uses its best pointwise cast and a best remaining basic.
// At most max_fragments=15 are returned. AoE-center work is bounded by the supplied
// in-map cast box (<=min(width,2range+1)*min(height,2range+1)); each score visits N<=32.
inline std::vector<Fragment> enumerate_fragments(const State& state)
{
    std::vector<Fragment> result;
    const auto status = state_status(state);
    if (state.member_count > max_members || status == ModelStatus::invalid) return result;
    const auto targets = detail::basic_targets(state,first_basic_limit);
    result.reserve((targets.size()+1)*(spell_slots+1));
    for (std::size_t prefix = 0; prefix <= targets.size(); ++prefix)
    {
        Fragment base;
        base.result = state;
        base.complete = status == ModelStatus::ready;
        base.minimum_prefix_hp_after_volley = std::numeric_limits<float>::infinity();
        if (prefix > 0)
        {
            const int id = targets[prefix-1];
            const int index = member_index(state,id);
            const Primitive basic{PrimitiveKind::basic,id,-1,state.members[static_cast<std::size_t>(index)].pos};
            if (!detail::apply_basic_unchecked(base.result,id)) continue;
            detail::append(base,basic);
        }
        Fragment no_cast = base;
        detail::finish(state,no_cast);
        result.push_back(std::move(no_cast));
        for (int slot = 0; slot < static_cast<int>(spell_slots); ++slot)
        {
            const auto cast = detail::best_cast(base.result,slot);
            if (!cast) continue;
            Fragment fragment = base;
            if (!detail::apply_cast_unchecked(fragment.result,slot,cast->aim,cast->member_id)) continue;
            detail::append(fragment,*cast);
            const auto after = detail::basic_targets(fragment.result,1);
            if (!after.empty())
            {
                const int id = after.front();
                const int index = member_index(fragment.result,id);
                const Primitive basic{PrimitiveKind::basic,id,-1,fragment.result.members[static_cast<std::size_t>(index)].pos};
                if (detail::apply_basic_unchecked(fragment.result,id)) detail::append(fragment,basic);
            }
            detail::finish(state,fragment);
            result.push_back(std::move(fragment));
        }
    }
    return result;
}

inline std::optional<Fragment> choose_fragment(const State& state, float escape_reserve)
{
    if (!nonnegative(escape_reserve) || state_status(state) != ModelStatus::ready) return std::nullopt;
    std::optional<Fragment> best;
    for (auto& fragment : enumerate_fragments(state))
    {
        if (!fragment.complete || fragment.effective_damage <= 0.0f ||
            fragment.minimum_prefix_hp_after_volley <= escape_reserve) continue;
        if (!best || fragment.stopped_strength > best->stopped_strength ||
            (fragment.stopped_strength == best->stopped_strength && fragment.effective_damage > best->effective_damage) ||
            (fragment.stopped_strength == best->stopped_strength && fragment.effective_damage == best->effective_damage &&
                fragment.next_volley.hp_damage < best->next_volley.hp_damage)) best = std::move(fragment);
    }
    return best;
}

} // namespace mb::service
