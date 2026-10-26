#include "brain.h"
#include <algorithm>
#include <cmath>
#include <string_view>
#include "config.h"
#include "hazard_model.h"
#include "knowledge.h"
#include "learn.h"
#include "nav.h"
#include "shop.h"
#include "world.h"

namespace mb {

namespace {

// cfg::learned state (one bot instance per champion, so one Brain per instance). Kept out of Brain so
// that with the layer off the class, and so the bot, is unchanged.
struct LearnState
{
    int gap_round{ -1 };
    float gap{};
    // Group block: weights (read once), per group the weighted static features, territory and the
    // factor last computed (round and travel it is valid for), and this round's coefficients.
    float w[learn::kMaxFeatures]{}, w2[learn::kMaxFeatures]{};
    std::vector<float> group_static, group_territory, group_lgems, factor;
    std::vector<std::uint8_t> group_jackpot;
    std::vector<int> factor_round, factor_travel;
    int turn_round{ -1 };
    float c_travel{}, c_enemy{}, c_territory{}, gap_now{};
    vec2 near_pos[16]{};
    int near_n{};
    // Spawn block: consecutive turns held on spawn (reset on leaving it) and our deaths so far.
    int hold_turns{};
    int deaths{};
};

LearnState& learn_state()
{
    static LearnState s;
    return s;
}

// cfg::hazard state (out of Brain for the same reason): health by round modulo 4, disengage count.
struct HazardState
{
    float hp[4]{ -1.0f, -1.0f, -1.0f, -1.0f };
    int fires{};
};

HazardState& hazard_state()
{
    static HazardState s;
    return s;
}

// Diagnostic state stays out of Brain; the default-off hooks and state are dead code.
// Flags: handler 1, fresh simulation 2, hazard branch 4, farm 8, planning 16,
// choice 32, can_choose 64, following 128, target switch 256, capturing 512, done 1024.
struct DecisionProbe
{
    int round{ -1 };
    bool sampled{};
    int flags{}, start_fuel{}, enemies{};
    std::uint32_t gems{};
    int old_target{ -1 }, candidate{ -1 }, old_score{ -1 }, new_score{ -1 };
};

DecisionProbe& decision_probe()
{
    static DecisionProbe p;
    return p;
}

DecisionProbe* decision_probe_at(int round)
{
    auto& p = decision_probe();
    return p.sampled && p.round == round ? &p : nullptr;
}

int probe_score(float score)
{
    if (!std::isfinite(score)) return -2;
    return static_cast<int>(std::clamp(score, -1.0f, 1000000.0f) * 100.0f);
}

// cfg::recall_tail: the round a health recall was deferred to a retreat step (recall at that turn's end).
int& tail_recall_round()
{
    static int r = -1;
    return r;
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
    FuelTrace trace;
    trace.begin();
    // cfg::debug_recall_probe R: does a recall started at the end of round R (after the turn's actions)
    // count, and does cancelling it at the start of R+1 leave that turn's actions free? (slot 0 only)
    const bool probing = cfg::debug_recall_probe > 0 && Knowledge::get().self_index == 0;
    if (probing && static_cast<int>(cambc::round_number()) == cfg::debug_recall_probe + 1)
    {
        auto& me = cambc::self();
        MB_LOG("probe r%u start: state %x recalling %d\n", cambc::round_number(), me.state().bits(), me.is_recalling() ? 1 : 0);
        try { if (me.is_recalling()) me.cancel_recall(); MB_LOG("probe cancel ok: state %x\n", me.state().bits()); }
        catch (const std::exception& e) { MB_LOG("probe cancel failed: %s\n", e.what()); }
    }
    if constexpr (cfg::debug_error_phase)
    {
        try
        {
            turn_inner(trace);
        }
        catch (...)
        {
            MB_LOG("error phase after %s (why %s) in %s\n", trace.n > 0 ? trace.names[trace.n - 1] : "-", why_ ? why_ : "-", g_crumb);
            try
            {
                auto& me = cambc::self();
                MB_LOG("error state: alive %d hp %.0f can_cast %d\n", me.is_alive() ? 1 : 0, static_cast<double>(me.health()), me.can_cast() ? 1 : 0);
                const auto ab = me.abilities();
                for (int i = 0; i < 2; ++i)
                    try { MB_LOG("error slot %d cooldown %d\n", i, static_cast<int>(ab[static_cast<std::size_t>(i)].cooldown())); }
                    catch (const std::exception& e) { MB_LOG("error slot %d: %s\n", i, e.what()); }
            }
            catch (const std::exception& e)
            {
                MB_LOG("error state query: %s\n", e.what());
            }
            throw;
        }
    }
    else
        turn_inner(trace);
    if (probing && static_cast<int>(cambc::round_number()) == cfg::debug_recall_probe + 1)
        MB_LOG("probe r%u end: state %x pos %d,%d\n", cambc::round_number(), cambc::self().state().bits(), cambc::self().position().x,
               cambc::self().position().y);
    if (probing && static_cast<int>(cambc::round_number()) == cfg::debug_recall_probe)
    {
        auto& me = cambc::self();
        MB_LOG("probe r%u before recall: state %x pos %d,%d\n", cambc::round_number(), me.state().bits(), me.position().x, me.position().y);
        try { me.recall(); MB_LOG("probe recall ok: state %x\n", me.state().bits()); }
        catch (const std::exception& e) { MB_LOG("probe recall failed: %s\n", e.what()); }
    }
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
        if constexpr (cfg::learned > 0)
            if (!death_reported_) ++learn_state().deaths;
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

    Ctx ctx;
    trace.mark("ctx");
    ctx.observe();
    trace.mark("observe");
    if constexpr (cfg::hazard > 0)
    {
        hazard_state().hp[ctx.round % 4] = ctx.hp;
        if (ctx.round % 500 == 0 && fuel_ok(cfg::fuel_phase_log)) MB_LOG("hazard r%d fires %d\n", ctx.round, hazard_state().fires);
    }
    if constexpr (cfg::shop_plan == 100 || cfg::shop_recall_mode > 0) note_shop_signals(ctx, true);
    kn.observe_groups(ctx, kit().vision + 1);
    if constexpr (cfg::group_news) share_news(ctx);
    remember_threats(ctx);
    if (cfg::share_sightings) share_sightings(ctx);
    if constexpr (cfg::phantom_mode == 2) share_pack(ctx);
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
    if constexpr (cfg::learned > 0)
        if (!ctx.on_spawn) learn_state().hold_turns = 0;
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
    // cfg::recall_cancel_reach k: damage interrupts a recall anyway, and cancelling is free - when an enemy
    // (off its spawn) is within its attack range + k tiles, cancel and let the threat logic act this turn
    // instead of standing in the channel until the hit lands.
    if constexpr (cfg::recall_cancel_reach > 0)
        if (!cancelled && st.contains(cambc::champion_state::recalling))
        {
            const auto& their_spawn = cambc::map::spawn_region(cambc::team::enemy);
            bool reach = false;
            for (const auto& e : ctx.enemies)
                if (!their_spawn.is_set(e.pos) && cheb(e.pos, ctx.pos) <= e.range + cfg::recall_cancel_reach) reach = true;
            if (reach)
                try
                {
                    cambc::self().cancel_recall();
                    cancelled = true;
                }
                catch (const std::exception& e)
                {
                    MB_LOG("cancel recall failed: %s\n", e.what());
                }
        }
    if (!cancelled && (st.contains(cambc::champion_state::recalling) || st.contains(cambc::champion_state::submitting_gems) ||
                       st.contains(cambc::champion_state::teleporting)))
    {
        // Recalling on a capture still holds its tile until the reward lands (cfg::capture_recall).
        if (cfg::capture_recall > 0 && capture_recall_group_ >= 0 && st.contains(cambc::champion_state::recalling))
            publish(capture_recall_group_, static_cast<int>(SlotStatus::capturing));
        else
            publish(-1, static_cast<int>(SlotStatus::idle));
        warm_fields(ctx);
        return;
    }
    if constexpr (cfg::capture_recall > 0) capture_recall_group_ = -1;
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
    if constexpr (cfg::debug_decision_probe > 0)
    {
        auto& p = decision_probe();
        p = DecisionProbe{};
        p.round = ctx.round;
        p.sampled = Knowledge::get().self_index == cfg::debug_trace_champion && ctx.round % std::max(1, cfg::debug_decision_probe) == 0;
        if (p.sampled)
        {
            p.flags = (capturing_ ? 512 : 0) | (done ? 1024 : 0);
            p.start_fuel = static_cast<int>(fuel());
            p.enemies = static_cast<int>(ctx.enemies.size());
            p.gems = ctx.gems;
            p.old_target = target_;
        }
    }
    // Phases run in priority order while fuel lasts; each later phase needs a larger margin
    // so a heavy turn degrades to fewer decisions instead of being suspended mid-action.
    if constexpr (cfg::tactics_s == 1)
    {
        if (!done && fuel_ok(cfg::fuel_phase_threats) && (done = handle_threats_s(ctx, acts))) mode = "pvp";
    }
    else if (!done && fuel_ok(cfg::fuel_phase_threats) && (done = handle_threats(ctx, acts)))
        mode = "pvp";
    if (!done && cfg::retreat_mode >= 2 && homeward_ && fuel_ok(cfg::fuel_phase_basic) && (done = continue_homeward(ctx, acts))) mode = "home";
    if constexpr (cfg::learned > 0)
        if (!done && ctx.on_spawn && hold_on_spawn(ctx))
        {
            publish(-1, static_cast<int>(SlotStatus::idle));
            done = true;
            mode = "hold";
        }
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
    // Retaliation (Ren's passive) can kill us during our own turn: no turn-end actions then (they throw).
    if (!me.is_alive()) return;
    if (!acts.cast && fuel_ok(cfg::fuel_phase_basic))
        use_support(ctx, acts, camp_threat_at(ctx, ctx.pos) > 0.0f || enemy_threat_at(ctx, ctx.pos) > 0.0f);
    if (cfg::ward_mode > 0 && fuel_ok(cfg::fuel_phase_basic)) maybe_place_ward(ctx);
    if constexpr (cfg::recall_tail > 0)
        if (tail_recall_round() == ctx.round && me.is_alive())
        {
            ctx.refresh_self();
            const auto st = me.state();
            const bool channel = st.contains(cambc::champion_state::recalling) || st.contains(cambc::champion_state::submitting_gems) ||
                                 st.contains(cambc::champion_state::teleporting);
            if (!channel && !cambc::map::spawn_region(cambc::team::ally).is_set(ctx.pos) && camp_threat_at(ctx, ctx.pos) <= 0.0f &&
                enemy_threat_at(ctx, ctx.pos) <= 0.0f)
                try
                {
                    me.recall();
                    why_ = "tail-recall";
                }
                catch (const std::exception& e)
                {
                    MB_LOG("tail recall failed: %s\n", e.what());
                }
        }
    trace.mark("support");
    warm_fields(ctx);
    trace.mark("warm");
    // The learned layer's per-game tables wait for a turn with fuel to spare (the first gameplay
    // turn already spends up to ~171K of its 200K); until then group factors are 1.
    if constexpr (cfg::learned > 0)
        if (fuel_ok(40000)) learn_setup();
    if constexpr (cfg::debug_decision_probe > 0)
        if (const auto* p = decision_probe_at(ctx.round); p && fuel_ok(25000))
            MB_LOG("dp r%d f%d u%d e%d g%u o%d c%d t%d s%d,%d m%.2s/%.2s\n", p->round, p->flags, p->start_fuel,
                   p->enemies, p->gems, p->old_target, p->candidate, target_, p->old_score, p->new_score, mode, why_);
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
    if constexpr (cfg::retreat_retaliation_guard)
    {
        if (!kit().ranged)
        {
            bool reserve_move = false;
            if constexpr (cfg::retreat_preserve_move)
                reserve_move = !ctx.enemies.empty() && cambc::self().can_move();
            fight_retreat_counters(ctx, acts, reserve_move);
        }
    }
    else if constexpr (cfg::retreat_preserve_move)
    {
        if (!kit().ranged)
            fight_retreat_counters(ctx, acts, !ctx.enemies.empty() && cambc::self().can_move());
    }
    else if (!kit().ranged && fight_enemies(ctx, acts, false)) {}
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

// ---- Local-power tactics (cfg::tactics_s, D327) ----------------------------------------------
// Fight study (.cache/main/study/fights/report.md): 85% of our deaths start with the local health
// share S below 0.5, and our forecast already predicted the loss in 80% of them; the response was
// wrong (holding, farming beside the fight, one-tile retreats, recalls in reach). This policy acts on
// S directly and commits to a disengage.

namespace {

bool ranged_burst_kit(cambc::champion_type t)
{
    return t == cambc::champion_type::vesper_vale || t == cambc::champion_type::cassi || t == cambc::champion_type::ryth ||
           t == cambc::champion_type::stellar || t == cambc::champion_type::prism || t == cambc::champion_type::tavi;
}

bool carry_kit(cambc::champion_type t)
{
    return t == cambc::champion_type::vesper_vale || t == cambc::champion_type::cassi || t == cambc::champion_type::stellar ||
           t == cambc::champion_type::prism;
}

int design_range(cambc::champion_type t)
{
    static int cache[16]{};
    const auto i = static_cast<std::size_t>(t);
    if (i >= 16) return 1;
    if (cache[i] == 0) cache[i] = std::max(1, static_cast<int>(design_summary(t).attack_range));
    return cache[i];
}

} // namespace

const ChampView* Brain::focus_s(const Ctx& ctx, bool* killable) const
{
    const auto& their_spawn = cambc::map::spawn_region(cambc::team::enemy);
    const float g = growth(ctx);
    // Our burst this turn: basic, a ready reset's second basic, the best ready damaging cast.
    float spell = 0.0f;
    int spell_range = 0, dash = 0;
    bool reset = false;
    {
        const auto abilities = cambc::self().abilities();
        for (int k = 0; k < 2; ++k)
        {
            const auto& s = kit().slot[k];
            if (abilities[static_cast<std::size_t>(k)].cooldown() > 0) continue;
            if (s.kind == model::AbilityKind::attack_reset) reset = true;
            else if ((s.kind == model::AbilityKind::damage || s.kind == model::AbilityKind::cc || s.kind == model::AbilityKind::aoe) && s.enemies)
            {
                spell = std::max(spell, s.modifier * ctx.focus);
                spell_range = std::max(spell_range, static_cast<int>(s.range + s.radius));
            }
            else if (s.kind == model::AbilityKind::dash)
            {
                dash = std::max(dash, static_cast<int>(s.range));
                if (s.modifier > 0.0f)
                {
                    spell = std::max(spell, s.modifier * ctx.focus);
                    spell_range = std::max(spell_range, static_cast<int>(s.range + s.radius));
                }
            }
        }
    }
    const int hit = kit().ranged ? kit().attack_range : kit().attack_range + 1;  // melee may step and strike
    const ChampView* best = nullptr;
    float best_score = -1e30f;
    bool best_kill = false;
    for (const auto& e : ctx.enemies)
    {
        if (e.hp <= 0.0f) continue;
        const int d = cheb(e.pos, ctx.pos);
        if (d > 9 || (their_spawn.is_set(e.pos) && d > 1)) continue;
        const float shield = shield_now(*e.champ);
        const float eff = e.hp + shield;
        float burst = 0.0f;
        if (d <= hit) burst += ctx.strength * (reset ? 2.0f : 1.0f);
        if (d <= spell_range) burst += spell;
        for (const auto& a : ctx.allies)
            if (cheb(a.pos, e.pos) <= (design_range(a.type) > 1 ? design_range(a.type) : 2))
                burst += pvp_profile_of(a.type, 0.0f, 0.0f).basic * g;
        const bool linked = e.type == cambc::champion_type::cassi || e.type == cambc::champion_type::issac;
        const bool kill = burst >= eff;
        const bool reachable = d <= std::max(hit, spell_range) + dash;
        float score = 0.0f;
        if (kill) score += 1000.0f + (linked ? 200.0f : 0.0f);
        if (carry_kit(e.type) && reachable) score += 300.0f;
        if (linked) score += 50.0f;
        if (e.type == cambc::champion_type::heart_of_ruin && shield > 0.0f) score -= 400.0f;
        score -= eff / 10.0f + static_cast<float>(d) + 0.01f * static_cast<float>(e.index);
        if (score > best_score)
        {
            best_score = score;
            best = &e;
            best_kill = kill && d <= std::max(hit, spell_range);
        }
    }
    if (killable) *killable = best_kill;
    return best;
}

bool Brain::flee_s(Ctx& ctx, TurnActions& acts, bool allow_recall)
{
    FuelSpike spike{ "flee_s", 30000 };
    publish(-1, static_cast<int>(SlotStatus::idle));
    capturing_ = false;
    engaged_ = false;
    why_ = "flee";
    auto& world = World::get();
    const auto* spawn = world.spawn_field(false);
    auto home = [&](vec2 p) {
        if (spawn)
        {
            const auto d = spawn->get(p);
            if (d != cambc::distance_field::unreachable) return static_cast<int>(d);
        }
        return cheb(p, world.ally_spawn_center);
    };
    // A kill on the way pays 100 gold and the bag: take a certain one (melee keeps its step).
    if (cambc::self().can_attack())
        for (auto& e : ctx.enemies)
            if (e.hp > 0.0f && e.hp + shield_now(*e.champ) <= ctx.strength && cheb(e.pos, ctx.pos) <= kit().attack_range)
            {
                fight_enemies(ctx, acts, false, &e);
                break;
            }
    // Threat sources: enemies by engage reach (+1), remembered ones by walking reach plus their age,
    // awake camps that can reach the step.
    struct Src { vec2 pos; int reach; float rate; };
    Src src[24];
    int ns = 0;
    int nearest = 1 << 20;
    bool burst = false;
    for (const auto& e : ctx.enemies)
    {
        const int d = cheb(e.pos, ctx.pos);
        nearest = std::min(nearest, d);
        burst = burst || ranged_burst_kit(e.type);
        if (d <= 12 && ns < 24) src[ns++] = { e.pos, enemy_reach(e) + 1, std::max(20.0f, e.dps) };
    }
    for (int i = 0; i < 8; ++i)
    {
        const auto& m = enemy_memory_[i];
        const int age = ctx.round - m.round;
        if (age <= 0 || age > cfg::enemy_memory_rounds || m.hp <= 0.0f) continue;
        bool seen = false;
        for (const auto& e : ctx.enemies)
            if (e.index == i) seen = true;
        if (seen) continue;
        const int d = cheb(m.pos, ctx.pos);
        nearest = std::min(nearest, std::max(1, d - age));
        burst = burst || ranged_burst_kit(m.type);
        if (d <= 12 + age && ns < 24) src[ns++] = { m.pos, (m.range <= 1 ? 2 : 4) + age + 1, 40.0f };
    }
    for (const auto& c : ctx.camps)
        if (c.awake && c.strength > 0.0f && cheb(c.pos, ctx.pos) <= c.range + 3 && ns < 24)
            src[ns++] = { c.pos, c.range, c.strength * ctx.camp_damage_taken_mult };
    // Recall only out of engage reach (study: completion 51-63% within 4 tiles, 85-92% at 5-8).
    const int need = cfg::s_recall_dist + (burst ? 1 : 0);
    if (allow_recall && !ctx.on_spawn && !acts.moved && !acts.attacked && !acts.cast && nearest >= need && ctx.n_unseen == 0 &&
        !ctx.hit_unseen && camp_threat_at(ctx, ctx.pos) <= 0.0f && (ctx.hp_frac() < 0.75f || home(ctx.pos) > 15))
        try
        {
            cambc::self().recall();
            why_ = "s-recall";
            return true;
        }
        catch (const std::exception& e)
        {
            MB_LOG("s recall failed: %s\n", e.what());
        }
    // Head for a healthy ally that is not between us and the enemies, else home.
    vec2 ally{ -1, -1 };
    int ally_d = 1 << 20;
    for (const auto& a : ctx.allies)
    {
        const int d = cheb(a.pos, ctx.pos);
        if (a.hp < 0.5f * a.max_hp || d > 12 || d <= 1) continue;
        if (last_threat_pos_.x >= 0 && cheb(a.pos, last_threat_pos_) + 2 < cheb(ctx.pos, last_threat_pos_)) continue;
        if (d < ally_d) { ally_d = d; ally = a.pos; }
    }
    auto threat = [&](vec2 n) {
        float t = 0.0f;
        for (int i = 0; i < ns; ++i)
        {
            const int depth = src[i].reach + 1 - cheb(n, src[i].pos);
            if (depth > 0) t += src[i].rate * static_cast<float>(depth);
        }
        return t;
    };
    auto score = [&](vec2 n) {
        const int dest = ally.x >= 0 ? cheb(n, ally) : home(n);
        return -dest * 100 - static_cast<int>(threat(n) * 2.0f);
    };
    // Dash out when something reaches us (survivors dashed in 58% of escapes, those who died 34%).
    if (!acts.cast && threat(ctx.pos) > 0.0f && fuel_ok(cfg::fuel_escape_dash) && cambc::self().can_cast())
    {
        const auto abilities = cambc::self().abilities();
        for (int i = 0; i < 2 && !acts.cast; ++i)
        {
            const auto& s = kit().slot[i];
            const auto& a = abilities[static_cast<std::size_t>(i)];
            if (s.kind != model::AbilityKind::dash || (s.exclusive && acts.moved) || a.cooldown() > 0) continue;
            vec2 best = ctx.pos;
            int best_score = score(ctx.pos);
            for (auto n : dash_landings(ctx.pos, static_cast<int>(s.range)))
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
                    invalidate_obstacles();
                    ctx.refresh_self();
                }
            }
            catch (const std::exception& e)
            {
                MB_LOG("s escape dash failed: %s\n", e.what());
            }
        }
    }
    if (cambc::self().can_move() && step_best(score, score(ctx.pos)))
    {
        acts.moved = true;
        ctx.refresh_self();
    }
    // Melee strikes back while backing off (walking and attacking share no allowance).
    if (!kit().ranged && !acts.attacked) fight_enemies(ctx, acts, false, focus_s(ctx, nullptr));
    return true;
}

bool Brain::handle_threats_s(Ctx& ctx, TurnActions& acts)
{
    FuelSpike spike{ "threats_s", 40000 };
    const auto& their_spawn = cambc::map::spawn_region(cambc::team::enemy);
    float their = 0.0f;
    int nearest = 1 << 20, count = 0;
    bool reach_now = false;
    int sx = 0, sy = 0;
    for (const auto& e : ctx.enemies)
    {
        const int d = cheb(e.pos, ctx.pos);
        if (d > 8) continue;
        const int reach = enemy_reach(e);
        if (their_spawn.is_set(e.pos) && d > reach) continue;
        their += e.hp + shield_now(*e.champ);
        nearest = std::min(nearest, d);
        reach_now = reach_now || d <= reach;
        sx += e.pos.x;
        sy += e.pos.y;
        ++count;
    }
    for (int i = 0; i < 8; ++i)
    {
        const auto& m = enemy_memory_[i];
        const int age = ctx.round - m.round;
        if (age <= 0 || age > cfg::enemy_memory_rounds || m.hp <= 0.0f) continue;
        bool seen = false;
        for (const auto& e : ctx.enemies)
            if (e.index == i) seen = true;
        if (seen) continue;
        const int d = cheb(m.pos, ctx.pos);
        if (d > 8 + age || their_spawn.is_set(m.pos)) continue;
        their += m.hp;
        nearest = std::min(nearest, std::max(1, d - age));
        sx += m.pos.x;
        sy += m.pos.y;
        ++count;
    }
    if constexpr (cfg::s_pack)
    {
        vec2 at{};
        int size = 0, age = 0;
        if (count > 0 && Knowledge::get().read_pack(at, size, age, ctx.round, cfg::pack_age) && cheb(at, ctx.pos) <= 8 + age)
        {
            int in_view = 0;
            for (const auto& e : ctx.enemies)
                if (cheb(e.pos, at) <= 4 + age) ++in_view;
            const float bulk = 1.0f + std::min(1.0f, static_cast<float>(ctx.round) / 600.0f);
            if (size > in_view) their += static_cast<float>(size - in_view) * 1000.0f * bulk;
        }
    }
    if (count == 0)
    {
        // A committed disengage ends two rounds after the last sighting (time to recall in peace).
        if (ctx.round <= disengage_until_ && ctx.round - last_threat_round_ <= 2) return flee_s(ctx, acts, true);
        spawn_hold_since_ = -1;
        return false;
    }
    last_threat_round_ = ctx.round;
    last_threat_pos_ = { sx / count, sy / count };
    float ours = ctx.hp + shield_now(cambc::self());
    for (const auto& a : ctx.allies)
        if (cheb(a.pos, ctx.pos) <= 8) ours += a.hp;
    const float s = ours / std::max(1.0f, ours + their);
    bool killable = false;
    const ChampView* focus = focus_s(ctx, &killable);
    // Spawn exit: below s_fight keep the healing tiles and strike what comes adjacent.
    if (ctx.on_spawn && s < cfg::s_fight)
    {
        if (spawn_hold_since_ < 0) spawn_hold_since_ = ctx.round;
        if (ctx.round - spawn_hold_since_ <= cfg::spawn_hold_max)
        {
            publish(-1, static_cast<int>(SlotStatus::idle));
            why_ = "spawn-hold";
            fight_enemies(ctx, acts, false, focus);
            return true;
        }
    }
    if (!ctx.on_spawn) spawn_hold_since_ = -1;
    if (s < cfg::s_flee && !killable)
    {
        disengage_until_ = std::max(disengage_until_, ctx.round + cfg::s_commit);
        return flee_s(ctx, acts, true);
    }
    if (s >= cfg::s_fight || killable)
    {
        disengage_until_ = -1;
        publish(-1, static_cast<int>(SlotStatus::idle));
        why_ = "fight";
        if (fight_enemies(ctx, acts, true, focus)) return true;
        return reach_now;
    }
    // Between the thresholds: answer in place when struck at, keep our distance when close, keep
    // capturing or farming otherwise; a committed disengage still runs.
    if (ctx.round <= disengage_until_) return flee_s(ctx, acts, true);
    if (reach_now)
    {
        publish(-1, static_cast<int>(SlotStatus::idle));
        why_ = "answer";
        fight_enemies(ctx, acts, false, focus);
        return true;
    }
    if (capturing_ || nearest > 5) return false;
    return flee_s(ctx, acts, false);
}

bool Brain::handle_threats(Ctx& ctx, TurnActions& acts)
{
    if constexpr (cfg::debug_decision_probe > 0)
        if (auto* p = decision_probe_at(ctx.round)) p->flags |= 1;
    FuelSpike spike{ "threats", 40000 };
    last_sim_[0] = last_sim_[1] = -1.0f;
    if constexpr (cfg::tactics_s == 2)
        if (ctx.round <= disengage_until_ && ctx.round - last_threat_round_ <= 2 && ctx.enemies.empty()) return flee_s(ctx, acts, true);
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
    // cfg::finish_kill: a kill this turn pays 100 gold and the bag and removes a fighter before the
    // verdict (the simulation below then sees it dead). Never steps off our spawn.
    if constexpr (cfg::finish_kill > 0)
        if (try_finish_kill(ctx, acts, ctx.on_spawn ? 1 : cfg::finish_kill, cfg::finish_slack)) ++finish_kills_;

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
    // Phantoms (cfg::phantom_mode): live enemies out of sight that may be walking with the nearest
    // visible one. They stand on its tile and arrive phantom_delay rounds after it (set below).
    [[maybe_unused]] const int phantom_from = n;
    if constexpr (cfg::phantom_mode == 1)
    {
        const ChampView* anchor = nullptr;
        int anchor_d = 1 << 20;
        for (const auto& e : ctx.enemies)
        {
            const int d = cheb(e.pos, ctx.pos);
            if (d < anchor_d && !their_spawn_tiles.is_set(e.pos)) { anchor_d = d; anchor = &e; }
        }
        const auto& r = roster(cambc::team::enemy);
        int added = 0;
        for (std::size_t i = 0; anchor && i < r.champs.size() && n < 16 && added < cfg::phantom_max; ++i)
        {
            bool seen = false;
            for (const auto& e : ctx.enemies)
                if (e.index == static_cast<int>(i)) seen = true;
            if (seen) continue;
            const bool known = i < 8 && enemy_memory_[i].round > -1000;
            const EnemyMemory* m = known ? &enemy_memory_[i] : nullptr;
            const int age = m ? ctx.round - m->round : ctx.round;
            // Already in the simulation as a remembered fighter (same test as the loop above).
            if (m && age > 0 && age <= cfg::enemy_memory_rounds && m->hp > 0.0f &&
                cheb(m->pos, ctx.pos) <= cfg::memory_join_radius + age && !their_spawn.is_set(m->pos))
                continue;
            bool alive = true;
            try
            {
                alive = r.champs[i].is_alive();
            }
            catch (const std::exception&)
            {
            }
            if (!alive) continue;
            const vec2 last = m ? m->pos : World::get().enemy_spawn_center;
            if (cheb(last, anchor->pos) - age > cfg::phantom_radius) continue;
            const auto type = r.types[i];
            const auto design = design_summary(type);
            const float bulk = 1.0f + std::min(1.0f, static_cast<float>(ctx.round) / 600.0f);
            const float hp = m ? std::max(m->hp, m->max_hp) : design.health * bulk;
            f[n] = { hp, 0.0f, false, value(m ? m->gems : 0u) };
            with_profile(f[n], pvp_profile_of(type, m ? m->strength : 0.0f, m ? m->focus : 0.0f), m ? 1.0f : g);
            enemy_of[n] = nullptr;
            pos_of[n] = anchor->pos;
            reach_of[n] = walk_reach(m ? m->range : static_cast<int>(design.attack_range));
            type_of[n++] = type;
            ++added;
        }
    }
    if constexpr (cfg::phantom_mode == 2)
    {
        // A pack an ally (or we) saw within pack_age rounds: members not in view stand at its last
        // centre; the sighting's age counts as already walked (arrivals below).
        vec2 at{};
        int size = 0, age = 0;
        auto& kn = Knowledge::get();
        if (kn.read_pack(at, size, age, ctx.round, cfg::pack_age) && cheb(at, ctx.pos) <= 8 + age + cfg::pack_reach &&
            !their_spawn.is_set(at))
        {
            int in_view = 0;
            for (const auto& e : ctx.enemies)
                if (cheb(e.pos, at) <= 4 + age) ++in_view;
            for (int i = 0; i < n; ++i)
                if (!f[i].ours && enemy_of[i] == nullptr && cheb(pos_of[i], at) <= 4 + age) ++in_view;  // remembered
            const auto& r = roster(cambc::team::enemy);
            int missing = std::min(size - in_view, cfg::phantom_max);
            for (std::size_t i = 0; missing > 0 && i < r.champs.size() && n < 16; ++i)
            {
                bool seen = false;
                for (const auto& e : ctx.enemies)
                    if (e.index == static_cast<int>(i)) seen = true;
                if (seen) continue;
                bool alive = true;
                try
                {
                    alive = r.champs[i].is_alive();
                }
                catch (const std::exception&)
                {
                }
                if (!alive) continue;
                const bool known = i < 8 && enemy_memory_[i].round > -1000;
                const EnemyMemory* m = known ? &enemy_memory_[i] : nullptr;
                const auto type = r.types[i];
                const auto design = design_summary(type);
                const float bulk = 1.0f + std::min(1.0f, static_cast<float>(ctx.round) / 600.0f);
                f[n] = { m ? std::max(m->hp, m->max_hp) : design.health * bulk, 0.0f, false, value(m ? m->gems : 0u) };
                with_profile(f[n], pvp_profile_of(type, m ? m->strength : 0.0f, m ? m->focus : 0.0f), m ? 1.0f : g);
                enemy_of[n] = nullptr;
                pos_of[n] = at;
                reach_of[n] = walk_reach(m ? m->range : static_cast<int>(design.attack_range));
                age_of[n] = age;
                type_of[n++] = type;
                --missing;
            }
        }
    }
    [[maybe_unused]] const int phantom_to = n;
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
    if constexpr (cfg::phantom_mode == 1)
        for (int i = phantom_from; i < phantom_to; ++i) f[i].arrive += cfg::phantom_delay;
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
    if constexpr (cfg::debug_decision_probe > 0)
        if (auto* p = decision_probe_at(ctx.round)) p->flags |= 2;
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
    // cfg::learned (D340): a learned edge added to the simulated enemy losses in the verdicts below.
    float theirs = o.their_losses;
    if constexpr (cfg::learned > 0)
    {
        float ours_hp = 0.0f, all_hp = 0.0f;
        for (int i = 0; i < n; ++i) { all_hp += f[i].hp; if (i < n_ours) ours_hp += f[i].hp; }
        std::uint32_t prize = 0;
        for (const auto& e : ctx.enemies) prize = std::max(prize, e.gems);
        const float x[learn::kMaxFeatures] = {
            1.0f,
            static_cast<float>(n_ours) / 5.0f,
            static_cast<float>(n - n_ours) / 5.0f,
            ours_hp / (all_hp + 1.0f),
            ctx.hp_frac(),
            std::min(1.0f, static_cast<float>(ctx.gems) / 100.0f),
            std::min(1.0f, static_cast<float>(prize) / 100.0f),
            ctx.on_spawn ? 1.0f : 0.0f,
            std::min(1.0f, static_cast<float>(nearest) / 8.0f),
            std::min(1.0f, static_cast<float>(o.rounds) / 10.0f),
            std::min(5.0f, o.our_losses) / 2.0f,
            std::min(5.0f, o.their_losses) / 2.0f,
            gem_gap(ctx),
            static_cast<float>(ctx.round) / 1000.0f,
            allies_engaged ? 1.0f : 0.0f,
            std::min(1.0f, static_cast<float>(cheb(ctx.pos, World::get().ally_spawn_center)) / 40.0f),
        };
        theirs += learn::dot(learn::fight, x);
    }
    const bool favourable = cfg::fight_better_trades_only && !behind
                                ? theirs >= o.our_losses + (allies_engaged ? 0.0f : margin)
                                : (o.we_survive && !o.they_survive && o.our_losses <= 1.0f) || theirs >= o.our_losses + 1.0f;
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
    // cfg::hazard: the learned death hazard (hazard_model.h) overrides the verdict with a disengage.
    if constexpr (cfg::hazard > 0)
    {
        static int hazard_until = -1;
        bool hot = ctx.round <= hazard_until;
        if (!hot)
        {
            const vec2 home = World::get().ally_spawn_center;
            int a4 = 0, a8 = 0, e4 = 0, e8 = 0, ranged5 = 0, nearest_ally = 8, nearest_enemy = 8, cut = 0;
            float hp4 = ctx.hp, hp8 = ctx.hp, eh4 = 0.0f, eh8 = 0.0f, frac_sum = 0.0f, enemy_max = 0.0f;
            for (const auto& a : ctx.allies)
            {
                const int d = cheb(a.pos, ctx.pos);
                nearest_ally = std::min(nearest_ally, d);
                if (d <= 4) { ++a4; hp4 += a.hp; }
                if (d <= 8) { ++a8; hp8 += a.hp; }
            }
            for (const auto& e : ctx.enemies)
            {
                if (their_spawn_tiles.is_set(e.pos)) continue;
                const int d = cheb(e.pos, ctx.pos);
                nearest_enemy = std::min(nearest_enemy, d);
                enemy_max = std::max(enemy_max, e.max_hp);
                if (d <= 4) { ++e4; eh4 += e.hp; }
                if (d <= 8) { ++e8; eh8 += e.hp; frac_sum += e.max_hp > 0.0f ? e.hp / e.max_hp : 1.0f; }
                if (d <= 5 && e.range > 1) ++ranged5;
                if (d <= 5 && cheb(e.pos, home) < cheb(ctx.pos, home)) cut = 1;
            }
            const float hp3 = hazard_state().hp[(ctx.round + 1) % 4];  // health three rounds ago (ring by round)
            const float lost = hp3 >= 0.0f ? (hp3 - ctx.hp) / std::max(1.0f, ctx.max_hp) : 0.0f;
            const float x[hazard::kFeatures] = {
                1.0f, ctx.hp_frac(), ctx.hp / 1000.0f, ctx.max_hp / 2000.0f,
                static_cast<float>(a4) / 4.0f, static_cast<float>(a8) / 4.0f, static_cast<float>(e4) / 4.0f, static_cast<float>(e8) / 4.0f,
                hp4 / (hp4 + eh4 + 1.0f), hp8 / (hp8 + eh8 + 1.0f),
                static_cast<float>(nearest_enemy) / 8.0f, static_cast<float>(nearest_ally) / 8.0f,
                static_cast<float>(ranged5) / 4.0f, static_cast<float>(cheb(ctx.pos, home)) / 40.0f,
                static_cast<float>(ctx.round) / 1000.0f, static_cast<float>(ctx.gems) / 100.0f,
                e8 > 0 ? frac_sum / static_cast<float>(e8) : 1.0f, lost, enemy_max / 2000.0f, static_cast<float>(cut),
            };
            float z = 0.0f;
            for (int i = 0; i < hazard::kFeatures; ++i) z += hazard::kWeights[i] * x[i];
            // p >= t  <=>  z >= logit(t)
            const float t = cfg::hazard_threshold;
            if (e8 > 0 && z >= std::log(t / (1.0f - t)))
            {
                hot = true;
                hazard_until = ctx.round + cfg::hazard_commit - 1;
                ++hazard_state().fires;
            }
        }
        if (hot)
        {
            if constexpr (cfg::debug_decision_probe > 0)
                if (auto* p = decision_probe_at(ctx.round)) p->flags |= 4;
            publish(-1, static_cast<int>(SlotStatus::idle));
            return retreat(ctx, acts);
        }
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
        if constexpr (cfg::tactics_s == 2) disengage_until_ = -1;
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
                              (cfg::approach_margin <= 0.0f || theirs >= o.our_losses + margin + cfg::approach_margin);
        const bool r = fight_enemies(ctx, acts, close_in, focus);
        if constexpr (cfg::pvp_camp_basic)
            if (r && target_ >= 0 && engaged_ && try_pvp_camp_basic(ctx, acts, target_))
            {
                last_hit_round_ = ctx.round;
                progress_round_ = ctx.round;
            }
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
    if constexpr (cfg::tactics_s == 2)
    {
        // D327 mode 2: the forecast's retreat verdict starts a committed disengage toward healthy
        // allies or home (recall only out of engage reach); farming may not resume meanwhile.
        if (nearest <= 5 || reachable || ctx.round <= disengage_until_)
        {
            if (nearest <= 5 || reachable) disengage_until_ = std::max(disengage_until_, ctx.round + cfg::s_commit);
            last_threat_round_ = ctx.round;
            int sx = 0, sy = 0, k = 0;
            for (const auto& e : ctx.enemies)
                if (cheb(e.pos, ctx.pos) <= 8) { sx += e.pos.x; sy += e.pos.y; ++k; }
            if (k > 0) last_threat_pos_ = { sx / k, sy / k };
            return flee_s(ctx, acts, true);
        }
        return false;
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
    // cfg::learned (D340): a learned factor on the health fraction that triggers the recall.
    if constexpr (cfg::learned > 0)
    {
        int enemies_near = 0, allies_near = 0;
        for (const auto& e : ctx.enemies)
            if (cheb(e.pos, ctx.pos) <= 8) ++enemies_near;
        for (const auto& a : ctx.allies)
            if (cheb(a.pos, ctx.pos) <= 4) ++allies_near;
        const float x[learn::kMaxFeatures] = {
            1.0f,
            std::min(1.0f, static_cast<float>(cheb(ctx.pos, World::get().ally_spawn_center)) / 40.0f),
            static_cast<float>(enemies_near) / 3.0f,
            std::min(1.0f, static_cast<float>(ctx.gems) / 100.0f),
            std::min(1.0f, static_cast<float>(ctx.gold) / 500.0f),
            static_cast<float>(ctx.round) / 1000.0f,
            engaged_ ? 1.0f : 0.0f,
            static_cast<float>(allies_near) / 3.0f,
            std::min(2.0f, incoming / std::max(1.0f, ctx.max_hp) * 10.0f),
            ctx.hit_unseen ? 1.0f : 0.0f,
            ctx.hp_frac(),
            -gem_gap(ctx),
        };
        recall_frac = std::clamp(recall_frac * learn::factor(learn::dot(learn::recall, x)), 0.02f, 0.98f);
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
            if constexpr (cfg::debug_finish_probe)
            {
                // Observe the shared candidate before the tested floor can reject its deferral.
                // All inputs are already-cached observations; one bounded line per category.
                static unsigned seen = 0;
                if (seen != 7u && f.damage * ctx.camp_damage_taken_mult < ctx.hp * 0.6f && ctx.hp_frac() > 0.15f && fuel_ok(45000))
                {
                    int near = 1 << 20;
                    vec2 near_pos{ -1, -1 };
                    float near_hp = 0.0f;
                    int members = 0, in_reach = 0;
                    const int reach = kit().attack_range;
                    for (const auto& c : ctx.camps)
                        if (c.group == target_ && c.hp > 0.0f)
                        {
                            const int d = cheb(c.pos, ctx.pos);
                            if (d < near) { near = d; near_pos = c.pos; near_hp = c.hp; }
                            ++members;
                            if (d <= reach) ++in_reach;
                        }
                    const unsigned category = near > reach ? 4u : f.rounds <= 1 ? 1u : 2u;
                    if ((seen & category) == 0)
                    {
                        seen |= category;
                        MB_LOG("F r%d k%d p%d,%d hp%d/%d t%d d%d mp%d,%d mh%u n%d nr%d fr%d fd%d raw%u pr%u cm%u inc%d th%d en%d a%d%d%d cat%u defer%d\n",
                               ctx.round, static_cast<int>(kit().type), ctx.pos.x, ctx.pos.y,
                               static_cast<int>(ctx.hp), static_cast<int>(ctx.max_hp), target_, near, near_pos.x, near_pos.y,
                               std::bit_cast<std::uint32_t>(near_hp), members, in_reach, f.rounds,
                               static_cast<int>(f.damage * ctx.camp_damage_taken_mult), std::bit_cast<std::uint32_t>(f.damage),
                               std::bit_cast<std::uint32_t>(f.peak_rate), std::bit_cast<std::uint32_t>(ctx.camp_damage_taken_mult), static_cast<int>(incoming),
                               static_cast<int>(recall_frac * 1000.0f), engaged_, acts.moved, acts.attacked, acts.cast, category,
                               ctx.hp_frac() > cfg::disengage_health_frac * 0.5f);
                    }
                }
            }
            if (f.damage * ctx.camp_damage_taken_mult < ctx.hp * 0.6f && ctx.hp_frac() > cfg::disengage_health_frac * 0.5f)
                return false;
        }
    }
    bool threatened = camp_threat_at(ctx, ctx.pos) > 0.0f || enemy_threat_at(ctx, ctx.pos) > 0.0f || ctx.hit_unseen;
    if constexpr (cfg::tactics_s == 2)
    {
        // Recall only out of engage reach (fight study: completion 51-63% within 4 tiles, 85-92% at 5-8).
        int near = 1 << 20;
        bool burst = false;
        for (const auto& e : ctx.enemies)
        {
            near = std::min(near, cheb(e.pos, ctx.pos));
            burst = burst || ranged_burst_kit(e.type);
        }
        if (near < cfg::s_recall_dist + (burst ? 1 : 0)) threatened = true;
    }
    publish(-1, static_cast<int>(SlotStatus::idle));
    capturing_ = false;
    engaged_ = false;
    if constexpr (cfg::tactics_s == 2)
        if (threatened && !ctx.enemies.empty()) return flee_s(ctx, acts, true);
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
    // Threatened: step away now and, if the step leaves every threat's reach, start the recall at the
    // end of this same turn (a channel blocks only the actions after it; the starting turn counts).
    if constexpr (cfg::recall_tail > 0) tail_recall_round() = ctx.round;
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
    // cfg::learned (D340): a learned factor on the bag size that triggers banking.
    if constexpr (cfg::learned > 0)
    {
        bool hunted = false;
        for (const auto& m : enemy_memory_)
            if (ctx.round - m.round <= 10 && cheb(m.pos, ctx.pos) <= 10) hunted = true;
        int allies_near = 0;
        for (const auto& a : ctx.allies)
            if (cheb(a.pos, ctx.pos) <= 6) ++allies_near;
        const float x[learn::kMaxFeatures] = {
            1.0f,
            static_cast<float>(std::min(da_used, 80)) / 40.0f,
            hunted ? 1.0f : 0.0f,
            ctx.hp_frac(),
            static_cast<float>(ctx.round) / 1000.0f,
            static_cast<float>(banked) / 1000.0f,
            static_cast<float>(cambc::global_submitted_gems(cambc::team::enemy)) / 1000.0f,
            static_cast<float>(allies_near) / 4.0f,
            std::min(1.0f, static_cast<float>(ctx.gold) / 500.0f),
            static_cast<float>(ctx.enemies.size()) / 3.0f,
            std::min(2.0f, static_cast<float>(ctx.gems) / 100.0f),
            std::min(1.0f, static_cast<float>(cheb(ctx.pos, world.ally_spawn_center)) / 40.0f),
        };
        bag = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::lround(static_cast<float>(bag) * learn::factor(learn::dot(learn::bank, x)))));
    }
    const bool trigger = gated_reach || via_gate || ctx.gems >= bag || da_used <= cfg::tribute_detour_tiles ||
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
    if constexpr (cfg::learned > 0) rate *= group_factor(ctx, group, travel);
    return rate;
}

