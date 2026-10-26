#pragma once
// Retained PURE charge construction for one complete caller-supplied word:
// ingress -> frontier waits -> reverse ingress/actual tail -> quiet terminal.
// Geometry beyond adjacency, route identity, Trail capacity, complete fixed-source
// field, and its generation binding remain caller premises. Bounds cover real
// sums of raw potential, not native float32 HP-loss rounding or fuel duration.
// No HP, epoch, allowance, visibility, SDK handle or command authority is stored.
#include "production_potential.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace tt::production_transit {
using Point=mb::production::potential::Point;
using Status=mb::production::potential::Status;
using Value=mb::production::potential::Value;
using Field=mb::production::potential::Field;

struct Key {
    int group{-1};
    std::uint64_t graph{},field{},walk{};
    friend bool operator==(Key,Key)=default;
};
struct Leg { Point origin,destination; double required_hp{}; };

namespace detail {
inline bool adjacent(Point from,Point to)
{
    const auto dx=static_cast<std::int64_t>(from.x)-to.x;
    const auto dy=static_cast<std::int64_t>(from.y)-to.y;
    return dx>=-1 && dx<=1 && dy>=-1 && dy<=1 && (dx!=0 || dy!=0);
}
inline bool multiply_upper(double value,unsigned count,double& upper)
{
    if (!std::isfinite(value) || value<0) return false;
    if (value==0 || count==0) { upper=0; return true; }
    if (count==1) { upper=value; return true; }
    const double product=value*static_cast<double>(count);
    if (!std::isfinite(product)) return false;
    upper=std::nextafter(product,std::numeric_limits<double>::infinity());
    return std::isfinite(upper);
}
} // namespace detail

class Ledger {
public:
    // Move the already constructed word into this bounded family. Reservations
    // allocate uninitialized capacity; charge/bound entries are appended only by
    // advance. The word's reverse-tail identity/capacity proof is caller-owned.
    Ledger(std::vector<Point> wholeword,std::size_t frontier_index,unsigned waits,Key key,
           std::size_t max_points)
        : word_(std::move(wholeword)),frontier_(frontier_index),waits_(waits),key_(key)
    {
        if (word_.empty() || max_points==0 || word_.size()>max_points || frontier_>=word_.size() ||
            word_.size()>charges_.max_size() || frontier_+1>remaining_.max_size()) return;
        try { charges_.reserve(word_.size()); remaining_.reserve(frontier_+1); }
        catch (const std::bad_alloc&) { return; }
        catch (const std::length_error&) { return; }
        status_=Status::NeedCompute;
    }

    Ledger(const Ledger&)=delete;
    Ledger& operator=(const Ledger&)=delete;
    Ledger(Ledger&& other) noexcept { *this=std::move(other); }
    Ledger& operator=(Ledger&& other) noexcept
    {
        if (this==&other) return *this;
        word_=std::move(other.word_); charges_=std::move(other.charges_); remaining_=std::move(other.remaining_);
        frontier_=other.frontier_; cursor_=other.cursor_; waits_=other.waits_; key_=other.key_;
        fixed_return_=other.fixed_return_; maximum_=other.maximum_; last_work_=other.last_work_;
        status_=other.status_;
        other.invalidate();
        return *this;
    }

    Status status() const { return status_; }
    std::size_t last_work() const { return last_work_; }
    bool matches(Key key) const { return status_!=Status::Unknown && key_==key; }
    Key key() const { return key_; }
    void invalidate() { status_=Status::Unknown; last_work_=0; }

    // The same completed immutable Field bound by key.field must be supplied on
    // every call. Its generation is external: Field has no generation API.
    // One unit gathers/checks one word point OR appends one remaining bound.
    // Publication is part of the final bound unit, never a partial Ready result.
    Status advance(const Field& field,std::size_t units)
    {
        last_work_=0;
        if (status_!=Status::NeedCompute) return status_;
        if (field.status()==Status::Unknown) { status_=Status::Unknown; return status_; }
        if (field.status()!=Status::Ready) return status_;
        while (last_work_<units)
        {
            ++last_work_;
            if (charges_.size()<word_.size())
            {
                const auto index=charges_.size();
                if (index && !detail::adjacent(word_[index-1],word_[index]))
                { status_=Status::Unknown; return status_; }
                const auto charge=field.at(word_[index]);
                if (charge.status!=Status::Ready || !std::isfinite(charge.upper) || charge.upper<0 ||
                    (index+1==word_.size() && charge.upper!=0))
                { status_=Status::Unknown; return status_; }
                charges_.push_back(charge.upper);
                maximum_=std::max(maximum_,charge.upper);
                if (index>frontier_ && !add(fixed_return_,charge.upper,fixed_return_))
                { status_=Status::Unknown; return status_; }
                continue;
            }
            double bound{};
            if (remaining_.empty())
            {
                double wait_charge{};
                if (!detail::multiply_upper(charges_[frontier_],waits_,wait_charge) ||
                    !add(fixed_return_,wait_charge,bound) || !add(bound,maximum_,bound))
                { status_=Status::Unknown; return status_; }
            }
            else
            {
                const auto ingress_index=frontier_-remaining_.size();
                if (!add(remaining_.back(),charges_[ingress_index+1],bound))
                { status_=Status::Unknown; return status_; }
            }
            remaining_.push_back(bound); // frontier, frontier-1, ..., current zero
            if (remaining_.size()==frontier_+1)
            { status_=Status::Ready; return status_; }
        }
        return status_;
    }

