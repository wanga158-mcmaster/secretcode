#pragma once
// Item purchases.
//
// Mechanics assumed (GameRules "Items & gold"): shards cost item.cost() gold and add the
// champion's growth for their stat; five matching shards combine into an emblem whose family
// passive applies once per family; Power emblems count as one extra shard instead. Purchases
// need spawn tiles or death, except Tavi's remote shop (queried via can_buy()).
#include "ctx.h"

namespace mb {

enum class Stat : std::uint8_t { strength, focus, health };

// Which stat gives this kit the most camp damage per shard (or health for tanks).
Stat main_stat(const Kit& k);

// Buy as many planned items as gold and location allow. Returns number bought.
int shop(Ctx& ctx);

// The champion's own history, which the state-driven shop (cfg::shop_plan 100) and the value-based
// shop recall (cfg::shop_recall_mode 1) price items with. Brain updates it once per turn, and only
// when one of those knobs is on.
struct ShopSignals
{
    int alive_rounds{};      // turns taken alive
    float earned_gold{};     // gold gained
    float camp_damage{};     // health lost between turns with no enemy champion within 6
    float pvp_damage{};      // health lost between turns with an enemy champion within 6
    int fight_rounds{};      // turns spent fighting camps
    int deaths{};            // own deaths
    int team_deaths{};       // deaths of other allies seen through their alive flags
    int recalls{};           // recall channels started
    int last_gold{ -1 };     // gold after last turn's purchases
    int spawn_visits{};      // arrivals on our spawn (or deaths): shopping opportunities
    int last_visit{};        // round of the latest one
    bool was_recalling{};
    bool was_dead{};
    bool was_on_spawn{};
    std::vector<std::uint8_t> ally_alive;  // previous turn's ally alive flags
};
ShopSignals& shop_signals();

// Value-based shop recall (cfg::shop_recall_mode 1): rounds saved by recalling now to spend our gold
// and heal, against walking on (the caller compares this with the detour in rounds).
// `extra_gold` counts gold about to arrive (a capture reward landing with the recall, cfg::capture_recall 2).
float shop_recall_benefit(const Ctx& ctx, std::uint32_t extra_gold = 0);

} // namespace mb
