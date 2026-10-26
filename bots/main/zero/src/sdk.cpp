#include "sdk.h"
#include <cambc.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <map>
#include <memory>
#include <variant>
#include "core/catalog.h"
#include "core/moves.h"
#include "core/utility.h"
#if defined(ZERO_STUDENT_PROBE)
#include "core/ground_scene.h"
#endif

namespace zero::sdk {

namespace {

// Recall bookkeeping: standing = started by end_turn_recall (free to keep or cancel), committed = chosen by the
// step head (runs to the end; the bot then waits).
bool g_standing = false, g_committed = false, g_tail_ran = false;
bool g_explicit_channels = false;
std::uint64_t g_observation_revision = 0;
ObserverFuel g_observer_fuel;
#if defined(ZERO_STUDENT_PROBE)
GroundUtilityScene* g_probe_ground_utility_scene{};
#endif

// Before any action other than keeping the recall: cancel a standing recall (the engine forbids acting while
// channelling).
void release()
{
    if (g_explicit_channels) return;
    if (!g_standing) return;
    g_standing = false;
    try { cambc::self().cancel_recall(); }
    catch (const std::exception&) {}
}

// Measured SDK costs (smoke main/zero-smoke2, round 70): simple queries 50-80 fuel; vision::champions 4.7K;
// champions() 3.7K (cached here once); bitboard OR ~550; positions() ~1.8K; visible_tiles ~540; vision::camps ~1K.
Tile T(cambc::vec2 p) { return { static_cast<std::int16_t>(p.x), static_cast<std::int16_t>(p.y) }; }
cambc::vec2 V(Tile t) { return { t.x, t.y }; }

int absolute_team()
{
    static const int team = static_cast<int>(cambc::detail::absolute_team(cambc::team::ally)) == 0 ? 0 : 1;
    return team;
}

struct Roster
{
    std::vector<cambc::champion> champs;
    std::vector<int> ids;
    std::vector<std::uint8_t> types;
};

const Roster& roster(cambc::team side)
{
    static const auto load = [](cambc::team s) {
        Roster r;
        r.champs = cambc::champions(s);
        for (const auto& c : r.champs)
        {
            r.ids.push_back(static_cast<int>(static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(c))));
            r.types.push_back(static_cast<std::uint8_t>(c.champion_name()));
        }
        return r;
    };
    static const Roster ally = load(cambc::team::ally), enemy = load(cambc::team::enemy);
    return side == cambc::team::ally ? ally : enemy;
}

const std::vector<std::uint32_t>& raw_roster(bool ally)
{
    static const auto load = [](bool side) {
        std::uint32_t count = 0;
        cambc::detail::check(side ? cambc::detail::sdk_num_allies(&count) : cambc::detail::sdk_num_enemies(&count));
        std::vector<std::uint32_t> ids(count);
        cambc::detail::check(side ? cambc::detail::sdk_allies(ids.data(), ids.size(), &count) :
                                   cambc::detail::sdk_enemies(ids.data(), ids.size(), &count));
        if (count != ids.size()) throw std::runtime_error("invalid engine roster count");
        return ids;
    };
    static const auto allies = load(true), enemies = load(false);
    return ally ? allies : enemies;
}

int my_id()
{
    static const int id = static_cast<int>(static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(cambc::self())));
    return id;
}

int dash_range(const Obs& o)
{
    int dash = 0;
    const auto& d = catalog().champion(o.self.type);
    for (int k = 0; k < 2; ++k)
        if (d.ability[k].kind == ab_dash && o.self.cooldown[k] == 0) dash = std::max(dash, static_cast<int>(d.ability[k].range));
    return dash;
}

