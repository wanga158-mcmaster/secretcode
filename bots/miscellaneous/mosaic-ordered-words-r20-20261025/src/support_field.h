#pragma once
// Complete CAMP potential support with explicit, externally proved GROUP absence.
// A query bounds ONE camp phase at every time through its requested horizon;
// return_budget separately sums the physical waiting/travel phases. No work
// nomination, sleep, last-seen corpse or visibility estimate is authority here.
#include "return_budget.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace mosaic::support_field {
using Point = return_budget::Point;
using Cost = return_budget::Cost;
using Damage = return_budget::Damage;

enum class Status { ok, deferred, invalid, capacity, incomplete, sealed, stale };
struct Edit {
    Status status{Status::invalid};
    std::size_t work{};
    bool applied() const { return status == Status::ok; }
};
struct Query {
    Cost amount{};
    bool valid{};
    // A deferred query still returns a conservative bound: unvisited group
    // contributions remain charged. Only exact==true claims all discounts.
    bool exact{};
    std::size_t work{};
    Damage damage() const { return {amount, valid}; }
};

class Field {
public:
    static constexpr std::size_t unlimited = std::numeric_limits<std::size_t>::max();
    // Representation limits, not copied gameplay/balance constants.
    static constexpr std::size_t max_cells = 1u << 20;
    static constexpr std::size_t max_groups = 65535;

    // One setup allocation only. The caller admits this separately from hot
    // queries. Logical work includes grid/group initialization and reserved
    // entry capacity; it is NOT a native fuel measurement or allocation bound.
    // Failure/defer leaves an existing field unchanged.
    Edit reset(int width, int height, std::size_t groups,
               std::size_t entry_capacity, std::size_t quota = unlimited) {
        if (width <= 0 || height <= 0 || groups > max_groups ||
            entry_capacity >= no_node) return {Status::invalid, 0};
        const auto cells = std::uint64_t(width) * std::uint64_t(height);
        if (cells > max_cells) return {Status::capacity, 0};
        if (entry_capacity > unlimited - std::size_t(cells) - groups)
            return {Status::capacity, 0};
        const auto needed = std::size_t(cells) + groups + entry_capacity;
        if (needed > quota) return {Status::deferred, 0};
        std::vector<Tile> tiles(std::size_t(cells), Tile{});
        std::vector<Group> group_data(groups);
        std::vector<Entry> entries;
        entries.reserve(entry_capacity); // no later growth/copy inside addCamp
        tiles_.swap(tiles); groups_.swap(group_data); entries_.swap(entries);
        width_ = width; height_ = height; entry_capacity_ = entry_capacity;
        camp_count_ = 0; sealed_ = false; initialized_ = true;
        absent_groups_ = 0; highest_deadline_ = 0; ++revision_;
        return {Status::ok, needed};
    }

    // Incremental setup. Each tile has ONE aggregate per intersecting group,
    // so hot queries never walk the group's individual camp bodies. Two-pass
    // admission makes every failure/defer atomic, including capacity/overflow.
    // Units count both passes' cell/list visits and new-entry writes; vector
    // storage has already been reserved by reset. They are not WASM fuel units.
    Edit addCamp(std::size_t group, Point pos, int range, Cost ceil_hit,
                 std::size_t quota = unlimited) {
        if (!initialized_ || group >= groups_.size() || !inside(pos) || range < 0 ||
            ceil_hit > return_budget::cost_limit) return {Status::invalid, 0};
        if (sealed_) return {Status::sealed, 0};
        if (ceil_hit == 0) { ++camp_count_; ++revision_; return {Status::ok, 0}; }
        const auto box = footprint(pos, range);
        std::size_t work = 0, new_entries = 0;
        auto spend = [&](std::size_t n) {
            if (n > quota - work) return false;
            work += n; return true;
        };
        for (int y = box.y0; y <= box.y1; ++y) for (int x = box.x0; x <= box.x1; ++x) {
            if (!spend(2)) return {Status::deferred, work};
            const auto& tile = tiles_[index({x, y})];
            if (ceil_hit > return_budget::cost_limit - tile.total)
                return {Status::invalid, work};
            auto node = tile.head;
            while (node != no_node) {
                if (!spend(2)) return {Status::deferred, work};
                if (entries_[node].group == group) break;
                node = entries_[node].next;
            }
            if (node == no_node) {
                if (!spend(1)) return {Status::deferred, work};
                ++new_entries;
            }
        }
        if (new_entries > entry_capacity_ - entries_.size()) return {Status::capacity, work};
        for (int y = box.y0; y <= box.y1; ++y) for (int x = box.x0; x <= box.x1; ++x) {
            auto& tile = tiles_[index({x, y})];
            auto node = tile.head;
            while (node != no_node && entries_[node].group != group) node = entries_[node].next;
            if (node == no_node) {
                node = std::uint32_t(entries_.size());
                entries_.push_back({std::uint32_t(group), tile.head, ceil_hit});
                tile.head = node;
            } else entries_[node].damage += ceil_hit;
            tile.total += ceil_hit;
        }
        ++camp_count_; ++revision_; return {Status::ok, work};
    }

