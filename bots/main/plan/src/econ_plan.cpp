// Team economy planner (econ_plan.h): an event-driven surrogate of the team's farming (econ_model.h)
// searched by greedy dispatch with optional rollout improvement (one policy-iteration step, D332), all
// under a fixed node budget (one node = one dispatch decision, rollouts included) so every instance
// computes the same plan from the same snapshot.
#include "econ_plan.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include "econ_model.h"
#include "econ_search.h"

namespace mb::econ {

namespace {

long g_scanned = 0, g_passed = 0, g_forecasts = 0;
constexpr int kMaxGroups = 128;
constexpr int kTop = 8;

struct State
{
    std::array<Champ, kMaxChamps> c{};
    std::array<int, kMaxChamps> pend_at{}, pend_gems{};
    std::array<std::int16_t, kMaxGroups> avail{}, claimed{};
    std::array<float, kMaxGroups> hs{};
    int n{}, banked{}, t{};
    bool done{};
};

struct Cand
{
    float score{};
    int g{ -1 };
    std::uint8_t party{};
    std::int8_t cap{ -1 };
    int start{}, rounds{};
    float damage[kMaxChamps]{};
    int kind{};  // 0 clear, 1 bank, 2 recall
};

struct Sim
{
    const StaticModel& m;
    const SearchParams& o;
    const std::vector<std::vector<ShopStep>>& plans;
    State s;
    Plan* out{};          // records tasks when set
    long* nodes{};        // shared node counter
    long limit{};
    std::int16_t* tcache{};  // n x points: walk from a champion's snapshot tile (-1 = not read yet)

    // Walking rounds of champion i to point p; field reads from a snapshot tile are cached.
    int walk(int i, int p) const
    {
        const Champ& c = s.c[static_cast<std::size_t>(i)];
        if (c.pt >= 0 || !tcache) return walk_rounds(m, c, p);
        std::int16_t& v = tcache[static_cast<std::size_t>(i * m.n_points + p)];
        if (v < 0) v = static_cast<std::int16_t>(std::min(walk_rounds(m, c, p), 32000));
        return v >= 32000 ? kFar : v;
    }
    // A lower bound on walk(i, 1 + g) without field reads (Chebyshev to the member box, minus reach).
    int walk_lb(int i, int g) const
    {
        const Champ& c = s.c[static_cast<std::size_t>(i)];
        if (c.pt >= 0 || !tcache) return walk_rounds(m, c, 1 + g);
        const std::int16_t v = tcache[static_cast<std::size_t>(i * m.n_points + 1 + g)];
        if (v >= 0) return v >= 32000 ? kFar : v;
        const Group& gr = m.groups[static_cast<std::size_t>(g)];
        const int x = c.tile % m.width, y = c.tile / m.width;
        const int dx = x < gr.x0 ? gr.x0 - x : x > gr.x1 ? x - gr.x1 : 0, dy = y < gr.y0 ? gr.y0 - y : y > gr.y1 ? y - gr.y1 : 0;
        const int d = std::max(0, std::max(dx, dy) - 1);
        return static_cast<int>(static_cast<float>(d) * std::min(1.0f, calib().walk_mult));
    }

    int capture_rounds() const { return m.rules.capture_time + 1; }

    void record(int slot, const Task& task)
    {
        if (!out) return;
        auto& v = out->tasks[static_cast<std::size_t>(slot)];
        if (static_cast<int>(v.size()) < o.max_tasks) v.push_back(task);
    }

    std::pair<int, int> nearest_altar(int i) const
    {
        int best = -1, bd = kFar;
        for (int a = 0; a < m.A(); ++a)
        {
            const int d = walk(i, 1 + m.G() + a);
            if (d < bd)
            {
                bd = d;
                best = a;
            }
        }
        return { best, bd };
    }

