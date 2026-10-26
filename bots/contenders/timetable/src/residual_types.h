#pragma once
// Owning observations and immutable catalogue facts for one body's next service.
// Unknown facts stay unknown. A forecast never grants a later SDK command.
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace residual {
struct Point {int x{},y{};friend bool operator==(const Point&,const Point&)=default;};
inline int distance(Point a,Point b){const int x=a.x>b.x?a.x-b.x:b.x-a.x,y=a.y>b.y?a.y-b.y:b.y-a.y;return x>y?x:y;}
enum class Role : unsigned {Varg,Marcella,Ryth,Vesper,Heart,Other};
enum class Life : unsigned {Unknown,Living,Reward,Respawning};
enum class SpellKind : unsigned {None,Reset,Targeted,Area,Dash,Shield,Heal};
struct Spell {
    SpellKind kind{};unsigned slot{},cooldown{},period{};int range{},radius{},duration{};
    float damage{},shield{},heal{};int stun{};
    bool exclusive{},enhanced{},ready{},target_camps{},target_self{},target_ground{},target_enemies{};
};
struct Body {
    // strength is current effective single-target CAMP basic damage; raw_strength
    // is the separately read engine stat. Spell.damage is effective CAMP damage.
    Point pos;float hp{},max_hp{},strength{},raw_strength{},focus{},shield{},incoming_scale{1},splash_scale{};
    int range{1},splash_radius{},next_basic_stun{},stun{};
    bool can_move{},can_attack{},can_cast{},walked{},ranged{},splash{},immune{},enhanced{},shield_next_batch{},stunned{};
};
struct CampSpec {std::uint32_t handle{};int group{-1};Point pos;float max_hp{},strength{};int range{};};
struct GroupSpec {std::uint32_t handle{};std::vector<int> members;unsigned gems{},gold{},respawn{};};
struct Grid {int width{},height{};std::vector<std::uint8_t> blocked;std::vector<Point> own_spawn,enemy_spawn,altars;};
struct World {std::shared_ptr<const Grid> grid;std::vector<CampSpec> camps;std::vector<GroupSpec> groups;};
struct CampReading {int index{-1};Point pos;float hp{},shield{};int stun{};bool alive{},awake{},awake_known{};};
struct GroupReading {int index{-1};Life life{Life::Unknown};int respawn{-1};unsigned captured{},required{};bool capture_progress_known{};};
// Foe spell damage is caster output against champions, without invented hidden
// inventory or our CAMP-only outgoing modifiers.
struct FoeReading {std::uint32_t handle{};Point pos;float hp{},strength{},focus{},shield{};int range{};std::array<Spell,2> spells;};
struct Frame {
    int epoch{-1};Role role{Role::Other};Body body;std::array<Spell,2> spells;
    unsigned gold{},gems{};bool ready{},alive{},own_spawn{},submitting{},recalling{},teleporting{};int channel_remaining{};
    std::vector<CampReading> camps;std::vector<GroupReading> groups;std::vector<FoeReading> foes;
    std::vector<Point> allies;
};
enum class ReadStatus : unsigned {Ready,NeedCompute,Stale,Unsupported};
// A single-input compatible word; execution refreshes each primitive, can stop
// on refusal/change, and never treats simulated HP or a move as an SDK receipt.
enum class ActionKind : unsigned {Basic,CastTargeted,CastArea,Reset,Shield,Heal,Move,Dash};
struct Action {ActionKind kind{};unsigned slot{};int camp{-1};Point destination;};
struct Word {
    std::array<Action,4> actions;unsigned count{};float useful_damage{},incoming_upper{},own_healing{},shield_after{};
    float own_damage_upper{};
    bool supported{};
};
}
