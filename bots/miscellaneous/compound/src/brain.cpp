// Turn loop, observation, beliefs and the shared-memory protocol.
#include "brain.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <span>

#include "config.h"
#include "lifecycle.h"

namespace cb {

namespace {

volatile int noise_sink = 0;

float passive_magnitude(cambc::item_type emblem, float fallback)
{
    const auto m = emblem.passive_magnitude();
    return m > 0.0f ? m : fallback;
}

struct Passives
{
    float jungle{0.3f}, durability{0.1f}, treasure{0.2f}, monster{0.1f};
    bool loaded{};
};

Passives& passives()
{
    static Passives p;
    if (!p.loaded)
    {
        p.jungle = passive_magnitude(cambc::item_type::jungle_emblem_of_health, 0.3f);
        p.durability = passive_magnitude(cambc::item_type::durability_emblem_of_health, 0.1f);
        p.treasure = passive_magnitude(cambc::item_type::treasure_emblem_of_health, 0.2f);
        p.monster = passive_magnitude(cambc::item_type::monster_emblem_of_health, 0.1f);
        p.loaded = true;
    }
    return p;
}

bool has_emblem(const std::array<std::uint32_t, 42>& items, cm::Family f)
{
    for (int s = 0; s < 3; ++s)
        if (items[static_cast<std::size_t>(cm::emblem_index(f, static_cast<cm::Stat>(s)))] > 0) return true;
    return false;
}

}  // namespace

namespace {
// Fuel profile (debug_level >= 2): the costliest turn seen per phase, printed every 100 rounds.
struct Profile
{
    const char* names[16]{};
    std::uint64_t worst[16]{};
    int worst_round[16]{};
    int n{};
};
Profile g_profile;

template <class F>
void probe(int id, const char* name, int round, F&& f)
{
    if constexpr (cfg::debug_level == 2)
    {
        const auto before = cambc::fuel_remaining();
        f();
        const auto after = cambc::fuel_remaining();
        const auto used = before > after ? before - after : 0;
        g_profile.names[id] = name;
        if (used > g_profile.worst[id]) { g_profile.worst[id] = used; g_profile.worst_round[id] = round; }
        if (id >= g_profile.n) g_profile.n = id + 1;
    }
    else
    {
        (void)id;
        (void)name;
        (void)round;
        f();
    }
}
}  // namespace

void Brain::turn()
{
    act_ = '?';
    turn_body();
    if constexpr (cfg::debug_level == 3)
    {
        act_log_[static_cast<std::size_t>(act_n_++)] = act_;
        if (act_n_ >= 100 || round_ % 100 == 0) flush_activity();
    }
}

void Brain::flush_activity()
{
    act_log_[static_cast<std::size_t>(act_n_)] = 0;
    std::printf("A%d %d %s\n", slot_, round_, act_log_.data());
    act_n_ = 0;
}

void Brain::turn_body()
{
    round_ = static_cast<int>(cambc::round_number());
    if (!inited_)
    {
        if constexpr (cfg::bounded_turns)
        {
            auto& world = cw::World::get();
            world.init();
            if (!world.ready) return;
        }
        probe(0, "init", round_, [&] { first_turn(); });
        // The first gameplay turn has a double budget, mostly spent on static data: act from the
        // next turn unless plenty is left (a suspended turn resumes with stale locals).
        if (cambc::fuel_remaining() < 110000) return;
    }
    if constexpr (cfg::noise_burn > 0)
        for (int i = 0; i < cfg::noise_burn; ++i) noise_sink = noise_sink + i;
    if constexpr (cfg::debug_level == 2)
        if (round_ % 200 == 0 && slot_ == 0)
        {
            for (int i = 0; i < g_profile.n; ++i)
                if (g_profile.names[i]) std::printf("F %s %llu@r%d\n", g_profile.names[i], static_cast<unsigned long long>(g_profile.worst[i]), g_profile.worst_round[i]);
            return;
        }

    auto& me = cambc::self();
    if (!me.is_alive())
    {
        act('D');
        shop();
        write_shared(cm::Mode::home, 0);
        task_ = Task::none;
        target_ = -1;
        last_hp_ = -1;
        return;
    }
    probe(1, "snapshot", round_, [&] { snapshot(); });
    const auto state = me.state();
    if (state.contains(cambc::champion_state::recalling) || state.contains(cambc::champion_state::teleporting))
    {
        act('R');
        write_shared(cm::Mode::home, 0);
        return;
    }
    probe(3, "enemies", round_, [&] { observe_enemies(); });
    probe(2, "camps", round_, [&] { observe_camps(); });
    probe(4, "allies", round_, [&] { observe_allies(); });
    read_shared();
    update_threat();
    if constexpr (cfg::shop_plan == 0 && !cfg::bounded_turns)
        if (round_ % 40 == (slot_ * 7) % 40 || !specs_ready_) probe(12, "lambda", round_, [&] { update_lambda(); });
    if (state.contains(cambc::champion_state::submitting_gems))
    {
        act('T');
        write_shared(cm::Mode::farm, target_ >= 0 ? target_ + 1 : 0);
        return;
    }

    if (on_spawn_) probe(5, "shop", round_, [&] { shop(); });
    if constexpr (cfg::bounded_turns)
        if (cambc::fuel_remaining() < 45000) return; // shop valuation cannot borrow the next round
    // Stay on spawn for the turn end that heals us, unless an enemy stands next to the spawn.
    if (on_spawn_ && hp_ < max_hp_ - 1.0f && enemies_.empty())
    {
        act('S');
        write_shared(cm::Mode::home, 0);
        return;
    }

    // Decision order: safety first (enemies), then committed tasks (heal trip, bank, home), then
    // new decisions (beacon, recall, bank, farm). A committed task runs until its completion or an
    // interrupt condition; nothing is re-decided from scratch every turn (re-deciding flipped
    // between two tasks on alternate turns, 1,000 rounds in one game).
    bool acted = false;
    probe(6, "pvp", round_, [&] { acted = handle_enemies(); });
    if (acted) { if (act_ == '?') act('P'); }
    if (!acted && heal_trip_) acted = heal_trip();
    if (!acted && task_ == Task::bank)
    {
        if (gems_ <= 0) task_ = Task::none;
        else probe(9, "bank", round_, [&] { acted = do_bank(); if (acted) act('B'); else task_ = Task::none; });
    }
    if (!acted && task_ == Task::home) acted = continue_home();
    if (!acted) probe(7, "beacon", round_, [&] { acted = handle_beacon(); if (acted) act('V'); });
    if (!acted)
    {
        bool for_shop = false;
        probe(8, "recall", round_, [&] {
            if (want_recall(for_shop))
            {
                acted = try_recall();
                if (acted) act(for_shop ? 'G' : 'H');
                else if (task_ != Task::home) { heal_trip_ = true; acted = heal_trip(); }
            }
        });
    }
    if (!acted && task_ == Task::home) acted = continue_home();
    if (!acted && want_bank())
    {
        task_ = Task::bank;
        probe(9, "bank", round_, [&] { acted = do_bank(); if (acted) act('B'); else task_ = Task::none; });
    }
    if (!acted) probe(10, "farm", round_, [&] { farm(); });
    if (!acted && heal_trip_ && act_ != 'e' && act_ != 'h') heal_trip();

    // Monsters strike the nearest champion and break ties by turn order (M6): the toughest of us
    // acts first, so in a party it is the one struck at equal distance.
    if (cfg::parties && round_ % 20 == 3)
        cambc::self().set_priority(static_cast<int>(std::clamp(max_hp_ / std::max(0.1f, taken_all_) / 10.0f, 0.0f, 10000.0f)));

    // Optional repricing uses what actions leave, instead of consuming their execution reserve.
    if constexpr (cfg::shop_plan == 0 && cfg::bounded_turns)
        if (round_ % 40 == (slot_ * 7) % 40 || !specs_ready_) probe(12, "lambda", round_, [&] { update_lambda(); });

    // Spare fuel goes to the static fields (spawn, altars), so later turns can use them.
    if (cambc::fuel_remaining() > 70000) probe(11, "warm", round_, [&] { cw::World::get().warm(55000); });
    last_pos_ = pos_;
    if constexpr (cfg::debug_level == 1)
        if (round_ % 50 == static_cast<int>(slot_) % 50) trace("lambda", static_cast<int>(lambda_ * 1000.0f), race_left());
}

void Brain::trace(const char* what, int a, int b) const
{
    if constexpr (cfg::debug_level == 1)
    {
        std::printf("T r%d s%d t%d %s %d %d | task%d tgt%d (%d,%d) hp%d/%d g%d m%d dps%d fuel%d\n", round_, slot_, static_cast<int>(type_), what, a, b,
                    static_cast<int>(task_), target_, pos_.x, pos_.y, static_cast<int>(hp_), static_cast<int>(max_hp_), gold_, gems_,
                    static_cast<int>(dps_camp_), static_cast<int>(cambc::fuel_remaining()));
    }
    else
    {
        (void)what;
        (void)a;
        (void)b;
    }
}

void Brain::first_turn()
{
    if constexpr (!cfg::bounded_turns) inited_ = true;
    auto& world = cw::World::get();
    const auto fuel0 = cambc::fuel_remaining();
    world.init();
    const auto fuel1 = cambc::fuel_remaining();
    auto& me = cambc::self();
    handle_ = static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(me));
    for (const auto& c : cambc::champions(cambc::team::ally))
        ally_handles_.push_back(static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(c)));
    team_size_ = static_cast<int>(ally_handles_.size());
    for (int i = 0; i < team_size_; ++i)
        if (ally_handles_[static_cast<std::size_t>(i)] == handle_) slot_ = i;
    type_ = me.champion_name();
    switch (type_)
    {
    case cambc::champion_type::vesper_vale: role_ = cm::Role::vesper; break;
    case cambc::champion_type::heart_of_ruin: role_ = cm::Role::heart; break;
    case cambc::champion_type::ryth:
    case cambc::champion_type::marcella:
    case cambc::champion_type::vendetta:
    case cambc::champion_type::varg:
    case cambc::champion_type::ren: role_ = cm::Role::tank; break;
    default: role_ = cm::Role::other; break;
    }
    const auto base = me.base_stats();
    growth_ = { base.strength_growth, base.focus_growth, base.health_growth };
    base_move_ = static_cast<float>(std::max(1, base.base_move_speed));
    camp_mult_design_ = base.camp_damage_multiplier > 0.0f ? base.camp_damage_multiplier : 1.0f;
    ranged_ = base.base_attack_range > 1;

    for (const auto& ab : me.abilities())
    {
        const auto& info = ab.info();
        if (!info) continue;
        AbilitySlot s;
        s.slot = static_cast<int>(ab.slot());
        std::visit([&](const auto& v) {
            s.kind = v.kind;
            s.exclusive = v.exclusive_with_movement;
            s.targets_camps = v.valid_targets.contains(cambc::ability_target_mask::camps);
            s.targets_enemies = v.valid_targets.contains(cambc::ability_target_mask::enemies);
            s.targets_ground = v.valid_targets.contains(cambc::ability_target_mask::ground);
            s.targets_self = v.valid_targets.contains(cambc::ability_target_mask::self);
            if constexpr (requires { v.range; }) s.range = v.range;
            if constexpr (requires { v.modifier; }) s.modifier = v.modifier;
            if constexpr (requires { v.radius; }) s.radius = v.radius;
        }, *info);
        switch (s.kind)
        {
        case cambc::ability_kind::attack_reset: reset_ = s; break;
        case cambc::ability_kind::damage: spell_ = s; break;
        case cambc::ability_kind::aoe_damage: area_ = s; break;
        case cambc::ability_kind::cc:
            // Ground-targeted crowd control (Ryth) acts as an area hit; unit-targeted as a stun.
            if (s.targets_ground) area_ = s;
            else stun_ = s;
            break;
        case cambc::ability_kind::dash: dash_ = s; break;
        case cambc::ability_kind::shield: shield_ = s; break;
        default: break;
        }
    }
    plan_ = cm::shop_plan(cfg::shop_plan, role_);

    gb_.resize(world.groups.size());
    if constexpr (cfg::authoritative_groups) cl::groups().resize(world.groups.size());
    member_hp_.assign(world.all_members.size(), 0.0f);
    member_awake_.assign(world.all_members.size(), -1000);
    for (std::size_t g = 0; g < world.groups.size(); ++g)
    {
        const auto& grp = world.groups[g];
        gb_[g].hp = std::span<float>{ member_hp_.data() + grp.first, grp.members.size() };
        for (std::size_t i = 0; i < grp.members.size(); ++i) gb_[g].hp[i] = grp.members[i].hp;
    }
    {
        const auto& pm = passives();
        emblems_.jungle = pm.jungle;
        emblems_.durability = pm.durability;
        emblems_.treasure = pm.treasure;
        emblems_.monster = pm.monster;
        emblems_.home = passive_magnitude(cambc::item_type::home_emblem_of_health, 0.5f);
        // Threat prior: the two strongest enemy kits at base stats (their designs are known from
        // the draft); observation raises it as their items grow.
        float a = 0.0f, b = 0.0f;
        for (const auto& c : cambc::champions(cambc::team::enemy))
        {
            const auto design = cambc::champion{ c.champion_name() };
            const auto s = design.base_stats();
            const float d = estimate_enemy_dps(design, s.base_strength, s.base_focus);
            if (d > a) { b = a; a = d; }
            else if (d > b) b = d;
        }
        threat_prior_ = a + b;
        threat_dps_ = 0.5f * threat_prior_;
    }
    world.field_limit = 4;   // the first gameplay turn has a double budget
    world.warm(110000);
    world.field_limit = 1;
    if constexpr (cfg::debug_level == 2)
        std::printf("I start %llu init %llu rest %llu left %llu\n", static_cast<unsigned long long>(fuel0),
                    static_cast<unsigned long long>(fuel0 - fuel1), static_cast<unsigned long long>(fuel1 - cambc::fuel_remaining()),
                    static_cast<unsigned long long>(cambc::fuel_remaining()));
    (void)fuel0;
    (void)fuel1;
    if constexpr (cfg::bounded_turns) inited_ = true;
}

