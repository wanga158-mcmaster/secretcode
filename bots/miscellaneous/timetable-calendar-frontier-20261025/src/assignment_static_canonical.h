#pragma once
// Pure staged counterpart of canonical_groups. The completed bootstrap World
// and all member coordinates must remain immutable for this builder's life.
// Local order and member order are not identities: sorted complete (y,x)
// signatures determine canonical order. No SDK, handles, hashes or sorts.
#include <array>
#include <cstddef>
#include <cstdint>

namespace tt::assignment_static_canonical {
inline constexpr std::size_t max_groups=31,max_members=32;
enum class Status { NeedCompute,Ready,Unsupported };
enum class Reason { None,Shape,Capacity,Coordinate,DuplicateMember,DuplicateGroup,SourceChanged };
struct Result {
    std::array<int,max_groups> local_by_canonical,canonical_by_local;
    std::size_t count{};bool supported{};
};
template<class World> class CanonicalBuilder {
public:
    // O(1): no member traversal, vector allocation, bulk signature fill, sort
    // or publication. Unwritten fixed signature storage is never inspected.
    explicit CanonicalBuilder(const World& world,std::size_t member_bound=max_members):
        world_(&world),width_(world.width),height_(world.height),groups_(world.groups.size()),bound_(member_bound) {
        if(width_<=0||height_<=0||!groups_){fail(Reason::Shape);return;}
        if(groups_>max_groups||!bound_||bound_>max_members)fail(Reason::Capacity);
    }
    CanonicalBuilder(const CanonicalBuilder&)=delete;CanonicalBuilder& operator=(const CanonicalBuilder&)=delete;
    CanonicalBuilder(CanonicalBuilder&&)=delete;CanonicalBuilder& operator=(CanonicalBuilder&&)=delete;
    Status advance(std::size_t units) {
        last_work_=0;
        if(status_!=Status::NeedCompute)return status_;
        if(world_->width!=width_||world_->height!=height_||world_->groups.size()!=groups_)
            {fail(Reason::SourceChanged);return status_;}
        while(last_work_<units&&status_==Status::NeedCompute){++last_work_;++work_;unit();}
        return status_;
    }
    Status status() const{return status_;}Reason reason() const{return reason_;}
    std::size_t last_work() const{return last_work_;}std::uint64_t work_used() const{return work_;}
    // Unsupported/unfinished builders deliberately publish no partially built
    // mapping. Rejection decisions and every successful mapping match the old
    // function; its incidental partially written failure payload is not an API.
    const Result* result() const{return status_==Status::Ready?&result_:nullptr;}
private:
    using Coordinate=std::array<int,2>;
    enum class Phase { Init,Group,Read,InsertMember,StoreMember,Order,CompareGroup,ShiftGroup,StoreGroup,Publish,Done };
    const World* world_;int width_,height_;std::size_t groups_,bound_;
    // No {} initialization here: rows/slots become readable only after the
    // corresponding retained count says they have been written.
    std::array<std::array<Coordinate,max_members>,max_groups> signatures_;
    std::array<std::size_t,max_groups> counts_;
    std::array<int,max_groups> order_;
    Result result_;Status status_{Status::NeedCompute};Reason reason_{Reason::None};Phase phase_{Phase::Init};
    std::size_t init_{},group_{},read_{},sorted_{},insert_{},candidate_{},next_candidate_{},ordered_{},compare_{},publish_{},last_work_{};
    Coordinate member_;std::uint64_t work_{};
    void fail(Reason reason){status_=Status::Unsupported;reason_=reason;phase_=Phase::Done;}
    void unit() {
        switch(phase_) {
        case Phase::Init:
            if(init_<max_groups){result_.local_by_canonical[init_]=result_.canonical_by_local[init_]=-1;++init_;return;}
            phase_=Phase::Group;return;
        case Phase::Group:
            if(group_>=groups_){phase_=Phase::Order;return;}
            counts_[group_]=world_->groups[group_].members.size();
            if(!counts_[group_]){fail(Reason::Shape);return;}
            if(counts_[group_]>bound_){fail(Reason::Capacity);return;}
            read_=sorted_=0;phase_=Phase::Read;return;
        case Phase::Read: {
            if(read_>=counts_[group_]){++group_;phase_=Phase::Group;return;}
            const auto p=world_->groups[group_].members[read_++].pos;
            if(p.x<0||p.y<0||p.x>=width_||p.y>=height_){fail(Reason::Coordinate);return;}
            member_={p.y,p.x};insert_=sorted_;phase_=Phase::InsertMember;return;
        }
        case Phase::InsertMember:
            if(!insert_){phase_=Phase::StoreMember;return;}
            if(member_==signatures_[group_][insert_-1]){fail(Reason::DuplicateMember);return;}
            if(member_<signatures_[group_][insert_-1]) {
                signatures_[group_][insert_]=signatures_[group_][insert_-1];--insert_;return;
            }
            phase_=Phase::StoreMember;return;
        case Phase::StoreMember:
            signatures_[group_][insert_]=member_;++sorted_;phase_=Phase::Read;return;
        case Phase::Order:
            if(next_candidate_>=groups_){phase_=Phase::Publish;return;}
            candidate_=next_candidate_++;insert_=ordered_;compare_=0;phase_=Phase::CompareGroup;return;
        case Phase::CompareGroup: {
            if(!insert_){phase_=Phase::StoreGroup;return;}
            const auto other=static_cast<std::size_t>(order_[insert_-1]);
            if(compare_<counts_[candidate_]&&compare_<counts_[other]) {
                const auto& a=signatures_[candidate_][compare_];const auto& b=signatures_[other][compare_];
                if(a==b){++compare_;return;}
                phase_=a<b?Phase::ShiftGroup:Phase::StoreGroup;return;
            }
            if(counts_[candidate_]==counts_[other]){fail(Reason::DuplicateGroup);return;}
            phase_=counts_[candidate_]<counts_[other]?Phase::ShiftGroup:Phase::StoreGroup;return;
        }
        case Phase::ShiftGroup:
            order_[insert_]=order_[insert_-1];--insert_;compare_=0;phase_=Phase::CompareGroup;return;
        case Phase::StoreGroup:
            order_[insert_]=static_cast<int>(candidate_);++ordered_;phase_=Phase::Order;return;
        case Phase::Publish:
            if(publish_<groups_) {
                const auto local=static_cast<std::size_t>(order_[publish_]);
                result_.local_by_canonical[publish_]=static_cast<int>(local);
                result_.canonical_by_local[local]=static_cast<int>(publish_);++publish_;return;
            }
            result_.count=groups_;result_.supported=true;status_=Status::Ready;phase_=Phase::Done;return;
        case Phase::Done:return;
        }
    }
};
} // namespace tt::assignment_static_canonical
