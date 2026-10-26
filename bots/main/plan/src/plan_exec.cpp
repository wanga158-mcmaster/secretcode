// Team plan executor (cfg::team_plan): follows econ::plan_team's per-champion task queue with
// v53's execution layer. Brain's phases still run in their order (channels, spawn, threats,
// health recalls, tribute, farm); the plan only replaces the choice of what to do next:
//   clear  -> target_ / target_need_ (staging until the planned party is present, fight_camps,
//             the planned capturer holds the reward, the others move on to their next task)
//   bank   -> handle_tribute's trigger
//   recall -> a recall (handle_threats / retreat first when threatened)
//   wait   -> hold off camp tiles until the task's end round
// Every instance builds the opening plan from static inputs only (map, roster designs, a fixed
// round and node budget), so all agree without communicating. Allies' progress is read from the
// unchanged shared-memory slots (target group + status). A plan that keeps failing (skipped tasks)
// or runs out of tasks hands decisions back to v53.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "brain.h"
#include "config.h"
#include "econ_model.h"
#include "econ_plan.h"
#include "knowledge.h"
#include "plan_exec.h"
#include "nav.h"
#include "world.h"

namespace mb {

namespace pexec {

// Executor tests only (cfg::team_plan 2): a greedy plan with solo and pair clears, banks and
// recalls, from the same Snapshot plan_team receives. Static distances (Chebyshev x 1.25).
econ::Plan test_plan(const econ::Snapshot& s, int node_budget);

namespace {

struct Exec
{
    bool built{};
    bool stale{};
    econ::Plan plan;
    int idx{};          // our current task
    int skips{};
    int begin{ -1 };    // round the current task became current
    int engage{ -1 };   // first engaged / channel round of the current task
    int arrive{ -1 };   // clear: first round at the staging point or fighting
    int lowhp{};        // health recalls (v53 rule) during the current task
    bool saw_alive{}, saw_reward{};
    bool forced{};      // target_ set from the plan this turn
    bool holding{};     // waiting on spawn for the plan (field preparation)
    int ready_round{ -1 };  // first round every point field existed
    bool model_built{};
    bool draft_tried{};
    int logged{};
};

Exec& ex()
{
    static Exec e;
    return e;
}

const econ::Task* current()
{
    auto& e = ex();
    const int slot = Knowledge::get().self_index;
    if (!e.built || e.stale || slot < 0 || slot >= static_cast<int>(e.plan.tasks.size())) return nullptr;
    const auto& q = e.plan.tasks[static_cast<std::size_t>(slot)];
    if (e.idx >= static_cast<int>(q.size())) return nullptr;
    return &q[static_cast<std::size_t>(e.idx)];
}

char kind_char(econ::TaskKind k)
{
    switch (k)
    {
    case econ::TaskKind::clear: return 'C';
    case econ::TaskKind::bank: return 'B';
    case econ::TaskKind::recall: return 'R';
    case econ::TaskKind::wait: return 'W';
    }
    return '?';
}

// Opening snapshot: static facts every instance knows identically (all champions on the spawn
// centre at full design health, nothing banked or carried, every group alive).
// Opening snapshot: static facts every instance knows identically (all champions on the spawn
// centre at full design health, nothing banked or carried, every group alive). `types` in roster
// order; the draft passes its pick order, gameplay Knowledge::allies.
econ::Snapshot opening_snapshot(const std::vector<cambc::champion_type>& types, int round)
{
    econ::Snapshot s;
    s.round = round;
    s.banked = 0;
    const auto& world = World::get();
    for (const auto type : types)
    {
        const auto& design = cambc::champion_designs[static_cast<std::size_t>(type)];
        const auto stats = design.base_stats();
        econ::ChampState st;
        st.type = static_cast<std::int8_t>(type);
        st.alive = true;
        st.pos = world.ally_spawn_center;
        st.free_round = s.round;
        st.hp = st.max_hp = stats.base_health;
        st.strength = stats.base_strength;
        st.focus = stats.base_focus;
        st.gold = 0;
        st.gems = 0;
        s.champs.push_back(st);
    }
    s.groups.assign(world.groups.size(), econ::GroupState{});
    return s;
}

econ::Snapshot opening_snapshot()
{
    std::vector<cambc::champion_type> types;
    for (const auto& c : Knowledge::get().allies) types.push_back(c.champion_name());
    return opening_snapshot(types, cfg::team_plan_round);
}

template <int Mode>
econ::Plan run_planner(const econ::Snapshot& s, int nodes)
{
    if constexpr (Mode == 1)
        return econ::plan_team(s, nodes);
    else
        return test_plan(s, nodes);
}

void log_plan(const Exec& e)
{
    if (!cfg::team_plan_log) return;
    const int slot = Knowledge::get().self_index;
    const auto& q = e.plan.tasks[static_cast<std::size_t>(slot)];
    char buf[400];
    int len = std::snprintf(buf, sizeof buf, "PQ s%d n%zu f%d |", slot, q.size(), e.plan.predicted_finish);
    for (std::size_t i = 0; i < q.size(); ++i)
    {
        const auto& t = q[i];
        if (len > 330)
        {
            MB_LOG("%s\n", buf);
            len = std::snprintf(buf, sizeof buf, "PQ s%d |", slot);
        }
        len += std::snprintf(buf + len, sizeof buf - static_cast<std::size_t>(len), " %c%d/%x/%d@%d-%d", kind_char(t.kind),
                             t.kind == econ::TaskKind::bank ? t.altar : t.group, t.party, t.capturer, t.start_round, t.end_round);
    }
    MB_LOG("%s\n", buf);
}

} // namespace

} // namespace pexec

bool pexec::holding() { return ex().holding; }

void pexec::note_lowhp() { ++ex().lowhp; }

bool Brain::plan_forced() const { return pexec::ex().forced; }

void Brain::plan_publish_status(const Ctx& ctx) const
{
    const auto& e = pexec::ex();
    const bool recalling = ctx.state.contains(cambc::champion_state::recalling);
    pexec::set_status(e.stale || !e.built ? 2 : capturing_ ? 1 : ctx.on_spawn || recalling ? 2 : 0);
}

void Brain::plan_turn(const Ctx& ctx)
{
    auto& e = pexec::ex();
    auto& kn = Knowledge::get();
    e.forced = false;
    e.holding = false;
    if constexpr (false)
        if (!e.built && !e.draft_tried)
        {
            e.draft_tried = true;
            if (pexec::decode_draft(e.plan))
            {
                e.built = true;
                e.stale = kn.self_index >= static_cast<int>(e.plan.tasks.size());
                if (cfg::team_plan_log && fuel_ok(8000))
                {
                    MB_LOG("PP r%d draft s%d finish %d\n", ctx.round, kn.self_index, e.plan.predicted_finish);
                    if (!e.stale) pexec::log_plan(e);
                }
            }
            else if (cfg::team_plan_log && fuel_ok(8000))
                MB_LOG("PP r%d s%d no draft plan\n", ctx.round, kn.self_index);
        }
    if (!e.built)
    {
        if (ctx.round < cfg::team_plan_round) return;
        if constexpr (cfg::team_plan == 1)
            if (!e.model_built)
            {
                // The planner's static model on a turn of its own (cost measured in the log).
                e.holding = true;
                const auto before = fuel();
                econ::static_model();
                e.model_built = true;
                const int now = static_cast<int>(cambc::round_number());
                if (cfg::team_plan_log && fuel_ok(8000))
                    MB_LOG("PM r%d-%d s%d fuel %llu\n", ctx.round, now, kn.self_index, static_cast<unsigned long long>(before - std::min(before, fuel())));
                return;
            }
        if constexpr (cfg::team_plan == 1 && cfg::team_plan_fields > 0)
        {
            // Every instance stays on spawn and computes the planner's point fields on the same
            // rounds with the same quota (a deterministic schedule, never gated on fuel), so all
            // build the plan on the same round from the same model.
            if (e.ready_round < 0)
            {
                e.holding = true;
                const auto before = fuel();
                const bool ready = econ::prepare(cfg::team_plan_fields);
                if (cfg::team_plan_log && fuel_ok(8000))
                    MB_LOG("PF r%d s%d fuel %llu ready %d\n", ctx.round, kn.self_index,
                           static_cast<unsigned long long>(before - std::min(before, fuel())), ready ? 1 : 0);
                if (!ready) return;
                e.ready_round = ctx.round;
                return;  // build next turn on a fresh budget
            }
        }
        // Same inputs on every instance; a turn short of fuel builds it next turn (same plan).
        if (!fuel_ok(cfg::team_plan_fuel)) { e.holding = cfg::team_plan == 1; return; }
        const auto before = fuel();
        auto snap = pexec::opening_snapshot();
        if (e.ready_round >= 0) snap.round = e.ready_round + 1;
        for (auto& c : snap.champs) c.free_round = snap.round;
        e.plan = pexec::run_planner<cfg::team_plan>(snap, cfg::team_plan_nodes);
        e.built = true;
        e.holding = true;  // the search used a large share of this turn: act from the next one
        e.stale = kn.self_index >= static_cast<int>(e.plan.tasks.size());
        if constexpr (cfg::team_plan == 1) pexec::activate_sync();
        if (cfg::team_plan_log && fuel_ok(8000))
        {
            MB_LOG("PP r%d-%u s%d fuel %llu finish %d\n", ctx.round, cambc::round_number(), kn.self_index, static_cast<unsigned long long>(before - std::min(before, fuel())),
                   e.plan.predicted_finish);
            if (!e.stale) pexec::log_plan(e);
        }
    }
    struct StatusOnExit
    {
        const Brain& b;
        const Ctx& c;
        ~StatusOnExit() { b.plan_publish_status(c); }
    } status_on_exit{ *this, ctx };
    if (e.stale) return;
    const auto& world = World::get();
    for (int guard = 0; guard < 4; ++guard)
    {
        const auto* t = pexec::current();
        if (!t)
        {
            e.stale = true;
            if (cfg::team_plan_log && fuel_ok(cfg::fuel_phase_log)) MB_LOG("PE r%d s%d plan done skips %d\n", ctx.round, kn.self_index, e.skips);
            return;
        }
        if (e.begin < 0)
        {
            e.begin = ctx.round;
            e.engage = -1;
            e.arrive = -1;
            e.lowhp = 0;
            e.saw_alive = e.saw_reward = false;
        }
        bool done = false, skip = false;
        const auto st = ctx.state;
        switch (t->kind)
        {
        case econ::TaskKind::clear:
        {
            const int g = t->group;
            if (g < 0 || g >= static_cast<int>(world.groups.size())) { skip = true; break; }
            const auto& b = kn.groups[static_cast<std::size_t>(g)];
            if (b.seen_round == ctx.round)
            {
                if (b.status == GroupStatus::alive) e.saw_alive = true;
                if (b.status == GroupStatus::reward) e.saw_reward = true;
            }
            if (e.engage < 0 && target_ == g && (engaged_ || capturing_)) e.engage = ctx.round;
            if (e.arrive < 0 && target_ == g && (engaged_ || capturing_ || staged_since_ >= 0)) e.arrive = ctx.round;
            if (b.status == GroupStatus::respawning)
            {
                if (e.saw_alive || e.saw_reward || capturing_) done = true;
                else if (b.respawn_round - ctx.round > cfg::team_plan_respawn_wait) done = true;  // taken by an ally (or stale)
            }
            else if (b.status == GroupStatus::reward && b.seen_round == ctx.round && !plan_capturer(ctx, g))
                done = true;  // the capturer holds it; we move on
            else if (banned_until_[static_cast<std::size_t>(g)] > ctx.round && target_ != g && e.begin < ctx.round)
                skip = true;  // the machinery gave it up (stall, watchdog, staging patience)
            else if (cfg::team_plan_overrun > 0 && e.arrive >= 0 && !capturing_ && b.status != GroupStatus::reward &&
                     ctx.round - e.arrive > cfg::team_plan_overrun + 2 * std::max(0, t->end_round - t->start_round))
                skip = true;  // far beyond its planned length: infeasible as planned
            break;
        }
        case econ::TaskKind::bank:
            if (e.engage < 0 && st.contains(cambc::champion_state::submitting_gems)) e.engage = ctx.round;
            if (ctx.gems == 0) done = true;
            break;
        case econ::TaskKind::recall:
            if (e.engage < 0 && st.contains(cambc::champion_state::recalling)) e.engage = ctx.round;
            if (ctx.on_spawn) done = true;
            break;
        case econ::TaskKind::wait:
            if (ctx.round >= t->end_round) done = true;
            break;
        }
        if (!done && !skip) break;
        if (skip) ++e.skips;
        if (cfg::team_plan_log && fuel_ok(cfg::fuel_phase_log))
            MB_LOG("PT s%d i%d %c%d p%x ps%d pe%d ab%d ar%d ag%d ae%d lh%d hp%d%s\n", kn.self_index, e.idx, pexec::kind_char(t->kind),
                   t->kind == econ::TaskKind::bank ? t->altar : t->group, t->party, t->start_round, t->end_round, e.begin, e.arrive, e.engage,
                   ctx.round, e.lowhp, static_cast<int>(100.0f * ctx.hp_frac()), skip ? " skip" : "");
        if (t->kind == econ::TaskKind::clear && target_ == t->group)
        {
            capturing_ = false;
            engaged_ = false;
            staged_since_ = -1;
        }
        ++e.idx;
        e.begin = -1;
        if (e.skips > cfg::team_plan_max_skips)
        {
            e.stale = true;
            if (cfg::team_plan_log && fuel_ok(cfg::fuel_phase_log)) MB_LOG("PE r%d s%d plan stale skips %d\n", ctx.round, kn.self_index, e.skips);
            return;
        }
    }
}

int Brain::plan_farm(Ctx& ctx, TurnActions& acts)
{
    auto& e = pexec::ex();
    e.forced = false;
    const auto* t = pexec::current();
    if (!t) return -1;
    auto& kn = Knowledge::get();
    auto& world = World::get();
    switch (t->kind)
    {
    case econ::TaskKind::recall:
    {
        if (ctx.on_spawn) return -1;
        publish(-1, static_cast<int>(SlotStatus::idle));
        target_ = -1;
        capturing_ = false;
        engaged_ = false;
        if (camp_threat_at(ctx, ctx.pos) > 0.0f || enemy_threat_at(ctx, ctx.pos) > 0.0f || ctx.hit_unseen)
        {
            why_ = "plan-recall-retreat";
            return retreat(ctx, acts) ? 1 : 0;
        }
        try
        {
            cambc::self().recall();
            why_ = "plan-recall";
            return 1;
        }
        catch (const std::exception& err)
        {
            MB_LOG("plan recall failed: %s\n", err.what());
        }
        return 0;
    }
    case econ::TaskKind::wait:
        publish(-1, static_cast<int>(SlotStatus::idle));
        target_ = -1;
        why_ = "await";
        leave_camp_tiles(ctx, acts);
        return 1;
    case econ::TaskKind::bank:
        // handle_tribute banks first; reaching here with a bag means no altar is reachable.
        if (ctx.gems > 0) ++e.skips, ++e.idx, e.begin = -1;
        return -1;
    case econ::TaskKind::clear: break;
    }
    const int g = t->group;
    if (target_ != g)
    {
        target_ = g;
        capturing_ = false;
        staged_since_ = -1;
        engaged_ = false;
        progress_best_ = 1 << 20;
        progress_round_ = ctx.round;
        chosen_round_ = ctx.round;
    }
    // Planned party members still alive (the party waits for them at the staging point).
    int need = 0;
    for (std::size_t i = 0; i < kn.allies.size() && i < 8; ++i)
        if ((t->party >> i) & 1u)
            if (static_cast<int>(i) == kn.self_index || kn.alive[i]) ++need;
    target_need_ = std::max(1, need);
    e.forced = true;
    const auto& b = kn.groups[static_cast<std::size_t>(g)];
    if (b.status == GroupStatus::respawning && b.respawn_round > ctx.round)
    {
        // Respawns soon (plan_turn skips the far ones): walk there in time, then wait beside it.
        const int walk = World::distance(world.group_field(g, false), ctx.route_pos, world.groups[static_cast<std::size_t>(g)].center);
        publish(g, static_cast<int>(SlotStatus::engaging));
        if (walk > std::max(2, b.respawn_round - ctx.round - 1))
        {
            why_ = "travel";
            travel_to_group(ctx, acts, g);
            return 1;
        }
        why_ = "await";
        leave_camp_tiles(ctx, acts);
        return 1;
    }
    return -1;
}

int Brain::plan_bank(const Ctx& ctx) const
{
    (void)ctx;
    const auto* t = pexec::current();
    if (!t) return -1;
    return t->kind == econ::TaskKind::bank ? 1 : 0;
}

bool Brain::plan_capturer(const Ctx& ctx, int group) const
{
    const auto* t = pexec::current();
    if (!t || t->kind != econ::TaskKind::clear || t->group != group) return true;
    auto& kn = Knowledge::get();
    const int c = t->capturer;
    if (c < 0 || c == kn.self_index || c >= static_cast<int>(kn.allies.size())) return true;
    if (!kn.alive[static_cast<std::size_t>(c)]) return true;
    // The capturer is on it: its slot names the group, or it stands by a member tile.
    const auto& slot = kn.slots()[static_cast<std::size_t>(c)];
    if (slot.group == group && (slot.status == SlotStatus::capturing || slot.status == SlotStatus::engaging)) return false;
    const auto handle = static_cast<cambc::handle_champion_t>(kn.allies[static_cast<std::size_t>(c)]);
    for (const auto& a : ctx.allies)
    {
        if (!a.champ || static_cast<cambc::handle_champion_t>(*a.champ) != handle) continue;
        for (auto tile : World::get().groups[static_cast<std::size_t>(group)].tiles)
            if (cheb(tile, a.pos) <= 2) return false;
    }
    return true;
}

bool Brain::plan_recall_on_capture() const
{
    const auto& e = pexec::ex();
    const auto* t = pexec::current();
    if (!t || t->kind != econ::TaskKind::clear) return false;
    const auto& q = e.plan.tasks[static_cast<std::size_t>(Knowledge::get().self_index)];
    const std::size_t next = static_cast<std::size_t>(e.idx) + 1;
    return next < q.size() && q[next].kind == econ::TaskKind::recall;
}

bool Brain::plan_party_member(const ChampView& ally) const
{
    const auto* t = pexec::current();
    if (!t || !ally.champ) return true;
    const auto& kn = Knowledge::get();
    const auto handle = static_cast<cambc::handle_champion_t>(*ally.champ);
    for (std::size_t i = 0; i < kn.allies.size() && i < 8; ++i)
        if (static_cast<cambc::handle_champion_t>(kn.allies[i]) == handle) return ((t->party >> i) & 1u) != 0;
    return false;
}

int Brain::plan_patience(const Ctx& ctx, int patience) const
{
    (void)ctx;
    const auto* t = pexec::current();
    if (!t || !pexec::ex().forced || staged_since_ < 0) return patience;
    return std::max(patience, t->start_round + cfg::team_plan_stage_slack - staged_since_);
}

// ---------------------------------------------------------------------------------------------
// Greedy test planner (cfg::team_plan 2).

namespace pexec {

namespace {

int est_dist(vec2 a, vec2 b) { return (cheb(a, b) * 5 + 3) / 4; }

} // namespace

econ::Plan test_plan(const econ::Snapshot& s, int node_budget)
{
    const auto& world = World::get();
    const auto& kn = Knowledge::get();
    const int n = static_cast<int>(s.champs.size());
    const int ng = static_cast<int>(world.groups.size());
    econ::Plan plan;
    plan.built_round = s.round;
    plan.tasks.assign(static_cast<std::size_t>(n), {});
    struct C
    {
        vec2 pos;
        int t;
        int bag;
        float used;
        float hp;
        model::ChampionSummary sum;
    };
    std::vector<C> cs;
    for (int i = 0; i < n; ++i)
    {
        const auto& st = s.champs[static_cast<std::size_t>(i)];
        cs.push_back({ st.pos, std::max(st.free_round, s.round), st.gems, 0.0f, st.max_hp,
                       design_summary(kn.allies[static_cast<std::size_t>(i)].champion_name()) });
    }
    std::vector<int> next_alive(static_cast<std::size_t>(ng), s.round);
    std::vector<model::FightConstants> fc(static_cast<std::size_t>(ng));
    for (int g = 0; g < ng; ++g)
    {
        const auto& info = world.groups[static_cast<std::size_t>(g)];
        fc[static_cast<std::size_t>(g)] = { info.total_hp, info.weighted_hp, info.total_strength };
    }
    const int capture = cambc::constants::capture_time;
    const int win = static_cast<int>(cambc::constants::winning_gems);
    int banked = s.banked;
    auto nearest_altar = [&](vec2 p) {
        int best = -1, bd = 1 << 20;
        for (std::size_t a = 0; a < world.altars.size(); ++a)
            if (est_dist(p, world.altars[a]) < bd) { bd = est_dist(p, world.altars[a]); best = static_cast<int>(a); }
        return best;
    };
    auto bank = [&](int i) {
        auto& c = cs[static_cast<std::size_t>(i)];
        const int a = nearest_altar(c.pos);
        if (a < 0 || c.bag == 0) return;
        const int arrive = c.t + est_dist(c.pos, world.altars[static_cast<std::size_t>(a)]);
        const int end = arrive + std::max(1, (c.bag + 9) / 10);
        econ::Task t;
        t.kind = econ::TaskKind::bank;
        t.altar = static_cast<std::int16_t>(a);
        t.party = static_cast<std::uint8_t>(1u << i);
        t.start_round = static_cast<std::int16_t>(arrive);
        t.end_round = static_cast<std::int16_t>(end);
        plan.tasks[static_cast<std::size_t>(i)].push_back(t);
        banked += c.bag;
        c.bag = 0;
        c.t = end;
        c.pos = world.altars[static_cast<std::size_t>(a)];
    };
    auto pool = [&](const C& c) { return c.hp * (1.0f - cfg::recall_health_frac) - c.used; };
    int nodes = 0;
    while (banked < win && nodes < node_budget)
    {
        int carried = 0;
        for (auto& c : cs) carried += c.bag;
        if (banked + carried >= win)
        {
            for (int i = 0; i < n; ++i) bank(i);
            break;
        }
        int i = 0;
        for (int k = 1; k < n; ++k)
            if (cs[static_cast<std::size_t>(k)].t < cs[static_cast<std::size_t>(i)].t) i = k;
        auto& c = cs[static_cast<std::size_t>(i)];
        if (c.t > 1900) break;
        // Best solo or pair option by gems per champion-round.
        float best_rate = 0.0f;
        int best_g = -1, best_j = -1, best_start = 0, best_rounds = 0;
        float best_damage = 0.0f;
        for (int g = 0; g < ng; ++g)
        {
            ++nodes;
            const auto& info = world.groups[static_cast<std::size_t>(g)];
            if (info.gems == 0) continue;
            for (int j = -1; j < n; ++j)
            {
                if (j == i) continue;
                const float dps_i = model::camp_dps(c.sum, info.neighbours, cfg::camp_dash_damage);
                const int arrive_i = c.t + est_dist(c.pos, info.center);
                int start = std::max(arrive_i, next_alive[static_cast<std::size_t>(g)]);
                float dps = dps_i, p = pool(c);
                int members = 1;
                if (j >= 0)
                {
                    const auto& d = cs[static_cast<std::size_t>(j)];
                    if (d.t > c.t + 30) continue;
                    start = std::max(start, d.t + est_dist(d.pos, info.center));
                    dps += model::camp_dps(d.sum, info.neighbours, cfg::camp_dash_damage);
                    p += pool(d);
                    members = 2;
                }
                const auto f = model::forecast_from_constants(fc[static_cast<std::size_t>(g)], dps, 1.0f);
                if (f.damage * cfg::camp_safety_margin > p) continue;
                const int span = start + f.rounds + capture - c.t;
                const float rate = static_cast<float>(info.gems) / static_cast<float>(std::max(1, span) * members);
                if (rate > best_rate)
                {
                    best_rate = rate;
                    best_g = g;
                    best_j = j;
                    best_start = start;
                    best_rounds = f.rounds;
                    best_damage = f.damage;
                }
            }
        }
        if (best_g < 0)
        {
            // Nothing feasible with the health left: recall (full heal), or stop.
            if (c.used <= 0.0f) break;
            econ::Task t;
            t.kind = econ::TaskKind::recall;
            t.party = static_cast<std::uint8_t>(1u << i);
            t.start_round = static_cast<std::int16_t>(c.t);
            t.end_round = static_cast<std::int16_t>(c.t + cambc::constants::recall_time + 1);
            plan.tasks[static_cast<std::size_t>(i)].push_back(t);
            c.t += cambc::constants::recall_time + 1;
            c.pos = world.ally_spawn_center;
            c.used = 0.0f;
            continue;
        }
        const auto& info = world.groups[static_cast<std::size_t>(best_g)];
        const std::uint8_t party = static_cast<std::uint8_t>((1u << i) | (best_j >= 0 ? 1u << best_j : 0u));
        const int cleared = best_start + best_rounds;
        const int captured = cleared + capture;
        const int members = best_j >= 0 ? 2 : 1;
        econ::Task t;
        t.kind = econ::TaskKind::clear;
        t.group = static_cast<std::int16_t>(best_g);
        t.party = party;
        t.capturer = static_cast<std::int8_t>(i);
        t.start_round = static_cast<std::int16_t>(best_start);
        t.end_round = static_cast<std::int16_t>(captured);
        plan.tasks[static_cast<std::size_t>(i)].push_back(t);
        c.t = captured;
        c.pos = info.center;
        c.bag += static_cast<int>(info.gems);
        c.used += best_damage / static_cast<float>(members);
        if (best_j >= 0)
        {
            auto& d = cs[static_cast<std::size_t>(best_j)];
            t.end_round = static_cast<std::int16_t>(cleared);
            plan.tasks[static_cast<std::size_t>(best_j)].push_back(t);
            d.t = cleared;
            d.pos = info.center;
            d.used += best_damage / static_cast<float>(members);
        }
        next_alive[static_cast<std::size_t>(best_g)] = captured + info.respawn;
        if (c.bag >= 60) bank(i);
    }
    plan.predicted_finish = 0;
    for (const auto& c : cs) plan.predicted_finish = std::max(plan.predicted_finish, c.t);
    return plan;
}

} // namespace pexec

} // namespace mb

