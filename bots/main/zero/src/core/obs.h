#pragma once
// One champion's observation for one turn (facts it can know), and the memory it carries between turns.
// Filled by the SDK adapter (src/sdk_obs.cpp) or the replay adapter (tools/replay_obs.cpp): features are
// computed from these structs only, so the bot and the imitation data see the same inputs.
#include <array>
#include <algorithm>
#include <cstdint>
#include <vector>
#include "map.h"

namespace zero {

inline constexpr int kMaxItems = 48;

struct UnitView
{
    int id{ -1 };           // engine actor id (stable)
    int roster{ -1 };       // index in its team's roster
    std::uint8_t type{};    // champion_type
    Tile at;
    float hp{}, max_hp{}, strength{}, focus{};
    std::uint32_t gems{}, gold{};
    std::int8_t cooldown[2]{ -1, -1 };  // -1 unknown
    bool recalling{}, tributing{}, stunned{};
    float shield{};
};

struct CampView
{
    int id{ -1 };  // stable engine camp handle when supplied by an exact adapter
    Tile at;
    std::uint8_t type{};
    float hp{};
    bool awake{};
    int group{ -1 };
};

enum GroupStatus : std::uint8_t { gs_unknown = 0, gs_alive, gs_reward, gs_respawning };
enum Lock : std::uint8_t { lk_none = 0, lk_ours, lk_theirs };

// Engine-validated utility arguments. `type` is an ActionType value; keeping it numeric avoids an
// observation/action include cycle. The SDK owns facts 15..23, and the action layer supplies type facts.
struct UtilityAction
{
    std::uint8_t type{};
    std::int32_t target{ -1 };
    Tile at{};
    float phi[24]{};
};

struct Obs
{
    std::uint64_t revision{};  // changes at every SDK observation/refresh, including within one round
    int round{};
    int team{};  // absolute: 0 = A, 1 = B
    int command_index{};  // issued primitive/internal commands in this owner turn; exact self history
    // Self.
    UnitView self;
    bool alive{ true }, can_move{}, can_attack{}, can_cast{}, on_spawn{};
    // A standing recall (sdk::end_turn_recall) is running: self.recalling stays false, the step head's recall keeps
    // it (one turn end already counted) and any other action cancels it first.
    bool recall_started{};
    // Explicit command controller facts, read live only in its opt-in SDK mode. No allowance is fabricated
    // for an active channel. A missing utility-known bit means unsupported, not unavailable.
    bool command_facts{}, teleporting{}, can_recall{}, can_tribute{}, can_cancel{}, can_buy{}, can_priority{};
    bool can_cast_slot[2]{};  // SDK readiness (present slot, zero cooldown, generic live cast allowance)
    std::uint32_t state_bits{}, utility_known_mask{};
    std::uint32_t utility_known_consumables{}, utility_capability_limits{}, utility_affected_limits{};
    bool ward_arguments_complete{}, consumable_arguments_complete{};
    std::uint8_t restriction_bits{};
    int channel_remaining{}, ward_charges{}, ward_cooldown{}, priority{};
    std::array<std::uint32_t, 2> consumables{};
    std::array<std::uint32_t, kMaxItems> item_counts{};  // exact live counts; legacy `items` stays wire-compatible
    std::array<bool, kMaxItems> buyable_items{};
    std::vector<Tile> wards;
    bool ally_ward_facts{};
    std::vector<Tile> ally_ward_tiles;  // personally known allied wards, including wards owned by teammates
    std::vector<UtilityAction> utility_actions;
    int respawn_in{};  // rounds until respawn while dead
    std::array<std::uint8_t, kMaxItems> items{};  // owned count per item id
    bool ward_ready{};
    // Legal destinations (the engine's movement mask: walls, holes, altars, enemy spawn and known occupants
    // excluded): one ordinary step, and dash landings within the dash range (dash paths may cross holes).
    std::vector<Tile> step_ok, dash_ok;
    bool dash_destinations_known{};
    int active_dash_range{};
    bool reservation_facts{};
    std::vector<Tile> reserved_tiles;  // visible teleport reservations, physical blockers in exact adapters
    // What we see.
    std::vector<UnitView> allies, enemies;  // visible, alive, excluding self
    std::vector<CampView> camps;            // visible, alive
    std::vector<std::uint8_t> group_status; // GroupStatus per group (this turn's view where visible)
    std::vector<Lock> altar_lock;
    std::vector<int> altar_lock_hits;  // aligned altars; current represented lock's remaining hits, -1 unknown
    // Global facts.
    std::uint32_t banked_us{}, banked_them{};
    int allies_alive{}, enemies_alive{};
    // Shared-memory claims of allies (roster index -> goal kind / target), -1 none.
    std::array<std::int8_t, 8> claim_kind{ -1, -1, -1, -1, -1, -1, -1, -1 };
    std::array<std::int16_t, 8> claim_target{ -1, -1, -1, -1, -1, -1, -1, -1 };
};

// The public SDK state-bit ABI is shared with native exact-record conversion. Stun comes from the live
// buff collection, not from guessing that a spent allowance is a stun. Destination legality is separate.
inline void derive_command_state(Obs& o)
{
    const auto bit = [&](int i) { return (o.state_bits & (1u << i)) != 0; };
    o.self.recalling = o.alive && bit(1);
    o.self.tributing = o.alive && bit(2);
    o.teleporting = o.alive && bit(12);
    const bool free = o.alive && !o.self.recalling && !o.self.tributing && !o.teleporting && !o.self.stunned && !bit(5);
    o.can_recall = free && !bit(6);
    o.can_tribute = free && o.self.gems > 0 && !bit(7);
    o.can_cancel = o.alive && !o.teleporting && ((o.self.recalling && !bit(8)) || (o.self.tributing && !bit(9)));
}

// Candidate caps must select the same represented subset in the SDK and exact native adapter. Engine
// bitboard iteration order is an implementation detail; stable public identities define this ordering.
inline void canonical_observation_order(Obs& o)
{
    const auto unit_less = [](const UnitView& a, const UnitView& b) { return a.id < b.id; };
    std::sort(o.allies.begin(), o.allies.end(), unit_less);
    std::sort(o.enemies.begin(), o.enemies.end(), unit_less);
    std::sort(o.camps.begin(), o.camps.end(), [](const CampView& a, const CampView& b) {
        if ((a.id >= 0) != (b.id >= 0)) return a.id >= 0;
        if (a.id != b.id) return a.id < b.id;
        if (a.at.y != b.at.y) return a.at.y < b.at.y;
        return a.at.x < b.at.x;
    });
}

inline UtilityAction& append_utility(Obs& o, std::uint8_t type, std::int32_t target, Tile at)
{
    o.utility_actions.emplace_back();
    auto& a = o.utility_actions.back();
    a.type = type;
    a.target = target;
    a.at = at;
    a.phi[15] = static_cast<float>(target) / 48.0f;
    a.phi[16] = static_cast<float>(at.x - o.self.at.x) / 8.0f;
    a.phi[17] = static_cast<float>(at.y - o.self.at.y) / 8.0f;
    a.phi[18] = static_cast<float>(cheb(at, o.self.at)) / 8.0f;
    return a;
}

inline void append_buy_consumable_utility(Obs& o, std::uint8_t type, int item, std::uint32_t price)
{
    auto& a = append_utility(o, type, item, o.self.at);
    a.phi[19] = static_cast<float>(o.consumables[static_cast<std::size_t>(item)]);
    a.phi[20] = static_cast<float>(price) / static_cast<float>(o.self.gold ? o.self.gold : 1);
}

inline void append_sell_utility(Obs& o, std::uint8_t type, int item, std::uint32_t price)
{
    auto& a = append_utility(o, type, item, o.self.at);
    a.phi[19] = static_cast<float>(o.item_counts[static_cast<std::size_t>(item)]) / 5.0f;
    a.phi[20] = static_cast<float>(price) / 1000.0f;
}

struct SeenEnemy
{
    int id{ -1 };
    std::uint8_t type{};
    Tile at;
    float hp{}, max_hp{}, strength{}, focus{};
    std::uint32_t gems{};
    int round{ -1000 };
};

struct Memory
{
    std::vector<SeenEnemy> enemies;     // last sighting per enemy id
    std::vector<int> respawn_round;     // per group: estimated round it is alive again (-1 unknown)
    std::vector<int> seen_round;        // per group: last round a member tile was in view
    std::vector<int> enemy_near_round;  // per group: last round an enemy was seen near it
    std::array<std::uint32_t, 6> banked_us_hist{}, banked_them_hist{};
    std::array<int, 6> hist_round{ -1, -1, -1, -1, -1, -1 };
    int hist_head{};
    int deaths{};
    bool was_alive{ true };
    // Current goal (decided by the goal head) and when it started.
    int goal_kind{ -1 }, goal_target{ -1 }, goal_since{ -1 };
    float goal_recall_p{};  // the goal head's probability of recall at its last decision (goal_since..)
    int goal_decided{ -1 };  // round of that decision
    // Own motion: last position seen and the direction of the last move (0, 0 none since spawn).
    Tile prev_at{};
    bool prev_valid{};
    int dir_x{}, dir_y{};
};

// Update memory from this turn's observation (sightings, group respawns observed, tempo history, deaths).
void observe(const Obs& obs, const Map& map, Memory& mem);

}  // namespace zero
