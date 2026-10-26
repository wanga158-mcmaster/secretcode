// _duel-lab: scripted duels, chases and skirmishes for the contenders' combat foundation
// (bots/contenders/foundation/combat.md). Scratch bot: never a contender, never on the ladder.
//
// One build serves every scenario: the scenario is encoded as wall bits on the map's bottom row
// (x = kX0 .. kX0+kBits-1, y = H-1), written by foundation/tools/combat/duel.py. Both teams run
// this same bot and read the same bits, so each side knows both kits, builds and policies.
// Bits (LSB first): 0-3 pick A, 4-7 pick B (champion_draft), 8-11 build A, 12-15 build B,
// 16-18 policy A, 19-21 policy B, 22-24 placement, 25-27 misc, 28 table mode, 29 parity.
// Logs one line per duelist turn from the start round ("D ..."), plus "S" stat lines.
#include <cambc.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "table.h"

using cambc::champion;
using cambc::champion_draft;
using cambc::champion_type;
using cambc::team;
using cambc::vec2;

namespace {

constexpr int kX0 = 6, kBits = 32;  // bits 30/31: high bit of policy A/B

int W = 0, H = 0, MYTEAM = 0;  // MYTEAM: 0 = A (spawn at the top-left), 1 = B

int cheb(vec2 a, vec2 b) { return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y)); }
int eu2(vec2 a, vec2 b) { return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y); }
vec2 mir(vec2 p) { return { W - 1 - p.x, H - 1 - p.y }; }
// A-frame coordinates -> absolute for side `s`.
vec2 for_side(int s, vec2 p) { return s == 0 ? p : mir(p); }

struct Scn
{
    Side s[2]{};
    int place{}, misc{};
    bool table{};
    int tindex{};
    int parity{};
    int start{};
    bool any_build{};
};
Scn S;
bool decoded = false;

void decode()
{
    if (decoded) return;
    W = cambc::map::width();
    H = cambc::map::height();
    MYTEAM = cambc::map::spawn_region(team::ally).is_set(vec2{ 1, 1 }) ? 0 : 1;
    const auto& walls = cambc::map::walls();
    std::uint32_t v = 0;
    for (int i = 0; i < kBits; ++i)
        if (walls.is_set(vec2{ kX0 + i, H - 1 })) v |= 1u << i;
    S.s[0].pick = int(v & 15u);
    S.s[1].pick = int((v >> 4) & 15u);
    S.s[0].build = int((v >> 8) & 15u);
    S.s[1].build = int((v >> 12) & 15u);
    S.s[0].pol = int((v >> 16) & 7u) | int(((v >> 30) & 1u) << 3);
    S.s[1].pol = int((v >> 19) & 7u) | int(((v >> 31) & 1u) << 3);
    S.place = int((v >> 22) & 7u);
    S.misc = int((v >> 25) & 7u);
    S.table = ((v >> 28) & 1u) != 0;
    S.parity = int((v >> 29) & 1u);
    S.tindex = int(v & 0xFFFu);
    S.any_build = S.table ? table_any_build(S.tindex) : (S.s[0].build != 0 || S.s[1].build != 0);
    S.start = (S.any_build ? 110 : (S.place == 7 ? 50 : 30)) + S.parity;
    decoded = true;
}

// ------------------------------------------------------------------ draft
void play_draft()
{
    decode();
    const auto mine = cambc::draft::current_draft(team::ally);
    const std::size_t k = mine.size();
    int want = -1;
    if (S.table)
    {
        want = table_pick(S.tindex, MYTEAM, int(k));
    }
    else if (k == 0)
    {
        want = S.s[MYTEAM].pick;
    }
    if (want < 0)
    {
        // Fillers: strong farmers first, never the duelist, never a second body.
        static const int pref[] = { 2, 1, 7, 10, 0, 6, 9, 11, 4, 5, 8 };
        for (int p : pref)
        {
            bool used = false;
            for (auto c : mine) used = used || int(c) == p;
            if (!used) { want = p; break; }
        }
    }
    cambc::draft::pick(static_cast<champion_draft>(want));
}

