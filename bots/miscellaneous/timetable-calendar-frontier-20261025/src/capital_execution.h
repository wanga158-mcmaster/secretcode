#pragma once
// Named conditional capital cycle contracts. No SDK state, commands or native
// authority are supplied by this header; every physical transition is refreshed
// by the independent adapter.
#include "production_investment.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace tt::capital_execution {
using Point=production_investment::Point;

enum class Phase {
    Idle,Walls,QuietField,QuietExport,CaptureField,CaptureExport,
    BankField,BankExport,Value,Recall,AwaitRecall,Shop,Return,Arrived,Retreat
};
enum class Reason {
    None,Unsupported,NoProfit,Stale,Enemy,ActorUnavailable,Changed,
    Blocked,Cancelled,Readback,Computation
};

struct Key {
    int group{-1};
    Point perch;
    std::uint32_t gold{};
    float focus{},strength{},max_hp{},hp{};
    std::size_t member_count{},inventory_count{};
    std::array<float,production_investment::max_members> member_hp{};
    std::array<std::uint32_t,128> inventory{};
};

inline bool meaningful_key(const Key& key) noexcept {
    if(key.group<0 || key.perch.x<0 || key.perch.y<0 ||
       key.member_count==0 || key.member_count>key.member_hp.size() ||
       key.inventory_count>key.inventory.size() ||
       !std::isfinite(key.focus) || key.focus<0 ||
       !std::isfinite(key.strength) || key.strength<0 ||
       !std::isfinite(key.max_hp) || key.max_hp<=0 ||
       !std::isfinite(key.hp) || key.hp<0 || key.hp>key.max_hp)return false;
    for(std::size_t i=0;i<key.member_count;++i)
        if(!std::isfinite(key.member_hp[i]) || key.member_hp[i]<0)return false;
    return true;
}

// Only the declared active member/inventory prefixes are meaningful. The caller
// owns their exact observed provenance and immutable SDK metadata. Owner-round
// and remaining cooldown are not key fields: a later round alone does not
// invalidate a retained negative physical/economic state.
inline bool same_key(const Key& a,const Key& b) noexcept {
    if(!meaningful_key(a) || !meaningful_key(b) ||
       a.group!=b.group || a.perch.x!=b.perch.x || a.perch.y!=b.perch.y ||
       a.gold!=b.gold || a.focus!=b.focus || a.strength!=b.strength ||
       a.max_hp!=b.max_hp || a.hp!=b.hp ||
       a.member_count!=b.member_count || a.inventory_count!=b.inventory_count)return false;
    for(std::size_t i=0;i<a.member_count;++i)
        if(a.member_hp[i]!=b.member_hp[i])return false;
    for(std::size_t i=0;i<a.inventory_count;++i)
        if(a.inventory[i]!=b.inventory[i])return false;
    return true;
}

struct Negative {
    bool valid{};
    Key key;
    Reason reason{Reason::None};
    bool matches(const Key& candidate) const noexcept {
        return valid && same_key(key,candidate);
    }
    void remember(const Key& candidate,Reason why) noexcept {
        if(!meaningful_key(candidate)){clear();return;}
        key=candidate;reason=why;valid=true;
    }
    void clear() noexcept {
        valid=false;key=Key{};reason=Reason::None;
    }
};

enum class ReturnClass { Quiet,Charged };
enum class ReturnDisposition {
    Unknown,Ready,Arrived,NeedNewInput,Displaced,Invalid,Unsafe,Blocked
};
inline constexpr std::size_t max_return_steps=512;

namespace return_detail {
inline bool add_upper(double a,double b,double& result) noexcept {
    if(!std::isfinite(a) || !std::isfinite(b) || a<0 || b<0)return false;
    if(a==0){result=b;return true;}
    if(b==0){result=a;return true;}
    const auto sum=a+b;
    if(!std::isfinite(sum))return false;
    result=std::nextafter(sum,std::numeric_limits<double>::infinity());
    return std::isfinite(result);
}
inline bool same(Point a,Point b) noexcept {return a.x==b.x && a.y==b.y;}
} // namespace return_detail

