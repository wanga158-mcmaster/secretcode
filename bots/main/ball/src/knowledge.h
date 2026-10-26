#pragma once
// Per-instance beliefs about camp groups and allies, plus the shared-memory protocol.
//
// Mechanics assumed: vision is per champion (allies' views are not shared); a group's live
// state is only readable while one of its spawn tiles is in our vision; all groups start
// alive; a captured group respawns after its max respawn timer (GameRules "What happens
// next?"). Shared memory is 8 bytes (queried at runtime), readable/writable by allies only.
#include <cambc.h>
#include <cstdint>
#include <vector>
#include "util.h"

namespace mb {

struct Ctx;

enum class GroupStatus : std::uint8_t { alive, reward, respawning };

struct GroupBelief
{
    GroupStatus status{ GroupStatus::alive };
    int respawn_round{};     // estimated round the group is alive again (respawning only)
    int seen_round{ -1 };    // last round a member tile was in our vision
    float hp_left{ -1.0f };  // living members' total health when last seen; -1 = assume full
    bool awake{};
    int enemy_round{ -1000 };  // last round enemy champions were seen near the group
};

// Slot status published by each champion for its current target group.
enum class SlotStatus : std::uint8_t { engaging = 0, capturing = 1, cleared = 2, idle = 3 };

struct AllySlot
{
    int group{ -1 };  // -1: none
    SlotStatus status{ SlotStatus::idle };
};

class Knowledge
{
public:
    static Knowledge& get();

    std::vector<GroupBelief> groups;
    std::vector<cambc::champion> allies;  // sorted by handle; includes self
    int self_index{};
    int round{};

    // Refresh beliefs for groups with a member tile in this turn's vision, using the
    // turn's living-camp views; only reward/respawn distinctions need a host query.
    void observe_groups(const Ctx& ctx, int vision_radius);
    // Mark a group cleared/captured at `round` with an estimated respawn delay.
    void mark_respawning(int group, int delay);
    // Is the group expected to be alive (or reward available) by round `at`?
    bool available_by(int group, int at) const;

    // Shared memory: read all ally slots, write our own.
    void read_slots();
    void write_slot(int group, SlotStatus status);
    // SOS beacon in the last three shared-memory bytes (x + 1, y, round & 255; 0 = none), used
    // only when those bytes are clear of the ally slots.
    void write_sos(vec2 at, int round);
    bool read_sos(vec2& at, int& age, int round) const;
    // Enemy sighting in the same three bytes (cfg::share_sightings): x + 1, y, then ((round / 2) & 31) << 3
    // | (enemy roster index & 7); ages up to 62 rounds.
    void write_sighting(vec2 at, int index, int round);
    bool read_sighting(vec2& at, int& index, int& age, int round) const;
    // Enemy pack sighting (cfg::phantom_mode 2) in the same three bytes: x + 1, y, then
    // (size & 7) << 5 | (round & 31). A reader clears a record older than `max_age` rounds.
    void write_pack(vec2 at, int size, int round);
    bool read_pack(vec2& at, int& size, int& age, int round, int max_age);
    const std::vector<AllySlot>& slots() const { return slots_; }
    // Squads (model::partition_squads): fixed at start; the lowest-index living member leads.
    std::vector<int> squad_of;
    std::vector<std::uint8_t> alive;  // refreshed each turn
    int my_squad() const { return squad_of[static_cast<std::size_t>(self_index)]; }
    int squad_leader(int squad) const;
    int squad_size(int squad) const;

    int committed(int group) const { return group >= 0 && group < static_cast<int>(committed_.size()) ? committed_[static_cast<std::size_t>(group)] : 0; }
    int capturers(int group) const { return group >= 0 && group < static_cast<int>(capturers_.size()) ? capturers_[static_cast<std::size_t>(group)] : 0; }

private:
    Knowledge();
    void recount();
    std::vector<AllySlot> slots_;
    std::vector<AllySlot> previous_;
    std::vector<std::uint8_t> committed_, capturers_;  // per group, allies other than us
};

} // namespace mb
