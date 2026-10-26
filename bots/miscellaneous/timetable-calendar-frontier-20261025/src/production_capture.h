#pragma once
// Dated post-clear geometry for one selected group. The caller must bind this
// native field's zero roots to that group's actual immutable spawn region and
// its exact frozen graph premises. No Boolean reachability certificate suffices.
// No HP, epoch, allowance, current occupancy, quietness, hold timer or receipt is
// retained. Every actual primitive/hold needs separate fresh native authority.
#include "service_model.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace tt::production_capture {
using Point=mb::service::Point;
enum class Status { Ready, NeedCompute, Unknown };
enum class Kind { Step, Hold };
struct GeometryLeg { Kind kind; Point origin,destination; };

class CaptureWord
{
public:
    CaptureWord(int width,int height,int group,std::uint64_t graph,
                std::vector<std::uint16_t> dated_distances,std::size_t max_nodes)
        : width_{width},height_{height},group_{group},graph_{graph},
          distances_{std::move(dated_distances)},max_nodes_{max_nodes}
    {
        if (width<=0 || height<=0 || group<0 || max_nodes==0) return;
        const auto cells=static_cast<std::uint64_t>(width)*static_cast<std::uint64_t>(height);
        valid_=cells<=std::numeric_limits<std::size_t>::max() && distances_.size()==cells;
    }
    CaptureWord(const CaptureWord&)=delete;
    CaptureWord& operator=(const CaptureWord&)=delete;
    CaptureWord(CaptureWord&& other) noexcept { *this=std::move(other); }
    CaptureWord& operator=(CaptureWord&& other) noexcept
    {
        if (this==&other) return *this;
        width_=other.width_; height_=other.height_; group_=other.group_; graph_=other.graph_;
        distances_=std::move(other.distances_); path_=std::move(other.path_);
        max_nodes_=other.max_nodes_; cursor_=other.cursor_; last_work_=other.last_work_;
        initial_=other.initial_; distance_=other.distance_; status_=other.status_;
        valid_=other.valid_; started_=other.started_;
        other.invalidate();
        return *this;
    }
    Status status() const { return status_; }
    std::size_t built_nodes() const { return path_.size(); }
    std::size_t last_work() const { return last_work_; }
    // Full path publication is geometry only; partial vectors are never exposed.
    std::span<const Point> path() const
    { return status_==Status::Ready?std::span<const Point>{path_}:std::span<const Point>{}; }
    void invalidate() { valid_=false; status_=Status::Unknown; last_work_=0; }

    // Repeated same-origin begins retain partial construction. Once Ready, only
    // the current consumed origin is accepted. Displacement/key changes revoke.
    Status begin(Point actual_origin,int group,std::uint64_t graph)
    {
        if (!valid_ || group!=group_ || graph!=graph_) { invalidate(); return status_; }
        if (started_)
        {
            const auto expected=status_==Status::Ready?path_[cursor_]:initial_;
            if (actual_origin!=expected) invalidate();
            return status_;
        }
        const auto distance=at(actual_origin);
        if (distance==uncovered || distance>=distances_.size() ||
            static_cast<std::size_t>(distance)+1>max_nodes_)
        { invalidate(); return status_; }
        try { path_.reserve(static_cast<std::size_t>(distance)+1); }
        catch (const std::bad_alloc&) { invalidate(); return status_; }
        catch (const std::length_error&) { invalidate(); return status_; }
        initial_=actual_origin; distance_=distance; started_=true;
        status_=Status::NeedCompute;
        return status_;
    }

    // One unit appends the origin or one strict d-1 point (at most eight neighbor
    // checks). An old compute_to may not cover this frontier: missing gradient
    // means Unknown, never physical or global infeasibility. No search is run.
    Status advance(std::size_t units)
    {
        last_work_=0;
        if (status_!=Status::NeedCompute) return status_;
        while (last_work_<units)
        {
            ++last_work_;
            if (path_.empty()) path_.push_back(initial_);
            else
            {
                constexpr int directions[][2]={{0,1},{1,1},{1,0},{1,-1},{0,-1},{-1,-1},{-1,0},{-1,1}};
                const auto from=path_.back(); bool found=false;
                for (const auto& direction:directions)
                {
                    const Point next{from.x+direction[0],from.y+direction[1]};
                    const auto value=at(next);
                    if (value!=uncovered && distance_>0 && value==distance_-1)
                    { path_.push_back(next); distance_=value; found=true; break; }
                }
                if (!found) { invalidate(); return status_; }
            }
            if (distance_==0) { status_=Status::Ready; return status_; }
        }
        return status_;
    }

    // Fresh Reward is an external resolving-operator result, not a timer or this
    // dated field. False supplies no leg but does not refute historical geometry.
    // Hold identifies actual position at a pinned zero root; it never pays out,
    // increments capture progress, grants a safe wait or closes a production cycle.
    std::optional<GeometryLeg> leg(Point actual_origin,int group,std::uint64_t graph,bool fresh_reward_available)
    {
        if (group!=group_ || graph!=graph_) { invalidate(); return std::nullopt; }
        if (status_!=Status::Ready) return std::nullopt;
        if (path_[cursor_]!=actual_origin) { invalidate(); return std::nullopt; }
        if (!fresh_reward_available) return std::nullopt;
        if (cursor_+1==path_.size()) return GeometryLeg{Kind::Hold,actual_origin,actual_origin};
        return GeometryLeg{Kind::Step,actual_origin,path_[cursor_+1]};
    }

    // Only after the actual successful ordinary move. No refusal, planned edge,
    // stationary hold, timer event or reverse move is silently consumed.
    bool consume_applied(Point from,Point to,int group,std::uint64_t graph)
    {
        if (status_!=Status::Ready || group!=group_ || graph!=graph_ || cursor_+1>=path_.size() ||
            path_[cursor_]!=from || path_[cursor_+1]!=to)
        { invalidate(); return false; }
        ++cursor_;
        return true;
    }

private:
    static constexpr auto uncovered=std::numeric_limits<std::uint16_t>::max();
    std::uint16_t at(Point point) const
    {
        if (point.x<0 || point.y<0 || point.x>=width_ || point.y>=height_) return uncovered;
        return distances_[static_cast<std::size_t>(point.y)*static_cast<std::size_t>(width_)+point.x];
    }
    int width_{},height_{},group_{-1};
    std::uint64_t graph_{};
    std::vector<std::uint16_t> distances_;
    std::vector<Point> path_;
    std::size_t max_nodes_{},cursor_{},last_work_{};
    Point initial_;
    std::uint16_t distance_{};
    Status status_{Status::Unknown};
    bool valid_{},started_{};
};
} // namespace tt::production_capture
