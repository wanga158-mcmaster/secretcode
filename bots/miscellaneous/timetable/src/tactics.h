#pragma once
// Pure tactical models (no SDK): enemy reach, threat at a tile, camp damage accounting, escape
// steps and a skirmish verdict. These carry the safeguards:
//   S1 no engagement the model says we lose        (verdict)
//   S2 no contact without a known way out          (reach + escape)
//   S3 no awake camp left out of a fight's accounting (camp_incoming over every awake camp)
// Contact is sticky at equal speed (every champion walks one tile a round), so the decisions that
// matter are taken before contact: keep every enemy outside its reach and keep a way home.
#include <algorithm>
#include <cstdint>

namespace tt {

struct P2 {
    int x = 0, y = 0;
};

inline int dist(P2 a, P2 b)
{
    const int dx = a.x > b.x ? a.x - b.x : b.x - a.x;
    const int dy = a.y > b.y ? a.y - b.y : b.y - a.y;
    return dx > dy ? dx : dy;
}

// One champion (either team) as the tactical models see it.
struct Fighter {
    P2 pos;
    float hp = 0, max_hp = 1;
    float basic = 0;          // damage of one basic attack (strength)
    int range = 1;            // basic attack range
    bool melee = true;
    bool reset_ready = false; // attack reset available: two basics this turn
    bool dash_ready = false;  // dash of `dash_range` available (also carries Heart's landing hit)
    int dash_range = 2;
    float dash_hit = 0;       // damage of a damaging dash landing (radius 1)
    float spell = 0;          // ready single-target or area damage spell (0 if none ready)
    int spell_range = 0;
    int spell_radius = 0;
    float dps = 0;            // sustained damage per round (basic + resets + spells averaged)
    bool alive = true;
    int age = 0;              // rounds since this fighter was last seen (0 = visible now)
};

// Farthest Chebyshev distance at which `e` can hurt a target during its next turn.
// Melee walk and strike; ranged basic attacks share the move allowance, so a moved ranged
// champion cannot strike; damage spells exclude walking (one cast per turn, so no dash + spell).
inline int threat_reach(const Fighter& e)
{
    if (!e.alive) return -1;
    const int walk = 1;
    const int dash = e.dash_ready ? e.dash_range : 0;
    int reach = e.melee ? walk + dash + e.range : e.range + dash; // ranged: dash then shoot
    if (e.spell > 0) reach = std::max(reach, e.spell_range + e.spell_radius);
    if (e.dash_hit > 0 && e.dash_ready) reach = std::max(reach, walk + e.dash_range + 1);
    return reach + e.age; // an unseen enemy may have closed in since
}

// Damage `e` can deal next turn to a target at Chebyshev distance d.
inline float threat_damage(const Fighter& e, int d)
{
    if (!e.alive || d > threat_reach(e)) return 0.0f;
    const float basics = e.basic * (e.reset_ready ? 2.0f : 1.0f);
    float best = basics;
    if (e.spell > 0 && d <= e.spell_range + e.spell_radius) best = std::max(best, e.spell);
    if (e.dash_hit > 0 && e.dash_ready) best = std::max(best, e.dash_hit + e.basic);
    return best;
}

// A camp monster as the tactical models see it.
struct CampThreat {
    P2 pos;
    float str = 0;
    int range = 0;
    bool awake = false;
    int ally_dist = 99; // distance from the monster to the nearest other champion we see
};

// S3: damage per round at tile t from every awake camp whose range covers t. A monster strikes
// the nearest champion, so one with another champion strictly nearer than t spares t (a tie is
// counted against us: the engine's tie-break is not known).
inline float camp_incoming(P2 t, const CampThreat* camps, int n)
{
    float sum = 0;
    for (int i = 0; i < n; ++i) {
        const CampThreat& c = camps[i];
        if (!c.awake || c.str <= 0) continue;
        const int d = dist(t, c.pos);
        if (d <= c.range && d <= c.ally_dist) sum += c.str;
    }
    return sum;
}

// Steps needed to leave the reach of every awake camp striking tile t (Chebyshev, no walls):
// at equal footing a melee champion beside an elk (range 4) needs four steps, four more hits.
inline int camp_escape_steps(P2 t, const CampThreat* camps, int n)
{
    int steps = 0;
    for (int i = 0; i < n; ++i) {
        const CampThreat& c = camps[i];
        if (!c.awake || c.str <= 0) continue;
        const int d = dist(t, c.pos);
        if (d <= c.range && d <= c.ally_dist) steps = std::max(steps, c.range - d + 1);
    }
    return steps;
}

// Damage expected at tile t next round from the visible or remembered enemies.
inline float enemy_incoming(P2 t, const Fighter* enemies, int n)
{
    float sum = 0;
    for (int i = 0; i < n; ++i) sum += threat_damage(enemies[i], dist(t, enemies[i].pos));
    return sum;
}

// S2: a tile is safe when no enemy can reach it next turn.
inline bool safe_from(P2 t, const Fighter* enemies, int n)
{
    for (int i = 0; i < n; ++i)
        if (enemies[i].alive && dist(t, enemies[i].pos) <= threat_reach(enemies[i])) return false;
    return true;
}

// Margin by which tile t is outside every enemy's reach (negative: inside).
inline int safety_margin(P2 t, const Fighter* enemies, int n)
{
    int m = 99;
    for (int i = 0; i < n; ++i)
        if (enemies[i].alive) m = std::min(m, dist(t, enemies[i].pos) - threat_reach(enemies[i]));
    return m;
}

// Skirmish verdict: both sides fight from now with focus fire on the lowest-health reachable
// target; fighters walk in at one tile a round (arrival = distance beyond reach).
struct Verdict {
    int our_deaths = 0, their_deaths = 0;
    int first_our_death = 99, first_their_death = 99;
    float our_hp = 0, their_hp = 0;
};

inline Verdict skirmish(const Fighter* ours, int n, const Fighter* theirs, int m, int horizon = 12)
{
    constexpr int kMax = 8;
    float hp_a[kMax], hp_b[kMax];
    int arrive_a[kMax], arrive_b[kMax];
    n = std::min(n, kMax);
    m = std::min(m, kMax);
    for (int i = 0; i < n; ++i) {
        hp_a[i] = ours[i].alive ? ours[i].hp : 0;
        int gap = 99;
        for (int j = 0; j < m; ++j) gap = std::min(gap, dist(ours[i].pos, theirs[j].pos) - ours[i].range);
        arrive_a[i] = std::max(0, gap);
    }
    for (int j = 0; j < m; ++j) {
        hp_b[j] = theirs[j].alive ? theirs[j].hp : 0;
        int gap = 99;
        for (int i = 0; i < n; ++i) gap = std::min(gap, dist(theirs[j].pos, ours[i].pos) - theirs[j].range);
        arrive_b[j] = std::max(0, gap - theirs[j].age);
    }
    Verdict v;
    for (int r = 0; r < horizon; ++r) {
        // Focus: lowest current health among the living opponents.
        int fa = -1, fb = -1;
        for (int j = 0; j < m; ++j)
            if (hp_b[j] > 0 && (fa < 0 || hp_b[j] < hp_b[fa])) fa = j;
        for (int i = 0; i < n; ++i)
            if (hp_a[i] > 0 && (fb < 0 || hp_a[i] < hp_a[fb])) fb = i;
        if (fa < 0 || fb < 0) break;
        float dmg_to_b = 0, dmg_to_a = 0;
        for (int i = 0; i < n; ++i)
            if (hp_a[i] > 0 && r >= arrive_a[i]) dmg_to_b += r == arrive_a[i] ? std::max(ours[i].dps, ours[i].basic) : ours[i].dps;
        for (int j = 0; j < m; ++j)
            if (hp_b[j] > 0 && r >= arrive_b[j]) dmg_to_a += r == arrive_b[j] ? std::max(theirs[j].dps, theirs[j].basic) : theirs[j].dps;
        hp_b[fa] -= dmg_to_b;
        hp_a[fb] -= dmg_to_a;
        if (hp_b[fa] <= 0) { ++v.their_deaths; v.first_their_death = std::min(v.first_their_death, r); }
        if (hp_a[fb] <= 0) { ++v.our_deaths; v.first_our_death = std::min(v.first_our_death, r); }
    }
    for (int i = 0; i < n; ++i) v.our_hp += std::max(0.0f, hp_a[i]);
    for (int j = 0; j < m; ++j) v.their_hp += std::max(0.0f, hp_b[j]);
    return v;
}

// S1: we only choose to fight when the model gives a strictly better trade with no loss of ours.
inline bool favourable(const Verdict& v)
{
    return v.our_deaths == 0 && v.their_deaths > 0;
}

} // namespace tt
