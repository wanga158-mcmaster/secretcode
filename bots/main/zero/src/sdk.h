#pragma once
// SDK adapter (bot only): fills the core's Map and Obs from the engine, provides engine-backed path distances,
// executes chosen candidates, and reads/writes the shared-memory claim protocol.
#include "core/cand.h"
#include "core/action.h"
#include "core/map.h"
#include "core/obs.h"

#if defined(ZERO_STUDENT_PROBE)
namespace zero { struct GroundUtilityScene; }
#endif

namespace zero::sdk {

void load_terrain(Map& map);                              // first turn: tiles, spawns, altars
void load_groups(Map& map);                               // then camp groups (Map::finish after)
void observe(const Map& map, Obs& obs);                   // every turn
// Last opt-in exact observation, profiled without log output. Complete=false means an interrupted read;
// fuel_reset marks a counter reset/suspension, for which elapsed fuel is not a valid single-turn measure.
struct ObserverFuel
{
    std::uint64_t revision{}, self{}, items{}, commands{}, champions{}, camps{}, groups{}, locks_moves{}, total{};
#if defined(ZERO_STUDENT_PROBE)
    // Subintervals of locks_moves; probe fuel reads/accounting are included in the enclosing totals.
    std::uint64_t locks_read{}, canonical_order{}, move_arguments{}, ground_utilities{};
#endif
    int round{};
    bool complete{}, fuel_reset{};
};
const ObserverFuel& last_observer_fuel();
void refresh_self(const Map& map, Obs& obs);              // after an action: own state and legal moves only
// Opt-in primitive controller. False preserves the historical standing-recall adapter.
void set_explicit_channel_mode(bool enabled);
#if defined(ZERO_STUDENT_PROBE)
// Borrowed only while the caller keeps the scene alive; clear before yield or leaving that scope.
void set_probe_ground_utility_scene(GroundUtilityScene* scene);
#endif
// Re-read live targets, visibility, self, allowances and inventory after every attempted primitive.
void refresh_after_primitive(const Map& map, Obs& obs);
void refresh_after_primitive(const Map& map, Obs& obs, ActionType type, const Cand& argument);
enum class PrimitiveResult : std::uint8_t { rejected, accepted, unexpected_error };
// ActionType is declared in core/action.h. END has no SDK effect; REPLAN belongs to the controller.
PrimitiveResult primitive_result(ActionType type, const Obs& obs, const Cand& argument);
bool primitive(ActionType type, const Obs& obs, const Cand& argument);
void publish_claim(int kind, int target);                 // our goal into shared memory
void read_claims(Obs& obs);                               // allies' claims (shared memory), at goal decisions
std::uint64_t fuel();

class EnginePaths final : public Paths
{
public:
    explicit EnginePaths(const Map& map, int team) : map_(map), team_(team) {}
    int to_group(int group, Tile from) override;
    int to_altar(int altar, Tile from) override;
    int to_spawn(Tile from) override;
    int fields_built() const { return built_; }  // distinct fields with some exact distances, possibly partial
    // Prepare queued fields in bounded slices, compute an affordable depth, and retry partials at larger depths.
    // Reached entries are exact; unreached partial entries retain the existing center-distance fallback.
    void warm(std::uint64_t reserve);
    void begin_turn() { ++epoch_; }  // the observation changed (a new turn): forget memoised distances

private:
    int memo(int key, Tile from, Tile fallback);
    struct Memo
    {
        unsigned epoch{};
        int key{};
        Tile at{};
        int d{};
    };
    static constexpr int kMemo = 64;
    const Map& map_;
    int team_;
    int built_{};
    unsigned epoch_{ 1 };
    Memo memo_[kMemo]{};
};

// Actions. Each returns true when the engine accepted it (a hidden obstruction returns false without cost).
bool act_step(const Obs& obs, const Cand& c);
bool act_attack(const Obs& obs, const Cand& c);
bool act_cast(const Obs& obs, int slot, const Cand& c);
bool act_buy(const Cand& c);

// The tail head's choice at the end of a turn: start a standing recall (cancelled free by the next turn's first other
// action, kept by the step head's recall with one turn end done), or not (a standing recall not kept is cancelled).
bool home_now();
void settle_recall();
void end_turn_recall(bool home, bool start);

}  // namespace zero::sdk
