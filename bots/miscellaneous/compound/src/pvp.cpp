// Champion fights. Durable farmers keep farming beside enemies that cannot kill them; kills come
// from converging on an isolated enemy (a beacon in shared memory pulls nearby allies in).
#include <algorithm>
#include <cmath>

#include "brain.h"
#include "config.h"

namespace cb {

bool Brain::isolated(const Enemy& e) const
{
    for (const auto& o : enemies_)
        if (o.handle != e.handle && cw::cheb(o.pos, e.pos) <= 6) return false;
    return true;
}

cm::SkirmishResult Brain::forecast_fight(const std::vector<Enemy>& foes, bool with_beacon_allies) const
{
    std::vector<cm::Fighter> fs;
    const auto join_round = [](int gap, int reach) { return 1 + std::max(0, gap - reach) / 2; };
    const auto nearest_foe = [&](vec2 p) {
        int d = 99;
        for (const auto& e : foes) d = std::min(d, cw::cheb(p, e.pos));
        return d;
    };
    cm::Fighter self;
    self.hp = hp_ / std::max(0.5f, taken_all_);
    self.dps = dps_champ_;
    self.value = 1.0f + static_cast<float>(gems_) / 40.0f;
    self.join = join_round(nearest_foe(pos_), range_);
    self.ours = true;
    fs.push_back(self);
    for (const auto& a : allies_)
    {
        cm::Fighter f;
        f.hp = a.hp;
        f.dps = a.dps;
        f.join = join_round(nearest_foe(a.pos), 1);
        f.ours = true;
        fs.push_back(f);
    }
    if (with_beacon_allies)
    {
        // Allies answering our beacon: unseen, assumed as strong as we are, arriving at their ETA.
        for (int i = 0; i < team_size_ && i < 5; ++i)
        {
            if (i == slot_) continue;
            const auto b = shared_[static_cast<std::size_t>(i)];
            if (cm::slot_mode(b) != cm::Mode::converge) continue;
            bool seen = false;
            for (const auto& a : allies_) if (a.slot == i) seen = true;
            if (seen) continue;
            cm::Fighter f;
            f.hp = max_hp_;
            f.dps = dps_champ_;
            f.join = 1 + cm::slot_value(b);
            f.ours = true;
            fs.push_back(f);
        }
    }
    for (const auto& e : foes)
    {
        cm::Fighter f;
        f.hp = e.hp;
        f.dps = e.dps;
        f.value = 1.0f + static_cast<float>(e.gems) / 40.0f;
        int gap = 99;
        for (const auto& x : fs) (void)x;
        gap = std::min(gap, cw::cheb(e.pos, pos_));
        for (const auto& a : allies_) gap = std::min(gap, cw::cheb(e.pos, a.pos));
        f.join = join_round(gap, e.range);
        f.ours = false;
        fs.push_back(f);
    }
    return cm::simulate_skirmish(fs, cfg::fight_horizon);
}

bool Brain::handle_enemies()
{
    if (enemies_.empty()) return false;
    auto& world = cw::World::get();
    std::vector<Enemy> near;
    int nearest = 99;
    for (const auto& e : enemies_)
    {
        const int d = cw::cheb(e.pos, pos_);
        if (d <= cfg::threat_radius) near.push_back(e);
        nearest = std::min(nearest, d);
    }
    if (near.empty()) return false;

    const auto sim = forecast_fight(near, true);
    trace("pvp", static_cast<int>(10.0f * sim.our_loss), static_cast<int>(10.0f * sim.their_loss));
    const bool we_lose_someone = sim.our_deaths > 0;
    const bool favourable = sim.their_loss - sim.our_loss >= cfg::fight_edge && sim.our_loss < 0.5f;
    const bool no_loss = sim.our_loss < 0.5f;

    // Focus: the attackable enemy with the least health.
    const Enemy* focus = nullptr;
    for (const auto& e : near)
        if (cw::cheb(e.pos, pos_) <= range_ && (!focus || e.hp < focus->hp)) focus = &e;

    // A lone enemy near us: call allies.
    if (favourable || no_loss)
    {
        for (const auto& e : near)
        {
            if (!isolated(e) || hp_ < max_hp_ * 0.5f) continue;
            if (round_ - beacon_published_ >= 3)
            {
                cm::Beacon b;
                b.x = e.pos.x;
                b.y = e.pos.y;
                b.round_mod = round_ & 63;
                b.asks = 2;
                const auto bytes = cm::encode_beacon(b);
                for (std::size_t i = 0; i < 3; ++i) cambc::shared_memory[5 + i] = bytes[i];
                beacon_published_ = round_;
            }
            break;
        }
    }

    if (favourable)
    {
        write_shared(cm::Mode::fight, 0);
        fight_start_round_ = round_;
        if (focus)
        {
            cast_spell_on_enemy(*focus);
            attack_enemy(*focus);
            return true;
        }
        // Close in on the weakest reachable enemy, never toward their spawn's surroundings.
        const Enemy* prey = nullptr;
        for (const auto& e : near)
        {
            if (cw::cheb(e.pos, world.enemy_center) <= cfg::chase_spawn_radius) continue;
            if (!prey || e.hp < prey->hp) prey = &e;
        }
        if (prey)
        {
            const int d = cw::cheb(prey->pos, pos_);
            if (d <= range_ + 3)
            {
                cast_spell_on_enemy(*prey);
                if (step_towards(prey->pos))
                {
                    auto& me = cambc::self();
                    if (me.can_attack() && cw::cheb(prey->pos, pos_) <= range_) attack_enemy(*prey);
                }
                return true;
            }
        }
        return false;
    }

    if (no_loss)
    {
        // Durable: keep farming. Strike back at an enemy in reach when nothing else needs our attack.
        if (focus && engaged_round_ < round_ - 1)
        {
            attack_enemy(*focus);
            return true;
        }
        return false;
    }

    // We would lose someone: leave before contact, strike while leaving if an enemy is in reach.
    if (we_lose_someone && nearest <= 6)
    {
        act('E');
        write_shared(cm::Mode::home, 0);
        std::vector<vec2> threats;
        for (const auto& e : near) threats.push_back(e.pos);
        auto& me = cambc::self();
        if (focus && !ranged_ && me.can_attack()) attack_enemy(*focus);
        if (flee_from(threats, world.spawn_center)) return true;
        if (focus && me.can_attack()) attack_enemy(*focus);
        return true;
    }
    return false;
}

bool Brain::handle_beacon()
{
    if (!beacon_.valid()) return false;
    if (round_ - beacon_published_ <= cfg::beacon_ttl) return false;   // our own beacon
    const vec2 at{ beacon_.x, beacon_.y };
    const int d = cw::cheb(at, pos_);
    if (d > cfg::beacon_radius || d <= 1) return false;
    if (hp_ < max_hp_ * 0.5f || task_ == Task::capture || task_ == Task::bank) return false;
    write_shared(cm::Mode::converge, std::min(63, d));
    task_ = Task::converge;
    trace("beacon", at.x, at.y);
    if (step_towards(at)) return true;
    return false;
}

}  // namespace cb
