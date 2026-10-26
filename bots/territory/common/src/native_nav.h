#pragma once
#include <algorithm>
#include <limits>
#include <vector>

namespace p7::live {
struct NativeMobilityRoute {
    // Includes the origin and subsequent landing tiles. A nonnegative via[i]
    // identifies the intermediate hole of the dash ending at tiles[i].
    std::vector<int> tiles;
    std::vector<int> via;
    int crossings = 0;
    bool complete = false;
};

class NativeNav {
    int width_ = 0, height_ = 0, count_ = 0;
    bool (*guard_)() = nullptr;
    std::vector<unsigned char> blocked_;
    std::vector<int> distances_, parents_, via_, queue_;

    void reset_result() {
        last_complete = last_exhausted = last_found = last_invalid = false;
        last_expansions = 0;
    }
    bool permitted() const {
        return !guard_ || last_expansions % 16 != 0 || guard_();
    }
    bool contains(int tile) const { return tile >= 0 && tile < count_; }
    bool allowed(int tile, const std::vector<unsigned char>* extra = nullptr) const {
        return contains(tile) && !blocked_[tile] && (!extra || !(*extra)[tile]);
    }
    template<class Visit> void neighbors(int tile, Visit visit) const {
        const int x = tile % width_, y = tile / width_;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            if (!dx && !dy) continue;
            const int nx = x + dx, ny = y + dy;
            if (nx >= 0 && nx < width_ && ny >= 0 && ny < height_)
                visit(ny * width_ + nx);
        }
    }
    std::vector<int> witness(int from, int to) const {
        std::vector<int> out;
        if (!contains(to) || parents_[to] < 0) return out;
        for (int tile = to;; tile = parents_[tile]) {
            out.push_back(tile);
            if (tile == from) break;
        }
        std::reverse(out.begin(), out.end());
        return out;
    }
    std::vector<int> route_impl(int from, int to,
        const std::vector<unsigned char>* extra, int max_expansions,
        int forbidden_from = -1, int forbidden_to = -1) {
        reset_result();
        if (!count_ || !contains(from) || !contains(to) || max_expansions < 0 ||
            (extra && extra->size() != blocked_.size())) { last_invalid = true; return {}; }
        if (!allowed(from, extra) || !allowed(to, extra)) {
            last_complete = last_exhausted = true; return {};
        }
        parents_.assign(count_, -1); queue_.clear();
        parents_[from] = from; queue_.push_back(from);
        if (from == to) { last_complete = last_found = true; return {from}; }
        std::size_t head = 0;
        while (head < queue_.size()) {
            if (last_expansions >= max_expansions || !permitted()) return {};
            const int tile = queue_[head++]; ++last_expansions;
            neighbors(tile, [&](int next) {
                if (last_found || parents_[next] >= 0 || !allowed(next, extra)) return;
                if ((tile == forbidden_from && next == forbidden_to) ||
                    (tile == forbidden_to && next == forbidden_from)) return;
                parents_[next] = tile; queue_.push_back(next);
                if (next == to) last_found = true;
            });
            if (last_found) { last_complete = true; return witness(from, to); }
        }
        last_complete = last_exhausted = true; return {};
    }

public:
    // Complete means the requested result was established without cutoff.
    // Exhausted means the reachable search space was fully processed. Finding a
    // route is complete without exhausting the graph; a fuel cutoff is neither.
    bool last_complete = false, last_exhausted = false;
    bool last_found = false, last_invalid = false;
    int last_expansions = 0;

    // True permits more work; false is a resource cutoff, never disconnection.
    // Initialization preserves this adapter-supplied callback.
    void set_guard(bool (*guard)()) { guard_ = guard; }

    void init(int width, int height, const std::vector<unsigned char>& blocked) {
        width_ = height_ = count_ = 0; blocked_.clear();
        distances_.clear(); parents_.clear(); via_.clear(); queue_.clear(); reset_result();
        if (width <= 0 || height <= 0 || height > std::numeric_limits<int>::max() / width ||
            blocked.size() != static_cast<std::size_t>(width * height)) { last_invalid = true; return; }
        width_ = width; height_ = height; count_ = width * height; blocked_ = blocked;
        distances_.reserve(count_); parents_.reserve(count_); via_.reserve(count_); queue_.reserve(count_);
    }

