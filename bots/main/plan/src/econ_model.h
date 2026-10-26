#pragma once
// Economy surrogate for the team planner (cfg::team_plan): SDK-free data and arithmetic, so the same
// code runs in the bot (inputs from econ_sdk.cpp) and natively in the offline harness (inputs from an
// exported text file). Port of bots/main/tools/econ/model.mjs + sched.mjs (D331-D332), with clear
// forecasts in O(party) from per-group constants and calibration factors fitted to engine games.
//
// Mechanics assumed (GameRules / docs/rules-notes.md, all numbers read at runtime): a group wakes on
// the first hit and every awake monster strikes the nearest champion in range once per round; a
// cleared group is captured after capture_time rounds on a member tile and respawns after its timer;
// recall is recall_time turn ends and spawn heals at turn end; tribute takes max(1, ceil(gems x mult))
// ticks within submit range of an altar; shards add item points x the champion's growth and five
// matching shards combine into an emblem whose family passive applies once.
#include <cstdint>
#include <vector>

namespace cambc { class distance_field; }

namespace mb::econ {

constexpr int kMaxChamps = 8;
constexpr int kMaxFamilies = 16;
constexpr std::uint16_t kFar = 0xFFFF;

// Ability kinds as in the SDK (ability_kind) and passive kinds as in passive_type.
enum : std::uint8_t { ab_none = 0, ab_reset = 1, ab_damage, ab_aoe, ab_cc, ab_heal, ab_dash, ab_shield };
enum : std::uint8_t { pa_none = 0, pa_bonus_gold = 1, pa_remote_shop, pa_gem_focus, pa_splash, pa_health_strength,
                      pa_kill_reset, pa_retaliation, pa_tribute_gold, pa_missing_health = 10 };
// Item family passives (SDK item_passive); 0 = stats only (power, bruiser).
enum : std::uint8_t { fp_none = 0, fp_monster = 1, fp_durability, fp_treasure, fp_jungle, fp_home };
// Item families in SDK order (ChampState::emblems bit = value = family index in Rules::fam).
enum class Emblem : std::uint8_t { monster, power, durability, treasure, jungle, home, bruiser };

struct Ability
{
    std::uint8_t kind{};
    float modifier{};
    float radius{};
    int cooldown{ 8 };
    int duration{};
    bool hits_camps{};
};

struct Design
{
    int type{ -1 };
    float hp{}, str{}, foc{};
    float ghp{}, gstr{}, gfoc{};
    int range{ 1 };
    bool melee{ true };
    float camp_mult{ 1.0f };
    Ability ab[2]{};
    std::uint8_t passive{};
    float passive_mag{};
};

// One shard family: points per shard (h, s, f per stat column), emblem points and passive.
struct Family
{
    float shard_pts[3][3]{};   // [stat][h,s,f] for the shard of stat (0 str, 1 foc, 2 hp)
    float emblem_pts[3][3]{};  // the emblem of five
    std::uint8_t passive{};
    float magnitude{};
    bool valid{};
};

struct Rules
{
    int capture_time{ 5 }, recall_time{ 5 }, respawn_time{ 60 };
    int winning_gems{ 1000 }, max_rounds{ 2000 };
    double submit_mult{ 0.1 };
    int submit_base{ 0 }, submit_min{ 1 }, submit_range{ 1 };
    float spawn_healing{ 1.0f };
    int shard_cost{ 30 };
    int n_families{};
    Family fam[kMaxFamilies]{};
    int fam_monster{ -1 }, fam_power{ -1 }, fam_durability{ -1 }, fam_treasure{ -1 }, fam_jungle{ -1 }, fam_home{ -1 }, fam_bruiser{ -1 };
    float marcella_gold_per_gem{ 2.0f };
};

struct Member
{
    int x{}, y{};
    float hp{}, str{};
    int range{};
    int type{};  // SDK camp_type: 0 wolf, 1 elk, 2 gem cluster, 3 gold cluster, 4 monolith, 5 golem
};

struct Group
{
    std::vector<Member> members;
    std::vector<int> tiles;  // member tiles (row-major)
    int cx{}, cy{};          // member nearest the centroid
    int x0{}, y0{}, x1{}, y1{};  // member bounding box
    int gold{}, gems{}, respawn{};
    float H{}, W{}, S{};     // total health, Smith-weighted health sum, total strength
    float nb{};              // mean living neighbours of the target over a Smith-order clear
    int max_range{};
    float max_str{};
    bool harmless{}, elk{}, big{};
};

// Static model of one game for one team: rules, roster designs, groups, altars and distance fields.
// Points: 0 = our spawn, 1..G = groups, G+1.. = altars. field(p)[tile] = walking rounds from tile to
// the point's roots (spawn tiles, group member tiles, altar-adjacent tiles), kFar if unknown.
struct StaticModel
{
    Rules rules;
    std::vector<Design> roster;  // roster order (Knowledge::allies)
    int width{}, height{};
    std::vector<std::uint8_t> blocked;   // walls, holes, altars, enemy spawn (native harness only)
    std::vector<std::uint8_t> blocked_bits;  // the same, packed row-major (LSB first); both builds
    std::vector<std::int16_t> camp_tile; // group index owning a tile or -1
    std::vector<Group> groups;
    std::vector<int> altars;             // altar tiles
    std::vector<int> spawn;              // our spawn tiles
    int gate_group{ -1 };                // altar reachable only through one group's tiles (not modelled: -1)
    // Fields per point, natively as row-major vectors (empty = not computed yet: Chebyshev
    // fallback x detour factor); in the bot as SDK fields read through `getter` when have[p].
    std::vector<std::vector<std::uint16_t>> fields;
    std::vector<std::uint8_t> have;
    int (*getter)(int p, int tile){};
    std::vector<int> rep;                // representative tile per point (where a party stands / spawn centre)
    // Point-to-point distances (rounds to stand in reach), computed on first use (kUnset until then).
    mutable std::vector<std::uint16_t> dist;
    int n_points{};
    float detour{ 1.25f };
    bool ready{};