// ------------------------------------------------------------------ kits
enum Role { NONE, RESET, DASH, DMG, STUN, AOE, AOESTUN, HEAL, SHIELD, ENHRESET, HDASH };
struct Kit { Role r[2]; bool ranged; };
Kit kit_of(champion_type t)
{
    switch (t)
    {
    case champion_type::ryth: return { { AOESTUN, RESET }, false };
    case champion_type::vesper_vale: return { { RESET, DASH }, true };
    case champion_type::vendetta: return { { RESET, DASH }, false };
    case champion_type::cassi: return { { DMG, STUN }, true };
    case champion_type::issac: return { { RESET, DASH }, false };
    case champion_type::stellar: return { { DMG, AOE }, true };
    case champion_type::prism: return { { STUN, AOE }, true };
    case champion_type::marcella: return { { ENHRESET, DMG }, false };
    case champion_type::willow: return { { HEAL, RESET }, true };
    case champion_type::ren: return { { STUN, DASH }, false };
    case champion_type::varg: return { { RESET, SHIELD }, false };
    case champion_type::tavi: return { { RESET, STUN }, true };
    case champion_type::heart_of_ruin: return { { SHIELD, HDASH }, false };
    default: return { { NONE, NONE }, false };
    }
}
// champion_draft pick -> the champion types it creates
bool pick_is(int pick, champion_type t)
{
    switch (pick)
    {
    case 0: return t == champion_type::ryth;
    case 1: return t == champion_type::vesper_vale;
    case 2: return t == champion_type::vendetta;
    case 3: return t == champion_type::cassi || t == champion_type::issac;
    case 4: return t == champion_type::stellar;
    case 5: return t == champion_type::prism;
    case 6: return t == champion_type::marcella;
    case 7: return t == champion_type::willow;
    case 8: return t == champion_type::ren;
    case 9: return t == champion_type::varg;
    case 10: return t == champion_type::tavi;
    case 11: return t == champion_type::heart_of_ruin;
    default: return false;
    }
}
const char* short_name(champion_type t)
{
    static const char* names[] = { "ryth", "vesp", "vend", "cass", "issa", "stel", "pris", "marc", "will", "ren", "varg", "tavi", "hear" };
    const int i = int(t);
    return i >= 0 && i < 13 ? names[i] : "?";
}

// ------------------------------------------------------------------ builds
struct Buy { int item, count; };
const std::vector<Buy>& build_items(int b)
{
    static const std::vector<std::vector<Buy>> builds = {
        {},                                   // 0 base
        { { 4, 5 } },                         // 1 H5   (monster: no PvP passive)
        { { 4, 10 } },                        // 2 H10
        { { 4, 15 } },                        // 3 H15
        { { 0, 5 } },                         // 4 S5
        { { 0, 10 } },                        // 5 S10
        { { 0, 15 } },                        // 6 S15
        { { 2, 5 } },                         // 7 F5
        { { 2, 10 } },                        // 8 F10
        { { 2, 15 } },                        // 9 F15
        { { 22, 5 }, { 24, 5 } },             // 10 Treasure/H + Jungle/S        = H5 S5
        { { 22, 5 }, { 24, 5 }, { 40, 5 } },  // 11 + Bruiser/H                  = H10 S6 F1
        { { 20, 5 }, { 8, 5 }, { 16, 5 } },   // 12 Treasure/F + Power/F + Dur/H = F11 H5, -10%
        { { 16, 5 } },                        // 13 Durability/H                 = H5, -10%
        { { 16, 5 }, { 4, 5 } },              // 14 Durability/H + H5            = H10, -10%
        { { 18, 5 }, { 24, 5 }, { 16, 5 } },  // 15 Treasure/S + Jungle/S + Dur/H  = S10 H5, -10% (main r350)
    };
    return builds[std::size_t(b < 0 || b > 15 ? 0 : b)];
}
int build_cost(int b)
{
    int c = 0;
    for (auto& x : build_items(b)) c += 30 * x.count;
    return c;
}

// ------------------------------------------------------------------ state
struct Mem
{
    bool init = false;
    bool duelist = false;
    int side = 0;      // my absolute side
    int pol = 0, build = 0;
    int phase = 0;     // 0 farm, 1 capture, 2 shop, 3 position, 4 duel
    int farm_rounds = 0, capture_wait = 0;
    bool shopped = false;
    bool baited = false;  // BAIT: the camp has been hit
    bool logged_stats = false;
    bool dead_logged = false;
    int slot_index = 0;  // formation slot (table mode)
    Unit unit{};
};
Mem M;

bool is_blocked(vec2 p)
{
    if (!cambc::map::is_in_bounds(p)) return true;
    if (cambc::map::walls().is_set(p) || cambc::map::holes().is_set(p) || cambc::map::altars().is_set(p)) return true;
    if (cambc::map::spawn_region(team::enemy).is_set(p)) return true;
    const auto occ = cambc::vision::champion_tiles() | cambc::vision::camp_tiles();
    return occ.is_set(p);
}

