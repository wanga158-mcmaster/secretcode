#pragma once
// SDK-free dated geometry nominations. Eight-neighbor destination legality is
// the represented graph. Unknown occupancy, future lifecycle and every actual
// command/allowance remain caller guards. No proposed path edits actual Trail.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace tt::assignment_geometry {
struct Point { int x{},y{}; friend bool operator==(Point,Point)=default; };
enum class Status { NeedCompute,Ready,Unreachable,Unsupported,Stale };
enum class Reason { None,Shape,Capacity,Number,Origin,Roots,Group,Perch,Kit,Ownership,Changed,Epoch,Path,Allocation };
struct Stamp {
    std::uint64_t geometry{},potential{},body{},query{};
    int epoch{};Point origin;
};
inline bool same_support(const Stamp& a,const Stamp& b) {
    return a.geometry==b.geometry && a.potential==b.potential && a.body==b.body &&
        a.query==b.query && a.origin==b.origin;
}
struct Limits { std::size_t cells{1u<<20},path_points{1u<<20}; };
// Consume a fresh owned Grid with std::move. No borrowed span, mutable shared
// backing store or external mutation through a retained raw pointer is admitted.
// Revisions are semantic support stamps, not permission to mutate owned inputs.
struct Grid {
    int width{},height{};
    std::vector<std::uint8_t> terrain;
    std::vector<int> camp_group; // -1 ordinary cell; every NPC spawn has its group
    std::vector<double> raw_upper; // COMPLETE all-NPC static upper at every tile
    std::vector<Point> altars,home_roots;
    bool exclusive_ownership{};
    // Immutable nomination support: filter bank GOALS only. Positive-upper
    // travel cells and every NPC spawn keep their existing graph semantics.
    bool require_quiet_bank{};
};
enum class Family { Basic,Area,Targeted };
struct Kit {
    Family family{Family::Basic};int basic_range{1},cast_range{},area_radius{};Point aim;
};
enum class PerchSupport { Supported,Unsupported,Invalid,Capacity };
inline constexpr std::size_t perch_check_capacity=32;
enum class Leg : std::size_t { Initial,Capture,Release,Bank,Renewal,Home,Redeploy,Count };
inline constexpr std::size_t leg_count=static_cast<std::size_t>(Leg::Count);
struct Step { Point point;double raw_upper{}; };
struct Path {
    std::vector<Step> points;
    std::uint32_t handoff_inputs{},edge_inputs{};
    double destination_upper{},max_point_upper{};
    bool complete{},disjoint_supported{};
};
struct Request {
    Stamp stamp;int selected_group{-1};std::vector<Point> selected_tiles;Point perch;Kit kit;
    std::array<std::uint32_t,leg_count> handoff_inputs{};
    bool disjoint_inputs_supported{},exclusive_ownership{};
};
struct Result {
    Status status{Status::NeedCompute};Reason reason{Reason::None};Stamp dated;
    Point perch,capture_tile,release_tile,bank_stand,home;
    std::array<Path,leg_count> legs;
    std::uint64_t pure_units{},progress_inputs{};
    bool conditional_occupancy{true},conditional_selected_clear{true};
    const Path& leg(Leg which) const { return legs[static_cast<std::size_t>(which)]; }
};
namespace detail {
inline constexpr std::uint32_t none=std::numeric_limits<std::uint32_t>::max();
inline constexpr std::array<Point,8> directions{{{-1,-1},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1}}};
inline std::int64_t separation(Point a,Point b) {
    const auto x=static_cast<std::int64_t>(a.x)-b.x,y=static_cast<std::int64_t>(a.y)-b.y;
    return std::max(x<0?-x:x,y<0?-y:y);
}
inline bool add_upper(double a,double b,double& out) {
    if(!std::isfinite(a)||!std::isfinite(b)||a<0||b<0)return false;
    if(a==0){out=b;return true;}if(b==0){out=a;return true;}
    out=std::nextafter(a+b,std::numeric_limits<double>::infinity());return std::isfinite(out);
}
inline bool valid_kit(const Kit& kit) {
    return (kit.family==Family::Basic||kit.family==Family::Area||kit.family==Family::Targeted)&&
        kit.basic_range>=0&&kit.cast_range>=0&&kit.area_radius>=0;
}
inline bool supports_member(const Kit& kit,Point perch,Point member) {
    if(kit.family==Family::Basic)return separation(perch,member)<=kit.basic_range;
    if(kit.family==Family::Targeted)return separation(perch,member)<=kit.cast_range;
    return separation(perch,kit.aim)<=kit.cast_range&&separation(kit.aim,member)<=kit.area_radius;
}
// One unit is one cell initialization, one root, one queue pop (<=8 endpoint
// visits), or one phase transition. Storage reserves do not initialize cells.
class Search {
public:
    void begin(std::size_t count,std::vector<std::uint32_t> roots) {
        count_=count;roots_=std::move(roots);distance_.reserve(count);parent_.reserve(count);
        root_.reserve(count);queue_.reserve(count);phase_=0;cursor_=head_=0;ready_=false;
    }
    template<class Inside,class Index,class Position,class Open>
    void unit(Inside inside,Index index,Position position,Open open) {
        if(ready_)return;
        if(phase_==0) {
            if(distance_.size()<count_){distance_.push_back(none);parent_.push_back(none);root_.push_back(none);return;}
            phase_=1;cursor_=0;return;
        }
        if(phase_==1) {
            if(cursor_<roots_.size()) {
                const auto p=roots_[cursor_++];
                if(p<count_ && open(p) && distance_[p]==none){distance_[p]=0;parent_[p]=p;root_[p]=p;queue_.push_back(p);}return;
            }
            phase_=2;return;
        }
        if(head_>=queue_.size()){ready_=true;return;}
        const auto p=queue_[head_++];const auto point=position(p);
        for(const auto direction:directions) {
            const Point next{point.x+direction.x,point.y+direction.y};
            if(!inside(next))continue;const auto child=index(next);
            if(!open(child)||distance_[child]!=none)continue;
            distance_[child]=distance_[p]+1;parent_[child]=p;root_[child]=root_[p];queue_.push_back(child);
        }
    }
    bool ready() const{return ready_;}
    std::uint32_t distance(std::uint32_t p) const{return ready_&&p<distance_.size()?distance_[p]:none;}
    std::uint32_t parent(std::uint32_t p) const{return ready_&&p<parent_.size()?parent_[p]:none;}
    std::uint32_t root(std::uint32_t p) const{return ready_&&p<root_.size()?root_[p]:none;}
private:
    std::size_t count_{},cursor_{},head_{};unsigned phase_{};bool ready_{};
    std::vector<std::uint32_t> roots_,distance_,parent_,root_,queue_;
};
} // namespace detail

