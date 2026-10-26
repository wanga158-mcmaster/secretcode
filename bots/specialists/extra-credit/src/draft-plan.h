#pragma once
// Only an immutable public-map probe plan is shared. The engine copies the
// complete fixed-capacity draft buffer into each separate physical instance.
struct PairPlan {
    std::int32_t magic = 0x45435831, even = -1, odd = -1, status = 0;
};
PairPlan pair_probe_plan() {
    PairPlan plan;
    const auto groups = public_groups();
    auto walls = public_terrain();
    for (const auto& g : groups) walls |= g.spawns;
    Field from_spawn; bitboard visited;
    if (cambc::fuel_remaining() < 45000 || from_spawn.compute_with_expansion_count(
        walls, cambc::map::spawn_region(team::ally) & ~walls, visited, 256) >= 256) { plan.status = 3; return plan; }
    std::array<int,2> best{unreachable,unreachable};
    for (const auto& g : groups) {
        if (cambc::fuel_remaining() < 45000) { plan.status = 3; return plan; }
        if (g.monolith || g.incoming > 0 || !g.gems || g.spawns.positions().size() < 2) continue;
        bitboard boundary;
        for (const auto& c : g.group) boundary |= around(c.spawn_position(),1);
        const int d = min_distance(from_spawn, boundary & ~walls);
        const unsigned parity = static_cast<unsigned>(g.canonical_non_monolith % 2);
        const int previous = parity ? plan.odd : plan.even;
        if (d < best[parity] || (d == best[parity] && (previous < 0 || g.id < previous))) {
            best[parity] = d; (parity ? plan.odd : plan.even) = g.id;
        }
    }
    plan.status = plan.even >= 0 && plan.odd >= 0 && best[0] != unreachable && best[1] != unreachable ? 1 : 2;
    return plan;
}
void draft() {
    static bool planned = false;
    if (!planned) {
        if constexpr(profile::probe_mode) {
            const auto plan = pair_probe_plan();
            if (cambc::constants::draft_memory_bytes >= sizeof plan) cambc::draft_memory = std::array{plan};
        }
        planned = true;
    }
    constexpr std::array order{cambc::champion_draft::tavi,cambc::champion_draft::cassi_and_issac,
        cambc::champion_draft::vendetta,cambc::champion_draft::vesper_vale,cambc::champion_draft::marcella};
    const auto selected = cambc::draft::current_draft(team::ally);
    for (const auto choice : order) if (std::find(selected.begin(),selected.end(),choice)==selected.end()) {
        cambc::draft::pick(choice); return;
    }
}