// Greedy step: minimise (or maximise) Chebyshev distance to `t`, tie-break by squared distance
// to `tie` (closer is better), then by a fixed direction order.
bool step(vec2 t, bool away, vec2 tie)
{
    auto& me = cambc::self();
    if (!me.can_move()) return false;
    const vec2 o = me.position();
    const int base = cheb(o, t);
    vec2 best = o;
    long bestk = 0;
    bool found = false;
    static const int dx[] = { 1, -1, 0, 0, 1, 1, -1, -1 };
    static const int dy[] = { 0, 0, 1, -1, 1, -1, 1, -1 };
    for (int i = 0; i < 8; ++i)
    {
        const vec2 p{ o.x + dx[i], o.y + dy[i] };
        if (is_blocked(p)) continue;
        const int d = cheb(p, t);
        if (!away && d > base) continue;
        if (away && d < base) continue;
        const long key = (away ? -long(d) : long(d)) * 1000000L + long(eu2(p, tie));
        if (!found || key < bestk) { best = p; bestk = key; found = true; }
    }
    if (!found || best == o) return false;
    if (!away && cheb(best, t) >= base && eu2(best, tie) >= eu2(o, tie)) return false;
    return me.move_absolute(best);
}
bool step_toward(vec2 t) { return step(t, false, t); }
bool step_away(vec2 from, vec2 home) { return step(from, true, home); }

std::string acts;

struct Foe
{
    champion c;
    vec2 p;
    float hp;
    int d;
};

std::vector<Foe> foes()
{
    std::vector<Foe> out;
    const vec2 me = cambc::self().position();
    for (auto& c : cambc::champions(team::enemy))
    {
        if (!c.is_visible()) continue;
        if (!c.is_alive()) continue;
        const vec2 p = c.position();
        out.push_back({ c, p, c.health(), cheb(me, p) });
    }
    return out;
}

// Ability helpers ----------------------------------------------------------
int slot_of(Role r)
{
    const auto k = kit_of(cambc::self().champion_name());
    for (int i = 0; i < 2; ++i)
        if (k.r[i] == r) return i;
    return -1;
}
bool ready(Role r)
{
    const int s = slot_of(r);
    if (s < 0) return false;
    const auto abs = cambc::self().abilities();
    return abs[std::size_t(s)].is_ready();
}
bool cast_self(Role r)
{
    const int s = slot_of(r);
    if (s < 0 || !ready(r)) return false;
    const auto abs = cambc::self().abilities();
    bool ok = false;
    try { ok = abs[std::size_t(s)].use(); } catch (const std::exception&) { acts += "F" + std::to_string(s); return false; }
    if (ok) acts += "C" + std::to_string(s);
    return ok;
}
bool cast_on(Role r, const champion& t)
{
    const int s = slot_of(r);
    if (s < 0 || !ready(r)) return false;
    const auto abs = cambc::self().abilities();
    bool ok = false;
    try { ok = abs[std::size_t(s)].use(t); } catch (const std::exception&) { acts += "F" + std::to_string(s); return false; }
    if (ok) acts += "C" + std::to_string(s);
    return ok;
}
bool cast_at(Role r, vec2 p)
{
    const int s = slot_of(r);
    if (s < 0 || !ready(r)) return false;
    const auto abs = cambc::self().abilities();
    bool ok = false;
    try { ok = abs[std::size_t(s)].use(p); } catch (const std::exception&) { acts += "F" + std::to_string(s); return false; }
    if (ok) acts += "C" + std::to_string(s) + "@" + std::to_string(p.x) + "," + std::to_string(p.y);
    return ok;
}

// A dash path of up to two 8-directional steps may cross holes but not walls, units or altars.
bool step_free(vec2 q)
{
    if (!cambc::map::is_in_bounds(q)) return false;
    if (cambc::map::walls().is_set(q) || cambc::map::altars().is_set(q)) return false;
    if (cambc::map::spawn_region(team::enemy).is_set(q)) return false;
    const auto occ = cambc::vision::champion_tiles() | cambc::vision::camp_tiles();
    return !occ.is_set(q);
}
bool dash_path_ok(vec2 o, vec2 p)
{
    if (cheb(o, p) <= 1) return true;
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
        {
            const vec2 q{ o.x + dx, o.y + dy };
            if (q == o || cheb(q, p) != 1) continue;
            if (step_free(q)) return true;
        }
    return false;
}

// Dash landing: within Chebyshev 2 of me, free; `toward` = minimise distance to t (prefer exactly
// `want` tiles from t), else maximise distance from t (tie: closer to home).
bool dash(Role r, vec2 t, bool toward, int want, vec2 home)
{
    if (!ready(r)) return false;
    const vec2 o = cambc::self().position();
    vec2 best = o;
    long bestk = 0;
    bool found = false;
    for (int ddx = -2; ddx <= 2; ++ddx)
        for (int ddy = -2; ddy <= 2; ++ddy)
        {
            if (ddx == 0 && ddy == 0) continue;
            const vec2 p{ o.x + ddx, o.y + ddy };
            if (is_blocked(p) || !dash_path_ok(o, p)) continue;
            const int d = cheb(p, t);
            long key;
            if (toward)
                key = long(std::abs(d - want)) * 1000000L + long(d) * 1000L + long(eu2(p, t));
            else
                key = -long(d) * 1000000L + long(eu2(p, home));
            if (!found || key < bestk) { best = p; bestk = key; found = true; }
        }
    if (!found) return false;
    if (toward && cheb(best, t) >= cheb(o, t) && !(r == HDASH && cheb(best, t) <= 1)) return false;
    if (!toward && cheb(best, t) <= cheb(o, t)) return false;
    return cast_at(r, best);
}

