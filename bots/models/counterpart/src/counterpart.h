#pragma once
#include "../../common/pure.h"

namespace plan4::counterpart {
inline constexpr const char* version = "plan4-counterpart/1";
inline constexpr int max_targets = 8, max_camps = 32, max_turns = 6, max_corridor_tiles = 256;
struct PolicySeal { std::uint64_t identity = 0; bool inspectable = false, frozen = false, matches_rule = false; };
struct Target { int id = -1; Point position{}; unsigned bag = 0; bool visible = false; };
struct Camp { int id = -1; Point position{}; };
struct Geometry {
    int (*distance)(Point,Point,const void*) = nullptr;
    // 1=open, walkable, non-bush and non-occluding; 0=known other; -1=unknown.
    int (*open)(Point,const void*) = nullptr;
    const void* context = nullptr;
};
enum class Branch { idle, chase, camp, unsupported };
struct Decision { Branch branch = Branch::unsupported; int target_id = -1; Point target{}; };
// The actual legal fixture can call this same rule with its own current visible
// target list. It selects an intent; its executor must still check legal actions.
inline Decision fixture_decision(Point rival, const Target* targets, int count,
                                 const Camp* priority, int camp_count, int cursor, Geometry geometry) {
    Decision result;
    if (count < 0 || count > max_targets || (count && !targets) || camp_count < 0 || camp_count > max_camps
        || (camp_count && !priority) || cursor < 0 || !geometry.distance) return result;
    int chosen = -1, best = std::numeric_limits<int>::max();
    for (int n = 0; n < count; ++n) {
        if (!targets[n].visible || !targets[n].bag) continue;
        if (targets[n].id < 0) return result;
        const int d = geometry.distance(rival,targets[n].position,geometry.context);
        if (d < 0) return result; // an unknown possible nearest target cannot be skipped
        if (d < best || (d == best && (chosen < 0 || targets[n].id < targets[chosen].id))) { best = d; chosen = n; }
    }
    if (chosen >= 0) return {Branch::chase,targets[chosen].id,targets[chosen].position};
    if (!camp_count) return {Branch::idle,-1,rival};
    const auto& camp = priority[cursor % camp_count];
    if (camp.id < 0 || geometry.distance(rival,camp.position,geometry.context) < 0) return result;
    return {Branch::camp,camp.id,camp.position};
}
struct OwnFrame { Point position{}; unsigned bag = 0; };
struct OtherTarget {
    Target value{};
    Provenance provenance{};
    bool stationary_fixture_rule = false;
};
struct ResponseInput {
    int round = 0;
    Point rival{};
    Decision decision{};
    OwnFrame own{};
    float own_health = 0;
};
struct Response {
    Point rival_position{};
    float own_damage = 0;
    int own_delay = 0;
    bool recall_interrupted = false, supported = false;
};
struct Input {
    int round = -1, own_id = -1, rival_id = -1, turns = 0;
    OwnFrame before{};
    std::array<OwnFrame,max_turns> after{};
    float own_health = 0;
    Point rival{};
    Provenance rival_provenance{};
    bool rival_personally_visible = false;
    PolicySeal policy{};
    Geometry geometry{};
    Point corridor_min{}, corridor_max{}; // inclusive, conservatively all open
    int public_vision_radius = -1;
    bool vision_modifiers_known = false, complete_choice_inputs = false;
    bool order_known = false, opposing_teams = false;
    bool rival_before_own_current_round = false, rival_already_completed = false;
    std::array<OtherTarget,max_targets-1> other_targets{};
    int other_count = 0;
    std::array<Camp,max_camps> camp_priority{};
    int camp_count = 0, camp_cursor = 0;
    bool (*quote_response)(const ResponseInput&,Response&,const void*) = nullptr;
    const void* quote_context = nullptr;
    WorkBudget work_budget{};
};
enum class Unsupported { none, unknown_policy, stale_rival, observability, ordering, path, quote, budget };
struct Step { int round = -1; Decision decision{}; Point rival_before{}, rival_after{}; float own_damage = 0; Status status = Status::predicted; };
struct Result {
    std::array<Step,max_turns> steps{};
    int count = 0, own_delay = 0;
    float own_damage = 0, final_health = 0;
    bool supported = false, recall_interrupted = false, predicted_death = false;
    // The physical prefix may stop before this owner's proposed move if an
    // earlier rival action is lethal. Never advance that unexecuted body.
    int own_turns = 0;
    int death_round = -1;
    OwnFrame final_own{};
    Unsupported unsupported = Unsupported::none;
};
inline int chebyshev(Point a,Point b) { return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y)); }
inline bool in_corridor(const Input& i,Point p) {
    return p.x >= i.corridor_min.x && p.y >= i.corridor_min.y && p.x <= i.corridor_max.x && p.y <= i.corridor_max.y;
}
inline bool exact(const Provenance& p,int round) { return p.observed_round == round && p.status == Status::exact_now && p.source == Source::personal_vision; }
inline void predict_into(const Input& i,bool coupling_enabled,Result& out) {
    out = Result{}; out.final_health = i.own_health; out.final_own = i.before;
    auto fail = [&](Unsupported reason) { out.unsupported = reason; out.supported = false; };
    if (!i.policy.identity || !i.policy.inspectable || !i.policy.frozen || !i.policy.matches_rule) { fail(Unsupported::unknown_policy); return; }
    if (i.round < 0 || i.round > std::numeric_limits<int>::max()-max_turns || i.own_id < 0 || i.rival_id < 0 || i.own_id == i.rival_id
        || !i.rival_personally_visible || !exact(i.rival_provenance,i.round)) { fail(Unsupported::stale_rival); return; }
    if (!i.order_known || !i.opposing_teams || i.rival_before_own_current_round != i.rival_already_completed) { fail(Unsupported::ordering); return; }
    const auto width = std::int64_t(i.corridor_max.x)-i.corridor_min.x+1;
    const auto height = std::int64_t(i.corridor_max.y)-i.corridor_min.y+1;
    if (!i.geometry.open || !i.geometry.distance || !i.vision_modifiers_known || !i.complete_choice_inputs
        || i.public_vision_radius < 1 || i.corridor_min.x < 0 || i.corridor_min.y < 0
        || i.corridor_max.x == std::numeric_limits<int>::max() || i.corridor_max.y == std::numeric_limits<int>::max()
        || width <= 0 || height <= 0 || width > max_corridor_tiles || height > max_corridor_tiles || width*height > max_corridor_tiles
        || i.other_count < 0 || i.other_count >= max_targets || i.turns < 1 || i.turns > max_turns
        || !std::isfinite(i.own_health) || i.own_health <= 0) { fail(Unsupported::observability); return; }
    for (int y=i.corridor_min.y;y<=i.corridor_max.y;++y) for (int x=i.corridor_min.x;x<=i.corridor_max.x;++x) {
        if (i.work_budget.can_refine && !i.work_budget.can_refine(i.work_budget.context)) { fail(Unsupported::budget); return; }
        if (i.geometry.open({x,y},i.geometry.context) != 1) { fail(Unsupported::observability); return; }
    }
    if (!in_corridor(i,i.before.position) || !in_corridor(i,i.rival)
        || chebyshev(i.before.position,i.rival)>i.public_vision_radius) { fail(Unsupported::observability); return; }
    Point previous=i.before.position;
    for (int n=0;n<i.turns;++n) {
        if (!in_corridor(i,i.after[n].position) || chebyshev(previous,i.after[n].position)>1) { fail(Unsupported::path); return; }
        previous=i.after[n].position;
    }
    std::array<Target,max_targets> targets{};
    for (int n=0;n<i.other_count;++n) {
        const auto& other=i.other_targets[n];
        if (!other.stationary_fixture_rule || !other.value.visible || !exact(other.provenance,i.round)
            || other.value.id < 0 || other.value.id == i.own_id || other.value.id == i.rival_id
            || !in_corridor(i,other.value.position)) { fail(Unsupported::observability); return; }
        for (int p=0;p<n;++p) if (i.other_targets[p].value.id==other.value.id) { fail(Unsupported::observability); return; }
        targets[n+1]=other.value;
    }
    if (!i.quote_response) { fail(Unsupported::quote); return; }
    Point rival=i.rival; OwnFrame own=i.before;
    auto respond = [&](int round) {
        if (i.work_budget.can_refine && !i.work_budget.can_refine(i.work_budget.context)) { fail(Unsupported::budget); return false; }
        const auto seen=coupling_enabled?own:i.before;
        if (!in_corridor(i,rival) || chebyshev(rival,seen.position)>i.public_vision_radius
            || chebyshev(rival,own.position)>i.public_vision_radius) { fail(Unsupported::observability); return false; }
        targets[0]={i.own_id,seen.position,seen.bag,true};
        for (int n=1;n<=i.other_count;++n) targets[n].visible=chebyshev(rival,targets[n].position)<=i.public_vision_radius;
        const auto decision=fixture_decision(rival,targets.data(),i.other_count+1,
            i.camp_priority.data(),i.camp_count,i.camp_cursor,i.geometry);
        if (decision.branch==Branch::unsupported) { fail(Unsupported::path); return false; }
        Response response;
        // Only the policy input is frozen in control. Shared physical occupancy,
        // damage and interruption quotes still use the actual candidate body;
        // the estimator must execute this intent, never retarget it.
        if (!i.quote_response({round,rival,decision,own,out.final_health},response,i.quote_context) || !response.supported
            || !in_corridor(i,response.rival_position) || chebyshev(rival,response.rival_position)>1
            || response.rival_position==own.position || !std::isfinite(response.own_damage)
            || response.own_damage<0 || response.own_damage>out.final_health
            || response.own_delay<0 || response.own_delay>max_turns) { fail(Unsupported::quote); return false; }
        auto& step=out.steps[out.count++]; step={round,decision,rival,response.rival_position,response.own_damage,Status::predicted};
        rival=response.rival_position; out.own_damage+=response.own_damage; out.own_delay+=response.own_delay;
        out.recall_interrupted|=response.recall_interrupted;
        out.final_health=std::max(0.0f,i.own_health-out.own_damage); out.predicted_death=out.final_health<=0;
        if(out.predicted_death) {out.death_round=round;out.final_own.bag=0;}
        return true;
    };
    for (int turn=0;turn<i.turns && !out.predicted_death;++turn) {
        const bool before=turn? (i.rival_before_own_current_round != bool(turn%2)) : i.rival_before_own_current_round;
        if (turn && before && !respond(i.round+turn)) return;
        if (out.predicted_death) break;
        if (i.after[turn].position==rival) { fail(Unsupported::path); return; }
        own=i.after[turn];
        ++out.own_turns; out.final_own=own;
        if (!before && !respond(i.round+turn)) return;
    }
    out.supported=true;
}
} // namespace plan4::counterpart