void Brain::learn_setup() const
{
    const auto& world = World::get();
    auto& s = learn_state();
    if (!s.group_static.empty() || world.groups.empty()) return;
    for (int i = 0; i < learn::kSize[learn::group]; ++i) s.w[i] = learn::params.w[learn::kOffset[learn::group] + i];
    for (int i = 0; i < learn::kSize[learn::group2]; ++i) s.w2[i] = learn::params.w[learn::kOffset[learn::group2] + i];
    const auto n = world.groups.size();
    s.factor.assign(n, 1.0f);
    s.factor_round.assign(n, -1);
    s.factor_travel.assign(n, -1);
    s.group_static.reserve(n);
    s.group_territory.reserve(n);
    s.group_lgems.reserve(n);
    s.group_jackpot.reserve(n);
    for (const auto& info : world.groups)
    {
        const float d_ours = static_cast<float>(cheb(info.center, world.ally_spawn_center));
        const float d_theirs = static_cast<float>(cheb(info.center, world.enemy_spawn_center));
        const float territory = (d_ours - d_theirs) / (d_ours + d_theirs + 1.0f);
        int altar = 80;
        for (auto a : world.altars) altar = std::min(altar, cheb(a, info.center));
        const int g = static_cast<int>(s.group_static.size());
        // Features 9-15: log gems, log gold, territory (+ nearer theirs), jackpot, harmless, log health, altar distance.
        const float x[7] = { learn::log1p_fast(static_cast<float>(info.gems)) / 5.0f, learn::log1p_fast(static_cast<float>(info.gold)) / 6.0f,
                             territory, jackpot(g) ? 1.0f : 0.0f, info.harmless ? 1.0f : 0.0f, learn::log1p_fast(info.total_hp) / 8.0f,
                             static_cast<float>(altar) / 40.0f };
        s.group_static.push_back(learn::dot(learn::group, 9, x, 7));
        s.group_territory.push_back(territory);
        s.group_lgems.push_back(x[0]);
        s.group_jackpot.push_back(jackpot(g) ? 1 : 0);
    }
}