void Brain::snapshot()
{
    auto& me = cambc::self();
    pos_ = me.position();
    hp_ = me.health();
    max_hp_ = me.max_health();
    str_ = me.strength();
    focus_ = me.focus();
    range_ = me.attack_range();
    gold_ = static_cast<int>(me.gold_count());
    gems_ = static_cast<int>(me.gem_count());
    const auto counts = me.item_count();
    for (std::size_t i = 0; i < items_.size() && i < counts.size(); ++i) items_[i] = counts.ptr()[i];
    on_spawn_ = cw::World::get().ally_spawn.is_set(pos_);
    took_damage_ = last_hp_ >= 0.0f && hp_ < last_hp_ - 0.5f;
    hp_loss_ = last_hp_ >= 0.0f ? std::max(0.0f, last_hp_ - hp_) : 0.0f;
    // Damage a round while away from spawn (the rate at which health runs out between recalls).
    if (last_hp_ >= 0.0f && !on_spawn_) dmg_rate_ = 0.97f * dmg_rate_ + 0.03f * std::max(0.0f, last_hp_ - hp_);
    last_hp_ = hp_;
    gained_ = last_gems_ >= 0 && (gems_ > last_gems_ || gold_ > last_gold_);
    last_gems_ = gems_;
    last_gold_ = gold_;
    update_derived_stats();
    our_banked_ = static_cast<int>(cambc::global_submitted_gems(cambc::team::ally));
    their_banked_ = static_cast<int>(cambc::global_submitted_gems(cambc::team::enemy));
}

