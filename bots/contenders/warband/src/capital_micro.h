#pragma once
#ifndef WB_CAPITAL_MICRO_HOST_FREE
#include <cambc.h>
#endif
#include <algorithm>
#include <array>
#include <optional>
#include <type_traits>

// Bounded primitive words. Context is a local macro estimate, not a safety
// certificate about hidden champions, future arrivals, or all enemy responses.
namespace wb::capital_micro {
struct Context {
    float incoming_before_next = 0;
    float remaining_work_inputs = 1;
};
struct Result {
    unsigned basics = 0;
    bool cast = false, supported = false;
    bool acted() const { return basics != 0 || cast || supported; }
    void merge(const Result& x) {
        basics += x.basics; cast |= x.cast; supported |= x.supported;
    }
};
namespace detail {
// Empirical dispatch reserve. Native games must still measure actual health.
inline bool epoch(unsigned round) {
    return cambc::round_number() == round && cambc::fuel_remaining() >= 9000;
}
inline bool free_body() {
    const auto& me = cambc::self();
    const auto state = me.state();
    return state.contains(cambc::champion_state::alive)
        && !state.contains(cambc::champion_state::recalling)
        && !state.contains(cambc::champion_state::submitting_gems)
        && !state.contains(cambc::champion_state::teleporting)
        && !me.buffs().has_stun();
}
inline bool can_basic(unsigned round) {
    return epoch(round) && free_body() && cambc::self().can_attack()
        && !cambc::self().restrictions().contains(cambc::buff_restrictions::attack);
}
inline bool can_cast(unsigned round, const cambc::ability& ability) {
    return epoch(round) && free_body() && ability.is_ready()
        && !cambc::self().restrictions().contains(cambc::buff_restrictions::cast)
        // Specific exclusive casts are not made ready merely by global C.
        // Unused M is a conservative sufficient precondition for this word.
        && (!std::visit([](const auto& p) { return p.exclusive_with_movement; },ability.info().value())
            || cambc::self().can_move());
}
inline bool live(const cambc::camp& target) {
    return target.is_visible() && target.is_alive() && target.health() > 0;
}
inline bool live(const cambc::champion& target) {
    return target.is_visible() && target.is_alive() && target.health() > 0;
}
template<class Target> inline bool basic_range(const Target& target) {
    return cambc::self().position().chebyshev_distance(target.position())
        <= cambc::self().attack_range();
}
template<class Target> inline bool permitted(const cambc::ability& ability) {
    if constexpr (std::is_same_v<Target,cambc::camp>) return ability.can_target_camps();
    else return ability.can_target_enemies();
}
template<class Target> inline bool basic(const Target& target, unsigned round) {
    if (!can_basic(round) || !live(target) || !basic_range(target) || !epoch(round)) return false;
    cambc::self().attack(target);
    return true;
}
template<class Target> inline bool spell(const Target& target, unsigned round) {
    for (const auto& ability : cambc::self().abilities()) {
        if (!can_cast(round,ability) || !live(target)) continue;
        if (!permitted<Target>(ability) && !ability.can_target_ground()) continue;
        if (!(ability.is_damage() || ability.is_cc() || ability.is_aoe_damage())) continue;
        // Range, category, target life, owner availability remain distinct.
        if (!ability.valid_tiles().is_set(target.position()) || !epoch(round)) continue;
        if (ability.can_target_ground()) return ability.use(target.position());
        return ability.use(target);
    }
    return false;
}
inline float shield_health() {
    float result = 0;
    const auto buffs = cambc::self().buffs();
    for (const auto& buff : buffs)
        if (const auto* shield = std::get_if<cambc::buff_shield>(&buff)) result += shield->value;
    return result;
}
template<class Target> inline bool shield_for_work(const Target& target, Context c, unsigned round) {
    if (!(c.incoming_before_next > 0) || !live(target)) return false;
    const auto& me = cambc::self();
    const float exposure = c.incoming_before_next * std::max(1.0f,c.remaining_work_inputs);
    // Use the cast for a shield when estimated remaining work does not fit
    // current HP. No fixed health trigger, no future-safety inference.
    if (me.health()+shield_health() > exposure) return false;
    for (const auto& ability : me.abilities()) {
        if (!ability.is_shield() || !can_cast(round,ability) || !ability.can_target_self()) continue;
        return epoch(round) && ability.use();
    }
    return false;
}
template<class Target> inline bool reset(const Target& target, unsigned round) {
    if (cambc::self().can_attack() || !live(target) || !basic_range(target)) return false;
    for (const auto& ability : cambc::self().abilities()) {
        if (!ability.is_attack_reset() || !can_cast(round,ability) || !ability.can_target_self()) continue;
        return epoch(round) && ability.use();
    }
    return false;
}
} // namespace detail

// Willow restores actual visible ally health. Her reset is self-only. This
// allocation compares useful healing with the immediate own extra-basic
// opportunity; it is a stamina heuristic, not an optimality theorem.
inline Result support(Context = {}) {
    Result result;
    const auto round = cambc::round_number();
    if (!detail::epoch(round) || !detail::free_body()) return result;
    const auto self_handle = static_cast<cambc::handle_champion_t>(cambc::self());
    for (const auto& ability : cambc::self().abilities()) {
        if (!ability.is_heal() || !detail::can_cast(round,ability) || !ability.can_target_allies()) continue;
        const auto mask = ability.valid_tiles();
        std::optional<cambc::champion> selected;
        float useful = 0;
        const float raw_heal = ability.heal_amount();
        for (const auto& ally : cambc::vision::champions(cambc::team::ally)) {
            if (static_cast<cambc::handle_champion_t>(ally) == self_handle || !detail::live(ally)
                || !mask.is_set(ally.position())) continue;
            const float amount = std::min(raw_heal,ally.max_health()-ally.health());
            if (amount > useful) { useful = amount; selected = ally; }
        }
        if (!selected || useful <= cambc::self().strength()) continue;
        if (!detail::can_cast(round,ability) || !detail::live(*selected)
            || !ability.valid_tiles().is_set(selected->position()) || !detail::epoch(round)) return result;
        result.supported = ability.use(*selected); result.cast = result.supported;
        return result;
    }
    return result;
}

template<class Target> inline Result engage(const Target& target, Context context = {}) {
    static_assert(std::is_same_v<Target,cambc::camp> || std::is_same_v<Target,cambc::champion>);
    Result result;
    const auto round = cambc::round_number();
    if (!detail::epoch(round) || !detail::free_body() || !detail::live(target)) return result;
    // Exclusive damage/stun precedes any possible walk. Caller re-queries M/A/C.
    // Standing ranged casters can then basic without an ordinary walk.
    result.cast = detail::spell(target,round);
    if (!result.cast) result.cast = detail::shield_for_work(target,context,round);
    if (detail::basic(target,round)) ++result.basics;
    // A Vendetta kill refreshes cooldowns, never the spent single-cast input.
    // Fresh ability readiness enforces that distinction.
    if (!result.cast && detail::reset(target,round)) {
        result.cast = true;
        if (detail::basic(target,round)) ++result.basics;
    }
    return result;
}

// Optional compatible dash. Actual native refusal remains a refusal, not
// observed motion. Parent retains route/pressure/capture authority.
inline bool gap_dash(cambc::vec2 toward) {
    const auto round = cambc::round_number();
    if (!detail::epoch(round) || !detail::free_body()) return false;
    auto& me = cambc::self();
    const auto origin = me.position();
    for (const auto& ability : me.abilities()) {
        if (!ability.is_dash() || !detail::can_cast(round,ability)) continue;
        const auto payload = ability.get<cambc::ability_dash>();
        if (payload.exclusive_with_movement || !ability.can_target_ground()) continue;
        auto blocked = cambc::map::walls() | cambc::map::altars()
            | cambc::map::spawn_region(cambc::team::enemy) | cambc::vision::champion_tiles()
            | cambc::vision::camp_tiles() | cambc::vision::teleport_tiles();
        blocked.set(origin,false);
        const auto reachable = blocked.reachable_tiles(origin,static_cast<std::uint16_t>(payload.range));
        const auto holes = cambc::map::holes();
        cambc::vec2 chosen = origin;
        for (const auto landing : reachable.positions()) {
            if (holes.is_set(landing)) continue;
            if (landing.chebyshev_distance(toward) < chosen.chebyshev_distance(toward)) chosen = landing;
        }
        if (chosen == origin || !detail::can_cast(round,ability) || !detail::epoch(round)) return false;
        return ability.use(chosen);
    }
    return false;
}
} // namespace wb::capital_micro

