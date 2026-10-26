#include "runtime.h"
#include "dash_service.h"
#include "production_potential.h"

namespace tt {
namespace {
struct DashBackend {
    const mb::production::potential::Field& field;
    cambc::champion& actor;
    cambc::bitboard camps,champions,enemies,teleports;
    int epoch() const { return static_cast<int>(cambc::round_number()); }
    dash_service::Point position() const { const auto p=actor.position(); return {p.x,p.y}; }
    bool budget() const { return tt::budget(26000); }
    dash_service::Flags flags() const {
        const auto state=actor.state();
        return {state.contains(cambc::champion_state::alive),
            state.contains(cambc::champion_state::recalling) || state.contains(cambc::champion_state::submitting_gems) ||
                state.contains(cambc::champion_state::teleporting),
            state.contains(cambc::champion_state::can_move),state.contains(cambc::champion_state::can_attack),
            state.contains(cambc::champion_state::can_cast)};
    }
    bool quiet(dash_service::Point p) const {
        const auto value=field.at({p.x,p.y});
        return value.status==mb::production::potential::Status::Ready && value.upper==0;
    }
    dash_service::Scope prepare(bool protect_work) {
        cambc::vision::camp_tiles_into(camps);
        cambc::vision::champion_tiles_into(champions);
        cambc::vision::champion_tiles_into(cambc::team::enemy,enemies);
        cambc::vision::teleport_tiles_into(teleports);
        if (!enemies.is_empty()) return dash_service::Scope::HasTarget;
        if (!protect_work) return dash_service::Scope::Clear;
        // A living basic-reach member still names stationary work even after a
        // basic was spent; leave the cast available to the selected service.
        const int reach=actor.attack_range();
        if (reach<0 || reach>4) return dash_service::Scope::Unsupported;
        const auto from=position();
        for (int dy=-reach;dy<=reach;++dy) {
            if (!budget()) return dash_service::Scope::Unsupported;
            for (int dx=-reach;dx<=reach;++dx) {
                const cambc::vec2 p{from.x+dx,from.y+dy};
                if (cambc::map::is_in_bounds(p) && camps.is_set(p) && cambc::vision::camp_at(p).is_alive())
                    return dash_service::Scope::HasTarget;
            }
        }
        return dash_service::Scope::Clear;
    }
    dash_service::Tile tile(dash_service::Point point) const {
        const cambc::vec2 p{point.x,point.y};
        const bool blocked=cambc::map::walls().is_set(p) || cambc::map::altars().is_set(p) ||
            cambc::map::spawn_region(cambc::team::enemy).is_set(p) || camps.is_set(p) || champions.is_set(p) || teleports.is_set(p);
        return {blocked,blocked || cambc::map::holes().is_set(p)};
    }
    dash_service::Dash dash(int slot) const {
        const auto ability=actor.abilities()[static_cast<std::size_t>(slot)];
        if (!ability.is_dash()) return {};
        const auto props=ability.get<cambc::ability_dash>();
        return {true,ability.can_target_ground(),ability.is_ready(),props.exclusive_with_movement,
                props.range,props.modifier,props.duration};
    }
    bool use(int slot,dash_service::Point p) { return actor.abilities()[static_cast<std::size_t>(slot)].use({p.x,p.y}); }
};
}

dash_service::Outcome try_dash_progress(const Route& route,const mb::production::potential::Field& field,bool walked_this_turn) {
    using namespace dash_service;
    Outcome unavailable;
    unavailable.geometry_epoch=route.epoch;
    if (field.status()==mb::production::potential::Status::NeedCompute) { unavailable.status=dash_service::Status::NeedCompute; return unavailable; }
    if (field.status()!=mb::production::potential::Status::Ready ||
        field.width()!=cambc::map::width() || field.height()!=cambc::map::height()) { unavailable.status=dash_service::Status::Unsupported; return unavailable; }
    Purpose purpose=Purpose::Unsupported;
    switch (route.kind) {
    case RouteKind::Work: purpose=Purpose::Work; break;
    case RouteKind::Capture: purpose=Purpose::Capture; break;
    case RouteKind::Bank: purpose=Purpose::Bank; break;
    default: break;
    }
    const RouteView view{route.status==tt::Status::Ready || route.status==tt::Status::Unreachable,
        purpose,route.group,route.epoch,field.width(),field.height(),route.distances};
    DashBackend backend{field,cambc::self(),{},{},{},{}};
    return try_progress(view,walked_this_turn,backend);
}
} // namespace tt
