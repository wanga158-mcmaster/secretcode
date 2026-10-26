// Imitation data from top-vs-top replays (DESIGN.md 3.1): replays a decoded game (tools/totext.py) round by round,
// rebuilds each champion's observation at its turn (its own vision by shadowcasting, items from shop events, camp
// health from hits, captures and respawns, banked gems), computes the candidates and facts with the bot's own core
// code, and writes one record per decision labelled with what the champion did.
//   replay_obs IN.txt OUT.bin
// Record (little-endian): int32 head, n, label, kphi, team, round, won, rank_self, rank_opp; float state[48];
// float phi[n * kphi].
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include "../src/core/cand.h"
#include "../src/core/catalog.h"
#include "../src/core/map.h"
#include "../src/core/moves.h"
#include "../src/core/obs.h"
#include "../src/core/rules.h"

using namespace zero;

namespace {

struct Sample { int x, y; float hp, gold, gems; };

struct Champ
{
    int actor{}, team{}, type{};
    std::map<int, Sample> s;  // by round
    std::array<std::uint8_t, kMaxItems> items{};
    float max_hp{};
    int cooldown[2]{};
    bool recalling{}, tributing{};
    bool standing{};  // a recall started at the end of last turn after other actions (the tail's pre-start)
    Memory mem;
    int goal_emit{ -100 };
    int last_goal_kind{ -1 }, last_goal_target{ -1 };
};

struct Ev
{
    int round{}, actor{};
    std::string kind;
    std::vector<double> v;  // numeric fields (entity kinds encoded: camp 0, champion 1, other 2)
};

struct Camp { int group{}, member{}; float hp{}; int last_hit{ -100 }; };

struct Game
{
    Map map;
    std::map<int, int> group_by_native;          // group native id -> index
    std::map<int, std::pair<int, int>> member_by_native;  // camp native id -> (group, member)
    std::vector<std::vector<float>> member_hp;   // per group per member
    std::vector<int> respawn_round;              // per group, -1 alive
    std::vector<int> member_hit;                 // flat last hit round per (group,member) via offsets
    std::vector<int> offset;
    std::map<int, Champ> champs;
    std::map<int, std::vector<Ev>> events;       // by round
    int winner{ -1 }, rank[2]{ -1, -1 };
    std::uint32_t banked[2]{};
    int last_round{};
};

bool load(const char* path, Game& g)
{
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line))
    {
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "MAP") { ss >> g.map.w >> g.map.h; g.map.flags.assign(static_cast<std::size_t>(g.map.w * g.map.h), 0); }
        else if (tag == "FLAGS")
        {
            std::string f;
            ss >> f;
            for (std::size_t i = 0; i < f.size() && i < g.map.flags.size(); ++i)
            {
                const int v = f[i] <= '9' ? f[i] - '0' : f[i] - 'a' + 10;
                std::uint8_t b = 0;
                if (v & 1) b |= tf_wall;
                if (v & 2) b |= tf_hole;
                if (v & 4) b |= tf_bush;
                if (v & 8) b |= tf_altar;
                g.map.flags[i] = b;
            }
        }
        else if (tag == "SPAWN")
        {
            int team, n;
            ss >> team >> n;
            for (int i = 0; i < n; ++i)
            {
                int x, y;
                ss >> x >> y;
                g.map.spawn[team].push_back({ static_cast<std::int16_t>(x), static_cast<std::int16_t>(y) });
                g.map.flags[static_cast<std::size_t>(y * g.map.w + x)] |= team == 0 ? tf_spawn_a : tf_spawn_b;
            }
        }
        else if (tag == "ALTARS")
        {
            int n;
            ss >> n;
            for (int i = 0; i < n; ++i) { int x, y; ss >> x >> y; g.map.altars.push_back({ static_cast<std::int16_t>(x), static_cast<std::int16_t>(y) }); }
        }
        else if (tag == "GROUP")
        {
            int native, n;
            Group gr;
            ss >> native >> gr.gold >> gr.gems >> gr.respawn >> n;
            const int gi = static_cast<int>(g.map.groups.size());
            g.group_by_native[native] = gi;
            for (int i = 0; i < n; ++i)
            {
                int cn, x, y, type;
                ss >> cn >> x >> y >> type;
                g.member_by_native[cn] = { gi, i };
                gr.members.push_back({ { static_cast<std::int16_t>(x), static_cast<std::int16_t>(y) }, static_cast<std::uint8_t>(type) });
            }
            g.map.groups.push_back(gr);
        }
        else if (tag == "CHAMP")
        {
            Champ c;
            ss >> c.actor >> c.team >> c.type;
            c.max_hp = catalog().champion(c.type).health;
            g.champs[c.actor] = c;
        }
        else if (tag == "S")
        {
            int actor, round;
            Sample s{};
            ss >> actor >> round >> s.x >> s.y >> s.hp >> s.gold >> s.gems;
            g.champs[actor].s[round] = s;
            g.last_round = std::max(g.last_round, round);
        }
        else if (tag == "E")
        {
            Ev e;
            ss >> e.round >> e.actor >> e.kind;
            std::string tok;
            while (ss >> tok)
            {
                if (tok == "camp") e.v.push_back(0);
                else if (tok == "champion") e.v.push_back(1);
                else if (tok == "none" || tok == "altar" || tok == "lock" || tok == "ward") e.v.push_back(2);
                else e.v.push_back(std::atof(tok.c_str()));
            }
            g.events[e.round].push_back(e);
        }
        else if (tag == "WIN") { std::string r; ss >> g.winner >> r >> g.rank[0] >> g.rank[1]; }
    }
    g.map.finish();
    for (const auto& gr : g.map.groups)
    {
        std::vector<float> hp;
        for (const auto& m : gr.members) hp.push_back(catalog().camp(m.camp_type).health);
        g.member_hp.push_back(hp);
    }
    g.respawn_round.assign(g.map.groups.size(), -1);
    int off = 0;
    for (const auto& gr : g.map.groups) { g.offset.push_back(off); off += static_cast<int>(gr.members.size()); }
    g.member_hit.assign(static_cast<std::size_t>(off), -100);
    return true;
}