bool g_dead = false;  // died during this turn (retaliation): stop acting

int foes_near(vec2 p, const std::vector<Foe>& fs)
{
    int n = 0;
    for (auto& f : fs) n += cheb(f.p, p) <= 1 ? 1 : 0;
    return n;
}
// Area spells (radius 1, range 4): the tile within range covering most foes; ties prefer covering
// the focus target, then the tile nearest it.
vec2 aoe_tile(const std::vector<Foe>& fs, vec2 focus)
{
    const vec2 o = cambc::self().position();
    vec2 best = focus;
    long bk = 0;
    bool found = false;
    for (int x = o.x - 4; x <= o.x + 4; ++x)
        for (int y = o.y - 4; y <= o.y + 4; ++y)
        {
            const vec2 p{ x, y };
            if (!cambc::map::is_in_bounds(p)) continue;
            const int n = foes_near(p, fs);
            if (n == 0) continue;
            const long k = -long(n) * 1000000L - (cheb(p, focus) <= 1 ? 1000L : 0L) + long(eu2(p, focus));
            if (!found || k < bk) { bk = k; best = p; found = true; }
        }
    return best;
}
// Heart's dash: a free landing tile within 2 adjacent to the most foes (ties: the focus target, nearer).
bool dash_aoe(const std::vector<Foe>& fs, vec2 focus)
{
    if (!ready(HDASH)) return false;
    const vec2 o = cambc::self().position();
    vec2 best = o;
    long bk = 0;
    bool found = false;
    for (int ddx = -2; ddx <= 2; ++ddx)
        for (int ddy = -2; ddy <= 2; ++ddy)
        {
            if (ddx == 0 && ddy == 0) continue;
            const vec2 p{ o.x + ddx, o.y + ddy };
            if (is_blocked(p) || !dash_path_ok(o, p)) continue;
            const int n = foes_near(p, fs);
            if (n == 0) continue;
            const long k = -long(n) * 1000000L - (cheb(p, focus) <= 1 ? 1000L : 0L) + long(eu2(p, o));
            if (!found || k < bk) { bk = k; best = p; found = true; }
        }
    if (!found) return false;
    return cast_at(HDASH, best);
}

void attack(const champion& t)
{
    auto& me = cambc::self();
    if (g_dead || !me.can_attack()) return;
    if (!t.is_visible() || !t.is_alive()) return;
    if (cheb(me.position(), t.position()) > me.attack_range()) return;
    me.attack(t);
    acts += "A";
    if (!me.is_alive()) { g_dead = true; acts += "!"; }
}

// Spend an attack reset for a second basic attack on a target still in range.
void reset_attack(const champion& t, bool& cast_used)
{
    auto& me = cambc::self();
    if (g_dead || cast_used || me.can_attack() || !t.is_visible() || !t.is_alive()) return;
    if (cheb(me.position(), t.position()) > me.attack_range()) return;
    Role r = ready(RESET) ? RESET : (ready(ENHRESET) ? ENHRESET : NONE);
    if (r == NONE) return;
    if (cast_self(r))
    {
        cast_used = true;
        if (t.is_alive()) attack(t);
    }
}

// Pick the target: least health among foes within 4, else the nearest. Spread: foe number
// (my slot mod foes) in champion-type order, so a team spreads its damage deterministically.
bool g_spread = false;
int g_slot = 0;
int choose(const std::vector<Foe>& fs)
{
    int best = -1;
    if (g_spread && !fs.empty())
    {
        std::vector<int> order;
        for (int i = 0; i < int(fs.size()); ++i) order.push_back(i);
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            return int(fs[std::size_t(a)].c.champion_name()) < int(fs[std::size_t(b)].c.champion_name());
        });
        return order[std::size_t(g_slot % int(order.size()))];
    }
    for (int i = 0; i < int(fs.size()); ++i)
    {
        if (fs[std::size_t(i)].d > 4) continue;
        if (best < 0 || fs[std::size_t(i)].hp < fs[std::size_t(best)].hp) best = i;
    }
    if (best >= 0) return best;
    for (int i = 0; i < int(fs.size()); ++i)
        if (best < 0 || fs[std::size_t(i)].d < fs[std::size_t(best)].d) best = i;
    return best;
}

vec2 home_tile(int side) { return for_side(side, vec2{ 1, 1 }); }

// Policies ------------------------------------------------------------------
enum Pol { FIGHT = 0, KITE = 1, FLEE = 2, PASSIVE = 3, BASIC = 4, FLEESTUN = 5, HOLD = 6, FLEEHIT = 7,
           SPREAD = 8, BASICSPREAD = 9, STAGGER = 10, BAIT = 11, STANDOFF = 12 };