void Brain::update_derived_stats()
{
    const auto& p = passives();
    taken_all_ = has_emblem(items_, cm::Family::durability) ? 1.0f - p.durability : 1.0f;
    taken_camp_ = taken_all_ * (has_emblem(items_, cm::Family::jungle) ? 1.0f - p.jungle : 1.0f);
    gold_mult_ = 1.0f + (role_ == cm::Role::vesper ? 0.33f : 0.0f) + (has_emblem(items_, cm::Family::treasure) ? p.treasure : 0.0f);

    cm::Kit k;
    k.str = str_;
    k.focus = focus_;
    k.ranged = ranged_;
    k.attack_reset = reset_.has_value();
    if (spell_ && spell_->targets_camps) k.spell_mod = spell_->modifier;
    if (area_ && area_->targets_camps) k.aoe_mod = area_->modifier;
    if (dash_ && dash_->modifier > 0.0f) k.dash_mod = dash_->modifier;
    if (shield_) k.shield_mod = shield_->modifier;
    k.kill_reset = type_ == cambc::champion_type::vendetta;
    k.splash = type_ == cambc::champion_type::varg;
    k.camp_mult = camp_mult_design_ * (has_emblem(items_, cm::Family::monster) ? 1.0f + p.monster : 1.0f);
    dps_camp_ = cm::camp_dps(k, 1);

    // Against champions: basic attacks plus reset and damage spells once per cycle.
    float cycle = str_ * 8.0f;
    if (reset_) cycle += str_;
    if (spell_ && spell_->targets_enemies) cycle += spell_->modifier * focus_;
    if (area_ && area_->targets_enemies) cycle += area_->modifier * focus_;
    if (dash_ && dash_->modifier > 0.0f) cycle += dash_->modifier * focus_;
    if (stun_ && stun_->targets_enemies) cycle += stun_->modifier * focus_;
    dps_champ_ = cycle / 8.0f;
}