    std::vector<int> field(const std::vector<int>& roots, int max_expansions = 12000) {
        reset_result(); distances_.assign(count_, -1); queue_.clear();
        if (!count_ || max_expansions < 0) { last_invalid = true; return distances_; }
        for (int root : roots) if (allowed(root) && distances_[root] < 0) {
            distances_[root] = 0; queue_.push_back(root);
        }
        std::size_t head = 0;
        while (head < queue_.size()) {
            if (last_expansions >= max_expansions || !permitted()) return distances_;
            const int tile = queue_[head++]; ++last_expansions;
            neighbors(tile, [&](int next) {
                if (allowed(next) && distances_[next] < 0) {
                    distances_[next] = distances_[tile] + 1; queue_.push_back(next);
                }
            });
        }
        last_complete = last_exhausted = true;
        return distances_;
    }

    std::vector<int> route(int from, int to, const std::vector<unsigned char>* extra = nullptr,
        int max_expansions = 12000) { return route_impl(from, to, extra, max_expansions); }

    std::vector<int> disjoint(int from, int to, int alternative_to, int max_expansions = 12000) {
        const auto first = route(from, to, nullptr, max_expansions);
        if (first.empty()) return {};
        const int first_expansions = last_expansions;
        if (first.size() == 1) { last_found = false; return {}; }
        std::vector<unsigned char> removed(count_, 0);
        for (std::size_t i = 1; i + 1 < first.size(); ++i) removed[first[i]] = 1;
        // Adjacent endpoints have empty interiors. Exclude their common edge so
        // the same one-step witness cannot masquerade as a second exit.
        auto second = route_impl(from, alternative_to, &removed,
            std::max(0, max_expansions - first_expansions),
            first.size() == 2 ? from : -1, first.size() == 2 ? to : -1);
        last_expansions += first_expansions;
        return second;
    }

    NativeMobilityRoute mobility_route(int from, int to,
        const std::vector<unsigned char>& holes, int range = 2, int max_expansions = 12000) {
        reset_result(); NativeMobilityRoute out;
        if (!count_ || !contains(from) || !contains(to) || holes.size() != blocked_.size() ||
            range < 1 || max_expansions < 0) { last_invalid = true; return out; }
        if (!allowed(from) || !allowed(to) || holes[from] || holes[to]) {
            out.complete = last_complete = last_exhausted = true; return out;
        }
        parents_.assign(count_, -1); via_.assign(count_, -1); queue_.clear();
        parents_[from] = from; queue_.push_back(from);
        last_found = from == to;
        std::size_t head = 0;
        while (!last_found && head < queue_.size()) {
            if (last_expansions >= max_expansions || !permitted()) return out;
            const int tile = queue_[head++]; ++last_expansions;
            // Ordinary eight-neighbor steps retain the SDK's lack of an extra
            // diagonal corner restriction. Holes are never ordinary landings.
            neighbors(tile, [&](int next) {
                if (!last_found && allowed(next) && !holes[next] && parents_[next] < 0) {
                    parents_[next] = tile; queue_.push_back(next);
                    if (next == to) last_found = true;
                }
            });
            if (range < 2 || last_found) continue;
            // Only the intermediate hole overrides blocked terrain. Walls,
            // altars and enemy spawn remain blocked, including all landings.
            neighbors(tile, [&](int middle) {
                if (!holes[middle] || last_found) return;
                neighbors(middle, [&](int next) {
                    if (last_found || next == tile || !allowed(next) || holes[next] || parents_[next] >= 0) return;
                    parents_[next] = tile; via_[next] = middle; queue_.push_back(next);
                    if (next == to) last_found = true;
                });
            });
        }
        out.complete = last_complete = true;
        if (!last_found) { last_exhausted = true; return out; }
        out.tiles = witness(from, to); out.via.reserve(out.tiles.size());
        for (int tile : out.tiles) {
            out.via.push_back(via_[tile]);
            if (via_[tile] >= 0) ++out.crossings;
        }
        return out;
    }
};
} // namespace p7::live
