#include "runtime.h"
#include "recon_execution.h"
#include "production_potential.h"
#include <algorithm>

namespace tt {
namespace {
recon_execution::Session& recon_session() {
    static recon_execution::Session value;
    return value;
}
struct ReconBackend {
    const World& world;
    const mb::production::potential::Field& field;
    cambc::champion& actor;
    int vision_radius{};
    cambc::bitboard actual_view,hypothetical_view;
    int epoch() const {return static_cast<int>(cambc::round_number());}
    recon_execution::Point position() const {const auto p=actor.position();return {p.x,p.y};}
    bool query_budget() const {return tt::budget(18000);}
    // Initial scheduling bound, to be qualified on actual native inputs. It is
    // not a formal bound on the host hypothetical-vision import.
    bool compute_budget() const {return tt::budget(65000);}
    bool step_budget() const {return tt::budget(28000);}
    recon_execution::Flags flags() const {
        const auto s=actor.state();
        return {s.contains(cambc::champion_state::alive),
            s.contains(cambc::champion_state::recalling) || s.contains(cambc::champion_state::submitting_gems) ||
                s.contains(cambc::champion_state::teleporting),
            s.contains(cambc::champion_state::can_move)};
    }
    bool quiet(recon_execution::Point p) const {
        const auto v=field.at({p.x,p.y});
        return v.status==mb::production::potential::Status::Ready && v.upper==0;
    }
    production_recon::Cell cell(recon_execution::Point p) const {
        const auto v=field.at({p.x,p.y});
        if(v.status!=mb::production::potential::Status::Ready)return production_recon::Cell::Unknown;
        // All original spawn tiles remain conservative graph blockers. Losing
        // visibility cannot reopen a dated corpse as either a free tile or work.
        return v.upper==0 && !world.terrain.is_set({p.x,p.y}) && !world.camp_tiles.is_set({p.x,p.y})?
            production_recon::Cell::QuietOpen:production_recon::Cell::Closed;
    }
    std::uint32_t mask(const cambc::bitboard& view,const recon_execution::Request& r) const {
        std::uint32_t value{};
        for(std::size_t i=0;i<r.member_count;++i)
            if((r.missing_members&(std::uint32_t{1}<<i)) && view.is_set({r.members[i].x,r.members[i].y}))
                value|=std::uint32_t{1}<<i;
        return value;
    }
    std::uint32_t actual_visible(const recon_execution::Request& r) {
        cambc::vision::visible_tiles_into(actual_view);
        return mask(actual_view,r);
    }
    bool candidate(recon_execution::Point p,const recon_execution::Request& r) const {
        for(std::size_t i=0;i<r.member_count;++i)
            if((r.missing_members&(std::uint32_t{1}<<i)) &&
               std::max(std::abs(p.x-r.members[i].x),std::abs(p.y-r.members[i].y))<=vision_radius)return true;
        return false;
    }
    std::uint32_t hypothetical_visible(recon_execution::Point p,const recon_execution::Request& r) {
        // Queried champion-design visibility handles real walls/bush clusters.
        // This is a dated geometry nomination and does not include a future ward
        // or assert that any member is alive when the actor actually arrives.
        actor.visible_tiles_from_into({p.x,p.y},hypothetical_view);
        return mask(hypothetical_view,r);
    }
    bool enemies_present() const {
        const auto enemies=cambc::vision::champion_tiles(cambc::team::enemy);
        return !enemies.is_empty();
    }
    bool legal_step(recon_execution::Point from,recon_execution::Point to) const {
        // Minimal stamped frame; no global observation fields are promoted.
        Observation stamp;stamp.status=tt::Status::NeedObservation;
        stamp.epoch=epoch();stamp.pos={from.x,from.y};
        const auto legal=legal_steps(stamp);
        return std::find(legal.begin(),legal.end(),vec2{to.x,to.y})!=legal.end();
    }
    bool move(recon_execution::Point p) {return actor.move_absolute({p.x,p.y});}
};
}
recon_execution::Outcome step_named_recon(const recon_execution::Request& request,
    const mb::production::potential::Field& field) {
    recon_execution::Outcome unsupported;unsupported.key=request.key;
    // Caller completes World bootstrap before ANY geometry consumer, as for
    // the existing route/potential adapters. No partial World is authority.
    const auto& world=World::get();
    if(field.status()==mb::production::potential::Status::NeedCompute) {
        unsupported.status=recon_execution::Status::NeedCompute;
        unsupported.handled=true;return unsupported;
    }
    if(field.status()!=mb::production::potential::Status::Ready ||
       field.width()!=world.width || field.height()!=world.height ||
       !recon_execution::valid_request(request,world.width,world.height) ||
       static_cast<std::size_t>(request.key.group)>=world.groups.size())return unsupported;
    const auto& group=world.groups[static_cast<std::size_t>(request.key.group)];
    if(request.member_count!=group.members.size())return unsupported;
    for(std::size_t i=0;i<request.member_count;++i)
        if(request.members[i]!=recon_execution::Point{group.members[i].pos.x,group.members[i].pos.y})return unsupported;
    if(!budget(18000)) {
        unsupported.status=recon_execution::Status::NeedCompute;
        unsupported.handled=true;return unsupported;
    }
    auto& actor=cambc::self();
    const auto vision_radius=actor.base_stats().vision_radius;
    if(vision_radius<0)return unsupported;
    ReconBackend backend{world,field,actor,vision_radius,{},{}};
    // A bounded represented component; exhaustion never means global physical
    // infeasibility or completeness of dash/ward/investment families.
    return recon_session().step(request,world.width,world.height,65536,backend);
}
void cancel_named_recon() {recon_session().clear();}
} // namespace tt