// ---------------------------------------------------------------------------------------------
// Draft-phase plan (cfg::team_plan_draft): the draft bot (1M fuel on its first turn, whole map in
// view) plans for our deterministic roster and writes the task queues to draft memory; every
// champion decodes the same bytes, so all hold the identical plan from round 1.
//
// Layout: 'T' 'P' version n finish_lo finish_hi checksum, then per champion (draft order) its
// champion_type and task count, then the tasks, 6 bytes each:
//   b0 kind (2 bits) | (capturer + 1) << 2 (3 bits)   b1 target + 1 (group or altar)   b2 party
//   b3..b5 (start + 1) | (end + 1) << 11 (11 bits each, rounds clamped to 2046)
// Party bits and the capturer index refer to the draft order; decoding remaps them to roster slots.

namespace mb::pexec {

namespace {

constexpr std::uint8_t kVersion = 1;
constexpr std::size_t kHeader = 7;

std::vector<cambc::champion_type> types_of(const std::vector<cambc::champion_draft>& picks)
{
    std::vector<cambc::champion_type> out;
    for (const auto d : picks)
    {
        const int v = static_cast<int>(d);
        if (v < 3) out.push_back(static_cast<cambc::champion_type>(v));
        else if (v == 3)
        {
            out.push_back(cambc::champion_type::cassi);
            out.push_back(cambc::champion_type::issac);
        }
        else out.push_back(static_cast<cambc::champion_type>(v + 1));
    }
    return out;
}

std::vector<std::uint8_t> encode(const econ::Plan& plan, const std::vector<cambc::champion_type>& types, std::size_t capacity)
{
    const std::size_t n = types.size();
    std::vector<std::vector<econ::Task>> q(n);
    for (std::size_t i = 0; i < n && i < plan.tasks.size(); ++i)
    {
        q[i] = plan.tasks[i];
        while (!q[i].empty() && q[i].back().kind == econ::TaskKind::wait) q[i].pop_back();  // trailing waits carry nothing
    }
    // Fit the byte budget: take tasks round-robin so every champion keeps its earliest ones.
    const std::size_t room = capacity > kHeader + 2 * n ? (capacity - kHeader - 2 * n) / 6 : 0;
    std::vector<std::size_t> keep(n, 0);
    std::size_t used = 0;
    for (bool more = true; more && used < room;)
    {
        more = false;
        for (std::size_t i = 0; i < n && used < room; ++i)
            if (keep[i] < q[i].size() && keep[i] < 255) { ++keep[i]; ++used; more = true; }
    }
    std::vector<std::uint8_t> b{ 'T', 'P', kVersion, static_cast<std::uint8_t>(n),
                                 static_cast<std::uint8_t>(plan.predicted_finish & 255), static_cast<std::uint8_t>((plan.predicted_finish >> 8) & 255), 0 };
    for (std::size_t i = 0; i < n; ++i)
    {
        b.push_back(static_cast<std::uint8_t>(types[i]));
        b.push_back(static_cast<std::uint8_t>(keep[i]));
    }
    auto r11 = [](int r) { return static_cast<std::uint32_t>(std::clamp(r + 1, 0, 2047)); };
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t k = 0; k < keep[i]; ++k)
        {
            const auto& t = q[i][k];
            const int target = t.kind == econ::TaskKind::bank ? t.altar : t.group;
            b.push_back(static_cast<std::uint8_t>((static_cast<int>(t.kind) & 3) | (std::clamp(t.capturer + 1, 0, 7) << 2)));
            b.push_back(static_cast<std::uint8_t>(std::clamp(target + 1, 0, 255)));
            b.push_back(t.party);
            const std::uint32_t v = r11(t.start_round) | (r11(t.end_round) << 11);
            b.push_back(static_cast<std::uint8_t>(v & 255));
            b.push_back(static_cast<std::uint8_t>((v >> 8) & 255));
            b.push_back(static_cast<std::uint8_t>((v >> 16) & 255));
        }
    std::uint8_t sum = 0;
    for (std::size_t k = 0; k < b.size(); ++k)
        if (k != 6) sum = static_cast<std::uint8_t>(sum + b[k]);
    b[6] = sum;
    return b;
}

struct DraftState
{
    econ::SearchState* search{};
    bool done{};
    bool failed{};
    std::vector<cambc::champion_type> types;
};

DraftState& draft_state()
{
    static DraftState d;
    return d;
}

} // namespace

