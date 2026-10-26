// main/zero turn loop (DESIGN.md 2): observe -> trunk -> heads -> actions. No choice is made here; every choice
// is a network head over rule-generated candidates.
#include "bot.h"
#include <cambc.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include "core/cand.h"
#include "core/action.h"
#include "core/catalog.h"
#include "core/codec.h"
#include "core/net.h"
#include "core/plan.h"
#include "core/student_decision.h"
#include "core/student_context_cache.h"
#include "core/student_history.h"
#include "core/student_probe.h"
#include "sdk.h"
#include "weights.h"

namespace {

using namespace zero;

const float* weights()
{
    float* p = const_cast<float*>(params.w);
    asm volatile("" : "+r"(p) : : "memory");  // the block is written by the runner after load: never fold it
    return p + kConfig;
}

float config(int i)
{
    const float* p = weights() - kConfig;
    return p[i];
}

struct State
{
    Map map;
    Obs obs;  // reused every turn (its vectors keep their capacity)
    Memory mem;
    student::CampContextCache camp_context_cache;
    sdk::EnginePaths* paths{};
    std::uint64_t rng{};
    int lines{};
    int turns{};  // turns played since the map load
    int init_stage{};  // 0 terrain, 1 groups, 2 finish, 3 path cache, 4 first observation, 5 spawn field, 6 playing
};

State& state()
{
    static State s;
    return s;
}

// Sampled training log line: Z head round roster n chosen logp | state | phi... (codec, two chars per value).
void log_decision(const Obs& o, const float* sv, const CandSet& set, int chosen, float logp)
{
    auto& s = state();
    if (s.lines >= 6000) return;
    const float rate = config(1);
    if (rate <= 0.0f) return;
    s.rng = net::mix(s.rng);
    if (static_cast<float>(s.rng >> 40) / static_cast<float>(1ull << 24) >= rate) return;
    ++s.lines;
    static char buf[2 * (kState + kMaxCands * kMaxPhi) + 64];
    int n = 0;
    for (int i = 0; i < kState; ++i, n += 2) codec::put2(buf + n, sv[i]);
    const int phi = kPhi[set.head];
    for (int c = 0; c < set.n; ++c)
        for (int k = 0; k < phi; ++k, n += 2) codec::put2(buf + n, set.c[c].phi[k]);
    buf[n] = 0;
    std::printf("Z %d %d %d %d %d %.4f %s\n", static_cast<int>(set.head), o.round, o.self.roster, set.n, chosen, static_cast<double>(logp), buf);
}

int choose_decision(const Obs& o, const float* sv, const CandSet& set, const float* lg)
{
    if (set.n <= 0) return -1;
    if (set.n == 1) return 0;  // nothing to decide (and nothing to learn from)
    auto& s = state();
    net::Pick pick;
    if (config(5) > 0.0f && config(0) <= 0.0f && config(1) <= 0.0f)
    {
        // The deterministic policy has probability one on its argmax. Evaluation does not need the
        // softmax/exp/log work used by stochastic training or its sampled probability records.
        for (int i = 1; i < set.n; ++i) if (lg[i] > lg[pick.index]) pick.index = i;
        pick.logp = 0.0f;
    }
    else pick = net::choose(lg, set.n, config(0), s.rng);
    prof::mark(44);
    if (set.head == h_goal && config(5) <= 0.0f)  // a hint for the legacy tail head
    {
        float mx = lg[0], z = 0.0f, r = 0.0f;
        for (int k = 1; k < set.n; ++k) mx = std::max(mx, lg[k]);
        for (int k = 0; k < set.n; ++k)
        {
            const float e = std::exp(lg[k] - mx);
            z += e;
            if (set.c[k].kind == g_recall) r += e;
        }
        s.mem.goal_recall_p = r / z;
        s.mem.goal_decided = o.round;
    }
    prof::mark(45);
    log_decision(o, sv, set, pick.index, pick.logp);
    prof::mark(46);
    // TEMP decision trace (config 4): one line per decision of roster 0 on a few rounds.
    if (config(4) > 0.0f && o.self.roster == 0 && (o.round % 50 == 1 || o.round % 50 == 2))
        std::printf("dec r%d head %d n %d pick %d kind %d target %d at %d,%d self %d,%d goal %d/%d lg %.2f/%.2f\n", o.round, set.head, set.n,
                    pick.index, set.c[pick.index].kind, set.c[pick.index].target, set.c[pick.index].at.x, set.c[pick.index].at.y,
                    o.self.at.x, o.self.at.y, s.mem.goal_kind, s.mem.goal_target, lg[0], lg[pick.index]);
    if (config(4) > 1.0f && o.self.roster == 0 && (o.round % 50 == 1 || o.round % 50 == 2))
        for (int k = 0; k < set.n && k < 12; ++k)
            std::printf("  c%d kind %d lg %.3f phi0-3 %.2f %.2f %.2f %.2f sv27 %.2f\n", k, set.c[k].kind, lg[k], set.c[k].phi[0], set.c[k].phi[1],
                        set.c[k].phi[2], set.c[k].phi[3], sv[27]);
    return pick.index;
}

// Option layer: a head's projection of the turn's trunk (U_h t, V_h t) is computed once per turn and reused by
// every later decision of that head (shop purchases, the re-attack). `generation` changes with every new trunk.
struct Projections
{
    static inline unsigned generation{};
    static inline unsigned made[h_count]{};
    static inline net::HeadContext head[h_count];
    static const net::HeadContext& of(const net::Trunk& t, Head h)
    {
        if (made[h] != generation)
        {
            net::project_fast(weights(), t, h, head[h]);
            made[h] = generation;
        }
        return head[h];
    }
};

int decide(const Obs& o, const float* sv, const net::Trunk& t, const CandSet& set,
           const net::HeadContext* cached = nullptr)
{
    if (set.n <= 0) return -1;
    if (set.n == 1) return 0;
    float lg[kMaxCands];
    if (!cached && goal_state_facts && config(5) <= 0.0f && Projections::generation != 0) cached = &Projections::of(t, set.head);
    if (config(5) > 0.0f || goal_state_facts)  // the option layer uses the exact SIMD kernels too
    {
        if (cached) net::logits_cached(weights(), *cached, set, lg);
        else net::logits_fast(weights(), t, set, lg);
    }
    else net::logits(weights(), t, set, lg);
    prof::mark(48 + set.head);
    const int r = choose_decision(o, sv, set, lg);
    prof::mark(43);
    return r;
}

bool goal_over(const Obs& o, const Memory& m)
{
    if (m.goal_kind < 0) return true;
    switch (m.goal_kind)
    {
    case g_farm:
        return m.goal_target < 0 || (m.goal_target < static_cast<int>(o.group_status.size()) &&
                                     o.group_status[static_cast<std::size_t>(m.goal_target)] == gs_respawning);
    case g_bank: return o.self.gems == 0 && !o.self.tributing;
    case g_recall: return o.on_spawn;
    case g_engage:
        for (const auto& e : o.enemies) if (e.id == m.goal_target) return false;
        return true;
    default: return false;
    }
}

}  // namespace

