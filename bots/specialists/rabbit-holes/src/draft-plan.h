#pragma once
// Only the selected endpoints enter draft memory. Full terrain fields belong
// to Vendetta's isolated gameplay instance, never to shared/draft storage.
struct RabbitEdge { int a = -1, b = -1, holes = -1; };
bool rabbit_geometry(vec2 a, vec2 b, const bitboard& terrain) {
    if (!cambc::map::is_in_bounds(a) || !cambc::map::is_in_bounds(b) || terrain.is_set(a) || terrain.is_set(b)) return false;
    if (!((a.x == b.x && std::abs(a.y - b.y) == 2) || (a.y == b.y && std::abs(a.x - b.x) == 2))) return false;
    const vec2 middle{(a.x + b.x) / 2, (a.y + b.y) / 2};
    return cambc::map::holes().is_set(middle) && !cambc::map::walls().is_set(middle);
}
void draft() {
    static bool planned = false;
    if (!planned) {
        planned = true;
        RabbitEdge best;
        const auto terrain = public_terrain();
        int candidates = 0;
        bool reserve = false;
        // (a,b) is in ascending tile-index order: right precedes down.
        for (int a = 0; a < cambc::map::width() * cambc::map::height() && candidates < profile::draft_candidate_limit && !reserve; ++a) {
            if (cambc::fuel_remaining() < 45000) break;
            const auto p = tile_position(a);
            for (const auto q : {vec2{p.x + 2, p.y}, vec2{p.x, p.y + 2}}) {
                if (candidates >= profile::draft_candidate_limit) break;
                if (cambc::fuel_remaining() < 45000) { reserve = true; break; }
                if (!rabbit_geometry(p, q, terrain)) continue;
                ++candidates;
                const vec2 midpoint{(p.x + q.x) / 2, (p.y + q.y) / 2};
                const int holes = static_cast<int>((around(midpoint, 2) & cambc::map::holes()).count());
                const int b = tile_index(q);
                if (holes > best.holes || (holes == best.holes && (a < best.a || (a == best.a && b < best.b)))) best = {a, b, holes};
            }
        }
        const std::array<std::int32_t, 2> endpoints{best.a, best.b};
        if (sizeof endpoints <= cambc::constants::draft_memory_bytes) cambc::draft_memory = endpoints;
    }
    constexpr std::array order{cambc::champion_draft::tavi, cambc::champion_draft::varg, cambc::champion_draft::vendetta,
        cambc::champion_draft::vesper_vale, cambc::champion_draft::marcella};
    const auto selected = cambc::draft::current_draft(team::ally);
    for (const auto choice : order) if (std::find(selected.begin(), selected.end(), choice) == selected.end()) {
        cambc::draft::pick(choice); return;
    }
}
