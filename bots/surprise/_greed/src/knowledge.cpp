#include "knowledge.h"
#include <algorithm>
#include <array>
#include <variant>
#include "config.h"
#include "ctx.h"
#include "model.h"
#include "world.h"

namespace mb {

Knowledge& Knowledge::get()
{
    static Knowledge knowledge;
    return knowledge;
}

Knowledge::Knowledge()
{
    const auto& world = World::get();
    groups.resize(world.groups.size());
    allies = cambc::champions(cambc::team::ally);
    std::sort(allies.begin(), allies.end(), [](const cambc::champion& a, const cambc::champion& b) {
        return static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(a)) <
               static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(b));
    });
    const auto& me = cambc::self();
    for (std::size_t i = 0; i < allies.size(); ++i)
        if (allies[i] == me) self_index = static_cast<int>(i);
    slots_.resize(allies.size());
    previous_.resize(allies.size());
    // Squad partition from immutable designs: power ~ damage rate x health, melee from range.
    std::vector<float> power;
    std::vector<bool> melee;
    std::vector<int> linked(allies.size(), -1);
    std::vector<cambc::champion_type> types;
    for (const auto& c : allies)
    {
        const auto type = c.champion_name();
        types.push_back(type);
        const auto s = design_summary(type);
        power.push_back(model::camp_dps(s, 0.0f, cfg::camp_dash_damage) * s.health / 1000.0f);
        melee.push_back(s.attack_range <= 1);
    }
    for (std::size_t i = 0; i < types.size(); ++i)
        for (std::size_t j = 0; j < types.size(); ++j)
            if ((types[i] == cambc::champion_type::cassi && types[j] == cambc::champion_type::issac) ||
                (types[i] == cambc::champion_type::issac && types[j] == cambc::champion_type::cassi))
                linked[i] = static_cast<int>(j);
    if (cfg::squad_size >= 3)
        squad_of = model::partition_squads(power, melee, linked);
    else if (cfg::squad_size == 2)
        squad_of = model::partition_small_squads(power, linked, 2);
    else
    {
        squad_of.resize(allies.size());
        for (std::size_t i = 0; i < squad_of.size(); ++i) squad_of[i] = static_cast<int>(i);  // everyone leads itself
    }
    alive.assign(allies.size(), 1);
}

void Knowledge::observe_groups(const Ctx& ctx, int /*vision_radius*/)
{
    auto& world = World::get();
    const std::size_t n_groups = world.groups.size();
    // Visible member tiles per group: one mask intersection instead of per-tile queries.
    std::vector<std::uint8_t> seen(n_groups, 0), living(n_groups, 0), awake(n_groups, 0);
    std::vector<float> hp(n_groups, 0.0f);
    for (auto p : (ctx.visible & world.camp_spawns).positions())
    {
        const int g = world.group_at(p);
        if (g >= 0 && seen[static_cast<std::size_t>(g)] < 255) ++seen[static_cast<std::size_t>(g)];
    }
    for (const auto& c : ctx.camps)
    {
        const auto g = static_cast<std::size_t>(c.group);
        hp[g] += c.hp;
        if (living[g] < 255) ++living[g];
        if (c.awake) awake[g] = 1;
    }
    for (std::size_t i = 0; i < n_groups; ++i)
    {
        if (seen[i] == 0) continue;
        const auto& info = world.groups[i];
        const int members = static_cast<int>(info.tiles.size());
        const float unseen_hp = members > seen[i] ? info.total_hp * static_cast<float>(members - seen[i]) / static_cast<float>(members) : 0.0f;
        auto& belief = groups[i];
        belief.seen_round = round;
        if (living[i] > 0)
        {
            belief.status = GroupStatus::alive;
            belief.hp_left = hp[i] + unseen_hp;
            belief.awake = awake[i] != 0;
            continue;
        }
        // A respawn timer we already read cannot change before it expires (the query costs ~1K).
        if (belief.status == GroupStatus::respawning && belief.respawn_round > round) continue;
        const auto state = world.group_objects[i].state();
        if (std::holds_alternative<cambc::map::camp_group_respawning>(state))
        {
            belief.status = GroupStatus::respawning;
            belief.respawn_round = round + std::get<cambc::map::camp_group_respawning>(state).timer;
            belief.awake = false;
            belief.hp_left = -1.0f;
        }
        else if (std::holds_alternative<cambc::map::camp_group_reward_available>(state))
        {
            belief.status = GroupStatus::reward;
            belief.awake = false;
            belief.hp_left = 0.0f;
        }
        else
        {
            belief.status = GroupStatus::alive;
            belief.hp_left = unseen_hp;
            belief.awake = false;
        }
    }
}

void Knowledge::mark_respawning(int group, int delay)
{
    if (group < 0 || group >= static_cast<int>(groups.size())) return;
    auto& belief = groups[static_cast<std::size_t>(group)];
    belief.status = GroupStatus::respawning;
    belief.respawn_round = round + delay;
    belief.hp_left = -1.0f;
    belief.awake = false;
}

bool Knowledge::available_by(int group, int at) const
{
    const auto& belief = groups[static_cast<std::size_t>(group)];
    if (belief.status != GroupStatus::respawning) return true;
    return belief.respawn_round <= at;
}