// A fixed ordinary-walk word has one charged End at EACH destination, including
// the terminal destination. The origin is excluded from that sum. raw_upper[i]
// is a complete, immutable-model upper bound at points[i]. The origin must be
// quiet: an executed prefix then has a physically named quiet recovery endpoint.
// reverse_prefix[i] charges every reverse landing back to that origin, excluding
// the current point. The global maximum volley reserves ONE failed/no-move End.
// Nonconsecutive
// repeated points are allowed and charged on every visit; a wait is not a step.
//
// Native premises remain with the caller: all possible fixed NPC sources are
// represented, raw potential upper-bounds actual HP loss, no new hostile source
// or displacement appears, and each landing consumes at most one camp interval.
// In particular, upward double sums do not themselves prove a universal bound
// for repeated float32 HP subtraction. The adapter must establish that numerical
// premise, or account for native rounding separately. For integer raw NPC hit s
// and surviving float HP<=2^24, HP-s is exactly representable; monotonic float
// rounding of damage<=s cannot lose more HP than s. No beneficial mitigation,
// shield, stun or sleep is
// credited here. zero_incoming_through_continuation applies only to the separate
// POST-return job calendar, never to this charged prefix.
struct ReturnWord {
    std::array<Point,max_return_steps+1> points{};
    std::array<double,max_return_steps+1> raw_upper{},suffix{},reserve{},reverse_prefix{};
    double global_reserve{};
    std::size_t count{};
    bool valid{};
    ReturnClass kind{ReturnClass::Quiet};

    // O(point_count), no allocation. A zero-edge word is supported only when its
    // sole point is quiet. Failures leave no reusable word authority.
    bool build(const std::array<Point,max_return_steps+1>& path,
               const std::array<double,max_return_steps+1>& upper,
               std::size_t point_count,int width,int height) noexcept {
        valid=false;count=0;global_reserve=0;kind=ReturnClass::Quiet;
        if(point_count==0 || point_count>points.size() || width<=0 || height<=0)return false;
        for(std::size_t i=0;i<point_count;++i) {
            const auto p=path[i];
            if(p.x<0 || p.y<0 || p.x>=width || p.y>=height ||
               !std::isfinite(upper[i]) || upper[i]<0)return false;
            if(i) {
                const auto dx=static_cast<std::int64_t>(p.x)-path[i-1].x;
                const auto dy=static_cast<std::int64_t>(p.y)-path[i-1].y;
                if(dx<-1 || dx>1 || dy<-1 || dy>1 || (dx==0 && dy==0))return false;
            }
        }
        if(upper[0]!=0 || upper[point_count-1]!=0)return false;
        for(std::size_t i=0;i<point_count;++i) {
            points[i]=path[i];raw_upper[i]=upper[i];
            if(upper[i]>0)kind=ReturnClass::Charged;
        }
        const auto last=point_count-1;suffix[last]=0;reserve[last]=0;
        for(std::size_t i=last;i>0;--i) {
            if(!return_detail::add_upper(raw_upper[i],suffix[i],suffix[i-1]))return false;
            reserve[i-1]=std::max(raw_upper[i-1],reserve[i]);
        }
        reverse_prefix[0]=0;
        for(std::size_t i=1;i<point_count;++i)
            if(!return_detail::add_upper(reverse_prefix[i-1],raw_upper[i-1],reverse_prefix[i]))return false;
        global_reserve=reserve[0];
        for(std::size_t i=0;i<point_count;++i) {
            if(i==last)continue;
            double next_reverse{},threshold{};
            if(!return_detail::add_upper(raw_upper[i+1],reverse_prefix[i+1],next_reverse) ||
               !return_detail::add_upper(std::max({suffix[i],reverse_prefix[i],next_reverse}),
                                         global_reserve,threshold))return false;
        }
        count=point_count;valid=true;return true;
    }
};

struct ReturnProgress {
    std::size_t index{}; // number of actual accepted ordinary moves
    std::uint64_t last_move_input{};
    bool has_moved{};
};
struct ReturnObservation {
    Point actual;
    double hp{},incoming_multiplier{},raw_upper{};
    std::uint64_t input{},fenced_input{};
    bool exact{}; // caller refreshed body, point potential and owner/input identity
};
struct ReturnCheck {
    ReturnDisposition disposition{ReturnDisposition::Unknown};
    Point next;
    double remaining_charge{},reverse_charge{},transition_reserve{},required_hp{};
};

