#pragma once
// SDK-free staged capture of the actor's immutable World/potential. The caller
// supplies only a completed bootstrap World and COMPLETE all-NPC raw field;
// those objects must stay immutable throughout this builder's lifetime.
#include "assignment_static_geometry.h"
#include <bit>
#include <span>

namespace tt::assignment_static_world {
namespace sg=assignment_static_geometry;
namespace ag=assignment_geometry;
using Status=ag::Status;using Reason=ag::Reason;

inline void mix(std::uint64_t& hash,std::uint64_t value) {
    hash^=value;hash*=1099511628211ull;
}
inline std::uint64_t nonzero(std::uint64_t value) {return value?value:1;}
struct Policy {bool require_quiet_bank{true},require_quiet_home{};};
template<class World,class Potential> class Builder {
public:
    Builder(const World& world,const Potential& potential,std::span<const int> canonical,
        std::uint64_t potential_revision,std::vector<std::uint8_t>&& terrain,
        sg::Limits limits={},Policy policy={}) : world_(&world),potential_(&potential),revision_(potential_revision),
        terrain_(std::move(terrain)),limits_(limits) {
        if(world.width<=0||world.height<=0||!revision_||canonical.empty()||canonical.size()>31||
           canonical.size()!=world.groups.size()) {fail(Reason::Shape);return;}
        const auto width=static_cast<std::size_t>(world.width),height=static_cast<std::size_t>(world.height);
        if(width>std::numeric_limits<std::size_t>::max()/height) {fail(Reason::Capacity);return;}
        count_=width*height;
        if(count_>limits.cells||count_>=ag::detail::none) {fail(Reason::Capacity);return;}
        packed_=terrain_.size()==count_/8+(count_%8!=0);
        if(!packed_&&terrain_.size()!=count_) {fail(Reason::Shape);return;}
        std::array<bool,31> used{};
        for(std::size_t i=0;i<canonical.size();++i) {
            const auto local=canonical[i];
            if(local<0||static_cast<std::size_t>(local)>=canonical.size()||used[static_cast<std::size_t>(local)])
                {fail(Reason::Group);return;}
            order_[i]=local;used[static_cast<std::size_t>(local)]=true;
        }
        groups_=canonical.size();
        seed_.grid.width=world.width;seed_.grid.height=world.height;
        seed_.grid.exclusive_ownership=true;seed_.grid.require_quiet_bank=policy.require_quiet_bank;
        seed_.require_quiet_home=policy.require_quiet_home;
        seed_.complete_potential=true;
        seed_.terrain_includes_altars=true;
        mix(layout_,width);mix(layout_,height);mix(catalogue_,groups_);mix(field_,revision_);
    }
    Status advance(std::uint64_t current_revision,std::size_t units) {
        last_work_=0;
        if(current_revision!=revision_&&(status_==Status::NeedCompute||status_==Status::Ready)) {
            status_=Status::Stale;reason_=Reason::Changed;ready_.reset();return status_;
        }
        try {
            while(status_==Status::NeedCompute&&last_work_<units) {
                ++last_work_;++work_;unit();
            }
        } catch(const std::bad_alloc&) {fail(Reason::Allocation);}
          catch(const std::length_error&) {fail(Reason::Capacity);}
        return status_;
    }
    Status status() const{return status_;}Reason reason() const{return reason_;}
    std::size_t last_work() const{return last_work_;}std::uint64_t work_used() const{return work_;}
    std::shared_ptr<const sg::StaticGeometry> result() const{return status_==Status::Ready?ready_:nullptr;}
private:
    enum class Phase { ReserveTerrain,ReserveCamps,ReserveRaw,ReserveCatalogue,Cells,Groups,Members,Altars,Homes,Seal,Fields };
    const World* world_;const Potential* potential_;std::uint64_t revision_{};
    std::vector<std::uint8_t> terrain_;std::array<int,31> order_{};
    sg::StaticSeed seed_;sg::Limits limits_;std::unique_ptr<sg::StaticBuilder> builder_;
    std::shared_ptr<const sg::StaticGeometry> ready_;
    Status status_{Status::NeedCompute};Reason reason_{Reason::None};Phase phase_{Phase::ReserveTerrain};
    std::size_t count_{},groups_{},cell_{},group_{},member_{},root_{},last_work_{};
    bool packed_{};std::uint64_t work_{};
    std::uint64_t layout_{1469598103934665603ull},catalogue_{1469598103934665603ull},
        roots_{1469598103934665603ull},field_{1469598103934665603ull};
    void fail(Reason reason) {status_=Status::Unsupported;reason_=reason;}
    void unit() {
        const auto& world=*world_;
        if(phase_==Phase::ReserveTerrain){seed_.grid.terrain.reserve(count_);phase_=Phase::ReserveCamps;return;}
        if(phase_==Phase::ReserveCamps){seed_.grid.camp_group.reserve(count_);phase_=Phase::ReserveRaw;return;}
        if(phase_==Phase::ReserveRaw){seed_.grid.raw_upper.reserve(count_);phase_=Phase::ReserveCatalogue;return;}
        if(phase_==Phase::ReserveCatalogue){seed_.catalogue.reserve(groups_);phase_=Phase::Cells;return;}
        if(phase_==Phase::Cells) {
            if(cell_<count_) {
                const auto tile=cell_++;const ag::Point point{static_cast<int>(tile%world.width),static_cast<int>(tile/world.width)};
                const auto upper=potential_->at({point.x,point.y});
                if(upper.status!=decltype(upper.status)::Ready||!std::isfinite(upper.upper)||upper.upper<0)
                    {fail(Reason::Number);return;}
                const auto blocked=packed_?(terrain_[tile/8]&(std::uint8_t{1}<<(tile%8)))!=0:terrain_[tile]!=0;
                seed_.grid.terrain.push_back(blocked);seed_.grid.camp_group.push_back(-1);seed_.grid.raw_upper.push_back(upper.upper);
                mix(layout_,blocked);mix(field_,std::bit_cast<std::uint64_t>(static_cast<double>(upper.upper)));return;
            }
            terrain_.clear();phase_=Phase::Groups;return;
        }
        if(phase_==Phase::Groups) {
            if(group_>=groups_) {root_=0;phase_=Phase::Altars;return;}
            const auto& source=world.groups[static_cast<std::size_t>(order_[group_])];
            if(source.members.empty()||source.members.size()>ag::perch_check_capacity) {fail(Reason::Group);return;}
            sg::CatalogGroup group;group.canonical=static_cast<int>(group_);group.members.reserve(source.members.size());
            seed_.catalogue.push_back(std::move(group));member_=0;
            mix(catalogue_,group_);mix(catalogue_,source.members.size());phase_=Phase::Members;return;
        }
        if(phase_==Phase::Members) {
            const auto& source=world.groups[static_cast<std::size_t>(order_[group_])];
            if(member_>=source.members.size()) {++group_;phase_=Phase::Groups;return;}
            const auto& member=source.members[member_++];const auto point=member.pos;
            if(point.x<0||point.y<0||point.x>=world.width||point.y>=world.height) {fail(Reason::Group);return;}
            const auto tile=static_cast<std::size_t>(point.y)*world.width+point.x;
            if(seed_.grid.camp_group[tile]>=0) {fail(Reason::Group);return;}
            seed_.grid.camp_group[tile]=static_cast<int>(group_);
            seed_.catalogue.back().members.push_back({{point.x,point.y},member.max_hp,member.strength,member.range});
            mix(layout_,tile);mix(layout_,group_+1);mix(catalogue_,tile);
            mix(catalogue_,std::bit_cast<std::uint32_t>(member.max_hp));
            mix(catalogue_,std::bit_cast<std::uint32_t>(member.strength));mix(catalogue_,static_cast<unsigned>(member.range));return;
        }
        if(phase_==Phase::Altars) {
            if(root_<world.altars.size()) {
                const auto point=world.altars[root_++];seed_.grid.altars.push_back({point.x,point.y});
                mix(roots_,1);mix(roots_,static_cast<unsigned>(point.x));mix(roots_,static_cast<unsigned>(point.y));return;
            }
            root_=0;phase_=Phase::Homes;return;
        }
        if(phase_==Phase::Homes) {
            if(root_<world.spawn_tiles.size()) {
                const auto point=world.spawn_tiles[root_++];seed_.grid.home_roots.push_back({point.x,point.y});
                mix(roots_,2);mix(roots_,static_cast<unsigned>(point.x));mix(roots_,static_cast<unsigned>(point.y));return;
            }
            phase_=Phase::Seal;return;
        }
        if(phase_==Phase::Seal) {
            auto map=layout_;mix(map,catalogue_);mix(map,roots_);
            seed_.key={nonzero(map),nonzero(layout_),nonzero(catalogue_),nonzero(roots_),1,nonzero(field_),
                seed_.grid.require_quiet_bank,seed_.require_quiet_home};
            builder_=std::make_unique<sg::StaticBuilder>(std::move(seed_),limits_);phase_=Phase::Fields;return;
        }
        const auto state=builder_->advance(1);
        if(state==Status::Ready) {ready_=builder_->result();status_=Status::Ready;}
        else if(state!=Status::NeedCompute) {status_=state;reason_=builder_->reason();}
    }
};
} // namespace tt::assignment_static_world
