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
float shop_recall_benefit(const Ctx& ctx);

// Capital policy consumes only cached estimates in hot target/bank callers. Refresh is bounded
// and runs after ordinary actions; actual purchases recompute from the fresh purse and items.
void capital_refresh(const Ctx& ctx, int productive_target);
void capital_forecast_emit();
void capital_forecast_recall_caller(const Ctx& ctx, int target, int extra, float benefit, bool shopping);
int capital_episode_scout(const Ctx& ctx, int gold_target);
float capital_gold_price();
float capital_gem_flow();
float capital_bank_credit(const Ctx& ctx);

} // namespace mb
