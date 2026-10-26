#include "residual_routes.h"
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace residual::routes {
namespace {
constexpr std::uint32_t unreachable=std::numeric_limits<std::uint32_t>::max();
constexpr std::uint8_t no_parent=255;
// Deterministic expansion order N, NE, E, SE, S, SW, W, NW. Seed order is
// caller root order; equal-length ties retain the first discovery. Diagonal
// edges are allowed directly; there is no extra corner-cut restriction.
constexpr int dx[8]={0,1,1,1,0,-1,-1,-1};
constexpr int dy[8]={-1,-1,0,1,1,1,0,-1};
bool inside(const Grid& g,Point p) noexcept {return p.x>=0&&p.y>=0&&p.x<g.width&&p.y<g.height;}
std::size_t index(const Grid& g,Point p) noexcept {return static_cast<std::size_t>(p.y)*static_cast<std::size_t>(g.width)+static_cast<std::size_t>(p.x);}
Point point(const Grid& g,std::uint32_t i) noexcept {return {static_cast<int>(i%static_cast<std::uint32_t>(g.width)),static_cast<int>(i/static_cast<std::uint32_t>(g.width))};}
}

std::optional<std::uint32_t> RouteField::distance(Point origin) const noexcept {
    if(!ready_||!grid_||!inside(*grid_,origin))return std::nullopt;
    const auto d=distance_[index(*grid_,origin)];
    return d==unreachable?std::nullopt:std::optional<std::uint32_t>{d};
}
std::optional<Point> RouteField::next(Point origin) const noexcept {
    if(!ready_||!grid_||!inside(*grid_,origin))return std::nullopt;
    const auto i=index(*grid_,origin),direction=static_cast<std::size_t>(parent_[i]);
    if(distance_[i]==unreachable||direction>=8)return std::nullopt;
    return Point{origin.x+dx[direction],origin.y+dy[direction]};
}

Builder::Builder(std::shared_ptr<const Grid> grid,std::vector<Point> roots) noexcept
    :grid_(std::move(grid)),roots_(std::move(roots)) {
    if(!grid_||grid_->width<=0||grid_->height<=0){fail(Reason::InvalidGrid);return;}
    const auto w=static_cast<std::size_t>(grid_->width),h=static_cast<std::size_t>(grid_->height);
    if(w>(static_cast<std::size_t>(unreachable)-1)/h){fail(Reason::InvalidGrid);return;}
    cells_=w*h;
    if(grid_->blocked.size()!=cells_){fail(Reason::InvalidGrid);return;}
    if(roots_.empty())fail(Reason::NoRoots);
}
Cost Builder::next_cost() const noexcept {
    if(status_!=Status::NeedCompute)return Cost::None;
    switch(phase_){
    case Phase::Reserve:return Cost::Allocate;
    case Phase::Initialize:return Cost::Cell;
    case Phase::Roots:return Cost::Root;
    case Phase::Search:return Cost::Pop;
    default:return Cost::None;
    }
}
std::shared_ptr<const RouteField> Builder::result() const noexcept {
    return status_==Status::Ready?field_:std::shared_ptr<const RouteField>{};
}
void Builder::fail(Reason reason) noexcept {
    status_=Status::Unsupported;phase_=Phase::Failed;reason_=reason;
    // Failure never publishes partially initialized storage. Destruction or
    // a later caller reset releases the builder's private allocations.
}
void Builder::advance(std::size_t units) noexcept {
    last_work_=0;
    if(status_==Status::NeedCompute&&!grid_){fail(Reason::InvalidGrid);return;}
    while(status_==Status::NeedCompute&&last_work_<units){
        ++last_work_;
        try {step();}
        catch(const std::bad_alloc&) {fail(Reason::Allocation);}
        catch(const std::length_error&) {fail(Reason::Allocation);}
    }
}
void Builder::step() {
    if(phase_==Phase::Reserve){
        switch(reserve_step_){
        case 0:field_=std::make_shared<RouteField>();field_->grid_=grid_;break;
        case 1:field_->distance_.reserve(cells_);break;
        case 2:field_->parent_.reserve(cells_);break;
        case 3:queue_.reserve(cells_);break;
        default:fail(Reason::Allocation);return;
        }
        if(++reserve_step_==4)phase_=Phase::Initialize;
        return;
    }
    if(phase_==Phase::Initialize){
        field_->distance_.push_back(unreachable);field_->parent_.push_back(no_parent);
        if(++initialized_==cells_)phase_=Phase::Roots;
        return;
    }
    if(phase_==Phase::Roots){
        const Point p=roots_[root_cursor_++];
        if(inside(*grid_,p)){
            const auto i=index(*grid_,p);
            if(!grid_->blocked[i]&&field_->distance_[i]==unreachable){
                field_->distance_[i]=0;queue_.push_back(static_cast<std::uint32_t>(i));roots_[root_count_++]=p;
            }
        }
        if(root_cursor_==roots_.size()){
            roots_.resize(root_count_);
            if(!root_count_){fail(Reason::NoRoots);return;}
            field_->roots_=std::move(roots_);phase_=Phase::Search;
        }
        return;
    }
    if(phase_==Phase::Search){
        const auto id=queue_[head_++];const auto p=point(*grid_,id);
        for(std::uint8_t d=0;d<8;++d){
            const Point q{p.x+dx[d],p.y+dy[d]};
            if(!inside(*grid_,q))continue;
            const auto i=index(*grid_,q);
            if(grid_->blocked[i]||field_->distance_[i]!=unreachable)continue;
            field_->distance_[i]=field_->distance_[id]+1;
            field_->parent_[i]=static_cast<std::uint8_t>((d+4)%8);
            queue_.push_back(static_cast<std::uint32_t>(i));
        }
        if(head_==queue_.size()){
            field_->ready_=true;phase_=Phase::Done;status_=Status::Ready;
        }
    }
}
}
