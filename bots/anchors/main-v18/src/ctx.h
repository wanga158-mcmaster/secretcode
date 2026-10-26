#pragma once
// Per-turn observation snapshot and the champion's decoded kit.
//
// Every live query is a host call (~50-100 fuel), so a turn reads each visible unit once
// into these views and later logic uses the cached values. Views are refreshed after our
// own movement when legality depends on positions.
#include <cambc.h>
#include <optional>
#include <vector>
#include "model.h"
#include "util.h"

namespace mb {

struct CampView
{
    const cambc::camp* camp{};  // owned by World::group_objects
    vec2 pos{};
    float hp{};
    float strength{};
    int range{};
    bool awake{};
    int group{ -1 };
    int member{ -1 };
};

struct ChampView
{
    const cambc::champion* champ{};  // owned by the cached roster
    vec2 pos{};
    float hp{};
    float max_hp{};
    float strength{};
    float focus{};
    float dps{};  // enemies: sustained damage per round against champions (computed once per turn)
    int range{};
    std::uint32_t gems{};
    cambc::champion_type type{};
    bool channeling{};
    int index{ -1 };  // position in the side's roster (stable identity across turns)
    int cooldown[2]{ -1, -1 };  // live remaining cooldown per ability slot (-1: unknown)
};

struct SlotKit
{
    model::AbilityKind kind{ model::AbilityKind::none };
    float range{};
    float modifier{};
    float radius{};
    int duration{};
    int cooldown{ 8 };
    bool exclusive{};
    bool camps{}, enemies{}, allies{}, self{}, ground{};
    bool enhanced{};
};

struct Kit
{
    cambc::champion_type type{};
    bool ranged{};
    int attack_range{ 1 };
    int vision{ 8 };
    SlotKit slot[2];
    bool splash{}, kill_resets{}, remote_shop{}, stun_immune{}, bonus_gold{}, gem_focus{};
    float strength_growth{}, focus_growth{}, health_growth{};
    model::ChampionSummary summary;  // refreshed with live stats each turn
};

const Kit& kit();

// All champion handles of one side with their (immutable) types, built once.
struct Roster
{
    std::vector<cambc::champion> champs;
    std::vector<cambc::champion_type> types;
};
const Roster& roster(cambc::team side);
// Incoming camp-damage multiplier from our emblems (jungle/durability); cached until refresh.
float damage_taken_multiplier(bool refresh);
// Summary for any champion type from immutable designs (base stats, no items).
model::ChampionSummary design_summary(cambc::champion_type type);

struct Ctx
{
    int round{};
    vec2 pos{};
    float hp{}, max_hp{};
    float strength{}, focus{};
    std::uint32_t gems{}, gold{};
    cambc::champion_state state{};
    bool on_spawn{};
    cambc::bitboard visible;
    std::vector<CampView> camps;     // visible living camps
    std::vector<ChampView> enemies;  // visible living enemy champions
    std::vector<ChampView> allies;   // visible allied champions other than self
    float camp_damage_taken_mult{ 1.0f };  // from our items (jungle/durability)
    float gem_weight{ 1.0f };              // value of one gem in gold this turn (Brain::turn)
    // Awake camps seen recently but out of view now: a camp keeps attacking while any champion
    // is within its range, seen or not (bushes hide camps that can still reach us).
    struct Threat
    {
        vec2 pos{};
        int range{};
        float strength{};
    };
    Threat unseen[8]{};
    int n_unseen{};
    bool hit_unseen{};  // health fell since our last turn with no visible source in reach
    // Origin for route-distance queries: our tile, or a walkable neighbour when we stand on a
    // camp tile (camp tiles are walls in other groups' fields).
    vec2 route_pos{};

    void refresh_self();
    void observe();
    float hp_frac() const { return max_hp > 0 ? hp / max_hp : 0.0f; }
};

} // namespace mb
