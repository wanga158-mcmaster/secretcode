#pragma once
// DATED route-nomination input only. A zero occupancy bit is Unknown/MayRefuse,
// never KnownFree. This type grants no selected-packet or actuation authority.
#include "candidate_observation.h"

namespace mosaic::route_observation {
using Stamp=selected::Stamp;
enum class Status {ready,incomplete,stale,unsupported};
enum class Tile {out_of_bounds,observed_blocked,unknown_may_refuse};
inline constexpr std::size_t max_packed_bytes=1250; // supported maps <=100x100
inline std::size_t packed_size(int width,int height) {
    if(width<=0||height<=0||width>100||height>100)return 0;
    return (std::size_t(width)*std::size_t(height)+7)/8;
}
struct SelfPacket {
    Stamp stamp;frame::Self body;spatial::Token layout;
    std::size_t member_count{};bool complete{};
    void revoke(){complete=false;body={};layout={};member_count=0;}
};
struct Packet {
    Stamp stamp;frame::Self body;spatial::Token layout;
    std::size_t member_count{};int width{},height{};
    std::vector<std::uint8_t> observed_blocked;
    bool has_visible_enemy{},complete{};
    void revoke() {
        complete=false;body={};layout={};member_count=0;width=height=0;
        observed_blocked.clear();has_visible_enemy=false;
    }
    Tile tile(int x,int y)const {
        if(!complete||!layout.valid(member_count)||x<0||y<0||x>=width||y>=height||
           observed_blocked.size()!=packed_size(width,height))return Tile::out_of_bounds;
        const auto bit=std::size_t(y)*std::size_t(width)+std::size_t(x);
        return (observed_blocked[bit/8]&(std::uint8_t(1u)<<(bit%8)))?
            Tile::observed_blocked:Tile::unknown_may_refuse;
    }
};
struct Cache {
    const frame::SourceWorld* owner{};
    candidate::Service self_reader;
};
template<class Output>struct Publication {
    Output& out;
    ~Publication(){if(!out.complete)out.revoke();}
};

// Observer seam exercises exactly this staging/publication algorithm. Revision
// is caller-managed mutation provenance; native clock/actor queries cannot
// discover an unreported same-input mutation.
template<class Observer>
Status collect(Observer& source,Packet& out,Stamp stamp,spatial::Token layout,
               std::size_t member_count,int width,int height) {
    out.revoke();out.stamp=stamp;Publication publication{out};
    const auto stopped=[&] {
        return !source.same_input()||!layout.valid(member_count)?Status::stale:Status::incomplete;
    };
    if(!layout.valid(member_count)||!packed_size(width,height))return Status::unsupported;
    if(!source.begin(stamp)||!source.read_self(out.body))return stopped();
    if(!out.body.alive)return Status::unsupported;
    out.layout=layout;out.member_count=member_count;out.width=width;out.height=height;
    if(!source.read_observed(out))return stopped();
    if(out.observed_blocked.size()!=packed_size(width,height))return Status::unsupported;
    if(!source.fence(20000))return stopped();
    if(!source.same_layout(layout,member_count,width,height))return Status::stale;
    out.complete=true;return Status::ready;
}
template<class Observer>
Status collect_self(Observer& source,SelfPacket& out,Stamp stamp,spatial::Token layout,
                    std::size_t member_count,int width,int height) {
    out.revoke();out.stamp=stamp;Publication publication{out};
    const auto stopped=[&] {
        return !source.same_input()||!layout.valid(member_count)?Status::stale:Status::incomplete;
    };
    if(!layout.valid(member_count)||!packed_size(width,height))return Status::unsupported;
    if(!source.begin(stamp)||!source.read_self(out.body))return stopped();
    if(!source.fence(20000))return stopped();
    if(!source.same_layout(layout,member_count,width,height))return Status::stale;
    out.layout=layout;out.member_count=member_count;out.complete=true;return Status::ready;
}
class NativeSource {
    frame::SourceWorld& world_;
    candidate::NativeSource self_;
public:
    NativeSource(frame::SourceWorld& world,Cache& cache):world_(world),self_(world,cache.self_reader){}
    bool begin(Stamp stamp){return self_.begin(stamp);} // existing 52K admission
    bool read_self(frame::Self& body){return self_.read_self(body);}
    bool fence(std::uint64_t reserve)const{return self_.fence(reserve);}
    bool same_input()const{return self_.same_input();}
    bool same_layout(spatial::Token token,std::size_t count,int width,int height)const {
        const auto current=world_.spatial_support.borrow();
        return token.valid(count)&&current.index==token.index&&current.generation==token.generation&&
            current.count==count&&world_.camps.size()==count&&world_.static_blocked&&
            world_.static_blocked->width()==width&&world_.static_blocked->height()==height;
    }
    bool read_observed(Packet& out) {
        using namespace cambc;
        // These masks retain exactly the SDK's observation scope. In particular
        // there is no invented unseen-ally position or visible-only landing rule.
        auto occupied=vision::champion_tiles(team::ally);
        const auto enemy=vision::champion_tiles(team::enemy);
        out.has_visible_enemy=!enemy.is_empty();
        occupied|=enemy;
        occupied|=vision::camp_tiles();
        occupied|=vision::teleport_tiles();
        if(out.body.pos.x<0||out.body.pos.y<0||out.body.pos.x>=out.width||out.body.pos.y>=out.height)
            return false;
        occupied.set(out.body.pos,false); // ONLY actual fresh self origin
        out.observed_blocked.resize(packed_size(out.width,out.height));
        occupied.export_linear_buffer(std::span<std::uint8_t>{out.observed_blocked});
        return fence(20000); // boards/destructors finish before publication fence
    }
};
inline bool bind(frame::SourceWorld& world,Cache& cache) {
    if(cache.owner&&cache.owner!=&world)return false;
    if(!world.initialized||!world.self_design||!world.static_blocked||!world.spatial_support.valid())return false;
    cache.owner=&world;return true;
}
inline Status capture(frame::SourceWorld& world,Cache& cache,Stamp stamp,Packet& out) {
    out.revoke();out.stamp=stamp;
    if(!bind(world,cache))return Status::unsupported;
    NativeSource source(world,cache);
    return collect(source,out,stamp,world.spatial_support.borrow(),world.camps.size(),
                   world.static_blocked->width(),world.static_blocked->height());
}
// A fresh self-only observation does not update/re-stamp an old route Packet.
// Alive=false is supported here as an actual self fact, useful to reject work.
inline Status capture_self(frame::SourceWorld& world,Cache& cache,Stamp stamp,SelfPacket& out) {
    out.revoke();out.stamp=stamp;
    if(!bind(world,cache))return Status::unsupported;
    NativeSource source(world,cache);
    return collect_self(source,out,stamp,world.spatial_support.borrow(),world.camps.size(),
                        world.static_blocked->width(),world.static_blocked->height());
}
} // namespace mosaic::route_observation
