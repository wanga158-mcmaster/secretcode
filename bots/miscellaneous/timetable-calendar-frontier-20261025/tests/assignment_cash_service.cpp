// Pure composition fixture. Guard booleans deliberately represent separately
// proved CURRENT-HP/all-source/actual-return contracts. No SDK fuel, visibility,
// channel timing, personal bank attribution or native execution is qualified.
#include "../src/assignment_cash_service.h"
#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <vector>

namespace {
namespace c=tt::assignment_cash_service;
int checks{},failures{};
void check(bool value,const char* why){++checks;if(!value){++failures;std::printf("FAIL %s\n",why);}}
struct Body {
    c::Point pos{0,0};int epoch{10};std::uint64_t observation{};
    float hp{100},max_hp{100};std::uint32_t gems{11},gold{40},remaining{};
    bool alive{true},can_move{true},can_submit{true},channel{},enemy{},contact_known{true};
    bool progress_funded{true},channel_funded{true},wait_funded{true},return_funded{true};
    bool programme_applicable{true},report_channel{true},refuse{},reveal_on_refusal{},suspend_after{};
    c::BankReadback readback{c::BankReadback::Pending};c::Read life_read{c::Read::Fresh};c::Life life{c::Life::Alive};
    c::CommandStatus forced{c::CommandStatus::Applied};
    int attempts{},moves{},starts{},cancels{},returns{};std::vector<c::Point> actual_after;
    bool quiet() const{return pos==c::Point{0,0};}
    void tick() {
        ++epoch;can_move=can_submit=true;
        if(!quiet())hp-=2; // A real simulated batch in this fixture, not a prior-prefix debt.
        if(channel&&remaining&&!--remaining){channel=false;gems=0;readback=c::BankReadback::EmptyObserved;}
        if(quiet())hp=std::min(max_hp,hp+20);
    }
};
c::Programme plan(){return {17,3,{2,0},{2,1},{0,0},{4,0},3,5,2};}
c::Goal goal(const c::State& s,const Body& b) {
    if(s.mode==c::Mode::ToHome||s.mode==c::Mode::Heal||
       ((s.mode==c::Mode::Channel||s.mode==c::Mode::Readback)&&!b.channel&&!b.gems))return c::Goal::Home;
    return s.mode==c::Mode::Redeploy||s.mode==c::Mode::Reobserve?c::Goal::Perch:c::Goal::Bank;
}
c::Frame frame(Body& b,const c::State& s) {
    c::Frame f;f.read=c::Read::Fresh;f.epoch=b.epoch;f.observation=++b.observation;f.programme=17;
    f.pos=b.pos;f.hp=b.hp;f.max_hp=b.max_hp;f.gems=b.gems;f.gold=b.gold;
    f.alive=b.alive;f.can_move=b.can_move;f.can_submit=b.can_submit;f.channeling=b.channel;
    f.own_spawn=f.quiet=b.quiet();f.enemy_known=b.contact_known;f.enemy=b.enemy;
    f.programme_applicable=b.programme_applicable;f.progress_funded=b.progress_funded;
    f.channel_funded=b.channel_funded;f.wait_funded=b.wait_funded;f.return_funded=b.return_funded;
    f.channel_remaining=b.remaining;f.route_goal=goal(s,b);f.route_ready=true;
    f.route=100+static_cast<unsigned>(f.route_goal);f.distance=c::separation(b.pos,c::destination(s,f.route_goal));
    f.bank_readback=b.readback;f.life_read=b.life_read;f.life=b.life;
    f.renewal_time_bound=true;f.renewal_not_before=0;f.redeploy_inputs=4;return f;
}
struct Backend {
    Body& b;
    bool fresh(const c::Frame& f) const{return f.epoch==b.epoch&&f.pos==b.pos;}
    c::Command result(c::CommandStatus status,bool attempted=false) {
        return {status,attempted,b.pos,b.epoch,-1,true,b.quiet(),b.channel&&b.report_channel};
    }
    c::Command move(const c::Frame& f,c::Point to,bool back,c::Point destination) {
        if(b.forced!=c::CommandStatus::Applied)return result(b.forced);
        ++b.attempts;
        if(b.refuse){if(b.reveal_on_refusal)b.enemy=true;return result(c::CommandStatus::Refused,true);}
        b.pos=to;b.can_move=false;++b.moves;if(back)++b.returns;b.actual_after.push_back(to);
        auto value=result(c::CommandStatus::Applied,true);value.distance_after=c::separation(to,destination);
        if(b.suspend_after){++b.epoch;value.epoch=b.epoch;value.post_fresh=false;}
        check(f.pos!=b.pos,"only actual accepted movement changes the fixture pose");return value;
    }
    c::Command advance(const c::Frame& f,c::Goal,c::Point destination) {
        auto to=b.pos;to.x+=to.x<destination.x?1:-1;return move(f,to,false,destination);
    }
    c::Command retreat(const c::Frame& f) {
        auto to=b.pos;to.x+=to.x>0?-1:1;return move(f,to,true,{0,0});
    }
    c::Command start_submit(const c::Frame&,c::Point altar) {
        check(altar==plan().altar,"actual submit retains the selected altar identity");
        if(b.forced!=c::CommandStatus::Applied)return result(b.forced);
        ++b.attempts;++b.starts;b.channel=true;b.remaining=3;b.can_submit=false;
        auto value=result(c::CommandStatus::Applied,true);
        if(b.suspend_after){++b.epoch;value.epoch=b.epoch;value.post_fresh=false;}
        return value;
    }
    c::Command cancel_submit(const c::Frame&) {
        if(b.forced!=c::CommandStatus::Applied)return result(b.forced);
        ++b.attempts;++b.cancels;b.channel=false;b.remaining=0;return result(c::CommandStatus::Applied,true);
    }
};
bool arm(c::State& s,Body& b) {
    s.plan=plan();const auto f=frame(b,s);s.reset();return c::arm(s,plan(),f);
}
void complete_actual_chain() {
    c::State s;Body b;b.hp=60;Backend backend{b};check(arm(s,b),"fresh carried custody arms one selected cash owner");
    bool channel_seen=false,empty_seen=false,home_seen=false,heal_seen=false,return_seen=false;
    c::Outcome last=c::Outcome::Idle;
    for(int input=0;input<40&&s.mode!=c::Mode::Done;++input) {
        const auto before=b.attempts;const auto old=s.mode;last=c::dispatch(s,frame(b,s),backend);
        check(b.attempts-before<=1,"one dispatch never hides two physical commands");
        channel_seen|=s.mode==c::Mode::Channel;empty_seen|=s.actual_empty_readback;
        home_seen|=s.mode==c::Mode::ToHome;heal_seen|=s.mode==c::Mode::Heal;
        return_seen|=s.mode==c::Mode::Redeploy;
        if(c::selected_home_heal(s))check(s.mode==c::Mode::ToHome||s.mode==c::Mode::Heal||s.mode==c::Mode::Redeploy,
            "SelectedHomeHeal retains only the real home/heal/redeploy owner");
        if(old==c::Mode::Heal&&s.mode==c::Mode::Redeploy)check(b.hp==b.max_hp,"redeploy requires actual observed full HP");
        if(s.mode!=c::Mode::Done)b.tick();
    }
    check(s.mode==c::Mode::Done&&last==c::Outcome::WorkReady,"complete chain ends at fresh Alive work handoff");
    check(channel_seen&&empty_seen&&home_seen&&heal_seen&&return_seen,"all actual cash/recovery phases are visited");
    check(b.starts==1&&b.cancels==0&&b.gems==0&&b.pos==plan().perch,"accepted submit and actual empty custody precede renewed work");
    check(b.gold==40,"the NoPurchase owner never invents purchases or bank gold");
    check(!c::selected_home_heal(s),"fresh Life handoff ends SelectedHomeHeal scope");
    check(!s.physical_refutation,"the complete chain creates no physical refutation");
}
void absent_or_interrupted_bank_readback() {
    for(int variant=0;variant<3;++variant) {
        c::State s;Body b;b.pos=plan().bank_stand;Backend backend{b};check(arm(s,b),"bank witness starts with fresh actual bag");
        b.report_channel=variant!=2;c::dispatch(s,frame(b,s),backend);
        check(s.submit_accepted&&b.starts==1,"only an accepted actual submit records the start bag");
        b.tick();b.channel=false;b.remaining=0;b.gems=variant==1?11:0;
        b.readback=variant==1?c::BankReadback::Interrupted:variant==2?c::BankReadback::EmptyObserved:c::BankReadback::Pending;
        for(int n=0;n<5&&s.mode!=c::Mode::Done;++n) {
            c::dispatch(s,frame(b,s),backend);check(!s.actual_empty_readback,"timer/empty bag alone or an unobserved channel is not bank readback");
            check(!c::selected_home_heal(s),"unproved or interrupted submission never acquires selected recovery");b.tick();
        }
        check(b.starts==1,"an interrupted or unavailable readback does not silently repeat submit");
    }
}
void refusal_contact_and_same_input_fence() {
    c::State s;Body b;Backend backend{b};check(arm(s,b),"contact test arms");
    b.refuse=b.reveal_on_refusal=true;const auto old=frame(b,s);
    check(c::dispatch(s,old,backend)==c::Outcome::Contact&&b.attempts==1&&b.actual_after.empty(),
        "a refused hidden-occupant move commits no Trail and transfers fresh contact ownership");
    check(c::dispatch(s,old,backend)==c::Outcome::Contact&&b.attempts==1,
        "old negative contact support cannot authorize another forward or reverse command");
    const auto fresh=frame(b,s);
    check(c::dispatch(s,fresh,backend)==c::Outcome::Contact&&b.attempts==1,
        "freshly revealed enemy prevents an NPC-only return command");
}
void stale_commands_preserve_actual_effects() {
    for(bool submit:{false,true}) {
        c::State s;Body b;if(submit)b.pos=plan().bank_stand;Backend backend{b};check(arm(s,b),"suspension test arms");
        b.suspend_after=true;const auto old=frame(b,s);c::dispatch(s,old,backend);
        check(s.mode==c::Mode::Returning&&s.expected==b.pos&&!s.physical_refutation,
            "accepted post-suspension pose is retained without old-frame action authority or verdict");
        check(s.last_command_epoch==b.epoch,"actual post-effect input is conservatively fenced");
        if(submit)check(s.submit_accepted,"accepted submit survives suspension so a later fresh owner can cancel it");
        const auto attempts=b.attempts;c::dispatch(s,frame(b,s),backend);
        check(b.attempts==attempts,"fresh redispatch cannot issue a second physical command in the retained actual input");
    }
}
void charged_channel_loss_cancels_before_return() {
    c::State s;Body b;b.pos=plan().bank_stand;Backend backend{b};check(arm(s,b),"charged channel test arms");
    c::dispatch(s,frame(b,s),backend);b.tick();b.channel_funded=false;
    const auto before=b.attempts;c::dispatch(s,frame(b,s),backend);
    check(b.cancels==1&&b.returns==0&&b.attempts==before+1&&s.mode==c::Mode::Returning,
        "lost full remaining-channel support cancels the actual channel before any return move");
    c::dispatch(s,frame(b,s),backend);check(b.returns==0,"cancel and return never share one physical input");
    b.tick();c::dispatch(s,frame(b,s),backend);check(b.returns==1,"a later freshly paid actual return remains available");
}
void stale_unknown_move_retains_only_observed_pose() {
    struct ObservedStale : Backend {
        explicit ObservedStale(Body& body):Backend{body}{}
        c::Command advance(const c::Frame&,c::Goal,c::Point) {
            // A suspended SDK adapter may observe a changed pose without
            // knowing whether its own ordinary move was accepted.
            ++b.attempts;b.pos={1,0};++b.epoch;
            return {c::CommandStatus::Stale,true,b.pos,b.epoch,-1,false,false,false,true};
        }
    };
    c::State s;Body b;ObservedStale backend{b};check(arm(s,b),"observed stale-pose test arms");
    c::dispatch(s,frame(b,s),backend);
    check(s.mode==c::Mode::Returning&&s.expected==b.pos&&b.moves==0&&b.actual_after.empty(),
        "a stale pose readback preserves return ownership without inventing an accepted Trail edge");
    check(s.last_command_epoch==b.epoch&&!s.physical_refutation,
        "unknown post-suspension action is conservatively fenced and is not a physical verdict");
}
void quiet_home_wait_uses_actual_timing() {
    c::State s;Body b;b.gems=0;s.plan=plan();s.mode=c::Mode::Heal;s.expected=b.pos;
    Backend backend{b};
    for(int input=10;input<26;++input) {
        auto f=frame(b,s);f.renewal_not_before=30;f.redeploy_inputs=4;
        check(c::dispatch(s,f,backend)==c::Outcome::Handled&&s.mode==c::Mode::Heal&&b.attempts==0,
            "actual full HP retains quiet-home ownership until the named renewal departure bound");
        b.tick();
    }
    auto depart=frame(b,s);depart.renewal_not_before=30;depart.redeploy_inputs=4;
    c::dispatch(s,depart,backend);
    check(s.mode==c::Mode::Redeploy&&c::selected_home_heal(s),"paid redeployment starts only at the actual timing bound");
    c::State unknown;unknown.plan=plan();unknown.mode=c::Mode::Heal;unknown.expected=b.pos;
    auto missing=frame(b,unknown);missing.renewal_time_bound=false;
    check(c::dispatch(unknown,missing,backend)==c::Outcome::Revalue&&!c::selected_home_heal(unknown),
        "missing actual timing releases recovery instead of redating a forecast or departing early");
}
void observation_and_preparation_are_paid_and_finite() {
    c::State s;Body b;b.pos=plan().perch;b.gems=0;s.plan=plan();s.mode=c::Mode::Reobserve;s.expected=b.pos;
    b.life_read=c::Read::Unavailable;b.wait_funded=false;Backend backend{b};
    const auto result=c::dispatch(s,frame(b,s),backend);
    check(result!=c::Outcome::NeedObservation&&s.mode==c::Mode::Returning&&b.returns==1,
        "a charged unavailable-Life perch cannot own an unfunded observation yield");
    s={};b=Body{};check(arm(s,b),"finite preparation test arms");
    for(int input=0;input<3;++input) {
        auto f=frame(b,s);f.route_ready=false;f.preparation_limit=3;f.preparation_work=0;
        c::dispatch(s,f,backend);b.tick();
    }
    check(s.mode==c::Mode::Done&&b.attempts==0,"a fixed quiet preparation with no progress deliberately hands back after its paid wait family");
}
void support_and_life_boundaries() {
    for(const auto life:{c::Life::Reward,c::Life::Respawning,c::Life::Unknown}) {
        c::State s;Body b;b.pos=plan().perch;b.gems=0;b.life=life;s.plan=plan();s.mode=c::Mode::Reobserve;s.expected=b.pos;
        Backend backend{b};const auto out=c::dispatch(s,frame(b,s),backend);
        check(out==(life==c::Life::Reward?c::Outcome::CaptureReady:c::Outcome::Revalue),
            "fresh final Life selects capture/revaluation instead of assuming future Alive");
    }
    c::State s;Body b;Backend backend{b};check(arm(s,b),"deferral test arms");b.forced=c::CommandStatus::NeedCompute;
    c::dispatch(s,frame(b,s),backend);
    check(!s.physical_refutation&&b.attempts==0,"a fuel deferral neither commands nor refutes physical support");
    b.forced=c::CommandStatus::Applied;b.tick();c::dispatch(s,frame(b,s),backend);b.tick();
    auto changed=frame(b,s);changed.programme=99;const auto id=s.plan.id;c::dispatch(s,changed,backend);
    check(s.plan.id==id&&!s.physical_refutation&&b.returns==1,"changed programme cannot retag the retained plan or destroy its actual paid return");
}
}
int main() {
    complete_actual_chain();absent_or_interrupted_bank_readback();refusal_contact_and_same_input_fence();
    stale_commands_preserve_actual_effects();charged_channel_loss_cancels_before_return();
    stale_unknown_move_retains_only_observed_pose();
    quiet_home_wait_uses_actual_timing();
    observation_and_preparation_are_paid_and_finite();support_and_life_boundaries();
    std::printf("assignment cash service (pure, no SDK actions): %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