// Fuel accounting (measurement): the worst fuel per phase over each 100 rounds, printed by every champion.
// Learned fuel budgets per phase (engineering bound): each phase runs only when the fuel left covers the worst
// cost it has had in this game x 1.15 + 5K. A phase that ran out of fuel counts as costing everything it had.
// Fuel gates (engineering): each decision runs only when the fuel left covers the dearest run of it so far (with
// 15% + 8K margin: logging and the end of the turn need a few K). A run cut by suspension resumed with a fresh 100K turn: its cost is what it had plus what it used
// after resuming.
// Option layer (config[10]): the bound is the dearest of the last kRuns runs of that decision, not the dearest
// ever, so one expensive run (a first path read, a diagnostic print) stops counting after kRuns further runs.
// A refused decision keeps its bound; the turn order guarantees it room on a later turn (see play_body).
struct Gate
{
    enum { shop, goal, cast, ranged, attack, step, reattack, tail, n };
    // The Big World zero-policy gate measured 37,234 in the first cast phase; admit the first run
    // with room for that complete caller instead of learning its cost by exhausting a turn.
    static inline std::uint64_t worst[n]{ 10000, 35000, 45000, 40000, 10000, 40000, 10000, 8000 };
    static constexpr int kRuns = 8;
    static inline std::uint64_t runs[n][kRuns]{};
    static inline int next[n]{};
    static inline bool recent{};  // config[10]: recent-run bounds
    static std::uint64_t bound(int k)
    {
        if (!recent) return worst[k];
        std::uint64_t b = 0;
        for (const auto c : runs[k]) b = c > b ? c : b;
        return b;
    }
    // What the decision typically costs: the median of its recent runs (the room later decisions are given).
    static std::uint64_t typical(int k)
    {
        std::uint64_t v[kRuns];
        for (int i = 0; i < kRuns; ++i) v[i] = runs[k][i];
        std::sort(v, v + kRuns);
        return v[kRuns / 2];
    }
    static void seed()
    {
        // First runs: the measured option-layer costs (25 Oct, 00corners/01big_world) with room to spare.
        static constexpr std::uint64_t first[n]{ 8000, 30000, 15000, 25000, 8000, 20000, 8000, 8000 };
        for (int k = 0; k < n; ++k) for (auto& c : runs[k]) c = first[k];
    }
    static void ran(int k, std::uint64_t cost)
    {
        if (cost > worst[k]) worst[k] = cost;
        runs[k][next[k]] = cost;
        next[k] = (next[k] + 1) % kRuns;
    }
    // Margins: legacy 15% + 8K; option layer 10% + 6K over its recent-run bound (logging and the turn's end need a few K).
    // A decision refused on kPatience turns in a row forgets its dearest recent run (a suspended run counts as
    // everything it had): one outlier can delay a decision, never lock it out.
    static constexpr int kPatience = 4;
    static inline int refused[n]{};
    static inline std::uint32_t admitted_count[n]{}, refused_count[n]{};
    static bool ok(int k, std::uint64_t after = 0)
    {
        const auto need = recent ? bound(k) * 110 / 100 + 6000 : bound(k) * 115 / 100 + 8000;
        const bool fits = cambc::fuel_remaining() > need + after;
        ++(fits ? admitted_count : refused_count)[k];
        if (!recent) return fits;
        if (fits) refused[k] = 0;
        else if (++refused[k] >= kPatience)
        {
            refused[k] = 0;
            int top = 0;
            for (int i = 1; i < kRuns; ++i) if (runs[k][i] > runs[k][top]) top = i;
            std::uint64_t second = 0;
            for (int i = 0; i < kRuns; ++i) if (i != top && runs[k][i] > second) second = runs[k][i];
            runs[k][top] = std::max(second, runs[k][top] - runs[k][top] / 4);  // an unrun seed decays too
        }
        return fits;
    }
};

struct Measure
{
    int k;
    bool discard{};  // an abandoned run is not a measurement of the decision
    std::uint64_t before{ cambc::fuel_remaining() };
    int round{ static_cast<int>(cambc::round_number()) };
    explicit Measure(int phase) : k(phase) {}
    ~Measure()
    {
        const auto now = cambc::fuel_remaining();
        const bool suspended = static_cast<int>(cambc::round_number()) != round;
        const std::uint64_t cost = suspended ? before + (now < 100000 ? 100000 - now : 0) : (now <= before ? before - now : 0);
        if (!discard || suspended) Gate::ran(k, cost);
        if (suspended) std::printf("suspended in phase %d r%d cost %llu\n", k, round, static_cast<unsigned long long>(cost));
    }
};

