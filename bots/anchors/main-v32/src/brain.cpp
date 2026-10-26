#include "brain.h"
#include <algorithm>
#include <cmath>
#include <string_view>
#include "config.h"
#include "knowledge.h"
#include "nav.h"
#include "shop.h"
#include "world.h"

namespace mb {

namespace {

struct AllyPower
{
    float dps{};
    float hp{};
    float pvp{};  // design damage rate against champions
};

// Design-based power of every ally slot (types never change during a match).
const std::vector<AllyPower>& ally_powers()
{
    static const std::vector<AllyPower> powers = [] {
        std::vector<AllyPower> out;
        for (const auto& c : Knowledge::get().allies)
        {
            const auto s = design_summary(c.champion_name());
            out.push_back({ model::camp_dps(s, 0.0f, cfg::camp_dash_damage), s.health });
        }
        return out;
    }();
    return powers;
}

// Design damage rate and health per champion type (13 types), cached.
const AllyPower& type_power(cambc::champion_type type)
{
    static AllyPower cache[16];
    static bool known[16]{};
    const auto i = static_cast<std::size_t>(type) & 15u;
    if (!known[i])
    {
        const auto s = design_summary(type);
        cache[i] = { model::camp_dps(s, 0.0f, cfg::camp_dash_damage), s.health, model::pvp_dps(s) };
        known[i] = true;
    }
    return cache[i];
}

float my_dps(const Ctx& ctx, float neighbours)
{
    auto s = kit().summary;
    s.strength = ctx.strength;
    s.focus = ctx.focus;
    return model::camp_dps(s, neighbours, cfg::camp_dash_damage);
}

// Our items scale everyone roughly alike; use our own growth as the team's progress proxy.
float growth(const Ctx& ctx)
{
    const float base = std::max(1.0f, kit().summary.strength);
    return std::clamp(ctx.strength / base, 1.0f, 4.0f);
}

// Live monsters of a visible group from this turn's views (exact health).
std::vector<model::Monster> live_monsters(const Ctx& ctx, int group)
{
    std::vector<model::Monster> out;
    for (const auto& c : ctx.camps)
        if (c.group == group && c.hp > 0.0f) out.push_back({ c.hp, c.strength });
    return out;
}

int nearest_altar_cheb(vec2 from)
{
    const auto& world = World::get();
    int best = 1 << 20;
    for (auto a : world.altars) best = std::min(best, cheb(from, a));
    return best;
}

vec2 nearest_altar(vec2 from)
{
    const auto& world = World::get();
    vec2 best{};
    int bd = 1 << 20;
    for (auto a : world.altars)
        if (cheb(from, a) < bd) { bd = cheb(from, a); best = a; }
    return best;
}

} // namespace

void Brain::publish(int group, int status) { Knowledge::get().write_slot(group, static_cast<SlotStatus>(status)); }

void Brain::turn()
{
    FuelTrace trace;
    trace.begin();
    turn_inner(trace);
    trace.mark("end");
    if (fuel_ok(cfg::fuel_phase_log)) trace.report(static_cast<int>(cambc::round_number()), 60000);
}

void Brain::turn_inner(FuelTrace& trace)
{
    // Evaluation seed (cfg::noise_burn, local experiments only): burn a little fuel each turn so the
    // otherwise deterministic games diverge into an independent but equivalent sample.
    if (cfg::noise_burn > 0)
    {
        volatile int sink = 0;
        for (int i = 0; i < cfg::noise_burn; ++i) sink = sink + i;
    }
    new_turn_obstacles();
    auto& me = cambc::self();
    auto& kn = Knowledge::get();
    auto& world = World::get();
    if (!initialised_)
    {
        // First gameplay turn: build every static cache (expensive on camp-dense maps)
        // and only act if a comfortable budget remains; nothing is urgent at spawn.
        kit();
        roster(cambc::team::ally);
        roster(cambc::team::enemy);
        ally_powers();
        initialised_ = true;
        trace.mark("init");
        if (!fuel_ok(70000)) return;
    }
    // Calibrate the field-cost estimate with a whole-map search from our spawn (near the worst
    // case for search depth) as soon as it is affordable; until then the estimate is cautious.
    if (!world.calibrated() && fuel_ok(world.field_cost() + 20000)) world.spawn_field(true);
    // A gated altar's field is static and rarely affordable mid-turn: build it early.
    if (!world.gate_tiles.empty() && world.calibrated() && fuel_ok(world.field_cost() + 20000)) world.gate_field(true);
    trace.mark("init");
    if (banned_until_.empty()) banned_until_.assign(world.groups.size(), 0);
    kn.round = static_cast<int>(cambc::round_number());
    kn.read_slots();
    trace.mark("slots");

    if (!me.is_alive())
    {
        if (!death_reported_) report_death(kn.round);
        Ctx ctx;
        ctx.round = kn.round;
        shop(ctx);  // dead champions may shop
        target_ = -1;
        capturing_ = false;
        publish(-1, static_cast<int>(SlotStatus::idle));
        return;
    }

    Ctx ctx;
    trace.mark("ctx");
    ctx.observe();
    trace.mark("observe");
    kn.observe_groups(ctx, kit().vision + 1);
    remember_threats(ctx);
    ctx.gem_weight = gem_weight(ctx);
    trace.mark("groups");
    if (!ctx.on_spawn) hold_since_ = -1;  // a new spawn visit gets a fresh hold allowance
    // Our current target's route is worth computing while the turn's budget is fresh. After a
    // blocked travel turn with nothing in view, spend most of this turn on it (the cached field
    // unblocks every later turn; without it greedy steps stall at walls and holes).
    if (target_ >= 0)
        world.group_field(target_, true, cfg::stuck_field_margin > 0 && travel_blocked_ && ctx.enemies.empty() ? cfg::stuck_field_margin : 0);
    travel_blocked_ = false;
    trace.mark("route");

    const auto st = ctx.state;
    // A tribute channel survives damage but not death, and the killer takes the whole bag: when
    // an enemy can strike next turn and the channel has more than a turn left, cancel it and
    // let the threat logic act (a 1600-gem bag needs ~160 turns at the altar).
    bool cancelled = false;
    if (st.contains(cambc::champion_state::submitting_gems) && tribute_end_round_ - ctx.round > 1)
    {
        // Only when the enemies that can reach us would kill us before the channel completes.
        float incoming = 0.0f;
        for (const auto& e : ctx.enemies)
            if (cheb(e.pos, ctx.pos) <= (e.range <= 1 ? 3 : 5)) incoming += e.dps;
        const bool danger = incoming * static_cast<float>(tribute_end_round_ - ctx.round) >= ctx.hp * 0.8f;
        if (danger)
            try
            {
                cambc::self().cancel_submit_gems();
                cancelled = true;
                MB_LOG("tribute cancelled r%d gems %u left %d\n", ctx.round, ctx.gems, tribute_end_round_ - ctx.round);
            }
            catch (const std::exception& e)
            {
                MB_LOG("cancel tribute failed: %s\n", e.what());
            }
    }
    if (!cancelled && (st.contains(cambc::champion_state::recalling) || st.contains(cambc::champion_state::submitting_gems) ||
                       st.contains(cambc::champion_state::teleporting)))
    {
        publish(-1, static_cast<int>(SlotStatus::idle));
        warm_fields(ctx);
        return;
    }
    if (cancelled) ctx.refresh_self();

    if (ctx.on_spawn || kit().remote_shop) shop(ctx);
    trace.mark("shop");
    // Stunned (no action allowed at all): publish and wait.
    if (!ctx.state.contains(cambc::champion_state::can_move) && !ctx.state.contains(cambc::champion_state::can_attack) &&
        !ctx.state.contains(cambc::champion_state::can_cast))
    {
        publish(target_, static_cast<int>(SlotStatus::engaging));
        return;
    }

    TurnActions acts;
    bool done = false;
    if (cfg::lock_mode > 0 && fuel_ok(cfg::fuel_phase_basic)) lock_enemy_tribute(ctx);
    // Healing on spawn happens at turn end: stay for the turn we arrive (or were revived).
    // Later turns on spawn below full health mean someone is hitting us across the edge;
    // waiting would never end, so act normally.
    spawn_turns_ = ctx.on_spawn ? spawn_turns_ + 1 : 0;
    if (ctx.on_spawn && ctx.hp_frac() < 0.999f && spawn_turns_ <= 1)
    {
        publish(target_, static_cast<int>(SlotStatus::idle));
        done = true;
    }
    const char* mode = done ? "heal" : "idle";
    why_ = "-";
    // Phases run in priority order while fuel lasts; each later phase needs a larger margin
    // so a heavy turn degrades to fewer decisions instead of being suspended mid-action.
    if (!done && fuel_ok(cfg::fuel_phase_threats) && (done = handle_threats(ctx, acts))) mode = "pvp";
    trace.mark("threats");
    if (!done && fuel_ok(cfg::fuel_phase_basic) && (done = handle_low_health(ctx, acts))) mode = "lowhp";
    trace.mark("health");
    if (!done && fuel_ok(cfg::fuel_phase_basic) && (done = handle_tribute(ctx, acts))) mode = "tribute";
    trace.mark("tribute");
    if (!done && fuel_ok(cfg::fuel_phase_farm) && (done = handle_farm(ctx, acts))) mode = capturing_ ? "capture" : "farm";
    trace.mark("farm");
    // The travel stall detector only counts consecutive travelling turns.
    if (std::string_view(why_) != "travel" && std::string_view(why_) != "idle-blocked") progress_round_ = ctx.round;
    if (cfg::debug_trace_hi > 0 && ctx.round >= cfg::debug_trace_lo && ctx.round <= cfg::debug_trace_hi &&
        kn.self_index == cfg::debug_trace_champion)
    {
        const auto& b = target_ >= 0 ? kn.groups[static_cast<std::size_t>(target_)] : GroupBelief{};
        const int d = target_ >= 0 ? World::distance(world.group_field(target_, false), ctx.pos, world.groups[static_cast<std::size_t>(target_)].center) : -1;
        MB_LOG("T r%d p%d,%d hp%.0f %s/%s e%d t%d st%d seen%d d%d camps%zu allies%zu acts%d%d%d\n", ctx.round, ctx.pos.x, ctx.pos.y, ctx.hp, mode, why_, engaged_,
               target_, static_cast<int>(b.status), b.seen_round == ctx.round, d, ctx.camps.size(), ctx.allies.size(),
               acts.moved, acts.attacked, acts.cast);
        for (const auto& c : ctx.camps)
            if (cheb(c.pos, ctx.pos) <= 6)
                MB_LOG("  c%d,%d g%d r%d s%.0f hp%.0f aw%d d%d\n", c.pos.x, c.pos.y, c.group, c.range, c.strength, c.hp, c.awake, cheb(c.pos, ctx.pos));
    }
    note_turn(ctx, mode);
    last_hp_ = ctx.hp;
    if (cfg::debug_pvp_hi > 0 && ctx.round >= cfg::debug_pvp_lo && ctx.round <= cfg::debug_pvp_hi && !ctx.enemies.empty() && fuel_ok(20000))
    {
        char buf[512];
        int len = std::snprintf(buf, sizeof buf, "P r%d k%d p%d,%d hp%.0f g%u %s s%.1f-%.1f act%d%d%d |", ctx.round, static_cast<int>(kit().type), ctx.pos.x,
                                ctx.pos.y, ctx.hp, ctx.gems, mode, last_sim_[0], last_sim_[1], acts.moved, acts.attacked, acts.cast);
        for (const auto& e : ctx.enemies)
            if (len < 440) len += std::snprintf(buf + len, sizeof buf - static_cast<std::size_t>(len), " e%d@%d,%d:%.0f", static_cast<int>(e.type), e.pos.x, e.pos.y, e.hp);
        for (const auto& a : ctx.allies)
            if (len < 480 && cheb(a.pos, ctx.pos) <= 8)
                len += std::snprintf(buf + len, sizeof buf - static_cast<std::size_t>(len), " a%d@%d,%d:%.0f", static_cast<int>(a.type), a.pos.x, a.pos.y, a.hp);
        MB_LOG("%s\n", buf);
    }
    // Time allocation by activity (diagnostics): mode, refined by the farm branch.
    {
        const std::string_view m{ mode }, w{ why_ };
        int bucket = 7;  // idle (sub-causes counted separately)
        if (m == "idle")
        {
            if (w == "idle-fuel") ++idle_use_[0];
            else if (w == "idle-none") ++idle_use_[1];
            else if (w == "idle-resp") ++idle_use_[2];
            else if (w == "idle-stall") ++idle_use_[3];
            else if (w == "idle-blocked") ++idle_use_[4];
            else ++idle_use_[5];
        }
        if (m == "pvp") bucket = 0;
        else if (m == "lowhp" || m == "heal") bucket = 1;
        else if (m == "tribute") bucket = 2;
        else if (m == "capture") bucket = 3;
        else if (w == "fight" || w == "fight-travel") bucket = 4;
        else if (w == "stage") bucket = 5;
        else if (w == "await") bucket = 8;
        else if (w == "travel") bucket = 6;
        ++time_use_[bucket];
    }
    if (ctx.round % 500 == 0 && fuel_ok(cfg::fuel_phase_log))
        MB_LOG("t r%d k%d pvp%d hp%d trib%d cap%d fight%d stage%d trav%d idle%d await%d\n", ctx.round, static_cast<int>(kit().type),
               time_use_[0], time_use_[1], time_use_[2], time_use_[3], time_use_[4], time_use_[5], time_use_[6], time_use_[7], time_use_[8]);
    if (ctx.round % 500 == 0 && fuel_ok(cfg::fuel_phase_log))
        MB_LOG("i r%d k%d fuel%d none%d resp%d stall%d blocked%d other%d\n", ctx.round, static_cast<int>(kit().type), idle_use_[0],
               idle_use_[1], idle_use_[2], idle_use_[3], idle_use_[4], idle_use_[5]);
    if (cfg::standoff_recall_turns > 0 && ctx.round % 500 == 0 && fuel_ok(cfg::fuel_phase_log))
        MB_LOG("standoff recalls r%d k%d %d\n", ctx.round, static_cast<int>(kit().type), standoff_recalls_);
    if (cfg::lock_mode > 0 && ctx.round % 500 == 0 && fuel_ok(cfg::fuel_phase_log))
        MB_LOG("locks r%d k%d %d\n", ctx.round, static_cast<int>(kit().type), locks_placed_);
    if (cfg::travel_recall_margin > 0 && ctx.round % 500 == 0 && fuel_ok(cfg::fuel_phase_log))
        MB_LOG("travel recalls r%d k%d %d\n", ctx.round, static_cast<int>(kit().type), travel_recalls_);
    if (ctx.round % 100 == 0 && fuel_ok(cfg::fuel_phase_log))
        MB_LOG("s r%d k%d p%d,%d hp%d/%d g%u au%u %s t%d\n", ctx.round, static_cast<int>(kit().type), ctx.pos.x, ctx.pos.y,
               static_cast<int>(ctx.hp), static_cast<int>(ctx.max_hp), ctx.gems, ctx.gold, mode, target_);
    if (!acts.cast && fuel_ok(cfg::fuel_phase_basic))
        use_support(ctx, acts, camp_threat_at(ctx, ctx.pos) > 0.0f || enemy_threat_at(ctx, ctx.pos) > 0.0f);
    if (cfg::ward_mode > 0 && fuel_ok(cfg::fuel_phase_basic)) maybe_place_ward(ctx);
    trace.mark("support");
    warm_fields(ctx);
    trace.mark("warm");
}

void Brain::warm_fields(const Ctx& ctx)
{
    auto& world = World::get();
    // Prioritise the altar field, then groups nearest to us.
    if (!world.altar_field(false))
    {
        if (!fuel_ok(world.field_cost() + 30000)) return;
        world.altar_field(true);
    }
    for (int guard = 0; guard < 64; ++guard)
    {
        if (!fuel_ok(world.field_cost() + 30000)) return;
        int best = -1, best_d = 1 << 20;
        for (std::size_t g = 0; g < world.groups.size(); ++g)
        {
            if (world.group_field(static_cast<int>(g), false)) continue;
            const int d = cheb(ctx.pos, world.groups[g].center);
            if (d < best_d) { best_d = d; best = static_cast<int>(g); }
        }
        if (best < 0) return;
        world.group_field(best, true);
    }
}

bool Brain::retreat(Ctx& ctx, TurnActions& acts)
{
    FuelSpike spike{ "retreat", 30000 };
    last_retreat_round_ = ctx.round;
    auto& world = World::get();
    const auto* spawn = world.spawn_field(false);
    // Threat sources gathered once (tile scoring below runs for up to ~30 candidates).
    struct Source { vec2 pos; int reach; float rate; };
    Source sources[24];
    int n_sources = 0;
    for (const auto& c : ctx.camps)
        if (c.awake && c.strength > 0.0f && cheb(c.pos, ctx.pos) <= c.range + 3 && n_sources < 24)
            sources[n_sources++] = { c.pos, c.range, c.strength * ctx.camp_damage_taken_mult };
    for (int i = 0; i < ctx.n_unseen && n_sources < 24; ++i)
        sources[n_sources++] = { ctx.unseen[i].pos, ctx.unseen[i].range, ctx.unseen[i].strength * ctx.camp_damage_taken_mult };
    // Enemies move: they repel over a buffer beyond their reach, or a chaser standing between us
    // and home turns the walk home into circles at the edge of its reach.
    for (const auto& e : ctx.enemies)
        if (cheb(e.pos, ctx.pos) <= 8 && n_sources < 24)
            sources[n_sources++] = { e.pos, enemy_reach(e) + cfg::retreat_enemy_buffer, e.dps };
    // Healthy allies nearby can turn a chase into a fight we win: drift towards the nearest.
    vec2 ally_pos{ -1, -1 };
    int ally_d = 1 << 20;
    for (const auto& a : ctx.allies)
        if (a.hp > 0.5f * a.max_hp && cheb(a.pos, ctx.pos) < ally_d && cheb(a.pos, ctx.pos) <= 10)
        {
            ally_d = cheb(a.pos, ctx.pos);
            ally_pos = a.pos;
        }
    const bool chased = !ctx.enemies.empty();
    // Each source weighs by the steps still needed to leave its reach, so deep inside a range
    // the score keeps pointing outwards (a flat in-range penalty let the home term walk us along
    // the inside of a monster's range until we died).
    auto threat = [&](vec2 n) {
        float t = 0.0f;
        for (int i = 0; i < n_sources; ++i)
        {
            const int depth = sources[i].reach + 1 - cheb(n, sources[i].pos);
            if (depth > 0) t += sources[i].rate * static_cast<float>(depth);
        }
        return t;
    };
    // Safety field: walking distance to the nearest tile outside every threat's reach (bounded
    // search from those tiles). A local gradient walked a champion into a map corner pocket
    // whose every tile was in an elk's range (01yellow-brick-road).
    std::unique_ptr<cambc::distance_field> safety;
    if (n_sources > 0 && fuel_ok(cfg::fuel_safety_field))
    {
        cambc::bitboard threatened;
        for (int i = 0; i < n_sources; ++i) threatened.draw_chebyshev(sources[i].pos, static_cast<float>(sources[i].reach));
        const auto& walls = blocked_now();
        const cambc::bitboard safe = ~(walls | threatened);
        cambc::bitboard visited;
        safety = std::make_unique<cambc::distance_field>();
        safety->compute(walls, safe, visited, static_cast<std::uint16_t>(cfg::safety_field_radius));
        if (safety->get(ctx.pos) == cambc::distance_field::unreachable)
        {
            bool any = false;  // no safe tile within the radius: keep the local scoring
            for (auto n : step_options(walls, ctx.pos))
                if (safety->get(n) != cambc::distance_field::unreachable) any = true;
            if (!any) safety.reset();
        }
    }
    auto score = [&](vec2 n) {
        const int home = spawn ? static_cast<int>(spawn->get(n)) : cheb(n, world.ally_spawn_center);
        const int ally = chased && ally_pos.x >= 0 ? cfg::retreat_ally_pull * cheb(n, ally_pos) : 0;
        int to_safety = 0;
        if (safety)
        {
            const auto d = safety->get(n);
            to_safety = d == cambc::distance_field::unreachable ? 100 : static_cast<int>(d);
        }
        return -to_safety * 100000 - static_cast<int>(threat(n) * 10.0f) - home - ally;
    };
    // Melee champions can strike while backing off.
    if (!kit().ranged && fight_enemies(ctx, acts, false)) {}
    // Dash out when something can still reach us (a walk alone rarely escapes melee chasers).
    if (!acts.cast && threat(ctx.pos) > 0.0f && fuel_ok(cfg::fuel_escape_dash) && cambc::self().can_cast())
    {
        const auto abilities = cambc::self().abilities();
        for (int i = 0; i < 2 && !acts.cast; ++i)
        {
            const auto& s = kit().slot[i];
            const auto& a = abilities[static_cast<std::size_t>(i)];
            if (s.kind != model::AbilityKind::dash || (s.exclusive && acts.moved) || a.cooldown() > 0) continue;
            // Same legality model as nav's dash_landings.
            const auto landings = dash_landings(ctx.pos, static_cast<int>(s.range));
            vec2 best = ctx.pos;
            int best_score = score(ctx.pos);
            for (auto n : landings)
            {
                const int sc = score(n);
                if (sc > best_score) { best_score = sc; best = n; }
            }
            if (best == ctx.pos) continue;
            try
            {
                if (a.use(best))
                {
                    acts.cast = true;
                    ctx.refresh_self();
                }
            }
            catch (const std::exception& e)
            {
                MB_LOG("escape dash failed: %s\n", e.what());
            }
        }
    }
    const bool moved = step_best(score, score(ctx.pos));
    if (moved)
    {
        acts.moved = true;
        ctx.refresh_self();
    }
    return true;
}

bool Brain::handle_threats(Ctx& ctx, TurnActions& acts)
{
    FuelSpike spike{ "threats", 40000 };
    last_sim_[0] = last_sim_[1] = -1.0f;
    if (ctx.enemies.empty()) return false;
    // Enemies resting on their own spawn heal fully every round and cannot be chased there:
    // they matter only when they can already reach us.
    const auto& their_spawn_tiles = cambc::map::spawn_region(cambc::team::enemy);
    int nearest = 1 << 20;
    for (auto& e : ctx.enemies)
    {
        const int d = cheb(ctx.pos, e.pos);
        const int reach = e.range <= 1 ? 2 : 4;
        if (their_spawn_tiles.is_set(e.pos) && d > reach) continue;
        nearest = std::min(nearest, d);
    }
    if (nearest > 7) return false;

    // Model the local fight: us and allies within 6 tiles against enemies within 8.
    // Allies' live strength is not read (costly); their design rate scales with our growth.
    model::Fighter f[16];
    float raw_hp[16]{};  // health without the spawn bonus (what we have once we step off)
    const ChampView* enemy_of[16]{};
    cambc::champion_type type_of[16]{};
    // Where each fighter stands, how far it reaches this turn and how many rounds old the
    // sighting is (remembered enemies may have closed in since): arrival rounds below.
    vec2 pos_of[16]{};
    int reach_of[16]{};
    int age_of[16]{};
    auto walk_reach = [](int range) { return range <= 1 ? 2 : 4; };
    int n = 0;
    auto value = [](std::uint32_t gems) { return 1.0f + static_cast<float>(gems) / 40.0f; };
    const float g = growth(ctx);
    // Spawn tiles restore all health every round: a fighter there must be burst down in a
    // single round, so its effective health is much larger.
    const auto& our_spawn = cambc::map::spawn_region(cambc::team::ally);
    const auto& their_spawn = cambc::map::spawn_region(cambc::team::enemy);
    auto effective = [](float hp, float max_hp, bool on_spawn) { return on_spawn ? hp + 3.0f * max_hp : hp; };
    // Damage: basic attacks every round plus ability casts (front-loaded: most fights are
    // shorter than a cooldown). Our own readiness is known; others are assumed ready.
    auto with_profile = [](model::Fighter& fi, const model::PvpProfile& p, float scale) {
        fi.dps = p.basic * scale;
        fi.cooldown = p.cooldown;
        for (int k = 0; k < 2; ++k) fi.cast[k] = p.cast[k] * scale;
    };
    raw_hp[n] = ctx.hp;
    f[n] = { effective(ctx.hp, ctx.max_hp, ctx.on_spawn), 0.0f, true, value(ctx.gems) };
    with_profile(f[n], pvp_profile_of(kit().type, ctx.strength, ctx.focus), 1.0f);
    {
        const auto abilities = cambc::self().abilities();
        for (int k = 0; k < 2; ++k) f[n].ready[k] = 1 + std::max(0, static_cast<int>(abilities[static_cast<std::size_t>(k)].cooldown()));
        // Shields absorb damage before health: an active one counts as health, and a shield
        // ability ready now is cast as the fight starts.
        if (cfg::sim_shields)
        {
            float extra = shield_now(cambc::self());
            for (int k = 0; k < 2; ++k)
                if (kit().slot[k].kind == model::AbilityKind::shield && f[n].ready[k] == 1) extra += kit().slot[k].modifier * ctx.focus;
            f[n].hp += extra;
            raw_hp[n] += extra;
        }
    }
    pos_of[n] = ctx.pos;
    reach_of[n] = walk_reach(kit().attack_range);
    type_of[n++] = kit().type;
    for (auto& a : ctx.allies)
    {
        if (cheb(a.pos, ctx.pos) > 6 || n >= 16) continue;
        raw_hp[n] = a.hp;
        f[n] = { effective(a.hp, a.max_hp, our_spawn.is_set(a.pos)), 0.0f, true, 1.0f };
        with_profile(f[n], pvp_profile_of(a.type, 0.0f, 0.0f), g);
        if (cfg::live_cooldowns)
            for (int k = 0; k < 2; ++k)
                if (a.cooldown[k] >= 0) f[n].ready[k] = 1 + a.cooldown[k];
        if (cfg::sim_shields)
        {
            // Unknown readiness (-1) counts as not ready for allies.
            const auto sa = shield_ability_of(a.type);
            const float extra = shield_now(*a.champ) + (sa.slot >= 0 && a.cooldown[sa.slot] == 0 ? sa.modifier * sa.design_focus * g : 0.0f);
            f[n].hp += extra;
            raw_hp[n] += extra;
        }
        pos_of[n] = a.pos;
        reach_of[n] = walk_reach(a.range);
        type_of[n++] = a.type;
    }
    const int n_ours = n;
    for (auto& e : ctx.enemies)
    {
        if (cheb(e.pos, ctx.pos) > 8 || n >= 16) continue;
        f[n] = { effective(e.hp, e.max_hp, their_spawn.is_set(e.pos)), 0.0f, false, value(e.gems) };
        with_profile(f[n], pvp_profile_of(e.type, e.strength, e.focus), 1.0f);
        if (cfg::live_cooldowns)
            for (int k = 0; k < 2; ++k)
                if (e.cooldown[k] >= 0) f[n].ready[k] = 1 + e.cooldown[k];
        if (cfg::sim_shields)
        {
            // Unknown readiness (-1) counts as ready for enemies.
            const auto sa = shield_ability_of(e.type);
            const float extra = shield_now(*e.champ) + (sa.slot >= 0 && e.cooldown[sa.slot] <= 0 ? sa.modifier * e.focus : 0.0f);
            f[n].hp += extra;
        }
        enemy_of[n] = &e;
        pos_of[n] = e.pos;
        reach_of[n] = enemy_reach(e);
        type_of[n++] = e.type;
    }
    // Enemies seen in the last few rounds but hidden now (bushes, edge of vision) that could have
    // reached us still join the fight; they are never chosen as focus targets.
    for (const auto& m : enemy_memory_)
    {
        const int age = ctx.round - m.round;
        if (age <= 0 || age > cfg::enemy_memory_rounds || n >= 16 || m.hp <= 0.0f) continue;
        if (cheb(m.pos, ctx.pos) > 6 + age || their_spawn.is_set(m.pos)) continue;
        f[n] = { m.hp, 0.0f, false, value(m.gems) };
        with_profile(f[n], pvp_profile_of(m.type, m.strength, m.focus), 1.0f);
        enemy_of[n] = nullptr;
        pos_of[n] = m.pos;
        reach_of[n] = walk_reach(m.range);
        age_of[n] = age;
        type_of[n++] = m.type;
    }
    // Fighters beyond reach of every opponent join once they have walked the gap (one tile a
    // round; both sightings' ages count as closed already): a lone harasser beside us with its
    // team farming 10 tiles away is a short fight first, not the whole team at once.
    // Each ours-theirs pair once (ours come first): the full n x n scan cost ~2.3K fuel at 4v4.
    if (cfg::sim_arrivals)
    {
        int gap[16];
        for (int i = 0; i < n; ++i) gap[i] = 1 << 20;
        for (int i = 0; i < n_ours; ++i)
            for (int j = n_ours; j < n; ++j)
            {
                const int d = cheb(pos_of[i], pos_of[j]) - age_of[i] - age_of[j];
                gap[i] = std::min(gap[i], d);
                gap[j] = std::min(gap[j], d);
            }
        for (int i = 0; i < n; ++i)
        {
            if (gap[i] >= (1 << 20) || (f[i].ours && !cfg::sim_arrivals_allies)) continue;
            f[i].arrive = 1 + std::max(0, gap[i] - reach_of[i]) / std::max(1, cfg::sim_arrival_divisor);
        }
    }
    // Cassi and Issac die together: link visible pairs, otherwise the absent partner adds value.
    for (int i = 0; i < n; ++i)
    {
        const bool c = type_of[i] == cambc::champion_type::cassi, is = type_of[i] == cambc::champion_type::issac;
        if (!c && !is) continue;
        const auto partner = c ? cambc::champion_type::issac : cambc::champion_type::cassi;
        for (int j = 0; j < n; ++j)
            if (j != i && f[j].ours == f[i].ours && type_of[j] == partner) f[i].linked = j;
        if (f[i].linked < 0) f[i].value += 1.0f;
    }
    const auto o = model::simulate_skirmish(f, n, cfg::skirmish_horizon, cfg::threat_focus);
    last_sim_[0] = o.our_losses;
    last_sim_[1] = o.their_losses;
    // A kill pays the killer 100 gold plus the victim's gems either way, so only a strictly
    // better (gem-weighted) trade is worth fighting; "we win but lose one" is a 1-for-1 trade.
    // A team fight already under way: allies beside us in contact with enemies lose more if we
    // leave (traced: a 4v4 where one champion's even simulation pulled it out and the rest lost).
    bool allies_engaged = false;
    if (cfg::stand_with_engaged_allies)
        for (const auto& a : ctx.allies)
        {
            if (cheb(a.pos, ctx.pos) > 3) continue;
            for (const auto& e : ctx.enemies)
                if (cheb(e.pos, a.pos) <= 2) allies_engaged = true;
        }
    // Well behind on banked gems late in the game, a careful race is lost anyway: take the
    // "win with at most one loss" fights too (their carriers' bags are the way back).
    const bool behind = cfg::comeback_gap > 0 && ctx.round >= cfg::comeback_round &&
                        static_cast<int>(cambc::global_submitted_gems(cambc::team::enemy)) -
                                static_cast<int>(cambc::global_submitted_gems(cambc::team::ally)) >=
                            cfg::comeback_gap;
    // The opposite test: behind because their fights go better than ours - demand a wider margin.
    float margin = cfg::fight_margin;
    if (cfg::behind_caution_gap > 0 && ctx.round >= cfg::comeback_round &&
        static_cast<int>(cambc::global_submitted_gems(cambc::team::enemy)) - static_cast<int>(cambc::global_submitted_gems(cambc::team::ally)) >=
            cfg::behind_caution_gap)
        margin = cfg::behind_fight_margin;
    const bool favourable = cfg::fight_better_trades_only && !behind
                                ? o.their_losses >= o.our_losses + (allies_engaged ? 0.0f : margin)
                                : (o.we_survive && !o.they_survive && o.our_losses <= 1.0f) || o.their_losses >= o.our_losses + 1.0f;
    // About to die within two rounds of the damage that can reach us: leave even a winning fight.
    const bool doomed = ctx.hp <= enemy_threat_at(ctx, ctx.pos) * 2.0f && o.rounds > 2;
    // On our spawn we are safe (enemies cannot enter; we heal fully) but only while we stay:
    // hunters camp the exits (online: 40-53 deaths a game, median 6 tiles from our spawn).
    // Fight only what we can reach from here, and leave only if the fight is good without the
    // spawn bonus; otherwise hold until they go or allies gather.
    if (ctx.on_spawn)
    {
        if (favourable) fight_enemies(ctx, acts, false, o.first_target >= 0 ? enemy_of[o.first_target] : nullptr);  // no chasing
        // Capped: enemies idling near our spawn (00reverse-spiral) held us there all game. The
        // second simulation only decides a hold that can still happen: once the cap has run out
        // it cost every turn ~10K fuel for nothing, and with enemies on the adjacent spawn the
        // farm phase then never had its fuel (v28 candidate: 0 clears in 2000 rounds there).
        const bool may_hold = nearest <= cfg::spawn_hold_radius && (hold_since_ < 0 || ctx.round - hold_since_ <= cfg::spawn_hold_max);
        bool leave_ok = true;
        if (may_hold)
        {
            model::Fighter out[16];
            for (int i = 0; i < n; ++i)
            {
                out[i] = f[i];
                if (f[i].ours) out[i].hp = raw_hp[i];
            }
            const auto o2 = model::simulate_skirmish(out, n, cfg::skirmish_horizon, cfg::threat_focus);
            leave_ok = o2.their_losses >= o2.our_losses + 0.5f;
        }
        if (!leave_ok && nearest <= cfg::spawn_hold_radius)
        {
            if (hold_since_ < 0) hold_since_ = ctx.round;
            if (ctx.round - hold_since_ <= cfg::spawn_hold_max)
            {
                publish(-1, static_cast<int>(SlotStatus::idle));
                return true;
            }
        }
        return false;
    }
    // Doomed, but an enemy in reach dies to one basic attack: take the kill first (it pays 100 gold
    // and its bag; fleeing rarely outruns the damage that dooms us).
    if (cfg::doomed_kill && doomed && cambc::self().can_attack())
        for (auto& e : ctx.enemies)
            if (e.hp > 0.0f && e.hp <= ctx.strength && cheb(e.pos, ctx.pos) <= kit().attack_range)
            {
                fight_enemies(ctx, acts, false, &e);
                if (acts.attacked) break;
            }
    if (favourable && !doomed)
    {
        publish(-1, static_cast<int>(SlotStatus::idle));
        const ChampView* focus = o.first_target >= 0 ? enemy_of[o.first_target] : nullptr;
        // Close in when we are near or an ally within 6 is already in contact (help it).
        bool ally_fighting = false;
        if (cfg::help_engaged_allies)
            for (const auto& a : ctx.allies)
            {
                if (cheb(a.pos, ctx.pos) > 6) continue;
                for (const auto& e : ctx.enemies)
                    if (cheb(e.pos, a.pos) <= 2) ally_fighting = true;
            }
        const bool r = fight_enemies(ctx, acts, nearest <= 5 || ally_fighting, focus);
        return r;
    }

    // Capturing and not predicted to die: stepping off resets the capture (and the enemy may take
    // it) - hold the tile, striking back if it can hit us.
    if (cfg::capture_hold && capturing_ && o.our_losses < 0.5f && !doomed)
    {
        if (enemy_threat_at(ctx, ctx.pos) > 0.0f)
        {
            const ChampView* focus = o.first_target >= 0 ? enemy_of[o.first_target] : nullptr;
            fight_enemies(ctx, acts, false, focus);
        }
        return false;
    }
    // No death predicted on our side (e.g. two full-health champions 1v1, 0-0 within the
    // horizon): retreating only starts a standoff - farming steps in, retreat steps out, both
    // sides lose hundreds of turns (traced on 01elk-arena). Answer in place if it can hit us,
    // otherwise keep farming; the simulation reassesses every turn.
    if (cfg::no_loss_stance && o.our_losses < 0.5f && !doomed)
    {
        if (enemy_threat_at(ctx, ctx.pos) > 0.0f)
        {
            publish(-1, static_cast<int>(SlotStatus::idle));
            const ChampView* focus = o.first_target >= 0 ? enemy_of[o.first_target] : nullptr;
            if (fight_enemies(ctx, acts, false, focus)) return true;
        }
        return false;
    }
    if (nearest <= 5 || enemy_threat_at(ctx, ctx.pos) > 0.0f)
    {
        publish(-1, static_cast<int>(SlotStatus::idle));
        // Vision is not shared: tell healthy allies nearby where the losing fight is.
        if (cfg::sos_radius > 0) Knowledge::get().write_sos(ctx.pos, ctx.round);
        // A standoff: retreating turn after turn without being hit. Walking is not separating us
        // (they follow, or stand between us and home) and the low-health recall never runs while
        // they are near; a recall completes unless they strike (locally 30% of deaths followed
        // turns at unchanged health with an enemy within 4).
        if (cfg::standoff_recall_turns > 0)
            calm_retreats_ = ctx.round - last_retreat_round_ <= 1 && last_hp_ >= 0.0f && ctx.hp >= last_hp_ - 0.5f ? calm_retreats_ + 1 : 0;
        if (cfg::standoff_recall_turns > 0 && calm_retreats_ >= cfg::standoff_recall_turns && nearest >= 2 &&
            ctx.hp_frac() < cfg::standoff_recall_frac)
            try
            {
                cambc::self().recall();
                ++standoff_recalls_;
                calm_retreats_ = 0;
                return true;
            }
            catch (const std::exception& e)
            {
                MB_LOG("standoff recall failed: %s\n", e.what());
            }
        const bool r = retreat(ctx, acts);
        return r;
    }
    return false;
}

bool Brain::handle_low_health(Ctx& ctx, TurnActions& acts)
{
    if (ctx.on_spawn) return false;
    // Danger: health will not cover the rounds needed to get clear. With an ally beside us,
    // stepping back hands the monsters' aggro over; alone we must walk out of their range.
    const float incoming = incoming_camp_rate(ctx, ctx.pos) + enemy_threat_at(ctx, ctx.pos);
    bool ally_near = false;
    for (const auto& a : ctx.allies)
        if (cheb(a.pos, ctx.pos) <= 3) ally_near = true;
    const int escape = ally_near ? 1 : std::max(1, escape_rounds(ctx));
    const bool in_danger = incoming > 0.0f && ctx.hp < incoming * static_cast<float>(escape + 1);
    if (!in_danger && ctx.hp_frac() >= cfg::recall_health_frac) return false;
    // Finish a nearly dead camp fight rather than throwing the progress away.
    if (!in_danger && target_ >= 0)
    {
        const auto mons = live_monsters(ctx, target_);
        if (!mons.empty())
        {
            const auto f = model::forecast_camp_fight(mons, my_dps(ctx, World::get().groups[static_cast<std::size_t>(target_)].neighbours));
            if (f.damage * ctx.camp_damage_taken_mult < ctx.hp * 0.6f && ctx.hp_frac() > cfg::disengage_health_frac * 0.5f)
                return false;
        }
    }
    const bool threatened = camp_threat_at(ctx, ctx.pos) > 0.0f || enemy_threat_at(ctx, ctx.pos) > 0.0f || ctx.hit_unseen;
    publish(-1, static_cast<int>(SlotStatus::idle));
    capturing_ = false;
    engaged_ = false;
    if (!threatened)
    {
        try
        {
            cambc::self().recall();
            return true;
        }
        catch (const std::exception& e)
        {
            MB_LOG("recall failed: %s\n", e.what());
        }
    }
    return retreat(ctx, acts);
}

bool Brain::handle_tribute(Ctx& ctx, TurnActions& acts)
{
    if (ctx.gems == 0 || capturing_) return false;
    auto& world = World::get();
    if (world.altars.empty()) return false;
    const auto* field = world.altar_field(true);
    const int da = World::distance(field, ctx.pos, nearest_altar(ctx.pos));
    // No altar can be reached on foot (01boss-battle: the only altar is walled in by holes and a
    // monolith whose tile opens once it dies): keep farming, except when standing within submit
    // range already (on the captured monolith tile), where the static field cannot see us.
    bool gated_reach = false;
    bool via_gate = false;
    int da_used = da;
    if (field && da >= (1 << 14))
    {
        for (auto altar : world.altars)
            if (cheb(ctx.pos, altar) <= cambc::constants::submit_gems_range) gated_reach = true;
        // The gate group is down: walk onto its tile beside the altar.
        if (!gated_reach && world.gate_group >= 0 &&
            Knowledge::get().groups[static_cast<std::size_t>(world.gate_group)].status != GroupStatus::alive)
            if (const auto* gate = world.gate_field(true))
            {
                const int dg = World::distance(gate, ctx.route_pos, world.gate_tiles.front());
                if (dg < (1 << 14))
                {
                    via_gate = true;
                    field = gate;
                    da_used = dg;
                }
            }
        if (!gated_reach && !via_gate) return false;
    }
    const int rounds_left = static_cast<int>(cambc::constants::max_rounds) - ctx.round;
    const auto banked = cambc::global_submitted_gems(cambc::team::ally);
    const int channel = std::max(1, static_cast<int>(std::ceil(ctx.gems * cambc::constants::submit_gem_duration_multiplier)));
    const bool win_now = banked + ctx.gems >= cambc::constants::winning_gems;
    // Hunters about (an enemy seen nearby lately): a carried bag is theirs if they catch us, so
    // bank smaller bags (cfg::tribute_gems_danger; 0 keeps the one threshold).
    std::uint32_t bag = cfg::tribute_gems;
    if (cfg::tribute_gems_danger > 0)
        for (const auto& m : enemy_memory_)
            if (ctx.round - m.round <= cfg::danger_rounds && cheb(m.pos, ctx.pos) <= cfg::danger_radius)
                bag = std::min(bag, static_cast<std::uint32_t>(cfg::tribute_gems_danger));
    const bool trigger = gated_reach || via_gate || ctx.gems >= bag || da_used <= cfg::tribute_detour_tiles ||
                         rounds_left <= da_used + channel + 15 || win_now;
    if (!trigger) return false;

    publish(-1, static_cast<int>(SlotStatus::idle));
    auto& me = cambc::self();
    const int reach = cambc::constants::submit_gems_range;
    for (auto altar : world.altars)
    {
        if (cheb(ctx.pos, altar) > reach) continue;
        bool enemy_lock = false;
        try
        {
            if (cambc::vision::has_altar_lock(altar))
            {
                const auto lock = cambc::vision::altar_lock_at(altar);
                enemy_lock = lock.is_enemy();
                if (enemy_lock && me.can_attack() && cheb(ctx.pos, altar) <= kit().attack_range)
                {
                    me.attack(lock);
                    acts.attacked = true;
                    return true;
                }
            }
        }
        catch (const std::exception& e)
        {
            MB_LOG("altar lock check failed: %s\n", e.what());
        }
        if (enemy_lock) continue;
        // Our own lock first (a free action): an enemy lock can no longer cancel the channel,
        // and their tributes here must break ours first.
        if (cfg::lock_mode >= 2) place_lock(ctx, altar);
        try
        {
            me.submit_gems(altar);
            tribute_end_round_ = ctx.round + channel;
            return true;
        }
        catch (const std::exception& e)
        {
            MB_LOG("submit failed: %s\n", e.what());
        }
    }
    // Recall as transport to the altar (gems are kept through a recall): from far away the
    // channel plus the walk from spawn beats walking. Only when nothing could interrupt it.
    if (cfg::tribute_recall_margin > 0 && field && !via_gate && !gated_reach && da_used < (1 << 14) && ctx.enemies.empty() &&
        ctx.n_unseen == 0 && !ctx.hit_unseen && camp_threat_at(ctx, ctx.pos) <= 0.0f && !ctx.on_spawn)
    {
        const int from_spawn = World::distance(field, world.ally_spawn_center, nearest_altar(world.ally_spawn_center));
        if (from_spawn < (1 << 14) && da_used > from_spawn + recall_turns() + 1 + cfg::tribute_recall_margin)
            try
            {
                cambc::self().recall();
                ++travel_recalls_;
                return true;
            }
            catch (const std::exception& e)
            {
                MB_LOG("tribute recall failed: %s\n", e.what());
            }
    }
    bool moved = false;
    if (field)
    {
        try_travel_dash(ctx, acts, *field, true);
        moved = step_along(*field);
    }
    if (!moved && !acts.cast) moved = step_greedy(nearest_altar(ctx.pos));
    if (moved) acts.moved = true;
    return true;
}

// Health one camp trip can spend before the recall rule pulls us out (siege planning). With
// cfg::siege_pool_from_recall off: all current health, allies 80% (the pre-v19 planning).
float Brain::trip_pool(const Ctx& ctx)
{
    const float usable = cfg::siege_pool_from_recall ? std::max(0.0f, ctx.hp - cfg::recall_health_frac * ctx.max_hp) : ctx.hp;
    return usable / std::max(0.1f, ctx.camp_damage_taken_mult);
}

float Brain::ally_trip_share() { return cfg::siege_pool_from_recall ? 1.0f - cfg::recall_health_frac : 0.8f; }

// Rounds to reach a group under our own movement policy: the walk, or (when the travel recall
// would fire and nothing in view could interrupt it) the recall channel plus the walk from spawn.
int Brain::effective_travel(const Ctx& ctx, const cambc::distance_field* field, int walk, vec2 center)
{
    if (!cfg::recall_in_target_travel || cfg::travel_recall_margin <= 0 || walk >= (1 << 14) || ctx.on_spawn || !ctx.enemies.empty())
        return walk;
    const int from_spawn = World::distance(field, World::get().ally_spawn_center, center);
    if (from_spawn >= (1 << 14)) return walk;
    const int via_recall = from_spawn + recall_turns() + 1;
    return walk > via_recall + cfg::travel_recall_margin ? via_recall : walk;
}

float Brain::full_trip_pool(const Ctx& ctx)
{
    return (1.0f - cfg::recall_health_frac) * ctx.max_hp / std::max(0.1f, ctx.camp_damage_taken_mult);
}

float Brain::gem_weight(const Ctx& ctx)
{
    // Gold buys items that pay off for the rest of the game; gems only count at the end. Weight
    // gems by game progress: the later of the round clock and the leading team's banked gems.
    const float by_round = static_cast<float>(ctx.round) / static_cast<float>(std::max(1, static_cast<int>(cambc::constants::max_rounds)));
    const auto lead = std::max(cambc::global_submitted_gems(cambc::team::ally), cambc::global_submitted_gems(cambc::team::enemy));
    const float by_gems = static_cast<float>(lead) / static_cast<float>(std::max(1, static_cast<int>(cambc::constants::winning_gems)));
    const float p = std::clamp(std::max(by_round, by_gems), 0.0f, 1.0f);
    // Short gem races: harmless groups alone yield many gems a round, so gold buys little before
    // someone banks the winning total.
    static const bool rich = [] {
        if (cfg::gem_rich_value <= 0.0f) return false;
        float rate = 0.0f;
        for (const auto& g : World::get().groups)
            if (g.harmless && g.respawn > 0) rate += static_cast<float>(g.gems) / static_cast<float>(g.respawn);
        return rate >= cfg::gem_rich_rate;
    }();
    const float early = rich ? std::max(cfg::gem_value, cfg::gem_rich_value) : cfg::gem_value;
    const float late = rich ? std::max(cfg::gem_value_late, cfg::gem_rich_value) : cfg::gem_value_late;
    return early + (late - early) * p;
}

float Brain::score_group(const Ctx& ctx, int group, int* need_out) const
{
    auto& world = World::get();
    const auto& kn = Knowledge::get();
    const auto& info = world.groups[static_cast<std::size_t>(group)];
    const auto& belief = kn.groups[static_cast<std::size_t>(group)];
    // Islands ringed by holes are not walkable (our dashes are kept for fights).
    if (world.walk_reachable(group) == 0) { last_reject_ = 1; return 0.0f; }
    auto* field = world.group_field(group, false);
    const int travel = effective_travel(ctx, field, World::distance(field, ctx.route_pos, info.center), info.center);
    if (travel >= (1 << 14)) { last_reject_ = 2; return 0.0f; }
    int wait = 0;
    if (belief.status == GroupStatus::respawning)
    {
        wait = std::max(0, belief.respawn_round - (ctx.round + travel));
        if (wait > cfg::max_respawn_wait) { last_reject_ = 3; return 0.0f; }
    }
    const int committed = kn.committed(group);
    // Someone else is already capturing it: nothing left for us.
    if (belief.status == GroupStatus::reward && kn.capturers(group) > 0) { last_reject_ = 4; return 0.0f; }

    const float g = growth(ctx);
    float scale = 1.0f;
    if (belief.status == GroupStatus::reward) scale = 0.0f;
    else if (belief.status == GroupStatus::alive && belief.hp_left >= 0.0f && info.total_hp > 0.0f)
        scale = std::min(1.0f, belief.hp_left / info.total_hp);
    const model::FightConstants fc{ info.total_hp, info.weighted_hp, info.total_strength };
    float dps = my_dps(ctx, info.neighbours);
    float pool = trip_pool(ctx);
    const auto& slots = kn.slots();
    const auto& powers = ally_powers();
    // Allies on their way here: committed slots, plus our squad if we lead it.
    // Leading means steering a real squad (two or more); alone we count hypothetical joiners.
    const bool leading = kn.squad_leader(kn.my_squad()) == kn.self_index && kn.squad_size(kn.my_squad()) >= 2;
    int coming = 0;
    for (std::size_t i = 0; i < slots.size(); ++i)
    {
        if (static_cast<int>(i) == kn.self_index || !kn.alive[i]) continue;
        const bool squad_mate = leading && kn.squad_of[i] == kn.my_squad();
        const bool on_it = slots[i].group == group && (slots[i].status == SlotStatus::engaging || slots[i].status == SlotStatus::capturing);
        if (!squad_mate && !on_it) continue;
        dps += powers[i].dps * g;
        pool += powers[i].hp * ally_trip_share() * g;
        ++coming;
    }
    float avg_dps = 0.0f, avg_hp = 0.0f;
    for (auto& p : powers) { avg_dps += p.dps; avg_hp += p.hp; }
    const float team_n = static_cast<float>(std::max<std::size_t>(1, powers.size()));
    avg_dps = avg_dps / team_n * g;
    avg_hp = avg_hp / team_n * g;
    // Extra trips cost a recall, a heal and the walk back from spawn.
    const int overhead = cambc::constants::recall_time + 1 + World::distance(field, world.ally_spawn_center, info.center);
    const float reward = ctx.gem_weight * static_cast<float>(info.gems) + cfg::gold_value * static_cast<float>(info.gold);
    const int capture = cambc::constants::capture_time;
    // Best team size: reward per champion-round, so a big siege competes fairly with
    // champions farming separately. Hypothetical joiners are average allies.
    // Without coordination, plan on at most a few hypothetical joiners (cfg); committed allies count fully.
    const int free_allies = leading ? 0 : std::min(cfg::max_hypothetical_joiners, std::max(0, static_cast<int>(powers.size()) - 1 - committed));
    float rate = 0.0f;
    int best_extra = -1;
    for (int extra = 0; extra <= free_allies; ++extra)
    {
        const float d = dps + avg_dps * static_cast<float>(extra);
        const float p = pool + avg_hp * ally_trip_share() * static_cast<float>(extra);
        const auto f = model::forecast_from_constants(fc, d, scale);
        // With recall-consistent planning, later trips start from full health.
        const auto siege = cfg::siege_pool_from_recall
                               ? model::plan_siege(f, p, p - trip_pool(ctx) + full_trip_pool(ctx), overhead, cfg::camp_safety_margin, cfg::siege_max_trips)
                               : model::plan_siege(f, p, overhead, cfg::camp_safety_margin, cfg::siege_max_trips);
        if (!siege.feasible) continue;
        const int members = 1 + std::max(committed, coming) + extra;
        // Waiting earns nothing and leaves us where we are; travelling moves us towards further
        // groups, so a waiting round costs more than a walking one.
        const int waited = static_cast<int>(static_cast<float>(wait) * cfg::wait_weight);
        const float r = model::group_rate(reward, travel + waited, siege.rounds, capture) / static_cast<float>(members);
        if (r > rate) { rate = r; best_extra = extra; }
    }
    if (best_extra < 0) { last_reject_ = 5; return 0.0f; }  // beyond our whole team now
    const int need = 1 + std::max(committed, coming) + best_extra;
    if (need_out) *need_out = need;
    // Harmless groups need one champion only.
    if (info.harmless && committed - (leading ? coming : 0) > 0) rate *= 0.1f;
    // enemy_round is written only with cfg::track_group_enemies, measured harmful (radius 4/6/9:
    // 142/132/119 vs 166 of 280 - avoiding contested groups gives up the middle).
    if (ctx.round - belief.enemy_round < 20) rate *= 0.5f;
    if (ctx.round < banned_until_[static_cast<std::size_t>(group)]) rate *= 0.05f;
    // Territory: a group nearer their spawn than ours lies on their routes and respawn exits.
    if (cfg::territory_penalty > 0.0f)
    {
        const int d_ours = World::distance(field, world.ally_spawn_center, info.center);
        const int d_theirs = World::distance(field, world.enemy_spawn_center, info.center);
        if (d_ours < (1 << 14) && d_theirs < (1 << 14) && d_ours + d_theirs > 0)
            rate /= 1.0f + cfg::territory_penalty * static_cast<float>(std::max(0, d_ours - d_theirs)) / static_cast<float>(d_ours + d_theirs);
    }
    return rate;
}

int Brain::choose_target(const Ctx& ctx, float* out_score)
{
    FuelSpike spike{ "choose", 25000 };
    auto& world = World::get();
    const std::size_t n = world.groups.size();
    // Cheap upper bound per group (reward over travel plus capture: clearing time and every
    // multiplier only lower the real score), then full evaluation in descending bound order
    // until no remaining bound can beat the best score.
    std::vector<float> bound(n, 0.0f);
    const int capture = cambc::constants::capture_time;
    for (std::size_t g = 0; g < n; ++g)
    {
        const auto& info = world.groups[g];
        const auto* gfield = world.group_field(static_cast<int>(g), false);
        const int travel = effective_travel(ctx, gfield, World::distance(gfield, ctx.route_pos, info.center), info.center);
        if (travel >= (1 << 14)) continue;
        const float reward = ctx.gem_weight * static_cast<float>(info.gems) + cfg::gold_value * static_cast<float>(info.gold);
        bound[g] = reward / static_cast<float>(std::max(1, travel + capture));
    }
    static int last_dump = -1;
    if (cfg::debug_scores_every > 0 && ctx.round / cfg::debug_scores_every != last_dump && Knowledge::get().self_index == cfg::debug_trace_champion &&
        (last_dump = ctx.round / cfg::debug_scores_every) >= 0)
        for (std::size_t g = 0; g < n; ++g)
        {
            int need = 0;
            const float s = score_group(ctx, static_cast<int>(g), &need);
            const auto& info = world.groups[g];
            MB_LOG("S r%d p%d,%d g%zu c%d,%d bound %.2f score %.3f rej %d need %d st%d travel %d\n", ctx.round, ctx.pos.x, ctx.pos.y, g, info.center.x,
                   info.center.y, bound[g], s, last_reject_, need, static_cast<int>(Knowledge::get().groups[g].status),
                   World::distance(world.group_field(static_cast<int>(g), false), ctx.route_pos, info.center));
        }
    int best = -1;
    float best_score = 0.0f;
    int best_need = 1;
    // Each evaluation must leave fuel_score_group for acting: reserve the dearest one seen so far
    // (on 01yellow-brick-road's many-member groups one costs several K).
    std::uint64_t dearest = 0;
    for (int evaluated = 0; evaluated < cfg::max_target_evals; ++evaluated)
    {
        std::size_t pick = n;
        float top = best_score;
        for (std::size_t g = 0; g < n; ++g)
            if (bound[g] > top) { top = bound[g]; pick = g; }
        if (pick == n || !fuel_ok(cfg::fuel_score_group + (cfg::score_reserve_dearest ? dearest : 0))) break;
        bound[pick] = 0.0f;
        int need = 1;
        const auto before = cfg::score_reserve_dearest ? fuel() : 0;
        const float s = score_group(ctx, static_cast<int>(pick), &need);
        if (cfg::score_reserve_dearest) dearest = std::max(dearest, before - std::min(before, fuel()));
        if (s > best_score) { best_score = s; best = static_cast<int>(pick); best_need = need; }
    }
    if (out_score) *out_score = best_score;
    if (best >= 0) target_need_ = best_need;
    return best;
}

bool Brain::try_tp_shortcut(Ctx& ctx, TurnActions& acts, const cambc::distance_field& field)
{
    // Teleport crystal as a terrain shortcut: it ignores walls and holes between us and a free
    // landing within range 3; worth its channel when the walk from there is much shorter.
    auto& world = World::get();
    try
    {
        auto& me = cambc::self();
        const auto& tp = me.consumables()[cambc::consumable_type::tp_crystal];
        if (tp.count() == 0) return false;
        const auto here = field.get(ctx.pos);
        if (here == cambc::distance_field::unreachable) return false;
        vec2 best{ -1, -1 };
        int best_d = static_cast<int>(here);
        for (int dy = -3; dy <= 3; ++dy)
            for (int dx = -3; dx <= 3; ++dx)
            {
                const vec2 t{ ctx.pos.x + dx, ctx.pos.y + dy };
                if (t.x < 0 || t.y < 0 || t.x >= world.width || t.y >= world.height || world.blocked.is_set(t) || world.group_at(t) >= 0) continue;
                // Wards (ours included) and teleport reservations also occupy a landing tile.
                if (cambc::vision::has_champion(t) || cambc::vision::has_camp(t) || cambc::vision::has_ward(t) ||
                    cambc::vision::has_teleport(t) || !cambc::vision::in_vision(t))
                    continue;
                const auto d = field.get(t);
                if (d == cambc::distance_field::unreachable || static_cast<int>(d) >= best_d) continue;
                best_d = static_cast<int>(d);
                best = t;
            }
        if (best.x < 0 || static_cast<int>(here) - best_d - (cambc::constants::tp_crystal_time + 1) < cfg::tp_shortcut_margin) return false;
        if (tp.use(best))
        {
            acts.cast = true;
            ++tp_jumps_;
            return true;
        }
    }
    catch (const std::exception& e)
    {
        if (++tp_errors_ <= 3) MB_LOG("crystal failed: %s\n", e.what());
    }
    return false;
}

// Altar locks (cfg::lock_mode; 10-gold consumable, one carried, bought on spawn): placing one
// on an altar in reach is a free action. It blocks the other team's tributes there until
// broken by basic attacks and cancels their channels at it; our team still tributes there.
bool Brain::place_lock(const Ctx& ctx, vec2 altar)
{
    if (cheb(ctx.pos, altar) > lock_range_) return false;
    try
    {
        // A hidden altar may carry our own lock, which cannot be replaced (engine error).
        if (!cambc::vision::in_vision(altar) || cambc::vision::has_altar_lock(altar)) return false;
        const auto& lock = cambc::self().consumables()[cambc::consumable_type::altar_lock];
        if (lock.count() == 0 || !lock.use(altar)) return false;
        ++locks_placed_;
        return true;
    }
    catch (const cambc::error& e)
    {
        if (e.code() == cambc::error_code::invalid_action_altar_lock_out_of_range) lock_range_ = cheb(ctx.pos, altar) - 1;
        if (++lock_errors_ <= 3) MB_LOG("lock failed: %s\n", e.what());
    }
    catch (const std::exception& e)
    {
        if (++lock_errors_ <= 3) MB_LOG("lock failed: %s\n", e.what());
    }
    return false;
}

// A visible enemy tributing beside an altar we can reach: lock it (cancels the channel).
void Brain::lock_enemy_tribute(const Ctx& ctx)
{
    const auto& world = World::get();
    for (const auto& e : ctx.enemies)
    {
        if (!e.channeling || e.gems == 0) continue;
        try
        {
            if (!e.champ->state().contains(cambc::champion_state::submitting_gems)) continue;
        }
        catch (const std::exception&)
        {
            continue;
        }
        for (auto altar : world.altars)
            if (cheb(e.pos, altar) <= cambc::constants::submit_gems_range && place_lock(ctx, altar)) return;
    }
    // Mode 3: any unlocked altar in reach gets our lock (their tributes there must break it first).
    if (cfg::lock_mode >= 3)
        for (auto altar : world.altars)
            if (place_lock(ctx, altar)) return;
}

bool Brain::travel_to_group(Ctx& ctx, TurnActions& acts, int group)
{
    auto& world = World::get();
    bool moved = false;
    // A new route field mid-turn needs extra margin beyond its own estimate.
    const bool can_compute = fuel_ok(world.field_cost() + 10000) && fuel_ok(cfg::fuel_light_farm);
    const auto* field = world.group_field(group, can_compute);
    if (!field) field = world.route_field(group, ctx.route_pos);
    if (field && cfg::tp_shortcut_margin > 0 && ctx.enemies.empty() && ctx.n_unseen == 0 && !ctx.hit_unseen &&
        camp_threat_at(ctx, ctx.pos) <= 0.0f && try_tp_shortcut(ctx, acts, *field))
        return true;
    if (field)
    {
        // Damaging dashes are saved for the fight unless the target is still far away.
        try_travel_dash(ctx, acts, *field, field->get(ctx.pos) > 12);
        moved = step_along(*field) || acts.cast;
        // Congested route: sidestep along equal distance to get around the blocker.
        if (!moved) moved = step_along(*field, true);
    }
    if (!moved) moved = step_greedy(world.groups[static_cast<std::size_t>(group)].center);
    if (moved)
    {
        acts.moved = true;
        ctx.refresh_self();
    }
    return moved;
}

void Brain::maybe_place_ward(const Ctx& ctx)
{
    // Vision is per champion (plus its own ward), and group beliefs only update from tiles in
    // view: a ward left at the camp we capture keeps its respawn and any contest in sight after
    // we leave (mode 1); mode 2 drops a ward wherever we stand (a trail of recent ground).
    auto& world = World::get();
    try
    {
        auto& me = cambc::self();
        // A recall or tribute started this turn is a channel: no other action until it ends.
        const auto st = me.state();
        if (st.contains(cambc::champion_state::recalling) || st.contains(cambc::champion_state::submitting_gems) ||
            st.contains(cambc::champion_state::teleporting))
            return;
        if (me.ward_cooldown() > 0) return;
        vec2 aim = ctx.pos;
        if (cfg::ward_mode == 1)
        {
            if (!capturing_ || target_ < 0) return;
            aim = world.groups[static_cast<std::size_t>(target_)].center;
        }
        if (cfg::ward_mode == 3 && target_ >= 0) aim = world.groups[static_cast<std::size_t>(target_)].center;  // look ahead
        const auto& bush = cambc::map::bushes();
        // Mode 6: place where the ward uncovers the most tiles we cannot see now (walls and bushes
        // block our line of sight), bushes favoured; falls back to mode 4 when fuel is short.
        const int mode = cfg::ward_mode == 6 && !fuel_ok(cfg::fuel_ward_scan) ? 4 : cfg::ward_mode;
        cambc::bitboard unseen;
        if (mode == 6) unseen = ~ctx.visible & ~world.blocked;
        int scans = 0;
        const auto legal = me.ward_placement_tiles();
        const int reach = static_cast<int>(cambc::constants::ward_placement_radius);
        vec2 options[3];
        int dists[3];
        int n = 0;
        for (int dy = -reach; dy <= reach; ++dy)
            for (int dx = -reach; dx <= reach; ++dx)
            {
                const vec2 t{ ctx.pos.x + dx, ctx.pos.y + dy };
                if (t.x < 0 || t.y < 0 || t.x >= world.width || t.y >= world.height || !legal.is_set(t) || world.blocked.is_set(t)) continue;
                if (cambc::vision::has_ward(t) || cambc::vision::has_champion(t) || cambc::vision::has_camp(t)) continue;
                if (mode == 5 && !bush.is_set(t)) continue;  // mode 5: bushes only (keep the old ward)
                int d = cheb(t, aim) + (mode == 4 && !bush.is_set(t) ? 100 : 0);  // mode 4: bushes first
                if (mode == 6)
                {
                    if (!bush.is_set(t) && cheb(t, ctx.pos) < reach) continue;  // the outer ring and bushes
                    if (scans++ >= 10) continue;
                    cambc::bitboard disc;
                    disc.draw_chebyshev(t, cambc::constants::ward_vision_radius);
                    d = -static_cast<int>((disc & unseen).count()) - (bush.is_set(t) ? 15 : 0);
                }
                // keep the 3 nearest to the aim
                int i = n < 3 ? n++ : 3;
                if (i == 3)
                {
                    int worst = 0;
                    for (int k = 1; k < 3; ++k)
                        if (dists[k] > dists[worst]) worst = k;
                    if (d >= dists[worst]) continue;
                    i = worst;
                }
                options[i] = t;
                dists[i] = d;
            }
        for (int pass = 0; pass < n; ++pass)
        {
            int best = 0;
            for (int k = 1; k < n; ++k)
                if (dists[k] < dists[best]) best = k;
            const vec2 t = options[best];
            dists[best] = 1 << 20;
            if (me.place_ward(t))
            {
                ++wards_placed_;
                return;
            }
        }
    }
    catch (const std::exception& e)
    {
        if (++ward_errors_ <= 3) MB_LOG("ward failed: %s\n", e.what());
    }
}

bool Brain::go_capture(Ctx& ctx, TurnActions& acts, int group)
{
    auto& world = World::get();
    const auto& info = world.groups[static_cast<std::size_t>(group)];
    for (auto t : info.tiles)
        if (t == ctx.pos)
        {
            capturing_ = true;
            publish(group, static_cast<int>(SlotStatus::capturing));
            return true;
        }
    // Any free member tile? (allies already on tiles share the reward but add no speed)
    const auto occupied = cambc::vision::champion_tiles();
    bool free_tile = false;
    for (auto t : info.tiles)
        if (!occupied.is_set(t)) free_tile = true;
    if (!free_tile) return false;
    publish(group, static_cast<int>(SlotStatus::engaging));
    why_ = "travel";
    bool moved = false;
    if (const auto* field = world.group_field(group, true))
        moved = step_along(*field);
    else
        moved = step_greedy(info.center);
    if (moved)
    {
        acts.moved = true;
        ctx.refresh_self();
    }
    for (auto t : info.tiles)
        if (t == ctx.pos) { capturing_ = true; publish(group, static_cast<int>(SlotStatus::capturing)); }
    // A free member tile we cannot step towards: two of ours stood in each other's only way to
    // their capture tiles for ~1800 rounds (01gold-rush) - give the target up (callers ban it).
    if (moved || capturing_)
        capture_stall_ = 0;
    else if (++capture_stall_ >= cfg::capture_stall_turns)
    {
        capture_stall_ = 0;
        MB_LOG("capture stall: group %d at %d,%d\n", group, ctx.pos.x, ctx.pos.y);
        return false;
    }
    return true;
}

bool Brain::go_stage(Ctx& ctx, TurnActions& acts, int group)
{
    const auto& info = World::get().groups[static_cast<std::size_t>(group)];
    std::vector<const CampView*> living;
    for (auto& c : ctx.camps)
        if (c.group == group) living.push_back(&c);
    auto score = [&](vec2 n) {
        int s = 0;
        int nearest = 1 << 20;
        for (auto* c : living)
        {
            const int d = cheb(n, c->pos);
            nearest = std::min(nearest, d);
            if (c->strength > 0.0f && d <= c->range) s -= 10000;
        }
        if (living.empty()) nearest = cheb(n, info.center);
        const int want = info.max_range + 1;
        return s - 50 * std::abs(nearest - want);
    };
    if (staged_since_ < 0) staged_since_ = ctx.round;
    if (step_best(score, score(ctx.pos))) { acts.moved = true; ctx.refresh_self(); }
    return true;
}

bool Brain::handle_farm(Ctx& ctx, TurnActions& acts)
{
    // Hunt (cfg::hunt_mode): seek an enemy seen lately nearby when healthy; the threat phase still
    // decides whether to fight it (the hunter proxies pair this with reckless fight rules).
    if (cfg::hunt_mode > 0 && !capturing_ && ctx.hp_frac() > cfg::hunt_health && ctx.round > 30)
    {
        const EnemyMemory* prey = nullptr;
        for (const auto& m : enemy_memory_)
            if (ctx.round - m.round <= cfg::hunt_age && m.hp > 0.0f && cheb(m.pos, ctx.pos) <= cfg::hunt_radius &&
                !cambc::map::spawn_region(cambc::team::enemy).is_set(m.pos) && (!prey || m.hp < prey->hp))
                prey = &m;
        // Mode 2: only prey weaker than us (lower health).
        if (prey && (cfg::hunt_mode == 1 || prey->hp < ctx.hp))
        {
            why_ = "travel";
            if (step_greedy(prey->pos)) { acts.moved = true; ctx.refresh_self(); }
            return true;
        }
    }
    // Answer an ally's SOS (a losing fight it reported in the last rounds) when healthy and close.
    if (cfg::sos_radius > 0 && !capturing_ && ctx.hp_frac() >= 0.6f)
    {
        vec2 at{};
        int age = 0;
        if (Knowledge::get().read_sos(at, age, ctx.round) && age <= cfg::sos_age && cheb(at, ctx.pos) <= cfg::sos_radius &&
            cheb(at, ctx.pos) > 2)
        {
            why_ = "sos";
            if (step_greedy(at)) { acts.moved = true; ctx.refresh_self(); }
            return true;
        }
    }
    FuelSpike spike{ "farm", 60000 };
    auto& kn = Knowledge::get();
    auto& world = World::get();

    // Capture in progress: hold the tile until the reward lands.
    if (capturing_ && target_ >= 0)
    {
        const auto& b = kn.groups[static_cast<std::size_t>(target_)];
        if (b.seen_round == ctx.round && b.status == GroupStatus::reward)
        {
            bool on_tile = false;
            for (auto t : world.groups[static_cast<std::size_t>(target_)].tiles)
                if (t == ctx.pos) on_tile = true;
            if (on_tile)
            {
                // A capture lands after five capture phases; one held far longer is contested from a
                // tile of the region we cannot see (01yellow-brick-road: an enemy on the far end of
                // the gold road kept one of ours holding for 943 turns) - give it up for a while.
                if (cfg::capture_patience > 0)
                {
                    capture_hold_turns_ = last_hold_round_ == ctx.round - 1 ? capture_hold_turns_ + 1 : 1;
                    last_hold_round_ = ctx.round;
                }
                if (cfg::capture_patience > 0 && capture_hold_turns_ > cfg::capture_patience)
                {
                    MB_LOG("capture contested: group %d held %d turns\n", target_, capture_hold_turns_);
                    banned_until_[static_cast<std::size_t>(target_)] = ctx.round + 40;
                    capturing_ = false;
                    target_ = -1;
                    capture_hold_turns_ = 0;
                    why_ = "idle-stall";
                    leave_camp_tiles(ctx, acts);
                    return false;
                }
                publish(target_, static_cast<int>(SlotStatus::capturing));
                // Hit anything in reach while waiting (contesting enemies are handled earlier).
                return true;
            }
        }
        capturing_ = false;
        if (b.status == GroupStatus::respawning)
        {
            publish(target_, static_cast<int>(SlotStatus::cleared));
            target_ = -1;
        }
    }

    // A cleared target: one capturer suffices (extra allies only split the reward), so we
    // capture when standing on a member tile or when no ally is capturing yet.
    if (target_ >= 0)
    {
        const auto& b = kn.groups[static_cast<std::size_t>(target_)];
        if (b.seen_round == ctx.round && b.status == GroupStatus::reward)
        {
            bool on_tile = false;
            for (auto t : world.groups[static_cast<std::size_t>(target_)].tiles)
                if (t == ctx.pos) on_tile = true;
            if (on_tile || kn.capturers(target_) == 0)
            {
                if (go_capture(ctx, acts, target_)) return true;
            }
            banned_until_[static_cast<std::size_t>(target_)] = ctx.round + 8;
            target_ = -1;
        }
    }

    // Drop targets that are no longer worth the trip.
    if (target_ >= 0)
    {
        const auto& b = kn.groups[static_cast<std::size_t>(target_)];
        const int travel = World::distance(world.group_field(target_, false), ctx.pos, world.groups[static_cast<std::size_t>(target_)].center);
        // Same horizon as score_group: worth keeping while the wait after arrival stays short.
        if (b.status == GroupStatus::respawning && b.respawn_round > ctx.round + travel + cfg::max_respawn_wait) target_ = -1;
        // Allies that published this group are on their way (from across the map on 00stampede):
        // keep the staging point longer for them.
        const bool big_prize = target_ >= 0 && world.groups[static_cast<std::size_t>(target_)].gems >= static_cast<std::uint32_t>(cfg::siege_patience_min_gems);
        const int patience = cfg::staging_patience_committed > 0 && target_ >= 0 && kn.committed(target_) > 0 && big_prize
                                 ? cfg::staging_patience_committed
                                 : cfg::staging_patience;
        if (target_ >= 0 && staged_since_ >= 0 && ctx.round - staged_since_ > patience)
        {
            banned_until_[static_cast<std::size_t>(target_)] = ctx.round + 60;
            target_ = -1;
        }
    }

    // Squad followers adopt the leader's published target (fights happen together).
    bool following = false;
    {
        const int leader = kn.squad_leader(kn.my_squad());
        if (leader >= 0 && leader != kn.self_index && !capturing_)
        {
            const auto& ls = kn.slots()[static_cast<std::size_t>(leader)];
            if (ls.group >= 0 && (ls.status == SlotStatus::engaging || ls.status == SlotStatus::capturing) &&
                kn.groups[static_cast<std::size_t>(ls.group)].status != GroupStatus::respawning)
            {
                following = true;
                if (target_ != ls.group)
                {
                    target_ = ls.group;
                    staged_since_ = -1;
                    engaged_ = false;
                    progress_best_ = 1 << 20;
                    progress_round_ = ctx.round;
                    chosen_round_ = ctx.round;
                }
            }
        }
    }
    // Re-scoring groups costs up to ~25K on 30-group maps and acting up to ~40K, so a turn
    // that already spent heavily keeps its target (or waits a turn for a fresh budget).
    const bool can_choose = fuel_ok(cfg::fuel_choose);
    if (!following && target_ < 0 && !can_choose)
    {
        why_ = "idle-fuel"; leave_camp_tiles(ctx, acts);
        return false;
    }
    if (!following && can_choose && (target_ < 0 || ctx.round - chosen_round_ >= 8))
    {
        float score = 0.0f;
        const int pick = choose_target(ctx, &score);
        float current = 0.0f;
        int need = 1;
        if (target_ >= 0) current = score_group(ctx, target_, &need);
        if (target_ < 0 || (pick >= 0 && pick != target_ && score > current * cfg::retarget_ratio))
        {
            if (pick != target_)
            {
                staged_since_ = -1;
                engaged_ = false;
                progress_best_ = 1 << 20;
                progress_round_ = ctx.round;
            }
            if (cfg::debug_targets && pick != target_ && pick >= 0)
            {
                const auto& gi = world.groups[static_cast<std::size_t>(pick)];
                MB_LOG("r%d tgt %d c%d,%d score %.2f need %d st%d (old %d %.2f)\n", ctx.round, pick, gi.center.x, gi.center.y, score, target_need_,
                       static_cast<int>(kn.groups[static_cast<std::size_t>(pick)].status), target_, current);
            }
            target_ = pick;
            target_score_ = score;
        }
        else if (target_ >= 0)
            target_need_ = need;
        chosen_round_ = ctx.round;
    }
    if (target_ < 0)
    {
        publish(-1, static_cast<int>(SlotStatus::idle));
        why_ = "idle-none"; leave_camp_tiles(ctx, acts);
        return false;
    }

    const auto& b = kn.groups[static_cast<std::size_t>(target_)];
    const bool seen = b.seen_round == ctx.round;
    if (seen && b.status == GroupStatus::reward)
    {
        if (!go_capture(ctx, acts, target_))
        {
            banned_until_[static_cast<std::size_t>(target_)] = ctx.round + 10;
            target_ = -1;
        }
        return true;
    }
    if (seen && b.status == GroupStatus::respawning)
    {
        // Respawning soon: wait beside it (never on a member tile, which would block the
        // respawn). Otherwise announce it cleared and pick something else.
        if (b.respawn_round - ctx.round <= cfg::max_respawn_wait)
        {
            publish(target_, static_cast<int>(SlotStatus::engaging));
            why_ = "await";
            leave_camp_tiles(ctx, acts);
            return true;
        }
        publish(target_, static_cast<int>(SlotStatus::cleared));
        target_ = -1;
        why_ = "idle-resp"; leave_camp_tiles(ctx, acts);
        return false;
    }
    publish(target_, static_cast<int>(SlotStatus::engaging));
    const auto mons = live_monsters(ctx, target_);
    if (seen && !mons.empty())
    {
        // Engage when the group is already awake or the force assembled here is enough.
        // The decision latches: re-testing each turn makes champions oscillate as allies move.
        if (!engaged_ && (b.awake || force_sufficient(ctx, target_)))
        {
            engaged_ = true;
            last_hit_round_ = ctx.round;
        }
        if (engaged_)
        {
            staged_since_ = -1;
            progress_round_ = ctx.round;
            // Watchdog: an engagement that lands nothing for long cannot reach its target
            // (walls, holes); the latch otherwise held champions there for 1700 rounds.
            if (ctx.round - last_hit_round_ > cfg::fight_watchdog)
            {
                MB_LOG("fight watchdog: target %d at %d,%d\n", target_, ctx.pos.x, ctx.pos.y);
                banned_until_[static_cast<std::size_t>(target_)] = ctx.round + 60;
                engaged_ = false;
                target_ = -1;
                why_ = "idle-stall";
                return false;
            }
            why_ = "fight";
            const bool fought = fight_camps(ctx, acts, target_, true);
            if (acts.attacked || acts.cast) last_hit_round_ = ctx.round;
            if (fought) return true;
            why_ = "fight-travel"; return travel_to_group(ctx, acts, target_);
        }
        progress_round_ = ctx.round;  // waiting at the staging point is not a stall
        why_ = "stage"; return go_stage(ctx, acts, target_);
    }
    // Travelling: abandon targets we stop approaching (blocked routes, crowding).
    const int d = World::distance(world.group_field(target_, false), ctx.pos, world.groups[static_cast<std::size_t>(target_)].center);
    // Restart tracking after being pushed back (recall, retreat) so old progress is not a stall.
    if (d < progress_best_ || d > progress_best_ + 4) { progress_best_ = d; progress_round_ = ctx.round; }
    if (ctx.round - progress_round_ > 15)
    {
        MB_LOG("stalled on target %d at %d,%d (d=%d)\n", target_, ctx.pos.x, ctx.pos.y, d);
        banned_until_[static_cast<std::size_t>(target_)] = ctx.round + 40;
        target_ = -1;
        why_ = "idle-stall";
        return false;
    }
    // Recall as transport: from far away, the recall channel plus the walk from spawn beats
    // walking (and passes the shop). Only when nothing in view could interrupt the channel.
    // With a full purse (cfg::shop_recall_gold) a small detour is worth it to spend the gold.
    if ((cfg::travel_recall_margin > 0 || cfg::shop_recall_gold > 0) && d < (1 << 14) && ctx.enemies.empty() && ctx.n_unseen == 0 &&
        !ctx.hit_unseen && camp_threat_at(ctx, ctx.pos) <= 0.0f && !ctx.on_spawn)
    {
        const auto& info = world.groups[static_cast<std::size_t>(target_)];
        const int from_spawn = World::distance(world.group_field(target_, false), world.ally_spawn_center, info.center);
        const int extra = from_spawn + recall_turns() + 1 - d;  // rounds lost by recalling instead of walking
        const bool transport = cfg::travel_recall_margin > 0 && extra < -cfg::travel_recall_margin;
        // Early items compound (faster clears for the rest of the game): a smaller purse is worth
        // the trip in the opening (cfg::early_shop_gold until cfg::early_shop_until).
        const int purse = cfg::early_shop_gold > 0 && ctx.round < cfg::early_shop_until ? cfg::early_shop_gold : cfg::shop_recall_gold;
        const bool shopping = cfg::shop_recall_gold > 0 && ctx.gold >= static_cast<std::uint32_t>(purse) &&
                              extra <= cfg::shop_recall_detour;
        if (from_spawn < (1 << 14) && (transport || shopping))
            try
            {
                cambc::self().recall();
                why_ = "travel";
                ++travel_recalls_;
                return true;
            }
            catch (const std::exception& e)
            {
                MB_LOG("travel recall failed: %s\n", e.what());
            }
    }
    // Standoff breaker: walking back towards a target we keep retreating from (an enemy holds
    // the way; farm and retreat alternate every turn) - give that target up for a while.
    if (cfg::standoff_ban_after > 0)
    {
        if (ctx.round - last_retreat_round_ <= 2)
        {
            if (++standoff_turns_ >= cfg::standoff_ban_after)
            {
                banned_until_[static_cast<std::size_t>(target_)] = ctx.round + 40;
                target_ = -1;
                standoff_turns_ = 0;
                why_ = "idle-stall";
                return false;
            }
        }
        else
            standoff_turns_ = 0;
    }
    why_ = "travel";
    if (travel_to_group(ctx, acts, target_)) return true;
    why_ = "idle-blocked";
    travel_blocked_ = true;
    if (++blocked_logs_ <= 6)
    {
        const auto* fld = world.group_field(target_, false);
        MB_LOG("blocked r%d p%d,%d t%d d%d seen%d mons%zu st%d can_move%d moved%d cast%d att%d\n", ctx.round, ctx.pos.x, ctx.pos.y,
               target_, fld ? static_cast<int>(fld->get(ctx.pos)) : -1, seen, mons.size(), static_cast<int>(b.status),
               cambc::self().can_move(), acts.moved, acts.cast, acts.attacked);
    }
    return false;
}

void Brain::remember_threats(Ctx& ctx)
{
    for (const auto& e : ctx.enemies)
        if (e.index >= 0 && e.index < 8) enemy_memory_[e.index] = { ctx.round, e.pos, e.hp, e.max_hp, e.strength, e.focus, e.gems, e.type, e.range };
    // Groups with enemy champions beside them (score_group halves their rate for 20 rounds; the
    // belief was declared but never written before 2026-10-21).
    if (cfg::track_group_enemies && !ctx.enemies.empty())
    {
        auto& kn = Knowledge::get();
        const auto& groups = World::get().groups;
        for (std::size_t g = 0; g < groups.size() && g < kn.groups.size(); ++g)
            for (const auto& e : ctx.enemies)
                if (cheb(e.pos, groups[g].center) <= cfg::enemy_group_radius)
                {
                    kn.groups[g].enemy_round = ctx.round;
                    break;
                }
    }
    // Refresh from awake camps in view.
    for (const auto& c : ctx.camps)
    {
        if (!c.awake || c.strength <= 0.0f) continue;
        int slot = -1;
        for (int i = 0; i < n_camp_memory_; ++i)
            if (camp_memory_[i].pos == c.pos) slot = i;
        if (slot < 0 && n_camp_memory_ < 8) slot = n_camp_memory_++;
        if (slot < 0)
        {
            slot = 0;  // replace the stalest entry
            for (int i = 1; i < n_camp_memory_; ++i)
                if (camp_memory_[i].round < camp_memory_[slot].round) slot = i;
        }
        camp_memory_[slot] = { c.pos, c.range, c.strength, ctx.round };
    }
    // Keep entries that are out of view; a visible tile without an awake camp clears its entry.
    int kept = 0;
    for (int i = 0; i < n_camp_memory_; ++i)
    {
        const auto m = camp_memory_[i];
        bool awake_now = false;
        for (const auto& c : ctx.camps)
            if (c.pos == m.pos && c.awake) awake_now = true;
        const int d = cheb(ctx.pos, m.pos);
        const bool keep = awake_now || (!ctx.visible.is_set(m.pos) && ctx.round - m.round <= 8 && d <= m.range + 3);
        if (!keep) continue;
        camp_memory_[kept++] = m;
        if (!awake_now && ctx.n_unseen < 8) ctx.unseen[ctx.n_unseen++] = { m.pos, m.range, m.strength };
    }
    n_camp_memory_ = kept;
    // Damage with nothing visible in reach: something unseen (a camp or an enemy in a bush).
    if (last_hp_ >= 0.0f && ctx.hp < last_hp_ - 0.5f && !ctx.on_spawn)
    {
        bool explained = ctx.n_unseen > 0;
        for (const auto& c : ctx.camps)
            if (c.awake && c.strength > 0.0f && cheb(c.pos, ctx.pos) <= c.range) explained = true;
        for (const auto& e : ctx.enemies)
            if (cheb(e.pos, ctx.pos) <= 5) explained = true;
        ctx.hit_unseen = !explained;
    }
}

void Brain::note_turn(const Ctx& ctx, const char* mode)
{
    TurnNote& n = notes_[note_count_ % kNotes];
    n = { ctx.round, static_cast<int>(ctx.hp), mode, why_, static_cast<int>(ctx.enemies.size()), 99, 0, last_sim_[0], last_sim_[1] };
    for (const auto& e : ctx.enemies) n.enemy_dist = std::min(n.enemy_dist, cheb(e.pos, ctx.pos));
    for (const auto& a : ctx.allies)
        if (cheb(a.pos, ctx.pos) <= 4) ++n.allies_near;
    ++note_count_;
    death_reported_ = false;
}

void Brain::report_death(int round)
{
    death_reported_ = true;
    char buf[640];
    int len = std::snprintf(buf, sizeof buf, "death r%d k%d", round, static_cast<int>(kit().type));
    const int first = std::max(0, note_count_ - kNotes);
    for (int i = first; i < note_count_ && len < static_cast<int>(sizeof buf) - 48; ++i)
    {
        const TurnNote& n = notes_[i % kNotes];
        len += std::snprintf(buf + len, sizeof buf - static_cast<std::size_t>(len), " %d:%d:%.2s/%.2s:e%dd%da%d:s%.0f-%.0f", n.round - round, n.hp,
                             n.mode, n.why, n.enemies, n.enemy_dist, n.allies_near, n.our_loss, n.their_loss);
    }
    MB_LOG("%s\n", buf);
}

void Brain::leave_camp_tiles(Ctx& ctx, TurnActions& acts)
{
    // An occupied spawn tile delays the whole group's respawn (GameRules "After defeating a
    // camp"), so idle champions never stand on one.
    auto& world = World::get();
    const int group = world.group_at(ctx.pos);
    if (capturing_ || group < 0 || !cambc::self().can_move()) return;
    // Inside a big block every neighbour is a camp tile too: move away from the group's centre
    // so each step makes progress outwards (a one-step test left a champion in the middle of a
    // 5x5 block for 700 rounds, holding its respawn).
    const vec2 center = world.groups[static_cast<std::size_t>(group)].center;
    auto score = [&](vec2 n) { return (world.group_at(n) < 0 ? 1000 : 0) + cheb(n, center); };
    if (step_best(score, score(ctx.pos)))
    {
        acts.moved = true;
        ctx.refresh_self();
    }
}

bool Brain::force_sufficient(const Ctx& ctx, int group) const
{
    // Engage when the force assembled here meets the planned team size, or when one trip
    // with it removes a good share of the group (damage persists between trips).
    auto& world = World::get();
    const auto& info = world.groups[static_cast<std::size_t>(group)];
    const float g = growth(ctx);
    float dps = my_dps(ctx, info.neighbours);
    float pool = trip_pool(ctx);
    int present = 1;
    float living_hp = 0.0f;
    for (const auto& c : ctx.camps)
        if (c.group == group) living_hp += c.hp;
    for (const auto& a : ctx.allies)
    {
        int d = 1 << 20;
        for (const auto& c : ctx.camps)
            if (c.group == group) d = std::min(d, cheb(a.pos, c.pos));
        if (d > info.max_range + 3) continue;
        dps += type_power(a.type).dps * g;
        pool += a.hp * ally_trip_share();
        ++present;
    }
    if (present >= target_need_) return true;
    const auto& belief = Knowledge::get().groups[static_cast<std::size_t>(group)];
    const float hp_left = std::max(living_hp, belief.hp_left);
    const float scale = info.total_hp > 0.0f ? std::min(1.0f, hp_left / info.total_hp) : 0.0f;
    const auto f = model::forecast_from_constants({ info.total_hp, info.weighted_hp, info.total_strength }, dps, scale);
    if (f.damage * cfg::camp_safety_margin <= pool) return true;
    return model::trip_progress(f, dps, pool / cfg::camp_safety_margin, hp_left) >= cfg::engage_progress;
}

} // namespace mb
