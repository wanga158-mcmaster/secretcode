#pragma once
// Immutable cash-goal fields and complete raw suffix prices. These are static
// nominations, never fresh body/Life/occupancy, actual Trail or command permits.
// Every reserve, cell and tree propagation is owned by Builder before Ready;
// a charged input only looks up already sealed suffix sums and maxima.
#include "assignment_static_geometry.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace tt::assignment_cash_routes {
namespace sg=assignment_static_geometry;
namespace ag=assignment_geometry;
using Point=sg::Point;
enum class Goal : unsigned {Bank,Home,Perch};
enum class Status {NeedCompute,Ready,Unreachable,Unsupported,Stale};
enum class Reason {None,Shape,Capacity,Goal,HomeHazard,Origin,Path,Number,Allocation,Changed};
struct Goals {
    Point bank,home,perch;std::uint64_t bank_id{},home_id{},perch_id{};
    friend bool operator==(const Goals&,const Goals&)=default;
};
struct Key {
    const sg::StaticGeometry* object{};sg::StaticKey support;Goals goals;
    friend bool operator==(const Key&,const Key&)=default;
};
struct Limits {std::size_t cells{1u<<20},points{128};};
struct Quote {
    Status status{Status::NeedCompute};Reason reason{Reason::None};Key key;
    std::shared_ptr<const sg::StaticGeometry> support_owner; // Keeps the key's exact static object alive.
    Goal goal{Goal::Bank};Point origin,destination;std::optional<Point> first_step;
    std::uint32_t remaining_channel_inputs{},forward_edges{},home_edges{};
    std::uint64_t points{};
    double raw_forward{},raw_channel{},raw_home{},raw_max{},raw_reserve{},raw_required{};
    bool complete() const{return status==Status::Ready;}
    // A raw comparison only. No shield, sleep/death, damage already paid or
    // unobserved mitigation is credited. The caller still rechecks real HP,
    // actual incoming modifiers, epoch/pose and every physical dependency.
    bool survives(double current_hp) const {
        return complete()&&std::isfinite(current_hp)&&current_hp>raw_required;
    }
};
namespace detail {
inline std::optional<std::size_t> index(Goal g) {
    const auto i=static_cast<unsigned>(g);return i<3?std::optional<std::size_t>{i}:std::nullopt;
}
inline Point point(const Goals& g,Goal which) {return which==Goal::Bank?g.bank:which==Goal::Home?g.home:g.perch;}
inline bool upper_add(double a,double b,double& out){return ag::detail::add_upper(a,b,out);}
inline bool upper_mul(double raw,std::uint32_t inputs,double& out) {
    if(!std::isfinite(raw)||raw<0)return false;
    if(!raw||!inputs){out=0;return true;}
    out=std::nextafter(raw*static_cast<double>(inputs),std::numeric_limits<double>::infinity());
    return std::isfinite(out);
}
}
class Builder;
class Routes {
public:
    const Key& key() const{return key_;}
    const Goals& goals() const{return key_.goals;}
    const std::shared_ptr<const sg::StaticGeometry>& static_geometry() const{return geometry_;}
    bool matches(const Key& current) const{return current==key_;}
    // A closed origin follows one sealed parent. A real catalogue spawn may
    // nominate ONE adjacent closed first edge; an arbitrary blocked tile may
    // not borrow that exception. At most eight neighbours are inspected.
    std::optional<std::uint32_t> distance(Goal goal,Point origin) const;
    std::optional<Point> next(Goal goal,Point origin) const;
    Quote price(Goal goal,Point origin,std::uint32_t remaining_channel_inputs) const
    {return price(goal,origin,remaining_channel_inputs,limits_);}
    Quote price(Goal goal,Point origin,std::uint32_t remaining_channel_inputs,Limits limits) const;
    Quote price(const Key& current,Goal goal,Point origin,std::uint32_t remaining_channel_inputs) const
    {return price(current,goal,origin,remaining_channel_inputs,limits_);}
    Quote price(const Key& current,Goal goal,Point origin,std::uint32_t remaining_channel_inputs,Limits limits) const {
        if(!matches(current)){Quote q;q.key=key_;q.support_owner=geometry_;q.goal=goal;q.origin=origin;q.status=Status::Stale;q.reason=Reason::Changed;return q;}
        return price(goal,origin,remaining_channel_inputs,limits);
    }
private:
    friend class Builder;
    Routes()=default;
    struct Cost {std::vector<double> sum,maximum;};
    struct Start {
        Status status{Status::Unsupported};Reason reason{Reason::Origin};
        std::uint32_t node{ag::detail::none},distance{};std::optional<Point> next;
        double charge{},maximum{};
    };
    Key key_;Limits limits_;std::shared_ptr<const sg::StaticGeometry> geometry_;
    std::array<sg::Search,3> fields_;std::array<Cost,3> costs_;
    Start start(Goal,Point) const;
};
class Builder {
public:
    Builder(std::shared_ptr<const sg::StaticGeometry> geometry,Goals goals,Limits limits={})
        : geometry_(std::move(geometry)),limits_(limits) {
        if(!geometry_||!limits_.cells||!limits_.points){fail(Reason::Shape);return;}
        key_={geometry_.get(),geometry_->key(),goals};
        if(geometry_->size()>limits_.cells){fail(Reason::Capacity);return;}
        if(!goals.bank_id||!goals.home_id||!goals.perch_id){fail(Reason::Goal);return;}
        for(const auto goal:{Goal::Bank,Goal::Home,Goal::Perch}) {
            const auto p=detail::point(goals,goal);const auto i=geometry_->index(p);
            if(!geometry_->inside(p)||!geometry_->closed(i)){fail(Reason::Goal);return;}
        }
        if(geometry_->bank_field().distance(geometry_->index(goals.bank))!=0||
           geometry_->home_field().distance(geometry_->index(goals.home))!=0){fail(Reason::Goal);return;}
        const auto home=geometry_->raw_upper(goals.home);
        if(!home||*home!=0){fail(Reason::HomeHazard);return;}
    }
    Builder(const Builder&)=delete;Builder& operator=(const Builder&)=delete;
    Builder(Builder&&)=delete;Builder& operator=(Builder&&)=delete;
    const Key& key() const{return key_;}
    Status advance(std::size_t units){return advance(key_,units);}
    Status advance(const Key& current,std::size_t units) {
        last_work_=0;
        if(current!=key_){status_=Status::Stale;reason_=Reason::Changed;return status_;}
        try {
            while(status_==Status::NeedCompute&&last_work_<units){++last_work_;++work_;unit();}
        } catch(const std::bad_alloc&){fail(Reason::Allocation);}
          catch(const std::length_error&){fail(Reason::Capacity);}
        return status_;
    }
    Status status() const{return status_;}Reason reason() const{return reason_;}
    std::size_t last_work() const{return last_work_;}std::uint64_t work_used() const{return work_;}
    std::shared_ptr<const Routes> result() const{return status_==Status::Ready?data_:nullptr;}
private:
    enum class Phase {Allocate,BeginSearch,Search,ReserveSum,ReserveMax,ReserveQueue,Cells,Root,Tree,Done};
    std::shared_ptr<const sg::StaticGeometry> geometry_;Limits limits_;Key key_;
    std::shared_ptr<Routes> data_;std::vector<std::uint32_t> queue_;
    Status status_{Status::NeedCompute};Reason reason_{Reason::None};Phase phase_{Phase::Allocate};
    std::size_t field_{},cell_{},head_{},last_work_{};std::uint64_t work_{};
    void fail(Reason reason){status_=Status::Unsupported;reason_=reason;}
    void unit();
};
inline void Builder::unit() {
    const auto& g=*geometry_;const auto n=g.size();
    if(phase_==Phase::Allocate) {
        data_=std::shared_ptr<Routes>(new Routes);data_->geometry_=geometry_;data_->key_=key_;data_->limits_=limits_;
        phase_=Phase::BeginSearch;return;
    }
    if(phase_==Phase::BeginSearch) {
        const auto root=g.index(detail::point(key_.goals,static_cast<Goal>(field_)));
        data_->fields_[field_].begin(n,{root});phase_=Phase::Search;return;
    }
    auto& search=data_->fields_[field_];
    if(phase_==Phase::Search) {
        search.unit([&](Point p){return g.inside(p);},[&](Point p){return g.index(p);},
            [&](std::uint32_t i){return g.point(i);},[&](std::uint32_t i){return g.closed(i);});
        if(search.ready()){if(++field_<3)phase_=Phase::BeginSearch;else{field_=0;phase_=Phase::ReserveSum;}}return;
    }
    auto& cost=data_->costs_[field_];
    if(phase_==Phase::ReserveSum){cost.sum.reserve(n);phase_=Phase::ReserveMax;return;}
    if(phase_==Phase::ReserveMax){cost.maximum.reserve(n);phase_=Phase::ReserveQueue;return;}
    if(phase_==Phase::ReserveQueue){queue_.reserve(n);queue_.clear();cell_=head_=0;phase_=Phase::Cells;return;}
    if(phase_==Phase::Cells) {
        if(cell_<n){++cell_;cost.sum.push_back(std::numeric_limits<double>::infinity());cost.maximum.push_back(0);return;}
        phase_=Phase::Root;return;
    }
    if(phase_==Phase::Root) {
        const auto root=g.index(detail::point(key_.goals,static_cast<Goal>(field_)));
        if(search.distance(root)!=0||search.root(root)!=root){fail(Reason::Path);return;}
        cost.sum[root]=0;cost.maximum[root]=g.grid().raw_upper[root];queue_.push_back(root);phase_=Phase::Tree;return;
    }
    if(phase_==Phase::Tree) {
        if(head_>=queue_.size()) {
            if(++field_<3){phase_=Phase::ReserveSum;return;}
            phase_=Phase::Done;status_=Status::Ready;return;
        }
        const auto parent=queue_[head_++];const auto p=g.point(parent);
        if(!std::isfinite(cost.sum[parent])||!std::isfinite(cost.maximum[parent])){fail(Reason::Number);return;}
        for(const auto d:ag::detail::directions) {
            const Point next{p.x+d.x,p.y+d.y};if(!g.inside(next))continue;
            const auto child=g.index(next);
            if(!g.closed(child)||search.parent(child)!=parent)continue;
            if(search.distance(child)==ag::detail::none||search.distance(parent)+1!=search.distance(child)||
               search.root(child)!=search.root(parent)||std::isfinite(cost.sum[child])){fail(Reason::Path);return;}
            if(!detail::upper_add(cost.sum[parent],g.grid().raw_upper[parent],cost.sum[child])){fail(Reason::Number);return;}
            cost.maximum[child]=std::max(g.grid().raw_upper[child],cost.maximum[parent]);queue_.push_back(child);
        }
        return;
    }
    fail(Reason::Shape);
}
inline Routes::Start Routes::start(Goal goal,Point origin) const {
    Start out;const auto which=detail::index(goal);if(!which){out.reason=Reason::Goal;return out;}
    const auto& g=*geometry_;if(!g.inside(origin))return out;
    const auto i=g.index(origin);const auto& search=fields_[*which];const auto& cost=costs_[*which];
    if(g.closed(i)) {
        const auto distance=search.distance(i);
        if(distance==ag::detail::none){out.status=Status::Unreachable;out.reason=Reason::Path;return out;}
        if(i>=cost.sum.size()||!std::isfinite(cost.sum[i])||!std::isfinite(cost.maximum[i])){out.reason=Reason::Path;return out;}
        out.node=i;out.distance=distance;out.charge=cost.sum[i];out.maximum=cost.maximum[i];
        if(distance) {
            const auto parent=search.parent(i);
            if(!g.closed(parent)||search.distance(parent)+1!=distance){out.reason=Reason::Path;return out;}
            out.next=g.point(parent);
        }
        out.status=Status::Ready;out.reason=Reason::None;return out;
    }
    if(!g.hard_open(i)||g.grid().camp_group[i]<0)return out;
    std::uint32_t best=ag::detail::none,best_distance=ag::detail::none;
    for(const auto d:ag::detail::directions) {
        const Point next{origin.x+d.x,origin.y+d.y};if(!g.inside(next))continue;
        const auto child=g.index(next);if(!g.closed(child))continue;
        const auto distance=search.distance(child);
        if(distance<best_distance){best=child;best_distance=distance;}
    }
    if(best==ag::detail::none||best_distance>=ag::detail::none-1){out.status=Status::Unreachable;out.reason=Reason::Path;return out;}
    if(best>=cost.sum.size()||!std::isfinite(cost.sum[best])||!std::isfinite(cost.maximum[best])||
       !detail::upper_add(g.grid().raw_upper[best],cost.sum[best],out.charge)){out.reason=Reason::Number;return out;}
    out.node=best;out.distance=best_distance+1;out.maximum=std::max(g.grid().raw_upper[i],cost.maximum[best]);
    out.next=g.point(best);out.status=Status::Ready;out.reason=Reason::None;return out;
}
inline std::optional<std::uint32_t> Routes::distance(Goal goal,Point origin) const {
    const auto s=start(goal,origin);return s.status==Status::Ready?std::optional<std::uint32_t>{s.distance}:std::nullopt;
}
inline std::optional<Point> Routes::next(Goal goal,Point origin) const {
    const auto s=start(goal,origin);return s.status==Status::Ready?s.next:std::nullopt;
}
inline Quote Routes::price(Goal goal,Point origin,std::uint32_t remaining,Limits limits) const {
    Quote q;q.key=key_;q.support_owner=geometry_;q.goal=goal;q.origin=origin;q.destination=detail::point(key_.goals,goal);
    if(!limits.points||geometry_->size()>limits.cells){q.status=Status::Unsupported;q.reason=Reason::Capacity;return q;}
    const auto forward=start(goal,origin);q.status=forward.status;q.reason=forward.reason;
    if(forward.status!=Status::Ready)return q;
    q.forward_edges=forward.distance;q.first_step=forward.next;q.raw_forward=forward.charge;q.raw_max=forward.maximum;
    if(goal!=Goal::Home) {
        const auto home=start(Goal::Home,q.destination);
        if(home.status!=Status::Ready){q.status=home.status;q.reason=home.reason;return q;}
        q.home_edges=home.distance;q.raw_home=home.charge;q.raw_max=std::max(q.raw_max,home.maximum);
    }
    q.points=static_cast<std::uint64_t>(q.forward_edges)+q.home_edges+1;
    if(q.points>std::min(limits.points,limits_.points)){q.status=Status::Unsupported;q.reason=Reason::Capacity;return q;}
    if(goal==Goal::Bank) {
        q.remaining_channel_inputs=remaining;
        const auto raw=geometry_->raw_upper(key_.goals.bank);
        if(!raw||!detail::upper_mul(*raw,remaining,q.raw_channel)){q.status=Status::Unsupported;q.reason=Reason::Number;return q;}
    }
    // Two future observation waits plus one possible refused command, each at
    // the largest remaining source upper. No already incurred prefix is paid
    // again, and the actual origin is included only in this future reserve.
    double sum{};
    if(!detail::upper_mul(q.raw_max,3,q.raw_reserve)||
       !detail::upper_add(q.raw_forward,q.raw_channel,sum)||
       !detail::upper_add(sum,q.raw_home,sum)||!detail::upper_add(sum,q.raw_reserve,q.raw_required))
    {q.status=Status::Unsupported;q.reason=Reason::Number;return q;}
    q.status=Status::Ready;q.reason=Reason::None;return q;
}
} // namespace tt::assignment_cash_routes