struct Meter
{
    enum { obs, trunk, shop, goal, cast, act, n };
    std::uint64_t start{ cambc::fuel_remaining() }, last{ start };
    int round{ -1 };
    int start_round{ static_cast<int>(cambc::round_number()) };
    bool reported{};
    static inline std::uint64_t worst[n]{}, worst_total{};
    static inline int heavy_reports{};
    std::uint64_t turn[n]{};
    void mark(int i)
    {
        const auto now = cambc::fuel_remaining();
        if (!reported && static_cast<int>(cambc::round_number()) != start_round)
        {
            reported = true;
            std::printf("suspended before mark %d r%d\n", i, start_round);
        }
        turn[i] = last - now;
        if (last - now > worst[i]) worst[i] = last - now;
        last = now;
        if (config(4) > 0.0f && round <= 6) std::printf("mark r%d p%d left %llu\n", round, i, static_cast<unsigned long long>(now));
    }
    ~Meter()
    {
        const auto used = start - cambc::fuel_remaining();
        if (used > worst_total) worst_total = used;
        if (used > 85000 && used < 1000000 && heavy_reports < 20 && cambc::fuel_remaining() > 10000)
        {
            ++heavy_reports;
            std::printf("heavy r%d used %llu phases %llu %llu %llu %llu %llu %llu\n", round, static_cast<unsigned long long>(used),
                        static_cast<unsigned long long>(turn[obs]), static_cast<unsigned long long>(turn[trunk]), static_cast<unsigned long long>(turn[shop]),
                        static_cast<unsigned long long>(turn[goal]), static_cast<unsigned long long>(turn[cast]), static_cast<unsigned long long>(turn[act]));
        }
        if (round > 0 && (round % 100 == 0 || round <= 2) && cambc::fuel_remaining() > 10000)  // reports only with fuel to spare
        {
            std::printf("fuel r%d max %llu obs %llu trunk %llu shop %llu goal %llu cast %llu act %llu\n", round,
                        static_cast<unsigned long long>(worst_total), static_cast<unsigned long long>(worst[obs]),
                        static_cast<unsigned long long>(worst[trunk]), static_cast<unsigned long long>(worst[shop]),
                        static_cast<unsigned long long>(worst[goal]), static_cast<unsigned long long>(worst[cast]),
                        static_cast<unsigned long long>(worst[act]));
            worst_total = 0;
            for (auto& w : worst) w = 0;
        }
    }
};

static std::uint64_t engine_fuel() { return cambc::fuel_remaining(); }

// Primitive command scheduling is learned. These are engineering admission bounds only: a command is
// considered only when its complete selected-head/SDK caller fits. No unselected argument head runs.
struct CommandFuel
{
    static inline std::uint64_t root = 18000, refresh = 40000, embed = 14000;
    static inline std::uint64_t argument[a_count]{
        0, 40000, 18000, 45000, 45000, 20000, 4000, 5000, 4000, 60000,
        16000, 28000, 28000, 16000, 10000
    };
    static inline std::uint32_t learned_end{}, rule_end{}, forced_end{}, attempted{}, rejected{}, replans{};
    static void measured(std::uint64_t before, std::uint64_t& bound, int round)
    {
        const auto after = cambc::fuel_remaining();
        if (static_cast<int>(cambc::round_number()) != round)
        {
            std::printf("error command caller suspended r%d\n", round);
            bound = std::max(bound, before + (after < 100000 ? 100000 - after : 0));
        }
        else if (before >= after) bound = std::max(bound, before - after);
    }
    static std::uint64_t admitted(std::uint64_t bound) { return bound * 5u / 4u; }
};

static bool select_command_argument(Context& ctx, ActionType type, const float* sv,
                                    const net::Trunk& trunk, CandSet& arguments, Cand& command)
{
    auto& o = state().obs;
    ActionBuild built;
    if (type == a_replan)
    {
        GoalPlanSession session(ctx);
        net::HeadContext projection;
        bool projected = false;
        for (int level = 0; level < 16 && session.stage() != GoalStage::done; ++level)
        {
            const auto choices = session.candidates(arguments);
            if (!choices.usable() || arguments.n == 0)
            {
                std::printf("error goal traversal unavailable (%d)\n", static_cast<int>(choices.status));
                return false;
            }
            if (arguments.n > 1 && !projected)
            {
                net::project_fast(weights(), trunk, h_goal, projection);
                projected = true;
            }
            const int chosen = decide(o, sv, trunk, arguments, projected ? &projection : nullptr);
            if (chosen < 0 || !session.select(arguments.c[chosen]))
            {
                std::printf("error goal traversal selection mismatch\n");
                return false;
            }
        }
        if (session.stage() != GoalStage::done)
        {
            std::printf("error goal traversal exceeded its engineering depth\n");
            return false;
        }
        command = Cand{};
        command.kind = static_cast<std::uint8_t>(session.kind());
        command.target = session.target();
        return true;
    }
    if (spatial_action(ctx, type))
    {
        static std::vector<SpatialTile> legal;
        net::HeadContext projection;
        bool projected = false;
        auto spatial_choice = [&] {
            if (arguments.n > 1 && !projected)
            {
                net::project_fast(weights(), trunk, arguments.head, projection);
                projected = true;
            }
            return decide(o, sv, trunk, arguments, projected ? &projection : nullptr);
        };
        built = spatial_argument_tiles(ctx, type, legal);
        if (built.usable()) built = spatial_argument_candidates(ctx, type, legal, SpatialStage::column, {}, arguments);
        if (!built.usable() || arguments.n == 0)
        {
            std::printf("error command %s column unavailable (%d)\n", action_name(type), static_cast<int>(built.status));
            return false;
        }
        int chosen = spatial_choice();
        if (chosen < 0) return false;
        const SpatialColumn column{ arguments.c[chosen].target, arguments.c[chosen].at.x };
        built = spatial_argument_candidates(ctx, type, legal, SpatialStage::row, column, arguments);
        if (!built.usable() || arguments.n == 0)
        {
            std::printf("error command %s row unavailable (%d)\n", action_name(type), static_cast<int>(built.status));
            return false;
        }
        chosen = spatial_choice();
        if (chosen < 0) return false;
        command = arguments.c[chosen];
        return true;
    }
    built = argument_candidates(ctx, type, arguments);
    if (!built.usable() || arguments.n == 0)
    {
        std::printf("error command %s unavailable after controller selection (%d)\n", action_name(type), static_cast<int>(built.status));
        return false;
    }
    const int chosen = type == a_priority ? 0 : decide(o, sv, trunk, arguments);
    if (chosen < 0) return false;
    command = arguments.c[chosen];
    if (type == a_priority || type == a_buy)
    {
        if (type == a_buy)
        {
            built = quantity_argument_candidates(ctx, command, arguments);
            if (!built.usable() || arguments.n != 1)
            {
                std::printf("error quantity unavailable after item selection\n");
                return false;
            }
        }
        float scalar{};
        net::logits_fast(weights(), trunk, arguments, &scalar);
        if (!std::isfinite(scalar)) { std::printf("error scalar argument is not finite\n"); return false; }
        if (type == a_priority) command.target = decode_priority(scalar);
        else command.count = decode_quantity(scalar, max_affordable_quantity(ctx, command.target));
    }
    return true;
}

