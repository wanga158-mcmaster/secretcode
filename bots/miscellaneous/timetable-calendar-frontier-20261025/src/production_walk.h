#pragma once
// Actual ordinary-walk history, not a future legality/safety certificate.
// A caller-supplied quiet premise names an immutable point for its current model.
#include "service_model.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace tt::production_walk {
using Point=mb::service::Point;
enum class Status { Known, Unknown };

inline bool adjacent(Point from,Point to)
{
    const auto dx=static_cast<std::int64_t>(from.x)-to.x;
    const auto dy=static_cast<std::int64_t>(from.y)-to.y;
    return dx>=-1 && dx<=1 && dy>=-1 && dy<=1 && (dx!=0 || dy!=0);
}

class Trail
{
public:
    explicit Trail(std::size_t max_nodes) : max_nodes_{max_nodes} {}

    void invalidate()
    {
        revise();
        nodes_.clear();
        status_=Status::Unknown;
    }

    // This is a supplied observation premise, not a discovered property. It may
    // establish the first anchor without inventing a zero-length movement edge.
    Status anchor_quiet(Point actual)
    {
        if (max_nodes_==0) { invalidate(); return status_; }
        if (!revise()) return status_;
        nodes_.assign(1,actual);
        status_=Status::Known;
        return status_;
    }

    // Call only after a successful ordinary move. A refusal/dash/displacement is
    // not a recorded edge; its caller must invalidate this historical witness.
    Status record(Point from,Point to,bool from_quiet,bool to_quiet)
    {
        if (!adjacent(from,to) || max_nodes_==0)
        { invalidate(); return status_; }
        if (from_quiet)
        {
            if (anchor_quiet(from)!=Status::Known) return status_;
        }
        else if (status_!=Status::Known || nodes_.empty() || nodes_.back()!=from)
        { invalidate(); return status_; }

        // Arrival at a newly supplied quiet point supersedes every older anchor.
        if (to_quiet) return anchor_quiet(to);
        const auto previous=std::find(nodes_.begin(),nodes_.end(),to);
        if (previous!=nodes_.end())
        {
            // Loop erasure retains actual successful edges on the surviving suffix.
            if (!revise()) return status_;
            nodes_.erase(previous+1,nodes_.end());
            return status_;
        }
        if (nodes_.size()>=max_nodes_)
        { invalidate(); return status_; }
        if (!revise()) return status_;
        nodes_.push_back(to);
        return status_;
    }

    Status status() const { return status_; }
    std::size_t max_nodes() const { return max_nodes_; }
    std::uint64_t revision() const { return revision_; }
    std::span<const Point> nodes() const { return nodes_; }

    // Conditional storage/geometry preflight only: these future edges have not
    // happened and authorize no command. Quiet is the caller's completed model
    // premise. Limit this representative prefix to at most max_nodes edges,
    // even when future quiet resets could admit a longer physical walk.
    template<class Quiet>
    bool can_retain_prefix(std::span<const Point> ingress,Quiet quiet) const
    {
        if (max_nodes_==0 || status_!=Status::Known || nodes_.empty() || ingress.empty() ||
            ingress.size()-1>max_nodes_ || ingress.front()!=nodes_.back()) return false;
        Trail proposed=*this;
        for (std::size_t i=1;i<ingress.size();++i)
        {
            const auto from=ingress[i-1],to=ingress[i];
            if (!adjacent(from,to)) return false;
            const bool from_quiet=quiet(from),to_quiet=quiet(to);
            if (proposed.record(from,to,from_quiet,to_quiet)!=Status::Known) return false;
        }
        return true;
    }

    // Returns a candidate reverse suffix after an optional one-step prospective
    // move. Empty means unknown, not infeasible. No future occupancy, movement
    // allowance, reversal timing, actor order, camp exposure or fuel is asserted.
    std::vector<Point> make_return(Point actual,std::optional<Point> prospective=std::nullopt) const
    {
        if (status_!=Status::Known || nodes_.empty() || nodes_.back()!=actual) return {};
        std::size_t last=nodes_.size()-1;
        bool prefix=false;
        if (prospective && *prospective!=actual)
        {
            if (!adjacent(actual,*prospective)) return {};
            const auto previous=std::find(nodes_.begin(),nodes_.end(),*prospective);
            if (previous!=nodes_.end()) last=static_cast<std::size_t>(previous-nodes_.begin());
            else prefix=true;
        }
        // An unexecuted proposal does not erase the real trail, but its returned
        // suffix remains within the same finite caller-supplied node capacity.
        if (prefix && nodes_.size()>=max_nodes_) return {};
        std::vector<Point> result;
        result.reserve(last+1+(prefix?1:0));
        if (prefix) result.push_back(*prospective);
        for (std::size_t i=last+1;i>0;--i) result.push_back(nodes_[i-1]);
        return result;
    }

private:
    bool revise()
    {
        if (revision_==std::numeric_limits<std::uint64_t>::max())
        { nodes_.clear(); status_=Status::Unknown; return false; }
        ++revision_;
        return true;
    }
    std::size_t max_nodes_{};
    std::uint64_t revision_{};
    Status status_{Status::Unknown};
    std::vector<Point> nodes_; // quiet anchor -> current actual point, loop-erased
};