// Strike reach: the largest distance from which a champion can damage a target this turn (walk 1 then
// melee attack; dash 2 + walk 1 + attack; ranged attack 3, or 4 with a ready damage spell / reset after a walk).
int strike_reach(const champion& c, bool self_view)
{
    const auto k = kit_of(c.champion_name());
    bool dash_ready = false, spell_ready = false, reset_ready = false;
    try
    {
        const auto ab = c.abilities();
        for (int i = 0; i < 2; ++i)
        {
            const bool rdy = ab[std::size_t(i)].cooldown() == 0;
            if (!rdy) continue;
            const Role r = k.r[i];
            if (r == DASH || r == HDASH) dash_ready = true;
            if (r == DMG || r == STUN || r == AOE || r == AOESTUN) spell_ready = true;
            if (r == RESET || r == ENHRESET) reset_ready = true;
        }
    }
    catch (...) {}
    (void)self_view;
    // As this bot fights: a ranged champion strikes from 3, or 4 when a walk-compatible reset (walk,
    // reset, attack) or a damage spell is ready; a melee one from 2 (walk + attack), 4 with a ready dash.
    if (k.ranged) return (spell_ready || reset_ready) ? 4 : 3;
    return dash_ready ? 4 : 2;
}

bool target_stunned(const champion& t)
{
    try
    {
        const auto b = t.buffs();
        return b.has_stun() && b.get<cambc::buff_stun>().duration > 0;
    }
    catch (...) { return false; }
}

// Willow: heal the most injured visible ally within 4 when at least 60% of the heal lands.
bool try_heal()
{
    auto& me = cambc::self();
    if (!ready(HEAL)) return false;
    const int s = slot_of(HEAL);
    const float amount = me.abilities()[std::size_t(s)].heal_amount();
    float best = 0.6f * amount;
    int bi = -1;
    auto allies = cambc::champions(team::ally);
    for (int i = 0; i < int(allies.size()); ++i)
    {
        auto& a = allies[std::size_t(i)];
        if (!a.is_visible() || !a.is_alive() || a.position() == me.position()) continue;
        if (cheb(a.position(), me.position()) > 4) continue;
        const float miss = a.max_health() - a.health();
        if (miss >= best) { best = miss; bi = i; }
    }
    if (bi < 0) return false;
    return cast_on(HEAL, allies[std::size_t(bi)]);
}

vec2 prepos(int side, int index);

