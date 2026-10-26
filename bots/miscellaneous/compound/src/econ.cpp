// Shopping, recalls and banking: where gold turns into durability and gems into score.
#include <algorithm>
#include <cmath>

#include "brain.h"
#include "config.h"
#include "execution.h"

namespace cb {

void Brain::shop()
{
    if constexpr (cfg::shop_plan == 0)
    {
        shop_by_value();
        return;
    }
    auto& me = cambc::self();
    gold_ = static_cast<int>(me.gold_count());
    if (gold_ < 30) return;
    const auto counts = me.item_count();
    for (std::size_t i = 0; i < items_.size() && i < counts.size(); ++i) items_[i] = counts.ptr()[i];
    bool bought = false;
    for (int guard = 0; guard < 12 && gold_ >= 30; ++guard)
    {
        const int idx = cm::next_shard(plan_, items_);
        if (idx < 0) break;
        const cambc::item_type item{ static_cast<std::uint32_t>(idx) };
        const int cost = static_cast<int>(item.cost());
        if (cost <= 0 || gold_ < cost) break;
        // Buy up to the end of the current five-shard emblem in one call.
        const int owned = static_cast<int>(items_[static_cast<std::size_t>(idx)]);
        const int to_emblem = 5 - owned % 5;
        const int n = std::max(1, std::min(gold_ / cost, to_emblem));
        if (!me.can_buy(item, static_cast<std::uint32_t>(n))) break;
        me.buy(item, static_cast<std::uint32_t>(n));
        bought = true;
        gold_ -= n * cost;
        items_[static_cast<std::size_t>(idx)] += static_cast<std::uint32_t>(n);
        if (items_[static_cast<std::size_t>(idx)] >= 5)
        {
            items_[static_cast<std::size_t>(idx)] -= 5;
            items_[static_cast<std::size_t>(idx + 1)] += 1;
        }
    }
    if (bought && me.is_alive())
    {
        hp_ = me.health();
        max_hp_ = me.max_health();
        str_ = me.strength();
        focus_ = me.focus();
        update_derived_stats();
    }
}

void Brain::shop_by_value()
{
    // Buy the bundle with the highest marginal value per gold over the rest of the race
    // (econ_model.h best_purchase), as many of its shards as the purse allows; re-evaluate while
    // gold remains. Never runs out: there is always a best item.
    auto& me = cambc::self();
    gold_ = static_cast<int>(me.gold_count());
    if (gold_ < 30) return;
    if (!specs_ready_) build_camp_specs();
    const auto counts = me.item_count();
    for (std::size_t i = 0; i < items_.size() && i < counts.size(); ++i) items_[i] = counts.ptr()[i];
    bool bought = false;
    for (int guard = 0; guard < 2 && gold_ >= 30 && cambc::fuel_remaining() > 50000; ++guard)
    {
        std::array<std::array<int, 3>, 7> owned{};
        for (int f = 0; f < 7; ++f)
            for (int s = 0; s < 3; ++s)
                owned[static_cast<std::size_t>(f)][static_cast<std::size_t>(s)] =
                    static_cast<int>(items_[static_cast<std::size_t>(cm::shard_index(static_cast<cm::Family>(f), static_cast<cm::Stat>(s)))]) % 5;
        const bool focus_kit = (dash_ && dash_->modifier > 0.0f) || (shield_ && shield_->modifier > 0.0f) || (spell_ && spell_->targets_camps);
        const auto best = cm::best_purchase(profile(), camp_specs_, econ(), emblems_, owned, 30.0f, focus_kit);
        if (best.per_gold > 0.0f)
        {
            lambda_ = std::clamp(0.5f * lambda_ + 0.5f * best.per_gold, 0.02f, cfg::lambda_cap);
            purchase_rate_ = lambda_ / std::max(1.0f, static_cast<float>(race_left()));
        }
        const int idx = cm::shard_index(best.family, best.stat);
        const cambc::item_type item{ static_cast<std::uint32_t>(idx) };
        const int cost = static_cast<int>(item.cost());
        if (cost <= 0 || gold_ < cost) break;
        const int n = std::max(1, std::min(gold_ / cost, best.shards));
        if (!me.can_buy(item, static_cast<std::uint32_t>(n))) break;
        me.buy(item, static_cast<std::uint32_t>(n));
        bought = true;
        gold_ -= n * cost;
        items_[static_cast<std::size_t>(idx)] += static_cast<std::uint32_t>(n);
        if (items_[static_cast<std::size_t>(idx)] >= 5)
        {
            items_[static_cast<std::size_t>(idx)] -= 5;
            items_[static_cast<std::size_t>(idx + 1)] += 1;
        }
        if (me.is_alive())
        {
            max_hp_ = me.max_health();
            str_ = me.strength();
            focus_ = me.focus();
        }
        update_derived_stats();
    }
    if (bought && me.is_alive())
    {
        hp_ = me.health();
        max_hp_ = me.max_health();
        str_ = me.strength();
        focus_ = me.focus();
        update_derived_stats();
    }
}

bool Brain::safe_to_channel() const
{
    if (camp_threatens(pos_)) return false;
    for (const auto& e : enemies_)
        if (cw::cheb(e.pos, pos_) <= e.range + 4) return false;
    return !took_damage_;
}

bool Brain::want_recall(bool& for_shop)
{
    for_shop = false;
    if (on_spawn_) return false;
    auto& world = cw::World::get();
    const auto* sf = world.spawn_field(false);
    int home = cw::cheb(pos_, world.spawn_center);
    if (sf)
    {
        const auto d = sf->get(pos_);
        if (d != cambc::distance_field::unreachable) home = d;
    }
    const float frac = max_hp_ > 0.0f ? hp_ / max_hp_ : 1.0f;
    bool want = false;
    if (frac < cfg::recall_hp_frac) want = true;
    if constexpr (cfg::shop_plan == 0)
    {
        // A shop trip pays when the items it buys, arriving `advance` rounds before the next
        // forced (health) recall, add more than the farming the trip costs net of the health it
        // refills (econ_model.h shop_trip_worth; NOTES decision 2).
        if (!want && gold_ >= 30)
        {
            const float usable = std::max(1.0f, max_hp_ * 0.9f);
            const float advance = dmg_rate_ > 0.5f ? std::max(0.0f, hp_ - max_hp_ * cfg::recall_hp_frac) / dmg_rate_ : usable / 4.0f;
            const float trip = 5.0f + static_cast<float>(home);
            const float items_rate = purchase_rate_ * static_cast<float>(gold_ / 30 * 30);
            if (cm::shop_trip_worth(items_rate, advance, trip, farm_rate_cache_, frac)) want = for_shop = true;
        }
    }
    else
    {
        if (gold_ >= cfg::shop_recall_full) want = for_shop = true;
        if (gold_ >= cfg::shop_recall_gold && frac < cfg::shop_recall_hp) want = for_shop = true;
    }
    if (!want) return false;
    if (home <= cfg::walk_home_tiles)
    {
        // Walking home is about as fast as the channel and cannot be interrupted.
        task_ = Task::home;
        return false;
    }
    return true;
}

bool Brain::heal_trip()
{
    // Committed: get out of every threat's reach, then recall; ends when the recall starts, when
    // we stand on spawn, or when health is back (a heal from somewhere, or the threat was wrong).
    if (on_spawn_ || hp_ >= max_hp_ * 0.95f)
    {
        heal_trip_ = false;
        return false;
    }
    if (try_recall())
    {
        heal_trip_ = false;
        act('h');
        return true;
    }
    act('e');
    auto& world = cw::World::get();
    std::vector<vec2> threats;
    threats.reserve(threats_.size() + enemies_.size());
    for (const auto& e : enemies_) threats.push_back(e.pos);
    for (const auto& t : threats_) threats.push_back(t.tile);
    if (!threats.empty() && flee_from(threats, world.spawn_center)) return true;
    // Nothing to flee from (or no better tile): walk home, recalling as soon as it is safe.
    if (const auto* sf = world.spawn_field(true))
        if (step_field(*sf, false)) return true;
    return step_towards(world.spawn_center);
}

bool Brain::continue_home()
{
    if (on_spawn_)
    {
        task_ = Task::none;
        return false;
    }
    act('W');
    auto& world = cw::World::get();
    if (const auto* sf = world.spawn_field(true))
        if (step_field(*sf, false)) return true;
    if (step_towards(world.spawn_center)) return true;
    task_ = Task::none;   // blocked: let the other tasks run
    return false;
}

bool Brain::try_recall()
{
    if (!safe_to_channel()) return false;
    auto& me = cambc::self();
    if (!me.state().contains(cambc::champion_state::can_move) && !me.state().contains(cambc::champion_state::can_attack)) return false;
    me.recall();
    trace("recall", gold_, static_cast<int>(100.0f * hp_ / std::max(1.0f, max_hp_)));
    recall_started_ = round_;
    task_ = Task::home;
    target_ = -1;
    write_shared(cm::Mode::home, 0);
    return true;
}

int Brain::nearest_altar(int& dist) const
{
    auto& world = cw::World::get();
    int best = -1;
    dist = 1 << 20;
    for (std::size_t a = 0; a < world.altars.size(); ++a)
    {
        int d = cw::cheb(pos_, world.altars[a]);
        if (const auto* f = world.altar_field(static_cast<int>(a), false))
        {
            const auto v = f->get(pos_);
            if (v == cambc::distance_field::unreachable) continue;
            d = v;
        }
        if (d < dist)
        {
            dist = d;
            best = static_cast<int>(a);
        }
    }
    return best;
}

bool Brain::want_bank() const
{
    if (gems_ <= 0 || round_ < bank_block_until_) return false;
    int d = 0;
    if (nearest_altar(d) < 0) return false;
    if (round_ >= cfg::bag_late_round) return true;
    if (our_banked_ + gems_ >= 1000) return true;
    if (gems_ >= cfg::bag_gems) return true;
    if (d <= cfg::bag_detour) return true;
    // A full bag beside enemies is a liability: bank it while we can.
    if (gems_ >= 10 && !enemies_.empty() && d <= 12) return true;
    return false;
}

bool Brain::do_bank()
{
    auto& world = cw::World::get();
    auto& me = cambc::self();
    int d = 0;
    const int a = nearest_altar(d);
    if (a < 0) return false;
    const vec2 altar = world.altars[static_cast<std::size_t>(a)];
    task_ = Task::bank;
    if (cw::cheb(pos_, altar) <= std::max(1, cambc::constants::submit_gems_range))
    {
        const auto locks = cambc::vision::altar_lock_tiles(cambc::team::enemy);
        if (locks.is_set(altar))
        {
            if (me.can_attack())
            {
                const auto lock = cambc::vision::altar_lock_at(altar);
                if (lock.is_enemy())
                {
                    me.attack(lock);
                    return true;
                }
            }
            return true;
        }
        const auto st = me.state();
        if (st.contains(cambc::champion_state::submit_gems_started)) return true;
        if constexpr (cfg::bounded_turns)
        {
            if (!ce::try_start_tribute(me, altar)) return true; // keep the committed bank
        }
        else me.submit_gems(altar);
        trace("tribute", gems_, a);
        return true;
    }
    bool moved = false;
    if (const auto* f = world.altar_field(a, true)) moved = step_field(*f, false);
    if (!moved) moved = step_towards(altar);
    if (moved)
    {
        bank_stuck_ = 0;
        return true;
    }
    if (++bank_stuck_ > cfg::stuck_turns)
    {
        bank_stuck_ = 0;
        bank_block_until_ = round_ + 30;
        return false;
    }
    return true;   // blocked this turn (a body in the way): keep the task
}

}  // namespace cb
