#pragma once
// Sealed actor-local geometry. Static fields and target-rooted witnesses never
// become fresh self/Life or command authority. Builders alone own mutable work.
#include "assignment_geometry.h"
#include <memory>
#include <optional>

namespace tt::assignment_static_geometry {
namespace ag=assignment_geometry;
using Point=ag::Point;using Status=ag::Status;using Reason=ag::Reason;using Limits=ag::Limits;
// The static compiler admits each allocation separately. The ordinary dated
// geometry Search remains unchanged; reserve is real work even without a fill.
class Search {
public:
    void begin(std::size_t count,std::vector<std::uint32_t> roots) {
        count_=count;roots_=std::move(roots);phase_=0;cursor_=head_=0;ready_=false;
    }
    template<class Inside,class Index,class Position,class Open>
    void unit(Inside inside,Index index,Position position,Open open) {
        if(ready_)return;
        if(phase_==0){distance_.reserve(count_);++phase_;return;}
        if(phase_==1){parent_.reserve(count_);++phase_;return;}
        if(phase_==2){root_.reserve(count_);++phase_;return;}
        if(phase_==3){queue_.reserve(count_);++phase_;return;}
        if(phase_==4) {
            if(distance_.size()<count_){distance_.push_back(ag::detail::none);parent_.push_back(ag::detail::none);root_.push_back(ag::detail::none);return;}
            phase_=5;cursor_=0;return;
        }
        if(phase_==5) {
            if(cursor_<roots_.size()) {
                const auto p=roots_[cursor_++];
                if(p<count_&&open(p)&&distance_[p]==ag::detail::none){distance_[p]=0;parent_[p]=p;root_[p]=p;queue_.push_back(p);}return;
            }
            phase_=6;return;
        }
        if(head_>=queue_.size()){ready_=true;return;}
        const auto p=queue_[head_++];const auto point=position(p);
        for(const auto direction:ag::detail::directions) {
            const Point next{point.x+direction.x,point.y+direction.y};
            if(!inside(next))continue;const auto child=index(next);
            if(!open(child)||distance_[child]!=ag::detail::none)continue;
            distance_[child]=distance_[p]+1;parent_[child]=p;root_[child]=root_[p];queue_.push_back(child);
        }
    }
    bool ready() const{return ready_;}
    std::uint32_t distance(std::uint32_t p) const{return ready_&&p<distance_.size()?distance_[p]:ag::detail::none;}
    std::uint32_t parent(std::uint32_t p) const{return ready_&&p<parent_.size()?parent_[p]:ag::detail::none;}
    std::uint32_t root(std::uint32_t p) const{return ready_&&p<root_.size()?root_[p]:ag::detail::none;}
private:
    std::size_t count_{},cursor_{},head_{};unsigned phase_{};bool ready_{};
    std::vector<std::uint32_t> roots_,distance_,parent_,root_,queue_;
};
struct StaticKey {
    std::uint64_t map{},layout{},catalogue{},roots{},graph_policy{},potential{};
    bool require_quiet_bank{};
    bool require_quiet_home{};
    friend bool operator==(const StaticKey&,const StaticKey&)=default;
};
struct CatalogMember {Point pos;float max_hp{},strength{};int range{};};
struct CatalogGroup {int canonical{-1};std::vector<CatalogMember> members;};
struct StaticSeed {
    StaticKey key;ag::Grid grid;std::vector<CatalogGroup> catalogue;
    // Caller binds the complete fixed-position source envelope to potential.
    // No current Life/sleep/shield/mitigation may discount this raw field.
    bool complete_potential{};
    // Actual World terrain already includes declared altars. Retain every
    // blocked bit; this only distinguishes its representation at validation.
    bool terrain_includes_altars{};
    bool require_quiet_home{};
};
class StaticBuilder;class OriginBuilder;class TargetBuilder;
class StaticGeometry {
public:
    const StaticKey& key() const{return key_;}
    const ag::Grid& grid() const{return grid_;}
    const std::vector<CatalogGroup>& catalogue() const{return catalogue_;}
    std::size_t size() const{return count_;}
    std::size_t max_path_points() const{return std::min(count_,limits_.path_points);}
    bool inside(Point p) const{return p.x>=0&&p.y>=0&&p.x<grid_.width&&p.y<grid_.height;}
    std::uint32_t index(Point p) const{return inside(p)?static_cast<std::uint32_t>(static_cast<std::size_t>(p.y)*grid_.width+p.x):ag::detail::none;}
    Point point(std::uint32_t i) const{return i<count_?Point{static_cast<int>(i%static_cast<unsigned>(grid_.width)),static_cast<int>(i/static_cast<unsigned>(grid_.width))}:Point{-1,-1};}
    bool hard_open(std::uint32_t i) const{return i<count_&&!grid_.terrain[i]&&!altar_[i];}
    bool closed(std::uint32_t i) const{return hard_open(i)&&grid_.camp_group[i]<0;}
    std::optional<double> raw_upper(Point p) const {const auto i=index(p);return i<count_?std::optional<double>{grid_.raw_upper[i]}:std::nullopt;}
    const Search& bank_field() const{return bank_;}
    const Search& home_field() const{return home_;}
private:
    friend class StaticBuilder;
    StaticGeometry()=default;
    StaticKey key_;ag::Grid grid_;std::vector<CatalogGroup> catalogue_;Limits limits_;
    std::size_t count_{};std::vector<std::uint8_t> altar_,bank_bits_,home_bits_;
    Search bank_,home_;
};
class StaticBuilder {
public:
    explicit StaticBuilder(StaticSeed&&,Limits limits={});
    StaticBuilder(const StaticBuilder&)=delete;StaticBuilder& operator=(const StaticBuilder&)=delete;
    StaticBuilder(StaticBuilder&&)=delete;StaticBuilder& operator=(StaticBuilder&&)=delete;
    Status advance(std::size_t units);
    Status status() const{return status_;}Reason reason() const{return reason_;}
    std::size_t last_work() const{return last_work_;}
    std::uint64_t work_used() const{return work_;}
    std::shared_ptr<const StaticGeometry> result() const{return status_==Status::Ready?data_:nullptr;}
private:
    void fail(Reason reason){status_=Status::Unsupported;reason_=reason;}
    void unit();
    std::shared_ptr<StaticGeometry> data_;
    Status status_{Status::NeedCompute};Reason reason_{Reason::None};
    unsigned phase_{};std::size_t cursor_{},neighbor_{},group_{},member_{},last_work_{};
    bool terrain_includes_altars_{};
    std::uint64_t work_{};std::array<std::size_t,31> counts_{};
    std::vector<std::uint32_t> bank_roots_,home_roots_;
};
enum class OriginMode { Field,BindOnly };
class OriginView {
public:
    const ag::Stamp& stamp() const{return stamp_;}
    const StaticKey& static_key() const{return static_->key();}
    const std::shared_ptr<const StaticGeometry>& static_geometry() const{return static_;}
    bool has_field() const{return static_cast<bool>(field_);}
    std::uint32_t distance(Point p) const{return field_?field_->distance(static_->index(p)):ag::detail::none;}
    const Search* origin_field() const{return field_.get();}
private:
    friend class OriginBuilder;
    OriginView()=default;
    std::shared_ptr<const StaticGeometry> static_;ag::Stamp stamp_;
    std::shared_ptr<const Search> field_;
};
class OriginBuilder {
public:
    OriginBuilder(std::shared_ptr<const StaticGeometry>,ag::Stamp,
        OriginMode mode=OriginMode::Field,std::shared_ptr<const OriginView> reuse={});
    OriginBuilder(const OriginBuilder&)=delete;OriginBuilder& operator=(const OriginBuilder&)=delete;
    OriginBuilder(OriginBuilder&&)=delete;OriginBuilder& operator=(OriginBuilder&&)=delete;
    Status advance(ag::Stamp current,std::size_t units);
    Status status() const{return status_;}Reason reason() const{return reason_;}
    std::size_t last_work() const{return last_work_;}
    std::shared_ptr<const OriginView> result() const{return status_==Status::Ready?view_:nullptr;}
private:
    void fail(Reason reason){status_=Status::Unsupported;reason_=reason;}
    std::shared_ptr<OriginView> view_;std::shared_ptr<Search> work_;
    Status status_{Status::NeedCompute};Reason reason_{Reason::None};
    std::size_t last_work_{};int last_epoch_{-1};
};
// Static/target identity excludes own body, query, origin and round. Exact
// object identity additionally prevents an unrelated grid from aliasing a key.
struct TargetKey {
    StaticKey static_key;int group{-1};Point perch;ag::Kit kit;
    std::array<std::uint32_t,ag::leg_count> handoff_inputs{};
    std::uint64_t capture_policy{1},members{};
};
enum class TargetMode { WithInitial,FutureOnly };
class TargetGeometry {
public:
    const ag::Result& result() const{return result_;}
    const TargetKey& key() const{return key_;}
    const StaticKey& static_key() const{return static_->key();}
    const std::shared_ptr<const StaticGeometry>& static_geometry() const{return static_;}
    const ag::Stamp& stamp() const{return result_.dated;}
    int group() const{return key_.group;}
    const ag::Kit& kit() const{return key_.kit;}
    Point perch() const{return key_.perch;}
    // FutureOnly certifies six reusable future legs and the perch field. Its
    // absent Initial is never a physical programme; fresh binding must build it.
    bool future_only() const{return future_only_;}
    bool has_initial() const{return !future_only_&&result_.legs[0].complete;}
    const std::vector<Point>& selected_tiles() const{return selected_;}
    std::optional<double> raw_upper(Point p) const{return static_->raw_upper(p);}
    std::uint32_t distance_to_perch(Point p) const{return closed_->distance(static_->index(p));}
    std::optional<Point> next_to_perch(Point p) const;
private:
    friend class TargetBuilder;
    TargetGeometry()=default;
    TargetKey key_;ag::Result result_;std::vector<Point> selected_;
    std::shared_ptr<const StaticGeometry> static_;
    std::shared_ptr<const Search> closed_;
    bool future_only_{};
};
class TargetBuilder {
public:
    TargetBuilder(std::shared_ptr<const OriginView>,ag::Request&&,TargetMode mode=TargetMode::WithInitial);
    TargetBuilder(std::shared_ptr<const StaticGeometry>,ag::Request&&,TargetMode);
    TargetBuilder(const TargetBuilder&)=delete;TargetBuilder& operator=(const TargetBuilder&)=delete;
    TargetBuilder(TargetBuilder&&)=delete;TargetBuilder& operator=(TargetBuilder&&)=delete;
    Status advance(ag::Stamp current,std::size_t units);
    Status status() const{return status_;}Reason reason() const{return reason_;}
    std::size_t last_work() const{return last_work_;}
    std::shared_ptr<const TargetGeometry> result() const{return status_==Status::Ready?out_:nullptr;}
private:
    void fail(Reason reason){status_=Status::Unsupported;reason_=reason;}
    void initialize(std::shared_ptr<const StaticGeometry>,ag::Request&&,TargetMode);
    void unit();void begin_path(std::size_t,const Search*,std::uint32_t,bool,std::uint32_t);
    void path_unit();void next_path();void release_unit();
    std::shared_ptr<const OriginView> origin_;std::shared_ptr<TargetGeometry> out_;
    std::shared_ptr<Search> closed_;Search post_;
    Status status_{Status::NeedCompute};Reason reason_{Reason::None};
    std::size_t cursor_{},neighbor_{},last_work_{},leg_{},publish_{};unsigned phase_{},path_phase_{};
    std::uint32_t capture_{ag::detail::none},release_{ag::detail::none},bank_{ag::detail::none},home_{ag::detail::none},trace_current_{},expected_root_{};
    std::uint64_t best_cost_{};const Search* path_field_{};bool reverse_{};
    std::vector<std::uint32_t> trace_;int last_epoch_{-1},last_progress_epoch_{-1};
};
inline StaticBuilder::StaticBuilder(StaticSeed&& seed,Limits limits) {
    if(!seed.complete_potential||!seed.grid.exclusive_ownership){fail(Reason::Ownership);return;}
    if(!seed.key.map||!seed.key.layout||!seed.key.catalogue||!seed.key.roots||!seed.key.potential||
       seed.key.graph_policy!=1||seed.key.require_quiet_bank!=seed.grid.require_quiet_bank||
       seed.key.require_quiet_home!=seed.require_quiet_home)
        {fail(Reason::Changed);return;}
    if(seed.grid.width<=0||seed.grid.height<=0||seed.catalogue.empty()||seed.catalogue.size()>31)
        {fail(Reason::Shape);return;}
    const auto w=static_cast<std::size_t>(seed.grid.width),h=static_cast<std::size_t>(seed.grid.height);
    if(w>std::numeric_limits<std::size_t>::max()/h){fail(Reason::Capacity);return;}
    const auto n=w*h;
    if(n>=ag::detail::none||n>limits.cells||!limits.path_points){fail(Reason::Capacity);return;}
    if(seed.grid.terrain.size()!=n||seed.grid.camp_group.size()!=n||seed.grid.raw_upper.size()!=n)
        {fail(Reason::Shape);return;}
    try {
        data_=std::shared_ptr<StaticGeometry>(new StaticGeometry);
        data_->key_=seed.key;data_->grid_=std::move(seed.grid);data_->catalogue_=std::move(seed.catalogue);
        data_->count_=n;data_->limits_=limits;
        terrain_includes_altars_=seed.terrain_includes_altars;phase_=8;
    } catch(const std::bad_alloc&) {fail(Reason::Allocation);}
      catch(const std::length_error&) {fail(Reason::Capacity);}
}
inline Status StaticBuilder::advance(std::size_t units) {
    last_work_=0;
    try {while(status_==Status::NeedCompute&&last_work_<units){++last_work_;++work_;unit();}}
    catch(const std::bad_alloc&){fail(Reason::Allocation);}catch(const std::length_error&){fail(Reason::Capacity);}
    return status_;
}
inline void StaticBuilder::unit() {
    auto& d=*data_;const auto n=d.count_;
    if(phase_==8){d.altar_.reserve(n);phase_=9;return;}
    if(phase_==9){d.bank_bits_.reserve(n);phase_=10;return;}
    if(phase_==10){d.home_bits_.reserve(n);phase_=0;return;}
    if(phase_==0) {
        if(cursor_<n) {
            const auto i=cursor_++;const auto group=d.grid_.camp_group[i];
            if(d.grid_.terrain[i]>1||group<-1||group>=static_cast<int>(d.catalogue_.size())||
               !std::isfinite(d.grid_.raw_upper[i])||d.grid_.raw_upper[i]<0){fail(Reason::Number);return;}
            if(group>=0)++counts_[static_cast<std::size_t>(group)];
            d.altar_.push_back(0);d.bank_bits_.push_back(0);d.home_bits_.push_back(0);return;
        }
        phase_=1;cursor_=0;return;
    }
    if(phase_==1) {
        if(group_>=d.catalogue_.size()){phase_=2;cursor_=0;return;}
        const auto& group=d.catalogue_[group_];
        if(group.canonical!=static_cast<int>(group_)||group.members.empty()||
           group.members.size()>ag::perch_check_capacity||group.members.size()!=counts_[group_])
            {fail(Reason::Group);return;}
        if(member_<group.members.size()) {
            const auto& m=group.members[member_];const auto i=d.index(m.pos);
            if(!d.inside(m.pos)||d.grid_.camp_group[i]!=group.canonical||d.grid_.terrain[i]||
               !std::isfinite(m.max_hp)||m.max_hp<=0||!std::isfinite(m.strength)||m.strength<0||m.range<0)
                {fail(Reason::Group);return;}
            for(std::size_t j=0;j<member_;++j)if(group.members[j].pos==m.pos){fail(Reason::Group);return;}
            if(d.grid_.raw_upper[i]<m.strength){fail(Reason::Number);return;}
            ++member_;return;
        }
        ++group_;member_=0;return;
    }
    if(phase_==2) {
        if(cursor_<d.grid_.altars.size()) {
            const auto p=d.grid_.altars[cursor_++];if(!d.inside(p)){fail(Reason::Roots);return;}
            const auto i=d.index(p);if(d.grid_.camp_group[i]>=0||(!terrain_includes_altars_&&d.grid_.terrain[i])){fail(Reason::Roots);return;}
            d.altar_[i]=1;return;
        }
        phase_=3;cursor_=neighbor_=0;return;
    }
    if(phase_==3) {
        if(cursor_<d.grid_.altars.size()) {
            const auto center=d.grid_.altars[cursor_];const auto delta=ag::detail::directions[neighbor_++];
            const Point p{center.x+delta.x,center.y+delta.y};const auto i=d.index(p);
            if(d.inside(p)&&d.closed(i)&&(!d.key_.require_quiet_bank||d.grid_.raw_upper[i]==0))d.bank_bits_[i]=1;
            if(neighbor_==ag::detail::directions.size()){neighbor_=0;++cursor_;}return;
        }
        phase_=4;cursor_=0;return;
    }
    if(phase_==4) {
        if(cursor_<d.grid_.home_roots.size()) {
            const auto p=d.grid_.home_roots[cursor_++];const auto i=d.index(p);
            if(!d.inside(p)||!d.closed(i)){fail(Reason::Roots);return;}
            if(!d.key_.require_quiet_home||d.grid_.raw_upper[i]==0)d.home_bits_[i]=1;return;
        }
        phase_=5;cursor_=0;return;
    }
    if(phase_==5) {
        if(cursor_<n) {const auto i=static_cast<std::uint32_t>(cursor_++);
            if(d.bank_bits_[i])bank_roots_.push_back(i);if(d.home_bits_[i])home_roots_.push_back(i);return;}
        if(bank_roots_.empty()||home_roots_.empty()){fail(Reason::Roots);return;}
        d.bank_.begin(n,std::move(bank_roots_));d.home_.begin(n,std::move(home_roots_));phase_=6;return;
    }
    auto search=[&](Search& field) {
        field.unit([&](Point p){return d.inside(p);},[&](Point p){return d.index(p);},
            [&](std::uint32_t i){return d.point(i);},[&](std::uint32_t i){return d.closed(i);});
    };
    if(phase_==6){search(d.bank_);if(d.bank_.ready())phase_=7;return;}
    if(phase_==7){search(d.home_);if(d.home_.ready())status_=Status::Ready;return;}
    fail(Reason::Shape);
}
inline OriginBuilder::OriginBuilder(std::shared_ptr<const StaticGeometry> data,ag::Stamp stamp,
    OriginMode mode,std::shared_ptr<const OriginView> reuse) {
    if(!data||stamp.epoch<0||stamp.geometry!=data->key().layout||stamp.potential!=data->key().potential)
        {fail(Reason::Changed);return;}
    if(!data->inside(stamp.origin)||!data->closed(data->index(stamp.origin))){fail(Reason::Origin);return;}
    try {
        view_=std::shared_ptr<OriginView>(new OriginView);view_->static_=std::move(data);view_->stamp_=stamp;last_epoch_=stamp.epoch;
        if(mode==OriginMode::BindOnly){status_=Status::Ready;return;}
        if(reuse&&reuse->static_.get()==view_->static_.get()&&reuse->stamp_.origin==stamp.origin&&reuse->field_)
            {view_->field_=reuse->field_;status_=Status::Ready;return;}
        work_=std::make_shared<Search>();work_->begin(view_->static_->size(),{view_->static_->index(stamp.origin)});
    } catch(const std::bad_alloc&) {fail(Reason::Allocation);}
      catch(const std::length_error&) {fail(Reason::Capacity);}
}
inline Status OriginBuilder::advance(ag::Stamp current,std::size_t units) {
    last_work_=0;if(status_==Status::Unsupported||status_==Status::Stale)return status_;
    if(!view_||!ag::same_support(view_->stamp_,current)||current.epoch<last_epoch_)
        {status_=Status::Stale;reason_=Reason::Changed;return status_;}
    last_epoch_=current.epoch;
    try {
        const auto& d=*view_->static_;
        while(status_==Status::NeedCompute&&last_work_<units) {
            ++last_work_;work_->unit([&](Point p){return d.inside(p);},[&](Point p){return d.index(p);},
                [&](std::uint32_t i){return d.point(i);},[&](std::uint32_t i){return d.closed(i);});
            if(work_->ready()){view_->field_=work_;status_=Status::Ready;}
        }
    } catch(const std::bad_alloc&) {fail(Reason::Allocation);}
      catch(const std::length_error&) {fail(Reason::Capacity);}
    return status_;
}
inline std::optional<Point> TargetGeometry::next_to_perch(Point p) const {
    const auto i=static_->index(p);const auto distance=closed_->distance(i);
    if(distance==ag::detail::none||distance==0)return std::nullopt;
    const auto parent=closed_->parent(i);const auto next=static_->point(parent);
    if(!static_->closed(i)||!static_->closed(parent)||ag::detail::separation(p,next)!=1||
       closed_->distance(parent)==ag::detail::none||closed_->distance(parent)+1!=distance)return std::nullopt;
    return next;
}
inline TargetBuilder::TargetBuilder(std::shared_ptr<const OriginView> origin,ag::Request&& request,TargetMode mode) {
    if(!origin||!request.exclusive_ownership||!request.disjoint_inputs_supported){fail(Reason::Ownership);return;}
    if(!ag::same_support(origin->stamp(),request.stamp)||request.stamp.epoch<origin->stamp().epoch)
        {status_=Status::Stale;reason_=Reason::Changed;return;}
    origin_=std::move(origin);initialize(origin_->static_geometry(),std::move(request),mode);
}
inline TargetBuilder::TargetBuilder(std::shared_ptr<const StaticGeometry> geometry,ag::Request&& request,TargetMode mode) {
    if(mode!=TargetMode::FutureOnly){fail(Reason::Origin);return;}
    initialize(std::move(geometry),std::move(request),mode);
}
inline void TargetBuilder::initialize(std::shared_ptr<const StaticGeometry> geometry,ag::Request&& request,TargetMode mode) {
    if(!geometry||!request.exclusive_ownership||!request.disjoint_inputs_supported){fail(Reason::Ownership);return;}
    if(mode!=TargetMode::WithInitial&&mode!=TargetMode::FutureOnly){fail(Reason::Shape);return;}
    const auto& data=*geometry;
    if(request.stamp.geometry!=data.key().layout||request.stamp.potential!=data.key().potential||request.stamp.epoch<0)
        {status_=Status::Stale;reason_=Reason::Changed;return;}
    if(request.selected_group<0||static_cast<std::size_t>(request.selected_group)>=data.catalogue().size()||
       request.selected_tiles.empty()||request.selected_tiles.size()>ag::perch_check_capacity||
       request.selected_tiles.size()!=data.catalogue()[static_cast<std::size_t>(request.selected_group)].members.size())
        {fail(Reason::Group);return;}
    if(!data.inside(request.perch)||!data.closed(data.index(request.perch))||!ag::detail::valid_kit(request.kit)||
       (request.kit.family==ag::Family::Area&&(!data.inside(request.kit.aim)||
        ag::detail::separation(request.perch,request.kit.aim)>request.kit.cast_range)))
        {fail(Reason::Perch);return;}
    try {
        out_=std::shared_ptr<TargetGeometry>(new TargetGeometry);
        out_->future_only_=mode==TargetMode::FutureOnly;
        out_->static_=std::move(geometry);out_->result_.dated=request.stamp;out_->result_.perch=request.perch;
        out_->key_={data.key(),request.selected_group,request.perch,request.kit,request.handoff_inputs,1,1469598103934665603ull};
        out_->selected_=std::move(request.selected_tiles);last_epoch_=request.stamp.epoch;
        closed_=std::make_shared<Search>();trace_.reserve(data.max_path_points());
    } catch(const std::bad_alloc&) {fail(Reason::Allocation);}
      catch(const std::length_error&) {fail(Reason::Capacity);}
}
inline Status TargetBuilder::advance(ag::Stamp current,std::size_t units) {
    last_work_=0;if(status_==Status::Unsupported||status_==Status::Stale||status_==Status::Unreachable)return status_;
    if(!out_||current.geometry!=out_->static_key().layout||current.potential!=out_->static_key().potential||
       current.epoch<last_epoch_){status_=Status::Stale;reason_=Reason::Changed;return status_;}
    // Body/query/origin are not static-target dependencies. The retained result
    // keeps its ORIGINAL stamp/Initial; fresh binding must trace a NEW Initial.
    last_epoch_=current.epoch;
    try {
        while(status_==Status::NeedCompute&&last_work_<units) {
            ++last_work_;++out_->result_.pure_units;unit();
        }
    } catch(const std::bad_alloc&){fail(Reason::Allocation);}catch(const std::length_error&){fail(Reason::Capacity);}
    if(last_work_&&current.epoch!=last_progress_epoch_){last_progress_epoch_=current.epoch;++out_->result_.progress_inputs;}
    return status_;
}
inline void TargetBuilder::begin_path(std::size_t leg,const Search* field,
    std::uint32_t target,bool reverse,std::uint32_t root) {
    leg_=leg;path_field_=field;trace_current_=target;reverse_=reverse;expected_root_=root;
    trace_.clear();publish_=0;path_phase_=0;
}
inline void TargetBuilder::path_unit() {
    const auto& data=*out_->static_;auto& path=out_->result_.legs[leg_];
    if(path_phase_==0) {
        if(trace_current_>=data.size()||path_field_->distance(trace_current_)==ag::detail::none||
           path_field_->root(trace_current_)!=expected_root_||trace_.size()>=data.max_path_points())
            {fail(Reason::Path);return;}
        trace_.push_back(trace_current_);
        if(trace_current_==expected_root_) {
            if(path_field_->distance(trace_current_)!=0){fail(Reason::Path);return;}
            path.points.reserve(trace_.size());path.handoff_inputs=out_->key_.handoff_inputs[leg_];
            path.disjoint_supported=true;path_phase_=1;return;
        }
        const auto parent=path_field_->parent(trace_current_);
        if(parent>=data.size()||parent==trace_current_||path_field_->distance(parent)==ag::detail::none||
           path_field_->distance(parent)+1!=path_field_->distance(trace_current_)||
           ag::detail::separation(data.point(parent),data.point(trace_current_))!=1)
            {fail(Reason::Path);return;}
        trace_current_=parent;return;
    }
    if(publish_<trace_.size()) {
        const auto i=reverse_?trace_[trace_.size()-1-publish_]:trace_[publish_];++publish_;
        const auto raw=data.grid().raw_upper[i];path.points.push_back({data.point(i),raw});
        path.max_point_upper=std::max(path.max_point_upper,raw);
        if(publish_>1) {
            double total{};if(!ag::detail::add_upper(path.destination_upper,raw,total)){fail(Reason::Number);return;}
            path.destination_upper=total;
        }
        return;
    }
    path.edge_inputs=static_cast<std::uint32_t>(path.points.size()-1);path.complete=true;next_path();
}
inline void TargetBuilder::next_path() {
    const auto& data=*out_->static_;const auto perch=data.index(out_->key_.perch);
    if(leg_==0){begin_path(1,&post_,capture_,true,perch);return;}
    if(leg_==1){leg_=2;path_phase_=0;return;}
    if(leg_==2){begin_path(3,&data.bank_field(),release_,false,bank_);return;}
    if(leg_==3){begin_path(4,closed_.get(),bank_,false,perch);return;}
    if(leg_==4){begin_path(5,&data.home_field(),bank_,false,home_);return;}
    if(leg_==5){begin_path(6,closed_.get(),home_,false,perch);return;}
    out_->closed_=closed_;out_->result_.status=Status::Ready;out_->result_.reason=Reason::None;status_=Status::Ready;
}
inline void TargetBuilder::release_unit() {
    auto& path=out_->result_.legs[2];const auto& data=*out_->static_;
    if(path_phase_==0){path.points.reserve(2);path.handoff_inputs=out_->key_.handoff_inputs[2];path.disjoint_supported=true;path_phase_=1;return;}
    if(path_phase_<=2) {
        const auto i=path_phase_==1?capture_:release_;const auto raw=data.grid().raw_upper[i];
        path.points.push_back({data.point(i),raw});path.max_point_upper=std::max(path.max_point_upper,raw);
        path.destination_upper=raw;++path_phase_;return;
    }
    path.edge_inputs=1;path.complete=true;next_path();
}
inline void TargetBuilder::unit() {
    const auto& data=*out_->static_;const auto perch=data.index(out_->key_.perch);
    if(phase_==0) {
        if(cursor_<out_->selected_.size()) {
            const auto p=out_->selected_[cursor_];const auto i=data.index(p);
            if(!data.inside(p)||data.grid().camp_group[i]!=out_->key_.group||
               !ag::detail::supports_member(out_->key_.kit,out_->key_.perch,p))
                {fail(Reason::Group);return;}
            for(std::size_t j=0;j<cursor_;++j)if(out_->selected_[j]==p){fail(Reason::Group);return;}
            for(const auto coordinate:{p.x,p.y}) {
                out_->key_.members^=static_cast<std::uint32_t>(coordinate);out_->key_.members*=1099511628211ull;
            }
            ++cursor_;return;
        }
        post_.begin(data.size(),{perch});closed_->begin(data.size(),{perch});phase_=1;return;
    }
    auto search=[&](Search& field,bool post) {
        field.unit([&](Point p){return data.inside(p);},[&](Point p){return data.index(p);},
            [&](std::uint32_t i){return data.point(i);},[&](std::uint32_t i){return data.closed(i)||
                (post&&data.hard_open(i)&&data.grid().camp_group[i]==out_->key_.group);});
    };
    if(phase_==1){search(post_,true);if(post_.ready())phase_=2;return;}
    if(phase_==2){search(*closed_,false);if(closed_->ready()){phase_=3;cursor_=neighbor_=0;}return;}
    if(phase_==3) {
        if(cursor_>=out_->selected_.size()) {
            if(capture_==ag::detail::none||(!out_->future_only_&&
               closed_->distance(data.index(origin_->stamp().origin))==ag::detail::none))
                {status_=Status::Unreachable;return;}
            auto& result=out_->result_;result.capture_tile=data.point(capture_);result.release_tile=data.point(release_);
            result.bank_stand=data.point(bank_);result.home=data.point(home_);
            if(out_->future_only_)begin_path(1,&post_,capture_,true,perch);
            else begin_path(0,closed_.get(),data.index(origin_->stamp().origin),false,perch);
            phase_=4;return;
        }
        const auto point=out_->selected_[cursor_];const auto target=data.index(point);
        const auto direction=ag::detail::directions[neighbor_++];const Point release{point.x+direction.x,point.y+direction.y};
        if(neighbor_==ag::detail::directions.size()){neighbor_=0;++cursor_;}
        if(post_.distance(target)==ag::detail::none||!data.inside(release))return;
        const auto r=data.index(release);if(!data.closed(r)||closed_->distance(r)==ag::detail::none||data.bank_field().distance(r)==ag::detail::none)return;
        const auto bank=data.bank_field().root(r),home=data.home_field().root(bank);
        if(bank==ag::detail::none||home==ag::detail::none||closed_->distance(bank)==ag::detail::none||closed_->distance(home)==ag::detail::none)return;
        const auto cost=static_cast<std::uint64_t>(post_.distance(target))+1+data.bank_field().distance(r);
        if(capture_==ag::detail::none||cost<best_cost_||(cost==best_cost_&&std::pair{target,r}<std::pair{capture_,release_}))
            {best_cost_=cost;capture_=target;release_=r;bank_=bank;home_=home;}return;
    }
    if(phase_==4){if(leg_==2)release_unit();else path_unit();return;}
    fail(Reason::Shape);
}
} // namespace tt::assignment_static_geometry
