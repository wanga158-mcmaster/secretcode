#pragma once
#include "observe.h"
#include "runtime_opportunities.h"
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace p6::runtime_routes {
// Immutable public walking terrain only. Live units/camps never enter this
// component, and a partial traversal is never a disconnection certificate.
class WalkingComponent {
    int width_=0,height_=0;
    std::vector<std::uint8_t> cells_;
    std::vector<std::uint8_t> packed_;
    std::vector<int> queue_;
    std::size_t next_=0,decoded_=0;
    int root_=-1;
    bool started_=false,complete_=false;
    int index(Point p) const {
        return p.x>=0 && p.y>=0 && p.x<width_ && p.y<height_?p.y*width_+p.x:-1;
    }
public:
    bool started() const { return started_; }
    bool complete() const { return complete_; }
    bool begin_packed(int width,int height,Point origin,std::vector<std::uint8_t> public_terrain) {
        if(started_ || width<=0 || height<=0)return false;
        const auto count=static_cast<std::size_t>(width)*static_cast<std::size_t>(height);
        if(public_terrain.size()!=(count+7)/8)return false;
        width_=width;height_=height;root_=index(origin);
        if(root_<0 || ((public_terrain[root_/8]>>(root_%8))&1u))return false;
        cells_.resize(count);packed_=std::move(public_terrain);
        started_=true;return true;
    }
    bool begin(int width,int height,Point origin,std::vector<std::uint8_t> public_terrain) {
        if(started_ || width<=0 || height<=0 ||
           public_terrain.size()!=static_cast<std::size_t>(width)*static_cast<std::size_t>(height))return false;
        width_=width;height_=height;
        const int root=index(origin);
        if(root<0 || public_terrain[static_cast<std::size_t>(root)]!=0)return false;
        for(auto& cell:public_terrain)cell=cell?1:0;
        cells_=std::move(public_terrain);
        queue_.reserve(cells_.size());queue_.push_back(root);cells_[root]=2;
        started_=true;return true;
    }
    // Caller chooses a fixed work quantum and owns its turn fuel admission.
    void advance(std::size_t maximum_nodes) {
        if(!started_ || complete_)return;
        // Packed export is immutable, but decoding a whole100x100 map in one
        // owner turn exhausts. Charge this local work in the same fixed units
        // as graph traversal, and publish no connectivity until both finish.
        if(!packed_.empty()) {
            for(std::size_t processed=0;processed<maximum_nodes && decoded_<cells_.size();++processed,++decoded_)
                cells_[decoded_]=(packed_[decoded_/8]>>(decoded_%8))&1u;
            if(decoded_==cells_.size()) {
                packed_.clear();queue_.reserve(cells_.size());queue_.push_back(root_);cells_[root_]=2;
            }
            return;
        }
        for(std::size_t processed=0;processed<maximum_nodes && next_<queue_.size();++processed) {
            const int current=queue_[next_++];
            const Point center{current%width_,current/width_};
            for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
                if(x==0 && y==0)continue;
                const int at=index({center.x+x,center.y+y});
                if(at>=0 && cells_[at]==0) {cells_[at]=2;queue_.push_back(at);}
            }
        }
        complete_=next_==queue_.size();
    }
    bool refuses_walking_inspection(Point actor,Point approach,bool walking_only=true) const {
        if(!complete_ || !walking_only)return false;
        const int own=index(actor),target=index(approach);
        // An actor which crossed components by another legal adapter has no
        // proof from this rooted component. Do not erase its new possibilities.
        return own>=0 && target>=0 && cells_[own]==2 && cells_[target]!=2;
    }
};
inline void screen_walking_inspection(runtime_opportunities::Candidate& candidate,
    const WalkingComponent& component,Point actor,Point approach,bool walking_only=true) {
    if(candidate.kind==runtime_opportunities::Kind::inspection && !candidate.protected_incumbent &&
       component.refuses_walking_inspection(actor,approach,walking_only))candidate.eligible=false;
}
} // namespace p6::runtime_routes
