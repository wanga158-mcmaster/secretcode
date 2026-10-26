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

struct ProductionRuntime
{
    production::Episode episode;
    int deadline{};
    int logs{};
    production::Trip trip;
    bool front_checked{};
    std::vector<std::uint16_t> spawn_distances, altar_distances;
    std::vector<production::Point> refused_fronts;
    float planned_strength{}, planned_focus{}, planned_max_hp{}, expected_group_hp{ -1.0f };
    bool needs_origin{ true };
    production::Point origin_position{ -1, -1 };
    std::vector<std::uint16_t> origin_distances;
};

// This storage is reached only from the default-off controller. The control Brain retains
// the frozen allocation/layout and does not initialize or update episode bookkeeping.
ProductionRuntime& production_runtime()
{
    static ProductionRuntime state;
    return state;
}

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

// Each group's share of the map's gem production (gems per respawn round), computed once.
const std::vector<float>& gem_shares()
{
    static const std::vector<float> shares = [] {
        const auto& groups = World::get().groups;
        std::vector<float> s(groups.size(), 0.0f);
        float total = 0.0f;
        for (std::size_t i = 0; i < groups.size(); ++i)
            if (groups[i].respawn > 0)
            {
                s[i] = static_cast<float>(groups[i].gems) / static_cast<float>(groups[i].respawn);
                total += s[i];
            }
        for (auto& x : s) x = total > 0.0f ? x / total : 0.0f;
        return s;
    }();
    return shares;
}

// A jackpot holds a large share of the map's gems (01elk_arena and 00stampede: all of them).
bool jackpot(int group)
{
    return cfg::jackpot_share > 0.0f && group >= 0 && gem_shares()[static_cast<std::size_t>(group)] >= cfg::jackpot_share;
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
    if constexpr (cfg::production_service) { service_turn(); return; }
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
        if constexpr (cfg::production_episodes || cfg::production_routes)
        {
            production_runtime().episode.release();
            production_runtime().trip.invalidate();
        }
        if (!death_reported_) report_death(kn.round);
        Ctx ctx;
        ctx.round = kn.round;
        if constexpr (cfg::shop_plan == 100 || cfg::shop_recall_mode > 0) note_shop_signals(ctx, false);
        shop(ctx);  // dead champions may shop
        if constexpr (cfg::shop_plan == 100 || cfg::shop_recall_mode > 0) shop_signals().last_gold = static_cast<int>(me.gold_count());
        target_ = -1;
        capturing_ = false;
        publish(-1, static_cast<int>(SlotStatus::idle));
        return;
    }

    if constexpr (cfg::production_routes)
        if (production_prepare_route()) return;
    Ctx ctx;
    trace.mark("ctx");
    ctx.observe();
    trace.mark("observe");
    if constexpr (cfg::shop_plan == 100 || cfg::shop_recall_mode > 0) note_shop_signals(ctx, true);
    kn.observe_groups(ctx, kit().vision + 1);
    remember_threats(ctx);
    if (cfg::share_sightings) share_sightings(ctx);
    if (cfg::retreat_mode >= 2)
    {
        const auto& their_spawn = cambc::map::spawn_region(cambc::team::enemy);
        for (const auto& e : ctx.enemies)
            if (cheb(e.pos, ctx.pos) <= 8 && !their_spawn.is_set(e.pos)) { last_enemy_round_ = ctx.round; last_enemy_pos_ = e.pos; }
    }
    ctx.gem_weight = gem_weight(ctx);
    if constexpr (cfg::passive_rewards)
    {
        // Rewards are worth what this champion turns them into: Vesper gains 33% more gold, a
        // Treasure emblem 20% more; Marcella gains 2 gold per gem she tributes (online, their Vesper
        // and Marcella explain most of the top teams' gold per damage lead, steals analysis).
        namespace pa = cambc::constants::passive_abilities;
        gold_mult_ = 1.0f + (kit().bonus_gold ? static_cast<float>(pa::vesper_vale_bonus_gold_percent) / 100.0f : 0.0f);
        try
        {
            const auto items = cambc::self().item_count();
            if (items[cambc::item_type::treasure_emblem_of_strength] + items[cambc::item_type::treasure_emblem_of_focus] +
                    items[cambc::item_type::treasure_emblem_of_health] > 0)
                gold_mult_ += 0.2f;
        }
        catch (const std::exception&)
        {
        }
        gem_gold_ = kit().stun_immune ? static_cast<float>(pa::marcella_tribute_gold_per_gem) : 0.0f;
    }
    trace.mark("groups");
    if (!ctx.on_spawn) hold_since_ = -1;  // a new spawn visit gets a fresh hold allowance
    // Our current target's route is worth computing while the turn's budget is fresh. After a
    // blocked travel turn with nothing in view, spend most of this turn on it (the cached field
    // unblocks every later turn; without it greedy steps stall at walls and holes).
    if (!cfg::production_routes && target_ >= 0)
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
    if constexpr (cfg::shop_plan == 100 || cfg::shop_recall_mode > 0) shop_signals().last_gold = static_cast<int>(me.gold_count());
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
    if (!done && cfg::retreat_mode >= 2 && homeward_ && fuel_ok(cfg::fuel_phase_basic) && (done = continue_homeward(ctx, acts))) mode = "home";
    trace.mark("threats");
    if constexpr (cfg::production_episodes || cfg::production_routes)
    {
        if (!done && fuel_ok(cfg::fuel_phase_farm) && (done = handle_production(ctx, acts))) mode = "production";
    }
    else
    {
    if (!done && fuel_ok(cfg::fuel_phase_basic) && (done = handle_low_health(ctx, acts))) mode = "lowhp";
    trace.mark("health");
    if (!done && fuel_ok(cfg::fuel_phase_basic) && (done = handle_tribute(ctx, acts))) mode = "tribute";
    trace.mark("tribute");
    if (!done && fuel_ok(cfg::fuel_phase_farm) && (done = handle_farm(ctx, acts))) mode = capturing_ ? "capture" : "farm";
    }
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
    // Retaliation (Ren's passive) can kill us during our own turn: no turn-end actions then (they throw).
    if (!me.is_alive()) return;
    if (!acts.cast && fuel_ok(cfg::fuel_phase_basic))
        use_support(ctx, acts, camp_threat_at(ctx, ctx.pos) > 0.0f || enemy_threat_at(ctx, ctx.pos) > 0.0f);
    if (cfg::ward_mode > 0 && fuel_ok(cfg::fuel_phase_basic)) maybe_place_ward(ctx);
    trace.mark("support");
    warm_fields(ctx);
    trace.mark("warm");
}

