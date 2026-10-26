#pragma once
#include "native_service_navigation.h"
#include <array>
#include <cstdint>
#include <span>

namespace mosaic::native_service {

enum class RoutePurpose : std::uint8_t {work,capture,bank,exit,recover};
enum class RouteNeed : std::uint8_t {none,arrival,prepare,move,observe,blocked,unreachable,feedback,transfer};
enum class RouteBinding : std::uint8_t {bound,retained,replacement_required,invalid};
enum class RouteRejectionKind : std::uint8_t {terrain,camp,champion,teleport,unknown};
enum class RouteServiceKind : std::uint8_t {work_effect,work_complete,capture,bank_start,bank_complete,egress_closed,recovered};

struct RouteGoal {
    // token is a nonzero, unreused route-episode identity, not an SDK handle.
    std::uint64_t token{},footprint{};
    RoutePurpose purpose{RoutePurpose::work};
    NativeId group{},member{};Point target{};bool member_bound{true};
    friend constexpr bool operator==(RouteGoal,RouteGoal)=default;
    constexpr bool valid()const noexcept {
        return token&&unsigned(purpose)<=unsigned(RoutePurpose::recover);
    }
};
struct RouteFrame {
    RouteGoal goal{};Epoch epoch{};Point pose{};
    std::uint64_t field_generation{};
    NavigationStatus status{NavigationStatus::idle};int distance{-1};
    bool field_matches{},nominees_available{},geometric_arrival{};
};
// The caller produces this only after reserving the complete named branch in
// its actual input budget. It is not a query result, permission or numeric quote.
struct RouteFunding {
    std::uint64_t owner{},serial{};Epoch epoch{};RouteNeed need{RouteNeed::none};
    bool complete_continuation{},reserved{};
    // Required for replacement: identifies the suspended source owner.
    std::uint64_t from_owner{};
};
struct RouteMove {
    std::uint64_t owner{},serial{};Epoch before{},after{};Point from{},to{};
    bool applied_actual{};
};
struct RouteFeedback {
    std::uint64_t serial{};bool exact_ack{};
};
struct RouteRejection {
    std::uint64_t owner{},decision{};Epoch epoch{};Point from{},to{};
    RouteRejectionKind kind{RouteRejectionKind::unknown};bool raw_attempt{};
};
struct RouteServiceEvent {
    std::uint64_t owner{},serial{};Epoch epoch{};
    RouteServiceKind kind{RouteServiceKind::work_effect};
    bool actual{},exact_ack{};
};

namespace route_owner_detail {
constexpr bool before(Epoch a,Epoch b)noexcept {
    if(a.round!=b.round)return a.round<b.round;
    if(a.revision!=b.revision)return a.revision<b.revision;
    return a.attempt<b.attempt;
}
constexpr bool fresh(Epoch value)noexcept{return value.round>=0;}
}

// Owns transport to an actual service operator. Geometry nominates the next
// step; only the caller's fresh SDK arrival/command/feedback can apply it.
// All loops below have at most eight iterations. No world copy or SDK imports.
class RouteOwner {
public:
    static constexpr unsigned max_exclusions=8;

    constexpr RouteBinding bind(RouteGoal goal)noexcept {
        if(!goal.valid())return RouteBinding::invalid;
        if(active_){
            if(goal==goal_){replacement_requested_=false;return RouteBinding::retained;}
            replacement_requested_=true;return RouteBinding::replacement_required;
        }
        if(goal.token==goal_.token)return RouteBinding::invalid;
        install(goal);return RouteBinding::bound;
    }
    // A replacement transfers the outstanding route, not its custody or past
    // physical history. Pending command feedback cannot be transferred away.
    constexpr bool replace(RouteGoal goal,const RouteFunding& funding)noexcept {
        if(!active_||pending_.active||!goal.valid()||goal.token==goal_.token||funding.from_owner!=goal_.token||
           !funding_valid(funding,goal.token)||funding.need==RouteNeed::none||
           funding.need==RouteNeed::feedback||funding.need==RouteNeed::transfer)return false;
        last_funding_=funding.serial;install(goal);latest_=funding.epoch;need_=funding.need;funding_=funding;
        return true;
    }
    constexpr bool fund(const RouteFunding& funding)noexcept {
        if(!active_||!funding_valid(funding,goal_.token)||funding.epoch!=latest_||
           (funding.from_owner&&funding.from_owner!=goal_.token)||funding.need!=need()||
           funding.need==RouteNeed::none)return false;
        last_funding_=funding.serial;funding_=funding;return true;
    }
    constexpr bool funded()const noexcept {
        return active_&&funding_.owner==goal_.token&&funding_.epoch==latest_&&
            funding_.need==need()&&funding_.complete_continuation&&funding_.reserved;
    }
    constexpr RouteNeed need()const noexcept {
        if(!active_)return RouteNeed::none;
        if(pending_.active)return RouteNeed::feedback;
        if(replacement_requested_)return RouteNeed::transfer;
        return need_;
    }
    constexpr bool active()const noexcept{return active_;}
    constexpr RouteGoal goal()const noexcept{return goal_;}
    constexpr bool feedback_pending()const noexcept{return pending_.active;}
    constexpr Epoch latest_epoch()const noexcept{return latest_;}