void draft_turn(const std::vector<cambc::champion_draft>& order)
{
    if constexpr (cfg::team_plan != 1 || !cfg::team_plan_draft) return;
    auto& d = draft_state();
    if (d.done || d.failed) return;
    try
    {
        if (!d.search)
        {
            // Our roster: the first team_size() picks of the (static) order; the picks so far must
            // be its prefix (opponents never block our picks; another roster fails at decoding).
            const auto size = static_cast<std::size_t>(cambc::draft::team_size());
            std::vector<cambc::champion_draft> picks;
            const auto mine = cambc::draft::current_draft(cambc::team::ally);
            for (const auto p : order)
            {
                if (picks.size() >= size) break;
                picks.push_back(p);
            }
            for (std::size_t i = 0; i < mine.size() && i < picks.size(); ++i)
                if (mine[i] != picks[i])
                {
                    d.failed = true;
                    MB_LOG("draft plan: roster mismatch\n");
                    return;
                }
            d.types = types_of(picks);
            if (!fuel_ok(600000)) return;  // only the first draft turn can afford the model
            d.search = econ::plan_begin(opening_snapshot(d.types, 1), cfg::team_plan_nodes);
            if (!d.search)
            {
                d.failed = true;
                return;
            }
        }
        bool finished = false;
        while (!finished && fuel_ok(cfg::team_plan_draft_margin)) finished = econ::plan_step(*d.search, cfg::team_plan_slice);
        if (!finished) return;
        const auto plan = econ::plan_result(*d.search);
        econ::plan_end(d.search);
        d.search = nullptr;
        d.done = true;
        const auto bytes = encode(plan, d.types, cambc::constants::draft_memory_bytes);
        cambc::draft_memory = std::span<const std::uint8_t>{ bytes.data(), bytes.size() };
        if (cfg::team_plan_log)
            MB_LOG("draft plan: %zu bytes, finish %d, fuel left %llu\n", bytes.size(), plan.predicted_finish, static_cast<unsigned long long>(fuel()));
    }
    catch (const std::exception& err)
    {
        d.failed = true;
        MB_LOG("draft plan error: %s\n", err.what());
    }
}