    int G() const { return static_cast<int>(groups.size()); }
    int A() const { return static_cast<int>(altars.size()); }
    static constexpr std::uint16_t kUnset = 0xFFFE;
    int d(int a, int b) const
    {
        const std::uint16_t v = dist[static_cast<std::size_t>(a * n_points + b)];
        return v != kUnset ? v : dist_slow(a, b);
    }
    int dist_slow(int a, int b) const;
    bool is_blocked(int t) const { return blocked_bits[static_cast<std::size_t>(t >> 3)] >> (t & 7) & 1; }
    void pack_blocked();  // blocked -> blocked_bits
    int tile_to_point(int tile, int p) const;  // rounds from a tile to stand in reach of point p
    bool has_field(int p) const;
    int raw(int p, int tile) const;            // field value (kFar = unreached)
};

// Derived constants of a group (Smith order, effective neighbours) from its members.
void finish_group(Group& g);
// Point representatives and point-to-point distance matrix from whatever fields exist.
void finish_distances(StaticModel& m);
// Forget the distance column of point p after its field was computed (recomputed on use).
void update_column(StaticModel& m, int p);
// Bot only (econ_sdk.cpp): compute up to `max_fields` more point fields in a fixed order (spawn,
// groups, altars). Call with the same quota every turn on every instance so plans agree. Returns
// true when all fields exist.
bool prepare(int max_fields);
// Bot only: the SDK field of point p (0 spawn, 1..G groups, then altars) once prepare() made it.
const cambc::distance_field* point_field(int p);
// Native BFS fields for every point (harness; the bot uses SDK fields in econ_sdk.cpp).
void compute_fields_native(StaticModel& m);
// Walls for a point's field: blocked tiles plus camp tiles of dangerous groups other than `group`.
void field_walls(const StaticModel& m, int group, std::vector<std::uint8_t>& walls);

// The static model the planner uses (built from the SDK in the bot, by the harness natively).
const StaticModel* static_model();

// Calibration factors fitted against engine games (econ harness, calib.tsv). Defaults are the
// surrogate's raw arithmetic.
struct Calib
{
    float rate_solo{ 1.0f };      // camp damage rate multiplier, solo
    float rate_party{ 1.0f };     // ... per member in a party
    int clear_extra{ 1 };         // rounds added to every clear (approach, wake-up)
    int party_extra{ 0 };         // extra rounds per additional party member (staging loss)
    float damage_mult{ 1.0f };    // damage taken multiplier
    float walk_mult{ 1.0f };      // walking rounds multiplier
    int walk_extra{ 0 };          // rounds added per trip
    int recall_extra{ 1 };        // rounds after the recall channel (arrival turn heal)
    int bank_extra{ 1 };          // rounds added to a tribute
};
inline Calib g_calib{};
inline Calib& calib() { return g_calib; }

// Live champion state in the surrogate.
struct Champ
{
    std::uint8_t shards[kMaxFamilies][3]{};  // shards owned per family x stat (5 = emblem)
    float str{}, foc{}, maxhp{};
    float hp{};
    float rate_a{}, rate_b{};  // camp damage per round = rate_a + rate_b x neighbours
    float shield_amt{}, shield_dur{}, shield_cd{};
    float taken_mult{ 1.0f }, gold_mult{ 1.0f };
    int recall_turns{ 5 };
    float gold{};
    int bag{};
    int pt{};         // point index the champion stands at (0 = spawn), or -1 = at `tile`
    int tile{ -1 };
    int free{};       // round it can start a new task
    int plan_pos{};   // next shop plan step
    std::uint16_t emblem_mask{};  // families known to hold an emblem (snapshot), on top of shards
};

// Shop plan step: family, stat (0 str, 1 foc, 2 hp), shards to own after it (0 = unlimited).
struct ShopStep
{
    std::int8_t family{ -1 };
    std::int8_t stat{};
    std::int8_t shards{};
};
std::vector<ShopStep> default_shop_plan(const StaticModel& m, int slot);
// Recompute derived stats from design + shards (keeps the current health fraction unless `heal`).
void refresh_stats(const StaticModel& m, int slot, Champ& c);
// Buy along the plan with the gold carried (spawn only). Returns shards bought.
int shop(const StaticModel& m, int slot, Champ& c, const std::vector<ShopStep>& plan);

struct ClearForecast
{
    int rounds{};
    float damage[kMaxChamps]{};  // per party member, in party order
    bool ok{};
};
// Forecast for `n` champions (slots) all present from the start, on group g with health scale hs.
ClearForecast forecast_clear(const StaticModel& m, const Champ* const* party, const int* slots, int n, int g, float hs);

// Walking rounds from a champion's position to point p.
int walk_rounds(const StaticModel& m, const Champ& c, int p);
int tribute_rounds(const Rules& r, int gems);

} // namespace mb::econ
