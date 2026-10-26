#pragma once
#include "mechanics.h"
#include "../../../miscellaneous/mosaic-ordered-words-r20-20261025/src/native_service_navigation.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace mosaic::adaptive {

using RouteStatus=game::NavigationStatus;
using RouteProgress=game::NavigationProgress;
enum class BlockKind : std::uint8_t {terrain,camp,champion,teleport,unknown};
enum class MoveOutcome : std::uint8_t {not_attempted,refused,applied,unknown};
struct MoveRecord {
    std::uint64_t serial{};Epoch epoch{};Point from{},to{};MoveOutcome outcome{MoveOutcome::not_attempted};
};
struct MoveUpdate {bool recorded{},raw_attempt{},accepted{},strict_progress{};};
struct MoveLedger {
    std::uint64_t last_serial{},attempts{},accepted{},refused{},not_attempted{},unresolved{},distance_progress{};
};
struct Candidates {
    std::array<Point,8> points{};unsigned count{};bool field_ready{},direct{};
};

// Geometry and physical receipt accounting only. A passable byte does not
// assert occupancy, visibility, survivability, action availability or payout.
// The caller keeps this board immutable until retire(), admits begin's bounded
// <=256-root comparison/copy, and binds semantic changes to actual goal facts.
template<std::size_t Cells=16384> class Route {
public:
    using Navigation=game::Navigation<Cells>;
    constexpr Route()noexcept=default;
    Route(const Route&)=delete;Route& operator=(const Route&)=delete;
    Route(Route&&)=delete;Route& operator=(Route&&)=delete;

    bool begin(int width,int height,std::span<const std::uint8_t> board,
               std::span<const Point> roots,std::uint64_t semantic)noexcept {
        bool same=configured_&&width_==width&&height_==height&&board_.data()==board.data()&&
            semantic_==semantic&&root_count_==roots.size()&&roots.size()<=Navigation::max_roots;
        for(std::size_t i=0;same&&i<roots.size();++i)same=roots_[i]==roots[i];
        if(!same&&revision_==std::numeric_limits<std::uint64_t>::max()){retire();return false;}
        if(!field_.begin(width,height,board,roots,semantic)){forget();return false;}
        if(same)return true;
        width_=width;height_=height;board_=board.first(std::size_t(width)*std::size_t(height));
        semantic_=semantic;root_count_=roots.size();
        for(std::size_t i=0;i<roots.size();++i)roots_[i]=roots[i];
        configured_=true;++revision_;return true;
    }
    void retire()noexcept {field_.retire();forget();}

    // Revisions/attempts may advance after a FALSE within this owner input.
    // Its known blocked landing stays omitted. The next actual ROUND or a new
    // actual pose releases these transient observations, not the static board.
    bool begin_input(Epoch actual,Point pose)noexcept {
        if(actual.round<0||pose.x<0||pose.y<0||(have_input_&&before(actual,input_)))return false;
        if(!have_input_||actual.round!=input_.round||pose!=pose_)clear_blocks();
        input_=actual;pose_=pose;have_input_=true;return true;
    }
    bool reject(Point from,Point to,Epoch observed,BlockKind kind)noexcept {
        if(!current(from,observed)||!adjacent(from,to)||unsigned(kind)>unsigned(BlockKind::unknown))return false;
        unsigned at=8;
        for(unsigned i=0;i<blocks_.size();++i)if(blocks_[i].used&&blocks_[i].from==from&&blocks_[i].to==to){at=i;break;}
        if(at==8)for(unsigned i=0;i<blocks_.size();++i)if(!blocks_[i].used){at=i;break;}
        if(at==8)return false; // eight adjacent destinations are the complete set
        auto& block=blocks_[at];
        if(block.used&&before(observed,block.epoch))return false;
        if(kind==BlockKind::unknown&&block.used&&block.active)kind=block.kind;
        block={observed,from,to,kind,true,true};return true;
    }
    bool observe_clear(Point from,Point to,Epoch observed)noexcept {
        if(!current(from,observed))return false;
        for(auto& block:blocks_)if(block.used&&block.from==from&&block.to==to){
            if(before(observed,block.epoch))return false;
            block.epoch=observed;block.active=false;return true;
        }
        return false;
    }

    template<class Admit> RouteProgress advance(unsigned cap,Admit&& admit){
        RouteProgress out{field_.status(),0,false};const auto revision=revision_;
        while(out.units<cap&&out.status==RouteStatus::preparing){
            const bool allowed=bool(admit());out.status=field_.status();
            if(!allowed||!configured_||revision_!=revision||out.status!=RouteStatus::preparing)break;
            const auto leaf=field_.step(1);out.units+=leaf.units;out.progressed|=leaf.progressed;out.status=leaf.status;
            if(!leaf.units||!leaf.progressed)break;
        }
        return out;
    }
    Candidates nominees(std::optional<Point> actual_goal=std::nullopt)const noexcept {
        Candidates out{};if(!configured_||!have_input_||!inside(pose_))return out;
        std::array<Point,8> omitted{};unsigned count=0;
        for(const auto& block:blocks_)if(block.active&&block.epoch.round==input_.round&&block.from==pose_)
            omitted[count++]=block.to;
        out.field_ready=field_.available(pose_);
        if(out.field_ready){out.count=field_.nominees(pose_,{omitted.data(),count},out.points);return out;}
        if(field_.status()!=RouteStatus::preparing||!actual_goal||!inside(*actual_goal)||*actual_goal==pose_)return out;
        const int dx=sign(std::int64_t(actual_goal->x)-pose_.x),dy=sign(std::int64_t(actual_goal->y)-pose_.y);
        const std::array<Point,3> direct{{{pose_.x+dx,pose_.y+dy},{pose_.x+dx,pose_.y},{pose_.x,pose_.y+dy}}};
        for(const auto to:direct){
            if(!adjacent(pose_,to)||!passable(to)||distance(to,*actual_goal)>=distance(pose_,*actual_goal))continue;
            bool blocked=false;for(unsigned i=0;i<count;++i)blocked|=omitted[i]==to;
            if(!blocked){out.points[0]=to;out.count=1;out.direct=true;break;}
        }
        return out;
    }

    // Receipt serials are caller supplied and monotonic. An unresolved latest
    // receipt may resolve once with its SAME pre-attempt epoch/from/to. No
    // observation here changes actual pose, M, service ownership or custody.
    MoveUpdate record(const MoveRecord& receipt)noexcept {
        MoveUpdate out{};
        if(!receipt.serial||receipt.epoch.round<0||receipt.from==receipt.to||
           unsigned(receipt.outcome)>unsigned(MoveOutcome::unknown)||receipt.serial<ledger_.last_serial)return out;
        const bool resolve=receipt.serial==ledger_.last_serial;
        if(resolve){
            if(last_.outcome!=MoveOutcome::unknown||receipt.epoch!=last_.epoch||receipt.from!=last_.from||receipt.to!=last_.to||
               (receipt.outcome!=MoveOutcome::applied&&receipt.outcome!=MoveOutcome::refused))return out;
            if(ledger_.unresolved)--ledger_.unresolved;
        }else{
            ledger_.last_serial=receipt.serial;last_revision_=revision();
            if(receipt.outcome!=MoveOutcome::not_attempted)++ledger_.attempts;
        }
        out.recorded=true;out.raw_attempt=receipt.outcome!=MoveOutcome::not_attempted;
        if(receipt.outcome==MoveOutcome::applied){
            out.accepted=true;++ledger_.accepted;
            observe_clear(receipt.from,receipt.to,receipt.epoch);
            const auto from=field_.distance(receipt.from),to=field_.distance(receipt.to);
            out.strict_progress=configured_&&last_revision_==revision_&&from&&to&&*to<*from;
            if(out.strict_progress)++ledger_.distance_progress;
        }else if(receipt.outcome==MoveOutcome::unknown)++ledger_.unresolved;
        else{
            if(receipt.outcome==MoveOutcome::refused)++ledger_.refused;else ++ledger_.not_attempted;
            reject(receipt.from,receipt.to,receipt.epoch,BlockKind::unknown);
        }
        last_=receipt;return out;
    }
    [[nodiscard]] const Navigation& navigation()const noexcept{return field_;}
    [[nodiscard]] std::uint64_t revision()const noexcept{return configured_?revision_:0;}
    [[nodiscard]] std::uint64_t semantic()const noexcept{return configured_?semantic_:0;}
    [[nodiscard]] const MoveLedger& ledger()const noexcept{return ledger_;}

private:
    struct Block {Epoch epoch{};Point from{},to{};BlockKind kind{BlockKind::unknown};bool used{},active{};};
    Navigation field_;
    std::array<Point,Navigation::max_roots> roots_;
    std::array<Block,8> blocks_{};
    std::span<const std::uint8_t> board_{};std::size_t root_count_{};
    int width_{},height_{};std::uint64_t semantic_{},revision_{},last_revision_{};
    Epoch input_{};Point pose_{};bool configured_{},have_input_{};
    MoveRecord last_{};MoveLedger ledger_{};
    static constexpr bool before(Epoch a,Epoch b)noexcept {
        return a.round!=b.round?a.round<b.round:a.revision!=b.revision?a.revision<b.revision:a.attempt<b.attempt;
    }
    static constexpr int sign(std::int64_t value)noexcept{return (value>0)-(value<0);}
    static constexpr std::uint64_t distance(Point a,Point b)noexcept {
        const auto x=std::int64_t(a.x)-b.x,y=std::int64_t(a.y)-b.y;
        return std::uint64_t(std::max(x<0?-x:x,y<0?-y:y));
    }
    static constexpr bool adjacent(Point a,Point b)noexcept{return distance(a,b)==1;}
    bool inside(Point point)const noexcept{return point.x>=0&&point.y>=0&&point.x<width_&&point.y<height_;}
    bool passable(Point point)const noexcept{return inside(point)&&board_[std::size_t(point.y)*std::size_t(width_)+std::size_t(point.x)]!=0;}
    bool current(Point from,Epoch observed)const noexcept {
        return have_input_&&observed.round==input_.round&&from==pose_;
    }
    void clear_blocks()noexcept {for(auto& block:blocks_)block={};}
    void forget()noexcept {configured_=false;board_={};root_count_=0;width_=height_=0;semantic_=0;}
};

} // namespace mosaic::adaptive
