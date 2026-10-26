#pragma once
#include "profile.h"

namespace p6::public_modes {
using namespace organization;
enum class Mode { circulate, deliver, contest };
inline Mode prescribed(int round) {
    const int residue = round % 96;
    return residue < 64 ? Mode::circulate : residue < 80 ? Mode::deliver : Mode::contest;
}
struct State {
    Mode prescribed_mode = Mode::circulate, executed_mode = Mode::circulate;
    Phase stage = Phase::bootstrap;
    int group = -1, cycle = -1, intercept_turns = 0, execution_lag = 0;
    bool initialized = false;
};
inline int distance(Point a, Point b) { return std::abs(a.x - b.x) + std::abs(a.y - b.y); }
inline const Actor* route_enemy(const Input& in) {
    if(in.role_ranks_current && in.intercept_target>=0) {
        const auto* target=in.frame.visible(in.intercept_target);
        if(target && target->team==Team::enemy && target->alive)return target;
    }
    const Actor* selected = nullptr;
    for (int i = 0; i < in.frame.actor_count; ++i) {
        const auto& seen = in.frame.actors[i]; const auto& a = seen.value;
        if (!seen.current(in.frame.clock, seen.subject) || a.team != Team::enemy || !a.alive ||
            distance(a.position, in.bank_approach) > 6) continue;
        if (!selected || a.health < selected->health ||
            (a.health == selected->health && a.id < selected->id)) selected = &a;
    }
    return selected;
}
inline ScoreEvaluation independent_rank(const Site* site, int actor) {
    if (site) for (int i = 0; i < site->actor_count; ++i)
        if (site->actors[i] == actor) return site->independent[i];
    return {};
}
inline const Site* useful_site(const Input& in) {
    const auto* selected = in.site(in.current_group);
    if (!selected) selected = in.site(in.proposed_group);
    return selected;
}
inline bool delivery(const Input& in, const Site* site) {
    const auto& own = in.frame.own.actor;
    if (in.ordinal == 0) return own.bag > 0 && in.safe_now;
    if (in.ordinal != 1 || !site || !joint(in, site->group.id) ||
        !site->solo(in.frame, own.id)) return false;
    const auto* converter = in.ordinal_actor(0);
    return ready(converter) && converter->bag > 0 && converter->position == in.rally &&
           site->solo(in.frame, converter->id);
}
inline bool contest(const Input& in, const Site* site) {
    if (!site || !site->known || !site->solo(in.frame, in.frame.clock.self) ||
        !route_enemy(in)) return false;
    if (in.ordinal == 2) return true;
    if (in.ordinal != 1 || !joint(in, site->group.id)) return false;
    const auto* interceptor = in.ordinal_actor(2);
    return ready(interceptor) && distance(interceptor->position, in.bank_approach) <= 6 &&
        site->solo(in.frame, interceptor->id);
}
inline Mode local_mode(const Input& in) {
    const auto* site = useful_site(in);
    const auto* solo = in.site(in.independent_group);
    const auto own_rank = independent_rank(solo, in.frame.clock.self);
    const bool can_deliver = delivery(in, site), can_contest = contest(in, site);
    // Runtime role ranks describe the actual bank-only or joint rescue template.
    // A timed public score query is never a common-mode input. Legacy Site
    // ranks are retained only for explicit synthetic fixtures without the flag.
    ScoreEvaluation deliver_rank, contest_rank;
    if (in.role_ranks_current) {
        if (can_deliver) deliver_rank = in.delivery_rank;
        if (can_contest) contest_rank = in.contest_rank;
    } else {
        if (can_deliver) deliver_rank = in.ordinal == 0 ?
            independent_rank(site ? site : solo, in.frame.clock.self) : site->joint_rank;
        if (can_contest && site && joint(in, site->group.id)) contest_rank = site->joint_rank;
    }
    if (can_deliver && deliver_rank.eligible() && compare(deliver_rank, own_rank) >= 0 &&
        (!can_contest || !contest_rank.eligible() || compare(deliver_rank, contest_rank) >= 0))
        return Mode::deliver; // Comparator ties: DELIVER, CONTEST, CIRCULATE.
    if (can_contest && contest_rank.eligible() && compare(contest_rank, own_rank) >= 0)
        return Mode::contest;
    return Mode::circulate;
}
inline Decision choose(State& state, const Input& in, bool control) {
    const auto& own = in.frame.own.actor;
    if (!in.frame.valid()) return independent(in);
    state.prescribed_mode = prescribed(in.frame.clock.round);
    if (own.channel != Channel::none && own.channel_valid) return independent(in);
    if (!in.safe_now) return {Intent::recover, -1, -1, {}, false, "public-local-survival"};
    // Only this actor's fresh legal altar context may trigger a direct win.
    if (in.at_bank && own.bag > 0 && in.settings.win_gems > 0 &&
        in.frame.own_team_score >= 0 &&
        own.bag >= std::max<Gems>(0, Gems(in.settings.win_gems) - in.frame.own_team_score))
        return {Intent::bank, in.current_group, -1, {}, false, "public-local-winning-bank"};
    if (state.cycle != in.frame.clock.round / 96) {
        state.cycle = in.frame.clock.round / 96; state.intercept_turns = 0;
    }
    Mode next = control ? local_mode(in) : state.prescribed_mode;
    const bool active_stage = in.current_group >= 0 && in.phase != Phase::bootstrap &&
        in.phase != Phase::assemble && !in.actual_payout && !in.actual_bank && !in.restored;
    if (state.initialized && next != state.executed_mode && active_stage &&
        state.stage == in.phase && state.group == in.current_group) {
        next = state.executed_mode; ++state.execution_lag;
    }
    state.initialized = true; state.executed_mode = next;
    state.stage = in.phase; state.group = in.current_group;
    if (in.phase == Phase::hold && own.spawn_group == in.current_group &&
        in.current_group >= 0 && !in.actual_payout)
        return work(in.current_group, "public-finish-safe-capture-stage");
    if (in.ordinal >= 3 || in.ordinal < 0 || next == Mode::circulate) return independent(in);
    const auto* site = useful_site(in);
    if (next == Mode::deliver && delivery(in, site)) {
        if (in.ordinal == 0)
            return {Intent::bank, in.current_group, -1, {}, false, "public-deliver-converter"};
        const auto* converter = in.ordinal_actor(0);
        return {Intent::escort, site->group.id, converter->id, in.bank_approach, true,
                "public-deliver-visible-rendezvous-escort"};
    }
    if (next == Mode::contest && contest(in, site) && state.intercept_turns < 8) {
        ++state.intercept_turns;
        const auto* enemy = route_enemy(in);
        return {Intent::intercept, site->group.id, enemy->id, enemy->position, true,
                in.ordinal == 2 ? "public-bounded-visible-intercept" : "public-optional-visible-support"};
    }
    return independent(in); // Missing local prerequisites never cause a wait.
}
struct Policy {
    State state;
    static const organization::Profile& profile() { return profile_value; }
    organization::Decision choose(const organization::Input& in, bool control) {
        return public_modes::choose(state, in, control);
    }
};
} // namespace p6::public_modes
