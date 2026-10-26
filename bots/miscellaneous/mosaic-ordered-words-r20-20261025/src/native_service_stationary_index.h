#pragma once
#include "native_service_types.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace mosaic::native_service {

enum class StationaryConfigure : std::uint8_t {configured,retained,invalid,changed};
enum class StationaryAdd : std::uint8_t {added,duplicate,conflict,out_of_bounds,invalid_ordinal,unconfigured};

// Immutable catalogue geometry only: a cell's ordinal is neither an observed
// occupant nor permission to attack, move or capture. A value-initialized or
// constinit object has empty zero cells; ordinal0 is encoded as1. Configure
// writes only dimensions. It never performs a hidden MaxCells clear, and an
// incompatible reconfiguration must use a separate explicitly owned lifetime.
template<std::size_t MaxCells=16384> class StationaryIndex {
    static_assert(MaxCells>0&&MaxCells<=std::size_t(std::numeric_limits<int>::max()));
public:
    constexpr StationaryIndex()noexcept=default;
    constexpr StationaryConfigure configure(int width,int height)noexcept {
        if(width<=0||height<=0||std::size_t(width)>MaxCells/std::size_t(height))return StationaryConfigure::invalid;
        if(width_||height_)return width==width_&&height==height_?StationaryConfigure::retained:StationaryConfigure::changed;
        width_=width;height_=height;return StationaryConfigure::configured;
    }
    constexpr StationaryAdd add(Point point,int ordinal)noexcept {
        if(!configured())return StationaryAdd::unconfigured;
        if(ordinal<0)return StationaryAdd::invalid_ordinal;
        if(!inside(point))return StationaryAdd::out_of_bounds;
        auto& cell=cells_[offset(point)];const auto encoded=std::uint32_t(ordinal)+1u;
        if(cell)return cell==encoded?StationaryAdd::duplicate:StationaryAdd::conflict;
        cell=encoded;return StationaryAdd::added;
    }
    constexpr int at(Point point)const noexcept {
        if(!configured()||!inside(point))return -1;
        const auto value=cells_[offset(point)];return value?int(value-1u):-1;
    }
    constexpr bool configured()const noexcept{return width_>0&&height_>0;}
    constexpr int width()const noexcept{return width_;}
    constexpr int height()const noexcept{return height_;}
    static constexpr std::size_t max_cells=MaxCells;
private:
    std::array<std::uint32_t,MaxCells> cells_{};int width_{},height_{};
    constexpr bool inside(Point point)const noexcept {
        return point.x>=0&&point.y>=0&&point.x<width_&&point.y<height_;
    }
    constexpr std::size_t offset(Point point)const noexcept {
        return std::size_t(point.y)*std::size_t(width_)+std::size_t(point.x);
    }
};

} // namespace mosaic::native_service