// One tile-scan unit may check at most32 declared members. Capacity is explicit,
// not evidence of unreachable work. This checks only a future hit envelope:
// map/obstacles, actual allowances, target legality, spell cooldown/word prices,
// incoming and every fresh actuator guard remain separate. Targeted range is
// NEVER substituted for standing Basic range or one area center/radius.
inline PerchSupport supports_perch(const Kit& kit,std::span<const Point> selected,Point perch) {
    if(!detail::valid_kit(kit)||selected.empty()||perch.x<0||perch.y<0||
       (kit.family==Family::Area&&(kit.aim.x<0||kit.aim.y<0)))return PerchSupport::Invalid;
    if(selected.size()>perch_check_capacity)return PerchSupport::Capacity;
    for(const auto member:selected) {
        if(member.x<0||member.y<0)return PerchSupport::Invalid;
        if(!detail::supports_member(kit,perch,member))return PerchSupport::Unsupported;
    }
    return PerchSupport::Supported;
}
inline PerchSupport supports_perch(const Request& request) {
    return supports_perch(request.kit,request.selected_tiles,request.perch);
}

// Retained closed-graph fields: one actual-origin field prices every group/perch;
// bank/home multi-root fields are reusable across all of those same nominations.
class Cache {
public:
    Cache(Grid&& grid,Stamp stamp,Limits limits={}):grid_(std::move(grid)),stamp_(stamp),limits_(limits) {
        if(!grid_.exclusive_ownership){fail(Reason::Ownership);return;}
        if(grid_.width<=0||grid_.height<=0||stamp.epoch<0){fail(Reason::Shape);return;}
        const auto w=static_cast<std::size_t>(grid_.width),h=static_cast<std::size_t>(grid_.height);
        if(w>std::numeric_limits<std::size_t>::max()/h){fail(Reason::Capacity);return;}count_=w*h;
        if(count_>=detail::none||count_>limits.cells||limits.path_points==0){fail(Reason::Capacity);return;}
        if(grid_.terrain.size()!=count_||grid_.camp_group.size()!=count_||grid_.raw_upper.size()!=count_){fail(Reason::Shape);return;}
        try{altar_.reserve(count_);bank_bits_.reserve(count_);home_bits_.reserve(count_);}
        catch(const std::bad_alloc&){fail(Reason::Allocation);}catch(const std::length_error&){fail(Reason::Capacity);}
    }
    Cache(const Cache&)=delete;Cache& operator=(const Cache&)=delete;
    Cache(Cache&&)=delete;Cache& operator=(Cache&&)=delete;
    Status advance(Stamp current,std::size_t units) {
        last_work_=0;
        if(status_==Status::Unsupported||status_==Status::Stale)return status_;
        if(!same_support(stamp_,current)){status_=Status::Stale;reason_=Reason::Changed;return status_;}
        if(current.epoch<std::max(stamp_.epoch,last_epoch_)){status_=Status::Stale;reason_=Reason::Epoch;return status_;}
        last_epoch_=current.epoch;
        try{while(status_==Status::NeedCompute&&last_work_<units){++last_work_;++work_;unit();}}
        catch(const std::bad_alloc&){fail(Reason::Allocation);}catch(const std::length_error&){fail(Reason::Capacity);}
        if(last_work_&&current.epoch!=last_progress_epoch_){last_progress_epoch_=current.epoch;++progress_inputs_;}
        return status_;
    }
    // Explicit new actual-origin/query support on the SAME immutable grid and
    // potential. Bank/home fields stay cached. Every old Plan becomes stale.
    // This never runs while a previous field is incomplete, or silently binds a
    // different geometry/potential revision to old data.
    Status reorigin(Stamp next) {
        last_work_=0;
        if(status_!=Status::Ready)return status_;
        if(next.geometry!=stamp_.geometry||next.potential!=stamp_.potential){status_=Status::Stale;reason_=Reason::Changed;return status_;}
        if(next.epoch<std::max(stamp_.epoch,last_epoch_)){status_=Status::Stale;reason_=Reason::Epoch;return status_;}
        if(!inside(next.origin)||!closed(index(next.origin))){fail(Reason::Origin);return status_;}
        const bool changed=next.origin!=stamp_.origin;stamp_=next;last_epoch_=next.epoch;
        if(changed){origin_=detail::Search{};phase_=8;status_=Status::NeedCompute;}
        return status_;
    }
    Status status() const{return status_;}Reason reason() const{return reason_;}
    std::size_t last_work() const{return last_work_;}std::uint64_t work() const{return work_;}
    std::uint64_t progress_inputs() const{return progress_inputs_;}
    const Stamp& stamp() const{return stamp_;}const Grid& grid() const{return grid_;}
    std::size_t size() const{return count_;}std::size_t max_path_points() const{return std::min(count_,limits_.path_points);}
    bool inside(Point p) const{return p.x>=0&&p.y>=0&&p.x<grid_.width&&p.y<grid_.height;}
    std::uint32_t index(Point p) const{return inside(p)?static_cast<std::uint32_t>(static_cast<std::size_t>(p.y)*grid_.width+p.x):detail::none;}
    Point point(std::uint32_t p) const{return p<count_&&grid_.width>0?Point{static_cast<int>(p%static_cast<std::uint32_t>(grid_.width)),static_cast<int>(p/static_cast<std::uint32_t>(grid_.width))}:Point{-1,-1};}
    bool hard_open(std::uint32_t p) const{return p<count_&&p<altar_.size()&&!grid_.terrain[p]&&!altar_[p];}
    bool closed(std::uint32_t p) const{return hard_open(p)&&grid_.camp_group[p]<0;}
    std::uint32_t distance(Point p) const{return status_==Status::Ready&&inside(p)?origin_.distance(index(p)):detail::none;}
    const detail::Search& origin_field() const{return origin_;}
    const detail::Search& bank_field() const{return bank_;}
    const detail::Search& home_field() const{return home_;}
private:
    void fail(Reason reason){status_=Status::Unsupported;reason_=reason;}
    void search(detail::Search& value) {
        value.unit([&](Point p){return inside(p);},[&](Point p){return index(p);},[&](std::uint32_t p){return point(p);},[&](std::uint32_t p){return closed(p);});
    }
    void unit() {
        if(phase_==0) {
            if(cursor_<count_) {
                const auto p=cursor_++;
                if(grid_.terrain[p]>1||grid_.camp_group[p]<-1||!std::isfinite(grid_.raw_upper[p])||grid_.raw_upper[p]<0){fail(Reason::Number);return;}
                altar_.push_back(0);bank_bits_.push_back(0);home_bits_.push_back(0);return;
            }
            cursor_=0;phase_=1;return;
        }
        if(phase_==1) {
            if(cursor_<grid_.altars.size()){const auto p=grid_.altars[cursor_++];if(!inside(p)){fail(Reason::Roots);return;}altar_[index(p)]=1;return;}
            // Origin support is a prerequisite for every future job/root. It
            // may also be listed as a home tile; do not misclassify an actual
            // occupied origin as only a bad future home root, or seed it free.
            if(!inside(stamp_.origin)||!closed(index(stamp_.origin))){fail(Reason::Origin);return;}
            cursor_=neighbor_=0;phase_=2;return;
        }
        if(phase_==2) {
            if(cursor_<grid_.altars.size()) {
                const auto center=grid_.altars[cursor_];const auto d=detail::directions[neighbor_++];
                const Point p{center.x+d.x,center.y+d.y};
                if(inside(p)&&closed(index(p))&&(!grid_.require_quiet_bank||grid_.raw_upper[index(p)]==0))bank_bits_[index(p)]=1;
                if(neighbor_==detail::directions.size()){neighbor_=0;++cursor_;}return;
            }
            cursor_=0;phase_=3;return;
        }
        if(phase_==3) {
            if(cursor_<grid_.home_roots.size()) {
                const auto p=grid_.home_roots[cursor_++];if(!inside(p)||!closed(index(p))){fail(Reason::Roots);return;}
                home_bits_[index(p)]=1;return;
            }
            cursor_=0;phase_=4;return;
        }
        if(phase_==4) {
            if(cursor_<count_){const auto p=static_cast<std::uint32_t>(cursor_++);if(bank_bits_[p])bank_roots_.push_back(p);if(home_bits_[p])home_roots_.push_back(p);return;}
            if(!inside(stamp_.origin)||!closed(index(stamp_.origin))){fail(Reason::Origin);return;}
            if(bank_roots_.empty()||home_roots_.empty()){fail(Reason::Roots);return;}
            origin_.begin(count_,{index(stamp_.origin)});bank_.begin(count_,std::move(bank_roots_));home_.begin(count_,std::move(home_roots_));phase_=5;return;
        }
        if(phase_==5){search(origin_);if(origin_.ready())phase_=6;return;}
        if(phase_==6){search(bank_);if(bank_.ready())phase_=7;return;}
        if(phase_==7){search(home_);if(home_.ready())status_=Status::Ready;return;}
        if(phase_==8){origin_.begin(count_,{index(stamp_.origin)});phase_=5;return;}
        fail(Reason::Shape);
    }
    Grid grid_;Stamp stamp_;Limits limits_;Status status_{Status::NeedCompute};Reason reason_{Reason::None};
    std::size_t count_{},cursor_{},neighbor_{},last_work_{};unsigned phase_{};std::uint64_t work_{};
    std::uint64_t progress_inputs_{};int last_epoch_{-1},last_progress_epoch_{-1};
    std::vector<std::uint8_t> altar_,bank_bits_,home_bits_;std::vector<std::uint32_t> bank_roots_,home_roots_;
    detail::Search origin_,bank_,home_;
};