    void do_recall(int i, int at)
    {
        Champ& c = s.c[static_cast<std::size_t>(i)];
        const int end = at + c.recall_turns + calib().recall_extra;
        Task t;
        t.kind = TaskKind::recall;
        t.start_round = static_cast<std::int16_t>(at);
        t.end_round = static_cast<std::int16_t>(end);
        record(i, t);
        c.free = end;
        c.pt = 0;
        c.tile = -1;
        c.hp = c.maxhp;
        shop(m, i, c, plans[static_cast<std::size_t>(i)]);
    }

    void do_bank(int i)
    {
        Champ& c = s.c[static_cast<std::size_t>(i)];
        const auto [a, d] = nearest_altar(i);
        const int ch = tribute_rounds(m.rules, c.bag) + calib().bank_extra;
        const int end = s.t + d + ch;
        Task t;
        t.kind = TaskKind::bank;
        t.altar = static_cast<std::int16_t>(a);
        t.start_round = static_cast<std::int16_t>(s.t + d);
        t.end_round = static_cast<std::int16_t>(end);
        record(i, t);
        s.pend_at[static_cast<std::size_t>(i)] = end;
        s.pend_gems[static_cast<std::size_t>(i)] += c.bag;
        if (m.roster[static_cast<std::size_t>(i)].passive == pa_tribute_gold) c.gold += m.rules.marcella_gold_per_gem * static_cast<float>(c.bag);
        c.bag = 0;
        c.pt = 1 + m.G() + a;
        c.tile = -1;
        c.free = end;
    }

    void do_wait(int i, int until)
    {
        Champ& c = s.c[static_cast<std::size_t>(i)];
        Task t;
        t.kind = TaskKind::wait;
        t.start_round = static_cast<std::int16_t>(s.t);
        t.end_round = static_cast<std::int16_t>(until);
        record(i, t);
        c.free = until;
    }

    int bag_total_reachable() const
    {
        int v = 0;
        for (int i = 0; i < s.n; ++i) v += s.c[static_cast<std::size_t>(i)].bag;
        return v;
    }

    // Advance to the next champion that needs a decision (automatic banks, endgame waits and shop
    // recalls applied on the way). Returns -1 when the game is over or the horizon is reached.
    int next()
    {
        for (;;)
        {
            int i = 0;
            for (int k = 1; k < s.n; ++k)
                if (s.c[static_cast<std::size_t>(k)].free < s.c[static_cast<std::size_t>(i)].free) i = k;
            Champ& c = s.c[static_cast<std::size_t>(i)];
            s.t = std::max(s.t, c.free);
            for (int k = 0; k < s.n; ++k)
                if (s.pend_gems[static_cast<std::size_t>(k)] && s.pend_at[static_cast<std::size_t>(k)] <= s.t)
                {
                    s.banked += s.pend_gems[static_cast<std::size_t>(k)];
                    s.pend_gems[static_cast<std::size_t>(k)] = 0;
                }
            if (s.banked >= m.rules.winning_gems || s.t >= o.max_rounds)
            {
                s.done = true;
                return -1;
            }
            if (nodes && *nodes >= limit) return -1;
            if (c.pt == 0) shop(m, i, c, plans[static_cast<std::size_t>(i)]);
            int pending = 0;
            for (int k = 0; k < s.n; ++k) pending += s.pend_gems[static_cast<std::size_t>(k)];
            const bool endgame = s.banked + pending + bag_total_reachable() >= m.rules.winning_gems;
            if (c.bag > 0 && (c.bag >= o.bank || endgame))
            {
                const auto [a, d] = nearest_altar(i);
                if (a >= 0 && d < kFar)
                {
                    do_bank(i);
                    continue;
                }
                if (c.pt != 0)
                {
                    do_recall(i, s.t);
                    continue;
                }
            }
            if (endgame)
            {
                // Nothing left to farm for: wait for the next tribute to land.
                int until = s.t + 5;
                for (int k = 0; k < s.n; ++k)
                {
                    if (k == i) continue;
                    if (s.pend_gems[static_cast<std::size_t>(k)]) until = std::min(until, s.pend_at[static_cast<std::size_t>(k)]);
                    if (s.c[static_cast<std::size_t>(k)].bag) until = std::min(until, std::max(s.t + 1, s.c[static_cast<std::size_t>(k)].free));
                }
                do_wait(i, std::max(until, s.t + 1));
                continue;
            }
            if (o.shop_gold > 0 && c.gold >= o.shop_gold && c.pt != 0 && c.hp < o.shop_frac * c.maxhp)
            {
                do_recall(i, s.t);
                continue;
            }
            return i;
        }
    }

