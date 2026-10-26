#pragma once
#include "../../common/pure.h"

namespace plan4::capital {
inline constexpr const char* version = "plan4-capital/2";
enum class Family : unsigned { monster_strength, jungle_health, power_strength };
struct ItemDefinition {
    unsigned cost = 0, combine_count = 0;
    int shard_health = 0, shard_strength = 0, shard_focus = 0;
    int emblem_health = 0, emblem_strength = 0, emblem_focus = 0;
    float passive_magnitude = 0;
    bool purchasable = false;
    friend bool operator==(const ItemDefinition&,const ItemDefinition&)=default;
};
struct Definitions {
    std::array<ItemDefinition,3> item{};
    float health_growth = 0, strength_growth = 0, focus_growth = 0;
    std::uint64_t public_source = 0, quote_law = 0;
    bool queried_public = false, actual_purchase_receipt_parity = false;
};
struct Stats { float maximum_health = 0, strength = 0, focus = 0, camp_outgoing = 1, camp_incoming = 1; };
struct Equipment { std::array<unsigned,3> total_shards{}; }; // Includes ingredients inside emblems.
struct ShopAccess { bool on_own_spawn = false, dead = false, tavi_field_purchase = false, legal = false; };
struct PurchaseQuote { Stats after{}; float spawn_healing = 0; unsigned cost = 0; bool supported = false; };
inline bool valid_stats(const Stats& stats) {
    return std::isfinite(stats.maximum_health) && stats.maximum_health > 0 && std::isfinite(stats.strength) && stats.strength >= 0 &&
        std::isfinite(stats.focus) && stats.focus >= 0 && std::isfinite(stats.camp_outgoing) && stats.camp_outgoing > 0 &&
        std::isfinite(stats.camp_incoming) && stats.camp_incoming >= 0;
}
inline bool valid_definitions(const Definitions& defs) {
    if (!defs.queried_public || !defs.public_source || !defs.quote_law || !std::isfinite(defs.health_growth) || defs.health_growth < 0 ||
        !std::isfinite(defs.strength_growth) || defs.strength_growth < 0 || !std::isfinite(defs.focus_growth) || defs.focus_growth < 0) return false;
    for (const auto& item : defs.item)
        if (!item.cost || !item.combine_count || !item.purchasable || item.shard_health < 0 || item.shard_strength < 0 || item.shard_focus < 0 ||
            item.emblem_health < 0 || item.emblem_strength < 0 || item.emblem_focus < 0 || !std::isfinite(item.passive_magnitude) ||
            item.passive_magnitude < 0 || item.passive_magnitude >= 1) return false;
    return true;
}
inline std::int64_t points(unsigned shards, unsigned combine, int plain, int emblem) {
    return std::int64_t(shards / combine) * emblem + std::int64_t(shards % combine) * plain;
}
// The pure quote uses queried public item points and champion growth. No second
// cash debit is charged for replacing five ingredients with their emblem.
inline PurchaseQuote public_quote(const Stats& before, const Equipment& equipment, Family family, unsigned count,
                                  ShopAccess access, const Definitions& defs, const void*) {
    PurchaseQuote quote; const unsigned f = unsigned(family);
    if (f >= 3 || !valid_definitions(defs) || !valid_stats(before) || !count || !access.legal ||
        !(access.on_own_spawn || access.dead || access.tavi_field_purchase)) return quote;
    const auto& item = defs.item[f]; const unsigned old = equipment.total_shards[f];
    if (count > std::numeric_limits<unsigned>::max() - old || count > std::numeric_limits<unsigned>::max() / item.cost) return quote;
    const unsigned next = old + count;
    const float hp = float(points(next,item.combine_count,item.shard_health,item.emblem_health)-points(old,item.combine_count,item.shard_health,item.emblem_health))*defs.health_growth;
    const float strength = float(points(next,item.combine_count,item.shard_strength,item.emblem_strength)-points(old,item.combine_count,item.shard_strength,item.emblem_strength))*defs.strength_growth;
    const float focus = float(points(next,item.combine_count,item.shard_focus,item.emblem_focus)-points(old,item.combine_count,item.shard_focus,item.emblem_focus))*defs.focus_growth;
    if (hp < 0 || strength < 0 || focus < 0) return quote;
    quote.after = before; quote.after.maximum_health += hp; quote.after.strength += strength; quote.after.focus += focus;
    if (old / item.combine_count == 0 && next / item.combine_count > 0) {
        if (family == Family::monster_strength) quote.after.camp_outgoing *= 1 + item.passive_magnitude;
        if (family == Family::jungle_health) quote.after.camp_incoming *= 1 - item.passive_magnitude;
    }
    quote.spawn_healing = access.on_own_spawn && !access.dead ? hp : 0;
    quote.cost = item.cost * count; quote.supported = valid_stats(quote.after) && std::isfinite(quote.spawn_healing);
    return quote;
}
struct QuoteAdapter {
    PurchaseQuote (*purchase)(const Stats&,const Equipment&,Family,unsigned,ShopAccess,const Definitions&,const void*) = public_quote;
    const void* context = nullptr;
};
struct CombatQuote { int rounds = -1; float loss = 0; bool affordable = false, supported = false; };
inline CombatQuote conservative_combat(const Stats& stats,float health,float camp_health,float raw_incoming) {
    CombatQuote quote;
    if (!valid_stats(stats) || !std::isfinite(health) || health <= 0 || !std::isfinite(camp_health) || camp_health < 0 ||
        !std::isfinite(raw_incoming) || raw_incoming < 0) return quote;
    const float dps=(stats.strength*1.125f+stats.focus*.12f)*stats.camp_outgoing;
    if (!std::isfinite(dps) || dps <= 0) return quote;
    const double rounds=std::ceil(camp_health/dps); // Match common observer's float quotient before ceil.
    if (rounds > max_horizon) return quote;
    quote.rounds=int(rounds);quote.loss=raw_incoming*stats.camp_incoming*float(quote.rounds+2);
    quote.supported=std::isfinite(quote.loss);quote.affordable=quote.supported&&health-quote.loss>=stats.maximum_health*.25f;
    return quote;
}
struct Stock {
    int id = -1; Point spawn{}; Lifecycle lifecycle = Lifecycle::unknown;
    float current_health = 0, full_health = 0, unmodified_incoming = 0;
    unsigned gems = 0, gold = 0;
    unsigned eligible_allied_occupants = 1;
    int ready_round = -1, respawn_rounds = 0;
    bool assured = false, owned_spawn = false, occupancy_known = false, own_is_lowest_occupant = true;
};
enum class Kind { travel, wait, clear, capture, bank, restore, shop };
struct Step { Kind kind=Kind::wait; int group=-1, rounds=0; Point target{}; ShopAccess shop{}; bool legal_quote=false, off_target_spawns=false; };
struct PurchaseEvent { Family family; unsigned count,cost,cash_before,cash_after; int round; Stats accounting_after,effective_after; float health_after; };
struct Path {
    Stats accounting{}, effective{}; Equipment equipment{};
    float health=0; unsigned cash=0,bag=0,secured=0;
    Point position{}; int now=0,deadline=0,macros=0,stock_count=0,purchase_count=0;
    std::array<Stock,max_groups> stock{};
    std::array<PurchaseEvent,24> purchases;
    std::uint64_t public_source=0,quote_law=0;
    bool coupling_enabled=true, supported=false, purchase_parity_supported=false, stopped=false,alive=true;
};
inline bool begin(Path& path,int round,int horizon_rounds,Point position,float health,unsigned cash,unsigned bag,
                  Stats stats,Equipment equipment,const Definitions& defs,bool enabled,bool alive=true) {
    path.supported=false;
    if (round<0||horizon_rounds<0||horizon_rounds>max_horizon||horizon_rounds>std::numeric_limits<int>::max()-round||
        !valid_stats(stats)||!std::isfinite(health)||(alive?health<=0:health!=0)||health>stats.maximum_health||!valid_definitions(defs)) return false;
    path.accounting=path.effective=stats;path.equipment=equipment;path.health=health;path.cash=cash;path.bag=bag;path.secured=0;
    path.position=position;path.now=round;path.deadline=round+horizon_rounds;path.macros=path.stock_count=path.purchase_count=0;
    path.coupling_enabled=enabled;path.stopped=false;path.supported=true;
    path.alive=alive;path.public_source=defs.public_source;path.quote_law=defs.quote_law;
    path.purchase_parity_supported=defs.actual_purchase_receipt_parity;return true;
}
inline bool add_stock(Path& path,const Stock& stock) {
    if(path.stock_count>=max_groups||stock.id<0||stock.respawn_rounds<0||!std::isfinite(stock.current_health)||stock.current_health<0||
        !std::isfinite(stock.full_health)||stock.full_health<0||!std::isfinite(stock.unmodified_incoming)||stock.unmodified_incoming<0)return false;
    for(int n=0;n<path.stock_count;n++)if(path.stock[n].id==stock.id)return false;
    path.stock[path.stock_count++]=stock;return true;
}
inline bool advance(Path& path,int rounds) {
    if(path.stopped||rounds<0||rounds>path.deadline-path.now){path.stopped=true;return false;}
    path.now+=rounds;
    for(int n=0;n<path.stock_count;n++) {
        auto& stock=path.stock[n];
        if(stock.assured&&!stock.owned_spawn&&path.position!=stock.spawn&&stock.ready_round>=0&&path.now>=stock.ready_round&&
            (stock.lifecycle==Lifecycle::captured||stock.lifecycle==Lifecycle::respawning)) {
            stock.lifecycle=Lifecycle::living;stock.current_health=stock.full_health;
        }
    }
    return true;
}
inline bool production_supported(const Path& path){return path.supported&&path.purchase_parity_supported;}
inline bool shop(Path& path,ShopAccess access,const Definitions& defs,QuoteAdapter adapter={}) {
    if(!path.supported||path.stopped||!adapter.purchase||!valid_definitions(defs)||defs.public_source!=path.public_source||defs.quote_law!=path.quote_law||
        access.dead==path.alive||!access.legal||!(access.on_own_spawn||access.dead||access.tavi_field_purchase))return false;
    for(unsigned attempt=0;attempt<3;attempt++) {
        const unsigned f=path.equipment.total_shards[0]<5?0:path.equipment.total_shards[1]<5?1:2;
        const auto& item=defs.item[f];const unsigned owned=path.equipment.total_shards[f];
        const unsigned limit=f<2?(owned<5?5-owned:0):100;
        const unsigned count=std::min(limit,path.cash/item.cost);
        if(!limit)continue;
        if(!count)break; // Never skip an unaffordable inherited stage.
        if(path.purchase_count>=int(path.purchases.size())){path.stopped=true;return false;}
        const auto q=adapter.purchase(path.accounting,path.equipment,Family(f),count,access,defs,adapter.context);
        if(!q.supported||!valid_stats(q.after)||!std::isfinite(q.spawn_healing)||q.spawn_healing<0||
            q.spawn_healing>(access.on_own_spawn&&!access.dead?std::max(0.0f,q.after.maximum_health-path.accounting.maximum_health):0)||
            count>std::numeric_limits<unsigned>::max()-owned||q.cost!=count*item.cost||q.cost>path.cash)return false;
        const unsigned before=path.cash;path.cash-=q.cost;path.equipment.total_shards[f]+=count;
        path.accounting=q.after;
        if(path.coupling_enabled) {
            path.effective=q.after;
            path.health=std::min(path.effective.maximum_health,path.health+q.spawn_healing);
        }
        path.purchases[path.purchase_count++]={Family(f),count,q.cost,before,path.cash,path.now,path.accounting,path.effective,path.health};
        if(count<limit)break;
    }
    return true;
}
// Explicit phase steps are supplied by the same fixed common macro/list. This
// core neither invents travel/restore durations nor optimizes a purchase policy.
// Durations must be complete common phase quotes (including capture initiation
// and channel boundaries); raw camp incoming excludes current kit modifiers.
inline bool step(Path& path,const Step& step,const Definitions& defs,QuoteAdapter adapter={}) {
    if(!path.supported||path.stopped||!step.legal_quote)return false;
    if(step.kind==Kind::shop)return step.rounds==0&&shop(path,step.shop,defs,adapter);
    if(step.kind==Kind::travel) {
        if(!path.alive)return false;
        if(path.position!=step.target)for(int n=0;n<path.stock_count;n++)if(path.stock[n].owned_spawn){path.stock[n].owned_spawn=false;path.stock[n].ready_round=std::max(path.stock[n].ready_round,path.now==std::numeric_limits<int>::max()?path.now:path.now+1);}
        if(!advance(path,step.rounds))return false;
        path.position=step.target;return advance(path,0);
    }
    if(step.kind==Kind::wait)return step.off_target_spawns&&advance(path,step.rounds);
    if(step.kind==Kind::restore) {
        if(!step.shop.on_own_spawn||step.rounds<1||!advance(path,step.rounds))return false;
        path.health=path.effective.maximum_health;path.alive=true;return true;
    }
    if(step.kind==Kind::bank) {
        if(!path.alive||!path.bag||step.rounds<1||path.bag>std::numeric_limits<unsigned>::max()-path.secured||!advance(path,step.rounds))return false;
        path.secured+=path.bag;path.bag=0;return true;
    }
    if(step.group<0||step.group>=path.stock_count)return false;
    auto& source=path.stock[step.group];
    if(!source.assured)return false;
    if(source.lifecycle==Lifecycle::captured||source.lifecycle==Lifecycle::respawning) {
        if(source.owned_spawn||path.position==source.spawn||source.ready_round<0||path.now<source.ready_round)return false;
        source.lifecycle=Lifecycle::living;source.current_health=source.full_health;
    }
    if(step.kind==Kind::clear) {
        if(!path.alive||source.lifecycle!=Lifecycle::living)return false;
        const auto quote=conservative_combat(path.effective,path.health,source.current_health,source.unmodified_incoming);
        if(!quote.supported||!quote.affordable||!advance(path,quote.rounds))return false;
        path.health-=quote.loss;source.lifecycle=Lifecycle::cleared;source.current_health=0;return true;
    }
    const unsigned own_gems=allocated_reward(source.gems,source.eligible_allied_occupants,source.own_is_lowest_occupant);
    const unsigned own_gold=allocated_reward(source.gold,source.eligible_allied_occupants,source.own_is_lowest_occupant);
    if(!path.alive||step.kind!=Kind::capture||source.lifecycle!=Lifecycle::cleared||path.position!=source.spawn||step.rounds<1||
        !source.occupancy_known||!source.eligible_allied_occupants||
        own_gems>std::numeric_limits<unsigned>::max()-path.bag||own_gold>std::numeric_limits<unsigned>::max()-path.cash||
        !advance(path,step.rounds))return false;
    path.bag+=own_gems;path.cash+=own_gold;source.lifecycle=Lifecycle::captured;source.owned_spawn=true;
    source.ready_round=source.respawn_rounds>std::numeric_limits<int>::max()-path.now?-1:path.now+source.respawn_rounds;
    return true;
}
inline bool begin_macro(Path& path){if(path.stopped||path.macros>=max_macros)return false;++path.macros;return true;}
// Common checkpoint composition and an SDK adapter proving actual purchase
// quote parity remain mandatory runtime gates. Conditional pure execution is
// possible with purchase_parity_supported=false and makes no legal-run claim.
} // namespace plan4::capital