// Optional hook domain. The complete frozen legacy API above is unchanged.
#include <cstdint>
namespace wb::capital_micro::guarded {
enum class Primitive { Basic,Cast,Move };
struct Attempt {
    Primitive kind{Primitive::Basic};unsigned round{};int target{-1},slot{-1};
    cambc::vec2 destination{-1,-1};
};
struct Hooks {
    void* context{};
    bool (*before)(void*,const Attempt&){};
    void (*after)(void*,const Attempt&,bool accepted){};
};
using Context=::wb::capital_micro::Context;
using Result=::wb::capital_micro::Result;
namespace detail {
inline bool active(const Hooks* hooks){return hooks&&(hooks->before||hooks->after);}
// Empirical dispatch reserve. Native games must still measure actual health.
inline bool epoch(unsigned round) {
    return cambc::round_number() == round && cambc::fuel_remaining() >= 9000;
}
inline bool raw_attempt(const Attempt& actual,unsigned target_kind) {
    if(actual.kind==Primitive::Basic) {
        cambc::detail::check(cambc::detail::sdk_attack(target_kind,static_cast<std::uint32_t>(actual.target)));
        return true;
    }
    std::int32_t accepted=0;
    if(actual.kind==Primitive::Cast)
        cambc::detail::check(cambc::detail::sdk_cast(static_cast<std::uint32_t>(actual.slot),target_kind,
            target_kind?static_cast<std::uint32_t>(actual.target):0,actual.destination.x,actual.destination.y,&accepted));
    else
        cambc::detail::check(cambc::detail::sdk_move_to(actual.destination.x,actual.destination.y,&accepted));
    return accepted!=0;
}
// Callbacks observe/rebind caller authority; they issue no primitive. Denial
// stops this invocation. A throwing command leaves its before-claim pending for
// the caller's existing boundary/next-input reconciliation, not a fake refusal.
template<bool Enabled> class Invocation {
    const Hooks* hooks_{};bool stopped_{};
public:
    explicit Invocation(const Hooks* hooks):hooks_(Enabled?hooks:nullptr){}
    bool stopped() const {return stopped_;}
    template<class Describe,class Call> bool attempt(unsigned round,Describe&& describe,Call&& call,unsigned target_kind=0) {
        if constexpr(!Enabled)return call();
        else {
            if(!hooks_||(!hooks_->before&&!hooks_->after))return call();
            if(stopped_)return false;
            const Attempt actual=describe();
            if(!epoch(round)){stopped_=true;return false;}
            // before(TRUE) owns the FINAL actual owner/epoch/fuel fence and
            // consumes caller authority. No SDK import intervenes between that
            // return and the ONE raw physical command below. Checked SDK errors
            // throw (no after); a normal cast refusal reports after(false).
            if(hooks_->before&&!hooks_->before(hooks_->context,actual)){stopped_=true;return false;}
            // An after-only observer has no lease/final-fence contract: retain
            // its legacy SDK wrapper and merely report that returned attempt.
            const bool accepted=hooks_->before?raw_attempt(actual,target_kind):call();
            if(hooks_->after)hooks_->after(hooks_->context,actual,accepted);
            return accepted;
        }
    }
};
template<class Target> inline int target_handle(const Target& target) {
    if constexpr(std::is_same_v<Target,cambc::camp>)return static_cast<int>(static_cast<cambc::handle_camp_t>(target));
    else return static_cast<int>(static_cast<cambc::handle_champion_t>(target));
}
template<class Target> inline constexpr unsigned target_kind() {
    if constexpr(std::is_same_v<Target,cambc::camp>)return static_cast<unsigned>(cambc::detail::entity_kind::camp);
    else return static_cast<unsigned>(cambc::detail::entity_kind::champion);
}
inline bool free_body() {
    const auto& me = cambc::self();
    const auto state = me.state();
    return state.contains(cambc::champion_state::alive)
        && !state.contains(cambc::champion_state::recalling)
        && !state.contains(cambc::champion_state::submitting_gems)
        && !state.contains(cambc::champion_state::teleporting)
        && !me.buffs().has_stun();
}
inline bool can_basic(unsigned round) {
    return epoch(round) && free_body() && cambc::self().can_attack()
        && !cambc::self().restrictions().contains(cambc::buff_restrictions::attack);
}
inline bool can_cast(unsigned round, const cambc::ability& ability) {
    return epoch(round) && free_body() && ability.is_ready()
        && !cambc::self().restrictions().contains(cambc::buff_restrictions::cast)
        // Specific exclusive casts are not made ready merely by global C.
        // Unused M is a conservative sufficient precondition for this word.
        && (!std::visit([](const auto& p) { return p.exclusive_with_movement; },ability.info().value())
            || cambc::self().can_move());
}
inline bool live(const cambc::camp& target) {
    return target.is_visible() && target.is_alive() && target.health() > 0;
}
inline bool live(const cambc::champion& target) {
    return target.is_visible() && target.is_alive() && target.health() > 0;
}
template<class Target> inline bool basic_range(const Target& target) {
    return cambc::self().position().chebyshev_distance(target.position())
        <= cambc::self().attack_range();
}
template<class Target> inline bool permitted(const cambc::ability& ability) {
    if constexpr (std::is_same_v<Target,cambc::camp>) return ability.can_target_camps();
    else return ability.can_target_enemies();
}
template<bool Enabled,class Target> inline bool basic_invoke(const Target& target,unsigned round,Invocation<Enabled>& invocation) {
    if(invocation.stopped())return false;
    if (!can_basic(round) || !live(target) || !basic_range(target) || !epoch(round)) return false;
    return invocation.attempt(round,[&]{return Attempt{Primitive::Basic,round,target_handle(target),-1,{-1,-1}};},
        [&]{cambc::self().attack(target);return true;},target_kind<Target>());
}
template<bool EnableHooks=true,class Target> inline bool basic(const Target& target,unsigned round,const Hooks* hooks=nullptr) {
    if constexpr(!EnableHooks)return ::wb::capital_micro::detail::basic(target,round);
    if(!active(hooks))return ::wb::capital_micro::detail::basic(target,round);
    Invocation<EnableHooks> invocation(hooks);return basic_invoke(target,round,invocation);
}
template<bool Enabled,class Target> inline bool spell_invoke(const Target& target,unsigned round,Invocation<Enabled>& invocation) {
    if(invocation.stopped())return false;
    for (const auto& ability : cambc::self().abilities()) {
        if (!can_cast(round,ability) || !live(target)) continue;
        if (!permitted<Target>(ability) && !ability.can_target_ground()) continue;
        if (!(ability.is_damage() || ability.is_cc() || ability.is_aoe_damage())) continue;
        // Range, category, target life, owner availability remain distinct.
        if (!ability.valid_tiles().is_set(target.position()) || !epoch(round)) continue;
        if (ability.can_target_ground()) {
            const auto destination=target.position();
            return invocation.attempt(round,[&]{return Attempt{Primitive::Cast,round,-1,static_cast<int>(ability.slot()),destination};},
                [&]{return ability.use(destination);});
        }
        return invocation.attempt(round,[&]{return Attempt{Primitive::Cast,round,target_handle(target),static_cast<int>(ability.slot()),{-1,-1}};},
            [&]{return ability.use(target);},target_kind<Target>());
    }
    return false;
}
template<bool EnableHooks=true,class Target> inline bool spell(const Target& target,unsigned round,const Hooks* hooks=nullptr) {
    if constexpr(!EnableHooks)return ::wb::capital_micro::detail::spell(target,round);
    if(!active(hooks))return ::wb::capital_micro::detail::spell(target,round);
    Invocation<EnableHooks> invocation(hooks);return spell_invoke(target,round,invocation);
}
inline float shield_health() {
    float result = 0;
    const auto buffs = cambc::self().buffs();
    for (const auto& buff : buffs)
        if (const auto* shield = std::get_if<cambc::buff_shield>(&buff)) result += shield->value;
    return result;
}
template<bool Enabled,class Target> inline bool shield_for_work_invoke(const Target& target,Context c,unsigned round,Invocation<Enabled>& invocation) {
    if(invocation.stopped())return false;
    if (!(c.incoming_before_next > 0) || !live(target)) return false;
    const auto& me = cambc::self();
    const float exposure = c.incoming_before_next * std::max(1.0f,c.remaining_work_inputs);
    // Use the cast for a shield when estimated remaining work does not fit
    // current HP. No fixed health trigger, no future-safety inference.
    if (me.health()+shield_health() > exposure) return false;
    for (const auto& ability : me.abilities()) {
        if (!ability.is_shield() || !can_cast(round,ability) || !ability.can_target_self()) continue;
        return epoch(round) && invocation.attempt(round,
            [&]{return Attempt{Primitive::Cast,round,-1,static_cast<int>(ability.slot()),{-1,-1}};},[&]{return ability.use();});
    }
    return false;
}
template<bool EnableHooks=true,class Target> inline bool shield_for_work(const Target& target,Context c,unsigned round,const Hooks* hooks=nullptr) {
    if constexpr(!EnableHooks)return ::wb::capital_micro::detail::shield_for_work(target,c,round);
    if(!active(hooks))return ::wb::capital_micro::detail::shield_for_work(target,c,round);
    Invocation<EnableHooks> invocation(hooks);return shield_for_work_invoke(target,c,round,invocation);
}
template<bool Enabled,class Target> inline bool reset_invoke(const Target& target,unsigned round,Invocation<Enabled>& invocation) {
    if(invocation.stopped())return false;
    if (cambc::self().can_attack() || !live(target) || !basic_range(target)) return false;
    for (const auto& ability : cambc::self().abilities()) {
        if (!ability.is_attack_reset() || !can_cast(round,ability) || !ability.can_target_self()) continue;
        return epoch(round) && invocation.attempt(round,
            [&]{return Attempt{Primitive::Cast,round,-1,static_cast<int>(ability.slot()),{-1,-1}};},[&]{return ability.use();});
    }
    return false;
}
template<bool EnableHooks=true,class Target> inline bool reset(const Target& target,unsigned round,const Hooks* hooks=nullptr) {
    if constexpr(!EnableHooks)return ::wb::capital_micro::detail::reset(target,round);
    if(!active(hooks))return ::wb::capital_micro::detail::reset(target,round);
    Invocation<EnableHooks> invocation(hooks);return reset_invoke(target,round,invocation);
}
} // namespace detail

// Willow restores actual visible ally health. Her reset is self-only. This
// allocation compares useful healing with the immediate own extra-basic
// opportunity; it is a stamina heuristic, not an optimality theorem.
template<bool EnableHooks=true> inline Result support(Context context = {},const Hooks* hooks=nullptr) {
    if constexpr(!EnableHooks)return ::wb::capital_micro::support(context);
    if(!detail::active(hooks))return ::wb::capital_micro::support(context);
    Result result;
    detail::Invocation<EnableHooks> invocation(hooks);
    const auto round = cambc::round_number();
    if (!detail::epoch(round) || !detail::free_body()) return result;
    const auto self_handle = static_cast<cambc::handle_champion_t>(cambc::self());
    for (const auto& ability : cambc::self().abilities()) {
        if (!ability.is_heal() || !detail::can_cast(round,ability) || !ability.can_target_allies()) continue;
        const auto mask = ability.valid_tiles();
        std::optional<cambc::champion> selected;
        float useful = 0;
        const float raw_heal = ability.heal_amount();
        for (const auto& ally : cambc::vision::champions(cambc::team::ally)) {
            if (static_cast<cambc::handle_champion_t>(ally) == self_handle || !detail::live(ally)
                || !mask.is_set(ally.position())) continue;
            const float amount = std::min(raw_heal,ally.max_health()-ally.health());
            if (amount > useful) { useful = amount; selected = ally; }
        }
        if (!selected || useful <= cambc::self().strength()) continue;
        if (!detail::can_cast(round,ability) || !detail::live(*selected)
            || !ability.valid_tiles().is_set(selected->position()) || !detail::epoch(round)) return result;
        result.supported = invocation.attempt(round,
            [&]{return Attempt{Primitive::Cast,round,detail::target_handle(*selected),static_cast<int>(ability.slot()),{-1,-1}};},
            [&]{return ability.use(*selected);},detail::target_kind<cambc::champion>()); result.cast = result.supported;
        return result;
    }
    return result;
}

template<bool EnableHooks=true,class Target> inline Result engage(const Target& target,Context context = {},const Hooks* hooks=nullptr) {
    if constexpr(!EnableHooks)return ::wb::capital_micro::engage(target,context);
    if(!detail::active(hooks))return ::wb::capital_micro::engage(target,context);
    static_assert(std::is_same_v<Target,cambc::camp> || std::is_same_v<Target,cambc::champion>);
    Result result;
    detail::Invocation<EnableHooks> invocation(hooks);
    const auto round = cambc::round_number();
    if (!detail::epoch(round) || !detail::free_body() || !detail::live(target)) return result;
    // Exclusive damage/stun precedes any possible walk. Caller re-queries M/A/C.
    // Standing ranged casters can then basic without an ordinary walk.
    result.cast = detail::spell_invoke(target,round,invocation);
    if(invocation.stopped())return result;
    if (!result.cast) result.cast = detail::shield_for_work_invoke(target,context,round,invocation);
    if(invocation.stopped())return result;
    if (detail::basic_invoke(target,round,invocation)) ++result.basics;
    if(invocation.stopped())return result;
    // A Vendetta kill refreshes cooldowns, never the spent single-cast input.
    // Fresh ability readiness enforces that distinction.
    if (!result.cast && detail::reset_invoke(target,round,invocation)) {
        result.cast = true;
        if (detail::basic_invoke(target,round,invocation)) ++result.basics;
    }
    return result;
}

// Optional compatible dash. Actual native refusal remains a refusal, not
// observed motion. Parent retains route/pressure/capture authority.
template<bool EnableHooks=true> inline bool gap_dash(cambc::vec2 toward,const Hooks* hooks=nullptr) {
    if constexpr(!EnableHooks)return ::wb::capital_micro::gap_dash(toward);
    if(!detail::active(hooks))return ::wb::capital_micro::gap_dash(toward);
    detail::Invocation<EnableHooks> invocation(hooks);
    const auto round = cambc::round_number();
    if (!detail::epoch(round) || !detail::free_body()) return false;
    auto& me = cambc::self();
    const auto origin = me.position();
    for (const auto& ability : me.abilities()) {
        if (!ability.is_dash() || !detail::can_cast(round,ability)) continue;
        const auto payload = ability.get<cambc::ability_dash>();
        if (payload.exclusive_with_movement || !ability.can_target_ground()) continue;
        auto blocked = cambc::map::walls() | cambc::map::altars()
            | cambc::map::spawn_region(cambc::team::enemy) | cambc::vision::champion_tiles()
            | cambc::vision::camp_tiles() | cambc::vision::teleport_tiles();
        blocked.set(origin,false);
        const auto reachable = blocked.reachable_tiles(origin,static_cast<std::uint16_t>(payload.range));
        const auto holes = cambc::map::holes();
        cambc::vec2 chosen = origin;
        for (const auto landing : reachable.positions()) {
            if (holes.is_set(landing)) continue;
            if (landing.chebyshev_distance(toward) < chosen.chebyshev_distance(toward)) chosen = landing;
        }
        if (chosen == origin || !detail::can_cast(round,ability) || !detail::epoch(round)) return false;
        return invocation.attempt(round,[&]{return Attempt{Primitive::Cast,round,-1,static_cast<int>(ability.slot()),chosen};},
            [&]{return ability.use(chosen);});
    }
    return false;
}
} // namespace wb::capital_micro::guarded
namespace wb::capital_micro {
using Primitive=guarded::Primitive;
using Attempt=guarded::Attempt;
using Hooks=guarded::Hooks;
}