    // Top greedy candidates for champion i (best first), at most `want`.
    int candidates(int i, Cand* top, int want)
    {
        const Champ& c = s.c[static_cast<std::size_t>(i)];
        const int t = s.t;
        int nt = 0;
        const int G = m.G();
        // Rates for the bound: ours, and the best recruitable ally's (a + b x neighbours, upper).
        float oa = 0, ob = 0, max_gold = c.gold_mult;
        int recruitable = 0;
        for (int k = 0; k < s.n; ++k)
        {
            const Champ& x = s.c[static_cast<std::size_t>(k)];
            if (k == i || x.free > t + o.recruit) continue;
            ++recruitable;
            oa = std::max(oa, x.rate_a);
            ob = std::max(ob, x.rate_b);
            max_gold = std::max(max_gold, x.gold_mult);
        }
        const int kbig = std::max(1, std::min(o.kmax, 1 + recruitable));
        const float rf = std::max(calib().rate_solo, calib().rate_party);
        struct Other
        {
            int slot, arrive;
        };
        // Optimistic score per group (the deciding champion's walk or the respawn wait, the fastest
        // clear by every recruitable champion, and the capture are part of any party's busy time),
        // then groups in descending bound order until the bound can not beat the kept list.
        float ubs[kMaxGroups], rmins[kMaxGroups];
        std::int16_t order[kMaxGroups], reaches[kMaxGroups];
        int ng = 0;
        for (int g = 0; g < G && g < kMaxGroups; ++g)
        {
            if (s.claimed[static_cast<std::size_t>(g)] > t) continue;
            ++g_scanned;
            const Group& gr = m.groups[static_cast<std::size_t>(g)];
            const int reach = walk_lb(i, g);
            const int avail = s.avail[static_cast<std::size_t>(g)];
            if (reach >= kFar || avail > t + reach + o.wait_max) continue;
            // Busy rounds of k members >= k x H / (r_self + (k - 1) r_ally), monotone in k: check the ends.
            const float H = gr.H * s.hs[static_cast<std::size_t>(g)];
            const float rs = std::max(1e-3f, (c.rate_a + c.rate_b * gr.nb) * rf), ro = (oa + ob * gr.nb) * rf;
            const float rounds_min = std::min(H / rs, static_cast<float>(kbig) * H / (rs + static_cast<float>(kbig - 1) * ro));
            const float ub = (static_cast<float>(gr.gems) + o.gold_w * static_cast<float>(gr.gold) * max_gold) /
                             (static_cast<float>(std::max(reach, avail - t) + capture_rounds()) + rounds_min);
            ubs[ng] = ub;
            rmins[ng] = rounds_min;
            order[ng] = static_cast<std::int16_t>(g);
            reaches[ng] = static_cast<std::int16_t>(reach);
            ++ng;
        }
        // Best bound first by repeated selection (few groups are evaluated; ties: lower index).
        for (;;)
        {
            int q0 = -1;
            for (int q = 0; q < ng; ++q)
                if (ubs[q] >= 0 && (q0 < 0 || ubs[q] > ubs[q0])) q0 = q;
            if (q0 < 0 || (nt == want && ubs[q0] <= top[nt - 1].score)) break;
            ubs[q0] = -1;
            const int g = order[q0];
            const Group& gr = m.groups[static_cast<std::size_t>(g)];
            const int gp = 1 + g;
            const float value_base = static_cast<float>(gr.gems);
            // Exact walk (a field read for a champion still on its snapshot tile), bound rechecked.
            const int reach = walk(i, gp);
            const int avail = s.avail[static_cast<std::size_t>(g)];
            if (reach >= kFar || avail > t + reach + o.wait_max) continue;
            if (reach != reaches[q0] && nt == want &&
                (value_base + o.gold_w * static_cast<float>(gr.gold) * max_gold) /
                        (static_cast<float>(std::max(reach, avail - t) + capture_rounds()) + rmins[q0]) <= top[nt - 1].score)
                continue;
            ++g_passed;
            Other others[kMaxChamps];
            int no = 0;
            if (o.kmax > 1)
                for (int k = 0; k < s.n; ++k)
                {
                    if (k == i) continue;
                    const Champ& x = s.c[static_cast<std::size_t>(k)];
                    if (x.free > t + o.recruit) continue;
                    const int w = walk(k, gp);
                    if (w >= kFar) continue;
                    const int arrive = std::max(x.free, t) + w;
                    int j = no++;
                    while (j > 0 && others[j - 1].arrive > arrive)
                    {
                        others[j] = others[j - 1];
                        --j;
                    }
                    others[j] = { k, arrive };
                }
            const Champ* party[kMaxChamps];
            int slots[kMaxChamps];
            int arrive_max = t + reach;
            party[0] = &c;
            slots[0] = i;
            for (int k = 1; k <= std::min(o.kmax, s.n); ++k)
            {
                if (k > 1)
                {
                    if (k - 2 >= no) break;
                    const auto& e = others[k - 2];
                    party[k - 1] = &s.c[static_cast<std::size_t>(e.slot)];
                    slots[k - 1] = e.slot;
                    arrive_max = std::max(arrive_max, e.arrive);
                }
                const int start = std::max(arrive_max, static_cast<int>(s.avail[static_cast<std::size_t>(g)]));
                ++g_forecasts;
                const ClearForecast f = forecast_clear(m, party, slots, k, g, s.hs[static_cast<std::size_t>(g)]);
                if (!f.ok) continue;
                const bool covered = k >= 2;
                bool ok = true;
                for (int q = 0; q < k && ok; ++q)
                {
                    const float reserve = covered ? 0.0f : static_cast<float>(std::max(0, gr.max_range - 1)) * gr.max_str * party[q]->taken_mult * (o.reserve_rounds * 0.5f);
                    if (f.damage[q] * o.safety > party[q]->hp - reserve) ok = false;
                }
                if (!ok) continue;
                int cap = 0;
                auto cap_score = [&](int q) {
                    const auto& d = m.roster[static_cast<std::size_t>(slots[q])];
                    if (gr.gold > 0) return party[q]->gold_mult;
                    return d.passive == pa_tribute_gold ? 2.0f : d.passive == pa_gem_focus ? 1.5f : 1.0f;
                };
                for (int q = 1; q < k; ++q)
                    if (cap_score(q) > cap_score(cap)) cap = q;
                int busy = capture_rounds();
                for (int q = 0; q < k; ++q) busy += (start - std::max(party[q]->free, t)) + f.rounds;
                const float value = value_base + o.gold_w * static_cast<float>(gr.gold) * party[cap]->gold_mult;
                Cand cd;
                cd.score = value / static_cast<float>(std::max(1, busy));
                cd.g = g;
                cd.start = start;
                cd.rounds = f.rounds;
                cd.cap = static_cast<std::int8_t>(slots[cap]);
                for (int q = 0; q < k; ++q)
                {
                    cd.party |= static_cast<std::uint8_t>(1u << slots[q]);
                    cd.damage[slots[q]] = f.damage[q];
                }
                // Insert into the kept top list (stable: earlier candidates win ties).
                if (nt < want || cd.score > top[nt - 1].score)
                {
                    int j = nt < want ? nt++ : nt - 1;
                    while (j > 0 && top[j - 1].score < cd.score)
                    {
                        top[j] = top[j - 1];
                        --j;
                    }
                    top[j] = cd;
                }
            }
        }
        return nt;
    }