static void play_commands(Context& ctx, float* sv, net::Trunk& trunk)
{
    auto& s = state();
    auto& o = s.obs;
    bool replanned = false;
    bool ended = false;
    // An engineering cap, not a command plan. All repeated legal engine commands remain representable.
    constexpr int kMaxCommands = 32;
    constexpr std::uint64_t cleanup = 8000;
    CandSet families, arguments;
    for (int micro = 0; micro < kMaxCommands; ++micro)
    {
        const auto left = cambc::fuel_remaining();
        if (left <= CommandFuel::admitted(CommandFuel::root) + cleanup) break;
        auto before = cambc::fuel_remaining();
        const int round = o.round;
        const auto internal = replanned ? kAllActions & ~action_bit(a_replan) : kAllActions;
        action_candidates(ctx, families, internal);
        if (families.n <= 1)
        {
            CommandFuel::measured(before, CommandFuel::root, round);
            if (families.n == 1 && families.c[0].kind == a_end)
            {
                ++CommandFuel::rule_end;
                ended = true;
            }
            break;
        }
        float family_logits[kMaxCands];
        net::logits_fast(weights(), trunk, families, family_logits);
        // The root has now been paid for. Admit arguments against actual remaining fuel, retaining
        // their complete-caller margins and cleanup reserve instead of reserving the root twice.
        const auto after_score = cambc::fuel_remaining();
        const std::uint64_t choice_reserve = config(0) > 0.0f || config(1) > 0.0f ? 6000u : 1000u;
        int kept = 0;
        for (int i = 0; i < families.n; ++i)
        {
            const auto kind = static_cast<ActionType>(families.c[i].kind);
            if (kind != a_end && after_score <= CommandFuel::admitted(CommandFuel::argument[kind]) + cleanup + choice_reserve) continue;
            families.c[kept] = families.c[i];
            family_logits[kept++] = family_logits[i];
        }
        families.n = kept;
        if (families.n <= 1)
        {
            CommandFuel::measured(before, CommandFuel::root, round);
            break;  // a fuel-masked END is not a learned END label
        }
        const int chosen = choose_decision(o, sv, families, family_logits);
        CommandFuel::measured(before, CommandFuel::root, round);
        if (chosen < 0) break;
        const auto type = static_cast<ActionType>(families.c[chosen].kind);
        if (type == a_end)
        {
            ++CommandFuel::learned_end;
            ended = true;
            break;
        }
        before = cambc::fuel_remaining();
        Cand command;
        if (!select_command_argument(ctx, type, sv, trunk, arguments, command)) break;
        if (type == a_replan)
        {
            if (command.kind != s.mem.goal_kind || command.target != s.mem.goal_target) s.mem.goal_since = o.round;
            s.mem.goal_kind = command.kind;
            s.mem.goal_target = command.target;
            s.mem.goal_decided = o.round;
            sdk::publish_claim(command.kind, command.target);
            replanned = true;
            ++CommandFuel::replans;
        }
        else
        {
            ++CommandFuel::attempted;
            const auto accepted = sdk::primitive_result(type, o, command);
            if (accepted == sdk::PrimitiveResult::unexpected_error) break;
            if (accepted == sdk::PrimitiveResult::rejected) ++CommandFuel::rejected;
        }
        CommandFuel::measured(before, CommandFuel::argument[type], round);
        ++o.command_index;
        // Ending now requires no observation. Refresh only when another learned decision can fit;
        // otherwise yield rather than observe a state that cannot be acted upon this owner turn.
        const auto refresh_bound = type == a_replan ? 0u : CommandFuel::refresh;
        if (cambc::fuel_remaining() <= CommandFuel::admitted(refresh_bound + CommandFuel::embed + CommandFuel::root) + cleanup) break;
        if (type != a_replan)
        {
            before = cambc::fuel_remaining();
            sdk::refresh_after_primitive(s.map, o, type, command);
            observe(o, s.map, s.mem);
            CommandFuel::measured(before, CommandFuel::refresh, round);
        }
        before = cambc::fuel_remaining();
        state_vector(ctx, sv);
        net::trunk_fast(weights(), sv, trunk);
        CommandFuel::measured(before, CommandFuel::embed, round);
    }
    if (!ended) ++CommandFuel::forced_end;
    if (config(4) > 0.0f && o.self.roster == 0 && o.round % 100 == 0 && cambc::fuel_remaining() > cleanup)
        std::printf("commands r%d learned_end%u rule_end%u forced_end%u attempted%u rejected%u replans%u root%llu refresh%llu\n",
                    o.round, CommandFuel::learned_end, CommandFuel::rule_end, CommandFuel::forced_end, CommandFuel::attempted,
                    CommandFuel::rejected, CommandFuel::replans, static_cast<unsigned long long>(CommandFuel::root),
                    static_cast<unsigned long long>(CommandFuel::refresh));
}

