#pragma once
// Dated geometry for one NAMED quiet-frontier family. No SDK handles, epochs,
// HP, occupancy, movement allowance or action authority are retained. Callbacks
// must describe the same immutable graph/goal family on every matching-key call.
// Construction reserves bookkeeping once; advance performs no allocation and
// one unit initializes one cell, checks one goal/neighbor, or climbs one parent.
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <span>
#include <utility>
#include <vector>

namespace tt::production_recon {
struct Point { int x{},y{}; friend bool operator==(Point,Point)=default; };
struct Key {
    int group{-1};
    // body names the capability/storage family, not changing current HP. Healing
    // alone must not erase tried origins. family binds the named goal predicate.
    std::uint64_t graph{},field{},body{},family{};
    friend bool operator==(Key,Key)=default;
};
enum class MenuCause { WaitingForEligibility,MenuExhausted,Incomplete,UnknownGeometry,UnsupportedModel };
enum class Resolver { WaitEligibility,ResumeMenu,NamedQuietFrontier,ResolveGeometry,InvestmentOrOtherClass };
// WaitingForEligibility requires a caller-identified future eligibility event;
// it is never the default interpretation of a completed empty menu.
inline Resolver select_resolver(MenuCause cause) {
    switch(cause) {
    case MenuCause::WaitingForEligibility:return Resolver::WaitEligibility;
    case MenuCause::MenuExhausted:return Resolver::NamedQuietFrontier;
    case MenuCause::Incomplete:return Resolver::ResumeMenu;
    case MenuCause::UnknownGeometry:return Resolver::ResolveGeometry;
    case MenuCause::UnsupportedModel:return Resolver::InvestmentOrOtherClass;
    }
    return Resolver::ResolveGeometry;
}
// Negative-menu information obligation. Persistent exact death is nomination
// memory: loss of visibility alone cannot make that member unknown work again.
struct MissingMember {Point position;bool exact_dead{};};
enum class MissingStatus {Ready,Unknown};
struct Missing {MissingStatus status{MissingStatus::Unknown};std::uint32_t members{};};
inline Missing missing_members(int width,int height,std::span<const MissingMember> members,
    std::span<const std::uint8_t> visible) {
    if(width<=0 || height<=0 || members.empty() || members.size()>32 ||
        static_cast<std::size_t>(width)>std::numeric_limits<std::size_t>::max()/static_cast<std::size_t>(height))return {};
    const auto cells=static_cast<std::size_t>(width)*height;
    if(visible.size()!=cells/8+(cells%8!=0))return {};
    std::uint32_t missing{};
    for(std::size_t i=0;i<members.size();++i) {
        const auto p=members[i].position;
        if(p.x<0 || p.y<0 || p.x>=width || p.y>=height)return {};
        const auto index=static_cast<std::size_t>(p.y)*width+p.x;
        const bool seen=(visible[index/8]&(std::uint8_t{1}<<(index%8)))!=0;
        if(!seen && !members[i].exact_dead)missing|=std::uint32_t{1}<<i;
    }
    return {MissingStatus::Ready,missing};
}

enum class Cell { QuietOpen,Closed,Unknown };
enum class Status { NeedCompute,ReadyLeg,AtFrontier,Unsupported,Deferred,UnknownGeometry,Stale };
struct Leg { Point origin,destination,frontier; Key key; };

class Search {
public:
    Search(int width,int height,Point origin,Key key,std::size_t max_cells)
        : width_(width),height_(height) {
        if(width<=0 || height<=0 || static_cast<std::size_t>(width)>
            std::numeric_limits<std::size_t>::max()/static_cast<std::size_t>(height))return;
        cells_=static_cast<std::size_t>(width)*static_cast<std::size_t>(height);
        if(cells_>max_cells || cells_>=none || cells_>nodes_.max_size() || cells_>queue_.max_size())return;
        try { nodes_.reserve(cells_);queue_.reserve(cells_); }
        catch(const std::bad_alloc&) {return;}
        catch(const std::length_error&) {return;}
        storage_=true;
        begin_after_negative(origin,key);
    }
    Search(const Search&)=delete;
    Search& operator=(const Search&)=delete;
    Search(Search&& other) noexcept {*this=std::move(other);}
    Search& operator=(Search&& other) noexcept {
        if(this==&other)return *this;
        width_=other.width_;height_=other.height_;cells_=other.cells_;head_=other.head_;
        neighbor_=other.neighbor_;visited_=other.visited_;expanded_=other.expanded_;
        attempted_=other.attempted_;last_work_=other.last_work_;
        nodes_=std::move(other.nodes_);queue_=std::move(other.queue_);
        key_=other.key_;origin_=other.origin_;target_=other.target_;next_=other.next_;
        route_cursor_=other.route_cursor_;route_child_=other.route_child_;
        storage_=other.storage_;check_goal_=other.check_goal_;stage_=other.stage_;status_=other.status_;
        other.storage_=false;other.status_=Status::UnknownGeometry;
        return *this;
    }
    // Explicit generation reset. The starting origin has already received the
    // completed negative menu which requested this resolver, so it starts tried.
    // A new round or an unchanged failed query is never a reset reason.
    Status begin_after_negative(Point origin,Key key) {
        key_=key;origin_=origin;target_=next_=route_cursor_=route_child_=none;
        nodes_.clear();queue_.clear();head_=neighbor_=visited_=expanded_=attempted_=last_work_=0;
        check_goal_=true;stage_=Stage::Initialize;
        status_=storage_ && inside(origin) && key.group>=0?Status::NeedCompute:Status::UnknownGeometry;
        return status_;
    }
    Status status() const {return status_;}
    Key key() const {return key_;}
    Point origin() const {return origin_;}
    std::size_t visited() const {return visited_;}
    std::size_t expanded() const {return expanded_;}
    std::size_t attempted_origins() const {return attempted_;}
    std::size_t last_work() const {return last_work_;}

