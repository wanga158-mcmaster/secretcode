#pragma once
// SDK-free conditional graph compilation. Dated occupancy is a routing hint:
// no endpoint, path, field or generation is current movement authority.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>

namespace mosaic::service_route {
struct Point { int x, y; };
constexpr bool operator==(Point a, Point b) { return a.x == b.x && a.y == b.y; }
using Tile = std::uint32_t;
constexpr std::uint32_t unreachable = std::numeric_limits<std::uint32_t>::max();
constexpr Tile no_tile = std::numeric_limits<Tile>::max();
constexpr std::uint8_t not_local = std::numeric_limits<std::uint8_t>::max();
constexpr std::size_t max_cells = 10000, family_capacity = 81;
enum class Status { Idle, Working, Ready, UnreachableInModel, Unknown };
enum class Reason {
    None, Dimensions, Origin, Speed, PackedMaskLength, WorkspaceCapacity, Goal,
    OriginStaticallyBlocked, DatedOriginBlocked, SemanticGeneration, LocalGenerationExhausted
};
enum class Phase { Init, Goals, ReverseField, LocalReachability, Candidates, Order, Finished };
struct Input {
    int width{}, height{}; Point origin{}; unsigned speed{};
    // Both spans are exactly ceil(width*height/8) bytes, packed continuously
    // row-major, LSB first. Row boundaries do not pad; tail bits are ignored.
    std::span<const std::uint8_t> static_quiet_blocked{}, dated_observed_blocked{};
    std::span<const Point> goals{};
    std::uint64_t semantic_generation{}, occupancy_epoch{};
};
struct Node {
    std::uint32_t potential, local_generation; std::uint8_t local_steps;
    bool static_blocked, observed_blocked;
    bool blocked() const { return static_blocked || observed_blocked; }
};
struct Candidate {
    Point endpoint; std::uint32_t potential;
    std::uint8_t walk_steps; bool decreases;
};
static_assert(std::is_trivial_v<Point> && std::is_trivial_v<Node> && std::is_trivial_v<Candidate>,
              "Workspace arrays must not run per-element constructors before advance.");
struct Workspace {
    std::span<Node> nodes{}; std::span<Tile> queue{}; std::span<Candidate> candidates{};
};
struct Cell {
    bool static_blocked{}, dated_observed_blocked{};
    std::uint32_t potential{unreachable};
};
struct ConditionalFamily {
    std::span<const Candidate> entries{}; bool complete{};
    Point origin{}, graph_body_exclusion{}; unsigned speed{};
    std::uint32_t projection_generation{};
    std::uint64_t semantic_generation{}, source_occupancy_epoch{};
    // Complete only means all non-origin endpoints reachable in <=speed steps
    // of THIS frozen graph were considered. Unknown/unseen occupancy may refuse
    // a move. Nondecreasing entries need their own parking/refinement contract.
    // Borrowed entries are invalidated by a changed reposition and subsequent
    // advance work. Never retain this span across projections; bind consumers to
    // projection_generation as well as semantic_generation and origin.
};
struct Snapshot {
    Status status{}; Reason reason{}; Phase phase{};
    std::uint64_t work_units{}, global_work_units{}, semantic_generation{}, source_occupancy_epoch{};
    std::uint32_t local_generation{};
    std::size_t initialized_cells{}, goals_processed{}, accepted_goals{}, blocked_goals{};
    std::size_t reverse_discovered{}, local_discovered{}, candidate_count{}, decreasing_count{};
    bool field_complete{}, at_goal{};
    std::uint32_t origin_potential{unreachable};
};

// One compiler and caller-owned workspace bind to ONE immutable graph/origin.
// The caller must retain all borrowed spans unchanged while this object lives.
// A different capture epoch/round is not a restart trigger. New graph/goals
// require a new compiler. Explicit reposition changes only the local projection
// on the SAME dated graph; it never clears a new origin's observed blocker.
// No dated field/candidate becomes a live certificate.
// advance(q) performs <=q units. A unit initializes/decodes one cell, processes
// one goal, pops one BFS node, examines one neighbor, emits one candidate, or
// makes one ordering comparison+swap. Every unit has O(1) bounded pure work.
class Compiler {
public:
    Compiler() = default;
    Compiler(const Compiler&) = delete;
    Compiler& operator=(const Compiler&) = delete;
    bool begin(const Input& input, Workspace workspace) {
        if (bound_) return false;
        bound_ = true; input_ = input; workspace_ = workspace;
        if (input_.width < 1 || input_.height < 1 || input_.width > 100 || input_.height > 100)
            return fail(Reason::Dimensions);
        cells_ = static_cast<std::size_t>(input_.width) * input_.height;
        if (!inside(input_.origin)) return fail(Reason::Origin);
        if (input_.speed > 4) return fail(Reason::Speed);
        const auto bytes = (cells_ + 7) / 8;
        if (input_.static_quiet_blocked.size() != bytes || input_.dated_observed_blocked.size() != bytes)
            return fail(Reason::PackedMaskLength);
        if (workspace_.nodes.size() < cells_ || workspace_.queue.size() < cells_ ||
            workspace_.candidates.size() < family_capacity)
            return fail(Reason::WorkspaceCapacity);
        if (input_.goals.size() > cells_) return fail(Reason::Goal);
        origin_ = tile(input_.origin); current_speed_ = input_.speed; status_ = Status::Working; return true;
    }
    Status advance(std::size_t quota) {
        while (quota-- && status_ == Status::Working) { ++work_units_; unit(); }
        return status_;
    }
    // Explicit O(1) projection admission, followed by quota-bounded local work.
    // Calling before a finished projection returns false without resetting it.
    // A changed/blocked origin is no physical impossibility claim: recapture and
    // a new graph may be needed. Only the ORIGINAL body bit was ever excluded.
    bool reposition(Point new_origin, unsigned speed, std::uint64_t semantic_generation) {
        if (!field_complete_ || (status_ != Status::Ready && status_ != Status::UnreachableInModel)) return false;
        if (semantic_generation != input_.semantic_generation) return fail(Reason::SemanticGeneration);
        if (!inside(new_origin)) return fail(Reason::Origin);
        if (speed > 4) return fail(Reason::Speed);
        const auto next = tile(new_origin); const auto& node = workspace_.nodes[next];
        if (node.static_blocked) return fail(Reason::OriginStaticallyBlocked);
        if (node.observed_blocked) return fail(Reason::DatedOriginBlocked);
        if (next == origin_ && speed == current_speed_) return true;
        if (local_generation_ == std::numeric_limits<std::uint32_t>::max()) return fail(Reason::LocalGenerationExhausted);
        ++local_generation_; origin_ = next; current_speed_ = speed; start_local(); return true;
    }
    bool same_semantics(std::uint64_t generation, Point origin, unsigned speed) const {
        return bound_ && input_.semantic_generation == generation && point(origin_) == origin && current_speed_ == speed;
    }
    Snapshot snapshot() const {
        const auto potential = field_complete_ ? workspace_.nodes[origin_].potential : unreachable;
        return {status_, reason_, phase_, work_units_, global_work_units_, input_.semantic_generation, input_.occupancy_epoch, local_generation_,
                initialized_, goals_processed_, accepted_goals_, blocked_goals_,
                phase_ == Phase::ReverseField ? tail_ : reverse_discovered_,
                phase_ == Phase::LocalReachability ? tail_ : local_discovered_, candidate_count_, decreasing_count_, field_complete_,
                field_complete_ && potential == 0, potential};
    }
    std::optional<Cell> cell(Point point) const {
        if (!field_complete_ || !inside(point)) return std::nullopt;
        const auto& node = workspace_.nodes[tile(point)];
        return Cell{node.static_blocked, node.observed_blocked, node.potential};
    }
    ConditionalFamily family() const {
        const bool complete = status_ == Status::Ready || status_ == Status::UnreachableInModel;
        return {complete ? std::span<const Candidate>(workspace_.candidates.first(candidate_count_)) : std::span<const Candidate>{},
                complete, point(origin_), input_.origin, current_speed_, local_generation_, input_.semantic_generation, input_.occupancy_epoch};
    }
private:
    Input input_{}; Workspace workspace_{};
    Status status_{Status::Idle}; Reason reason_{Reason::None}; Phase phase_{Phase::Init};
    bool bound_{}, field_complete_{}; Tile origin_{}, active_{no_tile};
    unsigned current_speed_{}; std::uint32_t local_generation_{};
    std::size_t cells_{}, initialized_{}, goals_processed_{}, accepted_goals_{}, blocked_goals_{};
    std::size_t head_{}, tail_{}, neighbor_{}, reverse_discovered_{}, local_discovered_{};
    std::size_t candidate_scan_{}, candidate_count_{}, decreasing_count_{}, order_i_{1}, order_j_{1};
    std::uint64_t work_units_{}, global_work_units_{};
    bool fail(Reason reason) { status_ = Status::Unknown; reason_ = reason; return false; }
    bool inside(Point point) const { return point.x >= 0 && point.y >= 0 && point.x < input_.width && point.y < input_.height; }
    Tile tile(Point point) const { return static_cast<Tile>(point.y * input_.width + point.x); }
    Point point(Tile value) const {
        if (input_.width <= 0) return input_.origin; // Empty/error views remain safe.
        return {static_cast<int>(value % input_.width), static_cast<int>(value / input_.width)};
    }
    bool packed(std::span<const std::uint8_t> bytes, Tile value) const { return (bytes[value >> 3] & (1u << (value & 7))) != 0; }
    bool less(const Candidate& a, const Candidate& b) const {
        if (a.decreases != b.decreases) return a.decreases;
        if (a.potential != b.potential) return a.potential < b.potential;
        if (a.walk_steps != b.walk_steps) return a.walk_steps < b.walk_steps;
        return tile(a.endpoint) < tile(b.endpoint);
    }
    void start_local() {
        head_ = 0; tail_ = 1; active_ = no_tile; neighbor_ = 0;
        workspace_.queue[0] = origin_; auto& origin = workspace_.nodes[origin_];
        origin.local_generation = local_generation_; origin.local_steps = 0;
        local_discovered_ = candidate_scan_ = candidate_count_ = decreasing_count_ = 0;
        order_i_ = order_j_ = 1; phase_ = Phase::LocalReachability; status_ = Status::Working;
    }
    void finish() {
        phase_ = Phase::Finished;
        // This says nothing about physical impossibility: the frozen graph is
        // conservative, dated, and deliberately restricted to the quiet class.
        status_ = workspace_.nodes[origin_].potential == unreachable ? Status::UnreachableInModel : Status::Ready;
    }
    void bfs_neighbor(bool local) {
        const int dx = static_cast<int>(neighbor_ % 3) - 1, dy = static_cast<int>(neighbor_ / 3) - 1;
        const auto from = point(active_); const Point next{from.x + dx, from.y + dy};
        if ((dx || dy) && inside(next)) {
            const auto next_tile = tile(next); auto& to = workspace_.nodes[next_tile]; const auto& old = workspace_.nodes[active_];
            if (!to.blocked()) {
                if (local && to.local_generation != local_generation_) {
                    to.local_generation = local_generation_;
                    to.local_steps = static_cast<std::uint8_t>(old.local_steps + 1); workspace_.queue[tail_++] = next_tile;
                } else if (!local && to.potential == unreachable) {
                    to.potential = old.potential + 1; workspace_.queue[tail_++] = next_tile;
                }
            }
        }
        if (++neighbor_ == 9) { neighbor_ = 0; active_ = no_tile; }
    }
    void unit() {
        switch (phase_) {
        case Phase::Init: {
            auto& node = workspace_.nodes[initialized_];
            node.potential = unreachable; node.local_generation = 0; node.local_steps = not_local;
            node.static_blocked = packed(input_.static_quiet_blocked, static_cast<Tile>(initialized_));
            node.observed_blocked = initialized_ != origin_ && packed(input_.dated_observed_blocked, static_cast<Tile>(initialized_));
            if (++initialized_ == cells_) {
                if (workspace_.nodes[origin_].static_blocked) { fail(Reason::OriginStaticallyBlocked); break; }
                phase_ = Phase::Goals;
            }
            break;
        }
        case Phase::Goals:
            if (goals_processed_ == input_.goals.size()) { phase_ = Phase::ReverseField; break; }
            if (!inside(input_.goals[goals_processed_])) { fail(Reason::Goal); break; }
            {
                const auto goal = tile(input_.goals[goals_processed_++]); auto& node = workspace_.nodes[goal];
                if (node.blocked()) ++blocked_goals_;
                else if (node.potential == unreachable) { node.potential = 0; workspace_.queue[tail_++] = goal; ++accepted_goals_; }
            }
            break;
        case Phase::ReverseField:
            if (active_ != no_tile) bfs_neighbor(false);
            else if (head_ < tail_) { active_ = workspace_.queue[head_++]; neighbor_ = 0; }
            else {
                reverse_discovered_ = tail_; field_complete_ = true; global_work_units_ = work_units_;
                local_generation_ = 1; start_local();
            }
            break;
        case Phase::LocalReachability:
            if (active_ != no_tile) bfs_neighbor(true);
            else if (head_ < tail_) {
                const auto next = workspace_.queue[head_++];
                if (workspace_.nodes[next].local_steps < current_speed_) { active_ = next; neighbor_ = 0; }
            } else { local_discovered_ = tail_; phase_ = Phase::Candidates; }
            break;
        case Phase::Candidates:
            if (candidate_scan_ == tail_) { phase_ = Phase::Order; break; }
            {
                const auto endpoint = workspace_.queue[candidate_scan_++];
                if (endpoint == origin_) break;
                if (candidate_count_ >= family_capacity) { fail(Reason::WorkspaceCapacity); break; }
                const auto& node = workspace_.nodes[endpoint];
                const auto origin_potential = workspace_.nodes[origin_].potential;
                const bool decreases = origin_potential != unreachable && node.potential != unreachable && node.potential < origin_potential;
                workspace_.candidates[candidate_count_++] = {point(endpoint), node.potential, node.local_steps, decreases};
                if (decreases) ++decreasing_count_;
            }
            break;
        case Phase::Order:
            if (order_i_ >= candidate_count_) { finish(); break; }
            if (order_j_ && less(workspace_.candidates[order_j_], workspace_.candidates[order_j_ - 1])) {
                std::swap(workspace_.candidates[order_j_], workspace_.candidates[order_j_ - 1]); --order_j_;
            } else { ++order_i_; order_j_ = order_i_; }
            break;
        case Phase::Finished: break;
        }
    }
};
} // namespace mosaic::service_route
