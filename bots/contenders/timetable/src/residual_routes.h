#pragma once
// Terrain-only, immutable reverse fields. The caller names the goal and seals
// Grid (including altar/enemy-spawn policy) before sharing it. Neither a field
// nor a path certifies present occupancy, damage, Life or a later SDK move.
#include "residual_types.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace residual::routes {
enum class Status {NeedCompute,Ready,Unsupported};
enum class Phase {Reserve,Initialize,Roots,Search,Done,Failed};
enum class Cost {None,Allocate,Cell,Root,Pop};
enum class Reason {None,InvalidGrid,NoRoots,Allocation};

class Builder;
class RouteField {
public:
    std::optional<std::uint32_t> distance(Point actual_origin) const noexcept;
    // Root/unreachable/outside => nullopt. Otherwise returns one adjacent cell
    // whose distance is exactly one less. It does not authorize movement.
    std::optional<Point> next(Point actual_origin) const noexcept;
    const std::shared_ptr<const Grid>& grid() const noexcept {return grid_;}
    const std::vector<Point>& roots() const noexcept {return roots_;}
private:
    friend class Builder;
    std::shared_ptr<const Grid> grid_;
    std::vector<Point> roots_;
    std::vector<std::uint32_t> distance_;
    std::vector<std::uint8_t> parent_;
    bool ready_{};
};

class Builder {
public:
    // Move an already assembled root list. No N-sized allocation, zero fill,
    // catalogue scan or BFS occurs in this constructor.
    Builder(std::shared_ptr<const Grid> grid,std::vector<Point> roots) noexcept;
    Builder(const Builder&)=delete;
    Builder& operator=(const Builder&)=delete;
    Builder(Builder&&) noexcept=default;
    Builder& operator=(Builder&&) noexcept=default;
    // One unit is ONE allocation, ONE initialized cell, ONE root, or ONE queue
    // pop with at most eight neighbors. Reserve has four explicit units: small
    // field object, distance capacity, direction capacity, queue capacity.
    // Caller must admit each Allocate unit; units do not claim a fuel bound.
    void advance(std::size_t units) noexcept;
    Status status() const noexcept {return status_;}
    Phase phase() const noexcept {return phase_;}
    Cost next_cost() const noexcept;
    bool needs_allocation() const noexcept {return next_cost()==Cost::Allocate;}
    Reason reason() const noexcept {return reason_;}
    std::size_t last_work() const noexcept {return last_work_;}
    std::size_t initialized_cells() const noexcept {return initialized_;}
    std::size_t popped_cells() const noexcept {return head_;}
    std::shared_ptr<const RouteField> result() const noexcept;
private:
    void fail(Reason reason) noexcept;
    void step();
    std::shared_ptr<const Grid> grid_;
    std::vector<Point> roots_;
    std::shared_ptr<RouteField> field_;
    std::vector<std::uint32_t> queue_;
    std::size_t cells_{},reserve_step_{},initialized_{},root_cursor_{},root_count_{},head_{},last_work_{};
    Status status_{Status::NeedCompute};
    Phase phase_{Phase::Reserve};
    Reason reason_{Reason::None};
};
}