    void apply(int i, const Cand* cd)
    {
        Champ& c = s.c[static_cast<std::size_t>(i)];
        if (nodes) ++*nodes;
        if (!cd)
        {
            if (c.hp < c.maxhp && c.pt != 0)
                do_recall(i, s.t);
            else
                do_wait(i, s.t + 5);
            return;
        }
        if (cd->kind == 1)
        {
            do_bank(i);
            return;
        }
        if (cd->kind == 2)
        {
            do_recall(i, s.t);
            return;
        }
        const int g = cd->g, gp = 1 + g;
        const Group& gr = m.groups[static_cast<std::size_t>(g)];
        const int end = cd->start + cd->rounds;
        const int cap_end = end + capture_rounds();
        for (int k = 0; k < s.n; ++k)
        {
            if (!(cd->party >> k & 1)) continue;
            Champ& x = s.c[static_cast<std::size_t>(k)];
            Task t;
            t.kind = TaskKind::clear;
            t.group = static_cast<std::int16_t>(g);
            t.party = cd->party;
            t.capturer = cd->cap;
            t.start_round = static_cast<std::int16_t>(cd->start);
            t.end_round = static_cast<std::int16_t>(k == cd->cap ? cap_end : end);
            record(k, t);
            x.hp -= cd->damage[k];
            x.pt = gp;
            x.tile = -1;
            x.free = end;
        }
        Champ& cap = s.c[static_cast<std::size_t>(cd->cap)];
        cap.free = cap_end;
        const float gold = static_cast<float>(gr.gold) * cap.gold_mult;
        cap.gold += gold;
        cap.bag += gr.gems;
        s.claimed[static_cast<std::size_t>(g)] = static_cast<std::int16_t>(cap_end);
        s.avail[static_cast<std::size_t>(g)] = static_cast<std::int16_t>(std::min(30000, cap_end + gr.respawn));
        s.hs[static_cast<std::size_t>(g)] = 1.0f;
        if (o.capture_recall && ((o.shop_gold > 0 && cap.gold >= o.shop_gold) || cap.hp < o.recall_frac * cap.maxhp))
        {
            // Recall on the tile during the capture wait (the claim holds through the channel).
            Task t;
            t.kind = TaskKind::recall;
            t.start_round = static_cast<std::int16_t>(std::max(end, cap_end - cap.recall_turns - calib().recall_extra));
            t.end_round = static_cast<std::int16_t>(cap_end);
            record(cd->cap, t);
            cap.pt = 0;
            cap.hp = cap.maxhp;
            shop(m, cd->cap, cap, plans[static_cast<std::size_t>(cd->cap)]);
        }
        for (int k = 0; k < s.n; ++k)
            if ((cd->party >> k & 1) && k != cd->cap && s.c[static_cast<std::size_t>(k)].hp < o.recall_frac * s.c[static_cast<std::size_t>(k)].maxhp)
                do_recall(k, end);
    }

