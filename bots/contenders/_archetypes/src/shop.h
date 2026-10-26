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

} // namespace mb
