#pragma once
// One named ordinary-movement information operator. Geometry and hypothetical
// vision nominate; only current SDK predicates authorize a physical step.
// No HP/lifecycle record is read, edited or inferred here.
#include "production_recon.h"
#include <array>
#include <cstdint>
#include <optional>

namespace mb::production::potential { class Field; }
namespace tt::recon_execution {
using Point=production_recon::Point;
using Key=production_recon::Key;
inline constexpr std::size_t max_members=32;
struct Request {
    Key key;
    std::array<Point,max_members> members{};
    std::size_t member_count{};
    // Caller excludes exact corpses. The family key binds these positions and
    // this exact mask and actual camp generation/eligibility, not round/current
    // HP/unrelated visibility. A genuinely new generation needs a new key.
    std::uint32_t missing_members{};
    // A fresh completed negative menu acknowledges an earlier Observed/frontier
    // report. Ordinary repeated invocations are not an acknowledgement.
    // Deferred legs also resume automatically after an actually queried false
    // SDK legality becomes freshly true. This explicit flag requires another
    // caller-identified physical fact, never elapsed rounds alone.
    bool completed_negative{},retry_blocked{};
};
enum class Status {
    NeedCompute,Moved,Observed,AwaitingMenu,Resolved,AtFrontier,Exhausted,
    Blocked,Enemy,Unavailable,NeedObservation,Stale,Unsupported,UnknownGeometry
};
struct Flags { bool alive{},channeling{},can_move{}; };
struct Outcome {
    Status status{Status::Unsupported};
    Key key;
    Point from{-1,-1},to{-1,-1},frontier{-1,-1};
    int epoch{-1},observed_epoch{-1};
    std::uint32_t observed_members{};
    std::size_t attempted_origins{},work_units{},hypothetical_queries{};
    bool handled{},attempted{},moved{},ordinary_walk{},blocked_rechecked{},observed_legal_transition{};
};
inline bool same_family(const Request& a,const Request& b) {
    if(a.key!=b.key || a.member_count!=b.member_count || a.missing_members!=b.missing_members)return false;
    for(std::size_t i=0;i<a.member_count && i<max_members;++i)
        if(a.members[i]!=b.members[i])return false;
    return true;
}
inline bool valid_request(const Request& r,int width,int height) {
    if(r.key.group<0 || width<=0 || height<=0 || r.member_count==0 || r.member_count>max_members ||
       r.missing_members==0)return false;
    const auto allowed=r.member_count==32?~std::uint32_t{}:(std::uint32_t{1}<<r.member_count)-1;
    if(r.missing_members&~allowed)return false;
    for(std::size_t i=0;i<r.member_count;++i)
        if(r.members[i].x<0 || r.members[i].y<0 || r.members[i].x>=width || r.members[i].y>=height)return false;
    return true;
}

class Session {
public:
    void clear() { search_.reset();blocked_.reset();valid_=awaiting_menu_=have_blocked_legal_=false;observed_=0; }
    bool active() const {return valid_;}
    const Request* request() const {return valid_?&request_:nullptr;}
    // Backend imports and command are refreshed on EVERY invocation. The retained
    // Search is a nomination only. At most one hypothetical query and 256 pure
    // bookkeeping units occur; the native backend also checks empirical headroom.
    template<class Backend>
    Outcome step(const Request& request,int width,int height,std::size_t max_cells,Backend& backend) {
        Outcome out;out.key=request.key;out.epoch=backend.epoch();out.from=out.to=backend.position();
        auto finish=[&](Status status,bool handled=false) {
            out.status=status;out.handled=handled;out.observed_epoch=backend.epoch();
            if(search_)out.attempted_origins=search_->attempted_origins();
            return out;
        };
        if(!valid_request(request,width,height))return finish(Status::Unsupported);
        if(!backend.query_budget())return finish(Status::NeedCompute,true);
        const auto flags=backend.flags();
        if(!flags.alive || flags.channeling)return finish(Status::Unavailable);
        if(!backend.quiet(out.from))return finish(Status::NeedObservation);
        if(backend.epoch()!=out.epoch || backend.position()!=out.from)return finish(Status::Stale);
        // P0 bounds NPC exposure only. A visible enemy gives the ordinary threat
        // controller ownership BEFORE any retained search or expensive vision
        // computation can consume/yield this input. Preserve the search rank.
        const bool enemy=backend.enemies_present();
        if(backend.position()!=out.from || backend.epoch()!=out.epoch)return finish(Status::Stale);
        if(enemy)return finish(Status::Enemy);
        if(valid_ && request_.key==request.key && !same_family(request_,request))return finish(Status::Stale);
        if(!valid_ || !same_family(request_,request)) {
            request_=request;observed_=0;awaiting_menu_=false;blocked_.reset();have_blocked_legal_=false;
            search_.emplace(width,height,out.from,request.key,max_cells);
            valid_=true;width_=width;height_=height;
        } else if(width_!=width || height_!=height) return finish(Status::Stale);
        if(search_->origin()!=out.from && !search_->relocate(request.key,out.from))
            return finish(Status::Stale);
        const auto visible=backend.actual_visible(request_)&request_.missing_members;
        if(backend.epoch()!=out.epoch || backend.position()!=out.from)return finish(Status::Stale);
        const auto newly_visible=visible&~observed_;
        if(newly_visible) {
            observed_|=newly_visible;awaiting_menu_=true;
            out.observed_members=newly_visible;
            return finish(Status::Observed,true);
        }
        if(awaiting_menu_) {
            if(!request.completed_negative)return finish(Status::AwaitingMenu,true);
            awaiting_menu_=false;
            if(search_->status()==production_recon::Status::AtFrontier &&
               !search_->complete_frontier(request.key,out.from,production_recon::MenuCause::MenuExhausted))
                return finish(Status::Stale);
        }
        if(observed_==request_.missing_members)return finish(Status::Resolved);
        bool observed_legal_retry=false;
        if(search_->status()==production_recon::Status::Deferred) {
            // SDK legal_steps includes the actor's movement allowance. Do not
            // turn spent M into a fictitious occupancy false-to-true transition.
            if(!flags.can_move)return finish(Status::Unavailable);
            if(!blocked_ || blocked_->origin!=out.from)return finish(Status::Stale);
            out.to=blocked_->destination;out.frontier=blocked_->frontier;
            if(!request.retry_blocked) {
                if(!backend.query_budget())return finish(Status::NeedCompute,true);
                const bool legal_now=backend.legal_step(out.from,out.to);
                out.blocked_rechecked=true;
                if(backend.position()!=out.from || backend.epoch()!=out.epoch)return finish(Status::Stale);
                const bool changed=have_blocked_legal_ && !blocked_legal_ && legal_now;
                blocked_legal_=legal_now;have_blocked_legal_=true;
                if(!changed)return finish(Status::Blocked);
                out.observed_legal_transition=true;observed_legal_retry=true;
            }
            if(!search_->retry_deferred(request.key,out.from))return finish(Status::Stale);
        }
        // Actual newly observed members monotonically leave the goal set. This
        // does not reopen any prior false goal test or tried origin: BFS rank is
        // preserved under goal removal. An already selected frontier may finish.
        auto remaining=request_;
        remaining.missing_members&=~observed_;
        bool nominated=false;
        for(std::size_t units=0;units<256 && search_->status()==production_recon::Status::NeedCompute;++units) {
            if(!backend.compute_budget())return finish(Status::NeedCompute,true);
            search_->advance(request.key,out.from,1,
                [&](Point p){return backend.cell(p);},
                [&](Point p) {
                    if(!backend.candidate(p,remaining))return false;
                    ++out.hypothetical_queries;
                    nominated=(backend.hypothetical_visible(p,remaining)&remaining.missing_members)!=0;
                    return nominated;
                });
            out.work_units+=search_->last_work();
            if(backend.epoch()!=out.epoch || backend.position()!=out.from)return finish(Status::Stale);
            // A positive goal enters only Search's bounded parent-climb stage.
            // Finish that pure suffix and a fresh physical step now if fuel fits;
            // a negative query yields before another hypothetical import.
            if(out.hypothetical_queries && !nominated)break;
        }
        using S=production_recon::Status;
        if(search_->status()==S::NeedCompute)return finish(Status::NeedCompute,true);
        if(search_->status()==S::Unsupported)return finish(Status::Exhausted);
        if(search_->status()==S::UnknownGeometry)return finish(Status::UnknownGeometry);
        if(search_->status()==S::Stale)return finish(Status::Stale);
        if(search_->status()==S::AtFrontier) {
            // Arrival nominates a selected actual observation/menu, not a proof
            // that the camp is alive or a reason to reset the whole family.
            awaiting_menu_=true;out.frontier=out.from;
            return finish(Status::AtFrontier,true);
        }
        const auto leg=search_->next_leg(request.key,out.from);
        if(!leg)return finish(Status::NeedObservation);
        out.frontier=leg->frontier;out.to=leg->destination;
        if(!backend.step_budget())return finish(Status::NeedCompute,true);
        if(backend.enemies_present())return finish(Status::Enemy);
        if(!backend.quiet(out.from) || !backend.quiet(out.to))return finish(Status::NeedObservation);
        const auto final_flags=backend.flags();
        if(!final_flags.alive || final_flags.channeling || !final_flags.can_move)return finish(Status::Unavailable);
        const bool legal=observed_legal_retry || backend.legal_step(out.from,out.to);
        // A legality query can suspend too. Stale is not a blocked edge and must
        // not consume its retry premise or strand the retained geometric rank.
        if(backend.position()!=out.from || backend.epoch()!=out.epoch)return finish(Status::Stale);
        if(!legal) {
            search_->defer_leg(request.key,out.from,out.to);
            blocked_=*leg;blocked_legal_=false;have_blocked_legal_=true;
            return finish(Status::Blocked);
        }
        // Position first, epoch last: no cached observation authorizes the move.
        if(backend.position()!=out.from || backend.epoch()!=out.epoch)return finish(Status::Stale);
        out.attempted=true;out.moved=backend.move(out.to);out.ordinary_walk=out.moved;
        const auto actual=backend.position();
        if(backend.epoch()!=out.epoch || actual!=(out.moved?out.to:out.from)) {
            out.to=actual;return finish(Status::Stale,true);
        }
        if(out.moved) {
            if(!search_->record_applied(request.key,out.from,out.to))return finish(Status::Stale,true);
            blocked_.reset();have_blocked_legal_=false;
            return finish(Status::Moved,true);
        }
        search_->defer_leg(request.key,out.from,out.to);
        blocked_=*leg;have_blocked_legal_=false;
        // A native false reveals its tile for this round. Retain an actual
        // observed-legality baseline if the bounded readback fits now; the
        // physical refusal itself is not fabricated as an SDK mask reading.
        if(backend.query_budget()) {
            const bool after=backend.legal_step(out.from,out.to);
            out.blocked_rechecked=true;
            if(backend.position()!=out.from || backend.epoch()!=out.epoch)return finish(Status::Stale,true);
            blocked_legal_=after;have_blocked_legal_=true;
        }
        return finish(Status::Blocked,true);
    }
private:
    Request request_;
    std::optional<production_recon::Search> search_;
    std::optional<production_recon::Leg> blocked_;
    std::uint32_t observed_{};
    int width_{},height_{};
    bool valid_{},awaiting_menu_{},have_blocked_legal_{},blocked_legal_{};
};
} // namespace tt::recon_execution

namespace tt {
// World bootstrap and the completed, matching fixed-source potential are caller
// premises. Run early before global observations; it owns its fresh selected view.
// Exhausted means only this static P0 ordinary graph/missing-member family.
recon_execution::Outcome step_named_recon(const recon_execution::Request&,
    const mb::production::potential::Field&);
void cancel_named_recon();
}
