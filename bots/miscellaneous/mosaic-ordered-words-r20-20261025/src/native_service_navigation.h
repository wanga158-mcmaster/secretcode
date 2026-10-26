#pragma once
#include "native_service_types.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace mosaic::native_service {

enum class NavigationStatus : std::uint8_t { idle, preparing, ready, unreachable, failed };
struct NavigationProgress {
    NavigationStatus status{NavigationStatus::idle};
    unsigned units{};
    bool progressed{};
};

// The borrowed board is immutable for a field's lifetime. A nonzero byte is
// passable. Intent must bind its semantic revision; this is not an observation,
// occupancy check, path permission or safety certificate. Caller-admit begin's
// bounded root copy/comparison and the object's actual initialization cost.
// Default initialization leaves scratch arrays unread until staged stamp init;
// static constinit Navigation<> value{} is also a legitimate initialization.
struct NavigationFixtureAccess;
template<std::size_t MaxCells=16384> class Navigation {
    static_assert(MaxCells>0 && MaxCells<=std::size_t(std::numeric_limits<int>::max()));
public:
    using Status=NavigationStatus;
    using Progress=NavigationProgress;
    static constexpr std::size_t max_cells=MaxCells, max_roots=256, max_excluded=8;

    constexpr Navigation() noexcept=default;
    Navigation(const Navigation&)=delete;
    Navigation& operator=(const Navigation&)=delete;
    Navigation(Navigation&&)=delete;
    Navigation& operator=(Navigation&&)=delete;

    // End this field's borrow before changing or releasing its board. This is
    // constant work: old roots, queue entries and distances become inaccessible
    // through metadata only. Retain initialized stamps, generation and any
    // pending wrap clear so the next begin cannot revive old discoveries.
    void retire() noexcept {
        requested_=seeded_=false; board_={}; intent_=0;
        width_=height_=0; cell_count_=root_count_=root_cursor_=head_=tail_=0;
        status_=Status::idle;
    }

    // Equal ordered roots, intent, dimensions and board address retain the
    // partial field. An epoch alone must never be used as a new semantic intent.
    // Out-of-board roots invalidate the request; blocked/duplicate roots are
    // harmless and are inspected individually during the seeding stage.
    bool begin(int width,int height,std::span<const std::uint8_t> passable,
               std::span<const Point> roots,std::uint64_t intent) noexcept {
        if(width<=0 || height<=0 || std::size_t(width)>MaxCells/std::size_t(height) ||
           roots.size()>max_roots)return fail();
        const auto cells=std::size_t(width)*std::size_t(height);
        if(passable.size()<cells)return fail();
        for(const auto p:roots)if(p.x<0 || p.y<0 || p.x>=width || p.y>=height)return fail();
        bool same=requested_ && width==width_ && height==height_ && intent==intent_ &&
            passable.data()==board_.data() && roots.size()==root_count_;
        for(std::size_t i=0;same && i<roots.size();++i)same=roots_[i]==roots[i];
        if(same)return true;

        width_=width; height_=height; cell_count_=cells; board_=passable.first(cells);
        intent_=intent; root_count_=roots.size();
        for(std::size_t i=0;i<root_count_;++i)roots_[i]=roots[i];
        root_cursor_=head_=tail_=0; seeded_=false; requested_=true;
        // Zero denotes no visit. On wrap, clear all previously initialized
        // stamps incrementally, even if a later request shrinks the board or
        // replaces this request before that clear finishes.
        if(generation_==std::numeric_limits<std::uint32_t>::max()){
            generation_=1; clearing_=true; clear_cursor_=0; clear_limit_=initialized_;
        }else ++generation_;
        status_=Status::preparing;
        if(root_count_==0){seeded_=true; status_=Status::unreachable;}
        return true;
    }

    // One unit initializes one stamp, seeds one root, or expands one queue cell
    // with eight fixed neighbors. No distance is published before ALL roots
    // have been seeded. BFS discovery then fixes its shortest distance.
    Progress step(unsigned maxUnits) noexcept {
        unsigned units=0;
        while(units<maxUnits && status_==Status::preparing){
            if(clearing_){
                if(clear_cursor_<clear_limit_){stamps_[clear_cursor_++]=0; ++units;}
                if(clear_cursor_==clear_limit_)clearing_=false;
                continue;
            }
            if(initialized_<cell_count_){stamps_[initialized_++]=0; ++units; continue;}
            if(root_cursor_<root_count_){
                const auto at=index(roots_[root_cursor_++]);
                if(board_[at] && stamps_[at]!=generation_){
                    if(!enqueue(at,0))break;
                }
                ++units;
                if(root_cursor_==root_count_){seeded_=true; finish_if_empty();}
                continue;
            }
            if(head_==tail_){finish_if_empty(); break;}
            const auto at=std::size_t(queue_[head_++]);
            const Point from{int(at%std::size_t(width_)),int(at/std::size_t(width_))};
            const int next_distance=distances_[at]+1;
            for(const auto delta:directions_){
                const Point to{from.x+delta.x,from.y+delta.y};
                if(!inside(to))continue;
                const auto next=index(to);
                if(board_[next] && stamps_[next]!=generation_ && !enqueue(next,next_distance))break;
            }
            ++units;
            if(status_==Status::failed)break;
            finish_if_empty();
        }
        return {status_,units,units!=0};
    }

    [[nodiscard]] Status status()const noexcept{return status_;}
    // The field can be preparing while a particular discovered point is ready.
    // An undiscovered point becomes typed unreachable only on exhaustion.
    [[nodiscard]] Status status(Point point)const noexcept {
        if(status_==Status::idle || status_==Status::failed)return status_;
        if(!inside(point))return Status::failed;
        if(available(point))return Status::ready;
        return status_==Status::preparing?Status::preparing:Status::unreachable;
    }
    [[nodiscard]] bool available(Point point)const noexcept {
        return status_!=Status::idle && status_!=Status::failed && seeded_ && root_count_ &&
            !clearing_ && initialized_>=cell_count_ && inside(point) && stamps_[index(point)]==generation_;
    }
    [[nodiscard]] std::optional<int> distance(Point point)const noexcept {
        if(!available(point))return std::nullopt;
        return distances_[index(point)];
    }
    // Stable compass order: NW,N,NE,W,E,SW,S,SE. At most eight writes and 64
    // exclusion comparisons. Oversized exclusions fail closed without changing
    // the field; no ignored suffix can accidentally re-nominate a refused cell.
    unsigned nominees(Point from,std::span<const Point> excluded,std::span<Point> out)const noexcept {
        if(excluded.size()>max_excluded || out.empty() || !available(from))return 0;
        // Exclusions and output may legally alias. Preserve the bounded input
        // prefix before emitting a nominee into the caller's workspace.
        std::array<Point,max_excluded> omitted;
        for(std::size_t i=0;i<excluded.size();++i)omitted[i]=excluded[i];
        const int before=distances_[index(from)];
        unsigned count=0;
        for(const auto delta:directions_){
            const Point to{from.x+delta.x,from.y+delta.y};
            if(!inside(to) || !board_[index(to)] || !available(to) || distances_[index(to)]>=before)continue;
            bool omit=false;
            for(std::size_t i=0;i<excluded.size();++i)if(omitted[i]==to){omit=true; break;}
            if(omit)continue;
            out[count++]=to;
            if(std::size_t(count)==out.size())break;
        }
        return count;
    }

private:
    friend struct NavigationFixtureAccess;
    inline static constexpr std::array<Point,8> directions_{{{-1,-1},{0,-1},{1,-1},{-1,0},
                                                           {1,0},{-1,1},{0,1},{1,1}}};
    std::array<std::uint32_t,MaxCells> stamps_;
    std::array<int,MaxCells> distances_;
    std::array<std::uint32_t,MaxCells> queue_;
    std::array<Point,max_roots> roots_;
    std::span<const std::uint8_t> board_{};
    int width_{},height_{};
    std::size_t cell_count_{},initialized_{},root_count_{},root_cursor_{},head_{},tail_{};
    std::size_t clear_cursor_{},clear_limit_{};
    std::uint64_t intent_{};
    std::uint32_t generation_{};
    Status status_{Status::idle};
    bool requested_{},seeded_{},clearing_{};

    [[nodiscard]] bool inside(Point p)const noexcept {
        return p.x>=0 && p.y>=0 && p.x<width_ && p.y<height_;
    }
    [[nodiscard]] std::size_t index(Point p)const noexcept {
        return std::size_t(p.y)*std::size_t(width_)+std::size_t(p.x);
    }
    bool fail()noexcept {
        retire(); status_=Status::failed;
        return false;
    }
    bool enqueue(std::size_t at,int distance)noexcept {
        if(tail_>=cell_count_){fail(); return false;}
        queue_[tail_++]=std::uint32_t(at); stamps_[at]=generation_; distances_[at]=distance;
        return true;
    }
    void finish_if_empty()noexcept {
        if(seeded_ && head_==tail_)status_=tail_?Status::ready:Status::unreachable;
    }
};

} // namespace mosaic::native_service
