#pragma once
#include "../../common/pure.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>

namespace plan4::quotient {
inline constexpr int member_limit = 32, class_limit = 8, key_limit = 384;

struct Member {
    int kind = -1;
    Point spawn{};
    float public_maximum_health = 0, health = 0;
    Provenance health_provenance{};
};
struct Composition {
    const Member* members = nullptr;
    int count = 0;
    bool complete_public_layout = false;
};
// Extra combat inputs absent from Information. These are current legal values,
// not a hash standing in for equality. M0's other actor fields are keyed too.
struct Kit {
    int champion_kind = -1, attack_range = 0;
    float strength = 0, focus = 0;
    unsigned monster_shards = 0, jungle_shards = 0;
    std::uint64_t version = 0;
};
struct View {
    const Composition* groups = nullptr; // Same array order as Information.
    int count = 0;
    Kit kit{};
};

// An exact, length-delimited feature tuple, not a lossy hash. Only its written
// prefix is live. Physical group/member IDs and absolute camp coordinates are
// excluded; repeated kinds use public spawn-coordinate order below.
struct FeatureKey {
    std::array<std::uint64_t, key_limit> words;
    int count = 0;
    FeatureKey() noexcept {}
    FeatureKey(const FeatureKey& other) noexcept { *this = other; }
    FeatureKey& operator=(const FeatureKey& other) noexcept {
        if (this != &other) { count = other.count; std::copy_n(other.words.begin(), count, words.begin()); }
        return *this;
    }
    bool operator==(const FeatureKey& other) const {
        return count == other.count && std::equal(words.begin(), words.begin() + count, other.words.begin());
    }
    bool add(std::uint64_t value) {
        if (count == key_limit) return false;
        words[count++] = value; return true;
    }
};
struct Features {
    FeatureKey key;
    bool supported = false;
    int to_camp = -1, to_altar = -1, clear_rounds = -1;
    float health = 0;
};
inline int distance_bin(int distance) { return distance < 0 ? -1 : distance / 4 + (distance % 4 != 0); }
inline int health_bucket(float health, float maximum) {
    if (!std::isfinite(health) || !std::isfinite(maximum) || maximum <= 0 || health < 0 || health > maximum) return -1;
    return int(std::ceil(double(health) * 4 / maximum));
}
inline bool current_health(Provenance p, int round) {
    return p.observed_round == round && p.source == Source::personal_vision && p.status == Status::exact_now;
}
inline bool budget(const Information& info) {
    return !info.work_budget.can_refine || info.work_budget.can_refine(info.work_budget.context);
}

inline Features extract_features(const Information& info, const Job& job, const View& view) {
    Features out;
    if (job.kind != JobKind::farm || job.second_group >= 0 || job.group < 0 || job.group >= info.group_count ||
        job.group >= max_groups || job.altar < 0 || job.altar >= info.altar_count || job.altar >= max_altars ||
        job.group >= view.count || !view.groups || info.own.channel != Channel::none || !info.own.alive) return out;
    const auto& g = info.groups[job.group]; const auto& a = info.altars[job.altar];
    const auto& composition = view.groups[job.group];
    if (view.kit.champion_kind < 0 || view.kit.attack_range < 0 || view.kit.strength < 0 || view.kit.focus < 0) return out;
    if (!g.visible || !current_health(g.provenance, info.round) ||
        (g.lifecycle != Lifecycle::living && g.lifecycle != Lifecycle::cleared) ||
        !composition.complete_public_layout || !composition.members || composition.count < 1 || composition.count > member_limit)
        return out;
    out.to_camp = distance(info, info.own.position, g.position);
    out.to_altar = distance(info, g.position, a.position);
    if (out.to_camp < 0 || out.to_altar < 0 || !std::isfinite(g.health) || g.health < 0) return out;
    const bool health_exact = current_health(g.health_provenance, info.round);
    out.health = health_exact ? g.health : g.full_health;
    out.clear_rounds = health_exact ? g.clear_rounds : g.full_clear_rounds;
    if (out.clear_rounds < 0 && g.lifecycle != Lifecycle::cleared) return out;
    auto& key = out.key;
    bool okay = true;
    const auto add = [&](auto value) { okay &= key.add(std::uint64_t(value)); };
    const auto floating = [&](float value) {
        okay &= std::isfinite(value); add(std::bit_cast<std::uint32_t>(value));
    };
    const auto provenance = [&](Provenance p) { add(p.observed_round); add(p.source); add(p.status); };
    const auto& own = info.own; const auto& settings = info.settings;
    add(info.round); add(info.our_score); add(info.enemy_score);
    add(info.observation_truncated); add(info.public_layout_truncated);
    add(own.id); add(own.position.x); add(own.position.y);
    floating(own.health); floating(own.maximum_health); add(own.bag); add(own.gold);
    add(own.channel); add(own.channel_ticks); add(own.alive); add(own.on_spawn); add(own.recall_safe);
    add(own.escape_rounds); floating(own.escape_incoming); add(info.own_spawn.x); add(info.own_spawn.y);
    add(settings.max_rounds); add(settings.winning_gems); add(settings.capture_rounds); add(settings.recall_rounds);
    okay &= std::isfinite(settings.tribute_multiplier); add(std::bit_cast<std::uint64_t>(settings.tribute_multiplier));
    add(settings.tribute_base); add(settings.tribute_minimum); floating(settings.spawn_heal_fraction);
    add(view.kit.champion_kind); add(view.kit.attack_range); floating(view.kit.strength); floating(view.kit.focus);
    add(view.kit.monster_shards); add(view.kit.jungle_shards); add(view.kit.version);
    add(g.lifecycle); provenance(g.provenance); provenance(g.health_provenance);
    floating(g.full_health); floating(g.incoming_damage); floating(g.full_incoming_damage); add(g.full_clear_rounds);
    add(g.gems); add(g.gold); add(g.respawn_rounds); add(g.respawn_remaining);
    add(g.visible); add(g.all_spawns_visible); add(g.reward_available); add(g.contested);
    add(g.occupied_spawn); add(g.occupancy_known); add(g.capture_started_round);
    add(g.eligible_allied_occupants); add(g.own_is_lowest_occupant);
    add(info.in_progress_group_id == g.id); add(own.position == g.position); add(g.position == a.position);
    add(own.position == a.position); add(info.own_spawn == g.position); add(info.own_spawn == a.position);
    add(a.accessible); add(a.hostile_lock); add(a.lock_known); add(a.remove_lock_rounds);
    add(distance_bin(out.to_camp)); add(distance_bin(out.to_altar)); add(composition.count);
    std::array<int, member_limit> order;
    for (int n = 0; n < composition.count; ++n) {
        if (!budget(info)) return out;
        order[n] = n;
    }
    std::sort(order.begin(), order.begin() + composition.count, [&](int x, int y) {
        const auto& left = composition.members[x]; const auto& right = composition.members[y];
        return left.kind != right.kind ? left.kind < right.kind : point_less(left.spawn, right.spawn);
    });
    double public_sum = 0, observed_sum = 0; bool every_health_exact = true;
    for (int n = 0; n < composition.count; ++n) {
        if (!budget(info)) return out;
        const auto& member = composition.members[order[n]];
        if (member.kind < 0 || !std::isfinite(member.public_maximum_health) || member.public_maximum_health <= 0) return out;
        for (int previous = 0; previous < n; ++previous)
            if (composition.members[order[previous]].spawn == member.spawn) return out;
        const bool exact = current_health(member.health_provenance, info.round);
        const float health = exact ? member.health : member.public_maximum_health;
        const int bucket = health_bucket(health, member.public_maximum_health);
        if (bucket < 0) return out;
        add(member.kind); floating(member.public_maximum_health); add(bucket); provenance(member.health_provenance);
        public_sum += member.public_maximum_health; observed_sum += health; every_health_exact &= exact;
    }
    if (std::abs(public_sum - g.full_health) > 1e-3 ||
        (current_health(g.health_provenance, info.round) && (!every_health_exact || std::abs(observed_sum - g.health) > 1e-3))) return out;
    out.supported = okay;
    return out;
}

struct Endpoint {
    int first_group_id = -1, second_group_id = -1, altar_id = -1;
    Point camp{}, altar{}, final_position{};
    bool final_position_known = false;
};
struct Statistics {
    int classes = 0, forecasts_computed = 0, cache_hits = 0, exact_jobs = 0;
    int full_paths = 0, rebased_paths = 0, rebase_fallbacks = 0, continuation_steps = 0;
    int unsupported_features = 0;
    bool first_macro_only = true;
    bool preparation_incomplete = false, budget_exhausted = false;
};
struct Allowance { int maximum_forecasts = max_jobs, maximum_continuations = max_jobs; };

struct Frame { Point origin{}, camp{}, altar{}, spawn{}; };
enum class Rebase { supported, invalid_candidate, invalid_prefix, invalid_lifecycle, invalid_endpoint };

// A normalized representative has exactly one source group. Restore the actual
// source identity and every untouched physical group before continuing. The
// caller must obtain baseline with common initialize_state_into, never invent
// its source binding or its unknown/living/captured lifecycle evidence.
inline Rebase rebase_single_into(const Information& info, const Job& job, const Forecast& first,
    const SimulationState& normalized, Frame frame, const SimulationState& baseline, SimulationState& out) {
    if (&out == &normalized || &out == &baseline || info.group_count < 0 || info.group_count > max_groups || info.altar_count < 0 || info.altar_count > max_altars ||
        job.kind != JobKind::farm || job.second_group >= 0 || job.group < 0 || job.group >= info.group_count ||
        job.altar < 0 || job.altar >= info.altar_count || baseline.group_count != info.group_count ||
        normalized.group_count != 1 || normalized.source_actor != info.own.id || normalized.source_round != info.round ||
        baseline.source_actor != info.own.id || baseline.source_round != info.round || baseline.now != info.round ||
        baseline.position != info.own.position || baseline.health != info.own.health || baseline.bag != info.own.bag || baseline.gold != info.own.gold ||
        baseline.stopped || baseline.deadline != info.round + horizon(info) || frame.origin != info.own.position || frame.spawn != info.own_spawn ||
        normalized.deadline != baseline.deadline || normalized.now < info.round || normalized.now > baseline.deadline)
        return Rebase::invalid_candidate;
    for (int g = 0; g < info.group_count; ++g) {
        const auto& actual = info.groups[g]; const auto& state = baseline.groups[g];
        const bool fresh = actual.visible && actual.provenance.observed_round == info.round && actual.provenance.status == Status::exact_now;
        const auto lifecycle = fresh ? actual.lifecycle : Lifecycle::unknown;
        const bool reward = fresh && (actual.lifecycle == Lifecycle::living || (actual.lifecycle == Lifecycle::cleared && actual.reward_available) ||
            actual.lifecycle == Lifecycle::respawning || actual.lifecycle == Lifecycle::captured);
        const int ready = fresh && actual.respawn_remaining >= 0 ? info.round + actual.respawn_remaining : -1;
        if (state.lifecycle != lifecycle || state.reward_known != reward || state.ready_round != ready || state.occupied_by_us)
            return Rebase::invalid_candidate;
    }
    if (first.macros_started != 1 || first.event_count < 0 || first.event_count > max_events || first.objective.paths != 0 ||
        first.final_round != normalized.now || first.final_bag != normalized.bag || first.final_gold != normalized.gold ||
        first.final_health != normalized.health || !std::isfinite(normalized.health) || normalized.health < 0 ||
        normalized.health > info.own.maximum_health || (!normalized.stopped && normalized.health <= 0)) return Rebase::invalid_prefix;
    int captures = 0, captured_at = -1;
    std::uint64_t bag = info.own.bag, gold = info.own.gold, secured = 0;
    for (int e = 0; e < first.event_count; ++e) {
        const auto& event = first.events[e];
        if ((event.group_id != -1 && event.group_id != info.groups[job.group].id) || event.bag_before != bag ||
            event.start.round < info.round || before(event.end, event.start) || event.end.round > normalized.now ||
            !std::isfinite(event.health_after) || event.health_after < 0 || event.health_after > info.own.maximum_health)
            return Rebase::invalid_prefix;
        if (event.kind == EventKind::capture) {
            if (event.group_id != info.groups[job.group].id || ++captures > 1) return Rebase::invalid_prefix;
            captured_at = event.end.round;
            const auto& group = info.groups[job.group];
            bag += allocated_reward(group.gems, group.eligible_allied_occupants, group.own_is_lowest_occupant);
            gold += allocated_reward(group.gold, group.eligible_allied_occupants, group.own_is_lowest_occupant);
        } else if (event.kind == EventKind::bank) {
            if (!bag || event.secured != bag || event.end.phase != Phase::owner_start ||
                event.end.round - event.start.round < tribute_ticks(unsigned(bag), info.settings)) return Rebase::invalid_prefix;
            secured += bag; bag = 0;
        }
        if (bag > std::numeric_limits<unsigned>::max() || gold > std::numeric_limits<unsigned>::max() ||
            event.bag_after != bag || event.gold_after != gold) return Rebase::invalid_prefix;
    }
    if (first.final_bag != bag || first.final_gold != gold || first.our_secured != secured) return Rebase::invalid_prefix;
    const auto& source = normalized.groups[0];
    if (unsigned(source.lifecycle) > unsigned(Lifecycle::respawning) || source.ready_round < -1 ||
        (captures == 1 && (source.lifecycle != Lifecycle::captured || !source.reward_known)) ||
        (captures == 0 && source.lifecycle == Lifecycle::captured) || (source.occupied_by_us && captures != 1) ||
        (captures == 1 && std::int64_t(source.ready_round) < std::int64_t(captured_at) + std::max(0, info.groups[job.group].respawn_rounds)))
        return Rebase::invalid_lifecycle;
    const std::array<Point, 4> from{{frame.origin, frame.camp, frame.altar, frame.spawn}};
    const std::array<Point, 4> to{{info.own.position, info.groups[job.group].position, info.altars[job.altar].position, info.own_spawn}};
    bool found = false; Point endpoint{};
    for (int p = 0; p < 4; ++p) if (normalized.position == from[p]) {
        if (found && endpoint != to[p]) return Rebase::invalid_endpoint;
        found = true; endpoint = to[p];
    }
    if (!found) return Rebase::invalid_endpoint;
    out = baseline;
    out.groups[job.group] = source;
    out.position = endpoint; out.health = normalized.health; out.bag = normalized.bag; out.gold = normalized.gold;
    out.now = normalized.now; out.stopped = normalized.stopped;
    return Rebase::supported;
}

// evaluate_first_macro exposes the original bounded core. evaluate_full rebases
// each first macro to physical groups, then runs the common fixed continuation.
// Every batch invalidates the cache, including after any observation/stat/bag/
// settings change. Active/control build identical features/classes and share
// the same callback fuel reserve and forecast-call allowance.
// Keep this bounded cache on the actor-owned heap, not the 64 KiB WASI stack.
class Quotient {
    struct Candidate { Features features; int class_index = -1; };
    struct Class {
        FeatureKey key;
        int source = -1, to_camp = 0, to_altar = 0, clear_rounds = 0;
        float health = 0;
        bool ready = false;
        Forecast forecast;
        SimulationState state;
        Frame frame{};
    };
    std::array<Candidate, max_jobs> candidates_;
    std::array<Class, class_limit> classes_;
    std::unique_ptr<Information> representative_ = std::make_unique<Information>();
    std::unique_ptr<Forecast> path_a_ = std::make_unique<Forecast>(), path_b_ = std::make_unique<Forecast>();
    std::unique_ptr<SimulationState> state_a_ = std::make_unique<SimulationState>(), state_b_ = std::make_unique<SimulationState>();
    std::unique_ptr<SimulationState> baseline_ = std::make_unique<SimulationState>();
    bool enabled_;
    struct Routes { Point origin, camp, altar; int to_camp, to_altar; };
    static int routed(Point from, Point to, const void* context) {
        const auto& r = *static_cast<const Routes*>(context);
        if (from == r.origin && to == r.camp) return r.to_camp;
        if (from == r.camp && to == r.altar) return r.to_altar;
        return unreachable;
    }
    void representative(const Information& info, const Job& source, Class& cl, const Menu& menu, Forecast& out, bool checkpoint) {
        auto& target = *representative_;
        target.round = info.round; target.settings = info.settings; target.own = info.own; target.own_spawn = info.own_spawn;
        target.our_score = info.our_score; target.enemy_score = info.enemy_score;
        target.group_count = target.altar_count = 1;
        target.groups[0] = info.groups[source.group]; target.altars[0] = info.altars[source.altar];
        target.in_progress_group_id = info.in_progress_group_id == target.groups[0].id ? 0 : -1;
        target.groups[0].id = 0; target.altars[0].id = 0;
        target.groups[0].health = cl.health; target.groups[0].clear_rounds = cl.clear_rounds;
        target.observation_truncated = info.observation_truncated; target.public_layout_truncated = info.public_layout_truncated;
        target.work_budget = info.work_budget;
        const Routes routes{target.own.position, target.groups[0].position, target.altars[0].position, cl.to_camp, cl.to_altar};
        target.travel = {routed, &routes};
        Job single = source; single.group = single.altar = 0; single.second_group = -1;
        if (checkpoint) plan4::simulate_first_into(target, single, menu, out, cl.state);
        else plan4::predict_into(target, single, Menu{}, out);
        cl.frame = {target.own.position, target.groups[0].position, target.altars[0].position, target.own_spawn};
    }
    static Endpoint endpoint(const Information& info, const Job& job, const Forecast& forecast) {
        Endpoint out;
        if (job.group >= 0 && job.group < info.group_count) {
            out.first_group_id = info.groups[job.group].id; out.camp = info.groups[job.group].position;
        }
        if (job.second_group >= 0 && job.second_group < info.group_count) out.second_group_id = info.groups[job.second_group].id;
        if (job.altar >= 0 && job.altar < info.altar_count) {
            out.altar_id = info.altars[job.altar].id; out.altar = info.altars[job.altar].position;
            if (forecast.completed_conversion || forecast.no_conversion) { out.final_position_known = true; out.final_position = out.altar; }
        }
        return out;
    }
    bool finish_fixed(const Information& info, const Menu& menu, Forecast& out, Statistics& statistics) {
        int cursor = 0;
        path_b_->budget_exhausted = false;
        for (int step = 0; step < max_macros; ++step) {
            if (!budget(info)) return false;
            const auto result = plan4::advance_fixed_into(info, menu, *path_a_, *state_a_, *path_b_, *state_b_, cursor);
            ++statistics.continuation_steps;
            if (!result.valid) return false;
            path_a_.swap(path_b_); state_a_.swap(state_b_);
            if (!result.advanced) { out = *path_a_; return true; }
        }
        return false;
    }
public:
    explicit Quotient(bool enabled = true) : enabled_(enabled) {}
    Statistics evaluate_first_macro(const Information& info, const Menu& menu, const View& view,
        Evaluation& out, std::array<Endpoint, max_jobs>& endpoints, Allowance allowance = {}) {
        return evaluate_impl(info, menu, view, out, endpoints, allowance, false);
    }
    Statistics evaluate_full(const Information& info, const Menu& menu, const View& view,
        Evaluation& out, std::array<Endpoint, max_jobs>& endpoints, Allowance allowance = {}) {
        return evaluate_impl(info, menu, view, out, endpoints, allowance, true);
    }
private:
    Statistics evaluate_impl(const Information& info, const Menu& menu, const View& view,
        Evaluation& out, std::array<Endpoint, max_jobs>& endpoints, Allowance allowance, bool full) {
        Statistics statistics;
        statistics.first_macro_only = !full;
        out.menu = menu; out.chosen = -1; out.evaluated_count = 0; out.budget_exhausted = false;
        if (menu.count < 0 || menu.count > max_jobs || info.group_count < 0 || info.group_count > max_groups ||
            info.altar_count < 0 || info.altar_count > max_altars) { statistics.preparation_incomplete = true; return statistics; }
        int class_count = 0;
        for (int n = 0; n < menu.count; ++n) {
            if (!budget(info)) { statistics.preparation_incomplete = statistics.budget_exhausted = out.budget_exhausted = true; return statistics; }
            auto& candidate = candidates_[n]; candidate.class_index = -1;
            candidate.features = extract_features(info, menu.jobs[n], view);
            if (!budget(info)) { statistics.preparation_incomplete = statistics.budget_exhausted = out.budget_exhausted = true; return statistics; }
            if (!candidate.features.supported) { ++statistics.unsupported_features; continue; }
            int found = -1;
            for (int c = 0; c < class_count; ++c) if (classes_[c].key == candidate.features.key) { found = c; break; }
            if (found < 0) {
                if (class_count == class_limit) continue; // Extra candidates are computed exactly.
                found = class_count++; auto& cl = classes_[found];
                cl.key = candidate.features.key; cl.source = n; cl.ready = false;
                cl.to_camp = cl.to_altar = cl.clear_rounds = 0; cl.health = 0;
            }
            auto& cl = classes_[found]; candidate.class_index = found;
            cl.to_camp = std::max(cl.to_camp, candidate.features.to_camp);
            cl.to_altar = std::max(cl.to_altar, candidate.features.to_altar);
            cl.clear_rounds = std::max(cl.clear_rounds, candidate.features.clear_rounds);
            cl.health = std::max(cl.health, candidate.features.health);
        }
        statistics.classes = class_count;
        bool baseline_ready = false;
        int continuations = 0;
        for (int n = 0; n < menu.count; ++n) {
            if (!budget(info)) { statistics.budget_exhausted = out.budget_exhausted = true; break; }
            if (full && continuations >= std::max(0, allowance.maximum_continuations)) {
                statistics.budget_exhausted = out.budget_exhausted = true; break;
            }
            const int c = candidates_[n].class_index;
            auto& forecast = out.forecasts[n]; const auto& job = menu.jobs[n];
            if (enabled_ && c >= 0 && classes_[c].ready) {
                forecast = classes_[c].forecast; ++statistics.cache_hits;
            } else {
                if (!budget(info) || statistics.forecasts_computed >= std::max(0, allowance.maximum_forecasts)) {
                    statistics.budget_exhausted = out.budget_exhausted = true; break;
                }
                ++statistics.forecasts_computed;
                if (enabled_ && c >= 0) {
                    auto& cl = classes_[c]; representative(info, menu.jobs[cl.source], cl, full ? menu : Menu{}, forecast, full);
                    if (!forecast.budget_exhausted) { cl.forecast = forecast; cl.ready = true; }
                } else {
                    if (full) plan4::simulate_first_into(info, job, menu, forecast, *state_a_);
                    else plan4::predict_into(info, job, Menu{}, forecast);
                    ++statistics.exact_jobs;
                }
            }
            if (forecast.budget_exhausted) { statistics.budget_exhausted = out.budget_exhausted = true; break; }
            if (enabled_ && c >= 0) for (int e = 0; e < forecast.event_count; ++e)
                if (forecast.events[e].group_id == 0) forecast.events[e].group_id = info.groups[job.group].id;
            endpoints[n] = endpoint(info, job, forecast);
            if (full) {
                if (enabled_ && c >= 0) {
                    if (!baseline_ready) {
                        baseline_ready = plan4::initialize_state_into(info, menu, *path_a_, *baseline_);
                        if (!baseline_ready) { statistics.preparation_incomplete = true; break; }
                    }
                    const auto& cl = classes_[c];
                    if (rebase_single_into(info, job, forecast, cl.state, cl.frame, *baseline_, *state_a_) != Rebase::supported) {
                        if (!budget(info) || statistics.forecasts_computed >= std::max(0, allowance.maximum_forecasts)) {
                            statistics.budget_exhausted = out.budget_exhausted = true; break;
                        }
                        ++statistics.forecasts_computed; ++statistics.exact_jobs; ++statistics.rebase_fallbacks;
                        plan4::simulate_first_into(info, job, menu, forecast, *state_a_);
                    } else ++statistics.rebased_paths;
                }
                *path_a_ = forecast;
                ++continuations;
                if (!finish_fixed(info, menu, forecast, statistics)) {
                    if (!budget(info) || path_a_->budget_exhausted || path_b_->budget_exhausted)
                        statistics.budget_exhausted = out.budget_exhausted = true;
                    else statistics.preparation_incomplete = true;
                    break;
                }
                if (forecast.budget_exhausted) { statistics.budget_exhausted = out.budget_exhausted = true; break; }
                endpoints[n].final_position = state_a_->position; endpoints[n].final_position_known = true;
                ++statistics.full_paths;
            }
            ++out.evaluated_count;
        }
        Menu completed = menu; completed.count = out.evaluated_count;
        out.chosen = choose(info, completed, out.forecasts);
        return statistics;
    }
};
} // namespace plan4::quotient
