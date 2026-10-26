#pragma once
#include "organization.h"
#include <cstddef>
#include <cstdint>
#include <span>

namespace p8::native {
// Callers own an initialized Route. Inactive tiles remain initialized storage,
// but neither failure nor replacement clears/copies that unused payload.
inline void reset_route_header(Route& route) {
    route.count=0;route.blocker_assumptions=0;route.verified=false;
}
// Only for initialized immutable payloads already checked by Routes::retain.
// Success means the bounded payload was copied, not that geometry is valid;
// the candidate must still run its current route_valid checks before forecast.
inline bool copy_route_payload_into(Route& to,const Route& from) {
    const int count=from.count;
    if(!from.verified||count<=0||count>max_route_tiles) {
        reset_route_header(to);return false;
    }
    if(&to!=&from) {
        for(int i=0;i<count;++i)to.tiles[i]=from.tiles[i];
        to.count=count;to.blocker_assumptions=from.blocker_assumptions;to.verified=from.verified;
    }
    return true;
}
inline bool copy_route_into(Route& to,const Route& from) {
    const int count=from.count;
    const bool verified=from.verified;
    const auto blockers=from.blocker_assumptions;
    reset_route_header(to);
    if(!verified||count<=0||count>max_route_tiles)return false;
    for(int i=0;i<count;++i) {
        const auto point=from.tiles[i];
        if(point.x<0||point.y<0)return false;
        if(i) {
            const auto previous=to.tiles[i-1];
            const int dx=point.x-previous.x,dy=point.y-previous.y;
            if(dx < -1||dx > 1||dy < -1||dy > 1||(!dx&&!dy))return false;
        }
        to.tiles[i]=point;
    }
    to.count=count;to.blocker_assumptions=blockers;to.verified=true;return true;
}
inline bool join_into(const Route& a,const Route& b,Route& to) {
    if(!route_valid(a)||!route_valid(b)||!(a.tiles[a.count-1]==b.tiles[0])||
        a.count+b.count-1>max_route_tiles) {
        reset_route_header(to);return false;
    }
    const int first=a.count,second=b.count;
    const auto blockers=a.blocker_assumptions;
    if(&to==&b && &to!=&a) {
        // Preserve b's suffix before replacing its prefix with a.
        for(int i=second-1;i>0;--i)to.tiles[first+i-1]=b.tiles[i];
        for(int i=0;i<first;++i)to.tiles[i]=a.tiles[i];
    } else {
        if(&to!=&a)for(int i=0;i<first;++i)to.tiles[i]=a.tiles[i];
        for(int i=1;i<second;++i)to.tiles[first+i-1]=b.tiles[i];
    }
    // Match the old join: the first leg owns blocker assumptions.
    to.count=first+second-1;to.blocker_assumptions=blockers;to.verified=true;return true;
}

// Pure payload seam for the immutable row-major SDK distance cache. The caller
// supplies the same queried dimensions/root; live occupancy remains separate.
inline bool route_from_distances_into(p6::Point from,p6::Point destination,int width,int height,
                                      std::span<const std::uint16_t> distances,Route& to) {
    reset_route_header(to);
    if(width<=0||height<=0||std::size_t(width)>distances.size()/std::size_t(height)||
        distances.size()!=std::size_t(width)*std::size_t(height)||
        from.x<0||from.y<0||from.x>=width||from.y>=height||
        destination.x<0||destination.y<0||destination.x>=width||destination.y>=height)return false;
    const auto at=[&](p6::Point p){return distances[std::size_t(p.y)*std::size_t(width)+std::size_t(p.x)];};
    to.tiles[to.count++]=from;
    while(!(from==destination)&&to.count<max_route_tiles) {
        const int old=at(from);
        if(old<=0||old>=65535){reset_route_header(to);return false;}
        p6::Point next=from;
        for(int y=-1;y<=1&&next==from;++y)for(int x=-1;x<=1;++x) {
            const p6::Point point{from.x+x,from.y+y};
            if((!x&&!y)||point.x<0||point.y<0||point.x>=width||point.y>=height)continue;
            if(at(point)==old-1){next=point;break;}
        }
        if(next==from){reset_route_header(to);return false;}
        to.tiles[to.count++]=next;from=next;
    }
    if(!(from==destination)){reset_route_header(to);return false;}
    to.verified=true;return true;
}
} // namespace p8::native