struct StudentRuntime
{
    student::FuelBounds arguments;
    // New-graph bounds are populated only from its complete-caller measurements. Unknown families
    // remain unavailable; zero here is never an assertion that an unmeasured caller is free.
    std::uint64_t root_bound{20000};
    std::uint64_t dispatch[a_count]{};
    std::uint32_t dispatch_known{};
    std::uint64_t refresh[a_count]{};
    std::uint32_t refresh_known{};
    std::uint32_t learned_end{}, rule_end{}, forced_end{}, attempted{}, rejected{}, replans{};
};

static StudentRuntime& student_runtime()
{
    static StudentRuntime runtime = [] {
        StudentRuntime result;
        if (config(7) > 0.0f)
        {
            // Measured CurrentFeature2/cfg7 dense P9->MOVE and source1061 ATTACK fixtures. These are engineering costs,
            // never learned policy coefficients. Unmeasured families stay unavailable.
            result.root_bound = 15239;
            if (config(8) > 0.0f) {
                result.root_bound = 47833;  // measured complete attention root, 29 groups
                // Cached roots: source1061=36803, actual30-group455=41199. Seed from that maximum.
                // The unchanged 1.25 admission margin and 8K caller reserve apply afterward.
                if (config(9) > 0.0f) result.root_bound = 42000;
            }
            result.arguments.argument[a_priority] = 3616;
            result.arguments.argument[a_move] = 10514;
            result.arguments.argument[a_attack] = 5375;
            result.arguments.known_mask = action_bit(a_priority) | action_bit(a_move) | action_bit(a_attack);
            result.dispatch[a_priority] = 113;
            result.dispatch[a_move] = 269;
            result.dispatch[a_attack] = 229;
            result.dispatch_known = result.arguments.known_mask;
            result.refresh[a_priority] = 1242;
            result.refresh[a_attack] = 14046;
            result.refresh_known = action_bit(a_priority) | action_bit(a_attack);
        }
        return result;
    }();
    return runtime;
}

static void play_student(Context& ctx)
{
    auto& s = state();
    auto& runtime = student_runtime();
#if defined(ZERO_STUDENT_PROBE)
    static bool installed_profile = false;
    if (!installed_profile)
    {
        student::probe::install_profile(runtime.arguments, runtime.root_bound, runtime.dispatch,
            runtime.dispatch_known, runtime.refresh, runtime.refresh_known);
        installed_profile = true;
    }
#endif
    bool ended = false, replanned = false;
    constexpr std::uint64_t cleanup = 8000;
    constexpr int kMaxCommands = 32;
    for (int command_index = 0; command_index < kMaxCommands; ++command_index)
    {
        if (cambc::fuel_remaining() <= CommandFuel::admitted(runtime.root_bound) + cleanup) break;
        student::DecisionOptions options;
        options.remaining_fuel = engine_fuel;
        options.rng = &s.rng;
        options.temperature = config(0);
        options.camp_attention = config(8) > 0.0f;
        if (options.camp_attention && config(9) > 0.0f) options.camp_context_cache = &s.camp_context_cache;
        options.cleanup_reserve = cleanup;
        options.allowed_mask = (replanned ? kAllActions & ~action_bit(a_replan) : kAllActions)
            & (runtime.dispatch_known | action_bit(a_end));
        // First learning prototype is explicitly unconditional. It retains every engine command;
        // internal goal selection is outside this model profile, so conditioning stays known-none.
        if (config(7) > 0.0f) options.allowed_mask &= ~action_bit(a_replan);
        const auto round = s.obs.round;
        const student::TeacherSource* teacher = nullptr;
#if defined(ZERO_STUDENT_PROBE)
        student::probe::configure_options(ctx, options);
        if (options.diagnostic_bypass_admission)
        {
            std::printf("error lifecycle probe requested admission bypass\n");
            break;
        }
        teacher = student::probe::source(ctx);
#endif
        const auto decision = student::decide(ctx, weights(), options, runtime.arguments, teacher);
#if defined(ZERO_STUDENT_PROBE)
        student::probe::decision(ctx, decision);
#endif
        if (static_cast<int>(cambc::round_number()) != round)
        {
            std::printf("error student decision suspended r%d\n", round);
            break;
        }
        const auto root_cost = decision.fuel.total - std::min(decision.fuel.total, decision.fuel.argument + decision.fuel.trace);
        runtime.root_bound = std::max(runtime.root_bound, root_cost);
        if (decision.status == student::DecisionStatus::learned_end)
        {
            ++runtime.learned_end; ended = true; break;
        }
        if (decision.status == student::DecisionStatus::rule_end)
        {
            ++runtime.rule_end; ended = true; break;
        }
        if (decision.status == student::DecisionStatus::fuel_end) break;
        if (decision.status != student::DecisionStatus::command)
        {
            std::printf("error student decision status %d\n", static_cast<int>(decision.status));
            break;
        }
        // Argument work can grow beyond its previous bound. Its newly measured cost does not buy
        // a free SDK dispatch: preserve a separate dispatch bound and recheck actual remaining fuel.
        runtime.arguments.measured(decision.type, decision.fuel.argument + runtime.dispatch[decision.type]);
        if (!(runtime.dispatch_known & action_bit(decision.type)) ||
            cambc::fuel_remaining() <= CommandFuel::admitted(runtime.dispatch[decision.type]) + cleanup) break;
        const auto before_dispatch = cambc::fuel_remaining();
        bool unexpected = false;
        [[maybe_unused]] int receipt_code = -1;  // internal goal commit has no SDK receipt
        if (decision.type == a_replan)
        {
            student::set_goal(s.mem, decision.command, round);
            // Student schema1 has no shared-claim input or producer protocol; do not publish unused data.
            replanned = true;
            ++runtime.replans;
        }
        else
        {
            ++runtime.attempted;
            const auto receipt = sdk::primitive_result(decision.type, s.obs, decision.command);
            receipt_code = static_cast<int>(receipt);
            if (receipt == sdk::PrimitiveResult::rejected) ++runtime.rejected;
            unexpected = receipt == sdk::PrimitiveResult::unexpected_error;
        }
        student::record_command(s.obs);
        if (static_cast<int>(cambc::round_number()) != round)
        {
            std::printf("error student dispatch suspended r%d\n", round);
            break;
        }
        const auto after_dispatch = cambc::fuel_remaining();
        if (before_dispatch >= after_dispatch)
        {
            runtime.dispatch[decision.type] = std::max(runtime.dispatch[decision.type], before_dispatch - after_dispatch);
            runtime.arguments.measured(decision.type, decision.fuel.argument + runtime.dispatch[decision.type]);
        }
#if defined(ZERO_STUDENT_PROBE)
        student::probe::dispatched(ctx, decision.type, decision.command, receipt_code, before_dispatch, after_dispatch);
#endif
        if (unexpected) break;
        if (decision.type != a_replan)
        {
            if (!(runtime.refresh_known & action_bit(decision.type))) break;
            if (cambc::fuel_remaining() <= CommandFuel::admitted(runtime.refresh[decision.type] + runtime.root_bound) + cleanup) break;
            const auto before_refresh = cambc::fuel_remaining();
            sdk::refresh_after_primitive(s.map, s.obs, decision.type, decision.command);
            observe(s.obs, s.map, s.mem);
            if (static_cast<int>(cambc::round_number()) != round)
            {
                std::printf("error student refresh suspended r%d\n", round);
                break;
            }
            const auto after_refresh = cambc::fuel_remaining();
            if (before_refresh >= after_refresh)
                runtime.refresh[decision.type] = std::max(runtime.refresh[decision.type], before_refresh - after_refresh);
        }
    }
    if (!ended) ++runtime.forced_end;
    if (config(4) > 0.0f && s.obs.self.roster == 0 && s.obs.round % 100 == 0 && cambc::fuel_remaining() > cleanup)
        std::printf("student r%d learned_end%u rule_end%u forced_end%u attempted%u rejected%u replans%u\n",
                    s.obs.round, runtime.learned_end, runtime.rule_end, runtime.forced_end,
                    runtime.attempted, runtime.rejected, runtime.replans);
}

