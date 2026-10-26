#pragma once
// One static public-map mission. No live memory or remote ally observations.
struct OpeningPlan {
    std::int32_t magic = 0x43485231, group = -1, second = -1;
    std::int32_t holder_approach = -1, clearer_approach = -1, exit = -1;
    std::int32_t status = 0; // 1 found, 2 no pair, 3 bounded search incomplete.
};
bitboard boundary(const Group& g, int range, const bitboard& walls) {
    bitboard result;
    for (const auto& member : g.group) result |= around(member.spawn_position(), range);
    return result & ~walls & ~g.spawns;
}
OpeningPlan opening_plan() {
    OpeningPlan result;
    const auto groups = public_groups();
    auto walls = public_terrain();
    for (const auto& g : groups) walls |= g.spawns;
    Field spawn_distance;
    bitboard visited;
    if (cambc::fuel_remaining() < 45000
        || spawn_distance.compute_with_expansion_count(walls, cambc::map::spawn_region(team::ally) & ~walls, visited, 256) >= 256) {
        result.status = 3; return result;
    }
    struct Candidate { std::size_t index; bool preferred; int sum, holder, clearer, exit; };
    std::vector<Candidate> candidates;
    for (std::size_t i = 0; i < groups.size(); ++i) {
        if (cambc::fuel_remaining() < 45000) { result.status = 3; return result; }
        const auto& g = groups[i]; if (g.monolith) continue;
        std::array<int,2> tile{-1,-1}, cost{unreachable,unreachable};
        for (unsigned role = 0; role < 2; ++role) {
            for (const auto p : boundary(g, role ? 1 : 3, walls).positions()) {
                const int d = spawn_distance.get(p), n = tile_index(p);
                if (d < cost[role] || (d == cost[role] && (tile[role] < 0 || n < tile[role]))) { cost[role] = d; tile[role] = n; }
            }
        }
        const auto outside = boundary(g, 1, walls).positions();
        if (cost[0] > 12 || cost[1] > 12 || outside.empty()) continue;
        int exit = std::numeric_limits<int>::max();
        for (const auto p : outside) exit = std::min(exit, tile_index(p));
        candidates.push_back({i, g.incoming == 0 && g.group.gem_value() > 0, cost[0]+cost[1], tile[0], tile[1], exit});
    }
    std::sort(candidates.begin(), candidates.end(), [&](const auto& a, const auto& b) {
        if (a.preferred != b.preferred) return a.preferred > b.preferred;
        if (a.sum != b.sum) return a.sum < b.sum;
        return groups[a.index].id < groups[b.index].id;
    });
    for (std::size_t rank = 0; rank < candidates.size(); ++rank) {
        if (rank >= profile::candidate_limit || groups.size() > profile::second_group_limit || cambc::fuel_remaining() < 45000) {
            result.status = 3; return result;
        }
        const auto& candidate = candidates[rank];
        // Fixed local reachability masks, never a per-candidate distance_field.
        std::array<bitboard,9> reachable;
        for (int d = 0; d <= 8; ++d) reachable[d] = walls.reachable_tiles(tile_position(candidate.exit), d);
        int second = -1, best_distance = 9;
        for (std::size_t j = 0; j < groups.size(); ++j) {
            if (cambc::fuel_remaining() < 45000) { result.status = 3; return result; }
            const auto& g = groups[j];
            if (j == candidate.index || g.monolith || g.incoming > 0 || !(g.group.gold_value() || g.group.gem_value())) continue;
            const auto goals = boundary(g, 1, walls);
            if ((goals & reachable[8]).is_empty()) continue;
            int d = 0;
            for (; d <= 8; ++d) if (!(reachable[d] & goals).is_empty()) break;
            if (d < best_distance || (d == best_distance && (second < 0 || g.id < groups[second].id))) { second = static_cast<int>(j); best_distance = d; }
        }
        if (second >= 0) {
            result.group = groups[candidate.index].id; result.second = groups[second].id;
            result.holder_approach = candidate.holder; result.clearer_approach = candidate.clearer; result.exit = candidate.exit;
            result.status = 1; return result;
        }
    }
    result.status = 2; return result;
}
void draft() {
    static bool planned = false;
    if (!planned) {
        const auto plan = opening_plan();
        if (cambc::constants::draft_memory_bytes >= sizeof plan) cambc::draft_memory = std::array{plan};
        planned = true;
    }
    constexpr std::array order{cambc::champion_draft::tavi,cambc::champion_draft::varg,
        cambc::champion_draft::vendetta,cambc::champion_draft::vesper_vale,cambc::champion_draft::marcella};
    const auto selected = cambc::draft::current_draft(team::ally);
    for (const auto choice : order) if (std::find(selected.begin(), selected.end(), choice) == selected.end()) { cambc::draft::pick(choice); return; }
}
