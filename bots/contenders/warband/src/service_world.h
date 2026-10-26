#pragma once

#include <cambc.h>
#include "service_route.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace wb {

// Static geometry only. No stored health, wake, capture or occupancy is live
// authority. The first family permanently excludes every harmful spawn range.
class ServiceWorld {
public:
    enum class State { Empty, Terrain, PotentialSupport, Ready, Unsupported };
    enum class Failure { None, Dimensions, GroupCount, GroupMembers, MemberLayout, Design, InitialExposure, TerrainBuffer };
    route::Grid grid;
    std::vector<cambc::map::camp_group> groups;
    std::vector<std::uint8_t> permanent, static_camp;
    std::vector<int> altars;

    State state() const { return state_; }
    Failure failure() const { return failure_; }
    int terrain_stage() const { return stage_; }
    bool quiet(int tile) const { return tile >= 0 && tile < area_ && grid.quiet[tile]; }
    int tile(cambc::vec2 p) const { return p.y * grid.width + p.x; }
    cambc::vec2 position(int tile) const { return {tile % grid.width, tile / grid.width}; }

    bool begin(cambc::vec2 actual_origin) {
        if (state_ != State::Empty) return false;
        const int width = cambc::map::width(), height = cambc::map::height();
        if (width <= 0 || height <= 0 || width > 100 || height > 100) return unsupported(Failure::Dimensions);
        grid.width = width; grid.height = height; area_ = width * height;
        groups = cambc::map::camp_groups();
        if (groups.empty() || groups.size() > 51) return unsupported(Failure::GroupCount);
        unsigned count = 0;
        std::array<bool, 6> loaded{};
        for (const auto& group : groups) {
            if (group.camps().empty() || group.camps().size() > 30) return unsupported(Failure::GroupMembers);
            for (const auto& member : group.camps()) {
                const auto p = member.spawn_position();
                const int kind = static_cast<int>(member.kind());
                if (++count > 256 || p.x < 0 || p.y < 0 || p.x >= width || p.y >= height || kind < 0 || kind >= 6)
                    return unsupported(Failure::MemberLayout);
                if (!loaded[kind]) { designs_[kind] = member.base_stats(); loaded[kind] = true; }
                const auto design = designs_[kind];
                if (design.base_attack_range < 0 || design.base_attack_range > 20 || design.base_strength < 0)
                    return unsupported(Failure::Design);
                const bool harmful = !member.is_gem_cluster() && !member.is_gold_cluster();
                if (harmful && static_cast<int>(actual_origin.chebyshev_distance(p)) <= design.base_attack_range)
                    start_quiet_ = false;
                camps_.push_back({p.x,p.y,design.base_attack_range,harmful});
            }
        }
        // Outside this first supported family, do not silently prepare while
        // exposed. This is Unsupported, not proof that no physical escape exists.
        if (!start_quiet_) return unsupported(Failure::InitialExposure);
        grid.blocked.assign(area_, 0); grid.quiet.assign(area_, 1);
        grid.spawn.assign(area_, 0); grid.bank.assign(area_, 0);
        permanent.assign(area_, 0); static_camp.assign(area_, 0);
        for (const auto& camp : camps_) static_camp[camp.y * width + camp.x] = 1;
        stage_ = 0; state_ = State::Terrain;
        return true;
    }

    // One terrain packet OR <=128 potential-support cells per input. No native
    // bitboard construction, whole-world live observation or gameplay here.
    State advance() {
        if (state_ == State::Terrain) {
            switch (stage_++) {
            case 0: import_terrain(cambc::map::walls(), 0); break;
            case 1: import_terrain(cambc::map::holes(), 0); break;
            case 2: import_terrain(cambc::map::altars(), 1); break;
            case 3: import_terrain(cambc::map::spawn_region(cambc::team::enemy), 0); break;
            case 4: import_terrain(cambc::map::spawn_region(cambc::team::ally), 2); break;
            default:
                grid.blocked = permanent;
                for (int i = 0; i < area_; ++i) if (static_camp[i]) grid.blocked[i] = 1;
                state_ = State::PotentialSupport;
                break;
            }
            return state_;
        }
        if (state_ != State::PotentialSupport) return state_;
        unsigned cells = 0;
        while (support_ < camps_.size() && cells < 128) {
            const auto& camp = camps_[support_];
            if (!camp.harmful) { ++support_; dx_ = dy_ = 0; continue; }
            const int side = 2 * camp.range + 1;
            const int x = camp.x + dx_ - camp.range, y = camp.y + dy_ - camp.range;
            if (x >= 0 && y >= 0 && x < grid.width && y < grid.height) grid.quiet[y * grid.width + x] = 0;
            ++cells;
            if (++dx_ == side) { dx_ = 0; if (++dy_ == side) {dy_ = 0; ++support_;} }
        }
        if (support_ == camps_.size()) state_ = State::Ready;
        return state_;
    }

    bool harmless(const cambc::map::camp_group& group) const {
        if (!group.gem_value() || group.camps().empty() || group.camps().size() > 30) return false;
        for (const auto& member : group.camps())
            if ((!member.is_gem_cluster() && !member.is_gold_cluster()) || !quiet(tile(member.spawn_position()))) return false;
        return true;
    }

private:
    struct StaticCamp { int x, y, range; bool harmful; };
    State state_ = State::Empty;
    Failure failure_ = Failure::None;
    int area_ = 0, stage_ = 0, dx_ = 0, dy_ = 0;
    bool start_quiet_ = true;
    std::size_t support_ = 0;
    std::array<cambc::camp_stats, 6> designs_{};
    std::vector<StaticCamp> camps_;

    bool unsupported(Failure failure) { failure_=failure;state_ = State::Unsupported; return false; }
    void import_terrain(const cambc::bitboard& board, int role) {
        const auto bytes = board.export_linear_buffer();
        if (bytes.size() != static_cast<std::size_t>((area_ + 7) / 8)) {unsupported(Failure::TerrainBuffer);return;}
        for (int i = 0; i < area_; ++i) {
            if (!(bytes[i >> 3] & (1u << (i & 7)))) continue;
            if (role == 2) { grid.spawn[i] = 1; continue; }
            permanent[i] = 1;
            if (role == 1) {
                altars.push_back(i);
                const auto p = position(i);
                for (int dy = -static_cast<int>(cambc::constants::submit_gems_range); dy <= static_cast<int>(cambc::constants::submit_gems_range); ++dy)
                    for (int dx = -static_cast<int>(cambc::constants::submit_gems_range); dx <= static_cast<int>(cambc::constants::submit_gems_range); ++dx) {
                        if (!dx && !dy) continue;
                        const int x = p.x + dx, y = p.y + dy;
                        if (x >= 0 && y >= 0 && x < grid.width && y < grid.height) grid.bank[y * grid.width + x] = 1;
                    }
            }
        }
    }
};
} // namespace wb
