#include "student.h"
#include <algorithm>
#include <cmath>
#include "catalog.h"
#include "moves.h"
#include "rules.h"

namespace zero::student {
namespace {

// student1 argument ABI (all heads, including scalar heads, have 24 facts).
// Geometry rows: 0 bias; 1/2 dx/dy /20; 3 Chebyshev /20; 4 goal distance /20;
// 5 goal-distance delta /20; 6 goal known (+1/-1); 7 bush,8 own spawn,9 enemy spawn,
// 10 hole,11 wall,12 altar. There are no path/cache/claim/forecast features.
// Unit rows override12=goal target;13 camp,14 enemy,15 ally/self,16 lock;17 hp/2000,
// 18 max hp/2000,19 strength/200,20 focus/200,21 gems/100,22 shield/2000,
// 23 observed recall/tribute/stun bits /7. Locks use17 hits/20,18 full hits/20,
// 22 hits known (+1/-1),23 zero. Unknown lock health is never assumed full.
// Ground cast rows:13/14/15 visible enemy/camp/ally counts in ability radius /5,/20,/5;
// 16 a visible goal target in that radius;17 range/20,18 radius/20,19 duration/40,
// 20 ability kind/7,21 excludes walking,22 reservation,23 target mask/31.
// Shop rows:0 bias;1..7 item family;8/9/10 raw strength/health/focus points /5;
// 11 price/300,12 price/gold,13 owned/5,14 combine count/5,15 combine cost/300,
// 16 owned result/5,17 passive magnitude/10,18 shard,19 emblem,20 purchasable,
// 21 gold/300,22 positive price,23 other item kind. Quantity uses the same row
// with23=max affordable/100; teacher Cand.count is never an input.
// Utility rows keep geometry;13 buy-consumable,14 use,15 ward,16 sell;17 TP,
// 18 lock payload,19 inventory/charges /10,20 adapter price fact,21 radius fact,
// 22 adapter cooldown/replacement fact,23 reservation. SELL instead uses0=-1,
// 13..19 item family,20/21/22 raw strength/health/focus /5,23 owned/5.
// PRIORITY is constant phi={1,0,...}; scalar output uses shared asinh/10 encoding.

struct Geometry
{
    const Context& ctx;
    bool here_known{};
    int here{};
    explicit Geometry(const Context& c) : ctx(c)
    {
        if (ctx.obs->alive && ctx.map->inside(ctx.obs->self.at))
            here = geometry_goal_distance(ctx, ctx.obs->self.at, &here_known);
    }
    void fill(Cand& c, Tile at) const
    {
        const auto& o = *ctx.obs;
        c.at = at;
        c.phi[0] = 1.0f;
        if (ctx.map->inside(at) && ctx.map->inside(o.self.at))
        {
            c.phi[1] = static_cast<float>(at.x - o.self.at.x) / 20.0f;
            c.phi[2] = static_cast<float>(at.y - o.self.at.y) / 20.0f;
            c.phi[3] = static_cast<float>(cheb(at, o.self.at)) / 20.0f;
        }
        bool known = false;
        const int distance = ctx.map->inside(at) ? geometry_goal_distance(ctx, at, &known) : 0;
        c.phi[4] = known ? static_cast<float>(distance) / 20.0f : 0.0f;
        c.phi[5] = known && here_known ? static_cast<float>(distance - here) / 20.0f : 0.0f;
        c.phi[6] = known ? 1.0f : -1.0f;
        if (!ctx.map->inside(at)) return;
        const auto flags = ctx.map->at(at);
        c.phi[7] = (flags & tf_bush) ? 1.0f : 0.0f;
        c.phi[8] = (flags & (o.team == 0 ? tf_spawn_a : tf_spawn_b)) ? 1.0f : 0.0f;
        c.phi[9] = (flags & (o.team == 0 ? tf_spawn_b : tf_spawn_a)) ? 1.0f : 0.0f;
        c.phi[10] = (flags & tf_hole) ? 1.0f : 0.0f;
        c.phi[11] = (flags & tf_wall) ? 1.0f : 0.0f;
        c.phi[12] = (flags & tf_altar) ? 1.0f : 0.0f;
    }
};

bool in_range(Tile self, Tile target, float range) { return static_cast<float>(cheb(self, target)) <= range; }

bool enemy_lock(const Obs& o, std::size_t altar)
{
    return altar < o.altar_lock.size() && o.altar_lock[altar] == lk_theirs &&
        (altar >= o.altar_lock_hits.size() || o.altar_lock_hits[altar] != 0);
}

bool target_at(const Context& ctx, Tile tile)
{
    const auto& m = *ctx.mem;
    if (m.goal_kind == g_farm && ctx.map->inside(tile) &&
        static_cast<std::size_t>(ctx.map->idx(tile)) < ctx.map->group_of.size())
        return m.goal_target >= 0 && ctx.map->group_of[static_cast<std::size_t>(ctx.map->idx(tile))] == m.goal_target;
    if (m.goal_kind == g_bank && m.goal_target >= 0 && m.goal_target < static_cast<int>(ctx.map->altars.size()))
        return ctx.map->altars[static_cast<std::size_t>(m.goal_target)] == tile;
    if (m.goal_kind == g_engage)
        for (const auto& e : ctx.obs->enemies) if (e.id == m.goal_target) return e.at == tile;
    if (m.goal_kind == g_assist)
        for (const auto& a : ctx.obs->allies) if (a.id == m.goal_target) return a.at == tile;
    return false;
}

void unit_facts(const Context& ctx, Cand& c, const UnitView& target, bool ally)
{
    c.phi[12] = target_at(ctx, target.at) ? 1.0f : 0.0f;
    c.phi[ally ? 15 : 14] = 1.0f;
    c.phi[17] = target.hp / 2000.0f;
    c.phi[18] = target.max_hp / 2000.0f;
    c.phi[19] = target.strength / 200.0f;
    c.phi[20] = target.focus / 200.0f;
    c.phi[21] = static_cast<float>(target.gems) / 100.0f;
    c.phi[22] = target.shield / 2000.0f;
    c.phi[23] = static_cast<float>((target.recalling ? 1 : 0) | (target.tributing ? 2 : 0) | (target.stunned ? 4 : 0)) / 7.0f;
}

void camp_facts(const Context& ctx, Cand& c, const CampView& target)
{
    const auto& design = catalog().camp(target.type);
    c.phi[12] = target_at(ctx, target.at) ? 1.0f : 0.0f;
    c.phi[13] = 1.0f;
    c.phi[17] = target.hp / 2000.0f;
    c.phi[18] = design.health / 2000.0f;
    c.phi[19] = design.strength / 200.0f;
    c.phi[23] = target.awake ? 1.0f : 0.0f;
}

void ground_facts(const Context& ctx, Cand& c, const Ability& ability)
{
    const int radius = static_cast<int>(std::max(0.0f, ability.radius));
    for (const auto& e : ctx.obs->enemies)
        if (e.hp > 0 && cheb(e.at, c.at) <= radius)
        {
            c.phi[13] += 0.2f;
            if (target_at(ctx, e.at)) c.phi[16] = 1.0f;
        }
    for (const auto& cp : ctx.obs->camps)
        if (cp.hp > 0 && cheb(cp.at, c.at) <= radius)
        {
            c.phi[14] += 0.05f;
            if (target_at(ctx, cp.at)) c.phi[16] = 1.0f;
        }
    for (const auto& a : ctx.obs->allies)
        if (a.hp > 0 && cheb(a.at, c.at) <= radius) c.phi[15] += 0.2f;
    c.phi[17] = ability.range / 20.0f;
    c.phi[18] = ability.radius / 20.0f;
    c.phi[19] = ability.duration / 40.0f;
    c.phi[20] = static_cast<float>(ability.kind) / 7.0f;
    c.phi[21] = ability.excludes_walking ? 1.0f : 0.0f;
    c.phi[22] = std::find(ctx.obs->reserved_tiles.begin(), ctx.obs->reserved_tiles.end(), c.at) != ctx.obs->reserved_tiles.end() ? 1.0f : 0.0f;
    c.phi[23] = static_cast<float>(ability.targets) / 31.0f;
}

bool consumable(int item)
{
    const auto kind = rules::item_info(item).kind;
    return kind == rules::ik_lock || kind == rules::ik_crystal;
}

void shop_facts(const Context& ctx, int item, Cand& c)
{
    const auto& cat = catalog();
    const auto& d = cat.item(item);
    const auto& info = rules::item_info(item);
    c.phi[0] = 1.0f;
    if (info.family != rules::fam_none) c.phi[1 + info.family] = 1.0f;
    c.phi[8] = static_cast<float>(d.strength) / 5.0f;
    c.phi[9] = static_cast<float>(d.health) / 5.0f;
    c.phi[10] = static_cast<float>(d.focus) / 5.0f;
    c.phi[11] = static_cast<float>(d.cost) / 300.0f;
    c.phi[12] = static_cast<float>(d.cost) / static_cast<float>(std::max(1u, ctx.obs->self.gold));
    c.phi[13] = static_cast<float>(ctx.obs->item_counts[static_cast<std::size_t>(item)]) / 5.0f;
    c.phi[14] = static_cast<float>(d.combine_count) / 5.0f;
    c.phi[15] = static_cast<float>(d.combine_cost) / 300.0f;
    if (d.result >= 0 && d.result < kMaxItems) c.phi[16] = static_cast<float>(ctx.obs->item_counts[static_cast<std::size_t>(d.result)]) / 5.0f;
    c.phi[17] = d.passive_magnitude / 10.0f;
    c.phi[18] = info.kind == rules::ik_shard ? 1.0f : 0.0f;
    c.phi[19] = info.kind == rules::ik_emblem ? 1.0f : 0.0f;
    c.phi[20] = d.purchasable ? 1.0f : 0.0f;
    c.phi[21] = static_cast<float>(ctx.obs->self.gold) / 300.0f;
    c.phi[22] = d.cost > 0 ? 1.0f : 0.0f;
    c.phi[23] = info.kind == rules::ik_other ? 1.0f : 0.0f;
}

void canonicalize(std::vector<Cand>& out)
{
    const auto less = [](const Cand& a, const Cand& b) {
        if (a.kind != b.kind) return a.kind < b.kind;
        if (a.target != b.target) return a.target < b.target;
        if (a.at.y != b.at.y) return a.at.y < b.at.y;
        return a.at.x < b.at.x;
    };
    std::stable_sort(out.begin(), out.end(), less);
    out.erase(std::unique(out.begin(), out.end(), [](const Cand& a, const Cand& b) {
        return a.kind == b.kind && a.target == b.target && a.at == b.at;
    }), out.end());
}

}  // namespace

std::uint32_t rule_mask(const Context& ctx, std::uint32_t allowed)
{
    return action_rule_mask(ctx, allowed);
}

Head argument_head(ActionType type)
{
    switch (type)
    {
    case a_move: return h_step;
    case a_attack: return h_attack;
    case a_cast0: return h_cast0;
    case a_cast1: return h_cast1;
    case a_buy: return h_shop;
    case a_replan: return h_goal;
    case a_priority: return h_priority;
    default: return h_aux;
    }
}

ActionBuild argument_candidates(const Context& ctx, ActionType type, std::vector<Cand>& out)
{
    out.clear();
    if (type >= a_count) return {ActionBuildStatus::unsupported,0};
    const auto& o = *ctx.obs;
    const bool utility = type >= a_buy_consumable;
    if (utility && !(o.utility_known_mask & action_bit(type))) return {ActionBuildStatus::unsupported,0};
    if ((type == a_buy || type == a_recall || type == a_tribute || type == a_cancel) && !o.command_facts)
        return {ActionBuildStatus::unsupported,0};
    if (!(rule_mask(ctx, action_bit(type)) & action_bit(type)))
    {
        if ((type == a_ward && !o.ward_arguments_complete) ||
            (type == a_use_consumable && !o.consumable_arguments_complete)) return {ActionBuildStatus::unsupported,0};
        return {ActionBuildStatus::unavailable,0};
    }
    if (type == a_replan)
    {
        goal_candidates(ctx,out);
        return {out.empty() ? ActionBuildStatus::mismatch : ActionBuildStatus::ok,static_cast<int>(out.size())};
    }
    if (type == a_priority)
    {
        out.emplace_back();out.back().kind=a_priority;out.back().phi[0]=1.0f;
        return {ActionBuildStatus::ok,1};
    }
    Geometry geometry(ctx);
    auto append = [&](std::uint8_t kind, int target, Tile at) -> Cand& {
        out.emplace_back();auto& c=out.back();c.kind=kind;c.target=target;geometry.fill(c,at);return c;
    };
    if (type == a_move)
    {
        for (Tile tile : o.step_ok) if (tile != o.self.at) append(0,-1,tile);
    }
    else if (type == a_attack)
    {
        const float range=catalog().champion(o.self.type).attack_range;
        for (const auto& cp:o.camps) if(cp.hp>0 && in_range(o.self.at,cp.at,range))
            camp_facts(ctx,append(1,cp.group,cp.at),cp);
        for (const auto& e:o.enemies) if(e.hp>0 && in_range(o.self.at,e.at,range))
            unit_facts(ctx,append(2,e.id,e.at),e,false);
        for(std::size_t a=0;a<ctx.map->altars.size();++a) if(enemy_lock(o,a) && in_range(o.self.at,ctx.map->altars[a],range))
        {
            auto& c=append(3,static_cast<int>(a),ctx.map->altars[a]);
            const int hits=a<o.altar_lock_hits.size()?o.altar_lock_hits[a]:-1;
            c.phi[12]=target_at(ctx,c.at)?1.0f:0.0f;c.phi[16]=1.0f;
            c.phi[17]=hits>=0?static_cast<float>(hits)/20.0f:0.0f;
            c.phi[18]=static_cast<float>(catalog().settings.altar_lock_hits)/20.0f;
            c.phi[22]=hits>=0?1.0f:-1.0f;
        }
    }
    else if(type==a_cast0 || type==a_cast1)
    {
        const auto& a=catalog().champion(o.self.type).ability[type==a_cast0?0:1];
        if(a.kind==ab_dash)
        {
            const int range=static_cast<int>(std::floor(a.range));
            std::vector<Tile> generated;const auto* tiles=&o.dash_ok;
            if(!o.dash_destinations_known || o.active_dash_range!=range)
            {generate_dash_destinations(*ctx.map,o,range,generated);tiles=&generated;}
            for(Tile tile:*tiles) if(tile!=o.self.at && in_range(o.self.at,tile,a.range))
                ground_facts(ctx,append(a.kind,-1,tile),a);
        }
        else if(a.kind==ab_aoe || (a.targets&tg_ground))
        {
            const int range=static_cast<int>(std::floor(a.range));
            for(int y=std::max(0,static_cast<int>(o.self.at.y)-range);y<=std::min(ctx.map->h-1,static_cast<int>(o.self.at.y)+range);++y)
                for(int x=std::max(0,static_cast<int>(o.self.at.x)-range);x<=std::min(ctx.map->w-1,static_cast<int>(o.self.at.x)+range);++x)
                    ground_facts(ctx,append(a.kind,-1,{static_cast<std::int16_t>(x),static_cast<std::int16_t>(y)}),a);
        }
        else if(a.kind==ab_reset || a.kind==ab_shield)
            unit_facts(ctx,append(a.kind,o.self.id,o.self.at),o.self,true);
        else if(a.kind==ab_heal)
        {
            if(a.targets&tg_self)unit_facts(ctx,append(a.kind,o.self.id,o.self.at),o.self,true);
            if(a.targets&tg_allies)for(const auto& ally:o.allies)if(ally.hp>0 && in_range(o.self.at,ally.at,a.range))
                unit_facts(ctx,append(a.kind,ally.id,ally.at),ally,true);
        }
        else if(a.kind==ab_damage || a.kind==ab_cc)
        {
            if(a.targets&tg_camps)for(const auto& cp:o.camps)if(cp.hp>0 && in_range(o.self.at,cp.at,a.range))
                camp_facts(ctx,append(a.kind,-2-cp.group,cp.at),cp);
            if(a.targets&tg_enemies)for(const auto& e:o.enemies)if(e.hp>0 && in_range(o.self.at,e.at,a.range))
                unit_facts(ctx,append(a.kind,e.id,e.at),e,false);
        }
    }
    else if(type==a_buy)
    {
        const auto& cat=catalog();
        for(int item=0;item<cat.n_items && item<kMaxItems;++item)
            if(cat.item(item).purchasable && o.buyable_items[static_cast<std::size_t>(item)] && !consumable(item))
            {out.emplace_back();auto& c=out.back();c.kind=1;c.target=item;shop_facts(ctx,item,c);}
    }
    else if(type==a_tribute)
    {
        const float range=static_cast<float>(catalog().settings.submit_gems_range);
        for(std::size_t a=0;a<ctx.map->altars.size();++a)
            if(in_range(o.self.at,ctx.map->altars[a],range) && !(a<o.altar_lock.size() && o.altar_lock[a]==lk_theirs))
                append(a_tribute,static_cast<int>(a),ctx.map->altars[a]);
    }
    else if(type==a_end || type==a_recall || type==a_cancel)append(type,-1,o.self.at);
    else if(utility)
    {
        for(const auto& u:o.utility_actions)if(u.type==static_cast<std::uint8_t>(type))
        {
            auto& c=append(type,u.target,u.at);
            c.phi[13]=type==a_buy_consumable?1.0f:0.0f;c.phi[14]=type==a_use_consumable?1.0f:0.0f;
            c.phi[15]=type==a_ward?1.0f:0.0f;c.phi[16]=type==a_sell?1.0f:0.0f;
            if(type==a_sell && u.target>=0 && u.target<catalog().n_items && u.target<kMaxItems)
            {
                for(int k=13;k<kPhi;++k)c.phi[k]=0;
                c.phi[0]=-1.0f;
                const auto& item=catalog().item(u.target);const auto family=rules::item_info(u.target).family;
                if(family!=rules::fam_none)c.phi[13+family]=1.0f;
                c.phi[20]=static_cast<float>(item.strength)/5.0f;c.phi[21]=static_cast<float>(item.health)/5.0f;
                c.phi[22]=static_cast<float>(item.focus)/5.0f;c.phi[23]=static_cast<float>(o.item_counts[static_cast<std::size_t>(u.target)])/5.0f;
            }
            else
            {
                const bool consumption=type==a_use_consumable || type==a_buy_consumable;
                c.phi[17]=consumption && u.target==0?1.0f:0.0f;c.phi[18]=consumption && u.target==1?1.0f:0.0f;
                if(consumption && u.target>=0 && u.target<2)c.phi[19]=static_cast<float>(o.consumables[static_cast<std::size_t>(u.target)])/10.0f;
                if(type==a_ward)c.phi[19]=static_cast<float>(o.ward_charges)/10.0f;
                c.phi[20]=u.phi[20];c.phi[21]=u.phi[21];c.phi[22]=u.phi[22];
                c.phi[23]=std::find(o.reserved_tiles.begin(),o.reserved_tiles.end(),u.at)!=o.reserved_tiles.end()?1.0f:0.0f;
            }
        }
    }
    canonicalize(out);
    if(out.empty())return {ActionBuildStatus::mismatch,0};
    const bool partial=(type==a_ward && !o.ward_arguments_complete) || (type==a_use_consumable && !o.consumable_arguments_complete);
    return {partial?ActionBuildStatus::truncated:ActionBuildStatus::ok,static_cast<int>(out.size())};
}

void quantity_candidate(const Context& ctx, const Cand& item, Cand& out)
{
    out=Cand{};out.kind=1;out.target=item.target;
    if(item.target<0 || item.target>=catalog().n_items || item.target>=kMaxItems)return;
    shop_facts(ctx,item.target,out);
    const auto maximum=max_affordable_quantity(ctx,item.target);
    out.phi[23]=static_cast<float>(maximum)/100.0f;
    if(maximum<=0 || !ctx.obs->command_facts || !ctx.obs->can_buy || !ctx.obs->buyable_items[static_cast<std::size_t>(item.target)])out.phi[0]=0;
}

}  // namespace zero::student

