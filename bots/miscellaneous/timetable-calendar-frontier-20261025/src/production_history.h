#pragma once
// Fixed-storage semantic Trail snapshot. This is exact sequence equality, not
// current Trail Known status, route identity or command authority. The caller
// supplies the actual monotonic walk revision while copying and separately
// checks current Trail Known before using a completed semantic comparison.
#include "production_walk.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace tt::production_history {
using Point=tt::production_walk::Point;
inline constexpr std::size_t capacity=128;
enum class Status { NeedCompute, Ready, Unknown };

class Snapshot {
public:
    Status begin(std::size_t count,std::uint64_t walk_revision)
    {
        size_=copied_=last_work_=0;
        generation_=walk_revision;
        status_=Status::Unknown;
        if (count>capacity) return status_;
        size_=count;
        status_=count==0 ? Status::Ready : Status::NeedCompute;
        return status_;
    }

    // No source pointer is retained. Every copied point consumes one unit;
    // storage initialization is the fixed 128-Point object construction only.
    // A revision/size change invalidates even a previously Ready capture.
    Status advance(std::span<const Point> current,std::uint64_t walk_revision,std::size_t units)
    {
        last_work_=0;
        if (status_==Status::Unknown) return status_;
        if (walk_revision!=generation_ || current.size()!=size_)
        { status_=Status::Unknown; return status_; }
        if (status_==Status::Ready) return status_;
        while (last_work_<units && copied_<size_)
        {
            points_[copied_]=current[copied_];
            ++copied_;
            ++last_work_;
        }
        if (copied_==size_) status_=Status::Ready;
        return status_;
    }

    // Exact semantic equality deliberately ignores revision: a caller may prove
    // a later actual history identical despite a different revision. Partial or
    // invalid storage never matches, and the empty Ready sequence is exact too.
    bool matches(std::span<const Point> current) const
    {
        if (status_!=Status::Ready || current.size()!=size_) return false;
        for (std::size_t i=0;i<size_;++i)
            if (points_[i]!=current[i]) return false;
        return true;
    }

    Status status() const { return status_; }
    std::size_t size() const { return size_; }
    std::size_t copied() const { return copied_; }
    std::size_t last_work() const { return last_work_; }
    std::uint64_t generation() const { return generation_; }

private:
    std::array<Point,capacity> points_{};
    std::size_t size_{},copied_{},last_work_{};
    std::uint64_t generation_{};
    Status status_{Status::Unknown};
};

} // namespace tt::production_history
