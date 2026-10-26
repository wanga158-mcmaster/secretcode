#pragma once
// Dated navigation nomination with an explicitly committed native field.
// Nothing here authorizes movement, work, a channel, or exposed waiting.
// The adapter still validates actual M/body/occupancy, complete Route costs and
// its failure closure before dispatch. A field root is only a nominal endpoint.
#include <cambc.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include "funded_dispatch.h"

namespace mosaic::funded_navigation {
using Key=std::uint64_t;
using Frame=std::uint64_t;
using Fuel=funded_dispatch::Fuel;
using cambc::vec2;
using cambc::bitboard;
using cambc::distance_field;

enum class Status {
    NotRequested,NotPrepared,Preparing,Prepared,AtNominalGoal,NoRoute,
    BlockedNomination,Unfunded,HoldNotAdmitted,StaleFrame,InvalidOrigin
};
struct Preparation {
    Status status{Status::NotRequested};
    bool progressed{},committed{};
    unsigned remaining{};
};
struct Step {
    vec2 point{};
    std::uint16_t distance{distance_field::unreachable};
    // Stable compass ID for the parent's per-input failure fence.
    unsigned direction{};
};
struct Nomination {
    Status status{Status::NotRequested};
    std::array<Step,8> candidates{};
    std::size_t count{};
    Key key{};
    Frame roots_frame{},field_frame{},observed_frame{};
    vec2 origin{};
    std::uint16_t origin_distance{distance_field::unreachable};
};

class Cache {
public:
    static constexpr std::uint16_t maximum_distance=120;

    // Goal masks are captured once per semantic change, not enumerated into
    // positions. replace_roots=false retains an existing same-key nomination
    // (e.g. a quiet recovery set) until an actual failure/refinement requests a
    // new one. A different semantic key always captures its new goal mask.
    // force is a causal failure/dependency invalidation, never an epoch nonce.
    bool request(Key key,bitboard goals,Frame epoch,bool replace_roots=true,bool force=false){
        if(!fresh_epoch(epoch)){last_=Status::StaleFrame;return false;}
        bool changed=!requested_||key!=key_||force;
        if(!changed&&replace_roots)changed=!(*roots_==goals);
        if(!fresh_epoch(epoch)){last_=Status::StaleFrame;return false;}
        if(!changed){last_=ready_?Status::Prepared:Status::NotPrepared;return false;}
        // Any previous committed field is revoked BEFORE its roots/storage can
        // change. The existing allocation may be reused, but cannot be read.
        ready_=false;key_=key;roots_.emplace(std::move(goals));requested_=true;
        roots_frame_=epoch;capture_frame_=epoch;capture_reported_=false;
        restart_cursor(epoch);
        last_=Status::Preparing;return true;
    }

    // Explicitly revoke a failed/obsolete nomination. Retained roots remain
    // dated data, not newly earned preparation progress. The next funded EARLY
    // prepare() performs an actual native compute; late nominate() never does.
    void invalidate(){
        ready_=false;last_=requested_?Status::NotPrepared:Status::NotRequested;
        if(requested_){restart_cursor(highest_frame_);capture_reported_=true;}
    }
    void force_reprepare(){invalidate();}

    // Guard(Fuel) is supplied by the adapter and must check CURRENT remaining
    // fuel and the actual owner epoch. entry_floor includes allocation, masks,
    // the complete atomic SDK compute and the parent's executable action tail.
    // No SDK incremental/resume contract is assumed: visited is OUTPUT only.
    // hold_admitted covers a preparation-only return if no actionable endpoint
    // results; a finite cursor by itself is not permission to wait in danger.
    template<class Guard>
    Preparation prepare(vec2 fresh_origin,const bitboard& fresh_blocked,Frame epoch,
                        bool hold_admitted,Guard&& guard,Fuel entry_floor){
        if(!requested_)return result(Status::NotRequested);
        if(!fresh_epoch(epoch)||!guard(Fuel(0)))return result(Status::StaleFrame);
        if(!inside(fresh_origin))return result(Status::InvalidOrigin);
        if(ready_){
            const auto status=classify(fresh_origin);
            if(!fresh_epoch(epoch)||!guard(Fuel(0)))return result(Status::StaleFrame);
            return result(status,false,true);
        }
        if(!hold_admitted)return result(Status::HoldNotAdmitted);
        if(!guard(entry_floor)){
            const bool new_capture=capture_frame_==epoch&&!capture_reported_;
            capture_reported_=true;
            return result(new_capture?Status::Preparing:Status::Unfunded,new_capture);
        }
        capture_reported_=true;
        // All these allocations/imports are inside the caller-funded phase.
        // Ready remains false even if compute throws or fuel suspends the VM.
        if(!field_)field_.emplace();
        auto blocked=fresh_blocked;blocked.set(fresh_origin,false);
        auto roots=*roots_&~blocked;
        bitboard destination;destination.set(fresh_origin);bitboard visited;
        if(!fresh_epoch(epoch)||!guard(Fuel(0)))return result(Status::StaleFrame);
        ready_=false;last_=Status::Preparing;
        field_->compute_to(blocked,roots,destination,visited,maximum_distance);
        // An import completing in a later owner input does not commit this
        // captured origin/mask. It remains uncommitted and is never queried.
        if(!fresh_epoch(epoch)||!guard(Fuel(0)))return result(Status::StaleFrame);
        const auto distance=field_->get(fresh_origin);
        if(!fresh_epoch(epoch)||!guard(Fuel(0)))return result(Status::StaleFrame);
        if(!cursor_.advance(generation_,epoch,1,true))return result(Status::StaleFrame);
        field_frame_=epoch;field_origin_=fresh_origin;field_blocked_=std::move(blocked);ready_=true;
        return result(classify_value(distance),true,true);
    }