void read_items(Obs& o, bool force)
{
    // Items change only by buying (on spawn or while dead; act_buy refreshes): read them only then.
    static std::array<std::uint8_t, kMaxItems> items{};
    static bool read = false;
    if (g_explicit_channels)
    {
        // One exact ABI inventory snapshot, avoiding per-entry checked enum-map access. The wire-sized
        // legacy feature view saturates at 255, while item_counts keeps every current uint32 value.
        std::array<std::uint32_t, static_cast<std::size_t>(cambc::item_type::count)> counts{};
        cambc::detail::check(cambc::detail::sdk_champion_items(static_cast<std::uint32_t>(my_id()), counts.data()));
        items.fill(0);
        o.item_counts.fill(0);
        for (std::size_t i = 0; i < counts.size() && i < kMaxItems && i < static_cast<std::size_t>(catalog().n_items); ++i)
        {
            o.item_counts[i] = counts[i];
            items[i] = static_cast<std::uint8_t>(std::min(255u, counts[i]));
        }
        read = true;
        o.items = items;
        return;
    }
    if (force || !read || o.on_spawn || !o.alive)
    {
        const auto counts = cambc::self().item_count();
        for (int i = 0; i < kMaxItems && i < catalog().n_items; ++i)
        {
            const auto count = counts[static_cast<cambc::item_type>(i)];
            items[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(count);
        }
        read = true;
    }
    o.items = items;
}

void read_buffs(std::uint32_t id, UnitView& v)
{
    std::array<cambc::detail::buff_data, 3> buffs{};
    std::uint32_t count = 0;
    cambc::detail::check(cambc::detail::sdk_champion_buff_data(id, buffs.data(), buffs.size(), &count));
    if (count > buffs.size()) throw std::runtime_error("invalid engine buff count");
    v.stunned = false;
    v.shield = 0.0f;
    for (std::uint32_t i = 0; i < count; ++i)
    {
        if (buffs[i].type == static_cast<std::uint32_t>(cambc::buff_types::stun)) v.stunned = true;
        if (buffs[i].type == static_cast<std::uint32_t>(cambc::buff_types::shield)) v.shield = buffs[i].data.shield.value;
    }
}

void unpack_champion(const cambc::detail::champion_info& info, UnitView& v)
{
    v.type = static_cast<std::uint8_t>(info.type);
    v.at = { static_cast<std::int16_t>(info.x), static_cast<std::int16_t>(info.y) };
    v.hp = info.health;
    v.max_hp = info.max_health;
    v.strength = info.strength;
    v.focus = info.focus;
    const auto state = cambc::champion_state::from_bits_retain(static_cast<std::uint32_t>(info.state));
    v.recalling = state.contains(cambc::champion_state::recalling);
    v.tributing = state.contains(cambc::champion_state::submitting_gems);
}

void read_self_exact(Obs& o)
{
    const auto id = static_cast<std::uint32_t>(my_id());
    o.command_facts = true;
    o.state_bits = 0;
    o.restriction_bits = 0;
    o.teleporting = o.can_recall = o.can_tribute = o.can_cancel = o.can_buy = false;
    o.can_cast_slot[0] = o.can_cast_slot[1] = false;
    o.channel_remaining = 0;
    o.self.id = static_cast<int>(id);
    static const auto type = static_cast<std::uint8_t>(cambc::self().champion_name());
    o.self.type = type;
    std::int32_t alive = 0;
    cambc::detail::check(cambc::detail::sdk_champion_is_alive(id, &alive));
    o.alive = alive != 0;
    cambc::detail::check(cambc::detail::sdk_champion_gold(id, &o.self.gold));
    // The native self ABI exposes current stats/cooldowns while dead, including purchases made during
    // shopping turns. Keep that exact record; the legacy adapter below retains its old dead path.
    cambc::detail::champion_info info{};
    cambc::detail::check(cambc::detail::sdk_champion(id, &info));
    unpack_champion(info, o.self);
    o.state_bits = static_cast<std::uint32_t>(info.state);
    const auto state = cambc::champion_state::from_bits_retain(o.state_bits);
    o.can_move = state.contains(cambc::champion_state::can_move);
    o.can_attack = state.contains(cambc::champion_state::can_attack);
    o.can_cast = state.contains(cambc::champion_state::can_cast);
    o.recall_started = state.contains(cambc::champion_state::recall_started);
    cambc::detail::check(cambc::detail::sdk_champion_gems(id, &o.self.gems));
    cambc::detail::check(cambc::detail::sdk_champion_restrictions(id, &o.restriction_bits));
    read_buffs(id, o.self);
    derive_command_state(o);
    if (o.self.recalling || o.self.tributing || o.teleporting)
        cambc::detail::check(cambc::detail::sdk_champion_channel_time(id, &o.channel_remaining));
    for (int slot = 0; slot < 2; ++slot)
    {
        std::int32_t cooldown = 0;
        cambc::detail::check(cambc::detail::sdk_champion_cooldown(id, static_cast<std::uint32_t>(slot), &cooldown));
        o.self.cooldown[slot] = static_cast<std::int8_t>(std::max(0, std::min(100, cooldown)));
        o.can_cast_slot[slot] = o.can_cast && cooldown == 0 && catalog().champion(o.self.type).ability[slot].kind != ab_none;
    }
    static const auto& spawn = cambc::map::spawn_region(cambc::team::ally);
    o.on_spawn = o.alive && spawn.is_set(V(o.self.at));
}

void read_self(Obs& o)
{
    if (g_explicit_channels) { read_self_exact(o); return; }
    auto& me = cambc::self();
    o.alive = me.is_alive();
    o.self.id = my_id();
    static const auto type = static_cast<std::uint8_t>(me.champion_name());  // immutable design
    o.self.type = type;
    if (!o.alive)
    {
        // Dead: only shopping is possible; position, abilities and state queries are invalid.
        o.self.gold = me.gold_count();
        o.self.gems = 0;
        o.can_move = o.can_attack = o.can_cast = false;
        o.self.recalling = o.self.tributing = false;
        o.recall_started = g_standing = g_committed = false;
        return;
    }
    // One live record (the getters each re-read it): position, health, stats and state of this instant.
    cambc::detail::champion_info info{};
    cambc::detail::check(cambc::detail::sdk_champion(static_cast<std::uint32_t>(o.self.id), &info));
    o.self.at = { static_cast<std::int16_t>(info.x), static_cast<std::int16_t>(info.y) };
    o.self.hp = info.health;
    o.self.max_hp = info.max_health;
    o.self.strength = info.strength;
    o.self.focus = info.focus;
    o.self.gems = me.gem_count();
    o.self.gold = me.gold_count();
    try
    {
        for (int k = 0; k < 2; ++k)
        {
            std::int32_t cooldown = 0;
            cambc::detail::check(cambc::detail::sdk_champion_cooldown(static_cast<std::uint32_t>(o.self.id), static_cast<std::uint32_t>(k), &cooldown));
            o.self.cooldown[k] = static_cast<std::int8_t>(std::max(0, std::min(100, static_cast<int>(cooldown))));
        }
    }
    catch (const std::exception&)
    {
        o.self.cooldown[0] = o.self.cooldown[1] = -1;
    }
    const auto st = cambc::champion_state::from_bits_retain(static_cast<std::uint32_t>(info.state));
    o.can_move = st.contains(cambc::champion_state::can_move);
    o.can_attack = st.contains(cambc::champion_state::can_attack);
    o.can_cast = st.contains(cambc::champion_state::can_cast);
    o.self.recalling = st.contains(cambc::champion_state::recalling);
    if (!o.self.recalling) g_standing = g_committed = false;  // arrived, interrupted or cancelled
    o.recall_started = o.self.recalling && g_standing;
    if (o.recall_started)
    {
        // The channel blocks actions until cancelled; decisions see the turn as it is after a cancel (a recall
        // that was not interrupted means no stun: the turn's allowances are fresh, casts by cooldown).
        o.self.recalling = false;
        o.can_move = o.can_attack = true;
        o.can_cast = o.self.cooldown[0] == 0 || o.self.cooldown[1] == 0;
    }
    o.self.tributing = st.contains(cambc::champion_state::submitting_gems);
    static const auto& spawn = cambc::map::spawn_region(cambc::team::ally);
    o.on_spawn = spawn.is_set(V(o.self.at));
}

// Utility availability uses current host predicates, rather than spawn/gold heuristics. Families without
// a complete legality contract are deliberately absent from utility_known_mask until that contract is supplied.
void read_commands(Obs& o)
{
    if (!g_explicit_channels) return;
    o.utility_actions.clear();
    o.buyable_items.fill(false);
    o.can_buy = false;
    o.utility_known_mask = (1u << a_buy_consumable) | (1u << a_sell) | (1u << a_priority);
    o.consumables.fill(0);
    o.ward_ready = false;
    o.ward_charges = o.ward_cooldown = 0;
    o.wards.clear();
    const auto id = static_cast<std::uint32_t>(my_id());
    // Mirror the public SDK can_buy implementation, amortizing its immutable item metadata and three
    // permission queries instead of importing full item metadata and self gold once per item.
    static const auto item_info = [] {
        std::array<cambc::detail::item_info, kMaxItems> result{};
        for (int i = 0; i < kMaxItems && i < static_cast<int>(cambc::item_type::count); ++i)
            cambc::detail::check(cambc::detail::sdk_item_info(static_cast<std::uint32_t>(i), &result[static_cast<std::size_t>(i)]));
        return result;
    }();
    std::int32_t can_shop = 0, can_shop_consumables = 0, can_sell = 0;
    cambc::detail::check(cambc::detail::sdk_champion_can_shop(id, false, &can_shop));
    cambc::detail::check(cambc::detail::sdk_champion_can_shop(id, true, &can_shop_consumables));
    cambc::detail::check(cambc::detail::sdk_champion_can_sell(id, &can_sell));
    for (int i = 0; i < kMaxItems && i < catalog().n_items; ++i)
    {
        const auto& info = item_info[static_cast<std::size_t>(i)];
        o.buyable_items[static_cast<std::size_t>(i)] = can_shop != 0 && info.purchasable != 0 && info.cost <= o.self.gold;
        o.can_buy |= o.buyable_items[static_cast<std::size_t>(i)];
    }
    std::array<std::uint32_t, 4> consumables{};
    cambc::detail::check(cambc::detail::sdk_champion_consumables(id, consumables.data()));
    for (int i = 0; i < 2; ++i)
    {
        const auto type = static_cast<cambc::consumable_type>(i);
        o.consumables[static_cast<std::size_t>(i)] = consumables[cambc::detail::raw_consumable_ids[static_cast<std::size_t>(i)]];
        if (can_shop_consumables && type.cost() <= o.self.gold && o.consumables[static_cast<std::size_t>(i)] < type.carry_limit())
            append_buy_consumable_utility(o, a_buy_consumable, i, type.cost());
    }
    if (can_sell)
        for (int i = 0; i < kMaxItems && i < catalog().n_items; ++i)
        {
            const auto count = o.item_counts[static_cast<std::size_t>(i)];
            const auto price = item_info[static_cast<std::size_t>(i)].cost;
            if (!count || static_cast<std::uint64_t>(price) + o.self.gold > std::numeric_limits<std::uint32_t>::max()) continue;
            append_sell_utility(o, a_sell, i, price);
        }
    cambc::detail::check(cambc::detail::sdk_bc_champion_priority(id, true, &o.priority));
    o.can_priority = (o.state_bits & (1u << 5)) == 0;
    {
        cambc::detail::ward_resources resources{};
        cambc::detail::check(cambc::detail::sdk_champion_wards(id, &resources));
        o.ward_charges = static_cast<int>(resources.charges);
        o.ward_cooldown = resources.cooldown;
        o.ward_ready = resources.charges > 0;
        std::vector<cambc::detail::position> positions(cambc::detail::cached_game_settings.active_wards);
        std::uint32_t count = 0;
        cambc::detail::check(cambc::detail::sdk_champion_ward_positions(id, positions.data(), positions.size(), &count));
        if (count > positions.size()) throw std::runtime_error("invalid engine ward count");
        for (std::uint32_t i = 0; i < count; ++i) o.wards.push_back({ static_cast<std::int16_t>(positions[i].x), static_cast<std::int16_t>(positions[i].y) });
    }
}

void read_visible_champions(Obs& o)
{
    o.allies.clear();
    o.enemies.clear();
    o.allies_alive = o.enemies_alive = 0;
    for (const bool ally : { true, false })
    {
        const auto& ids = raw_roster(ally);
        for (std::size_t i = 0; i < ids.size(); ++i)
        {
            const auto id = ids[i];
            if (id == static_cast<std::uint32_t>(o.self.id)) o.self.roster = static_cast<int>(i);
            std::int32_t alive = 0;
            cambc::detail::check(cambc::detail::sdk_champion_is_alive(id, &alive));
            if (ally) o.allies_alive += alive != 0 ? 1 : 0;
            else o.enemies_alive += alive != 0 ? 1 : 0;
            if (!o.alive || !alive || id == static_cast<std::uint32_t>(o.self.id)) continue;
            cambc::detail::champion_info info{};
            const auto code = cambc::detail::sdk_champion(id, &info);
            if (code == static_cast<std::int32_t>(cambc::error_code::api_not_in_vision)) continue;
            cambc::detail::check(code);
            if (info.health <= 0.0f || info.x < 0 || info.y < 0) continue;
            UnitView v;
            v.id = static_cast<int>(id);
            v.roster = static_cast<int>(i);
            unpack_champion(info, v);
            cambc::detail::check(cambc::detail::sdk_champion_gems(id, &v.gems));
            read_buffs(id, v);
            for (int slot = 0; slot < 2; ++slot)
            {
                std::int32_t cooldown = 0;
                cambc::detail::check(cambc::detail::sdk_champion_cooldown(id, static_cast<std::uint32_t>(slot), &cooldown));
                v.cooldown[slot] = static_cast<std::int8_t>(std::max(0, std::min(100, cooldown)));
            }
            (ally ? o.allies : o.enemies).push_back(v);
        }
    }
}

void read_ground_commands(const Map& map, Obs& o)
{
    if (!g_explicit_channels) return;
    // Ward radius and lock price come from live SDK metadata. The pinned engine exposes no lock-radius
    // setting; 2 is the documented/canaried current-bundle rule. TP use remains explicitly unsupported.
    const UtilityRules rules{ cambc::detail::cached_game_settings.ward_placement_radius, 2.0f,
                              cambc::consumable_type::altar_lock.cost(), kConsumeLock };
#if defined(ZERO_STUDENT_PROBE)
    if (g_probe_ground_utility_scene)
    {
        build_ground_utility_scene(map, o, rules, *g_probe_ground_utility_scene);
        if (!apply_ground_utility_contract(o, *g_probe_ground_utility_scene))
            throw std::logic_error("stale ground utility scene during observation");
        return;
    }
#endif
    const auto contract = append_ground_utilities(map, o, rules);
    o.utility_known_consumables = contract.known_consume_mask;
    o.utility_capability_limits = contract.limitations;
    o.utility_affected_limits = contract.affected_limits;
    o.ward_arguments_complete = contract.ward_complete;
    o.consumable_arguments_complete = contract.consume_complete;
}

void read_altar_locks_exact(const Map& map, Obs& o)
{
    o.altar_lock.assign(map.altars.size(), lk_none);
    o.altar_lock_hits.assign(map.altars.size(), -1);
    const auto ours = cambc::vision::altar_lock_tiles(cambc::team::ally);
    const auto theirs = cambc::vision::altar_lock_tiles(cambc::team::enemy);
    for (std::size_t a = 0; a < map.altars.size(); ++a)
    {
        const auto at = map.altars[a];
        const bool enemy = theirs.is_set(V(at));
        const bool ally = !enemy && ours.is_set(V(at));
        if (!enemy && !ally) continue;
        cambc::detail::altar_lock_info info{};
        const auto team = static_cast<std::int32_t>(cambc::detail::absolute_team(enemy ? cambc::team::enemy : cambc::team::ally));
        cambc::detail::check(cambc::detail::sdk_altar_lock(at.x, at.y, team, &info));
        if (info.hits_remaining > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
            throw std::runtime_error("altar lock hit count exceeds observation representation");
        o.altar_lock[a] = enemy ? lk_theirs : lk_ours;
        o.altar_lock_hits[a] = static_cast<int>(info.hits_remaining);
    }
}

}  // namespace

// The engine's camp-group list (~17K fuel to load): loaded once with the map, reused for group states.
std::vector<cambc::map::camp_group>& engine_groups()
{
    static std::vector<cambc::map::camp_group> groups;
    return groups;
}

std::uint64_t fuel() { return cambc::fuel_remaining(); }

const ObserverFuel& last_observer_fuel() { return g_observer_fuel; }

void set_explicit_channel_mode(bool enabled)
{
    g_explicit_channels = enabled;
    if (enabled) g_standing = g_committed = g_tail_ran = false;
}

#if defined(ZERO_STUDENT_PROBE)
void set_probe_ground_utility_scene(GroundUtilityScene* scene)
{
    g_probe_ground_utility_scene = scene;
}
#endif

void load_terrain(Map& map)
{
    map.w = static_cast<int>(cambc::map::width());
    map.h = static_cast<int>(cambc::map::height());
    map.flags.assign(static_cast<std::size_t>(map.w * map.h), 0);
    auto mark = [&](const cambc::bitboard& b, std::uint8_t f) {
        for (auto p : b.positions()) map.flags[static_cast<std::size_t>(p.y * map.w + p.x)] |= f;
    };
    mark(cambc::map::walls(), tf_wall);
    mark(cambc::map::holes(), tf_hole);
    mark(cambc::map::bushes(), tf_bush);
    mark(cambc::map::altars(), tf_altar);
    const int team = absolute_team();
    const auto& mine = cambc::map::spawn_region(cambc::team::ally);
    const auto& theirs = cambc::map::spawn_region(cambc::team::enemy);
    mark(mine, team == 0 ? tf_spawn_a : tf_spawn_b);
    mark(theirs, team == 0 ? tf_spawn_b : tf_spawn_a);
    for (auto p : mine.positions()) map.spawn[team].push_back(T(p));
    for (auto p : theirs.positions()) map.spawn[1 - team].push_back(T(p));
    for (auto p : cambc::map::altars().positions()) map.altars.push_back(T(p));
}

void load_groups(Map& map)
{
    engine_groups() = cambc::map::camp_groups();
    for (const auto& g : engine_groups())
    {
        Group gr;
        gr.gold = g.gold_value();
        gr.gems = g.gem_value();
        gr.respawn = g.max_respawn_timer();
        for (const auto& c : g.camps()) gr.members.push_back({ T(c.spawn_position()), static_cast<std::uint8_t>(c.kind()) });
        map.groups.push_back(std::move(gr));
    }
}

namespace {

// One ABI snapshot per visible entity. Public scalar getters each re-import the complete record; this
// path preserves exact observations while amortizing those copies and the immutable roster/metadata.
void observe_exact(const Map& map, Obs& o)
{
    g_observer_fuel = ObserverFuel{};
    const auto started = cambc::fuel_remaining();
    auto previous = started;
    const auto mark = [&](std::uint64_t& part) {
        const auto now = cambc::fuel_remaining();
        if (now <= previous) part = previous - now;
        else g_observer_fuel.fuel_reset = true;
        previous = now;
    };
#if defined(ZERO_STUDENT_PROBE)
    std::uint64_t probe_previous{};
    const auto probe_mark = [&](std::uint64_t& part) {
        const auto now = cambc::fuel_remaining();
        if (now <= probe_previous) part = probe_previous - now;
        else g_observer_fuel.fuel_reset = true;
        probe_previous = now;
    };
#endif
    const auto finish = [&] {
        mark(g_observer_fuel.locks_moves);
        if (!g_observer_fuel.fuel_reset) g_observer_fuel.total = started - previous;
        g_observer_fuel.complete = true;
    };
    o.revision = ++g_observation_revision;
    o.round = static_cast<int>(cambc::round_number());
    o.team = absolute_team();
    g_observer_fuel.revision = o.revision;
    g_observer_fuel.round = o.round;
    read_self_exact(o);
    mark(g_observer_fuel.self);
    read_items(o, true);
    mark(g_observer_fuel.items);
    read_commands(o);
    o.banked_us = cambc::global_submitted_gems(cambc::team::ally);
    o.banked_them = cambc::global_submitted_gems(cambc::team::enemy);
    o.camps.clear();
    o.reservation_facts = false;
    o.reserved_tiles.clear();
    o.ally_ward_facts = true;
    o.ally_ward_tiles.clear();
    o.group_status.assign(map.groups.size(), gs_unknown);
    o.altar_lock.assign(map.altars.size(), lk_none);
    o.altar_lock_hits.assign(map.altars.size(), -1);
    mark(g_observer_fuel.commands);
    read_visible_champions(o);
    for (const auto position : cambc::vision::ward_tiles(cambc::team::ally).positions()) o.ally_ward_tiles.push_back(T(position));
    for (const auto position : cambc::vision::teleport_tiles().positions()) o.reserved_tiles.push_back(T(position));
    o.reservation_facts = true;
    mark(g_observer_fuel.champions);
    if (!o.alive)
    {
#if defined(ZERO_STUDENT_PROBE)
        probe_previous = cambc::fuel_remaining();
#endif
        legal_moves(map, o, dash_range(o), false);
#if defined(ZERO_STUDENT_PROBE)
        probe_mark(g_observer_fuel.move_arguments);
#endif
        read_ground_commands(map, o);
#if defined(ZERO_STUDENT_PROBE)
        probe_mark(g_observer_fuel.ground_utilities);
#endif
        finish();
        return;
    }
    const auto occupied = cambc::vision::camp_tiles();
    const auto awake = cambc::vision::awake_camp_tiles();
    for (const auto position : occupied.positions())
    {
        std::uint32_t handle = 0;
        cambc::detail::check(cambc::detail::sdk_camp_at(position.x, position.y, &handle));
        cambc::detail::camp_state info{};
        cambc::detail::check(cambc::detail::sdk_camp(handle, &info));
        if (info.health <= 0.0f) continue;
        CampView v;
        v.id = static_cast<int>(handle);
        v.at = { static_cast<std::int16_t>(info.x), static_cast<std::int16_t>(info.y) };
        v.hp = info.health;
        v.awake = awake.is_set(position);
        v.group = map.group_of[static_cast<std::size_t>(map.idx(v.at))];
        bool typed = false;
        if (v.group >= 0)
            for (const auto& member : map.groups[static_cast<std::size_t>(v.group)].members)
                if (member.at == v.at) { v.type = member.camp_type; typed = true; break; }
        if (!typed)
        {
            cambc::detail::camp_info design{};
            cambc::detail::check(cambc::detail::sdk_map_camp_info(handle, &design));
            v.type = static_cast<std::uint8_t>(design.type);
        }
        if (v.group >= 0) o.group_status[static_cast<std::size_t>(v.group)] = gs_alive;
        o.camps.push_back(v);
    }
    mark(g_observer_fuel.camps);
    // A single documented row-major packed export replaces a host Bitboard_Test import for every camp
    // spawn on the map. Group state is queried only when a member spawn is actually visible.
    const auto visible = cambc::vision::visible_tiles().export_linear_buffer();
    const auto is_visible = [&](Tile at) {
        const auto index = static_cast<std::size_t>(map.idx(at));
        return (visible[index >> 3u] & (1u << (index & 7u))) != 0;
    };
    const auto& groups = engine_groups();
    for (std::size_t g = 0; g < map.groups.size() && g < groups.size(); ++g)
    {
        if (o.group_status[g] == gs_alive) continue;
        bool seen = false;
        for (const auto& member : map.groups[g].members)
            if (is_visible(member.at)) { seen = true; break; }
        if (!seen) continue;
        std::uint32_t state = 0;
        const auto handle = static_cast<std::uint32_t>(static_cast<cambc::handle_camp_group_t>(groups[g]));
        cambc::detail::check(cambc::detail::sdk_camp_group_state(handle, &state));
        if (state == 1) o.group_status[g] = gs_alive;
        else if (state == 2) o.group_status[g] = gs_reward;
        else if (state == 3) o.group_status[g] = gs_respawning;
        else if (state != 0) throw std::runtime_error("unknown engine camp group state");
    }
    mark(g_observer_fuel.groups);
#if defined(ZERO_STUDENT_PROBE)
    probe_previous = cambc::fuel_remaining();
#endif
    read_altar_locks_exact(map, o);
#if defined(ZERO_STUDENT_PROBE)
    probe_mark(g_observer_fuel.locks_read);
#endif
    // Reservations are visible physical occupants even before their champion arrives.
    canonical_observation_order(o);
#if defined(ZERO_STUDENT_PROBE)
    probe_mark(g_observer_fuel.canonical_order);
#endif
    legal_moves(map, o, dash_range(o), false);
#if defined(ZERO_STUDENT_PROBE)
    probe_mark(g_observer_fuel.move_arguments);
#endif
    read_ground_commands(map, o);
#if defined(ZERO_STUDENT_PROBE)
    probe_mark(g_observer_fuel.ground_utilities);
#endif
    finish();
}

}  // namespace

void observe(const Map& map, Obs& o)
{
#if defined(ZERO_STUDENT_PROBE)
    if (g_probe_ground_utility_scene) *g_probe_ground_utility_scene = GroundUtilityScene{};
#endif
    if (g_explicit_channels) { observe_exact(map, o); return; }
    o.revision = ++g_observation_revision;
    std::uint64_t op_start = cambc::fuel_remaining(), op_last = op_start;
    std::uint64_t op_parts[8]{};
    int op_i = 0;
    auto op = [&] { prof::mark(30 + op_i); const auto now = cambc::fuel_remaining(); if (op_i < 8) op_parts[op_i++] = op_last - now; op_last = now; };
    struct OpEnd
    {
        std::uint64_t& start; std::uint64_t* parts; int round;
        ~OpEnd()
        {
            const auto used = start - cambc::fuel_remaining();
            if ((used > 60000 && used < 1000000) || static_cast<int>(cambc::round_number()) != round)  // heavy or suspended
                std::printf("obsheavy r%d %llu self %llu champs %llu camps %llu groups %llu locks %llu moves %llu\n", round,
                            static_cast<unsigned long long>(used), static_cast<unsigned long long>(parts[0]), static_cast<unsigned long long>(parts[1]),
                            static_cast<unsigned long long>(parts[2]), static_cast<unsigned long long>(parts[3]), static_cast<unsigned long long>(parts[4]),
                            static_cast<unsigned long long>(parts[5]));
        }
    } op_end{ op_start, op_parts, static_cast<int>(cambc::round_number()) };
    o.round = static_cast<int>(cambc::round_number());
    o.team = absolute_team();
    read_self(o);
    read_items(o, false);
    o.banked_us = cambc::global_submitted_gems(cambc::team::ally);
    o.banked_them = cambc::global_submitted_gems(cambc::team::enemy);
    if (!o.alive)
    {
        // Dead: nothing is in our vision; keep the last view's lists empty (shopping needs only gold and items).
        o.allies.clear();
        o.enemies.clear();
        o.camps.clear();
        o.step_ok.clear();
        o.dash_ok.clear();
        o.group_status.assign(map.groups.size(), gs_unknown);
        o.altar_lock.assign(map.altars.size(), lk_none);
        return;
    }
    op();
    // Champions: the cached roster with per-champion visibility (cheaper than vision::champions).
    o.allies.clear();
    o.enemies.clear();
    static int allies_alive = 0, enemies_alive = 0, counted_round = -100;
    const bool count = o.round - counted_round >= 5;
    if (count) { allies_alive = enemies_alive = 0; counted_round = o.round; }
    // Visible champions from the occupancy bitboards (one query per team; the roster gives their slots), alive counts
    // every fifth round (one query per champion).
    const auto& ar = roster(cambc::team::ally);
    const auto& er = roster(cambc::team::enemy);
    for (std::size_t i = 0; i < ar.ids.size(); ++i)
        if (ar.ids[i] == o.self.id) o.self.roster = static_cast<int>(i);
    if (count)
    {
        for (std::size_t i = 0; i < ar.champs.size(); ++i)
        {
            if (ar.ids[i] == o.self.id) { allies_alive += o.alive ? 1 : 0; continue; }
            try { allies_alive += ar.champs[i].is_alive() ? 1 : 0; } catch (const std::exception&) {}
        }
        for (std::size_t i = 0; i < er.champs.size(); ++i)
            try { enemies_alive += er.champs[i].is_alive() ? 1 : 0; } catch (const std::exception&) {}
    }
    auto slot_of = [](const Roster& r, int id) {
        for (std::size_t i = 0; i < r.ids.size(); ++i) if (r.ids[i] == id) return static_cast<int>(i);
        return -1;
    };
    for (const auto pos : cambc::vision::champion_tiles(cambc::team::ally).positions())
    {
        if (T(pos) == o.self.at) continue;
        try
        {
            std::uint32_t handle = 0;
            cambc::detail::check(cambc::detail::sdk_champion_at(pos.x, pos.y, &handle));
            const int id = static_cast<int>(handle);
            const int i = slot_of(ar, id);
            if (i < 0 || id == o.self.id) continue;
            UnitView v;
            v.id = id;
            v.roster = i;
            v.type = ar.types[static_cast<std::size_t>(i)];
            v.at = T(pos);
            cambc::detail::champion_info info{};
            cambc::detail::check(cambc::detail::sdk_champion(static_cast<std::uint32_t>(id), &info));
            v.hp = info.health;
            v.max_hp = info.max_health;
            if (v.hp > 0.0f) o.allies.push_back(v);
        }
        catch (const std::exception&)
        {
        }
    }
    for (const auto pos : cambc::vision::champion_tiles(cambc::team::enemy).positions())
    {
        try
        {
            std::uint32_t handle = 0;
            cambc::detail::check(cambc::detail::sdk_champion_at(pos.x, pos.y, &handle));
            const int id = static_cast<int>(handle);
            const int i = slot_of(er, id);
            if (i < 0) continue;
            UnitView v;
            v.id = id;
            v.roster = i;
            v.type = er.types[static_cast<std::size_t>(i)];
            v.at = T(pos);
            cambc::detail::champion_info info{};
            cambc::detail::check(cambc::detail::sdk_champion(static_cast<std::uint32_t>(id), &info));
            v.hp = info.health;
            if (v.hp <= 0.0f) continue;
            v.max_hp = info.max_health;
            v.strength = info.strength;
            v.focus = info.focus;
            cambc::detail::check(cambc::detail::sdk_champion_gems(handle, &v.gems));
            try
            {
                for (int k = 0; k < 2; ++k)
                {
                    std::int32_t cooldown = 0;
                    cambc::detail::check(cambc::detail::sdk_champion_cooldown(static_cast<std::uint32_t>(id), static_cast<std::uint32_t>(k), &cooldown));
                    v.cooldown[k] = static_cast<std::int8_t>(std::max(0, std::min(100, static_cast<int>(cooldown))));
                }
            }
            catch (const std::exception&)
            {
                v.cooldown[0] = v.cooldown[1] = -1;
            }
            const auto st = cambc::champion_state::from_bits_retain(static_cast<std::uint32_t>(info.state));
            v.recalling = st.contains(cambc::champion_state::recalling);
            v.tributing = st.contains(cambc::champion_state::submitting_gems);
            o.enemies.push_back(v);
        }
        catch (const std::exception&)
        {
        }
    }
    o.allies_alive = allies_alive;
    o.enemies_alive = enemies_alive;
    op();
    // Camps in view.
    // Health is read for near camps (and camps never read); farther camps keep their last reading
    // (perception fidelity: on dense maps reading every visible camp costs 15K+ a turn).
    // Readings are kept per camp-group member (a per-tile table costs ~100K to fill on large maps).
    struct Seen { float hp; bool awake; int round; };
    static std::vector<std::vector<Seen>> seen;
    if (seen.size() != map.groups.size())
    {
        seen.resize(map.groups.size());
        for (std::size_t g = 0; g < map.groups.size(); ++g) seen[g].assign(map.groups[g].members.size(), Seen{ 0.0f, false, -1 });
    }
    o.camps.clear();
    int reads = 0;
    const auto awake = cambc::vision::awake_camp_tiles();  // wakefulness for every camp in one query
    for (const auto pos : cambc::vision::camp_tiles().positions())  // positions from the bitboard; handles only to read
    {
        CampView v;
        v.at = T(pos);
        v.group = map.group_of[static_cast<std::size_t>(map.idx(v.at))];
        v.type = 0;
        Seen scratch{ 0.0f, false, -1 };
        Seen* slot = &scratch;  // a camp off its group's member tiles is read every time (within the cap)
        if (v.group >= 0)  // the camp type is the map's (one query fewer per camp)
        {
            const auto& members = map.groups[static_cast<std::size_t>(v.group)].members;
            for (std::size_t k = 0; k < members.size(); ++k)
                if (members[k].at == v.at)
                {
                    v.type = members[k].camp_type;
                    slot = &seen[static_cast<std::size_t>(v.group)][k];
                }
        }
        auto& sn = *slot;
        // At most 12 readings a turn (near, never read, or stale); a camp never read counts as its design's health.
        if (reads < 12 && (sn.round < 0 || cheb(v.at, o.self.at) <= 6 || o.round - sn.round > 20))
        {
            try
            {
                const auto c = cambc::vision::camp_at(pos);
                sn = Seen{ c.health(), false, o.round };
            }
            catch (const std::exception&)
            {
            }
            ++reads;
        }
        v.hp = sn.round >= 0 ? sn.hp : catalog().camp(v.type).health;
        v.awake = awake.is_set(pos);
        o.camps.push_back(v);
    }
    op();
    // Group states: a group within vision range showing no living member is reward or respawning (engine query).
    o.group_status.assign(map.groups.size(), gs_unknown);
    const auto& groups = engine_groups();
    bool fetched = false;
    cambc::bitboard visible;
    for (std::size_t g = 0; g < groups.size() && g < map.groups.size(); ++g)
    {
        if (cheb(map.groups[g].center, o.self.at) > 9) continue;
        bool living = false;
        for (const auto& c : o.camps) living |= c.group == static_cast<int>(g);
        if (living) { o.group_status[g] = gs_alive; continue; }
        if (!fetched) { visible = cambc::vision::visible_tiles(); fetched = true; }
        bool seen = false;
        for (const auto& m : map.groups[g].members) seen |= visible.is_set(V(m.at));
        if (!seen) continue;
        // The raw state code (1 alive, 2 reward available, 3 respawning) the SDK's variant is built from.
        std::uint32_t state = 0;
        const auto handle = static_cast<std::uint32_t>(static_cast<cambc::handle_camp_group_t>(groups[g]));
        if (cambc::detail::sdk_camp_group_state(handle, &state) == 0 && state >= 1 && state <= 3)
            o.group_status[g] = state == 1 ? gs_alive : (state == 2 ? gs_reward : gs_respawning);
    }
    op();
    // Altar locks within sight.
    o.altar_lock.assign(map.altars.size(), lk_none);
    for (std::size_t a = 0; a < map.altars.size(); ++a)
        if (cheb(map.altars[a], o.self.at) <= 8)
        {
            if (!fetched) { visible = cambc::vision::visible_tiles(); fetched = true; }
            if (visible.is_set(V(map.altars[a])) && cambc::vision::has_altar_lock(V(map.altars[a]))) o.altar_lock[a] = lk_theirs;
        }
    op();
    legal_moves(map, o, dash_range(o));
    op();
}

void refresh_self(const Map& map, Obs& o)
{
    if (g_explicit_channels) { observe(map, o); return; }
    o.revision = ++g_observation_revision;
    read_self(o);
    read_items(o, o.on_spawn || !o.alive);
    legal_moves(map, o, dash_range(o));
}

void refresh_after_primitive(const Map& map, Obs& o)
{
    // Reference path: every effect is queried from the engine, including kills and changed vision.
    observe(map, o);
}

void refresh_after_primitive(const Map& map, Obs& o, ActionType type, const Cand& argument)
{
    if (g_explicit_channels && type == a_priority)
    {
        // SetPriority changes only the requested next-turn priority; no turn or world time advances.
        // Retain the same boundary's other facts and read back any engine normalization of the value.
        o.revision = ++g_observation_revision;
        const auto id = static_cast<std::uint32_t>(my_id());
        cambc::detail::check(cambc::detail::sdk_bc_champion_priority(id, true, &o.priority));
        return;
    }
    if (type == a_buy || type == a_buy_consumable || type == a_sell || type == a_priority)
    {
        // These commands cannot change terrain, positions, channels or another unit's health/vision.
        o.revision = ++g_observation_revision;
        read_self(o);
        read_items(o, true);
        read_commands(o);
        legal_moves(map, o, dash_range(o), false);
        read_ground_commands(map, o);
        return;
    }
    if (g_explicit_channels && (type == a_recall || type == a_tribute || type == a_cancel))
    {
        o.revision = ++g_observation_revision;
        read_self_exact(o);
        read_items(o, true);
        read_commands(o);
        legal_moves(map, o, dash_range(o), false);
        read_ground_commands(map, o);
        return;
    }
    const bool nonmoving_cast = (type == a_cast0 || type == a_cast1) &&
        catalog().champion(o.self.type).ability[type == a_cast0 ? 0 : 1].kind != ab_dash;
    if (g_explicit_channels && ((type == a_attack && argument.kind != 3) || nonmoving_cast))
    {
        for (const auto& camp : o.camps)
            if (camp.id < 0) { refresh_after_primitive(map, o); return; }
        const auto before = o.self.at;
        o.revision = ++g_observation_revision;
        read_self_exact(o);
        // Reference fallback for any unexpected displacement/death. Nonmoving attacks/spells do not
        // alter map opacity or owned wards, but killing/stunning a caster can cancel a TP reservation.
        if (!o.alive || o.self.at != before) { refresh_after_primitive(map, o); return; }
        read_items(o, true);
        read_commands(o);
        read_visible_champions(o);
        std::size_t kept = 0;
        for (auto camp : o.camps)
        {
            const auto id = static_cast<std::uint32_t>(camp.id);
            std::uint32_t state = 0;
            cambc::detail::check(cambc::detail::sdk_camp_state(id, &state));
            if (state == 0) continue;
            cambc::detail::camp_state info{};
            cambc::detail::check(cambc::detail::sdk_camp(id, &info));
            if (info.health <= 0.0f) continue;
            camp.hp = info.health;
            camp.at = { static_cast<std::int16_t>(info.x), static_cast<std::int16_t>(info.y) };
            camp.awake = state == 1;
            o.camps[kept++] = camp;
        }
        o.camps.resize(kept);
        const auto& groups = engine_groups();
        for (std::size_t g = 0; g < o.group_status.size() && g < groups.size(); ++g)
        {
            if (o.group_status[g] != gs_alive) continue;
            bool living = false;
            for (const auto& camp : o.camps) if (camp.group == static_cast<int>(g)) { living = true; break; }
            if (living) continue;
            std::uint32_t state = 0;
            const auto handle = static_cast<std::uint32_t>(static_cast<cambc::handle_camp_group_t>(groups[g]));
            cambc::detail::check(cambc::detail::sdk_camp_group_state(handle, &state));
            if (state == 1) o.group_status[g] = gs_alive;
            else if (state == 2) o.group_status[g] = gs_reward;
            else if (state == 3) o.group_status[g] = gs_respawning;
            else o.group_status[g] = gs_unknown;
        }
        read_altar_locks_exact(map, o);
        o.reserved_tiles.clear();
        for (const auto position : cambc::vision::teleport_tiles().positions()) o.reserved_tiles.push_back(T(position));
        o.reservation_facts = true;
        legal_moves(map, o, dash_range(o), false);
        read_ground_commands(map, o);
        return;
    }
    refresh_after_primitive(map, o);
}

void read_claims(Obs& o)
{
    const auto& allies = roster(cambc::team::ally);
    // Shared memory: one byte per ally roster slot: kind (3 bits, +1) and target (5 bits).
    std::uint8_t mem[8]{};
    try { cambc::shared_memory.read(0, std::span{ mem, 8 }); } catch (const std::exception&) {}
    for (std::size_t i = 0; i < 8; ++i)
    {
        o.claim_kind[i] = -1;
        o.claim_target[i] = -1;
        if (i >= allies.champs.size() || static_cast<int>(i) == o.self.roster || mem[i] == 0) continue;
        o.claim_kind[i] = static_cast<std::int8_t>((mem[i] >> 5) - 1);
        o.claim_target[i] = static_cast<std::int16_t>(mem[i] & 31);
    }
}

void publish_claim(int kind, int target)
{
    const auto& allies = roster(cambc::team::ally);
    int slot = -1;
    for (std::size_t i = 0; i < allies.ids.size(); ++i)
        if (allies.ids[i] == my_id()) slot = static_cast<int>(i);
    if (slot < 0 || slot >= 8) return;
    const std::uint8_t b = (kind < 0 || kind > 6 || target < 0 || target > 31) ? 0 : static_cast<std::uint8_t>(((kind + 1) << 5) | target);
    try { cambc::shared_memory.write(static_cast<std::uint32_t>(slot), std::span{ &b, 1 }); } catch (const std::exception&) {}
}

namespace {

// Big World's capped caller probe measured at most 4,249 fuel outside the SDK's field charge, including cold
// walls and eight-member roots. Keep more than twice that for wrapper work, cache insertion and returning.
// Preparation checks this floor between members, so a large group is spread over calls rather than assumed
// to have the measured shape. The SDK charge itself is field_words + chunks * expansion_count.
constexpr std::uint64_t kPathMargin = 10000;

struct WallCache
{
    cambc::bitboard walls;
    std::size_t group{}, member{};
    bool started{}, complete{};
};

WallCache& wall_cache() { static WallCache c; return c; }

// Static walls: terrain, altars, the enemy spawn and dangerous camp members. Preparation can yield between
// members; a goal group's own field later frees its member tiles.
bool prepare_walls(const Map& map, std::uint64_t reserve)
{
    auto& c = wall_cache();
    if (c.complete) return true;
    if (!c.started)
    {
        c.walls = cambc::map::walls() | cambc::map::holes() | cambc::map::altars() | cambc::map::spawn_region(cambc::team::enemy);
        c.started = true;
    }
    while (c.group < map.groups.size())
    {
        if (cambc::fuel_remaining() <= reserve + kPathMargin) return false;
        const auto& g = map.groups[c.group];
        if (!g.harmless && c.member < g.members.size())
        {
            c.walls.set(V(g.members[c.member].at), true);
            ++c.member;
        }
        else { ++c.group; c.member = 0; }
    }
    c.complete = true;
    return true;
}

struct PreparedField
{
    int key;
    cambc::bitboard walls, roots;
    std::size_t member{};
    bool complete{};
    explicit PreparedField(int k) : key(k), walls(wall_cache().walls) {}
};

struct CachedField
{
    cambc::distance_field distances;
    std::uint16_t cap{};
    bool complete{};
    std::unique_ptr<PreparedField> prepared;  // a retry reuses its roots and walls
};

struct FieldCache
{
    std::map<int, CachedField> fields;
    std::vector<int> dead;     // empty arrival sets, confirmed unreachable without calling compute
    std::vector<int> pending;  // missing or incomplete fields; a partial field rotates behind other requests
    std::unique_ptr<PreparedField> preparing;
};

FieldCache& cache()
{
    static FieldCache c;
    return c;
}

// Distance reads go through the SDK getter: measured 38 fuel a read, cheaper than computing the chunked storage
// index in WASM (64; 25 Oct, 00corners).
inline std::uint16_t field_get(const cambc::distance_field& f, Tile t) { return f.get(V(t)); }

// Decisions never build a field. Missing or unreached partial entries retain the existing center-Chebyshev
// fallback (not an exact distance to the arrival set). Reached partial entries are already exact.
int lookup(const Map& map, int team, int key, Tile from, Tile fallback_to)
{
    if (!map.walkable(from, team)) return kUnreachable;
    // As in BfsPaths, `from` is the actor position or a destination conditional on successful step/dash
    // completion. Assume only this origin vacant, without asserting acceptance of a conditional move.
    // Static camp members around it remain blocked; terrain never gets an exception.
    const auto camp_origin = [&] {
        const int group = map.group_of[static_cast<std::size_t>(map.idx(from))];
        return group >= 0 && !map.groups[static_cast<std::size_t>(group)].harmless;
    };
    const auto origin_is_root = [&] {
        if (!camp_origin()) return false;
        if (key == 200000)
            return (map.at(from) & (team == 0 ? tf_spawn_a : tf_spawn_b)) != 0;
        else if (key >= 100000) return cheb(from, map.altars[static_cast<std::size_t>(key - 100000)]) <= 1;
        else
            // Arrival membership is a bounded 3x3 map lookup, even for a large target group.
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const Tile at{ static_cast<std::int16_t>(from.x + dx), static_cast<std::int16_t>(from.y + dy) };
                    if (map.inside(at) && map.group_of[static_cast<std::size_t>(map.idx(at))] == key) return true;
                }
        return false;
    };
    auto& c = cache();
    auto it = c.fields.find(key);
    if (it == c.fields.end())
    {
        for (int k : c.dead)
            if (k == key) return origin_is_root() ? 0 : kUnreachable;
        if (std::find(c.pending.begin(), c.pending.end(), key) == c.pending.end()) c.pending.push_back(key);
        return origin_is_root() ? 0 : cheb(from, fallback_to);
    }
    const auto d = field_get(it->second.distances, from);
    if (d < cambc::distance_field::unreachable) return static_cast<int>(d);
    if (camp_origin())
    {
        if (origin_is_root()) return 0;
        // The walkable neighbours of the last origin asked (terrain is static: one scan serves every field).
        static Tile nb_from{ -1, -1 };
        static int nb_team = -1, nb_n = 0;
        static const Map* nb_map = nullptr;
        static Tile nb[8];
        if (!(nb_from == from) || nb_team != team || nb_map != &map)
        {
            nb_from = from;
            nb_team = team;
            nb_map = &map;
            nb_n = 0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                {
                    if (!dx && !dy) continue;
                    const Tile to{ static_cast<std::int16_t>(from.x + dx), static_cast<std::int16_t>(from.y + dy) };
                    if (map.walkable(to, team)) nb[nb_n++] = to;
                }
        }
        int best = kUnreachable;
        for (int k = 0; k < nb_n; ++k)
        {
            const auto distance = field_get(it->second.distances, nb[k]);
            if (distance < best) best = distance;
        }
        if (best < kUnreachable - 1) return best + 1;
        // Unreached neighbors in a capped field can still be beyond its current depth. A reached neighbor
        // already gives the exact departure distance; no reached neighbor does not prove unreachability.
    }
    return it->second.complete ? kUnreachable : cheb(from, fallback_to);
}

// Retain a 1.5x margin on the SDK charge, after preparation and allocation have actually consumed fuel.
// Capped probes at 1/3/20/192 confirmed expansion_count <= cap. A strict count < cap certifies completion;
// equality is conservatively left pending, including a search that happened to finish on the boundary.
std::uint16_t affordable_cap(std::uint64_t remaining, std::uint64_t reserve)
{
    if (remaining <= reserve || remaining - reserve <= kPathMargin) return 0;
    const auto& layout = cambc::detail::layout();
    const std::uint64_t chunks = static_cast<std::uint64_t>(layout.chunked_width) * layout.chunked_height;
    const std::uint64_t words = layout.field_bytes / 8u;
    const auto allowance = (remaining - reserve - kPathMargin) * 2u / 3u;
    if (!chunks || allowance <= words) return 0;
    return static_cast<std::uint16_t>(std::min<std::uint64_t>((allowance - words) / chunks, cambc::distance_field::unreachable - 1u));
}

bool prepare_field(const Map& map, PreparedField& p, std::uint64_t reserve)
{
    if (p.complete) return true;
    if (p.key == 200000) p.roots = cambc::map::spawn_region(cambc::team::ally);
    else if (p.key >= 100000) p.roots.draw_chebyshev(V(map.altars[static_cast<std::size_t>(p.key - 100000)]), 1);
    else
    {
        const auto& gr = map.groups[static_cast<std::size_t>(p.key)];
        while (p.member < gr.members.size())
        {
            if (cambc::fuel_remaining() <= reserve + kPathMargin) return false;
            const auto at = V(gr.members[p.member].at);
            p.walls.set(at, false);
            p.roots.draw_chebyshev(at, 1);
            ++p.member;
        }
    }
    if (cambc::fuel_remaining() <= reserve + kPathMargin) return false;
    p.roots &= ~p.walls;
    p.complete = true;
    return true;
}

}  // namespace