static bool g_report_due = false;

static void play_body()
{
    zero::fuel_hook = engine_fuel;
    sdk::set_explicit_channel_mode(config(5) > 0.0f);
    goal_state_facts = config(10) > 0.0f && config(5) <= 0.0f;
    if (goal_state_facts && !Gate::recent) { Gate::recent = true; Gate::seed(); }
    Meter meter;
    auto& s = state();
    // Init in stages: terrain, camp groups, Map::finish, the path cache, the first observation, the spawn field. A turn runs its
    // first pending stage, then further ones only while more than 110K is left (the 200K first turn); every stage
    // fits a 100K turn. Decisions start the turn after the last stage.
    if (s.init_stage < 6)
    {
        for (int ran = 0; s.init_stage < 6 && (ran == 0 || cambc::fuel_remaining() > 110000); ++ran)
        {
            const auto before = cambc::fuel_remaining();
            switch (s.init_stage)
            {
            case 0:
                sdk::load_terrain(s.map);
                s.rng = net::mix(static_cast<std::uint64_t>(config(2) * 1e6f) ^
                                 (static_cast<std::uint64_t>(static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(cambc::self()))) << 32));
                if (net::layout().total > kWeights) std::printf("error: network layout %d exceeds the weights block %d\n", net::layout().total, kWeights);
                break;
            case 1: sdk::load_groups(s.map); break;
            case 2: s.map.finish(); break;
            case 3: s.paths = new sdk::EnginePaths(s.map, static_cast<int>(cambc::detail::absolute_team(cambc::team::ally)) == 0 ? 0 : 1); break;
            case 4:
                sdk::observe(s.map, s.obs);
                observe(s.obs, s.map, s.mem);
                break;
            default:
                if (!(config(5) > 0.0f && config(6) > 0.0f)) s.paths->warm(5000);
                else if (config(8) > 0.0f && config(9) > 0.0f) s.camp_context_cache.prepare(s.map);
                break;  // student geometry has no obstacle-path preparation dependency
            }
#if defined(ZERO_STUDENT_PROBE)
            student::probe::init_stage(s.init_stage, before, cambc::fuel_remaining());
#endif
            if (config(4) > 0.0f) std::printf("init stage %d r%d cost %llu\n", s.init_stage, static_cast<int>(cambc::round_number()), static_cast<unsigned long long>(before - cambc::fuel_remaining()));
            ++s.init_stage;
        }
        return;
    }
    ++s.turns;
    prof::on = config(4) > 2.5f;  // the fuel profiler (its report alone costs most of a turn)
    if (g_report_due) { prof::report(s.obs.round); g_report_due = false; }  // diagnostics: printed with a full budget
    prof::begin();
    Obs& o = s.obs;
    if (s.paths) s.paths->begin_turn();
    sdk::observe(s.map, o);
    prof::mark(0);
    student::begin_commands(o);
    observe(o, s.map, s.mem);
    prof::mark(1);
    meter.round = o.round;
    meter.mark(Meter::obs);
    Context ctx{ &s.map, s.paths, &o, &s.mem };
    if (config(5) > 0.0f && config(6) > 0.0f)
    {
        play_student(ctx);
        meter.mark(Meter::act);
        return;
    }
    float sv[kState];
    prof::mark(29);
    state_vector(ctx, sv);
    prof::mark(2);
    net::Trunk t;
    if (config(5) > 0.0f || goal_state_facts) net::trunk_fast(weights(), sv, t);
    else net::trunk(weights(), sv, t);
    if (goal_state_facts && config(5) <= 0.0f) ++Projections::generation;  // this turn's trunk
    prof::mark(3);
    meter.mark(Meter::trunk);
    CandSet set;

    if (config(5) > 0.0f)
    {
        play_commands(ctx, sv, t);
        meter.mark(Meter::act);
        return;
    }

    // Option layer: room the turn's action decisions need after an optional one (cast, shop, an early goal).
    const bool recent = Gate::recent;
    const bool ranged = catalog().champion(o.self.type).attack_type == 1;
    const std::uint64_t actions = !recent ? 0 :
        (ranged ? Gate::typical(Gate::ranged) : Gate::typical(Gate::attack) + Gate::typical(Gate::step)) + Gate::typical(Gate::tail);
    // Shop: on spawn or while dead, until the head says stop (bounded).
    if (o.on_spawn || !o.alive)
    {
        int bought = 0;
        // Each purchase leaves room for the stat re-read (~10K) and a step decision after the shop.
        for (int k = 0; k < 12 && Gate::ok(Gate::shop, 10000 + (recent ? actions : Gate::worst[Gate::step])); ++k)
        {
            Measure m(Gate::shop);
            shop_candidates(ctx, set);
            if (set.n <= 1) break;  // nothing affordable
            const int i = decide(o, sv, t, set);
            if (i <= 0 || !sdk::act_buy(set.c[i])) break;
            ++bought;
            // Gold and items by the rules (no engine queries): the fifth shard of a stat becomes its emblem.
            const auto& item = catalog().item(set.c[i].target);
            o.self.gold -= std::min(o.self.gold, item.cost);
            auto& n = o.items[static_cast<std::size_t>(set.c[i].target)];
            ++n;
            if (item.combine_count > 0 && n >= item.combine_count && item.result >= 0)
            {
                n = static_cast<std::uint8_t>(n - item.combine_count);
                ++o.items[static_cast<std::size_t>(item.result)];
            }
        }
        if (bought > 0 && o.alive) sdk::refresh_self(s.map, o);  // stats grew with the purchases
    }
    prof::mark(4);
    meter.mark(Meter::shop);
    if (!o.alive || o.self.recalling || o.self.tributing) return;

    // Goal: at decision points.
    const int period = config(3) > 0.0f ? static_cast<int>(config(3)) : 4;
    // A decision point: the goal ended, the decision period passed, or an enemy came into view.
    static int seen_before = 0;
    const bool new_threat = !o.enemies.empty() && seen_before == 0;
    seen_before = static_cast<int>(o.enemies.size());
    // Option layer: a due decision the fuel did not admit stays due. config[11] > 0 (default off): the period counts
    // from the last decision (by default it counts from when the goal was set, so with the goal unchanged every turn
    // after the period is a decision point).
    static bool goal_pending = false;
    static int goal_decided_round = -1000;
    const bool goal_due = goal_over(o, s.mem) ||
                          (recent && config(11) > 0.0f ? o.round - goal_decided_round >= period : o.round - s.mem.goal_since >= period) ||
                          new_threat || goal_pending;
    // The turn acts on the current goal; a due decision runs after the actions with the fuel left (it steers the
    // next turn), except with no goal yet, when it runs first. `after` keeps room for what follows it. Option layer:
    // a decision deferred by fuel runs first on the next turn (before the casts, with the turn's fullest budget).
    auto decide_goal = [&](std::uint64_t after) {
        if (s.turns <= 1 || !goal_due) return;
        if (!Gate::ok(Gate::goal, after))
        {
            if (config(4) > 0.0f && o.self.roster == 0 && o.round % 50 == 3)
                std::printf("goal refused r%d left %llu bound %llu after %llu pending %d\n", o.round, static_cast<unsigned long long>(cambc::fuel_remaining()),
                            static_cast<unsigned long long>(Gate::bound(Gate::goal)), static_cast<unsigned long long>(after), goal_pending ? 1 : 0);
            goal_pending = recent;
            return;
        }
        goal_pending = false;
        goal_decided_round = o.round;
        Measure m(Gate::goal);
        prof::mark(29);
        const auto g0 = cambc::fuel_remaining();
        sdk::read_claims(o);
        prof::mark(5);
        const auto g1 = cambc::fuel_remaining();
        // Option layer: the candidate work stops (no candidates) if it would leave too little for the scoring
        // (projection ~4.5K + ~0.6K for each of up to 16 candidates) and what follows; the decision then stays due.
        if (recent) goal_fuel_floor = after + 2000 + 4500 + 600 * 16;
        goal_candidates(ctx, set);
        goal_fuel_floor = 0;
        if (recent && set.n == 0)
        {
            goal_pending = true;
            m.discard = true;
            return;
        }
        prof::mark(6);
        const auto g2 = cambc::fuel_remaining();
        const int i = decide(o, sv, t, set);
        prof::mark(7);
        if (config(4) > 0.0f && g0 - cambc::fuel_remaining() > 30000 && cambc::fuel_remaining() > 8000)
            std::printf("goalheavy r%d c%d claims %llu cands %llu decide %llu n %d enemies %zu allies %zu camps %zu gems %u at %d,%d\n", o.round,
                        o.self.roster, (unsigned long long)(g0 - g1), (unsigned long long)(g1 - g2), (unsigned long long)(g2 - cambc::fuel_remaining()),
                        set.n, o.enemies.size(), o.allies.size(), o.camps.size(), o.self.gems, o.self.at.x, o.self.at.y);
        if (i >= 0)
        {
            const auto& c = set.c[i];
            if (c.kind != s.mem.goal_kind || c.target != s.mem.goal_target) s.mem.goal_since = o.round;
            s.mem.goal_kind = c.kind;
            s.mem.goal_target = c.target;
            sdk::publish_claim(c.kind, c.target);
        }
        prof::mark(8);
    };
    const bool goal_first = s.mem.goal_kind < 0 || (recent && goal_pending);
    if (goal_first) decide_goal(s.mem.goal_kind < 0 ? 0 : actions);

    meter.mark(Meter::goal);
    // Casts: one successful cast per turn across both slots.
    for (int slot = 0; slot < 2 && s.turns > 1 && o.can_cast && Gate::ok(Gate::cast, actions); ++slot)
    {
        Measure m(Gate::cast);
        prof::mark(29);
        cast_candidates(ctx, slot, set);
        prof::mark(9);
        if (set.n <= 1) continue;
        const int i = decide(o, sv, t, set);
        prof::mark(10);
        if (i > 0 && sdk::act_cast(o, slot, set.c[i]))
        {
            prof::mark(11);
            sdk::refresh_self(s.map, o);
            prof::mark(12);
            break;
        }
    }

    meter.mark(Meter::cast);
    // Step and attack.
    if (ranged && Gate::ok(Gate::ranged))
    {
        Measure m(Gate::ranged);
        prof::mark(29);
        // The same single candidate set and scoring head are used by imitation and runtime arbitration.
        ranged_candidates(ctx, set);
        prof::mark(13);
        const int i = decide(o, sv, t, set);
        prof::mark(14);
        if (i >= 0)
        {
            Cand c = set.c[i];
            if (c.kind < 4) sdk::act_step(o, c);
            else
            {
                c.kind = static_cast<std::uint8_t>(c.kind - 3);
                sdk::act_attack(o, c);
            }
        }
        prof::mark(15);
    }
    else if (!ranged)
    {
        // Melee: attack, step, then attack again from the new tile (each decision only with fuel for it).
        int i = -1;
        bool attacked = false;
        if (Gate::ok(Gate::attack))
        {
            Measure m(Gate::attack);
            prof::mark(29);
            attack_candidates(ctx, set);
            prof::mark(16);
            i = decide(o, sv, t, set);
            prof::mark(17);
            attacked = i > 0 && sdk::act_attack(o, set.c[i]);
            prof::mark(18);
        }
        if (attacked) o.can_attack = false;  // by the rules: the position and moves are unchanged
        if (Gate::ok(Gate::step))
        {
            Measure m(Gate::step);
            prof::mark(29);
            step_candidates(ctx, set);
            prof::mark(19);
            i = decide(o, sv, t, set);
            prof::mark(20);
            const bool stepped = i >= 0 && sdk::act_step(o, set.c[i]);
            prof::mark(21);
            if (stepped && !attacked && set.c[i].kind == 0 && Gate::ok(Gate::reattack))
            {
                Measure m2(Gate::reattack);
                o.self.at = set.c[i].at;  // by the rules (a re-read costs ~10K)
                o.can_move = false;
                attack_candidates(ctx, set);
                i = decide(o, sv, t, set);
                if (i > 0) sdk::act_attack(o, set.c[i]);
                prof::mark(22);
            }
        }
    }
    if (!goal_first || (recent && goal_pending)) decide_goal(recent ? Gate::typical(Gate::tail) : Gate::bound(Gate::tail));
    // End of turn: the tail head may start a recall the next turn keeps or cancels (no cost either way).
    if (Gate::ok(Gate::tail))
    {
        Measure m(Gate::tail);
        prof::mark(29);
        const bool home = sdk::home_now();
        prof::mark(23);
        tail_candidates(ctx, home, o.self.recalling || o.self.tributing, set);
        prof::mark(24);
        const int i = decide(o, sv, t, set);
        prof::mark(25);
        sdk::end_turn_recall(home, i > 0);
        prof::mark(26);
    }
    prof::mark(29);
    meter.mark(Meter::act);
}

