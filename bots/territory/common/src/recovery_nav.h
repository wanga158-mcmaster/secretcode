#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <queue>
#include <vector>

namespace p7::live {
struct RecoveryHazard {
    int x = 0, y = 0, range = 0;
    float damage = 0;
};
struct RecoveryStep {
    int x = -1, y = -1, expansions = 0;
    bool complete = false;
};

// Local escape, not a source selector: minimize observed attack exposure on a
// walk to a tile outside every recovery envelope, then minimize walking time.
// Occupancy is supplied fresh for this action-free search. A cutoff supplies no
// route; in particular, it never certifies that the current tile is safe.
template<class Blocked, class Permit>
RecoveryStep recovery_step(int x, int y, int width, int height,
    const std::vector<RecoveryHazard>& hazards, Blocked blocked, Permit permit) {
    constexpr int radius = 12, side = radius * 2 + 1, count = side * side;
    struct Node { float exposure; int steps, index; };
    struct Later { bool operator()(const Node& a, const Node& b) const {
        if (a.exposure != b.exposure) return a.exposure > b.exposure;
        if (a.steps != b.steps) return a.steps > b.steps;
        return a.index > b.index;
    }};
    std::array<float, count> costs; costs.fill(INFINITY);
    std::array<int, count> steps, first; steps.fill(count); first.fill(-1);
    std::priority_queue<Node, std::vector<Node>, Later> queue;
    const int root = radius * side + radius;
    costs[root] = 0; steps[root] = 0; first[root] = root; queue.push({0, 0, root});
    RecoveryStep result;
    auto separation = [](int ax, int ay, const RecoveryHazard& h) {
        return std::max(std::abs(ax - h.x), std::abs(ay - h.y));
    };
    while (!queue.empty()) {
        if (result.expansions >= 192 || (result.expansions % 8 == 0 && !permit())) return result;
        const auto current = queue.top(); queue.pop();
        if (current.exposure != costs[current.index] || current.steps != steps[current.index]) continue;
        ++result.expansions;
        const int px = x + current.index % side - radius, py = y + current.index / side - radius;
        bool safe = true;
        for (const auto& h : hazards) if (separation(px, py, h) <= h.range + 1) { safe = false; break; }
        if (safe) {
            const int next = first[current.index];
            result.x = x + next % side - radius; result.y = y + next / side - radius;
            result.complete = true; return result;
        }
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            if (!dx && !dy) continue;
            const int nx = px + dx, ny = py + dy, lx = nx - x + radius, ly = ny - y + radius;
            if (nx < 0 || ny < 0 || nx >= width || ny >= height || lx < 0 || ly < 0 || lx >= side || ly >= side || blocked(nx, ny)) continue;
            float damage = 0;
            for (const auto& h : hazards) if (separation(nx, ny, h) <= h.range) damage += h.damage;
            const int index = ly * side + lx, turns = current.steps + 1;
            const float cost = current.exposure + damage;
            if (cost > costs[index] || (cost == costs[index] && turns >= steps[index])) continue;
            costs[index] = cost; steps[index] = turns;
            first[index] = current.index == root ? index : first[current.index];
            queue.push({cost, turns, index});
        }
    }
    return result;
}
} // namespace p7::live