// Stats from design + items (shards add growth x points; emblems count their points).
void stats(const Champ& c, float& strength, float& focus, float& max_hp)
{
    const auto& d = catalog().champion(c.type);
    float hp = 0, st = 0, fo = 0;
    for (int i = 0; i < catalog().n_items && i < kMaxItems; ++i)
    {
        const auto n = c.items[static_cast<std::size_t>(i)];
        if (!n) continue;
        const auto& it = catalog().item(i);
        hp += static_cast<float>(n * it.health);
        st += static_cast<float>(n * it.strength);
        fo += static_cast<float>(n * it.focus);
    }
    max_hp = d.health + hp * d.health_growth;
    strength = d.strength + st * d.strength_growth;
    focus = d.focus + fo * d.focus_growth;
    const auto& p = catalog().passives[d.passive];
    if (p.type == 5) strength += p.magnitude * (max_hp - d.health);  // Ryth: strength from bonus health
}

struct Out
{
    FILE* f{};
    int records{};
    int kept[h_count]{}, dropped[h_count]{}, single[h_count]{};
    int ranged_complex{}, ranged_move_attack{}, ranged_multiple{};
    void write(int head, const CandSet& set, int label, const float* sv, int team, int round, int won, int rs, int ro)
    {
        if (set.n < 2) { ++single[head]; return; }
        if (label < 0 || label >= set.n) { ++dropped[head]; return; }
        ++kept[head];
        const int kphi = kPhi[head];
        const int hdr[9] = { head, set.n, label, kphi, team, round, won, rs, ro };
        std::fwrite(hdr, sizeof(int), 9, f);
        std::fwrite(sv, sizeof(float), kState, f);
        for (int i = 0; i < set.n; ++i) std::fwrite(set.c[i].phi, sizeof(float), static_cast<std::size_t>(kphi), f);
        ++records;
    }
};

