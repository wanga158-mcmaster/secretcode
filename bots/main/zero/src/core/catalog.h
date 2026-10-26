#pragma once
// Rule data (facts): champion designs and abilities, camps, items, passives, game settings. One generated table
// (catalog_data.h, from docs/reference/runtime-catalog.json by tools/gen_catalog.py) serves the bot and the native
// tools alike; the bot checks it against the SDK's live designs on its first turn (sdk_obs.cpp).
#include <cstdint>

namespace zero {

// SDK ability_kind values.
enum AbilityKind : std::uint8_t { ab_none = 0, ab_reset = 1, ab_damage = 2, ab_aoe = 3, ab_cc = 4, ab_heal = 5, ab_dash = 6, ab_shield = 7 };
// SDK ability_target_mask bits (camps, enemies, allies, self, ground).
enum TargetBits : std::uint8_t { tg_camps = 1, tg_enemies = 2, tg_allies = 4, tg_self = 8, tg_ground = 16 };

struct Ability
{
    std::uint8_t kind{}, targets{};
    bool excludes_walking{};
    std::int16_t cooldown{};
    float range{}, radius{}, modifier{}, duration{};
    bool enhanced_attack{};
};

struct ChampionDesign
{
    const char* name{};
    std::uint8_t type{}, passive{}, attack_type{};  // attack_type 0 melee, 1 ranged
    Ability ability[2]{};
    float vision{}, move_speed{}, attack_range{};
    float health{}, health_growth{}, strength{}, strength_growth{}, focus{}, focus_growth{}, camp_damage_mult{};
};

struct CampDesign
{
    const char* name{};
    std::uint8_t type{};
    float health{}, strength{}, attack_range{}, vision{};
    std::uint32_t gold{}, gems{};
    std::int16_t respawn{};
};

struct ItemDesign
{
    const char* name{};
    std::uint8_t id{};
    std::uint32_t cost{}, combine_count{}, combine_cost{};
    std::int16_t result{};  // -1 none
    std::int32_t health{}, strength{}, focus{};
    std::uint8_t passive{};
    float passive_magnitude{};
    bool purchasable{};
};

struct PassiveDesign
{
    const char* name{};
    std::uint8_t id{}, type{};
    float magnitude{};
};

struct Settings
{
    int capture_time{}, camp_sleep_time{}, respawn_time{}, recall_time{}, max_rounds{}, winning_gems{}, kill_gold{};
    float submit_gem_duration_multiplier{};
    int submit_gem_minimum_duration{}, submit_gems_range{}, ward_cooldown{}, ward_vision_radius{}, ward_placement_radius{};
    int altar_lock_hits{}, tp_crystal_time{};
};

struct Catalog
{
    Settings settings;
    const ChampionDesign* champions{};
    int n_champions{};
    const CampDesign* camps{};
    int n_camps{};
    const ItemDesign* items{};
    int n_items{};
    const PassiveDesign* passives{};
    int n_passives{};

    const ChampionDesign& champion(int type) const { return champions[type]; }
    const CampDesign& camp(int type) const { return camps[type]; }
    const ItemDesign& item(int id) const { return items[id]; }
};

const Catalog& catalog();

}  // namespace zero
