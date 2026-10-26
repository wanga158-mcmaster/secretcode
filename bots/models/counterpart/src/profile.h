#pragma once
#include "counterpart.h"
#include "../../common/model-runtime.h"

namespace plan4::counterpart {
// An explicit input seam, not an inference from an actor's champion or name.
// No production provider currently certifies these claims. In particular the
// existing multi-actor fixture does not imply that its other actors are inert,
// that our allies stay still, or that its private cursor is publicly known.
struct PublicProfile {
    runtime::ObservationClock clock{};
    std::array<std::uint8_t,32> executed_module{};
    std::uint64_t rule_identity=0, response_identity=0, own_executor_identity=0;
    bool exact_module=false, frozen=false, inspectable=false, matches_rule=false;
    bool matches_response=false, matches_own_first_step=false;
    bool complete_choice_inputs=false, complete_other_actor_laws=false;
    bool ordinary_response_exact=false, travel_window_no_other_effects=false;
    bool order_certified=false, opposing_teams=false, vision_modifiers_known=false;
    bool rival_before_owner=false, rival_already_completed=false;
    int rival_id=-1, turns=1, public_vision_radius=-1;
    Point corridor_min{},corridor_max{};
    // Must describe the RIVAL's public movement restrictions and visibility.
    // The owner's topology cannot certify an enemy spawn is walkable to it.
    Geometry rival_geometry{};
    bool rival_oriented_geometry=false;
    std::array<int,4> stationary_choice_ids{};
    int stationary_choice_count=0;
    std::array<int,4> inert_other_enemy_ids{};
    int inert_other_enemy_count=0;
    std::array<Camp,max_camps> camp_priority{};
    int camp_count=0,camp_cursor=0;
    bool camp_cursor_publicly_derived=false;
    bool (*quote_response)(const runtime::VisibleActor&,const ResponseInput&,Response&,const void*)=nullptr;
    const void* quote_context=nullptr;
};
enum class BindingGap { none, source, profile, roster, ordering, geometry, route, prefix, response, continuation, budget };

// A boundary is travel only: no farm/attack/channel/capture/heal or inventory
// effect is hidden in this prefix. Its positions are read from the common
// sealed shortest route, not manufactured from Chebyshev distance.
struct Boundary {
    std::array<OwnFrame,max_turns> after{};
    int turns=0;
    Point target{};
};
} // namespace plan4::counterpart
