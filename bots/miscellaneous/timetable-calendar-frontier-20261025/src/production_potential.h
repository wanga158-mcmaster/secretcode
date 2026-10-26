#pragma once
// Pure conditional upper exposure: every supplied fixed-position source contributes
// its nonnegative fixed strength throughout its Chebyshev range, regardless of life,
// wake state, stun, shield or beneficial mitigation. Supplying ALL possible harmful
// sources and accepting fixed strength/range are caller/native premises, not proved
// engine facts. Charges are per represented path point, not an elapsed-time promise.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mb::production::potential {

struct Point {
    int x{},y{};
    friend bool operator==(Point,Point)=default;
};
struct Source { Point pos; float strength{}; int range{}; };
enum class Status { Ready, NeedCompute, Unknown };
struct Value { Status status{Status::Unknown}; double upper{}; };
struct PathBound {
    Status status{Status::Unknown};
    double exit_before_yield{},wait_then_exit{};
};

namespace detail {
// Upward rounding is intentional. A positive term must never disappear through
// round-to-nearest; an unrepresentable finite upper bound is Unknown, never capped.
inline bool add_upper(double a,double b,double& out)
{
    if (!std::isfinite(a) || !std::isfinite(b) || a<0 || b<0) return false;
    if (a==0) { out=b; return true; }
    if (b==0) { out=a; return true; }
    const double sum=a+b;
    if (!std::isfinite(sum)) return false;
    out=std::nextafter(sum,std::numeric_limits<double>::infinity());
    return std::isfinite(out);
}
} // namespace detail

class Field {
public:
    // reserve allocates uninitialized capacity only. Every zero-cell append,
    // source validation/setup and source/tile addition consumes one advance unit.
    // No source or grid initialization loop runs in this constructor.
    Field(int width,int height,std::vector<Source> sources)
        : width_(width),height_(height),sources_(std::move(sources))
    {
        if (width<=0 || height<=0) return;
        const auto w=static_cast<std::size_t>(width),h=static_cast<std::size_t>(height);
        if (w>std::numeric_limits<std::size_t>::max()/h) return;
        count_=w*h;
        if (count_>values_.max_size()) return;
        try { values_.reserve(count_); }
        catch (const std::bad_alloc&) { return; }
        catch (const std::length_error&) { return; }
        status_=Status::NeedCompute;
    }

    Field(const Field&)=delete;
    Field& operator=(const Field&)=delete;
    Field(Field&& other) noexcept { *this=std::move(other); }
    Field& operator=(Field&& other) noexcept
    {
        if (this==&other) return *this;
        width_=other.width_; height_=other.height_;
        min_x_=other.min_x_; max_x_=other.max_x_; min_y_=other.min_y_; max_y_=other.max_y_;
        x_=other.x_; y_=other.y_; count_=other.count_; source_=other.source_;
        last_work_=other.last_work_; source_active_=other.source_active_; status_=other.status_;
        sources_=std::move(other.sources_); values_=std::move(other.values_);
        other.status_=Status::Unknown;
        other.width_=other.height_=0;
        other.count_=other.source_=other.last_work_=0;
        other.source_active_=false;
        return *this;
    }

    Status status() const { return status_; }
    int width() const { return width_; }
    int height() const { return height_; }
    std::size_t initialized_tiles() const { return values_.size(); }
    std::size_t last_work() const { return last_work_; }

    Status advance(std::size_t work_units)
    {
        last_work_=0;
        if (status_!=Status::NeedCompute) return status_;
        while (last_work_<work_units)
        {
            if (values_.size()<count_)
            {
                values_.push_back(0);
                ++last_work_;
                continue;
            }
            if (source_==sources_.size())
            { status_=Status::Ready; return status_; }
            if (!source_active_)
            {
                ++last_work_;
                const auto& source=sources_[source_];
                if (!inside(source.pos) || !std::isfinite(source.strength) ||
                    source.strength<0 || source.range<0)
                { status_=Status::Unknown; return status_; }
                if (source.strength==0) { ++source_; continue; }
                const auto r=static_cast<std::int64_t>(source.range);
                min_x_=static_cast<int>(std::max<std::int64_t>(0,static_cast<std::int64_t>(source.pos.x)-r));
                max_x_=static_cast<int>(std::min<std::int64_t>(width_-1,static_cast<std::int64_t>(source.pos.x)+r));
                min_y_=static_cast<int>(std::max<std::int64_t>(0,static_cast<std::int64_t>(source.pos.y)-r));
                max_y_=static_cast<int>(std::min<std::int64_t>(height_-1,static_cast<std::int64_t>(source.pos.y)+r));
                x_=min_x_; y_=min_y_; source_active_=true;
                continue;
            }
            ++last_work_;
            const auto index=static_cast<std::size_t>(y_)*static_cast<std::size_t>(width_)+static_cast<std::size_t>(x_);
            double upper{};
            if (!detail::add_upper(values_[index],sources_[source_].strength,upper))
            { status_=Status::Unknown; return status_; }
            values_[index]=upper;
            if (x_<max_x_) ++x_;
            else if (y_<max_y_) { x_=min_x_; ++y_; }
            else { source_active_=false; ++source_; }
        }
        if (values_.size()==count_ && source_==sources_.size()) status_=Status::Ready;
        return status_;
    }

    Value at(Point point) const
    {
        if (status_!=Status::Ready || !inside(point)) return {};
        const auto index=static_cast<std::size_t>(point.y)*static_cast<std::size_t>(width_)+static_cast<std::size_t>(point.x);
        const auto value=values_[index];
        return std::isfinite(value) && value>=0 ? Value{Status::Ready,value} : Value{};
    }

private:
    bool inside(Point point) const
    { return point.x>=0 && point.y>=0 && point.x<width_ && point.y<height_; }
    int width_{},height_{},min_x_{},max_x_{},min_y_{},max_y_{},x_{},y_{};
    std::size_t count_{},source_{},last_work_{};
    bool source_active_{};
    Status status_{Status::Unknown};
    std::vector<Source> sources_;
    std::vector<double> values_;
};

// This validates only field completeness, nonempty finite in-bounds point charges
// and arithmetic. Adjacency, actual movement, quiet anchor and one input interval
// per landing remain caller proofs. Repeated points are charged repeatedly.
inline PathBound bound_path(const Field& field,std::span<const Point> path)
{
    if (field.status()!=Status::Ready || path.empty()) return {};
    PathBound result;
    for (std::size_t i=0;i<path.size();++i)
    {
        const auto value=field.at(path[i]);
        if (value.status!=Status::Ready) return {};
        if (i!=0 && !detail::add_upper(result.exit_before_yield,value.upper,result.exit_before_yield)) return {};
        if (!detail::add_upper(result.wait_then_exit,value.upper,result.wait_then_exit)) return {};
    }
    result.status=Status::Ready;
    return result;
}

} // namespace mb::production::potential
