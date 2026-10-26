#pragma once

// SDK-free ordinary movement. Public topology persists; current occupants only
// decide whether a freshly proposed adjacent step may actually be attempted.
#include "pure.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace plan4 {

enum class MovementStatus { ready, at_goal, in_progress, unreachable, blocked, invalid_origin };
struct MovementStep {
    MovementStatus status = MovementStatus::in_progress;
    Point next{};
    unsigned visits = 0; // Newly expanded vertices in this call, not turn total.
    int remaining_distance = -1;
    Point obstruction{};
    bool has_obstruction = false;
};

class PersistentMovement {
    int width_ = 0, height_ = 0;
    std::vector<unsigned char> walls_;
    // Only the bitmap is cleared. Distance and queue slots are written before
    // use, so allocating their capacity never initializes an entire map.
    std::unique_ptr<unsigned char[]> seen_;
    std::unique_ptr<int[]> distances_, queue_;
    std::vector<int> goals_, normalized_;
    std::size_t bitmap_bytes_ = 0, clear_cursor_ = 0, seed_cursor_ = 0;
    std::size_t head_ = 0, tail_ = 0;
    std::uint32_t generation_ = 0;
    unsigned visits_ = 0, allowance_ = 0;
    bool initialized_ = false, collision_pending_ = false, reset_pending_ = false;
    Point collision_from_{}, collision_to_{};
    MovementStatus last_status_ = MovementStatus::in_progress;

    bool in_bounds(Point p) const {
        return p.x >= 0 && p.y >= 0 && p.x < width_ && p.y < height_;
    }
    int index(Point p) const { return p.y * width_ + p.x; }
    Point position(int key) const { return {key % width_, key / width_}; }
    bool walkable(Point p) const { return in_bounds(p) && !walls_[index(p)]; }
    bool seen(int key) const { return (seen_[std::size_t(key) / 8] & (1u << (key % 8))) != 0; }
    void mark_seen(int key) { seen_[std::size_t(key) / 8] |= static_cast<unsigned char>(1u << (key % 8)); }
    static bool allowed(WorkBudget work) {
        return !work.can_refine || work.can_refine(work.context);
    }

    void set_goals(std::span<const Point> requested) {
        // SDK masks may already arrive in the exact stored row-major order.
        // Reuse that canonical prefix before rebuilding normalization storage;
        // an out-of-bounds coordinate must not alias an in-bounds linear key.
        if (generation_ && requested.size() == goals_.size() &&
            std::equal(requested.begin(), requested.end(), goals_.begin(),
                [this](Point point, int goal) { return in_bounds(point) && index(point) == goal; })) return;
        normalized_.clear();
        for (Point p : requested) if (walkable(p)) normalized_.push_back(index(p));
        std::sort(normalized_.begin(), normalized_.end());
        normalized_.erase(std::unique(normalized_.begin(), normalized_.end()), normalized_.end());
        if (generation_ && normalized_ == goals_) return;
        goals_.swap(normalized_);
        if (++generation_ == 0) generation_ = 1;
        head_ = tail_ = 0;
        clear_cursor_ = seed_cursor_ = 0;
        reset_pending_ = true;
        collision_pending_ = false;
    }

    bool finish_reset(WorkBudget work) {
        if (!reset_pending_) return true;
        // A budget stop can leave any prefix cleared. No bitmap lookup or BFS
        // is allowed until the whole bitmap and every goal seed are complete.
        while (clear_cursor_ < bitmap_bytes_) {
            if (!allowed(work)) return false;
            const std::size_t stop = std::min(bitmap_bytes_, clear_cursor_ + 32);
            while (clear_cursor_ < stop) seen_[clear_cursor_++] = 0;
        }
        while (seed_cursor_ < goals_.size()) {
            if (!allowed(work)) return false;
            const int goal = goals_[seed_cursor_++];
            mark_seen(goal);
            distances_[goal] = 0;
            queue_[tail_++] = goal;
        }
        reset_pending_ = false;
        return true;
    }

public:
    using Status = MovementStatus;
    using Step = MovementStep;
    using Blocked = bool (*)(Point, const void*);

