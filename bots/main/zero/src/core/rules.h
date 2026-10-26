#pragma once
// Exact rule arithmetic (facts from docs/reference/GameRules.md and the catalog). No choices.
#include <array>
#include <cstdint>
#include <vector>
#include "catalog.h"
#include "map.h"
#include "obs.h"

namespace zero::rules {

// Item families and stats, parsed once from the catalog's item names ("<family>_shard_of_<stat>",
// "<family>_emblem_of_<stat>", consumables).
enum Family : std::uint8_t { fam_monster, fam_power, fam_durability, fam_treasure, fam_jungle, fam_home, fam_bruiser, fam_count, fam_none = 255 };
enum Stat : std::uint8_t { st_strength, st_health, st_focus, st_count, st_none = 255 };
enum ItemKind : std::uint8_t { ik_shard, ik_emblem, ik_lock, ik_crystal, ik_other };

struct ItemInfo
{
    ItemKind kind{ ik_other };
    Family family{ fam_none };
    Stat stat{ st_none };
};
const ItemInfo& item_info(int item_id);

// Owned emblems per family (from item counts).
std::array<bool, fam_count> emblems(const std::array<std::uint8_t, kMaxItems>& items);

// Multipliers from passives and emblems.
float gold_multiplier(int champion_type, const std::array<std::uint8_t, kMaxItems>& items);
float camp_damage_dealt_multiplier(int champion_type, const std::array<std::uint8_t, kMaxItems>& items);
float camp_damage_taken_multiplier(const std::array<std::uint8_t, kMaxItems>& items);
float champion_damage_taken_multiplier(const std::array<std::uint8_t, kMaxItems>& items);
int recall_turns(const std::array<std::uint8_t, kMaxItems>& items);
int tribute_turns(std::uint32_t gems);

// Stat gain of buying one item now (shards add the champion's growth; the fifth shard of a family also adds
// the emblem's extra stats: Power one more shard, Bruiser one of each other stat).
struct StatGain
{
    float health{}, strength{}, focus{};
    bool completes_emblem{};
};
StatGain buy_gain(int champion_type, const std::array<std::uint8_t, kMaxItems>& items, int item_id);

// Damage rates of a unit with the given stats (rule summaries, per round): basic attack, damaging casts when ready
// (modifier x focus), and the sustained rate counting casts once per cooldown and attack resets.
struct DamageRates
{
    float basic{}, burst{}, sustained{};
    int reach{};  // attack range + one walk (melee) or attack range (ranged), plus dash range if a dash is ready
};
DamageRates rates(int champion_type, float strength, float focus, const std::int8_t cooldown[2], bool ready_unknown_as_ready);

// Camp clear forecast for a set of attackers with total sustained camp damage `dps` on group members of known
// health (Smith order: highest strength per health first). Rounds to clear and damage the group deals meanwhile.
struct ClearForecast
{
    int rounds{};
    float damage{};
};
ClearForecast clear(const Map& map, int group, const float* member_hp, float dps);  // member_hp in member order

// Which champion a camp at `camp` with range `range` strikes: the nearest champion in range (Chebyshev),
// ties by lower actor id. Returns the index into `positions` or -1.
int camp_target(Tile camp, int range, const std::vector<Tile>& positions, const std::vector<int>& ids);

// Symmetric shadowcasting vision (Albert Ford) from `from` with radius `r`: walls and bushes block, the bush
// cluster we stand in is transparent to us. Fills `seen` (w*h bools).
void vision(const Map& map, Tile from, int r, std::vector<std::uint8_t>& seen);

}  // namespace zero::rules
