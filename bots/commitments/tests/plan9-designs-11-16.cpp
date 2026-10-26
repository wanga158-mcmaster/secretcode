#include "../committed-capacity/src/live-policy.h"
#include "../successive-waves/src/live-policy.h"
#include "../lock-work-exchange/src/live-policy.h"
#include "../forbearance-frontier/src/live-policy.h"
#include "../capped-contest-investment/src/live-policy.h"
#include "../critical-mass-conversion/src/live-policy.h"
#include <cstdlib>
#include <iostream>
using namespace p9::live;
namespace {
int checks=0;
void check(bool value,const char* message) {
    ++checks;
    if(!value) {std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}
}
Actor actor(int pick,Champion type,Point position={1,1}) {
    Actor a;a.id=10+pick;a.pick=pick;a.type=type;a.position=position;
    a.visible=a.alive=a.ally=a.can_move=a.can_attack=a.can_cast=true;
    a.hp=a.max_hp=1000;a.strength=50;a.focus=50;a.range=1;a.cooldown={0,0};return a;
}
Group group(int id,Point at,Gems gems=30) {
    Group g;g.id=id;g.position=g.approach=at;
    g.observed=g.living=g.route_known=g.safe_solo=true;
    g.travel=1;g.to_bank=2;g.own_distance=2;g.enemy_distance=8;
    g.gems=gems;g.gold=20;g.hp=100;g.incoming=0;g.clear_turns=2;return g;
}
World world(int pick,const Profile& profile) {
    World w;w.round=1;w.safe_now=true;w.max_rounds=2000;w.win_gems=1000;
    w.capture_turns=5;w.windows={120,120,120};w.tribute_multiplier=.1;
    w.own_gold=20;w.defense_cost=w.lock_cost=10;
    w.self=actor(pick,profile.draft[pick]);w.self.at_spawn=true;
    for(int p=0;p<3;++p) if(p!=pick) w.allies[w.ally_count++]=actor(p,profile.draft[p]);
    w.groups[0]=group(1,{2,2});w.groups[0].contested=true;
    w.groups[1]=group(2,{8,8},80);w.groups[1].travel=2;
    w.groups[1].own_distance=8;w.groups[1].enemy_distance=2;w.group_count=2;
    w.altars[0]={1,{3,3},{3,2},true,true,false,false,2};w.altar_count=1;
    for(int p=0;p<3;++p) {
        auto& routes=w.routes[w.route_count++];routes.actor=10+p;
        routes.groups[0]=routes.groups[1]=1;routes.altars[0]=2;
    }
    return w;
}
Actor& ally(World& w,int pick) {
    for(int i=0;i<w.ally_count;++i) if(w.allies[i].pick==pick) return w.allies[i];
    std::abort();
}
void committed_capacity() {
    using namespace p9::committed_capacity;
    auto w=world(0,profile);ally(w,1).bag=30;
    Policy active,control;
    const auto purchase=active.step(w,Arm::active);
    check(purchase.task==Task::buy_defense,"11 funded protection purchase changes actual task");
    check(purchase.purchase_target==2,"11 executor receives the funded owned-total target");
    check(control.step(w,Arm::control).task==Task::independent,"11 control keeps ordinary production purchases");
    w.round=2;w.self.defense_shards=2;
    const auto escort=active.step(w,Arm::active);
    check(escort.task==Task::support&&escort.actor==11&&escort.group==1,"11 paid capacity changes escort and district");
    auto recipient=world(1,profile);recipient.self.bag=30;ally(recipient,0).defense_shards=2;
    Policy holder;
    check(holder.step(recipient,Arm::active).task==Task::farm,"11 loaded collector actually changes acquisition work with visible paid escort");
    recipient.round=2;recipient.last_payout_round=2;recipient.last_payout_group=1;recipient.self.bag=60;
    check(holder.step(recipient,Arm::active).task==Task::bank,"11 collector converts only actual subsequent payout");
    auto poor=world(0,profile);poor.own_gold=0;ally(poor,1).bag=30;Policy unfunded;
    check(unfunded.step(poor,Arm::active).task==Task::independent,"11 future income cannot fund capacity");
    auto absent=world(0,profile);absent.ally_count=0;Policy no_collector;
    check(no_collector.step(absent,Arm::active).task==Task::independent,"11 unseen collector gives no campaign");
}
void successive_waves() {
    using namespace p9::successive_waves;
    auto w=world(0,profile);Policy first,control;
    check(first.step(w,Arm::active).task==Task::farm,"12 first productive contested approach");
    control.step(w,Arm::control);w.round=8;
    check(first.step(w,Arm::active).task==Task::recover,"12 first wave exits at its declared eight-turn boundary");
    check(control.step(w,Arm::control).task==Task::farm,"12 control retains first wave for ordinary segment");
    auto r=world(1,profile);r.round=10;ally(r,0).position={2,2};r.routes[0].groups[0]=0;
    Actor enemy=actor(0,Champion::other,{2,3});enemy.id=99;enemy.ally=false;enemy.hp=400;
    r.enemies[0]=enemy;r.enemy_count=1;Policy second,second_control;
    check(second.step(r,Arm::active).group==2,"12 second collector initially works separate source");
    r.round=11;ally(r,0).position={3,2};r.routes[0].groups[0]=1;
    check(second.step(r,Arm::active).group==1,"12 actual visible departure plus current expenditure admits second wave");
    check(second_control.step(r,Arm::control).task==Task::independent,"12 control never sends second wave");
    r.round=12;r.enemy_count=0;
    check(second.step(r,Arm::active).task==Task::independent,"12 lost rival removes temporary-window premise");
    auto restored=world(1,profile);restored.round=10;ally(restored,0).position={2,2};restored.routes[0].groups[0]=0;
    restored.enemies[0]=enemy;restored.enemies[0].hp=1000;restored.enemy_count=1;Policy fresh;
    fresh.step(restored,Arm::active);restored.round=11;ally(restored,0).position={3,2};restored.routes[0].groups[0]=1;
    check(fresh.step(restored,Arm::active).group!=1,"12 restored readiness cannot borrow old competitive expenditure");
}
void lock_work_exchange() {
    using namespace p9::lock_work_exchange;
    auto w=world(0,profile);ally(w,1).bag=30;w.self.range=3;
    Actor enemy=actor(0,Champion::ren,{3,2});enemy.id=99;enemy.ally=false;
    enemy.channel=Channel::tribute;enemy.bag=20;w.enemies[0]=enemy;w.enemy_count=1;
    Policy active,control;
    check(active.step(w,Arm::active).task==Task::buy_lock,"13 funded service capital purchase");
    check(control.step(w,Arm::control).task==Task::intercept,"13 control spends legal interruption labor instead of gold");
    w.round=2;w.has_lock=true;
    check(active.step(w,Arm::active).task==Task::place_lock,"13 carried lock is physically placed");
    w.round=3;w.has_lock=false;w.altars[0].friendly_lock=true;
    check(active.step(w,Arm::active).task==Task::farm,"13 observed placement releases actor to production");
    w.round=128;w.altars[0].friendly_lock=false;
    check(active.step(w,Arm::active).task==Task::farm,"13 no second lock after service removal");
    auto poor=world(0,profile);ally(poor,1).bag=30;poor.own_gold=0;Policy no_funds;
    check(no_funds.step(poor,Arm::active).task==Task::independent,"13 lock never funded by future tribute");
    auto no_target=w;no_target.enemy_count=0;Policy labor;
    check(labor.step(no_target,Arm::control).task==Task::farm,"13 no eligible stun target falls back productively");
}
void lock_attribution_negative() {
    using namespace p9::lock_work_exchange;
    auto w=world(0,profile);ally(w,1).bag=30;w.has_lock=true;Policy p;
    check(p.step(w,Arm::active).task==Task::place_lock,"13 retained lock emits a real placement request");
    w.round=5;w.has_lock=false;w.altars[0].friendly_lock=true;
    check(p.step(w,Arm::active).task==Task::independent,"13 observation gap cannot turn another lock into own placement");
}
void forbearance_frontier() {
    using namespace p9::forbearance_frontier;
    auto w=world(1,profile);Policy active,control;
    check(active.step(w,Arm::active).group==1,"14 active respects home sphere without observation");
    check(control.step(w,Arm::control).group==2,"14 control puts same feasible pair into unconditional outer pressure");
    w.round=2;w.groups[0].cleared=true;w.groups[0].living=false;w.productive_group=1;
    Actor enemy=actor(0,Champion::other,{2,2});enemy.id=99;enemy.ally=false;
    w.enemies[0]=enemy;w.enemy_count=1;
    check(active.step(w,Arm::active).group==2,"14 current enemy occupancy of active cleared region triggers counterclaim");
    w.round=3;w.self.bag=30;
    check(active.step(w,Arm::active).task==Task::bank,"14 counterclaim banks actual inventory");
    w.round=4;w.self.bag=0;w.enemy_count=0;
    check(active.step(w,Arm::active).group==1,"14 returns to original home after one cycle");
    auto unseen=world(1,profile);unseen.productive_group=1;unseen.groups[0].cleared=true;Policy no_trigger;
    check(no_trigger.step(unseen,Arm::active).group==1,"14 missing payout or hidden rival is no violation");
}
void forbearance_renewal() {
    using namespace p9::forbearance_frontier;
    auto w=world(1,profile);Policy p;
    check(p.step(w,Arm::active).group==1,"14 renewal retains the initially observed home");
    w.round=2;w.groups[0].living=false;w.groups[0].respawning=true;
    w.groups[2]=group(3,{4,3},12);w.groups[2].travel=3;w.group_count=3;
    auto next=p.step(w,Arm::active);
#if P9_FORBEARANCE_RENEWAL
    check(next.group==3,"14 renewal uses safe observed own-side work during home cooldown");
#else
    check(next.group==1,"14 disabled renewal preserves the old fixed-source request");
#endif
    w.round=3;w.self.bag=4;
    check(p.step(w,Arm::active).task==Task::bank,"14 temporary home work preserves actual-bag conversion");
    w.round=4;w.self.bag=0;w.self.channel=Channel::recall;
    check(p.step(w,Arm::active).task==Task::wait,"14 renewal preserves an existing physical channel");
    w.round=5;w.self.channel=Channel::none;w.capture_active=true;w.productive_group=3;
    check(p.step(w,Arm::active).task==Task::capture,"14 renewal never releases an unfinished own capture");
    w.round=6;w.capture_active=false;w.groups[0].respawning=false;w.groups[0].living=true;
    check(p.step(w,Arm::active).group==1,"14 original home identity survives temporary work");
    w.round=7;w.groups[0].cleared=true;w.groups[0].living=false;
    Actor enemy=actor(0,Champion::other,{2,2});enemy.id=99;enemy.ally=false;
    w.enemies[0]=enemy;w.enemy_count=1;
    check(p.step(w,Arm::active).group==1,"14 temporary payout cannot license intrusion at the original source");
    w.round=8;w.productive_group=1;
    check(p.step(w,Arm::active).group==2,"14 fresh original-source intrusion still licenses the bounded counterclaim");
    auto refused=world(1,profile);Policy no_alternate;no_alternate.step(refused,Arm::active);
    refused.round=2;refused.groups[0].respawning=true;refused.groups[0].living=false;
    check(no_alternate.step(refused,Arm::active).group==1,"14 renewal does not replace home cooldown with enemy-side pressure");
}
void capped_contest() {
    using namespace p9::capped_contest_investment;
    auto w=world(0,profile);Policy active,control;
    check(active.step(w,Arm::active).group==1,"15 admits a feasible current contest");
    control.step(w,Arm::control);
    w.round=8;w.groups[0].clear_turns=40;w.groups[0].hp=2000;
    check(active.step(w,Arm::active).group==2,"15 eight actual turns compare remaining contest against productive alternative");
    check(control.step(w,Arm::control).group==1,"15 control continues same current contest");
    auto close=world(0,profile);close.groups[0].gems=200;Policy finish;
    finish.step(close,Arm::active);close.round=8;close.groups[0].contested=false;close.groups[0].hp=1;close.groups[0].clear_turns=1;
    check(finish.step(close,Arm::active).group==1,"15 nearly secured larger payout survives marginal comparison");
    close.round=9;close.capture_active=true;close.productive_group=1;
    check(finish.step(close,Arm::active).task==Task::capture,"15 reallocation never resets safe capture");
    close.round=20;close.capture_active=false;
    check(finish.step(close,Arm::active).task==Task::independent,"15 hard cap releases further contest labor");
}
void critical_mass() {
    using namespace p9::critical_mass_conversion;
    auto w=world(0,profile);w.score=900;w.groups[0].gems=100;Policy active,control;
    check(active.step(w,Arm::active).group==1,"16 actual current score and future payout change collection ownership");
    check(control.step(w,Arm::control).task==Task::independent,"16 control keeps ordinary complete-cycle allocation");
    w.round=2;w.score=0;
    check(active.step(w,Arm::active).task==Task::independent,"16 changed winning premise aborts future package");
    w.score=900;w.self.bag=100;
    check(active.step(w,Arm::active).task==Task::bank&&control.step(w,Arm::control).task==Task::bank,
        "16 both arms take actual currently loaded winning conversion");
    auto absent=world(0,profile);absent.score=900;absent.groups[0].gems=100;absent.ally_count=0;Policy alone;
    check(alone.step(absent,Arm::active).task==Task::independent,"16 unseen support cannot license terminal allocation");
    auto tavi=world(2,profile);tavi.score=900;tavi.groups[0].gems=100;Policy lock;
    check(lock.step(tavi,Arm::active).task==Task::buy_lock,"16 optional lock is funded by its actual independent owner");
    tavi.own_gold=0;Policy poor;
    check(poor.step(tavi,Arm::active).task==Task::independent,"16 anticipated winning payout cannot fund support capital");
}
}
int main() {
    committed_capacity();successive_waves();lock_work_exchange();lock_attribution_negative();forbearance_frontier();forbearance_renewal();capped_contest();critical_mass();
    std::cout<<"Plan9 designs 11-16: "<<checks<<" explicit checks passed\n";
}