void Brain::warm_fields(const Ctx& ctx)
{
    if constexpr (cfg::production_routes) return;
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
    retreat_streak_ = ctx.round - last_retreat_round_ <= 1 ? retreat_streak_ + 1 : 1;
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
    // Retreat destination (cfg::retreat_mode 1): the nearest tile out of reach is one step ahead of
    // an equal-speed chaser, so a chase never ends (online: 47% of our early deaths on the trap maps,
    // median 24 rounds of contact). Head home along the spawn field unless an enemy within 5 is
    // nearer our spawn than we are (the way home is held); then towards the nearest healthy ally.
    auto spawn_dist = [&](vec2 p) {
        if (spawn)
        {
            const auto d = spawn->get(p);
            if (d != cambc::distance_field::unreachable) return static_cast<int>(d);
        }
        return cheb(p, world.ally_spawn_center);
    };
    int dest = 0;  // 0 local safety scoring, 1 home, 2 ally
    if (cfg::retreat_mode == 1 && chased)
    {
        const int mine = spawn_dist(ctx.pos);
        bool held = false;
        for (const auto& e : ctx.enemies)
            if (cheb(e.pos, ctx.pos) <= 5 && spawn_dist(e.pos) < mine) held = true;
        dest = !held ? 1 : ally_pos.x >= 0 ? 2 : 0;
    }
    // Mode 2 (top-20 analysis: walking home and recalling out of sight would have saved 66% of our
    // small-map deaths; at equal speed a chaser 3+ tiles behind never closes on a straight path, while
    // the nearest tile out of reach keeps us 3-6 tiles from it for ~20 rounds as its team gathers).
    // A single adjacent enemy keeps the local scoring (turning to walk from it dies 27% vs 12% holding).
    // Mode 3: the same commitment only once the local retreat has failed to separate us for
    // cfg::chase_turns consecutive turns (pursuit or a hovering standoff): opponents that do not
    // pursue rarely trigger it, so it costs little farming against them.
    const bool commit_mode = cfg::retreat_mode == 2 || (cfg::retreat_mode == 3 && retreat_streak_ >= cfg::chase_turns);
    if (commit_mode && chased)
    {
        const int mine = spawn_dist(ctx.pos);
        bool held = false;
        int near6 = 0, nearest = 1 << 20;
        for (const auto& e : ctx.enemies)
        {
            const int d = cheb(e.pos, ctx.pos);
            nearest = std::min(nearest, d);
            if (d <= 6) ++near6;
            if (d <= 5 && spawn_dist(e.pos) < mine) held = true;
        }
        const bool single_adjacent = near6 <= 1 && nearest <= 1;
        if (!single_adjacent && !held && (near6 >= 2 || nearest >= 3))
        {
            dest = 1;
            homeward_ = true;
        }
        else if (!single_adjacent && held && ally_pos.x >= 0 && cheb(ally_pos, ctx.pos) <= 8)
            dest = 2;
    }
    auto score = [&](vec2 n) {
        const int home = spawn ? static_cast<int>(spawn->get(n)) : cheb(n, world.ally_spawn_center);
        if (dest == 1) return -spawn_dist(n) * 1000 - static_cast<int>(threat(n) * 10.0f);
        if (dest == 2) return -cheb(n, ally_pos) * 1000 - static_cast<int>(threat(n) * 10.0f) - home;
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

// While committed home (cfg::retreat_mode 2): on spawn the commitment ends; two turns without an
// enemy within 8 end it with a recall (below homeward_recall_frac health, nothing in reach) or a new
// target away from the last sighting; otherwise keep walking the spawn field instead of farming.
bool Brain::continue_homeward(Ctx& ctx, TurnActions& acts)
{
    if (ctx.on_spawn)
    {
        homeward_ = false;
        return false;
    }
    if (ctx.round - last_enemy_round_ >= 2)
    {
        homeward_ = false;
        if (ctx.hp_frac() < cfg::homeward_recall_frac && camp_threat_at(ctx, ctx.pos) <= 0.0f && !ctx.hit_unseen &&
            ctx.n_unseen == 0)
            try
            {
                cambc::self().recall();
                publish(-1, static_cast<int>(SlotStatus::idle));
                capturing_ = false;
                engaged_ = false;
                return true;
            }
            catch (const std::exception& e)
            {
                MB_LOG("homeward recall failed: %s\n", e.what());
            }
        if (last_enemy_pos_.x >= 0)
        {
            const auto& world = World::get();
            for (std::size_t g = 0; g < world.groups.size(); ++g)
                if (cheb(world.groups[g].center, last_enemy_pos_) <= 8) banned_until_[g] = std::max(banned_until_[g], ctx.round + 30);
        }
        target_ = -1;
        return false;
    }
    publish(-1, static_cast<int>(SlotStatus::idle));
    return step_home(ctx, acts);
}

bool Brain::step_home(Ctx& ctx, TurnActions& acts)
{
    auto& world = World::get();
    const auto* spawn = world.spawn_field(false);
    auto spawn_dist = [&](vec2 p) {
        if (spawn)
        {
            const auto d = spawn->get(p);
            if (d != cambc::distance_field::unreachable) return static_cast<int>(d);
        }
        return cheb(p, world.ally_spawn_center);
    };
    // Home first; among equal steps the one farthest from the nearest enemy.
    auto score = [&](vec2 n) {
        int nearest = 1 << 10;
        for (const auto& e : ctx.enemies) nearest = std::min(nearest, cheb(e.pos, n));
        return -spawn_dist(n) * 100 + std::min(nearest, 20);
    };
    if (step_best(score, score(ctx.pos)))
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
        // Allies away from the enemy rarely join (measured: 92% at distance 1 from the nearest enemy,
        // 39% at 3, 24% at 4, 10% at 5); counting them made "favourable" verdicts flip once in contact.
        if (cfg::sim_ally_contact > 0)
        {
            bool contact = false;
            for (const auto& e : ctx.enemies)
                if (cheb(a.pos, e.pos) <= cfg::sim_ally_contact) { contact = true; break; }
            if (!contact) continue;
        }
        raw_hp[n] = a.hp;
        f[n] = { effective(a.hp, a.max_hp, our_spawn.is_set(a.pos)), 0.0f, true, 1.0f };
        with_profile(f[n], pvp_profile_of(a.type, 0.0f, 0.0f), g);
        if (cfg::sim_ally_channels && a.channeling)
        {
            f[n].dps = 0.0f;
            f[n].cast[0] = f[n].cast[1] = 0.0f;
        }
        if (cfg::live_cooldowns)
            for (int k = 0; k < 2; ++k)
                if (a.cooldown[k] >= 0) f[n].ready[k] = 1 + a.cooldown[k];
        if (cfg::sim_shields)
        {
            // Unknown readiness (-1) counts as not ready for allies.
            const auto sa = shield_ability_of(a.type);
            const float extra = shield_now(*a.champ) +
                                (sa.slot >= 0 && a.cooldown[sa.slot] == 0 && !(cfg::sim_ally_channels && a.channeling)
                                     ? sa.modifier * sa.design_focus * g : 0.0f);
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
        if (cheb(m.pos, ctx.pos) > cfg::memory_join_radius + age || their_spawn.is_set(m.pos)) continue;
        f[n] = { m.hp, 0.0f, false, value(m.gems) };
        with_profile(f[n], pvp_profile_of(m.type, m.strength, m.focus), 1.0f);
        enemy_of[n] = nullptr;
        pos_of[n] = m.pos;
        reach_of[n] = walk_reach(m.range);
        age_of[n] = age;
        type_of[n++] = m.type;
    }
    // Awake monsters fight too: each strikes the nearest champion in its range at round start. One
    // that will strike us (we are at least as near as any enemy) fights on their side, unkillable
    // within the horizon and worth nothing (online: a 4-elk camp dealt ~250 a round to us while the
    // verdict read "we lose nobody"; 21% of our early deaths on the trap maps were camp fights beside
    // enemies). Monsters that will strike them are left out (conservative).
    if (cfg::sim_camps)
    {
        int added = 0;
        for (const auto& c : ctx.camps)
        {
            if (n >= 16 || added >= cfg::sim_camp_max) break;
            if (!c.awake || c.hp <= 0.0f || c.strength <= 0.0f) continue;
            const int d_us = cheb(c.pos, ctx.pos);
            if (d_us > c.range + 1) continue;
            bool theirs = false;
            for (const auto& e : ctx.enemies)
                if (cheb(c.pos, e.pos) < d_us) { theirs = true; break; }
            if (theirs) continue;
            f[n] = { 1e6f, c.strength * ctx.camp_damage_taken_mult, false, 0.0f };
            enemy_of[n] = nullptr;
            pos_of[n] = c.pos;
            reach_of[n] = c.range;
            type_of[n++] = cambc::champion_type::ryth;  // any non-Cassi/Issac type (only used for links)
            ++added;
        }
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
    const auto o = [&] {
        if constexpr (cfg::skirmish_memo)
        {
            static model::SkirmishMemo memo;
            return memo.evaluate(f, n, cfg::skirmish_horizon, cfg::threat_focus);
        }
        return model::simulate_skirmish(f, n, cfg::skirmish_horizon, cfg::threat_focus);
    }();
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
            if (cfg::sim_ally_channels && a.channeling) continue;
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
            const auto o2 = [&] {
                if constexpr (cfg::skirmish_memo)
                {
                    static model::SkirmishMemo memo;
                    return memo.evaluate(out, n, cfg::skirmish_horizon, cfg::threat_focus);
                }
                return model::simulate_skirmish(out, n, cfg::skirmish_horizon, cfg::threat_focus);
            }();
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
                if (cfg::sim_ally_channels && a.channeling) continue;
                if (cheb(a.pos, ctx.pos) > 6) continue;
                for (const auto& e : ctx.enemies)
                    if (cheb(e.pos, a.pos) <= 2) ally_fighting = true;
            }
        // Stepping into their reach on a marginal verdict is how one arrival turns a favourable fight
        // into a retreat from contact (28% of early deaths on the trap maps): closing in needs
        // cfg::approach_margin more than fighting where we stand.
        const bool close_in = (nearest <= 5 || ally_fighting) &&
                              (cfg::approach_margin <= 0.0f || o.their_losses >= o.our_losses + margin + cfg::approach_margin);
        const bool r = fight_enemies(ctx, acts, close_in, focus);
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
    const bool reachable = enemy_threat_at(ctx, ctx.pos) > 0.0f;
    // Contact hold: turning away from an adjacent enemy rarely escapes (measured: 27% of retreats from
    // distance 1 die within 15 rounds, 50% below 40% health, vs 4% from distance 4-5) and deals no
    // damage. With an enemy adjacent and a loss that is not lopsided, strike back in place.
    if (cfg::contact_hold > 0.0f && !doomed && o.our_losses - o.their_losses <= cfg::contact_hold)
    {
        int adjacent = 1 << 20;
        for (const auto& e : ctx.enemies)
            if (e.hp > 0.0f && !their_spawn_tiles.is_set(e.pos)) adjacent = std::min(adjacent, cheb(e.pos, ctx.pos));
        if (adjacent <= 1)
        {
            publish(-1, static_cast<int>(SlotStatus::idle));
            const ChampView* focus = o.first_target >= 0 ? enemy_of[o.first_target] : nullptr;
            if (fight_enemies(ctx, acts, false, focus)) return true;
        }
    }
    if (cfg::calm_farm > 0)
    {
        // Leave contested ground instead of hovering at its edge (standoffs cost ~200 champion-rounds
        // a game online): drop a target the enemies stand at, and keep farming elsewhere while none of
        // them can strike us next turn.
        if (cfg::calm_farm >= 2 && target_ >= 0 && !capturing_ && group_contested(ctx, target_))
        {
            banned_until_[static_cast<std::size_t>(target_)] = ctx.round + cfg::contest_ban_rounds;
            target_ = -1;
            engaged_ = false;
        }
        if (!reachable) return false;
    }
    if (nearest <= 5 || reachable)
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
    // With no enemy in sight a recall completes: top teams recall more and earlier, out of sight
    // (17.9 vs our 12.2 recalls by r400; 30% of theirs at >= 70% health), and low with an opponent
    // near they die 19% of the time to our 45% (top-20 analysis).
    float recall_frac = cfg::recall_health_frac;
    if (cfg::safe_recall_frac > recall_frac)
    {
        bool seen = false;
        for (const auto& e : ctx.enemies)
            if (cheb(e.pos, ctx.pos) <= 8) seen = true;
        if (!seen) recall_frac = cfg::safe_recall_frac;
    }
    if (!in_danger && ctx.hp_frac() >= recall_frac) return false;
    // A cleared target whose reward is still on the ground: capture it first (five rounds without
    // camp damage) instead of recalling off it - online, top teams captured groups we had cleared
    // and left (19 of 59 such small-map losses had one of ours on the tile, steals analysis).
    if (cfg::capture_before_recall > 0 && !in_danger && target_ >= 0 && enemy_threat_at(ctx, ctx.pos) <= 0.0f &&
        Knowledge::get().groups[static_cast<std::size_t>(target_)].status == GroupStatus::reward &&
        live_monsters(ctx, target_).empty())
    {
        int near = 1 << 20;
        for (auto tile : World::get().groups[static_cast<std::size_t>(target_)].tiles) near = std::min(near, cheb(tile, ctx.pos));
        if (near <= cfg::capture_before_recall) return false;
    }
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
    if (cfg::prism_probe && kit().type == cambc::champion_type::prism && ctx.round % 50 == 0)
        MB_LOG("prism r%d gems %u focus %.1f\n", ctx.round, static_cast<unsigned>(ctx.gems), ctx.focus);
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
    const bool production_conversion = (cfg::production_episodes || cfg::production_routes) && production_runtime().episode.phase == production::Phase::conversion;
    const bool trigger = production_conversion || gated_reach || via_gate || ctx.gems >= bag || da_used <= cfg::tribute_detour_tiles ||
                         rounds_left <= da_used + channel + 15 || win_now;
    if (!trigger) return false;
    // Prism battery: the bag is Focus; cash it only to win, to beat the enemy's race, or at the end.
    if (cfg::prism_battery && kit().type == cambc::champion_type::prism && !win_now &&
        rounds_left > da_used + channel + 30 &&
        static_cast<int>(cambc::global_submitted_gems(cambc::team::enemy)) < cfg::prism_cash_enemy)
        return false;

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
    float w = early + (late - early) * p;
    if (cfg::gem_value_model)
    {
        // Gold only matters through items. A shard adds `growth` to a main stat of `stat`, speeding the
        // damage-limited share of our income by growth / stat for the rest of the race, which ends when
        // either team has banked the winning total: worth share x gems-left x growth / stat gems per
        // shard. Gems gain weight as items stack and the race shortens (online: losses with 4x the
        // opponent's gold and fewer banked gems).
        const float win = static_cast<float>(cambc::constants::winning_gems);
        const float ours = static_cast<float>(cambc::global_submitted_gems(cambc::team::ally));
        const float theirs = static_cast<float>(cambc::global_submitted_gems(cambc::team::enemy));
        const float left = std::max(50.0f, std::min(win - ours, win - theirs));
        const bool focus = main_stat(kit()) == Stat::focus;
        const float stat = std::max(1.0f, focus ? ctx.focus : ctx.strength);
        const float growth = focus ? kit().focus_growth : kit().strength_growth;
        static const float shard = static_cast<float>(cambc::item_type{ 0 }.cost());
        w = model::item_gem_weight(left, growth, stat, shard, cfg::gold_dps_share, w, cfg::gem_value_max);
    }
    // Losing the race: when they have banked more than us, our gold buys items the game may end
    // before (online: 0-5 vs a gem racer while ahead on gold, clears and kills; they collected 1.5-2x
    // our gems by r400-600). Each 100 gems behind adds race_gem_boost to the weight's multiplier.
    if (cfg::race_gem_boost > 0.0f)
    {
        const float behind = static_cast<float>(cambc::global_submitted_gems(cambc::team::enemy)) -
                             static_cast<float>(cambc::global_submitted_gems(cambc::team::ally));
        if (behind > 0.0f) w = std::min(std::max(w, cfg::gem_value_max), w * (1.0f + cfg::race_gem_boost * behind / 100.0f));
    }
    // Prism battery: every carried gem is also 1.5 Focus for the rest of the game.
    if (cfg::prism_battery && kit().type == cambc::champion_type::prism) w *= cfg::prism_gem_weight;
    return w;
}

bool Brain::blocked_respawn(const Ctx& ctx, int group) const
{
    const auto& belief = Knowledge::get().groups[static_cast<std::size_t>(group)];
    // observe_groups re-queries an expired timer when a member tile is visible.
    // A fresh respawning state says no camps became available this owner turn.
    // An old estimate alone does not establish that the respawn is still blocked.
    return belief.status == GroupStatus::respawning && belief.seen_round == ctx.round &&
           belief.respawn_round <= ctx.round;
}

float Brain::score_group(const Ctx& ctx, int group, int* need_out) const
{
    if constexpr (cfg::respawn_wait_liveness)
        if (blocked_respawn(ctx, group)) { last_reject_ = 11; return 0.0f; }
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
    // Squads (cfg::squad_size) may hold only for the opening (cfg::squad_until_round): first blood
    // decides most games against grouped teams, later solo farming pays.
    const bool squads_on = cfg::squad_until_round <= 0 || ctx.round < cfg::squad_until_round;
    const bool leading = squads_on && kn.squad_leader(kn.my_squad()) == kn.self_index && kn.squad_size(kn.my_squad()) >= 2;
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
    // Scarce concentrated gems are worth more: whoever holds the jackpot holds the race (ecosystem
    // survey: the side with more jackpot captures won 31 of 36 games).
    const float scarcity = jackpot(group) ? 1.0f + cfg::jackpot_weight : 1.0f;
    float reward = ctx.gem_weight * scarcity * static_cast<float>(info.gems) + cfg::gold_value * static_cast<float>(info.gold);
    if constexpr (cfg::passive_rewards)
        reward = (ctx.gem_weight * scarcity + cfg::gold_value * gem_gold_) * static_cast<float>(info.gems) +
                 cfg::gold_value * gold_mult_ * static_cast<float>(info.gold);
    const int capture = cambc::constants::capture_time;
    // Best team size: reward per champion-round, so a big siege competes fairly with
    // champions farming separately. Hypothetical joiners are average allies.
    // Without coordination, plan on at most a few hypothetical joiners (cfg); committed allies count fully.
    const int joiners = jackpot(group) ? cfg::jackpot_joiners : cfg::max_hypothetical_joiners;
    const int free_allies = leading ? 0 : std::min(joiners, std::max(0, static_cast<int>(powers.size()) - 1 - committed));
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
        // The health a fight burns is paid later by a recall trip (recall, heal, walk back): charge its
        // share of that trip (economy analysis: 14 heal recalls a game by r400, ~20% of champion-rounds).
        int hp_rounds = 0;
        if (cfg::hp_cost_weight > 0.0f)
        {
            const float frac = std::min(1.0f, f.damage * ctx.camp_damage_taken_mult /
                                                  (static_cast<float>(members) * std::max(1.0f, ctx.max_hp)));
            hp_rounds = static_cast<int>(cfg::hp_cost_weight * frac * static_cast<float>(overhead));
        }
        const float r = model::group_rate(reward, travel + waited + hp_rounds, siege.rounds, capture) / static_cast<float>(members);
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
    if (cfg::calm_farm >= 2 && group_contested(ctx, group)) rate *= 0.1f;
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
        if constexpr (cfg::respawn_wait_liveness)
            if (blocked_respawn(ctx, static_cast<int>(g))) continue;
        const auto& info = world.groups[g];
        const auto* gfield = world.group_field(static_cast<int>(g), false);
        const int travel = effective_travel(ctx, gfield, World::distance(gfield, ctx.route_pos, info.center), info.center);
        if (travel >= (1 << 14)) continue;
        const float scarcity = jackpot(static_cast<int>(g)) ? 1.0f + cfg::jackpot_weight : 1.0f;
        float reward = ctx.gem_weight * scarcity * static_cast<float>(info.gems) + cfg::gold_value * static_cast<float>(info.gold);
        if constexpr (cfg::passive_rewards)
            reward = (ctx.gem_weight * scarcity + cfg::gold_value * gem_gold_) * static_cast<float>(info.gems) +
                     cfg::gold_value * gold_mult_ * static_cast<float>(info.gold);
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

int Brain::plan_target(const Ctx& ctx, float* out_score)
{
    // This path never executes a farm action in the planning turn. It needs a
    // conservative evaluator allowance plus the unchanged normal turn reserve.
    const std::size_t count = World::get().groups.size();
    if (out_score) *out_score = 0.0f;
    if (count == 0) return -1;
    static std::size_t next{};
    const int limit = std::min(cfg::max_target_evals, static_cast<int>(count));
    for (int evaluated = 0; evaluated < limit; ++evaluated)
    {
        if (!fuel_ok(cfg::fuel_plan_score)) break;
        const int group = static_cast<int>(next % count);
        next = (next + 1) % count;
        int need = 1;
        const float score = score_group(ctx, group, &need);
        if (!(score > 0.0f)) continue;
        target_need_ = need;
        if (out_score) *out_score = score;
        return group;
    }
    // Advancing even on rejected scores prevents the same infeasible prefix
    // from consuming every low-fuel turn. Full-budget choice remains unchanged.
    return -1;
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
            ward_try_ = t;
            if (me.place_ward(t))
            {
                ++wards_placed_;
                return;
            }
        }
    }
    catch (const std::exception& e)
    {
        if (++ward_errors_ <= 3)
            MB_LOG("ward failed r%d p%d,%d at %d,%d: %s\n", ctx.round, ctx.pos.x, ctx.pos.y, ward_try_.x, ward_try_.y, e.what());
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

void Brain::production_log(const Ctx& ctx, const char* event)
{
    if constexpr (cfg::production_trace)
        if (production_runtime().logs++ < 120)
            MB_LOG("EP r%d actor%d g%d %s ph%d hp%.0f bag%u remain%.0f trips%d\n", ctx.round,
                   Knowledge::get().self_index, production_runtime().episode.group, event, static_cast<int>(production_runtime().episode.phase),
                   ctx.hp, ctx.gems, production_runtime().episode.best_hp, production_runtime().episode.recoveries);
}

void Brain::production_release(const Ctx& ctx, const char* reason, int ban)
{
    production_log(ctx, reason);
    if (production_runtime().episode.group >= 0 && ban > 0)
        banned_until_[static_cast<std::size_t>(production_runtime().episode.group)] = ctx.round + ban;
    production_runtime().episode.release();
    if constexpr (cfg::production_routes)
    {
        production_runtime().trip.invalidate();
        production_runtime().refused_fronts.clear();
    }
    target_ = -1;
    capturing_ = engaged_ = false;
    capture_hold_turns_ = capture_stall_ = 0;
    publish(-1, static_cast<int>(SlotStatus::idle));
}

production::Quote Brain::production_quote(const Ctx& ctx, int group) const
{
    auto& world = World::get();
    const auto& info = world.groups[static_cast<std::size_t>(group)];
    const auto& belief = Knowledge::get().groups[static_cast<std::size_t>(group)];
    const auto* field = world.group_field(group, false);
    const int walk = World::distance(field, ctx.route_pos, info.center);
    const int return_walk = World::distance(field, world.ally_spawn_center, info.center);
    if (walk >= (1 << 14) || return_walk >= (1 << 14)) return {};
    if (belief.status == GroupStatus::respawning &&
        (belief.seen_round == ctx.round || belief.respawn_round > ctx.round + walk)) return {};
    const bool reward = belief.seen_round == ctx.round && belief.status == GroupStatus::reward;
    // Remembered damage prices a return, but only fresh observations authorize work below.
    const float scale = !reward && belief.hp_left >= 0.0f && info.total_hp > 0.0f
                            ? std::clamp(belief.hp_left / info.total_hp, 0.0f, 1.0f) : 1.0f;
    auto forecast = reward ? model::FightForecast{} :
        model::forecast_from_constants({ info.total_hp, info.weighted_hp, info.total_strength },
                                       my_dps(ctx, info.neighbours), scale);
    bool all_seen = true;
    for (auto tile : info.tiles) if (!ctx.visible.is_set(tile)) { all_seen = false; break; }
    if (!reward && all_seen && belief.seen_round == ctx.round)
        forecast = model::forecast_camp_fight(live_monsters(ctx, group), my_dps(ctx, info.neighbours));
    const int exit_steps = info.harmless ? 0 : info.max_range + 1;
    const float reserve = forecast.peak_rate * ctx.camp_damage_taken_mult * static_cast<float>(exit_steps + 1);
    float value = ctx.gem_weight * static_cast<float>(info.gems) + cfg::gold_value * static_cast<float>(info.gold);
    if constexpr (cfg::passive_rewards)
        value = (ctx.gem_weight + cfg::gold_value * gem_gold_) * static_cast<float>(info.gems) +
                cfg::gold_value * gold_mult_ * static_cast<float>(info.gold);
    int bank_walk = info.gems == 0 ? 0 : 1 << 20;
    if (info.gems > 0)
    {
        const auto* altar = world.altar_field(false);
        // Camp tiles are walls in the static altar field. The completed region opens a step
        // to one of its neighbors; charge that step instead of treating the camp center as free.
        for (auto tile : info.tiles)
            for (auto direction : kDirs)
            {
                const auto neighbor = tile + direction;
                if (neighbor.x < 0 || neighbor.y < 0 || neighbor.x >= world.width || neighbor.y >= world.height) continue;
                const int d = World::distance(altar, neighbor, nearest_altar(neighbor));
                bank_walk = std::min(bank_walk, d + 1);
            }
        if (bank_walk >= (1 << 14)) return {};
    }
    // A full retained purse needs actual spending service after delivery, not merely a gold
    // score. Charge the quiet exit/recall/heal trip that the completion caller will perform.
    if (!kit().remote_shop && cfg::shop_recall_gold > 0 &&
        ctx.gold + info.gold >= static_cast<std::uint32_t>(cfg::shop_recall_gold))
        bank_walk += exit_steps + recall_turns() + 1;
    const int bank_ticks = info.gems == 0 ? 0 : std::max(1, static_cast<int>(std::ceil(
        static_cast<float>(ctx.gems + info.gems) * cambc::constants::submit_gem_duration_multiplier)));
    return production::quote(value, walk, return_walk, exit_steps, recall_turns(),
        cambc::constants::capture_time + 1, bank_walk, bank_ticks,
        static_cast<int>(cambc::constants::max_rounds) - ctx.round,
        static_cast<float>(forecast.rounds), forecast.damage, ctx.camp_damage_taken_mult,
        ctx.hp, ctx.max_hp, reserve);
}

int Brain::production_choose(const Ctx& ctx, production::Quote& quote)
{
    if constexpr (cfg::production_routes) return production_route_plan(ctx, quote, false);
    auto& world = World::get();
    const auto& kn = Knowledge::get();
    // Conversion is part of the cost, so compute this shared static route once when affordable.
    if (!world.altars.empty() && fuel_ok(world.field_cost() + cfg::fuel_phase_farm)) world.altar_field(true);
    const int count = static_cast<int>(world.groups.size());
    if (count == 0) return -1;
    static int next{};
    int best = -1;
    for (int scanned = 0; scanned < count && fuel_ok(cfg::fuel_score_group); ++scanned)
    {
        const int group = next;
        next = (next + 1) % count;
        if (ctx.round < banned_until_[static_cast<std::size_t>(group)] || kn.committed(group) > 0 ||
            world.walk_reachable(group) == 0) continue;
        const auto candidate = production_quote(ctx, group);
        if (candidate.feasible && candidate.score > quote.score)
        {
            quote = candidate;
            best = group;
        }
    }
    return best;
}

production::Quote Brain::production_route_quote(const Ctx& ctx, int group, const production::RouteCosts& route) const
{
    if (!route.certified || route.walk < 0 || route.return_walk < 0 || route.bank_walk < 0) return {};
    const auto& world = World::get();
    const auto& info = world.groups[static_cast<std::size_t>(group)];
    const auto& belief = Knowledge::get().groups[static_cast<std::size_t>(group)];
    if (belief.status == GroupStatus::respawning &&
        (belief.seen_round == ctx.round || belief.respawn_round > ctx.round + route.walk)) return {};
    const bool reward = belief.seen_round == ctx.round && belief.status == GroupStatus::reward;
    const float scale = !reward && belief.hp_left >= 0.0f && info.total_hp > 0.0f
        ? std::clamp(belief.hp_left / info.total_hp, 0.0f, 1.0f) : 1.0f;
    auto forecast = reward ? model::FightForecast{} : model::forecast_from_constants(
        {info.total_hp, info.weighted_hp, info.total_strength}, my_dps(ctx, info.neighbours), scale);
    bool all_seen = belief.seen_round == ctx.round;
    for (auto tile : info.tiles) if (!ctx.visible.is_set(tile)) { all_seen = false; break; }
    if (!reward && all_seen) forecast = model::forecast_camp_fight(live_monsters(ctx, group), my_dps(ctx, info.neighbours));
    const int exit_steps = info.harmless ? 0 : info.max_range + 1;
    const float reserve = forecast.peak_rate * ctx.camp_damage_taken_mult * static_cast<float>(exit_steps + 1);
    float value = ctx.gem_weight * static_cast<float>(info.gems) + cfg::gold_value * static_cast<float>(info.gold);
    if constexpr (cfg::passive_rewards)
        value = (ctx.gem_weight + cfg::gold_value * gem_gold_) * static_cast<float>(info.gems) +
            cfg::gold_value * gold_mult_ * static_cast<float>(info.gold);
    int bank_walk = route.bank_walk;
    if (!kit().remote_shop && cfg::shop_recall_gold > 0 &&
        ctx.gold + info.gold >= static_cast<std::uint32_t>(cfg::shop_recall_gold))
        bank_walk += exit_steps + recall_turns() + 1;
    const int bank_ticks = info.gems == 0 ? 0 : std::max(1, static_cast<int>(std::ceil(
        static_cast<float>(ctx.gems + info.gems) * cambc::constants::submit_gem_duration_multiplier)));
    return production::quote(value, route.walk, route.return_walk, exit_steps, recall_turns(),
        cambc::constants::capture_time + 1, bank_walk, bank_ticks,
        static_cast<int>(cambc::constants::max_rounds) - ctx.round,
        static_cast<float>(forecast.rounds), forecast.damage, ctx.camp_damage_taken_mult,
        ctx.hp, ctx.max_hp, reserve);
}

bool Brain::production_prepare_route()
{
    auto& state = production_runtime();
    if (!state.needs_origin) return false;
    auto& world = World::get();
    auto& me = cambc::self();
    const int epoch = static_cast<int>(cambc::round_number());
    const auto position = me.position();
    if (!fuel_ok(world.field_cost_at(position) + 18000)) return false;
    // This phase performs no action. Visible enemies or nearby awake camps defer it
    // to the ordinary fresh observer/threat handler instead of spending an exposed turn.
    for (const auto& enemy : roster(cambc::team::enemy).champs) if (enemy.is_visible()) return false;
    for (auto tile : cambc::vision::awake_camp_tiles().positions())
    {
        const int group = world.group_at(tile), member = world.member_at(tile);
        if (group < 0 || member < 0) continue;
        const auto& info = world.groups[static_cast<std::size_t>(group)];
        const auto& design = world.camp_stats[info.types[static_cast<std::size_t>(member)]];
        if (design.strength > 0.0f && cheb(position, tile) <= design.range + 1) return false;
    }
    cambc::bitboard walls = world.blocked | world.camp_spawns;
    walls.set(position, false);
    cambc::bitboard roots, visited;
    roots.set(position);
    cambc::distance_field origin;
    origin.compute(walls, roots, visited);
    auto distances = origin.export_linear_buffer();
    if (static_cast<int>(cambc::round_number()) != epoch || me.position() != position)
    {
        state.origin_distances.clear();
        return true;
    }
    state.origin_position = {position.x,position.y};
    state.origin_distances = std::move(distances);
    state.needs_origin = false;
    return true;
}

int Brain::production_route_plan(const Ctx& ctx, production::Quote& quote, bool continuation)
{
    auto& state = production_runtime();
    if (static_cast<int>(cambc::round_number()) != ctx.round) { state.trip.invalidate(); return -2; }
    auto& world = World::get();
    const auto& knowledge = Knowledge::get();
    // Service graphs are conservative static routes. Export once, in separate planning
    // turns; an unavailable graph is not a geometric distance substitute.
    if (state.spawn_distances.empty())
    {
        const auto* field = world.spawn_field(true);
        if (field) state.spawn_distances = field->export_linear_buffer();
        return -2;
    }
    if (state.altar_distances.empty())
    {
        const auto* field = world.altar_field(true);
        if (field) state.altar_distances = field->export_linear_buffer();
        return -2;
    }
    const bool exposed = continuation && camp_threat_at(ctx, ctx.pos) > 0.0f;
    if (!exposed && (state.origin_distances.empty() || state.origin_position != production::Point{ctx.pos.x,ctx.pos.y}))
    {
        state.needs_origin = true;
        return -2;
    }
    const std::uint64_t search_reserve = exposed ? 22000 : 14000;
    if (!fuel_ok(search_reserve)) return -2;
    // Unknown camp spawns remain blocked. Only personally visible empty spawns are
    // released; visible units and refused destinations are part of this graph epoch.
    const cambc::bitboard occupied_camps = cambc::vision::camp_tiles();
    const cambc::bitboard camp_walls = world.camp_spawns & ~(ctx.visible & ~occupied_camps);
    cambc::bitboard walls = blocked_now() | camp_walls;
    walls.set(ctx.pos, false);
    std::vector<std::uint16_t> local_distances;
    if (exposed)
    {
        cambc::bitboard roots, visited;
        roots.set(ctx.pos);
        cambc::distance_field origin;
        // During an awake fight only this fresh local opportunity envelope is inspected.
        // Unreached candidates stay unknown; no geometric substitute authorizes travel.
        const auto radius = static_cast<std::uint16_t>(2 * world.groups[static_cast<std::size_t>(state.episode.group)].max_range + kit().attack_range + 2);
        origin.compute(walls, roots, visited, radius);
        local_distances = origin.export_linear_buffer();
    }
    // A cached quiet graph is static terrain plus conservative camp occupancy, not
    // current dynamic world state. Every actual step has a separate fresh authority.
    const auto& distances = exposed ? local_distances : state.origin_distances;
    const auto blocked = walls.export_linear_buffer();
    const auto visible = ctx.visible.export_linear_buffer();
    if (static_cast<int>(cambc::round_number()) != ctx.round) { state.trip.invalidate(); return -2; }
    const auto index = [&](vec2 tile) { return static_cast<std::size_t>(tile.y) * world.width + tile.x; };
    const auto inside = [&](vec2 tile) { return tile.x >= 0 && tile.y >= 0 && tile.x < world.width && tile.y < world.height; };
    std::vector<unsigned char> living(distances.size(), 0);
    for (const auto& camp : ctx.camps) if (camp.hp > 0.0f) living[index(camp.pos)] = 1;
    const auto service_distance = [&](const std::vector<std::uint16_t>& service, vec2 front) {
        int distance = service[index(front)];
        if (distance != cambc::distance_field::unreachable) return distance;
        for (auto direction : kDirs)
        {
            const auto neighbor = front + direction;
            if (!inside(neighbor) || blocked[index(neighbor)]) continue;
            const int d = service[index(neighbor)];
            if (d != cambc::distance_field::unreachable) distance = std::min(distance, d + 1);
        }
        return distance;
    };
    int best = -1, best_member = -1;
    vec2 best_front{ -1, -1 };
    for (int group = 0; group < static_cast<int>(world.groups.size()) && fuel_ok(14000); ++group)
    {
        if (ctx.round < banned_until_[static_cast<std::size_t>(group)] ||
            (group != state.episode.group && knowledge.committed(group) > 0)) continue;
        const auto& info = world.groups[static_cast<std::size_t>(group)];
        const auto& belief = knowledge.groups[static_cast<std::size_t>(group)];
        const bool reward = belief.seen_round == ctx.round && belief.status == GroupStatus::reward;
        int member = -1;
        float priority = -1.0f;
        for (int m = 0; m < static_cast<int>(info.tiles.size()); ++m)
        {
            const auto tile = info.tiles[static_cast<std::size_t>(m)];
            if (!reward && visible[index(tile)] && !living[index(tile)]) continue;
            const auto& design = world.camp_stats[info.types[static_cast<std::size_t>(m)]];
            float hp = design.health;
            for (const auto& camp : ctx.camps) if (camp.group == group && camp.member == m) hp = camp.hp;
            if (reward && (blocked[index(tile)] || distances[index(tile)] == cambc::distance_field::unreachable)) continue;
            const float value = reward ? 1.0f / (1.0f + distances[index(tile)]) : design.strength / std::max(1.0f, hp);
            if (value > priority) { priority = value; member = m; }
        }
        if (member < 0) continue;
        const auto target = info.tiles[static_cast<std::size_t>(member)];
        vec2 front{ -1, -1 };
        int walk = 1 << 20;
        const int reach = reward ? 0 : kit().attack_range;
        for (int y = std::max(0, target.y - reach); y <= std::min(world.height - 1, target.y + reach); ++y)
            for (int x = std::max(0, target.x - reach); x <= std::min(world.width - 1, target.x + reach); ++x)
            {
                const vec2 tile{ x, y };
                const auto i = index(tile);
                if (blocked[i] || distances[i] == cambc::distance_field::unreachable) continue;
                if (group == state.episode.group && std::find(state.refused_fronts.begin(), state.refused_fronts.end(),
                    production::Point{x,y}) != state.refused_fronts.end()) continue;
                if (distances[i] < walk) { walk = distances[i]; front = tile; }
            }
        if (front.x < 0) continue;
        const int back = service_distance(state.spawn_distances, front);
        int bank = info.gems == 0 ? 0 : service_distance(state.altar_distances, front);
        if (back == cambc::distance_field::unreachable || bank == cambc::distance_field::unreachable) continue;
        // Capture finishes on a member tile, not this attack stance. The connected cleared
        // region adds a conservative local reposition allowance to the static altar route.
        if (info.gems > 0) bank = std::max(0, bank - 1) + reach + std::max(info.max_x-info.min_x, info.max_y-info.min_y);
        const auto candidate = production_route_quote(ctx, group, {walk, back, bank, true});
        if (candidate.feasible && candidate.score > quote.score)
        {
            quote = candidate; best = group; best_member = member; best_front = front;
        }
    }
    if (best < 0) return -1;
    auto path = production::reconstruct_route(world.width, world.height, distances,
        {ctx.pos.x, ctx.pos.y}, {best_front.x, best_front.y});
    if (static_cast<int>(cambc::round_number()) != ctx.round) { state.trip.invalidate(); return -2; }
    if (best != state.episode.group) state.refused_fronts.clear();
    if (!state.trip.install(best, best_member, ctx.round, std::move(path))) return -1;
    state.trip.observe(ctx.round, {ctx.pos.x, ctx.pos.y});
    state.front_checked = state.trip.arrived();
    state.planned_strength = ctx.strength; state.planned_focus = ctx.focus; state.planned_max_hp = ctx.max_hp;
    state.expected_group_hp = knowledge.groups[static_cast<std::size_t>(best)].hp_left;
    if constexpr (cfg::production_trace)
        if (state.logs++ < 120) MB_LOG("TR r%d actor%d g%d m%d front%d,%d steps%zu local%d quote%d\n", ctx.round,
            knowledge.self_index, best, best_member, best_front.x, best_front.y, state.trip.remaining_steps(), exposed, quote.rounds);
    return best;
}

bool Brain::production_route_step(Ctx& ctx, TurnActions& acts)
{
    auto& trip = production_runtime().trip;
    if (static_cast<int>(cambc::round_number()) != ctx.round) { trip.invalidate(); return false; }
    if (!trip.observe(ctx.round, {ctx.pos.x,ctx.pos.y})) return false;
    const auto next = trip.next(ctx.round);
    if (!next) return false;
    const vec2 destination{ next->x, next->y };
    // The stored graph is a route witness only. This owner-turn's legality, unknown
    // blockers and approach exposure must authorize the actual step afresh.
    const auto options = step_options(blocked_now(), ctx.pos);
    if (std::find(options.begin(), options.end(), destination) == options.end()) { trip.invalidate(); return false; }
    const float reserve = production_exit(ctx, nullptr, -1, destination);
    if (reserve < 0.0f || ctx.hp <= reserve) { trip.invalidate(); return false; }
    if (static_cast<int>(cambc::round_number()) != ctx.round) { trip.invalidate(); return false; }
    bool moved = false;
    try { moved = production::apply_absolute_destination(cambc::self(), destination); }
    catch (...) { trip.invalidate(); throw; }
    if (!moved) remember_revealed(destination);
    invalidate_obstacles();
    trip.moved(ctx.round, *next, moved);
    if (moved) { acts.moved = true; ctx.refresh_self(); }
    return moved;
}

float Brain::production_exit(Ctx& ctx, TurnActions* acts, int group, vec2 origin)
{
    // A legal walking path to safety is required before waking/continuing work. Current
    // occupancy is frozen for this owner turn only; every future turn recomputes the path.
    if (!fuel_ok(cfg::fuel_safety_field + 7000)) return -1.0f;
    cambc::bitboard threatened;
    float maximum_rate = 0.0f;
    const vec2 from = origin.x >= 0 ? origin : ctx.pos;
    float collateral_radius = kit().splash ? 1.0f : 0.0f;
    for (const auto& spell : kit().slot)
        if (spell.camps && spell.kind == model::AbilityKind::aoe) collateral_radius = std::max(collateral_radius, spell.radius);
    const auto& world = World::get();
    std::vector<production::WakeMember> members;
    members.reserve(ctx.camps.size());
    for (const auto& camp : ctx.camps) members.push_back({ camp.group, camp.pos.x, camp.pos.y, camp.hp > 0.0f });
    const auto collateral_groups = production::collateral_wake_groups(members, group,
        static_cast<int>(collateral_radius), static_cast<int>(world.groups.size()));
    for (const auto& c : ctx.camps)
    {
        const bool collateral = collateral_groups[static_cast<std::size_t>(c.group)] != 0;
        if (c.hp > 0.0f && c.strength > 0.0f && (c.awake || c.group == group || collateral))
        {
            threatened.draw_chebyshev(c.pos, static_cast<float>(c.range));
            maximum_rate += c.strength;
        }
    }
    for (std::size_t g = 0; g < collateral_groups.size(); ++g)
        if (collateral_groups[g])
            for (std::size_t member = 0; member < world.groups[g].tiles.size(); ++member)
            {
                const auto tile = world.groups[g].tiles[member];
                const auto& design = world.camp_stats[world.groups[g].types[member]];
                if (ctx.visible.is_set(tile) || design.strength <= 0.0f) continue;
                // Unknown members are conservative possible sources, not inferred live state.
                threatened.draw_chebyshev(tile, static_cast<float>(design.range));
                maximum_rate += design.strength;
            }
    for (int i = 0; i < ctx.n_unseen; ++i)
    {
        threatened.draw_chebyshev(ctx.unseen[i].pos, static_cast<float>(ctx.unseen[i].range));
        maximum_rate += ctx.unseen[i].strength;
    }
    if (!threatened.is_set(from)) return 0.0f;
    cambc::bitboard walls = blocked_now();
    walls.set(ctx.pos, false);
    cambc::bitboard visited;
    cambc::distance_field field;
    field.compute(walls, ~(walls | threatened), visited, static_cast<std::uint16_t>(cfg::safety_field_radius));
    const auto distance = field.get(from);
    if (distance == cambc::distance_field::unreachable) return -1.0f;
    if (acts && from == ctx.pos && distance > 0 && cambc::self().can_move() && step_along(field))
    {
        acts->moved = true;
        ctx.refresh_self();
    }
    // One additional next-round volley is reserved before leaving after another work turn.
    return maximum_rate * ctx.camp_damage_taken_mult * static_cast<float>(distance + 1);
}

bool Brain::handle_production(Ctx& ctx, TurnActions& acts)
{
    using production::Phase;
    auto& world = World::get();
    auto& kn = Knowledge::get();
    if (production_runtime().episode.group >= 0 && ctx.round > production_runtime().deadline)
        production_release(ctx, "deadline", 40);
    // Threat/homeward handling may have changed the generic target; our identity is explicit.
    target_ = production_runtime().episode.phase == Phase::conversion ? -1 : production_runtime().episode.group;
    if (production_runtime().episode.phase == Phase::conversion)
    {
        capturing_ = false;
        if (ctx.gems == 0) production_release(ctx, "banked");
        else
        {
            why_ = "convert";
            if (camp_threat_at(ctx, ctx.pos) > 0.0f) return retreat(ctx, acts);
            if (handle_tribute(ctx, acts)) return true;
            production_release(ctx, "altar-unreachable");
        }
    }
    if (production_runtime().episode.group >= 0)
    {
        const auto& info = world.groups[static_cast<std::size_t>(production_runtime().episode.group)];
        const auto& belief = kn.groups[static_cast<std::size_t>(production_runtime().episode.group)];
        if (belief.seen_round == ctx.round && belief.status == GroupStatus::respawning)
        {
            // An actual observed state, rather than an ally intent, closes this generation.
            production_log(ctx, "payout-or-other-owner");
            const int completed = production_runtime().episode.group;
            capturing_ = engaged_ = false;
            target_ = -1;
            publish(completed, static_cast<int>(SlotStatus::cleared));
            if (ctx.gems > 0) production_runtime().episode.phase = Phase::conversion;
            else production_runtime().episode.release();
            leave_camp_tiles(ctx, acts);
            why_ = "release";
            return true;
        }
        if (belief.seen_round == ctx.round && belief.status == GroupStatus::reward &&
            camp_threat_at(ctx, ctx.pos) <= 0.0f)
        {
            if (production_runtime().episode.phase != Phase::capture) production_log(ctx, "reward");
            production_runtime().episode.phase = Phase::capture;
            bool on_tile = false, peer_on_tile = false;
            for (auto tile : info.tiles)
            {
                if (tile == ctx.pos) on_tile = true;
                for (const auto& ally : ctx.allies) if (ally.pos == tile) peer_on_tile = true;
            }
            if (peer_on_tile && !on_tile)
            {
                production_release(ctx, "physical-receiver", cambc::constants::capture_time + 3);
                leave_camp_tiles(ctx, acts);
                return true;
            }
            if (on_tile)
            {
                capture_hold_turns_ = last_hold_round_ == ctx.round - 1 ? capture_hold_turns_ + 1 : 1;
                last_hold_round_ = ctx.round;
                if (cfg::capture_patience > 0 && capture_hold_turns_ > cfg::capture_patience)
                {
                    production_release(ctx, "contested-capture", 40);
                    leave_camp_tiles(ctx, acts);
                    return true;
                }
            }
            why_ = "capture";
            if (!go_capture(ctx, acts, production_runtime().episode.group)) production_release(ctx, "capture-blocked", 10);
            return true;
        }
    }
    if (production_runtime().episode.phase == Phase::recovery)
    {
        capturing_ = engaged_ = false;
        publish(production_runtime().episode.group, static_cast<int>(SlotStatus::engaging));
        if (ctx.on_spawn)
        {
            if (ctx.hp_frac() < 0.999f) { why_ = "recover-heal"; return true; }
            if (production_runtime().episode.group < 0) production_runtime().episode.release();
            else production_runtime().episode.resume(ctx.round);
            if constexpr (cfg::production_routes)
            {
                production_runtime().trip.invalidate();
                production_runtime().refused_fronts.clear();
            }
            production_log(ctx, "resume");
        }
        else
        {
            why_ = "recover";
            if (camp_threat_at(ctx, ctx.pos) > 0.0f || ctx.n_unseen > 0 || ctx.hit_unseen)
            {
                production_exit(ctx, &acts, -1);
                if (!acts.moved && fuel_ok(cfg::fuel_phase_basic)) return retreat(ctx, acts);
                return true;
            }
            if (ctx.enemies.empty())
            {
                cambc::self().recall();
                production_log(ctx, "recall");
                return true;
            }
            return retreat(ctx, acts);
        }
    }
    if (production_runtime().episode.group < 0)
    {
        if (handle_low_health(ctx, acts)) { why_ = "recover-idle"; return true; }
        if (handle_tribute(ctx, acts)) { why_ = "bank-idle"; return true; }
        if (!ctx.on_spawn && !kit().remote_shop && cfg::shop_recall_gold > 0 &&
            ctx.gold >= static_cast<std::uint32_t>(cfg::shop_recall_gold) && ctx.enemies.empty() &&
            ctx.n_unseen == 0 && !ctx.hit_unseen && camp_threat_at(ctx, ctx.pos) <= 0.0f)
        {
            production_runtime().episode.phase = Phase::recovery;
            production_runtime().episode.recovery_round = ctx.round;
            cambc::self().recall();
            production_log(ctx, "spend-service");
            why_ = "recover-spend";
            return true;
        }
        production::Quote quote;
        const int group = production_choose(ctx, quote);
        if (group == -2) { why_ = "plan-routes"; return true; }
        if (group < 0)
        {
            why_ = "plan";
            publish(-1, static_cast<int>(SlotStatus::idle));
            leave_camp_tiles(ctx, acts);
            return true;
        }
        const auto& belief = kn.groups[static_cast<std::size_t>(group)];
        production_runtime().episode.begin(group, ctx.round, belief.hp_left, ctx.gems);
        production_runtime().deadline = std::min(static_cast<int>(cambc::constants::max_rounds), ctx.round + 2 * quote.rounds);
        target_ = group;
        target_need_ = 1;
        production_log(ctx, "acquire");
        if (!fuel_ok(cfg::fuel_phase_farm)) return true;
    }
    target_ = production_runtime().episode.group;
    publish(target_, static_cast<int>(SlotStatus::engaging));
    if constexpr (cfg::production_routes)
    {
        auto& state = production_runtime();
        const auto& observed = kn.groups[static_cast<std::size_t>(target_)];
        bool refresh = !state.trip.observe(ctx.round, {ctx.pos.x,ctx.pos.y});
        if (state.trip.valid() && state.trip.group != target_) refresh = true;
        if (state.planned_strength != ctx.strength || state.planned_focus != ctx.focus || state.planned_max_hp != ctx.max_hp)
            refresh = true;
        if (state.trip.valid() && state.trip.group == target_)
        {
            const auto member_tile = world.groups[static_cast<std::size_t>(target_)].tiles[static_cast<std::size_t>(state.trip.member)];
            if (ctx.visible.is_set(member_tile))
            {
                bool alive = false;
                for (const auto& camp : ctx.camps)
                    if (camp.group == target_ && camp.member == state.trip.member && camp.hp > 0.0f) alive = true;
                if (!alive && observed.status == GroupStatus::alive) refresh = true;
            }
        }
        if (observed.seen_round == ctx.round && state.expected_group_hp >= 0.0f &&
            observed.hp_left >= 0.0f && std::abs(observed.hp_left - state.expected_group_hp) > 0.01f) refresh = true;
        if (state.trip.arrived() && !state.front_checked) refresh = true;
        if (refresh)
        {
            // A changed physical continuation is repriced with actual remaining work and
            // alternatives from this origin. Preserve the episode through recovery; a
            // better reachable job is an explicit release, never a silent target change.
            const float reserve = production_exit(ctx, nullptr, target_);
            if (reserve < 0.0f || ctx.hp <= reserve)
            {
                state.episode.recover(ctx.round);
                state.trip.invalidate();
                production_log(ctx, "trip-recovery");
                production_exit(ctx, &acts, -1);
                return true;
            }
            production::Quote quote;
            const int next_group = production_route_plan(ctx, quote, true);
            if (next_group == -2) { why_ = "plan-continuation"; return true; }
            if (next_group < 0)
            {
                production_release(ctx, "no-reachable-front");
                if (camp_threat_at(ctx, ctx.pos) > 0.0f) return retreat(ctx, acts);
                leave_camp_tiles(ctx, acts);
                return true;
            }
            if (next_group != target_)
            {
                production_log(ctx, "displaced-opportunity");
                state.episode.begin(next_group, ctx.round, kn.groups[static_cast<std::size_t>(next_group)].hp_left, ctx.gems);
                target_ = next_group;
                capturing_ = engaged_ = false;
                publish(target_, static_cast<int>(SlotStatus::engaging));
            }
            state.deadline = std::min(static_cast<int>(cambc::constants::max_rounds), ctx.round + 2 * quote.rounds);
            if (!fuel_ok(cfg::fuel_phase_farm)) return true;
        }
        if (!state.trip.arrived())
        {
            why_ = "trip-travel";
            if (!production_route_step(ctx, acts)) why_ = "trip-blocked";
            return true;
        }
    }
    const auto& info = world.groups[static_cast<std::size_t>(target_)];
    const auto& belief = kn.groups[static_cast<std::size_t>(target_)];
    bool all_seen = belief.seen_round == ctx.round;
    for (auto tile : info.tiles) if (!ctx.visible.is_set(tile)) { all_seen = false; break; }
    if constexpr (cfg::production_routes)
        if (!all_seen)
        {
            production_runtime().refused_fronts.push_back({ctx.pos.x,ctx.pos.y});
            production_runtime().trip.invalidate();
            why_ = "front-needs-vision";
            return true;
        }
    if (all_seen && belief.status == GroupStatus::alive)
    {
        if (production_runtime().episode.best_hp >= 0.0f && belief.hp_left > production_runtime().episode.best_hp + 0.01f)
        {
            production_release(ctx, "generation-changed", 10);
            return true;
        }
        production_runtime().episode.observe_progress(ctx.round, belief.hp_left);
        int nearest = 1 << 20;
        for (const auto& c : ctx.camps) if (c.group == target_) nearest = std::min(nearest, cheb(ctx.pos, c.pos));
        const float reserve = info.harmless ? 0.0f : production_exit(ctx, nullptr, target_);
        if (reserve < 0.0f || ctx.hp <= reserve)
        {
            production_runtime().episode.recover(ctx.round);
            production_log(ctx, "exit");
            capturing_ = engaged_ = false;
            if (camp_threat_at(ctx, ctx.pos) > 0.0f) production_exit(ctx, &acts, -1);
            else if (ctx.enemies.empty()) { cambc::self().recall(); production_log(ctx, "recall"); }
            return true;
        }
        if (nearest <= kit().attack_range)
        {
            why_ = "work";
            if (!fuel_ok(cfg::fuel_phase_basic)) return true;
            const bool acted = cfg::production_routes ? production_work_front(ctx, acts, target_, production_runtime().trip.member)
                                                       : production_work(ctx, acts, target_);
            float remaining = 0.0f;
            for (const auto& c : ctx.camps) if (c.group == target_ && c.hp > 0.0f) remaining += c.hp;
            production_runtime().episode.observe_progress(ctx.round, remaining);
            if constexpr (cfg::production_routes) production_runtime().expected_group_hp = remaining;
            if (acted) return true;
        }
    }
    if constexpr (cfg::production_routes)
    {
        production_runtime().trip.invalidate();
        why_ = "front-changed";
        return true;
    }
    // Movement is a separate boundary: one ordinary legal step, no hidden damaging dash.
    // The next owner turn observes the new geometry before it can wake/continue this group.
    if (ctx.round - production_runtime().episode.last_progress_round > std::max(30, production_runtime().deadline - production_runtime().episode.started_round))
    {
        production_release(ctx, "no-progress", 40);
        return true;
    }
    why_ = "travel";
    const auto* field = world.group_field(target_, true);
    if (!field) field = world.route_field(target_, ctx.route_pos);
    vec2 step{ -1, -1 };
    int best_distance = field ? static_cast<int>(field->get(ctx.pos)) : 1 << 20;
    if (field)
        for (auto candidate : step_options(blocked_now(), ctx.pos))
        {
            const int distance = field->get(candidate);
            if (distance != cambc::distance_field::unreachable && distance < best_distance)
            {
                best_distance = distance;
                step = candidate;
            }
        }
    const float destination_reserve = step.x >= 0 ? production_exit(ctx, nullptr, -1, step) : -1.0f;
    if (step.x >= 0 && destination_reserve >= 0.0f && ctx.hp > destination_reserve &&
        step_best([&](vec2 candidate) { return candidate == step ? 1 : 0; }, 0))
    {
        acts.moved = true;
        ctx.refresh_self();
    }
    else travel_blocked_ = true;
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
        {
            if (ctx.round - m.round > cfg::hunt_age || m.hp <= 0.0f || cheb(m.pos, ctx.pos) > cfg::hunt_radius ||
                cambc::map::spawn_region(cambc::team::enemy).is_set(m.pos))
                continue;
            // Mode 3: only carriers (a kill hands us the bag), the richest first.
            if (cfg::hunt_mode == 3)
            {
                if (m.gems >= static_cast<std::uint32_t>(cfg::hunt_carrier_gems) && (!prey || m.gems > prey->gems)) prey = &m;
            }
            else if (!prey || m.hp < prey->hp)
                prey = &m;
        }
        // Mode 2: only prey weaker than us (lower health).
        if (prey && (cfg::hunt_mode != 2 || prey->hp < ctx.hp))
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
    if constexpr (cfg::respawn_wait_liveness)
        if (target_ >= 0 && blocked_respawn(ctx, target_))
        {
            target_ = -1;
            staged_since_ = -1;
            engaged_ = false;
        }
    if (target_ >= 0)
    {
        const auto& b = kn.groups[static_cast<std::size_t>(target_)];
        const int travel = World::distance(world.group_field(target_, false), ctx.pos, world.groups[static_cast<std::size_t>(target_)].center);
        // Same horizon as score_group: worth keeping while the wait after arrival stays short.
        if (b.status == GroupStatus::respawning && b.respawn_round > ctx.round + travel + cfg::max_respawn_wait) target_ = -1;
        // Allies that published this group are on their way (from across the map on 00stampede):
        // keep the staging point longer for them.
        const bool big_prize = target_ >= 0 && world.groups[static_cast<std::size_t>(target_)].gems >= static_cast<std::uint32_t>(cfg::siege_patience_min_gems);
        int patience = cfg::staging_patience_committed > 0 && target_ >= 0 && kn.committed(target_) > 0 && big_prize
                           ? cfg::staging_patience_committed
                           : cfg::staging_patience;
        if (jackpot(target_)) patience = std::max(patience, cfg::jackpot_patience);  // the party needs time to gather
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
        const bool squads_on = cfg::squad_until_round <= 0 || ctx.round < cfg::squad_until_round;
        if (squads_on && leader >= 0 && leader != kn.self_index && !capturing_)
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
        if constexpr (cfg::planning_turn)
        {
            float score = 0.0f;
            const int pick = plan_target(ctx, &score);
            if (pick >= 0)
            {
                target_ = pick;
                target_score_ = score;
                chosen_round_ = ctx.round;
                staged_since_ = -1;
                engaged_ = false;
                progress_best_ = 1 << 20;
                progress_round_ = ctx.round;
                publish(pick, static_cast<int>(SlotStatus::engaging));
                why_ = "plan";
                return true;
            }
            publish(-1, static_cast<int>(SlotStatus::idle));
        }
        if constexpr (cfg::skirmish_memo) publish(-1, static_cast<int>(SlotStatus::idle));
        why_ = "idle-fuel"; leave_camp_tiles(ctx, acts);
        return false;
    }
    if (!following && can_choose && (target_ < 0 || ctx.round - chosen_round_ >= cfg::rechoose_every))
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
        if (b.respawn_round - ctx.round <= cfg::max_respawn_wait &&
            (!cfg::respawn_wait_liveness ||
             World::distance(world.group_field(target_, false), ctx.pos,
                             world.groups[static_cast<std::size_t>(target_)].center) <= cfg::respawn_travel_near))
        {
            publish(target_, static_cast<int>(SlotStatus::engaging));
            why_ = "await";
            leave_camp_tiles(ctx, acts);
            return true;
        }
        bool walk_on = false;
        if constexpr (cfg::respawn_travel)
        {
            const int walk = World::distance(world.group_field(target_, false), ctx.route_pos,
                                             world.groups[static_cast<std::size_t>(target_)].center);
            if (model::approach_respawn(b.respawn_round - ctx.round, walk))
            {
                if (walk <= cfg::respawn_travel_near)
                {
                    publish(target_, static_cast<int>(SlotStatus::engaging));
                    why_ = "await";
                    leave_camp_tiles(ctx, acts);
                    return true;
                }
                walk_on = true;
            }
        }
        if (!walk_on)
        {
            publish(target_, static_cast<int>(SlotStatus::cleared));
            target_ = -1;
            why_ = "idle-resp"; leave_camp_tiles(ctx, acts);
            return false;
        }
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
        bool shopping = cfg::shop_recall_gold > 0 && ctx.gold >= static_cast<std::uint32_t>(purse) &&
                        extra <= cfg::shop_recall_detour;
        // Value-based: what the gold buys, owned until our next natural spawn visit, plus the healing,
        // against the detour (shop.cpp, shop_recall_benefit).
        if constexpr (cfg::shop_recall_mode == 1)
        {
            const float benefit = !transport && ctx.gold >= 30u && fuel_ok(cfg::fuel_phase_farm) ? shop_recall_benefit(ctx) * cfg::shop_recall_scale : 0.0f;
            shopping = benefit >= static_cast<float>(std::max(0, extra) + 2);
            if (cfg::debug_shop && shopping && fuel_ok(cfg::fuel_phase_log))
                MB_LOG("shoprec r%d k%d g%u hp%.0f/%.0f extra%d b%.1f\n", ctx.round, static_cast<int>(kit().type), ctx.gold, ctx.hp, ctx.max_hp, extra, benefit);
        }
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

// Vision is per champion: report the nearest enemy we see in the spare shared bytes, and take a
// fresher report from an ally into our enemy memory (21% of traced deaths had a killer outside the
// fight simulation's starting set; top teams' kill parties converge from ~12 tiles, beyond our sight).
void Brain::share_sightings(Ctx& ctx)
{
    auto& kn = Knowledge::get();
    const auto& their_spawn = cambc::map::spawn_region(cambc::team::enemy);
    const ChampView* nearest = nullptr;
    for (const auto& e : ctx.enemies)
        if (!their_spawn.is_set(e.pos) && e.index >= 0 && e.index < 8 && (!nearest || cheb(e.pos, ctx.pos) < cheb(nearest->pos, ctx.pos)))
            nearest = &e;
    if (nearest)
    {
        kn.write_sighting(nearest->pos, nearest->index, ctx.round);
        return;
    }
    vec2 at{};
    int index = 0, age = 0;
    if (!kn.read_sighting(at, index, age, ctx.round) || age > cfg::sighting_age || index >= 8) return;
    auto& m = enemy_memory_[static_cast<std::size_t>(index)];
    if (m.round >= ctx.round - age) return;  // ours is as fresh
    const auto& r = roster(cambc::team::enemy);
    if (static_cast<std::size_t>(index) >= r.types.size()) return;
    if (m.round < 0 || m.hp <= 0.0f)
    {
        // Never seen alive: design stats grown like ours.
        const auto s = design_summary(r.types[static_cast<std::size_t>(index)]);
        const float g = std::clamp(ctx.strength / std::max(1.0f, kit().summary.strength), 1.0f, 4.0f);
        m.hp = m.max_hp = s.health * g;
        m.strength = s.strength * g;
        m.focus = s.focus * g;
        m.type = r.types[static_cast<std::size_t>(index)];
        m.range = s.attack_range;
        m.gems = 0;
    }
    m.pos = at;
    m.round = ctx.round - std::max(1, age);  // a report from this round counts as one round old (the fight
                                             // simulation takes remembered enemies from age 1)
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

void Brain::note_shop_signals(const Ctx& ctx, bool alive)
{
    auto& sig = shop_signals();
    const auto& kn = Knowledge::get();
    // Deaths of the other allies (their alive flags were refreshed this turn).
    if (sig.ally_alive.size() != kn.alive.size()) sig.ally_alive.assign(kn.alive.size(), 1);
    for (std::size_t i = 0; i < kn.alive.size(); ++i)
    {
        if (static_cast<int>(i) != kn.self_index && sig.ally_alive[i] && !kn.alive[i]) ++sig.team_deaths;
        sig.ally_alive[i] = kn.alive[i];
    }
    if (!alive)
    {
        if (!sig.was_dead)
        {
            ++sig.deaths;
            ++sig.spawn_visits;
        }
        sig.last_visit = ctx.round;
        sig.was_dead = true;
        sig.was_recalling = false;
        sig.was_on_spawn = true;
        return;
    }
    if (ctx.on_spawn)
    {
        if (!sig.was_on_spawn) ++sig.spawn_visits;
        sig.last_visit = ctx.round;
    }
    sig.was_on_spawn = ctx.on_spawn;
    const bool respawned = sig.was_dead;
    sig.was_dead = false;
    ++sig.alive_rounds;
    sig.fight_rounds = time_use_[4];
    if (sig.last_gold >= 0 && static_cast<int>(ctx.gold) > sig.last_gold) sig.earned_gold += static_cast<float>(static_cast<int>(ctx.gold) - sig.last_gold);
    sig.last_gold = static_cast<int>(ctx.gold);
    // Health lost since our last turn: champion damage when an enemy champion is within 6.
    if (!respawned && last_hp_ > 0.0f && ctx.hp < last_hp_)
    {
        bool pvp = false;
        for (const auto& e : ctx.enemies)
            if (cheb(e.pos, ctx.pos) <= 6) pvp = true;
        (pvp ? sig.pvp_damage : sig.camp_damage) += last_hp_ - ctx.hp;
    }
    // Completed recalls (a Home emblem saves turn ends on each): recalling last turn, on spawn now.
    const bool recalling = ctx.state.contains(cambc::champion_state::recalling);
    if (sig.was_recalling && !recalling && ctx.on_spawn) ++sig.recalls;
    sig.was_recalling = recalling;
}

void Brain::report_death(int round)
{
    death_reported_ = true;
    char buf[640];
    int len = std::snprintf(buf, sizeof buf, "death r%d k%d", round, static_cast<int>(kit().type));
    const int first = std::max(0, note_count_ - kNotes);
    // Integer fields only: formatting floats cost most of the death turn's fuel (85-87K of 100K
    // measured; two more log lines suspended the bot). Losses are printed rounded, as before.
    for (int i = first; i < note_count_ && len < static_cast<int>(sizeof buf) - 48; ++i)
    {
        const TurnNote& n = notes_[i % kNotes];
        len += std::snprintf(buf + len, sizeof buf - static_cast<std::size_t>(len), " %d:%d:%.2s/%.2s:e%dd%da%d:s%d-%d", n.round - round, n.hp,
                             n.mode, n.why, n.enemies, n.enemy_dist, n.allies_near, static_cast<int>(std::lround(n.our_loss)),
                             static_cast<int>(std::lround(n.their_loss)));
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
        return;
    }
    // On a boundary tile at a wall or the map edge every legal neighbour is another member tile no
    // farther from the centre, so the step above never fires: the champion held the group's respawn
    // for 476-1141 rounds while the team waited (00stampede both sides never finished). Walk the
    // shortest path to the nearest free tile off every camp (bounded field, a few K fuel).
    if (cfg::leave_camp_search && fuel_ok(cfg::fuel_safety_field))
    {
        const auto& walls = blocked_now();
        const cambc::bitboard roots = ~(walls | world.camp_spawns);
        cambc::bitboard visited;
        cambc::distance_field field;
        field.compute(walls, roots, visited, static_cast<std::uint16_t>(cfg::safety_field_radius));
        auto near_free = [&](vec2 n) {
            const auto d = field.get(n);
            return d == cambc::distance_field::unreachable ? -(1 << 20) : -static_cast<int>(d);
        };
        if (step_best(near_free, near_free(ctx.pos)))
        {
            acts.moved = true;
            ctx.refresh_self();
        }
    }
}

// A visible enemy (off its own spawn) stands within striking reach plus two tiles of the group.
bool Brain::group_contested(const Ctx& ctx, int group) const
{
    const auto& info = World::get().groups[static_cast<std::size_t>(group)];
    const auto& their_spawn = cambc::map::spawn_region(cambc::team::enemy);
    for (const auto& e : ctx.enemies)
    {
        if (their_spawn.is_set(e.pos)) continue;
        const int dx = std::max({ info.min_x - e.pos.x, e.pos.x - info.max_x, 0 });
        const int dy = std::max({ info.min_y - e.pos.y, e.pos.y - info.max_y, 0 });
        if (std::max(dx, dy) <= enemy_reach(e) + 2) return true;
    }
    return false;
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