bool decode_draft(econ::Plan& out)
{
    const auto& mem = cambc::draft_memory;
    const std::size_t size = mem.size();
    if (size < kHeader) return false;
    auto at = [&](std::size_t k) { return std::to_integer<std::uint8_t>(mem[k]); };
    if (at(0) != 'T' || at(1) != 'P' || at(2) != kVersion) return false;
    const std::size_t n = at(3);
    if (size < kHeader + 2 * n) return false;
    const auto& kn = Knowledge::get();
    if (n != kn.allies.size() || n > 8) return false;
    std::vector<int> slot(n, -1);
    std::vector<int> counts(n);
    std::size_t total = 0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const auto type = static_cast<int>(at(kHeader + 2 * i));
        counts[i] = at(kHeader + 2 * i + 1);
        total += static_cast<std::size_t>(counts[i]);
        for (std::size_t s = 0; s < n; ++s)
            if (static_cast<int>(kn.allies[s].champion_name()) == type) slot[i] = static_cast<int>(s);
        if (slot[i] < 0) return false;
        for (std::size_t j = 0; j < i; ++j)
            if (slot[j] == slot[i]) return false;
    }
    const std::size_t end = kHeader + 2 * n + 6 * total;
    if (end > size) return false;
    std::uint8_t sum = 0;
    for (std::size_t k = 0; k < end; ++k)
        if (k != 6) sum = static_cast<std::uint8_t>(sum + at(k));
    if (sum != at(6)) return false;
    auto remap = [&](std::uint8_t bits) {
        std::uint8_t outb = 0;
        for (std::size_t i = 0; i < n; ++i)
            if ((bits >> i) & 1u) outb = static_cast<std::uint8_t>(outb | (1u << slot[i]));
        return outb;
    };
    out = econ::Plan{};
    out.built_round = 1;
    out.predicted_finish = at(4) | (at(5) << 8);
    out.tasks.assign(n, {});
    std::size_t k = kHeader + 2 * n;
    for (std::size_t i = 0; i < n; ++i)
        for (int c = 0; c < counts[i]; ++c, k += 6)
        {
            econ::Task t;
            const int b0 = at(k);
            t.kind = static_cast<econ::TaskKind>(b0 & 3);
            const int cap = ((b0 >> 2) & 7) - 1;
            t.capturer = static_cast<std::int8_t>(cap >= 0 && cap < static_cast<int>(n) ? slot[static_cast<std::size_t>(cap)] : -1);
            const int target = static_cast<int>(at(k + 1)) - 1;
            if (t.kind == econ::TaskKind::bank) t.altar = static_cast<std::int16_t>(target);
            else t.group = static_cast<std::int16_t>(target);
            t.party = remap(at(k + 2));
            const std::uint32_t v = static_cast<std::uint32_t>(at(k + 3)) | (static_cast<std::uint32_t>(at(k + 4)) << 8) |
                                    (static_cast<std::uint32_t>(at(k + 5)) << 16);
            t.start_round = static_cast<std::int16_t>(static_cast<int>(v & 2047) - 1);
            t.end_round = static_cast<std::int16_t>(static_cast<int>((v >> 11) & 2047) - 1);
            out.tasks[static_cast<std::size_t>(slot[i])].push_back(t);
        }
    return true;
}

} // namespace mb::pexec