    template<class ReadCell,class NamedGoal>
    Status advance(Key actual,Point actual_origin,std::size_t units,ReadCell cell,NamedGoal named_goal) {
        last_work_=0;
        if(!validate(actual,actual_origin) || status_!=Status::NeedCompute)return status_;
        while(last_work_<units && status_==Status::NeedCompute) {
            ++last_work_;
            if(stage_==Stage::Initialize) {
                if(nodes_.size()<cells_)nodes_.push_back(Node{});
                else stage_=Stage::Seed;
                continue;
            }
            if(stage_==Stage::Seed) {
                if(cell(origin_)!=Cell::QuietOpen) {status_=Status::UnknownGeometry;continue;}
                const auto root=index(origin_);
                nodes_[root]={root,0,static_cast<std::uint8_t>(discovered|tried)};
                queue_.push_back(root);visited_=attempted_=1;stage_=Stage::Explore;
                continue;
            }
            if(stage_==Stage::Route) {
                const auto here=index(origin_);
                if(here==target_) {status_=Status::AtFrontier;continue;}
                if(nodes_[route_cursor_].depth>nodes_[here].depth) {
                    route_child_=route_cursor_;route_cursor_=nodes_[route_cursor_].parent;
                    continue;
                }
                next_=route_cursor_==here?route_child_:nodes_[here].parent;
                if(next_==none || next_==here)status_=Status::UnknownGeometry;
                else status_=Status::ReadyLeg;
                continue;
            }
            if(head_==queue_.size()) {status_=Status::Unsupported;continue;}
            const auto current=queue_[head_];
            const auto point=position(current);
            if(check_goal_) {
                check_goal_=false;
                if(!(nodes_[current].marks&tried) && named_goal(point)) {
                    target_=current;start_route();
                }
                continue;
            }
            if(neighbor_==directions.size()) {
                ++head_;++expanded_;neighbor_=0;check_goal_=true;continue;
            }
            const auto delta=directions[neighbor_++];
            const Point adjacent{point.x+delta.x,point.y+delta.y};
            if(!inside(adjacent))continue;
            const auto candidate=index(adjacent);
            if(nodes_[candidate].marks&discovered)continue;
            const auto kind=cell(adjacent);
            if(kind==Cell::Unknown) {status_=Status::UnknownGeometry;continue;}
            if(kind!=Cell::QuietOpen)continue;
            nodes_[candidate]={current,nodes_[current].depth+1,discovered};
            queue_.push_back(candidate);++visited_;
        }
        return status_;
    }