    std::optional<Point> current_origin() const
    { return status_==Status::Ready ? std::optional<Point>{word_[cursor_]} : std::nullopt; }
    std::optional<Point> next_destination() const
    { return status_==Status::Ready && cursor_<frontier_ ? std::optional<Point>{word_[cursor_+1]} : std::nullopt; }
    bool at_frontier() const { return status_==Status::Ready && cursor_==frontier_; }
    Value required_hp() const
    {
        if (status_!=Status::Ready) return {};
        return {Status::Ready,remaining_[frontier_-cursor_]};
    }

    // Shared caller gate before fresh SDK authority. This checks only the
    // caller-supplied identity/current-Trail premises and the complete pure
    // record; current HP, epoch, flags and physical point legality remain fresh
    // adapter obligations. A rejected request never mutates accounting.
    std::optional<Leg> leg(Key actual,Point actual_origin,bool actual_trail_known) const
    {
        if (!actual_trail_known || status_!=Status::Ready || !matches(actual) || at_frontier())
            return std::nullopt;
        const auto origin=current_origin(),destination=next_destination();
        const auto required=required_hp();
        if (!origin || !destination || *origin!=actual_origin || required.status!=Status::Ready ||
            !std::isfinite(required.upper) || required.upper<0) return std::nullopt;
        return Leg{*origin,*destination,required.upper};
    }

    // Call only AFTER the real absolute ordinary move and actual Trail.record.
    // This consumes accounting, not an action: fresh origin/HP/allowances/epoch,
    // refusal/displacement handling and strict HP>bound remain caller obligations.
    bool consume_applied(Point actual_from,Point actual_to,Key next)
    {
        if (status_!=Status::Ready || cursor_>=frontier_ || word_[cursor_]!=actual_from ||
            word_[cursor_+1]!=actual_to || key_.group!=next.group || key_.graph!=next.graph ||
            key_.field!=next.field || next.walk<=key_.walk)
        { invalidate(); return false; }
        ++cursor_;
        key_=next;
        return true;
    }

private:
    static bool add(double a,double b,double& out)
    { return mb::production::potential::detail::add_upper(a,b,out); }
    std::vector<Point> word_;
    std::vector<double> charges_,remaining_;
    std::size_t frontier_{},cursor_{},last_work_{};
    unsigned waits_{};
    Key key_;
    double fixed_return_{},maximum_{};
    Status status_{Status::Unknown};
};

struct Rejection { Point first; double required_hp{}; float observed_hp{}; };

// Retains ONLY completed HP rejections within one caller-declared finite first-
// endpoint family. It is neither a selector nor a global route/job exclusion.
// The start HP/rejection ceiling just preserves the validity of past failures;
// it is never current HP or permission to act. No epoch or SDK state is retained.
// A first endpoint names a UNIQUE priced word only under the caller's retained
// exact route, origin, history, target and field bindings; no whole word is copied.
class Probe {
public:
    Probe(Key key,Point origin,float start_hp,std::vector<Point> first_endpoints)
        : key_(key),origin_(origin),hp_ceiling_(start_hp),endpoints_(std::move(first_endpoints)),
          valid_(std::isfinite(start_hp) && start_hp>0)
    {
        if (!valid_) return;
        if (endpoints_.size()>rejections_.max_size()) { valid_=false; return; }
        try { rejections_.reserve(endpoints_.size()); }
        catch (const std::bad_alloc&) { valid_=false; }
        catch (const std::length_error&) { valid_=false; }
    }

    Probe(const Probe&)=delete;
    Probe& operator=(const Probe&)=delete;
    Probe(Probe&& other) noexcept { *this=std::move(other); }
    Probe& operator=(Probe&& other) noexcept
    {
        if (this==&other) return *this;
        key_=other.key_; origin_=other.origin_; hp_ceiling_=other.hp_ceiling_;
        endpoints_=std::move(other.endpoints_); rejections_=std::move(other.rejections_);
        cursor_=other.cursor_; valid_=other.valid_;
        other.valid_=false;
        return *this;
    }

    bool matches(Key actual,Point origin,float current_hp) const
    {
        return valid_ && key_==actual && origin_==origin && std::isfinite(current_hp) &&
            current_hp>0 && current_hp<=hp_ceiling_;
    }
    std::optional<Point> next() const
    {
        if (!valid_ || cursor_>=endpoints_.size()) return std::nullopt;
        return endpoints_[cursor_];
    }
    bool exhausted() const { return valid_ && cursor_==endpoints_.size(); }
    std::span<const Rejection> rejections() const
    { return valid_ ? std::span<const Rejection>{rejections_} : std::span<const Rejection>{}; }

    // Caller must first establish current key/origin and a COMPLETE Ledger leg;
    // the double alone cannot carry completion provenance. Unknown model, budget
    // or capacity results must never call this transition. Rejecting is allowed
    // only when the actual latest positive HP fails the completed strict HP>bound.
    bool reject_completed(Point first,double required,float actual_fresh_hp)
    {
        const auto endpoint=next();
        if (!endpoint || *endpoint!=first || !std::isfinite(required) || required<0 ||
            !std::isfinite(actual_fresh_hp) || actual_fresh_hp<=0 ||
            actual_fresh_hp>hp_ceiling_ || static_cast<double>(actual_fresh_hp)>required) return false;
        rejections_.push_back({first,required,actual_fresh_hp});
        ++cursor_;
        hp_ceiling_=actual_fresh_hp;
        return true;
    }

private:
    Key key_;
    Point origin_;
    float hp_ceiling_{};
    std::vector<Point> endpoints_;
    std::vector<Rejection> rejections_;
    std::size_t cursor_{};
    bool valid_{};
};

} // namespace tt::production_transit