float Brain::estimate_enemy_dps(const cambc::champion& c, float str, float focus) const
{
    float cycle = str * 8.0f;
    for (const auto& ab : c.abilities())
    {
        const auto& info = ab.info();
        if (!info) continue;
        std::visit([&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, cambc::ability_attack_reset>) cycle += str;
            else if constexpr (std::is_same_v<T, cambc::ability_damage>) cycle += v.modifier * focus;
            else if constexpr (std::is_same_v<T, cambc::ability_aoe_damage>) cycle += v.modifier * focus;
            else if constexpr (std::is_same_v<T, cambc::ability_cc>) cycle += v.modifier * focus;
            else if constexpr (std::is_same_v<T, cambc::ability_dash>) cycle += v.modifier * focus;
        }, *info);
    }
    return cycle / 8.0f;
}

void Brain::observe_enemies()
{
    enemies_.clear();
    const auto tiles = cambc::vision::champion_tiles(cambc::team::enemy);
    if (tiles.is_empty()) return;
    for (const auto p : tiles.positions())
    {
        if (cw::cheb(p, pos_) > 9) continue;
        const auto c = cambc::vision::champion_at(p);
        Enemy e;
        e.pos = p;
        e.hp = c.health();
        e.max_hp = c.max_health();
        e.str = c.strength();
        e.focus = c.focus();
        e.range = c.attack_range();
        e.ranged = e.range > 1;
        e.type = c.champion_name();
        e.handle = static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(c));
        e.gems = static_cast<int>(c.gem_count());
        e.dps = estimate_enemy_dps(c, e.str, e.focus);
        e.attackable = cw::cheb(p, pos_) <= range_;
        enemies_.push_back(e);
        if (enemies_.size() >= 6) break;
    }
}