// Fast path: a reached tile of a cached field (the common case) is one map-flag test, one pointer-table read and
// one storage read; everything else (missing fields, unreached tiles, camp origins) takes the full lookup.
namespace {
std::vector<const cambc::distance_field*> g_field_ptr;  // by slot: groups, then altars, then spawn

int slot_of(const Map& map, int key)
{
    if (key == 200000) return static_cast<int>(map.groups.size() + map.altars.size());
    if (key >= 100000) return static_cast<int>(map.groups.size()) + key - 100000;
    return key;
}

inline bool fast_lookup(const Map& map, int team, int key, Tile from, int& out)
{
    if (!map.walkable(from, team)) { out = kUnreachable; return true; }
    const auto slot = static_cast<std::size_t>(slot_of(map, key));
    if (slot >= g_field_ptr.size() || !g_field_ptr[slot]) return false;
    const auto d = field_get(*g_field_ptr[slot], from);
    if (d >= cambc::distance_field::unreachable) return false;
    out = static_cast<int>(d);
    return true;
}

void register_field(const Map& map, int key, const cambc::distance_field* f)
{
    const auto n = map.groups.size() + map.altars.size() + 1;
    if (g_field_ptr.size() != n) g_field_ptr.assign(n, nullptr);
    const auto slot = static_cast<std::size_t>(slot_of(map, key));
    if (slot < n) g_field_ptr[slot] = f;
}
}  // namespace