void fight_turn(int pol, vec2 rally)
{
    auto& me = cambc::self();
    if (pol == BAIT)
    {
        if (!M.baited)
        {
            int best = -1;
            auto cs = cambc::vision::camps();
            for (int i = 0; i < int(cs.size()); ++i)
                if (cs[std::size_t(i)].health() > 0 &&
                    (best < 0 || cheb(me.position(), cs[std::size_t(i)].position()) < cheb(me.position(), cs[std::size_t(best)].position())))
                    best = i;
            if (best < 0) { M.baited = true; }
            else
            {
                auto c = cs[std::size_t(best)];
                if (cheb(me.position(), c.position()) <= me.attack_range() && me.can_attack())
                {
                    me.attack(c);
                    acts += "W";
                    M.baited = true;
                }
                else if (step_toward(c.position())) acts += "M";
                return;
            }
        }
        const vec2 hold = prepos(M.side, M.slot_index);
        auto fs = foes();
        bool in_range = false;
        for (auto& f : fs) in_range = in_range || cheb(me.position(), f.p) <= me.attack_range();
        if (me.position() != hold && !in_range)
        {
            if (step_toward(hold)) acts += "M";
            return;
        }
        pol = HOLD;
    }
    if (pol == STANDOFF)
    {
        // Hold just outside the enemy's strike reach; strike as soon as it is inside ours.
        auto fs0 = foes();
        if (!fs0.empty())
        {
            int ci = 0;
            for (int i = 1; i < int(fs0.size()); ++i)
                if (fs0[std::size_t(i)].d < fs0[std::size_t(ci)].d) ci = i;
            const int d0 = fs0[std::size_t(ci)].d;
            const int mine = strike_reach(me, true), theirs = strike_reach(fs0[std::size_t(ci)].c, false);
            // The enemy moves twice before my next turn when I move first in this round (A first in odd
            // rounds): one more step of margin then, or its double turn closes the gap and strikes.
            const bool first_mover = (int(cambc::round_number()) % 2 == 1) == (M.side == 0);
            const int margin = (S.misc == 1 && first_mover) ? 2 : 1;
            if (d0 <= mine || d0 <= theirs) pol = FIGHT;  // in reach either way: no reason to wait
            else if (d0 <= theirs + margin) { acts += "H"; return; }
            else pol = FIGHT;  // approach
        }
        else pol = FIGHT;
    }
    g_spread = pol == SPREAD || pol == BASICSPREAD;
    if (pol == SPREAD || pol == STAGGER) pol = pol == SPREAD ? FIGHT : pol;
    if (pol == BASICSPREAD) pol = BASIC;
    const bool stagger = pol == STAGGER;
    if (stagger) pol = FIGHT;
    const auto kit = kit_of(me.champion_name());
    auto fs = foes();
    if (fs.empty())
    {
        if (pol != HOLD && pol != PASSIVE && pol != FLEE && pol != FLEESTUN && pol != FLEEHIT)
            if (step_toward(rally)) acts += "M";
        return;
    }
    int ti = choose(fs);
    champion t = fs[std::size_t(ti)].c;
    vec2 tp = fs[std::size_t(ti)].p;
    int d = cheb(me.position(), tp);
    bool cast_used = false;
    const bool abilities = pol != BASIC;
    const vec2 home = home_tile(M.side);

    if (pol == PASSIVE) return;
    if (abilities && pol != FLEE && pol != FLEESTUN && pol != FLEEHIT && try_heal()) cast_used = true;

    if (pol == FLEE || pol == FLEESTUN || pol == FLEEHIT)
    {
        // Nearest foe is the chaser.
        int ci = 0;
        for (int i = 1; i < int(fs.size()); ++i)
            if (fs[std::size_t(i)].d < fs[std::size_t(ci)].d) ci = i;
        champion c = fs[std::size_t(ci)].c;
        vec2 cp = fs[std::size_t(ci)].p;
        int cd = cheb(me.position(), cp);
        if (pol == FLEESTUN && cd <= 4)
        {
            if (ready(STUN) && cast_on(STUN, c)) cast_used = true;
            else if (ready(AOESTUN) && cast_at(AOESTUN, cp)) cast_used = true;
        }
        if (pol == FLEEHIT && cd <= me.attack_range() && !kit.ranged) attack(c);
        if (!cast_used && cd <= 3)
        {
            Role dr = ready(DASH) ? DASH : (ready(HDASH) ? HDASH : NONE);
            if (dr != NONE && dash(dr, cp, false, 0, home)) { cast_used = true; acts += "D"; }
        }
        if (step_away(cp, home)) acts += "M";
        if (pol == FLEEHIT && kit.ranged && me.can_attack())
        {
            cp = c.is_visible() ? c.position() : cp;
            if (cheb(me.position(), cp) <= me.attack_range()) attack(c);
        }
        if (pol == FLEEHIT && !kit.ranged && me.can_attack() && c.is_visible() && cheb(me.position(), c.position()) <= 1) attack(c);
        return;
    }

    // Heart's dash lands damage and a stun on every enemy adjacent to the landing tile: open with it.
    if (abilities && ready(HDASH) && d <= 3)
        if (dash_aoe(fs, tp)) { cast_used = true; acts += "D"; }
    // Shields before contact.
    if (abilities && !cast_used && d <= 2 && ready(SHIELD) && cast_self(SHIELD)) cast_used = true;

    // Spells in range 4. Melee casts walking-exclusive spells only when already adjacent.
    if (abilities && !cast_used && d <= 4)
    {
        const bool can_cast_excl = kit.ranged || d <= 1;
        const bool hold_stun = stagger && target_stunned(t);
        if (!hold_stun && ready(STUN) && (kit.ranged ? true : can_cast_excl) && cast_on(STUN, t)) cast_used = true;
        else if (!hold_stun && ready(AOESTUN) && can_cast_excl && cast_at(AOESTUN, aoe_tile(fs, tp))) cast_used = true;
        else if (ready(DMG) && can_cast_excl && cast_on(DMG, t)) cast_used = true;
        else if (ready(AOE) && can_cast_excl && cast_at(AOE, aoe_tile(fs, tp))) cast_used = true;
    }
    // Melee dash to close.
    if (abilities && !cast_used && !kit.ranged && ready(DASH) && d >= 2 && d <= 4 && pol != HOLD)
        if (dash(DASH, tp, true, 1, home)) { cast_used = true; acts += "D"; }

    d = t.is_visible() ? cheb(me.position(), t.position()) : d;
    // Kiting ranged champions step back when a foe is within 2.
    if (pol == KITE && kit.ranged)
    {
        int nd = 99;
        vec2 np{};
        for (auto& f : fs)
        {
            const int fd = cheb(me.position(), f.p);
            if (fd < nd) { nd = fd; np = f.p; }
        }
        if (nd <= 2)
        {
            // Kite towards a neutral corner (A frame: bottom-left), away from home and the fillers.
            const vec2 anchor = for_side(M.side, vec2{ 0, H - 1 });
            if (abilities && !cast_used && nd <= 1 && ready(DASH) && dash(DASH, np, false, 0, anchor)) { cast_used = true; acts += "D"; }
            if (step_away(np, anchor)) acts += "M";
        }
    }
    else if (pol != HOLD)
    {
        const int reach = kit.ranged ? me.attack_range() : 1;
        if (d > reach && step_toward(tp)) acts += "M";
    }
    if (!t.is_visible() || !t.is_alive())
    {
        // retarget after movement
        fs = foes();
        if (fs.empty()) return;
        ti = choose(fs);
        t = fs[std::size_t(ti)].c;
    }
    attack(t);
    if (abilities) reset_attack(t, cast_used);
}