void Brain::observe_allies()
{
    allies_.clear();
    const auto tiles = cambc::vision::champion_tiles(cambc::team::ally);
    if (tiles.count() <= 1) return;
    for (const auto p : tiles.positions())
    {
        if (p == pos_ || cw::cheb(p, pos_) > 9) continue;
        const auto c = cambc::vision::champion_at(p);
        Ally a;
        a.pos = p;
        a.hp = c.health();
        a.max_hp = c.max_health();
        a.dps = estimate_enemy_dps(c, c.strength(), c.focus());
        const auto h = static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(c));
        for (int i = 0; i < team_size_; ++i)
            if (ally_handles_[static_cast<std::size_t>(i)] == h) a.slot = i;
        allies_.push_back(a);
    }
}

void Brain::observe_camps()
{
    auto& world = cw::World::get();
    camp_occ_ = cambc::vision::camp_tiles();
    awake_occ_ = cambc::vision::awake_camp_tiles();
    visible_ = cambc::vision::visible_tiles();
    champ_occ_ = cambc::vision::champion_tiles();
    threats_.clear();
    n_revealed_ = 0;
    for (auto& g : world.groups)
    {
        if (cw::cheb(g.anchor, pos_) > 11)
        {
            if constexpr (cfg::authoritative_groups)
            {
                bool member_visible = false;
                for (const auto& member : g.members)
                    if (visible_.is_set(member.tile)) { member_visible = true; break; }
                if (!member_visible) continue;
            }
            else continue;
        }
        if (g.harmful)
        {
            const auto& gbel = gb_[static_cast<std::size_t>(g.id)];
            const bool believed_alive = gbel.alive_at <= round_;
            for (std::size_t mi = 0; mi < g.members.size(); ++mi)
            {
                const auto& m = g.members[mi];
                if (m.str <= 0.0f) continue;
                auto& awake = member_awake_[static_cast<std::size_t>(g.first) + mi];
                if (awake_occ_.is_set(m.tile))
                {
                    awake = round_;
                    threats_.push_back({ m.tile, m.str, m.range, false });
                }
                else if (believed_alive && gbel.hp[mi] > 0.0f && !visible_.is_set(m.tile))
                {
                    // Out of view (bushes hide camps from champions, never champions from camps,
                    // M12/M46). Proximity never wakes a camp (M9), so an unseen monster counts only
                    // with evidence it is awake: seen awake lately, or health lost in its reach
                    // that nothing in view explains.
                    if (took_damage_ && enemies_.empty() && cw::cheb(m.tile, pos_) <= m.range) awake = round_;
                    if (round_ - awake <= cfg::hidden_awake_memory) threats_.push_back({ m.tile, m.str, m.range, true });
                }
            }
        }
        auto& b = gb_[static_cast<std::size_t>(g.id)];
        bool any_visible = false, any_alive = false;
        for (std::size_t i = 0; i < g.members.size(); ++i)
        {
            const auto t = g.members[i].tile;
            if (!visible_.is_set(t)) continue;
            any_visible = true;
            if (camp_occ_.is_set(t))
            {
                any_alive = true;
                b.hp[i] = g.camps[i].health();
            }
            else
                b.hp[i] = 0.0f;
        }
        if (!any_visible) continue;
        b.seen = round_;
        if (any_alive)
        {
            if constexpr (cfg::authoritative_groups) cl::groups()[static_cast<std::size_t>(g.id)] = { cl::Phase::alive, round_, round_ };
            b.alive_at = std::min(b.alive_at, round_);
            b.reward = false;
            continue;
        }
        // Every visible member is dead: reward waiting, or respawning.
        try
        {
            const auto st = world.sdk_groups[static_cast<std::size_t>(g.id)].state();
            if (std::holds_alternative<cambc::map::camp_group_reward_available>(st))
            {
                if constexpr (cfg::authoritative_groups) cl::groups()[static_cast<std::size_t>(g.id)] = { cl::Phase::reward, round_, round_ };
                b.reward = true;
                b.alive_at = round_;
            }
            else if (const auto* r = std::get_if<cambc::map::camp_group_respawning>(&st))
            {
                if constexpr (cfg::authoritative_groups) cl::groups()[static_cast<std::size_t>(g.id)] = cl::observed_respawn(round_, r->timer);
                b.reward = false;
                b.alive_at = round_ + std::max(cfg::authoritative_groups ? 1 : 0, r->timer);
                for (std::size_t i = 0; i < g.members.size(); ++i) b.hp[i] = g.members[i].hp;
            }
            else
            {
                if constexpr (cfg::authoritative_groups) cl::groups()[static_cast<std::size_t>(g.id)] = { cl::Phase::alive, round_, round_ };
                // alive but none visible alive: hidden members still stand
                b.alive_at = std::min(b.alive_at, round_);
                b.reward = false;
            }
        }
        catch (const cambc::error&)
        {
        }
    }
}