// Whether a recall started at round q completed: the champion teleports onto its spawn region within the
// recall window (a jump of more than one tile from a living position), not a pre-start later cancelled.
bool recall_completes(const Game& g, const Champ& c, int q)
{
    const auto flag = c.team == 0 ? tf_spawn_a : tf_spawn_b;
    for (int k = q + 1; k <= q + 6 && k <= g.last_round; ++k)
    {
        auto a = c.s.find(k - 1), b = c.s.find(k);
        if (a == c.s.end() || b == c.s.end() || a->second.x < 0 || b->second.x < 0) continue;
        const Tile ta{ static_cast<std::int16_t>(a->second.x), static_cast<std::int16_t>(a->second.y) };
        const Tile tb{ static_cast<std::int16_t>(b->second.x), static_cast<std::int16_t>(b->second.y) };
        if (cheb(ta, tb) > 1 && (g.map.at(tb) & flag)) return true;
        if (ta != tb) return false;  // moved: the channel was cancelled
    }
    return false;
}

// Whether the champion stays on its spawn region (alive) for the rounds (r, r + n].
bool stays_on_spawn(const Game& g, const Champ& c, int r, int n)
{
    const auto flag = c.team == 0 ? tf_spawn_a : tf_spawn_b;
    for (int k = r + 1; k <= r + n && k <= g.last_round; ++k)
    {
        auto it = c.s.find(k);
        if (it == c.s.end() || it->second.x < 0) continue;
        if (!(g.map.at({ static_cast<std::int16_t>(it->second.x), static_cast<std::int16_t>(it->second.y) }) & flag)) return false;
    }
    return true;
}

