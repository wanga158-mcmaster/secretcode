#pragma once
#include "native_service_types.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace mosaic::native_service {

struct FrontierMember {Point pose{};int ordinal{-1};};
struct FrontierIntent {
    NativeId group_id{};int group_ordinal{-1};
    std::uint64_t static_generation{},board_generation{},remaining_revision{};
    int basic_range{};
    friend constexpr bool operator==(FrontierIntent,FrontierIntent)=default;
};
enum class FrontierStatus : std::uint8_t {idle,preparing,ready,more,failed};
struct FrontierProgress {FrontierStatus status{FrontierStatus::idle};unsigned units{};bool progressed{};};
struct GroupFrontierFixtureAccess;

// Geometry nomination for ONE retained group, not a nearest-member policy.
// Borrow board and ordered members immutably until retire(). The intent binds
// their semantic revisions and the caller's stable work_remaining(ordinal)
// predicate: Unknown must remain possible; only qualified Dead may be excluded.
// Predicate acquisition, life/freshness, occupancy, HP and SDK permission are
// caller authority. The predicate must be pure, bounded and stable per intent.
//
// Own centre is never a Basic perch. Other centres obey the supplied board;
// this builder never manufactures cleared occupancy from a static spawn.
// Default initialization leaves stamp scratch untouched; static constinit
// GroupFrontier<> value{} is also legitimate. Initial/wrap stamp clearing is
// staged, not a hot O(MaxCells) operation in begin(), retire() or next_batch().
template<std::size_t MaxCells=16384,std::size_t MaxRoots=256> class GroupFrontier {
    static_assert(MaxCells>0&&MaxCells<=std::size_t(std::numeric_limits<int>::max()));
    static_assert(MaxRoots>0&&MaxRoots<=256);
public:
    static constexpr std::size_t max_cells=MaxCells,max_roots=MaxRoots;
    constexpr GroupFrontier()noexcept=default;
    GroupFrontier(const GroupFrontier&)=delete;
    GroupFrontier& operator=(const GroupFrontier&)=delete;
    GroupFrontier(GroupFrontier&&)=delete;
    GroupFrontier& operator=(GroupFrontier&&)=delete;

    bool begin(int width,int height,std::span<const std::uint8_t> board,
               std::span<const FrontierMember> members,FrontierIntent intent)noexcept {
        if(width<=0||height<=0||std::size_t(width)>MaxCells/std::size_t(height)||
           intent.group_ordinal<0||intent.basic_range<1)return fail();
        const auto cells=std::size_t(width)*std::size_t(height);
        if(board.size()<cells)return fail();
        if(active()&&width_==width&&height_==height&&board_.data()==board.data()&&
           members_.data()==members.data()&&members_.size()==members.size()&&intent_==intent)return true;
        if(next_generation_==std::numeric_limits<std::uint64_t>::max())return fail();
        width_=width;height_=height;cells_=cells;board_=board.first(cells);members_=members;intent_=intent;
        generation_=++next_generation_;batch_=member_cursor_=root_count_=0;member_active_=pending_=batched_=false;
        if(stamp_generation_==std::numeric_limits<std::uint32_t>::max()){
            stamp_generation_=1;clearing_=true;clear_cursor_=0;clear_limit_=initialized_;
        }else ++stamp_generation_;
        status_=members.empty()?FrontierStatus::ready:FrontierStatus::preparing;return true;
    }

    // One unit zeroes one unread/wrapped stamp, admits/skips one member, or
    // inspects one perch. O(1) deduplication; no member/root/board scan per unit.
    // The stored overflow perch consumes one explicit unit in its next batch.
    template<class Remaining> FrontierProgress step(unsigned maxUnits,Remaining&& work_remaining){
        unsigned units=0;
        while(units<maxUnits&&status_==FrontierStatus::preparing){
            if(clearing_){
                if(clear_cursor_<clear_limit_){stamps_[clear_cursor_++]=0;++units;}
                if(clear_cursor_==clear_limit_)clearing_=false;
                continue;
            }
            if(initialized_<cells_){stamps_[initialized_++]=0;++units;continue;}
            if(pending_){
                stamps_[index(pending_point_)]=stamp_generation_;roots_[root_count_++]=pending_point_;
                pending_=false;++units;finish_if_done();continue;
            }
            if(!member_active_){
                if(member_cursor_==members_.size()){status_=FrontierStatus::ready;break;}
                const auto member=members_[member_cursor_];++units;
                if(member.ordinal<0||!inside(member.pose)){fail();break;}
                if(!work_remaining(member.ordinal)){++member_cursor_;finish_if_done();continue;}
                member_point_=member.pose;
                const auto range=std::int64_t(intent_.basic_range);
                left_=int(std::max<std::int64_t>(0,std::int64_t(member.pose.x)-range));
                right_=int(std::min<std::int64_t>(width_-1,std::int64_t(member.pose.x)+range));
                top_=int(std::max<std::int64_t>(0,std::int64_t(member.pose.y)-range));
                bottom_=int(std::min<std::int64_t>(height_-1,std::int64_t(member.pose.y)+range));
                cursor_={left_,top_};member_active_=true;continue;
            }
            const auto point=cursor_;advance_perch();++units;
            const auto at=index(point);
            if(point!=member_point_&&board_[at]&&stamps_[at]!=stamp_generation_){
                if(root_count_==MaxRoots){
                    pending_point_=point;pending_=batched_=true;status_=FrontierStatus::more;
                }else {stamps_[at]=stamp_generation_;roots_[root_count_++]=point;}
            }
            finish_if_done();
        }
        return {status_,units,units!=0};
    }

    // A more batch is full and has one saved unseen root. Publishing it never
    // erases that continuation. The next batch keeps GLOBAL dedup stamps, so
    // every unique possible-member perch appears once across a finite scan.
    bool next_batch()noexcept {
        if(status_!=FrontierStatus::more||!pending_)return false;
        ++batch_;root_count_=0;status_=FrontierStatus::preparing;return true;
    }
    void retire()noexcept {
        board_={};members_={};intent_={};width_=height_=0;cells_=0;
        generation_=batch_=member_cursor_=root_count_=0;member_active_=pending_=batched_=false;status_=FrontierStatus::idle;
    }
    [[nodiscard]] bool active()const noexcept {return status_!=FrontierStatus::idle&&status_!=FrontierStatus::failed;}
    [[nodiscard]] FrontierStatus status()const noexcept {return status_;}
    [[nodiscard]] std::uint64_t generation()const noexcept {return generation_;}
    [[nodiscard]] std::size_t batch()const noexcept {return batch_;}
    [[nodiscard]] FrontierIntent intent()const noexcept {return intent_;}
    [[nodiscard]] std::size_t member_cursor()const noexcept {return member_cursor_;}
    [[nodiscard]] std::span<const Point> roots()const noexcept {
        return status_==FrontierStatus::ready||status_==FrontierStatus::more?
            std::span<const Point>{roots_.data(),root_count_}:std::span<const Point>{};
    }
    // Even the final ready batch of an overflowed enumeration omits earlier
    // batches. No individual batched Navigation can certify group-unreachable.
    [[nodiscard]] bool complete_roots()const noexcept {return status_==FrontierStatus::ready&&!batched_;}
    [[nodiscard]] bool batched()const noexcept {return batched_;}

private:
    friend struct GroupFrontierFixtureAccess;
    std::array<std::uint32_t,MaxCells> stamps_;
    std::array<Point,MaxRoots> roots_{};
    std::span<const std::uint8_t> board_{};std::span<const FrontierMember> members_{};
    FrontierIntent intent_{};FrontierStatus status_{FrontierStatus::idle};
    std::uint64_t generation_{},next_generation_{};std::uint32_t stamp_generation_{};
    std::size_t cells_{},initialized_{},clear_cursor_{},clear_limit_{},batch_{},member_cursor_{},root_count_{};
    int width_{},height_{},left_{},right_{},top_{},bottom_{};Point cursor_{},member_point_{},pending_point_{};
    bool clearing_{},member_active_{},pending_{},batched_{};
    bool fail()noexcept {retire();status_=FrontierStatus::failed;return false;}
    [[nodiscard]] bool inside(Point point)const noexcept {
        return point.x>=0&&point.y>=0&&point.x<width_&&point.y<height_;
    }
    [[nodiscard]] std::size_t index(Point point)const noexcept {
        return std::size_t(point.y)*std::size_t(width_)+std::size_t(point.x);
    }
    void advance_perch()noexcept {
        if(cursor_.x<right_){++cursor_.x;return;}
        cursor_.x=left_;
        if(cursor_.y<bottom_){++cursor_.y;return;}
        member_active_=false;++member_cursor_;
    }
    void finish_if_done()noexcept {
        if(status_==FrontierStatus::preparing&&!pending_&&!member_active_&&member_cursor_==members_.size())
            status_=FrontierStatus::ready;
    }
};

} // namespace mosaic::native_service