    // At most eight local mask tests and nine field reads. No compute, root
    // positions(), field export or request mutation is hidden on this path.
    // The fresh reachability mask is supplied by the adapter; these candidates
    // are still only decreasing steps in a DATED guest-owned field.
    Nomination nominate(vec2 origin,const bitboard& fresh_reachable)const{
        Nomination out;out.key=key_;out.roots_frame=roots_frame_;
        out.field_frame=field_frame_;out.origin=origin;
        if(!requested_)return out;
        if(!ready_||!field_){out.status=Status::NotPrepared;return out;}
        if(!inside(origin)){out.status=Status::InvalidOrigin;return out;}
        const auto epoch=Frame(cambc::round_number());out.observed_frame=epoch;
        const auto here=field_->get(origin);out.origin_distance=here;
        if(here==0)out.status=Status::AtNominalGoal;
        else if(here==distance_field::unreachable)out.status=Status::NoRoute;
        else {
            for(unsigned direction=0;direction<directions_.size();++direction){
                const auto next=origin+directions_[direction];
                if(!inside(next)||!fresh_reachable.is_set(next))continue;
                const auto distance=field_->get(next);
                if(distance>=here)continue;
                std::size_t at=out.count;
                while(at&&out.candidates[at-1].distance>distance){
                    out.candidates[at]=out.candidates[at-1];--at;
                }
                out.candidates[at]={next,distance,direction};++out.count;
            }
            out.status=out.count?Status::Prepared:Status::BlockedNomination;
        }
        if(Frame(cambc::round_number())!=epoch){out.status=Status::StaleFrame;out.count=0;}
        return out;
    }

    bool ready()const{return ready_&&field_.has_value();}
    // Recompute an unusable dated gradient only when its actual occupancy
    // premise changed. An unchanged NoRoute is not new preparation progress.
    bool repair(const Nomination& nomination,const bitboard& fresh_blocked){
        if(!ready()||!field_blocked_||
           (nomination.status!=Status::NoRoute&&nomination.status!=Status::BlockedNomination))return false;
        auto blocked=fresh_blocked;blocked.set(nomination.origin,false);
        const bool outside_committed_domain=nomination.status==Status::NoRoute&&nomination.origin!=field_origin_;
        if(!outside_committed_domain&&*field_blocked_==blocked)return false;
        invalidate();return true;
    }
    Key key()const{return key_;}
    Frame roots_frame()const{return roots_frame_;}
    Frame field_frame()const{return field_frame_;}
    Status status()const{return last_;}
    const funded_dispatch::Cursor& cursor()const{return cursor_;}
    funded_dispatch::FailureFence& failures(){return failures_;}
    const funded_dispatch::FailureFence& failures()const{return failures_;}
    std::optional<std::uint16_t> distance(vec2 origin)const{
        if(!ready()||!inside(origin))return std::nullopt;
        return field_->get(origin);
    }

private:
    inline static constexpr std::array<vec2,8> directions_{{
        {0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}}};
    std::optional<bitboard> roots_;
    std::optional<distance_field> field_;
    std::optional<bitboard> field_blocked_;
    funded_dispatch::Cursor cursor_;
    funded_dispatch::FailureFence failures_;
    Key key_{},generation_{};
    vec2 field_origin_{};
    Frame highest_frame_{},roots_frame_{},field_frame_{},capture_frame_{};
    bool requested_{},ready_{},capture_reported_{};
    Status last_{Status::NotRequested};

    bool inside(vec2 p)const{
        return roots_&&p.x>=0&&p.y>=0&&p.x<roots_->width()&&p.y<roots_->height();
    }
    bool fresh_epoch(Frame epoch){
        const auto actual=Frame(cambc::round_number());
        if(actual>highest_frame_)highest_frame_=actual;
        if(epoch!=actual||epoch<highest_frame_)return false;
        return cursor_.enter(epoch);
    }
    void restart_cursor(Frame epoch){
        ++generation_;cursor_.request(generation_,2);cursor_.enter(epoch);
        cursor_.advance(generation_,epoch,0,true);
    }
    Status classify(vec2 origin)const{
        // Called only after the explicit ready/written invariant is established.
        return classify_value(field_->get(origin));
    }
    static Status classify_value(std::uint16_t distance){
        return distance==0?Status::AtNominalGoal:
            distance==distance_field::unreachable?Status::NoRoute:Status::Prepared;
    }
    Preparation result(Status status,bool progressed=false,bool committed=false){
        last_=status;return {status,progressed,committed,cursor_.remaining()};
    }
};
}