// O(1). This is conditional envelope validation, not movement permission. The
// caller still queries alive/channel/stun, movement, destination legality and
// hostile occupancy immediately before its SDK command. A nonzero current
// potential is permitted ONLY while this separate return word is being applied.
// The prospective term funds one next landing AND its actual reverse prefix.
// A later fresh forward check may reject; the already funded immediate reverse
// then owns recovery. This is not a promise that every future forward decision
// will pass. The global reserve covers at most one extra interval total.
inline ReturnCheck check_return(const ReturnWord& word,const ReturnProgress& progress,
                                const ReturnObservation& observation) noexcept {
    ReturnCheck result;
    if(!word.valid)return result;
    if(word.count==0 || word.count>word.points.size() || progress.index>=word.count) {
        result.disposition=ReturnDisposition::Invalid;return result;
    }
    if(!observation.exact || observation.input!=observation.fenced_input)return result;
    if(!return_detail::same(observation.actual,word.points[progress.index])) {
        result.disposition=ReturnDisposition::Displaced;return result;
    }
    if(!std::isfinite(observation.hp) || observation.hp<=0 ||
       !std::isfinite(observation.incoming_multiplier) || observation.incoming_multiplier<0 ||
       observation.incoming_multiplier>1 || !std::isfinite(observation.raw_upper) ||
       observation.raw_upper<0) {
        result.disposition=ReturnDisposition::Invalid;return result;
    }
    if(observation.raw_upper>word.raw_upper[progress.index])return result;
    result.remaining_charge=word.suffix[progress.index];
    result.reverse_charge=word.reverse_prefix[progress.index];
    // An accepted ordinary move forbids another step or an arrival/job claim in
    // that same input. Tokens must denote this actor's monotonically increasing
    // actual owner inputs, never a planning generation or reused stale epoch.
    if(progress.has_moved && observation.input<=progress.last_move_input) {
        result.disposition=ReturnDisposition::NeedNewInput;return result;
    }
    if(progress.index+1==word.count) {
        if(observation.raw_upper!=0)return result;
        // The actual quiet terminal hands off now. It owes neither another
        // dangerous interval nor a hypothetical reverse trip after completion.
        result.next=observation.actual;result.disposition=ReturnDisposition::Arrived;return result;
    }
    result.transition_reserve=word.global_reserve;
    double next_reverse{};
    if(!return_detail::add_upper(word.raw_upper[progress.index+1],word.reverse_prefix[progress.index+1],next_reverse) ||
       !return_detail::add_upper(std::max({result.remaining_charge,result.reverse_charge,next_reverse}),
                                 result.transition_reserve,result.required_hp)) {
        result.disposition=ReturnDisposition::Invalid;return result;
    }
    if(!(observation.hp>result.required_hp)) {
        result.disposition=ReturnDisposition::Unsafe;return result;
    }
    result.next=word.points[progress.index+1];result.disposition=ReturnDisposition::Ready;
    return result;
}

// Read back an actual SDK result after a Ready check in this SAME fenced input.
// A false result never advances or consumes planned charge. The caller must
// resolve a refusal, unavailable move or low-fuel wait at a positive point; the
// one-volley reserve is not authority for repeated uncharged Ends. Success still
// requires check_return on a later input before the next step or Arrived claim.
inline ReturnDisposition advance_return(const ReturnWord& word,ReturnProgress& progress,
                                        Point actual,bool moved,std::uint64_t input,
                                        std::uint64_t fenced_input) noexcept {
    if(!word.valid)return ReturnDisposition::Unknown;
    if(word.count==0 || word.count>word.points.size() || progress.index>=word.count)
        return ReturnDisposition::Invalid;
    if(input!=fenced_input)return ReturnDisposition::Unknown;
    if(progress.has_moved && input<=progress.last_move_input)return ReturnDisposition::NeedNewInput;
    const auto expected=progress.index+(moved?1u:0u);
    if(expected>=word.count)return ReturnDisposition::Invalid;
    if(!return_detail::same(actual,word.points[expected]))return ReturnDisposition::Displaced;
    if(!moved)return ReturnDisposition::Blocked;
    ++progress.index;progress.last_move_input=input;progress.has_moved=true;
    return ReturnDisposition::Ready;
}

