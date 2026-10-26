#pragma once
#include "organization.h"
#include <cstdlib>
#include <memory>
#include <type_traits>

// Conditional numeric continuations from one personal view. These schedules
// grant no action permission, future arrival, enemy immobility or action receipt.
namespace p6::runtime_joint {
// The runtime usually inspects only a few sites/actors. Retain allocated slots
// for reuse, but make every unread/current-missing slot an immutable empty value.
// Clearing a turn changes only validity bits, never a large forecast payload.
template<class T> class LazyGrid {
    struct Entry { std::unique_ptr<T> value; const T* borrowed=nullptr; bool current=false; };
    static const T& empty() {
        static const T value=[] {
            T result;
            if constexpr(std::is_same_v<T,forecast::Result>)result.status=Status::invalid;
            return result;
        }();
        return value;
    }
public:
    struct Row {
        std::array<Entry,max_actors> entries{};
        T& operator[](std::size_t actor) {
            auto& entry=entries[actor];
            if(!entry.value)entry.value=std::unique_ptr<T>(new T);
            entry.borrowed=nullptr;entry.current=true;return *entry.value;
        }
        const T& operator[](std::size_t actor) const {
            const auto& entry=entries[actor];
            return entry.current?(entry.borrowed?*entry.borrowed:*entry.value):empty();
        }
    };
    std::array<Row,8> rows{};
    Row& operator[](std::size_t site) { return rows[site]; }
    const Row& operator[](std::size_t site) const { return rows[site]; }
    bool current(std::size_t site,std::size_t actor) const { return rows[site].entries[actor].current; }
    bool storage_ready(std::size_t site,std::size_t actor) const { return static_cast<bool>(rows[site].entries[actor].value); }
    void prepare(std::size_t site,std::size_t actor) {
        auto& entry=rows[site].entries[actor];
        if(!entry.value)entry.value=std::unique_ptr<T>(new T);
    }
    void borrow(std::size_t site,std::size_t actor,const T& value) {
        auto& entry=rows[site].entries[actor];entry.borrowed=&value;entry.current=true;
    }
    void clear_current() {
        for(auto& row:rows)for(auto& entry:row.entries)entry.current=false;
    }
};
using Models = LazyGrid<forecast::Result>;
using Requests = LazyGrid<forecast::Request>;
inline int slot(const organization::Input& in, int id) {
    if (id == in.frame.clock.self) return 0;
    int index = 1;
    for (int i = 0; i < in.frame.actor_count; ++i) {
        const auto& seen = in.frame.actors[i];
        if (!seen.current(in.frame.clock, seen.subject) || seen.value.team != Team::ally) continue;
        if (seen.subject == id) return index < max_actors ? index : -1;
        ++index;
    }
    return -1;
}
inline Frame subject_frame(const organization::Input& in, const Actor& actor, int removed = -1,
                           double charged = 0) {
    Frame f; f.clock = in.frame.clock; f.clock.self = actor.id;
    f.own = {actor, actor.id == in.frame.clock.self ? in.frame.own.gold : 0, false, false};
    f.own.actor.health -= charged; f.own_team_score = in.frame.own_team_score;
    auto add = [&](const Actor& a) {
        if (a.id != actor.id && a.id != removed && f.actor_count < max_actors)
            f.actors[f.actor_count++] = {a, f.clock, a.id, Knowledge::exact};
    };
    add(in.frame.own.actor);
    for (int i = 0; i < in.frame.actor_count; ++i)
        if (in.frame.actors[i].current(in.frame.clock, in.frame.actors[i].subject)) add(in.frame.actors[i].value);
    return f;
}
inline bool fresh_request(const organization::Input& in, const Actor& actor,
                          const forecast::Request& request, int group) {
    ViewClock clock = in.frame.clock; clock.self = actor.id;
    return request.kit.current(clock, actor.id) && request.ambient_incoming.current(clock, actor.id) &&
        request.shape == forecast::Shape::produce_bank && request.project_group == group && request.group_count == 1 &&
        request.groups[0].observed.current(clock, group) &&
        request.groups[0].observed.value.id == group && request.leg_count == 4 &&
        request.legs[0].from == actor.position && request.timing.settings.source == Source::queried_setting &&
        request.timing.settings.value.max_rounds == in.settings.max_rounds &&
        request.timing.settings.value.win_gems == in.settings.win_gems &&
        request.timing.settings.value.capture_rounds == in.settings.capture_rounds;
}
inline bool fresh_result(const organization::Input& in, const Actor& actor,
                         const forecast::Result& result, int group) {
    if (result.status != Status::feasible || !result.certificate.solo ||
        !result.certificate.valid(in.frame, actor.id, group) || result.schedule.actor_count != 1) return false;
    const auto& a = result.schedule.actors[0];
    return a.actor_id == actor.id && a.initial_bag.value == actor.bag &&
        a.initial_bag.source == (actor.id == in.frame.clock.self ? Source::self_observed : Source::visible_subject) &&
        a.initial_gold.value == (actor.id == in.frame.clock.self ? in.frame.own.gold : 0) &&
        a.initial_gold.source == (actor.id == in.frame.clock.self ? Source::self_observed : Source::conservative);
}
inline bool append(Schedule& to, const Schedule& from, int delay, int prefix,
                   const organization::Input& in) {
    if (from.actor_count != 1 || to.actor_count == max_actors || delay < 0 ||
        to.production_count + from.production_count > max_productions) return false;
    auto a = from.actors[0]; const auto* current = in.frame.visible(a.actor_id);
    if (!current) return false;
    a.initial_bag = {current->bag, a.actor_id == in.frame.clock.self ? Source::self_observed : Source::visible_subject};
    a.initial_gold = {a.actor_id == in.frame.clock.self ? in.frame.own.gold : 0,
        a.actor_id == in.frame.clock.self ? Source::self_observed : Source::conservative};
    const int offset = to.production_count;
    a.occupied_turns.value += delay;
    for (int e = 0; e < a.event_count; ++e) {
        a.events[e].tick += delay; a.events[e].event_id += prefix;
        if (a.events[e].production_index >= 0) a.events[e].production_index += offset;
    }
    for (int p = 0; p < from.production_count; ++p) {
        auto production = from.productions[p];
        if (production.holder_count != 1 || production.holders[0] != current->id) return false;
        production.tick += delay; production.event_id += prefix;
        to.productions[to.production_count++] = production;
    }
    to.actors[to.actor_count++] = a; return true;
}
inline bool cumulative_safe(const Actor& actor, const CycleCertificate& cycle, double extra) {
    if (!std::isfinite(extra) || extra < 0) return false;
    for (auto exposure : {cycle.clear, cycle.hold, cycle.bank, cycle.recovery}) {
        exposure.health = actor.health; exposure.max_health = actor.max_health;
        exposure.incoming_total += extra;
        // A shield scheduled at the start of the independent route cannot be
        // moved later merely because the actor first helped somebody else.
        exposure.own_timed_shield = exposure.assigned_timed_heal = 0;
        if (!survives(exposure)) return false;
    }
    return true;
}
inline double camp_stress(const forecast::Request& request) {
    double total = 0;
    if (request.group_count != 1 || request.groups[0].member_count < 1 || request.groups[0].member_count > 32) return -1;
    const auto& g = request.groups[0];
    for (int m = 0; m < g.member_count; ++m) {
        const auto& member = g.members[m];
        if (!member.current.current(g.observed.clock, member.public_full.value.id)) return -1;
        const auto& value = member.current.value;
        if (!std::isfinite(value.health) || !std::isfinite(value.effective_strength) ||
            value.health < 0 || value.effective_strength < 0) return -1;
        if (value.health > 0) total += value.effective_strength;
    }
    return std::isfinite(total) ? total : -1;
}
inline double ambient_stress(const Frame& f, const forecast::Request& request) {
    if (!request.ambient_incoming.current(f.clock, f.clock.self) ||
        !std::isfinite(request.ambient_incoming.value) || request.ambient_incoming.value < 0 ||
        request.enemy_cast_count < 0 || request.enemy_cast_count > max_actors) return -1;
    double total = request.ambient_incoming.value;
    for (int i = 0; i < f.actor_count; ++i) {
        const auto& enemy = f.actors[i];
        if (!enemy.current(f.clock, enemy.subject) || enemy.value.team != Team::enemy || !enemy.value.alive) continue;
        int match = -1;
        for (int k = 0; k < request.enemy_cast_count; ++k)
            if (request.enemy_cast_stress[k].current(f.clock, enemy.subject)) {
                if (match >= 0) return -1;
                match = k;
            }
        if (match < 0 || !std::isfinite(request.enemy_cast_stress[match].value) ||
            request.enemy_cast_stress[match].value < 0) return -1;
        total += enemy.value.strength + request.enemy_cast_stress[match].value;
    }
    return std::isfinite(total) ? total : -1;
}
inline const Actor* producing_owner(const organization::Input& in, const organization::Site& site,
                                    std::string_view slug) {
    if (slug == "owner-builder") return in.ordinal_actor(site.group.id % 3);
    const Actor* owner = nullptr;
    auto consider = [&](const Actor* a) {
        if (!organization::useful(a, site.group.id)) return;
        if (slug == "firebrigade" && a->pick_ordinal > 1) return;
        if (!owner || a->pick_ordinal < owner->pick_ordinal ||
            (a->pick_ordinal == owner->pick_ordinal && a->id < owner->id)) owner = a;
    };
    consider(&in.frame.own.actor);
    for (int i = 0; i < in.frame.actor_count; ++i) consider(in.frame.visible(in.frame.actors[i].subject));
    return owner;
}
template<class Continue>
bool speedup(organization::Input& in, int index, const Models& models,
             const Requests& requests, std::string_view slug, Continue& may_continue) {
    if (!may_continue()) return false;
    // A speedup needs both its owner and a distinct helper's current solo
    // model. A single cached actor cannot admit this comparison, so avoid the
    // more expensive visible-actor search in that common runtime case.
    int current_models=0;
    for(int actor=0;actor<max_actors;++actor)if(models.current(index,actor))++current_models;
    if(current_models<2)return false;
    auto& site = in.sites[index]; const auto* owner = producing_owner(in, site, slug);
    // Launchpad's real ready-crew barrier must be able to admit the first attack.
    if (!owner || !site.known || site.blocked || (!site.damaged_now && slug != "launchpad") ||
        !site.group.all_spawns_visible || owner->ready_attack_group != site.group.id) return false;
    const int os = slot(in, owner->id); if (os < 0) return false;
    const auto& baseline = models[index][os]; const auto& request = requests[index][os];
    if (!fresh_request(in, *owner, request, site.group.id) ||
        !fresh_result(in, *owner, baseline, site.group.id) || request.legs[0].distance.value != 0) return false;
    int old_clear = -1;
    for (int p = 0; p < baseline.phase_count; ++p) {
        const auto& phase = baseline.phases[p];
        if (phase.kind == forecast::PhaseKind::clear && phase.group == site.group.id && phase.start == 0)
            old_clear = phase.finish;
    }
    if (old_clear <= 0) return false;
    std::array<MonsterWork, 32> targets{}; const double camp = camp_stress(request);
    if (camp < 0) return false;
    for (int m = 0; m < request.groups[0].member_count; ++m) targets[m] = request.groups[0].members[m].current.value;
    for (int hs = 0; hs < max_actors; ++hs) {
        if (!may_continue()) return false;
        if(!models.current(index,hs))continue;
        const auto& source_cycle = models[index][hs];
        const auto* helper = in.frame.visible(source_cycle.certificate.actor);
        if (!helper || helper->id == owner->id || helper->ready_attack_group != site.group.id ||
            !organization::useful(helper, site.group.id) ||
            !fresh_result(in, *helper, source_cycle, site.group.id)) continue;
        std::array<Productivity, 2> participants{Productivity{owner->strength,0,owner->ranged},
            Productivity{helper->strength,0,helper->ranged}};
        const auto work = combat_work({targets.data(), static_cast<std::size_t>(request.groups[0].member_count)}, participants);
        if (!work.resolved || work.rounds >= old_clear) continue;
        const int holder_distance = request.legs[1].distance.value;
        if (!request.legs[1].distance.known() || holder_distance < 0 || holder_distance > 256) continue;
        // The helper cannot start its other circuit at the instant the final
        // monster dies: allow the actual holder's spawn walk and a fresh local
        // hold witness before its organizational release.
        const int release_delay = work.rounds + (holder_distance ? holder_distance + 2 : 0) + 1;
        const auto helper_frame = subject_frame(in, *helper);
        int best_other = -1; ScoreEvaluation best_rank;
        for (int other = 0; other < in.site_count; ++other) {
            if (!may_continue()) return false;
            if (other == index || !in.sites[other].known || in.sites[other].blocked ||
                (slug == "owner-builder" && helper->pick_ordinal >= 0 && helper->pick_ordinal < 3 &&
                 in.sites[other].group.id % 3 != helper->pick_ordinal) ||
                !fresh_request(in,*helper,requests[other][hs],in.sites[other].group.id) ||
                !fresh_result(in,*helper,models[other][hs],in.sites[other].group.id)) continue;
            const auto rank = evaluate_schedule(models[other][hs].schedule,in.windows,in.settings,
                {in.frame.clock.round,in.frame.clock.self},in.frame.own_team_score);
            if (rank.eligible() && (best_other < 0 || compare(rank,best_rank) > 0)) {
                best_other = other; best_rank = rank;
            }
        }
        for (int other = 0; other < in.site_count; ++other) {
            if (!may_continue()) return false;
            if (other != best_other) continue;
            const auto& independent = models[other][hs]; const auto& continuation = requests[other][hs];
            if (!fresh_request(in, *helper, continuation, in.sites[other].group.id) ||
                !fresh_result(in, *helper, independent, in.sites[other].group.id)) continue;
            const double ambient = ambient_stress(helper_frame, continuation);
            const double initial_ambient = ambient_stress(helper_frame, requests[index][hs]);
            if (ambient < 0 || initial_ambient < 0 ||
                !cumulative_safe(*helper, independent.certificate,
                    work.rounds * camp + release_delay * std::max(ambient, initial_ambient))) continue;
            Schedule accelerated = baseline.schedule;
            const int saved = old_clear - work.rounds;
            for (int p = 0; p < accelerated.production_count; ++p) accelerated.productions[p].tick -= saved;
            for (int e = 0; e < accelerated.actors[0].event_count; ++e) accelerated.actors[0].events[e].tick -= saved;
            accelerated.actors[0].occupied_turns.value -= saved;
            Schedule cooperative, separate; cooperative.target_id = separate.target_id = site.group.id;
            if (!append(cooperative, accelerated, 0, 0, in) ||
                !append(cooperative, independent.schedule, release_delay, 64, in) ||
                !append(separate, baseline.schedule, 0, 0, in) ||
                !append(separate, independent.schedule, 0, 64, in)) continue;
            const auto comparison = evaluate_alternatives(cooperative, separate, in.windows, in.settings,
                {in.frame.clock.round,in.frame.clock.self}, in.frame.own_team_score);
            if (!joint_support_admissible(comparison)) continue;
            if (!may_continue()) return false;
            site.joint_better = true; site.joint_rank = comparison.cooperative; site.holder = owner->id;
            return true;
        }
    }
    return false;
}
inline long long distance(Point a, Point b) {
    return std::max(std::llabs(static_cast<long long>(a.x) - b.x),
                    std::llabs(static_cast<long long>(a.y) - b.y));
}
inline bool threatened(const organization::Site& site, const Actor& owner, const Actor& enemy) {
    const bool reach = enemy.attack_range >= 0 && distance(owner.position, enemy.position) <= enemy.attack_range;
    const bool hold = site.group.lifecycle == Lifecycle::reward_available &&
        owner.spawn_group == site.group.id && owner.action == WitnessAction::occupied_spawn;
    return (reach && (hold || owner.bag > 0)) ||
        (owner.channel == Channel::none && owner.position != site.group.approach &&
         distance(owner.position, site.group.approach) <= 1 && enemy.position == site.group.approach);
}
inline void remove_enemy(forecast::Request& request, int enemy) {
    int count = 0;
    for (int i = 0; i < request.enemy_cast_count; ++i)
        if (request.enemy_cast_stress[i].subject != enemy)
            request.enemy_cast_stress[count++] = request.enemy_cast_stress[i];
    request.enemy_cast_count = count;
    request.shield.enabled = false;
}
inline bool bank_request(const forecast::Request& original, forecast::Request& bank, double camp) {
    if (original.leg_count != 4 || camp < 0) return false;
    bank = original; bank.shape = forecast::Shape::bank_only; bank.group_count = 0;
    int distance_to_bank = 0;
    for (int i = 0; i < 3; ++i) {
        if (!original.legs[i].distance.known() || original.legs[i].distance.value < 0 ||
            original.legs[i].distance.value > 256) return false;
        distance_to_bank += original.legs[i].distance.value;
    }
    bank.leg_count = 2;
    bank.legs[0] = {original.legs[0].from,original.bank_position,{distance_to_bank,Source::public_map}};
    bank.legs[1] = original.legs[3];
    bank.ambient_incoming.value += camp;
    bank.shield.enabled = false;
    return std::isfinite(bank.ambient_incoming.value);
}
inline CycleCertificate lift(CycleCertificate value, const organization::Input& in,
                             const Actor& actor, int group, double charged) {
    value.clock = in.frame.clock; value.group = group; value.solo = false;
    for (auto* e : {&value.clear,&value.hold,&value.bank,&value.recovery}) {
        e->health = actor.health; e->max_health = actor.max_health;
        e->incoming_total += charged;
        e->own_timed_shield = e->assigned_timed_heal = 0;
    }
    return value;
}
inline void install(organization::Site& site, const CycleCertificate& certificate) {
    for (int i = 0; i < site.actor_count; ++i) if (site.actors[i] == certificate.actor) {
        // Keep a stronger independent certificate if this actor already has one.
        if (!site.cycles[i].solo) site.cycles[i] = certificate;
        return;
    }
    if (site.actor_count < max_actors) {
        const int i = site.actor_count++; site.actors[i] = certificate.actor; site.cycles[i] = certificate;
        site.independent[i] = {}; // A joint-only append has no current solo rank.
    }
}
template<class Continue>
void support_wait(organization::Input& in, int index, const Models& models,
                  const Requests& requests, Continue& may_continue) {
    auto& site = in.sites[index]; const auto& reserve = in.frame.own.actor;
    const auto* owner = producing_owner(in,site,"firebrigade");
    if (!may_continue() || reserve.pick_ordinal != 2 || !site.known || site.blocked ||
        site.group.lifecycle != Lifecycle::reward_available || !site.group.uncontested_now ||
        !owner || owner->spawn_group != site.group.id || !site.solo(in.frame,owner->id)) return;
    // After the enemy actually dies, no new alarm or saved-score claim exists.
    // An already admitted reserve still needs a fresh proof for its remaining
    // wait and named solo continuation until personally witnessing payout.
    int nearest = -1;
    for (int other = 0; other < in.site_count; ++other) {
        if (!may_continue()) return;
        if (other == index || !in.sites[other].known || in.sites[other].blocked ||
            !fresh_request(in,reserve,requests[other][0],in.sites[other].group.id) ||
            !fresh_result(in,reserve,models[other][0],in.sites[other].group.id)) continue;
        if (nearest < 0 || distance(reserve.position,in.sites[other].group.approach) <
            distance(reserve.position,in.sites[nearest].group.approach) ||
            (distance(reserve.position,in.sites[other].group.approach) ==
             distance(reserve.position,in.sites[nearest].group.approach) &&
             in.sites[other].group.id < in.sites[nearest].group.id)) nearest = other;
    }
    if (nearest < 0) return;
    const double ambient = ambient_stress(in.frame,requests[nearest][0]);
    const double extra = (in.settings.capture_rounds + 1.) * ambient;
    if (ambient < 0 || !cumulative_safe(reserve,models[nearest][0].certificate,extra)) return;
    const auto certificate = lift(models[nearest][0].certificate,in,reserve,site.group.id,extra);
    if (!certificate.valid(in.frame,reserve.id,site.group.id) || !may_continue()) return;
    install(site,certificate);
}
struct RescueWorkspace {
    forecast::Request bank{}, owner{}, reserve{};
    forecast::Result independent_owner{}, saved_owner{}, continued_reserve{};
    Schedule cooperative{}, separate{};
    // Keep member defaults without a second whole-object zero-fill in make_unique.
    RescueWorkspace() {}
};
template<class Continue>
bool rescue(organization::Input& in, int index, const Models& models, const Requests& requests,
            Continue& may_continue) {
    if (!may_continue()) return false;
    auto& site = in.sites[index]; const auto& responder = in.frame.own.actor;
    const auto* enemy = in.frame.visible(in.legal_enemy_target);
    if (!site.known || site.blocked || responder.pick_ordinal != 2 || !organization::ready(&responder) ||
        !responder.can_attack || responder.strength <= 0 || !enemy || enemy->team != Team::enemy ||
        !enemy->alive || enemy->health <= 0 || enemy->strength <= 0 || responder.attack_range < 0 ||
        distance(responder.position,enemy->position) > responder.attack_range ||
        in.enemy_stress_count < 0 || in.enemy_stress_count > max_actors) return false;
    const double turns_value = std::ceil(enemy->health / responder.strength);
    if (!std::isfinite(turns_value) || turns_value < 1 || turns_value > 8) return false;
    const int turns = static_cast<int>(turns_value);
    double all_enemy_stress = 0; int visible_enemies = 0;
    for (int i = 0; i < in.frame.actor_count; ++i) {
        const auto& seen = in.frame.actors[i];
        if (!seen.current(in.frame.clock,seen.subject) || seen.value.team != Team::enemy || !seen.value.alive) continue;
        int match = -1;
        for (int k = 0; k < in.enemy_stress_count; ++k)
            if (in.enemy_stress[k].current(in.frame.clock,seen.subject)) {
                if (match >= 0) return false;
                match = k;
            }
        if (match < 0 || !std::isfinite(in.enemy_stress[match].value) ||
            in.enemy_stress[match].value < seen.value.strength) return false;
        all_enemy_stress += in.enemy_stress[match].value; ++visible_enemies;
    }
    if (visible_enemies != in.enemy_stress_count) return false;
    // The reserve's displaced continuation is its actual nearest complete solo
    // source, rather than whichever unrelated source makes a rescue look cheap.
    int local = -1;
    for (int i = 0; i < in.site_count; ++i) {
        if (!in.sites[i].known || in.sites[i].blocked ||
            !fresh_request(in,responder,requests[i][0],in.sites[i].group.id) ||
            !fresh_result(in,responder,models[i][0],in.sites[i].group.id)) continue;
        if (local < 0 || distance(responder.position,in.sites[i].group.approach) <
            distance(responder.position,in.sites[local].group.approach) ||
            (distance(responder.position,in.sites[i].group.approach) ==
             distance(responder.position,in.sites[local].group.approach) &&
             in.sites[i].group.id < in.sites[local].group.id)) local = i;
    }
    if (local < 0 || local == index) return false;
    auto work = std::make_unique<RescueWorkspace>();
    for (int pick = 0; pick < 2; ++pick) {
        if (!may_continue()) return false;
        const auto* owner = in.ordinal_actor(pick);
        if (!organization::ready(owner) || owner->bag <= 0 || !threatened(site,*owner,*enemy)) continue;
        const int os = slot(in,owner->id); if (os < 0) continue;
        const auto& original = requests[index][os];
        if (!fresh_request(in,*owner,original,site.group.id)) continue;
        const double camp = camp_stress(original);
        const auto owner_frame = subject_frame(in,*owner);
        const double owner_ambient = ambient_stress(owner_frame,original);
        const double reserve_ambient = ambient_stress(in.frame,requests[local][0]);
        if (camp < 0 || owner_ambient < 0 || reserve_ambient < 0 ||
            !bank_request(original,work->bank,camp)) continue;
        if (!may_continue()) return false;
        forecast::build_into(owner_frame,work->bank,work->independent_owner);
        if (work->independent_owner.status != Status::feasible) continue;
        auto best_owner = evaluate_schedule(work->independent_owner.schedule,in.windows,in.settings,
            {in.frame.clock.round,in.frame.clock.self},in.frame.own_team_score);
        // Banking is a real fallback, not a deliberately weak incumbent. If the
        // owner already has a better safe source continuation, charge that too.
        for (int other = 0; other < in.site_count; ++other) {
            if (!may_continue()) return false;
            if (other == local || !in.sites[other].known || in.sites[other].blocked ||
                !fresh_request(in,*owner,requests[other][os],in.sites[other].group.id) ||
                !fresh_result(in,*owner,models[other][os],in.sites[other].group.id)) continue;
            const auto alternative = evaluate_schedule(models[other][os].schedule,in.windows,in.settings,
                {in.frame.clock.round,in.frame.clock.self},in.frame.own_team_score);
            if (compare(alternative,best_owner) > 0) {
                best_owner = alternative; work->independent_owner = models[other][os];
            }
        }
        // Charge the full currently observed enemy/camp bound on *both* actors
        // throughout the basic-only kill, without any CC, shields or healing.
        const double rate = camp + std::max({owner_ambient,reserve_ambient,all_enemy_stress});
        const double charged = turns * rate;
        Exposure waiting; waiting.health = owner->health; waiting.max_health = owner->max_health;
        waiting.incoming_total = charged; waiting.next_phase_incoming = rate; waiting.observations_complete = true;
        if (!survives(waiting)) continue;
        waiting.health = responder.health; waiting.max_health = responder.max_health;
        if (!survives(waiting)) continue;
        auto saved_frame = subject_frame(in,*owner,enemy->id,charged);
        auto continued_frame = subject_frame(in,responder,enemy->id,charged);
        work->owner = original; work->reserve = requests[local][0];
        // A threatened loaded return ends in its actual bank; an exposed hold
        // or blocked approach may preserve the real source as well as inventory.
        const bool productive = owner->spawn_group == site.group.id ||
            (enemy->position == site.group.approach && distance(owner->position,site.group.approach) <= 1);
        if (!productive && !bank_request(original,work->owner,camp)) continue;
        remove_enemy(work->owner,enemy->id); remove_enemy(work->reserve,enemy->id);
        if (!may_continue()) return false;
        forecast::build_into(saved_frame,work->owner,work->saved_owner);
        if (work->saved_owner.status != Status::feasible) continue;
        int terminal = -1;
        const auto& saved_plan = work->saved_owner.schedule.actors[0];
        for (int e = 0; e < saved_plan.event_count; ++e) {
            const auto& event = saved_plan.events[e];
            const bool endpoint = productive ? event.kind == EventKind::production : event.kind == EventKind::tribute;
            if (endpoint && event.tick + turns <= 8 && (terminal < 0 || event.tick + turns < terminal)) terminal = event.tick + turns;
        }
        if (terminal < 0) continue;
        const double remaining_owner = ambient_stress(saved_frame,work->owner);
        const double remaining_reserve = ambient_stress(continued_frame,work->reserve);
        if (remaining_owner < 0 || remaining_reserve < 0) continue;
        const double reserve_charge = charged + (terminal - turns) *
            (camp + std::max(remaining_owner,remaining_reserve));
        waiting.health = responder.health; waiting.max_health = responder.max_health;
        waiting.incoming_total = reserve_charge;
        waiting.next_phase_incoming = camp + std::max(remaining_owner,remaining_reserve);
        if (!survives(waiting)) continue;
        continued_frame = subject_frame(in,responder,enemy->id,reserve_charge);
        if (!may_continue()) return false;
        forecast::build_into(continued_frame,work->reserve,work->continued_reserve);
        if (work->continued_reserve.status != Status::feasible) continue;
        const auto owner_certificate = lift(work->saved_owner.certificate,in,*owner,site.group.id,charged);
        const auto reserve_certificate = lift(work->continued_reserve.certificate,in,responder,site.group.id,reserve_charge);
        if (!owner_certificate.valid(in.frame,owner->id,site.group.id) ||
            !reserve_certificate.valid(in.frame,responder.id,site.group.id)) continue;
        work->cooperative = {}; work->separate = {};
        work->cooperative.target_id = work->separate.target_id = site.group.id;
        if (!append(work->cooperative,work->saved_owner.schedule,turns,0,in) ||
            !append(work->cooperative,work->continued_reserve.schedule,terminal,64,in) ||
            !append(work->separate,work->independent_owner.schedule,0,0,in) ||
            !append(work->separate,models[local][0].schedule,0,64,in)) continue;
        const auto comparison = evaluate_alternatives(work->cooperative,work->separate,in.windows,in.settings,
            {in.frame.clock.round,in.frame.clock.self},in.frame.own_team_score);
        if (!joint_support_admissible(comparison)) continue;
        if (!may_continue()) return false;
        site.joint_better = true; site.holder = owner->id; site.joint_rank = comparison.cooperative;
        install(site,owner_certificate); install(site,reserve_certificate); return true;
    }
    return false;
}
template<class Quotes, class Continue>
void populate(organization::Input& in, const Models& models, const Requests& requests,
              const Quotes& quotes, std::string_view slug, Continue may_continue) {
    (void)quotes;
    if (!in.frame.valid() || in.site_count < 0 || in.site_count > 8) return;
    int attempts = 0;
    for (int i = 0; i < in.site_count; ++i) {
        if (!may_continue()) return;
        bool current = false;
        for (int k = 0; k < max_actors; ++k) {
            if(!requests.current(i,k))continue;
            const auto* actor = in.frame.visible(requests[i][k].kit.subject);
            if (actor && actor->team == Team::ally && fresh_request(in,*actor,requests[i][k],in.sites[i].group.id)) current = true;
        }
        if (current && attempts++ == 3) return;
        in.sites[i].joint_better = false; in.sites[i].holder = -1;
        if (!current) continue;
        if (slug == "firebrigade" && in.frame.own.actor.pick_ordinal == 2) rescue(in,i,models,requests,may_continue);
        if (!may_continue()) return;
        if (!in.sites[i].joint_better) speedup(in,i,models,requests,slug,may_continue);
        if (slug == "firebrigade" && in.frame.own.actor.pick_ordinal == 2 && !in.sites[i].joint_better)
            support_wait(in,i,models,requests,may_continue);
    }
}
template<class Quotes>
void populate(organization::Input& in, const Models& models, const Requests& requests,
              const Quotes& quotes, std::string_view slug) {
    populate(in,models,requests,quotes,slug,[]{ return true; });
}
} // namespace p6::runtime_joint
