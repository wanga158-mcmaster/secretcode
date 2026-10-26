#pragma once
// Fresh timetable SDK contract. No retired chassis headers or implementation are used.
#include <cambc.h>
#include "service_model.h"
#include "config.h"
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace mb::production::potential { class Field; }

namespace tt {
using cambc::vec2;
inline int distance(vec2 a,vec2 b)
{ return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y)); }
inline bool current(int epoch) { return static_cast<int>(cambc::round_number())==epoch; }
inline bool budget(std::uint64_t work) { return cambc::fuel_remaining()>14000+work; }
enum class Status { Ready, NeedCompute, NeedObservation, Stale, Unreachable };
enum class Availability { Unknown, Alive, Reward, Respawning };
struct MemberSpec { vec2 pos; float max_hp{},strength{}; int range{},group{},member{}; };
struct GroupSpec
{
    std::vector<MemberSpec> members;
    vec2 center;
    int min_x{},max_x{},min_y{},max_y{},respawn{};
    std::uint32_t gold{},gems{};
};
struct World
{
    int width{},height{};
    cambc::bitboard terrain,camp_tiles,own_spawn;
    std::vector<vec2> altars,spawn_tiles;
    std::vector<GroupSpec> groups;
    std::vector<cambc::map::camp_group> handles;
    static World& get();
    static bool advance_bootstrap();
    static bool bootstrap_ready();
    const cambc::camp& camp(int group,int member) const
    { return handles[static_cast<std::size_t>(group)].camps()[static_cast<std::size_t>(member)]; }
};
struct SeenCamp
{
    int group{},member{}; vec2 pos; float hp{},strength{}; int range{},stun{};
    bool awake{},hp_exact{},stun_exact{};
};
struct SeenHero
{
    cambc::champion handle;
    vec2 pos;
    float hp{},strength{},focus{};
    int range{};
    std::uint32_t gems{};
    bool numbers_exact{};
};
struct AllyPresence { vec2 pos; };
struct Observation
{
    Status status{Status::NeedObservation};
    int epoch{},attack_range{};
    vec2 pos;
    // Physical provenance is separate from the passive-adjusted damage estimate.
    float hp{},max_hp{},raw_strength{},strength{},focus{},damage_multiplier{1};
    std::uint32_t gold{},gems{};
    bool on_spawn{},ranged{},splash{},channeling{},can_move{},can_attack{},can_cast{};
    cambc::bitboard visible,living,awake,occupied;
    // Same-epoch packed mirrors already exported by observe(); row-major LSB.
    std::vector<std::uint8_t> visible_bits,living_bits,awake_bits;
    mb::service::Body service_body;
    bool body_complete{};
    // A service packet resolves own/body/masks and visible enemies only. Omitted
    // presence families stay explicitly unknown and cannot serve global callers.
    bool camp_presence_complete{true},ally_presence_complete{true};
    std::vector<SeenCamp> camps;
    std::vector<AllyPresence> allies;
    std::vector<SeenHero> enemies;
};
// Selected lifecycle/presence resolution is cheaper than a service command frame.
// No HP, spell readiness or action authority is published by this packet.
struct GroupObservation
{
    Status status{Status::NeedObservation};
    int epoch{},group{-1},respawn_timer{-1};
    Availability availability{Availability::Unknown};
    std::size_t member_count{};
    std::array<bool,mb::service::max_members> visible{},living{};
};
struct WorkObservation
{
    Status status{Status::NeedObservation};
    int epoch{},group{-1},respawn_timer{-1};
    Availability availability{Availability::Unknown};
    mb::service::State state;
    // Fresh selected-work collection may own its three exported mask fields.
    // Their authority has exactly the same no-intervening-command lifetime as
    // this Work body; they never promote a global packet to Ready.
    bool masks_complete{};
    std::vector<std::uint8_t> visible_bits,living_bits,awake_bits;
};
Observation observe();
Observation observe_service();
struct PotentialAuthority
{
    Status status{Status::NeedObservation};
    int epoch{};
    vec2 pos;
    float hp{};
    bool can_move{},safe_exit{},safe_wait{},safe_return{},safe_service{},moved{};
};
struct CaptureAuthority
{
    Status status{Status::NeedObservation};
    int epoch{},group{-1};
    vec2 pos;
    float hp{};
    std::uint32_t gold{},gems{};
    Availability availability{Availability::Unknown};
    double current_damage{},next_damage{};
    bool can_move{},safe_input{},holding{},moved{},visible_enemy{},zero_external{};
};
// One fresh Reward/point invocation. Geometry is supplied separately; no dated
// field, timer or previous invocation is current command authority.
CaptureAuthority capture_authority(int group,vec2 expected_origin,vec2 destination,bool allow_staging=false);
enum class PotentialUse { Inspect, CertifiedEntry, BestEffortExit };
// All potential harmful camp strengths/ranges must be a valid static upper
// envelope. This limited operator ignores beneficial shields/stun/removals.
PotentialAuthority potential_authority(const mb::production::potential::Field&,
                                      std::span<const vec2> return_path,PotentialUse use=PotentialUse::Inspect,
                                      vec2 service_point={-1,-1},unsigned service_waits=0);