void Knowledge::read_slots()
{
    const std::size_t capacity = std::min<std::size_t>(cambc::shared_memory_buffer::size(), slots_.size());
    std::array<std::byte, 16> raw{};
    if (capacity == 0) return;
    cambc::shared_memory.read(0, std::span<std::byte>{ raw.data(), capacity });
    const auto& world = World::get();
    for (std::size_t i = 0; i < capacity; ++i)
    {
        const auto value = std::to_integer<std::uint8_t>(raw[i]);
        AllySlot slot;
        slot.group = static_cast<int>(value & 63u) - 1;
        slot.status = static_cast<SlotStatus>(value >> 6);
        if (slot.group >= static_cast<int>(world.groups.size())) slot.group = -1;
        if (static_cast<int>(i) != self_index)
        {
            const auto& old = previous_[i];
            const bool finished_capture = old.status == SlotStatus::capturing && old.group >= 0 &&
                                          !(slot.status == SlotStatus::capturing && slot.group == old.group);
            const bool cleared = slot.status == SlotStatus::cleared && slot.group >= 0;
            const int g = cleared ? slot.group : old.group;
            if ((finished_capture || cleared) && g >= 0 && groups[static_cast<std::size_t>(g)].status != GroupStatus::respawning)
                mark_respawning(g, world.groups[static_cast<std::size_t>(g)].respawn);
        }
        previous_[i] = slot;
        slots_[i] = slot;
    }
    for (std::size_t i = 0; i < allies.size(); ++i)
    {
        try
        {
            alive[i] = allies[i].is_alive() ? 1 : 0;
        }
        catch (...)
        {
            alive[i] = 1;
        }
    }
    recount();
}

void Knowledge::write_sos(vec2 at, int round)
{
    const std::size_t size = cambc::shared_memory_buffer::size();
    if (size < slots_.size() + 3 || at.x < 0 || at.x > 254 || at.y < 0 || at.y > 255) return;
    const std::array<std::byte, 3> b{ static_cast<std::byte>(at.x + 1), static_cast<std::byte>(at.y), static_cast<std::byte>(round & 255) };
    cambc::shared_memory.write(size - 3, std::span<const std::byte>{ b.data(), b.size() });
}

bool Knowledge::read_sos(vec2& at, int& age, int round) const
{
    const std::size_t size = cambc::shared_memory_buffer::size();
    if (size < slots_.size() + 3) return false;
    std::array<std::byte, 3> b{};
    cambc::shared_memory.read(size - 3, std::span<std::byte>{ b.data(), b.size() });
    const int x = static_cast<int>(b[0]);
    if (x == 0) return false;
    at = { x - 1, static_cast<int>(b[1]) };
    age = (round - static_cast<int>(b[2])) & 255;
    return true;
}

void Knowledge::write_sighting(vec2 at, int index, int round)
{
    const std::size_t size = cambc::shared_memory_buffer::size();
    if (size < slots_.size() + 3 || at.x < 0 || at.x > 254 || at.y < 0 || at.y > 255) return;
    const std::array<std::byte, 3> b{ static_cast<std::byte>(at.x + 1), static_cast<std::byte>(at.y),
                                      static_cast<std::byte>((((round / 2) & 31) << 3) | (index & 7)) };
    cambc::shared_memory.write(size - 3, std::span<const std::byte>{ b.data(), b.size() });
}

bool Knowledge::read_sighting(vec2& at, int& index, int& age, int round) const
{
    const std::size_t size = cambc::shared_memory_buffer::size();
    if (size < slots_.size() + 3) return false;
    std::array<std::byte, 3> b{};
    cambc::shared_memory.read(size - 3, std::span<std::byte>{ b.data(), b.size() });
    const int x = static_cast<int>(b[0]);
    if (x == 0) return false;
    at = { x - 1, static_cast<int>(b[1]) };
    const int packed = static_cast<int>(b[2]);
    index = packed & 7;
    age = 2 * (((round / 2) - (packed >> 3)) & 31);
    return true;
}

void Knowledge::write_slot(int group, SlotStatus status)
{
    if (self_index >= static_cast<int>(cambc::shared_memory_buffer::size())) return;
    const std::uint8_t value = static_cast<std::uint8_t>(((group + 1) & 63) | (static_cast<int>(status) << 6));
    const std::byte b{ value };
    cambc::shared_memory.write(static_cast<std::size_t>(self_index), std::span<const std::byte>{ &b, 1 });
    slots_[static_cast<std::size_t>(self_index)] = { group, status };
    previous_[static_cast<std::size_t>(self_index)] = { group, status };
}

int Knowledge::squad_leader(int squad) const
{
    for (std::size_t i = 0; i < squad_of.size(); ++i)
        if (squad_of[i] == squad && alive[i]) return static_cast<int>(i);
    return -1;
}

int Knowledge::squad_size(int squad) const
{
    int n = 0;
    for (std::size_t i = 0; i < squad_of.size(); ++i)
        if (squad_of[i] == squad && alive[i]) ++n;
    return n;
}

void Knowledge::recount()
{
    const auto n = World::get().groups.size();
    committed_.assign(n, 0);
    capturers_.assign(n, 0);
    for (std::size_t i = 0; i < slots_.size(); ++i)
    {
        if (static_cast<int>(i) == self_index) continue;
        const int g = slots_[i].group;
        if (g < 0 || g >= static_cast<int>(n)) continue;
        if (slots_[i].status == SlotStatus::engaging || slots_[i].status == SlotStatus::capturing) ++committed_[static_cast<std::size_t>(g)];
        if (slots_[i].status == SlotStatus::capturing) ++capturers_[static_cast<std::size_t>(g)];
    }
}

} // namespace mb