void Brain::read_shared()
{
    std::array<std::byte, 8> raw{};
    cambc::shared_memory.read(0, std::span<std::byte>{ raw });
    for (std::size_t i = 0; i < 8; ++i) shared_[i] = std::to_integer<std::uint8_t>(raw[i]);
    beacon_ = cm::decode_beacon({ shared_[5], shared_[6], shared_[7] });
    if (beacon_.valid() && cm::beacon_age(round_, beacon_.round_mod) > cfg::beacon_ttl) beacon_ = cm::Beacon{};
}

void Brain::write_shared(cm::Mode mode, int value)
{
    if (slot_ < 0 || slot_ > 4) return;
    const auto b = cm::encode_slot(mode, value);
    if (shared_[static_cast<std::size_t>(slot_)] == b && round_ > 1) return;
    cambc::shared_memory[static_cast<std::size_t>(slot_)] = b;
    shared_[static_cast<std::size_t>(slot_)] = b;
}

int Brain::race_left() const
{
    if constexpr (cfg::shop_plan == 0)
    {
        // Our pace is the gem rate a gem-focused plan achieves (econ_model.h race_horizon); theirs
        // is their observed banking rate once they have banked for a while.
        const float their_rate = round_ > 150 ? static_cast<float>(their_banked_) / static_cast<float>(round_) : 0.0f;
        return static_cast<int>(cm::race_horizon(round_, static_cast<int>(cambc::constants::max_rounds),
                                                 static_cast<int>(cambc::constants::winning_gems), our_banked_, their_banked_,
                                                 plan_gem_rate_, their_rate));
    }
    const float r = static_cast<float>(std::max(1, round_));
    return cm::race_rounds_left(round_, static_cast<int>(cambc::constants::max_rounds), our_banked_, their_banked_,
                                static_cast<float>(our_banked_) / r, static_cast<float>(their_banked_) / r, cfg::race_rounds_default);
}