// Scalar is a completed retained pure word bound, never cached live authority.
PotentialAuthority transit_authority(double required_hp,vec2 expected_origin,vec2 destination);
GroupObservation observe_group(const Observation&,int group);
WorkObservation observe_work(const Observation&,int group,bool moved=false);
// Only an actual accepted stationary primitive with unchanged spatial/inventory
// support may reuse that support within the same owner input. No global freshness.
WorkObservation observe_after_stationary(const Observation& anchor,const WorkObservation& before,
    const mb::service::Primitive& accepted,bool spatial_or_inventory_changed);

enum class RouteKind { Work, Capture, Bank, Home, Exit };
struct RouteRequest
{
    RouteKind kind{RouteKind::Work};
    int group{-1};
    std::uint64_t graph_revision{};
    std::vector<vec2> roots;
    // Native masks permit large goal sets without one host import per tile.
    std::shared_ptr<cambc::bitboard> goal_mask;
    // Personally observed empty camp spawns in this frozen graph. Live step
    // legality still refreshes occupancy; this is no future absence claim.
    std::shared_ptr<cambc::bitboard> observed_empty;
};
struct RouteGeometry;
template<bool Enabled> struct RouteGeometryOwner {};
template<> struct RouteGeometryOwner<true>
{
    std::shared_ptr<RouteGeometry> geometry;
};
struct Route : RouteGeometryOwner<cfg::route_service && cfg::prefix_service && cfg::direct_service>
{
    Status status{Status::NeedCompute};
    RouteKind kind{RouteKind::Work};
    int group{-1},epoch{-1};
    std::uint64_t graph_revision{};
    vec2 origin{-1,-1};
    std::vector<std::uint16_t> distances;
    Status capture_status{Status::NeedCompute};
    int capture_group{-1};
    std::uint64_t capture_revision{};
    vec2 capture_origin{-1,-1};
    // Dated counterfactual post-clear graph/field, never live action authority.
    std::vector<std::uint16_t> capture_distances;
    std::shared_ptr<cambc::bitboard> capture_walls,capture_roots;
    std::shared_ptr<cambc::distance_field> capture_native;
    bool capture_export_pending{};
    vec2 capture_destination{-1,-1},capture_frontier{-1,-1},capture_check_position{-1,-1};
    std::uint16_t capture_check_distance{std::numeric_limits<std::uint16_t>::max()};
    bool capture_check_started{};
    Status capture_frontier_status{Status::NeedCompute};
};
// Bounded assembly of a frozen geometry request. It publishes no live action
// authority; the caller owns current origin/epoch fences around every advance.
template<bool Enabled> struct AssignmentRequestOwner {};
template<> struct AssignmentRequestOwner<true>
{
    bool forced_work_root{};
    vec2 forced_perch{-1,-1};
};
struct RequestAssembly : AssignmentRequestOwner<cfg::assignment_service>
{
    Status status{Status::NeedCompute};
    RouteKind kind{RouteKind::Work};
    int group{-1},reach{},epoch{};
    vec2 origin{-1,-1};
    std::vector<std::uint8_t> visible_bits,living_bits,goal_bits,empty_bits;
    std::vector<vec2> targets;
    std::size_t phase{},empty_group{},empty_member{},root_target{},root_cell{};
    RouteRequest request;
};
Status advance_request(RequestAssembly&);
// Conditional static graph planning and fresh one-step legal destinations.
Status plan_route(Route&,const RouteRequest&,vec2 origin,int epoch,
                  const std::vector<Availability>& known);
// Caller owns a quiet planning phase at actual_quiet_origin. This only resolves
// the future work frontier in the same retained dated capture graph.
Status resolve_capture_frontier(Route&,vec2 frontier,vec2 actual_quiet_origin,int epoch);
std::vector<vec2> legal_steps(const Observation&);
std::vector<vec2> descent_steps(const Route&,const Observation&);
// Frozen-graph proposals only; neither occupancy nor current action authority.
std::vector<vec2> proposed_steps(const Route&,const Observation&);
// Complete conditional ingress, reverse planned ingress, then actual return tail.
// Empty means this bounded witness family is Unknown, never globally infeasible.
std::vector<vec2> approach_witness(const Route&,vec2 origin,vec2 first,
                                 std::span<const vec2> actual_return,std::size_t max_nodes);
void refuse(vec2,int epoch);
bool move_absolute(Observation&,vec2);

struct Applied { bool moved{},attacked{},cast{}; };
struct ContactHistory
{
    int epoch{-1};
    bool walked{},dashed{};
    vec2 walk_from{-1,-1},walk_to{-1,-1};
};
const ContactHistory& contact_history();
bool contact_immutable_quiet(vec2);
Status apply_command(Observation&,const WorkObservation&,const mb::service::Primitive&,Applied&);
Status purchase(Observation&);
bool service_threat(Observation&,Applied&);
struct PrefixCoverage
{
    Status status{Status::NeedCompute};
    const char* trace_stage{};
    int trace_detail{};
    bool witness_blocked{};
    float reserve{},known_next_damage{},current_damage{};
    bool safe{};
    // A conditional current-occupancy route, beginning at the current origin.
    std::vector<vec2> exit_path;
};
PrefixCoverage cover_prefix(const Observation&,const WorkObservation*,
                            const mb::service::Primitive*,vec2 prospective_origin={-1,-1});
bool immutable_camp_quiet(vec2);
PrefixCoverage cover_witness(const Observation&,const WorkObservation*,const mb::service::Primitive*,
                             std::span<const vec2> return_path,vec2 prospective_origin={-1,-1});
void play_service_turn();
void play_service_draft();
} // namespace tt