    void initialize(int width, int height, std::vector<unsigned char> static_blocked) {
        if (width <= 0 || height <= 0 ||
            std::uint64_t(width) * std::uint64_t(height) > std::uint64_t(std::numeric_limits<int>::max()) ||
            std::size_t(width) * std::size_t(height) != static_blocked.size())
            throw std::invalid_argument("movement topology dimensions differ");
        width_ = width; height_ = height; walls_ = std::move(static_blocked);
        bitmap_bytes_ = (walls_.size() + 7) / 8;
        seen_.reset(new unsigned char[bitmap_bytes_]);
        distances_.reset(new int[walls_.size()]);
        queue_.reset(new int[walls_.size()]);
        goals_.clear(); normalized_.clear();
        head_ = tail_ = clear_cursor_ = seed_cursor_ = 0; generation_ = 0;
        visits_ = allowance_ = 0;
        collision_pending_ = reset_pending_ = false; initialized_ = true;
        last_status_ = MovementStatus::in_progress;
    }

    // already_used includes optional navigation and previous movement expansions.
    // All next_step calls before the next begin_turn share this allowance.
    void begin_turn(unsigned already_used = 0, unsigned allowance = 256) {
        visits_ = 0;
        allowance_ = std::min(allowance, 256u - std::min(256u, already_used));
    }
    unsigned vertex_visits() const { return visits_; }
    std::uint32_t cache_generation() const { return generation_; }
    bool in_progress() const { return last_status_ == MovementStatus::in_progress; }

    MovementStep next_step(Point origin, std::span<const Point> goals,
                           Blocked fresh_blocked = nullptr, const void* context = nullptr,
                           unsigned max_new_visits = 256, WorkBudget work = {}) {
        const unsigned before = visits_;
        auto finish = [&](MovementStatus status, Point next, int distance = -1) {
            last_status_ = status;
            return MovementStep{status, next, visits_ - before, distance};
        };
        if (!initialized_ || !walkable(origin)) return finish(MovementStatus::invalid_origin, origin);
        if (!allowed(work)) return finish(MovementStatus::in_progress, origin);
        set_goals(goals);
        if (!finish_reset(work)) return finish(MovementStatus::in_progress, origin);
        const int start = index(origin);
        const unsigned permitted = std::min(max_new_visits, allowance_ - std::min(allowance_, visits_));
        const unsigned stop = visits_ + permitted;
        // Distances become exact when discovered. The reverse multi-source BFS
        // can resume for a different origin without changing its public target.
        while (!seen(start) && head_ < tail_ && visits_ < stop) {
            if (!allowed(work)) return finish(MovementStatus::in_progress, origin);
            const int current = queue_[head_++];
            ++visits_;
            const Point p = position(current);
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dy) continue;
                const Point next{p.x + dx, p.y + dy};
                if (!walkable(next)) continue;
                const int key = index(next);
                if (seen(key)) continue;
                mark_seen(key);
                distances_[key] = distances_[current] + 1;
                queue_[tail_++] = key;
            }
        }
        if (!seen(start))
            return finish(head_ == tail_ ? MovementStatus::unreachable : MovementStatus::in_progress, origin);
        const int distance = distances_[start];
        if (!distance) { collision_pending_ = false; return finish(MovementStatus::at_goal, origin, 0); }
        const bool avoid_collision = collision_pending_ && origin == collision_from_;
        collision_pending_ = false;
        Point best = origin;
        Point obstruction = origin;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            if (!dx && !dy) continue;
            const Point next{origin.x + dx, origin.y + dy};
            if (!walkable(next) || (avoid_collision && next == collision_to_)) continue;
            const int key = index(next);
            if (!seen(key) || distances_[key] != distance - 1) continue;
            if (fresh_blocked && fresh_blocked(next, context)) {
                if (obstruction==origin || point_less(next,obstruction)) obstruction=next;
                continue;
            }
            if (best == origin || point_less(next, best)) best = next;
        }
        if (best == origin) {
            auto result=finish(MovementStatus::blocked,origin,distance);
            result.obstruction=obstruction;result.has_obstruction=obstruction!=origin;
            return result;
        }
        return finish(MovementStatus::ready, best, distance);
    }

    void notify_move_result(Point from, Point to, bool success) {
        if (success) { collision_pending_ = false; return; }
        // An unseen occupant is a transient obstruction. Avoid that exact step
        // for the next proposal; do not insert it into the public terrain cache.
        collision_from_ = from; collision_to_ = to; collision_pending_ = true;
        last_status_ = MovementStatus::blocked;
    }
};

} // namespace plan4