    void playout()
    {
        Cand top[1];
        for (int i = next(); i >= 0; i = next())
        {
            const int n = candidates(i, top, 1);
            apply(i, n ? &top[0] : nullptr);
        }
    }
};

// Estimated finish from a partial simulation: current round plus the gems still missing at the
// simulated team income so far.
int extrapolate(const State& s, int start_round, int start_banked, int win)
{
    if (s.banked >= win) return s.t;
    int carried = 0;
    for (int k = 0; k < s.n; ++k) carried += s.c[static_cast<std::size_t>(k)].bag + s.pend_gems[static_cast<std::size_t>(k)];
    const int have = s.banked + carried;
    const int span = std::max(1, s.t - start_round);
    const float rate = static_cast<float>(std::max(1, have - start_banked)) / static_cast<float>(span);
    return s.t + static_cast<int>(std::ceil(static_cast<float>(std::max(0, win - have)) / rate)) + 10;
}

State initial_state(const StaticModel& m, const Snapshot& snap, const std::vector<std::vector<ShopStep>>& plans)
{
    State s;
    s.n = std::min<int>(static_cast<int>(m.roster.size()), kMaxChamps);
    s.t = snap.round;
    s.banked = snap.banked;
    for (int i = 0; i < s.n; ++i)
    {
        Champ& c = s.c[static_cast<std::size_t>(i)];
        refresh_stats(m, i, c);
        const bool have = i < static_cast<int>(snap.champs.size());
        const ChampState cs = have ? snap.champs[static_cast<std::size_t>(i)] : ChampState{};
        // Shards: follow the shop plan while the snapshot's stats still exceed the model's.
        if (have && cs.max_hp > 0)
        {
            const auto& plan = plans[static_cast<std::size_t>(i)];
            for (int guard = 0; guard < 120; ++guard)
            {
                Champ t = c;
                t.gold = static_cast<float>(m.rules.shard_cost);
                if (!shop(m, i, t, plan)) break;
                if (t.maxhp > cs.max_hp + 0.5f || t.str > cs.strength + 0.5f || t.foc > cs.focus + 0.5f) break;
                c = t;
            }
            c.maxhp = std::max(c.maxhp, cs.max_hp);
            if (cs.emblems)
            {
                c.emblem_mask = cs.emblems;
                const float hp = c.maxhp;
                refresh_stats(m, i, c);
                c.maxhp = std::max(hp, c.maxhp);
            }
        }
        c.gold = have ? static_cast<float>(cs.gold) : 0.0f;
        c.bag = have ? cs.gems : 0;
        c.free = snap.round;
        c.pt = 0;
        c.tile = -1;
        c.hp = c.maxhp;
        if (!have) continue;
        if (!cs.alive)
        {
            c.free = std::max(snap.round, cs.respawn_round);
            continue;
        }
        c.hp = cs.hp > 0 ? std::min(cs.hp, c.maxhp) : c.maxhp;
        c.free = std::max(snap.round, cs.free_round);
        const int tile = cs.pos.y * m.width + cs.pos.x;
        const bool on_spawn = std::find(m.spawn.begin(), m.spawn.end(), tile) != m.spawn.end();
        if (!on_spawn && tile >= 0 && tile < m.width * m.height)
        {
            c.pt = -1;
            c.tile = tile;
        }
    }
    for (int g = 0; g < m.G() && g < kMaxGroups; ++g)
    {
        s.avail[static_cast<std::size_t>(g)] = static_cast<std::int16_t>(snap.round);
        s.claimed[static_cast<std::size_t>(g)] = -1;
        s.hs[static_cast<std::size_t>(g)] = 1.0f;
        if (g < static_cast<int>(snap.groups.size()))
        {
            const auto& gs = snap.groups[static_cast<std::size_t>(g)];
            if (!gs.alive) s.avail[static_cast<std::size_t>(g)] = static_cast<std::int16_t>(std::max(snap.round, gs.respawn_round));
            const float H = m.groups[static_cast<std::size_t>(g)].H;
            if (gs.hp_left >= 0 && H > 0) s.hs[static_cast<std::size_t>(g)] = std::clamp(gs.hp_left / H, 0.02f, 1.0f);
        }
    }
    return s;
}

} // namespace

SearchParams& search_params()
{
    static SearchParams p;
    return p;
}

} // namespace mb::econ (reopened below for the search state)