#include "student_dash_fast.h"

namespace zero::student {

namespace {

void dash_no_goal_geometry(const Context& ctx, Cand& c, Tile at)
{
    // Callers prove fresh generation, alive/inside self and goal_kind<0. Every
    // generated tile is inside, and both geometry goal queries would return
    // known=false. Keep the remaining float operations in their original order.
    const auto& o = *ctx.obs;
    c.at = at;
    c.phi[0] = 1.0f;
    c.phi[1] = static_cast<float>(at.x - o.self.at.x) / 20.0f;
    c.phi[2] = static_cast<float>(at.y - o.self.at.y) / 20.0f;
    c.phi[3] = static_cast<float>(cheb(at, o.self.at)) / 20.0f;
    c.phi[4] = 0.0f;
    c.phi[5] = 0.0f;
    c.phi[6] = -1.0f;
    const auto flags = ctx.map->at(at);
    c.phi[7] = (flags & tf_bush) ? 1.0f : 0.0f;
    c.phi[8] = (flags & (o.team == 0 ? tf_spawn_a : tf_spawn_b)) ? 1.0f : 0.0f;
    c.phi[9] = (flags & (o.team == 0 ? tf_spawn_b : tf_spawn_a)) ? 1.0f : 0.0f;
    c.phi[10] = (flags & tf_hole) ? 1.0f : 0.0f;
    c.phi[11] = (flags & tf_wall) ? 1.0f : 0.0f;
    c.phi[12] = (flags & tf_altar) ? 1.0f : 0.0f;
}

}  // namespace

ActionBuild argument_candidates_dash_fast(const Context& ctx, ActionType type, std::vector<Cand>& out)
{
    if (type != a_cast0 && type != a_cast1) return argument_candidates(ctx, type, out);
    out.clear();
    const auto& o = *ctx.obs;
    if (!(rule_mask(ctx, action_bit(type)) & action_bit(type)))
        return {ActionBuildStatus::unavailable, 0};
    const auto& ability = catalog().champion(o.self.type).ability[type == a_cast0 ? 0 : 1];
    if (ability.kind != ab_dash) return argument_candidates(ctx, type, out);

    Geometry geometry(ctx);
    const int range = static_cast<int>(std::floor(ability.range));
    const bool generated = !o.dash_destinations_known || o.active_dash_range != range;
    std::vector<Tile> tiles;
    if (generated) generate_dash_destinations_canonical(*ctx.map, o, range, tiles);
    else
    {
        tiles = o.dash_ok;
        tiles.erase(std::remove_if(tiles.begin(), tiles.end(), [&](Tile tile) {
            return tile == o.self.at || !in_range(o.self.at, tile, ability.range);
        }), tiles.end());
        // Supplied caches retain the original permissive filtering, including
        // off-map/occupied entries, then canonicalize tiles before row creation.
        std::sort(tiles.begin(), tiles.end(), [](Tile a, Tile b) {
            return a.y != b.y ? a.y < b.y : a.x < b.x;
        });
        tiles.erase(std::unique(tiles.begin(), tiles.end()), tiles.end());
    }
    out.reserve(tiles.size());
    // Fresh generate_dash_destinations excludes every observed ally/enemy/camp
    // and reservation from every landing, even in historical command-facts mode.
    // At radius zero this proves counts, target presence and reservation are all
    // exactly +0. Supplied caches do not establish this invariant and use the
    // original ground_facts, including caches with duplicates or occupied tiles.
    const bool empty_ground = generated && static_cast<int>(std::max(0.0f, ability.radius)) == 0;
    const bool no_goal = generated && o.alive && ctx.map->inside(o.self.at) && ctx.mem->goal_kind < 0;
    for (Tile tile : tiles)
    {
        out.emplace_back();
        auto& c = out.back();
        c.kind = ability.kind;
        if (no_goal) dash_no_goal_geometry(ctx, c, tile);
        else geometry.fill(c, tile);
        if (empty_ground)
        {
            c.phi[17] = ability.range / 20.0f;
            c.phi[18] = ability.radius / 20.0f;
            c.phi[19] = ability.duration / 40.0f;
            c.phi[20] = static_cast<float>(ability.kind) / 7.0f;
            c.phi[21] = ability.excludes_walking ? 1.0f : 0.0f;
            c.phi[23] = static_cast<float>(ability.targets) / 31.0f;
        }
        else ground_facts(ctx, c, ability);
    }
    return {out.empty() ? ActionBuildStatus::mismatch : ActionBuildStatus::ok, static_cast<int>(out.size())};
}

}  // namespace zero::student