    constexpr bool begin(const SelfFact& self)noexcept {
        if(!self.actual(self.epoch)||route_owner_detail::before(self.epoch,latest_))return false;
        if(self.epoch!=latest_)need_=RouteNeed::arrival;
        latest_=self.epoch;return true;
    }

    // Plain input/observation epochs do not reset the semantic goal, exclusions
    // or custody. Arrival is an instruction to run the fresh service resolver,
    // never proof that a Basic, capture or tribute has happened.
    constexpr bool observe(const SelfFact& self,const RouteFrame& frame)noexcept {
        if(!frame_valid(self,frame))return false;
        latest_=self.epoch;apply_frame(frame);return true;
    }
    constexpr bool observe_custody(const SelfFact& self)noexcept {
        if(!self.actual(self.epoch)||!self.gems_actual||!self.custody_actual||
           self.gems<0||route_owner_detail::before(self.epoch,custody_epoch_))return false;
        custody_epoch_=self.epoch;custody_=self.gems;custody_known_=true;
        return true;
    }
    constexpr bool custody_known()const noexcept{return custody_known_;}
    constexpr bool custody_owed()const noexcept{return custody_known_&&custody_>0;}
    constexpr int custody()const noexcept{return custody_;}
    constexpr Epoch custody_epoch()const noexcept{return custody_epoch_;}

    // This records accepted physical truth even when following feedback fails.
    // A successful move alone creates no service-progress or egress receipt.
    constexpr bool move_applied(const RouteMove& move)noexcept {
        if(!active_||pending_.active||move.owner!=goal_.token||!move.serial||
           move.serial<=last_move_||!move.applied_actual||move.from==move.to||
           !route_owner_detail::fresh(move.before)||route_owner_detail::before(move.after,move.before)||
           route_owner_detail::before(move.before,latest_))return false;
        pending_={};pending_.active=true;pending_.move=move;
        pending_.comparable=have_frame_&&frame_.epoch==move.before&&frame_.pose==move.from&&
            ready(frame_)&&frame_.goal==goal_;
        pending_.before=frame_;pending_.best_known=best_known_;pending_.best=best_distance_;
        last_move_=move.serial;++physical_edges_;latest_=move.after;return true;
    }
    // An attempted move with unresolved effect owes readback, but is not an
    // accepted edge and cannot receive distance/service credit retroactively.
    constexpr bool move_unresolved(const RouteMove& move)noexcept {
        if(!active_||pending_.active||move.owner!=goal_.token||!move.serial||
           move.serial<=last_move_||move.applied_actual||move.from==move.to||
           !route_owner_detail::fresh(move.before)||route_owner_detail::before(move.after,move.before)||
           route_owner_detail::before(move.before,latest_))return false;
        pending_={};pending_.active=true;pending_.move=move;
        last_move_=move.serial;latest_=move.after;return true;
    }
    constexpr bool close_feedback(RouteFeedback feedback,const SelfFact& self,const RouteFrame& frame)noexcept {
        if(!pending_.active||feedback.serial!=pending_.move.serial||!feedback.exact_ack||
           !self.actual(self.epoch)||route_owner_detail::before(self.epoch,pending_.move.after)||
           route_owner_detail::before(self.epoch,latest_))return false;
        const bool matched=frame_valid(self,frame);
        const bool progress=matched&&pending_.comparable&&self.epoch==pending_.move.after&&
            self.pose==pending_.move.to&&ready(frame)&&frame.field_generation==pending_.before.field_generation&&
            frame.distance<pending_.before.distance&&(!pending_.best_known||frame.distance<pending_.best);
        if(progress){++distance_progress_;last_progress_=self.epoch;}
        pending_.active=false;latest_=self.epoch;
        if(matched)apply_frame(frame);else{have_frame_=false;need_=RouteNeed::observe;}
        return true;
    }