// The goal a champion pursued from round r: the first decisive event in (r, r + horizon]. A recall counts only when
// it completes (experts pre-start a recall at most turn ends and cancel it next turn).
void infer_goal(const Game& g, const Champ& c, int r, int horizon, int& kind, int& target)
{
    kind = -1;
    target = -1;
    for (int q = r; q <= r + horizon && q <= g.last_round; ++q)
    {
        auto it = g.events.find(q);
        if (it == g.events.end()) continue;
        for (const auto& e : it->second)
        {
            if (e.kind == "capture" && e.v.size() >= 2)
            {
                const int n = static_cast<int>(e.v[1]);
                for (int k = 0; k < n; ++k)
                    if (static_cast<int>(e.v[2 + 3 * k]) == c.actor)
                    {
                        auto gi = g.group_by_native.find(static_cast<int>(e.v[0]));
                        if (gi != g.group_by_native.end()) { kind = g_farm; target = gi->second; return; }
                    }
            }
            if (e.actor != c.actor) continue;
            if (e.kind == "recall" && recall_completes(g, c, q)) { kind = g_recall; return; }
            if (e.kind == "tribute")
            {
                auto si = c.s.find(q);
                if (si != c.s.end())
                {
                    int best = -1, bd = 1 << 20;
                    for (std::size_t a = 0; a < g.map.altars.size(); ++a)
                    {
                        const int d = cheb(g.map.altars[a], { static_cast<std::int16_t>(si->second.x), static_cast<std::int16_t>(si->second.y) });
                        if (d < bd) { bd = d; best = static_cast<int>(a); }
                    }
                    kind = g_bank;
                    target = best;
                    return;
                }
            }
            if (e.kind == "attack" && e.v.size() >= 2)
            {
                if (e.v[0] == 0)
                {
                    auto m = g.member_by_native.find(static_cast<int>(e.v[1]));
                    if (m != g.member_by_native.end()) { kind = g_farm; target = m->second.first; return; }
                }
                else if (e.v[0] == 1) { kind = g_engage; target = static_cast<int>(e.v[1]); return; }
            }
            if (e.kind == "cast" && e.v.size() >= 4)
                for (int k = 0; k < static_cast<int>(e.v[3]); ++k)
                    if (e.v[4 + 4 * k] == 1 && g.champs.count(static_cast<int>(e.v[5 + 4 * k])) &&
                        g.champs.at(static_cast<int>(e.v[5 + 4 * k])).team != c.team)
                    {
                        kind = g_engage;
                        target = static_cast<int>(e.v[5 + 4 * k]);
                        return;
                    }
        }
    }
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "usage: replay_obs IN.txt OUT.bin\n"); return 2; }
    Game g;
    if (!load(argv[1], g)) { std::fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    Out out;
    out.f = std::fopen(argv[2], "wb");
    if (!out.f) return 1;
    BfsPaths paths0(g.map, 0), paths1(g.map, 1);
    const auto& cat = catalog();
    std::vector<std::uint8_t> seen;
    for (int r = 1; r <= g.last_round; ++r)
    {
        // Respawns due this round.
        for (std::size_t gi = 0; gi < g.map.groups.size(); ++gi)
            if (g.respawn_round[gi] >= 0 && r >= g.respawn_round[gi])
            {
                g.respawn_round[gi] = -1;
                for (std::size_t m = 0; m < g.member_hp[gi].size(); ++m) g.member_hp[gi][m] = cat.camp(g.map.groups[gi].members[m].camp_type).health;
            }
        const auto ev_it = g.events.find(r);
        static const std::vector<Ev> none;
        const auto& evs = ev_it != g.events.end() ? ev_it->second : none;
        for (auto& [actor, c] : g.champs)
        {
            auto si = c.s.find(r);
            if (si == c.s.end()) continue;
            bool took_turn = false;
            for (const auto& e : evs) took_turn |= e.actor == actor && e.kind == "end";
            if (!took_turn) continue;
            const Sample& me = si->second;
            const Tile here{ static_cast<std::int16_t>(std::max(0, me.x)), static_cast<std::int16_t>(std::max(0, me.y)) };
            const bool on_spawn = me.x >= 0 && (g.map.at(here) & (c.team == 0 ? tf_spawn_a : tf_spawn_b));
            bool ordinary_action = false;
            for (const auto& e : evs)
                ordinary_action |= e.actor == actor && (e.kind == "move" || e.kind == "attack" || e.kind == "cast");
            // A dead/spawn sample ends an old channel. A proven ordinary action means any committed channel
            // was cancelled or interrupted before that action; do not discard that whole actor-turn as channelled.
            // A standing tail recall is different: decisions still see its saved turn end before choosing an action.
            if (me.x < 0 || on_spawn)
            {
                c.recalling = c.tributing = c.standing = false;
            }
            else if (ordinary_action) c.recalling = c.tributing = false;
            if (me.gems <= 0.0f) c.tributing = false;
            Obs o;
            o.round = r;
            o.team = c.team;
            o.alive = me.x >= 0;
            o.self.id = actor;
            o.self.type = static_cast<std::uint8_t>(c.type);
            o.self.at = here;
            o.self.hp = me.hp;
            o.self.gold = static_cast<std::uint32_t>(me.gold);
            o.self.gems = static_cast<std::uint32_t>(me.gems);
            stats(c, o.self.strength, o.self.focus, o.self.max_hp);
            o.self.cooldown[0] = static_cast<std::int8_t>(c.cooldown[0]);
            o.self.cooldown[1] = static_cast<std::int8_t>(c.cooldown[1]);
            o.self.recalling = c.recalling;
            o.self.tributing = c.tributing;
            o.recall_started = c.standing;
            o.items = c.items;
            o.can_move = o.can_attack = o.can_cast = o.alive && !c.recalling && !c.tributing;
            o.on_spawn = on_spawn;
            o.banked_us = g.banked[c.team];
            o.banked_them = g.banked[1 - c.team];
            if (o.alive) rules::vision(g.map, o.self.at, static_cast<int>(cat.champion(c.type).vision), seen);
            for (auto& [oa, oc] : g.champs)
            {
                if (oa == actor) continue;
                auto os = oc.s.find(r);
                const bool alive = os != oc.s.end() && os->second.x >= 0;
                if (oc.team == c.team) o.allies_alive += alive ? 1 : 0; else o.enemies_alive += alive ? 1 : 0;
                if (!alive || !o.alive) continue;
                const Tile t{ static_cast<std::int16_t>(os->second.x), static_cast<std::int16_t>(os->second.y) };
                if (!seen[static_cast<std::size_t>(g.map.idx(t))]) continue;
                UnitView v;
                v.id = oa;
                v.type = static_cast<std::uint8_t>(oc.type);
                v.at = t;
                v.hp = os->second.hp;
                stats(oc, v.strength, v.focus, v.max_hp);
                v.gems = static_cast<std::uint32_t>(os->second.gems);
                v.cooldown[0] = static_cast<std::int8_t>(oc.cooldown[0]);
                v.cooldown[1] = static_cast<std::int8_t>(oc.cooldown[1]);
                v.recalling = oc.recalling;
                v.tributing = oc.tributing;
                (oc.team == c.team ? o.allies : o.enemies).push_back(v);
            }
            o.allies_alive += o.alive ? 1 : 0;
            o.group_status.assign(g.map.groups.size(), gs_unknown);
            o.altar_lock.assign(g.map.altars.size(), lk_none);
            for (std::size_t gi = 0; gi < g.map.groups.size() && o.alive; ++gi)
            {
                bool any_seen = false, any_alive = false;
                for (std::size_t m = 0; m < g.map.groups[gi].members.size(); ++m)
                {
                    const Tile t = g.map.groups[gi].members[m].at;
                    const bool vis = seen[static_cast<std::size_t>(g.map.idx(t))] != 0;
                    any_seen |= vis;
                    const bool alive = g.respawn_round[gi] < 0 && g.member_hp[gi][m] > 0.0f;
                    any_alive |= alive;
                    if (vis && alive)
                    {
                        CampView cv;
                        cv.at = t;
                        cv.type = g.map.groups[gi].members[m].camp_type;
                        cv.hp = g.member_hp[gi][m];
                        cv.awake = r - g.member_hit[static_cast<std::size_t>(g.offset[gi]) + m] <= 3;
                        cv.group = static_cast<int>(gi);
                        o.camps.push_back(cv);
                    }
                }
                if (any_seen) o.group_status[gi] = g.respawn_round[gi] >= 0 ? gs_respawning : (any_alive ? gs_alive : gs_reward);
            }
            // Allies' claims: their inferred goals (what the shared-memory protocol would carry).
            int slot = 0;
            for (auto& [oa, oc] : g.champs)
            {
                if (oc.team != c.team || slot >= 8) continue;
                if (oa != actor && oc.last_goal_kind >= 0)
                {
                    o.claim_kind[static_cast<std::size_t>(slot)] = static_cast<std::int8_t>(oc.last_goal_kind);
                    o.claim_target[static_cast<std::size_t>(slot)] = static_cast<std::int16_t>(oc.last_goal_target);
                }
                ++slot;
            }
            int dash = 0;
            for (int k = 0; k < 2; ++k)
                if (cat.champion(c.type).ability[k].kind == ab_dash && c.cooldown[k] == 0) dash = std::max(dash, static_cast<int>(cat.champion(c.type).ability[k].range));
            legal_moves(g.map, o, dash);
            observe(o, g.map, c.mem);
            Context ctx{ &g.map, c.team == 0 ? static_cast<Paths*>(&paths0) : static_cast<Paths*>(&paths1), &o, &c.mem };
            const int won = g.winner < 0 ? -1 : (g.winner == c.team ? 1 : 0);
            const int rs = g.rank[c.team], ro = g.rank[1 - c.team];
            // Teacher forcing: the goal the champion pursued (inferred) drives the goal-relative step facts.
            int gk, gt;
            // Near events first; a champion walking further (e.g. out of spawn) is labelled by its first decisive
            // event within the walk, and Hold only when it stays on its spawn region meanwhile.
            infer_goal(g, c, r, 20, gk, gt);
            if (gk < 0 && !stays_on_spawn(g, c, r, 20)) infer_goal(g, c, r, 80, gk, gt);
            else if (gk < 0 && o.on_spawn) gk = g_hold;
            float sv[kState];
            CandSet set;
            if (o.alive && gk >= 0 && (gk != c.last_goal_kind || gt != c.last_goal_target || r - c.goal_emit >= 4))
            {
                state_vector(ctx, sv);
                goal_candidates(ctx, set);
                int label = -1;
                for (int i = 0; i < set.n; ++i)
                    if (set.c[i].kind == gk && (gk == g_recall || gk == g_hold || gk == g_avoid || set.c[i].target == gt)) label = i;
                out.write(h_goal, set, label, sv, c.team, r, won, rs, ro);
                c.goal_emit = r;
            }
            if (gk >= 0)
            {
                if (gk != c.mem.goal_kind || gt != c.mem.goal_target) c.mem.goal_since = r;
                c.mem.goal_kind = gk;
                c.mem.goal_target = gt;
                c.last_goal_kind = gk;
                c.last_goal_target = gt;
                // The bot's tail hints read the goal head's recall probability; the teacher's goal stands in.
                c.mem.goal_recall_p = gk == g_recall ? 1.0f : 0.0f;
                c.mem.goal_decided = r;
            }
            // Actions this turn.
            const Ev* move = nullptr;
            const Ev* attack = nullptr;
            const Ev* cast = nullptr;
            const Ev* shop = nullptr;
            int moves = 0, attacks = 0;
            bool recall = false, tribute = false;
            for (const auto& e : evs)
            {
                if (e.actor != actor) continue;
                if (e.kind == "move") { ++moves; if (!move) move = &e; }
                if (e.kind == "attack") { ++attacks; if (!attack) attack = &e; }
                if (e.kind == "cast" && !cast) cast = &e;
                if (e.kind == "shop") shop = &e;
                recall |= e.kind == "recall" && !c.recalling;
                tribute |= e.kind == "tribute" && !c.tributing;
            }
            // A recall after this turn's other actions is the tail's pre-start (step label from the actions);
            // a pre-start left standing through a turn without actions was kept (step label recall).
            const bool pre_start = recall && (move || attack || cast);
            if (c.standing && !move && !attack && !cast && !tribute) recall = true;
            state_vector(ctx, sv);
            // Shop: one record per purchased item (in item order), then stop.
            if (shop && shop->v.size() >= 2)
            {
                std::array<std::uint8_t, kMaxItems> after{};
                const int n = static_cast<int>(shop->v[1]);
                for (int k = 0; k < n; ++k)
                {
                    const int id = static_cast<int>(shop->v[2 + 2 * k]);
                    if (id >= 0 && id < kMaxItems) after[static_cast<std::size_t>(id)] = static_cast<std::uint8_t>(shop->v[3 + 2 * k]);
                }
                for (int id = 0; id < cat.n_items && id < kMaxItems; ++id)
                {
                    // Bought shards: more than before, or an emblem formed from them.
                    // Shards bought of this item: its count's increase plus combine_count per new emblem formed from it.
                    const auto& it = cat.item(id);
                    int bought = static_cast<int>(after[static_cast<std::size_t>(id)]) - static_cast<int>(o.items[static_cast<std::size_t>(id)]);
                    if (it.result >= 0 && it.result < kMaxItems)
                        bought += (static_cast<int>(after[static_cast<std::size_t>(it.result)]) - static_cast<int>(o.items[static_cast<std::size_t>(it.result)])) *
                                  static_cast<int>(it.combine_count);
                    bought = std::max(0, bought);
                    for (int b = 0; b < bought && b < 10; ++b)
                    {
                        shop_candidates(ctx, set);
                        int label = -1;
                        for (int i = 0; i < set.n; ++i) if (set.c[i].kind == 1 && set.c[i].target == id) label = i;
                        out.write(h_shop, set, label, sv, c.team, r, won, rs, ro);
                        if (label < 0) break;
                        o.self.gold -= std::min(o.self.gold, it.cost);
                        auto& cnt = o.items[static_cast<std::size_t>(id)];
                        ++cnt;
                        if (it.combine_count > 0 && cnt >= it.combine_count && it.result >= 0) { cnt = static_cast<std::uint8_t>(cnt - it.combine_count); ++o.items[static_cast<std::size_t>(it.result)]; }
                    }
                }
                shop_candidates(ctx, set);
                out.write(h_shop, set, 0, sv, c.team, r, won, rs, ro);  // stop
                c.items = after;
                if (shop->v[0] > 0) c.max_hp = static_cast<float>(shop->v[0]);
            }
            if (o.alive && !c.recalling && !c.tributing)
            {
                // Casts (both slots offered; the cast slot is labelled, the other "none").
                for (int slot = 0; slot < 2; ++slot)
                {
                    cast_candidates(ctx, slot, set);
                    if (set.n < 2) continue;
                    int label = 0;
                    if (cast && static_cast<int>(cast->v[0]) == slot)
                    {
                        label = -1;
                        const Tile t{ static_cast<std::int16_t>(cast->v[1]), static_cast<std::int16_t>(cast->v[2]) };
                        for (int i = 1; i < set.n; ++i) if (set.c[i].at == t) label = i;
                    }
                    out.write(slot == 0 ? h_cast0 : h_cast1, set, label, sv, c.team, r, won, rs, ro);
                }
                const bool ranged = cat.champion(c.type).attack_type == 1;
                if (ranged)
                {
                    // One shared allowance, one shared head, and one no-op. Reset turns that use more than one
                    // allowance cannot be reconstructed as this single decision; count and omit those rows.
                    if ((move && attack) || moves > 1 || attacks > 1)
                    {
                        ++out.ranged_complex;
                        out.ranged_move_attack += move && attack ? 1 : 0;
                        out.ranged_multiple += moves > 1 || attacks > 1 ? 1 : 0;
                        ++out.dropped[h_ranged];
                    }
                    else
                    {
                        ranged_candidates(ctx, set);
                        int label = -1;
                        if (recall && !pre_start) { for (int i = 0; i < set.n; ++i) if (set.c[i].kind == 2) label = i; }
                        else if (tribute) { for (int i = 0; i < set.n; ++i) if (set.c[i].kind == 3) label = i; }
                        else if (attack && attack->v.size() >= 6)
                        {
                            const Tile t{ static_cast<std::int16_t>(attack->v[4]), static_cast<std::int16_t>(attack->v[5]) };
                            const int kind = static_cast<int>(attack->v[0]) + 4;
                            for (int i = 0; i < set.n; ++i) if (set.c[i].kind == kind && set.c[i].at == t) label = i;
                        }
                        else if (move)
                        {
                            const Tile t{ static_cast<std::int16_t>(move->v[0]), static_cast<std::int16_t>(move->v[1]) };
                            for (int i = 0; i < set.n; ++i) if (set.c[i].kind == 0 && set.c[i].at == t) label = i;
                        }
                        else { for (int i = 0; i < set.n; ++i) if (set.c[i].kind == 1) label = i; }
                        out.write(h_ranged, set, label, sv, c.team, r, won, rs, ro);
                    }
                }
                else
                {
                    // Melee retains independent attack and movement allowances.
                    attack_candidates(ctx, set);
                    {
                        int label = 0;
                        if (attack && attack->v.size() >= 6)
                        {
                            label = -1;
                            const Tile t{ static_cast<std::int16_t>(attack->v[4]), static_cast<std::int16_t>(attack->v[5]) };
                            for (int i = 1; i < set.n; ++i) if (set.c[i].at == t) label = i;
                        }
                        out.write(h_attack, set, label, sv, c.team, r, won, rs, ro);
                    }
                    step_candidates(ctx, set);
                    {
                        int label = -1;
                        if (recall && !pre_start) { for (int i = 0; i < set.n; ++i) if (set.c[i].kind == 2) label = i; }
                        else if (tribute) { for (int i = 0; i < set.n; ++i) if (set.c[i].kind == 3) label = i; }
                        else if (move)
                        {
                            const Tile t{ static_cast<std::int16_t>(move->v[0]), static_cast<std::int16_t>(move->v[1]) };
                            for (int i = 0; i < set.n; ++i) if (set.c[i].kind == 0 && set.c[i].at == t) label = i;
                        }
                        else { for (int i = 0; i < set.n; ++i) if (set.c[i].kind == 1) label = i; }
                        out.write(h_step, set, label, sv, c.team, r, won, rs, ro);
                    }
                }
                // Tail: pre-start a recall at the turn end (one record while a choice exists).
                tail_candidates(ctx, o.on_spawn, (recall && !pre_start) || tribute, set);
                if (set.n > 1) out.write(h_tail, set, pre_start ? 1 : 0, sv, c.team, r, won, rs, ro);
            }
            // State updates from this turn's own actions.
            for (int k = 0; k < 2; ++k) if (c.cooldown[k] > 0) --c.cooldown[k];
            // The casting turn's owner end already consumes the first cooldown tick before the next sample.
            if (cast) { const int slot = static_cast<int>(cast->v[0]); if (slot == 0 || slot == 1) c.cooldown[slot] = std::max(0, static_cast<int>(cat.champion(c.type).ability[slot].cooldown) - 1); }
            c.standing = pre_start && !o.on_spawn;
            if (recall && !pre_start && o.alive) c.recalling = true;
            if (tribute) c.tributing = true;
            if (o.on_spawn) c.recalling = false;
            if (c.tributing && me.gems <= 0.0f) c.tributing = false;
            if (move) c.recalling = false;
        }
        // World updates from this round's events.
        for (const auto& e : evs)
        {
            if (e.kind == "attack" && e.v.size() >= 4 && e.v[0] == 0)
            {
                auto m = g.member_by_native.find(static_cast<int>(e.v[1]));
                if (m != g.member_by_native.end())
                {
                    if (e.v[3] >= 0) g.member_hp[static_cast<std::size_t>(m->second.first)][static_cast<std::size_t>(m->second.second)] = static_cast<float>(e.v[3]);
                    g.member_hit[static_cast<std::size_t>(g.offset[static_cast<std::size_t>(m->second.first)] + m->second.second)] = r;
                }
            }
            if (e.kind == "cast" && e.v.size() >= 4)
                for (int k = 0; k < static_cast<int>(e.v[3]); ++k)
                    if (e.v[4 + 4 * k] == 0)
                    {
                        auto m = g.member_by_native.find(static_cast<int>(e.v[5 + 4 * k]));
                        if (m != g.member_by_native.end())
                        {
                            g.member_hp[static_cast<std::size_t>(m->second.first)][static_cast<std::size_t>(m->second.second)] = static_cast<float>(e.v[7 + 4 * k]);
                            g.member_hit[static_cast<std::size_t>(g.offset[static_cast<std::size_t>(m->second.first)] + m->second.second)] = r;
                        }
                    }
            if (e.kind == "capture" && !e.v.empty())
            {
                auto gi = g.group_by_native.find(static_cast<int>(e.v[0]));
                if (gi != g.group_by_native.end()) g.respawn_round[static_cast<std::size_t>(gi->second)] = r + g.map.groups[static_cast<std::size_t>(gi->second)].respawn;
            }
            if (e.kind == "banked" && !e.v.empty() && g.champs.count(e.actor))
            {
                g.banked[g.champs[e.actor].team] += static_cast<std::uint32_t>(std::max(0.0, e.v[0]));
                g.champs[e.actor].tributing = false;
            }
        }
    }
    std::fclose(out.f);
    std::printf("%s: %d records", argv[1], out.records);
    const char* names[h_count] = { "goal", "step", "attack", "cast0", "cast1", "shop", "draft", "tail", "ranged" };
    for (int h = 0; h < h_count; ++h) std::printf(" | %s %d kept %d dropped %d single", names[h], out.kept[h], out.dropped[h], out.single[h]);
    std::printf(" | ranged omitted %d (move+attack %d, repeated actions %d)", out.ranged_complex, out.ranged_move_attack, out.ranged_multiple);
    std::printf("\n");
    return 0;
}