// ---------------------------------------------------------------------------------------------
// Team replans (cfg::team_plan_sync). Every instance replans on the same rounds from the same
// snapshot, built only from facts all instances share: the current plan and the shared-memory
// progress bytes. In plan mode each champion's shared byte is
//   bits 0-4 task index in its current queue (31: beyond), bit 5 plan epoch parity,
//   bits 6-7 status (0 working, 1 capturing, 2 at spawn / recalling / v53 fallback, 3 dead)
// and v53's slot readers see it decoded back into (target group, status). Nobody writes in a sync
// round R, so every instance reads the bytes as they stood at the end of round R - 1. The search
// starts in R, runs in fixed slices in R+1..R+k and is adopted in R+k+1. Each champion first
// finishes the task it published in R (prepended to its new queue), then follows the new plan.

namespace mb::pexec {

namespace {

struct Sync
{
    bool active{};            // the shared bytes carry plan progress (draft plan decoded)
    int epoch{};
    econ::Plan prev;          // the plan before the last adoption (decodes old-epoch bytes)
    std::vector<int> bag0;    // predicted carried gems per slot at the epoch start
    int banked0{};
    int banked_r{};
    std::vector<int> group_until;  // predicted respawn end per group (identical everywhere)
    econ::SearchState* search{};
    int sync_round{ -1 };
    std::vector<int> pub_idx, pub_status;  // bytes read in the sync round
    int my_status{ 2 };
    int replans{};
};

Sync& sy()
{
    static Sync s;
    return s;
}

std::uint8_t read_byte(std::size_t i)
{
    std::byte b{};
    cambc::shared_memory.read(i, std::span<std::byte>{ &b, 1 });
    return std::to_integer<std::uint8_t>(b);
}

void write_byte(std::size_t i, std::uint8_t v)
{
    const std::byte b{ v };
    cambc::shared_memory.write(i, std::span<const std::byte>{ &b, 1 });
}

int gems_of(int g)
{
    const auto& w = World::get();
    return g >= 0 && g < static_cast<int>(w.groups.size()) ? static_cast<int>(w.groups[static_cast<std::size_t>(g)].gems) : 0;
}

// Snapshot for the plan adopted in round `adopt`, from the plan and the bytes read in the sync round.
econ::Snapshot sync_snapshot(int round, int adopt)
{
    const auto& e = ex();
    auto& s = sy();
    const auto& world = World::get();
    const auto& kn = Knowledge::get();
    std::vector<cambc::champion_type> types;
    for (const auto& c : kn.allies) types.push_back(c.champion_name());
    econ::Snapshot snap = opening_snapshot(types, adopt);
    const std::size_t n = std::min(snap.champs.size(), e.plan.tasks.size());
    int banked = s.banked_r;  // the real total as read in the sync round (a tribute completing within that round can split instances)
    std::vector<int> until = s.group_until;
    until.resize(world.groups.size(), 0);
    for (std::size_t i = 0; i < n; ++i)
    {
        const auto& q = e.plan.tasks[i];
        auto& c = snap.champs[i];
        const int k = std::min(s.pub_idx[i], static_cast<int>(q.size()));
        const int status = s.pub_status[i];
        int bag = i < s.bag0.size() ? s.bag0[i] : 0;
        for (int j = 0; j < k; ++j)
        {
            const auto& t = q[static_cast<std::size_t>(j)];
            if (t.kind == econ::TaskKind::clear && t.capturer == static_cast<int>(i))
            {
                bag += gems_of(t.group);
                if (t.group >= 0 && t.group < static_cast<int>(until.size()))
                    until[static_cast<std::size_t>(t.group)] = std::max(until[static_cast<std::size_t>(t.group)],
                                                                        t.end_round + world.groups[static_cast<std::size_t>(t.group)].respawn);
            }
            else if (t.kind == econ::TaskKind::bank)
            {
                (void)0;  // banked: the real total already holds completed tributes
                bag = 0;
            }
        }
        c.pos = world.ally_spawn_center;
        c.free_round = adopt;
        c.hp = c.max_hp;
        if (status == 3)
        {
            c.alive = false;
            c.respawn_round = adopt + 30;
        }
        else if ((status == 0 || status == 1) && k < static_cast<int>(q.size()))
        {
            // The published task is finished first (it heads the new queue).
            const auto& t = q[static_cast<std::size_t>(k)];
            c.hp = 0.75f * c.max_hp;
            switch (t.kind)
            {
            case econ::TaskKind::clear:
            {
                const int g = t.group;
                if (g >= 0 && g < static_cast<int>(world.groups.size()))
                {
                    c.pos = world.groups[static_cast<std::size_t>(g)].center;
                    const int rem = status == 1 ? 4 : std::clamp(t.end_round - round, 6, std::max(6, t.end_round - t.start_round + 10));
                    c.free_round = std::max(adopt, round + rem);
                    if (t.capturer == static_cast<int>(i)) bag += gems_of(g);
                    until[static_cast<std::size_t>(g)] = std::max(until[static_cast<std::size_t>(g)], c.free_round + world.groups[static_cast<std::size_t>(g)].respawn);
                }
                break;
            }
            case econ::TaskKind::bank:
                if (t.altar >= 0 && t.altar < static_cast<int>(world.altars.size())) c.pos = world.altars[static_cast<std::size_t>(t.altar)];
                c.free_round = std::max(adopt, round + bag / 10 + 8);
                banked += bag;
                bag = 0;
                break;
            case econ::TaskKind::recall:
                c.free_round = std::max(adopt, round + 6);
                c.hp = c.max_hp;
                break;
            case econ::TaskKind::wait:
                c.free_round = std::max(adopt, static_cast<int>(t.end_round));
                break;
            }
        }
        c.gems = bag;
    }
    snap.banked = banked;
    for (std::size_t g = 0; g < snap.groups.size() && g < until.size(); ++g)
        if (until[g] > adopt)
        {
            snap.groups[g].alive = false;
            snap.groups[g].respawn_round = until[g];
        }
    return snap;
}

void adopt_plan(int round)
{
    auto& e = ex();
    auto& s = sy();
    const auto& kn = Knowledge::get();
    const int adopt = s.sync_round + cfg::team_plan_sync_turns + 1;
    // The snapshot's predictions become the new epoch's starting facts (recomputed identically).
    const auto snap = sync_snapshot(s.sync_round, adopt);
    auto next = econ::plan_result(*s.search);
    econ::plan_end(s.search);
    s.search = nullptr;
    next.tasks.resize(e.plan.tasks.size());
    for (std::size_t i = 0; i < next.tasks.size(); ++i)
    {
        const auto& q = e.plan.tasks[i];
        const int k = s.pub_idx[i];
        if ((s.pub_status[i] == 0 || s.pub_status[i] == 1) && k < static_cast<int>(q.size()))
            next.tasks[i].insert(next.tasks[i].begin(), q[static_cast<std::size_t>(k)]);
        if (next.tasks[i].size() > 31) next.tasks[i].resize(31);
    }
    s.bag0.assign(next.tasks.size(), 0);
    for (std::size_t i = 0; i < next.tasks.size() && i < snap.champs.size(); ++i) s.bag0[i] = snap.champs[i].gems;
    s.banked0 = snap.banked;
    s.group_until.assign(snap.groups.size(), 0);
    for (std::size_t g = 0; g < snap.groups.size(); ++g)
        if (!snap.groups[g].alive) s.group_until[g] = snap.groups[g].respawn_round;
    const int me = kn.self_index;
    const bool carried = (s.pub_status[static_cast<std::size_t>(me)] == 0 || s.pub_status[static_cast<std::size_t>(me)] == 1) &&
                         s.pub_idx[static_cast<std::size_t>(me)] < static_cast<int>(e.plan.tasks[static_cast<std::size_t>(me)].size());
    const bool moved_on = e.stale || e.idx != s.pub_idx[static_cast<std::size_t>(me)];
    s.prev = std::move(e.plan);
    e.plan = std::move(next);
    ++s.epoch;
    ++s.replans;
    e.stale = false;
    e.skips = 0;
    if (!carried || moved_on)
    {
        e.idx = carried ? 1 : 0;
        e.begin = -1;
    }
    else
        e.idx = 0;  // still on the carried task: keep its progress
    if (cfg::team_plan_log && fuel_ok(8000))
    {
        MB_LOG("PR r%d s%d epoch %d finish %d banked %d idx %d\n", round, me, s.epoch, e.plan.predicted_finish, snap.banked, e.idx);
        log_plan(e);
    }
}

} // namespace

bool slots_active() { return sy().active; }

void decode_slot(std::size_t i, std::uint8_t byte, int& group, int& status)
{
    const auto& e = ex();
    const auto& s = sy();
    group = -1;
    status = 3;  // SlotStatus::idle
    const int st = byte >> 6;
    const int idx = byte & 31;
    const bool cur = ((byte >> 5) & 1) == (s.epoch & 1);
    const auto& plan = cur ? e.plan : s.prev;
    if (st > 1 || i >= plan.tasks.size() || idx >= static_cast<int>(plan.tasks[i].size())) return;
    const auto& t = plan.tasks[i][static_cast<std::size_t>(idx)];
    if (t.kind != econ::TaskKind::clear) return;
    group = t.group;
    status = st == 1 ? 1 : 0;  // capturing : engaging
}

void sync(int round, bool alive)
{
    auto& e = ex();
    auto& s = sy();
    auto& kn = Knowledge::get();
    if (!e.draft_tried)
    {
        e.draft_tried = true;
        if (decode_draft(e.plan))
        {
            e.built = true;
            e.stale = kn.self_index >= static_cast<int>(e.plan.tasks.size());
            s.active = cfg::team_plan_sync > 0 && !e.stale && kn.allies.size() <= cambc::shared_memory_buffer::size();
            s.bag0.assign(e.plan.tasks.size(), 0);
            if (cfg::team_plan_log && fuel_ok(8000))
            {
                MB_LOG("PP r%d draft s%d finish %d\n", round, kn.self_index, e.plan.predicted_finish);
                if (!e.stale) log_plan(e);
            }
        }
        else if (cfg::team_plan_log && fuel_ok(8000))
            MB_LOG("PP r%d s%d no draft plan\n", round, kn.self_index);
    }
    if (!s.active) return;
    const int P = cfg::team_plan_sync;
    const int k = cfg::team_plan_sync_turns;
    const auto me = static_cast<std::size_t>(kn.self_index);
    const bool sync_round = round >= P && round % P == 0 && s.search == nullptr;
    if (!sync_round)
    {
        const int status = alive ? s.my_status : 3;
        const int idx = std::min(e.idx, 31);
        write_byte(me, static_cast<std::uint8_t>(idx | ((s.epoch & 1) << 5) | (status << 6)));
    }
    if (sync_round)
    {
        const auto before = fuel();
        s.sync_round = round;
        s.banked_r = static_cast<int>(cambc::global_submitted_gems(cambc::team::ally));
        s.pub_idx.assign(e.plan.tasks.size(), 31);
        s.pub_status.assign(e.plan.tasks.size(), 3);
        for (std::size_t i = 0; i < e.plan.tasks.size(); ++i)
        {
            const std::uint8_t b = read_byte(i);
            const bool cur = ((b >> 5) & 1) == (s.epoch & 1);
            s.pub_status[i] = b >> 6;
            s.pub_idx[i] = cur ? (b & 31) : 0;  // a byte from the old epoch: at the head of its queue
        }
        s.search = econ::plan_begin(sync_snapshot(round, round + k + 1), cfg::team_plan_replan_nodes);
        if (cfg::team_plan_log && fuel_ok(8000))
            MB_LOG("PS r%d s%d begin fuel %llu\n", round, kn.self_index, static_cast<unsigned long long>(before - std::min(before, fuel())));
        return;
    }
    if (!s.search) return;
    if (round <= s.sync_round + k)
    {
        const auto before = fuel();
        econ::plan_step(*s.search, (cfg::team_plan_replan_nodes + k - 1) / k);
        if (cfg::team_plan_log && fuel_ok(8000))
            MB_LOG("PS r%d s%d step fuel %llu\n", round, kn.self_index, static_cast<unsigned long long>(before - std::min(before, fuel())));
        return;
    }
    adopt_plan(round);
}

void set_status(int status) { sy().my_status = status; }

void activate_sync()
{
    auto& s = sy();
    const auto& e = ex();
    s.active = cfg::team_plan_sync > 0 && !e.stale && Knowledge::get().allies.size() <= cambc::shared_memory_buffer::size();
    s.bag0.assign(e.plan.tasks.size(), 0);
}

} // namespace mb::pexec