    // Prevalidation rejection is not a raw SDK attempt. Both retain an owed
    // continuation, and these dated omissions never become obstacle authority.
    constexpr bool reject(const RouteRejection& rejection)noexcept {
        if(!active_||pending_.active||rejection.owner!=goal_.token||!rejection.decision||
           rejection.decision<=last_rejection_||!route_owner_detail::fresh(rejection.epoch)||
           unsigned(rejection.kind)>unsigned(RouteRejectionKind::unknown)||
           route_owner_detail::before(rejection.epoch,latest_))return false;
        last_rejection_=rejection.decision;latest_=rejection.epoch;
        if(rejection.raw_attempt)++raw_refusals_;else ++prevalidation_rejections_;
        unsigned at=exclusion_cursor_;bool found=false;
        for(unsigned i=0;i<exclusions_.size();++i)if(exclusions_[i].active&&
            exclusions_[i].value.owner==rejection.owner&&exclusions_[i].value.from==rejection.from&&
            exclusions_[i].value.to==rejection.to){at=i;found=true;break;}
        if(!found){
            if(exclusions_[at].active)exclusions_truncated_=true;
            exclusion_cursor_=(exclusion_cursor_+1)%exclusions_.size();
        }
        exclusions_[at]={rejection,true};need_=RouteNeed::blocked;return true;
    }
    constexpr bool release_exclusion(Point from,Point to,Epoch actual_newer)noexcept {
        bool released=false;
        for(auto& omitted:exclusions_)if(omitted.active&&omitted.value.owner==goal_.token&&
            omitted.value.from==from&&omitted.value.to==to&&
            route_owner_detail::before(omitted.value.epoch,actual_newer)){
            omitted.active=false;released=true;
        }
        return released;
    }
    constexpr unsigned exclusions(Point from,std::span<Point> out)const noexcept {
        unsigned count=0;
        for(const auto& omitted:exclusions_)if(omitted.active&&omitted.value.owner==goal_.token&&
            omitted.value.from==from&&count<out.size())out[count++]=omitted.value.to;
        return count;
    }
    constexpr bool exclusions_truncated()const noexcept{return exclusions_truncated_;}