    // expected_camps is the adapter's complete catalog count, including all
    // zero-strength kinds. Incomplete setup can never publish a quiet tile.
    Edit seal(std::size_t expected_camps) {
        if (!initialized_) return {Status::invalid, 0};
        if (camp_count_ != expected_camps) return {Status::incomplete, 0};
        if (!sealed_) { sealed_ = true; ++revision_; }
        return {Status::ok, 0};
    }

    // Only a freshly queried whole-group lifecycle can supply this earliest
    // possible respawn epoch. Deadline expiry NEVER infers living work; it
    // merely restores potential threat. A fresh alive/new-generation event
    // MUST call restore immediately, before any further quote or actuation.
    Edit absence(std::size_t group, std::int64_t observed_epoch,
                 std::int64_t earliest_respawn) {
        if (!initialized_ || group >= groups_.size() || observed_epoch < 0 || earliest_respawn < 0)
            return {Status::invalid, 0};
        auto& g = groups_[group];
        if (observed_epoch < g.seen || (observed_epoch == g.seen && !g.absent))
            return {Status::stale, 0}; // restoration wins same-epoch conflicts
        const bool absent = earliest_respawn > observed_epoch;
        auto deadline = std::uint64_t(earliest_respawn);
        if (observed_epoch == g.seen && g.absent) deadline = std::min(deadline, g.deadline);
        if (g.seen == observed_epoch && g.deadline == deadline && g.absent == absent)
            return {Status::ok, 1};
        if (g.absent != absent) absent_groups_ = absent ? absent_groups_ + 1 : absent_groups_ - 1;
        g = {observed_epoch, deadline, absent};
        if (absent) highest_deadline_ = std::max(highest_deadline_, deadline);
        ++revision_; return {Status::ok, 1};
    }
    Edit restore(std::size_t group, std::int64_t observed_epoch) {
        if (!initialized_ || group >= groups_.size() || observed_epoch < 0) return {Status::invalid, 0};
        auto& g = groups_[group];
        if (observed_epoch < g.seen) return {Status::stale, 0};
        if (g.seen == observed_epoch && !g.absent) return {Status::ok, 1};
        if (g.absent) --absent_groups_;
        g = {observed_epoch, 0, false}; ++revision_; return {Status::ok, 1};
    }

    Query damageAt(Point p, std::int64_t now, std::uint64_t required_horizon,
                   std::size_t quota = unlimited) const {
        if (!sealed_ || !inside(p) || now < 0 ||
            required_horizon > std::numeric_limits<std::uint64_t>::max() - std::uint64_t(now)) return {};
        const auto endpoint = std::uint64_t(now) + required_horizon;
        const auto& tile = tiles_[index(p)];
        Query out{tile.total, true, true, 0};
        if (absent_groups_ == 0 || highest_deadline_ <= endpoint || tile.total == 0) return out;
        for (auto node = tile.head; node != no_node; node = entries_[node].next) {
            if (out.work == quota) { out.exact = false; return out; }
            ++out.work;
            const auto& entry = entries_[node]; const auto& g = groups_[entry.group];
            if (g.absent && g.seen <= now && g.deadline > endpoint) out.amount -= entry.damage;
        }
        return out;
    }

    Damage potentialAt(Point p) const {
        if (!sealed_ || !inside(p)) return {0, false};
        return {tiles_[index(p)].total, true};
    }
    bool ready() const { return sealed_; }
    std::size_t campCount() const { return camp_count_; }
    std::size_t entryCount() const { return entries_.size(); }
    std::uint64_t revision() const { return revision_; }

private:
    static constexpr std::uint32_t no_node = std::numeric_limits<std::uint32_t>::max();
    struct Tile { Cost total{}; std::uint32_t head{no_node}; };
    struct Entry { std::uint32_t group, next; Cost damage; };
    struct Group { std::int64_t seen{-1}; std::uint64_t deadline{}; bool absent{}; };
    struct Box { int x0, y0, x1, y1; };
    int width_{}, height_{};
    std::size_t entry_capacity_{}, camp_count_{}, absent_groups_{};
    std::uint64_t highest_deadline_{}, revision_{};
    bool initialized_{}, sealed_{};
    std::vector<Tile> tiles_;
    std::vector<Group> groups_;
    std::vector<Entry> entries_;
    bool inside(Point p) const { return p.x >= 0 && p.y >= 0 && p.x < width_ && p.y < height_; }
    std::size_t index(Point p) const { return std::size_t(p.y) * std::size_t(width_) + std::size_t(p.x); }
    Box footprint(Point p, int r) const {
        return {int(std::max<std::int64_t>(0, std::int64_t(p.x) - r)),
                int(std::max<std::int64_t>(0, std::int64_t(p.y) - r)),
                int(std::min<std::int64_t>(width_ - 1, std::int64_t(p.x) + r)),
                int(std::min<std::int64_t>(height_ - 1, std::int64_t(p.y) + r))};
    }
};
} // namespace mosaic::support_field
