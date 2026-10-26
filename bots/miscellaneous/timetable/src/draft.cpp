// Draft: choose the roster, measure the whole map and write the team's plan into draft memory.
#include "draft.h"
#include <cambc.h>
#include <algorithm>
#include <array>
#include <vector>
#include "config.h"
#include "kit.h"
#include "plan.h"
#include "util.h"
#include "world.h"

namespace tt {

std::vector<cambc::champion_type> bodies_of(cambc::champion_draft d)
{
    using D = cambc::champion_draft;
    using T = cambc::champion_type;
    switch (d) {
    case D::ryth: return { T::ryth };
    case D::vesper_vale: return { T::vesper_vale };
    case D::vendetta: return { T::vendetta };
    case D::cassi_and_issac: return { T::cassi, T::issac };
    case D::stellar: return { T::stellar };
    case D::prism: return { T::prism };
    case D::marcella: return { T::marcella };
    case D::willow: return { T::willow };
    case D::ren: return { T::ren };
    case D::varg: return { T::varg };
    case D::tavi: return { T::tavi };
    case D::heart_of_ruin: return { T::heart_of_ruin };
    }
    return {};
}

namespace {

using D = cambc::champion_draft;

struct DraftState {
    bool started = false;
    int team_size = 0;
    std::vector<D> roster;
    // Planning job, resumable across draft turns.
    int stage = 0;          // 0 init, 1 group fields, 2 spawn/altar fields, 3 solve, 4 done
    int next_group = 0;
    std::vector<int> rep;   // representative tile per group (member nearest the centre)
    bool written = false;
    Plan plan;
    long fuel_init = 0, fuel_fields = 0, fuel_plan = 0; // fuel spent per stage (diagnosis)
};

DraftState& state()
{
    static DraftState s;
    return s;
}

PlanInput& input()
{
    static PlanInput in;
    return in;
}

// Fixed roster by priority (cfg::draft_mode 0) and the default of mode 1.
std::vector<D> fixed_roster(int n)
{
    static const std::array<D, 5> order{ D::cassi_and_issac, D::vendetta, D::marcella, D::tavi, D::vesper_vale };
    std::vector<D> out(order.begin(), order.begin() + std::min<int>(n, static_cast<int>(order.size())));
    return out;
}

void fill_bodies(PlanInput& in, const std::vector<D>& roster)
{
    in.n_bodies = 0;
    for (D d : roster)
        for (auto t : bodies_of(d)) {
            if (in.n_bodies >= kMaxBodies) break;
            const Kit k = kit_of(t);
            in.bodies[in.n_bodies++] = body_spec(k, k.base_hp, k.base_str, k.base_focus);
        }
}

// Distances from the group's member tiles over static terrain (camp tiles passable).
void measure_group(int g)
{
    auto& w = World::get();
    auto& st = state();
    auto& in = input();
    cambc::bitboard roots;
    roots.clear();
    for (int m : w.groups[static_cast<std::size_t>(g)].members) roots.set(w.members[static_cast<std::size_t>(m)].pos);
    cambc::distance_field f;
    cambc::bitboard visited;
    f.compute(w.walls_static & ~roots, roots, visited);
    for (int h = 0; h < in.n_groups; ++h) {
        const int t = st.rep[static_cast<std::size_t>(h)];
        const int v = f.get({ t % w.W, t / w.W });
        in.dist[g][h] = static_cast<std::uint16_t>(v >= cambc::distance_field::unreachable ? 65535 : v);
        if (in.dist[h][g] == 65535) in.dist[h][g] = in.dist[g][h]; // unmeasured rows borrow the column
    }
}

void measure_field(const cambc::bitboard& roots, const cambc::bitboard& walls, int* out)
{
    auto& w = World::get();
    auto& st = state();
    auto& in = input();
    cambc::distance_field f;
    cambc::bitboard visited;
    f.compute(walls & ~roots, roots, visited);
    for (int h = 0; h < in.n_groups; ++h) {
        const int t = st.rep[static_cast<std::size_t>(h)];
        const int v = f.get({ t % w.W, t / w.W });
        out[h] = v >= cambc::distance_field::unreachable ? kInf : v;
    }
}

void write_plan(const Plan& p)
{
    std::array<std::uint8_t, 1024> bytes{};
    const std::size_t n = encode_plan(p, bytes.data(), std::min<std::size_t>(bytes.size(), cambc::constants::draft_memory_bytes));
    if (n == 0) return;
    cambc::draft_memory = std::span<const std::uint8_t>(bytes.data(), n);
    state().written = true;
}

// Advance the planning job while fuel lasts; returns true when the plan is final.
Plan fallback_plan();

bool work()
{
    auto& st = state();
    auto& w = World::get();
    auto& in = input();
    const long f0 = fuel_left();
    if (st.stage == 0) {
        in.width = w.W;
        in.height = w.H;
        in.n_groups = std::min<int>(static_cast<int>(w.groups.size()), kMaxGroups);
        st.rep.assign(static_cast<std::size_t>(in.n_groups), 0);
        for (int g = 0; g < in.n_groups; ++g) {
            const auto& grp = w.groups[static_cast<std::size_t>(g)];
            in.groups[g] = grp.spec;
            int best = grp.members.front();
            for (int m : grp.members)
                if (cheb(w.members[static_cast<std::size_t>(m)].pos, grp.centre) < cheb(w.members[static_cast<std::size_t>(best)].pos, grp.centre)) best = m;
            st.rep[static_cast<std::size_t>(g)] = w.members[static_cast<std::size_t>(best)].tile;
        }
        in.gold_weight = cfg::gold_weight;
        in.territory_margin = cfg::territory_margin;
        in.contest_margin = cfg::contest_margin;
        in.camp_margin = cfg::camp_margin;
        in.capture_rounds = cfg::capture_rounds;
        in.bank_gems = cfg::bank_gems;
        in.siege_max_trips = cfg::siege_max_trips;
        fill_bodies(in, st.roster);
        st.stage = 1;
        st.fuel_init = f0 - fuel_left();
    }
    const long f1 = fuel_left();
    const long field = w.field_cost() + 6000;
    if (st.stage == 1) {
        // Spawns and altars first: they decide which groups are worth measuring.
        if (!fuel_ok(3 * field)) return false;
        measure_field(w.my_spawn, w.walls_static, in.d_ours);
        // The enemy cannot enter our spawn; our spawn tiles are walls for them.
        const cambc::bitboard their_walls = cambc::map::walls() | cambc::map::holes() | cambc::map::altars() | w.my_spawn;
        measure_field(w.enemy_spawn, their_walls, in.d_theirs);
        cambc::bitboard altar_roots;
        altar_roots.clear();
        for (const auto& s : w.altar_submit) altar_roots |= s;
        measure_field(altar_roots, w.walls_static, in.d_altar);
        for (int g = 0; g < in.n_groups; ++g) {
            int best = 0, bd = kInf;
            const int t = st.rep[static_cast<std::size_t>(g)];
            const vec2 p{ t % w.W, t / w.W };
            for (int a = 0; a < static_cast<int>(w.altars.size()); ++a)
                if (cheb(p, w.altars[static_cast<std::size_t>(a)]) < bd) { bd = cheb(p, w.altars[static_cast<std::size_t>(a)]); best = a; }
            in.altar_of[g] = best;
            for (int h = 0; h < in.n_groups; ++h) in.dist[g][h] = g == h ? 0 : 65535;
        }
        st.stage = 2;
    }
    while (st.stage == 2) {
        // Group-to-group distances only for groups the plan can use: our side and the contested middle.
        if (st.next_group >= in.n_groups) { st.stage = 3; break; }
        const int g = st.next_group;
        if (in.d_ours[g] >= kInf || in.d_ours[g] > in.d_theirs[g] + in.territory_margin + in.contest_margin + cfg::measure_margin) {
            ++st.next_group;
            continue;
        }
        if (!fuel_ok(field)) return false;
        measure_group(st.next_group++);
    }
    st.fuel_fields += f1 - fuel_left();
    if (st.stage == 3) {
        if (!fuel_ok(150000)) return false;
        Planner planner(in);
        planner.set_fuel(&fuel_left, 160000);
        // A work unit costs a few dozen fuel: leave a wide margin, an exhausted draft turn loses picks.
        const long f2 = fuel_left();
        const long budget = std::max<long>(5000, (f2 - 250000) / cfg::plan_fuel_per_op);
        st.plan = planner.solve(budget);
        if (planner.structures_done() == 0) st.plan = fallback_plan();
        st.fuel_plan = f2 - fuel_left();
        st.stage = 4;
        TT_LOG(1, "draft fuel init %ld fields %ld plan %ld ops %d left %ld\n", st.fuel_init, st.fuel_fields, st.fuel_plan,
               planner.last_ops(), fuel_left());
        TT_LOG(1, "plan groups %d bodies %d units %d structure %d est %d ops %d scores %.2f %.2f %.2f %.2f\n", in.n_groups,
               in.n_bodies, st.plan.n_units, st.plan.structure, st.plan.est_rounds, planner.last_ops(), planner.structure_score(0),
               planner.structure_score(1), planner.structure_score(2), planner.structure_score(3));
        if (cfg::log_level >= 2)
            for (int g = 0; g < in.n_groups; ++g)
                TT_LOG(2, "g%d gems %d gold %d hp %.0f str %.0f ours %d theirs %d altar %d owner %d tour %d\n", g, in.groups[g].gems,
                       in.groups[g].gold, in.groups[g].hp_total, in.groups[g].str_total, in.d_ours[g], in.d_theirs[g], in.d_altar[g],
                       st.plan.owner[g], st.plan.tour[g]);
    }
    return st.stage == 4;
}

// A plan without measurements: everyone greedy over the whole map (used when fuel ran short).
Plan fallback_plan()
{
    auto& in = input();
    Plan p;
    p.valid = true;
    p.width = World::get().W;
    p.height = World::get().H;
    p.n_groups = in.n_groups;
    p.n_bodies = in.n_bodies;
    for (int b = 0; b < in.n_bodies; ++b) p.roster[b] = in.bodies[b].type;
    for (int b = 0; b < in.n_bodies; ++b) p.body_unit[b] = b;
    p.n_units = in.n_bodies;
    for (int g = 0; g < in.n_groups; ++g) { p.owner[g] = kNoOwner; p.party[g] = false; p.ours[g] = true; }
    p.n_matrix = 0;
    for (int g = 0; g < kMaxGroups; ++g) p.matrix_index[g] = -1;
    p.gold_weight = cfg::gold_weight;
    p.bank_gems = cfg::bank_gems;
    return p;
}

} // namespace

void play_draft()
{
    auto& st = state();
    if (!st.started) {
        st.started = true;
        st.team_size = static_cast<int>(cambc::draft::team_size());
        st.roster = fixed_roster(st.team_size);
    }
    const auto mine = cambc::draft::current_draft(cambc::team::ally);
    const int done = static_cast<int>(mine.size());
    // Pick first: a failed or exhausted turn must never cost the pick.
    D pick = st.roster.empty() ? D::vendetta : st.roster.front();
    for (D d : st.roster)
        if (std::find(mine.begin(), mine.end(), d) == mine.end()) { pick = d; break; }
    if (std::find(mine.begin(), mine.end(), pick) != mine.end()) {
        for (int i = 0; i < 12; ++i) {
            const auto d = static_cast<D>(i);
            if (std::find(mine.begin(), mine.end(), d) == mine.end()) { pick = d; break; }
        }
    }
    cambc::draft::pick(pick);

    if (cfg::plan_mode == 0) return;
    const bool last = done + 1 >= st.team_size;
    if (st.stage < 4) work();
    if (st.stage == 4 && !st.written) write_plan(st.plan);
    else if (last && !st.written) {
        auto& in = input();
        if (in.n_groups == 0) {
            auto& w = World::get();
            in.n_groups = std::min<int>(static_cast<int>(w.groups.size()), kMaxGroups);
            fill_bodies(in, st.roster);
        }
        write_plan(fallback_plan());
    }
}

} // namespace tt