    // These are actual effect/completion events supplied by the service owner.
    // Work effect and Bank START are progress, not completed group/bank payout.
    // No event changes observed custody or fabricates an amount submitted.
    constexpr bool service_actual(const RouteServiceEvent& event)noexcept {
        if(!active_||pending_.active||event.owner!=goal_.token||!event.serial||
           event.serial<=last_service_||!event.actual||!route_owner_detail::fresh(event.epoch)||
           route_owner_detail::before(event.epoch,latest_)||!compatible(event.kind)||
           ((event.kind==RouteServiceKind::work_effect||event.kind==RouteServiceKind::bank_start)&&!event.exact_ack))return false;
        last_service_=event.serial;latest_=last_progress_=event.epoch;++service_progress_;
        const bool complete=(goal_.purpose==RoutePurpose::work&&event.kind==RouteServiceKind::work_complete)||
            (goal_.purpose==RoutePurpose::capture&&event.kind==RouteServiceKind::capture)||
            (goal_.purpose==RoutePurpose::bank&&event.kind==RouteServiceKind::bank_complete)||
            (goal_.purpose==RoutePurpose::exit&&event.kind==RouteServiceKind::egress_closed)||
            (goal_.purpose==RoutePurpose::recover&&event.kind==RouteServiceKind::recovered);
        if(complete){active_=false;need_=RouteNeed::none;replacement_requested_=false;}
        else need_=RouteNeed::arrival;
        return true;
    }
    constexpr std::uint64_t physical_edges()const noexcept{return physical_edges_;}
    constexpr std::uint64_t distance_progress()const noexcept{return distance_progress_;}
    constexpr std::uint64_t service_progress()const noexcept{return service_progress_;}
    constexpr std::uint64_t raw_refusals()const noexcept{return raw_refusals_;}
    constexpr std::uint64_t prevalidation_rejections()const noexcept{return prevalidation_rejections_;}
    constexpr Epoch last_progress_epoch()const noexcept{return last_progress_;}

private:
    struct PendingMove {RouteMove move{};RouteFrame before{};int best{};bool active{},comparable{},best_known{};};
    struct Omitted {RouteRejection value{};bool active{};};
    RouteGoal goal_{};RouteFrame frame_{};RouteFunding funding_{};PendingMove pending_{};
    std::array<Omitted,max_exclusions> exclusions_{};
    Epoch latest_{},last_progress_{},custody_epoch_{};
    std::uint64_t last_move_{},last_service_{},last_rejection_{},last_funding_{},best_generation_{};
    std::uint64_t physical_edges_{},distance_progress_{},service_progress_{},raw_refusals_{},prevalidation_rejections_{};
    RouteNeed need_{RouteNeed::none};unsigned exclusion_cursor_{};int best_distance_{},custody_{};
    bool active_{},replacement_requested_{},have_frame_{},best_known_{},custody_known_{},exclusions_truncated_{};
    constexpr void install(RouteGoal goal)noexcept {
        goal_=goal;active_=true;replacement_requested_=false;have_frame_=best_known_=false;best_generation_=0;
        need_=RouteNeed::arrival;funding_={};
    }
    constexpr bool funding_valid(const RouteFunding& value,std::uint64_t owner)const noexcept {
        return value.owner==owner&&value.serial>last_funding_&&route_owner_detail::fresh(value.epoch)&&
            unsigned(value.need)<=unsigned(RouteNeed::transfer)&&
            !route_owner_detail::before(value.epoch,latest_)&&value.complete_continuation&&value.reserved;
    }
    constexpr bool compatible(RouteServiceKind kind)const noexcept {
        switch(goal_.purpose){
        case RoutePurpose::work:return kind==RouteServiceKind::work_effect||kind==RouteServiceKind::work_complete;
        case RoutePurpose::capture:return kind==RouteServiceKind::capture;
        case RoutePurpose::bank:return kind==RouteServiceKind::bank_start||kind==RouteServiceKind::bank_complete;
        case RoutePurpose::exit:return kind==RouteServiceKind::egress_closed;
        case RoutePurpose::recover:return kind==RouteServiceKind::egress_closed||kind==RouteServiceKind::recovered;
        }return false;
    }
    constexpr bool frame_valid(const SelfFact& self,const RouteFrame& value)const noexcept {
        return active_&&self.actual(self.epoch)&&value.goal==goal_&&value.epoch==self.epoch&&
            value.pose==self.pose&&!route_owner_detail::before(value.epoch,latest_);
    }
    static constexpr bool ready(const RouteFrame& value)noexcept {
        return value.field_matches&&value.field_generation&&value.status==NavigationStatus::ready&&value.distance>=0;
    }
    constexpr void apply_frame(const RouteFrame& value)noexcept {
        if(value.field_matches&&value.field_generation&&best_generation_!=value.field_generation){
            best_generation_=value.field_generation;best_known_=false;
        }
        if(ready(value)){
            if(!best_known_){best_known_=true;best_distance_=value.distance;}
            else if(value.distance<best_distance_){best_distance_=value.distance;}
        }
        frame_=value;have_frame_=true;
        if(value.geometric_arrival||(ready(value)&&value.distance==0))need_=RouteNeed::arrival;
        else if(!value.field_matches)need_=RouteNeed::prepare;
        else if(value.status==NavigationStatus::ready)need_=ready(value)?(value.nominees_available?RouteNeed::move:RouteNeed::blocked):RouteNeed::observe;
        else if(value.status==NavigationStatus::idle||value.status==NavigationStatus::preparing)need_=RouteNeed::prepare;
        else if(value.status==NavigationStatus::unreachable)need_=RouteNeed::unreachable;
        else need_=RouteNeed::observe;
    }
};

} // namespace mosaic::native_service
