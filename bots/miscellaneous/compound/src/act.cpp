// Movement, attacks and ability casts. Every action is checked before it is attempted, so the
// engine never throws for a known-invalid action; a hidden occupant makes a move return false
// and the next candidate is tried.
#include <algorithm>
#include <cmath>

#include "brain.h"
#include "config.h"
#include "dash_plan.h"

namespace cb {

namespace {
constexpr std::array<vec2, 8> kDirs{ vec2{ 0, 1 }, vec2{ 1, 1 }, vec2{ 1, 0 }, vec2{ 1, -1 },
                                     vec2{ 0, -1 }, vec2{ -1, -1 }, vec2{ -1, 0 }, vec2{ -1, 1 } };
}

int Brain::neighbours(vec2 p, std::array<vec2, 8>& out) const
{
    const auto& world = cw::World::get();
    int n = 0;
    for (const auto d : kDirs)
    {
        const vec2 t{ p.x + d.x, p.y + d.y };
        if (world.in_bounds(t)) out[static_cast<std::size_t>(n++)] = t;
    }
    return n;
}

bool Brain::tile_free(vec2 t) const
{
    const auto& world = cw::World::get();
    if (!world.in_bounds(t) || world.impassable.is_set(t)) return false;
    for (int i = 0; i < n_revealed_; ++i)
        if (revealed_[static_cast<std::size_t>(i)] == t) return false;
    // Never step into the reach of a big monster we cannot see (it may be awake in a bush,
    // M12/M46); stepping within or out of a reach we already stand in is allowed.
    for (const auto& th : threats_)
        if (th.hidden && th.str >= 100.0f && cw::cheb(th.tile, t) <= th.range && cw::cheb(th.tile, pos_) > th.range) return false;
    if (champ_occ_.is_set(t)) return false;
    if (camp_occ_.is_set(t)) return false;
    // A camp tile out of view may hold a living camp: only step on camp tiles we see empty.
    if (world.group_at(t) >= 0 && !visible_.is_set(t)) return false;
    if (cambc::vision::has_teleport(t)) return false;
    return true;
}

bool Brain::step_score(const Moves& m)
{
    auto& me = cambc::self();
    std::array<bool, 9> tried{};
    for (int k = 0; k < m.n; ++k)
    {
        int best = -1;
        for (int i = 0; i < m.n; ++i)
            if (!tried[static_cast<std::size_t>(i)] && (best < 0 || m.score[static_cast<std::size_t>(i)] < m.score[static_cast<std::size_t>(best)])) best = i;
        if (best < 0 || !me.can_move()) return false;
        tried[static_cast<std::size_t>(best)] = true;
        const auto t = m.tile[static_cast<std::size_t>(best)];
        if (me.move_absolute(t))
        {
            pos_ = t;
            return true;
        }
        if (n_revealed_ < 8) revealed_[static_cast<std::size_t>(n_revealed_++)] = t;
    }
    return false;
}

bool Brain::step_field(const cambc::distance_field& f, bool allow_equal)
{
    bool dashed = false;
    if constexpr (cfg::productive_dash)
    {
        dashed = try_productive_dash(pos_, &f);
        if (dashed && !cfg::dash_follow_step) return true;
    }
    auto& me = cambc::self();
    if (!me.can_move()) return dashed;
    const auto here = f.get(pos_);
    Moves cands;
    std::array<vec2, 8> around{};
    const int n_around = neighbours(pos_, around);
    for (int k = 0; k < n_around; ++k)
    {
        const auto t = around[static_cast<std::size_t>(k)];
        if (!tile_free(t)) continue;
        const auto v = f.get(t);
        if (v == cambc::distance_field::unreachable) continue;
        if (here != cambc::distance_field::unreachable && (v > here || (v == here && !allow_equal))) continue;
        // Prefer tiles out of awake monsters' reach when the distance ties.
        cands.add(t, static_cast<float>(v) + 0.01f * camp_incoming(t));
    }
    if (cands.n == 0) return dashed;
    return step_score(cands) || dashed;
}

bool Brain::step_towards(vec2 goal)
{
    bool dashed = false;
    if constexpr (cfg::productive_dash)
    {
        dashed = goal != pos_ && try_productive_dash(goal);
        if (dashed && !cfg::dash_follow_step) return true;
    }
    auto& me = cambc::self();
    if (!me.can_move() || goal == pos_) return dashed;
    Moves cands;
    std::array<vec2, 8> around{};
    const int n_around = neighbours(pos_, around);
    const int here = cw::cheb(pos_, goal);
    for (int k = 0; k < n_around; ++k)
    {
        const auto t = around[static_cast<std::size_t>(k)];
        if (!tile_free(t)) continue;
        const int d = cw::cheb(t, goal);
        if (d > here) continue;
        const float ex = static_cast<float>(t.x - goal.x), ey = static_cast<float>(t.y - goal.y);
        cands.add(t, static_cast<float>(d) * 10.0f + std::sqrt(ex * ex + ey * ey) * 0.1f + 0.01f * camp_incoming(t));
    }
    if (cands.n == 0) return dashed;
    return step_score(cands) || dashed;
}

bool Brain::flee_from(const std::vector<vec2>& threats, vec2 home)
{
    auto& me = cambc::self();
    if (!me.can_move()) return false;
    auto& world = cw::World::get();
    const auto* sf = world.spawn_field(false);
    Moves cands;
    std::array<vec2, 8> around{};
    const int n_around = neighbours(pos_, around);
    const auto score_of = [&](vec2 t) {
        int nearest = 99;
        for (const auto th : threats) nearest = std::min(nearest, cw::cheb(t, th));
        float home_d = static_cast<float>(cw::cheb(t, home));
        if (sf)
        {
            const auto v = sf->get(t);
            if (v != cambc::distance_field::unreachable) home_d = static_cast<float>(v);
        }
        // Distance from the nearest threat dominates; walking home breaks ties; awake camps hurt.
        return -static_cast<float>(std::min(nearest, 8)) * 10.0f + home_d * 0.5f + camp_incoming(t) * 0.05f;
    };
    const float stay = score_of(pos_);
    for (int k = 0; k < n_around; ++k)
    {
        const auto t = around[static_cast<std::size_t>(k)];
        if (!tile_free(t)) continue;
        const float s = score_of(t);
        if (s >= stay) continue;
        cands.add(t, s);
    }
    if (cands.n == 0) return false;
    return step_score(cands);
}

bool Brain::cast_reset_and_attack(const cambc::unit& target)
{
    auto& me = cambc::self();
    if (!reset_ || !me.can_cast()) return false;
    const auto ab = me.abilities()[static_cast<std::size_t>(reset_->slot)];
    if (ab.cooldown() > 0) return false;
    if (me.can_attack()) return false;  // the reset is wasted while the attack is still available
    if (!ab.use()) return false;
    if (me.can_attack() && target.is_visible() && cw::cheb(target.position(), pos_) <= range_)
    {
        me.attack(target);
        last_attack_round_ = round_;
    }
    return true;
}

bool Brain::attack_camp(int g, int member)
{
    auto& me = cambc::self();
    const auto& group = cw::World::get().groups[static_cast<std::size_t>(g)];
    const auto& camp = group.camps[static_cast<std::size_t>(member)];
    const auto tile = group.members[static_cast<std::size_t>(member)].tile;
    if (!camp_occ_.is_set(tile) || cw::cheb(tile, pos_) > range_) return false;
    // Vision changes when we move: check the camp is still in view right before striking.
    if (!camp.is_visible()) return false;
    bool acted = false;
    if (me.can_attack())
    {
        me.attack(camp);
        last_attack_round_ = round_;
        acted = true;
    }
    if (camp.is_visible() && cast_reset_and_attack(camp)) acted = true;
    return acted;
}

bool Brain::cast_spell_on_camp(int g, int member)
{
    auto& me = cambc::self();
    if (!spell_ || !spell_->targets_camps || !me.can_cast()) return false;
    const auto& group = cw::World::get().groups[static_cast<std::size_t>(g)];
    const auto tile = group.members[static_cast<std::size_t>(member)].tile;
    if (!camp_occ_.is_set(tile) || static_cast<float>(cw::cheb(tile, pos_)) > spell_->range) return false;
    const auto ab = me.abilities()[static_cast<std::size_t>(spell_->slot)];
    if (ab.cooldown() > 0) return false;
    if (spell_->exclusive && !me.can_move()) return false;  // already walked this turn
    if (!group.camps[static_cast<std::size_t>(member)].is_visible()) return false;
    return ab.use(group.camps[static_cast<std::size_t>(member)]);
}

bool Brain::cast_area_on_camps(int g)
{
    auto& me = cambc::self();
    if (!area_ || !me.can_cast()) return false;
    const auto ab = me.abilities()[static_cast<std::size_t>(area_->slot)];
    if (ab.cooldown() > 0) return false;
    if (area_->exclusive && !me.can_move()) return false;
    const auto& group = cw::World::get().groups[static_cast<std::size_t>(g)];
    // Centre on the living member that covers most living members within radius one.
    int best = -1, best_count = 0;
    for (std::size_t i = 0; i < group.members.size(); ++i)
    {
        const auto t = group.members[i].tile;
        if (!camp_occ_.is_set(t) || static_cast<float>(cw::cheb(t, pos_)) > area_->range) continue;
        int count = 0;
        for (const auto& m : group.members)
            if (camp_occ_.is_set(m.tile) && cw::cheb(m.tile, t) <= 1 && m.str > 0.0f) ++count;
        if (count > best_count)
        {
            best_count = count;
            best = static_cast<int>(i);
        }
    }
    if (best < 0) return false;
    const auto centre = group.members[static_cast<std::size_t>(best)].tile;
    // Never catch ourselves or allies in our own stun? Stuns affect enemies/camps only.
    if (!group.camps[static_cast<std::size_t>(best)].is_visible()) return false;
    if (area_->targets_ground) return ab.use(centre);
    return ab.use(group.camps[static_cast<std::size_t>(best)]);
}

bool Brain::cast_shield_if_hit()
{
    auto& me = cambc::self();
    if (!shield_ || !me.can_cast() || !took_damage_) return false;
    const auto ab = me.abilities()[static_cast<std::size_t>(shield_->slot)];
    if (ab.cooldown() > 0) return false;
    if (shield_->exclusive && !me.can_move()) return false;
    return ab.use();
}

bool Brain::attack_enemy(const Enemy& e)
{
    auto& me = cambc::self();
    if (cw::cheb(e.pos, pos_) > range_) return false;
    if (!cambc::vision::has_champion(e.pos)) return false;
    const auto c = cambc::vision::champion_at(e.pos);
    if (!c.is_enemy()) return false;
    bool acted = false;
    if (me.can_attack())
    {
        me.attack(c);
        last_attack_round_ = round_;
        acted = true;
    }
    if (c.is_visible() && c.is_alive() && cast_reset_and_attack(c)) acted = true;
    return acted;
}

bool Brain::cast_spell_on_enemy(const Enemy& e)
{
    auto& me = cambc::self();
    if (!me.can_cast()) return false;
    const float d = static_cast<float>(cw::cheb(e.pos, pos_));
    const auto try_slot = [&](const std::optional<AbilitySlot>& s) -> bool {
        if (!s || !s->targets_enemies) return false;
        if (d > s->range) return false;
        if (s->exclusive && !me.can_move()) return false;
        const auto ab = me.abilities()[static_cast<std::size_t>(s->slot)];
        if (ab.cooldown() > 0) return false;
        if (s->targets_ground && (!s->targets_enemies || s->kind == cambc::ability_kind::cc)) return ab.use(e.pos);
        if (!cambc::vision::has_champion(e.pos)) return false;
        const auto c = cambc::vision::champion_at(e.pos);
        if (!c.is_enemy()) return false;
        return ab.use(c);
    };
    if (try_slot(stun_)) return true;
    if (try_slot(spell_)) return true;
    if (try_slot(area_)) return true;
    return false;
}

bool Brain::walk_to_attack_position(int g, int member)
{
    auto& me = cambc::self();
    if (!me.can_move()) return false;
    auto& world = cw::World::get();
    const auto& group = world.groups[static_cast<std::size_t>(g)];
    const auto tile = group.members[static_cast<std::size_t>(member)].tile;
    const auto* f = world.group_field(g, true);
    Moves cands;
    std::array<vec2, 8> around{};
    const int n_around = neighbours(pos_, around);
    const int here = cw::cheb(pos_, tile);
    for (int k = 0; k < n_around; ++k)
    {
        const auto t = around[static_cast<std::size_t>(k)];
        if (!tile_free(t)) continue;
        const int d = cw::cheb(t, tile);
        float s;
        if (d <= range_)
            s = -1000.0f + camp_incoming(t) * 0.1f - static_cast<float>(d);  // in range: least damage, farthest
        else if (f)
        {
            const auto v = f->get(t);
            if (v == cambc::distance_field::unreachable) continue;
            const auto vh = f->get(pos_);
            if (vh != cambc::distance_field::unreachable && v > vh) continue;
            s = static_cast<float>(v) * 10.0f + static_cast<float>(d) + camp_incoming(t) * 0.01f;
        }
        else
        {
            if (d > here) continue;
            s = static_cast<float>(d) * 10.0f + camp_incoming(t) * 0.01f;
        }
        cands.add(t, s);
    }
    if (cands.n == 0) return false;
    return step_score(cands);
}

bool Brain::try_productive_dash(vec2 goal, const cambc::distance_field* field, int group)
{
    if constexpr (!cfg::productive_dash)
    {
        (void)goal; (void)field; (void)group;
        return false;
    }
    else
    {
    // First supported kit is Heart. No visible champion contact: this arm is a productive
    // movement/camp executor, not an uncalibrated PvP dash or escape model.
    if (type_ != cambc::champion_type::heart_of_ruin || !dash_ || !enemies_.empty()
        || cambc::fuel_remaining() < 70000 || hp_ < max_hp_ * 0.4f) return false;
    auto& me = cambc::self();
    if (!me.is_alive() || !me.can_cast() || (dash_->exclusive && !me.can_move())) return false;
    const auto ability = me.abilities()[static_cast<std::size_t>(dash_->slot)];
    if (ability.cooldown() != 0 || !ability.can_target_ground()) return false;
    const int radius = std::clamp(static_cast<int>(dash_->range), 0, cd::radius_cap);
    if (radius < 2) return false;
    auto& world = cw::World::get();
    const auto holes = cambc::map::holes();
    auto blocked = cambc::map::walls() | cambc::map::altars() | world.enemy_spawn
        | champ_occ_ | camp_occ_ | cambc::vision::teleport_tiles();
    blocked.set(pos_, false);
    cd::Grid grid;
    grid.radius = radius;
    for (int y = -radius; y <= radius; ++y)
        for (int x = -radius; x <= radius; ++x)
        {
            const vec2 tile{ pos_.x + x, pos_.y + y };
            if (!world.in_bounds(tile) || blocked.is_set(tile)) continue;
            if (world.group_at(tile) >= 0 && !visible_.is_set(tile)) continue;
            const auto key = static_cast<std::size_t>(cd::index(x, y));
            grid.path[key] = true; // holes can be crossed
            grid.landing[key] = !holes.is_set(tile);
        }
    const auto reach = cd::flood(grid);
    const bool use_field = field && field->get(pos_) != cambc::distance_field::unreachable;
    const auto distance = [&](vec2 tile) {
        if (use_field)
        {
            const auto value = field->get(tile);
            if (value != cambc::distance_field::unreachable) return static_cast<int>(value);
            return 1 << 20;
        }
        return cw::cheb(tile, goal);
    };
    const int before_distance = distance(pos_);
    const float queried_damage = ability.damage_amount(); // conservative query credit; Heart passive-query calibration is unresolved (M29)
    const int home = group >= 0 && world.groups[static_cast<std::size_t>(group)].spawn_dist >= 0
        ? world.groups[static_cast<std::size_t>(group)].spawn_dist : cw::cheb(pos_, world.spawn_center);
    const float heal_trip = static_cast<float>(cambc::constants::recall_time + 1 + home);
    struct Nearby
    {
        int x, y;
        float str, hp;
        int range, group;
        bool visible_alive, awake;
    };
    std::array<Nearby, 512> nearby; // only the populated prefix is read; do not zero an 18K scratch array
    int nearby_count = 0;
    for (const auto& other : world.groups)
        for (std::size_t i = 0; i < other.members.size(); ++i)
        {
            if (cambc::fuel_remaining() < 55000) return false;
            const auto& member = other.members[i];
            if (cw::cheb(member.tile, pos_) > radius + std::max(member.range, static_cast<int>(std::ceil(dash_->radius)))) continue;
            const bool visible = visible_.is_set(member.tile);
            const bool alive = camp_occ_.is_set(member.tile);
            if (visible && !alive) continue;
            if (nearby_count >= static_cast<int>(nearby.size())) return false;
            // Unknown members can still be struck as collateral, or wake with our target group.
            // Include their conservative future reach even if they have never been seen awake.
            nearby[static_cast<std::size_t>(nearby_count++)] = { member.tile.x, member.tile.y, member.str,
                alive ? std::max(0.0f, gb_[static_cast<std::size_t>(other.id)].hp[i]) : member.hp,
                member.range, other.id, alive, alive && awake_occ_.is_set(member.tile) };
        }
    float best_score = 0.0f;
    vec2 best{ -1, -1 };
    bool search_done = false;
    for (int y = -radius; y <= radius && !search_done; ++y)
        for (int x = -radius; x <= radius; ++x)
        {
            if (cambc::fuel_remaining() < 45000) { search_done = true; break; }
            if (std::max(std::abs(x), std::abs(y)) < 2 || !reach.can_land(grid, x, y)) continue;
            const vec2 tile{ pos_.x + x, pos_.y + y };
            const int after_distance = distance(tile);
            if (after_distance >= (1 << 20)) continue;
            const int progress = before_distance - after_distance;
            bool unsafe = false, collateral = false;
            float damage = 0.0f;
            float incoming = 0.0f;
            for (const auto& threat : threats_)
                if (threat.hidden)
                {
                    if (cw::cheb(threat.tile, tile) <= threat.range) incoming += threat.str * taken_camp_;
                    if (threat.str >= 100.0f && cw::cheb(threat.tile, tile) <= threat.range
                        && cw::cheb(threat.tile, pos_) > threat.range) unsafe = true;
                }
            for (int i = 0; i < nearby_count; ++i)
            {
                if (cambc::fuel_remaining() < 45000) { search_done = true; break; }
                const auto& member = nearby[static_cast<std::size_t>(i)];
                const vec2 member_tile{ member.x, member.y };
                const bool hit = static_cast<float>(cw::cheb(member_tile, tile)) <= dash_->radius;
                if (hit && member.group != group) { collateral = true; break; }
                if (cw::cheb(member_tile, tile) <= member.range
                    && (member.awake || member.group == group)) incoming += member.str * taken_camp_;
                if (hit && member.group == group && member.visible_alive) damage += std::min(queried_damage, member.hp);
            }
            if (search_done) break; // never use an incomplete threat/benefit score
            if (unsafe || collateral || incoming * 3.0f > hp_ * 0.7f) continue;
            if (group < 0 && incoming > camp_incoming(pos_) + 0.01f) continue;
            const float clear_rounds = damage / std::max(1.0f, dps_camp_);
            const float health_rounds = incoming / std::max(1.0f, max_hp_ * cfg::usable_share) * heal_trip;
            const float score = static_cast<float>(progress) + clear_rounds - health_rounds;
            if (score > best_score + 0.01f) { best_score = score; best = tile; }
        }
    if (best.x < 0 || cambc::fuel_remaining() < 40000) return false;
    const bool cast = ability.use(best);
    // A false cast reveals a hidden obstruction; refresh before any ordinary fallback action.
    camp_occ_ = cambc::vision::camp_tiles();
    awake_occ_ = cambc::vision::awake_camp_tiles();
    champ_occ_ = cambc::vision::champion_tiles();
    visible_ = cambc::vision::visible_tiles();
    if (!cast) return false;
    pos_ = me.position();
    hp_ = me.health();
    max_hp_ = me.max_health();
    trace("dash", static_cast<int>(queried_damage), group);
    return true;
    }
}

}  // namespace cb