// Farm the gold block of my side (A frame x 4..8, y 4..8) for a build.
bool in_block(vec2 p)
{
    const vec2 a = M.side == 0 ? p : mir(p);
    return a.x >= 4 && a.x <= 8 && a.y >= 4 && a.y <= 8;
}
int block_alive_count(std::vector<cambc::camp>& out)
{
    out.clear();
    for (auto& c : cambc::vision::camps())
        if (c.is_alive() && c.health() > 0 && in_block(c.position())) out.push_back(c);
    return int(out.size());
}
void farm_turn(bool helper)
{
    auto& me = cambc::self();
    std::vector<cambc::camp> cs;
    const int n = block_alive_count(cs);
    const vec2 o = me.position();
    if (n == 0) return;
    if (helper && n <= 3) return;  // leave the last kills (and the capture) to the duelist
    // nearest alive cluster
    int bi = 0;
    for (int i = 1; i < n; ++i)
        if (cheb(o, cs[std::size_t(i)].position()) < cheb(o, cs[std::size_t(bi)].position())) bi = i;
    auto tgt = cs[std::size_t(bi)];
    if (cheb(o, tgt.position()) > me.attack_range())
    {
        // helpers never step onto block tiles
        if (helper)
        {
            const vec2 tp = tgt.position();
            vec2 best = o;
            int bd = cheb(o, tp);
            static const int dx[] = { 1, -1, 0, 0, 1, 1, -1, -1 };
            static const int dy[] = { 0, 0, 1, -1, 1, -1, 1, -1 };
            for (int i = 0; i < 8; ++i)
            {
                const vec2 p{ o.x + dx[i], o.y + dy[i] };
                if (is_blocked(p) || in_block(p)) continue;
                if (cheb(p, tp) < bd) { bd = cheb(p, tp); best = p; }
            }
            if (best != o) me.move_absolute(best);
        }
        else
            step_toward(tgt.position());
    }
    if (me.can_attack() && cheb(me.position(), tgt.position()) <= me.attack_range()) me.attack(tgt);
}

vec2 prepos(int side, int index)
{
    // A-frame placements; side B mirrors.  W = 41, H = 30 expected (centre column x = 20).
    static const vec2 duel_a[] = { { 20, 14 }, { 20, 13 }, { 19, 14 }, { 18, 14 }, { 17, 14 }, { 20, 12 }, { 16, 14 }, { 0, 0 } };
    if (S.table) return table_prepos(S.tindex, side, index, W, H);
    if (S.place == 7)
    {
        // Chase: the fleer stands L tiles (diagonal) from its home corner, the chaser one tile behind it.
        const int L = 3 * S.misc + 3;
        const int fleer = S.s[0].pol == FLEE || S.s[0].pol == FLEESTUN || S.s[0].pol == FLEEHIT ? 0 : 1;
        const vec2 fa{ 2 + L, 2 + L }, ca{ 3 + L, 3 + L };
        return side == fleer ? for_side(fleer, fa) : for_side(fleer, ca);
    }
    return for_side(side, duel_a[S.place]);
}

void log_stats()
{
    auto& me = cambc::self();
    char buf[200];
    std::snprintf(buf, sizeof buf, "S r=%d t=%c c=%s mh=%.1f h=%.1f s=%.2f f=%.2f rg=%d g=%u b=%d",
                  int(cambc::round_number()), M.side ? 'B' : 'A', short_name(me.champion_name()), me.max_health(),
                  me.health(), me.strength(), me.focus(), int(me.attack_range()), unsigned(me.gold_count()), M.build);
    std::cout << buf << '\n';
}

void log_turn(const char* tag)
{
    auto& me = cambc::self();
    float sh = 0;
    bool st = false;
    try
    {
        const auto b = me.buffs();
        if (b.has_shield()) sh = b.get<cambc::buff_shield>().value;
        st = b.has_stun();
    }
    catch (...) {}
    std::string es;
    for (auto& c : cambc::champions(team::enemy))
    {
        if (!c.is_visible()) continue;
        char e[64];
        const vec2 p = c.position();
        std::snprintf(e, sizeof e, " %s:%.1f@%d,%d", short_name(c.champion_name()), c.health(), p.x, p.y);
        es += e;
    }
    const vec2 p = me.position();
    char buf[160];
    std::snprintf(buf, sizeof buf, "%s r=%d t=%c c=%s hp=%.1f sh=%.1f p=%d,%d st=%d cd=%d,%d", tag, int(cambc::round_number()),
                  M.side ? 'B' : 'A', short_name(me.champion_name()), me.health(), sh, p.x, p.y, st ? 1 : 0,
                  int(me.abilities()[0].cooldown()), int(me.abilities()[1].cooldown()));
    std::cout << buf << " a=" << (acts.empty() ? "-" : acts) << " |" << es << '\n';
}