// Per-turn memo (a direct-mapped table): within a turn the fields do not change (they are built after the
// decisions), so a repeated query (the own tile asked by the state, goal, step and tail facts) is answered once.
int EnginePaths::memo(int key, Tile from, Tile fallback)
{
    const auto h = (static_cast<std::uint32_t>(key) * 2654435761u ^ static_cast<std::uint32_t>(from.x) * 40503u ^
                    static_cast<std::uint32_t>(from.y) * 9973u) & (kMemo - 1);
    auto& e = memo_[h];
    if (e.epoch == epoch_ && e.key == key && e.at == from) return e.d;
    int d;
    if (!fast_lookup(map_, team_, key, from, d)) d = lookup(map_, team_, key, from, fallback);
    e = Memo{ epoch_, key, from, d };
    return d;
}
int EnginePaths::to_group(int group, Tile from) { return memo(group, from, map_.groups[static_cast<std::size_t>(group)].center); }
int EnginePaths::to_altar(int altar, Tile from) { return memo(100000 + altar, from, map_.altars[static_cast<std::size_t>(altar)]); }
int EnginePaths::to_spawn(Tile from) { return memo(200000, from, map_.spawn_center[team_]); }

void EnginePaths::warm(std::uint64_t reserve)
{
    ++epoch_;  // fields may change below: the memo is for one turn's decisions
    auto& c = cache();
    // The spawn field first (every decision uses it), then queued fields in request order.
    if (c.fields.find(200000) == c.fields.end() && std::find(c.dead.begin(), c.dead.end(), 200000) == c.dead.end() &&
        std::find(c.pending.begin(), c.pending.end(), 200000) == c.pending.end())
        c.pending.insert(c.pending.begin(), 200000);
    if (cambc::fuel_remaining() <= reserve + kPathMargin || !prepare_walls(map_, reserve)) return;
    auto rotate = [&] {
        const int key = c.pending.front();
        const auto old = c.fields.find(key);
        if (c.preparing && old != c.fields.end()) old->second.prepared = std::move(c.preparing);
        c.pending.erase(c.pending.begin());
        c.pending.push_back(key);
        c.preparing.reset();
    };
    std::size_t scanned = c.pending.size();
    while (scanned-- && !c.pending.empty() && cambc::fuel_remaining() > reserve + kPathMargin)
    {
        const int key = c.pending.front();
        const auto previous = c.fields.find(key);
        if (previous != c.fields.end() && previous->second.complete)
        {
            c.pending.erase(c.pending.begin());
            c.preparing.reset();
            continue;
        }
        const auto old_cap = previous == c.fields.end() ? 0u : previous->second.cap;
        if (c.preparing && !affordable_cap(cambc::fuel_remaining(), reserve)) return;
        if (affordable_cap(cambc::fuel_remaining(), reserve) <= old_cap) { rotate(); continue; }
        if (!c.preparing && previous != c.fields.end()) c.preparing = std::move(previous->second.prepared);
        if (!c.preparing) c.preparing = std::make_unique<PreparedField>(key);
        auto& prepared = *c.preparing;
        if (!prepare_field(map_, prepared, reserve)) return;
        if (prepared.roots.is_empty())
        {
            c.dead.push_back(key);
            c.pending.erase(c.pending.begin());
            c.preparing.reset();
            continue;
        }
        if (affordable_cap(cambc::fuel_remaining(), reserve) <= old_cap)
        {
            if (previous == c.fields.end()) return;  // retain this first preparation for a fresher call
            rotate();
            continue;
        }
        cambc::distance_field f;
        cambc::bitboard visited;
        const auto cap = affordable_cap(cambc::fuel_remaining(), reserve);
        if (cap <= old_cap)
        {
            if (previous != c.fields.end()) rotate();
            return;
        }
        // Unexpected SDK failures propagate to the turn boundary, with this request still pending.
        const auto expansions = f.compute_with_expansion_count(prepared.walls, prepared.roots, visited, cap);
        const bool complete = expansions < cap;
        const bool first = previous == c.fields.end();
        auto retry = complete ? std::unique_ptr<PreparedField>{} : std::move(c.preparing);
        const auto placed = c.fields.insert_or_assign(key, CachedField{ std::move(f), cap, complete, std::move(retry) });
        register_field(map_, key, &placed.first->second.distances);
        if (first) ++built_;
        if (complete)
        {
            c.pending.erase(c.pending.begin());
            c.preparing.reset();
        }
        else rotate();
        return;  // at most one SDK field computation per call
    }
}