#include "ground_scene.h"

namespace zero::student {

std::uint32_t rule_mask_ground_scene(const Context& ctx, const GroundUtilityScene& scene,
                                     std::uint32_t allowed)
{
    if (!ctx.map || !ctx.obs || !scene.fresh_for(*ctx.map, *ctx.obs)) return action_bit(a_end);
    constexpr auto ground_bits = action_bit(a_ward) | action_bit(a_use_consumable);
    auto mask = rule_mask(ctx, allowed & ~ground_bits);
    if ((allowed & scene.known_mask & action_bit(a_ward)) && !scene.ward_tiles.empty())
        mask |= action_bit(a_ward);
    if ((allowed & scene.known_mask & action_bit(a_use_consumable)) && !scene.lock_rows.empty())
        mask |= action_bit(a_use_consumable);
    return mask;
}

ActionBuild argument_candidates_ground_scene(const Context& ctx, const GroundUtilityScene& scene,
                                             ActionType type, std::vector<Cand>& out)
{
    if (type != a_ward && type != a_use_consumable) return argument_candidates(ctx, type, out);
    out.clear();
    if (!ctx.map || !ctx.obs || !ctx.mem || !scene.fresh_for(*ctx.map, *ctx.obs))
        return {ActionBuildStatus::mismatch, 0};
    if (!(scene.known_mask & action_bit(type))) return {ActionBuildStatus::unsupported, 0};
    const bool partial = type == a_ward ? !scene.contract.ward_complete : !scene.contract.consume_complete;
    const auto count = type == a_ward ? scene.ward_tiles.size() : scene.lock_rows.size();
    if (!count) return {partial ? ActionBuildStatus::unsupported : ActionBuildStatus::unavailable, 0};
    const auto& o = *ctx.obs;
    Geometry geometry(ctx);
    out.reserve(count);
    const auto append = [&](int target, Tile at, float price, float radius, float last) {
        out.emplace_back();
        auto& c = out.back();
        c.kind = type;
        c.target = target;
        geometry.fill(c, at);
        c.phi[13] = 0.0f;
        c.phi[14] = type == a_use_consumable ? 1.0f : 0.0f;
        c.phi[15] = type == a_ward ? 1.0f : 0.0f;
        c.phi[16] = 0.0f;
        c.phi[17] = 0.0f;
        c.phi[18] = type == a_use_consumable ? 1.0f : 0.0f;
        c.phi[19] = type == a_ward ? static_cast<float>(o.ward_charges) / 10.0f
                                 : static_cast<float>(o.consumables[1]) / 10.0f;
        c.phi[20] = price;
        c.phi[21] = radius;
        c.phi[22] = last;
        c.phi[23] = std::find(o.reserved_tiles.begin(), o.reserved_tiles.end(), at) != o.reserved_tiles.end() ? 1.0f : 0.0f;
    };
    if (type == a_ward)
    {
        for (Tile at : scene.ward_tiles)
            append(-1, at, static_cast<float>(0u) / 1000.0f, scene.rules.ward_radius / 20.0f,
                   static_cast<float>(o.ward_cooldown) / 40.0f);
    }
    else
    {
        for (const auto& lock : scene.lock_rows)
            append(1, lock.at, static_cast<float>(scene.rules.lock_price) / 1000.0f,
                   scene.rules.lock_radius / 20.0f, lock.enemy ? 1.0f : 0.0f);
    }
    canonicalize(out);
    if (out.empty()) return {ActionBuildStatus::mismatch, 0};
    return {partial ? ActionBuildStatus::truncated : ActionBuildStatus::ok, static_cast<int>(out.size())};
}

}  // namespace zero::student

#include "student_buy_fast.h"

namespace zero::student {

ActionBuild argument_candidates_buy_fast(const Context& ctx, ActionType type,
                                         std::vector<Cand>& out)
{
    if (type != a_buy) return argument_candidates(ctx, type, out);
    out.clear();
    const auto& o = *ctx.obs;
    if (!o.command_facts) return {ActionBuildStatus::unsupported, 0};
    if (!o.can_buy) return {ActionBuildStatus::unavailable, 0};
    const auto& cat = catalog();
    const int limit = std::max(0, std::min(cat.n_items, kMaxItems));
    out.reserve(static_cast<std::size_t>(limit));
    for (int item = 0; item < limit; ++item)
    {
        if (!cat.item(item).purchasable || !o.buyable_items[static_cast<std::size_t>(item)] || consumable(item))
            continue;
        out.emplace_back();
        auto& c = out.back();
        c.kind = 1;
        c.target = item;
        shop_facts(ctx, item, c);
    }
    // All rows have kind 1, one unique increasing item ID, and default tile/count.
    // Their existing canonical order is already established; no sort/dedup is needed.
    return {out.empty() ? ActionBuildStatus::unavailable : ActionBuildStatus::ok,
            static_cast<int>(out.size())};
}

}  // namespace zero::student