void Bot::play_turn()
{
    const bool init = state().init_stage < 6;  // init turns end with the stage they ran
    play_body();
    prof::mark(29);
    const int r1 = static_cast<int>(cambc::round_number());
    sdk::settle_recall();
    prof::mark(27);  // a standing recall the turn did not decide about is cancelled
    if (static_cast<int>(cambc::round_number()) != r1) std::printf("suspended in settle r%d\n", r1);
    if (init) return;
    // Spare fuel builds the path fields decisions asked for (they used straight-line distances meanwhile).
    const int r0 = static_cast<int>(cambc::round_number());
    const auto f0 = cambc::fuel_remaining();
    if (!(config(5) > 0.0f && config(6) > 0.0f))
        if (auto* paths = state().paths) paths->warm(30000);
    prof::mark(28);
    g_report_due |= prof::on && state().obs.self.roster == 0 && state().obs.round % 100 == 0;
    if (config(4) > 0.0f && state().obs.round % 100 == 0 && cambc::fuel_remaining() > 8000)
    {
        std::printf("gates r%d c%d type %d fields %d", state().obs.round, state().obs.self.roster, state().obs.self.type, state().paths ? state().paths->fields_built() : -1);
        for (int k = 0; k < Gate::n; ++k) std::printf(" %u/%u/%llu", Gate::admitted_count[k], Gate::refused_count[k], static_cast<unsigned long long>(Gate::bound(k)));
        std::printf("\n");
    }
    if (static_cast<int>(cambc::round_number()) != r0) std::printf("suspended in warm r%d had %llu\n", r0, static_cast<unsigned long long>(f0));
}
