// Farming: choose a camp group, clear it, capture it. Targets are scored in one currency
// (gems, with gold converted at the current worth of gold) per round of time, where time
// includes the share of a recall trip the fight's damage will cost us.
#include <algorithm>
#include <cmath>

#include "brain.h"
#include "config.h"
#include "lifecycle.h"
#include "target_bound.h"

namespace cb {

cm::MonsterSet Brain::believed_monsters(int g) const
{
    const auto& group = cw::World::get().groups[static_cast<std::size_t>(g)];
    const auto& b = gb_[static_cast<std::size_t>(g)];
    cm::MonsterSet ms;
    const bool respawned = b.alive_at > round_;
    for (std::size_t i = 0; i < group.members.size(); ++i)
    {
        const float hp = respawned ? group.members[i].hp : b.hp[i];
        if (hp > 0.0f) ms.add({ hp, group.members[i].str });
    }
    return ms;
}

bool Brain::is_claimed_by_ally(int g) const
{
    for (int i = 0; i < team_size_ && i < 5; ++i)
    {
        if (i == slot_) continue;
        const auto b = shared_[static_cast<std::size_t>(i)];
        if (cm::slot_mode(b) == cm::Mode::farm && cm::slot_value(b) == g + 1) return true;
    }
    return false;
}

float Brain::camp_incoming(vec2 at) const
{
    // Damage per round awake monsters in view would deal to a champion standing at `at`.
    float total = 0.0f;
    for (const auto& t : threats_)
        if (cw::cheb(t.tile, at) <= t.range) total += t.str;
    return total * taken_camp_;
}

bool Brain::camp_threatens(vec2 at) const { return camp_incoming(at) > 0.0f; }

int Brain::claimants(int g) const
{
    int n = 0;
    for (int i = 0; i < team_size_ && i < 5; ++i)
    {
        if (i == slot_) continue;
        const auto b = shared_[static_cast<std::size_t>(i)];
        if (cm::slot_mode(b) == cm::Mode::farm && cm::slot_value(b) == g + 1) ++n;
    }
    return n;
}

int Brain::party_present(int g) const
{
    const auto& group = cw::World::get().groups[static_cast<std::size_t>(g)];
    int n = 1;
    for (const auto& a : allies_)
    {
        if (a.slot < 0 || a.slot > 4 || cw::cheb(a.pos, group.anchor) > 6) continue;
        const auto b = shared_[static_cast<std::size_t>(a.slot)];
        if (cm::slot_mode(b) == cm::Mode::farm && cm::slot_value(b) == g + 1) ++n;
    }
    return n;
}

float Brain::score_group(int g, int travel, float hp_now, bool& needs_heal, int party) const
{
    needs_heal = false;
    auto& world = cw::World::get();
    const auto& group = world.groups[static_cast<std::size_t>(g)];
    const auto& b = gb_[static_cast<std::size_t>(g)];
    if (b.banned_until > round_) return -1.1f;
    if (world.spawn_field(false) && group.spawn_dist < 0) return -1.2f;  // not reachable on foot
    const int arrive = round_ + travel;
    const int wait = std::max(0, b.alive_at - arrive);
    if (wait > cfg::max_wait_rounds) return -1.3f;

    // A party of `party` champions like us (foundation E8): the damage rate multiplies, the
    // damage spreads over the pooled health (monsters strike the nearest; a tank that steps back
    // hands the strikes over, E4), the reward is shared and one member waits for the capture.
    const int m = std::max(1, party);
    const auto ms = believed_monsters(g);
    const auto fc = cm::forecast_camp(ms, std::max(1.0f, dps_camp_ * static_cast<float>(m)), taken_camp_);
    float damage = fc.damage / static_cast<float>(m);
    if (shield_) damage = std::max(0.0f, damage - shield_->modifier * focus_ * static_cast<float>(fc.rounds) / 8.0f);
    const float need = damage * cfg::camp_margin;
    const int home = group.spawn_dist >= 0 ? group.spawn_dist : travel;
    const float trip = static_cast<float>(cambc::constants::recall_time + 1 + home);   // recall, arrival turn, walk back
    const float capture_share = static_cast<float>(cambc::constants::capture_time + 1) / static_cast<float>(m);
    const float stage = m > 1 ? static_cast<float>(cfg::party_assembly_estimate) : 0.0f;
    const float usable = std::max(1.0f, max_hp_ * cfg::usable_share);
    if (need > usable * static_cast<float>(cfg::siege_trips)) return -1.4f;
    float time = static_cast<float>(travel + wait + fc.rounds) + capture_share + stage;
    // Health is time: each usable pool spent costs one heal trip (E4, E18), charged once.
    time += damage / usable * trip;
    if (need > hp_now)
    {
        if (need <= usable)
        {
            // Feasible on a full pool: heal first (the trip replaces the walk from here).
            needs_heal = true;
            time = trip + static_cast<float>(fc.rounds) + capture_share + stage + damage / usable * trip;
        }
        else
            needs_heal = hp_now < usable * 0.5f;
    }

    const float gold_w = gold_worth();
    float value = static_cast<float>(group.gems) + static_cast<float>(group.gold) * gold_mult_ * gold_w;
    if (type_ == cambc::champion_type::marcella) value += 2.0f * static_cast<float>(group.gems) * gold_w;
    value /= static_cast<float>(m);
    // An ally already on it: joining only pays while the party is short of its size.
    const int others = claimants(g);
    if (others > 0 && others + 1 > std::max(1, group.party_n)) value *= cfg::claim_penalty;
    return value / std::max(1.0f, time);
}

int Brain::travel_to(int g) const
{
    auto& world = cw::World::get();
    const auto& group = world.groups[static_cast<std::size_t>(g)];
    if (const auto* f = world.group_field(g, false))
    {
        const auto d = f->get(pos_);
        if (d == cambc::distance_field::unreachable) return -1;
        return std::max(0, static_cast<int>(d) - range_);
    }
    return std::max(0, cw::cheb(pos_, group.anchor) * 5 / 4 - range_);
}

void Brain::choose_target()
{
    auto& world = cw::World::get();
    // Upper bound per group (value over travel plus a minimum fight and capture), then the
    // full score for the best few in bound order: scoring every group costs too much fuel.
    const float gold_w = gold_worth();
    struct Cand { int g; int travel; float bound; };
    std::array<Cand, 16> top{};
    int n_top = 0;
    const int k_top = std::min(16, cfg::target_evals);
    for (const auto& group : world.groups)
    {
        if constexpr (cfg::bounded_turns)
            if (cambc::fuel_remaining() < 35000) break;
        const auto& b = gb_[static_cast<std::size_t>(group.id)];
        if constexpr (cfg::authoritative_groups)
            if (cl::groups()[static_cast<std::size_t>(group.id)].suppressed(round_)) continue;
        if (b.banned_until > round_) continue;
        const int travel = travel_to(group.id);
        if (travel < 0) continue;
        if (b.alive_at > round_ + travel + cfg::max_wait_rounds) continue;
        // Health-weighted progress of a siege: the believed share of the group's health left.
        float left = 0.0f;
        if (b.alive_at > round_) left = 1.0f;
        else
        {
            for (std::size_t i = 0; i < group.members.size(); ++i) left += b.hp[i];
            left /= std::max(1.0f, group.total_hp);
        }
        // Bound with the group's own best party (E8), capped by the team: big groups enter the
        // shortlist as party targets instead of being pruned as infeasible solos.
#if admissible_targets
            // The full caller offers solo even when the static base-stat party size is >1.
            // Its possible heal-first option replaces travel, and Marcella/hysteresis add value.
            // A static-party Smith estimate cannot certify that all those options are infeasible.
            float health = 0.0f;
            int represented = 0;
            for (std::size_t i = 0; i < group.members.size(); ++i)
            {
                const float hp = std::max(0.0f, b.alive_at > round_ ? group.members[i].hp : b.hp[i]);
                if (hp > 0.0f && represented++ < 32) health += hp; // exact MonsterSet prefix
            }
            float value = static_cast<float>(group.gems) + static_cast<float>(group.gold) * gold_mult_ * gold_w;
            if (type_ == cambc::champion_type::marcella) value += 2.0f * static_cast<float>(group.gems) * gold_w;
            const int home = group.spawn_dist >= 0 ? group.spawn_dist : travel;
            const float bound = cm::target_score_bound(value, health, dps_camp_, travel,
                cambc::constants::recall_time + 1 + home, cambc::constants::capture_time + 1, group.id == target_);
            const Cand c{ group.id, travel, bound };
#else
            const int m = cfg::parties ? std::clamp(group.party_n, 1, std::max(1, team_size_)) : 1;
            const cm::SmithConstants sc{ group.smith_w, group.total_str, group.total_hp };
            const float damage = cm::estimate_camp_damage(sc, std::max(1.0f, dps_camp_ * static_cast<float>(m)), taken_camp_, left)
                               / static_cast<float>(m);
            const float usable = std::max(1.0f, max_hp_ * cfg::usable_share);
            if (damage * cfg::camp_margin > usable * static_cast<float>(cfg::siege_trips)) continue;
            const int home = group.spawn_dist >= 0 ? group.spawn_dist : travel;
            const float value = (static_cast<float>(group.gems) + static_cast<float>(group.gold) * gold_mult_ * gold_w * 1.25f) / static_cast<float>(m);
            const float min_time = static_cast<float>(travel) + static_cast<float>(cambc::constants::capture_time + 1) / static_cast<float>(m)
                + group.total_hp * left / std::max(1.0f, dps_camp_ * static_cast<float>(m)) * 0.8f
                + damage / usable * static_cast<float>(cambc::constants::recall_time + 1 + home) * 0.8f;
            const Cand c{ group.id, travel, value / std::max(1.0f, min_time) };
#endif
        // keep the k best bounds in descending order (insertion)
        if (n_top < k_top) ++n_top;
        else if (c.bound <= top[static_cast<std::size_t>(n_top - 1)].bound) continue;
        int j = n_top - 1;
        while (j > 0 && top[static_cast<std::size_t>(j - 1)].bound < c.bound) { top[static_cast<std::size_t>(j)] = top[static_cast<std::size_t>(j - 1)]; --j; }
        top[static_cast<std::size_t>(j)] = c;
    }
    int best = -1;
    int best_party = 1;
    float best_score = 0.0f;
    bool best_heal = false;
    for (int i = 0; i < n_top; ++i)
    {
        if constexpr (cfg::bounded_turns)
            if (cambc::fuel_remaining() < 30000) break;
        const auto& c = top[static_cast<std::size_t>(i)];
        if (c.bound <= best_score) break;
        const auto& group = world.groups[static_cast<std::size_t>(c.g)];
        // Options: solo, or the group's party. A party already forming (an ally claims the group)
        // is joined at its current size plus us; founding one is discounted by the chance that
        // allies come (they decide alone, from their own options).
        const int others = claimants(c.g);
        int party = 1;
        float discount = 1.0f;
        if (cfg::parties && group.party_n > 1 && team_size_ > 1)
        {
            if (others > 0) party = std::min(group.party_n, others + 1);
            else
            {
                party = std::min(group.party_n, team_size_);
                discount = cfg::party_found_discount;
            }
        }
        for (const int m : { 1, party })
        {
            bool heal = false;
            float s = score_group(c.g, c.travel, hp_, heal, m);
            if (m > 1) s *= discount;
            if (s <= 0.0f) continue;
            if (c.g == target_) s *= 1.15f;   // hysteresis: switching costs the walk already made
            if (s > best_score)
            {
                best_score = s;
                best = c.g;
                best_party = m;
                best_heal = heal;
            }
            if (party == 1) break;
        }
    }
    if (best != target_) stage_since_ = -1;
    target_party_ = best_party;
    if (best != target_) capture_tile_member_ = -1;
    target_ = best;
    target_round_ = round_;
    task_ = best >= 0 ? Task::farm : Task::none;
    if (best >= 0 && best_heal) heal_trip_ = true;
    trace("target", best, static_cast<int>(best_score * 1000.0f));
}

bool Brain::stage_for_party(int g)
{
    // Wait one tile outside the reach of every living harmful member until the party is here
    // (allies in view claiming the group), at most party_stage_rounds; then fight alone if that is
    // still feasible, else give the group up for a while.
    auto& world = cw::World::get();
    const auto& group = world.groups[static_cast<std::size_t>(g)];
    if (target_party_ <= 1) return false;
    bool awake = false;
    for (const auto& m : group.members)
        if (awake_occ_.is_set(m.tile)) awake = true;
    if (awake) return false;                       // the fight has started: join it
    if (party_present(g) >= target_party_) return false;
    if (stage_since_ < 0) stage_since_ = round_;
    if (round_ - stage_since_ > cfg::party_stage_rounds)
    {
        bool heal = false;
        if (score_group(g, 0, hp_, heal, 1) > 0.0f && !heal)
        {
            target_party_ = 1;                     // solo is still feasible: go
            return false;
        }
        drop_target(cfg::ban_rounds / 2);
        return true;
    }
    act('w');
    int reach = 0;
    std::vector<vec2> threats;
    for (const auto& m : group.members)
        if (m.str > 0.0f && camp_occ_.is_set(m.tile))
        {
            threats.push_back(m.tile);
            reach = std::max(reach, cw::cheb(m.tile, pos_) <= m.range + 1 ? 1 : 0);
        }
    if (reach > 0 && flee_from(threats, world.spawn_center)) return true;
    if (world.group_at(pos_) >= 0) leave_camp_tiles();
    return true;
}

bool Brain::leave_camp_tiles()
{
    auto& world = cw::World::get();
    const int g = world.group_at(pos_);
    if (g < 0) return false;
    const auto anchor = world.groups[static_cast<std::size_t>(g)].anchor;
    Moves cands;
    std::array<vec2, 8> around{};
    const int n_around = neighbours(pos_, around);
    for (int k = 0; k < n_around; ++k)
    {
        const auto t = around[static_cast<std::size_t>(k)];
        if (!tile_free(t)) continue;
        const bool off = world.group_at(t) != g;
        cands.add(t, (off ? 0.0f : 10.0f) - static_cast<float>(cw::cheb(t, anchor)) + camp_incoming(t) * 0.01f);
    }
    if (cands.n == 0) return false;
    return step_score(cands);
}

bool Brain::capture(int g)
{
    auto& world = cw::World::get();
    const auto& group = world.groups[static_cast<std::size_t>(g)];
    task_ = Task::capture;
    if (world.group_at(pos_) == g) return true;   // stand on the tile until the reward arrives
    // Walk to the nearest member tile that is free.
    const auto* f = world.group_field(g, true);
    if (f && step_field(*f, false)) return true;
    vec2 best{ -1, -1 };
    for (const auto& m : group.members)
        if (tile_free(m.tile) && (best.x < 0 || cw::cheb(m.tile, pos_) < cw::cheb(best, pos_))) best = m.tile;
    if (best.x >= 0) return step_towards(best);
    return false;
}


bool Brain::fight_camp(int g)
{
    auto& world = cw::World::get();
    const auto& group = world.groups[static_cast<std::size_t>(g)];
    auto& b = gb_[static_cast<std::size_t>(g)];
    // Living members in view, and the one to strike: most strength per health first (Smith).
    int pick = -1;
    float pick_key = -1.0f;
    float hp_left = 0.0f;
    cm::MonsterSet alive;
    for (std::size_t i = 0; i < group.members.size(); ++i)
    {
        const auto& m = group.members[i];
        if (!camp_occ_.is_set(m.tile)) continue;
        const float hp = std::max(1.0f, b.hp[i]);
        hp_left += hp;
        alive.add({ hp, m.str });
        float key = m.str / hp;
        if (m.str <= 0.0f) key = -static_cast<float>(cw::cheb(m.tile, pos_)) * 1e-3f;   // harmless: nearest last
        if (key > pick_key)
        {
            pick_key = key;
            pick = static_cast<int>(i);
        }
    }
    if (pick < 0) return false;

    // Progress watchdog: an engagement that removes no camp health for a while cannot be won from
    // here (unreachable tiles, a blocked approach, an island); ban the group and move on.
    if (fight_group_ != g || hp_left < fight_hp_ - 0.5f)
    {
        fight_group_ = g;
        fight_hp_ = hp_left;
        fight_progress_round_ = round_;
    }
    else if (round_ - fight_progress_round_ > cfg::fight_watchdog)
    {
        b.banned_until = round_ + cfg::ban_rounds;
        fight_group_ = -1;
        target_ = -1;
        task_ = Task::none;
        return false;
    }

    // Disengage when the rest of the fight would cost more health than we have; the camp keeps
    // its damage, so this is a siege trip, not a loss.
    const auto fc = cm::forecast_camp(alive, std::max(1.0f, dps_camp_), taken_camp_);
    const float incoming = camp_incoming(pos_);
    if (fc.damage > 0.0f && hp_ < fc.damage + incoming * 2.0f && hp_ < incoming * 3.0f + max_hp_ * 0.15f)
    {
        heal_trip_ = true;
        return false;
    }

    engaged_round_ = round_;
    const auto tile = group.members[static_cast<std::size_t>(pick)].tile;
    auto& me = cambc::self();
    if constexpr (cfg::productive_dash)
    {
        if (try_productive_dash(tile, world.group_field(g, false), g))
        {
            // Landing damage can kill the old target. Re-query before packing the ordinary hit.
            int after = -1;
            float after_key = -1e30f;
            for (std::size_t i = 0; i < group.members.size(); ++i)
                if (camp_occ_.is_set(group.members[i].tile) && cw::cheb(group.members[i].tile, pos_) <= range_
                    && group.camps[i].is_visible())
                {
                    const float health = group.camps[i].health();
                    const auto& member = group.members[i];
                    const float key = member.str > 0.0f ? member.str / std::max(1.0f, health)
                        : -static_cast<float>(cw::cheb(member.tile, pos_)) * 1e-3f;
                    if (key > after_key) { after = static_cast<int>(i); after_key = key; }
                }
            if (after >= 0 && me.can_attack()) attack_camp(g, after);
            return true;
        }
    }
    const bool in_range = cw::cheb(tile, pos_) <= range_;
    if (in_range)
    {
        // Casts that exclude walking go first when we are already in place (M25: they combine with
        // the basic hit); an attack reset needs the attack spent first (M24).
        bool cast = cast_area_on_camps(g);
        if (!cast) cast = cast_spell_on_camp(g, pick);
        if (!cast) cast = cast_shield_if_hit();
        attack_camp(g, pick);
        return true;
    }
    // Out of range: a spell may still reach, otherwise walk into position (melee strike after).
    if (cast_spell_on_camp(g, pick)) return true;
    const bool moved = walk_to_attack_position(g, pick);
    if (cw::cheb(tile, pos_) <= range_ && (me.can_attack()
        || (cfg::move_reset && type_ == cambc::champion_type::vesper_vale && reset_ && me.can_cast()))) attack_camp(g, pick);
    else if (me.can_cast()) cast_shield_if_hit();
    return moved || cw::cheb(tile, pos_) <= range_;
}

void Brain::drop_target(int ban)
{
    if (target_ >= 0 && ban > 0) gb_[static_cast<std::size_t>(target_)].banned_until = round_ + ban;
    target_ = -1;
    task_ = Task::none;
    stuck_turns_ = 0;
}

void Brain::farm()
{
    auto& world = cw::World::get();
    auto& me = cambc::self();
    // Capture finished: the group now respawns.
    if (target_ >= 0)
    {
        auto& b = gb_[static_cast<std::size_t>(target_)];
        bool capture_done = task_ == Task::capture && gained_ && world.group_at(pos_) == target_;
        if constexpr (cfg::authoritative_groups)
        {
            const auto& observed = cl::groups()[static_cast<std::size_t>(target_)];
            capture_done = task_ == Task::capture && observed.is_respawning(round_);
            if (observed.suppressed(round_))
            {
                drop_target(0);
                if (world.group_at(pos_) >= 0 && leave_camp_tiles()) { act('w'); return; }
            }
        }
        if ((cfg::authoritative_groups ? target_ >= 0 : true) && capture_done)
        {
            if constexpr (!cfg::authoritative_groups)
            {
                b.alive_at = round_ + world.groups[static_cast<std::size_t>(target_)].cooldown;
                b.reward = false;
                for (std::size_t i = 0; i < b.hp.size(); ++i) b.hp[i] = world.groups[static_cast<std::size_t>(target_)].members[i].hp;
            }
            drop_target(0);
        }
        else if (b.alive_at > round_ + cfg::max_wait_rounds && !b.reward)
            drop_target(0);   // someone else took it; it respawns later
    }
    if (target_ >= 0)
    {
        // A target whose cached field cannot reach us is unreachable on foot (an island).
        if (const auto* f = world.group_field(target_, false))
            if (f->get(pos_) == cambc::distance_field::unreachable && world.group_at(pos_) != target_) drop_target(cfg::ban_rounds * 3);
    }

    // Commitment: keep the target while fighting or capturing; while travelling re-evaluate every
    // retarget_period rounds (choose_target applies hysteresis).
    const bool busy = task_ == Task::capture || (target_ >= 0 && engaged_round_ >= round_ - 1);
    if (target_ < 0 || (!busy && round_ - target_round_ >= cfg::retarget_period)) choose_target();
    if (heal_trip_) return;
    if (target_ < 0)
    {
        act('I');
        if (world.group_at(pos_) >= 0) leave_camp_tiles();
        write_shared(cm::Mode::farm, 0);
        return;
    }
    write_shared(cm::Mode::farm, target_ + 1);
    const auto& group = world.groups[static_cast<std::size_t>(target_)];
    const auto& b = gb_[static_cast<std::size_t>(target_)];

    // Alive members in view: fight. Cleared: capture. Otherwise walk there.
    bool any_alive_visible = false, all_visible_dead = true, any_visible = false;
    for (const auto& m : group.members)
    {
        if (!visible_.is_set(m.tile)) { all_visible_dead = false; continue; }
        any_visible = true;
        if (camp_occ_.is_set(m.tile)) { any_alive_visible = true; all_visible_dead = false; }
    }
    if (any_alive_visible)
    {
        if (cfg::parties && stage_for_party(target_)) return;
        if (target_ < 0) return;
        act('F');
        if (fight_camp(target_)) return;
        if (heal_trip_ || target_ < 0) return;
    }
    else if (any_visible && (cfg::authoritative_groups
        ? cl::groups()[static_cast<std::size_t>(target_)].can_capture(round_)
        : all_visible_dead && (b.reward || (b.alive_at <= round_ && cw::cheb(group.anchor, pos_) <= 2))))
    {
        // One capturer per group (E3): an ally already on a member tile takes it.
        bool ally_on = false;
        if (world.group_at(pos_) != target_)
            for (const auto& a : allies_)
                if (world.group_at(a.pos) == target_) ally_on = true;
        if (ally_on)
        {
            drop_target(cambc::constants::capture_time + 4);   // it pays out within six rounds (M14)
            choose_target();
            if (target_ < 0) return;
        }
        else
        {
            act('C');
            if (capture(target_)) return;
        }
    }
    else if (any_visible && all_visible_dead && b.alive_at > round_ && cw::cheb(group.anchor, pos_) <= range_ + 2)
    {
        // Respawning soon (within max_wait_rounds): wait beside it, never on its tiles (an
        // occupied spawn tile delays the respawn, M16).
        act('w');
        if (world.group_at(pos_) == target_) leave_camp_tiles();
        return;
    }
    if (world.group_at(pos_) >= 0 && world.group_at(pos_) != target_ && leave_camp_tiles()) return;
    // Travel.
    act('W');
    const auto* f = world.group_field(target_, true);
    bool moved = false;
    if (f) moved = step_field(*f, false);
    if (!moved) moved = step_towards(group.anchor);
    if (!moved && me.can_move())
    {
        if (++stuck_turns_ > cfg::stuck_turns) drop_target(cfg::ban_rounds / 2);
    }
    else
        stuck_turns_ = 0;
}

}  // namespace cb
