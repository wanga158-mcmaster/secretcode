#pragma once
#include "../../common/pure.h"
#include <memory>
#include <vector>

namespace plan4::archipelago {
inline constexpr const char* version = "plan4-archipelago/1";
inline constexpr int max_vertices = 16384, max_route_points = 256, max_blockers = 32;
enum class Diagonals { eight_destination_only, unsupported };
struct Progress { int work = 0, vertex_visits = 0; bool complete = false, unsupported = false, budget_exhausted = false; };
inline bool budget(const WorkBudget& b) { return !b.can_refine || b.can_refine(b.context); }
inline Point neighbor(Point p, int direction) {
    const int slot = direction < 4 ? direction : direction + 1;
    return {p.x + slot % 3 - 1, p.y + slot / 3 - 1};
}

// Iterative Tarjan decomposition. Initialization, DFS edges and component-edge
// pops are resumable; no large component is synchronously drained at root exit.
class Graph {
    struct Frame { int vertex, parent, next, children; };
    struct Edge { int from, to; };
    int width_ = 0, height_ = 0, size_ = 0, initialized_ = 0, root_ = 0;
    int clock_ = 0, depth_ = 0, edges_ = 0, components_ = 0;
    Edge pop_until_{};
    bool popping_ = false, supported_ = false, complete_ = false;
    unsigned generation_ = 0;
    std::vector<unsigned char> blocked_;
    std::unique_ptr<int[]> discovered_, low_, component_;
    std::unique_ptr<unsigned char[]> articulation_;
    std::unique_ptr<Frame[]> stack_;
    std::unique_ptr<Edge[]> edge_stack_;
    int direction(int from, int to) const {
        const auto a = point(from), b = point(to);
        const int slot = (b.y - a.y + 1) * 3 + b.x - a.x + 1;
        return slot < 4 ? slot : slot - 1;
    }
    void discover(int vertex, int parent) {
        discovered_[vertex] = low_[vertex] = ++clock_;
        stack_[depth_++] = {vertex,parent,0,0};
    }
public:
    bool initialize(int width, int height, std::vector<unsigned char> blocked,
                    Diagonals diagonals = Diagonals::eight_destination_only) {
        ++generation_; supported_ = complete_ = popping_ = false;
        width_ = width; height_ = height; size_ = 0;
        initialized_ = root_ = clock_ = depth_ = edges_ = components_ = 0;
        if (width <= 0 || height <= 0 || std::int64_t(width) * height > max_vertices ||
            std::int64_t(width) * height != std::int64_t(blocked.size()) || diagonals != Diagonals::eight_destination_only) return false;
        size_ = width * height; blocked_ = std::move(blocked);
        discovered_.reset(new int[size_]); low_.reset(new int[size_]);
        component_.reset(new int[std::size_t(size_) * 8]);
        articulation_.reset(new unsigned char[size_]); stack_.reset(new Frame[size_]);
        edge_stack_.reset(new Edge[std::size_t(size_) * 4]);
        supported_ = true; return true;
    }
    int size() const { return size_; }
    unsigned generation() const { return generation_; }
    bool supported() const { return supported_; }
    bool complete() const { return complete_; }
    int component_count() const { return complete_ ? components_ : -1; }
    Point point(int key) const { return {key % width_,key / width_}; }
    int key(Point p) const { return p.y * width_ + p.x; }
    bool walkable(Point p) const {
        return supported_ && p.x >= 0 && p.y >= 0 && p.x < width_ && p.y < height_ && !blocked_[key(p)];
    }
    bool adjacent(Point a, Point b) const {
        return walkable(a) && walkable(b) && a != b && std::abs(a.x-b.x) <= 1 && std::abs(a.y-b.y) <= 1;
    }
    bool articulation(Point p) const { return complete_ && walkable(p) && articulation_[key(p)]; }
    int edge_component(Point a, Point b) const {
        return complete_ && adjacent(a,b) ? component_[std::size_t(key(a))*8 + direction(key(a),key(b))] : -1;
    }
    Progress advance(int allowance = 256, WorkBudget fuel = {}) {
        Progress result; result.unsupported = !supported_;
        const int limit = std::max(0,std::min(256,allowance));
        while (supported_ && !complete_ && result.work < limit) {
            if (!budget(fuel)) { result.budget_exhausted = true; break; }
            ++result.work;
            if (initialized_ < size_) {
                discovered_[initialized_] = low_[initialized_] = 0;
                articulation_[initialized_++] = 0; continue;
            }
            if (popping_) {
                const auto edge = edge_stack_[--edges_];
                component_[std::size_t(edge.from)*8 + direction(edge.from,edge.to)] = components_;
                component_[std::size_t(edge.to)*8 + direction(edge.to,edge.from)] = components_;
                if (edge.from == pop_until_.from && edge.to == pop_until_.to) { popping_ = false; ++components_; }
                continue;
            }
            if (!depth_) {
                if (root_ == size_) { complete_ = true; break; }
                const int vertex = root_++;
                if (!blocked_[vertex] && !discovered_[vertex]) { discover(vertex,-1); ++result.vertex_visits; }
                continue;
            }
            auto& frame = stack_[depth_-1];
            if (frame.next < 8) {
                const auto p = neighbor(point(frame.vertex),frame.next++);
                if (!walkable(p)) continue;
                const int next = key(p);
                if (!discovered_[next]) {
                    ++frame.children; edge_stack_[edges_++] = {frame.vertex,next};
                    discover(next,frame.vertex); ++result.vertex_visits;
                } else if (next != frame.parent && discovered_[next] < discovered_[frame.vertex]) {
                    edge_stack_[edges_++] = {frame.vertex,next};
                    low_[frame.vertex] = std::min(low_[frame.vertex],discovered_[next]);
                }
                continue;
            }
            const Frame finished = frame; --depth_;
            if (finished.parent < 0) {
                articulation_[finished.vertex] = finished.children > 1;
                if (!finished.children) ++components_; // isolated vertex block
            } else {
                auto& parent = stack_[depth_-1];
                low_[parent.vertex] = std::min(low_[parent.vertex],low_[finished.vertex]);
                if (low_[finished.vertex] >= discovered_[parent.vertex]) {
                    if (parent.parent >= 0) articulation_[parent.vertex] = 1;
                    popping_ = true; pop_until_ = {parent.vertex,finished.vertex};
                }
            }
        }
        result.complete = complete_; return result;
    }
};

enum class RouteStatus { pending, found, unreachable, unsupported };
struct Route {
    std::array<Point,max_route_points> points;
    int count = 0, distance = -1;
    const Graph* graph = nullptr;
    unsigned graph_generation = 0;
    RouteStatus status = RouteStatus::unsupported;
    bool complete_path = false;
    Route() noexcept {}
};
// The exact same ordinary BFS is used for base and repaired routes in both arms.
// Buffers are lazily initialized; every expanded/reconstructed vertex consumes
// a work unit, with at most 256 units per call. No SDK or hidden callback exists.
class RouteSearch {
    const Graph* graph_ = nullptr;
    std::unique_ptr<int[]> queue_, parent_, distance_;
    std::unique_ptr<unsigned char[]> seen_;
    std::array<int,max_blockers> avoid_;
    int avoid_count_ = 0, start_ = -1, target_ = -1, head_ = 0, tail_ = 0, building_ = -1;
    std::size_t cleared_ = 0;
    bool seeded_ = false;
    Route route_;
    bool seen(int key) const { return (seen_[key/8] & (1u << (key%8))) != 0; }
    bool excluded(int key) const {
        for (int n = 0; n < avoid_count_; ++n) if (avoid_[n] == key) return true;
        return false;
    }
    void add(int key, int parent, int distance) {
        seen_[key/8] |= static_cast<unsigned char>(1u << (key%8));
        parent_[key] = parent; distance_[key] = distance; queue_[tail_++] = key;
    }
public:
    bool begin(const Graph& graph, Point from, Point to, const Point* avoid = nullptr, int avoid_count = 0) {
        graph_ = &graph; route_.count = 0; route_.distance = -1; route_.complete_path = false;
        route_.graph = &graph;
        route_.graph_generation = graph.generation(); route_.status = RouteStatus::unsupported;
        cleared_ = 0; seeded_ = false; head_ = tail_ = 0; building_ = -1;
        if (!graph.supported() || avoid_count < 0 || avoid_count > max_blockers || (avoid_count && !avoid)) return false;
        if (!graph.walkable(from) || !graph.walkable(to)) { route_.status = RouteStatus::unreachable; return true; }
        start_ = graph.key(from); target_ = graph.key(to); avoid_count_ = avoid_count;
        for (int n = 0; n < avoid_count; ++n) {
            if (!graph.walkable(avoid[n])) return false;
            avoid_[n] = graph.key(avoid[n]);
        }
        if (excluded(start_) || excluded(target_)) { route_.status = RouteStatus::unreachable; return true; }
        queue_.reset(new int[graph.size()]); parent_.reset(new int[graph.size()]); distance_.reset(new int[graph.size()]);
        seen_.reset(new unsigned char[(graph.size()+7)/8]);
        route_.status = RouteStatus::pending; return true;
    }
    const Route& result() const { return route_; }
    Progress advance(int allowance = 256, WorkBudget fuel = {}) {
        Progress progress; progress.unsupported = route_.status == RouteStatus::unsupported;
        const int limit = std::max(0,std::min(256,allowance));
        if (!graph_ || graph_->generation() != route_.graph_generation) {
            route_.status = RouteStatus::unsupported; progress.unsupported = true; return progress;
        }
        while (route_.status == RouteStatus::pending && progress.work < limit) {
            if (!budget(fuel)) { progress.budget_exhausted = true; break; }
            ++progress.work;
            const auto bytes = std::size_t(graph_->size()+7)/8;
            if (cleared_ < bytes) {
                const auto count = std::min(std::size_t(32),bytes-cleared_);
                std::fill_n(seen_.get()+cleared_,count,static_cast<unsigned char>(0)); cleared_ += count; continue;
            }
            if (!seeded_) {
                add(start_,-1,0); seeded_ = true;
                if (start_ == target_) { building_ = start_; route_.distance = 0; }
                continue;
            }
            if (building_ >= 0) {
                ++progress.vertex_visits;
                route_.points[route_.count++] = graph_->point(building_);
                building_ = parent_[building_];
                if (building_ < 0) {
                    std::reverse(route_.points.begin(),route_.points.begin()+route_.count);
                    route_.complete_path = true; route_.status = RouteStatus::found;
                } else if (route_.count == max_route_points) {
                    route_.count = 0; route_.status = RouteStatus::found; // exact distance, unknown dependency path
                }
                continue;
            }
            if (head_ == tail_) { route_.status = RouteStatus::unreachable; break; }
            const int current = queue_[head_++]; ++progress.vertex_visits;
            for (int d = 0; d < 8; ++d) {
                const auto next = neighbor(graph_->point(current),d);
                if (!graph_->walkable(next)) continue;
                const int key = graph_->key(next);
                if (seen(key) || excluded(key)) continue;
                add(key,current,distance_[current]+1);
                if (key == target_) { building_ = key; route_.distance = distance_[key]; break; }
            }
        }
        progress.complete = route_.status != RouteStatus::pending; return progress;
    }
};

struct BlockedTile { Point position{}; bool blocked = true, personally_visible = false; Provenance provenance{}; };
struct RepairPlan {
    std::array<Point,max_blockers> avoid{};
    int avoid_count = 0, base_distance = -1;
    bool supported = false, repair = false, uncertain_risk = false, incomplete_observation = false;
};
class State {
    struct Registered {
        std::array<int,max_route_points> path, connectors;
        int count = 0, connector_count = 0, distance = -1;
        unsigned generation = 0;
        bool valid = false;
    };
    struct Evidence { Point position; bool blocked; int round; std::uint64_t epoch; };
    std::array<Registered,max_jobs> routes_;
    std::array<Evidence,max_blockers> evidence_;
    int evidence_count_ = 0, actor_ = -1, round_ = -1;
    std::uint64_t epoch_ = 0;
    bool enabled_ = true, truncated_ = false;
    static bool contains(const std::array<int,max_route_points>& keys, int count, int key) {
        for (int n = 0; n < count; ++n) if (keys[n] == key) return true;
        return false;
    }
public:
    explicit State(bool enabled = true) : enabled_(enabled) {}
    // Invalidate written prefixes without initializing or reallocating their
    // unused capacities when the immutable topology changes.
    void clear() {
        for (auto& route : routes_) { route.count = route.connector_count = 0; route.valid = false; }
        evidence_count_ = 0; actor_ = round_ = -1; epoch_ = 0; truncated_ = false;
    }
    Progress bind_route_prefix(int job, const Graph& graph, const Route& route, int& cursor,
                               int allowance = 256, WorkBudget fuel = {}) {
        Progress result;
        if (job < 0 || job >= max_jobs) { result.unsupported = true; return result; }
        auto& target = routes_[job];
        if (cursor == 0) {
            target.valid = false; target.count = target.connector_count = 0;
            target.generation = graph.generation(); target.distance = route.distance;
        }
        if (!graph.complete() || route.graph != &graph || route.graph_generation != graph.generation() || route.status != RouteStatus::found
            || !route.complete_path || route.count <= 0 || route.count > max_route_points || route.distance != route.count-1 ||
            cursor < 0 || cursor > route.count || target.count != cursor || target.generation != graph.generation() ||
            target.distance != route.distance || (cursor && target.path[cursor-1] != graph.key(route.points[cursor-1]))) {
            target.valid = false; result.unsupported = true; return result;
        }
        const int limit = std::clamp(allowance, 0, 256);
        while (cursor < route.count && result.work < limit) {
            if (!budget(fuel)) { result.budget_exhausted = true; break; }
            ++result.work; ++result.vertex_visits;
            if (!graph.walkable(route.points[cursor]) || (cursor && !graph.adjacent(route.points[cursor-1],route.points[cursor]))) {
                target.valid = false; result.unsupported = true; return result;
            }
            target.path[cursor] = graph.key(route.points[cursor]);
            if (graph.articulation(route.points[cursor])) target.connectors[target.connector_count++] = target.path[cursor];
            ++cursor; ++target.count;
        }
        if (cursor == route.count) {
            target.distance = route.distance; target.generation = graph.generation(); target.valid = true; result.complete = true;
        }
        return result;
    }
    bool bind_route(int job, const Graph& graph, const Route& route) {
        int cursor = 0;
        return bind_route_prefix(job, graph, route, cursor).complete;
    }
    bool observe(const Graph& graph, int round, int actor, const BlockedTile* samples, int count, bool truncated = false) {
        ++epoch_; truncated_ = truncated;
        if (round < 0 || actor < 0 || count < 0 || count > max_blockers || (count && !samples)) {
            truncated_ = true; return false;
        }
        if (actor != actor_ || round < round_) evidence_count_ = 0;
        actor_ = actor; round_ = round;
        for (int n = 0; n < count; ++n) {
            const auto& sample = samples[n]; const auto& p = sample.provenance;
            if (!sample.personally_visible || p.status != Status::exact_now || p.observed_round != round
                || p.source != Source::personal_vision || !graph.walkable(sample.position)) continue;
            int slot = 0;
            while (slot < evidence_count_ && evidence_[slot].position != sample.position) ++slot;
            if (slot == max_blockers) { truncated_ = true; continue; }
            if (slot == evidence_count_) ++evidence_count_;
            evidence_[slot] = {sample.position,sample.blocked,round,epoch_};
        }
        return true;
    }
    RepairPlan repair_plan(const Graph& graph, int job, int attempted_job, int round) const {
        RepairPlan result; result.incomplete_observation = truncated_;
        if (job < 0 || job >= max_jobs || !graph.complete()) return result;
        const auto& route = routes_[job];
        if (!route.valid || route.generation != graph.generation()) return result;
        result.supported = true; result.base_distance = route.distance;
        for (int n = 0; n < evidence_count_; ++n) {
            const auto& e = evidence_[n]; if (!e.blocked) continue;
            const int key = graph.key(e.position);
            const bool dependent = job == attempted_job ? contains(route.path,route.count,key)
                : enabled_ && contains(route.connectors,route.connector_count,key);
            if (!dependent) continue;
            if (e.epoch == epoch_ && round == round_) result.repair = true;
            else result.uncertain_risk = true;
        }
        // Once a route is repaired, its alternative respects every freshly
        // witnessed blocker, not just the connector that triggered repair.
        if (result.repair) for (int n = 0; n < evidence_count_; ++n) {
            const auto& e = evidence_[n];
            if (e.blocked && e.epoch == epoch_ && round == round_) result.avoid[result.avoid_count++] = e.position;
        }
        return result;
    }
};
} // namespace plan4::archipelago