bool Brain::hold_on_spawn(const Ctx& ctx)
{
    // Spawn block: wait on our spawn (it heals, enemies cannot enter) instead of walking out into a
    // camped exit (check-in 2: 24% of exits in losses die within 40 rounds, 43% with a fed enemy near
    // the spawn or no ally alive). Capped at 40 turns per visit.
    auto& s = learn_state();
    if (s.hold_turns >= 40) return false;
    const auto& world = World::get();
    const auto& kn = Knowledge::get();
    int enemies = 0;
    float strongest = 0.0f;
    for (const auto& m : enemy_memory_)
        if (ctx.round - m.round <= 10 && cheb(m.pos, world.ally_spawn_center) <= 10)
        {
            ++enemies;
            strongest = std::max(strongest, m.max_hp);
        }
    int alive = 0, near_spawn = 0;
    for (std::size_t i = 0; i < kn.alive.size(); ++i)
        if (static_cast<int>(i) != kn.self_index && kn.alive[i]) ++alive;
    for (const auto& a : ctx.allies)
        if (cheb(a.pos, world.ally_spawn_center) <= 6) ++near_spawn;
    const float others = static_cast<float>(std::max<std::size_t>(1, kn.alive.size() - 1));
    const float x[learn::kMaxFeatures] = {
        1.0f,
        static_cast<float>(enemies) / 3.0f,
        std::min(3.0f, strongest / std::max(1.0f, ctx.max_hp)) / 3.0f,
        static_cast<float>(alive) / others,
        static_cast<float>(near_spawn) / others,
        std::min(1.0f, static_cast<float>(s.deaths) / 10.0f),
        std::min(1.0f, static_cast<float>(s.hold_turns) / 20.0f),
        static_cast<float>(ctx.round) / 1000.0f,
    };
    const auto key = static_cast<std::uint32_t>(ctx.round) * 16u + static_cast<std::uint32_t>(kn.self_index);
    if (!learn::soft_yes(learn::dot(learn::spawn, x), key)) return false;
    ++s.hold_turns;
    return true;
}