namespace mb::econ {

struct SearchState
{
    const StaticModel* m{};
    SearchParams o;
    std::vector<std::vector<ShopStep>> plans;
    std::vector<std::int16_t> tcache;
    Plan plan;
    long nodes{};
    long budget{};
    int start_round{}, start_banked{};
    int changed{};
    bool finished{};
    std::optional<Sim> sim;
};

namespace {

void init_state(SearchState& st, const StaticModel& m, const Snapshot& snap, int node_budget, const SearchParams& o)
{
    st.m = &m;
    st.o = o;
    st.budget = node_budget;
    st.start_round = snap.round;
    st.start_banked = snap.banked;
    st.plan.built_round = snap.round;
    const int n = std::min<int>(static_cast<int>(m.roster.size()), kMaxChamps);
    st.plan.tasks.assign(static_cast<std::size_t>(n), {});
    if (!m.ready || n == 0)
    {
        st.finished = true;
        return;
    }
    for (int i = 0; i < n; ++i) st.plans.push_back(default_shop_plan(m, i));
    st.tcache.assign(static_cast<std::size_t>(n * m.n_points), -1);
    st.sim.emplace(Sim{ m, st.o, st.plans, initial_state(m, snap, st.plans), &st.plan, &st.nodes, 0, st.tcache.data() });
}

// Run the search until `limit` nodes in total (or the end). Greedy dispatch; with rollout_m > 1,
// candidates are scored by greedy playouts when the remaining budget affords them.
void run(SearchState& st, long limit)
{
    if (st.finished || !st.sim) return;
    Sim& sim = *st.sim;
    const StaticModel& m = *st.m;
    const SearchParams& o = st.o;
    sim.limit = std::min(limit, st.budget);
    Cand top[kTop];
    const int M = std::clamp(o.rollout_m, 1, kTop);
    for (;;)
    {
        unsigned long long f0 = g_fuel ? g_fuel() : 0;
        const int i = sim.next();
        if (g_fuel) { const auto f1 = g_fuel(); g_prof[0] += f0 - f1; f0 = f1; }
        if (i < 0) break;
        const int nc = sim.candidates(i, top, M);
        if (g_fuel) { const auto f1 = g_fuel(); g_prof[1] += f0 - f1; f0 = f1; }
        if (nc <= 1 || M == 1 || st.budget - st.nodes < static_cast<long>(nc) * o.rollout_cost + o.reserve_nodes)
        {
            sim.apply(i, nc ? &top[0] : nullptr);
            if (g_fuel) g_prof[2] += f0 - g_fuel();
            continue;
        }
        int best = 0, best_t = 1 << 30, best_banked = -1;
        for (int k = 0; k < nc; ++k)
        {
            Sim r{ m, o, st.plans, sim.s, nullptr, &st.nodes, st.budget, st.tcache.data() };
            r.apply(i, &top[k]);
            r.playout();
            const int ft = r.s.done ? r.s.t : extrapolate(r.s, st.start_round, st.start_banked, m.rules.winning_gems);
            if (ft < best_t || (ft == best_t && r.s.banked > best_banked))
            {
                best_t = ft;
                best_banked = r.s.banked;
                best = k;
            }
        }
        if (best != 0) ++st.changed;
        sim.apply(i, &top[best]);
    }
    if (sim.s.done || st.nodes >= st.budget) st.finished = true;
}

void finish_plan(const SearchState& st, Plan& out)
{
    out = st.plan;
    if (!st.sim) return;
    const auto& s = st.sim->s;
    const int win = st.m->rules.winning_gems;
    out.predicted_finish = s.done && s.banked >= win ? s.t : s.done ? st.o.max_rounds : extrapolate(s, st.start_round, st.start_banked, win);
}

} // namespace

Plan plan_with(const StaticModel& m, const Snapshot& snap, int node_budget, const SearchParams& o, SearchStats* stats)
{
    g_scanned = g_passed = g_forecasts = 0;
    SearchState st;
    init_state(st, m, snap, node_budget, o);
    run(st, node_budget);
    Plan plan;
    finish_plan(st, plan);
    if (stats)
    {
        stats->nodes = st.nodes;
        stats->scanned = g_scanned;
        stats->passed = g_passed;
        stats->forecasts = g_forecasts;
        stats->changed = st.changed;
        if (st.sim)
        {
            stats->horizon = st.sim->s.t;
            stats->finished = st.sim->s.done && st.sim->s.banked >= m.rules.winning_gems;
            stats->banked = st.sim->s.banked;
        }
    }
    return plan;
}

Plan plan_team(const Snapshot& s, int node_budget)
{
    return plan_with(*model_for(s), s, node_budget, search_params(), nullptr);
}

SearchState* plan_begin(const Snapshot& s, int node_budget)
{
    auto* st = new SearchState;
    init_state(*st, *model_for(s), s, node_budget, search_params());
    return st;
}

bool plan_step(SearchState& st, int nodes)
{
    run(st, st.nodes + std::max(0, nodes));
    return st.finished;
}

Plan plan_result(const SearchState& st)
{
    Plan p;
    finish_plan(st, p);
    return p;
}

void plan_end(SearchState* st) { delete st; }

} // namespace mb::econ