// One prospective ordinary edge followed by a complete return to an actual
// quiet anchor. A newly quiet landing may replace that historical suffix.
// This is geometry/storage only; current HP, occupancy and allowances are fresh.
template<class Quiet>
std::vector<Point> prefix_word(const Trail& actual,Point origin,Point destination,Quiet quiet)
{
    if (!adjacent(origin,destination)) return {};
    if (quiet(destination)) return {origin,destination};
    const auto tail=actual.make_return(origin,destination);
    if (tail.empty()) return {};
    std::vector<Point> word;word.reserve(tail.size()+1);word.push_back(origin);
    word.insert(word.end(),tail.begin(),tail.end());
    return word;
}

enum class RetentionStatus { NeedCompute, Ready, Unknown };
struct RetentionKey {
    std::uint64_t walk{},word{};
    friend bool operator==(RetentionKey,RetentionKey)=default;
};

// Complete ingress+CaptureWord storage replay, built only in a caller-owned
// quiet phase. The join point occurs ONCE in the supplied immutable word.
// word generation must bind its exact points and the completed quiet-field
// premise. This retains geometry/storage only, never future command authority.
class Retention {
public:
    Retention(const Trail& actual,std::vector<Point> complete_word,RetentionKey key,std::size_t max_points)
        : word_(std::move(complete_word)),key_(key)
    {
        if (actual.status()!=Status::Known || actual.max_nodes()==0 || actual.nodes().empty() ||
            actual.revision()!=key.walk || word_.empty() || word_.size()>max_points ||
            word_.front()!=actual.nodes().back()) return;
        // Copy actual history ONCE. Its size is bounded by actual.max_nodes()
        // (128 in the caller), independently of the staged full-word edge count.
        try { simulated_=actual; }
        catch (const std::bad_alloc&) { return; }
        catch (const std::length_error&) { return; }
        status_=word_.size()==1 ? RetentionStatus::Ready : RetentionStatus::NeedCompute;
    }

    Retention(const Retention&)=delete;
    Retention& operator=(const Retention&)=delete;
    Retention(Retention&& other) noexcept { *this=std::move(other); }
    Retention& operator=(Retention&& other) noexcept
    {
        if (this==&other) return *this;
        simulated_=std::move(other.simulated_); word_=std::move(other.word_);
        key_=other.key_; cursor_=other.cursor_; last_work_=other.last_work_; status_=other.status_;
        other.status_=RetentionStatus::Unknown;
        other.last_work_=0;
        return *this;
    }

    RetentionStatus status() const { return status_; }
    std::size_t replayed_edges() const { return cursor_; }
    std::size_t last_work() const { return last_work_; }
    bool matches(RetentionKey actual) const
    { return status_!=RetentionStatus::Unknown && key_==actual; }

    // One unit attempts one adjacent edge through the ACTUAL shared record()
    // rules, including loop erasure, quiet reset, capacity and revision overflow.
    // Per-edge record work is bounded by actual Trail capacity. The caller
    // supplies its unchanged actual walk/word generations on EVERY advance.
    template<class Quiet>
    RetentionStatus advance(RetentionKey actual,Quiet quiet,std::size_t edge_units)
    {
        last_work_=0;
        if (status_==RetentionStatus::Unknown) return status_;
        if (actual!=key_) { status_=RetentionStatus::Unknown; return status_; }
        if (status_==RetentionStatus::Ready) return status_;
        while (last_work_<edge_units)
        {
            ++last_work_;
            const auto from=word_[cursor_],to=word_[cursor_+1];
            if (!adjacent(from,to)) { status_=RetentionStatus::Unknown; return status_; }
            const bool from_quiet=quiet(from),to_quiet=quiet(to);
            if (simulated_.record(from,to,from_quiet,to_quiet)!=Status::Known)
            { status_=RetentionStatus::Unknown; return status_; }
            ++cursor_;
            if (cursor_+1==word_.size())
            { status_=RetentionStatus::Ready; return status_; }
        }
        return status_;
    }

private:
    Trail simulated_{0};
    std::vector<Point> word_;
    RetentionKey key_;
    std::size_t cursor_{},last_work_{};
    RetentionStatus status_{RetentionStatus::Unknown};
};
} // namespace tt::production_walk