void play_turn()
{
    decode();
    auto& me = cambc::self();
    if (!M.init)
    {
        M.init = true;
        M.side = MYTEAM;
        const auto t = me.champion_name();
        if (S.table)
        {
            M.unit = table_unit(S.tindex, M.side, t);
            M.duelist = M.unit.active;
            M.pol = M.unit.pol;
            M.build = M.unit.build;
            M.slot_index = M.unit.slot;
        }
        else
        {
            M.duelist = pick_is(S.s[M.side].pick, t);
            M.pol = S.s[M.side].pol;
            M.build = M.duelist ? S.s[M.side].build : 0;
            M.slot_index = (t == champion_type::issac) ? 1 : 0;
        }
        M.phase = (M.duelist && M.build) ? 0 : 3;
        // Slot marker (wall at (W-1, 10), mirrored to (0, H-11)): team B's fillers take priority 5, so
        // B's duelist acts after them (turn slot 3 against A's slot 1; mechanics M2).
        if (cambc::map::walls().is_set(vec2{ W - 1, 10 }) && M.side == 1 && !M.duelist) me.set_priority(5);
    }
    acts.clear();
    g_dead = false;
    g_slot = M.slot_index;
    const int r = int(cambc::round_number());
    if (!me.is_alive())
    {
        if (M.duelist && !M.dead_logged && r >= S.start)
        {
            M.dead_logged = true;
            std::cout << "X r=" << r << " t=" << (M.side ? 'B' : 'A') << " c=" << short_name(me.champion_name()) << " dead\n";
        }
        return;
    }
    // Helpers: farm for my side's duelist build, otherwise rest on spawn.
    if (!M.duelist)
    {
        bool my_build = false;
        if (S.table) my_build = table_side_build(S.tindex, M.side);
        else my_build = S.s[M.side].build != 0;
        if (my_build && r < S.start - 40) farm_turn(true);
        else if (!cambc::map::spawn_region(team::ally).is_set(me.position())) step_toward(home_tile(M.side));
        return;
    }
    if (M.phase == 0)
    {
        std::vector<cambc::camp> cs;
        if (block_alive_count(cs) > 0 || r < 3) { farm_turn(false); return; }
        M.phase = 1;
    }
    if (M.phase == 1)
    {
        // stand on a block tile until the capture pays
        if (int(me.gold_count()) >= build_cost(M.build) || M.capture_wait > 12) M.phase = 2;
        else
        {
            if (!in_block(me.position()))
            {
                const vec2 o = me.position();
                vec2 best = o;
                int bd = 99;
                for (int x = 4; x <= 8; ++x)
                    for (int y = 4; y <= 8; ++y)
                    {
                        const vec2 p = for_side(M.side, vec2{ x, y });
                        if (!is_blocked(p) && cheb(o, p) < bd) { bd = cheb(o, p); best = p; }
                    }
                step_toward(best);
            }
            else ++M.capture_wait;
            return;
        }
    }
    if (M.phase == 2)
    {
        if (!cambc::map::spawn_region(team::ally).is_set(me.position())) { step_toward(home_tile(M.side)); return; }
        for (auto& b : build_items(M.build))
        {
            const cambc::item_type it{ std::uint32_t(b.item) };
            for (int k = 0; k < b.count; ++k)
                if (me.can_buy(it, 1)) me.buy(it, 1);
        }
        M.phase = 3;
    }
    if (r < S.start)
    {
        const vec2 pp = prepos(M.side, M.slot_index);
        if (me.position() != pp) step_toward(pp);
        return;
    }
    if (!M.logged_stats) { M.logged_stats = true; log_stats(); }
    if (r > S.start + 70) return;
    const bool stunned = !me.can_move() && !me.can_attack() && !me.can_cast();
    if (!stunned) fight_turn(M.pol, prepos(1 - M.side, M.slot_index));
    if (g_dead || !me.is_alive())
    {
        std::cout << "D r=" << r << " t=" << (M.side ? 'B' : 'A') << " c=" << short_name(me.champion_name())
                  << " hp=0.0 sh=0.0 p=-1,-1 st=0 cd=0,0 a=" << acts << " | died-in-turn\n";
        return;
    }
    log_turn(stunned ? "D*" : "D");
}

}  // namespace

int main()
{
    while (true)
    {
        try
        {
            if (cambc::is_draft()) play_draft();
            else play_turn();
        }
        catch (const std::exception& e)
        {
            std::cout << "duel error: " << e.what() << '\n';
        }
        catch (...)
        {
            std::cout << "duel error: unknown\n";
        }
        cambc::end_turn();
    }
}