bool act_step(const Obs& obs, const Cand& c)
{
    auto& me = cambc::self();
    try
    {
        switch (c.kind)
        {
        case 0:
            release();
            // A champion seen on the destination now (the observation may predate it): stay instead of an error.
            if (cambc::vision::champion_tiles().is_set(V(c.at))) return false;
            return me.move_absolute(V(c.at));
        case 2:  // a standing recall is kept as it is (one turn end done)
            if (!g_standing) me.recall();
            g_standing = false;
            g_committed = true;
            return true;
        case 3: release(); me.submit_gems(V(c.at)); return true;  // c.at = the altar
        default: return true;                         // stay
        }
    }
    catch (const std::exception& e)
    {
        std::printf("error step kind %d to %d,%d from %d,%d: %s\n", c.kind, c.at.x, c.at.y, obs.self.at.x, obs.self.at.y, e.what());
        return false;
    }
}

bool act_attack(const Obs& obs, const Cand& c)
{
    (void)obs;
    if (c.kind == 0) return true;
    release();
    auto& me = cambc::self();
    try
    {
        // The observation can predate the attack (a step changed our vision, a target died or left): one cheap
        // query confirms the target is still visible; a gone target is no attack, not an engine error.
        if (c.kind == 1)
        {
            std::int32_t present = 0;
            if (cambc::detail::sdk_has_camp(c.at.x, c.at.y, &present) != 0 || !present) return false;
            me.attack(cambc::vision::camp_at(V(c.at)));
            return true;
        }
        else
        {
            cambc::detail::champion_info info{};
            if (c.target < 0 || cambc::detail::sdk_champion(static_cast<std::uint32_t>(c.target), &info) != 0 || info.health <= 0.0f) return false;
            const auto& er = roster(cambc::team::enemy);
            for (std::size_t i = 0; i < er.ids.size(); ++i)
                if (er.ids[i] == c.target) { me.attack(er.champs[i]); return true; }
        }
    }
    catch (const std::exception& e)
    {
        std::printf("error attack: %s\n", e.what());
    }
    return false;
}

