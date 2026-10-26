#include "obs.h"
#include <algorithm>

namespace zero {

void observe(const Obs& o, const Map& map, Memory& m)
{
    const std::size_t G = map.groups.size();
    if (m.respawn_round.size() != G)
    {
        m.respawn_round.assign(G, -1);
        m.seen_round.assign(G, -1);
        m.enemy_near_round.assign(G, -1000);
    }
    // Deaths: the first turn we are seen dead after being alive.
    if (!o.alive && m.was_alive) ++m.deaths;
    m.was_alive = o.alive;
    // Own motion (a jump of more than two tiles is a teleport or respawn, not a move or dash).
    if (!o.alive) { m.prev_valid = false; m.dir_x = m.dir_y = 0; }
    else
    {
        if (m.prev_valid && o.self.at != m.prev_at && cheb(o.self.at, m.prev_at) <= 2)
        {
            m.dir_x = (o.self.at.x > m.prev_at.x) - (o.self.at.x < m.prev_at.x);
            m.dir_y = (o.self.at.y > m.prev_at.y) - (o.self.at.y < m.prev_at.y);
        }
        else if (m.prev_valid && cheb(o.self.at, m.prev_at) > 2) m.dir_x = m.dir_y = 0;
        m.prev_at = o.self.at;
        m.prev_valid = true;
    }
    // Sightings.
    for (const auto& e : o.enemies)
    {
        auto it = std::find_if(m.enemies.begin(), m.enemies.end(), [&](const SeenEnemy& s) { return s.id == e.id; });
        if (it == m.enemies.end()) { m.enemies.push_back({}); it = m.enemies.end() - 1; }
        *it = { e.id, e.type, e.at, e.hp, e.max_hp, e.strength, e.focus, e.gems, o.round };
        for (std::size_t g = 0; g < G; ++g)
            if (cheb(e.at, map.groups[g].center) <= 6) m.enemy_near_round[g] = o.round;
    }
    // Groups in view: a group seen respawning that we had not marked gets the rules' respawn delay from now.
    for (std::size_t g = 0; g < G && g < o.group_status.size(); ++g)
    {
        const auto st = o.group_status[g];
        if (st == gs_unknown) continue;
        m.seen_round[g] = o.round;
        if (st == gs_respawning && m.respawn_round[g] < o.round) m.respawn_round[g] = o.round + map.groups[g].respawn;
        if (st == gs_alive || st == gs_reward) m.respawn_round[g] = -1;
    }
    // Tempo history every 10 rounds.
    if (o.round % 10 == 0 && m.hist_round[static_cast<std::size_t>((m.hist_head + 5) % 6)] != o.round)
    {
        const auto h = static_cast<std::size_t>(m.hist_head);
        m.hist_round[h] = o.round;
        m.banked_us_hist[h] = o.banked_us;
        m.banked_them_hist[h] = o.banked_them;
        m.hist_head = (m.hist_head + 1) % 6;
    }
}

}  // namespace zero