float Brain::gold_worth() const
{
    if constexpr (cfg::shop_plan == 0) return lambda_;
    return cm::gems_per_gold(race_left(), cfg::gem_gold_value, cfg::race_rounds_default);
}

void Brain::build_camp_specs()
{
    // The camps this champion's items are valued on: reachable groups on our side of the map,
    // the best by static value density; travel is the walk from the nearest other such group.
    auto& world = cw::World::get();
    if (!world.spawn_field(false)) return;
    struct Pick { int g; float key; };
    constexpr int kCamps = 8;
    std::array<Pick, kCamps> top{};
    int n = 0;
    for (const auto& g : world.groups)
    {
        if (g.spawn_dist < 0) continue;
        if (cw::cheb(g.anchor, world.spawn_center) > cw::cheb(g.anchor, world.enemy_center) + 2) continue;
        const float key = (static_cast<float>(g.gems) + 0.3f * static_cast<float>(g.gold))
                        / (g.total_hp / 60.0f + 10.0f + 0.5f * static_cast<float>(g.spawn_dist));
        if (n < kCamps) top[static_cast<std::size_t>(n++)] = { g.id, key };
        else
        {
            int lo = 0;
            for (int i = 1; i < kCamps; ++i)
                if (top[static_cast<std::size_t>(i)].key < top[static_cast<std::size_t>(lo)].key) lo = i;
            if (key > top[static_cast<std::size_t>(lo)].key) top[static_cast<std::size_t>(lo)] = { g.id, key };
        }
    }
    camp_specs_.clear();
    camp_specs_.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        const auto& g = world.groups[static_cast<std::size_t>(top[static_cast<std::size_t>(i)].g)];
        int nearest = 99;
        for (int j = 0; j < n; ++j)
            if (j != i) nearest = std::min(nearest, cw::cheb(g.anchor, world.groups[static_cast<std::size_t>(top[static_cast<std::size_t>(j)].g)].anchor));
        cm::CampSpec c;
        c.sc = cm::SmithConstants{ g.smith_w, g.total_str, g.total_hp };
        c.gold = static_cast<float>(g.gold);
        c.gems = static_cast<float>(g.gems);
        c.travel = static_cast<float>(std::min(nearest, 30)) * 1.25f;
        c.home = static_cast<float>(g.spawn_dist);
        c.cooldown = static_cast<float>(g.cooldown);
        c.members = static_cast<int>(g.members.size());
        camp_specs_.push_back(c);
    }
    specs_ready_ = !camp_specs_.empty();
}

