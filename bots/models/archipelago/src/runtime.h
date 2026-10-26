#pragma once
#include "archipelago.h"
#include "../../common/model-runtime.h"
#include <tuple>

namespace plan4::runtime {
class Archipelago : public M0 {
    static constexpr int legs_limit = 3;
    struct Key {
        JobKind kind = JobKind::hold;
        Point origin{};
        std::array<Point, legs_limit> targets{};
        std::array<int, legs_limit> distances{};
        int legs = 0, first = -1, second = -1, altar = -1;
        bool operator==(const Key&) const = default;
    };
    enum class Cost { base, pending, found, unreachable };
    struct Repair {
        std::array<Point, archipelago::max_blockers> world{}, avoid{};
        std::array<bool, legs_limit> blocked{}, cut{};
        std::array<Cost, legs_limit> status{};
        std::array<int, legs_limit> distances{};
        int world_count = -1, avoid_count = 0, filter = 0, scanned = 0;
        bool living_first = false, living_second = false, metadata = false, path_hit = false, connector_hit = false, complete = false;
    };
    struct Cached {
        Key key{};
        archipelago::Route route;
        std::array<int, legs_limit> ends{};
        int leg = 0, copied = 0;
        std::uint64_t serial = 0;
        bool occupied = false;
        Repair repair{};
    };
public:
    struct Evidence {
        bool topology_supported = false, graph_complete = false, current_observation = false, incomplete_observation = false;
        int attempted = -1, certified_routes = 0, unsupported_jobs = 0, pending_repairs = 0;
        int affected_jobs = 0, applied_jobs = 0, conditional_unreachable = 0, alternate_routes = 0, uncertain_risks = 0;
        int topology_copied = 0, graph_work = 0, route_work = 0, occupancy_queries = 0, search_work = 0;
    };
private:
    bool enabled_ = true, copying_ = false, graph_started_ = false, spatial_supported_ = false, batch_current_ = false;
    bool planning_quota_set_ = false, static_quota_set_ = false, routes_dirty_ = false, batch_budget_exhausted_ = false;
    PublicTopology topology_{};
    SpatialFrame spatial_{};
    PublicLayoutFrame layout_{}; // Borrowed only for this common planning call.
    const Information* snapshot_ = nullptr;
    std::vector<unsigned char> blocked_;
    // Construction runs before the caller can supply a fuel guard. In
    // particular, a Route's Point capacity and Information's Group capacity
    // must not be initialized here for actors that never use this model.
    std::unique_ptr<archipelago::Graph> graph_;
    std::unique_ptr<archipelago::State> dependencies_;
    std::unique_ptr<archipelago::RouteSearch> search_;
    std::array<std::unique_ptr<Cached>, max_jobs> caches_;
    std::array<int, max_jobs> jobs_{}, bound_{}, binding_{};
    std::array<std::uint64_t, max_jobs> bound_serial_{};
    std::array<Point, archipelago::max_blockers> history_{}, fresh_{};
    std::array<archipelago::BlockedTile, archipelago::max_blockers * 2> samples_{};
    int history_count_ = 0, fresh_count_ = 0, sample_count_ = 0;
    int used_ = 0, quota_ = 192, static_ceiling_ = 128, round_ = -1, actor_ = -1, copy_cursor_ = 0;
    int base_cursor_ = 0, observe_job_ = 0, observe_point_ = 0, repair_cursor_ = 0, search_job_ = -1, search_leg_ = -1;
    bool search_active_ = false;
    std::uint64_t cache_serial_ = 0;
    Evidence evidence_{};
    std::unique_ptr<Information> conditional_;
    std::unique_ptr<Forecast> first_, repaired_;
    std::unique_ptr<SimulationState> checkpoint_;
    const Cached* quoting_ = nullptr;

