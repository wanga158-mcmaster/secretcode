#pragma once
#include "../response.h"
#include "../../../common/src/routes.h"
#include "../../../common/src/sdk.h"
namespace p8::finite_counterprotocol::native {
// One immutable public field, not a rival action/intent model. The rival's
// own spawn is legal; our spawn is not. Self-navigation terrain is different.
class RivalDistance {
    cambc::distance_field field_;
    cambc::bitboard terrain_;
    Site site_{};bool ready_=false;
public:
    bool ready() const{return ready_;}
    std::uint64_t map_identity() const {
        auto hash=p8::hash_word(1469598103934665603ULL,std::uint64_t(cambc::map::width()));
        hash=p8::hash_word(hash,std::uint64_t(cambc::map::height()));
        const auto terrain=cambc::map::walls()|cambc::map::holes()|cambc::map::altars()|cambc::map::spawn_region(cambc::team::ally);
        for(const auto byte:terrain.export_raw_buffer())hash=p8::hash_word(hash,byte);
        for(const auto byte:cambc::map::bushes().export_raw_buffer())hash=p8::hash_word(hash,byte);
        return hash;
    }
    bool prepare(const Site& named) {
        if(ready_)return same_site(named,site_);
        if(!site_valid(named)||named.width!=cambc::map::width()||named.height!=cambc::map::height()||
            cambc::fuel_remaining()<60000)return false;
        terrain_=cambc::map::walls()|cambc::map::holes()|cambc::map::altars()|cambc::map::spawn_region(cambc::team::ally);
        const auto target=sdk::native(named.target);if(terrain_.is_set(target))return false;
        cambc::bitboard root,visited;root.set(target);const int round=cambc::round_number();
        field_.compute(terrain_,root,visited,256);
        if(int(cambc::round_number())!=round||cambc::fuel_remaining()<25000||named.map_identity.value!=map_identity())return false;
        site_=named;ready_=true;return true;
    }
    DistanceObservation read(const p6::Frame& frame,int rival) const {
        DistanceObservation out;if(!ready_)return out;const auto* actor=frame.visible(rival);
        if(!actor||actor->team!=p6::Team::enemy||!actor->alive||actor->health<=0)return out;
        const auto from=sdk::native(actor->position);const auto target=sdk::native(site_.target);
        const auto visible=cambc::vision::visible_tiles(),bushes=cambc::map::bushes();
        if(!visible.is_set(from)||!visible.is_set(target)||bushes.is_set(from)||bushes.is_set(target))return out;
        auto occupied=cambc::vision::champion_tiles()|cambc::vision::camp_tiles()|cambc::vision::teleport_tiles();occupied.set(from,false);
        const int distance=field_.get(from);if(distance==cambc::distance_field::unreachable||distance>32)return out;
        // Require a fully inspected clear descending shortest path right now.
        // One unsupported/bush/occupied suffix is UNKNOWN, never zero or STAY.
        auto at=from;
        for(int remaining=distance;remaining>=0;--remaining) {
            if(!visible.is_set(at)||bushes.is_set(at)||occupied.is_set(at)||terrain_.is_set(at))return out;
            if(remaining==0){if(at!=target)return out;break;}
            bool found=false;
            for(int y=-1;y<=1&&!found;++y)for(int x=-1;x<=1&&!found;++x) {
                if(!x&&!y)continue;const cambc::vec2 next{at.x+x,at.y+y};
                if(next.x<0||next.y<0||next.x>=site_.width||next.y>=site_.height||!visible.is_set(next)||bushes.is_set(next)||
                    occupied.is_set(next)||terrain_.is_set(next)||field_.get(next)!=remaining-1)continue;
                at=next;found=true;
            }
            if(!found)return out;
        }
        if(int(cambc::round_number())!=frame.clock.round||cambc::fuel_remaining()<18000)return out;
        StaticDistance reading;reading.opportunity=site_.opportunity;reading.map_identity=site_.map_identity.value;
        reading.from=actor->position;reading.target=site_.target;reading.distance={distance,p6::Source::public_map};
        reading.shortest_path_known=reading.unobstructed=true;return {reading,frame.clock,rival,p6::Knowledge::exact};
    }
};
} // namespace p8::finite_counterprotocol::native