bool act_cast(const Obs& obs, int slot, const Cand& c)
{
    (void)obs;
    if (c.kind == 0) return true;
    release();
    auto& me = cambc::self();
    try
    {
        const auto abilities = me.abilities();
        const auto& ab = abilities[static_cast<std::size_t>(slot)];
        if (c.target == my_id()) return ab.use();
        if (c.target <= -2)  // camp target: -2 - group, at c.at
        {
            std::int32_t present = 0;
            if (cambc::detail::sdk_has_camp(c.at.x, c.at.y, &present) != 0 || !present) return false;
            return ab.use(cambc::vision::camp_at(V(c.at)));
        }
        if (c.target >= 0)
        {
            for (const auto side : { cambc::team::enemy, cambc::team::ally })
            {
                const auto& r = roster(side);
                for (std::size_t i = 0; i < r.ids.size(); ++i)
                    if (r.ids[i] == c.target) return ab.use(r.champs[i]);
            }
            return false;
        }
        if (cambc::vision::champion_tiles().is_set(V(c.at))) return false;  // landing taken since we looked
        return ab.use(V(c.at));  // ground (dash landing)
    }
    catch (const std::exception& e)
    {
        std::printf("error cast %d: %s\n", slot, e.what());
        return false;
    }
}

bool act_buy(const Cand& c)
{
    if (c.kind == 0) return true;
    auto& me = cambc::self();
    try
    {
        const auto item = static_cast<cambc::item_type>(c.target);
        if (!me.can_buy(item, 1)) return false;
        me.buy(item, 1);
        return true;
    }
    catch (const std::exception& e)
    {
        std::printf("error buy: %s\n", e.what());
        return false;
    }
}