    bool current(const Information& info) const {
        return spatial_supported_ && spatial_.clock.round == info.round && spatial_.clock.own.id == info.own.id && spatial_.information == &info &&
            snapshot_ == &info && info.round == round_ && info.own.id == actor_;
    }
    void begin_clock(ObservationClock clock) {
        if (clock.round == round_ && clock.own.id == actor_) return;
        round_ = clock.round; actor_ = clock.own.id; used_ = 0; quota_ = 192; static_ceiling_ = 128;
        planning_quota_set_ = static_quota_set_ = false; evidence_ = {}; batch_current_ = spatial_supported_ = false;
    }
    bool valid_topology(const PublicTopology& topology) const {
        return topology.complete && topology.walkable && topology.width > 0 && topology.height > 0 &&
            std::int64_t(topology.width) * topology.height <= archipelago::max_vertices;
    }
    void advance_geometry(PublicTopology topology, WorkBudget fuel, int ceiling) {
        if (!valid_topology(topology)) return;
        evidence_.topology_supported = true;
        if (topology_.width != topology.width || topology_.height != topology.height || topology_.walkable != topology.walkable || topology_.context != topology.context) {
            topology_ = topology; blocked_.clear();
            copy_cursor_ = 0; copying_ = true; graph_started_ = false; routes_dirty_ = true;
            evidence_.graph_complete = false;
        }
        const auto charge = [&]() {
            if (used_ >= ceiling || !archipelago::budget(fuel)) return false;
            ++used_; return true;
        };
        const int size = topology_.width * topology_.height;
        while (copying_ && copy_cursor_ < size) {
            if (!charge()) return;
            if (!copy_cursor_) blocked_.reserve(size);
            const Point point{copy_cursor_ % topology_.width, copy_cursor_ / topology_.width};
            blocked_.push_back(static_cast<unsigned char>(!topology_.walkable(point, topology_.context))); ++copy_cursor_; ++evidence_.topology_copied;
        }
        if (!graph_started_) {
            if (!charge()) return;
            if (!graph_) graph_ = std::make_unique<archipelago::Graph>();
            graph_started_ = graph_->initialize(topology_.width, topology_.height, std::move(blocked_)); copying_ = false;
        }
        const auto progress = graph_->advance(std::max(0,ceiling-used_), fuel); used_ += progress.work; evidence_.graph_work += progress.work;
        evidence_.graph_complete = graph_->complete();
    }
    void invalidate_routes() {
        if (!routes_dirty_) return;
        history_count_ = 0; search_active_ = false;
        for (auto& cache : caches_) if (cache) cache->occupied = false;
        bound_.fill(-1); binding_.fill(0);
        if (dependencies_) dependencies_->clear();
        routes_dirty_ = false;
    }
    bool take(const Information& info, int count = 1) {
        if (count < 0 || count > quota_ - used_ || !archipelago::budget(info.work_budget)) return false;
        used_ += count; return true;
    }
    int remaining() const { return std::max(0, quota_ - used_); }
    bool make_key(const Information& info, const Job& job, Key& key) {
        if (!take(info)) return false;
        key = {}; key.kind = job.kind; key.origin = info.own.position;
        if (job.kind == JobKind::farm) {
            if (job.group < 0 || job.group >= info.group_count || job.second_group >= info.group_count) return false;
            key.first = info.groups[job.group].id; key.targets[key.legs++] = info.groups[job.group].position;
            if (job.second_group >= 0) { key.second = info.groups[job.second_group].id; key.targets[key.legs++] = info.groups[job.second_group].position; }
        } else if (job.kind != JobKind::bank) return false;
        if (job.altar < 0 || job.altar >= info.altar_count) return false;
        key.altar = info.altars[job.altar].id; key.targets[key.legs++] = info.altars[job.altar].position;
        Point from = key.origin; int total = 0;
        for (int leg = 0; leg < key.legs; ++leg) {
            key.distances[leg] = distance(info, from, key.targets[leg]);
            if (key.distances[leg] < 0 || key.distances[leg] >= archipelago::max_route_points - total) return false;
            total += key.distances[leg]; from = key.targets[leg];
        }
        return total < archipelago::max_route_points;
    }
    int find_cache(const Information& info, const Key& key, const std::array<bool, max_jobs>& pinned) {
        for (int c = 0; c < max_jobs; ++c) if (caches_[c] && caches_[c]->occupied && caches_[c]->key == key) return c;
        int slot = 0;
        while (slot < max_jobs && pinned[slot]) ++slot;
        if (slot == max_jobs) return -1;
        if (!caches_[slot]) {
            if (!take(info)) return -1;
            caches_[slot] = std::make_unique<Cached>();
        }
        auto& cache = *caches_[slot]; cache.occupied = true; cache.key = key; cache.serial = ++cache_serial_;
        cache.route.count = 0; cache.route.distance = -1; cache.route.graph = graph_.get(); cache.route.graph_generation = graph_->generation();
        cache.route.status = archipelago::RouteStatus::pending; cache.route.complete_path = false;
        cache.leg = cache.copied = 0; cache.repair = {};
        if (search_job_ == slot) search_active_ = false;
        return slot;
    }
    void advance_base(const Information& info, int index, int limit) {
        auto& cache = *caches_[index]; auto& route = cache.route;
        if (route.status == archipelago::RouteStatus::found || limit <= 0) return;
        const int before = used_;
        if (!route.count) {
            if (!take(info) || !graph_->walkable(cache.key.origin)) return;
            route.points[route.count++] = cache.key.origin;
        }
        while (cache.leg < cache.key.legs && used_ - before < limit && remaining()) {
            const int required = cache.key.distances[cache.leg];
            if (cache.copied == required) {
                if (!take(info)) return;
                if (route.points[route.count - 1] != cache.key.targets[cache.leg]) return;
                cache.ends[cache.leg++] = route.count - 1; cache.copied = 0; continue;
            }
            const int count = std::min({8, required - cache.copied, limit - (used_ - before), remaining()});
            if (count <= 0 || !spatial_.route_prefix || !take(info, count)) return;
            std::array<Point, 8> prefix;
            const int returned = spatial_.route_prefix(route.points[route.count - 1], cache.key.targets[cache.leg], prefix.data(), count, spatial_.route_context);
            if (returned != count) return; // Missing cache stays unsupported; never substitute a route.
            for (int p = 0; p < count; ++p) {
                if (route.count == archipelago::max_route_points || !graph_->adjacent(route.points[route.count - 1], prefix[p])) return;
                route.points[route.count++] = prefix[p]; ++cache.copied;
            }
        }
        if (cache.leg == cache.key.legs) {
            route.distance = route.count - 1; route.complete_path = true; route.status = archipelago::RouteStatus::found;
        }
    }
    void prepare_routes(const Information& info, const Menu& menu) {
        jobs_.fill(-1);
        if (!dependencies_) {
            if (!take(info)) return;
            dependencies_ = std::make_unique<archipelago::State>(true);
        }
        std::array<bool, max_jobs> pinned{};
        for (int n = 0; n < menu.count; ++n) {
            Key key; jobs_[n] = -1;
            if (!make_key(info, menu.jobs[n], key)) { bound_[n] = -1; binding_[n] = 0; continue; }
            const int found = find_cache(info, key, pinned); jobs_[n] = found;
            if (found >= 0) pinned[found] = true;
            if (bound_[n] != found || (found >= 0 && bound_serial_[n] != caches_[found]->serial)) {
                bound_[n] = found; binding_[n] = 0; bound_serial_[n] = found >= 0 ? caches_[found]->serial : 0;
            }
        }
        const int allowance = std::max(0, remaining() / 2), start = used_;
        for (int visited = 0; visited < menu.count && used_ - start < allowance; ++visited) {
            const int n = (base_cursor_ + visited) % menu.count, c = jobs_[n];
            if (c < 0) continue;
            advance_base(info, c, allowance - (used_ - start));
            if (caches_[c]->route.complete_path && used_ - start < allowance) {
                const auto progress = dependencies_->bind_route_prefix(n, *graph_, caches_[c]->route, binding_[n], allowance - (used_ - start), info.work_budget);
                used_ += progress.work;
            }
        }
        if (menu.count) base_cursor_ = (base_cursor_ + 1) % menu.count;
        evidence_.route_work += used_ - start;
    }
    void occupancy(const Information& info, Point point) {
        if (!take(info)) return;
        ++evidence_.occupancy_queries;
        const auto state = spatial_.current_complete && spatial_.occupancy ? spatial_.occupancy(point, spatial_.occupancy_context) : TileOccupancy::unknown;
        if (state == TileOccupancy::unknown) { ++evidence_.uncertain_risks; return; }
        for (int n = 0; n < sample_count_; ++n) if (samples_[n].position == point) return;
        if (sample_count_ == int(samples_.size())) { evidence_.incomplete_observation = true; return; }
        samples_[sample_count_++] = {point, state == TileOccupancy::occupied, true, {info.round, Source::personal_vision, Status::exact_now}};
        if (state != TileOccupancy::occupied) return;
        for (int n = 0; n < fresh_count_; ++n) if (fresh_[n] == point) return;
        if (fresh_count_ == archipelago::max_blockers) { evidence_.incomplete_observation = true; return; }
        fresh_[fresh_count_++] = point;
    }
    void observe_routes(const Information& info, const Menu& menu) {
        fresh_count_ = sample_count_ = 0;
        const int allowance = std::min(64, remaining() / 3), start = used_;
        for (int n = 0; n < history_count_ && used_ - start < allowance; ++n) occupancy(info, history_[n]);
        int empty = 0;
        while (menu.count && used_ - start < allowance && empty < menu.count) {
            observe_job_ %= menu.count; const int c = jobs_[observe_job_];
            if (c < 0 || !caches_[c]->route.complete_path) { ++observe_job_; observe_point_ = 0; ++empty; continue; }
            const auto& route = caches_[c]->route;
            if (observe_point_ >= route.count) { ++observe_job_; observe_point_ = 0; ++empty; continue; }
            occupancy(info, route.points[observe_point_++]);
            if (!remaining() || !archipelago::budget(info.work_budget)) break;
        }
        std::sort(fresh_.begin(), fresh_.begin() + fresh_count_, point_less);
        for (int n = 0; n < history_count_;) {
            bool free = false;
            for (int sample = 0; sample < sample_count_; ++sample)
                free |= samples_[sample].position == history_[n] && !samples_[sample].blocked;
            if (free) history_[n] = history_[--history_count_]; else ++n;
        }
        for (int n = 0; n < fresh_count_; ++n) {
            int slot = 0; while (slot < history_count_ && history_[slot] != fresh_[n]) ++slot;
            if (slot == history_count_ && history_count_ < archipelago::max_blockers) history_[history_count_++] = fresh_[n];
        }
        if (dependencies_) dependencies_->observe(*graph_, info.round, info.own.id, samples_.data(), std::min(sample_count_, archipelago::max_blockers),
            evidence_.incomplete_observation || sample_count_ > archipelago::max_blockers);
    }
    bool metadata_for(int group_id, const Information& info) const {
        if (group_id < 0) return true;
        if (layout_.clock.round != round_ || layout_.clock.own.id != actor_ || layout_.group_count < 0 || layout_.group_count > 8 ||
            layout_.member_count < 0 || layout_.member_count > 256 || !layout_.groups || !layout_.members) return false;
        for (int g = 0; g < layout_.group_count; ++g) if (layout_.groups[g].group_id == group_id) {
            const auto& row = layout_.groups[g];
            if (!row.complete || row.count <= 0 || row.count > 32 || row.first < 0 || row.first > layout_.member_count - row.count) return false;
            const Group* source = nullptr;
            for (int k = 0; k < info.group_count; ++k) if (info.groups[k].id == group_id) source = &info.groups[k];
            if (!source) return false;
            std::array<bool, 32> ordinals{}; double health = 0;
            for (int m = 0; m < row.count; ++m) {
                const auto& member = layout_.members[row.first + m];
                if (member.group_id != group_id || member.member_id < 0 || member.ordinal < 0 || member.ordinal >= row.count ||
                    member.declared_members != row.count || ordinals[member.ordinal] || !std::isfinite(member.maximum_health) ||
                    member.maximum_health <= 0 || !graph_->walkable(member.spawn)) return false;
                if (member.ordinal == 0 && member.spawn != source->position) return false;
                ordinals[member.ordinal] = true; health += member.maximum_health;
            }
            return std::abs(health - source->full_health) <= 1e-3;
        }
        return false;
    }
    bool production_endpoint(Point point, const Job& job, const Information& info) const {
        if (job.kind != JobKind::farm) return false;
        for (int index : {job.group, job.second_group}) if (index >= 0 && index < info.group_count) {
            const auto& group = info.groups[index];
            // A production job targets the group, not an occupied anchor tile.
            // Its own clear/capture guards remain in the unchanged M0 body.
            if (point == group.position) return true;
            for (int g = 0; g < layout_.group_count; ++g) if (layout_.groups[g].group_id == group.id) {
                const auto& row = layout_.groups[g];
                for (int n = 0; n < row.count; ++n) if (layout_.members[row.first + n].spawn == point) return true;
            }
        }
        return false;
    }
    void prepare_repair(const Information& info, const Job& job, int c) {
        auto& cache = *caches_[c]; auto& repair = cache.repair;
        const bool living_first = job.kind == JobKind::farm;
        const bool living_second = job.second_group >= 0;
        const bool metadata = metadata_for(living_first ? cache.key.first : -1, info) && metadata_for(living_second ? cache.key.second : -1, info);
        if (repair.world_count != fresh_count_ || repair.living_first != living_first || repair.living_second != living_second || repair.metadata != metadata ||
            !std::equal(fresh_.begin(), fresh_.begin() + fresh_count_, repair.world.begin())) {
            repair = {}; repair.world_count = fresh_count_; repair.living_first = living_first; repair.living_second = living_second; repair.metadata = metadata;
            std::copy_n(fresh_.begin(), fresh_count_, repair.world.begin());
            if (search_job_ == c) search_active_ = false;
        }
        if (!metadata || repair.complete) return;
        while (repair.filter < fresh_count_) {
            if (!take(info)) return;
            const auto point = fresh_[repair.filter++];
            if (!production_endpoint(point, job, info)) repair.avoid[repair.avoid_count++] = point;
        }
        while (repair.scanned < cache.route.count) {
            if (!take(info)) return;
            const int index = repair.scanned++; const auto point = cache.route.points[index];
            if (std::find(repair.avoid.begin(), repair.avoid.begin() + repair.avoid_count, point) == repair.avoid.begin() + repair.avoid_count) continue;
            repair.path_hit = true; repair.connector_hit |= graph_->articulation(point);
            for (int leg = 0; leg < cache.key.legs; ++leg) {
                const int begin = leg ? cache.ends[leg - 1] : 0, end = cache.ends[leg];
                if (index < begin || index > end) continue;
                repair.blocked[leg] = true;
                repair.cut[leg] |= index == begin || index == end ||
                    graph_->edge_component(cache.route.points[index - 1], point) != graph_->edge_component(point, cache.route.points[index + 1]);
            }
        }
        for (int leg = 0; leg < cache.key.legs; ++leg) {
            repair.distances[leg] = cache.key.distances[leg];
            repair.status[leg] = !repair.blocked[leg] ? Cost::base : repair.cut[leg] ? Cost::unreachable : Cost::pending;
        }
        repair.complete = true;
    }
    void repair_routes(const Information& info, const Menu& menu) {
        int start = repair_cursor_;
        if (search_active_) {
            bool present = false;
            for (int n = 0; n < menu.count; ++n) if (jobs_[n] == search_job_) { start = n; present = true; break; }
            if (!present) search_active_ = false;
        }
        for (int visited = 0; visited < menu.count && remaining(); ++visited) {
            const int n = (start + visited) % menu.count, c = jobs_[n];
            if (c < 0 || !caches_[c]->route.complete_path || binding_[n] != caches_[c]->route.count) continue;
            prepare_repair(info, menu.jobs[n], c);
            auto& cache = *caches_[c]; auto& repair = cache.repair;
            if (!repair.complete || !repair.path_hit || (!repair.connector_hit && n != evidence_.attempted)) continue;
            for (int leg = 0; leg < cache.key.legs && remaining(); ++leg) if (repair.status[leg] == Cost::pending) {
                if (!search_active_ || search_job_ != c || search_leg_ != leg) {
                    if (!take(info)) return;
                    if (!search_) search_ = std::make_unique<archipelago::RouteSearch>();
                    if (!search_->begin(*graph_, leg ? cache.key.targets[leg - 1] : cache.key.origin, cache.key.targets[leg], repair.avoid.data(), repair.avoid_count)) return;
                    search_job_ = c; search_leg_ = leg; search_active_ = true;
                }
                const auto progress = search_->advance(remaining(), info.work_budget);
                used_ += progress.work; evidence_.search_work += progress.work;
                if (search_->result().status == archipelago::RouteStatus::pending) return;
                const auto& result = search_->result();
                if (result.status == archipelago::RouteStatus::unreachable) repair.status[leg] = Cost::unreachable;
                else if (result.status == archipelago::RouteStatus::found && result.complete_path && result.distance >= cache.key.distances[leg]) {
                    repair.status[leg] = Cost::found; repair.distances[leg] = result.distance;
                } else return; // Missing/too-long/invalid alternative remains unsupported.
                search_active_ = false;
            }
        }
        if (menu.count) repair_cursor_ = (repair_cursor_ + 1) % menu.count;
    }
    static int conditional_distance(Point a, Point b, const void* context) {
        const auto& self = *static_cast<const Archipelago*>(context); const auto& cache = *self.quoting_;
        Point from = cache.key.origin;
        for (int leg = 0; leg < cache.key.legs; ++leg) {
            if (a == from && b == cache.key.targets[leg]) return cache.repair.status[leg] == Cost::unreachable ? -1 : cache.repair.distances[leg];
            from = cache.key.targets[leg];
        }
        return self.spatial_.travel.distance ? self.spatial_.travel.distance(a, b, self.spatial_.travel.context) : -1;
    }
public:
    explicit Archipelago(bool enabled = true) : enabled_(enabled) { bound_.fill(-1); jobs_.fill(-1); }
    const char* name() const { return "archipelago"; }
    void observe_layout(const PublicLayoutFrame& frame) {
        layout_ = frame;
        if (frame.group_count < 0 || frame.group_count > 8 || frame.member_count < 0 || frame.member_count > 256 ||
            (frame.group_count && !frame.groups) || (frame.member_count && !frame.members)) { layout_ = {}; return; }
        for (int g = 0; g < frame.group_count; ++g) for (int k = 0; k < g; ++k)
            if (frame.groups[g].group_id == frame.groups[k].group_id) { layout_ = {}; return; }
    }
    void observe_snapshot(const Information& info, MemberFrame = {}) { snapshot_ = &info; }
    void static_spatial(const StaticSpatialFrame& frame) {
        if (frame.clock.round < 0 || frame.clock.own.id < 0 || (frame.clock.own.id == actor_ && frame.clock.round < round_)) return;
        begin_clock(frame.clock);
        const int limit = std::clamp(frame.graph_quota,0,128);
        static_ceiling_ = static_quota_set_ ? std::min(static_ceiling_,limit) : limit; static_quota_set_ = true;
        // The static path cannot observe occupants, bind menu routes or alter a
        // forecast. The later planning frame pays its own remaining work.
        advance_geometry(frame.topology,frame.work_budget,std::min(static_ceiling_,quota_));
    }
    void observe_spatial(const SpatialFrame& frame) {
        spatial_ = frame; spatial_supported_ = false;
        batch_current_ = false;
        const auto* info = frame.information;
        if (!info || info->round < 0 || info->own.id < 0 || (info->own.id == actor_ && info->round < round_)) return;
        begin_clock({info->round,{info->own.id}});
        if (frame.clock.round != info->round || frame.clock.own.id != info->own.id) return;
        const int limit = std::clamp(frame.graph_quota,0,192);
        quota_ = planning_quota_set_ ? std::min(quota_,limit) : limit; planning_quota_set_ = true;
        evidence_.topology_supported = false; evidence_.current_observation = false; evidence_.graph_complete = false;
        if (!valid_topology(frame.topology)) return;
        spatial_supported_ = true; evidence_.topology_supported = true; evidence_.current_observation = frame.current_complete;
        advance_geometry(frame.topology,info->work_budget,quota_); invalidate_routes();
        evidence_.graph_complete = graph_started_ && graph_->complete();
    }
    int spatial_vertices() const { return used_; }
    void evaluate_into(const Information& info, const Menu& menu, Evaluation& out) {
        batch_current_ = current(info);
        evidence_.attempted = -1; evidence_.certified_routes = evidence_.unsupported_jobs = evidence_.pending_repairs = 0;
        evidence_.affected_jobs = evidence_.applied_jobs = evidence_.conditional_unreachable = evidence_.alternate_routes = 0;
        plan4::evaluate_into(info, menu, out);
        batch_budget_exhausted_ = out.budget_exhausted;
        if (!current(info) || !graph_started_ || !graph_->complete() || menu.count < 0 || menu.count > max_jobs || out.budget_exhausted) return;
        evidence_.attempted = out.chosen;
        const int evaluated_count = out.evaluated_count;
        prepare_routes(info, menu); observe_routes(info, menu); repair_routes(info, menu);
        bool conditional_current = false;
        for (int n = 0; n < evaluated_count; ++n) {
            const auto& job = menu.jobs[n];
            // Common RENEW/HOLD keep their complete M0 body and continuation;
            // this model supplies no spatial replacement for either job.
            if ((job.kind == JobKind::renew || job.kind == JobKind::hold) &&
                job.group == -1 && job.second_group == -1 && job.altar == -1) continue;
            const int c = jobs_[n];
            if (c < 0 || !caches_[c]->route.complete_path || binding_[n] != caches_[c]->route.count) { ++evidence_.unsupported_jobs; continue; }
            ++evidence_.certified_routes; const auto& cache = *caches_[c]; const auto& repair = cache.repair;
            if (!repair.complete || !repair.metadata) { ++evidence_.unsupported_jobs; continue; }
            if (!repair.path_hit || (!repair.connector_hit && n != evidence_.attempted)) continue;
            ++evidence_.affected_jobs;
            bool supported = true, unreachable = false, alternate = false;
            for (int leg = 0; leg < cache.key.legs; ++leg) {
                supported &= repair.status[leg] != Cost::pending; unreachable |= repair.status[leg] == Cost::unreachable;
                alternate |= repair.status[leg] == Cost::found;
            }
            if (!supported) { ++evidence_.pending_repairs; continue; }
            if (!archipelago::budget(info.work_budget)) { out.evaluated_count = n; out.budget_exhausted = true; break; }
            if (!conditional_current) {
                // Copy the actual source directly; default-constructing its
                // 256 groups first performs unrelated unguarded initialization.
                if (!conditional_) conditional_ = std::make_unique<Information>(info);
                else *conditional_ = info;
                conditional_->travel = {conditional_distance, this}; conditional_current = true;
            }
            if (!archipelago::budget(info.work_budget)) { out.evaluated_count = n; out.budget_exhausted = true; break; }
            if (!first_) first_ = std::make_unique<Forecast>();
            if (!repaired_) repaired_ = std::make_unique<Forecast>();
            // Simulation initializes and reads only group_count entries.
            if (!checkpoint_) checkpoint_.reset(new SimulationState);
            quoting_ = &cache;
            // Both arms compute the same conditional path. Only influence is disabled.
            simulate_first_into(*conditional_, menu.jobs[n], menu, *first_, *checkpoint_);
            if (first_->budget_exhausted) { out.evaluated_count = n; out.budget_exhausted = true; break; }
            if (!continue_from_into(info, menu, *first_, *checkpoint_, *repaired_)) { ++evidence_.unsupported_jobs; continue; }
            if (repaired_->budget_exhausted) { out.evaluated_count = n; out.budget_exhausted = true; break; }
            if (enabled_ || n == evidence_.attempted) {
                out.forecasts[n] = *repaired_; ++evidence_.applied_jobs;
                evidence_.conditional_unreachable += unreachable; evidence_.alternate_routes += alternate;
            }
        }
        batch_budget_exhausted_ = out.budget_exhausted;
        Menu completed = menu; completed.count = out.evaluated_count; out.chosen = choose(info, completed, out.forecasts);
    }
    void predict_into(const Information& info, const Job& job, const Menu& menu, Forecast& out) { plan4::predict_into(info, job, menu, out); }
    ModelEvidence model_evidence() const {
        // Reasons: 1 missing current frame; 2 graph unfinished; 3 bounded route/
        // composition missing; 4 repair unfinished; 5 ordinary budget failure.
        ModelEvidence value;
        value.reported = true;
        value.reason = !batch_current_ ? 1 : !graph_started_ || !graph_->complete() ? 2 : batch_budget_exhausted_ ? 5 :
            evidence_.unsupported_jobs ? 3 : evidence_.pending_repairs ? 4 : 0;
        value.supported = value.reason == 0;
        value.coupling_enabled = enabled_ && evidence_.applied_jobs > 0;
        value.samples = evidence_.occupancy_queries;
        value.unknown = evidence_.uncertain_risks + evidence_.unsupported_jobs + evidence_.pending_repairs;
        value.paths = evidence_.applied_jobs; value.work = used_; value.revision = graph_ ? graph_->generation() : 0;
        return value;
    }
    void set_enabled(bool enabled) { enabled_ = enabled; }
    const Evidence& evidence() const { return evidence_; }
};
} // namespace plan4::runtime
