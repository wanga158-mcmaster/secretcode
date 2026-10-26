#pragma once
#include "model-runtime.h"
#include <cambc.h>
#include <vector>

namespace plan4::runtime {
// Current personal masks supplement immutable public topology. These callbacks
// only read sealed local masks/cache; executor permissions are freshly queried.
class SpatialEvidence {
    std::vector<std::uint8_t> visible_, occupied_;
    SpatialFrame frame_{};
    unsigned floor_ = 0;
    bool budget() const {
        return cambc::fuel_remaining() >= floor_ + 1200 && int(cambc::round_number()) == frame_.clock.round;
    }
    static TileOccupancy occupancy(Point p, const void* context) {
        const auto& self = *static_cast<const SpatialEvidence*>(context);
        if (!self.frame_.current_complete || p.x < 0 || p.y < 0 || p.x >= self.frame_.topology.width || p.y >= self.frame_.topology.height)
            return TileOccupancy::unknown;
        const unsigned key=unsigned(p.y*self.frame_.topology.width+p.x), mask=1u<<(key&7);
        return !(self.visible_[key>>3]&mask) ? TileOccupancy::unknown
            : (self.occupied_[key>>3]&mask) ? TileOccupancy::occupied : TileOccupancy::free;
    }
public:
    template<class Camps, class Enemies>
    void start(const Information& info, const Menu& menu, PublicTopology topology, const Camps& camps, const Enemies& enemies,
               int (*routes)(Point,Point,Point*,int,const void*), const void* route_context,
               int graph_quota, unsigned common_floor, int reference_vertices_used = 0) {
        frame_ = {}; frame_.clock = {info.round,{info.own.id}};
        frame_.information = &info; frame_.menu = &menu;
        frame_.topology = topology; frame_.travel = info.travel;
        frame_.route_prefix = routes; frame_.route_context = route_context;
        frame_.graph_quota = std::max(0,std::min(192,graph_quota));
        frame_.reference_vertices_used = std::max(0,reference_vertices_used);
        frame_.optional_fuel_floor = common_floor;
        frame_.occupancy = occupancy; frame_.occupancy_context = this;
        const unsigned before = unsigned(cambc::fuel_remaining());
        floor_ = std::max(common_floor,before > 5000 ? before-5000 : common_floor);
        const auto tiles=std::int64_t(topology.width)*topology.height;
        if (!topology.complete || topology.width<=0 || topology.height<=0 || tiles>16384 || !info.own.alive || !budget()) return;
        const auto bytes=std::size_t((tiles+7)/8);
        if (bytes!=cambc::bitboard::linear_buffer_size()) return;
        visible_.resize(bytes);occupied_.resize(bytes);
        const auto visible = cambc::vision::visible_tiles();
        if (!budget()) return;
        auto occupied = cambc::vision::champion_tiles();
        if (!budget()) return;
        occupied |= cambc::vision::camp_tiles();
        if (!budget()) return;
        occupied |= cambc::vision::teleport_tiles();
        if (!budget()) return;
        // Pinned SDK defines packed row-major bits, least-significant first.
        // Its method's "one byte per tile" prose conflicts with its actual
        // ceil(tileCount/8) size; use the pinned packed-buffer contract.
        visible.export_linear_buffer(visible_);
        if (!budget()) return;
        occupied.export_linear_buffer(occupied_);
        if (!budget()) return;
        if (info.own.position.x<0 || info.own.position.y<0 || info.own.position.x>=topology.width || info.own.position.y>=topology.height) return;
        const unsigned own_key=unsigned(info.own.position.y*topology.width+info.own.position.x);
        occupied_[own_key>>3]&=std::uint8_t(~(1u<<(own_key&7)));
        frame_.current_complete = true;
        // Match the executor's conservative stationary-recall admission. No
        // safety claim follows from an incomplete prefix or unseen camp HP.
        bool safe = info.own.recall_safe && !info.observation_truncated;
        int count = 0;
        for (const auto& monster : camps) {
            if (!safe) break;
            if (++count > 64 || !budget()) { safe=false; break; }
            if (!monster.is_visible()) { safe=false; break; }
            if (!budget()) { safe=false; break; }
            if (!monster.is_alive()) continue;
            if (!budget()) { safe=false; break; }
            if (!monster.is_awake()) continue;
            if (!budget()) { safe=false; break; }
            if (monster.strength() <= 0) continue;
            if (!budget()) { safe=false; break; }
            const auto p = monster.position();
            if (!budget()) { safe=false; break; }
            if (std::max(std::abs(p.x-info.own.position.x),std::abs(p.y-info.own.position.y)) <= monster.attack_range()+1) safe=false;
        }
        count = 0;
        for (const auto& enemy : enemies) {
            if (!safe) break;
            if (++count > 5 || !budget()) { safe=false; break; }
            if (!enemy.is_visible()) { safe=false; break; }
            if (!budget()) { safe=false; break; }
            if (!enemy.is_alive()) continue;
            if (!budget()) { safe=false; break; }
            const auto p = enemy.position();
            if (std::max(std::abs(p.x-info.own.position.x),std::abs(p.y-info.own.position.y)) <= 8) safe=false;
        }
        frame_.own_recall_here = safe && budget();
        if (int(cambc::round_number()) != info.round) frame_.current_complete = frame_.own_recall_here = false;
    }
    const SpatialFrame& frame() const { return frame_; }
};
} // namespace plan4::runtime