struct Outcome {
    bool handled{},returned{},aborted{},conditional{true},projection_matches{};
    Reason reason{Reason::None};
    Reason recovery_reason{Reason::None};
    bool recovered{},moved{};
    int group{-1};
    Point perch,actual;
    std::uint64_t generation{};
    std::uint32_t decision_round{},return_round{},quoted_calls{},actual_calls{},gold_spent{};
    float focus_before{},focus_after{},projected_focus{};
    std::int64_t same_work_time_saved{};
    ReturnClass return_class{ReturnClass::Quiet};
    std::uint32_t return_steps{},return_steps_completed{},retreat_steps{};
    double return_exposure{},return_reserve{},return_required_hp{};
};

// Exact integer Chebyshev intersection for a single fixed AoE center. Every
// counted HP must be exact, finite and nonnegative; only living positions bound
// the hit geometry. The chosen aim is written only after complete success.
// This establishes geometric coverage, never visibility, readiness, safety,
// future availability, cast authority or a native economic result.
inline bool fixed_aim(
    const std::array<production_investment::Member,production_investment::max_members>& members,
    std::size_t count,Point perch,int width,int height,float cast_range,float radius,Point& aim) noexcept
{
    if(width<=0 || height<=0 || count==0 || count>members.size() ||
       perch.x<0 || perch.y<0 || perch.x>=width || perch.y>=height ||
       !std::isfinite(cast_range) || cast_range<0 ||
       !std::isfinite(radius) || radius<0)return false;
    // No distance between in-map coordinates exceeds this diameter. Clamp in
    // double BEFORE conversion: float(INT_MAX-1) can round out of int range.
    const auto diameter=static_cast<double>(std::max(width,height)-1);
    const auto reach=static_cast<std::int64_t>(std::floor(std::min(static_cast<double>(cast_range),diameter)));
    const auto area=static_cast<std::int64_t>(std::floor(std::min(static_cast<double>(radius),diameter)));
    std::int64_t lo_x=std::max<std::int64_t>(0,static_cast<std::int64_t>(perch.x)-reach);
    std::int64_t hi_x=std::min<std::int64_t>(static_cast<std::int64_t>(width)-1,static_cast<std::int64_t>(perch.x)+reach);
    std::int64_t lo_y=std::max<std::int64_t>(0,static_cast<std::int64_t>(perch.y)-reach);
    std::int64_t hi_y=std::min<std::int64_t>(static_cast<std::int64_t>(height)-1,static_cast<std::int64_t>(perch.y)+reach);
    bool living=false;
    for(std::size_t i=0;i<count;++i) {
        const auto& member=members[i];
        if(!member.hp_exact || !std::isfinite(member.hp) || member.hp<0)return false;
        if(member.hp==0)continue;
        if(member.pos.x<0 || member.pos.y<0 || member.pos.x>=width || member.pos.y>=height)return false;
        living=true;
        lo_x=std::max(lo_x,static_cast<std::int64_t>(member.pos.x)-area);
        hi_x=std::min(hi_x,static_cast<std::int64_t>(member.pos.x)+area);
        lo_y=std::max(lo_y,static_cast<std::int64_t>(member.pos.y)-area);
        hi_y=std::min(hi_y,static_cast<std::int64_t>(member.pos.y)+area);
        if(lo_x>hi_x || lo_y>hi_y)return false;
    }
    if(!living)return false;
    const auto x=std::clamp<std::int64_t>(perch.x,lo_x,hi_x);
    const auto y=std::clamp<std::int64_t>(perch.y,lo_y,hi_y);
    aim={static_cast<int>(x),static_cast<int>(y)};
    return true;
}
} // namespace tt::capital_execution

namespace mb::production::potential { class Field; }
namespace tt {
struct Observation;
struct WorkObservation;
bool capital_active();
bool try_capital_offer(const Observation&,const WorkObservation&,const mb::production::potential::Field&);
capital_execution::Outcome step_capital(const mb::production::potential::Field&);
std::span<const capital_execution::Point> capital_return_prefix();
} // namespace tt
