#pragma once
#include "coalition.h"
#include "defaults.h"
#include <algorithm>
#include <array>
#include <memory>
#include <span>

namespace p8::native {
struct SupportedGeometry {
    std::uint64_t hash=0;
    std::array<int,6> participants{-1,-1,-1,-1,-1,-1};
    int count=0;
    bool valid=false; // Public geometry only; never an episode or safety result.
};
inline bool context_roster_equal(const common::DraftRoster& a,const common::DraftRoster& b) {
    return a.picks==b.picks&&a.actors==b.actors&&a.champions==b.champions;
}
inline bool context_route_equal(const Route& a,const Route& b) {
    if(a.count!=b.count||a.count<0||a.count>max_route_tiles)return false;
    if(!a.count)return true;
    if(a.verified!=b.verified||a.blocker_assumptions!=b.blocker_assumptions)return false;
    for(int i=0;i<a.count;++i)if(!(a.tiles[i]==b.tiles[i]))return false;
    return true;
}

// One first-intent cache. Only immutable map routes and actual public draft IDs
// survive a turn boundary. No forecast durations/permissions, actor observations,
// build/HP/Focus, threat, certificates or evidence are retained here.
class ContextGeometry {
    std::unique_ptr<const Plan> fixed_;
    std::array<common::DraftRoster,2> rosters_{};
    int next_=0;

    bool source_matches(const Routes& routes,int s,int b,int owner,bool spawn_visit) const {
        return frozen()&&s==site&&b==bank&&owner==self&&spawn_visit==via_spawn&&
            s>=0&&s<int(routes.sites.size())&&b>=0&&b<int(routes.banks.size())&&
            group_id(routes.sites[s].group)==source_group&&
            routes.sites[s].approach.destination==entry&&
            routes.banks[b].altar.y*cambc::map::width()+routes.banks[b].altar.x==endpoint;
    }
public:
    int site=-1,bank=-1,self=-1,source_group=-1,endpoint=-1,shape_count=0;
    p6::Point entry{};
    bool via_spawn=false;
    std::uint64_t fixed_hash=0,return_hash=0;
    std::array<SupportedGeometry,2> supported{};

    bool frozen() const {return bool(fixed_);}
    bool complete() const {return frozen()&&next_==shape_count+1;}
    bool ready() const {return complete()&&fixed_hash&&return_hash;}

    // A different later feasible pair cannot evict the first frozen intent.
    bool matches(const Routes& routes,int s,int b,int owner,const Plan& selected,
                 std::span<const common::DraftRoster> rosters,bool spawn_visit) const {
        if(!source_matches(routes,s,b,owner,spawn_visit)||rosters.size()!=std::size_t(shape_count)||
           selected.organization!=Organization::U||selected.group!=source_group||
           selected.collector!=self||selected.altar!=endpoint||selected.crew_count!=1||selected.crew[0]!=self)return false;
        for(int i=0;i<shape_count;++i)if(!context_roster_equal(rosters_[i],rosters[i]))return false;
        return context_route_equal(fixed_->capture,selected.capture)&&context_route_equal(fixed_->bank,selected.bank)&&
            context_route_equal(fixed_->renewal,selected.renewal)&&context_route_equal(fixed_->release,selected.release);
    }
    bool freeze(const Routes& routes,int s,int b,int owner,const Plan& selected,
                std::span<const common::DraftRoster> rosters,bool spawn_visit) {
        if(frozen())return matches(routes,s,b,owner,selected,rosters,spawn_visit);
        if(!routes.initialized()||s<0||s>=int(routes.sites.size())||b<0||b>=int(routes.banks.size())||
           owner<0||rosters.size()>supported.size()||selected.organization!=Organization::U||
           selected.collector!=owner||selected.crew_count!=1||selected.crew[0]!=owner)return false;
        const auto& target=routes.sites[s];const auto& altar=routes.banks[b];
        if(!target.approach.ready||!target.capture.ready||!altar.approach.ready||
           (spawn_visit&&!routes.spawn.ready)||selected.group!=group_id(target.group)||
           selected.altar!=altar.altar.y*cambc::map::width()+altar.altar.x||
           !route_valid(selected.capture)||!route_valid(selected.bank)||!route_valid(selected.renewal)||
           selected.release.count<0||selected.release.count>max_route_tiles||
           (selected.release.count&&!route_valid(selected.release)))return false;
        const auto end=[](const Route& route){return route.tiles[route.count-1];};
        if(selected.capture.tiles[0]!=target.approach.destination||end(selected.capture)!=target.capture.destination||
           selected.bank.tiles[0]!=end(selected.capture)||end(selected.bank)!=altar.approach.destination||
           selected.renewal.tiles[0]!=end(selected.bank)||end(selected.renewal)!=target.approach.destination)return false;
        for(std::size_t i=0;i<rosters.size();++i) {
            if(!rosters[i].valid()||rosters[i].actors[0]!=owner)return false;
            for(std::size_t j=0;j<i;++j)if(rosters[j].actors[2]==rosters[i].actors[2])return false;
        }
        auto fixed=make_default<Plan>();
        fixed->group=selected.group;fixed->collector=owner;fixed->altar=selected.altar;
        fixed->crew[0]=owner;fixed->crew_count=1;fixed->pads[0]=target.approach.destination;
        fixed->ingress.count=1;fixed->ingress.verified=true;fixed->ingress.tiles[0]=target.approach.destination;
        if(!copy_route_into(fixed->capture,selected.capture)||!copy_route_into(fixed->bank,selected.bank)||
           !copy_route_into(fixed->renewal,selected.renewal)||
           (selected.release.count&&!copy_route_into(fixed->release,selected.release)))return false;
        site=s;bank=b;self=owner;source_group=selected.group;endpoint=selected.altar;
        entry=target.approach.destination;via_spawn=spawn_visit;shape_count=int(rosters.size());
        for(int i=0;i<shape_count;++i)rosters_[i]=rosters[i];
        fixed_=std::move(fixed);return true;
    }
    // Exactly one fixed-hash OR one H-formation/hash quantum. A round/fuel
    // change does not invalidate immutable geometry, but never licenses a
    // declaration: the caller must reobserve and recheck its current premises.
    bool advance(const Routes& routes,int round) {
        if(round<0||complete()||!source_matches(routes,site,bank,self,via_spawn))return false;
        if(!next_) {
            fixed_hash=plan_route_hash(*fixed_);return_hash=plan_return_hash(*fixed_);
            ++next_;return true;
        }
        const int index=next_-1;
        p6::Frame geometry;geometry.clock={round,self,0};geometry.own.actor.id=self;
        geometry.own.actor.position=entry;
        const auto plan=formation_plan(routes,site,bank,geometry,rosters_[index],Organization::H,via_spawn);
        auto& shape=supported[index];
        if(fixed_hash&&return_hash&&plan_valid(plan,self)&&plan_return_hash(plan)==return_hash) {
            shape.hash=plan_route_hash(plan);shape.participants=plan.crew;shape.count=plan.crew_count;
            std::sort(shape.participants.begin(),shape.participants.begin()+shape.count);
            shape.valid=shape.hash!=0;
        }
        ++next_;return true;
    }
};
} // namespace p8::native
