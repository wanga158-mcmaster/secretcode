#pragma once
// A selected NoPurchase cash continuation. This owner grants no movement,
// channel or return permission: each backend command must revalidate the fresh
// frame and its complete CURRENT-HP exposure/actual-Trail obligation. Planned
// points and clocks never become actual custody, movement or bank credit.
#include <cmath>
#include <cstdint>

namespace tt::assignment_cash_service {
struct Point {int x{-1},y{-1};friend bool operator==(Point,Point)=default;};
enum class Mode {Dormant,ToBank,Channel,Readback,ToHome,Heal,Redeploy,Reobserve,Returning,Done};
enum class Goal {Bank,Home,Perch};
enum class Read {Unavailable,Fresh,Stale};
enum class Life {Unknown,Alive,Reward,Respawning};
// EmptyObserved is usable physical custody readback, not exclusive actor bank
// attribution. Confirmed may include a team-total check; this module never
// credits banked gems, including when peers can submit in the same interval.
enum class BankReadback {Pending,EmptyObserved,Interrupted,Confirmed};
enum class CommandStatus {Applied,Spent,Refused,Denied,NeedCompute,Stale,NeedContact,Refuted,NoSupport};
enum class Outcome {Idle,Handled,NeedRoute,NeedObservation,NeedResolver,Contact,WorkReady,CaptureReady,Revalue,Fallback};
enum class Reason {None,Premise,Contact,Interrupted,NoProgress,Support,Refused,Stale,UnexpectedCustody,Refuted};
struct Programme {
    std::uint64_t id{};int group{-1};Point bank_stand,altar,home,perch;
    // Caller derives these finite bounds from the actual carried bag/runtime
    // channel settings and owned spawn healing. They are withdrawal deadlines,
    // NEVER evidence that an operation completed or HP/resources changed.
    std::uint32_t channel_inputs{},heal_inputs{},route_wait_inputs{2};
};
struct Frame {
    Read read{Read::Unavailable};int epoch{-1};std::uint64_t observation{},programme{};
    Point pos;float hp{},max_hp{};std::uint32_t gems{},gold{};
    bool alive{},can_move{},can_submit{},channeling{},other_channel{};
    bool own_spawn{},quiet{},enemy_known{},enemy{},programme_applicable{};
    // Each bool refers to a FRESH complete guard, not a cached forecast:
    // progress pays the next edge, bounded refusal/wait and complete return;
    // channel pays its entire remaining channel and complete return;
    // wait pays ONE further input plus complete actual reverse/escape;
    // return pays the actual reverse/escape from the CURRENT pose/HP.
    bool progress_funded{},channel_funded{},wait_funded{},return_funded{};
    std::uint32_t channel_remaining{}; // Actual SDK read; channel_funded prices this remaining suffix, not past damage.
    Goal route_goal{Goal::Bank};bool route_ready{};int distance{-1};std::uint64_t route{};
    // Before route readiness, work must monotonically advance within this
    // declared finite retained preparation. A date/hash change is not progress.
    std::uint64_t preparation_work{},preparation_limit{};
    BankReadback bank_readback{BankReadback::Pending};
    Read life_read{Read::Unavailable};Life life{Life::Unknown};
    // Absolute lower-bound timing comes from an actual named group read, not
    // a shifted conditional forecast. It does not promise Alive on arrival.
    bool renewal_time_bound{};int renewal_not_before{-1};std::uint32_t redeploy_inputs{};
};
struct Command {
    CommandStatus status{CommandStatus::NoSupport};
    bool attempted{};Point after;int epoch{-1},distance_after{-1};
    bool post_fresh{},quiet_after{},channeling_after{};
    bool actual_pose_known{}; // Spatial readback alone never commits an ordinary Trail edge.
};
struct State {
    Mode mode{Mode::Dormant};Reason reason{Reason::None};Programme plan;
    Point expected;std::uint32_t start_gems{};
    bool submit_accepted{},channel_seen{},actual_empty_readback{},physical_refutation{};
    int last_command_epoch{-1},last_wait_epoch{-1},channel_start{-1},heal_start{-1};
    std::uint32_t waits{};std::uint64_t contact_after{},route{},preparation_work{},preparation_limit{};
    int distance{-1};Goal route_goal{Goal::Bank};
    void reset(){*this=State{};}
};
inline bool selected_home_heal(const State& s)
{return s.mode==Mode::ToHome||s.mode==Mode::Heal||s.mode==Mode::Redeploy;}
inline bool active(const State& s){return s.mode!=Mode::Dormant&&s.mode!=Mode::Done;}
inline bool valid(Point p){return p.x>=0&&p.y>=0;}
inline bool valid_body(const Frame& f)
{return f.alive&&std::isfinite(f.hp)&&std::isfinite(f.max_hp)&&f.hp>0&&f.max_hp>0&&f.hp<=f.max_hp;}
inline bool arm(State& s,const Programme& p,const Frame& f)
{
    if(active(s)||!p.id||p.group<0||!valid(p.bank_stand)||!valid(p.altar)||!valid(p.home)||!valid(p.perch)||
       !p.channel_inputs||!p.heal_inputs||f.read!=Read::Fresh||f.epoch<0||!f.observation||
       f.programme!=p.id||!f.programme_applicable||!valid_body(f)||!f.gems||f.channeling||f.other_channel)return false;
    s.reset();s.plan=p;s.mode=Mode::ToBank;s.expected=f.pos;return true;
}
inline void returning(State& s,Reason reason)
{s.mode=Mode::Returning;s.reason=reason;s.route=0;s.distance=-1;}
inline void reset_route(State& s)
{s.route=0;s.distance=-1;s.waits=0;s.last_wait_epoch=-1;s.preparation_work=s.preparation_limit=0;}
inline bool wait_once(State& s,const Frame& f)
{
    if(!f.wait_funded)return false;
    if(s.last_wait_epoch!=f.epoch){s.last_wait_epoch=f.epoch;++s.waits;}
    return s.waits<=s.plan.route_wait_inputs;
}
inline Point destination(const State& s,Goal g)
{return g==Goal::Bank?s.plan.bank_stand:g==Goal::Home?s.plan.home:s.plan.perch;}
inline int separation(Point a,Point b)
{const int x=a.x>b.x?a.x-b.x:b.x-a.x,y=a.y>b.y?a.y-b.y:b.y-a.y;return x>y?x:y;}

// Backend obligations (one command at most per dispatch/owner input):
//   fresh(frame) verifies epoch/pose/read completion immediately before use.
//   advance(frame, goal, destination), start_submit(frame, altar),
//   cancel_submit(frame), retreat(frame) each execute at most ONE physical
//   command and return actual post-effect observations. Applied movement alone
//   commits the real Trail. No method may silently substitute another goal.
// A refused command invalidates old negative contact support. observation must
// be a later completed contact read before another NPC-only command is admitted.
template<class Backend>
Outcome dispatch(State& s,const Frame& f,Backend& backend)
{
    if(s.mode==Mode::Dormant||s.mode==Mode::Done)return Outcome::Idle;
    if(f.read!=Read::Fresh||f.epoch<0||!f.observation||!backend.fresh(f)) {
        returning(s,Reason::Stale);return Outcome::Handled;
    }
    if(!valid_body(f)){s.reset();return Outcome::Fallback;}
    if(f.programme!=s.plan.id||!f.programme_applicable||f.pos!=s.expected)
        returning(s,Reason::Premise);
    if(s.contact_after) {
        if(f.observation<=s.contact_after)return Outcome::Contact;
        s.contact_after=0;
    }
    const auto command=[&](Command c,bool movement)->Outcome {
        if(c.attempted)s.last_command_epoch=c.epoch>f.epoch?c.epoch:f.epoch;
        if(c.actual_pose_known&&valid(c.after))s.expected=c.after;
        if(c.status==CommandStatus::Applied) {
            if(valid(c.after))s.expected=c.after; // Retain the actual accepted after-pose even on support loss.
            if(!c.attempted||!valid(c.after)||(movement?separation(f.pos,c.after)!=1:c.after!=f.pos)) {
                returning(s,Reason::Premise);return Outcome::NeedResolver;
            }
            if(!c.post_fresh||c.epoch!=f.epoch){returning(s,Reason::Stale);return Outcome::Handled;}
            s.waits=0;s.last_wait_epoch=-1;
            return Outcome::Handled;
        }
        if(c.status==CommandStatus::Refused||c.status==CommandStatus::NeedContact) {
            s.contact_after=f.observation;returning(s,Reason::Refused);return Outcome::Contact;
        }
        if(c.status==CommandStatus::Refuted) {
            s.physical_refutation=true;returning(s,Reason::Refuted);return Outcome::NeedResolver;
        }
        if(c.status==CommandStatus::Stale){returning(s,Reason::Stale);return Outcome::Handled;}
        if(c.status==CommandStatus::NeedCompute||c.status==CommandStatus::Spent) {
            if(wait_once(s,f))return Outcome::Handled;
            returning(s,Reason::Support);return Outcome::NeedResolver;
        }
        returning(s,Reason::Support);return Outcome::NeedResolver;
    };
    const auto cancel=[&]()->Outcome {
        if(s.last_command_epoch==f.epoch)return Outcome::Handled;
        const auto c=backend.cancel_submit(f);
        const auto out=command(c,false);
        if(c.status==CommandStatus::Applied&&c.post_fresh&&c.epoch==f.epoch)
            returning(s,s.reason==Reason::None?Reason::Interrupted:s.reason);
        return out;
    };
    if(!f.enemy_known||f.enemy) {
        returning(s,Reason::Contact);
        if(f.channeling&&s.submit_accepted){cancel();return Outcome::Contact;}
        return Outcome::Contact;
    }
    if(f.other_channel){returning(s,Reason::Premise);return Outcome::Fallback;}
    if(s.last_command_epoch==f.epoch)return f.quiet||f.wait_funded?Outcome::Handled:Outcome::NeedResolver;
    const auto withdraw=[&]()->Outcome {
        if(f.channeling) {
            if(!s.submit_accepted)return Outcome::Fallback;
            return cancel();
        }
        if(f.quiet){s.mode=Mode::Done;return Outcome::Fallback;}
        if(!f.return_funded)return Outcome::NeedResolver;
        if(!f.can_move)return wait_once(s,f)?Outcome::Handled:Outcome::NeedResolver;
        if(!backend.fresh(f))return Outcome::Handled;
        const auto c=backend.retreat(f);const auto out=command(c,true);
        if(c.status==CommandStatus::Applied&&c.post_fresh&&c.epoch==f.epoch&&c.quiet_after)
        {s.mode=Mode::Done;return Outcome::Fallback;}
        return out;
    };
    if(s.mode==Mode::Returning)return withdraw();
    if((s.mode==Mode::ToHome||s.mode==Mode::Heal||s.mode==Mode::Redeploy||s.mode==Mode::Reobserve)&&f.gems!=0) {
        returning(s,Reason::UnexpectedCustody);return withdraw();
    }
    // Pure phase transitions can share a frame; no loop can issue two commands.
    for(unsigned transition=0;transition<5;++transition) {
        if(s.mode==Mode::Channel||s.mode==Mode::Readback) {
            if(f.pos!=s.plan.bank_stand||!s.submit_accepted){returning(s,Reason::Premise);return withdraw();}
            if(f.channeling) {
                s.channel_seen=true;s.mode=Mode::Channel;
                if(f.channel_funded&&static_cast<std::uint64_t>(f.epoch-s.channel_start)<=s.plan.channel_inputs)return Outcome::Handled;
                returning(s,Reason::Support);return cancel();
            }
            if(f.bank_readback==BankReadback::Interrupted||f.gems>0) {
                returning(s,Reason::Interrupted);return withdraw();
            }
            if(s.channel_seen&&f.gems==0&&(f.bank_readback==BankReadback::EmptyObserved||f.bank_readback==BankReadback::Confirmed)) {
                s.actual_empty_readback=true;s.mode=Mode::ToHome;reset_route(s);continue;
            }
            s.mode=Mode::Readback;
            if(wait_once(s,f))return Outcome::Handled;
            returning(s,Reason::Support);return withdraw();
        }
        if(s.mode==Mode::Heal) {
            if(f.pos!=s.plan.home||!f.own_spawn){returning(s,Reason::Premise);return withdraw();}
            if(f.hp>=f.max_hp) {
                if(!f.renewal_time_bound||f.renewal_not_before<0)
                    {s.mode=Mode::Done;return Outcome::Revalue;}
                if(static_cast<std::uint64_t>(f.epoch)+f.redeploy_inputs<static_cast<std::uint64_t>(f.renewal_not_before))
                    return f.quiet&&f.wait_funded?Outcome::Handled:Outcome::NeedResolver;
                s.mode=Mode::Redeploy;reset_route(s);continue;
            }
            if(f.quiet&&f.wait_funded&&static_cast<std::uint64_t>(f.epoch-s.heal_start)<=s.plan.heal_inputs)return Outcome::Handled;
            returning(s,Reason::Support);return withdraw();
        }
        if(s.mode==Mode::Reobserve) {
            if(f.pos!=s.plan.perch){returning(s,Reason::Premise);return withdraw();}
            if(f.life_read!=Read::Fresh) {
                if(wait_once(s,f))return Outcome::NeedObservation;
                returning(s,Reason::Support);return withdraw();
            }
            s.mode=Mode::Done;
            return f.life==Life::Alive?Outcome::WorkReady:f.life==Life::Reward?Outcome::CaptureReady:Outcome::Revalue;
        }
        const auto goal=s.mode==Mode::ToBank?Goal::Bank:s.mode==Mode::ToHome?Goal::Home:Goal::Perch;
        if(f.pos==destination(s,goal)) {
            if(goal==Goal::Home){s.mode=Mode::Heal;s.heal_start=f.epoch;continue;}
            if(goal==Goal::Perch){s.mode=Mode::Reobserve;continue;}
            if(!f.gems){returning(s,Reason::UnexpectedCustody);return withdraw();}
            if(!f.can_submit||f.channeling||!f.channel_funded){returning(s,Reason::Support);return withdraw();}
            if(!backend.fresh(f))return Outcome::Handled;
            const auto c=backend.start_submit(f,s.plan.altar);const auto out=command(c,false);
            if(c.status==CommandStatus::Applied&&c.attempted&&c.after==f.pos) {
                s.submit_accepted=true;s.start_gems=f.gems;s.channel_seen=c.channeling_after;
                s.channel_start=c.epoch>f.epoch?c.epoch:f.epoch;
                if(c.post_fresh&&c.epoch==f.epoch){s.mode=Mode::Channel;reset_route(s);}
            }
            return out;
        }
        if(!f.route_ready||f.route_goal!=goal||!f.route||f.distance<=0) {
            if(f.quiet&&f.preparation_limit&&f.preparation_work<=f.preparation_limit&&
               (!s.preparation_limit||s.preparation_limit==f.preparation_limit)) {
                if(f.preparation_work>s.preparation_work){s.waits=0;s.preparation_work=f.preparation_work;}
                s.preparation_limit=f.preparation_limit;
                if(wait_once(s,f))return Outcome::NeedRoute;
            } else if(wait_once(s,f))return Outcome::Handled;
            returning(s,Reason::NoProgress);return withdraw();
        }
        if(s.route&&(s.route_goal!=goal||s.route!=f.route||s.distance!=f.distance)) {
            returning(s,Reason::Premise);return withdraw();
        }
        s.route=f.route;s.route_goal=goal;s.distance=f.distance;
        if(!f.progress_funded||!f.can_move){returning(s,Reason::Support);return withdraw();}
        if(!backend.fresh(f))return Outcome::Handled;
        const auto c=backend.advance(f,goal,destination(s,goal));const auto out=command(c,true);
        if(c.status==CommandStatus::Applied&&c.post_fresh&&c.epoch==f.epoch) {
            if(c.distance_after<0||c.distance_after>=f.distance){returning(s,Reason::NoProgress);return Outcome::NeedResolver;}
            s.distance=c.distance_after;
        }
        return out;
    }
    returning(s,Reason::NoProgress);return withdraw();
}
} // namespace tt::assignment_cash_service
