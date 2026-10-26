#include "../twin-deadlines/src/live-policy.h"
#include "../capture-fork/src/live-policy.h"
#include "../cross-type-fork/src/live-policy.h"
#include "../counterclaim/src/live-policy.h"
#include "../banked-vanguard/src/live-policy.h"
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {
using namespace p9::live;
int checks=0,cases=0;
void check(bool condition,std::string_view name) {
    ++checks;if(!condition) {std::cerr<<"FAIL P9 designs01-05: "<<name<<'\n';std::abort();}
}
Actor actor(int pick,const Profile& profile) {
    Actor a;a.id=10+pick;a.pick=pick;a.type=profile.draft[pick];
    a.position={10+pick%2,10+pick/2};a.visible=a.alive=a.ally=true;
    a.can_move=a.can_attack=a.can_cast=true;a.hp=a.max_hp=100;a.strength=20;a.focus=40;a.range=1;
    return a;
}
World fixture(const Profile& profile,int pick) {
    World w;w.round=3;w.picks=3;w.max_rounds=600;w.win_gems=1000;w.capture_turns=4;
    w.tribute_multiplier=.125;w.tribute_base=0;w.tribute_minimum=1;
    w.safe_now=true;w.windows={120,120,120};w.self=actor(pick,profile);
    for(int other=0;other<3;++other) if(other!=pick) w.allies[w.ally_count++]=actor(other,profile);
    for(int site=0;site<2;++site) {
        auto& g=w.groups[site];g.id=100+site*100;g.position={14,14-site*8};g.approach={13,13-site*6};
        g.observed=g.living=g.route_known=g.safe_solo=true;g.travel=g.to_bank=4;
        g.own_distance=4;g.enemy_distance=8;g.gems=10;g.gold=15;g.hp=20;g.incoming=1;g.clear_turns=2;
        auto& a=w.altars[site];a.id=30+site*10;a.position=site==0?Point{14,10}:Point{10,14};
        a.approach=site==0?Point{13,10}:Point{10,13};a.visible=a.route_known=true;a.travel=4;
    }
    w.group_count=w.altar_count=2;
    for(int other=0;other<3;++other) {
        auto& route=w.routes[w.route_count++];route.actor=10+other;
        route.altars={4,4};route.groups={4,4,-1,-1,-1,-1,-1,-1};
    }
    return w;
}
Actor& pick(World& w,int ordinal) {
    if(w.self.pick==ordinal) return w.self;
    for(int i=0;i<w.ally_count;++i) if(w.allies[i].pick==ordinal) return w.allies[i];
    std::abort();
}
void twin_deadlines() {
    ++cases;auto w=fixture(p9::twin_deadlines::profile,0);w.self.bag=16;pick(w,1).bag=16;
    p9::twin_deadlines::Policy active,control;
    const auto a=active.step(w,Arm::active),c=control.step(w,Arm::control);
    check(a.task==Task::bank&&a.altar==30&&a.until==12&&a.strategic,"Twin active physically schedules its own first altar");
    check(c.task==Task::bank&&c.altar==30&&c.until<0,"Twin control uses same route and banks when ready");
    auto second=fixture(p9::twin_deadlines::profile,1);second.self.bag=16;pick(second,0).bag=16;
    p9::twin_deadlines::Policy second_policy;
    check(second_policy.step(second,Arm::active).altar==40,"second collector retains a distinct ascending-ID altar");
    ++cases;auto missing=w;pick(missing,1).visible=false;p9::twin_deadlines::Policy no_peer;
    const auto blocked=no_peer.step(missing,Arm::active);
    check(blocked.task==Task::independent&&blocked.until<0&&!blocked.strategic,"unseen collector resumes shared productive economy without synchronized bank");
    ++cases;auto unknown=w;unknown.routes[1].altars[1]=-1;p9::twin_deadlines::Policy no_route;
    check(no_route.step(unknown,Arm::active).until<0,"partner route must be an actual represented route");
    ++cases;w.round=13;const auto late=active.step(w,Arm::active);
    check(late.task==Task::independent&&late.until<0,"missed target resumes productive economy without restarting a slot");
    ++cases;auto too_early=fixture(p9::twin_deadlines::profile,0);too_early.round=0;too_early.self.bag=16;pick(too_early,1).bag=16;
    too_early.altars[0].travel=0;p9::twin_deadlines::Policy too_much_wait;
    check(too_much_wait.step(too_early,Arm::active).until<0,"more than eight predicted waiting turns rejects admission");
    ++cases;auto channel=w;channel.self.channel=Channel::tribute;
    check(active.step(channel,Arm::active).task==Task::independent,"controller never resets an active tribute");
}
void twin_campaign_caps() {
    ++cases;auto w=fixture(p9::twin_deadlines::profile,0);w.self.bag=16;pick(w,1).bag=16;
    p9::twin_deadlines::Policy policy;
    check(policy.step(w,Arm::active).strategic,"first campaign admitted");
    w.round=4;w.safe_now=false;(void)policy.step(w,Arm::active);
    w.safe_now=true;w.round=19;
    check(policy.step(w,Arm::active).strategic,"a second bounded attempt may use the same epoch");
    w.round=20;w.safe_now=false;(void)policy.step(w,Arm::active);w.safe_now=true;w.round=35;
    check(!policy.step(w,Arm::active).strategic,"two attempts exhaust their shared epoch allowance");
    w.round=67;check(policy.step(w,Arm::active).strategic,"second campaign can start in a new epoch");
    w.round=68;w.safe_now=false;(void)policy.step(w,Arm::active);w.safe_now=true;w.round=131;
    check(policy.step(w,Arm::active).strategic,"later epochs renew the campaign allowance");
    ++cases;auto long_channel=fixture(p9::twin_deadlines::profile,0);long_channel.self.bag=16;pick(long_channel,1).bag=16;
    p9::twin_deadlines::Policy overrun;(void)overrun.step(long_channel,Arm::active);
    long_channel.round=12;long_channel.self.channel=Channel::tribute;(void)overrun.step(long_channel,Arm::active);
    long_channel.round=20;long_channel.self.channel=Channel::none;long_channel.self.bag=0;
    long_channel.own_bank_completed=true;long_channel.last_bank_round=20;(void)overrun.step(long_channel,Arm::active);
    long_channel.round=35;long_channel.self.bag=16;long_channel.own_bank_completed=false;
    check(!overrun.step(long_channel,Arm::active).strategic,"safe completion overrun disables the current epoch");
    long_channel.round=67;
    check(overrun.step(long_channel,Arm::active).strategic,"overrun does not disable later epochs forever");
}
void twin_continuation_and_visibility() {
    // Conditional production-policy worlds, not native runtime/private-state reconstructions.
    // The retained Collision loaded meeting lacks the OTHER visible altar; test that exact
    // necessary condition without calling a proposal a physical native activation.
    for(int role=0;role<2;++role) {
        ++cases;auto w=fixture(p9::twin_deadlines::profile,role);
        w.self.bag=16;pick(w,1-role).bag=16;
        p9::twin_deadlines::Policy active,control;
        const auto admitted=active.step(w,Arm::active),ordinary=control.step(w,Arm::control);
        check(admitted.task==Task::bank&&admitted.altar==30+role*10&&admitted.until==12&&admitted.strategic,
            "both separate policy instances adopt distinct altars and the same fixed deadline");
        check(ordinary.task==Task::bank&&ordinary.altar==admitted.altar&&ordinary.until<0,
            "matched control keeps the assigned altar while removing synchronized waiting");
        w.round=7;w.altars[role].travel=0;w.self.position=w.altars[role].approach;
        const auto waiting=active.step(w,Arm::active);
        check(waiting.task==Task::bank&&waiting.altar==admitted.altar&&waiting.until==12&&waiting.strategic,
            "arrived collector retains its deadline through continuation");
        w.round=12;
        check(active.step(w,Arm::active).until==12,"deadline arrival does not create a later target");
        w.round=14;w.self.bag=0;w.own_bank_completed=true;w.last_bank_round=14;
        const auto renewed=active.step(w,Arm::active);
        check(renewed.task==Task::independent&&!renewed.strategic&&renewed.until<0&&
              std::string_view(renewed.reason)=="twin-renew-after-own-bank",
              "own bank releases the campaign to ordinary work rather than holding the empty collector");
        w.round=19;w.self.bag=16;w.own_bank_completed=false;
        const auto at_goal=active.step(w,Arm::active);
        check(w.self.position==w.altars[role].approach&&w.altars[role].travel==0&&
              at_goal.task==Task::independent&&!at_goal.strategic&&at_goal.until<0&&
              std::string_view(at_goal.reason)=="twin-route-or-wait-unavailable",
              "remaining at the altar goal rejects nine waiting turns rather than fabricating travel");
        // A new conditional staging premise restores ALL actor positions and route rows.
        // Preserve the SAME policy's completed campaign and the last own-bank fact;
        // no native path, collection history or autonomous joint reachability is claimed.
        auto later=fixture(p9::twin_deadlines::profile,role);later.round=19;
        later.self.bag=16;pick(later,1-role).bag=16;
        later.last_bank_round=14;later.own_bank_completed=false;
        const auto second_admission=active.step(later,Arm::active);
        check(distance(later.self.position,later.altars[role].position)>1&&later.altars[role].travel>0&&
              second_admission.task==Task::bank&&second_admission.strategic&&second_admission.until==28,
              "same completed policy can renew under a fresh coherent conditional staging premise");

        ++cases;auto one_altar=fixture(p9::twin_deadlines::profile,role);
        one_altar.self.bag=16;pick(one_altar,1-role).bag=16;one_altar.altars[1-role].visible=false;
        p9::twin_deadlines::Policy missing_other;
        const auto refused=missing_other.step(one_altar,Arm::active);
        check(refused.task==Task::independent&&!refused.strategic&&refused.until<0&&
              std::string_view(refused.reason)=="twin-route-or-wait-unavailable",
              "hidden OTHER altar prevents adoption despite own visible altar and personal loaded meeting");
    }
    ++cases;auto lost=fixture(p9::twin_deadlines::profile,0);lost.self.bag=16;pick(lost,1).bag=16;
    p9::twin_deadlines::Policy interrupted;(void)interrupted.step(lost,Arm::active);
    lost.round=4;lost.altars[0].visible=false;
    const auto exited=interrupted.step(lost,Arm::active);
    check(exited.task==Task::independent&&!exited.strategic&&exited.until<0,
          "loss of current assigned altar observation exits the commitment safely");
}
void capture_fork() {
    ++cases;auto w=fixture(p9::capture_fork::profile,1);
    p9::capture_fork::Policy active,control;
    const auto a=active.step(w,Arm::active),c=control.step(w,Arm::control);
    check(a.task==Task::farm&&a.group==200,"Capture Fork sends second holder to the second real source");
    check(c.task==Task::farm&&c.group==100,"control keeps clearance serial at the first source");
    w.round=12;w.groups[1].living=false;w.groups[1].cleared=true;
    const auto hold=active.step(w,Arm::active);
    check(hold.task==Task::capture&&hold.group==200&&hold.until==12,"second holder executes its own fixed capture window");
    ++cases;auto missing=fixture(p9::capture_fork::profile,1);pick(missing,2).visible=false;
    p9::capture_fork::Policy no_holder;
    check(no_holder.step(missing,Arm::active).task==Task::independent,"unseen future holder rejects the fork and resumes shared independent work");
    ++cases;auto clearer=fixture(p9::capture_fork::profile,0);p9::capture_fork::Policy release;
    (void)release.step(clearer,Arm::active);clearer.round=4;clearer.groups[0].living=false;clearer.groups[0].cleared=true;
    check(release.step(clearer,Arm::active).task==Task::capture,"clearer finishes safely when the handoff is not visible at the site");
    auto handoff=fixture(p9::capture_fork::profile,0);p9::capture_fork::Policy visible_release;
    (void)visible_release.step(handoff,Arm::active);handoff.round=4;
    handoff.groups[0].living=false;handoff.groups[0].cleared=true;
    pick(handoff,2).position=handoff.groups[0].position;
    check(visible_release.step(handoff,Arm::active).group==200,"personally observed holder permits real clearer redeployment");
    pick(clearer,2).position=clearer.groups[0].position;clearer.round=5;
    check(!release.step(clearer,Arm::active).strategic,"failed handoff retires rather than privately restarting the fork");
    ++cases;clearer.capture_active=true;
    check(release.step(clearer,Arm::active).task==Task::independent,"valid existing capture is never reset to synchronize");
}
void cross_type_fork() {
    ++cases;auto w=fixture(p9::cross_type_fork::profile,0);pick(w,1).bag=16;
    p9::cross_type_fork::Policy active,control;
    const auto a=active.step(w,Arm::active),c=control.step(w,Arm::control);
    check(a.task==Task::farm&&a.until==12&&a.strategic,"acquisition starts inside the fixed bank window");
    check(c.task==Task::independent&&!c.strategic,"control defers that named acquisition until visible conversion readiness");
    ++cases;auto converter=fixture(p9::cross_type_fork::profile,1);converter.self.bag=16;
    p9::cross_type_fork::Policy bank_active,bank_control;
    check(bank_active.step(converter,Arm::active).until==12&&bank_control.step(converter,Arm::control).until<0,
        "loaded converter is an independently executable second project");
    ++cases;auto helper=fixture(p9::cross_type_fork::profile,2);pick(helper,1).bag=16;
    p9::cross_type_fork::Policy helper_policy;
    check(helper_policy.step(helper,Arm::active).task==Task::support,"distinct visible helper physically joins acquisition");
    ++cases;auto missing=w;pick(missing,1).visible=false;p9::cross_type_fork::Policy unavailable;
    check(!unavailable.step(missing,Arm::active).strategic,"hidden bank participant cannot license parallel acquisition");
    ++cases;auto blocked=w;blocked.routes[1].altars[0]=-1;blocked.routes[1].altars[1]=-1;
    p9::cross_type_fork::Policy no_route;
    check(!no_route.step(blocked,Arm::active).strategic,"unrepresented converter routes reject the cross-type organization");
    ++cases;auto too_long=w;too_long.groups[0].clear_turns=20;p9::cross_type_fork::Policy long_acquisition;
    check(!long_acquisition.step(too_long,Arm::active).strategic,"a known acquisition cannot exceed the declared segment cap");
}
void counterclaim() {
    auto make=[] {
        auto w=fixture(p9::counterclaim::profile,0);pick(w,2).bag=16;pick(w,2).position={11,10};
        auto& enemy=w.enemies[0];enemy=actor(0,p9::counterclaim::profile);enemy.id=90;enemy.ally=false;
        enemy.position={13,10};enemy.hp=20;enemy.strength=3;enemy.bag=20;enemy.range=1;w.enemy_count=1;return w;
    };
    ++cases;auto w=make();p9::counterclaim::Policy active,control;
    const auto a=active.step(w,Arm::active),c=control.step(w,Arm::control);
    check(a.task==Task::farm&&a.strategic,"active force retains a productive regional option");
    check(c.task==Task::intercept&&c.actor==90,"control commits to one exclusive favorable task");
    w.round=4;w.enemies[0].position={12,10};
    const auto escort=active.step(w,Arm::active),exclusive=control.step(w,Arm::control);
    check(escort.task==Task::support&&escort.actor==12,"observed hostile approach selects converter escort");
    check(exclusive.task==Task::intercept,"control keeps its exclusive proposal-time allocation");
    ++cases;auto departure=make();p9::counterclaim::Policy intercept;(void)intercept.step(departure,Arm::active);
    departure.round=4;departure.enemies[0].position={14,10};
    check(intercept.step(departure,Arm::active).task==Task::intercept,"personally observed vulnerable departure selects interception");
    ++cases;auto lost=make();p9::counterclaim::Policy missing;(void)missing.step(lost,Arm::active);
    lost.round=4;pick(lost,2).visible=false;
    check(!missing.step(lost,Arm::active).strategic,"lost converter contact terminates contingent pursuit");
    ++cases;auto overflow=make();overflow.extra_opposition=true;p9::counterclaim::Policy uncertain;
    check(!uncertain.step(overflow,Arm::active).strategic,"unrepresented opposition never creates a favorable option");
    ++cases;auto gap=make();p9::counterclaim::Policy nonconsecutive;(void)nonconsecutive.step(gap,Arm::active);
    gap.round=5;gap.enemies[0].position={14,10};
    check(nonconsecutive.step(gap,Arm::active).task==Task::farm,"a gap cannot establish a vulnerable departure");
}
void banked_vanguard() {
    auto make=[](int pick_id=0) {
        auto w=fixture(p9::banked_vanguard::profile,pick_id);w.groups[0].contested=true;
        pick(w,0).bag=32;pick(w,0).focus=60;return w;
    };
    ++cases;auto w=make();p9::banked_vanguard::Policy active,control;
    const auto a=active.step(w,Arm::active),c=control.step(w,Arm::control);
    check(a.task==Task::bank,"Prism actively liquidates his actual theft prize before redeployment");
    check(c.task==Task::farm&&c.group==100&&c.strategic,"control deploys the same loaded Prism with future collector responsibility");
    w.round=4;w.self.bag=0;w.self.focus=40;w.own_bank_completed=true;w.last_bank_round=4;
    w.bank_lower=32;w.bank_upper=-1;
    const auto support=active.step(w,Arm::active);
    check(support.task==Task::support&&support.actor==12&&support.group==100,
        "own whole-bag lower receipt and actual weaker Focus permit real support deployment");
    ++cases;auto loss=make();p9::banked_vanguard::Policy no_receipt;(void)no_receipt.step(loss,Arm::active);
    loss.round=4;loss.self.bag=0;loss.self.focus=40;
    check(!no_receipt.step(loss,Arm::active).strategic,"bag disappearance does not prove a bank");
    ++cases;auto partial=make();p9::banked_vanguard::Policy partial_receipt;(void)partial_receipt.step(partial,Arm::active);
    partial.round=4;partial.self.bag=0;partial.self.focus=40;partial.own_bank_completed=true;
    partial.last_bank_round=4;partial.bank_lower=31;partial.bank_upper=40;
    check(!partial_receipt.step(partial,Arm::active).strategic,"insufficient proven whole-bag conversion rejects role change");
    ++cases;auto weak=make();p9::banked_vanguard::Policy unsafe;(void)unsafe.step(weak,Arm::active);
    weak.round=4;weak.self.bag=0;weak.self.focus=10;weak.self.hp=20;weak.self.strength=1;
    weak.own_bank_completed=true;weak.last_bank_round=4;weak.bank_lower=32;weak.groups[0].incoming=5;
    check(!unsafe.step(weak,Arm::active).strategic,"fresh post-bank actor must remain independently safe");
    ++cases;auto collector=make(1);pick(collector,0).bag=0;pick(collector,0).focus=40;
    collector.groups[0].living=false;collector.groups[0].cleared=true;
    p9::banked_vanguard::Policy future_collector,control_collector;
    check(future_collector.step(collector,Arm::active).task==Task::capture,
        "different named Marcella receives the future whole capture duty");
    check(control_collector.step(collector,Arm::control).task!=Task::capture,"control keeps Marcella independent");
    ++cases;auto cold=make();cold.self.bag=0;cold.self.focus=40;p9::banked_vanguard::Policy cold_start;
    check(!cold_start.step(cold,Arm::active).strategic,"zero-bag cold start has no invented personal bank receipt");
}
void profiles() {
    ++cases;
    for(const auto& profile:{p9::twin_deadlines::profile,p9::capture_fork::profile,p9::cross_type_fork::profile,
        p9::counterclaim::profile,p9::banked_vanguard::profile}) {
        check(profile.design>=1&&profile.design<=5,"actual card profile identity");
        check(profile.bank_batch>=16&&profile.bank_batch<=48&&profile.capital_recall_shards>=6&&profile.capital_recall_shards<=14,
            "economic pacing has bounded active caller values");
        for(int i=0;i<5;++i) for(int j=0;j<i;++j) check(profile.draft[i]!=profile.draft[j],"every three/four/five draft prefix is unique");
    }
    check(p9::banked_vanguard::profile.draft[0]==Champion::prism,"Prism passive remains mandatory for Banked Vanguard");
}
void epoch_boundaries() {
    ++cases;Campaign campaign;auto w=fixture(p9::twin_deadlines::profile,0);w.round=63;
    check(campaign.start(w),"campaign may start before an epoch boundary");
    w.round=64;campaign.tick(w,true);
    check(!campaign.may_start(w)&&campaign.used==1,"an active segment cannot reset its allowance at an epoch boundary");
    campaign.finish();check(campaign.start(w),"an inactive campaign may renew at the new epoch");
    campaign.finish();++w.round;check(campaign.start(w),"two starts fit the new epoch allowance");
    campaign.finish();++w.round;check(!campaign.start(w),"third start is blocked within its epoch");
    w.round=128;w.safe_now=false;check(!campaign.start(w),"a new epoch does not waive current safety");
    w.safe_now=true;check(campaign.start(w),"safe later epochs remain available");
}
}
int main() {
    twin_deadlines();twin_campaign_caps();twin_continuation_and_visibility();capture_fork();cross_type_fork();counterclaim();banked_vanguard();profiles();epoch_boundaries();
    std::cout<<"Plan9 designs01-05: "<<cases<<" cases, "<<checks<<" checks passed\n";
}
