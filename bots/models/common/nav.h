#pragma once

#include "pure.h"
#include "model-runtime.h"
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace plan4 {

// All navigation admission margins derive from the same phase allowance.
// In particular, the startup margin must fit inside the caller's allowance.
struct NavigationPhaseBudget {
    std::uint64_t floor = 50000;
    unsigned allowance = 15000;
    void begin(std::uint64_t before, std::uint64_t common_floor, unsigned phase_allowance = 15000) {
        allowance = std::max(1u, phase_allowance);
        floor = std::max(std::uint64_t(50000), common_floor);
        if (before > allowance) floor = std::max(floor, before - allowance);
    }
    unsigned atomic_margin() const { return std::max(5000u, allowance / 16); }
    unsigned startup_margin() const { return std::max(6000u, allowance / 4); }
    unsigned ordering_margin() const { return std::max(6000u, unsigned(std::uint64_t(allowance) * 3 / 8)); }
    bool permits(std::uint64_t remaining, unsigned margin) const {
        return remaining >= floor && remaining - floor >= margin;
    }
};

// Geometry retained across turns contains no observation, stock, or occupancy.
struct PublicRouteLayout {
    struct GroupEndpoint { Point position{}; };
    struct AltarEndpoint { Point position{}; bool accessible = false; };
    std::array<GroupEndpoint, max_groups> groups{};
    std::array<AltarEndpoint, max_altars> altars{};
    int group_count = 0, altar_count = 0;
};

// Public terrain only. A discovered BFS distance is a completed shortest-path
// query; unfinished queries return unknown. Occupants never enter this cache.
class BoundedNavigation {
    struct Field {
        Point target{};
        std::unique_ptr<int[]> distances, queue;
        std::unique_ptr<unsigned char[]> seen;
        std::size_t head = 0, tail = 0, cleared_bytes = 0;
        unsigned next_neighbor = 0;
        unsigned used = 0;
        bool reserved = false, active = false;
        bool contains(int key) const { return (seen[key / 8] & (1u << (key % 8))) != 0; }
        void add(int key, int distance) {
            seen[key / 8] |= static_cast<unsigned char>(1u << (key % 8));
            distances[key] = distance; queue[tail++] = key;
        }
    };
    int width_ = 0, height_ = 0;
    std::size_t cell_count_ = 0;
    std::vector<unsigned char> blocked_;
    std::unique_ptr<unsigned char[]> local_blocked_;
    bool (*terrain_blocked_)(Point, const void*) = nullptr;
    const void* terrain_context_ = nullptr;
    mutable std::array<Field, 16> fields_{};
    // Targets never move or evict. Only fully seeded active fields enter this
    // fixed index; zero means empty and other entries store the field slot + 1.
    mutable std::array<unsigned char, 32> active_targets_{};
    struct CompletedPair { std::uint64_t key; int distance; };
    mutable std::array<CompletedPair, 256> completed_;
    mutable std::array<unsigned char, 32> completed_valid_{};
    mutable std::array<unsigned char, 64> replacement_{};
    mutable std::size_t completed_count_ = 0;
    mutable unsigned generation_ = 0;
    mutable int visits_ = 0;
    mutable unsigned request_cursor_ = 0;
    bool sealed_ = false;
    int vertex_limit_ = 256;
    bool (*fuel_available_)() = nullptr;
    bool (*contextual_fuel_)(const void*) = nullptr;
    const void* contextual_fuel_context_ = nullptr;

    int index(Point p) const { return p.y * width_ + p.x; }
    unsigned target_bucket(Point p) const {
        return (unsigned(index(p)) * 2654435761u) >> 27;
    }
    int target_field(Point p) const {
        unsigned bucket = target_bucket(p);
        for (unsigned probe = 0; probe < active_targets_.size(); ++probe) {
            const unsigned entry = active_targets_[bucket];
            if (!entry) return -1;
            const auto& field = fields_[entry - 1];
            if (field.active && field.target == p) return int(entry - 1);
            bucket = (bucket + 1) & 31u;
        }
        return -1;
    }
    void publish_target(unsigned slot) const {
        unsigned bucket = target_bucket(fields_[slot].target);
        for (unsigned probe = 0; probe < active_targets_.size(); ++probe) {
            auto& entry = active_targets_[bucket];
            if (!entry || entry == slot + 1) {
                entry = static_cast<unsigned char>(slot + 1);
                return;
            }
            bucket = (bucket + 1) & 31u;
        }
    }
    std::uint64_t pair_key(Point a, Point b) const {
        unsigned first = unsigned(index(a)), second = unsigned(index(b));
        if (first > second) std::swap(first, second);
        return (std::uint64_t(first) << 32) | second;
    }
    static unsigned pair_bucket(std::uint64_t key) {
        key ^= key >> 33; key *= 0xff51afd7ed558ccdULL;
        key ^= key >> 33; key *= 0xc4ceb9fe1a85ec53ULL;
        return unsigned(key ^ (key >> 33)) & 63u;
    }
    bool pair_valid(unsigned slot) const { return (completed_valid_[slot / 8] & (1u << (slot % 8))) != 0; }
    int pair_distance(std::uint64_t key) const {
        const unsigned base = pair_bucket(key) * 4;
        for (unsigned slot = base; slot < base + 4; ++slot)
            if (pair_valid(slot) && completed_[slot].key == key) return completed_[slot].distance;
        return unreachable;
    }
    void remember(Point a, Point b, int result) const {
        const auto key = pair_key(a, b);
        const unsigned bucket = pair_bucket(key), base = bucket * 4;
        for (unsigned slot = base; slot < base + 4; ++slot) {
            if (pair_valid(slot) && completed_[slot].key == key) return;
            if (!pair_valid(slot)) {
                completed_[slot] = {key, result};
                completed_valid_[slot / 8] |= static_cast<unsigned char>(1u << (slot % 8));
                ++completed_count_; return;
            }
        }
        const unsigned slot = base + replacement_[bucket];
        replacement_[bucket] = static_cast<unsigned char>((replacement_[bucket] + 1) % 4);
        completed_[slot] = {key, result};
    }
    bool fuel_available() const {
        return !sealed_ && (!fuel_available_ || fuel_available_())
            && (!contextual_fuel_ || contextual_fuel_(contextual_fuel_context_));
    }
    bool available() const {
        return visits_ < vertex_limit_ && fuel_available();
    }
    void advance(Field& f, int allowance) const {
        const int stop = std::min(vertex_limit_, visits_ + allowance);
        while (f.head < f.tail && visits_ < stop && available()) {
            const int current = f.queue[f.head];
            ++visits_;
            const Point p{current % width_, current / width_};
            while (f.next_neighbor < 9) {
                const int dx = int(f.next_neighbor % 3) - 1;
                const int dy = int(f.next_neighbor / 3) - 1;
                if (!dx && !dy) { ++f.next_neighbor; continue; }
                if (terrain_blocked_ && !fuel_available()) return;
                const Point n{p.x + dx, p.y + dy};
                if (!walkable(n)) { ++f.next_neighbor; continue; }
                const int key = index(n);
                if (terrain_blocked_ && !fuel_available()) return;
                if (!f.contains(key)) f.add(key, f.distances[current] + 1);
                ++f.next_neighbor;
            }
            f.next_neighbor = 0; ++f.head;
        }
    }
    static int callback(Point a, Point b, const void* context) {
        return static_cast<const BoundedNavigation*>(context)->cached_distance(a, b);
    }
    static int prefix_callback(Point a,Point b,Point* out,int capacity,const void* context) {
        const auto& owner=*static_cast<const BoundedNavigation*>(context);
        return owner.route_prefix(a,b,out,capacity,{owner.contextual_fuel_,owner.contextual_fuel_context_});
    }

public:
    void initialize(int width, int height, std::vector<unsigned char> blocked,
                    bool (*fuel_available)() = nullptr,
                    bool (*terrain_blocked)(Point, const void*) = nullptr,
                    const void* terrain_context = nullptr) {
        width_ = width; height_ = height; blocked_ = std::move(blocked);
        local_blocked_.reset();
        cell_count_ = std::size_t(width) * std::size_t(height);
        fuel_available_ = fuel_available;
        contextual_fuel_ = nullptr; contextual_fuel_context_ = nullptr;
        terrain_blocked_ = terrain_blocked; terrain_context_ = terrain_context;
        fields_ = {}; generation_ = 0; visits_ = 0; request_cursor_ = 0;
        active_targets_.fill(0);
        completed_valid_.fill(0); replacement_.fill(0); completed_count_ = 0;
    }
    void initialize_local(int width, int height, std::unique_ptr<unsigned char[]> blocked) {
        initialize(width, height, {});
        local_blocked_ = std::move(blocked);
    }
    bool walkable(Point p) const {
        if (p.x < 0 || p.y < 0 || p.x >= width_ || p.y >= height_) return false;
        if (local_blocked_) return !local_blocked_[index(p)];
        return terrain_blocked_ ? !terrain_blocked_(p, terrain_context_)
            : index(p) < int(blocked_.size()) && !blocked_[index(p)];
    }
    void begin_turn() { visits_ = 0; sealed_ = false; }
    void seal() { sealed_ = true; }
    void set_vertex_limit(int limit) { vertex_limit_ = std::max(0, std::min(256, limit)); }
    void set_fuel_budget(bool (*callback)(const void*), const void* context) {
        contextual_fuel_ = callback; contextual_fuel_context_ = context;
    }
    int vertex_visits() const { return visits_; }
    std::size_t completed_pairs() const { return completed_count_; }
    std::size_t completed_route_entries() const {
        std::size_t count = completed_count_;
        for (const auto& field : fields_) if (field.active && field.tail)
            count += field.tail - 1; // individual target-to-vertex routes
        return count; // diagnostic entries, not a count of unique endpoint pairs
    }
    int pending_fields() const {
        int count = 0;
        for (const auto& field : fields_) count += field.active ? field.head < field.tail : field.reserved;
        return count;
    }
    TravelLookup lookup() const { return {callback, this, prefix_callback, true}; }
    // Read an already-proved route; never expand a frontier or charge vertices.
    // Output contains up to capacity adjacent destinations, excluding origin.
    int route_prefix(Point a, Point b, Point* output, int capacity, const WorkBudget& work = {}) const {
        if (!output || capacity < 0 || capacity > 256 || !walkable(a) || !walkable(b)) return -1;
        if (a == b || !capacity) return 0;
        const Field* field = nullptr;
        for (const auto& candidate : fields_) if (candidate.active && candidate.target == b && candidate.contains(index(a))) {
            field = &candidate; break;
        }
        if (field) {
            const int count = std::min(capacity, field->distances[index(a)]);
            for (int n = 0; n < count; ++n) {
                if (work.can_refine && !work.can_refine(work.context)) return -1;
                Point best{}; bool found = false;
                for (int dx = -1; dx <= 1; ++dx) for (int dy = -1; dy <= 1; ++dy) {
                    if (!dx && !dy) continue;
                    const Point next{a.x + dx, a.y + dy};
                    if (next.x < 0 || next.y < 0 || next.x >= width_ || next.y >= height_) continue;
                    const int key = index(next);
                    if (field->contains(key) && field->distances[key] == field->distances[index(a)] - 1 &&
                        (!found || point_less(next, best))) { best = next; found = true; }
                }
                if (!found) return -1;
                output[n] = a = best;
            }
            return count;
        }
        const int lower = std::max(std::abs(a.x - b.x), std::abs(a.y - b.y));
        if (lower > 8 || cached_distance(a, b) != lower) return -1;
        Point next = a;
        for (int n = 0; n < lower; ++n) {
            if (work.can_refine && !work.can_refine(work.context)) return -1;
            next.x += (b.x > next.x) - (b.x < next.x);
            next.y += (b.y > next.y) - (b.y < next.y);
            if (!walkable(next)) return -1;
            if (n < capacity) output[n] = next;
        }
        return std::min(capacity, lower);
    }

    template<class Geometry>
    void prepare_geometry(const Geometry& information, Point own) const {
        const unsigned altars = unsigned(std::max(0, std::min(max_altars, information.altar_count)));
        const unsigned groups = unsigned(std::max(0, std::min(max_groups, information.group_count)));
        const unsigned count = altars + groups * (altars + 1);
        if (!count) return;
        request_cursor_ %= count;
        for (unsigned attempted = 0; attempted < count && available(); ++attempted) {
            const unsigned request = request_cursor_;
            // Advance before work: one long request cannot monopolize the next
            // phase when its remaining route is still unresolved.
            request_cursor_ = (request_cursor_ + 1) % count;
            if (request < altars) {
                const auto& altar = information.altars[request];
                if (altar.accessible) distance(own, altar.position);
                continue;
            }
            const unsigned relative = request - altars, group = relative / (altars + 1);
            const unsigned leg = relative % (altars + 1);
            if (!leg) distance(own, information.groups[group].position);
            else {
                const auto& altar = information.altars[leg - 1];
                if (altar.accessible) distance(information.groups[group].position, altar.position);
            }
        }
    }
    void prepare(const Information& information) const {
        prepare_geometry(information, information.own.position);
    }

    int cached_distance(Point a, Point b) const {
        // Bounds are local. The optional terrain adapter may call the SDK, so
        // it is deliberately absent from this read-only forecast callback.
        if (a.x < 0 || a.y < 0 || b.x < 0 || b.y < 0
            || a.x >= width_ || b.x >= width_ || a.y >= height_ || b.y >= height_)
            return unreachable;
        if (a == b) return 0;
        const int pair = pair_distance(pair_key(a, b));
        if (pair >= 0) return pair;
        int candidates[2] = {target_field(a), target_field(b)};
        if (candidates[0] < 0 || (candidates[1] >= 0 && candidates[1] < candidates[0]))
            std::swap(candidates[0], candidates[1]);
        for (const int slot : candidates) if (slot >= 0) {
            const auto& field = fields_[slot];
            const int key = index(field.target == a ? b : a);
            if (field.contains(key)) return field.distances[key];
        }
        return unreachable;
    }

    int distance(Point a, Point b) const {
        const int cached = cached_distance(a, b);
        if (cached >= 0) return cached;
        if (!available() || !walkable(a)) return unreachable;
        if (!available() || !walkable(b)) return unreachable;
        for (auto& f : fields_) if (f.active && (f.target == a || f.target == b)) {
            f.used = ++generation_;
            const int key = index(f.target == a ? b : a);
            if (!f.contains(key)) advance(f, 32);
            return f.contains(key) ? f.distances[key] : unreachable;
        }
        Field* selected = nullptr;
        for (auto& f : fields_) if (f.reserved && !f.active && (f.target == a || f.target == b)) {
            selected = &f; break;
        }
        if (!selected) {
            // Any unobstructed Chebyshev-length ordinary route is already shortest.
            Point p = a;
            int direct = 0;
            bool obstruction = false;
            const int lower = std::max(std::abs(a.x - b.x), std::abs(a.y - b.y));
            // A long shortcut must not consume every phase before a persistent
            // field can be seeded. Long requests go directly to resumable BFS.
            while (lower <= 8 && p != b && available()) {
                p.x += (b.x > p.x) - (b.x < p.x);
                p.y += (b.y > p.y) - (b.y < p.y);
                ++visits_; ++direct;
                if (!walkable(p)) { obstruction = true; break; }
            }
            if (p == b && !obstruction && fuel_available()) { remember(a, b, direct); return direct; }
            if (!available()) return unreachable;
            for (auto& f : fields_) {
                if (!f.reserved) { selected = &f; break; }
            }
            // No eviction: unfinished initialization already owns its slot.
            if (!selected) return unreachable;
            selected->target = b; selected->reserved = true;
        }
        auto& f = *selected;
        // Allocate without value-initializing n distances/queue entries. A field
        // is published only after its bitmap is completely cleared and seeded.
        if (!f.distances) {
            if (!fuel_available()) return unreachable;
            f.distances.reset(new int[cell_count_]);
        }
        if (!f.queue) {
            if (!fuel_available()) return unreachable;
            f.queue.reset(new int[cell_count_]);
        }
        const std::size_t bitmap_bytes = (cell_count_ + 7) / 8;
        if (!f.seen) {
            if (!fuel_available()) return unreachable;
            f.seen.reset(new unsigned char[bitmap_bytes]);
        }
        while (f.cleared_bytes < bitmap_bytes) {
            if (!fuel_available()) return unreachable;
            const auto bytes = std::min(std::size_t(32), bitmap_bytes - f.cleared_bytes);
            std::fill_n(f.seen.get() + f.cleared_bytes, bytes, static_cast<unsigned char>(0));
            f.cleared_bytes += bytes;
        }
        if (!fuel_available()) return unreachable;
        f.head = f.tail = 0;
        f.next_neighbor = 0;
        f.add(index(f.target), 0); // A reversed query resumes the stored target.
        publish_target(unsigned(&f - fields_.data()));
        f.active = true; f.used = ++generation_;
        advance(f, 32);
        const int key = index(f.target == a ? b : a);
        return f.contains(key) ? f.distances[key] : unreachable;
    }
};

} // namespace plan4