float Brain::gem_gap(const Ctx& ctx) const
{
    auto& s = learn_state();
    if (s.gap_round != ctx.round)
    {
        s.gap_round = ctx.round;
        const float gap = static_cast<float>(static_cast<int>(cambc::global_submitted_gems(cambc::team::enemy)) -
                                             static_cast<int>(cambc::global_submitted_gems(cambc::team::ally)));
        s.gap = std::clamp(gap / 300.0f, -1.0f, 1.0f);
    }
    return s.gap;
}

float Brain::group_factor(const Ctx& ctx, int group, int travel) const
{
    // Features 0-8 change during the game; 9-15 never do, so their weighted sum is computed once
    // per group. The bound loop calls this for every group, so per call it costs a few dozen
    // instructions: the features that depend only on the turn fold into per-round coefficients.
    const auto& world = World::get();
    const auto& kn = Knowledge::get();
    auto& s = learn_state();
    const auto gi = static_cast<std::size_t>(group);
    if (s.group_static.empty()) return 1.0f;  // tables not built yet (learn_setup)
    if (s.factor_round[gi] == ctx.round && s.factor_travel[gi] == travel) return s.factor[gi];
    // Once per round: enemies in view or seen in the last 10 rounds, and the coefficients of the
    // features that multiply a per-turn quantity (travel x carried gems, enemy proximity x missing
    // health, territory x banked-gem gap).
    if (s.turn_round != ctx.round)
    {
        s.turn_round = ctx.round;
        s.near_n = 0;
        for (const auto& e : ctx.enemies)
            if (s.near_n < 16) s.near_pos[s.near_n++] = e.pos;
        for (const auto& m : enemy_memory_)
            if (ctx.round - m.round <= 10 && s.near_n < 16) s.near_pos[s.near_n++] = m.pos;
        s.c_travel = (s.w[0] + s.w[8] * std::min(1.0f, static_cast<float>(ctx.gems) / 100.0f)) / 40.0f;
        s.c_enemy = s.w[2] + s.w[7] * (1.0f - ctx.hp_frac());
        s.gap_now = gem_gap(ctx);
        s.c_territory = s.w[6] * s.gap_now;
    }
    const auto& info = world.groups[gi];
    const auto& belief = kn.groups[gi];
    // Features: 0 travel/40, 1 gem share of the reward, 2 enemy proximity (1 at the centre, 0 from
    // 15 tiles), 3 respawn wait/20, 4 committed allies/3, 5 health left, 6 territory x gap,
    // 7 proximity x missing health, 8 travel x carried gems/100 (folded into the coefficients).
    float logit = s.group_static[gi] + s.c_travel * static_cast<float>(travel) + s.c_territory * s.group_territory[gi];
    const int committed = kn.committed(group);
    if (s.near_n > 0)
    {
        int closest = 15;
        for (int i = 0; i < s.near_n; ++i) closest = std::min(closest, cheb(s.near_pos[i], info.center));
        const float near = 1.0f - static_cast<float>(closest) / 15.0f;
        // group2 block: enemy proximity x (jackpot, log gems, committed allies, banked-gem gap).
        logit += near * (s.c_enemy + s.w2[0] * static_cast<float>(s.group_jackpot[gi]) + s.w2[1] * s.group_lgems[gi] +
                         s.w2[2] * static_cast<float>(committed) / 3.0f + s.w2[3] * s.gap_now);
    }
    if (info.gems > 0)
    {
        const float gem_value = ctx.gem_weight * static_cast<float>(info.gems);
        const float reward = gem_value + cfg::gold_value * static_cast<float>(info.gold);
        logit += s.w[1] * (info.gold > 0 && reward > 0.0f ? gem_value / reward : 1.0f);
    }
    if (belief.status == GroupStatus::respawning)
        logit += s.w[3] * static_cast<float>(std::max(0, belief.respawn_round - (ctx.round + travel))) / 20.0f;
    if (committed) logit += s.w[4] * static_cast<float>(committed) / 3.0f;
    float left = 1.0f;
    if (belief.status == GroupStatus::reward) left = 0.0f;
    else if (belief.status == GroupStatus::alive && belief.hp_left >= 0.0f && info.total_hp > 0.0f) left = std::min(1.0f, belief.hp_left / info.total_hp);
    logit += s.w[5] * left;
    const float f = learn::factor(logit);
    s.factor[gi] = f;
    s.factor_round[gi] = ctx.round;
    s.factor_travel[gi] = travel;
    return f;
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
        if constexpr (cfg::learned > 0) bound[g] *= group_factor(ctx, static_cast<int>(g), travel);
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
        if (pick == n || !fuel_ok(cfg::fuel_score_group + ((cfg::score_reserve_dearest || cfg::squad_size >= 9) ? dearest : 0))) break;
        bound[pick] = 0.0f;
        int need = 1;
        const auto before = (cfg::score_reserve_dearest || cfg::squad_size >= 9) ? fuel() : 0;
        const float s = score_group(ctx, static_cast<int>(pick), &need);
        if (cfg::score_reserve_dearest || cfg::squad_size >= 9) dearest = std::max(dearest, before - std::min(before, fuel()));
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

bool Brain::handle_farm(Ctx& ctx, TurnActions& acts)
{
    if constexpr (cfg::debug_decision_probe > 0)
        if (auto* p = decision_probe_at(ctx.round)) p->flags |= 8;
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
    if (cfg::debug_farm_fuel > 0 && ctx.round <= cfg::debug_farm_fuel) MB_LOG("ff r%d start %llu\n", ctx.round, static_cast<unsigned long long>(fuel()));
    auto& kn = Knowledge::get();
    auto& world = World::get();

    if constexpr (cfg::escort_until > 0)
        if (ctx.round < cfg::escort_until && !capturing_)
        {
            const int r = escort(ctx, acts);
            if (r >= 0) return r > 0;
        }

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
                // Recall on the capture: the channel lands right after the reward (capture phases
                // resolve at round start, a recall at our turn start), so the wait buys the trip home.
                if constexpr (cfg::capture_recall > 0)
                    if (capture_hold_turns_ >= cambc::constants::capture_time - recall_turns() + 1 && ctx.enemies.empty() &&
                        ctx.n_unseen == 0 && !ctx.hit_unseen && camp_threat_at(ctx, ctx.pos) <= 0.0f)
                    {
                        const auto& info = world.groups[static_cast<std::size_t>(target_)];
                        const float mult = kit().bonus_gold ? 1.33f : 1.0f;
                        const float after = static_cast<float>(ctx.gold) + mult * static_cast<float>(info.gold);
                        bool go = after >= static_cast<float>(cfg::capture_recall_gold) || ctx.hp_frac() < cfg::capture_recall_hp;
                        if constexpr (cfg::capture_recall == 2)
                        {
                            const int from_spawn = World::distance(world.group_field(target_, false), world.ally_spawn_center, info.center);
                            const float walk = from_spawn < (1 << 14) ? static_cast<float>(from_spawn) : 1e6f;
                            const auto reward = static_cast<std::uint32_t>(mult * static_cast<float>(info.gold));
                            const float benefit = fuel_ok(cfg::fuel_phase_farm) ? shop_recall_benefit(ctx, reward) * cfg::shop_recall_scale : 0.0f;
                            go = benefit >= cfg::capture_recall_walk * walk - static_cast<float>(cfg::capture_recall_slack);
                        }
                        if (go)
                            try
                            {
                                cambc::self().recall();
                                capture_recall_group_ = target_;
                                why_ = "caprecall";
                                return true;
                            }
                            catch (const std::exception& e)
                            {
                                MB_LOG("capture recall failed: %s\n", e.what());
                            }
                    }
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
            const bool mine = !cfg::carrier_capture || designated_capturer(ctx, target_);
            if (mine && (on_tile || kn.capturers(target_) == 0))
            {
                if (go_capture(ctx, acts, target_)) return true;
            }
            banned_until_[static_cast<std::size_t>(target_)] = ctx.round + 8;
            target_ = -1;
            // Another champion takes this reward: off its tiles, or we would split it.
            if (cfg::carrier_capture && !mine && on_tile) leave_camp_tiles(ctx, acts);
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
    // The opening elk party (cfg::opening_party, D331): the first K drafted champions take the
    // elk-bearing group nearest our spawn together before farming on their own.
    if constexpr (cfg::opening_party > 0)
        if (!opening_done_ && ctx.round < cfg::opening_until && !capturing_)
        {
            if (opening_group_ == -2) opening_group_ = pick_opening_group();
            const int g = opening_group_;
            const auto& r = roster(cambc::team::ally);
            int my_index = 0;
            for (std::size_t i = 0; i < r.champs.size(); ++i)
                if (static_cast<cambc::handle_champion_t>(r.champs[i]) == static_cast<cambc::handle_champion_t>(cambc::self()))
                    my_index = static_cast<int>(i);
            if (g < 0 || my_index >= cfg::opening_party) opening_done_ = true;
            else
            {
                const auto& ob = kn.groups[static_cast<std::size_t>(g)];
                // Done once it is taken (reward on the ground or respawning) or after the deadline.
                if (ob.status == GroupStatus::respawning || (ob.status == GroupStatus::reward && ob.seen_round == ctx.round && target_ != g))
                    opening_done_ = true;
                else
                {
                    following = true;
                    if (target_ != g)
                    {
                        target_ = g;
                        staged_since_ = -1;
                        engaged_ = false;
                        progress_best_ = 1 << 20;
                        progress_round_ = ctx.round;
                        chosen_round_ = ctx.round;
                    }
                    target_need_ = std::min(cfg::opening_party, static_cast<int>(r.champs.size()));
                }
            }
        }
    if (!following)
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
    if constexpr (cfg::debug_decision_probe > 0)
        if (auto* p = decision_probe_at(ctx.round)) p->flags |= (can_choose ? 64 : 0) | (following ? 128 : 0);
    if (cfg::debug_farm_fuel > 0 && ctx.round <= cfg::debug_farm_fuel) MB_LOG("ff r%d prechoose %llu\n", ctx.round, static_cast<unsigned long long>(fuel()));
    if (!following && target_ < 0 && !can_choose)
    {
        if constexpr (cfg::planning_turn)
        {
            float score = 0.0f;
            const int pick = plan_target(ctx, &score);
            if constexpr (cfg::debug_decision_probe > 0)
                if (auto* p = decision_probe_at(ctx.round))
                {
                    p->flags |= 16;
                    p->candidate = pick;
                    p->new_score = probe_score(score);
                }
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
    bool chose_now = false;
    if (!following && can_choose && (target_ < 0 || ctx.round - chosen_round_ >= cfg::rechoose_every))
    {
        chose_now = true;
        float score = 0.0f;
        const int pick = choose_target(ctx, &score);
        float current = 0.0f;
        int need = 1;
        if (target_ >= 0) current = score_group(ctx, target_, &need);
        if constexpr (cfg::debug_decision_probe > 0)
            if (auto* p = decision_probe_at(ctx.round))
            {
                p->flags |= 32;
                p->old_target = target_;
                p->candidate = pick;
                p->old_score = probe_score(current);
                p->new_score = probe_score(score);
            }
        if (target_ < 0 || (pick >= 0 && pick != target_ && score > current * cfg::retarget_ratio))
        {
            if constexpr (cfg::debug_decision_probe > 0)
                if (auto* p = decision_probe_at(ctx.round); p && pick >= 0 && pick != target_) p->flags |= 256;
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
    if (cfg::debug_farm_fuel > 0 && ctx.round <= cfg::debug_farm_fuel) MB_LOG("ff r%d chosen %llu t%d\n", ctx.round, static_cast<unsigned long long>(fuel()), target_);
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
            // A camp fight costs ~15-20K (plan, dash scan, tile scores, strikes): after a fresh target
            // choice on a heavy turn, open it next turn instead (cfg::choose_hold_fuel).
            if (cfg::choose_hold_fuel > 0 && chose_now && !fuel_ok(cfg::choose_hold_fuel))
            {
                why_ = "choose-hold";
                return true;
            }
            why_ = "fight";
            if (cfg::debug_farm_fuel > 0 && ctx.round <= cfg::debug_farm_fuel) MB_LOG("ff r%d prefight %llu\n", ctx.round, static_cast<unsigned long long>(fuel()));
            const bool fought = fight_camps(ctx, acts, target_, true);
            if (acts.attacked || acts.cast) last_hit_round_ = ctx.round;
            if (fought) return true;
            why_ = "fight-travel"; return travel_to_group(ctx, acts, target_);
        }
        progress_round_ = ctx.round;  // waiting at the staging point is not a stall
        if (cfg::debug_farm_fuel > 0 && ctx.round <= cfg::debug_farm_fuel) MB_LOG("ff r%d prestage %llu\n", ctx.round, static_cast<unsigned long long>(fuel()));
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
    if (cfg::debug_farm_fuel > 0 && ctx.round <= cfg::debug_farm_fuel) MB_LOG("ff r%d pretravel %llu\n", ctx.round, static_cast<unsigned long long>(fuel()));
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

// cfg::escort_until (D329): the opening ball. The acting leader - the lowest index alive with a
// published engaging/capturing target - farms for the whole team (score_group counts its squad);
// everyone else escorts it: stays within escort_radius tiles, strikes the camps of its target in
// reach, captures the cleared target when designated (carrier rule), and walks to the leader's
// target when the leader is out of sight. Returns -1 when this champion leads (normal farming).
int Brain::escort(Ctx& ctx, TurnActions& acts)
{
    auto& kn = Knowledge::get();
    const auto& slots = kn.slots();
    // The leader of our squad (squad_size 9+: everyone; 2: our pair).
    int leader = -1;
    const int squad = kn.my_squad();
    for (std::size_t i = 0; i < slots.size() && i < kn.alive.size() && i < kn.squad_of.size(); ++i)
        if (kn.squad_of[i] == squad && kn.alive[i] && slots[i].group >= 0 &&
            (slots[i].status == SlotStatus::engaging || slots[i].status == SlotStatus::capturing))
        {
            leader = static_cast<int>(i);
            break;
        }
    if (leader < 0 || leader == kn.self_index) return -1;
    const int group = slots[static_cast<std::size_t>(leader)].group;
    auto& world = World::get();
    if (group >= static_cast<int>(world.groups.size())) return -1;
    const auto& b = kn.groups[static_cast<std::size_t>(group)];
    // Carrier capture of the leader's cleared target, or step off its tiles.
    if (b.seen_round == ctx.round && b.status == GroupStatus::reward)
    {
        if (designated_capturer(ctx, group))
        {
            target_ = group;
            if (go_capture(ctx, acts, group)) return 1;
        }
        else
            leave_camp_tiles(ctx, acts);
    }
    if (capturing_ && target_ >= 0) return -1;  // holding a capture: the normal branch keeps it
    target_ = group;
    publish(group, static_cast<int>(SlotStatus::engaging));
    why_ = "escort";
    const auto leader_handle = static_cast<cambc::handle_champion_t>(kn.allies[static_cast<std::size_t>(leader)]);
    const ChampView* lv = nullptr;
    for (const auto& a : ctx.allies)
        if (static_cast<cambc::handle_champion_t>(*a.champ) == leader_handle) lv = &a;
    bool camps_here = false, awake = false;
    for (const auto& c : ctx.camps)
        if (c.group == group && c.hp > 0.0f)
        {
            camps_here = true;
            awake = awake || c.awake;
        }
    if (lv)
    {
        const int d = cheb(lv->pos, ctx.pos);
        // Strike the leader's camps beside the leader, or once they are awake; never wake them alone
        // while the leader stages for the team.
        if (camps_here && (d <= cfg::escort_radius || awake) && fight_camps(ctx, acts, group, d <= cfg::escort_radius)) return 1;
        if (d > cfg::escort_radius && cambc::self().can_move())
        {
            const vec2 at = lv->pos;
            auto score = [&](vec2 n) { return -cheb(n, at) * 100 - static_cast<int>(camp_threat_at(ctx, n)); };
            if (step_best(score, score(ctx.pos)))
            {
                acts.moved = true;
                ctx.refresh_self();
            }
        }
        return 1;
    }
    // Leader out of sight: its target (strike only what is already awake).
    if (camps_here && awake && fight_camps(ctx, acts, group, true)) return 1;
    travel_to_group(ctx, acts, group);
    return 1;
}

int Brain::pick_opening_group() const
{
    // The elk-bearing group (no golem or monolith) nearest our spawn and nearer it than theirs.
    const auto& world = World::get();
    int best = -1, best_d = 1 << 20;
    for (std::size_t g = 0; g < world.groups.size(); ++g)
    {
        const auto& info = world.groups[g];
        bool elk = false, big = false;
        for (auto ty : info.types)
        {
            elk = elk || ty == static_cast<std::uint8_t>(cambc::camp_type::elk);
            big = big || ty == static_cast<std::uint8_t>(cambc::camp_type::golem) || ty == static_cast<std::uint8_t>(cambc::camp_type::monolith);
        }
        if (!elk || big) continue;
        const int ours = cheb(info.center, world.ally_spawn_center), theirs = cheb(info.center, world.enemy_spawn_center);
        if (ours >= theirs || ours > cfg::opening_radius) continue;
        if (ours < best_d) { best_d = ours; best = static_cast<int>(g); }
    }
    return best;
}

bool Brain::designated_capturer(const Ctx& ctx, int group) const
{
    const auto& info = World::get().groups[static_cast<std::size_t>(group)];
    auto near_group = [&](vec2 p) {
        for (auto t : info.tiles)
            if (cheb(p, t) <= 2) return true;
        return false;
    };
    auto on_member = [&](vec2 p) {
        for (auto t : info.tiles)
            if (t == p) return true;
        return false;
    };
    // Gold to Vesper (+33%); gems to Marcella (2 gold each at tribute) or Prism (Focus while carried).
    const bool gold = info.gold > 0;
    auto score = [&](cambc::champion_type type, vec2 p, int index) {
        float s = 0.0f;
        if (gold && type == cambc::champion_type::vesper_vale) s += 100.0f;
        if (!gold)
        {
            if (type == cambc::champion_type::marcella) s += 20.0f;
            else if (type == cambc::champion_type::prism) s += 15.0f;
            else if (type == cambc::champion_type::vesper_vale) s -= 50.0f;
        }
        if (on_member(p)) s += 5.0f;
        return s - 0.01f * static_cast<float>(index);
    };
    static const int my_index = [] {
        const auto& r = roster(cambc::team::ally);
        const auto me = static_cast<cambc::handle_champion_t>(cambc::self());
        for (std::size_t i = 0; i < r.champs.size(); ++i)
            if (static_cast<cambc::handle_champion_t>(r.champs[i]) == me) return static_cast<int>(i);
        return 0;
    }();
    const float mine = score(kit().type, ctx.pos, my_index);
    for (const auto& a : ctx.allies)
    {
        if (!near_group(a.pos) || score(a.type, a.pos, a.index) <= mine) continue;
        try
        {
            const auto s = a.champ->state();
            if (s.contains(cambc::champion_state::recalling) || s.contains(cambc::champion_state::submitting_gems)) continue;
        }
        catch (const std::exception&)
        {
        }
        return false;
    }
    return true;
}

void Brain::share_news(const Ctx& ctx)
{
    // cfg::group_news: read the team's news record, then post the most useful respawning group we see
    // now - one an ally is walking to first (it would find it gone on arrival: 14% of champion-rounds
    // went to walks towards groups no longer alive, tracks/medium.md), else the richest. A record is
    // replaced only by news at least as urgent, so a later turn does not bury an ally's warning.
    auto& kn = Knowledge::get();
    const auto& world = World::get();
    const int current = kn.apply_news();
    const auto& slots = kn.slots();
    auto urgent = [&](int g) {
        for (std::size_t i = 0; i < slots.size(); ++i)
            if (static_cast<int>(i) != kn.self_index && kn.alive[i] && slots[i].group == g && slots[i].status == SlotStatus::engaging) return true;
        return false;
    };
    int best = -1;
    float best_score = 0.0f;
    for (std::size_t g = 0; g < world.groups.size(); ++g)
    {
        const auto& b = kn.groups[g];
        if (b.seen_round != ctx.round || b.status != GroupStatus::respawning || b.respawn_round <= ctx.round + 5) continue;
        if (static_cast<int>(g) == current) continue;
        const float s = (urgent(static_cast<int>(g)) ? 1e6f : 0.0f) + static_cast<float>(world.groups[g].gems + world.groups[g].gold);
        if (s > best_score) { best_score = s; best = static_cast<int>(g); }
    }
    if (best < 0) return;
    if (current >= 0 && urgent(current) && !urgent(best)) return;
    kn.news_out(best, kn.groups[static_cast<std::size_t>(best)].respawn_round);
}

void Brain::share_pack(const Ctx& ctx)
{
    // The largest cluster of visible enemies (within 4 tiles of one of them, off their spawn).
    const auto& their_spawn = cambc::map::spawn_region(cambc::team::enemy);
    int best = 0;
    vec2 centre{};
    for (const auto& e : ctx.enemies)
    {
        if (their_spawn.is_set(e.pos)) continue;
        int k = 0, sx = 0, sy = 0;
        for (const auto& o : ctx.enemies)
            if (!their_spawn.is_set(o.pos) && cheb(o.pos, e.pos) <= 4)
            {
                ++k;
                sx += o.pos.x;
                sy += o.pos.y;
            }
        if (k > best)
        {
            best = k;
            centre = { sx / k, sy / k };
        }
    }
    if (best < 2) return;
    auto& kn = Knowledge::get();
    vec2 at{};
    int size = 0, age = 0;
    const bool held = kn.read_pack(at, size, age, ctx.round, cfg::pack_age);
    if (!held || age >= 2 || best >= size || cheb(at, centre) <= 4) kn.write_pack(centre, best, ctx.round);
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