    std::optional<Leg> next_leg(Key actual,Point actual_origin) {
        if(!validate(actual,actual_origin) || status_!=Status::ReadyLeg)return std::nullopt;
        return Leg{origin_,position(next_),position(target_),key_};
    }
    // Only consume AFTER the fresh native move actually succeeded. This records
    // geometry, never infers that an offered edge happened or will remain free.
    bool record_applied(Key actual,Point from,Point to) {
        const auto leg=next_leg(actual,from);
        if(!leg || leg->destination!=to)return false;
        origin_=to;
        if(index(to)==target_)status_=Status::AtFrontier;
        else start_route();
        return true;
    }
    bool complete_frontier(Key actual,Point actual_origin,MenuCause cause) {
        if(!validate(actual,actual_origin) || status_!=Status::AtFrontier ||
            cause!=MenuCause::MenuExhausted || index(actual_origin)!=target_)return false;
        if(!(nodes_[target_].marks&tried)) {nodes_[target_].marks|=tried;++attempted_;}
        target_=next_=none;stage_=Stage::Explore;status_=Status::NeedCompute;
        return true;
    }
    // A dynamic refusal is not a static wall, a tried origin, or class exhaustion.
    // Resume only after the caller has a reason to recheck physical legality;
    // elapsed rounds alone do not supply that evidence.
    bool defer_leg(Key actual,Point from,Point to) {
        const auto leg=next_leg(actual,from);
        if(!leg || leg->destination!=to)return false;
        status_=Status::Deferred;return true;
    }
    bool retry_deferred(Key actual,Point actual_origin) {
        if(!validate(actual,actual_origin) || status_!=Status::Deferred)return false;
        status_=Status::ReadyLeg;return true;
    }
    // Another actual operator may relocate within the already discovered tree.
    // Preserve attempted/BFS rank; only the current geometric next leg is rebuilt.
    bool relocate(Key actual,Point actual_origin) {
        if(actual!=key_) {status_=Status::Stale;return false;}
        if(status_==Status::Stale || status_==Status::UnknownGeometry || !inside(actual_origin) || nodes_.size()!=cells_ ||
            !(nodes_[index(actual_origin)].marks&discovered))return false;
        if(actual_origin==origin_)return true;
        origin_=actual_origin;
        if(target_!=none)start_route();
        return true;
    }
private:
    static constexpr std::uint32_t none=std::numeric_limits<std::uint32_t>::max();
    static constexpr std::uint8_t discovered=1,tried=2;
    static constexpr std::array<Point,8> directions{{{0,1},{1,1},{1,0},{1,-1},{0,-1},{-1,-1},{-1,0},{-1,1}}};
    struct Node {std::uint32_t parent{none},depth{};std::uint8_t marks{};};
    enum class Stage {Initialize,Seed,Explore,Route};
    int width_{},height_{};
    std::size_t cells_{},head_{},neighbor_{},visited_{},expanded_{},attempted_{},last_work_{};
    std::vector<Node> nodes_;
    std::vector<std::uint32_t> queue_;
    Key key_;
    Point origin_;
    std::uint32_t target_{none},next_{none},route_cursor_{none},route_child_{none};
    bool storage_{},check_goal_{true};
    Stage stage_{Stage::Initialize};
    Status status_{Status::UnknownGeometry};
    bool inside(Point point) const {return point.x>=0 && point.y>=0 && point.x<width_ && point.y<height_;}
    std::uint32_t index(Point point) const {return static_cast<std::uint32_t>(static_cast<std::size_t>(point.y)*width_+point.x);}
    Point position(std::uint32_t value) const {return {static_cast<int>(value%static_cast<std::uint32_t>(width_)),static_cast<int>(value/static_cast<std::uint32_t>(width_))};}
    bool validate(Key actual,Point point) {
        if(actual!=key_ || point!=origin_)status_=Status::Stale;
        return status_!=Status::Stale && status_!=Status::UnknownGeometry;
    }
    void start_route() {
        route_cursor_=target_;route_child_=next_=none;stage_=Stage::Route;status_=Status::NeedCompute;
    }
};
} // namespace tt::production_recon