#ifndef MODEL4_OBSERVER_NO_SDK
#include "cambc.h"
namespace plan4 {
class PublicNavigation {
    BoundedNavigation navigation_;
    cambc::bitboard blocked_{cambc::uninitialized};
    bool initialized_ = false;
    bool topology_loaded_ = false;
    int source_round_ = -1;
    int width_ = 0, height_ = 0;
    std::unique_ptr<unsigned char[]> local_topology_;
    std::vector<cambc::vec2> blocked_positions_;
    std::size_t cleared_topology_ = 0, copied_positions_ = 0;
    PublicRouteLayout route_layout_;
    bool routes_remembered_ = false;
    int vertex_limit_ = 192;
    NavigationPhaseBudget budget_;
    bool has_fuel(unsigned margin = 0) const {
        const auto fuel = cambc::fuel_remaining();
        return source_round_ >= 0 && int(cambc::round_number()) == source_round_
            && budget_.permits(fuel, margin ? margin : budget_.atomic_margin());
    }
    static bool fuel_budget(const void* context) {
        return static_cast<const PublicNavigation*>(context)->has_fuel();
    }
    void initialize() {
        if (!initialized_) {
            if (!topology_loaded_) {
                if (!has_fuel(budget_.startup_margin())) return;
                width_ = cambc::map::width(); height_ = cambc::map::height();
                blocked_ = cambc::map::walls() | cambc::map::holes()
                    | cambc::map::altars() | cambc::map::spawn_region(cambc::team::enemy);
                blocked_positions_ = blocked_.positions();
                topology_loaded_ = true;
            }
            const auto size = std::size_t(width_) * std::size_t(height_);
            if (!local_topology_) {
                if (!has_fuel()) return;
                local_topology_.reset(new unsigned char[size]);
            }
            while (cleared_topology_ < size) {
                if (!has_fuel()) return;
                const auto chunk = std::min(std::size_t(256), size - cleared_topology_);
                std::fill_n(local_topology_.get() + cleared_topology_, chunk, static_cast<unsigned char>(0));
                cleared_topology_ += chunk;
            }
            while (copied_positions_ < blocked_positions_.size()) {
                if (!has_fuel()) return;
                const auto end = std::min(copied_positions_ + 16, blocked_positions_.size());
                for (; copied_positions_ < end; ++copied_positions_) {
                    const auto p = blocked_positions_[copied_positions_];
                    local_topology_[std::size_t(p.y * width_ + p.x)] = 1;
                }
            }
            if (!has_fuel()) return;
            navigation_.initialize_local(width_, height_, std::move(local_topology_));
            navigation_.set_fuel_budget(fuel_budget, this);
            navigation_.set_vertex_limit(vertex_limit_); // preserve the model's share of the 192 public vertices
            initialized_ = true;
        }
    }
public:
    void begin_turn() {
        navigation_.begin_turn();
        source_round_ = int(cambc::round_number());
    }
    TravelLookup lookup() const {
        return {[](Point a,Point b,const void* context) {
            return static_cast<const PublicNavigation*>(context)->navigation_.cached_distance(a,b);
        },this,route_prefix,true};
    }
    void set_vertex_limit(int limit) {
        vertex_limit_ = std::max(0, std::min(192, limit));
        navigation_.set_vertex_limit(vertex_limit_);
    }
    runtime::PublicTopology topology() const {
        return {width_, height_, initialized_, [](Point p, const void* context) {
            const auto& owner = *static_cast<const PublicNavigation*>(context);
            return owner.initialized_ && owner.navigation_.walkable(p);
        }, this};
    }
    static int route_prefix(Point a, Point b, Point* output, int capacity, const void* context) {
        const auto& owner = *static_cast<const PublicNavigation*>(context);
        struct Guard { int round; } guard{owner.source_round_};
        const WorkBudget work{[](const void* value) {
            const auto round = static_cast<const Guard*>(value)->round;
            return round >= 0 && cambc::fuel_remaining() >= 45000 && int(cambc::round_number()) == round;
        }, &guard};
        return owner.initialized_ && work.can_refine(work.context)
            ? owner.navigation_.route_prefix(a, b, output, capacity, work) : -1;
    }
    bool remember_public_routes(const Information& information) {
        if (information.public_layout_truncated || information.group_count < 0 || information.group_count > max_groups
            || information.altar_count < 0 || information.altar_count > max_altars) return false;
        if (routes_remembered_) return true; // Map endpoints are immutable for this match.
        route_layout_.group_count = information.group_count;
        route_layout_.altar_count = information.altar_count;
        for (int g = 0; g < information.group_count; ++g)
            route_layout_.groups[g].position = information.groups[g].position;
        for (int a = 0; a < information.altar_count; ++a) {
            route_layout_.altars[a].position = information.altars[a].position;
            route_layout_.altars[a].accessible = information.altars[a].accessible;
        }
        routes_remembered_ = true;
        return true;
    }
    void warm(int round, Point own, unsigned fuel_floor = 50000, unsigned phase_allowance = 15000) {
        if (round != source_round_ || int(cambc::round_number()) != source_round_) return;
        budget_.begin(cambc::fuel_remaining(), fuel_floor, phase_allowance);
        if (!has_fuel(budget_.startup_margin())) return;
        if (!initialized_) initialize();
        if (!initialized_ || !has_fuel()) return;
        if (routes_remembered_) navigation_.prepare_geometry(route_layout_, own);
    }
    void prepare(const Information& information, unsigned fuel_floor = 50000, unsigned phase_allowance = 15000) {
        // Resolve geometry outside predict(). Its callback only reads complete
        // distances and never queries SDK fuel or mutates a search frontier.
        if (information.round != source_round_ || int(cambc::round_number()) != source_round_) return;
        remember_public_routes(information);
        budget_.begin(cambc::fuel_remaining(), fuel_floor, phase_allowance);
        if (!has_fuel(budget_.startup_margin())) return;
        if (!initialized_) initialize();
        if (!initialized_ || !has_fuel()) return;
        navigation_.prepare(information);
    }
    int vertex_visits() const { return navigation_.vertex_visits(); }
    int visits() const { return navigation_.vertex_visits(); }
    bool initialized() const { return initialized_; }
    std::size_t completed_pairs() const { return navigation_.completed_pairs(); }
    std::size_t completed_route_entries() const { return navigation_.completed_route_entries(); }
    int pending_fields() const { return navigation_.pending_fields(); }
    void seal() { navigation_.seal(); }
};
} // namespace plan4
#endif