cm::Profile Brain::profile() const
{
    cm::Profile p;
    p.kit.str = str_;
    p.kit.focus = focus_;
    p.kit.ranged = ranged_;
    p.kit.attack_reset = reset_.has_value();
    if (spell_ && spell_->targets_camps) p.kit.spell_mod = spell_->modifier;
    if (area_ && area_->targets_camps) p.kit.aoe_mod = area_->modifier;
    if (dash_ && dash_->modifier > 0.0f) p.kit.dash_mod = dash_->modifier;
    if (shield_) p.kit.shield_mod = shield_->modifier;
    p.kit.kill_reset = type_ == cambc::champion_type::vendetta;
    p.kit.splash = type_ == cambc::champion_type::varg;
    p.kit.camp_mult = camp_mult_design_;
    p.hp = max_hp_;
    p.taken_camp = taken_camp_;
    p.taken_all = taken_all_;
    p.gold_mult = gold_mult_;
    p.recall = 5.0f;
    p.gem_gold = type_ == cambc::champion_type::marcella ? 2.0f : 0.0f;
    p.growth = growth_;
    p.health_strength = type_ == cambc::champion_type::ryth;
    for (int f = 0; f < 7; ++f)
        for (int s = 0; s < 3; ++s)
            if (items_[static_cast<std::size_t>(cm::emblem_index(static_cast<cm::Family>(f), static_cast<cm::Stat>(s)))] > 0)
                p.passive[static_cast<std::size_t>(f)] = true;
    if (p.passive[static_cast<std::size_t>(cm::Family::monster)]) p.kit.camp_mult *= 1.0f + emblems_.monster;
    if (p.passive[static_cast<std::size_t>(cm::Family::home)]) p.recall = 3.0f;
    return p;
}

cm::Econ Brain::econ() const
{
    cm::Econ e;
    e.gold_w = lambda_;
    e.race_left = static_cast<float>(race_left());
    e.margin = cfg::camp_margin;
    e.capture = static_cast<float>(cambc::constants::capture_time + 1);
    e.siege_trips = cfg::siege_trips;
    e.reinvest = cfg::reinvest_weight;
    e.usable_share = cfg::usable_share;
    e.threat_dps = threat_dps_;
    e.exposure_rounds = cfg::exposure_rounds;
    const float alive_rounds = static_cast<float>(std::max(1, round_));
    e.fights_per_round = std::max(cfg::fight_rate_prior, static_cast<float>(contacts_) / std::max(150.0f, alive_rounds));
    e.risk_slope = cfg::risk_slope;
    e.death_rounds = static_cast<float>(cambc::constants::respawn_time) + 15.0f;
    e.death_gold = static_cast<float>(cambc::constants::kill_gold);
    e.carried = carried_ema_;
    return e;
}

void Brain::update_threat()
{
    // A contact is an enemy within 6. In contact turns the health we lost that the camps in reach
    // cannot explain is champion damage; its upper envelope is the damage a round a contact puts
    // on us. A weak opponent barely scratches us and a strong one hits hard, so the survival value
    // of Health (econ_model.h death_risk) follows what opponents actually do, not their roster.
    // Until enough contact is seen the prior (two strongest enemy kits at base, half weight)
    // dominates.
    bool contact = false;
    for (const auto& e : enemies_)
        if (cw::cheb(e.pos, pos_) <= 6) contact = true;
    if (contact)
    {
        if (round_ - last_contact_round_ > 5) ++contacts_;
        last_contact_round_ = round_;
        const float champ_loss = std::max(0.0f, hp_loss_ - camp_incoming(pos_));
        const float w = champ_loss > observed_threat_ ? 0.3f : 0.03f;
        observed_threat_ = (1.0f - w) * observed_threat_ + w * champ_loss;
        ++contact_turns_;
    }
    const float n = static_cast<float>(contact_turns_);
    threat_dps_ = (n * observed_threat_ + 20.0f * 0.5f * threat_prior_) / (n + 20.0f);
    carried_ema_ = 0.99f * carried_ema_ + 0.01f * static_cast<float>(gems_);
}

void Brain::update_lambda()
{
    if constexpr (cfg::bounded_turns)
        if (cambc::fuel_remaining() < 85000) return; // includes the first build_camp_specs, not just pricing
    if (!specs_ready_) build_camp_specs();
    if (!specs_ready_ || cambc::fuel_remaining() < 75000) return;
    {
        // The team's planned gem pace (foundation E12-E13, E24): the smaller of the map's
        // respawn-limited gem supply (our share of it) and the team's labour (champions times a
        // gems-per-champion-round rate, E11), at least the online loser's pace.
        auto& world = cw::World::get();
        const float supply = world.gem_supply * cfg::supply_share;
        const float labour = static_cast<float>(std::max(1, team_size_)) * cfg::labour_gem_rate;
        plan_gem_rate_ = std::max(cfg::min_plan_rate, std::min(supply, labour));
    }
    farm_rate_cache_ = std::max(0.05f, cm::net_rate(profile(), camp_specs_, econ()).value(lambda_));
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
}

}  // namespace cb