class Plan {
public:
    Plan(std::shared_ptr<const Cache> cache,Request&& request):cache_(std::move(cache)),request_(std::move(request)) {
        out_.dated=request_.stamp;out_.perch=request_.perch;
        if(!cache_||!request_.exclusive_ownership||!request_.disjoint_inputs_supported){fail(Reason::Ownership);return;}
        if(cache_->status()!=Status::Ready){fail(Reason::Shape);return;}
        if(!same_support(cache_->stamp(),request_.stamp)||request_.stamp.epoch<cache_->stamp().epoch){stale(Reason::Changed);return;}
        if(request_.selected_group<0||request_.selected_tiles.empty()){fail(Reason::Group);return;}
        if(!cache_->inside(request_.perch)||!cache_->closed(cache_->index(request_.perch))){fail(Reason::Perch);return;}
        const auto& kit=request_.kit;
        if(!detail::valid_kit(kit)||
           (kit.family==Family::Area&&(!cache_->inside(kit.aim)||detail::separation(request_.perch,kit.aim)>kit.cast_range))){fail(Reason::Kit);return;}
        try{selected_.reserve(cache_->size());trace_.reserve(cache_->max_path_points());}
        catch(const std::bad_alloc&){fail(Reason::Allocation);}catch(const std::length_error&){fail(Reason::Capacity);}
    }
    Plan(const Plan&)=delete;Plan& operator=(const Plan&)=delete;Plan(Plan&&)=delete;Plan& operator=(Plan&&)=delete;
    Status advance(Stamp current,std::size_t units) {
        last_work_=0;
        if(out_.status==Status::Unsupported||out_.status==Status::Stale)return out_.status;
        if(!same_support(request_.stamp,current)||!cache_||cache_->status()!=Status::Ready||!same_support(cache_->stamp(),request_.stamp)){stale(Reason::Changed);return out_.status;}
        if(current.epoch<std::max(request_.stamp.epoch,last_epoch_)){stale(Reason::Epoch);return out_.status;}
        last_epoch_=current.epoch;
        try{while(out_.status==Status::NeedCompute&&last_work_<units){++last_work_;++out_.pure_units;unit();}}
        catch(const std::bad_alloc&){fail(Reason::Allocation);}catch(const std::length_error&){fail(Reason::Capacity);}
        if(last_work_&&current.epoch!=last_progress_epoch_){last_progress_epoch_=current.epoch;++out_.progress_inputs;}
        return out_.status;
    }
    Status status() const{return out_.status;}std::size_t last_work() const{return last_work_;}
    const Result& result() const{return out_;}
private:
    void fail(Reason reason){out_.status=Status::Unsupported;out_.reason=reason;}
    void stale(Reason reason){out_.status=Status::Stale;out_.reason=reason;}
    bool post(std::uint32_t p) const{return cache_->closed(p)||(p<selected_.size()&&selected_[p]&&cache_->hard_open(p));}
    void search(detail::Search& value,bool after) {
        value.unit([&](Point p){return cache_->inside(p);},[&](Point p){return cache_->index(p);},[&](std::uint32_t p){return cache_->point(p);},
            [&](std::uint32_t p){return after?post(p):cache_->closed(p);});
    }
    void begin_path(std::size_t leg,const detail::Search* field,std::uint32_t target,bool reverse,std::uint32_t root) {
        leg_=leg;path_field_=field;trace_current_=target;expected_root_=root;reverse_=reverse;trace_.clear();publish_=0;path_phase_=0;
        auto& path=out_.legs[leg];path.handoff_inputs=request_.handoff_inputs[leg];path.disjoint_supported=true;
    }
    void path_unit() {
        auto& path=out_.legs[leg_];
        if(path_phase_==0) {
            if(trace_.size()>=cache_->max_path_points()){fail(Reason::Capacity);return;}
            if(trace_current_>=cache_->size()){fail(Reason::Path);return;}
            trace_.push_back(trace_current_);
            if(trace_current_==expected_root_){path_phase_=1;return;}
            const auto parent=path_field_->parent(trace_current_);
            if(parent>=cache_->size()||parent==trace_current_||path_field_->distance(parent)==detail::none||detail::separation(cache_->point(parent),cache_->point(trace_current_))!=1||
               path_field_->distance(parent)+1!=path_field_->distance(trace_current_)){fail(Reason::Path);return;}
            trace_current_=parent;return;
        }
        if(path_phase_==1){path.points.reserve(trace_.size());path_phase_=2;return;}
        if(publish_<trace_.size()) {
            const auto p=trace_[reverse_?trace_.size()-1-publish_:publish_];const auto upper=cache_->grid().raw_upper[p];
            if(publish_&&!detail::add_upper(path.destination_upper,upper,path.destination_upper)){fail(Reason::Number);return;}
            path.max_point_upper=std::max(path.max_point_upper,upper);path.points.push_back({cache_->point(p),upper});++publish_;return;
        }
        path.edge_inputs=static_cast<std::uint32_t>(path.points.size()-1);path.complete=true;next_path();
    }
    void next_path() {
        const auto perch=cache_->index(request_.perch);
        if(leg_==0){begin_path(1,&post_,capture_,true,perch);return;}
        if(leg_==1){leg_=2;path_phase_=0;return;}
        if(leg_==2){begin_path(3,&cache_->bank_field(),release_,false,bank_);return;}
        if(leg_==3){begin_path(4,&closed_,bank_,false,perch);return;}
        if(leg_==4){begin_path(5,&cache_->home_field(),bank_,false,home_);return;}
        if(leg_==5){begin_path(6,&closed_,home_,false,perch);return;}
        out_.status=Status::Ready;
    }
    void release_unit() {
        auto& path=out_.legs[2];
        if(path_phase_==0){path.points.reserve(2);path.handoff_inputs=request_.handoff_inputs[2];path.disjoint_supported=true;path_phase_=1;return;}
        if(path_phase_<=2) {
            const auto p=path_phase_==1?capture_:release_;const auto upper=cache_->grid().raw_upper[p];
            if(path_phase_==2)path.destination_upper=upper;
            path.max_point_upper=std::max(path.max_point_upper,upper);path.points.push_back({cache_->point(p),upper});++path_phase_;return;
        }
        path.edge_inputs=1;path.complete=true;next_path();
    }
    void unit() {
        if(phase_==0){if(selected_.size()<cache_->size()){selected_.push_back(0);return;}phase_=1;cursor_=0;return;}
        if(phase_==1) {
            if(cursor_<request_.selected_tiles.size()) {
                const auto p=request_.selected_tiles[cursor_++];
                if(!cache_->inside(p)){fail(Reason::Group);return;}const auto i=cache_->index(p);
                if(selected_[i]||cache_->grid().camp_group[i]!=request_.selected_group){fail(Reason::Group);return;}selected_[i]=1;
                const auto& kit=request_.kit;
                if(!detail::supports_member(kit,request_.perch,p)){fail(Reason::Kit);return;}return;
            }
            phase_=2;cursor_=0;return;
        }
        if(phase_==2) {
            if(cursor_<cache_->size()){const auto p=cursor_++;if((cache_->grid().camp_group[p]==request_.selected_group)!=(selected_[p]!=0))fail(Reason::Group);return;}
            const auto perch=cache_->index(request_.perch);
            if(cache_->distance(request_.perch)==detail::none||cache_->bank_field().distance(perch)==detail::none||cache_->home_field().distance(perch)==detail::none){out_.status=Status::Unreachable;return;}
            post_.begin(cache_->size(),{perch});closed_.begin(cache_->size(),{perch});phase_=3;return;
        }
        if(phase_==3){search(post_,true);if(post_.ready())phase_=4;return;}
        if(phase_==4){search(closed_,false);if(closed_.ready()){phase_=5;cursor_=neighbor_=0;}return;}
        if(phase_==5) {
            if(cursor_>=request_.selected_tiles.size()) {
                if(capture_==detail::none){out_.status=Status::Unreachable;return;}
                out_.capture_tile=cache_->point(capture_);out_.release_tile=cache_->point(release_);out_.bank_stand=cache_->point(bank_);out_.home=cache_->point(home_);
                begin_path(0,&cache_->origin_field(),cache_->index(request_.perch),true,cache_->index(request_.stamp.origin));phase_=6;return;
            }
            const auto target=cache_->index(request_.selected_tiles[cursor_]);const auto p=cache_->point(target);const auto d=detail::directions[neighbor_++];const Point release{p.x+d.x,p.y+d.y};
            if(neighbor_==detail::directions.size()){neighbor_=0;++cursor_;}
            if(post_.distance(target)==detail::none||!cache_->inside(release))return;
            const auto r=cache_->index(release);if(!cache_->closed(r)||closed_.distance(r)==detail::none||cache_->bank_field().distance(r)==detail::none)return;
            const auto bank=cache_->bank_field().root(r),home=cache_->home_field().root(bank);
            if(bank==detail::none||home==detail::none||closed_.distance(bank)==detail::none||closed_.distance(home)==detail::none)return;
            const std::uint64_t cost=static_cast<std::uint64_t>(post_.distance(target))+1+cache_->bank_field().distance(r);
            if(capture_==detail::none||cost<best_cost_||(cost==best_cost_&&std::pair{target,r}<std::pair{capture_,release_}))
            {best_cost_=cost;capture_=target;release_=r;bank_=bank;home_=home;}return;
        }
        if(phase_==6){if(leg_==2)release_unit();else path_unit();return;}
        fail(Reason::Shape);
    }
    std::shared_ptr<const Cache> cache_;Request request_;Result out_;std::vector<std::uint8_t> selected_;
    detail::Search post_,closed_;std::size_t cursor_{},neighbor_{},last_work_{},leg_{},publish_{};unsigned phase_{},path_phase_{};
    int last_progress_epoch_{-1},last_epoch_{-1};std::uint32_t capture_{detail::none},release_{detail::none},bank_{detail::none},home_{detail::none},trace_current_{},expected_root_{};
    std::uint64_t best_cost_{};const detail::Search* path_field_{};bool reverse_{};std::vector<std::uint32_t> trace_;
};
} // namespace tt::assignment_geometry