bool home_now()
{
    static const auto& spawn = cambc::map::spawn_region(cambc::team::ally);
    try { return spawn.is_set(cambc::self().position()); }
    catch (const std::exception&) { return true; }
}

PrimitiveResult primitive_result(ActionType type, const Obs& obs, const Cand& c)
{
    (void)obs;
    if (type == a_end) return PrimitiveResult::accepted;
    if (type == a_replan || type >= a_count || !g_explicit_channels) return PrimitiveResult::rejected;
    auto& me = cambc::self();
    try
    {
        if (type == a_buy)
        {
            if (c.target < 0 || c.target >= static_cast<int>(cambc::item_type::count) || c.count <= 0) return PrimitiveResult::rejected;
            const auto item = static_cast<cambc::item_type>(c.target);
            const auto count = static_cast<std::uint32_t>(c.count);
            // Public can_buy multiplies live metadata cost by count in uint64, so unaffordable and
            // uint32-price-overflow requests are rejected before the one bulk engine command.
            if (!me.can_buy(item, count)) return PrimitiveResult::rejected;
            me.buy(item, count);
            return PrimitiveResult::accepted;
        }
        if (type == a_buy_consumable)
        {
            if (c.target < 0 || c.target >= static_cast<int>(cambc::consumable_type::count)) return PrimitiveResult::rejected;
            const auto item = static_cast<cambc::consumable_type>(c.target);
            if (!me.can_buy(item)) return PrimitiveResult::rejected;
            me.buy(item);
            return PrimitiveResult::accepted;
        }
        if (type == a_sell)
        {
            if (c.target < 0 || c.target >= static_cast<int>(cambc::item_type::count) || !me.can_sell()) return PrimitiveResult::rejected;
            const auto item = static_cast<cambc::item_type>(c.target);
            if (!me.item_count()[item]) return PrimitiveResult::rejected;
            me.sell(item, 1);
            return PrimitiveResult::accepted;
        }
        if (type == a_priority)
        {
            me.set_priority(c.target);
            return PrimitiveResult::accepted;
        }
        if (!me.is_alive()) return PrimitiveResult::rejected;
        const auto state = me.state();
        const bool channel = state.contains(cambc::champion_state::recalling) ||
            state.contains(cambc::champion_state::submitting_gems) || state.contains(cambc::champion_state::teleporting);
        if (type == a_cancel)
        {
            if (state.contains(cambc::champion_state::recalling)) me.cancel_recall();
            else if (state.contains(cambc::champion_state::submitting_gems)) me.cancel_submit_gems();
            else return PrimitiveResult::rejected;  // teleport crystals are not cancellable
            return PrimitiveResult::accepted;
        }
        if (channel) return PrimitiveResult::rejected;
        const auto accepted = [](bool ok) { return ok ? PrimitiveResult::accepted : PrimitiveResult::rejected; };
        switch (type)
        {
        case a_move:
            if (!state.contains(cambc::champion_state::can_move)) return PrimitiveResult::rejected;
            return accepted(me.move_absolute(V(c.at)));
        case a_attack:
            if (!state.contains(cambc::champion_state::can_attack)) return PrimitiveResult::rejected;
            if (c.kind == 1)
            {
                std::uint32_t handle = 0;
                cambc::detail::check(cambc::detail::sdk_camp_at(c.at.x, c.at.y, &handle));
                cambc::detail::check(cambc::detail::sdk_attack(static_cast<std::uint32_t>(cambc::detail::entity_kind::camp), handle));
            }
            else if (c.kind == 3)
            {
                // A friendly and enemy lock may coexist; the generic SDK lookup prefers friendly.
                // This argument family explicitly targets the enemy lock represented by lk_theirs.
                cambc::detail::altar_lock_info lock{};
                cambc::detail::check(cambc::detail::sdk_altar_lock(c.at.x, c.at.y, 1 - absolute_team(), &lock));
                cambc::detail::check(cambc::detail::sdk_attack(static_cast<std::uint32_t>(cambc::detail::entity_kind::altar_lock), lock.handle));
            }
            else
            {
                if (c.target < 0) return PrimitiveResult::rejected;
                cambc::detail::check(cambc::detail::sdk_attack(static_cast<std::uint32_t>(cambc::detail::entity_kind::champion), static_cast<std::uint32_t>(c.target)));
            }
            return PrimitiveResult::accepted;
        case a_cast0:
        case a_cast1: {
            if (!state.contains(cambc::champion_state::can_cast)) return PrimitiveResult::rejected;
            const auto slot = static_cast<std::uint32_t>(type == a_cast0 ? 0 : 1);
            std::int32_t cooldown = 0;
            cambc::detail::check(cambc::detail::sdk_champion_cooldown(static_cast<std::uint32_t>(my_id()), slot, &cooldown));
            if (cooldown != 0) return PrimitiveResult::rejected;
            std::uint32_t kind = 0, handle = 0;
            cambc::vec2 at = V(c.at);
            if (c.target == my_id()) at = { -1, -1 };
            else if (c.target <= -2)
            {
                kind = static_cast<std::uint32_t>(cambc::detail::entity_kind::camp);
                cambc::detail::check(cambc::detail::sdk_camp_at(c.at.x, c.at.y, &handle));
                at = { -1, -1 };
            }
            else if (c.target >= 0)
            {
                kind = static_cast<std::uint32_t>(cambc::detail::entity_kind::champion);
                handle = static_cast<std::uint32_t>(c.target);
                at = { -1, -1 };
            }
            std::int32_t success = 0;
            cambc::detail::check(cambc::detail::sdk_cast(slot, kind, handle, at.x, at.y, &success));
            return accepted(success != 0);
        }
        case a_recall:
            me.recall();
            return PrimitiveResult::accepted;
        case a_tribute:
            me.submit_gems(V(c.at));
            return PrimitiveResult::accepted;
        case a_use_consumable:
            if (c.target < 0 || c.target >= static_cast<int>(cambc::consumable_type::count)) return PrimitiveResult::rejected;
            return accepted(me.consumables()[static_cast<cambc::consumable_type>(c.target)].use(V(c.at)));
        case a_ward:
            return accepted(me.place_ward(V(c.at)));
        default:
            return PrimitiveResult::rejected;
        }
    }
    catch (const cambc::error& e)
    {
        const int code = static_cast<int>(e.code());
        // The host is the final validator. Known command rejections are ordinary rejected attempts; API,
        // memory and engine errors are surfaced so the turn boundary can stop rather than keep guessing.
        if ((code >= 18 && code <= 65) || (code >= 113 && code <= 120) || (code >= 123 && code <= 133) ||
            e.code() == cambc::error_code::api_not_found || e.code() == cambc::error_code::api_not_in_vision)
            return PrimitiveResult::rejected;
        std::printf("error primitive %d sdk %d: %s\n", static_cast<int>(type), code, e.what());
    }
    catch (const std::exception& e)
    {
        std::printf("error primitive %d: %s\n", static_cast<int>(type), e.what());
    }
    catch (...)
    {
        std::printf("error primitive %d: unknown engine exception\n", static_cast<int>(type));
    }
    return PrimitiveResult::unexpected_error;
}

bool primitive(ActionType type, const Obs& obs, const Cand& c)
{
    return primitive_result(type, obs, c) == PrimitiveResult::accepted;
}

void end_turn_recall(bool home, bool start)
{
    if (g_explicit_channels) return;
    g_tail_ran = true;
    if (g_committed || home) return;
    auto& me = cambc::self();
    try
    {
        // A standing recall not kept this turn is cancelled (it must not count turn ends nobody chose).
        if (g_standing) me.cancel_recall();
        g_standing = false;
        if (!start) return;
        me.recall();
        g_standing = true;
    }
    catch (const std::exception&)
    {
        g_standing = false;  // stunned, channelling a tribute, or otherwise unable: nothing started
    }
}

void settle_recall()
{
    if (g_explicit_channels) return;
    if (!g_tail_ran) end_turn_recall(false, false);
    g_tail_ran = false;
}

}  // namespace zero::sdk

